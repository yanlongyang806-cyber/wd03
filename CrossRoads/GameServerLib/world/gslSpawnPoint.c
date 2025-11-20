/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiStruct.h"
#include "beacon.h"
#include "Character_target.h"
#include "DoorTransitionCommon.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntityMovementManager.h"
#include "EntityMovementDefault.h"
#include "EntitySavedData.h"
#include "EntityIterator.h"
#include "Expression.h"
#include "GameServerLib.h"
#include "gslDoorTransition.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventTracker.h"
#include "LoggedTransactions.h"
#include "gslMapState.h"
#include "gslMapTransfer.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslMission.h"
#include "gslPartition.h"
#include "gslQueue.h"
#include "gslSavedPet.h"
#include "gslSendToClient.h"
#include "gslSpawnPoint.h"
#include "gslTransactions.h"
#include "gslVolume.h"
#include "gslWaypoint.h"
#include "gslWorldVariable.h"
#include "logging.h"
#include "mathutil.h"
#include "MicroTransactions.h"
#include "MicroTransactions_h_ast.h"
#include "mission_common.h"
#include "NotifyCommon.h"
#include "objTransactions.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "queue_common.h"
#include "queue_common_structs.h"
#include "rand.h"
#include "RegionRules.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "Team.h"
#include "UGCProjectUtils.h"
#include "wlEncounter.h"
#include "wlVolumes.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "GameAccountDataCommon.h"
#include "Character.h"
#include "SuperCritterPet.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/gslSpawnpoint_h_ast.h"
#include "AutoGen/gslSpawnpoint_h_ast.c"
#include "Player_h_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"


// ----------------------------------------------------------------------------------
// Definitions
// ----------------------------------------------------------------------------------

#define NEW_RESPAWN_KEY "MechanicsUI.NewRespawnFound"

#define SPAWN_SNAP_TO_DIST   7


static bool spawnpoint_HasPlayerActivatedSpawn(Entity *pPlayerEnt, const char *pcSpawnName, const char *pcSpawnMap);


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

static GameSpawnPoint **s_eaSpawnPoints = NULL;

static U32 s_eSpawnPointVolumeType = 0;

static ExprContext *s_pSpawnPointContext = NULL;


// ----------------------------------------------------------------------------------
// Sole interfaces to searching s_eaSpawnPoints.  No other function
// than spawnpoint_GetByName, spawnpoint_GetByNameForSpawning and
// spawnpoint_GetByEntry should be searching s_eaSpawnPoints.
// ----------------------------------------------------------------------------------
GameSpawnPoint *spawnpoint_GetByName(const char *pcSpawnPointName, const WorldScope *pScope)
{
	if (pScope && gUseScopedExpr) {
		WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcSpawnPointName);

		if (pObject && pObject->type == WL_ENC_SPAWN_POINT) {
			WorldSpawnPoint *pNamedSpawnPoint = (WorldSpawnPoint *)pObject;
			GameSpawnPoint *pGameSpawnPoint = spawnpoint_GetByEntry(pNamedSpawnPoint);
			if (pGameSpawnPoint) {
				return pGameSpawnPoint;
			}
		}
	} else {
		int i;
		for(i=eaSize(&s_eaSpawnPoints)-1; i>=0; --i) {
			if (stricmp(pcSpawnPointName, s_eaSpawnPoints[i]->pcName) == 0) {
				return s_eaSpawnPoints[i];
			}
		}
	}
	
	return NULL;
}


GameSpawnPoint *spawnpoint_GetByNameForSpawning(const char *pcSpawnName, const WorldScope *pScope)
{
	GameSpawnPoint *pGamePoint = spawnpoint_GetByName(pcSpawnName, pScope);
	
	if (pGamePoint) {
		// The spawn point was found, so return it
		return pGamePoint;
	} else {
		// The spawn point wasn't found, so we need to check if there is a logical
		// group by this name, and if so, select a random spawn point from that
		// logical group
		WorldEncounterObject *pObject;
		WorldLogicalGroup *pGroup;
		S32 i, iNumObjects;
		GameSpawnPoint **eaSpawnPointOptions = NULL;
		
		if (!pScope) {
			pScope = (WorldScope*)zmapGetScope(NULL);
			if (!pScope) {
				return NULL;
			}
		}
		
		pObject = worldScopeGetObject(pScope, pcSpawnName);
		if (!pObject || pObject->type != WL_ENC_LOGICAL_GROUP) {
			return NULL;
		}
		
		pGroup = (WorldLogicalGroup *)pObject;
		iNumObjects = eaSize(&pGroup->objects);
		
		// Find all spawn points in the group and add them to an earray
		for (i = 0; i < iNumObjects; i++) {
			if (pGroup->objects[i]->type == WL_ENC_SPAWN_POINT) {
				GameSpawnPoint *pCurrentPoint = spawnpoint_GetByEntry((WorldSpawnPoint*)pGroup->objects[i]);
				if (pCurrentPoint){
					eaPush(&eaSpawnPointOptions, pCurrentPoint);
				}
			}
		}
		
		// Select a random spawn point from the earray
		pGamePoint = eaRandChoice(&eaSpawnPointOptions);
		
		eaDestroy(&eaSpawnPointOptions);
		return pGamePoint;
	}
}


GameSpawnPoint *spawnpoint_GetByEntry(WorldSpawnPoint *pSpawnPoint)
{
	int i;

	for(i=eaSize(&s_eaSpawnPoints)-1; i>=0; --i) {
		if (SAFE_MEMBER(s_eaSpawnPoints[i], pWorldPoint) == pSpawnPoint) {
			return s_eaSpawnPoints[i];
		}
	}
	
	return NULL;
}

#define FOR_EACH_SPAWN_POINT(it) { int i##it##Index; for(i##it##Index=eaSize(&s_eaSpawnPoints)-1; i##it##Index>=0; --i##it##Index) { GameSpawnPoint *it = s_eaSpawnPoints[i##it##Index];
#define FOR_EACH_SPAWN_POINT2(outerIt, it) { int i##it##Index; for(i##it##Index=i##outerIt##Index-1; i##it##Index>=0; --i##it##Index) { GameSpawnPoint *it = s_eaSpawnPoints[i##it##Index];
// ----------------------------------------------------------------------------------
// End of sole interfaces to searching s_eaSpawnPoints.
// ----------------------------------------------------------------------------------


// ----------------------------------------------------------------------------------
// Player Spawn Activation transaction
// ----------------------------------------------------------------------------------


static void spawnpoint_NullTransRetValCB(TransactionReturnVal *pReturn, void *udata)
{
	// Do nothing
}


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Eapersistactivatedspawns");
enumTransactionOutcome tr_SpawnPointActivated(ATR_ARGS, NOCONST(Entity) *pEnt, const char *pcSpawnName, const char *pcMapName)
{
	if (ISNULL(pEnt->pPlayer)) {
		TRANSACTION_RETURN_LOG_FAILURE("Tried to activate spawn point for non-player");

	} else if (!pcSpawnName){
		TRANSACTION_RETURN_LOG_FAILURE("Tried to activate unnamed spawn point");

	} else {
		NOCONST(ActivatedPlayerSpawn) *pSpawn = StructCreateNoConst(parse_ActivatedPlayerSpawn);
		pSpawn->spawnPointName = StructAllocString(pcSpawnName);
		pSpawn->mapName = StructAllocString(pcMapName);
		eaPush(&pEnt->pPlayer->eaPersistActivatedSpawns, pSpawn);

		if (pcMapName) {
			TRANSACTION_RETURN_LOG_SUCCESS("Added spawn point %s on map %s to list of activated spawns", pcSpawnName, pcMapName);
		} else {
			TRANSACTION_RETURN_LOG_SUCCESS("Added spawn point %s to list of activated spawns", pcSpawnName);
		}
	}
}


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Eapersistactivatedspawns");
enumTransactionOutcome tr_SpawnActivatedReset(ATR_ARGS, NOCONST(Entity) *pEnt)
{
	if (ISNULL(pEnt->pPlayer)) {
		TRANSACTION_RETURN_LOG_FAILURE("Tried to reset spawn point activations for non-player");
	} else {
		eaClearStructNoConst(&pEnt->pPlayer->eaPersistActivatedSpawns, parse_ActivatedPlayerSpawn);
		TRANSACTION_RETURN_LOG_SUCCESS("Reset spawn point activations");
	}
}


// ----------------------------------------------------------------------------------
// Player Spawn Point Logic
// ----------------------------------------------------------------------------------

static bool spawnpoint_HasPlayerActivatedSpawn(Entity *pPlayerEnt, const char *pcSpawnName, const char *pcSpawnMap)
{
	// Note: Map name is passed in, but it is assumed by this code to always be the current map.
	//       It is only passed in because the parent function always has the info so why look it up again.

	PERFINFO_AUTO_START_FUNC();

	if (pPlayerEnt->pPlayer) {
		GameSpawnPoint *pGamePoint = spawnpoint_GetByName(pcSpawnName, NULL);
		int i;
		for(i=eaSize(&pPlayerEnt->pPlayer->eaPersistActivatedSpawns)-1; i>=0; --i) {
			ActivatedPlayerSpawn *pSpawn = pPlayerEnt->pPlayer->eaPersistActivatedSpawns[i];
			if ((stricmp(pSpawn->mapName, pcSpawnMap) == 0) && (stricmp(pSpawn->spawnPointName, pcSpawnName) == 0)) {
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
		if (pGamePoint) {
			for(i=eaiSize(&pGamePoint->eaiPlayerUnlocks)-1; i>=0; --i) {
				ContainerID id = pGamePoint->eaiPlayerUnlocks[i];
				if (id == pPlayerEnt->myContainerID) {
					PERFINFO_AUTO_STOP();
					return true;
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
	return false;
}


static void spawnpoint_VolumeEnterCB(SA_PARAM_NN_VALID WorldVolume *pVolume, SA_PARAM_NN_VALID WorldVolumeQueryCache *pQueryCache)
{
	Entity *pEnt;
	GameSpawnPoint *pGamePoint;

	PERFINFO_AUTO_START_FUNC();

	pEnt = wlVolumeQueryCacheGetData(pQueryCache);
	pGamePoint = wlVolumeGetVolumeData(pVolume);

	if (pEnt && pEnt->pPlayer && pGamePoint) {
		const char *pcMapName = zmapInfoGetPublicName(NULL);

		ANALYSIS_ASSUME(pEnt != NULL);
		// If the player has never visited this spawn before, record it
		if (pGamePoint->pcName && pcMapName && !spawnpoint_HasPlayerActivatedSpawn(pEnt, pGamePoint->pcName, pcMapName)) {
			const char *pcText = langTranslateMessageKey(entGetLanguage(pEnt),  NEW_RESPAWN_KEY);
			WorldSpawnProperties *pProperties = SAFE_MEMBER2(pGamePoint, pWorldPoint, properties);

			// Display a message -- only if there's no associated condition
			if (!pProperties || !pProperties->active_cond){
				ClientCmd_NotifySend(pEnt, kNotifyType_RespawnUnlocked, pcText, NULL, NULL);
			}

			if (zmapInfoGetMapType(NULL) == ZMTYPE_STATIC) {
				TransactionReturnVal * pRetVal = LoggedTransactions_CreateManagedReturnVal("tr_SpawnPointActivated", spawnpoint_NullTransRetValCB, NULL);
				// PERSIST: Record that the player has activated this respawn point
				AutoTrans_tr_SpawnPointActivated(pRetVal, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pGamePoint->pcName, pcMapName);
			} else {
				// NO-PERSIST: Record that the player has activated this respawn point
				eaiPushUnique(&pGamePoint->eaiPlayerUnlocks, pEnt->myContainerID);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


static bool spawnpoint_ZoneMapRequires_Eval(ZoneMapInfo *pInfo, Entity *pEnt)
{
	Expression *pExpr = zmapInfoGetRequiresExpr(pInfo);
	if (pExpr && entGetAccessLevel(pEnt) < ACCESS_GM) {
		MultiVal mvResult = {0};
		ExprContext *pContext = zmapGetExprContext();

		//Set up context pointers
		exprContextSetSelfPtr(pContext, pEnt);
		exprContextSetPartition(pContext, entGetPartitionIdx(pEnt));
		exprContextSetPointerVar(pContext, "Player", pEnt, parse_Entity, false, true);

		exprEvaluate(pExpr, pContext, &mvResult);
		return MultiValGetInt(&mvResult, NULL) != 0;
	}
	return true;
}

static bool spawnpoint_ZoneMapRequires_Eval_Team(ZoneMapInfo *pInfo, Entity *pEnt, Team *pTeam)
{
	if (pInfo && pEnt && pTeam) {
		int i, iSize = eaSize(&pTeam->eaMembers);
		int iPartitionIdx = entGetPartitionIdx(pEnt);

		for (i = 0; i < iSize; i++) {
			Entity* pEntity = entFromContainerID( iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);
			if (pEntity && !spawnpoint_ZoneMapRequires_Eval(pInfo, pEntity)) {
				return false;
			}
		}
	}
	return true;
}

static bool spawnpoint_ZoneMap_EvalPermission(ZoneMapInfo *pInfo, Entity *pEnt)
{
	Expression *pExpr = zmapInfoGetPermissionExpr(pInfo);
	//If there's an expression and the entity doesn't have Dev/GM access
	if (pExpr && entGetAccessLevel(pEnt) < ACCESS_GM)
	{
		MultiVal mvResult = {0};
		ExprContext *pContext = zmapGetExprContext();

		//Set up context pointers
		exprContextSetSelfPtr(pContext, pEnt);
		exprContextSetPartition(pContext, entGetPartitionIdx(pEnt));
		exprContextSetPointerVar(pContext, "Player", pEnt, parse_Entity, false, true);

		exprEvaluate(pExpr, pContext, &mvResult);
		return MultiValGetInt(&mvResult, NULL) != 0;
	}
	return true;
}


static void spawnpoint_MovePlayerToMapAndSpawnEx(Entity *pEnt, const char *pcMapName, const char *pcNamedSpawnPoint, 
												 const char *pcQueueName, GlobalType eOwnerType, ContainerID uOwnerID, ContainerID uMapID, U32 uPartitionID,
												 WorldVariable **eaVariables, const WorldScope *pScope,
												 ZoneMapInfo* pNextZoneMap,
												 DoorTransitionSequenceDef* pTransOverride,
												 U32 eFlags,
												 bool bIncludeTeammates)
{
	RegionRules *pCurrRules = getRegionRulesFromEnt(pEnt);
	const char *pcCurrentMap = zmapInfoGetPublicName(NULL);
	ZoneMapInfo* pCurrZoneMap = worldGetZoneMapByPublicName(pcCurrentMap);
	WorldVariable **eaNormalizedVariables = NULL;
	const char* pchNormalizedMapVars;
	RegionRules *pNextRules = pNextZoneMap ? getRegionRulesFromZoneMap(pNextZoneMap) : NULL;
	S32 bConfirmSuccess = false;
	
	if (!pcNamedSpawnPoint || !(*pcNamedSpawnPoint) || !stricmp(pcNamedSpawnPoint, START_SPAWN)) {
		pcNamedSpawnPoint = allocAddString(START_SPAWN);
	}

	
	if (pNextZoneMap) {
		if (!spawnpoint_ZoneMapRequires_Eval(pNextZoneMap, pEnt)) {
			ErrorDetailsf("Destination Map %s, Source Map %s, Player Name %s, Player Container ID %d", 
				pcMapName, pcCurrentMap, pEnt->debugName, entGetContainerID(pEnt));
			Errorf("ZoneMap Requires Expression not satisfied when attempting to transfer to map");
			return;
		}
		// Normalize variables if have a target zone map
		gslWorldVariableNormalizeVariables(pNextZoneMap, eaVariables, &eaNormalizedVariables);
	} else {
		Errorf("Cannot transfer to unknown map '%s'\n", pcMapName);
		return;
	}

	pchNormalizedMapVars = worldVariableArrayToString(eaNormalizedVariables);

	//If the next zone has a permission expression and you've failed it
	if (zmapInfoGetPermissionExpr(pNextZoneMap)) {
		// Handle confirm of permissions
		if(	spawnpoint_ZoneMap_EvalPermission(pNextZoneMap, pEnt)) {
			bConfirmSuccess = true;
		} else {
			PlayerMapMoveConfirm *pConfirm = StructCreate(parse_PlayerMapMoveConfirm);
			pConfirm->eType = kPlayerMapMove_Permission;
			pConfirm->pcMapName = StructAllocString(pcMapName);

			if(!pEnt->pPlayer->pWarp) {
				pConfirm->pcNamedSpawnPoint = StructAllocString(pcNamedSpawnPoint);
			} else {
				pConfirm->pWarp = StructClone(parse_PlayerWarpToData, pEnt->pPlayer->pWarp);
				//If this is a warp, take the warp's spawn
				pConfirm->pcNamedSpawnPoint = StructAllocString(pEnt->pPlayer->pWarp->pchSpawn);
			}

			pConfirm->pcQueueName = StructAllocString(pcQueueName);
			pConfirm->eOwnerType = eOwnerType;
			pConfirm->uOwnerID = uOwnerID;
			eaCopyStructs(&eaVariables,&pConfirm->eaVariables,parse_WorldVariable);
			pConfirm->pScope = StructClone(parse_WorldScope,pScope);
			if(pEnt->pPlayer->pMapMoveConfirm) {
				pConfirm->uiTimeStart = pEnt->pPlayer->pMapMoveConfirm->uiTimeStart;
				pConfirm->uiTimeToConfirm = pEnt->pPlayer->pMapMoveConfirm->uiTimeToConfirm;
			}

			//Steal the warp
			if(pEnt->pPlayer->pWarp) {
				pConfirm->pWarp = pEnt->pPlayer->pWarp;
				pEnt->pPlayer->pWarp = NULL;
			}

			StructDestroySafe(parse_PlayerMapMoveConfirm,&pEnt->pPlayer->pMapMoveConfirm);
			pEnt->pPlayer->pMapMoveConfirm = pConfirm;
		}
	} else if(pCurrZoneMap && zmapInfoConfirmPurchasesOnExit(pCurrZoneMap) 
		&& pNextZoneMap && !zmapInfoConfirmPurchasesOnExit(pNextZoneMap)) {
		
		// Handle confirmation of purchases

		// Make a confirmation structure using the given parameters
		PlayerMapMoveConfirm *pConfirm = StructCreate(parse_PlayerMapMoveConfirm);
		pConfirm->eType = kPlayerMapMove_PowerPurchase;
		pConfirm->pcMapName = StructAllocString(pcMapName);
		
		if(!pEnt->pPlayer->pWarp) {
			pConfirm->pcNamedSpawnPoint = StructAllocString(pcNamedSpawnPoint);
		} else {
			pConfirm->pWarp = StructClone(parse_PlayerWarpToData, pEnt->pPlayer->pWarp);
			//If this is a warp, take the warp's spawn
			pConfirm->pcNamedSpawnPoint = StructAllocString(pEnt->pPlayer->pWarp->pchSpawn);
		}
		
		pConfirm->pcQueueName = StructAllocString(pcQueueName);
		pConfirm->eOwnerType = eOwnerType;
		pConfirm->uOwnerID = uOwnerID;
		eaCopyStructs(&eaVariables,&pConfirm->eaVariables,parse_WorldVariable);
		pConfirm->pScope = StructClone(parse_WorldScope,pScope);
		pConfirm->pWarp = StructClone(parse_PlayerWarpToData, pEnt->pPlayer->pWarp);
		if(pEnt->pPlayer->pMapMoveConfirm) {
			pConfirm->uiTimeStart = pEnt->pPlayer->pMapMoveConfirm->uiTimeStart;
			pConfirm->uiTimeToConfirm = pEnt->pPlayer->pMapMoveConfirm->uiTimeToConfirm;
		}

		if(pEnt->pPlayer->pMapMoveConfirm
			&& pEnt->pPlayer->pMapMoveConfirm->bConfirmed
			&& !StructCompare(parse_PlayerMapMoveConfirm,pConfirm,pEnt->pPlayer->pMapMoveConfirm,0,0,0)) {
			
			// Has a pending confirm, which has been confirmed, and matches this mapmove request, so everything
			//  is fine - destroy the confirm, do any necessary updates, and then continue
			bConfirmSuccess = true;
			StructDestroy(parse_PlayerMapMoveConfirm,pConfirm);

			// In theory we might have several reasons for confirming an exit, but right now the
			//  only one is confirming purchases, so handle that now.
			AutoTrans_trCharacter_UpdatePointsSpentPowerTrees(LoggedTransactions_MakeEntReturnVal("UpdatePointsSpentPowerTrees", pEnt), GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt));
		} else {
			// Either they have no pending confirm, haven't confirmed the one they had, or this mapmove is
			//  different... in any case, destroy the old one, save the new one
			StructDestroySafe(parse_PlayerMapMoveConfirm,&pEnt->pPlayer->pMapMoveConfirm);
			pEnt->pPlayer->pMapMoveConfirm = pConfirm;

			//NULL this out since it's on the confirmation now
			StructDestroySafe(parse_PlayerWarpToData, &pEnt->pPlayer->pWarp);
		}
	} else if(pEnt->pPlayer->pMapMoveConfirm && pEnt->pPlayer->pMapMoveConfirm->eType == kPlayerMapMove_Warp) {
		// Handle confirmation of warping

		// Make a confirmation structure using the given parameters
		PlayerMapMoveConfirm *pConfirm = StructCreate(parse_PlayerMapMoveConfirm);
		pConfirm->eType = kPlayerMapMove_Warp;
		pConfirm->pcMapName = StructAllocString(pcMapName);
		pConfirm->pcQueueName = StructAllocString(pcQueueName);

		if(!pEnt->pPlayer->pWarp) {
			pConfirm->pcNamedSpawnPoint = StructAllocString(pcNamedSpawnPoint);
		} else {
			pConfirm->pWarp = StructClone(parse_PlayerWarpToData, pEnt->pPlayer->pWarp);
			//If this is a warp, take the warp's spawn
			pConfirm->pcNamedSpawnPoint = StructAllocString(pEnt->pPlayer->pWarp->pchSpawn);
		}
		
		pConfirm->eOwnerType = eOwnerType;
		pConfirm->uOwnerID = uOwnerID;
		eaCopyStructs(&eaVariables,&pConfirm->eaVariables,parse_WorldVariable);
		pConfirm->pScope = StructClone(parse_WorldScope,pScope);

		if(pEnt->pPlayer->pMapMoveConfirm) {
			pConfirm->uiTimeStart = pEnt->pPlayer->pMapMoveConfirm->uiTimeStart;
			pConfirm->uiTimeToConfirm = pEnt->pPlayer->pMapMoveConfirm->uiTimeToConfirm;
		}

		if(pEnt->pPlayer->pMapMoveConfirm
			&& pEnt->pPlayer->pMapMoveConfirm->bConfirmed
			&& !StructCompare(parse_PlayerMapMoveConfirm,pConfirm,pEnt->pPlayer->pMapMoveConfirm,0,0,0)) {

				// Has a pending confirm, which has been confirmed, and matches this mapmove request, so everything
				//  is fine - destroy the confirm, do any necessary updates, and then continue
				bConfirmSuccess = true;
				StructDestroy(parse_PlayerMapMoveConfirm,pConfirm);
		} else {
			// Either they have no pending confirm, haven't confirmed the one they had, or this mapmove is
			//  different... in any case, destroy the old one, save the new one
			StructDestroySafe(parse_PlayerMapMoveConfirm,&pEnt->pPlayer->pMapMoveConfirm);
			pEnt->pPlayer->pMapMoveConfirm = pConfirm;

			//NULL this out since it's on the confirmation now
			StructDestroySafe(parse_PlayerWarpToData, &pEnt->pPlayer->pWarp);
		}
	}

	// This block was originally written and used for the "confirmation of purchases" feature written by Jered W.  Subsequent versions (above) were done
	// by Ben Hanka.  I am not sure if this case is used for "kPlayerMapMove_Warp", for example, but I do know that the ClientCmd is sent in gslWarp, so it
	// would be strange if it were also done here in that case.  [RMARR - 2/1/13]
	if (pEnt->pPlayer->pMapMoveConfirm && !bConfirmSuccess) {

		//Send the confirmation information to the client
		PlayerMapMoveClient *pClientConfirm = StructCreate(parse_PlayerMapMoveClient);
		pClientConfirm->eType = pEnt->pPlayer->pMapMoveConfirm->eType;
		
		if(zmapInfoGetDisplayNameMessage(pNextZoneMap)) {
			COPY_HANDLE(pClientConfirm->hDisplayName, zmapInfoGetDisplayNameMessage(pNextZoneMap)->hMessage);
		}
		
		if(pClientConfirm->eType == kPlayerMapMove_Permission) {
			//Find the microtransaction(s) for the confirmation
			MicroTransactionDef **eaMTDefs = NULL;
			
			microtrans_FindAllMTDefsForPermissionExpr(zmapInfoGetPermissionExpr(pNextZoneMap), &eaMTDefs);
			FOR_EACH_IN_EARRAY(eaMTDefs, MicroTransactionDef, pDef) {
				MicroTransactionRef *pRef = StructCreate(parse_MicroTransactionRef);
				SET_HANDLE_FROM_STRING(g_hMicroTransDefDict, pDef->pchName, pRef->hMTDef);
				eaPush(&pClientConfirm->ppMTRefs, pRef);
			} FOR_EACH_END;

			eaDestroy(&eaMTDefs);
		}
		
		// Waiting on confirmation of some sort, send the request down
		ClientCmd_RequestMapMoveConfirm(pEnt, pClientConfirm);
		
		StructDestroy(parse_PlayerMapMoveClient, pClientConfirm);
	} else if (pcQueueName && (*pcQueueName)) {
		QueueDef *pDef = queue_DefFromName(pcQueueName);
		
		if(pDef)
		{
			QueueCannotUseReason eReason = gslEntCannotUseQueue(pEnt, pDef, false, false, false);
			if (eReason == QueueCannotUseReason_None)
			{
				gslQueue_PlayerAddQueue(pEnt, pDef);

				// We used to distinguish Champions from STO/Neverwinter here.
				//   Champions had functionality where queue doors might not exist in
				//   a players Queue UI until they clicked on the door. Both STO
				//   and Neverwinter use the "AutoJoinQueueDoors" functionality where
				//   the door acts as an automatic join to the queue. WOLF[8Feb2013]
			}
			else
			{
				gslQueue_NotifyPlayerCannotUseReason(pEnt, pDef, eReason);
			}
		}
	} else if (pNextZoneMap && pCurrZoneMap && pCurrRules && pNextRules)
	{
		ANALYSIS_ASSUME(pCurrRules != NULL && pNextRules != NULL);
		if ((gConf.bUseAwayTeams && 
			  gslTeam_IsValidAwayTeamMapTransfer(pCurrZoneMap,pNextZoneMap,pCurrRules,pNextRules)) ||
			  bIncludeTeammates || gslTeam_IsValidTeamMapTransferForNNO(pCurrZoneMap,pNextZoneMap,pCurrRules,pNextRules))
		{
			TeamMapTransferResult eMapTransferResult = gslTeam_IsMapTransferChoiceTakingPlace(pEnt,pcMapName,pcNamedSpawnPoint);

			if (eMapTransferResult == TeamMapTransferResult_SameMap) {
				if (gslTeam_AddAwayTeamMemberToMapTransfer(pEnt, bIncludeTeammates)) {
					gslTeam_AwayTeamMemberStateChangedUpdate(pEnt);
				}

			} else if (eMapTransferResult == TeamMapTransferResult_DifferentMap) {
				//TODO: this isn't handled properly
				//transfer to the next map immediately
				gslEntityPlayTransitionSequenceThenMapMoveEx(pEnt, pcMapName, zmapInfoGetMapType(pNextZoneMap), pcNamedSpawnPoint, 0, uMapID, uPartitionID, eOwnerType, uOwnerID, pchNormalizedMapVars, pCurrRules, pNextRules, pTransOverride,eFlags);
			} else if (eMapTransferResult == TeamMapTransferResult_None) {
				Team *pTeam = team_GetTeam(pEnt);

				if (pTeam && spawnpoint_ZoneMapRequires_Eval_Team(pNextZoneMap, pEnt, pTeam)) {
					int i, j, iSize = eaSize(&pTeam->eaMembers);
					int iPartitionIdx = entGetPartitionIdx(pEnt);

					for (i = 0; i < iSize; i++) {
						if (stricmp(pTeam->eaMembers[i]->pcMapName, pcMapName) == 0) {
							if (bIncludeTeammates)
							{
								gslTeam_CreateAwayTeamMapTransferData(pEnt, pcMapName, pcNamedSpawnPoint,
									eOwnerType, uOwnerID, pchNormalizedMapVars, 
									pCurrZoneMap, pNextZoneMap, pCurrRules, pNextRules, 
									pTransOverride);

								for (j = 0; j < iSize; j++) {
									Entity* pEntity = entFromContainerID( iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);

									if (i != j && pEntity && pEntity->pPlayer 
										&& !entCheckFlag(pEntity,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS)) {
											gslTeam_AddAwayTeamMemberToMapTransfer(pEntity, bIncludeTeammates);
									}
								}
							}
							else
							{
								//this will destroy the list of away team pets and set the proper states
								Entity_SaveAwayTeamPets(pEnt, NULL);
								//if someone is already on the next map, then just transfer us
								gslEntityPlayTransitionSequenceThenMapMoveEx(pEnt, pcMapName, zmapInfoGetMapType(pNextZoneMap), pcNamedSpawnPoint, 0, uMapID, uPartitionID, eOwnerType, uOwnerID, pchNormalizedMapVars, pCurrRules, pNextRules, pTransOverride,eFlags);
								break;
							}
						}
					}

					if (i == iSize) {
						gslTeam_CreateAwayTeamMapTransferData(pEnt, pcMapName, pcNamedSpawnPoint,
															eOwnerType, uOwnerID, pchNormalizedMapVars, 
															pCurrZoneMap, pNextZoneMap, pCurrRules, pNextRules, 
															pTransOverride);
			
						//no one is on the next map, check the players on the current map
						for (i = 0; i < iSize; i++) {
							Entity* pEntity = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);

							if (pEntity && pEntity->pPlayer 
								&& !entCheckFlag(pEntity,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS)) {
								//if this member is on the current map and not transitioning, try to pull him with us
								gslTeam_AddAwayTeamMemberToMapTransfer(pEntity, bIncludeTeammates);
							}
						}
					}
				} else {
					gslTeam_CreateAwayTeamMapTransferData(pEnt, pcMapName, pcNamedSpawnPoint, 
														eOwnerType, uOwnerID, pchNormalizedMapVars, 
														pCurrZoneMap, pNextZoneMap,
														pCurrRules, pNextRules, 
														pTransOverride);
			
					//1-player map transfer
					gslTeam_AddAwayTeamMemberToMapTransfer(pEnt, bIncludeTeammates);
				}

				gslTeam_SendMapTransferInfoToClients(pEnt, bIncludeTeammates);	
			}
		} else
		{
			gslEntityPlayTransitionSequenceThenMapMoveEx(pEnt, pcMapName, zmapInfoGetMapType(pNextZoneMap), pcNamedSpawnPoint, 0, uMapID, uPartitionID, eOwnerType, uOwnerID, pchNormalizedMapVars, pCurrRules, pNextRules, pTransOverride,eFlags);
		}
	} else 
	{
		gslEntityPlayTransitionSequenceThenMapMoveEx(pEnt, pcMapName, zmapInfoGetMapType(pNextZoneMap), pcNamedSpawnPoint, 0, uMapID, uPartitionID, eOwnerType, uOwnerID, pchNormalizedMapVars, pCurrRules, pNextRules, pTransOverride,eFlags);
	}

	// Clean up
	eaDestroy(&eaNormalizedVariables);
}


static void spawnpoint_RequestZoneMapInfoByPublicNameCB(TransactionReturnVal* pReturn, SpawnpointMapMoveData* pData)
{
	ZoneMapInfoRequest* pZoneMapInfoRequest = NULL;
	enumTransactionOutcome eOutcome;
	eOutcome = RemoteCommandCheck_aslMapManagerRequestZoneMapInfoByPublicName(pReturn, &pZoneMapInfoRequest);
	switch (eOutcome)
	{
		xcase TRANSACTION_OUTCOME_FAILURE:
			//What to do here?
	xcase TRANSACTION_OUTCOME_SUCCESS:
	{
		Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);
		DoorTransitionSequenceDef* pTransOverride = GET_REF(pData->hTransOverride);

		if (pEnt && pZoneMapInfoRequest)
		{
			ZoneMapInfo* pNextZoneMapInfo = zmapInfoGetFromRequest(pZoneMapInfoRequest);
			spawnpoint_MovePlayerToMapAndSpawnEx(pEnt, 
				pData->pchMapName, 
				pData->pchSpawnpoint, 
				pData->pchQueueName,
				pData->eOwnerType, 
				pData->uOwnerID, 
				pData->uMapID,
				pData->uPartitionID,
				pData->eaVars,
				pData->pScope, 
				pNextZoneMapInfo, 
				pTransOverride,
				pData->eFlags,
				pData->bIncludeTeammates);
			}
		}
	}
	StructDestroySafe(parse_ZoneMapInfoRequest, &pZoneMapInfoRequest);
	StructDestroySafe(parse_SpawnpointMapMoveData, &pData);
}


SpawnpointMapMoveData* spawnpoint_GetMapMoveData(	Entity *pEnt, 
													const char *pchMapName, 
													const char *pchSpawnpoint, 
													const char *pchQueueName, 
													GlobalType eOwnerType, 
													ContainerID uOwnerID, 
													ContainerID uMapID,
													U32 uPartitionID,
													WorldVariable **eaVariables, 
													const WorldScope *pScope, 
													DoorTransitionSequenceDef* pTransOverride,
													U32 eFlags,
													bool bIncludeTeammates)
{
	SpawnpointMapMoveData* pCBData = StructCreate(parse_SpawnpointMapMoveData);
	pCBData->erEnt = entGetRef(pEnt);
	pCBData->pchMapName = StructAllocString(pchMapName);
	pCBData->pchSpawnpoint = StructAllocString(pchSpawnpoint);
	pCBData->pchQueueName = StructAllocString(pchQueueName);
	pCBData->eOwnerType = eOwnerType;
	pCBData->uOwnerID = uOwnerID;
	pCBData->uMapID = uMapID;
	pCBData->uPartitionID = uPartitionID;
	eaCopyStructs(&eaVariables, &pCBData->eaVars, parse_WorldVariable);
	pCBData->pScope = pScope;
	SET_HANDLE_FROM_REFERENT("DoorTransitionSequenceDef", pTransOverride, pCBData->hTransOverride);
	pCBData->eFlags = eFlags;
	pCBData->bIncludeTeammates = bIncludeTeammates;
	return pCBData;
}

void spawnpoint_RequestMoveConfirm(Entity *pEnt, const char *pcMapName, const char *pcNamedSpawnPoint, 
										const char *pcQueueName, GlobalType eOwnerType, ContainerID uOwnerID, 
										ContainerID uMapID, WorldVariable **eaVariables, const WorldScope *pScope, 
										DoorTransitionSequenceDef* pTransOverride,
										U32 eFlags, bool bIncludeTeammates)
{
	if(pEnt && pEnt->pPlayer)
	{
		static const U32 uConfirmTime = 30;
		int iCurrentTime = timeSecondsSince2000();

		ANALYSIS_ASSUME(pEnt != NULL);
		if(!pEnt->pPlayer->pWarp && !pEnt->pPlayer->pMapMoveConfirm || iCurrentTime-pEnt->pPlayer->pMapMoveConfirm->uiTimeStart > pEnt->pPlayer->pMapMoveConfirm->uiTimeToConfirm)
		{
			PlayerMapMoveConfirm *pConfirm = StructCreate(parse_PlayerMapMoveConfirm);

			if (pEnt->pPlayer->pMapMoveConfirm)
				StructDestroy(parse_PlayerMapMoveConfirm,pEnt->pPlayer->pMapMoveConfirm);

			pConfirm->eType = kPlayerMapMove_Warp;
			pConfirm->pcMapName = StructAllocString(pcMapName);
			pConfirm->pcNamedSpawnPoint = StructAllocString(pcNamedSpawnPoint);
			eaCopyStructs(&eaVariables,&pConfirm->eaVariables,parse_WorldVariable);
			pConfirm->eFlags = eFlags;
			pConfirm->pScope = StructClone(parse_WorldScope,pScope);

			pConfirm->pWarp = 0;

			pConfirm->uiTimeStart = timeSecondsSince2000();

			pConfirm->uiTimeToConfirm = uConfirmTime;

			pEnt->pPlayer->pMapMoveConfirm = pConfirm;

			//Client confirm and boosh!
			{
				//Send the confirmation information to the client
					
				PlayerMapMoveClient *pClientConfirm = StructCreate(parse_PlayerMapMoveClient);
				pClientConfirm->eType = kPlayerMapMove_Warp;

				// don't need for now
				//if(pchMapMsgKey)
					//SET_HANDLE_FROM_STRING(gMessageDict,pchMapMsgKey, pClientConfirm->hDisplayName);

				pClientConfirm->uiTimeStart = timeSecondsSince2000();
				pClientConfirm->uiTimeToConfirm = uConfirmTime;

				// Waiting on confirmation of some sort, send the request down
				ClientCmd_RequestMapMoveConfirm(pEnt, pClientConfirm);

				StructDestroy(parse_PlayerMapMoveClient, pClientConfirm);
			}
		}
		else
		{
			//This tried to warp you but you were already involved in warping or confirming something
		}
	}
}

void spawnpoint_MovePlayerToMapAndSpawn(Entity *pEnt, const char *pcMapName, const char *pcNamedSpawnPoint, 
										const char *pcQueueName, GlobalType eOwnerType, ContainerID uOwnerID, 
										ContainerID uMapID, U32 uPartitionID, WorldVariable **eaVariables, const WorldScope *pScope, 
										DoorTransitionSequenceDef* pTransOverride,
										U32 eFlags, bool bIncludeTeammates)
{
	bool bMissionReturn = (stricmp("MissionReturn", pcNamedSpawnPoint) == 0);

	if (pEnt && pEnt->pPlayer) {
		//If the player is logging off
		EntityTimedLogoff *pLogoff = eaIndexedGetUsingInt(&g_eaLogoffEnts, entGetContainerID(pEnt));
		if(pLogoff) {
			// If they've already disconnected
			if(pLogoff->bDisconnected) {
				//Remove timer...
				gslLogoff_RemoveTimer(pEnt);
				
				// and instantly logout
				gslLogOutEntity(pEnt, 0, 0);
				
				return;
			} else {
				//Just remove the timer and carry on moving them from the map
				gslLogoff_RemoveTimer(pEnt);
			}
		}

		if (pcMapName && (*pcMapName)) {
			// If a map name is provided, perform a map change
			// We do this even if the map name is the same as the current map
			// to allow people to move between instances of the same map 
			// that have different internal values
			ZoneMapInfo* pNextZoneMap = worldGetZoneMapByPublicName(pcMapName);
			if (!pNextZoneMap) {
				// If the ZoneMapInfo is not available, request a copy from the map manager before proceeding
				TransactionReturnVal* pReturn;
				SpawnpointMapMoveData* pCBData = spawnpoint_GetMapMoveData(pEnt, pcMapName, pcNamedSpawnPoint,
					pcQueueName, eOwnerType, uOwnerID, uMapID, uPartitionID, eaVariables, pScope, pTransOverride, eFlags, bIncludeTeammates);
				pReturn = objCreateManagedReturnVal(spawnpoint_RequestZoneMapInfoByPublicNameCB, pCBData);
				RemoteCommand_aslMapManagerRequestZoneMapInfoByPublicName(pReturn, GLOBALTYPE_MAPMANAGER, 0, pcMapName);
			} else {
				spawnpoint_MovePlayerToMapAndSpawnEx(pEnt, pcMapName, pcNamedSpawnPoint, pcQueueName,
					eOwnerType, uOwnerID, uMapID, uPartitionID, eaVariables, pScope, pNextZoneMap, pTransOverride, eFlags, bIncludeTeammates);
			}
		}
		else if (bMissionReturn)
		{
			//For mission returns, we may still want to bring the whole team along.
			Team* pTeam = team_GetTeam(pEnt);
			int i;
			if (bIncludeTeammates && pTeam)
			{
				for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
				{
					Entity* pTeamEnt = entFromContainerID(pTeam->eaMembers[i]->uPartitionID, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);
					if (pTeamEnt)
						LeaveMapEx(pTeamEnt, pTransOverride);
				}
			}
			else
			{
				LeaveMapEx(pEnt, pTransOverride);
			}
		}
		else if (bIncludeTeammates)
		{
			//With no map name and no special mission-return string, attempt to bring our entire team along if the flag is set.
			// Even if no spawn point is specified, we still want to try just in case somebody is in a 
			// different partition/instance.
			const char *pcCurrentMap = zmapInfoGetPublicName(NULL);
			ZoneMapInfo* pCurrZoneMap = worldGetZoneMapByPublicName(pcCurrentMap);
			WorldVariable** eaCurrVars = NULL;
			MapDescription * pDesc = partition_GetMapDescription(entGetPartitionIdx(pEnt));
			worldVariableStringToArray(pDesc->mapVariables, &eaCurrVars);

			spawnpoint_MovePlayerToMapAndSpawnEx(pEnt, pcCurrentMap, pcNamedSpawnPoint, pcQueueName,
				pDesc->ownerType, pDesc->ownerID, uMapID, uPartitionID, eaCurrVars, pScope, pCurrZoneMap, pTransOverride, eFlags, bIncludeTeammates);
			eaDestroyStruct(&eaCurrVars, parse_WorldVariable);
		}
		else if (pcNamedSpawnPoint && (*pcNamedSpawnPoint)) {
			// If no map name is provided, relocate on current map.
			RegionRules *pCurrRules = getRegionRulesFromEnt(pEnt);
			if( pScope ) {
				GameSpawnPoint* pSpawnPoint = spawnpoint_GetByName( pcNamedSpawnPoint, pScope );
				if( pSpawnPoint ) {
					pcNamedSpawnPoint = pSpawnPoint->pcName;
				}
			}			
			gslEntityPlayTransitionSequenceThenMapMoveEx(pEnt, NULL, ZMTYPE_UNSPECIFIED, pcNamedSpawnPoint, 0, 0, 0, 0, 0, NULL, pCurrRules, pCurrRules, pTransOverride,eFlags);
		}
		else
		{
			Errorf("Attempted map transfer with no destination map name or spawn point name. This is probably not intentional.");
		}
	}
}

void spawnpoint_MovePlayerToLocation(	Entity *pPlayerEnt, 
										Vec3 vecSpawnLoc, 
										Quat quatSpawnRot, 
										DoorTransitionSequenceDef *pTransDef, 
										bool bMovePets)
{
	Entity *pMount;
	Vec3 vOldPos;
	Vec3 vNewPos;
	Vec3 pyrFace;
	RegionRules *pRules = NULL;
	int bFloorFound = false;
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	// Move the player and mount
	entGetPos(pPlayerEnt, vOldPos);
	if (pMount = entGetMount(pPlayerEnt)) {
		entSetPos(pMount, vecSpawnLoc, true, "spawnLoc");
		entSetRot(pMount, quatSpawnRot, true, "spawnLoc");
	}
	copyVec3(vecSpawnLoc,vNewPos);

	pRules = RegionRulesFromVec3(vNewPos);

	if (pRules) {
		Entity_GetPositionOffset(iPartitionIdx, pRules, quatSpawnRot, Entity_GetSeedNumber(iPartitionIdx, pPlayerEnt, vNewPos), vNewPos, pPlayerEnt->iBoxNumber);
	}

	if (!pRules || pRules->fSpawnRadius==0) {
		vNewPos[0] += randomF32();
		vNewPos[2] += randomF32();
	} else {
		vNewPos[0] += (randomF32() * pRules->fSpawnRadius) - pRules->fSpawnRadius / 2;
		vNewPos[2] += (randomF32() * pRules->fSpawnRadius) - pRules->fSpawnRadius / 2;
	}

	worldSnapPosToGround(iPartitionIdx, vNewPos, SPAWN_SNAP_TO_DIST, -SPAWN_SNAP_TO_DIST, &bFloorFound);
	quatToPYR(quatSpawnRot, pyrFace);
	pyrFace[0] = 0.f;
	entSetPosRotFace(pPlayerEnt, vNewPos, quatSpawnRot, pyrFace, true, true, "spawnLoc");

	gslCacheEntRegion(pPlayerEnt,entity_GetCachedGameAccountDataExtract(pPlayerEnt));

	if (bFloorFound && gConf.bNewAnimationSystem) {
		mrSurfaceSetSpawnedOnGround(pPlayerEnt->mm.mrSurface, true);
	}

	if (bMovePets) {
		int i;

		if(pPlayerEnt->pSaved)
		{
			EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);

 			for(i=0; i<eaSize(&pPlayerEnt->pSaved->ppOwnedContainers); i++) {
				if (pPlayerEnt->pSaved->ppOwnedContainers[i]->curEntity) {
					Entity* pEntity = pPlayerEnt->pSaved->ppOwnedContainers[i]->curEntity;
					gslSavedPet_SetSpawnLocationRotationForPet(iPartitionIdx, pPlayerEnt, pEntity);
				}
			}

			for(i=0; i<eaSize(&pPlayerEnt->pSaved->ppCritterPets); i++) {
				if (pPlayerEnt->pSaved->ppCritterPets[i]->pEntity) {
					Entity* pEntity = pPlayerEnt->pSaved->ppCritterPets[i]->pEntity;
					gslSavedPet_SetSpawnLocationRotationForPet(iPartitionIdx, pPlayerEnt, pEntity);
				}
			}
			if (pSCPData)
			{
				if (pSCPData->iSummonedSCP >= 0) {
					Entity* pEntity = entFromEntityRef(pPlayerEnt->iPartitionIdx_UseAccessor, pSCPData->erSCP);
					gslSavedPet_SetSpawnLocationRotationForPet(iPartitionIdx, pPlayerEnt, pEntity);
				}
			}
		}

		//Warp the FC 'powers'-base critter pet's
		if(pPlayerEnt->pPlayer)
		{
			for(i=0; i<eaSize(&pPlayerEnt->pPlayer->petInfo); i++)
			{
				PlayerPetInfo *pPetInfo = pPlayerEnt->pPlayer->petInfo[i];
				Entity *pPetEnt = entFromEntityRef(iPartitionIdx, pPetInfo->iPetRef);
				if(pPetEnt 
					&& pPetEnt->erOwner == entGetRef(pPlayerEnt)
					&& pPetEnt->myEntityType == GLOBALTYPE_ENTITYCRITTER
					&& !entCheckFlag(pPetEnt, ENTITYFLAG_CRITTERPET))
				{
					gslSavedPet_SetSpawnLocationRotationForPet(iPartitionIdx, pPlayerEnt, pPetEnt);
				}
			}
		}
	}

	// Clear the target
	entity_SetTarget(pPlayerEnt, 0);

	// tell the client to reset their cursor mode to default
	ClientCmd_gclCursorMode_ChangeToDefault(pPlayerEnt);

	// Set the arrival sequence override
	pPlayerEnt->pPlayer->pchTransitionSequence = SAFE_MEMBER(pTransDef, pchName);
	// Set up a transition sequence, if necessary
	gslHandleDoorTransitionSequenceSetup(pPlayerEnt);

	// Show the loading screen unless this is a short-distance move (less than 200) and there is no "arrival sequence" override
	if(		pPlayerEnt->pPlayer->pchTransitionSequence
		||	distance3Squared(vOldPos, vecSpawnLoc) > gConf.fSpawnPointLoadingScreenDistSq
		||	worldGetWorldRegionByPos(vOldPos) != worldGetWorldRegionByPos(vecSpawnLoc)) 
	{
		ClientCmd_gclLoading_Respawned(pPlayerEnt);
		gslEntitySetInvisibleTransient(pPlayerEnt, 1);
		entSetCodeFlagBits(pPlayerEnt, ENTITYFLAG_IGNORE);
		mmDisabledHandleCreate(&pPlayerEnt->mm.mdhIgnored, pPlayerEnt->mm.movement, __FILE__, __LINE__);
	}

	// Tag mission info to refresh
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pInfo) {
			waypoint_FlagWaypointRefresh(pInfo);
			waypoint_UpdateLandmarkWaypoints(pPlayerEnt);
		}
	}

	// Moving pets, this means its an internal map move not a new map
	// reset powers as the player may have region based powers
	if(bMovePets)
	{
		pPlayerEnt->pChar->bResetPowersArray = true;
	}

	// NOTE - update of visited maps used to be here.  It has been moved to the map history code in gslEntity.c

	// Update time spawned for critters not to kill
	pPlayerEnt->aibase->time.timeSpawned = ABS_TIME_PARTITION(iPartitionIdx);
}

static void spawnpoint_MovePlayerToGameSpawn(Entity *pPlayerEnt, SA_PARAM_NN_VALID GameSpawnPoint *pGamePoint, bool bMovePets)
{
	Vec3 spawnLoc = {0};
	Quat quatSpawnRot = {0};
	
	copyVec3(pGamePoint->pWorldPoint->spawn_pos, spawnLoc);
	copyQuat(pGamePoint->pWorldPoint->spawn_rot, quatSpawnRot);

	spawnpoint_MovePlayerToLocation(pPlayerEnt, 
		spawnLoc, 
		quatSpawnRot, 
		pGamePoint->pWorldPoint->properties ? GET_REF(pGamePoint->pWorldPoint->properties->hTransSequence) : NULL, 
		bMovePets);

	// Commented out by jpanttaja to reduce log load
	//entLog(LOG_PLAYER, pPlayerEnt, "MovePlayerToSpawn", "Moved player to spawn point: %s, <%f %f %f>", pGamePoint->pcName, pGamePoint->pWorldPoint->spawn_pos[0], pGamePoint->pWorldPoint->spawn_pos[1], pGamePoint->pWorldPoint->spawn_pos[2]);
}

GameSpawnPoint* spawnpoint_GetPlayerStartSpawn()
{
	const char* startSpawnName = zmapInfoGetStartSpawnName(NULL);

	// Find the Start Spawn
	if (startSpawnName) {
		GameSpawnPoint* pSpawnPt = spawnpoint_GetByName( startSpawnName, NULL );
		if (pSpawnPt)
			return pSpawnPt;
	} 

	FOR_EACH_SPAWN_POINT(pSpawn) {
		WorldSpawnProperties *pProperties = pSpawn->pWorldPoint ? pSpawn->pWorldPoint->properties : NULL;
		bool bInVolume = false;

		assert(pProperties);

		// Ignore goto spawn points (these are only used if requested by name)
		if (pProperties->spawn_type == SPAWNPOINT_STARTSPAWN) {
			return pSpawn;
		}
	} FOR_EACH_END;

	return NULL;	
}

GameSpawnPoint* spawnPoint_FindPVPPlayerStartSpawn(Entity *pPlayerEnt)
{
	GameSpawnPoint *pGamePoint = NULL;
	const char *pchSpawnPoint = NULL;

	pchSpawnPoint = pvp_GetPlayerSpawnPoint(pPlayerEnt);

	if(pchSpawnPoint)
		pGamePoint = spawnpoint_GetByName(pchSpawnPoint,NULL);

	if(!pGamePoint)
		return spawnpoint_GetPlayerStartSpawn();

	return pGamePoint;
}

void spawnpoint_MovePlayerToStartSpawn(Entity *pPlayerEnt, bool bMovePets)
{
	GameSpawnPoint *pGamePoint = NULL;

	// Commented out by jpanttaja to reduce log load
	//entLog(LOG_PLAYER, pPlayerEnt, "MovePlayerToSpawn", "Attempting to move player to Start Spawn");

	// Find the Start Spawn
	if(zmapInfoGetMapType(NULL) == ZMTYPE_PVP || zmapInfoGetMapType(NULL) == ZMTYPE_QUEUED_PVE)
		pGamePoint = spawnPoint_FindPVPPlayerStartSpawn(pPlayerEnt);
	else
		pGamePoint = spawnpoint_GetPlayerStartSpawn();

	if (pGamePoint){
		spawnpoint_MovePlayerToGameSpawn(pPlayerEnt, pGamePoint, bMovePets);
	} else {
		// Commented out by jpanttaja to reduce log load
		//entLog(LOG_PLAYER, pPlayerEnt, "MovePlayerToSpawn", "No Start Spawn found");
	}
}

bool spawnpoint_validateSpawnPointForPlayer(GameSpawnPoint *pSpawn, Entity *pPlayerEnt, bool bActivatedSpawnsOnly)
{
	WorldSpawnProperties *pProperties = pSpawn->pWorldPoint ? pSpawn->pWorldPoint->properties : NULL;

	assert(pProperties);
	
	// Ignore goto spawn points (these are only used if requested by name)
	if (pProperties->spawn_type == SPAWNPOINT_GOTO) {
		return false;
	}

	// If the player is respawning, only consider "activated" respawn points
	if (bActivatedSpawnsOnly) {
		const char *pcMapName = zmapInfoGetPublicName(NULL);

		if (pProperties->needs_activation &&
			pSpawn->pcName && pcMapName && 
			!spawnpoint_HasPlayerActivatedSpawn(pPlayerEnt, pSpawn->pcName, pcMapName)) {
				return false;
		}
	}

	// If the spawn has a condition, that condition must be true
	if (pProperties->active_cond) {
		MultiVal resultVal;
		MultiVal mv = { 0 };
		Mat4 mat;

		// Set the player, self, and scope vars
		exprContextSetSelfPtr(s_pSpawnPointContext, pPlayerEnt);
		exprContextSetPartition(s_pSpawnPointContext, entGetPartitionIdx(pPlayerEnt));
		exprContextSetPointerVar(s_pSpawnPointContext, "Player", pPlayerEnt, parse_Entity, false, true);

		quatVecToMat4(pSpawn->pWorldPoint->spawn_rot, pSpawn->pWorldPoint->spawn_pos, mat);
		MultiValSetMat4(&mv, mat);
		exprContextSetSimpleVar(s_pSpawnPointContext, "SpawnLocation", &mv);

		exprContextSetScope(s_pSpawnPointContext, SAFE_MEMBER2(pSpawn, pWorldPoint, common_data.closest_scope));

		exprEvaluate(pProperties->active_cond, s_pSpawnPointContext, &resultVal);
		if (MultiValGetInt(&resultVal, NULL) == 0) {
			return false;
		}
	}

	return true;
}

void spawnpoint_MovePlayerToNearestSpawn(Entity *pPlayerEnt, bool bActivatedSpawnsOnly, bool bMovePets)
{
	GameSpawnPoint *pGamePoint = NULL;
	F32 fDistance, fMinDistance = -1;
	Vec3 vPlayerLoc;
	bool bFoundMatchingSourceVolume = false;
	int j;

	// Commented out by jpanttaja to reduce log load
	//entLog(LOG_PLAYER, pPlayerEnt, "MovePlayerToSpawn", "Attempting to move player to nearest Spawn Point, ActivatedSpawnsOnly=%d", bActivatedSpawnsOnly);

	entGetPos(pPlayerEnt, vPlayerLoc);

	// Correct behavior for respawn points is as follows: move the player to the nearest active respawn point
	// that lists a volume that the player is in as one of its source volumes.  If the player is not in
	// any volumes that feed into an active spawn point, move the player to the closest active spawn point,
	// ignoring source volumes.  If there is no such spawn, move the player to the start spawn.

	// Check all respawn points on this layer
	FOR_EACH_SPAWN_POINT(pSpawn) {
		WorldSpawnProperties *pProperties = pSpawn->pWorldPoint ? pSpawn->pWorldPoint->properties : NULL;
		bool bInVolume = false;

		assert(pProperties);

		if(!spawnpoint_validateSpawnPointForPlayer(pSpawn,pPlayerEnt,bActivatedSpawnsOnly))
		{
			continue;
		}

		// Check whether the player is in one of this spawn point's source volumes
		for(j=eaSize(&pProperties->source_volume_names)-1; j>=0; --j) {
			if (volume_IsEntityInVolumeByName(pPlayerEnt, pProperties->source_volume_names[j], SAFE_MEMBER2(pSpawn, pWorldPoint, common_data.closest_scope))) {
				bInVolume = true;
				break;
			}
		}

		// If we've previously found a spawn point with a source volume that matches one of the player's current volumes,
		// reject any spawn points that don't also match the player's current volumes.
		if (bInVolume || !bFoundMatchingSourceVolume) {
			// Use this spawn if it's the closest to the player, or if it's the only spawn we've found so far
			// with a matching source volume
			fDistance = distance3Squared(vPlayerLoc, pSpawn->pWorldPoint->spawn_pos);

			if (bInVolume && !bFoundMatchingSourceVolume) {
				pGamePoint = pSpawn;
				fMinDistance = fDistance;
				bFoundMatchingSourceVolume = true;
			} else if ((fMinDistance < 0) || (fDistance < fMinDistance)) {
				pGamePoint = pSpawn;
				fMinDistance = fDistance;
			}
		}
	} FOR_EACH_END; 

	if (pGamePoint) {
		spawnpoint_MovePlayerToGameSpawn(pPlayerEnt, pGamePoint, bMovePets);
	} else  {
		// No spawn point found; go to the start spawn
		// Commented out by jpanttaja to reduce log load
		//entLog(LOG_PLAYER, pPlayerEnt, "MovePlayerToSpawn", "No spawn point found. Attempting to move player to Start Spawn");
		spawnpoint_MovePlayerToStartSpawn(pPlayerEnt, bMovePets);
	}
}

#define SPAWN_ACTION_DISTANCE_SCALE 0.1f

void spawnpoint_MovePlayerToSpawnPointNearTeam(Entity *pPlayerEnt, bool bActivatedSpawnsOnly, bool bMovePets)
{
	GameSpawnPoint *pGamePoint = NULL;
	RegionRules *pRegionRules = getRegionRulesFromEnt(pPlayerEnt);
	PlayerSpawnInfluence **eaFriendlyInfluence = NULL;
	PlayerSpawnInfluence **eaEnemyInfluence = NULL;
	PlayerSpawnWeightedVote **eaWeightedVotes = NULL;
	F32 fActionDistance = ENTITY_DEFAULT_SEND_DISTANCE;
	F32 fActionDistanceSqr;
	F32 fMaxSpawnDist = 10000.0f;
	F32 fMaxSpawnDistanceSqr = fMaxSpawnDist * fMaxSpawnDist;
	F32 fLeadingVoteWeight = 0.0f;
	int *piValidSpawnIndices = NULL;
	int i;

	// Calculate the "action" distance.
	// If an enemy is within this distance from a spawnpoint, then don't allow the player to spawn there
	// TODO: This is hardcoded for now
	if (pRegionRules && pRegionRules->fSendDistanceMin)
	{
		fActionDistance = pRegionRules->fSendDistanceMin;
	}
	fActionDistance *= SPAWN_ACTION_DISTANCE_SCALE;
	fActionDistanceSqr = fActionDistance*fActionDistance;

	// If its a queue map, find all friendly and enemy players
	if(gslQueue_IsQueueMap())
	{
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		EntityIterator *iter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
		Entity *pent;

		while(pent = EntityIteratorGetNext(iter))
		{
			EntityRelation eRelation;

			if(pent == pPlayerEnt)
				continue;

			eRelation = entity_GetRelationEx(iPartitionIdx,pent,pPlayerEnt,true);

			switch (eRelation)
			{
				xcase kEntityRelation_Friend:
				{
					PlayerSpawnInfluence* pInfluence = StructCreate(parse_PlayerSpawnInfluence);
					entGetPos(pent, pInfluence->vPos);
					pInfluence->fDistanceSqrFromSpawnIndex = FLT_MAX;
					eaPush(&eaFriendlyInfluence, pInfluence);
				}
				xcase kEntityRelation_Foe:
				{
					PlayerSpawnInfluence* pInfluence = StructCreate(parse_PlayerSpawnInfluence);
					entGetPos(pent, pInfluence->vPos);
					pInfluence->fDistanceSqrFromSpawnIndex = FLT_MAX;
					eaPush(&eaEnemyInfluence, pInfluence);
				}
			}
		}
		EntityIteratorRelease(iter);
	}
	else if(pPlayerEnt->pTeam && pPlayerEnt->pTeam->eState == TeamState_Member)
	{
		Team *pTeam = GET_REF(pPlayerEnt->pTeam->hTeam);

		if(pTeam)
		{
			int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

			for(i=0;i<eaSize(&pTeam->eaMembers);i++)
			{
				Entity *pTeamEntity = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pTeam->eaMembers[i]->iEntID);

				if(pTeamEntity && pTeamEntity != pPlayerEnt)
				{
					PlayerSpawnInfluence* pInfluence = StructCreate(parse_PlayerSpawnInfluence);
					entGetPos(pTeamEntity, pInfluence->vPos);
					pInfluence->fDistanceSqrFromSpawnIndex = FLT_MAX;
					eaPush(&eaFriendlyInfluence, pInfluence);
				}
			}
		}
	}

	// If there is no friendly or enemy influence, then just spawn the player normally
	if(eaSize(&eaFriendlyInfluence)==0 && eaSize(&eaEnemyInfluence)==0)
	{
		spawnpoint_MovePlayerToNearestSpawn(pPlayerEnt,bActivatedSpawnsOnly,bMovePets);
		return;
	}

	// Iterate over all spawnpoints and get the preferred spawnpoint for each friendly player
	FOR_EACH_SPAWN_POINT(pSpawn) {
		WorldSpawnProperties *pProperties = pSpawn->pWorldPoint ? pSpawn->pWorldPoint->properties : NULL;
		bool bInVolume = false;
		bool bFriendlyIsCloserThanEnemy = false;
		F32 fClosestEnemyDistSqr = FLT_MAX;

		assert(pProperties);

		if(!spawnpoint_validateSpawnPointForPlayer(pSpawn,pPlayerEnt,bActivatedSpawnsOnly))
		{
			continue;
		}

		eaiPush(&piValidSpawnIndices, ipSpawnIndex);

		// If any enemy is within the action distance, then skip this spawnpoint
		for (i = eaSize(&eaEnemyInfluence)-1; i >= 0; i--)
		{
			PlayerSpawnInfluence* pInfluence = eaEnemyInfluence[i];
			F32 fDistanceSqr = distance3Squared(pInfluence->vPos,pSpawn->pWorldPoint->spawn_pos);
			if (fDistanceSqr <= fActionDistanceSqr)
			{
				break;
			}
			if (fDistanceSqr < fClosestEnemyDistSqr)
			{
				fClosestEnemyDistSqr = fDistanceSqr;
			}
		}
		if (i >= 0)
		{
			continue;
		}

		// If the closest enemy distance is closer than the closest friendly distance, skip this spawnpoint
		for (i = eaSize(&eaFriendlyInfluence)-1; i >= 0; i--)
		{
			PlayerSpawnInfluence* pInfluence = eaFriendlyInfluence[i];
			pInfluence->fDistanceSqr = distance3Squared(pInfluence->vPos,pSpawn->pWorldPoint->spawn_pos);
			if (pInfluence->fDistanceSqr < fClosestEnemyDistSqr)
			{
				bFriendlyIsCloserThanEnemy = true;
			}
		}
		if (!bFriendlyIsCloserThanEnemy)
		{
			continue;
		}

		// If any player is closest to this spawnpoint, cache the spawn information
		for (i = eaSize(&eaFriendlyInfluence)-1; i >= 0; i--)
		{
			PlayerSpawnInfluence* pInfluence = eaFriendlyInfluence[i];
			if (pInfluence->fDistanceSqr < fMaxSpawnDistanceSqr &&
				pInfluence->fDistanceSqr < pInfluence->fDistanceSqrFromSpawnIndex)
			{
				pInfluence->iSpawnIndex = ipSpawnIndex;
				pInfluence->fDistanceSqrFromSpawnIndex = pInfluence->fDistanceSqr;
			}
		}
	} FOR_EACH_END;
	
	// Add up the vote weights
	for (i = eaSize(&eaFriendlyInfluence)-1; i >= 0; i--)
	{
		PlayerSpawnInfluence* pInfluence = eaFriendlyInfluence[i];
		if (pInfluence->iSpawnIndex >= 0)
		{
			PlayerSpawnWeightedVote* pVote = eaIndexedGetUsingInt(&eaWeightedVotes, pInfluence->iSpawnIndex);
			F32 fWeight;
		
			if (!pVote)
			{
				pVote = StructCreate(parse_PlayerSpawnWeightedVote);
				pVote->iSpawnIndex = pInfluence->iSpawnIndex;
				eaIndexedEnable(&eaWeightedVotes, parse_PlayerSpawnWeightedVote);
				eaPush(&eaWeightedVotes, pVote);
			}

			fWeight = 1.0f - (sqrt(pInfluence->fDistanceSqrFromSpawnIndex) / fMaxSpawnDist);
			pVote->fWeight += fWeight;
		}
	}

	// Get the best spawnpoint for the player
	for (i = eaSize(&eaWeightedVotes)-1; i >= 0; i--)
	{
		PlayerSpawnWeightedVote* pVote = eaWeightedVotes[i];
		if (pVote->fWeight > fLeadingVoteWeight)
		{
			pGamePoint = s_eaSpawnPoints[pVote->iSpawnIndex];
			fLeadingVoteWeight = pVote->fWeight;
		}
	}

	// If a spawnpoint wasn't found, randomize one from the list of valid spawns
	if (!pGamePoint && eaiSize(&piValidSpawnIndices))
	{
		int iValidSpawns = eaiSize(&piValidSpawnIndices);
		int iSpawnIndex;
		if (iValidSpawns > 1)
		{
			int iIndex = randomIntRange(0, eaiSize(&piValidSpawnIndices)-1);
			iSpawnIndex = piValidSpawnIndices[iIndex];
		}
		else
		{
			iSpawnIndex = piValidSpawnIndices[0];
		}
		pGamePoint = s_eaSpawnPoints[iSpawnIndex];
	}

	eaDestroyStruct(&eaFriendlyInfluence, parse_PlayerSpawnInfluence);
	eaDestroyStruct(&eaEnemyInfluence, parse_PlayerSpawnInfluence);
	eaDestroyStruct(&eaWeightedVotes, parse_PlayerSpawnWeightedVote);
	eaiDestroy(&piValidSpawnIndices);

	if (pGamePoint) {
		spawnpoint_MovePlayerToGameSpawn(pPlayerEnt, pGamePoint, bMovePets);
	} else  {
		// No spawn point found; go to the start spawn
		spawnpoint_MovePlayerToStartSpawn(pPlayerEnt, bMovePets);
	}
}


void spawnpoint_MovePlayerToNamedSpawn(Entity *pPlayerEnt, const char *pchNamedSpawnPoint, const WorldScope *pScope, bool bDefaultToStartSpawn)
{
	GameSpawnPoint *pGamePoint = NULL;
	S32 bIsStartSpawn = (stricmp(START_SPAWN, pchNamedSpawnPoint) == 0);
	S32 bIsRespawnPoint = (!bIsStartSpawn && stricmp(SPAWN_AT_NEAR_RESPAWN, pchNamedSpawnPoint) == 0);

	// Commented out by jpanttaja to reduce log load
	//entLog(LOG_PLAYER, pPlayerEnt, "MovePlayerToSpawn", "Attempting to move player to Spawn Point named %s", pchNamedSpawnPoint);

	if(bIsStartSpawn) {
		spawnpoint_MovePlayerToStartSpawn(pPlayerEnt,true);
	} else if (bIsRespawnPoint) {
		spawnpoint_MovePlayerToNearestSpawn(pPlayerEnt, true, true);
	} else {
		// Find the spawn point
		if (pchNamedSpawnPoint) {
			pGamePoint = spawnpoint_GetByNameForSpawning(pchNamedSpawnPoint, pScope);

			if (!pGamePoint) {
				Errorf("Trying to send player to spawn location %s on map %s; no such spawn.", pchNamedSpawnPoint, zmapInfoGetPublicName(NULL));
			}
			if (strnicmp(pchNamedSpawnPoint, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0) {
				Errorf("Trying to send player to spawn location %s on map %s; this location has a temporary name, which is not allowed.", pchNamedSpawnPoint, zmapInfoGetPublicName(NULL));
			}
		} 

		if (pGamePoint) {
			spawnpoint_MovePlayerToGameSpawn(pPlayerEnt, pGamePoint,true);
		
		} else if (bDefaultToStartSpawn){
			// No spawn point found; go to the start spawn
			// Commented out by jpanttaja to reduce log load
			//entLog(LOG_PLAYER, pPlayerEnt, "MovePlayerToSpawn", "No spawn point found. Attempting to move player to Start Spawn");
			spawnpoint_MovePlayerToStartSpawn(pPlayerEnt,true);
		} else {
			// Commented out by jpanttaja to reduce log load
			//entLog(LOG_PLAYER, pPlayerEnt, "MovePlayerToSpawn", "No spawn point found.");
		}
	}
	gslCacheEntRegion(pPlayerEnt, entity_GetCachedGameAccountDataExtract(pPlayerEnt));
}


int spawnpoint_ExprLocationResolveSpawnPoint(ExprContext *pContext, const char *pcName, Mat4 matOut, const char *pcBlamefile)
{
	GameSpawnPoint *pGamePoint;
	WorldScope *pScope = exprContextGetScope(pContext);

	pGamePoint = spawnpoint_GetByName(pcName, pScope);

	if (!pGamePoint) {
		Errorf("Trying to get location of spawn point %s on map %s; no such spawn.", pcName, zmapInfoGetPublicName(NULL));
	}
	if (strnicmp(pcName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0) {
		Errorf("Trying to get location of spawn point %s on map %s; this location has a temporary name, which is not allowed.", pcName, zmapInfoGetPublicName(NULL));
	}

	if (pGamePoint && pGamePoint->pWorldPoint) {
		copyMat4(unitmat, matOut);
		copyVec3(pGamePoint->pWorldPoint->spawn_pos, matOut[3]);
		return true;
	}

	return false;
}


// ----------------------------------------------------------------------------------
// Post Processing Logic
// ----------------------------------------------------------------------------------


static void spawnpoint_InitPartitionState(GameSpawnPoint *pGamePoint, SpawnPointPartitionState *pState)
{
	WorldSpawnProperties *pProperties = SAFE_MEMBER(pGamePoint->pWorldPoint, properties);

	// Add activation volume around spawn point if required
	if (pGamePoint->pWorldPoint && pProperties && 
		pProperties->needs_activation &&
		(pProperties->spawn_type == SPAWNPOINT_RESPAWN || pProperties->spawn_type == SPAWNPOINT_STARTSPAWN)) {
		Mat4 mSpawnMat;
		Vec3 vZero = { 0, 0, 0};

		quatToMat(pGamePoint->pWorldPoint->spawn_rot, mSpawnMat);
		copyVec3(pGamePoint->pWorldPoint->spawn_pos, mSpawnMat[3]);

		{
			RegionRules *rr = RegionRulesFromVec3(mSpawnMat[3]);
			pState->pVolume = wlVolumeCreateSphere(pState->iPartitionIdx, s_eSpawnPointVolumeType, pGamePoint, mSpawnMat, vZero, rr ? rr->fSpawnUnlockRadius : SPAWN_POINT_DEFAULT_UNLOCK_RADIUS);
		}
		wlVolumeSetQueryCallbacks(pState->pVolume, spawnpoint_VolumeEnterCB, NULL, NULL);
	}
}


static void spawnpoint_PostProcess(GameSpawnPoint *pGamePoint)
{
	WorldSpawnProperties *pProperties = SAFE_MEMBER(pGamePoint->pWorldPoint, properties);
	WorldScope *pScope = SAFE_MEMBER(pGamePoint->pWorldPoint, common_data.closest_scope);

	exprContextSetScope(s_pSpawnPointContext, pScope);

	// Compile expression if necessary
	if (pProperties && pProperties->active_cond) {
		// Compile expression
		exprGenerate(pProperties->active_cond, s_pSpawnPointContext);
	}

	//// Debug printing
	//if (pGamePoint->pcName) {
	//	printf("## Name='%s'\n", pGamePoint->pcName);
	//} else {
	//	printf("## Unnamed Point\n");
	//}
}


static void spawnpoint_PostProcessAll(void)
{
	// Post-process all spawn points
	FOR_EACH_SPAWN_POINT(pSpawn) {
		spawnpoint_PostProcess(pSpawn);
	} FOR_EACH_END;
}


// ----------------------------------------------------------------------------------
// Spawn Point List Logic
// ----------------------------------------------------------------------------------

static void spawnpoint_FreePartitionState(SpawnPointPartitionState *pState)
{
	if (!pState) {
		return;
	}
	if (pState->pVolume) {
		wlVolumeFree(pState->pVolume);
	}
	free(pState);
}

static void spawnpoint_Free(GameSpawnPoint *pGamePoint)
{
	if (!pGamePoint) {
		return;
	}
	eaiDestroy(&pGamePoint->eaiPlayerUnlocks);
	eaDestroyEx(&pGamePoint->eaPartitionStates, spawnpoint_FreePartitionState);
	free(pGamePoint);
}


static void spawnpoint_AddWorldSpawn(const char *pcName, WorldSpawnPoint *pWorldPoint)
{
	GameSpawnPoint *pGamePoint = calloc(1,sizeof(GameSpawnPoint));
	if (pcName) {
		pGamePoint->pcName = allocAddString(pcName);
	}
	pGamePoint->pWorldPoint = pWorldPoint;
	eaPush(&s_eaSpawnPoints, pGamePoint);
}


static void spawnpoint_ClearList(void)
{
	eaDestroyEx(&s_eaSpawnPoints, spawnpoint_Free);
}


// Check if a spawn point exists
bool spawnpoint_SpawnPointExists(const char *pcName, const WorldScope *pScope)
{
	return spawnpoint_GetByName(pcName, pScope) != NULL;
}


// Get position.  Returns true if it exists
bool spawnpoint_GetSpawnPosition(const char *pcName, const WorldScope *pScope, Vec3 out_vPosition)
{
	GameSpawnPoint *pSpawn = spawnpoint_GetByName(pcName, pScope);
	if (pSpawn) {
		copyVec3(pSpawn->pWorldPoint->spawn_pos, out_vPosition);
		return true;
	}

	return false;
}


// Report all spawn point positions to the beacon system
void spawnpoint_GatherBeaconPositions(void)
{
	FOR_EACH_SPAWN_POINT(pSpawn) {
		beaconAddSpawnLocation(pSpawn->pWorldPoint->spawn_pos, 0);
	} FOR_EACH_END;
}


// ----------------------------------------------------------------------------------
// Map Load Logic
// ----------------------------------------------------------------------------------

bool spawnpoint_IsInPlayable(GameSpawnPoint *pSpawnPoint)
{
	int i;
	WorldVolumeEntry **eaPlayable = gslPlayableGet();

	if(!eaSize(&eaPlayable)) {
		return true;
	}

	for(i=0; i<eaSize(&eaPlayable); i++) {
		WorldVolumeEntry *pEntry = eaPlayable[i];
		
		if(sphereOrientBoxCollision(pSpawnPoint->pWorldPoint->spawn_pos, 0, 
									pEntry->base_entry.shared_bounds->local_min, 
									pEntry->base_entry.shared_bounds->local_max,
									pEntry->base_entry.bounds.world_matrix, NULL)) {
			return true;
		}
	}

	return false;
}


void spawnpoint_MapValidate(ZoneMap *pZoneMap)
{
	int j;
	int iNumStartSpawns = 0;
	bool bIsSpaceMap = 0;
	WorldRegion **eaRegions = worldGetAllWorldRegions();
	ZoneMapInfo* pZoneMapInfo = zmapGetInfo(pZoneMap);

	if (zmapInfoGetNotPlayerVisited(pZoneMapInfo))
		return;

	for(j=0; j<eaSize(&eaRegions); j++) {
		WorldRegionType eType = worldRegionGetType(eaRegions[j]);
		if (eType==WRT_Space || eType==WRT_SectorSpace) {
			bIsSpaceMap = true;
			break;
		}
	}

	if (!pZoneMap || stricmp("Emptymap", zmapInfoGetPublicName(pZoneMapInfo)) != 0) {
		const char* startSpawnName = zmapInfoGetStartSpawnName(pZoneMapInfo);
			
		// Check that there is exactly one start spawn on the map
		FOR_EACH_SPAWN_POINT(pSpawn) {
			bool isStartSpawn = false;

			if (pSpawn->pWorldPoint->properties->spawn_type == SPAWNPOINT_STARTSPAWN) {
				isStartSpawn = true;
			}
			if (startSpawnName && stricmp(startSpawnName, pSpawn->pcName) == 0) {
				isStartSpawn = true;
			}
			
			if (isStartSpawn) {
				++iNumStartSpawns;
			
				if (bIsSpaceMap && !spawnpoint_IsInPlayable(pSpawn)) {
					ErrorFilenamef(layerGetFilename(pSpawn->pWorldPoint->common_data.layer),
									"Space Map has start spawn outside of playable volume");
				}
			}
		} FOR_EACH_END;
		
		if (iNumStartSpawns == 0 && 
			!zmapInfoIsTestMap(pZoneMapInfo) && 
			!zmapInfoGetNotPlayerVisited(pZoneMapInfo)) {
			if (pZoneMap) {
				ErrorFilenamef(zmapGetFilename(pZoneMap), "Map has no starting player spawn locations and it must have one");
			}
		}

		if (iNumStartSpawns > 1) {
			ErrorFilenamef(layerGetFilename(s_eaSpawnPoints[0]->pWorldPoint->common_data.layer),
						   "Map has %d starting player spawn locations and it may only have one", iNumStartSpawns);
		}

		if (!nullStr(startSpawnName) && !spawnpoint_GetByName(startSpawnName, NULL)) {
			ErrorFilenamef(zmapGetFilename(pZoneMap), "Map specifies start spawn \"%s\", but it does not exist.",
						   startSpawnName);
		}
	}

	// Check that no two spawn points have the same name on the map
	FOR_EACH_SPAWN_POINT(pSpawn1) {
		FOR_EACH_SPAWN_POINT2(pSpawn1, pSpawn2) {
			if (stricmp(pSpawn1->pcName, pSpawn2->pcName) == 0) {
				ErrorFilenamef(layerGetFilename(pSpawn1->pWorldPoint->common_data.layer),
							   "Map has two spawn points with name '%s'.  All spawn points must have unique names.", pSpawn1->pcName);
			}
		} FOR_EACH_END;
	} FOR_EACH_END;

	// Check that all referenced volumes actually exist
	FOR_EACH_SPAWN_POINT(pSpawn) {
		WorldScope *pScope = pSpawn->pWorldPoint->common_data.closest_scope;
		
		for(j=eaSize(&pSpawn->pWorldPoint->properties->source_volume_names)-1; j>=0; --j) {
			char *pcVolName = pSpawn->pWorldPoint->properties->source_volume_names[j];
			if (!volume_VolumeExists(pcVolName, pScope)) {
				ErrorFilenamef(layerGetFilename(pSpawn->pWorldPoint->common_data.layer),
							   "Unable to find volume '%s' referenced by spawn point '%s'", pcVolName, pSpawn->pcName);
			}
			if (pcVolName && (strnicmp(pcVolName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0)) {
				ErrorFilenamef(layerGetFilename(pSpawn->pWorldPoint->common_data.layer), 
				"Spawn point '%s' references temporary volume name '%s'.  You need to give the volume a non-temporary name before referencing it from this location.", pSpawn->pcName, pcVolName);
			}
		}
	} FOR_EACH_END; 

	// Check that GOTOs cannot be activated
	FOR_EACH_SPAWN_POINT(pSpawn) {
		WorldSpawnProperties *pSpawnData = pSpawn->pWorldPoint->properties;
		if ((pSpawnData->spawn_type == SPAWNPOINT_GOTO) && pSpawnData->needs_activation) {
			ErrorFilenamef(layerGetFilename(pSpawn->pWorldPoint->common_data.layer),
						   "Spawn point '%s' is set to need activation, but it is not a start or respawn point", pSpawn->pcName);
		}
		if ((pSpawnData->spawn_type == SPAWNPOINT_GOTO) && (pSpawnData->active_cond)) {
			ErrorFilenamef(layerGetFilename(pSpawn->pWorldPoint->common_data.layer),
						   "Spawn point '%s' has a Spawn When condition, but it is not a start or respawn point", pSpawn->pcName);
		}
	} FOR_EACH_END;

	// Check that all respawn and startspawn spawn points are within .5ft of the ground
	if (!resNamespaceIsUGC(zmapInfoGetPublicName(pZoneMapInfo)) && !zmapInfoHasSpaceRegion(NULL))
	{
		FOR_EACH_SPAWN_POINT(pSpawn) {
			S32 foundWorldShort = 0;
			//S32 foundWorldLong = 0;
			F32 dist1 = worldGetPointFloorDistance(worldGetAnyActiveColl(), pSpawn->pWorldPoint->spawn_pos, 0.5f, 5.5f, &foundWorldShort);
			//F32 dist2 = worldGetPointFloorDistance(worldGetAnyActiveColl(), pSpawn->pWorldPoint->spawn_pos, 5.0f, 10.5f, &foundWorldLong);

			if(pSpawn->pWorldPoint->properties->spawn_type==SPAWNPOINT_GOTO)
				continue;

			if(!foundWorldShort)
			{
				ErrorFilenamef(layerGetFilename(pSpawn->pWorldPoint->common_data.layer),
								"Spawn point '%s' (%.2f %.2f %.2f) is too far from world geometry for beaconizing", 
								pSpawn->pcName, 
								vecParamsXYZ(pSpawn->pWorldPoint->spawn_pos));
			}
		} FOR_EACH_END;
	}
}


void spawnpoint_PartitionLoad(int iPartitionIdx)
{
	PERFINFO_AUTO_START_FUNC();

	// Create state on all spawn points
	FOR_EACH_SPAWN_POINT(pSpawn) {
		SpawnPointPartitionState *pState = eaGet(&pSpawn->eaPartitionStates, iPartitionIdx);
		if (!pState) {
			pState = calloc(1,sizeof(SpawnPointPartitionState));
			pState->iPartitionIdx = iPartitionIdx;
			eaSet(&pSpawn->eaPartitionStates, pState, iPartitionIdx);
		}
		spawnpoint_InitPartitionState(pSpawn, pState);
	} FOR_EACH_END;

	PERFINFO_AUTO_STOP();
}

void spawnpoint_PartitionUnload(int iPartitionIdx)
{
	// Free state on all spawn points
	FOR_EACH_SPAWN_POINT(pSpawn) {
		SpawnPointPartitionState *pState = eaGet(&pSpawn->eaPartitionStates, iPartitionIdx);
		if (pState) {
			spawnpoint_FreePartitionState(pState);
			eaSet(&pSpawn->eaPartitionStates, NULL, iPartitionIdx);
		}
	} FOR_EACH_END;
}

void spawnpoint_MapLoad(ZoneMap *pZoneMap)
{
	WorldZoneMapScope *pScope;
	int i;

	// Clear all data
	spawnpoint_ClearList();

	// Get zone map scope
	pScope = zmapGetScope(pZoneMap);

	// Find all spawn points in all scopes
	if (pScope) {
		for(i=eaSize(&pScope->spawn_points)-1; i>=0; --i) {
			const char *pcName = worldScopeGetObjectName(&pScope->scope, &pScope->spawn_points[i]->common_data);
			spawnpoint_AddWorldSpawn(pcName, pScope->spawn_points[i]);
		}
	}

	// Post-process after load
	spawnpoint_PostProcessAll();
}


void spawnpoint_MapUnload(void)
{
	spawnpoint_ClearList();
}


AUTO_RUN;
void spawnpoint_InitSystem(void)
{
	s_pSpawnPointContext = exprContextCreate();
	exprContextSetFuncTable(s_pSpawnPointContext, encPlayer_CreateExprFuncTable());
	exprContextSetAllowRuntimeSelfPtr(s_pSpawnPointContext);
	exprContextSetAllowRuntimePartition(s_pSpawnPointContext);

	exprRegisterLocationPrefix("spawnpoint", spawnpoint_ExprLocationResolveSpawnPoint, false);

	s_eSpawnPointVolumeType = wlVolumeTypeNameToBitMask("Spawn Point Volume");
}

