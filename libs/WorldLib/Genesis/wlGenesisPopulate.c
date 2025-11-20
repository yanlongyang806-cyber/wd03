#define GENESIS_ALLOW_OLD_HEADERS
#include "wlGenesis.h"
#include "wlGenesisPopulate.h"
#include "wlGenesisMissions.h"
#include "wlGenesisRoom.h"
#include "wlTerrainSource.h"
#include "wlExclusionGrid.h"
#include "wlGenesisMissions.h"

#include "ObjectLibrary.h"
#include "WorldGrid.h"
#include "wlState.h"
#include "ResourceSearch.h"
#include "error.h"
#include "rand.h"
#include "tga.h"
#include "utils.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "StringCache.h"

#include "wlGenesisPopulate_h_ast.h"
#include "wlGenesis_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

static DictionaryHandle genesis_detail_dict = NULL;

#ifndef NO_EDITORS

#define POPULATE_OBJECT_PLACE_ATTEMPTS 300
#define POPULATE_OBJECT_ATTEMPTS 20
#define POPULATE_GRID_PATH 0xff
#define POPULATE_GRID_BOUNDS 0xfe
#define POPULATE_MAX_ROOM_OBJECTS 1000

#define POPULATE_DEFAULT_PATH_WIDTH 15		// In feet
#define POPULATE_DEFAULT_PATH_HEIGHT 10		// In feet
#define POPULATE_DOOR_RADIUS 20				// In feet
#define POPULATE_SPACING 4.f				// In feet

#define PATROL_POINT_RADIUS 2.f
#define PATROL_POINT_STRIDE 3				// Skip every few points to generate less data

static int genesis_populate_debug_images = 0;
static int genesis_populate_debug_path_images = 0;

typedef enum GenesisDirection
{
	GENESIS_North=0,
	GENESIS_East,
	GENESIS_South,
	GENESIS_West,
} GenesisDirection;

AUTO_COMMAND ACMD_CMDLINE;
void genesisPopulateEnableDebugImages(int enabled)
{
	genesis_populate_debug_images = enabled;
}

AUTO_COMMAND ACMD_CMDLINE;
void genesisPopulateEnableDebugPathImages(int enabled)
{
	genesis_populate_debug_path_images = enabled;
}

typedef struct GenesisPopulateObject
{
	// Data set by calling init
	ExclusionVolumeGroup **volume_groups;
	GenesisPlacementParams *placement;
	GenesisObject *challenge;
	bool is_challenge;
	bool is_door_cap;
	GroupDef *def;
	int multi_exclude_child_count;
	Vec3 bounds[2];
	F32 radius;
	GenesisRuntimeErrorContext* source_context;

	// Optional data set external to init, but before placing
	bool pos_pre_set;		//If true, then will use the value in pre_set_pos instead of picking a random one.
	Vec3 pre_set_pos;
	int plane_id;			//Affinity group checks only work with objects on the same plane
	F32 offset_rot;			//Additional rotation to apply to the object.  GenesisChallengeFacing overwrites this. 

	// Data is valid if placing the object succeeded
	Vec3 pos;
	F32 rot;

	GenesisObjectVolume *volume;
	
} GenesisPopulateObject;

// Note to Shawn:  The patrol POINTS, not the encounter patroling (the patroller?)
typedef struct GenesisPopulatePatrol
{
	GenesisPatrolObject *patrol;
	GenesisPopulateObject *owner_object; //< can be NULL, if the object is not in this room

	Vec3 path_start_pos;
	Vec3 path_end_pos;
} GenesisPopulatePatrol;

typedef struct GenesisPopulateBlob
{
	Vec3 pos;
	F32 radius;
	U8 side_trail : 1;
} GenesisPopulateBlob;

typedef struct GenesisPopulateCell
{
	F32 height;
	U8 bits;
} GenesisPopulateCell;

typedef struct GenesisPopulateCellLowRes
{
	unsigned isWall : 1;
} GenesisPopulateCellLowRes;

typedef struct GenesisPopulateState
{
	GenesisPopulateArea *area;
	MersenneTable *table;
	
	GenesisPopulateCell *buffer;
	int buffer_width;
	int buffer_height;

	bool position_override;

	GenesisPopulateCellLowRes *low_res_buffer;
	int low_res_buffer_width;
	int low_res_buffer_height;
	int low_res_factor;
	
	int debug_index;
	GenesisRuntimeErrorContext* room_source_context;
	bool in_path;
	bool disable_patrol;
	F32 obj_common_room_rot;
	int room_dir;
	GenesisPopulateObject **populate_list;
	GenesisPopulateObject **reachable_list;
	GenesisPopulatePatrol **patrol_list;
	GenesisToPlaceState *to_place;
	GenesisToPlaceObject *parent_group_shared;
	HeightMapExcludeGrid *exclude_grid;
	ExclusionVolumeGroup *door_volume_group;
	ExclusionVolumeGroup *path_volume_group;
	GenesisPopulateBlob **blobs;
} GenesisPopulateState;

typedef struct GenesisPopulatePlaceState
{
	int attempt;
	
	int plane_id;
	int room_rot;
	int multi_rotation;

	int challenge_idx;
	int entrance_idx;
	int exit_idx;
	int door_idx;

	F32 width;
	F32 height;
} GenesisPopulatePlaceState;

typedef struct GenesisPathfindCell
{
	IVec2 pos;	
	int costSoFar;
} GenesisPathfindCell;

typedef struct perimPoint
{
	int x, z;
	int x_dir, z_dir;
	bool invalid;
} perimPoint;

static U64 global_test_num = 1;
static F32 GENESIS_PRIORITY_BIAS = 2;
static bool genesisPopulateEnsurePaths(int iPartitionIdx, GenesisPopulateState *state, GenesisPopulateObject *new_object, bool raise_errors, bool do_pop, const char* debug_suffix);
static F32 genesisDetailDensityScale(int iPartitionIdx, GenesisPopulateState *state);
static void genesisPopulateGetWorldPos(int iPartitionIdx, GenesisPopulateState *state, int x, int z, bool for_challenge, Vec3 out_pos);

//////////////////////////////////////////////////////////////////
// DetailKit Library
//////////////////////////////////////////////////////////////////

static bool genesis_detail_dict_loaded = false;

void genesisReloadDetailKit(const char* relpath, int when)
{
	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, RefSystem_GetDictionaryHandleFromNameOrHandle(GENESIS_DETAIL_DICTIONARY) );
}

AUTO_FIXUPFUNC;
TextParserResult genesisFixupDetailKit(GenesisDetailKit *detail_kit, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			char name[256];
			if (detail_kit->name)
				StructFreeString(detail_kit->name);
			getFileNameNoExt(name, detail_kit->filename);
			detail_kit->name = StructAllocString(name);
		}
	}
	return 1;
}

static bool genesisDetailValidate(const char *file_name, GenesisDetail *detail)
{
	GroupDef *def = objectLibraryGetGroupDefFromRef(&detail->obj, false);
	if(!def)
	{
		ErrorFilenamef(file_name, "Detail Kit references an object that does not exist.  Name (%s) UID (%d)", detail->obj.name_str, detail->obj.name_uid);
		return false;
	}
	return true;
}

bool genesisDetailKitValidate(GenesisDetailKit *detail_kit, const char *file_name_in)
{
	int i;
	bool ret = true;
	const char *file_name = (file_name_in ? file_name_in : detail_kit->filename);
	for ( i=0; i < eaSize(&detail_kit->details); i++ )
	{
		GenesisDetail *detail = detail_kit->details[i];
		if(!genesisDetailValidate(file_name, detail))
			ret = false;
	}
	for ( i=0; i < eaSize(&detail_kit->path_details); i++ )
	{
		GenesisDetail *detail = detail_kit->path_details[i];
		if(!genesisDetailValidate(file_name, detail))
			ret = false;
	}
	return ret;
}

static int genesisDetailKitValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GenesisDetailKit *pDetailKit, U32 userID)
{
	switch(eType)
	{
		case RESVALIDATE_CHECK_REFERENCES:
			genesisDetailKitValidate(pDetailKit, NULL);		
			return VALIDATE_HANDLED;;
	}
	return VALIDATE_NOT_HANDLED;
}

void genesisLoadDetailKitLibrary()
{
	if (!areEditorsPossible() || genesis_detail_dict_loaded)
		return;

	resLoadResourcesFromDisk(genesis_detail_dict, "genesis/detail_kits", ".detailkit", "GenesisDetailKits.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "genesis/detail_kits/*.detailkit", genesisReloadDetailKit);

	genesis_detail_dict_loaded = true;
}

//////////////////////////////////////////////////////////////////
// Pathfinding inside rooms
//////////////////////////////////////////////////////////////////

static bool genesisPopulateGeneratePath(int iPartitionIdx, GenesisPopulateState *state, int startx, int startz, int endx, int endz, F32 target_size_sq, bool perimeter, int **ret_path, int debug_index, int path_num, bool is_distance_capped)
{
	MersenneTable *table = state->table;
	GenesisPopulateCell *buffer = state->buffer;
	int buffer_width = state->buffer_width;
	int buffer_height = state->buffer_height;
	static const S32 dx[] = { -1, 0, 1, 0, -1, 1, 1, -1 };
	static const S32 dz[] = { 0, -1, 0, 1, -1, -1, 1, 1 };
	static const F32 dist[] = { 1, 1, 1, 1, 1.4142f, 1.4142f, 1.4142f, 1.4142f };
	F32 direct_path_length;

	typedef struct GPC
	{
		int x;
		int z;
		F32 heuristic;
		F32 cost;
		F32 dist;
		struct GPC *parent;
	} GPC;

	U8 *debug_buffer = calloc(1, buffer_width*buffer_height*4);
	F32 debug_ref;
	static int debug_num = 0;
	int iter_num = 0;

	int i, j;
	int order[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	GPC **checked = NULL;
	GPC **unchecked = NULL;
	GPC *item, *new_item;

	new_item = calloc(1, sizeof(GPC));
	new_item->x = -1;
	new_item->z = -1;
	new_item->dist = 1e8;
	for (i = 0; i < buffer_width; i++)
		for (j = 0; j < buffer_height; j++)
		{
			U8 tag = buffer[i+j*buffer_width].bits;
			if (tag == 0 || tag == POPULATE_GRID_PATH)
			{
				F32 start_dist = fabs(i-startx) + fabs(j-startz);
				if (start_dist < new_item->dist)
				{
					new_item->x = i;
					new_item->z = j;
					new_item->dist = start_dist;
				}
			}
		}
	new_item->cost = new_item->dist;
	direct_path_length = sqrt((SQR(endx-new_item->x)+SQR(endz-new_item->z)));
	debug_ref = new_item->heuristic = fabs(endx-startx)+fabs(endz-startz);
	new_item->parent = NULL;
	eaPush(&unchecked, new_item);

	if (new_item->x < 0)
	{
		// Couldn't even find a start location
		eaDestroyEx(&unchecked, NULL);
		SAFE_FREE(debug_buffer);
		return false;
	}

	do {
		item = eaPop(&unchecked);
		eaPush(&checked, item);

		randomMersennePermuteN(table, order, 8, 1);
		for (i = 0; i < 8; i++)
		{
			int px = item->x+dx[order[i]];
			int pz = item->z+dz[order[i]];
			bool found = false;
			F32 step_dist = dist[order[i]];
			F32 step_cost = step_dist*2;
			F32 stop_dist = SQR(endx-px) + SQR(endz-pz);

			// We reached our target
			if (stop_dist <= target_size_sq)
			{
				F32 path_length = item->dist;
				do
				{
					setVec3(&debug_buffer[(item->x+item->z*buffer_width)*4], 255, 255, 0);
					if (buffer[item->x+item->z*buffer_width].bits == 0)
						buffer[item->x+item->z*buffer_width].bits = POPULATE_GRID_PATH;
					if(ret_path)
					{
						eaiPush(ret_path, item->x);
						eaiPush(ret_path, item->z);
					}
					item = item->parent;
				} while (item);
				eaDestroyEx(&checked, NULL);
				eaDestroyEx(&unchecked, NULL);

				if (genesis_populate_debug_path_images)
				{
					char filename[256];
					sprintf(filename, "C:/POPULATE/%d/path_%d/final.tga", debug_index, path_num);
					tgaSave(filename, debug_buffer, buffer_width, buffer_height, 3);
				}
				SAFE_FREE(debug_buffer);
				if(path_length > direct_path_length*3 && is_distance_capped)
					return false;
				return true;
			}

			// We walked off the edge
			if (px < 0 || px >= buffer_width || pz < 0 || pz >= buffer_height)
				continue;
			if (state->area->radius > 0)
			{
				Vec3 worldPos;
				Vec3 midpoint = { (state->area->min[0]+state->area->max[0])*0.5f, 0.f, (state->area->min[2]+state->area->max[2])*0.5f };
				genesisPopulateGetWorldPos(iPartitionIdx, state, px, pz, false, worldPos);
				
				if (SQR(worldPos[0]-midpoint[0])+SQR(worldPos[2]-midpoint[2]) > SQR(state->area->radius))
					continue;
			}

			// TomY TODO - make a parameter for doing this...
			// Makes the paths "straighter"
			if (item->parent && (item->x != item->parent->x || item->parent->x != px) &&
				(item->z != item->parent->z || item->parent->z != pz) && !perimeter)
				step_cost *= 4;

			{
				U8 tag = buffer[px+pz*buffer_width].bits;
				if (tag != 0 && tag != POPULATE_GRID_PATH)
					continue;
				if (tag == POPULATE_GRID_PATH && !perimeter)
					step_cost = 1;
			}

			// We've already checked this point
			for (j = 0; j < eaSize(&checked); j++)
				if (checked[j]->x == px && checked[j]->z == pz)
				{
					if (item->cost + step_cost < checked[j]->cost)
					{
						assert(checked[j] != item->parent);
						checked[j]->cost = item->cost + step_cost;
						checked[j]->heuristic = checked[j]->cost + fabs(endx-px) + fabs(endz-pz);
						checked[j]->parent = item;
					}
					found = true;
					break;
				}
			if (found)
				continue;

			for (j = 0; j < eaSize(&unchecked); j++)
				if (unchecked[j]->x == px && unchecked[j]->z == pz)
				{
					if (item->cost + step_cost < unchecked[j]->cost)
					{
						assert(unchecked[j] != item->parent);
						unchecked[j]->cost = item->cost + step_cost;
						unchecked[j]->heuristic = unchecked[j]->cost + fabs(endx-px) + fabs(endz-pz);
						unchecked[j]->parent = item;
					}
					found = true;
					break;
				}
			if (found)
				continue;

			setVec3(&debug_buffer[(px+pz*buffer_width)*4], 255, 0, 0);

			if (genesis_populate_debug_path_images && ((++iter_num) % 16) == 0)
			{
				char filename[256];
				sprintf(filename, "C:/POPULATE/%d/path_%d/iter_%03d.tga", debug_index, path_num, iter_num);
				tgaSave(filename, debug_buffer, buffer_width, buffer_height, 3);
			}

			new_item = calloc(1, sizeof(GPC));
			new_item->x = px;
			new_item->z = pz;
			new_item->cost = item->cost + step_cost;
			new_item->dist = item->dist + step_dist;
			new_item->heuristic = new_item->cost + fabs(endx-px) + fabs(endz-pz);
			new_item->parent = item;

			{
				U8 v = CLAMP(new_item->heuristic - debug_ref, 0, 255);
				setVec3(&debug_buffer[(px+pz*buffer_width)*4], 0, v, step_cost*44);
			}

			for (j = eaSize(&unchecked)-1; j >= 0; j--)
				if (unchecked[j]->heuristic > new_item->heuristic)
				{
					eaInsert(&unchecked, new_item, j+1);
					break;
				}
			if (j < 0)
				eaInsert(&unchecked, new_item, 0);
		}
	} while (eaSize(&unchecked) > 0);

	eaDestroyEx(&checked, NULL);
	eaDestroyEx(&unchecked, NULL);

	if (genesis_populate_debug_path_images)
	{
		char filename[256];
		sprintf(filename, "C:/POPULATE/%d/path_%d/failed.tga", debug_index, path_num);
		tgaSave(filename, debug_buffer, buffer_width, buffer_height, 3);
	}
	SAFE_FREE(debug_buffer);
	return false;
}

static void genesisWriteDebug(char *file_path, GenesisPopulateCell *buffer, int buffer_width, int buffer_height)
{
	int x, z;
	U8 *mybuffer = calloc(1, buffer_width*buffer_height*4);
	for (z = 0; z < buffer_height; z++)
		for (x = 0; x < buffer_width; x++)
		{
			U8 tag = buffer[x+z*buffer_width].bits;
			if (tag == POPULATE_GRID_PATH)
			{
				setVec3(&mybuffer[(x+z*buffer_width)*4], 0, 255, 255);
			}
			else if (tag > 0)
			{
				setVec3(&mybuffer[(x+z*buffer_width)*4], 128, tag, 0);
			}
		}
	tgaSave(file_path, mybuffer, buffer_width, buffer_height, 3);
	SAFE_FREE(mybuffer);
}

static void genesisWriteDebugLowRes(char *file_path, GenesisPopulateCellLowRes *buffer, int buffer_width, int buffer_height)
{
	int x, z;
	U8 *mybuffer;
	
	if (!buffer || buffer_width < 0 || buffer_height < 0 ) {
		return;
	}
	
	mybuffer = calloc(1, buffer_width*buffer_height*4);
	for (z = 0; z < buffer_height; z++)
		for (x = 0; x < buffer_width; x++)
		{
			GenesisPopulateCellLowRes* cell = &buffer[x+z*buffer_width];

			if (cell->isWall)
			{
				setVec3(&mybuffer[(x+z*buffer_width)*4], 128, 255, 0);
			}
		}
	tgaSave(file_path, mybuffer, buffer_width, buffer_height, 3);
	SAFE_FREE(mybuffer);
}

static void genesisPopulateGetGridPos(GenesisPopulateState *state, Vec3 pos, int *out_x, int *out_z)
{
	*out_x = (pos[0]-state->area->min[0])/POPULATE_SPACING;
	*out_z = (pos[2]-state->area->min[2])/POPULATE_SPACING;
}

static void genesisPopulateGetWorldPos(int iPartitionIdx, GenesisPopulateState *state, int x, int z, bool for_challenge, Vec3 out_pos)
{
	Mat4 pos_matrix;
	identityMat4( pos_matrix );
	setVec3(pos_matrix[3],
			x*POPULATE_SPACING+state->area->min[0],
			state->area->min[1],
			z*POPULATE_SPACING+state->area->min[2]);

	pos_matrix[3][1] = exclusionCalculateObjectHeightDefault(state->exclude_grid, pos_matrix, PATROL_POINT_RADIUS, global_test_num++, iPartitionIdx, for_challenge);
	copyVec3(pos_matrix[3], out_pos);
}

static bool genesisPopulateFindPerimPointPos(GenesisPopulateState *state, perimPoint *point)
{
	int c_x = state->buffer_width / 2;
	int c_z = state->buffer_height / 2;
	F32 dist_sqr = SQR(c_x - point->x) + SQR(c_z - point->z);
	float radius = state->area->radius / POPULATE_SPACING;
	//Keep moving till we get too close to center
	while(dist_sqr > SQR(POPULATE_SPACING*4))
	{
		U8 pos_val = state->buffer[point->x+point->z*state->buffer_width].bits;
		//If we found a good location
		if(pos_val == 0
		   && (state->area->radius <= 0 || dist_sqr < SQR(radius)))
		{
			int next_x = point->x + point->x_dir;
			int next_z = point->z + point->z_dir;
			pos_val = state->buffer[next_x+next_z*state->buffer_width].bits;
			//Move inwards as far as we can
			while (pos_val == 0 && dist_sqr > SQR(POPULATE_SPACING*4))
			{
				point->x += point->x_dir;
				point->z += point->z_dir;
				next_x = point->x + point->x_dir;
				next_z = point->z + point->z_dir;
				dist_sqr = SQR(c_x - point->x) + SQR(c_z - point->z);
				pos_val = state->buffer[next_x+next_z*state->buffer_width].bits;
			}
			return true;
		}
		point->x += point->x_dir;
		point->z += point->z_dir;
		dist_sqr = SQR(c_x - point->x) + SQR(c_z - point->z);
	}
	//We did not find a location
	point->invalid = true;
	return false;
}

static bool genesisPopulatePerimeterPatrol(int iPartitionIdx, GenesisPopulateState *state, GenesisPopulatePatrol *pat_obj, bool raise_errors, bool do_placement)
{
	#define GEN_POP_NUM_PNT 8
	int found_cnt = 0;
	perimPoint perim_points[GEN_POP_NUM_PNT+1];//If you want to save one sizeof(perimPoint) in memory, turn off static checker
	memset(perim_points, 0, sizeof(perim_points));

	if (!pat_obj->owner_object)
		return false;

	//Init points
	//Up
	perim_points[0].x = state->buffer_width / 2;
	perim_points[0].z = state->buffer_height-1;
	perim_points[0].z_dir = -1;
	found_cnt += (genesisPopulateFindPerimPointPos(state, &perim_points[0]) ? 1 : 0);
	//Right/Up
	perim_points[1].x = state->buffer_width-1;
	perim_points[1].z = state->buffer_height-1;
	perim_points[1].x_dir = -1;
	perim_points[1].z_dir = -1;
	found_cnt += (genesisPopulateFindPerimPointPos(state, &perim_points[1]) ? 1 : 0);
	//Right
	perim_points[2].x = state->buffer_width-1;
	perim_points[2].z = state->buffer_height / 2;
	perim_points[2].x_dir = -1;
	found_cnt += (genesisPopulateFindPerimPointPos(state, &perim_points[2]) ? 1 : 0);
	//Right/Bottom
	perim_points[3].x = state->buffer_width-1;
	perim_points[3].z = 0;
	perim_points[3].x_dir = -1;
	perim_points[3].z_dir = 1;
	found_cnt += (genesisPopulateFindPerimPointPos(state, &perim_points[3]) ? 1 : 0);
	//Down
	perim_points[4].x = state->buffer_width / 2;
	perim_points[4].z = 0;
	perim_points[4].z_dir = 1;
	found_cnt += (genesisPopulateFindPerimPointPos(state, &perim_points[4]) ? 1 : 0);
	//Left/Bottom
	perim_points[5].x = 0;
	perim_points[5].z = 0;
	perim_points[5].x_dir = 1;
	perim_points[5].z_dir = 1;
	found_cnt += (genesisPopulateFindPerimPointPos(state, &perim_points[5]) ? 1 : 0);
	//Left
	perim_points[6].x = 0;
	perim_points[6].z = state->buffer_height / 2;
	perim_points[6].x_dir = 1;
	found_cnt += (genesisPopulateFindPerimPointPos(state, &perim_points[6]) ? 1 : 0);
	//Left/Up
	perim_points[7].x = 0;
	perim_points[7].z = state->buffer_height-1;
	perim_points[7].x_dir = 1;
	perim_points[7].z_dir = -1;
	found_cnt += (genesisPopulateFindPerimPointPos(state, &perim_points[7]) ? 1 : 0);

	if(found_cnt > 2)
	{
		int i;
		WorldPatrolPointProperties **patrol_points=NULL;
		bool first = true;
		int first_x, first_z;
		int x_1, z_1;

		for( i=0; i < GEN_POP_NUM_PNT+1; i++ )
		{
			int *path = NULL;
			int path_size;
			int x_2, z_2;
			int pat_pt;

			if(i < GEN_POP_NUM_PNT)
			{
				if(perim_points[i].invalid)
					continue;
				if(first)
				{
					first_x = x_1 = perim_points[i].x;
					first_z = z_1 = perim_points[i].z;
					first = false;
					continue;
				}
				x_2 = perim_points[i].x;
				z_2 = perim_points[i].z;
			}
			else
			{
				x_2 = first_x;
				z_2 = first_z;
			}

			if(!genesisPopulateGeneratePath(iPartitionIdx, state, x_1, z_1, x_2, z_2, 0, true, &path, 0, 0, true))
			{
				if(raise_errors)
					genesisRaiseError(GENESIS_ERROR, pat_obj->patrol->owner_challenge->source_context,
									  "Could not find path for patrol." );
				eaiDestroy(&path);
				eaDestroy(&patrol_points);
				return false;
			}
			path_size = eaiSize(&path);
			assert(path_size > 0 && path_size%2 == 0);
			//Ignore the first point so that we don't get duplicate points
			for (pat_pt = path_size-4; pat_pt >= 0; pat_pt -= PATROL_POINT_STRIDE * 2)
			{
				WorldPatrolPointProperties *patrol_point = StructCreate(parse_WorldPatrolPointProperties);
				genesisPopulateGetWorldPos(iPartitionIdx, state, path[pat_pt + 0], path[pat_pt + 1], true, patrol_point->pos);
				patrol_point->pos[1] += 5.0f;
				eaPush(&patrol_points, patrol_point);
			}
			eaiDestroy(&path);
			x_1 = x_2;
			z_1 = z_2;
		}
		if(!do_placement)
		{
			eaDestroy(&patrol_points);
			return true;
		}
		if(eaSize(&patrol_points) > 0)
		{
			char patrol_name[256];
			GenesisToPlacePatrol *patrol = calloc(1, sizeof(*patrol));
			sprintf(patrol_name, "%s_%02d_Patrol", pat_obj->patrol->owner_challenge->challenge_name, pat_obj->patrol->owner_challenge->challenge_id);
			patrol->patrol_name = StructAllocString(patrol_name);
			patrol->patrol_properties.route_type = PATROL_CIRCLE;
			eaCopy(&patrol->patrol_properties.patrol_points, &patrol_points);
			eaPush(&state->to_place->patrols, patrol);
			eaDestroy(&patrol_points);
		}
		return true;
	}
	if(raise_errors)
		genesisRaiseError(GENESIS_ERROR, pat_obj->patrol->owner_challenge->source_context,
						  "Could not find path for patrol." );
	return false;

	#undef GEN_POP_NUM_PNT
}

static float genesisChallengeGetEffectiveRadius( GenesisPopulateObject* challenge )
{
	float radiusAccum = 0;

	radiusAccum = MAX( radiusAccum, lengthVec3XZ( challenge->def->bounds.min ));
	radiusAccum = MAX( radiusAccum, lengthVec3XZ( challenge->def->bounds.max ));

	return radiusAccum;
}

static bool genesisPopulateChallengesPatrol(int iPartitionIdx, GenesisPopulateState *state, GenesisPopulatePatrol *pat_obj, bool raise_errors, bool do_placement)
{
	int i;
	WorldPatrolPointProperties **patrol_points=NULL;
	int x_1, z_1;

	if (!pat_obj->owner_object)
		return false;

	genesisPopulateGetGridPos(state, pat_obj->owner_object->pos, &x_1, &z_1);
	for( i=0; i < eaSize(&state->populate_list); i++ )
	{
		GenesisPopulateObject *challenge = state->populate_list[i];
		int *path = NULL;
		int path_size;
		int x_2, z_2;
		int pat_pt;

		//Only do challenges and only ones that are not me
		if(!SAFE_MEMBER(challenge->challenge, challenge_name) && challenge != pat_obj->owner_object)
			continue;

		genesisPopulateGetGridPos(state, challenge->pos, &x_2, &z_2);
		{
			float effective_radius = genesisChallengeGetEffectiveRadius( challenge );			
			if(!genesisPopulateGeneratePath(iPartitionIdx, state, x_1, z_1, x_2, z_2,
											SQR(effective_radius/POPULATE_SPACING)+1,
											false, &path, 0, 0, false))
			{
				if(raise_errors)
					genesisRaiseError(GENESIS_ERROR, pat_obj->patrol->owner_challenge->source_context,
									  "Could not find path for patrol." );
				eaiDestroy(&path);
				eaDestroy(&patrol_points);
				return false;
			}
		}
		
		path_size = eaiSize(&path);
		assert(path_size > 0 && path_size%2 == 0);
		//Ignore the first point so that we don't get duplicate points
		for (pat_pt = path_size-4; pat_pt >= 0; pat_pt-= PATROL_POINT_STRIDE * 2)
		{
			WorldPatrolPointProperties *patrol_point = StructCreate(parse_WorldPatrolPointProperties);
			genesisPopulateGetWorldPos(iPartitionIdx, state, path[pat_pt + 0], path[pat_pt + 1], true, patrol_point->pos);
			patrol_point->pos[1] += 5.0f;
			eaPush(&patrol_points, patrol_point);
		}
		eaiDestroy(&path);
		x_1 = x_2;
		z_1 = z_2;
	}
	if(!do_placement)
	{
		eaDestroy(&patrol_points);
		return true;
	}
	if(eaSize(&patrol_points) > 0)
	{
		char patrol_name[256];
		GenesisToPlacePatrol *patrol = calloc(1, sizeof(*patrol));
		sprintf(patrol_name, "%s_%02d_Patrol", pat_obj->patrol->owner_challenge->challenge_name, pat_obj->patrol->owner_challenge->challenge_id);
		patrol->patrol_name = StructAllocString(patrol_name);
		patrol->patrol_properties.route_type = PATROL_PINGPONG;
		eaCopy(&patrol->patrol_properties.patrol_points, &patrol_points);
		eaPush(&state->to_place->patrols, patrol);
		eaDestroy(&patrol_points);
	}
	return true;
}

static bool genesisPopulatePathPatrol(int iPartitionIdx, GenesisPopulateState *state, GenesisPopulatePatrol *pat_obj, bool raise_errors, bool do_placement)
{
	int *path = NULL;
	int path_size;
	int x_1, z_1;
	int x_2, z_2;

	genesisPopulateGetGridPos(state, pat_obj->path_start_pos, &x_1, &z_1);
	genesisPopulateGetGridPos(state, pat_obj->path_end_pos, &x_2, &z_2);
	if(!genesisPopulateGeneratePath(iPartitionIdx, state, x_1, z_1, x_2, z_2, 0, false, &path, 0, 0, false))
	{
		if(raise_errors)
			genesisRaiseError(GENESIS_ERROR, pat_obj->patrol->owner_challenge->source_context,
							  "Could not find path for patrol.");
		eaiDestroy(&path);
		return false;
	}
	if(!do_placement)
	{
		eaiDestroy(&path);
		return true;
	}
	path_size = eaiSize(&path);
	assert(path_size > 0 && path_size%2 == 0);
	{
		int pat_pt;
		char patrol_name[256];
		GenesisToPlacePatrol *new_patrol = calloc(1, sizeof(*new_patrol));
		sprintf(patrol_name, "%s_%02d_Patrol", pat_obj->patrol->owner_challenge->challenge_name, pat_obj->patrol->owner_challenge->challenge_id);
		new_patrol->patrol_name = StructAllocString(patrol_name);
		if( pat_obj->patrol->type == GENESIS_PATROL_Path_OneWay ) {
			new_patrol->patrol_properties.route_type = PATROL_ONEWAY;
		} else {
			new_patrol->patrol_properties.route_type = PATROL_PINGPONG;
		}
		
		if(path_size >= (PATROL_POINT_STRIDE + 1) * 2)
		{

			for (pat_pt = path_size-2; pat_pt >= 0; pat_pt -= PATROL_POINT_STRIDE * 2)
			{
				WorldPatrolPointProperties *patrol_point = StructCreate(parse_WorldPatrolPointProperties);
				genesisPopulateGetWorldPos(iPartitionIdx, state, path[pat_pt + 0], path[pat_pt + 1], true, patrol_point->pos);
				patrol_point->pos[1] += 5.0f;
				eaPush(&new_patrol->patrol_properties.patrol_points, patrol_point);
			}
			if( pat_pt < 0 ) {
				WorldPatrolPointProperties *patrol_point = StructCreate(parse_WorldPatrolPointProperties);
				genesisPopulateGetWorldPos(iPartitionIdx, state, path[0], path[1], true, patrol_point->pos);
				patrol_point->pos[1] += 5.0f;
				eaPush(&new_patrol->patrol_properties.patrol_points, patrol_point);
			}
		}
		else
		{
			WorldPatrolPointProperties* start_patrol_point = StructCreate(parse_WorldPatrolPointProperties);
			WorldPatrolPointProperties* end_patrol_point = StructCreate(parse_WorldPatrolPointProperties);
			genesisPopulateGetWorldPos(iPartitionIdx, state, x_1, z_1, true, start_patrol_point->pos);
			start_patrol_point->pos[1] += 5.0f;
			eaPush(&new_patrol->patrol_properties.patrol_points, start_patrol_point);
			
			genesisPopulateGetWorldPos(iPartitionIdx, state, x_2, z_2, true, end_patrol_point->pos);
			end_patrol_point->pos[1] += 5.0f;
			eaPush(&new_patrol->patrol_properties.patrol_points, end_patrol_point);
		}
		
		eaPush(&state->to_place->patrols, new_patrol);
	}

	eaiDestroy(&path);
	return true;
}

static bool genesisPopulatePatrols(int iPartitionIdx, GenesisPopulateState *state, bool raise_errors, bool do_placement)
{
	int i;
	bool ret = true;

	for( i=0; i < eaSize(&state->patrol_list) && ret; i++ )
	{
		GenesisPopulatePatrol *patrol = state->patrol_list[i];
		
		if(!patrol->patrol)
			continue;
		switch(patrol->patrol->type)
		{
		case GENESIS_PATROL_Path: case GENESIS_PATROL_Path_OneWay:
			if(!genesisPopulatePathPatrol(iPartitionIdx, state, patrol, raise_errors, do_placement))
				ret = false;
			break;
		case GENESIS_PATROL_Challenges:
			if(!genesisPopulateChallengesPatrol(iPartitionIdx, state, patrol, raise_errors, do_placement))
				ret = false;
			break;
		case GENESIS_PATROL_Perimeter:
			if(!genesisPopulatePerimeterPatrol(iPartitionIdx, state, patrol, raise_errors, do_placement))
				ret = false;
			break;
		default:
			break;
		}
	}

	return ret;
}

static bool genesisPopulateCreatePaths(int iPartitionIdx, GenesisPopulateState *state, U32 debug_index, bool raise_errors)
{
	int i, j;
	int num_doors = eaSize(&state->area->doors);
	int num_objects = eaSize(&state->reachable_list);
	int *door_order = NULL;
	int *object_order = NULL;
	int path_index = 0;

	// Randomize door order
	if (num_doors)
	{
		door_order = calloc(1, sizeof(int)*num_doors);
		if (num_objects)
		{
			object_order = calloc(1, sizeof(int)*num_objects);
			for (i = 0; i < num_objects; i++)
				object_order[i] = i;
		}

		for (i = 0; i < num_doors; i++)
			door_order[i] = i;
		randomMersennePermuteN(state->table, door_order, num_doors, 1);
	}

	// Draw paths between doors and objects
	for (i = 0; i < num_doors; i++)
	{
		int door = door_order[i];

		if (num_objects == 0)
		{
			// No objects; draw paths to all other doors
			for (j = 0; j < i; j++)
			{
				int door2 = door_order[j];
				if (!genesisPopulateGeneratePath(iPartitionIdx, state,
												 state->area->doors[door]->gridx, state->area->doors[door]->gridz,
												 state->area->doors[door2]->gridx, state->area->doors[door2]->gridz, SQR(POPULATE_DOOR_RADIUS/POPULATE_SPACING)+1, false, NULL, debug_index, path_index++,
						false))
				{
					SAFE_FREE(door_order);
					SAFE_FREE(object_order);
					if(raise_errors)
						genesisRaiseError(GENESIS_ERROR, state->room_source_context,
										  "Could not create walkable path in area." );
					return false;
				}
				if (genesis_populate_debug_images)
				{
					char filename[256];
					sprintf(filename, "C:/POPULATE/%d/grid_%d-%d_%d.tga", debug_index, i, door, j);
					genesisWriteDebug(filename, state->buffer, state->buffer_width, state->buffer_height);
				}
			}
		}
		else
		{
			// There are objects; draw paths to each object (doors are then implicitly connected via objects)
			randomMersennePermuteN(state->table, object_order, num_objects, 1);
			for (j = 0; j < num_objects; j++)
			{
				int obj = object_order[j];
				if (!genesisPopulateGeneratePath(iPartitionIdx, state,
												 state->area->doors[door]->gridx, state->area->doors[door]->gridz,
												 (state->reachable_list[obj]->pos[0]-state->area->min[0])/POPULATE_SPACING,
												 (state->reachable_list[obj]->pos[2]-state->area->min[2])/POPULATE_SPACING,
												 SQR(state->reachable_list[obj]->def->bounds.radius/POPULATE_SPACING)+1, false, NULL, debug_index, path_index++,
												 false))
				{
					SAFE_FREE(door_order);
					SAFE_FREE(object_order);
					if(raise_errors)
						genesisRaiseError(GENESIS_ERROR, state->room_source_context, "Could not create walkable path in area." );
					return false;
				}
				if (genesis_populate_debug_images)
				{
					char filename[256];
					sprintf(filename, "C:/POPULATE/%d/grid_%d-%d_%d-%d.tga", debug_index, i, door, j, obj);
					genesisWriteDebug(filename, state->buffer, state->buffer_width, state->buffer_height);
				}
			}
		}
	}
	SAFE_FREE(door_order);
	SAFE_FREE(object_order);
	return true;
}

//////////////////////////////////////////////////////////////////
// Object placement
//////////////////////////////////////////////////////////////////

static void genesisPopulateGetRandomDoors(GenesisPopulateState *state, int *entrance, int *exit, int *door)
{
	int i, j, entrance_count = 0, exit_count = 0;
	*entrance = -1;
	*exit = -1;
	*door = -1;
	for (i = 0; i < eaSize(&state->area->doors); i++)
	{
		if (state->area->doors[i]->exit)
			exit_count++;
		if (state->area->doors[i]->entrance)
			entrance_count++;
	}
	if (entrance_count > 0)
	{
		j = randomMersenneIntRange(state->table, 0, entrance_count-1);
		for (i = 0; i < eaSize(&state->area->doors); i++)
			if (state->area->doors[i]->entrance && j-- == 0)
			{
				*entrance = i;
				break;
			}
	}
	if (exit_count > 0)
	{
		j = randomMersenneIntRange(state->table, 0, exit_count-1);
		for (i = 0; i < eaSize(&state->area->doors); i++)
			if (state->area->doors[i]->exit && j-- == 0)
			{
				*exit = i;
				break;
			}
	}
	if (eaSize(&state->area->doors) > 0)
	{
		*door = randomMersenneIntRange(state->table, 0, eaSize(&state->area->doors)-1);
	}
}

static int genesisPopulateGetSpecificDoor(GenesisPopulateState *state, const char *ref_door_dest_name)
{
	int i;
	for (i = 0; i < eaSize(&state->area->doors); i++)
	{
		GenesisPopulateDoor* door = state->area->doors[i];

		if (door->dest_name && ref_door_dest_name && stricmp(door->dest_name, ref_door_dest_name) == 0)
		{
			return i;
		}
	}

	return -1;
}

static void genesisPopulatePlaceOnDoor(GenesisPopulateState *state, F32 size, int idx, bool min_randomness, Vec3 out_pos)
{
	Vec3 min_place, max_place;
	Vec3 door_pos;
	copyVec3(state->area->doors[idx]->point, door_pos);
	setVec3(min_place, MAX(state->area->min[0]+size, door_pos[0]-POPULATE_DOOR_RADIUS), 0, MAX(state->area->min[2]+size, door_pos[2]-POPULATE_DOOR_RADIUS));
	setVec3(max_place, MIN(state->area->max[0]-size, door_pos[0]+POPULATE_DOOR_RADIUS), 0, MIN(state->area->max[2]-size, door_pos[2]+POPULATE_DOOR_RADIUS));
	if( min_randomness )
	{
		out_pos[0] = 0.5*(max_place[0]-min_place[0]) + min_place[0];
		out_pos[2] = 0.5*(max_place[2]-min_place[2]) + min_place[2];
	}
	else
	{
		out_pos[0] = randomMersennePositiveF32(state->table)*(max_place[0]-min_place[0]) + min_place[0];
		out_pos[2] = randomMersennePositiveF32(state->table)*(max_place[2]-min_place[2]) + min_place[2];
	}
}

static void genesisPopulatePlaceAtNamedLocation(GenesisPopulateState *state, GenesisPopulateObject* object, char *location_name)
{
	GroupDef* prefabGroup = objectLibraryGetGroupDefByName( state->area->prefab_library_piece, false );
	int* indexes = NULL;

	object->rot = 0;

	if (!prefabGroup) {
		genesisRaiseError( GENESIS_ERROR, state->room_source_context, "Could not find prefab group for room." );
		return;
	}

	indexes = groupDefScopeGetIndexesFromName(prefabGroup, location_name);
	if (!indexes) {
		genesisRaiseError( GENESIS_ERROR, object->source_context,
						   "Could not find prefab location '%s' in room '%s'.",
						   location_name, state->room_source_context->location_name );
		return;
	}

	{
		Vec3 posAccum = { 0, 0, 0 };
		float rotAccum = 0;
		GroupDef* defIt = prefabGroup;
		int it;
		for( it = 0; it != eaiSize( &indexes ); ++it ) {
			GroupChild *child = defIt->children[ indexes[ it ]];
			GroupDef *child_def = groupChildGetDef(defIt, child, false);

			addVec3( posAccum, child->pos, posAccum );
			if( child->rot[0] != 0 || child->rot[2] != 0 ) {
				genesisRaiseError( GENESIS_ERROR, state->room_source_context,
								   "Prefab group has non-zero pitch or roll for prefab location '%s'.  This is not supported (yet).",
								   location_name );
			}
			rotAccum += child->rot[1];
			
			assert(child_def);
			defIt = child_def;
		}

		mulVecMat4( posAccum, state->area->room_origin, object->pos );
		{
			Vec3 pyr;
			getMat3YPR( state->area->room_origin, pyr );
			object->rot = rotAccum + pyr[1];
		}
	}

	eaiDestroy( &indexes );
}

static bool genesisPopulateChallengeMatches(GenesisPopulateObject *object, GroupDef *my_def, const char *challenge_to_match)
{
	char* object_name = SAFE_MEMBER(object->challenge, challenge_name);

	if (!object_name) {
		return false;
	}
	
	if (challenge_to_match && challenge_to_match[0])
		return !stricmp(object_name, challenge_to_match);

	return object->def && (object->def != my_def);
}

static bool genesisPopulateAddPlacement(int iPartitionIdx, GenesisPopulatePlaceState *place_state, GenesisPopulateState *state, GenesisPopulateObject *object, ExclusionVolumeGroup *vol_group, Mat4 placement_mat)
{
	int i;
	F32 size = object->radius;
	int plane_id = place_state->plane_id;
	bool ignore_exclusion = (object->placement->location == GenesisChallengePlace_InSpecificDoor || object->placement->location == GenesisChallengePlace_Prefab_Location);

	if (ignore_exclusion)
		return true;
		
	{
		F32 *positions = NULL;
		if (object->is_door_cap)
		{
			eafPush(&positions, placement_mat[3][1]);
		}
		else
		{
			exclusionCalculateObjectHeight(state->exclude_grid, placement_mat, size, global_test_num++, object->is_challenge, iPartitionIdx, &positions, NULL);

			if(eafSize(&positions) == 0)
			{
				if(state->area->volumized)
				{
					eafDestroy(&positions);
					return false;
				}
				else
					eafPush(&positions, placement_mat[3][1]);
			}
		}
	
		randomMersennePermuteFloatN(state->table, positions, eafSize(&positions), 1);
		for ( i=0; i < eafSize(&positions); i++ )
		{
			placement_mat[3][1] = positions[i];
			if (exclusionCheckVolumesInRange(vol_group, placement_mat, state->area->min[0], state->area->min[2], state->area->max[0]-state->area->min[0], state->area->max[2]-state->area->min[2]) &&
				!exclusionGridVolumeGridCollision(state->exclude_grid, placement_mat, size, vol_group->volumes, plane_id, NULL, global_test_num++, NULL))
			{
				eafDestroy(&positions);
				return true;
			}
		}
	}
	return false;
}

F32 genesisClampAngle(F32 angle, F32 min, F32 max)
{
	while(angle < min)
		angle += TWOPI;
	while(angle > max)
		angle -= TWOPI;
	return angle;
}

static genesisPopulateGetMirrorMat(GenesisPopulateState *state, Mat4 placement_mat, F32 object_rot, int room_rot)
{
	//Mirror based on the rotation of the room
	if(room_rot%2)
	{
		F32 room_width = state->area->max[0] - state->area->min[0];
		F32 x_pos = placement_mat[3][0] - state->area->min[0];
		placement_mat[3][0] = (room_width - x_pos) + state->area->min[0];
		object_rot = genesisClampAngle(object_rot, -PI, PI);
		object_rot = -object_rot;
	}
	else
	{
		F32 room_depth = state->area->max[2]-state->area->min[2];
		F32 z_pos = placement_mat[3][2] - state->area->min[2];
		placement_mat[3][2] = (room_depth - z_pos) + state->area->min[2];
		object_rot = genesisClampAngle(object_rot, -PI, PI);
		object_rot = PI-object_rot;
	}
	identityMat3(placement_mat);
	yawMat3(object_rot, placement_mat);
}

static void genesisPopulateAddVolGroupToExclusionGrid(GenesisPopulateState *state, GenesisPopulateObject *object, int plane_id, ExclusionVolumeGroup *vol_group, Mat4 place_mat)
{
	ExclusionObject *exclude;
	exclude = calloc(1, sizeof(ExclusionObject));
	copyMat4(place_mat, exclude->mat);
	exclude->volume_group = vol_group;
	if(SAFE_MEMBER(object->challenge, has_patrol))
		exclude->id = POPULATE_GRID_PATH;
	else
		exclude->id = 1;
	exclude->max_radius = object->radius;
	exclude->plane_id = plane_id;
	exclusionGridAddObject(state->exclude_grid, exclude, object->radius, true);
}

static void genesisPopulateAddToExclusionGrid(GenesisPopulateState *state, GenesisPopulateObject *object, int plane_id, int rand_room_rot)
{
	bool ignore_exclusion = (object->placement->location == GenesisChallengePlace_InSpecificDoor);
	if (!ignore_exclusion)
	{
		int i;
		for ( i=0; i < eaSize(&object->volume_groups); i++ )
		{
			ExclusionVolumeGroup *vol_group = object->volume_groups[i];
			if(!vol_group->was_placed)
				continue;
			genesisPopulateAddVolGroupToExclusionGrid(state, object, plane_id, vol_group, vol_group->final_mat);
			if(object->placement->mirror)
			{
				Mat4 mirror_mat;
				copyMat4(vol_group->final_mat, mirror_mat);
				genesisPopulateGetMirrorMat(state, mirror_mat, object->rot, rand_room_rot);
				genesisPopulateAddVolGroupToExclusionGrid(state, object, plane_id, vol_group, mirror_mat);		
			}
		}
	}
}

static bool genesisHasLineOfSightLowRes(GenesisPopulateState *state, const IVec2 start, const IVec2 end)
{
	int xStart = MIN( start[ 0 ], end[ 0 ]);
	int xEnd = MAX( start[ 0 ], end[ 0 ]);
	int yStart = MIN( start[ 1 ], end[ 1 ]);
	int yEnd = MAX( start[ 1 ], end[ 1 ]);
	
	int xIt;
	int yIt;
	for( yIt = yStart; yIt <= yEnd; ++yIt ) {
		for( xIt = xStart; xIt <= xEnd; ++xIt ) {
			GenesisPopulateCellLowRes* cell = &state->low_res_buffer[ xIt + yIt * state->low_res_buffer_width ];

			if( cell->isWall ) {
				return false;
			}
		}
	}

	return true;
}

static void genesisGetLowResPosXZ(GenesisPopulateState *state, IVec2 out_low_res_pos, Vec3 pos)
{
	float low_res_spacing = POPULATE_SPACING * state->low_res_factor;
	
	// due to placement inaccuracies, the cell could be slightly out of
	// bounds
	float minDistAccum;
	int xIt;
	int yIt;

	setVec2(out_low_res_pos, 0, 0);
	minDistAccum = FLT_MAX;
	for( yIt = 0; yIt != state->low_res_buffer_height; ++yIt ) {
		for( xIt = 0; xIt != state->low_res_buffer_width; ++xIt ) {
			GenesisPopulateCellLowRes* cell = &state->low_res_buffer[xIt + yIt * state->low_res_buffer_width];
			float dx = state->area->min[0] + low_res_spacing * (xIt + 0.5) - pos[0];
			float dy = state->area->min[2] + low_res_spacing * (yIt + 0.5) - pos[2];
			float dist = sqrt( SQR( dx ) + SQR( dy ));
			
			if( !cell->isWall && dist < minDistAccum ) {
				minDistAccum = dist;
				setVec2( out_low_res_pos, xIt, yIt );
			}
		}
	}
}

/// Queue looking at a cell DX, DY from CELL.
static void genesisPathfindVisitCell( int dx, int dy, GenesisPopulateState* state, GenesisPathfindCell* cell, unsigned int low_res_cost[], GenesisPathfindCell*** queue, IVec2 target )
{
	IVec2 newCellPos = { cell->pos[0] + dx, cell->pos[1] + dy };
	
	if(   0 <= newCellPos[0] && newCellPos[0] < state->low_res_buffer_width
		  && 0 <= newCellPos[1] && newCellPos[1] < state->low_res_buffer_height ) {
		GenesisPopulateCellLowRes* newCell = &state->low_res_buffer[newCellPos[0] + newCellPos[1]*state->low_res_buffer_width];
		unsigned int newCost = cell->costSoFar + abs(dx) + abs(dy);
		unsigned int cost = low_res_cost[newCellPos[0] + newCellPos[1]*state->low_res_buffer_width];

		if( !newCell->isWall && cost > newCost ) {
			GenesisPathfindCell* newAStarCell = calloc( 1, sizeof( *cell ));
			int totalCost = newCost + abs(target[0] - newCellPos[0]) + abs(target[1] - newCellPos[1]);
			
			copyVec2( newCellPos, newAStarCell->pos );
			newAStarCell->costSoFar = newCost;

			{
				int insert;
				for( insert = 0; insert != eaSize( queue ); ++insert ) {
					GenesisPathfindCell* other = (*queue)[ insert ];
					int otherTotalCost = other->costSoFar + abs(target[0] - other->pos[0]) + abs(target[1] - other->pos[1]);
					if( totalCost > otherTotalCost ) {
						break;
					}
				}
				eaInsert( queue, newAStarCell, insert );
			}
		}
	}
}

static void genesisPopulateObjectCalcRotationByTarget(F32 *desired_rotation_accum, GenesisPopulateState *state, Vec3 object_pos, Vec3 target_pos, bool away)
{
	/* printf( "Obj: %f, %f, %f; Target: %f, %f, %f;", */
	/* 		object_pos[0], object_pos[1], object_pos[2], */
	/* 		target_pos[0], target_pos[1], target_pos[2] ); */
	
	if (!state->low_res_buffer) {
		Vec3 target_vec;
		subVec3(target_pos, object_pos, target_vec);
		if (away) {
			scaleVec3(target_vec, -1, target_vec);
		}
		
		*desired_rotation_accum = atan2(target_vec[2], -target_vec[0]) - HALFPI;

		/* printf( "NO LOW RES Angle: %f\n", DEG(*desired_rotation_accum)); */
	} else {
		IVec2 low_res_pos;
		IVec2 low_res_target;

		genesisGetLowResPosXZ( state, low_res_pos, object_pos );
		genesisGetLowResPosXZ( state, low_res_target, target_pos );
		
		if( genesisHasLineOfSightLowRes( state, low_res_pos, low_res_target )) {
			Vec3 target_vec;
			subVec3(target_pos, object_pos, target_vec);
			if (away) {
				scaleVec3(target_vec, -1, target_vec);
			}
		
			*desired_rotation_accum = atan2(target_vec[2], -target_vec[0]) - HALFPI;
			
			/* printf( "DIRECT Angle: %f\n", DEG(*desired_rotation_accum)); */
		} else {
			// Do pathfinding to find the optimal path from pos to target
			//
			// TODO: make this use A*
			GenesisPathfindCell** queue = NULL;
			unsigned int* low_res_cost = alloca( sizeof( *low_res_cost ) * state->low_res_buffer_width * state->low_res_buffer_height );
			memset( low_res_cost, 0xFF, sizeof( *low_res_cost ) * state->low_res_buffer_width * state->low_res_buffer_height );

			{
				GenesisPathfindCell* firstCell = calloc( 1, sizeof( *firstCell ));
				copyVec2( low_res_target, firstCell->pos );
				eaPush( &queue, firstCell );
			}
			while( eaSize( &queue ) > 0 ) {
				GenesisPathfindCell* cell = eaPop( &queue );
				low_res_cost[cell->pos[0] + cell->pos[1] * state->low_res_buffer_width] = cell->costSoFar;

				// if we can now see the cell, then we're golden!
				if( genesisHasLineOfSightLowRes( state, low_res_pos, cell->pos )) {
					IVec2 dir;

					subVec2( cell->pos, low_res_pos, dir );

					if (away) {
						scaleVec2( dir, -1, dir );
					}
					*desired_rotation_accum = atan2( dir[1], -dir[0] ) - HALFPI;

					eaDestroyEx( &queue, NULL );

					//printf( "PATHFIND Angle: %f\n", *desired_rotation_accum );
					return;
				}
				
				genesisPathfindVisitCell( 0, -1, state, cell, low_res_cost, &queue, low_res_pos );
				genesisPathfindVisitCell( 0, -1, state, cell, low_res_cost, &queue, low_res_pos );
				genesisPathfindVisitCell( 0, +1, state, cell, low_res_cost, &queue, low_res_pos );
				genesisPathfindVisitCell( -1, 0, state, cell, low_res_cost, &queue, low_res_pos );
				genesisPathfindVisitCell( +1, 0, state, cell, low_res_cost, &queue, low_res_pos );
				free( cell );
			}

			genesisRaiseErrorInternalCode( GENESIS_WARNING, "Pos: %f, %f, %f -- Could not figure out path for object at this position.  This is an internal error." );
			eaDestroyEx( &queue, NULL );
			return;
		}
	}
}

static bool genesisPopulateObjectRotation(GenesisPopulatePlaceState *place_state, GenesisPopulateState* state, GenesisPopulateObject *object)
{
	GroupDef *def = object->def;
	GenesisPlacementParams* placement = object->placement;
	const F32 rotation_increment = RAD(placement->rotation_increment);
	const char* ref_challenge_name = placement->ref_challenge_name;
	F32 desired_rotation = 0;

	// Prefab location also forces rotation
	if(placement->location == GenesisChallengePlace_Prefab_Location)
		return true;

	if(placement->common_room_rot)
		desired_rotation = state->obj_common_room_rot;
	else if(placement->use_room_dir)
		desired_rotation = (state->room_dir * -TWOPI);
	else
		desired_rotation = randomMersennePositiveF32(state->table)*TWOPI;
	desired_rotation += object->offset_rot;

	switch (placement->facing)
	{
		case GenesisChallengeFace_Fixed:
			desired_rotation = 0;
			break;
		case GenesisChallengeFace_Random:
			break;
		case GenesisChallengeFace_Center:
			{
				Vec3 center_pos = { (state->area->min[0]+state->area->max[0])*0.5, object->pos[1], (state->area->min[2]+state->area->max[2])*0.5 };
				genesisPopulateObjectCalcRotationByTarget( &desired_rotation, state, object->pos, center_pos, false );
			}
			break;
		case GenesisChallengeFace_Exit:
			if (place_state->exit_idx != -1)
			{
				genesisPopulateObjectCalcRotationByTarget( &desired_rotation, state, object->pos, state->area->doors[place_state->exit_idx]->point, false );
			}
			break;
		case GenesisChallengeFace_Entrance:
			if (place_state->entrance_idx != -1)
			{
				genesisPopulateObjectCalcRotationByTarget( &desired_rotation, state, object->pos, state->area->doors[place_state->entrance_idx]->point, false );
			}
			break;
		case GenesisChallengeFace_Entrance_Exit:
			if (place_state->door_idx != -1)
			{
				genesisPopulateObjectCalcRotationByTarget( &desired_rotation, state, object->pos, state->area->doors[place_state->door_idx]->point, false );
			}
			break;
		case GenesisChallengeFace_Challenge_Away:
		case GenesisChallengeFace_Challenge_Toward:
			{
				F32 min_distance = 1e8;
				if (place_state->challenge_idx == -1)
				{
					// Find nearest challenge
					int idx;
					for (idx = 0; idx < eaSize(&state->populate_list); idx++)
					{
						if (genesisPopulateChallengeMatches(state->populate_list[idx], def, ref_challenge_name))
						{
							F32 distance = distance3(state->populate_list[idx]->pos, object->pos);
							if (distance < min_distance)
							{
								min_distance = distance;
								place_state->challenge_idx = idx;
							}
						}
					}
				}
				if (place_state->challenge_idx != -1)
				{
					genesisPopulateObjectCalcRotationByTarget( &desired_rotation, state, object->pos, state->populate_list[place_state->challenge_idx]->pos,
															   placement->facing == GenesisChallengeFace_Challenge_Away );
				}
			}
			break;
	}
	desired_rotation += RAD(placement->constant_rotation);

	if (desired_rotation < 0)
		desired_rotation += TWOPI;
	if(desired_rotation >= TWOPI)
		desired_rotation -= TWOPI;

	// Snap to increment if requested
	if (rotation_increment != 0)
		desired_rotation = floor(desired_rotation / rotation_increment) * rotation_increment;

	object->rot = desired_rotation;
	return true;
}

static void genesisPopulateObjectPushToWall(GenesisPopulatePlaceState *place_state, GenesisPopulateState* state, GenesisPopulateObject *object)
{
	// Snap rotation to 90 degree increments
	Vec3 nearest_point, line_end;
	int rotation_amount = floor(object->rot / HALFPI);
	object->rot = rotation_amount * HALFPI;
	object->pos[1] += 0.00001;//Add a little bit so it doesn't collide with the floor
	copyVec3(object->pos, line_end);

	// Snap to edge based on rotation (Assumes object is facing down +z axis)
	switch (rotation_amount)
	{
	case 4:
	case 0:
		line_end[2] = state->area->min[2];
		exclusionCollideLine(state->exclude_grid, object->pos, line_end, EXCLUDE_VOLUME_WALL|EXCLUDE_VOLUME_SUBTRACT, place_state->plane_id, global_test_num++, nearest_point, NULL);
		copyVec3(nearest_point, object->pos);
		if(!object->placement->pivot_on_wall)
			object->pos[2] -= object->bounds[0][2];
		object->pos[2] += 0.001;
		break;
	case 1:
		line_end[0] = state->area->min[0];
		exclusionCollideLine(state->exclude_grid, object->pos, line_end, EXCLUDE_VOLUME_WALL|EXCLUDE_VOLUME_SUBTRACT, place_state->plane_id, global_test_num++, nearest_point, NULL);
		copyVec3(nearest_point, object->pos);
		if(!object->placement->pivot_on_wall)
			object->pos[0] -= object->bounds[0][2];
		object->pos[0] += 0.001;
		break;
	case 2:
		line_end[2] = state->area->max[2];
		exclusionCollideLine(state->exclude_grid, object->pos, line_end, EXCLUDE_VOLUME_WALL|EXCLUDE_VOLUME_SUBTRACT, place_state->plane_id, global_test_num++, nearest_point, NULL);
		copyVec3(nearest_point, object->pos);
		if(!object->placement->pivot_on_wall)
			object->pos[2] += object->bounds[0][2];
		object->pos[2] -= 0.001;
		break;
	case 3:
		line_end[0] = state->area->max[0];
		exclusionCollideLine(state->exclude_grid, object->pos, line_end, EXCLUDE_VOLUME_WALL|EXCLUDE_VOLUME_SUBTRACT, place_state->plane_id, global_test_num++, nearest_point, NULL);
		copyVec3(nearest_point, object->pos);
		if(!object->placement->pivot_on_wall)
			object->pos[0] += object->bounds[0][2];
		object->pos[0] -= 0.001;
		break;
	}
}

static void genesisPopulateObjectSetBounds(GenesisPopulateObject *object)
{
	GroupDef *def = object->def;
	Vec3 min = { def->bounds.min[0], def->bounds.min[1], def->bounds.min[2] };
	Vec3 max = { def->bounds.max[0], def->bounds.max[1], def->bounds.max[2] };
	Vec2 extremes;

	extremes[0] = MAX(fabs(min[0]), fabs(max[0]));
	extremes[1] = MAX(fabs(min[2]), fabs(max[2]));
	object->radius = sqrt(SQR(extremes[0])+SQR(extremes[1]));

	if (def->property_structs.encounter_hack_properties)
	{
		F32 encounter_size = 10;
		if (def->property_structs.encounter_hack_properties->physical_radius > 0)
			encounter_size = def->property_structs.encounter_hack_properties->physical_radius;
		// TomY HACK to make encounters not overlap
		min[0] -= encounter_size;
		min[2] -= encounter_size;
		max[0] += encounter_size;
		max[2] += encounter_size;
	}

	//Will not be used unless it is encounter 2.0
	if(object->challenge)
	{
		F32 extra_size = 2;
		GenesisEncounterJitter *enc_jitter = &object->placement->enc_jitter;
		if(!object->challenge->has_patrol && enc_jitter->jitter_type != GEJT_None)
			extra_size += (enc_jitter->jitter_type == GEJT_Custom ? enc_jitter->enc_pos_jitter : 4.0f);
		min[0] -= extra_size;
		min[2] -= extra_size;
		max[0] += extra_size;
		max[2] += extra_size;
	}

	copyVec3(min, object->bounds[0]);
	copyVec3(max, object->bounds[1]);
}

static void genesisPopulateObjectInit(int iPartitionIdx, GenesisPopulateObject *object, GenesisObject *challenge, GenesisDetail *detail, GenesisPlacementParams *params)
{
	int i;
	GroupDef *def;

	//Setup from challenge object if one is passed in
	if(challenge)
	{
		assert(!detail);//Should only have one or the other
		object->challenge = challenge;
		object->is_challenge = true;
		object->source_context = challenge->source_context;
		object->volume = challenge->volume;

		if (!IS_HANDLE_ACTIVE( challenge->start_spawn_using_transition ))
		{
			object->def = objectLibraryGetGroupDefFromRef( &challenge->obj, true );
		}
		else
		{
			GroupDefRef startSpawnRef;
			startSpawnRef.name_str = "Start Spawn Point";
			object->def = objectLibraryGetGroupDefFromRef(&startSpawnRef, true);
		}
	} else if (detail) {
		GenesisDetailKit *detail_kit = detail->parent_kit->light_details;
		if(!detail_kit)
			detail_kit = GET_REF(detail->parent_kit->details);
		assert(detail_kit);
		
		object->def = detail->def;
		object->source_context = genesisMakeTempErrorContextDictionary( GENESIS_DETAIL_DICTIONARY, detail_kit->name );
	}
	if( !object->source_context ) {
		Errorf( "GenesisPopulateObject missing Genesis Context.  Please notify the Genesis team immediately." );
	}

	object->placement = params;
	def = object->def;
	if(!def)
		return;

	genesisPopulateObjectSetBounds(object);

	if (!object->volume_groups && object->placement->is_start_spawn && wl_state.genesis_get_spawn_pos_func)
	{
		#define GENESIS_MAX_TEAM_SIZE 5
		for ( i=0; i < GENESIS_MAX_TEAM_SIZE; i++ )
		{
			ExclusionVolumeGroup *new_group = calloc(1, sizeof(ExclusionVolumeGroup));
			ExclusionVolume *enc_vol = calloc(1, sizeof(ExclusionVolume));

			new_group->optional = false;

			//Find Matrix
			identityMat4(new_group->mat_offset);
			wl_state.genesis_get_spawn_pos_func(iPartitionIdx, WRT_Ground, i, new_group->mat_offset[3]);

			//Setup properties
			enc_vol->type = EXCLUDE_VOLUME_BOX;
			enc_vol->extents[0][0] = enc_vol->extents[0][2] = -2.0f;
			enc_vol->extents[0][1] = 2.0f;//Little bit off the floor so it can place on ramps
			enc_vol->extents[1][0] = enc_vol->extents[1][2] =  2.0f;
			enc_vol->extents[1][1] = 12.0f;
			enc_vol->begin_radius = 3.0f;
			enc_vol->collides = true;
			enc_vol->is_on_path = true;
			identityMat4(enc_vol->mat);

			eaPush(&new_group->volumes, enc_vol);
			eaPush(&object->volume_groups, new_group);
		}
	}
	if(!object->volume_groups)
	{
		if(object->is_challenge && object->placement->children)
		{
			ExclusionVolumeGroup *group = calloc(1, sizeof(ExclusionVolumeGroup));
			group->idx_in_parent = 0;
			group->optional = true;
			identityMat4(group->mat_offset);

			eaPush(&object->volume_groups, group);

			FOR_EACH_IN_EARRAY_FORWARDS(object->placement->children, GenesisPlacementChildParams, child)
			{
				ExclusionVolumeGroup *vol_group = calloc(1, sizeof(ExclusionVolumeGroup));
				ExclusionVolume *vol = calloc(1, sizeof(ExclusionVolume));

				vol_group->is_actor = true;
				vol_group->optional = false;

				//Find Matrix
				createMat3YPR(vol_group->mat_offset, child->vPyr);
				copyVec3(child->vOffset, vol_group->mat_offset[3]); 
				vol_group->mat_offset[3][1] = 0.0f;

				//Setup properties
				vol->type = EXCLUDE_VOLUME_BOX;
				vol->extents[0][0] = vol->extents[0][2] = -1.0f;
				vol->extents[0][1] = 2.0f;//Little bit off the floor so it can place on ramps
				vol->extents[1][0] = vol->extents[1][2] =  1.0f;
				vol->extents[1][1] = 12.0f;
				vol->begin_radius = 3.0f;
				vol->collides = false;
				vol->is_on_path = true;
				identityMat4(vol->mat);

				eaPush(&vol_group->volumes, vol);
				eaPush(&object->volume_groups, vol_group);
			}
			FOR_EACH_END
		}
	}
	if (!object->volume_groups)
	{
		object->multi_exclude_child_count = exclusionGetDefVolumeGroups(def, &object->volume_groups, object->is_challenge, -1);
		if(object->multi_exclude_child_count > 0 && object->is_challenge)
			genesisRaiseErrorInternalCode(GENESIS_ERROR, OBJECT_LIBRARY_DICT, def->name_str, "Challenges do not support multi excluders.");
	}
	if (!object->volume_groups)
	{
		ExclusionVolumeGroup *volume_group = calloc(1, sizeof(ExclusionVolumeGroup));
		ExclusionVolume *volume = calloc(1, sizeof(ExclusionVolume));
		volume->type = EXCLUDE_VOLUME_BOX;
		copyVec3(def->bounds.min, volume->extents[0]);
		volume->extents[0][1] = MAX(0.0f, volume->extents[0][1]);
		copyVec3(def->bounds.max, volume->extents[1]);
		volume->extents[1][1] = MAX(0.0f, volume->extents[1][1]);
		identityMat4(volume->mat);
		volume->begin_radius = object->radius;
		volume->collides = true;
		if (object->placement->location == GenesisChallengePlace_Entrance ||
			object->placement->location == GenesisChallengePlace_Exit || 
			object->placement->location == GenesisChallengePlace_Entrance_Exit)
			volume->is_on_path = true;
		eaPush(&volume_group->volumes, volume);
		eaPush(&object->volume_groups, volume_group);
	}

	if (object->is_door_cap)
	{
		// Disable collision for door caps.
		FOR_EACH_IN_EARRAY(object->volume_groups, ExclusionVolumeGroup, group)
		{
			FOR_EACH_IN_EARRAY(group->volumes, ExclusionVolume, volume)
			{
				volume->collides = false;
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}
}

static bool genesisPopulateObjectPosition(GenesisPopulatePlaceState *place_state, GenesisPopulateState *state, GenesisPopulateObject *object)
{
	int idx;
	GenesisPlacementParams* placement = object->placement;
	GroupDef *def = object->def;
	F32 size = object->radius;
	F32 width = place_state->width;
	F32 height = place_state->height;

	// Choose a position (Defaults to random)
	if(object->pos_pre_set) {
		copyVec3(object->pre_set_pos, object->pos);
	} else {
		object->pos[0] = randomMersennePositiveF32(state->table)*width + size + state->area->min[0];
		object->pos[2] = randomMersennePositiveF32(state->table)*height + size + state->area->min[2];
	}
	switch (placement->location)
	{
	case GenesisChallengePlace_ExactCenter:
		object->pos[0] = width*0.5f + state->area->min[0];
		object->pos[2] = height*0.5f + state->area->min[2];
		break;
	case GenesisChallengePlace_Center:
		object->pos[0] = randomMersennePositiveF32(state->table)*width*0.5f + size + state->area->min[0] + width*0.25f;
		object->pos[2] = randomMersennePositiveF32(state->table)*height*0.5f + size + state->area->min[2] + height*0.25f;
		break;
	case GenesisChallengePlace_Near_Challenge:
		{
			int object_count = 0;
			for (idx = 0; idx < eaSize(&state->populate_list); idx++)
			{
				if (genesisPopulateChallengeMatches(state->populate_list[idx], def, placement->ref_challenge_name))
				{
					object_count++;
				}
			}
			if (object_count > 0)
			{
				int j = randomMersenneIntRange(state->table, 0, object_count-1);
				for (idx = 0; idx < eaSize(&state->populate_list); idx++)
				{
					if (genesisPopulateChallengeMatches(state->populate_list[idx], def, placement->ref_challenge_name) && j-- == 0)
					{
						F32 dist = size * 2 * MAX(1, place_state->attempt * 10.f / POPULATE_OBJECT_PLACE_ATTEMPTS); // Get more lax as failed attempts increase
						Vec3 min_place, max_place;
						setVec3(min_place, MAX(state->area->min[0]+size, state->populate_list[idx]->pos[0]-dist), 0, MAX(state->area->min[2]+size, state->populate_list[idx]->pos[2]-dist));
						setVec3(max_place, MIN(state->area->max[0]-size, state->populate_list[idx]->pos[0]+dist), 0, MIN(state->area->max[2]-size, state->populate_list[idx]->pos[2]+dist));
						object->pos[0] = randomMersennePositiveF32(state->table)*(max_place[0]-min_place[0]) + min_place[0];
						object->pos[2] = randomMersennePositiveF32(state->table)*(max_place[2]-min_place[2]) + min_place[2];
						place_state->challenge_idx = idx;
						break;
					}
				}
				break;
			}
		}
		break;
	case GenesisChallengePlace_Exit:
		if (place_state->exit_idx != -1)
		{
			genesisPopulatePlaceOnDoor(state, size, place_state->exit_idx, false, object->pos);
		}
		break;
	case GenesisChallengePlace_Entrance:
		if (place_state->entrance_idx != -1)
		{
			genesisPopulatePlaceOnDoor(state, size, place_state->entrance_idx, false, object->pos);
		}
		break;
	case GenesisChallengePlace_Entrance_Exit:
		if (place_state->door_idx != -1)
		{
			genesisPopulatePlaceOnDoor(state, size, place_state->door_idx, false, object->pos);
		}
		break;
	case GenesisChallengePlace_InSpecificDoor:
		{
			int specific_door_idx = genesisPopulateGetSpecificDoor(state, placement->ref_door_dest_name);
			if (specific_door_idx != -1)
			{
				genesisPopulatePlaceOnDoor(state, 0, specific_door_idx, true, object->pos);
			}
		}
		break;

	case GenesisChallengePlace_Prefab_Location:
		{
			if(!state->area->prefab_library_piece)
			{
				genesisRaiseError( GENESIS_ERROR, object->source_context,
								   "Object is placed at prefab location in room '%s', but that room is not a prefab.",
								   state->room_source_context->location_name );
			}
			else
			{
				genesisPopulatePlaceAtNamedLocation(state, object, placement->ref_prefab_location);
			}
		}
		break;

	case GenesisChallengePlace_Random:
	case GenesisChallengePlace_On_Wall:
		break;
	}

	if (placement->location != GenesisChallengePlace_Prefab_Location)
		object->pos[1] = state->area->min[1];

	if (placement->location == GenesisChallengePlace_On_Wall) {
		// Snap to edge based on radius
		if (state->area->radius > 0) {
			genesisRaiseError( GENESIS_ERROR, object->source_context,
							   "You are not allowed to place On_Wall objects in circular rooms (like exteriors)." );
			return false;
		}
	}

	//Ensure that objects that are mirrored are only placed on one side of the room so that it works better with rotations.
	if(placement->mirror)
	{
		if(place_state->room_rot%2)
		{
			F32 room_width = state->area->max[0] - state->area->min[0];
			F32 x_pos = object->pos[0] - state->area->min[0];
			if(x_pos > room_width/2.0f)
				object->pos[0] = (room_width - x_pos) + state->area->min[0];
		}
		else
		{
			F32 room_depth = state->area->max[2]-state->area->min[2];
			F32 z_pos = object->pos[2] - state->area->min[2];
			if(z_pos > room_depth/2.0f)
				object->pos[2] = (room_depth - z_pos) + state->area->min[2];
		}
	}

	return true;
}

static bool genesisPopulateIsValidPosition(GenesisPopulatePlaceState *place_state, GenesisPopulateState *state, GenesisPopulateObject *object)
{
	int idx;
	bool found = false;
	GroupDef *def = object->def;

	if (state->area->radius > 0)
	{
		Vec3 midpoint = { (state->area->min[0]+state->area->max[0])*0.5f, 0.f, (state->area->min[2]+state->area->max[2])*0.5f };
		if (sqrtf(SQR(object->pos[0]-midpoint[0])+SQR(object->pos[2]-midpoint[2])) > state->area->radius)
			return false;
	}

	// Affinity group distance check
	for (idx = 0; idx < eaSize(&state->populate_list); idx++)
	{
		GenesisPopulateObject *pop_obj = state->populate_list[idx];
		if (!exclusionGridIsIntersectingPlanes(pop_obj->plane_id, place_state->plane_id))
			continue;
		if (pop_obj->def)
		{
			F32 dist = distance3(object->pos, pop_obj->pos);
			if (def->property_structs.encounter_hack_properties && def->property_structs.encounter_hack_properties->agro_radius > 0 &&
				pop_obj->def->property_structs.encounter_hack_properties &&
				dist < MAX(def->property_structs.encounter_hack_properties->agro_radius, pop_obj->def->property_structs.encounter_hack_properties->agro_radius))
			{
				found = true;
				break;
			}
			else if (((def == pop_obj->def) ||
				(object->placement->affinity_group && object->placement->affinity_group == pop_obj->placement->affinity_group)) &&
				dist < MAX(object->placement->exclusion_dist,pop_obj->placement->exclusion_dist))
			{
				found = true;
				break;
			}
		}
	}
	if (found)
		return false;
	return true;
}

static void genesisPopulateGetSubObjectMatrix(GenesisPopulatePlaceState *place_state, GenesisPopulateState *state, GenesisPopulateObject *object, ExclusionVolumeGroup *vol_group, Mat4 placement_mat)
{
	Mat4 parent_mat, offset_mat, child_rot_mat;
	F32 rotation_increment = RAD(object->placement->rotation_increment);
	F32 object_rot = object->rot;
	F32 child_rot = 0;

	//Apply child rotations as needed
	switch (place_state->multi_rotation)
	{
	case EXCLUDE_ROT_TYPE_FULL:
		child_rot = randomMersennePositiveF32(state->table)*TWOPI;
		if (rotation_increment != 0)
			child_rot = floor(child_rot / rotation_increment) * rotation_increment;
		//Notice there is no break here because full changes both object_rot and child_rot
		//break;
	case EXCLUDE_ROT_TYPE_IN_PLACE:
		object_rot = randomMersennePositiveF32(state->table)*TWOPI;
		if (rotation_increment != 0)
			object_rot = floor(object_rot / rotation_increment) * rotation_increment;
		break;
	case EXCLUDE_ROT_TYPE_NO_ROT:
		break;
	};

	// Parent Matrix
	identityMat4(parent_mat);
	yawMat3(object_rot, parent_mat);
	copyVec3(object->pos, parent_mat[3]);

	// Child Offset
	if(!isNonZeroMat3(vol_group->mat_offset))
		identityMat3(vol_group->mat_offset);
	mulMat4(parent_mat, vol_group->mat_offset, offset_mat);

	// Child Rotation
	identityMat4(child_rot_mat);
	yawMat3(child_rot, child_rot_mat);
	mulMat4(offset_mat, child_rot_mat, placement_mat);
}

static void genesisPopulateJitterActor(GenesisPopulatePlaceState *place_state, GenesisPopulateState *state, GenesisPopulateObject *object, ExclusionVolumeGroup *vol_group, Mat4 placement_mat)
{
	GenesisEncounterJitter *enc_jitter = &object->placement->enc_jitter;
	if(enc_jitter->jitter_type != GEJT_None)
	{
		F32 pos_jitter = (enc_jitter->jitter_type == GEJT_Custom ? enc_jitter->enc_pos_jitter : 4.0f);
		F32 rot_jitter = (enc_jitter->jitter_type == GEJT_Custom ? RAD(enc_jitter->enc_rot_jitter) : RAD(30.0f));
		F32 rand_rot_jitter = randomMersenneF32(state->table)*rot_jitter;
		F32 rand_pos_jitter = randomMersennePositiveF32(state->table)*pos_jitter;
		F32 pos_dir_angle = randomMersenneF32(state->table)*PI;
		Vec3 pos_offset;
		Mat3 rot_offset;
		Mat3 mat_copy;

		//Find Rotation and Position Jitter
		pos_offset[0] = cos(pos_dir_angle);
		pos_offset[1] = 0;
		pos_offset[2] = sin(pos_dir_angle);
		scaleVec3(pos_offset, rand_pos_jitter, pos_offset);
		identityMat3(rot_offset);
		yawMat3(rand_rot_jitter, rot_offset);

		//Alter Matrix
		copyMat3(placement_mat, mat_copy);
		mulMat3(mat_copy, rot_offset, placement_mat);
		addToVec3(pos_offset, placement_mat[3]);
	}
}

static bool genesisPopulateInternalObjectCollision(GenesisPopulatePlaceState *place_state, GenesisPopulateState *state, GenesisPopulateObject *object, bool check_mirror)
{
	int i, j;
	//Start Spawns do not internally collide.
	if(object->placement->is_start_spawn)
		return false;
	for ( i=0; i < eaSize(&object->volume_groups); i++ )
	{
		ExclusionVolumeGroup *vol_group_1 = object->volume_groups[i];
		if(vol_group_1->was_placed)
		{
			for ( j=0; j < eaSize(&object->volume_groups); j++ )
			{
				ExclusionVolumeGroup *vol_group_2 = object->volume_groups[j];
				if(vol_group_2->was_placed)
				{
					// Check against the mirror of each volume group
					if(check_mirror)
					{
						Mat4 mirror_mat;
						copyMat4(vol_group_2->final_mat, mirror_mat);
						genesisPopulateGetMirrorMat(state, mirror_mat, object->rot, place_state->room_rot);
						if(exclusionGridVolumeGroupCollision(vol_group_1->volumes, vol_group_1->final_mat, vol_group_2->volumes, mirror_mat))
							return true;
					}
					
					// Check against all other volume groups
					if(j>i)
					{
						if(exclusionGridVolumeGroupCollision(vol_group_1->volumes, vol_group_1->final_mat, vol_group_2->volumes, vol_group_2->final_mat))
							return true;						
					}
				}
			}
		}
	}
	return false;
}

static bool genesisPopulatePlaceObject(int iPartitionIdx, GenesisPopulateState *state, GenesisPopulateObject *object, bool is_encounter, int plane_id, int room_rot, int max_attempt)
{
	int attempt, i;
	GenesisPopulatePlaceState place_state = {0};
	GenesisPlacementParams* placement = object->placement;
	GroupDef *def = object->def;
	F32 multi_density = def->property_structs.terrain_properties.fMultiExclusionVolumesDensity;

	place_state.plane_id = plane_id;
	place_state.room_rot = room_rot;
	place_state.width = (state->area->max[0]-state->area->min[0]-2*object->radius);
	place_state.height = (state->area->max[2]-state->area->min[2]-2*object->radius);
	place_state.multi_rotation = def->property_structs.terrain_properties.iMultiExclusionVolumesRotation;
	if(!def->property_structs.terrain_properties.fMultiExclusionVolumesRequired)
		place_state.multi_rotation = EXCLUDE_ROT_TYPE_NO_ROT;

	if (place_state.width <= 0 || place_state.height <= 0)
		return false;

	if(max_attempt <= 0)
		max_attempt = POPULATE_OBJECT_PLACE_ATTEMPTS;

	// If the position is at a known location, just one attempt is
	// necesarry, and a *lot* fasterc
	if(object->placement->location == GenesisChallengePlace_Prefab_Location) {
		max_attempt = 1;
	}
	
	for (attempt = 0; attempt < max_attempt ; attempt++)
	{
		Mat4 placement_mat;
		bool failed = false;

		place_state.attempt = attempt;
		place_state.challenge_idx = -1;

		// Choose doors here so that placement & facing use the same entrance or exit
		genesisPopulateGetRandomDoors(state, &place_state.entrance_idx, &place_state.exit_idx, &place_state.door_idx);

		// Choose a position (Defaults to random)
		if(!genesisPopulateObjectPosition(&place_state, state, object))
			break;

		// Choose a rotation (Defaults to random)
		if(placement->on_wall && max_attempt == 4 && attempt > 0) {
			object->rot -= HALFPI;
			if (object->rot < 0)
				object->rot += TWOPI;
		} else if(!genesisPopulateObjectRotation(&place_state, state, object)) {
			break;
		}

		// Rotate and Snap to wall if requested
		if (placement->on_wall || placement->location == GenesisChallengePlace_On_Wall)
			genesisPopulateObjectPushToWall(&place_state, state, object);

		// Do Basic Collision Tests
		if(!genesisPopulateIsValidPosition(&place_state, state, object))
			continue;

		for (i = 0; i < eaSize(&object->volume_groups); i++)
		{
			ExclusionVolumeGroup *vol_group = object->volume_groups[i];
			vol_group->was_placed = false;

			// Random deletion of non-critical objects
			if (vol_group->optional && multi_density < 1)
			{
				if (randomMersennePositiveF32(state->table) > multi_density)
					continue;
			}

			// Get the matrix and add any volume_group specific rotation/position
			genesisPopulateGetSubObjectMatrix(&place_state, state, object, vol_group, placement_mat);

			// Add jitter to actors
			if(vol_group->is_actor && !object->challenge->has_patrol)
				genesisPopulateJitterActor(&place_state, state, object, vol_group, placement_mat);

			// Test Placement
			if(!genesisPopulateAddPlacement(iPartitionIdx, &place_state, state, object, vol_group, placement_mat))
			{
				// In an optional volume we can just skip.
				if (vol_group->optional)
					continue;
				failed = true;
				break;
			}

			// If object is mirroring, test mirrored position as well.
			if(placement->mirror)
			{
				Mat4 mirror_mat;
				copyMat4(placement_mat, mirror_mat);
				genesisPopulateGetMirrorMat(state, mirror_mat, object->rot, place_state.room_rot);
				if(!genesisPopulateAddPlacement(iPartitionIdx, &place_state, state, object, vol_group, mirror_mat))
				{
					failed = true;
					break;
				}
			}

			// Set matrix on success
			vol_group->was_placed = true;
			copyMat4(placement_mat, vol_group->final_mat);
		}
		if(failed)
			continue;

		// Make sure that we are not colliding with ourselves
		if(genesisPopulateInternalObjectCollision(&place_state, state, object, placement->mirror))
			continue;

		// If success and we can still make paths, use that location
		if (!genesisPopulateEnsurePaths(iPartitionIdx, state, object, false, false, "-placement"))
			continue;
		
		// If no problems, then we have a valid position and we can return.
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////
// Helper functions
//////////////////////////////////////////////////////////////////

static void genesisPopulateCreateDoorVolumes(GenesisPopulateState *state)
{
	int i;
	ExclusionVolume *door_volume;

	state->door_volume_group = calloc(1, sizeof(ExclusionVolumeGroup));
	door_volume = calloc(1, sizeof(ExclusionVolume));

	door_volume->type = EXCLUDE_VOLUME_BOX;
	setVec3(door_volume->extents[0], -POPULATE_DOOR_RADIUS, -POPULATE_DOOR_RADIUS/2, -POPULATE_DOOR_RADIUS);
	setVec3(door_volume->extents[1], POPULATE_DOOR_RADIUS, POPULATE_DOOR_RADIUS/2, POPULATE_DOOR_RADIUS);
	door_volume->begin_radius = 30;
	door_volume->collides = 1;
	door_volume->is_a_path = true;
	identityMat4(door_volume->mat);
	eaPush(&state->door_volume_group->volumes, door_volume);

	for (i = 0; i < eaSize(&state->area->doors); i++)
	{
		ExclusionObject *exclude = calloc(1, sizeof(ExclusionObject));
		identityMat3(exclude->mat);
		copyVec3(state->area->doors[i]->point, exclude->mat[3]);
		exclude->volume_group = state->door_volume_group;
		exclude->max_radius = 30;
		exclude->id = POPULATE_GRID_PATH;
		exclusionGridAddObject(state->exclude_grid, exclude, 30, false);

		state->area->doors[i]->gridx = MIN(state->buffer_width-1,MAX(0,(int)(state->area->doors[i]->point[0]-state->area->min[0])/POPULATE_SPACING));
		state->area->doors[i]->gridz = MIN(state->buffer_height-1,MAX(0,(int)(state->area->doors[i]->point[2]-state->area->min[2])/POPULATE_SPACING));
	}
}

static void genesisPopulatePlaceVolumeBlob(GenesisPopulateState *state, Vec3 pos, F32 radius, bool side_trail)
{
	GenesisPopulateBlob *blob;

	if (state->area->radius > 0)
	{
		Vec3 midpoint = { (state->area->min[0]+state->area->max[0])*0.5f, 0.f, (state->area->min[2]+state->area->max[2])*0.5f };
		if (sqrtf(SQR(pos[0]-midpoint[0])+SQR(pos[2]-midpoint[2])) > state->area->radius)
			return;
	}

	blob = calloc(1, sizeof(GenesisPopulateBlob));
	copyVec3(pos, blob->pos);
	blob->pos[1] = state->area->min[1];
	blob->radius = radius;
	blob->side_trail = side_trail;
	eaPush(&state->blobs, blob);
}

static void genesisPopulateCreateBlobs(GenesisPopulateState *state, U32 unique_index)
{
	int i, j;
	int pre_count = eaSize(&state->blobs);

	if (eaSize(&state->blobs) == 0)
		return;

	for (i = eaSize(&state->blobs)-1; i >= 0; --i)
	{
		for (j = 0; j < i; j++)
		{
			if (distance3(state->blobs[j]->pos, state->blobs[i]->pos) < MAX(state->blobs[j]->radius, state->blobs[i]->radius)*0.75)
			{
				Vec3 new_pos;
				lerpVec3(state->blobs[i]->pos, state->blobs[i]->radius/(state->blobs[i]->radius+state->blobs[j]->radius), state->blobs[j]->pos, new_pos);
				copyVec3(new_pos, state->blobs[j]->pos);
				state->blobs[j]->radius = powf(powf(state->blobs[i]->radius,3)+powf(state->blobs[j]->radius,3), 0.333f);
				SAFE_FREE(state->blobs[i]);
				eaRemove(&state->blobs, i);
				break;
			}
		}
	}

	printf("Blobs %d -> %d\n", pre_count, eaSize(&state->blobs));

	for (i = 0; i < eaSize(&state->blobs); i++)
	{
		char node_radius[256];
		GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
		to_place_object->object_name = allocAddString("Blob");
		to_place_object->uid = 0;
		identityMat4(to_place_object->mat);
		copyVec3(state->blobs[i]->pos, to_place_object->mat[3]);
		to_place_object->parent = state->parent_group_shared;
		to_place_object->params = StructCreate(parse_GenesisProceduralObjectParams);
		to_place_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
		to_place_object->params->volume_properties->eShape = GVS_Sphere;
		to_place_object->params->volume_properties->fSphereRadius = state->blobs[i]->radius;
// 		if(state->blobs[i]->side_trail)
// 			genesisObjectSetProperty(to_place_object, "VolumeType", "TerrainExclusion");
// 		else
		genesisProceduralObjectAddVolumeType(to_place_object->params, "TerrainExclusion");
		genesisProceduralObjectAddVolumeType(to_place_object->params, "TerrainFilter");
		sprintf(node_radius, "Populate_%d", unique_index);
		to_place_object->params->terrain_properties.pcVolumeName = StructAllocString(node_radius);
		eaPush(&state->to_place->objects, to_place_object);
	}
}

static void genesisPopulateInsertSorted(GenesisObject **objects, GenesisObject ***sorted_array, GenesisObject *object, int depth, const char **infinite_loop_name)
{
	int i;

	if (depth > eaSize(&objects))
	{
		*infinite_loop_name = object->challenge_name;
		return; // This means we have an infinite loop
	}

	for (i = 0; i < eaSize(sorted_array); i++)
		if ((*sorted_array)[i] == object)
			return;

	if (object->params.ref_challenge_name && object->params.ref_challenge_name[0])
	{
		// Make sure all the dependent challenges have already been placed
		for (i = 0; i < eaSize(&objects); i++)
		{
			if (!stricmp(objects[i]->challenge_name, object->params.ref_challenge_name))
			{
				genesisPopulateInsertSorted(objects, sorted_array, objects[i], depth+1, infinite_loop_name);
			}
		}
	}

	eaPush(sorted_array, object);
}

static GenesisPopulateObject* genesisPopulatePatrolOwnerObject(GenesisPopulateState *state, GenesisPopulatePatrol* patrol)
{
	int objIt;
	for( objIt = 0; objIt != eaSize( &state->populate_list ); ++objIt ) {
		GenesisPopulateObject* obj = state->populate_list[objIt];
		if(   obj->challenge
			  && patrol->patrol->owner_challenge->challenge_id == obj->challenge->challenge_id
			  && stricmp( patrol->patrol->owner_challenge->challenge_name, obj->challenge->challenge_name ) == 0 ) {
			return obj;
		}
	}

	return NULL;
}

GenesisInstancedChildParams *genesisChildParamsToInstancedChildParams(GenesisInstancedObjectParams *params, GenesisPlacementChildParams *child, int idx, const char* challengeName)
{
	char name[1024];
	GenesisInstancedChildParams *instanced = NULL;

	instanced = eaGet(&params->eaChildParams, idx);

	if(!instanced)
		eaSet(&params->eaChildParams, instanced = StructCreate(parse_GenesisInstancedChildParams), idx);

	sprintf(name, "%s_%d", challengeName, idx);
	instanced->pcInternalName = StructAllocString(name);
	copyVec3(child->vOffset, instanced->vOffset);
	copyVec3(child->vPyr, instanced->vPyr);
	if(child->actor_params.costume) {
		if(!instanced->pCostumeProperties) {
			instanced->pCostumeProperties = StructCreate(parse_WorldActorCostumeProperties);
		}

		StructCopyAll(parse_WorldActorCostumeProperties, child->actor_params.costume, instanced->pCostumeProperties);
	}
	return instanced;
}

static void genesisPopulatePlaceFinish(GenesisPopulateState *state, GenesisMissionRequirements *mission_req, GenesisPopulateObject *new_object, GenesisToPlaceObject *parent_object, bool place_volume_blobs, bool side_trail)
{
	GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
	S32 j;
	eaPush(&state->populate_list, new_object);
	eaPush(&state->reachable_list, new_object);
	if (IS_HANDLE_ACTIVE(new_object->challenge->start_spawn_using_transition))
	{
		to_place_object->uid = 0;
		to_place_object->params = genesisCreateStartSpawn( REF_STRING_FROM_HANDLE(new_object->challenge->start_spawn_using_transition) );
		if( !to_place_object->instanced ) {
			to_place_object->instanced = StructCreate( parse_GenesisInstancedObjectParams );
		}
	}
	else
	{
		to_place_object->uid = new_object->def->name_uid;
	}
	to_place_object->group_def = new_object->def;
	if (new_object->is_door_cap)
	{
		char buf[256];
		sprintf(buf, "%s_%s", mission_req->missionName, new_object->challenge->challenge_name);
		to_place_object->challenge_name = strdup(buf);
		to_place_object->challenge_is_unique = true;
	}
	else
	{
		to_place_object->challenge_name = strdup(new_object->challenge->challenge_name);
		to_place_object->challenge_is_unique = new_object->challenge->challenge_is_unique;
	}
	to_place_object->challenge_count = new_object->challenge->challenge_count;
	to_place_object->challenge_index = new_object->challenge->challenge_id;
	copyMat4(new_object->volume_groups[0]->final_mat, to_place_object->mat);//ShawnF TODO: If we want multi excluders for encounters we need to change this to do what the detail objects do
	to_place_object->parent = parent_object;
	to_place_object->seed = randomMersenneInt(state->table);
	to_place_object->source_context = StructClone( parse_GenesisRuntimeErrorContext, new_object->challenge->source_context );
	to_place_object->spawn_name = allocAddString(new_object->challenge->spawn_point_name);
	to_place_object->force_named_object = new_object->challenge->force_named_object;

	if (place_volume_blobs)
	{
		for ( j=0; j < eaSize(&new_object->volume_groups) ; j++ )
		{
			genesisPopulatePlaceVolumeBlob(state, new_object->volume_groups[j]->final_mat[3], new_object->def->bounds.radius, side_trail);
		}
	}

	{
		GenesisInstancedObjectParams* object_params = NULL;
		GenesisInteractObjectParams* interact_params = NULL;
		object_params = genesisFindMissionChallengeInstanceParams(mission_req, to_place_object->challenge_name);
		if( object_params ) {
			if( !to_place_object->instanced ) {
				to_place_object->instanced = StructCreate( parse_GenesisInstancedObjectParams );
			}
			StructCopyAll( parse_GenesisInstancedObjectParams, object_params, to_place_object->instanced );
		}
		interact_params = genesisFindMissionChallengeInteractParams(mission_req, to_place_object->challenge_name);
		if( interact_params ) {
			if( !to_place_object->interact ) {
				to_place_object->interact = StructCreate( parse_GenesisInteractObjectParams );
			}
			StructCopyAll( parse_GenesisInteractObjectParams, interact_params, to_place_object->interact );
		}
	}

	if (new_object->challenge->has_patrol) {
		if( !to_place_object->instanced ) {
			to_place_object->instanced = StructCreate( parse_GenesisInstancedObjectParams );
		}
		to_place_object->instanced->has_patrol = true;
	}

	if(eaSize(&new_object->placement->children))
	{
		if(!to_place_object->instanced)
			to_place_object->instanced = StructCreate(parse_GenesisInstancedObjectParams);
		for(j=0; j<eaSize(&new_object->placement->children); j++)
		{
			GenesisPlacementChildParams *child = new_object->placement->children[j];

			genesisChildParamsToInstancedChildParams(to_place_object->instanced, child, j, new_object->challenge->challenge_name);
		}
	}
	else if(eaSize(&new_object->volume_groups) > 1 && !new_object->placement->is_start_spawn)
	{
		// Preserve old genesis path
		if(!to_place_object->instanced)
			to_place_object->instanced = StructCreate(parse_GenesisInstancedObjectParams);
		for ( j=1; j < eaSize(&new_object->volume_groups); j++ )
		{
			GenesisInstancedChildParams *childParams = eaGet(&to_place_object->instanced->eaChildParams, j-1);
			char name[1024];

			if(!childParams)
			{
				childParams = StructCreate(parse_GenesisInstancedChildParams);
				eaSet(&to_place_object->instanced->eaChildParams, childParams, j-1);
			}

			sprintf(name, "%s_%d", new_object->challenge->challenge_name, j);
			childParams->pcInternalName = StructAllocString(name);
			copyVec3(new_object->volume_groups[j]->final_mat[3], childParams->vOffset);
			getMat3YPR(new_object->volume_groups[j]->final_mat, childParams->vPyr);
		}
	}
	eaPush(&state->to_place->objects, to_place_object);

	if (new_object->def && new_object->volume)
	{
		GenesisProceduralObjectParams *volume_params = genesisFindMissionChallengeVolumeParams(mission_req, to_place_object->challenge_name);
		if (volume_params)
		{
			to_place_object = genesisCreateChallengeVolume(parent_object, volume_params, new_object->def, to_place_object, new_object->source_context, new_object->volume);
			eaPush(&state->to_place->objects, to_place_object);
		}
	}
}

static bool genesisPopulatePlaceDoorCaps(int iPartitionIdx, GenesisPopulateState *state, GenesisMissionRequirements *mission_req, GenesisToPlaceObject *parent_object, GenesisObject **door_caps, int mission_uid)
{
	int i;
	for (i = 0; i < eaSize(&door_caps); i++)
	{
		GenesisPopulateObject *new_object = calloc(1, sizeof(GenesisPopulateObject));
		new_object->is_door_cap = true;
		genesisPopulateObjectInit(iPartitionIdx, new_object, door_caps[i], NULL, &door_caps[i]->params);
		new_object->plane_id = mission_uid + 1;
		
		if (new_object->def)
		{
			int max_attempt = -1;

			assert(eaSize(&new_object->volume_groups) > 0);
			{
				new_object->pos_pre_set = true;
				new_object->pre_set_pos[0] = state->area->min[0] + new_object->placement->override_position[0];
				new_object->pre_set_pos[1] = state->area->min[1] + new_object->placement->override_position[1];
				new_object->pre_set_pos[2] = state->area->max[2] - new_object->placement->override_position[2];
				new_object->placement->facing = GenesisChallengeFace_Fixed;
				new_object->placement->constant_rotation = new_object->placement->override_rot;
				max_attempt = 1;
			}
			if (!genesisPopulatePlaceObject(iPartitionIdx, state, new_object, true, new_object->plane_id, 0, max_attempt))
			{
				SAFE_FREE(new_object);
				return false;
			}
			genesisPopulateAddToExclusionGrid(state, new_object, new_object->plane_id, 0);
			genesisPopulatePlaceFinish(state, mission_req, new_object, parent_object, false, false);
		}
		else
		{
			Errorf("Invalid object: %s (%d)", door_caps[i]->obj.name_str, door_caps[i]->obj.name_uid);
			return false;
		}
	}
	return true;
}

static bool genesisPopulatePlaceImportantObjects(int iPartitionIdx, GenesisPopulateState *state, GenesisMissionRequirements *mission_req, GenesisRoomMission *room_mission, GenesisToPlaceObject *parent_object, bool place_volume_blobs, bool side_trail)
{
	int i;
	int *object_indices;
	GenesisObject **sorted_array = NULL;
	int num_objects = eaSize( &room_mission->objects );
	const char* infinite_loop_name = NULL;

	if (num_objects == 0 && eaSize( &room_mission->patrols ) == 0)
		return true;

	// Come up with a random ordering that respects dependencies
	{
		object_indices = alloca(sizeof(int)*num_objects);
		for (i = 0; i < num_objects; i++)
			object_indices[i] = i;
		randomMersennePermuteN(state->table, object_indices, num_objects, 1);

		// Place known positions first, so they can't get their place
		// filled by other objects.
		for (i = 0; i < num_objects; i++) {
			GenesisObject* object = room_mission->objects[object_indices[i]];
			if (object->params.location == GenesisChallengePlace_Prefab_Location ) {
				genesisPopulateInsertSorted(room_mission->objects, &sorted_array, object, 0, &infinite_loop_name);
			}
		}
		for (i = 0; i < num_objects; i++) {
			GenesisObject* object = room_mission->objects[object_indices[i]];
			if (object->params.location != GenesisChallengePlace_Prefab_Location ) {
				genesisPopulateInsertSorted(room_mission->objects, &sorted_array, object, 0, &infinite_loop_name);
			}
		}
			
		// If placed "Near_Challenge", this challenge must be placed near
		// the end, so that there is a challenge to place next to!
		for (i = 0; i < num_objects; i++) {
			if( sorted_array[i]->params.location == GenesisChallengePlace_Near_Challenge ) {
				eaMove( &sorted_array, eaSize(&sorted_array) - 1, i );
			}
		}
		if (eaSize(&sorted_array) && sorted_array[0]->params.location == GenesisChallengePlace_Near_Challenge)
			genesisRaiseError( GENESIS_ERROR, state->room_source_context,
							   "All the challenges in this room  have "
							   "placement NearChallenge, so none of them "
							   "can be placed first." );
	}

	if (infinite_loop_name)
		genesisRaiseError(GENESIS_ERROR, state->room_source_context,
						  "Circular challenge dependency detected "
						  "involving challenge \"%s\".",
						  infinite_loop_name );

	// Place objects in sorted order
	for (i = 0; i < eaSize(&sorted_array); i++)
	{
		GenesisPopulateObject *new_object = calloc(1, sizeof(GenesisPopulateObject));
		genesisPopulateObjectInit(iPartitionIdx, new_object, sorted_array[i], NULL, &sorted_array[i]->params);
		new_object->plane_id = room_mission->mission_uid + 1;
		
		if (new_object->def)
		{
			int max_attempt = -1;

			assert(eaSize(&new_object->volume_groups) > 0);
			if (state->position_override)
			{
				new_object->pos_pre_set = true;
				new_object->pre_set_pos[0] = state->area->min[0] + new_object->placement->override_position[0];
				new_object->pre_set_pos[1] = state->area->min[1] + new_object->placement->override_position[1];
				new_object->pre_set_pos[2] = state->area->max[2] - new_object->placement->override_position[2];
				new_object->placement->facing = GenesisChallengeFace_Fixed;
				new_object->placement->constant_rotation = new_object->placement->override_rot;
				max_attempt = 1;
			}
			if (!genesisPopulatePlaceObject(iPartitionIdx, state, new_object, true, new_object->plane_id, 0, max_attempt))
			{
				SAFE_FREE(new_object);
				return false;
			}
			genesisPopulateAddToExclusionGrid(state, new_object, new_object->plane_id, 0);
			genesisPopulatePlaceFinish(state, mission_req, new_object, parent_object, place_volume_blobs, side_trail);
		}
		else
		{
			Errorf("Invalid object: %s (%d)", sorted_array[i]->obj.name_str, sorted_array[i]->obj.name_uid);
			eaDestroy(&sorted_array);
			return false;
		}
	}

	// Then boil down the patrol objects
	if (!state->disable_patrol ) {
		for (i = 0; i < eaSize( &room_mission->patrols ); i++)
		{
			GenesisPatrolObject* patrol = room_mission->patrols[i];
			GenesisPopulatePatrol* pop_patrol = calloc(1, sizeof(*pop_patrol));

			pop_patrol->patrol = patrol;
			pop_patrol->owner_object = genesisPopulatePatrolOwnerObject( state, pop_patrol );

			if( patrol->type == GENESIS_PATROL_Path || patrol->type == GENESIS_PATROL_Path_OneWay ) {

				// Get pos
				if (patrol->path_start_is_challenge_pos ) {
					if (pop_patrol->owner_object == NULL ) {
						genesisRaiseError( GENESIS_FATAL_ERROR, pop_patrol->patrol->owner_challenge->source_context,
										   "INTERNAL ERROR Patrol start is in a different room from the patrol." );
					} else {
						copyVec3( pop_patrol->owner_object->pos, pop_patrol->path_start_pos );
					}
				} else {
					GenesisPopulateObject dummyPatrolStartOwnerObject = { 0 };
				
					genesisPopulateObjectInit(iPartitionIdx, &dummyPatrolStartOwnerObject, patrol->owner_challenge, NULL, &patrol->path_start);
					
					if( !genesisPopulatePlaceObject(iPartitionIdx, state, &dummyPatrolStartOwnerObject, true, room_mission->mission_uid + 1, 0, -1) ) {
						SAFE_FREE( pop_patrol );
						return false;
					}
					genesisPopulateAddToExclusionGrid(state, &dummyPatrolStartOwnerObject, room_mission->mission_uid + 1, 0);
					
					copyVec3( dummyPatrolStartOwnerObject.pos, pop_patrol->path_start_pos );
				}

				// Get end pos
				{
					GenesisPopulateObject dummyPatrolEndOwnerObject = { 0 };

					genesisPopulateObjectInit(iPartitionIdx, &dummyPatrolEndOwnerObject, patrol->owner_challenge, NULL, &patrol->path_end);
									
					if( !genesisPopulatePlaceObject(iPartitionIdx, state, &dummyPatrolEndOwnerObject, true, room_mission->mission_uid + 1, 0, -1) ) {
						SAFE_FREE( pop_patrol );
						return false;
					}
					genesisPopulateAddToExclusionGrid(state, &dummyPatrolEndOwnerObject, room_mission->mission_uid + 1, 0);

					copyVec3( dummyPatrolEndOwnerObject.pos, pop_patrol->path_end_pos );
				}
			}

			eaPush(&state->patrol_list, pop_patrol);
		}
	}
	
	eaDestroy(&sorted_array);
	return true;
}

static bool genesisPopulateVolumesObjectCollision(ExclusionVolume **grid_volumes, Mat4 vol_mat, GenesisPopulateObject *new_object)
{
	int i;
	for ( i=0; i < eaSize(&new_object->volume_groups); i++ )
	{
		ExclusionVolumeGroup *vol_group = new_object->volume_groups[i];
		if(exclusionGridVolumeGroupCollision(vol_group->volumes, vol_group->final_mat, grid_volumes, vol_mat))
			return true;
	}
	return false;
}

static void genesisPopulateWalkablePaths(int iPartitionIdx, GenesisPopulateState *state, GenesisPopulateObject *new_object)
{
	int x,z;
	Mat4 matrix;
	F32 box_size = POPULATE_SPACING/2.f;
	ExclusionVolume **grid_volumes = NULL;
	ExclusionVolume *new_volume = calloc(1, sizeof(ExclusionVolume));
	identityMat4(matrix);
	new_volume->type = EXCLUDE_VOLUME_BOX;
	setVec3(new_volume->extents[0], -box_size, -POPULATE_DEFAULT_PATH_HEIGHT/2 + 2, -box_size);
	setVec3(new_volume->extents[1], box_size, POPULATE_DEFAULT_PATH_HEIGHT/2 + 2, box_size);
	new_volume->begin_radius = sqrtf(2*SQR(box_size) + SQR(POPULATE_DEFAULT_PATH_HEIGHT/2));
	new_volume->collides = 1;
	identityMat4(new_volume->mat);
	eaPush(&grid_volumes, new_volume);

	for (z = 0; z < state->buffer_height; z++)
		for (x = 0; x < state->buffer_width; x++)
		{
			setVec3(matrix[3],
					x*POPULATE_SPACING+new_volume->extents[1][0]+state->area->min[0],
					state->area->min[1],
					z*POPULATE_SPACING+new_volume->extents[1][2]+state->area->min[2]);
			state->buffer[x+z*state->buffer_width].height = exclusionCalculateObjectHeightDefault(state->exclude_grid, matrix, new_volume->begin_radius, global_test_num++, iPartitionIdx, true);
			matrix[3][1] = state->buffer[x+z*state->buffer_width].height + new_volume->extents[1][1];

			if (state->buffer[x+z*state->buffer_width].bits != POPULATE_GRID_PATH)
			{
				bool collision;
				U32 id = 0;
				collision = exclusionGridVolumeGridCollision(state->exclude_grid, matrix, new_volume->begin_radius, grid_volumes, 0, NULL, global_test_num++, &id);
				if(!collision && new_object && genesisPopulateVolumesObjectCollision(grid_volumes, matrix, new_object))
				{
					collision = true;
					id = (SAFE_MEMBER(new_object->challenge, has_patrol) ? POPULATE_GRID_PATH : 1);
				}
				if (!id)
					id = POPULATE_GRID_BOUNDS;
				state->buffer[x+z*state->buffer_width].bits = (collision ? id : 0);
			}
		}

	eaDestroyEx(&grid_volumes, NULL);
}

static void genesisPopulateCreatePathVolumes(GenesisPopulateState *state, bool place_volume_blobs, bool side_trail)
{
	int x, z;
	ExclusionVolume *path_volume;

	state->path_volume_group = calloc(1, sizeof(ExclusionVolumeGroup));

	path_volume = calloc(1, sizeof(ExclusionVolume));

	path_volume->type = EXCLUDE_VOLUME_BOX;
	setVec3(path_volume->extents[0], -POPULATE_DEFAULT_PATH_WIDTH/2, -POPULATE_DEFAULT_PATH_HEIGHT/2, -POPULATE_DEFAULT_PATH_WIDTH/2);
	setVec3(path_volume->extents[1], POPULATE_DEFAULT_PATH_WIDTH/2, POPULATE_DEFAULT_PATH_HEIGHT/2, POPULATE_DEFAULT_PATH_WIDTH/2);
	path_volume->begin_radius = sqrtf(2*SQR(POPULATE_DEFAULT_PATH_WIDTH/2) + SQR(POPULATE_DEFAULT_PATH_HEIGHT/2));
	path_volume->collides = 1;
	path_volume->is_a_path = true;
	identityMat4(path_volume->mat);
	eaPush(&state->path_volume_group->volumes, path_volume);

	for (z = 0; z < state->buffer_height; z++)
		for (x = 0; x < state->buffer_width; x++)
			if (state->buffer[x+z*state->buffer_width].bits == POPULATE_GRID_PATH)
			{
				F32 rand_scale;
				ExclusionObject *exclude = calloc(1, sizeof(ExclusionObject));
				identityMat3(exclude->mat);
				setVec3(exclude->mat[3], x*POPULATE_SPACING+state->area->min[0], state->buffer[x+z*state->buffer_width].height+path_volume->extents[1][1], z*POPULATE_SPACING+state->area->min[2]);
				exclude->volume_group = state->path_volume_group;
				exclude->max_radius = path_volume->begin_radius;
				exclude->id = POPULATE_GRID_PATH;
				exclusionGridAddObject(state->exclude_grid, exclude, path_volume->begin_radius, false);

				// Not conditional; table state should be consistent regardless of whether we are placing blob volumes
				rand_scale = randomMersennePositiveF32(state->table);
				if (place_volume_blobs)
				{
					genesisPopulatePlaceVolumeBlob(state, exclude->mat[3], path_volume->begin_radius*rand_scale, side_trail);
				}
			}
}

static void genesisPopulateCreateDebugIcons(GenesisPopulateState *state)
{
	int i, j;
	GenesisToPlaceObject *icon_parent_group = calloc(1, sizeof(GenesisToPlaceObject));
	icon_parent_group->object_name = allocAddString("Icons");
	identityMat4(icon_parent_group->mat);
	icon_parent_group->parent = state->parent_group_shared;
	eaPush(&state->to_place->objects, icon_parent_group);

	for (i = 0; i < 4; i++)
	{
		GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
		to_place_object->uid = -11223377;
		identityMat4(to_place_object->mat);
		setVec3(to_place_object->mat[3], (i%2) ? state->area->min[0] : state->area->max[0], state->area->min[1], (i/2) ? state->area->min[2] : state->area->max[2]);
		to_place_object->parent = icon_parent_group;
		to_place_object->seed = randomMersenneInt(state->table);
		eaPush(&state->to_place->objects, to_place_object);
	}

	for (i = 0; i < eaSize(&state->area->doors); i++)
	{
		GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
		to_place_object->uid = -11223377;
		identityMat4(to_place_object->mat);
		setVec3(to_place_object->mat[3], state->area->doors[i]->point[0], state->area->min[1], state->area->doors[i]->point[2]);
		to_place_object->parent = icon_parent_group;
		to_place_object->seed = randomMersenneInt(state->table);
		eaPush(&state->to_place->objects, to_place_object);
	}

	for (i = 0; i < state->buffer_width; i++)
		for (j = 0; j < state->buffer_height; j++)
			if (state->buffer[i+j*state->buffer_width].bits == POPULATE_GRID_PATH)
			{
				GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
				to_place_object->uid = -11223355;
				identityMat4(to_place_object->mat);
				setVec3(to_place_object->mat[3], i*POPULATE_SPACING+state->area->min[0], state->buffer[i+j*state->buffer_width].height, j*POPULATE_SPACING+state->area->min[2]);
				to_place_object->parent = icon_parent_group;
				to_place_object->seed = randomMersenneInt(state->table);
				eaPush(&state->to_place->objects, to_place_object);
			}
}

static bool genesisPopulateEnsurePaths(int iPartitionIdx, GenesisPopulateState *state, GenesisPopulateObject *new_object, bool raise_errors, bool do_pop, const char* debug_suffix)
{
	if(state->area->no_details_or_paths)
		return true;

	memset(state->buffer, 0, state->buffer_width*state->buffer_height*sizeof(GenesisPopulateCell));

	// Test collision along a grid to determine walkable path cells
	genesisPopulateWalkablePaths(iPartitionIdx, state, new_object);
		
#if !PLATFORM_CONSOLE
	{
		if (genesis_populate_debug_images)
		{
			char filename[256];
			sprintf(filename, "C:/POPULATE/%d/grid%s.tga", state->debug_index, debug_suffix);
			genesisWriteDebug(filename, state->buffer, state->buffer_width, state->buffer_height);
		}
	}
#endif

	// Make the area walkable
	if (!genesisPopulateCreatePaths(iPartitionIdx, state, state->debug_index, raise_errors))
		return false;

	//Make Patrol Paths
	if(!state->disable_patrol && !genesisPopulatePatrols(iPartitionIdx, state, raise_errors, do_pop))
		return false;

	return true;
}

/// Fill in the LOW_RES_BUFFER based on data from BUFFER.
///
/// This must be called AFTER doors have been placed down, but before any other paths have been placed.
static void genesisPopulateFillLowResBuffer(GenesisPopulateState *state)
{
	int lowResXIt;
	int lowResYIt;
	for( lowResYIt = 0; lowResYIt != state->low_res_buffer_height; ++lowResYIt ) {
		for( lowResXIt = 0; lowResXIt != state->low_res_buffer_width; ++lowResXIt ) {
			GenesisPopulateCellLowRes* lowResCell = &state->low_res_buffer[ lowResXIt + lowResYIt * state->low_res_buffer_width];
			bool hasOpenSpaceAccum = false;
			
			int xIt;
			int yIt;
			for( yIt = lowResYIt * state->low_res_factor; yIt < MIN( state->buffer_height, (lowResYIt + 1) * state->low_res_factor ); ++yIt ) {
				for( xIt = lowResXIt * state->low_res_factor; xIt < MIN( state->buffer_width, (lowResXIt + 1) * state->low_res_factor ); ++xIt ) {
					GenesisPopulateCell* cell = &state->buffer[ xIt + yIt * state->buffer_width];

					if( cell->bits != POPULATE_GRID_BOUNDS ) {
						hasOpenSpaceAccum = true;
					}
				}
			}

			lowResCell->isWall = !hasOpenSpaceAccum;
		}
	}

	if( genesis_populate_debug_images ) {
		char filename[256];
		sprintf( filename, "C:/POPULATE/%d/grid-low_res.tga", state->debug_index );
		genesisWriteDebugLowRes( filename, state->low_res_buffer, state->low_res_buffer_width, state->low_res_buffer_height );
	}
}

static F32 genesisDetailDensityScale(int iPartitionIdx, GenesisPopulateState *state)
{
	int totalCells = state->buffer_width * state->buffer_height;
	int availableCellsAccum = 0;
	int it;

	if (state->area->no_details_or_paths)
		return 0.f;

	memset(state->buffer, 0, state->buffer_width*state->buffer_height*sizeof(GenesisPopulateCell));

	// Test collision along a grid to determine walkable path cells
	genesisPopulateWalkablePaths(iPartitionIdx, state, NULL);	

	for( it = 0; it != totalCells; ++it ) {
		if(   state->buffer[ it ].bits != POPULATE_GRID_PATH
			  && state->buffer[ it ].bits != POPULATE_GRID_BOUNDS ) {
			++availableCellsAccum;
		}
	}

	return (F32)availableCellsAccum / totalCells;
}

static void genesisPopulateFreeState(GenesisPopulateState *state)
{
	eaDestroy(&state->reachable_list);
	eaDestroyEx(&state->populate_list, NULL);
	eaDestroyEx(&state->patrol_list, NULL);
	SAFE_FREE(state->buffer);
	SAFE_FREE(state->low_res_buffer);
	exclusionGridFree(state->exclude_grid);

	eaDestroyEx(&state->door_volume_group->volumes, NULL);
	SAFE_FREE(state->door_volume_group);
	if (state->path_volume_group)
	{
		eaDestroyEx(&state->path_volume_group->volumes, NULL);
		SAFE_FREE(state->path_volume_group);
	}

	eaDestroyEx(&state->blobs, NULL);
}

static void genesisPopulateObjectDestroy(GenesisPopulateObject *object)
{
	eaDestroyEx(&object->volume_groups, exclusionVolumeGroupDestroy);
	SAFE_FREE(object);
}

//////////////////////////////////////////////////////////////////
// Detail object placement
//////////////////////////////////////////////////////////////////

static int genesisGetDetailObjectByPriority(GenesisDetail **prioritized_list, F32 priority)
{
	int i;
	for (i = 0; i < eaSize(&prioritized_list); i++)
	{
		if (priority < prioritized_list[i]->priority)
			return i;
		priority -= prioritized_list[i]->priority;
	}
	return eaSize(&prioritized_list)-1;
}

static void genesisPopulateCommitVolGroup(GenesisPopulateState *state, GenesisPopulateObject *new_object, GenesisToPlaceObject *parent_group, GenesisDetail *detail, bool place_volume_blobs, bool side_trail, ExclusionVolumeGroup *volume_group, Mat4 obj_mat)
{
	GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
	GroupDef *def_to_place;

	if (new_object->multi_exclude_child_count == 0)
		def_to_place = detail->def;
	else
		def_to_place = groupChildGetDef(detail->def, detail->def->children[volume_group->idx_in_parent], false);
	to_place_object->uid = def_to_place->name_uid;
	to_place_object->group_def = def_to_place;
	to_place_object->challenge_name = NULL;
	copyMat4(obj_mat, to_place_object->mat);
	to_place_object->parent = parent_group;
	to_place_object->seed = randomMersenneInt(state->table);
	eaPush(&state->to_place->objects, to_place_object);

	if (place_volume_blobs)
	{
		Vec3 place_mat;
		F32 radius;
		copyVec3(to_place_object->mat[3], place_mat);
		place_mat[0] += def_to_place->bounds.mid[0];
		place_mat[2] += def_to_place->bounds.mid[2];
		radius = sqrt(SQR(def_to_place->bounds.max[0]-def_to_place->bounds.min[0]) + SQR(def_to_place->bounds.max[2]-def_to_place->bounds.min[2])) / 2.0f;
		genesisPopulatePlaceVolumeBlob(state, place_mat, radius, side_trail);
	}
}

static void genesisPopulateCommitObject(GenesisPopulateState *state, GenesisPopulateObject *new_object, GenesisToPlaceObject *parent_group, GenesisDetail *detail, bool place_volume_blobs, bool side_trail, int rand_room_rot)
{
	int j;
	GenesisToPlaceObject *placement_parent_group = parent_group;

	genesisPopulateAddToExclusionGrid(state, new_object, 0, rand_room_rot);

	detail->placed_in_room++;
	if (new_object->multi_exclude_child_count > 0)
	{
		Vec3 min, max;
		setVec3same(min, 1000000);
		setVec3same(max, -1000000);
		placement_parent_group = calloc(1, sizeof(GenesisToPlaceObject));
		placement_parent_group->object_name = allocAddString(new_object->def->name_str);
		placement_parent_group->parent = parent_group;
		identityMat4(placement_parent_group->mat);
		for ( j=0; j < eaSize(&new_object->volume_groups); j++ )
		{
			MINVEC3(min, new_object->volume_groups[j]->final_mat[3], min);
			MAXVEC3(max, new_object->volume_groups[j]->final_mat[3], max);
		}
		addVec3(min, max, placement_parent_group->mat[3]);
		scaleVec3(placement_parent_group->mat[3], 0.5f, placement_parent_group->mat[3]);
		placement_parent_group->mat[3][1] = min[1];
		eaPush(&state->to_place->objects, placement_parent_group);
	}
	eaPush(&state->populate_list, new_object);
	for (j = 0; j < eaSize(&new_object->volume_groups); j++)
	{
		ExclusionVolumeGroup *volume_group = new_object->volume_groups[j];
		if(!volume_group->was_placed)
			continue;
		genesisPopulateCommitVolGroup(state, new_object, placement_parent_group, detail, place_volume_blobs, side_trail, volume_group, volume_group->final_mat);
		if(new_object->placement->mirror)
		{
			Mat4 mirror_mat;
			copyMat4(volume_group->final_mat, mirror_mat);
			genesisPopulateGetMirrorMat(state, mirror_mat, new_object->rot, rand_room_rot);
			genesisPopulateCommitVolGroup(state, new_object, placement_parent_group, detail, place_volume_blobs, side_trail, volume_group, mirror_mat);		
		}
	}
}

static F32 genesisPopulateCalculatePlaceCnt(GenesisPopulateState *state, GenesisDetail *detail, F32 density_scale)
{
	F32 room_width = state->area->max[0]-state->area->min[0];
	F32 room_depth = state->area->max[2]-state->area->min[2];
	F32 density = 1.0;
	GenesisDetailKit *detail_kit = detail->parent_kit->light_details;
	if(!detail_kit)
		detail_kit = GET_REF(detail->parent_kit->details);
	assert(detail_kit);

	if( detail->density ) {
		density *= detail->density;
	} else if( state->in_path ) {
		density *= detail_kit->path_default_density;
	} else {
		density *= detail_kit->default_density;
	}
	density *= detail->parent_kit->detail_density/100.0f;
	density *= (room_width*room_depth)/SQR(256.0f);
	density *= density_scale;
	density += randomMersennePositiveF32(state->table); //To correct for really low numbers / percentages
	return density;
}

static void genesisPopulateFillGridWithDetail(int iPartitionIdx, GenesisPopulateState *state, GenesisDetail *detail, F32 density_scale, int rand_room_rot, bool place_volume_blobs, bool side_trail)
{
	int i, j;
	F32 room_width = state->area->max[0]-state->area->min[0];
	F32 room_depth = state->area->max[2]-state->area->min[2];
	int max_rows = detail->params.max_rows;
	int max_col = detail->params.max_cols;
	F32 row_spacing = detail->params.row_spacing;
	F32 col_spacing = detail->params.col_spacing;
	F32 z_offset = detail->params.row_offset;
	F32 x_offset = detail->params.col_offset;
	F32 rotation_offset = 0.0f;
	bool flip_x = false;
	bool flip_z = false;
	int rows, cols;
	F32 density;

	if(detail->params.grid_ignores_padding) {
		room_width += state->area->padding_used*2;
		room_depth += state->area->padding_used*2;
	}

	// Setup how to traverse the room based off the internal direction of the room
	// If the room did not rotate (dir = north), the user would assume rows is +x and cols is -z
	// However, we traverse from 0 to + so the z is flipped when the room is pointing north
	switch(rand_room_rot)
	{
	case GENESIS_North:
		flip_z = true;
		break;
	case GENESIS_East:
		rotation_offset = HALFPI;
		max_rows = detail->params.max_cols;
		max_col = detail->params.max_rows;
		row_spacing = detail->params.col_spacing;
		col_spacing = detail->params.row_spacing;
		z_offset = detail->params.col_offset;
		x_offset = detail->params.row_offset;
		flip_x = true;
		flip_z = true;
		break;
	case GENESIS_South:
		rotation_offset = PI;
		flip_x = true;
		break;
	case GENESIS_West:
		rotation_offset = PI + HALFPI;
		max_rows = detail->params.max_cols;
		max_col = detail->params.max_rows;
		row_spacing = detail->params.col_spacing;
		col_spacing = detail->params.row_spacing;
		z_offset = detail->params.col_offset;
		x_offset = detail->params.row_offset;
		break;
	default:
		assert(false);
	}

	row_spacing = MAX(row_spacing, 1.0f);
	col_spacing = MAX(col_spacing, 1.0f);
	rows = room_depth/row_spacing;
	cols = room_width/col_spacing;
	rows = MAX(1, rows);
	cols = MAX(1, cols);
	if(!x_offset)
		x_offset = (room_width - col_spacing*(cols-1)) / 2.0f;
	if(!z_offset)
		z_offset = (room_depth - row_spacing*(rows-1)) / 2.0f;
	if(max_col)
		cols = MIN(max_col, cols);
	if(max_rows)
		rows = MIN(max_rows, rows);

	if(detail->params.grid_uses_density)
		density = (genesisPopulateCalculatePlaceCnt(state, detail, density_scale) / (cols*rows));

	for ( i=0; i < cols; i++ )
	{
		for ( j=0; j < rows; j++ )
		{
			GenesisPopulateObject *new_object;
			F32 x = x_offset + i*col_spacing;
			F32 z = z_offset + j*row_spacing;

			if(detail->params.grid_uses_density && density < randomMersennePositiveF32(state->table))
				continue;

			if(flip_x)
				x = room_width - x;
			if(flip_z)
				z = room_depth - z;
			if(detail->params.max_per_room && detail->placed_in_room >= detail->params.max_per_room)
				break;

			new_object = calloc(1, sizeof(GenesisPopulateObject));
			genesisPopulateObjectInit(iPartitionIdx, new_object, NULL, detail, &detail->params);
			new_object->pos_pre_set = true;
			new_object->pre_set_pos[0] = state->area->min[0] + x;
			new_object->pre_set_pos[1] = state->area->min[1];
			new_object->pre_set_pos[2] = state->area->min[2] + z;
			if(detail->params.grid_ignores_padding) {
				new_object->pre_set_pos[0] -= state->area->padding_used;
				new_object->pre_set_pos[2] -= state->area->padding_used;
			}
			new_object->offset_rot = rotation_offset;

			if (genesisPopulatePlaceObject(iPartitionIdx, state, new_object, false, 0, rand_room_rot, 4))
				genesisPopulateCommitObject(state, new_object, detail->parent_group, detail, place_volume_blobs, side_trail, rand_room_rot);
			else
				SAFE_FREE(new_object);
		}
	}

}

static void genesisPopulateRandomPositionDetail(int iPartitionIdx, GenesisPopulateState *state, GenesisDetail *detail, F32 density_scale, int rand_room_rot, bool place_volume_blobs, bool side_trail)
{
	int i, place_cnt;
	F32 room_width = state->area->max[0]-state->area->min[0];
	F32 room_depth = state->area->max[2]-state->area->min[2];

	place_cnt = genesisPopulateCalculatePlaceCnt(state, detail, density_scale);

	for ( i=0; i < place_cnt; i++ )
	{
		GenesisPopulateObject *new_object;

		if(detail->params.max_per_room && detail->placed_in_room >= detail->params.max_per_room)
			break;

		new_object = calloc(1, sizeof(GenesisPopulateObject));
		genesisPopulateObjectInit(iPartitionIdx, new_object, NULL, detail, &detail->params);
		
		if (genesisPopulatePlaceObject(iPartitionIdx, state, new_object, false, 0, rand_room_rot, -1))
		{
			genesisPopulateCommitObject(state, new_object, detail->parent_group, detail, place_volume_blobs, side_trail, rand_room_rot);
		}
		else
		{
			SAFE_FREE(new_object);
			break;
		}
	}

}

static void genesisPopulateFillDetailObjectsList(GenesisPopulateState *state, GenesisDetail ***prioritized_list, GenesisDetailKitAndDensity *detail_kit_and_density, const char *group_name, bool pre_challenge)
{
	int i;
	char name[256];
	GenesisToPlaceObject *parent_group;
	GenesisDetailKit *detail_kit;
	GenesisDetail** detail_kit_details;

	if(!detail_kit_and_density)
		return;

	detail_kit = detail_kit_and_density->light_details;
	if(!detail_kit)
		detail_kit = GET_REF(detail_kit_and_density->details);
	if (state->in_path)
		detail_kit_details = SAFE_MEMBER(detail_kit, path_details);
	else
		detail_kit_details = SAFE_MEMBER(detail_kit, details);
	
	if (eaSize(&detail_kit_details) <= 0)
		return;

	parent_group = calloc(1, sizeof(GenesisToPlaceObject));
	sprintf(name, "%s_%s", group_name, (pre_challenge ? "PreChallenge" : "PostChallenge"));
	parent_group->object_name = allocAddString(name);
	identityMat4(parent_group->mat);
	parent_group->parent = state->parent_group_shared;
	eaPush(&state->to_place->objects, parent_group);

	// Get items that we are going to fill a grid with rather than randomly place
	// Sort details by priority
	for (i = 0; i < eaSize(&detail_kit_details); i++)
	{
		GenesisDetail *detail = detail_kit_details[i];
		if(detail->params.pre_challenge != pre_challenge)
			continue;
		detail->placed_in_room = 0;
		detail->def = objectLibraryGetGroupDefFromRef(&detail->obj, true);
		detail->parent_group = parent_group;
		detail->parent_kit = detail_kit_and_density;
		if (detail->def)
		{
			int j;
			bool found = false;
			// Insertion sort
			for (j = 0; j < eaSize(prioritized_list); j++)
			{
				if ((*prioritized_list)[j]->priority_scale < detail->priority_scale)
				{
					eaInsert(prioritized_list, detail, j);
					found = true;
					break;
				}
			}
			if (!found)
				eaPush(prioritized_list, detail);
		}
		else
		{
			genesisRaiseErrorInternal(GENESIS_WARNING, "Detail Kit", detail_kit->name, "Could not find the def named %s", detail->obj.name_str);
		}
	}

}

static void genesisPopulateDetailObjects(int iPartitionIdx, GenesisPopulateState *state, GenesisDetailKitAndDensity *detail_kits[3], F32 detail_density_scale, bool place_volume_blobs, bool side_trail, bool pre_challenge)
{
	int i;
	GenesisDetail **prioritized_list = NULL;
	int rand_room_rot = randomMersenneU32(state->table)%4;
	state->obj_common_room_rot = randomMersennePositiveF32(state->table)*TWOPI;
	state->room_dir = rand_room_rot;

	if(state->area->no_details_or_paths)
		return;

	genesisPopulateFillDetailObjectsList(state, &prioritized_list, detail_kits[GDKT_Detail_1], "Details_1", pre_challenge);
	genesisPopulateFillDetailObjectsList(state, &prioritized_list, detail_kits[GDKT_Detail_2], "Details_2", pre_challenge);
	genesisPopulateFillDetailObjectsList(state, &prioritized_list, detail_kits[GDKT_Light_Details], "Light_Details", pre_challenge);

	for ( i=0; i < eaSize(&prioritized_list); i++ )
	{
		GenesisDetail *detail = prioritized_list[i];
		if(detail->params.fill_grid)
			genesisPopulateFillGridWithDetail(iPartitionIdx, state, detail, detail_density_scale, rand_room_rot, place_volume_blobs, side_trail);
		else 
			genesisPopulateRandomPositionDetail(iPartitionIdx, state, detail, detail_density_scale, rand_room_rot, place_volume_blobs, side_trail);
	}

	eaDestroy(&prioritized_list);
}

//////////////////////////////////////////////////////////////////
// Main entry point
//////////////////////////////////////////////////////////////////
bool genesisPopulateArea(int iPartitionIdx, int debug_index, GenesisRuntimeErrorContext* room_source_context,
						 GenesisToPlaceState *to_place, GenesisPopulateArea *area, U32 seed,
						 GenesisToPlaceObject *shared_parent, GenesisToPlaceObject **mission_parents,
						 GenesisMissionRequirements **mission_reqs, GenesisRoomMission **room_missions, 
						 GenesisObject **door_caps,
						 GenesisDetailKitAndDensity *detail_kits[3], bool place_volume_blobs, 
						 bool side_trail, bool in_path, bool disable_patrol, bool no_sharing, float low_res_spacing,
						 bool position_override)
{
	GenesisPopulateState state = { 0 };
	int excluder;
	F32 detail_density_scale = 1.0;
	
	if( !room_source_context ) {
		Errorf( "genesisPopulateArea() missing Genesis Context.  Please notify the Genesis team immediately." );
	}
	
	if (no_sharing)
	{
		int missionIt;
		for( missionIt = 0; missionIt != eaSize( &mission_reqs ); ++ missionIt ) {
			GenesisToPlaceObject** specificMissionParents = NULL;
			GenesisMissionRequirements** specificMissionReqs = NULL;
			GenesisRoomMission** specificMissions = NULL;

			eaPush( &specificMissionParents, mission_parents[ missionIt ]);
			eaPush( &specificMissionReqs, mission_reqs[ missionIt ]);
			{
				int roomMissionIt;
				for( roomMissionIt = 0; roomMissionIt != eaSize( &room_missions ); ++roomMissionIt ) {
					GenesisRoomMission* roomMission = room_missions[ roomMissionIt ];
				
					if( roomMission->mission_uid == -1 || roomMission->mission_uid == missionIt ) {
						eaPush( &specificMissions, roomMission );
					}
				}
			}

			{
				bool succeeded = genesisPopulateArea( iPartitionIdx, debug_index, room_source_context,
													  to_place, area, seed, specificMissionParents[ 0 ],
													  specificMissionParents, specificMissionReqs, specificMissions, door_caps,
													  detail_kits, place_volume_blobs, side_trail, in_path, disable_patrol, false,
													  low_res_spacing, position_override );
				eaDestroy( &specificMissions );
				eaDestroy( &specificMissionReqs );
				eaDestroy( &specificMissionParents );

				if( !succeeded ) {
					return false;
				}
			}
		}
		return true;
	}
	

	state.table = mersenneTableCreate(seed);
	state.area = area;
	state.to_place = to_place;
	state.parent_group_shared = shared_parent;
	state.debug_index = debug_index;
	state.room_source_context = room_source_context;
	state.in_path = in_path;
	state.disable_patrol = disable_patrol;
	state.position_override = position_override;

	state.buffer_width = (area->max[0] - area->min[0])/POPULATE_SPACING + 1;
	state.buffer_height = (area->max[2] - area->min[2])/POPULATE_SPACING + 1;
	if (state.buffer_width <= 0 || state.buffer_height <= 0)
		return false;
	state.buffer = calloc(sizeof(GenesisPopulateCell), state.buffer_width*state.buffer_height);

	if( low_res_spacing > 0 ) {
		// Disabling the magic going on in
		// genesisPopulateObjectCalcRotationByTarget for non-paths.
		// In particular, this could be useful for non-rectangular
		// rooms.  There's no real reason to do this other than
		// paranoia.
		if( state.in_path ) {
			state.low_res_factor = low_res_spacing / POPULATE_SPACING;
			assert( state.low_res_factor * POPULATE_SPACING == low_res_spacing ); //< truncation could cause issues

			state.low_res_buffer_width = (state.buffer_width + state.low_res_factor - 1) / state.low_res_factor;
			state.low_res_buffer_height = (state.buffer_height + state.low_res_factor - 1) / state.low_res_factor;
			state.low_res_buffer = calloc(sizeof(GenesisPopulateCellLowRes), state.low_res_buffer_width * state.low_res_buffer_height );
		}
		
	}

	// Create exclusion grid for collision checking
	state.exclude_grid = exclusionGridCreate(area->min[0]-1, area->min[2]-1, area->max[0]+1-area->min[0], area->max[2]+1-area->min[2]);

	// Insert incoming volumes
	for (excluder = 0; excluder < eaSize(&area->excluders); excluder++)
	{
		exclusionGridAddObject(state.exclude_grid, area->excluders[excluder], area->excluders[excluder]->max_radius, false);
	}

	// Prevent objects from placing near entry ways
	genesisPopulateCreateDoorVolumes(&state);

	// Before we place anything make sure we have walkable paths to start
	if(!genesisPopulateEnsurePaths(iPartitionIdx, &state, NULL, true, false, ""))
	{
		genesisPopulateFreeState(&state);
		
		genesisRaiseError( GENESIS_ERROR, state.room_source_context,
						   "Can not create a walkable path, "
						   "even with no objects in the room.  Internal error?" );
		return false;
	}

	// Fill in the low res buffer for pathfinding
	genesisPopulateFillLowResBuffer( &state );

	detail_density_scale = genesisDetailDensityScale( iPartitionIdx, &state );

	// Detail objects place before challenges
	genesisPopulateDetailObjects(iPartitionIdx, &state, detail_kits, detail_density_scale, place_volume_blobs, side_trail, true);

	// Place shared objects
	{
		int roomMissionIt;
		for( roomMissionIt = 0; roomMissionIt != eaSize( &room_missions ); ++roomMissionIt ) {
			GenesisRoomMission *room_mission = room_missions[ roomMissionIt ];

			if (room_mission->mission_uid == -1) {
				if (!genesisPopulatePlaceImportantObjects(iPartitionIdx, &state, NULL, room_mission, state.parent_group_shared, place_volume_blobs, side_trail) ) {
					genesisPopulateFreeState(&state);
					return false;
				}
			}
		}
	}

	// Place each mission specific object
	{
		int missionIt;
		int roomMissionIt;
		for( missionIt = 0; missionIt != eaSize( &mission_reqs ); ++missionIt ) {
			GenesisMissionRequirements *mission_req = mission_reqs[ missionIt ];
			GenesisToPlaceObject *mission_parent = mission_parents[ missionIt ];

			if (!genesisPopulatePlaceDoorCaps(iPartitionIdx, &state, mission_req, mission_parent, door_caps, missionIt))
			{
				genesisPopulateFreeState(&state);
				return false;
			}

			for( roomMissionIt = 0; roomMissionIt != eaSize( &room_missions ); ++roomMissionIt ) {
				GenesisRoomMission *room_mission = room_missions[ roomMissionIt ];

				if (room_mission->mission_uid == missionIt) {
					if (!genesisPopulatePlaceImportantObjects(iPartitionIdx, &state, mission_req, room_mission, mission_parent, place_volume_blobs, side_trail) ) {
						genesisPopulateFreeState(&state);
						return false;
					}
				}
			}
		}
	}

	// Detail objects place after challenges
	genesisPopulateDetailObjects(iPartitionIdx, &state, detail_kits, detail_density_scale, place_volume_blobs, side_trail, false);

	// Double check that we still have walkable paths after having placed everything
	if(!genesisPopulateEnsurePaths(iPartitionIdx, &state, NULL, true, true, "-final"))
	{
		genesisPopulateFreeState(&state);
		return false;
	}

	genesisPopulateCreateDebugIcons(&state);

	// Place path excluder objects so that detail objects spawn off the path
	genesisPopulateCreatePathVolumes(&state, place_volume_blobs, side_trail);

	genesisPopulateCreateBlobs(&state, debug_index);

	genesisPopulateFreeState(&state);
	return true;
}

#endif

AUTO_RUN;
void genesisInitDetailKitLibrary(void)
{
	genesis_detail_dict = RefSystem_RegisterSelfDefiningDictionary(GENESIS_DETAIL_DICTIONARY, false, parse_GenesisDetailKit, true, false, NULL);

	#ifndef NO_EDITORS
	{
		resDictMaintainInfoIndex(genesis_detail_dict, NULL, NULL, ".Tags", NULL, NULL);
		resDictManageValidation(genesis_detail_dict, genesisDetailKitValidateCB);
	}
	#endif
}

#include "wlGenesisPopulate_h_ast.c"
