#ifndef _RDRDRAWLISTPRIVATE_H_
#define _RDRDRAWLISTPRIVATE_H_

#include "windefinclude.h"
#include "earray.h"

#include "RdrDevice.h"
#include "RdrDrawable.h"
#include "RdrState.h"

typedef struct StashTableImp *StashTable;
typedef struct WTCmdPacket WTCmdPacket;
typedef struct LinearAllocator LinearAllocator;
typedef struct DynSkinningMatSet DynSkinningMatSet;
typedef struct SortedArray SortedArray;

typedef struct RdrDrawListData
{
	LinearAllocator *allocator;

	SortedArray * light_hashtable;
	SortedArray	* ambient_light_hashtable;
	StashTable 	light_list_hashtable;
	SortedArray * material_hashtable;

	DynSkinningMatSet** skinning_mat_sets_to_dec_ref;

	RdrDrawListPassData *pass_data;		// dynarray
	int			pass_count, pass_max;
	RdrDrawListPassData **pass_earray;	// for the gdraw to reference

	U32			next_frustum_bit;
	U64			next_frustum_flag; // FRUSTUM_CLIP_TYPE

	RdrDrawListPassData *visual_pass_data;
	Vec4		visual_pass_view_z;

	bool		sent_materials;
	bool		sent_lights, sent_light_shadow_params;
	bool		sort_opaque;
	bool		use_shadow_buffer;
	bool		globally_using_shadow_buffer;
	bool		use_ssao;

	TexHandle	surface_handle_fixup[MAX_SURFACE_FIXUP_HANDLES];

	F32			dof_distance;

	RdrDrawListStats stats;
	RdrDrawListStats fg_stats;

	RdrDevice	*device;				// set if render thread has control right now, not available for main thread

	RdrDrawList *draw_list;				// NULL if main thread has stopped using this data and the render thread should free it when finished

} RdrDrawListData;

typedef struct RdrDrawList
{
	RdrDrawListData *current_data;

	bool	zprepass;
	bool    zprepass_test;
	bool	outlining;
	RdrDrawListLightingMode light_mode;
	bool	separate_hdr_pass;
	bool	has_hdr_texture;
	bool	aux_visual_pass;
	F32		bloom_brightness;

	bool	need_texture_screen_color;	// Some object added to the list requires the screen color as a texture
	bool	need_texture_screen_depth;	// Some object added to the list requires the depth as a texture
	bool	need_texture_screen_color_late; // Same for late pass
	bool	need_texture_screen_depth_late;
	bool	need_texture_screen_color_blurred; // Some object added to the list requires the blurred screen color as a texture

	bool	is_primary_drawlist;

	F32		max_bloom_brightness, bloom_brightness_multiplier;

	RdrDrawListStats stats;
	RdrDrawListStats last_stats;

	RdrDrawListData **data_ptrs;
	CRITICAL_SECTION release_critical_section;

} RdrDrawList;

typedef struct RdrDrawListSortCmd
{
	RdrDrawListData *draw_data;
	int pass_num;
} RdrDrawListSortCmd;

typedef struct RdrDrawListDrawCmd
{
	RdrDrawListData *draw_data;
	int pass_num;
	RdrSortBucketType sort_bucket_type;

	// for low res passes
	unsigned is_low_res_edge_pass : 1;
	unsigned manual_depth_test : 1;
} RdrDrawListDrawCmd;

void rdrReleaseDrawListDataDirect(RdrDevice *device, RdrDrawListData **data_ptr, WTCmdPacket *packet);

__forceinline static void rdrSortDrawListData(RdrDevice *device, RdrDrawListData *data, int pass_num)
{
	RdrDrawListSortCmd *cmd = wtAllocCmd(device->worker_thread, RDRCMD_DRAWLIST_SORT, sizeof(*cmd));
	cmd->draw_data = data;
	cmd->pass_num = pass_num;
	wtSendCmd(device->worker_thread);
}

__forceinline static int rdrDrawDrawListData(RdrDevice *device, RdrDrawList *draw_list, RdrDrawListData *data, RdrSortBucketType type, int pass_num, bool is_low_res_edge_pass, bool manual_depth_test)
{
	RdrDrawListPassData *pass_data = &data->pass_data[pass_num];
	RdrDrawListDrawCmd *cmd = wtAllocCmd(device->worker_thread, RDRCMD_DRAWLIST_DRAW, sizeof(*cmd));
	int total_tri_count = pass_data->sort_node_buckets[type].total_tri_count;
	pass_data->owned_by_thread = true;
	cmd->draw_data = data;
	cmd->sort_bucket_type = type;
	cmd->pass_num = pass_num;
	cmd->is_low_res_edge_pass = is_low_res_edge_pass;
	cmd->manual_depth_test = manual_depth_test;
	if (type >= RSBT_ALPHA_PRE_DOF)
	{
		data->fg_stats.pass_stats[pass_data->shader_mode].alpha_objects_drawn += eaSize(&pass_data->sort_node_buckets[type].sort_nodes);
		data->fg_stats.pass_stats[pass_data->shader_mode].alpha_triangles_drawn += total_tri_count;
	}
	else
	{
		data->fg_stats.pass_stats[pass_data->shader_mode].opaque_objects_drawn += eaSize(&pass_data->sort_node_buckets[type].sort_nodes);
		data->fg_stats.pass_stats[pass_data->shader_mode].opaque_triangles_drawn += total_tri_count;
	}
	wtSendCmd(device->worker_thread);
	return total_tri_count;
}

__forceinline static void rdrReleaseDrawListData(RdrDevice *device, RdrDrawListData *data)
{
	wtQueueCmd(device->worker_thread, RDRCMD_DRAWLIST_RELEASE, &data, sizeof(data));
}

__forceinline static bool rdrSortNodesCanInstance(const RdrSortNode *sort_node1, const RdrSortNode *sort_node2)
{
	return	sort_node1->do_instancing && sort_node2->do_instancing && 
			sort_node1->subobject_idx == sort_node2->subobject_idx && 
			sort_node1->subobject_count == sort_node2->subobject_count && 
			sort_node1->lights == sort_node2->lights && 
			sort_node1->force_no_shadow_receive == sort_node2->force_no_shadow_receive &&
			sort_node1->uses_far_depth_range == sort_node2->uses_far_depth_range &&
			sort_node1->camera_centered == sort_node2->camera_centered &&
			sort_node1->debug_me == sort_node2->debug_me &&
			sort_node1->add_material_flags == sort_node2->add_material_flags &&
			sort_node1->category == sort_node2->category &&
			sort_node1->has_wind == sort_node2->has_wind &&
			sort_node1->has_trunk_wind == sort_node2->has_trunk_wind && 
			sort_node1->ignore_vertex_colors == sort_node2->ignore_vertex_colors &&
			sort_node1->two_bone_skinning == sort_node2->two_bone_skinning &&
			sameVec4(sort_node1->wind_params, sort_node2->wind_params);
}


#endif //_RDRDRAWLISTPRIVATE_H_

