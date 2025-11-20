// Terrain object functions
// Tom Yedwab
// Started 01/31/07


#include "ScratchStack.h"
#include "rand.h"
#include "error.h"
#include "GenericMesh.h"
#include "Color.h"

#include "wlTerrainPrivate.h"
#include "WorldGridPrivate.h"
#include "WorldCellEntryPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Terrain_System););

/////////////////////////////////////////////////////////////////////////////////////////
//  DEPRECATED - ONLY USED FOR BACKWARD COMPATIBILITY
/////////////////////////////////////////////////////////////////////////////////////////

typedef struct HeightMapExcludeObject
{
	U32 group_id;
	int x, z;
	Mat4 mat;
	F32 priority;

	F32 same_radius_sq;
	F32 others_radius_begin_sq;
	F32 others_radius_end_sq;
} HeightMapExcludeObject;

void terrainFreeTerrainBinnedObjectGroup(TerrainBinnedObjectGroup *object_group)
{
	eaDestroyEx(&object_group->objects, NULL);
	free(object_group);
}

TerrainObjectEntry *terrainGetLayerObject(ZoneMapLayer *layer, int index)
{
	if (index >= 0 && index < eaSize(&layer->terrain.object_table))
	{
		return layer->terrain.object_table[index];
	}
	return NULL;
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
	normalVec3(normal_mat[0]);
	crossVec3(normal_mat[0], normal_mat[1], normal_mat[2]);
	copyMat3(normal_mat, matrix);
	copyVec3(position, matrix[3]);
	assert(validateMat4(matrix));
	assert(isNonZeroMat3(matrix));
}

void ecology_create_group(GroupDef *parent_def, GroupDefLib *def_lib, TerrainBinnedObject *new_obj, GroupDef *child_group, U32 *id_counter)
{
	char node_name_orig[64], node_name[64];
	GroupDef *def;
	GroupChild *ent;
	Vec3 tint_color;

	sprintf(node_name_orig, "inst_%d", *id_counter);
	groupLibMakeGroupName(def_lib, node_name_orig, SAFESTR(node_name), 0);
	def = groupLibNewGroupDef(def_lib, NULL, 0, node_name, 0, true, true);
	(*id_counter)++;

	// Add group to list
	groupChildInitialize(parent_def, -1, def, NULL, 0, new_obj->seed, eaSize(&parent_def->children)+1);
	groupDefModify(parent_def, eaSize(&parent_def->children)-1, true);

	// Add the group as a child
	ent = groupChildInitialize(def, 0, child_group, NULL, 0, 0, 1);
	getMatForTerrainObject(ent->mat, new_obj->position, new_obj->normal, new_obj->rotation);

	scaleVec3(new_obj->tint, new_obj->intensity*2*U8TOF32_COLOR, tint_color); 
	//setVec3same(tint_color, new_obj->intensity);
	groupSetTintColor(def, tint_color);

	groupSetBounds(def, true);
}

void ecology_create_instanced_group(GroupDef *parent_def, TerrainBinnedObject *new_obj, GroupDef *child_group)
{
	int i;
	Mat4 world_matrix;
	GroupInstanceBuffer *instance_buffer = NULL;
	WorldModelInstanceInfo *info;
	Model *model = child_group->model;
	F32 *scale = child_group->model_scale;
	WorldPhysicalProperties *props = &child_group->property_structs.physical_properties;
	Vec3 tint_color;

	if (!model)
	{
		GroupDef *child_def;
		int idx = new_obj->seed % eaSize(&child_group->children);
		if (eaSize(&child_group->children) == 0)
			return;
		child_def = groupChildGetDef(child_group, child_group->children[idx], false);
		model = child_def->model;
		scale = child_def->model_scale;
		props = &child_group->property_structs.physical_properties;
	}
	if (!model)
		return;

	getMatForTerrainObject(world_matrix, new_obj->position, new_obj->normal, new_obj->rotation);
	assert(scale[0] && scale[1] && scale[2]);
	scaleMat3Vec3(world_matrix, scale);
	scaleVec3(new_obj->tint, new_obj->intensity*2*U8TOF32_COLOR, tint_color); 
	info = createInstanceInfo(world_matrix, tint_color, model, props, child_group);

	for (i = 0; i < eaSize(&parent_def->instance_buffers); i++)
	{
		if (parent_def->instance_buffers[i]->model == model)
		{
			instance_buffer = parent_def->instance_buffers[i];
		}
	}

	if (!instance_buffer)
	{
		instance_buffer = calloc(1, sizeof(GroupInstanceBuffer));
		eaPush(&parent_def->instance_buffers, instance_buffer);
		instance_buffer->model = model;
	}

	eaPush(&instance_buffer->instances, info);
}

// this function ignores the objlib, so is not a thing you should use in general
void sloppyFastRemoveGroupChild(GroupDef *def, int uidInParent)
{
	int index;
	GroupChild *child;
	GroupDef *child_def;

	if (!def)
		return;
	for (index = 0; index < eaSize(&def->children); index++)
	{
		if (def->children[index]->uid_in_parent == uidInParent)
			break;
	}
	if (index >= eaSize(&def->children))
	{
		devassert(0);
		return;
	}

	journalDef(def);
	child = def->children[index];
	eaRemove(&def->children, index);

	child_def = groupChildGetDef(def, child, true);
	if (child_def)
	{
		// Here is the sloppy dangerous part - also does not recurse
		groupDefFree(child_def);
	}
	StructDestroy(parse_GroupChild, child);

	groupDefModify(def, UPDATE_REMOVED_CHILD, true);

	//filelog_printf("objectLog", "sloppyFastRemoveGroupChild SUCCEEDED: Index %d removed from %d %s", index, def->name_uid, def->name_str);
}


GroupDef *create_heightmap_def(HeightMap *height_map, GroupDef *parent_def, GroupDefLib *def_lib, U32 *id_counter, F32 color_shift)
{
	GroupDef *hmp_def = NULL;
	char node_name_orig[64], node_name[64];

	sprintf(node_name_orig, "Eco_%d_%d", height_map->map_local_pos[0], height_map->map_local_pos[1]);
	devassert(groupGetChildByName(parent_def,node_name_orig) == NULL);
	groupLibMakeGroupName(def_lib, node_name_orig, SAFESTR(node_name), 0);

	assert(strcmp(node_name,node_name_orig) == 0);
	hmp_def = groupLibNewGroupDef(def_lib, NULL, 0, node_name, 0, true, true);

	if(color_shift)
	{
		Vec3 shift_vec = {0,0,0};
		shift_vec[0] = color_shift;
		groupSetTintOffset(hmp_def, shift_vec);
	}

	// Add group to list
	groupChildInitialize(parent_def, -1, hmp_def, NULL, 0, 0, 0);
	groupDefModify(parent_def, eaSize(&parent_def->children)-1, true);
	return hmp_def;
}

GroupDef *create_heightmap_sub_def(IVec2 rel_pos, GroupDef *parent_def, GroupDefLib *def_lib, U32 *id_counter, int i, F32 color_shift)
{
	GroupDef *hmp_def = NULL;
	char node_name_orig[64], node_name[64];

	sprintf(node_name_orig, "eco_%d_%d_%d", rel_pos[0], rel_pos[1], i);
	groupLibMakeGroupName(def_lib, node_name_orig, SAFESTR(node_name), 0);
	hmp_def = groupLibNewGroupDef(def_lib, NULL, 0, node_name, 0, true, true);

	if(color_shift)
	{
		Vec3 shift_vec = {0,0,0};
		shift_vec[0] = color_shift;
		groupSetTintOffset(hmp_def, shift_vec);
	}

	hmp_def->property_structs.physical_properties.iWeldInstances = 1;

	// Add group to list
	groupChildInitialize(parent_def, -1, hmp_def, NULL, 0, 0, 0);
	groupDefModify(parent_def, eaSize(&parent_def->children)-1, true);

	return hmp_def;
}

// TomY TODO deprecate?
// void terrainFreeObjectGroups(GroupDef *objects_root_def, GroupFile *objects_file)
// {
// 	StashElement result;
// 	char *defname;
//     int i;
// 	strdup_alloca(defname, objects_root_def->name_str);
// 
// 	SAFE_FREE(objects_root_def->def_children.children);
// 	objects_root_def->def_children.child_count = 0;
// 
// 	objects_file->freeing = 1;
// 	for (i = eaSize(&objects_file->defs) - 1; i >= 0; --i)
// 	{
// 		if (objects_file->defs[i] != objects_root_def)
// 			groupDefFree(objects_file->defs[i]);
// 	}
// 	eaSetSize(&objects_file->defs, 1);
// 
// 	stashTableDestroy(objects_file->defs_by_name);
//	stashTableClearEx(objects_file->def_names_by_uid, NULL, freeWrapper);
// 
// 	objects_file->defs_by_name = stashTableCreateWithStringKeys(1024,StashDeepCopyKeys_NeverRelease);
// 	stashAddPointerAndGetElement(objects_file->defs_by_name, defname, objects_root_def, false, &result);
// 	objects_root_def->name_str = stashElementGetStringKey(result);
// 
// 	mpCompactPools();
// }


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

#define MAX_GROUP_SIZE 512
#define MAX_GROUP_DEPTH 4
#define MAX_GROUP_NUM 1024
#define DESIRED_SIZE_RADIUS_MULTIPLIER 10.f

static void terrain_subdivide_group(TerrainBinnedObject **objects, int group_depth, int group_num, int *num_groups, int count, U32 min_x, U32 min_z, U32 max_x, U32 max_z, F32 size, F32 desired_size)
{
	int i, new_group_1, new_group_2, new_group_3, count_1, count_2, count_3;
	U32 mid_x, mid_z;

	if (count == 0)
		return;

	if (group_depth > MAX_GROUP_DEPTH)
		return;
	if (group_num >= MAX_GROUP_NUM-1)
		return;
	if (size <= desired_size && count < MAX_GROUP_SIZE)
		return;

	// Subdivide
	mid_x = (min_x + max_x) / 2;
	mid_z = (min_z + max_z) / 2;
	size *= 0.5f;
	new_group_1 = *num_groups;
	new_group_2 = new_group_1+1;
	new_group_3 = new_group_1+2;
	count = count_1 = count_2 = count_3 = 0;
	for (i = 0; i < eaSize(&objects); i++)
		if (objects[i]->weld > 0 && objects[i]->weld_group == group_num)
		{
			if (objects[i]->x >= mid_x && objects[i]->z < mid_z) // new group 1
			{
				objects[i]->weld_group = new_group_1;
				++count_1;
			}
			else if (objects[i]->x < mid_x && objects[i]->z >= mid_z) // new group 2
			{
				objects[i]->weld_group = new_group_2;
				++count_2;
			}
			else if (objects[i]->x >= mid_x && objects[i]->z >= mid_z) // new group 3
			{
				objects[i]->weld_group = new_group_3;
				++count_3;
			}
			else
			{
				++count;
			}
		}
	*num_groups += 3;

	terrain_subdivide_group(objects, group_depth+1, group_num, num_groups, count, min_x, min_z, mid_x, mid_z, size, desired_size);
	terrain_subdivide_group(objects, group_depth+1, new_group_1, num_groups, count_1, mid_x, min_z, max_x, mid_z, size, desired_size);
	terrain_subdivide_group(objects, group_depth+1, new_group_2, num_groups, count_2, min_x, mid_z, mid_x, max_z, size, desired_size);
	terrain_subdivide_group(objects, group_depth+1, new_group_3, num_groups, count_3, mid_x, mid_z, max_x, max_z, size, desired_size);
}

typedef struct heightMapObjectGroupItem
{
    TerrainObjectEntry *info;
	TerrainObjectWrapper *wrapper;
    GroupDef *def;
	bool high_detail;
    F32 priority;
    F32 scale_min;
    F32 scale_max;
    F32 exclude_same;
    F32 exclude_others_begin;
    F32 exclude_others_end;
    F32 intensity_variation;
    F32 normal_snap_amount;
    S32 weld_type;
    bool get_terrain_color;
} heightMapObjectGroupItem;

// Only called when layer is loaded FROM SOURCE (or while editing)
static void heightMapCreateObjectGroups_OldExclusion(HeightMap *height_maps[3][3], int *object_lookup, TerrainObjectEntry **object_table,
		                                 heightMapObjectGroupItem *item, bool use_exclusion, F32 color_shift)
{
	int x, z, i, j, num_groups;
    HeightMap *height_map = height_maps[1][1];
	TerrainBuffer *objects_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);
	TerrainBuffer *normal_buffer = NULL;
	TerrainBuffer *color_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, height_map->loaded_level_of_detail-2);
	GroupDef *hmp_def = NULL;
	bool error_thrown = false;
	F32 inv_scale;
	GroupDef **hmp_sub_def;
    // Clamp number between 0 and 255 (works for negative numbers as well)
    U32 clamped_x = height_map->map_local_pos[0] & 0xFF;
    U32 clamped_y = height_map->map_local_pos[1] & 0xFF;
	SubdivArrays *subdiv = SAFE_MEMBER_ADDR(height_map->terrain_mesh, subdiv);
	GMesh *mesh = SAFE_MEMBER(height_map->terrain_mesh, mesh);
	TerrainObjectBuffer *objects_upsample = NULL;
	S32 stride;

	// We might have been REMOVING objects, so we need to clear out the height map def regardless
	if (item->wrapper->root_def)
	{
		GroupChild * pOldGroupChild;
		char node_name[64];
		
		sprintf(node_name, "Eco_%d_%d", height_map->map_local_pos[0], height_map->map_local_pos[1]);
		pOldGroupChild = groupGetChildByName(item->wrapper->root_def,node_name);
		if (pOldGroupChild)
		{
			sloppyFastRemoveGroupChild(item->wrapper->root_def,pOldGroupChild->uid_in_parent);
		}
	}
	
	if(!objects_buffer || eaSize(&objects_buffer->data_objects) == 0)
		return;

	if ((item->high_detail || item->def->bounds.radius < 30) && height_map->zone_map_layer->terrain.non_playable)
		return;

	if (!subdiv)
		normal_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_NORMAL, height_map->loaded_level_of_detail);

	inv_scale = 1.f/(1<<objects_buffer->lod);

	hmp_sub_def = ScratchAlloc(MAX_GROUP_NUM * sizeof(GroupDef*));

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
            groupDefInfo = terrainGetLayerObject(height_map->zone_map_layer, objects_buffer->data_objects[i]->object_type);
        }

		if (groupDefInfo &&
            groupDefInfo == item->info)
		{
			// Do temporary upsample
			if (!objects_upsample)
			{
				objects_upsample = ScratchAlloc(sizeof(TerrainObjectBuffer));
				objects_upsample->density = calloc((GRID_BLOCK_SIZE+1)*(GRID_BLOCK_SIZE+1), sizeof(TerrainObjectDensity));
			}
			stride = (GRID_BLOCK_SIZE)/(objects_buffer->size-1);
			for (z = 0; z <= GRID_BLOCK_SIZE; z+=stride)
			{
				for (x = 0; x <= GRID_BLOCK_SIZE; x+=stride)
				{
					objects_upsample->density[x+z*(U32)(GRID_BLOCK_SIZE+1)] = 
						objects_buffer->data_objects[i]->density[(x/stride) + (z/stride)*objects_buffer->size];
				}
			}
			for (j = objects_buffer->lod-1; j >= 0; j--)
			{
				stride /= 2;
				terrainSubdivObjectDensity(objects_upsample->density, stride, GRID_BLOCK_SIZE+1, GRID_BLOCK_SIZE+1);
			}

			ZeroStructs(hmp_sub_def, MAX_GROUP_NUM);
            
			for (z = 0; z < GRID_BLOCK_SIZE; z++) // Skip the last row and last column, because they overlap with neighbors
			{
                S32 gz = z + GRID_BLOCK_SIZE*height_map->map_local_pos[1];
				for (x = 0; x < GRID_BLOCK_SIZE; x++)
				{
                    S32 gx = x + GRID_BLOCK_SIZE*height_map->map_local_pos[0];
					U32 random_seed;
					U32 probability;
					MersenneTable* pRandTable;
                    bool early_out = false;
                    F32 exclude_weight = 1.f;
					U8 *normal_pt;
					Vec4 color_pt = { 255.f, 255.f, 255.f, 255.f };

					probability = objects_upsample->density[x+z*(U32)(GRID_BLOCK_SIZE+1)];

					if (probability < 0.1)
						continue;

                    if (use_exclusion)
                    {
                        random_seed = (random_eco_x[(x*7+z*5)	 & 0xFF] ^ (random_eco_z[clamped_x]&0xC00F) ^ 
                                       random_eco_z[(x*3+z*11) & 0xFF] ^ (random_eco_x[clamped_y]&0xC00F) ^ 
                                       random_eco_type[groupDefInfo->seed%64] ^ 
                                       (groupDefInfo->seed*175)) & 0xFFFF; // New method
                    }
                    else
                    {
                    	random_seed = ((random_eco_x[(x*7+z*5)	 & 0xFF]+random_eco_z[clamped_x]) ^ 
                                       (random_eco_z[(x*3+z*11) & 0xFF]+random_eco_x[clamped_y]) ^ 
                                       random_eco_type[groupDefInfo->seed%64] ^ 
                                       (groupDefInfo->seed*175)) & 0xFFFF; // Old method
                    }

					if (random_seed >= probability)
						continue;

                    if (use_exclusion)
                    {
                        for (j = 0; j < eaSize(&height_map->exclude_objects); j++)
                        {
                            HeightMapExcludeObject *exclude = height_map->exclude_objects[j];
                            F32 dist_sq = (exclude->x-gx)*(exclude->x-gx) + (exclude->z-gz)*(exclude->z-gz);
                            if ((exclude->priority == item->priority && dist_sq < exclude->same_radius_sq) ||
                                (dist_sq < exclude->others_radius_begin_sq))
                            {
                                early_out = true;
                                break;
                            }
                            if (dist_sq < exclude->others_radius_end_sq)
                                exclude_weight *= (dist_sq-exclude->others_radius_begin_sq)/(exclude->others_radius_end_sq-exclude->others_radius_begin_sq);
                        }
                        if (early_out)
                            continue;
                        for (j = 0; j < eaSize(&height_map->exclude_neighbors); j++)
                        {
                            HeightMapExcludeObject *exclude = height_map->exclude_neighbors[j];
                            F32 dist_sq = (exclude->x-gx)*(exclude->x-gx) + (exclude->z-gz)*(exclude->z-gz);
                            if ((exclude->priority == item->priority && dist_sq < exclude->same_radius_sq) ||
                                (dist_sq < exclude->others_radius_begin_sq))
                            {
                                early_out = true;
                                break;
                            }
                            if (dist_sq < exclude->others_radius_end_sq)
                                exclude_weight *= (dist_sq-exclude->others_radius_begin_sq)/(exclude->others_radius_end_sq-exclude->others_radius_begin_sq);
                        }
                        if (early_out)
                            continue;
                        
                        if (probability*exclude_weight < 0.1 || random_seed >= probability*exclude_weight)
                            continue;
                    }

					if (color_buffer)
					{
						U8 *src = color_buffer->data_u8vec3[(x>>color_buffer->lod)+(z>>color_buffer->lod)*color_buffer->size];
						color_pt[0] = src[2];
						color_pt[1] = src[1];
						color_pt[2] = src[0];
					}

					pRandTable = mersenneTableCreate(random_seed + x + z * GRID_BLOCK_SIZE);

                    {
						TerrainBinnedObject *new_obj = calloc(1, sizeof(TerrainBinnedObject));
						F32 rand_x = x + randomMersennePositiveF32(pRandTable);
						F32 rand_z = z + randomMersennePositiveF32(pRandTable);
						F32 final_x = rand_x + (height_map->map_local_pos[0] * GRID_BLOCK_SIZE);
						F32 final_z = rand_z + (height_map->map_local_pos[1] * GRID_BLOCK_SIZE);
						int px = x >> objects_buffer->lod;
						int pz = z >> objects_buffer->lod;
						Vec3 normal = {0, 1, 0};
						F32 final_y = 0;
						
						if (subdiv && mesh)
						{
							Vec3 pos = {rand_x, 0, rand_z};
							int *tri_idxs = subdivGetBucket(subdiv, pos);

							for (j = 0; j < eaiSize(&tri_idxs); ++j)
							{
								GTriIdx *tri = &mesh->tris[tri_idxs[j]];
								Vec3 barycentric_coords;

								if (nearSameVec3XZTol(pos, mesh->positions[tri->idx[0]], 0.03f))
								{
									final_y = mesh->positions[tri->idx[0]][1];
									if (mesh->normals)
										copyVec3(mesh->normals[tri->idx[0]], normal);
									else
										makePlaneNormal(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]], normal);
									break;
								}
								else if (nearSameVec3XZTol(pos, mesh->positions[tri->idx[1]], 0.03f))
								{
									final_y = mesh->positions[tri->idx[1]][1];
									if (mesh->normals)
										copyVec3(mesh->normals[tri->idx[1]], normal);
									else
										makePlaneNormal(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]], normal);
									break;
								}
								else if (nearSameVec3XZTol(pos, mesh->positions[tri->idx[2]], 0.03f))
								{
									final_y = mesh->positions[tri->idx[2]][1];
									if (mesh->normals)
										copyVec3(mesh->normals[tri->idx[2]], normal);
									else
										makePlaneNormal(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]], normal);
									break;
								}
								else if (calcBarycentricCoordsXZProjected(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]], pos, barycentric_coords))
								{
									final_y = barycentric_coords[0] * mesh->positions[tri->idx[0]][1] + barycentric_coords[1] * mesh->positions[tri->idx[1]][1] + barycentric_coords[2] * mesh->positions[tri->idx[2]][1];
									makePlaneNormal(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]], normal);
									break;
								}
							}
						}
						else
						{
							final_y = heightMapGetInterpolatedHeight(height_map,px,pz,(rand_x*inv_scale)-px,(rand_z*inv_scale)-pz);
							normal_pt = normal_buffer ? normal_buffer->data_normal[(x>>normal_buffer->lod)+(z>>normal_buffer->lod)*normal_buffer->size] : NULL;
							if (normal_pt)
								setVec3(normal, -(normal_pt[2]-128)*(1.f/127.f), (normal_pt[1]-128)*(1.f/127.f), (normal_pt[0]-128)*(1.f/127.f));
						}

						new_obj->group_id = groupDefInfo->objInfo.name_uid;
						new_obj->scale = randomMersennePositiveF32(pRandTable) * (item->scale_max - item->scale_min) + item->scale_min;
						new_obj->rotation = randomMersennePositiveF32(pRandTable)*6.2831853072f;
						new_obj->intensity = randomMersennePositiveF32(pRandTable) * item->intensity_variation + (1.f - item->intensity_variation);
						if (item->normal_snap_amount > 0.f)
						{
							scaleVec3(normal, item->normal_snap_amount, new_obj->normal);
							new_obj->normal[1] += 1.f - item->normal_snap_amount;
							normalVec3(new_obj->normal);
							if (!new_obj->normal[0] && !new_obj->normal[1] && !new_obj->normal[2])
								setVec3(new_obj->normal, 0, 1, 0);
						}
						else
						{
							setVec3(new_obj->normal, 0, 1, 0);
						}
						if (item->get_terrain_color)
							copyVec3(color_pt, new_obj->tint);
						else
							setVec3(new_obj->tint, 127, 127, 127); // this will be multiplied by 2 when the groupdefs are created
						setVec3(new_obj->position, final_x, final_y, final_z);
						new_obj->seed = randomMersenneU32(pRandTable);
						new_obj->x = x;
						new_obj->z = z;

						new_obj->weld = item->weld_type;

						if (new_obj->weld == 0)
						{
							if (item->wrapper->def_lib)
							{
								if (!hmp_def) 
									hmp_def = create_heightmap_def(height_map, item->wrapper->root_def, item->wrapper->def_lib, &item->wrapper->id, color_shift);

								// Here is where the defs will be created that will later be used to create actual visible objects
								ecology_create_group(hmp_def, item->wrapper->def_lib,
									new_obj, item->def, &item->wrapper->id);
							}
						}
						else
						{
							eaPush(&instance_list, new_obj);
							new_obj->weld_group = 0;
						}

						// Object instances is only used for binning.  The binned data is never used as far as I can tell.  [RMARR - 6/28/13]
						eaPush(&height_map->object_instances, new_obj);

                        if (use_exclusion && (item->exclude_same > 1.f || item->exclude_others_end > 1.f))
                        {
                            F32 max_radius = MAX(item->exclude_same, item->exclude_others_end);
							HeightMapExcludeObject *exclude = calloc(1, sizeof(HeightMapExcludeObject));
                            exclude->x = gx;
                            exclude->z = gz;
                            exclude->same_radius_sq = item->exclude_same * item->exclude_same;
                            exclude->others_radius_begin_sq = item->exclude_others_begin * item->exclude_others_begin;
                            exclude->others_radius_end_sq = item->exclude_others_end * item->exclude_others_end;
                            exclude->priority = item->priority;
                            eaPush(&height_map->exclude_objects, exclude);

                            if (x - max_radius < 0)
                            {
                                if (z - max_radius < 0 && height_maps[0][0])
                                    eaPush(&height_maps[0][0]->exclude_neighbors, exclude);
                                if (height_maps[0][1])
	                                eaPush(&height_maps[0][1]->exclude_neighbors, exclude);
                                if (z + max_radius >= GRID_BLOCK_SIZE && height_maps[0][2])
                                    eaPush(&height_maps[0][2]->exclude_neighbors, exclude);
                            }
                            
                            if (z - max_radius < 0 && height_maps[1][0])
                                eaPush(&height_maps[1][0]->exclude_neighbors, exclude);
                            if (z + max_radius >= GRID_BLOCK_SIZE && height_maps[1][2])
                                eaPush(&height_maps[1][2]->exclude_neighbors, exclude);
                            
                            if (x + max_radius >= GRID_BLOCK_SIZE)
                            {
                                if (z - max_radius < 0 && height_maps[2][0])
                                    eaPush(&height_maps[2][0]->exclude_neighbors, exclude);
                                if (height_maps[2][1])
	                                eaPush(&height_maps[2][1]->exclude_neighbors, exclude);
                                if (z + max_radius >= GRID_BLOCK_SIZE && height_maps[2][2])
                                    eaPush(&height_maps[2][2]->exclude_neighbors, exclude);
                            }
                        }
					}
					mersenneTableFree(pRandTable);
				}
			}

			num_groups = 1;
			terrain_subdivide_group(instance_list, 0, 0, &num_groups, eaSize(&instance_list), 0, 0, 256, 256, 256.f * 1.415f, item->def->bounds.radius * DESIRED_SIZE_RADIUS_MULTIPLIER);
			for (j = 0; j < eaSize(&instance_list); j++)
			{
				if (!hmp_def && item->wrapper->def_lib) 
					hmp_def = create_heightmap_def(height_map, item->wrapper->root_def, item->wrapper->def_lib, &item->wrapper->id, color_shift);
				if (instance_list[j]->weld > 0 && hmp_def)
				{
					TerrainBinnedObject *obj = instance_list[j];
					if (!hmp_sub_def[obj->weld_group])
						hmp_sub_def[obj->weld_group] = create_heightmap_sub_def(height_map->map_local_pos, hmp_def, item->wrapper->def_lib, &item->wrapper->id, obj->weld_group, color_shift);
					if (obj->weld == 1)
						ecology_create_group(hmp_sub_def[obj->weld_group], item->wrapper->def_lib,
							obj, item->def, &item->wrapper->id);
					else
						ecology_create_instanced_group(hmp_sub_def[obj->weld_group], obj, item->def);
				}
			}
			eaDestroy(&instance_list);
		}
	}

	if (hmp_def)
	{
		groupSetBounds(hmp_def, true);
	}

	if (objects_upsample)
	{
		SAFE_FREE(objects_upsample->density);
		ScratchFree(objects_upsample);
	}
	ScratchFree(hmp_sub_def);
}

void terrainCreateObjectGroupsHelper_OldExclusion(HeightMap **maps, TerrainObjectWrapper **wrappers,
							   int *object_lookup, TerrainObjectEntry **orig_object_table, 
                               TerrainObjectEntry **object_table, TerrainExclusionVersion exclusion_version, F32 color_shift, bool bForce)
{
    int i, j;
    heightMapObjectGroupItem **items = NULL;

    if (eaSize(&maps) == 0)
        return;

	assert(eaSize(&wrappers) == eaSize(&object_table));

	for (i = 0; i < eaSize(&maps); i++)
	{
		if (bForce ||
			heightMapWasTouched(maps[i], TERRAIN_BUFFER_HEIGHT) ||
			heightMapWasTouched(maps[i], TERRAIN_BUFFER_COLOR) ||
			heightMapWasTouched(maps[i], TERRAIN_BUFFER_OBJECTS))
		{
			eaDestroyEx(&maps[i]->object_instances, NULL); // frees the array and the elements
			eaDestroyEx(&maps[i]->exclude_objects, NULL);
			eaDestroy(&maps[i]->exclude_neighbors);

			if (maps[i]->terrain_mesh && !maps[i]->terrain_mesh->subdiv.cell_div)
				subdivCreate(&maps[i]->terrain_mesh->subdiv, maps[i]->terrain_mesh->mesh);
		}
	}
	for (i = 0; i < eaSize(&object_table); i++)
    {
        TerrainObjectEntry *groupDefInfo = object_table[i];
        GroupDef *child_group = objectLibraryGetGroupDefFromRef(&groupDefInfo->objInfo, true);
        heightMapObjectGroupItem *new_item;
			
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

        new_item = calloc(1, sizeof(heightMapObjectGroupItem));
        new_item->info = groupDefInfo;
		new_item->wrapper = wrappers[i]; // item points to a wrapper, does not own it
        new_item->def = child_group;
        new_item->high_detail = child_group->property_structs.physical_properties.oLodProps.bHighDetail;
        new_item->priority = child_group->property_structs.terrain_properties.fExcludePriority;
        new_item->scale_min = child_group->property_structs.terrain_properties.fScaleMin;
        new_item->scale_max = child_group->property_structs.terrain_properties.fScaleMax;
        new_item->exclude_same = child_group->property_structs.terrain_properties.fExcludeSame;
        new_item->exclude_others_begin = child_group->property_structs.terrain_properties.iExcludeOthersBegin;
        new_item->exclude_others_end = (child_group->property_structs.terrain_properties.iExcludeOthersEnd ? child_group->property_structs.terrain_properties.iExcludeOthersEnd : new_item->exclude_others_begin);
        new_item->intensity_variation = child_group->property_structs.terrain_properties.fIntensityVariation;
        new_item->normal_snap_amount = child_group->property_structs.terrain_properties.fSnapToTerrainNormal;
        new_item->get_terrain_color = child_group->property_structs.terrain_properties.bGetTerrainColor;
        new_item->weld_type = child_group->property_structs.physical_properties.iWeldInstances;

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

	// This case is now mutually exclusive with volume exclusion.  So we are either doing simple exclusion, or possibly no exclusion.

	//printf("Laying out terrain objects...");

	// Looping over the items here.  items has references to things in the dummy layers.  They don't own them and the array is deleted just below this.
	// It seems to just be an intermediate data structure
    for (i = 0; i < eaSize(&items); i++)
	{
		for (j = 0; j < eaSize(&maps); j++)
		{
			if (bForce ||
				heightMapWasTouched(maps[j], TERRAIN_BUFFER_HEIGHT) ||
				heightMapWasTouched(maps[j], TERRAIN_BUFFER_COLOR) ||
				heightMapWasTouched(maps[j], TERRAIN_BUFFER_OBJECTS))
			{
				HeightMap *height_maps[3][3];
				height_maps[1][1] = maps[j];
				heightMapGetNeighbors(height_maps, maps);
				//printf("Placing %s - %d/%d  \r", items[i]->def->name_str, j, eaSize(&maps));
				heightMapCreateObjectGroups_OldExclusion(height_maps, object_lookup, orig_object_table, items[i], (exclusion_version!=EXCLUSION_NONE), color_shift);
			}
        }
		//printf("\n");
    }
	//printf("Done.\n");
    for (i = 0; i < eaSize(&maps); i++)
    {
		if (bForce ||
			heightMapWasTouched(maps[i], TERRAIN_BUFFER_HEIGHT) ||
			heightMapWasTouched(maps[i], TERRAIN_BUFFER_COLOR) ||
			heightMapWasTouched(maps[i], TERRAIN_BUFFER_OBJECTS))
		{
			eaDestroyEx(&maps[i]->exclude_objects, NULL);
			eaDestroy(&maps[i]->exclude_neighbors);
		}
    }
    eaDestroyEx(&items, NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////
//  SHARED BETWEEN V1 AND V2
/////////////////////////////////////////////////////////////////////////////////////////

void terrainCreateObjectGroupsHelper(ZoneMapLayer *layer, HeightMap **maps, TerrainBlockRange *block_range, TerrainObjectWrapper **wrappers,
									 int *object_lookup, TerrainObjectEntry **orig_object_table, 
									 TerrainObjectEntry **object_table, TerrainExclusionVersion exclusion_version, F32 color_shift, bool playable, bool bForce);
// Done during editing ONLY
void terrainCreateObjectGroupsWithLookup(HeightMap **maps, TerrainBlockRange *range, ZoneMapLayer *layer, TerrainObjectWrapper **wrappers,
                               int *object_lookup, TerrainObjectEntry **object_table, TerrainExclusionVersion exclusion_version, F32 color_shift, bool playable, bool bForce)
{
	int i;
    TerrainObjectEntry **entries_table;
	TerrainObjectEntry **entries = NULL;

	if (eaSize(&maps) == 0)
		return;

    if (object_table)
    {
        entries_table = object_table;
    }
    else
    {
        entries_table = layer->terrain.object_table;
    }

	for (i = 0; i < eaiSize(&object_lookup); i++)
	{
		assert(object_lookup[i] >= 0 && 
			object_lookup[i] < eaSize(&entries_table));
		eaPush(&entries, entries_table[object_lookup[i]]);
	}

	if (exclusion_version == EXCLUSION_VOLUMES)
	{
		terrainCreateObjectGroupsHelper(layer, maps, range, wrappers, object_lookup, object_table, entries, exclusion_version, color_shift, playable,bForce);
	}
	else
	{
		terrainCreateObjectGroupsHelper_OldExclusion(maps, wrappers, object_lookup, object_table, entries, exclusion_version, color_shift,bForce);
	}

	eaDestroy(&entries);
}

// Done during binning ONLY
void terrainCreateObjectGroups(HeightMap **maps, TerrainBlockRange *range, ZoneMapLayer *layer, TerrainObjectWrapper **wrappers,
                               TerrainObjectEntry **object_table, TerrainExclusionVersion exclusion_version, F32 color_shift)
{
	TerrainObjectEntry **entries = NULL;

	if (eaSize(&maps) == 0)
		return;

	entries = object_table ? object_table : maps[0]->zone_map_layer->terrain.object_table;
	if (exclusion_version == EXCLUSION_VOLUMES)
	{
		terrainCreateObjectGroupsHelper(layer, maps, range, wrappers, NULL, entries, entries, exclusion_version, color_shift, !layer->terrain.non_playable,true);
	}
	else
	{
		terrainCreateObjectGroupsHelper_OldExclusion(maps, wrappers, NULL, entries, entries, exclusion_version, color_shift,true);
	}
}

// Only called when layer is loaded FROM BINS, from layerLoadTerrainObjects
void heightMapUpdateObjectGroupsFromBins(ZoneMapLayer *layer)
{
	GroupDef **hmp_sub_def;
	int i, j;
	TerrainObjectWrapper *wrapper;
	F32 color_shift = layer->terrain.color_shift;

	if (eaSize(&layer->terrain.binned_instance_groups) <= 0)
	{
		eaDestroyEx(&layer->terrain.object_defs, layerFreeObjectWrapper);
		return;
	}

	layerInitObjectWrappers(layer, 1);
	wrapper = layer->terrain.object_defs[0];

	hmp_sub_def = ScratchAlloc(MAX_GROUP_NUM * sizeof(GroupDef*));

	if(color_shift)
	{
		Vec3 shift_vec = {0,0,0};
		shift_vec[0] = color_shift;
		groupSetTintOffset(wrapper->root_def, shift_vec);
	}

	// Create groups from bin data
	for (j = 0; j < eaSize(&layer->terrain.binned_instance_groups); ++j)
	{
		TerrainBinnedObjectGroup *instance_group = layer->terrain.binned_instance_groups[j];
		int last_group_id = -1;
		GroupDef *child_group = NULL;

		for (i = 0; i < eaSize(&instance_group->objects); i++)
		{
			TerrainBinnedObject *obj = instance_group->objects[i];

			if (!child_group || obj->group_id != last_group_id)
			{
				child_group = objectLibraryGetGroupDef(obj->group_id, true);
				last_group_id = obj->group_id;
				ZeroStructs(hmp_sub_def, MAX_GROUP_NUM);
			}

			if (!child_group)
				continue;

			if (obj->weld == 0)
				ecology_create_group(wrapper->root_def, wrapper->def_lib,
					obj, child_group, &wrapper->id);
			else
			{
				if (!hmp_sub_def[obj->weld_group])
					hmp_sub_def[obj->weld_group] = create_heightmap_sub_def(instance_group->rel_pos, 
																			wrapper->root_def, wrapper->def_lib, 
																			&wrapper->id, obj->weld_group, color_shift);
				if (obj->weld == 1)
					ecology_create_group(hmp_sub_def[obj->weld_group], wrapper->def_lib,
						obj, child_group, &wrapper->id);
				else
					ecology_create_instanced_group(hmp_sub_def[obj->weld_group], obj, child_group);
			}
		}

		eaDestroyEx(&instance_group->objects, NULL); // frees the array and the elements
	}

	groupSetBounds(wrapper->root_def, true);

	eaDestroyEx(&layer->terrain.binned_instance_groups, terrainFreeTerrainBinnedObjectGroup); // frees the array and the elements
	ScratchFree(hmp_sub_def);
}

void terrainUpdateObjectGroups( ZoneMapLayer *layer, bool from_bins )
{
	/*terrainFreeObjectGroups(layer->terrain.objects_root_def, layer->terrain.objects_file);
	groupDefModify(layer->terrain.objects_root_def, UPDATE_REMOVED_CHILD);
	groupSetBounds(layer->terrain.objects_root_def, true);*/

    heightMapUpdateObjectGroupsFromBins(layer);
}
