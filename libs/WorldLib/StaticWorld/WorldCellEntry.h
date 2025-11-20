/***************************************************************************



***************************************************************************/

#ifndef __WORLDCELLENTRY_H__
#define __WORLDCELLENTRY_H__
GCC_SYSTEM

#include "referencesystem.h"
#include "wlInteraction.h"
#include "group.h"
#include "grouputil.h"
#include "wlModel.h"
#include "WorldColl.h"

typedef struct WorldCell WorldCell;
typedef struct WorldCellWeldedBin WorldCellWeldedBin;
typedef struct WorldRegion WorldRegion;
typedef struct WorldFXCondition WorldFXCondition;
typedef struct WorldVolume WorldVolume;
typedef struct WorldVolumeElement WorldVolumeElement;
typedef struct WorldCollObject WorldCollObject;
typedef struct WorldClusterState WorldClusterState;
typedef struct ScaledCollision ScaledCollision;
typedef struct PSDKCookedMesh PSDKCookedMesh;
typedef struct SoundSpace SoundSpace;
typedef struct SoundSpaceConnector SoundSpaceConnector;
typedef struct GfxLight GfxLight;
typedef struct Material Material;
typedef struct MaterialRenderInfo MaterialRenderInfo;
typedef struct BasicTexture BasicTexture;
typedef struct GfxStaticObjLightCache GfxStaticObjLightCache;
typedef struct Model Model;
typedef struct GroupSplineParams GroupSplineParams;
typedef struct GroupDef GroupDef;
typedef struct GroupTracker GroupTracker;
typedef struct WindInternal WindInternal;
typedef struct LightData LightData;
typedef struct WLCostume WLCostume;
typedef struct WorldInteractionCostume WorldInteractionCostume;
typedef struct MaterialConstantMapping MaterialConstantMapping;
typedef struct RdrDrawableGeo RdrDrawableGeo;
typedef struct RdrSubobject RdrSubobject;
typedef struct RdrInstancePerDrawableData RdrInstancePerDrawableData;
typedef struct RdrAmbientLight RdrAmbientLight;
typedef struct InstanceEntryInfo InstanceEntryInfo;
typedef struct GroupInfo GroupInfo;
typedef struct MaterialNamedTexture MaterialNamedTexture;
typedef U32 dtNode;
typedef U32 dtFxManager;
typedef U32 dtFx;

typedef struct WorldFXEntry WorldFXEntry;
typedef struct WorldDrawableList WorldDrawableList;
typedef struct NOCONST(WorldDrawableListPool) NOCONST(WorldDrawableListPool);
typedef struct WorldInstanceParamList WorldInstanceParamList;


#define GROUP_ID_MASK 0x03ffffff
#define GROUP_ID_LAYER_MASK 0xff000000
#define GROUP_ID_LAYER_OFFSET 24

// changing either of these defines requires bumping the world cell version number
#define MAX_LIGHT_DISTANCE 800
#define LIGHT_RADIUS_VIS_MULTIPLIER 30

AUTO_ENUM;
typedef enum WorldCellEntryType
{
	WCENT_INVALID = 0,

	// independently managed & partitioned, server and client
	WCENT_VOLUME,
	WCENT_COLLISION,
	WCENT_ALTPIVOT,
	WCENT_INTERACTION,


	WCENT_BEGIN_CLIENT_ONLY,


	// independently managed & partitioned, client only
	WCENT_SOUND,
	WCENT_LIGHT,
	WCENT_FX,
	WCENT_ANIMATION,
	WCENT_WIND_SOURCE,


	WCENT_BEGIN_DRAWABLES,


	// drawable, client only
	WCENT_MODEL,
	WCENT_MODELINSTANCED,
	WCENT_SPLINE,
	WCENT_OCCLUSION,

	WCENT_MODELCLUSTER,

	WCENT_COUNT,

} WorldCellEntryType;

AUTO_STRUCT;
typedef struct WorldCellEntryBounds
{
	Vec3					world_mid;
	Mat4					world_matrix;
	int						object_tag;
} WorldCellEntryBounds;

AUTO_STRUCT;
typedef struct WorldCellEntrySharedBounds
{
	// all values are rounded up to next biggest integer, unless use_model_bounds is set
	Vec3					local_min;				AST( FLOAT_ONES )
	Vec3					local_max;				AST( FLOAT_ONES )
	F32						radius;					AST( FLOAT_ONES )
	F32						near_lod_near_dist;		AST( FLOAT_ONES )
	F32						far_lod_near_dist;		AST( FLOAT_ONES )
	F32						far_lod_far_dist;		AST( FLOAT_ONES )

	int						model_idx;				AST( DEFAULT(-1) )
	Vec3					model_scale;			AST( FLOAT_TENTHS )
	U8						use_model_bounds : 1;

	const Model				*model;					NO_AST
	ZoneMap					*zmap;					NO_AST
	U32						uid;					NO_AST
	int						ref_count;				NO_AST
} WorldCellEntrySharedBounds;


#define ENTRY_CULL_IS_ACTIVE_MASK (1<<0)
#define ENTRY_CULL_LAST_VISIBLE_MASK (1<<1)
#define ENTRY_CULL_POINTER_MASK (~(ENTRY_CULL_IS_ACTIVE_MASK | ENTRY_CULL_LAST_VISIBLE_MASK))

// struct size: 32 bytes on 32 bit architecture (try to keep it within 32 bytes)
typedef struct WorldCellEntryCullHeader
{
	WorldCell				*leaf_cell;
	U16						reserved;
	S16						world_fade_mid[3];
	U16						scaled_near_vis_dist;
	U16						scaled_far_vis_dist;
	S16						world_mid[3];
	U16						world_corner[3];
	intptr_t				entry_and_flags; // pointer with is_active and last_visible flags ORed into it
} WorldCellEntryCullHeader;

__forceinline static void worldCellEntryHeaderSetActive(WorldCellEntryCullHeader *header, bool is_active)
{
	if (is_active)
		header->entry_and_flags |= ENTRY_CULL_IS_ACTIVE_MASK;
	else
		header->entry_and_flags &= ~ENTRY_CULL_IS_ACTIVE_MASK;
}

#define ENTRY_HEADER_IS_ACTIVE(header) ((header)->entry_and_flags & ENTRY_CULL_IS_ACTIVE_MASK)
#define ENTRY_HEADER_LAST_VISIBLE(header) ((header)->entry_and_flags & ENTRY_CULL_LAST_VISIBLE_MASK)
#define ENTRY_HEADER_ENTRY_PTR(header) ((WorldDrawableEntry *)((header)->entry_and_flags & ENTRY_CULL_POINTER_MASK))

typedef struct WorldCellEntry
{
	U32							type : 16;
	U32							owned : 1;
	U32							streaming_mode : 1; // NOTE: can still be from parsed data and not have this flag set
	U32							cluster_hidden : 1;
	U32							baIsOpen;
	U32							baIsDisabled;
	WorldCellEntryBounds		bounds;
	WorldCellEntrySharedBounds	*shared_bounds;
} WorldCellEntry;

typedef struct WorldCellEntryData
{
	WorldInteractionEntry	*parent_entry;
	int						interaction_child_idx;
	WorldCell				*cell;
	WorldCellWeldedBin		**bins;					// edit mode only
	U32						group_id;				// unique id for the group this came from 
	 												// Except in the case of terrain objects, this is
													// incorrect and instead the layer bits are stored here in the upper 25 bits.
													// The group_id NEEDS 32 bits.
													// If we ever need the group id in the data, 
													// we need to add a different field for layer bits
} WorldCellEntryData;


//////////////////////////////////////////////////////////////////////////
// server and client

typedef struct WorldInteractionEntry
{
	WorldCellEntry				base_entry; // must be first

	int							uid; // unique interaction entry identifier (must be the same on server and client)

	U8							hasInteractionNode:1;

	WorldBaseInteractionProperties	*base_interaction_properties; // On client and server
	WorldInteractionProperties	*full_interaction_properties; // Only available on the server

	int							initial_selected_child;
	int							visible_child_count;

	WorldInteractionCostume		*costume;	// only valid when not in streaming mode

	WorldCellEntry				**child_entries;
	WorldVolumeEntry			*attached_volume_entry;

	REF_TO(WorldInteractionNode) hInteractionNode;

	GroupDef					*parent_def; // only valid in edit mode, used for validation

	WorldCellEntryData			base_entry_data; // should be last for cache reasons
} WorldInteractionEntry;

typedef struct WorldVolumeEntry
{
	WorldCellEntry						base_entry; // must be first

	WorldVolumeElement					**elements;

	U32									volume_type_bits;
	U8									indoor:1;
	U8									occluder:1;
	Room								*room;

	GroupVolumePropertiesServer			server_volume;
	GroupVolumePropertiesClient			client_volume;

	// For client volumes with a trigger condition.
	WorldFXCondition *fx_condition;
	// For client volumes with a trigger condition. When there is no trigger condition, state is assumed to always be 1
	int fx_condition_state;

	WorldVolume 						**eaVolumes;
	SoundSpace							*sound_space;
	SoundSpaceConnector					*sound_connector;
	PSDKCookedMesh						*mesh;
	WorldCollObject						**eaCollObjects;
	RdrAmbientLight						*indoor_ambient_light;

	WorldCellEntryData					base_entry_data; // should be last for cache reasons
} WorldVolumeEntry;

typedef struct WorldCollisionEntry
{
	WorldCellEntry			base_entry; // must be first

	GroupSplineParams		*spline;
	Model					*model;
	const char				**eaMaterialSwaps; // used for swapping physical properties on the model [STO-34751]
	Vec3					scale;
	WorldCollFilter			filter;
	F32						collision_radius; // set to 0 if this is not a collision sphere
	Vec3					collision_min, collision_max; // in local space, for box collision

	TrackerHandle			*tracker_handle; // edit mode only

	WorldCollObject 		**eaCollObjects;
	ScaledCollision			*model_col; // needs to be reference counted
	PSDKCookedMesh			*cooked_mesh; // needs to be freed

	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldCollisionEntry;

typedef struct WorldAltPivotEntry
{
	WorldCellEntry			base_entry; // must be first

	U32						hand_pivot:1;
	U32						mass_pivot:1;

	const char				*carry_anim_bit;

	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldAltPivotEntry;



//////////////////////////////////////////////////////////////////////////
// client only

typedef struct WorldAnimationEntry
{
	WorldCellEntry			base_entry; // must be first

	U32						id;

	// cached animated matrix
	U32						last_update_timestamp;
	Mat4					full_matrix;

	// animation properties and matrix
	WorldAnimationProperties animation_properties;

	// parent animation controller
	REF_TO(WorldAnimationEntry) parent_animation_controller_handle;
	Mat4					parent_controller_relative_matrix;	// parent animation may happen in a different basis, so we need a relative matrix for this animation

	int						ref_count;

	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldAnimationEntry;

typedef struct WorldSoundEntry
{
	WorldCellEntry			base_entry; // must be first

	const char				*event_name;
	char					*excluder_str;
	const char				*dsp_str;
	char					*editor_group_str;
	char					*sound_group_str;
	const char				*sound_group_ord;
	SoundSpace				*sound;

	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldSoundEntry;

typedef struct WorldLightEntry
{
	WorldCellEntry			base_entry; // must be first

	LightData				*light_data;
	REF_TO(WorldAnimationEntry) animation_controller_handle;
	GfxLight				*light;

	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldLightEntry;

typedef struct WorldWindSourceEntry
{
	WorldCellEntry			base_entry; // must be first

	WorldWindSourceProperties source_data;

	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldWindSourceEntry;

typedef struct WorldFXEntry
{
	WorldCellEntry			base_entry; // must be first

	U32						id;

	Vec3					ambient_offset;

	WorldFXCondition		*fx_condition;
	char					*fx_name;
	char					*fx_params;
	bool					fx_is_continuous;
	F32						fx_hue;
	F32						fx_alpha;
	char					*faction_name;

	Material				*material;
	U8						add_material:1;
	U8						dissolve_material:1;
	U8						has_target_node:1;
	U8						target_no_anim:1;

	U8						low_detail;
	U8						high_detail;
	U8						high_fill_detail;

	dtNode					node_guid;
	dtFxManager				fx_manager;
	dtFx					debris_fx_guid;

	dtNode					target_node_guid;
	Mat4					target_node_mat;

	char					*debris_model_name;
	WorldDrawableList		*debris_draw_list; // May be shared between multiple entries
	WorldInstanceParamList	*debris_instance_param_list;
	Vec3					debris_scale;
	Vec4					debris_tint_color;

	char					*interaction_fx_name;

	U8						interaction_node_owned;
	U8						debris_needs_reset;
	U8						started;

	U8						controller_relative_matrix_inited;
	REF_TO(WorldAnimationEntry) animation_controller_handle;
	Mat4					controller_relative_matrix;	// animation may happen in a different basis, so we need a relative matrix for this drawable

	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldFXEntry;


//////////////////////////////////////////////////////////////////////////
// drawables, client only

// This is not allocated with StructCreate. It uses AUTO_STRUCT purely for AST_FORCE_CONST.
// Do not modify the contents of this struct at run-time unless you know what you're doing!
AUTO_STRUCT AST_FORCE_CONST;
typedef struct MaterialDraw
{
	// This struct allows us to pre-apply material and texture swaps
	// This is not marked as const so that it can be modified in GfxMaterialsOpt.
	MaterialRenderInfo				*temp_render_info; NO_AST

	const Material					*material; NO_AST
	CONST_EARRAY_OF(BasicTexture)	textures; NO_AST
	const Vec4						*constants; NO_AST
	const MaterialConstantMapping	*constant_mappings; NO_AST
	const U16						tex_count;
	const U16						const_count;
	const U16						const_mapping_count;
	const U8						has_swaps;
	const U8						is_occluder;

	const int						ref_count;
	const U32						uid;
	const U32						hash;
	const U16						hash_debug[4];
		// TomY this is used for detecting where changes that break the hash are happening
} MaterialDraw;

typedef struct WorldDrawDebugLogEntry
{
	const char* caller_fname;
	int caller_line;
	int ref_count;
	U32 id;
	U32 removed : 1;
} WorldDrawDebugLogEntry;

// This struct contains circular lists of debug information about each world draw type
typedef struct WorldDrawDebugLogs
{
	WorldDrawDebugLogEntry** eaMaterialDraws;
	WorldDrawDebugLogEntry** eaModelDraws;
	WorldDrawDebugLogEntry** eaSubObjects;
	WorldDrawDebugLogEntry** eaDrawLists;
	WorldDrawDebugLogEntry** eaInstanceParamLists;

	int iCurMatDrawIndex;
	int iCurModelDrawIndex;
	int iCurSubObjIndex;
	int iCurDrawListIndex;
	int iCurInstParamListIndex;
} WorldDrawDebugLogs;

// This is not allocated with StructCreate. It uses AUTO_STRUCT purely for AST_FORCE_CONST.
// Do not modify the contents of this struct at run-time unless you know what you're doing!
AUTO_STRUCT AST_FORCE_CONST;
typedef struct ModelDraw
{
	RdrDrawableGeo	*temp_geo_draw; NO_AST

	const Model		*model; NO_AST
	const U32		lod_idx;

	const int		ref_count;
	const U32		uid;
} ModelDraw;

// This is not allocated with StructCreate. It uses AUTO_STRUCT purely for AST_FORCE_CONST.
// Do not modify the contents of this struct at run-time unless you know what you're doing!
AUTO_STRUCT AST_FORCE_CONST;
typedef struct WorldDrawableSubobject
{
	RdrSubobject						*temp_rdr_subobject;  NO_AST
		// stores the subobject for this frame for auto-instancing
	MaterialRenderInfo					*material_render_info; NO_AST
		// stores the render_info for this frame for updating draw distances
	int									fallback_idx; NO_AST
		// stores the fallback index into the material_draws[] being used this frame

	CONST_EARRAY_OF(MaterialDraw)		material_draws;
		// EArray
	CONST_OPTIONAL_STRUCT(ModelDraw)	model; AST(NAME(Model))
	const U32							subobject_idx;

	const int							ref_count;
	const U32							uid;
} WorldDrawableSubobject;

// This is not allocated with StructCreate. It uses AUTO_STRUCT purely for AST_FORCE_CONST.
// Do not modify the contents of this struct at run-time unless you know what you're doing!
AUTO_STRUCT AST_FORCE_CONST;
typedef struct WorldDrawableLod
{
	CONST_EARRAY_OF(WorldDrawableSubobject)	subobjects;
	const U16								subobject_count;
	const U8								no_fade;
	const F32								near_dist;
	const F32								far_dist;
	const F32								far_morph;
	const U32								occlusion_materials;

	const int								ref_count;
	const U32								uid;
} WorldDrawableLod;

// This is not allocated with StructCreate. It uses AUTO_STRUCT purely for AST_FORCE_CONST.
// Do not modify the contents of this struct at run-time unless you know what you're doing!
AUTO_STRUCT AST_FORCE_CONST;
typedef struct WorldDrawableListPool
{
	const StashTable						material_draw_hash_table; NO_AST
	const StashTable						model_draw_hash_table; NO_AST
	const StashTable						subobject_hash_table; NO_AST
	const StashTable						draw_list_hash_table; NO_AST
	const StashTable						instance_data_hash_table; NO_AST
	CONST_EARRAY_OF(ModelDraw)				model_draw_list;
	CONST_EARRAY_OF(WorldDrawableSubobject)	subobject_list;
	CONST_EARRAY_OF(MaterialDraw)			material_draw_list;
	const int								total_material_draw_count;
	const int								total_model_draw_count;
	const int								total_subobject_count;
	const int								total_draw_list_count;
	const int								total_instance_data_count;
	const U32								material_draw_uid_counter;
	const U32								model_draw_uid_counter;
	const U32								subobject_uid_counter;
	const U32								draw_list_uid_counter;
	const U32								instance_data_uid_counter;
} WorldDrawableListPool;

// This is not allocated with StructCreate. It uses AUTO_STRUCT purely for AST_FORCE_CONST.
// Do not modify the contents of this struct at run-time unless you know what you're doing!
AUTO_STRUCT AST_FORCE_CONST;
typedef struct WorldDrawableList
{
	CONST_OPTIONAL_STRUCT(WorldDrawableLod)			drawable_lods;
	const U8										lod_count;
	const U8										no_fog;
	const U8										high_detail_high_lod;
	const int										ref_count;
	CONST_OPTIONAL_STRUCT(WorldDrawableListPool)	pool; AST(NAME(Pool))
	const U32										uid;
} WorldDrawableList;

typedef enum {
	NON_CLUSTERABLE,	// This would be things like terrain and dynamic objects.
	CLUSTERED,
	NON_CLUSTERED,
} cluster_states;


#ifndef DECLARED_RdrInstancePerDrawableData
#define DECLARED_RdrInstancePerDrawableData
typedef struct RdrInstancePerDrawableData
{
	Vec4						instance_param; 
} RdrInstancePerDrawableData;
#endif

typedef struct WorldInstanceParamPerSubObj
{
	int							fallback_count;
	RdrInstancePerDrawableData	*fallback_params; // array length fallback_count
} WorldInstanceParamPerSubObj;

typedef struct WorldInstanceParamPerLod
{
	int							subobject_count;
	WorldInstanceParamPerSubObj *subobject_params; // array length of subobject_count
} WorldInstanceParamPerLod;

typedef struct WorldInstanceParamList
{
	int							lod_count;
	WorldInstanceParamPerLod	*lod_params;
	int							ref_count;
	WorldDrawableListPool		*pool;
	int							uid;
} WorldInstanceParamList;

typedef struct WorldDrawableEntryVertexLightColors
{
	F32		offset;
	F32		multipler;
	U32		*vertex_light_colors;
} WorldDrawableEntryVertexLightColors;

typedef struct WorldDrawableEntry
{
	WorldCellEntry			base_entry; // must be first

	Vec3					world_fade_mid;
	F32						world_fade_radius;

	struct 
	{
		U32					last_update_time;
	} occlusion_data;
	U8						occluded_bits;

	U8 						debug_me			:1;
	U8 						wireframe			:2;
	U8	 					camera_facing		:1;

	U8	 					axis_camera_facing	:1;
	U8						unlit				:1;
	U8						editor_only			:1;
	U8						headshot_visible	:1;

	U8						occluder			:1;
	U8						double_sided_occluder:1;
	U8						low_detail			:1;
	U8						high_detail			:1;

	U8						high_fill_detail	:1;
	U8						map_snap_hidden		:1;
	U8						no_shadow_cast		:1;
	U8						no_shadow_receive	:1;

	U8  					force_trunk_wind    :1;
	U8						controller_relative_matrix_inited :1;
	U8						no_vertex_lighting : 1;
	U8						use_character_lighting :1;

	U8						is_camera_fading :1; // The camera code has modified the alpha on this object, we want to do a few other special things
	U8						should_cluster      :2;
	// 1 bit remaining

	REF_TO(WorldFXEntry)	fx_parent_handle;
	REF_TO(WorldAnimationEntry) animation_controller_handle;
	WorldDrawableList		*draw_list; // May be shared between multiple entries
	WorldInstanceParamList	*instance_param_list;
	GfxStaticObjLightCache	*light_cache;
	Vec4					color;
	Mat4					controller_relative_matrix;	// animation may happen in a different basis, so we need a relative matrix for this drawable

	WorldDrawableEntryVertexLightColors **lod_vertex_light_colors;

} WorldDrawableEntry;

typedef struct WorldModelEntry
{
	WorldDrawableEntry		base_drawable_entry; // must be first

	Vec3					model_scale;
	U32						uid:24; // may not be unique
	U32						scaled:1;
	Vec4					wind_params;
	Vec4					current_wind_parameters; //the wind parameters computed for the object for this frame
	ModelLoadTracker		model_tracker;

	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldModelEntry;

typedef struct WorldModelInstanceInfo
{
	// world matrix in row major order
	Vec4					world_matrix_x;
	Vec4					world_matrix_y;
	Vec4					world_matrix_z;
	Vec4					tint_color;
	Vec3					world_mid;
	Vec4					current_wind_parameters; //the wind parameters computed for the object for this frame
	U32						inst_radius:30;
	U32						camera_facing:1;
	U32						axis_camera_facing:1;
	WorldInstanceParamList	*instance_param_list; // if NULL use the one from the base_drawable_entry
	// 108 bytes
} WorldModelInstanceInfo;

typedef struct WorldModelInstanceEntry
{
	WorldDrawableEntry		base_drawable_entry; // must be first

	Model					*model; // NULL in streaming mode or if belonging to a welded bin
	Vec4					wind_params;
	WorldModelInstanceInfo	**instances;
	int						lod_idx;

	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldModelInstanceEntry;

typedef struct WorldSplinedModelEntry
{
	WorldDrawableEntry		base_drawable_entry; // must be first

	SkinningMat4			spline_mats[2];
	ModelLoadTracker		model_tracker;

	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldSplinedModelEntry;

typedef struct WorldOcclusionEntry
{
	WorldDrawableEntry		base_drawable_entry; // must be first

	U32						type_flags; // has volume type info	
	Model					*model; // if NULL, draw a volume
	U8						owns_model:1;
	U8						looked_for_drawable:1;
	VolumeFaces				volume_faces;
	F32						volume_radius; // 0 for non-sphere occluders
	Vec3					volume_min, volume_max; // model occluders use volume_min for scale value
	WorldDrawableEntry		*pMatchingDrawable; // pointer to the drawable for this occluder, if there is one. Set at first draw time
	WorldCellEntryData		base_entry_data; // should be last for cache reasons
} WorldOcclusionEntry;

typedef void (*WorldVolumeGameFunc)(WorldVolumeEntry *entry);

//////////////////////////////////////////////////////////////////////////

WorldCellEntryData *worldCellEntryGetData(SA_PARAM_NN_VALID WorldCellEntry *entry);
void worldCellEntryOpen(int iPartitionIdx, WorldCellEntry *entry, WorldRegion *region);
void worldCellEntryOpenAll(WorldCellEntry *entry, WorldRegion *region);
void worldCellEntryClose(int iPartitionIdx, WorldCellEntry *entry);
void worldCellEntryCloseAndClear(int iPartitionIdx, WorldCellEntry *entry);
void worldCellEntryCloseAll(WorldCellEntry *entry);
void worldCellEntryCloseAndClearAll(WorldCellEntry *entry);
void worldCellEntryFree(WorldCellEntry *entry);

__forceinline static bool worldCellEntryIsPartitionOpen(WorldCellEntry *pEntry, int iPartitionIdx)
{
	assert(iPartitionIdx>=0 && iPartitionIdx<=31);
	return ((pEntry->baIsOpen >> iPartitionIdx) & 0x00000001) != 0;
}

#define worldCellEntryIsAnyPartitionOpen(pEntry) ((pEntry)->baIsOpen != 0)

__forceinline static bool worldCellEntryIsPartitionDisabled(WorldCellEntry *pEntry, int iPartitionIdx)
{
	assert(iPartitionIdx>=0 && iPartitionIdx<=31);
	return ((pEntry->baIsDisabled >> iPartitionIdx) & 0x00000001) != 0;
}

#define worldCellEntryIsAnyPartitionDisabled(pEntry) ((pEntry)->baIsDisabled != 0)

__forceinline static void worldCellEntrySetPartitionDisabled(WorldCellEntry *pEntry, int iPartitionIdx, bool bIsDisabled)
{
	assert(iPartitionIdx>=0 && iPartitionIdx<=31);
	if (bIsDisabled) {
		pEntry->baIsDisabled |= (0x00000001 << iPartitionIdx);
	} else {
		pEntry->baIsDisabled &= ~(0x00000001 << iPartitionIdx);
	}
}


void worldEntryCreateForDefTree(SA_PARAM_NN_VALID ZoneMapLayer *layer, 
								SA_PARAM_NN_VALID GroupDef *root_def, 
								SA_PRE_OP_RBYTES(sizeof(Mat4)) const Mat4 world_matrix);
void worldEntryDestroyInstanceEntry(InstanceEntryInfo *instance_info, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info);
bool worldEntryCreateForTracker(GroupTracker *tracker, const Mat4 tracker_world_matrix, bool in_editor, bool in_headshot, bool temp_group, bool is_client, GroupSplineParams *params, GroupTracker *spline_tracker);
void worldEntryCreateForWeldedBin(WorldCellWeldedBin *bin);

void worldEntryUpdateWireframe(SA_PARAM_OP_VALID WorldCellEntry *entry, SA_PARAM_OP_VALID GroupTracker *tracker);

SA_RET_OP_VALID ZoneMapLayer *worldEntryGetLayer(SA_PARAM_OP_VALID WorldCellEntry *entry);

void worldCellEntrySetDisabled(int iPartitionIdx, WorldCellEntry *entry, bool disabled);
void worldCellEntrySetDisabledAll(WorldCellEntry *entry, bool disabled);
void worldInteractionEntrySetDisabled(int iPartitionIdx, WorldInteractionEntry *entry, bool disabled);
void worldInteractionEntrySetFX(int iPartitionIdx, SA_PARAM_NN_VALID WorldInteractionEntry *entry, SA_PARAM_OP_STR const char *fx_name, SA_PARAM_OP_STR const char* unique_fx_name);

void worldVolumeEntrySetGameCallbacks(SA_PARAM_OP_VALID WorldVolumeGameFunc open_func, SA_PARAM_OP_VALID WorldVolumeGameFunc close_func);

SA_RET_OP_VALID const Model *worldDrawableEntryGetModel(SA_PARAM_NN_VALID WorldDrawableEntry *draw, SA_PRE_OP_FREE SA_POST_OP_VALID int *idx, 
													 Mat4 spline_mats[2], 
													 SA_PRE_OP_FREE SA_POST_OP_VALID bool *has_spline, 
													 SA_PRE_OP_FREE SA_POST_OP_VALID F32 **model_scale);

SA_RET_OP_VALID WorldInteractionEntry *worldGetInteractionEntryFromUid(SA_PARAM_NN_VALID ZoneMap *zmap, int uid, bool closed_ok);

SA_RET_OP_VALID WLCostume *worldInteractionGetWLCostume(SA_PARAM_OP_STR const char *interaction_name);
SA_RET_OP_STR const char *worldInteractionGetCarryAnimationBitName(SA_PARAM_OP_STR const char *interaction_name);

void worldFXConditionSetState(const char *fx_condition, bool state);
void worldFxReloadEntriesMatchingFxName(const char* fx_name);
void worldFXUpdateOncePerFrame(F32 loop_timer, U32 update_timestamp);

void worldAnimationEntryUpdate(SA_PARAM_NN_VALID WorldAnimationEntry *entry, F32 loop_timer, U32 update_timestamp);
void worldAnimationEntryModifyBounds(SA_PARAM_NN_VALID WorldAnimationEntry *entry, 
									 SA_PRE_OP_RBYTES(sizeof(Mat4)) const Mat4 world_matrix, 
									 SA_PRE_OP_BYTES(sizeof(Vec3)) SA_POST_OP_VALID Vec3 local_min, 
									 SA_PRE_OP_BYTES(sizeof(Vec3)) SA_POST_OP_VALID Vec3 local_max, 
									 SA_PRE_NN_BYTES(sizeof(Vec3)) SA_POST_NN_VALID Vec3 world_mid, 
									 SA_PARAM_NN_VALID F32 *radius);
bool worldAnimationEntryInitChildRelativeMatrix(SA_PARAM_OP_VALID WorldAnimationEntry *animation_controller, const Mat4 world_matrix, Mat4 controller_relative_matrix);

void worldCellEntryReset(ZoneMap *zmap);

void worldDrawableListPoolClearMaterialCache(SA_PARAM_NN_VALID WorldDrawableListPool *pool);
void worldCellEntryClearTempMaterials(void);

void worldDrawableListPoolClearGeoCache(SA_PARAM_NN_VALID WorldDrawableListPool *pool);
void worldCellEntryClearTempGeoDraws(void);

void worldFXClearPostDrawing(void);

SA_RET_NN_VALID WorldModelInstanceInfo *worldDupModelInstanceInfo(SA_PARAM_NN_VALID WorldModelInstanceInfo *info);
void worldFreeModelInstanceInfo(SA_PRE_OP_VALID SA_POST_P_FREE WorldModelInstanceInfo *info);

WorldDrawableList *worldCreateDrawableListDbg(Model *model, const Vec3 model_scale, const GroupInfo *info, GroupDef *def, 
											  const char *material_replace, const char ***texture_swaps, const char ***material_swaps, 
											  MaterialNamedTexture **named_texture_swaps, MaterialNamedConstant **material_constants,
											  WorldDrawableListPool *pool, 
											  int bWaitForLoad, WLUsageFlags use_flags,
											  WorldInstanceParamList **instance_param_list MEM_DBG_PARMS);
#define worldCreateDrawableList(model, model_scale, info, def, material_replace, texture_swaps, material_swaps, named_texture_swaps, material_constants, pool, bWaitForLoad, use_flags, instance_param_list) worldCreateDrawableListDbg(model, model_scale, info, def, material_replace, texture_swaps, material_swaps, named_texture_swaps, material_constants, pool, bWaitForLoad, use_flags, instance_param_list MEM_DBG_PARMS_INIT)

WorldRegion *worldCellGetLightRegion(WorldRegion *region);

void worldOcclusionEntryGetScale(const WorldOcclusionEntry *entry, Vec3 scale);
void worldOcclusionEntrySetScale(WorldOcclusionEntry *entry, Vec3 scale);

AUTO_STRUCT;
typedef struct WorldDependenciesParsed
{
	const char *filename;						AST(CURRENTFILE)
	char **material_deps;						AST(POOL_STRING)
	char **texture_deps;						AST(POOL_STRING)
	char **sky_deps;							AST(POOL_STRING)
} WorldDependenciesParsed;

AUTO_STRUCT;
typedef struct WorldDependenciesList
{
	WorldDependenciesParsed **deps;
} WorldDependenciesList;
extern ParseTable parse_WorldDependenciesList[];
#define TYPE_parse_WorldDependenciesList WorldDependenciesList

void worldDepsReportMaterial(const char *material_name, GroupDef *def);
void worldDepsReportTexture(const char *texture_name);
void worldDepsReportSky(const char *sky_name);

WorldDrawableEntry * worldCollisionEntryToWorldDrawable(const WorldCollisionEntry * entry);
WorldCollObject *worldCollisionEntryGetCollObject(WorldCollisionEntry *pEntry, int iPartitionIdx);

WorldDrawableEntry * worldCellFindDrawableForOccluder(const WorldCell * cell, const WorldOcclusionEntry *me);

AUTO_STRUCT;
typedef struct ClientOverridenWorldDrawableEntry
{
	WorldDrawableEntry *pDrawableEntry;		NO_AST

	// The original alpha value
	F32 fOriginalAlpha;
} ClientOverridenWorldDrawableEntry;

// List of world drawable entries which are overridden by the client
extern ClientOverridenWorldDrawableEntry **g_ppClientOverridenWorldDrawableEntries;

GMesh * worldVolumeCalculateUnionMesh(WorldVolumeEntry *pEntry);

#endif //__WORLDCELLENTRY_H__
