#include "GraphicsLibPrivate.h"
#include "GfxPerformance.h"
#include "GfxPrimitive.h"
#include "GfxFont.h"
#include "GfxSpriteText.h"
#include "inputMouse.h"
#include "inputLib.h"
#include "StashTable.h"
#include "strings_opt.h"
#include "StringUtil.h"
#include "ScratchStack.h"
#include "ThreadManager.h"
#include "Prefs.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

// JE: This has a bunch of functions that used to be in entDebug, they
//  should probably be cleaned up a bit and moved into some general
//  debug printing functions (or unified with how we normally print/format
//  text/buttons/etc, as they're just making UI sprite calls now anyway)

#define ALIGN_BOTTOM	0
#define ALIGN_VCENTER	(1 << 0)
#define ALIGN_TOP		(1 << 1)
#define ALIGN_LEFT		0
#define ALIGN_HCENTER	(1 << 2)
#define	ALIGN_RIGHT		(1 << 3)

#define PERFINFO_LINE_Z	(GRAPHICSLIB_Z + 2000.f)
#define PERFINFO_Z (GRAPHICSLIB_Z + 10)
#define PERFINOF_MAIN_W 750

static void parseDebugCommands(char* commands)
{
	char* str;
	char* cur;
	char delim;
	char buffer[10000];

	strcpy(buffer, commands);

	cur = buffer;

	for(str = strsep2(&cur, "\n", &delim); str; str = strsep2(&cur, "\n", &delim)){
		globCmdParse(str);
		if(delim){
			cur[-1] = delim;
		}
	}
}


static GfxFont* getDebugFont()
{
	static GfxFont* debugFont = NULL;

	if(!debugFont){

		debugFont = gfxFontCreateFromName("Fonts/Vera.font");
		assert(debugFont);
		debugFont->renderSize = 11;
		debugFont->outlineWidth = 1;
	}

	return debugFont;
}

void printDebugString(	char* outputString,
						int x,
						int y,
						float scale,
						int lineHeight,
						int overridecolor,
						int defaultcolor,
						U8 alpha,
						int* getWidthHeightBuffer)
{
	GfxFont* debugFont = getDebugFont();
	unsigned int curColor[4];
	unsigned int curColorARGB;
	int tabWidth = max(350 * scale, 1);
	char* cur = outputString;
	int i;
	char* str;
	char delim;
	int colorCount;
	int doDraw = getWidthHeightBuffer ? 0 : 1;
	char *outputStringCopy = NULL;
	float fontScale = 1;
	int w, h;

	unsigned int colors[] = {
		0xeef8ff00,		// ^0 WHITE
		0xff99aa00,		// ^1 PINK
		0x88ff9900,		// ^2 GREEN	 
		0xeebbbb00,		// ^3 LIGHT PINK	  
		0xf9ab7700,		// ^4 ORANGE	
		0x43b6fa00, 	// ^5 BLUE	
		0xff945200,		// ^6 DARK ORANGE	 
		0xffff3200,		// ^7 YELLOW	 
		0x68ffff00,		// ^8 LIGHT BLUE	 
		0xccccff00,		// ^9 PURPLE	
		0xffde8c00,		
	};
	
	if(!outputString)
		return;

	gfxGetActiveSurfaceSizeInline(&w, &h);

	outputStringCopy = ScratchAlloc(20000);
	if(strlen(outputString) > ScratchSize(outputStringCopy) - 1){
		cur = outputString;
	}else{
		strcpy_s(outputStringCopy, ScratchSize(outputStringCopy), outputString);
		cur = outputStringCopy;
	}

	if(tabWidth <= 0)
		tabWidth = 1;

	colorCount = ARRAY_SIZE(colors);
	overridecolor = max(-1, min(overridecolor, colorCount - 1));
	defaultcolor = max(0, min(defaultcolor, colorCount - 1));

	if(getWidthHeightBuffer){
		getWidthHeightBuffer[0] = 0;
		getWidthHeightBuffer[1] = 0;
	}

	for(i = 0, str = strsep2(&cur, "\n", &delim); str; str = strsep2(&cur, "\n", &delim), i++) {
		char* str2;
		char delim2;
		int xofs = 0;
		unsigned int highlightColorARGB = 0;
		int y0 = y + i * lineHeight * scale;
		int y1 = y + (i+1) * lineHeight * scale;

		{
			int j;
			curColorARGB = colors[overridecolor >= 0 ? overridecolor : defaultcolor] | alpha;
			for(j = 0; j < 4; j++)
				curColor[j] = curColorARGB;
			curColorARGB = (curColorARGB >> 8) | (curColorARGB << 24);
		}

		for(str2 = strsep2(&str, "^", &delim2); str2; str2 = strsep2(&str, "^", &delim2)){
			char* str3;
			char delim3;

			for(str3 = strsep2(&str2, "\t", &delim3); str3; str3 = strsep2(&str2, "\t", &delim3)){
				wchar_t buffer[1000];
				F32 width = 0;
				int length;

				length = UTF8ToWideStrConvert(str3, buffer, ARRAY_SIZE(buffer)-1);

				buffer[length] = '\0';

				if (y < 0 && doDraw)
					continue;

				gfxfont_Dimensions(debugFont, scale * fontScale, scale * fontScale, str3, length, &width, NULL, NULL, true);

				if(doDraw){
					if(highlightColorARGB){
						gfxDrawQuadARGB(	x + xofs,
										y0 + 2 - lineHeight * scale,
										x + xofs + width,
										y1 + 2 - lineHeight * scale,
										PERFINFO_Z,
										highlightColorARGB);
					}

					gfxfont_PrintEx(debugFont,
												x + xofs,
												y0,
												PERFINFO_LINE_Z,
												scale * fontScale,
												scale * fontScale,
												0,
												str3,
												(int)wcslen(buffer), (int*)curColor, NULL);
				}

				xofs += width;

				if(delim3){
					int old_xofs = xofs;

					str2[-1] = delim3;

					xofs = ((xofs + tabWidth) / tabWidth) * tabWidth;

					if(doDraw && highlightColorARGB){
						gfxDrawQuadARGB(	x + old_xofs,
										y0 + 2 - lineHeight * scale,
										x + xofs,
										y1 + 2 - lineHeight * scale,
										PERFINFO_Z,
										highlightColorARGB);
					}
				}
			}

			if(delim2){
				int code = tolower(*str);

				str[-1] = delim2;

				if(code >= '0' && code <= '9'){
					int idx = *str - '0';
					if(overridecolor < 0){
						int j;
						curColorARGB = colors[idx] | alpha;
						for(j = 0; j < 4; j++)
							curColor[j] = curColorARGB;
						curColorARGB = (curColorARGB >> 8) | (curColorARGB << 24);
					}

					str++;
				}
				else if(code == '#'){
					// RGB coloring in the form ^#(r,g,b)
					// So, the string ^#(255,0,0) will change the text to red
					int r, g, b, num;
					num = sscanf(str+1, "(%d,%d,%d)", &r, &g, &b);
					if (num == 3) {
						if(overridecolor < 0){
							int j;
							curColorARGB = (b<<8) | (g<<16) | (r<<24) | alpha;
							for(j = 0; j < 4; j++)
								curColor[j] = curColorARGB;
							curColorARGB = (curColorARGB >> 8) | (curColorARGB << 24);
						}
						str=strchr(str, ')');
						assert(str);
						str++;
					}
				}
				else if(code == 'd'){
					if(overridecolor < 0){
						int j;
						curColorARGB = colors[overridecolor >= 0 ? overridecolor : defaultcolor] | alpha;
						for(j = 0; j < 4; j++)
							curColor[j] = curColorARGB;
						curColorARGB = (curColorARGB >> 8) | (curColorARGB << 24);
					}

					str++;
				}
				else if(code == 'h'){
					highlightColorARGB = (((curColorARGB >> 24) / 2) << 24) | (curColorARGB & 0xffffff);
					str++;
				}
				else if(code == 't'){
					char buffer[100];
					char* curpos = buffer;
					for(str++; *str && tolower(*str) != 't' && curpos - buffer < ARRAY_SIZE(buffer) - 1; str++)
						*curpos++ = *str;
					*curpos = '\0';
					if(code == 't')
						str++;

					tabWidth = atoi(buffer) * scale;

					if(tabWidth <= 0)
						tabWidth = 1;
				}
				else if(code == 'r'){
					if(overridecolor < 0){
						int j;
						curColorARGB =	0x000000ff |
										(0xff - (((gfx_state.frame_count + 0) * 10) & 0x7f)) << 24 |
										(0x80 | (((gfx_state.frame_count + 0) * 10) & 0x7f)) << 16;
						for(j = 0; j < 4; j++)
							curColor[j] = curColorARGB;
						curColorARGB = (curColorARGB >> 8) | (curColorARGB << 24);
					}

					str++;
				}
				else if(code == 'b'){
					int dx, dx2;
					char buffer[100];
					char buffer2[100];
					char* curpos = buffer;
					buffer2[0] = 0;
					for(str++; *str && tolower(*str) != 'b' && tolower(*str) != '/' && curpos - buffer < ARRAY_SIZE(buffer) - 1; str++)
						*curpos++ = *str;
					*curpos = '\0';
					if(tolower(*str) == '/'){
						curpos = buffer2;
						for(str++; *str && tolower(*str) != 'b' && curpos - buffer2 < ARRAY_SIZE(buffer2) - 1; str++)
							*curpos++ = *str;
						*curpos = '\0';
					}

					if(tolower(*str) == 'b'){
						str++;
					}

					dx = atoi(buffer) * scale;

					if(dx < 0)
						dx = 0;

					if(atoi(buffer2) > 0){
						dx2 = atoi(buffer2) * scale;
						if(dx > dx2)
							dx = dx2;
					}else{
						dx2 = dx;
					}

					if(doDraw){
						if(highlightColorARGB){
							gfxDrawQuadARGB(	x + xofs,
											y0 + 2 - lineHeight * scale,
											x + xofs + dx2 + 4,
											y1 + 2 - lineHeight * scale,
											PERFINFO_Z,
											highlightColorARGB);
						}

						gfxDrawQuadARGB(	x + xofs,
										y0 - 1,
										x + xofs + dx2 + 4,
										y - (i - 1) * lineHeight * scale - 4,
										PERFINFO_Z,
										(alpha << 24) | 0xcccccc);

						gfxDrawQuadARGB(	x + xofs + 1,
										y0,
										x + xofs + dx2 + 3,
										y - (i - 1) * lineHeight * scale - 5,
										PERFINFO_Z,
										(alpha << 24) | 0x000000);

						gfxDrawQuadARGB(	x + xofs + 2,
										y0 + 1,
										x + xofs + dx + 2,
										y - (i - 1) * lineHeight * scale - 6,
										PERFINFO_Z,
										(curColorARGB & 0x00ffffff) | ((alpha / 2) << 24));

						if(dx > 2){
							gfxDrawQuadARGB(	x + xofs + 3,
											y0 + 2,
											x + xofs + dx + 1,
											y - (i - 1) * lineHeight * scale - 7,
											PERFINFO_Z,
											curColorARGB);
						}

						if(dx < dx2){
							gfxDrawQuadARGB(	x + xofs + dx + 2,
											y0 + 1,
											x + xofs + dx2 + 2,
											y - (i - 1) * lineHeight * scale - 6,
											PERFINFO_Z,
											(alpha << 24) | 0x334433);
						}
					}

					xofs += dx2 + 4;
				}
				else if(code == 'l'){
					int dx;
					char buffer[100];
					char* curpos = buffer;
					for(str++; *str && tolower(*str) != 'l' && curpos - buffer < ARRAY_SIZE(buffer) - 1; str++)
						*curpos++ = *str;
					*curpos = '\0';

					if(tolower(*str) == 'l'){
						str++;
					}

					dx = atoi(buffer) * scale;

					if(doDraw){
						gfxDrawQuadARGB(	x + xofs,
										y0 + scale * (lineHeight / 2 - 3) - 1,
										x + xofs + dx + 2,
										y0 + scale * (lineHeight / 2 - 2) + 1,
										PERFINFO_Z,
										curColorARGB & 0xff000000);

						gfxDrawQuadARGB(	x + xofs + 1,
										y0 + scale * (lineHeight / 2 - 3),
										x + xofs + dx + 1,
										y0 + scale * (lineHeight / 2 - 2),
										PERFINFO_Z,
										curColorARGB);
					}

					xofs += dx + 2;
				}
				else if(code == 's'){
					fontScale = 0.7;

					str++;
				}
				else if(code == 'n'){
					fontScale = 1;

					str++;
				}
			}
		}

		if(getWidthHeightBuffer){
			if(xofs > getWidthHeightBuffer[0])
				getWidthHeightBuffer[0] = xofs;
			getWidthHeightBuffer[1] += lineHeight * scale;
		}

		if(delim)
			cur[-1] = delim;
	}

	ScratchFree(outputStringCopy);
}

void printDebugStringAlign(char* outputString,
					  int x,
					  int y,
					  int dx,
					  int dy,
					  int align,
					  float scale,
					  int lineHeight,
					  int overridecolor,
					  int defaultcolor,
					  U8 alpha)
{
	int widthheight[2];
	printDebugString(outputString, x, y, scale, lineHeight, overridecolor, defaultcolor, alpha, widthheight);
	if (ALIGN_HCENTER & align)
	{
		x = x + (dx - widthheight[0]) / 2;
	}
	else if (ALIGN_RIGHT & align)
	{
		x = x + dx - widthheight[0];
	}

	if (ALIGN_VCENTER & align)
	{
		y = y + (dy - widthheight[1]) / 2;
	}
	else if (ALIGN_TOP & align)
	{
		y = y + dy - widthheight[1];
	}
	printDebugString(outputString, x, y, scale, lineHeight, overridecolor, defaultcolor, alpha, 0);
}


static int mouseOverLastButtonState;

int mouseOverLastButton(){
	return mouseOverLastButtonState;
}

int drawButton2D(int x1, int y1, int dx, int dy, int centered, char* text, float scale, char* command, int* setMe){
	int w, h;
	int mouse_x, mouse_y;
	int over = 0;
	int x2 = x1 + dx;
	int y2 = y1 + dy;
	int widthHeight[2];
	
	mouseOverLastButtonState = 0;

	gfxGetActiveSurfaceSizeInline(&w,&h);

	if(x1 > w || x2 < 0 || y1 > h || y2 < 0)
		return 0;

	mousePos(&mouse_x, &mouse_y);

	mouse_y = mouse_y;

	if(!mouseIsLocked() && (mouse_x >= x1 && mouse_x < x2 && mouse_y >= y1 && mouse_y < y2)){
		over = 1;
		mouseOverLastButtonState = 1;
	}

	gfxDrawQuadARGB(x1, y1, x2, y2, PERFINFO_Z, (0xd0 << 24) | (over ? 0x7f + abs(0x80 - ((gfx_state.frame_count * 10) & 0xff)) / 2 : 0x223322));
	gfxDrawQuadARGB(x1, y1, x1 + 1, y2 + 1, PERFINFO_Z, 0xff888888);
	gfxDrawQuadARGB(x1, y1, x2 + 1, y1 + 1, PERFINFO_Z, 0xff888888);
	gfxDrawQuadARGB(x2, y1, x2 + 1, y2 + 1, PERFINFO_Z, 0xff888888);
	gfxDrawQuadARGB(x1, y2, x2 + 1, y2 + 1, PERFINFO_Z, 0xff888888);

	if(over){
		gfxDrawQuadARGB(x1 + 1, y1 + 1, x1 + 2, y2,	PERFINFO_Z, 0xffffff00);
		gfxDrawQuadARGB(x1 + 1, y1 + 1, x2, y1 + 2,	PERFINFO_Z, 0xffffff00);
		gfxDrawQuadARGB(x2 - 1, y1 + 1, x2, y2,		PERFINFO_Z, 0xffffff00);
		gfxDrawQuadARGB(x1 + 1, y2 - 1, x2, y2,		PERFINFO_Z, 0xffffff00);
	}

	if(centered){
		printDebugString(text, 0, 0, scale, 11, -1, -1, 255, widthHeight);
		
		printDebugString(	text,
							x1 + (x2 - x1 - widthHeight[0]) / 2,
							y1 + (y2 - y1 - widthHeight[1]) / 2 + widthHeight[1]/* - scale * 9*/,
							scale,
							11,
							over ? 7 : -1,
							-1,
							255,
							NULL);
	}else{
		printDebugString(text, 0, 0, scale, 7, -1, -1, 255, widthHeight);

		printDebugString(	text,
							x1 + 3,
							y1 + (y2 - y1 - widthHeight[1]) / 2,
							scale,
							11,
							over ? 7 : -1,
							-1,
							255,
							NULL);
	}

	if(over && !mouseIsLocked()){
		if (mouseDown(MS_LEFT))
		{
			if(command)
				parseDebugCommands(command);

			if(setMe)
				*setMe = 1;

			return 1;
		}
	}

	return 0;
}

static int drawDefaultButton2D(int x1, int y1, int dx, int dy, char* text){
	return drawButton2D(x1, y1, dx, dy, 1, text, 1, NULL, NULL);
}

typedef struct ThreadPerfDrawData
{
	int x;
	int y;
	StashTable interestingThreads;
} ThreadPerfDrawData;

static int last_framecount=1;
static F32 last_time=1;

#define THREAD_LINE_HEIGHT 20
#define THREAD_SCALE (THREAD_LINE_HEIGHT/16.f)
#define THREAD_NAME_WIDTH (220 * THREAD_SCALE)
#define THREAD_TOTAL_WIDTH (THREAD_NAME_WIDTH + 260*THREAD_SCALE)

static void diplayThreadPerf(ManagedThread *thread_ptr, ThreadPerfDrawData *data)
{
	ManagedThreadPerformance *perf = tmGetThreadPerformance(thread_ptr);
	char buf[1024];
	F32 percent = (F32)((perf->kernelTimeDelta + perf->userTimeDelta) * 100 / last_time);
	bool interesting = false;
	if (percent >= 1) {
		stashAddInt(data->interestingThreads, thread_ptr, gfx_state.client_frame_timestamp, true);
		interesting = true;
	} else if (stashFindElement(data->interestingThreads, thread_ptr, NULL)) {
		interesting = true;
	}
	if (!interesting && !gfx_state.debug.threadPerfAll)
		return;
	sprintf(buf, "%s (%d):", tmGetThreadName(thread_ptr), tmGetThreadId(thread_ptr));
	printDebugStringAlign(buf,
		data->x, data->y, THREAD_NAME_WIDTH, 16, ALIGN_RIGHT, 
		THREAD_SCALE, THREAD_LINE_HEIGHT, -1, -1, 255);
	sprintf(buf, "%4.1f%% (Kernel: %4.1fms/f  User: %4.1fms/f)", percent,  (F32)(perf->kernelTimeDelta*1000)/last_framecount, (F32)(perf->userTimeDelta*1000)/last_framecount);
	printDebugString(buf,
		data->x + THREAD_NAME_WIDTH + 4, data->y,
		THREAD_SCALE, THREAD_LINE_HEIGHT, -1, -1, 255, NULL);
	data->y -= THREAD_LINE_HEIGHT;
}

void gfxDisplayThreadPerformance(void)
{
	static ThreadPerfDrawData data = {0};
	int y0;

	static U32 last_thread_ticks;
	static U32 thread_framecount=0;
	U32	delta;
	F32 time;
	delta = gfx_state.client_frame_timestamp - last_thread_ticks;
	time = (F32)delta / (F32)timerCpuSpeed();
	if (time > 1.f)
	{
		tmUpdateAllPerformance(gfx_state.debug.threadPerf == 1);
		gfx_state.debug.threadPerf = 2;
		last_framecount = thread_framecount;
		last_time = time;
		thread_framecount = 0;
		last_thread_ticks = gfx_state.client_frame_timestamp;
	}
	thread_framecount++;

	gfxGetActiveSurfaceSize(&data.x, &data.y);

	data.x = 10;
	y0 = data.y -= 70;
	if (!data.interestingThreads)
		data.interestingThreads = stashTableCreateAddress(32);
	tmForEachThread(diplayThreadPerf, &data);
#if 1
	gfxDrawQuadARGB( data.x,
		y0+2,
		data.x + THREAD_TOTAL_WIDTH,
		data.y - 2,
		PERFINFO_Z-1,
		0x9F000000);
#endif
}
