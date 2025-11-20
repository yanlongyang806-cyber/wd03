/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "encounter_common.h"
#include "entCritter.h"
#include "EntDebugMenu.h"
#include "Entity.h"
#include "GlobalTypes.h"
#include "gslEncounter.h"
#include "gslOldEncounter.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "wlEncounter.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

// Debugging info for the server
static U32 s_numSpawnedEnts = 0;
static U32 s_spawnedFSMcost = 0;
static U32 s_potentialFSMcost = 0;
static U32 s_numRunningEncs = 0;


#define MAX_ENCOUNTER_BEACONS 200

#define ENCOUNTER_DEBUG_TICK 300

extern PartitionEncounterData **s_eaPartitionEncounterData;
extern OldEncounterPartitionState **s_eaOldEncounterPartitionStates;


// ----------------------------------------------------------------------------------
// Debug Menu
// ----------------------------------------------------------------------------------

static void encounterdebug_updateGlobalDebugInfo(void)
{
	Entity **eaEntities = NULL;
	int i, fCost = 0;

	s_numRunningEncs = 0;
	s_potentialFSMcost = 0;

	// Loop the partitions
	for(i=eaSize(&s_eaPartitionEncounterData)-1; i>=0; --i) {
		if (s_eaPartitionEncounterData[i]) {
			encounter_GetAllSpawnedEntities(i, &eaEntities);
			s_numRunningEncs += encounter_GetNumRunningEncounters(i);
			s_potentialFSMcost = MAX(s_potentialFSMcost, encounter_GetPotentialFSMCost(i));
		}
	}

	// Add the old encounter data
	if (gConf.bAllowOldEncounterData) {
		// Loop the partitions
		for(i=eaSize(&s_eaOldEncounterPartitionStates)-1; i>=0; --i) {
			if (s_eaOldEncounterPartitionStates[i]) {
				oldencounter_GetAllSpawnedEntities(i, &eaEntities);
				s_numRunningEncs += oldencounter_GetNumRunningEncounters(i);
				s_potentialFSMcost += oldencounter_GetPotentialFSMCost(i);
			}
		}
	}

	// Iterate entities to find info on them
	for(i=eaSize(&eaEntities)-1; i>=0; --i) {
		Entity *pEnt = eaEntities[i];
		if (pEnt->pCritter) {
			if (pEnt->pCritter->encounterData.pGameEncounter) {
				fCost += encounter_GetActorMaxFSMCost(entGetPartitionIdx(pEnt), pEnt->pCritter->encounterData.pGameEncounter, pEnt->pCritter->encounterData.iActorIndex);
			} else if (gConf.bAllowOldEncounterData && pEnt->pCritter->encounterData.sourceActor) {
				fCost += oldencounter_ActorGetMaxFSMCost(pEnt->pCritter->encounterData.sourceActor);
			} 
		}
	}
	eaDestroy(&eaEntities);

	s_numSpawnedEnts = eaSize(&eaEntities);
	s_spawnedFSMcost = (U32)fCost;
}


void encounterdebug_Update(Entity *pPlayerEnt)
{
	static U32 uLastUpdateTime = 0;
	EncounterDebug *pEncDebug;
	PlayerDebug* pDebug = entGetPlayerDebug(pPlayerEnt, false);
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	if (!pDebug) {
		return;
	}

	// Update beacons
	if (pEncDebug = pDebug->encDebug) {
		int iDist, i, n;
		Vec3 vPlayerPos;
		GameEncounter **eaNearbyEncs = NULL;
		OldEncounter **eaNearbyOldEncs = NULL;

		entGetPos(pPlayerEnt, vPlayerPos);

		for(iDist = 500; iDist <= 2000; iDist += 500) {
			eaNearbyEncs = encounter_GetEncountersWithinDistance(vPlayerPos, iDist);
			if (gConf.bAllowOldEncounterData) {
				OldEncounterPartitionState *pState = oldencounter_GetPartitionState(iPartitionIdx);
				eaNearbyOldEncs = oldencounter_GetEncountersWithinDistance(pState, vPlayerPos, iDist);
			}
			if ((eaSize(&eaNearbyEncs)+eaSize(&eaNearbyOldEncs)) >= MAX_ENCOUNTER_BEACONS) {
				break;
			}
		}

		n = MIN(eaSize(&eaNearbyEncs) + eaSize(&eaNearbyOldEncs), MAX_ENCOUNTER_BEACONS);

		eaClearStruct(&pEncDebug->eaEncBeacons, parse_EncounterDebugBeacon);
		eaSetSize(&pEncDebug->eaEncBeacons, n);
		--n;
		for(i=eaSize(&eaNearbyEncs)-1; i>=0 && n>=0; --i, --n) {
			GameEncounter *pEncounter = eaNearbyEncs[i];
			GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);

			pEncDebug->eaEncBeacons[n] = StructCreate(parse_EncounterDebugBeacon);
			pEncDebug->eaEncBeacons[n]->eCurrState = pState->eState;
			copyVec3(pEncounter->pWorldEncounter->encounter_pos, pEncDebug->eaEncBeacons[n]->vEncPos);
		}
		for(i=eaSize(&eaNearbyOldEncs)-1; i>=0 && n>=0; --i, --n) {
			pEncDebug->eaEncBeacons[n] = StructCreate(parse_EncounterDebugBeacon);
			pEncDebug->eaEncBeacons[n]->eCurrState = eaNearbyOldEncs[i]->state;
			copyVec3(eaNearbyOldEncs[i]->mat[3], pEncDebug->eaEncBeacons[n]->vEncPos);
		}

		pEncDebug->uLastBeaconUpdate = timeSecondsSince2000();
	}

	// Update the number of spawned entities once a second.  TODO: this may get updated and sent even when we don't care about it.
	pDebug->numSpawnedEnts = s_numSpawnedEnts;
	pDebug->numRunningEncs = s_numRunningEncs;
	pDebug->spawnedFSMCost = s_spawnedFSMcost;
	pDebug->numTotalEncs = encounter_GetNumEncounters(iPartitionIdx) + (gConf.bAllowOldEncounterData ? oldencounter_GetNumEncounters(iPartitionIdx) : 0);
	pDebug->potentialFSMCost = s_potentialFSMcost;
	entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
}


void encounterdebug_DebugMenu(Entity *pPlayerEnt, DebugMenuItem *pGroupRoot)
{
	int i;
	DebugMenuItem *pMenuItem;
	char dispStr[1024];
	char cmdStr[1024];
	int n;
	PlayerDebug* pDebug = entGetPlayerDebug(pPlayerEnt, false);

	PERFINFO_AUTO_START_FUNC();

	// All simple commands are first
	debugmenu_AddNewCommand(pGroupRoot, "Init Encounters", "initencounters");
	if (g_EncounterProcessing) {
		debugmenu_AddNewCommand(pGroupRoot, "Disable Encounters", "encounterprocessing 0");
	} else {
		debugmenu_AddNewCommand(pGroupRoot, "Enable Encounters", "encounterprocessing 1");
	}

	if (pDebug && pDebug->encDebug) {
		debugmenu_AddNewCommand(pGroupRoot, "Disable Beacons", "encounterbeacons 0");
	} else {
		debugmenu_AddNewCommand(pGroupRoot, "Enable Beacons", "encounterbeacons 1");
	}

	debugmenu_AddNewCommand(pGroupRoot, "Unignore All Layers", "encounterUnignoreAllLayers");

	pMenuItem = debugmenu_AddNewCommandGroup(pGroupRoot, "Event Debugging", "Event debugging menus", 0);
	debugmenu_AddNewCommand(pMenuItem, "Enable Event Debugging (prints to server console)", "EventLogDebug 1");

	pMenuItem = debugmenu_AddNewCommandGroup(pMenuItem, "Event Filters", "", 0);
	debugmenu_AddNewCommand(pMenuItem, "Filter on Clickables", "eventLogSetFilter clickableInteract");

	// Add the force team size list
	if (g_ForceTeamSize) {
		sprintf(dispStr, "Team Size (%i)", g_ForceTeamSize);
	} else {
		strcpy(dispStr, "Team Size (Normal)");
	}
	pMenuItem = debugmenu_AddNewCommandGroup(pGroupRoot, dispStr, "Force all encounters to spawn at this team size instead of default.", false);
	debugmenu_AddNewCommand(pMenuItem, "Normal", "forceteamsize 0");
	for (i = 1; i <= MAX_TEAM_SIZE; i++) {
		sprintf(cmdStr, "forceteamsize %i", i);
		sprintf(dispStr, "%i", i);
		debugmenu_AddNewCommand(pMenuItem, dispStr, cmdStr);
	}

	if (gConf.bAllowOldEncounterData) {
		// Add the encounter layer kill commands
		pMenuItem = debugmenu_AddNewCommandGroup(pGroupRoot, "Kill Encounter Layer", "Kills all spawns on an encounter layer by name.", false);
		n = g_EncounterMasterLayer ? eaSize(&g_EncounterMasterLayer->encLayers) : 0;
		for (i = 0; i < n; i++) {
			char currLayerName[1024];
			EncounterLayer *pEncLayer = g_EncounterMasterLayer->encLayers[i];
			getFileNameNoExt(currLayerName, pEncLayer->pchFilename);
			sprintf(cmdStr, "encounterlayerkill %s", currLayerName);
			debugmenu_AddNewCommand(pMenuItem, currLayerName, cmdStr);
		}

		pMenuItem = debugmenu_AddNewCommandGroup(pGroupRoot, "Ignore All Encounter Layers Except", "Turns off loading for all encounter layers but one.", false);
		n = g_EncounterMasterLayer ? eaSize(&g_EncounterMasterLayer->encLayers) : 0;
		for (i = 0; i < n; i++) {
			char currLayerName[1024];
			EncounterLayer *pEncLayer = g_EncounterMasterLayer->encLayers[i];
			getFileNameNoExt(currLayerName, pEncLayer->pchFilename);
			sprintf(cmdStr, "encounterIgnoreAllLayersExcept %s", currLayerName);
			debugmenu_AddNewCommand(pMenuItem, currLayerName, cmdStr);
		}
	}
	PERFINFO_AUTO_STOP();
}


void encounterdebug_OncePerFrame(void)
{
	static int s_EncounterDebugTick = 0;

	// Debugging information
	if (s_EncounterDebugTick++ % ENCOUNTER_DEBUG_TICK == 0) {
		PERFINFO_AUTO_START("encounterdebug_updateGlobalDebugInfo", 1);
		encounterdebug_updateGlobalDebugInfo();
		PERFINFO_AUTO_STOP();
	}

}


AUTO_COMMAND ACMD_NAME(EncounterBeacons) ACMD_ACCESSLEVEL(9);
void encounterdebug_ShowBeacons(Entity *pPlayerEnt, bool bEnable)
{
	PlayerDebug* pDebug = entGetPlayerDebug(pPlayerEnt, bEnable);
	if (pDebug) {
		if (bEnable && !pDebug->encDebug) {
			pDebug->encDebug = StructCreate(parse_EncounterDebug);

		} else if (!bEnable && pDebug->encDebug)	{
			StructDestroy(parse_EncounterDebug, pDebug->encDebug);
			pDebug->encDebug = NULL;
		}
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}
}


AUTO_RUN;
void encounterdebug_RegisterDebugMenu(void)
{
	debugmenu_RegisterNewGroup("Encounters", encounterdebug_DebugMenu);
}


