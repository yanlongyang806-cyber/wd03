#include "GameServerLib.h"
#include "ServerLib.h"
#include "referencesystem.h"
#include "StringCache.h"
#include "NameList.h"
#include "file.h"
#include "GameAccountDataCommon.h"
#include "GlobalTypes.h"
#include "GameServerLib.h"
#include "gslSendToClient.h"
#include "PowerTree.h"
#include "PowerTreeTransactions.h"
#include "Character.h"
#include "Powers.h"
#include "EntityLib.h"
#include "Player.h"
#include "gslContact.h"
#include "gslMission.h"
#include "gslPartition.h"
#include "GameEvent.h"
#include "gslMapTransfer.h"
#include "EntityIterator.h"
#include "contact_common.h"
#include "item/itemTransaction.h"
#include "mission_common.h"
#include "rewardCommon.h"
#include "TestClientCommon.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/TestServer_autogen_RemoteFuncs.h"

CmdList gTCCommandList;

// void TransactionHammer_CB(TransactionReturnVal *returnVal, void *userData)
// {
// 	return;
// }
// 
// int giTransactionHammerCount = 0;
// int giTransHammerPerFrame = 250;
// AUTO_CMD_INT(giTransHammerPerFrame, TransHammerPerFrame);
// void gslTestClient_TransactionHammer_Callback(TimedCallback *callback, F32 timeSinceLastCallback, void *userdata);
// void gslTestClient_TransactionHammer_Dispatch()
// {
// 	int count = 0;
// 
// 	while(giTransactionHammerCount > 0)
// 	{
// 		--giTransactionHammerCount;
// 		RemoteCommand_TestClient_Hammer(objCreateManagedReturnVal(TransactionHammer_CB, NULL), GLOBALTYPE_TESTCLIENTSERVER, 0);
// 		++count;
// 
// 		if(count >= giTransHammerPerFrame)
// 		{
// 			TimedCallback_Run(gslTestClient_TransactionHammer_Callback, NULL, 0.0f);
// 			break;
// 		}
// 	}
// }
// 
// void gslTestClient_TransactionHammer_Callback(TimedCallback *callback, F32 timeSinceLastCallback, void *userdata)
// {
// 	gslTestClient_TransactionHammer_Dispatch();
// }
// 
// AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(TransactionHammer);
// void gslTestClient_TransactionHammer(int iCount)
// {
// 	giTransactionHammerCount = iCount;
// 	gslTestClient_TransactionHammer_Dispatch();	
// }
// 
// AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(TransactionHammerReset);
// void gslTestClient_TransactionHammerReset()
// {
// 	RemoteCommand_TestClient_ResetHammer(GLOBALTYPE_TESTCLIENTSERVER, 0);
// }

AUTO_COMMAND_REMOTE ACMD_IFDEF(TESTSERVER) ACMD_IFDEF(GAMESERVER) ACMD_NAME(TestClient_Receive);
void gslTestClient_Receive(CmdContext *context, ContainerID iSenderID, char *sender, char *cmd)
{
	Entity *pEnt;

	if(!context || context->clientType != GLOBALTYPE_ENTITYPLAYER)
	{
		return;
	}

	pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, context->clientID);

	if(!pEnt)
	{
		return;
	}

	ClientCmd_TestClient_Receive(pEnt, iSenderID, sender, cmd);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(TestClient) ACMD_CATEGORY(TestClient);
void gslTestClientCommand(Entity *pEnt, CmdContext *pContext, ACMD_NAMELIST(gTCCommandList, COMMANDLIST) ACMD_SENTENCE cmd)
{
	CmdContext cmd_context = *pContext;

	if(!strStartsWith(cmd, "Startup") &&
		!strStartsWith(cmd, "NextUsefulContact") &&
		!strStartsWith(cmd, "BlacklistMission") &&
		!strStartsWith(cmd, "WarpToMission") &&
		!strStartsWith(cmd, "LearnPowerTree"))
	{
		VerifyServerTypeExistsInShard(GLOBALTYPE_TESTSERVER);
	}

	cmdParseAndExecute(&gTCCommandList, cmd, &cmd_context);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(Startup);
void gslTestClient_Startup()
{
	RemoteCommand_StartServer_NoReturn(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_TESTSERVER, 0, NULL, "gslTestClient_Startup()", NULL);
}

typedef struct LearnPowerTreeRequest
{
	EntityRef iRef;
	PowerTreeDef *pTreeDef;
} LearnPowerTreeRequest;

void LearnPowerTree_CB(TransactionReturnVal *returnVal, void *userData)
{
	LearnPowerTreeRequest *pRequest = (LearnPowerTreeRequest *)userData;
	Entity *pEnt = entFromEntityRefAnyPartition(pRequest->iRef);

	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pRequest->pTreeDef->ppGroups, PTGroupDef, pGroupDef)
		{
			if(!stricmp(pGroupDef->pchGroup, "Auto"))
			{
				FOR_EACH_IN_EARRAY_FORWARDS(pGroupDef->ppNodes, PTNodeDef, pNodeDef)
				{
					entity_PowerTreeNodeInceaseRank(entGetPartitionIdx(pEnt), pEnt, pRequest->pTreeDef->pchName, pNodeDef->pchName);
				}
				FOR_EACH_END

				break;
			}
		}
		FOR_EACH_END
	}

	free(pRequest);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(LearnPowerTree) ACMD_HIDE;
void gslTestClient_LearnPowerTree(Entity *pEnt, ACMD_NAMELIST("PowerTreeDef", REFDICTIONARY) char *pchTreeName)
{
	PowerTreeDef *pTreeDef;
	LearnPowerTreeRequest *pRequest;

	if(!pEnt)
	{
		return;
	}

	pTreeDef = powertreedef_Find(pchTreeName);

	if(!pTreeDef)
	{
		return;
	}

	pRequest = calloc(1, sizeof(LearnPowerTreeRequest));
	pRequest->iRef = entGetRef(pEnt);
	pRequest->pTreeDef = pTreeDef;

	objRequestTransactionSimplef(objCreateManagedReturnVal(LearnPowerTree_CB, pRequest), entGetType(pEnt), entGetContainerID(pEnt), "AddPowerTree", "create pChar.ppPowerTrees[%s]", pchTreeName);
}

extern ContactLocation **g_ContactLocations;
extern DictionaryHandle g_ContactDictionary;
const char **ppBlacklistedMissions = NULL;

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(NextUsefulContact) ACMD_HIDE;
void gslTestClient_NextUsefulContact(Entity *pEnt)
{
	static int lastContact = 0;
	static int repeats = 0;
	int i, j, k;
	ContactDef *pContactDef;
	ContactMissionOffer** eaOfferList = NULL;
	ItemChangeReason reason = {0};

	if(eaSize(&g_ContactLocations) == 0)
	{
		gslSendPrintf(pEnt, "NO CONTACT");
		return;
	}

	inv_FillItemChangeReason(&reason, pEnt, "Internal:NextUsefulContact", NULL);

	for(i = 0; i < eaSize(&g_ContactLocations); ++i)
	{
		pContactDef = RefSystem_ReferentFromString(g_ContactDictionary, g_ContactLocations[i]->pchContactDefName);

		if(!pContactDef || !contact_CanInteract(pContactDef, pEnt) || (lastContact == i+1 && repeats > 10))
		{
			continue;
		}

		contact_GetMissionOfferList(pContactDef, pEnt, &eaOfferList);

		for(j = 0; j < eaSize(&eaOfferList); ++j)
		{
			ContactMissionOffer* pOffer = eaOfferList[j];
			MissionDef *pMissionDef = GET_REF(pOffer->missionDef);
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
			MissionOfferStatus eStatus = MissionOfferStatus_OK;
			int iOfferLevel = -1;
			Mission *pMission = NULL;
			bool bBlacklisted = false;

			if(pMissionDef->repeatable)
			{
				// Skip repeatables for now, they will probably wreck our plans for world domination
				continue;
			}

			for(k = 0; k < eaSize(&ppBlacklistedMissions); ++k)
			{
				if(!stricmp(ppBlacklistedMissions[k], pMissionDef->name))
				{
					bBlacklisted = true;
					break;
				}
			}

			if(bBlacklisted)
			{
				continue;
			}

			if((pOffer->allowGrantOrReturn == ContactMissionAllow_GrantOnly ||
				pOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn) &&
				(missiondef_CanBeOfferedAsPrimary(pEnt, pMissionDef, &iOfferLevel, &eStatus) || eStatus == MissionOfferStatus_TooLowLevel) &&
				!(gConf.iMaxActiveMissions && missiondef_CountsTowardsMaxActive(pMissionDef) && mission_GetNumMissionsTowardsMaxActive(pInfo) >= gConf.iMaxActiveMissions))
			{
				if(lastContact != i+1)
				{
					repeats = 0;
				}

				lastContact = i+1;
				++repeats;

				if(eStatus == MissionOfferStatus_TooLowLevel && iOfferLevel > -1)
				{
					itemtransaction_SetNumeric(pEnt, gConf.pcLevelingNumericItem, NUMERIC_AT_LEVEL(iOfferLevel), &reason, NULL, NULL);
				}
				entSetPos(pEnt, g_ContactLocations[i]->loc, 1, "ec");
				gslSendPrintf(pEnt, "OK CONTACT");
				if(eaOfferList)
					eaDestroy(&eaOfferList);
				return;
			}
			else if((pOffer->allowGrantOrReturn == ContactMissionAllow_ReturnOnly ||
				pOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn) &&
				(pMission = mission_GetMissionFromDef(pInfo, pMissionDef)) &&
				pMission->state == MissionState_Succeeded)
			{
				if(lastContact != i+1)
				{
					repeats = 0;
				}

				lastContact = i+1;
				++repeats;

				entSetPos(pEnt, g_ContactLocations[i]->loc, 1, "ec");
				gslSendPrintf(pEnt, "OK CONTACT");
				if(eaOfferList)
					eaDestroy(&eaOfferList);
				return;
			}
		}
	}

	eaClear(&eaOfferList);

	for(i = 0; i < eaSize(&pEnt->pPlayer->pInteractInfo->eaRemoteContacts); ++i)
	{
		pContactDef = RefSystem_ReferentFromString(g_ContactDictionary, pEnt->pPlayer->pInteractInfo->eaRemoteContacts[i]->pchContactDef);

		if(!pContactDef || !contact_CanInteract(pContactDef, pEnt) || (lastContact == -(i+1) && repeats > 5))
		{
			continue;
		}

		contact_GetMissionOfferList(pContactDef, pEnt, &eaOfferList);

		for(j = 0; j < eaSize(&eaOfferList); ++j)
		{
			ContactMissionOffer* pOffer = eaOfferList[j];
			MissionDef *pMissionDef = GET_REF(pOffer->missionDef);
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
			MissionOfferStatus eStatus = MissionOfferStatus_OK;
			int iOfferLevel = -1;
			Mission *pMission = NULL;
			bool bBlacklisted = false;

			if(pMissionDef->repeatable)
			{
				// Skip repeatables for now, they will probably wreck our plans for world domination
				continue;
			}

			for(k = 0; k < eaSize(&ppBlacklistedMissions); ++k)
			{
				if(!stricmp(ppBlacklistedMissions[k], pMissionDef->name))
				{
					bBlacklisted = true;
					break;
				}
			}

			if(bBlacklisted)
			{
				continue;
			}

			if(pEnt->pPlayer->pInteractInfo->eaRemoteContacts[i]->eFlags & ContactFlag_RemoteOfferGrant &&
				(pOffer->allowGrantOrReturn == ContactMissionAllow_GrantOnly ||
				pOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn) &&
				(missiondef_CanBeOfferedAsPrimary(pEnt, pMissionDef, &iOfferLevel, &eStatus) || eStatus == MissionOfferStatus_TooLowLevel) &&
				!(gConf.iMaxActiveMissions && missiondef_CountsTowardsMaxActive(pMissionDef) && mission_GetNumMissionsTowardsMaxActive(pInfo) >= gConf.iMaxActiveMissions))
			{
				if(lastContact != -(i+1))
				{
					repeats = 0;
				}

				lastContact = -(i+1);
				++repeats;

				if(eStatus == MissionOfferStatus_TooLowLevel && iOfferLevel > -1)
				{
					itemtransaction_SetNumeric(pEnt, gConf.pcLevelingNumericItem, NUMERIC_AT_LEVEL(iOfferLevel), &reason, NULL, NULL);
				}
				gslSendPrintf(pEnt, "OK REMOTE CONTACT %d", -(i+1));
				if(eaOfferList)
					eaDestroy(&eaOfferList);
				return;
			}
			else if(pEnt->pPlayer->pInteractInfo->eaRemoteContacts[i]->eFlags & ContactFlag_RemoteOfferReturn &&
				(pOffer->allowGrantOrReturn == ContactMissionAllow_ReturnOnly ||
				pOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn) &&
				(pMission = mission_GetMissionFromDef(pInfo, pMissionDef)) &&
				pMission->state == MissionState_Succeeded)
			{
				if(lastContact != -(i+1))
				{
					repeats = 0;
				}

				lastContact = -(i+1);
				++repeats;

				gslSendPrintf(pEnt, "OK REMOTE CONTACT %d", -(i+1));
				if(eaOfferList)
					eaDestroy(&eaOfferList);
				return;
			}
		}
	}

	if(eaOfferList)
		eaDestroy(&eaOfferList);

	gslSendPrintf(pEnt, "NO CONTACT");
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(BlacklistMission) ACMD_HIDE;
void gslTestClient_BlacklistMission(Entity *pEnt, const char *pMissionName)
{
	eaPush(&ppBlacklistedMissions, allocAddString(pMissionName));
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(WarpToMission) ACMD_HIDE;
void gslTestClient_WarpToMission(Entity *pEnt, const char *pMissionName)
{
	MissionInfo *pInfo = NULL;
	Mission *pMission = NULL;
	MissionDef *pDef = NULL;

	if(!pEnt || !(pInfo = mission_GetInfoFromPlayer(pEnt)) || !(pMission = mission_FindMissionFromRefString(pInfo, pMissionName)) || !(pDef = mission_GetDef(pMission)))
	{
		return;
	}

	if(pMission->state > MissionState_InProgress)
	{
		if(pDef->needsReturn && pDef->pchReturnMap && stricmp(gGSLState.gameServerDescription.baseMapDescription.mapDescription, pDef->pchReturnMap))
		{
			MapDescription *pDesc = StructCreate(parse_MapDescription);
			MapMoveFillMapDescription(pDesc, pDef->pchReturnMap, ZMTYPE_UNSPECIFIED, NULL, 0, 0, 0, 0, NULL);
			MapMoveOrSpawnWithDescription(pEnt, pDesc, "TestClient WarpToMission",0);
			gslSendPrintf(pEnt, "OK WARP");
			return;
		}
		else
		{
			gslSendPrintf(pEnt, "NO WARP");
			return;
		}
	}

	FOR_EACH_IN_EARRAY_FORWARDS(pDef->eaTrackedEvents, GameEvent, pEvent)
	{
		if(pEvent->pchMapName && stricmp(gGSLState.gameServerDescription.baseMapDescription.mapDescription, pEvent->pchMapName))
		{
			MapDescription *pDesc = StructCreate(parse_MapDescription);
			MapMoveFillMapDescription(pDesc, pEvent->pchMapName, ZMTYPE_UNSPECIFIED, NULL, 0, 0, 0, 0, NULL);
			MapMoveOrSpawnWithDescription(pEnt, pDesc, "TestClient WarpToMission",0);
			gslSendPrintf(pEnt, "OK WARP");
			return;
		}
	}
	FOR_EACH_END

	gslSendPrintf(pEnt, "NO WARP");
}

static TimedCallback *pChurnCB = NULL;
static char memchurn_timestr[CRYPTIC_MAX_PATH];
static bool bMemoryChurnRunning = false;
static bool bMemoryChurnSoakDone = false;
static F32 fMemoryChurnSoakTime = 0.0f;
static U32 iMemoryChurnNumClients = 0;
static int iMemoryChurnCurIteration = 0;
static int iMemoryChurnIterations = 2;

void gslTestClient_MemoryChurn_ClientCheck(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData);
void gslTestClient_MemoryChurn_Continue(void);

void gslTestClient_MemoryChurn_DumpCallback(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	char memlogname[CRYPTIC_MAX_PATH];

	if(!bMemoryChurnRunning)
	{
		globCmdParse("encounterprocessing 1");
		return;
	}

	printf("MemoryChurn %d: dumping memlog.\n", iMemoryChurnCurIteration);

	// Dump memory
	sprintf(memlogname, "C:\\%s_memchurn_%d.txt", memchurn_timestr, iMemoryChurnCurIteration);
	memCheckDumpAllocsFile(memlogname);

	// Reenable encounter processing
	globCmdParse("encounterprocessing 1");

	// Call the next iteration (will terminate if it should)
	gslTestClient_MemoryChurn_Continue();
}

void gslTestClient_MemoryChurn_SoakCallback(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	if(!bMemoryChurnRunning)
	{
		globCmdParse("TestClient KillAllLocal");
		globCmdParse("encounterprocessing 1");
		return;
	}

	printf("MemoryChurn %d: killing all clients.\n", iMemoryChurnCurIteration);

	// Kill all clients
	globCmdParse("TestClient KillAllLocal");
	bMemoryChurnSoakDone = true;
	pChurnCB = TimedCallback_Add(gslTestClient_MemoryChurn_ClientCheck, NULL, 1.0f);
}

void gslTestClient_MemoryChurn_ClientCheck(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	if(!bMemoryChurnRunning)
	{
		TimedCallback_Remove(pChurnCB);
		globCmdParse("TestClient KillAllLocal");
		globCmdParse("encounterprocessing 1");
		return;
	}

	if(!bMemoryChurnSoakDone && gGSLState.clients.loggedInCount >= iMemoryChurnNumClients)
	{
		TimedCallback_Remove(pChurnCB);

		printf("MemoryChurn %d: beginning soak for %0.1f seconds.\n", iMemoryChurnCurIteration, fMemoryChurnSoakTime);

		// Soak the clients for the specified amount of time
		pChurnCB = TimedCallback_Run(gslTestClient_MemoryChurn_SoakCallback, NULL, fMemoryChurnSoakTime);
	}
	else if(bMemoryChurnSoakDone && gGSLState.clients.loggedInCount == 0)
	{
		TimedCallback_Remove(pChurnCB);

		printf("MemoryChurn %d: dumping memlog in 5.0 seconds.\n", iMemoryChurnCurIteration);

		// Kill all critters and encounter processing on the server
		globCmdParse("encounterprocessing 0");
		globCmdParse("ec -1 destroy");

		// Dump memlog
		pChurnCB = TimedCallback_Run(gslTestClient_MemoryChurn_DumpCallback, NULL, 5.0f);
	}
}

void gslTestClient_MemoryChurn_Stop(void)
{
	if(!bMemoryChurnRunning)
	{
		return;
	}

	bMemoryChurnRunning = false;
}

void gslTestClient_MemoryChurn_Continue(void)
{
	if(!bMemoryChurnRunning)
	{
		return;
	}

	if(++iMemoryChurnCurIteration > iMemoryChurnIterations)
	{
		printf("MemoryChurn: completed.\n");
		bMemoryChurnRunning = false;
		return;
	}

	printf("MemoryChurn %d: spawning clients.\n", iMemoryChurnCurIteration);

	bMemoryChurnSoakDone = false;

	// Turn encounters back on (if they're not on)
	globCmdParse("encounterprocessing 1");

	// Spawn a bunch of clients
	globCmdParsef("TestClient SpawnMultipleLocalWith %d Default -ForceCreate -ClientFPS 5", iMemoryChurnNumClients);
	pChurnCB = TimedCallback_Add(gslTestClient_MemoryChurn_ClientCheck, NULL, 1.0f);
}

void gslTestClient_MemoryChurn_Start(int numClients, F32 soakTime, int numIterations)
{
	if(bMemoryChurnRunning)
	{
		return;
	}

	bMemoryChurnRunning = true;
	bMemoryChurnSoakDone = false;
	fMemoryChurnSoakTime = soakTime;
	iMemoryChurnNumClients = numClients;
	iMemoryChurnCurIteration = 0;
	iMemoryChurnIterations = numIterations;
	memchurn_timestr[0] = 0;

	// Record the initial timestamp
	timeMakeFilenameDateStringFromSecondsSince2000(memchurn_timestr, timeClampSecondsSince2000ToMinutes(timeSecondsSince2000(), 1));

	gslTestClient_MemoryChurn_Continue();
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(MemoryChurnTest);
void gslTestClient_MemoryChurnTest(int numClients, F32 soakTime, int numIterations)
{
	gslTestClient_MemoryChurn_Start(numClients, soakTime, numIterations);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(MemoryChurnTest) ACMD_ACCESSLEVEL(9);
void gslTestClient_MemoryChurnTestDefault(void)
{
	gslTestClient_MemoryChurn_Start(100, 3600.0f, 2);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(MemoryChurnTestStop);
void gslTestClient_MemoryChurnTestStop(void)
{
	gslTestClient_MemoryChurn_Stop();
}

void Status_CB(TransactionReturnVal *returnVal, void *userData)
{
	char *status = NULL;
	Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)((intptr_t)userData));

	switch(RemoteCommandCheck_TestServer_Client_Status(returnVal, &status))
	{
	case TRANSACTION_OUTCOME_SUCCESS:
		gslSendPrintf(pEnt, "%s", status);
	xdefault:
		break;
	}
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(Status);
void gslTestClient_Status(Entity *pEnt)
{
	RemoteCommand_TestServer_Client_Status(objCreateManagedReturnVal(Status_CB, (void *)((intptr_t)entGetRef(pEnt))), GLOBALTYPE_TESTSERVER, 0);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(ClearMetric);
void gslTestClient_ClearMetric(char *key)
{
	RemoteCommand_TestServer_Client_ClearMetric(GLOBALTYPE_TESTSERVER, 0, key);
}

typedef struct TestClientMetricRequest
{
	char *key;
	char *metric;
	EntityRef ent;
} TestClientMetricRequest;

void GetMetric_CB(TransactionReturnVal *returnVal, void *userData)
{
	TestClientMetricRequest *request = (TestClientMetricRequest *)userData;
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_SUCCESS:
		gslSendPrintf(entFromEntityRefAnyPartition(request->ent),
			"TestClientServer metric %s: (%s, %s)",
			request->metric,
			request->key,
			returnVal->pBaseReturnVals->returnString);
	xdefault:
		break;
	}

	free(userData);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(PushMetric);
void gslTestClient_PushMetric(Entity *pEnt, char *key, F32 val, bool bPersist)
{
	TransactionReturnVal *returnVal;
	TestClientMetricRequest *pRequest = calloc(1, sizeof(TestClientMetricRequest));

	pRequest->key = NULL;
	pRequest->metric = NULL;
	estrCopy2(&pRequest->key, key);
	estrPrintf(&pRequest->metric, "count");
	pRequest->ent = entGetRef(pEnt);

	returnVal = objCreateManagedReturnVal(GetMetric_CB, (void *)pRequest);

	RemoteCommand_TestServer_Client_PushMetric(returnVal, GLOBALTYPE_TESTSERVER, 0, key, val);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(GetMetricMax);
void gslTestClient_GetMetricMax(Entity *pEnt, char *key)
{
	TransactionReturnVal *returnVal;
	TestClientMetricRequest *pRequest = calloc(1, sizeof(TestClientMetricRequest));

	pRequest->key = NULL;
	pRequest->metric = NULL;
	estrCopy2(&pRequest->key, key);
	estrPrintf(&pRequest->metric, "maximum");
	pRequest->ent = entGetRef(pEnt);

	returnVal = objCreateManagedReturnVal(GetMetric_CB, (void *)pRequest);

	RemoteCommand_TestServer_Client_GetMetricMax(returnVal, GLOBALTYPE_TESTSERVER, 0, key);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(GetMetricMin);
void gslTestClient_GetMetricMin(Entity *pEnt, char *key)
{
	TransactionReturnVal *returnVal;
	TestClientMetricRequest *pRequest = calloc(1, sizeof(TestClientMetricRequest));

	pRequest->key = NULL;
	pRequest->metric = NULL;
	estrCopy2(&pRequest->key, key);
	estrPrintf(&pRequest->metric, "minimum");
	pRequest->ent = entGetRef(pEnt);

	returnVal = objCreateManagedReturnVal(GetMetric_CB, (void *)pRequest);

	RemoteCommand_TestServer_Client_GetMetricMin(returnVal, GLOBALTYPE_TESTSERVER, 0, key);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(GetMetricAvg);
void gslTestClient_GetMetricAvg(Entity *pEnt, char *key)
{
	TransactionReturnVal *returnVal;
	TestClientMetricRequest *pRequest = calloc(1, sizeof(TestClientMetricRequest));

	pRequest->key = NULL;
	pRequest->metric = NULL;
	estrCopy2(&pRequest->key, key);
	estrPrintf(&pRequest->metric, "average");
	pRequest->ent = entGetRef(pEnt);

	returnVal = objCreateManagedReturnVal(GetMetric_CB, (void *)pRequest);

	RemoteCommand_TestServer_Client_GetMetricAvg(returnVal, GLOBALTYPE_TESTSERVER, 0, key);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(GetMetricTotal);
void gslTestClient_GetMetricTotal(Entity *pEnt, char *key)
{
	TransactionReturnVal *returnVal;
	TestClientMetricRequest *pRequest = calloc(1, sizeof(TestClientMetricRequest));

	pRequest->key = NULL;
	pRequest->metric = NULL;
	estrCopy2(&pRequest->key, key);
	estrPrintf(&pRequest->metric, "sum");
	pRequest->ent = entGetRef(pEnt);

	returnVal = objCreateManagedReturnVal(GetMetric_CB, (void *)pRequest);

	RemoteCommand_TestServer_Client_GetMetricTotal(returnVal, GLOBALTYPE_TESTSERVER, 0, key);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(GetMetricCount);
void gslTestClient_GetMetricCount(Entity *pEnt, char *key)
{
	TransactionReturnVal *returnVal;
	TestClientMetricRequest *pRequest = calloc(1, sizeof(TestClientMetricRequest));

	pRequest->key = NULL;
	pRequest->metric = NULL;
	estrCopy2(&pRequest->key, key);
	estrPrintf(&pRequest->metric, "count");
	pRequest->ent = entGetRef(pEnt);

	returnVal = objCreateManagedReturnVal(GetMetric_CB, (void *)pRequest);

	RemoteCommand_TestServer_Client_GetMetricCount(returnVal, GLOBALTYPE_TESTSERVER, 0, key);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(GetMetric);
void gslTestClient_GetMetric(Entity *pEnt, char *key)
{
	gslTestClient_GetMetricCount(pEnt, key);
	gslTestClient_GetMetricTotal(pEnt, key);
	gslTestClient_GetMetricAvg(pEnt, key);
	gslTestClient_GetMetricMin(pEnt, key);
	gslTestClient_GetMetricMax(pEnt, key);
}

void gslTestClient_Spawn_internal(Entity *pEnt, int numClients, char *clientType, char *cmdLine, bool bLocal, F32 fDelay)
{
	TestClientSpawnRequest requestInfo = {0};

	strcpy(requestInfo.cScriptName, clientType);
	strcpy(requestInfo.cMapName, gGSLState.gameServerDescription.baseMapDescription.mapDescription);
	requestInfo.iInstanceIndex = bLocal ? partition_PublicInstanceIndexFromIdx(entGetPartitionIdx(pEnt)) : 0;

	if(pEnt && !entGetClientSelectedTargetPos(pEnt, requestInfo.spawnPos))
	{
		entGetPos(pEnt, requestInfo.spawnPos);
	}

	requestInfo.iOwnerID = pEnt ? entGetContainerID(pEnt) : 0;
	requestInfo.iNumToSpawn = numClients;
	requestInfo.fSpawnDelay = fDelay;
	estrCopy2(&requestInfo.pcCmdLine, cmdLine ? cmdLine : "");

	RemoteCommand_TestServer_Client_Spawn(GLOBALTYPE_TESTSERVER, 0, &requestInfo);

	estrDestroy(&requestInfo.pcCmdLine);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(Spawn);
void gslTestClient_Spawn(Entity *pEnt, char *clientType)
{
	gslTestClient_Spawn_internal(pEnt, 1, clientType, NULL, false, 0.04f);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(Spawn);
void gslTestClient_SpawnDefault(Entity *pEnt)
{
	gslTestClient_Spawn_internal(pEnt, 1, "Default", NULL, false, 0.04f);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnMultiple);
void gslTestClient_SpawnMultiple(Entity *pEnt, int numClients, char *clientType)
{
	gslTestClient_Spawn_internal(pEnt, numClients, clientType, NULL, false, 0.04f);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnWith);
void gslTestClient_SpawnWith(Entity *pEnt, char *clientType, ACMD_SENTENCE cmdLine)
{
	gslTestClient_Spawn_internal(pEnt, 1, clientType, cmdLine, false, 0.04f);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnMultipleWith);
void gslTestClient_SpawnMultipleWith(Entity *pEnt, int numClients, char *clientType, ACMD_SENTENCE cmdLine)
{
	gslTestClient_Spawn_internal(pEnt, numClients, clientType, cmdLine, false, 0.04f);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnLocal);
void gslTestClient_SpawnLocal(Entity *pEnt, char *clientType)
{
	gslTestClient_Spawn_internal(pEnt, 1, clientType, NULL, true, 0.04f);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(SpawnLocal);
void gslTestClient_SpawnLocalDefault(Entity *pEnt)
{
	gslTestClient_Spawn_internal(pEnt, 1, "Default", NULL, true, 0.04f);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnMultipleLocal);
void gslTestClient_SpawnMultipleLocal(Entity *pEnt, int numClients, char *clientType)
{
	gslTestClient_Spawn_internal(pEnt, numClients, clientType, NULL, true, 0.04f);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnLocalWith);
void gslTestClient_SpawnLocalWith(Entity *pEnt, char *clientType, ACMD_SENTENCE cmdLine)
{
	gslTestClient_Spawn_internal(pEnt, 1, clientType, cmdLine, true, 0.04f);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnMultipleLocalWith);
void gslTestClient_SpawnMultipleLocalWith(Entity *pEnt, int numClients, char *clientType, ACMD_SENTENCE cmdLine)
{
	gslTestClient_Spawn_internal(pEnt, numClients, clientType, cmdLine, true, 0.04f);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(MassSpawn);
void gslTestClient_MassSpawn(Entity *pEnt, int numClients, char *clientType)
{
	gslTestClient_Spawn_internal(pEnt, numClients, clientType, NULL, false, 5.0f);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(MassSpawnWithDelay);
void gslTestClient_MassSpawnWithDelay(Entity *pEnt, int numClients, char *clientType, F32 fDelay)
{
	gslTestClient_Spawn_internal(pEnt, numClients, clientType, NULL, false, fDelay);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(CancelAllSpawns);
void gslTestClient_CancelAllSpawns(void)
{
	RemoteCommand_TestServer_Client_CancelAllSpawns(GLOBALTYPE_TESTSERVER, 0);
}

void SpawnPer_CB(TransactionReturnVal *returnVal, void *userData)
{
	TestClientSpawnRequest *requestInfo = (TestClientSpawnRequest *)userData;
	PossibleMapChoices *choices;

	switch(RemoteCommandCheck_GetPossibleMapChoices(returnVal, &choices))
	{
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			int i = 0;

			for(i = 0; i < eaSize(&choices->ppChoices); ++i)
			{
				if(choices->ppChoices[i]->bNotALegalChoice)
				{
					continue;
				}

				strcpy(requestInfo->cMapName, choices->ppChoices[i]->baseMapDescription.mapDescription);
				requestInfo->iInstanceIndex = choices->ppChoices[i]->baseMapDescription.mapInstanceIndex;

				RemoteCommand_TestServer_Client_Spawn(GLOBALTYPE_TESTSERVER, 0, requestInfo);
			}
		}
	xdefault:
		break;
	}

	StructDestroy(parse_TestClientSpawnRequest, requestInfo);
}

void gslTestClient_SpawnPer_internal(Entity *pEnt, int numClientsPer, char *mapName, char *clientType, char *cmdLine)
{
	TestClientSpawnRequest *requestInfo = StructCreate(parse_TestClientSpawnRequest);
	MapSearchInfo searchInfo = {0};

	strcpy(requestInfo->cScriptName, clientType);

	if(pEnt && !entGetClientSelectedTargetPos(pEnt, requestInfo->spawnPos))
	{
		entGetPos(pEnt, requestInfo->spawnPos);
	}

	requestInfo->iOwnerID = pEnt ? entGetContainerID(pEnt) : 0;
	requestInfo->iNumToSpawn = numClientsPer;
	estrCopy2(&requestInfo->pcCmdLine, cmdLine ? cmdLine : "");

	searchInfo.baseMapDescription.mapDescription = allocAddString(mapName);
	searchInfo.playerID = pEnt ? pEnt->myContainerID : 0;
	searchInfo.pPlayerName = strdup(pEnt ? entGetLangName(pEnt, entGetLanguage(pEnt)) : "ServerMonitor");

	RemoteCommand_GetPossibleMapChoices(objCreateManagedReturnVal(SpawnPer_CB, requestInfo), GLOBALTYPE_MAPMANAGER, 0, &searchInfo, NULL, "gslTestClient_SpawnPer_internal()");

	StructDeInit(parse_MapSearchInfo, &searchInfo);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnPer);
void gslTestClient_SpawnPer(Entity *pEnt, char *mapName, char *clientType)
{
	gslTestClient_SpawnPer_internal(pEnt, 1, mapName, clientType, NULL);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnMultiplePer);
void gslTestClient_SpawnMultiplePer(Entity *pEnt, int numClients, char *mapName, char *clientType)
{
	gslTestClient_SpawnPer_internal(pEnt, numClients, mapName, clientType, NULL);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnPerWith);
void gslTestClient_SpawnPerWith(Entity *pEnt, char *mapName, char *clientType, ACMD_SENTENCE cmdLine)
{
	gslTestClient_SpawnPer_internal(pEnt, 1, mapName, clientType, cmdLine);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(SpawnMultiplePerWith);
void gslTestClient_SpawnMultiplePerWith(Entity *pEnt, int numClients, char *mapName, char *clientType, ACMD_SENTENCE cmdLine)
{
	gslTestClient_SpawnPer_internal(pEnt, numClients, mapName, clientType, cmdLine);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(Kill);
void gslTestClient_Kill(Entity *pEnt)
{
	Entity *pTarget;

	if(!pEnt)
	{
		return;
	}
		
	pTarget	= entGetClientTarget(pEnt, "selected", NULL);

	if(!pTarget)
	{
		return;
	}

	RemoteCommand_TestServer_Client_KillSpecific(GLOBALTYPE_TESTSERVER, 0, entGetContainerID(pTarget));
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(KillID);
void gslTestClient_KillID(ContainerID iClientID)
{
	RemoteCommand_TestServer_Client_KillID(GLOBALTYPE_TESTSERVER, 0, iClientID);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(KillRange);
void gslTestClient_KillRange(ContainerID iClientMin, ContainerID iClientMax)
{
	RemoteCommand_TestServer_Client_KillRange(GLOBALTYPE_TESTSERVER, 0, iClientMin, iClientMax);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(KillAll);
void gslTestClient_KillAll(void)
{
	RemoteCommand_TestServer_Client_KillAll(GLOBALTYPE_TESTSERVER, 0);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(KillAllLocal);
void gslTestClient_KillAllLocal(void)
{
	EntityIterator *iter = NULL;
	Entity *pEnt = NULL;

	iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);

	while((pEnt = EntityIteratorGetNext(iter)))
	{
		RemoteCommand_TestServer_Client_KillSpecific(GLOBALTYPE_TESTSERVER, 0, entGetContainerID(pEnt));
	}
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(Claim);
void gslTestClient_Claim(Entity *pEnt)
{
	Entity *pTarget;

	if(!pEnt)
	{
		return;
	}

	pTarget = entGetClientTarget(pEnt, "selected", NULL);

	if(!pTarget)
	{
		return;
	}

	RemoteCommand_TestServer_Client_ClaimSpecific(GLOBALTYPE_TESTSERVER, 0, entGetContainerID(pEnt), entGetContainerID(pTarget));
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(ClaimID);
void gslTestClient_ClaimID(Entity *pEnt, ContainerID iClientID)
{
	if(!pEnt)
	{
		return;
	}

	RemoteCommand_TestServer_Client_ClaimID(GLOBALTYPE_TESTSERVER, 0, entGetContainerID(pEnt), iClientID);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(ClaimRange);
void gslTestClient_ClaimRange(Entity *pEnt, ContainerID iClientMin, ContainerID iClientMax)
{
	if(!pEnt)
	{
		return;
	}

	RemoteCommand_TestServer_Client_ClaimRange(GLOBALTYPE_TESTSERVER, 0, entGetContainerID(pEnt), iClientMin, iClientMax);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(ClaimAll);
void gslTestClient_ClaimAll(Entity *pEnt)
{
	if(!pEnt)
	{
		return;
	}

	RemoteCommand_TestServer_Client_ClaimAll(GLOBALTYPE_TESTSERVER, 0, entGetContainerID(pEnt));
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(Command);
void gslTestClient_Command(Entity *pEnt, ACMD_SENTENCE cmd)
{
	Entity *pTarget;

	if(!pEnt)
	{
		return;
	}

	pTarget = entGetClientTarget(pEnt, "selected", NULL);

	if(!pTarget)
	{
		return;
	}

	RemoteCommand_TestServer_Client_CommandSpecific(GLOBALTYPE_TESTSERVER, 0, entGetContainerID(pTarget), cmd);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(CommandID);
void gslTestClient_CommandID(ContainerID iClient, ACMD_SENTENCE cmd)
{
	RemoteCommand_TestServer_Client_CommandID(GLOBALTYPE_TESTSERVER, 0, iClient, cmd);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(CommandRange);
void gslTestClient_CommandRange(ContainerID iClientMin, ContainerID iClientMax, ACMD_SENTENCE cmd)
{
	RemoteCommand_TestServer_Client_CommandRange(GLOBALTYPE_TESTSERVER, 0, iClientMin, iClientMax, cmd);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(CommandAll);
void gslTestClient_CommandAll(ACMD_SENTENCE cmd)
{
	RemoteCommand_TestServer_Client_CommandAll(GLOBALTYPE_TESTSERVER, 0, cmd);
}

AUTO_COMMAND ACMD_LIST(gTCCommandList) ACMD_ACCESSLEVEL(9) ACMD_NAME(Send);
void gslTestClient_Send(Entity *pEnt, ContainerID iClient, ACMD_SENTENCE cmd)
{
	char *pHandle = NULL;

	if(!pEnt)
	{
		return;
	}

	estrPrintf(&pHandle, "%s@%s", entGetLangName(pEnt, entGetLanguage(pEnt)), entGetAccountOrLangName(pEnt, entGetLanguage(pEnt)));
	RemoteCommand_TestClient_Receive(GLOBALTYPE_ENTITYPLAYER, iClient, entGetContainerID(pEnt), pHandle, cmd);
	estrDestroy(&pHandle);
}

#include "TestClientCommon_h_ast.c"
