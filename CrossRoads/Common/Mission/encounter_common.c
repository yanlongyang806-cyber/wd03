/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiJobs.h"
#include "aiStructCommon.h"
#include "Character.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "error.h"
#include "Entity.h"
#include "Expression.h"
#include "GameEvent.h"
#include "interaction_common.h"
#include "RegionRules.h"
#include "StateMachine.h"
#include "WorldGrid.h"
#include "AutoGen/Entity_h_ast.h"
#include "EntitySavedData.h"
#include "entitylib.h"

#ifdef GAMESERVER
#include "gslMapVariable.h"
#include "gslVolume.h"
#endif

#include "AutoGen/encounter_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

DictionaryHandle g_hEncounterTemplateDict = NULL;

// context for data-defined encounter difficulties
DefineContext *s_pDefineEncounterDifficulties = NULL;

// Disable all damage everywhere on the partition.  Used to "freeze" the partition for cutscenes.
// Same partition may be pushed onto the list multipule times
// This should always empty zero on the client
int *g_iDamageDisabledPartitions = NULL;

static EncounterTemplateChangeFunc s_TemplateChangeFunc;

#define TEMPLATE_BASE_DIR  "defs/encounters2"
#define TEMPLATE_EXTENSION "encounter2"

// Default level range for calculating the avg. team level.  All teammates within this number of levels from the highest level
// member will be included in the average.
#define DEFAULT_TEAM_LEVEL_RANGE 5

static int siOverrideMapLevel = 0; // Debugging override for map level, if set
AUTO_CMD_INT(siOverrideMapLevel, ForceMapLevel) ACMD_SERVERONLY;

static int siOverrideMapDifficulty = 0; // Debugging override for map difficulty, if set
AUTO_CMD_INT(siOverrideMapDifficulty, ForceMapEncounterDifficulty) ACMD_SERVERONLY;

static int g_iEncounterDifficultyCount = 0;


// --------------------------------------------------------------------------
// Data Support Functions
// --------------------------------------------------------------------------

bool encounter_IsDamageDisabled(int iPartitionIdx)
{
	return (eaiFind(&g_iDamageDisabledPartitions, iPartitionIdx) >= 0);
}


void encounter_SetDamageDisabled(int iPartitionIdx)
{
	eaiPush(&g_iDamageDisabledPartitions, iPartitionIdx);
}


void encounter_RemoveDamageDisabled(int iPartitionIdx)
{
	eaiFindAndRemove(&g_iDamageDisabledPartitions, iPartitionIdx);
}


// Gets values for parameterized values
F32 encounter_GetRadiusValue(WorldEncounterRadiusType eRadius, F32 fCustom, Vec3 vPos)
{
#if defined(GAMESERVER) || defined(GAMECLIENT)
	WorldRegion* pRegion = vPos ? worldGetWorldRegionByPos(vPos) : NULL;
	RegionRules* pRules = pRegion ? getRegionRulesFromRegion(pRegion) : NULL;

	// Check Region set values first
	if (pRules && pRules->eaEncounterDistancePresets) {
		int i;
		for (i = eaSize(&pRules->eaEncounterDistancePresets)-1; i>=0; i--) {
			if (pRules->eaEncounterDistancePresets[i]->eType == eRadius) {
				return pRules->eaEncounterDistancePresets[i]->fValue;
			}
		}
	}
#endif

	// Default values
	switch(eRadius)
	{
		case WorldEncounterRadiusType_None:		return 0;
		case WorldEncounterRadiusType_Short:	return 100;
		case WorldEncounterRadiusType_Medium:	return 300;
		case WorldEncounterRadiusType_Long:		return 600;
		case WorldEncounterRadiusType_Always:	return WORLD_ENCOUNTER_RADIUS_TYPE_ALWAYS_DISTANCE;
		case WorldEncounterRadiusType_Custom:	return fCustom;
		default: assertmsg(0, "Unexpected radius type");
	}
}

F32 encounter_GetTimerValue(WorldEncounterTimerType eTimer, F32 fCustom, const Vec3 vPos)
{
#if defined(GAMESERVER) || defined(GAMECLIENT)
	WorldRegion* pRegion = vPos ? worldGetWorldRegionByPos(vPos) : NULL;
	RegionRules* pRules = pRegion ? getRegionRulesFromRegion(pRegion) : NULL;

	// Check Region set values first
	if (pRules && pRules->eaEncounterTimePresets) {
		int i;
		for (i = eaSize(&pRules->eaEncounterTimePresets)-1; i>=0; i--) {
			if (pRules->eaEncounterTimePresets[i]->eType == eTimer) {
				return pRules->eaEncounterTimePresets[i]->fValue;
			}
		}
	}
#endif

	switch(eTimer)
	{
		case WorldEncounterTimerType_None:		return 0;
		case WorldEncounterTimerType_Never:		return -1;
		case WorldEncounterTimerType_Short:		return 120;
		case WorldEncounterTimerType_Medium:	return 300;
		case WorldEncounterTimerType_Long:		return 1800;
		case WorldEncounterTimerType_Custom:	return fCustom;
		default: assertmsg(0, "Unexpected timer type");
	}
}

F32 encounter_GetWaveTimerValue(WorldEncounterWaveTimerType eTimer, F32 fCustom, const Vec3 vPos)
{
#if defined(GAMESERVER) || defined(GAMECLIENT)
	WorldRegion* pRegion = vPos ? worldGetWorldRegionByPos(vPos) : NULL;
	RegionRules* pRules = pRegion ? getRegionRulesFromRegion(pRegion) : NULL;

	// Check Region set values first
	if (pRules && pRules->eaEncounterWaveTimePresets) {
		int i;
		for (i = eaSize(&pRules->eaEncounterWaveTimePresets)-1; i>=0; i--) {
			if (pRules->eaEncounterWaveTimePresets[i]->eType == eTimer) {
				return pRules->eaEncounterWaveTimePresets[i]->fValue;
			}
		}
	}
#endif

	switch(eTimer)
	{
	case WorldEncounterWaveTimerType_Short:		return 5;
	case WorldEncounterWaveTimerType_Medium:	return 30;
	case WorldEncounterWaveTimerType_Long:		return 120;
	case WorldEncounterWaveTimerType_Immediate:	return 1;
	case WorldEncounterWaveTimerType_Custom:	return fCustom;
	default: assertmsg(0, "Unexpected timer type");
	}
}

void encounter_GetWaveDelayTimerValue(WorldEncounterWaveDelayTimerType eTimer, F32 fCustomMin, F32 fCustomMax, const Vec3 vPos, F32 *fpMin, F32 *fpMax)
{
#if defined(GAMESERVER) || defined(GAMECLIENT)
	WorldRegion* pRegion = vPos ? worldGetWorldRegionByPos(vPos) : NULL;
	RegionRules* pRules = pRegion ? getRegionRulesFromRegion(pRegion) : NULL;

	// Check Region set values first
	if (pRules && pRules->eaEncounterWaveDelayTimePresets) {
		int i;
		for (i = eaSize(&pRules->eaEncounterWaveDelayTimePresets)-1; i>=0; i--) {
			if (pRules->eaEncounterWaveDelayTimePresets[i]->eType == eTimer) {
				(*fpMin) = pRules->eaEncounterWaveDelayTimePresets[i]->fMinValue;
				(*fpMax) = pRules->eaEncounterWaveDelayTimePresets[i]->fMaxValue;
				return;
			}
		}
	}
#endif

	switch(eTimer)
	{
	case WorldEncounterWaveDelayTimerType_None:		(*fpMin) = 0;			(*fpMax) = 0;			return;
	case WorldEncounterWaveDelayTimerType_Short:	(*fpMin) = 0;			(*fpMax) = 5;			return;
	case WorldEncounterWaveDelayTimerType_Medium:	(*fpMin) = 15;			(*fpMax) = 30;			return;
	case WorldEncounterWaveDelayTimerType_Long:		(*fpMin) = 60;			(*fpMax) = 120;			return;
	case WorldEncounterWaveDelayTimerType_Custom:	(*fpMin) = fCustomMin;	(*fpMax) = fCustomMax;	return;
	default: assertmsg(0, "Unexpected timer type");
	}
}


// This is the radius within which an encounter becomes "Active"
F32 encounter_GetActiveRadius(Vec3 vPos)
{
	// TODO: Make this a region ruled value
	return 200;
}


// Can pass "NULL" to get default
WorldEncounterRadiusType encounter_GetSpawnRadiusTypeFromProperties(ZoneMapInfo* pZmapInfo, WorldEncounterProperties *pProps)
{
	if (pProps && pProps->pSpawnProperties) {
		return pProps->pSpawnProperties->eSpawnRadiusType;
	} else {
		return WorldEncounterRadiusType_Medium;
	}
}


F32 encounter_GetSpawnRadiusValueFromProperties(WorldEncounterProperties *pProps, Vec3 vPos)
{
	return encounter_GetRadiusValue(encounter_GetSpawnRadiusTypeFromProperties(NULL, pProps),
									SAFE_MEMBER2(pProps, pSpawnProperties, fSpawnRadius),
									vPos);
}


// Can also pass "NULL" to get default
WorldEncounterTimerType encounter_GetRespawnTimerTypeFromProperties(ZoneMapInfo* pZmapInfo, WorldEncounterProperties *pProps)
{
	if (pProps && pProps->pSpawnProperties) {
		return pProps->pSpawnProperties->eRespawnTimerType;
	} else if ((zmapInfoGetMapType(pZmapInfo) == ZMTYPE_STATIC) || (zmapInfoGetMapType(pZmapInfo) == ZMTYPE_SHARED)) {
		return WorldEncounterTimerType_Medium;
	} else {
		return WorldEncounterTimerType_Never;
	}
}


F32 encounter_GetRespawnTimerValueFromProperties(WorldEncounterProperties *pProps, Vec3 vPos)
{
	return encounter_GetTimerValue(encounter_GetRespawnTimerTypeFromProperties(NULL, pProps),
								   SAFE_MEMBER2(pProps, pSpawnProperties, fRespawnTimer),
								   vPos);
}


// Gets an array of teammates including the player who are within a predefined maximum team distance (defined in the regionrules).
// Sets peaTeam to this array of teammates.
void encounter_getTeammatesInRange(Entity* pEntPlayer, Entity*** peaTeam)
{
	Team* pTeam = team_GetTeam(pEntPlayer);
	RegionRules* pRules = NULL;
	Vec3 vPos = {0,0,0};
	U32 uiMaxTeamDistance = 500;
	int i;

	if (pEntPlayer) {
		entGetPos(pEntPlayer, vPos);
		pRules = RegionRulesFromVec3(vPos);
		if (pRules) {
			uiMaxTeamDistance = pRules->uiEncounterSpawnTeamDist;
		}
	} else {
		return;
	}

	eaPush(peaTeam, pEntPlayer);

	if (pTeam) {
		ZoneMapType eType = zmapInfoGetMapType(NULL);
		team_GetOnMapEntsUnique(entGetPartitionIdx(pEntPlayer), peaTeam, pTeam, gConf.bEncountersScaleWithActivePets);

		if (eType != ZMTYPE_MISSION || !gConf.bDisableEncounterTeamSizeRangeCheckOnMissionMaps) { //no range check on mission maps.
			for(i = eaSize(peaTeam)-1; i >= 0; i--) {
				if (entGetDistance(pEntPlayer, NULL, (*peaTeam)[i], NULL, NULL) > uiMaxTeamDistance) {
					eaRemove(peaTeam, i);
				}
			}
		}
	} else if (gConf.bEncountersScaleWithActivePets && pEntPlayer->pSaved) {
		S32 iOwnedPetsSize = ea32Size(&pEntPlayer->pSaved->ppAwayTeamPetID);
		int iPartitionIdx = entGetPartitionIdx(pEntPlayer);

		for ( i = 0; i < iOwnedPetsSize; i++ ) {
			Entity *pPetEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYSAVEDPET,pEntPlayer->pSaved->ppAwayTeamPetID[i]);
			if (pPetEnt) {
				eaPushUnique(peaTeam,pPetEnt);
			}
		}

		// Also add the critter pets
		FOR_EACH_IN_EARRAY_FORWARDS(pEntPlayer->pSaved->ppCritterPets, CritterPetRelationship, pRelationship) {
			if (pRelationship->pEntity) {
				eaPushUnique(peaTeam, pRelationship->pEntity);
			}
		}
		FOR_EACH_END
	}
}


// Finds all teammates within range of the player, and computes the average level of all the players given they are within 
// a predefined amount of levels of the highest level player found
// number of players in range can be returned in optional pCount
int encounter_getTeamLevelInRange(Entity* pEntPlayer, int *piCount, bool bUseCachedLevel)
{
	Entity** eaTeam = NULL;
	int iCount = 0;
	int iAverage = 0;
	int iMaxLevel = 0;
	int i;

	if (piCount)
	{
		*piCount = 0;
	}
	
	if (!pEntPlayer) {
		return -1;
	}

	eaCreate(&eaTeam);
	encounter_getTeammatesInRange(pEntPlayer, &eaTeam);
	iMaxLevel = entity_GetCombatLevel(pEntPlayer);	// use this function to guarantee a valid level.

	for(i = eaSize(&eaTeam)-1; i>=0; i--) {
		if (eaTeam[i]->pChar && eaTeam[i]->pChar->iLevelCombat > iMaxLevel) {
			iMaxLevel = eaTeam[i]->pChar->iLevelCombat;
		}
	}

	for(i = eaSize(&eaTeam)-1; i>=0; i--) {
		if (eaTeam[i]->pChar && (iMaxLevel - eaTeam[i]->pChar->iLevelCombat < DEFAULT_TEAM_LEVEL_RANGE) ) {
			iAverage += eaTeam[i]->pChar->iLevelCombat;
			iCount++;
		}
	}

	if (iCount) {
		iAverage = iAverage / iCount;
	} else {
		iAverage = iMaxLevel;
	}

	eaDestroy(&eaTeam);
	
	if (piCount) {
		*piCount = iCount;
	}
	
	// Used cached level, this is for map transfers to prevent 1st character in level being used for spawns
	if (bUseCachedLevel && pEntPlayer->pTeam && pEntPlayer->pTeam->iAverageTeamLevel > 0) {
		Team *pTeam = team_GetTeam(pEntPlayer);
		if(pTeam && team_NumTotalMembers(pTeam) > iCount && pEntPlayer->pTeam->iAverageTeamLevelTime > timeServerSecondsSince2000()) {
			iAverage = pEntPlayer->pTeam->iAverageTeamLevel;
		}
#ifdef GAMESERVER
		else {
			// no longer valid
			pEntPlayer->pTeam->iAverageTeamLevel = 0;
			entity_SetDirtyBit(pEntPlayer, parse_PlayerTeam, pEntPlayer->pTeam, true);
		}
#endif
	}

	return iAverage;
}


// --------------------------------------------------------------------------
// Encounter Access Functions
// --------------------------------------------------------------------------

void encounterTemplate_FillActorEarray(EncounterTemplate* pTemplate, EncounterActorProperties*** peaActorsToFill)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		int i, j;
		bool newActor;

		if (pParentTemplate) {
			encounterTemplate_FillActorEarray(pParentTemplate, peaActorsToFill);
		}

		for (i = 0; i < eaSize(&pTemplate->eaActors); i++) {
			newActor = true;
			for (j = 0; j < eaSize(peaActorsToFill); j++) {
				if (pTemplate->eaActors[i]->pcName == (*peaActorsToFill)[j]->pcName) {
					newActor = false;
					(*peaActorsToFill)[j] = pTemplate->eaActors[i];
					break;
				}
			}
			if (newActor) {
				eaPush(peaActorsToFill, pTemplate->eaActors[i]);
			}
		}
	}
}


void encounterTemplate_FillTrackedEventsEarrays(EncounterTemplate* pTemplate, GameEvent*** peaTrackedEvents, GameEvent*** peaTrackedEventsSinceSpawn, GameEvent*** peaTrackedEventsSinceComplete)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		int i;

		if (pParentTemplate) {
			encounterTemplate_FillTrackedEventsEarrays(pParentTemplate, peaTrackedEvents, peaTrackedEventsSinceSpawn, peaTrackedEventsSinceComplete);
		}

		for (i = 0; i < eaSize(&pTemplate->eaTrackedEvents); i++) {
			eaPush(peaTrackedEvents, pTemplate->eaTrackedEvents[i]);
		}
		for (i = 0; i < eaSize(&pTemplate->eaTrackedEventsSinceSpawn); i++) {
			eaPush(peaTrackedEventsSinceSpawn, pTemplate->eaTrackedEventsSinceSpawn[i]);
		}
		for (i = 0; i < eaSize(&pTemplate->eaTrackedEventsSinceComplete); i++) {
			eaPush(peaTrackedEventsSinceComplete, pTemplate->eaTrackedEventsSinceComplete[i]);
		}
	}
}


void encounterTemplate_FillActorInteractionEarray(EncounterTemplate* pTemplate, EncounterActorProperties* pActor, WorldInteractionPropertyEntry*** eaInteractionsToFill)
{
	if (pTemplate && pActor) {
		EncounterTemplate* pParentTemplate;
		EncounterActorProperties* pActualActor;
		int i;

		PERFINFO_AUTO_START_FUNC();

		pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		//make sure we're dealing with an actor with actual data we want
		pActualActor = encounterTemplate_GetActorByName(pTemplate, pActor->pcName);

		if (pActualActor) {
			if (pParentTemplate) {
				encounterTemplate_FillActorInteractionEarray(pParentTemplate, pActor, eaInteractionsToFill);
			}
			if (pActualActor->pInteractionProperties) {
				for (i = 0; i < eaSize(&pActualActor->pInteractionProperties->eaEntries); i++) {
					eaPush(eaInteractionsToFill, pActualActor->pInteractionProperties->eaEntries[i]);
				}
			}
		}

		PERFINFO_AUTO_STOP();
	}
}


WorldInteractionProperties* encounterTemplate_GetActorInteractionProps(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	WorldInteractionProperties *pProps = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (pActor && !pActor->pInteractionProperties && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		pProps = encounterTemplate_GetActorInteractionProps(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));

	} else if (pActor) {
		pProps = pActor->pInteractionProperties;
	}

	PERFINFO_AUTO_STOP();

	return pProps;
}


EncounterLevelProperties* encounterTemplate_GetLevelProperties(EncounterTemplate* pTemplate)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		if (pParentTemplate && (!pTemplate->pLevelProperties || !pTemplate->pLevelProperties->bOverrideParentValues)) {
			return encounterTemplate_GetLevelProperties(pParentTemplate);
		}
		return pTemplate->pLevelProperties;
	}
	return NULL;
}


EncounterDifficultyProperties* encounterTemplate_GetDifficultyProperties(EncounterTemplate* pTemplate)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		if (pParentTemplate) {
			return encounterTemplate_GetDifficultyProperties(pParentTemplate);
		}
		return pTemplate->pDifficultyProperties;
	}
	return NULL;
}

EncounterRewardProperties* encounterTemplate_GetRewardProperties(EncounterTemplate *pTemplate)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		if (pParentTemplate && (!pTemplate->pRewardProperties || pTemplate->pRewardProperties->bOverrideParentValues)) {
			return encounterTemplate_GetRewardProperties(pParentTemplate);
		}
		return pTemplate->pRewardProperties;
	}
	return NULL;
}

EncounterAIProperties* encounterTemplate_GetAIProperties(EncounterTemplate* pTemplate)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		if (pParentTemplate && (!pTemplate->pSharedAIProperties || !pTemplate->pSharedAIProperties->bOverrideParentValues)) {
			return encounterTemplate_GetAIProperties(pParentTemplate);
		}
		return pTemplate->pSharedAIProperties;
	}
	return NULL;
}


EncounterSpawnProperties* encounterTemplate_GetSpawnProperties(EncounterTemplate* pTemplate)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		if (pParentTemplate && (!pTemplate->pSpawnProperties || !pTemplate->pSpawnProperties->bOverrideParentValues)) {
			return encounterTemplate_GetSpawnProperties(pParentTemplate);
		}
		return pTemplate->pSpawnProperties;
	}
	return NULL;
}


EncounterCritterOverrideType encounterTemplate_GetFactionSource(EncounterTemplate* pTemplate)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		if (pParentTemplate && (!pTemplate->pActorSharedProperties || !pTemplate->pActorSharedProperties->bOverrideParentValues)) {
			return encounterTemplate_GetFactionSource(pParentTemplate);
		}

		if (pTemplate->pActorSharedProperties) {
			return pTemplate->pActorSharedProperties->eFactionType;	
		}
	}
	return EncounterCritterOverrideType_FromCritter;	// default override type
}


CritterFaction* encounterTemplate_GetFaction(EncounterTemplate* pTemplate)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		if (pParentTemplate && (!pTemplate->pActorSharedProperties || !pTemplate->pActorSharedProperties->bOverrideParentValues)) {
			return encounterTemplate_GetFaction(pParentTemplate);
		}

		if (pTemplate->pActorSharedProperties && encounterTemplate_GetFactionSource(pTemplate) == EncounterCritterOverrideType_Specified) {
			return GET_REF(pTemplate->pActorSharedProperties->hFaction);
		}
	}
	return NULL;
}


int encounterTemplate_GetGangID(EncounterTemplate* pTemplate)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		if (pParentTemplate && (!pTemplate->pActorSharedProperties || !pTemplate->pActorSharedProperties->bOverrideParentValues)) {
			return encounterTemplate_GetGangID(pParentTemplate);
		}

		if (pTemplate->pActorSharedProperties && encounterTemplate_GetFactionSource(pTemplate) == EncounterCritterOverrideType_Specified) {
			return pTemplate->pActorSharedProperties->iGangID;
		}
	}
	return 0;
}


void encounterTemplate_FillAIJobEArray(EncounterTemplate* pTemplate, AIJobDesc*** peaJobsToFill)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		int i;

		if (pParentTemplate) {
			encounterTemplate_FillAIJobEArray(pParentTemplate, peaJobsToFill);
		}

		for (i = 0; i < eaSize(&pTemplate->eaJobs); i++) {
			eaPush(peaJobsToFill, pTemplate->eaJobs[i]);
		}
	}
}


EncounterWaveProperties* encounterTemplate_GetWaveProperties(EncounterTemplate* pTemplate)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		if (pParentTemplate && (!pTemplate->pWaveProperties || !pTemplate->pWaveProperties->bOverrideParentValues)) {
			return encounterTemplate_GetWaveProperties(pParentTemplate);
		}

		return pTemplate->pWaveProperties;
	}
	return NULL;
}


EncounterActorSharedProperties* encounterTemplate_GetActorSharedProperties(EncounterTemplate* pTemplate)
{
	if (pTemplate) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		if (pParentTemplate && (!pTemplate->pActorSharedProperties || !pTemplate->pActorSharedProperties->bOverrideParentValues)) {
			return encounterTemplate_GetActorSharedProperties(pParentTemplate);
		}

		return pTemplate->pActorSharedProperties;
	}
	return NULL;
}


EncounterActorProperties *encounterTemplate_GetActorByName(EncounterTemplate *pTemplate, const char *pcName)
{
	int i;

	if (pTemplate && pcName) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);

		if (pTemplate->eaActors) {
			for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
				EncounterActorProperties *pActor = pTemplate->eaActors[i];
				if (pActor->pcName && (stricmp(pActor->pcName, pcName) == 0)) {
					return pActor;
				}
			}
		}
		if (pParentTemplate) {
			return encounterTemplate_GetActorByName(pParentTemplate, pcName);
		}
	}

	return NULL;
}


EncounterActorProperties *encounterTemplate_GetActorByIndex(EncounterTemplate *pTemplate, int iActorIndex)
{
	if (pTemplate && iActorIndex >= 0) {
		EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
		EncounterActorProperties **eaActors = NULL;

		encounterTemplate_FillActorEarray(pTemplate, &eaActors);

		if (eaActors) {
			if (iActorIndex < eaSize(&eaActors)) {
				EncounterActorProperties *pTmpActorProperties = eaActors[iActorIndex];
				eaDestroy(&eaActors);
				return pTmpActorProperties;
			}
			eaDestroy(&eaActors);
		}
	}

	return NULL;
}


EncounterActorProperties* encounterTemplate_GetActorFromWorldActor(EncounterTemplate *pTemplate, WorldEncounterProperties *pWorldEncounter, WorldActorProperties *pWorldActor)
{
	if (pTemplate && pWorldEncounter && pWorldActor) {
		if (pWorldEncounter->bFillActorsInOrder) {
			return pWorldActor->bActorIndexSet ? encounterTemplate_GetActorByIndex(pTemplate, pWorldActor->iActorIndex) : NULL;
		} else {
			return encounterTemplate_GetActorByName(pTemplate, pWorldActor->pcName);
		}
	}

	return NULL;
}


static int encounterTemplate_GetMapLevel(Vec3 v3Pos)
{
	int iLevel = 1;

#ifdef GAMESERVER
	if (siOverrideMapLevel) {
		iLevel = siOverrideMapLevel;
	} else {
		S32 iVolumeLevelOverride = volume_GetLevelOverrideForPosition(v3Pos);
		
		iLevel = (int)zmapInfoGetMapLevel(NULL);
		
		if (iVolumeLevelOverride >= 0)
		{
			iLevel = iVolumeLevelOverride;
		}
	}	
#endif
	return iLevel;
}


// Get the encounter level
void encounterTemplate_GetLevelRange(EncounterTemplate *pTemplate, Entity *pEnt, int iPartitionIdx, int *piMin, int *piMax, Vec3 v3EncounterPos)
{
	int iMinLevel = 1, iMaxLevel = MAX_LEVELS;
	int iMinOffset = 0, iMaxOffset = 0;
	EncounterLevelProperties *pLevelProperties = NULL;

	if (!pTemplate) {
		return;
	}

	*piMin = *piMax = 0;
	pLevelProperties = encounterTemplate_GetLevelProperties(pTemplate);

	// Default to map level if no level properties
	if (!pLevelProperties) {
#ifdef GAMESERVER
		*piMin = *piMax = encounterTemplate_GetMapLevel(v3EncounterPos);
#endif
		return;
	}

	// Determine min and max clamp range
	if (pLevelProperties->eClampType == EncounterLevelClampType_Specified) {
		if (pLevelProperties->iClampSpecifiedMin) {
			iMinLevel = pLevelProperties->iClampSpecifiedMin;
		}
		if (pLevelProperties->iClampSpecifiedMax) {
			iMaxLevel = pLevelProperties->iClampSpecifiedMax;
		}

	} else if (pLevelProperties->eClampType == EncounterLevelClampType_MapLevel) {
#ifdef GAMESERVER
		int iBaseLevel = encounterTemplate_GetMapLevel(v3EncounterPos);
		iMinLevel = CLAMP(iBaseLevel + pLevelProperties->iClampOffsetMin, 1, MAX_LEVELS);
		iMaxLevel = CLAMP(iBaseLevel + pLevelProperties->iClampOffsetMax, 1, MAX_LEVELS);
#endif

	} else if (pLevelProperties->eClampType == EncounterLevelClampType_MapVariable) {
#ifdef GAMESERVER
		MapVariable *pVar = mapvariable_GetByName(iPartitionIdx, pLevelProperties->pcClampMapVariable);
		if (pVar && pVar->pVariable && pVar->pVariable->eType == WVAR_INT) {
			iMinLevel = CLAMP(pVar->pVariable->iIntVal + pLevelProperties->iClampOffsetMin, 1, MAX_LEVELS);
			iMaxLevel = CLAMP(pVar->pVariable->iIntVal + pLevelProperties->iClampOffsetMax, 1, MAX_LEVELS);
		}
#endif
	}

	// Determine offset range
	if (pLevelProperties->eLevelType != EncounterLevelType_Specified) {
		iMinOffset = pLevelProperties->iLevelOffsetMin;
		iMaxOffset = pLevelProperties->iLevelOffsetMax;
		if (iMinOffset > iMaxOffset) {
			iMaxOffset = iMinOffset;
		}
	}

	// Determine final result
	switch (pLevelProperties->eLevelType) 
	{
	case EncounterLevelType_Specified:
			*piMin = pLevelProperties->iSpecifiedMin;
			*piMax = pLevelProperties->iSpecifiedMax > pLevelProperties->iSpecifiedMin ? pLevelProperties->iSpecifiedMax : pLevelProperties->iSpecifiedMin;
			return;
	case EncounterLevelType_MapLevel:
		{
#ifdef GAMESERVER
			int iBaseLevel = encounterTemplate_GetMapLevel(v3EncounterPos);
			*piMin = CLAMP(iBaseLevel + iMinOffset, iMinLevel, iMaxLevel);
			*piMax = CLAMP(iBaseLevel + iMaxOffset, iMinLevel, iMaxLevel);
#endif
			return;
		}
	case EncounterLevelType_PlayerLevel:
			if (pEnt && pEnt->pChar) {
				int avgLevel = encounter_getTeamLevelInRange(pEnt, NULL, true);
				*piMin = CLAMP(avgLevel + iMinOffset, iMinLevel, iMaxLevel);
				*piMax = CLAMP(avgLevel + iMaxOffset, iMinLevel, iMaxLevel);
			} else {
				Errorf("Spawning encounter \"%s\" at level 0 because level type is PlayerLevel and no player is available.  You don't want this.",pTemplate->pcName);
			}
			return;
	case EncounterLevelType_MapVariable:
		{
#ifdef GAMESERVER
			MapVariable *pVar = mapvariable_GetByName(iPartitionIdx, pLevelProperties->pcMapVariable);
			if (pVar && pVar->pVariable && pVar->pVariable->eType == WVAR_INT) {
				*piMin = CLAMP(pVar->pVariable->iIntVal + iMinOffset, iMinLevel, iMaxLevel);
				*piMax = CLAMP(pVar->pVariable->iIntVal + iMaxOffset, iMinLevel, iMaxLevel);
			}
#endif
			return;
		}
	}
}


static EncounterDifficulty encounterTemplate_GetMapDifficulty(void)
{
	EncounterDifficulty eDifficulty = 0;

#ifdef GAMESERVER
	if (siOverrideMapDifficulty) {
		eDifficulty = siOverrideMapDifficulty;
	} else {	
		eDifficulty = zmapInfoGetMapDifficulty(NULL);
	}	
#endif
	return eDifficulty;
}


// Get the encounter difficulty
EncounterDifficulty encounterTemplate_GetDifficulty(EncounterTemplate *pTemplate, int iPartitionIdx)
{
	EncounterDifficulty eDifficulty = 0;
	EncounterDifficultyProperties *pDifficultyProperties = NULL;

	if (!pTemplate) {
		return eDifficulty;
	}

	pDifficultyProperties = encounterTemplate_GetDifficultyProperties(pTemplate);

	// Default to map difficulty if no level properties
	if (!pDifficultyProperties) {
#ifdef GAMESERVER
		eDifficulty = encounterTemplate_GetMapDifficulty();
#endif
		return eDifficulty;
	}

	// Determine map difficulty
	switch (pDifficultyProperties->eDifficultyType) 
	{
		xcase EncounterDifficultyType_Specified:
			eDifficulty = pDifficultyProperties->eSpecifiedDifficulty;
		xcase EncounterLevelType_MapLevel:
		{
#ifdef GAMESERVER
			eDifficulty = encounterTemplate_GetMapDifficulty();
#endif
		}
		xcase EncounterDifficultyType_MapVariable:
		{
#ifdef GAMESERVER
			MapVariable *pVar = mapvariable_GetByName(iPartitionIdx, pDifficultyProperties->pcMapVariable);
			if (pVar && pVar->pVariable) {
				if (pVar->pVariable->eType == WVAR_INT) {
					eDifficulty = pVar->pVariable->iIntVal;
				} else if ( pVar->pVariable->eType == WVAR_STRING && pVar->pVariable->pcStringVal) {
					eDifficulty = StaticDefineIntGetInt(EncounterDifficultyEnum, pVar->pVariable->pcStringVal);
					if (eDifficulty < 0) {
						eDifficulty = 0;
					}
				}
			}
#endif
		}
	}
	return eDifficulty;
}


EncounterSharedCritterGroupSource encounterTemplate_GetCritterGroupSource(EncounterTemplate *pTemplate)
{
	if (pTemplate && pTemplate->pActorSharedProperties && pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_FromParent && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetCritterGroupSource(pParent);
	}
	if (pTemplate && pTemplate->pActorSharedProperties) {
		return pTemplate->pActorSharedProperties->eCritterGroupType;
	}
	return EncounterSharedCritterGroupSource_Specified;		// Default (0)
}


const char* encounterTemplate_GetCritterGroupSourceMapVarName(EncounterTemplate *pTemplate)
{
	if (pTemplate && pTemplate->pActorSharedProperties && pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_FromParent && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetCritterGroupSourceMapVarName(pParent);
	}
	if (pTemplate && pTemplate->pActorSharedProperties) {
		return pTemplate->pActorSharedProperties->pcCritterGroupMapVar;
	}
	return NULL;		// Default (0)
}


CritterGroup *encounterTemplate_GetCritterGroup(EncounterTemplate *pTemplate, int iPartitionIdx)
{
	if (pTemplate && (!pTemplate->pActorSharedProperties || pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_FromParent) && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetCritterGroup(pParent, iPartitionIdx);
	}
	if (pTemplate && pTemplate->pActorSharedProperties) {

		if (pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_Specified) {
			return GET_REF(pTemplate->pActorSharedProperties->hCritterGroup);

		} else if (pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_MapVariable) {
#ifdef GAMESERVER
			MapVariable *pVar = mapvariable_GetByName(iPartitionIdx, pTemplate->pActorSharedProperties->pcCritterGroupMapVar);
			if (pVar && pVar->pVariable && pVar->pVariable->eType == WVAR_CRITTER_GROUP) {
				return GET_REF(pVar->pVariable->hCritterGroup);
			}
#endif
		}
	}
	return NULL;
}


const char *encounterTemplate_GetCritterGroupMapVarName(EncounterTemplate *pTemplate)
{
	if (pTemplate && pTemplate->pActorSharedProperties && pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_FromParent && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetCritterGroupMapVarName(pParent);
	}
	if (pTemplate && pTemplate->pActorSharedProperties) {
		if (pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_MapVariable) {
			return pTemplate->pActorSharedProperties->pcCritterGroupMapVar;
		} 
	}
	return NULL;
}

EncounterActorNameProperties* encounterTemplate_GetActorNameProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideDisplayName && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorNameProperties(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	return pActor ? &(pActor->nameProps) : NULL;
}

EncounterActorCritterProperties* encounterTemplate_GetActorCritterProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorCritterProperties(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	return pActor ? &(pActor->critterProps) : NULL;
}

EncounterActorFactionProperties* encounterTemplate_GetActorFactionProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideFaction && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorFactionProperties(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	return pActor ? &(pActor->factionProps) : NULL;
}

EncounterActorSpawnInfoProperties* encounterTemplate_GetActorSpawnInfoProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideCritterSpawnInfo && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorSpawnInfoProperties(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	return pActor ? &(pActor->spawnInfoProps) : NULL;
}

EncounterActorMiscProperties* encounterTemplate_GetActorMiscProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideMisc && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorMiscProperties(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	return pActor ? &(pActor->miscProps) : NULL;
}

EncounterActorFSMProperties* encounterTemplate_GetActorFSMProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideFSMInfo && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorFSMProperties(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	return pActor ? &(pActor->fsmProps) : NULL;
}

CritterGroup *encounterTemplate_GetActorCritterGroup(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, ActorCritterType eType)
{
	if (!pActor) {
		return NULL;
	}

	//This is a tricky one. We should follow this actor's inheritance chain ONLY if we're not supposed to be 
	//pulling the crittergroup from the template. 
	//We need to have the type passed in because we can't rely on our own inheritance chain to give us the correct one.
	switch(eType) 
	{
	case ActorCritterType_CritterGroup:
		{
			if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
				EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
				if (pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_FromParent) {
					return encounterTemplate_GetActorCritterGroup(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iPartitionIdx, eType);
				}
			}
			return GET_REF(pActor->critterProps.hCritterGroup);
		}
	case ActorCritterType_MapVariableGroup:
		{
#ifdef GAMESERVER
			MapVariable *pVar = mapvariable_GetByName(iPartitionIdx, pActor->critterProps.pcCritterMapVariable);
			if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
				EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
				if (pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_FromParent) {
					return encounterTemplate_GetActorCritterGroup(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iPartitionIdx, eType);
				}
			}
			if (pVar && pVar->pVariable && pVar->pVariable->eType == WVAR_CRITTER_GROUP) {
				return GET_REF(pVar->pVariable->hCritterGroup);
			}
#endif
			return NULL;
		}
	case ActorCritterType_FromTemplate:
	case ActorCritterType_PetContactList:
		return encounterTemplate_GetCritterGroup(pTemplate, iPartitionIdx);

	case ActorCritterType_CritterDef:
	case ActorCritterType_MapVariableDef:
		return NULL;
	}
	return NULL;
}


const char *encounterTemplate_GetActorRank(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorRank(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	if
	(
		pActor &&
		(
			(pActor->critterProps.eCritterType == ActorCritterType_CritterGroup) || 
			(pActor->critterProps.eCritterType == ActorCritterType_MapVariableGroup) ||
			(pActor->critterProps.eCritterType == ActorCritterType_FromTemplate) ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinion ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionForLeader ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionNormal ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionTeam
		)
	)
	{
			return pActor->critterProps.pcRank;
	}
	return g_pcCritterDefaultRank;
}


const char *encounterTemplate_GetActorSubRank(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorSubRank(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	if
	(
		pActor &&
		(
			(pActor->critterProps.eCritterType == ActorCritterType_CritterGroup) || 
			(pActor->critterProps.eCritterType == ActorCritterType_MapVariableGroup) ||
			(pActor->critterProps.eCritterType == ActorCritterType_FromTemplate) ||
			(pActor->critterProps.eCritterType == ActorCritterType_CritterDef) ||
			(pActor->critterProps.eCritterType == ActorCritterType_MapVariableDef) ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinion ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionForLeader ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionNormal ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionTeam

		)
	)
	{
		return pActor->critterProps.pcSubRank;
	}
	return g_pcCritterDefaultSubRank;
}


ActorCritterType encounterTemplate_GetActorCritterType(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorCritterType(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	if (pActor) {
		return pActor->critterProps.eCritterType;
	}
	return -1;
}


const char* encounterTemplate_GetActorCritterTypeMapVarName(EncounterTemplate *pTemplate, EncounterActorProperties* pActor)
{
	if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorCritterTypeMapVarName(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	if (pActor) {
		return pActor->critterProps.pcCritterMapVariable;
	}
	return NULL;
}


CritterDef *encounterTemplate_GetActorCritterDef(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx)
{
	CritterDef *pDef = NULL;

	if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorCritterDef(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iPartitionIdx);
	}
	if (pActor && pActor->critterProps.eCritterType == ActorCritterType_CritterDef) {
		return GET_REF(pActor->critterProps.hCritterDef);

	} else if (pActor && pActor->critterProps.eCritterType == ActorCritterType_MapVariableDef) {
#ifdef GAMESERVER
		MapVariable *pVar = mapvariable_GetByName(iPartitionIdx, pActor->critterProps.pcCritterMapVariable);
		if (pVar && pVar->pVariable && pVar->pVariable->eType == WVAR_CRITTER_DEF) {
			return GET_REF(pVar->pVariable->hCritterDef);
		}
#endif
	} 

	return NULL;
}


bool encounterTemplate_IsActorCritterDefKnown(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_IsActorCritterDefKnown(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	if (pActor && pActor->critterProps.eCritterType == ActorCritterType_CritterDef) {
		return true;
	}
	return false;
}


CritterFaction* encounterTemplate_GetActorFactionHelper(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideFaction && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorFactionHelper(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	return pActor ? GET_REF(pActor->factionProps.hFaction) : NULL;
}


EncounterTemplateOverrideType encounterTemplate_GetActorFactionSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	CritterFaction *pFaction = NULL;

	if (!pActor) {
		return EncounterTemplateOverrideType_FromTemplate;
	}

	if (pActor && !pActor->bOverrideFaction && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorFactionSource(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	return pActor ? pActor->factionProps.eFactionType : 0;
}


CritterFaction *encounterTemplate_GetActorFaction(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx)
{
	CritterFaction *pFaction = NULL;
	EncounterTemplateOverrideType eSrc = 0;

	if (!pActor) {
		return NULL;
	}

	eSrc = encounterTemplate_GetActorFactionSource(pTemplate, pActor);

	if (eSrc == EncounterTemplateOverrideType_Specified) {
		pFaction = encounterTemplate_GetActorFactionHelper(pTemplate, pActor);
	} else if (eSrc == EncounterTemplateOverrideType_FromTemplate) {
		if (encounterTemplate_GetFactionSource(pTemplate) == EncounterCritterOverrideType_Specified) {
			pFaction = encounterTemplate_GetFaction(pTemplate);
		} else {
			CritterDef *pCritter = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
			if (pCritter) {
				pFaction = GET_REF(pCritter->hFaction);
			}
		}
	}

	return pFaction;
}


int encounterTemplate_GetActorLevelOffset(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorLevelOffset(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	return pActor ? pActor->critterProps.iLevelOffset : 0;
}


int encounterTemplate_GetActorLevel(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iEncounterLevel)
{
	if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorLevel(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iEncounterLevel);
	}
	if (pActor && pActor->critterProps.iLevelOffset) {		
		iEncounterLevel = CLAMP(iEncounterLevel + pActor->critterProps.iLevelOffset, 1, 60);
	}

	return iEncounterLevel;
}


int encounterTemplate_GetActorGangIDHelper(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideFaction && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorGangIDHelper(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	return pActor ? pActor->factionProps.iGangID : 0;
}


int encounterTemplate_GetActorGangID(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx)
{
	int iGangID = 0;
	EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);
	EncounterTemplateOverrideType eSrc = 0;

	if (!pActor) {
		return 0;
	}

	eSrc = encounterTemplate_GetActorFactionSource(pTemplate, pActor);

	if (eSrc == EncounterTemplateOverrideType_Specified) {
		iGangID = encounterTemplate_GetActorGangIDHelper(pTemplate, pActor);

	} else if (eSrc == EncounterTemplateOverrideType_FromTemplate) {
		if (encounterTemplate_GetFactionSource(pTemplate) == EncounterCritterOverrideType_Specified) {
			iGangID = encounterTemplate_GetGangID(pTemplate);
		} else {
			CritterDef *pCritter = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
			if (pCritter) {
				iGangID = pCritter->iGangID;
			}
		}
	}

	return iGangID;
}


bool encounterTemplate_IsActorFactionKnown(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (!pActor) {
		return false;
	}

	if (!pActor->bOverrideFaction && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_IsActorFactionKnown(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	if (pActor->factionProps.eFactionType == EncounterTemplateOverrideType_Specified) {
		return true;
	} else if (pActor->factionProps.eFactionType == EncounterTemplateOverrideType_FromTemplate) {
		if (encounterTemplate_GetFactionSource(pTemplate) == EncounterCritterOverrideType_Specified) {
			return true;
		}
		return encounterTemplate_IsActorCritterDefKnown(pTemplate, pActor);
	}

	return false;
}


bool encounterTemplate_GetActorFactionName(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, char **estrName)
{
	if (!pActor) {
		return false;
	}

	if (!pActor->bOverrideFaction && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorFactionName(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iPartitionIdx, estrName);
	}

	if (pActor->factionProps.eFactionType == EncounterTemplateOverrideType_Specified) {
		if (REF_STRING_FROM_HANDLE(pActor->factionProps.hFaction)) {
			estrPrintf(estrName, "%s", REF_STRING_FROM_HANDLE(pActor->factionProps.hFaction));
		} else {
			estrPrintf(estrName, "* None *");
		}
		return true;

	} else if (pActor->factionProps.eFactionType == EncounterTemplateOverrideType_FromTemplate) {
		if (encounterTemplate_GetFactionSource(pTemplate) == EncounterCritterOverrideType_Specified) {
			CritterFaction *pTemplateFaction = encounterTemplate_GetFaction(pTemplate);
			if (pTemplateFaction) {
				estrPrintf(estrName, "%s", pTemplateFaction->pchName);
			} else {
				estrPrintf(estrName, "* None *");
			}
			return true;
		} else {
			CritterDef *pCritter = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
			if (pCritter) {
				if (REF_STRING_FROM_HANDLE(pCritter->hFaction)) {
					estrPrintf(estrName, "%s", REF_STRING_FROM_HANDLE(pCritter->hFaction));
				} else {
					estrPrintf(estrName, "* None *");
				}
				return true;
			}
		}
	}

	return false;
}


AICombatRolesDef* encounterTemplate_GetCombatRolesDef(EncounterTemplate *pTemplate)
{
	EncounterAIProperties *pAIProperties = NULL;
	if (!pTemplate) {
		return NULL;
	}

	pAIProperties = encounterTemplate_GetAIProperties(pTemplate);

	if (pAIProperties) {
		return RefSystem_ReferentFromString("AICombatRolesDef", pAIProperties->pchCombatRolesDef);
	} else {
		return NULL;
	}
}


const char* encounterTemplate_GetActorCombatRole(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	// the actor's combat role can only be gotten through the actor
	if (pActor && !pActor->bOverrideMisc && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorCombatRole(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	if (pActor) {
		return pActor->miscProps.pcCombatRole;
	}
	return NULL;
}


bool encounterTemplate_GetActorIsCombatant(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideMisc && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorIsCombatant(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	if (pActor) {
		return !pActor->miscProps.bIsNonCombatant;
	}
	return true;
}


FSM *encounterTemplate_GetEncounterFSM(EncounterTemplate *pTemplate)
{
	EncounterAIProperties *pAIProperties = NULL;

	if (!pTemplate) {
		return NULL;
	}

	pAIProperties = encounterTemplate_GetAIProperties(pTemplate);

	if (pAIProperties && (pAIProperties->eFSMType == EncounterCritterOverrideType_Specified)) {
		return GET_REF(pAIProperties->hFSM);
	} else {
		return NULL;
	}
}


FSM* encounterTemplate_GetActorFSMHelper(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideFSMInfo && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorFSMHelper(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}
	return pActor ? GET_REF(pActor->fsmProps.hFSM) : NULL;
}


EncounterTemplateOverrideType encounterTemplate_GetActorFSMSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	CritterFaction *pFaction = NULL;

	if (!pActor) {
		return EncounterTemplateOverrideType_FromTemplate;
	}

	if (pActor && !pActor->bOverrideFSMInfo && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorFSMSource(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	return pActor->fsmProps.eFSMType;
}


FSM *encounterTemplate_GetActorFSM(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx)
{
	FSM *pFSM = NULL;
	EncounterTemplateOverrideType eSrc = 0;

	if (!pActor) {
		return NULL;
	}

	eSrc = encounterTemplate_GetActorFSMSource(pTemplate, pActor);

	if (pActor) {
		if (pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_Specified) {
			pFSM = encounterTemplate_GetActorFSMHelper(pTemplate, pActor);
		} else if (pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_FromTemplate) {
			EncounterAIProperties *pAIProperties = encounterTemplate_GetAIProperties(pTemplate); 
			if (pAIProperties) {
				if (pAIProperties->eFSMType == EncounterCritterOverrideType_Specified) {
					pFSM = GET_REF(pAIProperties->hFSM);
				} else if (pAIProperties->eFSMType == EncounterCritterOverrideType_FromCritter) {
					CritterDef *pCritter = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
					if (pCritter) {
						pFSM = GET_REF(pCritter->hFSM);
					}
				}
			}
		}
	}

	return pFSM;
}


bool encounterTemplate_IsActorFSMKnown(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (!pActor) {
		return false;
	}

	if (pActor && !pActor->bOverrideFSMInfo && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_IsActorFSMKnown(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	if (pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_Specified) {
		return true;
	} else if (pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_FromTemplate) {
		EncounterAIProperties *pAIProperties = encounterTemplate_GetAIProperties(pTemplate);
		if (pAIProperties && pAIProperties->eFSMType == EncounterCritterOverrideType_Specified) {
			return true;
		}
		return encounterTemplate_IsActorCritterDefKnown(pTemplate, pActor);
	}
	return false;
}


bool encounterTemplate_GetActorFSMName(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, char **estrName)
{
	if (!pActor) {
		return false;
	}

	if (pActor && !pActor->bOverrideFSMInfo && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorFSMName(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iPartitionIdx, estrName);
	}

	if (pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_Specified) {
		if (REF_STRING_FROM_HANDLE(pActor->fsmProps.hFSM)) {
			estrPrintf(estrName, "%s", REF_STRING_FROM_HANDLE(pActor->fsmProps.hFSM));
		} else {
			estrPrintf(estrName, "* None *");
		}
		return true;

	} else if (pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_FromTemplate) {
		EncounterAIProperties *pAIProperties = encounterTemplate_GetAIProperties(pTemplate);
		if (pAIProperties && pAIProperties->eFSMType == EncounterCritterOverrideType_Specified) {
			if (REF_STRING_FROM_HANDLE(pAIProperties->hFSM)) {
				estrPrintf(estrName, "%s", REF_STRING_FROM_HANDLE(pAIProperties->hFSM));
			} else {
				estrPrintf(estrName, "* None *");
			}
			return true;
		} else {
			CritterDef *pCritter = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
			if (pCritter) {
				if (REF_STRING_FROM_HANDLE(pCritter->hFSM)) {
					estrPrintf(estrName, "%s", REF_STRING_FROM_HANDLE(pCritter->hFSM));
				} else {
					estrPrintf(estrName, "* None *");
				}
				return true;
			}
		}
	}

	return false;
}


void encounterTemplate_AddVarIfNotPresent(WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars, WorldVariable *pVar)
{
	int i;

	if (!peaVars || !pVar) {
		return;
	}

	if (peaVarDefs) {
		for(i=eaSize(peaVarDefs)-1; i>=0; --i) {
			if (pVar->pcName && (*peaVarDefs)[i]->pcName && (stricmp(pVar->pcName, (*peaVarDefs)[i]->pcName) == 0)) {
				return; // Var already in list
			}
		}
	}
	if (peaVars) {
		for(i=eaSize(peaVars)-1; i>=0; --i) {
			if (pVar->pcName && (*peaVars)[i]->pcName && (stricmp(pVar->pcName, (*peaVars)[i]->pcName) == 0)) {
				return; // Var already in list
			}
		}
	}
	// Var not in list, so add it
	eaPush(peaVars, pVar);
}


void encounterTemplate_AddVarDefIfNotPresent(WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars, WorldVariableDef *pVarDef)
{
	int i;

	if (!peaVarDefs || !pVarDef) {
		return;
	}

	if (peaVarDefs) {
		for(i=eaSize(peaVarDefs)-1; i>=0; --i) {
			if (pVarDef->pcName && (*peaVarDefs)[i]->pcName && (stricmp(pVarDef->pcName, (*peaVarDefs)[i]->pcName) == 0)) {
				return; // Var already in list
			}
		}
	}
	if (peaVars) {
		for(i=eaSize(peaVars)-1; i>=0; --i) {
			if (pVarDef->pcName && (*peaVars)[i]->pcName && (stricmp(pVarDef->pcName, (*peaVars)[i]->pcName) == 0)) {
				return; // Var already in list
			}
		}
	}
	// Var not in either list, so add it to both
	eaPush(peaVarDefs, pVarDef);
}


// Gets the FSM var defs following override rules
void encounterTemplate_GetEncounterFSMVarDefs(EncounterTemplate *pTemplate, WorldVariableDef*** peaVarDefs, WorldVariable ***peaVars)
{
	EncounterAIProperties *pAIProperties = NULL;
	EncounterTemplate* pParentTemplate = NULL;
	if (!pTemplate) {
		return;
	}

	pAIProperties = pTemplate->pSharedAIProperties;
	pParentTemplate = SAFE_GET_REF(pTemplate, hParent);

	if (pParentTemplate) {
		encounterTemplate_GetEncounterFSMVarDefs(pParentTemplate, peaVarDefs, peaVars);
	}

	if (pAIProperties) {
		int i;
		for(i=0; i<eaSize(&pAIProperties->eaVariableDefs); ++i) {
			encounterTemplate_AddVarDefIfNotPresent(peaVarDefs, peaVars,pAIProperties->eaVariableDefs[i]);
		}
	}
}


// Get the FSM vars from the critter group (if a group is known)
void encounterTemplate_GetEncounterGroupFSMVars(EncounterTemplate *pTemplate, int iPartitionIdx, WorldVariableDef*** peaVarDefs, WorldVariable ***peaVars)
{
	CritterGroup *pGroup = encounterTemplate_GetCritterGroup(pTemplate, iPartitionIdx);
	if (pGroup) {
		int i;
		for(i=0; i<eaSize(&pGroup->ppCritterVars); ++i) {
			encounterTemplate_AddVarIfNotPresent(peaVarDefs, peaVars, &pGroup->ppCritterVars[i]->var);
		}
	}
}


// Get the FSM var defs from the actor
void encounterTemplate_GetActorFSMVarDefs(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars)
{
 	if (pActor && pTemplate && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		encounterTemplate_GetActorFSMVarDefs(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), peaVarDefs, peaVars);
	}
	if (pActor) {
		int i;
		for(i=0; i<eaSize(&pActor->eaVariableDefs); ++i) {
			encounterTemplate_AddVarDefIfNotPresent(peaVarDefs, peaVars, pActor->eaVariableDefs[i]);
		}
	} 
}


// Get the FSM vars from the actor's critter (if known)
void encounterTemplate_GetActorCritterFSMVars(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars)
{
	if (pActor && !pActor->bOverrideCritterType && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		encounterTemplate_GetActorCritterFSMVars(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iPartitionIdx, peaVarDefs, peaVars);
		return;
	}
	if (pActor) {	
		CritterDef *pCritterDef = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
		CritterGroup *pCritterGroup;
		int i;

		if (pCritterDef) {
			// If have a critter def, add its variables
			for(i=0; i<eaSize(&pCritterDef->ppCritterVars); ++i) {
				encounterTemplate_AddVarIfNotPresent(peaVarDefs, peaVars, &pCritterDef->ppCritterVars[i]->var);
			}

			// Then add it's critter group's variables
			pCritterGroup = GET_REF(pCritterDef->hGroup);
		} else {
			// If don't know the critter def, then try the critter group
			pCritterGroup = encounterTemplate_GetActorCritterGroup(pTemplate, pActor, iPartitionIdx, encounterTemplate_GetActorCritterType(pTemplate, pActor));
		}
		if (pCritterGroup) {
			for(i=0; i<eaSize(&pCritterGroup->ppCritterVars); ++i) {
				encounterTemplate_AddVarIfNotPresent(peaVarDefs, peaVars, &pCritterGroup->ppCritterVars[i]->var);
			}
		}
	}
}

// Get the FSM var defs from the actor
void encounter_GetWorldActorFSMVarDefs(WorldActorProperties *pActor, WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars)
{
	if (pActor) {
		int i;
		for(i=0; i<eaSize(&pActor->eaFSMVariableDefs); ++i) {
			if (pActor->eaFSMVariableDefs[i] && pActor->eaFSMVariableDefs[i]->pcName) {
				encounterTemplate_AddVarDefIfNotPresent(peaVarDefs, peaVars, pActor->eaFSMVariableDefs[i]);
			}
		}
	} 
}


const char *encounterTemplate_GetActorSpawnAnim(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, F32* pfAnimTime)
{
	const char *pcSpawnAnim = NULL;

	if (!pActor) {
		return NULL;
	}

	if (pActor && !pActor->bOverrideCritterSpawnInfo && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorSpawnAnim(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iPartitionIdx, pfAnimTime);
	}

	// Specified, set the anim
	if (pActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_Specified) {
		pcSpawnAnim = pActor->spawnInfoProps.pcSpawnAnim;
		if ( pfAnimTime ) {
			(*pfAnimTime) = pActor->spawnInfoProps.fSpawnLockdownTime;
		}
	} else if (pActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_FromTemplate) {
		EncounterSpawnProperties *pSpawnProperties = encounterTemplate_GetSpawnProperties(pTemplate);
		if (pSpawnProperties) {
			// From template, specified
			if (pSpawnProperties->eSpawnAnimType == EncounterSpawnAnimType_Specified) {
				pcSpawnAnim = pSpawnProperties->pcSpawnAnim;
				if ( pfAnimTime ) {
					(*pfAnimTime) = pSpawnProperties->fSpawnLockdownTime;
				}
			} else if (pSpawnProperties->eSpawnAnimType == EncounterSpawnAnimType_FromCritter) {
				// From Template, from critter
				CritterDef *pCritter = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
				if (pCritter) {
					pcSpawnAnim = pCritter->pchSpawnAnim;
					if ( pfAnimTime ) {
						(*pfAnimTime) = pCritter->fSpawnLockdownTime;
					}

					// Anim not found on critter def, try critter group
					if (!EMPTY_TO_NULL(pcSpawnAnim)) {
						CritterGroup* pCritterGroup = GET_REF(pCritter->hGroup);
						if(pCritterGroup) {
							pcSpawnAnim = pCritterGroup->pchSpawnAnim;
							if ( pfAnimTime ) {
								(*pfAnimTime) = pCritterGroup->fSpawnLockdownTime;
							}
						}
					}
				}
			} else if (pSpawnProperties->eSpawnAnimType == EncounterSpawnAnimType_FromCritterAlternate) {
				// From Template, from critter (alternate anim)
				CritterDef *pCritter = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
				if (pCritter) {
					pcSpawnAnim = pCritter->pchSpawnAnimAlternate;
					if ( pfAnimTime ) {
						(*pfAnimTime) = pCritter->fSpawnLockdownTimeAlternate;
					}

					// Anim not found on critter def, try critter group (alternate anim)
					if (!EMPTY_TO_NULL(pcSpawnAnim)) {
						CritterGroup* pCritterGroup = GET_REF(pCritter->hGroup);
						if (pCritterGroup) {
							pcSpawnAnim = pCritterGroup->pchSpawnAnimAlternate;
							if ( pfAnimTime ) {
								(*pfAnimTime) = pCritterGroup->fSpawnLockdownTimeAlternate;
							}
						}
					}
				}
			}
		}
	}
	return pcSpawnAnim;
}


bool encounterTemplate_IsActorSpawnAnimKnown(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (!pActor) {
		return false;
	}

	if (pActor && !pActor->bOverrideCritterSpawnInfo && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_IsActorSpawnAnimKnown(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	if (pActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_Specified) {
		return true;

	} else if (pActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_FromTemplate) {
		EncounterSpawnProperties* pSpawnProperties = encounterTemplate_GetSpawnProperties(pTemplate);
		if (pSpawnProperties && pSpawnProperties->eSpawnAnimType == EncounterSpawnAnimType_Specified) {
			return true;
		}
		return encounterTemplate_IsActorCritterDefKnown(pTemplate, pActor);
	}

	return false;
}

EncounterTemplateOverrideType encounterTemplate_GetActorSpawnAnimSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (!pActor) {
		return EncounterTemplateOverrideType_FromTemplate;
	}

	if (pActor && !pActor->bOverrideCritterSpawnInfo && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorSpawnAnimSource(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	return pActor->spawnInfoProps.eSpawnAnimType;
}

Message *encounterTemplate_GetActorCritterGroupDisplayMessageInternal(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, bool bOnlyUseOverride)
{
	Message *pMsg = NULL;

	if (pActor && !pActor->bOverrideCritterGroupDisplayName && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorCritterGroupDisplayMessage(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iPartitionIdx);
	}

	if (pActor) {
		pMsg = GET_REF(pActor->nameProps.critterGroupDisplayNameMsg.hMessage);
		if (!pMsg) {
			pMsg = pActor->nameProps.critterGroupDisplayNameMsg.pEditorCopy;
		}
		if (!pMsg && !bOnlyUseOverride) {
			CritterDef *pCritterDef = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
			if (pCritterDef) {
				CritterGroup *pCritterGroup = GET_REF(pCritterDef->hGroup);
				if(pCritterGroup) {
					return GET_REF(pCritterGroup->displayNameMsg.hMessage);
				}
			}
		}
	}
	return pMsg;
}

Message *encounterTemplate_GetActorDisplayMessageInternal(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, bool bOnlyUseOverride)
{
	Message *pMsg = NULL;

	if (pActor && !pActor->bOverrideDisplayName && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorDisplayMessage(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iPartitionIdx);
	}

	if (pActor) {
		pMsg = GET_REF(pActor->nameProps.displayNameMsg.hMessage);
		if (!pMsg) {
			pMsg = pActor->nameProps.displayNameMsg.pEditorCopy;
		}
		if (!pMsg && !bOnlyUseOverride) {
			CritterDef *pCritterDef = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
			if (pCritterDef) {
				return GET_REF(pCritterDef->displayNameMsg.hMessage);
			}
		}
	}
	return pMsg;
}

Message *encounterTemplate_GetActorDisplaySubNameMessageInternal(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, bool bOnlyUseOverride)
{
	Message *pMsg = NULL;

	if (pActor && !pActor->bOverrideDisplaySubName && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorDisplaySubNameMessage(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iPartitionIdx);
	}

	if (pActor) {
		pMsg = GET_REF(pActor->nameProps.displaySubNameMsg.hMessage);
		if (!pMsg) {
			pMsg = pActor->nameProps.displaySubNameMsg.pEditorCopy;
		}
		if (!pMsg && !bOnlyUseOverride) {
			CritterDef *pCritterDef = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
			if (pCritterDef) {
				return GET_REF(pCritterDef->displaySubNameMsg.hMessage);
			}
		}
	}
	return pMsg;
}

EncounterCritterOverrideType encounterTemplate_GetActorCritterGroupDisplayMessageSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideCritterGroupDisplayName && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorCritterGroupDisplayMessageSource(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	return pActor ? pActor->nameProps.eCritterGroupDisplayNameType : EncounterCritterOverrideType_FromCritter;
}

EncounterCritterOverrideType encounterTemplate_GetActorDisplayMessageSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideDisplayName && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorDisplayMessageSource(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	return pActor ? pActor->nameProps.eDisplayNameType : EncounterCritterOverrideType_FromCritter;
}

EncounterCritterOverrideType encounterTemplate_GetActorDisplaySubNameMessageSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (pActor && !pActor->bOverrideDisplaySubName && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorDisplaySubNameMessageSource(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	return pActor ? pActor->nameProps.eDisplaySubNameType : EncounterCritterOverrideType_FromCritter;
}

Message *encounter_GetActorCritterGroupDisplayMessageInternal(int iPartitionIdx, WorldEncounterProperties* pProps, WorldActorProperties *pWorldActor, bool bOnlyUseOverride)
{
	Message *pMsg = NULL;
	if (pProps) {
		// Check world actor
		if (pWorldActor) {
			pMsg = pWorldActor->critterGroupDisplayNameMsg.pEditorCopy;
			if (!pMsg) {
				pMsg = GET_REF(pWorldActor->critterGroupDisplayNameMsg.hMessage);
			}
		}

		// Check template
		if (!pMsg && pProps && pWorldActor) {
			EncounterTemplate* pTemplate = GET_REF(pProps->hTemplate);
			EncounterActorProperties* pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pProps, pWorldActor);
			CritterDef *pDef = NULL;

			if(pWorldActor->pCritterProperties) {
				pDef = GET_REF(pWorldActor->pCritterProperties->hCritterDef);
			}

			if(pDef && encounterTemplate_GetActorCritterGroupDisplayMessageSource(pTemplate,pActor) == EncounterCritterOverrideType_FromCritter) {
				CritterGroup *pCritterGroup = GET_REF(pDef->hGroup);
				if(pCritterGroup)
					pMsg = GET_REF(pCritterGroup->displayNameMsg.hMessage);
			} else {
				pMsg = encounterTemplate_GetActorCritterGroupDisplayMessageInternal(pTemplate, pActor, iPartitionIdx, bOnlyUseOverride);
			}
		}
	}

	return pMsg;
}

Message *encounter_GetActorDisplayMessageInternal(int iPartitionIdx, WorldEncounterProperties* pProps, WorldActorProperties *pWorldActor, bool bOnlyUseOverride)
{
	Message *pMsg = NULL;
	if (pProps) {
		// Check world actor
		if (pWorldActor) {
			pMsg = pWorldActor->displayNameMsg.pEditorCopy;
			if (!pMsg) {
				pMsg = GET_REF(pWorldActor->displayNameMsg.hMessage);
			}
		}

		// Check template
		if (!pMsg && pProps && pWorldActor) {
			EncounterTemplate* pTemplate = GET_REF(pProps->hTemplate);
			EncounterActorProperties* pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pProps, pWorldActor);
			CritterDef *pDef = NULL;

			if(pWorldActor->pCritterProperties) {
				pDef = GET_REF(pWorldActor->pCritterProperties->hCritterDef);
			}

			if(pDef && encounterTemplate_GetActorDisplayMessageSource(pTemplate,pActor) == EncounterCritterOverrideType_FromCritter) {
				pMsg = GET_REF(pDef->displayNameMsg.hMessage);
			} else {
				pMsg = encounterTemplate_GetActorDisplayMessageInternal(pTemplate, pActor, iPartitionIdx, bOnlyUseOverride);
			}
		}
	}

	return pMsg;
}

Message *encounter_GetActorDisplaySubNameMessageInternal(int iPartitionIdx, WorldEncounterProperties* pProps, WorldActorProperties *pWorldActor, bool bOnlyUseOverride)
{
	Message *pMsg = NULL;
	if (pProps) {
		// Check world actor
		if (pWorldActor) {
			pMsg = pWorldActor->displaySubNameMsg.pEditorCopy;
			if (!pMsg) {
				pMsg = GET_REF(pWorldActor->displaySubNameMsg.hMessage);
			}
		}

		// Check template
		if (!pMsg && pProps && pWorldActor) {
			EncounterTemplate* pTemplate = GET_REF(pProps->hTemplate);
			EncounterActorProperties* pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pProps, pWorldActor);
			CritterDef *pDef = NULL;

			if(pWorldActor->pCritterProperties) {
				pDef = GET_REF(pWorldActor->pCritterProperties->hCritterDef);
			}

			if(pDef && encounterTemplate_GetActorDisplaySubNameMessageSource(pTemplate,pActor) == EncounterCritterOverrideType_FromCritter) {
				pMsg = GET_REF(pDef->displaySubNameMsg.hMessage);
			} else {
				pMsg = encounterTemplate_GetActorDisplaySubNameMessageInternal(pTemplate, pActor, iPartitionIdx, bOnlyUseOverride);
			}
		}
	}

	return pMsg;
}

bool encounterTemplate_GetActorEnabled(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iTeamSize, EncounterDifficulty eDifficulty)
{
	EncounterActorSpawnProperties *pProps = NULL;

	if (!pActor || (!pActor->eaSpawnProperties && !IS_HANDLE_ACTIVE(pTemplate->hParent))) {
		return false;
	}

	if (!pActor->bOverrideSpawnConditions && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorEnabled(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iTeamSize, eDifficulty);
	}

	pProps = eaIndexedGetUsingInt(&pActor->eaSpawnProperties, eDifficulty);

	if (pProps) {
		switch(iTeamSize) {
			case 1: return ((pProps->eSpawnAtTeamSize & TeamSizeFlags_1) != 0);
			case 2: return ((pProps->eSpawnAtTeamSize & TeamSizeFlags_2) != 0);
			case 3: return ((pProps->eSpawnAtTeamSize & TeamSizeFlags_3) != 0);
			case 4: return ((pProps->eSpawnAtTeamSize & TeamSizeFlags_4) != 0);
			case 5: return ((pProps->eSpawnAtTeamSize & TeamSizeFlags_5) != 0);
		}
	}

	// Actors will spawn by default if spawn properties are not defined
	return true;
}


EncounterActorSpawnProperties** encounterTemplate_GetActorSpawnProps(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{

	if (!pActor || (!pActor->eaSpawnProperties && !IS_HANDLE_ACTIVE(pTemplate->hParent))) {
		return false;
	}

	if (!pActor->bOverrideSpawnConditions && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorSpawnProps(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	return pActor->eaSpawnProperties;
}


bool encounterTemplate_GetActorBossBar(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iTeamSize, EncounterDifficulty eDifficulty)
{
	EncounterActorSpawnProperties *pProps = NULL;

	if (!pActor || (!pActor->eaBossSpawnProperties && !IS_HANDLE_ACTIVE(pTemplate->hParent))) {
		return false;
	}

	if (pActor && !pActor->bOverrideSpawnConditions && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorBossBar(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName), iTeamSize, eDifficulty);
	}

	pProps = eaIndexedGetUsingInt(&pActor->eaBossSpawnProperties, eDifficulty);

	if (pProps) {
		switch(iTeamSize) {
			case 1: return ((pProps->eSpawnAtTeamSize & TeamSizeFlags_1) != 0);
			case 2: return ((pProps->eSpawnAtTeamSize & TeamSizeFlags_2) != 0);
			case 3: return ((pProps->eSpawnAtTeamSize & TeamSizeFlags_3) != 0);
			case 4: return ((pProps->eSpawnAtTeamSize & TeamSizeFlags_4) != 0);
			case 5: return ((pProps->eSpawnAtTeamSize & TeamSizeFlags_5) != 0);
		}
	}
	return false;
}


EncounterActorSpawnProperties** encounterTemplate_GetActorBossProps(EncounterTemplate *pTemplate, EncounterActorProperties *pActor)
{
	if (!pActor || (!pActor->eaBossSpawnProperties && !IS_HANDLE_ACTIVE(pTemplate->hParent))) {
		return false;
	}

	if (!pActor->bOverrideSpawnConditions && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		return encounterTemplate_GetActorBossProps(pParent, encounterTemplate_GetActorByName(pParent, pActor->pcName));
	}

	return pActor->eaBossSpawnProperties;
}


F32 encounterTemplate_GetActorValue(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, int iTeamSize, EncounterDifficulty eDifficulty)
{
	if (!pTemplate || !pActor) {
		return -1.0;
	}

	if (encounterTemplate_GetActorEnabled(pTemplate, pActor, iTeamSize, eDifficulty)) {
		CritterDef *pCritterDef = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
		const char *pcRank;
		const char *pcSubRank;

		if (pCritterDef) {
			pcRank = pCritterDef->pcRank;
		} else {
			pcRank = encounterTemplate_GetActorRank(pTemplate, pActor);
		}
		pcSubRank = encounterTemplate_GetActorSubRank(pTemplate, pActor);

		return critterRankGetDifficultyValue(pcRank, pcSubRank, pActor->critterProps.iLevelOffset);
	}

	return 0.0;  // Actor doesn't spawn at this team size
}


F32 encounterTemplate_GetEncounterValue(EncounterTemplate *pTemplate, int iPartitionIdx, int iTeamSize, EncounterDifficulty eDifficulty)
{
	F32 fResult = 0.0f;
	int i;
	EncounterActorProperties **eaActors = NULL;
	EncounterTemplate* pParentTemplate = SAFE_GET_REF(pTemplate, hParent);

	if (pParentTemplate) {
		return encounterTemplate_GetEncounterValue(pParentTemplate, iPartitionIdx, iTeamSize, eDifficulty);
	}

	if (!pTemplate) {
		return -1.0;
	}

	encounterTemplate_FillActorEarray(pTemplate, &eaActors);

	if (eaActors) {
		for(i=eaSize(&eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = eaActors[i];
			F32 fValue = encounterTemplate_GetActorValue(pTemplate, pActor, iPartitionIdx, iTeamSize, eDifficulty);
			if (fValue < 0) {
				fValue = -1.0;
			}
			fResult += fValue;
		}
		eaDestroy(&eaActors);
	}

	return fResult;
}


// Returns the number of enabled actors at a specified team size
int encounterTemplate_GetNumActorsAtTeamSize(EncounterTemplate *pTemplate, int iTeamSize, EncounterDifficulty eDifficulty)
{
	int iResult = 0;
	int i;
	EncounterActorProperties **eaActors = NULL;

	if (!pTemplate) {
		return 0;
	}

	encounterTemplate_FillActorEarray(pTemplate, &eaActors);

	if (eaActors) {
		for(i=eaSize(&eaActors)-1; i>=0; --i) {
			if (encounterTemplate_GetActorEnabled(pTemplate, eaActors[i], iTeamSize, eDifficulty)) {
				iResult++;
			}
		}
		eaDestroy(&eaActors);
	}

	return iResult;
}


// Returns the maximum number of enabled actors at any team size
int encounterTemplate_GetMaxNumActors(EncounterTemplate *pTemplate)
{
	int iResult = 0;
	int i,j;
	int iMaxDifficulty = MAX(encounter_GetEncounterDifficultiesCount(), 1);
	EncounterActorProperties **eaActors = NULL;

	if (!pTemplate || iMaxDifficulty < 0) {
		return 0;
	}

	for(i = 1; i <= TEAM_MAX_SIZE; i++) {
		for(j = 0; j < iMaxDifficulty; j++)	{
			iResult = MAX(iResult, encounterTemplate_GetNumActorsAtTeamSize(pTemplate, i, j));
		}
	}

	return iResult;
}


bool encounterTemplate_TemplateExistsInInheritanceChain(EncounterTemplate* pTemplate, EncounterTemplate* pTemplateToFind)
{
	if (pTemplateToFind->pcName == pTemplate->pcName) {
		return true;
	}

	if (pTemplate && IS_HANDLE_ACTIVE(pTemplate->hParent)) {
		EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
		if (!pParent)
		{
			if (IsServer())
				ErrorFilenamef(pTemplate->pcFilename, "Encounter template %s references a parent template that doesn't exist!", pTemplate->pcName);
			return false;
		}
		if (pParent->pcName == pTemplateToFind->pcName) {
			return true;
		} else {
			return encounterTemplate_TemplateExistsInInheritanceChain(pParent, pTemplateToFind);
		}
	}
	return false;
}


U32 encounterTemplate_GetMajorityGangID(EncounterTemplate *pTemplate, int iPartitionIdx)
{
	static U32 *s_eauGangIDs = NULL;
	static int *s_eaiCounts = NULL;
	U32 uMajorityGangID = 0;
	int iMajorityCount = 0;
	int i;
	EncounterActorProperties **eaActors = NULL;
	encounterTemplate_FillActorEarray(pTemplate, &eaActors);

	ea32ClearFast(&s_eauGangIDs);
	eaiClearFast(&s_eaiCounts);

	if (pTemplate && eaActors) {
		for (i = eaSize(&eaActors)-1; i>=0; --i) {
			U32 uActorGangID = encounterTemplate_GetActorGangID(pTemplate, eaActors[i], iPartitionIdx);
			if (uActorGangID) {
				int pos = ea32Find(&s_eauGangIDs, uActorGangID);
				if (pos >= 0) {
					++s_eaiCounts[pos];
				} else {
					ea32Push(&s_eauGangIDs, uActorGangID);
					eaiPush(&s_eaiCounts, 1);
				}
			}
		}
		eaDestroy(&eaActors);
	}

	for (i = ea32Size(&s_eauGangIDs)-1; i>=0; --i) {
		if (s_eaiCounts[i] > iMajorityCount) {
			iMajorityCount = s_eaiCounts[i];
			uMajorityGangID = s_eauGangIDs[i];
		}
	}

	return uMajorityGangID;
}


CritterFaction* encounterTemplate_GetMajorityFaction(EncounterTemplate *pTemplate, int iPartitionIdx)
{
	static CritterFaction** s_eaFactions = NULL;
	static int *s_eaiCounts = NULL;
	CritterFaction *pMajorityFaction = NULL;
	int iMajorityCount = 0;
	int i;
	EncounterActorProperties **eaActors = NULL;
	encounterTemplate_FillActorEarray(pTemplate, &eaActors);

	eaClearFast(&s_eaFactions);
	eaiClearFast(&s_eaiCounts);

	if (pTemplate && eaActors) {
		for (i = eaSize(&eaActors)-1; i>=0; --i) {
			CritterFaction* pActorFaction = encounterTemplate_GetActorFaction(pTemplate, eaActors[i], iPartitionIdx);

			if (pActorFaction) {
				int pos = eaFind(&s_eaFactions, pActorFaction);
				if (pos >= 0) {
					++s_eaiCounts[pos];
				} else {
					eaPush(&s_eaFactions, pActorFaction);
					eaiPush(&s_eaiCounts, 1);
				}
			}
		}
		eaDestroy(&eaActors);
	}

	for (i = eaSize(&s_eaFactions)-1; i>=0; --i) {
		if (s_eaiCounts[i] > iMajorityCount) {
			iMajorityCount = s_eaiCounts[i];
			pMajorityFaction = s_eaFactions[i];
		}
	}

	return pMajorityFaction;
}


// --------------------------------------------------------------------------
// Fixup and Optimization Functions
// --------------------------------------------------------------------------


static void encounterTemplate_FixupVariableMessage(WorldVariable *pVar, char *pcBase, char *pcScope, int iIndex)
{
	if (pVar->eType == WVAR_MESSAGE) {
		char buf1[1024];
		DisplayMessage *pDispMsg = &pVar->messageVal;
		sprintf(buf1, "%s.var_%d", pcBase, iIndex);
		langFixupMessage(pDispMsg->pEditorCopy, buf1, "This is a variable for an EncounterTemplate.", pcScope);
	}
}


void encounterTemplate_FixupMessages(EncounterTemplate *pTemplate)
{
	int i,j;
	char buf[1024], scope[1024];

	if (pTemplate->pcScope) {
		sprintf(scope, "EncounterTemplate/%s/%s", pTemplate->pcScope, pTemplate->pcName);
	} else {
		sprintf(scope, "EncounterTemplate/%s", pTemplate->pcName);
	}

	if (pTemplate->pSharedAIProperties) {
		// Fix up variables
		sprintf(buf, "EncounterTemplate.%s.shared", pTemplate->pcName);
		for(i=eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)-1; i>=0; --i) {
			if (pTemplate->pSharedAIProperties->eaVariableDefs[i]->eDefaultType == WVARDEF_SPECIFY_DEFAULT && pTemplate->pSharedAIProperties->eaVariableDefs[i]->eType == WVAR_MESSAGE) {
				encounterTemplate_FixupVariableMessage(pTemplate->pSharedAIProperties->eaVariableDefs[i]->pSpecificValue, buf, scope, i);
			}
		}
	}

	for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
		EncounterActorProperties *pActor = pTemplate->eaActors[i];

		sprintf(buf, "EncounterTemplate.%s.actor_%d.name", pTemplate->pcName, i);
		langFixupMessage(pActor->nameProps.displayNameMsg.pEditorCopy, buf, "This is an actor name for an EncounterTemplate.", scope);

		sprintf(buf, "EncounterTemplate.%s.actor_%d.subname", pTemplate->pcName, i);
		langFixupMessage(pActor->nameProps.displaySubNameMsg.pEditorCopy, buf, "This is an actor sub name for an EncounterTemplate.", scope);

		// Fix up variables
		sprintf(buf, "EncounterTemplate.%s.actor_%d", pTemplate->pcName, i);
		for(j=eaSize(&pActor->eaVariableDefs)-1; j>=0; --j) {
			if (pActor->eaVariableDefs[j]->eDefaultType == WVARDEF_SPECIFY_DEFAULT && pActor->eaVariableDefs[j]->eType == WVAR_MESSAGE) {
				encounterTemplate_FixupVariableMessage(pActor->eaVariableDefs[j]->pSpecificValue, buf, scope, j);
			}
		}

		// Fix up interaction
		if (pActor->pInteractionProperties) {
			sprintf(buf, "EncounterTemplate.%s.actor_%d", pTemplate->pcName, i);
			for(j=eaSize(&pActor->pInteractionProperties->eaEntries)-1; j>=0; --j) {
				interaction_FixupMessages(pActor->pInteractionProperties->eaEntries[j], scope, buf, "actor", j);
			}
		}
	}
}


void encounterTemplate_Clean(EncounterTemplate *pTemplate)
{
	int i;

	// Clean Expressions

	// Jobs
	for(i=eaSize(&pTemplate->eaJobs)-1; i>=0; --i) {
		exprClean(pTemplate->eaJobs[i]->jobRating);
		exprClean(pTemplate->eaJobs[i]->jobRequires);
	}

	// Shared AI
	if (pTemplate->pSharedAIProperties) {
		for(i=eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)-1; i>=0; --i) {
			worldVariableDefCleanExpressions(pTemplate->pSharedAIProperties->eaVariableDefs[i]);
		}
	}

	// Actors
	for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
		EncounterActorProperties *pActor = pTemplate->eaActors[i];
		int j;

		// FSM Vars
		for(j=eaSize(&pActor->eaVariableDefs)-1; j>=0; --j) {
			worldVariableDefCleanExpressions(pActor->eaVariableDefs[j]);
		}

		if (pActor->pInteractionProperties) {
			// Interactions
			for(j=eaSize(&pActor->pInteractionProperties->eaEntries)-1; j>=0; --j) {
				interaction_CleanProperties(pActor->pInteractionProperties->eaEntries[j]);
			}
		}
	}
	if(pTemplate->pWaveProperties) {
		exprClean(pTemplate->pWaveProperties->pWaveCond);
	}
}


static void worldEncounter_FixupVariableMessage(WorldVariable *pVar, const char *pcFilename, const char *pcGroupName, const char *pcBase, const char *pcScope, int iIndex)
{
	if (pVar->eType == WVAR_MESSAGE) {
		char buf1[1024];
		DisplayMessage *pDispMsg = &pVar->messageVal;
		sprintf(buf1, "%s.var_%d", pcBase, iIndex);
		langFixupMessage(pDispMsg->pEditorCopy, groupDefMessageKeyRaw(pcFilename, pcGroupName, buf1, 0, false), "This is a variable for an encounter.", pcScope);
	}
}


void worldEncounter_FixupMessages(WorldEncounterProperties *pWorldEncounter, const char *pcFilename, const char *pcGroupName, const char *pcScope)
{
	int i,j;
	char buf[1024];

	for(i=eaSize(&pWorldEncounter->eaActors)-1; i>=0; --i) {
		WorldActorProperties *pActor = pWorldEncounter->eaActors[i];

		// Fix up variables
		sprintf(buf, "actor_%d", i);
		for(j=eaSize(&pActor->eaFSMVariableDefs)-1; j>=0; --j) {
			if (pActor->eaFSMVariableDefs[j]->eDefaultType == WVARDEF_SPECIFY_DEFAULT && pActor->eaFSMVariableDefs[j]->eType == WVAR_MESSAGE) {
				worldEncounter_FixupVariableMessage(pActor->eaFSMVariableDefs[j]->pSpecificValue, pcFilename, pcGroupName, buf, pcScope, j);
			}
		}
	}
}


void encounterTemplate_Optimize(EncounterTemplate *pTemplate)
{
	int i, j, k;
	int iPartitionIdx = PARTITION_STATIC_CHECK; // Using Static Check to really mean "never runs at a real time"

	if (eaSize(&pTemplate->eaActors) <= 1) {
		// If zero or one actors, do the de-optimize as being the best optimize action
		encounterTemplate_Deoptimize(pTemplate);
		return;
	} 

	// -- Critter Group --

	// If no critter group on template, set most common group from actors there
	if (!pTemplate->pActorSharedProperties || !GET_REF(pTemplate->pActorSharedProperties->hCritterGroup)) {
		CritterGroup *pBestGroup = NULL;
		int iBestCount = 0;

		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];

			if ((pActor->critterProps.eCritterType == ActorCritterType_CritterGroup) && 
				GET_REF(pActor->critterProps.hCritterGroup) && 
				(pBestGroup != GET_REF(pActor->critterProps.hCritterGroup))) {

				CritterGroup *pTestGroup = GET_REF(pActor->critterProps.hCritterGroup);
				int iTestCount = 1;
				for(j=i-1; j>=0; --j) {
					EncounterActorProperties *pOtherActor = pTemplate->eaActors[j];
					if ((pOtherActor->critterProps.eCritterType == ActorCritterType_CritterGroup) &&
						(GET_REF(pOtherActor->critterProps.hCritterGroup) == pTestGroup)) {
						++iTestCount;
					}
				}
				if (iTestCount > iBestCount) {
					iBestCount = iTestCount;
					pBestGroup = pTestGroup;
				}
			}
		}

		if (pBestGroup) {
			if (!pTemplate->pActorSharedProperties) {
				pTemplate->pActorSharedProperties = StructCreate(parse_EncounterActorSharedProperties);
			}
			pTemplate->pActorSharedProperties->eCritterGroupType = EncounterSharedCritterGroupSource_Specified;
			SET_HANDLE_FROM_REFERENT("CritterGroup", pBestGroup, pTemplate->pActorSharedProperties->hCritterGroup);
		}
	}

	// If actor uses same critter group as template, then use that
	if (pTemplate->pActorSharedProperties && 
		(pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_Specified) &&
		GET_REF(pTemplate->pActorSharedProperties->hCritterGroup)) {

		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];
			if ((pActor->critterProps.eCritterType == ActorCritterType_CritterGroup) &&
				(GET_REF(pActor->critterProps.hCritterGroup) == GET_REF(pTemplate->pActorSharedProperties->hCritterGroup))) {
				pActor->critterProps.eCritterType = ActorCritterType_FromTemplate;
				REMOVE_HANDLE(pActor->critterProps.hCritterGroup);
			}
		}
	}

	// If all actors override the critter group, don't set
	if (pTemplate->pActorSharedProperties && 
		(pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_Specified) &&
		GET_REF(pTemplate->pActorSharedProperties->hCritterGroup)) {
		int iCount = 0;

		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];
			if (pActor->critterProps.eCritterType == ActorCritterType_FromTemplate) {
				++iCount;
			}
		}
		if (iCount == 0) {
			// No actor uses the root critter group so don't bother keeping it
			REMOVE_HANDLE(pTemplate->pActorSharedProperties->hCritterGroup);

			// Remove entire properties if not needed
			if ((pTemplate->pActorSharedProperties->eFactionType == EncounterCritterOverrideType_FromCritter) ||
				!GET_REF(pTemplate->pActorSharedProperties->hFaction)) {
				StructDestroySafe(parse_EncounterActorSharedProperties, &pTemplate->pActorSharedProperties);
			}
		}
	}

	// -- Faction --

	// If no faction on template, set most common faction from actors there
	if (!pTemplate->pActorSharedProperties || !GET_REF(pTemplate->pActorSharedProperties->hFaction)) {
		CritterFaction *pBestFaction = NULL;
		int iBestCount = 0;

		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];

			if ((pActor->factionProps.eFactionType == EncounterTemplateOverrideType_Specified) && 
				GET_REF(pActor->factionProps.hFaction) && 
				(pBestFaction != GET_REF(pActor->factionProps.hFaction))) {
				CritterFaction *pTestFaction = GET_REF(pActor->factionProps.hFaction);
				int iTestCount = 1;
				for(j=i-1; j>=0; --j) {
					EncounterActorProperties *pOtherActor = pTemplate->eaActors[j];
					if ((pOtherActor->factionProps.eFactionType == EncounterTemplateOverrideType_Specified) &&
						(GET_REF(pOtherActor->factionProps.hFaction) == pTestFaction)) {
						++iTestCount;
					}
				}
				if (iTestCount > iBestCount) {
					iBestCount = iTestCount;
					pBestFaction = pTestFaction;
				}
			}
		}

		if (pBestFaction) {
			if (!pTemplate->pActorSharedProperties) {
				pTemplate->pActorSharedProperties = StructCreate(parse_EncounterActorSharedProperties);
			}
			pTemplate->pActorSharedProperties->eFactionType = EncounterCritterOverrideType_Specified;
			SET_HANDLE_FROM_REFERENT("CritterFaction", pBestFaction, pTemplate->pActorSharedProperties->hFaction);
		}
	}

	// If actor uses same faction as template, then use that
	if (pTemplate->pActorSharedProperties && 
		(pTemplate->pActorSharedProperties->eFactionType == EncounterCritterOverrideType_Specified) &&
		GET_REF(pTemplate->pActorSharedProperties->hFaction)) {

		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];
			if ((pActor->factionProps.eFactionType == EncounterTemplateOverrideType_Specified) &&
				(GET_REF(pActor->factionProps.hFaction) == GET_REF(pTemplate->pActorSharedProperties->hFaction))) {
				pActor->factionProps.eFactionType = EncounterTemplateOverrideType_FromTemplate;
				REMOVE_HANDLE(pActor->factionProps.hFaction);
			}
		}
	}

	// -- Spawn Anim --

	// If no spawn anim on template, set most common spawn anim from actors there
	if (!pTemplate->pSpawnProperties || !pTemplate->pSpawnProperties->pcSpawnAnim) {
		const char *pcBestAnim = NULL;
		int iBestCount = 0;

		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];

			if ((pActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_Specified) && 
				pActor->spawnInfoProps.pcSpawnAnim &&
				(!pcBestAnim || (stricmp(pcBestAnim, pActor->spawnInfoProps.pcSpawnAnim) != 0))) {

				const char *pcTestAnim = pActor->spawnInfoProps.pcSpawnAnim;
				int iTestCount = 1;
				for(j=i-1; j>=0; --j) {
					EncounterActorProperties *pOtherActor = pTemplate->eaActors[j];
					if ((pOtherActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_Specified) &&
						(stricmp(pOtherActor->spawnInfoProps.pcSpawnAnim, pcTestAnim) == 0)) {
						++iTestCount;
					}
				}
				if (iTestCount > iBestCount) {
					iBestCount = iTestCount;
					pcBestAnim = pcTestAnim;
				}
			}
		}

		if (pcBestAnim) {
			if (!pTemplate->pSpawnProperties) {
				pTemplate->pSpawnProperties = StructCreate(parse_EncounterSpawnProperties);
			}
			pTemplate->pSpawnProperties->eSpawnAnimType = EncounterSpawnAnimType_Specified;
			pTemplate->pSpawnProperties->pcSpawnAnim = StructAllocString(pcBestAnim);
		}
	}

	// If actor uses same spawn anim as template, then use that
	if (pTemplate->pSpawnProperties && 
		(pTemplate->pSpawnProperties->eSpawnAnimType == EncounterSpawnAnimType_Specified) &&
		pTemplate->pSpawnProperties->pcSpawnAnim) {

		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];
			if ((pActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_Specified) &&
				pActor->spawnInfoProps.pcSpawnAnim && 
				(stricmp(pActor->spawnInfoProps.pcSpawnAnim, pTemplate->pSpawnProperties->pcSpawnAnim) == 0)) {

				pActor->spawnInfoProps.eSpawnAnimType = EncounterTemplateOverrideType_FromTemplate;
				pActor->spawnInfoProps.pcSpawnAnim = NULL;
			}
		}
	}

	// -- FSM --

	// If no FSM on template, set most common FSM from actors there
	if (!pTemplate->pSharedAIProperties || !GET_REF(pTemplate->pSharedAIProperties->hFSM)) {
		FSM *pBestFSM = NULL;
		int iBestCount = 0;

		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];

			if ((pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_Specified) && 
				GET_REF(pActor->fsmProps.hFSM) && 
				(pBestFSM != GET_REF(pActor->fsmProps.hFSM))) {
				FSM *pTestFSM = GET_REF(pActor->fsmProps.hFSM);
				int iTestCount = 1;
				for(j=i-1; j>=0; --j) {
					EncounterActorProperties *pOtherActor = pTemplate->eaActors[j];
					if ((pOtherActor->fsmProps.eFSMType == EncounterTemplateOverrideType_Specified) &&
						(GET_REF(pOtherActor->fsmProps.hFSM) == pTestFSM)) {
						++iTestCount;
					}
				}
				if (iTestCount > iBestCount) {
					iBestCount = iTestCount;
					pBestFSM = pTestFSM;
				}
			}
		}

		if (pBestFSM) {
			if (!pTemplate->pSharedAIProperties) {
				pTemplate->pSharedAIProperties = StructCreate(parse_EncounterAIProperties);
			}
			pTemplate->pSharedAIProperties->eFSMType = EncounterCritterOverrideType_Specified;
			SET_HANDLE_FROM_REFERENT("FSM", pBestFSM, pTemplate->pSharedAIProperties->hFSM);
		}
	}

	// If actor uses same FSM as template, then use that
	if (pTemplate->pSharedAIProperties &&
		(pTemplate->pSharedAIProperties->eFSMType == EncounterCritterOverrideType_Specified) &&
		GET_REF(pTemplate->pSharedAIProperties->hFSM)) {

		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];
			if ((pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_Specified) &&
				(GET_REF(pActor->fsmProps.hFSM) == GET_REF(pTemplate->pSharedAIProperties->hFSM))) {
				pActor->fsmProps.eFSMType = EncounterTemplateOverrideType_FromTemplate;
				REMOVE_HANDLE(pActor->fsmProps.hFSM);
			}
		}
	}

	// -- FSM Vars --

	// If FSM Vars on actors, set most common FSM Vars there
	if (!pTemplate->pSharedAIProperties || !eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)) {
		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pTestActor = pTemplate->eaActors[i];
			for(j=eaSize(&pTestActor->eaVariableDefs)-1; j>=0; --j) {
				WorldVariableDef *pTestVar = pTestActor->eaVariableDefs[j];

				// Check if this var was already pushed to template
				if (pTemplate->pSharedAIProperties) {
					for(k=eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)-1; k>=0; --k) {
						WorldVariableDef *pOtherVar = pTemplate->pSharedAIProperties->eaVariableDefs[k];
						if (stricmp(pOtherVar->pcName, pTestVar->pcName) == 0) {
							break;
						}
					}
					if (k >= 0) {
						continue;
					}
				}

				// Check other actors to see if this var can be pushed up
				for(k=eaSize(&pTemplate->eaActors)-1; k>=0; --k) {
					EncounterActorProperties *pOtherActor = pTemplate->eaActors[k];
					int n;
					bool bCanPromote = true;

					// Don't bother testing the current actor
					if (k == i) {
						continue;
					}

					// Look if the variable has a value on this actor
					for(n=eaSize(&pOtherActor->eaVariableDefs)-1; n>=0; --n) {
						WorldVariableDef *pOtherVar = pOtherActor->eaVariableDefs[n];
						if (stricmp(pOtherVar->pcName, pTestVar->pcName) == 0) {
							break;
						}
					}
					if (n < 0) {
						// Not found on actor, so see if it's simply not cared about by FSM
						FSM *pFSM = encounterTemplate_GetActorFSM(pTemplate, pOtherActor, iPartitionIdx);
						if (pFSM) {
							FSMExternVar **eaFSMVarDefs = NULL;
							fsmGetExternVarNamesRecursive( pFSM, &eaFSMVarDefs, "Encounter" );
							for(n=eaSize(&eaFSMVarDefs)-1; n>=0; --n) {
								FSMExternVar *pOtherVar = eaFSMVarDefs[n];
								if (pOtherVar->name && stricmp(pOtherVar->name, pTestVar->pcName) == 0) {
									break;
								}
							}
							if (n >= 0) {
								// Other FSM does care about var so can't promote it
								bCanPromote = false;
							}
							eaDestroy(&eaFSMVarDefs);
						} else if (!encounterTemplate_IsActorFSMKnown(pTemplate, pOtherActor)) {
							// Can't promote if we don't know the actor's actual FSM
							bCanPromote = false;
						}
					}
					if (bCanPromote) {
						// Variable is set on all actors to something, so it can be on root
						if (!pTemplate->pSharedAIProperties) {
							pTemplate->pSharedAIProperties = StructCreate(parse_EncounterAIProperties);
						}
						eaPush(&pTemplate->pSharedAIProperties->eaVariableDefs, StructClone(parse_WorldVariableDef, pTestVar));
					}
				}
			}
		}
	}

	// If actor uses same FSM Var as template, then don't set again
	if (pTemplate->pSharedAIProperties && eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)) {
		for(i=eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)-1; i>=0; --i) {
			WorldVariableDef *pRootVar = pTemplate->pSharedAIProperties->eaVariableDefs[i];

			for(j=eaSize(&pTemplate->eaActors)-1; j>=0; --j) {
				EncounterActorProperties *pActor = pTemplate->eaActors[j];

				for(k=eaSize(&pActor->eaVariableDefs)-1; k>=0; --k) {
					WorldVariableDef *pActorVar = pActor->eaVariableDefs[k];

					if (worldVariableDefEquals(pActorVar, pRootVar)) {
						StructDestroy(parse_WorldVariableDef, pActorVar);
						eaRemove(&pActor->eaVariableDefs, k);
					}
				}
			}
		}
	}
}


void encounterTemplate_Deoptimize(EncounterTemplate *pTemplate)
{
	int i, j, k;
	int iPartitionIdx = PARTITION_STATIC_CHECK; // Using Static Check to really mean "never runs at a real time"

	// If Critter Group on template, apply to any actor without a Critter Group
	if (pTemplate->pActorSharedProperties && 
		(pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_Specified) && 
		GET_REF(pTemplate->pActorSharedProperties->hCritterGroup)
		) {
		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];

			if (pActor->critterProps.eCritterType == ActorCritterType_FromTemplate) {
				pActor->critterProps.eCritterType = ActorCritterType_CritterGroup;
				SET_HANDLE_FROM_REFERENT("CritterGroup", GET_REF(pTemplate->pActorSharedProperties->hCritterGroup), pActor->critterProps.hCritterGroup);
			}
		}

		// Clear critter group from template
		REMOVE_HANDLE(pTemplate->pActorSharedProperties->hCritterGroup);
		pTemplate->pActorSharedProperties->eCritterGroupType = EncounterSharedCritterGroupSource_Specified;
	}

	// If Critter Faction on template, apply to any actor without a Critter Faction
	if (pTemplate->pActorSharedProperties && 
		(pTemplate->pActorSharedProperties->eFactionType == EncounterCritterOverrideType_Specified) && 
		GET_REF(pTemplate->pActorSharedProperties->hFaction)
		) {
		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];

			if (pActor->factionProps.eFactionType == EncounterTemplateOverrideType_FromTemplate) {
				pActor->factionProps.eFactionType = EncounterTemplateOverrideType_Specified;
				SET_HANDLE_FROM_REFERENT("CritterFaction", GET_REF(pTemplate->pActorSharedProperties->hFaction), pActor->factionProps.hFaction);
			}
		}

		// Clear critter faction from template
		REMOVE_HANDLE(pTemplate->pActorSharedProperties->hFaction);
		pTemplate->pActorSharedProperties->eFactionType = EncounterCritterOverrideType_FromCritter;
	}

	// Clean up Shared properties
	if (pTemplate->pActorSharedProperties && 
		(pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_Specified)
		) {
		StructDestroySafe(parse_EncounterActorSharedProperties, &pTemplate->pActorSharedProperties);
	}

	// If Spawn Anim on template, apply to any actor without a Spawn Anim
	if (pTemplate->pSpawnProperties && 
		(pTemplate->pSpawnProperties->eSpawnAnimType == EncounterSpawnAnimType_Specified) && 
		pTemplate->pSpawnProperties->pcSpawnAnim
		) {
		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];

			if (pActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_FromTemplate) {
				pActor->spawnInfoProps.eSpawnAnimType = EncounterTemplateOverrideType_Specified;
				pActor->spawnInfoProps.pcSpawnAnim = StructAllocString(pTemplate->pSpawnProperties->pcSpawnAnim);
			}
		}

		// Clear spawn anim from template
		StructFreeStringSafe(&pTemplate->pSpawnProperties->pcSpawnAnim);
		pTemplate->pSpawnProperties->eSpawnAnimType = EncounterSpawnAnimType_FromCritter;
	}

	// Clean up Spawn properties
	if (pTemplate->pSpawnProperties && (pTemplate->pSpawnProperties->fSpawnLockdownTime == 0) && !pTemplate->pSpawnProperties->bIsAmbush) {
		StructDestroySafe(parse_EncounterSpawnProperties, &pTemplate->pSpawnProperties);
	}

	// If FSM on template, apply to any actor without an FSM
	if (pTemplate->pSharedAIProperties && GET_REF(pTemplate->pSharedAIProperties->hFSM)) {
		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];

			if ((pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_FromTemplate) || !GET_REF(pActor->fsmProps.hFSM)) {
				pActor->fsmProps.eFSMType = EncounterTemplateOverrideType_Specified;
				SET_HANDLE_FROM_REFERENT("FSM", GET_REF(pTemplate->pSharedAIProperties->hFSM), pActor->fsmProps.hFSM);
			}
		}

		// Clear FSM from template
		REMOVE_HANDLE(pTemplate->pSharedAIProperties->hFSM);
		pTemplate->pSharedAIProperties->eFSMType = EncounterCritterOverrideType_FromCritter;
	}

	// If variable on template, apply to any actor without that variable
	if (pTemplate->pSharedAIProperties && eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)) {
		for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = pTemplate->eaActors[i];

			for(j=eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)-1; j>=0; --j) {
				WorldVariableDef *pVarDef = pTemplate->pSharedAIProperties->eaVariableDefs[j];

				for(k=eaSize(&pActor->eaVariableDefs)-1; k>=0; --k) {
					WorldVariableDef *pActorDef = pActor->eaVariableDefs[k];
					if (pActorDef->pcName && pVarDef->pcName && (stricmp(pActorDef->pcName, pVarDef->pcName) == 0)) {
						break;
					}
				}
				if (k < 0) {
					// Not found on actor, so see if it's simply not cared about by FSM
					FSM *pFSM = encounterTemplate_GetActorFSM(pTemplate, pActor, iPartitionIdx);
					int n;
					bool bShouldKeep = true;

					if (pFSM) {
						FSMExternVar **eaFSMVarDefs = NULL;
						fsmGetExternVarNamesRecursive( pFSM, &eaFSMVarDefs, "Encounter" );
						for(n=eaSize(&eaFSMVarDefs)-1; n>=0; --n) {
							FSMExternVar *pOtherVar = eaFSMVarDefs[n];
							if (pOtherVar->name && stricmp(pOtherVar->name, pVarDef->pcName) == 0) {
								break;
							}
						}
						if (n < 0) {
							// FSM doesn't care about var so don't push it
							bShouldKeep = false;
						}
						eaDestroy(&eaFSMVarDefs);
					} else if (encounterTemplate_IsActorFSMKnown(pTemplate, pActor)) {
						// No FSM is set for this actor, so it doesn't need vars at all
						bShouldKeep = false;
					}
					if (bShouldKeep) {
						eaPush(&pActor->eaVariableDefs, StructClone(parse_WorldVariableDef, pVarDef));
					}
				}
			}
		}

		// Clear variables from template
		for(i=eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)-1; i>=0; --i) {
			StructDestroy(parse_WorldVariableDef, pTemplate->pSharedAIProperties->eaVariableDefs[i]);
		}
		eaDestroy(&pTemplate->pSharedAIProperties->eaVariableDefs);
	}

	// Clean up AI properties
	if (pTemplate->pSharedAIProperties) {
		StructDestroySafe(parse_EncounterAIProperties, &pTemplate->pSharedAIProperties);
	}
}


// --------------------------------------------------------------------------
// Encounter Difficulties
// --------------------------------------------------------------------------

AUTO_STARTUP(EncounterDifficulties);
void encounterDifficulties_Load(void)
{
	ExtraEncounterDifficulties pExtraEncounterDifficulties = {0};
	int i = 0; 

	s_pDefineEncounterDifficulties = DefineCreate();

	if (s_pDefineEncounterDifficulties) {
		loadstart_printf("Loading Encounter Difficulties... ");

		ParserLoadFiles(NULL, "defs/EncounterDifficulties.def", "EncounterDifficulties.bin", PARSER_OPTIONALFLAG, parse_ExtraEncounterDifficulties, &pExtraEncounterDifficulties);

		for (i = 0; i < eaSize(&pExtraEncounterDifficulties.ppchDifficulties); i++) {
			DefineAddInt(s_pDefineEncounterDifficulties, pExtraEncounterDifficulties.ppchDifficulties[i], i);
		}

		g_iEncounterDifficultyCount = eaSize(&pExtraEncounterDifficulties.ppchDifficulties);

		StructDeInit(parse_ExtraEncounterDifficulties, &pExtraEncounterDifficulties);

		loadend_printf("done (%d).", i); 
	}
}


int encounter_GetEncounterDifficultiesCount()
{
	return g_iEncounterDifficultyCount;
}


// --------------------------------------------------------------------------
// Dictionary Management
// --------------------------------------------------------------------------

static void encounterTemplate_Validate(EncounterTemplate *pTemplate)
{
	const char *pcTempFilename;
	int i, j, k;

	if (!resIsValidName(pTemplate->pcName)) {
		ErrorFilenamef(pTemplate->pcFilename,"Encounter Template '%s' does not have a valid name\n",pTemplate->pcName);
	}
	if( !resIsValidScope(pTemplate->pcScope) ) {
		ErrorFilenamef( pTemplate->pcFilename, "Encounter Template scope is illegal: '%s'", pTemplate->pcScope );
	}
	pcTempFilename = pTemplate->pcFilename;
	if (resFixPooledFilename(&pcTempFilename, TEMPLATE_BASE_DIR, pTemplate->pcScope, pTemplate->pcName, TEMPLATE_EXTENSION)) {
		if (IsServer()) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template filename does not match name '%s' scope '%s'", pTemplate->pcName, pTemplate->pcScope);
		}
	}

	if (pTemplate->pLevelProperties) {
		if ((pTemplate->pLevelProperties->eLevelType == EncounterLevelType_Specified) && (pTemplate->pLevelProperties->iSpecifiedMin < 1)) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template min specified level '%d' is less than 1", pTemplate->pLevelProperties->iSpecifiedMin);
		}
		if ((pTemplate->pLevelProperties->eLevelType == EncounterLevelType_Specified) && (pTemplate->pLevelProperties->iSpecifiedMax > 0) && (pTemplate->pLevelProperties->iSpecifiedMax < pTemplate->pLevelProperties->iSpecifiedMin)) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template max specified level '%d' is less than the min specified of '%d'", pTemplate->pLevelProperties->iSpecifiedMax, pTemplate->pLevelProperties->iSpecifiedMin);
		}

		if ((pTemplate->pLevelProperties->eLevelType == EncounterLevelType_MapVariable) && !pTemplate->pLevelProperties->pcMapVariable) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template level is set to pull from a map variable, but no variable name was provided");
		}

		if (pTemplate->pLevelProperties->iLevelOffsetMin > pTemplate->pLevelProperties->iLevelOffsetMax) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template min level offset '%d' is set to less than the max level offset '%d'", pTemplate->pLevelProperties->iLevelOffsetMin, pTemplate->pLevelProperties->iLevelOffsetMax);
		}

		if ((pTemplate->pLevelProperties->eClampType == EncounterLevelClampType_Specified) && (pTemplate->pLevelProperties->iClampSpecifiedMin < 0)) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template clamp min specified level '%d' is less than 0", pTemplate->pLevelProperties->iClampSpecifiedMin);
		}
		if ((pTemplate->pLevelProperties->eClampType == EncounterLevelClampType_Specified) && (pTemplate->pLevelProperties->iClampSpecifiedMax > 0) && (pTemplate->pLevelProperties->iClampSpecifiedMax < pTemplate->pLevelProperties->iClampSpecifiedMin)) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template clamp max specified level '%d' is less than the min specified of '%d'", pTemplate->pLevelProperties->iClampSpecifiedMax, pTemplate->pLevelProperties->iClampSpecifiedMin);
		}

		if ((pTemplate->pLevelProperties->eClampType == EncounterLevelClampType_MapVariable) && !pTemplate->pLevelProperties->pcClampMapVariable) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template clamp is set to pull from a map variable, but no variable name was provided");
		}
	}

	if (pTemplate->pSpawnProperties) {
		if (pTemplate->pSpawnProperties->eSpawnAnimType == EncounterTemplateOverrideType_Specified) {
			if (IsServer() && pTemplate->pSpawnProperties->pcSpawnAnim && !RefSystem_ReferentFromString("AIAnimList", pTemplate->pSpawnProperties->pcSpawnAnim)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template refers to non-existent animation '%s'", pTemplate->pSpawnProperties->pcSpawnAnim);
			}
		}
	}

	if (pTemplate->pActorSharedProperties) {
		if (pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_Specified) {
			if (IsServer() && REF_STRING_FROM_HANDLE(pTemplate->pActorSharedProperties->hCritterGroup) && !GET_REF(pTemplate->pActorSharedProperties->hCritterGroup)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template refers to non-existent critter group '%s'", REF_STRING_FROM_HANDLE(pTemplate->pActorSharedProperties->hCritterGroup));
			}
		}
		if (pTemplate->pActorSharedProperties->eFactionType == EncounterCritterOverrideType_Specified) {
			if (IsServer() && REF_STRING_FROM_HANDLE(pTemplate->pActorSharedProperties->hFaction) && !GET_REF(pTemplate->pActorSharedProperties->hFaction)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template refers to non-existent faction '%s'", REF_STRING_FROM_HANDLE(pTemplate->pActorSharedProperties->hFaction));
			}
		}
	}

	if (pTemplate->pSharedAIProperties) {
		FSM *pFSM;
		if (pTemplate->pSharedAIProperties->eFSMType == EncounterCritterOverrideType_Specified) {
			if (IsServer() && REF_STRING_FROM_HANDLE(pTemplate->pSharedAIProperties->hFSM) && !GET_REF(pTemplate->pSharedAIProperties->hFSM)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template refers to non-existent FSM '%s'", REF_STRING_FROM_HANDLE(pTemplate->pSharedAIProperties->hFSM));
			}
		}
		pFSM = encounterTemplate_GetEncounterFSM(pTemplate);
		if (pFSM && eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)) {
			FSMExternVar **eaExternVars = NULL;
			fsmGetExternVarNamesRecursive(pFSM, &eaExternVars, "Encounter");

			for(i=eaSize(&eaExternVars)-1; i>=0; --i) {
				FSMExternVar *pExternVar = eaExternVars[i];
				for(j=eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)-1; j>=0; --j) {
					WorldVariableDef *pVar = pTemplate->pSharedAIProperties->eaVariableDefs[j];
					if (pVar->pcName && pExternVar->name && (stricmp(pVar->pcName, pExternVar->name) == 0)) {
						if (!worldVariableTypeCompatibleWithFSMExternVar(pVar->eType, pExternVar)) {
							ErrorFilenamef( pTemplate->pcFilename, "Encounter Template refers to FSM '%s' variable '%s' as type '%s' when the variable is not of that type.", pFSM->name, pVar->pcName, worldVariableTypeToString(pVar->eType));
						}
					}
				}
			}
			eaDestroy(&eaExternVars);
		}
	}

	if (pTemplate->pRewardProperties) {
		if (pTemplate->pRewardProperties->eRewardType == kWorldEncounterRewardType_DefaultRewards && 
			REF_STRING_FROM_HANDLE(pTemplate->pRewardProperties->hRewardTable)) {
			ErrorFilenamef(pTemplate->pcFilename, "Encounter Template is set to RewardType 'DefaultRewards' but specifies a RewardTable %s", REF_STRING_FROM_HANDLE(pTemplate->pRewardProperties->hRewardTable));
		}
		if (REF_STRING_FROM_HANDLE(pTemplate->pRewardProperties->hRewardTable))
		{
			if (!killreward_Validate(GET_REF(pTemplate->pRewardProperties->hRewardTable), pTemplate->pcFilename))
				ErrorFilenamef(pTemplate->pcFilename, "Encounter template reward table cannot be granted from killed entities.");
		}
		if (pTemplate->pRewardProperties->eRewardLevelType != kWorldEncounterRewardLevelType_SpecificLevel && 
			pTemplate->pRewardProperties->iRewardLevel) {
			ErrorFilenamef(pTemplate->pcFilename, "Encounter Template is set to RewardLevelType 'DefaultLevel' but specifies a RewardLevel %d", pTemplate->pRewardProperties->iRewardLevel);
		} else if (pTemplate->pRewardProperties->eRewardLevelType == kWorldEncounterRewardLevelType_SpecificLevel && 
				   pTemplate->pRewardProperties->iRewardLevel <= 0) {
			ErrorFilenamef(pTemplate->pcFilename, "Encounter Template is set to RewardLevelType 'SpecificLevel' but specifies an invalid RewardLevel %d", pTemplate->pRewardProperties->iRewardLevel);
		}
	}

	for(i=eaSize(&pTemplate->eaJobs)-1; i>=0; --i) {
		AIJobDesc *pJob = pTemplate->eaJobs[i];

		if (!pJob->jobName) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template has a job with no name");
		}
		for(j=i-1; j>=0; --j) {
			if (pJob->jobName && pTemplate->eaJobs[j]->jobName && (stricmp(pJob->jobName, pTemplate->eaJobs[j]->jobName) == 0)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template has more than one job with name '%s'", pJob->jobName);
			}
		}

		if (IsServer() && !RefSystem_ReferentFromString("FSM", pJob->fsmName)) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template job '%s' refers to non-existent FSM '%s'", pJob->jobName, pJob->fsmName);
		} else if (!pJob->fsmName) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template job '%s' does not specify an FSM", pJob->jobName);
		}
	}

	for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
		EncounterActorProperties *pActor = pTemplate->eaActors[i];
		FSM *pFSM;

		if (!pActor->pcName) {
			ErrorFilenamef( pTemplate->pcFilename, "Encounter Template has an Actor with no name");
		}
		for(j=i-1; j>=0; --j) {
			if (pActor->pcName && pTemplate->eaActors[j]->pcName && (stricmp(pActor->pcName, pTemplate->eaActors[j]->pcName) == 0)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template has more than one Actor with name '%s'", pActor->pcName);
			}
		}

		if (IsServer() && (pActor->nameProps.eDisplayNameType == EncounterCritterOverrideType_Specified) && !GET_REF(pActor->nameProps.displayNameMsg.hMessage)) {
			if (REF_STRING_FROM_HANDLE(pActor->nameProps.displayNameMsg.hMessage)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' refers to non-existent message '%s'", pActor->pcName, REF_STRING_FROM_HANDLE(pActor->nameProps.displayNameMsg.hMessage));
			} else {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' is stated to have a display name, but no name was given", pActor->pcName);
			}
		}


		if (IsServer() && (pActor->nameProps.eDisplaySubNameType == EncounterCritterOverrideType_Specified) && !GET_REF(pActor->nameProps.displaySubNameMsg.hMessage)) {
			if (REF_STRING_FROM_HANDLE(pActor->nameProps.displaySubNameMsg.hMessage)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' refers to non-existent message '%s'", pActor->pcName, REF_STRING_FROM_HANDLE(pActor->nameProps.displaySubNameMsg.hMessage));
			} else {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' is stated to have a display sub name, but no name was given", pActor->pcName);
			}
		}

		if (IsServer() && (pActor->critterProps.eCritterType == ActorCritterType_CritterDef) && !GET_REF(pActor->critterProps.hCritterDef)) {
			if (REF_STRING_FROM_HANDLE(pActor->critterProps.hCritterDef)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' refers to non-existent critter def '%s'", pActor->pcName, REF_STRING_FROM_HANDLE(pActor->critterProps.hCritterDef));
			} else {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' is stated to have a critter def, but no name was given", pActor->pcName);
			}
		}

		if (IsServer() && (pActor->critterProps.eCritterType == ActorCritterType_CritterGroup)) {
			if (!GET_REF(pActor->critterProps.hCritterGroup)) {
				if (REF_STRING_FROM_HANDLE(pActor->critterProps.hCritterGroup)) {
					ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' refers to non-existent critter group '%s'", pActor->pcName, REF_STRING_FROM_HANDLE(pActor->critterProps.hCritterGroup));
				} else {
					ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' is stated to have a critter group, but no name was given", pActor->pcName);
				}
			}
		}

		if (IsServer() && (pActor->critterProps.eCritterType == ActorCritterType_FromTemplate)) {
			if (pTemplate->pActorSharedProperties &&
				(pTemplate->pActorSharedProperties->eCritterGroupType == EncounterSharedCritterGroupSource_Specified) && 
				!GET_REF(pTemplate->pActorSharedProperties->hCritterGroup)) {
				if (REF_STRING_FROM_HANDLE(pTemplate->pActorSharedProperties->hCritterGroup)) {
					ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' relies on the encounter for its critter group, but the critter group is non-existent '%s'", pActor->pcName, REF_STRING_FROM_HANDLE(pTemplate->pActorSharedProperties->hCritterGroup));
				} else {
					ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' relies on the encounter for its critter group, but critter group was not defined on the encounter", pActor->pcName);
				}
			} else if (!pTemplate->pActorSharedProperties) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' relies on the encounter for its critter group, but critter group was not defined on the encounter", pActor->pcName);
			}
		}

		if (IsServer() && 
			((pActor->critterProps.eCritterType == ActorCritterType_CritterGroup) ||
			 (pActor->critterProps.eCritterType == ActorCritterType_MapVariableGroup) ||
			 (pActor->critterProps.eCritterType == ActorCritterType_FromTemplate) ) && 
			 (!IS_HANDLE_ACTIVE(pTemplate->hParent) || pActor->bOverrideCritterType)) {
			if (!pActor->critterProps.pcRank) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' does not define its critter rank", pActor->pcName);
			} else if (!critterRankExists(pActor->critterProps.pcRank)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' refers to non-existent critter rank '%s'", pActor->pcName, pActor->critterProps.pcRank);
			}
			if (!pActor->critterProps.pcSubRank) {
				if (!gConf.bManualSubRank)
					ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' does not define its critter sub-rank", pActor->pcName);
			} else if (!critterSubRankExists(pActor->critterProps.pcSubRank)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' refers to non-existent critter sub-rank '%s'", pActor->pcName, pActor->critterProps.pcSubRank);
			}
		}

		if (IsServer() && (pActor->factionProps.eFactionType == EncounterTemplateOverrideType_Specified) && !GET_REF(pActor->factionProps.hFaction)) {
			if (REF_STRING_FROM_HANDLE(pActor->factionProps.hFaction)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' refers to non-existent faction '%s'", pActor->pcName, REF_STRING_FROM_HANDLE(pActor->factionProps.hFaction));
			} else {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' is stated to have a faction, but no name was given", pActor->pcName);
			}
		}

		if (IsServer() && (pActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_Specified) && pActor->spawnInfoProps.pcSpawnAnim) {
			if (!RefSystem_ReferentFromString("AIAnimList", pActor->spawnInfoProps.pcSpawnAnim)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' refers to non-existent spawn animation '%s'", pActor->pcName, pActor->spawnInfoProps.pcSpawnAnim);
			}
		}

		if (IsServer() && (pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_Specified) && REF_STRING_FROM_HANDLE(pActor->fsmProps.hFSM)) {
			if (!GET_REF(pActor->fsmProps.hFSM)) {
				ErrorFilenamef( pTemplate->pcFilename, "Encounter Template actor '%s' refers to non-existent FSM '%s'", pActor->pcName, REF_STRING_FROM_HANDLE(pActor->fsmProps.hFSM));
			}
		}

		pFSM = encounterTemplate_GetActorFSM(pTemplate, pActor, PARTITION_STATIC_CHECK);
		if (pFSM && eaSize(&pActor->eaVariableDefs)) {
			FSMExternVar **eaExternVars = NULL;
			fsmGetExternVarNamesRecursive(pFSM, &eaExternVars, "Encounter");

			// Validate that ones matching the FSM are defined in a matching way
			for(j=eaSize(&eaExternVars)-1; j>=0; --j) {
				FSMExternVar *pExternVar = eaExternVars[j];
				for(k=eaSize(&pActor->eaVariableDefs)-1; k>=0; --k) {
					WorldVariableDef *pVar = pActor->eaVariableDefs[k];
					if (pVar->pcName && pExternVar->name && (stricmp(pVar->pcName, pExternVar->name) == 0)) {
						if (!worldVariableTypeCompatibleWithFSMExternVar(pVar->eType, pExternVar)) {
							ErrorFilenamef( pTemplate->pcFilename, "Encounter Template refers to FSM '%s' variable '%s' as type '%s' when the variable is not of that type.", pFSM->name, pVar->pcName, worldVariableTypeToString(pVar->eType));
						}
					}
				}
			}
			eaDestroy(&eaExternVars);

			// Validate the variables are proper
			for(j=eaSize(&pActor->eaVariableDefs)-1; j>=0; --j) {
				WorldVariableDef *pVar = pActor->eaVariableDefs[j];
				worldVariableValidateDef(pVar, pVar, "Encounter Template", pTemplate->pcFilename);
			}
		}

		// TODO: Validate Interaction
	}
}

static void encounterTemplate_ValidateInheritance(EncounterTemplate *pTemplate)
{
	EncounterTemplate* pParent = GET_REF(pTemplate->hParent);
	int i;

	if (!pParent)
		return;
	for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
		EncounterActorProperties *pActor = pTemplate->eaActors[i];
		if (!encounterTemplate_GetActorByName(pParent, pActor->pcName))
		{
			//this is a new actor, everything SHOULD be overridden.
			if (!pActor->bOverrideCritterSpawnInfo ||
				!pActor->bOverrideCritterType ||
				!pActor->bOverrideDisplayName ||
				!pActor->bOverrideFaction ||
				!pActor->bOverrideFSMInfo ||
				!pActor->bOverrideMisc ||
				!pActor->bOverrideSpawnConditions)
			{
				ErrorFilenamef(pTemplate->pcFilename, "Actor %s in encounter template %s hasn't been re-saved since its parent was removed. Please correct this immediately!", pActor->pcName, pTemplate->pcName);
				pActor->bOverrideCritterSpawnInfo = true;
				pActor->bOverrideCritterType = true;
				pActor->bOverrideDisplayName = true;
				pActor->bOverrideFaction = true;
				pActor->bOverrideFSMInfo = true;
				pActor->bOverrideMisc = true;
				pActor->bOverrideSpawnConditions = true;
			}
		}
	}
}



static int encounterTemplate_ResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, EncounterTemplate *pTemplate, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
			encounterTemplate_Validate(pTemplate);
			if (s_TemplateChangeFunc) {
				(*s_TemplateChangeFunc)(pTemplate);
			}
			return VALIDATE_HANDLED;

		xcase RESVALIDATE_POST_BINNING:
			encounterTemplate_ValidateInheritance(pTemplate);
			return VALIDATE_HANDLED;

		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename((char**)&pTemplate->pcFilename, TEMPLATE_BASE_DIR, pTemplate->pcScope, pTemplate->pcName, TEMPLATE_EXTENSION);
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


void encounterTemplate_Load(void)
{
	// Loads only on the server
	resLoadResourcesFromDisk(g_hEncounterTemplateDict, TEMPLATE_BASE_DIR, ".encounter2", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
}


// Register callback when a dictionary element is loaded or reloaded
void encounterTemplate_SetPostProcessCallback(EncounterTemplateChangeFunc changeFunc)
{
	s_TemplateChangeFunc = changeFunc;
}


AUTO_RUN;
int RegisterEncounterTemplateDict(void)
{
	// Set up reference dictionary for parts and such
	g_hEncounterTemplateDict = RefSystem_RegisterSelfDefiningDictionary("EncounterTemplate", false, parse_EncounterTemplate, true, true, NULL);

	resDictManageValidation(g_hEncounterTemplateDict, encounterTemplate_ResValidateCB);
	resDictSetDisplayName(g_hEncounterTemplateDict, "Encounter Template", "Encounter Templates", RESCATEGORY_DESIGN);

	if (IsServer()) {
		resDictProvideMissingResources(g_hEncounterTemplateDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hEncounterTemplateDict, ".DisplayNameMsg.Message", ".scope", NULL, NULL, NULL);
		}
	} else if (IsClient()) {
		resDictRequestMissingResources(g_hEncounterTemplateDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}
	resDictProvideMissingRequiresEditMode(g_hEncounterTemplateDict);

	return 1;
}

// Function table for interact conditions
// moved here as to support gslencounter entcritter and gslinteraction
ExprFuncTable* encounter_CreateInteractExprFuncTable()
{
	static ExprFuncTable* s_encounterInteractFuncTable = NULL;
	if (!s_encounterInteractFuncTable) {
		s_encounterInteractFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_encounterInteractFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_encounterInteractFuncTable, "event_count");
		exprContextAddFuncsToTableByTag(s_encounterInteractFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_encounterInteractFuncTable, "encounter");
		exprContextAddFuncsToTableByTag(s_encounterInteractFuncTable, "player");
		exprContextAddFuncsToTableByTag(s_encounterInteractFuncTable, "PTECharacter");
		exprContextAddFuncsToTableByTag(s_encounterInteractFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_encounterInteractFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_encounterInteractFuncTable, "s_CritterExprFuncList");
		exprContextAddFuncsToTableByTag(s_encounterInteractFuncTable, "gameutil");
	}
	return s_encounterInteractFuncTable;
}

static bool encounter_GetAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
{
	if (pcAddString)
	{
		bool bDup = false;
		FOR_EACH_IN_EARRAY(*peaStrings, const char, pcHasString) {
			if (strcmpi(pcHasString, pcAddString) == 0) {
				bDup = true;
			}
		} FOR_EACH_END;
		if (!bDup) {
			eaPush(peaStrings, strdup(pcAddString));
		}
		return true;
	}
	return false;
}

void encounter_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	EncounterTemplate *pEncounterTemplate;
	ResourceIterator rI;

	*ppcType = strdup("EncounterTemplate");

	resInitIterator(g_ContactDictionary, &rI);
	while (resIteratorGetNext(&rI, NULL, &pEncounterTemplate))
	{
		bool bResourceHasAudio = false;

		FOR_EACH_IN_EARRAY(pEncounterTemplate->eaActors, EncounterActorProperties, pEncounterActorProperties) {
			if (pEncounterActorProperties->pInteractionProperties) {
				FOR_EACH_IN_EARRAY(pEncounterActorProperties->pInteractionProperties->eaEntries, WorldInteractionPropertyEntry, pWorldInteractionPropertyEntry)
				{
					bResourceHasAudio |= encounter_GetAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchAttemptSound,	peaStrings);
					bResourceHasAudio |= encounter_GetAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchFailureSound,	peaStrings);
					bResourceHasAudio |= encounter_GetAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchInterruptSound,	peaStrings);
					bResourceHasAudio |= encounter_GetAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchSuccessSound,	peaStrings);

					bResourceHasAudio |= encounter_GetAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchMovementReturnEndSound,		peaStrings);
					bResourceHasAudio |= encounter_GetAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchMovementReturnStartSound,	peaStrings);
					bResourceHasAudio |= encounter_GetAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchMovementTransEndSound,		peaStrings);
					bResourceHasAudio |= encounter_GetAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchMovementTransStartSound,	peaStrings);
				}
				FOR_EACH_END;
			}
		} FOR_EACH_END;
		
		*puiNumData = *puiNumData + 1;
		if (bResourceHasAudio) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	resFreeIterator(&rI);
}

#include "AutoGen/encounter_common_h_ast.c"
