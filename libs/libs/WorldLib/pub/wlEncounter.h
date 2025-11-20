#pragma once

#include "StashTable.h"
#include "textparser.h"
GCC_SYSTEM

typedef struct GroupTracker GroupTracker;
typedef struct GroupDef GroupDef;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct LogicalGroupProperties LogicalGroupProperties;
typedef struct WorldEncounterProperties WorldEncounterProperties;
typedef struct WorldEncounterHackProperties WorldEncounterHackProperties;
typedef struct WorldSpawnProperties WorldSpawnProperties;
typedef struct WorldPatrolProperties WorldPatrolProperties;
typedef struct WorldEncounterLayerProperties WorldEncounterLayerProperties;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct WorldNamedInteractable WorldNamedInteractable;
typedef struct WorldTriggerConditionProperties WorldTriggerConditionProperties;
typedef struct WorldLayerFSMProperties WorldLayerFSMProperties;
typedef struct WorldCivilianPOIProperties WorldCivilianPOIProperties;
typedef struct WorldVolumeEntry WorldVolumeEntry;

typedef struct WorldLogicalGroup WorldLogicalGroup;
typedef struct WorldScope WorldScope;

/********************
* BINNED DATA MODELS
********************/
// Common
AUTO_ENUM;
typedef enum WorldEncounterObjectType
{
	WL_ENC_ENCOUNTER,
	WL_ENC_ENCOUNTER_HACK,
	WL_ENC_INTERACTABLE,
	WL_ENC_SPAWN_POINT,
	WL_ENC_PATROL_ROUTE,
	WL_ENC_NAMED_POINT,
	WL_ENC_NAMED_VOLUME,
	WL_ENC_LOGICAL_GROUP,
	WL_ENC_TRIGGER_CONDITION,
	WL_ENC_LAYER_FSM,
} WorldEncounterObjectType;
extern StaticDefineInt WorldEncounterObjectTypeEnum[];

AUTO_STRUCT;
typedef struct WorldScopeNamePair
{
	WorldScope *scope;		AST(UNOWNED)
	const char *name;		AST(POOL_STRING)
} WorldScopeNamePair;
extern ParseTable parse_WorldScopeNamePair[];
#define TYPE_parse_WorldScopeNamePair WorldScopeNamePair

AUTO_STRUCT;
typedef struct WorldEncounterObject
{
	WorldEncounterObjectType type;					// type of object
	int unique_id;									// DO NOT USE; unique ID populated for binning; also used on load to populate name tables
	int layer_idx;									// DO NOT USE; populated for binning and used to set the layer pointer on load
	int parent_node_id;								// DO NOT USE; populated for binning and used to set the parent node pointer on load
	int parent_node_child_idx;						// Parent child index used with parent_node
	WorldScopeNamePair **scope_names;	AST(NO_WRITE)	// populated when this object is used in an event; caches the various names this object has
														// in all scopes to which it belongs
	AST_STOP

	WorldScope *closest_scope;						// fixed up to point to the innermost WorldScope that contains this object
	ZoneMapLayer *layer;							// fixed up to point to the layer 
	GroupTracker *tracker;							// used during editing to track deleted objects
	WorldLogicalGroup *parent_group;				// set when the object belongs to a logical group
	WorldInteractionEntry *parent_node_entry;		// fixed up to point to the innermost interactable that contains this object
	WorldNamedInteractable *parent_node_object;		// fixed up to point to the innermost interactable that contains this object
} WorldEncounterObject;

// Encounters
AUTO_STRUCT;
typedef struct WorldEncounter
{
	WorldEncounterObject common_data;
	WorldEncounterProperties *properties;

	Vec3 encounter_pos;
	Quat encounter_rot;

	AST_STOP
		// data cached/processed on load
} WorldEncounter;
extern ParseTable parse_WorldEncounter[];
#define TYPE_parse_WorldEncounter WorldEncounter

AUTO_STRUCT;
typedef struct WorldEncounterHack
{
	WorldEncounterObject common_data;
	WorldEncounterHackProperties *properties;

	Vec3 encounter_pos;
	Quat encounter_rot;

	AST_STOP
	// data cached/processed on load
} WorldEncounterHack;

// Interactions (clickables)
AUTO_STRUCT;
typedef struct WorldNamedInteractable
{
	WorldEncounterObject common_data;
	// data cached/processed on load
	int entry_id;							// DO NOT USE; unique ID used to associate interaction entries with the named interaction

	AST_STOP
	// data cached/processed on load
	WorldInteractionEntry *entry;
} WorldNamedInteractable;

// Spawn points
AUTO_STRUCT;
typedef struct WorldSpawnPoint
{
	WorldEncounterObject common_data;
	WorldSpawnProperties *properties;
	
	Vec3 spawn_pos;
	Quat spawn_rot;
	
	AST_STOP
	// data cached/processed on load
} WorldSpawnPoint;

// Patrol routes
AUTO_STRUCT;
typedef struct WorldPatrolRoute
{
	WorldEncounterObject common_data;
	WorldPatrolProperties *properties;

	AST_STOP
	// data cached/processed on load
} WorldPatrolRoute;
extern ParseTable parse_WorldPatrolRoute[];
#define TYPE_parse_WorldPatrolRoute WorldPatrolRoute

// Trigger conditions
AUTO_STRUCT;
typedef struct WorldTriggerCondition
{
	WorldEncounterObject common_data;
	WorldTriggerConditionProperties *properties;

	AST_STOP
	// data cached/processed on load
} WorldTriggerCondition;

// Layer FSMs
AUTO_STRUCT;
typedef struct WorldLayerFSM
{
	WorldEncounterObject common_data;
	WorldLayerFSMProperties *properties;

	AST_STOP
	// data cached/processed on load
} WorldLayerFSM;
extern ParseTable parse_WorldLayerFSM[];
#define TYPE_parse_WorldLayerFSM WorldLayerFSM

// Named points
AUTO_STRUCT;
typedef struct WorldNamedPoint
{
	WorldEncounterObject common_data;

	Vec3 point_pos;
	Quat point_rot;

	AST_STOP
	// data cached/processed on load
} WorldNamedPoint;

// Named volumes
AUTO_STRUCT AST_IGNORE(def_name);
typedef struct WorldNamedVolume
{
	WorldEncounterObject common_data;
	int entry_id;							// DO NOT USE; unique ID used to associate volume entries with the named volume

	AST_STOP
	// data cached/processed on load
	WorldVolumeEntry *entry;
} WorldNamedVolume;


/********************
* LOGICAL GROUPS
********************/
AUTO_STRUCT;
typedef struct WorldLogicalGroup
{
	WorldEncounterObject common_data;
	LogicalGroupProperties *properties;

	// list of encounter object ID's contained in this group, which may include other groups;
	// populated immediately before saving and cleared after load
	int *object_ids;								// DO NOT USE
	
	AST_STOP
	// data cached/processed on load
	WorldEncounterObject **objects;

	// temp data
	U32 selected : 1;
} WorldLogicalGroup;

extern ParseTable parse_WorldLogicalGroup[];
#define TYPE_parse_WorldLogicalGroup WorldLogicalGroup

void worldEncounterObjectAddToLogicalGroup(SA_PARAM_NN_VALID WorldEncounterObject *object, SA_PARAM_OP_VALID WorldLogicalGroup *group);

/********************
* SCOPES
********************/
AUTO_STRUCT;
typedef struct WorldScopeNameId
{
	char *name;										// name of object in the scope
	int unique_id;									// unique ID of object saved on disk
} WorldScopeNameId;

AUTO_STRUCT;
typedef struct WorldLogicalGroupLoc
{
	WorldLogicalGroup *group;			NO_AST		// logical group
	int index;										// index in the group's children
} WorldLogicalGroupLoc;

AUTO_STRUCT;
typedef struct WorldScope
{
	int layer_idx;									// DO NOT USE; stores the layer index in bins

	// table of unique names and the encounter object ID's associated with that name at this scope;
	// populated immediately before saving and cleared after load
	WorldScopeNameId **name_id_pairs;				// DO NOT USE

	// immediate child scopes
	WorldScope **sub_scopes;

	AST_STOP
	// data cached/processed on load
	WorldScope *parent_scope;						// points to parent of this scope; NULL signifies an encounter layer scope
	ZoneMapLayer *layer;							// layer containing the object from which this scope was created
	StashTable name_to_obj;							// hash table mapping object's unique name to object pointer
	StashTable obj_to_name;							// hash table mapping object pointer to object's unique name

	// edit mode data
	WorldLogicalGroup **logical_groups;				// logical groups defined at this scope; only needed when trackers are closed
	StashTable name_to_group;						// hash table mapping unique name in this scope to a parent logical group in this scope (and its index in that group)
	GroupDef *def;									// def that created this scope
	GroupTracker *tracker;							// tracker that created this scope
} WorldScope;
extern ParseTable parse_WorldScope[];
#define TYPE_parse_WorldScope WorldScope

AUTO_STRUCT;
typedef struct WorldZoneMapScope
{
	WorldScope scope;

	// flat lists of relevant layer contents
	WorldEncounterHack **encounter_hacks;
	WorldEncounter **encounters;
	WorldNamedInteractable **interactables;
	WorldSpawnPoint **spawn_points;
	WorldPatrolRoute **patrol_routes;
	WorldNamedPoint **named_points;
	WorldNamedVolume **named_volumes;
	WorldTriggerCondition **trigger_conditions;
	WorldLayerFSM **layer_fsms;

	WorldLogicalGroup **groups;						// ALL groups (tree hierarchy is established after load)

	AST_STOP
	// data cached/processed on load
} WorldZoneMapScope;
extern ParseTable parse_WorldZoneMapScope[];
#define TYPE_parse_WorldZoneMapScope WorldZoneMapScope

/********************
* LOGICAL GROUPING
********************/
void worldLogicalGroupRemoveEncounterObject(SA_PARAM_NN_VALID WorldLogicalGroup *group, SA_PARAM_NN_VALID WorldEncounterObject *object);
void worldLogicalGroupAddEncounterObject(SA_PARAM_NN_VALID WorldLogicalGroup *group, SA_PARAM_NN_VALID WorldEncounterObject *object, int index);

/********************
* WORLD INTEGRATION
********************/
SA_RET_NN_VALID WorldScope *worldScopeCreate(SA_PARAM_NN_VALID GroupDef *def, SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID WorldScope *parent_scope);
void worldScopeDestroy(SA_PARAM_NN_VALID WorldScope *scope);
SA_RET_NN_VALID WorldZoneMapScope *worldZoneMapScopeCreate(void);
void worldZoneMapScopeDestroy(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope);
void worldScopeAddData(SA_PARAM_NN_VALID WorldScope *scope, SA_PARAM_NN_STR const char *unique_name, void *data);

void worldLogicalGroupDestroy(SA_PARAM_NN_VALID WorldLogicalGroup *logical_group);
SA_RET_NN_VALID WorldLogicalGroup *worldZoneMapScopeAddLogicalGroup(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_OP_VALID LogicalGroupProperties *properties);

SA_RET_NN_VALID WorldLogicalGroupLoc *worldLogicalGroupLocCreate(SA_PARAM_NN_VALID WorldLogicalGroup *logical_group, int index);
void worldLogicalGroupLocDestroy(SA_PARAM_NN_VALID WorldLogicalGroupLoc *loc);

SA_RET_NN_VALID WorldEncounterHack *worldZoneMapScopeAddEncounterHack(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_OP_VALID WorldInteractionEntry *parent_entry, int parent_entry_child_idx, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID GroupDef *def, SA_PARAM_OP_VALID GroupTracker *tracker, const Mat4 world_mat);
SA_RET_NN_VALID WorldEncounter *worldZoneMapScopeAddEncounter(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_OP_VALID WorldInteractionEntry *parent_entry, int parent_entry_child_idx, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID GroupDef *def, SA_PARAM_OP_VALID GroupTracker *tracker, const Mat4 world_mat);
SA_RET_NN_VALID WorldNamedInteractable *worldZoneMapScopeAddInteractable(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_OP_VALID WorldInteractionEntry *parent_entry, int parent_entry_child_idx, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_OP_VALID WorldInteractionEntry *entry, const Mat4 world_mat);
SA_RET_NN_VALID WorldSpawnPoint *worldZoneMapScopeAddSpawnPoint(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_OP_VALID WorldInteractionEntry *parent_entry, int parent_entry_child_idx, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID GroupDef *def, SA_PARAM_OP_VALID GroupTracker *tracker, const Mat4 world_mat);
SA_RET_NN_VALID WorldNamedPoint *worldZoneMapScopeAddNamedPoint(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_OP_VALID WorldInteractionEntry *parent_entry, int parent_entry_child_idx, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_OP_VALID GroupTracker *tracker, const Mat4 world_mat);
SA_RET_NN_VALID WorldPatrolRoute *worldZoneMapScopeAddPatrolRoute(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_OP_VALID WorldInteractionEntry *parent_entry, int parent_entry_child_idx, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID GroupDef *def, SA_PARAM_OP_VALID GroupTracker *tracker, const Mat4 world_mat);
SA_RET_NN_VALID WorldNamedVolume *worldZoneMapScopeAddNamedVolume(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_OP_VALID WorldInteractionEntry *parent_entry, int parent_entry_child_idx, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_OP_VALID WorldVolumeEntry *entry, const Mat4 world_mat);
SA_RET_NN_VALID WorldTriggerCondition *worldZoneMapScopeAddTriggerCondition(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_OP_VALID WorldInteractionEntry *parent_entry, int parent_entry_child_idx, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID GroupDef *def, SA_PARAM_OP_VALID GroupTracker *tracker);
SA_RET_NN_VALID WorldLayerFSM *worldZoneMapScopeAddLayerFSM(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_OP_VALID WorldInteractionEntry *parent_entry, int parent_entry_child_idx, SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID GroupDef *def, SA_PARAM_OP_VALID GroupTracker *tracker);

void worldZoneMapScopeUnloadLayer(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, SA_PARAM_NN_VALID ZoneMapLayer *layer);

void worldScopeRemoveTracker(SA_PARAM_NN_VALID WorldScope *closest_scope, SA_PARAM_NN_VALID GroupTracker *tracker);

/********************
* BINNED CONVERSION
********************/
void worldZoneMapScopeCleanupBinData(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope);
void worldZoneMapScopePrepareForBins(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, StashTable volume_entry_to_id, StashTable interactable_entry_to_id);
void worldZoneMapScopeProcessBinData(SA_PARAM_NN_VALID WorldZoneMapScope *zmap_scope, StashTable id_to_named_volume, StashTable id_to_interactable);

/********************
* ENCOUNTER SYSTEM INTEGRATION
********************/
SA_RET_OP_VALID WorldEncounterObject *worldScopeGetObject(SA_PARAM_NN_VALID const WorldScope *scope, SA_PARAM_OP_STR const char *unique_name);
SA_RET_OP_STR const char *worldScopeGetObjectName(SA_PARAM_NN_VALID const WorldScope *scope, SA_PARAM_NN_VALID WorldEncounterObject *object);
SA_RET_OP_STR const char *worldScopeGetObjectGroupName(SA_PARAM_NN_VALID const WorldScope *scope, SA_PARAM_NN_VALID WorldEncounterObject *object);

/********************
* SCOPING UTILITY FUNCTIONS
********************/
SA_RET_NN_VALID WorldScopeNamePair **worldEncObjGetScopeNames(SA_PARAM_NN_VALID WorldEncounterObject *obj);
void worldGetObjectNames(WorldEncounterObjectType type, const char ***peaNames, SA_PARAM_OP_VALID const WorldScope *scope);
