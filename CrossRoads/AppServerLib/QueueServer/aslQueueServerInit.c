#include "aslQueue.h"
#include "AutoStartupSupport.h"
#include "AppServerLib.h"
#include "objSchema.h"
#include "ServerLib.h"
#include "objTransactions.h"
#include "queue_common.h"
#include "StringCache.h"
#include "ChoiceTable_common.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"

#include "queue_common.h"
#include "AutoGen/queue_common_h_ast.h"

static S32 s_iContainersReceived = 0;
static S32 s_iContainersRequested = -1;
static S32 s_bReceiveContainersError = false;
static GlobalType s_eConType = GLOBALTYPE_QUEUEINFO;

void aslQueue_ContainerMove_CB(TransactionReturnVal *pReturn, void *pData)
{
	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		GlobalType *type = (GlobalType*)pData;
		s_bReceiveContainersError = true;
		loadupdate_printf("Failed to fetch %s container: %s.\n", GlobalTypeToName(*type),pReturn->pBaseReturnVals[0].returnString);
	}
	else if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		s_iContainersReceived++;
	}
}

void aslQueue_AcquireQueues_CB(TransactionReturnVal *pReturn, void *pData)
{
	ContainerList *pList;
	if (RemoteCommandCheck_dbAcquireContainers(pReturn, &pList) == TRANSACTION_OUTCOME_SUCCESS)
	{
		s_iContainersRequested = 0;
		while (eaiSize(&pList->eaiContainers) > 0) 
		{
			U32 conid = eaiPop(&pList->eaiContainers);
			objRequestContainerMove(objCreateManagedReturnVal(aslQueue_ContainerMove_CB, pData), pList->type, conid, GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID());
			s_iContainersRequested++;
		}
	}
}

int QueueServerStartupOncePerFrame(F32 fElapsed)
{
	static bool bOnce = false;

	if(!bOnce)
	{
		loadstart_printf("Requesting queue containers from the ObjectDB\n");
		RemoteCommand_dbAcquireContainers(objCreateManagedReturnVal(aslQueue_AcquireQueues_CB, &s_eConType), GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID(), GLOBALTYPE_QUEUEINFO);

		bOnce = true;
	}

	if(s_bReceiveContainersError)
	{
		loadend_printf("Error!\n");
		//Error and shutdown
	}

	if(s_iContainersRequested >= 0 && s_iContainersRequested == s_iContainersReceived)
	{
		loadend_printf("Received [%d] queue containers from the ObjectDB  ", s_iContainersReceived);
		gAppServer->oncePerFrame = QueueServerOncePerFrame;
	}

	return 1;
}

AUTO_STARTUP(QueueServer) ASTRT_DEPS(QueueCategories, CharacterClasses);
void QueueServerLoad(void)
{
	choice_Load();
	Queues_Load(NULL, aslQueue_ReloadQueues);
	Queues_LoadConfig();
	aslQueue_LoadRandomQueueLists();	// aslQueueServer.c
}

int QueueServerLibInit(void)
{
	objLoadAllGenericSchemas();
	
	AutoStartup_SetTaskIsOn("QueueServer", 1);

	stringCacheFinalizeShared();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_QUEUESERVER, "QueueServer type not set");
	
	loadstart_printf("Connecting QueueServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
	
	while (!InitObjectTransactionManager(GetAppGlobalType(),
			gServerLibState.containerID,
			gServerLibState.transactionServerHost,
			gServerLibState.transactionServerPort,
			gServerLibState.bUseMultiplexerForTransactions, NULL)) {
		Sleep(1000);
	}
	if (!objLocalManager())
	{
		loadend_printf("Failed.");
		return 0;
	}
	
	loadend_printf("Connected.");

	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");
	
	gAppServer->oncePerFrame = QueueServerStartupOncePerFrame;
	
	return 1;
}

