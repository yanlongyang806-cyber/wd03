/***************************************************************************



***************************************************************************/

#ifndef __WORLDCELLSTREAMINGPRIVATE_H__
#define __WORLDCELLSTREAMINGPRIVATE_H__
GCC_SYSTEM

#include "WorldCellEntry.h"
#include "WorldGridPrivate.h"
#include "RoomConnPrivate.h"
#include "GenericMesh.h"
#include "MapSnap.h"

//////////////////////////////////////////////////////////////////////////
//
// Notes
//------------------------------------------------------------------------
// 
// System Restrictions:
// - Must support individual layers switching from streaming mode to 
//   editing mode.
// - Must not require opening trackers for all layers at the same time to 
//   make bins.
// 
// Internals:
// - Bounds information is binned for all cells and loaded with the map.
// - As a cell comes into range, its entries are loaded from bins.
// - When a layer switches to editing mode, the bin-based cell tree is 
//   discarded and all entries are loaded from bins and inserted in the 
//   new cell tree.  Entries belonging to the layer that is switching to 
//   editing mode are discarded and the layer is loaded from src files.
//   The tracker tree is then opened for the layer, which creates new 
//   entries that are inserted in the cell tree.
//
//////////////////////////////////////////////////////////////////////////

#define WORLD_STREAMING_BIN_VERSION 78
#define WORLD_EXTERNAL_BIN_VERSION 1

typedef struct WorldCellParsed WorldCellParsed;
typedef struct WorldInteractionCostume WorldInteractionCostume;
typedef struct MaterialConstantMappingFake MaterialConstantMappingFake;
typedef struct WorldZoneMapScope WorldZoneMapScope;
typedef struct HogFile HogFile;
typedef struct SerializablePackedStructStream SerializablePackedStructStream;
typedef struct PackedStructStream PackedStructStream;
typedef struct WorldPathNode WorldPathNode;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#define DELTA_ENCODE_ENTRIES 0

// sorts cell entries before writing them out; this is not always a win for some reason, and I think it may not be great for patching
#if DELTA_ENCODE_ENTRIES
#define SORT_ENTRIES 1
#else
#define SORT_ENTRIES 0
#endif

AUTO_STRUCT;
typedef struct SplineParamsParsed
{
	Vec3 param0;
	Vec3 param1;
	Vec3 param2;
	Vec3 param3;

	Vec3 param4;
	Vec3 param5;
	Vec3 param6;
	Vec3 param7;
} SplineParamsParsed;

AUTO_STRUCT;
typedef struct WorldCellEntryBoundsParsed
{
	Mat4					world_matrix;
	int						local_mid_idx;					AST( DEFAULT(-1) )
	U32						object_tag;
} WorldCellEntryBoundsParsed;

AUTO_STRUCT;
typedef struct WorldCellEntryParsed
{
	WorldCellEntryBoundsParsed	entry_bounds;
	int							shared_bounds_idx;
	int							parent_entry_uid;			AST( DEFAULT(-1) )
	int							interaction_child_idx;
	U32							group_id;  // In may cases, this will contain layer bits instead of group id.
	                                       // If we need to have group id, we need to add a different field for layer bits.
} WorldCellEntryParsed;

AUTO_STRUCT;
typedef struct WorldInteractionEntryServerParsed
{
	// common data
	WorldCellEntryParsed		base_data;

	// entry specific data
	int							uid;
	int							initial_selected_child;
	int							visible_child_count;
	WorldInteractionProperties	*full_interaction_properties;

} WorldInteractionEntryServerParsed;

AUTO_STRUCT;
typedef struct WorldInteractionEntryClientParsed
{
	// common data
	WorldCellEntryParsed		base_data;

	// entry specific data
	int							uid;
	int							initial_selected_child;
	int							visible_child_count;
	WorldBaseInteractionProperties	*base_interaction_properties;

} WorldInteractionEntryClientParsed;

AUTO_STRUCT;
typedef struct WorldVolumeEntryServerParsed
{
	// common data
	WorldCellEntryParsed		base_data;

	// entry specific data
	WorldVolumeElement			**elements;
	int							*volume_type_string_idxs;
	U32							indoor : 1;
	U32							occluder : 1;
	U32							attached_to_parent : 1;

	GroupVolumePropertiesServer properties;

	int							named_volume_id;

} WorldVolumeEntryServerParsed;

AUTO_STRUCT;
typedef struct WorldVolumeEntryClientParsed
{
	// common data
	WorldCellEntryParsed		base_data;

	// entry specific data
	WorldVolumeElement			**elements;
	int							*volume_type_string_idxs;
	U32							indoor : 1;
	U32							occluder : 1;

	GroupVolumePropertiesClient properties;

} WorldVolumeEntryClientParsed;

// note: heightmap collision and editor-only collision objects are not binned.
AUTO_STRUCT;
typedef struct WorldCollisionEntryParsed
{
	// common data
	WorldCellEntryParsed	base_data;

	// entry specific data
	SplineParamsParsed		*spline;
	int						model_idx;					AST( DEFAULT(-1) )
	const char				**eaMaterialSwaps;
	U32						filterBits;
	Vec3					scale;						AST( FLOAT_TENTHS )
	F32						collision_radius;
	Vec3					collision_min;
	Vec3					collision_max;

} WorldCollisionEntryParsed;

AUTO_STRUCT;
typedef struct WorldAltPivotEntryParsed
{
	// common data
	WorldCellEntryParsed	base_data;

	U32						mass_pivot:1;
	U32						hand_pivot:1;
	int						carry_anim_bit_string_idx;	AST( DEFAULT(-1) )
} WorldAltPivotEntryParsed;

//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct WorldSoundEntryParsed
{
	// common data
	WorldCellEntryParsed	base_data;

	// entry specific data
	int						event_name_idx;				AST( DEFAULT(-1) )
	int						excluder_str_idx;			AST( DEFAULT(-1) )
	int						dsp_str_idx;				AST( DEFAULT(-1) )
	int						editor_group_str_idx;		AST( DEFAULT(-1) )
	int						sound_group_str_idx;		AST( DEFAULT(-1) )
	int						sound_group_ord_idx;		AST( DEFAULT(-1) )

} WorldSoundEntryParsed;

AUTO_STRUCT;
typedef struct WorldLightEntryParsed
{
	// common data
	WorldCellEntryParsed	base_data;

	// entry specific data
	LightData				*light_data; // need to fix up the region pointer after loading
	int						animation_entry_id; // for animation controller

} WorldLightEntryParsed;

AUTO_STRUCT;
typedef struct WorldWindSourceEntryParsed
{
	// common data
	WorldCellEntryParsed	base_data;

	// entry specific data
	WorldWindSourceProperties source_data; 
} WorldWindSourceEntryParsed;

AUTO_STRUCT;
typedef struct WorldFXDebrisParsed
{
	int						model_idx;					AST( DEFAULT(-1) )
	int						draw_list_idx;				AST( DEFAULT(-1) )
	int						instance_param_list_idx;	AST( DEFAULT(-1) )
	Vec3					scale;						AST( FLOAT_TENTHS )
	Vec4					tint_color;					AST( FLOAT_HUNDREDTHS )
} WorldFXDebrisParsed;

AUTO_STRUCT;
typedef struct WorldFXEntryParsed
{
	// common data
	WorldCellEntryParsed	base_data;

	// entry specific data
	U32						id;
	int						fx_name_idx;				AST( DEFAULT(-1) )
	F32						fx_hue;						AST( FLOAT_HUNDREDTHS )
	int						fx_condition_idx;			AST( DEFAULT(-1) )
	int						fx_params_idx;				AST( DEFAULT(-1) )
	int						fx_faction_idx;				AST( DEFAULT(-1) )
	WorldFXDebrisParsed		*debris;
	U32						interaction_node_owned : 1;
	U32						low_detail : 1;
	U32						high_detail : 1;
	U32						high_fill_detail : 1;
	U32						has_target_node : 1;
	U32						target_no_anim : 1;
	Mat4					target_node_mat;
	int						animation_entry_id; // for animation controller

} WorldFXEntryParsed;

AUTO_STRUCT;
typedef struct WorldAnimationEntryParsed
{
	// common data
	WorldCellEntryParsed	base_data;

	// entry specific data
	U32						id;
	WorldAnimationProperties animation_properties;
	int						parent_animation_entry_id; // for parent animation controller

} WorldAnimationEntryParsed;

//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct MaterialDrawParsed
{
	int						material_name_idx;
	S32						fallback_idx	: 6;
	U32						has_swaps		: 1;
	U32						is_occluder		: 1;
	int						*texture_idxs;
	F32						*constants;
	MaterialConstantMappingFake	**constant_mappings;
} MaterialDrawParsed;

AUTO_STRUCT;
typedef struct ModelDrawParsed
{
	int						model_idx;
	U8						lod_idx;
} ModelDrawParsed;

AUTO_STRUCT;
typedef struct WorldDrawableSubobjectParsed
{
	int						*materialdraw_idxs;
	int						modeldraw_idx;
	U8						subobject_idx;
} WorldDrawableSubobjectParsed;

AUTO_STRUCT;
typedef struct WorldDrawableLodParsed
{
	int						*subobject_idxs;
	int						subobject_count;
	F32						near_dist;				AST( FLOAT_TENTHS )
	F32						far_dist;				AST( FLOAT_TENTHS )
	F32						far_morph;				AST( FLOAT_TENTHS )
	U32						occlusion_materials;
	bool					no_fade;
} WorldDrawableLodParsed;

AUTO_STRUCT;
typedef struct WorldDrawableListParsed
{
	WorldDrawableLodParsed	**drawable_lods;
	bool					no_fog;
	bool					high_detail_high_lod;
} WorldDrawableListParsed;

AUTO_STRUCT;
typedef struct WorldInstanceParamListParsed
{
	int lod_count;
	F32 *instance_params;
} WorldInstanceParamListParsed;

// Currently 32-bits total. Expanding past 32-bits will nearly guarantee significantly different client bins, 
// and harmfully large patch sizes.
AUTO_STRUCT;
typedef struct WorldDrawableBitfield
{
	U32				camera_facing : 1;
	U32				axis_camera_facing : 1;
	U32				unlit : 1;
	U32				occluder : 1;

	U32				double_sided_occluder : 1;
	U32				low_detail : 1;
	U32				high_detail : 1;
	U32				high_fill_detail : 1;

	U32				map_snap_hidden : 1;
	U32				no_shadow_cast : 1;
	U32				no_shadow_receive : 1;
	U32				force_trunk_wind : 1;

	U32				no_vertex_lighting : 1;
	U32				use_character_lighting : 1;
	// 14-bits

	// CD: these are here so I can add stuff without forcing rebinning; should be short term only
	U32				future_flag2 : 1;
	U32				future_flag3 : 1;
	U32				future_flag4 : 1;
	U32				future_flag5 : 1;
	// 18-bits
} WorldDrawableBitfield;

AUTO_STRUCT;
typedef struct WorldDrawableEntryVertexLightColorsParsed
{
	F32		offset;
	F32		multipler;
	U32		*vertex_light_colors;
} WorldDrawableEntryVertexLightColorsParsed;

AUTO_STRUCT;
typedef struct WorldDrawableEntryParsed
{
	// common data
	WorldCellEntryParsed	base_data;

	// entry specific data
	int						fade_mid_idx;		AST( DEFAULT(-1) )
	F32						fade_radius;		AST( FLOAT_ONES )
	Vec4					color;				AST( FLOAT_HUNDREDTHS )

	int						draw_list_idx;
	int						instance_param_list_idx;
	int						fx_entry_id; // for ambient offset
	int						animation_entry_id; // for animation controller

	U32						is_near_fade:1;
	U32						is_clustered:1;
	U32						undeclared:30;	// If you need a bit for parsed data, make sure to decrease this field by the number of bits being pulled.

	union
	{
		WorldDrawableBitfield	bitfield;		AST( EMBEDDED_FLAT )
		U32						bitfield_u32;	AST( REDUNDANTNAME )
	};

	WorldDrawableEntryVertexLightColorsParsed **lod_vertex_light_colors;
} WorldDrawableEntryParsed;

AUTO_STRUCT;
typedef struct WorldModelEntryParsed
{
	// common data
	WorldDrawableEntryParsed	base_drawable;

	// entry specific data
	Vec3						model_scale;	AST( FLOAT_TENTHS )
	Vec4						wind_params;	AST( FLOAT_HUNDREDTHS )

} WorldModelEntryParsed;

AUTO_STRUCT;
typedef struct WorldModelInstanceInfoParsed
{
	// world matrix in row major order
	Vec4					world_matrix_x;
	Vec4					world_matrix_y;
	Vec4					world_matrix_z;
	Vec4					tint_color;					AST( FLOAT_HUNDREDTHS )
	int						local_mid_idx;				AST( DEFAULT(-1) )
	U32						inst_radius:30;
	U32						camera_facing:1;
	U32						axis_camera_facing:1;
	int						instance_param_list_idx;	AST( DEFAULT(-1) )
} WorldModelInstanceInfoParsed;

AUTO_STRUCT;
typedef struct WorldModelInstanceEntryParsed
{
	// common data
	WorldDrawableEntryParsed	base_drawable;

	// entry specific data
	Vec4						wind_params;	AST( FLOAT_HUNDREDTHS )
	WorldModelInstanceInfoParsed **instances;
	int							lod_idx;

} WorldModelInstanceEntryParsed;

AUTO_STRUCT;
typedef struct WorldSplinedModelEntryParsed
{
	// common data
	WorldDrawableEntryParsed	base_drawable;

	// entry specific data
	SplineParamsParsed			spline;

} WorldSplinedModelEntryParsed;

AUTO_STRUCT;
typedef struct WorldOcclusionEntryParsed
{
	// common data
	WorldCellEntryParsed		base_data;

	// entry specific data
	GMeshParsed					*gmesh;
	int							model_idx;					AST( DEFAULT(-1) )
	U32							type_flags:30;
	U32							occluder:1;					AST( DEFAULT(1) )
	U32							double_sided_occluder:1;
	VolumeFaces					volume_faces;				AST(INT)
	F32							volume_radius;				AST( FLOAT_TENTHS )
	Vec3						volume_min;					AST( FLOAT_HUNDREDTHS )
	Vec3						volume_max;					AST( FLOAT_HUNDREDTHS )

} WorldOcclusionEntryParsed;

//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct WorldInteractionCostumePartParsed
{
	int							matrix_idx;
	int							model_idx;					AST( DEFAULT(-1) )
	int							draw_list_idx;				AST( DEFAULT(-1) )
	int							instance_param_list_idx;	AST( DEFAULT(-1) )
	Vec3						scale;						AST( FLOAT_TENTHS )
	Vec4						tint_color;					AST( FLOAT_HUNDREDTHS )
	bool						collision;
} WorldInteractionCostumePartParsed;

AUTO_STRUCT;
typedef struct WorldInteractionCostumeParsed
{
	int							hand_pivot_idx;
	int							mass_pivot_idx;
	int							carry_anim_bit_string_idx;	AST( DEFAULT(-1) )
	WorldInteractionCostumePartParsed **costume_parts;
	int							*interaction_uids;
} WorldInteractionCostumeParsed;

//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct WorldCellDrawableDataParsed
{
	WorldModelEntryParsed				**model_entries;
	WorldModelInstanceEntryParsed		**model_instance_entries;
	WorldSplinedModelEntryParsed		**spline_entries;
	WorldOcclusionEntryParsed			**occlusion_entries;

} WorldCellDrawableDataParsed;

AUTO_STRUCT;
typedef struct WorldCellServerDataParsed
{
	WorldInteractionEntryServerParsed	**interaction_entries;
	WorldVolumeEntryServerParsed		**volume_entries;
	WorldCollisionEntryParsed			**collision_entries;
	WorldAltPivotEntryParsed			**altpivot_entries;
} WorldCellServerDataParsed;

AUTO_STRUCT;
typedef struct WorldCellClientDataParsed
{
	WorldInteractionEntryClientParsed	**interaction_entries;
	WorldVolumeEntryClientParsed		**volume_entries;
	WorldCollisionEntryParsed			**collision_entries;
	WorldAltPivotEntryParsed			**altpivot_entries;

	// client-only entries
	WorldSoundEntryParsed				**sound_entries;
	WorldLightEntryParsed				**light_entries;
	WorldFXEntryParsed					**fx_entries;
	WorldAnimationEntryParsed			**animation_entries;
	WorldWindSourceEntryParsed			**wind_source_entries;

	// drawable entries
	WorldCellDrawableDataParsed			drawables;

} WorldCellClientDataParsed;

AUTO_STRUCT;
typedef struct WorldCellWeldedDataParsed
{
	WorldCellDrawableDataParsed			**bins;

} WorldCellWeldedDataParsed;

AUTO_STRUCT;
typedef struct WorldCellParsed
{
	U8 is_empty:1;
	U8 no_drawables:1;
	U8 no_collision:1;
	U8 contain_cluster:1;

	BlockRange block_range;

	Vec3 draw_min;
	Vec3 draw_max;

	Vec3 coll_min;
	Vec3 coll_max;

	Vec3 bounds_min;
	Vec3 bounds_max;

	int cell_id;

	WorldCellServerDataParsed	*server_data;		NO_AST
	WorldCellClientDataParsed	*client_data;		NO_AST
	WorldCellWeldedDataParsed	*welded_data;		NO_AST

	WorldCellParsed *children[8];					NO_AST

} WorldCellParsed;

AUTO_STRUCT;
typedef struct WorldRegionCommonParsed
{
	const char *region_name;					AST( POOL_STRING )
	BlockRange cell_extents;
	Vec3 world_min;
	Vec3 world_max;
	int cell_count;
	WorldCellParsed **cells;

	WorldTagLocation **tag_locations;

	WorldCellParsed *root_cell;					NO_AST
	HogFile *hog_file;							NO_AST
	HogFile *cluster_hog_file;					NO_AST
} WorldRegionCommonParsed;

AUTO_STRUCT;
typedef struct WorldRegionServerParsed
{
	WorldRegionCommonParsed common;				AST( EMBEDDED_FLAT )

	RoomConnGraphServerParsed *conn_graph;
	WorldCivilianGenerator **world_civilian_generators;
	WorldForbiddenPosition **world_forbidden_positions;

} WorldRegionServerParsed;

AUTO_STRUCT;
typedef struct WorldRegionClientParsed
{
	WorldRegionCommonParsed common;				AST( EMBEDDED_FLAT )

	RoomConnGraphClientParsed *conn_graph;

	// So far, there is no need for a special Parsed version.  Change if necessary.
	MapSnapRegionData mapsnap_data;				AST( STRUCT(parse_MapSnapRegionData) )

	WorldPathNode **world_path_nodes;
} WorldRegionClientParsed;

AUTO_STRUCT;
typedef struct WorldInfoStringTable
{
	char **strings;
	StashTable string_hash;						NO_AST
	int *available_idxs;						NO_AST
} WorldInfoStringTable;

AUTO_STRUCT;
typedef struct WorldStreamingInfo
{
	LayerBounds **layer_bounds;
	char **layer_names;

	WorldRegionCommonParsed **regions_parsed;			NO_AST

	U32 zmap_scope_data_offset;
	U32 *region_data_offsets;

	U32 fx_entry_id_counter;
	U32 animation_entry_id_counter;
	U32 group_id_counter;

	// fields used to determine if the bins are up to date:
	bool has_errors;
	U32 parse_table_crcs;
	F32 cell_size;
	int bin_version_number;

	SerializablePackedStructStream	*packed_data_serialize;
	PackedStructStream				*packed_data;		NO_AST

	StashTable id_to_named_volume;						NO_AST  // used to associate named volumes with their volume cell entries
	StashTable id_to_interactable;						NO_AST  // used to associate named interactables with their interaction cell entries

} WorldStreamingInfo;

AUTO_STRUCT;
typedef struct Mat4Pooled
{
	Mat4 matrix;
} Mat4Pooled;

AUTO_STRUCT;
typedef struct WorldCellLocalMidParsed
{
	Vec3					local_mid;					AST( FLOAT_HUNDREDTHS )
} WorldCellLocalMidParsed;

AUTO_STRUCT;
typedef struct WorldStreamingPackedInfo
{
	WorldCellEntrySharedBounds		**shared_bounds;
	Mat4Pooled						**pooled_matrices;

	WorldCellLocalMidParsed			**shared_local_mids;

	MaterialDrawParsed				**material_draws_parsed;
	ModelDrawParsed					**model_draws_parsed;
	WorldDrawableSubobjectParsed	**subobjects_parsed;
	WorldDrawableListParsed			**drawable_lists_parsed;
	WorldInstanceParamListParsed	**instance_param_lists_parsed;

	WorldInteractionCostumeParsed	**interaction_costumes_parsed;
} WorldStreamingPackedInfo;


// TODO: Make this binarily versionable.
AUTO_STRUCT;
typedef struct WorldStreamingPooledInfo
{
	WorldInfoStringTable			*strings;
	U32								packed_info_offset;
	U32								packed_info_crc;
	WorldStreamingPackedInfo		*packed_info;						NO_AST

	SerializablePackedStructStream	*packed_data_serialize;
	PackedStructStream				*packed_data;						NO_AST

	// non-parsed data:
	MaterialDraw					**material_draws;					NO_AST
	ModelDraw						**model_draws;						NO_AST
	WorldDrawableSubobject			**subobjects;						NO_AST
	WorldDrawableList				**drawable_lists;					NO_AST
	WorldInstanceParamList			**instance_param_lists;				NO_AST

	WorldInteractionCostume			**interaction_costumes;				NO_AST

} WorldStreamingPooledInfo;


//////////////////////////////////////////////////////////////////////////
// keep the file lists in a different file so the parse table changes won't
// cause the file lists to fail to parse

AUTO_STRUCT;
typedef struct BinFileEntry
{
	char *filename;
	U32 timestamp;
} BinFileEntry;

AUTO_STRUCT;
typedef struct BinFileList
{
	char **layer_names;							// entries specify which layer they are from by indexing into this array
	const char **layer_region_names;			AST( POOL_STRING )
	char *scene_filename;
	BinFileEntry **source_files;
	BinFileEntry **output_files;
	StashTable source_file_hash;				NO_AST
	StashTable output_file_hash;				NO_AST
} BinFileList;
extern ParseTable parse_BinFileList[];
#define TYPE_parse_BinFileList BinFileList

AUTO_STRUCT;
typedef struct ClusterDependency
{
	U32 cluster_version;
} ClusterDependency;
extern ParseTable parse_ClusterDependency[];
#define TYPE_parse_ClusterDependency ClusterDependency

AUTO_STRUCT;
typedef struct BinFileListWithCRCs
{
	BinFileList *file_list;
	U32 world_crc;
	U32 encounterlayer_crc;
	U32 gaelayer_crc;
} BinFileListWithCRCs;
extern ParseTable parse_BinFileListWithCRCs[];
#define TYPE_parse_BinFileListWithCRCs BinFileListWithCRCs

extern StashTable bin_manifest_cache;

char *worldGetStreamedString(WorldStreamingPooledInfo *streaming_pooled_info, int idx);
Mat4ConstPtr worldGetStreamedMatrix(WorldStreamingPooledInfo *streaming_pooled_info, int idx);

void freeStreamingPooledInfoSafe(SA_PARAM_OP_VALID ZoneMap *zmap, SA_PRE_NN_NN_VALID WorldStreamingPooledInfo **streaming_pooled_info_ptr);

U32 getWorldCellParseTableCRC(bool client_and_server);
void verifyWorldCellCRCHasNotChanged(void);

#define IGNORE_EXIST_TIMESTAMP_BIT (1<<31)

#endif //__WORLDCELLSTREAMINGPRIVATE_H__

