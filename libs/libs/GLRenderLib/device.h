#ifndef _DEVICE_H
#define _DEVICE_H

#include "wininclude.h"
#include "StashTable.h"
#include "RdrDevice.h"
#include "surface.h"
#include "rt_shaders.h"

typedef struct RdrGeometryDataWinGL RdrGeometryDataWinGL;

typedef enum
{
	// create and destroy
	RWGLCMD_CREATE = RDRCMD_MAX,
	RWGLCMD_DESTROY,

	// process messages
	RWGLCMD_PROCESSWINMSGS,

	// surfaces
	RWGLCMD_UPDATEMATRICES,
	RWGLCMD_INITSURFACE,
	RWGLCMD_FREESURFACE,

	// cursor
	RWGLCMD_SETCURSOR,
	RWGLCMD_DESTROYCURSOR,
	RWGLCMD_DESTROYACTIVECURSOR,

} RwglCmdType;


typedef struct RdrDeviceWinGL
{
	// RdrDevice MUST be first in the struct!
	RdrDevice device_base;

	WNDCLASSEX windowClass;
	char windowClassName[256];
	HWND hwnd;
	HINSTANCE hInstance;

	U32 fullscreen : 1;
	U32 maximized : 1;
	U32 minimized : 1;
	U32 inactive_display : 1;
	int screen_x_pos, screen_y_pos;
	int screen_width_restored, screen_height_restored;
	int refresh_rate;

	// surfaces
	RdrSurfaceWinGL primary_surface;
	RdrSurfaceWinGL *active_surface;
	RdrSurfaceWinGL **surfaces;

	// cursor
	U32 cursor_size:16;
	U32 compatible_cursor:1;
	U32 cursor_gdi_plus:1;
	U32 can_set_cursor:1;
	U32 dont_destroy_cursor:1;
	HCURSOR cursor;
	StashTable cursor_cache;

	// gamma
	WORD preserved_ramp[256*3];
	int gamma_ramp_has_been_preserved;
	F32 current_gamma;

	// vertex/fragment programs
	ShaderHandle *vertex_programs;
	ShaderHandle *fragment_programs;
	StashTable lpc_crctable;

	// cached data
	StashTable texture_data;
	RdrGeometryDataWinGL **geometry_data;

	// misc
	TexHandle white_tex_handle;

} RdrDeviceWinGL;


#endif //_DEVICE_H

