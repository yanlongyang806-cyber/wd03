#define GENESIS_ALLOW_OLD_HEADERS
#include "wlGenesisExterior.h"

#include "FolderCache.h"
#include "ResourceInfo.h"
#include "ScratchStack.h"
#include "WorldGridPrivate.h"
#include "error.h"
#include "file.h"
#include "fileutil.h"
#include "gimmeDLLWrapper.h"
#include "rand.h"
#include "tga.h"
#include "LineDist.h"
#include "wlCurve.h"
#include "wlGenesis.h"
#include "wlGenesisExteriorDesign.h"
#include "wlGenesisExteriorNode.h"
#include "wlGenesisRoom.h"
#include "wlState.h"
#include "wlTerrainSource.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#ifndef NO_EDITORS

#define NODE_RES 16.0f
#define GENESIS_PATH_MAX_DELTA 0.13f

static int enable_debug_images = 0;
#define DEBUG_SHIFT 3

#define PLACE_NODE_PARAMS GenesisNode *placing_node, GenesisNodeConstraint *constraint, MersenneTable *random_table, F32 *min_height, F32 *max_height, bool *height_constraint
typedef bool (*genesisExteriorPlaceNodeFunc)(PLACE_NODE_PARAMS);

static int genesis_exterior_resolution = 2;

AUTO_COMMAND;
void genesisExteriorSetResolution(int res)
{
	genesis_exterior_resolution = res;
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.genesisEnableRoomDebugImages");
void genesisExteriorEnableRoomDebugImages(int debug)
{
	enable_debug_images = debug;
}

typedef struct GenesisExteriorData
{
	F32 water_offset;
	Vec2 mountain_range;
	Vec2 chasm_range;
	Vec2 playable_range;
	GenesisGeotype *geotype_filled;
} GenesisExteriorData;

//////////////////////////////////////////////////////////////////
// Geotype Library
//////////////////////////////////////////////////////////////////

static DictionaryHandle genesis_geotype_dict = NULL;
static genesis_geotype_dict_loaded = false;

void genesisReloadGeotype(const char* relpath, int when)
{
	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, RefSystem_GetDictionaryHandleFromNameOrHandle(GENESIS_GEOTYPE_DICTIONARY) );
}

//Fills in the filename_no_path of the Geotype during load
AUTO_FIXUPFUNC;
TextParserResult genesisFixupGeotype(GenesisGeotype *geotype, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			char name[256];
			if (geotype->name)
				StructFreeString(geotype->name);
			getFileNameNoExt(name, geotype->filename);
			geotype->name = StructAllocString(name);
		}
	}
	return 1;
}

AUTO_RUN;
void genesisInitGeotypeLibrary(void)
{
	genesis_geotype_dict = RefSystem_RegisterSelfDefiningDictionary(GENESIS_GEOTYPE_DICTIONARY, false, parse_GenesisGeotype, true, false, NULL);
	resDictMaintainInfoIndex(genesis_geotype_dict, NULL, NULL, ".Tags", NULL, NULL);
}

void genesisLoadGeotypeLibrary()
{
	if (!areEditorsPossible() || genesis_geotype_dict_loaded)
		return;

	resLoadResourcesFromDisk(genesis_geotype_dict, "genesis/geotypes", ".geotype", "GenesisGeotypes.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "genesis/geotypes/*.geotype", genesisReloadGeotype);

	genesis_geotype_dict_loaded = true;
}

//////////////////////////////////////////////////////////////////
// Utility Functions
//////////////////////////////////////////////////////////////////

static bool genesisExteriorIsParent(GenesisNode *child, GenesisNode *parent, int depth)
{
	int i;
	assert(depth < 100);
	if(parent->priority > child->priority)
		return true;
	if(eaSize(&child->parents) == 0)
		return false;
	for( i=0; i < eaSize(&child->parents); i++ )
	{
		if(parent == child->parents[i])
			return true;
		else if(genesisExteriorIsParent(child->parents[i], parent, depth+1))
			return true;
	}
	return false;
}

static int genesisExteriorGetParamIndex(GenesisConstraintParam **params, const char *name)
{
	/*int i;
	for( i=0; i < eaSize(&params) ; i++ )
	{
		if(stricmp(name, params[i]->name) == 0)
			return i;
	}*/
	return -1;
}

static int genesisExteriorGetRoomIndex(GenesisZoneMapRoom **rooms, const char *name)
{
	int i;
	for( i=0; i < eaSize(&rooms) ; i++ )
	{
		if(stricmp(name, rooms[i]->room.name) == 0)
			return i;
	}
	return -1;
}

//////////////////////////////////////////////////////////////////
// Constraint builders
//////////////////////////////////////////////////////////////////

static void genesisExteriorMakeBasicConstraint(GenesisParamValues *values, GenesisConstraintFuncName function)
{
	GenesisNodeConstraint *new_constraint = StructCreate(parse_GenesisNodeConstraint);
	new_constraint->function = function;
	memcpy(&new_constraint->values, values, sizeof(GenesisParamValues));
	assert(values->end != values->start);
	if (genesisExteriorIsParent(values->end, values->start, 0))
	{
		eaPush(&values->end->final_constraints, new_constraint);
	}
	else
	{
		eaPush(&values->start->final_constraints, new_constraint);
		eaPushUnique(&values->start->parents, values->end);
	}
}

static bool genesisExteriorMake3WayConstraint(GenesisParamValues *values, GenesisConstraintFuncName function)
{
	GenesisNodeConstraint *new_constraint = StructCreate(parse_GenesisNodeConstraint);
	memcpy(&new_constraint->values, values, sizeof(GenesisParamValues));
	new_constraint->function = function;

	assert(values->start != values->mid &&
		values->mid != values->end &&
		values->start != values->end);

	// If I am not their parents then they can be mine
	if (!genesisExteriorIsParent(values->start, values->mid, 0) && !genesisExteriorIsParent(values->end, values->mid, 0))
	{
		eaPush(&values->mid->final_constraints, new_constraint);
		eaPushUnique(&values->mid->parents, values->start);
		eaPushUnique(&values->mid->parents, values->end);
	}
	else if(!genesisExteriorIsParent(values->mid, values->start, 0) && !genesisExteriorIsParent(values->end, values->start, 0))
	{
		eaPush(&values->start->final_constraints, new_constraint);
		eaPushUnique(&values->start->parents, values->mid);
		eaPushUnique(&values->start->parents, values->end);
	}
	else if(!genesisExteriorIsParent(values->mid, values->end, 0) && !genesisExteriorIsParent(values->start, values->end, 0))
	{
		eaPush(&values->end->final_constraints, new_constraint);
		eaPushUnique(&values->end->parents, values->mid);
		eaPushUnique(&values->end->parents, values->start);
	}
	else
	{
		assert(false);
		return false;
	}
	return true;
}

static bool genesisExteriorMake4DirConstraint(GenesisParamValues *values, GenesisConstraintFuncName function)
{
	GenesisNodeConstraint *new_constraint = StructCreate(parse_GenesisNodeConstraint);
	memcpy(&new_constraint->values, values, sizeof(GenesisParamValues));
	new_constraint->function = function;

	assert(	values->start != values->end &&
		values->start != values->side1 &&
		values->start != values->side2 &&
		values->side1 != values->side2 &&
		values->side1 != values->end &&
		values->side2 != values->end);

	if(	!genesisExteriorIsParent(values->side1, values->start, 0) && 
		!genesisExteriorIsParent(values->side2, values->start, 0) && 
		!genesisExteriorIsParent(values->end, values->start, 0) )
	{
		eaPush(&values->start->final_constraints, new_constraint);
		eaPushUnique(&values->start->parents, values->side1);
		eaPushUnique(&values->start->parents, values->side2);
		eaPushUnique(&values->start->parents, values->end);
	}
	else if(!genesisExteriorIsParent(values->start, values->side1, 0) && 
			!genesisExteriorIsParent(values->side2, values->side1, 0) && 
			!genesisExteriorIsParent(values->end, values->side1, 0) )
	{
		eaPush(&values->side1->final_constraints, new_constraint);
		eaPushUnique(&values->side1->parents, values->start);
		eaPushUnique(&values->side1->parents, values->side2);
		eaPushUnique(&values->side1->parents, values->end);
	}
	else if(!genesisExteriorIsParent(values->start, values->side2, 0) && 
			!genesisExteriorIsParent(values->side1, values->side2, 0) && 
			!genesisExteriorIsParent(values->end, values->side2, 0) )
	{
		eaPush(&values->side2->final_constraints, new_constraint);
		eaPushUnique(&values->side2->parents, values->start);
		eaPushUnique(&values->side2->parents, values->side1);
		eaPushUnique(&values->side2->parents, values->end);
	}
	else if(!genesisExteriorIsParent(values->start, values->end, 0) && 
			!genesisExteriorIsParent(values->side1, values->end, 0) && 
			!genesisExteriorIsParent(values->side2, values->end, 0) )
	{
		eaPush(&values->end->final_constraints, new_constraint);
		eaPushUnique(&values->end->parents, values->start);
		eaPushUnique(&values->end->parents, values->side1);
		eaPushUnique(&values->end->parents, values->side2);
	}
	else
	{
		assert(false);
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////
// Constraint convert
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
// Constraint expander functions
//////////////////////////////////////////////////////////////////


static void genesisExteriorMakePathConstraints(	GenesisNodeConnection *connection, 
												GenesisZoneExterior *layout, GenesisZoneNodeLayout *node_layout,
												GenesisNodeConstraint ***constraint_list, F32 max_road_angle )
{
	F32 min_dist = 100;
	F32 max_dist = 800;
	GenesisNodeConstraint *new_constraint;
	F32 min_length = connection->min_length;
	F32 max_length = connection->max_length;
	GenesisNode *start_node;
	GenesisNode *end_node;

	assert(	(int)connection->start_uid < eaSize(&node_layout->nodes) && 
			(int)connection->end_uid < eaSize(&node_layout->nodes) );
	start_node = node_layout->nodes[connection->start_uid];
	end_node = node_layout->nodes[connection->end_uid];

	//Connected Constraint
	new_constraint = StructCreate(parse_GenesisNodeConstraint);
	new_constraint->function = GCF_Connected;
	//new_constraint->validated = true;
	new_constraint->values.start = start_node;
	new_constraint->values.end = end_node;
	eaPush(constraint_list, new_constraint);

	//Vertical Angle Constraint
	new_constraint = StructCreate(parse_GenesisNodeConstraint);
	new_constraint->function = GCF_MaxVertAngleDiff;
	//new_constraint->validated = true;
	new_constraint->values.angle = max_road_angle;
	new_constraint->values.start = start_node;
	new_constraint->values.end = end_node;
	eaPush(constraint_list, new_constraint);

	if(layout->vert_dir == 	GENESIS_PATH_UPHILL || layout->vert_dir == GENESIS_PATH_DOWNHILL)
	{
		new_constraint = StructCreate(parse_GenesisNodeConstraint);
		new_constraint->function = GCF_Uphill;
		new_constraint->values.invert = (layout->vert_dir == GENESIS_PATH_DOWNHILL);
		//new_constraint->validated = true;
		new_constraint->values.start = start_node;
		new_constraint->values.end = end_node;
		eaPush(constraint_list, new_constraint);
	}

	//Time
	if (min_length > 0)
	{
		min_dist = min_length;
	}
	if (max_length > 0)
	{
		max_dist = max_length;
	}
	//Min Dist Constraint
	new_constraint = StructCreate(parse_GenesisNodeConstraint);
	new_constraint->function = GCF_MinDist;
	//new_constraint->validated = true;
	new_constraint->values.invert = false;
	new_constraint->values.dist = min_dist+start_node->actual_size+end_node->actual_size;
	new_constraint->values.start = start_node;
	new_constraint->values.end = end_node;
	eaPush(constraint_list, new_constraint);
	//Max Dist Constraint
	new_constraint = StructCreate(parse_GenesisNodeConstraint);
	new_constraint->function = GCF_MaxDist;
	//new_constraint->validated = true;
	new_constraint->values.invert = true;
	new_constraint->values.dist = max_dist+start_node->actual_size+end_node->actual_size;
	new_constraint->values.start = start_node;
	new_constraint->values.end = end_node;
	eaPush(constraint_list, new_constraint);
}


static bool genesisExteriorConvertPathList(GenesisZoneExterior *layout,
										   GenesisZoneNodeLayout *node_layout,
										   GenesisNodeConstraint ***constraint_list, F32 max_road_angle,
										   bool run_silent, MersenneTable *random_table)
{
	int i, j, k;
	char buf[256];
	int start_idx, end_idx;
	for ( j=0; j < eaSize(&layout->paths); j++ )
	{
		GenesisZoneMapPath *src_path = layout->paths[j];
		GenesisRoomMission **missions = src_path->missions;
		GenesisNodeConnectionGroup *new_hallway_group = StructCreate(parse_GenesisNodeConnectionGroup);
		GenesisNodeConnection *last_hallway = NULL;
		GenesisNodeConnection *new_hallway = NULL;
		F32 width = src_path->path.width;
		int start_cnt = eaSize(&src_path->start_rooms);
		int end_cnt = eaSize(&src_path->end_rooms);
		int start_end_cnt = start_cnt + end_cnt;
		int junction_cnt = start_end_cnt - 2;
		int hallway_cnt = junction_cnt*2 + 1;
		int min_length_inc=0, max_length_inc=0;
		int min_length_cur=0, max_length_cur=0;
		int min_length_end=0, max_length_end=0;
		int *rand_idx_list;
		int list_idx = 0; 

		eaPush(&node_layout->connection_groups, new_hallway_group);
		new_hallway_group->name = StructAllocString(src_path->path.name);
		new_hallway_group->source_context = genesisMakeErrorContextPath( src_path->path.name, node_layout->layout_name );
		new_hallway_group->side_trail = src_path->side_trail;

		if(eaSize(&src_path->start_rooms) <= 0 || eaSize(&src_path->end_rooms) <= 0)
		{
			genesisRaiseError(GENESIS_FATAL_ERROR, new_hallway_group->source_context, 
				"Path must have at least one start and one exit room");
			return false;
		}
		assert(junction_cnt >= 0);

		rand_idx_list = ScratchAlloc(sizeof(int)*hallway_cnt);
		for ( i=0; i < hallway_cnt; i++ )
			rand_idx_list[i] = i;
		randomMersennePermuteN(random_table, rand_idx_list, hallway_cnt, 1);

		for (i = 0; i < eaSize(&missions); i++)
		{
			GenesisRoomMission *src_mission = missions[i];
			GenesisNodeMission *mission = StructCreate(parse_GenesisNodeMission);
			mission->mission_name = StructAllocString(src_mission->mission_name);
			eaPush(&new_hallway_group->missions, mission);

			for (k = 0; k < eaSize(&src_mission->objects); k++)
			{
				GenesisNodeObject *object = StructCreate(parse_GenesisNodeObject);
				object->path_idx = rand_idx_list[list_idx];
				list_idx++;
				if(list_idx >= hallway_cnt)
				{
					randomMersennePermuteN(random_table, rand_idx_list, hallway_cnt, 1);
					list_idx = 0;
				}
				object->actual_size = width*2;
				object->draw_size = width*2;
				object->offset = (F32)(k+1)/(eaSize(&src_mission->objects)+1);
				object->object = StructClone(parse_GenesisObject, src_mission->objects[k]);
				eaPush(&mission->objects, object);
			}
			for (k = 0; k < eaSize(&src_mission->patrols); k++)
			{
				GenesisPatrolObject *patrol = src_mission->patrols[k];
				GenesisNodePatrol *new_patrol = StructCreate(parse_GenesisNodePatrol);
				int objIt;
				
				for (objIt = 0; objIt < eaSize(&mission->objects); objIt++)
				{
					GenesisNodeObject* obj = mission->objects[objIt];
					if(  patrol->owner_challenge->challenge_id == obj->object->challenge_id
						 && stricmp(patrol->owner_challenge->challenge_name, obj->object->challenge_name) == 0) {
						new_patrol->path_idx = obj->path_idx;
						break;
					}
				}
				new_patrol->patrol = StructClone(parse_GenesisPatrolObject, patrol);
				eaPush(&mission->patrols, new_patrol);
			}
		}
		ScratchFree(rand_idx_list);

		min_length_inc = (src_path->path.min_length / (F32)(junction_cnt+1)) + 0.5f;
		max_length_inc = (src_path->path.max_length / (F32)(junction_cnt+1)) + 0.5f;
		min_length_cur = min_length_inc;
		max_length_cur = max_length_inc;
		min_length_end = min_length_inc*(junction_cnt+1);
		max_length_end = max_length_inc*(junction_cnt+1);

		//Make the first hallway and set it's start room to the first start room
		new_hallway = StructAlloc(parse_GenesisNodeConnection);
		eaPush(&new_hallway_group->connections, new_hallway);
		new_hallway->radius = width/2.0f;
		new_hallway->path = StructCreate(parse_Spline);
		new_hallway->min_length = min_length_end;
		new_hallway->max_length = max_length_end;
		start_idx = genesisExteriorGetRoomIndex(layout->rooms, src_path->start_rooms[0]);
		new_hallway->start_uid = start_idx;
		if (start_idx < 0)
		{
			if(!run_silent) {
				genesisRaiseError(GENESIS_FATAL_ERROR, new_hallway_group->source_context, 
					"Path references start room \"%s\", but no such room exists.", src_path->start_rooms[0]);
			}
			return false;
		}

		last_hallway = new_hallway;

		for ( i=0; i < junction_cnt; i++, min_length_cur += min_length_inc, max_length_cur += max_length_inc)
		{
			//Make a new junction as a room
			bool start_room = (i+1) < start_cnt;
			char *room_name = (start_room ? src_path->start_rooms[i+1] : src_path->end_rooms[(i+1)-start_cnt]);
			GenesisNode *new_junction = StructCreate(parse_GenesisNode);
			int new_junction_idx;
			int room_idx;
			sprintf(buf, "%s_junc_%02d", src_path->path.name, i);
 			new_junction->name = StructAllocString(buf);
			new_junction->node_type = GENESIS_NODE_Clearing;
			new_junction->do_not_pop = true;
			new_junction_idx = eaPush(&node_layout->nodes, new_junction);
			new_junction->uid = new_junction_idx;

			//Attach the last hall
			last_hallway->end_uid = new_junction_idx;
			last_hallway->min_length = min_length_inc;//override the length
			last_hallway->max_length = max_length_inc;
			genesisExteriorMakePathConstraints(last_hallway, layout, node_layout, constraint_list, max_road_angle);

			//Make a hall for the current room
			room_idx = genesisExteriorGetRoomIndex(layout->rooms, room_name);
			if (room_idx < 0)
			{
				genesisRaiseError(GENESIS_FATAL_ERROR, new_hallway_group->source_context, 
					"Could not find %s room %s.", (start_room ? "start" : "end"), room_name);
				return false;
			}
			new_hallway = StructAlloc(parse_GenesisNodeConnection);
			eaPush(&new_hallway_group->connections, new_hallway);
			new_hallway->radius = width/2.0f;
			new_hallway->path = StructCreate(parse_Spline);
			if(start_room)
			{
				new_hallway->start_uid = room_idx;
				new_hallway->end_uid = new_junction_idx;
				new_hallway->min_length = min_length_cur;
				new_hallway->max_length = max_length_cur;
			}
			else
			{
				new_hallway->start_uid = new_junction_idx;
				new_hallway->end_uid = room_idx;			
				new_hallway->min_length = min_length_end - min_length_cur;
				new_hallway->max_length = max_length_end - max_length_cur;
			}
			genesisExteriorMakePathConstraints(new_hallway, layout, node_layout, constraint_list, max_road_angle);

			//Make a hall to the next junction
			new_hallway = StructAlloc(parse_GenesisNodeConnection);
			eaPush(&new_hallway_group->connections, new_hallway);
			new_hallway->radius = width/2.0f;
			new_hallway->path = StructCreate(parse_Spline);
			new_hallway->start_uid = new_junction_idx;
			new_hallway->min_length = min_length_end - min_length_cur;
			new_hallway->max_length = max_length_end - max_length_cur;

			last_hallway = new_hallway;
		}

		end_idx = genesisExteriorGetRoomIndex(layout->rooms, src_path->end_rooms[end_cnt-1]);
		new_hallway->end_uid = end_idx;
		if (end_idx < 0)
		{
			if(!run_silent) {
				genesisRaiseError(GENESIS_FATAL_ERROR, new_hallway_group->source_context, 
					"Path references start room \"%s\", but no such room exists.", src_path->end_rooms[end_cnt-1]);
			}
			return false;
		}
		genesisExteriorMakePathConstraints(new_hallway, layout, node_layout, constraint_list, max_road_angle);
	}
	return true;
}

static void genesisExteriorGetTree(GenesisNode *start, GenesisNodeConstraint **constraint_list, GenesisNode ***node_list)
{
	int i,j;
	int search_idx = 0;
	start->prev_node = NULL;
	eaPush(node_list, start);
	while (search_idx < eaSize(node_list))
	{
		for( i=0; i < eaSize(&constraint_list); i++ )
		{
			GenesisNode *to_node = NULL;
			if(constraint_list[i]->function != GCF_Connected)
				continue;
			if((*node_list)[search_idx] == constraint_list[i]->values.start)
				to_node = constraint_list[i]->values.end;
			else if((*node_list)[search_idx] == constraint_list[i]->values.end)
				to_node = constraint_list[i]->values.start;
			if(to_node)
			{
				bool dup = false;
				for( j=0; j < eaSize(node_list); j++ )
				{
					if(to_node == (*node_list)[j])
					{
						dup = true;
						break;
					}
				}
				if(!dup)
					eaPush(node_list, to_node);
			}
		}
		search_idx++;
	}
	eaReverse(node_list);
}

static bool genesisExteriorFindPathFromStartToEnd(GenesisNode *start, GenesisNode *end, GenesisNodeConstraint **constraint_list, GenesisNode ***node_list)
{
	int i,j;
	int search_idx = 0;
	bool found_path = false;
	GenesisNode *node;
	GenesisNode **visit_list=NULL;
	start->prev_node = NULL;
	eaPush(&visit_list, start);
	while (search_idx < eaSize(&visit_list) && !found_path)
	{
		for( i=0; i < eaSize(&constraint_list); i++ )
		{
			GenesisNode *to_node = NULL;
			if(constraint_list[i]->function != GCF_Connected)
				continue;
			if(visit_list[search_idx] == constraint_list[i]->values.start)
				to_node = constraint_list[i]->values.end;
			else if(visit_list[search_idx] == constraint_list[i]->values.end)
				to_node = constraint_list[i]->values.start;
			if(to_node)
			{
				bool dup = false;
				if(to_node == end)
				{
					end->prev_node = visit_list[search_idx];
					found_path = true;
					break;
				}
				for( j=0; j < eaSize(&visit_list); j++ )
				{
					if(to_node == visit_list[j])
					{
						dup = true;
						break;
					}
				}
				if(!dup)
				{
					to_node->prev_node = visit_list[search_idx];
					eaPush(&visit_list, to_node);
				}
			}
		}
		search_idx++;
	}
	eaDestroy(&visit_list);
	if(!found_path)
		return false;
	for(node = end ; node ; node = node->prev_node)
		eaPush(node_list, node);
	return true;
}

static bool genesisExteriorApplyPriorities(GenesisZoneExterior *layout,
									  GenesisZoneNodeLayout *node_layout,
									  GenesisNodeConstraint ***constraint_list, bool run_silent)
{
	GenesisNode **node_list=NULL;
	int i;
	int idx_start=-1, idx_end=-1;
	if(layout->start_room)
		idx_start = genesisExteriorGetRoomIndex(layout->rooms, layout->start_room);
	if(layout->end_room)
		idx_end = genesisExteriorGetRoomIndex(layout->rooms, layout->end_room);
	if(layout->shape == GENESIS_EXT_LINEAR)
	{
		if (idx_start < 0)
		{
			if(!run_silent)
				genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(layout->layout_name), "Start room \"%s\" does not exist.", layout->start_room);
			return false;
		}
		if (idx_end < 0)
		{
			if(!run_silent)
				genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(layout->layout_name), "End room \"%s\" does not exist.", layout->end_room);
			return false;
		}
		if (idx_start == idx_end)
		{
			if(!run_silent)
				genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(layout->layout_name), "Shapes require start room and end room to be different, but they are both \"%s\".", layout->start_room);
			return false;
		}
		assert(MAX(idx_start, idx_end) < eaSize(&node_layout->nodes));

		if(!genesisExteriorFindPathFromStartToEnd(node_layout->nodes[idx_start], node_layout->nodes[idx_end], *constraint_list, &node_list))
		{
			if(!run_silent)
				genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(layout->layout_name), "Could not find path from start to end.");
			return false;
		}

		for( i=0; i < eaSize(&node_list); i++ )
		{
			node_list[i]->priority = i+501;
			if(i > 0 && i < eaSize(&node_list)-1)
			{
				GenesisNodeConstraint *new_constraint;
				new_constraint = StructCreate(parse_GenesisNodeConstraint);
				new_constraint->function = GCF_IsBetween;
				new_constraint->values.start = node_list[i-1];
				new_constraint->values.mid = node_list[i];
				new_constraint->values.end = node_list[i+1];
				eaPush(constraint_list, new_constraint);
			}
		}
		eaDestroy(&node_list);
	}
	if(idx_start >= 0)
	{
		genesisExteriorGetTree(node_layout->nodes[idx_start], *constraint_list, &node_list);
		if(!run_silent && eaSize(&node_list) > 500)
			genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(layout->layout_name), "Genesis does not currently support more than 500 rooms.");
		for( i=0; i < eaSize(&node_list); i++ )
		{
			//Set the priority if it was not already set.
			if(node_list[i]->priority == 0)
				node_list[i]->priority = i+1;
		}
		eaDestroy(&node_list);
	}
	return true;
}

/*
static bool genesisExteriorValidateConnection(GenesisNodeConstraint *constraint, GenesisZoneMapRoom *room, GenesisNode *node, GenesisZoneExterior *layout, GenesisZoneNodeLayout *node_layout, bool run_silent)
{
	int idx;
	GenesisNodeConnection *new_connection;
	if((idx = genesisExteriorGetParamIndex(constraint->params, "Target")) == -1)
	{
		if(!run_silent)
			Alertf("No target specified for a connection in '%s'", room->room.name);
		return false;
	}
	if((idx = genesisExteriorGetRoomIndex(layout->rooms, constraint->params[idx]->value)) == -1)
	{
		if(!run_silent)
			Alertf("Cannot find target for a connection in '%s'", room->room.name);
		return false;
	}
	assert(eaSize(&node_layout->nodes) > idx);
	constraint->values.ref_1 = node_layout->nodes[idx];
	constraint->validated = true;

	new_connection = StructCreate(parse_GenesisNodeConnection);
	new_connection->start_uid = node->uid;
	new_connection->end_uid = node_layout->nodes[idx]->uid;
	eaPush(&node_layout->connections, new_connection);
	return true;
}*/

static bool genesisExteriorExpandConnections(GenesisNodeConstraint ** in_constraints, 
											 GenesisZoneNodeLayout *node_layout,
											 bool run_silent)
{
	int i, j;

	for (i = 0; i < eaSize(&in_constraints); i++)
	{
		GenesisNodeConstraint *constraint = in_constraints[i];
		if (constraint->function != GCF_Connected)
			continue;

		for (j = 0; j < eaSize(&node_layout->nodes); ++j)
		{
			if (node_layout->nodes[j] != constraint->values.start &&
				node_layout->nodes[j] != constraint->values.end)
			{
				GenesisParamValues temp_values={0};
				temp_values.invert = true;
				temp_values.mid = node_layout->nodes[j];
				temp_values.start = constraint->values.start;
				temp_values.end = constraint->values.end;

				//Ensure Not Between
				if (!genesisExteriorMake3WayConstraint(&temp_values, GCF_IsBetween))
					return false;
			}
		}

		for (j = 0; j < eaSize(&in_constraints); ++j)
		{
			if (j != i)
			{
				GenesisNodeConstraint *constraint2 = in_constraints[j];
				if (constraint2->function == GCF_Connected &&
					constraint->values.start != constraint2->values.start &&
					constraint->values.start != constraint2->values.end &&
					constraint->values.end != constraint2->values.start &&
					constraint->values.end != constraint2->values.end)
				{
					GenesisParamValues temp_values={0};
					temp_values.invert = true;
					temp_values.start = constraint->values.start;
					temp_values.end = constraint->values.end;
					temp_values.side1 = constraint2->values.start;
					temp_values.side2 = constraint2->values.end;

					if (!genesisExteriorMake4DirConstraint(&temp_values, GCF_OtherSideOf))
						return false;
				}
			}
		}
	}
	return true;
}

static bool genesisExteriorExpandVertAngle(GenesisNodeConstraint **in_constraints, GenesisZoneNodeLayout *node_layout, bool run_silent)
{
	int i;
	for( i=0; i < eaSize(&in_constraints); i++ )
	{
		GenesisNodeConstraint *constraint = in_constraints[i];
		if(constraint->function != GCF_MaxVertAngleDiff)
			continue;

// 			if(!constraint->validated)
// 			{
// 				//Angle
// 				if((idx = genesisExteriorGetParamIndex(constraint->params, "Angle")) == -1)
// 				{
// 					if(!run_silent)
// 						Alertf("No angle specified for a max vertical angle in '%s'", room->room.name);
// 					return false;
// 				}
// 				constraint->values.float_1 = atof(constraint->params[idx]->value);
// 
// 				//Target
// 				if((idx = genesisExteriorGetParamIndex(constraint->params, "Target")) == -1)
// 				{
// 					if(!run_silent)
// 						Alertf("No target specified for a max vertical angle in '%s'", room->room.name);
// 					return false;
// 				}
// 				if((idx = genesisExteriorGetRoomIndex(layout->rooms, constraint->params[idx]->value)) == -1)
// 				{
// 					if(!run_silent)
// 						Alertf("Cannot find target for a max vertical angle in '%s'", room->room.name);
// 					return false;
// 				}
// 				assert(eaSize(&node_layout->nodes) > idx);
// 				constraint->values.ref_1 = node_layout->nodes[idx];
// 				constraint->validated = true;
// 			}

		genesisExteriorMakeBasicConstraint(&constraint->values, GCF_MaxVertAngleDiff);
	}
	return true;
}

static bool genesisExteriorExpandVertDir(GenesisNodeConstraint **in_constraints, GenesisZoneNodeLayout *node_layout, bool run_silent)
{
	int node_count = eaSize(&node_layout->nodes);
	int i, j, k, it;
	U8 *table;
	bool still_changing;

	table = calloc(node_count*node_count, sizeof(U8));

	for (i = 0; i < eaSize(&in_constraints) ; i++)
	{
		GenesisNodeConstraint *constraint = in_constraints[i];
		bool invert = constraint->values.invert;
		int low_idx, high_idx;
		if( constraint->function != GCF_Uphill )
			continue;

		assert(constraint->values.end->uid != constraint->values.start->uid);
		if(constraint->values.end->uid > constraint->values.start->uid)
		{
			high_idx = constraint->values.end->uid;
			low_idx = constraint->values.start->uid;
		}
		else
		{
			high_idx = constraint->values.start->uid;
			low_idx = constraint->values.end->uid;
			invert = !invert;
		}

		if(invert)
			table[high_idx + low_idx*node_count] = GENESIS_PATH_DOWNHILL;
		else
			table[high_idx + low_idx*node_count] = GENESIS_PATH_UPHILL;
	}

	still_changing = (node_count > 0);
	for (it = 0; still_changing; it++)
	{
		assert(it < node_count);
		still_changing = false;
		for( i=0; i < node_count; i++ )
		{
			for( k=i+1; k < node_count; k++ )
			{
				for( j=0; j < node_count; j++ )
				{
					U8 ab, bc, ac;
					F32 min=0, max=0;
					if(j==i || j==k)
						continue;

					if(i<j)
					{
						ab = table[j + i*node_count];
					}
					else 
					{
						ab = table[i + j*node_count];
						if(ab != 0)
							ab = (ab == GENESIS_PATH_UPHILL ? GENESIS_PATH_DOWNHILL : GENESIS_PATH_UPHILL);
					}
					if(j<k)
					{
						bc = table[k + j*node_count];
					}
					else 
					{
						bc = table[j + k*node_count];
						if(bc != 0)
							bc = (bc == GENESIS_PATH_UPHILL ? GENESIS_PATH_DOWNHILL : GENESIS_PATH_UPHILL);
					}
					ac = table[k + i*node_count];

					if(ab == bc && ab != 0)
					{
						if(ac != 0 && ac != ab)
						{
							genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextLayout(node_layout->layout_name), "Uphill/Downhill constraints are unresolvable.");
						}
						else if (ac != ab)
						{
							table[k + i*node_count] = ab;
							still_changing = true;
						}
					}
				}
			}
		}
	}

	for( j=0; j < node_count; j++ )
	{
		for( i=j+1; i < node_count; i++ )
		{
			U8 val = table[i + j*node_count];
			if(val != 0)
			{
				GenesisParamValues temp_values={0};
				temp_values.invert = (val == GENESIS_PATH_DOWNHILL);
				temp_values.start = node_layout->nodes[j]; 
				temp_values.end = node_layout->nodes[i];
				genesisExteriorMakeBasicConstraint(&temp_values, GCF_Uphill);
			}
		}
	}
	free(table);
	return true;
}

static bool genesisExteriorExpandIsBetween(GenesisNodeConstraint **in_constraints, 
										   GenesisZoneNodeLayout *node_layout, bool run_silent)
{
	int i;

	for( i=0; i < eaSize(&in_constraints); i++ )
	{
		GenesisNodeConstraint *constraint = in_constraints[i];
		if( constraint->function != GCF_IsBetween )
			continue;

// 			if(!constraint->validated)
// 			{
// 				//Target1
// 				if((idx = genesisExteriorGetParamIndex(constraint->params, "Target1")) == -1)
// 				{
// 					if(!run_silent)
// 						Alertf("No target1 specified for a between in '%s'", room->room.name);
// 					return false;
// 				}
// 				if((idx = genesisExteriorGetRoomIndex(layout->rooms, constraint->params[idx]->value)) == -1)
// 				{
// 					if(!run_silent)
// 						Alertf("Cannot find target for a between in '%s'", room->room.name);
// 					return false;
// 				}
// 				assert(eaSize(&node_layout->nodes) > idx);
// 				constraint->values.ref_1 = node_layout->nodes[idx];
// 
// 				//Target2
// 				if((idx = genesisExteriorGetParamIndex(constraint->params, "Target2")) == -1)
// 				{
// 					if(!run_silent)
// 						Alertf("No target2 specified for a between in '%s'", room->room.name);
// 					return false;
// 				}
// 				if((idx = genesisExteriorGetRoomIndex(layout->rooms, constraint->params[idx]->value)) == -1)
// 				{
// 					if(!run_silent)
// 						Alertf("Cannot find target for a between in '%s'", room->room.name);
// 					return false;
// 				}
// 				assert(eaSize(&node_layout->nodes) > idx);
// 				constraint->values.ref_2 = node_layout->nodes[idx];
// 
// 				constraint->validated = true;
// 			}

		if (!genesisExteriorMake3WayConstraint(&constraint->values, GCF_IsBetween))
			return false;
	}
	return true;
}

static bool genesisExteriorExpandMinMax(GenesisNodeConstraint **in_constraints, GenesisZoneNodeLayout *node_layout, bool run_silent)
{
	int node_count = eaSize(&node_layout->nodes);
	int i, j, k, it;
	F32 *min_max_table;
	F32 *ab, *bc;
	F32 val;
	bool still_changing;

	min_max_table = calloc(node_count*node_count, sizeof(F32)*2);

	for (i = 0; i < eaSize(&in_constraints) ; i++)
	{
		F32 *val_ptr;
		GenesisNodeConstraint *constraint = in_constraints[i];
		int low_idx, high_idx;
		if( constraint->function != GCF_MinDist && 
			constraint->function != GCF_MaxDist )
			continue;
// 
// 			if(!constraint->validated)
// 			{
// 				//Distance
// 				if((idx = genesisExteriorGetParamIndex(constraint->params, "Distance")) == -1)
// 				{
// 					if(!run_silent)
// 						Alertf("No distance specified for a min/max in '%s'", room->room.name);
// 					free(min_max_table);
// 					return false;
// 				}
// 				constraint->values.float_1 = atof(constraint->params[idx]->value);
// 
// 				//Target
// 				if((idx = genesisExteriorGetParamIndex(constraint->params, "Target")) == -1)
// 				{
// 					if(!run_silent)
// 						Alertf("No target specified for a min/max in '%s'", room->room.name);
// 					free(min_max_table);
// 					return false;
// 				}
// 				if((idx = genesisExteriorGetRoomIndex(layout->rooms, constraint->params[idx]->value)) == -1)
// 				{
// 					if(!run_silent)
// 						Alertf("Cannot find target for a min/max in '%s'", room->room.name);
// 					free(min_max_table);
// 					return false;
// 				}
// 				assert(eaSize(&node_layout->nodes) > idx);
// 				constraint->values.ref_1 = node_layout->nodes[idx];
// 				constraint->validated = true;
// 			}

		assert(constraint->values.end->uid != constraint->values.start->uid);
		if(constraint->values.end->uid > constraint->values.start->uid)
		{
			high_idx = constraint->values.end->uid;
			low_idx = constraint->values.start->uid;
		}
		else
		{
			high_idx = constraint->values.start->uid;
			low_idx = constraint->values.end->uid;
		}

		if(constraint->function == GCF_MaxDist)
		{
			val_ptr = &min_max_table[(high_idx + low_idx*node_count)*2 + 1];
			if(constraint->values.dist != 0.0f)
			{
				if(constraint->values.dist < (*val_ptr) || (*val_ptr) == 0.0f)
					(*val_ptr) = constraint->values.dist;
			}
		}
		else
		{
			val_ptr = &min_max_table[(high_idx + low_idx*node_count)*2 + 0];
			if(constraint->values.dist > (*val_ptr))
				(*val_ptr) = constraint->values.dist;
		}
	}

	still_changing = (node_count > 0);
	for (it = 0; still_changing; it++)
	{
		assert(it < node_count);
		still_changing = false;
		for( i=0; i < node_count; i++ )
		{
			for( k=i+1; k < node_count; k++ )
			{
				for( j=0; j < node_count; j++ )
				{
					F32 min=0, max=0;
					if(j==i || j==k)
						continue;

					//Calculate Min
					ab = min_max_table + (MAX(i,j) + MIN(i,j)*node_count)*2;
					bc = min_max_table + (MAX(j,k) + MIN(j,k)*node_count)*2 + 1;
					if(*ab > 0.0f && *bc > 0.0f)
						min = (MAX(0, *ab - *bc) + NODE_RES);
					if(min > min_max_table[(MAX(i,k) + MIN(i,k)*node_count)*2 + 0])
					{
						min_max_table[(MAX(i,k) + MIN(i,k)*node_count)*2 + 0] = min;
						still_changing = true;
					}

					//Calculate Max
					ab = min_max_table + (MAX(i,j) + MIN(i,j)*node_count)*2 + 1;
					if(*ab > 0.0f && *bc > 0.0f)
						max = *ab + *bc - NODE_RES;
					val = min_max_table[(MAX(i,k) + MIN(i,k)*node_count)*2 + 1];
					if((max < val && max != 0.0f) || (val == 0.0f && max != 0.0f))
					{
						min_max_table[(MAX(i,k) + MIN(i,k)*node_count)*2 + 1] = max;
						still_changing = true;
					}
				}
			}
		}
	}

	for( j=0; j < node_count; j++ )
	{
		for( i=j+1; i < node_count; i++ )
		{
			F32 min=0, max=0;
			min = min_max_table[(i + j*node_count)*2 + 0];
			max = min_max_table[(i + j*node_count)*2 + 1];

			if(max < min && max != 0.0f)
			{
				if(!run_silent)
					genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(node_layout->layout_name), "Min/Max path length contraints unsolvable.");
				free(min_max_table);
				return false;
			}

			if(min != 0.0f)
			{
				GenesisParamValues temp_values={0};
				temp_values.invert = false;
				temp_values.dist = min;
				temp_values.start = node_layout->nodes[j]; 
				temp_values.end = node_layout->nodes[i];
				genesisExteriorMakeBasicConstraint(&temp_values, GCF_MinDist);
			}
			if(max != 0.0f)
			{
				GenesisParamValues temp_values={0};
				temp_values.invert = true;
				temp_values.dist = max;
				temp_values.start = node_layout->nodes[j]; 
				temp_values.end = node_layout->nodes[i];
				genesisExteriorMakeBasicConstraint(&temp_values, GCF_MaxDist);
			}
		}
	}
	free(min_max_table);
	return true;
}

//////////////////////////////////////////////////////////////////
// Nature Nodes
//////////////////////////////////////////////////////////////////

#define GEN_RND_NATURE_PNTS 20
#define GEN_RND_NATURE_SUB_PNTS 3

static void genesisExteriorAddNaturePoints(GenesisExteriorData *data, GenesisZoneNodeLayout *node_layout, MersenneTable *random_table)
{
	typedef enum GenesisNatureTypes
	{
		GNT_Mountain=0,
		GNT_Chasm,
		GNT_Plain,
		GNT_Count,
	} GenesisNatureTypes;
	GenesisNatureTypes nature_types[GNT_Count];
	int type_count;
	int i, j, node_count;
	int nature_start = eaSize(&node_layout->nodes);

	//Setup Nature Data
	type_count=0;
	if (!data->geotype_filled->room_data.no_mountain)
	{
		nature_types[type_count] = GNT_Mountain;
		type_count++;
	}
	if (!data->geotype_filled->room_data.no_chasm)
	{
		nature_types[type_count] = GNT_Chasm;
		type_count++;
	}
	if (!data->geotype_filled->room_data.no_plains)
	{
		nature_types[type_count] = GNT_Plain;
		type_count++;
	}
	if(type_count == 0)
	{
		nature_types[type_count] = -1;
		type_count++;
	}

	node_count = GEN_RND_NATURE_PNTS * (node_layout->play_max[0]-node_layout->play_min[0]) * (node_layout->play_max[1]-node_layout->play_min[1]) * 0.0000002384185791015625f; // 1/SQR(2048)
	node_count = MAX(1, node_count);

	for( i=0; i < node_count; i++ )
	{
		GenesisNodeConstraint *new_constraint;
		GenesisNode *node = StructCreate(parse_GenesisNode);

		U32 type;
		F32 min_height, max_height;
		char node_name[64];
		sprintf(node_name, "NATURE_%d", i);
		node->name = StructAllocString(node_name);
		node->uid = nature_start+i;
		node->node_type = GENESIS_NODE_Nature;
		type = randomMersenneU32(random_table)%type_count;
		switch(nature_types[type])
		{
		case GNT_Mountain:
			min_height = data->mountain_range[0];
			max_height = data->mountain_range[1];
			break;
		case GNT_Chasm:
			min_height = data->chasm_range[0];
			max_height = data->chasm_range[1];
			break;
		case GNT_Plain:
		default:
			min_height = data->playable_range[0];
			max_height = data->playable_range[1];
		}

		//Min Max Height
		new_constraint = StructCreate(parse_GenesisNodeConstraint);
		new_constraint->function = GCF_GlobalHeight;
		//new_constraint->validated = true;
		new_constraint->values.start = node;
		new_constraint->values.min = min_height;
		new_constraint->values.max = max_height;
		eaPush(&node->final_constraints, new_constraint);

		//Angle from the edge of the vista
		new_constraint = StructCreate(parse_GenesisNodeConstraint);
		new_constraint->function = GCF_VistaBox;
		//new_constraint->validated = true;
		new_constraint->values.invert = false;
		new_constraint->values.start = node;
		new_constraint->values.angle = data->geotype_filled->room_data.max_mountain_angle;
		new_constraint->values.min = data->playable_range[0];
		new_constraint->values.max = data->playable_range[1];
		eaPush(&node->final_constraints, new_constraint);

		for( j=0; j < eaSize(&node_layout->nodes); j++ )
		{
			F32 dist = 200.0f;
			if(j >= nature_start)
				dist = 1400.0f;

			eaPushUnique(&node->parents, node_layout->nodes[j]);

			//Min Dist Constraint
			new_constraint = StructCreate(parse_GenesisNodeConstraint);
			new_constraint->function = GCF_MinDist;
			//new_constraint->validated = true;
			new_constraint->values.invert = false;
			new_constraint->values.dist = dist;
			new_constraint->values.start = node;
			new_constraint->values.end = node_layout->nodes[j];
			eaPush(&node->final_constraints, new_constraint);

			//Not Between
			if(j < nature_start)
			{
				//Max Angle Constraint
				new_constraint = StructCreate(parse_GenesisNodeConstraint);
				new_constraint->function = GCF_MaxVertAngleDiff;
				//new_constraint->validated = true;
				new_constraint->values.angle = data->geotype_filled->room_data.max_mountain_angle;
				new_constraint->values.start = node;
				new_constraint->values.end = node_layout->nodes[j];
				eaPush(&node->final_constraints, new_constraint);

				/* TomY TODO
				for( k=0; k < eaSize(&layout->rooms[j]->constraints); k++ )
				{
					GenesisNodeConstraint *constraint = layout->rooms[j]->constraints[k];
					if(constraint->function != GCF_Connected)
						continue;

					new_constraint = StructCreate(parse_GenesisNodeConstraint);
					new_constraint->function = GCF_IsBetween;
					new_constraint->validated = true;
					new_constraint->values.invert = true;
					new_constraint->values.ref_1 = node_layout->nodes[j];
					new_constraint->values.ref_2 = constraint->values.ref_1;
					eaPush(&node->constraints, new_constraint);
				}*/
			}
		}

		eaPush(&node_layout->nodes, node);
	}

}

static void genesisExteriorAddNatureSubPoints(GenesisExteriorData *data, GenesisZoneNodeLayout *node_layout, int nature_start, MersenneTable *random_table)
{
	int p, i, j;
	int sub_nature_start = eaSize(&node_layout->nodes);
	int nature_set_start;
	if(!node_layout->nodes)
		return;
	for( p=nature_start; p < sub_nature_start; p++ )
	{
		assert(node_layout->nodes[p]);
		nature_set_start = eaSize(&node_layout->nodes);
		for( i=0; i < GEN_RND_NATURE_SUB_PNTS; i++ )
		{
			GenesisNodeConstraint *new_constraint;
			GenesisNode *node = StructCreate(parse_GenesisNode);
			char node_name[64];
			sprintf(node_name, "SUBNATURE_%d_%d", p, i);
			node->name = StructAllocString(node_name);
			node->uid = nature_set_start+i;
			node->node_type = GENESIS_NODE_Nature;
			node->nature_parent = node_layout->nodes[p];

			//Max Dist Constraint
			new_constraint = StructCreate(parse_GenesisNodeConstraint);
			new_constraint->function = GCF_MinDist;
			//new_constraint->validated = true;
			new_constraint->values.invert = true;
			new_constraint->values.dist = 700.0f;
			new_constraint->values.start = node;
			new_constraint->values.end = node_layout->nodes[p];
			eaPush(&node->final_constraints, new_constraint);
			//Max Angle Constraint
			new_constraint = StructCreate(parse_GenesisNodeConstraint);
			new_constraint->function = GCF_MaxVertAngleDiff;
			//new_constraint->validated = true;
			new_constraint->values.angle = data->geotype_filled->room_data.max_road_angle / 2.0f;
			new_constraint->values.start = node;
			new_constraint->values.end = node_layout->nodes[p];
			eaPush(&node->final_constraints, new_constraint);

			for( j=0; j < eaSize(&node_layout->nodes); j++ )
			{
				eaPushUnique(&node->parents, node_layout->nodes[j]);

				//Min Dist Constraint
				new_constraint = StructCreate(parse_GenesisNodeConstraint);
				new_constraint->function = GCF_MinDist;
				//new_constraint->validated = true;
				new_constraint->values.invert = false;
				new_constraint->values.dist = 200.0f;
				new_constraint->values.start = node;
				new_constraint->values.end = node_layout->nodes[j];
				eaPush(&node->final_constraints, new_constraint);

				if(j >= nature_set_start)
				{
					//Max Angle Constraint
					new_constraint = StructCreate(parse_GenesisNodeConstraint);
					new_constraint->function = GCF_MaxVertAngleDiff;
					//new_constraint->validated = true;
					new_constraint->values.angle = data->geotype_filled->room_data.max_road_angle / 2.0f;
					new_constraint->values.start = node;
					new_constraint->values.end = node_layout->nodes[j];
					eaPush(&node->final_constraints, new_constraint);
				}

				//Not Between and same side as parent
				/* TomY TODO
				if(j < nature_start)
				{
					for( k=0; k < eaSize(&layout->rooms[j]->constraints); k++ )
					{
						GenesisNodeConstraint *constraint = layout->rooms[j]->constraints[k];
						if(constraint->function != GCF_Connected)
							continue;

						new_constraint = StructCreate(parse_GenesisNodeConstraint);
						new_constraint->function = GCF_IsBetween;
						new_constraint->validated = true;
						new_constraint->values.invert = true;
						new_constraint->values.ref_1 = node_layout->nodes[j];
						new_constraint->values.ref_2 = constraint->values.ref_1;
						eaPush(&node->constraints, new_constraint);

						new_constraint = StructCreate(parse_GenesisNodeConstraint);
						new_constraint->function = GCF_OtherSideOf;
						new_constraint->validated = true;
						new_constraint->values.invert = true;
						new_constraint->values.ref_1 = node_layout->nodes[j];
						new_constraint->values.ref_2 = constraint->values.ref_1;
						new_constraint->values.ref_3 = node_layout->nodes[p];
						eaPush(&node->constraints, new_constraint);
					}
				}*/
			}

			eaPush(&node_layout->nodes, node);
		}
	}
}

//////////////////////////////////////////////////////////////////
// Placement Functions
//////////////////////////////////////////////////////////////////

static bool genesisExteriorPlaceMinDist(PLACE_NODE_PARAMS)
{
	bool ret = false;
	GenesisNode *node = constraint->values.start;
	GenesisNode *to_node = constraint->values.end;
	F32 xdiff, zdiff;
	F32 dist;
	assert(to_node);

	if (node->node_type == GENESIS_NODE_Nature && !to_node->placed)
		return true;

	xdiff = node->pos[0] - to_node->pos[0];
	zdiff = node->pos[2] - to_node->pos[2];
	dist = fsqrt(xdiff*xdiff + zdiff*zdiff);
	if(dist > constraint->values.dist)
		ret = true;

	return (constraint->values.invert ? !ret : ret);
}

static bool genesisExteriorPlaceMaxVertAngleDiff(PLACE_NODE_PARAMS)
{
	GenesisNode *other_node = (placing_node == constraint->values.start) ? constraint->values.end : constraint->values.start;
	F32 xdiff, zdiff;
	F32 max_height_diff;
	F32 dist;
	assert(other_node);

	xdiff = placing_node->pos[0] - other_node->pos[0];
	zdiff = placing_node->pos[2] - other_node->pos[2];
	dist = fsqrt(xdiff*xdiff + zdiff*zdiff) - placing_node->actual_size - other_node->actual_size;
	if(dist < 0)
		return false;
	max_height_diff = dist*tan(constraint->values.angle*3.14159/180.0f);

	if(other_node->pos[1] - max_height_diff > *min_height)
		*min_height = other_node->pos[1] - max_height_diff;
	if(other_node->pos[1] + max_height_diff < *max_height)
		*max_height = other_node->pos[1] + max_height_diff;

	*height_constraint = true;
	return true;
}

static bool genesisExteriorPlaceUphill(PLACE_NODE_PARAMS)
{
	GenesisNode *other_node;
	bool uphill = !constraint->values.invert;

	if(placing_node == constraint->values.start)
	{
		other_node = constraint->values.end;
	}
	else
	{
		other_node = constraint->values.start;
		uphill = !uphill;
	}
	assert(other_node);

	if(uphill)
	{
		if(other_node->pos[1] < *max_height)
			*max_height = other_node->pos[1];
	}
	else
	{
		if(other_node->pos[1] > *min_height)
			*min_height = other_node->pos[1];
	}

	*height_constraint = true;
	return true;
}

static bool genesisExteriorPlaceGlobalHeight(PLACE_NODE_PARAMS)
{
	F32 new_min = constraint->values.min;
	F32 new_max = constraint->values.max;

	if(new_min > *min_height)
		*min_height = new_min;
	if(new_max < *max_height)
		*max_height = new_max;

	*height_constraint = true;
	return true;
}

static bool genesisExteriorPlaceVistaBox(PLACE_NODE_PARAMS)
{
	GenesisNode *node = constraint->values.start;
	F32 xdiff, zdiff;
	F32 max_height_diff;
	F32 dist;

	if( node->pos[0] > DEFAULT_EXTERIOR_PLAYFIELD_BUFFER &&
		node->pos[2] > DEFAULT_EXTERIOR_PLAYFIELD_BUFFER &&
		node->pos[0] < DEFAULT_EXTERIOR_PLAYFIELD_SIZE-DEFAULT_EXTERIOR_PLAYFIELD_BUFFER &&
		node->pos[2] < DEFAULT_EXTERIOR_PLAYFIELD_SIZE-DEFAULT_EXTERIOR_PLAYFIELD_BUFFER )
	{
		return true;
	}

	if(node->pos[0] < DEFAULT_EXTERIOR_PLAYFIELD_BUFFER)
		xdiff = DEFAULT_EXTERIOR_PLAYFIELD_BUFFER - node->pos[0];
	else if(node->pos[0] > DEFAULT_EXTERIOR_PLAYFIELD_SIZE-DEFAULT_EXTERIOR_PLAYFIELD_BUFFER)
		xdiff = node->pos[0] - (DEFAULT_EXTERIOR_PLAYFIELD_SIZE-DEFAULT_EXTERIOR_PLAYFIELD_BUFFER);
	else
		xdiff = 0;

	if(node->pos[2] < DEFAULT_EXTERIOR_PLAYFIELD_BUFFER)
		zdiff = DEFAULT_EXTERIOR_PLAYFIELD_BUFFER - node->pos[2];
	else if(node->pos[2] > DEFAULT_EXTERIOR_PLAYFIELD_SIZE-DEFAULT_EXTERIOR_PLAYFIELD_BUFFER)
		zdiff = node->pos[2] - (DEFAULT_EXTERIOR_PLAYFIELD_SIZE-DEFAULT_EXTERIOR_PLAYFIELD_BUFFER);
	else
		zdiff = 0;

	dist = fsqrt(xdiff*xdiff + zdiff*zdiff);
	if(dist < 0)
		return false;
	max_height_diff = dist*tan(constraint->values.angle*3.14159/180.0f);

	// Set min to *at most* max-max_height_diff, and vice versa
	if(constraint->values.max - max_height_diff > *min_height)
		*min_height = constraint->values.max - max_height_diff;
	if(constraint->values.min + max_height_diff < *max_height)
		*max_height = constraint->values.min + max_height_diff;

	*height_constraint = true;
	return true;
}

#define BETWEEN_ALLOWANCE 1.1f
#define BETWEEN_ADJUSTMENT 0.04545f //This adjustment equates to (BETWEEN_ALLOWANCE - 1) / (2*BETWEEN_ALLOWANCE)

// Is "node" between "a" and "b"
static bool genesisExteriorIsBetween(Vec2 node_pos, Vec2 pos_a, Vec2 pos_b)
{
	Vec2 dir;
	F32 ab_dist;
	F32 a_dist;
	F32 b_dist;

	subVec2(pos_b, pos_a, dir);
	ab_dist = normalVec2(dir);
	scaleVec2(dir, BETWEEN_ADJUSTMENT*ab_dist, dir);
	addVec2(pos_a, dir, pos_a);
	subVec2(pos_b, dir, pos_b);

	subVec2(pos_b, pos_a, dir);
	ab_dist = fsqrt(SQR(dir[0])+SQR(dir[1]));
	subVec2(pos_a, node_pos, dir);
	a_dist = fsqrt(SQR(dir[0])+SQR(dir[1]));
	subVec2(pos_b, node_pos, dir);
	b_dist = fsqrt(SQR(dir[0])+SQR(dir[1]));

	if((a_dist+b_dist)/ab_dist < BETWEEN_ALLOWANCE)
		return true;
	return false;
}

static bool genesisExteriorPlaceIsBetween(PLACE_NODE_PARAMS)
{
	bool ret = false;
	GenesisNode *node = constraint->values.mid;
	GenesisNode *node_a = constraint->values.start;
	GenesisNode *node_b = constraint->values.end;
	Vec2 node_pos;
	Vec2 pos_a;
	Vec2 pos_b;
	Vec2 dir;
	Vec2 offset;
	assert(node_a && node_b);

	node_pos[0] = node->pos[0];
	node_pos[1] = node->pos[2];
	pos_a[0] = node_a->pos[0];
	pos_a[1] = node_a->pos[2];
	pos_b[0] = node_b->pos[0];
	pos_b[1] = node_b->pos[2];

	subVec2(pos_a, pos_b, dir);
	normalVec2(dir);
	scaleVec2(dir, node->actual_size + node_a->actual_size, offset);
	addVec2(pos_a, offset, pos_a);
	scaleVec2(dir, node->actual_size + node_b->actual_size, offset);
	subVec2(pos_b, offset, pos_b);

	ret = genesisExteriorIsBetween(node_pos, pos_a, pos_b);

	return (constraint->values.invert ? !ret : ret);
}

static bool genesisExteriorPlaceOtherSideOf(PLACE_NODE_PARAMS)
{
	GenesisNode *node = constraint->values.start;
	GenesisNode *node_a = constraint->values.side1;
	GenesisNode *node_b = constraint->values.side2;
	GenesisNode *to_node = constraint->values.end;
	Vec2 node_pos;
	Vec2 to_node_pos;
	Vec2 pos_a;
	Vec2 pos_b;
	Vec2 line_vec;
	Vec2 point_vec;
	F32 temp;
	assert(node_a && node_b && to_node);

	node_pos[0] = node->pos[0];
	node_pos[1] = node->pos[2];
	to_node_pos[0] = to_node->pos[0];
	to_node_pos[1] = to_node->pos[2];
	pos_a[0] = node_a->pos[0];
	pos_a[1] = node_a->pos[2];
	pos_b[0] = node_b->pos[0];
	pos_b[1] = node_b->pos[2];

	// Clip plane 1: Line between A & B
	subVec2(pos_b, pos_a, line_vec);
	normalVec2(line_vec);
	temp = line_vec[0];
	line_vec[0] = -line_vec[1]; // (Reciprocal tangent of line)
	line_vec[1] = temp;
	subVec2(to_node_pos, pos_a, point_vec);
	if(dotVec2(line_vec, point_vec) > 0) // (Projection onto tangent)
	{
		copyVec2(pos_a, point_vec);
		copyVec2(pos_b, pos_a);
		copyVec2(point_vec, pos_b);
		subVec2(pos_b, pos_a, line_vec);
		normalVec2(line_vec);
		temp = line_vec[0];
		line_vec[0] = -line_vec[1];
		line_vec[1] = temp;
	}
	subVec2(node_pos, pos_a, point_vec);
	if(dotVec2(line_vec, point_vec) < 0) // Clip test
		return (constraint->values.invert ? true : false);

	// Clip plane 2: Line from To Node to A
	subVec2(to_node_pos, pos_a, line_vec);
	normalVec2(line_vec);
	temp = line_vec[0];
	line_vec[0] = -line_vec[1];
	line_vec[1] = temp;
	subVec2(node_pos, pos_a, point_vec);
	if(dotVec2(line_vec, point_vec) < 0) // Clip Test
		return (constraint->values.invert ? true : false);

	// Clip plane 3: Line from B to To Node
	subVec2(pos_b, to_node_pos, line_vec);
	normalVec2(line_vec);
	temp = line_vec[0];
	line_vec[0] = -line_vec[1];
	line_vec[1] = temp;
	subVec2(node_pos, to_node_pos, point_vec);
	if(dotVec2(line_vec, point_vec) < 0) // Clip Test
		return (constraint->values.invert ? true : false);

	return (constraint->values.invert ? false : true);
}

static void* genesisExteriorGetPlaceFunc(GenesisConstraintFuncName name)
{
	switch(name)
	{
	case GCF_MaxDist:
	case GCF_MinDist:
		return genesisExteriorPlaceMinDist;
	case GCF_IsBetween:
		return genesisExteriorPlaceIsBetween;
	case GCF_OtherSideOf:
		return genesisExteriorPlaceOtherSideOf;
	case GCF_MaxVertAngleDiff:
		return genesisExteriorPlaceMaxVertAngleDiff;
	case GCF_Uphill:
		return genesisExteriorPlaceUphill;
	case GCF_GlobalHeight:
		return genesisExteriorPlaceGlobalHeight;
	case GCF_VistaBox:
		return genesisExteriorPlaceVistaBox;
	}
	return NULL;
}

//////////////////////////////////////////////////////////////////
// Node Handling
//////////////////////////////////////////////////////////////////

void genesisExteriorAddToSortedList(GenesisNode ***list, GenesisNode *node)
{
	int i;
	if(eaFind(list, node) != -1)
		return;
	for( i=0; i < eaSize(&node->parents) ; i++ )
	{
		if(eaFind(list, node->parents[i]) == -1)
			genesisExteriorAddToSortedList(list, node->parents[i]);
	}
	eaPush(list, node);
}

static bool genesisExteriorPlaceNode(GenesisExteriorData *data, GenesisNode *node, F32 offset_x, F32 offset_z, F32 width, F32 height, F32 pos_x, F32 pos_z, F32 size_test, MersenneTable *random_table, GenesisNode **node_list, int num_placed)
{
	int i;
	if(size_test > NODE_RES)
	{
		//Keep descending
		F32 new_pos_x = 0;
		F32 new_pos_z = 0;
		U8 quad_list[4] = {0, 1, 2, 3};
		U8 temp;

		for( i=3; i >= 0; i-- )
		{
			U8 quad = randomMersenneU32(random_table)%(i+1);
			switch(quad_list[quad])
			{
			case 0:
				new_pos_x = pos_x/2.0f;
				new_pos_z = pos_z/2.0f;
				break;
			case 1:
				new_pos_x = pos_x/2.0f + 0.5f;
				new_pos_z = pos_z/2.0f;
				break;
			case 2:
				new_pos_x = pos_x/2.0f + 0.5f;
				new_pos_z = pos_z/2.0f + 0.5f;
				break;
			case 3:
				new_pos_x = pos_x/2.0f;
				new_pos_z = pos_z/2.0f + 0.5f;
				break;
			}
			if(genesisExteriorPlaceNode(data, node, offset_x, offset_z, width, height, new_pos_x, new_pos_z, size_test/2.0f, random_table, node_list, num_placed))
				return true;

			temp = quad_list[quad];
			quad_list[quad] = quad_list[i];
			quad_list[i] = temp;
		}
		return false;
	}
	else
	{
		//Check placement
		Vec2 this_node_pos;
		Vec2 chk_node_pos;
		Vec2 line;
		F32 min_height = -5000.0f;
		F32 max_height =  5000.0f;
		bool height_constraint=false;

		node->pos[0] = offset_x + width*pos_x;
		node->pos[2] = offset_z + height*pos_z;

		for( i=0; i < num_placed ; i++ )
		{
			this_node_pos[0] = node->pos[0];
			this_node_pos[1] = node->pos[2];
			chk_node_pos[0] = node_list[i]->pos[0];
			chk_node_pos[1] = node_list[i]->pos[2];
			subVec2(this_node_pos, chk_node_pos, line);
			if(fsqrt(SQR(line[0])+SQR(line[1])) < (node->actual_size + node_list[i]->actual_size + 35.0f))//Extra 35ft is just to make sure the clearings are not touching
				return false;
		}

		//Check Position
		for( i=0; i < eaSize(&node->final_constraints) ; i++ )
		{
			genesisExteriorPlaceNodeFunc place_func = 
				genesisExteriorGetPlaceFunc(node->final_constraints[i]->function);
			if(place_func && !place_func(node, node->final_constraints[i], random_table, &min_height, &max_height, &height_constraint))
				return false;
			if(max_height < min_height)
				return false;
		}

		if(!height_constraint)
		{
			min_height = data->playable_range[0];
			max_height = data->playable_range[1];
		}
		node->pos[1] = ((randomMersenneF32(random_table)+1.0f)/2.0f)*(max_height-min_height) + min_height;

		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////
// Exterior Data
//////////////////////////////////////////////////////////////////

static F32 genesisGetMaxRoadAngle(GenesisGeotype *geo_data)
{
	F32 ret = -1.0f;
	if (geo_data)
		ret = geo_data->room_data.max_road_angle;
	if (ret < 0 || ret > 90)
		ret = 30.0f;
	return ret;
}

static GenesisExteriorData* genesisCreateExteriorData(GenesisGeotype *geo_data)
{
	GenesisExteriorData *data = calloc(sizeof(GenesisExteriorData), 1);
	data->geotype_filled = StructCreate(parse_GenesisGeotype);
	if (geo_data)
	{
		StructCopyAll(parse_GenesisGeotype, geo_data, data->geotype_filled);
		if (data->geotype_filled->room_data.max_road_angle < 0 || data->geotype_filled->room_data.max_road_angle > 90)
			data->geotype_filled->room_data.max_road_angle = 30.0f;
		if (data->geotype_filled->room_data.max_mountain_angle < 0 || data->geotype_filled->room_data.max_mountain_angle > 90)
			data->geotype_filled->room_data.max_mountain_angle = 40.0f;
	}
	else
	{
		data->geotype_filled->room_data.max_road_angle = 30.0f;
		data->geotype_filled->room_data.max_mountain_angle = 40.0f;
		data->geotype_filled->room_data.water_offset = 16.0f;
		data->geotype_filled->node_data.noise_level = 20.0f;
	}
	data->mountain_range[0] = 1200.0f*data->geotype_filled->room_data.max_mountain_angle/30.0f;
	data->mountain_range[1] = 1700.0f*data->geotype_filled->room_data.max_mountain_angle/30.0f;
	data->chasm_range[0] = -700.0f*data->geotype_filled->room_data.max_mountain_angle/30.0f;
	data->chasm_range[1] = -200.0f*data->geotype_filled->room_data.max_mountain_angle/30.0f;
	data->playable_range[0] = 0.0f;
	data->playable_range[1] = 1000.0f*data->geotype_filled->room_data.max_road_angle/40.0f;
	data->water_offset = data->geotype_filled->room_data.water_offset;
	return data;
}

static void genesisDestroyExteriorData(GenesisExteriorData *data)
{
	StructDestroy(parse_GenesisGeotype, data->geotype_filled);
	free(data);
}

//////////////////////////////////////////////////////////////////
// Main function
//////////////////////////////////////////////////////////////////

GenesisZoneNodeLayout* genesisExteriorMoveRoomsToNodes(GenesisZoneExterior *layout, U32 seed, U32 detail_seed, bool run_silent, bool make_vista, bool add_nature)
{
	#define GEN_ROOM_SIZE_TO_NODE(x) ((x)->room.width/2.0f)
	int i, j;
	GenesisGeotype *geo_data;
	GenesisZoneNodeLayout *node_layout;
	GenesisZoneNodeLayout *node_layout_ret;
	MersenneTable *random_table;
	F32 play_buffer;
	Vec2 full_play_size;
	Vec2 full_play_offset;
	F32 max_road_angle;
	GenesisNodeConstraint **input_constraints = NULL;

	if(eaSize(&layout->rooms) == 0)
	{
		genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(layout->layout_name), "No rooms in layout");
		return NULL;
	}

	//Init properties 
	if(nearSameVec2(layout->play_min, layout->play_max))
	{
		setVec2same(full_play_offset, 0);
		play_buffer = DEFAULT_EXTERIOR_PLAYFIELD_BUFFER;
		setVec2same(full_play_size, DEFAULT_EXTERIOR_PLAYFIELD_SIZE);
	}
	else
	{
		subVec2(layout->play_max, layout->play_min, full_play_size);
		full_play_offset[0] = layout->play_min[0];
		full_play_offset[1] = layout->play_min[1];
		play_buffer = layout->play_buffer;
	}
	play_buffer = MAX(EXTERIOR_MIN_PLAYFIELD_BUFFER, play_buffer);
	if(!run_silent)
	{
		//Verify there is enough area for all the rooms
		F32 room_area=0;
		for ( i=0; i < eaSize(&layout->rooms); i++ )
		{
			GenesisZoneMapRoom *room = layout->rooms[i];
			F32 room_rad = GEN_ROOM_SIZE_TO_NODE(room);
			room_area += SQR(room_rad*2);
		}
		if(room_area > (full_play_size[0]-play_buffer)*(full_play_size[1]-play_buffer)/4.0f)
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextLayout(layout->layout_name), "There are too many rooms or too large of rooms to be placed in this map.  It may be very unlikely to solve.");
	}

	//Get road angle
	geo_data = GET_REF(layout->geotype);
	max_road_angle = genesisGetMaxRoadAngle(geo_data);
	if(layout->max_road_angle > 0)
		max_road_angle = layout->max_road_angle;
	if(layout->vert_dir == GENESIS_PATH_FLAT)
		max_road_angle = 0;

	//Copy general data to the nodes
	node_layout = StructCreate(parse_GenesisZoneNodeLayout);
	node_layout->seed = seed;
	node_layout->color_shift = layout->color_shift;
	node_layout->play_buffer = play_buffer;
	node_layout->no_sharing_detail = layout->no_sharing_detail;
	copyVec2(layout->play_min, node_layout->play_min);
	copyVec2(layout->play_max, node_layout->play_max);

	//Copy and convert rooms over to nodes
	random_table = mersenneTableCreate(detail_seed);
	for (i = 0; i < eaSize(&layout->rooms); i++)
	{
		GenesisZoneMapRoom *room = layout->rooms[i];
		GenesisNode *node = StructCreate(parse_GenesisNode);
		node->name = StructAllocString(room->room.name);
		//node->user_data = room->user_data;
		node->seed = randomMersenneInt(random_table);
		node->uid = i;
		node->draw_size = node->actual_size = GEN_ROOM_SIZE_TO_NODE(room);
		if(room->side_trail)
			node->node_type = GENESIS_NODE_SideTrail;
		else if(room->off_map)
			node->node_type = GENESIS_NODE_OffMap;
		else
			node->node_type = GENESIS_NODE_Clearing;
		node->source_context = genesisMakeErrorContextRoom( room->room.name, layout->layout_name );

		//Detail Kits
		StructCopyAll(parse_GenesisDetailKitAndDensity, &room->detail_kit_1, &node->detail_kit_1);
		StructCopyAll(parse_GenesisDetailKitAndDensity, &room->detail_kit_2, &node->detail_kit_2);

		for (j = 0; j < eaSize(&room->static_objects); j++)
		{
			eaPush(&node->static_objects, StructClone(parse_GenesisObject, room->static_objects[j]));
		}
		for (j = 0; j < eaSize(&room->missions); j++)
		{
			eaPush(&node->missions, StructClone(parse_GenesisRoomMission, room->missions[j]));
		}
		eaPush(&node_layout->nodes, node);
	}
	assert(eaSize(&layout->rooms) == eaSize(&node_layout->nodes));

	//Convert Constraints
	//ConvertALL TomY TODO write convert all function

	//Convert paths into constraints
	//Apply placement priorities 
	if (!genesisExteriorConvertPathList(layout, node_layout, &input_constraints, max_road_angle, run_silent, random_table) ||
		!genesisExteriorApplyPriorities(layout, node_layout, &input_constraints, run_silent))
	{
		eaDestroyStruct(&input_constraints, parse_GenesisNodeConstraint);
		genesisNodesLayoutDestroy(node_layout);
		return NULL;
	}
	mersenneTableFree(random_table);

	//Find the locations of the nodes
	node_layout_ret = genesisExpandAndPlaceNodes(&node_layout, input_constraints, geo_data, full_play_size, full_play_offset, play_buffer, run_silent, make_vista, add_nature);
	eaDestroyStruct(&input_constraints, parse_GenesisNodeConstraint);

	if (node_layout_ret)
	{
		node_layout_ret->vista_map = layout->vista_map;
		COPY_HANDLE(node_layout_ret->ecosystem, layout->ecosystem);
		COPY_HANDLE(node_layout_ret->geotype, layout->geotype);
		node_layout_ret->backdrop = StructClone(parse_GenesisBackdrop, layout->backdrop);
		node_layout_ret->is_vista = layout->is_vista;
		StructCopyString(&node_layout_ret->layout_name, layout->layout_name);
	}

	return node_layout_ret;
}

static F32 genesisOffMapBestT(F32 new_t, F32 best_t)
{
	if(new_t > 0)
	{
		if(best_t < 0 || new_t < best_t)
			return new_t;
	}
	return best_t;
}

static void genesisConnectionGetLine(GenesisZoneNodeLayout *node_layout, GenesisNodeConnection *connection, Vec3 start, Vec3 dir, F32 *length, F32 *width)
{
	GenesisNode *start_node = genesisGetNodeFromUID(node_layout, connection->start_uid);
	GenesisNode *end_node = genesisGetNodeFromUID(node_layout, connection->end_uid);
	assert(start_node && end_node);
	copyVec3(start_node->pos, start);
	subVec3(end_node->pos, start_node->pos, dir);
	*length = normalVec3(dir);
	*width = MAX(start_node->actual_size, end_node->actual_size);
	scaleAddVec3(dir, start_node->actual_size, start, start);
	*length -= start_node->actual_size;
	*length -= end_node->actual_size;
	//Subtract width from both ends because we are going to use width for distance check and we don't care if the edges touch
	scaleAddVec3(dir, *width, start, start);
	*length -= (*width * 2);
}

static bool genesisConnectionCollides(GenesisZoneNodeLayout *node_layout, GenesisNodeConnection *a, GenesisNodeConnection *b)
{
	Vec3 a_start, b_start;
	Vec3 a_dir, b_dir;
	F32 a_len, b_len;
	F32 a_width, b_width;
	F32 dist;

	genesisConnectionGetLine(node_layout, a, a_start, a_dir, &a_len, &a_width);
	genesisConnectionGetLine(node_layout, b, b_start, b_dir, &b_len, &b_width);
	dist = LineLineDistSquared(a_start, a_dir, a_len, NULL, b_start, b_dir, b_len, NULL);
	dist = sqrt(dist);
	dist -= a_width;
	dist -= b_width;
	if(dist <= 0)
		return true;
	return false;
}

static bool genesisNodeLyaoutConnectionGroupCollides(GenesisZoneNodeLayout *node_layout, GenesisNodeConnectionGroup *test_group)
{
	int i, j, k;
	GenesisNodeConnectionGroup **connection_groups = node_layout->connection_groups;
	for ( i=0; i < eaSize(&connection_groups); i++ )
	{
		GenesisNodeConnectionGroup *group = connection_groups[i];
		if(group == test_group)
			continue;
		for ( j=0; j < eaSize(&group->connections); j++ )
		{
			for ( k=0; k < eaSize(&test_group->connections); k++ )
			{
				if(genesisConnectionCollides(node_layout, group->connections[j], test_group->connections[k]))
					return true;
			}
		}
	}
	return false;
}

GenesisZoneNodeLayout* genesisExpandAndPlaceNodes(GenesisZoneNodeLayout **in_node_layout, GenesisNodeConstraint **input_constraints, 
											  GenesisGeotype *geo_data, Vec2 full_play_size, Vec2 full_play_offset, F32 play_buffer,
											  bool run_silent, bool make_vista, bool add_nature)
{
	int i, j, k, cg;
	Vec2 min_bound;
	Vec2 max_bound;
	Vec2 play_offset;
	Vec2 play_size;
	Vec2 play_center;
	int nature_start;
	int sub_nature_start;
	F32 play_min_height = 50000;
	F32 total_min_height = 50000;
	MersenneTable *random_table;
	GenesisNode **sorted_nodes = NULL;
	GenesisExteriorData *data;
	GenesisZoneNodeLayout *node_layout = *in_node_layout;
	bool move_handled = false;
	GenesisConfig* config = genesisConfig();
	int vista_thickness = SAFE_MEMBER(config, vista_thickness);

	*in_node_layout = NULL;

	if(vista_thickness <= 0)
		vista_thickness = GENESIS_EXTERIOR_DEFAULT_VISTA_THICKNESS;

	//Expand constraints and attach to nodes
	if(	!genesisExteriorExpandConnections(input_constraints, node_layout, run_silent)			||
		!genesisExteriorExpandIsBetween(input_constraints, node_layout, run_silent)								||
		!genesisExteriorExpandMinMax(input_constraints, node_layout, run_silent)									||
		!genesisExteriorExpandVertAngle(input_constraints, node_layout, run_silent)								||
		!genesisExteriorExpandVertDir(input_constraints, node_layout, run_silent)								)
	{
		genesisNodesLayoutDestroy(node_layout);

		return NULL;
	}

	//Create a general info object with inited values
	data = genesisCreateExteriorData(geo_data);

	//Add Nature Nodes
	random_table = mersenneTableCreate(node_layout->seed);
	nature_start = eaSize(&node_layout->nodes);
	if(add_nature)
		genesisExteriorAddNaturePoints(data, node_layout, random_table);
	sub_nature_start = eaSize(&node_layout->nodes);
	if(add_nature)
		genesisExteriorAddNatureSubPoints(data, node_layout, nature_start, random_table);

	//Sort the list so that all the parents come first
	for( i=0; i < eaSize(&node_layout->nodes) ; i++ )
	{
		genesisExteriorAddToSortedList(&sorted_nodes, node_layout->nodes[i]);
	}
	assert(eaSize(&sorted_nodes) == eaSize(&node_layout->nodes));//Are you sending a room_layout that has already been used?

	//Place Nodes
	setVec2(min_bound, -0.01, -0.01);
	setVec2(max_bound, 0.01, 0.01);
	setVec2(play_offset, -0.01, -0.01);
	setVec2(play_size, 0.02, 0.02);
	setVec2(play_center, 0.0, 0.0);
	for( i=0; i < eaSize(&sorted_nodes) ; i++ )
	{
		if(	sorted_nodes[i]->node_type == GENESIS_NODE_Nature &&
			sorted_nodes[i]->nature_parent &&
			!sorted_nodes[i]->nature_parent->placed)
			continue;

		//DEBUG CODE
		if (enable_debug_images > 1 ||
			(enable_debug_images > 0 && i < nature_start))
		{
			int x, z;
			U8 *buffer;
			char file_path[255];
			Vec2 this_node_pos;
			Vec2 chk_node_pos;
			Vec2 line;
			int debug_size[2];

			debug_size[0] = play_size[0] - 2*sorted_nodes[i]->actual_size;
			debug_size[1] = play_size[1] - 2*sorted_nodes[i]->actual_size;
			debug_size[0] >>= DEBUG_SHIFT;
			debug_size[1] >>= DEBUG_SHIFT;

			if(debug_size[0] != 0 && debug_size[1] != 0)
			{
				buffer = malloc(4*debug_size[0]*debug_size[1]);
				for( z=0; z < debug_size[1] ; z++ )
				{
					for( x=0; x < debug_size[0] ; x++ )
					{
						bool can_place = true;
						bool object_in_way = false;
						//Check placement
						sorted_nodes[i]->pos[0] = (x<<DEBUG_SHIFT) + play_offset[0];
						sorted_nodes[i]->pos[2] = ((debug_size[1]-z-1)<<DEBUG_SHIFT) + play_offset[1];
						sorted_nodes[i]->pos[1] = 0.0f;

						buffer[(x + z*debug_size[0])*4 + 0] = 0;
						buffer[(x + z*debug_size[0])*4 + 1] = 0;
						buffer[(x + z*debug_size[0])*4 + 2] = 0;
						for( j=0; j < i ; j++ )
						{
							F32 dist;
							this_node_pos[0] = sorted_nodes[i]->pos[0];
							this_node_pos[1] = sorted_nodes[i]->pos[2];
							chk_node_pos[0] = sorted_nodes[j]->pos[0];
							chk_node_pos[1] = sorted_nodes[j]->pos[2];
							subVec2(this_node_pos, chk_node_pos, line);
							dist = fsqrt(SQR(line[0])+SQR(line[1]));
							if(dist < (sorted_nodes[j]->actual_size))
							{
								object_in_way = true;
								buffer[(x + z*debug_size[0])*4 + 0] = 128;
								buffer[(x + z*debug_size[0])*4 + 1] = 0;
								buffer[(x + z*debug_size[0])*4 + 2] = 255;
								break;
							}
							else if(dist < (sorted_nodes[i]->actual_size + sorted_nodes[j]->actual_size))
							{
								can_place = false;
								buffer[(x + z*debug_size[0])*4 + 0] = 25;
								buffer[(x + z*debug_size[0])*4 + 1] = 25;
								buffer[(x + z*debug_size[0])*4 + 2] = 25;
								break;
							}
						}

						if(!object_in_way)
						{
							F32 min_height = -5000.0f;
							F32 max_height =  5000.0f;
							bool height_constraint=false;
							for( j=0; j < eaSize(&sorted_nodes[i]->final_constraints) ; j++ )
							{
								genesisExteriorPlaceNodeFunc place_func = genesisExteriorGetPlaceFunc(sorted_nodes[i]->final_constraints[j]->function);
								if(place_func && !place_func(sorted_nodes[i], sorted_nodes[i]->final_constraints[j], random_table, &min_height, &max_height, &height_constraint))
								{
									can_place = false;
									buffer[(x + z*debug_size[0])*4 + 0] = MIN(255, buffer[(x + z*debug_size[0])*4 + 0] + 25);
									buffer[(x + z*debug_size[0])*4 + 1] = MIN(255, buffer[(x + z*debug_size[0])*4 + 1] + 25);
									buffer[(x + z*debug_size[0])*4 + 2] = MIN(255, buffer[(x + z*debug_size[0])*4 + 2] + 25);
								}
							}
							if(max_height < min_height)
							{
								can_place = false;
								buffer[(x + z*debug_size[0])*4 + 2] = MIN(255, buffer[(x + z*debug_size[0])*4 + 2] + 100);
							}
							if(!can_place)
							{
								buffer[(x + z*debug_size[0])*4 + 0] = MIN(255, buffer[(x + z*debug_size[0])*4 + 0] + 50);
								buffer[(x + z*debug_size[0])*4 + 1] = MIN(255, buffer[(x + z*debug_size[0])*4 + 1] + 50);
								buffer[(x + z*debug_size[0])*4 + 2] = MIN(255, buffer[(x + z*debug_size[0])*4 + 2] + 50);
							}
						}
					}
				}

				if (i > 0)
				{
					sprintf(file_path, "C:\\NODE_DEBUG\\%d", node_layout->seed);
					makeDirectoriesForFile(file_path);
					sprintf(file_path, "C:\\NODE_DEBUG\\%d\\Step_%03d_%03d_%d.tga", node_layout->seed, i, sorted_nodes[i]->uid ,node_layout->seed);
					tgaSave(file_path, buffer, debug_size[0], debug_size[1], 3);
					free(buffer);
				}
			}
		}

		//Attempt to place the node
		if(!genesisExteriorPlaceNode(data,
			sorted_nodes[i], 
			play_offset[0] + sorted_nodes[i]->actual_size, 
			play_offset[1] + sorted_nodes[i]->actual_size, 
			play_size[0] - 2*sorted_nodes[i]->actual_size, 
			play_size[1] - 2*sorted_nodes[i]->actual_size, 
			0.5f, 0.5f, 
			MAX(play_size[0], play_size[1]) - 2*sorted_nodes[i]->actual_size, 
			random_table, sorted_nodes, i))
		{
			if(sorted_nodes[i]->node_type == GENESIS_NODE_Nature)
			{
				if(i < sub_nature_start)
					i = sub_nature_start-1;
				continue;
			}
			eaDestroy(&sorted_nodes);
			mersenneTableFree(random_table);
			genesisNodesLayoutDestroy(node_layout);
			genesisDestroyExteriorData(data);
			return NULL;
		}

		sorted_nodes[i]->placed = true;

		//Keep track of the lowest point overall
		if(sorted_nodes[i]->pos[1] < total_min_height)
			total_min_height = sorted_nodes[i]->pos[1];

		//The next code is for play area points only
		if(sorted_nodes[i]->node_type == GENESIS_NODE_Nature)
			continue;

		//Keep track of the lowest point for just the play area
		if(sorted_nodes[i]->pos[1] < play_min_height)
			play_min_height = sorted_nodes[i]->pos[1];

		//Calculate the new bounds
		if(min_bound[0] > (sorted_nodes[i]->pos[0] - sorted_nodes[i]->actual_size))
			min_bound[0] = (sorted_nodes[i]->pos[0] - sorted_nodes[i]->actual_size);
		if(min_bound[1] > (sorted_nodes[i]->pos[2] - sorted_nodes[i]->actual_size))
			min_bound[1] = (sorted_nodes[i]->pos[2] - sorted_nodes[i]->actual_size);
		if(max_bound[0] < (sorted_nodes[i]->pos[0] + sorted_nodes[i]->actual_size))
			max_bound[0] = (sorted_nodes[i]->pos[0] + sorted_nodes[i]->actual_size);
		if(max_bound[1] < (sorted_nodes[i]->pos[2] + sorted_nodes[i]->actual_size))
			max_bound[1] = (sorted_nodes[i]->pos[2] + sorted_nodes[i]->actual_size);
		//Calculate the new offset and size
		subVec2(max_bound, min_bound, play_size);
		scaleVec2(play_size, 0.5f, play_center);
		addVec2(min_bound, play_center, play_center);
		//If we are switching to non play area nodes next loop
		if (i < eaSize(&sorted_nodes)-1 && sorted_nodes[i+1]->node_type == GENESIS_NODE_Nature)
		{
			F32 move_x = (full_play_size[0]/2.0f) - play_center[0];
			F32 move_z = (full_play_size[1]/2.0f) - play_center[1];
			//Move all the nodes so they are centered in the play area
			for( j=0; j <= i; j++ )
			{
				sorted_nodes[j]->pos[0] += move_x;
				sorted_nodes[j]->pos[2] += move_z;
			}
			move_handled = true;

			if(make_vista)
			{
				//Make room for vista minus a 500 buffer around the edges.
				play_offset[0] = -(vista_thickness*GRID_BLOCK_SIZE)+500.0f;
				play_offset[1] = -(vista_thickness*GRID_BLOCK_SIZE)+500.0f;
				play_size[0] = full_play_size[0]+(2*vista_thickness*GRID_BLOCK_SIZE)-1000.0f;
				play_size[1] = full_play_size[1]+(2*vista_thickness*GRID_BLOCK_SIZE)-1000.0f;
			}
			else
			{
				play_offset[0] = play_buffer;
				play_offset[1] = play_buffer;
				play_size[0] = full_play_size[0]-(2*play_buffer);
				play_size[1] = full_play_size[1]-(2*play_buffer);
			}
		}
		//Otherwise recenter/resize the placeable area around the placed nodes
		else
		{
			play_size[0] = (full_play_size[0]-play_buffer*2)*2 - play_size[0];
			play_size[1] = (full_play_size[1]-play_buffer*2)*2 - play_size[1];
			play_offset[0] = play_center[0] - play_size[0]/2.0f;
			play_offset[1] = play_center[1] - play_size[1]/2.0f;
		}
	}

	//We only need to set these values if they were not set when we got to nature nodes
	if(!move_handled)
	{
		play_offset[0] = (full_play_size[0]/2.0f) - play_center[0];
		play_offset[1] = (full_play_size[1]/2.0f) - play_center[1];
	}
	//Removed non-placed nature nodes and recenter everything as needed.
	for( i=0; i < eaSize(&sorted_nodes) ; i++ )
	{
		if(!sorted_nodes[i]->placed)
		{
			eaFindAndRemove(&node_layout->nodes, sorted_nodes[i]);
			genesisNodeDestroy(sorted_nodes[i]);
		}
		sorted_nodes[i]->pos[1] -= (data->geotype_filled->node_data.no_water ? (total_min_height - 100.0f) : (play_min_height - data->water_offset));
		if(!move_handled)
		{
			sorted_nodes[i]->pos[0] += play_offset[0];
			sorted_nodes[i]->pos[2] += play_offset[1];
		}
		sorted_nodes[i]->pos[0] += full_play_offset[0];
		sorted_nodes[i]->pos[2] += full_play_offset[1];
	}
	eaDestroy(&sorted_nodes);

	// Create Off Map Nodes
	for ( i=0; i < eaSize(&node_layout->nodes); i++ )
	{
		#define GENESIS_OFF_MAP_BUFFER 100
		GenesisNode *node = node_layout->nodes[i];
		Vec2 edge_pos = {0,0};
		GenesisNode *con_node = NULL;
		F32 path_radius;

		if(node->node_type != GENESIS_NODE_OffMap)
			continue;

		// Get a list of connected nodes
		for ( cg=0; cg < eaSize(&node_layout->connection_groups); cg++ )
		{
			GenesisNodeConnectionGroup *connection_group = node_layout->connection_groups[cg];
			for (j = 0; j < eaSize(&connection_group->connections); j++)
			{
				GenesisNode *start_node = NULL, *end_node = NULL;
				GenesisNodeConnection *conn = connection_group->connections[j];
				U32 other_uid;

				if (node->uid == conn->start_uid)
					other_uid = conn->end_uid;
				else if (node->uid == conn->end_uid)
					other_uid = conn->start_uid;
				else
					continue;

				for ( k = 0; k < eaSize(&node_layout->nodes); k++ )
				{
					if(node_layout->nodes[k]->uid == other_uid)
					{
						con_node = node_layout->nodes[k];
						path_radius = conn->radius;
						break;
					}
				}
				break;
			}
		}

		if(!con_node)
		{
			genesisRaiseError(GENESIS_ERROR, 
				genesisMakeTempErrorContextRoom(node->name, node_layout->layout_name), 
				"Out of bound nodes must be connected to another node.");
			continue;
		}

		{
			F32 best_t = -1;
			F32 new_t = -1;
			Vec2 line_start = {con_node->pos[0]-full_play_offset[0], con_node->pos[2]-full_play_offset[1]};
			Vec2 line_end = {node->pos[0]-full_play_offset[0], node->pos[2]-full_play_offset[1]};
			Vec2 dir;
			subVec2(line_end, line_start, dir);
			normalVec2(dir);

			if(dir[0])
			{
				best_t = genesisOffMapBestT((full_play_size[0] - line_start[0])/dir[0], best_t);
				best_t = genesisOffMapBestT(-line_start[0]/dir[0], best_t);
			}
			if(dir[1])
			{
				best_t = genesisOffMapBestT((full_play_size[1] - line_start[1])/dir[1], best_t);
				best_t = genesisOffMapBestT(-line_start[1]/dir[1], best_t);
			}

			best_t -= GENESIS_OFF_MAP_BUFFER;
			best_t -= node->actual_size;

			scaleVec2(dir, best_t, dir);
			addVec2(line_start, dir, edge_pos);
			addVec2(full_play_offset, edge_pos, edge_pos);
		}

		{
			char name_str[256];
			GenesisNodeConnectionGroup *new_connection_group;
			GenesisNodeConnection *new_connection;
			GenesisNode *new_node;

			//New Node
			new_node = StructCreate(parse_GenesisNode);
			StructCopyAll(parse_GenesisNode, node, new_node);
			eaDestroyStruct(&new_node->missions, parse_GenesisRoomMission);
			eaDestroyStruct(&new_node->static_objects, parse_GenesisObject);
			new_node->node_type = GENESIS_NODE_Clearing;
			new_node->uid += 9000;
			new_node->pos[0] = edge_pos[0];
			new_node->pos[2] = edge_pos[1];
			if(new_node->name)
				StructFreeString(new_node->name);
			sprintf(name_str, "OffMap_%s", node->name);
			new_node->name = StructAllocString(name_str);
			new_node->source_context = StructClone( parse_GenesisRuntimeErrorContext, genesisMakeErrorAutoGen(genesisMakeTempErrorContextRoom( "OffMap", node_layout->layout_name )));
			eaPush(&node_layout->nodes, new_node);
			
			//New Connection Group
			new_connection = StructCreate(parse_GenesisNodeConnection);
			new_connection->start_uid = node->uid;
			new_connection->end_uid = new_node->uid;
			new_connection->radius = path_radius;
			new_connection->path = StructCreate(parse_Spline);
			new_connection->to_off_map = true;
			new_connection_group = StructAlloc(parse_GenesisNodeConnectionGroup);
			eaPush(&new_connection_group->connections, new_connection);
			eaPush(&node_layout->connection_groups, new_connection_group);
			new_connection_group->source_context = StructClone( parse_GenesisRuntimeErrorContext, genesisMakeErrorAutoGen(genesisMakeTempErrorContextRoom( "OffMap", node_layout->layout_name )));
			 
			// Edit original node
			node->off_map_uid = new_node->uid;
			REMOVE_HANDLE(node->detail_kit_1.details);
			REMOVE_HANDLE(node->detail_kit_2.details);

			if(genesisNodeLyaoutConnectionGroupCollides(node_layout, new_connection_group))
			{
				mersenneTableFree(random_table);
				genesisDestroyExteriorData(data);
				return NULL;
			}
		}
	}

	// Create road splines
	for ( cg=0; cg < eaSize(&node_layout->connection_groups); cg++ )
	{
		GenesisNodeConnectionGroup *connection_group = node_layout->connection_groups[cg];
		for (i = 0; i < eaSize(&connection_group->connections); i++)
		{
			GenesisNode *start_node = NULL, *end_node = NULL;
			GenesisNodeConnection *conn = connection_group->connections[i];
			for (k = 0; k < eaSize(&node_layout->nodes); k++)
			{
				if (node_layout->nodes[k]->uid == conn->start_uid)
					start_node = node_layout->nodes[k];
				if (node_layout->nodes[k]->uid == conn->end_uid)
					end_node = node_layout->nodes[k];
			}
			if (start_node && end_node)
			{
				F32 path_size = (conn->radius > 0.0f) ? conn->radius : GENESIS_DEFAULT_ROAD_RADIUS;
				F32 start_size = (start_node->node_type == GENESIS_NODE_OffMap ? 0 : start_node->draw_size);
				F32 end_size = (end_node->node_type == GENESIS_NODE_OffMap ? 0 : end_node->draw_size);
				Vec3 up = { 0, 1, 0 };
				Vec3 start_pos, end_pos, delta, interp_pos;
				F32 path_length, max_delta;
				S32 num_points;

				subVec3(end_node->pos, start_node->pos, delta);
				delta[1] = 0;
				normalVec3(delta);
				setVec3(start_pos, start_node->pos[0]+delta[0]*start_size, 
					start_node->pos[1] - (start_node->node_type == GENESIS_NODE_OffMap ? 2 : 0),
					start_node->pos[2]+delta[2]*start_size);
				setVec3(end_pos, end_node->pos[0]-delta[0]*end_size, 
					end_node->pos[1] - (end_node->node_type == GENESIS_NODE_OffMap ? 2 : 0),
					end_node->pos[2]-delta[2]*end_size);

				setVec3(delta, end_pos[2]-start_pos[2], 0, start_pos[0]-end_pos[0]);
				path_length = normalVec3(delta);
				max_delta = GENESIS_PATH_MAX_DELTA * path_length;
				max_delta = MIN(max_delta, MAX(start_node->actual_size, end_node->actual_size));
				//num_points = MAX(MAX(5, path_length/160.f), eaSize(&layout->paths[i]->path_rooms)*2) + 2;
				num_points = MAX(2, path_length/160.f) + 2;
				//printf("Creating %d-point path.\n", num_points);
				splineDestroy(conn->path);
				splineAppendAutoCP(conn->path, start_pos, up, true, 0, path_size);
				for (j = 0; j < num_points-2; j++)
				{
					GenesisNodePathPoint *new_point = StructCreate(parse_GenesisNodePathPoint);
					F32 t = (F32)(j+1)/(num_points-1);
					F32 distance = randomMersenneF32(random_table) * max_delta * (1 - SQR(2*t-1));
					F32 radius = path_size * (1.f + randomMersenneF32(random_table) * GENESIS_ROAD_RADIUS_VARIANCE);
					lerpVec3(end_pos, t, start_pos, interp_pos);
					interp_pos[0] += distance*delta[0];
					interp_pos[2] += distance*delta[2];
					splineAppendAutoCP(conn->path, interp_pos, up, true, 0, radius);
					new_point->offset = t;
					new_point->radius = radius;
					new_point->rel_pos[0] = distance;
					eaPush(&conn->path_points, new_point);
				}
				splineAppendAutoCP(conn->path, end_pos, up, true, 0, path_size);

				//Straighten beginning and end points
				setVec3(delta, end_pos[0]-start_pos[0], end_pos[1]-start_pos[1], end_pos[2]-start_pos[2]);
				normalVec3(delta);
				assert(eafSize(&conn->path->spline_deltas) >= 3);
				copyVec3(delta, conn->path->spline_deltas);
				copyVec3(delta, &conn->path->spline_deltas[eafSize(&conn->path->spline_deltas)-3]);
			}
		}
	}

	mersenneTableFree(random_table);
	genesisDestroyExteriorData(data);
	return node_layout;
}

void genesisExteriorUpdate(ZoneMap *zmap)
{
	int i;
	GenesisZoneExterior *genesis_exterior = zmap->map_info.genesis_data->genesis_exterior;
	if (genesis_exterior && !wl_state.genesis_node_layout)
	{
		GenesisZoneNodeLayout *node_layout;
		GenesisZoneNodeLayout *vista_nodes=NULL;
		int itr = 0;
		U32 seed = (genesis_exterior->layout_seed ? genesis_exterior->layout_seed : zmap->map_info.genesis_data->seed);
		U32 detail_seed = zmap->map_info.genesis_data->detail_seed;

		//If we are not vista ourself, try and find vista nodes;
		if(!genesis_exterior->is_vista)
		{
			for( i=0; i < eaSize(&zmap->map_info.secondary_maps); i++ )
			{
				ZoneMapInfo *secondary_zmap = worldGetZoneMapByPublicName(zmap->map_info.secondary_maps[i]->map_name);

				if(!secondary_zmap)
				{
					genesisRaiseErrorInternal(GENESIS_FATAL_ERROR, "ZoneMap", zmap->map_info.secondary_maps[i]->map_name, "ZoneMap is being used as a secondary map, but it has been deleted." );
					continue;
				}

				if(secondary_zmap->genesis_data && secondary_zmap->genesis_data->genesis_exterior_nodes)
					vista_nodes = secondary_zmap->genesis_data->genesis_exterior_nodes;
				else if(secondary_zmap->backup_genesis_data && secondary_zmap->backup_genesis_data->genesis_exterior_nodes)
					vista_nodes = secondary_zmap->backup_genesis_data->genesis_exterior_nodes;

				if(vista_nodes)
				{
					if(!gimmeDLLQueryIsFileLatest(secondary_zmap->filename))
						genesisRaiseErrorInternal(GENESIS_FATAL_ERROR, "ZoneMap", secondary_zmap->map_name, "You do not have latest on this zone map.  You must have latest on the vistas before you generate a map or the map will be made incorrectly.  It's filename is \"%s\".",
												  secondary_zmap->filename);
					break;
				}
			}
		}

		// Room -> Node
		loadstart_printf("Rooms -> Nodes...");
		while(!(node_layout = genesisExteriorMoveRoomsToNodes(genesis_exterior, seed, detail_seed, genesisGetCurrentStage()==NULL || genesisStageHasErrors(GENESIS_ERROR), vista_nodes==NULL, true)))
		{
			if(genesisStageFailed())
			{
				itr = 101;
				break;
			}
			printf(".");
			seed++;
			itr++;
			if(itr > 100)
			{
				if(genesisGetCurrentStage()==NULL)
					ErrorFilenamef(zmapGetFilename(zmap), "No solution found for this room layout");
				else
					genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(genesis_exterior->layout_name), "No solution found for this room layout");
				break;
			}
		}
		if (itr <= 100)
		{
			Vec2 min_pos, max_pos;
			copyVec2(genesis_exterior->play_min, min_pos);
			copyVec2(genesis_exterior->play_max, max_pos);
			if(vista_nodes)
			{
				GenesisGeotype *geotype = GET_REF(node_layout->geotype);
				genesisMakeNodeBorders(node_layout, vista_nodes, min_pos, max_pos);
				if(geotype && geotype->node_data.no_water)
					genesisCenterNodesInsideBorders(node_layout);
			}

			loadend_printf("Done.");

			wl_state.genesis_node_layout = StructClone(parse_GenesisZoneNodeLayout, node_layout);
			if (zmap->map_info.genesis_data->genesis_exterior_nodes && zmapGenesisDataLocked(NULL))
			{
				StructDestroy(parse_GenesisZoneNodeLayout, zmap->map_info.genesis_data->genesis_exterior_nodes);
				zmap->map_info.genesis_data->genesis_exterior_nodes = StructClone(parse_GenesisZoneNodeLayout, node_layout);
				StructDestroy(parse_GenesisZoneExterior, zmap->map_info.genesis_data->genesis_exterior);
				zmap->map_info.genesis_data->genesis_exterior = NULL;
				zmapInfoSetModified(&zmap->map_info);
			}
		}
		else
		{
			loadend_printf("Failed.");
		}
	}
	else if (zmap->map_info.genesis_data->genesis_exterior_nodes)
	{
		wl_state.genesis_node_layout = StructClone(parse_GenesisZoneNodeLayout, zmap->map_info.genesis_data->genesis_exterior_nodes);
	}
}

void genesisExteriorPopulateLayer(int iPartitionIdx, ZoneMap *zmap, GenesisMissionRequirements **mission_reqs, ZoneMapLayer *layer)
{
	GenesisToPlaceState to_place = { 0 };
	GenesisEcosystem *ecosystem;

	genesisExteriorUpdate(zmap);

	if (wl_state.genesis_node_layout && !wl_state.genesis_node_layout->is_vista)
	{
		// Object placement
		genesisNodeDoObjectPlacement(iPartitionIdx, zmap->map_info.genesis_data, &to_place, wl_state.genesis_node_layout, mission_reqs, true,
									 wl_state.genesis_node_layout->no_sharing_detail);

		ecosystem = GET_REF(wl_state.genesis_node_layout->ecosystem);
		if (!ecosystem)
		{
			ErrorFilenamef(zmapGetFilename(zmap), "Zmap references invalid ecosystem.");
			return;
		}

		genesisMakeDetailObjects(&to_place, ecosystem, wl_state.genesis_node_layout, false, true);
		genesisPopulateWaypointVolumes(&to_place, mission_reqs);
		genesisPlaceObjects(zmapGetInfo(zmap), &to_place, layer->grouptree.root_def);
	}
}

void genesisExteriorPaintTerrain(int iPartitionIdx, ZoneMap *zmap, TerrainEditorSourceLayer *source_layer, TerrainTaskQueue *queue, int flags, bool in_editor)
{
	GenesisEcosystem *ecosystem = NULL;

	if (!zmap->map_info.genesis_data)
		return;
	if (!wl_state.genesis_node_layout)
		return;

	if (eaSize(&source_layer->blocks) == 0)
	{
		if (wl_state.genesis_node_layout->is_vista)
		{
			IVec2 min_block, max_block;
			GenesisConfig* config = genesisConfig();
			int vista_thickness = SAFE_MEMBER(config, vista_thickness);
			int vista_hole_size = SAFE_MEMBER(config, vista_hole_size);
			if(vista_thickness <= 0)
				vista_thickness = GENESIS_EXTERIOR_DEFAULT_VISTA_THICKNESS;
			if(vista_hole_size <= 0)
				vista_hole_size = GENESIS_EXTERIOR_DEFAULT_VISTA_HOLE_SIZE;

			setVec2(min_block, -vista_thickness, -vista_thickness);
			setVec2(max_block, vista_hole_size-1, -1);
			terrainSourceCreateBlock(iPartitionIdx, source_layer, min_block, max_block, false, true); 
			setVec2(min_block, vista_hole_size, -vista_thickness);
			setVec2(max_block, vista_hole_size-1+vista_thickness, vista_hole_size-1);
			terrainSourceCreateBlock(iPartitionIdx, source_layer, min_block, max_block, false, true); 
			setVec2(min_block, -vista_thickness, 0);
			setVec2(max_block, -1, vista_hole_size-1+vista_thickness);
			terrainSourceCreateBlock(iPartitionIdx, source_layer, min_block, max_block, false, true); 
			setVec2(min_block, 0, vista_hole_size);
			setVec2(max_block, vista_hole_size-1+vista_thickness, vista_hole_size-1+vista_thickness);
			terrainSourceCreateBlock(iPartitionIdx, source_layer, min_block, max_block, false, true); 
		}
		else
		{
			IVec2 min_block, max_block;
			genesisExteriorGetBlockExtents(zmap->map_info.genesis_data, min_block, max_block);
			terrainSourceCreateBlock(iPartitionIdx, source_layer, min_block, max_block, false, true); 
		}
	}
	if (!in_editor)
	{
		source_layer->source->visible_lod = source_layer->source->editing_lod = source_layer->loaded_lod;
		source_layer->layer->layer_mode = source_layer->effective_mode = LAYER_MODE_EDITABLE;
	}

	if (zmap->genesis_view_type > GENESIS_VIEW_NODES)
	{
		int resolution = genesis_exterior_resolution;
		loadstart_printf("Resampling...");

		if(wl_state.genesis_node_layout->is_vista)
			resolution = 4;
		terrainSourceLayerResample(source_layer, resolution);
		terrainSourceSetVisibleLOD(source_layer->source, resolution);
		source_layer->source->editing_lod = resolution;

		loadend_printf("Done.");

		terrainBrushInit();

		loadstart_printf("Running Genesis System...");

		ecosystem = GET_REF(wl_state.genesis_node_layout->ecosystem);
		if (!ecosystem)
		{
			ErrorFilenamef(zmapGetFilename(zmap), "Zmap references invalid ecosystem.");
			return;
		}

		// Node -> Design
		genesisMoveNodesToDesign(queue, source_layer->source, wl_state.genesis_node_layout, flags);

		if (zmap->genesis_view_type > GENESIS_VIEW_WHITEBOX)
		{
			// Design - >Detail
			genesisMoveDesignToDetail(queue, source_layer->source, ecosystem, flags, NULL, NULL);
		}

		//StructDestroy(parse_GenesisZoneNodeLayout, vista_nodes);
		loadend_printf("Done.");
	}
}

void genesisExteriorGetBlockExtents(GenesisZoneMapData *data, IVec2 min_block, IVec2 max_block)
{
	Vec2 min, max;
	if (data->genesis_exterior)
	{
		copyVec2(data->genesis_exterior->play_min, min);
		copyVec2(data->genesis_exterior->play_max, max);
	}
	else if (data->genesis_exterior_nodes)
	{
		copyVec2(data->genesis_exterior_nodes->play_min, min);
		copyVec2(data->genesis_exterior_nodes->play_max, max);
	}
	else
	{
		setVec2(min_block, 0, 0);
		setVec2(max_block, 0, 0);
		return;
	}
	min_block[0] = floor(min[0]/GRID_BLOCK_SIZE);
	max_block[0] = floor((max[0]-1)/GRID_BLOCK_SIZE);
	if (max_block[0] < min_block[0])
		max_block[0] = min_block[0];
	min_block[1] = floor(min[1]/GRID_BLOCK_SIZE);
	max_block[1] = floor((max[1]-1)/GRID_BLOCK_SIZE);
	if (max_block[1] < min_block[1])
		max_block[1] = min_block[1];
}

#endif

/// Fixup function for GenesisExteriorLayout
TextParserResult fixupGenesisExteriorLayout(GenesisExteriorLayout *pExterior, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_CONSTRUCTOR:
			if(nearSameVec2(pExterior->play_min, pExterior->play_max))
			{
				setVec2same(pExterior->play_min, 0);
				setVec2same(pExterior->play_max, DEFAULT_EXTERIOR_PLAYFIELD_SIZE);
			}
		break;
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup tags into new format
			{
				if( pExterior->info.old_geotype_tags ) {
					eaDestroyEx( &pExterior->info.geotype_tag_list, StructFreeString );
					DivideString( pExterior->info.old_geotype_tags, ",", &pExterior->info.geotype_tag_list,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pExterior->info.old_geotype_tags );
				}
				if( pExterior->info.old_ecosystem_tags ) {
					eaDestroyEx( &pExterior->info.ecosystem_tag_list, StructFreeString );
					DivideString( pExterior->info.old_ecosystem_tags, ",", &pExterior->info.ecosystem_tag_list,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pExterior->info.old_ecosystem_tags );
				}

				fixupGenesisDetailKitLayout( &pExterior->detail_kit_1, eType, pExtraData );
			}
		}
	}
	
	return PARSERESULT_SUCCESS;
}

#include "wlGenesisExterior_h_ast.c"
