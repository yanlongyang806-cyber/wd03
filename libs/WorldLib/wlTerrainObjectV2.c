#include "ScratchStack.h"
#include "rand.h"
#include "error.h"
#include "TriCube/vec.h"
#include "GenericMesh.h"
#include "Color.h"

#include "wlTerrainPrivate.h"
#include "wlExclusionGrid.h"
#include "WorldGridPrivate.h"
#include "WorldCellEntryPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Terrain_System););

#define EXCLUDE_STACK_DEPTH 20
#define GRID_SPACING 64 // TomY TODO GOING AWAY

//#define EXCLUSION_DEBUG 1

typedef struct TerrainObjectRecurseStackItem
{
	int order[4]; // Quad traversal order (FF = already traversed)
	int corner[2];
	U8 idx;
} TerrainObjectRecurseStackItem;

typedef struct TerrainObjectRecurseStack
{
	TerrainObjectRecurseStackItem items[EXCLUDE_STACK_DEPTH]; // Where we are in the traversal
	int pos;
	bool done;
	bool initialized;
} TerrainObjectRecurseStack;

typedef struct TerrainObjectRecurseBounds
{
	S32 range_min_x;
	S32 range_min_z;
	S32 range_width;
	S32 range_height;
} TerrainObjectRecurseBounds;

typedef struct HeightMapGroupParams
{
	F32 scale_min;
	F32 scale_max;
	F32 intensity_variation;
	F32 normal_snap_amount;
	bool get_terrain_color;
} HeightMapGroupParams;

typedef struct HeightMapTerrainItem
{
	U32 id;
	TerrainObjectEntry *info;
	TerrainObjectWrapper *wrapper;
	GroupDef *def;
	bool high_detail;
	F32 priority;
	S32 weld_type;
	F32 max_radius;
	int place_count;

	int num_children;						// > 0 iff we have a Multi-Excluder
	F32 multi_density;						// Percentage of the "optional" children to actually place
	S32 multi_rotation;						// Rotation type: 0 = in-place, 1 = full, 2 = none
	HeightMapGroupParams **param_groups;	// Terrain-specific parameters for each subgroup
	ExclusionVolumeGroup **volume_groups;	// Volumes for each subgroup

	// Statistics
	HeightMapExcludeStat *stats;
	int exclusion_debug_early_out_count;

	// Fellow instances
	HeightMapExcludeGrid *grid;

	// Position in our recursion
	TerrainObjectRecurseStack stack;
} HeightMapTerrainItem;

typedef struct TerrainExclusionData
{
	U32 density;
	U32 density_count;
	U64 test_number_density;
} TerrainExclusionData;

typedef struct TerrainSubobjectPlacementInfo
{
	int group_idx;
	Mat4 matrix;
	Vec3 pos;
	Vec3 normal;
	F32 rotation;
} TerrainSubobjectPlacementInfo;

typedef struct TerrainObjectPlacementInfo
{
	Vec3 color;
	U32 density_count;
	U32 density;
	HeightMap *height_map;
	TerrainSubobjectPlacementInfo **subobjects;
} TerrainObjectPlacementInfo;

typedef struct TerrainObjectRecurseInfo
{
	MersenneTable *table;
	StashTable height_map_table;
	HeightMapCache *height_map_cache;
	HeightMapExcludeGrid *grid;
	int *object_lookup;
	TerrainObjectEntry **object_table;
	HeightMapTerrainItem *item;
} TerrainObjectRecurseInfo;

const static U32 random_eco_x[] = {
	34058,	61366,	6647,	31369,	28188,	26748,	56968,	26404,	63715,	34082,	18592,	50724,	38200,	35316,	54412,	45901,
	55168,	21567,	34328,	12404,	39064,	43248,	35510,	9111,	18254,	10647,	32618,	51864,	4854,	34633,	19513,	50626,
	44818,	24534,	3161,	31038,	48115,	50900,	42552,	45787,	1180,	3058,	46350,	46509,	26514,	20941,	2732,	48634,
	59192,	45683,	45274,	22408,	31985,	5069,	58046,	7660,	33142,	18361,	15644,	16492,	37146,	22640,	1314,	47414,
	14326,	15822,	31108,	53213,	49526,	1185,	13466,	17505,	19587,	57760,	58063,	41883,	14633,	38302,	24042,	19937,
	5248,	22493,	18213,	41529,	23682,	61964,	64260,	28137,	17358,	12384,	12022,	4259,	26342,	25970,	3078,	3880,
	13336,	33877,	20580,	64399,	5337,	16008,	11303,	11652,	7953,	24527,	24194,	3282,	17604,	25206,	59931,	12330,
	54853,	31684,	46909,	34395,	14879,	1528,	48546,	17928,	36053,	36968,	49534,	27292,	7992,	46300,	63524,	62802,
	22020,	46005,	21672,	49416,	12333,	6665,	64637,	7675,	48424,	50548,	37154,	52029,	49944,	148,	39266,	3188,
	43583,	61394,	50153,	22046,	2219,	60218,	18548,	27608,	37919,	4189,	10998,	38486,	14560,	2776,	23241,	43551,
	28852,	56654,	52232,	55432,	48573,	21470,	33820,	9569,	16145,	33466,	57125,	3463,	47921,	14627,	59370,	28887,
	53821,	4281,	22903,	9460,	25161,	19093,	5649,	3446,	41476,	31801,	43824,	31614,	21480,	36798,	36712,	58776,
	10443,	58900,	31362,	38556,	41104,	52689,	17027,	20052,	28710,	28339,	56670,	61964,	52440,	21573,	33063,	13222,
	24445,	23875,	37344,	53600,	48137,	48855,	328,	26077,	11114,	16317,	33281,	61025,	28596,	35991,	26819,	59699,
	23518,	45665,	4570,	3217,	1224,	10334,	17898,	49657,	19388,	27845,	32748,	47956,	3788,	55948,	19228,	48935,
	6655,	51042,	50735,	14619,	15272,	26478,	4697,	43879,	7298,	55963,	22020,	44025,	19967,	33135,	53183,	1607
};

const static U32 random_eco_z[] = {
	2776,	16751,	12274,	42058,	5478,	51832,	15365,	63073,	43255,	28184,	55104,	24849,	33047,	37617,	4916,	21572,
	52673,	22380,	17623,	37644,	59, 	18940,	64601,	8153,	45018,	38781,	21893,	46840,	58596,	18268,	10446,	54357,
	28250,	46146,	45746,	61818,	62661,	10611,	37502,	43619,	25252,	39341,	36553,	63571,	56133,	52717,	36062,	49629,
	50336,	13840,	64064,	61281,	87, 	61294,	34989,	62475,	55924,	41159,	55120,	23916,	29967,	39719,	5247,	46216,
	3240,	16541,	37147,	41347,	16963,	18948,	17414,	43424,	8953,	19471,	18397,	34222,	20095,	11357,	14017,	3302,
	6177,	62626,	4803,	48281,	46659,	12040,	18245,	16034,	35588,	29150,	57717,	19817,	42279,	17125,	42519,	54493,
	58088,	33572,	18653,	28954,	26278,	55034,	6526,	63052,	28765,	46789,	21042,	50370,	54479,	34969,	60774,	16645,
	3782,	34112,	44139,	33057,	64058,	2590,	10727,	21505,	24127,	56365,	50974,	10556,	56323,	44760,	40188,	58227,
	12757,	57430,	57313,	6693,	58959,	28271,	61621,	12990,	17053,	64851,	22199,	45450,	8443,	21530,	48262,	27847,
	12831,	59128,	3633,	35652,	62298,	15022,	29290,	12440,	31881,	36907,	23865,	50976,	54681,	405,	14536,	50901,
	8550,	50262,	14289,	41707,	31518,	16917,	30816,	55333,	21501,	23168,	64236,	57977,	59235,	53207,	43555,	16209,
	5686,	34901,	60074,	33037,	21298,	32853,	13545,	52620,	45800,	9659,	34832,	16425,	9689,	43259,	2508,	33406,
	27268,	36209,	1938,	54112,	61394,	37802,	27976,	39495,	63529,	9530,	41285,	28627,	56627,	7367,	33968,	8900,
	32868,	20196,	27398,	21344,	62415,	21504,	28799,	53053,	60907,	40177,	47581,	29973,	49775,	39777,	54782,	29782,
	43144,	50786,	29614,	57697,	59175,	13232,	59922,	52420,	53042,	2650,	37570,	27767,	45019,	37823,	3056,	40837,
	41173,	55243,	17882,	57213,	8944,	42022,	11085,	40455,	4645,	17595,	16851,	8155,	11937,	61137,	65452,	49999
};

const static U32 random_eco_type[] = {
	36571,	50374,	39213,	7958,	50473,	8862,	30377,	50250,	60647,	49626,	65533,	36607,	25690,	38150,	26991,	21866,
	39784,	64677,	29537,	36456,	49878,	26062,	27760,	21417,	51860,	3445,	59391,	43169,	26916,	61979,	50937,	4561,
	38868,	51811,	19860,	65247,	39755,	58468,	39617,	48431,	34111,	54018,	11447,	831,	33295,	230,	45531,	63868,
	25815,	63383,	26859,	52949,	37302,	61924,	4562,	14865,	25564,	1906,	24696,	40894,	53458,	56865,	10229,	55722
};

static U64 global_test_num = 1;

static bool exclusion_flag_density_check = false;
static bool exclusion_flag_old_priorities = false;
static int exclusion_flag_traverse_method = 2; // 1 = N object / 1 traverse, 2 = N object / N traverse, 3 = 1 traverse / N object
static bool exclusion_flag_random_traverse = true;

static F32 exclusion_density_scale = 1.0f;

AUTO_COMMAND;
void terrainSetExclusionFlags(char *flags)
{
	if (strlen(flags) < 4)
		return;

	if (flags[0] == 'D')
		exclusion_flag_density_check = true;
	else
		exclusion_flag_density_check = false;

	if (flags[1] == 'P')
		exclusion_flag_old_priorities = false;
	else
		exclusion_flag_old_priorities = true;

	if (flags[2] == 'R')
		exclusion_flag_random_traverse = true;
	else
		exclusion_flag_random_traverse = false;

	if (flags[3] == '1')
		exclusion_flag_traverse_method = 1;
	else if (flags[3] == '2')
		exclusion_flag_traverse_method = 2;
	else
		exclusion_flag_traverse_method = 3;
}

AUTO_COMMAND;
void terrainSetExclusionDensityScale(F32 scale)
{
	exclusion_density_scale = scale;
}

void terrainPrintExclusionFlags()
{
	printf("%c%c%c%c",
		exclusion_flag_density_check ? 'D' : '-',
		exclusion_flag_old_priorities ? '-' : 'P',
		exclusion_flag_random_traverse ? 'R' : 'O',
		(exclusion_flag_traverse_method == 1) ? '1' : ((exclusion_flag_traverse_method == 2) ? '2' : '3'));
}

__forceinline static void getMatForTerrainObject(Mat4 matrix, const Vec3 position, const Vec3 normal, F32 rotation)
{
	Mat3 normal_mat, rotate_mat;
	Vec3 x_axis = { 1.f, 0.f, 0.f };
	Vec3 x_axis_rotated, pyr;
	setVec3(pyr, 0, rotation, 0);
	createMat3YPR(rotate_mat, pyr);
	copyVec3(normal, normal_mat[1]);
	mulVecMat3(x_axis, rotate_mat, x_axis_rotated);
	crossVec3(x_axis_rotated, normal_mat[1], normal_mat[2]);
	normalVec3(normal_mat[2]);
	crossVec3(normal_mat[1], normal_mat[2], normal_mat[0]);
	copyMat3(normal_mat, matrix);
	copyVec3(position, matrix[3]);
}

GroupDef *terrainGetHeightMapDef(HeightMap *height_map, GroupDefLib *def_lib)
{
	char node_name[64];
	sprintf(node_name, "eco_%d_%d", height_map->map_local_pos[0], height_map->map_local_pos[1]);
	return groupLibFindGroupDefByName(def_lib, node_name, false);
}

GroupDef *terrainCreateHeightMapDef(HeightMap *height_map, GroupDef *parent_def, GroupDefLib *def_lib, U32 *id_counter, F32 color_shift)
{
	GroupDef *hmp_def = NULL;
	char node_name[64];

	sprintf(node_name, "eco_%d_%d", height_map->map_local_pos[0], height_map->map_local_pos[1]);
	hmp_def = groupLibNewGroupDef(def_lib, NULL, 0, node_name, 0, true, true);
	assert(hmp_def);

	if(color_shift)
	{
		Vec3 shift_vec = {0,0,0};
		shift_vec[0] = color_shift;
		groupSetTintOffset(hmp_def, shift_vec);
	}

	// Add group to list
	groupChildInitialize(parent_def, -1, hmp_def, NULL, 0, 0, eaSize(&parent_def->children)+1);
	groupDefModify(parent_def, eaSize(&parent_def->children)-1, true);
	return hmp_def;
}

void terrainCreateObjectGroup(GroupDef *parent_def, GroupDefLib *def_lib, TerrainBinnedObject *new_obj, GroupDef *child_group, U32 *id_counter)
{
	char node_name_orig[64], node_name[64];
	GroupDef *def;
	GroupChild *ent;
	Vec3 tint_color;

	assert(def_lib == parent_def->def_lib);

	sprintf(node_name_orig, "inst_%d", *id_counter);
	groupLibMakeGroupName(def_lib, node_name_orig, SAFESTR(node_name), 0);
	def = groupLibNewGroupDef(def_lib, NULL, 0, node_name, 0, true, true);
	assert(def);
	(*id_counter)++;

	// Add group to list
	ent = groupChildInitialize(parent_def, -1, def, NULL, 0, new_obj->seed, eaSize(&parent_def->children)+1);
	ent->always_use_seed = 1;
	groupDefModify(parent_def, eaSize(&parent_def->children)-1, true);

	// Add the group as a child
	ent = groupChildInitialize(def, 0, child_group, NULL, 0, 0, 1);
	ent->always_use_seed = 1;
	getMatForTerrainObject(ent->mat, new_obj->position, new_obj->normal, new_obj->rotation);
	assert(isNonZeroMat3(ent->mat));

	scaleVec3(new_obj->tint, new_obj->intensity*2*U8TOF32_COLOR, tint_color); 
	//setVec3same(tint_color, new_obj->intensity);
	groupSetTintColor(def, tint_color);

	groupSetBounds(def, true);
}

#define floor_fast(val) ((val) >= 0 ? (int)(val) : floor(val))

static void heightMapGetExclusionHeight(StashTable height_map_table, HeightMapCache *height_map_cache, Vec3 pos, Vec3 normal)
{
	int j;
	HeightMap *height_map = NULL;
	SubdivArrays *subdiv;
	GMesh *mesh;
	TerrainBuffer *normal_buffer;
	IVec2 grid_pos = { floor_fast(pos[0]/GRID_BLOCK_SIZE), floor_fast(pos[2]/GRID_BLOCK_SIZE) };
	Vec3 local_position;

	height_map = terrainFindHeightMap(height_map_table, grid_pos, height_map_cache);

	setVec3(normal, 0, 1, 0);

	if (!height_map)
		return;

	{
		IVec2 local_pos;
		heightMapGetMapLocalPos(height_map, local_pos);
		setVec3(local_position, pos[0]-local_pos[0]*GRID_BLOCK_SIZE, 0, pos[2]-local_pos[1]*GRID_BLOCK_SIZE);
	}

	subdiv = SAFE_MEMBER_ADDR(height_map->terrain_mesh, subdiv);
	mesh = SAFE_MEMBER(height_map->terrain_mesh, mesh);

	if (subdiv && mesh)
	{
		int *tri_idxs = subdivGetBucket(subdiv, local_position);

		for (j = 0; j < eaiSize(&tri_idxs); ++j)
		{
			GTriIdx *tri = &mesh->tris[tri_idxs[j]];
			Vec3 barycentric_coords;

			if (nearSameVec3XZTol(local_position, mesh->positions[tri->idx[0]], 0.03f))
			{
				pos[1] = mesh->positions[tri->idx[0]][1];
				if (mesh->normals)
					copyVec3(mesh->normals[tri->idx[0]], normal);
				else
					makePlaneNormal(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]], normal);
				break;
			}
			else if (nearSameVec3XZTol(local_position, mesh->positions[tri->idx[1]], 0.03f))
			{
				pos[1] = mesh->positions[tri->idx[1]][1];
				if (mesh->normals)
					copyVec3(mesh->normals[tri->idx[1]], normal);
				else
					makePlaneNormal(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]], normal);
				break;
			}
			else if (nearSameVec3XZTol(local_position, mesh->positions[tri->idx[2]], 0.03f))
			{
				pos[1] = mesh->positions[tri->idx[2]][1];
				if (mesh->normals)
					copyVec3(mesh->normals[tri->idx[2]], normal);
				else
					makePlaneNormal(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]], normal);
				break;
			}
			else if (calcBarycentricCoordsXZProjected(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]], local_position, barycentric_coords))
			{
				pos[1] = barycentric_coords[0] * mesh->positions[tri->idx[0]][1] + barycentric_coords[1] * mesh->positions[tri->idx[1]][1] + barycentric_coords[2] * mesh->positions[tri->idx[2]][1];
				makePlaneNormal(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]], normal);
				break;
			}
		}
	}
	else
	{
		U8 *normal_pt;
		int x = (int)local_position[0], z = (int)local_position[2];
		int lod = heightMapGetLevelOfDetail(height_map);
		int px = x >> lod;
		int pz = z >> lod;
		F32 inv_scale = 1.f/(1<<lod);

		pos[1] = heightMapGetInterpolatedHeight(height_map,px,pz,(local_position[0]*inv_scale)-px,(local_position[2]*inv_scale)-pz);

		normal_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_NORMAL, height_map->loaded_level_of_detail);
		normal_pt = normal_buffer ? normal_buffer->data_normal[(x>>normal_buffer->lod)+(z>>normal_buffer->lod)*normal_buffer->size] : NULL;
		if (normal_pt)
			setVec3(normal, -(normal_pt[2]-128)*(1.f/127.f), (normal_pt[1]-128)*(1.f/127.f), (normal_pt[0]-128)*(1.f/127.f));
	}
}

static bool heightMapCheckTerrainExclusion(StashTable height_map_table, HeightMapCache *height_map_cache, Mat4 item_matrix, ExclusionVolumeGroup *group)
{
	int j;
	F32 min_valid_height = -1e8;
	F32 max_valid_height = 1e8;

	for (j = 0; j < eaSize(&group->volumes); j++)
	{
		ExclusionVolume *volume = group->volumes[j];
		if (volume->above_terrain || volume->below_terrain)
		{
			if (volume->type == EXCLUDE_VOLUME_SPHERE)
			{
				// Sphere-Terrain collision
				Vec3 center, point, normal;
				IVec2 min_point, max_point;
				F32 height_delta;
				F32 vol_rad_sq = SQR(volume->begin_radius);
				F32 vol_rad_pad_sq = SQR(volume->begin_radius+4);

				mulVecMat4(volume->mat[3], item_matrix, center);

				min_point[0] = center[0]-volume->begin_radius-8;
				min_point[0] -= min_point[0]%8;
				min_point[1] = center[2]-volume->begin_radius-8;
				min_point[1] -= min_point[1]%8;

				max_point[0] = center[0]+volume->begin_radius+15;
				max_point[0] -= max_point[0]%8;
				max_point[1] = center[2]+volume->begin_radius+15;
				max_point[1] -= max_point[1]%8;

				for (point[0] = min_point[0]; point[0] <= max_point[0]; point[0] += 8)
				{
					for (point[2] = min_point[1]; point[2] <= max_point[1]; point[2] += 8)
					{
						F32 v_dist_sq = SQR(point[0]-center[0]) + SQR(point[2]-center[2]);
						if (v_dist_sq <= vol_rad_pad_sq)
						{
							if (v_dist_sq < vol_rad_sq)
								height_delta = volume->mat[3][1] + sqrtf(SQR(volume->begin_radius)-v_dist_sq) * (volume->above_terrain ? -1 : 1);
							else
								height_delta = volume->mat[3][1];

							heightMapGetExclusionHeight(height_map_table, height_map_cache, point, normal);
							if (volume->above_terrain)
								min_valid_height = MAX(min_valid_height, point[1]-height_delta);
							else
								max_valid_height = MIN(max_valid_height, point[1]-height_delta);

							if (min_valid_height > max_valid_height)
								return false;
						}
					}
				}
			}
			// Box-Terrain collision not yet supported
		}
	}
	if (min_valid_height > max_valid_height)
		return false;
	if (min_valid_height < item_matrix[3][1] && max_valid_height > item_matrix[3][1])
		return true;
	if (max_valid_height < 1e8) 
		item_matrix[3][1] = max_valid_height;
	else
		item_matrix[3][1] = min_valid_height;
	return true;
}

static __forceinline TerrainObjectEntry *terrainGetLayerObjectV2(ZoneMapLayer *layer, int index)
{
	if (index >= 0 && index < eaSize(&layer->terrain.object_table))
	{
		return layer->terrain.object_table[index];
	}
	return NULL;
}

static TerrainObjectPlacementInfo *heightMapTerrainObjectTest(TerrainObjectRecurseInfo *r, TerrainObjectRecurseBounds *bounds, Vec3 pos)
{
	int i, j;
	IVec2 grid_pos = { floor_fast(pos[0]/GRID_BLOCK_SIZE), floor_fast(pos[2]/GRID_BLOCK_SIZE) };
	static TerrainObjectPlacementInfo info;
	Vec3 local_position;
	HeightMap *height_map;
	TerrainBuffer *objects_buffer, *color_buffer;
	U32 density = 0;
	int seed;
	int x, z;
	U32 density_count = 0;
	IVec2 min, max;
	F32 object_rotation;
	HeightMapExcludeGridElement *my_element = NULL;

	eaDestroyEx(&info.subobjects, NULL);

	global_test_num++;

	// Find height map
	info.height_map = height_map = terrainFindHeightMap(r->height_map_table, grid_pos, r->height_map_cache);

	if (!height_map)
		return NULL;

	{
		IVec2 local_pos;
		heightMapGetMapLocalPos(height_map, local_pos);
		setVec3(local_position, pos[0]-local_pos[0]*GRID_BLOCK_SIZE, 0, pos[2]-local_pos[1]*GRID_BLOCK_SIZE);
		assert(local_position[0] > 0 && local_position[2] > 0);
		x = (int)local_position[0];
		z = (int)local_position[2];
	}

	// Get density
	objects_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);
	for (i = eaSize(&objects_buffer->data_objects)-1; i >= 0; --i)
	{
		TerrainObjectEntry *groupDefInfo = NULL;
		if (r->object_lookup)
		{
			if (objects_buffer->data_objects[i]->object_type >= 0 &&
				objects_buffer->data_objects[i]->object_type < eaiSize(&r->object_lookup))
				groupDefInfo = r->object_table[r->object_lookup[objects_buffer->data_objects[i]->object_type]];
		}
		else
		{
			groupDefInfo = terrainGetLayerObjectV2(height_map->zone_map_layer, objects_buffer->data_objects[i]->object_type);
		}

		if (groupDefInfo && groupDefInfo == r->item->info)
		{
			// Sample object densities
			int stride = (GRID_BLOCK_SIZE)/(objects_buffer->size-1);
			density = objects_buffer->data_objects[i]->density[(x/stride) + (z/stride)*objects_buffer->size] * exclusion_density_scale;
			seed = groupDefInfo->seed;
			break;
		}
	}
	if (density == 0)
		return NULL;

	if (exclusion_flag_density_check)
	{
		IVec2 gpos = { (int)(pos[0]-r->item->grid->min[0])/GRID_SPACING, (int)(pos[2]-r->item->grid->min[1])/GRID_SPACING };
		if (gpos[0] >= 0 && gpos[0] < r->item->grid->width-1 &&
			gpos[1] >= 0 && gpos[1] < r->item->grid->height-1)
		{
			my_element = &r->item->grid->elements[gpos[0]+gpos[1]*r->item->grid->width];
			if (my_element->full == 1 || eaUSize(&my_element->excluders) >= density)
				return NULL;
		}
	}

	// Density Check
	if (exclusion_flag_density_check)
	{
		bool need_count = false;
		setVec2(min, (int)(pos[0]-r->item->grid->min[0]-128.f)/GRID_SPACING, (int)(pos[2]-r->item->grid->min[1]-128.f)/GRID_SPACING);
		setVec2(max, (int)(pos[0]-r->item->grid->min[0]+128.f)/GRID_SPACING, (int)(pos[2]-r->item->grid->min[1]+128.f)/GRID_SPACING);
		min[0] = CLAMP(min[0], 0, r->item->grid->width-1);
		min[1] = CLAMP(min[1], 0, r->item->grid->height-1);
		max[0] = CLAMP(max[0], 0, r->item->grid->width-1);
		max[1] = CLAMP(max[1], 0, r->item->grid->height-1);
		for (x = min[0]; x <= max[0]; x++)
			for (z = min[1]; z <= max[1]; z++)
			{
				HeightMapExcludeGridElement *element = &r->item->grid->elements[x+z*r->item->grid->width];
				density_count += eaSize(&element->excluders);
				if (element->full == 1)
					need_count = true;
			}
			if (density_count > density || need_count)
			{
				density_count = 0;
				for (x = min[0]; x <= max[0]; x++)
					for (z = min[1]; z <= max[1]; z++)
					{
						HeightMapExcludeGridElement *element = &r->item->grid->elements[x+z*r->item->grid->width];
						for (j = 0; j < eaSize(&element->excluders); j++)
						{
							TerrainExclusionData *data = element->excluders[j]->userdata;
							if (data && data->test_number_density != global_test_num
								&& (SQR(element->excluders[j]->mat[3][0]-pos[0])+SQR(element->excluders[j]->mat[3][2]-pos[2])) < SQR(144.4))
							{
								data->test_number_density = global_test_num;
								if (++density_count >= density ||
									data->density_count >= data->density)
								{
#ifdef EXCLUSION_DEBUG
									r->item->exclusion_debug_early_out_count++;
#endif
									return NULL;
								}
							}
						}
					}
			}
			info.density = density;
			info.density_count = density_count;
	}
	else
	{
		U32 clamped_x = height_map->map_local_pos[0] & 0xFF;
		U32 clamped_y = height_map->map_local_pos[1] & 0xFF;
		U32 random_seed = (random_eco_x[(x*7+z*5)	 & 0xFF] ^ (random_eco_z[clamped_x]&0xC00F) ^ 
			random_eco_z[(x*3+z*11) & 0xFF] ^ (random_eco_x[clamped_y]&0xC00F) ^ 
			random_eco_type[seed%64] ^ 
			(seed*175)) & 0xFFFF; // New method

		if (density < 1 || random_seed >= density)
			return NULL;
	}

	object_rotation = randomMersennePositiveF32(r->table)*6.2831853072f;

	if (!r->item->volume_groups)
	{
		// Object has no volumes; just place
		TerrainSubobjectPlacementInfo *subobject;
		subobject = calloc(1, sizeof(TerrainSubobjectPlacementInfo));

		// Rotation
		subobject->rotation = object_rotation;
		heightMapGetExclusionHeight(r->height_map_table, r->height_map_cache, pos, subobject->normal);
		if (r->item->param_groups[0]->normal_snap_amount > 0.f)
		{
			scaleVec3(subobject->normal, r->item->param_groups[0]->normal_snap_amount, subobject->normal);
			subobject->normal[1] += 1.f - r->item->param_groups[0]->normal_snap_amount;
			normalVec3(subobject->normal);
		}
		else
		{
			setVec3(subobject->normal, 0, 1, 0);
		}
		assert(!ISZEROVEC3(subobject->normal));
		getMatForTerrainObject(subobject->matrix, pos, subobject->normal, subobject->rotation);
		eaPush(&info.subobjects, subobject);
		copyVec3(subobject->matrix[3], subobject->pos);
	}
	else
	{
		for (i = 0; i < eaSize(&r->item->volume_groups); i++)
		{
			Mat4 rel_mat;
			Vec3 pos2;
			ExclusionObject *exclude;
			TerrainSubobjectPlacementInfo *subobject;
			int attempt, attempt_count = 1;
			bool attempt_succeeded = false;
		   
			// Random deletion of non-critical objects
			if (r->item->volume_groups[i]->optional)
			{
				if (r->item->multi_density < 1 && randomMersennePositiveF32(r->table) > r->item->multi_density)
					continue;
				attempt_count = (int)MAX(1,r->item->multi_density);
			}

			subobject = calloc(1, sizeof(TerrainSubobjectPlacementInfo));

			for (attempt = 0; attempt < attempt_count; attempt++)
			{
				// Rotation
				identityMat3(rel_mat);
				switch (r->item->multi_rotation)
				{
				case EXCLUDE_ROT_TYPE_IN_PLACE:
					// In-place rotation of all subobjects; positions rigidly rotate around pivot
					subobject->rotation = randomMersennePositiveF32(r->table)*6.2831853072f;
					yawMat3(object_rotation, rel_mat);
					break;
				case EXCLUDE_ROT_TYPE_FULL:
					// Full rotation around multi-excluder pivot and local object pivot
					subobject->rotation = randomMersennePositiveF32(r->table)*6.2831853072f;
					yawMat3(randomMersennePositiveF32(r->table)*6.2831853072f, rel_mat);
					break;
				case EXCLUDE_ROT_TYPE_NO_ROT:
					// Rotate the entire multi-excluder rigidly
					subobject->rotation = object_rotation;
					yawMat3(object_rotation, rel_mat);
					break;
				};

				copyVec3(pos, rel_mat[3]);
				//TODO: Mutli-Excluders have always been broken in that they looks the child rotation
				//I fixed this for interiors by changing offset to be a full matrix
				//I should do the same thing for terrain at some point
				mulVecMat4(r->item->volume_groups[i]->mat_offset[3], rel_mat, pos2);

				// Height & Normal
				heightMapGetExclusionHeight(r->height_map_table, r->height_map_cache, pos2, subobject->normal);
				if (r->item->param_groups[i]->normal_snap_amount > 0.f)
				{
					scaleVec3(subobject->normal, r->item->param_groups[i]->normal_snap_amount, subobject->normal);
					subobject->normal[1] += 1.f - r->item->param_groups[i]->normal_snap_amount;
					normalVec3(subobject->normal);
				}
				else
				{
					setVec3(subobject->normal, 0, 1, 0);
				}
				assert(!ISZEROVEC3(subobject->normal));
				getMatForTerrainObject(subobject->matrix, pos2, subobject->normal, subobject->rotation);

				// Check that objects don't stick out of the block bounds, that volumes don't collide with terrain, and volumes don't collide with other objects
				if (r->item->volume_groups[i]->volumes &&
						!exclusionCheckVolumesInRange(r->item->volume_groups[i], subobject->matrix, bounds->range_min_x, bounds->range_min_z, bounds->range_width, bounds->range_height) ||
						!heightMapCheckTerrainExclusion(r->height_map_table, r->height_map_cache, subobject->matrix, r->item->volume_groups[i]) ||
						exclusionGridVolumeGridCollision(r->grid, subobject->matrix, r->item->max_radius, r->item->volume_groups[i]->volumes, 0, r->item->stats, global_test_num++, NULL))
				{
					// Try again
					continue;
				}
				attempt_succeeded = true;
				break;
			}


			if (!attempt_succeeded)
			{
				if (r->item->volume_groups[i]->optional)
				{
					// Optional volume; just skip.
					SAFE_FREE(subobject);
					continue;
				}
				else
				{
					// Non-optional volume failure case - remove the volumes we've created
					SAFE_FREE(subobject);
					exclusionGridPurgeProvisionalObjects(r->grid);
					return NULL;
				}
			}

			// Success; add volume to list
			subobject->group_idx = i;
			eaPush(&info.subobjects, subobject);

			// Excluder object for grid
			exclude = calloc(1, sizeof(ExclusionObject));
			copyMat4(subobject->matrix, exclude->mat);
			exclude->volume_group = r->item->volume_groups[i];
			exclude->max_radius = r->item->max_radius;
			exclusionGridAddObject(r->grid, exclude, r->item->max_radius, true);

			// Return valid position
			copyVec3(subobject->matrix[3], subobject->pos);
		}
	}
	exclusionGridCommitProvisionalObjects(r->grid);

	// At this point, we've passed all tests; we are definitely placing this object

	if (exclusion_flag_density_check)
	{
		// Bump up neighbor counts
		for (x = min[0]; x <= max[0]; x++)
			for (z = min[1]; z <= max[1]; z++)
			{
				HeightMapExcludeGridElement *element = &r->item->grid->elements[x+z*r->item->grid->width];
				for (j = 0; j < eaSize(&element->excluders); j++)
				{
					TerrainExclusionData *data = element->excluders[j]->userdata;
					if (data && data->test_number_density == global_test_num)
					{
						data->test_number_density--; // Don't look at this excluder again in this loop
						data->density_count++;
						if (data->density_count == data->density)
						{
							// We're full! Mark the element
							element->full = 1;
						}
					}
				}
			}

			if (info.density_count == info.density && my_element)
				my_element->full = 1;
	}

	// Color
	color_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, height_map->loaded_level_of_detail-2);
	if (color_buffer)
	{
		U8 *src = color_buffer->data_u8vec3[(x>>color_buffer->lod)+(z>>color_buffer->lod)*color_buffer->size];
		info.color[0] = src[2];
		info.color[1] = src[1];
		info.color[2] = src[0];
	}
	else
	{
		setVec3(info.color, 127, 127, 127);
	}

	return &info;
}

static bool heightMapRecurseStackGetNext(TerrainObjectRecurseStack *S, TerrainObjectRecurseBounds *B, MersenneTable *table, Vec3 out_pos)
{
	bool ret = false;

	if (S->done)
		return false; // Traversed the whole field

	if (!exclusion_flag_random_traverse)
	{
		if (!S->initialized)
		{
			S->items[0].corner[0] = S->items[0].corner[1] = 0;
			S->initialized = true;
		}

		setVec3(out_pos, (F32)S->items[S->pos].corner[0]+0.5f+B->range_min_x, 0, (F32)S->items[S->pos].corner[1]+0.5f+B->range_min_z);

		S->items[0].corner[0]++;
		if (S->items[0].corner[0] >= B->range_width)
		{
			S->items[0].corner[0] = 0;
			S->items[0].corner[1]++;
			if (S->items[0].corner[1] >= B->range_height)
				S->done = true;
		}
		return true;
	}

	if (!S->initialized)
	{
		// Initialize stack
		S->items[0].corner[0] = S->items[0].corner[1] = 0;
		S->items[0].idx = 0;
		randomMersennePermutation4(table, S->items[0].order);
		S->initialized = true;
	}

	assert(S->pos < EXCLUDE_STACK_DEPTH-1);

	// Extend stack to full length
	while (true)
	{
		int size = (1<<S->pos);
		U8 quad;
		assert(S->items[S->pos].idx < 4);
		quad = S->items[S->pos].order[S->items[S->pos].idx];

		S->pos++;

		assert(S->pos < EXCLUDE_STACK_DEPTH-1);

		// Calculate new bounds
		switch (quad)
		{
		case 0:
			S->items[S->pos].corner[0] = S->items[S->pos-1].corner[0];
			S->items[S->pos].corner[1] = S->items[S->pos-1].corner[1];
			break;
		case 1:
			S->items[S->pos].corner[0] = (S->items[S->pos-1].corner[1]+S->items[S->pos-1].corner[0])%size + size;
			S->items[S->pos].corner[1] = S->items[S->pos-1].corner[1];
			break;
		case 2:
			S->items[S->pos].corner[0] = S->items[S->pos-1].corner[0];
			S->items[S->pos].corner[1] = (S->items[S->pos-1].corner[1]+S->items[S->pos-1].corner[0])%size + size;
			break;
		case 3:
			S->items[S->pos].corner[0] = 2*size-1-S->items[S->pos-1].corner[0];
			S->items[S->pos].corner[1] = 2*size-1-S->items[S->pos-1].corner[1];
			break;
		}

		size *= 2;

		if (size < B->range_width && size < B->range_height)
		{
			// Random traversal of all four children
			randomMersennePermutation4(table, S->items[S->pos].order);
			S->items[S->pos].idx = 0;
		}
		else if (size < B->range_width)
		{
			// Random traversal across X only
			if (randomMersenneF32(table) < 0)
				setVec4(S->items[S->pos].order, 0xFF, 0xFF, 0, 1);
			else	
				setVec4(S->items[S->pos].order, 0xFF, 0xFF, 1, 0);
			S->items[S->pos].idx = 2;
		}
		else if (size < B->range_height)
		{
			// Random traversal across Z only
			if (randomMersenneF32(table) < 0)
				setVec4(S->items[S->pos].order, 0xFF, 0xFF, 0, 2);
			else	
				setVec4(S->items[S->pos].order, 0xFF, 0xFF, 2, 0);
			S->items[S->pos].idx = 2;
		}
		else
		{
			break;
		}
	}

	if (S->items[S->pos].corner[0] < B->range_width && S->items[S->pos].corner[1] < B->range_height)
	{
		setVec3(out_pos, (F32)S->items[S->pos].corner[0]+0.5f+B->range_min_x, 0, (F32)S->items[S->pos].corner[1]+0.5f+B->range_min_z);
		ret = true;
	}

	// Pop up to next available quad
	do
	{
		S->pos--;
		if (S->pos < 0)
		{
			S->done = true;
			return ret; // Traversed the whole field
		}
		S->items[S->pos].order[S->items[S->pos].idx] = 0xFF;
		S->items[S->pos].idx++;
	} while (S->items[S->pos].idx == 4);

	return ret;
}

AUTO_COMMAND;
void terrainObjectTestTraversal(int w, int h)
{
	TerrainObjectRecurseStack s = { 0 };
	TerrainObjectRecurseBounds b = { 0, 0, w, h };
	MersenneTable *table = mersenneTableCreate(23230);
	Vec3 out_pos;
	int count = 0;
	do
	{
		if (heightMapRecurseStackGetNext(&s, &b, table, out_pos))
		{
			printf("%03d,%03d\n", (int)out_pos[0], (int)out_pos[2]);
			count++;
		}
	} while (!s.done);
	printf("COUNT: %d (%d)\n", count, w*h);
	mersenneTableFree(table);
}

static bool heightMapPlaceTerrainObject(MersenneTable *table, StashTable height_map_table, HeightMapCache *height_map_cache,
										StashTable height_map_defs, HeightMapExcludeGrid *grid,
										TerrainObjectRecurseBounds *bounds, int *object_lookup, TerrainObjectEntry **object_table,
										HeightMapTerrainItem *item, F32 color_shift, Vec3 test_pos, bool playable)
{
	int i;
	TerrainObjectPlacementInfo *placement = NULL;
	TerrainObjectRecurseInfo info = { table, height_map_table, height_map_cache, grid, object_lookup, object_table, item };

	if ((item->high_detail || item->def->bounds.radius < 30) && !playable)
	{
		return false;
	}

	placement = heightMapTerrainObjectTest(&info, bounds, test_pos);
	if (placement)
	{
		// Place object!
		TerrainBinnedObject *new_obj;
		HeightMap *height_map = placement->height_map;//height_maps[placement->height_map_idx];

		for (i = 0; i < eaSize(&placement->subobjects); i++)
		{
			GroupDef *def_to_place = NULL;
			ExclusionVolumeGroup *group = item->volume_groups ? item->volume_groups[placement->subobjects[i]->group_idx] : NULL;
			HeightMapGroupParams *params = item->param_groups[placement->subobjects[i]->group_idx];
			TerrainExclusionData *data;
			ExclusionObject *exclude;

			// Item params
			new_obj = calloc(1, sizeof(TerrainBinnedObject));
			if (item->num_children == 0)
			{
				def_to_place = item->def;
			}
			else if (group)
			{
				def_to_place = groupChildGetDef(item->def, item->def->children[group->idx_in_parent], false);
			}
			else
				continue;

			new_obj->group_id = def_to_place->name_uid;
			new_obj->scale = randomMersennePositiveF32(table) * (params->scale_max - params->scale_min) + params->scale_min;
			new_obj->rotation = placement->subobjects[i]->rotation;
			copyVec3(placement->subobjects[i]->pos, new_obj->position);
			copyVec3(placement->subobjects[i]->normal, new_obj->normal);
			new_obj->intensity = randomMersennePositiveF32(table) * params->intensity_variation + (1.f - params->intensity_variation);
			if (params->get_terrain_color)
				copyVec3(placement->color, new_obj->tint);
			else
				setVec3(new_obj->tint, 127, 127, 127); // this will be multiplied by 2 when the groupdefs are created
			new_obj->seed = randomMersenneU32(table);
			new_obj->weld = item->weld_type;

			if (item->wrapper->def_lib)
			{
				IVec2 map_pos;
				GroupDef *def;
				heightMapGetMapLocalPos(height_map, map_pos);
				if ((def = terrainGetHeightMapDef(height_map, item->wrapper->def_lib)) == NULL)
				{
					def = terrainCreateHeightMapDef(height_map, item->wrapper->root_def, item->wrapper->def_lib, &item->wrapper->id, color_shift);
				}

				terrainCreateObjectGroup(def, item->wrapper->def_lib,
						new_obj, def_to_place, &item->wrapper->id);
			}
			eaPush(&height_map->object_instances, new_obj);

			if (i == 0)
			{
				// Excluder object for density calculations
				exclude = calloc(1, sizeof(ExclusionObject));
				copyMat4(placement->subobjects[i]->matrix, exclude->mat);
				exclude->userdata = data = calloc(1, sizeof(TerrainExclusionData));
				exclude->userdata_owned = true;
				data->density = placement->density;
				data->density_count = placement->density_count;
				exclude->max_radius = item->max_radius;
				exclusionGridAddObject(item->grid, exclude, item->max_radius, false);
			}
		}
		return true;
	}
	return false;
}

static bool heightMapPlaceTerrainObjectSearch(MersenneTable *table, StashTable height_map_table, HeightMapCache *height_map_cache, 
											  StashTable height_map_defs, HeightMapExcludeGrid *grid,
											  TerrainObjectRecurseBounds *bounds, int *object_lookup, TerrainObjectEntry **object_table,
											  HeightMapTerrainItem *item, F32 color_shift, bool playable)
{
	do
	{
		Vec3 test_pos;
		if (heightMapRecurseStackGetNext(&item->stack, bounds, table, test_pos))
		{
			if (heightMapPlaceTerrainObject(table, height_map_table, height_map_cache, height_map_defs, grid, bounds, object_lookup, object_table, item, color_shift, test_pos, playable))
				return true;
		}
	} while (!item->stack.done);
	return false;
}

static bool heightMapHasObjectGroup(HeightMap *height_map, int *object_lookup, TerrainObjectEntry **object_table, HeightMapTerrainItem *item)
{
	int i;
	TerrainBuffer *objects_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);
	if(!objects_buffer || eaSize(&objects_buffer->data_objects) == 0)
		return false;

	for(i=0; i < eaSize(&objects_buffer->data_objects); i++)
	{
		TerrainBinnedObject **instance_list = NULL;
		TerrainObjectEntry *groupDefInfo = NULL;
		if (object_lookup)
		{
			if (objects_buffer->data_objects[i]->object_type >= 0 &&
				objects_buffer->data_objects[i]->object_type < eaiSize(&object_lookup))
				groupDefInfo = object_table[object_lookup[objects_buffer->data_objects[i]->object_type]];
		}
		else
		{
			groupDefInfo = terrainGetLayerObjectV2(height_map->zone_map_layer, objects_buffer->data_objects[i]->object_type);
		}

		if (groupDefInfo &&
			groupDefInfo == item->info)
		{
			return true;
		}
	}
	return false;
}

int dontBeStupid = 0;
AUTO_CMD_INT(dontBeStupid, dontBeStupid);

void terrainCreateObjectGroupsHelper(ZoneMapLayer *layer, HeightMap **maps, TerrainBlockRange *block_range, TerrainObjectWrapper **wrappers,
									 int *object_lookup, TerrainObjectEntry **orig_object_table, 
									 TerrainObjectEntry **object_table, TerrainExclusionVersion exclusion_version, F32 color_shift, bool playable, bool bForce)
{
	int i, j, k;
	HeightMapTerrainItem **items = NULL;
	ExclusionObject *world_excluders;
	StashTable height_map_table;
	StashTable height_map_defs; // TomY TODO Delete
	HeightMapCache height_map_cache = { 0 };

	if (eaSize(&maps) == 0)
		return;

	world_excluders = calloc(1, sizeof(ExclusionObject));
	identityMat4(world_excluders->mat);
	world_excluders->max_radius = 1e8;
	layerGroupTreeTraverse(layer, exclusionVolumesTraverseCallback, world_excluders, false, false);

	assert(eaSize(&wrappers) == eaSize(&object_table));

	height_map_table = stashTableCreateInt(16);
	height_map_defs = stashTableCreateInt(16);

	for (i = 0; i < eaSize(&maps); i++)
	{
		IVec2 map_pos;
		eaDestroyEx(&maps[i]->object_instances, NULL); // frees the array and the elements

		if (maps[i]->terrain_mesh && !maps[i]->terrain_mesh->subdiv.cell_div)
			subdivCreate(&maps[i]->terrain_mesh->subdiv, maps[i]->terrain_mesh->mesh);

		heightMapGetMapLocalPos(maps[i], map_pos);
		stashIntAddPointer(height_map_table, getTerrainGridPosKey(map_pos), maps[i], false);
	}
	for (i = 0; i < eaSize(&object_table); i++)
	{
		TerrainObjectEntry *groupDefInfo = object_table[i];
		GroupDef *child_group = objectLibraryGetGroupDefFromRef(&groupDefInfo->objInfo, true);
		HeightMapTerrainItem *new_item;

		if(!child_group)
		{
			/*if(error_thrown)
			continue;
			error_thrown = true;
			if(height_map->zone_map_layer)
			ErrorFilenamef(height_map->zone_map_layer->filename, "Terrain Object '%s' is missing or invalid.", groupDefInfo->objInfo.name_str); 
			else
			Errorf("Terrain Object '%s' is missing or invalid: Unknown Layer", groupDefInfo->objInfo.name_str);*/ 
			continue;
		}

		groupSetBounds(child_group, false);

		new_item = calloc(1, sizeof(HeightMapTerrainItem));
		new_item->id = i;
		new_item->info = groupDefInfo;
		new_item->wrapper = wrappers[i];
		new_item->def = child_group;
		new_item->high_detail = child_group->property_structs.physical_properties.oLodProps.bHighDetail;
		new_item->num_children = exclusionGetDefVolumeGroups(child_group, &new_item->volume_groups, false, -1);
		if (new_item->num_children == 0)
		{
			HeightMapGroupParams *params = calloc(1, sizeof(HeightMapGroupParams));
			params->scale_min = child_group->property_structs.terrain_properties.fScaleMin;
			params->scale_max = child_group->property_structs.terrain_properties.fScaleMax;
			params->intensity_variation = child_group->property_structs.terrain_properties.fIntensityVariation;
			params->normal_snap_amount = child_group->property_structs.terrain_properties.fSnapToTerrainNormal;
			params->get_terrain_color = child_group->property_structs.terrain_properties.bGetTerrainColor;
			eaPush(&new_item->param_groups, params);
			new_item->multi_density = 1;
		}
		else
		{
			new_item->multi_density = child_group->property_structs.terrain_properties.fMultiExclusionVolumesDensity;
			new_item->multi_rotation = child_group->property_structs.terrain_properties.iMultiExclusionVolumesRotation;
			for (j = 0; j < new_item->num_children; j++)
			{
				GroupDef *subgroup = groupChildGetDef(child_group, child_group->children[j], false);
				HeightMapGroupParams *params = calloc(1, sizeof(HeightMapGroupParams));
				params->scale_min = child_group->property_structs.terrain_properties.fScaleMin;
				params->scale_max = child_group->property_structs.terrain_properties.fScaleMax;
				params->intensity_variation = child_group->property_structs.terrain_properties.fIntensityVariation;
				params->normal_snap_amount = child_group->property_structs.terrain_properties.fSnapToTerrainNormal;
				params->get_terrain_color = child_group->property_structs.terrain_properties.bGetTerrainColor;
				eaPush(&new_item->param_groups, params);
			}
		}
		new_item->max_radius = 0;
#ifdef EXCLUSION_DEBUG
		new_item->stats = calloc(1, sizeof(HeightMapExcludeStat)*eaSize(&object_table));
#endif
		for (j = 0; j < eaSize(&new_item->volume_groups); j++)
		{
			for (k = 0; k < eaSize(&new_item->volume_groups[j]->volumes); k++)
			{
				ExclusionVolume *volume = new_item->volume_groups[j]->volumes[k];
				if (volume->type == EXCLUDE_VOLUME_SPHERE)
				{
					F32 radius = lengthVec3(volume->mat[3]) + MAX(volume->begin_radius, volume->end_radius);
					new_item->max_radius = MAX(new_item->max_radius, radius);
				}
				else
				{
					F32 radius = lengthVec3(volume->mat[3]) + distance3(volume->extents[0], volume->extents[1]);
					new_item->max_radius = MAX(new_item->max_radius, radius);
				}
			}
		}
		if (exclusion_flag_old_priorities)
		{
			new_item->priority = child_group->property_structs.terrain_properties.fExcludePriority;
		}
		else
		{
			new_item->priority = child_group->bounds.radius;
			new_item->priority *= child_group->property_structs.terrain_properties.fExcludePriorityScale;
		}

		// Insertion sort
		for (j = 0; j < eaSize(&items); j++)
			if (new_item->priority > items[j]->priority ||
				(new_item->priority == items[j]->priority && new_item->def->name_uid > items[j]->def->name_uid))
				break; 
		if (j == eaSize(&items))
			eaPush(&items, new_item);
		else
			eaInsert(&items, new_item, j);
	}
	if (eaSize(&items) && !dontBeStupid)
	{
		MersenneTable *table = mersenneTableCreate(0x5566abab);
		F32 priority_total = 0;
		int max_objects = 1000*eaSize(&maps);
		HeightMapExcludeGrid *grid;
		int block_width = (block_range->range.max_block[0]+1-block_range->range.min_block[0])*GRID_BLOCK_SIZE;
		int block_height = (block_range->range.max_block[2]+1-block_range->range.min_block[2])*GRID_BLOCK_SIZE;
		TerrainObjectRecurseBounds bounds = { 
			block_range->range.min_block[0]*GRID_BLOCK_SIZE, block_range->range.min_block[2]*GRID_BLOCK_SIZE, 
			block_width, block_height };

		loadstart_printf("Calculating VOLUMETRIC exclusion (");
		terrainPrintExclusionFlags();
		printf(")...");

		grid = exclusionGridCreate(bounds.range_min_x, bounds.range_min_z, block_width, block_height);

		for (i = 0; i < grid->width*grid->height; i++)
		{
			eaPush(&grid->elements[i].excluders, world_excluders);
		}

		// Throw out objects that aren't actually on the map
		for (i = eaSize(&items)-1; i >= 0; --i)
		{
			bool found = false;
			for (j = 0; j < eaSize(&maps); j++)
			{
				if (heightMapHasObjectGroup(maps[j], object_lookup, orig_object_table, items[i]))
				{
					priority_total += items[i]->priority;
					found = true;
					break;
				}
			}
			if (!found)
			{
				if (items[i]->volume_groups)
				{
					eaDestroyEx(&items[i]->volume_groups, exclusionVolumeGroupDestroy);
				}
				SAFE_FREE(items[i]);
				eaRemove(&items, i);
			}
		}

		for (i = 0; i < eaSize(&items); i++)
		{
			items[i]->grid = exclusionGridCreate(bounds.range_min_x, bounds.range_min_z, block_width, block_height);
		}

		global_test_num = 1;

		if (exclusion_flag_traverse_method == 1)
		{
			// For each object, do a complete sweep
			for (j = 0; j < eaSize(&items); j++)
			{
				i = 0;
				while (++i < max_objects &&
						heightMapPlaceTerrainObjectSearch(table, height_map_table, &height_map_cache, height_map_defs, grid, &bounds, object_lookup, orig_object_table, items[j], color_shift, playable))
				{
					items[j]->place_count++;
				}
				if (i == max_objects)
					printf("TOO MANY OBJECTS! ");
			}
		}
		else if (exclusion_flag_traverse_method == 2)
		{
			// Place objects
			i = 0;
			while (priority_total > 0 && i < max_objects)
			{
				// Pick a random item biased by priority
				F32 rand = randomMersennePositiveF32(table)*priority_total;
				int item_idx = -1;

				for (j = 0; j < eaSize(&items)-1; j++)
				{
					if (rand < items[j]->priority)
					{
						item_idx = j;
						break;
					}
					else
					{
						rand -= items[j]->priority;
					}
				}
				if (item_idx < 0)
					item_idx = eaSize(&items)-1;

				//printf("%04d %s...", i, items[item_idx]->def->name_str);

				if (!heightMapPlaceTerrainObjectSearch(table, height_map_table, &height_map_cache, height_map_defs, grid, &bounds, object_lookup, orig_object_table, items[item_idx], color_shift, playable))
				{
					// Don't draw this item again
					items[item_idx]->priority = 0;
					// Recalculate priority
					priority_total = 0;
					for (j = eaSize(&items)-1; j >= 0; --j)
					{
						priority_total += items[j]->priority;
					}
				}
				else
				{
					items[item_idx]->place_count++;
				}
				i++;
			}

			if (i == max_objects)
				printf("TOO MANY OBJECTS! ");
		}
		else
		{
			int object_count = 0;
			int index_size, item_count = eaSize(&items);
			F32 *priority_list = calloc(1, item_count*sizeof(F32));
			S32 *index_list = calloc(1, item_count*sizeof(S32));
			TerrainObjectRecurseStack stack = { 0 }; // Where we are in the traversal
			do
			{
				Vec3 out_pos;
				if (heightMapRecurseStackGetNext(&stack, &bounds, table, out_pos))
				{
					F32 rand;
					int item_idx = -1;
					// Initialize priority and index lists
					for (i = 0; i < item_count; i++)
						priority_list[i] = items[i]->priority;
					index_size = 0;
					// Create index list based on priority list
					do
					{
						priority_total = 0;
						for (i = 0; i < item_count; i++)
							priority_total += priority_list[i];

						if (priority_total > 0)
						{
							rand = randomMersennePositiveF32(table)*priority_total;

							item_idx = -1;
							for (i = 0; i < item_count-1; i++)
							{
								if (rand < priority_list[i])
								{
									item_idx = i;
									break;
								}
								else
								{
									rand -= priority_list[i];
								}
							}
							if (item_idx < 0)
								item_idx = item_count-1;
							index_list[index_size++] = item_idx;
							priority_list[item_idx] = 0;
						}
					} while (priority_total > 0);

					// Place objects from index list
					for (i = 0; i < item_count; i++)
					{
						if (heightMapPlaceTerrainObject(table, height_map_table, &height_map_cache, height_map_defs, grid, &bounds, object_lookup, orig_object_table, items[index_list[i]], color_shift, out_pos, playable))
						{
							object_count++;
							items[index_list[i]]->place_count++;
						}
					}
				}
			} while (!stack.done && object_count < max_objects);

			if (object_count == max_objects)
				printf("TOO MANY OBJECTS! ");
		}
		for (i = 0; i < grid->width*grid->height; i++)
		{
			eaRemove(&grid->elements[i].excluders, 0);
		}
		exclusionGridFree(grid);

		// Debug output
#ifdef EXCLUSION_DEBUG
		{
			int total_count = 0;
			int total_early_out = 0;
			int total_sphere_sphere = 0;
			int total_sphere_box = 0;
			int total_box_box = 0;
			printf("\n");
			for (i = 0; i < eaSize(&items); i++)
			{
				printf("%s: (%d / %d)\n", items[i]->def->name_str, items[i]->place_count, items[i]->exclusion_debug_early_out_count);
				total_count += items[i]->place_count;
				total_early_out += items[i]->exclusion_debug_early_out_count;
				for (j = 0; j < eaSize(&items); j++)
				{
					int idx = items[j]->id;
					if (items[i]->stats[idx].exclusion_debug_sphere_sphere_count > 0
						|| items[i]->stats[idx].exclusion_debug_sphere_box_count > 0
						|| items[i]->stats[idx].exclusion_debug_box_box_count)
					{
						printf(" <> %s (%d / %d / %d)\n", items[j]->def->name_str, items[i]->stats[idx].exclusion_debug_sphere_sphere_count,
							   items[i]->stats[idx].exclusion_debug_sphere_box_count, items[i]->stats[idx].exclusion_debug_box_box_count);
						total_sphere_sphere += items[i]->stats[idx].exclusion_debug_sphere_sphere_count;
						total_sphere_box += items[i]->stats[idx].exclusion_debug_sphere_box_count;
						total_box_box += items[i]->stats[idx].exclusion_debug_box_box_count;
					}
				}
			}
			printf("TOTALS: %d / %d >< %d / %d / %d\n", total_count, total_early_out, total_sphere_sphere, total_sphere_box, total_box_box);
		}
#endif
		loadend_printf("Done.");
		for (i = 0; i < eaSize(&items); i++)
		{
			if (items[i]->volume_groups)
			{
				eaDestroyEx(&items[i]->volume_groups, exclusionVolumeGroupDestroy);
			}
			exclusionGridFree(items[i]->grid);
		}
	}
	eaDestroyEx(&items, NULL);

	exclusionVolumeGroupDestroy(world_excluders->volume_group);
	SAFE_FREE(world_excluders);
	stashTableDestroy(height_map_table);
	stashTableDestroy(height_map_defs);
}
