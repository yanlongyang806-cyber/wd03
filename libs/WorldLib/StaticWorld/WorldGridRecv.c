#include <process.h>

#include "timing.h"
#include "StringCache.h"
#include "wininclude.h"

#include "wlTime.h"
#include "wlState.h"
#include "wlUGC.h"

#include "WorldGridPrivate.h"
#include "WorldGridLoadPrivate.h"
#include "ObjectLibrary.h"
#include "WorldCellStreaming.h"
#include "ZoneMapLayer.h"
#include "GroupdbModify.h"

#include "net/net.h"
#include "net/netpacketutil.h"
#include "structNet.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

static bool receiveIntoDefLib(SA_PARAM_NN_VALID Packet *pak, SA_PARAM_NN_VALID GroupDefLib *def_lib)
{
	if (!def_lib)
	{
		assert(pktGetBitsAuto(pak) == 0);
		return false;
	}
	else
	{
		U32 uid;
		int i;
		bool changed = false;
		GroupDef **modified_defs = NULL;
		GroupDef **defs_to_check = NULL;
		StashTable def_table = stashTableCreateInt(64);

		// Detect changed defs
		while ((uid = pktGetBitsAuto(pak)) != 0)
		{
			GroupDef *new_def = pktGetStruct(pak, parse_GroupDef);
			GroupDef *old_def = groupLibFindGroupDef(def_lib, uid, false);
			if (old_def)
			{
				bool changes;
				groupFixupBeforeWriteEx(old_def, false, true); // Make sure the StructParser sections are up-to-date
																	//Be silent about child library groups not being in the library, as they're about to be in the library &
																	//the errors window should not be spammed when unnecessary.
				changes = StructCompare(parse_GroupDef, old_def, new_def, 0, 0, 0);
				if (changes)
				{
					FOR_EACH_IN_EARRAY(old_def->children, GroupChild, child)
					{
						GroupDef *child_def = groupChildGetDef(old_def, child, true);
						if (child_def)
						{
							eaPushUnique(&defs_to_check, child_def);
						}
					}
					FOR_EACH_END;
					eaPush(&defs_to_check, old_def);

					stashIntAddPointer(def_table, new_def->name_uid, new_def, false);
				}
			}
			else
			{
				stashIntAddPointer(def_table, new_def->name_uid, new_def, false);
			}

			objectLibraryConsistencyCheck(true);
		}

		// Do deletions
		FOR_EACH_IN_EARRAY(defs_to_check, GroupDef, child_def)
		{
			groupdbCheckRemoveDefFromLib(child_def, def_table);
		}
		FOR_EACH_END;
		eaDestroy(&defs_to_check);

		// Apply def changes
		FOR_EACH_IN_STASHTABLE(def_table, GroupDef, new_def)
		{
			GroupDef *old_def = groupLibFindGroupDef(def_lib, new_def->name_uid, false);
			GroupProperties *old_properties = NULL;
			if (old_def)
			{
				printf("Updating def %s (%d) -> %s (%d)\n", old_def->name_str, old_def->name_uid, new_def->name_str, new_def->name_uid);

				old_properties = StructClone(parse_GroupProperties, &old_def->property_structs);

				stashRemoveInt(old_def->def_lib->def_name_table, old_def->name_str, NULL);
				StructCopy(parse_GroupDef, new_def, old_def, STRUCTCOPYFLAG_DONT_COPY_NO_ASTS, 0, 0);
				stashAddInt(old_def->def_lib->def_name_table, old_def->name_str, old_def->name_uid, true);

				eaPush(&modified_defs, old_def);
				groupDefModify(old_def, UPDATE_GROUP_PROPERTIES, true);
				changed = true;

				StructDestroy(parse_GroupDef, new_def);
				new_def = old_def;
			}
			else
			{
				GroupDef *dup = NULL;
				printf("New def %s (%d)\n", new_def->name_str, new_def->name_uid);
				old_properties = StructCreate(parse_GroupProperties);
				if (!groupLibAddGroupDef(def_lib, new_def, &dup))
				{
					Errorf("Internal error adding def %s (%d), conflicts with %s (%d)", new_def->name_str, new_def->name_uid, dup->name_str, dup->name_uid);
					assert(0);
				}
				else
				{
					assert(new_def->def_lib == def_lib);
					eaPush(&modified_defs, new_def);
					groupDefModify(new_def, UPDATE_GROUP_PROPERTIES, true);
					changed = true;
				}
			}

			// Messages
			if (old_properties)
			{
				GroupDefPropertyGroup old_gdg = { 0 };
				GroupDefPropertyGroup gdg = { 0 };

				gdg.filename = new_def->filename;
				eaPush( &gdg.props, &new_def->property_structs );
				eaPush( &old_gdg.props, old_properties );

				groupDefFixupMessages(new_def);
				langApplyEditorCopy(parse_GroupDefPropertyGroup, &gdg, &old_gdg, true, true);

				eaDestroy( &gdg.props );
				eaDestroy( &old_gdg.props );

				StructDestroy(parse_GroupProperties, old_properties);
			}
		}
		FOR_EACH_END;
		stashTableDestroy(def_table);

		objectLibraryConsistencyCheck(true);

		for (i = 0; i < eaSize(&modified_defs); i++)
			groupFixupAfterRead(modified_defs[i], false);

		objectLibraryConsistencyCheck(true);

		eaDestroy(&modified_defs);
		return changed;
	}
}

static bool receiveObjectLibrary(SA_PARAM_NN_VALID Packet *pak)
{
	return receiveIntoDefLib(pak, objectLibraryGetEditingDefLib());
}

static bool receiveLayer(SA_PARAM_NN_VALID Packet *pak, ZoneMapLayer *layer)
{
	bool ret;
	ret = receiveIntoDefLib(pak, layer->grouptree.def_lib);
	if (layer->grouptree.def_lib)
		layerResetRootGroupDef(layer, true);
	return ret;
}

static bool receiveLayers(Packet *pak, ZoneMap *zmap)
{
	int i, j, count;
	bool changed = false;
	bool overwrite_layers = false;
	ZoneMapLayer **temp_layers = NULL;
	count = pktGetBitsAuto(pak);

	if ((zmapInfoGetLockOwner(&zmap->map_info) == pktLinkID(pak)) || (zmapInfoGetLockOwnerIsZero(&zmap->map_info)))
	{
		overwrite_layers = true;
	}

	for (i = 0; i < count; i++)
	{
		ZoneMapLayer *found_layer = NULL;
		const char *filename, *region;

		filename = allocAddFilename(pktGetStringTemp(pak));
		region = allocAddString(pktGetStringTemp(pak));

		for (j = 0; j < eaSize(&zmap->layers); j++)
			if (filename == zmap->layers[j]->filename)
			{
				found_layer = zmap->layers[j];
				if (overwrite_layers)
				{
					eaRemove(&zmap->layers, j);
					eaPush(&temp_layers, found_layer);
				}
				break;
			}

			if (!found_layer)
			{
				if (overwrite_layers)
				{
					found_layer = zmapNewLayer(zmap, eaSize(&zmap->layers), filename);
					assert(found_layer);
					layerLoadGroupSource(found_layer, zmap, NULL, false);

					changed = true;

					// New layers are checked out by default
					found_layer->lock_owner = pktLinkID(pak);

					if (overwrite_layers)
					{
						eaFindAndRemove(&zmap->layers, found_layer);
						eaPush(&temp_layers, found_layer);
					}
				}
				else
				{
					Errorf("Client attempted to modify unknown layer: %s", filename);
					return false;
				}
			}
			assert(found_layer);

			if (overwrite_layers)
			{
				if (found_layer->region_name != region)
					changed = true;

				found_layer->region_name = region;
			}

			if (receiveLayer(pak,found_layer))
				changed = true;
	}

	if (overwrite_layers)
	{
		// Don't send or receive the scratch layer to prevent syncing issues.
		for ( j=0; j < eaSize(&zmap->layers); j++ )
		{
			ZoneMapLayer *found_layer = zmap->layers[j];
			if(found_layer->scratch) {
				eaRemove(&zmap->layers, j);
				eaPush(&temp_layers, found_layer);
			}
		}
		if (eaSize(&zmap->layers) > 0)
		{
			worldCellSetEditable();
			eaDestroyEx(&zmap->layers, layerFree);
			changed = true;
		}
		zmap->layers = temp_layers;
	}
	return changed;
}


static int disable_delayed_worldgrid;
// Disables delayed world grid loading which is enabled for patch streaming and/or
//  development mode.  Can enable this if a) development mode is crashing or b)
//  we have patch streaming on, but we actually pre-patch map data, and this code
//  is causing problems
AUTO_CMD_INT(disable_delayed_worldgrid, disable_delayed_worldgrid) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

static bool delayed_load_in_progress;
static int world_delay_load_map_reset_count;
void worldReceiveUpdateAsyncLoadCallback(void *userData)
{
	assert(delayed_load_in_progress);
	assert(world_grid.map_reset_count == world_delay_load_map_reset_count+1); // loading the zonemap will have incremented this
	verbose_printf("world grid changed\n");
	world_grid.map_reset_count = world_delay_load_map_reset_count;
	worldSetModTime_AsyncOK(0);
	delayed_load_in_progress = false;
}

// Server-to-client updates
void worldReceiveUpdate(Packet *pak, MapNameRecordCallback nameCB)
{
	ZoneMapInfo *zminfo;
	ZoneMap *zmap = worldGetPrimaryMap();
	bool new_map = false;
	U32 map_reset_count, full_update;
	bool ref_update;

	PERFINFO_AUTO_START_FUNC();

		verbose_printf("worldReceiveUpdate called\n");

		ref_update = pktGetBits(pak, 1);
		if (ref_update)
			resClientProcessServerUpdates(pak);

		full_update = pktGetBits(pak, 1);

		map_reset_count = pktGetBitsAuto(pak);
		world_grid.needs_reload = pktGetBitsAuto(pak);
		zminfo = pktGetStruct(pak, parse_ZoneMapInfo);

		if (world_grid.needs_reload)
			Errorf("The current zonemap has changed on disk and could not be reloaded; saving to the zonemap has been disabled. Please save your pending changes to layers and object library pieces and then reload the map from source.");

		if (full_update)
			new_map = true;
		if (map_reset_count != world_grid.map_reset_count)
			new_map = true;

		if (new_map && !wl_state.stop_map_transfer)
		{
			if(delayed_load_in_progress)
				worldLoadZoneMapAsyncCancel();
			verbose_printf("Got world update %s\n", zminfo->map_name);
			if ((isPatchStreamingOn() || isDevelopmentMode()) && !disable_delayed_worldgrid) // Enabling this always in development mode to get some better testing, though we may find this does not work with edit mode?
			{
				delayed_load_in_progress = true;
				if (!worldLoadZoneMapAsync(zminfo, worldReceiveUpdateAsyncLoadCallback, NULL))
				{
					assertmsg(0, "Couldn't start patching map!");
				}
				if (nameCB)
					nameCB(zminfo->filename);
				world_delay_load_map_reset_count = map_reset_count;
				zminfo = NULL;
			} else {
				if (!worldLoadZoneMap(zminfo, false, false))
				{
					assertmsg(0, "Couldn't load map!");
				}
				verbose_printf("world grid changed\n");
				if (nameCB)
					nameCB(zminfo->filename);
			}
			world_grid.map_reset_count = map_reset_count;
			worldSetModTime_AsyncOK(0);
		}
		StructDestroy(parse_ZoneMapInfo, zminfo);

	PERFINFO_AUTO_STOP();
}

void worldReceivePeriodicUpdate(Packet *pak)
{
	ZoneMap *zmap = worldGetPrimaryMap();

	PERFINFO_AUTO_START_FUNC();

	wlTimeUpdateServerTimeDiff(pktGetF32(pak));
	wlTimeSetScale(pktGetF32(pak));
	wlTimeSetStepScaleGame(pktGetF32(pak));
	wlTimeSetStepScaleDebug(pktGetF32(pak));

	if (pktGetBits(pak, 1))
	{
		// receive sky override updates
		int count;
		for (count = pktGetBitsAuto(pak); count > 0; --count)
		{
			const char *region_name = pktGetStringTemp(pak);
			WorldRegion *region = zmapGetWorldRegionByNameEx(zmap, region_name, false);
			WorldRegionSkyOverride *sky_override = NULL;

			if (pktGetBits(pak, 1))
				sky_override = pktGetStruct(pak, parse_WorldRegionSkyOverride);

			if (region)
			{
				StructDestroySafe(parse_WorldRegionSkyOverride, &region->sky_override);
				region->sky_override = sky_override;
			}
			else
			{
				StructDestroy(parse_WorldRegionSkyOverride, sky_override);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

// Client-to-server updates (locked layers only)
void worldReceiveLockedUpdate(Packet *pak, NetLink *link)
{
	bool changed;
	bool bEditCallbackCalled = false;
	ZoneMap *zmap = worldGetPrimaryMap();

	if (zmap->map_info.genesis_data)
		return;

	// Object library updates
	changed = receiveObjectLibrary(pak);

	// Layers
	if (receiveLayers(pak, zmap))
		changed = true;

	if (!changed)
		return;

	assert(!delayed_load_in_progress); // If we haven't loaded the map yet, this probably isn't going to have worked

	// post processing
	wl_state.HACK_disable_game_callbacks = true;
	worldUpdateBounds(true, false);
	zmapTrackerUpdate(zmap, false, true);
	wl_state.HACK_disable_game_callbacks = false;

	bEditCallbackCalled = worldCheckForNeedToOpenCells();

	// Callback after change, if necessary
	if(!bEditCallbackCalled && wl_state.edit_map_game_callback)
		wl_state.edit_map_game_callback(worldGetPrimaryMap());
}
