#include "net/net.h"
#include "net/netpacketutil.h"
#include "error.h"

#include "wlTime.h"
#include "wlState.h"

#include "WorldGridPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static void sendDefLib(Packet *pak, GroupDefLib *def_lib)
{
	if (def_lib)
	{
		GroupDef **defs = groupLibGetDefEArray(def_lib);
		int i;

		for (i = 0; i < eaSize(&defs); i++)
		{
			GroupDef *def = defs[i];
			if (def->all_mod_time != def->server_mod_time)
			{
				assert(def->name_uid != 0);
				pktSendBitsAuto(pak, def->name_uid);
				groupFixupBeforeWrite(def, true);
				pktSendStruct(pak, def, parse_GroupDef);
				def->server_mod_time = def->all_mod_time;
			}
		}
	}
	pktSendBitsAuto(pak, 0);

}

static void sendObjectLibrary(Packet *pak)
{
	sendDefLib(pak, objectLibraryGetEditingDefLib());
}

static void sendLayer(ZoneMapLayer *layer, Packet *pak)
{
	pktSendString(pak, layer->filename);
	pktSendString(pak, layer->region_name);

	sendDefLib(pak, layer->grouptree.def_lib);
}

int worldNeedsUpdate(U32 last_update_time)
{
	return last_update_time < world_grid.mod_time;
}

// Server-to-client updates
bool worldSendUpdate(Packet *pak, int full_update, ResourceCache *ref_cache_if_allow_updates, U32 *last_update_time)
{
	ZoneMap *zmap = worldGetPrimaryMap();
	
	if (!zmap)
	{
		// Bad or invalid zone map
		*last_update_time = world_grid.mod_time;
		return false;
	}

	// One-way Server->Client only updates
	
	// For message sending
	if (!ref_cache_if_allow_updates || !resServerAreTherePendingUpdates(ref_cache_if_allow_updates))
	{
		pktSendBits(pak, 1, 0);
	}
	else
	{
		pktSendBits(pak, 1, 1);
		resServerSendUpdatesToClient(pak, ref_cache_if_allow_updates, NULL, LANGUAGE_DEFAULT, false);
	}

	pktSendBits(pak, 1, full_update);

	pktSendBitsAuto(pak, world_grid.map_reset_count);
	pktSendBitsAuto(pak, world_grid.needs_reload);
	pktSendStruct(pak, zmapGetInfo(zmap), parse_ZoneMapInfo);

	verbose_printf("world update: %d bytes\n",pktGetSize(pak));

	*last_update_time = world_grid.mod_time;
	return true;
}

bool worldSendPeriodicUpdate(Packet *pak, U32 *last_sky_update_time, bool bRecordSkyTime)
{
	ZoneMap *zmap = worldGetPrimaryMap();
	int i;

	if (!zmap)
		return false;

	pktSendF32(pak, wlTimeGet());
	pktSendF32(pak, wlTimeGetScale());
	pktSendF32(pak, wlTimeGetStepScaleGame());
	pktSendF32(pak, wlTimeGetStepScaleDebug());

	if (*last_sky_update_time != zmap->sky_override_mod_time)
	{
		// send sky override updates
		pktSendBits(pak, 1, 1);

		// This (hacky) change made to fix sky file update that gets erased (login success) after it is sent and will need to be sent to client again.
		if(bRecordSkyTime)
		{
			*last_sky_update_time = zmap->sky_override_mod_time;
		}

		pktSendBitsAuto(pak, eaSize(&zmap->map_info.regions));

		for (i = 0; i < eaSize(&zmap->map_info.regions); ++i)
		{
			WorldRegion *region = zmap->map_info.regions[i];
			pktSendString(pak, region->name);
			if (region->sky_override)
			{
				pktSendBits(pak, 1, 1);
				pktSendStruct(pak, region->sky_override, parse_WorldRegionSkyOverride);
			}
			else
			{
				pktSendBits(pak, 1, 0);
			}
		}
	}
	else
	{
		pktSendBits(pak, 1, 0);
	}

	return true;
}

// Pausing client-to-server updates
static bool worldLockedUpdatesPaused = false;
void worldPauseLockedUpdates(bool pause)
{
	worldLockedUpdatesPaused = pause;
}

// Client-to-server updates (locked layers only)
void worldSendLockedUpdate(NetLink *link, int cmd)
{
	Packet *pak;
	int i;

	if (world_grid.active_map->map_info.genesis_data)
	{
		return;
	}

	if (isProductionMode() || isProductionEditMode())
		return;

	world_grid.active_map->genesis_data_preview = false;

	if (!worldLockedUpdatesPaused && (world_grid.mod_time != world_grid.server_mod_time || world_grid.active_map->deleted_layer))
	{
		pak = pktCreate(link, cmd);

		// Send updates to object library pieces
		sendObjectLibrary(pak);

		// Layers
		if (world_grid.active_map->map_info.genesis_data)
		{
			pktSendBitsAuto(pak, 0); // We're not sending any layers
		}
		else
		{
			int layer_cnt = eaSize(&world_grid.active_map->layers);
			// Don't send or receive the scratch layer to prevent syncing issues.
			for (i = 0; i < eaSize(&world_grid.active_map->layers); i++) {
				if(world_grid.active_map->layers[i]->scratch)
					layer_cnt--;		
			}
			pktSendBitsAuto(pak, layer_cnt); // How many layers we're sending
			for (i = 0; i < eaSize(&world_grid.active_map->layers); i++)
			{
				ZoneMapLayer *layer = world_grid.active_map->layers[i];
				if(layer->scratch)
					continue;
				sendLayer(layer, pak);
			}
		}

		world_grid.server_mod_time = world_grid.mod_time;

		world_grid.active_map->deleted_layer = false;

		pktSend(&pak);
	}
}
