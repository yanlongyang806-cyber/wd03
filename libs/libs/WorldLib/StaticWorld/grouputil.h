#ifndef GROUPUTIL_H
#define GROUPUTIL_H
GCC_SYSTEM

#include "wlCurve.h"
#include "wlGroupPropertyStructs.h"

typedef struct GroupDef GroupDef;
typedef struct GroupTracker GroupTracker;
typedef struct Material Material;
typedef struct WorldDrawableEntry WorldDrawableEntry;
typedef struct WorldRegion WorldRegion;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct GroupSplineParams GroupSplineParams;
typedef struct WorldModelInstanceEntry WorldModelInstanceEntry;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct WorldFXEntry WorldFXEntry;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct WorldAnimationEntry WorldAnimationEntry;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct WorldLODOverride WorldLODOverride;
typedef struct Room Room;
typedef struct RoomPartition RoomPartition;
typedef struct RoomPortal RoomPortal;
typedef struct WorldScope WorldScope;
typedef struct ZoneMap ZoneMap;

typedef struct InstanceEntryInfo
{
	GroupDef *def;
	WorldModelInstanceEntry **entries;
	Mat4 world_matrix;
	Vec3 tint;
} InstanceEntryInfo;

typedef struct GroupInheritedInfo
{
	GroupDef **parent_defs;
	int *idxs_in_parent;

	const char **material_swap_list;
	const char **texture_swap_list;
	MaterialNamedConstant **material_property_list;
} GroupInheritedInfo;

typedef struct GroupInfo
{
	struct GroupInfo *parent_info;

	U32 traverse_id;
	Mat4 world_matrix;
	Vec3 world_min, world_max, world_mid;
	F32 radius;
	F32 uniform_scale;
	F32 current_scale;

	Vec3 fade_mid;
	F32 fade_radius;
	bool has_fade_node;

	F32 lod_scale;

	Vec4 color;	// in RGB
	Vec3 tint_offset; // in HSV
	U32 collisionFilterBits;
	bool editor_only, visible, headshot_visible;
	bool no_vertex_lighting, use_character_lighting, unlit, dummy_group, low_detail, high_detail, high_fill_detail;
	bool map_snap_hidden, double_sided_occluder, is_debris, force_trunk_wind;
	bool no_shadow_cast, no_shadow_receive, no_occlusion, parent_no_occlusion, apply_tint_offset;
	bool in_dynamic_object;
	bool childrenNeedInteractFX;// somewhere in my ancestry (or me) is an interact node without its own FX

	U32 seed;
	bool always_use_seed;
	GroupSplineParams *spline;
    Spline inherited_spline;
	WorldCurveGap **inherited_gaps;
	Mat4 curve_matrix;

	F32 node_height;

	F32 *debris_excluders;

	InstanceEntryInfo *instance_info;

	char *material_replace;

	int parent_entry_child_idx;
	int visible_child;

	U32 layer_id_bits;
	int tag_id;

	WorldRegion *region;
	ZoneMapLayer *layer;
	WorldFXEntry *fx_entry;
	WorldInteractionEntry *parent_entry;
	WorldAnimationEntry *animation_entry;
	WorldVolumeEntry *volume_entry;
	GroupDef *volume_def;

	WorldLODOverride *lod_override;
	bool ignore_lod_override;

	Room *room;
	RoomPartition *room_partition;
	RoomPortal *room_portal;
	bool exclude_from_room;

	WorldScope *closest_scope;

	ZoneMap *zmap;

	GroupChildParameter parameters[GROUP_CHILD_MAX_PARAMETERS];
} GroupInfo;

void applyDefToGroupInfo(GroupInfo *info, GroupInheritedInfo *inherited_info, 
						 GroupDef *current_def, GroupDef *parent_def,
						 int idx_in_parent, bool is_client, bool is_drawable, 
						 bool in_spline, bool update_spline);

// traverse the group tree, calling callback on every node.  if callback returns true it will traverse the node's children.
// return false from the callback to not traverse the group's children.
typedef bool (*GroupTreeTraverserCallback)(void *user_data, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry);
void groupTreeTraverse(ZoneMapLayer *layer, GroupDef *root_def, const Mat4 world_matrix, 
						   GroupTreeTraverserCallback pre_callback, GroupTreeTraverserCallback post_callback, 
						   void *userdata, bool in_editor, bool skip_debris_field_cont);

void groupInheritedInfoDestroy(GroupInheritedInfo *info);

WorldDrawableEntry* worldFindNearestDrawableEntry(const Vec3 pos, int entry_type, const char *modelname, bool near_fade, bool welded);

bool groupIsDynamic(GroupDef *def);
GroupChild **groupGetDynamicChildren(GroupDef *def, GroupTracker *tracker, GroupInfo *info, const Mat4 world_mat);
void groupFreeDynamicChildren(GroupDef *parent, GroupChild **children);
int groupGetRandomChildIdx(GroupChild **children, U32 seed);

bool groupHasLight(GroupDef *def);
bool groupHasMotionProperties(GroupDef* def);

// Override the child select values
void groupClearOverrideParameters( void );
void groupSetOverrideIntParameter( const char* parameterName, int value );
void groupSetOverrideStringParameter( const char* parameterName, const char *value );
int groupInfoGetIntParameter(GroupInfo *info, const char *param_name, int default_value);
const char *groupInfoGetStringParameter(GroupInfo *info, const char *param_name, const char *default_value);

extern int groupInfoOverrideIntParameterValue;

// This does a linear search and is not smart at all, so should only be used for very specific purposes.
GroupChild * groupGetChildByName( GroupDef * pGroup, const char * pchName );

#endif
