/***************************************************************************



***************************************************************************/

#include "WorldCellEntryPrivate.h"
#include "WorldCellStreamingPrivate.h"
#include "wlModelLoad.h"
#include "PhysicsSDK.h"
#include "wlState.h"
#include "beaconConnection.h"
#include "partition_enums.h"
#include "WorldCell.h"
#include "bounds.h"

#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

__forceinline static PSDKCookedMesh *entryGetCookedMesh(WorldCollisionEntry *entry, Mat4 mat, U32 createSMD)
{
	WorldCellEntryBounds*	bounds = &entry->base_entry.bounds;
	WorldCellEntrySharedBounds *shared_bounds = entry->base_entry.shared_bounds;
	Model*				model = entry->model;
	PSDKCookedMesh		*mesh = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (model)
	{
		mesh = geoCookMesh(model, entry->scale, entry->spline, entry, true, true);

		if (createSMD)
		{
			char name[MAX_PATH];
			char detail[MAX_PATH];
			geoGetCookedMeshName(SAFESTR(name), SAFESTR(detail), model, entry->spline, entry->scale);

			if (!wcStoredModelDataFind(name, NULL))
			{
				WorldCollStoredModelDataDesc	desc = {0};

				if(SAFE_MEMBER2(model, header, filename))
					desc.filename = model->header->filename;

				copyVec3(shared_bounds->local_min, desc.min);
				copyVec3(shared_bounds->local_max, desc.max);
#if !PSDK_DISABLED
				psdkCookedMeshGetTriangles(mesh, &desc.tris, &desc.tri_count);
				psdkCookedMeshGetVertices(mesh, &desc.verts, &desc.vert_count);
#endif
				wcStoredModelDataCreate(name, detail, &desc, NULL);
			}
		}
	}
	else
	{
		// volume

		if (!entry->cooked_mesh)
		{
			if (entry->collision_radius > 0)
			{
				entry->cooked_mesh = geoCookSphere(entry->collision_radius);
			}
			else
			{
				Vec3 box_size;
				subVec3(entry->collision_max, entry->collision_min, box_size);
				scaleVec3(box_size, 0.5f, box_size);
				entry->cooked_mesh = geoCookBox(box_size);
			}
		}

		mesh = entry->cooked_mesh;
	}

	if (mat)
		copyMat4(bounds->world_matrix, mat);

	PERFINFO_AUTO_STOP();

	return mesh;
}

static int entryIsTransient(WorldCollisionEntry *entry)
{
	WorldInteractionEntry *cursor = entry->base_entry_data.parent_entry;

	while(cursor)
	{
		if (wl_state.is_consumable_func && cursor && wl_state.is_consumable_func(cursor))
		{
			// Consumables are ignored by beaconizer and turned into dyn conns at rebuild block time
			return 1; 
		}

		if (cursor && cursor->hasInteractionNode && cursor->base_interaction_properties && wlInteractionBaseIsDestructible(cursor->base_interaction_properties))
		{
			// Destructibles are ignored by beaconizer and turned into dyn conns at rebuild block time
			return 1;
		}

		cursor = cursor->base_entry_data.parent_entry;
	}

	return 0;
}

bool worldCollisionEntryIsTraversable(WorldCollisionEntry *entry)
{
	WorldInteractionEntry *parent = entry->base_entry_data.parent_entry;

	if (wl_state.is_traversable_func)
		return wl_state.is_traversable_func(parent);
	return false;
}

WorldCollObject *worldCollisionEntryGetCollObject(WorldCollisionEntry *pEntry, int iPartitionIdx)
{
	return eaGet(&pEntry->eaCollObjects, iPartitionIdx);
}

void worldCollisionEntrySetCollObject(WorldCollisionEntry *pEntry, int iPartitionIdx, WorldCollObject *wco)
{
	assert(iPartitionIdx >= 0 && iPartitionIdx <= 31);
	eaSet(&pEntry->eaCollObjects, wco, iPartitionIdx);
}

void entryCollObjectMsgHandler(const WorldCollObjectMsg* msg)
{
	WorldCollisionEntry *entry = msg->userPointer;

	switch(msg->msgType)
	{
		xcase WCO_MSG_GET_DEBUG_STRING:
		{
			snprintf_s(	msg->in.getDebugString.buffer,
						msg->in.getDebugString.bufferLen,
						"Model: %s",
						SAFE_MEMBER2(entry, model, name));
		}
		
		xcase WCO_MSG_DESTROYED:
		{
			// There was logic here to go find the entry and clear the collision
			// field, but this usually gets called after the memory is freed, so
			// doing anything here is at best useless and often causes memory corruption.
		}
		
		xcase WCO_MSG_GET_SHAPE:
		{
			WorldCollObjectMsgGetShapeOut*	getShape = msg->out.getShape;
			WorldCollObjectMsgGetShapeOutInst* shapeInst;

			if (entryIsTransient(entry) && beaconIsBeaconizer())
			{
				return;
			}

			wcoAddShapeInstance(getShape, &shapeInst);
			shapeInst->mesh = entryGetCookedMesh(entry, getShape->mat, 0);
			shapeInst->filter = entry->filter;
		}
		
		xcase WCO_MSG_GET_MODEL_DATA:
		{
			U32							filterBits = msg->in.getModelData.filterBits;
			Model*						model = SAFE_MEMBER(entry, model);
			WorldCollStoredModelData*	smd = NULL;
			WorldCollModelInstanceData* inst = NULL;
			char						name[200];
		
			if (!model)
				return;

			// ensure correct bits
			if (!(entry->filter.filterBits & filterBits))
				return;

			// ensure collision isn't traversable (in which case it should be
			// entirely ignored for beaconizer purposes
			if (entry->filter.filterBits & WC_FILTER_BIT_WORLD_TRAVERSABLE)//worldCollisionEntryIsTraversable(entry))
				return;

			inst = callocStruct(WorldCollModelInstanceData);
			
			inst->transient = entryIsTransient(entry);

			geoGetCookedMeshName(SAFESTR(name), NULL, 0, model, entry->spline, entry->scale);
			entryGetCookedMesh(entry, inst->world_mat, 1);
			wcStoredModelDataFind(name, &smd);

			assert(smd);
			
			msg->out.getModelData->modelData = smd;
			msg->out.getModelData->instData = inst;
		}

		xcase WCO_MSG_GET_INSTANCE_DATA:
		{
			U32							filterBits = msg->in.getInstData.filterBits;
			Model*						model = SAFE_MEMBER(entry, model);
			WorldCollModelInstanceData* inst = NULL;

			if(!model)
				return;

			if(!(filterBits & entry->filter.filterBits))
				return;

			inst = callocStruct(WorldCollModelInstanceData);

			inst->transient = entryIsTransient(entry);
			copyMat4(entry->base_entry.bounds.world_matrix, inst->world_mat);
			inst->noGroundConnections = 0;

			msg->out.getInstData->instData = inst;
		}
	}
}

S32 worldCollisionEntryIsTerrainFromWCO(WorldCollObject* wco){
	WorldCollisionEntry *entry;

	return	wcoGetUserPointer(wco, entryCollObjectMsgHandler, &entry) &&
			entry &&
			entry->filter.shapeGroup == WC_SHAPEGROUP_TERRAIN;
}

static WorldCollisionEntry **queuedCookings=NULL;

// If beaconizer, block for cooking
__forceinline static bool tryCookingMesh(WorldCollisionEntry *entry)
{
	return entry->model ? !!geoCookMesh(entry->model, entry->scale, entry->spline, entry, true, wlIsServer() || beaconIsBeaconizer()) : true;
}

void worldCellCollisionEntryCheckCookings(void)
{
	int i;
	PERFINFO_AUTO_START_FUNC();
	ADD_MISC_COUNT(eaSize(&queuedCookings), "queuedCookings");
	for (i=eaSize(&queuedCookings)-1; i>=0; i--) {
		WorldCollisionEntry *entry = queuedCookings[i];
 		if (tryCookingMesh(entry))
		{
			if (worldCellEntryIsAnyPartitionOpen(&queuedCookings[i]->base_entry))
				openWorldCollisionEntryAll(queuedCookings[i]);
 			eaRemoveFast(&queuedCookings, i);
		}
	}
	PERFINFO_AUTO_STOP();
}

void worldCellCollisionEntryBeingDestroyed(WorldCollisionEntry *entry)
{
	eaFindAndRemoveFast(&queuedCookings, entry);
	geoScaledCollisionRemoveRef(entry);
}

WorldCollisionEntry *createWorldCollisionEntry(GroupTracker *tracker, GroupDef *def, Model *model, const char ***texture_swaps, const char ***material_swaps,
	WorldPlanetProperties *planet, const GroupInfo *info, bool is_terrain)
{
	WorldCollisionEntry *entry = calloc(1, sizeof(WorldCollisionEntry));
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	bool no_coll = (!info->collisionFilterBits || info->is_debris) && !is_terrain;
	Vec3 object_scale;
	F32 radius;
	int i;

	entry->base_entry.type = WCENT_COLLISION;
	
	entry->model = model;
	if(material_swaps)
		for(i = 0; i < eaSize(material_swaps); i++)
			eaPush(&entry->eaMaterialSwaps, strdup((*material_swaps)[i]));
	entry->tracker_handle = trackerHandleCreate(tracker);

	scaleVec3(def?def->model_scale:onevec3, info->uniform_scale, object_scale);

	groupQuantizeScale(object_scale, entry->scale);

	if (info->spline)
	{
		radius = info->radius;
		entry->spline = calloc(1, sizeof(GroupSplineParams));
		memcpy(entry->spline, info->spline, sizeof(GroupSplineParams));
	}
	else if (planet)
	{
		radius = planet->collision_radius;
		setVec3same(entry->scale, planet->collision_radius);
	}
	else if (model)
	{
		radius = model->radius * vec3MaxComponent(entry->scale);
	}
	else
	{
		radius = info->radius;
	}

	copyMat4(info->world_matrix, bounds->world_matrix);
	copyVec3(info->world_mid, bounds->world_mid);

	if (!model && !planet)
	{
		// volume
		no_coll = true;
	}

	entry->filter.shapeGroup = WC_SHAPEGROUP_NONE;
	if (no_coll && !model && !planet)
	{
		entry->filter.filterBits = WC_FILTER_BITS_VOLUME;
	}
	else if (no_coll)
	{
		entry->filter.filterBits = WC_FILTER_BIT_EDITOR;
		entry->filter.shapeGroup = WC_SHAPEGROUP_EDITOR_ONLY;
	}
	else if (is_terrain)
	{
		entry->filter.filterBits = WC_FILTER_BITS_TERRAIN;
		entry->filter.shapeGroup = WC_SHAPEGROUP_TERRAIN;
	}
	else
	{
		entry->filter.filterBits = info->collisionFilterBits | WC_FILTER_BIT_EDITOR;
		entry->filter.shapeGroup = WC_SHAPEGROUP_WORLD_BASIC;
	}

	// collision entries inside of moving parts on automatic gates should be traversable
	if (worldInteractionEntryIsTraversable(info->parent_entry) && entry->filter.shapeGroup == WC_SHAPEGROUP_WORLD_BASIC)
	{
		entry->filter.filterBits &= ~(WC_FILTER_BIT_WORLD_NORMAL);
		entry->filter.filterBits |= WC_FILTER_BIT_WORLD_TRAVERSABLE;
	}

	if (info->spline)
	{
		entry->base_entry.shared_bounds = createSharedBounds(info->zmap, model, info->world_min, info->world_max, radius, 0, 0, 0);
	}
	else if (!model && !planet && def)
	{
		GroupVolumeProperties *props = def->property_structs.volume;
		if (props && props->eShape == GVS_Sphere)
		{
			entry->base_entry.shared_bounds = createSharedBoundsSphere(info->zmap, props->fSphereRadius, 0, 0, 0);
			copyVec3(bounds->world_matrix[3], bounds->world_mid);
			entry->collision_radius = props->fSphereRadius;
		}
		else if (props && props->eShape == GVS_Box)
		{
			Vec3 box_center;
			centerVec3(props->vBoxMin, props->vBoxMax, box_center);
			addVec3(bounds->world_matrix[3], box_center, bounds->world_matrix[3]);
			subVec3(props->vBoxMin, box_center, entry->collision_min);
			subVec3(props->vBoxMax, box_center, entry->collision_max);
			entry->base_entry.shared_bounds = createSharedBounds(info->zmap, NULL, entry->collision_min, entry->collision_max, radius, 0, 0, 0);
		}
		else 
		{
			entry->base_entry.shared_bounds = createSharedBounds(info->zmap, NULL, zerovec3, zerovec3, radius, 0, 0, 0);		
		}
	}
	else if (model)
	{
		Vec3 local_min, local_max;
		mulVecVec3(model->min, entry->scale, local_min);
		mulVecVec3(model->max, entry->scale, local_max);
		entry->base_entry.shared_bounds = createSharedBounds(info->zmap, model, local_min, local_max, radius, 0, 0, 0);
	}
	else
	{
		assert(def);
		entry->base_entry.shared_bounds = createSharedBounds(info->zmap, model, def->bounds.min, def->bounds.max, radius, 0, 0, 0);
	}

	if (!tryCookingMesh(entry))
		eaPush(&queuedCookings, entry);

	return entry;
}

WorldCellEntry *createWorldCollisionEntryFromParsed(ZoneMap *zmap, WorldStreamingPooledInfo *streaming_pooled_info, WorldCollisionEntryParsed *entry_parsed)
{
	WorldCollisionEntry *entry = calloc(1, sizeof(WorldCollisionEntry));
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	WorldInteractionEntry *parent = worldGetInteractionEntryFromUid(zmap, entry_parsed->base_data.parent_entry_uid, true);
	int i;

	entry->base_entry.type = WCENT_COLLISION;

	worldEntryInitBoundsFromParsed(streaming_pooled_info, &entry->base_entry, &entry_parsed->base_data);

	if (entry_parsed->spline)
	{
		entry->spline = calloc(1, sizeof(GroupSplineParams));
		
		copyVec3(entry_parsed->spline->param0, entry->spline->spline_matrices[0][0]);
		copyVec3(entry_parsed->spline->param1, entry->spline->spline_matrices[0][1]);
		copyVec3(entry_parsed->spline->param2, entry->spline->spline_matrices[0][2]);
		copyVec3(entry_parsed->spline->param3, entry->spline->spline_matrices[0][3]);

		copyVec3(entry_parsed->spline->param4, entry->spline->spline_matrices[1][0]);
		copyVec3(entry_parsed->spline->param5, entry->spline->spline_matrices[1][1]);
		copyVec3(entry_parsed->spline->param6, entry->spline->spline_matrices[1][2]);
		copyVec3(entry_parsed->spline->param7, entry->spline->spline_matrices[1][3]);
	}

	if (entry_parsed->model_idx >= 0)
		entry->model = groupModelFind(worldGetStreamedString(streaming_pooled_info, entry_parsed->model_idx), 0);

	for(i = 0; i < eaSize(&entry_parsed->eaMaterialSwaps); i++)
		eaPush(&entry->eaMaterialSwaps, strdup(entry_parsed->eaMaterialSwaps[i]));

	copyVec3(entry_parsed->scale, entry->scale);

	entry->collision_radius = entry_parsed->collision_radius;
	copyVec3(entry_parsed->collision_min, entry->collision_min);
	copyVec3(entry_parsed->collision_max, entry->collision_max);

	entry->filter.shapeGroup = WC_SHAPEGROUP_WORLD_BASIC;
	entry->filter.filterBits = entry_parsed->filterBits;

	if (!tryCookingMesh(entry))
		eaPush(&queuedCookings, entry);

	return &entry->base_entry;
}

static void resetDebris(int iPartitionIdx, WorldCell *cell, const Vec3 world_min, const Vec3 world_max, const WorldCellEntryBounds *bounds, const WorldCellEntrySharedBounds *shared_bounds, const Mat4 inv_world_mat)
{
	int i;

	if (!cell || cell->cell_state != WCS_OPEN)
		return;

	// check overlap with cell
	if (!boxBoxCollision(cell->bounds.world_min, cell->bounds.world_max, world_min, world_max))
		return;

	for (i = 0; i < eaSize(&cell->nondrawable_entries); ++i)
	{
		if (cell->nondrawable_entries[i]->type == WCENT_FX)
		{
			WorldFXEntry *fx_entry = (WorldFXEntry *)cell->nondrawable_entries[i];
			if (worldCellEntryIsPartitionOpen(&fx_entry->base_entry, iPartitionIdx) && fx_entry->debris_model_name && !fx_entry->debris_needs_reset)
			{
				// check overlap with debris fx bounds
				Vec3 local_mid;
				mulVecMat4(fx_entry->base_entry.bounds.world_mid, inv_world_mat, local_mid);
				fx_entry->debris_needs_reset = boxSphereCollision(shared_bounds->local_min, shared_bounds->local_max, local_mid, fx_entry->base_entry.shared_bounds->radius);
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
		resetDebris(iPartitionIdx, cell->children[i], world_min, world_max, bounds, shared_bounds, inv_world_mat);
}

void openWorldCollisionEntry(int iPartitionIdx, WorldCollisionEntry *entry)
{
	WorldCellEntryBounds *bounds;
	WorldCellEntrySharedBounds *shared_bounds;
	Vec3 world_min, world_max;
	WorldCollObject *wco;

	wco = eaGet(&entry->eaCollObjects, iPartitionIdx);
	if (wco && !wcoIsDestroyed(wco))
	{
		return;
	}
		
	if (!tryCookingMesh(entry))
	{
		eaPushUnique(&queuedCookings, entry);
		return;
	}

	bounds = &entry->base_entry.bounds;
	shared_bounds = entry->base_entry.shared_bounds;

	mulBoundsAA(shared_bounds->local_min, shared_bounds->local_max, bounds->world_matrix, world_min, world_max);
	
	if (wco) {
		wcoDestroy(&entry->eaCollObjects[iPartitionIdx]);
	} 
	eaSet(&entry->eaCollObjects, NULL, iPartitionIdx);
	
	wcoCreate(	&entry->eaCollObjects[iPartitionIdx],
				worldGetActiveColl(iPartitionIdx),
				entryCollObjectMsgHandler,
				entry,
				world_min,
				world_max,
				0,
				!entryIsTransient(entry));

	if (wlIsClient() && entry->base_entry_data.cell && entry->base_entry_data.cell->region && entry->filter.shapeGroup == WC_SHAPEGROUP_EDITOR_ONLY)
	{
		Mat4 inv_world_mat;
		invertMat4(bounds->world_matrix, inv_world_mat);
		resetDebris(iPartitionIdx, entry->base_entry_data.cell->region->root_world_cell, world_min, world_max, bounds, shared_bounds, inv_world_mat);
		resetDebris(iPartitionIdx, entry->base_entry_data.cell->region->temp_world_cell, world_min, world_max, bounds, shared_bounds, inv_world_mat);
	}
}

void openWorldCollisionEntryAll(WorldCollisionEntry *entry)
{
	int i;
	for(i=eaSize(&world_grid.eaWorldColls)-1; i>=0; --i) {
		if (world_grid.eaWorldColls[i]) {
			openWorldCollisionEntry(i, entry);
		}
	}
}

void closeWorldCollisionEntry(int iPartitionIdx, WorldCollisionEntry *entry)
{
	WorldCollObject *wco;

	wco = eaGet(&entry->eaCollObjects, iPartitionIdx);
	if (wco) {
		wcoDestroy(&entry->eaCollObjects[iPartitionIdx]);
	}
}
