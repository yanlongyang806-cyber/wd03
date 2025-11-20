#include <wininclude.h>
#include <winuser.h>
#include "timing.h"
#include "color.h"

#include "rt_cursor.h"
#include "rt_surface.h"

typedef struct
{
	BITMAPV5HEADER	bi;
	U32				palette[256];
} BMAP;


static int findNearestIdx(U8 *c,PALETTEENTRY *syspals,int num_sys)
{
	int		i,dist,mindist=0x7fffffff,idx=0;

	for(i=0;i<num_sys;i++)
	{
		dist = ABS(c[0] - syspals[i].peRed);
		dist += ABS(c[1] - syspals[i].peGreen);
		dist += ABS(c[2] - syspals[i].peBlue);
		if (dist < mindist && (i < 10 || i > 245))// && (syspals[i].peRed == syspals[i].peGreen && syspals[i].peBlue == syspals[i].peGreen))
		{
			mindist = dist;
			idx = i;
		}
	}
	return idx;// ? 248 : 0;
}

static HCURSOR CreateCompatibleCursor(RdrDeviceWinGL *device, U8 *pixdata,int hotspot_x,int hotspot_y)
{
	DWORD dwWidth = device->cursor_size; // width of cursor
	DWORD dwHeight = device->cursor_size; // height of cursor
	HCURSOR hCurs1;
	int size = (device->cursor_size*device->cursor_size+7)/8;
	U8 *andmask = _alloca(size);
	U8 *xormask = _alloca(size);
	U32 i, j;
	int bitc=0;

	BYTE ANDmaskCursor[] = 
	{ 
		0xFF, 0xFC, 0x3F, 0xFF,   // line 1 
		0xFF, 0xC0, 0x1F, 0xFF,   // line 2 
		0xFF, 0x00, 0x3F, 0xFF,   // line 3 
		0xFE, 0x00, 0xFF, 0xFF,   // line 4 

		0xF7, 0x01, 0xFF, 0xFF,   // line 5 
		0xF0, 0x03, 0xFF, 0xFF,   // line 6 
		0xF0, 0x03, 0xFF, 0xFF,   // line 7 
		0xE0, 0x07, 0xFF, 0xFF,   // line 8 

		0xC0, 0x07, 0xFF, 0xFF,   // line 9 
		0xC0, 0x0F, 0xFF, 0xFF,   // line 10 
		0x80, 0x0F, 0xFF, 0xFF,   // line 11 
		0x80, 0x0F, 0xFF, 0xFF,   // line 12 

		0x80, 0x07, 0xFF, 0xFF,   // line 13 
		0x00, 0x07, 0xFF, 0xFF,   // line 14 
		0x00, 0x03, 0xFF, 0xFF,   // line 15 
		0x00, 0x00, 0xFF, 0xFF,   // line 16 

		0x00, 0x00, 0x7F, 0xFF,   // line 17 
		0x00, 0x00, 0x1F, 0xFF,   // line 18 
		0x00, 0x00, 0x0F, 0xFF,   // line 19 
		0x80, 0x00, 0x0F, 0xFF,   // line 20 

		0x80, 0x00, 0x07, 0xFF,   // line 21 
		0x80, 0x00, 0x07, 0xFF,   // line 22 
		0xC0, 0x00, 0x07, 0xFF,   // line 23 
		0xC0, 0x00, 0x0F, 0xFF,   // line 24 

		0xE0, 0x00, 0x0F, 0xFF,   // line 25 
		0xF0, 0x00, 0x1F, 0xFF,   // line 26 
		0xF0, 0x00, 0x1F, 0xFF,   // line 27 
		0xF8, 0x00, 0x3F, 0xFF,   // line 28 

		0xFE, 0x00, 0x7F, 0xFF,   // line 29 
		0xFF, 0x00, 0xFF, 0xFF,   // line 30 
		0xFF, 0xC3, 0xFF, 0xFF,   // line 31 
		0xFF, 0xFF, 0xFF, 0xFF    // line 32 
	};

	// Yin-shaped cursor XOR mask 

	BYTE XORmaskCursor[] = 
	{ 
		0x00, 0x00, 0x00, 0x00,   // line 1 
		0x00, 0x03, 0xC0, 0x00,   // line 2 
		0x00, 0x3F, 0x00, 0x00,   // line 3 
		0x00, 0xFE, 0x00, 0x00,   // line 4 

		0x0E, 0xFC, 0x00, 0x00,   // line 5 
		0x07, 0xF8, 0x00, 0x00,   // line 6 
		0x07, 0xF8, 0x00, 0x00,   // line 7 
		0x0F, 0xF0, 0x00, 0x00,   // line 8 

		0x1F, 0xF0, 0x00, 0x00,   // line 9 
		0x1F, 0xE0, 0x00, 0x00,   // line 10 
		0x3F, 0xE0, 0x00, 0x00,   // line 11 
		0x3F, 0xE0, 0x00, 0x00,   // line 12 

		0x3F, 0xF0, 0x00, 0x00,   // line 13 
		0x7F, 0xF0, 0x00, 0x00,   // line 14 
		0x7F, 0xF8, 0x00, 0x00,   // line 15 
		0x7F, 0xFC, 0x00, 0x00,   // line 16 

		0x7F, 0xFF, 0x00, 0x00,   // line 17 
		0x7F, 0xFF, 0x80, 0x00,   // line 18 
		0x7F, 0xFF, 0xE0, 0x00,   // line 19 
		0x3F, 0xFF, 0xE0, 0x00,   // line 20 

		0x3F, 0xC7, 0xF0, 0x00,   // line 21 
		0x3F, 0x83, 0xF0, 0x00,   // line 22 
		0x1F, 0x83, 0xF0, 0x00,   // line 23 
		0x1F, 0x83, 0xE0, 0x00,   // line 24 

		0x0F, 0xC7, 0xE0, 0x00,   // line 25 
		0x07, 0xFF, 0xC0, 0x00,   // line 26 
		0x07, 0xFF, 0xC0, 0x00,   // line 27 
		0x01, 0xFF, 0x80, 0x00,   // line 28 

		0x00, 0xFF, 0x00, 0x00,   // line 29 
		0x00, 0x3C, 0x00, 0x00,   // line 30 
		0x00, 0x00, 0x00, 0x00,   // line 31 
		0x00, 0x00, 0x00, 0x00    // line 32 
	};
	char bitp[] = {0x00, 0x11, 0x55, 0x77, 0xff};

#define SETBB(mem,bitnum) ((mem)[(bitnum) >> 3] |= (1 << (7-(bitnum) & 7)))
	memset(andmask, 0, size);
	memset(xormask, 0, size);

	for (j=0; j<device->cursor_size; j++) {
		U8 *pd = &pixdata[device->cursor_size*4*(device->cursor_size-1-j)];
		for (i=0; i<device->cursor_size; i++) {
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


	hCurs1 = CreateCursor( device->hInstance,   // app. instance 
		hotspot_x,         // horizontal position of hot spot 
		hotspot_y,         // vertical position of hot spot 
		dwWidth,           // cursor width 
		dwHeight,          // cursor height 
		andmask,     // AND mask 
		xormask);   // XOR mask 


	return hCurs1;
}


static HCURSOR CreateAlphaCursor(RdrDeviceWinGL *device, U8 *pixdata, int hotspot_x, int hotspot_y)
{
	HDC				hdc;
	HBITMAP			hMonoBitmap;
	DWORD			dwWidth, dwHeight;
	DWORD			*lpdwPixel;
	HBITMAP			hBitmap;
	void			*lpBits;
	DWORD			x,y;
	HCURSOR			hAlphaCursor = NULL;
	BMAP			bmap;
	BITMAPV5HEADER	*bi;
	ICONINFO		ii;
	static U32 bitmask[64*64/32];
	int				num_entries;
	PALETTEENTRY	syspals[256];
	HDC				hdcfrom, hdcto;
	HBITMAP			hBitmap2, hbmfrom, hbmto;

	dwWidth = device->cursor_size; // width of cursor
	dwHeight = device->cursor_size; // height of cursor
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
	if (device->cursor_gdi_plus)
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
	num_entries = GetSystemPaletteEntries(hdc,0,256,syspals);
	hBitmap = CreateDIBSection(hdc, (BITMAPINFO *)bi, DIB_RGB_COLORS, (void **)&lpBits, 0, 0);
	ReleaseDC(NULL,hdc);

	// Set the alpha values for each pixel in the cursor so that
	// the complete cursor is semi-transparent.
	lpdwPixel = (DWORD *)lpBits;
	memset(bitmask,0,sizeof(bitmask));
	if (!device->cursor_gdi_plus)
	{
		U8	*pix = (void*)lpBits,*mask = (U8*)bitmask;

		for (y=0;y<dwHeight;y++)
		{
			for (x=0;x<dwWidth;x++)
			{
				if (pixdata[3] < 1)
					mask[((dwHeight-1-y)*dwWidth + (x)) >> 3] |= 1 << (7-(x & 7)); // Had to figure this out by trial and error. Grr.
				*pix = findNearestIdx(pixdata,syspals,256);
				pix++;
				pixdata += 4;
			}
		}
	}
	else
	{
		for (y=0;y<dwHeight;y++)
		{
			for (x=0;x<dwWidth;x++)
			{
				*lpdwPixel++ = (pixdata[0] << 16) | (pixdata[1] << 8) | (pixdata[2] << 0) | (pixdata[3] << 24);
				pixdata += 4;
			}
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

void rwglSetCursorDirect(RdrDeviceWinGL *device)
{
	HCURSOR hcursor;
	PERFINFO_AUTO_START("rwglSetCursorDirect", 1);
	if (device->cursor && device->can_set_cursor)
		SetCursor(device->cursor);
	else if (hcursor = GetCursor())
		SetCursor(hcursor);
	PERFINFO_AUTO_STOP();
}

void rwglDestroyCursorDirect(RdrDeviceWinGL *device, HCURSOR handle)
{
	if (device->cursor == handle)
		device->cursor = 0;
	DestroyIcon(handle);
}

void rwglDestroyActiveCursorDirect(RdrDeviceWinGL *device)
{
	if (device->cursor && !device->dont_destroy_cursor)
		rwglDestroyCursorDirect(device, device->cursor);
}

__forceinline static int isFullyTransparent(U8 *data, int pixel_count)
{
	int i;
	data += 3;
	for (i = 0; i < pixel_count; i++, data+=4)
		if (*data)
			return 0;
	return 1;
}

void rwglReloadCursorDirect(RdrDeviceWinGL *device, RwglCursor *cursor)
{
	CHECKDEVICELOCK(device);
	if (!cursor->handle)
	{
		RdrSurfaceData surface_get = {0};
		surface_get.type = SURFDATA_RGBA;
		surface_get.width = device->cursor_size;
		surface_get.height = device->cursor_size;
		rdrAllocBufferForSurfaceData(&surface_get);
		rwglGetSurfaceDataDirect(device->active_surface, &surface_get);

		rwglDestroyActiveCursorDirect(device);

		if (device->compatible_cursor || isFullyTransparent(surface_get.data, device->cursor_size*device->cursor_size))
			device->cursor = CreateCompatibleCursor(device, surface_get.data, cursor->hotspot_x, cursor->hotspot_y);
		else
			device->cursor = CreateAlphaCursor(device, surface_get.data, cursor->hotspot_x, cursor->hotspot_y);

		free(surface_get.data);

		if (cursor->cache_it)
		{
			// we are caching this cursor, so don't destroy it automatically
			cursor->handle = device->cursor;
			device->dont_destroy_cursor = 1;
		}
		else
		{
			device->dont_destroy_cursor = 0;
			free(cursor);
		}
	}
	else
	{
		device->cursor = cursor->handle;
		device->dont_destroy_cursor = 1;
	}

	rwglSetCursorDirect(device);
}

void rwglShowCursorDirect(RdrDeviceWinGL *device, int show_cursor)
{
	ShowCursor(show_cursor);
}

