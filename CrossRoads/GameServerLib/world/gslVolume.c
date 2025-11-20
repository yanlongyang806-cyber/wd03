/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "aiLib.h"
#include "aiMastermind.h"
#include "Beacon.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Character_combat.h"
#include "Entity.h"
#include "EntityInteraction.h"
#include "EntityIterator.h"
#include "Expression.h"
#include "gameaction_common.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventSend.h"
#include "gslGameAction.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "gslLandmark.h"
#include "gslMapTransfer.h"
#include "gslMapVariable.h"
#include "gslMapReveal.h"
#include "gslMechanics.h"
#include "gslNeighborhood.h"
#include "gslPartition.h"
#include "gslSavedPet.h"
#include "gslSpawnPoint.h"
#include "gslVolume.h"
#include "interaction_common.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "Player.h"
#include "Powers.h"
#include "rand.h"
#include "RoomConn.h"
#include "StringCache.h"
#include "timing.h"
#include "wlBeacon.h"
#include "wlEncounter.h"
#include "wlVolumes.h"
#include "WorldGrid.h"
#include "gslWorldVariable.h"
#include "ChoiceTable.h"
#include "ChoiceTable_common.h"

#include "AutoGen/gslInteractable_h_ast.h"


// ----------------------------------------------------------------------------------
// Static Data Initialization
// ----------------------------------------------------------------------------------

static ExprContext *s_pVolumeContext = NULL;

static GameNamedVolume **s_eaNamedVolumes = NULL;
static StashTable s_stNameToGameVolume = NULL;
static StashTable s_stEntryToGameVolume = NULL;

static WorldVolumeEntry **s_eaWorldVolumes = NULL;

const char *g_pchVolumeVarName = NULL;

static bool s_bVolumesLoaded = false;

static U32 s_ePetsDisabledVolumeType = 0;

static bool volume_NeedsEnterCB(WorldVolumeEntry *pEntry);
static bool volume_NeedsRemainCB(WorldVolumeEntry *pEntry);
static bool volume_NeedsExitCB(WorldVolumeEntry *pEntry);
static void volume_VolumeEnteredCB(WorldVolume *pVolume, WorldVolumeQueryCache *pQueryCache);
static void volume_VolumeRemainCB(WorldVolume *pVolume, WorldVolumeQueryCache *pQueryCache);
static void volume_VolumeExitedCB(WorldVolume *pVolume, WorldVolumeQueryCache *pQueryCache);

// ----------------------------------------------------------------------------------
// Entity Volume Logic
// ----------------------------------------------------------------------------------


// ----------------------------------------------------------------------------------
// Sole interfaces to searching s_eaNamedVolume.  No other function
// than volume_GetByName and volume_GetByEntry should be searching
// s_eaNamedVolume.
// ----------------------------------------------------------------------------------
GameNamedVolume *volume_GetByName(const char *pcVolumeName, const WorldScope* pScope)
{
	if (pScope && gUseScopedExpr) {
		WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcVolumeName);

		if (pObject && (pObject->type == WL_ENC_NAMED_VOLUME)) {
			WorldNamedVolume *pNamedVolume = (WorldNamedVolume*)pObject;
			GameNamedVolume *pGameVolume = volume_GetByEntry(pNamedVolume->entry);
			if (pGameVolume) {
				return pGameVolume;
			}
		}
	} else {
		GameNamedVolume* ret = NULL;
		stashFindPointer(s_stNameToGameVolume, allocAddString(pcVolumeName), &ret);
		return ret;
	}

	return NULL;
}


GameNamedVolume *volume_GetByEntry(WorldVolumeEntry *pVolume)
{
	GameNamedVolume* ret = NULL;
	stashFindPointer(s_stEntryToGameVolume, pVolume, &ret);
	return ret;
}

#define FOR_EACH_VOLUME(it) { int i##it##Index; for(i##it##Index=eaSize(&s_eaNamedVolumes)-1; i##it##Index>=0; --i##it##Index) { GameNamedVolume *it = s_eaNamedVolumes[i##it##Index];
#define FOR_EACH_VOLUME2(outerIt, it) { int i##it##Index; for(i##it##Index=i##outerIt##Index-1; i##it##Index>=0; --i##it##Index) { GameNamedVolume *it = s_eaNamedVolumes[i##it##Index];
#define FOR_EACH_WORLD_VOLUME(it) { int i##it##Index; for(i##it##Index=eaSize(&s_eaWorldVolumes)-1; i##it##Index>=0; --i##it##Index) { WorldVolumeEntry *it = s_eaWorldVolumes[i##it##Index];
// ----------------------------------------------------------------------------------
// End of sole interfaces to searching s_eaNamedVolumes.
// ----------------------------------------------------------------------------------

void volume_ForEachEntry(const WorldScope* pScope, VolumeForEachEntryCallback cb, void *pUserData)
{
	FOR_EACH_VOLUME(pVolume)
	{
		cb(pVolume->pNamedVolume->entry, pUserData);
	}
	FOR_EACH_END
}

bool volume_IsEntityInVolume(const Entity *pEnt, GameNamedVolume *pGameVolume)
{
	if (pEnt) {
		if (pEnt->pPlayer) {
			// If it's a player, do an optimized search
			return eaFind(&pEnt->pPlayer->InteractStatus.eaInVolumes, pGameVolume->pcName) >= 0;
		} else if (pGameVolume && pEnt->volumeCache) {
			const WorldVolume **eaVolumes;
			int i;

			// Match it to the world volumes for the entity
			eaVolumes = wlVolumeCacheGetCachedVolumes(pEnt->volumeCache);
			for(i=eaSize(&eaVolumes)-1; i>=0; --i) {
				const WorldVolume *pVolume = eaVolumes[i];
				WorldVolumeEntry *pEntry = wlVolumeGetVolumeData(pVolume);
				if (pEntry == pGameVolume->pNamedVolume->entry) {
					return true;
				}
			}
		}
	}
	return false;
}


bool volume_IsEntityInVolumeByName(const Entity *pEnt, const char *pcVolumeName, const WorldScope *pScope)
{
	if (pEnt && pcVolumeName) {
		GameNamedVolume *pGameVolume;

		// Find the named volume
		pGameVolume = volume_GetByName(pcVolumeName, pScope);
		if (!pGameVolume) {
			//Errorf("Unable to find volume named '%s' for IsEntityInVolume.", pcVolumeName);
			return false;
		}

		return volume_IsEntityInVolume(pEnt, pGameVolume);
	}
	return false;
}


bool volume_IsEntityInAnyVolumeByName(const Entity *pEnt, const char **eaVolumeNames, const WorldScope *pScope)
{
	int it;
	for( it = 0; it != eaSize( &eaVolumeNames ); ++it ) {
		if( volume_IsEntityInVolumeByName( pEnt, eaVolumeNames[ it ], pScope )) {
			return true;
		}
	}

	return false;
}


void volume_GetEntitiesInVolume(int iPartitionIdx, const char *pcVolumeName, const WorldScope *pScope, Entity ***peaEntsOut, bool bFilterResults)
{
	WorldVolume *pVolume = NULL;
	WorldVolumeQueryCache **eaQueries;
	GameNamedVolume *pGameVolume;
	int i;

	// Initialize the entity query type, in case it hasn't yet been initialized
	devassert(s_EntityVolumeQueryType);

	// Find the named volume
	pGameVolume = volume_GetByName(pcVolumeName, pScope);
	if (!pGameVolume) {
		//Errorf("Unable to find volume named '%s' for GetEntitiesInVolume", pcVolumeName);
		return;
	}

	// Validation test to help locate corruption in volume entries
	if (eaFind(&s_eaWorldVolumes, pGameVolume->pNamedVolume->entry) == -1) {
		// This error was taken out, as it can happen if a volume is currently in a hidden node
		//printf("Volume '%s' refers to an invalid cell entry during volume_GetEntitiesInVolume.  (This is a software and not a data problem.  This can occur normally during a map edit change but should not occur at other times.)\n", pGameVolume->pcName);
		return;
	}

	// Find the world volume
	if (!pGameVolume->pNamedVolume->entry) {
		//Errorf("Unable to find world volume named '%s' for GetEntitiesInVolume", pcVolumeName);
		return;
	}
	pVolume = eaGet(&pGameVolume->pNamedVolume->entry->eaVolumes, iPartitionIdx);
	if (!pVolume) {
		return;
	}

	// Query for entities in the volume
	eaQueries = wlVolumeGetCachedQueries(pVolume);

	for(i=eaSize(&eaQueries)-1; i>=0; --i) {
		WorldVolumeQueryCache *pQuery = eaQueries[i];

		// Each "query cache" corresponds to a thing in the volume.
		if (pQuery && wlVolumeQueryCacheIsType(pQuery, s_EntityVolumeQueryType)) {
			Entity *pEnt = wlVolumeQueryCacheGetData(pQuery);
			// Add the character to the list if it wasn't there before
			if((entGetPartitionIdx(pEnt) == iPartitionIdx) && entIsAlive(pEnt) && eaFind(peaEntsOut, pEnt) == -1) {
				if(!(bFilterResults && exprFuncHelperShouldExcludeFromEntArray(pEnt))) {
					eaPush(peaEntsOut, pEnt);
				}
			}
		}
	}
}


int volume_EvaluateExpr(int iPartitionIdx, GameNamedVolume *pVolume, Entity *pEnt, Expression *pExpr)
{
	WorldScope *pScope;
	MultiVal mvResultVal;
	int iResult;

	PERFINFO_AUTO_START_FUNC();

	pScope = SAFE_MEMBER2(pVolume, pNamedVolume, common_data.closest_scope);

	exprContextSetSelfPtr(s_pVolumeContext, pEnt);
	exprContextSetPartition(s_pVolumeContext, iPartitionIdx);
	exprContextSetScope(s_pVolumeContext, pScope);
	exprContextSetPointerVarPooled(s_pVolumeContext, g_pchVolumeVarName, pVolume, NULL, false, true);

	// If the entity is a player, add it to the context as "Player"
	if (entGetPlayer(pEnt)) {
		exprContextSetPointerVarPooled(s_pVolumeContext, g_PlayerVarName, pEnt, NULL, false, true);
	} else {
		exprContextRemoveVarPooled(s_pVolumeContext, g_PlayerVarName);
	}

	exprEvaluate(pExpr, s_pVolumeContext, &mvResultVal);
	iResult = MultiValGetInt(&mvResultVal, NULL);

	PERFINFO_AUTO_STOP();
	return iResult;
}


static VolumePartitionState *volume_GetOrCreatePartitionState(GameNamedVolume *pVolume, int iPartitionIdx)
{
	VolumePartitionState *pState = eaGet(&pVolume->eaPartitionStates, iPartitionIdx);
	if (!pState) {
		assert(partition_ExistsByIdx(iPartitionIdx));
		pState = calloc(1,sizeof(VolumePartitionState));
		pState->iPartitionIdx = iPartitionIdx;
		eaSet(&pVolume->eaPartitionStates, pState, iPartitionIdx);
	}
	return pState;
}


// ----------------------------------------------------------------------------------
// Action Volumes
// ----------------------------------------------------------------------------------


static void volume_ActionVolumeEnteredCB(WorldVolumeEntry *pEntry, Entity *pEnt, GameNamedVolume *pGameVolume)
{
	WorldActionVolumeProperties *pActionData;
	bool bDoAction = true;

	if (!g_EncounterProcessing || !pGameVolume) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	pActionData = pEntry->server_volume.action_volume_properties;
	if (pActionData->entered_action_cond) {
		bDoAction = volume_EvaluateExpr(entGetPartitionIdx(pEnt), pGameVolume, pEnt, pActionData->entered_action_cond);
	}
	if (bDoAction && pActionData->entered_action) {
		volume_EvaluateExpr(entGetPartitionIdx(pEnt), pGameVolume, pEnt, pActionData->entered_action);
	}

	PERFINFO_AUTO_STOP();
}


static void volume_ActionVolumeExitedCB(WorldVolumeEntry *pEntry, Entity *pEnt, GameNamedVolume *pGameVolume)
{
	WorldActionVolumeProperties *pActionData;
	bool bDoAction = true;

	if (!g_EncounterProcessing || !pGameVolume) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	pActionData = pEntry->server_volume.action_volume_properties;
	if (pActionData->exited_action_cond) {
		bDoAction = volume_EvaluateExpr(entGetPartitionIdx(pEnt), pGameVolume, pEnt, pActionData->exited_action_cond);
	}
	if (bDoAction && pActionData->exited_action) {
		volume_EvaluateExpr(entGetPartitionIdx(pEnt), pGameVolume, pEnt, pActionData->exited_action);
	}

	PERFINFO_AUTO_STOP();
}


static void volume_ValidateActionVolume(GameNamedVolume *pGameVolume)
{
	WorldActionVolumeProperties *pActionData = pGameVolume->pNamedVolume->entry->server_volume.action_volume_properties;

	// Check that if a condition is provided that there is an action
	if (pActionData->entered_action_cond && !pActionData->entered_action) {
		ErrorFilenamef(layerGetFilename(pGameVolume->pNamedVolume->common_data.layer), 
					   "Action volume '%s' has an entered condition but no entered action.  You should have an action if you have a condition.", pGameVolume->pcName);
	}
	if (pActionData->exited_action_cond && !pActionData->exited_action) {
		ErrorFilenamef(layerGetFilename(pGameVolume->pNamedVolume->common_data.layer), 
					   "Action volume '%s' has an exited condition but no exited action.  You should have an action if you have a condition.", pGameVolume->pcName);
	}
}


// ----------------------------------------------------------------------------------
// Tracking Volumes
// ----------------------------------------------------------------------------------


static void volume_TrackVolumeEnteredCB(WorldVolumeEntry *pEntry, Entity *pEnt, GameNamedVolume *pGameVolume)
{
	if ( !pEnt->pPlayer || !pGameVolume) {
		return;
	}

	// Track the volume on the player
	eaPushUnique(&pEnt->pPlayer->InteractStatus.eaInVolumes, pGameVolume->pcName); // Name is pooled
}


static void volume_TrackVolumeExitedCB(WorldVolumeEntry *pEntry, Entity *pEnt, GameNamedVolume *pGameVolume)
{
	if (!pEnt->pPlayer || !pGameVolume) {
		return;
	}

	eaFindAndRemove(&pEnt->pPlayer->InteractStatus.eaInVolumes, pGameVolume->pcName); // Name is pooled
}


void volume_UpdateVolumeTracking(int iPartitionIdx, GameNamedVolume *pGameVolume)
{
	WorldVolume *pVolume = NULL;
	WorldVolumeQueryCache **eaQueries;
	int i;

	// Initialize the entity query type, in case it hasn't yet been initialized
	devassert(s_EntityVolumeQueryType);

	// Find the world volume
	if (!pGameVolume->pNamedVolume->entry) {
		//Errorf("Unable to find world volume named '%s' for GetEntitiesInVolume", pcVolumeName);
		return;
	}
	pVolume = eaGet(&pGameVolume->pNamedVolume->entry->eaVolumes, iPartitionIdx);
	if (!pVolume) {
		return;
	}

	// Query for entities in the volume
	eaQueries = wlVolumeGetCachedQueries(pVolume);

	for(i=eaSize(&eaQueries)-1; i>=0; --i) {
		WorldVolumeQueryCache *pQuery = eaQueries[i];

		// Each "query cache" corresponds to a thing in the volume.
		if (pQuery && wlVolumeQueryCacheIsType(pQuery, s_EntityVolumeQueryType)) {
			Entity *pEnt = wlVolumeQueryCacheGetData(pQuery);
			// Add the character to the list if it wasn't there before
			if(entIsAlive(pEnt)) {
				volume_TrackVolumeEnteredCB(pGameVolume->pNamedVolume->entry, pEnt, pGameVolume);
			}
		}
	}
}


// ----------------------------------------------------------------------------------
// Interaction Volumes
// ----------------------------------------------------------------------------------

static void volume_ValidateInteractionVolume(GameNamedVolume *pGameVolume, WorldScope *pScope)
{
	WorldInteractionProperties *pVolumeInteractionProps = pGameVolume->pNamedVolume->entry->server_volume.interaction_volume_properties;
	const char *pcFilename = layerGetFilename(pGameVolume->pNamedVolume->common_data.layer);
	int i;

	for (i = eaSize(&pVolumeInteractionProps->eaEntries) - 1; i >= 0; i--)
		interaction_ValidatePropertyEntry(pVolumeInteractionProps->eaEntries[i], pScope, pcFilename, "Volume", pGameVolume->pcName);

	for (i = eaSize(&pGameVolume->eaOverrides) - 1; i >= 0; i--)
		interaction_ValidatePropertyEntry(pGameVolume->eaOverrides[i]->pEntry, pScope, pGameVolume->eaOverrides[i]->pcFilename, "Volume", pGameVolume->pcName);
}

WorldInteractionPropertyEntry *volume_GetInteractionPropEntry(GameNamedVolume *pGameVolume, int iIndex)
{
	if (pGameVolume && pGameVolume->pNamedVolume && pGameVolume->pNamedVolume->entry && pGameVolume->pNamedVolume->entry->server_volume.interaction_volume_properties)
	{
		if (eaSize(&pGameVolume->pNamedVolume->entry->server_volume.interaction_volume_properties->eaEntries) > iIndex)
			return pGameVolume->pNamedVolume->entry->server_volume.interaction_volume_properties->eaEntries[iIndex];
		iIndex -= eaSize(&pGameVolume->pNamedVolume->entry->server_volume.interaction_volume_properties->eaEntries);
	}
	if (pGameVolume && pGameVolume->eaOverrides && eaSize(&pGameVolume->eaOverrides) > iIndex)
		return pGameVolume->eaOverrides[iIndex]->pEntry;
	return NULL;
}

WorldInteractionPropertyEntry *volume_GetInteractionPropEntryByName(const char *pcVolumeName, const WorldScope* pScope, int iIndex)
{
	GameNamedVolume *pGameVolume = volume_GetByName(pcVolumeName, pScope);

	if(!pGameVolume)
		return NULL;

	// Return properties if in main list
	if (pGameVolume->pNamedVolume && pGameVolume->pNamedVolume->entry && pGameVolume->pNamedVolume->entry->server_volume.interaction_volume_properties)
	{
		if(eaSize(&pGameVolume->pNamedVolume->entry->server_volume.interaction_volume_properties->eaEntries) > iIndex) 
		{
			return pGameVolume->pNamedVolume->entry->server_volume.interaction_volume_properties->eaEntries[iIndex];
		}
		else
		{
			iIndex -= eaSize(&pGameVolume->pNamedVolume->entry->server_volume.interaction_volume_properties->eaEntries);
		}
	} 
	
	// Return properties if in override list
	if (pGameVolume->eaOverrides && iIndex >= 0 && iIndex < eaSize(&pGameVolume->eaOverrides)) {
		return pGameVolume->eaOverrides[iIndex]->pEntry;
	}

	return NULL;
}


// Checks if the player can interact with the given volume
bool volume_AddInteractOptions(int iPartitionIdx, const char *pcVolumeName, const WorldScope* pScope, Entity *pEnt, InteractOption ***peaOptionList)
{
	GameNamedVolume *pGameVolume;
	WorldInteractionProperties *pProps;
	int i;
	bool bResult = false;
	WorldInteractionPropertyEntry** eaEntries = NULL;

	PERFINFO_AUTO_START_FUNC();
	
	pGameVolume = volume_GetByName(pcVolumeName, pScope);
	if (!pGameVolume || !pGameVolume->pNamedVolume || !pGameVolume->pNamedVolume->entry) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	pProps = pGameVolume->pNamedVolume->entry->server_volume.interaction_volume_properties;
	if (pProps)
		eaPushEArray(&eaEntries, &pProps->eaEntries);

	for (i = 0; i < eaSize(&pGameVolume->eaOverrides); i++)
	{
		eaPush(&eaEntries, pGameVolume->eaOverrides[i]->pEntry);
	}

	for (i = 0; i < eaSize(&eaEntries); ++i) {
		WorldInteractionPropertyEntry* pEntry = eaEntries[i];
		bool bVisible = !interaction_IsInteractTargetBusy2(entGetPartitionIdx(pEnt), pEnt, 0, NULL, pGameVolume->pcName, i);

		if (pEntry->pVisibleExpr) {
			PERFINFO_AUTO_START("CheckVisibleExpression",1);
			bVisible = bVisible && volume_EvaluateExpr(iPartitionIdx, pGameVolume, pEnt, pEntry->pVisibleExpr);
			PERFINFO_AUTO_STOP();
		}

		if (pEntry->pInteractCond) {
			PERFINFO_AUTO_START("CheckInteractExpression",1);
			bVisible = bVisible && volume_EvaluateExpr(iPartitionIdx, pGameVolume, pEnt, pEntry->pInteractCond);
			PERFINFO_AUTO_STOP();
		}

		if (pEntry->pActionProperties) {
			PERFINFO_AUTO_START("CheckGameActions",1);
			bVisible = bVisible && gameaction_PlayerIsEligible(pEnt, &pEntry->pActionProperties->successActions);
			PERFINFO_AUTO_STOP();
		}

		if (bVisible) {
			interaction_AddOptionToInteract(entGetPartitionIdx(pEnt), peaOptionList, pEntry, NULL, NULL, NULL, pGameVolume, i, pEnt, false);
		}
	}

	if(eaEntries)
		eaDestroy(&eaEntries);

	PERFINFO_AUTO_STOP();
	return bResult;
}

void volume_RemoveInteractableOverridesFromMission(const char *pcMissionName)
{
	if(pcMissionName && pcMissionName[0])
	{
		FOR_EACH_VOLUME(pVolume)
		{
			int j;
			for (j=eaSize(&pVolume->eaOverrides)-1; j >= 0 ; j--) 
			{
				if(stricmp(pVolume->eaOverrides[j]->pcName, pcMissionName) == 0)
				{
					free(pVolume->eaOverrides[j]);
					eaRemove(&pVolume->eaOverrides, j);
				}
			}
		} FOR_EACH_END;
	}
}

bool volume_ApplyInteractableOverride(const char *pcMissionName, const char *pcFilename, const char *pcVolumeName, WorldInteractionPropertyEntry *pEntry)
{
	GameInteractableOverride *pOverride;

	if (pcVolumeName && pcVolumeName[0])
	{
		GameNamedVolume *pGameVolume;

		// Check that the volume exists
		pGameVolume = volume_GetByName(pcVolumeName, NULL);
		if (!pGameVolume) {
			ErrorFilenamef(pcFilename, "Volume/Interactable interaction override for mission '%s' attempting to override non-existent volume/interactable '%s'", pcMissionName, pcVolumeName);
			return false;
		}

		// Add the properties
		pOverride = StructCreate(parse_GameInteractableOverride);
		pOverride->pcFilename = allocAddFilename(pcFilename);
		pOverride->pcName = allocAddString(pcMissionName);
		pOverride->pEntry = StructClone(parse_WorldInteractionPropertyEntry, pEntry);
		eaPush(&pGameVolume->eaOverrides, pOverride);

		return true;
	}

	return false;
}

static InteractOption *volume_FindInteractOption(SA_PARAM_NN_VALID Entity *pEnt, const char *pcVolumeName, int iIndex)
{
	int i;

	for(i=eaSize(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions)-1; i>=0; --i) {
		InteractOption *pOption = pEnt->pPlayer->InteractStatus.interactOptions.eaOptions[i];

		if ((pcVolumeName == pOption->pcVolumeName) && // Can do this since both are pooled strings
			(iIndex == pOption->iIndex)) {
				return pOption;
		}
	}

	return NULL;
}


bool volume_VerifyInteract(SA_PARAM_NN_VALID Entity *pEnt, const char *pcVolumeName, int iIndex)
{
	InteractOption *pOption = volume_FindInteractOption(pEnt, allocAddString(pcVolumeName), iIndex);

	if (pOption && !pOption->bDisabled) {
		return true;
	}
	return false;
}


// ----------------------------------------------------------------------------------
// Power Volumes
// ----------------------------------------------------------------------------------


static CharacterClass *volume_FindCharacterClassByPowerStrength( WorldPowerVolumeStrength eStrength )
{
	switch (eStrength) {
		case WorldPowerVolumeStrength_Harmless:
			return characterclasses_FindByName("Object_Harmless");
		case WorldPowerVolumeStrength_Default:
			return characterclasses_FindByName("Object_Default");
		case WorldPowerVolumeStrength_Deadly:
			return characterclasses_FindByName("Object_Deadly");
	}
	return NULL;
}


static bool volume_EvaluatePowerCondition(Entity* pEnt, WorldPowerVolumeProperties *pPowerData)
{
	// If this power has a condition, check that it's valid
	if (pPowerData->trigger_cond) {
		MultiVal mvResult;

		bool bReturn;

		PERFINFO_AUTO_START_FUNC();

		// Do we need this?
		//exprContextSetPointerVar(s_pVolumeContext, "PowerVolumeData", volData, parse_PowerVolumeData, false, true);

		// RvP decided to not change this to call volume_EvaluateExpr() in case setting the
		// scope to NULL is important
		exprContextSetSelfPtr(s_pVolumeContext, pEnt);
		exprContextSetPartition(s_pVolumeContext, entGetPartitionIdx(pEnt));
		exprContextSetScope(s_pVolumeContext, NULL);

		if (entGetPlayer(pEnt)) {
			exprContextSetPointerVarPooled(s_pVolumeContext, g_PlayerVarName, pEnt, NULL, false, true);
		} else {
			exprContextRemoveVarPooled(s_pVolumeContext, g_PlayerVarName);
		}
		exprEvaluate(pPowerData->trigger_cond, s_pVolumeContext, &mvResult);
		bReturn = MultiValGetInt(&mvResult, false);

		PERFINFO_AUTO_STOP();

		return bReturn;
	} else {
		return true;
	}
}


static void volume_PowerVolumeEntityUpdate(Entity *pEnt, WorldPowerVolumeProperties *pPowerData, WorldVolumeEntry *pEntry, GameNamedVolume *pGameVolume)
{
	PowerDef *pPower;
	S64 currentTime;
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	VolumePartitionState *pPartitionState = NULL;

	PERFINFO_AUTO_START_FUNC();

	pPower = GET_REF(pPowerData->power);

	// Power does not execute if no power OR condition fails OR partition paused
	if (!pPower
		|| !pGameVolume
		|| mapState_IsMapPausedForPartition(iPartitionIdx)
		|| !volume_EvaluatePowerCondition(pEnt, pPowerData)) {
		PERFINFO_AUTO_STOP();
		return;
	}

	pPartitionState = volume_GetOrCreatePartitionState(pGameVolume, iPartitionIdx);

	// Power does not execute if it has a repeat time and it hasn't elapsed
	currentTime = timeMsecsSince2000();
	if ((pPowerData->repeat_time > 0) && (pPartitionState->iNextPowerTime > currentTime)) {
		PERFINFO_AUTO_STOP();
		return;
	}

	// Set the repeat timer, if there is one
	if (pPowerData->repeat_time > 0) {
		S64 interval = (S64)(pPowerData->repeat_time * 1000.0);
		// Increment by one repeat time, but if this is still less than the current time
		// Then we missed more than one power interval, so start over with the current
		// time being the new start time.
		pPartitionState->iNextPowerTime += interval;
		if (pPartitionState->iNextPowerTime < currentTime) {
			pPartitionState->iNextPowerTime = currentTime + interval;
		}
	}

	// Mark the partition for power processing this frame
	pPartitionState->bExecutePower = true;

	PERFINFO_AUTO_STOP();
}

// Executes a power from the volume center for the given partition
// TODO: This should probably eventually execute a power on a specific entity. To avoid possible breakages
//  with existing powers volumes, I'm keeping the original implementation in tact for now.
static void volume_PowerExecute(VolumePartitionState *pPartitionState, 
								WorldPowerVolumeProperties *pPowerData, 
								WorldVolume *pVolume)
{
	PowerDef *pPower = GET_REF(pPowerData->power);
	if (pPower)
	{
		U32 iLevel = pPowerData->level;
		int iPartitionIdx = pPartitionState->iPartitionIdx;
		WorldVolume **eaVolumes = NULL;
		Vec3 vSourcePos;

		ANALYSIS_ASSUME(pPower != NULL);
		PERFINFO_AUTO_START_FUNC();

		// Power level based on data or calculation if zero
		if (iLevel <= 0) {
			iLevel = mechanics_GetMapLevel(iPartitionIdx);
		}

		// Put volume on list to process
		eaPush(&eaVolumes, pVolume);

		// Power comes from center of volume
		wlVolumeGetVolumeWorldMid(pVolume, vSourcePos);

		// Execute the power in the volume
		location_ApplyPowerDef(vSourcePos, iPartitionIdx, pPower, 0, NULL, &eaVolumes, NULL, 
			volume_FindCharacterClassByPowerStrength(pPowerData->strength), iLevel, 0);

		// Clear the 'bExecutePower' flag
		pPartitionState->bExecutePower = false;

		eaDestroy(&eaVolumes);

		PERFINFO_AUTO_STOP();
	}
}


static void volume_PowerVolumeEnteredCB(WorldVolumeEntry *pEntry, Entity *pEnt, GameNamedVolume *pGameVolume)
{
	PowerDef *pPower = GET_REF(pEntry->server_volume.power_volume_properties->power);
	
	PERFINFO_AUTO_START_FUNC();

	if (pPower && pPower->eType==kPowerType_Innate) {
		entity_ExternalInnateUpdateVolumeCount(pEnt,true);
	} else {
		volume_PowerVolumeEntityUpdate(pEnt, pEntry->server_volume.power_volume_properties, pEntry, pGameVolume);
	}

	PERFINFO_AUTO_STOP();
}


static void volume_PowerVolumeRemainCB(WorldVolumeEntry *pEntry, Entity *pEnt)
{
	WorldPowerVolumeProperties* pProps = pEntry->server_volume.power_volume_properties;
	GameNamedVolume *pGameVolume;
	int i;

	PERFINFO_AUTO_START_FUNC();

	pGameVolume = volume_GetByEntry(pEntry);
	if (pGameVolume) {
		// Only repeat a power in a volume if it's a repeating power
		if (pProps->repeat_time > 0) {
			volume_PowerVolumeEntityUpdate(pEnt, pProps, pEntry, pGameVolume);
		}

		// Execute powers for each partition that requires an update
		for (i = eaSize(&pGameVolume->eaPartitionStates)-1; i >= 0; i--) {
			VolumePartitionState *pState = pGameVolume->eaPartitionStates[i];
			if (pState && pState->bExecutePower && !mapState_IsMapPausedForPartition(i)) {
				WorldVolume *pWorldVolume = eaGet(&pEntry->eaVolumes, i);
				if (pWorldVolume) {
					volume_PowerExecute(pState, pProps, pWorldVolume);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


static void volume_PowerVolumeExitedCB(WorldVolumeEntry *pEntry, Entity *pEnt)
{
	PowerDef *pPower = GET_REF(pEntry->server_volume.power_volume_properties->power);

	PERFINFO_AUTO_START_FUNC();

	if (pPower && pPower->eType==kPowerType_Innate) {
		entity_ExternalInnateUpdateVolumeCount(pEnt,false);
	}

	PERFINFO_AUTO_STOP();
}


static void volume_ValidatePowerVolume(GameNamedVolume *pGameVolume)
{
	WorldPowerVolumeProperties *pPowerData = pGameVolume->pNamedVolume->entry->server_volume.power_volume_properties;

	// Check that power exists
	PowerDef *pPower = GET_REF(pPowerData->power);
	if (!pPower) {
		if (REF_STRING_FROM_HANDLE(pPowerData->power)) {
			ErrorFilenamef(layerGetFilename(pGameVolume->pNamedVolume->common_data.layer), 
						   "Power volume '%s' uses non-existent power '%s'", pGameVolume->pcName, REF_STRING_FROM_HANDLE(pPowerData->power));
		} else {
			ErrorFilenamef(layerGetFilename(pGameVolume->pNamedVolume->common_data.layer), 
						   "Power volume '%s' has no power defined and it must have a power.", pGameVolume->pcName);
		}
	}
	else if(pPower->eType==kPowerType_Innate && pPowerData->repeat_time > 0)
	{
		ErrorFilenameGroupRetroactivef(layerGetFilename(pGameVolume->pNamedVolume->common_data.layer),
			"Design",7,3,10,2009,
			"Power volume '%s' is using Innate power '%s' but has a repeat timer greater than zero.", pGameVolume->pcName, pPower->pchName);
	}

	// Check for out-of-range values
	if (pPowerData->level < 0) {
		ErrorFilenamef(layerGetFilename(pGameVolume->pNamedVolume->common_data.layer), 
					   "Power volume '%s' has a level less than zero.", pGameVolume->pcName);
	}
	if (pPowerData->repeat_time < 0) {
		ErrorFilenamef(layerGetFilename(pGameVolume->pNamedVolume->common_data.layer), 
					   "Power volume '%s' has a repeat timer less than zero.", pGameVolume->pcName);
	}
}


// ----------------------------------------------------------------------------------
// Warp Volumes
// ----------------------------------------------------------------------------------

static int volume_EvaluateWarpCondition(WorldWarpVolumeProperties *pWarpData, GameNamedVolume *pGameVolume, Entity *pEnt)
{
	// If this warp volume has a condition, check that it's valid
	if (pWarpData->warp_cond) {
		return volume_EvaluateExpr(entGetPartitionIdx(pEnt), pGameVolume, pEnt, pWarpData->warp_cond);
	} else {
		return 1;
	}
}

static void volume_WarpVolume_DoWarp(WorldWarpVolumeProperties *pWarpData, GameNamedVolume *pGameVolume, Entity* pEnt)
{
	WorldVariable* dest = NULL;
	WorldVariable** variables = NULL;
	int seed = randomInt();
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	
	PERFINFO_AUTO_START_FUNC();

	dest = worldVariableCalcVariableAndAlloc(iPartitionIdx, &pWarpData->warpDest, pEnt, seed, 0);
	variables = worldVariableCalcVariablesAndAlloc(iPartitionIdx, pWarpData->variableDefs, pEnt, seed, 0);
	
	if (dest && dest->eType == WVAR_MAP_POINT) {
		MapVariable *pVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, FORCEMISSIONRETURN_MAPVAR);

		// If the "ForceMissionReturn" mapvar is set, anything that changes maps should instead just leave the current map
		if (dest->pcZoneMap && pVar && pVar->pVariable && pVar->pVariable->iIntVal != 0){
			LeaveMapEx(pEnt, GET_REF(pWarpData->hTransSequence));
		} else {
			spawnpoint_MovePlayerToMapAndSpawn(pEnt, dest->pcZoneMap, dest->pcStringVal, NULL, 0, 0, 0, 0, variables, SAFE_MEMBER2(pGameVolume, pNamedVolume, common_data.closest_scope), GET_REF(pWarpData->hTransSequence),0, 0);
		}
	}
	
	StructDestroy(parse_WorldVariable, dest);
	eaDestroyStruct(&variables, parse_WorldVariable);

	PERFINFO_AUTO_STOP();
}

static void volume_WarpVolumeEnteredCB(WorldVolumeEntry *pEntry, Entity *pEnt, GameNamedVolume *pGameVolume)
{
	WorldWarpVolumeProperties *pWarpData = pEntry->server_volume.warp_volume_properties;

	PERFINFO_AUTO_START_FUNC();

	// If it's a player, warp it
	if (pEnt && pEnt->pPlayer && volume_EvaluateWarpCondition(pWarpData, pGameVolume, pEnt)) {
		volume_WarpVolume_DoWarp(pWarpData, pGameVolume, pEnt);
	}

	PERFINFO_AUTO_STOP();
}

static void volume_WarpVolumeRemainCB(WorldVolumeEntry *pEntry, Entity *pEnt)
{
	WorldWarpVolumeProperties *pWarpData = pEntry->server_volume.warp_volume_properties;

	PERFINFO_AUTO_START_FUNC();

	// Re-evaluate the warp condition
	if (pEnt && pEnt->pPlayer && pWarpData && pWarpData->warp_cond) {
		GameNamedVolume *pGameVolume = volume_GetByEntry(pEntry);
		if (pGameVolume && volume_EvaluateWarpCondition(pWarpData, pGameVolume, pEnt)) {
			volume_WarpVolume_DoWarp(pWarpData, pGameVolume, pEnt);
		}
	}

	PERFINFO_AUTO_STOP();
}

static void volume_ValidateWarpVolume(GameNamedVolume *pGameVolume)
{
	WorldWarpVolumeProperties *pWarpData = pGameVolume->pNamedVolume->entry->server_volume.warp_volume_properties;
	ZoneMapInfo *pZoneMap = NULL;
	const char* pcFilename = layerGetFilename(pGameVolume->pNamedVolume->common_data.layer);

	// A warp volume must have a warp target
	if (pWarpData->warpDest.eType != WVAR_MAP_POINT) {
		ErrorFilenamef(pcFilename, "Volume: %s -- Warp destination is not a MAP_POINT.  This is an internal editor error.", pGameVolume->pcName);
	} else {
		char buffer[ 256 ];
		sprintf(buffer, "Volume: %s", pGameVolume->pcName);
		worldVariableValidateDef(&pWarpData->warpDest,&pWarpData->warpDest, pGameVolume->pcName, pcFilename);
	}
}


// ----------------------------------------------------------------------------------
// AI Volumes
// ----------------------------------------------------------------------------------

// Adding is accomplished by a scan during partition load
static void volume_removeAIAvoid(WorldVolumeEntry *pEntry)
{
	FOR_EACH_IN_EARRAY(pEntry->elements, WorldVolumeElement, pElement)
		aiAvoidVolumeRemove(0, pElement);
	FOR_EACH_END
	
}

// ----------------------------------------------------------------------------------
// Beacon Volumes
// ----------------------------------------------------------------------------------


static void volume_addBeaconNoDynConn(WorldVolumeEntry *pEntry)
{
	FOR_EACH_IN_EARRAY(pEntry->elements, WorldVolumeElement, pElement)
	{
		if(pElement->volume_shape == WL_VOLUME_BOX) {
			beaconAddNoDynConnBox(pElement->world_mat, pElement->local_min, pElement->local_max, pElement);
		} else if(pElement->volume_shape == WL_VOLUME_SPHERE) {
			beaconAddNoDynConnSphere(pElement->world_mat[3], pElement->radius, pElement);
		}
	}
	FOR_EACH_END
}

static void volume_removeNoDynConn(WorldVolumeEntry *pEntry)
{
	FOR_EACH_IN_EARRAY(pEntry->elements, WorldVolumeElement, pElement)
	{
		beaconRemoveNoDynConnVol(pElement);
	}
	FOR_EACH_END
}

// ----------------------------------------------------------------------------------
// Map Level Override Volumes
// ----------------------------------------------------------------------------------

static WorldVolumeEntry **s_eaMapLevelOverrideVolumes = NULL;

static void volume_AddMapLevelOverrideVolume(WorldVolumeEntry *pEntry)
{
	if (!s_eaMapLevelOverrideVolumes)
	{
		eaCreate(&s_eaMapLevelOverrideVolumes);
	}

	eaPush(&s_eaMapLevelOverrideVolumes, pEntry);
}

static void volume_RemoveMapLevelOverrideVolume(WorldVolumeEntry *pEntry)
{
	eaFindAndRemove(&s_eaMapLevelOverrideVolumes, pEntry);
}

static void volume_ValidateMapLevelOverrideVolume(GameNamedVolume *pGameVolume)
{
	WorldVolumeEntry *pEntry = pGameVolume->pNamedVolume->entry;

	if (pEntry->elements)
	{
		S32 i;

		for (i = 0; i < eaSize(&pEntry->elements); ++i)
		{
			if (pEntry->elements[i]->volume_shape == WL_VOLUME_HULL)
			{
				ErrorFilenamef(layerGetFilename(pGameVolume->pNamedVolume->common_data.layer), 
					"Map level override volume %s uses hull shape. Only spheres and boxes are allowed.", pGameVolume->pcName);
			}
		}
	}
}

S32 volume_GetLevelOverrideForPosition(Vec3 v3WorldPos)
{
	S32 i;

	for (i = 0; i < eaSize(&s_eaMapLevelOverrideVolumes); ++i)
	{
		WorldVolumeEntry *pEntry = s_eaMapLevelOverrideVolumes[i];

		if (pEntry && pEntry->server_volume.map_level_volume_properties)
		{
			if (volume_IsPointInVolume(v3WorldPos, pEntry))
			{
				return pEntry->server_volume.map_level_volume_properties->iLevel;
			}
		}
	}

	return -1;
}

// ----------------------------------------------------------------------------------
// Events for Volume Entry/Exit
// ----------------------------------------------------------------------------------


static void volume_EventVolumeEnteredCB(WorldVolumeEntry *pEntry, SA_PARAM_NN_VALID Entity *pEnt, GameNamedVolume *pGameVolume)
{
	WorldEventVolumeProperties *pEventData;
	bool bSendEvent = true;

	if (!pGameVolume) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	pEventData = pEntry->server_volume.event_volume_properties;

	//first enter -- this can have game actions because it can't get spammed.
	if(pEventData->first_entered_action && pEnt && pEnt->pPlayer
	&& (!pEventData->entered_cond || volume_EvaluateExpr(entGetPartitionIdx(pEnt), pGameVolume, pEnt, pEventData->entered_cond))){
		if(eaFind(&pEnt->pPlayer->InteractStatus.eaFirstEnterEventVolumes, pGameVolume->pcName) == -1){
			//this ent hasn't entered this volume yet.
			ItemChangeReason reason = {0};
			inv_FillItemChangeReason(&reason, pEnt, "Volume:VolumeFirstEnterGameAction", pGameVolume->pcName);
			gameaction_RunActions(pEnt, pEventData->first_entered_action, &reason, NULL, NULL);
			// If this volume has a first enter action, track that we entered it
			eaPushUnique(&pEnt->pPlayer->InteractStatus.eaFirstEnterEventVolumes, pGameVolume->pcName);
		}
	}

	//every enter
	if (pEventData->entered_cond) {
		bSendEvent = volume_EvaluateExpr(entGetPartitionIdx(pEnt), pGameVolume, pEnt, pEventData->entered_cond);
	}
	if (bSendEvent) {
		eventsend_RecordVolumeEntered(pEnt, pGameVolume);

		// If this volume has an enter condition, track that we sent the event
		if (pEnt && pEnt->pPlayer && pEventData->entered_cond) {
			eaPushUnique(&pEnt->pPlayer->InteractStatus.eaInEventVolumes, pGameVolume->pcName);
		}
	}

	PERFINFO_AUTO_STOP();
}


static void volume_EventVolumeRemainCB(WorldVolumeEntry *pEntry, Entity *pEnt)
{
	WorldEventVolumeProperties *pEventData;

	if (!pEnt || !pEnt->pPlayer) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	// If this volume has an entry condition, check if we already sent event
	// Note that while non-conditional entry/exit events work on critters, conditional ones only work on players
	pEventData = pEntry->server_volume.event_volume_properties;
	if (pEventData->entered_cond) {
		GameNamedVolume *pGameVolume = volume_GetByEntry(pEntry);
		if (pGameVolume && (eaFind(&pEnt->pPlayer->InteractStatus.eaInEventVolumes, pGameVolume->pcName) == -1)) {
			// Event not sent, so see if we need to send it
			bool bEntered = volume_EvaluateExpr(entGetPartitionIdx(pEnt), pGameVolume, pEnt, pEventData->entered_cond);
			if (bEntered) {
				eventsend_RecordVolumeEntered(pEnt, pGameVolume);
				eaPushUnique(&pEnt->pPlayer->InteractStatus.eaInEventVolumes, pGameVolume->pcName);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


static void volume_EventVolumeExitedCB(WorldVolumeEntry *pEntry, Entity *pEnt, GameNamedVolume *pGameVolume)
{
	WorldEventVolumeProperties *pEventData;
	bool bSendEvent = true;

	if (!pGameVolume) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	pEventData = pEntry->server_volume.event_volume_properties;
	if (pEventData->exited_cond) {
		bSendEvent = volume_EvaluateExpr(entGetPartitionIdx(pEnt), pGameVolume, pEnt, pEventData->exited_cond);
	}
	if (bSendEvent) {
		eventsend_RecordVolumeExited(pEnt, pGameVolume);
	}
	// Remove any tracking for this event volume
	if (pEnt && pEnt->pPlayer) {
		eaFindAndRemove(&pEnt->pPlayer->InteractStatus.eaInEventVolumes, pGameVolume->pcName);
	}

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Volume Utilities
// ----------------------------------------------------------------------------------


bool volume_VolumeExists(const char *pcVolumeName, const WorldScope *pScope)
{
	return volume_GetByName(pcVolumeName, pScope) != NULL;
}


// Gets a volume's center position.  Returns true if volume exists.
bool volume_GetCenterPosition(int iPartitionIdx, const char *pcVolumeName, const WorldScope *pScope, Vec3 vPosition)
{
	GameNamedVolume *pVolume = volume_GetByName(pcVolumeName, pScope);

	if (pVolume) {
		WorldVolume *pWorldVolume;

		// Validation test to help locate corruption in volume entries
		if (eaFind(&s_eaWorldVolumes, pVolume->pNamedVolume->entry) == -1) {
			printf("Volume '%s' refers to an invalid cell entry during volume_GetCenterPosition.  (This is a software and not a data problem.  This can occur normally during a map edit change but should not occur at other times.)\n", pVolume->pcName);
			return false;
		}

		pWorldVolume = eaGet(&pVolume->pNamedVolume->entry->eaVolumes, iPartitionIdx);
		if (pWorldVolume) {
			// Get middle of volume from world layer
			wlVolumeGetVolumeWorldMid(pWorldVolume, vPosition);
		}
		return true;
	}
	return false;
}

void volume_GetWarpConnections(DoorConn ***eaDoors)
{
	FOR_EACH_VOLUME(pVolume) 
	{
		WorldWarpVolumeProperties *pProps = pVolume->pNamedVolume->entry->server_volume.warp_volume_properties;
		if(pProps && pProps->warpDest.eDefaultType!=WVARDEF_CHOICE_TABLE)
		{
			WorldVariable* dest = NULL;
			WorldVariable** variables = NULL;

			dest = worldVariableCalcVariableAndAlloc(PARTITION_STATIC_CHECK, &pProps->warpDest, NULL, 0, 0);

			if(dest && dest->eType==WVAR_MAP_POINT && !dest->pcZoneMap)
			{
				GameSpawnPoint *pSpawn = spawnpoint_GetByNameForSpawning(dest->pcStringVal, 
											SAFE_MEMBER2(pVolume, pNamedVolume, common_data.closest_scope));

				if(pSpawn)
				{
					int i;

					for(i=eaSize(&pVolume->pNamedVolume->entry->eaVolumes)-1; i>=0; --i) {
						if (pVolume->pNamedVolume->entry->eaVolumes[i]) {
							DoorConn *door = calloc(1, sizeof(DoorConn));
							wlVolumeGetVolumeWorldMid(pVolume->pNamedVolume->entry->eaVolumes[i], door->src);
							copyVec3(pSpawn->pWorldPoint->spawn_pos, door->dst);

							eaPush(eaDoors, door);
							break;
						}
					}
				}
			}

			StructDestroy(parse_WorldVariable, dest);
		}
	} 
	FOR_EACH_END;
}


// Gets volume position and size data.  Returns true if volume exists.
bool volume_GetVolumeData(int iPartitionIdx, const char *pcVolumeName, const WorldScope* pScope, Vec3 vCenter, Vec3 vPosition, Vec3 vLocalMin, Vec3 vLocalMax, F32 *pfRot)
{
	GameNamedVolume *pVolume;

	PERFINFO_AUTO_START_FUNC();
	
	pVolume = volume_GetByName(pcVolumeName, pScope);

	if (pVolume) {
		WorldVolume *pWorldVolume;
		Quat qRot;
		Vec3 vUp;

		// Validation test to help locate corruption in volume entries
		if (eaFind(&s_eaWorldVolumes, pVolume->pNamedVolume->entry) == -1) {
			printf("Volume '%s' refers to an invalid cell entry during volume_GetVolumeData.  (This is a software and not a data problem.  This can occur normally during a map edit change but should not occur at other times.)\n", pVolume->pcName);
			PERFINFO_AUTO_STOP();
			return false;
		}
		pWorldVolume = eaGet(&pVolume->pNamedVolume->entry->eaVolumes, iPartitionIdx);
		if (!pWorldVolume) {
			PERFINFO_AUTO_STOP();
			return false;
		}

		// Get information from world layer
		wlVolumeGetVolumeWorldMid(pWorldVolume, vCenter);
		wlVolumeGetWorldPosRotMinMax(pWorldVolume, vPosition, qRot, vLocalMin, vLocalMax);
		quatToAxisAngle(qRot, vUp, pfRot);
		if (vUp[1] < 0)
			(*pfRot) *= -1;
		PERFINFO_AUTO_STOP();
		return true;
	}

	PERFINFO_AUTO_STOP();
	return false;
}


// Gets the world volume for a volume
WorldVolume *volume_GetWorldVolume(int iPartitionIdx, const char *pcVolumeName, const WorldScope* pScope)
{
	GameNamedVolume *pVolume = volume_GetByName(pcVolumeName, pScope);

	if (pVolume) {
		// Validation test to help locate corruption in volume entries
		if (eaFind(&s_eaWorldVolumes, pVolume->pNamedVolume->entry) == -1) {
			printf("Volume '%s' refers to an invalid cell entry during volume_GetWorldVolume.  (This is a software and not a data problem.  This can occur normally during a map edit change but should not occur at other times.)\n", pVolume->pcName);
			return false;
		}

		return eaGet(&pVolume->pNamedVolume->entry->eaVolumes, iPartitionIdx);
	}
	return NULL;
}


const char *volume_NameFromWorldEntry(WorldVolumeEntry *pEntry)
{
	GameNamedVolume *pGameVolume = volume_GetByEntry(pEntry);
	if (pGameVolume) {
		return pGameVolume->pcName;
	}

	return NULL;
}


void volume_ClearPlayerVolumeTrackingData(Entity *pPlayerEnt)
{
	neighborhood_ClearEntityHoodData(pPlayerEnt);

	if (pPlayerEnt && pPlayerEnt->pPlayer) {
		eaDestroy(&pPlayerEnt->pPlayer->InteractStatus.eaInVolumes);
	}
}


void volume_ClearAllPlayerVolumeTrackingData(void)
{
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	Entity *pPlayerEnt;

	while ((pPlayerEnt = EntityIteratorGetNext(iter)))
	{
		volume_ClearPlayerVolumeTrackingData(pPlayerEnt);
	}
	EntityIteratorRelease(iter);
}

//This returns true if the vec3 param is located inside of the passed in volume.
//This does not work for hull volumes at the moment.
bool volume_IsPointInVolume(Vec3 v3Point, WorldVolumeEntry *pEntry)
{
	if (pEntry && pEntry->elements)
	{
		S32 i;
		Vec3 v3Temp, v3Local;
		bool bInVolume = false;

		for (i = 0; i < eaSize(&pEntry->elements); ++i)
		{
			subVec3(v3Point, pEntry->elements[i]->world_mat[3], v3Temp);
			mulVecMat3Transpose(v3Temp, pEntry->elements[i]->world_mat, v3Local);

			if (pEntry->elements[i]->volume_shape == WL_VOLUME_BOX)
			{
				bInVolume = bInVolume || pointBoxCollision(v3Local, pEntry->elements[i]->local_min, pEntry->elements[i]->local_max);
			}
			else if (pEntry->elements[i]->volume_shape == WL_VOLUME_SPHERE)
			{
				Vec3 v3origin = {0};
				bInVolume = bInVolume || distance3(v3Local, v3origin) < pEntry->elements[i]->radius;
			}
			else
			{
				//Not supporting hulls at the moment. If we need to then we can add it later ~DHOGBERG 1/14/2013
			}

			if (bInVolume)
			{
				return bInVolume;
			}
		}

		return bInVolume;
	}

	return false;
}


// ----------------------------------------------------------------------------------
// Named Volume Tracking Logic
// ----------------------------------------------------------------------------------

static void volume_FreePartitionState(VolumePartitionState *pState)
{
	free(pState);
}

static void volume_FreeNamedVolume(GameNamedVolume *pGameVolume)
{
	eaDestroyStruct(&pGameVolume->eaOverrides, parse_GameInteractableOverride);
	eaDestroyEx(&pGameVolume->eaPartitionStates, volume_FreePartitionState);
	free(pGameVolume);
}


static void volume_AddNamedVolume(const char *pcName, WorldNamedVolume *pNamedVolume)
{
	GameNamedVolume *pGameVolume;
	WorldVolumeEntry *pEntry = pNamedVolume->entry;
	const char *pcFilename = layerGetFilename(pNamedVolume->common_data.layer);

	if (!pEntry) {
		return;
	}

	pGameVolume = calloc(1,sizeof(GameNamedVolume));
	if (pcName) {
		pGameVolume->pcName = allocAddString(pcName);
	}
	pGameVolume->pNamedVolume = pNamedVolume;
	eaPush(&s_eaNamedVolumes, pGameVolume);
	if( !s_stNameToGameVolume ) {
		s_stNameToGameVolume = stashTableCreate( 8, StashDefault, StashKeyTypeAddress, sizeof( void* ));
	}
	if( pGameVolume->pcName ) {
		stashAddPointer(s_stNameToGameVolume, pGameVolume->pcName, pGameVolume, true);
	}
	if( !s_stEntryToGameVolume ) {
		s_stEntryToGameVolume = stashTableCreate( 8, StashDefault, StashKeyTypeAddress, sizeof( void* ));
	}
	stashAddPointer(s_stEntryToGameVolume, pNamedVolume->entry, pGameVolume, true);
	
	exprContextSetScope(s_pVolumeContext, SAFE_MEMBER2(pGameVolume, pNamedVolume, common_data.closest_scope));
	exprContextSetScope(g_pInteractionNonPlayerContext, SAFE_MEMBER2(pGameVolume, pNamedVolume, common_data.closest_scope));

	if (gUseScopedExpr) {
		// If it's an action volume, set up expressions
		if (pEntry->server_volume.action_volume_properties) {
			WorldActionVolumeProperties *pActionData = pEntry->server_volume.action_volume_properties;

			if (pActionData->entered_action) {
				exprGenerate(pActionData->entered_action, s_pVolumeContext);
			}
			if (pActionData->exited_action) {
				exprGenerate(pActionData->exited_action, s_pVolumeContext);
			}
			if (pActionData->entered_action_cond) {
				exprGenerate(pActionData->entered_action_cond, s_pVolumeContext);
			}
			if (pActionData->exited_action_cond) {
				exprGenerate(pActionData->exited_action_cond, s_pVolumeContext);
			}
		}

		// If it's a power volume, set up expression
		if (pEntry->server_volume.power_volume_properties) {
			WorldPowerVolumeProperties *pPowerData = pEntry->server_volume.power_volume_properties;

			if (pPowerData->trigger_cond) {
				exprGenerate(pPowerData->trigger_cond, s_pVolumeContext);
			}
		}
	
		// If it's an event volume, set up expressions
		if (pEntry->server_volume.event_volume_properties) {
			WorldEventVolumeProperties *pEventData = pEntry->server_volume.event_volume_properties;
			if (pEventData->first_entered_action){
				gameaction_GenerateActions(&pEventData->first_entered_action->eaActions, NULL, pcFilename);
			}
			if (pEventData->entered_cond) {
				exprGenerate(pEventData->entered_cond, s_pVolumeContext);
			}
			if (pEventData->exited_cond) {
				exprGenerate(pEventData->exited_cond, s_pVolumeContext);
			}
		}

		// If it's an optional action volume, set up expressions
		if (pEntry->server_volume.interaction_volume_properties)
		{
			WorldInteractionProperties *pInteractionProps = pEntry->server_volume.interaction_volume_properties;
			int i;

			for (i = 0; i < eaSize(&pInteractionProps->eaEntries); i++)
				interaction_InitPropertyEntry(pInteractionProps->eaEntries[i], s_pVolumeContext, pcFilename, "Volume", pcName, true);
		}

		// If it's a warp volume, set up expressions
		if (pEntry->server_volume.warp_volume_properties)
		{
			WorldWarpVolumeProperties *pWarpData = pEntry->server_volume.warp_volume_properties;
			int i;

			worldVariableDefGenerateExpressions(&pWarpData->warpDest, pcName, pcFilename);

			for(i=0; i < eaSize(&pWarpData->variableDefs); i++)
			{
				worldVariableDefGenerateExpressions(pWarpData->variableDefs[i], pcName, pcFilename);
			}

			if (pWarpData->warp_cond) {
				exprGenerate(pWarpData->warp_cond, s_pVolumeContext);
			}
		}
	}

	// Clear data set into contexts
	exprContextSetScope(s_pVolumeContext, NULL);
	exprContextSetScope(g_pInteractionNonPlayerContext, NULL);
}


static void volume_ClearNamedVolumeList(void)
{
	eaDestroyEx(&s_eaNamedVolumes, volume_FreeNamedVolume);
	stashTableClear(s_stNameToGameVolume);
	stashTableClear(s_stEntryToGameVolume);
}

void volume_ClearVolumeOverrides(void)
{
	FOR_EACH_VOLUME(pVolume) {
		if(pVolume->eaOverrides)
		{
			eaDestroyStruct(&pVolume->eaOverrides, parse_GameInteractableOverride);
		}
	} FOR_EACH_END;
}

void volume_GenerateOverrideExpressions(void)
{
	FOR_EACH_VOLUME(pVolume) {
		if(pVolume->eaOverrides)
		{
			int j;
			for (j=0; j < eaSize(&pVolume->eaOverrides); j++) 
			{
				interaction_InitPropertyEntry(pVolume->eaOverrides[j]->pEntry, s_pVolumeContext, pVolume->eaOverrides[j]->pcFilename, "Override on Volume", pVolume->pcName, true);
			}
		}
	} FOR_EACH_END;
}

void volume_InitOverridesMatchingName(const char* pchName)
{
	FOR_EACH_VOLUME(pVolume) 
	{
		int j;
		for (j=0; j < eaSize(&pVolume->eaOverrides); j++) 
		{
			if(stricmp(pVolume->eaOverrides[j]->pcName, pchName) == 0)
			{
				interaction_InitPropertyEntry(pVolume->eaOverrides[j]->pEntry, s_pVolumeContext, pVolume->eaOverrides[j]->pcFilename, "Override on Volume", pVolume->pcName, true);
			}
		}
	}
	FOR_EACH_END;
}


void volume_MapValidate(void)
{
	// Check that no two volumes have the same name on the map
	FOR_EACH_VOLUME(pVolume1) {
		FOR_EACH_VOLUME2(pVolume1, pVolume2) {
			if (stricmp(pVolume1->pcName, pVolume2->pcName) == 0) {
				Errorf("Map has more than one volume with name '%s'.  All volumes must have unique names.", pVolume1->pcName);
			}
		} FOR_EACH_END;
	} FOR_EACH_END;

	// TODO: Find out if null entries are legal or not
	//// Check that all named volumes have a world volume
	//for(i=eaSize(&s_eaNamedVolumes)-1; i>=0; --i) {
	//	if (!s_eaNamedVolumes[i]->pNamedVolume->entry) {
	//		Errorf("Volume '%s' is missing its cell entry.  (This is a software and not a data problem.)", s_eaNamedVolumes[i]->pcName);
	//	}
	//}

	// Validate each volume by type
	FOR_EACH_VOLUME(pVolume) {
		if (pVolume->pNamedVolume->entry) {
			if (pVolume->pNamedVolume->entry->server_volume.action_volume_properties) {
				volume_ValidateActionVolume(pVolume);
			}
			if (pVolume->pNamedVolume->entry->server_volume.power_volume_properties) {
				volume_ValidatePowerVolume(pVolume);
			}
			if (pVolume->pNamedVolume->entry->server_volume.warp_volume_properties) {
				volume_ValidateWarpVolume(pVolume);
			}
			if (pVolume->pNamedVolume->entry->server_volume.neighborhood_volume_properties) {
				neighborhood_ValidateNeighborhoodVolume(pVolume);
			}
			if (pVolume->pNamedVolume->entry->server_volume.interaction_volume_properties) {
				volume_ValidateInteractionVolume(pVolume, pVolume->pNamedVolume->common_data.closest_scope);
			}
			if (pVolume->pNamedVolume->entry->server_volume.map_level_volume_properties) {
				volume_ValidateMapLevelOverrideVolume(pVolume);
			}
		}
	} FOR_EACH_END;

	// Validate systems with their own data
	landmark_MapValidate();
}


void volume_PartitionInit(int iPartitionIdx)
{
	// Make sure that volume tracking is accurate
	FOR_EACH_VOLUME(pVolume) {
		WorldVolumeEntry *pEntry = SAFE_MEMBER(pVolume->pNamedVolume, entry);
		if (pEntry) {
			WorldVolume *pWorldVolume = eaGet(&pEntry->eaVolumes, iPartitionIdx);

			// Register for entry/remain/exit callbacks
			if (pWorldVolume) {
				wlVolumeSetQueryCallbacks(pWorldVolume, 
					volume_NeedsEnterCB(pEntry) ? volume_VolumeEnteredCB : NULL, 
					volume_NeedsExitCB(pEntry) ? volume_VolumeExitedCB : NULL, 
					volume_NeedsRemainCB(pEntry) ? volume_VolumeRemainCB : NULL);
			}
		}

		volume_UpdateVolumeTracking(iPartitionIdx, pVolume);
	} FOR_EACH_END;
}


void volume_PartitionLoad(int iPartitionIdx, bool bFullInit)
{
	PERFINFO_AUTO_START_FUNC();

	// Partition state is created only if needed, so normally don't create here.
	// But do need to clear on a full init
	if (bFullInit) {
		FOR_EACH_VOLUME(pVolume) {
			VolumePartitionState *pState = eaGet(&pVolume->eaPartitionStates, iPartitionIdx);
			if (pState) {
				volume_FreePartitionState(pState);
				eaSet(&pVolume->eaPartitionStates, NULL, iPartitionIdx);
			}
		} FOR_EACH_END;
	}

	volume_PartitionInit(iPartitionIdx);

	PERFINFO_AUTO_STOP();
}


void volume_PartitionUnload(int iPartitionIdx)
{
	FOR_EACH_VOLUME(pVolume) {
		VolumePartitionState *pState = eaGet(&pVolume->eaPartitionStates, iPartitionIdx);
		if (pState) {
			volume_FreePartitionState(pState);
			eaSet(&pVolume->eaPartitionStates, NULL, iPartitionIdx);
		}
	} FOR_EACH_END;
}


void volume_MapLoad(ZoneMap *pZoneMap)
{
	WorldZoneMapScope *pScope;
	int i;

	// Clear all data
	s_bVolumesLoaded = false;
	volume_ClearNamedVolumeList();

	// Get zone map scopes
	pScope = zmapGetScope(pZoneMap);

	// Find all named volumes in all scopes
	if(pScope) {
		for(i=eaSize(&pScope->named_volumes)-1; i>=0; --i) {
			const char *pcName = worldScopeGetObjectName(&pScope->scope, &pScope->named_volumes[i]->common_data);
			volume_AddNamedVolume(pcName, pScope->named_volumes[i]);
		}
	}

	//// Debug printing
	//printf("## MAP LOAD\n");
	//for(i=eaSize(&s_eaNamedVolumes)-1; i>=0; --i) {
	//	GameNamedVolume *pGameVolume = s_eaNamedVolumes[i];
	//	if (pGameVolume->pcName) {
	//		printf("## Volume Name='%s' (%p)\n", pGameVolume->pcName, pGameVolume->pNamedVolume->entry);
	//	} else {
	//		printf("## Unnamed Volume (%p)\n", pGameVolume->pNamedVolume->entry);
	//	}
	//}

	FOR_EACH_WORLD_VOLUME(pEntry) {
		if (pEntry->server_volume.beacon_volume_properties) {
			if (pEntry->server_volume.beacon_volume_properties->nodynconn) {
				volume_addBeaconNoDynConn(pEntry);
			}
		}
	} FOR_EACH_END;

	// Refresh query data on each partition
	partition_ExecuteOnEachPartition(volume_PartitionInit);

	s_bVolumesLoaded = true;
}


void volume_MapUnload(void)
{
	s_bVolumesLoaded = false;
	volume_ClearNamedVolumeList();

	volume_ClearAllPlayerVolumeTrackingData();
}


void volume_ResetVolumes(void)
{
	volume_MapUnload();
	volume_MapLoad(worldGetActiveMap());
}

bool volume_AreVolumesLoaded()
{
	return s_bVolumesLoaded;
}


// ----------------------------------------------------------------------------------
// World Volume Enter/Remain/Exit
// ----------------------------------------------------------------------------------

static bool volume_NeedsEnterCB(WorldVolumeEntry *pEntry)
{
	GameNamedVolume *pGameVolume = volume_GetByEntry(pEntry);
	return (pEntry->server_volume.action_volume_properties || 
			pEntry->server_volume.power_volume_properties || 
			pEntry->server_volume.warp_volume_properties || 
			pEntry->server_volume.neighborhood_volume_properties || 
			pEntry->server_volume.event_volume_properties ||
			pEntry->server_volume.interaction_volume_properties || 
			(pGameVolume && eaSize(&pGameVolume->eaOverrides) > 0) ||
			(pEntry->volume_type_bits & s_ePetsDisabledVolumeType) ||
			pEntry->room);
}


static bool volume_NeedsRemainCB(WorldVolumeEntry *pEntry)
{
	return (pEntry->server_volume.power_volume_properties ||
		    (pEntry->server_volume.event_volume_properties &&
			 pEntry->server_volume.event_volume_properties->entered_cond)||
			(pEntry->server_volume.warp_volume_properties && 
			 pEntry->server_volume.warp_volume_properties->warp_cond));
}


static bool volume_NeedsExitCB(WorldVolumeEntry *pEntry)
{
	return (pEntry->server_volume.action_volume_properties || 
			pEntry->server_volume.power_volume_properties || 
			pEntry->server_volume.neighborhood_volume_properties || 
			pEntry->server_volume.event_volume_properties ||
			pEntry->server_volume.interaction_volume_properties || 
			(pEntry->volume_type_bits & s_ePetsDisabledVolumeType) ||
			(pEntry->room && aiMastermind_IsMastermindMap()));
}


static void volume_VolumeEnteredCB(WorldVolume *pVolume, WorldVolumeQueryCache *pQueryCache)
{
	Entity *pEnt;
	WorldVolumeEntry *pEntry;
	GameNamedVolume *pGameVolume;

	// Don't do anything if callback is from an alternate query cache
	pEnt = wlVolumeQueryCacheGetData(pQueryCache);
	if (!pEnt || (pQueryCache != pEnt->volumeCache)) {
		return;
	}

	ANALYSIS_ASSUME(pEnt);

	PERFINFO_AUTO_START_FUNC();

	// NOTE: If add/remove any types in this list, also update volume_NeedsEnterCB()

	pEntry = wlVolumeGetVolumeData(pVolume);
	pGameVolume = volume_GetByEntry(pEntry);
	//printf("## Entered volume %s\n", pEntry->group_name);

	if (pEntry->server_volume.action_volume_properties) {
		volume_ActionVolumeEnteredCB(pEntry, pEnt, pGameVolume);
	}
	if (pEntry->server_volume.power_volume_properties) {
		volume_PowerVolumeEnteredCB(pEntry, pEnt, pGameVolume);
	}
	if (pEntry->server_volume.warp_volume_properties) {
		volume_WarpVolumeEnteredCB(pEntry, pEnt, pGameVolume);
	}
	if (pEntry->server_volume.neighborhood_volume_properties) {
		neighborhood_VolumeEnteredCB(pEntry, pEnt, pGameVolume);
	}
	if (pEntry->server_volume.event_volume_properties) {
		volume_EventVolumeEnteredCB(pEntry, pEnt, pGameVolume);
	}

	// due to old champions data we need to track all named volumes, otherwise 
	// open missions such as the tutorial mission will fail to add the players
	if (pGameVolume) {
		volume_TrackVolumeEnteredCB(pEntry, pEnt, pGameVolume);
	}

	if (pEntry->room && aiMastermind_IsMastermindMap() && pEntry->room->ai_room) {
		aiMastermindHeat_TrackVolumeEntered(pEntry, pEnt);
	}

	//if it's a player entering a room, update the player's map:
	if (pEntry->room && pEnt->pPlayer){
		gslMapRevealEnterRoomVolumeCB(pEnt);
	}

	// If it's a pet disabled volume, remove all pets
	if (pEnt && pEnt->pPlayer && wlVolumeIsType(pVolume, s_ePetsDisabledVolumeType))
	{
		gslTeam_UnSummonPetsForEnt(entGetPartitionIdx(pEnt), pEnt);
	}

	PERFINFO_AUTO_STOP();
}


static void volume_VolumeRemainCB(WorldVolume *pVolume, WorldVolumeQueryCache *pQueryCache)
{
	WorldVolumeEntry *pEntry;
	Entity *pEnt;

	if (!g_EncounterProcessing) {
		return;
	}

	// Don't do anything if callback is from an alternate query cache
	pEnt = wlVolumeQueryCacheGetData(pQueryCache);
	if (!pEnt || (pQueryCache != pEnt->volumeCache)) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	// NOTE: If add/remove any types in this list, also update volume_NeedsRemainCB()

	pEntry = wlVolumeGetVolumeData(pVolume);
	//printf("## Remaining in volume %s\n", pEntry->group_name);

	if (pEntry->server_volume.power_volume_properties) {
		volume_PowerVolumeRemainCB(pEntry, pEnt);
	}
	if (pEntry->server_volume.event_volume_properties) {
		volume_EventVolumeRemainCB(pEntry, pEnt);
	}
	if (pEntry->server_volume.warp_volume_properties) {
		volume_WarpVolumeRemainCB(pEntry, pEnt);
	}

	PERFINFO_AUTO_STOP();
}


static void volume_VolumeExitedCB(WorldVolume *pVolume, WorldVolumeQueryCache *pQueryCache)
{
	Entity *pEnt;
	WorldVolumeEntry *pEntry;
	int iPartitionIdx;
	GameNamedVolume *pGameVolume;

	// Don't do anything if callback is from an alternate query cache
	pEnt = wlVolumeQueryCacheGetData(pQueryCache);
	if (!pEnt || pQueryCache != pEnt->volumeCache) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	// Skip if partition is being destroyed
	iPartitionIdx = entGetPartitionIdx(pEnt);
	if (partition_IsBeingDestroyed(iPartitionIdx)) { // Destroy test reports "not exists" as being destroyed
		PERFINFO_AUTO_STOP();
		return;
	}

	// NOTE: If add/remove any types in this list, also update volume_NeedsExitCB()

	pEntry = wlVolumeGetVolumeData(pVolume);
	pGameVolume = volume_GetByEntry(pEntry);
	//printf("## Exited volume %s\n", pEntry->group_name);

	if (pEntry->server_volume.action_volume_properties) {
		volume_ActionVolumeExitedCB(pEntry, pEnt, pGameVolume);
	}
	if (pEntry->server_volume.power_volume_properties) {
		volume_PowerVolumeExitedCB(pEntry, pEnt);
	}
	if (pEntry->server_volume.neighborhood_volume_properties) {
		neighborhood_VolumeExitedCB(pEntry, pEnt);
	}
	if (pEntry->server_volume.event_volume_properties) {
		volume_EventVolumeExitedCB(pEntry, pEnt, pGameVolume);
	}

	// due to old champions data we need to track all named volumes, otherwise 
	// open missions such as the tutorial mission will fail to add the players
	if (pGameVolume) {
		volume_TrackVolumeExitedCB(pEntry, pEnt, pGameVolume);
	}

	if (pEntry->room && pEntry->room->ai_room) {
		aiMastermindHeat_TrackVolumeExited(pEntry, pEnt);
	}

	// If it's a pet disabled volume, add all pets that were removed by the volume
	if (pEnt && pEnt->pPlayer && wlVolumeIsType(pVolume, s_ePetsDisabledVolumeType))
	{
		gslTeam_ReSummonPetsForEnt(pEnt);
	}
	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// World Volume Lifecycle Management
// ----------------------------------------------------------------------------------

static void volume_AddGameVolume(WorldVolumeEntry *pEntry)
{
	int i;

	//printf("## Tracking volume (%p) %s\n", pEntry, pEntry->group_name);

	if (beaconIsBeaconizer()) {
		return;
	}

	// Track the entry
	eaPush(&s_eaWorldVolumes, pEntry);

	// If it's a landmark, register it
	if (pEntry->server_volume.landmark_volume_properties) {
		landmark_AddLandmarkVolume(pEntry);
	}

	// Register for entry/remain/exit callbacks
	for(i=eaSize(&pEntry->eaVolumes)-1; i>=0; --i) {
		if (pEntry->eaVolumes[i]) {
			wlVolumeSetQueryCallbacks(pEntry->eaVolumes[i], 
				volume_NeedsEnterCB(pEntry) ? volume_VolumeEnteredCB : NULL, 
				volume_NeedsExitCB(pEntry) ? volume_VolumeExitedCB : NULL, 
				volume_NeedsRemainCB(pEntry) ? volume_VolumeRemainCB : NULL);
		}
	}

	if (pEntry->server_volume.map_level_volume_properties)
	{
		volume_AddMapLevelOverrideVolume(pEntry);
	}

	// AM: Process avoid volumes during map load callback

	// expressions are added in AddNamedVolume, so they have access to
	// the scope
	if (!gUseScopedExpr) {
		// If it's an action volume, set up expressions
		if (pEntry->server_volume.action_volume_properties) {
			WorldActionVolumeProperties *pActionData = pEntry->server_volume.action_volume_properties;

			if (pActionData->entered_action) {
				exprGenerate(pActionData->entered_action, s_pVolumeContext);
			}
			if (pActionData->exited_action) {
				exprGenerate(pActionData->exited_action, s_pVolumeContext);
			}
			if (pActionData->entered_action_cond) {
				exprGenerate(pActionData->entered_action_cond, s_pVolumeContext);
			}
			if (pActionData->exited_action_cond) {
				exprGenerate(pActionData->exited_action_cond, s_pVolumeContext);
			}
		}

		// If it's a power volume, set up expression
		if (pEntry->server_volume.power_volume_properties) {
			WorldPowerVolumeProperties *pPowerData = pEntry->server_volume.power_volume_properties;

			if (pPowerData->trigger_cond) {
				exprGenerate(pPowerData->trigger_cond, s_pVolumeContext);
			}
		}

		// If it's an event volume, set up expressions
		if (pEntry->server_volume.event_volume_properties) {
			WorldEventVolumeProperties *pEventData = pEntry->server_volume.event_volume_properties;
			if (pEventData->first_entered_action){
				gameaction_GenerateActions(&pEventData->first_entered_action->eaActions, NULL, NULL);//Filename?
			}
			if (pEventData->entered_cond) {
				exprGenerate(pEventData->entered_cond, s_pVolumeContext);
			}
			if (pEventData->exited_cond) {
				exprGenerate(pEventData->exited_cond, s_pVolumeContext);
			}
		}

		// If it's a warp volume, set up expression
		if (pEntry->server_volume.warp_volume_properties) {
			WorldWarpVolumeProperties *pWarpData = pEntry->server_volume.warp_volume_properties;
			if (pWarpData->warp_cond) {
				exprGenerate(pWarpData->warp_cond, s_pVolumeContext);
			}
		}
	}
}


static void volume_RemoveGameVolume(WorldVolumeEntry *pEntry)
{
	//printf("## Removing volume (%p) %s\n", pEntry, pEntry->group_name);

	if(beaconIsBeaconizer())
		return;

	// Remove it from the array
	eaFindAndRemove(&s_eaWorldVolumes, pEntry);

	if (pEntry->server_volume.map_level_volume_properties)
	{
		volume_RemoveMapLevelOverrideVolume(pEntry);
	}

	// If it's a landmark unregister it
	if (pEntry->server_volume.landmark_volume_properties) {
		landmark_RemoveLandmarkVolume(pEntry);
	}

	// If it's an AI volume, unregister it
	if (pEntry->server_volume.ai_volume_properties) {
		if (pEntry->server_volume.ai_volume_properties->avoid) {
			volume_removeAIAvoid(pEntry);
		}
	}

	// Unregister nodynconn volumes
	if (pEntry->server_volume.beacon_volume_properties) {
		if (pEntry->server_volume.beacon_volume_properties->nodynconn) {
			volume_removeNoDynConn(pEntry);
		}
	}
}


// ----------------------------------------------------------------------------------
// System Initialization
// ----------------------------------------------------------------------------------

AUTO_RUN;
void volume_InitSystem(void)
{
	g_pchVolumeVarName = allocAddString("Volume");

	// Set up the volume expression context
	s_pVolumeContext = exprContextCreate();
	exprContextSetFuncTable(s_pVolumeContext, encPlayer_CreateExprFuncTable());
	exprContextSetAllowRuntimePartition(s_pVolumeContext);
	exprContextSetAllowRuntimeSelfPtr(s_pVolumeContext);

	s_ePetsDisabledVolumeType = wlVolumeTypeNameToBitMask("PetsDisabled");

	// Register for game volume callbacks
	worldVolumeEntrySetGameCallbacks(volume_AddGameVolume, volume_RemoveGameVolume);
}

