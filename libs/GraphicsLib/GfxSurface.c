#include "GraphicsLibPrivate.h"
#include "earray.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

__forceinline static bool paramsEqual(const RdrSurfaceParams *params1, const RdrSurfaceParams *params2, RdrSurfaceFlags ignoreFlags)
{
	int i, buffers;

	if ((params1->flags & ~ignoreFlags) != (params2->flags & ~ignoreFlags))
		return false;
	if (params1->width != params2->width)
		return false;
	if (params1->height != params2->height)
		return false;
	if (params1->desired_multisample_level != params2->desired_multisample_level)
		return false;
	if (params1->required_multisample_level != params2->required_multisample_level)
		return false;
	if (params1->stencil_bits != params2->stencil_bits)
		return false;
	if (params1->depth_bits != params2->depth_bits)
		return false;
	if (params1->stereo_option != params2->stereo_option)
		return false;

	if (params1->flags & SF_MRT4)
		buffers = 4;
	else if (params1->flags & SF_MRT2)
		buffers = 2;
	else
		buffers = 1;

	for (i = 0; i < buffers; ++i)
	{
		if (params1->buffer_types[i] != params2->buffer_types[i])
			return false;
		if (params1->buffer_default_flags[i] != params2->buffer_default_flags[i])
			return false;
	}

	return true;
}

GfxTempSurface *gfxGetTempSurface(const RdrSurfaceParams *params)
{
	GfxTempSurface *available_surface = NULL;
	int i;

	for (i = 0; i < eaSize(&gfx_state.currentDevice->temp_buffers); ++i)
	{
		GfxTempSurface *surface = gfx_state.currentDevice->temp_buffers[i];
		if (surface->in_use)
			continue;
		if (!paramsEqual(&surface->creation_params, params, params->ignoreFlags))
			continue;
		if (!surface->surface)	// feature isn't really supported, most likely a portable device with a dual video card (Like an Intel card switching with an Nvidia card).
			return NULL;
		available_surface = surface;
		break;
	}

	if (!available_surface)
	{
		RdrSurface *surface = rdrCreateSurface(gfx_state.currentDevice->rdr_device, params);
		if (!surface)		// Did I attempt to create a surface using an unsupported feature?
			return NULL;	// Yes, so bail.
		available_surface = calloc(1, sizeof(GfxTempSurface));
		available_surface->surface = surface;
		if (gfx_state.debug.echoTempSurfaceCreation)
			printf("Creating new temp surface %dx%d (%s)\n", params->width, params->height, params->name);
		available_surface->creation_params = *params;
		eaPush(&gfx_state.currentDevice->temp_buffers, available_surface);
	}

	available_surface->in_use = true;
	rdrRenameSurface(gfx_state.currentDevice->rdr_device, available_surface->surface, params->name);
	available_surface->last_used_frame = gfx_state.currentDevice->per_device_frame_count;

	return available_surface;
}

void gfxReleaseTempSurfaceEx(GfxTempSurface *surface)
{
	if (surface)
	{
		surface->in_use = false;
		surface->last_used_frame = gfx_state.currentDevice->per_device_frame_count;
	}
}

void gfxMarkTempSurfaceUsed(GfxTempSurface *surface)
{
	surface->last_used_frame = gfx_state.currentDevice->per_device_frame_count;
}

// Call this to after destroying a RdrSurface, if you did not use gfxSurfaceDestroy.
void gfxOnSurfaceDestroy(const RdrSurface *surface)
{
	if (gfx_state.currentSurface == surface->surface && gfx_state.currentDevice)
	{
		gfxUnsetActiveSurface(gfx_state.currentDevice->rdr_device);
		gfxSetActiveSurface(rdrGetPrimarySurface(gfx_state.currentDevice->rdr_device));
	}
}

// Use this to destroy surfaces as it updates GraphicsLib state.
void gfxSurfaceDestroy(RdrSurface *surface)
{
	gfxOnSurfaceDestroy(surface);
	rdrSurfaceDestroy(surface);
}


void gfxTempSurfaceDestroy(GfxPerDeviceState *device_state, int i)
{
	GfxTempSurface *surface = device_state->temp_buffers[i];
	if (gfx_state.debug.echoTempSurfaceCreation)
		printf("Freeing temp surface %dx%d (%s)\n", surface->creation_params.width, surface->creation_params.height, surface->creation_params.name);
	eaRemoveFast(&device_state->temp_buffers, i);
	gfxSurfaceDestroy(surface->surface);
	free(surface);
}

void gfxTempSurfaceOncePerFramePerDevice(void)
{
	int i;

	PERFINFO_AUTO_START_FUNC_PIX();

	for (i = eaSize(&gfx_state.currentDevice->temp_buffers) - 1; i >= 0; --i)
	{
		GfxTempSurface *surface = gfx_state.currentDevice->temp_buffers[i];

		if (!surface->in_use &&
			gfx_state.currentDevice->per_device_frame_count - surface->last_used_frame >
			((surface->creation_params.width <= 64 && surface->creation_params.height <= 64) ?
				gfx_state.surface_frames_unused_max_lowres :
				gfx_state.surface_frames_unused_max)
			)
		{
			gfxTempSurfaceDestroy(gfx_state.currentDevice, i);
		}
	}

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

void gfxFreeTempSurfaces(GfxPerDeviceState *device_state)
{
	int i;

	for (i = eaSize(&device_state->temp_buffers) - 1; i >= 0; --i)
	{
		gfxTempSurfaceDestroy(device_state, i);
	}

	eaDestroy(&device_state->temp_buffers);
}

void gfxFreeSpecificSizedTempSurfaces(GfxPerDeviceState *device_state, U32 size[2], bool freeMultiples)
{
	int i;

	if (!device_state)
		return; // Called during startup/command line parsing, probably

	for (i = eaSize(&device_state->temp_buffers) - 1; i >= 0; --i)
	{
		GfxTempSurface *surface = device_state->temp_buffers[i];

		if (surface->in_use)
		{
			// Shouldn't happen?
		} else {
			int mult;
			bool bFreeMe = false;
			for (mult=0; mult <= (freeMultiples?6:0); mult++)
			{
				U32 newsize[2];
				scaleVec2(size, 1 << mult, newsize);
				if (surface->creation_params.width == newsize[0] && surface->creation_params.height == newsize[1])
					bFreeMe = true;
				scaleVec2(size, 1.f/(1<<mult), newsize);
				if (ABS_UNS_DIFF(surface->creation_params.width, newsize[0])<=1 && ABS_UNS_DIFF(surface->creation_params.height, newsize[1])<=1)
					bFreeMe = true;
			}
			if (bFreeMe)
				gfxTempSurfaceDestroy(device_state, i);
			else {
				if (gfx_state.debug.echoTempSurfaceCreation)
					printf("Not freeing surface %dx%d (%s)\n", surface->creation_params.width, surface->creation_params.height, surface->creation_params.name);
			}
		}
	}
}
