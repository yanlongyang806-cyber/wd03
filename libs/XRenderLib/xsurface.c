#include "EventTimingLog.h"

#include "xsurface.h"
#include "rt_xsurface.h"
#include "memlog.h"
#include "nvapi_wrapper.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

static void rxbxDestroySurface(RdrSurface *surface)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)surface->device;
	RdrSurfaceDX *xsurface = (RdrSurfaceDX *)surface;
	bool bNeedQueue = !xdevice->device_base.is_locked_nonthread;
	if (xsurface->type == SURF_PRIMARY)
		return;

	if (!rdr_state.disableSurfaceLifetimeLog)
		memlog_printf(NULL, "rxbxDestroySurface(%p) %s", surface, surface->params_nonthread.name);

	surface->destroyed_nonthread = true;

	baDestroy(surface->used_snapshot_indices);

	// Check to see if we have any other queued commands for this surface
	FOR_EACH_IN_EARRAY(xdevice->device_base.queued_commands, QueuedRenderCmd, cmd)
	{
		if (cmd->cmd == RXBXCMD_SURFACE_INIT)
		{
			RxbxSurfaceParams *params = (RxbxSurfaceParams *)(cmd + 1);
			if (params->surface == xsurface)
			{
				bNeedQueue = true;
				break;
			}
		}
	}
	FOR_EACH_END;

	if (!bNeedQueue)
	{
		wtQueueCmd(xdevice->device_base.worker_thread, RXBXCMD_SURFACE_FREE, &xsurface, sizeof(xsurface));
	}
	else
	{
		QueuedRenderCmd *cmd = calloc(1, sizeof(QueuedRenderCmd) + sizeof(xsurface));
		cmd->cmd = RXBXCMD_SURFACE_FREE;
		cmd->data_size = sizeof(xsurface);
		*((RdrSurfaceDX **)(cmd + 1)) = xsurface;
		eaPush(&xdevice->device_base.queued_commands, cmd);
	}
}

static int rxbxChangeSurfaceParams(RdrSurface *surface, const RdrSurfaceParams *params)
{
	RxbxSurfaceParams *xparams;
	RdrDeviceDX *device = (RdrDeviceDX *)surface->device;
#if !PLATFORM_CONSOLE
	U32 max_allowed_multisample = 16;
#else
	U32 max_allowed_multisample = 4;
#endif
	U32 desired_multisample_level = params->desired_multisample_level;
	int i;
	int num_buffers = 1;

#if !PLATFORM_CONSOLE
	if (params->flags & SF_DEPTH_TEXTURE)
	{
		assertmsg(rxbxSupportsFeature(surface->device, FEATURE_DEPTH_TEXTURE), "Trying to create a depth surface when the hardware does not support one");
	}

	if (params->flags & SF_MRT4)
	{
		num_buffers = 4;
		max_allowed_multisample = 1; // DX9 does not support multisampling on MRTs
	}
	else if (params->flags & SF_MRT2)
	{
		num_buffers = 2;
		max_allowed_multisample = 1; // DX9 does not support multisampling on MRTs
	}

	assert(device->caps_filled);

	for (i = 0; i < num_buffers; i++)
	{
		MIN1(max_allowed_multisample, device->surface_types_multisample_supported[params->buffer_types[i] & SBT_TYPE_MASK]);
	}
#endif

	MIN1(desired_multisample_level, max_allowed_multisample);
	if (desired_multisample_level < params->required_multisample_level)
		return 0;

	for (i = 0; i < ARRAY_SIZE(surface->default_tex_flags); ++i)
		surface->default_tex_flags[i] = params->buffer_default_flags[i];

	surface->width_nonthread = surface->vwidth_nonthread = params->width;
	surface->height_nonthread = surface->vheight_nonthread = params->height;
	surface->params_nonthread = *params;

	if (device->device_base.is_locked_nonthread)
	{
		xparams = wtAllocCmd(surface->device->worker_thread, RXBXCMD_SURFACE_INIT, sizeof(*xparams));
	}
	else
	{
		QueuedRenderCmd *cmd = calloc(1, sizeof(QueuedRenderCmd) + sizeof(*xparams));
		cmd->cmd = RXBXCMD_SURFACE_INIT;
		cmd->data_size = sizeof(*xparams);
		xparams = (void *)(cmd + 1);
		eaPush(&device->device_base.queued_commands, cmd);
	}

	xparams->surface = (RdrSurfaceDX *)surface;
	CopyStructs(&xparams->params, params, 1);
	xparams->params.desired_multisample_level = desired_multisample_level;

	if (device->device_base.is_locked_nonthread)
		wtSendCmd(surface->device->worker_thread);

	return 1;
}

__forceinline static void rxbxInitSurfaceCommon(RdrDeviceDX *device, RdrSurfaceDX *surface, const char *name)
{
	int stateStackIndex;

	if (!rdr_state.disableSurfaceLifetimeLog)
		memlog_printf(NULL, "rxbxInitSurfaceCommon(%p) %s", surface, name);

	rdrInitSurface(&device->device_base, &surface->surface_base);

	surface->surface_base.destroy = rxbxDestroySurface;
	surface->surface_base.changeParams = rxbxChangeSurfaceParams;

	for ( stateStackIndex = 0; 
		stateStackIndex < ARRAY_SIZE( surface->state.blend_func.stack ); ++stateStackIndex )
	{
		surface->state.blend_func.stack[ stateStackIndex ].blend_enable = false;
		surface->state.blend_func.stack[ stateStackIndex ].sfactor = D3DBLEND_ONE;
		surface->state.blend_func.stack[ stateStackIndex ].dfactor = D3DBLEND_ZERO;
		surface->state.blend_func.stack[ stateStackIndex ].op = D3DBLENDOP_ADD;
	}

	surface->surface_base.used_snapshot_indices = baCreate(16);

	if (!surface->event_timer)
	{
		char buffer[1024];
		sprintf(buffer, "Surface: %s", name?name:"<unnamed>");
		surface->event_timer = etlCreateEventOwner(buffer, "Surface", "RenderLib");
	}
}

void rxbxInitPrimarySurface(RdrDeviceDX *device)
{
	RdrSurfaceDX *surface = &device->primary_surface;

	rxbxInitSurfaceCommon(device, surface, "Primary");
	surface->type = SURF_PRIMARY;
#if _XBOX
    surface->tile_count = 1;
#endif
	eaPush(&device->surfaces, surface); // not thread safe, but the thread is not running yet
}

RdrSurface *rxbxCreateSurface(RdrDevice *device, const RdrSurfaceParams *params)
{
	RdrSurfaceDX *surface = calloc(1,sizeof(*surface));

	if (!rdr_state.disableSurfaceLifetimeLog)
		memlog_printf(NULL, "rxbxCreateSurface(%p) %s", surface, params->name);

	assert(params->width && params->height);

	if (params->stereo_option)
	{
		rxbxNVSurfaceStereoCreationMode(params->stereo_option);
	}
	rxbxInitSurfaceCommon((RdrDeviceDX *)device, surface, params->name);
	if (!rxbxChangeSurfaceParams(&surface->surface_base, params))
	{
		free(surface);
		return NULL;
	}
	if (params->stereo_option)
	{
		rxbxNVSurfaceStereoCreationMode(SURF_STEREO_AUTO);
	}

	return (RdrSurface *)surface;
}

void rxbxRenameSurface(RdrDevice *device, RdrSurface *surface, const char *name)
{
	if (surface->params_nonthread.name != name)
	{
		surface->params_nonthread.name = name;
		((RdrSurfaceDX*)surface)->params_thread.name = name; // Not thread-safe
		// TODO: rename the etl event too (although may way to do so thread-safely)
	}
}

