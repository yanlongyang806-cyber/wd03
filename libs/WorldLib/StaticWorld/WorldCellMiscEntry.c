/***************************************************************************



***************************************************************************/

#include "WorldCellEntryPrivate.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldCell.h"
#include "wlState.h"
#include "wlVolumes.h"
#include "PhysicsSDK.h"
#include "wlBeacon.h"
#include "wlEncounter.h"
#include "RoomConn.h"
#include "dynFxInfo.h"
#include "dynWind.h"
#include "bounds.h"

#include "error.h"
#include "StringCache.h"
#include "ScratchStack.h"
#include "rgb_hsv.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

static WorldVolumeGameFunc game_open_callback;
static WorldVolumeGameFunc game_close_callback;

//////////////////////////////////////////////////////////////////////////
// Volume

#if PSDK_DISABLED
typedef struct PSDKMeshDesc PSDKMeshDesc;
#endif

SA_RET_OP_VALID static void *volumeGetVertsTris(WorldVolumeEntry *entry, PSDKMeshDesc *meshDesc)
{
#if !PSDK_DISABLED
	int i;
	int *tris;
	Vec3 *positions;
	U8 * pMemToFree = NULL;
	GMesh * pMesh;
	int iTriMemSize,iVertSize;

	if (!eaSize(&entry->elements))
		return NULL;

	pMesh = worldVolumeCalculateUnionMesh(entry);

	if(!pMesh)
		return NULL;

	iTriMemSize = pMesh->tri_count * 3 * sizeof(int);
	iVertSize = pMesh->vert_count * sizeof(*pMesh->positions);

	// this isn't the most efficient possible way to get the data passed around and minimize copies, but it's the easiest right now.
	tris = (int *)ScratchAlloc(iTriMemSize + iVertSize);
	pMemToFree = (U8 *)tris;
	positions = (Vec3 *)(pMemToFree + iTriMemSize);
	memcpy(positions,pMesh->positions,iVertSize);

	meshDesc->vertArray = positions;
	meshDesc->vertCount = pMesh->vert_count;
	meshDesc->triArray = tris;
	meshDesc->triCount = pMesh->tri_count;
	for (i = 0; i < pMesh->tri_count; ++i)
	{
		tris[i*3+0] = pMesh->tris[i].idx[0];
		tris[i*3+1] = pMesh->tris[i].idx[1];
		tris[i*3+2] = pMesh->tris[i].idx[2];
	}

	gmeshFreeData(pMesh);
	free(pMesh);

	return pMemToFree;

#endif
	return NULL;
}

static PSDKCookedMesh* volumeCookMesh(WorldVolumeEntry *entry)
{
#if !PSDK_DISABLED
	char			buffer[200];
	PSDKMeshDesc	meshDesc = {0};
	PSDKCookedMesh  *mesh = NULL;

	if(!entry->mesh)
	{
		void *memToFree = volumeGetVertsTris(entry, &meshDesc);
		
		strcpy(buffer, "PlayableVolume");
		meshDesc.name = buffer;

		if (meshDesc.vertArray)
			psdkCookedMeshCreate(&entry->mesh, &meshDesc);

		if (memToFree)
			ScratchFree(memToFree);
	}

	return entry->mesh;
#else
	return NULL;
#endif
}

void volumeCollObjectMsgHandler(const WorldCollObjectMsg* msg)
{
	WorldVolumeEntry *entry = msg->userPointer;

	switch(msg->msgType)
	{
		xcase WCO_MSG_GET_DEBUG_STRING:	{
			WorldVolume *pVolume = NULL;
			Vec3 min, max;
			int i;

			// Pick any volume since all should be same size
			for(i=eaSize(&entry->eaVolumes)-1; i>=0; --i) {
				pVolume = entry->eaVolumes[i];
				if (pVolume) {
					break;
				}
			}
			if (pVolume) {
				wlVolumeGetWorldMinMax(pVolume, min, max);
				snprintf_s(	msg->in.getDebugString.buffer,
					msg->in.getDebugString.bufferLen,
					"Volume: Min: %.2f %.2f %.2f - Max: %.2f %.2f %.2f",
					vecParamsXYZ(min), vecParamsXYZ(max));
			}
		}

		xcase WCO_MSG_DESTROYED: {
			// There was logic here to go find the entry and clear the collision
			// field, but this usually gets called after the memory is freed, so
			// doing anything here is at best useless and often causes memory corruption.
		}

		xcase WCO_MSG_GET_SHAPE:
		{
			WorldCollObjectMsgGetShapeOut*	getShape = msg->out.getShape;
			WorldCollObjectMsgGetShapeOutInst* shapeInst;

			wcoAddShapeInstance(getShape, &shapeInst);
			shapeInst->mesh = volumeCookMesh(entry);
			shapeInst->filter.filterBits = 	WC_FILTER_BIT_PLAYABLEVOLUMEGEO |
											(	WC_FILTER_BITS_WORLD_STANDARD &
												~(	WC_FILTER_BIT_FX_SPLAT |
													WC_FILTER_BIT_CAMERA_BLOCKING));
			shapeInst->filter.shapeGroup = WC_SHAPEGROUP_WORLD_BASIC;

			copyMat4(unitmat, msg->out.getShape->mat);
		}

		xcase WCO_MSG_GET_MODEL_DATA:
		{
			WorldCollStoredModelData*		smd = NULL;
			WorldCollModelInstanceData*		inst = NULL;
			WorldCollStoredModelDataDesc	desc = {0};
			char							name[MAX_PATH];

			if(entry)
			{
				// give it a name here
				sprintf(name, "PlayableVolumeMin(%.2f%.2f%.2f)Max(%.2f%.2f%.2f)",
					vecParamsXYZ(entry->base_entry.shared_bounds->local_min), 
					vecParamsXYZ(entry->base_entry.shared_bounds->local_max));

				inst = callocStruct(WorldCollModelInstanceData);
				inst->noGroundConnections = 1;

				if(!wcStoredModelDataFind(name, &smd))
				{
#if !PSDK_DISABLED
					PSDKMeshDesc	meshDesc = {0};

					void *memToFree = volumeGetVertsTris(entry, &meshDesc);

					desc.verts = meshDesc.vertArray;
					desc.vert_count = meshDesc.vertCount;
					desc.tris = meshDesc.triArray;
					desc.tri_count = meshDesc.triCount;
					mulBoundsAA(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max, 
						entry->base_entry.bounds.world_matrix, desc.min, desc.max);

					if (meshDesc.vertArray)
						wcStoredModelDataCreate(name, name, &desc, &smd);

					if (memToFree)
						ScratchFree(memToFree);

					if(!meshDesc.vertArray) // Failed to create the sucker
					{
						free( inst );
						break;
					}
#endif
				}

				assert(smd);

				copyMat4(unitmat, inst->world_mat);


				msg->out.getModelData->modelData = smd;
				msg->out.getModelData->instData = inst;
			}
		}
	}
}

static void worldVolumeEntryUpdateBounds(WorldVolumeEntry *entry)
{
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	Mat4 inv_world_matrix;
	Vec3 local_mid, local_min, local_max;
	F32 radius, vis_dist;
	int i;
	ZoneMap *zmap;

	removeSharedBoundsRef(entry->base_entry.shared_bounds);
	entry->base_entry.shared_bounds = NULL;

	invertMat4Copy(bounds->world_matrix, inv_world_matrix);

	// update entry bounds
	setVec3same(local_min, 8e16);
	setVec3same(local_max, -8e16);
	for (i = 0; i < eaSize(&entry->elements); ++i)
	{
		Vec3 elem_local_min, elem_local_max;
		Mat4 local_to_local;

		mulMat4(inv_world_matrix, entry->elements[i]->world_mat, local_to_local);

		if (entry->elements[i]->volume_shape == WL_VOLUME_SPHERE)
		{
			addVec3same(local_to_local[3], -entry->elements[i]->radius, elem_local_min);
			addVec3same(local_to_local[3], entry->elements[i]->radius, elem_local_max);
		}
		else
		{
			mulBoundsAA(entry->elements[i]->local_min, entry->elements[i]->local_max, local_to_local, elem_local_min, elem_local_max);
		}

		vec3RunningMin(elem_local_min, local_min);
		vec3RunningMax(elem_local_max, local_max);
	}

	if (local_min[0] > local_max[0] || local_min[1] > local_max[1] || local_min[2] > local_max[2])
	{
		setVec3same(local_min, 0);
		setVec3same(local_max, 0);
	}

	radius = boxCalcMid(local_min, local_max, local_mid);
	mulVecMat4(local_mid, bounds->world_matrix, bounds->world_mid);
	vis_dist = radius * WORLD_VOLUME_VISDIST_MULTIPLIER;

	if (entry->room && entry->room->layer && entry->room->layer->zmap_parent)
		zmap = entry->room->layer->zmap_parent;
	else
		zmap = worldGetActiveMap();
	entry->base_entry.shared_bounds = createSharedBounds(zmap, NULL, local_min, local_max, radius, 0, vis_dist, vis_dist);
}

WorldVolumeElement *worldVolumeEntryAddSubVolume(WorldVolumeEntry *entry, GroupDef *def, const GroupInfo *info)
{
	WorldVolumeElement *element = StructCreate(parse_WorldVolumeElement);
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	GroupVolumeProperties *props = def->property_structs.volume;
	eaPush(&entry->elements, element);

	copyMat4(info->world_matrix, element->world_mat);

	if (props && props->eShape == GVS_Sphere)
	{
		element->volume_shape = WL_VOLUME_SPHERE;
		element->radius = props->fSphereRadius;
	}
	else
	{
		element->volume_shape = WL_VOLUME_BOX;
		if(props) {
			copyVec3(props->vBoxMin, element->local_min);
			copyVec3(props->vBoxMax, element->local_max);
		} else {
			setVec3same(element->local_min, 0);
			setVec3same(element->local_max, 0);
		}

		if (entry->occluder)
			element->face_bits = def->property_structs.physical_properties.iOccluderFaces;
	}

	worldVolumeEntryUpdateBounds(entry);

	return element;
}

WorldVolumeEntry *createWorldVolumeEntry(GroupDef *def, const GroupInfo *info, U32 volume_type_bits)
{
	WorldVolumeEntry *entry = calloc(1, sizeof(WorldVolumeEntry));
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;

	entry->base_entry.type = WCENT_VOLUME;
	entry->indoor = !!def->property_structs.client_volume.indoor_volume_properties;
	if (groupIsVolumeType(def, "Occluder"))
		entry->occluder = 1;

	entry->volume_type_bits = volume_type_bits;
	copyMat4(info->world_matrix, bounds->world_matrix);

	worldVolumeEntryAddSubVolume(entry, def, info);

	if (wlIsServer())
		StructCopy(parse_GroupVolumePropertiesServer, &def->property_structs.server_volume, &entry->server_volume, 0, 0, 0);
	else
	{
		StructCopy(parse_GroupVolumePropertiesClient, &def->property_structs.client_volume, &entry->client_volume, 0, 0, 0);

		if(def->property_structs.client_volume.water_volume_properties && def->property_structs.client_volume.water_volume_properties->water_cond)
		{
			entry->fx_condition = getWorldFXCondition(info->zmap, def->property_structs.client_volume.water_volume_properties->water_cond, /*fx_entry=*/NULL, /*water_entry=*/entry);
			entry->fx_condition_state = entry->fx_condition->state;
		}
		else
		{
			entry->fx_condition = NULL;
			entry->fx_condition_state = 1;
		}
	}

	return entry;
}

bool elementHullValid(const WorldVolumeElement *element)
{
	// no hull, or hull is non-empty and not ill-formed, where ill-formed means non-zero plane count but NULL planes
	return !element->hull || (element->hull->count && element->hull->planes);
}

WorldVolumeEntry *createWorldVolumeEntryForRoom(Room *room, WorldVolumeElement **volume_elements)
{
	WorldVolumeEntry *entry = calloc(1, sizeof(WorldVolumeEntry));
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;

	EARRAY_FOREACH_BEGIN(volume_elements, volume_element_index);
	{
		assert(elementHullValid(volume_elements[volume_element_index]));
	}
	EARRAY_FOREACH_END;

	entry->base_entry.type = WCENT_VOLUME;
	entry->indoor = !!room->client_volume.indoor_volume_properties;

	entry->volume_type_bits = room->volume_type_bits;
	copyMat3(unitmat, bounds->world_matrix);
	copyVec3(room->bounds_mid, bounds->world_matrix[3]);

	if (wlIsServer())
		StructCopy(parse_GroupVolumePropertiesServer, &room->server_volume, &entry->server_volume, 0, 0, 0);
	else
		StructCopy(parse_GroupVolumePropertiesClient, &room->client_volume, &entry->client_volume, 0, 0, 0);

	entry->room = room;

	entry->elements = volume_elements;
	worldVolumeEntryUpdateBounds(entry);

	return entry;
}

WorldCellEntry *createWorldVolumeEntryFromServerParsed(ZoneMap *zmap, WorldStreamingInfo *streaming_info, WorldStreamingPooledInfo *streaming_pooled_info, WorldVolumeEntryServerParsed *entry_parsed, bool create_all, bool parsed_will_be_freed)
{
	U32 volume_type_bits = 0;
	WorldVolumeEntry *entry;
	int i;

	for (i = 0; i < eaiSize(&entry_parsed->volume_type_string_idxs); ++i)
	{
		// don't load occluder volumes in streaming mode
		const char *type_string = worldGetStreamedString(streaming_pooled_info, entry_parsed->volume_type_string_idxs[i]);
		if (type_string && (create_all || stricmp(type_string, "Occluder")!=0))
			volume_type_bits |= wlVolumeTypeNameToBitMask(type_string);
	}

	if (!volume_type_bits)
		return NULL;

	entry = calloc(1, sizeof(WorldVolumeEntry));

	entry->base_entry.type = WCENT_VOLUME;

	worldEntryInitBoundsFromParsed(streaming_pooled_info, &entry->base_entry, &entry_parsed->base_data);

	if (parsed_will_be_freed)
	{
		entry->elements = entry_parsed->elements; // transfer ownership
		entry_parsed->elements = NULL;
	}
	else
	{
		eaCopyStructs(&entry_parsed->elements, &entry->elements, parse_WorldVolumeElement);
	}

	StructCopy(parse_GroupVolumePropertiesServer, &entry_parsed->properties, &entry->server_volume, 0, 0, 0);

	entry->indoor = entry_parsed->indoor;
	entry->occluder = entry_parsed->occluder;

	entry->volume_type_bits = volume_type_bits;

	// fixup pointer from named volume to this entry
	if (streaming_info && streaming_info->id_to_named_volume)
	{
		WorldNamedVolume *named_volume;
		if (stashIntFindPointer(streaming_info->id_to_named_volume, entry_parsed->named_volume_id, &named_volume))
			named_volume->entry = entry;
	}

	// fixup volume properties
	worldFixupVolumeInteractions(NULL, entry, &entry->server_volume);

	return &entry->base_entry;
}

WorldCellEntry *createWorldVolumeEntryFromClientParsed(ZoneMap *zmap, WorldStreamingInfo *streaming_info, WorldStreamingPooledInfo *streaming_pooled_info, WorldVolumeEntryClientParsed *entry_parsed, bool create_all, bool parsed_will_be_freed)
{
	U32 volume_type_bits = 0;
	WorldVolumeEntry *entry;
	int i;

	for (i = 0; i < eaiSize(&entry_parsed->volume_type_string_idxs); ++i)
	{
		// don't load occluder volumes in streaming mode
		const char *type_string = worldGetStreamedString(streaming_pooled_info, entry_parsed->volume_type_string_idxs[i]);
		if (type_string && (create_all || stricmp(type_string, "Occluder")!=0))
			volume_type_bits |= wlVolumeTypeNameToBitMask(type_string);
	}

	if (!volume_type_bits)
		return NULL;

	entry = calloc(1, sizeof(WorldVolumeEntry));

	entry->base_entry.type = WCENT_VOLUME;

	worldEntryInitBoundsFromParsed(streaming_pooled_info, &entry->base_entry, &entry_parsed->base_data);

	if (parsed_will_be_freed)
	{
		entry->elements = entry_parsed->elements; // transfer ownership
		entry_parsed->elements = NULL;
	}
	else
	{
		eaCopyStructs(&entry_parsed->elements, &entry->elements, parse_WorldVolumeElement);
	}

	StructCopy(parse_GroupVolumePropertiesClient, &entry_parsed->properties, &entry->client_volume, 0, 0, 0);

	entry->indoor = entry_parsed->indoor;
	entry->occluder = entry_parsed->occluder;

	entry->volume_type_bits = volume_type_bits;

	if(entry->client_volume.water_volume_properties && entry->client_volume.water_volume_properties->water_cond)
	{
		entry->fx_condition = getWorldFXCondition(zmap, entry->client_volume.water_volume_properties->water_cond, /*fx_entry=*/NULL, /*water_entry=*/entry);
		entry->fx_condition_state = entry->fx_condition->state;
	}
	else
	{
		entry->fx_condition = NULL;
		entry->fx_condition_state = 1;
	}

	return &entry->base_entry;
}

void updateWorldIndoorVolumeEntry(WorldVolumeEntry* entry)
{
	if (entry->indoor)
	{
		// update light caches if an indoor volume is opened
		if (wl_state.update_indoor_volume_func)
			wl_state.update_indoor_volume_func(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max, entry->base_entry.bounds.world_matrix, entry->base_entry.streaming_mode);

		// create ambient light for this indoor volume and attach it to the entry
		if (entry->client_volume.indoor_volume_properties && wl_state.update_ambient_light_func)
		{
			Vec3 ambient_rgb;
			hsvToRgb(entry->client_volume.indoor_volume_properties->ambient_hsv, ambient_rgb);
			entry->indoor_ambient_light = wl_state.update_ambient_light_func(entry->indoor_ambient_light, ambient_rgb, zerovec3, zerovec3, zerovec3);
		}
	}
	else if (entry->indoor_ambient_light && wl_state.remove_ambient_light_func)
	{
		wl_state.remove_ambient_light_func(entry->indoor_ambient_light);
		entry->indoor_ambient_light = NULL;
	}

}

void openWorldVolumeEntry(int iPartitionIdx, WorldVolumeEntry *entry)
{
	static U32 playable_volume_type = 0;
	static U32 fx_volume_type = 0;
	static U32 civilian_volume_type = 0;
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	WorldCellEntrySharedBounds *shared_bounds = entry->base_entry.shared_bounds;
	WorldVolume *pVolume;

	pVolume = eaGet(&entry->eaVolumes, iPartitionIdx);
	if (!pVolume)
	{
		pVolume = wlVolumeCreate(iPartitionIdx, entry->volume_type_bits, entry, entry->elements);
		eaSet(&entry->eaVolumes, pVolume, iPartitionIdx);
	}

	if (!playable_volume_type)
		playable_volume_type = wlVolumeTypeNameToBitMask("Playable");
	if (!civilian_volume_type)
		civilian_volume_type = wlVolumeTypeNameToBitMask("Civilian");

	updateWorldIndoorVolumeEntry(entry);

	if (!entry->base_entry.streaming_mode)
	{
		// update vertex lighting if an occluder is opened (not entirely precise because the occluder may not overlap the object, but good enough for the editor)
		if (entry->occluder && wl_state.force_update_light_caches_func)
			wl_state.force_update_light_caches_func(shared_bounds->local_min, shared_bounds->local_max, bounds->world_matrix, true, false, LCIT_STATIC_LIGHTS, NULL);
	}

	if (wlVolumeIsType(pVolume, playable_volume_type))
	{
		Vec3 w_min, w_max;
		
		mulBoundsAA(shared_bounds->local_min, shared_bounds->local_max, 
					bounds->world_matrix, w_min, w_max);

		if (eaGet(&entry->eaCollObjects, iPartitionIdx)) {
			wcoDestroy(&entry->eaCollObjects[iPartitionIdx]);
		}
		eaSet(&entry->eaCollObjects, NULL, iPartitionIdx);

		wcoCreate(	&entry->eaCollObjects[iPartitionIdx],
					worldGetActiveColl(iPartitionIdx),
					volumeCollObjectMsgHandler,
					entry,
					w_min,
					w_max,
					0,
					1);

		if (wl_state.playable_create_func)
			wl_state.playable_create_func(entry);
	}

	if (wlVolumeIsType(pVolume, fx_volume_type) && entry->client_volume.fx_volume_properties)
	{
		if (GET_REF(entry->client_volume.fx_volume_properties->fx_entrance) && !dynFxInfoSelfTerminates(REF_STRING_FROM_HANDLE(entry->client_volume.fx_volume_properties->fx_entrance)))
		{
			ZoneMapLayer* pLayer = worldEntryGetLayer(&entry->base_entry);
			const char* pcFileName = pLayer?layerGetFilename(pLayer):NULL;
			ErrorFilenamef(pcFileName, "FX %s referenced in FX volume does not self terminate. Please use an FX without ContinuingFX set, so that this FX does not stay on forever.", REF_STRING_FROM_HANDLE(entry->client_volume.fx_volume_properties->fx_entrance));
		}
		if (GET_REF(entry->client_volume.fx_volume_properties->fx_exit) && !dynFxInfoSelfTerminates(REF_STRING_FROM_HANDLE(entry->client_volume.fx_volume_properties->fx_exit)))
		{
			ZoneMapLayer* pLayer = worldEntryGetLayer(&entry->base_entry);
			const char* pcFileName = pLayer?layerGetFilename(pLayer):NULL;
			ErrorFilenamef(pcFileName, "FX %s referenced in FX volume does not self terminate. Please use an FX without ContinuingFX set, so that this FX does not stay on forever.", REF_STRING_FROM_HANDLE(entry->client_volume.fx_volume_properties->fx_exit));
		}
	}

	if (entry->server_volume.civilian_volume_properties && wl_state.civvolume_create_func && wlVolumeIsType(pVolume, civilian_volume_type))
		wl_state.civvolume_create_func(entry);

	if (game_open_callback)
		game_open_callback(entry);
}

void closeWorldVolumeEntry(int iPartitionIdx, WorldVolumeEntry *entry)
{
	WorldVolume *pVolume;

	pVolume = eaGet(&entry->eaVolumes, iPartitionIdx);
	if (pVolume)
	{
		wlVolumeFree(pVolume);
		eaSet(&entry->eaVolumes, NULL, iPartitionIdx);

		if (entry->indoor)
		{
			// update light caches if an indoor volume is closed
			if (wl_state.update_indoor_volume_func)
				wl_state.update_indoor_volume_func(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max, entry->base_entry.bounds.world_matrix, entry->base_entry.streaming_mode);

			// free ambient light
			if (entry->indoor_ambient_light && wl_state.remove_ambient_light_func)
				wl_state.remove_ambient_light_func(entry->indoor_ambient_light);
			entry->indoor_ambient_light = NULL;
		}

		if (!entry->base_entry.streaming_mode)
		{
			// update vertex lighting if an occluder is closed (not entirely precise because the occluder may not overlap the object, but good enough for the editor)
			if (entry->occluder && wl_state.force_update_light_caches_func)
				wl_state.force_update_light_caches_func(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max, entry->base_entry.bounds.world_matrix, true, false, LCIT_STATIC_LIGHTS, NULL);
		}
	}

#if !PSDK_DISABLED
	if (entry->mesh)
		psdkCookedMeshDestroy(&entry->mesh);
#endif

	if (eaGet(&entry->eaCollObjects, iPartitionIdx))
	{
		wcoDestroy(&entry->eaCollObjects[iPartitionIdx]);

		if (wl_state.playable_destroy_func)
			wl_state.playable_destroy_func(entry);
	}

	if (entry->server_volume.civilian_volume_properties && wl_state.civvolume_destroy_func)
		wl_state.civvolume_destroy_func(entry);

	if (game_close_callback)
		game_close_callback(entry);
}

void worldVolumeEntrySetGameCallbacks(WorldVolumeGameFunc open_func, WorldVolumeGameFunc close_func)
{
	game_open_callback = open_func;
	game_close_callback = close_func;
}

void setupWorldAnimationEntryDictionary(ZoneMap *zmap)
{
	if (zmap->world_cell_data.animation_entry_dict)
		return;

	sprintf(zmap->world_cell_data.animation_entry_dict_name, "WorldAnimationEntry_%d", eaFind(&world_grid.maps, zmap));
	zmap->world_cell_data.animation_entry_dict = RefSystem_GetDictionaryHandleFromNameOrHandle(zmap->world_cell_data.animation_entry_dict_name);
	if (!zmap->world_cell_data.animation_entry_dict)
		zmap->world_cell_data.animation_entry_dict = RefSystem_RegisterDictionaryWithIntRefData(zmap->world_cell_data.animation_entry_dict_name, NULL, NULL, false);
}

//////////////////////////////////////////////////////////////////////////
// Light

static int getGroupDefParentIndex(GroupDef* parent, GroupDef* child)
{
	int i;

	for (i = 0; i < eaSize(&parent->children); ++i)
	{
		if (parent->children[i]->name_uid == child->name_uid)
			return i;
	}
	return -1;
}

bool validateWorldLightEntry(GroupDef **def_chain, const GroupInfo* info, char** error_message)
{
	int def_chain_size = eaSize(&def_chain);
	GroupDef *def = def_chain_size ? def_chain[def_chain_size-1] : NULL;
	bool is_key = false;
	bool is_valid = true;

	// For vertex lights, validate that they are not inside a group that might attempt to hide it via a child select
	// Since vertex lights are baked in, that will not work in game although it may appear to while editing.
	groupGetLightPropertyBool(NULL, def_chain, def, "LightIsKey", &is_key);
	if (!is_key)
	{
		int i, k, m;
		bool first = true;
		int child_idx = info->parent_entry_child_idx;

		for (i = def_chain_size - 1; is_valid && i >= 0; --i)
		{
			WorldInteractionPropertyEntry** entries;
			int num_entries;

			if (first)
				first = false;
			else
				child_idx = getGroupDefParentIndex(def_chain[i], def);
			def = def_chain[i];
			if (!def->property_structs.interaction_properties)
				continue;

			entries = def->property_structs.interaction_properties->eaEntries;
			num_entries = eaSize(&entries);
			for (k = 0; is_valid && k < num_entries; ++k)
			{
				WorldMoveDescriptorProperties** move_descriptors = 
					(entries[k]->pMotionProperties ? entries[k]->pMotionProperties->eaMoveDescriptors : NULL);
				int num_move_descriptors = eaSize(&move_descriptors);
				for (m = 0; is_valid && m < num_move_descriptors; ++m)
				{
					WorldMoveDescriptorProperties* move_descriptor = move_descriptors[m];
					if (move_descriptor->iStartChildIdx == child_idx || move_descriptor->iDestChildIdx == child_idx)
						is_valid = false;
				}
			}
		}

		if (!is_valid)
		{
			estrConcatf(error_message, "Vertex light is moved by interaction attribute in group %s.", def->name_str);
		}
	}
	return is_valid;
}

WorldLightEntry *createWorldLightEntry(GroupDef **def_chain, const GroupInfo *info)
{
	WorldLightEntry *entry = calloc(1, sizeof(WorldLightEntry));
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	F32 radius, vis_dist;

	entry->base_entry.type = WCENT_LIGHT;

	entry->light_data = groupGetLightData(def_chain, info->world_matrix);
	entry->light_data->region = info->region;

	copyMat4(info->world_matrix, bounds->world_matrix);
	copyVec3(info->world_matrix[3], bounds->world_mid);
	radius = entry->light_data->outer_radius;
	vis_dist = radius * LIGHT_RADIUS_VIS_MULTIPLIER * info->lod_scale;
	MIN1(vis_dist, MAX_LIGHT_DISTANCE);

	if (info->animation_entry)
	{
		setupWorldAnimationEntryDictionary(info->zmap);
		SET_HANDLE_FROM_INT(info->zmap->world_cell_data.animation_entry_dict_name, info->animation_entry->id, entry->animation_controller_handle);
		worldAnimationEntryModifyBounds(info->animation_entry, 
			NULL, NULL, NULL, 
			bounds->world_mid, &radius);
	}

	entry->base_entry.shared_bounds = createSharedBoundsSphere(info->zmap, radius, 0, vis_dist, vis_dist);

	return entry;
}

WorldCellEntry *createWorldLightEntryFromParsed(ZoneMap *zmap, WorldRegion *region, WorldLightEntryParsed *entry_parsed, bool parsed_will_be_freed)
{
	WorldLightEntry *entry = calloc(1, sizeof(WorldLightEntry));

	entry->base_entry.type = WCENT_LIGHT;

	worldEntryInitBoundsFromParsed(zmap->world_cell_data.streaming_pooled_info, &entry->base_entry, &entry_parsed->base_data);

	if (parsed_will_be_freed)
	{
		entry->light_data = entry_parsed->light_data; // transfer ownership
		entry_parsed->light_data = NULL;
	}
	else
	{
		entry->light_data = StructClone(parse_LightData, entry_parsed->light_data);
	}
	
	assert(entry->light_data);

	entry->light_data->region = region;

	if (entry_parsed->animation_entry_id)
	{
		setupWorldAnimationEntryDictionary(zmap);
		SET_HANDLE_FROM_INT(zmap->world_cell_data.animation_entry_dict_name, entry_parsed->animation_entry_id, entry->animation_controller_handle);
	}

	return &entry->base_entry;
}

//////////////////////////////////////////////////////////////////////////
// Wind

WorldWindSourceEntry *createWorldWindSourceEntry(GroupDef *def, const GroupInfo *info)
{
	WorldWindSourceEntry *entry = calloc(1, sizeof(WorldWindSourceEntry));
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	F32 radius, vis_dist;

	entry->base_entry.type = WCENT_WIND_SOURCE;

	StructCopyAll(parse_WorldWindSourceProperties, def->property_structs.wind_source_properties, &entry->source_data);

	copyMat4(info->world_matrix, bounds->world_matrix);
	copyVec3(info->world_matrix[3], bounds->world_mid);
	radius = entry->source_data.radius;
	vis_dist = 2.0f * radius * info->lod_scale;

	entry->base_entry.shared_bounds = createSharedBoundsSphere(info->zmap, radius, 0, vis_dist, vis_dist);

	return entry;
}

WorldCellEntry *createWorldWindSourceEntryFromParsed(ZoneMap *zmap, WorldRegion *region, WorldWindSourceEntryParsed *entry_parsed)
{
	WorldWindSourceEntry *entry = calloc(1, sizeof(WorldWindSourceEntry));

	entry->base_entry.type = WCENT_WIND_SOURCE;

	worldEntryInitBoundsFromParsed(zmap->world_cell_data.streaming_pooled_info, &entry->base_entry, &entry_parsed->base_data);

	StructCopyAll(parse_WorldWindSourceProperties, &entry_parsed->source_data, &entry->source_data);

	return &entry->base_entry;
}


//////////////////////////////////////////////////////////////////////////
// Sound

WorldSoundEntry *createWorldSoundEntry(	const char *event_name, const char *excluder_str, const char* dsp_str, 
										const char *group_str, int group_ord, const GroupInfo *info, const GroupDef *def)
{
	WorldSoundEntry *entry = calloc(1, sizeof(WorldSoundEntry));
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	char group_ord_str[256];
	sprintf(group_ord_str, "%d", group_ord);

	entry->base_entry.type = WCENT_SOUND;

	entry->event_name = allocAddString(event_name);
	entry->excluder_str = StructAllocString(excluder_str);
	entry->dsp_str = allocAddString(dsp_str);
	entry->editor_group_str = StructAllocString(def->name_str);
	entry->sound_group_str = StructAllocString(group_str);
	entry->sound_group_ord = allocAddString(group_ord_str);
	copyMat4(info->world_matrix, bounds->world_matrix);
	copyVec3(info->world_matrix[3], bounds->world_mid);

	if(entry->sound_group_str && entry->sound_group_str[0])
	{
		entry->base_entry.shared_bounds = createSharedBoundsSphere(info->zmap, 1000.0, 0, 0, 0);
	}
	else
	{
		entry->base_entry.shared_bounds = createSharedBoundsSphere(info->zmap, wl_state.sound_radius_func(entry->event_name), 0, 0, 0);
	}
	

	return entry;
}

WorldCellEntry *createWorldSoundEntryFromParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldSoundEntryParsed *entry_parsed)
{
	WorldSoundEntry *entry = calloc(1, sizeof(WorldSoundEntry));

	entry->base_entry.type = WCENT_SOUND;

	worldEntryInitBoundsFromParsed(streaming_pooled_info, &entry->base_entry, &entry_parsed->base_data);

	entry->event_name = allocAddString(worldGetStreamedString(streaming_pooled_info, entry_parsed->event_name_idx));
	entry->excluder_str = StructAllocString(worldGetStreamedString(streaming_pooled_info, entry_parsed->excluder_str_idx));
	entry->dsp_str = allocAddString(worldGetStreamedString(streaming_pooled_info, entry_parsed->dsp_str_idx));
	entry->editor_group_str = StructAllocString(worldGetStreamedString(streaming_pooled_info, entry_parsed->editor_group_str_idx));
	entry->sound_group_str = StructAllocString(worldGetStreamedString(streaming_pooled_info, entry_parsed->sound_group_str_idx));
	entry->sound_group_ord = worldGetStreamedString(streaming_pooled_info, entry_parsed->sound_group_ord_idx);

	return &entry->base_entry;
}

void destroyWorldSoundEntry(WorldSoundEntry *entry)
{
	StructFreeString(entry->editor_group_str);
	StructFreeString(entry->excluder_str);
	StructFreeString(entry->sound_group_str);
}

//////////////////////////////////////////////////////////////////////////
// Animation

void worldAnimationEntryResetIDCounter(ZoneMap *zmap)
{
	zmap->world_cell_data.animation_id_counter = 1;
	if (zmap->world_cell_data.animation_entry_dict)
		RefSystem_ClearDictionary(zmap->world_cell_data.animation_entry_dict, true);
	zmap->world_cell_data.animation_entry_dict = NULL;
}

void worldAnimationEntrySetIDCounter(ZoneMap *zmap, U32 id_counter)
{
	MAX1(zmap->world_cell_data.animation_id_counter, id_counter);
}

static void addToDictionary(ZoneMap *zmap, WorldAnimationEntry *animation_entry)
{
	setupWorldAnimationEntryDictionary(zmap);
	assert(zmap->world_cell_data.animation_id_counter > animation_entry->id);
	RefSystem_AddIntKeyReferent(zmap->world_cell_data.animation_entry_dict, animation_entry->id, animation_entry);
}

bool worldAnimationEntryInitChildRelativeMatrix(WorldAnimationEntry *animation_controller, const Mat4 world_matrix, Mat4 controller_relative_matrix)
{
	Mat4 inv_animation_matrix;
	if (!animation_controller)
		return false;
	invertMat4Copy(animation_controller->base_entry.bounds.world_matrix, inv_animation_matrix);
	mulMat4Inline(inv_animation_matrix, world_matrix, controller_relative_matrix);
	return true;
}

static void updateWorldAnimationEntryBounds(ZoneMap *zmap, WorldAnimationEntry *animation_entry, const Mat4 world_matrix, F32 radius, bool force_no_add_to_cell)
{
	WorldCellEntryBounds *bounds;
	WorldCell *old_cell;
	F32 dist, bounds_radius = animation_entry->base_entry.shared_bounds->radius;
	F32 bounds_near_lod_near_dist = animation_entry->base_entry.shared_bounds->near_lod_near_dist;
	F32 bounds_far_lod_near_dist = animation_entry->base_entry.shared_bounds->far_lod_near_dist;
	F32 bounds_far_lod_far_dist = animation_entry->base_entry.shared_bounds->far_lod_far_dist;
	Vec3 v;

	old_cell = worldRemoveCellEntryEx(&animation_entry->base_entry, false, true);

	removeSharedBoundsRef(animation_entry->base_entry.shared_bounds);
	animation_entry->base_entry.shared_bounds = NULL;

	bounds = &animation_entry->base_entry.bounds;
	subVec3(bounds->world_mid, world_matrix[3], v);
	dist = normalVec3(v);

	if (bounds_radius > dist + radius)
	{
		// existing bounds contain the new bounds
	}
	else if (radius > dist + bounds_radius)
	{
		// new bounds contain the existing bounds
		copyMat4(world_matrix, bounds->world_matrix);
		copyVec3(world_matrix[3], bounds->world_mid);
		bounds_radius = radius;
	}
	else if (dist > bounds_radius + radius)
	{
		// bounds do not overlap
		scaleAddVec3(v, bounds_radius, bounds->world_mid, bounds->world_mid);
		scaleAddVec3(v, -radius, bounds->world_mid, bounds->world_mid);
		addVec3(bounds->world_mid, world_matrix[3], bounds->world_mid);
		scaleVec3(bounds->world_mid, 0.5f, bounds->world_mid);

		copyVec3(bounds->world_mid, bounds->world_matrix[3]);
		bounds_radius = distance3(bounds->world_mid, world_matrix[3]) + radius;
	}
	else if (bounds_radius > radius)
	{
		F32 halfrad = 0.5f * radius;
		scaleAddVec3(v, -halfrad, bounds->world_mid, bounds->world_mid);

		copyVec3(bounds->world_mid, bounds->world_matrix[3]);
		bounds_radius = bounds_radius + halfrad;
	}
	else
	{
		F32 halfrad = 0.5f * bounds_radius;
		scaleAddVec3(v, halfrad, world_matrix[3], bounds->world_mid);

		copyVec3(bounds->world_mid, bounds->world_matrix[3]);
		bounds_radius = radius + halfrad;
	}

	if (animation_entry->animation_properties.local_space)
		copyMat3(unitmat, bounds->world_matrix);

	animation_entry->base_entry.shared_bounds = createSharedBoundsSphere(zmap, bounds_radius, bounds_near_lod_near_dist, bounds_far_lod_near_dist, bounds_far_lod_far_dist);

	if (old_cell && !force_no_add_to_cell)
		worldAddCellEntry(old_cell->region, &animation_entry->base_entry);
}

WorldAnimationEntry *createWorldAnimationEntry(const GroupInfo *info, F32 far_lod_near_dist, F32 far_lod_far_dist, const WorldAnimationProperties *animation_properties)
{
	WorldAnimationEntry *animation_entry = NULL;
	WorldCellEntryBounds *bounds;

	if (animation_properties->local_space && !info->animation_entry && !info->parent_entry)
	{
		// try to pool the animation entry
		RefDictIterator iter;
		WorldAnimationEntry *entry;

		setupWorldAnimationEntryDictionary(info->zmap);
		RefSystem_InitRefDictIterator(info->zmap->world_cell_data.animation_entry_dict_name, &iter);
		while (entry = RefSystem_GetNextReferentFromIterator(&iter))
		{
			if (!GET_REF(entry->parent_animation_controller_handle) && 
				!entry->base_entry_data.parent_entry && 
				entry->base_entry.bounds.object_tag == info->tag_id && 
				SAFE_MEMBER(entry->base_entry_data.cell, region) == info->region && 
				StructCompare(parse_WorldAnimationProperties, animation_properties, &entry->animation_properties, 0, 0, 0)==0)
			{
				animation_entry = entry;
				break;
			}
		}
	}

	if (animation_entry)
	{
		updateWorldAnimationEntryBounds(info->zmap, animation_entry, info->world_matrix, info->radius, true);
		bounds = &animation_entry->base_entry.bounds;
	}
	else
	{
		animation_entry = calloc(1, sizeof(WorldAnimationEntry));
		bounds = &animation_entry->base_entry.bounds;

		animation_entry->base_entry.type = WCENT_ANIMATION;
		copyMat4(info->world_matrix, bounds->world_matrix);

		copyVec3(info->world_matrix[3], bounds->world_mid);

		animation_entry->base_entry.shared_bounds = createSharedBoundsSphere(info->zmap, info->radius, 0, far_lod_near_dist, far_lod_far_dist);

		animation_entry->id = info->zmap->world_cell_data.animation_id_counter++;
		addToDictionary(info->zmap, animation_entry);

		StructCopy(parse_WorldAnimationProperties, animation_properties, &animation_entry->animation_properties, 0, 0, 0);
	}

	if (animation_properties->local_space)
		copyMat3(unitmat, bounds->world_matrix);

	MAX1(far_lod_near_dist, animation_entry->base_entry.shared_bounds->far_lod_near_dist);
	MAX1(far_lod_far_dist, animation_entry->base_entry.shared_bounds->far_lod_far_dist);

	setSharedBoundsSphere(animation_entry->base_entry.shared_bounds, animation_entry->base_entry.shared_bounds->radius);
	setSharedBoundsVisDist(animation_entry->base_entry.shared_bounds, 0, far_lod_near_dist, far_lod_far_dist);

	animation_entry->ref_count++;

	if (info->animation_entry)
	{
		Mat4 inv_animation_matrix;

		setupWorldAnimationEntryDictionary(info->zmap);
		SET_HANDLE_FROM_INT(info->zmap->world_cell_data.animation_entry_dict_name, info->animation_entry->id, animation_entry->parent_animation_controller_handle);

		invertMat4Copy(info->animation_entry->base_entry.bounds.world_matrix, inv_animation_matrix);
		mulMat4Inline(inv_animation_matrix, bounds->world_matrix, animation_entry->parent_controller_relative_matrix);
		assert(validateMat4(animation_entry->parent_controller_relative_matrix));
		assert(isNonZeroMat3(animation_entry->parent_controller_relative_matrix));

		worldAnimationEntryModifyBounds(info->animation_entry, 
			NULL, NULL, NULL, 
			bounds->world_mid, &animation_entry->base_entry.shared_bounds->radius);

		setSharedBoundsSphere(animation_entry->base_entry.shared_bounds, animation_entry->base_entry.shared_bounds->radius);
	}

	return animation_entry;
}

WorldCellEntry *createWorldAnimationEntryFromParsed(ZoneMap *zmap, WorldAnimationEntryParsed *entry_parsed)
{
	WorldAnimationEntry *animation_entry = calloc(1, sizeof(WorldAnimationEntry));
	WorldStreamingInfo *streaming_info = zmap->world_cell_data.streaming_info;
	WorldAnimationEntry *parent_animation_controller;

	animation_entry->base_entry.type = WCENT_ANIMATION;

	worldEntryInitBoundsFromParsed(zmap->world_cell_data.streaming_pooled_info, &animation_entry->base_entry, &entry_parsed->base_data);

	animation_entry->id = entry_parsed->id;
	addToDictionary(zmap, animation_entry);

	StructCopy(parse_WorldAnimationProperties, &entry_parsed->animation_properties, &animation_entry->animation_properties, 0, 0, 0);

	if (entry_parsed->parent_animation_entry_id)
	{
		setupWorldAnimationEntryDictionary(zmap);
		SET_HANDLE_FROM_INT(zmap->world_cell_data.animation_entry_dict_name, entry_parsed->parent_animation_entry_id, animation_entry->parent_animation_controller_handle);
	}

	if (parent_animation_controller = GET_REF(animation_entry->parent_animation_controller_handle))
	{
		Mat4 inv_animation_matrix;
		invertMat4Copy(parent_animation_controller->base_entry.bounds.world_matrix, inv_animation_matrix);
		mulMat4Inline(inv_animation_matrix, entry_parsed->base_data.entry_bounds.world_matrix, animation_entry->parent_controller_relative_matrix);
	}

	animation_entry->ref_count++;

	return &animation_entry->base_entry;
}

bool uninitWorldAnimationEntry(WorldAnimationEntry *animation_entry)
{
	animation_entry->ref_count--;
	
	if (animation_entry->ref_count <= 0)
	{
		if (IS_HANDLE_ACTIVE(animation_entry->parent_animation_controller_handle))
			REMOVE_HANDLE(animation_entry->parent_animation_controller_handle);
		RefSystem_RemoveReferent(animation_entry, false);
		return true;
	}

	return false;
}

void worldAnimationEntryModifyBounds(WorldAnimationEntry *entry, const Mat4 world_matrix, Vec3 local_min, Vec3 local_max, Vec3 world_mid, F32 *radius)
{
	WorldAnimationProperties *anim = &entry->animation_properties;
	Vec3 scale = {1,1,1}, translation;

	// scale
	MAX1(scale[0], anim->scale_time[0]?anim->scale_amount[0]:1);
	MAX1(scale[1], anim->scale_time[1]?anim->scale_amount[1]:1);
	MAX1(scale[2], anim->scale_time[2]?anim->scale_amount[2]:1);

	// translation
	setVec3(translation, anim->translation_time[0]?anim->translation_amount[0]:0, anim->translation_time[1]?anim->translation_amount[1]:0, anim->translation_time[2]?anim->translation_amount[2]:0);

	if (world_matrix && local_min && local_max)
	{
		Vec3 local_mid;

		// scale
		mulVecVec3(scale, local_min, local_min);
		mulVecVec3(scale, local_max, local_max);

		// sway & rotation
		if ((!sameVec3(anim->sway_angle, zerovec3) && !sameVec3(anim->sway_time, zerovec3)) || !sameVec3(anim->rotation_time, zerovec3))
		{
			// to simplify the calculations, use the rotation bounds code for sway:
			Vec3 local_pivot;
			F32 local_radius;

			centerVec3(local_min, local_max, local_mid);
			local_radius = distance3(local_max, local_mid);

			if (anim->local_space)
			{
				zeroVec3(local_pivot);
			}
			else
			{
				Mat4 inv_world_matrix;
				invertMat4Copy(world_matrix, inv_world_matrix);
				mulVecMat4(entry->base_entry.bounds.world_matrix[3], inv_world_matrix, local_pivot);
			}

			// set the mid point to the animation pivot point and extend the radius
			local_radius += distance3(local_pivot, local_mid);
			addVec3same(local_pivot, -local_radius, local_min);
			addVec3same(local_pivot, local_radius, local_max);
		}

		// translation
		local_min[0] += translation[0] < 0 ? translation[0] : 0;
		local_min[1] += translation[1] < 0 ? translation[1] : 0;
		local_min[2] += translation[2] < 0 ? translation[2] : 0;

		local_max[0] += translation[0] > 0 ? translation[0] : 0;
		local_max[1] += translation[1] > 0 ? translation[1] : 0;
		local_max[2] += translation[2] > 0 ? translation[2] : 0;

		// fixup world bounds
		centerVec3(local_min, local_max, local_mid);
		mulVecMat4(local_mid, world_matrix, world_mid);
		*radius = distance3(local_mid, local_max);
	}
	else
	{
		F32 max_scale, max_translation;

		// scale
		max_scale = lengthVec3(scale);
		*radius *= max_scale;

		// sway & rotation
		if ((!sameVec3(anim->sway_angle, zerovec3) && !sameVec3(anim->sway_time, zerovec3)) || !sameVec3(anim->rotation_time, zerovec3))
		{
			// to simplify the calculations, use the rotation bounds code for sway:
			// set the mid point to the animation pivot point and extend the radius

			if (anim->local_space)
			{
				// don't know the local mid, so assume it is (0,0,0)
			}
			else
			{
				*radius += distance3(entry->base_entry.bounds.world_matrix[3], world_mid);
				copyVec3(entry->base_entry.bounds.world_matrix[3], world_mid);
			}
		}

		// translation
		max_translation = lengthVec3(translation);
		scaleAddVec3(translation, 0.5f, world_mid, world_mid);
		*radius += 0.5f * max_translation;
	}

	if (GET_REF(entry->parent_animation_controller_handle))
		worldAnimationEntryModifyBounds(GET_REF(entry->parent_animation_controller_handle), world_matrix, local_min, local_max, world_mid, radius);
}

void worldAnimationEntryUpdate(WorldAnimationEntry *entry, F32 loop_timer, U32 update_timestamp)
{
	WorldAnimationProperties *anim = &entry->animation_properties;
	Mat4 temp_mat, temp_mat2, temp_mat3;
	Mat4Ptr res_mat, in_mat;
	F32 time, angle;
	WorldAnimationEntry *parent_animation_controller = GET_REF(entry->parent_animation_controller_handle);

	if (anim->local_space)
	{
		copyMat4(unitmat, temp_mat3);
	}
	else if (parent_animation_controller)
	{
		assert(parent_animation_controller != entry);
		if (parent_animation_controller->last_update_timestamp != update_timestamp)
			worldAnimationEntryUpdate(parent_animation_controller, loop_timer, update_timestamp);
		if (parent_animation_controller->animation_properties.local_space)
			copyMat4(unitmat, temp_mat3);
		else
			mulMat4Inline(parent_animation_controller->full_matrix, entry->parent_controller_relative_matrix, temp_mat3);
	}
	else
	{
		copyMat4(entry->base_entry.bounds.world_matrix, temp_mat3);
	}

	entry->last_update_timestamp = update_timestamp;
	loop_timer += anim->time_offset;

	in_mat = temp_mat3;
	res_mat = temp_mat2;

	copyMat4(unitmat, temp_mat);

	//////////////////////////////////////////////////////////////////////////
	// translation

	if (anim->translation_amount[0] && anim->translation_time[0])
	{
		time = loop_timer / anim->translation_time[0];
		if (anim->translation_loop)
			temp_mat[3][0] += anim->translation_amount[0] * (time - ((int)time));
		else
			temp_mat[3][0] += anim->translation_amount[0] * 0.5f * (1 + sinf((time - ((int)time)) * TWOPI));
	}

	if (anim->translation_amount[1] && anim->translation_time[1])
	{
		time = loop_timer / anim->translation_time[1];
		if (anim->translation_loop)
			temp_mat[3][1] += anim->translation_amount[1] * (time - ((int)time));
		else
			temp_mat[3][1] += anim->translation_amount[1] * 0.5f * (1 + sinf((time - ((int)time)) * TWOPI));
	}

	if (anim->translation_amount[2] && anim->translation_time[2])
	{
		time = loop_timer / anim->translation_time[2];
		if (anim->translation_loop)
			temp_mat[3][2] += anim->translation_amount[2] * (time - ((int)time));
		else
			temp_mat[3][2] += anim->translation_amount[2] * 0.5f * (1 + sinf((time - ((int)time)) * TWOPI));
	}

	if (temp_mat[3][0] || temp_mat[3][1] || temp_mat[3][2])
	{
		mulMat4Inline(in_mat, temp_mat, res_mat);
		SWAPP(res_mat, in_mat);
		zeroVec3(temp_mat[3]);
	}

	//////////////////////////////////////////////////////////////////////////
	// rotation

	if (anim->rotation_time[0])
	{
		time = loop_timer / anim->rotation_time[0];
		angle = (time - ((int)time)) * TWOPI;
		mat3FromAxisAngle(temp_mat, sidevec, angle);
		mulMat4Inline(in_mat, temp_mat, res_mat);
		SWAPP(res_mat, in_mat);
	}

	if (anim->rotation_time[1])
	{
		time = loop_timer / anim->rotation_time[1];
		angle = (time - ((int)time)) * TWOPI;
		mat3FromAxisAngle(temp_mat, upvec, angle);
		mulMat4Inline(in_mat, temp_mat, res_mat);
		SWAPP(res_mat, in_mat);
	}

	if (anim->rotation_time[2])
	{
		time = loop_timer / anim->rotation_time[2];
		angle = (time - ((int)time)) * TWOPI;
		mat3FromAxisAngle(temp_mat, forwardvec, angle);
		mulMat4Inline(in_mat, temp_mat, res_mat);
		SWAPP(res_mat, in_mat);
	}

	//////////////////////////////////////////////////////////////////////////
	// sway

	if (anim->sway_angle[0] && anim->sway_time[0])
	{
		time = loop_timer / anim->sway_time[0];
		angle = anim->sway_angle[0] * sinf((time - ((int)time)) * TWOPI);
		mat3FromAxisAngle(temp_mat, sidevec, angle);
		mulMat4Inline(in_mat, temp_mat, res_mat);
		SWAPP(res_mat, in_mat);
	}

	if (anim->sway_angle[1] && anim->sway_time[1])
	{
		time = loop_timer / anim->sway_time[1];
		angle = anim->sway_angle[1] * sinf((time - ((int)time)) * TWOPI);
		mat3FromAxisAngle(temp_mat, upvec, angle);
		mulMat4Inline(in_mat, temp_mat, res_mat);
		SWAPP(res_mat, in_mat);
	}

	if (anim->sway_angle[2] && anim->sway_time[2])
	{
		time = loop_timer / anim->sway_time[2];
		angle = anim->sway_angle[2] * sinf((time - ((int)time)) * TWOPI);
		mat3FromAxisAngle(temp_mat, forwardvec, angle);
		mulMat4Inline(in_mat, temp_mat, res_mat);
		SWAPP(res_mat, in_mat);
	}

	//////////////////////////////////////////////////////////////////////////
	// scale

	if (anim->scale_amount[0] != 1 && anim->scale_time[0])
	{
		time = loop_timer / anim->scale_time[0];
		angle = 1 + (anim->scale_amount[0] - 1) * 0.5f * (1 + sinf((time - ((int)time)) * TWOPI));
		scaleVec3(in_mat[0], angle, in_mat[0]);
	}

	if (anim->scale_amount[1] != 1 && anim->scale_time[1])
	{
		time = loop_timer / anim->scale_time[1];
		angle = 1 + (anim->scale_amount[1] - 1) * 0.5f * (1 + sinf((time - ((int)time)) * TWOPI));
		scaleVec3(in_mat[1], angle, in_mat[1]);
	}

	if (anim->scale_amount[2] != 1 && anim->scale_time[2])
	{
		time = loop_timer / anim->scale_time[2];
		angle = 1 + (anim->scale_amount[2] - 1) * 0.5f * (1 + sinf((time - ((int)time)) * TWOPI));
		scaleVec3(in_mat[2], angle, in_mat[2]);
	}

	if (!anim->local_space && parent_animation_controller && parent_animation_controller->animation_properties.local_space)
		mulMat4Inline(in_mat, parent_animation_controller->full_matrix, entry->full_matrix);
	else
		copyMat4(in_mat, entry->full_matrix);


	if (wl_state.debug.disable_world_animation)
	{
		if (anim->local_space)
			copyMat4(unitmat, entry->full_matrix);
		else
			copyMat4(entry->base_entry.bounds.world_matrix, entry->full_matrix);
	}

	++wl_state.debug.world_animation_update_count;
}


//////////////////////////////////////////////////////////////////////////
// AltPivot

WorldAltPivotEntry *createWorldAltPivotEntry(GroupDef *def, GroupInfo *info)
{
	WorldAltPivotEntry *entry = calloc(1, sizeof(WorldAltPivotEntry));
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	WorldPhysicalProperties *physical_properties = &def->property_structs.physical_properties;

	entry->base_entry.type = WCENT_ALTPIVOT;

	if (physical_properties->bHandPivot)
		entry->hand_pivot = 1;
	if (physical_properties->bMassPivot)
		entry->mass_pivot = 1;
	if (entry->carry_anim_bit = StaticDefineIntRevLookup(WorldCarryAnimationModeEnum, physical_properties->eCarryAnimationBit))
		entry->carry_anim_bit = allocAddString(entry->carry_anim_bit);

	copyMat4(info->world_matrix, bounds->world_matrix);
	copyVec3(info->world_mid, bounds->world_mid);

	entry->base_entry.shared_bounds = createSharedBoundsSphere(info->zmap, info->radius, 0, 0, 0);

	return entry;
}

WorldCellEntry *createWorldAltPivotEntryFromParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldAltPivotEntryParsed *entry_parsed)
{
	WorldAltPivotEntry *entry = calloc(1, sizeof(WorldAltPivotEntry));
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;

	entry->base_entry.type = WCENT_ALTPIVOT;

	worldEntryInitBoundsFromParsed(streaming_pooled_info, &entry->base_entry, &entry_parsed->base_data);

	entry->hand_pivot = entry_parsed->hand_pivot;
	entry->mass_pivot = entry_parsed->mass_pivot;
	entry->carry_anim_bit = worldGetStreamedString(streaming_pooled_info, entry_parsed->carry_anim_bit_string_idx);
	entry->carry_anim_bit = allocAddString(entry->carry_anim_bit);

	return &entry->base_entry;
}

