/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiLib.h"

#include "CombatGlobal.h"
#include "Reward.h"
#include "WorldGrid.h"
#include "cutscene.h"
#include "error.h"
#include "gslChat.h"
#include "gslCombatDeathPrediction.h"
#include "gslContact.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventTracker.h"
#include "gslGroupProject.h"
#include "gslInteractable.h"
#include "gslInterior.h"
#include "gslLayerFSM.h"
#include "gslMapState.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslMission.h"
#include "gslNamedPoint.h"
#include "gslOldEncounter.h"
#include "gslTeamUp.h"
#include "gslPartition.h"
#include "gslPatrolRoute.h"
#include "gslPlayerMatchStats.h"
#include "gslPlayerStats.h"
#include "gslPvPGame.h"
#include "gslQueue.h"
#include "gslScoreboard.h"
#include "gslShardVariable.h"
#include "gslSound.h"
#include "gslSpawnPoint.h"
#include "gslTriggerCondition.h"
#include "gslUGC.h"
#include "gslVolume.h"
#include "oldencounter_common.h"
#include "utilitieslib.h"
#include "wlBeacon.h"
#include "wlInteraction.h"
#include "worldlib.h"


// ----------------------------------------------------------------------------------
// Static data
// ----------------------------------------------------------------------------------

static bool g_bMapIsLoaded = false;
bool g_bMapHasBeenEdited = false;
//static bool gPlaceholder_CreatedOldModelPartition = false;


// ----------------------------------------------------------------------------------
// Partition Change Callbacks
// ----------------------------------------------------------------------------------

void game_PartitionCreate(MapPartition *pPart, bool bInitMapVars)
{
	int iPartitionIdx;

	if (!pPart) {
		return;
	}
	loadstart_printf("Initializing partition %d...", pPart->iPartitionIdx);

	iPartitionIdx = pPart->iPartitionIdx;

	// Base Systems
	coarseTimerAddInstance(NULL, "PartitionCreate-Base");
	eventtracker_PartitionLoad(iPartitionIdx);
	mapState_PartitionLoad(iPartitionIdx);
	mapvariable_PartitionLoad(NULL, iPartitionIdx, bInitMapVars);
	coarseTimerStopInstance(NULL, "PartitionCreate-Base");

	// World Object Systems
	coarseTimerAddInstance(NULL, "PartitionCreate-World");
    gslGroupProject_PartitionLoad(iPartitionIdx);
	spawnpoint_PartitionLoad(iPartitionIdx);
	volume_PartitionLoad(iPartitionIdx, true);
	triggercondition_PartitionLoad(iPartitionIdx);
	layerfsm_PartitionLoad(iPartitionIdx);
	interactable_PartitionLoad(iPartitionIdx);
	encounter_PartitionLoad(iPartitionIdx, true);
	oldencounter_PartitionLoad(iPartitionIdx);
	coarseTimerStopInstance(NULL, "PartitionCreate-World");

	// Beacons
	coarseTimerAddInstance(NULL, "PartitionCreate-Beacons");
	beaconPartitionLoad(iPartitionIdx);
	coarseTimerStopInstance(NULL, "PartitionCreate-Beacons");

	// AI
	coarseTimerAddInstance(NULL, "PartitionCreate-AI");
	aiPartitionLoad(iPartitionIdx);
	coarseTimerStopInstance(NULL, "PartitionCreate-AI");

	// Civilian
	coarseTimerAddInstance(NULL, "PartitionCreate-AICivilian");
	aiCivilian_PartitionLoad(iPartitionIdx, true);
	coarseTimerStopInstance(NULL, "PartitionCreate-AICivilian");

	// Game Play Systems
	coarseTimerAddInstance(NULL, "PartitionCreate-GamePlay");
	gslTeamUp_PartitionLoad(iPartitionIdx);
	mission_PartitionLoad(iPartitionIdx);
	queue_PartitionLoad(iPartitionIdx);
	gslPVPGame_PartitionLoad(iPartitionIdx);
	playermatchstats_PartitionLoad(iPartitionIdx, true);
	cutscene_PartitionLoad(iPartitionIdx);
	gslInterior_PartitionLoad(iPartitionIdx);
	gslGameSpecific_Minigame_PartitionLoad(iPartitionIdx);
	coarseTimerStopInstance(NULL, "PartitionCreate-GamePlay");

	// Late Load
	coarseTimerAddInstance(NULL, "PartitionCreate-Late");
	eventtracker_PartitionLoadLate(iPartitionIdx);
	interactable_PartitionLoadLate(iPartitionIdx);

	// Partition specific validation happens after creating
	mapvariable_PartitionValidate(pPart->iPartitionIdx);
	coarseTimerStopInstance(NULL, "PartitionCreate-Late");

	loadend_printf("done");
}


void game_PartitionDestroy(MapPartition *pPart)
{
	int iPartitionIdx;

	if (!pPart) {
		return;
	}
	// Loadstartprintf is in "partition_DestroyByIdx"

	iPartitionIdx = pPart->iPartitionIdx;

	// Destroyed in reverse order from creation

	coarseTimerAddInstance(NULL, "PartitionDestroy-Early");
	combat_GlobalPartitionUnload(iPartitionIdx); // Created on demand
	coarseTimerStopInstance(NULL, "PartitionDestroy-Early");

	// Game Play Systems
	coarseTimerAddInstance(NULL, "PartitionDestroy-GamePlay");
	gslGameSpecific_Minigame_PartitionUnload(iPartitionIdx);
	gslInterior_PartitionUnload(iPartitionIdx);
	cutscene_PartitionUnload(iPartitionIdx);
	playermatchstats_PartitionUnload(iPartitionIdx);
	gslPVPGame_PartitionUnload(iPartitionIdx);
	queue_PartitionUnload(iPartitionIdx);
	gslScoreboard_PartitionUnload(iPartitionIdx);
	mission_PartitionUnload(iPartitionIdx);
	gslTeamUp_PartitionUnload(iPartitionIdx);
	gslCombatDeathPrediction_PartitionUnload(iPartitionIdx);
	coarseTimerStopInstance(NULL, "PartitionDestroy-GamePlay");

	// AICivilian
	coarseTimerAddInstance(NULL, "PartitionDestroy-AICivilian");
	aiCivilian_PartitionUnload(iPartitionIdx);
	coarseTimerStopInstance(NULL, "PartitionDestroy-AICivilian");

	// AI
	coarseTimerAddInstance(NULL, "PartitionDestroy-AI");
	aiPartitionUnload(iPartitionIdx);
	coarseTimerStopInstance(NULL, "PartitionDestroy-AI");

	// Beacons
	coarseTimerAddInstance(NULL, "PartitionDestroy-Beacons");
	beaconPartitionUnload(iPartitionIdx);
	coarseTimerStopInstance(NULL, "PartitionDestroy-Beacons");

	// World Object Systems
	coarseTimerAddInstance(NULL, "PartitionDestroy-World");
	oldencounter_PartitionUnload(iPartitionIdx);
	encounter_PartitionUnload(iPartitionIdx);
	interactable_PartitionUnload(iPartitionIdx);
	layerfsm_PartitionUnload(iPartitionIdx);
	triggercondition_PartitionUnload(iPartitionIdx);
	volume_PartitionUnload(iPartitionIdx);
	spawnpoint_PartitionUnload(iPartitionIdx);
    gslGroupProject_PartitionUnload(iPartitionIdx);
	coarseTimerStopInstance(NULL, "PartitionDestroy-World");

	// Base Systems
	coarseTimerAddInstance(NULL, "PartitionDestroy-Base");
	mapvariable_PartitionUnload(iPartitionIdx, true);
	coarseTimerStopInstance(NULL, "PartitionDestroy-Base");
}



// This is called very late in the partition destroy process
void game_PartitionDestroyLate(MapPartition *pPart)
{
	int iPartitionIdx;

	if (!pPart) {
		return;
	}
	iPartitionIdx = pPart->iPartitionIdx;

	mapState_PartitionUnload(iPartitionIdx);
	eventtracker_PartitionUnload(iPartitionIdx);
}


// ----------------------------------------------------------------------------------
// Map Change Callbacks
// ----------------------------------------------------------------------------------

void game_MapLoadServerAll(ZoneMap *pZoneMap, bool bFullInit, bool bInitMapVars)
{
	// Base Systems
	eventtracker_MapLoad(bFullInit);
	mapState_MapLoad();
	shardvariable_MapLoad(pZoneMap);
	mapvariable_MapLoad(pZoneMap, bInitMapVars);

	// World Object Systems
	namedpoint_MapLoad(pZoneMap);
	patrolroute_MapLoad(pZoneMap);
	spawnpoint_MapLoad(pZoneMap);
	triggercondition_MapLoad(pZoneMap);	
	volume_MapLoad(pZoneMap);
	layerfsm_MapLoad(pZoneMap);
	interactable_MapLoad(pZoneMap);
	mechanics_MapLoad(pZoneMap, bFullInit);
	encounter_MapLoad(pZoneMap, bFullInit);
	oldencounter_MapLoad(pZoneMap, bFullInit);
	beaconMapLoad(pZoneMap, bFullInit);

	// Game Play Systems
	sndServerMapLoad(pZoneMap);
	mission_MapLoad(bFullInit);
	gslPVPGame_MapLoad();
	queue_MapLoad();
	playerstats_MapLoad(bFullInit);
	playermatchstats_MapLoad(bFullInit);
	aiMapLoad(bFullInit);
	aiCivilian_MapLoad(pZoneMap, bFullInit);
	contactsystem_MapLoad();
	cutscene_MapLoad();
	gslInterior_MapLoad();
	ServerChat_MapLoad();

	// Late Load
	eventtracker_MapLoadLate(bFullInit);
	interactable_MapLoadLate(pZoneMap);

	// Initialize player related data
	mechanics_LoadAllPlayers();
}


void game_MapValidateServerAll(ZoneMap *pZoneMap)
{
	// Base Systems
	shardvariable_MapValidate(pZoneMap);
	mapvariable_MapValidate(pZoneMap);

	// World Object Systems
	namedpoint_MapValidate(pZoneMap);
	patrolroute_MapValidate(pZoneMap);
	spawnpoint_MapValidate(pZoneMap);
	volume_MapValidate();
	triggercondition_MapValidate(pZoneMap);
	layerfsm_MapValidate(pZoneMap, false);
	interactable_MapValidate();	
	encounter_MapValidate(pZoneMap);
	reward_MapValidate(pZoneMap);
	oldencounter_MapValidate();
	gslUGC_MapValidate();

	// Game Play Systems
	mission_MapValidate(pZoneMap);
	contactsystem_MapValidate();
	cutscene_MapValidate();
	interactable_ComputeMaxInteractRange( pZoneMap );		// TODO_PARTITION: NEEDS REVIEW
	gslInterior_MapValidate();
}


void game_MapUnloadServerAll(bool bInitMapVars)
{
	if (!beaconIsBeaconizer()) {
		mechanics_CleanupAllPlayers();						// TODO_PARTITION: NEEDS REVIEW
	}

	// Unload in reverse order from loading

	// Game Play Systems
	ServerChat_MapUnload();
	gslInterior_MapUnload();
	cutscene_MapUnload();
	contactsystem_MapUnload();
	aiCivilian_MapUnload();
	aiMapUnload();
	playermatchstats_MapUnload();
	playerstats_MapUnload();
	queue_MapUnload();
	gslScoreboard_MapUnload();
	gslPVPGame_MapUnload();
	mission_MapUnload();
	sndServerMapUnload();

	// World Object Systems
	beaconMapUnload();
	oldencounter_MapUnload();
	encounter_MapUnload();
	mechanics_MapUnload();
	interactable_MapUnload();
	layerfsm_MapUnload();
	triggercondition_MapUnload();
	volume_MapUnload();
	spawnpoint_MapUnload();
	patrolroute_MapUnload();
	namedpoint_MapUnload();
	
	// Base Systems
	mapvariable_MapUnload(bInitMapVars);
	shardvariable_MapUnload();
	mapState_MapUnload();
	eventtracker_MapUnload();
}


// Called when a map loads
void game_MapLoad(ZoneMap *pZoneMap)
{
	loadstart_printf("Applying map load at game level...");

	g_bMapHasBeenEdited = false;

	if (g_bMapIsLoaded) {
		Errorf("Warning: World layer called game callback for Load Map, but map is already loaded");
	}

	// Initialize non-partition data
	game_MapLoadServerAll(pZoneMap, true, true);

	// Validation takes place after all loading
	game_MapValidateServerAll(pZoneMap);

	// Call this because this function is only called following an unload
	partition_ReInitAllActive("game_MapLoad");

	loadend_printf(" done.");

	if (gbMakeBinsAndExit) {
		partition_InitMakeBinsAndExit();
	}

	g_bMapIsLoaded = true;
}


// Called when a map reloads
void game_MapReload(ZoneMap *pZoneMap)
{
	loadstart_printf("Applying map reload at game level...");

	g_bMapHasBeenEdited = false;

	if (!g_bMapIsLoaded) {
		Errorf("Warning: World layer called game callback for Reload Map, but map is NOT loaded");
	}

	// Do not need to call partition_ReInitAllActive
	game_MapLoadServerAll(pZoneMap, true, true);

	// Validation takes place after all loading
	game_MapValidateServerAll(pZoneMap);

	loadend_printf(" done.");
}


// Called when the map is changed in the editor
void game_MapEdit(ZoneMap *pZoneMap)
{
	loadstart_printf("Applying map edits at game level...");

	g_bMapHasBeenEdited = true;

	if (g_bMapIsLoaded) {
		// Do not need to call partition_ReInitAllActive
		game_MapLoadServerAll(pZoneMap, false, false);

		// Do minimal valiate on individual edits.  Do full only on map save.
		layerfsm_MapValidate(pZoneMap, true);
	} else {
		Errorf("Error: World layer called game callback for Edit Map, but map is NOT loaded.  Ignoring call.");
	}

	loadend_printf(" done.");
}


// Called when the map is saved in the editor
void game_MapSave(ZoneMap *pZoneMap)
{
	loadstart_printf("Applying map save at game level...");

	g_bMapHasBeenEdited = true;

	if (!g_bMapIsLoaded) {
		Errorf("Warning: World layer called game callback for Save Map, but map is NOT loaded");
	}

	// Do not need to call partition_ReInitAllActive
	game_MapLoadServerAll(pZoneMap, false, false);

	// Validation takes place after all loading
	game_MapValidateServerAll(pZoneMap); 

	loadend_printf(" done.");
}


// Called when a user runs InitEncounters
void game_MapReInit(void)
{
	ZoneMap *pZoneMap = worldGetActiveMap();
	loadstart_printf("Performing re-init at game level...");

	g_bMapHasBeenEdited = false;

	if (!g_bMapIsLoaded) {
		Errorf("Warning: Init Encounters executed, but map is NOT loaded");
	}

	// Reinit the map
	game_MapUnloadServerAll(false);
	game_MapLoadServerAll(pZoneMap, true, false);

	// Since unload then load, need to re-create all partitions
	partition_ReInitAllActive("game_MapReInit");

	// Only validate EncounterLayers for now.  In the future maybe validate everything?
	oldencounter_MapValidate();

	loadend_printf(" done.");
}


// Called when a map unloads
void game_MapUnload(void)
{
	loadstart_printf("Applying map unload at game level...");

	if (g_bMapIsLoaded) {
		game_MapUnloadServerAll(true);
		g_bMapIsLoaded = false;
	} else {
		// Silently ignore an unload when we're already unloaded
	}

	loadend_printf(" done.");
}


AUTO_RUN;
int GameSetWorldCallbacks(void)
{
	worldLibSetGameCallbacks(game_MapUnload, game_MapLoad, game_MapReload, game_MapEdit, game_MapSave);
	return 0;
}

