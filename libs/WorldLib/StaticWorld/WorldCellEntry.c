/***************************************************************************



***************************************************************************/

#include "WorldCellEntry.h"
#include "WorldCellEntryPrivate.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldCell.h"
#include "WorldCellClustering.h"
#include "wlState.h"
#include "wlGroupPropertyStructs.h"
#include "PhysicsSDK.h"
#include "WorldColl.h"
#include "ScratchStack.h"
#include "wlVolumes.h"
#include "wlDebrisField.h"
#include "wlEncounter.h"
#include "dynFxInfo.h"
#include "dynWind.h"
#include "grouputil.h"
#include "bounds.h"
#include "timing.h"
#include "rgb_hsv.h"
#include "RoomConn.h"
#include "EString.h"
#include "strings_opt.h"
#include "rand.h"
#include "HashFunctions.h"
#include "qsortG.h"
#include "wininclude.h"
#include "Color.h"
#include "partition_enums.h"

#include "WorldCellEntry_h_ast.h"
#include "wlGroupPropertyStructs_h_ast.h"

#include "Quat.h"
#include "Expression.h"

#include "wlAutoLOD.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#define BINERROR(exp) if (!(exp)) {Errorf("Error in binning (function %s), let Conor know.  %s", __FUNCTION__, #exp); if (IsDebuggerPresent()) _DbgBreak();}
#define DRAWERROR(exp) if (!(exp)) {Errorf("Bad drawable bounds or matrix found (function %s, def %s), let Conor or Shawn (if this is a Genesis map) know.  %s", __FUNCTION__, (def && def->name_str) ? def->name_str : "(NULL)", #exp); if (IsDebuggerPresent()) _DbgBreak();}

static int total_shared_bounds_count_no_zmap;

// List of world drawable entries which are overridden by the client
ClientOverridenWorldDrawableEntry **g_ppClientOverridenWorldDrawableEntries = NULL;

// I believe this should never fail. If this assert fires it probably indicates a leaking scope name. -- TomY
void worldDebugValidateScope(WorldScope *scope)
{
	GroupDef *def = scope->def;
	if (def && scope->name_to_obj)
	{
		FOR_EACH_IN_STASHTABLE2(scope->name_to_obj, elem)
		{
			const char *key1 = stashElementGetStringKey(elem);
			bool found = false;
			if (def->name_to_path)
			{
				FOR_EACH_IN_STASHTABLE2(def->name_to_path, elem2)
				{
					if (stricmp(stashElementGetStringKey(elem2), key1) == 0)
					{
						found = true;
						break;
					}
				}
				FOR_EACH_END;
			}
			assert(found);
		}
		FOR_EACH_END;
	}

	FOR_EACH_IN_EARRAY(scope->sub_scopes, WorldScope, sub_scope)
	{
		worldDebugValidateScope(sub_scope);
	}
	FOR_EACH_END;
}

__forceinline static WorldRegion *getWorldRegion(ZoneMapLayer *layer, const Vec3 world_mid)
{
	return layer ? layerGetWorldRegion(layer) : worldGetWorldRegionByPos(world_mid);
}

WorldCellEntryData *worldCellEntryGetData(WorldCellEntry *entry)
{
	// this is ugly, but it causes fewer cache misses if this data is at the end of the structs instead of the beginning
	switch (entry->type)
	{
		xcase WCENT_VOLUME:
			return &((WorldVolumeEntry *)entry)->base_entry_data;

		xcase WCENT_COLLISION:
			return &((WorldCollisionEntry *)entry)->base_entry_data;

		xcase WCENT_ALTPIVOT:
			return &((WorldAltPivotEntry *)entry)->base_entry_data;

		xcase WCENT_INTERACTION:
			return &((WorldInteractionEntry *)entry)->base_entry_data;

		xcase WCENT_SOUND:
			return &((WorldSoundEntry *)entry)->base_entry_data;

		xcase WCENT_LIGHT:
			return &((WorldLightEntry *)entry)->base_entry_data;

		xcase WCENT_FX:
			return &((WorldFXEntry *)entry)->base_entry_data;

		xcase WCENT_ANIMATION:
			return &((WorldAnimationEntry *)entry)->base_entry_data;

		xcase WCENT_MODEL:
			return &((WorldModelEntry *)entry)->base_entry_data;

		xcase WCENT_MODELINSTANCED:
			return &((WorldModelInstanceEntry *)entry)->base_entry_data;

		xcase WCENT_SPLINE:
			return &((WorldSplinedModelEntry *)entry)->base_entry_data;

 		xcase WCENT_OCCLUSION:
 			return &((WorldOcclusionEntry *)entry)->base_entry_data;

		xcase WCENT_WIND_SOURCE:
			return &((WorldWindSourceEntry *)entry)->base_entry_data;
	}

	assertmsgf(0, "Trying to get a WorldCellEntryData on an invalid type. Type attempted: %d", entry->type);

	return NULL;
}

static U32 hashSharedBounds(const WorldCellEntrySharedBounds *shared_bounds, int hashSeed)
{
	U32 hash = hashSeed;

	if (shared_bounds->use_model_bounds)
	{
		hash = burtlehash2((void *)&shared_bounds->model_scale[0], 3, hash);
	}
	else
	{
		hash = burtlehash2((void *)&shared_bounds->radius, 1, hash);
		hash = burtlehash2((void *)&shared_bounds->local_min[0], 3, hash);
		hash = burtlehash2((void *)&shared_bounds->local_max[0], 3, hash);
	}
	if (shared_bounds->model)
		hash = burtlehash(shared_bounds->model->name, (int)strlen(shared_bounds->model->name), hash);
	hash = burtlehash2((void *)&shared_bounds->near_lod_near_dist, 1, hash);
	hash = burtlehash2((void *)&shared_bounds->far_lod_near_dist, 1, hash);
	hash = burtlehash2((void *)&shared_bounds->far_lod_far_dist, 1, hash);
	return hash;
}

static int cmpSharedBounds(const WorldCellEntrySharedBounds *shared_bounds1, const WorldCellEntrySharedBounds *shared_bounds2)
{
	intptr_t pt;
	int t;

	t = (int)shared_bounds1->use_model_bounds - (int)shared_bounds2->use_model_bounds;
	if (t)
		return SIGN(t);

	if (shared_bounds1->use_model_bounds)
	{
		t = cmpVec3XYZ(shared_bounds1->model_scale, shared_bounds2->model_scale);
		if (t)
			return t;
	}
	else
	{
		t = (int)shared_bounds1->radius - (int)shared_bounds2->radius;
		if (t)
			return SIGN(t);

		t = cmpVec3XYZ(shared_bounds1->local_min, shared_bounds2->local_min);
		if (t)
			return t;

		t = cmpVec3XYZ(shared_bounds1->local_max, shared_bounds2->local_max);
		if (t)
			return t;
	}

	pt = (intptr_t)shared_bounds1->model - (intptr_t)shared_bounds2->model;
	if (pt)
		return SIGN(pt);

	t = (int)shared_bounds1->near_lod_near_dist - (int)shared_bounds2->near_lod_near_dist;
	if (t)
		return SIGN(t);

	t = (int)shared_bounds1->far_lod_near_dist - (int)shared_bounds2->far_lod_near_dist;
	if (t)
		return SIGN(t);

	t = (int)shared_bounds1->far_lod_far_dist - (int)shared_bounds2->far_lod_far_dist;
	if (t)
		return SIGN(t);

	return 0;
}

static int cmpSharedBoundsUID(const WorldCellEntrySharedBounds **pbounds1, const WorldCellEntrySharedBounds **pbounds2)
{
	int unique1 = (*pbounds1)->ref_count <= 2;
	int unique2 = (*pbounds2)->ref_count <= 2;
	if (unique1 != unique2)
		return unique1 - unique2;
	return (int)(*pbounds1)->uid - (int)(*pbounds2)->uid;
}

// returns model_radius
static F32 extractModelScale(const Model *model, const Vec3 local_min, const Vec3 local_max, Vec3 model_scale, Vec3 model_min, Vec3 model_max)
{
	if (model)
	{
		Vec3 min_scale, max_scale, model_mid;

		// extract scale and quantize
		setVec3(min_scale, 
			(local_min[0] && model->min[0]) ? (local_min[0] / model->min[0]) : ((local_max[0] && model->max[0]) ? (local_max[0] / model->max[0]) : 1),
			(local_min[1] && model->min[1]) ? (local_min[1] / model->min[1]) : ((local_max[1] && model->max[1]) ? (local_max[1] / model->max[1]) : 1),
			(local_min[2] && model->min[2]) ? (local_min[2] / model->min[2]) : ((local_max[2] && model->max[2]) ? (local_max[2] / model->max[2]) : 1));
		setVec3(max_scale, 
			(local_max[0] && model->max[0]) ? (local_max[0] / model->max[0]) : ((local_min[0] && model->min[0]) ? (local_min[0] / model->min[0]) : 1),
			(local_max[1] && model->max[1]) ? (local_max[1] / model->max[1]) : ((local_min[1] && model->min[1]) ? (local_min[1] / model->min[1]) : 1),
			(local_max[2] && model->max[2]) ? (local_max[2] / model->max[2]) : ((local_min[2] && model->min[2]) ? (local_min[2] / model->min[2]) : 1));

//		groupQuantizeScale(min_scale, min_scale);
//		groupQuantizeScale(max_scale, max_scale);

		if (nearSameVec3Tol(min_scale, max_scale, 0.001f))
			copyVec3(max_scale, model_scale);
		else
			setVec3same(model_scale, 1);

		mulVecVec3(model->min, model_scale, model_min);
		mulVecVec3(model->max, model_scale, model_max);
		return boxCalcMid(model_min, model_max, model_mid);
	}

	return 0;
}

WorldCellEntrySharedBounds *createSharedBounds(ZoneMap *zmap, const Model *model, const Vec3 local_min, const Vec3 local_max, F32 radius, F32 near_lod_near_dist, F32 far_lod_near_dist, F32 far_lod_far_dist)
{
	WorldCellEntrySharedBounds *shared_bounds = StructAlloc(parse_WorldCellEntrySharedBounds);
	Vec3 model_scale, model_min, model_max;
	F32 model_radius;

	model_radius = extractModelScale(model, local_min, local_max, model_scale, model_min, model_max);

	if (model && nearSameVec3(model_min, local_min) && nearSameVec3(model_max, local_max))
	{
		copyVec3(model_scale, shared_bounds->model_scale);
		copyVec3(model_min, shared_bounds->local_min);
		copyVec3(model_max, shared_bounds->local_max);
		shared_bounds->radius = model_radius;
		shared_bounds->use_model_bounds = 1;
	}
	else
	{
		setVec3same(shared_bounds->model_scale, 0);
		setVec3(shared_bounds->local_min, quantBoundsMin(local_min[0]), quantBoundsMin(local_min[1]), quantBoundsMin(local_min[2]));
		setVec3(shared_bounds->local_max, quantBoundsMax(local_max[0]), quantBoundsMax(local_max[1]), quantBoundsMax(local_max[2]));
		shared_bounds->radius = quantBoundsMax(radius);
	}

	shared_bounds->model = model;
	MAX1F(near_lod_near_dist, 0);
	shared_bounds->near_lod_near_dist = quantBoundsMin(near_lod_near_dist);
	shared_bounds->far_lod_near_dist = quantBoundsMax(far_lod_near_dist);
	shared_bounds->far_lod_far_dist = quantBoundsMax(far_lod_far_dist);

	if (zmap)
	{
		if (!zmap->world_cell_data.shared_bounds_hash_table)
			zmap->world_cell_data.shared_bounds_hash_table = stashTableCreateExternalFunctions(4096, StashDefault, hashSharedBounds, cmpSharedBounds);

		shared_bounds->uid = zmap->world_cell_data.shared_bounds_uid_counter++;
		shared_bounds->zmap = zmap;
		
		++zmap->world_cell_data.total_shared_bounds_count;
	}
	else
	{
		++total_shared_bounds_count_no_zmap;
	}

	return shared_bounds;
}

WorldCellEntrySharedBounds *createSharedBoundsCopy(WorldCellEntrySharedBounds *shared_bounds_src, bool remove_ref)
{
	WorldCellEntrySharedBounds *shared_bounds = StructAlloc(parse_WorldCellEntrySharedBounds);
	ZoneMap *zmap = shared_bounds_src->zmap;

	memcpy(shared_bounds, shared_bounds_src, sizeof(WorldCellEntrySharedBounds));
	shared_bounds->ref_count = 0;

	if (zmap)
	{
		if (!zmap->world_cell_data.shared_bounds_hash_table)
			zmap->world_cell_data.shared_bounds_hash_table = stashTableCreateExternalFunctions(4096, StashDefault, hashSharedBounds, cmpSharedBounds);

		shared_bounds->uid = zmap->world_cell_data.shared_bounds_uid_counter++;

		++zmap->world_cell_data.total_shared_bounds_count;
	}
	else
	{
		shared_bounds->uid = 0;
		++total_shared_bounds_count_no_zmap;
	}

	if (remove_ref)
		removeSharedBoundsRef(shared_bounds_src);

	return shared_bounds;
}

WorldCellEntrySharedBounds *createSharedBoundsSphere(ZoneMap *zmap, F32 radius, F32 near_lod_near_dist, F32 far_lod_near_dist, F32 far_lod_far_dist)
{
	Vec3 local_min, local_max;
	setVec3same(local_min, -radius);
	setVec3same(local_max, radius);
	return createSharedBounds(zmap, NULL, local_min, local_max, radius, near_lod_near_dist, far_lod_near_dist, far_lod_far_dist);
}

void setSharedBoundsRadius(WorldCellEntrySharedBounds *shared_bounds, F32 radius)
{
	assert(!shared_bounds->ref_count);
	shared_bounds->radius = quantBoundsMax(radius);
}

void setSharedBoundsMinMax(WorldCellEntrySharedBounds *shared_bounds, const Model *model, const Vec3 local_min, const Vec3 local_max)
{
	Vec3 model_scale, model_min, model_max;
	F32 model_radius;

	assert(!shared_bounds->ref_count);
	if (shared_bounds->model && !model)
		model = shared_bounds->model;
	
	model_radius = extractModelScale(model, local_min, local_max, model_scale, model_min, model_max);
	if (model && nearSameVec3(model_min, local_min) && nearSameVec3(model_max, local_max))
	{
		copyVec3(model_scale, shared_bounds->model_scale);
		copyVec3(model_min, shared_bounds->local_min);
		copyVec3(model_max, shared_bounds->local_max);
		shared_bounds->radius = model_radius;
		shared_bounds->use_model_bounds = 1;
	}
	else
	{
		setVec3same(shared_bounds->model_scale, 0);
		setVec3(shared_bounds->local_min, quantBoundsMin(local_min[0]), quantBoundsMin(local_min[1]), quantBoundsMin(local_min[2]));
		setVec3(shared_bounds->local_max, quantBoundsMax(local_max[0]), quantBoundsMax(local_max[1]), quantBoundsMax(local_max[2]));
		shared_bounds->use_model_bounds = 0;
	}
	shared_bounds->model = model;
}

void setSharedBoundsSphere(WorldCellEntrySharedBounds *shared_bounds, F32 radius)
{
	Vec3 local_min, local_max;
	assert(!shared_bounds->ref_count);
	setVec3same(local_min, -radius);
	setVec3same(local_max, radius);
	setSharedBoundsRadius(shared_bounds, radius);
	setSharedBoundsMinMax(shared_bounds, NULL, local_min, local_max);
}

void setSharedBoundsVisDist(WorldCellEntrySharedBounds *shared_bounds, F32 near_lod_near_dist, F32 far_lod_near_dist, F32 far_lod_far_dist)
{
	assert(!shared_bounds->ref_count);
	shared_bounds->near_lod_near_dist = quantBoundsMin(near_lod_near_dist);
	shared_bounds->far_lod_near_dist = quantBoundsMax(far_lod_near_dist);
	shared_bounds->far_lod_far_dist = quantBoundsMax(far_lod_far_dist);
}

void setSharedBoundsVisDistSame(WorldCellEntrySharedBounds *shared_bounds, F32 vis_dist)
{
	assert(!shared_bounds->ref_count);
	shared_bounds->near_lod_near_dist = 0;
	shared_bounds->far_lod_near_dist = shared_bounds->far_lod_far_dist = quantBoundsMax(vis_dist);
}

static void freeSharedBounds(SA_PRE_NN_VALID SA_POST_P_FREE WorldCellEntrySharedBounds *shared_bounds, bool remove_from_hash_table)
{
	if (shared_bounds->zmap)
	{
		assert(shared_bounds->ref_count == 0);
		--shared_bounds->zmap->world_cell_data.total_shared_bounds_count;
		if (remove_from_hash_table)
			stashRemovePointer(shared_bounds->zmap->world_cell_data.shared_bounds_hash_table, shared_bounds, NULL);
	}
	else
	{
		--total_shared_bounds_count_no_zmap;
	}

	StructDestroy(parse_WorldCellEntrySharedBounds, shared_bounds);
}

WorldCellEntrySharedBounds *lookupSharedBounds(ZoneMap *zmap, WorldCellEntrySharedBounds *shared_bounds)
{
	WorldCellEntrySharedBounds *existing_shared_bounds = NULL;

	if (!shared_bounds->use_model_bounds)
	{
		setVec3(shared_bounds->local_min, quantBoundsMin(shared_bounds->local_min[0]), quantBoundsMin(shared_bounds->local_min[1]), quantBoundsMin(shared_bounds->local_min[2]));
		setVec3(shared_bounds->local_max, quantBoundsMax(shared_bounds->local_max[0]), quantBoundsMax(shared_bounds->local_max[1]), quantBoundsMax(shared_bounds->local_max[2]));
		shared_bounds->radius = quantBoundsMax(shared_bounds->radius);
	}
	shared_bounds->near_lod_near_dist = quantBoundsMin(shared_bounds->near_lod_near_dist);
	shared_bounds->far_lod_near_dist = quantBoundsMax(shared_bounds->far_lod_near_dist);
	shared_bounds->far_lod_far_dist = quantBoundsMax(shared_bounds->far_lod_far_dist);
	assert(shared_bounds->zmap == zmap);
	assert(zmap);

	if (stashFindPointer(zmap->world_cell_data.shared_bounds_hash_table, shared_bounds, &existing_shared_bounds))
	{
		assert(shared_bounds != existing_shared_bounds);
		freeSharedBounds(shared_bounds, false);
		shared_bounds = existing_shared_bounds;
	}
	else
	{
		stashAddPointer(zmap->world_cell_data.shared_bounds_hash_table, shared_bounds, shared_bounds, false);
	}

	++shared_bounds->ref_count;

	return shared_bounds;
}

WorldCellEntrySharedBounds *lookupSharedBoundsFromParsed(ZoneMap *zmap, WorldCellEntrySharedBounds *shared_bounds)
{
	const char *modelname = worldGetStreamedString(zmap->world_cell_data.streaming_pooled_info, shared_bounds->model_idx);
	
	shared_bounds->model = modelname ? modelFind(modelname, false, WL_FOR_WORLD) : NULL;
	if (shared_bounds->model && shared_bounds->use_model_bounds)
	{
		Vec3 local_mid;
		mulVecVec3(shared_bounds->model->min, shared_bounds->model_scale, shared_bounds->local_min);
		mulVecVec3(shared_bounds->model->max, shared_bounds->model_scale, shared_bounds->local_max);
		shared_bounds->radius = boxCalcMid(shared_bounds->local_min, shared_bounds->local_max, local_mid);
	}

	if (!zmap->world_cell_data.shared_bounds_hash_table)
		zmap->world_cell_data.shared_bounds_hash_table = stashTableCreateExternalFunctions(4096, StashDefault, hashSharedBounds, cmpSharedBounds);

	shared_bounds->ref_count = 0;
	shared_bounds->uid = zmap->world_cell_data.shared_bounds_uid_counter++;
	shared_bounds->zmap = zmap;

	++zmap->world_cell_data.total_shared_bounds_count;

	return lookupSharedBounds(zmap, shared_bounds);
}

static void addSharedBoundsRef(WorldCellEntrySharedBounds *shared_bounds)
{
	shared_bounds->ref_count++;
}

void removeSharedBoundsRef(WorldCellEntrySharedBounds *shared_bounds)
{
	if (!shared_bounds)
		return;
	shared_bounds->ref_count--;
	if (shared_bounds->ref_count <= 0)
		freeSharedBounds(shared_bounds, true);
}

void **maintainOldIndices(void **new_struct_array_from_bins, void **old_struct_array_from_bins, int (*cmpFunc)(const void *, const void *), F32 (*distFunc)(const void *, const void *), F32 max_distance)
{
	// attempt to keep the array indices the same as in the old file
	void **new_array = NULL, **unmatched_old_structs = NULL, **unmatched_new_structs = NULL, **old_structs;
	int *indices, *unmatched_old_struct_indices = NULL;
	F32 *distances;
	int i, j;

	if (!eaSize(&old_struct_array_from_bins))
		return new_struct_array_from_bins;

	for (i = 0; i < eaSize(&old_struct_array_from_bins); ++i)
	{
		ANALYSIS_ASSUME(old_struct_array_from_bins);

		eaPush(&unmatched_old_structs, old_struct_array_from_bins[i]);
		eaiPush(&unmatched_old_struct_indices, i);

		for (j = 0; j < eaSize(&new_struct_array_from_bins); ++j)
		{
			if (new_struct_array_from_bins[j] && cmpFunc(new_struct_array_from_bins[j], old_struct_array_from_bins[i]) == 0)
			{
				eaSet(&new_array, new_struct_array_from_bins[j], i);
				new_struct_array_from_bins[j] = NULL;
				eaPop(&unmatched_old_structs);
				eaiPop(&unmatched_old_struct_indices);
				break;
			}
		}
	}

	for (j = 0; j < eaSize(&new_struct_array_from_bins); ++j)
	{
		if (new_struct_array_from_bins[j])
			eaPush(&unmatched_new_structs, new_struct_array_from_bins[j]);
	}

	eaDestroy(&new_struct_array_from_bins);

	if (eaSize(&unmatched_new_structs) > 200)
	{
		// too many to try to find closest matches, just fill in the holes
		i = 0;
		for (j = 0; j < eaSize(&unmatched_new_structs); ++j)
		{
			for (; i < eaSize(&new_array); ++i)
			{
				if (!new_array[i])
					break;
			}
			eaSet(&new_array, unmatched_new_structs[j], i);
		}
	}
	else
	{
		// these will be popped off in reverse order below, so reverse the earray here
		eaReverse(&unmatched_new_structs);

		distances = _alloca(eaSize(&unmatched_new_structs) * sizeof(F32));
		indices = _alloca(eaSize(&unmatched_new_structs) * sizeof(int));
		old_structs = _alloca(eaSize(&unmatched_new_structs) * sizeof(void *));

		while (eaSize(&unmatched_new_structs))
		{
			int n = eaSize(&unmatched_new_structs) - 1, idx;

			for (i = 0; i < eaSize(&unmatched_new_structs); ++i)
			{
				distances[i] = max_distance;
				indices[i] = -1;
				old_structs[i] = NULL;
				for (j = 0; j < eaSize(&unmatched_old_structs); ++j)
				{
					F32 dist = distFunc(unmatched_new_structs[i], unmatched_old_structs[j]);
					if (dist < distances[i])
					{
						distances[i] = dist;
						indices[i] = unmatched_old_struct_indices[j]; // index in old array
						old_structs[i] = unmatched_old_structs[j];
					}
				}
			}

			// insertion sort unmatched new bounds by distance to closest match
			for (i = 1; i < eaSize(&unmatched_new_structs); ++i)
			{
				for (j = i; j > 0 && distances[j-1] <= distances[j]; --j)
				{
					if (distances[j-1] == distances[j] && (indices[j-1] > indices[j] || indices[j-1] < 0))
						break;
					eaSwap(&unmatched_new_structs, j, j-1);
					SWAPF32(distances[j], distances[j-1]);
					SWAP32(indices[j], indices[j-1]);
					SWAPP(old_structs[j], old_structs[j-1]);
				}
			}

			// place closest match
			if (indices[n] >= 0)
			{
				assert(!eaGet(&new_array, indices[n]));
				eaSet(&new_array, unmatched_new_structs[n], indices[n]);
			}
			else
			{
				// search for a hole
				for (i = 0; i < eaSize(&new_array); ++i)
				{
					if (!new_array[i])
						break;
				}
				eaSet(&new_array, unmatched_new_structs[n], i);
			}

			idx = eaFindAndRemoveFast(&unmatched_old_structs, old_structs[n]);
			eaiRemoveFast(&unmatched_old_struct_indices, idx);
			eaPop(&unmatched_new_structs);
		}
	}

	// remove holes, filling from the end to maintain as many indices as possible
	for (i = 0; i < eaSize(&new_array); ++i)
	{
		if (!new_array[i])
		{
			eaRemoveFast(&new_array, i);
			--i;
		}
	}

	eaDestroy(&unmatched_old_structs);
	eaiDestroy(&unmatched_old_struct_indices);
	eaDestroy(&unmatched_new_structs);

	return new_array;
}

static F32 distSharedBounds(const WorldCellEntrySharedBounds *shared_bounds1, const WorldCellEntrySharedBounds *shared_bounds2)
{
	F32 dist = 0;

	if (shared_bounds1->model != shared_bounds2->model)
		return 8e16;

	if (shared_bounds1->use_model_bounds != shared_bounds2->use_model_bounds)
		return 8e16;

	if (!shared_bounds1->use_model_bounds)
	{
		dist += ABS(shared_bounds1->radius - shared_bounds2->radius);
		dist += distance3(shared_bounds1->local_min, shared_bounds2->local_min);
		dist += distance3(shared_bounds1->local_max, shared_bounds2->local_max);
	}
	else
	{
		dist += 10.f * distance3(shared_bounds1->model_scale, shared_bounds2->model_scale);
	}

	dist += ABS(shared_bounds1->near_lod_near_dist - shared_bounds2->near_lod_near_dist);
	dist += ABS(shared_bounds1->far_lod_near_dist - shared_bounds2->far_lod_near_dist);
	dist += ABS(shared_bounds1->far_lod_far_dist - shared_bounds2->far_lod_far_dist);

	return dist * (shared_bounds1->use_model_bounds ? 0.1f : 1.f);
}

void getAllSharedBounds(ZoneMap *zmap, WorldCellEntrySharedBounds ***shared_bounds_array, WorldStreamingPooledInfo *old_pooled_info)
{
	StashTableIterator iter;
	StashElement elem;
	int i, initial_size = eaSize(shared_bounds_array);

	if (!zmap->world_cell_data.shared_bounds_hash_table)
		return;

	stashGetIterator(zmap->world_cell_data.shared_bounds_hash_table, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		WorldCellEntrySharedBounds *shared_bounds = stashElementGetPointer(elem);
		if (shared_bounds && eaFind(shared_bounds_array, shared_bounds) < 0)
		{
			eaPush(shared_bounds_array, shared_bounds);
			++shared_bounds->ref_count;
		}
	}

	qsortG((*shared_bounds_array) + initial_size, eaSize(shared_bounds_array) - initial_size, sizeof(WorldCellEntrySharedBounds *), cmpSharedBoundsUID);

	if (old_pooled_info && old_pooled_info->packed_info)
	{
		for (i = 0; i < eaSize(&old_pooled_info->packed_info->shared_bounds); ++i)
		{
			WorldCellEntrySharedBounds *old_shared_bounds;
			const char *modelname;

			ANALYSIS_ASSUME(old_pooled_info->packed_info->shared_bounds);
			old_shared_bounds = old_pooled_info->packed_info->shared_bounds[i];

			modelname = worldGetStreamedString(old_pooled_info, old_shared_bounds->model_idx);
			old_shared_bounds->model = modelname ? modelFind(modelname, false, WL_FOR_WORLD) : NULL;
		}

		*shared_bounds_array = (WorldCellEntrySharedBounds**)maintainOldIndices(*shared_bounds_array, old_pooled_info->packed_info->shared_bounds, 
																				cmpSharedBounds, distSharedBounds, 200);
	}
}

void worldCellEntryResetSharedBounds(ZoneMap *zmap)
{
	assert(!zmap->world_cell_data.total_shared_bounds_count);
	stashTableDestroy(zmap->world_cell_data.shared_bounds_hash_table);
	zmap->world_cell_data.shared_bounds_hash_table = NULL;
	zmap->world_cell_data.shared_bounds_uid_counter = 0;
}

//////////////////////////////////////////////////////////////////////////

static void worldAddOrReaddCellEntryForInit(GroupInfo *info, WorldCellEntry *entry) {

	worldRemoveCellEntry(entry);
	worldAddCellEntry(info->region, entry);

	if (info->parent_entry)
	{
		WorldInteractionEntry *parent_entry;

		for (parent_entry = info->parent_entry; parent_entry; parent_entry = parent_entry->base_entry_data.parent_entry)
		{
			int fail_index = 0;
			while (!validateInteractionChildren(parent_entry, &fail_index))
			{
				// move parent up one cell level
				WorldCell *old_cell = worldRemoveCellEntryEx(&parent_entry->base_entry, true, false);

				assert(old_cell->region == info->region);
				assert(old_cell != info->region->root_world_cell);
				assert(old_cell);
				assert(old_cell->parent);

				// move entry to new cell
				worldAddCellEntryToCell(old_cell->parent, &parent_entry->base_entry, &parent_entry->base_entry_data, false);
			}
		}
	}
}

void worldEntryInit(WorldCellEntry *entry, GroupInfo *info, WorldCellEntry ***cell_entry_list, GroupDef *def, Room *room, bool making_bins)
{
	WorldCellEntryData *data;

	if (!entry)
		return;

	assert(entry->shared_bounds);

	DRAWERROR(FINITEVEC3(entry->bounds.world_matrix[0]));
	DRAWERROR(FINITEVEC3(entry->bounds.world_matrix[1]));
	DRAWERROR(FINITEVEC3(entry->bounds.world_matrix[2]));
	DRAWERROR(FINITEVEC3(entry->bounds.world_matrix[3]));
	DRAWERROR(FINITEVEC3(entry->bounds.world_mid));
	DRAWERROR(FINITEVEC3(entry->shared_bounds->local_min));
	DRAWERROR(FINITEVEC3(entry->shared_bounds->local_max));
	DRAWERROR(validateMat4(entry->bounds.world_matrix));
	DRAWERROR(isNonZeroMat3(entry->bounds.world_matrix));

	if (cell_entry_list)
		eaPush(cell_entry_list, entry);

	addChildToInteractionEntry(entry, info->parent_entry, info->parent_entry_child_idx);

	data = worldCellEntryGetData(entry);
	data->group_id = info->layer_id_bits; 	// The group ID is incorrect and not stored here.
											// If we ever need this, we will have to add a seperate layer bits field
											// and put the correct value here (def->name_uid). Then we can also lose the
											// masks
	entry->bounds.object_tag = info->tag_id;

	assert(!entry->shared_bounds->ref_count);
	entry->shared_bounds = lookupSharedBounds(info->zmap, entry->shared_bounds);

	if (making_bins && info->region->root_world_cell && info->layer)
	{
		F32 *world_mid;
		if (entry->type > WCENT_BEGIN_DRAWABLES)
			world_mid = ((WorldDrawableEntry *)entry)->world_fade_mid;
		else
			world_mid = entry->bounds.world_mid;
		if (!info->region->root_world_cell->cell_bounds_invalid)
		{
			IVec3 grid_pos;
			worldPosToGridPos(world_mid, grid_pos, CELL_BLOCK_SIZE);
			if (!gridPosInRange(grid_pos, &info->region->root_world_cell->cell_block_range))
			{
				Vec3 midpoint;

				if (def)
					ErrorFilenamef(info->layer->filename, "GroupDef \"%s\" (%d) is outside the bounds of its region", def->name_str, def->name_uid);
				else if (room)
					ErrorFilenamef(info->layer->filename, "Room \"%s\" is outside the bounds of its region", room->def_name);
				else
					ErrorFilenamef(info->layer->filename, "Unknown object is outside the bounds of its region");

				// Fix up the midpoint so we don't just crash later
				addVec3(info->region->world_bounds.world_min, info->region->world_bounds.world_max, midpoint);
				if (entry->type > WCENT_BEGIN_DRAWABLES)
					scaleVec3(midpoint, 0.5f, ((WorldDrawableEntry *)entry)->world_fade_mid);
				else
					scaleVec3(midpoint, 0.5f, entry->bounds.world_mid);
			}
		}
	}

	worldAddOrReaddCellEntryForInit(info, entry);
}

static ZoneMapLayer *getLayer(const ZoneMap *zmap, WorldStreamingInfo *streaming_info, int layer_idx)
{
	char *layer_name;
	int j;

	if (layer_idx < 0 || layer_idx >= eaSize(&streaming_info->layer_names))
		return NULL;

	layer_name = streaming_info->layer_names[layer_idx];

	for (j = 0; j < eaSize(&zmap->layers); ++j)
	{
		ZoneMapLayer *layer = zmap->layers[j];
		if (!layer)
			continue;
		if (stricmp(layerGetFilename(layer), layer_name)==0)
			return layer;
	}

	return NULL;
}

static void copyBoundsFromParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldCellEntryBounds *bounds, const WorldCellEntrySharedBounds *shared_bounds, const WorldCellEntryBoundsParsed *bounds_parsed)
{
	Vec3 local_mid;

	bounds->object_tag = bounds_parsed->object_tag;
	copyMat4(bounds_parsed->world_matrix, bounds->world_matrix);

	assert(bounds_parsed->local_mid_idx < eaSize(&streaming_pooled_info->packed_info->shared_local_mids));

	if (bounds_parsed->local_mid_idx < 0)
		centerVec3(shared_bounds->local_min, shared_bounds->local_max, local_mid);
	else
		copyVec3(streaming_pooled_info->packed_info->shared_local_mids[bounds_parsed->local_mid_idx]->local_mid, local_mid);

	mulVecMat4(local_mid, bounds->world_matrix, bounds->world_mid);
}

void worldEntryInitBoundsFromParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldCellEntry *entry, WorldCellEntryParsed *entry_parsed)
{
	WorldCellEntryData *entry_data = worldCellEntryGetData(entry);

	entry_data->group_id = entry_parsed->group_id;

	assert(entry_parsed->shared_bounds_idx < eaSize(&streaming_pooled_info->packed_info->shared_bounds));
	ANALYSIS_ASSUME(streaming_pooled_info->packed_info->shared_bounds);
	entry->shared_bounds = streaming_pooled_info->packed_info->shared_bounds[entry_parsed->shared_bounds_idx];
	addSharedBoundsRef(entry->shared_bounds);

	copyBoundsFromParsed(streaming_pooled_info, &entry->bounds, entry->shared_bounds, &entry_parsed->entry_bounds);

	BINERROR(FINITEVEC3(entry->bounds.world_matrix[0]));
	BINERROR(FINITEVEC3(entry->bounds.world_matrix[1]));
	BINERROR(FINITEVEC3(entry->bounds.world_matrix[2]));
	BINERROR(FINITEVEC3(entry->bounds.world_matrix[3]));
	BINERROR(fabs(mat3Determinant(entry->bounds.world_matrix)) > 0.001f);
	BINERROR(FINITEVEC3(entry->bounds.world_mid));
	BINERROR(FINITEVEC3(entry->shared_bounds->local_min));
	BINERROR(FINITEVEC3(entry->shared_bounds->local_max));
	BINERROR(validateMat4(entry->bounds.world_matrix));
	BINERROR(isNonZeroMat3(entry->bounds.world_matrix));
}

void worldEntryInitFromParsed(ZoneMap *zmap, WorldRegion *region, WorldCell *cell, WorldCellEntry *entry, WorldCellEntryParsed *entry_parsed, bool is_near_fade)
{
	WorldCellEntryData *entry_data = worldCellEntryGetData(entry);
	WorldInteractionEntry *parent = worldGetInteractionEntryFromUid(zmap, entry_parsed->parent_entry_uid, true);

	assert(entry->shared_bounds);

	addChildToInteractionEntry(entry, parent, entry_parsed->interaction_child_idx);

	if (cell)
	{
		worldAddCellEntryToCell(cell, entry, entry_data, is_near_fade);
	}
	else
	{
		int layer_idx = (entry_parsed->group_id & GROUP_ID_LAYER_MASK) >> GROUP_ID_LAYER_OFFSET;
		ZoneMapLayer *layer = getLayer(zmap, zmap->world_cell_data.streaming_info, layer_idx);
		worldAddCellEntry(region, entry);
		assert(layer);
		eaPush(&layer->cell_entries, entry);
	}
}

WorldCellWeldedBin *worldWeldedBinInitFromParsed(WorldCell *cell, WorldCellDrawableDataParsed *bin_parsed)
{
	WorldCellWeldedBin *bin = calloc(1, sizeof(WorldCellWeldedBin));
	WorldCellCullHeader *cell_header;

	bin->bin_key = bin; // just to make it unique for now, will be filled in when entries are added
	bin->cell = cell;
	bin->src_type = WCENT_INVALID; // nothing is allowed to join with a parsed bin
	eaPush(&cell->drawable.bins, bin);

	if (cell_header = worldCellGetHeader(cell))
	{
		SAFE_FREE(cell_header->welded_entry_headers);
		cell_header->welded_entries_inited = false;
	}

	return bin;
}

void worldWeldedBinAddEntryFromParsed(WorldCellWeldedBin *bin, WorldCellEntry *entry)
{
	WorldDrawableEntry *draw = (WorldDrawableEntry*)entry;
	WorldCellEntryData* entry_data;

	if (bin->bin_key == bin)
		bin->bin_key = draw->draw_list;
	
	entry_data = worldCellEntryGetData(entry);
	entry_data->cell = bin->cell;
	eaPush(&bin->drawable_entries, draw);
}

void worldEntryUpdateWireframe(WorldCellEntry *entry, GroupTracker *tracker)
{
	WorldDrawableEntry *draw = (WorldDrawableEntry *)entry;

	if (!entry || !tracker || entry->type < WCENT_BEGIN_DRAWABLES)
		return;

	for (draw->wireframe = 0; tracker; tracker = tracker->parent)
	{
		if (tracker->selected)
			draw->wireframe = wl_state.selected_wireframe;
		else if (tracker->wireframe)
			draw->wireframe = tracker->wireframe;

		if (draw->wireframe)
			break;
	}
}

ZoneMapLayer *worldEntryGetLayer(WorldCellEntry *entry)
{
	WorldCellEntryData *data;
	int layer_idx;

	if (!entry)
		return NULL;

	data = worldCellEntryGetData(entry);
	layer_idx = (data->group_id & GROUP_ID_LAYER_MASK) >> GROUP_ID_LAYER_OFFSET;
	return zmapGetLayer(NULL, layer_idx);
}

//////////////////////////////////////////////////////////////////////////

void worldCellEntryApplyTintColor(const GroupInfo *info, Vec4 out_color)
{
	if (info->apply_tint_offset)
	{
		Vec3 color;
		rgbToHsv(info->color, color);
		addVec3(info->tint_offset, color, color);
		hsvMakeLegal(color, false);
		hsvToRgb(color, out_color);
		out_color[3] = info->color[3];
	}
	else
	{
		copyVec4(info->color, out_color);
	}
}

static void initDrawableEntry(WorldDrawableEntry *entry, SA_PARAM_NN_VALID GroupInfo *info, GroupDef *def, GroupTracker *tracker, LightData *light_data, Vec4 tint_color, F32 override_radius, bool editor_only, bool has_volume, bool can_have_animation, bool in_editor, bool recenter_bounds)
{
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	WorldCellEntrySharedBounds *shared_bounds = entry->base_entry.shared_bounds;
	Vec3 local_min, local_max, local_mid;
	F32 radius = info->radius * info->uniform_scale;

	assert(!shared_bounds->ref_count);

	setSharedBoundsRadius(shared_bounds, radius);

	entry->camera_facing = !!(def->property_structs.physical_properties.bCameraFacing);
	entry->axis_camera_facing = !!(def->property_structs.physical_properties.bAxisCameraFacing);
	entry->debug_me = !!SAFE_MEMBER(tracker, debugMe);
	entry->unlit = info->unlit;
	entry->editor_only = editor_only;
	entry->low_detail = info->low_detail;
	entry->high_detail = info->high_detail;
	entry->high_fill_detail = info->high_fill_detail;
	entry->map_snap_hidden = info->map_snap_hidden;
	entry->no_shadow_cast = info->no_shadow_cast;
	entry->no_shadow_receive = info->no_shadow_receive;
	entry->force_trunk_wind = info->force_trunk_wind;
	entry->no_vertex_lighting = info->no_vertex_lighting ? 1 : 0;
	entry->use_character_lighting = info->use_character_lighting ? 1 : 0;

	if (light_data)
		hsvToRgb(light_data->diffuse_hsv, info->color);

	if (info->spline)
	{
		copyVec3(info->world_mid, bounds->world_mid);
		setSharedBoundsMinMax(shared_bounds, def->model, info->world_min, info->world_max);
	}
	else if (override_radius > 0)
	{
		radius = override_radius * info->uniform_scale;
		setSharedBoundsSphere(shared_bounds, radius);
		copyVec3(bounds->world_matrix[3], bounds->world_mid);
	}
	else if (has_volume)
	{
		GroupVolumeProperties *props = def->property_structs.volume;
		if (props->eShape == GVS_Sphere)
		{
			radius = props->fSphereRadius;
			setSharedBoundsSphere(shared_bounds, radius);
			copyVec3(bounds->world_matrix[3], bounds->world_mid);
		}
		else
		{
			setSharedBoundsMinMax(shared_bounds, NULL, props->vBoxMin, props->vBoxMax);
			setSharedBoundsRadius(shared_bounds, boxCalcMid(props->vBoxMin, props->vBoxMax, local_mid));
			mulVecMat4(local_mid, bounds->world_matrix, bounds->world_mid);
		}
		if (entry->base_entry.type != WCENT_OCCLUSION)
		{
			if (in_editor)
				setSharedBoundsVisDistSame(shared_bounds, shared_bounds->radius * WORLD_VOLUME_VISDIST_EDITOR_MULTIPLIER);
			else
				setSharedBoundsVisDistSame(shared_bounds, shared_bounds->radius * WORLD_VOLUME_VISDIST_MULTIPLIER);
		}
	}
	else
	{
		if (def->model)
		{
			if(def->property_structs.physical_properties.bCameraFacing || def->property_structs.physical_properties.bAxisCameraFacing)
			{
				// for camera facing models, we must grow the bounds to contain any possible orientation of the model about its local pivot point (0, 0, 0).
				calcBoundsForAnyOrientation(def->model->min, def->model->max, local_min, local_max);

				mulVecVec3(def->model_scale, local_min, local_min);
				mulVecVec3(def->model_scale, local_max, local_max);
			}
			else
			{
				mulVecVec3(def->model_scale, def->model->min, local_min);
				mulVecVec3(def->model_scale, def->model->max, local_max);
			}

			scaleVec3(local_min, info->uniform_scale, local_min);
			scaleVec3(local_max, info->uniform_scale, local_max);
		}
		else
		{
			scaleVec3(def->bounds.min, info->uniform_scale, local_min);
			scaleVec3(def->bounds.max, info->uniform_scale, local_max);
		}

		if (recenter_bounds)
		{
			Vec3 world_mid_offset;
			boxCalcMid(local_min, local_max, local_mid);
			subVec3(local_min, local_mid, local_min);
			subVec3(local_max, local_mid, local_max);
			mulVecMat3(local_mid, bounds->world_matrix, world_mid_offset);
			addVec3(bounds->world_matrix[3], world_mid_offset, bounds->world_matrix[3]);
		}

		radius = boxCalcMid(local_min, local_max, local_mid);
		mulVecMat4(local_mid, bounds->world_matrix, bounds->world_mid);
		setSharedBoundsSphere(shared_bounds, radius);
		setSharedBoundsMinMax(shared_bounds, def->model, local_min, local_max);
	}

	if (nearSameVec3(bounds->world_matrix[0], zerovec3) ||
		nearSameVec3(bounds->world_matrix[1], zerovec3) ||
		nearSameVec3(bounds->world_matrix[2], zerovec3) ||
		fabs(mat3Determinant(bounds->world_matrix)) < 0.001f)
	{
		Errorf("World cell entry in def %s (%X) has invalid world matrix.", def->name_str, def->name_uid);
		identityMat3(bounds->world_matrix);
	}

	if (tint_color)
		copyVec4(tint_color, entry->color);
	else
		worldCellEntryApplyTintColor(info, entry->color);

	if (info->is_debris)
	{
		MIN1F(entry->color[3], 0.4f);
	}

	if (info->fx_entry)
	{
		setupFXEntryDictionary(info->zmap);
		SET_HANDLE_FROM_INT(info->zmap->world_cell_data.fx_entry_dict_name, info->fx_entry->id, entry->fx_parent_handle);
	}

	if (can_have_animation && info->animation_entry)
	{
		setupWorldAnimationEntryDictionary(info->zmap);
		SET_HANDLE_FROM_INT(info->zmap->world_cell_data.animation_entry_dict_name, info->animation_entry->id, entry->animation_controller_handle);

		worldAnimationEntryModifyBounds(info->animation_entry, 
			bounds->world_matrix, shared_bounds->local_min, shared_bounds->local_max, 
			bounds->world_mid, &radius);

		setSharedBoundsRadius(shared_bounds, radius);
		setSharedBoundsMinMax(shared_bounds, def->model, shared_bounds->local_min, shared_bounds->local_max);

		entry->controller_relative_matrix_inited = worldAnimationEntryInitChildRelativeMatrix(info->animation_entry, bounds->world_matrix, entry->controller_relative_matrix);
	}

	if (info->has_fade_node && !info->ignore_lod_override && !has_volume)
	{
		copyVec3(info->fade_mid, entry->world_fade_mid);
		entry->world_fade_radius = info->fade_radius;
	}
	else
	{
		copyVec3(bounds->world_mid, entry->world_fade_mid);
		entry->world_fade_radius = shared_bounds->radius;
	}

	worldEntryUpdateWireframe(&entry->base_entry, tracker);
}

static InstanceData *worldEntryGetInstanceData(GroupDef *layer_def, const char *unique_name)
{
	InstanceData *ret;
	if (layer_def && layer_def->name_to_instance_data && unique_name && unique_name[0] && stashFindPointer(layer_def->name_to_instance_data, unique_name, &ret))
		return ret;
	else
		return NULL;
}

static void worldEntryAddScopeData(GroupDef **parent_defs, int *idxs_in_parent, WorldScope *starting_scope, const char *base_name, const char *first_unique_name, WorldEncounterObject *data, char **layer_scope_name, bool overwrite)
{
	WorldScope *current_scope = starting_scope;
	int *uids = NULL;
	char *path = NULL;
	char unique_name[256] = "";
	int i, j;

	// set i to current def in def chain
	i = eaSize(&parent_defs) - 1;
	assert(i >= 0);

	if (first_unique_name)
		strcpy(unique_name, first_unique_name);

	// ensure a unique name exists in all parent scope defs
	do 
	{
		GroupNameReturnVal ret = GROUP_NAME_EXISTS;

		// generate path name
		estrClear(&path);
		eaiClear(&uids);

		while (i > 0 && current_scope->def != parent_defs[i])
		{
			GroupChild **def_children = groupGetChildren(parent_defs[i - 1]);
			eaiPush(&uids, def_children[idxs_in_parent[i]]->uid_in_parent);
			i--;
		}
		eaiReverse(&uids);

		for (j = 0; j < eaiSize(&uids); j++)
			estrConcatf(&path, "%i,", uids[j]);
		if (unique_name[0])
			estrConcatf(&path, "%s,", unique_name);

		// generate a unique name if necessary; for logical groups at their closest scope (i.e. empty uids),
		// validate that unique name is not being used yet
		ret = groupDefScopeCreateUniqueName(current_scope->def ? current_scope->def : parent_defs[0], eaiSize(&uids) > 0 ? path : NULL, eaiSize(&uids) > 0 ? base_name : unique_name, SAFESTR(unique_name), !overwrite);

		// create a unique name entry in the current scope def
		if (eaiSize(&uids) > 0)
		{
			if (ret == GROUP_NAME_CREATED)
				groupDefScopeSetPathName(current_scope->def ? current_scope->def : parent_defs[0], path, unique_name, true);
		}

		// add the name lookup into the current scope
		if (data && ret != GROUP_NAME_DUPLICATE)
		{
			WorldLogicalGroupLoc *loc;

			worldScopeAddData(current_scope, unique_name, data);

			// fixup logical groups
			if (!data->parent_group && stashFindPointer(current_scope->name_to_group, unique_name, &loc) && loc->group->common_data.layer == data->layer)
				worldLogicalGroupAddEncounterObject(loc->group, data, loc->index);
		}

		current_scope = current_scope->parent_scope;
	} while (current_scope);

	if (layer_scope_name && unique_name[0])
		estrPrintf(layer_scope_name, "%s", unique_name);

	estrDestroy(&path);
	eaiDestroy(&uids);
}

static void worldEntryAddLogicalGroups(GroupDef *def, GroupTracker *tracker, GroupInfo *info, GroupDef **parent_defs, int *idxs_in_parent)
{
	WorldLogicalGroup **groups = NULL;
	int i, j;

	assert(info->closest_scope);
	for (i = 0; i < eaSize(&def->logical_groups); i++)
	{
		WorldLogicalGroup *group = worldZoneMapScopeAddLogicalGroup(info->zmap->zmap_scope, info->closest_scope, info->layer, tracker, def->logical_groups[i]->properties);

		eaPush(&groups, group);

		// store lookups for contents of logical groups; used whenever children are readded
		for (j = 0; j < eaSize(&def->logical_groups[i]->child_names); j++)
		{
			WorldLogicalGroupLoc *loc = worldLogicalGroupLocCreate(group, j);
			if (!info->closest_scope->name_to_group)
				info->closest_scope->name_to_group = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
			if (!stashAddPointer(info->closest_scope->name_to_group, def->logical_groups[i]->child_names[j], loc, false))
			{
				ErrorFilenamef(def->filename, "Logical group \"%s\" contains child \"%s\", which is already parented to another group.", def->logical_groups[i]->group_name, def->logical_groups[i]->child_names[j]);
				worldLogicalGroupLocDestroy(loc);
			}
		}
	}

	// populate scope stashes with logical group 
	for (i = 0; i < eaSize(&def->logical_groups); i++)
	{
		char base_name[64];
		assert(groups && groups[i]);
		sprintf(base_name, "%s%s", GROUP_UNNAMED_PREFIX, "LogicalGroup");
		worldEntryAddScopeData(parent_defs, idxs_in_parent, info->closest_scope, base_name, def->logical_groups[i]->group_name, (WorldEncounterObject*) groups[i], NULL, false);
	}

	eaDestroy(&groups);
}

// CD_HACK_REMOVEME
static void checkInteractionParentage(WorldCellEntry *entry, GroupDef *def, U32 interaction_class)
{
	static int named_object_bit;
	WorldCellEntryData *data;
	WorldInteractionEntry *parent;

	if (!named_object_bit)
		named_object_bit = wlInteractionClassNameToBitMask("NamedObject");
	
	if (interaction_class == named_object_bit)
		return;

	data = worldCellEntryGetData(entry);
	parent = data->parent_entry;
	while (parent)
	{
		if (parent->base_interaction_properties && parent->base_interaction_properties->eInteractionClass != named_object_bit)
		{
			GroupDef *parent_def = parent->parent_def;
			if (parent_def)
				ErrorFilenamef(parent_def->filename, "%s \"%s\" found as child of clickable or destructible group \"%s\".", interaction_class ? "Clickable or destructible group" : "Debris object", def->name_str, parent_def->name_str);
			break;
		}
		parent = parent->base_entry_data.parent_entry;
	}
}

void worldEntryCreateInteractLocation(GroupInfo *info, GroupDef *def)
{
	if(info && info->parent_entry && info->parent_entry->full_interaction_properties)
	{
		WorldInteractionProperties *pWorldInteractionProperties = info->parent_entry->full_interaction_properties;
		if(pWorldInteractionProperties)
		{
			bool bIsValid = true;
			const WorldInteractLocationProperties *pDefProperties;
			pDefProperties = def->property_structs.interact_location_properties;
			
			if(pDefProperties)
			{
				WorldInteractLocationProperties *pInteractLocProperties;
				pInteractLocProperties = StructCreate(parse_WorldInteractLocationProperties);

				// copy pos & orientation
				copyVec3(info->world_matrix[3],  pInteractLocProperties->vPos);
				mat3ToQuat(info->world_matrix, pInteractLocProperties->qOrientation);

				// copy other properties
				COPY_HANDLE(pInteractLocProperties->hFsm, pDefProperties->hFsm);
				COPY_HANDLE(pInteractLocProperties->hSecondaryFsm, pDefProperties->hSecondaryFsm);
				pInteractLocProperties->iPriority = pDefProperties->iPriority;

				if (pDefProperties->eaAnims)
				{
					FOR_EACH_IN_EARRAY(pDefProperties->eaAnims, const char, pcJobAnim)
					{	
						if (pcJobAnim)
						{
							AIAnimList *pAnimList = RefSystem_ReferentFromString("AIAnimList", pcJobAnim);
							if (!pAnimList)
							{
								Errorf("%s - invalid AnimList %s", def->name_str, pcJobAnim);
							}
							else
							{
								eaPush(&pInteractLocProperties->eaAnims, pcJobAnim);
							}
						}
					}
					FOR_EACH_END
				}
			
				// validate expression
				if(pDefProperties->pIgnoreCond)
				{
					if(!wl_state.ai_static_check_expr_context_func)
					{
						Errorf("Cannot attempt to validate Ambient Job ignore expression");
						bIsValid = false;
					}
					else
					{
						// equivalent to aiGetStaticCheckExprContext()
						ExprContext* staticCheckContext = wl_state.ai_static_check_expr_context_func();

						int iResult = exprGenerate(pDefProperties->pIgnoreCond, staticCheckContext);
						if(!iResult)
						{
							Errorf("%s - invalid Ambient Job ignore expression", def->name_str);
							bIsValid = false;
						}
						else
						{
							pInteractLocProperties->pIgnoreCond = exprClone(pDefProperties->pIgnoreCond);
						}
					}
				} // end if pIgnoreCond

				if(bIsValid)
				{
					eaPush(&pWorldInteractionProperties->peaInteractLocations, pInteractLocProperties);

				}
				else
				{
					StructDestroy(parse_WorldInteractLocationProperties, pInteractLocProperties);
				}
			}
		}
	}
}

static WorldFXEntry * _getFXEntry(WorldInteractionEntry * pInteractionEntry,int idx)
{
	int i;
	int iParentFXIdx=0;
	for (i=0;i<eaSize(&pInteractionEntry->child_entries);i++)
	{
		if (pInteractionEntry->child_entries[i]->type == WCENT_FX)
		{
			if (iParentFXIdx == idx)
			{
				return (WorldFXEntry*)pInteractionEntry->child_entries[i];
			}
			else
			{
				iParentFXIdx++;
			}
		}
	}

	return NULL;
}

bool _getMotionChildFX(WorldInteractionEntry *parent_entry,int iDefIdxInParent,WorldFXEntry ** ppFXEntry)
{
	// iParentFXEntryIdx means "The Nth FX entry you find, if you walk the entries forward"
	int iParentFXEntryIdx=0;
	int i;
	for (i = 0; i < eaSize(&parent_entry->parent_def->property_structs.interaction_properties->eaEntries); i++)
	{
		WorldMotionInteractionProperties *pMotionProps = parent_entry->parent_def->property_structs.interaction_properties->eaEntries[i]->pMotionProperties;
		int j;

		if (!pMotionProps)
			continue;

		// We found some move descriptors.  We'll need to make interaction entries for them
		for (j = 0; j < eaSize(&pMotionProps->eaMoveDescriptors); j++)
		{
			if (pMotionProps->eaMoveDescriptors[j]->iStartChildIdx == iDefIdxInParent ||
				pMotionProps->eaMoveDescriptors[j]->iDestChildIdx == iDefIdxInParent)
			{
				if (ppFXEntry)
				{
					// this should always find an existing entry to use, created by my parent
					*ppFXEntry = _getFXEntry(parent_entry,iParentFXEntryIdx);
				}
				return true;
			}

			// This works because we know that we made exactly as many FX entries as we had move descriptors.  FX for child geo that doesn't move
			// will be created and owned by that child
			iParentFXEntryIdx++;
		}
	}

	return false;
}

static bool worldDrawableEntryShouldInteractableCluster(
	const GroupDef *def,
	const GroupDef *parent_def)
{
	// return false if the parent def has the ability to turn off visibility of its children
	if (parent_def && parent_def->property_structs.interaction_properties) {
		if (	parent_def->property_structs.physical_properties.pcChildSelectParam ||
			parent_def->property_structs.physical_properties.bIsChildSelect ||
			parent_def->property_structs.physical_properties.bRandomSelect)
			return false;
	}

	// Skip interactables according to their properties.
	if(def->property_structs.interaction_properties) {
		WorldInteractionProperties *defInteractions = def->property_structs.interaction_properties;
		int entryCount = eaSize(&defInteractions->eaEntries);
		int i;

		if (defInteractions->bAllowExplicitHide || defInteractions->bStartsHidden)
			return false;

		for (i = 0; i < entryCount; i++) {
			int j;
			WorldInteractionPropertyEntry *wiProp = defInteractions->eaEntries[i];
			static const char *bannedClassList[] = {"Destructible",NULL};

			if (wiProp->bOverrideVisibility || wiProp->pVisibleExpr)
				return false;

			for (j = 0; bannedClassList[j]; j++) {
				if (strcmp(wiProp->pcInteractionClass, bannedClassList[j]) == 0)
					return false;
			}
		}
	}

	if (parent_def)		// The parent def should be checked as if it is the current def, should it exist, in case it may ever be hidden as well.
		return worldDrawableEntryShouldInteractableCluster(parent_def, NULL);

	return true;
}

typedef struct GroupDefMaterialProperties {
	bool containsAlpha;
	bool excludeClustering;
	bool doubleSided;
} GroupDefMaterialProperties;

static bool worldDrawableEntryShouldCluster(
	const WorldDrawableEntry *entry,
	const GroupInfo *info,
	const GroupDef *def,
	const GroupDef *parent_def,
	GroupDefMaterialProperties *gdMatProps,
	GroupDef **inheritedDefs) {

	ModelLOD *lod0 = NULL;

	bool inheritedWouldCluster = true;
	if(inheritedDefs) {
		GroupDef **defListMinusOne = NULL;
		GroupDef *curDef = inheritedDefs[eaSize(&inheritedDefs)-1];

		if(eaSize(&inheritedDefs) > 1) {
			eaCopy(&defListMinusOne, &inheritedDefs);
			eaRemove(&defListMinusOne, eaSize(&inheritedDefs)-1);
		}

		inheritedWouldCluster = worldDrawableEntryShouldCluster(
			entry, info, curDef, NULL, gdMatProps, defListMinusOne);

		if(defListMinusOne) {
			eaDestroy(&defListMinusOne);
		}
	}

	// Skip objects where clustering is explicitly disabled.
	if (def->property_structs.physical_properties.oLodProps.eClusteringOverride == WLECO_EXCLUDE)
	{
		return false;
	}
	else if (def->property_structs.physical_properties.oLodProps.eClusteringOverride == WLECO_INCLUDE)
	{
		return true;
	}

	// Skip if instructed to do so by the LOD settings, note that this could be inherited from the LOD template or written to in the LOD editor as a per-model override
	// also note that for the case of template inheritance, we don't want to trust the value in the model's lod_info but rather the value in the template (due to certain
	// code paths at runtime while manually editing data files)
	if (def->model &&
		def->model->lod_info)
	{
		if (def->model->lod_info->force_auto ||
			def->model->lod_info->is_automatic)
		{
			ModelLODTemplate *pTemplate = GET_REF(def->model->lod_info->lod_template);
			if (pTemplate && pTemplate->prevent_clustering)
				return false;
		} else {
			if (def->model->lod_info->prevent_clustering)
				return false;
		}
	}

	// Skip things in regions where clustering is disabled.
	if(!worldRegionGetWorldGeoClustering(info->region))
		if(!info->region || !info->region->cluster_options)
			return false;

	// Skip invisible things.
	if(!def->property_structs.physical_properties.bVisible)
		return false;

	// Skip if the object is animated.
	if (def->property_structs.animation_properties)
		return false;

	if (!worldDrawableEntryShouldInteractableCluster(def,parent_def))
		return false;

	if(def->model) {

		lod0 = modelLODLoadAndMaybeWait(def->model, 0, true);

		if (lod0 && lod0->loadstate == GEO_LOADED_NULL_DATA)
			return false;
	}

	// Below are material related exclusion conditions.  All properties of exclusions should be collected in worldEntryGetDefMatProperties.
	if (gdMatProps->excludeClustering)
		return false;

	if (info && info->region && info->region->cluster_options && info->region->cluster_options->debug.dont_cluster_alpha_objects && gdMatProps->containsAlpha)
		return false;

	return inheritedWouldCluster;
}

static MaterialSwap** worldEntryGetCombinedMaterialEAList(MaterialSwap **matSwaps, const char** matSwapList)
{
	MaterialSwap **matSwapRet = NULL;
	int swapIndex;
	int swapSize = eaSize(&matSwaps);
	MaterialSwap *matSwapAddition;

	for (swapIndex = 0; swapIndex < swapSize; swapIndex++) {
		matSwapAddition = calloc(1, sizeof(MaterialSwap));
		matSwapAddition->orig_name = matSwaps[swapIndex]->orig_name;
		matSwapAddition->replace_name = matSwaps[swapIndex]->replace_name;
		eaPush(&matSwapRet,matSwapAddition);
	}
	swapSize = eaSize(&matSwapList) / 2;
	for (swapIndex = 0; swapIndex < swapSize; swapIndex++) {
		matSwapAddition = calloc(1, sizeof(MaterialSwap));
		matSwapAddition->orig_name = matSwapList[swapIndex * 2];
		matSwapAddition->replace_name = matSwapList[(swapIndex * 2) + 1];
		eaPush(&matSwapRet,matSwapAddition);
	}
	return matSwapRet;
}

static TextureSwap** worldEntryGetCombinedTextureEAList(TextureSwap **texSwaps, const char** texSwapList)
{
	TextureSwap **texSwapRet = NULL;
	int swapIndex;
	int swapSize = eaSize(&texSwaps);
	TextureSwap *texSwapAddition;

	for (swapIndex = 0; swapIndex < swapSize; swapIndex++) {
		texSwapAddition = calloc(1, sizeof(TextureSwap));
		texSwapAddition->orig_name = texSwaps[swapIndex]->orig_name;
		texSwapAddition->replace_name = texSwaps[swapIndex]->replace_name;
		eaPush(&texSwapRet,texSwapAddition);
	}
	swapSize = eaSize(&texSwapList) / 2;
	for (swapIndex = 0; swapIndex < swapSize; swapIndex++) {
		texSwapAddition = calloc(1, sizeof(TextureSwap));
		texSwapAddition->orig_name = texSwapList[swapIndex * 2];
		texSwapAddition->replace_name = texSwapList[(swapIndex * 2) + 1];
		eaPush(&texSwapRet,texSwapAddition);
	}
	return texSwapRet;
}

// Collect information about the materials used for the GroupDef that are taken into account on an object level.
static void worldEntryGetDefMatProperties(GroupDef *def, MaterialSwap **matSwapList, GroupDefMaterialProperties *gdMatProps)
{
	ModelLOD *lod0 = NULL;

	lod0 = modelLODLoadAndMaybeWait(def->model, 0, true);

	if (lod0) {
		int i;

		if (lod0->loadstate == GEO_LOADED_NULL_DATA)
			return;

		for (i = 0; i < lod0->data->tex_count; i++) {
			int j;
			Material	*modelMat = lod0->materials[i];
			const MaterialData *modelMatData;

			for (j = 0; j < eaSize(&matSwapList); j++) {
				if (strcmp(matSwapList[j]->orig_name,modelMat->material_name) == 0) {
					modelMat = materialFind(matSwapList[j]->replace_name,WL_FOR_WORLD);
					break;
				}
			}

			if (modelMat->graphic_props.flags & RMATERIAL_NO_CLUSTER)
				gdMatProps->excludeClustering = true;

			if (!modelMat->material_data)
				modelMatData = materialFindData(modelMat->material_name);
			else
				modelMatData = modelMat->material_data;

			if (modelMatData && (modelMatData->graphic_props.shader_template->graph->graph_flags & SGRAPH_EXCLUDE_FROM_CLUSTER))
				gdMatProps->excludeClustering = true;

			if ((wl_state.gfx_material_has_transparency(modelMat) || (modelMat->graphic_props.flags & (RMATERIAL_ADDITIVE|RMATERIAL_SUBTRACTIVE|RMATERIAL_NOZWRITE))))
				gdMatProps->containsAlpha = true;

			if (modelMat->graphic_props.flags & RMATERIAL_DOUBLESIDED)
				gdMatProps->doubleSided = true;
		}
	}
}

static ModelClusterVolume* worldEntryCreateModelClusterVolume(GroupDef *def, GroupInfo *info)
{
	ModelClusterVolume *clusterVolume = modelClusterVolumeCreate();

	copyVec3(info->world_max, clusterVolume->volume_max);
	copyVec3(info->world_min, clusterVolume->volume_min);
	clusterVolume->target_lod_level = def->property_structs.client_volume.cluster_volume_properties->targetLOD;
	clusterVolume->lod_min = def->property_structs.client_volume.cluster_volume_properties->minLevel;
	clusterVolume->lod_max = def->property_structs.client_volume.cluster_volume_properties->maxLODLevel;

	if (def->property_structs.client_volume.cluster_volume_properties->textureHeight == ClusterTextureResolutionDefault)
		clusterVolume->texture_height = simplygonTextureHeight();
	else
		clusterVolume->texture_height = def->property_structs.client_volume.cluster_volume_properties->textureHeight;

	if (def->property_structs.client_volume.cluster_volume_properties->textureWidth == ClusterTextureResolutionDefault)
		clusterVolume->texture_width = simplygonTextureWidth();
	else
		clusterVolume->texture_width = def->property_structs.client_volume.cluster_volume_properties->textureWidth;

	if (def->property_structs.client_volume.cluster_volume_properties->textureSupersample == ClusterTextureSupersampleDefault) {
		if ((clusterVolume->texture_width * clusterVolume->texture_height * simplygonDefaultSuperSampling() * simplygonDefaultSuperSampling()) >
			ClusterTextureResolution4096 * ClusterTextureResolution4096)
		{
			clusterVolume->texture_super_sample = ClusterTextureSupersample1;
		} else {
			clusterVolume->texture_super_sample = simplygonDefaultSuperSampling();
		}
	} else {
		clusterVolume->texture_super_sample = def->property_structs.client_volume.cluster_volume_properties->textureSupersample;
		if ((clusterVolume->texture_width * clusterVolume->texture_height * simplygonDefaultSuperSampling() * simplygonDefaultSuperSampling()) >
			ClusterTextureResolution4096 * ClusterTextureResolution4096)
		{
			clusterVolume->texture_super_sample = ClusterTextureSupersample1;
		}
	}

	if (def->property_structs.client_volume.cluster_volume_properties->geometryResolution == ClusterGeometryResolutionDefault) {
		clusterVolume->geo_resolution = simplygonDefaultResolution();
	} else {
		clusterVolume->geo_resolution = def->property_structs.client_volume.cluster_volume_properties->geometryResolution;
	}

	clusterVolume->include_normal = def->property_structs.client_volume.cluster_volume_properties->includeNormal;
	clusterVolume->include_specular = def->property_structs.client_volume.cluster_volume_properties->includeSpecular;
	return clusterVolume;
}

// IMPORTANT NOTE: Nothing you do in worldEntryCreateForGroupDef can be passed onto the children via the GroupInfo, unless it's also 
// done by applyDefToGroupInfo in grouputil.c  This is because worldEntryCreateForGroupDef is called many times if you are recursing 
// via groupTreeTraverseRecurse, but it is called ONLY ONCE from worldEntryCreateForTracker.  The group defs are assumed to have all 
// the info that is required to create the GroupInfo that would have been generated via the recursive calls. Also, remember that 
// "tracker" is basically a write-only pointer in this function. [RMARR - 8/23/12]
static void worldEntryCreateForGroupDef(SA_PARAM_OP_VALID GroupInfo *info, WorldCellEntry ***cell_entry_list, 
										GroupDef **parent_defs, int *idxs_in_parent, 
										SA_PARAM_OP_VALID GroupTracker *tracker, 
										const char ***material_swap_list, const char ***texture_swap_list, 
										MaterialNamedConstant ***material_property_list, 
										bool in_editor, bool in_headshot, bool temp_group, bool is_client, 
										WorldModelInstanceEntry *instance_entry,
										GroupDef **inheritedDefs)
{
	WorldFXEntry *fx_entry = NULL;
	WorldFXEntry *fx_entry_to_use = NULL;
	LightData *light_data = NULL;
	bool is_drawable, has_volume, create_collision;
	GroupDef *def, *parent_def;
	Model *model;
	Model *modelCustomOccluder = NULL;
	WorldPlanetProperties *planet;
	WorldAnimationProperties *animation;
	WorldVolumeEntry *volume_entry = NULL;
	WorldInteractionEntry *interaction_entry = NULL, *old_parent_entry = NULL;
	int old_parent_entry_child_idx = -1;
	int i;
	bool no_coll = (!info->collisionFilterBits), editor_only = info->editor_only;
	bool custom_occluder = false;
	bool no_occlusion = info->no_occlusion;
	bool scopedata_written = false;
	bool bInteractionEntryRequiredForThisChild = false;
	bool bNeedFXEntry = false;
	char *partition_name = NULL;

	if (!info->visible)
		return;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	assert(eaSize(&parent_defs) > 0);

	def = parent_defs[eaSize(&parent_defs)-1];
	parent_def = (eaSize(&parent_defs) > 1) ? parent_defs[eaSize(&parent_defs)-2] : NULL;
	model = def->model;
	planet = def->property_structs.planet_properties;
	animation = def->property_structs.animation_properties;
	has_volume = !!(def->property_structs.volume);
	is_drawable = model || planet || has_volume; // this will be refined later

	create_collision = in_editor || !no_coll;

	if ((def->property_structs.physical_properties.bOcclusionOnly) && model && modelIsTemp(model))
	{
		// force collisions off for occlusion models generated by curves
		create_collision = false;
	}

	if (model && !no_occlusion)
	{
		char customOccluderName[MAX_PATH];
		STR_COMBINE_SS(customOccluderName, model->name, MODEL_OCCLUSION_NAME_SUFFIX);
		modelCustomOccluder = modelFind(customOccluderName, true, WL_FOR_WORLD);
	}
	if ((groupIsVolumeType(def, "Occluder")) || 
		(model && (def->property_structs.physical_properties.bOcclusionOnly)) ||
		((def->property_structs.volume && def->property_structs.volume->bSubVolume) && info->volume_def && groupIsVolumeType(info->volume_def, "Occluder")) ||
		modelCustomOccluder)
	{
		no_coll = (def->property_structs.physical_properties.bOcclusionOnly) || (!model && !planet);
		editor_only = false; // occluders need to be traversed out of the editor as well
		no_occlusion = false;
		custom_occluder = true;
	}

	if (info->parent_entry)
	{
		no_occlusion = true; // occlusion is one frame ahead, so we can't use interaction objects as occluders
		custom_occluder = false;
	}

	if (info->is_debris)
		editor_only = true;

	if (in_headshot && info->headshot_visible)
		editor_only = false;

	is_drawable = model || planet || custom_occluder || (in_editor && has_volume);
	if (no_coll && !is_client)
		is_drawable = false;
	if (!is_drawable)
		create_collision = false;
	if (editor_only && !in_editor)
		is_drawable = false;
	if (instance_entry)
		is_drawable = false; // still might want collision though

	// check if the animation controller has values that will make it not animate
	if (animation && is_client)
	{
		bool no_sway = false, no_rotation = false, no_scale = false, no_translation = false;
		if (sameVec3(animation->sway_angle, zerovec3) || sameVec3(animation->sway_time, zerovec3))
			no_sway = true;
		if (sameVec3(animation->scale_amount, onevec3) || sameVec3(animation->scale_time, zerovec3))
			no_scale = true;
		if (sameVec3(animation->rotation_time, zerovec3))
			no_rotation = true;
		if (sameVec3(animation->translation_amount, zerovec3) || sameVec3(animation->translation_time, zerovec3))
			no_translation = true;
		if (no_sway && no_scale && no_rotation && no_translation)
			animation = false;
	}

	if (def->property_structs.physical_properties.iTagID)
	{
		WorldTagLocation *tag_location = calloc(1, sizeof(WorldTagLocation));
		tag_location->tag_id = info->tag_id;
		copyVec3(info->world_matrix[3], tag_location->position);
		if (tracker)
			tracker->tag_location = tag_location;
		else if (info->layer)
			eaPush(&info->layer->tag_locations, tag_location);
		eaPush(&info->region->tag_locations, tag_location);
	}

	if (!editor_only)
	{
		if (model && (def->property_structs.interaction_properties || info->childrenNeedInteractFX))
		{
			// I've got a model, and either I have interaction properties, or an ancestor does (technically, childrenNeedInteractFX will always be set here, I believe)
			bNeedFXEntry = true;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// interaction, must be done before the parent_entry pointer is accessed, as we override it below (and stash the old one locally)
	//
	// Note we check the PARENT def here, not "def".  If we are OWNED by an interaction node, we want to ask questions about our
	// identity as part of a motion descriptor, etc.
	if (parent_def && parent_def->property_structs.interaction_properties && !temp_group)
	{
		if (parent_def->property_structs.interaction_properties->pChildProperties)
		{
			// Our parent had interaction properties that had child properties.  That means we might be hidden.  Should we be
			// making sure the child properties actually correspond to us??
			bInteractionEntryRequiredForThisChild = true;
		}
		else 
		{
			// Look through the entries in the interaction properties for motion properties
			int iDefIdxInParent = idxs_in_parent[eaiSize(&idxs_in_parent) - 1];
			WorldFXEntry ** ppEntry = NULL;

			if (is_client && !info->dummy_group)
				ppEntry = &fx_entry_to_use;

			devassert(info->parent_entry->parent_def == parent_def);
			if (_getMotionChildFX(info->parent_entry,iDefIdxInParent,ppEntry))
			{
				bInteractionEntryRequiredForThisChild = true;

				// this should always find an existing entry to use, created by my parent
				// I think this is now handled below
				//bNeedFXEntry = true;
			}
		}
	}

	if (def->property_structs.interaction_properties) 
	{
		// This code exists to detect and recover from some bad generated data in the past where
		// game system objects also had illegal parallel interaction properties.
		// Only volumes should be able to double up this way.  All other properties are exclusive.
		if (def->property_structs.encounter_hack_properties ||
			def->property_structs.encounter_properties ||
			def->property_structs.layer_fsm_properties ||
			def->property_structs.spawn_properties ||
			def->property_structs.trigger_condition_properties ||
			def->property_structs.patrol_properties) 
		{
			// Force wipe out the data
			def->property_structs.interaction_properties = NULL;
			ErrorFilenamef(def->filename, "Object '%s' found that has interaction properties in addition to other game system properties.  This is not allowed and the properties are being removed during binning.", def->name_str);
		}
	}

	if (bInteractionEntryRequiredForThisChild || (def->property_structs.interaction_properties && !info->dummy_group))
	{
		// For some reason, an interaction node is needed.  We may need to hide or show this object, or we may need to have an FX associated with it,
		// or we may just have interaction properties.  At any rate, this is where we create it.
		WorldInteractionEntry *entry = createWorldInteractionEntry(parent_defs, idxs_in_parent, def, info);

		if (tracker)
			tracker->cell_interaction_entry = entry;

		worldEntryInit(&entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);

		// CD_HACK_REMOVEME
		if (!tracker && entry->base_interaction_properties)
			checkInteractionParentage(&entry->base_entry, def, entry->base_interaction_properties->eInteractionClass);
		// CD_HACK_REMOVEME

		// Save off this value for building self
		old_parent_entry = info->parent_entry;
		old_parent_entry_child_idx = info->parent_entry_child_idx;

		// Alter data in info for building children
		info->parent_entry = entry;
		info->parent_entry_child_idx = -1;

		interaction_entry = entry;
	}

	// This block is for the case where our exact def has interaction properties
	if (is_client && def->property_structs.interaction_properties && !info->dummy_group)
	{
		// make an FX entry for each of my child motions, for children to use later
		for (i = 0; i < eaSize(&def->property_structs.interaction_properties->eaEntries); i++)
		{
			WorldMotionInteractionProperties *pMotionProps = def->property_structs.interaction_properties->eaEntries[i]->pMotionProperties;
			int j;

			if (!pMotionProps)
				continue;

			for (j = 0; j < eaSize(&pMotionProps->eaMoveDescriptors); j++)
			{
				WorldFXEntry * pMotionFXEntry = NULL;
				pMotionFXEntry = createWorldFXEntry(info->world_matrix, info->world_mid, info->radius, info->parent_entry->base_entry.shared_bounds->far_lod_far_dist, info);
				if (pMotionFXEntry)
				{
					pMotionFXEntry->interaction_node_owned = true;
					worldEntryInit(&pMotionFXEntry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
				}
			}
		}
	}

	// Handle the FX for the client.
	if (is_client && bNeedFXEntry && !info->dummy_group)
	{
		// At this point we've decided we need to have an FX entry on this entry.  We MIGHT already have one, and it might be shared with another entry
		if (fx_entry_to_use == NULL)
		{
			// This should literally be our parent interact node, unless it's our interact node.  That is to say, this is the nearest interaction node to me
			devassert (info->parent_entry);

			// if my parent entry doesn't have any interact options, then I am the child of a motion descriptor, so I should use the FX that was generated on that entry
			// (This code assumes there is at most one)
			if (info->parent_entry->base_interaction_properties == NULL)
			{
				// The parent of our parent - I expect him to have some interaction properties.  If this blows up, I'll revisit it. [RMARR - 8/23/12]
				assert(info->parent_entry->base_entry_data.parent_entry->base_interaction_properties);
				 _getMotionChildFX(info->parent_entry->base_entry_data.parent_entry,info->parent_entry->base_entry_data.interaction_child_idx,&fx_entry_to_use);
			}
			else
			{
				fx_entry = createWorldFXEntry(info->world_matrix, info->world_mid, info->radius, info->parent_entry->base_entry.shared_bounds->far_lod_far_dist, info);
				fx_entry->interaction_node_owned = true;
				fx_entry_to_use = fx_entry;
			}
		}

		// This is used to set the fx_parent_handle later (in initDrawableEntry, called from this function below)
		info->fx_entry = fx_entry_to_use;
	}

	if (is_client && info && info->parent_entry && def->property_structs.interaction_properties && def->property_structs.interaction_properties->pchAdditionalUniqueFX)
	{
		//We need an additional FX entry on the root interaction properties node for this FX to play.
		WorldFXEntry* pAdditionalEntry = createWorldFXEntry(info->world_matrix, info->world_mid, info->radius, info->parent_entry->base_entry.shared_bounds->far_lod_far_dist, info);
		worldEntryInit(&pAdditionalEntry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
	}

	//////////////////////////////////////////////////////////////////////////
	// Ambient Job
	if(def->property_structs.interact_location_properties)
		worldEntryCreateInteractLocation(info, def);

	//////////////////////////////////////////////////////////////////////////
	// Civilian generator
	{
		if(def->property_structs.physical_properties.bCivilianGenerator)
		{
			if(wl_state.civgen_create_func && wl_state.civgen_destroy_func)
			{
				CivilianGenerator *civgen = wl_state.civgen_create_func(info->world_mid);
				WorldCivilianGenerator *wlcivgen = StructCreate(parse_WorldCivilianGenerator);

				copyVec3(info->world_mid, wlcivgen->position);
				wlcivgen->civ_gen = civgen;
				
				if(tracker)
					tracker->world_civilian_generator = wlcivgen;
				eaPush(&info->region->world_civilian_generators, wlcivgen);
			}
		}

		if(def->property_structs.physical_properties.bForbiddenPosition)
		{
			WorldForbiddenPosition *wlforbid = StructCreate(parse_WorldForbiddenPosition);

			copyVec3(info->world_mid, wlforbid->position);
		
			if(tracker)
			{
				tracker->world_forbidden_position = wlforbid;
			}

			eaPush(&info->region->world_forbidden_positions, wlforbid);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// Path Nodes
	if(!info->dummy_group && def->property_structs.path_node_properties)
	{
		WorldPathNode *node = StructCreate(parse_WorldPathNode);
		copyVec3(info->world_matrix[3], node->position);
		node->position[1] += PATH_NODE_Y_OFFSET;
		StructCopy(parse_WorldPathNodeProperties, def->property_structs.path_node_properties, &node->properties, 0, 0, 0);

		if(tracker)
		{
			tracker->world_path_node = node;
		}

		node->uID = def->name_uid;

		if (node->properties.eaConnections){
			int iMyEdge, iNode;
			for(iMyEdge = 0; iMyEdge < eaSize(&node->properties.eaConnections); ++iMyEdge)
			{
				WorldPathEdge *pMyEdge = node->properties.eaConnections[iMyEdge];

				if(pMyEdge)
				{
					for (iNode = 0; iNode < eaSize(&info->region->world_path_nodes); ++iNode)
					{
						WorldPathNode *pNode = info->region->world_path_nodes[iNode];

						// Update the node's connection
						if(pNode && pNode->uID == pMyEdge->uOther)
						{
							int iOther;
							bool bFound = false;

							copyVec3(pNode->position, pMyEdge->v3Other);

							for (iOther = 0; iOther < eaSize(&pNode->properties.eaConnections); ++iOther)
							{
								WorldPathEdge *pOtherEdge = pNode->properties.eaConnections[iOther];

								if (pOtherEdge && pOtherEdge->uOther == node->uID)
								{
									copyVec3(node->position, pOtherEdge->v3Other);
									bFound = true;
								}
							}

							if (!bFound)
							{
								WorldPathEdge *pNewEdge = StructCreate(parse_WorldPathEdge);

								pNewEdge->uOther = node->uID;
								copyVec3(node->position, pNewEdge->v3Other);

								eaPush(&pNode->properties.eaConnections, pNewEdge);
							}
						}
					}
				}
			}
		}

		if(!def->property_structs.path_node_properties->bUGCNode) {
			eaPush(&info->region->world_path_nodes, node);
		} else {
			eaPush(&info->region->world_path_nodes_editor_only, node);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// alt pivot
	if (def->property_structs.physical_properties.bHandPivot ||
		def->property_structs.physical_properties.bMassPivot ||
		def->property_structs.physical_properties.eCarryAnimationBit)
	{
		WorldAltPivotEntry *entry = createWorldAltPivotEntry(def, info);
		worldEntryInit(&entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
	}

	//////////////////////////////////////////////////////////////////////////
	// collision
	if (create_collision && !info->dummy_group)
	{
		WorldCollisionEntry *entry;
		Model *planet_model = NULL;
		if (!planet || (planet_model = groupModelFind("_Planet_Collision", 0)))
		{
			entry = createWorldCollisionEntry(tracker, def, planet?planet_model:model, texture_swap_list, material_swap_list, planet, info, false);

			worldEntryInit(&entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
			//printf("   COLLISION ENTRY %X (DEF %X, CELL %X / %d)\n", (int)(intptr_t)entry, (int)(intptr_t)def, (int)(intptr_t)worldCellEntryGetData(&entry->base_entry)->cell, worldCellEntryGetData(&entry->base_entry)->cell ? worldCellEntryGetData(&entry->base_entry)->cell->streaming_mode : -1);

			if (info->collisionFilterBits & (WC_FILTER_BIT_MOVEMENT | WC_FILTER_BIT_POWERS))
			{
				if (wlIsServer())
				{
					WorldInteractionEntry *parent_entry = entry->base_entry_data.parent_entry;
					while (parent_entry)
					{
						if (parent_entry->full_interaction_properties && parent_entry->full_interaction_properties->bEvalVisExprPerEnt)
						{
							GroupDef *temp_parent = parent_entry->parent_def;
							if (temp_parent)
								ErrorFilenamef(temp_parent->filename, "Per-Entity Visibility Object \"%s\" has child \"%s\" which has non-cosmetic collision turned on.", temp_parent->name_str, def->name_str);
							break;
						}
						parent_entry = parent_entry->base_entry_data.parent_entry;
					}
				}
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// volume
	if (has_volume && !info->dummy_group && (!def->property_structs.room_properties || (def->property_structs.room_properties->eRoomType!=WorldRoomType_Partition && def->property_structs.room_properties->eRoomType!=WorldRoomType_Room)))
	{
		U32 volume_type_bits = 0;

		if (def->property_structs.volume && def->property_structs.volume->bSubVolume)
		{
			if (info->volume_entry)
			{
				// add to parent entry and update parent entry bounds
				WorldVolumeElement *element;
				worldRemoveCellEntry(&info->volume_entry->base_entry);
				element = worldVolumeEntryAddSubVolume(info->volume_entry, def, info);
				assert(!info->volume_entry->base_entry.shared_bounds->ref_count);
				info->volume_entry->base_entry.shared_bounds = lookupSharedBounds(info->zmap, info->volume_entry->base_entry.shared_bounds);
				worldAddCellEntry(info->region, &info->volume_entry->base_entry);
				if (tracker)
					tracker->cell_volume_element = element;
			}
		}
		else
		{
			if(def->property_structs.hull) {
				for (i = 0; i < eaSize(&def->property_structs.hull->ppcTypes); ++i)
					volume_type_bits |= wlVolumeTypeNameToBitMask(def->property_structs.hull->ppcTypes[i]);
			}

			if (interaction_entry)
				volume_type_bits |= wlVolumeTypeNameToBitMask("InteractionAttached");

			// Do not bin the volume on the client if the volume only
			// has server volume data, and vice versa.
			if (!in_editor)
				volume_type_bits &= volume_type_bit_masks[wlIsServer()];

			if (volume_type_bits)
			{
				volume_entry = info->volume_entry = createWorldVolumeEntry(def, info, volume_type_bits);
				worldEntryInit(&volume_entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
				if (tracker)
					tracker->cell_volume_entry = info->volume_entry;
				info->volume_def = def;
				if (interaction_entry)
					interaction_entry->attached_volume_entry = volume_entry;
			}
		}
	}

	if (is_client)
	{

		if (def->property_structs.client_volume.cluster_volume_properties && info->region->cluster_options) {
			ModelClusterVolume *clusterVolume = worldEntryCreateModelClusterVolume(def,info);
			eaPush(&info->region->cluster_options->cluster_volumes, clusterVolume);
		}

		if (def->property_structs.client_volume.sky_volume_properties)
		{
			FOR_EACH_IN_EARRAY(def->property_structs.client_volume.sky_volume_properties->sky_group.override_list, SkyInfoOverride, sky_override)
			{
				const char *sky_name = REF_STRING_FROM_HANDLE(sky_override->sky);
				if (sky_name)
					worldDepsReportSky(sky_name);
				else
				{
					// No sky name, this volume just uses the default sky?
				}
			}
			FOR_EACH_END;
		}

		//////////////////////////////////////////////////////////////////////////
		// animation controller
		if (animation)
		{
			F32 near_lod_near_dist, far_lod_near_dist, far_lod_far_dist;
			groupGetDrawVisDistRecursive(def, info->ignore_lod_override ? NULL : info->lod_override, info->lod_scale, &near_lod_near_dist, &far_lod_near_dist, &far_lod_far_dist);
			info->animation_entry = createWorldAnimationEntry(info, far_lod_near_dist, far_lod_far_dist, animation);
			worldEntryInit(&info->animation_entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
			if (tracker)
				tracker->cell_animation_entry = info->animation_entry;

			if (fx_entry)
			{
				setupWorldAnimationEntryDictionary(info->zmap);
				SET_HANDLE_FROM_INT(info->zmap->world_cell_data.animation_entry_dict_name, info->animation_entry->id, fx_entry->animation_controller_handle);

				assert(!fx_entry->base_entry.shared_bounds->ref_count);
				worldAnimationEntryModifyBounds(info->animation_entry, 
					fx_entry->base_entry.bounds.world_matrix, fx_entry->base_entry.shared_bounds->local_min, fx_entry->base_entry.shared_bounds->local_max, 
					fx_entry->base_entry.bounds.world_mid, &fx_entry->base_entry.shared_bounds->radius);

				setSharedBoundsRadius(fx_entry->base_entry.shared_bounds, fx_entry->base_entry.shared_bounds->radius);
				setSharedBoundsMinMax(fx_entry->base_entry.shared_bounds, NULL, fx_entry->base_entry.shared_bounds->local_min, fx_entry->base_entry.shared_bounds->local_max);

				fx_entry->controller_relative_matrix_inited = worldAnimationEntryInitChildRelativeMatrix(info->animation_entry, fx_entry->base_entry.bounds.world_matrix, fx_entry->controller_relative_matrix);
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// fx emitter
		if (def->property_structs.fx_properties)
		{
			WorldFXProperties *fx_props = def->property_structs.fx_properties;
			const char *fx_name = fx_props->pcName;
			if (fx_name)
			{
				Mat4 target_node_mat;

				if (!fx_entry)
				{
					F32 max_vis_distance = 300.0f;
					if (dynFxInfoExists(fx_name))
					{
						// figure out what the visdist should be for this fx emitter
						F32 fx_distance = dynFxInfoGetDrawDistance(fx_name);
						if (fx_distance > 0.0f)
							max_vis_distance = fx_distance;
					}
					fx_entry = createWorldFXEntry(info->world_matrix, NULL, info->radius, max_vis_distance, info);
				}

				copyVec3(fx_props->vTargetPos, target_node_mat[3]);
				createMat3YPR(target_node_mat, fx_props->vTargetPyr);

				initWorldFXEntry(info->zmap, fx_entry, fx_props->fHue, fx_props->pcCondition, fx_name, fx_props->pcParams,
					fx_props->bHasTarget, fx_props->bTargetNoAnim, target_node_mat, fx_props->pcFaction,
					info->layer?layerGetFilename(info->layer):NULL);

				info->fx_entry = fx_entry;
			}
		}

		if (!(info->editor_only && !in_editor) && model && info->is_debris && !info->dummy_group)
		{
			if (!fx_entry)
				fx_entry = createWorldFXEntry(info->world_matrix, info->world_mid, info->radius, 200, info);
			initWorldFXEntryDebris(fx_entry, model, def->model_scale, info, def, texture_swap_list, material_swap_list, *material_property_list);
		}

		if (fx_entry)
		{
			worldEntryInit(&fx_entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
			// CD_HACK_REMOVEME
			if (!tracker && fx_entry->debris_model_name)
				checkInteractionParentage(&fx_entry->base_entry, def, 0);
			// CD_HACK_REMOVEME
			fx_entry = NULL;
		}

		//////////////////////////////////////////////////////////////////////////
		// light
		if (wl_state.update_light_func && wl_state.remove_light_func && groupHasLight(def))
		{
			WorldLightEntry *entry = createWorldLightEntry(parent_defs, info);
			char* error_message;

			estrStackCreate(&error_message);
			if (!validateWorldLightEntry(parent_defs, info, &error_message))
				Errorf("%s", error_message);
			estrDestroy(&error_message);

			light_data = entry->light_data;
			if (tracker && !tracker->dummyTracker)
				light_data->tracker = tracker;
			worldEntryInit(&entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
		}

		//////////////////////////////////////////////////////////////////////////
		// wind
		if (def->property_structs.wind_source_properties)
		{
			WorldWindSourceEntry *entry = createWorldWindSourceEntry(def, info);
			worldEntryInit(&entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
		}

		//////////////////////////////////////////////////////////////////////////
		// sound emitter
		if (wl_state.create_sound_func && wl_state.remove_sound_func && wl_state.sound_radius_func && !info->dummy_group)
		{
			WorldSoundSphereProperties *props = def->property_structs.sound_sphere_properties;
			if (props)
			{
				WorldSoundEntry *entry = createWorldSoundEntry(	props->pcEventName, (props->bExclude ? "1" : NULL), props->pcDSPName, 
					props->pcGroup, props->iGroupOrd, info, def);
				worldEntryInit(&entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// drawable
		if (is_drawable)
		{
			MaterialSwap **eaMatSwaps = NULL;
			TextureSwap **eaTexSwaps = NULL;
			Material **defMats = NULL;
			GroupDefMaterialProperties gdMatProps = {0,0,0};

			eaMatSwaps = worldEntryGetCombinedMaterialEAList(def->material_swaps, *material_swap_list);
			eaTexSwaps = worldEntryGetCombinedTextureEAList(def->texture_swaps, *texture_swap_list);

			worldEntryGetDefMatProperties(def, eaMatSwaps, &gdMatProps);

			if (!def->property_structs.physical_properties.bOcclusionOnly)
			{
				if (model)
				{
					WorldDrawableEntry *entry;

					if (info->spline)
					{
						entry = createWorldSplinedModelEntry(def, model, info, texture_swap_list, material_swap_list, *material_property_list);
					}
					else
					{
						Vec3 scale;
						scaleVec3(def->model_scale, info->uniform_scale, scale);
						entry = createWorldModelEntry(def, model, info, texture_swap_list, material_swap_list, *material_property_list, !custom_occluder && !no_occlusion, scale, NULL);
					}
					initDrawableEntry(entry, info, def, tracker, light_data, NULL, 0, editor_only, false, !info->spline, in_editor, false);
					worldEntryInit(&entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);


					if(worldDrawableEntryShouldCluster(entry, info, def, parent_def, &gdMatProps, inheritedDefs))
					{
						if(info->region->cluster_options)
						{
							worldCellClusterBucketDrawableEntry(info->region->cluster_options, entry, eaTexSwaps, eaMatSwaps, def->replace_material_name);
						}
						entry->should_cluster = CLUSTERED;
					}
					else
					{
						entry->should_cluster = NON_CLUSTERED;
					}
					worldAddOrReaddCellEntryForInit(info, &entry->base_entry);


//					printf("   DRAWABLE ENTRY %X (DEF %X, CELL %X / %d)\n", (int)(intptr_t)entry, (int)(intptr_t)def, (int)(intptr_t)worldCellEntryGetData(&entry->base_entry)->cell, worldCellEntryGetData(&entry->base_entry)->cell ? worldCellEntryGetData(&entry->base_entry)->cell->streaming_mode : -1);
				}
				if (planet)
				{
					WorldAtmosphereProperties *atmosphere = planet->has_atmosphere?&planet->atmosphere:NULL;
					F32 atmosphere_size = atmosphere ? (planet->geometry_radius * atmosphere->atmosphere_thickness / atmosphere->planet_radius) : 0;
					F32 atmosphere_radius = planet->geometry_radius + atmosphere_size;
					WorldDrawableEntry *entry;
					Model *planet_model;
					Vec3 model_scale;
					int material_property_list_size = eaSize(material_property_list);
					int material_swap_list_size = eaSize(material_swap_list);

					if (planet->has_atmosphere)
					{
						atmosphereBuildDynamicConstantSwaps(atmosphere_size, atmosphere_radius, material_property_list);
						eaPush(material_swap_list, "Planet_Generic");
						eaPush(material_swap_list, "Planet_Generic_With_Atmosphere");
					}

					// planet
					if (planet_model = groupModelFind("_Planet_Generic", 0))
					{
						setVec3same(model_scale, planet->geometry_radius);
						entry = createWorldModelEntry(def, planet_model, info, texture_swap_list, material_swap_list, *material_property_list, !custom_occluder && !no_occlusion, model_scale, atmosphere);
						initDrawableEntry(entry, info, def, tracker, light_data, NULL, planet->geometry_radius, editor_only, false, true, in_editor, false);
						entry->no_shadow_receive = 1; // don't receive shadows
						worldEntryInit(&entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
					}

					if (planet->has_atmosphere && (planet_model = groupModelFind("_Planet_Atmosphere", 0)))
					{
						// atmosphere
						setVec3same(model_scale, atmosphere_radius*1.001f);
						entry = createWorldModelEntry(def, planet_model, info, texture_swap_list, material_swap_list, *material_property_list, false, model_scale, atmosphere);
						initDrawableEntry(entry, info, def, tracker, light_data, NULL, atmosphere_radius*1.001f, editor_only, false, true, in_editor, false);
						entry->no_shadow_receive = 1; // don't receive shadows
						worldEntryInit(&entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
					}

					eaSetSize(material_property_list, material_property_list_size);
					eaSetSize(material_swap_list, material_swap_list_size);
				}
			}

			if (custom_occluder && !(no_occlusion || info->parent_no_occlusion) || !custom_occluder && has_volume)
			{
				// volume or occlusion geometry
				Vec4 tint_color;
				WorldDrawableEntry *entry = createWorldOcclusionEntry(def, info->volume_def, modelCustomOccluder,
					info, tint_color, no_occlusion || info->parent_no_occlusion, in_editor, gdMatProps.doubleSided);
				initDrawableEntry(entry, info, def, tracker, light_data, tint_color, 0, editor_only, has_volume, false, in_editor, false);
				worldEntryInit(&entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
			}
			eaDestroyEx(&eaMatSwaps,NULL);
			eaDestroyEx(&eaTexSwaps,NULL);
		}
	}

	if (fx_entry)
	{
		worldEntryInit(&fx_entry->base_entry, info, cell_entry_list, def, NULL, !in_editor);
		fx_entry = NULL;
	}

	//////////////////////////////////////////////////////////////////////////
	// rooms
	if (!info->dummy_group && info->layer)
	{
		// if working within an existing room group...
		if (info->room)
		{
			// add model to an existing partition
			if (info->room_partition && model && !info->exclude_from_room)
				roomPartitionAddModel(info->room_partition, tracker, def, model, info->world_matrix);
			// if a partition is not specified
			else if (!info->room_partition)
			{
				// add a new partition if flagged to do so
				if (SAFE_MEMBER(def->property_structs.room_properties, eRoomType) == WorldRoomType_Partition)
				{
					GroupVolumeProperties *vol_props = def->property_structs.volume;
					InstanceData *instance_data;
					char base_name[64];

					if (vol_props && !vol_props->bSubVolume)
						info->room_partition = roomAddBoxPartition(info->room, vol_props->vBoxMin, vol_props->vBoxMax, info->world_matrix, tracker);
					else
						info->room_partition = roomAddPartition(info->room, tracker);

					// apply instance overrides
					if (info->room_partition)
					{
						sprintf(base_name, "%s%s", GROUP_UNNAMED_PREFIX, "RoomPartition");
						worldEntryAddScopeData(parent_defs, idxs_in_parent, info->closest_scope, base_name, NULL, NULL, &partition_name, false);
						if(info->layer->reserved_unique_names && partition_name)
							stashAddInt(info->layer->reserved_unique_names, partition_name, -1, true);
						scopedata_written = true;
						info->room_partition->zmap_scope_name = strdup(partition_name);

						// FIXUP OCCURRING HERE: We are moving the RoomInstanceData struct from being keyed on scope name to being on the Def having the Room or RoomPartition Volume

						// If we have already fixed up room instance data to be on our room properties, use it by copying onto our room partition
						if(SAFE_MEMBER(def->property_structs.room_properties, room_instance_data))
						{
							info->room_partition->partition_data = StructClone(parse_RoomInstanceData, def->property_structs.room_properties->room_instance_data);
						}
						else
						{
							// Otherwise, perform the fixup by grabbing RoomInstanceData from old Instance Data by Scope Name stash table...
							instance_data = worldEntryGetInstanceData(parent_defs[0], partition_name);
							if (instance_data)
							{
								// Copy data onto our room partition... if this data is saved, then from now on, we will not perform fixup
								info->room_partition->partition_data = StructClone(parse_RoomInstanceData, instance_data->room_data);

								// Copy data onto our room properties...
								def->property_structs.room_properties->room_instance_data = StructClone(parse_RoomInstanceData, instance_data->room_data);
							}
						}
					}
				}
				// add the group's model to the default (or newly created) partition
				if (model && !info->exclude_from_room)
					roomPartitionAddModel(info->room_partition ? info->room_partition : info->room->partitions[0], tracker, def, model, info->world_matrix);
			}

			// create portals
			if (SAFE_MEMBER(def->property_structs.room_properties, eRoomType) == WorldRoomType_Portal)
			{
				GroupVolumeProperties *vol_props = def->property_structs.volume;
				if(vol_props)
					info->room_portal = roomAddPortal(info->room, vol_props->vBoxMin, vol_props->vBoxMax, info->world_matrix);
				else
					info->room_portal = roomAddPortal(info->room, zerovec3, zerovec3, info->world_matrix);
				if (info->layer && layerGetFilename(info->layer))
					info->room_portal->layer = info->layer;
				if (def->name_str)
					info->room_portal->def_name = strdup(def->name_str);

				// set external data
				info->room_portal->sound_conn_props = StructClone(parse_WorldSoundConnProperties, def->property_structs.sound_conn_properties);
			}
		}
		// if creating a new room...
		else if (SAFE_MEMBER(def->property_structs.room_properties, eRoomType) == WorldRoomType_Room)
		{
			InstanceData *instance_data;
			char base_name[64];

			if (!info->region->room_conn_graph)
				info->region->room_conn_graph = calloc(1, sizeof(*info->region->room_conn_graph));
			if (def->property_structs.volume && !def->property_structs.volume->bSubVolume)
				info->room = roomConnGraphAddBoxRoom(info, tracker, def);
			else
			{
				info->room = roomConnGraphAddRoom(info, tracker, def);
				if (model && !info->exclude_from_room)
					roomPartitionAddModel(info->room->partitions[0], tracker, def, model, info->world_matrix);
			}
			
			if (info->room)
				info->room->limit_contained_lights_to_room = def->property_structs.room_properties->bLimitLights;

			// apply instance overrides
			sprintf(base_name, "%s%s", GROUP_UNNAMED_PREFIX, "RoomPartition");
			worldEntryAddScopeData(parent_defs, idxs_in_parent, info->closest_scope, base_name, NULL, NULL, &partition_name, false);
			scopedata_written = true;
			if (info->room && eaSize(&info->room->partitions) > 0)
				info->room->partitions[0]->zmap_scope_name = strdup(partition_name);
			if (info->layer->reserved_unique_names && partition_name)
				stashAddInt(info->layer->reserved_unique_names, partition_name, -1, true);

			if(info->room && eaSize(&info->room->partitions) && info->room->partitions[0])
			{
				// FIXUP OCCURRING HERE: We are moving the RoomInstanceData struct from being keyed on scope name to being on the Def having the Room or RoomPartition Volume

				// If we have already fixed up room instance data to be on our room properties, use it by copying onto our room partition
				if(SAFE_MEMBER(def->property_structs.room_properties, room_instance_data))
				{
					info->room->partitions[0]->partition_data = StructClone(parse_RoomInstanceData, def->property_structs.room_properties->room_instance_data);
				}
				else
				{
					// Otherwise, perform the fixup by grabbing RoomInstanceData from old Instance Data by Scope Name stash table...
					instance_data = worldEntryGetInstanceData(parent_defs[0], partition_name);
					if (instance_data)
					{
						// Copy data onto our room partition... if this data is saved, then from now on, we will not perform fixup
						info->room->partitions[0]->partition_data = StructClone(parse_RoomInstanceData, instance_data->room_data);

						// Copy data onto our room properties...
						def->property_structs.room_properties->room_instance_data = StructClone(parse_RoomInstanceData, instance_data->room_data);
					}
				}
			}

			// set external data
			if (info->layer && layerGetFilename(info->layer))
				info->room->layer = info->layer;
			if (def->name_str)
				info->room->def_name = strdup(def->name_str);
		}

		if (tracker)
		{
			tracker->room = info->room;
			tracker->room_partition = info->room_partition;
			tracker->room_portal = info->room_portal;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// scoped data
	if (!info->dummy_group && info->layer && !info->layer->dummy_layer) // JDJ: remove dummy layer check
	{
		if (def->is_layer)
		{
			if (!info->zmap->zmap_scope)
				info->zmap->zmap_scope = worldZoneMapScopeCreate();
			info->closest_scope = &info->zmap->zmap_scope->scope;

			// ensure that all of this scope def's logical groups have a public name in the parent scope defs
			worldEntryAddLogicalGroups(def, tracker, info, parent_defs, idxs_in_parent);
		}
		else
		{
			WorldEncounterObject *data = NULL;
			WorldEncounterObject *data_interact_2 = NULL;

			assert(info->closest_scope);

			// add the data to the scope tree
			if (!info->in_dynamic_object)
			{
				WorldZoneMapScope *zmap_scope = info->zmap->zmap_scope;

				if (def->property_structs.encounter_hack_properties)
					data = (WorldEncounterObject*) worldZoneMapScopeAddEncounterHack(zmap_scope, info->closest_scope, info->parent_entry, info->parent_entry_child_idx, info->layer, def, tracker, info->world_matrix);
				else if (def->property_structs.encounter_properties)
					data = (WorldEncounterObject*) worldZoneMapScopeAddEncounter(zmap_scope, info->closest_scope, info->parent_entry, info->parent_entry_child_idx, info->layer, def, tracker, info->world_matrix);
				else if (def->property_structs.spawn_properties)
					data = (WorldEncounterObject*) worldZoneMapScopeAddSpawnPoint(zmap_scope, info->closest_scope, info->parent_entry, info->parent_entry_child_idx, info->layer, def, tracker, info->world_matrix);
				else if (def->property_structs.trigger_condition_properties)
					data = (WorldEncounterObject*) worldZoneMapScopeAddTriggerCondition(zmap_scope, info->closest_scope, info->parent_entry, info->parent_entry_child_idx, info->layer, def, tracker);
				else if (def->property_structs.layer_fsm_properties)
					data = (WorldEncounterObject*) worldZoneMapScopeAddLayerFSM(zmap_scope, info->closest_scope, info->parent_entry, info->parent_entry_child_idx, info->layer, def, tracker);
				else if (def->property_structs.patrol_properties)
					data = (WorldEncounterObject*) worldZoneMapScopeAddPatrolRoute(zmap_scope, info->closest_scope, info->parent_entry, info->parent_entry_child_idx, info->layer, def, tracker, info->world_matrix);
				else if (def->property_structs.physical_properties.bNamedPoint)
					data = (WorldEncounterObject*) worldZoneMapScopeAddNamedPoint(zmap_scope, info->closest_scope, info->parent_entry, info->parent_entry_child_idx, info->layer, tracker, info->world_matrix);
				else if (volume_entry && !StructIsZero(&def->property_structs.server_volume))
				{
					data = (WorldEncounterObject*) worldZoneMapScopeAddNamedVolume(zmap_scope, info->closest_scope, info->parent_entry, info->parent_entry_child_idx, info->layer, tracker, volume_entry, info->world_matrix);
					if (bInteractionEntryRequiredForThisChild || def->property_structs.interaction_properties)
					{
						data_interact_2 = (WorldEncounterObject*) worldZoneMapScopeAddInteractable(zmap_scope, info->closest_scope, old_parent_entry, old_parent_entry_child_idx, info->layer, tracker, interaction_entry, info->world_matrix);
					}
				}
				else if (bInteractionEntryRequiredForThisChild || def->property_structs.interaction_properties)
				{
					data = (WorldEncounterObject*) worldZoneMapScopeAddInteractable(zmap_scope, info->closest_scope, old_parent_entry, old_parent_entry_child_idx, info->layer, tracker, interaction_entry, info->world_matrix);
				}
			}

			// add unique names up the scope tree
			if (data)
			{
				char *base_name = NULL;
				estrPrintf(&base_name, "%s%s", GROUP_UNNAMED_PREFIX, def->name_str);
				if (scopedata_written && estrLength(&partition_name) > 0)
					stashRemoveInt(info->layer->reserved_unique_names, partition_name, NULL);

				// worldDebugValidateScope(info->closest_scope); // TomY temporarily disabled until I have time to fix this for real

				worldEntryAddScopeData(parent_defs, idxs_in_parent, info->closest_scope, base_name, NULL, data, NULL, scopedata_written);

				estrDestroy(&base_name);
			}

			// create a subscope in the scope tree for public library groups
			if (groupHasScope(def) && !info->in_dynamic_object)
			{
 				info->closest_scope = worldScopeCreate(def, tracker, info->layer, info->closest_scope);

				// ensure that all of this scope def's logical groups have a public name in the parent scope defs
				worldEntryAddLogicalGroups(def, tracker, info, parent_defs, idxs_in_parent);
			}

			if (tracker && data)
				tracker->enc_obj = data;
		}
		if (tracker)
		{
			tracker->closest_scope = info->closest_scope;
		}
	}

	estrDestroy(&partition_name);

	PERFINFO_AUTO_STOP_FUNC();
}

static InstanceEntryInfo *createInstanceEntryInfo(GroupDef *def, const Mat4 world_matrix)
{
	int i, j;
	InstanceEntryInfo *instance_info = ScratchAlloc(sizeof(InstanceEntryInfo));
	instance_info->def = def;
	copyMat4(world_matrix, instance_info->world_matrix);
	setVec3(instance_info->tint, 1.f, 1.f, 1.f);

	// first look at any that might be attached to the groupdef
	if (eaSize(&def->instance_buffers) > 0)
	{
		for (i = 0; i < eaSize(&def->instance_buffers); i++)
		{
			GroupInstanceBuffer *instance_buffer = def->instance_buffers[i];
			WorldModelInstanceEntry *instance_entry = NULL;

			for (j = 0; j < eaSize(&instance_info->entries); ++j)
			{
				if (instance_info->entries[j]->model == instance_buffer->model)
				{
					instance_entry = instance_info->entries[j];
					break;
				}
			}

			if (!instance_entry)
			{
				instance_entry = calloc(1, sizeof(WorldModelInstanceEntry));
				instance_entry->model = instance_buffer->model;
				instance_entry->wind_params[0] = def->property_structs.wind_properties.fEffectAmount;
				instance_entry->wind_params[1] = def->property_structs.wind_properties.fBendiness;
				instance_entry->wind_params[2] = def->property_structs.wind_properties.fPivotOffset;
				instance_entry->wind_params[3] = def->property_structs.wind_properties.fRustling;
				eaPush(&instance_info->entries, instance_entry);
			}

			for (j = 0; j < eaSize(&instance_buffer->instances); ++j)
				eaPush(&instance_entry->instances, worldDupModelInstanceInfo(instance_buffer->instances[j]));
		}
	}

	return instance_info;
}

static void finishInstanceEntry(WorldModelInstanceEntry *entry, GroupDef *def, GroupTracker *tracker, 
								GroupInfo *info, WorldCellEntry ***cell_entry_list, 
								const char ***material_swap_list, const char ***texture_swap_list, 
								MaterialNamedConstant **material_property_list, 
								bool in_editor)
{
	WorldDrawableEntry *draw = &entry->base_drawable_entry;
	initWorldModelInstanceEntry(entry, def, info, texture_swap_list, material_swap_list, material_property_list);

	// override the data in the info so that initDrawableEntry doesn't overwrite it
	info->unlit = info->unlit || draw->unlit;
	info->low_detail = info->low_detail || draw->low_detail;
	info->high_detail = info->high_detail || draw->high_detail;
	info->high_fill_detail = info->high_fill_detail || draw->high_fill_detail;
	info->map_snap_hidden = info->map_snap_hidden || draw->map_snap_hidden;
	info->no_shadow_cast = info->no_shadow_cast || draw->no_shadow_cast;
	info->no_shadow_receive = info->no_shadow_receive || draw->no_shadow_receive;
	info->force_trunk_wind = info->force_trunk_wind || draw->force_trunk_wind;

	initDrawableEntry(draw, info, def, tracker, NULL, NULL, 0, false, false, false, in_editor, true);
	worldEntryInit(&draw->base_entry, info, cell_entry_list, def, NULL, !in_editor);
	if (tracker && tracker->parent_layer)
		eaPush(&tracker->parent_layer->cell_entries, (WorldCellEntry*)entry);
}


bool worldEntryDestroyInstanceEntryCallback(WorldCellEntry ***cell_entry_list, GroupDef *def,
									GroupInfo *info, GroupInheritedInfo *inherited_info,
									bool needs_entry)
{
	if (info->instance_info != info->parent_info->instance_info)
	{
		int i;
		for (i = 0; i < eaSize(&info->instance_info->entries); ++i)
			finishInstanceEntry(info->instance_info->entries[i], def, NULL, info, cell_entry_list, 
				&inherited_info->material_swap_list, &inherited_info->texture_swap_list, 
				inherited_info->material_property_list, false);
		eaDestroy(&info->instance_info->entries);
		ScratchFree(info->instance_info);
		info->instance_info = NULL;
	}
	return true;
}

bool worldEntryCreateForDefTreeCallback(WorldCellEntry ***cell_entry_list, GroupDef *def, 
										GroupInfo *info, GroupInheritedInfo *inherited_info,
										bool needs_entry)
{
	int i;
	WorldModelInstanceEntry *instance_entry = NULL;
	if (info->visible && info->instance_info && def->model)
	{
		Mat4 world_matrix;
		for (i = 0; i < eaSize(&info->instance_info->entries); ++i)
		{
			if (info->instance_info->entries[i]->model == def->model)
			{
				instance_entry = info->instance_info->entries[i];
				break;
			}
		}

		if (!instance_entry)
		{
			instance_entry = calloc(1, sizeof(WorldModelInstanceEntry));
			instance_entry->model = def->model;
			instance_entry->wind_params[0] = def->property_structs.wind_properties.fEffectAmount;
			instance_entry->wind_params[1] = def->property_structs.wind_properties.fBendiness;
			instance_entry->wind_params[2] = def->property_structs.wind_properties.fPivotOffset;
			instance_entry->wind_params[3] = def->property_structs.wind_properties.fRustling;
			eaPush(&info->instance_info->entries, instance_entry);
		}

		instance_entry->base_drawable_entry.unlit = info->unlit;
		instance_entry->base_drawable_entry.low_detail = info->low_detail;
		instance_entry->base_drawable_entry.high_detail = info->high_detail;
		instance_entry->base_drawable_entry.high_fill_detail = info->high_fill_detail;
		instance_entry->base_drawable_entry.map_snap_hidden = info->map_snap_hidden;
		instance_entry->base_drawable_entry.no_shadow_cast = info->no_shadow_cast;
		instance_entry->base_drawable_entry.no_shadow_receive = info->no_shadow_receive;
		instance_entry->base_drawable_entry.force_trunk_wind = info->force_trunk_wind;

		copyMat4(info->instance_info->world_matrix, world_matrix);
		scaleMat3Vec3(world_matrix, def->model_scale);

		eaPush(&instance_entry->instances, createInstanceInfo(world_matrix, info->instance_info->tint, instance_entry->model, &def->property_structs.physical_properties, def));
	}

	if (needs_entry || info->room || info->room_portal)
	{
		worldEntryCreateForGroupDef(
			info, cell_entry_list, inherited_info->parent_defs, inherited_info->idxs_in_parent,
			NULL, &inherited_info->material_swap_list, &inherited_info->texture_swap_list,
			&inherited_info->material_property_list,
			false /* JDJ: change to wlIsClient() */, false, false, wlIsClient(), instance_entry,
			inherited_info->parent_defs);

		if (!info->instance_info && wlIsClient() && (def->property_structs.physical_properties.iWeldInstances))
		{
			info->instance_info = createInstanceEntryInfo(def, info->world_matrix);
		}
	}
	return true;
}

void worldEntryCreateForDefTree(ZoneMapLayer *layer, 
								GroupDef *root_def, 
								const Mat4 world_matrix)
{
	groupTreeTraverse(layer, root_def, world_matrix, worldEntryCreateForDefTreeCallback, worldEntryDestroyInstanceEntryCallback, &layer->cell_entries, false, false);
}

static bool trackerNeedsEntry(GroupTracker *tracker, GroupDef *def, bool is_client, bool is_drawable, bool has_curve, GroupTracker *spline_tracker)
{
	if (is_client) {
		if (def->property_structs.sound_sphere_properties)
			return true;
		if(groupHasLight(def))
			return true;
		if(def->property_structs.fx_properties && def->property_structs.fx_properties->pcName)
			return true;
	}
	
	if (def->property_structs.room_properties) {
		return true;
	}
	
	if (def->property_structs.physical_properties.iWeldInstances ||
		def->property_structs.interact_location_properties ||
		def->property_structs.physical_properties.iTagID || 
		def->property_structs.physical_properties.bNamedPoint ) {
		return true;
	}
	
	if (has_curve || spline_tracker || is_drawable || def->is_layer || groupHasScope(def)) {
		return true;
	}
	
	if(!StructIsZero(&def->property_structs))
		return true;
	
	if (tracker->parent && tracker->parent->def) {
		if(groupHasMotionProperties(tracker->parent->def)) {
			return true;
		}
	}

	return false;
}

bool worldEntryCreateForTracker(GroupTracker *tracker, const Mat4 tracker_world_matrix, 
								bool in_editor, bool in_headshot, bool temp_group, bool is_client, GroupSplineParams *params, 
								GroupTracker *spline_tracker)
{
	static GroupDef **parent_defs = NULL;
	static GroupTracker **parents = NULL;
	static int *idxs_in_parent = NULL;

	GroupTracker *current, *parent = NULL, *fade_node = NULL;
	bool is_drawable, has_volume, needs_entry, has_curve;

	GroupInstanceBuffer *instance_buffer = NULL;
	WorldModelInstanceEntry *instance_entry = NULL;
	GroupInfo info, instance_parent_info;
	GroupInheritedInfo inherited_info = { 0 };
	ZoneMapLayer *layer;
	GroupDef *def, *parent_def = NULL;
	Model *model;
	WorldPlanetProperties *planet;
	int i;
	bool children_need_entries = true;

	if (!tracker || !tracker->def)
		return true;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	assert(!tracker->cell_entries);
	assert(!tracker->cell_interaction_entry);

	def = tracker->def;
	model = groupGetModel(def);
	planet = def->property_structs.planet_properties;
	has_volume = !!(def->property_structs.volume);
    has_curve = def->property_structs.curve || (tracker->parent && eafSize(&tracker->parent->inherited_spline.spline_points) > 0);
	is_drawable = model || planet || has_volume; // this will be refined later
	needs_entry = trackerNeedsEntry(tracker, def, is_client, is_drawable, has_curve, spline_tracker);

	parent = tracker->parent;
	while (!needs_entry && parent)
	{
		needs_entry = !!parent->room || !!parent->room_portal;
		parent = parent->parent;
	}

	parent = NULL;

	// set scoped data of parent
	if (tracker->parent)
	{
		tracker->closest_scope = tracker->parent->closest_scope;
	}

	if (!needs_entry)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	eaSetSize(&parents, 0);
	eaSetSize(&parent_defs, 0);
	eaiSetSize(&idxs_in_parent, 0);

	layer = tracker->parent_layer;

	initGroupInfo(&info, layer);

	info.spline = params;

	trackerGetBounds(tracker, tracker_world_matrix, info.world_min, info.world_max, info.world_mid, &info.radius, info.world_matrix, params);
	info.region = getWorldRegion(layer, info.world_mid);

	if (info.region->cluster_options)
	{
		eaDestroyStruct(&info.region->cluster_options->cluster_volumes, parse_ModelClusterVolume);
	}

	for (current = tracker; current; current = current->parent)
	{
		eaPush(&parents, current);
		eaPush(&parent_defs, current->def);
		eaiPush(&idxs_in_parent, current->idx_in_parent);
	}

	eaReverse(&parents);
	eaReverse(&parent_defs);
	eaiReverse(&idxs_in_parent);

	for (i = 0; i < eaSize(&parent_defs); ++i)
	{
		GroupDef *current_def = parent_defs[i];
		current = parents[i];

		if (current_def && current_def->property_structs.curve)
			trackerGetMat(current, info.curve_matrix);

		if (current != tracker)
		{
			// Copy the parent's spline instead of recomputing
			splineDestroy(&info.inherited_spline);
			StructCopyAll(parse_Spline, &current->inherited_spline, &info.inherited_spline);
		}

		info.parent_no_occlusion = info.no_occlusion;

		applyDefToGroupInfo(&info, &inherited_info, current_def, parent_def, current->idx_in_parent, 
			is_client, is_drawable,
			(spline_tracker != NULL), (current == tracker));

		if (current->unlit)
			info.unlit = true;

		if (is_client && is_drawable)
		{
			// fade node
			if (!fade_node && (current_def->property_structs.physical_properties.oLodProps.bFadeNode || current_def->property_structs.building_properties))
				fade_node = current;
		}

		if (parent && parent->cell_interaction_entry)
			info.parent_entry = parent->cell_interaction_entry;

		if (parent && parent->cell_animation_entry)
			info.animation_entry = parent->cell_animation_entry;

		if (parent && parent->cell_volume_entry)
		{
			info.volume_entry = parent->cell_volume_entry;
			info.volume_def = parent_def;
		}

		if (current == tracker->instance_parent)
			instance_parent_info = info;

		parent = current;
		parent_def = current_def;

		if (current->room && !info.room)
			info.room = current->room;
		if (current->room_partition && !info.room_partition)
			info.room_partition = current->room_partition;
		if (current->room_portal && !info.room_portal)
			info.room_portal = current->room_portal;

		if (current->closest_scope)
			info.closest_scope = current->closest_scope;
	}

	if (spline_tracker)
		info.in_dynamic_object = 1;

	splineDestroy(&tracker->inherited_spline);
	tracker->inherited_spline = info.inherited_spline;
    
	if (fade_node)
	{
		Vec3 fade_min, fade_max;
		Mat4 fade_mat;
		trackerGetBounds(fade_node, NULL, fade_min, fade_max, info.fade_mid, &info.fade_radius, fade_mat, NULL);
		info.has_fade_node = true;
	}

	if (is_client && info.visible && tracker->instance_parent && def->model)
	{
		Mat4 world_matrix;
		Vec3 tint;

		trackerGetBounds(tracker->instance_parent, NULL, instance_parent_info.world_min, instance_parent_info.world_max, instance_parent_info.world_mid, &instance_parent_info.radius, instance_parent_info.world_matrix, NULL);

		assert(!tracker->instance_info);

		for (i = 0; i < eaSize(&tracker->instance_parent->instance_buffers); ++i)
		{
			if (tracker->instance_parent->instance_buffers[i]->model == def->model)
			{
				instance_buffer = tracker->instance_parent->instance_buffers[i];
				break;
			}
		}

		if (instance_buffer)
		{
			if (instance_buffer->entry)
			{
				// free old entry so it can be recreated
				if (tracker->instance_parent->parent_layer)
					eaFindAndRemoveFast(&tracker->instance_parent->parent_layer->cell_entries, &instance_buffer->entry->base_drawable_entry.base_entry);
				eaFindAndRemoveFast(&tracker->instance_parent->cell_entries, &instance_buffer->entry->base_drawable_entry.base_entry);
				worldCellEntryFree(&instance_buffer->entry->base_drawable_entry.base_entry);
				instance_buffer->entry = NULL;
			}
		}
		else
		{
			instance_buffer = calloc(1, sizeof(GroupInstanceBuffer));
			instance_buffer->model = def->model;
			eaPush(&tracker->instance_parent->instance_buffers, instance_buffer);
		}

		// create a new entry
		instance_buffer->entry = instance_entry = calloc(1, sizeof(WorldModelInstanceEntry));
		instance_entry->model = instance_buffer->model;

		instance_entry->base_drawable_entry.unlit = info.unlit;
		instance_entry->base_drawable_entry.low_detail = info.low_detail;
		instance_entry->base_drawable_entry.high_detail = info.high_detail;
		instance_entry->base_drawable_entry.high_fill_detail = info.high_fill_detail;
		instance_entry->base_drawable_entry.map_snap_hidden = info.map_snap_hidden;
		instance_entry->base_drawable_entry.no_shadow_cast = info.no_shadow_cast;
		instance_entry->base_drawable_entry.no_shadow_receive = info.no_shadow_receive;
		instance_entry->base_drawable_entry.force_trunk_wind = info.force_trunk_wind;

		// add instance for this tracker
		copyMat4(info.world_matrix, world_matrix);
		scaleMat3Vec3(world_matrix, tracker->def->model_scale);
		trackerGetRelativeTint(tracker, tint, tracker->instance_parent);
		tracker->instance_info = createInstanceInfo(world_matrix, tint, instance_entry->model, &tracker->def->property_structs.physical_properties, tracker->def);
		eaPush(&instance_buffer->instances, tracker->instance_info);

		// populate it with the existing entries
		for (i = 0; i < eaSize(&instance_buffer->instances); ++i)
			eaPush(&instance_entry->instances, worldDupModelInstanceInfo(instance_buffer->instances[i]));

		// finalize entry
		finishInstanceEntry(instance_entry, tracker->instance_parent->def, 
			tracker->instance_parent, &instance_parent_info, &tracker->instance_parent->cell_entries, 
			&inherited_info.material_swap_list, &inherited_info.texture_swap_list,
			inherited_info.material_property_list, in_editor);
	}

    if (eafSize(&info.inherited_spline.spline_points) == 0 && !tracker->skip_entry_create)
    {
		worldEntryCreateForGroupDef(
			&info, &tracker->cell_entries, parent_defs, idxs_in_parent,
			spline_tracker ? spline_tracker : tracker,
			&inherited_info.material_swap_list, &inherited_info.texture_swap_list,
			&inherited_info.material_property_list,
			in_editor, in_headshot, temp_group, is_client, instance_entry,
			parent_defs);
        if (tracker->parent_layer)
            eaPushEArray(&tracker->parent_layer->cell_entries, &tracker->cell_entries);
    }

    if (!def->property_structs.curve && eafSize(&info.inherited_spline.spline_points) > 0 && !tracker->skip_entry_create && 
		(!def->property_structs.child_curve || def->property_structs.child_curve->child_type != CURVE_CHILD_INHERIT))
    {
		bool curved_spline = def->property_structs.child_curve ? def->property_structs.child_curve->deform : false; 
		int count = eafSize(&info.inherited_spline.spline_points)/3 - (curved_spline ? 1 : 0);
		F32 distance_offset = 0;
		Mat4 parent_matrix;
		GroupTracker dummy_parent_tracker = { 0 };
		GroupDef *dummy_def = StructCreate(parse_GroupDef);
		MersenneTable *table = mersenneTableCreate(1);

		if (tracker->parent && tracker->parent->def)
		{
			// Undo the transform, since it is applied in the curve
			Mat4 temp, temp2 = { 0 };
			Mat4 temp_matrix;
			GroupChild **children = groupGetChildren(tracker->parent->def);
			identityMat3(temp);
			transposeMat3Copy(children[tracker->idx_in_parent]->mat, temp2);
			scaleVec3(children[tracker->idx_in_parent]->mat[3], -1, temp[3]);
			mulMat4Inline(tracker_world_matrix, temp2, temp_matrix);
			mulMat4Inline(temp_matrix, temp, parent_matrix);
		}
		else
		{
			copyMat4(tracker_world_matrix, parent_matrix);
		}

		dummy_parent_tracker.dummyTracker = 1;
		dummy_parent_tracker.def = dummy_def;
		dummy_parent_tracker.parent = tracker->parent;
		dummy_parent_tracker.parent_layer = tracker->parent_layer;
		dummy_parent_tracker.idx_in_parent = tracker->idx_in_parent;
		dummy_parent_tracker.scale = (tracker->parent ? tracker->parent->scale : 1.0f);

		dummy_def->name_str = allocAddString("Spline Dummy String");
        
		for (i = 0; i < count; i++)
		{
			Mat4 geom_matrix;
			GroupSplineParams *spline_params;
			GroupDef *child_def = curveGetGeometry(unitmat, def, &info.inherited_spline, info.inherited_gaps, info.curve_matrix,
												   def->property_structs.child_curve ? def->property_structs.child_curve->uv_scale : 1.f,
												   def->property_structs.child_curve ? def->property_structs.child_curve->uv_rot : 0.f,
												   def->property_structs.child_curve ? def->property_structs.child_curve->stretch : 1.f,
												   i, geom_matrix, &spline_params, &distance_offset, info.uniform_scale);
			if (child_def)
			{
				GroupTracker dummy_tracker = { 0 };
				GroupChild *child = StructAlloc(parse_GroupChild);

// 				child->def = child_def;
				copyMat4(geom_matrix, child->mat);
				child->uid_in_parent = i+1;
				child->seed = randomMersenneInt(table);
				eaPush(&dummy_def->children, child);

				dummy_tracker.dummyTracker = 1;
				dummy_tracker.def = child_def;
				dummy_tracker.parent = &dummy_parent_tracker;
				dummy_tracker.parent_layer = tracker->parent_layer;
				dummy_tracker.idx_in_parent = i;
				dummy_tracker.debris_cont_tracker = tracker->debris_cont_tracker;
				dummy_tracker.scale = tracker->scale;

//		printf("RECURSING IN TRACKER %X DUMMY %X\n", (int)(intptr_t)tracker, (int)(intptr_t)&dummy_tracker);
				worldEntryCreateForTracker(&dummy_tracker, geom_matrix, in_editor, in_headshot, temp_group, is_client, spline_params, tracker);

				// Push cell entries all the way up to first non-dummy parent
				eaPushEArray(&tracker->cell_entries, &dummy_tracker.cell_entries);
				eaDestroy(&dummy_tracker.cell_entries);

				// Interaction entries?

				// TomY TODO: Clean up dummy, if necessary
			}
		}
		mersenneTableFree(table);
		eaDestroyStruct(&dummy_def->children, parse_GroupChild);
		StructDestroy(parse_GroupDef, dummy_def);

		children_need_entries = false;
    }
	else if (groupIsDynamic(def) && !tracker->skip_entry_create)
	{
		GroupChild **child_list = groupGetDynamicChildren(tracker->def, tracker, NULL, tracker_world_matrix);
		if (child_list)
		{
			GroupTracker dummy_parent_tracker = { 0 };
			GroupDef *dummy_def = StructCreate(parse_GroupDef);

			dummy_parent_tracker.dummyTracker = 1;
			dummy_parent_tracker.def = dummy_def;
			dummy_parent_tracker.parent = tracker->parent;
			dummy_parent_tracker.parent_layer = tracker->parent_layer;
			dummy_parent_tracker.idx_in_parent = tracker->idx_in_parent;

			StructCopyAll(parse_GroupDef, def, dummy_def);
			eaDestroyStruct(&dummy_def->children, parse_GroupChild);
			dummy_def->name_str = allocAddString("Dynamic Dummy String");
			dummy_def->property_structs.physical_properties.bHeadshotVisible = true;
			groupDefSetChildCount(dummy_def, NULL, eaSize(&child_list));

			for (i = 0; i < eaSize(&child_list); i++)
			{
				GroupChild *child = child_list[i];
				GroupDef *child_def = groupChildGetDef(tracker->def, child, false);
				if (child_def)
				{
					GroupTracker dummy_tracker = { 0 };

					copyMat4(child->mat, dummy_def->children[i]->mat);
					dummy_def->children[i]->uid_in_parent = i+1;
					dummy_def->children[i]->seed = child->seed;

					dummy_tracker.dummyTracker = 1;
					dummy_tracker.def = child_def;
					dummy_tracker.parent = &dummy_parent_tracker;
					dummy_tracker.parent_layer = tracker->parent_layer;
					dummy_tracker.idx_in_parent = i;
					dummy_tracker.scale = tracker->scale;

					worldEntryCreateForTracker(&dummy_tracker, child->mat, in_editor, in_headshot, temp_group, is_client, NULL, tracker);

					// Push cell entries all the way up to first non-dummy parent
					eaPushEArray(&tracker->cell_entries, &dummy_tracker.cell_entries);
					eaDestroy(&dummy_tracker.cell_entries);
				}
			}
			eaDestroyStruct(&dummy_def->children, parse_GroupChild);
			groupFreeDynamicChildren(def, child_list);
			StructDestroy(parse_GroupDef, dummy_def);
		}
	}
    else if (spline_tracker != NULL)
    {
        // Recurse here since we don't have a "real" tracker
		GroupChild **def_children = groupGetChildren(def);
        for (i = 0; i < eaSize(&def_children); i++)
        {
			GroupDef *child_def = groupChildGetDef(tracker->def, def_children[i], false);
            if (child_def)
            {
                Mat4 child_mat;
                GroupTracker dummy_tracker = { 0 };
				dummy_tracker.dummyTracker = 1;
                dummy_tracker.def = child_def;
                dummy_tracker.parent = tracker;
                dummy_tracker.parent_layer = tracker->parent_layer;
                dummy_tracker.idx_in_parent = i;
                
                mulMat4Inline(tracker_world_matrix, def_children[i]->mat, child_mat);

//		printf("RECURSING IN TRACKER %X DUMMY %X\n", (int)(intptr_t)tracker, (int)(intptr_t)&dummy_tracker);
                worldEntryCreateForTracker(&dummy_tracker, child_mat, in_editor, in_headshot, temp_group, is_client, params, spline_tracker);

                // Push cell entries all the way up to first non-dummy parent
                eaPushEArray(&tracker->cell_entries, &dummy_tracker.cell_entries);
                eaDestroy(&dummy_tracker.cell_entries);

                // Interaction entries?

                // TomY TODO: Clean up dummy, if necessary
            }
        }
    }

	groupInheritedInfoDestroy(&inherited_info);

	PERFINFO_AUTO_STOP_FUNC();

	return children_need_entries;
}

typedef struct BinSrcEntryBucket
{
	WorldDrawableEntry **src_entries;
} BinSrcEntryBucket;

void worldEntryCreateForWeldedBin(WorldCellWeldedBin *bin)
{
	ZoneMap *zmap = bin->cell->region->zmap_parent;
	int i, j;

	assert(!eaSize(&bin->drawable_entries));

	if (bin->src_type == WCENT_MODEL)
	{
		Vec3 world_min, world_max, mid_min, mid_max;
		const Model *model = (Model*)(-1);
		BinSrcEntryBucket **buckets = NULL;

		// TODO(CD) WELD - breakup welded instance entries so they cover a limited amount of space

		for (i = 0; i < eaSize(&bin->src_entries); ++i)
		{
			WorldDrawableEntry *src_draw = bin->src_entries[i];
			const Model *src_model = worldDrawableEntryGetModel(src_draw, NULL, NULL, NULL, NULL);
			BinSrcEntryBucket *bucket = NULL;

			if (src_model != model && model != (Model*)(-1))
				model = NULL; // this should never happen, since the bins are keyed on the draw list
			else
				model = src_model;

			// try to find a bucket to put this entry in to
			for (j = 0; j < eaSize(&buckets); ++j)
			{
				if (buckets[j]->src_entries[0]->low_detail == src_draw->low_detail && 
					buckets[j]->src_entries[0]->high_detail == src_draw->high_detail && 
					buckets[j]->src_entries[0]->high_fill_detail == src_draw->high_fill_detail && 
					buckets[j]->src_entries[0]->camera_facing == src_draw->camera_facing && 
					buckets[j]->src_entries[0]->axis_camera_facing == src_draw->axis_camera_facing && 
					buckets[j]->src_entries[0]->no_shadow_cast == src_draw->no_shadow_cast)
				{
					bucket = buckets[j];
					break;
				}
			}
			if (!bucket)
			{
				bucket = calloc(1, sizeof(BinSrcEntryBucket));
				eaPush(&buckets, bucket);
			}
			eaPush(&bucket->src_entries, src_draw);
		}

		if (model == (Model*)(-1))
			model = NULL;

		for (j = 0; j < eaSize(&buckets); ++j)
		{
			BinSrcEntryBucket *bucket = buckets[j];
			WorldModelInstanceEntry *instance_entry;
			WorldDrawableEntry *draw;
			WorldCellEntryBounds *bounds;
			Vec3 local_min, local_max;
			F32 radius, near_lod_dist = FLT_MAX, far_lod_dist = 0;

			instance_entry = calloc(1, sizeof(WorldModelInstanceEntry));
			instance_entry->base_drawable_entry.base_entry.type = WCENT_MODELINSTANCED;
			draw = &instance_entry->base_drawable_entry;
			bounds = &draw->base_entry.bounds;

			setVec3same(world_min, FLT_MAX);
			setVec3same(world_max, -FLT_MAX);
			setVec3same(mid_min, FLT_MAX);
			setVec3same(mid_max, -FLT_MAX);
			zeroVec3(draw->world_fade_mid);
			draw->world_fade_radius = 0;

			for (i = 0; i < eaSize(&bucket->src_entries); ++i)
			{
				const WorldModelEntry *src_model_entry = (WorldModelEntry *)bucket->src_entries[i];
				const WorldDrawableEntry *src_draw = &src_model_entry->base_drawable_entry;
				const WorldCellEntry *src_entry = &src_draw->base_entry;
				Vec3 entry_min, entry_max;

				draw->draw_list = src_draw->draw_list;

				draw->camera_facing = draw->camera_facing || src_draw->camera_facing;
				draw->axis_camera_facing = draw->axis_camera_facing || src_draw->axis_camera_facing;
				draw->unlit = draw->unlit || src_draw->unlit;
				assert(!src_draw->editor_only);
				draw->occluder = draw->occluder || src_draw->occluder;
				draw->double_sided_occluder = draw->double_sided_occluder || src_draw->double_sided_occluder;
				draw->low_detail = draw->low_detail || src_draw->low_detail;
				draw->high_detail = draw->high_detail || src_draw->high_detail;
				draw->high_fill_detail = draw->high_fill_detail || src_draw->high_fill_detail;
				draw->map_snap_hidden = draw->map_snap_hidden || src_draw->map_snap_hidden;
				draw->no_shadow_cast = draw->no_shadow_cast || src_draw->no_shadow_cast;
				draw->no_shadow_receive = draw->no_shadow_receive || src_draw->no_shadow_receive;
				draw->force_trunk_wind = draw->force_trunk_wind || src_draw->force_trunk_wind;
				draw->should_cluster = draw->should_cluster || src_draw->should_cluster;

				mulBoundsAA(src_entry->shared_bounds->local_min, src_entry->shared_bounds->local_max, 
					src_entry->bounds.world_matrix, entry_min, entry_max);
				vec3RunningMin(entry_min, world_min);
				vec3RunningMax(entry_max, world_max);
				vec3RunningMinMax(src_entry->bounds.world_mid, mid_min, mid_max);
				MIN1F(near_lod_dist, src_draw->base_entry.shared_bounds->far_lod_near_dist);
				MAX1F(far_lod_dist, src_draw->base_entry.shared_bounds->far_lod_far_dist);

				// average the fade mids, weighting higher radii instances higher
				scaleAddVec3(src_draw->world_fade_mid, 1 + src_draw->world_fade_radius, draw->world_fade_mid, draw->world_fade_mid);
				draw->world_fade_radius += 1 + src_draw->world_fade_radius;

				eaPush(&instance_entry->instances, createInstanceInfoFromModelEntry(src_model_entry));
			}

			// if all instances have the same instance param list, put it on the drawable and remove from the instances
			draw->instance_param_list = instance_entry->instances[0]->instance_param_list;
			for (i = 0; i < eaSize(&instance_entry->instances); ++i)
			{
				if (instance_entry->instances[i]->instance_param_list != draw->instance_param_list)
				{
					draw->instance_param_list = NULL;
					break;
				}
			}

			if (draw->instance_param_list)
			{
				for (i = 0; i < eaSize(&instance_entry->instances); ++i)
				{
					removeInstanceParamListRef(instance_entry->instances[i]->instance_param_list MEM_DBG_PARMS_INIT);
					instance_entry->instances[i]->instance_param_list = NULL;
				}

				addInstanceParamListRef(draw->instance_param_list MEM_DBG_PARMS_INIT);
			}

			// recenter bounds
			radius = boxCalcMid(world_min, world_max, bounds->world_mid);
			subVec3(world_min, bounds->world_mid, local_min);
			subVec3(world_max, bounds->world_mid, local_max);
			copyMat3(unitmat, bounds->world_matrix);
			copyVec3(bounds->world_mid, bounds->world_matrix[3]);

			draw->base_entry.shared_bounds = createSharedBounds(zmap, model, local_min, local_max, radius, near_lod_dist, far_lod_dist, far_lod_dist);

			scaleVec3(draw->world_fade_mid, 1.f / draw->world_fade_radius, draw->world_fade_mid);
			draw->world_fade_radius = 0;
			for (i = 0; i < eaSize(&instance_entry->instances); ++i)
			{
				F32 dist = distance3(instance_entry->instances[i]->world_mid, draw->world_fade_mid) + instance_entry->instances[i]->inst_radius;
				MAX1F(draw->world_fade_radius, dist);
			}

			setVec4same(draw->color, 1);
			instance_entry->lod_idx = draw->draw_list->lod_count - 1;
			addDrawableListRef(draw->draw_list MEM_DBG_PARMS_INIT);

			assert(!draw->base_entry.shared_bounds->ref_count);
			draw->base_entry.shared_bounds = lookupSharedBounds(zmap, draw->base_entry.shared_bounds);

			eaPush(&bin->drawable_entries, draw);

			eaDestroy(&bucket->src_entries);
		}

		eaDestroyEx(&buckets, NULL);
	}
	else
	{
		assertmsg(0, "Tried to weld an unsupported object type!");
	}

	if (bin->cell->bins_state == WCS_OPEN)
	{
		for (i = 0; i < eaSize(&bin->drawable_entries); ++i)
			worldCellEntryOpenAll(&bin->drawable_entries[i]->base_entry, bin->cell->region);
	}
}

WorldRegion *worldCellGetLightRegion(WorldRegion *region)
{
	if (!region->zmap_parent)
		return region;

	switch (zmapGetInfo(NULL)->light_override)
	{
	xcase MAP_LIGHT_OVERRIDE_NONE:
		;

	xcase MAP_LIGHT_OVERRIDE_USE_PRIMARY:
		if (region->zmap_parent && region->zmap_parent != worldGetActiveMap())
		{
			FOR_EACH_IN_EARRAY(zmapGetInfo(NULL)->regions, WorldRegion, active_region)
			{
				if (active_region->name == region->name)
				{
					return active_region;
				}
			}
			FOR_EACH_END;
		}
	xcase MAP_LIGHT_OVERRIDE_USE_SECONDARY:
		if (region->zmap_parent && region->zmap_parent == worldGetActiveMap() &&
			eaSize(&world_grid.maps) > 1)
		{
			ZoneMap *secondary = world_grid.maps[1];
			FOR_EACH_IN_EARRAY(zmapGetInfo(secondary)->regions, WorldRegion, active_region)
			{
				if (active_region->name == region->name)
				{
					return active_region;
				}
			}
			FOR_EACH_END;
		}
	}

	return region;
}

__forceinline static void worldCellEntrySetPartitionOpen(WorldCellEntry *pEntry, int iPartitionIdx, bool bIsOpen)
{
	assert(iPartitionIdx>=0 && iPartitionIdx<=31);
	if (bIsOpen) {
		pEntry->baIsOpen |= (0x00000001 << iPartitionIdx);
	} else {
		pEntry->baIsOpen &= ~(0x00000001 << iPartitionIdx);
	}
}

void worldCellEntryOpen(int iPartitionIdx, WorldCellEntry *entry, WorldRegion *region)
{
	WorldCellEntrySharedBounds *shared_bounds;
	WorldCellEntryBounds *bounds;

	if (!entry)
		return;

	if (worldCellEntryIsPartitionDisabled(entry, iPartitionIdx))
	{
		worldCellEntryClose(iPartitionIdx, entry);
		return;
	}

	if (worldCellEntryIsPartitionOpen(entry, iPartitionIdx))
		return;

	PERFINFO_AUTO_START_FUNC();

	worldCellEntrySetPartitionOpen(entry, iPartitionIdx, true);

	bounds = &entry->bounds;
	shared_bounds = entry->shared_bounds;

	switch (entry->type)
	{
		xcase WCENT_VOLUME:
		{
			WorldVolumeEntry *ent = (WorldVolumeEntry*)entry;
			openWorldVolumeEntry(iPartitionIdx, ent);
		}

		xcase WCENT_COLLISION:
		{
			WorldCollisionEntry *ent = (WorldCollisionEntry*)entry;
			openWorldCollisionEntry(iPartitionIdx, ent);
		}

		xcase WCENT_INTERACTION:
		{
			WorldInteractionEntry *ent = (WorldInteractionEntry*)entry;
			if (ent->uid && !ent->hasInteractionNode)
				wlInteractionOpenForEntry(region->zmap_parent, ent);
		}

		xcase WCENT_SOUND:
		{
			WorldSoundEntry *ent = (WorldSoundEntry*)entry;
			assert(!ent->sound);
			if (wl_state.create_sound_func)
				ent->sound = wl_state.create_sound_func(ent->event_name, ent->excluder_str, ent->dsp_str, 
														ent->editor_group_str, ent->sound_group_str,
														ent->sound_group_ord, bounds->world_matrix);
		}

		xcase WCENT_LIGHT:
		{
			WorldLightEntry *ent = (WorldLightEntry*)entry;
			assert(!ent->light);
			if(wl_state.update_light_func)
			{
				ent->light = wl_state.update_light_func(NULL, ent->light_data, shared_bounds->far_lod_far_dist, GET_REF(ent->animation_controller_handle));
			}
		}

		xcase WCENT_FX:
		{
			WorldFXEntry *ent = (WorldFXEntry*)entry;
			startWorldFX(ent);
		}

		xcase WCENT_WIND_SOURCE:
		{
			WorldWindSourceEntry *ent = (WorldWindSourceEntry*)entry;
			dynWindStartWindSource(ent);
		}
	}

	if (entry->type > WCENT_BEGIN_DRAWABLES)
	{
		WorldDrawableEntry *draw = (WorldDrawableEntry *)entry;
		bool no_lights;
		int lod_idx;
		const Model *model = worldDrawableEntryGetModel(draw, &lod_idx, NULL, NULL, NULL);

		no_lights = draw->unlit || !wl_state.create_static_light_cache_func || entry->type == WCENT_OCCLUSION;

		// JE: Should we be doing this request here?  We don't want to load all the high LODs!
		// CD: For welded far objects it is now just trying to load the specific LOD it needs.
		if (model)
			modelLoadLOD(model, lod_idx);

		if (!no_lights)
		{
			if (draw->light_cache)
			{
				wl_state.invalidate_light_cache_func((GfxLightCacheBase *)draw->light_cache, LCIT_ALL); // force update, since it did not get updates while closed
			}
			else
			{
				WorldRegion *light_region = worldCellGetLightRegion(region);
				draw->light_cache = wl_state.create_static_light_cache_func(draw, light_region);
			}
		}
		else if (draw->light_cache)
		{
			wl_state.free_static_light_cache_func(draw->light_cache);
			draw->light_cache = NULL;
		}
		draw->no_vertex_lighting = draw->no_vertex_lighting || region->bDisableVertexLighting;
	}

	PERFINFO_AUTO_STOP();
}

void worldCellEntryOpenAll(WorldCellEntry *entry, WorldRegion *region)
{
	int i;
	for(i=eaSize(&world_grid.eaWorldColls)-1; i>=0; --i) {
		if (world_grid.eaWorldColls[i]) {
			worldCellEntryOpen(i, entry, region);
		}
	}
}

void worldCellEntryClose(int iPartitionIdx, WorldCellEntry *entry)
{
	if (!entry || !worldCellEntryIsPartitionOpen(entry,iPartitionIdx))
		return;

	PERFINFO_AUTO_START_FUNC();

	// Remove this entry from client override list
	if (entry->type > WCENT_BEGIN_DRAWABLES)
	{
		S32 i;
		for (i = 0; i < eaSize(&g_ppClientOverridenWorldDrawableEntries); i++)
		{
			if (g_ppClientOverridenWorldDrawableEntries[i] && 
				(WorldCellEntry *)g_ppClientOverridenWorldDrawableEntries[i]->pDrawableEntry == entry)
			{
				ClientOverridenWorldDrawableEntry *pOverriddenEntry = g_ppClientOverridenWorldDrawableEntries[i];
				eaRemoveFast(&g_ppClientOverridenWorldDrawableEntries, i);
				StructDestroy(parse_ClientOverridenWorldDrawableEntry, pOverriddenEntry);
				break;
			}
		}
	}
	
	// Clear open flag before calling into type-specific functions
	worldCellEntrySetPartitionOpen(entry, iPartitionIdx, false);

	switch (entry->type)
	{
		xcase WCENT_VOLUME:
		{
			WorldVolumeEntry *ent = (WorldVolumeEntry*)entry;
			closeWorldVolumeEntry(iPartitionIdx, ent);
		}

		xcase WCENT_COLLISION:
		{
			WorldCollisionEntry *ent = (WorldCollisionEntry*)entry;
			closeWorldCollisionEntry(iPartitionIdx, ent);
		}

		xcase WCENT_SOUND:
		{
			WorldSoundEntry *ent = (WorldSoundEntry*)entry;
			if (ent->sound)
			{
				wl_state.remove_sound_func(ent->sound);
				ent->sound = NULL;
			}
		}

		xcase WCENT_LIGHT:
		{
			WorldLightEntry *ent = (WorldLightEntry*)entry;
			if (ent->light)
			{
				wl_state.remove_light_func(ent->light);
				ent->light = NULL;
			}
		}

		xcase WCENT_FX:
		{
			WorldFXEntry *ent = (WorldFXEntry*)entry;
			stopWorldFX(ent);
		}

		xcase WCENT_WIND_SOURCE:
		{
			WorldWindSourceEntry *ent = (WorldWindSourceEntry*)entry;
			dynWindStopWindSource(ent);
		}
	}

	PERFINFO_AUTO_STOP();
}

void worldCellEntryCloseAndClear(int iPartitionIdx, WorldCellEntry *entry)
{
	// Close
	worldCellEntryClose(iPartitionIdx, entry);

	// Clear extra data
	worldCellEntrySetPartitionDisabled(entry, iPartitionIdx, false);
}

void worldCellEntryCloseAll(WorldCellEntry *entry)
{
	int i;
	for(i=eaSize(&world_grid.eaWorldColls)-1; i>=0; --i) {
		if (world_grid.eaWorldColls[i]) {
			worldCellEntryClose(i, entry);
		}
	}
}

void worldCellEntryCloseAndClearAll(WorldCellEntry *entry)
{
	int i;
	for(i=eaSize(&world_grid.eaWorldColls)-1; i>=0; --i) {
		if (world_grid.eaWorldColls[i]) {
			worldCellEntryCloseAndClear(i, entry);
		}
	}
}

static void freeWorldDrawableEntryVertexLightColors(WorldDrawableEntryVertexLightColors* d)
{
	ea32Destroy(&d->vertex_light_colors);
	free(d);
}

void worldCellEntryFree(WorldCellEntry *entry)
{
	WorldCellEntryData *entry_data;
	WorldCell *cell;
	WorldRegion *region;

	if (!entry)
		return;

//	printf("   FREEING ENTRY %X (TYPE %d, CELL %X)\n", (int)(intptr_t)entry, entry->type, (int)(intptr_t)worldCellEntryGetData(entry)->cell);

	PERFINFO_AUTO_START_FUNC();

	cell = worldRemoveCellEntryEx(entry, true, true); // closes the entry and removes it from its cell
	region = SAFE_MEMBER(cell, region);

	entry_data = worldCellEntryGetData(entry);

	assert(!entry_data->cell);

	if (entry_data->parent_entry)
	{
		eaFindAndRemoveFast(&entry_data->parent_entry->child_entries, entry);
	}

	switch (entry->type)
	{
		xcase WCENT_VOLUME:
		{
			WorldVolumeEntry *ent = (WorldVolumeEntry*)entry;

			if(ent->client_volume.water_volume_properties && ent->client_volume.water_volume_properties->water_cond)
				eaFindAndRemoveFast(&ent->fx_condition->water_entries, ent);

			if (ent->client_volume.sky_volume_properties && wl_state.notify_sky_group_freed_func)
				wl_state.notify_sky_group_freed_func(&ent->client_volume.sky_volume_properties->sky_group);

			StructDeInit(parse_GroupVolumePropertiesServer, &ent->server_volume);
			StructDeInit(parse_GroupVolumePropertiesClient, &ent->client_volume);

			eaDestroyStruct(&ent->elements, parse_WorldVolumeElement);
			eaDestroyEx(&ent->eaVolumes, wlVolumeFree);
		}

		xcase WCENT_COLLISION:
		{
			WorldCollisionEntry *ent = (WorldCollisionEntry*)entry;
			int i;

			worldCellCollisionEntryBeingDestroyed(ent);
#if !PSDK_DISABLED
			psdkCookedMeshDestroy(&ent->cooked_mesh);
#endif
			for(i=eaSize(&ent->eaCollObjects)-1; i>=0; --i) {
				if (ent->eaCollObjects[i]) {
					wcoDestroy(&ent->eaCollObjects[i]);
				}
			}
			eaDestroy(&ent->eaCollObjects);

			for(i = 0; i < eaSize(&ent->eaMaterialSwaps); i++)
				SAFE_FREE(ent->eaMaterialSwaps[i]);
			eaDestroy(&ent->eaMaterialSwaps);

			SAFE_FREE(ent->spline);

			trackerHandleDestroy(ent->tracker_handle);
			ent->tracker_handle = NULL;
		}

		xcase WCENT_INTERACTION:
		{
			WorldInteractionEntry *ent = (WorldInteractionEntry*)entry;
			if(wl_state.free_world_node_callback)
				wl_state.free_world_node_callback();
			freeWorldInteractionEntryData(region->zmap_parent, ent);
		}

		xcase WCENT_OCCLUSION:
		{
			WorldOcclusionEntry *ent = (WorldOcclusionEntry*)entry;
			if (ent->owns_model && ent->model)
			{
				tempModelFree(&ent->model);
			}
		}

		xcase WCENT_SOUND:
		{
			WorldSoundEntry *ent = (WorldSoundEntry*)entry;
			destroyWorldSoundEntry(ent);
		}

		xcase WCENT_LIGHT:
		{
			WorldLightEntry *ent = (WorldLightEntry*)entry;
			StructDestroy(parse_LightData, ent->light_data);
			ent->light_data = NULL;
			if (IS_HANDLE_ACTIVE(ent->animation_controller_handle))
				REMOVE_HANDLE(ent->animation_controller_handle);
		}

		xcase WCENT_FX:
		{
			WorldFXEntry *ent = (WorldFXEntry*)entry;
			uninitWorldFXEntry(ent);
		}

		xcase WCENT_ANIMATION:
		{
			WorldAnimationEntry *ent = (WorldAnimationEntry*)entry;
			if (!uninitWorldAnimationEntry(ent))
				return;
		}

		xcase WCENT_MODELINSTANCED:
		{
			WorldModelInstanceEntry *ent = (WorldModelInstanceEntry*)entry;
			eaDestroyEx(&ent->instances, worldFreeModelInstanceInfo);
		}

	}

	if (entry->type > WCENT_BEGIN_DRAWABLES)
	{
		WorldDrawableEntry *ent = (WorldDrawableEntry*)entry;

		if (IS_HANDLE_ACTIVE(ent->animation_controller_handle))
			REMOVE_HANDLE(ent->animation_controller_handle);

		if (IS_HANDLE_ACTIVE(ent->fx_parent_handle))
			REMOVE_HANDLE(ent->fx_parent_handle);

		removeDrawableListRefDbg(ent->draw_list MEM_DBG_PARMS_INIT);
		ent->draw_list = NULL;

		removeInstanceParamListRef(ent->instance_param_list MEM_DBG_PARMS_INIT);
		ent->instance_param_list = NULL;

		if (wl_state.debug.drawable_entry == ent)
			wl_state.debug.drawable_entry = NULL;

		eaDestroyEx(&ent->lod_vertex_light_colors, freeWorldDrawableEntryVertexLightColors);

		if (ent->light_cache)
		{
			wl_state.free_static_light_cache_func(ent->light_cache);
			ent->light_cache = NULL;
		}
	}

	removeSharedBoundsRef(entry->shared_bounds);

	free(entry);

	PERFINFO_AUTO_STOP();
}

static bool worldCollisionMatchesDrawable(const WorldCollisionEntry * entry, const WorldDrawableEntry * drawable)
{
	if (drawable->base_entry.type != WCENT_MODEL && drawable->base_entry.type != WCENT_MODELINSTANCED)
		return false;

	if (entry->model != drawable->base_entry.shared_bounds->model)
		return false;

	if (!sameVec3InMem(entry->base_entry.bounds.world_matrix[3], drawable->base_entry.bounds.world_matrix[3]))
		return false;

	return true;
}

WorldDrawableEntry * worldCellFindDrawableForCollision(const WorldCell * cell, const WorldCollisionEntry * entry)
{
	WorldDrawableEntry **drawable_entries = cell->drawable.drawable_entries;
	int drawable, dc = eaSize(&drawable_entries);
	for (drawable = 0; drawable < dc; ++drawable)
	{
		if (worldCollisionMatchesDrawable(entry, drawable_entries[drawable]))
			return drawable_entries[drawable];
	}

	return NULL;
}

WorldDrawableEntry * worldCollisionEntryToWorldDrawable(const WorldCollisionEntry * entry)
{
	if (entry && entry->base_entry_data.cell)
	{
		const WorldCell * parent_cell = entry->base_entry_data.cell;
		while (parent_cell)
		{
			WorldDrawableEntry *drawable = worldCellFindDrawableForCollision(parent_cell, entry);
			if (drawable)
				return drawable;

			parent_cell = parent_cell->parent;
		}
	}

	return NULL;
}

static  WorldDrawableEntry * worldCellFindDrawableForOccluderInternal(const WorldCell * cell, const WorldOcclusionEntry  * me)
{
	// finds drawable in current cell
	WorldDrawableEntry **drawable_entries = cell->drawable.drawable_entries;

	int drawable, dc = eaSize(&drawable_entries);
	for (drawable = 0; drawable < dc; ++drawable)
	{ 
		WorldDrawableEntry *entry =  drawable_entries[drawable];
		int type = entry->base_entry.type;
		if (type != WCENT_MODEL && type != WCENT_MODELINSTANCED)
			continue;
		if (entry->base_entry.shared_bounds->model!= me->base_drawable_entry.base_entry.shared_bounds->model)
			continue;
		if (!sameVec3InMem(entry->base_entry.bounds.world_matrix[3], me->base_drawable_entry.base_entry.bounds.world_matrix[3]))
			continue;

		return entry;
	}

	return NULL;
}

// I don't know if we need this I am being cautious. Don't have a case for it currently.
static  WorldDrawableEntry * worldCellFindDrawableForOccluderDown(const WorldCell * cell, const WorldOcclusionEntry  * me)
{
	int i;
	WorldDrawableEntry *drawable = worldCellFindDrawableForOccluderInternal(cell,me);
	if(drawable)
		return drawable;

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
	{
		 if(cell->children[i])
		 {
			 drawable = worldCellFindDrawableForOccluderDown(cell->children[i],  me);
			 if(drawable)
				 return drawable;
		 }
	}
	return NULL;
}

WorldDrawableEntry * worldCellFindDrawableForOccluder(const WorldCell * cell, const WorldOcclusionEntry  * me)
{
	// Finds a different object associated with the occluder. In the original source art the names match (with different extensions).
	// It relies on the shared_bounds->model, which points to the original model.
	// I think it might be better to have this relationship noted at export time, so each object knows the other directly without searching
	// but this way prevents a CRC update.
	const WorldCell * tcell = cell;
	 
	while (tcell)
	{
		WorldDrawableEntry *drawable = worldCellFindDrawableForOccluderInternal(tcell, me);
		if(drawable)
			return drawable;
		tcell = tcell->parent;
	}

	// in case it is in a below cell instead. Case not seen yet however.
	if(cell)
		return worldCellFindDrawableForOccluderDown(cell, me);

	return NULL;
 
}

GMesh * worldVolumeCalculateUnionMesh(WorldVolumeEntry *pEntry)
{
	GMesh temp_mesh = {0};
	GMesh * pUnionMesh;
	int i;
	bool atLeastOne = false;

	pUnionMesh = calloc(1, sizeof(GMesh));
	gmeshSetUsageBits(pUnionMesh, USE_POSITIONS);

	for (i = 0; i < eaSize(&pEntry->elements); ++i)
	{ 
		WorldVolumeElement *pElement = pEntry->elements[i];
		GMesh * pMeshToUse = pElement->mesh;
		bool freeMesh = false;

		if (pElement->volume_shape == WL_VOLUME_BOX)
		{
			gmeshFromBoundingBox(&temp_mesh, pElement->local_min, pElement->local_max, pElement->world_mat);
			gmeshInvertTriangles(&temp_mesh);
			pMeshToUse = &temp_mesh;
			atLeastOne = true;
			freeMesh = true;
		}
		else if (pElement->volume_shape == WL_VOLUME_HULL)
		{
			pMeshToUse = pElement->mesh;
			atLeastOne = true;
		}
		else
		{
			Errorf("Cannot use subvolumes other than boxes and meshes to create a collision mesh\n");
			continue;
		}

		if (!pUnionMesh->vert_count)
			gmeshCopy(pUnionMesh, pMeshToUse, true);
		else
			gmeshBoolean(pUnionMesh, pUnionMesh, pMeshToUse, BOOLEAN_UNION, true);

		if (freeMesh)
			gmeshFreeData(&temp_mesh);
	}

	if(!atLeastOne)
	{
		gmeshFree(pUnionMesh);
		return NULL;
	}

	return pUnionMesh;
}

DictionaryHandle hWCENullDictHandle = NULL;

AUTO_RUN;
void wceNullDictInit(void)
{
	hWCENullDictHandle = RefSystem_RegisterNullDictionary("WCENullDict");
}

void worldOcclusionEntryGetScale(const WorldOcclusionEntry *entry, Vec3 scale) {
	if(entry->model && !vec3IsZero(entry->volume_min)) {
		copyVec3(entry->volume_min, scale);
	} else {
		setVec3same(scale, 1);
	}
}

void worldOcclusionEntrySetScale(WorldOcclusionEntry *entry, Vec3 scale) {
	if(entry->model) {
		copyVec3(scale, entry->volume_min);
	}
}

#include "WorldCellEntry_h_ast.c"
