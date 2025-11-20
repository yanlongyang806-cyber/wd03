#ifndef _RT_CURSOR_H_
#define _RT_CURSOR_H_

#include "device.h"

typedef struct RwglCursor
{
	U32 cache_it:1;
	HCURSOR handle;
	int hotspot_x, hotspot_y;
} RwglCursor;


void rwglSetCursorDirect(RdrDeviceWinGL *device);
void rwglReloadCursorDirect(RdrDeviceWinGL *device, RwglCursor *cursor);
void rwglDestroyActiveCursorDirect(RdrDeviceWinGL *device);
void rwglDestroyCursorDirect(RdrDeviceWinGL *device, HCURSOR handle);
void rwglShowCursorDirect(RdrDeviceWinGL *device, int show_cursor);

#endif //_RT_CURSOR_H_

