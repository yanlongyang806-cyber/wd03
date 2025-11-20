#include "GfxModelUnload.h"
#include "GfxModel.h"
#include "GfxGeo.h"
#include "sysutil.h"
#include "memlog.h"


#define MIN_ALLOWED_GEO_MEMORY 16*1024
#define FORCE_GEO_UNLOAD_CHECK (1000*MIN(10, geo_seconds_before_unload)) // in milliseconds

// #define DEBUG_MODEL_UNLOADING // Force models to get unloaded quickly


static int geo_seconds_before_unload = -1; // Number of seconds a model must not have been drawn before it can be unloaded
static U32 geo_memory_allowed=0;
static U32 geo_unload_last_ran_timestamp;
static U32 geo_unload_last_ran_size;

// Sets the timeout in seconds before unloading a piece of geometry
AUTO_CMD_INT(geo_seconds_before_unload, geo_seconds_before_unload) ACMD_CMDLINE ACMD_CATEGORY(Debug);

void gfxGeoDetermineAllowedMemory(void)
{
	unsigned long ulAvail, ulMax;
	long avail, max, remaining;
	getPhysicalMemory(&ulMax, &ulAvail);
	max = ulMax / 1024;
	avail = ulAvail / 1024;

	if (max <=256*1024) { // 256MB system or less
		if (geo_seconds_before_unload==-1)
			geo_seconds_before_unload = 60;
		remaining = 24*1024;
	} else if (max <=384*1024) { // 384M
		if (geo_seconds_before_unload==-1)
			geo_seconds_before_unload = 80;
		remaining = 24*1024;
	} else if (max <=512*1024) { // 512M or XBox 360
		if (geo_seconds_before_unload==-1)
			geo_seconds_before_unload = 160;
		remaining = 32*1024;
	} else { // Over 512MB (768MB+) of RAM
		if (geo_seconds_before_unload==-1)
			geo_seconds_before_unload = 160;
		remaining = 64*1024;
	}
	if (remaining < MIN_ALLOWED_GEO_MEMORY) {
		remaining = MIN_ALLOWED_GEO_MEMORY;
	}
	geo_memory_allowed = remaining*1024;

#ifdef DEBUG_MODEL_UNLOADING
	geo_memory_allowed = 1;
	geo_seconds_before_unload = 10;
#endif
}

static int geo_frees_this_frame=0;
static U32 geo_ticks_before_unload;
static U32 geo_ticks_before_preview_internal_unload;

static bool gfxGeoUnloadCallback(ModelLOD *model, intptr_t userdata)
{
	GeoRenderInfo *geo_render_info;

	// Check for unloading video data
	if ((geo_render_info=model->geo_render_info) && geo_render_info->geo_loaded)
	{
		U32 ticks_before_unload;

		if (model->model_parent->use_flags & WL_FOR_PREVIEW_INTERNAL)
		{
			ticks_before_unload = geo_ticks_before_preview_internal_unload;
		}
		else
		{
			ticks_before_unload = geo_ticks_before_unload;
		}
		
		if ((U32)(gfx_state.client_frame_timestamp - geo_render_info->geo_last_used_timestamp) > ticks_before_unload)
		{
			memlog_printf(geoGetMemLog(), "%u: Unloading model geo_render_info %s/%s, age of %5.2f, cat %d", gfx_state.client_frame_timestamp, model->model_parent->header->filename, model->model_parent->name, (float)(gfx_state.client_frame_timestamp - geo_render_info->geo_last_used_timestamp)/timerCpuSpeed(), geo_render_info->use_flags);
			// Free this geo_render_info
			gfxModelLODDestroyCallback(model);
			// Packed data may also immediately be freed in WorldLib
			geo_frees_this_frame++;
		}
	}

	return true;
}

void gfxGeoUnloadCheck(void)
{
	static bool inited=false;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if (!inited) {
		gfxGeoDetermineAllowedMemory();
		inited = true;
	}

	if ((timerCpuMs() - geo_unload_last_ran_timestamp > (U32)FORCE_GEO_UNLOAD_CHECK) ||
		(geoMemoryUsage > geo_memory_allowed && geo_unload_last_ran_size != geoMemoryUsage))
	{
		// Do geo unload check
		geo_frees_this_frame = 0;
		geo_ticks_before_unload = geo_seconds_before_unload * timerCpuSpeed();
		geo_ticks_before_preview_internal_unload = 0.1 * timerCpuSpeed(); //< but really, it won't happen more often then FORCE_GEO_UNLOAD_CHECK
		modelForEachModelLOD(gfxGeoUnloadCallback, 0, false);

		geo_unload_last_ran_size = geoMemoryUsage;
		geo_unload_last_ran_timestamp = timerCpuMs();
	}
	PERFINFO_AUTO_STOP();
}

void gfxGeoUnloadAllNotUsedThisFrame(void)
{
	geo_frees_this_frame = 0;
	geo_ticks_before_unload = 1;
	modelForEachModelLOD(gfxGeoUnloadCallback, 0, false);
	// Unload packed data as well
	modelUnloadCheck(1);
}
