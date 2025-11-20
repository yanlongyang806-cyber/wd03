#include <stdlib.h>
#include <memory.h>
#include "timing.h"
#include "net/net.h"
#include "EString.h"

#include "WorldGridPrivate.h"
#include "grouptrack.h"
#include "wlEncounter.h"
#include "wlState.h"
#include "wlVolumes.h"
#include "wlDebrisField.h"
#include "RoomConn.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

MP_DEFINE(GroupTracker);

GroupTracker *trackerAlloc(void)
{
	MP_CREATE(GroupTracker, 1024);
	return MP_ALLOC(GroupTracker);
}

void trackerFree(GroupTracker *tracker)
{
	if (tracker && (!tracker->parent || tracker->parent->spline_params != tracker->spline_params))
		SAFE_FREE(tracker->spline_params);
	MP_FREE(GroupTracker, tracker);
}

static GroupTracker **trackerAllocChildren(int count)
{
	GroupTracker **trackers;
	int i;

	if (!count)
		return NULL;

	trackers = calloc(count, sizeof(GroupTracker *));
	for (i = 0; i < count; ++i)
		trackers[i] = trackerAlloc();

	return trackers;
}

static void trackerFreeChildren(GroupTracker **trackers, int count)
{
	int i;
	if (!trackers)
		return;
	for (i = 0; i < count; ++i)
		trackerFree(trackers[i]);
	free(trackers);
}

void trackerGetRelativeMat(GroupTracker *tracker, Mat4 world_mat, GroupTracker *src)
{
	Mat4 parent_mat;
	GroupChild **parent_def_children;

	if (!tracker || tracker == src || !tracker->parent)
	{
		copyMat4(unitmat, world_mat);
		return;
	}

	assert(tracker != tracker->parent);

	parent_def_children = groupGetChildren(tracker->parent->def);
	trackerGetRelativeMat(tracker->parent, parent_mat, src);
	mulMat4Inline(parent_mat, parent_def_children[tracker->idx_in_parent]->mat, world_mat);
}

void trackerGetRelativeTint(GroupTracker *tracker, Vec3 tint, GroupTracker *src)
{
	if (!tracker || tracker == src || !tracker->parent)
	{
		setVec3(tint, 1.f, 1.f, 1.f);
		return;
	}

	assert(tracker != tracker->parent);

	trackerGetRelativeTint(tracker->parent, tint, src);
	if (tracker->def && tracker->def->group_flags & GRP_HAS_TINT)
		mulVecVec3(tint, tracker->def->tint_color0, tint);
}

void trackerInit(GroupTracker *tracker)
{
	GroupDef		*def, *parent_def;
	GroupTracker	*parent;

	if (!tracker)
		return;

	def = tracker->def;

	if (!def)
		return;

	parent = tracker->parent;
	parent_def = SAFE_MEMBER(parent, def);

	if (def && def->is_layer && def->def_lib->zmap_layer)
		tracker->parent_layer = def->def_lib->zmap_layer;
	else
		tracker->parent_layer = SAFE_MEMBER(parent, parent_layer);

	tracker->children = NULL;
	tracker->child_count = 0;
	tracker->instance_buffers = NULL;
	tracker->group_mod_time = def->group_mod_time;
	tracker->group_refresh_time = def->group_refresh_time;
	tracker->all_mod_time = def->all_mod_time;

	if (parent && parent->instance_parent)
		tracker->instance_parent = parent->instance_parent;
	else if (parent_def && (parent_def->property_structs.physical_properties.iWeldInstances))
		tracker->instance_parent = parent;
	else
		tracker->instance_parent = NULL;

	if (parent_def)
	{
		GroupChild **parent_def_children = groupGetChildren(tracker->parent->def);
		tracker->uid_in_parent = parent_def_children[tracker->idx_in_parent]->uid_in_parent;
		tracker->parent_child_mod_time = parent_def_children[tracker->idx_in_parent]->child_mod_time;
	}
}

void trackerOpenEx(GroupTracker *tracker, const Mat4 world_mat, F32 scale, bool child_entries, bool temp_group, bool in_headshot)
{
	int				i;
	GroupTracker	*child;
	GroupDef*		def = SAFE_MEMBER(tracker, def);
	Mat4			world_mat_data;
	GroupChild		**def_children;

	if (!tracker || tracker->tracker_opened || tracker->invisible || !def)
		return;

	assert(temp_group || def->def_lib->zmap_layer || def->def_lib->editing_lib || def->def_lib->dummy);

	PERFINFO_AUTO_START(__FUNCTION__, 1);

//printf("OPEN TRACKER %X (DEF %X)\n", (int)(intptr_t)tracker, (int)(intptr_t)tracker->def);

	def_children = groupGetChildren(def);

	if (!world_mat)
	{
		trackerGetMatEx(tracker, world_mat_data, &scale);
		world_mat = world_mat_data;
	}

	if (scale == 0)
		scale = 1;

	tracker->scale = (tracker->parent ? tracker->parent->scale : 1) * scale;

	if(tracker->parent)
	{
		tracker->debris_cont_tracker = tracker->parent->debris_cont_tracker;

		if(tracker->parent->def && tracker->parent->def->property_structs.debris_field_properties)
			tracker->skip_entry_create = true;
		else
			tracker->skip_entry_create = tracker->parent->skip_entry_create;
	}

	if(def->property_structs.physical_properties.bIsDebrisFieldCont)
	{
		//Search children for excluder volumes
		groupTreeTraverse(tracker->parent_layer, def, world_mat, wlDebrisFieldTraversePre, wlDebrisFieldTraversePost, &tracker->debris_excluders, true, true);
		if(eafSize(&tracker->debris_excluders) > 0)
			tracker->debris_cont_tracker = tracker;
	}

	tracker->tracker_opened = 1;

	trackerInit(tracker);

    if (child_entries)
		child_entries = worldEntryCreateForTracker(tracker, world_mat, wlIsClient(), in_headshot, temp_group, wlIsClient(), tracker->spline_params, NULL);

    tracker->child_count = eaSize(&def_children);
    tracker->children = trackerAllocChildren(tracker->child_count);

    for(i=0;i<tracker->child_count;i++)
    {
		bool open_child = true;
        Mat4 child_mat;
        child = tracker->children[i];
        child->def = groupChildGetDef(def, def_children[i], false);
        child->parent = tracker;
        child->idx_in_parent = i;
		child->spline_params = tracker->spline_params;

        trackerInit(child);

        // open all
        if (open_child && child->def)
        {
			Mat4 scaled_mat;
			copyMat3(def_children[i]->mat, scaled_mat);
			scaleVec3(def_children[i]->mat[3], scale, scaled_mat[3]);
            mulMat4Inline(world_mat, scaled_mat, child_mat);
            trackerOpenEx(child, child_mat, def_children[i]->scale, child_entries, temp_group, in_headshot);
        }
    }

	PERFINFO_AUTO_STOP();
}

//WARNING: tracker->def may be pointing to garbage when this function is called.
//If you need something off of the def, you must put it on the tracker in trackerOpen.
void trackerClose(GroupTracker *tracker)
{
	int		i;

	if (!tracker || !tracker->tracker_opened)
		return;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	for (i = 0; i < tracker->child_count; i++)
		trackerClose(tracker->children[i]);
	trackerFreeChildren(tracker->children, tracker->child_count);
	tracker->children = NULL;
	tracker->child_count = 0;
	tracker->tracker_opened = 0;

	eafDestroy(&tracker->debris_excluders);
	splineDestroy(&tracker->inherited_spline);

	if (tracker->instance_info && tracker->instance_parent)
	{
		for (i = 0; i < eaSize(&tracker->instance_parent->instance_buffers); ++i)
		{
			GroupInstanceBuffer *instance_buffer = tracker->instance_parent->instance_buffers[i];
			if (eaFindAndRemoveFast(&instance_buffer->instances, tracker->instance_info) >= 0)
			{
				if (instance_buffer->entry)
				{
					// destroy entry so it can be recreated
					if (tracker->instance_parent->parent_layer)
						eaFindAndRemoveFast(&tracker->instance_parent->parent_layer->cell_entries, &instance_buffer->entry->base_drawable_entry.base_entry);
					eaFindAndRemoveFast(&tracker->instance_parent->cell_entries, &instance_buffer->entry->base_drawable_entry.base_entry);
					worldCellEntryFree(&instance_buffer->entry->base_drawable_entry.base_entry);
					instance_buffer->entry = NULL;
				}

				if (!eaSize(&instance_buffer->instances))
				{
					eaDestroy(&instance_buffer->instances);
					free(instance_buffer);
					eaRemoveFast(&tracker->instance_parent->instance_buffers, i);
					--i;
				}

				break;
			}
		}

		worldFreeModelInstanceInfo(tracker->instance_info);
		tracker->instance_info = NULL;
	}

//	printf("FREE TRACKER %X (DEF %X)\n", (int)(intptr_t)tracker, (int)(intptr_t)tracker->def);

	if (tracker->cell_volume_element)
	{
		GroupTracker *parent;
		for (parent = tracker; parent; parent = parent->parent)
		{
			if (parent->cell_volume_entry)
			{
				eaFindAndRemoveFast(&parent->cell_volume_entry->elements, tracker->cell_volume_element);
				StructDestroy(parse_WorldVolumeElement, tracker->cell_volume_element);
				break;
			}
		}
		tracker->cell_volume_element = NULL;
	}

	for (i = 0; i < eaSize(&tracker->cell_entries); ++i)
	{
		if (tracker->parent_layer)
			eaFindAndRemoveFast(&tracker->parent_layer->cell_entries, tracker->cell_entries[i]);
		worldCellEntryFree(tracker->cell_entries[i]);
	}
	eaDestroy(&tracker->cell_entries);
	tracker->cell_interaction_entry = NULL;
	tracker->cell_animation_entry = NULL;
	tracker->cell_volume_entry = NULL;

	if (tracker->tag_location && tracker->parent_layer)
	{
		WorldRegion *region = layerGetWorldRegion(tracker->parent_layer);
		eaFindAndRemoveFast(&region->tag_locations, tracker->tag_location);
		free(tracker->tag_location);
	}
	tracker->tag_location = NULL;

	for (i = 0; i < eaSize(&tracker->instance_buffers); ++i)
	{
		eaDestroyEx(&tracker->instance_buffers[i]->instances, worldFreeModelInstanceInfo);
		free(tracker->instance_buffers[i]);
	}
	eaDestroy(&tracker->instance_buffers);

	// free scoped data
	if (tracker->closest_scope)
	{
		WorldScope *scope;

		scope = tracker->closest_scope;

		// delete library scopes
		if (tracker == scope->tracker)
		{
			WorldScope *parent_scope = scope->parent_scope;
			assert(parent_scope);
			eaFindAndRemove(&parent_scope->sub_scopes, scope);
			worldScopeDestroy(scope);

			scope = parent_scope;
		}
		// remove logical groups for a layer from zonemap scope
		else if (!tracker->parent)
		{
			WorldZoneMapScope *zmap_scope = (WorldZoneMapScope*) scope;
			StashTableIterator iter;
			StashElement el;
			char *unique_name;

			// remove logical groups from the zonemap scope's location lookup
			stashGetIterator(scope->name_to_group, &iter);
			while (stashGetNextElement(&iter, &el))
			{
				WorldLogicalGroupLoc *loc = stashElementGetPointer(el);
				if (loc->group->common_data.layer == tracker->parent_layer)
					stashRemovePointer(scope->name_to_group, stashElementGetKey(el), NULL);
			}
			for (i = eaSize(&scope->logical_groups) - 1; i >= 0; i--)
			{
				if (scope->logical_groups[i]->common_data.tracker == tracker)
				{
					// remove the logical group from the scope contents
					if (stashAddressRemovePointer(scope->obj_to_name, scope->logical_groups[i], &unique_name))
						stashRemovePointer(scope->name_to_obj, unique_name, NULL);
					worldLogicalGroupDestroy(scope->logical_groups[i]);
					eaFindAndRemove(&zmap_scope->groups, scope->logical_groups[i]);
					eaRemove(&scope->logical_groups, i);
				}
			}
		}

		// delete scope entry
		if (scope && tracker->parent)
			worldScopeRemoveTracker(scope, tracker);
	}
	tracker->closest_scope = NULL;
	tracker->enc_obj = NULL;

	if (tracker->world_civilian_generator)
	{
		if(wl_state.civgen_create_func && wl_state.civgen_destroy_func)
		{
			ZoneMapLayer *layer;
			WorldRegion *region;
			
			// Would be weird not to have these with a civgen extant
			wl_state.civgen_destroy_func(tracker->world_civilian_generator->civ_gen);

			layer = tracker->parent_layer;
			region = layerGetWorldRegion(layer);

			eaFindAndRemoveFast(&region->world_civilian_generators, tracker->world_civilian_generator);			
			StructDestroySafe(parse_WorldCivilianGenerator, &tracker->world_civilian_generator);
		}
	}

	if(tracker->world_path_node)
	{
		ZoneMapLayer *layer = tracker->parent_layer;
		WorldRegion *region = layerGetWorldRegion(layer);
		int j;

		for (j = 0; j < eaSize(&tracker->world_path_node->properties.eaConnections); ++j)
		{
			WorldPathEdge *pEdge = tracker->world_path_node->properties.eaConnections[j];

			if(pEdge)
			{
				int iNode;
				
				for (iNode = 0; iNode < eaSize(&region->world_path_nodes); ++iNode)
				{
					WorldPathNode *pNode = region->world_path_nodes[iNode];

					// Update the node's connection
					if(pNode && pNode->uID == pEdge->uOther)
					{
						int iOther;

						for (iOther = 0; iOther < eaSize(&pNode->properties.eaConnections); ++iOther)
						{
							WorldPathEdge *pOtherEdge = pNode->properties.eaConnections[iOther];

							if (pOtherEdge && pOtherEdge->uOther == tracker->world_path_node->uID)
							{
								eaRemove(&pNode->properties.eaConnections, iOther--);
								StructDestroySafe(parse_WorldPathEdge, &pOtherEdge);
							}
						}
					}
				}
			}
		}

		eaFindAndRemoveFast(&region->world_path_nodes, tracker->world_path_node);
		eaFindAndRemoveFast(&region->world_path_nodes_editor_only, tracker->world_path_node);
		StructDestroySafe(parse_WorldPathNode, &tracker->world_path_node);
	}

	if(tracker->world_forbidden_position)
	{
		WorldRegion *region;
		ZoneMapLayer *layer;

		layer = tracker->parent_layer;
		region = layerGetWorldRegion(layer);

		eaFindAndRemoveFast(&region->world_forbidden_positions, tracker->world_forbidden_position);			
		StructDestroySafe(parse_WorldForbiddenPosition, &tracker->world_forbidden_position);
	}

	// free associated room/portal
	if (tracker->room)
	{
		if (!tracker->parent || !tracker->parent->room)
		{
			if (eaSize(&tracker->room->partitions) > 0)
				stashRemoveInt(tracker->parent_layer->reserved_unique_names, tracker->room->partitions[0]->zmap_scope_name, NULL);
			roomConnGraphRemoveRoom(tracker->room->parent_graph, tracker->room);
		}
		else if (tracker->room_partition && !tracker->parent->room_partition)
		{
			assert(tracker->parent_layer);
			stashRemoveInt(tracker->parent_layer->reserved_unique_names, tracker->room_partition->zmap_scope_name, NULL);
			roomRemovePartition(tracker->room, tracker->room_partition);
		}
		else
			roomRemoveTracker(tracker->room, tracker);

		if (tracker->room_portal)
		{
			if (!tracker->parent || !tracker->parent->room_portal)
			{
				WorldRegion *region = layerGetWorldRegion(tracker->parent_layer);
				roomRemovePortal(tracker->room, tracker->room_portal);
			}
			else
				roomPortalDirty(tracker->room_portal);
		}

		tracker->room = NULL;
		tracker->room_partition = NULL;
		tracker->room_portal = NULL;
	}

	tracker->edit = 0;

	PERFINFO_AUTO_STOP();
}

bool trackerFindModifiedChildren(GroupTracker *tracker)
{
	int i;
	if (!tracker->def) return false;
	for (i = 0; i < tracker->child_count; i++)
	{
		GroupChild **def_children = groupGetChildren(tracker->def);
		if (tracker->children[i]->parent_child_mod_time != def_children[i]->child_mod_time)
			return true;
		else if (trackerFindModifiedChildren(tracker->children[i]))
			return true;
	}
	return false;
}

static bool trackerChildNeedUpdate(GroupTracker *tracker, GroupDef *def)
{
	int i;
	GroupChild **def_children;

	if (!tracker || !def)
		return false;
	if (tracker->def != def)
		return true;
	if (!tracker->tracker_opened)
		return true;
	if(tracker->def->group_mod_time != tracker->group_mod_time || def->all_mod_time != tracker->all_mod_time ||
		tracker->def->group_refresh_time != tracker->group_refresh_time)
		return true;

	def_children = groupGetChildren(def);
	for (i = 0; i < tracker->child_count; i++)
	{
		if(tracker->children[i]->parent_child_mod_time != def_children[i]->child_mod_time)
			return true;
		if(trackerChildNeedUpdate(tracker->children[i], groupChildGetDef(def, def_children[i], false)))
			return true;
	}
	return false;
}

//If you change trackerUpdate such that it changes when something will update, 
//please change trackerChildNeedUpdate so that it correctly detects if an update is needed.
void trackerUpdate(GroupTracker *tracker, GroupDef *def, bool force)
{
	int i, j;

	if (!tracker || !def)
		return;

	if (tracker->def != def)
		force = true;

	if (!tracker->tracker_opened)
		force = true;

	if(!force && (tracker->def->property_structs.debris_field_properties || tracker->def->property_structs.physical_properties.bIsDebrisFieldCont))
		force = trackerChildNeedUpdate(tracker, def);

	for (i = 0; i < tracker->child_count; i++)
		if (tracker->children[i]->def == objectLibraryGetDummyGroupDef())
			force = true;

	// CD: I don't think this is needed anymore
 	if (groupIsObjLib(tracker->def) && trackerFindModifiedChildren(tracker))
 		force = true;

//printf("UPDATE TRACKER %X (DEF %X)\n", (int)(intptr_t)tracker, (int)(intptr_t)tracker->def);

	tracker->def = def;

	if (force || tracker->def->group_mod_time != tracker->group_mod_time ||
			tracker->def->group_refresh_time != tracker->group_refresh_time)
	{
		trackerClose(tracker);
		trackerOpen(tracker);
	}
	else
	{
		GroupChild **def_children = groupGetChildren(def);

		if (def->all_mod_time != tracker->all_mod_time)
		{
			if (!eaSize(&def_children) && tracker->child_count)
			{
				for (j = 0; j < tracker->child_count; ++j)
					trackerClose(tracker->children[j]);
				trackerFreeChildren(tracker->children, tracker->child_count);
				tracker->children = NULL;
				tracker->child_count = 0;
			}
			else if (eaSize(&def_children))
			{
				GroupTracker **new_trackers = calloc(eaSize(&def_children), sizeof(GroupTracker *));

				// copy over old trackers that match children in the def
				for (i = 0; i < eaSize(&def_children); ++i)
				{
					GroupTracker *old_tracker = NULL;

					for (j = 0; j < tracker->child_count; ++j)
					{
						if (tracker->children[j] && 
							tracker->children[j]->uid_in_parent == def_children[i]->uid_in_parent && 
							tracker->children[j]->def == groupChildGetDef(def, def_children[i], false))
						{
							old_tracker = tracker->children[j];
							tracker->children[j] = NULL;
							break;
						}
					}

					if (old_tracker)
					{
						new_trackers[i] = old_tracker;
						old_tracker->idx_in_parent = i;
					}
					else
					{
						new_trackers[i] = trackerAlloc();
						new_trackers[i]->def = groupChildGetDef(def, def_children[i], false);
						new_trackers[i]->parent = tracker;
						new_trackers[i]->idx_in_parent = i;

						trackerInit(new_trackers[i]);
					}
				}

				// close all old trackers that didn't get copied over
				for (j = 0; j < tracker->child_count; ++j)
					trackerClose(tracker->children[j]);

				// replace old child array with new child array
				trackerFreeChildren(tracker->children, tracker->child_count);
				tracker->children = new_trackers;
				tracker->child_count = eaSize(&def_children);
			}

			tracker->all_mod_time = def->all_mod_time;
		}

		// recurse
		for (i = 0; i < tracker->child_count; i++)
			trackerUpdate(tracker->children[i], groupChildGetDef(def, def_children[i], false), tracker->children[i]->parent_child_mod_time != def_children[i]->child_mod_time);
	}
}

static void trackerGetMatRecurse(GroupTracker *tracker, Mat4 world_mat, F32 *scale)
{
	F32 parent_scale;
	Mat4 parent_mat, temp_mat;
	GroupChild **parent_def_children;

	if (!tracker || !tracker->parent)
	{
		copyMat4(unitmat, world_mat);
		if (scale)
			*scale = 1;
		return;
	}

	assert(tracker != tracker->parent);

	parent_def_children = groupGetChildren(tracker->parent->def);

	trackerGetMatRecurse(tracker->parent, parent_mat, &parent_scale);

	copyMat3(parent_def_children[tracker->idx_in_parent]->mat, temp_mat);
	scaleVec3(parent_def_children[tracker->idx_in_parent]->mat[3], parent_scale, temp_mat[3]);
	mulMat4Inline(parent_mat, temp_mat, world_mat);

	if (scale)
	{
		if (parent_def_children[tracker->idx_in_parent]->scale != 0 &&
			parent_def_children[tracker->idx_in_parent]->scale != 1)
			*scale = parent_def_children[tracker->idx_in_parent]->scale;
		else
			*scale = 1;
	}
}

void trackerGetMatEx(GroupTracker *tracker, Mat4 world_mat, F32 *scale)
{
	trackerGetMatRecurse(tracker, world_mat, scale);
}

void trackerGetBounds(GroupTracker *tracker, const Mat4 tracker_world_matrix, Vec3 world_min, Vec3 world_max, Vec3 world_mid, F32 *radius, Mat4 world_mat, GroupSplineParams *spline)
{
	GroupDef *def = tracker->def;
	GroupDef *parent_def = SAFE_MEMBER(tracker->parent, def);
	F32 scale;

	if (tracker_world_matrix)
		copyMat4(tracker_world_matrix, world_mat);
	else
		trackerGetMatEx(tracker, world_mat, &scale);

	if (parent_def && parent_def->spline_params)
		spline = parent_def->spline_params[tracker->idx_in_parent];

	groupGetBounds(def, spline, world_mat, tracker->scale, world_min, world_max, world_mid, radius, world_mat);
}

U32 trackerGetSeed(GroupTracker *tracker)
{
	GroupChild **parent_def_children;
	if (!tracker || !tracker->parent || !tracker->parent->def)
		return 0;
	parent_def_children = groupGetChildren(tracker->parent->def);
	if (eafSize(&tracker->inherited_spline.spline_points) > 0 || tracker->def->property_structs.debris_field_properties ||
		eaSize(&tracker->def->children) > 1 || parent_def_children[tracker->idx_in_parent]->always_use_seed)
	{
		return trackerGetSeed(tracker->parent) ^ parent_def_children[tracker->idx_in_parent]->seed;
	}
	// This nonsensical exception exists because it exists in applyDefToGroupInfo, in grouputil.c. Look there.
	return parent_def_children[tracker->idx_in_parent]->seed;
}

GroupDef **trackerGetDefChain(GroupTracker *tracker)
{
	GroupDef **parent_defs = NULL;

	for (; tracker; tracker = tracker->parent)
		eaPush(&parent_defs, tracker->def);
	eaReverse(&parent_defs);

	return parent_defs;
}

void trackerUpdateWireframeRecursive(GroupTracker *tracker)
{
	int i;
	if (!tracker)
		return;

	for (i = 0; i < eaSize(&tracker->cell_entries); ++i)
		worldEntryUpdateWireframe(tracker->cell_entries[i], tracker);

	for (i = 0; i < tracker->child_count; ++i)
		trackerUpdateWireframeRecursive(tracker->children[i]);
}

void trackerSetSelected(GroupTracker *tracker, bool selected)
{
	if (!tracker)
		return;

	selected = !!selected;
	if (selected == tracker->selected)
		return;

	tracker->selected = selected;
	trackerUpdateWireframeRecursive(tracker);
}

void trackerStopOnFXChanged(GroupTracker *tracker)
{
	trackerSetInvisible(tracker, true);
}

void trackerNotifyOnFXChanged(void *widget_unused, void *block_unused, GroupTracker *tracker)
{
	trackerSetInvisible(tracker, false);
}

void trackerSetInvisible(GroupTracker *tracker, bool invisible)
{
	if (!tracker)
		return;

	invisible = !!invisible;
	if (invisible == tracker->invisible)
		return;

	tracker->invisible = invisible;
	if (invisible)
		trackerClose(tracker);
	else
		trackerOpen(tracker);
}

//////////////////////////////////////////////////////////////////////////
// tracker handles

TrackerHandle *trackerHandleCreate(GroupTracker *tracker)
{
	TrackerHandle *handle;

	if (!tracker)
		return NULL;

	handle = StructCreate(parse_TrackerHandle);

	for (; tracker; tracker = tracker->parent)
	{
		if (!tracker->parent)
		{
			if (tracker->def && tracker->def->is_layer && tracker->def->def_lib->zmap_layer)
			{
				handle->zmap_name = StructAllocString(zmapGetFilename(tracker->def->def_lib->zmap_layer->zmap_parent));
				handle->layer_name = StructAllocString(tracker->def->def_lib->zmap_layer->filename);
			}
		}
		else if (tracker->parent->def)
		{
			GroupChild **parent_def_children = groupGetChildren(tracker->parent->def);
			U32 uid = parent_def_children[tracker->idx_in_parent]->uid_in_parent;
			assert(uid);
			eaiPush(&handle->uids, uid);
		}
	}

	eaiReverse(&handle->uids);

	return handle;
}

TrackerHandle *trackerHandleCreateFromDefChain(GroupDef **def_chain, int *idxs_in_parent)
{
	TrackerHandle *handle;
	GroupDef *parent_def = NULL;
	int i;

	if (!eaSize(&def_chain))
		return NULL;

	ANALYSIS_ASSUME(def_chain);

	handle = StructCreate(parse_TrackerHandle);

	for (i = 0; i < eaSize(&def_chain); ++i)
	{
		GroupDef *def = def_chain[i];
		if (!parent_def)
		{
			if (def && def->is_layer && def->def_lib->zmap_layer && def->def_lib->zmap_layer->zmap_parent && def->def_lib->zmap_layer->filename)
			{
				handle->zmap_name = StructAllocString(zmapGetFilename(def->def_lib->zmap_layer->zmap_parent));
				handle->layer_name = StructAllocString(def->def_lib->zmap_layer->filename);
			}
		}
		else
		{
			GroupChild **parent_def_children = groupGetChildren(parent_def);
			U32 uid = parent_def_children[idxs_in_parent[i]]->uid_in_parent;
			assert(uid);
			eaiPush(&handle->uids, uid);
		}

		parent_def = def;
	}

	return handle;
}

void trackerHandleDestroy(TrackerHandle *handle)
{
	if (!handle)
		return;

	StructDestroy(parse_TrackerHandle, handle);
}

TrackerHandle *trackerHandleCopy(const TrackerHandle *handle)
{
	TrackerHandle *ret;
	if (!handle)
		return NULL;

	ret = StructClone(parse_TrackerHandle, handle);
	return ret;
}

void trackerHandlePushUID(TrackerHandle *handle, U32 uid)
{
	if (!handle)
		return;
	assert(uid);
	eaiPush(&handle->uids, uid);
}

U32 trackerHandlePopUID(TrackerHandle *handle)
{
	if (!handle)
		return 0;
	return eaiPop(&handle->uids);
}

GroupTracker *trackerFromTrackerHandle(const TrackerHandle *handle)
{
	GroupTracker *tracker;
	ZoneMap *zmap;
	ZoneMapLayer *layer;
	int i, j;

	if (!handle || !handle->zmap_name || !handle->layer_name)
		return NULL;

	zmap = worldGetLoadedZoneMapByName(handle->zmap_name);
	if (!zmap)
		return NULL;

	layer = zmapGetLayerByName(zmap, handle->layer_name);
	if (!layer)
		return NULL;

	tracker = layerGetTracker(layer);

	if (!tracker)
		return NULL;

	trackerOpen(tracker);

	for (i = 0; i < eaiSize(&handle->uids); ++i)
	{
		GroupChild **def_children = groupGetChildren(tracker->def);
		bool broken = false;

		for (j = 0; j < tracker->child_count; ++j)
		{
			if (handle->uids[i] == def_children[j]->uid_in_parent)
			{
				tracker = tracker->children[j];
				broken = true;
				break;
			}
		}

		if (!broken)
			return NULL;

		trackerOpen(tracker);
	}

	return tracker;
}

GroupDef *groupDefFromTrackerHandle(const TrackerHandle *handle)
{
	GroupDef *def;
	ZoneMap *zmap;
	ZoneMapLayer *layer;
	int i, j;

	if (!handle || !handle->zmap_name || !handle->layer_name)
		return NULL;

	zmap = worldGetLoadedZoneMapByName(handle->zmap_name);
	if (!zmap)
		return NULL;

	layer = zmapGetLayerByName(zmap, handle->layer_name);
	if (!layer)
		return NULL;

	def = layerGetDef(layer);

	if (!def)
		return NULL;

	for (i = 0; i < eaiSize(&handle->uids); ++i)
	{
		GroupChild **def_children = groupGetChildren(def);
		bool broken = false;

		for (j = 0; j < eaSize(&def_children); ++j)
		{
			if (handle->uids[i] == def_children[j]->uid_in_parent)
			{
				def = groupChildGetDef(def, def_children[j], true);
				broken = true;
				break;
			}
		}

		if (!broken)
			return NULL;
	}

	return def;
}

TrackerHandle *trackerHandleFromTracker(GroupTracker *tracker)
{
	static TrackerHandle *handle = NULL;
	trackerHandleDestroy(handle);
	handle = trackerHandleCreate(tracker);
	return handle;
}

TrackerHandle *trackerHandleFromDefChain(GroupDef **def_chain, int *idxs_in_parent)
{
	static TrackerHandle *handle = NULL;
	trackerHandleDestroy(handle);
	handle = trackerHandleCreateFromDefChain(def_chain, idxs_in_parent);
	return handle;
}

GroupTracker *layerTrackerFromTrackerHandle(TrackerHandle *handle)
{
	ZoneMap *zmap;
	ZoneMapLayer *layer;

	if (!handle || !handle->zmap_name || !handle->layer_name)
		return NULL;

	zmap = worldGetLoadedZoneMapByName(handle->zmap_name);
	if (!zmap)
		return NULL;

	layer = zmapGetLayerByName(zmap, handle->layer_name);
	if (!layer)
		return NULL;

	return layerGetTracker(layer);
}

// you could sort with this, but the real point is that if they're equal it
// returns zero
int trackerHandleComp(const TrackerHandle *a, const TrackerHandle *b)
{
	int i;
	int t;

	t = stricmp(a->zmap_name, b->zmap_name);
	if (t)
		return t;

	t = stricmp(a->layer_name, b->layer_name);
	if (t)
		return t;

	for (i = 0; i < eaiSize(&a->uids) && i < eaiSize(&b->uids); ++i)
	{
		t = a->uids[i] - b->uids[i];
		if (t)
			return t;
	}

	return eaiSize(&a->uids) - eaiSize(&b->uids);
}

void pktSendTrackerHandle(Packet *pak, const TrackerHandle *handle)
{
	if (!handle || !handle->zmap_name || !handle->layer_name)
	{
		pktSendBitsAuto(pak, 0);
		return;
	}

	pktSendBitsAuto(pak, 1);
	pktSendStruct(pak, handle, parse_TrackerHandle);
}

TrackerHandle *pktGetTrackerHandle(Packet *pak)
{
	TrackerHandle *handle;

	if (!pktGetBitsAuto(pak))
		return NULL;

	handle = pktGetStruct(pak, parse_TrackerHandle);
	return handle;
}

char *stringFromTrackerHandle(const TrackerHandle *handle)
{
	int i;
	char *str = NULL, *strnew;

	if (!handle || !handle->zmap_name || !handle->layer_name)
		return NULL;

	estrStackCreate(&str);
	estrPrintf(&str, "\"%s\":\"%s\"", handle->zmap_name, handle->layer_name);
	for (i = 0; i < eaiSize(&handle->uids); ++i)
		estrConcatf(&str, ":%d", handle->uids[i]);

	strnew = strdup(str);
	estrDestroy(&str);
	return strnew;
}

TrackerHandle *trackerHandleFromString(const char *str)
{
	int i, argc;
	char *s, *args[256];
	TrackerHandle *handle;

	if (!str)
		return NULL;

	strdup_alloca(s, str);
	argc = tokenize_line_quoted_delim(s, args, ARRAY_SIZE(args), NULL, ":", ":");

	if (argc < 2)
		return NULL;

	handle = StructCreate(parse_TrackerHandle);

	handle->zmap_name = StructAllocString(args[0]);
	handle->layer_name = StructAllocString(args[1]);

	eaiSetSize(&handle->uids, argc-2);
	for (i = 2; i < argc; ++i)
		handle->uids[i-2] = atoi(args[i]);

	return handle;
}

char *handleStringFromTracker(GroupTracker *tracker)
{
	TrackerHandle *handle = trackerHandleFromTracker(tracker); // don't need to free it, it returns a static
	return stringFromTrackerHandle(handle);
}

char *handleStringFromDefChain(GroupDef **def_chain, int *idxs_in_parent)
{
	TrackerHandle *handle = trackerHandleFromDefChain(def_chain, idxs_in_parent); // don't need to free it, it returns a static
	return stringFromTrackerHandle(handle);
}

GroupTracker *trackerFromHandleString(const char *str)
{
	TrackerHandle *handle = trackerHandleFromString(str);
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	trackerHandleDestroy(handle);
	return tracker;
}

void trackerIdxListFromTrackerHandle(TrackerHandle *handle, int **idx_list)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	
	if (!tracker)
		return;

	for (; tracker; tracker = tracker->parent)
	{
		eaiPush(idx_list, tracker->idx_in_parent);
	}

	eaiReverse(idx_list);
}

///////////////////////////////////////////////////////////////

GroupTracker *trackerFromUniqueName(GroupTracker *scope_tracker, const char *name)
{
	char *path_copy, *path, *c;
	int i;

	if (!scope_tracker->def || !scope_tracker->def->name_to_path || !stashFindPointer(scope_tracker->def->name_to_path, name, &path))
		return NULL;

	strdup_alloca(path_copy, path);
	c = strtok_r(path_copy, ",", &path_copy);
	while (c)
	{
		int uid = atoi(c);
		bool found = false;
		if (!uid)
			return trackerFromUniqueName(scope_tracker, c);

		for (i = 0; i < scope_tracker->child_count; i++)
		{
			if (scope_tracker->children[i]->uid_in_parent == uid)
			{
				scope_tracker = scope_tracker->children[i];
				found = true;
				break;
			}
		}

		if (!found)
			return NULL;
		c = strtok_r(path_copy, ",", &path_copy);

		if (scope_tracker->def->name_to_path)
			return trackerFromUniqueName(scope_tracker, c);
	}

	return scope_tracker;
}

GroupTracker *trackerGetScopeTracker(GroupTracker *tracker)
{
	if (!tracker)
		return NULL;

	do
	{
		tracker = tracker->parent;
	} while (tracker && tracker->parent && !groupHasScope(tracker->def));

	return tracker;
}

const char *trackerGetUniqueScopeName(GroupDef *scope_def, GroupTracker *tracker, const char *group_name)
{
	int *uids = NULL;
	char *unique_name = NULL;
	int i;

	if (!tracker || !tracker->closest_scope)
		return NULL;
	if (group_name)
	{
		if (!tracker->def)
			return NULL;
		for (i = 0; i < eaSize(&tracker->def->logical_groups); i++)
		{
			if (strcmpi(tracker->def->logical_groups[i]->group_name, group_name) == 0)
				break;
		}
		if (i >= eaSize(&tracker->def->logical_groups))
			return NULL;
		else
			unique_name = (char*) group_name;
	}

	while (tracker && tracker->closest_scope)
	{
		// for scope defs
		if ((tracker->def && tracker->def == tracker->closest_scope->def) || (!tracker->closest_scope->parent_scope && !tracker->parent))
		{
			if (eaiSize(&uids) > 0)
			{
				char *path = NULL;
				eaiReverse(&uids);

				for (i = 0; i < eaiSize(&uids); i++)
					estrConcatf(&path, "%i,", uids[i]);
				if (unique_name)
					estrConcatf(&path, "%s,", unique_name);

				stashFindPointer(tracker->def->path_to_name, path, &unique_name);
				estrDestroy(&path);
			}

			if ((scope_def && tracker->def == scope_def) || (!scope_def && !tracker->parent))
			{
				eaiDestroy(&uids);
				return unique_name;
			}

			eaiClear(&uids);
		}

		eaiPush(&uids, tracker->uid_in_parent);
		tracker = tracker->parent;
	}

	eaiDestroy(&uids);
	return NULL;
}

const char *trackerGetUniqueZoneMapScopeName(GroupTracker *tracker)
{
	GroupDef *layer_def;
	
	if (!tracker)
		return NULL;

	layer_def = layerGetDef(tracker->parent_layer);
	return trackerGetUniqueScopeName(layer_def, tracker, NULL);
}


typedef struct TraverserGlobalDrawParams
{
	TrackerTreeTraverserCallback	callback;
	void						*user_data;
} TraverserGlobalDrawParams;

static void trackerTreeTraverseHelper(GroupTracker *tracker, U32 groupChildSeed, const Mat4 local_mat_p, const Mat4 parent_mat_p, TrackerTreeTraverserDrawParams *parent_draw, TraverserGlobalDrawParams *gdraw)
{
	TrackerTreeTraverserDrawParams local_draw = *parent_draw;
	int			i, child_count, visible_child = -1;
	GroupChild	*childGroup;
	GroupTracker *child=0;
	GroupChild **def_children;
	GroupDef *def = tracker->def;

	if (!def)
		return;

	def_children = groupGetChildren(def);

	local_draw.tracker = tracker;

	trackerOpen(tracker);

	if (!def->property_structs.physical_properties.bVisible) {
		local_draw.editor_visible_only = 1;
	} else {
		// Inherit from parent
	}

	local_draw.seed = parent_draw->seed ^ groupChildSeed;

	mulMat4(parent_mat_p, local_mat_p, local_draw.world_mat);

	if (!gdraw->callback(gdraw->user_data, &local_draw))
		return;

	if (def->property_structs.physical_properties.bRandomSelect)
		visible_child = groupGetRandomChildIdx(def_children, local_draw.seed);
	else if (def->property_structs.physical_properties.bIsChildSelect)
		visible_child = def->property_structs.physical_properties.iChildSelectIdx;

	child_count = min(eaSize(&def_children), tracker->child_count);
	for (i=0; i < child_count; i++)  
	{	
		childGroup = def_children[i];
		child = tracker->children[i];
		if (visible_child == -1 || visible_child == i)
		{
			trackerTreeTraverseHelper(child, childGroup->seed, childGroup->mat, local_draw.world_mat, &local_draw, gdraw);
		}
	}
}

void trackerTreeTraverse(ZoneMapLayer *layer, GroupTracker *tracker, TrackerTreeTraverserCallback callback, void *user_data)
{
	TrackerTreeTraverserDrawParams local_draw = {0};
	TraverserGlobalDrawParams gdraw = {0};

	if (!tracker)
		return;

	gdraw.callback = callback;
	gdraw.user_data = user_data;
	
	trackerTreeTraverseHelper(tracker, 0, unitmat, unitmat, &local_draw, &gdraw);
}
