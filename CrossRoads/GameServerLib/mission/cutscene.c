/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiLib.h"
#include "aiStruct.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "EntityMovementDisableMovement.h"
#include "EntityMovementManager.h"
#include "EntitySavedData.h"
#include "EString.h"
#include "cutscene.h"
#include "cutscene_common.h"
#include "DoorTransitionCommon.h"
#include "ResourceManager.h"
#include "gslDoorTransition.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventSend.h"
#include "gslNamedPoint.h"
#include "gslMapTransfer.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "gslMapVariable.h"
#include "oldencounter_common.h"
#include "Character.h"
#include "cutscene_common.h"
#include "cutscene_common_h_ast.h"
#include "WorldGrid.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "Player.h"
#include "PowerAnimFX.h"
#include "PowersMovement.h"
#include "MapDescription.h"
#include "ResourceSystem_Internal.h"
#include "GameServerLib.h"
#include "mapstate_common.h"
#include "gslMapState.h"
#include "WorldVariable.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "cutscene_h_ast.h"
#include "Entity_h_ast.h"
#include "Player_h_ast.h"
#include "AutoGen/cutscene_h_ast.h"

// List of all players who are watching cutscenes (and may need encounters to spawn for them)
ActiveCutscene** g_ppActiveCutscenes = NULL;


#define CUTSCENE_BASE_DIR "defs/cutscenes"

static void cutscene_EndAllCutscenesInPartition(int iPartitionIdx);
static ActiveCutscene* cutscene_FindActiveCutscene(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID CutsceneDef* pCutscene);
static void cutscene_EndOnServer(SA_PARAM_NN_VALID ActiveCutscene* pActiveCutscene);

static void cutscene_RunActiveCutsceneActions(ActiveCutscene* pActiveCutscene, U32 curTime, bool bRunAll)
{
	S32 i;
	for (i = eaSize(&pActiveCutscene->eaActions)-1; i >= 0; i--)
	{
		F32 fCurActivateTime = pActiveCutscene->eaActions[i]->fActivateTime;
		if (bRunAll || (fCurActivateTime >= 0.0f && curTime > SEC_TO_ABS_TIME(fCurActivateTime) + pActiveCutscene->startTime))
		{
			if (pActiveCutscene->eaActions[i]->pCallback)
			{
				pActiveCutscene->eaActions[i]->pCallback(pActiveCutscene->eaActions[i]->pCallbackData);
				StructDestroy(parse_ActiveCutsceneAction, eaRemoveFast(&pActiveCutscene->eaActions,i));
			}
		}
	}
}

static void cutscene_RunActiveCutsceneActionsForEnt(ActiveCutscene* pActiveCutscene, Entity* pEnt)
{
	S32 i;
	for (i = eaSize(&pActiveCutscene->eaActions)-1; i >= 0; i--)
	{
		ActiveCutsceneAction* pAction = pActiveCutscene->eaActions[i];
		if (pAction->erOwner == entGetRef(pEnt))
		{
			pAction->pCallback(pActiveCutscene->eaActions[i]->pCallbackData);
			StructDestroy(parse_ActiveCutsceneAction, eaRemoveFast(&pActiveCutscene->eaActions,i));
		}
	}
}

void cutscene_MapValidateOffset(CutsceneDef *pCutsceneDef, CutsceneOffsetData *pOffsetData)
{
	if (pOffsetData->offsetType == CutsceneOffsetType_Actor && pOffsetData->pchStaticEncName && pOffsetData->pchActorName){
		GameEncounter *pEncounter = encounter_GetByName(pOffsetData->pchStaticEncName, NULL);
		if (pEncounter) {
			if (!encounter_HasActorName(pEncounter, pOffsetData->pchActorName)) {
				ErrorFilenamef(pCutsceneDef->filename, "Error: Cutscene references unknown actor '%s' in encounter '%s'", pOffsetData->pchActorName, pOffsetData->pchStaticEncName);
			}
		} else if (gConf.bAllowOldEncounterData) {
			OldStaticEncounter *pStaticEnc = oldencounter_StaticEncounterFromName(pOffsetData->pchStaticEncName);
			if (pStaticEnc){
				OldActor *pActor = pStaticEnc->spawnRule?oldencounter_FindDefActorByName(pStaticEnc->spawnRule, pOffsetData->pchActorName):NULL;
				if (!pActor){
					ErrorFilenamef(pCutsceneDef->filename, "Error: Cutscene references unknown actor '%s' in encounter '%s'", pOffsetData->pchActorName, pOffsetData->pchStaticEncName);
				}
			}
		} else {
			ErrorFilenamef(pCutsceneDef->filename, "Error: Cutscene references unknown encounter '%s'", pOffsetData->pchStaticEncName);
		}
	}
}

// Validation callback for CutsceneDefs on map load
void cutscene_MapValidate(void)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_hCutsceneDict);
	int i, j;
	const char *pcMapName = zmapInfoGetPublicName(NULL);

	for(i=eaSize(&pStruct->ppReferents)-1; i>=0; --i){
		CutsceneDef *pCutsceneDef = (CutsceneDef*)pStruct->ppReferents[i];

		// For cutscenes that take place on this map, validate all of the Actor positions
		if (pCutsceneDef && pCutsceneDef->pPathList && pCutsceneDef->pcMapName
			&& !stricmp(pCutsceneDef->pcMapName, pcMapName)) {
			
			for (j = eaSize(&pCutsceneDef->pPathList->ppPaths)-1; j>=0; --j){
				CutscenePath *pPath = pCutsceneDef->pPathList->ppPaths[j];
				cutscene_MapValidateOffset(pCutsceneDef, &pPath->common.main_offset);
				if(pPath->common.bTwoRelativePos)
					cutscene_MapValidateOffset(pCutsceneDef, &pPath->common.second_offset);
			}
		}
	}
}


int cutscenedef_Validate(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, CutsceneDef *pCutscene, U32 userID)
{
	int i, j;
	switch (eType)
	{
	case RESVALIDATE_POST_TEXT_READING:
		if (IS_HANDLE_ACTIVE(pCutscene->Subtitles.hMessage) && !GET_REF(pCutscene->Subtitles.hMessage))
			InvalidDataErrorf("Cutscene subtitle message %s not found.", REF_STRING_FROM_HANDLE(pCutscene->Subtitles.hMessage));
// 		if (pCutscene->pchCutsceneSound && !GET_REF(pCutscene->Subtitles.hMessage))
// 			InvalidDataErrorf("Cutscene has sound %s but no subtitles.", pCutscene->pchCutsceneSound);
		for ( i=0; i < eaSize(&pCutscene->ppSubtitleLists); i++ ) {
			for ( j=0; j < eaSize(&pCutscene->ppSubtitleLists[i]->ppSubtitlePoints); j++ ) {
				CutsceneSubtitlePoint *pPoint = pCutscene->ppSubtitleLists[i]->ppSubtitlePoints[j];
				if (IS_HANDLE_ACTIVE(pPoint->displaySubtitle.hMessage) && !GET_REF(pPoint->displaySubtitle.hMessage))
					InvalidDataErrorf("Cutscene subtitle message %s not found.", REF_STRING_FROM_HANDLE(pPoint->displaySubtitle.hMessage));
			}
		}
		if(pCutscene->pUIGenList)
			for ( i=0; i < eaSize(&pCutscene->pUIGenList->ppUIGenPoints); i++ ) {
				CutsceneUIGenPoint *pPoint = pCutscene->pUIGenList->ppUIGenPoints[i];
				if (IS_HANDLE_ACTIVE(pPoint->messageValue.hMessage) && !GET_REF(pPoint->messageValue.hMessage))
					InvalidDataErrorf("Cutscene subtitle message %s not found.", REF_STRING_FROM_HANDLE(pPoint->messageValue.hMessage));
			}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_STARTUP(Cutscenes) ASTRT_DEPS(AS_Messages);
void cutsceneDefs_Load(void)
{
	if (!IsClient())
	{
		resDictManageValidation(g_hCutsceneDict, cutscenedef_Validate);
		resLoadResourcesFromDisk(g_hCutsceneDict, CUTSCENE_BASE_DIR, ".cutscene", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	}
}

static bool cutscene_IsCutsceneEnt(Entity *pEnt, CutsceneDef* pCutscene)
{
	const char* pchActorName = NULL;
	const char* pchStaticEncName = NULL;
	int i;

	if (!pEnt || !pEnt->pCritter){
		return false;
	}
	
	if(!pCutscene || pCutscene->bSinglePlayer)
		return false;

	if(!eaSize(&pCutscene->ppCutsceneEnts)){
		return false;
	}

	if (pEnt->pCritter && pEnt->pCritter->encounterData.pGameEncounter) {
		pchStaticEncName = pEnt->pCritter->encounterData.pGameEncounter->pcName;
		pchActorName = encounter_GetActorName(pEnt->pCritter->encounterData.pGameEncounter, pEnt->pCritter->encounterData.iActorIndex);
	}
	else if(gConf.bAllowOldEncounterData && pEnt->pCritter && pEnt->pCritter->encounterData.parentEncounter){
		OldStaticEncounter* staticEnc = GET_REF(pEnt->pCritter->encounterData.parentEncounter->staticEnc);
		if(staticEnc){
			pchStaticEncName = staticEnc->name;
		}
		if(pEnt->pCritter->encounterData.sourceActor){
			pchActorName = pEnt->pCritter->encounterData.sourceActor->name;
		}
	}

	// Otherwise, this entity must match the given actor name and/or static encounter name
	for(i=eaSize(&pCutscene->ppCutsceneEnts)-1; i>=0; --i)
	{
		CutsceneEnt* pTargetEnt = pCutscene->ppCutsceneEnts[i];

		if(pTargetEnt->actorName && pTargetEnt->actorName[0]){
			if(!pchActorName || stricmp(pchActorName, pTargetEnt->actorName))
				continue;
		}

		if(pTargetEnt->staticEncounterName && pTargetEnt->staticEncounterName[0]){
			if(!pchStaticEncName || stricmp(pchStaticEncName, pTargetEnt->staticEncounterName))
				continue;
		}

		// Matched static encounter and/or actor name.  This entity is not frozen
		return true;
	}
	return false;
}

// This callback is used by the FSM code to check whether an entity has been frozen because of a cutscene
bool cutscene_IsEntFrozenCB(Entity* pEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	// If the map isn't frozen (if damage isn't disabled), no ents are frozen
	if(!encounter_IsDamageDisabled(iPartitionIdx))
	{
		return false;
	}
	else
	{
		// Otherwise, the entity is frozen unless it's in one of the cutscenes' lists of unfrozen entities
		int i, n = eaSize(&g_ppActiveCutscenes);
		for(i=0; i<n; i++)
		{
			CutsceneDef* pCutscene = g_ppActiveCutscenes[i]->pDef;

			if(g_ppActiveCutscenes[i]->iPartitionIdx != iPartitionIdx)
				continue;

			// If there are no ents listed for this cutscene, then no ents are frozen
			if (!eaSize(&pCutscene->ppCutsceneEnts) || cutscene_IsCutsceneEnt(pEnt, pCutscene)){
				return false;
			}
		}
		return true;
	}
}

AUTO_RUN;
void CutsceneSystemServerAutoRunInit(void)
{
	aiRegisterIsEntFrozenCallback(cutscene_IsEntFrozenCB);
}

static void cutsceneSendGroupDefsRecursive(ResourceCache *pCache, GroupDef *pParentDef)
{
	int i;
	char strid[256];

	if(!pParentDef)
		return;

	sprintf(strid, "%d", pParentDef->name_uid);
	resServerRequestSendResourceUpdate(resGetDictionary(OBJECT_LIBRARY_DICT), strid, NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE);

	for ( i=0; i < eaSize(&pParentDef->children); i++ ) {
		GroupChild *pChild = pParentDef->children[i];
		GroupDef *pGroupDef = groupChildGetDef(pParentDef, pChild, true);
		cutsceneSendGroupDefsRecursive(pCache, pGroupDef);
	}
}

static void cutsceneSendAllGroupDefs(ResourceCache *pCache, CutsceneDef *pCutscene)
{
	int i;
	if(!pCutscene)
		return;

	for ( i=0; i < eaSize(&pCutscene->ppObjectLists); i++ ) {
		CutsceneObjectList *pList = pCutscene->ppObjectLists[i];
		GroupDef *pDef = objectLibraryGetGroupDefByName(pList->pcObjectName, true);
		cutsceneSendGroupDefsRecursive(pCache, pDef);
	}
}

typedef struct CutsceneResFenceData 
{
	int iPartitionIdx;
	EntityRef entRef;
	F32 fTime;
	DoorTransitionType eTransitionType;
	CutsceneWorldVars *pCutsceneVars;
} CutsceneResFenceData;

static void playerStartCutsceneFenceCallback(U32 uFenceID, CutsceneResFenceData *pData)
{
	Entity *pPlayerEnt = entFromEntityRef(pData->iPartitionIdx, pData->entRef);
	if (pPlayerEnt)
	{
		Player *pPlayer = entGetPlayer(pPlayerEnt);
		ClientCmd_CutsceneStartOnClient(pPlayerEnt, pData->fTime, pData->eTransitionType, pData->pCutsceneVars);
	}
	free(pData);
}

// 
static void gslActiveCutscene_RevertUntargetableEnts(int iPartitionIdx, ActiveCutscene* pActiveCutscene)
{
	FOR_EACH_IN_EARRAY_INT(pActiveCutscene->piUntargetableEnts, EntityRef, erEnt)
	{
		Entity *pEnt = entFromEntityRef(iPartitionIdx, erEnt);
		if (pEnt)
		{
			entClearCodeFlagBits(pEnt, ENTITYFLAG_UNTARGETABLE);
		}
	}
	FOR_EACH_END

	eaiDestroy(&pActiveCutscene->piUntargetableEnts);
}

static void gslActiveCutscene_AddUntargetableEnt(ActiveCutscene* pActiveCutscene, Entity *pEnt)
{
	if (!(pEnt->myCodeEntityFlags & ENTITYFLAG_UNTARGETABLE))
	{
		entSetCodeFlagBits(pEnt, ENTITYFLAG_UNTARGETABLE);
		eaiPushUnique(&pActiveCutscene->piUntargetableEnts, entGetRef(pEnt));
	}
}

// makes the player and any pets untargetable if necessary
static void gslActiveCutscene_AddPlayerAndPetsToUntargetableEnts(int iPartitionIdx, ActiveCutscene* pActiveCutscene, Entity *pEnt)
{
	gslActiveCutscene_AddUntargetableEnt(pActiveCutscene, pEnt);

	if (pEnt->aibase && pEnt->aibase->team)
	{
		AITeam *pAITeam = pEnt->aibase->team;
		
		FOR_EACH_IN_EARRAY(pAITeam->members, AITeamMember, pMember)
		{
			if (pMember->memberBE && pMember->memberBE->erOwner == entGetRef(pEnt))
			{
				gslActiveCutscene_AddUntargetableEnt(pActiveCutscene, pMember->memberBE);
			}
		}
		FOR_EACH_END
	}
}

static void playerStartCutscene(int iPartitionIdx,
								SA_PARAM_NN_VALID Entity *pPlayerEnt, 
								SA_PARAM_NN_VALID ActiveCutscene* pActiveCutscene, 
								bool bIsArrivalTransition)
{
	Player* pPlayer = entGetPlayer(pPlayerEnt);
	if(pPlayer)
	{
		ResourceCache *pCache;
		CutsceneResFenceData *pFenceData;
		DoorTransitionType eTransitionType = kDoorTransitionType_Unspecified;
		F32 fTime = ABS_TIME_TO_SEC(ABS_TIME - pActiveCutscene->startTime);
		if (fTime < 1)
			fTime = 0;

		pCache = entGetResourceCache(pPlayerEnt);
		if(pCache)
			cutsceneSendAllGroupDefs(pCache, pActiveCutscene->pDef);

		if (pActiveCutscene->pMapDescription)
		{
			eTransitionType = kDoorTransitionType_Departure;
		}
		else if (bIsArrivalTransition)
		{
			eTransitionType = kDoorTransitionType_Arrival;
		}

		if(pPlayer->pCutscene)
		{
			// Find old cutscene the player was in and remove the player from it
			ActiveCutscene* pOldActiveCutscene = cutscene_FindActiveCutscene(pPlayerEnt, pPlayer->pCutscene);
			if (pOldActiveCutscene){
				int i;
				for(i=eaiSize(&pOldActiveCutscene->pPlayerRefs)-1; i>=0; --i) {
					if (pOldActiveCutscene->pPlayerRefs[i] == pPlayerEnt->myRef) {
						eaiRemoveFast(&pOldActiveCutscene->pPlayerRefs, i);
					}
				}
				cutscene_RunActiveCutsceneActionsForEnt(pOldActiveCutscene, pPlayerEnt);
			}
			StructDestroy(parse_CutsceneDef, pPlayer->pCutscene);
		}
		pPlayer->pCutscene = StructClone(parse_CutsceneDef, pActiveCutscene->pDef);

		// Automatically exit persistent stances for cutscenes
		if (pPlayerEnt->pChar && pPlayerEnt->pChar->pPowerRefPersistStance && !pPlayerEnt->pChar->bPersistStanceInactive)
		{
			character_EnterPersistStance(iPartitionIdx, pPlayerEnt->pChar, NULL, NULL, NULL, pmTimestamp(0), 0, false);
		}
		gslEntity_LockMovement( pPlayerEnt, true );

		if (SAFE_MEMBER(pActiveCutscene->pDef, bPlayersAreUntargetable))
		{
			gslActiveCutscene_AddPlayerAndPetsToUntargetableEnts(iPartitionIdx, pActiveCutscene, pPlayerEnt);
		}


		eaiPush(&pActiveCutscene->pPlayerRefs, pPlayerEnt->myRef);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);

		pFenceData = calloc(sizeof(CutsceneResFenceData), 1);
		pFenceData->iPartitionIdx = iPartitionIdx;
		pFenceData->entRef = entGetRef(pPlayerEnt);
		pFenceData->fTime = fTime;
		pFenceData->eTransitionType = eTransitionType;
		pFenceData->pCutsceneVars = pActiveCutscene->pCutsceneVars;
		if(pCache)
			resServerRequestFenceInstruction(pCache, playerStartCutsceneFenceCallback, pFenceData);
		else
			playerStartCutsceneFenceCallback(0, pFenceData);

	}
}

static void playerEndCutscene(SA_PARAM_NN_VALID Entity *pPlayerEnt)
{
	Player* pPlayer = entGetPlayer(pPlayerEnt);
	if(pPlayer)
	{
		// Unlock the player's movement, if it hasn't already been done
		gslEntity_UnlockMovement(pPlayerEnt);

		StructDestroy(parse_CutsceneDef, pPlayer->pCutscene);
		pPlayer->pCutscene = NULL;
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}
}

// Returns true if there are any active cutscene points nearby
bool cutscene_GetNearbyCutscenes(int iPartitionIdx, Vec3 centerPos, F32 dist)
{
	int i, n = eaSize(&g_ppActiveCutscenes);
	int j, k;

	for(i=0; i<n; i++)
	{
		CutsceneDef* pCutscene = g_ppActiveCutscenes[i]->pDef;
		if(g_ppActiveCutscenes[i]->iPartitionIdx != iPartitionIdx)
			continue;
		if(pCutscene)
		{
			// Check whether any of the location in this cutscene is near the encounter
			if(pCutscene->pPathList)
			{
				int pathCnt = eaSize(&pCutscene->pPathList->ppPaths);
				for( j=0; j < pathCnt; j++ )
				{
					CutscenePath *pPath = pCutscene->pPathList->ppPaths[j];
					int posCnt =  eaSize(&pPath->ppPositions);
					for( k=0; k < posCnt; k++ )
					{
						CutscenePathPoint* pos = pPath->ppPositions[k];
						if(distance3(pos->pos, centerPos) < dist)
							return true;
					}
				}
			}
			else
			{
				k = eaSize(&pCutscene->ppCamPositions);
				for(j=0; j<k; j++)
				{
					CutscenePos* pos = pCutscene->ppCamPositions[j];
					if(distance3(pos->vPos, centerPos) < dist)
						return true;
				}
			}
		}
	}
	return false;
}

void cutscene_GetPlayersInCutscenes(Entity*** playerEnts, int iPartitionIdx)
{
	int i;

	for(i = eaSize(&g_ppActiveCutscenes)-1; i >= 0; i--)
	{
		int j, k;
		ActiveCutscene* pActiveCutscene = g_ppActiveCutscenes[i];

		if(pActiveCutscene->iPartitionIdx != iPartitionIdx)
			continue;

		k = eaiSize(&pActiveCutscene->pPlayerRefs);
		for(j=0; j<k; j++)
		{
			// Check to make sure the player actually exists (hasn't left the server)
			Entity* pEnt = entFromEntityRef(iPartitionIdx, pActiveCutscene->pPlayerRefs[j]);
			if(pEnt && pEnt->myEntityType != GLOBALTYPE_NONE)
				eaPush(playerEnts, pEnt);
		}
	}
}

void cutscene_GetPlayersInCutscenesNearCritter(Entity *pCritterEnt, Entity*** playerEnts)
{
	int i;
	int iPartitionIdx = entGetPartitionIdx(pCritterEnt);
	for(i = eaSize(&g_ppActiveCutscenes)-1; i >= 0; i--)
	{
		ActiveCutscene* pActiveCutscene = g_ppActiveCutscenes[i];
		if(pActiveCutscene->iPartitionIdx != iPartitionIdx)
			continue;
		if (!eaSize(&pActiveCutscene->pDef->ppCutsceneEnts) || cutscene_IsCutsceneEnt(pCritterEnt, pActiveCutscene->pDef)){
			int j, k = eaiSize(&pActiveCutscene->pPlayerRefs);

			for(j=0; j<k; j++)
			{
				// Check to make sure the player actually exists (hasn't left the server)
				Entity* pEnt = entFromEntityRef(pActiveCutscene->iPartitionIdx, pActiveCutscene->pPlayerRefs[j]);
				if(pEnt && pEnt->myEntityType != GLOBALTYPE_NONE)
					eaPush(playerEnts, pEnt);
			}
		}
	}
}

ActiveCutscene* cutscene_ActiveCutsceneFromPlayer(Entity *pPlayerEnt)
{
	int i;
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pCutscene){
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		for(i = eaSize(&g_ppActiveCutscenes)-1; i >= 0; i--){
			if(g_ppActiveCutscenes[i]->iPartitionIdx != iPartitionIdx)
				continue;
			if (pPlayerEnt->pPlayer->pCutscene->name == g_ppActiveCutscenes[i]->pDef->name
				&& ea32Find(&g_ppActiveCutscenes[i]->pPlayerRefs, pPlayerEnt->myRef) >= 0){
				return g_ppActiveCutscenes[i];
			}
		}
	}
	return NULL;
}

static EntityRef cutscene_GetActor(int iPartitionIdx, const char *pchStaticEncName, const char *pchActorName, Entity *pEntViewer)
{
	Entity *pEntity = NULL;
	GameEncounter *pEncounter = encounter_GetByName(pchStaticEncName, NULL);
	if (pEncounter) {
		pEntity = encounter_GetActorEntity(iPartitionIdx, pEncounter, pchActorName);
	} else if (gConf.bAllowOldEncounterData) {
		pEntity = oldencounter_EntFromEncActorName(iPartitionIdx, pchStaticEncName, pchActorName);
	}
	if(pEntity) {
		return pEntity->myRef;
	}
	return 0;
}

static void cutscene_FindCGTEntities(int iPartitionIdx, CutsceneDummyTrack *pCGT, Entity *pEntViewer)
{
	if(	pCGT->common.bRelativePos &&
		pCGT->common.main_offset.offsetType == CutsceneOffsetType_Actor && 
		pCGT->common.main_offset.pchStaticEncName && 
		pCGT->common.main_offset.pchActorName )
	{
		pCGT->common.main_offset.entRef = cutscene_GetActor(iPartitionIdx, pCGT->common.main_offset.pchStaticEncName, pCGT->common.main_offset.pchActorName, pEntViewer);
	}
	if(	pCGT->common.bRelativePos && pCGT->common.bTwoRelativePos &&
		pCGT->common.second_offset.offsetType == CutsceneOffsetType_Actor && 
		pCGT->common.second_offset.pchStaticEncName && 
		pCGT->common.second_offset.pchActorName )
	{
		pCGT->common.second_offset.entRef = cutscene_GetActor(iPartitionIdx, pCGT->common.second_offset.pchStaticEncName, pCGT->common.second_offset.pchActorName, pEntViewer);
	}
}

static void cutscene_FindCGTListEntities(int iPartitionIdx, void **ppCGTs, Entity *pEntViewer)
{
	int i;
	for ( i=0; i < eaSize(&ppCGTs); i++ ) {
		cutscene_FindCGTEntities(iPartitionIdx, ppCGTs[i], pEntViewer);
	}
}

static void cutscene_TranslateMessages(int iPartitionIdx, CutsceneDef* pCutscene, Entity *pEntViewer)
{
	int i;
	if(pCutscene->pUIGenList)
	{
		for(i = 0; i < eaSize(&pCutscene->pUIGenList->ppUIGenPoints); i++)
		{
			CutsceneUIGenPoint *pPoint = pCutscene->pUIGenList->ppUIGenPoints[i];
			if(pPoint->pcMessageValueVariable && pPoint->pcMessageValueVariable[0])
			{
				MapVariable *pMapVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, pPoint->pcMessageValueVariable);
				if(pMapVar && pMapVar->pVariable && pMapVar->pVariable->eType == WVAR_MESSAGE)
					pPoint->pcTranslatedMessage = strdup(langTranslateMessageRef(entGetLanguage(pEntViewer), pMapVar->pVariable->messageVal.hMessage));
			}

			if(!pPoint->pcTranslatedMessage || pPoint->pcTranslatedMessage[0] == '\0')
				pPoint->pcTranslatedMessage = strdup(langTranslateMessageRef(entGetLanguage(pEntViewer), pPoint->messageValue.hMessage));
		}
	}

	for(i = 0; i < eaSize(&pCutscene->ppSubtitleLists); i++)
	{
		int j;
		for(j = 0; j < eaSize(&pCutscene->ppSubtitleLists[i]->ppSubtitlePoints); j++)
		{
			CutsceneSubtitlePoint *pPoint = pCutscene->ppSubtitleLists[i]->ppSubtitlePoints[j];
			if(pPoint->pcSubtitleVariable && pPoint->pcSubtitleVariable[0])
			{
				MapVariable *pMapVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, pPoint->pcSubtitleVariable);
				if(pMapVar && pMapVar->pVariable && pMapVar->pVariable->eType == WVAR_MESSAGE)
					pPoint->pcTranslatedSubtitle = strdup(langTranslateMessageRef(entGetLanguage(pEntViewer), pMapVar->pVariable->messageVal.hMessage));
			}

			if(!pPoint->pcTranslatedSubtitle || pPoint->pcTranslatedSubtitle[0] == '\0')
				pPoint->pcTranslatedSubtitle = strdup(langTranslateMessageRef(entGetLanguage(pEntViewer), pPoint->displaySubtitle.hMessage));
		}
	}

	pCutscene->pcTranslatedSubtitles = strdup(langTranslateMessageRef(entGetLanguage(pEntViewer), pCutscene->Subtitles.hMessage));
}

static void cutscene_FindEntities(int iPartitionIdx, CutsceneDef* pCutscene, Entity *pEntViewer)
{
	int i;
	for ( i=0; i < eaSize(&pCutscene->ppEntityLists); i++ ) {
		CutsceneEntityList *pList = pCutscene->ppEntityLists[i];
		if(	pList->entityType == CutsceneEntityType_Actor &&
			pList->pchStaticEncName && 
			pList->pchActorName ) 
		{
			pList->entActorRef = cutscene_GetActor(iPartitionIdx, pList->pchStaticEncName, pList->pchActorName, pEntViewer);
		}
	}

	// CutsceneEffectsAndEvents
	// If it has locations, get ent refs for potential offsets
	cutscene_FindCGTListEntities(iPartitionIdx, pCutscene->ppObjectLists,	pEntViewer);
	cutscene_FindCGTListEntities(iPartitionIdx, pCutscene->ppEntityLists,	pEntViewer);
	cutscene_FindCGTListEntities(iPartitionIdx, pCutscene->ppFXLists,		pEntViewer);
	cutscene_FindCGTListEntities(iPartitionIdx, pCutscene->ppSoundLists,	pEntViewer);
}

static void cutscene_FixupPointPositionOffsets(int iPartitionIdx, CutsceneOffsetData *pOffsetData, Entity *pEntViewer)
{
	//Find the entity refs for entities used in this cut scene
	if(pOffsetData->offsetType == CutsceneOffsetType_Actor && pOffsetData->pchStaticEncName && pOffsetData->pchActorName)
	{
		Entity *pEntity = NULL;
		GameEncounter *pEncounter = encounter_GetByName(pOffsetData->pchStaticEncName, NULL);
		if (pEncounter) {
			pEntity = encounter_GetActorEntity(iPartitionIdx, pEncounter, pOffsetData->pchActorName);
		} else if (gConf.bAllowOldEncounterData) {
			pEntity = oldencounter_EntFromEncActorName(iPartitionIdx, pOffsetData->pchStaticEncName, pOffsetData->pchActorName);
		}
		if(pEntity)
			pOffsetData->entRef = pEntity->myRef;
	}
}

static void cutscene_FixupPointPositions(int iPartitionIdx, CutsceneDef* pCutscene, Entity *pEntViewer)
{
	int i, n;

	if(!pCutscene)
		return;

	if(pCutscene->pPathList)
	{
		n = eaSize(&pCutscene->pPathList->ppPaths);
		for( i=0; i < n; i++ )
		{
			CutscenePath *pPath = pCutscene->pPathList->ppPaths[i];
			cutscene_FixupPointPositionOffsets(iPartitionIdx, &pPath->common.main_offset, pEntViewer);
			if(pPath->common.bTwoRelativePos)
				cutscene_FixupPointPositionOffsets(iPartitionIdx, &pPath->common.second_offset, pEntViewer);
		}
	}

	n = eaSize(&pCutscene->ppCamPositions);
	for(i=0; i<n; i++)
	{
		CutscenePos* pPos = pCutscene->ppCamPositions[i];
		if(pPos->pchNamedPoint && pPos->pchNamedPoint[0])
		{
			// Copy the named point's position to the def
			GameNamedPoint *point = namedpoint_GetByName(pPos->pchNamedPoint, NULL);
			if(point)
			{
				namedpoint_GetPosition(point, pPos->vPos, NULL);
			}
			else
			{
				ErrorFilenamef(pCutscene->filename, "Cutscene %s specifies unknown point %s", pCutscene->name, pPos->pchNamedPoint);
			}
		}
	}
	// Do the same for target positions
	n = eaSize(&pCutscene->ppCamTargets);
	for(i=0; i<n; i++)
	{
		CutscenePos* pPos = pCutscene->ppCamTargets[i];
		if(pPos->pchNamedPoint && pPos->pchNamedPoint[0])
		{
			// Copy the named point's position to the def
			GameNamedPoint *point = namedpoint_GetByName(pPos->pchNamedPoint, NULL);
			if(point)
			{
				namedpoint_GetPosition(point, pPos->vPos, NULL);
			}
			else
			{
				ErrorFilenamef(pCutscene->filename, "Cutscene %s specifies unknown point %s", pCutscene->name, pPos->pchNamedPoint);
			}
		}
	}
}

void cutscene_FillActiveCutsceneVars(ActiveCutscene *pActiveCutscene)
{
	int i, j;
	MapVariable *pMapVar = NULL;

	for(i=0;i<eaSize(&pActiveCutscene->pDef->ppTexLists);i++)
	{
		pMapVar = NULL;
		if(pActiveCutscene->pDef->ppTexLists[i]->pcTextureVariable)
			pMapVar = mapvariable_GetByNameIncludingCodeOnly(pActiveCutscene->iPartitionIdx, pActiveCutscene->pDef->ppTexLists[i]->pcTextureVariable);

		if(pMapVar)
		{
			if(!pActiveCutscene->pCutsceneVars)
				pActiveCutscene->pCutsceneVars = StructCreate(parse_CutsceneWorldVars);

			eaPush(&pActiveCutscene->pCutsceneVars->eaWorldVars, StructClone(parse_WorldVariable, pMapVar->pVariable));
		}
	}

	for(i=0;i<eaSize(&pActiveCutscene->pDef->ppSoundLists);i++)
	{
		for(j=0;j<eaSize(&pActiveCutscene->pDef->ppSoundLists[i]->ppSoundPoints);j++)
		{
			pMapVar = NULL;
			if(pActiveCutscene->pDef->ppSoundLists[i]->ppSoundPoints[j]->pcSoundVariable)
				pMapVar = mapvariable_GetByNameIncludingCodeOnly(pActiveCutscene->iPartitionIdx, pActiveCutscene->pDef->ppSoundLists[i]->ppSoundPoints[j]->pcSoundVariable);

			if(pMapVar)
			{
				if(!pActiveCutscene->pCutsceneVars)
					pActiveCutscene->pCutsceneVars = StructCreate(parse_CutsceneWorldVars);

				eaPush(&pActiveCutscene->pCutsceneVars->eaWorldVars, StructClone(parse_WorldVariable, pMapVar->pVariable));
			}
		}
	}

	for(i=0;i<eaSize(&pActiveCutscene->pDef->ppSubtitleLists);i++)
	{
		for(j=0;j<eaSize(&pActiveCutscene->pDef->ppSubtitleLists[i]->ppSubtitlePoints);j++)
		{
			pMapVar = NULL;
			if(pActiveCutscene->pDef->ppSubtitleLists[i]->ppSubtitlePoints[j]->pcSubtitleVariable)
				pMapVar = mapvariable_GetByNameIncludingCodeOnly(pActiveCutscene->iPartitionIdx, pActiveCutscene->pDef->ppSubtitleLists[i]->ppSubtitlePoints[j]->pcSubtitleVariable);

			if(pMapVar)
			{
				if(!pActiveCutscene->pCutsceneVars)
					pActiveCutscene->pCutsceneVars = StructCreate(parse_CutsceneWorldVars);

				eaPush(&pActiveCutscene->pCutsceneVars->eaWorldVars, StructClone(parse_WorldVariable, pMapVar->pVariable));
			}
		}
	}

	if(pActiveCutscene->pDef->pUIGenList)
		for(i=0;i<eaSize(&pActiveCutscene->pDef->pUIGenList->ppUIGenPoints);i++)
		{
			pMapVar = NULL;
			if(pActiveCutscene->pDef->pUIGenList->ppUIGenPoints[i]->pcMessageValueVariable)
				pMapVar = mapvariable_GetByNameIncludingCodeOnly(pActiveCutscene->iPartitionIdx, pActiveCutscene->pDef->pUIGenList->ppUIGenPoints[i]->pcMessageValueVariable);

			if(pMapVar)
			{
				if(!pActiveCutscene->pCutsceneVars)
					pActiveCutscene->pCutsceneVars = StructCreate(parse_CutsceneWorldVars);

				eaPush(&pActiveCutscene->pCutsceneVars->eaWorldVars, StructClone(parse_WorldVariable, pMapVar->pVariable));
			}
		}
}

static bool s_bDisableAllCutscenes;
AUTO_CMD_INT(s_bDisableAllCutscenes, DisableAllCutscenes) ACMD_ACCESSLEVEL(7) ACMD_COMMANDLINE;

#define CUT_SCENE_ENTITY_RANGE 750.0f
bool cutscene_StartOnServerEx(CutsceneDef* pCutscene, Entity* pEnt, int iPartitionIdx, MapDescription* pMapDesc, U32 eTransferFlags, bool bIsArrivalTransition)
{
	Character* pChar = pEnt ? pEnt->pChar : NULL;
	Player* pPlayer = pEnt ? entGetPlayer(pEnt) : NULL;

	if(pCutscene && (pPlayer || !pCutscene->bSinglePlayer) && !partition_IsBeingDestroyed(iPartitionIdx)
			&& (!pCutscene->bPlayOnceOnly || !mapState_HasCutscenePlayed(iPartitionIdx, pCutscene->name))
			&& (!s_bDisableAllCutscenes))
	{
		ActiveCutscene* pActiveCutscene = NULL;

		// If this cutscene is already happening, don't start it again
		if (!pCutscene->bSinglePlayer && cutscene_FindActiveCutsceneByName(pCutscene->name, iPartitionIdx)){
			return false;
		}

		pActiveCutscene = StructCreate(parse_ActiveCutscene);
		pActiveCutscene->iPartitionIdx = iPartitionIdx;

		if ( pMapDesc )
		{
			pActiveCutscene->pMapDescription = StructClone( parse_MapDescription, pMapDesc );
		}
		
		pActiveCutscene->eTransferFlags = eTransferFlags;

		// Copy the cutscene def to the server (in case the player logs out or something)
		pActiveCutscene->pDef = StructClone(parse_CutsceneDef, pCutscene);

		// Record the start and end times for this scene, and add it to the global list
		pActiveCutscene->startTime = ABS_TIME;
		// Add an extra second to the length, to compensate for rounding errors and slow loading
		// Update: we might need to update this to reflect the game clients new waiting for AIAnimLists behavior which has a 1 second timeout
		//		This is important since the end time set here can effect when a player is target-able again.. sooo... the server might be letting
		//		a player get beat up or killed while the player is stuck viewing a cutscene. We also don't want to make this too long, since then
		//		it could be exploited.
		pActiveCutscene->endTime = pActiveCutscene->startTime + SEC_TO_ABS_TIME(cutscene_GetLength(pActiveCutscene->pDef, true) + 1);

		// Translate messages
		cutscene_TranslateMessages(iPartitionIdx, pActiveCutscene->pDef, pEnt);

		// Find entiy refs for CGTs
		cutscene_FindEntities(iPartitionIdx, pActiveCutscene->pDef, pEnt);

		// If the cutscene has any named points, find the location of those points
		cutscene_FixupPointPositions(iPartitionIdx, pActiveCutscene->pDef, pEnt);

		// If the cutscene has any world variables associated with it, fill those in to make sure they are up to date
		cutscene_FillActiveCutsceneVars(pActiveCutscene);

		// Start the cutscene for this player or or all players
		if(pCutscene->bSinglePlayer)
		{
			playerStartCutscene(iPartitionIdx, pEnt, pActiveCutscene, bIsArrivalTransition);
		}
		else
		{
			// Iterate over all players, and start the cutscene for each
			EntityIterator* iter;
			Entity* currEnt = NULL;
			
			// Force end the existing active cutscene on the server if the player
			// was in a map-wide (not single player) cutscene.  This happens if someone
			// tries to start a second map wide cutscene overlapping a running one.
			if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pCutscene) {
				ActiveCutscene *pOldActiveCutscene = cutscene_FindActiveCutscene(pEnt, pEnt->pPlayer->pCutscene); 
				if (pOldActiveCutscene && !pOldActiveCutscene->pDef->bSinglePlayer) {
					cutscene_EndOnServer(pOldActiveCutscene);
				}
			}

			// Start the cutscene for all players
			iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter)))
			{
				playerStartCutscene(iPartitionIdx, currEnt, pActiveCutscene, bIsArrivalTransition);
			}
			EntityIteratorRelease(iter);

			encounter_SetDamageDisabled(iPartitionIdx);

			if (eaSize(&pCutscene->ppCutsceneEnts)){
				// "Freeze" the map
				aiSetCheckForFrozenEnts(iPartitionIdx, true);

				// Send a callback for all "CutsceneEnts"
				iter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYCRITTER);
				currEnt = NULL;
				while ((currEnt = EntityIteratorGetNext(iter)))
				{
					if (cutscene_IsCutsceneEnt(currEnt, pCutscene)){
						aiCutsceneEntStartCallback(currEnt);
					}
				}
				EntityIteratorRelease(iter);
			}
		}

		// Send an event that the cutscene is starting
		eventsend_RecordCutsceneStart(iPartitionIdx, pCutscene->name, &pActiveCutscene->pPlayerRefs);

		if(pCutscene->bPlayOnceOnly)
			mapState_CutscenePlayed(iPartitionIdx, pCutscene->name);

		// Add the cutscene to the global list
		eaPush(&g_ppActiveCutscenes, pActiveCutscene);

		// Go through all entities and update their visual range. This is based on gConfig settings
		if(pCutscene->fMinCutSceneSendRange > 0.0f && zmapInfoGetMapType(NULL) != ZMTYPE_STATIC)
		{
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYCRITTER);
			Entity* currEnt = NULL;
			while ((currEnt = EntityIteratorGetNext(iter)))
			{
				// Add check vs cameras to update...
				S32 i;
				Vec3 vecTarget;

				entGetPos(currEnt, vecTarget);

				if(pCutscene->pPathList)
				{
					for(i=0; i < eaSize(&pCutscene->pPathList->ppPaths); ++i)
					{
						const CutscenePath *pPath = pCutscene->pPathList->ppPaths[i];
						S32 j;
						for(j=0; j < eaSize(&pPath->ppPositions); ++j)
						{
							F32 fDist = distance3Squared(pPath->ppPositions[j]->pos,vecTarget);
							if(fDist <= SQR(CUT_SCENE_ENTITY_RANGE))
							{
								// increase send radius
								gslEntityUpdateSendDistance(currEnt);
								break;
							}
						}
					}
				}
				else if(eaSize(&pCutscene->ppCamPositions) > 0)
				{
					for(i=0;i<eaSize(&pCutscene->ppCamPositions);i++)
					{
						F32 fDist = distance3Squared(pCutscene->ppCamPositions[i]->vPos,vecTarget);
						if(fDist <= SQR(CUT_SCENE_ENTITY_RANGE))
						{
							// increase send radius
							gslEntityUpdateSendDistance(currEnt);
							break;
						}
					}
				}
				else
				{
					// no idea of camera positions, just add it
					gslEntityUpdateSendDistance(currEnt);
				}

			}
			EntityIteratorRelease(iter);
		}

		return true;
	}

	return false;
}

bool cutscene_StartOnServer(CutsceneDef* pCutscene, Entity* pEnt, bool bIsArrivalTransition)
{
	return cutscene_StartOnServerEx(pCutscene, pEnt, entGetPartitionIdx(pEnt), NULL, 0, bIsArrivalTransition);
}

static ActiveCutscene* cutscene_FindActiveCutscene(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID CutsceneDef* pCutscene)
{
	int i, n = eaSize(&g_ppActiveCutscenes);
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	for(i=0; i<n; i++)
	{
		ActiveCutscene* pActiveCutscene = g_ppActiveCutscenes[i];
		if(pActiveCutscene->iPartitionIdx != iPartitionIdx)
			continue;
		// Compare of string pointers is safe, since they're pooled
		if(pActiveCutscene->pDef->name != pCutscene->name)
			continue;
		// See if the current ent is watching this cutscene
		if(eaiFind(&pActiveCutscene->pPlayerRefs, pEnt->myRef) >= 0)
			return pActiveCutscene;
	}
	return NULL;
}

ActiveCutscene* cutscene_FindActiveCutsceneByName(const char* cutsceneName, int iPartitionIdx)
{
	int i, n = eaSize(&g_ppActiveCutscenes);

	for(i=0; i<n; i++)
	{
		ActiveCutscene* pActiveCutscene = g_ppActiveCutscenes[i];
		if(pActiveCutscene->iPartitionIdx != iPartitionIdx)
			continue;

		if(0 == stricmp(pActiveCutscene->pDef->name, cutsceneName))
			return pActiveCutscene;
	}
	return NULL;
}

static void cutscene_MapMovePlayers(SA_PARAM_NN_VALID ActiveCutscene* pActiveCutscene)
{
	int i,n;
	if ( pActiveCutscene->pMapDescription==NULL )
		return;
	// Move all players in the cutscene
	n = eaiSize(&pActiveCutscene->pPlayerRefs);
	for(i=0; i<n; i++)
	{
		Entity* currEnt = entFromEntityRef(pActiveCutscene->iPartitionIdx, pActiveCutscene->pPlayerRefs[i]);

		if(currEnt)
		{
			gslEntitySetTransitionSequenceFlags( currEnt, ENTITYFLAG_DONOTDRAW|ENTITYFLAG_DONOTFADE, true );
			MapMoveOrSpawnWithDescription(currEnt,pActiveCutscene->pMapDescription,GetVerboseMapMoveComment(currEnt, "cutscene_MapMovePlayers, CS %s", pActiveCutscene->pDef->name), pActiveCutscene->eTransferFlags);
		}
	}
}

static void cutscene_UnfreezePlayers(SA_PARAM_NN_VALID ActiveCutscene* pActiveCutscene)
{
	int i,n;
	// Unlock movement for all players in the cutscene
	n = eaiSize(&pActiveCutscene->pPlayerRefs);
	for(i=0; i<n; i++)
	{
		Entity* currEnt = entFromEntityRef(pActiveCutscene->iPartitionIdx, pActiveCutscene->pPlayerRefs[i]);

		if(currEnt)
		{
			gslEntity_UnlockMovement(currEnt);
		}
	}
}

static void cutscene_EndOnServer(SA_PARAM_NN_VALID ActiveCutscene* pActiveCutscene)
{
	int i,n;
	CutsceneDef *pDef = pActiveCutscene->pDef;

	if(!pDef)
	{
		return;
	}

	gslActiveCutscene_RevertUntargetableEnts(pActiveCutscene->iPartitionIdx, pActiveCutscene);

	// End cutscene for all players who are in it
	n = eaiSize(&pActiveCutscene->pPlayerRefs);
	for(i=0; i<n; i++)
	{
		Entity* currEnt = entFromEntityRef(pActiveCutscene->iPartitionIdx, pActiveCutscene->pPlayerRefs[i]);

		if(currEnt)
		{
			playerEndCutscene(currEnt);
		}
	}

	if(zmapInfoGetMapType(NULL) != ZMTYPE_STATIC)
	{
		if(pDef->fMinCutSceneSendRange > 0.0f)
		{
			// all ents need send distance change
			EntityIterator *iter = entGetIteratorSingleType(pActiveCutscene->iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYCRITTER);
			Entity *currEnt = NULL;
			while ((currEnt = EntityIteratorGetNext(iter)))
			{
				// decrease send radius
				gslEntityUpdateSendDistance(currEnt);
			}
			EntityIteratorRelease(iter);
		}
	}

	// Unfreeze the map
	if(!pDef->bSinglePlayer)
	{
		encounter_RemoveDamageDisabled(pActiveCutscene->iPartitionIdx);

		if (eaSize(&pActiveCutscene->pDef->ppCutsceneEnts)){
			EntityIterator *iter = entGetIteratorSingleType(pActiveCutscene->iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYCRITTER);
			Entity *currEnt = NULL;

			// If this was the last cutscene, unfreeze all frozen ents
			if(eaSize(&g_ppActiveCutscenes) == 1)
				aiSetCheckForFrozenEnts(pActiveCutscene->iPartitionIdx, false);

			// Send a callback for all "CutsceneEnts"
			while ((currEnt = EntityIteratorGetNext(iter)))
			{
				if (cutscene_IsCutsceneEnt(currEnt, pActiveCutscene->pDef)){
					aiCutsceneEntEndCallback(currEnt);
				}
			}
			EntityIteratorRelease(iter);

			aiRewindAIFSMTime(pActiveCutscene->iPartitionIdx, ABS_TIME - pActiveCutscene->startTime);
		}
	}

	eaFindAndRemove(&g_ppActiveCutscenes, pActiveCutscene);

	//run any remaining actions
	cutscene_RunActiveCutsceneActions( pActiveCutscene, 0, true );

	//if there is a map description on this cutscene, then map transfer all attached entities
	if ( pActiveCutscene->pMapDescription )
	{
		cutscene_MapMovePlayers(pActiveCutscene);
	}

	// Send an event that the cutscene has ended
	eventsend_RecordCutsceneEnd(pActiveCutscene->iPartitionIdx, pDef->name, &pActiveCutscene->pPlayerRefs);

	StructDestroy(parse_ActiveCutscene, pActiveCutscene);
}

void cutscene_PlayerSkipCutscene(Entity* pEnt, bool bForce)
{
	Player* pPlayer = pEnt ? entGetPlayer(pEnt) : NULL;

	if(pPlayer && pPlayer->pCutscene)
	{
		if (bForce || (pPlayer->pCutscene->bSinglePlayer && !pPlayer->pCutscene->bUnskippable))
		{
			ActiveCutscene* pActiveCutscene = cutscene_FindActiveCutscene(pEnt, pPlayer->pCutscene);
			if(pActiveCutscene){
				cutscene_EndOnServer(pActiveCutscene);
				ClientCmd_CutsceneEndOnClient(pEnt);
			}
		}
	}
}

static void cutscene_EstimateCameraPos(ActiveCutscene* pActiveCutscene)
{
	if (pActiveCutscene){
		CutsceneDef *pDef = pActiveCutscene->pDef;
		F32 fRemainingTime = pActiveCutscene->fElapsedTime;
		
		if (pDef->pPathList){
			CutscenePath *pCurrentPath = NULL;
			CutscenePathPoint *pCurrentPoint = NULL;
			CutscenePathPoint *pLastPoint = NULL;
			bool bFound = false;
			int i, j;
			for (i = 0; i < eaSize(&pDef->pPathList->ppPaths); i++){
				pCurrentPath = pDef->pPathList->ppPaths[i];
				for (j = 0; j < eaSize(&pCurrentPath->ppPositions); j++){
					pLastPoint = pCurrentPoint;
					pCurrentPoint = pCurrentPath->ppPositions[j];

					if (pActiveCutscene->fElapsedTime < pCurrentPoint->common.time){
						bFound = true;
						break;
					}
				}
				if (bFound){
					break;
				}
			}

			if(pCurrentPoint) {
				F32 prevTime = (pLastPoint ? pLastPoint->common.time : 0);
				F32 moveTime = pCurrentPoint->common.time - prevTime;
				fRemainingTime -= prevTime;
				if (pLastPoint && moveTime && (fRemainingTime < moveTime) && bFound) {
					interpVec3(fRemainingTime/moveTime, pLastPoint->pos, pCurrentPoint->pos, pActiveCutscene->estimatedCameraPos);
				} else {
					copyVec3(pCurrentPoint->pos, pActiveCutscene->estimatedCameraPos);
				}
			}
		} else {
			CutscenePos *pLastPoint = NULL;
			CutscenePos *pCurrentPoint = NULL;
			bool bFound = false;
			int i;
			for (i = 0; i < eaSize(&pDef->ppCamPositions); i++){
				pLastPoint = pCurrentPoint;
				pCurrentPoint = pDef->ppCamPositions[i];
				if (fRemainingTime < pCurrentPoint->fMoveTime + pCurrentPoint->fHoldTime){
					bFound = true;
					break;
				}
				fRemainingTime -= pCurrentPoint->fMoveTime;
				fRemainingTime -= pCurrentPoint->fHoldTime;
			}

			if (pLastPoint && pCurrentPoint && pCurrentPoint->fMoveTime && (fRemainingTime < pCurrentPoint->fMoveTime) && bFound){
				interpVec3((fRemainingTime/pCurrentPoint->fMoveTime), pLastPoint->vPos, pCurrentPoint->vPos, pActiveCutscene->estimatedCameraPos);
			} else if (pCurrentPoint) {
				copyVec3(pCurrentPoint->vPos, pActiveCutscene->estimatedCameraPos);
			}
		}
	}
}


void cutscene_UpdateActiveCutscenes(F32 fTimeStep)
{
	U32 curTime = ABS_TIME;
	int i, n = eaSize(&g_ppActiveCutscenes);
	for(i=n-1; i>=0; i--)
	{
		ActiveCutscene* pActiveCutscene = g_ppActiveCutscenes[i];

		if (partition_IsBeingDestroyed(pActiveCutscene->iPartitionIdx))
		{
			Errorf("Somehow a cutscene started again after partition destroy started on partition %d.  Cleaning up.", pActiveCutscene->iPartitionIdx);
			cutscene_EndAllCutscenesInPartition(pActiveCutscene->iPartitionIdx);
			return;
		}

		g_ppActiveCutscenes[i]->fElapsedTime += fTimeStep;
		cutscene_EstimateCameraPos(g_ppActiveCutscenes[i]);

		if ( eaSize(&pActiveCutscene->eaActions) > 0 )
		{
			cutscene_RunActiveCutsceneActions(pActiveCutscene, curTime, false);
		}

		if(curTime > pActiveCutscene->endTime - SEC_TO_ABS_TIME(2))
		{
			// Unlock the player's controls before the cutscene ends, so that by the time the
			// camera returns to their character on the client they can move.  Otherwise, there
			// can be a second or two when the movement locking hasn't worn off.
			cutscene_UnfreezePlayers(pActiveCutscene);
		}

		if(curTime > pActiveCutscene->endTime)
		{
			// This will remove the entity from the array
			cutscene_EndOnServer(pActiveCutscene);
		}
	}
}

#define CUTSCENE_SYSTEM_TICK  10

void cutscene_OncePerFrame(F32 fTimeStep)
{
	static int s_CutsceneTick = 0;
	static F32 fTotalTime = 0;
	fTotalTime += fTimeStep;

	// Cutscenes
	++s_CutsceneTick;
	if((s_CutsceneTick % CUTSCENE_SYSTEM_TICK) == 0)
	{
		cutscene_UpdateActiveCutscenes(fTotalTime);
		fTotalTime = 0.f;
	}

}

void cutscene_PlayerEnteredMap(Entity *pEnt)
{
	// Nothing here for now, see cutscene_PlayerDoneLoading
}

void cutscene_PlayerDoneLoading(Entity *pEnt)
{
	// Find a map-wide cutscene to join
	if (pEnt && pEnt->pPlayer && !pEnt->pPlayer->pCutscene)
	{
		int i;
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		for (i = eaSize(&g_ppActiveCutscenes)-1; i >= 0; --i){
			if(!g_ppActiveCutscenes[i])
				continue;
			if(g_ppActiveCutscenes[i]->iPartitionIdx != iPartitionIdx)
				continue;
			if (g_ppActiveCutscenes[i]->pDef && !g_ppActiveCutscenes[i]->pDef->bSinglePlayer){
				playerStartCutscene(iPartitionIdx, pEnt, g_ppActiveCutscenes[i], false);
				break;
			}
		}
	}
}


void cutscene_PlayerLeftMap(Entity *pEnt)
{
	if(pEnt && pEnt->pPlayer && pEnt->pPlayer->pCutscene){
		cutscene_PlayerSkipCutscene(pEnt, false);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		StructDestroySafe(parse_CutsceneDef, &pEnt->pPlayer->pCutscene);
	}
}


bool cutscene_CreateAction(Entity* pEnt, ActiveCutscene* pActiveCutscene, 
						   ActiveCutsceneCallback pCallback, void* pCallbackData,
						   F32 fActivateTime)
{
	if ( pEnt && pActiveCutscene )
	{
		S32 i, iSize = eaSize(&pActiveCutscene->eaActions);
		for ( i = 0; i < iSize; i++ )
		{
			if ( pActiveCutscene->eaActions[i]->pCallbackData == pCallbackData )
				break;
		}

		if ( i == iSize )
		{
			ActiveCutsceneAction* pAction = StructCreate( parse_ActiveCutsceneAction );
			pAction->pCallback = pCallback;
			pAction->pCallbackData = pCallbackData;
			pAction->fActivateTime = fActivateTime;
			pAction->erOwner = entGetRef(pEnt);
			eaPush(&pActiveCutscene->eaActions,pAction);
			return true;
		}
	}

	return false;
}

static void cutscene_EndAllCutscenesInPartition(int iPartitionIdx)
{
	int i;
	for ( i=0; i < eaSize(&g_ppActiveCutscenes); i++ ) {
		if(g_ppActiveCutscenes[i]->iPartitionIdx == iPartitionIdx)
			cutscene_EndOnServer(g_ppActiveCutscenes[i]);
	}
}

static void cutscene_EndAllCutscenes()
{
	int i;
	for ( i=0; i < eaSize(&g_ppActiveCutscenes); i++ ) {
		cutscene_EndOnServer(g_ppActiveCutscenes[i]);
	}
}

void cutscene_PartitionLoad(int iPartitionIdx)
{
	PERFINFO_AUTO_START_FUNC();

	cutscene_EndAllCutscenesInPartition(iPartitionIdx);

	PERFINFO_AUTO_STOP();
}

void cutscene_PartitionUnload(int iPartitionIdx)
{
	cutscene_EndAllCutscenesInPartition(iPartitionIdx);
}

void cutscene_MapLoad()
{
	cutscene_EndAllCutscenes();
}

void cutscene_MapUnload()
{
	cutscene_EndAllCutscenes();
}

F32 cutscene_GetActiveHighSendRange(void)
{
	if(zmapInfoGetMapType(NULL) != ZMTYPE_STATIC)
	{
		S32 i;
		F32 fLong = 0.0f;

		for(i = 0; i < eaSize(&g_ppActiveCutscenes); ++i)
		{
			if(g_ppActiveCutscenes[i] && g_ppActiveCutscenes[i]->pDef && g_ppActiveCutscenes[i]->pDef->fMinCutSceneSendRange > fLong)
			{
				fLong = g_ppActiveCutscenes[i]->pDef->fMinCutSceneSendRange;
			}
		}

		return fLong;
	}

	return 0.0f;
}

#include "AutoGen/cutscene_h_ast.c"
