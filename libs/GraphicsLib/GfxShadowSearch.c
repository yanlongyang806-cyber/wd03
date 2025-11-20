/***************************************************************************



***************************************************************************/

#include "GfxShadowSearch.h"
#include "GfxLightsPrivate.h"
#include "GfxLights.h"
#include "GfxConsole.h"
#include "GraphicsLibPrivate.h"
#include "GfxPrimitive.h"
#include "GfxFont.h"

#include "RoomConn.h"
#include "../StaticWorld/WorldCell.h"
#include "beaconAStar.h"

#include "earray.h"
#include "qsortG.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););


static ShadowSearchData shadow_search_data = {0};
static float shadow_cross_portal_cost = 400.0f;
static int lock_shadow_caster_graph = 0; 
static int shadow_max_room_depth = 3;
int disable_shadow_caster_graph = 0;


//////////////////////////////////////////////////////////////////////////


//Use normal straight line distances instead
AUTO_CMD_INT(disable_shadow_caster_graph, disable_shadow_caster_graph);

//disable updating the shadow caster graph for debugging
AUTO_CMD_INT(lock_shadow_caster_graph, lock_shadow_caster_graph);

//The maximum number of rooms which the shadow graph builder will recurse into
AUTO_CMD_INT(shadow_max_room_depth, shadow_max_room_depth);

//The amount of extra distance that is added to a shadow caster by crossing a portal
AUTO_COMMAND ACMD_NAME(shadow_cross_portal_cost) ACMD_CATEGORY(Debug);
void set_shadow_cross_portal_cost(float val, Cmd* cmd, CmdContext* cmd_context)
{
	shadow_cross_portal_cost = val;
	gfxClearShadowSearchData();
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(shadow_cross_portal_cost);
void get_shadow_cross_portal_cost(void)
{
	conPrintf("shadow_cross_portal_cost: %f", (double)shadow_cross_portal_cost);
}


//////////////////////////////////////////////////////////////////////////


static void connectShadowSearchNode(ShadowSearchNode* first, ShadowSearchNode* second, F32 length)
{
	ShadowSearchEdge* fwd_edge, * bk_edge;
	int fwd_idx, bk_idx;

	assert(first != second);

	fwd_idx = first->dyn_edges.count;
	bk_idx = second->dyn_edges.count;

	fwd_edge = dynArrayAdd(first->dyn_edges.data, sizeof(ShadowSearchEdge), first->dyn_edges.count, first->dyn_edges.size, 1);
	fwd_edge->other_node = second;
	fwd_edge->other_direction_idx = bk_idx;
	fwd_edge->length = length;

	bk_edge = dynArrayAdd(second->dyn_edges.data, sizeof(ShadowSearchEdge), second->dyn_edges.count, second->dyn_edges.size, 1);
	bk_edge->other_node = first;
	bk_edge->other_direction_idx = fwd_idx;
	bk_edge->length = length;
}


static void buildShadowSearchGraphForLight(ShadowSearchNode* cur_node, const Vec3 cur_pos, GfxLight* light)
{
	ShadowSearchNode* new_node;
	ShadowSearchEdge* fwd_edge;
	int fwd_idx;

	if (gfx_state.debug.show_room_shadow_graph == 3)
	{
		shadow_search_data.debug_visit_count++;
		if (shadow_search_data.debug_visit_count > shadow_search_data.debug_max_visits)
			return;
	}

	if (light->shadow_search_node)
	{
		new_node = light->shadow_search_node;
		assert(new_node->light == light);
	}
	else
	{
		new_node = MP_ALLOC_MEMBER((&shadow_search_data), ShadowSearchNode);
		new_node->light = light;
		light->shadow_search_node = new_node;
	}

 	fwd_idx = cur_node->dyn_edges.count;
 	fwd_edge = dynArrayAdd(cur_node->dyn_edges.data, sizeof(ShadowSearchEdge), cur_node->dyn_edges.count, cur_node->dyn_edges.size, 1);
 	fwd_edge->other_direction_idx = -1; //the light is always a leaf node, so we never care about going back
 	fwd_edge->other_node = new_node;
 	
 	fwd_edge->length = distance3(cur_pos, light->world_query_mid);

	//The light should have no edges from it since you never need to start at the light
	assert(new_node->dyn_edges.count == 0);
	assert(new_node != cur_node);
}

static void buildShadowSearchGraphRecur(ShadowSearchNode* cur_node, Room* start_room, const Vec3 camera_pos, int recurs_left)
{
	if (recurs_left <= 0) //we need to stop looking
		return;

	if (gfx_state.debug.show_room_shadow_graph == 3)
	{
		shadow_search_data.debug_visit_count++;
		if (shadow_search_data.debug_visit_count > shadow_search_data.debug_max_visits)
			return;
	}

	FOR_EACH_IN_EARRAY(start_room->lights_in_room, GfxLight, light)
	{
		if (rdrIsShadowedLightType(light->orig_light_type))
			buildShadowSearchGraphForLight(cur_node, camera_pos, light);
	}
	FOR_EACH_END

	if (gfx_state.debug.show_room_shadow_graph == 3)
	{
		if (shadow_search_data.debug_visit_count > shadow_search_data.debug_max_visits)
			return;
	}

	FOR_EACH_IN_EARRAY(start_room->portals, RoomPortal, portal)
	{
		ShadowSearchNode* this_side;
		bool prev_visited = false;
		F32 this_side_length;

		if (!portal->neighbor)
			continue; //portal doesn't lead anywhere useful

		if (portal->gfx_search_node)
		{
			this_side = portal->gfx_search_node;
			assert(this_side->portal == portal);
			prev_visited = true;

			if (cur_node == this_side)
				continue; //we went through a portal and are trying to go back
		}
		else
		{
			this_side = MP_ALLOC_MEMBER((&shadow_search_data), ShadowSearchNode);
			this_side->portal = portal;
			portal->gfx_search_node = this_side;
		}

		this_side_length = distance3(portal->world_mid, camera_pos);
		connectShadowSearchNode(cur_node, this_side, this_side_length);


		if (!prev_visited)
		{
			//go into the adjoining room
			ShadowSearchNode* other_side;
			F32 other_side_length;
			RoomPortal* neighbor = portal->neighbor;

			assert(!neighbor->gfx_search_node);

			other_side = MP_ALLOC_MEMBER((&shadow_search_data), ShadowSearchNode);
			neighbor->gfx_search_node = other_side;
			other_side->portal = neighbor;

			other_side_length = distance3(neighbor->world_mid, portal->world_mid);
			connectShadowSearchNode(this_side, other_side, other_side_length + shadow_cross_portal_cost);

			buildShadowSearchGraphRecur(other_side, neighbor->parent_room, neighbor->world_mid, recurs_left - 1);
		}
	
	}
	FOR_EACH_END
}

void gfxInitShadowSearchData(const Vec3 camera_focal_pt, const Vec3 camera_pos, const Vec3 camera_vec)
{
	Room** new_cam_rooms = 0;
	int new_cam_rooms_size = 0;
	const WorldVolume** eaVolumes;
	Vec3 loc_cam_foc, loc_cam_pos, loc_cam_vec;
	
	PERFINFO_AUTO_START_FUNC();

	if (lock_shadow_caster_graph)
	{
		copyVec3(shadow_search_data.camera_foc, loc_cam_foc);
		copyVec3(shadow_search_data.camera_pos, loc_cam_pos);
		copyVec3(shadow_search_data.camera_vec, loc_cam_vec);
	}
	else
	{
		copyVec3(camera_focal_pt, loc_cam_foc);
		copyVec3(camera_pos, loc_cam_pos);
		copyVec3(camera_vec, loc_cam_vec);
	}

	if (gfx_state.debug.show_room_shadow_graph == 3)
	{
		int oldlock = lock_shadow_caster_graph;
		if (shadow_search_data.debug_visit_count < shadow_search_data.debug_max_visits)
		{
			shadow_search_data.debug_max_visits = 0;
		}
		shadow_search_data.debug_max_visits++;
		shadow_search_data.debug_visit_count = 0;
		lock_shadow_caster_graph = 0;
		gfxClearShadowSearchData(); //trigger full rebuild
		lock_shadow_caster_graph = oldlock;
	}

	if (!shadow_search_data.astar_data)
	{
		shadow_search_data.astar_data = createAStarSearchData();
		MP_CREATE_MEMBER((&shadow_search_data), ShadowSearchNode, 100);
	}

	eaStackCreate(&new_cam_rooms, 32); //assume the max amount of rooms you can have is 32, in sane situations it should be very low
	eaVolumes = wlVolumeCacheQuerySphereByType(lights_room_query_cache, loc_cam_pos, 0.0f, lights_room_volume_type);

	FOR_EACH_IN_EARRAY(eaVolumes, const WorldVolume, vol)
	{
		WorldVolumeEntry *volume_entry = wlVolumeGetVolumeData(vol);
		if (volume_entry && volume_entry->room)
		{
			eaPush(&new_cam_rooms, volume_entry->room);
			new_cam_rooms_size++;
		}
	}
	FOR_EACH_END

	eaQSort(new_cam_rooms, ptrCmp); //sort them so we can tell if we have the same list or not
	if (new_cam_rooms_size != eaSize(&shadow_search_data.ea_camera_rooms))
	{
		//it's a new list so clear the old data
		gfxClearShadowSearchData();
	}
	else
	{
		int i;
		for (i = 0; i < new_cam_rooms_size; ++i)
		{
			if (new_cam_rooms[i] != shadow_search_data.ea_camera_rooms[i])
			{
				//a room was different
				gfxClearShadowSearchData();
				break;
			}
		}
	}
	
	//at this point we have two options: 1) we are in the same room/rooms so just update length of the first edges 2) blow it out and make a new graph
	if (shadow_search_data.camera_node)
	{
		if (!sameVec3(loc_cam_foc, shadow_search_data.camera_foc))
		{
			//we have the old data so update the lengths of the inital edges
			int i;
			for (i = 0; i < shadow_search_data.camera_node->dyn_edges.count; ++i)
			{
				ShadowSearchEdge* edge = shadow_search_data.camera_node->dyn_edges.data + i;
				ShadowSearchNode* other_node = edge->other_node;
				Vec3 other_node_pos;

				if (other_node->light)
					copyVec3(other_node->light->world_query_mid, other_node_pos);
				else if (other_node->portal)
					copyVec3(other_node->portal->world_mid, other_node_pos);
				else
					assertmsg(0, "The node either needs to be a light or a portal");

				subVec3(other_node_pos, loc_cam_foc, other_node_pos);
				//update both directions
				edge->length = lengthVec3(other_node_pos);
				
				if (edge->other_direction_idx >= 0)
					other_node->dyn_edges.data[edge->other_direction_idx].length = edge->length;
			}

			copyVec3(loc_cam_foc, shadow_search_data.camera_foc);
		}
	}
	else
	{
		int i;
		
		copyVec3(loc_cam_foc, shadow_search_data.camera_foc);

		eaSetSize(&shadow_search_data.ea_camera_rooms, 0);
		eaPushEArray(&shadow_search_data.ea_camera_rooms, &new_cam_rooms);

		shadow_search_data.camera_node = MP_ALLOC_MEMBER((&shadow_search_data), ShadowSearchNode);
		for (i = 0; i < new_cam_rooms_size; ++i)
		{
			buildShadowSearchGraphRecur(shadow_search_data.camera_node, new_cam_rooms[i], loc_cam_pos, shadow_max_room_depth);
		}
	}

	copyVec3(camera_vec, shadow_search_data.camera_vec);
	copyVec3(loc_cam_pos, shadow_search_data.camera_pos);

	eaDestroy(&new_cam_rooms);

	PERFINFO_AUTO_STOP_FUNC();
}

static void clearVisitFlagsOnNodes(MemoryPool pool, ShadowSearchNode *data, void *userData)
{
	data->visit_flag = false;
}

void gfxClearShadowSearchDataVisitFlags()
{
	mpForEachAllocation(shadow_search_data.MP_NAME(ShadowSearchNode), clearVisitFlagsOnNodes, 0);
}

static void freeDynArraysInNodes(MemoryPool pool, ShadowSearchNode *data, void *userData)
{
	free(data->dyn_edges.data);
	data->dyn_edges.count;
	data->dyn_edges.size = 0;
	data->visit_flag = false;
	
	if (data->light)
		data->light->shadow_search_node = NULL;

	if (data->portal)
		data->portal->gfx_search_node = NULL;
}

void gfxClearShadowSearchData()
{
	PERFINFO_AUTO_START_FUNC();
	if (shadow_search_data.camera_node)
	{
		mpForEachAllocation(shadow_search_data.MP_NAME(ShadowSearchNode), freeDynArraysInNodes, 0);
		mpFreeAll(shadow_search_data.MP_NAME(ShadowSearchNode));
		shadow_search_data.camera_node = NULL;
		eaSetSize(&shadow_search_data.ea_camera_rooms, 0);
		clearAStarSearchDataTempInfo(shadow_search_data.astar_data);

		if (lock_shadow_caster_graph) //rebuild it in the same spot
			gfxInitShadowSearchData(shadow_search_data.camera_foc, shadow_search_data.camera_pos, shadow_search_data.camera_vec);
	}
	PERFINFO_AUTO_STOP_FUNC();
}

typedef struct SearchUserData
{
	F32 best_path_length;
	GfxLight* dest;
} SearchUserData;

static int shadow_NavSearchCostToTargetFunction(AStarSearchData* data, ShadowSearchNode* nodeParent, ShadowSearchNode* node, ShadowSearchEdge* connectionToNode)
{
	SearchUserData* output_ptr = (SearchUserData*)data->userData;
	F32 len;

	if (node->portal)
		len = distance3(node->portal->world_mid, output_ptr->dest->world_query_mid);
	else if (node->light)
		len = distance3(node->light->world_query_mid, output_ptr->dest->world_query_mid);
	else
		len = distance3(shadow_search_data.camera_foc, output_ptr->dest->world_query_mid);

	return (int)(len * 100);
}

static int shadow_NavSearchCostFunction(AStarSearchData* data, ShadowSearchNode* prevNode, ShadowSearchEdge* connFromPrev, ShadowSearchNode* sourceNode, ShadowSearchEdge* connection)
{
	assert(connection >= sourceNode->dyn_edges.data && connection < (sourceNode->dyn_edges.data + sourceNode->dyn_edges.count));
	return (int)(connection->length * 100);
}

static int shadow_NavSearchGetConnectionsFunction(AStarSearchData* data, ShadowSearchNode* node, ShadowSearchEdge*** connBuffer, ShadowSearchNode*** nodeBuffer, int* position,	int* count)
{
	SearchUserData* output_ptr = (SearchUserData*)data->userData;
	int max_count = *count;
	int i, cur_count = 0;

	//we should never need to pass more than 1000 connections so we don't need to support multiple calls.
	if (*position > 0)
		return 0;

	assert(node->dyn_edges.count <= max_count);

	for (i = 0; i < node->dyn_edges.count && cur_count < max_count; ++i)
	{
		ShadowSearchEdge* edge = node->dyn_edges.data + i;
		ShadowSearchNode* other_node = edge->other_node;

		//only add connections to light nodes if it's the one we're looking for since we don't want to pass through lights
		if(other_node && (!other_node->light || other_node->light == output_ptr->dest))
		{
			(*connBuffer)[cur_count] = edge;
			(*nodeBuffer)[cur_count] = other_node;
			cur_count++;
		}
	}

	*count = cur_count;
	*position = cur_count;
	return cur_count > 0 ? 1 : 0;
}

static void shadow_NavSearchOutputPath(AStarSearchData* data, AStarInfo* tailInfo)
{
	SearchUserData* output_ptr = (SearchUserData*)data->userData;
	F32 cur_cost = (F32)tailInfo->totalCost / 100.0f;
	
	if (cur_cost < output_ptr->best_path_length)
		output_ptr->best_path_length = cur_cost;
}

static NavSearchFunctions astarHookupFuncs = 
{
	(NavSearchCostToTargetFunction) shadow_NavSearchCostToTargetFunction,
	(NavSearchCostFunction) shadow_NavSearchCostFunction,
	(NavSearchGetConnectionsFunction) shadow_NavSearchGetConnectionsFunction,
	(NavSearchOutputPath) shadow_NavSearchOutputPath,
	NULL,
	NULL,
	NULL
};

F32	gfxComputeShadowSearchLightDistance(GfxLight* light)
{
	SearchUserData user_data = {FLT_MAX, light};
	F32 dist = 0;

	PERFINFO_AUTO_START_FUNC();

	if (gfx_state.debug.show_room_shadow_graph == 3) //if we're showing the half-built graph we can't query it
	{
		dist = 1;
	}
	else
	{
		if (!shadow_search_data.camera_node || !light->shadow_search_node)
			assert(0); //the shadow graph isn't setup

		clearAStarSearchDataTempInfo(shadow_search_data.astar_data);
		shadow_search_data.astar_data->sourceNode = shadow_search_data.camera_node;
		shadow_search_data.astar_data->targetNode = light->shadow_search_node;
		shadow_search_data.astar_data->userData = &user_data;
		shadow_search_data.astar_data->nodeAStarInfoOffset = offsetof(ShadowSearchNode, astar_info);
		AStarSearch(shadow_search_data.astar_data, &astarHookupFuncs);

		dist = user_data.best_path_length;
	}

	PERFINFO_AUTO_STOP_FUNC();

	return dist;
}

static void debugDrawShadowGraphGetNodePos(ShadowSearchNode* cur_node, Vec3 pos_out)
{
	if (cur_node->portal)
	{
		copyVec3(cur_node->portal->world_mid, pos_out);
	}
	else if (cur_node->light)
	{
		copyVec3(cur_node->light->world_query_mid, pos_out);
	}
	else
	{
		copyVec3(shadow_search_data.camera_foc, pos_out); 
	}
}

static void debugDrawShadowGraphDrawNode(ShadowSearchNode* cur_node)
{
	if (cur_node->portal)
	{
		Vec3 bmin, bmax;
		addVec3same(cur_node->portal->world_mid, -1, bmin);
		addVec3same(cur_node->portal->world_mid, 1, bmax);

		gfxDrawBox3D(bmin, bmax, unitmat, ColorBlue, 1);
	}
	else if (cur_node->light)
	{
		Color c;
		F32 dist;
		Vec2 screen_pos;
		bool light_casts_shadows_this_frame;

		if (gfx_state.debug.show_room_shadow_graph == 3)
			dist = 1;
		else
			dist = gfxComputeShadowSearchLightDistance(cur_node->light);

		c.r = CLAMPF(dist / 500,0,1)*255;
		c.g = 255-c.r;
		c.b = 0;
		c.a = 255;

		gfxDrawSphere3D(cur_node->light->world_query_mid, 1.0, 3, c, 1);

		light_casts_shadows_this_frame = eaSize(&cur_node->light->shadow_data_per_action) > 0 && cur_node->light->shadow_data_per_action[0]->last_update_frame == gfx_state.frame_count;
		if (light_casts_shadows_this_frame)
		{
			gfxDrawSphere3D(cur_node->light->world_query_mid, 2.0, 5, (gfx_state.frame_count % 2) ? ColorRed : ColorBlue, 1);
		}
	
		if (gfxWorldToScreenSpaceVector(gfx_state.currentCameraView, cur_node->light->world_query_mid, screen_pos, false))
		{
			char tempBuf[1024];
			GfxFont temp_font = {0};
			sprintf(tempBuf, "%.2f", (double)dist);
			gfxFontInitalizeFromData(&temp_font, g_font_Mono.fontData);
			temp_font.renderSize = 12;
			gfxFontPrint(&temp_font, screen_pos[0], screen_pos[1], GRAPHICSLIB_Z, tempBuf, NULL);
		}
	}
	else
	{
		Vec3 bmin, bmax;
		Vec3 cur_node_pos;

		copyVec3(shadow_search_data.camera_foc, cur_node_pos); 

		addVec3same(cur_node_pos, -0.05, bmin);
		addVec3same(cur_node_pos, 0.05, bmax);

		gfxDrawBox3D(bmin, bmax, unitmat, ColorRed, 1);
	}
}

static void debugDrawShadowGraph(MemoryPool pool, ShadowSearchNode *cur_node, void *userData)
{
	int i;

	if (cur_node->visit_flag)
		return;

	cur_node->visit_flag = true;

	if (cur_node->light && !(cur_node->light->orig_light_type & RDRLIGHT_SHADOWED) && gfx_state.debug.show_room_shadow_graph < 2)
		return; //only show lights with no shadows for mode 2

	debugDrawShadowGraphDrawNode(cur_node);

	//draw edges, we need to do a bredth first traversal to make this make any sense
	for (i = 0; i < cur_node->dyn_edges.count; ++i)
	{
		ShadowSearchNode* next_node = cur_node->dyn_edges.data[i].other_node;
		Vec3 cur_node_pos, next_node_pos;

		if (next_node->light && !(next_node->light->orig_light_type & RDRLIGHT_SHADOWED) && gfx_state.debug.show_room_shadow_graph < 2)
			continue; 

		debugDrawShadowGraphGetNodePos(cur_node, cur_node_pos);
		debugDrawShadowGraphGetNodePos(next_node, next_node_pos);

		gfxDrawLine3D(cur_node_pos, next_node_pos, ColorWhite);

	}
}


void gfxDebugDrawShadowGraph()
{
	if (!shadow_search_data.camera_node)
		return;

	gfxClearShadowSearchDataVisitFlags();
	gfxSetPrimZTest(0);
	mpForEachAllocation(shadow_search_data.MP_NAME(ShadowSearchNode), debugDrawShadowGraph, 0);
	gfxSetPrimZTest(1);
}

static void calcShadowLightSortParam(GfxLight *light, const Vec3 camera_pos, const Vec3 camera_vec)
{
	RdrLightType simple_light_type;
	RdrLight *rlight = &light->rdr_light;
	F32 dist;

	PERFINFO_AUTO_START_FUNC_L2();

	light->nondeferred_sort_param = ABS(light->light_colors.hsv_ambient[2]) + ABS(light->light_colors.hsv_diffuse[2]) * (1 + light->light_colors.hsv_diffuse[1]);
	setRdrLightType(light);
	simple_light_type = rdrGetSimpleLightType(light->orig_light_type);

	if (shadow_search_data.camera_node && light->shadow_search_node && simple_light_type != RDRLIGHT_DIRECTIONAL)
	{
		dist = gfxComputeShadowSearchLightDistance(light);
	}
	else if (simple_light_type != RDRLIGHT_DIRECTIONAL)
	{
		if (shadow_search_data.camera_node)
		{
			dist = distance3(camera_pos, light->world_query_mid); //if we have the graph then obvously we dont want this light
			light->nondeferred_sort_param /= 2;
		}
		else
		{
			dist = distance3(camera_pos, light->world_query_mid);
		}
	}
	else
	{
		dist = 0;
	}

	if (simple_light_type == RDRLIGHT_DIRECTIONAL)
	{
		light->nondeferred_sort_param *= 1e5;
	}
	else
	{
		// attenuate
		if (rlight->point_spot_params.outer_radius)
		{
			Vec3 light_vec;
			F32 r, straight_dist;
			F32 region;

			subVec3(light->world_query_mid, camera_pos, light_vec);
			straight_dist = normalVec3(light_vec);

			region = rdrGetSimpleLightType(rlight->light_type) == RDRLIGHT_PROJECTOR ? calcProjectorRgn(light, camera_pos) : calcPointSpotRgn(light, camera_pos, dist);
			if (region > 0.05f)
			{
				r = 3.0f * MAXF(region,0.05f);
			}
			else
			{
				r = 1.5f / (1.0f + dist);
			}
			light->nondeferred_sort_param *= r * ((log2(round(rlight->point_spot_params.outer_radius))+1)*0.5+0.5);
			if (region <= 0.05f && !light->indoors)
			{
				//we only want to do this if we aren't inside the light area
				//and not inside. doing this with coridoors and stuff doesn't look too good.
				light->nondeferred_sort_param *= (-dotVec3(camera_vec, light_vec) + 2) * 0.5f; 
			}
			if (light->dynamic)
				light->nondeferred_sort_param *= 0.5f;
		}
		else
		{
			light->nondeferred_sort_param = 0;
		}
	}

	MAX1F(light->nondeferred_sort_param, 0);
	light->debug_shadow_sort_param = light->nondeferred_sort_param;
	PERFINFO_AUTO_STOP_L2();
}

static void calcShadowLightSortParam_OldVersion(GfxLight *light, const Vec3 camera_pos, const Vec3 camera_vec)
{
	// figure out light's contribution to the object, weighting colored lights heavier
	RdrLightType simple_light_type;
	RdrLight *rlight = &light->rdr_light;

	PERFINFO_AUTO_START_FUNC_L2();

	light->nondeferred_sort_param = ABS(light->light_colors.hsv_ambient[2]) + ABS(light->light_colors.hsv_diffuse[2]) * (1 + light->light_colors.hsv_diffuse[1]);
	setRdrLightType(light);
	simple_light_type = rdrGetSimpleLightType(light->orig_light_type);
	if (simple_light_type == RDRLIGHT_DIRECTIONAL)
	{
		light->nondeferred_sort_param *= 1e5;
	}
	else
	{
		// attenuate
		if (rlight->point_spot_params.outer_radius)
		{
			Vec3 light_vec;
			F32 r;

			subVec3(rlight->world_mat[3], camera_pos, light_vec);
			r = normalVec3(light_vec);

			if (r < rlight->point_spot_params.inner_radius)
			{
				r = 2;
			}
			else if (r < rlight->point_spot_params.outer_radius)
			{
				r = 2 - saturate((r - rlight->point_spot_params.inner_radius) / (rlight->point_spot_params.outer_radius - rlight->point_spot_params.inner_radius));
			}
			else
			{
				r = 1 / (1 + r - rlight->point_spot_params.outer_radius);
			}
			light->nondeferred_sort_param *= r * (log2(round(rlight->point_spot_params.outer_radius))+1);
			light->nondeferred_sort_param *= (-dotVec3(camera_vec, light_vec) + 2) * 0.5f;
			if (light->dynamic)
				light->nondeferred_sort_param *= 0.5f;
		}
		else
		{
			light->nondeferred_sort_param = 0;
		}
	}

	light->debug_shadow_sort_param = light->nondeferred_sort_param;
	PERFINFO_AUTO_STOP_L2();
}


__forceinline static int cmpLightsShadowedNonDeferred(const GfxLight **light1, const GfxLight **light2)
{
	// shadowmaps being on or in transition is highest priority
	int shadow_trans2 = !!*(int*)&(*light2)->shadow_transition;
	int shadow_trans1 = !!*(int*)&(*light1)->shadow_transition;
	if (shadow_trans2 != shadow_trans1)
		return shadow_trans2 - shadow_trans1;

	// DJR much faster to use integer comparison of floats, when the values are
	// still in memory, and we know both are positive or negative
	return *(int*)&(*light2)->nondeferred_sort_param - *(int*)&(*light1)->nondeferred_sort_param;
}

__forceinline static void sortShadowLights(GfxLight **lights, const Vec3 camera_pos, const Vec3 camera_vec)
{
	int i, j;

	PERFINFO_AUTO_START_FUNC_L2();

	// sort
	if (use_new_light_selector)
	{
		for (i = 0; i < eaSize(&lights); ++i)
			calcShadowLightSortParam(lights[i], camera_pos, camera_vec);
	}
	else
	{
		for (i = 0; i < eaSize(&lights); ++i)
			calcShadowLightSortParam_OldVersion(lights[i], camera_pos, camera_vec);
	}

	for (i = 0; i < eaSize(&lights); ++i)
	{
		for (j = i+1; j < eaSize(&lights); ++j)
		{
			if (cmpLightsShadowedNonDeferred(&(lights[i]), &(lights[j])) > 0)
				SWAPP(lights[i], lights[j]);
		}
	}

	PERFINFO_AUTO_STOP_L2();
}

void gfxSortShadowcasters(const Frustum *camera_frustum, const Vec3 camera_focal_pt)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);

	if (!gdraw)
		return;

	if (!disable_shadow_caster_graph)
	{
		gfxInitShadowSearchData(camera_focal_pt, camera_frustum->cammat[3], camera_frustum->cammat[2]);
		sortShadowLights(gdraw->this_frame_shadowmap_lights, shadow_search_data.camera_foc, shadow_search_data.camera_vec);
	}
	else
	{
		gfxClearShadowSearchData();
		if (use_new_light_selector)
		{
			//use focal point
			sortShadowLights(gdraw->this_frame_shadowmap_lights, camera_focal_pt, camera_frustum->cammat[2]);
		}
		else
		{
			//use camera position
			sortShadowLights(gdraw->this_frame_shadowmap_lights, camera_frustum->cammat[3], camera_frustum->cammat[2]);
		}
	}
}
