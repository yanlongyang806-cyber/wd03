/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiLib.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "Character.h"
#include "Entity.h"
#include "EntityBuild.h"
#include "EntityGrid.h"
#include "EString.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "gslEntity.h"
#include "gslMission.h"
#include "gslPartition.h"
#include "interaction_common.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "PowerModes.h"
#include "url.h"
#include "HttpClient.h"
#include "httpAsync.h"
#include "gslHttpAsync.h"
#include "Player.h"
#include "EntitySavedData.h"
#include "utilitiesLib.h"
#include "accountnet.h"
#include "AutoGen/accountnet_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/websrv_h_ast.h"
#include "GlobalTypeEnum.h"

// Use WebSrv for Web Game Events
static bool sbSendWebGameEventsToWebSrv = false;
AUTO_CMD_INT(sbSendWebGameEventsToWebSrv, UseWebSrv) ACMD_CATEGORY(GameServer_Setting);

// ----------------------------------------------------------------------------------
// Expression functions to test player and critter entities
// ----------------------------------------------------------------------------------

// Returns the number of players currently on the map
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(NumPlayersOnMap);
int exprFuncNumPlayersOnMap(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	return partition_GetPlayerCount(iPartitionIdx);
}


// Determine if a single entity is alive
// Only checks first entity in ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntityIsAlive);
U32 exprFuncEntityIsAlive(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts)
{
	if (peaEnts && eaSize(peaEnts)) {
		return entIsAlive((*peaEnts[0]));
	}

	ErrorFilenamef(exprContextGetBlameFile(pContext), "EntityIsAlive : No entities given");
	return 0;
}

// Determine if a single entity is alive
// Only checks first entity in ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntityIsNearDeath);
U32 exprFuncEntityIsNearDeath(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts)
{
	if (peaEnts && eaSize(peaEnts)) 
	{
		Entity *pEnt = *peaEnts[0];
		return pEnt->pChar && pEnt->pChar->pNearDeath;
	}

	ErrorFilenamef(exprContextGetBlameFile(pContext), "EntityIsNearDeath : No entities given");
	return 0;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(IsStatusTableMissingAnyLuckyCharmEntity);
U32 exprFuncIsStatusTableMissingAnyLuckyCharmEntity(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ENTARRAY_IN pppOwner, int eLuckyCharmType, bool bCheckFirstOnly)
{
	S32 i;
	Entity *pEnt = NULL, *pOwner = NULL;
	if (peaEnts == NULL || eaSize(peaEnts) <= 0) 
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "IsStatusTableMissingAnyLuckyCharmEntity : No entity is given");
		return false;
	}

	if (pppOwner == NULL || eaSize(pppOwner) <= 0) 
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "IsStatusTableMissingAnyLuckyCharmEntity : No owner is given");
		return false;
	}

	// Get the entity
	pEnt = (*peaEnts)[0];

	// Get the owner
	pOwner = (*pppOwner)[0];

	if (pEnt == NULL || pOwner == NULL)
		return false;

	if ( team_IsMember(pOwner) ) //if the player is on a team, search the per-team list
	{
		TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapState_FromPartitionIdx(iPartitionIdx),pOwner->pTeam->iTeamID);

		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				if (pTeamMapValues->eaPetTargetingInfo[i]->eType == eLuckyCharmType)
				{
					Entity* pMatchingEnt = entFromEntityRef(iPartitionIdx, pTeamMapValues->eaPetTargetingInfo[i]->erTarget);

					if (pMatchingEnt == NULL)
						continue;
					else
					{
						// See if this guy exists in the status table
						if (!aiStatusFind(pEnt, pEnt->aibase, pMatchingEnt, false))
						{
							return true;
						}

						if (bCheckFirstOnly)
						{
							return false;
						}
					}
				}
			}
		}
	}
	else //if the player is not on team, search the per-player list
	{
		PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pOwner));

		if ( pPlayerMapValues )
		{
			for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
			{
				if (pPlayerMapValues->eaPetTargetingInfo[i]->eType == eLuckyCharmType)
				{
					Entity* pMatchingEnt = entFromEntityRef(iPartitionIdx, pPlayerMapValues->eaPetTargetingInfo[i]->erTarget);

					if (pMatchingEnt == NULL)
						continue;
					else
					{
						// See if this guy exists in the status table
						AIStatusTableEntry *pStatus = aiStatusFind(pEnt, pEnt->aibase, pMatchingEnt, false);
						AITeamStatusEntry *teamStatus;

						if (pStatus == NULL || !pStatus->visible)
						{
							return true;
						}

						teamStatus = aiGetTeamStatus(pEnt, pEnt->aibase, pStatus);
						if(!teamStatus || !teamStatus->legalTarget)
						{
							return true;
						}

						if (bCheckFirstOnly)
						{
							return false;
						}
					}


				}
			}
		}
	}

	return false;
}


// Determine if a single entity is in combat
// Only checks first entity in ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntityIsInCombat);
U32 exprFuncEntityIsInCombat(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts)
{
	if (peaEnts && eaSize(peaEnts)) {
		Entity *pEnt = (*peaEnts)[0];
		if (pEnt->pChar) {
			return character_HasMode(pEnt->pChar,kPowerMode_Combat);
		} else {
			return false;
		}
	}

	ErrorFilenamef(exprContextGetBlameFile(pContext), "EntityIsInCombat : No entities given");
	return 0;
}


AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerIsInCombat);
int exprFuncPlayerIsInCombat(ExprContext* context)
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if (pPlayerEnt)
	{
		if (pPlayerEnt->pChar) {
			return character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat);
		} else {
			return false;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerIsAvailable);
U32 exprFuncPlayerIsAvailable(ACMD_EXPR_ENTARRAY_IN peaEnts)
{
	if (peaEnts && eaSize(peaEnts)) {
		Entity *pEnt = (*peaEnts)[0];

		return (pEnt && pEnt->pChar && !pEnt->pChar->uiTimeCombatExit &&
			!interaction_IsPlayerInteracting(pEnt) &&
			!interaction_IsPlayerInDialog(pEnt));
	}

	return 0;
}

// Gets the Gang ID of ent array passed in.
// Only checks first entity in ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetGang);
ExprFuncReturnVal exprFuncGetGang(ExprContext *pContext, ACMD_EXPR_INT_OUT piOutInt, ACMD_EXPR_ENTARRAY_IN peaEntsIn, ACMD_EXPR_ERRSTRING errString)
{
	int iNum = eaSize(peaEntsIn);
	Entity* pEnt;

	if (iNum == 1) {
		pEnt = (*peaEntsIn)[0];

		if (pEnt->pChar) {
			*piOutInt = pEnt->pChar->gangID;
		} else {
			*piOutInt = -1;
		}

		return ExprFuncReturnFinished;
	} else {
		estrPrintf(errString, "GetGang only handles one entity at a time; an array of %d was passed in.", iNum);
		return ExprFuncReturnError;
	}
}


// Sets the Gang ID of all entities in the ent array passed in to <gang>
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SetGang);
void exprFuncSetGang(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEntsIn, int iGang)
{
	int i;
	for(i=eaSize(peaEntsIn)-1; i>=0; --i) {
		Entity *pEnt = (*peaEntsIn)[i];

		if (pEnt->pChar) {
			pEnt->pChar->gangID = iGang;
			entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);
		}
	}
}


AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(IsMapOwner);
ExprFuncReturnVal exprFuncIsMapOwner(ExprContext *pContext, ACMD_EXPR_INT_OUT pbRet, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ERRSTRING errString)
{
	Entity *pEnt;

	(*pbRet) = false;

	if (eaSize(peaEnts)>1) {
		estrPrintf(errString, "Too many entities passed in: %d (1 allowed)", eaSize(peaEnts));
		return ExprFuncReturnError;
	} else if (eaSize(peaEnts) == 0) {
		// This is fine; return false
		return ExprFuncReturnFinished;
	}

	devassert((*peaEnts)); // Fool static check
	pEnt = (*peaEnts)[0];

	if (pEnt) {
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		if (partition_OwnerIDFromIdx(iPartitionIdx) && (entGetContainerID(pEnt) == partition_OwnerIDFromIdx(iPartitionIdx))) {
			*pbRet = true;
			return ExprFuncReturnFinished;
		}
	}

	return ExprFuncReturnFinished;
}


// ----------------------------------------------------------------------------------
// Getting Entities
// ----------------------------------------------------------------------------------

// Get an entarray of all entities close to a named point
static ExprFuncReturnVal exprFuncGetEntsWithinDistOfPointHelper(ExprContext *pContext,
																ACMD_EXPR_PARTITION partition,
														 Mat4 mPoint, F32 fDist, ACMD_EXPR_ENTARRAY_OUT peaEntsOut,
														 int bAll, int bDead)
{
	static Entity **eaEnts = NULL;
	int i;
	entGridProximityLookupExEArray(partition, mPoint[3], &eaEnts, fDist, 0, 0, NULL);

	for(i=eaSize(&eaEnts)-1; i>=0; --i) {
		Entity *pEnt = eaEnts[i];
		if (pEnt && 
			(bDead || entIsAlive(pEnt)) &&
			(bAll || !exprFuncHelperShouldExcludeFromEntArray(pEnt))) {
			eaPush(peaEntsOut, pEnt);
		}
	}

	return ExprFuncReturnFinished;
}


// Get an entarray of all entities close to a named point
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetEntsWithinDistOfPoint);
ExprFuncReturnVal exprFuncGetEntsWithinDistOfPoint(ExprContext *pContext, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_LOC_MAT4_IN mPoint, F32 fDist)
{
	return exprFuncGetEntsWithinDistOfPointHelper(pContext, partition, mPoint, fDist, peaEntsOut, 0, 0);
};


// Get an entarray of all entities close to a named point
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetEntsWithinDistOfPointAll);
ExprFuncReturnVal exprFuncGetEntsWithinDistOfPointAll(ExprContext *pContext, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_LOC_MAT4_IN mPoint, F32 fDist)
{
	return exprFuncGetEntsWithinDistOfPointHelper(pContext, partition, mPoint, fDist, peaEntsOut, 1, 0);
};


// Get an entarray of all entities close to a named point
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetEntsWithinDistOfPointDeadAll);
ExprFuncReturnVal exprFuncGetEntsWithinDistOfPointDeadAll(ExprContext *pContext, ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_LOC_MAT4_IN mPoint, F32 fDist)
{
	return exprFuncGetEntsWithinDistOfPointHelper(pContext, partition, mPoint, fDist, peaEntsOut, 1, 1);
};


// Forces the given entities into a current build by index
AUTO_EXPR_FUNC(ai, encounter_action); 
ExprFuncReturnVal EntityBuildForceCurrentBuild(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN entsIn, S32 buildIndex)
{
	S32 i;
	if(!eaSize(entsIn))
		return ExprFuncReturnFinished;

	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		entity_BuildSetCurrent((*entsIn)[i], buildIndex, false);
	}

	return ExprFuncReturnFinished;
}

// Returns the current build index of the given entity,
// entsIn must only be 1 entity, 
// returns -1 on error or no build 
AUTO_EXPR_FUNC(ai, encounter_action); 
S32 EntityBuildGetCurrentBuildIndex(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN entsIn)
{
	S32 size = eaSize(entsIn);
	if(!size || size != 1)
	{
		// print out error, 
		return -1;
	}
	
	return entity_BuildGetCurrentIndex((*entsIn)[0]);
}


// ----------------------------------------------------------------------------------
// Game Account Tests
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerIsLifetimeSubscription);
int player_FuncPlayerIsLifetimeSubscription(ExprContext *pContext)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	return(entity_LifetimeSubscription(pPlayerEnt));
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerGetDaysSubscribed);
U32 player_FuncPlayerGetDaysSubscribed(ExprContext *pContext)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	return entity_GetDaysSubscribed(pPlayerEnt);
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerIsPressSubscription);
int player_FuncPlayerIsPressSubscription(ExprContext *pContext)
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	const GameAccountData *pData = entity_GetGameAccount(pPlayerEnt);
	if (pPlayerEnt && pData) {
		return(pData->bPress);
	}
	return 0;
}

// In order to get out quickly is hard coded for now. Will need to be changed to config soon (todays date 2011-04-29)
#define WEB_GAME_EVENT_TEST_URL "http://qa.fightclub/adminfunctions/trigger-game-event"
#define WEB_GAME_EVENT_FC_URL "http://www.champions-online.com/adminfunctions/trigger-game-event"
#define WEB_GAME_EVENT_ERROR_TIME_SECONDS 15

typedef struct WebGameEventCallBackData
{
	char *eaUrl;

} WebGameEventCallBackData;

void Player_WebGameEventCB(Entity *pEntity, const char *response, int len, int response_code, void *pUserData)
{
	static U32 uLastTime = 0;
	U32 tm = timeSecondsSince2000();
	// Just here to free callbak data
	// we might need to if some of these are actual rejects ...
	WebGameEventCallBackData *pData = NULL;
	if(pUserData)
	{
		pData = (WebGameEventCallBackData *)pUserData;
	}

	// Note the hard coded check here ... no idea what response_code is supposed to be.
	if(response && stricmp("Success.", response) != 0)
	{
		uLastTime = tm + WEB_GAME_EVENT_ERROR_TIME_SECONDS;
		if(tm >= uLastTime)
		{
			if(pEntity && pData && pData->eaUrl)
			{
				char *eaMsg = NULL;

				estrPrintf(&eaMsg, "Failure with Entity %s, URL %s, response %s.", pEntity->debugName, pData->eaUrl, response);
				if(eaMsg)
				{
					ErrorDetailsf("%s",eaMsg);
					Errorf("WebGameEventCB error");
				}
				estrDestroy(&eaMsg);

				// add alert?
			}
			else
			{
				ErrorDetailsf("Failure and no entity or userdata was bad.");
				Errorf("WebGameEventCB error");
				// add alert?
			}
		}
	}

	if(pData)
	{
		estrDestroy(&pData->eaUrl);
		free(pData);
	}

}

void Player_WebGameEventTimeoutCB(Entity *pEntity, void *pUserData)
{
	static U32 uLastTime = 0;
	U32 tm = timeSecondsSince2000();

	WebGameEventCallBackData *pData = NULL;
	if(pUserData)
	{
		pData = (WebGameEventCallBackData *)pUserData;
	}

	if(tm >= uLastTime)
	{
		uLastTime = tm + WEB_GAME_EVENT_ERROR_TIME_SECONDS;
		if(pEntity && pData && pData->eaUrl)
		{
			char *eaMsg = NULL;

			estrPrintf(&eaMsg, "WebGameEventTimeoutCB, Entity %s, URL %s", pEntity->debugName, pData->eaUrl);
			if(eaMsg)
			{
				ErrorDetailsf("%s",eaMsg);
				Errorf("WebGameEventTimeoutCB error");
			}
			estrDestroy(&eaMsg);

			// add alert?
		}
		else
		{
			ErrorDetailsf("Failure and no entity or userdata was bad.");
			Errorf("WebGameEventTimeoutCB error");
			// add alert?
		}
	}

	if(pData)
	{
		estrDestroy(&pData->eaUrl);
		free(pData);
	}
}

static bool player_SendWebGameEventInternal(Entity *pEntity, const char *pcEventName, const char* pcURL)
{
	if (sbSendWebGameEventsToWebSrv)
	{
		AccountProxyWebSrvGameEvent gameEvent = {0};

		gameEvent.pProductShortName = StructAllocString(GetShortProductName());
		gameEvent.pEventName = StructAllocString(pcEventName);
		gameEvent.keyValueList = StructCreate(parse_WebSrvKeyValueList);
		gameEvent.uAccountID = entGetAccountID(pEntity);
		websrvKVList_Add(gameEvent.keyValueList, "account", pEntity->pPlayer->publicAccountName);
		websrvKVList_Add(gameEvent.keyValueList, "character", pEntity->pSaved->savedName);
		websrvKVList_Add(gameEvent.keyValueList, "shard", GetShardNameFromShardInfoString());
		if(pEntity->pPlayer && locGetCode(locGetIDByLanguage(pEntity->pPlayer->langID)))
		{
			gameEvent.pLang = StructAllocString(locGetCode(locGetIDByLanguage(pEntity->pPlayer->langID)));
		}
		else	
		{
			// default is English
			gameEvent.pLang = StructAllocString(locGetCode(locGetIDByLanguage(LANGUAGE_DEFAULT)));
		}

		RemoteCommand_aslAPCmdWebSrvGameEventRequest(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, &gameEvent);
		StructDeInit(parse_AccountProxyWebSrvGameEvent, &gameEvent);
		return true;
	}
	else
	{
		if(pcURL)
		{
			UrlArgumentList *args;
			WebGameEventCallBackData *pData = (WebGameEventCallBackData *)calloc(1, sizeof(WebGameEventCallBackData));

			const char *pShardName = GetShardNameFromShardInfoString();

			args = urlToUrlArgumentList(pcURL);

			urlAddValue(args, "account", pEntity->pPlayer->publicAccountName, HTTPMETHOD_GET);
			urlAddValue(args, "character", pEntity->pSaved->savedName, HTTPMETHOD_GET);
			urlAddValue(args, "event", pcEventName, HTTPMETHOD_GET);
			urlAddValue(args, "shard", GetShardNameFromShardInfoString(), HTTPMETHOD_GET);
			if(pEntity->pPlayer && locGetCode(locGetIDByLanguage(pEntity->pPlayer->langID)))
			{
				urlAddValue(args, "lang", locGetCode(locGetIDByLanguage(pEntity->pPlayer->langID)) , HTTPMETHOD_GET);
			}
			else
			{
				// default is English
				urlAddValue(args, "lang", locGetCode(locGetIDByLanguage(LANGUAGE_DEFAULT)) , HTTPMETHOD_GET);
			}

			estrPrintf(&pData->eaUrl, "%s", pcURL);

			gslhaRequest(pEntity, args, Player_WebGameEventCB, Player_WebGameEventTimeoutCB, 0, pData);
			return true;
		}
	}
	return false;
}

const char *GetWebGameEventURL(void)
{
	const char *pcURL = NULL;
	const char *pShardName = GetShardNameFromShardInfoString();
	S32 i;

	// try to find a URL for this shard
	for(i = 0; i < eaSize(&gProjectGameServerConfig.eaWebGameEventData); ++i)
	{
		if(gProjectGameServerConfig.eaWebGameEventData[i]->pcShardName && gProjectGameServerConfig.eaWebGameEventData[i]->pcURL)
		{
			if(stricmp(pShardName, gProjectGameServerConfig.eaWebGameEventData[i]->pcShardName) == 0)
			{
				pcURL = gProjectGameServerConfig.eaWebGameEventData[i]->pcURL;
				break;
			}
		}
	}

	// Defaults if pcURL is NULL
	if(pcURL == NULL)
	{
		if(stricmp(pShardName, "Live") == 0)
		{
			pcURL = WEB_GAME_EVENT_FC_URL;
		}
		else
		{
			pcURL = WEB_GAME_EVENT_TEST_URL;
		}
	}

	return pcURL;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(SendWebGameEvent);
void exprPlayer_SendWebGameEvent(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID char *pcEventName)
{
	if(pEntity && pEntity->pPlayer && pEntity->pSaved && pcEventName && pcEventName[0])
	{
		const char *pcURL = GetWebGameEventURL();
		player_SendWebGameEventInternal(pEntity, pcEventName, pcURL);
	}
}


// Test function for web game event, please use expression above for real use
AUTO_COMMAND ACMD_NAME(TestWebGameEvent) ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD;
void player_WebGameEvent(Entity *pPlayerEnt, char *pcEventName)
{
	if (pPlayerEnt && pPlayerEnt->pChar)
	{
		exprPlayer_SendWebGameEvent(pPlayerEnt,pcEventName);
	}
}

// Test function for web game event with url that is typed in
AUTO_COMMAND ACMD_NAME(TestWebGameEventUrl) ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD;
void player_WebGameEventUrl(Entity *pPlayerEnt, char *pcEventName, char *pcUrl)
{
	if(pPlayerEnt && pPlayerEnt->pChar && pcEventName && pcUrl)
	{
		player_SendWebGameEventInternal(pPlayerEnt, pcEventName, pcUrl);
	}
}
