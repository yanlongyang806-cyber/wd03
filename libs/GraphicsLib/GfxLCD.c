#include "GfxLCD.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#if PLATFORM_CONSOLE

S32 gfxLCDIsEnabled(void){return 0;}
void gfxLCDInit(void) {;}
void gfxLCDDeinit(void) {;}
void gfxLCDOncePerFrame(void) {;}

void AutoGen_AutoRun_RegisterAutoCmd_noLCD(void) {;} // Guaranteed to break at some point

void gfxLCDAddMeter(const char *name, F32 value, F32 minv, F32 maxv, U32 textcolor, U32 colormin, U32 colormid, U32 colormax) {;}
void gfxLCDAddText(const char *text, U32 color) {;}
bool gfxLCDIsQVGA(void) {return false;}

#else

#include "GraphicsLibPrivate.h"
#include "GfxTextures.h"
#include "GfxDXT.h"
#include "earray.h"
#include "MemoryPool.h"
#include "ScratchStack.h"
#include "wininclude.h"
#include "GlobalTypes.h"
#include "Color.h"
#include "FolderCache.h"
#include "../../3rdparty/LogiTech/lglcd.h" // include the Logitech LCD SDK header

// make sure we use the library
#pragma comment(lib, "lgLcd.lib")

typedef enum LCDLineType {
	LCDLINE_TEXT,
	LCDLINE_METER,
} LCDLineType;

typedef struct LCDLine {
	LCDLineType type;
	char text[80];
	F32 value, minv, maxv;
	U32 textcolor;
	U32 barcolor;
} LCDLine;

typedef struct LCDState {
	bool enabled;
	bool noLCD;
	bool forceBW;
	lgLcdConnectContext connectContext;
	lgLcdDeviceDesc deviceDescription;
	lgLcdOpenByTypeContext openContext;

	LCDLine **lines;

	HFONT hFont;
	HBITMAP hBitmap;
	HDC hdc;
	BITMAPINFO *pBitmapInfo;
	BYTE *pBitmapBits;
	int width, height;

} LCDState;

MP_DEFINE(LCDLine);

static LCDState lcd_state;

// Disables the Logitech LCD interface code
AUTO_CMD_INT(lcd_state.noLCD, noLCD) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;
// Force Logitech LCD into black and white mode
AUTO_CMD_INT(lcd_state.forceBW, lcdForceBW) ACMD_CMDLINE;

void lcdTimeoutCallback(void)
{
	FatalErrorf("It appears the Logitech GamePanel API has hung.  If this persists, please add -noLCD to your Advanced Command Line in the Options window to disable Logitech GamePanel integration.  Restarting the application or rebooting your computer may also resolve this issue.");
}

S32 gfxLCDIsEnabled(void)
{
	return lcd_state.enabled;
}


void gfxLCDInit(void)
{
	bool need_loadend=false;
#define HandleError(res, msg)				\
		if (ERROR_SUCCESS != res) {			\
			if (need_loadend)				\
				loadend_printf("failed.");	\
			lcd_state.enabled = false;		\
			return;							\
		}

	DWORD res;

	if (lcd_state.noLCD)
		return;

#define LOGITECH_TIMEOUT 60

	//// initialize the library
	ASSERT_COMPLETES_CALLBACK(LOGITECH_TIMEOUT, lcdTimeoutCallback, res = lgLcdInit());
	HandleError(res, "lgLcdInit");

	need_loadend = true;
	loadstart_printf("Initializing Logitech LCD...");

	//// connect to LCDMon
	// set up connection context
	ZeroMemory(&lcd_state.connectContext, sizeof(lcd_state.connectContext));

	lcd_state.connectContext.appFriendlyName = UTF8_To_UTF16_malloc(GetProductDisplayName(getCurrentLocale()));
	if (!lcd_state.connectContext.appFriendlyName)
		lcd_state.connectContext.appFriendlyName = L"Cryptic Engine";
	lcd_state.connectContext.isAutostartable = FALSE;
	lcd_state.connectContext.isPersistent = FALSE;
	// we don't have a configuration screen
	lcd_state.connectContext.onConfigure.configCallback = NULL;
	lcd_state.connectContext.onConfigure.configContext = NULL;
	// the "connection" member will be returned upon return
	lcd_state.connectContext.connection = LGLCD_INVALID_CONNECTION;
	// and connect
	ASSERT_COMPLETES_CALLBACK(LOGITECH_TIMEOUT, lcdTimeoutCallback, res = lgLcdConnect(&lcd_state.connectContext))
	HandleError(res, "lgLcdConnect");

	// now we are connected (and have a connection handle returned),
	/*
	// let's enumerate an LCD (the first one, index = 0)
	ASSERT_COMPLETES_CALLBACK(LOGITECH_TIMEOUT, lcdTimeoutCallback, res = lgLcdEnumerate(lcd_state.connectContext.connection, 0, &lcd_state.deviceDescription));
	HandleError(res, "lgLcdEnumerate");

	// at this point, we have an LCD

	// open it
	ZeroMemory(&lcd_state.openContext, sizeof(lcd_state.openContext));
	lcd_state.openContext.connection = lcd_state.connectContext.connection;
	lcd_state.openContext.index = 0;
	// we have no softbutton notification callback
	lcd_state.openContext.onSoftbuttonsChanged.softbuttonsChangedCallback = NULL;
	lcd_state.openContext.onSoftbuttonsChanged.softbuttonsChangedContext = NULL;
	// the "device" member will be returned upon return
	lcd_state.openContext.device = LGLCD_INVALID_DEVICE;
	ASSERT_COMPLETES_CALLBACK(LOGITECH_TIMEOUT, lcdTimeoutCallback, res = lgLcdOpen(&lcd_state.openContext));
	HandleError(res, "lgLcdOpen");
	*/

	lcd_state.openContext.device = LGLCD_INVALID_DEVICE;
	lcd_state.openContext.connection = lcd_state.connectContext.connection;
	//lcd_state.openContext.onSoftbuttonsChanged.softbuttonsChangedCallback = OnLCDButtonsCallback;
	//lcd_state.openContext.onSoftbuttonsChanged.softbuttonsChangedContext = NULL;

	lcd_state.openContext.deviceType = lcd_state.forceBW?LGLCD_DEVICE_BW:LGLCD_DEVICE_QVGA;
	ASSERT_COMPLETES_CALLBACK(LOGITECH_TIMEOUT, lcdTimeoutCallback, res = lgLcdOpenByType(&lcd_state.openContext));
	if (res != ERROR_SUCCESS)
	{
		// Try black and white
		lcd_state.openContext.deviceType = LGLCD_DEVICE_BW;
		ASSERT_COMPLETES_CALLBACK(LOGITECH_TIMEOUT, lcdTimeoutCallback, res = lgLcdOpenByType(&lcd_state.openContext));
	}
	HandleError(res, "lgLcdOpenByType");

	// coming back from lgLcdOpen, we have a device handle (in lcd_state.openContext.device)
	// which we will be using from now on until the program exits

	lcd_state.enabled = true;

	MP_CREATE(LCDLine, 4);

	//printf("Found an LCD with %dx%d pixels, %d bits per pixel and %d soft buttons\n",
	//	lcd_state.deviceDescription.Width, lcd_state.deviceDescription.Height, lcd_state.deviceDescription.Bpp,
	//	lcd_state.deviceDescription.NumSoftButtons);
	loadend_printf(" LCD: %dx%d, %dbpp", lcd_state.deviceDescription.Width, lcd_state.deviceDescription.Height, lcd_state.deviceDescription.Bpp);

#undef HandleError
}

void gfxLCDDeinit(void)
{
#define HandleError(res, msg)	if (ERROR_SUCCESS != res) return;
	DWORD res;
	// let's close the device
	res = lgLcdClose(lcd_state.openContext.device);
	HandleError(res, "lgLcdClose");

	// and take down the connection
	res = lgLcdDisconnect(lcd_state.connectContext.connection);
	HandleError(res, "lgLcdDisconnect");

	// and shut down the library
	res = lgLcdDeInit();
	HandleError(res, "lgLcdDeInit");
}

void gfxLCDAddMeter(const char *name, F32 value, F32 minv, F32 maxv, U32 textcolor, U32 colormin, U32 colormid, U32 colormax)
{
	if (lcd_state.enabled) {
		F32 midv;
		LCDLine *line = MP_ALLOC(LCDLine);
		line->type = LCDLINE_METER;
		strcpy(line->text, name);
		line->value = value;
		line->minv = minv;
		line->maxv = maxv;
		line->textcolor = textcolor;
		midv = (maxv - minv)*0.5 + minv;
		if (value >= midv)
		{
			line->barcolor = lerpRGBAColors(colormid, colormax, (value - midv) / AVOID_DIV_0(maxv - midv));
		} else {
			line->barcolor = lerpRGBAColors(colormin, colormid, (value - minv) / AVOID_DIV_0(midv - minv));
		}
		eaPush(&lcd_state.lines, line);
	}
}

void gfxLCDAddText(const char *text, U32 color)
{
	if (lcd_state.enabled) {
		LCDLine *line = MP_ALLOC(LCDLine);
		line->type = LCDLINE_TEXT;
		line->textcolor = color;
		strcpy(line->text, text);
		eaPush(&lcd_state.lines, line);
	}
}

#define ISQVGA (lcd_state.openContext.deviceType == LGLCD_DEVICE_QVGA)
#define FONT_HEIGHT (ISQVGA?24:12)
#define LINE_HEIGHT (FONT_HEIGHT+(ISQVGA?4:2))

bool gfxLCDIsQVGA(void)
{
	return ISQVGA;
}

static void gfxLCDTextInit(void)
{
	static bool doneOnce=false;
	int nBMISize;
	int nColor;

	if (doneOnce)
		return;
	doneOnce = true;

	lcd_state.hFont = CreateFont(FONT_HEIGHT, FONT_HEIGHT * 8 / 16, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");

	lcd_state.hdc = CreateCompatibleDC(NULL);
	if(NULL == lcd_state.hdc)
	{
		lcd_state.enabled = false;
		return;
	}

	nBMISize = sizeof(BITMAPINFO) + 256 * sizeof(RGBQUAD);
	lcd_state.pBitmapInfo = (BITMAPINFO *) calloc(nBMISize, 1);
	if(NULL == lcd_state.pBitmapInfo)
	{
		lcd_state.enabled = false;
		return;
	}

	lcd_state.pBitmapInfo->bmiHeader.biSize = sizeof(lcd_state.pBitmapInfo->bmiHeader);
	if (lcd_state.openContext.deviceType == LGLCD_DEVICE_QVGA)
	{
		lcd_state.width = LGLCD_QVGA_BMP_WIDTH;
		lcd_state.height = LGLCD_QVGA_BMP_HEIGHT;
		lcd_state.pBitmapInfo->bmiHeader.biBitCount = LGLCD_QVGA_BMP_BPP*8;
	} else {
		lcd_state.width = LGLCD_BW_BMP_WIDTH;
		lcd_state.height = LGLCD_BW_BMP_HEIGHT;
		lcd_state.pBitmapInfo->bmiHeader.biBitCount = LGLCD_BW_BMP_BPP*8;

		lcd_state.pBitmapInfo->bmiHeader.biClrUsed = 256;
		lcd_state.pBitmapInfo->bmiHeader.biClrImportant = 256;
		for(nColor = 0; nColor < 256; ++nColor)
		{
			lcd_state.pBitmapInfo->bmiColors[nColor].rgbRed = (BYTE)((nColor > 128) ? 255 : 0);
			lcd_state.pBitmapInfo->bmiColors[nColor].rgbGreen = (BYTE)((nColor > 128) ? 255 : 0);
			lcd_state.pBitmapInfo->bmiColors[nColor].rgbBlue = (BYTE)((nColor > 128) ? 255 : 0);
			lcd_state.pBitmapInfo->bmiColors[nColor].rgbReserved = 0;
		}
	}
	lcd_state.pBitmapInfo->bmiHeader.biPlanes = 1;
	lcd_state.pBitmapInfo->bmiHeader.biWidth = lcd_state.width;
	lcd_state.pBitmapInfo->bmiHeader.biHeight = -lcd_state.height; // Why is this negative?
	lcd_state.pBitmapInfo->bmiHeader.biCompression = BI_RGB;
	lcd_state.pBitmapInfo->bmiHeader.biSizeImage = 
		(lcd_state.pBitmapInfo->bmiHeader.biWidth * 
		ABS(lcd_state.pBitmapInfo->bmiHeader.biHeight) * 
		lcd_state.pBitmapInfo->bmiHeader.biBitCount) / 8;
	lcd_state.pBitmapInfo->bmiHeader.biXPelsPerMeter = 3200;
	lcd_state.pBitmapInfo->bmiHeader.biYPelsPerMeter = 3200;

	lcd_state.hBitmap = CreateDIBSection(lcd_state.hdc, lcd_state.pBitmapInfo, DIB_RGB_COLORS, (PVOID *) &lcd_state.pBitmapBits, NULL, 0);
	assert(lcd_state.hBitmap);
}

static bool lcd_background_requested;
static BasicTexture *lcd_background_tex;
static U8 *lcd_background_data;
static bool lcd_background_reload_registered;

void gfxLCDBGReload(const char *relpath, int when)
{
	lcd_background_requested = 0; // Leaks memory
}

#define COLORREF_FROM_RGBA(rgba) RGB((rgba) >> 24, ((rgba) >> 16) & 0xff, ((rgba) >> 8) & 0xff)
#define LCD_COLOR(rgba) (ISQVGA?COLORREF_FROM_RGBA(rgba):RGB(253, 253, 253))
#pragma warning(disable:6262) // Stack size is 300k
static void gfxLCDUpdate(void)
{
	int i;
	int res;
	int y=0;
	BITMAPINFO bi={0};
	static int s_accum = 0;

	if (lcd_state.enabled && s_accum++ > GFX_LCD_SKIP_FRAMES)
	{
		gfxLCDTextInit();
		if (ISQVGA)
		{
			if (!lcd_background_requested)
			{
				lcd_background_tex = texLoadRawData("lcd_background", TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);
				lcd_background_requested = true;
				if (!lcd_background_reload_registered)
				{
					FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, lcd_background_tex->fullname, gfxLCDBGReload);
					lcd_background_reload_registered = true;
				}
			}
			if (lcd_background_tex)
			{
				if (lcd_background_tex->tex_is_loaded & RAW_DATA_BITMASK)
				{
					TexReadInfo *rawInfo = texGetRareData(lcd_background_tex->actualTexture)->bt_rawInfo;
					assert(rawInfo);
					verify(uncompressRawTexInfo(rawInfo,textureMipsReversed(lcd_background_tex->actualTexture)));

					if (rawInfo->width != LGLCD_QVGA_BMP_WIDTH ||
						rawInfo->height != LGLCD_QVGA_BMP_HEIGHT)
					{
						// bad!
						Errorf("LCD_Background texture is %dx%d, should be %dx%d", rawInfo->width, rawInfo->height, LGLCD_QVGA_BMP_WIDTH, LGLCD_QVGA_BMP_HEIGHT);
					} else {
						if (rawInfo->tex_format == RTEX_BGRA_U8)
						{
							// memdup instead of memRefIncrement so the memory blame is good (slightly slower)
							lcd_background_data = memdup(rawInfo->texture_data, rawInfo->width*rawInfo->height*4);
						}
						else if (rawInfo->tex_format == RTEX_BGR_U8)
						{
							int pixelNum;
							U8 *pIn = rawInfo->texture_data;
							U8 *pOut = lcd_background_data = calloc(rawInfo->width*rawInfo->height*4, 1);
							for (pixelNum = 0; pixelNum < rawInfo->width * rawInfo->height; pIn+=3, pOut+=4)
							{
								pOut[0] = pIn[0];
								pOut[1] = pIn[1];
								pOut[2] = pIn[2];
								pOut[3] = 0xFF;
							}
						}
						else
						{
							devassertmsg(0, "Unknown texture format for LCD_Background texture");
						}
					}
					texUnloadRawData(lcd_background_tex);
					lcd_background_tex = NULL;
				}
			}
		}

		s_accum = 0;
		PERFINFO_AUTO_START("Drawing lines", eaSize(&lcd_state.lines));
		SelectObject(lcd_state.hdc, lcd_state.hFont);
		SelectObject(lcd_state.hdc, lcd_state.hBitmap);
		SelectObject(lcd_state.hdc, GetStockObject(BLACK_PEN)); 
		SelectObject(lcd_state.hdc, GetStockObject(BLACK_BRUSH)); 
		if (ISQVGA && lcd_background_data)
		{
			memcpy(lcd_state.pBitmapBits, lcd_background_data, lcd_state.width * lcd_state.height * 4);
		} else {
			Rectangle(lcd_state.hdc, 0, 0, lcd_state.width, lcd_state.height);
		}

		SetBkColor(lcd_state.hdc, RGB(0, 0, 0));

		FOR_EACH_IN_EARRAY(lcd_state.lines, LCDLine, line)
		{
			SetBkMode(lcd_state.hdc, TRANSPARENT);
			SetTextColor(lcd_state.hdc, LCD_COLOR(line->textcolor));
			TextOut_UTF8(lcd_state.hdc, 0+(ISQVGA?4:0), y, line->text, (int)strlen(line->text));
			if (line->type == LCDLINE_METER) {
				int y0 = y;
				int y1 = y+LINE_HEIGHT - 1;
				int x0 = 60*(ISQVGA?2:1);
				int x1 = 159*(ISQVGA?2:1);
				F32 percent;
				char buf[160];
				HBRUSH hBrush;

				// Draw label/value
				SetBkMode(lcd_state.hdc, TRANSPARENT);
				SetTextColor(lcd_state.hdc, LCD_COLOR(line->textcolor));
				sprintf(buf, "%1.2f", line->value);
				TextOut_UTF8(lcd_state.hdc, x0 + 10, y + (ISQVGA?2:0), buf, (int)strlen(buf));

				// Draw border
				SelectObject(lcd_state.hdc, GetStockObject(HOLLOW_BRUSH)); 
				SelectObject(lcd_state.hdc, GetStockObject(WHITE_PEN)); 
				Rectangle(lcd_state.hdc, x0, y0, x1, y1);
				x0++;y0++;
				//x1--;y1--;

				// Draw filled in portion
				hBrush = CreateSolidBrush(LCD_COLOR(line->barcolor));
				percent = (line->value - line->minv) / AVOID_DIV_0(line->maxv - line->minv);
				percent = CLAMPF32(percent, 0, 1);
				x1 = x0 + percent * (x1 - x0);
				SelectObject(lcd_state.hdc, hBrush); // GetStockObject(WHITE_BRUSH)); 
				SelectObject(lcd_state.hdc, GetStockObject(NULL_PEN)); 
				SetROP2(lcd_state.hdc, R2_MASKPENNOT);
				Rectangle(lcd_state.hdc, x0, y0, x1, y1);
				SetROP2(lcd_state.hdc, R2_COPYPEN);
				DeleteObject(hBrush);
			}

			y+=LINE_HEIGHT;
		}
		FOR_EACH_END;
		
		PERFINFO_AUTO_STOP_START("GdiFlush", 1);
		GdiFlush();

		PERFINFO_AUTO_STOP_START("lgLcdUpdateBitmap", 1);
		if (lcd_state.openContext.deviceType == LGLCD_DEVICE_QVGA)
		{
			lgLcdBitmapQVGAx32 bmp;
			bmp.hdr.Format = LGLCD_BMP_FORMAT_QVGAx32;
			memcpy(&bmp.pixels, lcd_state.pBitmapBits, lcd_state.width * lcd_state.height * 4);

			res = lgLcdUpdateBitmap(lcd_state.openContext.device, &bmp.hdr, LGLCD_ASYNC_UPDATE(LGLCD_PRIORITY_NORMAL));
		} else {
			lgLcdBitmap160x43x1 bmp;
			bmp.hdr.Format = LGLCD_BMP_FORMAT_160x43x1;
			memcpy(&bmp.pixels, lcd_state.pBitmapBits, lcd_state.width * lcd_state.height);

			res = lgLcdUpdateBitmap(lcd_state.openContext.device, &bmp.hdr, LGLCD_ASYNC_UPDATE(LGLCD_PRIORITY_NORMAL));
		}
		PERFINFO_AUTO_STOP();
		//HandleError(res, _T("lgLcdUpdateBitmap"));

	}
	// Clear stuff
	for (i=eaSize(&lcd_state.lines)-1; i>=0; i--) 
		MP_FREE(LCDLine, lcd_state.lines[i]);
	eaClear(&lcd_state.lines);
}

void gfxLCDOncePerFrame(void)
{
	if (!lcd_state.enabled)
		return;
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	gfxLCDUpdate();
	PERFINFO_AUTO_STOP();
}



#endif
