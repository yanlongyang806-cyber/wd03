#ifndef _RT_XCURSOR_H_
#define _RT_XCURSOR_H_

#include "xdevice.h"

typedef struct RxbxCursor
{
	U32 cache_it:1;
	HCURSOR handle;
	int hotspot_x, hotspot_y;
} RxbxCursor;

typedef struct RxbxCursorData
{
	RxbxCursor * cursor;
	int size_x;
	int size_y;
	U8 *data;
} RxbxCursorData;


void rxbxApplyCursorDirect(RdrDeviceDX *device);
void rxbxSetCursorDirect(RdrDeviceDX *device, RxbxCursor **cursor_ptr, WTCmdPacket *packet);
void rxbxSetCursorFromDataDirect(RdrDeviceDX *device, RxbxCursorData *cursor_data, WTCmdPacket *packet);
void rxbxDestroyActiveCursorDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);
void rxbxDestroyCursorDirect(RdrDeviceDX *device, RxbxCursor **cursor_ptr, WTCmdPacket *packet);
void rxbxSetCursorStateDirect(RdrDeviceDX *device, RdrCursorState *state_ptr, WTCmdPacket *packet);
void rxbxRefreshCursorStateDirect(RdrDeviceDX *device);

#endif //_RT_XCURSOR_H_


