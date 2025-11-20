/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "error.h"
#include "interactionManager_common.h"
#include "oldencounter_common.h"
#include "gclDeadBodies.h"
#include "gclMapState.h"
#include "gclSpectator.h"
#include "wlInteraction.h"
#include "worldlib.h"
#include "WorldGrid.h"
#include "gclCombatDeathPrediction.h"
#include "ClientTargeting.h"


// ----------------------------------------------------------------------------------
// Static data
// ----------------------------------------------------------------------------------

static bool gMapIsLoaded = false;
static bool gPlaceholder_CreatedOldModelPartition = false;


// ----------------------------------------------------------------------------------
// Map Change Callbacks
// ----------------------------------------------------------------------------------


// Called when a map loads
void game_MapLoad(ZoneMap *pZoneMap)
{
	loadstart_printf("Loading map at game level...");

	gMapIsLoaded = true;
	gclDeadBodies_Initialize();
	mapState_MapLoad();

	loadend_printf("done");
}


// Called when a map reloads
void game_MapReload(ZoneMap *pZoneMap)
{
	loadstart_printf("Applying map reload at game level...");

	mapState_MapLoad();
	if (gConf.bAllowOldEncounterData) {
		oldencounter_UnloadLayers();
		oldencounter_LoadLayers(pZoneMap);
	}

	loadend_printf(" done.");
}


// Called when the map is changed in the editor
void game_MapEdit(ZoneMap *pZoneMap)
{
	loadstart_printf("Applying map edit at game level...");

	mapState_MapLoad();

	loadend_printf(" done.");
}


// Called when the map is saved in the editor
void game_MapSave(ZoneMap *pZoneMap)
{
	loadstart_printf("Applying map save at game level...");

	mapState_MapLoad();

	loadend_printf(" done.");
}


// Called when a user runs InitEncounters
void game_MapReInit(void)
{
	loadstart_printf("Applying map re-init at game level...");

	mapState_MapLoad();

	loadend_printf(" done.");
}


// Called when a map unloads
void game_MapUnload(void)
{
	loadstart_printf("Applying map unload at game level...");

	gclDeadBodies_Shutdown();
	gclSpectator_MapUnload();
	gclCombatDeathPrediction_Shutdown();
	gclClientTarget_Shutdown();

	if (gMapIsLoaded) {
		mapState_MapUnload();
		oldencounter_UnloadLayers();
		im_MapUnload();
		gMapIsLoaded = false;
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

