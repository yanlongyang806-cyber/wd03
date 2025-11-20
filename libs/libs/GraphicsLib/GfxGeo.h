#pragma once
GCC_SYSTEM

#include "WorldLibEnums.h"
#include "wlModelEnums.h"
#include "RdrEnums.h"
#include "wlModel.h"
#include "GraphicsLibPrivate.h"

typedef struct GMeshReductions GMeshReductions;
typedef int GeoHandle;

typedef struct GeoRenderInfoDetails {
	const char *filename;
	const char *name;
	U32			sys_mem_packed; // Packed and overhead
	U32			sys_mem_unpacked;
	F32			far_dist;
	F32			uv_density;
	const char *lod_template_name;
} GeoRenderInfoDetails;

typedef void (*GeoGetInfoCallbackFunc)(GeoRenderInfo *geo_render_info, void *parent, int param, GeoRenderInfoDetails *details);

// a ModelLOD has exactly one GeoRenderInfo (but several Material objects?)
typedef struct GeoRenderInfo {
	GeoBoolCallbackFunc	fill_callback;
	GeoCallbackFunc		free_memory_callback;
	GeoGetInfoCallbackFunc info_callback;
	void				*parent;
	int					param;
	WLUsageFlags		use_flags;
	U32					is_auxiliary:1; // Prevents geo_last_used_count from incrementing
	U32					is_canceled:1; // Tells the background thread that this GeoRenderInfo no longer needs to be filled out, it's been freed, various pointers may be invalid
	U32					is_being_processed_in_bg:1; // At this instance, this GeoRenderInfo is being processed in the background thread
	U32					info_callback_does_system_memory:1; // The info callback adjusts the system memory portion of the info
	U32					load_in_foreground:1;
	WLUsageFlagsBitIndex geo_mem_usage_bitindex:5; // Index this memory was tracked to
	S32					geo_last_sys_mem_usage; // System memory usage currently tracked

	int					send_queued; // do not make this part of the bit field

	GeoHandle			geo_handle_primary;
	U32					geo_loaded; // Bitmask of what renderers have it loaded
	U32					geo_last_used_timestamp;
	U32					geo_last_used_count;
	U32					geo_last_used_count_swapped;
	U32					geo_last_used_unique;
	U32					geo_last_used_unique_swapped;
	U32					geo_last_used_swap_frame; // Once per frame, move geo_last_used_count into geo_last_used_count_swapped
	U32					geo_loading_for; // Bitmask of what renders want it loaded
	U32					geo_loaded_timestamp; // When this model was loaded
	S32					geo_memory_use; // Video memory usage currently tracked
	volatile U32		num_lod_loads_queued;

	volatile GeoLoadState vbo_load_state;

	// unpacked and properly formatted/generated data for the renderer
	int				tri_count;		// Moved out to avoid getting cleared
	int				vert_count;		// Moved out to avoid getting cleared
	int				subobject_count;// Moved out to avoid getting cleared
	RdrGeoUsage		usage;  // Moved out to avoid getting cleared
	struct {
		// Warning: everything in there gets memset to 0 during run-time
		void			*data_ptr; // data to be freed
		int				data_size;

		U16				*tris;
		// The packed data will span multiple sub-objects.  As far as I can tell, they can share verts, and that
		// means they all must have the same vertex format, which may not actually be desirable in the future, though
		// if vertex components are totally de-interleaved, this becomes less of an issue
		U8				*vert_data; // packed data - should be platform-specific
		int				*subobject_tri_counts;
		int				*subobject_tri_bases;
	} vbo_data_primary;

} GeoRenderInfo;

void gfxGeoOncePerFrame(void);
void gfxGeoOncePerFramePerDevice(void);
void gfxGeoDoQueuedFrees(void);

// isThreadSafe should be true if you have wrapped the function calling this
// in a critical section, or are otherwise sure it's safe to call this from
// multiple threads.
// free_memory_callback may be called in *either* the background or foreground threads
GeoRenderInfo *gfxCreateGeoRenderInfo(SA_PARAM_NN_VALID GeoBoolCallbackFunc fill_callback, SA_PARAM_NN_VALID GeoCallbackFunc free_memory_callback, SA_PARAM_NN_VALID GeoGetInfoCallbackFunc info_callback, bool info_callback_does_system_memory, void *parent, int param, RdrGeoUsage usage, WLUsageFlags use_flags, bool load_in_foreground);

void gfxFreeGeoRenderInfo(SA_PARAM_OP_VALID GeoRenderInfo *geo_render_info, bool free_data_only);
void gfxGeoClearAllForDevice(int rendererIndex);
void gfxGeoClearForDevice(GeoRenderInfo *geo_render_info, int rendererIndex);

bool gfxGeoIsLoaded(SA_PARAM_NN_VALID GeoRenderInfo *geo_render_info);
bool gfxGeoSetupVertexObject(SA_PARAM_NN_VALID GeoRenderInfo *geo_render_info);
void gfxGeoDoneWithVertexObject(SA_PARAM_NN_VALID GeoRenderInfo *geo_render_info);

__forceinline static void gfxGeoIncrementUsedCount(GeoRenderInfo *geo_render_info, int inc, bool unique)
{
	if (geo_render_info->is_auxiliary)
		inc = 0;
	
	if (geo_render_info->geo_last_used_swap_frame != gfx_state.frame_count) {
		geo_render_info->geo_last_used_count_swapped = (geo_render_info->geo_last_used_swap_frame == gfx_state.frame_count - 1)?
			geo_render_info->geo_last_used_count:
			0;
		geo_render_info->geo_last_used_unique_swapped = (geo_render_info->geo_last_used_swap_frame == gfx_state.frame_count - 1)?
			geo_render_info->geo_last_used_unique:
			0;
		geo_render_info->geo_last_used_count = 0;
		geo_render_info->geo_last_used_unique = 0;
		geo_render_info->geo_last_used_swap_frame = gfx_state.frame_count;
	}

	if (unique) {
		geo_render_info->geo_last_used_unique += inc;
	} else {
		geo_render_info->geo_last_used_count += inc;
	}
}

// The basic demand load function for GeoRenderInfos.  Models have their own function and should not use this one.
__forceinline static GeoHandle gfxGeoDemandLoad(GeoRenderInfo *geo_render_info)
{
	if (!geo_render_info)
		return 0;

	if (gfxGeoIsLoaded(geo_render_info) || gfxGeoSetupVertexObject(geo_render_info)) {
		gfxGeoIncrementUsedCount(geo_render_info, 1, false);
		return geo_render_info->geo_handle_primary;
	}

	return 0;
}


const char *gfxModelLODGetName(ModelLOD *model);

// memory tracking
U32 gfxGetLoadedGeoCount(U32 *counts, WLUsageFlags flags_for_total);
U32 gfxGetLoadedGeoSize(U32 *sizes, WLUsageFlags flags_for_total);

int gfxGeoNumLoadsPending(void);
int gfxGeoQuotaPerFrame();
int gfxGeoSentThisFrame();

extern U32 geoMemoryUsage;

