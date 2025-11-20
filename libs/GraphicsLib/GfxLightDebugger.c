/***************************************************************************



***************************************************************************/


#include "GfxLightDebugger.h"
#include "GfxLightsPrivate.h"
#include "GfxLights.h"
#include "GfxLightCache.h"
#include "GfxTextures.h"
#include "GraphicsLibPrivate.h"

#include "WorldGrid.h"
#include "RoomConn.h"


#include "GfxLightDebugger_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

GfxLightDebugger gfx_light_debugger;

void gfxLightDebuggerClear()
{
	gfx_light_debugger.cur_cache = NULL;
}

GfxLightDebug **gfxGetKeyLights(const GfxLightCacheBase *light_cache)
{
	static GfxLight **light_earray = NULL;
	GfxLightDebug **debug_lights = NULL;
	int i;

	eaSetSize(&light_earray, 0);

	if (light_cache)
	{
		for (i = 0; i < ARRAY_SIZE(light_cache->light_params.lights); ++i)
		{
			if (!light_cache->light_params.lights[i] || light_cache->light_params.lights[i] == &unlit_light.rdr_light)
				continue;
			eaPush(&light_earray, GfxLightFromRdrLight(light_cache->light_params.lights[i]));
		}
		gfxSetDebugLightCache(light_cache->id);
	}
	else
	{
		GfxGlobalDrawParams *gdraw = NULL;
		if (gfx_state.currentDevice && eaSize(&gfx_state.currentDevice->actions) > 0)
		{
			gdraw = &gfx_state.currentDevice->actions[0]->gdraw;
			for (i = 0; i < eaSize(&gfx_state.currentDevice->actions); ++i)
			{
				if (!gfx_state.currentDevice->actions[i]->is_offscreen)
					gdraw = &gfx_state.currentDevice->actions[i]->gdraw;
			}
		}
		if (gdraw && gdraw->this_frame_lights)
			eaPushEArray(&light_earray, &gdraw->this_frame_lights);
		gfxSetDebugLightCache(0);
	}

	for (i = 0; i < eaSize(&light_earray); ++i)
	{
		GfxLightDebug *debug_light;
		GfxLight *light = light_earray[i];
		RdrLight *rlight = &light->rdr_light;
		bool light_casts_shadows_this_frame = eaSize(&light->shadow_data_per_action) > 0 && light->shadow_data_per_action[0]->last_update_frame == gfx_state.frame_count-1;

		if (!light->last_uses && !light_casts_shadows_this_frame && !light->occluded)
			continue;

		debug_light = StructCreate(parse_GfxLightDebug);

		if (light->light_affect_type == WL_LIGHTAFFECT_DYNAMIC)
		{
			debug_light->affects = StructAllocString("Dynamic");
		}
		else if (light->light_affect_type == WL_LIGHTAFFECT_STATIC)
		{
			debug_light->affects = StructAllocString("Static");
		}
		else
		{
			debug_light->affects = StructAllocString("All");
		}

		if (light->tracker && light->tracker->def && light->tracker->def->name_str)
			debug_light->name = StructAllocString(light->tracker->def->name_str);
		else if (light->key_override)
			debug_light->name = StructAllocString("<Cam>");
		else if (light->is_sun)
			debug_light->name = StructAllocString("<Sun>");
		else if (light->dynamic)
			debug_light->name = StructAllocString("Unknown (FX)");
		else
			debug_light->name = StructAllocString("Unknown (Streamed)");

		debug_light->simple_light_type = rdrGetSimpleLightType(light->orig_light_type);
		copyMat4(rlight->world_mat, debug_light->world_matrix);
		negateVec3(rlight->world_mat[1], debug_light->direction);
		copyVec3(rlight->world_mat[3], debug_light->position);

		debug_light->angle1 = light->orig_outer_cone_angle;
		debug_light->angle2 = light->orig_outer_cone_angle2;
		debug_light->distance = round(distance3(gfx_state.currentCameraView->frustum.cammat[3], debug_light->position) - rlight->point_spot_params.outer_radius);
		MAX1(debug_light->distance, 0);
		debug_light->radius_float = rlight->point_spot_params.outer_radius;
		debug_light->radius = round(debug_light->radius_float);

		switch (debug_light->simple_light_type)
		{
			xcase RDRLIGHT_DIRECTIONAL:
				debug_light->light_type = StructAllocString("Directional");
				if (light->cloud_texture && light->cloud_texture->name)
					debug_light->projected_texture = allocAddString(light->cloud_texture->name);
				setVec3same(debug_light->position, 0);
				debug_light->distance = 0;
				debug_light->radius = 0;
				debug_light->radius_float = 0;

			xcase RDRLIGHT_SPOT:
				debug_light->light_type = StructAllocString("Spot");

			xcase RDRLIGHT_PROJECTOR:
				debug_light->light_type = StructAllocString("Projector");
				if (light->texture && light->texture->name)
					debug_light->projected_texture = allocAddString(light->texture->name);

			xcase RDRLIGHT_POINT:
				debug_light->light_type = StructAllocString("Omni");
				setVec3same(debug_light->direction, 0);

			xdefault:
				assertmsg(0, "Unknown light type!");
		}
		copyVec3(light->static_entry.bounds.min, debug_light->bound_min);
		copyVec3(light->static_entry.bounds.max, debug_light->bound_max);
		copyVec3(light->movingBoundsMin, debug_light->moving_bound_min);
		copyVec3(light->movingBoundsMax, debug_light->moving_bound_max);

		debug_light->casts_shadows = !!(light->orig_light_type & RDRLIGHT_SHADOWED);
		debug_light->is_dynamic = light->dynamic;
		debug_light->use_count = light->last_uses;
		debug_light->light_id = light->id;
		debug_light->occluded = light->occluded;
		debug_light->indoors = light->indoors;

		if (!debug_light->projected_texture)
			debug_light->projected_texture = allocAddString("");

		copyVec3(light->light_colors.hsv_ambient, debug_light->ambient_color);
		copyVec3(light->light_colors.hsv_diffuse, debug_light->diffuse_color);
		copyVec3(light->light_colors.hsv_specular, debug_light->specular_color);

		if (light_casts_shadows_this_frame)
		{
			debug_light->shadowed = StructCreate(parse_GfxShadowLightDebug);
			debug_light->shadowed->drawn_shadow_caster_count = light->shadow_data_per_action[0]->last_draw_count;
			debug_light->shadowed->drawn_shadow_caster_tri_count = light->shadow_data_per_action[0]->last_tri_count;
			debug_light->shadowed->shadowmap_quality = MIN(light->shadow_data_per_action[0]->used_surface_size_ratio[0], light->shadow_data_per_action[0]->used_surface_size_ratio[1]);
			debug_light->casts_shadows_this_frame = true;
			
		}
		
		if (debug_light->casts_shadows)
		{
			debug_light->shadow_sort_val = light->debug_shadow_sort_param;
		}
		else
		{
			debug_light->shadow_sort_val = 0;
		}


		{
			Room* owner_room = light->room_assignment_valid ? light->owner_room : 0;
			if (owner_room)
			{
				char owner_room_label[ 256 ];
				sprintf(owner_room_label, "%s (0x%p)", owner_room->def_name, owner_room);
				debug_light->owner_room_name = StructAllocString(owner_room_label);
				debug_light->owner_room_limits_lights = owner_room->limit_contained_lights_to_room;
			}
			else
			{
				debug_light->owner_room_name = StructAllocString("");
				debug_light->owner_room_limits_lights = false;
			}
		}

		eaPush(&debug_lights, debug_light);
	}

	return debug_lights;
}

GfxLightDebug **gfxGetShadowedLights(void)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	GfxLightDebug **debug_lights = NULL;

	return debug_lights;
}

bool gfxLightDebuggerIsCacheIndoors(SA_PARAM_NN_VALID const GfxLightCacheBase *light_cache)
{
	return light_cache->is_indoors;
}

char * gfxLightDebuggerGetCacheRoomList(SA_PARAM_NN_VALID const GfxLightCacheBase *light_cache)
{
	char * estrRoomList = NULL;
	FOR_EACH_IN_EARRAY_FORWARDS(light_cache->rooms, Room, Room);
		estrConcatf(&estrRoomList, "%s ", light_cache->rooms[iRoomIndex]->def_name);
	FOR_EACH_END;
	return estrRoomList;
}
