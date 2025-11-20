#include "earray.h"

#include "surface.h"
#include "device.h"
#include "rt_surface.h"

static void rwglDestroySurface(RdrSurface *surface)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)surface->device;
	RdrSurfaceWinGL *glsurface = (RdrSurfaceWinGL *)surface;
	if (glsurface->type == SURF_PRIMARY)
		return;

	baDestroy(surface->used_snapshot_indices);

	eaFindAndRemoveFast(&gldevice->surfaces, glsurface);
	wtQueueCmd(surface->device->worker_thread, RWGLCMD_FREESURFACE, &glsurface, sizeof(glsurface));
}

static void rwglGetSurfaceSize(RdrSurface *surface, int *width, int *height)
{
	RdrSurfaceWinGL *glsurface = (RdrSurfaceWinGL *)surface;
	if (!glsurface->width || !glsurface->height)
		wtFlush(surface->device->worker_thread);
	if (width)
		*width = glsurface->width;
	if (height)
		*height = glsurface->height;
}

static void rwglGetSurfaceVirtualSize(RdrSurface *surface, int *width, int *height)
{
	RdrSurfaceWinGL *glsurface = (RdrSurfaceWinGL *)surface;
	if (!glsurface->virtual_width || !glsurface->virtual_height)
		wtFlush(surface->device->worker_thread);
	if (width)
		*width = glsurface->virtual_width?glsurface->virtual_width:glsurface->width;
	if (height)
		*height = glsurface->virtual_height?glsurface->virtual_height:glsurface->height;
}

static void rwglUpdateMatrices(RdrSurface *surface, const Mat44 projmat, const Mat4 viewmat, const Mat4 inv_viewmat, const Mat4 fogmat, F32 znear, F32 zfar)
{
	RwglUpdateMatrixData *data;
	
	// send updated matrices to thread
	data = wtAllocCmd(surface->device->worker_thread, RWGLCMD_UPDATEMATRICES, sizeof(RwglUpdateMatrixData));
	data->surface = (RdrSurfaceWinGL *)surface;
	copyMat4(viewmat, data->view_mat);
	copyMat4(inv_viewmat, data->inv_view_mat);
	copyMat44(projmat, data->projection);
	wtSendCmd(surface->device->worker_thread);
}

static int rwglChangeSurfaceParams(RdrSurface *surface, RdrSurfaceParams *params)
{
	RwglSurfaceParams *glparams = wtAllocCmd(surface->device->worker_thread, RWGLCMD_INITSURFACE, sizeof(*glparams));
	glparams->surface = (RdrSurfaceWinGL *)surface;
	CopyStructs(&glparams->params, params, 1);
	wtSendCmd(surface->device->worker_thread);
	return 1;
}

__forceinline static void rwglInitSurfaceCommon(RdrDeviceWinGL *device, RdrSurfaceWinGL *surface)
{
	rdrInitSurface(&device->device_base, &surface->surface_base);

	surface->surface_base.destroy = rwglDestroySurface;
	surface->surface_base.changeParams = rwglChangeSurfaceParams;
	surface->surface_base.updateMatrices = rwglUpdateMatrices;
	surface->surface_base.getSize = rwglGetSurfaceSize;
	surface->surface_base.getVirtualSize = rwglGetSurfaceVirtualSize;

	eaPush(&device->surfaces, surface);

	surface->state.white_tex_handle = device->white_tex_handle;

	surface->surface_base.used_snapshot_indices = baCreate(16);
}

void rwglInitPrimarySurface(RdrDeviceWinGL *device)
{
	RdrSurfaceWinGL *surface = &device->primary_surface;

	rwglInitSurfaceCommon(device, surface);
	surface->type = SURF_PRIMARY;
}

RdrSurface *rwglCreateSurface(RdrDevice *device, RdrSurfaceParams *params)
{
	RdrSurfaceWinGL *surface = calloc(1,sizeof(*surface));

	rwglInitSurfaceCommon((RdrDeviceWinGL *)device, surface);
	rwglChangeSurfaceParams((RdrSurface *)surface, params);

	return (RdrSurface *)surface;
}



