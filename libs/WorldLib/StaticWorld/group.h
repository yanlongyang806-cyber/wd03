#ifndef _GROUP_H
#define _GROUP_H
GCC_SYSTEM

#include "wlGroupPropertyStructs.h"
#include "mathutil.h"

typedef struct FSMExternVar FSMExternVar;
typedef struct GroupDef GroupDef;
typedef struct GroupTracker GroupTracker;
typedef struct GroupDefLib GroupDefLib;
typedef struct LightData LightData;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct Model Model;
typedef struct Room Room;
typedef struct RoomPartition RoomPartition;
typedef struct RoomPortal RoomPortal;
typedef struct StashTableImp *StashTable;
typedef struct TrackerHandle TrackerHandle;
typedef struct WorldAnimationEntry WorldAnimationEntry;
typedef struct WorldCellEntry WorldCellEntry;
typedef struct WorldCivilianGenerator WorldCivilianGenerator;
typedef struct WorldEncounterObject WorldEncounterObject;
typedef struct WorldForbiddenPosition WorldForbiddenPosition;
typedef struct WorldFXEntry WorldFXEntry;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldModelInstanceEntry WorldModelInstanceEntry;
typedef struct WorldModelInstanceInfo WorldModelInstanceInfo;
typedef struct WorldPathNode WorldPathNode;
typedef struct WorldScope WorldScope;
typedef struct WorldTagLocation WorldTagLocation;
typedef struct WorldVolumeElement WorldVolumeElement;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct ZoneMapLayer ZoneMapLayer;

/////////////////////////////////////////////////////////
// GroupDef property serialization struct
/////////////////////////////////////////////////////////

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n") AST_STRIP_UNDERSCORES;
typedef struct PropertyLoad
{
	char	*name;			AST( STRUCTPARAM )
	char	*value;			AST( STRUCTPARAM )
} PropertyLoad;
extern ParseTable parse_PropertyLoad[];
#define TYPE_parse_PropertyLoad PropertyLoad

/////////////////////////////////////////////////////////
// Material/Texture Swap run-time structs
/////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct MaterialSwap
{
	const char *orig_name; AST( POOL_STRING )
	const char *replace_name; AST( POOL_STRING )
} MaterialSwap;


AUTO_STRUCT;
typedef struct TextureSwap
{
	const char *orig_name; AST( POOL_STRING )
	const char *replace_name; AST( POOL_STRING )
} TextureSwap;

extern ParseTable parse_MaterialSwap[];
#define TYPE_parse_MaterialSwap MaterialSwap
extern ParseTable parse_TextureSwap[];
#define TYPE_parse_TextureSwap TextureSwap

/////////////////////////////////////////////////////////
// Material/Texture Swap serialization struct
/////////////////////////////////////////////////////////

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n") AST_STRIP_UNDERSCORES;
typedef struct TexSwapLoad
{
	const char	*orig_swap_name;	AST( STRUCTPARAM POOL_STRING )
	const char	*rep_swap_name;		AST( STRUCTPARAM POOL_STRING )
	int		is_material;		AST( STRUCTPARAM )
} TexSwapLoad;
extern ParseTable parse_TexSwapLoad[];
#define TYPE_parse_TexSwapLoad TexSwapLoad

typedef struct GroupSplineParams
{
	Mat4 spline_matrices[2];
} GroupSplineParams;

/////////////////////////////////////////////////////////
// Run-time child data
/////////////////////////////////////////////////////////

typedef struct GroupInstanceBuffer
{
	Model *model;
	WorldModelInstanceInfo **instances;
	WorldModelInstanceEntry *entry; // only used on trackers
} GroupInstanceBuffer;

AUTO_STRUCT;
typedef struct GroupDefPropertyGroup
{
	const char* filename;		AST(CURRENTFILE)
	GroupProperties** props;
} GroupDefPropertyGroup;


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////


// Name->value pair for insertion into StashTable
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct InstanceDataLoad
{
	char *name;				AST( STRUCTPARAM )
	InstanceData data;		AST( EMBEDDED_FLAT )
} InstanceDataLoad;
extern ParseTable parse_InstanceDataLoad[];
#define TYPE_parse_InstanceDataLoad InstanceDataLoad

/////////////////////////////////////////////////////////
// Scope Table name-value pair, from StashTable
/////////////////////////////////////////////////////////

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n") AST_STRIP_UNDERSCORES;
typedef struct ScopeTableLoad
{
	char *path;				AST( STRUCTPARAM )
	char *name;				AST( STRUCTPARAM )
} ScopeTableLoad;
extern ParseTable parse_ScopeTableLoad[];
#define TYPE_parse_ScopeTableLoad ScopeTableLoad

/////////////////////////////////////////////////////////
// The main attraction: GroupDef!
/////////////////////////////////////////////////////////

#define GROUP_DEF_VERSION 1
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End) AST_IGNORE("TintColorHSV1") AST_IGNORE_STRUCT(UGCProperties) AST_STRIP_UNDERSCORES;
typedef struct GroupDef
{
	// StructParser data
	int				name_uid;								AST( STRUCTPARAM KEY )
	const char		*name_str;								AST( NAME(Name) POOL_STRING )
	const char		*filename;								AST( NAME(FN) POOL_STRING CURRENTFILE )
	const char		*scope;									AST( NAME(Scope) POOL_STRING NO_TEXT_SAVE )
	char			*tags;									AST( NAME(Tags) )
	U8				version;								AST( NAME(Version) )

	int				root_id;								AST( NAME(RootID) ) // 0 for layer defs and public object library pieces
	GroupChild		**children;								AST( NAME(Group) )

	char			*replace_material_name;					AST( NAME(ReplaceMaterial) )
	MaterialNamedConstant **material_properties;			AST( NAME(MaterialProperty) )

	Vec3			model_scale;							AST( NAME(ModelScale) )

	Vec4			wind_params_deprecated;					AST( NAME(WindParams) ) // (amount, bendiness, pivot offset, rustling)
	
	// logical groupings for scope defs
	LogicalGroup	**logical_groups;						AST( NAME(LogicalGroup) )

	GroupProperties property_structs;						AST( EMBEDDED_FLAT )

	// StructParser only data - cleared during fixup
	char			*model_name;							AST( NAME(Model) )

	PropertyLoad	**prop_load_deprecated;					AST( NAME(Property) ) // Convert to actual values if still existing

	TexSwapLoad		**tex_swap_load;						AST( NAME(TexSwap) ) // Expanded into arrays "texture_swaps" and "material_swaps"
	ScopeTableLoad	**scope_entries_load;					AST( NAME(ScopeEntry) )
	InstanceDataLoad **instance_data_load;					AST( NAME(InstanceData) ) // Expanded into "name_to_instance_data"

	bool			hasTint0;								AST( NAME(HasTint0) BOOLFLAG )
	Vec3			tintColorHSV0;							AST( NAME(TintColorHSV0) FORMAT_HSV ) // Converted to RGB during fixup
	bool			hasTintOffset0;							AST( NAME(HasTintOffset0) BOOLFLAG )
	Vec3			tintColorOffsetHSV0;					AST( NAME(TintColorOffsetHSV0) )

	U32				bfParamsSpecified[3];					AST( USEDFIELD )

	AST_STOP

	// Non-StructParser data - maintained at runtime only

	Model			*model; // Please use the accessor for this field: groupGetModel

	struct
	{
		Vec3				min;
		Vec3				max;
		// TODO - this field will be much more useful if we make it JUST "sphereCenter" and not also "box center" which is rarely interesting
		// and can be calculated from min and max.  If we do this, we can get tighter spheres
		Vec3				mid;
		F32					radius;
	} bounds;

	// These flags should be combined with the below flags.  Their separate existence is entirely historical
	GroupFlags			group_flags;

	union
	{
		struct 
		{
			U32			is_layer		: 1;

			U32			bounds_valid	: 1;
			U32			bounds_null		: 1;

			U32			deleteMe		: 1;	// flagged for deletion due to an edit operation

			U32			is_dynamic		: 1;
			U32			referenced		: 1;	// This GroupDef is referenced in the layer
			U32			is_new			: 1;	// Newly-created GroupDef is writeable by default

			U32			model_valid		: 1;
			U32			model_null		: 1;
		};

		U32 flags;
	};

	Vec3			tint_color0;					// in RGB (redundant?)
	Vec3			tint_color0_offset;				// in HSV (redundant?)

	GroupDefLib		*def_lib;

	U32				save_mod_time;					// last time this group was changed by the user; needs to be saved
	U32				all_mod_time;					// last time this group or its child matrices were changed (used for network sending)
	U32				group_mod_time;					// last time this group was changed, not including the child array (used for tracker updating)
	U32				group_refresh_time;				// last time this group requested UI & world cell refresh (the actual data was not changed)
	U32				server_mod_time;				// last time this group was sent to the server

	TextureSwap		**texture_swaps;
	MaterialSwap	**material_swaps;

	// SCOPE DEF DATA
	// unique naming for scope defs
	StashTable		path_to_name;
	StashTable		name_to_path;
	int				starting_index;

	// instance overrides
	StashTable		name_to_instance_data;

	// END SCOPE DEF DATA

	U32				access_time;
	S32				dynamic_ref_count;
	bool			was_fixed_up;
	
	GroupSplineParams **spline_params; // an earray of parameter blocks, one per child def
	GroupInstanceBuffer **instance_buffers; // instances specified via code instead of as children (TomY TODO DELETE?)

} GroupDef;
extern ParseTable parse_GroupDef[];
#define TYPE_parse_GroupDef GroupDef

/////////////////////////////////////////////////////////
// A library of GroupDefs
/////////////////////////////////////////////////////////

typedef struct GroupDefLib
{
	StashTable		defs; // uid -> groupdef
	StashTable		def_name_table; // name -> id
	StashTable		temporary_defs; // uid -> temp groupdef

	ZoneMapLayer	*zmap_layer; // NULL if from object library
	bool			dummy;
	bool			editing_lib;
	bool			was_fixed_up;
} GroupDefLib;

/////////////////////////////////////////////////////////
// GroupTracker
/////////////////////////////////////////////////////////

typedef struct GroupTracker
{
	GroupDef		*def;
	GroupTracker	**children;			// list of children
	int				child_count;		// number of children

	union
	{
		struct
		{
			U32			tracker_opened			: 1;// used by trackerOpen
			U32			open					: 1;// used by editor
			U32			edit					: 1;// used by game editor to control if this tracker's children are selectable
			U32			frozen					: 1;// used by editor, the tracker cannot be changed if this flag is set
			U32         locked          		: 1;// used by editor, the tracker cannot be deselected if this flag is set
			U32			deleteMe				: 1;// flagged for deletion due to an edit operation
			U32			deleteMyChild			: 1;// some descendant has deleteMe set
			U32			debugMe					: 1;// flagged for debugging
			U32			subObjectEditing		: 1;// used by editor, editing sub objects (Curve CP's)
			U32			dummyTracker			: 1;// don't hold copies of this tracker; it is a dummy

			// drawing bits
			U32			invisible				: 1;// use accessor to set this bit
			U32			selected				: 1;// use accessor to set this bit
			U32			unlit					: 1;
			U32			wireframe				: 2;
		};

		U32 flags;
	};

	U32						all_mod_time;
	U32						group_mod_time;		// synchronizes last time this tracker was updated to its groupdef
	U32						group_refresh_time; // synchronizes last time this tracker was updated to its groupdef
	U32						parent_child_mod_time;
	U32						uid_in_parent;

	F32						scale;

	ZoneMapLayer			*parent_layer;
	GroupTracker			*parent;
	int						idx_in_parent;

	WorldCellEntry			**cell_entries;
	WorldInteractionEntry	*cell_interaction_entry;
	WorldAnimationEntry		*cell_animation_entry;
	WorldVolumeEntry		*cell_volume_entry;
	WorldVolumeElement		*cell_volume_element;
	WorldTagLocation		*tag_location;

	Room					*room;				// room to which this tracker belongs
	RoomPartition			*room_partition;	// room partition to which this tracker's model belongs
	RoomPortal				*room_portal;		// room portal for this tracker

	WorldScope				*closest_scope;
	WorldEncounterObject	*enc_obj;

	WorldCivilianGenerator	*world_civilian_generator; // For open/close modifications
	WorldForbiddenPosition  *world_forbidden_position; // Same
	WorldPathNode			*world_path_node;

	GroupTracker			*instance_parent;
	GroupInstanceBuffer		**instance_buffers;	// keeps track of child instances
	WorldModelInstanceInfo	*instance_info;

	Spline					inherited_spline;
	GroupSplineParams		*spline_params; // override the particular spline params for this tracker

	F32						*debris_excluders;
	GroupTracker			*debris_cont_tracker;

	U8						skip_entry_create : 1;

} GroupTracker;

AUTO_STRUCT;
typedef struct TrackerHandle
{
	char *zmap_name;
	char *layer_name;
	U32 *uids;
} TrackerHandle;


extern ParseTable parse_MaterialSwap[];
#define TYPE_parse_MaterialSwap MaterialSwap
extern ParseTable parse_TextureSwap[];
#define TYPE_parse_TextureSwap TextureSwap
extern ParseTable parse_GroupDefPropertyGroup[];
#define TYPE_parse_GroupDefPropertyGroup GroupDefPropertyGroup
extern ParseTable parse_TrackerHandle[];
#define TYPE_parse_TrackerHandle TrackerHandle

#define GROUPDEF_STRING_LENGTH 128 //TO DO: check groupDef strings

/////////////////////////////////////////////////////////
// GroupDef public accessor functions
/////////////////////////////////////////////////////////

Model *groupGetModel(GroupDef *def); // accessor for GroupDef model. Will make sure it is loaded if not already and has a model.

/////////////////////////////////////////////////////////
// GroupDef-related functions
/////////////////////////////////////////////////////////

void groupGetBoundsMinMax(GroupDef *def,const Mat4 mat,F32 scale,Vec3 min,Vec3 max);
Model *groupModelFind(SA_PARAM_OP_STR const char *name, WLUsageFlags extra_use_flags);
U32 defContainCount(GroupDef *container,GroupDef *def);
void groupSetBounds(GroupDef *group, bool force);
void groupGetBounds(GroupDef *def, GroupSplineParams *spline, const Mat4 world_mat_in, F32 scale, Vec3 world_min, Vec3 world_max, Vec3 world_mid, F32 *radius, Mat4 world_mat_out);
bool groupGetVisibleBounds(GroupDef *group, const Mat4 world_mat_in, F32 scale, Vec3 vMin, Vec3 vMax);
void groupGetDrawVisDistRecursive(GroupDef *def, const WorldLODOverride *lod_override, F32 lod_scale, F32 *near_lod_near_dist, F32 *far_lod_near_dist, F32 *far_lod_far_dist);
bool groupIsPrivate(GroupDef *def);
bool groupIsPublic(GroupDef *def);
bool groupHasScope(GroupDef *def);
bool groupIsObjLib(GroupDef *def);
bool groupIsObjLibUID(int uid);
bool groupIsInCoreDir(GroupDef *def);
bool groupIsCore(GroupDef *def);
bool groupIsCoreUID(int uid);
bool groupIsEditable(GroupDef *def);
void splineParamsGetBounds(GroupSplineParams *params, GroupDef *def, Vec3 min, Vec3 max);

TextureSwap *createTextureSwap(const char *filename, const char *orig_name, const char *replace_name);
void freeTextureSwap(TextureSwap *dts);
TextureSwap *dupTextureSwap(const char *filename, TextureSwap *dts);

MaterialSwap *createMaterialSwap(const char *orig_name, const char *replace_name);
void freeMaterialSwap(MaterialSwap *dts);
MaterialSwap *dupMaterialSwap(MaterialSwap *dts);

LightData *groupGetLightData(GroupDef **def_chain, const Mat4 world_matrix);
bool lightPropertyIsSet(WorldLightProperties *pProps, const char *property_name);

void groupSetTintColor(GroupDef *def, const Vec3 tint_color);
void groupRemoveTintColor(GroupDef *def);
void groupSetTintOffset(GroupDef *def, const Vec3 tint_offset);
void groupRemoveTintOffset(GroupDef *def);

bool groupGetMaterialPropertyF32(GroupDef *def, const char *property_name, F32 *value);
bool groupGetMaterialPropertyVec2(GroupDef *def, const char *property_name, Vec2 value);
bool groupGetMaterialPropertyVec3(GroupDef *def, const char *property_name, Vec3 value);
bool groupGetMaterialPropertyVec4(GroupDef *def, const char *property_name, Vec4 value);

void groupSetMaterialPropertyF32(GroupDef *def, const char *property_name, F32 value);
void groupSetMaterialPropertyVec2(GroupDef *def, const char *property_name, const Vec2 value);
void groupSetMaterialPropertyVec3(GroupDef *def, const char *property_name, const Vec3 value);
void groupSetMaterialPropertyVec4(GroupDef *def, const char *property_name, const Vec4 value);

void groupRemoveMaterialProperty(GroupDef *def, const char *property_name);

bool groupIsVolumeType(GroupDef *def, const char *volume_type);

#define GROUP_UNNAMED_PREFIX "UNNAMED_"
typedef enum GroupNameReturnVal
{
	GROUP_NAME_CREATED,
	GROUP_NAME_EXISTS,
	GROUP_NAME_DUPLICATE,
} GroupNameReturnVal;

void groupGetInheritedName(char *newName, int name_size, GroupDef *parent, GroupDef *child);
bool groupShouldDoTransfer(GroupDef *parent, GroupDef *child);

bool groupDefScopeSetPathName(SA_PARAM_NN_VALID GroupDef *scope_def, SA_PARAM_NN_STR const char *path, SA_PARAM_NN_STR const char *unique_name, bool overwrite);
bool groupDefScopeIsNameUnique(SA_PARAM_OP_VALID GroupDef *scope_def, SA_PARAM_OP_STR const char *name);
GroupNameReturnVal groupDefScopeCreateUniqueName(SA_PARAM_NN_VALID GroupDef *scope_def, SA_PARAM_OP_STR const char *path, SA_PARAM_NN_STR const char *base_name, char *output, size_t output_size, bool error_check);
void groupDefLayerScopeAddInstanceData(SA_PARAM_NN_VALID GroupDef *layer_def, SA_PARAM_NN_STR const char *unique_name, SA_PARAM_OP_VALID InstanceData *data);
SA_RET_OP_VALID InstanceData *groupDefLayerScopeGetInstanceData(SA_PARAM_NN_VALID GroupDef *layer_def, SA_PARAM_NN_STR const char *unique_name);
bool groupDefScopeIsNameUsed(SA_PARAM_NN_VALID GroupDef *scope_def, SA_PARAM_NN_STR const char *name);
bool groupDefNeedsUniqueName(SA_PARAM_OP_VALID GroupDef *def);
void groupDefScopeClearInvalidEntries(SA_PARAM_NN_VALID GroupDef *scope_def, bool increment_mod_time);
int* groupDefScopeGetIndexesFromPath(GroupDef* scope_def, const char* path);
int* groupDefScopeGetIndexesFromName(GroupDef* scope_def, const char* logical_name);
bool groupDefFindScopeNameByFullPath(GroupDef *scope_def, int *path, int path_size, const char **scope_name);

/// Message helpers
void groupDefFixupMessages(SA_PARAM_NN_VALID GroupDef *def);
void groupDefFixupMessageKey(SA_PARAM_NN_VALID const char** outMessageKey, SA_PARAM_NN_VALID GroupDef *def, SA_PARAM_NN_STR const char* scope, SA_PARAM_OP_VALID const int* num);
const char* groupDefMessageKeyRaw(SA_PARAM_NN_STR const char *layer_fname, SA_PARAM_NN_STR const char *group_name, SA_PARAM_NN_STR const char* scope, SA_PARAM_OP_VALID const int* num, bool preferTempbinName);

void atmosphereBuildDynamicConstantSwaps_dbg(F32 atmosphere_size, F32 atmosphere_radius, MaterialNamedConstant ***material_property_list, const char *caller_fname, int line);
#define atmosphereBuildDynamicConstantSwaps(atmosphere_size, atmosphere_radius, material_property_list) atmosphereBuildDynamicConstantSwaps_dbg(atmosphere_size, atmosphere_radius, material_property_list MEM_DBG_PARMS_INIT)

// GroupLib functions

void groupLibInit(GroupDefLib *lib);
GroupDefLib *groupLibCreate(bool dummy);
void groupLibFree(GroupDefLib *def_lib);
void groupLibClear(GroupDefLib *def_lib);
GroupDef *groupLibFindGroupDef(GroupDefLib *lib, int name_uid, bool allow_temporary_defs);
GroupDef *groupLibFindGroupDefByName(GroupDefLib *def_lib, const char *defname, bool allow_temporary_defs);
bool groupLibAddGroupDef(GroupDefLib *def_lib, GroupDef *def, GroupDef **dup);
void groupLibRemoveGroupDef(GroupDefLib *def_lib, GroupDef *def);
void groupLibAddTemporaryDef(GroupDefLib *def_lib, GroupDef *def, int uid);
void groupLibRemoveTemporaryDef(GroupDefLib *def_lib, int uid);
bool groupLibIsValidGroupName(GroupDefLib *def_lib, const char *name, int parent_id);
void groupLibMakeGroupName(GroupDefLib *def_lib, const char *oldname, char *newname, int newname_len, int parent_id);
GroupDef *groupLibNewGroupDef(GroupDefLib *def_lib, const char *filename, int defname_uid, const char *defname, int parent_id, bool update_mod_time, bool is_new);
GroupDef *groupLibCopyGroupDef(GroupDefLib *destlib, const char *filename, GroupDef *srcgroup, const char *srcname, bool increment_mod_time, bool keep_old_message_keys, bool copy_identical, int parent_id, bool force_new);
GroupDef **groupLibGetDefEArray(GroupDefLib *def_lib); // returns a static earray, DO NOT HOLD ON TO THESE POINTERS!
void groupLibMarkBadChildren(GroupDefLib *def_lib);
bool groupLibConsistencyCheckAll(GroupDefLib *def_lib, bool do_assert);

__forceinline static F32 groupQuantizeScaleFloat(F32 value)
{
	// this used to quantize scale to 0.2 increments (or 0.1 if less than 1).
	// CarlosM thinks he's heard some performance reason for this, but StevenD can't think of
	// a reason for such low resolution. Environment artists want higher res,
	// so I'm increasing resolution to 0.01 increments. 
	// SIP TODO: move all these functions into group.c 
	value = round(value * 100.0f) * 0.01f;
	if (value < 0.0f)
		value = MIN(value, -0.01f);
	else
		value = MAX(value, 0.01f);
	return value;
}

__forceinline static void groupQuantizeScale(const Vec3 src, Vec3 dst)
{
	dst[0] = groupQuantizeScaleFloat(src[0]);
	dst[1] = groupQuantizeScaleFloat(src[1]);
	dst[2] = groupQuantizeScaleFloat(src[2]);
}


__forceinline static GroupChild **groupGetChildren(GroupDef *def)
{
	return def->children;
}

SA_RET_OP_VALID GroupTracker *trackerFromTrackerHandle(SA_PARAM_OP_VALID const TrackerHandle *handle);
SA_RET_OP_VALID __forceinline static WorldCurve *curveFromTrackerHandle(SA_PARAM_OP_VALID TrackerHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	if (tracker && tracker->def && tracker->def->property_structs.curve)
		return tracker->def->property_structs.curve;
	return NULL;
}

GroupDef *groupChildGetDefEx(GroupDef *parent, int def_name_uid, const char *def_name, bool silent, bool skip_parent_check);
__inline static GroupDef *groupChildGetDef(GroupDef *parent, GroupChild *child, bool silent) { return groupChildGetDefEx(parent, child->name_uid, child->name, silent, false); }

void groupTrackerBuildPathNodeTrackerTable(GroupTracker *tracker, StashTable stTrackersByDefID);

#endif
