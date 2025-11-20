#define GENESIS_ALLOW_OLD_HEADERS
#include "wlGenesisSolarSystem.h"

#include "wlGenesis.h"
#include "wlGenesisExteriorNode.h" 
#include "wlGenesisExterior.h"
#include "wlGenesisMissions.h"
#include "wlUGC.h"

#include "WorldGridPrivate.h"
#include "ObjectLibrary.h"
#include "StringCache.h"
#include "ScratchStack.h"
#include "FolderCache.h"
#include "fileutil.h"
#include "rand.h"
#include "error.h"
#include "rgb_hsv.h"
#include "Expression.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

// Needed by ZoneMap Initalization
#define MAX_SHOEBOX 12

#ifndef NO_EDITORS

#define WORLD_BOUNDS 14900.0f
#define MINI_SS_SIZE 10000.0f
#define SHOEBOX_WARP_VOL_SIZE 500.0f
#define SHOEBOX_COL_BOX_SIZE 50.0f
#define SHOEBOX_OVERLAP_CORRECTION 50.0f
#define SYSTEM_OVERVIEW_SCALE 0.1f
#define SYSTEM_OVERVIEW_OBJ_SCALE 0.005f
#define DETAIL_LOC_SEARCH_RES 50.0f

static GroupDefLib *solarsystem_lib = NULL;

typedef struct SSClusterLocation {
	Vec3 loc;
	F32 size;//radius
} SSClusterLocation;

typedef struct SSClusterSearchInfo {
	SSCluster *cluster;
	SSClusterLocation **locations1;
	SSClusterLocation **locations2;
	F32 to_place_size;
	SSClusterObject *to_place_obj;
	Vec3 out_loc;
} SSClusterSearchInfo;

GroupDef *solarSystemMakeGroupDef(GroupDefLib *def_lib, const char *name)
{
	GroupDef *new_def = NULL;
	char node_name[64];

	groupLibMakeGroupName(def_lib, name, SAFESTR(node_name), 0);
	new_def = groupLibNewGroupDef(def_lib, NULL, 0, node_name, 0, false, true);
	return new_def;
}

static void solarSystemApplyChallengeParams(GenesisToPlaceObject *new_object, GenesisInstancedObjectParams *params, GenesisInteractObjectParams *inter_params, GenesisSpacePatrolType patrol_type)
{
	if( params ) {
		if( !new_object->instanced ) {
			new_object->instanced = StructCreate(parse_GenesisInstancedObjectParams);
		}
		StructCopyAll(parse_GenesisInstancedObjectParams, params, new_object->instanced);
	}
	if ( inter_params ) {
		if( !new_object->interact ) {
			new_object->interact = StructCreate(parse_GenesisInteractObjectParams);
		}
		StructCopyAll(parse_GenesisInteractObjectParams, inter_params, new_object->interact);
	}

	if (patrol_type) {
		if (!new_object->instanced) {
			new_object->instanced = StructCreate( parse_GenesisInstancedObjectParams );
		}
		new_object->instanced->has_patrol = true;
	}
}

GenesisToPlaceObject *solarSystemPlaceGroupDef(GroupDef *def, const char *name, const F32 *pos, F32 fRot, GenesisToPlaceObject *parent_object, GenesisToPlaceState *to_place)
{
	GenesisToPlaceObject *child_object = calloc(1, sizeof(GenesisToPlaceObject));
	child_object->group_def = (def && def->filename) ? objectLibraryGetEditingCopy(def, true, false) : def; // Object Library pieces need an editing copy
	child_object->mat_relative = true;
	if (name)
		child_object->object_name = allocAddString(name);
	identityMat3(child_object->mat);
	yawMat3(fRot,child_object->mat);
	if (pos)
		copyVec3(pos, child_object->mat[3]);
	child_object->parent = parent_object;
	eaPush(&to_place->objects, child_object);
	return child_object;
}

static GroupDef *solarSystemCreateDebrisFieldExcluder(GroupDefLib *def_lib, const char *name, F32 size)
{
	GroupDef *vol_def = solarSystemMakeGroupDef(def_lib, name);
	groupDefAddVolumeType(vol_def, "DebrisFieldExclusion");
	vol_def->property_structs.volume = StructCreate(parse_GroupVolumeProperties);
	vol_def->property_structs.volume->eShape = GVS_Sphere;
	vol_def->property_structs.volume->fSphereRadius = size;
	return vol_def;
}

static void solarSystemInitSphereVolume(GroupDef *volume_def, F32 rad)
{
	if(!volume_def->property_structs.volume)
		volume_def->property_structs.volume = StructCreate(parse_GroupVolumeProperties);
	volume_def->property_structs.volume->eShape = GVS_Sphere;
	volume_def->property_structs.volume->fSphereRadius = MAX(rad, 0.001f);
}

void solarSystemInitVolume(GroupDef *volume_def, Vec3 min, Vec3 max)
{
	if(!volume_def->property_structs.volume)
		volume_def->property_structs.volume = StructCreate(parse_GroupVolumeProperties);
	volume_def->property_structs.volume->eShape = GVS_Box;
	copyVec3(min, volume_def->property_structs.volume->vBoxMin);
	copyVec3(max, volume_def->property_structs.volume->vBoxMax);
}

static GroupDef* solarSystemGetGroupDefFromObject(SSLibObj* object)
{
	if (IS_HANDLE_ACTIVE(object->start_spawn_using_transition)) {
		GroupDefRef startSpawnRef;
		startSpawnRef.name_str = "Start Spawn Point";
		return objectLibraryGetGroupDefFromRef(&startSpawnRef, true);
	} else {
		return objectLibraryGetGroupDefFromRef(&object->obj, true);
	}
}

static void solarSystemValidateClusterSize(SSCluster *cluster)
{
	int i;
	F32 max_radius=32;
	if(cluster->radius <= 0 || cluster->height <= 0 || cluster->max_dist <= 0)
	{
		F32 volume = 0;
		//Find max dist
		for( i=0; i < eaSize(&cluster->cluster_objects) ; i++ )
		{
			SSClusterObject *object = cluster->cluster_objects[i];
			GroupDef *ref_group = object->lib_obj.cached_def;
			if(!ref_group)
			{
				ref_group = solarSystemGetGroupDefFromObject(&object->lib_obj);
				object->lib_obj.cached_def = ref_group;
			}
			if(ref_group)
			{
				F32 ref_rad = ref_group->bounds.radius;
				if(max_radius < ref_rad)
					max_radius = ref_rad;
			}
		}
		if(cluster->max_dist <= 0)
			cluster->max_dist = max_radius;
		//Find volume
		for( i=0; i < eaSize(&cluster->cluster_objects) ; i++ )
		{
			SSClusterObject *object = cluster->cluster_objects[i];
			GroupDef *ref_group = object->lib_obj.cached_def;
			if(!ref_group)
			{
				ref_group = solarSystemGetGroupDefFromObject(&object->lib_obj);
				object->lib_obj.cached_def = ref_group;
			}
			if(ref_group)
			{
				F32 ref_rad = ref_group->bounds.radius;
				F32 max_dist = cluster->max_dist;
				F32 radius = ref_rad + max_dist/2.0f;
				volume += PI*CUBE(radius)*4.0f/3.0f * object->count;
			}
		}
		volume *= 1.25; //So it is not such a tight fit
		volume = MAX(1.0f, volume);//ensure there is some size
		if(cluster->radius <= 0 || cluster->height <= 0)
		{
			cluster->radius = pow(volume/PI, 1.0f/3.0f); //solve for radius
			cluster->height = 2*cluster->radius;
		}
	}
}

static F32 solarSystemGetLibObjRadius(SSLibObj *lib_obj)
{
	GroupDef *ref_group = NULL;
	ref_group = solarSystemGetGroupDefFromObject(lib_obj);
	if(ref_group)
	{
		if(ref_group->property_structs.encounter_hack_properties)
			return MAX(ref_group->property_structs.encounter_hack_properties->physical_radius, ref_group->bounds.radius);
		return ref_group->bounds.radius;
	}
	return 0.0f;
}

static F32 solarSystemGetObjSetRadius(SSObjSet *obj_set)
{
	int i;
	F32 size = 0.0f;

	if(!obj_set)
		return 0.0f;

	if(obj_set->cluster)
	{
		solarSystemValidateClusterSize(obj_set->cluster);
		if(size < obj_set->cluster->radius)
			size = obj_set->cluster->radius;
	}
	for( i=0; i < eaSize(&obj_set->group_refs); i++ )
	{
		GroupDef *ref_group = obj_set->group_refs[i]->cached_def;
		if(!ref_group)
		{
			ref_group = solarSystemGetGroupDefFromObject(obj_set->group_refs[i]);
			obj_set->group_refs[i]->cached_def = ref_group;
		}
		if(ref_group)
		{
			F32 ref_radius;
			F32 sx, sz;
			//We do not care about vertical size
			sx = ref_group->bounds.max[0] - ref_group->bounds.min[0];
			sz = ref_group->bounds.max[2] - ref_group->bounds.min[2];
			ref_radius = sqrt(SQR(sx)+SQR(sz))/2.0f;
			if(size < ref_radius)
				size = ref_radius;
			if(ref_group->property_structs.encounter_hack_properties)
				size = MAX(ref_group->property_structs.encounter_hack_properties->physical_radius, size);
		}
	}
	return size;
}

static F32 solarSystemGetPointRadius(SSObjSet *point_rep, SSObjSet **mission_reps)
{
	int i;
	F32 radius = solarSystemGetObjSetRadius(point_rep);
	for( i=0; i < eaSize(&mission_reps); i++ )
	{
		F32 new_radius = solarSystemGetObjSetRadius(mission_reps[i]);
		radius = MAX(new_radius, radius);
	}
	return radius;
}

typedef bool (*FindLocationOctTreeFunc)(Vec3 pos, void *user_data);
static bool solarSystemFindLocationOctTree(Vec3 pos, F32 size_test, F32 search_size, MersenneTable *random_table, FindLocationOctTreeFunc check_func, void *user_data)
{
	int i;
	if(size_test > search_size)
	{
		//Keep descending
		U8 oct_list[8] = {0, 1, 2, 3, 4, 5, 6, 7};

		for( i=7; i >= 0; i-- )
		{
			Vec3 new_pos = {0,0,0};
			U8 quad = randomMersenneU32(random_table)%(i+1);
			switch(oct_list[quad])
			{
			case 0:
				new_pos[0] = pos[0]/2.0f;
				new_pos[1] = pos[1]/2.0f;
				new_pos[2] = pos[2]/2.0f;
				break;
			case 1:
				new_pos[0] = pos[0]/2.0f + 0.5f;
				new_pos[1] = pos[1]/2.0f + 0.5f;
				new_pos[2] = pos[2]/2.0f + 0.5f;
				break;
			case 2:
				new_pos[0] = pos[0]/2.0f;
				new_pos[1] = pos[1]/2.0f + 0.5f;
				new_pos[2] = pos[2]/2.0f;
				break;
			case 3:
				new_pos[0] = pos[0]/2.0f + 0.5f;
				new_pos[1] = pos[1]/2.0f;
				new_pos[2] = pos[2]/2.0f + 0.5f;
				break;
			case 4:
				new_pos[0] = pos[0]/2.0f + 0.5f;
				new_pos[1] = pos[1]/2.0f;
				new_pos[2] = pos[2]/2.0f;
				break;
			case 5:
				new_pos[0] = pos[0]/2.0f;
				new_pos[1] = pos[1]/2.0f + 0.5f;
				new_pos[2] = pos[2]/2.0f + 0.5f;
				break;
			case 6:
				new_pos[0] = pos[0]/2.0f + 0.5f;
				new_pos[1] = pos[1]/2.0f + 0.5f;
				new_pos[2] = pos[2]/2.0f;
				break;
			case 7:
				new_pos[0] = pos[0]/2.0f;
				new_pos[1] = pos[1]/2.0f;
				new_pos[2] = pos[2]/2.0f + 0.5f;
				break;
			default:
				assert(false);
			}
			if(solarSystemFindLocationOctTree(new_pos, size_test/2.0f, search_size, random_table, check_func, user_data))
				return true;
			oct_list[quad] = oct_list[i];
		}
		return false;
	}
	else
	{
		return check_func(pos, user_data);
	}
}

static bool solarSystemIsValidClusterLoc(Vec3 pos, SSClusterSearchInfo *search_info)
{
	int i;
	F32 dist;
	SSClusterLocation **locations1 = search_info->locations1;
	SSClusterLocation **locations2 = search_info->locations2;
	F32 to_place_size = search_info->to_place_size;
	SSCluster *cluster = search_info->cluster;
	Vec3* out_loc = &search_info->out_loc;
	bool connected = false;

	subVec3same(pos, 0.5, pos);
	pos[0] *= cluster->radius*2;
	pos[1] *= (cluster->height/2.0f - to_place_size)*2;
	pos[2] *= cluster->radius*2;
	dist = sqrt(SQR(pos[0])+SQR(pos[2]));
	dist += to_place_size;
	if(cluster->radius < dist)
		return false;
	for( i=0; i < eaSize(&locations1); i++ )
	{
		dist = distance3(locations1[i]->loc, pos);
		dist = dist - locations1[i]->size - to_place_size;
		if(dist < cluster->min_dist)
			return false;
		if(dist <= cluster->max_dist)
			connected = true;
	}
	for( i=0; i < eaSize(&locations2); i++ )
	{
		dist = distance3(locations2[i]->loc, pos);
		dist = dist - locations2[i]->size - to_place_size;
		if(dist < cluster->min_dist)
			return false;
		if(dist <= cluster->max_dist)
			connected = true;
	}
	copyVec3(pos, *out_loc);
	if(eaSize(&locations1) + eaSize(&locations2) == 0)
		return true;
	return connected;
}

static void solarSystemAddLibObj(SSLibObj *lib_obj, Vec3 pos, F32 scale, U32 seed, GenesisToPlaceObject *parent_object, GenesisMissionRequirements *mission_reqs, GenesisToPlaceState *to_place)
{
	GroupDef *ref_group = solarSystemGetGroupDefFromObject(lib_obj);
	if( !lib_obj->source_context ) {
		Errorf( "SSLibObj missing Genesis Context.  Please notify the Genesis team immediately." );
	}
	
	if(ref_group)
	{
		GenesisToPlaceObject *new_object;
		float fRot=0.0f;
		new_object = solarSystemPlaceGroupDef(ref_group, NULL, pos, fRot, parent_object, to_place);
		new_object->seed = seed;
		new_object->scale = scale;
		new_object->challenge_name = strdup(lib_obj->challenge_name);
		new_object->challenge_index = lib_obj->challenge_id;
		if (IS_HANDLE_ACTIVE(lib_obj->start_spawn_using_transition)) {
			new_object->params = genesisCreateStartSpawn( REF_STRING_FROM_HANDLE(lib_obj->start_spawn_using_transition ));
			if( !new_object->instanced ) {
				new_object->instanced = StructCreate( parse_GenesisInstancedObjectParams );
			}
		}
		new_object->source_context = lib_obj->source_context;

		solarSystemApplyChallengeParams( new_object,
										 genesisFindMissionChallengeInstanceParams( mission_reqs, new_object->challenge_name ),
										 genesisFindMissionChallengeInteractParams( mission_reqs, new_object->challenge_name ),
										 lib_obj->patrol_type );
	}
}

static void solarSystemCreateCluster(GroupDefLib *def_lib, GenesisToPlaceObject *child_object, GenesisMissionRequirements *mission_reqs,
									 SSCluster *cluster, F32 scale, U32 seed, bool place_excluders, GenesisToPlaceState *to_place,
									 SSClusterLocation ***existing_locations, bool add_to_existing_locations, char *room_name, const char *layout_name)
{
	int i;
	int total_objects = 0;
	SSClusterLocation **locations = NULL;
	SSClusterObject **objects;			//< not an EArray
	int *rand_idx_list;
	int loc_used_count;
	int object_type_idx;
	MersenneTable *random_table;
	random_table = mersenneTableCreate(seed);

	solarSystemValidateClusterSize(cluster);

	for( i=0; i < eaSize(&cluster->cluster_objects); i++ )
	{
		SSClusterObject *object = cluster->cluster_objects[i];
		if(cluster->cluster_objects[i]->lib_obj.patrol_type == GENESIS_SPACE_PATROL_None)
		{
			object->count = MAX(object->count, 1);
			total_objects += object->count;
		}
		else 
		{
			object->count = 0;
		}
	}
	if(total_objects <= 0)
		return;

	//Make an array of random indexes into the locations array
	objects = ScratchAlloc(sizeof(SSClusterObject*)*total_objects);
	rand_idx_list = ScratchAlloc(sizeof(int)*total_objects);
	for( i=0; i < total_objects; i++ )
		rand_idx_list[i] = i;
	object_type_idx=0;
	loc_used_count=0;
	for( i=total_objects-1; i >= 0 ; i-- )
	{
		int rand_idx = randomMersenneU32(random_table)%(i+1);
		SSClusterObject *object = cluster->cluster_objects[object_type_idx];
		while(object->count == 0)
		{
			object_type_idx++;
			object = cluster->cluster_objects[object_type_idx];
		}
		objects[rand_idx_list[rand_idx]] = object;
		rand_idx_list[rand_idx] = rand_idx_list[i];

		loc_used_count++;
		if(loc_used_count >= object->count)
		{
			object_type_idx++;
			loc_used_count=0;
		}
	}

	for( i=0; i < total_objects; i++ )
	{
		F32 object_size;
		SSClusterObject *object = objects[i];
		GroupDef *ref_group = solarSystemGetGroupDefFromObject(&object->lib_obj);

		if(ref_group)
		{
			SSClusterSearchInfo search_info = {0};
			GenesisToPlaceObject *new_object;
			Vec3 start = {0.5, 0.5, 0.5};

			object_size = ref_group->bounds.radius;

			if(	object_size*2 >= cluster->height ||
				object_size >= cluster->radius )
			{
				genesisRaiseError(GENESIS_ERROR, 
					genesisMakeTempErrorContextRoom(room_name, layout_name), 
					"Object: %s -- Room is too small to fit even one of these objects.", object->lib_obj.obj.name_str );
				eaDestroyEx(&locations, NULL);
				ScratchFree(objects);
				ScratchFree(rand_idx_list);
				mersenneTableFree(random_table);
				return;
			}

			search_info.cluster = cluster;
			search_info.locations1 = locations;
			search_info.locations2 = *existing_locations;
			search_info.to_place_size = object_size;
			search_info.to_place_obj = object;
			if(!solarSystemFindLocationOctTree(start, cluster->radius*2, 16.0f, random_table, solarSystemIsValidClusterLoc, &search_info))
			{
				genesisRaiseError(GENESIS_ERROR, 
					genesisMakeTempErrorContextRoom( room_name, layout_name ), 
					"Failed to place objects into a cluster.  Check your radius, height, min dist and max dist values." );
				eaDestroyEx(&locations, NULL);
				ScratchFree(objects);
				ScratchFree(rand_idx_list);
				mersenneTableFree(random_table);
				return;
			}

			{
				SSClusterLocation* new_loc = calloc( 1, sizeof( *new_loc ));
				copyVec3(search_info.out_loc, new_loc->loc);
				new_loc->size = object_size;
				eaPush(&locations, new_loc);
			}

			if(place_excluders && ref_group->bounds.radius > 0.0f && !ref_group->property_structs.debris_field_properties)
			{
				Vec3 debris_pos;
				float fRot=0.0f;
				GroupDef *excluder_group;
				excluder_group = solarSystemCreateDebrisFieldExcluder(def_lib, "Debris Excluder", ref_group->bounds.radius);
				addVec3(ref_group->bounds.min, ref_group->bounds.max, debris_pos);
				scaleVec3(debris_pos, 0.5f, debris_pos);
				addVec3(debris_pos, locations[i]->loc, debris_pos);
				solarSystemPlaceGroupDef(excluder_group, "Debris Excluder", debris_pos, fRot, child_object, to_place);
			}

			
			scaleVec3(locations[i]->loc, scale, locations[i]->loc);
			copyVec3(locations[i]->loc, object->lib_obj.offset.pos);
			{
				float fRot=0.0f;
				new_object = solarSystemPlaceGroupDef(ref_group, NULL, locations[i]->loc, fRot, child_object, to_place);
			}
			if(total_objects > 1)
				yawMat3(randomMersenneF32(random_table)*PI, new_object->mat);
			new_object->seed = seed+i+1;
			new_object->scale = scale;
			new_object->challenge_name = strdup(object->lib_obj.challenge_name);
			new_object->challenge_index = object->lib_obj.challenge_id;
			if (IS_HANDLE_ACTIVE(object->lib_obj.start_spawn_using_transition)) {
				new_object->params = genesisCreateStartSpawn(REF_STRING_FROM_HANDLE(object->lib_obj.start_spawn_using_transition));
				if( !new_object->instanced ) {
					new_object->instanced = StructCreate(parse_GenesisInstancedObjectParams);
				}
			}

			solarSystemApplyChallengeParams( new_object,
											 genesisFindMissionChallengeInstanceParams( mission_reqs, new_object->challenge_name ),
											 genesisFindMissionChallengeInteractParams( mission_reqs, new_object->challenge_name ),
											 object->lib_obj.patrol_type );
		}
	}

	if( add_to_existing_locations ) {
		eaPushEArray(existing_locations, &locations);
		eaDestroy(&locations);
	} else {
		eaDestroyEx(&locations, NULL);
	}
	ScratchFree(objects);
	ScratchFree(rand_idx_list);
	mersenneTableFree(random_table);
}

static void solarSystemAddLibObjects(GroupDefLib *def_lib, GenesisToPlaceObject *parent_object, GenesisMissionRequirements *mission_reqs, SSLibObj **group_refs, F32 scale, F32 angle, MersenneTable *random_table, bool place_excluders, GenesisToPlaceState *to_place, char* room_name)
{
	int i;
	GenesisToPlaceObject *new_object;
	for( i=0; i < eaSize(&group_refs); i++ )
	{
		GroupDef *ref_group = NULL;
		ref_group = solarSystemGetGroupDefFromObject(group_refs[i]);
		
		if(ref_group)
		{
			new_object = solarSystemPlaceGroupDef(ref_group, NULL, NULL, -angle, parent_object, to_place);
			new_object->seed = randomMersenneU32(random_table);
			new_object->scale = scale;
			new_object->challenge_name = strdup(group_refs[i]->challenge_name);
			new_object->challenge_index = group_refs[i]->challenge_id;
			if (IS_HANDLE_ACTIVE(group_refs[i]->start_spawn_using_transition)) {
				new_object->params = genesisCreateStartSpawn( REF_STRING_FROM_HANDLE(group_refs[i]->start_spawn_using_transition));
				new_object->instanced = StructCreate(parse_GenesisInstancedObjectParams);
			}
			new_object->source_context = group_refs[i]->source_context;

			solarSystemApplyChallengeParams( new_object,
											 genesisFindMissionChallengeInstanceParams( mission_reqs, new_object->challenge_name ),
											 genesisFindMissionChallengeInteractParams( mission_reqs, new_object->challenge_name ),
											 group_refs[i]->patrol_type );

			// Place Debris Field Excluders if needed
			if(place_excluders && !ref_group->property_structs.debris_field_properties && ref_group->bounds.radius > 0.0f)
			{
				Vec3 debris_pos;
				float fRot=0.0f;
				
				GroupDef *excluder_group;
				excluder_group = solarSystemCreateDebrisFieldExcluder(def_lib, "Debris Excluder", ref_group->bounds.radius);
				addVec3(ref_group->bounds.min, ref_group->bounds.max, debris_pos);
				scaleVec3(debris_pos, 0.5f, debris_pos);
				solarSystemPlaceGroupDef(excluder_group, "Debris Excluder", debris_pos, fRot, parent_object, to_place);
			}
		}
	}
}

static void solarSystemAddPathObjects(GenesisSolSysZoneMap *sol_sys, GroupDefLib *def_lib, GenesisToPlaceObject *parent_object, GenesisToPlaceObject *parent_curve, ShoeboxPointList *point_list, MersenneTable *random_table, GenesisToPlaceState *to_place)
{
	int i;
	GenesisToPlaceObject *new_object;
	int point_cnt = eaSize(&point_list->points);
	int unused_points_cnt = point_cnt;
	int *unused_points = ScratchAlloc(point_cnt * sizeof(int));

	for( i=0; i < point_cnt; i++ )
		unused_points[i] = i;

	for( i=0; i < eaSize(&point_list->curve_objects); i++ )
	{
		SSLibObj *curve_obj = point_list->curve_objects[i];
		GroupDef *ref_group = NULL;
		if(!curve_obj->offset.detached && !parent_curve)
			continue;

		ref_group = solarSystemGetGroupDefFromObject(curve_obj);
		if(ref_group)
		{
			F32 t = (randomMersenneF32(random_table)+1.0f)/2.0f;
			Vec3 offset;
			float fRot=0.0f;
			
			subVec3(curve_obj->offset.offset_max, curve_obj->offset.offset_min, offset);
			scaleVec3(offset, t, offset);
			addVec3(offset, curve_obj->offset.offset_min, offset);

			if(curve_obj->offset.detached)
			{
				if(unused_points_cnt > 0)
				{
					Mat3 rot_mat;
					Vec3 temp;
					int rand_idx, unused_idx;
					ShoeboxPoint *point;

					unused_idx = randomMersenneU32(random_table)%unused_points_cnt;
					rand_idx = unused_points[unused_idx];
					unused_points_cnt--;
					unused_points[unused_idx] = unused_points[unused_points_cnt];

					point = point_list->points[rand_idx];
					if(!point->pos_offset_right)
						offset[0] *= -1.0f;
					copyVec3(point->dir, rot_mat[2]);
					normalVec3(rot_mat[2]);
					copyVec3(upvec, rot_mat[1]);
					crossVec3(rot_mat[1], rot_mat[2], rot_mat[0]);
					normalVec3(rot_mat[0]);
					crossVec3(rot_mat[2], rot_mat[0], rot_mat[1]);
					normalVec3(rot_mat[1]);
					copyVec3(offset, temp);
					mulVecMat3(temp, rot_mat, offset);
					addVec3(offset, point->pos, offset);
				}
				else
				{
					genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextLayout(sol_sys->layout_name), "Too many detached objects placed on the curve");
				}

				if(ref_group->bounds.radius && !ref_group->property_structs.debris_field_properties)
				{
					Vec3 debris_pos;
					GroupDef *excluder_group;
					excluder_group = solarSystemCreateDebrisFieldExcluder(def_lib, "Debris Excluder", ref_group->bounds.radius);
					addVec3(ref_group->bounds.min, ref_group->bounds.max, debris_pos);
					scaleVec3(debris_pos, 0.5f, debris_pos);
					addVec3(debris_pos, offset, debris_pos);
					solarSystemPlaceGroupDef(excluder_group, "Debris Excluder", debris_pos, fRot, parent_object, to_place);
				}
			}

			new_object = solarSystemPlaceGroupDef(ref_group, NULL, offset, fRot, curve_obj->offset.detached ? parent_object : parent_curve, to_place);
			new_object->seed = randomMersenneU32(random_table);
			new_object->challenge_name = strdup(curve_obj->challenge_name);
			new_object->challenge_index = curve_obj->challenge_id;
		}
	}

	ScratchFree(unused_points);
}

static void solarSystemAddObjSet( GenesisSolSysZoneMap *sol_sys, GroupDefLib *def_lib, GenesisToPlaceObject *parent_object, GenesisToPlaceState *to_place, 
								  GenesisMissionRequirements *mission_reqs, SSObjSet *orbit_rep, F32 angle, F32 scale,
								  U32 seed, bool place_excluders, SSClusterLocation ***existing_locations, bool add_to_existing_locations, char *room_name)
{
	MersenneTable *random_table;

	if(!orbit_rep)
		return;

	random_table = mersenneTableCreate(seed);

	// Place Objects
	solarSystemAddLibObjects(def_lib, parent_object, mission_reqs, orbit_rep->group_refs, scale, angle, random_table, place_excluders, to_place, room_name);

	// Place Clusters
	if (orbit_rep->cluster)
		solarSystemCreateCluster(def_lib, parent_object, mission_reqs, orbit_rep->cluster, scale, randomMersenneU32(random_table), place_excluders, to_place, existing_locations, add_to_existing_locations, room_name, sol_sys->layout_name);

	mersenneTableFree(random_table);
}

//dir is expected to be in the x/z plane and normalized
static void solarSystemAddZigZagPath(ShoeboxPointList *point_list, Vec3 last_pos, Vec3 dir, Vec3 min_pos, Vec3 max_pos, bool first, MersenneTable *random_table)
{
	int j;
	Vec3 offset;
	Vec3 up_vec = {0,1,0};
	bool zig = ((randomMersenneInt(random_table) > 0) ? true : false);
	Vec3 normal;
	F32 temp;
	F32 prev_point_rad;
	F32 min_yaw = RAD(point_list->min_yaw);
	F32 max_yaw = RAD(point_list->max_yaw);
	F32 min_pitch = RAD(point_list->min_tilt);
	F32 max_pitch = RAD(point_list->max_tilt);
	F32 sint, cost;
	F32 theta;

	//Yaw the dir
	if(first && max_yaw == 0.0f && min_yaw == 0.0f)
	{
		min_yaw = -HALFPI;
		max_yaw =  HALFPI;
	}
	theta = (max_yaw - min_yaw) * (randomMersenneF32(random_table)+1.0f)/2.0f;
	theta += min_yaw;
	sincosf(theta, &sint, &cost);
	temp = dir[0]*cost - dir[2]*sint;
	dir[2] = dir[2]*cost + dir[0]*sint;
	dir[0] = temp;

	//Pitch of the dir
	theta = (max_pitch - min_pitch) * (randomMersenneF32(random_table)+1.0f)/2.0f;
	theta += min_pitch;
	dir[1] = atan(theta);
	normalVec3(dir);

	//Find up vec
	crossVec3(up_vec, dir, normal);
	normalVec3(normal);
	crossVec3(dir, normal, up_vec);
	normalVec3(up_vec);

	normal[0] = dir[2];
	normal[1] = 0;
	normal[2] = -dir[0];
	normalVec3(normal);
	point_list->spline = StructCreate(parse_Spline);
	splineAppendCP(point_list->spline, last_pos, up_vec, dir, 0, 0);

	prev_point_rad = 0;
	for( j=0; j < eaSize(&point_list->points) ; j++ )
	{
		F32 dist;
		ShoeboxPoint *point = point_list->points[j];
		dist = point->dist_from_last ? point->dist_from_last : SOLAR_SYSTEM_ENCOUNTER_DIST;
		dist += prev_point_rad;
		prev_point_rad = solarSystemGetPointRadius(point->point_rep, point->missions);
		if(j!=0)
			dist += prev_point_rad;

		scaleVec3(dir, dist, offset);
		addVec3(last_pos, offset, last_pos);

		//Distance from main path
		dist = (point_list->max_horiz - point_list->min_horiz) * (randomMersenneF32(random_table)+1.0f)/2.0f;
		dist += point_list->min_horiz;
		if(!zig)
			dist *= -1.0f;//then zag
		scaleVec3(normal, dist, offset);
		addVec3(last_pos, offset, point->pos);

		//Add Vertical Difference
		dist = (point_list->max_vert - point_list->min_vert) * (randomMersenneF32(random_table)+1.0f)/2.0f;
		dist += point_list->min_vert;
		point->pos[1] += dist;

		copyVec3(dir, point->dir);
		point->dir[1] = 0.0f;
		normalVec3(point->dir);
		point->pos_offset_right = true;
		MINVEC3(point->pos, min_pos, min_pos);
		MAXVEC3(point->pos, max_pos, max_pos);
		zig = !zig;

		if(point_list->follow_points)
			splineAppendCP(point_list->spline, point->pos, up_vec, dir, 0, 0);
		else
			splineAppendCP(point_list->spline, last_pos, up_vec, dir, 0, 0);
	}

	if(point_list->follow_points)
		splineRedrawPoints(point_list->spline);

	dir[1] = 0.0f;
	normalVec3(dir);
}

static void solarSystemAddOrbitPath(ShoeboxPointList *point_list, Vec3 last_pos, Vec3 dir, Vec3 min_pos, Vec3 max_pos, bool first, MersenneTable *random_table)
{
	int i;
	Mat3 rot_mat;
	Mat4 circ_mat;
	F32 radius;
	F32 theta, alpha;
	F32 min_tilt = RAD(point_list->min_tilt);
	F32 max_tilt = RAD(point_list->max_tilt);
	F32 min_yaw = RAD(point_list->min_yaw);
	F32 max_yaw = RAD(point_list->max_yaw);
	Vec3 offset;
	Vec3 Q, N;
	Vec3 tQ, tN;
	F32 circumference;
	F32 equidist_dist;
	bool clockwise;
	F32 angle;
	F32 orbit_object_size;
	F32 prev_point_rad;
	F32 sint, cost;

	//Yaw the dir
	if(first && max_yaw == 0.0f && min_yaw == 0.0f)
	{
		min_yaw = -HALFPI;
		max_yaw =  HALFPI;
	}
	theta = (max_yaw - min_yaw) * (randomMersenneF32(random_table)+1.0f)/2.0f;
	theta += min_yaw;
	clockwise = ((randomMersenneInt(random_table) > 0) ? true : false);
	if(theta < 0)
		clockwise = true;
	else if(theta > 0)
		clockwise = false;
	sincosf(theta, &sint, &cost);
	theta = dir[0]*cost - dir[2]*sint;
	dir[2] = dir[2]*cost + dir[0]*sint;
	dir[0] = theta;

	orbit_object_size = solarSystemGetObjSetRadius(point_list->orbit_object);

	//Find tilt
	theta = (max_tilt - min_tilt) * (randomMersenneF32(random_table)+1.0f)/2.0f;
	theta += min_tilt;
	//Find radius
	radius = (point_list->max_rad - point_list->min_rad) * (randomMersenneF32(random_table)+1.0f)/2.0f;
	radius += point_list->min_rad;
	radius += orbit_object_size;
	radius = MAX(radius, 0.01f);
	point_list->orbit_radius = radius;
	//Find angle position on circle
	alpha = PI*((randomMersenneU32(random_table)%8)-4)/8.0f;
	//Find vector from our point on the circle to 0,0,0, and the normal of the circle
	setVec3(Q, -radius*cos(alpha), -radius*sin(alpha)*sin(theta), -radius*sin(alpha)*cos(theta));
	setVec3(N, 0, cos(theta), -sin(theta));
	//Find rotation mat so that Q will travel down the z and y axis only
	setVec3(rot_mat[2], Q[0], 0, Q[2]);
	normalVec3(rot_mat[2]);
	setVec3(rot_mat[1], 0, 1, 0);
	crossVec3(rot_mat[1], rot_mat[2], rot_mat[0]);
	normalVec3(rot_mat[0]);
	transposeMat3(rot_mat);
	//Rotate
	mulVecMat3(Q, rot_mat, tQ);
	mulVecMat3(N, rot_mat, tN);
	//Find rotation mat so that Q will be to the right of our dir vec
	copyVec3(dir, rot_mat[2]);
	normalVec3(rot_mat[2]);
	crossVec3(rot_mat[1], rot_mat[2], rot_mat[0]);
	normalVec3(rot_mat[0]);
	//Rotate
	mulVecMat3(tQ, rot_mat, circ_mat[0]);
	mulVecMat3(tN, rot_mat, circ_mat[1]);
	//Mat matrix for the circle
	normalVec3(circ_mat[0]);
	normalVec3(circ_mat[1]);
	crossVec3(circ_mat[0], circ_mat[1], circ_mat[2]);
	normalVec3(circ_mat[2]);
	//Find center of circle
	scaleVec3(circ_mat[0], radius, offset);
	addVec3(last_pos, offset, circ_mat[3]);
	copyMat4(circ_mat, point_list->orbit_mat);

	point_list->spline = StructCreate(parse_Spline);
	for( i=0; i <= 16; i++ )
	{
		Vec3 point_start, point_end;
		Vec3 point_dir_start, point_dir_end;
		setVec3(point_start, radius*cos(i*PI/8.0f), 0, radius*sin(i*PI/8.0f));
		setVec3(point_dir_start, -sin(i*PI/8.0f), 0, cos(i*PI/8.0f));
		mulVecMat4(point_start, circ_mat, point_end);
		mulVecMat3(point_dir_start, circ_mat, point_dir_end);
		splineAppendCP(point_list->spline, point_end, circ_mat[1], point_dir_end, 0, 0);
	}

	circumference = TWOPI*radius;
	if(point_list->equidist && eaSize(&point_list->points) > 0)
		equidist_dist = circumference / eaSize(&point_list->points);

	angle = PI;
	prev_point_rad = 0;
	for( i=0; i < eaSize(&point_list->points) ; i++ )
	{
		ShoeboxPoint *point = point_list->points[i];
		if(circumference > 0)
		{
			Vec3 point_start, point_end;
			Vec3 point_dir_start, point_dir_end;
			F32 dist_from_last;
			if(point_list->equidist)
			{
				dist_from_last = (i==0) ? 0 : equidist_dist;
			}
			else
			{
				dist_from_last = point->dist_from_last ? point->dist_from_last : SOLAR_SYSTEM_ENCOUNTER_DIST;
				dist_from_last += prev_point_rad;
				prev_point_rad = solarSystemGetPointRadius(point->point_rep, point->missions);
				if(i!=0)
					dist_from_last += prev_point_rad;
			}
			angle += TWOPI * (dist_from_last / circumference) * (clockwise ? -1.0f : 1.0f);

			setVec3(point_start, radius*cos(angle), 0, radius*sin(angle));
			setVec3(point_dir_start, -sin(angle), 0, cos(angle));
			mulVecMat4(point_start, circ_mat, point_end);
			mulVecMat3(point_dir_start, circ_mat, point_dir_end);

			copyVec3(point_end, point->pos);
			copyVec3(point_end, last_pos);
			scaleVec3(point_dir_end, (clockwise ? -1.0f : 1.0f), point->dir);
			point->dir[1] = 0.0f;
			normalVec3(point->dir);
			point->pos_offset_right = !clockwise;
			copyVec3(point->dir, dir);
			MINVEC3(point->pos, min_pos, min_pos);
			MAXVEC3(point->pos, max_pos, max_pos);
		}
	}

	copyVec3(dir, Q);
	dir[0] = Q[2] * (clockwise ? -1.0f :  1.0f);
	dir[1] = 0;
	dir[2] = Q[0] * (clockwise ?  1.0f : -1.0f);
	normalVec3(dir);
}

static ShoeboxPoint* solarSystemFindPointInListByName(ShoeboxPointList *point_list, char *name)
{
	int i;
	for( i=0; i < eaSize(&point_list->points); i++ )
	{
		ShoeboxPoint *point = point_list->points[i];
		if(stricmp(point->name, name)==0)
			return point;
	}
	return NULL;
}

static ShoeboxPoint* solarSystemFindPointInShoeboxByName(GenesisShoebox *shoebox, char *name)
{
	int i;
	for( i=0; i < eaSize(&shoebox->point_lists); i++ )
	{
		ShoeboxPoint *point = solarSystemFindPointInListByName(shoebox->point_lists[i], name);
		if(point)
			return point;
	}
	return NULL;
}

static bool solarSystemIsValidDetailLoc(Vec3 pos, GenesisShoebox *shoebox)
{
	int i, j;
	SSLibObj *place_object = shoebox->detail_objects[shoebox->detail_placed];
	GroupDef *ref_group = place_object->cached_def;
	Vec3 abs_pos;
	F32 dist;
	bool close_enough = false;

	pos[0] = interpF32(pos[0], shoebox->search_min[0], shoebox->search_max[0]);
	pos[1] = interpF32(pos[1], shoebox->search_min[1], shoebox->search_max[1]);
	pos[2] = interpF32(pos[2], shoebox->search_min[2], shoebox->search_max[2]);

	for( i=0; i < eaSize(&shoebox->point_lists); i++ )
	{
		ShoeboxPointList *list = shoebox->point_lists[i];
		if(list->orbit_object)
		{
			F32 obrbit_rad=0;
			addVec3(list->orbit_mat[3], shoebox->layer_center, abs_pos);

			dist = distance3(abs_pos, pos) - ref_group->bounds.radius;

			for( j=0; j < eaSize(&list->orbit_object->group_refs); j++ )
			{
				SSLibObj* group_ref = list->orbit_object->group_refs[j];
				GroupDef *orbit_ref = group_ref->cached_def;
				F32 orbit_ref_radius;
				F32 sx, sz;

				if(!orbit_ref)
				{
					orbit_ref = solarSystemGetGroupDefFromObject(group_ref);
					group_ref->cached_def = orbit_ref;
				}
				if(!orbit_ref)
				{
					genesisRaiseError(GENESIS_ERROR, group_ref->source_context, "Could not find orbit rep object %s.", group_ref->obj.name_str);
					return true;
				}
				//We do not care about vertical size
				sx = orbit_ref->bounds.max[0] - orbit_ref->bounds.min[0];
				sz = orbit_ref->bounds.max[2] - orbit_ref->bounds.min[2];
				orbit_ref_radius = sqrt(SQR(sx)+SQR(sz))/2.0f;
				obrbit_rad = MAX(obrbit_rad, orbit_ref_radius);
			}

			if(dist < obrbit_rad)
				return false;
		}
		for( j=0; j < eaSize(&list->points); j++ )
		{
			addVec3(list->points[j]->pos, shoebox->layer_center, abs_pos);
			dist = distance3(abs_pos, pos) - ref_group->bounds.radius - solarSystemGetPointRadius(list->points[j]->point_rep, list->points[j]->missions);
			if(dist < place_object->offset.min_dist)
				return false;
			if(dist < place_object->offset.max_dist)
				close_enough = true;
		}
	}
	if(!close_enough)
		return false;

	for( i=0; i < shoebox->detail_placed; i++ )
	{
		SSLibObj *prev_object = shoebox->detail_objects[i];
		dist = distance3(prev_object->offset.pos, pos) - ref_group->bounds.radius - prev_object->offset.radius;
		if(dist < place_object->offset.min_dist)
			return false;
	}
	subVec3(pos, shoebox->layer_center, place_object->offset.pos);
	place_object->offset.radius = ref_group->bounds.radius;
	return true;
}

static bool solarSystemClusterHasPatrols(SSCluster *cluster)
{
	int i;
	if(!cluster)
		return false;
	for( i=0; i < eaSize(&cluster->cluster_objects); i++ )
	{
		SSLibObj *obj = &cluster->cluster_objects[i]->lib_obj;
		if(obj->patrol_type != GENESIS_SPACE_PATROL_None)
			return true;
	}
	return false;
}


static void solarSystemClusterMakeOrbitPatrol(GroupDefLib *def_lib, SSLibObj *obj, SSCluster *cluster, ShoeboxPoint *parent_point, ShoeboxPointList *point_list, GenesisToPlaceObject *parent_obj, GenesisToPlaceState *to_place, GenesisMissionRequirements *mission_reqs, MersenneTable *random_table)
{
	int i;
	bool found = false;
	if(obj->patrol_ref_name)
	{
		//Find challenge item
		for( i=0; i < eaSize(&cluster->cluster_objects); i++ )
		{
			SSLibObj *orbit_obj = &cluster->cluster_objects[i]->lib_obj;
			if(utils_stricmp(orbit_obj->challenge_name, obj->patrol_ref_name) == 0)
			{
				Vec3 location;
				F32 radius = solarSystemGetLibObjRadius(obj);
				F32 orbit_radius = solarSystemGetLibObjRadius(orbit_obj);
				F32 circ_radius = radius+orbit_radius;
				if(circ_radius && radius)
				{
					F32 theta;
					F32 theta_inc = radius/circ_radius;//Go arc length equal to the radius of the obj each time
					F32 alpha_offset = randomMersenneF32(random_table)*PI;
					F32 rise_amount = randomMersenneF32(random_table)*circ_radius*0.5;
					theta_inc = MAX(theta_inc, 0.0000001);
					copyVec3(orbit_obj->offset.pos, location);
					location[0] += circ_radius;
					solarSystemAddLibObj(obj, location, 1.0f, randomMersenneU32(random_table), parent_obj, mission_reqs, to_place);
					{
						Vec3 room_pos;
						char patrol_name[256];
						GenesisToPlacePatrol *patrol = calloc(1, sizeof(*patrol));
						sprintf(patrol_name, "%s_%02d_Patrol", obj->challenge_name, obj->challenge_id);
						patrol->patrol_name = StructAllocString(patrol_name);
						patrol->patrol_properties.route_type = PATROL_CIRCLE;
						genesisObjectGetAbsolutePos( parent_obj, room_pos );
						for( theta=0; theta < TWOPI; theta += theta_inc )
						{
							WorldPatrolPointProperties *patrol_point = StructCreate(parse_WorldPatrolPointProperties);
							patrol_point->pos[0] = circ_radius*cos(theta);
							patrol_point->pos[1] = rise_amount*sin(theta+alpha_offset);
							patrol_point->pos[2] = circ_radius*sin(theta);
							addVec3(orbit_obj->offset.pos, patrol_point->pos, patrol_point->pos);
							{
								float fRot=0.0f;
								GroupDef *excluder_group = solarSystemCreateDebrisFieldExcluder(def_lib, "Debris Excluder", radius);
								solarSystemPlaceGroupDef(excluder_group, "Debris Excluder", patrol_point->pos, fRot, parent_obj, to_place);
							}
							
							addVec3(room_pos, patrol_point->pos, patrol_point->pos);
							eaPush(&patrol->patrol_properties.patrol_points, patrol_point);
						}
						eaPush(&to_place->patrols, patrol);
					}
				}
				else 
					genesisRaiseError(GENESIS_ERROR, obj->source_context, "Patrol radius must be greater than zero, but it is %f.", radius);
				found = true;
				break;
			}
		}
		if(!found)
			genesisRaiseError(GENESIS_ERROR, obj->source_context, "Patrol references challenge %s, but it is not in the same room.", obj->patrol_ref_name);
	}
	else 
		genesisRaiseError(GENESIS_ERROR, obj->source_context, "Patrol is of type orbit, but does not have the Pat. Challenge set.");
}

static void solarSystemClusterMakePathPatrol(GroupDefLib *def_lib, SSLibObj *obj, SSCluster *cluster, ShoeboxPoint *start_point, ShoeboxPointList *point_list, GenesisToPlaceObject *parent_obj, GenesisToPlaceState *to_place, GenesisMissionRequirements *mission_reqs, MersenneTable *random_table)
{
	ShoeboxPoint *end_point = NULL;
	
	if(!obj->patrol_ref_name) {
		genesisRaiseError(GENESIS_ERROR, obj->source_context, "Patrol is of type path, but does not have the Pat. Room set.");
		return;
	}
	
	end_point = solarSystemFindPointInListByName(point_list, obj->patrol_ref_name);
	if(!end_point) {
		genesisRaiseError(GENESIS_ERROR, obj->source_context, "Patrol destination is room %s, which is not in the same room list.", obj->patrol_ref_name);
		return;
	}
	if(start_point == end_point) {
		genesisRaiseError(GENESIS_ERROR, obj->source_context, "Patrol destination is room %s, the same room it is placed in.", obj->patrol_ref_name);
		return;
	}
		
	{
		F32 radius = solarSystemGetLibObjRadius(obj);
		F32 start_r = solarSystemGetPointRadius(start_point->point_rep, start_point->missions);
		F32 end_r = solarSystemGetPointRadius(end_point->point_rep, end_point->missions);
		F32 full_dist;
		Vec3 start_absolute_pos = {0,0,0}, start_relative_pos = {0,0,0}, end_relative_pos = {0,0,0};
		Vec3 dir;
		subVec3(end_point->pos, start_point->pos, dir);
		full_dist = normalVec3(dir) - start_r - end_r;
		scaleVec3(dir, start_r, start_relative_pos);
		
		scaleVec3(dir, end_r, end_relative_pos);
		subVec3(end_point->pos, end_relative_pos, end_relative_pos);
		subVec3(end_relative_pos, start_point->pos, end_relative_pos);
		
		genesisObjectGetAbsolutePos( parent_obj, start_absolute_pos );

		solarSystemAddLibObj(obj, start_relative_pos, 1.0f, randomMersenneU32(random_table), parent_obj, mission_reqs, to_place);

		if(radius <= 0) {
			genesisRaiseError(GENESIS_ERROR, obj->source_context, "Patrol raidus must be greater than zero, but it is %f.", radius);
			return;
		}
		else
		{
			F32 dist;
			char patrol_name[256];
			GenesisToPlacePatrol *patrol = calloc(1, sizeof(*patrol));
			sprintf(patrol_name, "%s_%02d_Patrol", obj->challenge_name, obj->challenge_id);
			patrol->patrol_name = StructAllocString(patrol_name);
			if( obj->patrol_type == GENESIS_SPACE_PATROL_Path_OneWay ) {
				patrol->patrol_properties.route_type = PATROL_ONEWAY;
			} else {
				patrol->patrol_properties.route_type = PATROL_PINGPONG;
			}
			{
				WorldPatrolPointProperties *patrol_point = StructCreate(parse_WorldPatrolPointProperties);
				addVec3(start_relative_pos, start_absolute_pos, patrol_point->pos);
				eaPush(&patrol->patrol_properties.patrol_points, patrol_point);
			}
			{
				WorldPatrolPointProperties *patrol_point = StructCreate(parse_WorldPatrolPointProperties);
				addVec3(end_relative_pos, start_absolute_pos, patrol_point->pos);
				eaPush(&patrol->patrol_properties.patrol_points, patrol_point);
			}
			for( dist=0; dist < full_dist; dist += radius)
			{
				Vec3 debris_pos;
				float fRot=0.0f;
				GroupDef *excluder_group;
				scaleVec3(dir, dist + start_r, debris_pos);
				excluder_group = solarSystemCreateDebrisFieldExcluder(def_lib, "Debris Excluder", radius);
				solarSystemPlaceGroupDef(excluder_group, "Debris Excluder", debris_pos, fRot, parent_obj, to_place);
			}
			eaPush(&to_place->patrols, patrol);
		}
	}
}

static void solarSystemClusterMakePerimeterPatrol(GroupDefLib *def_lib, SSLibObj *obj, SSCluster *cluster, ShoeboxPoint *parent_point, ShoeboxPointList *point_list, GenesisToPlaceObject *parent_obj, GenesisToPlaceState *to_place, GenesisMissionRequirements *mission_reqs, MersenneTable *random_table)
{
	F32 radius = solarSystemGetLibObjRadius(obj);
	F32 orbit_radius = solarSystemGetPointRadius(parent_point->point_rep, parent_point->missions);
	F32 circ_radius = radius+orbit_radius;

	if(circ_radius && radius)
	{
		F32 theta;
		F32 theta_inc = radius/circ_radius;//Go arc length equal to the radius of the obj each time
		F32 alpha_offset = randomMersenneF32(random_table)*PI;
		F32 rise_amount = randomMersenneF32(random_table)*circ_radius*0.5;
		theta_inc = MAX(theta_inc, 0.0000001);
		{
			Vec3 location = { circ_radius, rise_amount*sin(alpha_offset), 0 };
			solarSystemAddLibObj(obj, location, 1.0f, randomMersenneU32(random_table), parent_obj, mission_reqs, to_place);
		}
		{
			Vec3 room_pos;
			char patrol_name[256];
			GenesisToPlacePatrol *patrol = calloc(1, sizeof(*patrol));
			sprintf(patrol_name, "%s_%02d_Patrol", obj->challenge_name, obj->challenge_id);
			patrol->patrol_name = StructAllocString(patrol_name);
			patrol->patrol_properties.route_type = PATROL_CIRCLE;
			genesisObjectGetAbsolutePos( parent_obj, room_pos );
			for( theta=0; theta < TWOPI; theta += theta_inc )
			{
				WorldPatrolPointProperties *patrol_point = StructCreate(parse_WorldPatrolPointProperties);
				patrol_point->pos[0] = circ_radius*cos(theta);
				patrol_point->pos[1] = rise_amount*sin(theta+alpha_offset);
				patrol_point->pos[2] = circ_radius*sin(theta);
				{
					float fRot=0.0f;
					// Since the excluder is relative, it should be done BEFORE adding room_pos
					GroupDef *excluder_group = solarSystemCreateDebrisFieldExcluder(def_lib, "Debris Excluder", radius);
					solarSystemPlaceGroupDef(excluder_group, "Debris Excluder", patrol_point->pos, fRot, parent_obj, to_place);
				}
				addVec3(room_pos, patrol_point->pos, patrol_point->pos);
				eaPush(&patrol->patrol_properties.patrol_points, patrol_point);
			}
			eaPush(&to_place->patrols, patrol);
		}
	}
	else 
		genesisRaiseError(GENESIS_ERROR, obj->source_context, "Patrol radius must be greater than zero, but it is %f.", radius);
}

static void solarSystemClusterMakePatrols(GroupDefLib *def_lib, SSCluster *cluster, ShoeboxPoint *start_point, ShoeboxPointList *point_list, GenesisToPlaceObject *parent_obj, GenesisToPlaceState *to_place, GenesisMissionRequirements *mission_reqs, MersenneTable *random_table)
{
	int i;
	if(!cluster)
		return;
	for( i=0; i < eaSize(&cluster->cluster_objects); i++ )
	{
		SSLibObj *obj = &cluster->cluster_objects[i]->lib_obj;
		if(obj->patrol_type == GENESIS_SPACE_PATROL_Path || obj->patrol_type == GENESIS_SPACE_PATROL_Path_OneWay)
			solarSystemClusterMakePathPatrol(def_lib, obj, cluster, start_point, point_list, parent_obj, to_place, mission_reqs, random_table);
		else if(obj->patrol_type == GENESIS_SPACE_PATROL_Orbit)
			solarSystemClusterMakeOrbitPatrol(def_lib, obj, cluster, start_point, point_list, parent_obj, to_place, mission_reqs, random_table);
		else if(obj->patrol_type == GENESIS_SPACE_PATROL_Perimeter)
			solarSystemClusterMakePerimeterPatrol(def_lib, obj, cluster, start_point, point_list, parent_obj, to_place, mission_reqs, random_table);
	}
}

static void solarSystemPopulateShoebox(GenesisSolSysZoneMap *sol_sys, ZoneMapLayer *layer, GenesisMissionRequirements **mission_reqs, GenesisToPlaceState *to_place, U32 seed_in, U32 version)
{
	GenesisShoebox *shoebox = &sol_sys->shoebox;
	char *solsys_name = sol_sys->layout_name;	
	int i,j,k,l;
	Vec3 offset;
	Vec3 last_pos, dir;
	Vec3 min_pos, max_pos, size;
	F32 layer_width = shoebox->layer_max[0]-shoebox->layer_min[0];
	F32 layer_height = shoebox->layer_max[1]-shoebox->layer_min[1];
	F32 layer_depth = shoebox->layer_max[2]-shoebox->layer_min[2];
	MersenneTable *random_table;
	GenesisToPlaceObject *system_object, *parent_curve_object, *curve_object, *detached_objects, *detail_object;
	GroupDef *system_root_def;
	char obj_name[256];
	float fZeroRot=0.0f;
	Vec3 inv_vec;

	setVec3same(last_pos, 0);
	setVec3same(min_pos, 500000);
	setVec3same(max_pos, -500000);

	random_table = mersenneTableCreate(seed_in);

	//Init the direction of game play as going away from the sun.
	if(nearSameVec3(zerovec3, shoebox->overview_pos))
		setVec3(dir, 0.0f, 0.0f, 1.0f);
	else
		copyVec3(shoebox->overview_pos, dir);
	dir[1] = 0.0f;
	normalVec3(dir);

	//Find Locations
	for( i=0; i < eaSize(&shoebox->point_lists); i++ )
	{
		ShoeboxPointList *point_list = shoebox->point_lists[i];
		Vec3 normal;

		normal[0] = dir[2];
		normal[1] = 0;
		normal[2] = -dir[0];

		if(point_list->list_type == SBLT_ZigZag)
			solarSystemAddZigZagPath(point_list, last_pos, dir, min_pos, max_pos, i==0, random_table);
		else if(point_list->list_type == SBLT_Orbit)
			solarSystemAddOrbitPath(point_list, last_pos, dir, min_pos, max_pos, i==0, random_table);
	}

	//Create parent def for all objects
	system_root_def = solarSystemMakeGroupDef(solarsystem_lib, "SystemRoot");
	system_root_def->property_structs.physical_properties.bIsDebrisFieldCont = 1;
	system_object = solarSystemPlaceGroupDef(system_root_def, "SystemRoot", shoebox->layer_center, fZeroRot, NULL, to_place);
	//Create parent def for all curves
	scaleVec3(shoebox->layer_center, -1.0f, inv_vec);
	parent_curve_object = solarSystemPlaceGroupDef(NULL, "Curves", inv_vec, fZeroRot, system_object, to_place);
	//Create parent def for detail objects place near curves
	detached_objects = solarSystemPlaceGroupDef(NULL, "Detached Objects", zerovec3, fZeroRot, system_object, to_place);
	//Create parent def for general detail objects
	detail_object = solarSystemPlaceGroupDef(NULL, "Detail Objects", zerovec3, fZeroRot, system_object, to_place);

	//Ensure that the game play did not go outside the bounds
	addVec3(min_pos, max_pos, offset);
	scaleVec3(offset, 0.5f, offset);
	subVec3(max_pos, min_pos, size);
	if(size[0] >= layer_width || size[2] >= layer_depth)
		genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextLayout(sol_sys->layout_name), "Play area is to long/wide to fit into a shoebox.");
	if(size[1] >= layer_height)
		genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextLayout(sol_sys->layout_name), "Play area is to tall to fit into a shoebox.");

	//Offset and place the objects and curves
	for( i=0; i < eaSize(&shoebox->point_lists); i++ )
	{
		ShoeboxPointList *point_list = shoebox->point_lists[i];
		GroupDef *curve_def;

		// Find positions for each room
		for( j=0; j < eaSize(&point_list->points); j++ )
		{
			ShoeboxPoint *shoebox_point = point_list->points[j];

			//Offset the position, we save this for later use
			subVec3(shoebox_point->pos, offset, shoebox_point->pos);
		}

		//Add all the room and mission objects
		for( j=0; j < eaSize(&point_list->points); j++ )
		{
			GenesisToPlaceObject *room_object;
			GenesisToPlaceObject *room_missions_object;
			GenesisToPlaceObject *room_shared_object;
			ShoeboxPoint *shoebox_point = point_list->points[j];
			SSClusterLocation **locations = NULL;
			U32 point_seed = randomMersenneU32(random_table);
			F32 angle;

			//Find the angle of the object
			switch(shoebox_point->face_dir)
			{
			case PLFD_Parent:
				angle = atan2(shoebox_point->dir[2], shoebox_point->dir[0])-HALFPI;
				break;
			case PLFD_Random:
			default:
				angle = randomMersenneF32(random_table)*PI;
			}
			angle += RAD(shoebox_point->face_offset);

			// Add the root objects
			room_object = solarSystemPlaceGroupDef(NULL, shoebox_point->name, shoebox_point->pos, fZeroRot, system_object, to_place);

			room_missions_object = solarSystemPlaceGroupDef(NULL, "Missions", zerovec3, fZeroRot, room_object, to_place);
			if (eaSize(&mission_reqs) > 1)
			{
				room_missions_object->params = genesisCreateMultiMissionWrapperParams();
			}

			// no_sharing is intentionally ignored for solar systems -- there's so little to share			
			room_shared_object = solarSystemPlaceGroupDef(NULL, "Shared", zerovec3, fZeroRot, room_object, to_place);
			
			//Add non-mission objects
			solarSystemAddObjSet(sol_sys, solarsystem_lib, room_shared_object, to_place, NULL, shoebox_point->point_rep, angle, 1.0f, point_seed, true, &locations, true, shoebox_point->name);
			for (k = 0; k < eaSize(&shoebox_point->missions); k++)
			{
				if (!shoebox_point->missions[k]->mission_name)
				{
					solarSystemAddObjSet(sol_sys, solarsystem_lib, room_shared_object, to_place, NULL,
										 shoebox_point->missions[k], angle, 1.0f, point_seed+k+1, true,
										 &locations, true, shoebox_point->name);
				}
			}
			
			//Add mission objects
			for (k = 0; k < eaSize(&mission_reqs); k++)
			{
				GenesisToPlaceObject *room_mission_object = solarSystemPlaceGroupDef(
						NULL, genesisMissionRoomVolumeName(sol_sys->layout_name, shoebox_point->name, mission_reqs[k]->missionName),
						zerovec3, fZeroRot, room_missions_object, to_place);
				GenesisProceduralObjectParams* params = genesisFindMissionRoomProceduralParams(mission_reqs[k], sol_sys->layout_name, shoebox_point->name);

				room_mission_object->challenge_name = strdup(room_mission_object->object_name);
				room_mission_object->challenge_is_unique = true;
					
				for (l = 0; l < eaSize(&shoebox_point->missions); l++)
				{
					if (  version == 0 ||
						  (shoebox_point->missions[l]->mission_name && 
						   !strcmp(shoebox_point->missions[l]->mission_name, mission_reqs[k]->missionName)))
					{
						solarSystemAddObjSet(sol_sys, solarsystem_lib, room_mission_object, to_place, mission_reqs[k],
											 shoebox_point->missions[l], angle, 1.0f, point_seed+k+1, true, &locations, false, shoebox_point->name);
						
						//Add the Patrols
						if(solarSystemClusterHasPatrols(shoebox_point->missions[l]->cluster))
						{
							solarSystemClusterMakePatrols(solarsystem_lib, shoebox_point->missions[l]->cluster, shoebox_point, point_list, room_mission_object, to_place, mission_reqs[k], random_table);
						}
					}
				}

				if( params ) {
					F32 radius = solarSystemGetPointRadius(shoebox_point->point_rep, shoebox_point->missions);
					room_mission_object->params = StructClone(parse_GenesisProceduralObjectParams, params);
					room_mission_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
					room_mission_object->params->volume_properties->eShape = GVS_Sphere;
					room_mission_object->params->volume_properties->fSphereRadius = radius;
				}
			}

			eaDestroyEx(&locations, NULL);
		}

		//Add the object we are orbiting around
		if(point_list->orbit_object)
		{
			GenesisToPlaceObject *orbit_object;
			GenesisToPlaceObject *orbit_room_object = NULL;
			U32 point_seed = randomMersenneU32(random_table);
			F32 angle = randomMersenneF32(random_table)*PI;
			subVec3(point_list->orbit_mat[3], offset, point_list->orbit_mat[3]);

			if (point_list->name && point_list->name[0]) {
				GenesisToPlaceObject *room_missions_object;
				
				orbit_room_object = solarSystemPlaceGroupDef(NULL, point_list->name, point_list->orbit_mat[3], fZeroRot, system_object, to_place);
				if(!orbit_room_object->params)
					orbit_room_object->params = StructCreate(parse_GenesisProceduralObjectParams);
				orbit_room_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
				orbit_room_object->params->volume_properties->eShape = GVS_Sphere;
				orbit_room_object->params->volume_properties->fSphereRadius = point_list->orbit_radius;
				
				//Add mission objects
				room_missions_object = solarSystemPlaceGroupDef(NULL, "Missions", zerovec3, fZeroRot, orbit_room_object, to_place);
				if (eaSize(&mission_reqs) > 1)
				{
					room_missions_object->params = genesisCreateMultiMissionWrapperParams();
				}
			
				for (j = 0; j < eaSize(&mission_reqs); j++)
				{
					GenesisToPlaceObject *orbit_room_mission_object = NULL;
					GenesisProceduralObjectParams* params = genesisFindMissionRoomProceduralParams(mission_reqs[j], sol_sys->layout_name, point_list->name);
					F32 radius = point_list->orbit_radius;
					orbit_room_mission_object = solarSystemPlaceGroupDef(NULL, genesisMissionRoomVolumeName(sol_sys->layout_name, point_list->name, mission_reqs[j]->missionName), zerovec3, fZeroRot, room_missions_object, to_place);
					orbit_room_mission_object->challenge_name = strdup(orbit_room_mission_object->object_name);
					orbit_room_mission_object->challenge_is_unique = true;
					
					orbit_room_mission_object->params = StructClone(parse_GenesisProceduralObjectParams, params);
					orbit_room_mission_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
					orbit_room_mission_object->params->volume_properties->eShape = GVS_Sphere;
					orbit_room_mission_object->params->volume_properties->fSphereRadius = radius;
				}
			}

			sprintf(obj_name, "List_%d_Orbit_Object", i+1);
			orbit_object = solarSystemPlaceGroupDef( NULL, obj_name, zerovec3, fZeroRot,
													 orbit_room_object ? orbit_room_object : system_object,
													 to_place );
			copyMat4(point_list->orbit_mat, orbit_object->mat);
			if(orbit_room_object)
				setVec3same(orbit_object->mat[3], 0);
			solarSystemAddObjSet(sol_sys, solarsystem_lib, orbit_object, to_place,
								 NULL, point_list->orbit_object, angle, 1.0f,
								 point_seed, false, NULL, false, "PointList");
		}

		//Add the curves
		curve_object = NULL;
		if(point_list->spline)
		{
			for( j=0; j < eafSize(&point_list->spline->spline_points); j+=3 )
			{
				subFromVec3(offset, point_list->spline->spline_points + j);
			}

			sprintf(obj_name, "List_%d_Curve", i+1);
			curve_def = solarSystemMakeGroupDef(solarsystem_lib, obj_name);
			curve_def->property_structs.curve = StructCreate(parse_WorldCurve);
			curve_def->property_structs.curve->spline.spline_points = point_list->spline->spline_points;
			curve_def->property_structs.curve->spline.spline_up = point_list->spline->spline_up;
			curve_def->property_structs.curve->spline.spline_deltas = point_list->spline->spline_deltas;
			curve_def->property_structs.curve->spline.spline_geom = point_list->spline->spline_geom;
			point_list->spline->spline_points = NULL;
			point_list->spline->spline_up = NULL;
			point_list->spline->spline_deltas = NULL;
			point_list->spline->spline_geom = NULL;
			curve_object = solarSystemPlaceGroupDef(curve_def, "PointList", shoebox->layer_center, fZeroRot, parent_curve_object, to_place);
		}

		//Add the objects that are attached or near the curve
		solarSystemAddPathObjects(sol_sys, solarsystem_lib, detached_objects, curve_object, point_list, random_table, to_place);
	}

	//Add general detail objects 
	for( i=0; i < eaSize(&shoebox->detail_objects); i++ )
	{
		bool inited = false;
		SSLibObj *lib_obj = shoebox->detail_objects[i];
		F32 min_dist = lib_obj->offset.min_dist;
		F32 max_dist = lib_obj->offset.max_dist;
		F32 required_diff = SQRT3*(DETAIL_LOC_SEARCH_RES+10.0f);
		Vec3 start = {0.5, 0.5, 0.5};
		Vec3 layer_min, layer_max;
		lib_obj->cached_def = solarSystemGetGroupDefFromObject(lib_obj);
		shoebox->detail_placed = i;
		if(max_dist-min_dist < required_diff || min_dist < 0 || max_dist < 0)
		{
			F32 mid = (max_dist+min_dist)/2.0f;
			min_dist = MAX(required_diff, mid-required_diff/2.0f);
			max_dist = min_dist + required_diff;
			lib_obj->offset.min_dist = min_dist;
			lib_obj->offset.max_dist = max_dist;
		}
		if(!lib_obj->cached_def)
		{
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextSolSysDetailObject(lib_obj->obj.name_str),
							  "Could not find detail object.");
			break;
		}

		//Find the searching bounds
		for( k=0; k < eaSize(&shoebox->point_lists); k++ )
		{
			ShoeboxPointList *list = shoebox->point_lists[k];
			for( j=0; j < eaSize(&list->points); j++ )
			{
				F32 radius;
				Vec3 abs_pos;
				Vec3 point_min, point_max;

				//Find the maximum distance possible from this point
				addVec3(list->points[j]->pos, shoebox->layer_center, abs_pos);
				radius = lib_obj->offset.max_dist + lib_obj->cached_def->bounds.radius + solarSystemGetPointRadius(list->points[j]->point_rep, list->points[j]->missions) + 10.0f;
				copyVec3(abs_pos, point_min);
				subVec3same(point_min, radius+required_diff, point_min);
				copyVec3(abs_pos, point_max);
				addVec3same(point_max, radius+required_diff, point_max);
				//Update search bounds
				if(!inited)
				{
					copyVec3(point_min, shoebox->search_min);
					copyVec3(point_max, shoebox->search_max);
					inited = true;
				}
				else
				{
					MINVEC3(point_min, shoebox->search_min, shoebox->search_min);
					MAXVEC3(point_max, shoebox->search_max, shoebox->search_max);
				}
			}
		}
		//Find the bounds that will keep the object inside the region.
		addVec3same(shoebox->layer_min, lib_obj->cached_def->bounds.radius, layer_min);
		subVec3same(shoebox->layer_max, lib_obj->cached_def->bounds.radius, layer_max);
		if(	layer_min[0] >= layer_max[0] || 
			layer_min[1] >= layer_max[1] || 
			layer_min[2] >= layer_max[2] )
		{
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextSolSysDetailObject(lib_obj->obj.name_str),
							  "Object is too big to fit into the shoebox.");
			break;
		}
		if(inited)
		{
			GenesisToPlaceObject *detail_object_child;
			F32 test_size = 0;
			//Ensure that we only search inside the region
			MAXVEC3(layer_min, shoebox->search_min, shoebox->search_min);
			MINVEC3(layer_max, shoebox->search_max, shoebox->search_max);

			test_size = MAX(test_size, shoebox->search_max[0]-shoebox->search_min[0]);
			test_size = MAX(test_size, shoebox->search_max[1]-shoebox->search_min[1]);
			test_size = MAX(test_size, shoebox->search_max[2]-shoebox->search_min[2]);
			if(!solarSystemFindLocationOctTree(start, test_size, DETAIL_LOC_SEARCH_RES, random_table, solarSystemIsValidDetailLoc, shoebox))
			{
				genesisRaiseError(GENESIS_WARNING, genesisMakeTempErrorContextLayout(solsys_name), "Not all detail objects were able to be placed.  Failed placing \"%s\".",
								  lib_obj->obj.name_str);
				break;
			}
			detail_object_child = solarSystemPlaceGroupDef(lib_obj->cached_def, "Detail Object", lib_obj->offset.pos, fZeroRot, detail_object, to_place);
			detail_object_child->seed = randomMersenneU32(random_table);
		}
		else
		{
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextLayout(solsys_name), "You must have at least one room if you want to use detail objects.");		
		}
	}

	mersenneTableFree(random_table);
}

void solarSystemAddLightProps(GroupDef *def, SpaceBackdropLight *light_props)
{
	char prop_string[256] = "";
	if (!light_props)
		return;
	sprintf(prop_string, "%f, %f, %f", light_props->light_diffuse_hsv[0], light_props->light_diffuse_hsv[1], light_props->light_diffuse_hsv[2]);
	groupDefAddProperty(def, "LightDiffuseHSV", prop_string);
	sprintf(prop_string, "%f, %f, %f", light_props->light_secondary_diffuse_hsv[0], light_props->light_secondary_diffuse_hsv[1], light_props->light_secondary_diffuse_hsv[2]);
	groupDefAddProperty(def, "LightSecondaryDiffuseHSV", prop_string);
	sprintf(prop_string, "%f, %f, %f", light_props->light_specular_hsv[0], light_props->light_specular_hsv[1], light_props->light_specular_hsv[2]);
	groupDefAddProperty(def, "LightSpecularHSV", prop_string);
	sprintf(prop_string, "%f, %f, %f", light_props->light_ambient_hsv[0], light_props->light_ambient_hsv[1], light_props->light_ambient_hsv[2]);
	groupDefAddProperty(def, "LightAmbientHSV", prop_string);
}

static GenesisToPlaceObject* solarSystemMakeToPlaceVolume(GenesisToPlaceObject *parent_object, const char *name, Vec3 min, Vec3 max)
{
	GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
	to_place_object->object_name = allocAddString(name);
	copyMat4(unitmat, to_place_object->mat);
	to_place_object->params = StructCreate(parse_GenesisProceduralObjectParams);
	to_place_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
	to_place_object->params->volume_properties->eShape = GVS_Box;
	copyVec3(min, to_place_object->params->volume_properties->vBoxMin);
	copyVec3(max, to_place_object->params->volume_properties->vBoxMax);
	to_place_object->uid = 0;
	to_place_object->parent = parent_object;
	return to_place_object;
}

// Shared function between wlGenesisSolarSystem and wlUGC
void solarSystemCreateCommonObjects(GenesisShoebox *shoebox, GenesisBackdrop *backdrop, const char *layout_name,
									GroupDefLib *def_lib, GenesisToPlaceState *to_place, GenesisMissionRequirements **mission_reqs)
{
	int i;
	int unit_box_uid = objectLibraryUIDFromObjName("Sys_Unit_Box");
	GenesisToPlaceObject *others_group, *new_object;
	GroupDef *new_group, *volume_def;
	Vec3 min, max;
	float fZeroRot=0.0f;
	int temp_uid;

	//Make a parent group to put all these extra items.
	others_group = solarSystemPlaceGroupDef(NULL, "Other Items", zerovec3, fZeroRot, NULL, to_place);
	//Init min and max to the whole shoebox
	subVec3(shoebox->layer_min, shoebox->layer_center, min);
	subVec3(shoebox->layer_max, shoebox->layer_center, max);

	//Add the sound volume
	volume_def = solarSystemMakeGroupDef(def_lib, "Playable");
	solarSystemInitVolume(volume_def, min, max);
	if(backdrop && eaSize(&backdrop->amb_sounds) > 0)
	{
		volume_def->property_structs.sound_sphere_properties = StructCreate(parse_WorldSoundSphereProperties);
		volume_def->property_structs.sound_sphere_properties->pcEventName = allocAddString(backdrop->amb_sounds[0]);
	}
	groupDefAddVolumeType(volume_def, "Playable");
	volume_def->property_structs.room_properties = StructCreate(parse_WorldRoomProperties);
	volume_def->property_structs.room_properties->eRoomType = WorldRoomType_Room;
	solarSystemPlaceGroupDef(volume_def, "Playable", shoebox->layer_center, fZeroRot, others_group, to_place);

	// FX Volume
	if(backdrop && backdrop->fx_volume)
	{
		GenesisToPlaceObject *to_place_object = solarSystemMakeToPlaceVolume(others_group, "FX_Volume", min, max);
		to_place_object->params->fx_volume = StructClone(parse_WorldFXVolumeProperties, backdrop->fx_volume);
		genesisProceduralObjectEnsureType(to_place_object->params);
		eaPush(&to_place->objects, to_place_object);	
	}
	// Power Volume
	if(backdrop && backdrop->power_volume)
	{
		GenesisToPlaceObject *to_place_object = solarSystemMakeToPlaceVolume(others_group, "Power_Volume", min, max);
		to_place_object->params->power_volume = StructClone(parse_WorldPowerVolumeProperties, backdrop->power_volume);
		genesisProceduralObjectEnsureType(to_place_object->params);
		eaPush(&to_place->objects, to_place_object);	
	}

	//Add any mission requirements to the sound volume, as it takes up the whole space anyways
	{
		GenesisToPlaceObject *mission_parent;
		mission_parent = calloc(1, sizeof(GenesisToPlaceObject));
		mission_parent->object_name = allocAddString("Missions");
		identityMat3(mission_parent->mat);
		mission_parent->uid = 0;
		mission_parent->parent = others_group;
		if (eaSize(&mission_reqs) > 1)
		{
			mission_parent->params = genesisCreateMultiMissionWrapperParams();
		}
		eaPush(&to_place->objects, mission_parent);
		
		for( i=0; i < eaSize(&mission_reqs); i++ )
		{
			if(!mission_reqs[i]->params || !mission_reqs[i]->missionName)
				continue;
			volume_def = solarSystemMakeGroupDef(def_lib, genesisMissionVolumeName(layout_name, mission_reqs[i]->missionName));
			genesisApplyObjectParams(volume_def, mission_reqs[i]->params);
			solarSystemInitVolume(volume_def, min, max);
			{
				GenesisToPlaceObject* mission_obj = solarSystemPlaceGroupDef(
						volume_def, genesisMissionVolumeName(layout_name, mission_reqs[i]->missionName), shoebox->layer_center, fZeroRot, mission_parent, to_place);
				mission_obj->challenge_name = strdup(mission_obj->object_name);
				mission_obj->challenge_is_unique = true;
			}
		}
	}

	//Add a guide plane to make editing maps easier
	temp_uid = objectLibraryUIDFromObjName("Space_Guide_Plane_01");
	new_group = objectLibraryGetGroupDef(temp_uid, true);
	if(!new_group)
		genesisRaiseErrorInternal(GENESIS_ERROR, OBJECT_LIBRARY_DICT, "Space_Guide_Plane_01", "Could not find (Space_Guide_Plane_01).");
	else 
		solarSystemPlaceGroupDef(new_group, "Guide Plane", shoebox->layer_center, fZeroRot, others_group, to_place);

	//Add a bounding cylinder to help in editing
	temp_uid = objectLibraryUIDFromObjName("Space_Cylinder");
	new_group = objectLibraryGetGroupDef(temp_uid, true);
	if(!new_group)
		genesisRaiseErrorInternal(GENESIS_ERROR, OBJECT_LIBRARY_DICT, "Space_Cylinder", "Could not find (Space_Cylinder).");
	else
		solarSystemPlaceGroupDef(new_group, "Cylinder", shoebox->layer_center, fZeroRot, others_group, to_place);

	//Add Sun Light
	new_group = solarSystemMakeGroupDef(def_lib, "SunLight");
	groupDefAddProperty(new_group, "LightType", "DirectionalLight");
	groupDefAddProperty(new_group, "LightIsKey", "1");
	groupDefAddProperty(new_group, "LightIsSun", "1");
	groupDefAddProperty(new_group, "LightInfiniteShadows", "1");
	groupDefAddProperty(new_group, "LightCastsShadows", "1");
	groupDefAddProperty(new_group, "LightRadius", "26000");
	groupDefAddProperty(new_group, "LightRadiusInner", "26000");
	solarSystemAddLightProps(new_group, backdrop ? backdrop->near_light : NULL);
	new_object = solarSystemPlaceGroupDef(new_group, "Light", shoebox->layer_center, fZeroRot, NULL, to_place);
	if(!sameVec3(shoebox->overview_pos, zerovec3))
	{
		copyVec3(shoebox->overview_pos, new_object->mat[1]);
		scaleVec3(new_object->mat[1], -1.0f, new_object->mat[1]);
		normalVec3(new_object->mat[1]);
		if(nearSameF32(new_object->mat[1][1], 1.0f))
			setVec3(new_object->mat[0], 1.0f, 0.0f, 0.0f);
		else
			setVec3(new_object->mat[0], 0.0f, 1.0f, 0.0f);
		crossVec3(new_object->mat[0], new_object->mat[1], new_object->mat[2]);
		normalVec3(new_object->mat[2]);
		crossVec3(new_object->mat[1], new_object->mat[2], new_object->mat[0]);
		normalVec3(new_object->mat[0]);
	}
}

static void solarSystemAddSuns(GenesisBackdrop *backdrop, Vec3 solar_system_center, GenesisToPlaceState *to_place, U32 seed)
{
	int i;
	float fZeroRot=0.0f;
	SolarSystemSunsInfo *sun_info;
	MersenneTable *random_table;

	if(!backdrop || !backdrop->sun_info || !backdrop->sun_info->sun_list)
		return;
	sun_info = backdrop->sun_info;

	random_table = mersenneTableCreate(seed);
	for( i=0; i < eaSize(&sun_info->sun_list->suns); i++ )
	{
		GroupDef *sun_group = NULL;
		SSLibObj *sun = sun_info->sun_list->suns[i];
		F32 rand_val;
		Vec3 solar_pos;

		rand_val = (randomMersenneF32(random_table)+1.0f)/2.0f;
		solar_pos[0] = interpF32(rand_val, sun->offset.offset_min[0], sun->offset.offset_max[0]);
		rand_val = (randomMersenneF32(random_table)+1.0f)/2.0f;
		solar_pos[1] = interpF32(rand_val, sun->offset.offset_min[1], sun->offset.offset_max[1]);
		rand_val = (randomMersenneF32(random_table)+1.0f)/2.0f;
		solar_pos[2] = interpF32(rand_val, sun->offset.offset_min[2], sun->offset.offset_max[2]);

		scaleVec3(solar_pos, SYSTEM_OVERVIEW_SCALE, solar_pos);
		addVec3(solar_pos, solar_system_center, solar_pos);

		sun_group = solarSystemGetGroupDefFromObject(sun);
		if(sun_group)
			solarSystemPlaceGroupDef(sun_group, "Sun", solar_pos, fZeroRot, NULL, to_place);
		else
			genesisRaiseErrorInternal(GENESIS_ERROR, "Backdrop", backdrop->name, "Could not find sun (%s)", sun->obj.name_str);
	}
	mersenneTableFree(random_table);
}

void solarSystemPopulateMiniLayer(ZoneMap *zmap, GenesisMissionRequirements **mission_reqs, ZoneMapLayer *layer, GenesisSolSysZoneMap *solar_system, int layer_idx)
{
	GenesisBackdrop *backdrop = solar_system->backdrop;
	GenesisToPlaceState to_place = { 0 };
	U32 seed = (solar_system->layout_seed ? solar_system->layout_seed : zmap->map_info.genesis_data->seed+layer_idx);
	GenesisShoebox *shoebox = &solar_system->shoebox;
	float fZeroRot=0.0f;

	solarsystem_lib = layer->grouptree.def_lib;

	loadstart_printf("Populating solar system layer %s...", layerGetFilename(layer));

	{
		GroupDef *new_group;
		GroupDef *vol_def;
		Vec3 solar_system_center, min, max;

		solar_system_center[0] = 0;
		solar_system_center[1] = -WORLD_BOUNDS - MINI_SS_SIZE/2.0f - MINI_SS_SIZE*layer_idx;
		solar_system_center[2] = 0;

		//Add the mini solar system representations of all the shoeboxes
		{
			F32 scale = 1.0f;
			Vec3 solar_pos;
			GenesisToPlaceObject *tagged_object;
			char id_str[16] = "";

			scaleVec3(shoebox->overview_pos, SYSTEM_OVERVIEW_SCALE, solar_pos);
			addVec3(solar_pos, solar_system_center, solar_pos);

			tagged_object = solarSystemPlaceGroupDef(NULL, "Planet", solar_pos, fZeroRot, NULL, &to_place);
			if (!tagged_object->params)
				tagged_object->params = StructCreate(parse_GenesisProceduralObjectParams);
			tagged_object->params->physical_properties.iTagID = layer_idx+1;
		}

		//Add the suns
		solarSystemAddSuns(backdrop, solar_system_center, &to_place, seed);

		// Add the light for the mini system
		new_group = solarSystemMakeGroupDef(solarsystem_lib, "SunLight");
		groupDefAddProperty(new_group, "LightType", "PointLight");
		groupDefAddProperty(new_group, "LightIsKey", "1");
		groupDefAddProperty(new_group, "LightIsSun", "1");
		groupDefAddProperty(new_group, "LightRadius", "5000");
		groupDefAddProperty(new_group, "LightRadiusInner", "5000");
		solarSystemAddLightProps(new_group, backdrop ? backdrop->far_light : NULL);
		groupSetBounds(new_group, false);
		solarSystemPlaceGroupDef(new_group, "Light", solar_system_center, fZeroRot, NULL, &to_place);

		// Add a volume so that it is easier for artist to edit the layer.
		vol_def = solarSystemMakeGroupDef(solarsystem_lib, "Mini_Vol");
		setVec3same(min, -MINI_SS_SIZE/2.0f+SHOEBOX_OVERLAP_CORRECTION+SHOEBOX_WARP_VOL_SIZE);
		setVec3same(max,  MINI_SS_SIZE/2.0f-SHOEBOX_OVERLAP_CORRECTION-SHOEBOX_WARP_VOL_SIZE);
		solarSystemInitVolume(vol_def, min, max);
		solarSystemPlaceGroupDef(vol_def, "Mini_Vol", solar_system_center, fZeroRot, NULL, &to_place);

		// Actually place all the objects
		genesisPlaceObjects(zmapGetInfo(zmap), &to_place, layer->grouptree.root_def);
	}

	loadend_printf("Done.");
}

void solarSystemPopulateLayer(ZoneMap *zmap, GenesisMissionRequirements **mission_reqs, ZoneMapLayer *layer, GenesisSolSysZoneMap *solar_system, int layer_idx)
{
	GenesisBackdrop *backdrop = solar_system->backdrop;
	GenesisToPlaceState to_place = { 0 };
	U32 seed = (solar_system->layout_seed ? solar_system->layout_seed : zmap->map_info.genesis_data->seed+layer_idx);
	GenesisShoebox *shoebox = &solar_system->shoebox;
	char name_str[MAX_PATH];
	Vec3 layer_min, layer_max;

	name_str[0] = 0;

	solarsystem_lib = layer->grouptree.def_lib;

	loadstart_printf("Populating solar system layer %s...", layerGetFilename(layer));

	//Find the bounds and center of the layer
	genesisGetBoundsForLayer(zmap->map_info.genesis_data, GenesisMapType_SolarSystem, layer_idx, layer_min, layer_max);
	addVec3(layer_min, layer_max, shoebox->layer_center);
	scaleVec3(shoebox->layer_center, 0.5f, shoebox->layer_center);
	copyVec3(layer_min, shoebox->layer_min);
	copyVec3(layer_max, shoebox->layer_max);

	//Add objects common to all solar systems.
	solarSystemCreateCommonObjects(&solar_system->shoebox, solar_system->backdrop, solar_system->layout_name, solarsystem_lib, &to_place, mission_reqs);

	//Add the description based items
	solarSystemPopulateShoebox(solar_system, layer, mission_reqs, &to_place, seed, solar_system->tmog_version);

	//Place Way Points and such 
	genesisPopulateWaypointVolumes(&to_place, mission_reqs);

	//Actually place all the objects
	genesisPlaceObjects(zmapGetInfo(zmap), &to_place, layer->grouptree.root_def);

	loadend_printf("done.");
}

SSObjSet* solarSystemFindObjSet(SSObjSet** missions, const char* mission_name)
{
	int it;
	for( it = 0; it != eaSize(&missions); ++it ) {
		if( stricmp( missions[it]->mission_name, mission_name ) == 0 ) {
			return missions[it];
		}
	}

	return NULL;
}

#endif

void solarSystemCalculateShoeboxPositions(ZoneMap *zmap, GenesisSolSysZoneMap *solar_system, int layer_idx)
{
	int i;
	U32 seed = (solar_system->layout_seed ? solar_system->layout_seed : zmap->map_info.genesis_data->seed+layer_idx);
	F32 angles[MAX_SHOEBOX];
	int choices_left = MAX_SHOEBOX;
	MersenneTable *random_table;

	random_table = mersenneTableCreate(seed);

	//Create and layers for each solar system
	for( i=0; i < MAX_SHOEBOX; i++ )
	{
		angles[i] = (2*PI/MAX_SHOEBOX)*i - PI;
	}

	{
		GenesisShoebox *shoebox = &solar_system->shoebox;
		SolarSystemSunsInfo *sun_info=NULL;
		U8 rand_angle;
		F32 angle;
		F32 height;
		F32 dist = 0;

		//Get random position for Shoebox
		rand_angle = randomMersenneU32(random_table)%choices_left;
		choices_left--;

		if(solar_system->backdrop)
			sun_info = solar_system->backdrop->sun_info;
		if (sun_info)
			dist = (sun_info->oribit_max-sun_info->oribit_min)*(randomMersenneF32(random_table)+1.0f)/2.0f + sun_info->oribit_min;
		else
			genesisRaiseErrorInternalCode(GENESIS_ERROR, "Backdrop", SAFE_MEMBER(solar_system->backdrop, name),
										  "No sun info found.  Shoeboxes will be placed badly.");

		angle = angles[rand_angle];
		angles[rand_angle] = angles[choices_left];

		height = randomMersenneF32(random_table);
		if(height > 0 && height < 0.5f)//We want the light to shine down on the player more often.
			height -= 1.0f;
		height *= (dist/2.0f);

		shoebox->overview_pos[0] = dist*cos(angle);
		shoebox->overview_pos[1] = height;
		shoebox->overview_pos[2] = dist*sin(angle);
	}
	mersenneTableFree(random_table);
}

/// Fixup function for SSTagObj
TextParserResult fixupSSTagObj(SSTagObj *pTagObj, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup tags into new format
			{
				if( pTagObj->oldTags ) {
					eaDestroyEx( &pTagObj->tags, StructFreeString );
					DivideString( pTagObj->oldTags, ",", &pTagObj->tags,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pTagObj->oldTags );
				}
			}
		}
	}
	
	return PARSERESULT_SUCCESS;
}


/// Fixup function for SSTagObj
TextParserResult fixupSolarSystemLayout(GenesisSolSysLayout *pLayout, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup tags into new format
			{
				if( pLayout->old_environment_tags ) {
					eaDestroyEx( &pLayout->environment_tags, StructFreeString );
					DivideString( pLayout->old_environment_tags, ",", &pLayout->environment_tags,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pLayout->old_environment_tags );
				}
			}
		}
	}
	
	return PARSERESULT_SUCCESS;
}

#include "wlGenesisSolarSystem_h_ast.c"
