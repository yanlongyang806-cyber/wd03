#include "wlEncounter.h"
#include "WorldGridPrivate.h"
#include "Quat.h"
#include "qsortG.h"
#include "StringCache.h"
#include "timing_profiler.h"

#include "wlEncounter_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

/********************
* LOGICAL GROUPING
********************/
void worldLogicalGroupRemoveEncounterObject(WorldLogicalGroup *group, WorldEncounterObject *object)
{
	int index = eaFind(&group->objects, object);
	object->parent_group = NULL;
	if (index >= 0)
		group->objects[index] = NULL;
}

void worldLogicalGroupAddEncounterObject(WorldLogicalGroup *group, WorldEncounterObject *object, int index)
{
	if (!object->parent_group)
		object->parent_group = group;
	if (index >= 0)
	{
		eaSet(&group->objects, object, index);
	}
	else
		eaPush(&group->objects, object);
}

/********************
* WORLD INTEGRATION
********************/
static void worldScopeCreateInternal(GroupDef *def, GroupTracker *tracker, ZoneMapLayer *layer, WorldScope *scope, WorldScope *parent_scope)
{
	scope->def = def;
	scope->tracker = tracker;
	scope->layer = layer;
	if (parent_scope)
	{
		eaPush(&parent_scope->sub_scopes, scope);
		scope->parent_scope = parent_scope;
	}
}

WorldScope *worldScopeCreate(GroupDef *def, GroupTracker *tracker, ZoneMapLayer *layer, WorldScope *parent_scope)
{
	WorldScope *scope = StructCreate(parse_WorldScope);
	worldScopeCreateInternal(def, tracker, layer, scope, parent_scope);
	return scope;
}

static void worldScopeDestroyUnparsed(WorldScope *scope)
{
	WorldScope *zmap_scope, *current_scope;
	int i;

	// recurse downward first
	eaForEach(&scope->sub_scopes, worldScopeDestroyUnparsed);

	// calculate zmap scope
	zmap_scope = scope;
	while(zmap_scope->parent_scope)
		zmap_scope = zmap_scope->parent_scope;

	// remove logical groups from parents
	for (i = 0; i < eaSize(&scope->logical_groups); i++)
	{
		if (scope->logical_groups[i]->common_data.parent_group)
			eaFindAndRemove(&scope->logical_groups[i]->common_data.parent_group->objects, (WorldEncounterObject*) scope->logical_groups[i]);
	}

	// remove logical groups from all parent scope stashes
	for (i = 0; i < eaSize(&scope->logical_groups); i++)
	{
		current_scope = scope->parent_scope;
		while (current_scope)
		{
			char *unique_name;
			stashAddressRemovePointer(current_scope->obj_to_name, scope->logical_groups[i], &unique_name);
			stashRemovePointer(current_scope->name_to_obj, unique_name, NULL);
			current_scope = current_scope->parent_scope;
		}

		// remove group from zonemap scope
		eaFindAndRemove(&((WorldZoneMapScope*) zmap_scope)->groups, scope->logical_groups[i]);
	}
	eaDestroyEx(&scope->logical_groups, worldLogicalGroupDestroy);

	// destroy stashes
	stashTableDestroyEx(scope->name_to_group, NULL, worldLogicalGroupLocDestroy);
	if (scope->name_to_obj)
		stashTableDestroy(scope->name_to_obj);
	if (scope->obj_to_name)
		stashTableDestroy(scope->obj_to_name);
}

void worldScopeDestroy(WorldScope *scope)
{
	// destroy unparsed data
	worldScopeDestroyUnparsed(scope);

	// destroy parsed data
	StructDestroy(parse_WorldScope, scope);
}

WorldZoneMapScope *worldZoneMapScopeCreate(void)
{
	WorldZoneMapScope *zmap_scope = StructCreate(parse_WorldZoneMapScope);

	// setup base scope
	worldScopeCreateInternal(NULL, NULL, NULL, &zmap_scope->scope, NULL);

	return zmap_scope;
}

static void worldZoneMapScopeDestroyUnparsed(WorldZoneMapScope *zmap_scope)
{
	// destroy unparsed scope data
	worldScopeDestroyUnparsed(&zmap_scope->scope);
}

void worldZoneMapScopeDestroy(WorldZoneMapScope *zmap_scope)
{
	// destroy unparsed data
	worldZoneMapScopeDestroyUnparsed(zmap_scope);

	// destroy parsed data
	StructDestroy(parse_WorldZoneMapScope, zmap_scope);
}

void worldScopeAddData(WorldScope *scope, const char *unique_name, void *data)
{
	WorldEncounterObject *object = (WorldEncounterObject *)data;

	const char *key_name;
	if (!scope->name_to_obj)
		scope->name_to_obj = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	assert(stashAddPointer(scope->name_to_obj, unique_name, object, false));
	assert(stashGetKey(scope->name_to_obj, unique_name, &key_name));
	if (!scope->obj_to_name)
		scope->obj_to_name = stashTableCreateAddress(16);
	assert(stashAddPointer(scope->obj_to_name, object, key_name, false));
}

void worldLogicalGroupDestroy(WorldLogicalGroup *logical_group)
{
	FOR_EACH_IN_EARRAY(logical_group->objects, WorldEncounterObject, object)
	{
		if(!object)
			continue;

		devassert(object->parent_group==logical_group || !object->parent_group);
		object->parent_group = NULL;
	}
	FOR_EACH_END;
	eaDestroy(&logical_group->objects);
	if(logical_group->common_data.parent_group!=NULL)
	{
		WorldLogicalGroup *parent = logical_group->common_data.parent_group;
		int idx = eaFind(&parent->objects, (WorldEncounterObject*)logical_group);
		eaSet(&parent->objects, NULL, idx);

		logical_group->common_data.parent_group = NULL;
	}
	StructDestroy(parse_WorldLogicalGroup, logical_group);
}

WorldLogicalGroup *worldZoneMapScopeAddLogicalGroup(WorldZoneMapScope *zmap_scope, WorldScope *closest_scope, ZoneMapLayer *layer, GroupTracker *tracker, LogicalGroupProperties *properties)
{
	WorldLogicalGroup *world_lg = StructCreate(parse_WorldLogicalGroup);

	// set group data
	world_lg->common_data.closest_scope = closest_scope;
	world_lg->common_data.type = WL_ENC_LOGICAL_GROUP;
	world_lg->common_data.layer = layer;
	world_lg->common_data.tracker = tracker;

	world_lg->properties = StructClone(parse_LogicalGroupProperties, properties);

	// add encounter to layer scope's flat list and the scope's top-level group list
	eaPush(&zmap_scope->groups, world_lg);
	eaPush(&closest_scope->logical_groups, world_lg);

	return world_lg;
}

WorldLogicalGroupLoc *worldLogicalGroupLocCreate(WorldLogicalGroup *logical_group, int index)
{
	WorldLogicalGroupLoc *loc = StructCreate(parse_WorldLogicalGroupLoc);
	loc->group = logical_group;
	loc->index = index;
	return loc;
}

void worldLogicalGroupLocDestroy(WorldLogicalGroupLoc *loc)
{
	loc->group = NULL;
	StructDestroy(parse_WorldLogicalGroupLoc, loc);
}

WorldEncounterHack *worldZoneMapScopeAddEncounterHack(WorldZoneMapScope *zmap_scope, WorldScope *closest_scope, WorldInteractionEntry *parent_entry, int parent_entry_child_idx, ZoneMapLayer *layer, GroupDef *def, GroupTracker *tracker, const Mat4 world_mat)
{
	WorldEncounterHack *encounter = StructCreate(parse_WorldEncounterHack);

	// set encounter data
	encounter->common_data.closest_scope = closest_scope;
	encounter->common_data.parent_node_entry = parent_entry;
	encounter->common_data.parent_node_child_idx = parent_entry_child_idx;
	encounter->common_data.type = WL_ENC_ENCOUNTER_HACK;
	encounter->common_data.layer = layer;
	encounter->common_data.tracker = tracker;

	encounter->properties = StructClone(parse_WorldEncounterHackProperties, def->property_structs.encounter_hack_properties);
	copyVec3(world_mat[3], encounter->encounter_pos);
	mat3ToQuat(world_mat, encounter->encounter_rot);

	// add encounter to layer scope's flat list
	eaPush(&zmap_scope->encounter_hacks, encounter);

	return encounter;
}

WorldEncounter *worldZoneMapScopeAddEncounter(WorldZoneMapScope *zmap_scope, WorldScope *closest_scope, WorldInteractionEntry *parent_entry, int parent_entry_child_idx, ZoneMapLayer *layer, GroupDef *def, GroupTracker *tracker, const Mat4 world_mat)
{
	WorldEncounter *encounter = StructCreate(parse_WorldEncounter);
	Mat4 actor_mat, temp;
	int i;

	// set encounter data
	encounter->common_data.closest_scope = closest_scope;
	encounter->common_data.parent_node_entry = parent_entry;
	encounter->common_data.parent_node_child_idx = parent_entry_child_idx;
	encounter->common_data.type = WL_ENC_ENCOUNTER;
	encounter->common_data.layer = layer;
	encounter->common_data.tracker = tracker;

	encounter->properties = StructClone(parse_WorldEncounterProperties, def->property_structs.encounter_properties);
	assert(encounter->properties);
	for (i = 0; i < eaSize(&encounter->properties->eaActors); i++)
	{
		copyVec3(encounter->properties->eaActors[i]->vPos, actor_mat[3]);
		createMat3YPR(actor_mat, encounter->properties->eaActors[i]->vRot);
		mulMat4(world_mat, actor_mat, temp);
		getMat3YPR(temp, encounter->properties->eaActors[i]->vRot);
		copyVec3(temp[3], encounter->properties->eaActors[i]->vPos);
	}

	copyVec3(world_mat[3], encounter->encounter_pos);
	mat3ToQuat(world_mat, encounter->encounter_rot);

	// add encounter to layer scope's flat list
	eaPush(&zmap_scope->encounters, encounter);

	return encounter;
}

WorldNamedInteractable *worldZoneMapScopeAddInteractable(WorldZoneMapScope *zmap_scope, WorldScope *closest_scope, WorldInteractionEntry *parent_entry, int parent_entry_child_idx, ZoneMapLayer *layer, GroupTracker *tracker, WorldInteractionEntry *entry, const Mat4 world_mat)
{
	WorldNamedInteractable *interactable = StructCreate(parse_WorldNamedInteractable);

	// set encounter data

	interactable->common_data.closest_scope = closest_scope;
	interactable->common_data.parent_node_entry = parent_entry;
	interactable->common_data.parent_node_child_idx = parent_entry_child_idx;
	interactable->common_data.type = WL_ENC_INTERACTABLE;
	interactable->common_data.layer = layer;
	interactable->common_data.tracker = tracker;

	interactable->entry = entry;

	assert(entry != parent_entry); // Can't have an interactable be its own parent

	// add to layer scope's flat list
	eaPush(&zmap_scope->interactables, interactable);

	return interactable;
}

WorldSpawnPoint *worldZoneMapScopeAddSpawnPoint(WorldZoneMapScope *zmap_scope, WorldScope *closest_scope, WorldInteractionEntry *parent_entry, int parent_entry_child_idx, ZoneMapLayer *layer, GroupDef *def, GroupTracker *tracker, const Mat4 world_mat)
{
	WorldSpawnPoint *spawn_point = StructCreate(parse_WorldSpawnPoint);

	// set spawn point data
	spawn_point->common_data.closest_scope = closest_scope;
	spawn_point->common_data.parent_node_entry = parent_entry;
	spawn_point->common_data.parent_node_child_idx = parent_entry_child_idx;
	spawn_point->common_data.type = WL_ENC_SPAWN_POINT;
	spawn_point->common_data.layer = layer;
	spawn_point->common_data.tracker = tracker;

	spawn_point->properties = StructClone(parse_WorldSpawnProperties, def->property_structs.spawn_properties);
	copyVec3(world_mat[3], spawn_point->spawn_pos);
	mat3ToQuat(world_mat, spawn_point->spawn_rot);

	// add spawn point to layer scope's flat list
	eaPush(&zmap_scope->spawn_points, spawn_point);

	return spawn_point;
}

WorldTriggerCondition *worldZoneMapScopeAddTriggerCondition(WorldZoneMapScope *zmap_scope, WorldScope *closest_scope, WorldInteractionEntry *parent_entry, int parent_entry_child_idx, ZoneMapLayer *layer, GroupDef *def, GroupTracker *tracker)
{
	WorldTriggerCondition *trigger_condition = StructCreate(parse_WorldTriggerCondition);

	// set trigger condition data
	trigger_condition->common_data.closest_scope = closest_scope;
	trigger_condition->common_data.parent_node_entry = parent_entry;
	trigger_condition->common_data.parent_node_child_idx = parent_entry_child_idx;
	trigger_condition->common_data.type = WL_ENC_TRIGGER_CONDITION;
	trigger_condition->common_data.layer = layer;
	trigger_condition->common_data.tracker = tracker;

	trigger_condition->properties = StructClone(parse_WorldTriggerConditionProperties, def->property_structs.trigger_condition_properties);

	// add trigger condition to layer scope's flat list
	eaPush(&zmap_scope->trigger_conditions, trigger_condition);

	return trigger_condition;
}

WorldLayerFSM *worldZoneMapScopeAddLayerFSM(WorldZoneMapScope *zmap_scope, WorldScope *closest_scope, WorldInteractionEntry *parent_entry, int parent_entry_child_idx, ZoneMapLayer *layer, GroupDef *def, GroupTracker *tracker)
{
	WorldLayerFSM *layer_fsm = StructCreate(parse_WorldLayerFSM);

	// set trigger condition data
	layer_fsm->common_data.closest_scope = closest_scope;
	layer_fsm->common_data.parent_node_entry = parent_entry;
	layer_fsm->common_data.parent_node_child_idx = parent_entry_child_idx;
	layer_fsm->common_data.type = WL_ENC_LAYER_FSM;
	layer_fsm->common_data.layer = layer;
	layer_fsm->common_data.tracker = tracker;

	layer_fsm->properties = StructClone(parse_WorldLayerFSMProperties, def->property_structs.layer_fsm_properties);

	// add trigger condition to layer scope's flat list
	eaPush(&zmap_scope->layer_fsms, layer_fsm);

	return layer_fsm;
}

WorldNamedPoint *worldZoneMapScopeAddNamedPoint(WorldZoneMapScope *zmap_scope, WorldScope *closest_scope, WorldInteractionEntry *parent_entry, int parent_entry_child_idx, ZoneMapLayer *layer, GroupTracker *tracker, const Mat4 world_mat)
{
	WorldNamedPoint *named_point = StructCreate(parse_WorldNamedPoint);

	// set spawn point data
	named_point->common_data.closest_scope = closest_scope;
	named_point->common_data.parent_node_entry = parent_entry;
	named_point->common_data.parent_node_child_idx = parent_entry_child_idx;
	named_point->common_data.type = WL_ENC_NAMED_POINT;
	named_point->common_data.layer = layer;
	named_point->common_data.tracker = tracker;

	copyVec3(world_mat[3], named_point->point_pos);
	mat3ToQuat(world_mat, named_point->point_rot);

	// add named point to layer scope's flat list
	eaPush(&zmap_scope->named_points, named_point);

	return named_point;
}

WorldPatrolRoute *worldZoneMapScopeAddPatrolRoute(WorldZoneMapScope *zmap_scope, WorldScope *closest_scope, WorldInteractionEntry *parent_entry, int parent_entry_child_idx, ZoneMapLayer *layer, GroupDef *def, GroupTracker *tracker, const Mat4 world_mat)
{
	WorldPatrolRoute *route = StructCreate(parse_WorldPatrolRoute);
	Vec3 temp;
	int i;

	// set route data
	route->common_data.closest_scope = closest_scope;
	route->common_data.parent_node_entry = parent_entry;
	route->common_data.parent_node_child_idx = parent_entry_child_idx;
	route->common_data.type = WL_ENC_PATROL_ROUTE;
	route->common_data.layer = layer;
	route->common_data.tracker = tracker;

	route->properties = StructClone(parse_WorldPatrolProperties, def->property_structs.patrol_properties);
	assert(route->properties);
	for (i = 0; i < eaSize(&route->properties->patrol_points); i++)
	{
		mulVecMat4(route->properties->patrol_points[i]->pos, world_mat, temp);
		copyVec3(temp, route->properties->patrol_points[i]->pos);
	}

	// add route to layer scope's flat list
	eaPush(&zmap_scope->patrol_routes, route);

	return route;
}

WorldNamedVolume *worldZoneMapScopeAddNamedVolume(WorldZoneMapScope *zmap_scope, WorldScope *closest_scope, WorldInteractionEntry *parent_entry, int parent_entry_child_idx, ZoneMapLayer *layer, GroupTracker *tracker, WorldVolumeEntry *entry, const Mat4 world_mat)
{
	WorldNamedVolume *named_volume = StructCreate(parse_WorldNamedVolume);

	// set spawn point data
	named_volume->common_data.closest_scope = closest_scope;
	named_volume->common_data.parent_node_entry = parent_entry;
	named_volume->common_data.parent_node_child_idx = parent_entry_child_idx;
	named_volume->common_data.type = WL_ENC_NAMED_VOLUME;
	named_volume->common_data.layer = layer;
	named_volume->common_data.tracker = tracker;
	named_volume->entry = entry;

	// add named volume to layer scope's flat list
	eaPush(&zmap_scope->named_volumes, named_volume);

	return named_volume;
}


static void worldScopeRemoveObj(WorldEncounterObject *obj)
{
	char *name = NULL;
	WorldScope *scope = obj->closest_scope;

	while (scope)
	{
		if (scope->obj_to_name && scope->name_to_obj && stashRemovePointer(scope->obj_to_name, obj, &name))
			stashRemovePointer(scope->name_to_obj, name, NULL);
		if (obj->type == WL_ENC_LOGICAL_GROUP)
			eaFindAndRemove(&scope->logical_groups, (WorldLogicalGroup*) obj);
		scope = scope->parent_scope;
	}
}

void worldZoneMapScopeUnloadLayer(WorldZoneMapScope *zmap_scope, ZoneMapLayer *layer)
{
	int i;

	for (i = eaSize(&zmap_scope->groups) - 1; i >= 0; i--)
	{
		if (zmap_scope->groups[i]->common_data.layer == layer)
		{
			worldScopeRemoveObj(&zmap_scope->groups[i]->common_data);
			worldLogicalGroupDestroy(zmap_scope->groups[i]);
			eaRemove(&zmap_scope->groups, i);
		}
	}
	for (i = eaSize(&zmap_scope->encounter_hacks) - 1; i >= 0; i--)
	{
		if (zmap_scope->encounter_hacks[i]->common_data.layer == layer)
		{
			worldScopeRemoveObj(&zmap_scope->encounter_hacks[i]->common_data);
			StructDestroy(parse_WorldEncounterHack, zmap_scope->encounter_hacks[i]);
			eaRemove(&zmap_scope->encounter_hacks, i);
		}
	}
	for (i = eaSize(&zmap_scope->encounters) - 1; i >= 0; i--)
	{
		if (zmap_scope->encounters[i]->common_data.layer == layer)
		{
			worldScopeRemoveObj(&zmap_scope->encounters[i]->common_data);
			StructDestroy(parse_WorldEncounter, zmap_scope->encounters[i]);
			eaRemove(&zmap_scope->encounters, i);
		}
	}
	for (i = eaSize(&zmap_scope->interactables) - 1; i >= 0; i--)
	{
		if (zmap_scope->interactables[i]->common_data.layer == layer)
		{
			worldScopeRemoveObj(&zmap_scope->interactables[i]->common_data);
			StructDestroy(parse_WorldNamedInteractable, zmap_scope->interactables[i]);
			eaRemove(&zmap_scope->interactables, i);
		}
	}
	for (i = eaSize(&zmap_scope->spawn_points) - 1; i >= 0; i--)
	{
		if (zmap_scope->spawn_points[i]->common_data.layer == layer)
		{
			worldScopeRemoveObj(&zmap_scope->spawn_points[i]->common_data);
			StructDestroy(parse_WorldSpawnPoint, zmap_scope->spawn_points[i]);
			eaRemove(&zmap_scope->spawn_points, i);
		}
	}
	for (i = eaSize(&zmap_scope->patrol_routes) - 1; i >= 0; i--)
	{
		if (zmap_scope->patrol_routes[i]->common_data.layer == layer)
		{
			worldScopeRemoveObj(&zmap_scope->patrol_routes[i]->common_data);
			StructDestroy(parse_WorldPatrolRoute, zmap_scope->patrol_routes[i]);
			eaRemove(&zmap_scope->patrol_routes, i);
		}
	}
	for (i = eaSize(&zmap_scope->named_points) - 1; i >= 0; i--)
	{
		if (zmap_scope->named_points[i]->common_data.layer == layer)
		{
			worldScopeRemoveObj(&zmap_scope->named_points[i]->common_data);
			StructDestroy(parse_WorldNamedPoint, zmap_scope->named_points[i]);
			eaRemove(&zmap_scope->named_points, i);
		}
	}
	for (i = eaSize(&zmap_scope->named_volumes) - 1; i >= 0; i--)
	{
		if (zmap_scope->named_volumes[i]->common_data.layer == layer)
		{
			worldScopeRemoveObj(&zmap_scope->named_volumes[i]->common_data);
			StructDestroy(parse_WorldNamedVolume, zmap_scope->named_volumes[i]);
			eaRemove(&zmap_scope->named_volumes, i);
		}
	}
	for (i = eaSize(&zmap_scope->trigger_conditions) - 1; i >= 0; i--)
	{
		if (zmap_scope->trigger_conditions[i]->common_data.layer == layer)
		{
			worldScopeRemoveObj(&zmap_scope->trigger_conditions[i]->common_data);
			StructDestroy(parse_WorldTriggerCondition, zmap_scope->trigger_conditions[i]);
			eaRemove(&zmap_scope->trigger_conditions, i);
		}
	}
	for (i = eaSize(&zmap_scope->layer_fsms) - 1; i >= 0; i--)
	{
		if (zmap_scope->layer_fsms[i]->common_data.layer == layer)
		{
			worldScopeRemoveObj(&zmap_scope->layer_fsms[i]->common_data);
			StructDestroy(parse_WorldLayerFSM, zmap_scope->layer_fsms[i]);
			eaRemove(&zmap_scope->layer_fsms, i);
		}
	}
	for (i = eaSize(&zmap_scope->scope.sub_scopes) - 1; i >=0; i--)
	{
		if (zmap_scope->scope.sub_scopes[i]->layer == layer)
		{
			worldScopeDestroy(zmap_scope->scope.sub_scopes[i]);
			eaRemove(&zmap_scope->scope.sub_scopes, i);
		}
	}
}

void worldScopeRemoveTracker(WorldScope *closest_scope, GroupTracker *tracker)
{
	WorldZoneMapScope *zmap_scope;
	WorldScope *current_scope = closest_scope;
	WorldEncounterObject **eaObj = NULL;
	int i;

	assert(current_scope);
	while (current_scope->parent_scope)
		current_scope = current_scope->parent_scope;
	zmap_scope = (WorldZoneMapScope*) current_scope;

	// find corresponding object
	for (i = 0; i < eaSize(&zmap_scope->encounter_hacks); i++)
	{
		if (zmap_scope->encounter_hacks[i]->common_data.tracker == tracker)
		{
			eaPush(&eaObj, &zmap_scope->encounter_hacks[i]->common_data);
			eaRemove(&zmap_scope->encounter_hacks, i);
			break;
		}
	}
	for (i = 0; i < eaSize(&zmap_scope->encounters); i++)
	{
		if (zmap_scope->encounters[i]->common_data.tracker == tracker)
		{
			eaPush(&eaObj, &zmap_scope->encounters[i]->common_data);
			eaRemove(&zmap_scope->encounters, i);
			break;
		}
	}
	for (i = 0; i < eaSize(&zmap_scope->interactables); i++)
	{
		if (zmap_scope->interactables[i]->common_data.tracker == tracker)
		{
			eaPush(&eaObj, &zmap_scope->interactables[i]->common_data);
			eaRemove(&zmap_scope->interactables, i);
			break;
		}
	}
	for (i = 0; i < eaSize(&zmap_scope->spawn_points); i++)
	{
		if (zmap_scope->spawn_points[i]->common_data.tracker == tracker)
		{
			eaPush(&eaObj, &zmap_scope->spawn_points[i]->common_data);
			eaRemove(&zmap_scope->spawn_points, i);
			break;
		}
	}
	for (i = 0; i < eaSize(&zmap_scope->patrol_routes); i++)
	{
		if (zmap_scope->patrol_routes[i]->common_data.tracker == tracker)
		{
			eaPush(&eaObj, &zmap_scope->patrol_routes[i]->common_data);
			eaRemove(&zmap_scope->patrol_routes, i);
			break;
		}
	}
	for (i = 0; i < eaSize(&zmap_scope->named_points); i++)
	{
		if (zmap_scope->named_points[i]->common_data.tracker == tracker)
		{
			eaPush(&eaObj, &zmap_scope->named_points[i]->common_data);
			eaRemove(&zmap_scope->named_points, i);
			break;
		}
	}
	for (i = 0; i < eaSize(&zmap_scope->named_volumes); i++)
	{
		if (zmap_scope->named_volumes[i]->common_data.tracker == tracker)
		{
			eaPush(&eaObj, &zmap_scope->named_volumes[i]->common_data);
			eaRemove(&zmap_scope->named_volumes, i);
			break;
		}
	}
	for (i = 0; i < eaSize(&zmap_scope->trigger_conditions); i++)
	{
		if (zmap_scope->trigger_conditions[i]->common_data.tracker == tracker)
		{
			eaPush(&eaObj, &zmap_scope->trigger_conditions[i]->common_data);
			eaRemove(&zmap_scope->trigger_conditions, i);
			break;
		}
	}
	for (i = 0; i < eaSize(&zmap_scope->layer_fsms); i++)
	{
		if (zmap_scope->layer_fsms[i]->common_data.tracker == tracker)
		{
			eaPush(&eaObj, &zmap_scope->layer_fsms[i]->common_data);
			eaRemove(&zmap_scope->layer_fsms, i);
			break;
		}
	}

	// Remove each object from all other systems
	for (i = 0; i < eaSize(&eaObj); i++)
	{
		WorldEncounterObject* obj = eaObj[i];
		// remove the object from its logical group
		if (obj->parent_group)
			worldLogicalGroupRemoveEncounterObject(obj->parent_group, obj);

		// remove the object from all parent scope hash tables
		current_scope = closest_scope;
		while (current_scope)
		{
			char *unique_name;
			if (current_scope->obj_to_name && current_scope->name_to_obj)
			{
				stashAddressRemovePointer(current_scope->obj_to_name, obj, &unique_name);
				stashRemovePointer(current_scope->name_to_obj, unique_name, NULL);
			}
			current_scope = current_scope->parent_scope;
		}

		// destroy object data
		switch (obj->type)
		{
			xcase WL_ENC_ENCOUNTER_HACK:
				StructDestroyVoid(parse_WorldEncounterHack, obj);
			xcase WL_ENC_ENCOUNTER:
				StructDestroyVoid(parse_WorldEncounter, obj);
			xcase WL_ENC_SPAWN_POINT:
				StructDestroyVoid(parse_WorldSpawnPoint, obj);
			xcase WL_ENC_PATROL_ROUTE:
				StructDestroyVoid(parse_WorldPatrolRoute, obj);
			xcase WL_ENC_NAMED_POINT:
				StructDestroyVoid(parse_WorldNamedPoint, obj);
			xcase WL_ENC_TRIGGER_CONDITION:
				StructDestroyVoid(parse_WorldTriggerCondition, obj);
			xcase WL_ENC_LAYER_FSM:
				StructDestroyVoid(parse_WorldLayerFSM, obj);
		}
	}
}

/********************
* BINNED CONVERSION
********************/
static void worldScopeCleanupBinData(WorldScope *scope_binned)
{
	int i;

	eaDestroyStruct(&scope_binned->name_id_pairs, parse_WorldScopeNameId);

	for (i = 0; i < eaSize(&scope_binned->sub_scopes); i++)
		worldScopeCleanupBinData(scope_binned->sub_scopes[i]);
}

static int cmpNameIdPair(const WorldScopeNameId **ppPair1, const WorldScopeNameId **ppPair2)
{
	return stricmp((*ppPair1)->name, (*ppPair2)->name);
}

static bool worldScopePrepareForBins(WorldScope *scope)
{
	StashTableIterator iter;
	StashElement el;
	bool foundData;
	int i;

	scope->layer_idx = layerIdxInParent(scope->layer);
	stashGetIterator(scope->name_to_obj, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		const char *key = stashElementGetStringKey(el);
		WorldEncounterObject *object = stashElementGetPointer(el);

		if (key && object)
		{
			WorldScopeNameId *name_id_pair = StructCreate(parse_WorldScopeNameId);
			name_id_pair->name = StructAllocString(stashElementGetStringKey(el));
			name_id_pair->unique_id = object->unique_id;
			eaPush(&scope->name_id_pairs, name_id_pair);
		}
	}

	eaQSortG(scope->name_id_pairs, cmpNameIdPair);
	foundData = (eaSize(&scope->name_id_pairs) > 0);

	for (i = eaSize(&scope->sub_scopes) - 1; i >= 0; i--)
	{
		bool ret = worldScopePrepareForBins(scope->sub_scopes[i]);

		// remove scopes without any data
		if (!ret)
			worldScopeDestroy(eaRemove(&scope->sub_scopes, i));

		foundData |= ret;
	}

	return foundData;
}

static void worldScopeProcessBinData(WorldScope *scope_binned, StashTable id_to_obj, StashTable id_to_scope, WorldScope *parent_scope)
{
	int i;

	scope_binned->layer = zmapGetLayer(NULL, scope_binned->layer_idx);
	scope_binned->parent_scope = parent_scope;
	for (i = 0; i < eaSize(&scope_binned->name_id_pairs); i++)
	{
		void *obj;
		if (stashIntFindPointer(id_to_obj, scope_binned->name_id_pairs[i]->unique_id, &obj))
		{
			const char *key_name;

			if (!scope_binned->name_to_obj)
				scope_binned->name_to_obj = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
			if (!scope_binned->obj_to_name)
				scope_binned->obj_to_name = stashTableCreateAddress(16);

			assert(stashAddPointer(scope_binned->name_to_obj, scope_binned->name_id_pairs[i]->name, obj, false));
			assert(stashGetKey(scope_binned->name_to_obj, scope_binned->name_id_pairs[i]->name, &key_name));
			assert(stashAddPointer(scope_binned->obj_to_name, obj, key_name, false));
			stashIntAddPointer(id_to_scope, scope_binned->name_id_pairs[i]->unique_id, scope_binned, true);
		}
	}

	for (i = 0; i < eaSize(&scope_binned->sub_scopes); i++)
		worldScopeProcessBinData(scope_binned->sub_scopes[i], id_to_obj, id_to_scope, scope_binned);
}

void worldZoneMapScopeCleanupBinData(WorldZoneMapScope *zmap_scope_binned)
{
	int i;

	for (i = 0; i < eaSize(&zmap_scope_binned->groups); i++)
		eaiDestroy(&zmap_scope_binned->groups[i]->object_ids);

	worldScopeCleanupBinData(&zmap_scope_binned->scope);
}

static void worldZoneMapScopePrepareForBinsSetLayer(WorldEncounterObject *object)
{
	ZoneMapLayer *layer = object->layer;
//	if (layer && layer->dummy_layer)
//		layer = layer->dummy_layer;
	object->layer_idx = layerIdxInParent(layer);
}

static void worldZoneMapScopePrepareForBinsSetParentNode(StashTable interactable_entry_to_unique_id, WorldEncounterObject *object)
{
	if (object->parent_node_entry)
	{
		assert(stashFindInt(interactable_entry_to_unique_id, object->parent_node_entry, &object->parent_node_id));
	}
}

void worldZoneMapScopePrepareForBins(WorldZoneMapScope *zmap_scope, StashTable volume_entry_to_id, StashTable interactable_entry_to_id)
{
	int unique_id = 1;
	int i, j;
	StashTable interactable_entry_to_unique_id;

	// Local stash table for mapping unique IDs
	interactable_entry_to_unique_id = stashTableCreate(256, StashDefault, StashKeyTypeAddress, sizeof(void*));

	// Do interactables first in two passes.  First sets IDs.  Second sets parent linkage.
	for (i = 0; i < eaSize(&zmap_scope->interactables); i++)
	{
		if (zmap_scope->interactables[i]->entry)
			assert(stashFindInt(interactable_entry_to_id, zmap_scope->interactables[i]->entry, &zmap_scope->interactables[i]->entry_id));
		else
			zmap_scope->interactables[i]->entry_id = 0;

		zmap_scope->interactables[i]->common_data.unique_id = unique_id++;
		stashAddInt(interactable_entry_to_unique_id, zmap_scope->interactables[i]->entry, zmap_scope->interactables[i]->common_data.unique_id, false);

		worldZoneMapScopePrepareForBinsSetLayer(&zmap_scope->interactables[i]->common_data);
	}
	for (i = 0; i < eaSize(&zmap_scope->interactables); i++)
	{
		worldZoneMapScopePrepareForBinsSetParentNode(interactable_entry_to_unique_id, &zmap_scope->interactables[i]->common_data);
	}

	// populate other ID's
	for (i = 0; i < eaSize(&zmap_scope->encounter_hacks); i++)
	{
		zmap_scope->encounter_hacks[i]->common_data.unique_id = unique_id++;
		worldZoneMapScopePrepareForBinsSetLayer(&zmap_scope->encounter_hacks[i]->common_data);
		worldZoneMapScopePrepareForBinsSetParentNode(interactable_entry_to_unique_id, &zmap_scope->encounter_hacks[i]->common_data);
	}
	for (i = 0; i < eaSize(&zmap_scope->encounters); i++)
	{
		zmap_scope->encounters[i]->common_data.unique_id = unique_id++;
		worldZoneMapScopePrepareForBinsSetLayer(&zmap_scope->encounters[i]->common_data);
		worldZoneMapScopePrepareForBinsSetParentNode(interactable_entry_to_unique_id, &zmap_scope->encounters[i]->common_data);
	}
	for (i = 0; i < eaSize(&zmap_scope->spawn_points); i++)
	{
		zmap_scope->spawn_points[i]->common_data.unique_id = unique_id++;
		worldZoneMapScopePrepareForBinsSetLayer(&zmap_scope->spawn_points[i]->common_data);
		worldZoneMapScopePrepareForBinsSetParentNode(interactable_entry_to_unique_id, &zmap_scope->spawn_points[i]->common_data);
	}
	for (i = 0; i < eaSize(&zmap_scope->patrol_routes); i++)
	{
		zmap_scope->patrol_routes[i]->common_data.unique_id = unique_id++;
		worldZoneMapScopePrepareForBinsSetLayer(&zmap_scope->patrol_routes[i]->common_data);
		worldZoneMapScopePrepareForBinsSetParentNode(interactable_entry_to_unique_id, &zmap_scope->patrol_routes[i]->common_data);
	}
	for (i = 0; i < eaSize(&zmap_scope->named_points); i++)
	{
		zmap_scope->named_points[i]->common_data.unique_id = unique_id++;
		worldZoneMapScopePrepareForBinsSetLayer(&zmap_scope->named_points[i]->common_data);
		worldZoneMapScopePrepareForBinsSetParentNode(interactable_entry_to_unique_id, &zmap_scope->named_points[i]->common_data);
	}
	for (i = 0; i < eaSize(&zmap_scope->named_volumes); i++)
	{
		if (zmap_scope->named_volumes[i]->entry)
			assert(stashFindInt(volume_entry_to_id, zmap_scope->named_volumes[i]->entry, &zmap_scope->named_volumes[i]->entry_id));
		else
			zmap_scope->named_volumes[i]->entry_id = 0;
		zmap_scope->named_volumes[i]->common_data.unique_id = unique_id++;
		worldZoneMapScopePrepareForBinsSetLayer(&zmap_scope->named_volumes[i]->common_data);
		worldZoneMapScopePrepareForBinsSetParentNode(interactable_entry_to_unique_id, &zmap_scope->named_volumes[i]->common_data);
	}
	for (i = 0; i < eaSize(&zmap_scope->trigger_conditions); i++)
	{
		zmap_scope->trigger_conditions[i]->common_data.unique_id = unique_id++;
		worldZoneMapScopePrepareForBinsSetLayer(&zmap_scope->trigger_conditions[i]->common_data);
		worldZoneMapScopePrepareForBinsSetParentNode(interactable_entry_to_unique_id, &zmap_scope->trigger_conditions[i]->common_data);
	}
	for (i = 0; i < eaSize(&zmap_scope->layer_fsms); i++)
	{
		zmap_scope->layer_fsms[i]->common_data.unique_id = unique_id++;
		worldZoneMapScopePrepareForBinsSetLayer(&zmap_scope->layer_fsms[i]->common_data);
		worldZoneMapScopePrepareForBinsSetParentNode(interactable_entry_to_unique_id, &zmap_scope->layer_fsms[i]->common_data);
	}
	for (i = 0; i < eaSize(&zmap_scope->groups); i++)
	{
		zmap_scope->groups[i]->common_data.unique_id = unique_id++;
		worldZoneMapScopePrepareForBinsSetLayer(&zmap_scope->groups[i]->common_data);
	}

	// populate binned group content ID's
	for (i = 0; i < eaSize(&zmap_scope->groups); i++)
	{
		for (j = 0; j < eaSize(&zmap_scope->groups[i]->objects); j++)
		{
			if (zmap_scope->groups[i]->objects[j])
				eaiPush(&zmap_scope->groups[i]->object_ids, zmap_scope->groups[i]->objects[j]->unique_id);
		}
	}

	worldScopePrepareForBins(&zmap_scope->scope);
	stashTableDestroy(interactable_entry_to_unique_id);
}

void worldZoneMapScopeProcessBinData(WorldZoneMapScope *zmap_scope_binned, StashTable id_to_named_volume, StashTable id_to_interactable)
{
	StashTable id_to_obj = stashTableCreateInt(16);
	StashTable id_to_scope = stashTableCreateInt(16);
	StashTableIterator iter;
	StashElement el;
	WorldScope *closest_scope;
	WorldNamedInteractable *closest_node;
	int i, j;

	// create ID-to-interactable hash
	for (i = 0; i < eaSize(&zmap_scope_binned->interactables); i++)
	{
		if (zmap_scope_binned->interactables[i]->entry_id)
			stashIntAddPointer(id_to_interactable, zmap_scope_binned->interactables[i]->entry_id, zmap_scope_binned->interactables[i], false);
	}

	// create ID-to-obj stash
	for (i = 0; i < eaSize(&zmap_scope_binned->encounter_hacks); i++)
	{
		stashIntAddPointer(id_to_obj, zmap_scope_binned->encounter_hacks[i]->common_data.unique_id, zmap_scope_binned->encounter_hacks[i], false);
		zmap_scope_binned->encounter_hacks[i]->common_data.layer = zmapGetLayer(NULL, zmap_scope_binned->encounter_hacks[i]->common_data.layer_idx);
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->encounters); i++)
	{
		stashIntAddPointer(id_to_obj, zmap_scope_binned->encounters[i]->common_data.unique_id, zmap_scope_binned->encounters[i], false);
		zmap_scope_binned->encounters[i]->common_data.layer = zmapGetLayer(NULL, zmap_scope_binned->encounters[i]->common_data.layer_idx);
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->interactables); i++)
	{
		stashIntAddPointer(id_to_obj, zmap_scope_binned->interactables[i]->common_data.unique_id, zmap_scope_binned->interactables[i], false);
		zmap_scope_binned->interactables[i]->common_data.layer = zmapGetLayer(NULL, zmap_scope_binned->interactables[i]->common_data.layer_idx);
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->spawn_points); i++)
	{
		stashIntAddPointer(id_to_obj, zmap_scope_binned->spawn_points[i]->common_data.unique_id, zmap_scope_binned->spawn_points[i], false);
		zmap_scope_binned->spawn_points[i]->common_data.layer = zmapGetLayer(NULL, zmap_scope_binned->spawn_points[i]->common_data.layer_idx);
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->trigger_conditions); i++)
	{
		stashIntAddPointer(id_to_obj, zmap_scope_binned->trigger_conditions[i]->common_data.unique_id, zmap_scope_binned->trigger_conditions[i], false);
		zmap_scope_binned->trigger_conditions[i]->common_data.layer = zmapGetLayer(NULL, zmap_scope_binned->trigger_conditions[i]->common_data.layer_idx);
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->layer_fsms); i++)
	{
		stashIntAddPointer(id_to_obj, zmap_scope_binned->layer_fsms[i]->common_data.unique_id, zmap_scope_binned->layer_fsms[i], false);
		zmap_scope_binned->layer_fsms[i]->common_data.layer = zmapGetLayer(NULL, zmap_scope_binned->layer_fsms[i]->common_data.layer_idx);
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->patrol_routes); i++)
	{
		stashIntAddPointer(id_to_obj, zmap_scope_binned->patrol_routes[i]->common_data.unique_id, zmap_scope_binned->patrol_routes[i], false);
		zmap_scope_binned->patrol_routes[i]->common_data.layer = zmapGetLayer(NULL, zmap_scope_binned->patrol_routes[i]->common_data.layer_idx);
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->named_points); i++)
	{
		stashIntAddPointer(id_to_obj, zmap_scope_binned->named_points[i]->common_data.unique_id, zmap_scope_binned->named_points[i], false);
		zmap_scope_binned->named_points[i]->common_data.layer = zmapGetLayer(NULL, zmap_scope_binned->named_points[i]->common_data.layer_idx);
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->named_volumes); i++)
	{
		stashIntAddPointer(id_to_obj, zmap_scope_binned->named_volumes[i]->common_data.unique_id, zmap_scope_binned->named_volumes[i], false);
		zmap_scope_binned->named_volumes[i]->common_data.layer = zmapGetLayer(NULL, zmap_scope_binned->named_volumes[i]->common_data.layer_idx);
		if (zmap_scope_binned->named_volumes[i]->entry_id)
			stashIntAddPointer(id_to_named_volume, zmap_scope_binned->named_volumes[i]->entry_id, zmap_scope_binned->named_volumes[i], false);
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->groups); i++)
	{
		stashIntAddPointer(id_to_obj, zmap_scope_binned->groups[i]->common_data.unique_id, zmap_scope_binned->groups[i], false);
		zmap_scope_binned->groups[i]->common_data.layer = zmapGetLayer(NULL, zmap_scope_binned->groups[i]->common_data.layer_idx);
	}

	// add contents to binned groups
	for (i = 0; i < eaSize(&zmap_scope_binned->groups); i++)
	{
		for (j = 0; j < eaiSize(&zmap_scope_binned->groups[i]->object_ids); j++)
		{
			WorldEncounterObject *object;
			if (stashIntFindPointer(id_to_obj, zmap_scope_binned->groups[i]->object_ids[j], &object))
				worldLogicalGroupAddEncounterObject(zmap_scope_binned->groups[i], object, -1);
		}
	}

	worldScopeProcessBinData(&zmap_scope_binned->scope, id_to_obj, id_to_scope, NULL);

	// set closest scopes and nodes
	for (i = 0; i < eaSize(&zmap_scope_binned->encounter_hacks); i++)
	{
		if (stashIntFindPointer(id_to_scope, zmap_scope_binned->encounter_hacks[i]->common_data.unique_id, &closest_scope))
			zmap_scope_binned->encounter_hacks[i]->common_data.closest_scope = closest_scope;
		if (stashIntFindPointer(id_to_obj, zmap_scope_binned->encounter_hacks[i]->common_data.parent_node_id, &closest_node))
			zmap_scope_binned->encounter_hacks[i]->common_data.parent_node_object = closest_node;
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->encounters); i++)
	{
		if (stashIntFindPointer(id_to_scope, zmap_scope_binned->encounters[i]->common_data.unique_id, &closest_scope))
			zmap_scope_binned->encounters[i]->common_data.closest_scope = closest_scope;
		if (stashIntFindPointer(id_to_obj, zmap_scope_binned->encounters[i]->common_data.parent_node_id, &closest_node))
			zmap_scope_binned->encounters[i]->common_data.parent_node_object = closest_node;
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->interactables); i++)
	{
		if (stashIntFindPointer(id_to_scope, zmap_scope_binned->interactables[i]->common_data.unique_id, &closest_scope))
			zmap_scope_binned->interactables[i]->common_data.closest_scope = closest_scope;
		if (stashIntFindPointer(id_to_obj, zmap_scope_binned->interactables[i]->common_data.parent_node_id, &closest_node))
			zmap_scope_binned->interactables[i]->common_data.parent_node_object = closest_node;
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->spawn_points); i++)
	{
		if (stashIntFindPointer(id_to_scope, zmap_scope_binned->spawn_points[i]->common_data.unique_id, &closest_scope))
			zmap_scope_binned->spawn_points[i]->common_data.closest_scope = closest_scope;
		if (stashIntFindPointer(id_to_obj, zmap_scope_binned->spawn_points[i]->common_data.parent_node_id, &closest_node))
			zmap_scope_binned->spawn_points[i]->common_data.parent_node_object = closest_node;
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->trigger_conditions); i++)
	{
		if (stashIntFindPointer(id_to_scope, zmap_scope_binned->trigger_conditions[i]->common_data.unique_id, &closest_scope))
			zmap_scope_binned->trigger_conditions[i]->common_data.closest_scope = closest_scope;
		if (stashIntFindPointer(id_to_obj, zmap_scope_binned->trigger_conditions[i]->common_data.parent_node_id, &closest_node))
			zmap_scope_binned->trigger_conditions[i]->common_data.parent_node_object = closest_node;
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->layer_fsms); i++)
	{
		if (stashIntFindPointer(id_to_scope, zmap_scope_binned->layer_fsms[i]->common_data.unique_id, &closest_scope))
			zmap_scope_binned->layer_fsms[i]->common_data.closest_scope = closest_scope;
		if (stashIntFindPointer(id_to_obj, zmap_scope_binned->layer_fsms[i]->common_data.parent_node_id, &closest_node))
			zmap_scope_binned->layer_fsms[i]->common_data.parent_node_object = closest_node;
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->patrol_routes); i++)
	{
		if (stashIntFindPointer(id_to_scope, zmap_scope_binned->patrol_routes[i]->common_data.unique_id, &closest_scope))
			zmap_scope_binned->patrol_routes[i]->common_data.closest_scope = closest_scope;
		if (stashIntFindPointer(id_to_obj, zmap_scope_binned->patrol_routes[i]->common_data.parent_node_id, &closest_node))
			zmap_scope_binned->patrol_routes[i]->common_data.parent_node_object = closest_node;
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->named_points); i++)
	{
		if (stashIntFindPointer(id_to_scope, zmap_scope_binned->named_points[i]->common_data.unique_id, &closest_scope))
			zmap_scope_binned->named_points[i]->common_data.closest_scope = closest_scope;
		if (stashIntFindPointer(id_to_obj, zmap_scope_binned->named_points[i]->common_data.parent_node_id, &closest_node))
			zmap_scope_binned->named_points[i]->common_data.parent_node_object = closest_node;
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->named_volumes); i++)
	{
		if (stashIntFindPointer(id_to_scope, zmap_scope_binned->named_volumes[i]->common_data.unique_id, &closest_scope))
			zmap_scope_binned->named_volumes[i]->common_data.closest_scope = closest_scope;
		if (stashIntFindPointer(id_to_obj, zmap_scope_binned->named_volumes[i]->common_data.parent_node_id, &closest_node))
			zmap_scope_binned->named_volumes[i]->common_data.parent_node_object = closest_node;
	}
	for (i = 0; i < eaSize(&zmap_scope_binned->groups); i++)
	{
		if (stashIntFindPointer(id_to_scope, zmap_scope_binned->groups[i]->common_data.unique_id, &closest_scope))
			zmap_scope_binned->groups[i]->common_data.closest_scope = closest_scope;
		// Groups don't have a parent node
	}

	// populate each layer's stash of reserved names
	stashGetIterator(zmap_scope_binned->scope.name_to_obj, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		WorldEncounterObject *obj = stashElementGetPointer(el);
		if (obj->layer && obj->layer->reserved_unique_names)
			stashAddInt(obj->layer->reserved_unique_names, stashElementGetStringKey(el), obj->type, true);
	}

	worldZoneMapScopeCleanupBinData(zmap_scope_binned);
	stashTableDestroy(id_to_obj);
	stashTableDestroy(id_to_scope);
}

/********************
* ENCOUNTER SYSTEM INTEGRATION
********************/
WorldEncounterObject *worldScopeGetObject(const WorldScope *scope, const char *unique_name)
{
	WorldEncounterObject *ret = NULL;

	// Use root scope if no scope provided
	if (!scope) {
		WorldZoneMapScope *zone_scope = zmapGetScope(worldGetPrimaryMap());
		if (zone_scope) {	
			scope = &zone_scope->scope;
		}
	}

	stashFindPointer(scope->name_to_obj, unique_name, &ret);

	return ret;
}

const char *worldScopeGetObjectName(const WorldScope *scope, WorldEncounterObject *object)
{
	char *ret = NULL;

	// Use root scope if no scope provided
	if (!scope) {
		WorldZoneMapScope *zone_scope = zmapGetScope(worldGetPrimaryMap());
		if (zone_scope) {	
			scope = &zone_scope->scope;
		}
	}

	stashFindPointer(scope->obj_to_name, object, &ret);

	return ret;
}

const char *worldScopeGetObjectGroupName(const WorldScope *scope, WorldEncounterObject *object)
{
	char *ret = NULL;

	// Use root scope if no scope provided
	if (!scope) {
		WorldZoneMapScope *zone_scope = zmapGetScope(worldGetPrimaryMap());
		if (zone_scope) {	
			scope = &zone_scope->scope;
		}
	}

	if (object && object->parent_group)
		stashFindPointer(scope->obj_to_name, object->parent_group, &ret);

	return ret;
}

static int worldCompareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}

WorldScopeNamePair **worldEncObjGetScopeNames(WorldEncounterObject *obj)
{
	if (!obj->scope_names)
	{
		WorldScope *scope;

		PERFINFO_AUTO_START_FUNC();
		
		scope = obj->closest_scope;
		eaCreate(&obj->scope_names);

		while (scope)
		{
			WorldScopeNamePair *scope_name = StructCreate(parse_WorldScopeNamePair);
			char *name;
			scope_name->scope = scope;
			if (stashFindPointer(scope->obj_to_name, obj, &name))
				scope_name->name = allocAddString(name);
			eaPush(&obj->scope_names, scope_name);
			scope = scope->parent_scope;
		}

		PERFINFO_AUTO_STOP();
	}
	return obj->scope_names;
}

void worldGetObjectNames(WorldEncounterObjectType type, const char ***peaNames, const WorldScope *scope)
{
	ZoneMap *zmap = worldGetPrimaryMap();
	WorldZoneMapScope *pScope = zmapGetScope(zmap);
	int i;

	if (scope == NULL && pScope)
	{
		scope = &pScope->scope;
	}

	// First get all names loaded for editing
	if (scope) {
		StashTableIterator iter;
		StashElement elem;

		stashGetIterator(scope->name_to_obj, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			WorldEncounterObject *obj = stashElementGetPointer(elem);
			if (obj && obj->type == type) {
				char *pcName = stashElementGetStringKey(elem);
				if (pcName && (strnicmp(pcName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) != 0)) {
					eaPush(peaNames, allocAddString(pcName));
				}
			}
		}
	}

	// Then get all names on layers which are not being edited
	for(i=eaSize(&zmap->layers)-1; i>=0; --i) {
		StashTableIterator iter;
		StashElement elem;

		stashGetIterator(zmap->layers[i]->reserved_unique_names, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			if (stashElementGetInt(elem) == type) {
				char *pcName = stashElementGetStringKey(elem);
				if (pcName && (strnicmp(pcName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) != 0)) {
					eaPush(peaNames, allocAddString(pcName));
				}
			}
		}
	}

	// Sort the names
	eaQSort(*peaNames, worldCompareStrings);
}

#include "wlEncounter_h_ast.c"
