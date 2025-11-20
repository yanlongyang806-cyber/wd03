#if !PLATFORM_CONSOLE
#include <wininclude.h>
#include <winuser.h>
#endif


#include "RdrState.h"
#include "RenderLib.h"
#include "rt_xcursor.h"
#include "rt_xsurface.h"
#include "file.h"
#include "tga.h"
#include "TimedCallback.h"
#include "winutil.h"

#if !PLATFORM_CONSOLE
typedef struct
{
	BITMAPV5HEADER	bi;
	U32				palette[256];
} BMAP;

static HCURSOR CreateCompatibleCursor(RdrDeviceDX *device, U8 *pixdata,int hotspot_x,int hotspot_y, DWORD dwWidth, DWORD dwHeight)
{
	HCURSOR hCurs1;
	int size = (dwWidth*dwHeight+7)/8;
	U8 *andmask = _alloca(size);
	U8 *xormask = _alloca(size);
	U32 i, j;
	int bitc=0;

	char bitp[] = {0x00, 0x11, 0x55, 0x77, 0xff};

#define SETBB(mem,bitnum) ((mem)[(bitnum) >> 3] |= (1 << (7-(bitnum) & 7)))
	memset(andmask, 0, size);
	memset(xormask, 0, size);

	for (j=0; j<dwHeight; j++) {
		U8 *pd = &pixdata[dwWidth*4*(dwHeight-1-j)];
		for (i=0; i<dwWidth; i++) {
			Color c = *(Color*)pd;
			pd+=4;
			if (c.a < 10) {
				// Screen
				SETBB(andmask, bitc);
				//SETBB(xormask, bitc); // Inverse
			} else {
				// Black -> white
				int ind = (c.r + c.g + c.b) * ARRAY_SIZE(bitp) / 766;
				assert(ind >= 0 && ind < ARRAY_SIZE(bitp));
				if (bitp[ind] & (1 << ((i + j) % 8))) {
					SETBB(xormask, bitc);
				}
			}
			bitc++;
		}
	}


	hCurs1 = CreateCursor( device->windowClass.hInstance,   // app. instance 
		hotspot_x,         // horizontal position of hot spot 
		hotspot_y,         // vertical position of hot spot 
		dwWidth,           // cursor width 
		dwHeight,          // cursor height 
		andmask,     // AND mask 
		xormask);   // XOR mask 


	return hCurs1;
}


static HCURSOR CreateAlphaCursor(RdrDeviceDX *device, U8 *pixdata, int hotspot_x, int hotspot_y, DWORD dwWidth, DWORD dwHeight)
{
	HDC				hdc;
	HBITMAP			hMonoBitmap;
	DWORD			*lpdwPixel;
	HBITMAP			hBitmap;
	void			*lpBits;
	DWORD			x,y;
	HCURSOR			hAlphaCursor = NULL;
	BMAP			bmap;
	BITMAPV5HEADER	*bi;
	ICONINFO		ii;
	static U32 bitmask[64*64/32];
	HDC				hdcfrom, hdcto;
	HBITMAP			hBitmap2, hbmfrom, hbmto;

	bi = &bmap.bi;
	ZeroMemory(bi,sizeof(BITMAPV5HEADER));

	bi->bV5Size = sizeof(BITMAPV5HEADER);
	bi->bV5Width = dwWidth;
	bi->bV5Height = dwHeight;
	bi->bV5Planes = 1;
	bi->bV5BitCount = 8;
	bi->bV5Compression = BI_RGB;

	// The following mask specification specifies a supported 32 BPP
	// alpha format for Windows XP.
	if (1) //device->cursor_gdi_plus)
	{
		bi->bV5BitCount = 32;
		bi->bV5Compression = BI_BITFIELDS;
		bi->bV5RedMask = 0x00FF0000;
		bi->bV5GreenMask = 0x0000FF00;
		bi->bV5BlueMask = 0x000000FF;
		bi->bV5AlphaMask = 0xFF000000;
	}

	for(y=0;y<256;y++)
		bmap.palette[y] = y;
	hdc = GetDC(NULL);
	hBitmap = CreateDIBSection(hdc, (BITMAPINFO *)bi, DIB_RGB_COLORS, (void **)&lpBits, 0, 0);
	ReleaseDC(NULL,hdc);

	// Set the alpha values for each pixel in the cursor so that
	// the complete cursor is semi-transparent.
	lpdwPixel = (DWORD *)lpBits;
	memset(bitmask,0,sizeof(bitmask));
	for (y=0;y<dwHeight;y++)
	{
		for (x=0;x<dwWidth;x++)
		{
			*lpdwPixel++ = (pixdata[0] << 16) | (pixdata[1] << 8) | (pixdata[2] << 0) | (pixdata[3] << 24);
			pixdata += 4;
		}
	}
	hMonoBitmap = CreateBitmap(dwWidth,dwHeight,1,1,bitmask);

	ii.fIcon = FALSE; // Change fIcon to TRUE to create an alpha icon
	ii.xHotspot = hotspot_x;
	ii.yHotspot = hotspot_y;
	ii.hbmMask = hMonoBitmap;
	ii.hbmColor = hBitmap;

	// make a copy of bitmap for the current device
	hdc = GetDC(NULL);
	hdcfrom = CreateCompatibleDC(hdc);
	hdcto = CreateCompatibleDC(hdc);
	hBitmap2 = CreateCompatibleBitmap(hdc, dwWidth, dwHeight);
	hbmfrom = SelectObject(hdcfrom, hBitmap);
	hbmto = SelectObject(hdcto, hBitmap2);
	BitBlt(hdcto, 0, 0, dwWidth, dwHeight, hdcfrom, 0, 0, SRCCOPY);
	SelectObject(hdcfrom, hbmfrom);
	SelectObject(hdcto, hbmto);
	DeleteDC(hdcfrom);
	DeleteDC(hdcto);
	ReleaseDC(NULL, hdc);
	// should have a normal device-dependent bitmap now


	// Create the alpha cursor with the alpha DIB section
	hAlphaCursor = CreateIconIndirect(&ii);
	DeleteObject(hBitmap); 
	DeleteObject(hBitmap2);
	DeleteObject(hMonoBitmap); 
	return hAlphaCursor;
} 
#endif

void rxbxApplyCursorDirect(RdrDeviceDX *device)
{
#if !PLATFORM_CONSOLE
	HCURSOR hcursor;

	if (rdr_state.noCustomCursor)
		return;
	
	PERFINFO_AUTO_START_FUNC();
	if (device->cursor && device->cursor->handle && device->can_set_cursor)
	{
		if (device->last_set_cursor != device->cursor->handle)
			SetCursor(device->cursor->handle);
		device->last_set_cursor = device->cursor->handle;
	} else if ( (hcursor = GetCursor()) ) {
		if (device->last_set_cursor != hcursor)
			SetCursor(hcursor);
		device->last_set_cursor = hcursor;
	}
	PERFINFO_AUTO_STOP();
#endif
}

void rxbxDestroyCursorDirect(RdrDeviceDX *device, RxbxCursor **cursor_ptr, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	RxbxCursor *cursor = *cursor_ptr;
	if (rdr_state.noCustomCursor)
		return;
	
	assert(cursor);
	if (device->cursor == cursor)
		device->cursor = NULL;
	device->last_set_cursor = 0; // Can't hurt anything to clear this here, though might not be needed
	DestroyIcon(cursor->handle);
	free(cursor);
#endif
}

void rxbxDestroyActiveCursorDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	if (device->cursor)
	{
		if (device->cursor->cache_it)
		{
			// We don't own it, leave it active
		} else {
			rxbxDestroyCursorDirect(device, &device->cursor, packet);
		}
	}
#endif
}

#if !PLATFORM_CONSOLE
static int isFullyTransparent(U8 *data, int pixel_count)
{
	int i;
	data += 3;
	for (i = 0; i < pixel_count; i++, data+=4)
		if (*data)
			return 0;
	return 1;
}
#endif

void rxbxSetCursorDirect(RdrDeviceDX *device, RxbxCursor **cursor_ptr, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	RxbxCursor *cursor = *cursor_ptr;

    if (rdr_state.noCustomCursor)
		return;

	assert(cursor->handle);
	assert(cursor->cache_it); // Only cached cursors should have handles already
	device->cursor = cursor;

	rxbxApplyCursorDirect(device);
#endif
}

void rxbxSetCursorFromDataDirect(RdrDeviceDX *device, RxbxCursorData *cursor_data, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	RxbxCursor *cursor = cursor_data->cursor;

	if (!rdr_state.noCustomCursor)
	{
		if (!cursor->handle)
		{
			// Destroys active cursor if not cached, otherwise does nothing
			rxbxDestroyActiveCursorDirect(device, NULL, packet);

			if (/*device->compatible_cursor || */isFullyTransparent(cursor_data->data, cursor_data->size_x*cursor_data->size_y))
				cursor->handle = CreateCompatibleCursor(device, cursor_data->data, cursor->hotspot_x, cursor->hotspot_y, cursor_data->size_x, cursor_data->size_y);
			else
				cursor->handle = CreateAlphaCursor(device, cursor_data->data, cursor->hotspot_x, cursor->hotspot_y, cursor_data->size_x, cursor_data->size_y);
			device->cursor = cursor;
		}
		else
		{
			assert(0); // This function shouldn't get called unless we're loading a new cursor!
			assert(cursor->cache_it); // Only cached cursors should have handles already
			device->cursor = cursor;
		}

		rxbxApplyCursorDirect(device);
	}
#endif
	SAFE_FREE(cursor_data->data);
}


void rxbxSetCursorStateDirect(RdrDeviceDX *device, RdrCursorState *state_ptr, WTCmdPacket *packet)
{
	device->device_base.thread_cursor_visible = state_ptr->visible;
	device->device_base.thread_cursor_restrict_to_window = state_ptr->restrict_to_window;
	rxbxRefreshCursorStateDirect( device );
}

void rxbxRefreshCursorStateDirect( RdrDeviceDX* device )
{
#if !PLATFORM_CONSOLE
	if (!rdr_state.noCustomCursor)
	{
		S32 doShowCursor = device->device_base.thread_cursor_visible;
		S32 value = ShowCursor(doShowCursor);
		
		while(value + 1 > doShowCursor){
			value = ShowCursor(0);
		}
		
		while(value + 1 < doShowCursor){
			value = ShowCursor(1);
		}
	}

	if (!rdr_state.noClipCursor)
	{
		if( device->display_thread.fullscreen ) {
			const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
			MONITORINFOEX moninfo;
			multiMonGetMonitorInfo( device_infos[device->device_info_index]->monitor_index, &moninfo );
			ClipCursor( &moninfo.rcMonitor );
		} else if( device->device_base.thread_cursor_restrict_to_window ) {
			HWND hwnd = rdrGetWindowHandle( &device->device_base );
			RECT rect;
			POINT zero = { 0 , 0 };
			GetClientRect( hwnd, &rect );
			ClientToScreen( hwnd, &zero );
			rect.left += zero.x;
			rect.top += zero.y;
			rect.right += zero.x;
			rect.bottom += zero.y;
			ClipCursor( &rect );
		} else {
			ClipCursor( NULL );
		}
	}
	else
	{
		ClipCursor( NULL );
	}
#endif
}
