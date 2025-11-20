#include "RoomConn.h"
#include "RoomConnPrivate.h"
#include "ScratchStack.h"
#include "qsortG.h"

#include "GenericMesh.h"
#include "partition_enums.h"
#include "rand.h"
#include "WorldGridPrivate.h"
#include "WorldCellEntryPrivate.h"
#include "wlModelInline.h"
#include "wlVolumes.h"
#include "wlState.h"
#include "bounds.h"

#include "PhysicsSDK.h"
#include "WorldColl.h"

#include "timing.h"

#include "grouputil.h"

#define ROOM_VEC3_TOL 0.01f	
#define ROOM_MAX_RESTARTS 5
#define EXPAND_AMOUNT 0.4f

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

/********************
* DEFINITIONS
********************/
static void roomPortalDestroyCallbacks(RoomPortal *portal);
static void roomDestroyCallbacks(Room *room);

/******
* This function frees the geometric data associated with a room partition.  This is generally
* called before updates of the room/partition.
* PARAMS:
*   partition - RoomPartition whose data is to be freed
******/
static void roomPartitionFreeData(RoomPartition *partition)
{
	if (partition->mesh)
	{
		gmeshFreeData(partition->mesh);
		SAFE_FREE(partition->mesh);
	}
	if (partition->reduced_mesh)
	{
		gmeshFreeData(partition->reduced_mesh);
		SAFE_FREE(partition->reduced_mesh);
	}
	if (partition->hull)
	{
		hullFreeData(partition->hull);
		SAFE_FREE(partition->hull);
	}
}

/******
* This function frees all of a partition's memory.
* PARAMS:
*   partition - RoomPartition to destroy
******/
static void roomPartitionDestroy(RoomPartition *partition)
{
	int i;
	SAFE_FREE(partition->zmap_scope_name);
	if (wl_state.gfx_map_photo_unregister)
	{
		for( i=0; i < eaSize(&partition->tex_list); i++ )
		{
			wl_state.gfx_map_photo_unregister(partition->tex_list[i]);
		}
		wl_state.gfx_map_photo_unregister(partition->overview_tex);
	}
	roomPartitionFreeData(partition);
	eaDestroyEx(&partition->models, NULL);
	free(partition);
}

/******
* This function destroys the specified room portal's data.
* PARAMS:
*   portal - RoomPortal to free
******/
static void roomPortalDestroy(RoomPortal *portal)
{
	// invoke destruction callbacks
	roomPortalDestroyCallbacks(portal);

	// destroy external data
	StructDestroy(parse_WorldSoundConnProperties, portal->sound_conn_props);

	// destroy main data
	SAFE_FREE(portal->def_name);
	free(portal);
}

/******
* This function destroys the specified room's data and all of its partitions.
* PARAMS:
*   room - Room to free
******/
static void roomDestroy(Room *room)
{
	// invoke destruction callbacks
	roomDestroyCallbacks(room);

	StructDeInit(parse_GroupVolumePropertiesClient, &room->client_volume);
	StructDeInit(parse_GroupVolumePropertiesServer, &room->server_volume);

	// destroy main data
	SAFE_FREE(room->def_name);
	eaDestroyEx(&room->portals, roomPortalDestroy);
	eaDestroyEx(&room->partitions, roomPartitionDestroy);
	eaDestroyEx(&room->occlusion_entries, worldCellEntryFree);
	if (room->volume_entry)
	{
		worldCellEntryFree(&room->volume_entry->base_entry);
		room->volume_entry = NULL;
	}
	eaDestroyEx(&room->portal_volumes, wlVolumeFree);
	if (room->union_mesh)
	{
		gmeshFreeData(room->union_mesh);
		free(room->union_mesh);
		room->union_mesh = NULL;
	}
	free(room);
}

/******
* This function creates a new portal and populates its primary data except for its parent data.
* PARAMS:
*   bounds_min - Vec3 local minimum bounds
*   bounds_max - Vec3 local maximum bounds
*   world_mat - Mat4 matrix transforming local coordinates to world coordinates
* RETURNS:
*   RoomPortal created with the specified data
******/
static RoomPortal *roomPortalCreate(const Vec3 bounds_min, const Vec3 bounds_max, Mat4 world_mat)
{
	RoomPortal *portal = calloc(1, sizeof(*portal));
	Vec3 min, max;

	copyVec3(bounds_min, portal->bounds_min);
	copyVec3(bounds_max, portal->bounds_max);
	copyMat4(world_mat, portal->world_mat);

	// calculate midpoint
	mulVecMat4(bounds_min, world_mat, min);
	mulVecMat4(bounds_max, world_mat, max);
	centerVec3(min, max, portal->world_mid);

	return portal;
}

/********************
* HULL CALCULATION
********************/
/******
* This function takes a model and returns its verts in world coordinates.
* PARAMS:
*   model - RoomPartition model from which to retrieve the verts
*   verts - Vec3 dynArray pointer
*   count - int pointer to the number of elements in the dynArray
*   max_count - int pointer to the size of the dynArray
******/
static void roomGetPartitionModelVerts(RoomPartition *partition, RoomPartitionModel *model, Vec3 **verts, int *count, int *max_count)
{
	int i;
	int curr_count = *count;

	if (!model->def)
		return;

	if (partition->parent_room->use_model_verts || SAFE_MEMBER(model->def->property_structs.room_properties, eRoomType) == WorldRoomType_Portal)
	{
		Vec3 temp;
		const Vec3 *verts_source;
		ModelLOD *model_lod;

		if (!model->model || !(model_lod = modelLoadLOD(model->model, 0)))
			return;

		// JE: model change: This needs to not be a synchronous stall! Unless it's only in the editors/binning or something?
		model_lod = modelLODWaitForLoad(model->model, 0);

		dynArrayAddStructs((*verts), (*count), (*max_count), model_lod->vert_count);
		verts_source = modelGetVerts(model_lod);

		// transform vertices by the tracker's world matrix (and scale, if necessary)
		for (i = 0; i < model_lod->vert_count; i++)
		{
			Vec3 vert;
			if (model->def)
			{
				vert[0] = verts_source[i][0] * model->def->model_scale[0];
				vert[1] = verts_source[i][1] * model->def->model_scale[1];
				vert[2] = verts_source[i][2] * model->def->model_scale[2];
			}
			else
				copyVec3(verts_source[i], vert);
			mulVecMat4(vert, model->world_mat, temp);
			copyVec3(temp, (*verts)[curr_count + i]);
		}
	}
	else
	{
		Vec4_aligned bounds[8];
		dynArrayAddStructs((*verts), (*count), (*max_count), 8);
		mulBounds(model->def->bounds.min, model->def->bounds.max, model->world_mat, bounds);	
		for (i=0; i<8; i++)
		{
			copyVec3(bounds[i], (*verts)[curr_count + i]);
		}
	}
}

static bool createPartitionOcclusionMeshInternal(RoomPartition *partition, GMesh *temp_mesh)
{
	Vec3 *normals;
	int j;

	if (partition->box_partition)
	{
		gmeshFromBoundingBox(temp_mesh, partition->local_min, partition->local_max, partition->world_mat);
		gmeshInvertTriangles(temp_mesh);
	}
	else if (partition->mesh)
		gmeshCopy(temp_mesh, partition->reduced_mesh, true);
	else
		return false;

	// push verts back
	normals = ScratchAlloc(temp_mesh->vert_count * sizeof(Vec3));

	for (j = 0; j < temp_mesh->tri_count; ++j)
	{
		GTriIdx *tri = &temp_mesh->tris[j];
		Vec3 tri_normal;
		makePlaneNormal(temp_mesh->positions[tri->idx[0]], temp_mesh->positions[tri->idx[1]], temp_mesh->positions[tri->idx[2]], tri_normal);
		assert(FINITEVEC3(tri_normal));
		addVec3(tri_normal, normals[tri->idx[0]], normals[tri->idx[0]]);
		addVec3(tri_normal, normals[tri->idx[1]], normals[tri->idx[1]]);
		addVec3(tri_normal, normals[tri->idx[2]], normals[tri->idx[2]]);
	}

	for (j = 0; j < temp_mesh->vert_count; ++j)
	{
		normalVec3(normals[j]);
		scaleAddVec3(normals[j], -EXPAND_AMOUNT, temp_mesh->positions[j], temp_mesh->positions[j]);
	}

	ScratchFree(normals);

	return true;
}

/******
* This function calculates the union mesh of all partition meshes for the specified room.
* PARAMS:
*   room - Room whose union mesh is to be calculated
******/
static void roomCalculateUnionMesh(Room *room)
{
	GMesh temp_mesh = {0};
	int i;

	if (room->union_mesh)
	{
		gmeshFreeData(room->union_mesh);
		free(room->union_mesh);
		room->union_mesh = NULL;
	}

	room->union_mesh = calloc(1, sizeof(GMesh));
	gmeshSetUsageBits(room->union_mesh, USE_POSITIONS);

	for (i = 0; i < eaSize(&room->partitions); ++i)
	{
		RoomPartition *partition = room->partitions[i];

		if (!createPartitionOcclusionMeshInternal(partition, &temp_mesh))
			continue;

		// union partition occlusion meshes
		if (!room->union_mesh->vert_count)
			gmeshCopy(room->union_mesh, &temp_mesh, true);
		else
			gmeshBoolean(room->union_mesh, room->union_mesh, &temp_mesh, BOOLEAN_UNION, true);
	}
	gmeshFreeData(&temp_mesh);
}

/******
* This is the main function for generating a RoomConvexHull out of the points recursively
* contained within a particular GroupTracker's model (or children's models).
* PARAMS:
*   room - Room whose convex hulls are to be updated
******/
static void roomCalculateConvexHull(Room *room)
{
	bool bounds_set;
	int i, j, c;

	PERFINFO_AUTO_START_FUNC();

	SET_FP_CONTROL_WORD_DEFAULT;

	// go through all established partitions and generate convex hull independently for each one
	for (c = eaSize(&room->partitions) - 1; c >= 0; c--)
	{
		Vec3 *verts = NULL;
		int count = 0, max_count = 0;
		RoomPartition *partition = room->partitions[c];

		// free any existing partition hull data
		roomPartitionFreeData(partition);

		// get vertices from box partitions
		if (partition->box_partition)
		{
			int k, box_count = 0;

			dynArrayAddStructs(verts, count, max_count, 8);
			for (i = 0; i < 2; i++)
			{
				for (j = 0; j < 2; j++)
				{
					for (k = 0; k < 2; k++)
					{
						Vec3 local ={i ? partition->local_min[0] : partition->local_max[0],
							j ? partition->local_min[1] : partition->local_max[1],
							k ? partition->local_min[2] : partition->local_max[2]};
						mulVecMat4(local, partition->world_mat, verts[box_count++]);
					}
				}
			}
		}
		// get all vertices from partition models
		else
		{
			for (i = 0; i < eaSize(&partition->models); i++)
				roomGetPartitionModelVerts(partition, partition->models[i], &verts, &count, &max_count);
		}
		if (!verts)
			continue;
		else
		{
#if !PSDK_DISABLED
			PSDKMeshDesc mesh_desc = {0};
			partition->mesh = psdkCreateConvexHullGMesh("room hull", count, verts);

			if(!partition->mesh)
			{
				const char *filename = layerGetFilename(room->layer);
				ErrorFilenamef(filename, "Hull calculation for partition \"%s\" failed.", room->def_name);
			}
#endif
		}
		SAFE_FREE(verts);

		// convert the mesh to a generic convex hull and populate other data onto the partition
		if (partition->mesh)
		{
			// create a reduced mesh for mesh unioning; do NOT use for hull creation, as reduced mesh might not be convex
			partition->reduced_mesh = calloc(1, sizeof(*partition->reduced_mesh));
			gmeshCopy(partition->reduced_mesh, partition->mesh, true);
			gmeshReduceDebug(
				partition->reduced_mesh,
				partition->mesh,
				0.35f, 0, ERROR_RMETHOD,
				1, 0, true, true, false,
				true,
				NULL, NULL);

			// create the convex hull for containment checks
			partition->hull = gmeshToGConvexHullEx(partition->mesh, &partition->tri_to_plane);

			// update partition bounds
			bounds_set = false;
			for (i = 0; i < partition->mesh->vert_count; i++)
			{
				if (!bounds_set)
				{
					copyVec3(partition->mesh->positions[i], partition->bounds_max);
					copyVec3(partition->mesh->positions[i], partition->bounds_min);

					bounds_set = true;
				}
				else
				{
					vec3RunningMin(partition->mesh->positions[i], partition->bounds_min);
					vec3RunningMax(partition->mesh->positions[i], partition->bounds_max);
				}
			}
			addVec3(partition->bounds_min, partition->bounds_max, partition->bounds_mid);
			scaleVec3(partition->bounds_mid, 0.5f, partition->bounds_mid);
		}
	}

	// update room bounds
	copyVec3(zerovec3, room->bounds_min);
	copyVec3(zerovec3, room->bounds_max);
	bounds_set = false;
	for (i = 0; i < eaSize(&room->partitions); i++)
	{
		if (!room->partitions[i]->box_partition && eaSize(&room->partitions[i]->models) == 0)
			continue;
		if (!bounds_set)
		{
			copyVec3(room->partitions[i]->bounds_min, room->bounds_min);
			copyVec3(room->partitions[i]->bounds_max, room->bounds_max);
			bounds_set = true;
		}
		else
		{
			vec3RunningMin(room->partitions[i]->bounds_min, room->bounds_min);
			vec3RunningMax(room->partitions[i]->bounds_max, room->bounds_max);
		}
	}
	addVec3(room->bounds_min, room->bounds_max, room->bounds_mid);
	scaleVec3(room->bounds_mid, 0.5f, room->bounds_mid);

	// create room's union mesh
	roomCalculateUnionMesh(room);

	PERFINFO_AUTO_STOP_FUNC();
}

static GMesh *createPartitionOcclusionMesh(RoomPartition *partition, Room *room)
{
	GMesh *gmesh = calloc(1, sizeof(*gmesh));
	GMesh temp_mesh = {0};
	int i;

	PERFINFO_AUTO_START_FUNC();

	SET_FP_CONTROL_WORD_DEFAULT;

	if (!createPartitionOcclusionMeshInternal(partition, gmesh))
	{
		free(gmesh);
		return NULL;
	}

	for (i = 0; i < eaSize(&room->partitions); ++i)
	{
		RoomPartition *partition2 = room->partitions[i];

		if (partition2 == partition)
			continue;

		if (!createPartitionOcclusionMeshInternal(partition2, &temp_mesh))
			continue;

		// subtract other partition meshes from this partition's occlusion mesh
		gmeshBoolean(gmesh, gmesh, &temp_mesh, BOOLEAN_SUBTRACT_NOFILL, true);
	}

	for (i = 0; i < eaSize(&room->portals); ++i)
	{
		RoomPortal *portal = room->portals[i];

		gmeshFromBoundingBox(&temp_mesh, portal->bounds_min, portal->bounds_max, portal->world_mat);
		gmeshInvertTriangles(&temp_mesh);

		// subtract portal volume from occlusion mesh
		gmeshBoolean(gmesh, gmesh, &temp_mesh, BOOLEAN_SUBTRACT_NOFILL, true);
	}

	gmeshFreeData(&temp_mesh);

	gmeshReduce(gmesh, gmesh, 0.4f, 1000, gmesh->tri_count >= 1000 ? TRICOUNT_AND_ERROR_RMETHOD : ERROR_RMETHOD, 1, 0, true, true, false);

	PERFINFO_AUTO_STOP_FUNC();

	return gmesh;
}

/********************
* WORLD INTEGRATION
********************/
/******
* This is where you would place all of your room update callbacks, as it is called immediately after a room
* is updated.
* PARAMS:
*   room - Room that was just created
******/
static void roomUpdateCallbacks(Room *room, bool create_occ_entries)
{
	WorldVolumeElement **elements = NULL;
	WorldCellEntryData *data;
	WorldVolumeElement **portal_elements = NULL;
	GroupInfo info;
	int i;
	int ignoreSound = wlVolumeTypeNameToBitMask("IgnoreSound");

	if (!room)
		return;

	if(!(room->volume_type_bits & ignoreSound))
	{
		if (wl_state.sound_volume_update_func && room->client_volume.sound_volume_properties)
			room->sound_space = wl_state.sound_volume_update_func(room);
	}

	// free entries
	if (room->volume_entry)
	{
		worldCellEntryFree(&room->volume_entry->base_entry);
		room->volume_entry = NULL;
	}
	if (create_occ_entries)
		eaDestroyEx(&room->occlusion_entries, worldCellEntryFree);

	// free portal volumes
	eaDestroyEx(&room->portal_volumes, wlVolumeFree);

	// create entries
	initGroupInfo(&info, room->layer);
	info.region = layerGetWorldRegion(room->layer);

	for (i = 0; i < eaSize(&room->partitions); i++)
	{
		RoomPartition *partition = room->partitions[i];

		if (create_occ_entries && room->is_occluder && wlIsClient())
		{
			Vec3 local_min, local_max, local_mid;
			Mat4 world_matrix;
			GMesh *gmesh;
			WorldOcclusionEntry *occlusion_entry;

			if (gmesh = createPartitionOcclusionMesh(partition, room))
			{
				// center gmesh around midpoint
				gmeshGetBounds(gmesh, local_min, local_max, local_mid);
				copyMat4(unitmat, world_matrix);
				scaleVec3(local_mid, -1, world_matrix[3]);
				gmeshTransform(gmesh, world_matrix);
				copyVec3(local_mid, world_matrix[3]);

				occlusion_entry = createWorldOcclusionEntryFromMesh(gmesh, world_matrix, room->double_sided_occluder);
				copyVec3(occlusion_entry->base_drawable_entry.base_entry.bounds.world_mid, occlusion_entry->base_drawable_entry.world_fade_mid);
				occlusion_entry->base_drawable_entry.world_fade_radius = occlusion_entry->base_drawable_entry.base_entry.shared_bounds->radius;
				worldEntryInit(&occlusion_entry->base_drawable_entry.base_entry, &info, NULL, NULL, room, true);

				eaPush(&room->occlusion_entries, occlusion_entry);
			}
		}

		if (partition->mesh && partition->hull)
		{
			WorldVolumeElement *element = StructCreate(parse_WorldVolumeElement);

			element->volume_shape = WL_VOLUME_HULL;
			copyMat4(unitmat, element->world_mat);
			centerVec3(partition->bounds_min, partition->bounds_max, element->world_mat[3]);
			subVec3(partition->bounds_min, element->world_mat[3], element->local_min);
			subVec3(partition->bounds_max, element->world_mat[3], element->local_max);
			element->mesh = partition->mesh;
			element->hull = partition->hull;

			eaPush(&elements, element);
		}
	}

	if (!eaSize(&elements))
		goto updateLightOwnershipAndReturn;

	room->volume_entry = createWorldVolumeEntryForRoom(room, elements); // takes ownership of the elements earray
//	worldEntryInit(&room->volume_entry->base_entry, &info, NULL, NULL);
	data = worldCellEntryGetData(&room->volume_entry->base_entry);
	data->group_id = info.layer_id_bits; // TODO(CD)
	room->volume_entry->base_entry.bounds.object_tag = info.tag_id;
	assert(!room->volume_entry->base_entry.shared_bounds->ref_count);
	room->volume_entry->base_entry.shared_bounds = lookupSharedBounds(info.zmap, room->volume_entry->base_entry.shared_bounds);
	worldCellEntryOpenAll(&room->volume_entry->base_entry, info.region);

	// create portal supervolume
	for (i = 0; i < eaSize(&room->portals); i++)
	{
		WorldVolumeElement *element = StructCreate(parse_WorldVolumeElement);

		element->volume_shape = WL_VOLUME_BOX;
		element->face_bits = VOLFACE_ALL;
		copyMat4(room->portals[i]->world_mat, element->world_mat);
		copyVec3(room->portals[i]->bounds_min, element->local_min);
		copyVec3(room->portals[i]->bounds_max, element->local_max);
		eaPush(&portal_elements, element);
	}
	if (!eaSize(&portal_elements))
		goto updateLightOwnershipAndReturn;

	// Create volumes on each active partition
	for(i=wlVolumeMaxPartitionIndex(); i>=0; --i) {
		if (wlVolumePartitionExists(i)) {
			eaSet(&room->portal_volumes, 
				  wlVolumeCreate(i, wlVolumeTypeNameToBitMask("RoomPortal"), room->volume_entry, portal_elements),
				  i);
		}
	}

	eaDestroyStruct(&portal_elements, parse_WorldVolumeElement);

updateLightOwnershipAndReturn:
	if (wl_state.room_lighting_update_func)
		wl_state.room_lighting_update_func(room);

}

/******
* This is where you would place all of your room destruction callbacks, as it is called immediately before
* rooms are destroyed.
* PARAMS:
*   room - Room that was just destroyed
******/
static void roomDestroyCallbacks(Room *room)
{
	if (!room)
		return;

	if (wl_state.sound_volume_destroy_func && room->sound_space)
	{
		wl_state.sound_volume_destroy_func(room->sound_space);
		room->sound_space = NULL;
	}

	eaDestroy(&room->lights_in_room);

	if (wl_state.room_lighting_update_func)
		wl_state.room_lighting_update_func(room);
}

/******
* This is where you would place all of your portal update callbacks, as it is called immediately after a portal
* is updated.
* PARAMS:
*   portal - RoomPortal that was just created
******/
static void roomPortalUpdateCallbacks(RoomPortal *portal)
{
	if (!portal)
		return;

	if (wl_state.sound_conn_update_func && portal->sound_conn_props && !portal->sound_conn)
		portal->sound_conn = wl_state.sound_conn_update_func(portal);

	if (portal->parent_room && wl_state.room_lighting_update_func)
		wl_state.room_lighting_update_func(portal->parent_room);

	if (wl_state.mastermind_room_update_func)
		wl_state.mastermind_room_update_func(portal);
}

/******
* This is where you would place all of your portal destruction callbacks, as it is called immediately before
* portals are destroyed.
* PARAMS:
*   portal - RoomPortal that was just destroyed
******/
static void roomPortalDestroyCallbacks(RoomPortal *portal)
{
	if (!portal)
		return;

	if (wl_state.sound_conn_destroy_func && portal->sound_conn_props)
	{
		wl_state.sound_conn_destroy_func(portal);
		portal->sound_conn = NULL;
	}

	if (portal->parent_room && wl_state.room_lighting_update_func)
		wl_state.room_lighting_update_func(portal->parent_room);

	if (wl_state.mastermind_room_destroy_func)
		wl_state.mastermind_room_destroy_func(portal);
}

/******
* This function is used to traverse a graph and call the update callbacks on all of its elements.
* PARAMS:
*   graph - RoomConnGraph on which to call update callbacks
******/
static void roomConnGraphUpdateCallbacks(RoomConnGraph *graph, bool create_occ_entries)
{
	int i, j;
	for (i = 0; i < eaSize(&graph->rooms); i++)
	{
		Room *room = graph->rooms[i];
		roomUpdateCallbacks(room, create_occ_entries);
		for (j = 0; j < eaSize(&room->portals); j++)
			roomPortalUpdateCallbacks(room->portals[j]);
	}
}

/******
* This function adds a model (optionally specifying the tracker it is attached to for editing mode)
* to a room partition.  The model is used during hull updating to recalculate the hull from all of the
* partition's model verts.
* PARAMS:
*   partition - RoomPartition to which the model should be added
*   tracker - GroupTracker associated with the model; optional
*   def - GroupDef holding properties that may need to be applied to the model (ie. scale)
*   model - Model whose verts will be used to calculate the partition's hull
*   world_mat - Mat4 transforming the model into its world coordinates
******/
void roomPartitionAddModel(RoomPartition *partition, GroupTracker *tracker, GroupDef *def, Model *model, Mat4 world_mat)
{
	RoomPartitionModel *partition_model;
	
	// disallow models for box partitions
	if (partition->box_partition)
		return;

	partition_model = calloc(1, sizeof(*partition_model));
	partition_model->model = model;
	copyMat4(world_mat, partition_model->world_mat);
	partition_model->tracker = tracker;
	partition_model->def = def;
	eaPush(&partition->models, partition_model);
	roomDirty(partition->parent_room);
}

/******
* This function adds a new, empty partition to a room.
* PARAMS:
*   room - Room to which a partition will be added
*   tracker - GroupTracker that created this partition
* RETURNS:
*   RoomPartition added to the room
******/
RoomPartition *roomAddPartition(Room *room, GroupTracker *tracker)
{
	RoomPartition *partition;
	
	// do not allow any additional partitions for box rooms
	if (room->box_room && eaSize(&room->partitions) > 0)
		return NULL;

	partition = calloc(1, sizeof(*partition));
	partition->tracker = tracker;
	eaPush(&room->partitions, partition);
	partition->parent_room = room;

	if(wl_state.gfx_map_photo_register)
		partition->overview_tex = wl_state.gfx_map_photo_register("eui_src_loaded.wtex");

	roomDirty(room);
	return partition;
}

/******
* This function adds a box partition to a room.
* PARAMS:
*   room - Room to which a box partition will be added; cannot add partitions to box rooms
*   local_min - Vec3 local minimum of the box
*   local_max - Vec3 local maximum of the box
*   world_mat - Mat4 world matrix of the box
*   tracker - GroupTracker that created this partition
* RETURNS:
*   RoomPartition added to the room
******/
RoomPartition *roomAddBoxPartition(Room *room, Vec3 local_min, Vec3 local_max, Mat4 world_mat, GroupTracker *tracker)
{
	RoomPartition *partition = roomAddPartition(room, tracker);

	if (!partition)
		return NULL;

	// set box values to partition
	partition->box_partition = 1;
	copyVec3(local_min, partition->local_min);
	copyVec3(local_max, partition->local_max);
	copyMat4(world_mat, partition->world_mat);

	return partition;
}

/******
* This function searches a room for a room partition, removing and destroying it if found.
* PARAMS:
*   room - Room whose partitions are to be searched
*   partition - RoomPartition to search for and get rid of
******/
void roomRemovePartition(Room *room, RoomPartition *partition)
{
	if (eaFindAndRemove(&room->partitions, partition) >= 0)
	{
		roomPartitionDestroy(partition);
		if (room->volume_entry)
		{
			worldCellEntryFree(&room->volume_entry->base_entry);
			room->volume_entry = NULL;
		}
		eaDestroyEx(&room->portal_volumes, wlVolumeFree);
		roomDirty(room);
	}
}

/******
* This function searches a room for a partition model associated with a particular tracker, removing
* the model from the room partition.
* PARAMS:
*   room - Room whose partitions are to be searched
*   tracker - GroupTracker to search for
******/
void roomRemoveTracker(Room *room, GroupTracker *tracker)
{
	int i, j;
	bool found = false;

	for (i = 0; i < eaSize(&room->partitions) && !found; i++)
	{
		RoomPartition *partition = room->partitions[i];
		for (j = eaSize(&partition->models) - 1; j >= 0 && !found; j--)
		{
			RoomPartitionModel *model = partition->models[j];
			if (model->tracker == tracker)
			{
				free(eaRemove(&partition->models, j));
				roomDirty(room);
				found = true;
			}
		}
	}
}

/******
* This function adds an outgoing portal to a room, given the portal's position, orientation, and bounds.
* PARAMS:
*   room - Room to which a portal should be added
*   bounds_min - Vec3 local portal minimum bounds
*   bounds_max - Vec3 local portal maximum bounds
*   world_mat - Mat4 transforming local bounds to world coordinates
* RETURNS:
*   RoomPortal added
******/
RoomPortal *roomAddPortal(Room *room, const Vec3 bounds_min, const Vec3 bounds_max, Mat4 world_mat)
{
	RoomPortal *portal = roomPortalCreate(bounds_min, bounds_max, world_mat);

	eaPush(&room->portals, portal);
	portal->parent_graph = room->parent_graph;
	portal->parent_room = room;
	roomDirty(room);

	return portal;
}

/******
* This function removes an outgoing portal from a room.
* PARAMS:
*   room - Room to which a portal should be added
*   bounds_min - Vec3 local portal minimum bounds
*   bounds_max - Vec3 local portal maximum bounds
*   world_mat - Mat4 transforming local bounds to world coordinates
* RETURNS:
*   RoomPortal added
******/
void roomRemovePortal(Room *room, RoomPortal *portal)
{
	if (eaFindAndRemove(&room->portals, portal) >= 0)
	{
		if (portal->neighbor)
			portal->neighbor->neighbor = NULL;
		roomPortalDestroy(portal);
		eaDestroyEx(&room->portal_volumes, wlVolumeFree);
		roomDirty(room);
	}
}

/******
* This function marks a room dirty, flagging it for hull recalculation.
* PARAMS:
*   room - Room to dirty
******/
void roomDirty(Room *room)
{
	room->parent_graph->dirty = 1;
	room->dirty = 1;
}

/******
* This function marks a portal dirty, flagging its graph for connectivity recalculation.
* PARAMS:
*   portal - RoomPortal whose graph is to be dirtied
******/
void roomPortalDirty(RoomPortal *portal)
{
	portal->parent_room->parent_graph->dirty = 1;
}

/******
* This function recalculates a room's convex hull if it is dirty.
* PARAMS:
*   room - Room to update
******/
static void roomUpdateHull(Room *room)
{
	if (!room->dirty)
		return;

	// get convex hull data
	roomCalculateConvexHull(room);
}

/******
* This function destroys a room connectivity graph, including all of its rooms and portals.
* PARAMS:
*   conn_graph - RoomConnGraph to destroy
******/
void roomConnGraphDestroy(RoomConnGraph *conn_graph)
{
	eaDestroyEx(&conn_graph->rooms, roomDestroy);
	eaDestroyEx(&conn_graph->outdoor_portals, roomPortalDestroy);
}

static void roomConnGraphCopyGroupVolumeProperties(Room *room, GroupInfo *info, GroupDef *def)
{
	StructCopy(parse_GroupVolumePropertiesServer, &def->property_structs.server_volume, &room->server_volume, 0, 0, 0);
	StructCopy(parse_GroupVolumePropertiesClient, &def->property_structs.client_volume, &room->client_volume, 0, 0, 0);

	if(room->client_volume.sound_volume_properties)
	{
		// Copy the override sound event name into the binned data
		{
			const char *event_name = groupInfoGetStringParameter(info, room->client_volume.sound_volume_properties->event_name_override_param, room->client_volume.sound_volume_properties->event_name);
			if(event_name && event_name[0])
				room->client_volume.sound_volume_properties->event_name = allocAddString(event_name); // this may just be the already set sound event passed in as the default above
			else
				room->client_volume.sound_volume_properties->event_name = NULL; // this allows the override to unset the sound event
			room->client_volume.sound_volume_properties->event_name_override_param = NULL;
		}

		{
			// Copy the override sound dsp name into the binned data
			const char *dsp_name = groupInfoGetStringParameter(info, room->client_volume.sound_volume_properties->dsp_name_override_param, room->client_volume.sound_volume_properties->dsp_name);
			if(dsp_name && dsp_name[0])
				room->client_volume.sound_volume_properties->dsp_name = allocAddString(dsp_name); // this may just be the already set sound dsp passed in as the default above
			else
				room->client_volume.sound_volume_properties->dsp_name = NULL; // this allows the override to unset the sound dsp
			room->client_volume.sound_volume_properties->dsp_name_override_param = NULL;
		}
	}
}

/******
* This function adds an empty room to a connectivity graph.
* PARAMS:
*   info - GroupInfo containing the connectivity graph to which a room should be added
*   tracker - GroupTracker that created this room
*	def - GroupDef for the room itself
* RETURNS:
*   Room added to the graph
******/
Room *roomConnGraphAddRoom(GroupInfo *info, GroupTracker *tracker, GroupDef *def)
{
	Room *room = calloc(1, sizeof(*room));
	RoomPartition *partition;
	int i;

	room->dirty = 1;
	room->is_occluder = !!def->property_structs.room_properties->bOccluder;
	room->double_sided_occluder = !!info->double_sided_occluder;
	room->use_model_verts = !!def->property_structs.room_properties->bUseModels;

	roomConnGraphCopyGroupVolumeProperties(room, info, def);

	if(def->property_structs.hull) {
		for (i = 0; i < eaSize(&def->property_structs.hull->ppcTypes); ++i)
			room->volume_type_bits |= wlVolumeTypeNameToBitMask(def->property_structs.hull->ppcTypes[i]);
	}
	room->volume_type_bits |= wlVolumeTypeNameToBitMask("RoomVolume");

	// add new room to the connectivity graph
	eaPush(&info->region->room_conn_graph->rooms, room);
	room->parent_graph = info->region->room_conn_graph;
	info->region->room_conn_graph->dirty = 1;

	// add default partition
	partition = roomAddPartition(room, tracker);

	return room;
}

/******
* This function adds a box room to the connectivity graph.  This room cannot have partitions
* beneath it.
* PARAMS:
*   info - GroupInfo containing the connectivity graph to which a room should be added
*   tracker - GroupTracker that created this room
*	def - GroupDef for the room itself
* RETURNS:
*   Room added to the graph
******/
Room *roomConnGraphAddBoxRoom(SA_PARAM_NN_VALID GroupInfo *info, SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_NN_VALID GroupDef *def)
{
	Room *room = calloc(1, sizeof(*room));
	RoomPartition *partition;
	int i;

	room->box_room = 1;
	room->dirty = 1;
	room->is_occluder = !!def->property_structs.room_properties->bOccluder;
	room->double_sided_occluder = !!info->double_sided_occluder;
	room->use_model_verts = !!def->property_structs.room_properties->bUseModels;

	roomConnGraphCopyGroupVolumeProperties(room, info, def);

	if(def->property_structs.hull) {
		for (i = 0; i < eaSize(&def->property_structs.hull->ppcTypes); ++i)
			room->volume_type_bits |= wlVolumeTypeNameToBitMask(def->property_structs.hull->ppcTypes[i]);
	}
	room->volume_type_bits |= wlVolumeTypeNameToBitMask("RoomVolume");

	// add new room to the connectivity graph
	eaPush(&info->region->room_conn_graph->rooms, room);
	room->parent_graph = info->region->room_conn_graph;
	info->region->room_conn_graph->dirty = 1;

	// add default partition
	partition = roomAddBoxPartition(room, def->property_structs.volume->vBoxMin, def->property_structs.volume->vBoxMax, info->world_matrix, tracker);

	return room;
}

/******
* This function removes a room from a connectivity graph, disconnecting it from all portals.
* PARAMS:
*   conn_graph - RoomConnGraph from which to remove the room
*   room - Room to remove
******/
void roomConnGraphRemoveRoom(RoomConnGraph *conn_graph, Room *room)
{
	int i = conn_graph ? eaFind(&conn_graph->rooms, room) : -1;
	int j;

	if (i == -1)
		return;

	for (j = 0; j < eaSize(&room->portals); j++)
	{
		if (room->portals[j]->neighbor)
			room->portals[j]->neighbor->neighbor = NULL;
	}

	conn_graph->dirty = 1;
	eaRemove(&conn_graph->rooms, i);
	roomDestroy(room);
}

/******
* This function adds an outdoor portal to a room conn graph, given the portal's position, orientation, and bounds.
* PARAMS:
*   conn_graph - RoomConnGraph to which a portal should be added
*   bounds_min - Vec3 local portal minimum bounds
*   bounds_max - Vec3 local portal maximum bounds
*   world_mat - Mat4 transforming local bounds to world coordinates
* RETURNS:
*   RoomPortal added
******/
RoomPortal *roomConnGraphAddOutdoorPortal(RoomConnGraph *conn_graph, Vec3 bounds_min, Vec3 bounds_max, Mat4 world_mat)
{
	RoomPortal *portal = roomPortalCreate(bounds_min, bounds_max, world_mat);

	eaPush(&conn_graph->outdoor_portals, portal);
	portal->parent_graph = conn_graph;
	conn_graph->dirty = 1;

	return portal;
}

/******
* This function removes an outdoor portal from a connectivity graph.
* PARAMS:
*   conn_graph - RoomConnGraph from which the portal should be removed
*   portal - RoomPortal to remove
******/
void roomConnGraphRemoveOutdoorPortal(RoomConnGraph *conn_graph, RoomPortal *portal)
{
	int i = eaFind(&conn_graph->outdoor_portals, portal);

	if (i == -1)
		return;
	
	if (portal->neighbor)
		portal->neighbor->neighbor = NULL;
	conn_graph->dirty = 1;
	eaRemove(&conn_graph->outdoor_portals, i);
	roomPortalDestroy(portal);
}

/******
* This function goes through a connectivity graph, updates its room hulls that need to
* be recalculated, and recalculates connections between rooms through portals.
* PARAMS:
*   conn_graph - RoomConnGraph to update
******/
void roomConnGraphUpdate(RoomConnGraph *conn_graph)
{
	int i, j, k, l;

	if (!conn_graph || !conn_graph->dirty)
		return;

	// clear all portal neighbors
	for (i = 0; i < eaSize(&conn_graph->rooms); i++)
	{
		for (j = 0; j < eaSize(&conn_graph->rooms[i]->portals); j++)
			conn_graph->rooms[i]->portals[j]->neighbor = NULL;
	}

	// update room hulls
	for (i = 0; i < eaSize(&conn_graph->rooms); i++)
	{
		Room *room = conn_graph->rooms[i];
		roomUpdateHull(room);

		for (j = 0; j < eaSize(&room->portals); j++)
		{
			RoomPortal *portal = room->portals[j];

			// check for a potential neighbor; connect to first detected overlapping portal
			for (k = i + 1; k < eaSize(&conn_graph->rooms) && !portal->neighbor; k++)
			{
				for (l = 0; l < eaSize(&conn_graph->rooms[k]->portals) && !portal->neighbor; l++)
				{
					RoomPortal *neighbor_cand = conn_graph->rooms[k]->portals[l];

					if (neighbor_cand->neighbor)
						continue;

					// see if this portal touches any portals from other rooms
					if (orientBoxBoxCollision(portal->bounds_min, portal->bounds_max, portal->world_mat,
						neighbor_cand->bounds_min, neighbor_cand->bounds_max, neighbor_cand->world_mat))
					{
						portal->neighbor = neighbor_cand;
						neighbor_cand->neighbor = portal;
					}
				}
			}

			// if still not touching anything, see if this portal touches outdoor portals
			for (k = 0; k < eaSize(&conn_graph->outdoor_portals) && !portal->neighbor; k++)
			{
				RoomPortal *neighbor_cand = conn_graph->outdoor_portals[k];

				if (neighbor_cand->neighbor)
					continue;

				// see if this portal touches the outdoor portal
				if (orientBoxBoxCollision(portal->bounds_min, portal->bounds_max, portal->world_mat,
					neighbor_cand->bounds_min, neighbor_cand->bounds_max, neighbor_cand->world_mat))
				{
					portal->neighbor = neighbor_cand;
					neighbor_cand->neighbor = portal;
				}
			}
		}
	}

	// undirty everything and invoke update callbacks
	conn_graph->dirty = 0;
	for (i = 0; i < eaSize(&conn_graph->rooms); i++)
	{
		Room *room = conn_graph->rooms[i];

		if (room->dirty)
			roomUpdateCallbacks(room, true);

		room->dirty = 0;
		for (j = 0; j < eaSize(&room->portals); j++)
			roomPortalUpdateCallbacks(room->portals[j]);
	}
	for (i = 0; i < eaSize(&conn_graph->outdoor_portals); i++)
		roomPortalUpdateCallbacks(conn_graph->outdoor_portals[i]);
}

void roomConnGraphUpdateAllRegions(ZoneMap *zmap)
{
	int i;
	if(!zmap)
		return;
	for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
		roomConnGraphUpdate(zmap->map_info.regions[i]->room_conn_graph);
}

void roomConnGraphCreatePartition(ZoneMap *zmap, int iPartitionIdx)
{
	int i, j, k;

	if (!zmap)
	{
		return;
	}

	for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
	{
		RoomConnGraph *conn_graph = zmap->map_info.regions[i]->room_conn_graph;
		if (conn_graph) {
			for (j = 0; j < eaSize(&conn_graph->rooms); j++) {
				Room *room = conn_graph->rooms[j];

				// Open the world cell
				worldCellEntryOpen(iPartitionIdx, &room->volume_entry->base_entry, zmap->map_info.regions[i]);

				// Create any required portal volumes
				if (!eaGet(&room->portal_volumes, iPartitionIdx)) 
				{
					WorldVolumeElement **portal_elements = NULL;

					// create portal supervolume
					for (k = 0; k < eaSize(&room->portals); k++)
					{
						WorldVolumeElement *element = StructCreate(parse_WorldVolumeElement);

						element->volume_shape = WL_VOLUME_BOX;
						element->face_bits = VOLFACE_ALL;
						copyMat4(room->portals[k]->world_mat, element->world_mat);
						copyVec3(room->portals[k]->bounds_min, element->local_min);
						copyVec3(room->portals[k]->bounds_max, element->local_max);
						eaPush(&portal_elements, element);
					}
					if (portal_elements) {
						eaSet(&room->portal_volumes, 
							  wlVolumeCreate(iPartitionIdx, wlVolumeTypeNameToBitMask("RoomPortal"), room->volume_entry, portal_elements),
							  iPartitionIdx);
					}
				}
			}
		}
	}
}

void roomConnGraphDestroyPartition(ZoneMap *zmap, int iPartitionIdx)
{
	int i,j;

	if (!zmap)
	{
		return;
	}

	// Free the portal volumes on each partition
	for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
	{
		RoomConnGraph *conn_graph = zmap->map_info.regions[i]->room_conn_graph;
		if (conn_graph) {
			for (j = 0; j < eaSize(&conn_graph->rooms); j++) {
				Room *room = conn_graph->rooms[j];
				if (eaGet(&room->portal_volumes, iPartitionIdx)) 
				{
					wlVolumeFree(room->portal_volumes[iPartitionIdx]);
					eaSet(&room->portal_volumes, NULL, iPartitionIdx);
				}

				// Close the world cell
				worldCellEntryClose(iPartitionIdx, &room->volume_entry->base_entry);
			}
		}
	}
}

/******
* This function removes the contents of a layer from a room conn graph.  This is used to unload a layer
* from a binned conn graph.
* PARAMS:
*   layer - ZoneMapLayer to unload
******/
void roomConnGraphUnloadLayer(ZoneMapLayer *layer)
{
	WorldRegion *region = layerGetWorldRegion(layer);
	int i;

	if (region && region->room_conn_graph)
	{
		for (i = eaSize(&region->room_conn_graph->rooms) - 1; i >= 0; i--)
		{
			Room *room = region->room_conn_graph->rooms[i];
			if (room->layer == layer)
				roomConnGraphRemoveRoom(region->room_conn_graph, room);
		}
		for (i = eaSize(&region->room_conn_graph->outdoor_portals) - 1; i >= 0; i--)
		{
			RoomPortal *portal = region->room_conn_graph->outdoor_portals[i];
			if (portal->layer == layer)
				roomConnGraphRemoveOutdoorPortal(region->room_conn_graph, portal);
		}
	}
}

static int getLayerIdx(const ZoneMapLayer *layer, const char **layer_names)
{
	const char *filename = layerGetFilename(layer);
	int i;

	for (i = eaSize(&layer_names) - 1; i >= 0; --i)
	{
		if (stricmp(layer_names[i], filename)==0)
			return i;
	}

	return 0;
}

/********************
* PARSED CONVERSION
********************/
/******
* This function converts a graph to a server parsable format.
* PARAMS:
*   graph - RoomConnGraph to convert
* RETURNS:
*   RoomConnGraphServerParsed converted from graph
******/
RoomConnGraphServerParsed *roomConnGraphGetServerParsed(RoomConnGraph *graph, const char **layer_names)
{
	RoomConnGraphServerParsed *graph_parsed = StructCreate(parse_RoomConnGraphServerParsed);
	int node_id = 1;
	int i, j;

	// populate node ID's to deal with pointer references
	for (i = 0; i < eaSize(&graph->rooms); i++)
	{
		for (j = 0; j < eaSize(&graph->rooms[i]->portals); j++)
			graph->rooms[i]->portals[j]->node_id = node_id++;
	}
	for (i = 0; i < eaSize(&graph->outdoor_portals); i++)
		graph->outdoor_portals[i]->node_id = node_id++;

	// convert all data into parseable structure
	for (i = 0; i < eaSize(&graph->rooms); i++)
	{
		RoomServerParsed *room_parsed = StructCreate(parse_RoomServerParsed);
		Room *room = graph->rooms[i];
		U32 bit = 1;

		StructCopy(parse_GroupVolumePropertiesServer, &room->server_volume, &room_parsed->server_volume, 0, 0, 0);

		for (j = 0; j < 32; ++j)
		{
			if (room->volume_type_bits & bit)
			{
				const char *s = wlVolumeBitMaskToTypeName(bit);
				if (s)
					eaPush(&room_parsed->volume_type_strings, s);
			}
			eaQSortG(room_parsed->volume_type_strings, strCmp);
			bit = bit << 1;
		}

		eaPush(&graph_parsed->rooms, room_parsed);
		if (room->layer)
			room_parsed->layer_idx = getLayerIdx(room->layer, layer_names);
		if (room->def_name)
			room_parsed->def_name = StructAllocString(room->def_name);
		copyVec3(room->bounds_min, room_parsed->bounds_min);
		copyVec3(room->bounds_max, room_parsed->bounds_max);
		copyVec3(room->bounds_mid, room_parsed->bounds_mid);

		// create portals
		for (j = 0; j < eaSize(&room->portals); j++)
		{
			RoomPortalParsed *portal_parsed = StructCreate(parse_RoomPortalParsed);
			RoomPortal *portal = room->portals[j];

			eaPush(&room_parsed->portals, portal_parsed);
			if (portal->layer)
				portal_parsed->layer_idx = getLayerIdx(portal->layer, layer_names);
			if (portal->def_name)
				portal_parsed->def_name = StructAllocString(portal->def_name);
			portal_parsed->portal_id = portal->node_id;
			if (portal->neighbor)
				portal_parsed->neighbor_id = portal->neighbor->node_id;
			else
				portal_parsed->neighbor_id = -1;
			copyVec3(portal->bounds_min, portal_parsed->bounds_min);
			copyVec3(portal->bounds_max, portal_parsed->bounds_max);
			copyMat4(portal->world_mat, portal_parsed->world_mat);

			// external portal data
			portal_parsed->sound_conn_props = StructClone(parse_WorldSoundConnProperties, portal->sound_conn_props);
		}

		// create partitions
		for (j = 0; j < eaSize(&room->partitions); j++)
		{
			RoomPartitionParsed *partition_parsed = StructCreate(parse_RoomPartitionParsed);
			RoomPartition *partition = room->partitions[j];

			eaPush(&room_parsed->partitions, partition_parsed);
			partition_parsed->zmap_scope_name = StructAllocString(partition->zmap_scope_name);
			copyVec3(partition->bounds_min, partition_parsed->bounds_min);
			copyVec3(partition->bounds_max, partition_parsed->bounds_max);
			copyVec3(partition->bounds_mid, partition_parsed->bounds_mid);
			partition_parsed->gmesh = gmeshToParsedFormat(partition->mesh);
			partition_parsed->partition_data = StructClone(parse_RoomInstanceData, partition->partition_data);
		}
	}

	// convert outdoor portals
	for (i = 0; i < eaSize(&graph->outdoor_portals); i++)
	{
		RoomPortalParsed *portal_parsed = StructCreate(parse_RoomPortalParsed);
		RoomPortal *portal = graph->outdoor_portals[i];

		eaPush(&graph_parsed->outdoor_portals, portal_parsed);
		if (portal->layer)
			portal_parsed->layer_idx = getLayerIdx(portal->layer, layer_names);
		if (portal->def_name)
			portal_parsed->def_name = StructAllocString(portal->def_name);
		portal_parsed->portal_id = portal->node_id;
		if (portal->neighbor)
			portal_parsed->neighbor_id = portal->neighbor->node_id;
		else
			portal_parsed->neighbor_id = -1;
		copyVec3(portal->bounds_min, portal_parsed->bounds_min);
		copyVec3(portal->bounds_max, portal_parsed->bounds_max);
		copyMat4(portal->world_mat, portal_parsed->world_mat);

		// external portal data
		portal_parsed->sound_conn_props = StructClone(parse_WorldSoundConnProperties, portal->sound_conn_props);
	}

	return graph_parsed;
}

/******
* This function converts a graph to a client parsable format.
* PARAMS:
*   graph - RoomConnGraph to convert
* RETURNS:
*   RoomConnGraphClientParsed converted from graph
******/
RoomConnGraphClientParsed *roomConnGraphGetClientParsed(RoomConnGraph *graph, const char **layer_names)
{
	RoomConnGraphClientParsed *graph_parsed = StructCreate(parse_RoomConnGraphClientParsed);
	int node_id = 1;
	int i, j;

	// populate node ID's to deal with pointer references
	for (i = 0; i < eaSize(&graph->rooms); i++)
	{
		for (j = 0; j < eaSize(&graph->rooms[i]->portals); j++)
			graph->rooms[i]->portals[j]->node_id = node_id++;
	}
	for (i = 0; i < eaSize(&graph->outdoor_portals); i++)
		graph->outdoor_portals[i]->node_id = node_id++;

	// convert all data into parseable structure
	for (i = 0; i < eaSize(&graph->rooms); i++)
	{
		RoomClientParsed *room_parsed = StructCreate(parse_RoomClientParsed);
		Room *room = graph->rooms[i];
		U32 bit = 1;

		StructCopy(parse_GroupVolumePropertiesClient, &room->client_volume, &room_parsed->client_volume, 0, 0, 0);

		room_parsed->limit_contained_lights_to_room = room->limit_contained_lights_to_room;

		for (j = 0; j < 32; ++j)
		{
			if (room->volume_type_bits & bit)
			{
				const char *s = wlVolumeBitMaskToTypeName(bit);
				if (s)
					eaPush(&room_parsed->volume_type_strings, s);
			}
			eaQSortG(room_parsed->volume_type_strings, strCmp);
			bit = bit << 1;
		}

		eaPush(&graph_parsed->rooms, room_parsed);
		if (room->layer)
			room_parsed->layer_idx = getLayerIdx(room->layer, layer_names);
		if (room->def_name)
			room_parsed->def_name = StructAllocString(room->def_name);
		copyVec3(room->bounds_min, room_parsed->bounds_min);
		copyVec3(room->bounds_max, room_parsed->bounds_max);
		copyVec3(room->bounds_mid, room_parsed->bounds_mid);

		// create portals
		for (j = 0; j < eaSize(&room->portals); j++)
		{
			RoomPortalParsed *portal_parsed = StructCreate(parse_RoomPortalParsed);
			RoomPortal *portal = room->portals[j];

			eaPush(&room_parsed->portals, portal_parsed);
			if (portal->layer)
				portal_parsed->layer_idx = getLayerIdx(portal->layer, layer_names);
			if (portal->def_name)
				portal_parsed->def_name = StructAllocString(portal->def_name);
			portal_parsed->portal_id = portal->node_id;
			if (portal->neighbor)
				portal_parsed->neighbor_id = portal->neighbor->node_id;
			else
				portal_parsed->neighbor_id = -1;
			copyVec3(portal->bounds_min, portal_parsed->bounds_min);
			copyVec3(portal->bounds_max, portal_parsed->bounds_max);
			copyMat4(portal->world_mat, portal_parsed->world_mat);

			// external portal data
			portal_parsed->sound_conn_props = StructClone(parse_WorldSoundConnProperties, portal->sound_conn_props);
		}

		// create partitions
		for (j = 0; j < eaSize(&room->partitions); j++)
		{
			RoomPartitionParsed *partition_parsed = StructCreate(parse_RoomPartitionParsed);
			RoomPartition *partition = room->partitions[j];

			eaPush(&room_parsed->partitions, partition_parsed);
			partition_parsed->zmap_scope_name = StructAllocString(partition->zmap_scope_name);
			copyVec3(partition->bounds_min, partition_parsed->bounds_min);
			copyVec3(partition->bounds_max, partition_parsed->bounds_max);
			copyVec3(partition->bounds_mid, partition_parsed->bounds_mid);
			partition_parsed->gmesh = gmeshToParsedFormat(partition->mesh);
			partition_parsed->partition_data = StructClone(parse_RoomInstanceData, partition->partition_data);
		}
	}

	// convert outdoor portals
	for (i = 0; i < eaSize(&graph->outdoor_portals); i++)
	{
		RoomPortalParsed *portal_parsed = StructCreate(parse_RoomPortalParsed);
		RoomPortal *portal = graph->outdoor_portals[i];

		eaPush(&graph_parsed->outdoor_portals, portal_parsed);
		if (portal->layer)
			portal_parsed->layer_idx = getLayerIdx(portal->layer, layer_names);
		if (portal->def_name)
			portal_parsed->def_name = StructAllocString(portal->def_name);
		portal_parsed->portal_id = portal->node_id;
		if (portal->neighbor)
			portal_parsed->neighbor_id = portal->neighbor->node_id;
		else
			portal_parsed->neighbor_id = -1;
		copyVec3(portal->bounds_min, portal_parsed->bounds_min);
		copyVec3(portal->bounds_max, portal_parsed->bounds_max);
		copyMat4(portal->world_mat, portal_parsed->world_mat);

		// external portal data
		portal_parsed->sound_conn_props = StructClone(parse_WorldSoundConnProperties, portal->sound_conn_props);
	}

	return graph_parsed;
}

/******
* This function converts a parsed graph back to a usable form
* PARAMS:
*   graph_parsed - RoomConnGraphParsed to convert
* RETURNS:
*   RoomConnGraph converted back from parsed format
******/
RoomConnGraph *roomConnGraphFromServerParsed(RoomConnGraphServerParsed *graph_parsed, const char **layer_names)
{
	RoomConnGraph *graph = calloc(1, sizeof(*graph));
	StashTable portal_id_stash = stashTableCreateInt(16);
	int i, j, k;
	char base_dir[MAX_PATH];

	worldGetClientBaseDir(zmapGetFilename(NULL), SAFESTR(base_dir));

	// create the rooms
	for (i = 0; i < eaSize(&graph_parsed->rooms); i++)
	{
		RoomServerParsed *room_parsed = graph_parsed->rooms[i];
		Room *room = calloc(1, sizeof(*room));

		StructCopy(parse_GroupVolumePropertiesServer, &room_parsed->server_volume, &room->server_volume, 0, 0, 0);

		for (j = 0; j < eaSize(&room_parsed->volume_type_strings); ++j)
			room->volume_type_bits |= wlVolumeTypeNameToBitMask(room_parsed->volume_type_strings[j]);

		FOR_EACH_IN_EARRAY(world_grid.maps, ZoneMap, map)
		{
			room->layer = zmapGetLayerByName(map, layer_names[room_parsed->layer_idx]);
			if (room->layer)
				break;
		}
		FOR_EACH_END;
		assert(room->layer);
		if (room_parsed->def_name)
			room->def_name = strdup(room_parsed->def_name);
		eaPush(&graph->rooms, room);
		copyVec3(room_parsed->bounds_min, room->bounds_min);
		copyVec3(room_parsed->bounds_max, room->bounds_max);
		copyVec3(room_parsed->bounds_mid, room->bounds_mid);
		room->dirty = 0;
		room->is_occluder = 0;
		room->double_sided_occluder = 0;
		room->parent_graph = graph;

		// create the partitions
		for (j = 0; j < eaSize(&room_parsed->partitions); j++)
		{
			char texture_name[MAX_PATH];
			RoomPartitionParsed *partition_parsed = room_parsed->partitions[j];
			RoomPartition *partition = calloc(1, sizeof(*partition));

			eaPush(&room->partitions, partition);
			partition->zmap_scope_name = strdup(partition_parsed->zmap_scope_name);
			if (room->layer->reserved_unique_names)
				stashAddInt(room->layer->reserved_unique_names, partition_parsed->zmap_scope_name, -1, true);
			copyVec3(partition_parsed->bounds_min, partition->bounds_min);
			copyVec3(partition_parsed->bounds_max, partition->bounds_max);
			copyVec3(partition_parsed->bounds_mid, partition->bounds_mid);
			partition->parent_room = room;
			partition->partition_data = StructClone(parse_RoomInstanceData, partition_parsed->partition_data);

			if(wl_state.gfx_map_photo_register)
			{
				for( k=0; k < eaSize(&partition_parsed->mapSnapData.image_name_list); k++ )
				{
					sprintf(texture_name, "#%s/map_snap.hogg#%s", base_dir, partition_parsed->mapSnapData.image_name_list[k]);
					eaPush(&partition->tex_list, wl_state.gfx_map_photo_register(texture_name));
				}
				if(partition_parsed->mapSnapData.overview_image_name)
				{
					sprintf(texture_name, "#%s/map_snap.hogg#%s", base_dir, partition_parsed->mapSnapData.overview_image_name);
					partition->overview_tex = wl_state.gfx_map_photo_register(texture_name);
				}
			}
			eaCopy(&partition->mapSnapData.image_name_list, &partition_parsed->mapSnapData.image_name_list);
			partition->mapSnapData.overview_image_name = partition_parsed->mapSnapData.overview_image_name;
			partition->mapSnapData.image_width = partition_parsed->mapSnapData.image_width;
			partition->mapSnapData.image_height = partition_parsed->mapSnapData.image_height;
			copyVec2(partition_parsed->mapSnapData.vMin,partition->mapSnapData.vMin);
			copyVec2(partition_parsed->mapSnapData.vMax,partition->mapSnapData.vMax);

			// create the gpset and convex hull
			partition->mesh = gmeshFromParsedFormat(partition_parsed->gmesh);
			if (partition->mesh)
				partition->hull = gmeshToGConvexHull(partition->mesh);
		}

		// create the portals
		for (j = 0; j < eaSize(&room_parsed->portals); j++)
		{
			RoomPortalParsed *portal_parsed = room_parsed->portals[j];
			RoomPortal *portal = roomAddPortal(room, portal_parsed->bounds_min, portal_parsed->bounds_max, portal_parsed->world_mat);

			FOR_EACH_IN_EARRAY(world_grid.maps, ZoneMap, map)
			{
				portal->layer = zmapGetLayerByName(map, layer_names[portal_parsed->layer_idx]);
				if (portal->layer)
					break;
			}
			FOR_EACH_END;
			if (portal_parsed->def_name)
				portal->def_name = strdup(portal_parsed->def_name);
			stashIntAddPointer(portal_id_stash, portal_parsed->portal_id, portal, false);

			// external portal data
			portal->sound_conn_props = StructClone(parse_WorldSoundConnProperties, portal_parsed->sound_conn_props);
		}
	}

	// create the outdoor portals
	for (i = 0; i < eaSize(&graph_parsed->outdoor_portals); i++)
	{
		RoomPortalParsed *portal_parsed = graph_parsed->outdoor_portals[i];
		RoomPortal *portal = roomConnGraphAddOutdoorPortal(graph, portal_parsed->bounds_min, portal_parsed->bounds_max, portal_parsed->world_mat);
		
		FOR_EACH_IN_EARRAY(world_grid.maps, ZoneMap, map)
		{
			portal->layer = zmapGetLayerByName(NULL, layer_names[portal_parsed->layer_idx]);
			if (portal->layer)
				break;
		}
		FOR_EACH_END;
		if (portal_parsed->def_name)
			portal->def_name = strdup(portal_parsed->def_name);
		stashIntAddPointer(portal_id_stash, portal_parsed->portal_id, portal, false);

		// external portal data
		portal->sound_conn_props = StructClone(parse_WorldSoundConnProperties, portal_parsed->sound_conn_props);
	}

	// create graph connections
	for (i = 0; i < eaSize(&graph_parsed->rooms); i++)
	{
		RoomServerParsed *room_parsed = graph_parsed->rooms[i];

		for (j = 0; j < eaSize(&room_parsed->portals); j++)
		{
			RoomPortalParsed *portal_parsed = room_parsed->portals[j];
			RoomPortal *portal, *neighbor;
		
			if (!stashIntFindPointer(portal_id_stash, portal_parsed->portal_id, &portal))
				continue;
			if (portal->neighbor)
				continue;
			if (!stashIntFindPointer(portal_id_stash, portal_parsed->neighbor_id, &neighbor))
				continue;

			portal->neighbor = neighbor;
			neighbor->neighbor = portal;
		}
	}

	graph->dirty = 0;

	// cleanup
	stashTableDestroy(portal_id_stash);

	// call update callbacks
	roomConnGraphUpdateCallbacks(graph, false);

	return graph;
}

/******
* This function converts a client parsed graph back to a usable form
* PARAMS:
*   graph_parsed - RoomConnGraphClientParsed to convert
* RETURNS:
*   RoomConnGraph converted back from parsed format
******/
RoomConnGraph *roomConnGraphFromClientParsed(RoomConnGraphClientParsed *graph_parsed, const char **layer_names)
{
	RoomConnGraph *graph = calloc(1, sizeof(*graph));
	StashTable portal_id_stash = stashTableCreateInt(16);
	int i, j, k;

	// create the rooms
	for (i = 0; i < eaSize(&graph_parsed->rooms); i++)
	{
		RoomClientParsed *room_parsed = graph_parsed->rooms[i];
		Room *room = calloc(1, sizeof(*room));
		ZoneMap *zmap = NULL;

		StructCopy(parse_GroupVolumePropertiesClient, &room_parsed->client_volume, &room->client_volume, 0, 0, 0);

		for (j = 0; j < eaSize(&room_parsed->volume_type_strings); ++j)
			room->volume_type_bits |= wlVolumeTypeNameToBitMask(room_parsed->volume_type_strings[j]);

		FOR_EACH_IN_EARRAY(world_grid.maps, ZoneMap, map)
		{
			room->layer = zmapGetLayerByName(map, layer_names[room_parsed->layer_idx]);
			if (room->layer)
			{
				zmap = map;
				break;
			}
		}
		FOR_EACH_END;
		assert(room->layer);
		if (room_parsed->def_name)
			room->def_name = strdup(room_parsed->def_name);
		eaPush(&graph->rooms, room);
		copyVec3(room_parsed->bounds_min, room->bounds_min);
		copyVec3(room_parsed->bounds_max, room->bounds_max);
		copyVec3(room_parsed->bounds_mid, room->bounds_mid);
		room->dirty = 0;
		room->is_occluder = 0;
		room->double_sided_occluder = 0;
		room->limit_contained_lights_to_room = room_parsed->limit_contained_lights_to_room;
		room->parent_graph = graph;

		// create the partitions
		for (j = 0; j < eaSize(&room_parsed->partitions); j++)
		{
			char texture_name[MAX_PATH];
			RoomPartitionParsed *partition_parsed = room_parsed->partitions[j];
			RoomPartition *partition = calloc(1, sizeof(*partition));

			eaPush(&room->partitions, partition);
			partition->zmap_scope_name = strdup(partition_parsed->zmap_scope_name);
			if (room->layer->reserved_unique_names)
				stashAddInt(room->layer->reserved_unique_names, partition_parsed->zmap_scope_name, -1, true);
			copyVec3(partition_parsed->bounds_min, partition->bounds_min);
			copyVec3(partition_parsed->bounds_max, partition->bounds_max);
			copyVec3(partition_parsed->bounds_mid, partition->bounds_mid);
			partition->parent_room = room;
			partition->partition_data = StructClone(parse_RoomInstanceData, partition_parsed->partition_data);

			if(wl_state.gfx_map_photo_register)
			{
				const char *zmfilename = zmapGetFilename(zmap);
				char prefix[CRYPTIC_MAX_PATH], base_dir[MAX_PATH];
				worldGetClientBaseDir(zmfilename, SAFESTR(base_dir));
				sprintf(prefix, "#%s", base_dir);
				for( k=0; k < eaSize(&partition_parsed->mapSnapData.image_name_list); k++ )
				{
					sprintf(texture_name, "%s/map_snap.hogg#%s", prefix, partition_parsed->mapSnapData.image_name_list[k]);
					eaPush(&partition->tex_list, wl_state.gfx_map_photo_register(texture_name));
				}
				if(partition_parsed->mapSnapData.overview_image_name)
				{
					sprintf(texture_name, "%s/map_snap.hogg#%s", prefix, partition_parsed->mapSnapData.overview_image_name);
					partition->overview_tex = wl_state.gfx_map_photo_register(texture_name);
				}
			}
			eaCopy(&partition->mapSnapData.image_name_list, &partition_parsed->mapSnapData.image_name_list);
			partition->mapSnapData.overview_image_name = partition_parsed->mapSnapData.overview_image_name;
			partition->mapSnapData.image_width = partition_parsed->mapSnapData.image_width;
			partition->mapSnapData.image_height = partition_parsed->mapSnapData.image_height;
			copyVec2(partition_parsed->mapSnapData.vMin,partition->mapSnapData.vMin);
			copyVec2(partition_parsed->mapSnapData.vMax,partition->mapSnapData.vMax);

			// create the gpset and convex hull
			partition->mesh = gmeshFromParsedFormat(partition_parsed->gmesh);
			if (partition->mesh)
				partition->hull = gmeshToGConvexHull(partition->mesh);
		}

		// create the portals
		for (j = 0; j < eaSize(&room_parsed->portals); j++)
		{
			RoomPortalParsed *portal_parsed = room_parsed->portals[j];
			RoomPortal *portal = roomAddPortal(room, portal_parsed->bounds_min, portal_parsed->bounds_max, portal_parsed->world_mat);

			FOR_EACH_IN_EARRAY(world_grid.maps, ZoneMap, map)
			{
				portal->layer = zmapGetLayerByName(map, layer_names[portal_parsed->layer_idx]);
				if (portal->layer)
					break;
			}
			FOR_EACH_END;
			if (portal_parsed->def_name)
				portal->def_name = strdup(portal_parsed->def_name);
			stashIntAddPointer(portal_id_stash, portal_parsed->portal_id, portal, false);

			// external portal data
			portal->sound_conn_props = StructClone(parse_WorldSoundConnProperties, portal_parsed->sound_conn_props);
		}
	}

	// create the outdoor portals
	for (i = 0; i < eaSize(&graph_parsed->outdoor_portals); i++)
	{
		RoomPortalParsed *portal_parsed = graph_parsed->outdoor_portals[i];
		RoomPortal *portal = roomConnGraphAddOutdoorPortal(graph, portal_parsed->bounds_min, portal_parsed->bounds_max, portal_parsed->world_mat);

		FOR_EACH_IN_EARRAY(world_grid.maps, ZoneMap, map)
		{
			portal->layer = zmapGetLayerByName(map, layer_names[portal_parsed->layer_idx]);
			if (portal->layer)
				break;
		}
		FOR_EACH_END;
		if (portal_parsed->def_name)
			portal->def_name = strdup(portal_parsed->def_name);
		stashIntAddPointer(portal_id_stash, portal_parsed->portal_id, portal, false);

		// external portal data
		portal->sound_conn_props = StructClone(parse_WorldSoundConnProperties, portal_parsed->sound_conn_props);
	}

	// create graph connections
	for (i = 0; i < eaSize(&graph_parsed->rooms); i++)
	{
		RoomClientParsed *room_parsed = graph_parsed->rooms[i];

		for (j = 0; j < eaSize(&room_parsed->portals); j++)
		{
			RoomPortalParsed *portal_parsed = room_parsed->portals[j];
			RoomPortal *portal, *neighbor;

			if (!stashIntFindPointer(portal_id_stash, portal_parsed->portal_id, &portal))
				continue;
			if (portal->neighbor)
				continue;
			if (!stashIntFindPointer(portal_id_stash, portal_parsed->neighbor_id, &neighbor))
				continue;

			portal->neighbor = neighbor;
			neighbor->neighbor = portal;
		}
	}

	graph->dirty = 0;

	// cleanup
	stashTableDestroy(portal_id_stash);

	// call update callbacks
	roomConnGraphUpdateCallbacks(graph, false);

	return graph;
}


/********************
* HELPER FUNCTIONS
********************/
void roomGetNeighbors(Room *room, Room ***neighbors)
{
	int i;
	for (i = 0; i < eaSize(&room->portals); i++)
	{
		RoomPortal *portal = room->portals[i];
		if (portal->neighbor)
			eaPushUnique(neighbors, portal->neighbor->parent_room);
	}
}

#include "RoomConnPrivate_h_ast.c"
