/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "ServerLib.h"
#include "objTransactions.h"
#include "FakeGameServer.h"
#include "DatabaseTest.h"
#include "sysutil.h"
#include "StringUtil.h"
#include "GlobalStateMachine.h"
#include "ControllerScriptingSupport.h"
#include "rand.h"
#include "utilitiesLib.h"

#include "Autogen/ObjectDB_autogen_remotefuncs.h"


FakeGameServerState gFGSState;




typedef struct ContainerInfo
{
	ContainerID id;
	bool isLocal;
	bool inTransit;
} ContainerInfo;

ContainerInfo *infoList;

static int waitingOn;
static int currentTransaction;
static bool bTestInProgress = false;

void fgsMoveResultCB(TransactionReturnVal *returnVal, void *userData)
{
	ContainerInfo *info = (ContainerInfo *)userData;
	if (returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{	
		ControllerScript_Failedf("Move Transaction on %d failed with %s",info->id,returnVal->pBaseReturnVals[0].returnString);
		bTestInProgress = false;
	}

	//	filelog_printf("transactions.log","RESPONSE%d %s[%d]: %s\n",i,GlobalTypeToName(GLOBALTYPE_ENTITYPLAYER),id,tracked[i].returnVal.pBaseReturnVals[0].returnString);

	info->inTransit = false;
	waitingOn--;
}

void fgsModifyResultCB(TransactionReturnVal *returnVal, void *userData)
{
	ContainerInfo *info = (ContainerInfo *)userData;
	if (returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{	
		ControllerScript_Failedf("Modify Transaction on %d failed with %s",info->id,returnVal->pBaseReturnVals[0].returnString);
		bTestInProgress = false;
	}
	//	filelog_printf("transactions.log","RESPONSE%d %s[%d]: %s\n",i,GlobalTypeToName(GLOBALTYPE_ENTITYPLAYER),id,tracked[i].returnVal.pBaseReturnVals[0].returnString);
	waitingOn--;
}

bool RequestLogon(ContainerID id, bool response)
{
	TransactionReturnVal *returnVal = NULL;
	infoList[id].isLocal = true;
	infoList[id].inTransit = true;

	if (response)
	{
		returnVal = objCreateManagedReturnVal(fgsMoveResultCB,&infoList[id]);
		waitingOn++;
	}

	objRequestContainerMove(returnVal, GLOBALTYPE_ENTITYPLAYER, id, GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID());
	return true;
}

bool RequestLogoff(ContainerID id, bool response)
{
	TransactionReturnVal *returnVal = NULL;
	infoList[id].isLocal = false;
	infoList[id].inTransit = true;

	if (response)
	{
		returnVal = objCreateManagedReturnVal(fgsMoveResultCB,&infoList[id]);
		waitingOn++;	
	}

	objRequestContainerMove(returnVal, GLOBALTYPE_ENTITYPLAYER, id, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);
	return true;
}

int RequestModify(ContainerID id, bool response)
{
	static modifyInited = false;
	//static NamedPathQuery **modifyQueries;
	int choice;
	
	//if (!modifyInited)
	//{
	//	int i;

	//	for (i = 0; i < 100; i++)
	//	{
	//		char modifyName[128];
	//		NamedPathQuery *query;
	//		sprintf(modifyName, "ModifyCharacter%d", i+1);
	//		query = objGetNamedQuery(modifyName);
	//		if (!query)
	//			break;
	//		eaPush(&modifyQueries, query);
	//	}
	//	assert(i!= 0);
	//	modifyInited = true;	
	//}
	//
	//choice = randomIntRange(0, eaSize(&modifyQueries) - 1);

	choice = randomIntRange(0, eaSize(&gDBQueryList->eaList) - 1);

	assert(gDBQueryList);

	if (response)
	{
		TransactionReturnVal *returnVal;
		U32 type = 0;
		char *estr = NULL;
		char *start = gDBQueryList->eaList[choice];
		start = strchr(start, ' ') + 1;
		assert ( sscanf_s(start, "%u %u ", &type, &id) == 2 );
		start = strchr(start, ' ') + 1;

		estrStackCreate(&estr);
		estrPrintf(&estr,"BatchUpdate %s %s", GlobalTypeToName(type), start);

		returnVal = objCreateManagedReturnVal(fgsModifyResultCB,&infoList[id]);
		waitingOn++;
		//objRequestTransactionSimple(returnVal, type, id, start);
		objRequestTransactionSimple(returnVal, GLOBALTYPE_OBJECTDB, 0, "RequestModify", estr);

		estrDestroy(&estr);

		return StringCountLines(start);
	}
	else
	{
		U32 type = 0;
		//char *estr = NULL;
		char *start = gDBQueryList->eaList[choice];
		start = strchr(start, ' ') + 1;
		assert ( sscanf_s(start, "%u %u ", &type, &id) == 2 );
		start = strchr(start, ' ') + 1;
		start = strchr(start, ' ') + 1;

		//estrStackCreate(&estr);
		//estrPrintf(&estr,"BatchUpdate %s %s", GlobalTypeToName(type), start);
		//objRequestTransactionSimple(NULL, GLOBALTYPE_OBJECTDB, 0, estr);
		//estrDestroy(&estr);

		RemoteCommand_DBUpdateNonTransactedData(GLOBALTYPE_OBJECTDB,0,GLOBALTYPE_ENTITYPLAYER,id,start);

		//objRequestTransactionSimple(NULL, GLOBALTYPE_ENTITYPLAYER, id, modifyQueries[choice]->queryString);
		return StringCountLines(start);
	}
}

#define MOVE_CHANCE 0 //chance, out of a 100, to move a container instead of modifying it
#define RESPONSE_CHANCE 0 //chance, out of a 100, that a transaction needs a response
#define OFFLINE_CHANCE 100 //chance, out of a 100, to do a transaction on a possibly offline character

extern FILE *trDebugLogFile;

AUTO_COMMAND;
void GameServerTransactionTest(void)
{
	int success;
	int i;

	if (infoList)
	{
		SAFE_FREE(infoList);
	}
	infoList = calloc(sizeof(ContainerInfo),gDBTestState.totalUsers);

	// Log on a bunch of users
	for (i = 0; i < gDBTestState.concurrentUsers; i++)
	{
		ContainerID id = i+1;

		infoList[id].id = id;
		success = RequestLogon(id,1);

		if (!success)
		{
			ControllerScript_Failedf("Failed to request transaction on container %d",id);
			return;
		}
	}

	waitingOn = 0;
	currentTransaction = 0;
	bTestInProgress = true;

}

AUTO_COMMAND;
void LocalTransactionTest(void)
{
	int success;
	int i;
	int commands = 0;
	S64 start = timeMsecsSince2000();
	printf("Starting local transaction test...\n");

	if (infoList)
	{
		SAFE_FREE(infoList);
	}
	infoList = calloc(sizeof(ContainerInfo),gDBTestState.totalUsers);

	for (i = 0; i < gDBTestState.totalTransactions; i++)
	{
		ContainerID id = (rand() % gDBTestState.totalUsers)+1;
		infoList[id].id = id;

		success = RequestModify(id,0);

		if (!success)
		{
			ControllerScript_Failedf("Failed to request transaction on container %d",id);
			printf("Failed to request transaction on container %d\n",id);
			return;
		}
		else
		{
			commands += success;
		}

		if (i % 1000 == 0)
		{
			printf("[%d] transactions dispatched\r", i);
		}
	}

	ControllerScript_Succeeded();

	if (gDBTestState.totalTransactions)
	{
		S64 timems = timeMsecsSince2000() - start;
		printf("%d transactions (%d commands) dispatched in %5.2f seconds\n%f trans/sec; %f commands/sec\n",
			gDBTestState.totalTransactions, commands, timems/1000.0f,
			gDBTestState.totalTransactions*1000.0f/timems, commands*1000.0f/timems);
	}
	printf("done!\n");
	Sleep(10000);
	
}


void fgsInit(void)
{
	char *pErrorString = NULL;
	loadstart_printf("Connecting to Transaction server... ");

	if (InitObjectTransactionManager(
		GetAppGlobalType(),gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, &pErrorString))
	{
		loadend_printf("connected to %s.",gServerLibState.transactionServerHost);
	}
	else
	{
		loadend_printf("failed. No transaction support.  %s", pErrorString);
		estrDestroy(&pErrorString);
	}
	objLoadAllGenericSchemas();
	objLoadNamedQueries("server/queries/databaseTest/", ".query", "DatabaseTestQueries.bin");
}


void fgsTick(void)
{
	static char *tempTitleString;
	char fullString[1024];

	static int frameTimer;
	F32 frametime;
	if (!frameTimer)
		frameTimer = timerAlloc();
	frametime = timerElapsedAndStart(frameTimer);
	utilitiesLibOncePerFrame(frametime, 1);
	serverLibOncePerFrame();
	commMonitor(commDefault());

	GSM_PutFullStateStackIntoEString(&tempTitleString);

	sprintf(fullString, "%s %s", GlobalTypeToName(GetAppGlobalType()),tempTitleString);

	setConsoleTitle(fullString);


	if (bTestInProgress)
	{
		int success;
		if (currentTransaction < gDBTestState.totalTransactions)
		{
			int i;
			for (i = 0; i < 500 && currentTransaction< gDBTestState.totalTransactions; i++, currentTransaction++)
			{			
				ContainerID id;
				bool bResponseNeeded;
				if (rand()%100 < OFFLINE_CHANCE)
				{
					id = (rand() % gDBTestState.totalUsers)+1;
				}
				else
				{
					id = (rand() % gDBTestState.concurrentUsers)+1;
				}

				bResponseNeeded = (rand()%100 < RESPONSE_CHANCE) || currentTransaction == gDBTestState.totalTransactions - 1;
				infoList[id].id = id;
				if (rand()%100 >= MOVE_CHANCE || infoList[id].inTransit)
				{
					success = RequestModify(id,bResponseNeeded);
				}
				else
				{
					if (infoList[id].isLocal)
					{
						success = RequestLogoff(id,1);
					}
					else
					{
						success = RequestLogon(id,1);
					}
				}
				if (!success)
				{
					ControllerScript_Failedf("Failed to request transaction on container %d",id);
					return;
				}
			}
		}
		if (!waitingOn)
		{
			bTestInProgress = 0;
			ControllerScript_Succeeded();
			return;
		}
	}
	Sleep(DEFAULT_SERVER_SLEEP_TIME);

}

void fgsShutdown(void)
{

}

void fgsWaiting_BeginFrame(void)
{
	static char *tempTitleString;
	char fullString[1024];

	static int frameTimer;
	F32 frametime;
	if (!frameTimer)
		frameTimer = timerAlloc();
	frametime = timerElapsedAndStart(frameTimer);
	utilitiesLibOncePerFrame(frametime, 1);
	serverLibOncePerFrame();
	commMonitor(commDefault());

	GSM_PutFullStateStackIntoEString(&tempTitleString);

	sprintf(fullString, "%s %s", GlobalTypeToName(GetAppGlobalType()),tempTitleString);

	setConsoleTitle(fullString);

	Sleep(DEFAULT_SERVER_SLEEP_TIME);
}

void fgsTesting_BeginFrame(void)
{
	fgsTick();
}

AUTO_RUN;
void fgsInitStates(void)
{
	GSM_AddGlobalState(FGSSTATE_WAITING);
	GSM_AddGlobalStateCallbacks(FGSSTATE_WAITING,NULL,fgsWaiting_BeginFrame,NULL,NULL);

	GSM_AddGlobalState(FGSSTATE_TESTING);
	GSM_AddGlobalStateCallbacks(FGSSTATE_TESTING,NULL,fgsTesting_BeginFrame,NULL,NULL);
}