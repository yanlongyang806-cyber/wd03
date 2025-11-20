#include "MemoryMonitor.h"
#include "MemoryPool.h"
#include "MemRef.h"
#include "file.h"
#include "memlog.h"
#include "utilitiesLib.h"

#include "GfxGeo.h"
#include "GfxDebug.h"
#include "GfxConsole.h"
#include "GfxTextureTools.h"
#include "GfxLoadScreens.h"
#include "WorldLib.h"
#include "textparser.h"
#include "referencesystem.h"
#include "wlAutoLOD.h"

const char GFXGEO_MEMMONITOR_NAME[] = "Geo:Models";
const char GFXGEO_TERRAIN_MEMMONITOR_NAME[] = "Geo:Terrain";

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art););
AUTO_RUN_ANON(memBudgetAddMapping("Geo:Models", BUDGET_Geometry_Art););
AUTO_RUN_ANON(memBudgetAddMapping("Geo:Terrain", BUDGET_Terrain_Art););

// #define DISABLE_ASYNC_SETUPVBO

MP_DEFINE(GeoRenderInfo);

CRITICAL_SECTION gfx_geo_critical_section; // Externed in gfxModel.c
CRITICAL_SECTION gfx_geo_flags_critical_section; // Externed in gfxModelLOD.c

U32 geoMemoryUsage=0;

static GeoRenderInfo **queued_geo_render_info_frees; // Free queued because it was still loading

AUTO_RUN;
void InitializeGfxGeoCriticalSections(void) {
	InitializeCriticalSection(&gfx_geo_critical_section);
	InitializeCriticalSection(&gfx_geo_flags_critical_section);
}

static const char *gfxGeoGetMemoryName(WLUsageFlags use_flags)
{
    if (use_flags == WL_FOR_TERRAIN)
        return GFXGEO_TERRAIN_MEMMONITOR_NAME;
    return GFXGEO_MEMMONITOR_NAME;
}

static U32 __forceinline gfxGeoGetLastUsedCount(GeoRenderInfo *geo_render_info, bool unique) {
	if (!geo_render_info)
		return 0;
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
	return unique ? geo_render_info->geo_last_used_unique_swapped : geo_render_info->geo_last_used_count_swapped;
}


// Assumes details is zeroed (at least the relevant field)
U32 gfxGetGetSysMemoryUsage(GeoRenderInfoDetails *details, GeoRenderInfo *geo_render_info)
{
	details->sys_mem_packed = sizeof(GeoRenderInfo);
	if (geo_render_info->info_callback_does_system_memory) {
		geo_render_info->info_callback(geo_render_info, geo_render_info->parent, geo_render_info->param, details);
	}
	return details->sys_mem_packed + details->sys_mem_unpacked;
}

GeoRenderInfo *gfxCreateGeoRenderInfo(GeoBoolCallbackFunc fill_callback, GeoCallbackFunc free_memory_callback, GeoGetInfoCallbackFunc info_callback, 
									  bool info_callback_does_system_memory, void *parent, int param, 
									  RdrGeoUsage usage, WLUsageFlags use_flags, 
									  bool load_in_foreground)
{
	GeoRenderInfo *geo_render_info;

	EnterCriticalSection(&gfx_geo_critical_section);

	//assert(use_flags);
	if (!use_flags)
		use_flags = WL_FOR_WORLD;

	MP_CREATE(GeoRenderInfo, 256);

	InterlockedExchangeAdd(&wl_geo_mem_usage.loadedSystem[wlGetUsageFlagsBitIndex(use_flags)], sizeof(GeoRenderInfo));
	InterlockedExchangeAdd(&wl_geo_mem_usage.loadedSystemTotal, sizeof(GeoRenderInfo));

	geo_render_info = MP_ALLOC(GeoRenderInfo);
	geo_render_info->fill_callback = fill_callback;
	geo_render_info->free_memory_callback = free_memory_callback;
	geo_render_info->info_callback = info_callback;
	geo_render_info->info_callback_does_system_memory = info_callback_does_system_memory;
	geo_render_info->parent = parent?parent:geo_render_info;
	geo_render_info->param = param;
	geo_render_info->use_flags = use_flags;
	geo_render_info->geo_mem_usage_bitindex = wlGetUsageFlagsBitIndex(use_flags);

	geo_render_info->usage = usage;
	if (!(gfx_state.allRenderersFeatures & FEATURE_DECL_F16_2))
	{
		int i;
		for (i=0;i<usage.iNumPrimaryStreams;i++)
		{
			if (geo_render_info->usage.bits[i] & (RUSE_TEXCOORDS | RUSE_TEXCOORD2S))
				geo_render_info->usage.bits[i] |= RUSE_TEXCOORDS_HI_FLAG;
		}
	}
	geo_render_info->load_in_foreground = load_in_foreground;
	LeaveCriticalSection(&gfx_geo_critical_section);
	return geo_render_info;
}

typedef struct QueuedGeoFree {
	GeoHandle geo_handle;
	U32 free_for_who;
	S32 geo_memory_use;
	WLUsageFlagsBitIndex geo_mem_usage_bitindex:5;
	WLUsageFlags use_flags;
} QueuedGeoFree;
MP_DEFINE(QueuedGeoFree);

static QueuedGeoFree **queuedGeoFrees;

static bool gfxGeoIsLoadingInBg(GeoRenderInfo *geo_render_info)
{
	return geo_render_info->num_lod_loads_queued > 0 || geo_render_info->vbo_load_state == GEO_LOADING;
}

__forceinline static void processQueuedFree(int i, bool free_geometry)
{
	QueuedGeoFree *queued_free = queuedGeoFrees[i];
	if (queued_free->free_for_who & gfx_state.currentRendererFlag) {
		WLUsageFlagsBitIndex use_bit = wlGetUsageFlagsBitIndex(queued_free->use_flags);
		if (free_geometry)
		{
			//memlog_printf(geoGetMemLog(), "Freeing geometry with handle %d on frame %d.", queued_free->geo_handle, gfx_state.frame_count);
			rdrFreeGeometry(gfx_state.currentDevice->rdr_device, queued_free->geo_handle);
		}
		queued_free->free_for_who &= ~gfx_state.currentRendererFlag;
		memMonitorTrackUserMemory(gfxGeoGetMemoryName(queued_free->use_flags), 1, -queued_free->geo_memory_use, MM_FREE);
		gfx_state.debug.loaded_geo_count[use_bit]--;
		gfx_state.debug.loaded_geo_size[use_bit] -= queued_free->geo_memory_use;
		geoMemoryUsage -= queued_free->geo_memory_use;
		InterlockedExchangeAdd(&wl_geo_mem_usage.loadedVideo[queued_free->geo_mem_usage_bitindex], -queued_free->geo_memory_use);
		InterlockedExchangeAdd(&wl_geo_mem_usage.loadedVideoTotal, -queued_free->geo_memory_use);
	}
	if (!queued_free->free_for_who) {
		//rdrFreeGeoHandle(queued_free->geo_handle);
		eaRemoveFast(&queuedGeoFrees, i);
		MP_FREE(QueuedGeoFree, queued_free);
	}
}

void gfxGeoDoQueuedFrees(void)
{
	int i;
	EnterCriticalSection(&gfx_geo_critical_section);
	for (i=eaSize(&queued_geo_render_info_frees)-1; i>=0; i--) 
	{
		GeoRenderInfo *geo_render_info = queued_geo_render_info_frees[i];
		if (gfxGeoIsLoadingInBg(geo_render_info) && !geo_render_info->send_queued)
		{
			// Still loading
		} else {
			// Done loading, we can free it now
			eaRemoveFast(&queued_geo_render_info_frees, i);
			gfxFreeGeoRenderInfo(geo_render_info, false);
		}
	}

	for (i=eaSize(&queuedGeoFrees)-1; i>=0; i--) {
		processQueuedFree(i, true);
	}
	LeaveCriticalSection(&gfx_geo_critical_section);
}

void gfxGeoClearForDevice(GeoRenderInfo *geo_render_info, intptr_t rendererIndex)
{
	if (geo_render_info) {
		if (geo_render_info->geo_loaded & (1 << rendererIndex)) {
			WLUsageFlagsBitIndex use_bit = wlGetUsageFlagsBitIndex(geo_render_info->use_flags);
			geo_render_info->geo_loaded &= ~(1 << rendererIndex);
			memMonitorTrackUserMemory(gfxGeoGetMemoryName(geo_render_info->use_flags), 1, -geo_render_info->geo_memory_use, MM_FREE);
			gfx_state.debug.loaded_geo_count[use_bit]--;
			gfx_state.debug.loaded_geo_size[use_bit] -= geo_render_info->geo_memory_use;
			geoMemoryUsage -= geo_render_info->geo_memory_use;
			InterlockedExchangeAdd(&wl_geo_mem_usage.loadedVideo[geo_render_info->geo_mem_usage_bitindex], -geo_render_info->geo_memory_use);
			InterlockedExchangeAdd(&wl_geo_mem_usage.loadedVideoTotal, -geo_render_info->geo_memory_use);
		}
	}
}

void gfxGeoClearAllForDevice(int rendererIndex)
{
	int i;
	U32 rendererFlagMask = 1 << rendererIndex;
	U32 rendererFlagUnMask = ~rendererFlagMask;
	// Remove any queued frees for this device
	EnterCriticalSection(&gfx_geo_critical_section);
	for (i=eaSize(&queuedGeoFrees)-1; i>=0; i--) {
		if (queuedGeoFrees[i]->free_for_who & rendererFlagMask) {
			WLUsageFlagsBitIndex use_bit = wlGetUsageFlagsBitIndex(queuedGeoFrees[i]->use_flags);
			queuedGeoFrees[i]->free_for_who &= rendererFlagUnMask;
			memMonitorTrackUserMemory(gfxGeoGetMemoryName(queuedGeoFrees[i]->use_flags), 1, -queuedGeoFrees[i]->geo_memory_use, MM_FREE);
			gfx_state.debug.loaded_geo_count[use_bit]--;
			gfx_state.debug.loaded_geo_size[use_bit] -= queuedGeoFrees[i]->geo_memory_use;
			geoMemoryUsage -= queuedGeoFrees[i]->geo_memory_use;
			InterlockedExchangeAdd(&wl_geo_mem_usage.loadedVideo[queuedGeoFrees[i]->geo_mem_usage_bitindex], -queuedGeoFrees[i]->geo_memory_use);
			InterlockedExchangeAdd(&wl_geo_mem_usage.loadedVideoTotal, -queuedGeoFrees[i]->geo_memory_use);
		}
	}
	LeaveCriticalSection(&gfx_geo_critical_section);
}

// Should be safe to call from multiple threads.  Called from terrain atlasing thread.
void gfxFreeGeoRenderInfo(GeoRenderInfo *geo_render_info, bool free_data_only)
{
	if (!geo_render_info)
		return;

	if (free_data_only)
	{
		assert(geo_render_info->load_in_foreground);
	}
	else if (gfxGeoIsLoadingInBg(geo_render_info))
	{
		bool bIsInBG=false;

		// This model still has an LOD queued up to be generated, or it's still
		//   loading, but we're freeing it!  Let the queued load finish first!
		EnterCriticalSection(&gfx_geo_flags_critical_section);
		if (geo_render_info->is_being_processed_in_bg) {
			bIsInBG = true;
		} else {
			geo_render_info->is_canceled = true;
		}
		LeaveCriticalSection(&gfx_geo_flags_critical_section);

		if (bIsInBG)
		{
			memlog_printf(geoGetMemLog(), "Tried to queue free of GeoRenderInfo %p, but it's currently being processed, waiting and then freeing (frame %d).", geo_render_info, gfx_state.frame_count);
			// Wait for it to finish, then continue this function
			while(gfxGeoIsLoadingInBg(geo_render_info))
				Sleep(1);
		} else {
			// Was flagged as canceled above, add to free queue
			// Call the free callback *before* returning from this function, it might need the data the caller's about to free
			memlog_printf(geoGetMemLog(), "Queueing free of GeoRenderInfo %p, but it's currently queued for processing.  Canceling and queuing the free (frame %d).", geo_render_info, gfx_state.frame_count);
			geo_render_info->free_memory_callback(geo_render_info, geo_render_info->parent, geo_render_info->param);
			EnterCriticalSection(&gfx_geo_critical_section);
			geo_render_info->parent = NULL;
			eaPush(&queued_geo_render_info_frees, geo_render_info);
			LeaveCriticalSection(&gfx_geo_critical_section);
			return;
		}
	}

	if (geo_render_info->send_queued)
	{
		memlog_printf(geoGetMemLog(), "Tried to free GeoRenderInfo %p, but it's currently queued to send to the renderer, waiting and then freeing (frame %d).", geo_render_info, gfx_state.frame_count);
		EnterCriticalSection(&gfx_geo_critical_section);
		geo_render_info->parent = NULL;
		eaPush(&queued_geo_render_info_frees, geo_render_info);
		LeaveCriticalSection(&gfx_geo_critical_section);
		return;
	}

	memlog_printf(geoGetMemLog(), "Queueing free of GeoRenderInfo %p, handle %d on frame %d.", geo_render_info, geo_render_info->geo_handle_primary, gfx_state.frame_count);

	// Queue the freeing of GeoHandles
	if (geo_render_info->geo_loaded)
	{
		QueuedGeoFree *queued_free;
		EnterCriticalSection(&gfx_geo_critical_section);
		MP_CREATE(QueuedGeoFree, 16);

		if (geo_render_info->geo_handle_primary)
		{
			queued_free = MP_ALLOC(QueuedGeoFree);
			queued_free->free_for_who = gfx_state.allRenderersFlag; // should this be geo_render_info->geo_loaded instead?
			queued_free->geo_handle = geo_render_info->geo_handle_primary;
			queued_free->geo_memory_use = geo_render_info->geo_memory_use;
			queued_free->geo_mem_usage_bitindex = geo_render_info->geo_mem_usage_bitindex;
			queued_free->use_flags = geo_render_info->use_flags;
			eaPush(&queuedGeoFrees, queued_free);
		}

		geo_render_info->geo_loaded = 0;
		LeaveCriticalSection(&gfx_geo_critical_section);
	}

	gfxGeoDoneWithVertexObject(geo_render_info); // Release reference to vbo_data (may free async in thread)

	if (!free_data_only)
	{
		EnterCriticalSection(&gfx_geo_critical_section);
		InterlockedExchangeAdd(&wl_geo_mem_usage.loadedSystem[wlGetUsageFlagsBitIndex(geo_render_info->use_flags)], -(S32)sizeof(GeoRenderInfo));
		InterlockedExchangeAdd(&wl_geo_mem_usage.loadedSystemTotal, -(S32)sizeof(GeoRenderInfo));
		geo_render_info->geo_handle_primary = 0;
		MP_FREE(GeoRenderInfo, geo_render_info);
		LeaveCriticalSection(&gfx_geo_critical_section);
	}
}

void gfxGeoDoneWithVertexObject(GeoRenderInfo *geo_render_info)
{
	assert(!geo_render_info->send_queued);
	memlog_printf(geoGetMemLog(), "gfxGeoDoneWithVertexObject on handle %d on frame %d.", geo_render_info->geo_handle_primary, gfx_state.frame_count);
	geo_render_info->vbo_load_state = GEO_NOT_LOADED;
	if (geo_render_info->vbo_data_primary.data_ptr)
	{
		memrefDecrement(geo_render_info->vbo_data_primary.data_ptr);
		ZeroStruct(&geo_render_info->vbo_data_primary);
	}
}

static void gfxGeoSendToRenderer(GeoRenderInfo *geo_render_info)
{
	RdrGeometryData *geo_data;
	WLUsageFlagsBitIndex use_bit = wlGetUsageFlagsBitIndex(geo_render_info->use_flags);

	assert(geo_render_info);
	assert(geo_render_info->geo_mem_usage_bitindex);

	//////////////////////////////////////////////////////////////////////////
	// primary stream
	assert(geo_render_info->usage.bits[0] != RUSE_NONE);

	assert(geo_render_info->geo_handle_primary);

	memlog_printf(geoGetMemLog(), "Sending geometry with handle %d to renderer with flag %d on frame %d.", geo_render_info->geo_handle_primary, gfx_state.currentRendererFlag, gfx_state.frame_count);
	rdrMemlogPrintf(gfx_state.currentDevice->rdr_device, geoGetMemLog(), "Renderer with flag %d received geometry with handle %d for frame %d.", gfx_state.currentRendererFlag, geo_render_info->geo_handle_primary, gfx_state.frame_count);

	memrefIncrement(geo_render_info->vbo_data_primary.data_ptr); // Increment reference count
	geo_data = rdrStartUpdateGeometry(gfx_state.currentDevice->rdr_device, geo_render_info->geo_handle_primary, geo_render_info->usage, 0);
	geo_data->tri_count = geo_render_info->tri_count;
	geo_data->vert_count = geo_render_info->vert_count;
	geo_data->subobject_count = geo_render_info->subobject_count;
	geo_data->tris = geo_render_info->vbo_data_primary.tris;
	geo_data->vert_data = geo_render_info->vbo_data_primary.vert_data;
	geo_data->subobject_tri_bases = geo_render_info->vbo_data_primary.subobject_tri_bases;
	geo_data->subobject_tri_counts = geo_render_info->vbo_data_primary.subobject_tri_counts;
	if (geo_render_info->parent)
	{
		GeoRenderInfoDetails details = {0};
		geo_render_info->info_callback(geo_render_info, geo_render_info->parent, geo_render_info->param, &details);
		geo_data->debug_name = details.filename ? details.filename : details.name;
	}

	rdrEndUpdateGeometry(gfx_state.currentDevice->rdr_device);

	// Decrement reference count in other thread
	rdrFreeReferencedData(gfx_state.currentDevice->rdr_device, geo_render_info->vbo_data_primary.data_ptr);

	geo_render_info->geo_memory_use = geo_render_info->vbo_data_primary.data_size;
	memMonitorTrackUserMemory(gfxGeoGetMemoryName(geo_render_info->use_flags), 1, geo_render_info->geo_memory_use, MM_ALLOC);
	geoMemoryUsage += geo_render_info->geo_memory_use;
	InterlockedExchangeAdd(&wl_geo_mem_usage.loadedVideo[geo_render_info->geo_mem_usage_bitindex], geo_render_info->geo_memory_use);
	InterlockedExchangeAdd(&wl_geo_mem_usage.loadedVideoTotal, geo_render_info->geo_memory_use);

	geo_render_info->geo_loaded_timestamp = gfx_state.client_frame_timestamp;
	geo_render_info->geo_loaded |= gfx_state.currentRendererFlag;

	gfx_state.debug.loaded_geo_count[use_bit]++;
	gfx_state.debug.loaded_geo_size[use_bit] += geo_render_info->geo_memory_use;
}

bool gfxGeoIsLoaded(GeoRenderInfo *geo_render_info)
{
	geo_render_info->geo_last_used_timestamp = gfx_state.client_frame_timestamp; // Flags child/LOD model as being used
	return !!(geo_render_info->geo_loaded & gfx_state.currentRendererFlag);
}

typedef struct GfxGeoRendererQueue
{
	GeoRenderInfo **queuedGeoLoads;
	int geo_per_frame_quota;
	int geoLoadsThisFrame;
} GfxGeoRendererQueue;

static GfxGeoRendererQueue gfx_geo_rdr_queue = { NULL, 8, 0 };

// Limits how many goes can be sent to each renderer each frame.  -1 is unlimited, 0 will load no geos
AUTO_CMD_INT(gfx_geo_rdr_queue.geo_per_frame_quota, geo_per_frame_quota) ACMD_CMDLINE ACMD_CATEGORY(Performance);

int gfxGeoNumLoadsPending(void)
{
	int ret;
	EnterCriticalSection(&gfx_geo_critical_section);
	ret = eaSize(&gfx_geo_rdr_queue.queuedGeoLoads);
	LeaveCriticalSection(&gfx_geo_critical_section);
	return ret;
}

int gfxGeoQuotaPerFrame(void)
{
	return gfx_geo_rdr_queue.geo_per_frame_quota;
}

int gfxGeoSentThisFrame(void)
{
	return gfx_geo_rdr_queue.geoLoadsThisFrame;
}

static void sendToRenderer(int idx)
{
	GeoRenderInfo *geo_render_info;
	int i;

	if (idx < 0 || idx >= eaSize(&gfx_geo_rdr_queue.queuedGeoLoads))
		return;

	if (!gfx_state.currentDevice || !gfx_state.currentDevice->rdr_device->is_locked_nonthread)
		return;

	if (gfx_geo_rdr_queue.geo_per_frame_quota != -1 && !gfxLoadingIsWaiting() && !gfx_state.inEditor && gfx_geo_rdr_queue.geoLoadsThisFrame >= gfx_geo_rdr_queue.geo_per_frame_quota)
		return;

	geo_render_info = gfx_geo_rdr_queue.queuedGeoLoads[idx];

	if (geo_render_info->vbo_load_state == GEO_LOADED) {
		if (geo_render_info->geo_loading_for & gfx_state.currentRendererFlag)
		{
			for (i = eaSize(&queuedGeoFrees) - 1; i >= 0; --i) {
				if ((queuedGeoFrees[i]->geo_handle == geo_render_info->geo_handle_primary)
					&& queuedGeoFrees[i]->free_for_who & gfx_state.currentRendererFlag)
				{
					processQueuedFree(i, false);
				}
			}

			gfxGeoSendToRenderer(geo_render_info);
			geo_render_info->geo_loading_for &= ~gfx_state.currentRendererFlag;
			gfx_geo_rdr_queue.geoLoadsThisFrame++;
		}
		if (!(geo_render_info->geo_loading_for & gfx_state.allRenderersFlag)) {
			geo_render_info->send_queued = false;
			gfxGeoDoneWithVertexObject(geo_render_info); // Free data async, zero struct
			eaRemoveFast(&gfx_geo_rdr_queue.queuedGeoLoads, idx);
		}
	} else if (geo_render_info->vbo_load_state == GEO_LOAD_FAILED) {
		// Nothing to do with it, just free data and remove from list
		geo_render_info->send_queued = false;
		gfxGeoDoneWithVertexObject(geo_render_info); // Free data async, zero struct
		eaRemoveFast(&gfx_geo_rdr_queue.queuedGeoLoads, idx);
	}
}

static void CALLBACK gfxGeoSetupVertexObjectCallback(GeoRenderInfo *geo_render_info, void *unused, int send_to_renderer)
{
	bool bCanceled=false, bLoaded=false;
	int idx;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&gfx_geo_flags_critical_section);
		if (geo_render_info->is_canceled)
			bCanceled = true;
		else
			geo_render_info->is_being_processed_in_bg = 1;
	LeaveCriticalSection(&gfx_geo_flags_critical_section);
	
	if (!bCanceled) {
		bLoaded = geo_render_info->fill_callback(geo_render_info, geo_render_info->parent, geo_render_info->param);
		geo_render_info->free_memory_callback(geo_render_info, geo_render_info->parent, geo_render_info->param);
	} else {
		// Canceled.  Main thread freed the data.
	}

	EnterCriticalSection(&gfx_geo_flags_critical_section);
		geo_render_info->is_being_processed_in_bg = 0;
	LeaveCriticalSection(&gfx_geo_flags_critical_section);
	EnterCriticalSection(&gfx_geo_critical_section);
		idx = eaPush(&gfx_geo_rdr_queue.queuedGeoLoads, geo_render_info);
		geo_render_info->vbo_load_state = bLoaded ? GEO_LOADED : GEO_LOAD_FAILED;
		geo_render_info->send_queued = true;
		if (send_to_renderer) {
			sendToRenderer(idx);
		}
	LeaveCriticalSection(&gfx_geo_critical_section);
	PERFINFO_AUTO_STOP();
}

//gfxGeoSetupVertexObject ---> gfxGeoSetupVertexObjectCallback ---> 
			//fill_callback ---> gfxModelFillVertexBufferObject
			//sendToRenderer ---> gfxGeoSendToRenderer ---> rdrStartUpdateGeometry
// so this happens on model load, for example, right after gfxFillModelRenderInfo
bool gfxGeoSetupVertexObject(GeoRenderInfo *geo_render_info)
{
	// Model is not loaded if it gets into this function.
	assert(!(geo_render_info->geo_loaded & gfx_state.currentRendererFlag));
	assert(geo_render_info->usage.bits[0] != RUSE_NONE);
	assert(gfx_state.currentDevice && !gfx_state.currentDevice->rdr_device->is_locked_nonthread);

	if (geo_render_info->vbo_load_state == GEO_LOAD_FAILED)
	{
		return false;
	}

	if (geo_render_info->vbo_load_state == GEO_NOT_LOADED || 
		geo_render_info->vbo_load_state == 0)
	{
		// Start async load
		geo_render_info->geo_loading_for |= gfx_state.currentRendererFlag;
		geo_render_info->vbo_load_state = GEO_LOADING;
		if (!geo_render_info->geo_handle_primary)
			geo_render_info->geo_handle_primary = rdrGenGeoHandle();
		if (geo_render_info->load_in_foreground) {
			gfxGeoSetupVertexObjectCallback(geo_render_info, NULL, 1);
		} else {
#ifdef DISABLE_ASYNC_SETUPVBO
			gfxGeoSetupVertexObjectCallback(geo_render_info, NULL, 0);
#else
			//memlog_printf(geoGetMemLog(), "Requesting background load for handle %d on frame %d.", geo_render_info->geo_handle_primary, gfx_state.frame_count);
			geoRequestBackgroundExec(gfxGeoSetupVertexObjectCallback, NULL, geo_render_info, NULL, 0, FILE_LOW_PRIORITY);
#endif
		}
	}
	if (geo_render_info->vbo_load_state == GEO_LOADED)
	{
		// Sent to card (async, but guaranteed to be done before drawing time)
		geo_render_info->geo_loading_for |= gfx_state.currentRendererFlag;
		if (gfx_state.currentDevice && !gfx_state.currentDevice->rdr_device->is_locked_nonthread)
		{
			memlog_printf(geoGetMemLog(), "Assuming background load complete for handle %d on frame %d.", geo_render_info->geo_handle_primary, gfx_state.frame_count);
			return true;
		}
	}
	else if (geo_render_info->vbo_load_state == GEO_LOADING)
	{
		geo_render_info->geo_loading_for |= gfx_state.currentRendererFlag;
	}

	return !!(geo_render_info->geo_loaded & gfx_state.currentRendererFlag);
}

static int bHasGeoQueueErroredOnLength = 0;
static const int GEO_QUEUE_LIMIT = 1000;

typedef struct GfxQueueTrackerEntry {
	char *name;
	unsigned int count;
} GfxQueueTrackerEntry;

static int gfxQueueTrackerEntryCompare(const GfxQueueTrackerEntry **a, const GfxQueueTrackerEntry **b) {
	if((*b)->count == (*a)->count) {
		return strcmp((*b)->name, (*a)->name);
	}
	return (*b)->count - (*a)->count;
}

static void gfxDestroyRenderQueueNames(GfxQueueTrackerEntry ***eaEntries) {

	int i;
	for(i = 0; i < eaSize(eaEntries); i++) {
		free((*eaEntries)[i]->name);
		free((*eaEntries)[i]);
	}
	eaDestroy(eaEntries);
}

static void gfxGetRenderQueueNames(GfxQueueTrackerEntry ***models, GfxQueueTrackerEntry ***vertexColors) {

	int i;
	GfxQueueTrackerEntry **queueTracker = NULL;

	for(i = 0; i < eaSize(&gfx_geo_rdr_queue.queuedGeoLoads); i++) {

		int j;
		GeoRenderInfo *geo_render_info = gfx_geo_rdr_queue.queuedGeoLoads[i];
		const char *name = NULL;
		bool foundEntry = false;
		GeoRenderInfoDetails details = {0};
		// Only issue info callback if not cancelled
		if (geo_render_info->parent)
			geo_render_info->info_callback(geo_render_info, geo_render_info->parent, geo_render_info->param, &details);

		if(details.filename) {
			name = details.filename;
		} else if(details.name) {
			name = details.name;
		} else {
			name = "unknown";
		}

		for(j = 0; j < eaSize(&queueTracker); j++) {
			if(!strcmp(queueTracker[j]->name, name)) {
				foundEntry = true;
				queueTracker[j]->count++;
				break;
			}
		}

		if(!foundEntry) {
			if (geo_render_info->parent) {
				GfxQueueTrackerEntry *newEntry = calloc(sizeof(GfxQueueTrackerEntry), 1);
				newEntry->name = strdup(name);
				newEntry->count = 1;
				eaPush(&queueTracker, newEntry);
			}
		}
	}

	if(queueTracker) {
		qsort(
			queueTracker, eaSize(&queueTracker),
			sizeof(GfxQueueTrackerEntry*),
			gfxQueueTrackerEntryCompare);
	}

	for(i = 0; i < eaSize(&queueTracker); i++) {
		if(strStartsWith(queueTracker[i]->name, "Vertexcolors:")) {
			eaPush(vertexColors, queueTracker[i]);
		} else {
			eaPush(models, queueTracker[i]);
		}
	}
	eaDestroy(&queueTracker);
}

void gfxGeoOncePerFramePerDevice(void)
{
	int i, max_geos;
	gfx_geo_rdr_queue.geoLoadsThisFrame = 0;

	EnterCriticalSection(&gfx_geo_critical_section);
	max_geos = eaSize(&gfx_geo_rdr_queue.queuedGeoLoads);
	for (i=max_geos-1; i>=0; i--) {
		sendToRenderer(i);
	}
	LeaveCriticalSection(&gfx_geo_critical_section);

	if (max_geos > GEO_QUEUE_LIMIT && !bHasGeoQueueErroredOnLength && !gbMakeBinsAndExit)
	{

		GfxQueueTrackerEntry **eaModels = NULL;
		GfxQueueTrackerEntry **eaVertexLights = NULL;
		char *estrDetails = NULL;

		gfxGetRenderQueueNames(&eaModels, &eaVertexLights);

		estrConcatf(&estrDetails, "Total unique models: %d\n", eaSize(&eaModels));
		estrConcatf(&estrDetails, "Top models:\n");
		for(i = 0; i < eaSize(&eaModels) && i < 10; i++) {
			estrConcatf(&estrDetails, "%5d : %s\n", eaModels[i]->count, eaModels[i]->name);
		}

		estrConcatf(&estrDetails, "Total unique vertex lights: %d\n", eaSize(&eaVertexLights));
		estrConcatf(&estrDetails, "Top vertex lights:\n");
		for(i = 0; i < eaSize(&eaVertexLights) && i < 10; i++) {
			estrConcatf(&estrDetails, "%5d : %s\n", eaVertexLights[i]->count, eaVertexLights[i]->name);
		}

		bHasGeoQueueErroredOnLength = 1;
		ErrorDetailsf("%s", estrDetails);
		Errorf("Renderer geometry queue is over %d entries. This may indicate overly-rapid "
			"requests to load geo. See geo_memlog for possible culprits, which might include vertex lighting "
			"data recreated every frame in headshots. Please notify the Graphics Team if this fires and "
			" the circumstances where it has happened. See JIRA [STO-27538].", GEO_QUEUE_LIMIT);

		estrDestroy(&estrDetails);
		gfxDestroyRenderQueueNames(&eaModels);
		gfxDestroyRenderQueueNames(&eaVertexLights);
	}
}

void gfxGeoOncePerFrame(void)
{
}

U32 gfxGetLoadedGeoCount(U32 *counts, WLUsageFlags flags_for_total)
{
	int i;
	U32 total = 0;
	for (i = 0; i < WL_FOR_MAXCOUNT; ++i)
	{
		counts[i] = gfx_state.debug.loaded_geo_count[i];
		if ((1 << i) & flags_for_total)
			total += counts[i];
	}
	return total;
}

U32 gfxGetLoadedGeoSize(U32 *sizes, WLUsageFlags flags_for_total)
{
	int i;
	U32 total = 0;
	for (i = 0; i < WL_FOR_MAXCOUNT; ++i)
	{
		sizes[i] = gfx_state.debug.loaded_geo_size[i];
		if ((1 << i) & flags_for_total)
			total += sizes[i];
	}
	return total;
}



void gfxGeoGetMemUsageOld(GeoMemUsage *usage, WLUsageFlags flags_for_total)
{
	// TODO: Smarter
	usage->loadedVideoTotal = gfxGetLoadedGeoSize(usage->loadedVideo, flags_for_total);
	usage->recentVideoTotal = gfxGetLoadedGeoSize(usage->recentVideo, flags_for_total);
	usage->countVideoTotal = gfxGetLoadedGeoCount(usage->countVideo, flags_for_total);
	usage->loadedSystemTotal = worldGetLoadedModelSize(usage->loadedSystem, flags_for_total);
	usage->countSystemTotal = worldGetLoadedModelCount(usage->countSystem, flags_for_total);
}

const char *gfxModelLODGetName(ModelLOD *model)
{
	if (model->debug_name)
		return model->debug_name;
	if (model->lod_index == 0)
	{
		// nothing special
		model->debug_name = model->model_parent->name;
	} else {
		const char *name = model->model_parent->name;
		size_t namelen = strlen(name);
		if (namelen > 3 && strStartsWith(&name[namelen-3], "_L")) {
			// a hand-build LOD, most likely
			// do nothing with the name
		} else {
			char namebuf[1024];
			sprintf(namebuf, "%s_AutoL%d", name, model->lod_index);
			model->debug_name = allocAddFilename(namebuf);
		}
	}
	return model->debug_name;
}

// Only looks at geometry which does not have a geo_render_info yet.
static bool gfxGeoGetMemUsageModelCallback(ModelLOD *model, intptr_t userData)
{
	GeoRenderInfo *geo_render_info=model->geo_render_info;
	if (!geo_render_info) {
		GeoMemUsage *usage = (GeoMemUsage*)userData;
		int sys_mem=0;
		bool bDoTotal;
		WLUsageFlags use_category = modelGetUseFlags(model->model_parent);
		if (!use_category)
			use_category = WL_FOR_NOTSURE;


		bDoTotal = !!(use_category & usage->flags_for_total);

		sys_mem = modelLODGetBytesSystem(model);
		if (bDoTotal) {
			if (modelLODIsLoaded(model) || modelLODHasUnpacked(model))
				usage->countSystemTotal++;
			usage->loadedSystemTotal += sys_mem;
		}
		if (sys_mem) {
			int index = 0;
			while (use_category) {
				if (use_category & 1) {
					usage->countSystem[index]++;
					usage->loadedSystem[index] += sys_mem;
				}
				index++;
				use_category >>=1;
			}
		}
	}
	return true;
}

static void gfxGeoGetMemUsageCallback(MemoryPool pool, GeoRenderInfo *geo_render_info, GeoMemUsage *usage)
{
	GeoRenderInfoDetails details = {0};
	int vid_mem=0, sys_mem=0;
	bool bDoTotal;
	bool bRecent;
	WLUsageFlags use_category = geo_render_info->use_flags;

	if (geo_render_info->is_canceled || !geo_render_info->parent)
		return;

	if (!use_category)
		use_category = WL_FOR_NOTSURE;

	bDoTotal = !!(use_category & usage->flags_for_total);
	bRecent = gfx_state.client_frame_timestamp - geo_render_info->geo_last_used_timestamp < usage->recent_time;

	sys_mem = gfxGetGetSysMemoryUsage(&details, geo_render_info);
	if (bDoTotal) {
		usage->countSystemTotal++;
		usage->loadedSystemTotal += sys_mem;
	}

	if (geo_render_info->geo_loaded) {
		vid_mem = geo_render_info->geo_memory_use;
		if (bDoTotal) {
			usage->countVideoTotal++;
			usage->loadedVideoTotal += vid_mem;
			if (bRecent)
				usage->recentVideoTotal += vid_mem;
		}
	}
	if (vid_mem || sys_mem) {
		int index = 0;
		while (use_category) {
			if (use_category & 1) {
				if (vid_mem)
					usage->countVideo[index]++;
				if (sys_mem)
					usage->countSystem[index]++;
				usage->loadedVideo[index] += vid_mem;
				usage->loadedSystem[index] += sys_mem;
				if (bRecent)
					usage->recentVideo[index] += vid_mem;
			}
			index++;
			use_category >>=1;
		}
	}
}

#define RECENT_SECONDS 1
void gfxGeoGetMemUsage(GeoMemUsage *usage, WLUsageFlags flags_for_total)
{
	ZeroStruct(usage);

	usage->recent_time = timerCpuSpeed()*RECENT_SECONDS;
	usage->flags_for_total = flags_for_total;
	if (!gbNoGraphics) {
		modelForEachModelLOD(gfxGeoGetMemUsageModelCallback, (intptr_t)usage, true);
		if (MP_NAME(GeoRenderInfo)) {
			PERFINFO_AUTO_START("mpForEachAllocation(GeoRenderInfo)", 1);
			EnterCriticalSection(&gfx_geo_critical_section);
			mpForEachAllocation(MP_NAME(GeoRenderInfo), gfxGeoGetMemUsageCallback, usage);
			LeaveCriticalSection(&gfx_geo_critical_section);
			PERFINFO_AUTO_STOP();
		}
	}
}


typedef struct GeoMemUsageDetailed
{
	int count;
	GeoMemUsageEntry ***list;
	WLUsageFlags flags;
	U32 recent_time;
	const char *textureName;
	bool currentSceneOnly;
	bool systemOnly;
} GeoMemUsageDetailed;


static bool gfxGeoGetMemUsageDetailedCallback(ModelLOD *model, intptr_t userData)
{
	GeoMemUsageDetailed *usage = (GeoMemUsageDetailed*)userData;
	GeoRenderInfo *geo_render_info = model->geo_render_info;
	GeoMemUsageEntry *entry;
	int vid_mem=0, sys_mem_packed=0, sys_mem_unpacked=0;
	int countInScene;
	if (usage->systemOnly && geo_render_info)
		return true;
	if (!(modelGetUseFlags(model->model_parent) & usage->flags))
		return true;
	countInScene = gfxGeoGetLastUsedCount(geo_render_info, false);
	if (!countInScene && usage->currentSceneOnly)
		return true;
	if (usage->textureName) {
		int i;
		bool good=false;
		if (!modelLODIsLoaded(model))
			return true;
		for (i=0; i<model->data->tex_count; i++) {
			if (gfxMaterialUsesTexture(model->materials[i], usage->textureName))
				good = true;
		}
		if (!good)
			return true;
	}
	sys_mem_packed = modelLODGetBytesCompressed(model);
	sys_mem_unpacked = modelLODGetBytesUncompressed(model);
	if (geo_render_info) {
		if (geo_render_info->geo_loaded) {
			vid_mem = geo_render_info->geo_memory_use;
		}
		sys_mem_packed += sizeof(GeoRenderInfo);
	}
	if (vid_mem || sys_mem_packed || sys_mem_unpacked) {
		if (usage->count < eaSize(usage->list)) {
			entry = eaGet(usage->list, usage->count);
		} else {
			entry = StructCreate(parse_GeoMemUsageEntry);
			eaPush(usage->list, entry);
		}
		usage->count++;
		entry->filename = modelIsTemp(model->model_parent)?"":model->model_parent->header->filename;
		entry->name = gfxModelLODGetName(model);
		entry->sys_mem_packed = sys_mem_packed;
		entry->sys_mem_unpacked = sys_mem_unpacked;
		entry->vid_mem = vid_mem;
		entry->total_mem = sys_mem_packed + sys_mem_unpacked + vid_mem;
		entry->tris = model->tri_count;
		entry->verts = model->vert_count;
		entry->recent = geo_render_info && ((gfx_state.client_frame_timestamp - geo_render_info->geo_last_used_timestamp < usage->recent_time));
		entry->countInScene = countInScene;
		entry->uniqueInScene = gfxGeoGetLastUsedCount(geo_render_info, true);
		entry->sub_objects = SAFE_MEMBER(model->data, tex_count);
		entry->trisInScene = (geo_render_info && geo_render_info->subobject_count) ? (geo_render_info->tri_count * (countInScene / geo_render_info->subobject_count)) : 0;
		entry->shared = !!(modelGetUseFlags(model->model_parent) & ~usage->flags);
		entry->geo_render_info = geo_render_info;
		entry->uv_density = model->uv_density;
		if (model->model_parent && model->model_parent->lod_info && GET_REF(model->model_parent->lod_info->lod_template))
			entry->lod_template_name = GET_REF(model->model_parent->lod_info->lod_template)->template_name;
	}
	return true;
}

static void gfxGeoGetDrawCallsDetailedCallback(MemoryPool pool, GeoRenderInfo *geo_render_info, GeoMemUsageDetailed *usage);


void gfxGeoGetMemUsageDetailed(WLUsageFlags flags, GeoMemUsageEntry ***entries)
{
	GeoMemUsageDetailed usage = {0};
	if (!*entries)
		eaCreate(entries);
	usage.flags = flags;
	usage.list = entries;
	usage.count = 0;
	usage.recent_time = timerCpuSpeed()*RECENT_SECONDS;
	usage.systemOnly = true;
	modelForEachModelLOD(gfxGeoGetMemUsageDetailedCallback, (intptr_t)&usage, true);
	usage.systemOnly = false;
	if (MP_NAME(GeoRenderInfo)) {
		EnterCriticalSection(&gfx_geo_critical_section);
		mpForEachAllocation(MP_NAME(GeoRenderInfo), gfxGeoGetDrawCallsDetailedCallback, &usage);
		LeaveCriticalSection(&gfx_geo_critical_section);
	}

	assert(eaSize(usage.list) >= usage.count);
	while (eaSize(usage.list) != usage.count)
		StructDestroy(parse_GeoMemUsageEntry, eaPop(usage.list));
}

void gfxGeoGetWhoUsesTexture(const char *textureName, bool currentSceneOnly, GeoMemUsageEntry ***entries)
{
	GeoMemUsageDetailed usage = {0};
	if (!*entries)
		eaCreate(entries);
	usage.flags = ~0;
	usage.textureName = textureName;
	usage.currentSceneOnly = currentSceneOnly;
	usage.list = entries;
	usage.count = 0;
	usage.recent_time = timerCpuSpeed()*RECENT_SECONDS;
	modelForEachModelLOD(gfxGeoGetMemUsageDetailedCallback, (intptr_t)&usage, true);
	assert(eaSize(usage.list) >= usage.count);
	while (eaSize(usage.list) != usage.count)
		StructDestroy(parse_GeoMemUsageEntry, eaPop(usage.list));
}

void gfxGeoSelectByRenderInfo(GeoRenderInfo *geo_render_info)
{
	if (geo_render_info) {
		rdrDrawListSelectByGeoHandle(geo_render_info->geo_handle_primary);
	} else {
		rdrDrawListSelectByGeoHandle(0);
	}
}


typedef struct GeoShowUsageData
{
	const char *search;
	int count;
	FILE *file;
} GeoShowUsageData;

static void gfxGeoShowUsageFunc(MemoryPool pool, GeoRenderInfo *geo_render_info, GeoShowUsageData *data)
{
	U32 countInScene;

	if (geo_render_info->is_canceled)
		return;

	countInScene = gfxGeoGetLastUsedCount(geo_render_info, false);
	if (countInScene) {
		GeoRenderInfoDetails details = {0};
		geo_render_info->info_callback(geo_render_info, geo_render_info->parent, geo_render_info->param, &details);
		if (!data->search || details.name && strstri(details.name, data->search) || details.filename && strstri(details.filename, data->search)) {
			data->count += countInScene;
			conPrintf("%d  %s/%s", countInScene, details.filename?details.filename:"", details.name?details.name:"");
			if (data->file) {
				fprintf(data->file, "%d  %s/%s (0x%08p)\n", countInScene, details.filename?details.filename:"", details.name?details.name:"", geo_render_info);
			}
		}
	}
}

AUTO_COMMAND ACMD_NAME(geoShowUsage);
void gfxGeoShowUsage(const char *search)
{
	GeoShowUsageData data = {0};
	data.search = search;
	data.file = fopen("C:\\geousage.txt", "w");
	conPrintf("Geometry Draw Calls:");
	if (MP_NAME(GeoRenderInfo)) {
		EnterCriticalSection(&gfx_geo_critical_section);
		mpForEachAllocation(MP_NAME(GeoRenderInfo), gfxGeoShowUsageFunc, &data);
		LeaveCriticalSection(&gfx_geo_critical_section);
	}
	conPrintf("Total: %d object draw calls", data.count);
	if (data.file)
		fclose(data.file);
}

static void gfxGeoGetDrawCallsDetailedCallback(MemoryPool pool, GeoRenderInfo *geo_render_info, GeoMemUsageDetailed *usage)
{
	GeoRenderInfoDetails details = {0};
	GeoMemUsageEntry *entry;
	int vid_mem=0;
	int countInScene;

	if (geo_render_info->is_canceled)
		return;

	assert(!usage->textureName); // Not handled
	assert(!usage->systemOnly); // This only sees thigns also in video memory

	if (!(geo_render_info->use_flags & usage->flags))
		return;
	countInScene = gfxGeoGetLastUsedCount(geo_render_info, false);
	if (usage->currentSceneOnly && !countInScene)
		return;

	details.sys_mem_packed = sizeof(GeoRenderInfo);
	details.sys_mem_unpacked = 0;
	geo_render_info->info_callback(geo_render_info, geo_render_info->parent, geo_render_info->param, &details);

	if (geo_render_info->geo_loaded) {
		vid_mem = geo_render_info->geo_memory_use;
	}
	if (usage->count < eaSize(usage->list)) {
		entry = eaGet(usage->list, usage->count);
	} else {
		entry = StructCreate(parse_GeoMemUsageEntry);
		eaPush(usage->list, entry);
	}
	usage->count++;
	entry->filename = details.filename;
	entry->name = details.name;
	entry->sys_mem_packed = details.sys_mem_packed;
	entry->sys_mem_unpacked = details.sys_mem_unpacked;
	entry->vid_mem = vid_mem;
	entry->total_mem = details.sys_mem_packed + details.sys_mem_unpacked + vid_mem;
	entry->tris = geo_render_info->tri_count;
	entry->verts = geo_render_info->vert_count;
	entry->recent = gfx_state.client_frame_timestamp - geo_render_info->geo_last_used_timestamp < usage->recent_time;
	entry->countInScene = countInScene;
	entry->uniqueInScene = gfxGeoGetLastUsedCount(geo_render_info, true);
	entry->trisInScene = geo_render_info->subobject_count ? (geo_render_info->tri_count * (countInScene / geo_render_info->subobject_count)) : 0;
	entry->sub_objects = geo_render_info->subobject_count;
	entry->far_dist = round(details.far_dist);
	entry->shared = !!(geo_render_info->use_flags & ~usage->flags);
	entry->geo_render_info = geo_render_info;
	entry->uv_density = details.uv_density;
	entry->lod_template_name = details.lod_template_name;
}

void gfxGeoGetDrawCallsDetailed(WLUsageFlags flags, GeoMemUsageEntry ***entries)
{
	GeoMemUsageDetailed usage = {0};
	if (!*entries)
		eaCreate(entries);
	usage.flags = flags;
	usage.list = entries;
	usage.count = 0;
	usage.recent_time = timerCpuSpeed()*RECENT_SECONDS;
	usage.currentSceneOnly = true;
	if (MP_NAME(GeoRenderInfo)) {
		EnterCriticalSection(&gfx_geo_critical_section);
		mpForEachAllocation(MP_NAME(GeoRenderInfo), gfxGeoGetDrawCallsDetailedCallback, &usage);
		LeaveCriticalSection(&gfx_geo_critical_section);
	}
	assert(eaSize(usage.list) >= usage.count);
	while (eaSize(usage.list) != usage.count)
		StructDestroy(parse_GeoMemUsageEntry, eaPop(usage.list));
}
