#include "controller.h"
#include "../../CrossRoads/common/autogen/GameServerLib_autogen_RemoteFuncs.h"
#include "../Common/AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "../../CrossRoads/common/autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "../../libs/common/autogen/ServerLib_autogen_RemoteFuncs.h"
#include "TimedCallback.h"
#include "TextParser.h"
#include "Controller_utils_c_ast.h"
#include "httpXpathSupport.h"
#include "file.h"
#include "utils.h"
#include "serverlib.h"
#include "HashFunctions.h"
#include "fileUtil2.h"
#include "stringCache.h"
#include "winInclude.h"
#include "Crypt.h"
#include "sysUtil.h"
#include "fileUtil.h"
#include "patchclient.h"
#include "patchtrivia.h"
#include "controller_utils.h"
#include "alerts.h"
#include "MemLeakTracking.h"
#include "process_util.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "StatusReporting.h"
#include "Controller_MachineGroups.h"
#include "IntFIFO.h"
#include "memorypool.h"
#include "utilitiesLib.h"
#include "Controller_PCLStatusMonitoring.h"
#include "SentryServerComm.h"
#include "structNet.h"
#include "zutils.h"
#include "ResourceInfo.h"
#include "Message.h"

#include "AutoGen/Controller_autogen_SlowFuncs.h"
#include "autogen/Controllerpub_h_ast.h"
#include "NotesServerComm.h"
#include "NotesServer_pub_h_ast.h"
#include "logging.h"
#include "rand.h"
#include "Controller_ShardCluster.h"
#include "Controller_ClusterController.h"



bool Confirm(char *pString);


static PointerFIFO *spMainLoginQueue = NULL;
static PointerFIFO *spVIPLoginQueue = NULL;

void ApplyFrankenbuildInternal(GlobalType eServerType, char *p32BitShortExeName, char *p64BitShortExeName, char *pComment, char **ppOutErrorString);

void AddFrankenBuildReport(FORMAT_STR const char *pFmt, ...);

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct IDsOfPlayerInLoginQueue
{
	ContainerID iLoginServerID;
	U64 iLoginServerCookie;
	
	U32 iQueueID; //ever-incrementing IDs assigned as people join the queue, allowing us to return our "now serving, # foo" commands
		//seperately kept for main and VIP queues	

	U32 iTimeAdded;
} IDsOfPlayerInLoginQueue;

MP_DEFINE(IDsOfPlayerInLoginQueue);


AUTO_COMMAND;
char *BroadcastMessage(ACMD_SENTENCE pMessage)
{
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_GAMESERVER];
	while (pServer)
	{
		RemoteCommand_RemoteObjBroadcastMessage(GLOBALTYPE_GAMESERVER, pServer->iContainerID, "Shard-global Message", pMessage);

		pServer = pServer->pNext;
	}

	pServer = gpServersByType[GLOBALTYPE_GATEWAYSERVER];
	while (pServer)
	{
		RemoteCommand_gslGatewayServer_BroadcastMessage(GLOBALTYPE_GATEWAYSERVER, pServer->iContainerID, "Shard-global Message", pMessage);

		pServer = pServer->pNext;
	}

	pServer = gpServersByType[GLOBALTYPE_LOGINSERVER];
	while (pServer)
	{
		RemoteCommand_RemoteObjBroadcastMessage(GLOBALTYPE_LOGINSERVER, pServer->iContainerID, "Shard-global Message", pMessage);

		pServer = pServer->pNext;
	}

	return "Message broadcast";
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(4);
void BroadcastMessageEx(MessageStruct *val)
{
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_GAMESERVER];
	while (pServer)
	{
		RemoteCommand_RemoteObjBroadcastMessageEx(GLOBALTYPE_GAMESERVER, pServer->iContainerID, "Shard-global Message", val);
		pServer = pServer->pNext;
	}
}

AUTO_COMMAND;
void BroadcastMessageToMachine(char *pMachineName, ACMD_SENTENCE pMessage)
{
	TrackedMachineState *pMachine = FindMachineByName(pMachineName);

	if (pMachine)
	{
		TrackedServerState *pServer = pMachine->pServersByType[GLOBALTYPE_GAMESERVER];
		while (pServer && pServer->pMachine == pMachine)
		{
			RemoteCommand_RemoteObjBroadcastMessage(GLOBALTYPE_GAMESERVER, pServer->iContainerID, "Machine-global Message", pMessage);
			pServer = pServer->pNext;
		}

		pServer = pMachine->pServersByType[GLOBALTYPE_GATEWAYSERVER];
		while (pServer && pServer->pMachine == pMachine)
		{
			RemoteCommand_gslGatewayServer_BroadcastMessage(GLOBALTYPE_GATEWAYSERVER, pServer->iContainerID, "Machine-global Message", pMessage);
			pServer = pServer->pNext;
		}
	}
}

AUTO_COMMAND;
void BroadcastMessageToGameServer(ContainerID iContainerID, ACMD_SENTENCE pMessage)
{
	RemoteCommand_RemoteObjBroadcastMessage(GLOBALTYPE_GAMESERVER, iContainerID, "Server-global Message", pMessage);
}

AUTO_COMMAND;
void TimerRecordStartAutoRemotely(ContainerID iContainerID)
{
	RemoteCommand_timerRecordStartAutoRemote(GLOBALTYPE_GAMESERVER, iContainerID);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD;
void DbCreateSnapshot(ContainerID gServerID, U32 delaySeconds, bool verbose, char *dumpwebdata)
{
	if (gServerID)
		RemoteCommand_dbCreateSnasphot(GLOBALTYPE_OBJECTDB, gServerID, delaySeconds, verbose, dumpwebdata);
}

char *BootEveryone(char *pYes, int iTryNum);


void timedCallbackBootEveryone(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	BootEveryone("yes", (intptr_t)userData);
}


AUTO_COMMAND;
char *BootEveryone(char *pYes, int iTryNum)
{
	if (stricmp(pYes, "yes") == 0)
	{
		TrackedServerState *pServer;

		printf("Booting everyone... try %d\n", iTryNum);

		ResetNumPlayersForSuddenDrops();

		pServer = gpServersByType[GLOBALTYPE_GAMESERVER];

		while (pServer)
		{
			RemoteCommand_gslBootEveryone(GLOBALTYPE_GAMESERVER, pServer->iContainerID, iTryNum < 7 ? 0 : 1);

			pServer = pServer->pNext;
		}

		pServer = gpServersByType[GLOBALTYPE_GATEWAYSERVER];
		while (pServer)
		{
			RemoteCommand_gslGatewayServer_BootEveryone(GLOBALTYPE_GATEWAYSERVER, pServer->iContainerID, iTryNum < 7 ? 0 : 1);

			pServer = pServer->pNext;
		}


		pServer = gpServersByType[GLOBALTYPE_LOGINSERVER];
		while (pServer)
		{
			RemoteCommand_aslBootEveryone(GLOBALTYPE_LOGINSERVER, pServer->iContainerID, "The system operators have aborted all logins");

			pServer = pServer->pNext;
		}

		if (iTryNum < 10)
		{
			TimedCallback_Run(timedCallbackBootEveryone, (void*)(intptr_t)(iTryNum+1), 2.0f);
		}

		return "Booted everyone";
	}

	return "You didn't type 'yes', no one booted";
}

AUTO_COMMAND ACMD_CMDLINE;
char *GatewayLockTheShard(int iLock)
{
	TrackedServerState *pServer;
	GlobalType eType;

	gbGatewayIsLocked = iLock;

	for (eType = 0; eType < GLOBALTYPE_MAX; eType++)
	{
		if (gServerTypeInfo[eType].bInformAboutGatewayLockStatus)
		{
			pServer = gpServersByType[eType];

			while (pServer)
			{
				RemoteCommand_GatewayIsLocked(eType, pServer->iContainerID, iLock);
				pServer = pServer->pNext;
			}
		}
	}

	if (iLock)
	{
		return "Gateway is locked";
	}
	else
	{
		return "Gateway unlocked";
	}
}

AUTO_COMMAND ACMD_CMDLINE;
char *LockTheShard(int iLock)
{
	TrackedServerState *pServer;
	GlobalType eType;

	gbShardIsLocked = iLock;

	for (eType = 0; eType < GLOBALTYPE_MAX; eType++)
	{
		if (gServerTypeInfo[eType].bInformAboutShardLockStatus)
		{
			pServer = gpServersByType[eType];

			while (pServer)
			{
				RemoteCommand_ShardIsLocked(eType, pServer->iContainerID, iLock);
				pServer = pServer->pNext;
			}
		}
	}

	// This forces an update of the Gateway lock status.
	GatewayLockTheShard(gbGatewayIsLocked);

	ControllerShardCluster_SomethingLocalChanged();

	if (iLock)
	{
		return "Shard locked";
	}
	else
	{
		return "Shard unlocked";
	}
}


AUTO_COMMAND ACMD_CMDLINE;
char *MTKillSwitch(int iLock)
{
	if(gbMTKillSwitch != iLock)
	{
		gbMTKillSwitch = iLock;
		RemoteCommand_aslAPCmdSetMTKillSwitch(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, iLock);
	}

	if (iLock)
	{
		return "MT Kill switch ON";
	}
	else
	{
		return "MT Kill switch OFF";
	}
}

AUTO_COMMAND ACMD_CMDLINE;
char *BillingKillSwitch(int iLock)
{
	if(gbBillingKillSwitch != iLock)
	{
		gbBillingKillSwitch = iLock;
		RemoteCommand_aslAPCmdSetBillingKillSwitch(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, iLock);
	}

	if (iLock)
	{
		return "Billing kill switch ON";
	}
	else
	{
		return "Billing kill switch OFF";
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(csr);
void BootPlayerByAccountID(ContainerID iAccountID)
{
	RemoteCommand_dbBootPlayerByAccountID_Remote(GLOBALTYPE_OBJECTDB, 0, iAccountID);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(csr);
void BootPlayerByAccountName(char *pAccountName)
{
	RemoteCommand_dbBootPlayerByAccountName_Remote(GLOBALTYPE_OBJECTDB, 0, pAccountName);
}

AUTO_COMMAND ACMD_NAME(BootPlayerByDisplayName, BootPlayer);
void BootPlayerByDisplayName(char *pDisplayName)
{
	RemoteCommand_dbBootPlayerByPublicAccountName_Remote(GLOBALTYPE_OBJECTDB, 0, pDisplayName);
}

/*
AUTO_COMMAND ACMD_CATEGORY(TEST);
void TestPrintfOnAllServers(void)
{
	ContainerRef **ppRecipients = NULL;
	int i;

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		TrackedServerState *pServer = gpServersByType[i];

		while (pServer)
		{
			ContainerRef *pRecipient = StructCreate(parse_ContainerRef);
			pRecipient->containerID = pServer->iContainerID;
			pRecipient->containerType = pServer->eContainerType;

			eaPush(&ppRecipients, pRecipient);

			pServer = pServer->pNext;
		}
	}

	RemoteCommand_MultipleRecipients_MultiPrintTest(&ppRecipients, "Hello World!");
	eaDestroyStruct(&ppRecipients, parse_ContainerRef);
}
*/

AUTO_COMMAND;
char *SetMaxPlayers(char *pNum)
{
	static char *spRetVal = NULL;
	if (ServerLib_GetIntFromStringPossiblyWithPerGsSuffix(pNum, NULL, NULL))
	{
		SendCommandToAllServersOfType(GLOBALTYPE_LOGINSERVER, 0, STACK_SPRINTF("MaxPlayers %s", pNum));
		estrPrintf(&spRetVal, "Max Players set to %s", pNum);
	}
	else
	{
		estrPrintf(&spRetVal, "Can't set max players to %s, badly formatted. Must be nnnn or nnnn" PER_GS_MACHINE_SUFFIX, pNum );
	}

	return spRetVal;
}

AUTO_COMMAND;
char *SetSoftMaxPlayers(char *pNum)
{
	static char *spRetVal = NULL;
	if (ServerLib_GetIntFromStringPossiblyWithPerGsSuffix(pNum, NULL, NULL))
	{
		SendCommandToAllServersOfType(GLOBALTYPE_LOGINSERVER, 0, STACK_SPRINTF("SoftMaxPlayers %s", pNum));
		estrPrintf(&spRetVal, "Soft Max Players set to %s", pNum);
	}
	else
	{
		estrPrintf(&spRetVal, "Can't set soft max players to %s, badly formatted. Must be nnnn or nnnn" PER_GS_MACHINE_SUFFIX, pNum);
	}

	return spRetVal;
}

AUTO_COMMAND;
void SetMaxLogins(U32 uNum)
{
	SendCommandToAllServersOfType(GLOBALTYPE_LOGINSERVER, 0, STACK_SPRINTF("MaxLogins %u", uNum));
}

AUTO_COMMAND;
void SetMaxQueue(U32 uNum)
{
	SendCommandToAllServersOfType(GLOBALTYPE_LOGINSERVER, 0, STACK_SPRINTF("MaxQueue %u", uNum));
}

AUTO_COMMAND;
void SetLoginRate(U32 uNum)
{
	SendCommandToAllServersOfType(GLOBALTYPE_LOGINSERVER, 0, STACK_SPRINTF("LoginRate %u", uNum));
}

bool TypeIsOKForLotsOfTransactions(GlobalType eType)
{
	switch(eType)
	{
	case GLOBALTYPE_GAMESERVER:
	case GLOBALTYPE_LOGINSERVER:
	case GLOBALTYPE_TEAMSERVER:
	case GLOBALTYPE_QUEUESERVER:
	case GLOBALTYPE_ACCOUNTPROXYSERVER:
	case GLOBALTYPE_OBJECTDB:
	case GLOBALTYPE_MAPMANAGER:
		return true;
	}
	return false;
}


AUTO_COMMAND ACMD_CATEGORY(test);
void TransactionLoadTest(int iNum)
{
	TrackedServerState **ppServers = NULL;

	int i, j;

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (TypeIsOKForLotsOfTransactions(i))
		{
			TrackedServerState *pServer = gpServersByType[i];

			while (pServer)
			{
				eaPush(&ppServers, pServer);
				pServer = pServer->pNext;
			}


		}
	}

	for (i=0; i < eaSize(&ppServers); i++)
	{
		for (j = 0; j < eaSize(&ppServers); j++)
		{
			if (i != j)
			{
				RemoteCommand_GenerateLotsOfTestTransactions(ppServers[i]->eContainerType, ppServers[i]->iContainerID,
					GlobalTypeToName(ppServers[j]->eContainerType), ppServers[j]->iContainerID, iNum);
			}
		}
	}

	eaDestroy(&ppServers);
}

AUTO_COMMAND ACMD_CATEGORY(test);
void StopTransactionLoadTest(void)
{
	TrackedServerState **ppServers = NULL;

	int i;

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (TypeIsOKForLotsOfTransactions(i))
		{
			TrackedServerState *pServer = gpServersByType[i];

			while (pServer)
			{
				eaPush(&ppServers, pServer);
				pServer = pServer->pNext;
			}

		}
	}

	for (i=0; i < eaSize(&ppServers); i++)
	{
		
		RemoteCommand_StopTestTransactions(ppServers[i]->eContainerType, ppServers[i]->iContainerID);
	
	}

	eaDestroy(&ppServers);
}


#define LAST_MINUTE_DIR_NAME "c:\\LastMinuteFiles"




#define RECENTLYDIEDSERVERS_LIFESPAN 3
typedef struct RecentlyDiedServer
{
	ContainerID iID;
	U32 iTimeOfDeath;
} RecentlyDiedServer;

typedef struct RecentlyDiedServerList
{
	RecentlyDiedServer **ppServers;
} RecentlyDiedServerList;

RecentlyDiedServerList sRecentlyDiedLists[GLOBALTYPE_MAXTYPES];

//briefly keep track of servers after they have died, so we don't think they're zombies
void SetServerJustDied(GlobalType eType, ContainerID iID)
{
	RecentlyDiedServer *pServer = malloc(sizeof(RecentlyDiedServer));
	pServer->iID = iID;
	pServer->iTimeOfDeath = timeSecondsSince2000();

	eaPush(&sRecentlyDiedLists[eType].ppServers, pServer);
}

bool ServerRecentlyDied(GlobalType eType, ContainerID iID)
{
	int i;

	for (i=0; i < eaSize(&sRecentlyDiedLists[eType].ppServers); i++)
	{
		if (sRecentlyDiedLists[eType].ppServers[i]->iID == iID)
		{
			return true;
		}
	}

	return false;
}

void UpdateRecentlyDiedServers(void)
{
	int i;
	U32 iCutoffTime = timeSecondsSince2000() - RECENTLYDIEDSERVERS_LIFESPAN;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		while (eaSize(&sRecentlyDiedLists[i].ppServers) && sRecentlyDiedLists[i].ppServers[0]->iTimeOfDeath < iCutoffTime)
		{
			free(sRecentlyDiedLists[i].ppServers[0]);
			eaRemove(&sRecentlyDiedLists[i].ppServers, 0);
		}
	}
}



#define TOP_AND_BOTTOM_LIST_SIZE 10

typedef struct TopAndBottomList
{
	TrackedServerState *pTop10[TOP_AND_BOTTOM_LIST_SIZE];
	float fTop10Vals[TOP_AND_BOTTOM_LIST_SIZE]; //highest = 0
	int iNumTopVals;

	TrackedServerState *pBottom10[TOP_AND_BOTTOM_LIST_SIZE];
	float fBottom10Vals[TOP_AND_BOTTOM_LIST_SIZE]; //lowest = 0
	int iNumBottomVals;
} TopAndBottomList;
/*
fields i care about for the netops portal:
machine, fps, cpu usage, physical memory used, creation time, num players, num entities, num active entities, 
mapname short, instance id, locked

also, dave s mentioned that he would like to know the number of players in character creation 
(regardless of whether they already have a character or not) and the number of players selecting a 
character across all login servers in one convenient location, so 
we don't have to parse each individual loginserver page to get that info.
*/


void AddServerToTopAndBottomList(TopAndBottomList *pList, TrackedServerState *pServer, float fVal)
{
	int iWhereToInsert = pList->iNumTopVals;
	
	assert(iWhereToInsert <= TOP_AND_BOTTOM_LIST_SIZE);

	while (iWhereToInsert && fVal > pList->fTop10Vals[iWhereToInsert-1])
	{
		iWhereToInsert--;
	}

	if (iWhereToInsert < TOP_AND_BOTTOM_LIST_SIZE)
	{
		if (iWhereToInsert < pList->iNumBottomVals && iWhereToInsert < TOP_AND_BOTTOM_LIST_SIZE - 1)
		{
			memmove(&pList->pTop10[iWhereToInsert + 1], &pList->pTop10[iWhereToInsert], sizeof(void*) * (TOP_AND_BOTTOM_LIST_SIZE - iWhereToInsert - 1));
			memmove(&pList->fTop10Vals[iWhereToInsert + 1], &pList->fTop10Vals[iWhereToInsert], sizeof(float) * (TOP_AND_BOTTOM_LIST_SIZE - iWhereToInsert - 1));
		}

		pList->pTop10[iWhereToInsert] = pServer;
		pList->fTop10Vals[iWhereToInsert] = fVal;

		if (pList->iNumTopVals < TOP_AND_BOTTOM_LIST_SIZE)
		{
			pList->iNumTopVals++;
		}
	}

	iWhereToInsert = pList->iNumBottomVals;
	assert(iWhereToInsert <= TOP_AND_BOTTOM_LIST_SIZE);

	while (iWhereToInsert && fVal < pList->fBottom10Vals[iWhereToInsert-1])
	{
		iWhereToInsert--;
	}

	if (iWhereToInsert < TOP_AND_BOTTOM_LIST_SIZE)
	{
		if (iWhereToInsert < pList->iNumBottomVals && iWhereToInsert < TOP_AND_BOTTOM_LIST_SIZE - 1)
		{
			memmove(&pList->pBottom10[iWhereToInsert + 1], &pList->pBottom10[iWhereToInsert], sizeof(void*) * (TOP_AND_BOTTOM_LIST_SIZE - iWhereToInsert - 1));
			memmove(&pList->fBottom10Vals[iWhereToInsert + 1], &pList->fBottom10Vals[iWhereToInsert], sizeof(float) * (TOP_AND_BOTTOM_LIST_SIZE - iWhereToInsert - 1));
		}

		pList->pBottom10[iWhereToInsert] = pServer;
		pList->fBottom10Vals[iWhereToInsert] = fVal;

		if (pList->iNumBottomVals < TOP_AND_BOTTOM_LIST_SIZE)
		{
			pList->iNumBottomVals++;
		}
	}
}


void AddServersFromTopAndBottomListToEarray(TopAndBottomList *pList, TrackedServerState ***pppOutList)
{
	int i;

	for (i=0; i < pList->iNumTopVals; i++)
	{
		eaPushUnique(pppOutList, pList->pTop10[i]);
	}

	for (i=0; i < pList->iNumBottomVals; i++)
	{
		eaPushUnique(pppOutList, pList->pBottom10[i]);
	}
}

static void AddMachineToMachineSet(ControllerInterestingStuff_MachineSet *pSet, TrackedMachineState *pMachine)
{
	pSet->iTotalMachineCPU += pMachine->performance.cpuUsage;

	if (pSet->iNumMachines == 0)
	{
		 pSet->iLowestCPU = pSet->iHighestCPU = pMachine->performance.cpuUsage;
		strcpy( pSet->lowestCPUMachineName, pMachine->machineName);
		strcpy( pSet->highestCPUMachineName, pMachine->machineName);
	}
	else
	{
		if (pMachine->performance.cpuUsage <  pSet->iLowestCPU)
		{
			 pSet->iLowestCPU = pMachine->performance.cpuUsage;
			strcpy( pSet->lowestCPUMachineName, pMachine->machineName);
		}
		else if (pMachine->performance.cpuUsage >  pSet->iHighestCPU)
		{
			 pSet->iHighestCPU = pMachine->performance.cpuUsage;
			strcpy( pSet->highestCPUMachineName, pMachine->machineName);
		}
	}

	 pSet->iTotalFreeRAM += pMachine->performance.iFreeRAM;
	 pSet->iTotalRAM += pMachine->performance.iTotalRAM;

	if (pSet->iNumMachines == 0)
	{
		pSet->iLowestFreeRAM =  pSet->iHighestFreeRAM = pMachine->performance.iFreeRAM;
		strcpy( pSet->lowestFreeRAMMachineName, pMachine->machineName);
		strcpy( pSet->highestFreeRAMMachineName, pMachine->machineName);
	}
	else
	{
		if (pMachine->performance.iFreeRAM <  pSet->iLowestFreeRAM)
		{
			 pSet->iLowestFreeRAM = pMachine->performance.iFreeRAM;
			strcpy( pSet->lowestFreeRAMMachineName, pMachine->machineName);
		}
		else if (pMachine->performance.iFreeRAM >  pSet->iHighestFreeRAM)
		{
			 pSet->iHighestFreeRAM = pMachine->performance.iFreeRAM;
			strcpy( pSet->highestFreeRAMMachineName, pMachine->machineName);
		}
	}

	pSet->iNumMachines++;
	pSet->iAvgMachineCPU =  pSet->iTotalMachineCPU / pSet->iNumMachines;

	if (pMachine->performance.cpuUsage <= 80)
	{
		if (pMachine->performance.cpuUsage <= 10)
		{
			pSet->iNumMachines_CPU0to10++;
		}
		else 
		{
			pSet->iNumMachines_CPU11to80++;
		}
	}
	else if (pMachine->performance.cpuUsage <= 95)
	{
		pSet->iNumMachines_CPU81to95++;
	}
	else
	{
		pSet->iNumMachines_CPU96to100++;
	}
}

ControllerInterestingStuff *GetInterestingStuff(void)
{
	static ControllerInterestingStuff *pStuff = NULL;
	static U32 iLastTime = 0;
	TrackedServerState *pServer;
	IDsOfPlayerInLoginQueue *pGuyInQueue;

	if (iLastTime > timeSecondsSince2000() - 5)
	{
		return pStuff;
	}
	else
	{

		int i;
		U64 totalQueueTime = 0;
		TopAndBottomList fpsList = {0};
		TopAndBottomList cpuList = {0};
		TopAndBottomList physMemList = {0};
		TopAndBottomList numPlayersList = {0};
		TopAndBottomList numEntsList = {0};
		TopAndBottomList numActiveEntsList = {0};
		TrackedServerState **ppAllServers = NULL;

		pServer = gpServersByType[GLOBALTYPE_GAMESERVER];



		if (!pStuff)
		{
			pStuff = StructCreate(parse_ControllerInterestingStuff);
		}
		else
		{
			StructReset(parse_ControllerInterestingStuff, pStuff);
		}

		while (pServer)
		{
			if (pServer->perfInfo.fFPS)
			{
				AddServerToTopAndBottomList(&fpsList, pServer, pServer->perfInfo.fFPS);
			}
		
			AddServerToTopAndBottomList(&cpuList, pServer, pServer->perfInfo.fCPUUsage);
			AddServerToTopAndBottomList(&physMemList, pServer, (float)(pServer->perfInfo.physicalMemUsed));
			
			if (pServer->pGameServerSpecificInfo)
			{
				AddServerToTopAndBottomList(&numPlayersList, pServer, pServer->pGameServerSpecificInfo->iNumPlayers);
				AddServerToTopAndBottomList(&numEntsList, pServer, pServer->pGameServerSpecificInfo->iNumEntities);
				AddServerToTopAndBottomList(&numActiveEntsList, pServer, pServer->pGameServerSpecificInfo->iNumActiveEnts);
			}

			if (pServer->perfInfo.fFPS)
			{
				if (pServer->perfInfo.fFPS < 10)
				{
					pStuff->iNumGameServersBelow10fps++;
				}
				else if (pServer->perfInfo.fFPS < 25)
				{
					pStuff->iNumGameServers10to25fps++;
				}
				else
				{
					pStuff->iNumGameServersAbove25fps++;
				}
			}

			pServer = pServer->pNext;
		}

		for (i=0; i < giNumMachines; i++)
		{
			TrackedMachineState *pMachine = &gTrackedMachines[i];

			AddMachineToMachineSet(&pStuff->allMachines, pMachine);

			if (pMachine->canLaunchServerTypes[GLOBALTYPE_GAMESERVER].eCanLaunch == CAN_LAUNCH_SPECIFIED)
			{
				AddMachineToMachineSet(&pStuff->gameServerMachines, pMachine);
			}
			else if (pMachine->canLaunchServerTypes[GLOBALTYPE_GATEWAYSERVER].eCanLaunch == CAN_LAUNCH_SPECIFIED)
			{
				AddMachineToMachineSet(&pStuff->gatewayServerMachines, pMachine);
			}
			else
			{
				AddMachineToMachineSet(&pStuff->nonGameServerMachines, pMachine);
			}
		}
		
		AddServersFromTopAndBottomListToEarray(&fpsList, &ppAllServers);
		AddServersFromTopAndBottomListToEarray(&cpuList, &ppAllServers);
		AddServersFromTopAndBottomListToEarray(&physMemList, &ppAllServers);
		AddServersFromTopAndBottomListToEarray(&numPlayersList, &ppAllServers);
		AddServersFromTopAndBottomListToEarray(&numEntsList, &ppAllServers);
		AddServersFromTopAndBottomListToEarray(&numActiveEntsList, &ppAllServers);

		for (i=0 ; i < eaSize(&ppAllServers); i++)
		{
			ControllerInterestingStuff_GameServer *pInterestingGS = StructCreate(parse_ControllerInterestingStuff_GameServer);
			pServer = ppAllServers[i];

			pInterestingGS->bLocked = pServer->bLocked;
			pInterestingGS->fCPUUsage = pServer->perfInfo.fCPUUsage;
			pInterestingGS->fFPS = pServer->perfInfo.fFPS;
			pInterestingGS->iCreationTime = pServer->iCreationTime;
			pInterestingGS->iID = pServer->iContainerID;
			pInterestingGS->physicalMemUsed = pServer->perfInfo.physicalMemUsed;

			if (pServer->pGameServerSpecificInfo)
			{
				pInterestingGS->iNumActiveEnts = pServer->pGameServerSpecificInfo->iNumActiveEnts;
				pInterestingGS->iNumEntities = pServer->pGameServerSpecificInfo->iNumEntities;
				pInterestingGS->iNumPlayers = pServer->pGameServerSpecificInfo->iNumPlayers;
				strcpy(pInterestingGS->mapNameShort, pServer->pGameServerSpecificInfo->mapNameShort);	
				pInterestingGS->iNumPartitions = pServer->pGameServerSpecificInfo->iNumPartitions;
			}

			pInterestingGS->pMachineName = pServer->pMachine->machineName;
				
			eaPush(&pStuff->ppGameServers, pInterestingGS);
		}


		////////////////////////////////////////////////////////////////
		// Gateway Servers

		pServer = gpServersByType[GLOBALTYPE_GATEWAYSERVER];
		while(pServer)
		{
			ControllerInterestingStuff_GatewayServer *pInterestingGS = StructCreate(parse_ControllerInterestingStuff_GatewayServer);

			pInterestingGS->iCPUUsage = pServer->perfInfo.fCPUUsage;
			pInterestingGS->fFPS = pServer->perfInfo.fFPS;
			pInterestingGS->iCreationTime = pServer->iCreationTime;
			pInterestingGS->iID = pServer->iContainerID;
			pInterestingGS->physicalMemUsed = pServer->perfInfo.physicalMemUsed;

			pInterestingGS->pMachineName = pServer->pMachine->machineName;

			if (pServer->pGatewayServerSpecificInfo)
			{
				pStuff->iNumGatewaySessions += pServer->pGatewayServerSpecificInfo->iNumSessions;

				pInterestingGS->iNumSessions = pServer->pGatewayServerSpecificInfo->iNumSessions;
				pInterestingGS->iHeapTotal = pServer->pGatewayServerSpecificInfo->iHeapTotal;
				pInterestingGS->iHeapUsed = pServer->pGatewayServerSpecificInfo->iHeapUsed;
				pInterestingGS->iWorkingSet = pServer->pGatewayServerSpecificInfo->iWorkingSet;
			}

			eaPush(&pStuff->ppGatewayServers, pInterestingGS);

			pServer = pServer->pNext;
		}

		////////////////////////////////////////////////////////////////

		pServer = gpServersByType[GLOBALTYPE_LOGINSERVER];
		while (pServer)
		{
			if (pServer->pLoginServerSpecificInfo)
			{
				pStuff->iPlayersLoggingIn += pServer->pLoginServerSpecificInfo->iNumLoggingIn;
				//			pStuff->iPlayersInQueue += pServer->pLoginServerSpecificInfo->iNumInQueue;
				//		if(pServer->pLoginServerSpecificInfo->iMaxQueueTime > pStuff->iMaxTimeInQueue)
				//			pStuff->iMaxTimeInQueue = pServer->pLoginServerSpecificInfo->iMaxQueueTime;
				//		totalQueueTime += pServer->pLoginServerSpecificInfo->iTotalQueueTime;
				//		pStuff->iQueueThreshold = pServer->pLoginServerSpecificInfo->iQueueThreshold;
			}

			pServer = pServer->pNext;
		}
		//	pStuff->iMeanTimeInQueue = pStuff->iPlayersInQueue ? (totalQueueTime / pStuff->iPlayersInQueue) : 0;

		if (gpServersByType[GLOBALTYPE_LOGSERVER])
		{
			pStuff->iLogsPerSecond = gpServersByType[GLOBALTYPE_LOGSERVER]->pLogServerSpecificInfo->iLogsPerSecond;
		}

		pStuff->fControllerFps = gfLastReportedControllerFPS;
	}

	
	pServer = gpServersByType[GLOBALTYPE_OBJECTDB];
	if (pServer)
	{
		pStuff->iObjDBLastContactSecsAgo = timeSecondsSince2000() - pServer->perfInfo.iLastContactTime;
	}

	pServer = gpServersByType[GLOBALTYPE_TRANSACTIONSERVER];
	if (pServer)
	{
		pStuff->iTransServerLastContactSecsAgo = timeSecondsSince2000() - pServer->perfInfo.iLastContactTime;
	}

	pStuff->iMainQueueSize = PointerFIFO_Count(spMainLoginQueue);
	pStuff->iVIPQueueSize = PointerFIFO_Count(spVIPLoginQueue);

	pStuff->iMaxMainQueueSize = PointerFIFO_GetMaxCount(spMainLoginQueue, 0);
	pStuff->iMaxMainQueueSize_LastDay = PointerFIFO_GetMaxCount(spMainLoginQueue, timeSecondsSince2000() - 24 * 60 * 60);
	pStuff->iMaxMainQueueSize_LastHour = PointerFIFO_GetMaxCount(spMainLoginQueue, timeSecondsSince2000() - 60 * 60);

	pStuff->iMaxVIPQueueSize = PointerFIFO_GetMaxCount(spVIPLoginQueue, 0);
	pStuff->iMaxVIPQueueSize_LastDay = PointerFIFO_GetMaxCount(spVIPLoginQueue, timeSecondsSince2000() - 24 * 60 * 60);
	pStuff->iMaxVIPQueueSize_LastHour = PointerFIFO_GetMaxCount(spVIPLoginQueue, timeSecondsSince2000() - 60 * 60);


	if (PointerFIFO_Peek(spMainLoginQueue, &pGuyInQueue))
	{
		pStuff->iHowLongFirstGuyInMainQueueHasBeenWaiting = timeSecondsSince2000() - pGuyInQueue->iTimeAdded;
	}


	if (PointerFIFO_Peek(spVIPLoginQueue, &pGuyInQueue))
	{
		pStuff->iHowLongFirstGuyInVIPQueueHasBeenWaiting = timeSecondsSince2000() - pGuyInQueue->iTimeAdded;
	}


	return pStuff;

}

bool ProcessInterestingStuffIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	ControllerInterestingStuff *pStuff = GetInterestingStuff();
	bool bRetVal;
	
	
	bRetVal = ProcessStructIntoStructInfoForHttp("", pArgList,
		pStuff, parse_ControllerInterestingStuff, iAccessLevel, 0, pStructInfo, eFlags);
	
	return bRetVal;
}


AUTO_RUN;
void SetupInterestingStuffList(void)
{
	RegisterCustomXPathDomain(".ControllerInterestingStuff", ProcessInterestingStuffIntoStructInfoForHttp, NULL);
}

AUTO_COMMAND;
char *ReloadGSLBannedCommands(void)
{
	char fileName[CRYPTIC_MAX_PATH];
	static char *pRetVal = NULL;
	char *pBuf;
	TrackedServerState *pServer;

	sprintf(fileName, "%s/GSLBannedCommands.txt", fileLocalDataDir());
	
	pBuf = fileAlloc(fileName, NULL);

	if (!pBuf)
	{
		estrPrintf(&pRetVal, "Couldn't load %s. Leaving banned list untouched.", fileName);
		return pRetVal;
	}

	SAFE_FREE(gpGSLBannedCommandsString);
	gpGSLBannedCommandsString = pBuf;

	eaDestroyEx(&gppGSLBannedCommands, NULL);

	DivideString(pBuf, ",\n", &gppGSLBannedCommands, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS	| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);


	estrPrintf(&pRetVal, "Loaded %s, banned %d commands", fileName, eaSize(&gppGSLBannedCommands));


	pServer = gpServersByType[GLOBALTYPE_GAMESERVER];

	while (pServer)
	{
		RemoteCommand_SetGSLBannedCommands(GLOBALTYPE_GAMESERVER, pServer->iContainerID, gpGSLBannedCommandsString);
		pServer = pServer->pNext;
	}

	return pRetVal;
}

AUTO_COMMAND;
char *VerboseProcListLogging(char *pMachineName, int iOn)
{
	TrackedMachineState *pMachine = FindMachineByName(pMachineName);
	Packet *pPak;

	if (!pMachine)
	{
		return "Couldn not find machine";
	}

	if (!pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
	{
		return "Machine has no launcher";
	}
	
	pMachine->bVerboseProcListLogging = iOn;
	pPak = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_VERBOSEPROCLISTLOGGING);
	pktSendBits(pPak, 1, iOn);
	pktSend(&pPak);

	if (iOn)
	{
		return "turned on";
	}
	else
	{
		return "turned off";
	}

}

//given "gameserver_franken6.exe", trims it to "gameserver.exe", also returns gameserverX64_franken6.exe
//returns true on success
bool GetNormalNameFromFrankenbuildName(char *pInName, char **ppOutNormalName, char **ppOutX64Name)
{
	char *pLastPeriod = strrchr(pInName, '.');
	char *pFirstUnderscore = strchr(pInName, '_');

	if (!pLastPeriod || !pFirstUnderscore || pFirstUnderscore > pLastPeriod)
	{
		return false;
	}

	estrCopy2(ppOutNormalName, pInName);

	if (ppOutX64Name)
	{
		estrCopy2(ppOutX64Name, pInName);
	}

	estrRemove(ppOutNormalName, pFirstUnderscore - pInName, pLastPeriod - pFirstUnderscore);

	if (ppOutX64Name)
	{
		estrInsert(ppOutX64Name, pFirstUnderscore - pInName, "X64", 3);
	}

	return true;
}

//gets all server types that have a given executable as their .exe
void GetAllServerTypesForExe(char *pExeName, U32 **ppOutServerTypes)
{
	int i;

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (gServerTypeInfo[i].bInUse)
		{
			char *pTempName1 = NULL;
			char *pTempName2 = NULL;
			estrCopy2(&pTempName1, gServerTypeInfo[i].executableName32_original);
			estrTruncateAtFirstOccurrence(&pTempName1, ' ');

			//might already be frankebuildt
			if (strchr(pTempName1, '_'))
			{
				if (!GetNormalNameFromFrankenbuildName(pTempName1, &pTempName2, NULL))
				{
					continue;
				}
			}
			else
			{
				estrCopy(&pTempName2, &pTempName1);
			}

			if (stricmp(pTempName2, pExeName) == 0)
			{
				ea32Push(ppOutServerTypes, i);
			}

			estrDestroy(&pTempName2);
			estrDestroy(&pTempName1);
		}
	}
}



AUTO_STRUCT;
typedef struct FrankenBuild
{
	U32 iID; AST(KEY)
	char *pExeName; AST(ESTRING)
	char *pExeX64Name; AST(ESTRING)
	char *pFullExeName; AST(ESTRING)
	char *pFullX64ExeName; AST(ESTRING)
	char *pFullPdbName; AST(ESTRING)
	char *pFullX64PdbName; AST(ESTRING)

	char *pComment;

	U32 *pContainerTypes;

	bool bSend32Bit;

	bool bWaitingForFilesToLoad;
	int iFilesWaitingFor;
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo; NO_AST
	bool bFailed;
} FrankenBuild;

FrankenBuild **ppPossibleFrankenBuilds = NULL;
FrankenBuild **ppActiveFrankenBuilds = NULL;

char *GetDescriptiveFrankenbuildString(FrankenBuild *pBuild)
{
	static char *pRetString = NULL;
	int i;

	estrClear(&pRetString);

	estrPrintf(&pRetString, "For container types ");
	
	for (i = 0; i < ea32Size(&pBuild->pContainerTypes); i++)
	{
		estrConcatf(&pRetString, "%s%s", i == 0 ? "" : ", ", GlobalTypeToName(pBuild->pContainerTypes[i]));
	}

	if (pBuild->pFullExeName)
	{
		estrConcatf(&pRetString, " setting 32-bit executable to %s", pBuild->pFullExeName);
		if (pBuild->pFullX64ExeName)
		{
			estrConcatf(&pRetString, " and");
		}
	}

	if (pBuild->pFullX64ExeName)
	{
		estrConcatf(&pRetString, " setting 64-bit executable to %s", pBuild->pFullX64ExeName);
	}

	estrConcatf(&pRetString, ". Comment: %s", pBuild->pComment);

	return pRetString;
}

AUTO_FIXUPFUNC;
TextParserResult fixupFrankenBuild(FrankenBuild *pBuild, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		SAFE_FREE(pBuild->pSlowReturnInfo);
		break;
	}

	return 1;
}


//forced IDs should be 

FrankenBuild *GetNewPotentialFrankenBuild(U32 iForcedID)
{
	FrankenBuild *pBuild;
	pBuild = StructCreate(parse_FrankenBuild);
	eaIndexedEnable(&ppPossibleFrankenBuilds, parse_FrankenBuild);


	if (iForcedID)
	{
		if (eaIndexedGetUsingInt(&ppPossibleFrankenBuilds, iForcedID))
		{
			AssertOrAlert("FORCED_ID_OVERLAP", "Cluster controller trying to start a frankenbuild with ID %u that's already in use",
				iForcedID);
		}
	}
	else
	{
		iForcedID = randomIntRange(1, MIN_FRANKENBUILD_ID_FROM_CLUSTER_CONTROLLER - 1);
	}

	while (eaIndexedGetUsingInt(&ppPossibleFrankenBuilds, iForcedID))
	{
		iForcedID++;
	}

	pBuild->iID = iForcedID;

	eaPush(&ppPossibleFrankenBuilds, pBuild);
	return pBuild;
}

AUTO_COMMAND;
void UpdateFrankenBuild(U32 iID)
{
#if 0
	int iBuildIndex = eaIndexedFindUsingInt(&ppActiveFrankenBuilds, iID);
	FrankenBuild *pBuild;
	Packet *pPak;
	TrackedMachineState *pMachine;

	if (iBuildIndex < 0)
	{
		return;
	}

	pBuild = ppActiveFrankenBuilds[iBuildIndex];

	pBuild->iCurMachineNum++;

	while (pBuild->iCurMachineNum < giNumMachines && (!gTrackedMachines[pBuild->iCurMachineNum].pServersByType[GLOBALTYPE_LAUNCHER] || gTrackedMachines[pBuild->iCurMachineNum].bIsLocalHost))
	{
		printf("Skipping machine %s... it is %s\n", gTrackedMachines[pBuild->iCurMachineNum].machineName,
			!gTrackedMachines[pBuild->iCurMachineNum].pServersByType[GLOBALTYPE_LAUNCHER] ? "currently dead" : "local host");
		pBuild->iCurMachineNum++;
	}


	

	if (pBuild->iCurMachineNum == giNumMachines)
	{
		int i;
		printf("All done... updating launching info:\n");

		SetAllowShardVersionMismatch(true);

		for (i=0; i < ea32Size(&pBuild->pContainerTypes); i++)
		{
			ServerTypeInfo *pTypeInfo = &gServerTypeInfo[pBuild->pContainerTypes[i]];
			char *pTemp = NULL;
			char *pTemp2 = NULL;

			if (pBuild->bSend32Bit)
			{
				printf("For %s, replacing <<%s>> with ", GlobalTypeToName(pBuild->pContainerTypes[i]), pTypeInfo->executableName32);
				estrCopy2(&pTemp, pTypeInfo->executableName32);
				estrRemoveUpToFirstOccurrence(&pTemp, ' ');
				estrPrintf(&pTemp2, "%s %s", pBuild->pExeName, pTemp);
				printf("<<%s>>", pTemp2);
				strcpy_trunc(pTypeInfo->executableName32, pTemp2);
				printf("\n");
			}

			if (pBuild->pExeX64Name && pTypeInfo->executableName64[0])
			{
				printf("For %s, replacing <<%s>> with ", GlobalTypeToName(pBuild->pContainerTypes[i]), pTypeInfo->executableName64);
				estrCopy2(&pTemp, pTypeInfo->executableName64);
				estrRemoveUpToFirstOccurrence(&pTemp, ' ');
				estrPrintf(&pTemp2, "%s %s", pBuild->pExeX64Name, pTemp);
				printf("<<%s>>", pTemp2);
				strcpy_trunc(pTypeInfo->executableName64, pTemp2);
				printf("\n");
			}
			estrDestroy(&pTemp);
			estrDestroy(&pTemp2);
		}

		StructDestroy(parse_FrankenBuild, pBuild);
		eaRemove(&ppActiveFrankenBuilds, iBuildIndex);
		return;
	}

	pMachine = &gTrackedMachines[pBuild->iCurMachineNum];

	printf("Sending files to %s\n", pMachine->machineName);

	if (pBuild->bSend32Bit)
	{
		if (!pBuild->pExe)
		{
			pBuild->pExe = fileAlloc(pBuild->pFullExeName, &pBuild->iExeSize);

			if (!pBuild->pExe)
			{
				AssertOrAlert("FRANKENBUILD_FAIL", "While doing frankenbuild, file %s didn't exist after being previous verified",
					pBuild->pFullExeName);
			}
		}
		if (!pBuild->pPdb)
		{
			pBuild->pPdb = fileAlloc(pBuild->pFullPdbName, &pBuild->iPdbSize);

			if (!pBuild->pPdb)
			{
				AssertOrAlert("FRANKENBUILD_FAIL", "While doing frankenbuild, file %s didn't exist after being previous verified",
					pBuild->pFullPdbName);
			}
		}
	}

	if (pBuild->pFullX64ExeName && !pBuild->pExeX64)
	{
		pBuild->pExeX64 = fileAlloc(pBuild->pFullX64ExeName, &pBuild->iExeX64Size);

		if (!pBuild->pExeX64)
		{
			AssertOrAlert("FRANKENBUILD_FAIL", "While doing frankenbuild, file %s didn't exist after being previous verified",
				pBuild->pFullX64ExeName);
		}
	}
	if (pBuild->pFullX64PdbName && !pBuild->pPdbX64)
	{
		pBuild->pPdbX64 = fileAlloc(pBuild->pFullX64PdbName, &pBuild->iPdbX64Size);

		if (!pBuild->pPdbX64)
		{
			AssertOrAlert("FRANKENBUILD_FAIL", "While doing frankenbuild, file %s didn't exist after being previous verified",
				pBuild->pFullX64PdbName);
		}
	}

	pPak = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_BEGINGETTINGFILES);

	if (pBuild->pExe)
	{
		pktSendString(pPak, pBuild->pFullExeName);
		pktSendBits(pPak, 32, pBuild->iExeSize);
	}

	if (pBuild->pPdb)
	{
		pktSendString(pPak, pBuild->pFullPdbName);
		pktSendBits(pPak, 32, pBuild->iPdbSize);
	}

	if (pBuild->pExeX64)
	{
		pktSendString(pPak, pBuild->pFullX64ExeName);
		pktSendBits(pPak, 32, pBuild->iExeX64Size);
	}

	if (pBuild->pPdbX64)
	{
		pktSendString(pPak, pBuild->pFullX64PdbName);
		pktSendBits(pPak, 32, pBuild->iPdbX64Size);
	}

	pktSendString(pPak, "");
	pktSendStringf(pPak, "UpdateFrankenBuild %u", pBuild->iID);
	pktSend(&pPak);
#endif
}

void ApplyFrankenbuildInternal(GlobalType eServerType, char *p32BitShortExeName, char *p64BitShortExeName, char *pComment, char **ppOutErrorString)
{
	CompressedFileCache *p32BitExe;
	CompressedFileCache *p64BitExe;

	SetAllowShardVersionMismatch(true);

	gServerTypeInfo[eServerType].eFrankenState = FRANKENSTATE_NONE;

	if (p32BitShortExeName && p32BitShortExeName[0])
	{
		p32BitExe = Controller_GetCompressedFileCache(p32BitShortExeName);
		if (!p32BitExe || !p32BitExe->pCompressedBuffer)
		{
			estrPrintf(ppOutErrorString, "Could not load 32 bit exe to compress and CRC");
			return;
		}

		estrCopy2(&gServerTypeInfo[eServerType].pExecutableName32_FrankenBuilt, gServerTypeInfo[eServerType].executableName32_original);
		estrRemoveUpToFirstOccurrence(&gServerTypeInfo[eServerType].pExecutableName32_FrankenBuilt, ' ');
		if (estrLength(&gServerTypeInfo[eServerType].pExecutableName32_FrankenBuilt))
		{
			estrInsertf(&gServerTypeInfo[eServerType].pExecutableName32_FrankenBuilt, 0, "%s ", p32BitShortExeName);
		}
		else
		{
			estrCopy2(&gServerTypeInfo[eServerType].pExecutableName32_FrankenBuilt, p32BitShortExeName);
		}

		gServerTypeInfo[eServerType].iExecutable32FrankenBuiltCRC = p32BitExe->iCRC;

		gServerTypeInfo[eServerType].eFrankenState |= FRANKENSTATE_32BIT;
	}

	if (p64BitShortExeName && p64BitShortExeName[0])
	{
		p64BitExe = Controller_GetCompressedFileCache(p64BitShortExeName);
		if (!p64BitExe || !p64BitExe->pCompressedBuffer)
		{
			estrPrintf(ppOutErrorString, "Could not load 64 bit exe to compress and CRC");
			return;
		}
		estrCopy2(&gServerTypeInfo[eServerType].pExecutableName64_FrankenBuilt, gServerTypeInfo[eServerType].executableName64_original);
		estrRemoveUpToFirstOccurrence(&gServerTypeInfo[eServerType].pExecutableName64_FrankenBuilt, ' ');
		if (estrLength(&gServerTypeInfo[eServerType].pExecutableName64_FrankenBuilt))
		{
			estrInsertf(&gServerTypeInfo[eServerType].pExecutableName64_FrankenBuilt, 0, "%s ", p64BitShortExeName);
		}
		else
		{
			estrCopy2(&gServerTypeInfo[eServerType].pExecutableName64_FrankenBuilt, p64BitShortExeName);
		}

		gServerTypeInfo[eServerType].iExecutable64FrankenBuiltCRC = p64BitExe->iCRC;
				
		gServerTypeInfo[eServerType].eFrankenState |= FRANKENSTATE_64BIT;

	}
}

void BeginFrankenBuildCompressedFileCB(CompressedFileCache *pCache, FrankenBuild *pBuild)
{
	char *pReturnString = NULL;
	char *pErrorString = NULL;
	int iServerTypeNum;
	GlobalType eServerType;

	pBuild->iFilesWaitingFor--;

	if (pCache->pCompressedBuffer == NULL)
	{
		pBuild->bFailed = true;
	}


	if (pBuild->iFilesWaitingFor > 0)
	{
		return;
	}

	eaFindAndRemove(&ppPossibleFrankenBuilds, pBuild);

	if (pBuild->bFailed)
	{
		AddFrankenBuildReport("HOTBUILD FAILED - could not open one or more files, frankenbuild not applied: %s", 
			GetDescriptiveFrankenbuildString(pBuild));

		DoSlowCmdReturn(0, "One or more files could not be loaded... frankenbuild not being applied", pBuild->pSlowReturnInfo);
		return;
	}

	//ntoe that pExeName and pX64ExeName are always set, so if you want to check whether the build really applies to the 32/64 bit 
	//exe, you have to check FullExeName
	for (iServerTypeNum = 0; iServerTypeNum < ea32Size(&pBuild->pContainerTypes); iServerTypeNum++)
	{
		eServerType = pBuild->pContainerTypes[iServerTypeNum];
		estrClear(&pErrorString);

		ApplyFrankenbuildInternal(eServerType, pBuild->pFullExeName ? pBuild->pExeName : NULL,
			pBuild->pFullX64ExeName ? pBuild->pExeX64Name : NULL, pBuild->pComment, &pErrorString);

		if (estrLength(&pErrorString))
		{
			estrConcatf(&pReturnString, "Problems applying to %s: %s\n", GlobalTypeToName(eServerType), pErrorString);
		}
	}

	if (estrLength(&pReturnString))
	{
		AddFrankenBuildReport("HOTBUILD - SOME ERRORS - encountered errors: <<%s>> while applying frankenbuild: %s",
			pErrorString, GetDescriptiveFrankenbuildString(pBuild));
		DoSlowCmdReturn(0, pReturnString, pBuild->pSlowReturnInfo);
	}
	else
	{
		AddFrankenBuildReport("HOTBUILD SUCCEEDED: %s", GetDescriptiveFrankenbuildString(pBuild));
		DoSlowCmdReturn(1, "Frankenbuild successfully applied", pBuild->pSlowReturnInfo);
	}

}











	

AUTO_COMMAND ACMD_CATEGORY(shard);
char *BeginFrankenBuildHotPush(CmdContext *pContext, U32 iID, ACMD_SENTENCE pDescriptiveComment)
{
	int iIndex = eaIndexedFindUsingInt(&ppPossibleFrankenBuilds, iID);
	FrankenBuild *pBuild;
	char *pTempPDBName = NULL;


	static char *pErrorString = NULL;

	if (iIndex < 0)
	{
		return "Unknown ID";
	}

	pBuild = ppPossibleFrankenBuilds[iIndex];
	
	if (pBuild->bWaitingForFilesToLoad)
	{
		return "Already in process... wtf are you doing?";
	}

	if (pDescriptiveComment[0])
	{
		FILE *pFile;
		char *pFileName = NULL;

		if (estrLength(&pBuild->pFullExeName))
		{
			estrCopy2(&pFileName, pBuild->pFullExeName);
		}
		else
		{
			estrCopy2(&pFileName, pBuild->pFullX64ExeName);
			estrReplaceOccurrences_CaseInsensitive(&pFileName, "X64", "");
		}

		estrReplaceOccurrences_CaseInsensitive(&pFileName, ".exe", ".txt");

		pFile = fopen(pFileName, "wt");
		if (pFile)
		{
			fprintf(pFile, "%s", pDescriptiveComment);
			fclose(pFile);
		}
		estrDestroy(&pFileName);
		pBuild->pComment = strdup(pDescriptiveComment);
	}
	pBuild->bWaitingForFilesToLoad = true;
	pBuild->iFilesWaitingFor = 0;
	pBuild->pSlowReturnInfo = calloc(sizeof(CmdSlowReturnForServerMonitorInfo), 1);
	memcpy(pBuild->pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	//have to set our counts first so that we don't get our callback until all of our files are actually ready
	if (pBuild->pFullX64ExeName)
	{
		pBuild->iFilesWaitingFor += 2;
	}

	if (pBuild->pFullExeName)
	{
		pBuild->iFilesWaitingFor += 2;
	}

	if (pBuild->pFullExeName)
	{
		Controller_GetCompressedFileCache_Threaded(pBuild->pExeName, BeginFrankenBuildCompressedFileCB, pBuild);
		estrCopy2(&pTempPDBName, pBuild->pExeName);
		estrReplaceOccurrences(&pTempPDBName, ".exe", ".pdb");
		Controller_GetCompressedFileCache_Threaded(pTempPDBName, BeginFrankenBuildCompressedFileCB, pBuild);
	}
	if (pBuild->pFullX64ExeName)
	{
		Controller_GetCompressedFileCache_Threaded(pBuild->pExeX64Name, BeginFrankenBuildCompressedFileCB, pBuild);
		estrCopy2(&pTempPDBName, pBuild->pExeX64Name);
		estrReplaceOccurrences(&pTempPDBName, ".exe", ".pdb");
		Controller_GetCompressedFileCache_Threaded(pTempPDBName, BeginFrankenBuildCompressedFileCB, pBuild);
	}

	pContext->slowReturnInfo.bDoingSlowReturn = true;
	return NULL;
}






AUTO_COMMAND ACMD_CATEGORY(shard);
char *RequestFrankenBuildHotpush(char *pFrankenExeName, ACMD_IGNORE U32 iForcedID)
{

	static char *pNormalExeName = NULL;
	static char *pFrankenX64Name = NULL;

	static char *pResultString = NULL;

	static U32 *pContainerTypes = NULL;

	bool bUsesX64;
	int i;

	static char *pFullExeName = NULL;
	static char *pFullX64ExeName = NULL;
	static char *pFullPdbName = NULL;
	static char *pFullX64PdbName = NULL;
	bool bSend32Bit = false;

	FrankenBuild *pFrankenBuild;

	estrClear(&pResultString);

	//if someone passes is a 64 bit name, treat it as a 32 bit name
	if (strstri(pFrankenExeName, "X64"))
	{
		static char *p32BitName_Internal = NULL;
		estrCopy2(&p32BitName_Internal, pFrankenExeName);
		estrReplaceOccurrences_CaseInsensitive(&p32BitName_Internal, "X64", "");
		pFrankenExeName = p32BitName_Internal;
	}

	if (!GetNormalNameFromFrankenbuildName(pFrankenExeName, &pNormalExeName, &pFrankenX64Name))
	{
		estrPrintf(&pResultString, "ERROR: %s doesn't seem to be a well-formed frankebuild name. It must look like gameserver_foo.exe",
			pFrankenExeName);
		return pResultString;
	}

	ea32Clear(&pContainerTypes);

	GetAllServerTypesForExe(pNormalExeName, &pContainerTypes);

	if (!ea32Size(&pContainerTypes))
	{
		estrPrintf(&pResultString, "ERROR: converted %s to %s, but that doesn't seem to be the exe namefor any server types",
			pFrankenExeName, pNormalExeName);
		return pResultString;
	}

	bUsesX64 = false;

	if (gbLaunch64BitWhenPossible)
	{
		for (i=0; i < ea32Size(&pContainerTypes); i++)
		{
			if (gServerTypeInfo[pContainerTypes[i]].executableName64_original[0])
			{
				bUsesX64 = true;
			}
		}
	}

	if (!gExecutableDirectory[0])
	{
		return "ERROR: gExecutableDirectory somehow not set";
	}

	estrPrintf(&pFullExeName, "%s/%s", gExecutableDirectory, pFrankenExeName);


	estrCopy(&pFullPdbName, &pFullExeName);
	estrReplaceOccurrences(&pFullPdbName, ".exe", ".pdb");



	if (bUsesX64)
	{	
		bool b32BitExeExists, b32BitPdbExists;

		estrPrintf(&pFullX64ExeName, "%s/%s", gExecutableDirectory, pFrankenX64Name);

		if (!fileExists(pFullX64ExeName))
		{
			estrPrintf(&pResultString, "file %s doesn't seem to exist", pFullX64ExeName);
			return pResultString;
		}

		estrCopy(&pFullX64PdbName, &pFullX64ExeName);
		estrReplaceOccurrences(&pFullX64PdbName, ".exe", ".pdb");

		if (!fileExists(pFullX64PdbName))
		{
			estrPrintf(&pResultString, "file %s doesn't seem to exist", pFullX64PdbName);
			return pResultString;
		}

		b32BitExeExists = fileExists(pFullExeName);
		b32BitPdbExists = fileExists(pFullPdbName);

		if (b32BitExeExists != b32BitPdbExists)
		{
			estrConcatf(&pResultString, "ERROR: 32 bit files %s and %s must either both exist or both not exist",
				pFullExeName, pFullPdbName);
			return pResultString;
		}

		if (b32BitExeExists)
		{
			bSend32Bit = true;
		}
		else
		{
			estrDestroy(&pFullExeName);
			estrDestroy(&pFullPdbName);
			estrConcatf(&pResultString, "WARNING: 32 bit files %s and %s doesn't seem to exist, not necessarily fatal.\n\n", pFullExeName, pFullPdbName);
		}
	}
	else
	{
		if (!fileExists(pFullExeName))
		{
			estrPrintf(&pResultString, "file %s doesn't seem to exist", pFullExeName);
			return pResultString;
		}
		if (!fileExists(pFullPdbName))
		{
			estrPrintf(&pResultString, "file %s doesn't seem to exist", pFullPdbName);
			return pResultString;
		}

		bSend32Bit = true;
	}


	estrConcatf(&pResultString, "SUCCESS: Frankbuild approved for %s, which will replace %s.\nContainer types affected: ",
		pFrankenExeName, pNormalExeName);

	for (i=0; i < ea32Size(&pContainerTypes); i++)
	{
		estrConcatf(&pResultString, "%s%s", i == 0 ? "" : ", ", GlobalTypeToName(pContainerTypes[i]));
	}

	estrConcatf(&pResultString, "\n");

	pFrankenBuild = GetNewPotentialFrankenBuild(iForcedID);
	pFrankenBuild->pContainerTypes = pContainerTypes;
	pContainerTypes = NULL;
	pFrankenBuild->pFullExeName = pFullExeName;
	pFrankenBuild->pFullX64ExeName = pFullX64ExeName;
	pFrankenBuild->pFullPdbName = pFullPdbName;
	pFrankenBuild->pFullX64PdbName = pFullX64PdbName;

	pFrankenBuild->bSend32Bit = bSend32Bit;

	pFullExeName = pFullX64ExeName = pFullPdbName = pFullX64PdbName = NULL;

	estrCopy2(&pFrankenBuild->pExeName, pFrankenExeName);
	pFrankenBuild->pExeX64Name = pFrankenX64Name;
	pFrankenX64Name = NULL;

	estrConcatf(&pResultString, "\n\nTo begin this frankenbuild, execute BeginFrankenbuildHotpush, ID %u",
		pFrankenBuild->iID);

	return pResultString;
}

AUTO_COMMAND ACMD_CATEGORY(shard);
char *RequestFrankenBuildHotpush_ForceID(char *pFrankenExeName, U32 iForcedID)
{
	return RequestFrankenBuildHotpush(pFrankenExeName, iForcedID);
}

AUTO_STRUCT; 
typedef struct FileReport
{
	char *pFullName; AST(ESTRING)
	char *pShortName; AST(ESTRING)//extension, no directory
	U32 iLastModifiedTime;
} FileReport;

AUTO_STRUCT;
typedef struct FullExeReport
{
	FileReport Exe32;
	FileReport Pdb32;
	FileReport Exe64;
	FileReport Pdb64;
	FileReport commentFile;
	char *pComment; 
	U32 *pServerTypes;
	bool bServerTypesX64;
	bool bServerTypeLaunchFromCore;
	bool bServerTypeNotLaunchFromCore;
	bool bIsController;

	char *pDir; AST(ESTRING)
	char *pFrankenExeString; AST(ESTRING)
	char *pFrankenPdbString; AST(ESTRING)

} FullExeReport;

bool FileMatchesCurExecutable(const char *pOtherFile)
{
	static U32 iCurExeCRC = 0;

	if (!iCurExeCRC)
	{
		iCurExeCRC = cryptAdlerFile(getExecutableName());
	}

	if (iCurExeCRC == cryptAdlerFile(pOtherFile))
	{
		return true;
	}

	return false;
}

FullExeReport *GetExeReport(char *pSourceName_in)
{

	char sourceName_Temp[CRYPTIC_MAX_PATH];
	char dir[CRYPTIC_MAX_PATH];
	char shortName[CRYPTIC_MAX_PATH];

	static char *pBaseName = NULL;
	
	//if the source name is "appserver_foo", then pFrankenString is "foo",
	//fullFrankeExeString is "_foo.exe", pFullFrankenPdbString = "_foo.pdb"
	static char *pFrankenString = NULL;
	static char *pFullFrankenExeString = NULL;
	static char *pFullFrankenPdbString = NULL;

	static char *pBaseName32 = NULL;
	static char *pBaseName64 = NULL;

	static U32 *pServerTypes = NULL;

	int i;

	char *pLastPeriod;
	char *pFirstUnderscore;

	FullExeReport *pExeReport;
	
	strcpy(sourceName_Temp, pSourceName_in);
	getFileNameNoDir(shortName, sourceName_Temp);
	strcpy(dir, getDirectoryName(sourceName_Temp));

	estrCopy2(&pBaseName, shortName);
	pLastPeriod = strrchr(pBaseName, '.');
	pFirstUnderscore = strchr(pBaseName, '_');

	if (!pLastPeriod || !pFirstUnderscore || pFirstUnderscore > pLastPeriod)
	{
		return NULL;
	}

	estrClear(&pFrankenString);
	estrSetSize(&pFrankenString, pLastPeriod - pFirstUnderscore - 1);
	memcpy(pFrankenString, pFirstUnderscore + 1, pLastPeriod - pFirstUnderscore - 1);
	estrRemove(&pBaseName, pFirstUnderscore - pBaseName, pLastPeriod - pFirstUnderscore);


	estrCopy(&pBaseName64, &pBaseName);
	estrCopy(&pBaseName32, &pBaseName);

	if (strstri(pBaseName, "X64"))
	{
		if (estrReplaceOccurrences_CaseInsensitive(&pBaseName32, "X64.exe", ".exe") != 1)
		{
			return NULL;
		}
	}
	else
	{
		if (estrReplaceOccurrences_CaseInsensitive(&pBaseName64, ".exe", "X64.exe") != 1)
		{
			return NULL;
		}
	}

	ea32Clear(&pServerTypes);

	GetAllServerTypesForExe(pBaseName32, &pServerTypes);

	if (!ea32Size(&pServerTypes))
	{
		return NULL;
	}

	if (stricmp(pBaseName32, "controller.exe") == 0)
	{
		if (FileMatchesCurExecutable(pSourceName_in))
		{
			return NULL;
		}
	}


	estrPrintf(&pFullFrankenExeString, "_%s.exe", pFrankenString);
	estrPrintf(&pFullFrankenPdbString, "_%s.pdb", pFrankenString);

	pExeReport = StructCreate(parse_FullExeReport);

	pExeReport->pServerTypes = pServerTypes;
	pServerTypes = NULL;

	estrPrintf(&pExeReport->Exe32.pFullName, "%s/%s", dir, pBaseName32);
	estrReplaceOccurrences_CaseInsensitive(&pExeReport->Exe32.pFullName, ".exe", pFullFrankenExeString);
	estrGetDirAndFileName(pExeReport->Exe32.pFullName, NULL, &pExeReport->Exe32.pShortName);

	estrPrintf(&pExeReport->Pdb32.pFullName, "%s/%s", dir, pBaseName32);
	estrReplaceOccurrences_CaseInsensitive(&pExeReport->Pdb32.pFullName, ".exe", pFullFrankenPdbString);
	estrGetDirAndFileName(pExeReport->Pdb32.pFullName, NULL, &pExeReport->Pdb32.pShortName);

	estrPrintf(&pExeReport->Exe64.pFullName, "%s/%s", dir, pBaseName64);
	estrReplaceOccurrences_CaseInsensitive(&pExeReport->Exe64.pFullName, ".exe", pFullFrankenExeString);
	estrGetDirAndFileName(pExeReport->Exe64.pFullName, NULL, &pExeReport->Exe64.pShortName);

	estrPrintf(&pExeReport->Pdb64.pFullName, "%s/%s", dir, pBaseName64);
	estrReplaceOccurrences_CaseInsensitive(&pExeReport->Pdb64.pFullName, ".exe", pFullFrankenPdbString);
	estrGetDirAndFileName(pExeReport->Pdb64.pFullName, NULL, &pExeReport->Pdb64.pShortName);

	estrPrintf(&pExeReport->commentFile.pFullName, "%s/%s", dir, pBaseName32);
	estrReplaceOccurrences_CaseInsensitive(&pExeReport->commentFile.pFullName, ".exe", pFullFrankenExeString);
	estrReplaceOccurrences_CaseInsensitive(&pExeReport->commentFile.pFullName, ".exe", ".txt");

	pExeReport->Exe32.iLastModifiedTime = fileLastChangedSS2000(pExeReport->Exe32.pFullName);
	pExeReport->Pdb32.iLastModifiedTime = fileLastChangedSS2000(pExeReport->Pdb32.pFullName);
	pExeReport->Exe64.iLastModifiedTime = fileLastChangedSS2000(pExeReport->Exe64.pFullName);
	pExeReport->Pdb64.iLastModifiedTime = fileLastChangedSS2000(pExeReport->Pdb64.pFullName);
	pExeReport->commentFile.iLastModifiedTime = fileLastChangedSS2000(pExeReport->commentFile.pFullName);

	for (i=0; i < ea32Size(&pExeReport->pServerTypes); i++)
	{
		ServerTypeInfo *pInfo = &gServerTypeInfo[pExeReport->pServerTypes[i]];

		if (pInfo->executableName64_original[0])
		{
			pExeReport->bServerTypesX64 = true;
		}

		if (pInfo->bLaunchFromCoreDirectory)
		{
			pExeReport->bServerTypeLaunchFromCore = true;
		}
		else
		{
			pExeReport->bServerTypeNotLaunchFromCore = true;
		}
	}

	if (pExeReport->bServerTypeLaunchFromCore && pExeReport->bServerTypeNotLaunchFromCore)
	{
		assertmsgf(0, "Controller things servers running %s should be launched from both core and non-core directory",
			pBaseName32);
	}

	if (ea32Find(&pExeReport->pServerTypes, GLOBALTYPE_CONTROLLER) >= 0)
	{
		pExeReport->bIsController = true;
	}

	estrCopy2(&pExeReport->pDir, dir);
	estrCopy2(&pExeReport->pFrankenPdbString, pFullFrankenPdbString);
	estrCopy2(&pExeReport->pFrankenExeString, pFullFrankenExeString);

	if (pExeReport->commentFile.iLastModifiedTime)
	{
		pExeReport->pComment = fileAlloc(pExeReport->commentFile.pFullName, NULL);
	}
	
	return pExeReport;
}

void DumpFileReport(FileReport *pReport, char *pTitle)
{
	if (pReport->iLastModifiedTime)
	{
		printf("%s: ", pTitle);
		consolePushColor();
		consoleSetFGColor(COLOR_RED | COLOR_GREEN | COLOR_BLUE | COLOR_BRIGHT);
		printf("%s\n", pReport->pFullName);
		consolePopColor();
		printf("\t(Modified: %s)\n", timeGetLocalDateStringFromSecondsSince2000(pReport->iLastModifiedTime));
	}
	else
	{
		printf("%s: DOES NOT EXIST\n", pTitle);
	}
}

void DumpReport(FullExeReport *pReport)
{
	int i;

	printf("Potential frankenbuild .exes/.pdbs found:\n");

	if (pReport->pComment)
	{
		printf("Comment: ");
		consolePushColor();
		consoleSetFGColor(COLOR_RED | COLOR_BLUE | COLOR_BRIGHT);
		printf("%s\n", pReport->pComment);
		consolePopColor();
	}
	else
	{
		consolePushColor();
		consoleSetFGColor(COLOR_RED | COLOR_BLUE | COLOR_BRIGHT);
		printf("No comment specified\n");
		consolePopColor();
	}
	
	DumpFileReport(&pReport->Exe32, "32 bit exe");
	DumpFileReport(&pReport->Pdb32, "32 bit pdb");
	DumpFileReport(&pReport->Exe64, "64 bit exe");
	DumpFileReport(&pReport->Pdb64, "64 bit pdb");

	printf("Server types affected: ");
	for (i=0; i < ea32Size(&pReport->pServerTypes); i++)
	{
		printf("%s%s", i == 0 ? "" : ", ", GlobalTypeToName(pReport->pServerTypes[i]));
	}
	printf("\n");

	if (!!pReport->Exe32.iLastModifiedTime != !!pReport->Pdb32.iLastModifiedTime
		|| !!pReport->Exe64.iLastModifiedTime != !!pReport->Pdb64.iLastModifiedTime)
	{
		consolePushColor();
		consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
		printf("WARNING: Frankenbuilds generally should have a corresponding .exe for each .pdb and vice versa.\n");
		consolePopColor();
}

	if (gbLaunch64BitWhenPossible && pReport->bServerTypesX64 && !pReport->Exe64.iLastModifiedTime)
	{
		consolePushColor();
		consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
		printf("WARNING: These server types may try to launch in 64 bit mode, no 64 bit exe found\n");
		consolePopColor();
	}

	if (!pReport->bServerTypesX64 && !pReport->Exe32.iLastModifiedTime)
	{
		consolePushColor();
		consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
		printf("WARNING: These server types launch in 32 bit mode, no 32 bit exe found\n");
		consolePopColor();
	}
}

void DeleteFileSafe(char *pFileName)
{
	if (!DeleteFile(pFileName))
	{
		consolePushColor();
		consoleSetColor(0, COLOR_RED);
		printf("WARNING: unable to delete file %s\n", pFileName);
		consolePopColor();
	}
}

void DeleteSingleFile(FileReport *pReport)
{
	if (pReport->iLastModifiedTime)
	{
		printf("Deleting %s\n", pReport->pFullName);
		DeleteFileSafe(pReport->pFullName);
	}
}

void DeleteFilesFromReport(FullExeReport *pReport)
{
	consolePushColor();
	consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
	DeleteSingleFile(&pReport->Exe32);
	DeleteSingleFile(&pReport->Pdb32);
	DeleteSingleFile(&pReport->Exe64);
	DeleteSingleFile(&pReport->Pdb64);
	DeleteSingleFile(&pReport->commentFile);
	consolePopColor();

}

void CopyReportFile(FileReport *pReport, char *pDestDir, char *pFrankenString, char *pRealExtension)
{
	static char *pTempString = NULL;
	char fullName[CRYPTIC_MAX_PATH];
	char shortName[CRYPTIC_MAX_PATH];
	char systemString[1024];

	if (!pReport->iLastModifiedTime)
	{
		return;
	}

	strcpy(fullName, pReport->pFullName);
	getFileNameNoDir(shortName, fullName);

	estrPrintf(&pTempString, "%s/%s", pDestDir, shortName);
	estrReplaceOccurrences_CaseInsensitive(&pTempString, pFrankenString, pRealExtension);

	printf("About to copy %s to %s\n", pReport->pFullName, pTempString);

	sprintf(systemString, "copy %s %s", pReport->pFullName, pTempString);
	backSlashes(systemString);
	strcat(systemString, "/Y");

	if (system_w_timeout(systemString, NULL, 600))
	{
		consolePushColor();
		consoleSetColor(0, COLOR_RED|COLOR_BRIGHT);
		printf("Could not copy %s to %s\n", pReport->pFullName, pTempString);
		consolePopColor();
	}
}

void RestartWithNewControllerFiles(char *pExeName, char *pPdbName)
{
	char *pCurExeName = NULL;
	char *pCurExeDir = NULL;
	char *pBackupOfCurExeName = NULL;
	char *pBackupFullName = NULL;
	char systemString[1024];
	char originalWorkingDir[CRYPTIC_MAX_PATH];

	

	estrGetDirAndFileName(getExecutableName(), &pCurExeDir, &pCurExeName);
	estrCopy(&pBackupOfCurExeName, &pCurExeName);
	estrReplaceOccurrences_CaseInsensitive(&pBackupOfCurExeName, ".exe", ".bak");
	estrPrintf(&pBackupFullName, "%s/%s", pCurExeDir, pBackupOfCurExeName);

	if (fileExists(pBackupFullName))
	{
		if (!DeleteFile(pBackupFullName))
		{
			assertmsgf(0, "Couldn't erase %s while restarting controller", pBackupOfCurExeName);
		}
	}



	fileGetcwd(SAFESTR(originalWorkingDir));
	assert(chdir(pCurExeDir) == 0);

	sprintf(systemString, "rename %s %s", pCurExeName, pBackupOfCurExeName);
	backSlashes(systemString);


	printf("About to Execute: %s\n", systemString);

	if (system_w_timeout(systemString, NULL, 600))
	{
		assertmsgf(0, "couldn't rename %s to %s while restarting controller", pCurExeName, pBackupOfCurExeName);
	}

	sprintf(systemString, "copy %s %s ", pExeName, getExecutableName());
	backSlashes(systemString);

	printf("About to Execute: %s\n", systemString);

	if (system_w_timeout(systemString, NULL, 600))
	{
		assertmsgf(0, "couldn't copy %s to %s while restarting controller", pExeName, getExecutableName());
	}

	if (pPdbName)
	{
		char *pFullCurPdbName = NULL;
		estrCopy2(&pFullCurPdbName, getExecutableName());
		estrReplaceOccurrences_CaseInsensitive(&pFullCurPdbName, ".exe", ".pdb");


		sprintf(systemString, "copy %s %s", pPdbName, pFullCurPdbName);
		backSlashes(systemString);

		printf("About to Execute: %s\n", systemString);

		if (system_w_timeout(systemString, NULL, 600))
		{
			assertmsgf(0, "couldn't copy %s to %s while restarting controller", pPdbName, pFullCurPdbName);
		}
	}


	assert(chdir(originalWorkingDir) == 0);

	system_detach(GetCommandLine(), 0, 0);

	exit(0);
}

void UseFilesFromReport(FullExeReport *pReport)
{
	int iBrk = 0;
	char *pExeDir = pReport->bServerTypeLaunchFromCore ? gCoreExecutableDirectory : gExecutableDirectory;
	static char *pSysString = NULL;
	static char *pTempString = NULL;
	int i;
	char *pReportString = NULL;



	if (!pReport->commentFile.iLastModifiedTime)
	{
		int iLen = 0;
		char buf[1024] = {0};
		FILE *pFile;

		printf("Please enter a comment for this frankenbuild.\n");
		
		while (1)
		{
			Sleep(1);

			if (_kbhit())
			{
				char c = _getch();
				if (c == 13)
				{
					break;
				}

				if (iLen < 1024)
				{
					buf[iLen++] = c;
					printf("%c", c);
				}
			}
		}

		pFile = fopen(pReport->commentFile.pFullName, "wt");
		if (pFile)
		{
			fprintf(pFile, "%s", buf);
			fclose(pFile);
		}
		printf("\n");
		SAFE_FREE(pReport->pComment);
		pReport->pComment = strdup(buf);
	}

	if (pReport->bIsController)
	{
		if (!Confirm("This will replace the currently running controller and restart it... really proceed?"))
		{
			return;
		}

		RestartWithNewControllerFiles(pReport->Exe32.pFullName, pReport->Pdb32.iLastModifiedTime ? pReport->Pdb32.pFullName : NULL);
	}

	estrPrintf(&pReportString, "AT STARTUP, for container types ");
	for (i = 0; i < ea32Size(&pReport->pServerTypes); i++)
	{	
		estrConcatf(&pReportString, "%s%s", i == 0 ? "" : ", ", GlobalTypeToName(pReport->pServerTypes[i]));
	}

	if (pReport->Exe32.iLastModifiedTime)
	{
		estrConcatf(&pReportString, " setting 32-bit executable to %s", pReport->Exe32.pFullName);
		if (pReport->Exe64.iLastModifiedTime)
		{
			estrConcatf(&pReportString, " and");
		}
	}
	if (pReport->Exe64.iLastModifiedTime)
	{
		estrConcatf(&pReportString, " setting 64-bit executable to %s", pReport->Exe64.pFullName);
	}

	estrConcatf(&pReportString, ".");

	consolePushColor();
	consoleSetFGColor(COLOR_GREEN|COLOR_BRIGHT);

	printf("Loading, compressing and CRCing these files\n");
	{
		char *pTempPDBName = NULL;
		char *pErrorString = NULL;

		CompressedFileCache *pCache;
		if (pReport->Exe32.iLastModifiedTime)
		{
			pCache = Controller_GetCompressedFileCache(pReport->Exe32.pShortName);
			if (!pCache || !pCache->pCompressedBuffer)
			{
				estrConcatf(&pErrorString, "Unable to load %s, frankenbuild has failed\n", pReport->Exe32.pShortName);
			}
			estrCopy2(&pTempPDBName, pReport->Exe32.pShortName);
			estrReplaceOccurrences(&pTempPDBName, ".exe", ".pdb");
			pCache = Controller_GetCompressedFileCache(pTempPDBName);
			if (!pCache || !pCache->pCompressedBuffer)
			{
				estrConcatf(&pErrorString, "Unable to load %s, frankenbuild has failed\n", pTempPDBName);
			}
		}
		if (pReport->Exe64.iLastModifiedTime)
		{
			pCache = Controller_GetCompressedFileCache(pReport->Exe64.pShortName);
			if (!pCache || !pCache->pCompressedBuffer)
			{
				estrConcatf(&pErrorString, "Unable to load %s, frankenbuild has failed\n", pReport->Exe64.pShortName);
			}
			estrCopy2(&pTempPDBName, pReport->Exe64.pShortName);
			estrReplaceOccurrences(&pTempPDBName, ".exe", ".pdb");
			pCache = Controller_GetCompressedFileCache(pTempPDBName);
			if (!pCache || !pCache->pCompressedBuffer)
			{
				estrConcatf(&pErrorString, "Unable to load %s, frankenbuild has failed\n", pTempPDBName);
			}
		}	
		estrDestroy(&pTempPDBName);

		if (estrLength(&pErrorString))
		{
			consoleSetColor(0, COLOR_RED|COLOR_BRIGHT);
			printf("One or more errors while loading Frankenbuild files: %s\n", pErrorString);
			printf("Halting execution... press a key to continue\n");
			(void)_getch();
			exit(-1);
		}
	}


	printf("Internally activating these files\n");



	for (i = 0; i < ea32Size(&pReport->pServerTypes); i++)
	{		
		char *pErrorString = NULL;
		ApplyFrankenbuildInternal(pReport->pServerTypes[i], 
			pReport->Exe32.iLastModifiedTime ? pReport->Exe32.pShortName : NULL, 
			pReport->Exe64.iLastModifiedTime ? pReport->Exe64.pShortName : NULL, 
			pReport->pComment, &pErrorString);
		if (estrLength(&pErrorString))
		{
			consolePushColor();
			consoleSetColor(0, COLOR_RED|COLOR_BRIGHT);
			printf("Something went wrong while applying to %s: %s\n", GlobalTypeToName(pReport->pServerTypes[i]), pErrorString);

			estrConcatf(&pReportString, "\nSomething went wrong while applying to %s: %s\n", GlobalTypeToName(pReport->pServerTypes[i]), pErrorString);

			consolePopColor();
			estrDestroy(&pErrorString);
		}
	}

	AddFrankenBuildReport("%s", pReportString);
	estrDestroy(&pReportString);

	/*
	printf("About to copy files from %s to %s, where they will presumably be launched\n",
		pReport->pDir, pExeDir);
	
	CopyReportFile(&pReport->Exe32, pExeDir, pReport->pFrankenExeString, ".exe");
	CopyReportFile(&pReport->Pdb32, pExeDir, pReport->pFrankenPdbString, ".pdb");
	CopyReportFile(&pReport->Exe64, pExeDir, pReport->pFrankenExeString, ".exe");
	CopyReportFile(&pReport->Pdb64, pExeDir, pReport->pFrankenPdbString, ".pdb");
	*/






	consolePopColor();

}

void BackupSingleFile(FileReport *pReport)
{
	char fullName[CRYPTIC_MAX_PATH];
	char dirName[CRYPTIC_MAX_PATH];
	char shortName[CRYPTIC_MAX_PATH];
	char backupName[CRYPTIC_MAX_PATH];
	char systemString[1024];
	int iResult;

	if (!pReport->iLastModifiedTime)
	{
		return;
	}

	strcpy(fullName, pReport->pFullName);
	getFileNameNoDir(shortName, fullName);
	strcpy(dirName, getDirectoryName(fullName));

	sprintf(backupName, "%s/bak/%s", dirName, shortName);

	mkdirtree_const(backupName);

	sprintf(systemString, "copy %s %s", pReport->pFullName, backupName);
	backSlashes(systemString);
	strcat(systemString, " /y");

	iResult = system_w_timeout(systemString, NULL, 600);

	if (iResult)
	{
		consolePushColor();
		consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
		printf("Unable to copy %s to %s\n", pReport->pFullName, backupName);
		consolePopColor();
		return;
	}

	DeleteFileSafe(pReport->pFullName);

	printf("Copied %s to %s\n", pReport->pFullName, backupName);
}

void BackupFilesFromReport(FullExeReport *pReport)
{
	consolePushColor();
	consoleSetFGColor(COLOR_GREEN);
	BackupSingleFile(&pReport->Exe32);
	BackupSingleFile(&pReport->Pdb32);
	BackupSingleFile(&pReport->Exe64);
	BackupSingleFile(&pReport->Pdb64);
	BackupSingleFile(&pReport->commentFile);
	consolePopColor();
}

bool Confirm(char *pString)
{
	consolePushColor();
	consoleSetFGColor(COLOR_RED | COLOR_GREEN | COLOR_BLUE | COLOR_BRIGHT);
	printf("%s (y/n)?\n", pString);
	consolePopColor();

	while (1)
	{
		Sleep(1);

		if (_kbhit())
		{
			char c = _getch();

			if (toupper(c) == 'Y')
			{
				return true;
			}

			if (toupper(c) == 'N')
			{
				return false;
			}
		}
	}
}

AUTO_COMMAND;
void AddFrankenBuildDir(char *pName)
{
	if (strcmp(pName, "1") != 0)
	{
		assertmsgf(0, "Franken build dirs no longer supported... just put 1 to turn on the system, and put the frankenbuilt exes directly in the executable dir");
	}
	gbCheckForFrankenbuildExes = true;
}


int SortFileReports(const FullExeReport **ppReport1, const FullExeReport **ppReport2)
{
	U32 iTime1, iTime2;
	if ((*ppReport1)->bIsController && !(*ppReport2)->bIsController)
	{
		return -1;
	}

	if ((*ppReport2)->bIsController && !(*ppReport1)->bIsController)
	{
		return 1;
	}

	if ((*ppReport1)->Exe32.iLastModifiedTime)
	{
		iTime1 = (*ppReport1)->Exe32.iLastModifiedTime;
	}
	else
	{
		iTime1 = (*ppReport1)->Exe64.iLastModifiedTime;
	}

	if ((*ppReport2)->Exe32.iLastModifiedTime)
	{
		iTime2 = (*ppReport2)->Exe32.iLastModifiedTime;
	}
	else
	{
		iTime2 = (*ppReport2)->Exe64.iLastModifiedTime;
	}

	if (iTime1 < iTime2)
	{
		return -1;
	}
	if (iTime1 > iTime2)
	{
		return 1;
	}

	return 0;
}


//find all potential frankenbuild exes. For each one, prompt use to (U)se, (D)elete, (B)rchive or (I)eave
void CheckForPotentialFrankenbuildExes(void)
{
	char **ppAllFiles = NULL;
	int i, iDirNum, j;
	char **ppDirs = NULL;

	char fullName[CRYPTIC_MAX_PATH];

	FullExeReport **ppFullReports = NULL;

	printf("About to scan for potential frankenbuild exes...\n");

	if (gExecutableDirectory[0])
	{
		fileLocateWrite(gExecutableDirectory, fullName);
		eaPushUnique(&ppDirs, (void*)allocAddString(fullName));
	}

	if (gCoreExecutableDirectory[0])
	{
		fileLocateWrite(gCoreExecutableDirectory, fullName);
		eaPushUnique(&ppDirs, (void*)allocAddString(fullName));
	}

	for (iDirNum = 0; iDirNum < eaSize(&ppDirs); iDirNum++)
	{
		char **ppTempList = fileScanDirNoSubdirRecurse(ppDirs[iDirNum]);
		
		printf("Scanning %s for potential frankenbuild .exes\n", ppDirs[iDirNum]);

		for (i=0; i < eaSize(&ppTempList); i++)
		{
			if (strEndsWith(ppTempList[i], ".exe"))
			{
				char shortName[128];
				getFileNameNoExtNoDirs(shortName, ppTempList[i]);
				if (strchr(shortName, '_'))
				{
					eaPushUnique(&ppAllFiles, (void*)allocAddString(ppTempList[i]));
				}
			}
		}

		fileScanDirFreeNames(ppTempList);
	}

	for (i=0; i < eaSize(&ppAllFiles); i++)
	{
		FullExeReport *pReport = GetExeReport(ppAllFiles[i]);

		if (pReport)
		{
			bool bFound = false;
			for (j=0; j < eaSize(&ppFullReports); j++)
			{
				if (StructCompare(parse_FullExeReport, pReport, ppFullReports[j], 0, 0, 0) == 0)
				{
					bFound = true;
					StructDestroy(parse_FullExeReport, pReport);
					break;
				}
			}

			if (!bFound)
			{
				eaPush(&ppFullReports, pReport);
			}
		}
	}

	eaQSort(ppFullReports, SortFileReports);

	for (i=0; i < eaSize(&ppFullReports); i++)
	{
		consolePushColor();
		consoleSetFGColor(COLOR_GREEN|COLOR_BRIGHT);
		printf("------------------------------------------------\nFound a potential frankenbuild:\n--------------------------------\n");
		consolePopColor();

		DumpReport(ppFullReports[i]);
		printf("Please choose one: ");

		consolePushColor();
		consoleSetFGColor(COLOR_BLUE | COLOR_GREEN |COLOR_BRIGHT);
		printf("(U)se these files, (D)elete these files, (B)ackup these files, (I)gnore these files\n");
		consolePopColor();

		while (1)
		{
			Sleep(1);

			if (_kbhit())
			{
				char c = _getch();

				if (toupper(c) == 'I')
				{
					printf("IGNORING\n");
					break;
				}

				if (toupper(c) == 'D')
				{
					if (Confirm("Really DELETE?"))
					{
						printf("DELETING\n");
						DeleteFilesFromReport(ppFullReports[i]);
						break;
					}
				}

				if (toupper(c) == 'U')
				{
					if (Confirm("Really USE?"))
					{
						UseFilesFromReport(ppFullReports[i]);
						break;
					}
				}

				if (toupper(c) == 'B')
				{
					BackupFilesFromReport(ppFullReports[i]);
					break;
				}
			}
		}
	}	

	eaDestroyStruct(&ppFullReports, parse_FullExeReport);
}


bool DoCommandLineFragmentsDifferMeaningfully(char *pCmdline1, char *pCmdline2)
{
	char *pTemp1 = estrDup(pCmdline1 ? pCmdline1 : "");
	char *pTemp2 = estrDup(pCmdline2 ? pCmdline2 : "");
	bool bRetVal;

	estrReplaceOccurrences(&pTemp1, " - ", "");
	estrReplaceOccurrences(&pTemp2, " - ", "");

	estrTrimLeadingAndTrailingWhitespace(&pTemp1);
	estrTrimLeadingAndTrailingWhitespace(&pTemp2);

	bRetVal = (stricmp(pTemp1, pTemp2) != 0);

	return bRetVal;
}

char *pOverrideLaunchDirs[GLOBALTYPE_MAXTYPES] = { 0 };
char *pOverrideExeNames[GLOBALTYPE_MAXTYPES] = { 0 };

//note that we can't just poke these directly into gServerTypeInfo 

AUTO_COMMAND ACMD_COMMANDLINE;
void ServerTypeOverrideLaunchDir(char *pServerType, char *pLaunchDir)
{
	GlobalType eType = NameToGlobalType(pServerType);
	if (!eType)
	{
		AssertOrAlert("BAD_SVR_OVERRIDE_TYPE", "Unknown server type %s passed to ServerTypeOverrideLaunchDir", pServerType);
		return;
	}

	assert(eType < GLOBALTYPE_MAXTYPES);

	//check if serverTypeInfo is "ready"
	if (gServerTypeInfo[GLOBALTYPE_OBJECTDB].bInUse)
	{
		estrCopy2(&gServerTypeInfo[eType].pOverrideLaunchDir, pLaunchDir);
	}
	else
	{
		pOverrideLaunchDirs[eType] = strdup(pLaunchDir);
	}
}

AUTO_COMMAND ACMD_COMMANDLINE;
void ServerTypeOverrideExeName(char *pServerType, char *pExeName)
{
	GlobalType eType = NameToGlobalType(pServerType);

	if (!eType)
	{
		AssertOrAlert("BAD_SVR_OVERRIDE_TYPE", "Unknown server type %s passed to ServerTypeOverrideExeName", pServerType);
		return;
	}

	assert(eType < GLOBALTYPE_MAXTYPES);

	//check if serverTypeInfo is "ready"
	if (gServerTypeInfo[GLOBALTYPE_OBJECTDB].bInUse)
	{
		estrCopy2(&gServerTypeInfo[eType].pOverrideExeName, pExeName);
	}
	else
	{
		pOverrideExeNames[eType] = strdup(pExeName);
	}


}

void ApplyOverrideLaunchDirsAndExeNames(void)
{
	int i;

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (pOverrideLaunchDirs[i])
		{
			estrCopy2(&gServerTypeInfo[i].pOverrideLaunchDir, pOverrideLaunchDirs[i]);
		}

		if (pOverrideExeNames[i])
		{
			estrCopy2(&gServerTypeInfo[i].pOverrideExeName, pOverrideExeNames[i]);
		}
	}
}

char *beaconMasterServerAddr = NULL;

AUTO_COMMAND ACMD_NAME("SetShardBeaconMasterServer");
void contSetShardBeaconMasterServer(char *address)
{
	estrPrintf(&beaconMasterServerAddr, "%s", address);
}

AUTO_COMMAND_REMOTE_SLOW(char*) ACMD_NAME("GetShardBeaconMasterServer");
void contGetShardBeaconMasterServer(SlowRemoteCommandID iCmdID)
{
	SlowRemoteCommandReturn_GetShardBeaconMasterServer(iCmdID, beaconMasterServerAddr);
}

void StartQueryableProcessOnAllLaunchers(char *pCmdString)
{
	int i;

	for (i=0; i < giNumMachines; i++)
	{
		TrackedMachineState *pMachine = &gTrackedMachines[i];

		if (pMachine->pServersByType[GLOBALTYPE_LAUNCHER] && pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink)
		{
			Packet *pPak = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_REQUEST_LAUNCH_AND_QUERY_PROCESS);
			pktSendString(pPak, pCmdString);
			pktSend(&pPak);
		}
	}
}

//Extra command line arguments to add to patchclient.exe when doing shard priming
static char *spExtraPrimingCmdLine = NULL;
AUTO_CMD_ESTRING(spExtraPrimingCmdLine, ExtraPrimingCmdLine) ACMD_CONTROLLER_AUTO_SETTING(Misc);


AUTO_COMMAND;
char *DoPrePatching(char *pVersionName, int iNumMachineGroups, char *pYes, ACMD_SENTENCE pExtraPatchCommandLine)
{
	char *pFullCommandLine = NULL;
	char *pMachineSpecificCommandLine = NULL;
	static char *pRetString = NULL;
	char patchServer[256];
	int i;

	if (stricmp(pYes, "yes") != 0)
	{
		return "I guess you weren't sure.";
	}

	estrStackCreate(&pFullCommandLine);

	estrPrintf(&pFullCommandLine, "%s -project %sServer -sync -name %s -root %s/prepatch -useCRTPrintf -skipmirroring - %s - ",
		patchclientCmdLineEx(false, gExecutableDirectory, gCoreExecutableDirectory),
		GetProductName(), pVersionName, gExecutableDirectory, pExtraPatchCommandLine);
	
	if (triviaGetPatchTriviaForFile(SAFESTR(patchServer), gCoreExecutableDirectory, "PatchServer"))
	{
		estrConcatf(&pFullCommandLine, " -server %s", patchServer);
	}

	if (spExtraPrimingCmdLine && spExtraPrimingCmdLine[0])
	{
		estrConcatf(&pFullCommandLine, " - %s - ", spExtraPrimingCmdLine);
	}
	
	Controller_ResetPCLStatusMonitoringForTask(CONTROLLER_PCL_PRIMING);
	estrStackCreate(&pMachineSpecificCommandLine);

	for (i=0; i < giNumMachines; i++)
	{
		TrackedMachineState *pMachine = &gTrackedMachines[i];
	
		if (pMachine->pServersByType[GLOBALTYPE_LAUNCHER] && pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink)
		{
			Packet *pPak = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_REQUEST_LAUNCH_AND_QUERY_PROCESS);
			estrPrintf(&pMachineSpecificCommandLine, "%s - %s", pFullCommandLine, Controller_GetPCLStatusMonitoringCmdLine(pMachine->machineName, CONTROLLER_PCL_PRIMING));
			pktSendString(pPak, pMachineSpecificCommandLine);
			pktSend(&pPak);
			Controller_AddPCLStatusMonitoringTask(pMachine->machineName, CONTROLLER_PCL_PRIMING);
		}
	}

	estrDestroy(&pMachineSpecificCommandLine);

	estrPrintf(&pRetString, "Executing the following command on all machines: %s", pFullCommandLine);

	if (iNumMachineGroups && gbControllerMachineGroups_SystemIsActive)
	{
		char *pMyCriticalSystemName = StatusReporting_GetMyName();

		//for prepatching done through a machine group that is not yet patched, it will use sentryserver to execute 
		//the patching command directly, meaning we need to add in failure alerting, as we won't get reports back from 
		//the launcher, which doesn't exist
		//if we are doing status reporting, tell patch client to send an alert for us if it fails
		if (pMyCriticalSystemName && pMyCriticalSystemName[0])
		{
			char *pFailExecuteCmdLine = NULL;
			char *pSuperEscFailExecuteCmdLine = NULL;

			estrPrintf(&pFailExecuteCmdLine, "SendAlert -controllerTrackerName %s -criticalSystemName \"%s\" -alertKey PCL_FAIL -alertString \"PCL failure while priming machine {MachineName}: {ErrorString}\"",
				StatusReporting_GetControllerTrackerName(), pMyCriticalSystemName);
			estrSuperEscapeString(&pSuperEscFailExecuteCmdLine, pFailExecuteCmdLine);
			estrConcatf(&pFullCommandLine, " -superEsc FailExecute %s ", pSuperEscFailExecuteCmdLine);

			estrDestroy(&pFailExecuteCmdLine);
			estrDestroy(&pSuperEscFailExecuteCmdLine);
		}

		ControllerMachineGroups_ExecuteCommandOnNonPatchedMachines(pFullCommandLine, "shard priming", iNumMachineGroups, CONTROLLER_PCL_PRIMING);
	}


	estrDestroy(&pFullCommandLine);

	return "Priming begun";
}


void SimpleServerLog_AddTick(TrackedServerState *pServer)
{
	pServer->simpleLog.iUpdateTickTimes[pServer->simpleLog.iNextUpdateTickTime] = timeSecondsSince2000();
	if (pServer->simpleLog.iNextUpdateTickTime == MAX_UPDATE_TICK_TIMES_TO_SAVE_FOR_SIMPLE_SERVER_LOG - 1)
	{
		pServer->simpleLog.iNextUpdateTickTime = 0;
	}
	else
	{
		pServer->simpleLog.iNextUpdateTickTime++;
	}
}

void SimpleServerLog_AddUpdate(TrackedServerState *pServer, FORMAT_STR const char* fmt, ...)
{
	char *pTempStr = NULL;
	SimpleServerUpdate *pCurUpdate;
	
	estrStackCreate(&pTempStr);
	estrGetVarArgs(&pTempStr, fmt);
	if (!eaSize(&pServer->simpleLog.ppUpdates))
	{
		eaSetSize(&pServer->simpleLog.ppUpdates, MAX_UPDATES_TO_SAVE_FOR_SIMPLE_SERVER_LOG);
	}

	pCurUpdate = pServer->simpleLog.ppUpdates[pServer->simpleLog.iNextUpdate];
	if (!pCurUpdate)
	{
		pServer->simpleLog.ppUpdates[pServer->simpleLog.iNextUpdate] = pCurUpdate = StructCreate(parse_SimpleServerUpdate);
	}
		
	if (pServer->simpleLog.iNextUpdate == MAX_UPDATES_TO_SAVE_FOR_SIMPLE_SERVER_LOG - 1)
	{
		pServer->simpleLog.iNextUpdate = 0;
	}
	else
	{
		pServer->simpleLog.iNextUpdate++;
	}

	pCurUpdate->iTime = timeSecondsSince2000();
	SAFE_FREE(pCurUpdate->pString);
	pCurUpdate->pString = strdup(pTempStr);

	estrDestroy(&pTempStr);
}

static char *spDuration = NULL;
static void AddTickToSimpleLogString(TrackedServerState *pServer, int iIndex, char **ppOutStr)
{
	timeSecondsDurationToShortEString(timeSecondsSince2000() - pServer->simpleLog.iUpdateTickTimes[iIndex], &spDuration);
	estrConcatf(ppOutStr, "%s ago: Update tick\n", spDuration);
}
static void AddUpdateToSimpleLogString(TrackedServerState *pServer, int iIndex, char **ppOutStr)
{
	timeSecondsDurationToShortEString(timeSecondsSince2000() - pServer->simpleLog.ppUpdates[iIndex]->iTime, &spDuration);
	estrConcatf(ppOutStr, "%s ago: %s\n", spDuration, pServer->simpleLog.ppUpdates[iIndex]->pString);
}


void SimpleServerLog_MakeLogString(TrackedServerState *pServer, char **ppOutStr)
{
	int iNumTicksRemaining = MAX_UPDATE_TICK_TIMES_TO_SAVE_FOR_SIMPLE_SERVER_LOG;
	int iNumUpdatesRemaining = MAX_UPDATES_TO_SAVE_FOR_SIMPLE_SERVER_LOG;
	int iNextTickIndex = pServer->simpleLog.iNextUpdateTickTime;
	int iNextUpdateIndex = pServer->simpleLog.iNextUpdate;

	estrClear(ppOutStr);
	estrPrintf(ppOutStr, "Recent updates for %s, tracking up to %d update ticks, and up to %d other events\n",
		GlobalTypeAndIDToString(pServer->eContainerType, pServer->iContainerID), MAX_UPDATE_TICK_TIMES_TO_SAVE_FOR_SIMPLE_SERVER_LOG, MAX_UPDATES_TO_SAVE_FOR_SIMPLE_SERVER_LOG);

	while (iNumTicksRemaining && pServer->simpleLog.iUpdateTickTimes[iNextTickIndex] == 0)
	{
		iNumTicksRemaining--;
		iNextTickIndex = (iNextTickIndex + 1) % MAX_UPDATE_TICK_TIMES_TO_SAVE_FOR_SIMPLE_SERVER_LOG;
	}

	while (iNumUpdatesRemaining && pServer->simpleLog.ppUpdates[iNextUpdateIndex] == NULL)
	{
		iNumUpdatesRemaining--;
		iNextUpdateIndex = (iNextUpdateIndex + 1) % MAX_UPDATES_TO_SAVE_FOR_SIMPLE_SERVER_LOG;
	}

	while (iNumTicksRemaining || iNumUpdatesRemaining)
	{
		if (!iNumUpdatesRemaining || iNumTicksRemaining && (pServer->simpleLog.iUpdateTickTimes[iNextTickIndex] < pServer->simpleLog.ppUpdates[iNextUpdateIndex]->iTime))
		{
			AddTickToSimpleLogString(pServer, iNextTickIndex, ppOutStr);
			iNumTicksRemaining--;
			iNextTickIndex = (iNextTickIndex + 1) % MAX_UPDATE_TICK_TIMES_TO_SAVE_FOR_SIMPLE_SERVER_LOG;
		}
		else
		{
			AddUpdateToSimpleLogString(pServer, iNextUpdateIndex, ppOutStr);
			iNumUpdatesRemaining--;
			iNextUpdateIndex = (iNextUpdateIndex + 1) % MAX_UPDATES_TO_SAVE_FOR_SIMPLE_SERVER_LOG;
		}
	}
}

char *SimpleServerLog_GetLogString(TrackedServerState *pServer)
{
	static char *spRetVal = NULL;
	SimpleServerLog_MakeLogString(pServer, &spRetVal);
	return spRetVal;
}

AUTO_COMMAND;
char *GetSimpleLogString(char *pServerType, U32 iContainerID)
{
   GlobalType eType = NameToGlobalType(pServerType);
	static char *pRetString = NULL;


	if (eType)
	{
		TrackedServerState *pServer = FindServerFromID(eType, iContainerID);

		if (pServer)
		{
			return SimpleServerLog_GetLogString(pServer);
		}
	}

	estrPrintf(&pRetString, "Can't find %s(%u)", pServerType, iContainerID);
	return pRetString;
}

static bool sbAllowShardVersionMismatch = false;
bool AllowShardVersionMismatch(void)
{
	return sbAllowShardVersionMismatch;
}

AUTO_COMMAND ACMD_COMMANDLINE ACMD_NAME(AllowShardVersionMismatch);
void SetAllowShardVersionMismatch(bool bSet)
{
	int eType;
	TrackedServerState *pServer;

	char cmdString[128];

	sprintf(cmdString, "SetControllerReportsAllowShardVersionMismatch %d", bSet);

	for (eType = 0; eType < GLOBALTYPE_MAX; eType++)
	{
		if (gServerTypeInfo[eType].bInformAboutAllowShardVersionMismatch)
		{
			pServer = gpServersByType[eType];

			while (pServer)
			{
				if (pServer->pLink)
				{
					SendCommandDirectlyToServerThroughNetLink(pServer, cmdString);
				}
				else
				{
					RemoteCommand_CallLocalCommandRemotely(eType, pServer->iContainerID, cmdString);
				}

				pServer = pServer->pNext;
			}
		}
	}

	sbAllowShardVersionMismatch = bSet;
}

static int siSuddenPlayerDropCheck_Time = 60;
static int siSuddenPlayerDropCheck_Percent = 10;
static int siSuddenPlayerDropCheck_MinCount = 100;
static float sfSuddenPlayerDropCheck_RatioIncrease = 3.0f; 

//__CATEGORY Code which generates an alert when the num active players drops suddenly
//(Seconds) Interval that is considered a single CCU drop
AUTO_CMD_INT(siSuddenPlayerDropCheck_Time, SuddenPlayerDropCheck_Time) ACMD_CONTROLLER_AUTO_SETTING(SuddenPlayerDropChecking);

//(Percentage) ALERT - Percent CCU drop required for alerting
AUTO_CMD_INT(siSuddenPlayerDropCheck_Percent, SuddenPlayerDropCheck_Percent) ACMD_CONTROLLER_AUTO_SETTING(SuddenPlayerDropChecking);

//(CCU) Minimum required CCU for Sudden Drop Check to be enabled
AUTO_CMD_INT(siSuddenPlayerDropCheck_MinCount, SuddenPlayerDropCheck_MinCount) ACMD_CONTROLLER_AUTO_SETTING(SuddenPlayerDropChecking);


//(Ratio) If set, only generate alert if current drop was more than this many times bigger than
//drop in previous equivalent time period
AUTO_CMD_FLOAT(sfSuddenPlayerDropCheck_RatioIncrease, SuddenPlayerDropCheck_RatioIncrease) ACMD_CONTROLLER_AUTO_SETTING(SuddenPlayerDropChecking);


typedef struct SuddenPlayerDropCheckDataPoint
{
	U32 iTime;
	int iNumPlayers;
} SuddenPlayerDropCheckDataPoint;

static SuddenPlayerDropCheckDataPoint **sppDataPoints = NULL;

void ReportNumPlayersForForSuddenDrops(int iNumPlayers)
{
	U32 iCurTime = timeSecondsSince2000();
	U32 iCutoffTime = iCurTime - siSuddenPlayerDropCheck_Time * 2;
	
	SuddenPlayerDropCheckDataPoint *pDataPoint;

	//this list is twice as big as you might think, as we iterate only over the second half, using the first half
	//to check the ratio of current time period drop to previous equal time period drop
	while (eaSize(&sppDataPoints) && sppDataPoints[0]->iTime < iCutoffTime)
	{
		free(eaRemove(&sppDataPoints, 0));
	}

	if (eaSize(&sppDataPoints) > 1)
	{
		int iCurIndex;

		for (iCurIndex = (eaSize(&sppDataPoints) + 1) / 2; iCurIndex < eaSize(&sppDataPoints); iCurIndex++)
		{
			if (sppDataPoints[iCurIndex]->iNumPlayers >= siSuddenPlayerDropCheck_MinCount)
			{
				if (iNumPlayers < sppDataPoints[iCurIndex]->iNumPlayers * (100 - siSuddenPlayerDropCheck_Percent) / 100)
				{
					char *pDurationString = NULL;

					if (sfSuddenPlayerDropCheck_RatioIncrease)
					{
						//previous index set such that the time interval between prev index and cur index is the
						//same as between cur index and end of list
						int iPreviousIndex = iCurIndex - (eaSize(&sppDataPoints) - iCurIndex);
						int iPreviousDrop = sppDataPoints[iPreviousIndex]->iNumPlayers - sppDataPoints[iCurIndex]->iNumPlayers;

						if (iPreviousDrop > (sppDataPoints[iCurIndex]->iNumPlayers - iNumPlayers) / sfSuddenPlayerDropCheck_RatioIncrease)
						{
							continue;
						}
					}

					timeSecondsDurationToPrettyEString(iCurTime - sppDataPoints[iCurIndex]->iTime, &pDurationString);

					CRITICAL_NETOPS_ALERT("SUDDEN_PLAYER_DROP", "Player count dropped from %d to %d in the last %s. This is a drop of more than %d percent and might indicate something is wrong",
						sppDataPoints[iCurIndex]->iNumPlayers, iNumPlayers, pDurationString, siSuddenPlayerDropCheck_Percent);

					eaDestroyEx(&sppDataPoints, NULL);
					estrDestroy(&pDurationString);

					break;
				}
			}
		}
	}

	pDataPoint = malloc(sizeof(SuddenPlayerDropCheckDataPoint));
	pDataPoint->iNumPlayers = iNumPlayers;
	pDataPoint->iTime = iCurTime;

	eaPush(&sppDataPoints, pDataPoint);
}

void ResetNumPlayersForSuddenDrops(void)
{
	eaDestroyEx(&sppDataPoints, NULL);
}



/*
char testCASString[256] = "Foo bar";
char testCASNULLString[256] = "";
char *pTestCASEString = "foo bar2";
char *pTestCASNULLEString = NULL;
float testCASFloat = 3.5f;

AUTO_CMD_STRING(testCASString, testCASString) ACMD_CONTROLLER_AUTO_SETTING(test); 
AUTO_CMD_STRING(testCASNULLString, testCASNULLString) ACMD_CONTROLLER_AUTO_SETTING(test); 
AUTO_CMD_ESTRING(pTestCASEString, testCASEString) ACMD_CONTROLLER_AUTO_SETTING(test); 
AUTO_CMD_ESTRING(pTestCASNULLEString, testCASNULLEString) ACMD_CONTROLLER_AUTO_SETTING(test); 
AUTO_CMD_FLOAT(testCASFloat, testCASFloat) ACMD_CONTROLLER_AUTO_SETTING(test); 
*/

int OVERRIDE_LATELINK_ServerLib_GetNumGSMachines(void)
{
	return CountMachinesForServerType(GLOBALTYPE_GAMESERVER);
}



int giCheckForEmptyGameServersInterval = 0;
int giEmptyGameServerAlertCount = 0;
int giEmptyGameServerAlertPercent = 0;
int giEmptyGameServerAlertMinCutoff = 0;

//__CATEGORY Settings for alert when a certain number of game servers are empty
//(SECONDS) Check for the alert every n seconds (0 = never)
AUTO_CMD_INT(giCheckForEmptyGameServersInterval, CheckForEmptyGameServersInterval) ACMD_CONTROLLER_AUTO_SETTING(EmptyGameServerCheck);

//(GS.EXE COUNT) Alert if num empty GS > this (or 0)
AUTO_CMD_INT(giEmptyGameServerAlertCount, EmptyGameServerAlertCount) ACMD_CONTROLLER_AUTO_SETTING(EmptyGameServerCheck);

//(GS.EXE PERCENT) Alert if percent of GS.EXE which are empty > this (or 0)
AUTO_CMD_INT(giEmptyGameServerAlertPercent, giEmptyGameServerAlertPercent) ACMD_CONTROLLER_AUTO_SETTING(EmptyGameServerCheck);

//(GS.EXE COUNT) Must be this many total GS to activate this system
AUTO_CMD_INT(giEmptyGameServerAlertMinCutoff, EmptyGameServerAlertMinCutoff) ACMD_CONTROLLER_AUTO_SETTING(EmptyGameServerCheck);



void CheckForEmptyGameServers(void)
{
	int iTotalCount = 0;
	int iEmptyCount = 0;
	int iEmptyPercent = 0;


	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_GAMESERVER];

	while (pServer)
	{
		if (strstri(pServer->stateString, "gslRunning"))
		{
			iTotalCount++;

			if (pServer->pGameServerSpecificInfo->iNumPlayers == 0)
			{
				iEmptyCount++;
			}
		}

		pServer = pServer->pNext;
	}

	if (giEmptyGameServerAlertMinCutoff && iTotalCount < giEmptyGameServerAlertMinCutoff)
	{
		return;
	}

	if (iTotalCount)
	{
		iEmptyPercent = (iEmptyCount * 100) / iTotalCount;
	}


	if (giEmptyGameServerAlertCount && iEmptyCount > giEmptyGameServerAlertCount 
		|| giEmptyGameServerAlertPercent && iEmptyPercent > giEmptyGameServerAlertPercent)
	{
		if (!RetriggerAlertIfActive("TOO_MANY_EMPTY_GS", 0, 0, 0, 0))
		{
			char *pAlertString = NULL;

			estrPrintf(&pAlertString, "%d out of %d running gameservers are empty. This has triggered alert conditions set in ControllerAutoSettings->EmptyGameServerCheck", 
				iEmptyCount, iTotalCount);

			TriggerAlert("TOO_MANY_EMPTY_GS", pAlertString, ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, giCheckForEmptyGameServersInterval * 3, 0, 0, 0, 0, NULL, 0);

			estrDestroy(&pAlertString);
		}
	}


}


void 	ControllerStartLocalMemLeakTracking(void)
{
	if (!(isProductionMode() || gbTrackMemLeaksInDevMode))
	{
		return;
	}

	if (gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_BeginDelay 
		|| gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_Frequency
		|| gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_IncreaseAmountThatIsALeak_Megs
		|| gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_FirstIncreaseAllowance_Megs)
	{
		if (!gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_BeginDelay 
		&& gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_Frequency
		&& gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_IncreaseAmountThatIsALeak_Megs)
		{
			AssertOrAlert("BAD_CTRLR_MEMLEAK_CONFIG", "Some options missing for controller mem leak tracking. All of BeginDay (%d), freq (%d) and increaseAmount (%d) must be set",
				gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_BeginDelay, 
				gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_Frequency,
				gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_IncreaseAmountThatIsALeak_Megs);
		}
		else
		{
			BeginMemLeakTracking(gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_BeginDelay,
				gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_Frequency,
				((size_t)gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_IncreaseAmountThatIsALeak_Megs) * 1024 * 1024,
				((size_t)gServerTypeInfo[GLOBALTYPE_CONTROLLER].iMemLeakTracking_FirstIncreaseAllowance_Megs) * 1024 * 1024);
		}
	}
}

static bool sbOnlyKillExesInMyDirectory = false;
AUTO_CMD_INT(sbOnlyKillExesInMyDirectory, OnlyKillExesInMyDirectory);

void ControllerKillAll(char *pMachineName, const char *pExeName)
{
	static U32 *spPIDsToSkip = NULL;

	if (!pMachineName)
	{
		if (!ea32Size(&spPIDsToSkip) && GetPIDOfMCPThatStartedMe())
		{
			ea32Push(&spPIDsToSkip, GetPIDOfMCPThatStartedMe());
		}
	}

	

	if (pMachineName)
	{
		Packet *pPak;
		char *pFullCommandLine = NULL;

		estrPrintf(&pFullCommandLine, "cryptickillall -kill %s", pExeName);
		if (sbOnlyKillExesInMyDirectory)
		{
			estrConcatf(&pFullCommandLine, " -restrictToDir %s", getExecutableTopDir());
		}

		pPak = SentryServerComm_CreatePacket(MONITORCLIENT_LAUNCH, "Killing %s on %s", 
			pExeName, pMachineName);
		pktSendString(pPak, pMachineName);
		pktSendString(pPak, pFullCommandLine);
		SentryServerComm_SendPacket(&pPak);
		estrDestroy(&pFullCommandLine);
	}
	else
	{
		if (sbOnlyKillExesInMyDirectory)
		{
			KillAllEx(pExeName, true, spPIDsToSkip, true, true, getExecutableTopDir());	
		}
		else
		{
			KillAllEx(pExeName, true, spPIDsToSkip, true, true, NULL);
		}
	}
}

AUTO_STRUCT;
typedef struct KillAllDeferredCache
{
	char *pMachineName;
	char **ppNamesToKill;
} KillAllDeferredCache;

static StashTable sDeferredKillsTable = NULL;

void ControllerKillAllDeferred(char *pMachineName, const char *pExeName)
{
	KillAllDeferredCache *pCache;

	if (!sDeferredKillsTable)
	{
		sDeferredKillsTable = stashTableCreateWithStringKeys(4, StashDefault);
	}

	if (!stashFindPointer(sDeferredKillsTable, pMachineName, &pCache))
	{
		pCache = StructCreate(parse_KillAllDeferredCache);
		pCache->pMachineName = strdup(pMachineName);
		stashAddPointer(sDeferredKillsTable, pCache->pMachineName, pCache, false);
	}

	eaPush(&pCache->ppNamesToKill, strdup(pExeName));
}

void ControllerKillAll_DoDeferredKills(char *pMachineName)
{
	KillAllDeferredCache *pCache;
	int i;
	static char *spTopDir = NULL;

	if (sbOnlyKillExesInMyDirectory)
	{
		if (!spTopDir)
		{
			char fullDir[CRYPTIC_MAX_PATH];
			char *pFirstSlash;
			
			getExecutableDir(fullDir);

			backSlashes(fullDir);

			pFirstSlash = strchr(fullDir, '\\');
			if (pFirstSlash)
			{
				pFirstSlash = strchr(pFirstSlash + 1, '\\');
			
				if (pFirstSlash)
				{
					*(pFirstSlash + 1) = 0;
				}
			}

			spTopDir = strdup(fullDir);
		}
	}

	if (stashRemovePointer(sDeferredKillsTable, pMachineName, &pCache))
	{
		if (eaSize(&pCache->ppNamesToKill))
		{
			Packet *pPak;
			char *pFullCommandLine = NULL;

			estrPrintf(&pFullCommandLine, "cryptickillall");

			for (i = 0; i < eaSize(&pCache->ppNamesToKill); i++)
			{
				estrConcatf(&pFullCommandLine, " -kill %s", pCache->ppNamesToKill[i]);
			}

			if (sbOnlyKillExesInMyDirectory)
			{
				estrConcatf(&pFullCommandLine, " -restrictToDir %s", getExecutableTopDir());
			}

			pPak = SentryServerComm_CreatePacket(MONITORCLIENT_LAUNCH_AND_WAIT,
				"Deferred kills on %s", pMachineName);
			pktSendString(pPak, pMachineName);
			pktSendString(pPak, pFullCommandLine);
			SentryServerComm_SendPacket(&pPak);
		}

		StructDestroy(parse_KillAllDeferredCache, pCache);
	}
}


AUTO_FIXUPFUNC;
TextParserResult fixupTrackedMachineState(TrackedMachineState* pMachine, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
		{
	
			Controller_FindServerLaunchingWeight(pMachine, GLOBALTYPE_GAMESERVER, NULL, &pMachine->pCurGSLaunchWeight_ForServerMonOnly);
			if (!pMachine->pVNC)
			{
				estrPrintf(&pMachine->pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pMachine->VNCString);
			}

		}
		break;
	}

	return 1;
}

static bool sbPutEveryoneInLoginQueueAtLeastBriefly = false;
AUTO_CMD_INT(sbPutEveryoneInLoginQueueAtLeastBriefly, PutEveryoneInLoginQueueAtLeastBriefly) ACMD_COMMANDLINE ACMD_CATEGORY(test);

static bool sbPauseLoginQueue = false;
AUTO_CMD_INT(sbPauseLoginQueue, PauseLoginQueue) ACMD_COMMANDLINE ACMD_CATEGORY(test);


//__CATEGORY Basic configuration for this shard
//max players allowed in the shard, if there are more than this, then start having a login queue
static int siMaxPlayersInShard = 0;
AUTO_CMD_INT(siMaxPlayersInShard, MaxPlayersInShard) ACMD_CONTROLLER_AUTO_SETTING(BasicConfig);






static int siPresumedNumPlayers = 0;


static U32 siNextMainID = 1;
static U32 siNextVIPID = 1;


//this much delay (seconds) between successive QUEUING_SOON alerts
int siDelayBetweenQueueSoonAlerts = 3600;
AUTO_CMD_INT(siDelayBetweenQueueSoonAlerts, DelayBetweenQueueSoonAlerts) ACMD_CONTROLLER_AUTO_SETTING(Misc);

//this much delay (seconds) between successive QUEUING_NOW alerts
int siDelayBetweenQueueNowAlerts = 3600 * 4;
AUTO_CMD_INT(siDelayBetweenQueueNowAlerts, DelayBetweenQueueNowAlerts) ACMD_CONTROLLER_AUTO_SETTING(Misc);


//if the shard gets this percentage close to the queuing cutoff, generate a QUEUING_SOON alert
int siMaxPlayersPercentFullForPreQueueAlert = 90;
AUTO_CMD_INT(siMaxPlayersPercentFullForPreQueueAlert, MaxPlayersPercentFullForPreQueueAlert) ACMD_CONTROLLER_AUTO_SETTING(Misc);


void Controller_HereIsTotalNumPlayersForQueue(int iNumPlayers)
{
	U32 iIDOfLastGuySent_Main = 0;
	U32 iIDOfLastGuySent_Vip = 0;
	TrackedServerState *pServer;
	static U32 siLastSoonAlertedTime = 0;
	static U32 siLastNowAlertedTime = 0;
	static U32 siLastTimeQueueing = 0;



	siPresumedNumPlayers = iNumPlayers;

	pServer = gpServersByType[GLOBALTYPE_LOGINSERVER];

	while (pServer)
	{
		siPresumedNumPlayers += pServer->pLoginServerSpecificInfo->iNumWhoWentThroughQueueWhoAreStillOnLoginServer;
		pServer = pServer->pNext;
	}

	if (siMaxPlayersInShard && siMaxPlayersPercentFullForPreQueueAlert)
	{
		if (siPresumedNumPlayers > siMaxPlayersPercentFullForPreQueueAlert * siMaxPlayersInShard / 100)
		{
			if (siLastSoonAlertedTime < timeSecondsSince2000() - siDelayBetweenQueueSoonAlerts
				&& siLastTimeQueueing < timeSecondsSince2000() - siDelayBetweenQueueSoonAlerts)
			{
				WARNING_NETOPS_ALERT("QUEUING_SOON", 
					"Now have %d players in the shard, will start queuing when that hits %d",
					siPresumedNumPlayers, siMaxPlayersInShard);
				siLastSoonAlertedTime = timeSecondsSince2000();
			}

			if (siPresumedNumPlayers >= siMaxPlayersInShard)
			{
				siLastTimeQueueing = timeSecondsSince2000();

				if (siLastNowAlertedTime < timeSecondsSince2000() - siDelayBetweenQueueNowAlerts)
				{
					siLastNowAlertedTime = timeSecondsSince2000();
					WARNING_NETOPS_ALERT("QUEUEING_NOW",
						"We have hit the max of %d players in the shard, login queue is active",
							siMaxPlayersInShard);
				}
			}
		}
	}




	if (!sbPauseLoginQueue)
	{
		while ((!siMaxPlayersInShard || siPresumedNumPlayers < siMaxPlayersInShard) 
			&& (PointerFIFO_Count(spMainLoginQueue) || PointerFIFO_Count(spVIPLoginQueue)))
		{
			IDsOfPlayerInLoginQueue *pGuyToSend;

			if (PointerFIFO_Count(spVIPLoginQueue))
			{
				PointerFIFO_Get(spVIPLoginQueue, &pGuyToSend);

				if (!pGuyToSend)
				{
					AssertOrAlert("QUEUE_CORRUPTION", "login queue failure, got a NULL guy to send, or FIFO corruption");
					return;
				}

				iIDOfLastGuySent_Vip = pGuyToSend->iQueueID;
			}
			else
			{
				PointerFIFO_Get(spMainLoginQueue, &pGuyToSend);

				if (!pGuyToSend)
				{
					AssertOrAlert("QUEUE_CORRUPTION", "login queue failure, got a NULL guy to send, or FIFO corruption");
					return;
				}

				iIDOfLastGuySent_Main = pGuyToSend->iQueueID;
			}
	
			pServer = FindServerFromID(GLOBALTYPE_LOGINSERVER, pGuyToSend->iLoginServerID);
			if (pServer)
			{
				pServer->pLoginServerSpecificInfo->iNumWhoWentThroughQueueWhoAreStillOnLoginServer++;
			}

			RemoteCommand_PlayerIsThroughQueue(GLOBALTYPE_LOGINSERVER, pGuyToSend->iLoginServerID, pGuyToSend->iLoginServerCookie);
			siPresumedNumPlayers++;
			StructDestroy(parse_IDsOfPlayerInLoginQueue, pGuyToSend);
		}
	}

	if (objLocalManager())
	{
		pServer = gpServersByType[GLOBALTYPE_LOGINSERVER];

		while (pServer)
		{
			RemoteCommand_QueueIDsUpdate(GLOBALTYPE_LOGINSERVER, pServer->iContainerID, iIDOfLastGuySent_Main, iIDOfLastGuySent_Vip, PointerFIFO_Count(spMainLoginQueue) + PointerFIFO_Count(spVIPLoginQueue));
			pServer = pServer->pNext;
		}
	}
	
}

static __forceinline bool PlayerShouldBeQueued(bool bVIP)
{
	if (sbPutEveryoneInLoginQueueAtLeastBriefly)
	{
		return true;
	}

	if (!siMaxPlayersInShard)
	{
		return false;
	}

	if (siPresumedNumPlayers >= siMaxPlayersInShard)
	{
		return true;
	}

	if (bVIP && !PointerFIFO_Count(spVIPLoginQueue))
	{
		return false;
	}

	if (!PointerFIFO_Count(spMainLoginQueue))
	{
		return false;
	}

	return true;
}


AUTO_COMMAND_REMOTE;
void QueuePlayerIfNecessary(ContainerID iLoginServerID, U64 iLoginServerCookie, bool bVIP)
{
	if (PlayerShouldBeQueued(bVIP))
	{
		IDsOfPlayerInLoginQueue *pIDs;


		//if this is the first guy queued ever, create our FIFOs and our mempool
		if (!spVIPLoginQueue)
		{
			spVIPLoginQueue = PointerFIFO_Create(16);
			spMainLoginQueue = PointerFIFO_Create(256);

			PointerFIFO_EnableMaxTracker(spVIPLoginQueue);
			PointerFIFO_EnableMaxTracker(spMainLoginQueue);

			MP_CREATE(IDsOfPlayerInLoginQueue, 256);
		}

		pIDs = StructCreate(parse_IDsOfPlayerInLoginQueue);


		pIDs->iLoginServerID = iLoginServerID;
		pIDs->iLoginServerCookie = iLoginServerCookie;
		pIDs->iTimeAdded = timeSecondsSince2000();

		if (bVIP)
		{
			pIDs->iQueueID = siNextVIPID;
			siNextVIPID++;
			if (!siNextVIPID)
			{
				siNextVIPID++;
			}

			PointerFIFO_Push(spVIPLoginQueue, pIDs);
			RemoteCommand_HereIsQueueID(GLOBALTYPE_LOGINSERVER, iLoginServerID, iLoginServerCookie, pIDs->iQueueID, PointerFIFO_Count(spVIPLoginQueue), 
				PointerFIFO_Count(spMainLoginQueue) + PointerFIFO_Count(spVIPLoginQueue));
		}
		else
		{
			pIDs->iQueueID = siNextMainID;
			siNextMainID++;
			if (!siNextMainID)
			{
				siNextMainID++;
			}
	
			PointerFIFO_Push(spMainLoginQueue, pIDs);
			RemoteCommand_HereIsQueueID(GLOBALTYPE_LOGINSERVER, iLoginServerID, iLoginServerCookie, pIDs->iQueueID, PointerFIFO_Count(spMainLoginQueue), 
				PointerFIFO_Count(spMainLoginQueue) + PointerFIFO_Count(spVIPLoginQueue));
		}
	}
	else
	{
	
		TrackedServerState *pServer = FindServerFromID(GLOBALTYPE_LOGINSERVER, iLoginServerID);
		if (pServer)
		{
			pServer->pLoginServerSpecificInfo->iNumWhoWentThroughQueueWhoAreStillOnLoginServer++;
		}


		RemoteCommand_PlayerIsThroughQueue(GLOBALTYPE_LOGINSERVER, iLoginServerID, iLoginServerCookie);
		siPresumedNumPlayers++;	
	}
}

AUTO_COMMAND_REMOTE;
void PlayerWhoWentThroughQueueHasLeftLoginServer(ContainerID iLoginServerID)
{
	TrackedServerState *pServer = FindServerFromID(GLOBALTYPE_LOGINSERVER, iLoginServerID);
	if (pServer && pServer->pLoginServerSpecificInfo->iNumWhoWentThroughQueueWhoAreStillOnLoginServer)
	{
		pServer->pLoginServerSpecificInfo->iNumWhoWentThroughQueueWhoAreStillOnLoginServer--;
	}

}

void Controller_DoXperfDumpOnMachine(TrackedMachineState *pMachine, const char *pFileNameFmt, ...)
{
	if (isProductionMode())
	{
		TrackedServerState *pLauncher = pMachine->pServersByType[GLOBALTYPE_LAUNCHER];
		if (pLauncher)
		{
			char *pFullCmdString = NULL;
			estrCopy2(&pFullCmdString, "xperfDumpNoForce ");
			estrGetVarArgs(&pFullCmdString, pFileNameFmt);
			SendCommandDirectlyToServerThroughNetLink(pLauncher, pFullCmdString);
			estrDestroy(&pFullCmdString);
		}
	}
}

void OVERRIDE_LATELINK_SingleNoteWasUpdated(SingleNote *pSingleNote)
{
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_MASTERCONTROLPROGRAM];

	while (pServer)
	{
		if (pServer->pLink)
		{
			Packet *pOutPack = pktCreate(pServer->pLink, FROM_CONTROLLER_HERE_IS_LOCAL_NOTE);
			ParserSendStruct(parse_SingleNote, pOutPack, pSingleNote);
			pktSend(&pOutPack);
		}

		pServer = pServer->pNext;
	}
}

void SendAllSingleNotes(TrackedServerState *pServer)
{
	if (!gSingleNotesByName)
	{
		return;
	}

	FOR_EACH_IN_STASHTABLE(gSingleNotesByName, SingleNote, pSingleNote)
	{
		if (pServer->pLink)
		{
			Packet *pOutPack = pktCreate(pServer->pLink, FROM_CONTROLLER_HERE_IS_LOCAL_NOTE);
			ParserSendStruct(parse_SingleNote, pOutPack, pSingleNote);
			pktSend(&pOutPack);
		}
	}
	FOR_EACH_END;
}


static char *spActiveLogServerName = NULL;
//called when the logserver is "going", so that the controller can now inform all present and future launchers
void Controller_LogServerNowSetAndActive(char *pMachineName)
{
	TrackedServerState *pServer;

	estrCopy2(&spActiveLogServerName, pMachineName);

	pServer = gpServersByType[GLOBALTYPE_LAUNCHER];

	while (pServer)
	{
		if (pServer->pLink)
		{
			Packet *pOutPack = pktCreate(pServer->pLink, LAUNCHERQUERY_SETLOGSERVER);
			pktSendString(pOutPack, pMachineName);
			pktSend(&pOutPack);
		}

		pServer = pServer->pNext;
	}
}

//returns NULL if it is not yet set/active
char *Controller_GetMachineNameOfActiveLogServer(void)
{
	if (estrLength(&spActiveLogServerName))
	{
		return spActiveLogServerName;
	}
	else
	{
		return NULL;
	}
}


//directly copied from transactionServerUtilities.c
StashTable sVersionMismatches[GLOBALTYPE_MAX] = {0};

bool VersionMismatchAlreadyReported(char *pVersionString, GlobalType eContainerType)
{
	if (!sVersionMismatches[eContainerType])
	{
		return false;
	}

	return stashFindPointer(sVersionMismatches[eContainerType], pVersionString, NULL);
}

void VersionMismatchReported(char *pVersionString, GlobalType eContainerType)
{
	if (!sVersionMismatches[eContainerType])
	{
		sVersionMismatches[eContainerType] = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);
	}

	stashAddPointer(sVersionMismatches[eContainerType], pVersionString, NULL, true);
}


	/*	if (!stashFindPointer(sCompressedFileDataByLocalFileName, pLocalFile, &pCache))
		{
			pInBuffer = fileAlloc(pLocalFile, &iInFileSize);
			if (!pInBuffer)
			{
				AssertOrAlert("CANT_READ_FILE_TO_SEND", "Couldn't read file %s in order to send it via sentry server", pLocalFile);
				return;
			}

			pCache = calloc(sizeof(CompressedFileCache), 1);
			pCache->pFileName = strdup(pLocalFile);
			pCache->pCompressedBuffer  = zipData(pInBuffer, iInFileSize, &pCache->iCompressedSize);
			pCache->iNormalSize = iInFileSize;
			stashAddPointer(sCompressedFileDataByLocalFileName, pCache->pFileName, pCache, false);

			SAFE_FREE(pInBuffer);

		}*/


U32 Controller_GetCRCFromExeName(char *pDirName, char *pLocalName)
{
	char *pFullName = NULL;
	U32 iCRC;
	estrCopy2(&pFullName, pLocalName);
	estrTruncateAtFirstOccurrence(&pFullName, ' ');
	estrInsertf(&pFullName, 0, "%s/", pDirName);

	iCRC = cryptAdlerFile(pFullName);

	estrDestroy(&pFullName);

	return iCRC;

}

//__CATEGORY Stuff for throttled file sends (ie, frankenbuilds)
//how many throttled file sends to feed each frame
static int siNumThrottledFileSendsToProcessPerTick = 10;
AUTO_CMD_INT(siNumThrottledFileSendsToProcessPerTick, NumThrottledFileSendsToProcessPerTick) ACMD_CONTROLLER_AUTO_SETTING(ThrottledFileSends);

//how much data to send per tick for each throttled file send
static U32 siMaxBytesPerFileSendPerTick = 400 * 1024;
AUTO_CMD_INT(siMaxBytesPerFileSendPerTick, MaxBytesPerFileSendPerTick) ACMD_CONTROLLER_AUTO_SETTING(ThrottledFileSends);

//how many msecs to delay between packets to the same launcher (deprecated)
static U32 siMsecsDelayBetweenSendsToOneLauncher = 1;
AUTO_CMD_INT(siMsecsDelayBetweenSendsToOneLauncher, MsecsDelayBetweenSendsToOneLauncher) ACMD_CONTROLLER_AUTO_SETTING(ThrottledFileSends);

typedef struct ThrottledFileSend
{
	U32 iID;

	bool bWaitingForHandshake;

	TrackedMachineState *pMachine;
	CompressedFileCache *pCache;
	S64 iAmountAlreadySent;
	S64 iLastTimeSent;
	ContainerID iLauncherID;

	CompressedFileCache *pNextFileToSend;
} ThrottledFileSend;

ThrottledFileSend **ppThrottledFileSends = NULL;

static StashTable sThrottledFileSendsByID = NULL;
static U32 siNextThrottledFileSendID = 1;

ThrottledFileSend *ThrottledFileSend_Create(void)
{
	ThrottledFileSend *pSend = calloc(sizeof(ThrottledFileSend), 1);
	pSend->iID = siNextThrottledFileSendID++;
	if (!siNextThrottledFileSendID)
	{
		siNextThrottledFileSendID++;
	}

	if (!sThrottledFileSendsByID)
	{
		sThrottledFileSendsByID = stashTableCreateInt(16);
	}

	stashIntAddPointer(sThrottledFileSendsByID, pSend->iID, pSend, true);

	return pSend;
}

void ThrottledFileSend_Destroy(ThrottledFileSend *pSend)
{
	stashIntRemovePointer(sThrottledFileSendsByID, pSend->iID, NULL);
	free(pSend);
}

ThrottledFileSend *ThrottledFileSend_FindFromID(U32 iID)
{
	ThrottledFileSend *pSend;

	if (stashIntFindPointer(sThrottledFileSendsByID, iID, &pSend))
	{
		return pSend;
	}

	return NULL;
}


void BeginThrottledFileSendInternal(TrackedMachineState *pMachine, CompressedFileCache *pFirstFile, CompressedFileCache *pSecondFile)
{
	ThrottledFileSend *pThrottledFileSend;

	if (!pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
	{
		return;
	}

	pThrottledFileSend = ThrottledFileSend_Create();
	pThrottledFileSend->pMachine = pMachine;
	pThrottledFileSend->pCache = pFirstFile;
	pThrottledFileSend->pNextFileToSend = pSecondFile;
	pThrottledFileSend->iLauncherID = pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID;

	eaPush(&ppThrottledFileSends, pThrottledFileSend);
}



void UdpateThrottledFileSends(void)
{
	U32 iBytesToSendThisFrame;
	ThrottledFileSend *pSend;
	Packet *pOutPack;
	ThrottledFileSend **ppProcessedThisFrame = NULL;
	int i;
	S64 iCurTime = timeGetTime();

	PERFINFO_AUTO_START_FUNC();


	for (i = 0; i < siNumThrottledFileSendsToProcessPerTick; i++)
	{
		if (!eaSize(&ppThrottledFileSends))
		{
			break;
		}


		pSend = eaRemove(&ppThrottledFileSends, 0);

		if (pSend->bWaitingForHandshake)
		{
			eaPush(&ppProcessedThisFrame, pSend);
			continue;
		}

		pSend->iLastTimeSent = iCurTime;

		if (!pSend->pMachine->pServersByType[GLOBALTYPE_LAUNCHER] || 
			pSend->pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID != pSend->iLauncherID)
		{
			ThrottledFileSend_Destroy(pSend);
			continue;
		}

		iBytesToSendThisFrame = pSend->pCache->iCompressedSize - pSend->iAmountAlreadySent;
		if (iBytesToSendThisFrame > siMaxBytesPerFileSendPerTick)
		{
			iBytesToSendThisFrame = siMaxBytesPerFileSendPerTick;
		}

		pOutPack = pktCreate(pSend->pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_FILEREQUESTFULFILLED);
		pktSendBits(pOutPack, 32, pSend->iID);
		pktSendString(pOutPack, pSend->pCache->pFileName);
		pktSendBits(pOutPack, 32, pSend->pCache->iCRC);
		pktSendBits(pOutPack, 32, pSend->pCache->iNormalSize);
		pktSendBits(pOutPack, 32, pSend->pCache->iCompressedSize);
		pktSendBits(pOutPack, 32, pSend->iAmountAlreadySent);
		pktSendBits(pOutPack, 32, iBytesToSendThisFrame);
		pktSendBytes(pOutPack, iBytesToSendThisFrame, pSend->pCache->pCompressedBuffer + pSend->iAmountAlreadySent);
		pktSend(&pOutPack);		

		pSend->iAmountAlreadySent += iBytesToSendThisFrame;
		if (pSend->iAmountAlreadySent == pSend->pCache->iCompressedSize)
		{
			if (pSend->pNextFileToSend)
			{
				BeginThrottledFileSendInternal(pSend->pMachine, pSend->pNextFileToSend, NULL);
			}

			ThrottledFileSend_Destroy(pSend);
		}
		else
		{
			pSend->bWaitingForHandshake = true;
			eaPush(&ppProcessedThisFrame, pSend);
		}
	}

	eaPushEArray(&ppThrottledFileSends, &ppProcessedThisFrame);
	eaDestroy(&ppProcessedThisFrame);

	PERFINFO_AUTO_STOP();
}
void HandleThrottledFileSendingHandshake(Packet *pak)
{
	U32 iID = pktGetBits(pak, 32);
	ThrottledFileSend *pSend = ThrottledFileSend_FindFromID(iID);
	if (pSend)
	{	
		pSend->bWaitingForHandshake = false;
	}
}

void AddThrottledExeSend(TrackedMachineState *pMachine, CompressedFileCache *pCache)
{
//	Packet *pOutPack;

	//whenever we are about to send a .exe, send the .pdb first
	if (strEndsWith(pCache->pFileName, ".exe"))
	{
		if (!pCache->pPDBCache)
		{
			char *pPDBFileName = NULL;
		
			estrCopy2(&pPDBFileName, pCache->pFileName);
			estrReplaceOccurrences(&pPDBFileName, ".exe", ".pdb");
			pCache->pPDBCache = Controller_GetCompressedFileCache(pPDBFileName);
			estrDestroy(&pPDBFileName);
		}

		if (pCache->pPDBCache)
		{
			BeginThrottledFileSendInternal(pMachine, pCache->pPDBCache, pCache);
			return;
/*			pOutPack = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_FILEREQUESTFULFILLED);
			pktSendString(pOutPack, pCache->pPDBCache->pFileName);
			pktSendBits(pOutPack, 32, pCache->pPDBCache->iCRC);
			pktSendBits(pOutPack, 32, pCache->pPDBCache->iNormalSize);
			pktSendBits(pOutPack, 32, pCache->pPDBCache->iCompressedSize);
			pktSendBytes(pOutPack, pCache->pPDBCache->iCompressedSize, pCache->pPDBCache->pCompressedBuffer);
			pktSend(&pOutPack);*/
		}
	}


/*	pOutPack = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_FILEREQUESTFULFILLED);
	pktSendString(pOutPack, pCache->pFileName);
	pktSendBits(pOutPack, 32, pCache->iCRC);
	pktSendBits(pOutPack, 32, pCache->iNormalSize);
	pktSendBits(pOutPack, 32, pCache->iCompressedSize);
	pktSendBytes(pOutPack, pCache->iCompressedSize, pCache->pCompressedBuffer);
	pktSend(&pOutPack);*/
	BeginThrottledFileSendInternal(pMachine, pCache, NULL);
}

void HandleLauncherRequestingExeFromNameAndCRC(Packet *pPak, NetLink *pLink)
{
	char *pFileName = pktGetStringTemp(pPak);
	U32 iCRC = pktGetBits(pPak, 32);
	CompressedFileCache *pCache = Controller_GetCompressedFileCache(pFileName);
	TrackedMachineState *pMachine = GetMachineFromNetLink(pLink);

	if (!pCache)
	{
		AssertOrAlert("UNKNOWN_EXE_REQUESTED", "Launcher wants to launch %s, this file doesn't seem to exist or can not be loaded",
			pFileName);
		return;
	}

	if ((U32)pCache->iCRC != iCRC)
	{
		AssertOrAlert("EXE_CRC_MISMATCH", "Launcher wants to launch %s with the wrong CRC... launches will never happen",
			pFileName);
		return;
	}

	AddThrottledExeSend(pMachine, pCache);
}

AUTO_STRUCT;
typedef struct CompressedFileCachePendingRequest
{
	GetCompressedFileCacheCB pCB; NO_AST
	void *pUserData; NO_AST
	int iDummy;
} CompressedFileCachePendingRequest;

AUTO_STRUCT;
typedef struct CompressedFileCacheRequests
{
	char *pFileName;
	CompressedFileCachePendingRequest **ppPendingRequests;
} CompressedFileCacheRequests;


StashTable sCompressedFileCachesByName = NULL;
StashTable sCompressedFileCacheRequestsByName = NULL;

static CompressedFileCache *spBlockingRetVal;

void Controller_GetCompressedFileCache_BlockingCB(CompressedFileCache *pCache, void *pUserData)
{
	spBlockingRetVal = pCache;
}

CompressedFileCache *Controller_GetCompressedFileCache(char *pFileName)
{
	CompressedFileCache *pRetVal;
	if (stashFindPointer(sCompressedFileCachesByName, pFileName, &pRetVal))
	{
		return pRetVal;
	}

	spBlockingRetVal = NULL;

	Controller_GetCompressedFileCache_Threaded(pFileName, Controller_GetCompressedFileCache_BlockingCB, NULL);

	while (!spBlockingRetVal)
	{
		zipTick();
		Sleep(1);
	}

	return spBlockingRetVal;
}

static void CompressedFileCacheZipCB(void *pData, int iDataSize, int iUncompressedDataSize, U32 iCRC,  CompressedFileCacheRequests *pRequests)
{
	CompressedFileCache *pCache = calloc(sizeof(CompressedFileCache), 1);
	pCache->pFileName = pRequests->pFileName;
	pCache->iCompressedSize = iDataSize;
	pCache->iCRC = iCRC;
	pCache->iNormalSize = iUncompressedDataSize;
	pCache->pCompressedBuffer = pData;

	if (!sCompressedFileCachesByName)
	{
		sCompressedFileCachesByName = stashTableCreateWithStringKeys(16, StashDefault);
	}

	stashAddPointer(sCompressedFileCachesByName, pCache->pFileName, pCache, true);

	FOR_EACH_IN_EARRAY_FORWARDS(pRequests->ppPendingRequests, CompressedFileCachePendingRequest, pPendingRequest)
	{
		pPendingRequest->pCB(pCache, pPendingRequest->pUserData);
	}
	FOR_EACH_END;

	stashRemovePointer(sCompressedFileCacheRequestsByName, pRequests->pFileName, NULL);
	pRequests->pFileName = NULL; //copied it into pCache, so don't want to free it
	StructDestroy(parse_CompressedFileCacheRequests, pRequests);



}

void Controller_GetCompressedFileCache_Threaded(char *pFileName, GetCompressedFileCacheCB pCB, void *pUserData)
{
	CompressedFileCache *pRetVal;
	CompressedFileCacheRequests *pRequests;
	CompressedFileCachePendingRequest *pNewRequest;

	if (stashFindPointer(sCompressedFileCachesByName, pFileName, &pRetVal))
	{
		pCB(pRetVal, pUserData);
		return;
	}

	if (!sCompressedFileCacheRequestsByName)
	{
		sCompressedFileCacheRequestsByName = stashTableCreateWithStringKeys(16, StashDefault);
	}

	if (!stashFindPointer(sCompressedFileCacheRequestsByName, pFileName, &pRequests))
	{
		pRequests = StructCreate(parse_CompressedFileCacheRequests);
		pRequests->pFileName = strdup(pFileName);
		stashAddPointer(sCompressedFileCacheRequestsByName, pRequests->pFileName, pRequests, false);

		ThreadedLoadAndZipFile(pRequests->pFileName, CompressedFileCacheZipCB, pRequests);
	}
	
	pNewRequest = StructCreate(parse_CompressedFileCachePendingRequest);
	pNewRequest->pCB = pCB;
	pNewRequest->pUserData = pUserData;
	eaPush(&pRequests->ppPendingRequests, pNewRequest);
}


StashTable sFrankenBuildReportsByID = NULL;

//created upon completion of a frankenbuild... solely for servermonitoring
AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Time, WhatHappened");
typedef struct FrankenBuildReport
{
	U32 iID; AST(KEY)
	U32 iTime; AST(FORMATSTRING(HTML_SECS=1))
	char *pWhatHappened; AST(ESTRING)
} FrankenBuildReport;

void AddFrankenBuildReport(FORMAT_STR const char *pFmt, ...)
{
	static U32 iNextID = 1;
	FrankenBuildReport *pReport = StructCreate(parse_FrankenBuildReport);
	if (!sFrankenBuildReportsByID)
	{
		sFrankenBuildReportsByID = stashTableCreateInt(16);
		resRegisterDictionaryForStashTable("FrankenBuilds", RESCATEGORY_SYSTEM, 0, sFrankenBuildReportsByID, parse_FrankenBuildReport);
	}

	pReport->iID = iNextID++;
	pReport->iTime = timeSecondsSince2000();
	estrGetVarArgs(&pReport->pWhatHappened, pFmt);

	stashIntAddPointer(sFrankenBuildReportsByID, pReport->iID, pReport, true);


}

bool Controller_AreThereFrankenBuilds(void)
{
	return !!sFrankenBuildReportsByID;
}

void Controller_MessageBoxError(const char *pTitle, FORMAT_STR const char* format, ...)
{
	char *pTempString1 = NULL;
	char *pTempString2 = NULL;
	estrGetVarArgs(&pTempString1, format);


	if (Controller_GetClusterControllerName())
	{
		char *pSuperEscapedMessage = NULL;
		estrSuperEscapeString(&pSuperEscapedMessage, pTempString1);

		estrPrintf(&pTempString2, "sentryHelper -launch %s \"MessageBox -title %s:%s -superesc message %s\"",
			Controller_GetClusterControllerName(), GetShardNameFromShardInfoString(), pTitle, pSuperEscapedMessage);
		system_detach(pTempString2, true, true);
		estrDestroy(&pSuperEscapedMessage);
	}
	else
	{
		estrReplaceOccurrences(&pTempString1, "\"", "\\q");

		estrPrintf(&pTempString2, "messagebox -title %s -message \"%s\" -icon error",
			pTitle, pTempString1);
		system_detach(pTempString2, true, true);
	}

	

	estrDestroy(&pTempString1);
	estrDestroy(&pTempString2);
}

bool OVERRIDE_LATELINK_TripleControlCOverride(void)
{
	printfColor(COLOR_BLUE|COLOR_BRIGHT | COLOR_GREEN, "To shut down this shard, execute command ShutdownShard shardname\nthrough the servermonitor, or the debug console");
	return true;
}

AUTO_COMMAND;
char *ShutDownShard(char *pShardName, CmdContext *pContext)
{
	if (stricmp(pShardName, GetShardNameFromShardInfoString()) != 0)
	{
		printfColor(COLOR_RED|COLOR_GREEN|COLOR_BRIGHT, "ShutDownShard requires the shard name (%s) to be entered",
			GetShardNameFromShardInfoString());
		return "ShutDownShard requires the shard name to be entered";
	}

	log_printf(LOG_SHARD, "ShutdownShard called via %s", GetContextHowString(pContext));
	exit(0);
	return "No one will ever see this string...";
}

AUTO_STRUCT;
typedef struct StartupStatusCategory
{
	char *pName; AST(KEY)
	char *pStatus; AST(ESTRING)
	U32 iTime;
} StartupStatusCategory;

static StartupStatusCategory **sppStartupStatusCategories = NULL;

void Controller_SetStartupStatusString(char *pCategoryName, FORMAT_STR const char* format, ...)
{
	StartupStatusCategory *pCategory;

	ONCE(eaIndexedEnable(&sppStartupStatusCategories, parse_StartupStatusCategory));

	if (!format)
	{
		pCategory = eaIndexedRemoveUsingString(&sppStartupStatusCategories, pCategoryName);
		StructDestroySafe(parse_StartupStatusCategory, &pCategory);
		return;
	}

	pCategory = eaIndexedGetUsingString(&sppStartupStatusCategories, pCategoryName);
	if (!pCategory)
	{
		pCategory = StructCreate(parse_StartupStatusCategory);
		pCategory->pName = strdup(pCategoryName);
		eaPush(&sppStartupStatusCategories, pCategory);
	}

	estrClear(&pCategory->pStatus);
	estrGetVarArgs(&pCategory->pStatus, format);
	pCategory->iTime = timeSecondsSince2000();
}

char *Controller_GetStartupStatusString(void)
{
	static char *spRetVal = NULL;
	char *pDuration = NULL;

	if (!eaSize(&sppStartupStatusCategories))
	{
		estrDestroy(&spRetVal);
	}
	else
	{
		int i;
		estrClear(&spRetVal);
		for (i = 0; i < eaSize(&sppStartupStatusCategories); i++)
		{
			timeSecondsDurationToShortEString(timeSecondsSince2000() - sppStartupStatusCategories[i]->iTime, &pDuration);

			estrConcatf(&spRetVal, "%s%s:%s(%s)", i == 0 ? "" : "\n", sppStartupStatusCategories[i]->pName, sppStartupStatusCategories[i]->pStatus, pDuration);
		}
	}
	
	estrDestroy(&pDuration);

	return spRetVal;
}

void GetCRCCB(const char *pMachineName, char *pFileName, void *pUserData, int iCRC, bool bTimedOut)
{
	printf("CRC of %s on %s: %d%s\n", pFileName, pMachineName, iCRC, bTimedOut ? "(TIMED OUT)" : "");
}

AUTO_COMMAND;
void GetCRCThroughSentryServer(char *pMachineName, char *pFileName)
{
	SentryServerComm_GetFileCRC(pMachineName, pFileName, GetCRCCB, NULL);

}


void GetDirContentsCB(const char *pMachineName, char *pDirName, void *pUserData, char *pDirContents, bool bFailed)
{
	printf("DirContents of %s on %s: %s%s\n", pDirName, pMachineName, pDirContents, bFailed ? "(FAILED)" : "");
}

AUTO_COMMAND;
void GetDirContentsThroughSentryServer(char *pMachineName, char *pDirName)
{
	SentryServerComm_GetDirectoryContents(pMachineName, pDirName, GetDirContentsCB, NULL);
}

static int siLogServerStressTestMode_NumLogsPerSecondPerGameServer = 0;
static bool sbLogServerStressTestMode_Active = false;

bool Controller_DoingLogServerStressTest(void)
{
	return sbLogServerStressTestMode_Active;
}

int Controller_GetLogServerStressTestLogsPerServerPerSecond(void)
{
	return siLogServerStressTestMode_NumLogsPerSecondPerGameServer;
}

AUTO_COMMAND;
char *BeginLogServerStressTest(char *pConfirm, int iNumGameServers, int iLogsPerSecondPerGameServer)
{
	int i;

	if (sbLogServerStressTestMode_Active)
	{
		return "LogServer Stress Test Mode already active";
	}

	if (stricmp(pConfirm, "yes") != 0)
	{
		return "You didn't type yes";
	}

	sbLogServerStressTestMode_Active = true;
	siLogServerStressTestMode_NumLogsPerSecondPerGameServer = iLogsPerSecondPerGameServer;

	for (i = 0; i < iNumGameServers; i++)
	{
		RemoteCommand_CallLocalCommandRemotely(GLOBALTYPE_MAPMANAGER, 0, "NewMapTransfer_LaunchNewServerCmd EmptyMap 1 -");
	}
	
	return "started";
}

AUTO_COMMAND;
void SetLogServerStressTestLogsPerServerPerSecond(int iNumPerSecond)
{
	TrackedServerState *pServer;

	siLogServerStressTestMode_NumLogsPerSecondPerGameServer = iNumPerSecond;

	pServer = gpServersByType[GLOBALTYPE_GAMESERVER];

	while (pServer)
	{
		if (strstri(pServer->stateString, "gslrunning"))
		{
			RemoteCommand_BeginLogServerStressTestMode(GLOBALTYPE_GAMESERVER, pServer->iContainerID, iNumPerSecond);
		}

		pServer = pServer->pNext;
	}
}

int Controller_GetNumPlayersInMainLoginQueue(void)
{
	return PointerFIFO_Count(spMainLoginQueue);
}
int Controller_GetNumPlayersInVIPLoginQueue(void)
{
	return PointerFIFO_Count(spVIPLoginQueue);
}

#include "Controller_utils_c_ast.c"
