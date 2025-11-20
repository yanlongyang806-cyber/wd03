
#include "earray.h"
#include "winutil.h"
#include "qsortG.h"
#include "error.h"

#include "RenderLib.h"
#include "../RdrDevicePrivate.h"
#include "../RdrDrawListPrivate.h"
#include "device.h"
#include "rt_device.h"
#include "rt_surface.h"
#include "rt_primitive.h"
#include "rt_sprite.h"
#include "rt_tex.h"
#include "rt_geo.h"
#include "rt_cursor.h"
#include "rt_model.h"
#include "rt_drawmode.h"
#include "rt_particle.h"
#include "rt_postprocessing.h"
#include "systemspecs.h"

static void rwglDestroyDevice(RdrDevice *device);

//////////////////////////////////////////////////////////////////////////
// command dispatch

static void rwglCmdDispatch(RdrDeviceWinGL *device, RwglCmdType cmd, void *data)
{
	switch (cmd)
	{
		//////////////////////////////////////////////////////////////////////////
		// create and destroy
		xcase RWGLCMD_CREATE:
			rwglCreateDirect(device, data);

		xcase RWGLCMD_DESTROY:
			rwglDestroyDirect(device);


		//////////////////////////////////////////////////////////////////////////
		// process messages and swap
		xcase RWGLCMD_PROCESSWINMSGS:
			rwglProcessWindowsMessagesDirect(device);

		xcase RDRCMD_BEGINSCENE:
			if (*((bool *)data))
				rwglSwapBufferDirect(device);

		xcase RDRCMD_ENDSCENE:
			// nothing to do here


		//////////////////////////////////////////////////////////////////////////
		// device window settings
		xcase RDRCMD_WIN_SETVSYNC:
			rwglSetVsyncDirect(device, *((int *)data));

		xcase RDRCMD_WIN_SETGAMMA:
			rwglSetGammaDirect(device, *((F32 *)data));

		xcase RDRCMD_WIN_GAMMANEEDRESET:
			rwglNeedGammaResetDirect(device);


		//////////////////////////////////////////////////////////////////////////
		// surfaces
		xcase RWGLCMD_INITSURFACE:
			rwglInitSurfaceDirect(device, data);

		xcase RWGLCMD_FREESURFACE:
			rwglFreeSurfaceDirect(device, *((RdrSurfaceWinGL **)data));

		xcase RDRCMD_SURFACE_SETACTIVE:
			if (!data)
				rwglSetSurfaceActiveDirect(device, 0);
			else
				rwglSetSurfaceActiveDirect(device, *((RdrSurfaceWinGL **)data));

		xcase RDRCMD_SURFACE_CLEAR:
			rwglClearActiveSurfaceDirect(device, data);

		xcase RDRCMD_SURFACE_GETDATA:
			rwglGetSurfaceDataDirect(device->active_surface, data);

		xcase RWGLCMD_UPDATEMATRICES:
			rwglUpdateSurfaceMatricesDirect(data);

		xcase RDRCMD_SURFACE_SETFOG:
			rwglSetSurfaceFogDirect(data);


		//////////////////////////////////////////////////////////////////////////
		// shaders
		xcase RDRCMD_UPDATESHADER:
			rwglSetShaderDataDirect(device, data);

		xcase RDRCMD_RELOADSHADERS:
			rwglReloadDefaultShadersDirect(device);

		xcase RDRCMD_QUERYSHADERPERF:
			rwglQueryShaderPerfDirect(device, *(RdrShaderPerformanceValues **)data);

		//////////////////////////////////////////////////////////////////////////
		// textures
		xcase RDRCMD_UPDATETEXTURE:
			rwglSetTextureDataDirect(device, data);

		xcase RDRCMD_UPDATESUBTEXTURE:
			rwglSetTextureSubDataDirect(device, data);

		xcase RDRCMD_SETTEXTUREANISOTROPY:
			rwglSetTexAnisotropyDirect(device, data);

		xcase RDRCMD_GETTEXINFO:
			rwglGetTexInfoDirect(device, data);

		xcase RDRCMD_FREETEXTURE:
			rwglFreeTextureDirect(device, *((TexHandle *)data));

		xcase RDRCMD_FREEALLTEXTURES:
			rwglFreeAllTexturesDirect(device);


		//////////////////////////////////////////////////////////////////////////
		// geometry
		xcase RDRCMD_UPDATEGEOMETRY:
			rwglSetGeometryDataDirect(device, data);

		xcase RDRCMD_FREEGEOMETRY:
			rwglFreeGeometryDirect(device, *((GeoHandle *)data));

		xcase RDRCMD_FREEALLGEOMETRY:
			rwglFreeAllGeometryDirect(device);


		//////////////////////////////////////////////////////////////////////////
		// drawing
		xcase RDRCMD_DRAWQUAD:
			rwglDrawQuadDirect(device, data);

		xcase RDRCMD_POSTPROCESSSCREEN:
			rwglPostProcessScreenDirect(device, data);

		xcase RDRCMD_POSTPROCESSSHAPE:
			rwglPostProcessShapeDirect(device, data);


		xcase RDRCMD_SETTREEPARAMS:
			// TomY NOT IMPLEMENTED


		//////////////////////////////////////////////////////////////////////////
		// cursor
		xcase RDRCMD_SHOWCURSOR:
			rwglShowCursorDirect(device, *((int *)data));

		xcase RWGLCMD_SETCURSOR:
			rwglReloadCursorDirect(device, *((RwglCursor **)data));

		xcase RWGLCMD_DESTROYCURSOR:
			rwglDestroyCursorDirect(device, (HCURSOR)data);

		xcase RWGLCMD_DESTROYACTIVECURSOR:
			rwglDestroyActiveCursorDirect(device);

		//////////////////////////////////////////////////////////////////////////
		xdefault:
			rdrAlertMsg((RdrDevice *)device, "Unhandled render command!");
			break;
	}
}


//////////////////////////////////////////////////////////////////////////
// process messages and swap

static void rwglProcessMessages(RdrDevice *device)
{
	wtQueueCmd(device->worker_thread, RWGLCMD_PROCESSWINMSGS, 0, 0);
	wtMonitor(device->worker_thread);
}


//////////////////////////////////////////////////////////////////////////
// surfaces

static RdrSurface *rwglGetPrimarySurface(RdrDevice *device)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;
	return (RdrSurface *)&gldevice->primary_surface;
}

static void rwglSetPrimarySurfaceActive(RdrDevice *device)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;
	rdrSurfaceSetActive((RdrSurface *)&gldevice->primary_surface, 0);
}

static void *rwglGetHWND(RdrDevice *device)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;
	return gldevice->hwnd;
}

static void *rwglGetHINSTANCE(RdrDevice *device)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;
	return gldevice->hInstance;
}

//////////////////////////////////////////////////////////////////////////
// capability query
static int rwglGetMaxTexAnisotropy(RdrDevice *device)
{
	if (!rdr_caps.filled_in)
		return 1;

	return rdr_caps.maxTexAnisotropic;
}

static void rwglGetDeviceSize(RdrDevice *device, int *xpos, int *ypos, int *width, int *height, int *refresh_rate, int *fullscreen, int *maximized)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;
	if (xpos)
		*xpos = gldevice->screen_x_pos;
	if (ypos)
		*ypos = gldevice->screen_y_pos;
	if (width)
	{
		if (gldevice->minimized)
			*width = gldevice->screen_width_restored;
		else
			*width = gldevice->primary_surface.width;
	}
	if (height)
	{
		if (gldevice->minimized)
			*height = gldevice->screen_height_restored;
		else
			*height = gldevice->primary_surface.height;
	}
	if (refresh_rate)
		*refresh_rate = gldevice->refresh_rate;
	if (fullscreen)
		*fullscreen = gldevice->fullscreen;
	if (maximized)
		*maximized = !!gldevice->maximized;
}

const char *rwglGetIdentifier(RdrDevice *device)
{
	return "WinGL";
}

//////////////////////////////////////////////////////////////////////////
// cursor

static int supportsGdiPlus() 
{
	DWORD	version;

	version = GetVersion();
	if (version & 0x80000000)
		return 0;
	if ((version & 255) < 5)
		return 0;
	return 1;
} 

static void rwglInitCursor(RdrDeviceWinGL *device, int compatible_cursor)
{
	device->cursor_size = 32;
	device->cursor_gdi_plus = !!supportsGdiPlus();
	if (device->cursor_gdi_plus)
	{
		if (rdr_caps.videoCardVendorID == VENDOR_ATI || rdr_caps.videoCardVendorID == VENDOR_INTEL)
			// ATI has (had?) a bug where the hardware cursor needed to be 64x64 or less
			// *including* the drop shadow, so we have to use a smaller size here
			// Intel has a similar issue, but said they will fix it soon
			device->cursor_size = 48;
		else
			device->cursor_size = 64;
	}

	if (compatible_cursor)
		device->cursor_size = 32;

	device->compatible_cursor = !!compatible_cursor;

	device->cursor_cache = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys);
}

static int rwglGetCursorSize(RdrDevice *device)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;
	return gldevice->cursor_size;
}

static int rwglSetCursorFromCache(RdrDevice *device, const char *cursor_name)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;
	RwglCursor *cursor;

	if (stashFindPointer(gldevice->cursor_cache, cursor_name, &cursor))
	{
		// set cursor
		wtQueueCmd(device->worker_thread, RWGLCMD_SETCURSOR, &cursor, sizeof(cursor));
		return 1;
	}
	return 0; // not cached
}

static void rwglSetCursorFromSurface(RdrDevice *device, const char *cursor_name, int hotspot_x, int hotspot_y)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;
	RwglCursor *cursor = malloc(sizeof(*cursor));

	cursor->cache_it = 0;
	if (cursor_name)
	{
		RwglCursor *old_cursor;

		// cache it
		if (stashFindPointer(gldevice->cursor_cache, cursor_name, &old_cursor))
		{
			// destroy old one
			wtQueueCmd(device->worker_thread, RWGLCMD_DESTROYCURSOR, &old_cursor->handle, sizeof(HCURSOR));
			free(old_cursor);
		}
		stashAddPointer(gldevice->cursor_cache, cursor_name, cursor, true);
		cursor->cache_it = 1;
	}

	cursor->hotspot_x = hotspot_x;
	cursor->hotspot_y = hotspot_y;
	cursor->handle = 0;
	wtQueueCmd(device->worker_thread, RWGLCMD_SETCURSOR, &cursor, sizeof(cursor));
}

static int rwglFreeCursorFromCache(void *param, StashElement element)
{
	RdrDevice *device = (RdrDevice *)param;
	RwglCursor *cursor = stashElementGetPointer(element);
	wtQueueCmd(device->worker_thread, RWGLCMD_DESTROYCURSOR, &cursor->handle, sizeof(HCURSOR));
	free(cursor);
	return 1;
}

static void rwglFreeAllCursors(RdrDevice *device)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;
	wtQueueCmd(device->worker_thread, RWGLCMD_DESTROYACTIVECURSOR, 0, 0);
	stashForEachElementEx(gldevice->cursor_cache, rwglFreeCursorFromCache, device);
	stashTableClear(gldevice->cursor_cache);
}

//////////////////////////////////////////////////////////////////////////
// create and destroy

static void rwglFreeShaders(RdrDevice *device)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;

	if (device->worker_thread)
		wtFlush(device->worker_thread);

	stashTableDestroy(gldevice->lpc_crctable);
	gldevice->lpc_crctable = 0;
	SAFE_FREE(gldevice->vertex_programs);
	SAFE_FREE(gldevice->fragment_programs);
}

RdrDevice *rdrCreateDeviceWinGL(WindowCreateParams *params, HINSTANCE hInstance, bool compatible_cursor, bool threaded)
{
	RdrDeviceWinGL *device = calloc(1, sizeof(RdrDeviceWinGL));

	systemSpecsInit();

	//////////////////////////////////////////////////////////////////////////
	// misc
	device->hInstance = hInstance;

	//////////////////////////////////////////////////////////////////////////
	// create worker thread
	rdrInitDevice((RdrDevice *)device, threaded, -1);
	wtSetDefaultCmdDispatch(device->device_base.worker_thread, (WTDefaultDispatchCallback)rwglCmdDispatch);

	device->texture_data = stashTableCreateInt(2048);

	//////////////////////////////////////////////////////////////////////////
	// setup function pointers
	device->device_base.processMessages = rwglProcessMessages;

	device->device_base.destroy = rwglDestroyDevice;
	device->device_base.isInactive = rwglIsInactive;
	device->device_base.reactivate = rwglReactivate;

	device->device_base.createSurface = rwglCreateSurface;
	device->device_base.getPrimarySurface = rwglGetPrimarySurface;
	device->device_base.setPrimarySurfaceActive = rwglSetPrimarySurfaceActive;

	device->device_base.getWindowHandle = rwglGetHWND;
	device->device_base.getInstanceHandle = rwglGetHINSTANCE;

	device->device_base.getMaxTexAnisotropy = rwglGetMaxTexAnisotropy;

	device->device_base.getCursorSize = rwglGetCursorSize;
	device->device_base.setCursorFromActiveSurface = rwglSetCursorFromSurface;
	device->device_base.setCursorFromCache = rwglSetCursorFromCache;

	device->device_base.getSize = rwglGetDeviceSize;
	device->device_base.getIdentifier = rwglGetIdentifier;

	//////////////////////////////////////////////////////////////////////////
	// setup primary surface
	rwglInitPrimarySurface(device);


	//////////////////////////////////////////////////////////////////////////
	// create window
	rdrLockActiveDevice((RdrDevice *)device, false, false);
	wtQueueCmd(device->device_base.worker_thread, RWGLCMD_CREATE, params, sizeof(*params));
	wtFlush(device->device_base.worker_thread);
	if (device->hwnd)
	{
		// create white texture
		U32 byte_count;
		RdrTexParams *texparams;
		device->primary_surface.state.white_tex_handle = device->white_tex_handle = rdrGenTexHandle(0);
		texparams = rdrStartUpdateTexture((RdrDevice *)device, device->white_tex_handle, RTEX_2D, RTEX_LA_U8, 1, 1, 1, 0, &byte_count, "Textures:Misc");
		memset(texparams+1, 0xffffffff, byte_count);
		rdrEndUpdateTexture((RdrDevice *)device);
		wtFlush(device->device_base.worker_thread);

		// cursor
		rwglInitCursor(device, compatible_cursor);
	}
	rdrUnlockActiveDevice((RdrDevice *)device, true, false);

	if (!device->hwnd)
	{
		rwglDestroyDevice((RdrDevice *)device);
		device = 0;
	}

	return (RdrDevice *)device;
}

static void rwglDestroyDevice(RdrDevice *device)
{
	RdrDeviceWinGL *gldevice = (RdrDeviceWinGL *)device;

	if (!gldevice)
		return;

	rdrLockActiveDevice(device, true, false);
	rwglFreeAllCursors(device);
	rdrFreeAllGeometry(device);
	rdrFreeAllTextures(device);
	wtQueueCmd(device->worker_thread, RWGLCMD_DESTROY, 0, 0);
	rdrUnlockActiveDevice(device, true, false);

	rdrUninitDevice(device);

	rwglFreeShaders(device);

	stashTableDestroy(gldevice->cursor_cache);

	eaDestroy(&gldevice->surfaces);

	stashTableDestroy(gldevice->texture_data);
	eaDestroy(&gldevice->geometry_data);

	ZeroStruct(gldevice);
	free(gldevice);
}







