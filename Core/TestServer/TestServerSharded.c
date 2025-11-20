#include "TestServerSharded.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "objSchema.h"
#include "objTransactions.h"
#include "ServerLib.h"
#include "StashTable.h"
#include "TestClientCommon.h"
#include "TestServerExternal.h"
#include "TestServerGlobal.h"
#include "TestServerHttp.h"
#include "TestServerLua.h"
#include "TestServerMetric.h"
#include "timing.h"
#include "windefinclude.h"

#include "TestClientCommon_h_ast.h"

#include "AutoGen/Controller_autogen_RemoteFuncs.h"

static bool gbSharded = false;

typedef struct TestClientID TestClientID;
typedef struct TestClientID
{
	int id;
	int count;
	TestClientID *next;
} TestClientID;

typedef struct TestClientInfo
{
	ContainerID		iID;
	ContainerID		iEntityID;

	NetLink			*pNetLink;
	int				iMultiplexIndex;
	char			*pcCustomStatus;

	bool			bConnected;
	bool			bCompleted;
	bool			bSucceeded;
	bool			bDiscarded;
	char			*pcResult;
} TestClientInfo;

static TestClientID *gpLinkedListOfIDs = NULL;
static StashTable gAllTestClients;
static StashTable gTestClientEntities;
static TestClientSpawnRequest **gppClientsToSpawn = NULL;
static TestClientInfo **gppClientsToKill = NULL;
static CRITICAL_SECTION cs_TestClientAccess;

void TestServer_Client_Spawn_internal(TestClientSpawnRequest *pRequest);
void TestServer_Client_Kill_internal(TestClientInfo *pClient);
void TestServer_Client_ReleaseID(int id);

bool TestServer_IsSharded(void)
{
	return gbSharded;
}

void TestServer_InitSharded(void)
{
	gbSharded = true;

	loadstart_printf("Connecting to Transaction Server...");
	while(!InitObjectTransactionManager(GetAppGlobalType(), GetAppGlobalID(), gServerLibState.transactionServerHost, gServerLibState.transactionServerPort, false, NULL))
	{
		Sleep(1000);
	}

	if(!objLocalManager())
	{
		loadend_printf("...failed.");
		assertmsg(0, "Something went horribly wrong connecting to Transaction Server!");
	}
	loadend_printf("...done.");

	InitializeCriticalSection(&cs_TestClientAccess);
	gAllTestClients = stashTableCreateInt(0);
	gTestClientEntities = stashTableCreateInt(0);

	gpLinkedListOfIDs = calloc(1, sizeof(TestClientID));
	gpLinkedListOfIDs->id = 1;
	gpLinkedListOfIDs->count = 100000;
	gpLinkedListOfIDs->next = NULL;
}

void TestServer_ShardedReady(void)
{
	if(!gbSharded)
	{
		return;
	}

	RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
	TestServer_ConsolePrintf("Ready - sharded mode.\n");
}

void TestServer_ShardedTick(F32 fTotalElapsed, F32 fElapsed)
{
	static bool bOnce = false;
	static F32 fKillTicker = 0.0f;

	fKillTicker += fElapsed;

	EnterCriticalSection(&cs_TestClientAccess);
	FOR_EACH_IN_EARRAY(gppClientsToSpawn, TestClientSpawnRequest, pRequest)
	{
		pRequest->fCurDelay += fElapsed;

		if(pRequest->fCurDelay >= pRequest->fSpawnDelay)
		{
			TestServer_Client_Spawn_internal(pRequest);

			if(!pRequest->iNumToSpawn)
			{
				eaRemove(&gppClientsToSpawn, ipRequestIndex);
				estrDestroy(&pRequest->pcCmdLine);
				free(pRequest);
			}
			else
			{
				pRequest->fCurDelay = 0.0f;
			}
		}
	}
	FOR_EACH_END

	if(eaSize(&gppClientsToKill) && fKillTicker >= 0.1f)
	{
		fKillTicker = 0.0f;
		TestServer_Client_Kill_internal(gppClientsToKill[0]);
		eaRemoveFast(&gppClientsToKill, 0);
	}
	LeaveCriticalSection(&cs_TestClientAccess);
}

static TestClientInfo *TestServer_Client_GetInfoByID(ContainerID id)
{
	TestClientInfo *pClient = NULL;
	stashIntFindPointer(gAllTestClients, id, &pClient);
	return pClient;
}

static TestClientInfo *TestServer_Client_GetInfoByEntityID(ContainerID id)
{
	TestClientInfo *pClient = NULL;
	stashIntFindPointer(gTestClientEntities, id, &pClient);
	return pClient;
}

// BEGIN HORRIBLY BLATANT COPY/PASTE OF CODE OUT OF THE TEST CLIENT SERVER
int TestServer_Client_GetFirstAvailableID()
{
	int returnVal;

	if(!gpLinkedListOfIDs)
	{
		return 0;
	}

	returnVal = gpLinkedListOfIDs->id;
	++gpLinkedListOfIDs->id;
	--gpLinkedListOfIDs->count;

	if(!gpLinkedListOfIDs->count)
	{
		TestClientID *rem = gpLinkedListOfIDs;
		gpLinkedListOfIDs = gpLinkedListOfIDs->next;
		free(rem);
	}

	log_printf(LOG_TC_TESTCLIENT, "GetFirstAvailableID returned %d", returnVal);

	return returnVal;
}

void TestServer_Client_NoteAlreadyUsedID(int id)
{
	TestClientID *cur = gpLinkedListOfIDs;
	TestClientID *last = NULL;

	while(cur)
	{
		if((cur->id <= id) && (cur->id + cur->count > id))
		{
			if(cur->count == 1)
			{
				if(last)
				{
					last->next = cur->next;
					free(cur);
				}
				else
				{
					gpLinkedListOfIDs = gpLinkedListOfIDs->next;
					free(cur);
				}
			}
			else if(cur->id == id)
			{
				++cur->id;
				--cur->count;
			}
			else if(cur->id + cur->count - 1 == id)
			{
				--cur->count;
			}
			else
			{
				TestClientID *add = calloc(1, sizeof(TestClientID));

				add->id = id + 1;
				add->count = cur->id + cur->count - add->id;
				add->next = cur->next;

				cur->count = id - cur->id;
				cur->next = add;
			}

			log_printf(LOG_TC_TESTCLIENT, "NoteAlreadyUsedID recorded new ID %d", id);

			break;
		}
		else
		{
			last = cur;
			cur = cur->next;
		}
	}

	if(!cur)
	{
		log_printf(LOG_TC_TESTCLIENT, "ALERT!!! NoteAlreadyUsedID recorded reused ID %d", id);
	}
}

void TestServer_Client_ReleaseID(int id)
{
	log_printf(LOG_TC_TESTCLIENT, "ReleaseID releasing ID %d", id);

	if(!gpLinkedListOfIDs)
	{
		gpLinkedListOfIDs = calloc(1, sizeof(TestClientID));
		gpLinkedListOfIDs->id = id;
		gpLinkedListOfIDs->count = 1;
		gpLinkedListOfIDs->next = NULL;
	}
	else if(gpLinkedListOfIDs->id > id)
	{
		if((gpLinkedListOfIDs->id - id) == 1)
		{
			--gpLinkedListOfIDs->id;
			++gpLinkedListOfIDs->count;
		}
		else
		{
			TestClientID *add = calloc(1, sizeof(TestClientID));

			add->id = id;
			add->count = 1;
			add->next = gpLinkedListOfIDs;
			gpLinkedListOfIDs = add;
		}
	}
	else
	{
		TestClientID *cur = gpLinkedListOfIDs;

		while(cur->next->id < id)
		{
			cur = cur->next;
			assertmsg(cur->next, "Apparently trying to release an ID that is considered to be released already.");
		}

		if((cur->next->id - id) == 1)
		{
			if((cur->id + cur->count) == id)
			{
				TestClientID *rem = cur->next;

				++cur->count;
				cur->count += cur->next->count;
				cur->next = rem->next;
				free(rem);
			}
			else
			{
				--cur->next->id;
				++cur->next->count;
			}
		}
		else
		{
			if((cur->id + cur->count) == id)
			{
				++cur->count;
			}
			else
			{
				TestClientID *add = calloc(1, sizeof(TestClientID));

				add->id = id;
				add->count = 1;
				add->next = cur->next;
				cur->next = add;
			}
		}
	}
}

void TestServer_Client_Spawn_internal(TestClientSpawnRequest *pRequest)
{
	char *pCmdLine = NULL;
	char *pSpawnPos = NULL;
	char *pEscapedSpawnPos = NULL;
	TestClientInfo *pClient = calloc(1, sizeof(TestClientInfo));
	int id;

	--pRequest->iNumToSpawn;
	id = TestServer_Client_GetFirstAvailableID();

	estrPrintf(&pCmdLine, " -testclientid %d -scriptname %s -ownerid %d %s", id, pRequest->cScriptName, pRequest->iOwnerID, pRequest->pcCmdLine ? pRequest->pcCmdLine : "");
	RemoteCommand_StartServer_NoReturn(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_TESTCLIENT, 0, pCmdLine, "TestServer_Client_Spawn_internal", NULL);
	estrDestroy(&pCmdLine);
	estrDestroy(&pSpawnPos);
	estrDestroy(&pEscapedSpawnPos);

	pClient->iID = id;
	EnterCriticalSection(&cs_TestClientAccess);
	stashIntAddPointer(gAllTestClients, id, pClient, false);
	LeaveCriticalSection(&cs_TestClientAccess);
}

void TestServer_Client_Kill_internal(TestClientInfo *pClient)
{
	Packet *pkt;
	
	if(pClient->bConnected)
	{
		pkt = TestServer_GetDestinationPacket(pClient->pNetLink, pClient->iMultiplexIndex, FROM_TESTSERVER_TESTCLIENT_DIE);
		pktSend(&pkt);
	}
}

void TestServer_Client_Succeed(ContainerID id, const char *pcMessage)
{
	TestClientInfo *pClient;
	
	EnterCriticalSection(&cs_TestClientAccess);
	pClient = TestServer_Client_GetInfoByID(id);

	if(!pClient)
	{
		LeaveCriticalSection(&cs_TestClientAccess);
		return;
	}

	// Do some succeedy stuff
	pClient->bSucceeded = true;
	pClient->bCompleted = true;
	estrCopy2(&pClient->pcResult, pcMessage);
	log_printf(LOG_TC_TESTCLIENT, "Client %d: Success - %s", pClient->iID, pcMessage);
	LeaveCriticalSection(&cs_TestClientAccess);
}

void TestServer_Client_Fail(ContainerID id, const char *pcMessage)
{
	TestClientInfo *pClient;
	
	EnterCriticalSection(&cs_TestClientAccess);
	pClient = TestServer_Client_GetInfoByID(id);

	if(!pClient)
	{
		LeaveCriticalSection(&cs_TestClientAccess);
		return;
	}

	// Do faily stuff
	pClient->bCompleted = true;
	estrCopy2(&pClient->pcResult, pcMessage);
	log_printf(LOG_TC_TESTCLIENT, "Client %d: Failure - %s", pClient->iID, pcMessage);
	LeaveCriticalSection(&cs_TestClientAccess);
}

static void TestServer_Client_NotifyStatus_internal(TestClientInfo *pClient, const char *pcStatus)
{
	estrCopy2(&pClient->pcCustomStatus, pcStatus);
}

void TestServer_Client_NotifyStatus(ContainerID id, const char *pcStatus)
{
	TestClientInfo *pClient;
	
	EnterCriticalSection(&cs_TestClientAccess);
	pClient = TestServer_Client_GetInfoByID(id);

	if(pClient)
	{
		TestServer_Client_NotifyStatus_internal(pClient, pcStatus);
	}
	LeaveCriticalSection(&cs_TestClientAccess);
}

static int TestServer_Client_NotifyExists(NetLink *link, int index, ContainerID id)
{
	TestClientInfo *pClient;
	
	EnterCriticalSection(&cs_TestClientAccess);
	pClient = TestServer_Client_GetInfoByID(id);

	if(id == 0)
	{
		// No ID yet, we must assign one and send it back
		id = TestServer_Client_GetFirstAvailableID();
	}
	else if(!pClient)
	{
		TestServer_Client_NoteAlreadyUsedID(id);
	}

	if(!pClient)
	{
		pClient = calloc(1, sizeof(TestClientInfo));
		pClient->iID = id;
		stashIntAddPointer(gAllTestClients, id, pClient, true);
	}

	TestServer_SetLinkUserData(link, index, pClient);

	pClient->pNetLink = link;
	pClient->iMultiplexIndex = index;
	pClient->bConnected = true;
	LeaveCriticalSection(&cs_TestClientAccess);

	return id;
}

void TestServer_Client_DisconnectCB(NetLink *link, int index, TestClientInfo *pClient)
{
	estrDestroy(&pClient->pcCustomStatus);
	pClient->bConnected = false;

	if(pClient->iEntityID)
	{
		stashIntRemovePointer(gTestClientEntities, pClient->iEntityID, NULL);
	}

	if(!pClient->bCompleted || !pClient->pcResult || pClient->bDiscarded)
	{
		TestServer_Client_ReleaseID(pClient->iID);
		stashIntRemovePointer(gAllTestClients, pClient->iID, NULL);
		estrDestroy(&pClient->pcResult);
		free(pClient);
	}
}

void TestServer_Client_MessageCB(Packet *pkt, int cmd, NetLink *link, int index, TestClientInfo *pClient)
{
	switch(cmd)
	{
	case TO_TESTSERVER_TESTCLIENT_ID:
		{
			Packet *pPacket;
			U32 id = pktGetU32(pkt);

			id = TestServer_Client_NotifyExists(link, index, id);
			pPacket = TestServer_GetDestinationPacket(link, index, FROM_TESTSERVER_TESTCLIENT_ID);
			pktSendU32(pPacket, id);
			pktSend(&pPacket);
		}
	xcase TO_TESTSERVER_TESTCLIENT_STATUS:
		{
			const char *pcStatus = pktGetStringTemp(pkt);
			TestServer_Client_NotifyStatus_internal(pClient, pcStatus);
		}
	xcase TO_TESTSERVER_TESTCLIENT_ENTITY_ID:
		{
			U32 id = pktGetU32(pkt);

			if(pClient->iEntityID)
			{
				stashIntRemovePointer(gTestClientEntities, pClient->iEntityID, NULL);
			}
			
			pClient->iEntityID = id;
			stashIntAddPointer(gTestClientEntities, id, pClient, true);
		}
	}
}

static void TestServer_Client_DiscardResults(ContainerID id)
{
	TestClientInfo *pClient;

	stashIntFindPointer(gAllTestClients, id, &pClient);

	if(!pClient)
	{
		return;
	}

	estrDestroy(&pClient->pcResult);
	pClient->bDiscarded = true;

	if(!pClient->bConnected)
	{
		TestServer_Client_ReleaseID(pClient->iID);
		stashIntRemovePointer(gAllTestClients, id, NULL);
		free(pClient);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
char *TestServer_Client_Status()
{
	char *pchStatus = NULL;
	int spawned = stashGetCount(gAllTestClients);
	StashTableIterator pClientIter = {0};
	StashElement pElem = NULL;
	TestClientInfo *pClient;

	stashGetIterator(gAllTestClients, &pClientIter);

	estrPrintf(&pchStatus, "Test Client Status\n");

	while(stashGetNextElement(&pClientIter, &pElem))
	{
		pClient = stashElementGetPointer(pElem);

		if(pClient->pcCustomStatus)
		{
			estrConcatf(&pchStatus, "Client %d - %s\n", pClient->iID, pClient->pcCustomStatus);
		}
	}

	estrConcatf(&pchStatus, "%d spawned\n", spawned);

	return pchStatus;
}

AUTO_COMMAND_REMOTE;
int TestServer_Client_PushMetric(const char *pchName, float val)
{
	return TestServer_PushMetric(NULL, pchName, val);
}

AUTO_COMMAND_REMOTE;
void TestServer_Client_ClearMetric(const char *pchName)
{
	TestServer_ClearMetric(NULL, pchName);
}

AUTO_COMMAND_REMOTE;
int TestServer_Client_GetMetricCount(const char *pchName)
{
	return TestServer_GetMetricCount(NULL, pchName);
}

AUTO_COMMAND_REMOTE;
float TestServer_Client_GetMetricTotal(const char *pchName)
{
	return TestServer_GetMetricTotal(NULL, pchName);
}

AUTO_COMMAND_REMOTE;
float TestServer_Client_GetMetricAvg(const char *pchName)
{
	return TestServer_GetMetricAverage(NULL, pchName);
}

AUTO_COMMAND_REMOTE;
float TestServer_Client_GetMetricMax(const char *pchName)
{
	return TestServer_GetMetricMaximum(NULL, pchName);
}

AUTO_COMMAND_REMOTE;
float TestServer_Client_GetMetricMin(const char *pchName)
{
	return TestServer_GetMetricMinimum(NULL, pchName);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_Spawn(TestClientSpawnRequest *requestInfo)
{
	eaPush(&gppClientsToSpawn, StructClone(parse_TestClientSpawnRequest, requestInfo));
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_CancelAllSpawns()
{
	FOR_EACH_IN_EARRAY(gppClientsToSpawn, TestClientSpawnRequest, pClient)
	{
		eaRemove(&gppClientsToSpawn, ipClientIndex);
		StructDestroy(parse_TestClientSpawnRequest, pClient);
	}
	FOR_EACH_END
}

typedef void (*TestServer_Client_ActionCB)(TestClientInfo *pClient, const void *pUserdata);

static void TestServer_Client_ActionSpecific(ContainerID id, TestServer_Client_ActionCB pCallback, const void *pUserdata)
{
	TestClientInfo *pClient = TestServer_Client_GetInfoByEntityID(id);

	if(!pClient)
	{
		return;
	}

	pCallback(pClient, pUserdata);
}

static void TestServer_Client_ActionID(ContainerID id, TestServer_Client_ActionCB pCallback, const void *pUserdata)
{
	TestClientInfo *pClient = TestServer_Client_GetInfoByID(id);

	if(!pClient)
	{
		return;
	}

	pCallback(pClient, pUserdata);
}

static void TestServer_Client_ActionRange(ContainerID idMin, ContainerID idMax, TestServer_Client_ActionCB pCallback, const void *pUserdata)
{
	TestClientInfo *pClient;
	ContainerID id;

	for(id = idMin; id <= idMax; ++id)
	{
		pClient = TestServer_Client_GetInfoByID(id);

		if(!pClient)
		{
			continue;
		}

		pCallback(pClient, pUserdata);
	}
}

static void TestServer_Client_ActionAll(TestServer_Client_ActionCB pCallback, const void *pUserdata)
{
	TestClientInfo *pClient;
	StashTableIterator iter = {0};
	StashElement pElem;

	stashGetIterator(gAllTestClients, &iter);

	while(stashGetNextElement(&iter, &pElem))
	{
		pClient = stashElementGetPointer(pElem);
		pCallback(pClient, pUserdata);
	}
}

static TestServer_Client_KillAction(TestClientInfo *pClient, const void *pUserdata)
{
	eaPush(&gppClientsToKill, pClient);
}

static TestServer_Client_ClaimAction(TestClientInfo *pClient, const ContainerID *piOwnerID)
{
	Packet *pkt;
	ContainerID iOwnerID = *piOwnerID;

	if(pClient->bConnected)
	{
		pkt = TestServer_GetDestinationPacket(pClient->pNetLink, pClient->iMultiplexIndex, FROM_TESTSERVER_TESTCLIENT_COMMAND);
		pktSendStringf(pkt, "OwnerID %d", iOwnerID);
		pktSend(&pkt);
	}
}

static TestServer_Client_CommandAction(TestClientInfo *pClient, const char *cmd)
{
	Packet *pkt;
	
	if(pClient->bConnected)
	{
		pkt = TestServer_GetDestinationPacket(pClient->pNetLink, pClient->iMultiplexIndex, FROM_TESTSERVER_TESTCLIENT_COMMAND);
		pktSendString(pkt, cmd);
		pktSend(&pkt);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_KillSpecific(ContainerID id)
{
	TestServer_Client_ActionSpecific(id, TestServer_Client_KillAction, NULL);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_KillID(ContainerID id)
{
	TestServer_Client_ActionID(id, TestServer_Client_KillAction, NULL);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_KillRange(ContainerID idMin, ContainerID idMax)
{
	TestServer_Client_ActionRange(idMin, idMax, TestServer_Client_KillAction, NULL);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_KillAll()
{
	TestServer_Client_ActionAll(TestServer_Client_KillAction, NULL);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_ClaimSpecific(ContainerID iOwnerID, ContainerID id)
{
	TestServer_Client_ActionSpecific(id, TestServer_Client_ClaimAction, &iOwnerID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_ClaimID(ContainerID iOwnerID, ContainerID id)
{
	TestServer_Client_ActionID(id, TestServer_Client_ClaimAction, &iOwnerID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_ClaimRange(ContainerID iOwnerID, ContainerID idMin, ContainerID idMax)
{
	TestServer_Client_ActionRange(idMin, idMax, TestServer_Client_ClaimAction, &iOwnerID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_ClaimAll(ContainerID iOwnerID)
{
	TestServer_Client_ActionAll(TestServer_Client_ClaimAction, &iOwnerID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_CommandSpecific(ContainerID id, const char *cmd)
{
	TestServer_Client_ActionSpecific(id, TestServer_Client_CommandAction, cmd);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_CommandID(ContainerID id, const char *cmd)
{
	TestServer_Client_ActionID(id, TestServer_Client_CommandAction, cmd);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_CommandRange(ContainerID idMin, ContainerID idMax, const char *cmd)
{
	TestServer_Client_ActionRange(idMin, idMax, TestServer_Client_CommandAction, cmd);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void TestServer_Client_CommandAll(const char *cmd)
{
	TestServer_Client_ActionAll(TestServer_Client_CommandAction, cmd);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_SetGlobal_Integer);
void TestServer_Sharded_SetGlobal_Integer(const char *pcScope, const char *pcName, int pos, int val)
{
	TestServer_SetGlobal_Integer(pcScope, pcName, pos, val);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_SetGlobal_Boolean);
void TestServer_Sharded_SetGlobal_Boolean(const char *pcScope, const char *pcName, int pos, bool val)
{
	TestServer_SetGlobal_Boolean(pcScope, pcName, pos, val);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_SetGlobal_Float);
void TestServer_Sharded_SetGlobal_Float(const char *pcScope, const char *pcName, int pos, float val)
{
	TestServer_SetGlobal_Float(pcScope, pcName, pos, val);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_SetGlobal_String);
void TestServer_Sharded_SetGlobal_String(const char *pcScope, const char *pcName, int pos, const char *val)
{
	TestServer_SetGlobal_String(pcScope, pcName, pos, val);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_SetGlobal_Password);
void TestServer_Sharded_SetGlobal_Password(const char *pcScope, const char *pcName, int pos, const char *val)
{
	TestServer_SetGlobal_Password(pcScope, pcName, pos, val);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_SetGlobal_Ref);
void TestServer_Sharded_SetGlobal_Ref(const char *pcScope, const char *pcName, int pos, const char *scope, const char *name)
{
	TestServer_SetGlobal_Ref(pcScope, pcName, pos, scope, name);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_InsertGlobal_Integer);
void TestServer_Sharded_InsertGlobal_Integer(const char *pcScope, const char *pcName, int pos, int val)
{
	TestServer_InsertGlobal_Integer(pcScope, pcName, pos, val);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_InsertGlobal_Boolean);
void TestServer_Sharded_InsertGlobal_Boolean(const char *pcScope, const char *pcName, int pos, bool val)
{
	TestServer_InsertGlobal_Boolean(pcScope, pcName, pos, val);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_InsertGlobal_Float);
void TestServer_Sharded_InsertGlobal_Float(const char *pcScope, const char *pcName, int pos, float val)
{
	TestServer_InsertGlobal_Float(pcScope, pcName, pos, val);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_InsertGlobal_String);
void TestServer_Sharded_InsertGlobal_String(const char *pcScope, const char *pcName, int pos, const char *val)
{
	TestServer_InsertGlobal_String(pcScope, pcName, pos, val);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_InsertGlobal_Password);
void TestServer_Sharded_InsertGlobal_Password(const char *pcScope, const char *pcName, int pos, const char *val)
{
	TestServer_InsertGlobal_Password(pcScope, pcName, pos, val);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_InsertGlobal_Ref);
void TestServer_Sharded_InsertGlobal_Ref(const char *pcScope, const char *pcName, int pos, const char *scope, const char *name)
{
	TestServer_InsertGlobal_Ref(pcScope, pcName, pos, scope, name);
}

AUTO_COMMAND_REMOTE ACMD_NAME(TestServer_RunScript);
void TestServer_Sharded_RunScript(const char *pcScript)
{
	TestServer_RunScript(pcScript);
}
