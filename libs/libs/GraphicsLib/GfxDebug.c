#include "GfxDebug.h"
#include "UnitSpec.h"
#include "MemRef.h"
#include "utilitiesLib.h"
#include "net/netpacketutil.h"
#include "qsortG.h"
#include "winutil.h"
#include "StringUtil.h"
#include "CrypticDXT.h"
#include "structHist.h"
#include "fileLoader.h"
#include "fileLoaderStats.h"
#include "osdependent.h"
#include "SimpleParser.h"

#include "RdrShader.h"
#include "RdrState.h"

#include "GfxSpriteText.h"
#include "GfxFont.h"
#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxSprite.h"
#include "GfxPostprocess.h"
#include "GfxPerformance.h"
#include "GfxConsole.h"
#include "GenericMesh.h"
#include "GfxPrimitive.h"
#include "GfxTextures.h"
#include "GfxGeo.h"
#include "GfxSurface.h"
#include "GfxImposter.h"
#include "GfxOcclusion.h"
#include "texWords.h"
#include "GfxNVPerf.h"
#include "GfxTexturesInline.h"
#include "GfxTexAtlas.h"
#include "GfxTexAtlasPrivate.h"
#include "GfxCommandParse.h"

#include "dynFxInfo.h"
#include "dynFxDebug.h"
#include "dynFxParticle.h"
#include "dynFxFastParticle.h"
#include "dynNodeInline.h"
#include "WorldLib.h"
#include "dynCloth.h"
#include "dynWind.h"
#include "dynAnimGraphPub.h"
#include "dynFxInterface.h"
#include "wlCostume.h"
#include "dynRagdollData.h"
#include "wlTime.h"
#include "../StaticWorld/WorldCell.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "WorldColl.h"
#include "PhysicsSDK.h"
#include "partition_enums.h"
#include "wlPerf.h"
#include "bounds.h"

#include "GfxDebug_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

void gfxDebugCheckFrameGrab(void);

static bool s_bLessAnnoyingAccessLevel;
AUTO_CMD_INT(s_bLessAnnoyingAccessLevel, ShowLessAnnoyingAccessLevelWarning) ACMD_CMDLINE;

static bool s_bPrintAccessLevelWarnings;
AUTO_CMD_INT(s_bPrintAccessLevelWarnings, PrintAccessLevelWarnings) ACMD_CMDLINE;


static struct {
	int index;
	char line[10][1000];
	int timer;
	CRITICAL_SECTION crit_sect;
} gfx_status_line;

AUTO_RUN;
void gfxDebugInit(void)
{
	InitializeCriticalSection(&gfx_status_line.crit_sect);
	initStuffBuff(&gfx_state.debug.queued_debug_text,100);
}


static void statusLineRemove()
{
	assert(gfx_status_line.index);
	memmove(&gfx_status_line.line[0], &gfx_status_line.line[1], sizeof(gfx_status_line.line[0])*(ARRAY_SIZE(gfx_status_line.line)-1));
	timerStart(gfx_status_line.timer);
	gfx_status_line.index--;
}

#undef gfxStatusPrintf
int gfxStatusPrintf(char const *fmt, ...)
{
	int ret;
	va_list ap;
	EnterCriticalSection(&gfx_status_line.crit_sect);

	if (!gfx_status_line.timer)
		gfx_status_line.timer = timerAlloc();
	if (gfx_status_line.index==ARRAY_SIZE(gfx_status_line.line)) {
		statusLineRemove();
	}

	assert(gfx_status_line.index < ARRAY_SIZE(gfx_status_line.line));

	va_start(ap, fmt);
	ret = Vsprintf(gfx_status_line.line[gfx_status_line.index], fmt, ap);
	va_end(ap);
	// Replace bad characters (bad because they'll cause gulim.ttf to get loaded!)
	strchrReplace(gfx_status_line.line[gfx_status_line.index], '\n', ' ');
	strchrReplace(gfx_status_line.line[gfx_status_line.index], '\t', ' ');
	gfx_status_line.index++;
	if (gfx_status_line.index==1)
		timerStart(gfx_status_line.timer);

	LeaveCriticalSection(&gfx_status_line.crit_sect);
	return ret;
}

// Displays a line of text in the status line (useful for debugging scripts, etc)
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug, Graphics) ACMD_HIDE;
void gfxStatusPrint(const char *str)
{
	gfxStatusPrintf("%s", str);
}

// Uncomment the debug_texLoadCheck auto command when enabling this debug feature.
#define DEBUG_TEXLOADCHECK 0
#if DEBUG_TEXLOADCHECK
char debug_texLoadCheck[ 256 ] = "";
// Displays loading state for texture, and thumbnail visualization of MIP 0, 1, and 9 to show state of loaded data.
//AUTO_COMMAND ACMD_NAME(debug_texLoadCheck) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug, Graphics) ACMD_HIDE;
char *gfxDebugTexLoadCheckCmd(ACMD_NAMELIST(g_basicTextures_ht, STASHTABLE) const char * debugTex)
{
	strcpy(debug_texLoadCheck, debugTex);
	return debug_texLoadCheck;
}

void gfxDebugShowTexLoad()
{
	BasicTexture * debug_tex = NULL;
	int y = 1;
	if (!debug_texLoadCheck[0])
		return;
	debug_tex = texFind(debug_texLoadCheck, WL_FOR_UTIL);
	if (!debug_tex)
		return;
	texEnableDetailedLifetimeLogIfAvailable(debug_tex);
	gfxXYprintf(1, y++, "%s %s. %s.", debug_texLoadCheck, debug_tex->loaded_data ? ( !debug_tex->loaded_data->loading ? "Loaded" : "Loading" ) : "Not loaded", 
		debug_tex->loaded_data ? "Has loaded" : "Has not loaded");
	if (debug_tex->loaded_data)
		gfxXYprintf(1, y++, "%s. Low mipped on %x. Loading for %x. %s", debug_tex->loaded_data->loading ? "Loading" : "", debug_tex->loaded_data->mip_bound_on, 
			debug_tex->loaded_data->tex_is_loading_for, texIsFullyLoadedInline(debug_tex) ? "Fully loaded!" : "Not ready!");
	gfxDebugThumbnailsAddTexture(debug_tex, debug_texLoadCheck, false, 0);
	gfxDebugThumbnailsAddTexture(debug_tex, debug_texLoadCheck, false, 1);
	gfxDebugThumbnailsAddTexture(debug_tex, debug_texLoadCheck, false, 2);
	gfxDebugThumbnailsAddTexture(debug_tex, debug_texLoadCheck, false, 9);
}
#endif

void gfxStatusLineDraw(void)
{
	int w, h;
	F32 sprite_h = 14.f;
	F32 timer_elapsed;
	AtlasTex *white;
	int rgba[4] = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
#define FADE_IN 0.25f
#define FADE_OUT 0.75f
#define QUEUED_DURATION 0.5f
#define TOTAL_DURATION 10.f
	EnterCriticalSection(&gfx_status_line.crit_sect);
	if (gfx_status_line.timer && gfx_status_line.index)
	{
		timer_elapsed=timerElapsed(gfx_status_line.timer);
		if (gfx_status_line.index > 1) {
			// Multiple lines queued up
			if (timer_elapsed > QUEUED_DURATION) {
				timerStart(gfx_status_line.timer);
				timer_elapsed = 0;
				statusLineRemove();
			}
		}
		if (timer_elapsed <= TOTAL_DURATION)
		{
			if (timer_elapsed < FADE_IN) {
				sprite_h*=timer_elapsed/FADE_IN;
			} else if (timer_elapsed>TOTAL_DURATION - FADE_OUT) {
				sprite_h*=(TOTAL_DURATION - timer_elapsed) / FADE_OUT;
			}
			white = atlasLoadTexture("white");
			gfxGetActiveSurfaceSizeInline(&w,&h);
			display_sprite(white, 0, (F32)(h - sprite_h), GRAPHICSLIB_Z, (F32)(w / white->width), (F32)(sprite_h / white->height), 0x3f3f3fA0);

			gfxfont_PrintEx(&g_font_Sans, 0, h + 13-sprite_h, GRAPHICSLIB_Z+1, 1, 1, 0, gfx_status_line.line[0], (int)strlen(gfx_status_line.line[0]), rgba, NULL);
		} else {
			statusLineRemove();
		}
	}

	LeaveCriticalSection(&gfx_status_line.crit_sect);
}

int gfxXYgetX(int x)
{
	int j1=0;

	if (x > TEXT_JUSTIFY/2)
		x = ((j1=x) - TEXT_JUSTIFY);
	x *= 8;
	if (j1) {
		x = gfx_activeSurfaceSize[0] - (640 - x);
		x -= gfx_state.debug.offsetFromRight;
	} else {
		x += gfx_state.debug.offsetFromLeft;
	}
	return x;
}

int gfxXYgetY(int y)
{
	int j2=0;

	if (y > TEXT_JUSTIFY/2)
		y = ((j2=y) - TEXT_JUSTIFY);
	y *= 12;
	if (j2) {
		y = gfx_activeSurfaceSize[1] - (720 - y);
		y -= gfx_state.debug.offsetFromBottom;
	} else {
		y+=gfx_state.debug.offsetFromTop;
	}
	return y;
}

static char * gfx_printf_buf = NULL;
// Print text using location in same floating-point pixel coordinate system as sprites - not the emulated console character coordinates.
void gfxXYZFprintfColor_s(F32 x, F32 y, F32 z, Color color, char const *fmt, ...)
{
	va_list ap;
	int clr = color.a | (color.r << 24) | (color.g << 16) | (color.b << 8);

	PERFINFO_AUTO_START_FUNC();
		gfxfont_SetFontEx(&g_font_Mono, 0, 0, 1, 0, clr, clr);
		va_start(ap, fmt);
		estrClear(&gfx_printf_buf);
		estrConcatfv(&gfx_printf_buf, fmt, ap);
		va_end(ap);
		// Do some parsing for carriage returns and tabs
		gfxfont_PrintMultiline(x, y, z, 1, 1, gfx_printf_buf);
	PERFINFO_AUTO_STOP();
}

// To Right justify, pass in TEXT_JUSTIFY + x/y, where x is 80 at the right side of the screen, and y is 60 at the bottom
void gfxXYZprintfColorInternal(int x, int y, int z, U32 color, char const *fmt, va_list ap)
{
	PERFINFO_AUTO_START_FUNC();
	x = gfxXYgetX(x);
	y = gfxXYgetY(y)+4;

	gfxfont_SetFontEx(&g_font_Mono, 0, 0, 1, 0, color, color);
	estrClear(&gfx_printf_buf);
	estrConcatfv(&gfx_printf_buf, fmt, ap);

	// Do some parsing for carriage returns and tabs
	gfxfont_PrintMultiline((float)x, (float)y, (float)z, 1, 1, gfx_printf_buf);
	PERFINFO_AUTO_STOP();
}

// To Right justify, pass in TEXT_JUSTIFY + x/y, where x is 80 at the right side of the screen, and y is 60 at the bottom
void gfxXYZprintfColor_s(int x, int y, int z, U8 r, U8 g, U8 b, U8 a, char const *fmt, ...)
{
	va_list ap;
	int clr = a | (r << 24) | (g << 16) | (b << 8);

	va_start(ap, fmt);
	gfxXYZprintfColorInternal(x,y,z,clr,fmt,ap);
	va_end(ap);
}

// To Right justify, pass in TEXT_JUSTIFY + x/y, where x is 80 at the right side of the screen, and y is 60 at the bottom
void gfxXYZprintfColor2_s(int x, int y, int z, U32 color, char const *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	gfxXYZprintfColorInternal(x,y,z,color,fmt,ap);
	va_end(ap);
}

void gfxDebugPrintfQueueInternal(U32 color,char const *fmt, va_list ap)
{
	char term = 0;
	int iEntryLoc = gfx_state.debug.queued_debug_text.idx;
	int iStringLoc;
	GfxDebugTextEntry entry;
	GfxDebugTextEntry * pEntry;
	entry.iColor = color;
	entry.iLength = 0;
	addBinaryDataToStuffBuff(&gfx_state.debug.queued_debug_text, (char const *)&entry, sizeof(entry));

	iStringLoc = gfx_state.debug.queued_debug_text.idx;
	addStringToStuffBuff_fv(&gfx_state.debug.queued_debug_text, fmt, ap);
	va_end(ap);

	// fix up the length in the entry
	pEntry = (GfxDebugTextEntry *)&gfx_state.debug.queued_debug_text.buff[iEntryLoc];
	pEntry->iLength = gfx_state.debug.queued_debug_text.idx - iStringLoc - 1;

	gfx_state.debug.queued_debug_text_count++;
}

#undef gfxDebugPrintfQueueColor
void gfxDebugPrintfQueueColor(U32 color,char const *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	gfxDebugPrintfQueueInternal(color,fmt,ap);
	va_end(ap);
}

#undef gfxDebugPrintfQueue
void gfxDebugPrintfQueue(char const *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	gfxDebugPrintfQueueInternal(0xffffffff,fmt,ap);
	va_end(ap);
}

#define LOG_SIZE 5000
static char mylog[LOG_SIZE];
static int myloglength = 0;
static int numlogentries = 0;
static int loginit = 0;
static int logpriority = -1;
void gfxDisplayScreenLog(int display)
{
	char * s;
	int i = 0;
	char temp[LOG_SIZE];

	while(numlogentries > 20)
	{
		s = strchr( mylog, '\n' );
		s++;
		strncpy(temp, s, LOG_SIZE);
		strncpy( mylog, temp, LOG_SIZE );
		numlogentries--;
	}

	if( display )
	{
		char * t;

		if(numlogentries)
		{
			s = mylog;
			gfxXYprintf( 1, 24 + i + TEXT_JUSTIFY, "'.' to clear, '/' to disable" );
			do
			{
				t = strchr(s, '\n');
				if( t )
				{
					*t = '\0';
					gfxXYprintf( 1, 25 + i + TEXT_JUSTIFY, "%s", s);
					*t = '\n';
				}
				s = (t + 1);
				i++;
			} 
			while ( t );
		}

	}

}

void gfxClearScreenLog(void)
{
	memset( mylog, 0, sizeof(char) * LOG_SIZE );
	numlogentries = 0;
	myloglength = 0;

}

void gfxSetScreenLogPriority(int min_message_priority)
{
	logpriority = min_message_priority;
}

#undef gfxPrintToScreenLog
void gfxPrintToScreenLog(int message_priority, char const *fmt, ...)
{
	char str[2000];
	va_list ap;

	if (message_priority <= logpriority)
		return ;

	if(!loginit)
	{ 
		gfxClearScreenLog();
		loginit = 1;
	}
	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	assert( strlen(str) < 2000 );
	strcat(str, "\n");

	printf("%s",str);
	if( strlen(str) >= 300 )
	{
		str[299] = 0;
	}

	numlogentries++;
	if( numlogentries > 30 || (myloglength + strlen(str) >= LOG_SIZE ))
	{
		gfxClearScreenLog();
		gfxPrintToScreenLog(0, "Log Overflow, Cleared Log");
	}

	myloglength += (int)strlen(str);

	strcat( mylog, str );
	{
		static	FILE	*file;

		if (!file)
			file = fopen("c:\\client_warnings.txt","wt");
		if (file)
		{
			fprintf(file,"%s",str);
			fflush(file);
		}
	}
}


void gfxDebugThumbnailsReset(void)
{
	int i;
	for (i=0; i<gfx_state.debug.thumbnail.thumbnails_count; i++) {
		if (gfx_state.debug.thumbnail.thumbnails[i].temp_surface) {
			gfxReleaseTempSurface(gfx_state.debug.thumbnail.thumbnails[i].temp_surface);
			gfx_state.debug.thumbnail.thumbnails[i].temp_surface = NULL;
		}
	}
	gfx_state.debug.thumbnail.thumbnails_count = 0;
	if (gfx_state.debug.thumbnail.strScreenshotRequestTitle)
	{
		estrDestroy(&gfx_state.debug.thumbnail.strScreenshotFilename);
		estrDestroy(&gfx_state.debug.thumbnail.strScreenshotRequestTitle);
		gfx_state.debug.thumbnail.thumbnail_zoomout = -1;
	}
}

void gfxDebugThumbnailsAdd(TexHandle tex_handle, const char *title, int show_alpha, bool is_shadow_map)
{
	GfxDebugThumbnail *thumb = dynArrayAddStruct(gfx_state.debug.thumbnail.thumbnails, gfx_state.debug.thumbnail.thumbnails_count, gfx_state.debug.thumbnail.thumbnails_max);
	thumb->tex_handle = tex_handle;
	thumb->title = title;
	thumb->temp_surface = NULL;
	thumb->show_alpha = show_alpha;
	thumb->is_shadow_map = is_shadow_map;
}

// Displays a texture library asset (any BasicTexture), optionally forcing display of a single MIP of the texture. 
// Set force_mip to -1 to allow normal automatic MIP calculation.
void gfxDebugThumbnailsAddTexture(BasicTexture *tex, const char *title, int show_alpha, S8 force_mip)
{
	GfxDebugThumbnail *thumb = dynArrayAddStruct(gfx_state.debug.thumbnail.thumbnails, gfx_state.debug.thumbnail.thumbnails_count, gfx_state.debug.thumbnail.thumbnails_max);
	thumb->tex_handle = texDemandLoad(tex, 0.0f, 1.0f, black_tex);
	thumb->title = title;
	thumb->temp_surface = NULL;
	thumb->show_alpha = show_alpha;
	thumb->is_shadow_map = false;
	thumb->force_mip = force_mip;
}

typedef struct GfxActiveSurfaceState
{
	RdrSurface *currentSurface;
	RdrSurfaceBufferMaskBits currentSurfaceWriteMask;
	RdrSurfaceFace currentSurfaceFace;
} GfxActiveSurfaceState;

void gfxPushActiveSurfaceState(GfxActiveSurfaceState * state)
{
	state->currentSurface = gfx_state.currentSurface;
	state->currentSurfaceWriteMask = gfx_state.currentSurfaceWriteMask;
	state->currentSurfaceFace = gfx_state.currentSurfaceFace;
}

void gfxPopActiveSurfaceState(GfxActiveSurfaceState * state)
{
	gfxSetActiveSurfaceEx(state->currentSurface, state->currentSurfaceWriteMask, state->currentSurfaceFace);
}

void gfxDebugThumbnailsAddSurface(RdrSurface *surface, RdrSurfaceBuffer buffer, int snapshot_idx, const char *title, int show_alpha)
{
	TexHandle tex_handle;
	if (!surface)
		return;

	// DX11TODO DJR not as efficient?
	if (!snapshot_idx)
		snapshot_idx = 1;

	tex_handle = rdrSurfaceToTexHandleEx(surface, buffer, snapshot_idx, 0, false);
	gfxDebugThumbnailsAdd(tex_handle, title, show_alpha, buffer==SBUF_DEPTH && (surface->params_nonthread.flags & SF_SHADOW_MAP));
}

void gfxDebugThumbnailsAddSurfaceCopy(RdrSurface *surface, RdrSurfaceBuffer buffer, int snapshot_idx, const char *title, int show_alpha)
{
	TexHandle tex_handle;
	GfxTempSurface *temp_surface;
	RdrSurfaceParams params = {0};
	GfxActiveSurfaceState savedActiveSurfaceState = {0};
	RdrQuadDrawable quad = {0};
	int i;
	TexHandle textures[1];
	bool bIsPCFDepth=false;

	if (!surface)
		return;

	gfxPushActiveSurfaceState(&savedActiveSurfaceState);
	if (surface->params_nonthread.flags & SF_SHADOW_MAP) {
		bIsPCFDepth = true;
	}

	params.desired_multisample_level = 1;
	params.required_multisample_level = 1;
	params.width = surface->width_nonthread;
	params.height = surface->height_nonthread;
	params.buffer_types[0] = SBT_RGBA;
	rdrSetDefaultTexFlagsForSurfaceParams(&params);
	params.name = "gfxDebugThumbnailSurfaceCopy";
	temp_surface = gfxGetTempSurface(&params);

	// Copy the passed in surface into this surface
	gfxSetActiveSurface(temp_surface->surface);
	if (!bIsPCFDepth && buffer == SBUF_DEPTH)
	{
		if (!gfx_state.screenshot_depth_max)
		{
			gfx_state.screenshot_depth_max = 1;
			gfx_state.screenshot_depth_power = 32;
		}
		gfxCopyDepthIntoRGB(surface, 0, gfx_state.screenshot_depth_min, gfx_state.screenshot_depth_max, gfx_state.screenshot_depth_power);
	} else {
		// Setup Texture coordinates
		for (i=0; i<4; i++) {
			int intpos[] = {(~i) & 1, i >> 1};
			setVec4(quad.vertices[i].color, 1.f, 1.f, 1.f, 1.f);
			quad.vertices[i].texcoords[0] = (F32)intpos[0]; // Could correct for NP2 textures here
			quad.vertices[i].texcoords[1] = 1.f-intpos[1]; // Could correct for NP2 textures here
			quad.vertices[i].point[0]= intpos[0]*surface->width_nonthread;
			quad.vertices[i].point[1]= intpos[1]*surface->height_nonthread;
		}

		quad.material.tex_count = 1;
		quad.material.textures = textures;
		// DX11TODO DJR not as efficient?
		if (!snapshot_idx)
			snapshot_idx = 1;

		textures[0] = rdrSurfaceToTexHandleEx(surface, buffer, snapshot_idx, 0, false);
		rdrAddRemoveTexHandleFlags(&textures[0], RTF_MAG_POINT|RTF_MIN_POINT, 0);
		quad.shader_handle = gfxDemandLoadSpecialShader(bIsPCFDepth?GSS_PCFDEPTHBLIT:GSS_ALPHABLIT);
		rdrDrawQuad(gfx_state.currentDevice->rdr_device, &quad);
		gfx_state.debug.frame_counts.sprites_drawn++;
	}

	tex_handle = rdrSurfaceToTexHandleEx(temp_surface->surface, SBUF_0, 0, 0, true);
	gfxDebugThumbnailsAdd(tex_handle, title, show_alpha, false);
	gfx_state.debug.thumbnail.thumbnails[gfx_state.debug.thumbnail.thumbnails_count-1].temp_surface = temp_surface;

	gfxPopActiveSurfaceState(&savedActiveSurfaceState);
}

void gfxDebugThumbnailsAddSurfaceMaybeCopy(RdrSurface *surface, RdrSurfaceBuffer buffer, int snapshot_idx, const char *title, int show_alpha, bool bCopy)
{
	if (bCopy)
		gfxDebugThumbnailsAddSurfaceCopy(surface, buffer, snapshot_idx, title, show_alpha);
	else
		gfxDebugThumbnailsAddSurface(surface, buffer, snapshot_idx, title, show_alpha);
}

static void drawThumbnailEntry(int index, float width, float height, float x0, float y0, RdrQuadDrawable *quad_out)
{
	RdrQuadDrawable quad = {0};
	TexHandle textures[1];
	Vec4 constants[1];
	int screenWidth, screenHeight;
	int i;
	GfxSpecialShader shader;

	gfxGetActiveSurfaceSizeInline(&screenWidth, &screenHeight);

	// Setup Texture coordinates
	for (i=0; i<4; i++) {
		int intpos[] = {(~i) & 1, i >> 1};
		setVec4(quad.vertices[i].color, 1.f, 1.f, 1.f, 1.f);
		quad.vertices[i].texcoords[0] = (F32)intpos[0]; // Could correct for NP2 textures here
		quad.vertices[i].texcoords[1] = 1.f-intpos[1]; // Could correct for NP2 textures here
	}

	quad.material.tex_count = 1;
	quad.material.textures = textures;

	if (gfx_state.debug.thumbnail.thumbnails[index].is_shadow_map)
	{
		shader = GSS_PCFDEPTHBLIT;
		if (!gfx_state.screenshot_depth_max)
		{
			gfx_state.screenshot_depth_max = 1;
			gfx_state.screenshot_depth_power = 32;
		}
		setVec4(constants[0], gfx_state.screenshot_depth_min, gfx_state.screenshot_depth_max, gfx_state.screenshot_depth_power, 0);
		quad.material.const_count = 1;
		quad.material.constants = constants;		
	}
	else if (gfx_state.debug.thumbnail.thumbnails[index].show_alpha==2)
		shader = GSS_SHOWALPHA_AS_COLOR;
	else if (gfx_state.debug.thumbnail.thumbnails[index].show_alpha==1)
		shader = GSS_SHOWALPHA;
	else if (gfx_state.debug.thumbnail.thumbnails[index].force_mip!=-1)
	{
		setVec4(constants[0], gfx_state.debug.thumbnail.thumbnails[index].force_mip, 0, 0, 0);
		quad.material.const_count = 1;
		quad.material.constants = constants;		
		shader = GSS_SHOWTEXTUREMIP;
	}
	else
		shader = GSS_NOALPHABLIT;

	quad.shader_handle = gfxDemandLoadSpecialShader(shader);
	textures[0] = gfx_state.debug.thumbnail.thumbnails[index].tex_handle;
	if (gfx_state.debug.thumbnail.point_sample)
		rdrAddRemoveTexHandleFlags(&textures[0], RTF_MAG_POINT|RTF_MIN_POINT, 0);

	for (i=0; i<4; i++) {
		int intpos[] = {(~i) & 1, i >> 1};
		quad.vertices[i].point[0]= width * intpos[0] + x0;
		quad.vertices[i].point[1]= height * intpos[1] + y0;
	}

	rdrDrawQuad(gfx_state.currentDevice->rdr_device, &quad);
	gfx_state.debug.frame_counts.sprites_drawn++;

	if (quad_out)
		*quad_out = quad;
}

static bool surface_debug_use_friendly=true;
// Enables/disables printing of values in KB/MB/B formats for /surface_debug
AUTO_CMD_INT(surface_debug_use_friendly, surface_debug_use_friendly) ACMD_CATEGORY(Debug);

static char *gfxDebugSurfaceBytes(S64 bytes, char *buf, size_t buf_size)
{
	if (surface_debug_use_friendly)
		return friendlyUnitBuf_s(byteSpec, bytes, SAFESTR2(buf));
	sprintf_s(SAFESTR2(buf), "%"FORM_LL"d", bytes);
	return buf;
}

static const char *gfxDebugSurfaceSRGB(const RdrSurfaceQueryData *surfaceDetails)
{
	if (surfaceDetails->buffer_types[0] & SBT_SRGB) {
		return "sRGB";
	} else {
		return "Linear";
	}
}


static void gfxDebugSurfaceDisplay(void)
{
	int y0=4;
	int y=y0;
	int x=4;
	int i;
	static RdrSurfaceQueryAllData query_data;
	U32 total_mem=0;
	char buf1[128], buf2[128], buf3[128];
	U32 mem_usage;
	U32 startup_mem_usage;
	int mx, my;
	static int selected_index=-1;
	static int selected_sub_index=-1;
	static int unselect_counter=0;
	bool selected_something=false;
	U8 rgb[2][2][3] =
	{
		// not selected
		{
			// not over
			{255, 255, 255},
			// over
			{255, 127, 127},
		},
		// selected
		{
			// not over
			{255, 255, 100},
			// over
			{200, 127, 127},
		}
	};
// 	FOR_EACH_IN_EARRAY(gfx_state.currentDevice->temp_buffers, GfxTempSurface, surf)
// 	{
// 		gfxXYprintf(x, y, "%dx%d %s %s", surf->creation_params.width, surf->creation_params.height, rdrGetSurfaceBufferTypeNameString(surf->creation_params.buffer_types[0]), surf->creation_params.name);
// 		y++;
// 	}
// 	FOR_EACH_END;
//	y++;

	mousePos(&mx, &my);

	gfxXYprintf(x-2, y++, "Click to select a surface for more info");

	rdrQueryAllSurfaces(gfx_state.currentDevice->rdr_device, &query_data);
	for (i=0; i<query_data.nsurfaces; i++)
	{
		RdrSurfaceQueryData *entry = &query_data.details[i];
		bool over=false;
		bool selected=(selected_index == i);
		if (mx >= gfxXYgetX(x) && mx < gfxXYgetX(x+75) &&
			my>=gfxXYgetY(y-1) && my < gfxXYgetY(y))
			over = true;
		gfxXYprintfColor(x, y,
			rgb[selected][over][0], rgb[selected][over][1], rgb[selected][over][2], 255,
			"%4dx%-4d %-10s x%d/%d MSAA:%d %-9s %-9s %s",
			entry->w, entry->h, rdrGetSurfaceBufferTypeNameString(entry->buffer_types[0]),
			entry->texture_count[0], entry->texture_count[SBUF_DEPTH], entry->msaa,
			gfxDebugSurfaceBytes(entry->total_mem_size, SAFESTR(buf1)), 
			gfxDebugSurfaceSRGB(entry), entry->name);
		total_mem += entry->total_mem_size;
		if (over && inpLevel(INP_LBUTTON))
		{
			selected_index = i;
			selected_something = true;
		}
		y++;
	}
	gfxXYprintf(x, y++, "%d surfaces, %s total surface memory", query_data.nsurfaces, friendlyBytes(total_mem));
	mem_usage = gfxNVPerfGetGPUMemUsage();
	startup_mem_usage = gfxNVPerfGetGPUStartupMemUsage();
	gfxXYprintf(x, y++, "NV: total used: %s  startup used: %s  delta used: %s",
		gfxDebugSurfaceBytes(mem_usage*1024LL, SAFESTR(buf1)),
		gfxDebugSurfaceBytes(startup_mem_usage*1024LL, SAFESTR(buf2)),
		gfxDebugSurfaceBytes((mem_usage - startup_mem_usage)*1024LL, SAFESTR(buf3))
		);
	{
		GeoMemUsage geo_usage;
		U32 used_other_mem=0;
		wlGeoGetMemUsageQuick(&geo_usage);
		used_other_mem += geo_usage.loadedVideoTotal;
		used_other_mem += texMemoryUsage[TEX_MEM_VIDEO];
		gfxXYprintf(x, y++, "Textures: %s Geo: %s", gfxDebugSurfaceBytes(texMemoryUsage[TEX_MEM_VIDEO], SAFESTR(buf1)),
			gfxDebugSurfaceBytes(geo_usage.loadedVideoTotal, SAFESTR(buf2)));
		gfxXYprintf(x, y++, "Difference: %s", gfxDebugSurfaceBytes((mem_usage - startup_mem_usage)*1024LL - total_mem - used_other_mem, SAFESTR(buf1)));
	}
	y++;

	y = y0;
	x+=75;
	if (selected_index>=0 && selected_index<query_data.nsurfaces)
	{
		RdrSurfaceQueryData *entry = &query_data.details[selected_index];
		gfxXYprintfColor(x, y++,
			200, 200, 200, 255,
			"%4dx%-4d %-10s x%d/%d MSAA:%d %-9s %s",
			entry->w, entry->h, rdrGetSurfaceBufferTypeNameString(entry->buffer_types[0]),
			entry->texture_count[0], entry->texture_count[SBUF_DEPTH], entry->msaa,
			gfxDebugSurfaceBytes(entry->total_mem_size, SAFESTR(buf1)), entry->name);
		x+=4;
		for (i=0; i<entry->texture_max[SBUF_0]; i++)
		{
			bool over=false;
			bool selected=(selected_sub_index == i);
			if (mx >= gfxXYgetX(x) && mx < gfxXYgetX(x+70) &&
				my>=gfxXYgetY(y-1) && my < gfxXYgetY(y))
				over = true;
			gfxXYprintfColor(x, y++,
				rgb[selected][over][0], rgb[selected][over][1], rgb[selected][over][2], 255,
				"Color0[%d] %s", i, entry->snapshot_names[SBUF_0][i]);
			if (over && inpLevel(INP_LBUTTON))
			{
				selected_sub_index = i;
				selected_something = true;
			}
		}
		for (i=0; i<entry->texture_max[SBUF_DEPTH]; i++)
		{
			bool over=false;
			bool selected=(selected_sub_index == i+16);
			if (mx >= gfxXYgetX(x) && mx < gfxXYgetX(x+70) &&
				my>=gfxXYgetY(y-1) && my < gfxXYgetY(y))
				over = true;
			gfxXYprintfColor(x, y++,
				rgb[selected][over][0], rgb[selected][over][1], rgb[selected][over][2], 255,
				"Depth[%d] %s", i, entry->snapshot_names[SBUF_DEPTH][i]);
			if (over && inpLevel(INP_LBUTTON))
			{
				selected_sub_index = i+16;
				selected_something = true;
			}
		}
		x-=4;

		if (selected_sub_index != -1)
		{
			if (entry->rdr_surface && _CrtIsValidPointer(entry->rdr_surface, sizeof(RdrSurface), 1) && !entry->rdr_surface->destroyed_nonthread)
			{
				bool b=false;
				int screenWidth, screenHeight;
				gfxGetActiveSurfaceSizeInline(&screenWidth, &screenHeight);
				if (selected_sub_index>=0 && selected_sub_index < entry->texture_max[SBUF_0])
				{
					gfxDebugThumbnailsAddSurface(entry->rdr_surface, SBUF_0, selected_sub_index, "surface_debug", 0);
					b = true;
				} else if (selected_sub_index-16>=0 && selected_sub_index-16 < entry->texture_max[SBUF_DEPTH])
				{
					gfxDebugThumbnailsAddSurface(entry->rdr_surface, SBUF_DEPTH, selected_sub_index-16, "surface_debug", 0);
					b = true;
				}
				if (b)
					drawThumbnailEntry(gfx_state.debug.thumbnail.thumbnails_count-1, screenWidth, screenHeight, 0, 0, NULL);
			}
		}
	}
	if (inpLevel(INP_ESCAPE))
	{
		unselect_counter++;
		if (unselect_counter==2)
		{
			if (selected_sub_index == -1)
				selected_index = -1;
			else
				selected_sub_index = -1;
		}
	} else {
		unselect_counter = 0;
	}
}


void gfxDebugThumbnailsDisplay(void)
{
#define THUMBNAIL_SIZE 128

	if (gfx_state.debug.surface_debug)
	{
		gfxDebugSurfaceDisplay();
	}

	if (gfx_state.debug.thumbnail.thumbnails_count) {
		F32 v;
		int k;
		int mouse_x, mouse_y;
		int screenWidth, screenHeight;
		int new_thumbnail_display = -1;
		F32 scale=1;
		extern Cmd cmdVarSetData_thumbnailDisplay;

		PERFINFO_AUTO_START(__FUNCTION__, gfx_state.debug.thumbnail.thumbnails_count);
		gfxBeginSection(__FUNCTION__);

		cmdVarSetData_thumbnailDisplay.data[0].max_value = gfx_state.debug.thumbnail.thumbnails_count;

		if (gfx_state.debug.thumbnail.thumbnail_display >= gfx_state.debug.thumbnail.thumbnails_count) {
			gfx_state.debug.thumbnail.fixed = false;
			gfx_state.debug.thumbnail.thumbnail_display = -1;
		}

		gfxGetActiveSurfaceSizeInline(&screenWidth, &screenHeight);
	
		mousePos(&mouse_x, &mouse_y);
		mouse_y = screenHeight - mouse_y;

		if (gfx_state.debug.thumbnail.thumbnails_count * (THUMBNAIL_SIZE + 10) + 5 > screenWidth)
		{
			scale = screenWidth / (float)(gfx_state.debug.thumbnail.thumbnails_count * (THUMBNAIL_SIZE + 10) + 5);
		}

		// Before thumbnails so it shows up under them.
		if (gfx_state.debug.thumbnail.strScreenshotRequestTitle)
		{
			int thumbIndex = 0;
			gfx_state.debug.thumbnail.thumbnail_display = -1;
			for (thumbIndex = 0; thumbIndex < gfx_state.debug.thumbnail.thumbnails_count; ++thumbIndex)
			{
				if (!strcmp(gfx_state.debug.thumbnail.thumbnails[thumbIndex].title, gfx_state.debug.thumbnail.strScreenshotRequestTitle))
				{
					gfx_state.debug.thumbnail.thumbnail_display = thumbIndex;
					gfx_state.debug.thumbnail.stretchout = 0.0f;
					gfx_state.debug.thumbnail.stretch = 1.0f;
					break;
				}
			}
		}
		if (gfx_state.debug.thumbnail.thumbnail_zoomout != -1)
		{
			gfx_state.debug.thumbnail.stretchout += gfx_state.real_frame_time * 5.0;
			gfx_state.debug.thumbnail.stretchout = CLAMP(gfx_state.debug.thumbnail.stretchout, 0.f, 1.f);
			v = sin((1.f - gfx_state.debug.thumbnail.stretchout) * PI / 2);
			drawThumbnailEntry(gfx_state.debug.thumbnail.thumbnail_zoomout, v*screenWidth, v*screenHeight, (1.f-v)*screenWidth, (1.f-v)*screenHeight, NULL);
			if (gfx_state.debug.thumbnail.stretchout >= 1.0) {
				gfx_state.debug.thumbnail.thumbnail_zoomout = -1;
			}
		}
		if (gfx_state.debug.thumbnail.thumbnail_display != -1)
		{
			gfx_state.debug.thumbnail.stretch += gfx_state.real_frame_time * 5.0;
			gfx_state.debug.thumbnail.stretch = CLAMP(gfx_state.debug.thumbnail.stretch, 0.f, 1.f);
			v = sin(gfx_state.debug.thumbnail.stretch * PI / 2);
			drawThumbnailEntry(gfx_state.debug.thumbnail.thumbnail_display, lerp(THUMBNAIL_SIZE*scale, screenWidth, v), lerp(THUMBNAIL_SIZE*scale, screenHeight, v),
				(1.0-v) * (gfx_state.debug.thumbnail.thumbnail_display*(THUMBNAIL_SIZE+10) + 5) * scale,
				(1.0-v) * 5 * scale, NULL);
			if (gfx_state.debug.thumbnail.thumbnails[gfx_state.debug.thumbnail.thumbnail_display].title) {
				if (!gfx_state.debug.runNVPerf) { // Will queue up new sprites mid-frame, which will mess up NVPerfAPI experiments
					bool bSaved = g_no_sprites_allowed;
					g_no_sprites_allowed = false;
					gfxXYprintf(1, 1, "%s", gfx_state.debug.thumbnail.thumbnails[gfx_state.debug.thumbnail.thumbnail_display].title);
					if (gfx_state.debug.thumbnail.fixed)
						gfxXYprintf(1, 2, "(locked - double click to unlock)");
					else
						gfxXYprintf(1, 2, "(double click to lock in place)");

					g_no_sprites_allowed = bSaved;
				}
			}

			if (gfx_state.debug.thumbnail.strScreenshotRequestTitle)
			{
				void gfxSaveScreenshotInternal(char *filename);
				gfxSaveScreenshotInternal(gfx_state.debug.thumbnail.strScreenshotFilename);
				estrDestroy(&gfx_state.debug.thumbnail.strScreenshotFilename);
				estrDestroy(&gfx_state.debug.thumbnail.strScreenshotRequestTitle);
			}
		}

		for (k=0; k<gfx_state.debug.thumbnail.thumbnails_count; k++)
		{
			RdrQuadDrawable quad = {0};
			drawThumbnailEntry(k, THUMBNAIL_SIZE * scale, THUMBNAIL_SIZE*scale,
				(k*(THUMBNAIL_SIZE+10) + 5) * scale, 5*scale, &quad);
			if (mouse_x > quad.vertices[1].point[0] &&
				mouse_x < quad.vertices[0].point[0] &&
				mouse_y > quad.vertices[0].point[1] &&
				mouse_y < quad.vertices[2].point[1])
			{
				// Mouse over
				new_thumbnail_display = k;
			}
		}

		if (mouseDoubleClick(MS_LEFT))
		{
			if (new_thumbnail_display == -1) {
				gfx_state.debug.thumbnail.fixed = false;
			} else {
				gfx_state.debug.thumbnail.fixed = true;
			}
		} else if (gfx_state.debug.thumbnail.fixed) {
			if (gfx_state.debug.thumbnail.thumbnail_display < gfx_state.debug.thumbnail.thumbnails_count)
				new_thumbnail_display = gfx_state.debug.thumbnail.thumbnail_display;
			else
				gfx_state.debug.thumbnail.fixed = false;
		}

		if (new_thumbnail_display != gfx_state.debug.thumbnail.thumbnail_display && !gfx_state.currentDevice->isInactive) {
			if (gfx_state.debug.thumbnail.thumbnail_display != -1) {
				// Fade away old one
				if (!gfx_state.debug.thumbnail.snap) {
					gfx_state.debug.thumbnail.thumbnail_zoomout = gfx_state.debug.thumbnail.thumbnail_display;
					gfx_state.debug.thumbnail.stretchout = 0.0f;
				}
				gfx_state.debug.thumbnail.thumbnail_display = -1;
			}
			if (new_thumbnail_display != -1) {
				gfx_state.debug.thumbnail.thumbnail_display = new_thumbnail_display;
				if (gfx_state.debug.thumbnail.snap) {
					gfx_state.debug.thumbnail.stretch = 1.f;
				} else {
					// Fade up!
					gfx_state.debug.thumbnail.stretch = 0.0f;
				}
			}
		}

		gfxDebugThumbnailsReset();

		gfxEndSection();
		PERFINFO_AUTO_STOP();
	} else {
		if (!gfx_state.currentDevice->isInactive) {
			gfx_state.debug.thumbnail.thumbnail_display = -1;
			gfx_state.debug.thumbnail.thumbnail_zoomout = -1;
		}
	}
}

void gfxDrawGPolySet(GPolySet *set, const Vec3 camera_mid)
{
	int i, j, prevJ;
	for (i = 0; i < set->count; i++)
	{
		GPoly *poly = &set->polys[i];
		Vec4 plane;
		F32 dist;

		makePlane(poly->points[0], poly->points[1], poly->points[2], plane);
		dist = distanceToPlane(camera_mid, plane);

		prevJ = poly->count-1;
		for (j = 0; j < poly->count; j++)
		{
			gfxDrawLine3D(poly->points[prevJ], poly->points[j], CreateColor(0xff, 0xff, 0xff, 0xff));
			prevJ = j;
		}

		for (j = 2; j < poly->count; j++)
		{
			gfxDrawTriangle3D(poly->points[0], poly->points[j-1], poly->points[j], (dist < 0) ? CreateColor(0xff, 0, 0, 0x50) : CreateColor(0, 0xff, 0, 0x50));
		}
	}
}

void gfxDrawGMesh(GMesh *mesh, Color c, int use_mesh_colors)
{
	int i;
	for (i = 0; i < mesh->tri_count; i++)
	{
		if(use_mesh_colors)
		{
			if(mesh->usagebits & USE_COLORS)
			{
				gfxDrawTriangle3D_3Ex(mesh->positions[mesh->tris[i].idx[0]],
										mesh->positions[mesh->tris[i].idx[1]],
										mesh->positions[mesh->tris[i].idx[2]],
										mesh->colors[mesh->tris[i].idx[0]], 
										mesh->colors[mesh->tris[i].idx[1]], 
										mesh->colors[mesh->tris[i].idx[2]], 
										1.0, false);
			}
			else
			{
				gfxDrawTriangle3D_3Ex(mesh->positions[mesh->tris[i].idx[0]],
										mesh->positions[mesh->tris[i].idx[1]],
										mesh->positions[mesh->tris[i].idx[2]],
										mesh->dbg_color, 
										mesh->dbg_color,
										mesh->dbg_color,
										1.0, false);
			}
		}
		else
		{
			gfxDrawTriangle3D_3Ex(mesh->positions[mesh->tris[i].idx[0]],
									mesh->positions[mesh->tris[i].idx[1]],
									mesh->positions[mesh->tris[i].idx[2]],
									c, c, c, 1.0, false);
		}
	}
}

static struct {
	char *cmdstr;
	S32 accesslevel;
} **nalz_warnings;

void gfxDebugClearAccessLevelCmdWarnings(void)
{
	if(!eaSize(&nalz_warnings)){
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	while (eaSize(&nalz_warnings)) {
		SAFE_FREE(nalz_warnings[0]->cmdstr);
		free(eaRemoveFast(&nalz_warnings, 0));
	}
	PERFINFO_AUTO_STOP();
}

void gfxDebugDisableAccessLevelWarnings(bool disable)
{
	gfx_state.debug.no_nalz_warnings = disable;
	gfx_state.debug.no_nalz_warnings_timeout = 0;
}

void gfxDebugDisableAccessLevelWarningsTemporarily(float fTimeout)
{
	gfx_state.debug.no_nalz_warnings_timeout = fTimeout;
	gfx_state.debug.no_nalz_warnings = true;
}



static void gfxDebugNotifyRanAccessLevelCmd(const char *cmdstr, AccessLevel accesslevel)
{
	// If someone ran this from the console they probably know it's access
	// level 9, so don't bother them.
	if (gfxConsoleVisible() || gfx_state.debug.no_nalz_warnings)
		return;
	if (strnicmp(cmdstr, "print ", 6)) { // just because I don't want it printing twice for each print command
		int index = eaPush(&nalz_warnings, calloc(sizeof(*(nalz_warnings[0])), 1));
		nalz_warnings[index]->cmdstr = strdup(cmdstr);
		nalz_warnings[index]->accesslevel = (S32)accesslevel;
	}
}

AUTO_COMMAND ACMD_NAME(print) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(testCommands);
void gfxDebugPrintString(const char *cmdstr)
{
	int index = eaPush(&nalz_warnings, calloc(sizeof(*(nalz_warnings[0])), 1));
	nalz_warnings[index]->cmdstr = strdup(cmdstr);
	nalz_warnings[index]->accesslevel = -1;
}

AUTO_RUN;
void gfxSetupAccessLevelCallback(void)
{
	cmdParserSetAccessLevelCmdCallback(gfxDebugNotifyRanAccessLevelCmd);
}

static void gfxDisplayAccessLevelWarnings(bool in_editor)
{
	static int timer=0;
	char msg[2048];
	int w, h;
#define ALW_TIMEOUT 10.f
#define ALW_FADEOUT 1.f
#define ALW_MINTIME 3.f

	if (in_editor)
	{
		gfxDebugClearAccessLevelCmdWarnings();
		return;
	}

	if (!timer)
		timer = timerAlloc();

	if (gfx_state.debug.no_nalz_warnings_timeout != 0) {
		gfx_state.debug.no_nalz_warnings_timeout -= gfx_state.frame_time;
		if (gfx_state.debug.no_nalz_warnings_timeout <= 0) {
			gfx_state.debug.no_nalz_warnings = false;
			gfx_state.debug.no_nalz_warnings_timeout = 0;
		}
	}

	gfxGetActiveSurfaceSizeInline(&w,&h);
	if (eaSize(&nalz_warnings))
	{
		F32 elapsed = timerElapsed(timer);
		bool flushit=false;
		if (elapsed >= ALW_TIMEOUT || (eaSize(&nalz_warnings) > 1) && (elapsed > ALW_MINTIME) || gfx_state.debug.no_nalz_warnings) {
			flushit = true;
		}
		if (flushit) {
			SAFE_FREE(nalz_warnings[0]->cmdstr);
			free(eaRemove(&nalz_warnings, 0));
			timerStart(timer);
		}

		if (eaSize(&nalz_warnings)) {
			int alpha = 0xff;
			F32 timeout = ((eaSize(&nalz_warnings)>1)?ALW_MINTIME:ALW_TIMEOUT);

			if (elapsed > timeout - ALW_FADEOUT) {
				alpha = (int)(255 * (timeout - elapsed) / ALW_FADEOUT);
				alpha = CLAMP(alpha, 0, 255);
			}

			if (nalz_warnings[0]->accesslevel >= 0 )
				sprintf(msg, "Ran AccessLevel %d Cmd:\n%s", nalz_warnings[0]->accesslevel, nalz_warnings[0]->cmdstr);
			else
				sprintf(msg, "%s", nalz_warnings[0]->cmdstr);

			gfxfont_SetFontEx(&g_font_Sans, 0, 0, 1, 0, 0xff000000|alpha, 0xff9f9f00|alpha);
			if (IsDebuggerPresent() && !s_bLessAnnoyingAccessLevel)
			{
				gfxfont_PrintMultiline(0, h / 2, GRAPHICSLIB_Z+1, 2, 2, msg);
			}
			else
				gfxfont_PrintMultiline((float)w - 350, 20, GRAPHICSLIB_Z+1, 1.5, 1.5, msg);

			if (s_bPrintAccessLevelWarnings)
			{
				// Log message to client text console so there's a record of what commands were run
				printf("%s\n", msg);
			}
		}
	} else {
		if (gfxGetAccessLevelForDisplay() >= ACCESS_GM_FULL && !s_bLessAnnoyingAccessLevel)
		{
			sprintf(msg, "Access Level %d", gfxGetAccessLevel());
			gfxfont_SetFontEx(&g_font_Sans, 0, 0, 1, 0, 0xff0000ff, 0xff9f9fff);
			gfxfont_PrintMultiline((float)w - 350, 20, GRAPHICSLIB_Z + 1, 1.5, 1.5, msg);
		}

		timerStart(timer);
	}
}

static void gfxDebugDrawWorldCell(int x, int y, WorldCell *cell, int pixwidth, Color color)
{
	int i;

	if (!cell || cell->cell_state != WCS_OPEN)
		return;

	if (cell->vis_dist_level == 0)
	{
		Vec3 pos;
		IVec3 cam_grid_pos;
		gfxGetActiveCameraPos(pos);
		worldPosToGridPos(pos, cam_grid_pos, CELL_BLOCK_SIZE);
		if (sameVec3(cam_grid_pos, cell->cell_block_range.min_block))
			color = colorFromRGBA(0xff0000ff);
	}

	gfxDrawBox(x, y, x + pixwidth, y - pixwidth, 500, color);

	pixwidth -= 4;
	pixwidth >>= 1;

	color.r = color.r ^ 0xff;

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
	{
		int child_x, child_y;
		if (i&1)
			child_x = x+2+pixwidth;
		else
			child_x = x+2;
		if (i&4)
			child_y = y-2-pixwidth;
		else
			child_y = y-2;
		gfxDebugDrawWorldCell(child_x, child_y, cell->children[i], pixwidth, color);
	}
}

static void gfxDebugDrawWorldCell3D(WorldCell *cell, Color color, int depth)
{
	Vec3 boxmin, boxmax;
	int i;
	char blink = 127 + round(128 * sin(gfx_state.client_loop_timer*PI*4));
	int colorValue = RGBAFromColor(color);

	if (!cell || ((cell->cell_state != WCS_OPEN) && (!(cell->draw_model_cluster && (!depth || cell->parent->cell_state == WCS_OPEN)))) || cell->is_empty)
		return;

	if (cell->vis_dist_level == 0)
	{
		Vec3 pos;
		IVec3 cam_grid_pos;
		gfxGetActiveCameraPos(pos);
		worldPosToGridPos(pos, cam_grid_pos, CELL_BLOCK_SIZE);
		if (sameVec3(cam_grid_pos, cell->cell_block_range.min_block))
			colorValue = 0xff0000ff;
	} else {
		switch (cell->vis_dist_level) {
		case 1:
			colorValue = 0x0000ffff;
			break;
		case 2:
			colorValue = 0x00ff00ff;
			break;
		case 3:
			colorValue = 0x00ffffff;
			break;
		case 4:
			colorValue = 0xffff00ff;
			break;
		case 5:
			colorValue = 0xff00ffff;
			break;
		default:
			colorValue = 0xffffffff;
		}
	}
	if (cell->draw_model_cluster && cell->cell_state != WCS_OPEN) {
		for (i = 1; i < 4; i++)
			if (((char*)(&colorValue))[i])
				((char*)(&colorValue))[i] = blink;
	}
	color = colorFromRGBA(colorValue);

	if (cell->vis_dist_level < gfx_state.debug.world_cell_3d)
	{
		addVec3same(cell->bounds.world_min, -0.1f * cell->vis_dist_level, boxmin);
		addVec3same(cell->bounds.world_max, 0.1f * cell->vis_dist_level, boxmax);
		gfxDrawBox3D(boxmin, boxmax, unitmat, color, 1);

		scaleVec3(cell->cell_block_range.min_block, CELL_BLOCK_SIZE, boxmin);
		addVec3same(cell->cell_block_range.max_block, 1, boxmax);
		scaleVec3(boxmax, CELL_BLOCK_SIZE, boxmax);
		subVec3same(boxmax, 1.0f, boxmax);
		gfxDrawBox3D(boxmin, boxmax, unitmat, colorFromRGBA(0x00ff00ff), 1);
	}

	color.r = color.r ^ 0xff;

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
		gfxDebugDrawWorldCell3D(cell->children[i], color, depth+1);
}

// Enable or disable drawing of collision objects.
typedef enum ECollisionDebugDraw
{
	ECollisionDebugDraw_OFF = 0,
	ECollisionDebugDraw_TYPE,
	ECollisionDebugDraw_DENSITY,
	ECollisionDebugDraw_BEACONIZE,
	ECollisionDebugDraw_PLAYABLEVOLUMEGEO
} ECollisionDebugDraw;

AUTO_CMD_INT(gfx_state.debugDrawStaticCollision, dbgShowCollision) ACMD_ACCESSLEVEL(7) ACMD_CMDLINE ACMD_CATEGORY(Debug, Graphics);
// Enable or disable drawing of dynamic collision objects (e.g. Entities).
AUTO_CMD_INT(gfx_state.debugDrawDynamicCollision, dbgShowDynamicCollision) ACMD_ACCESSLEVEL(7) ACMD_CMDLINE ACMD_CATEGORY(Debug, Graphics);

extern PSDKCookedMesh *geoScaledCollisionGetCookedMesh(ScaledCollision *col);

void drawCollisionObCB(void* userPointer, const WorldCollObjectTraverseParams* params)
{
#if !PSDK_DISABLED
	PSDKActor * pActor;
	const PSDKShape*const* pShapes;
	PSDKCookedMesh * pCookedMesh;
	unsigned int i;
	Vec3 sceneOffset;
	U32 uFilterBits;

	if (!wcoGetActor(params->wco,&pActor,sceneOffset))
		return;

	if (!psdkActorGetShapesArray(pActor,&pShapes))
		return;

	if (psdkActorGetShapeGroup(pActor) == WC_SHAPEGROUP_EDITOR_ONLY)
		return;

	psdkActorGetFilterBits(pActor,&uFilterBits);

	for (i=0;i<psdkActorGetShapeCount(pActor);i++)
	{
		Vec3 vMin,vMax;

		psdkShapeGetBounds(pShapes[i],vMin,vMax);
		subVec3(vMin, sceneOffset, vMin);
		subVec3(vMax, sceneOffset, vMax);
		if (!frustumCheckBoxWorld(&gfx_state.currentCameraView->frustum,0xff,vMin,vMax,NULL,false))
		{
			continue;
		}
		psdkShapeGetCookedMesh(pShapes[i],&pCookedMesh);
		if (pCookedMesh)
		{
			WorldCollStoredModelDataDesc	desc = {0};
			psdkCookedMeshGetTriangles(pCookedMesh, &desc.tris, &desc.tri_count);
			psdkCookedMeshGetVertices(pCookedMesh, &desc.verts, &desc.vert_count);

			if (desc.tri_count)
			{
				RdrDrawList* draw_list = gfx_state.currentAction->gdraw.draw_list;
				RdrDrawableMeshPrimitive* mesh;
				RdrDrawableMeshPrimitiveStrip* strip;
				Mat4 mat;

				int iTri,iVert;

				mesh = rdrDrawListAllocMeshPrimitive(draw_list, RTYPE_PRIMITIVE_MESH, desc.tri_count*3, 1, true);
				if (!mesh)
				{
					return;
				}

				strip = rdrDrawListAllocMeshPrimitiveStrip(draw_list, mesh, 0, desc.tri_count*3);
				if (!strip)
				{
					return;
				}

				strip->type = RP_TRILIST;

				for (iTri=0; iTri<desc.tri_count; iTri++)
				{
					strip->indices[iTri*3+0] = iTri*3+0;
					strip->indices[iTri*3+1] = iTri*3+1;
					strip->indices[iTri*3+2] = iTri*3+2;
					
					copyVec3(desc.verts[desc.tris[iTri*3+0]],mesh->verts[iTri*3+0].pos);
					copyVec3(desc.verts[desc.tris[iTri*3+1]],mesh->verts[iTri*3+1].pos);
					copyVec3(desc.verts[desc.tris[iTri*3+2]],mesh->verts[iTri*3+2].pos);

					zeroVec4(mesh->verts[iTri*3+0].color);
					zeroVec4(mesh->verts[iTri*3+1].color);
					zeroVec4(mesh->verts[iTri*3+2].color);
				}

				if (gfx_state.debugDrawStaticCollision == ECollisionDebugDraw_BEACONIZE)
				{
					for(iTri=0; iTri<desc.tri_count; iTri++)
					{
						RdrPrimitiveVertex *v1 = &mesh->verts[strip->indices[iTri*3+0]];
						RdrPrimitiveVertex *v2 = &mesh->verts[strip->indices[iTri*3+1]];
						RdrPrimitiveVertex *v3 = &mesh->verts[strip->indices[iTri*3+2]];

						Vec4 plane;
						makePlane(v1->pos, v2->pos, v3->pos, plane);

						if(plane[1]<0.766)
						{
							setVec3(mesh->verts[strip->indices[iTri*3+0]].color, 1.0, 0, 0);
							setVec3(mesh->verts[strip->indices[iTri*3+1]].color, 1.0, 0.35, 0.20);
							setVec3(mesh->verts[strip->indices[iTri*3+2]].color, 1.0, 0.20, 0.35);
						}
						else
						{
							setVec3(mesh->verts[strip->indices[iTri*3+0]].color, 0.75, 0.60, 0.75);
							setVec3(mesh->verts[strip->indices[iTri*3+1]].color, 0.60, 0.75, 0.75);
							setVec3(mesh->verts[strip->indices[iTri*3+2]].color, 0.75, 0.75, 0.60);
						}
						mesh->verts[strip->indices[iTri*3+0]].color[3] = 1.0f;
						mesh->verts[strip->indices[iTri*3+1]].color[3] = 1.0f;
						mesh->verts[strip->indices[iTri*3+2]].color[3] = 1.0f;
					}
				}
				else
				{
					for (iVert=0; iVert<(int)mesh->num_verts; iVert++)
					{
						F32 fColorVal = fmod(iVert,3.0f)/4.0f+0.25f;

						if (gfx_state.debugDrawStaticCollision == ECollisionDebugDraw_PLAYABLEVOLUMEGEO)
						{
							if (uFilterBits & WC_FILTER_BIT_PLAYABLEVOLUMEGEO) {
								setVec4(mesh->verts[iVert].color, 0.3f*fColorVal, 0.3f*fColorVal, 0.8f*fColorVal, 1.0f);
							} else {
								mesh->verts[iVert].color[3] = 0.0f;
							}
						}
						else if (gfx_state.debugDrawStaticCollision == ECollisionDebugDraw_DENSITY)
						{
							// color code by density
							F32 aDim[3];
							F32 fMinDim,fSize;
							aDim[0] = MAX(0.1f,(vMax[0]-vMin[0]));
							aDim[1] = MAX(0.1f,(vMax[1]-vMin[1]));
							aDim[2] = MAX(0.1f,(vMax[2]-vMin[2]));
							fSize = aDim[0]*aDim[1]*aDim[2];
							fMinDim = MIN(MIN(aDim[0],aDim[1]),aDim[2]);
							fSize /= fMinDim; // convert to an area
							if (fSize)
							{
								static F32 fMagicFactor = 0.5f;
								static Vec3 aColorTargets[3] = { {0.7f,0.7f,0.7f}, {0.8f,0.3f,0.0f}, {1.0f,0.0f,0.0f}};
								F32 fLerp = MAX(MIN(fMagicFactor*desc.tri_count/fSize-0.5f,2.0f),0.0f);
								if (fLerp <= 1.0f)
									lerpVec3(aColorTargets[1],fLerp,aColorTargets[0],mesh->verts[iVert].color);
								else
									lerpVec3(aColorTargets[2],fLerp-1.0f,aColorTargets[1],mesh->verts[iVert].color);

								scaleVec3(mesh->verts[iVert].color,fColorVal,mesh->verts[iVert].color);
							}
							else
								setVec3same(mesh->verts[iVert].color, fColorVal);
						}
						else
						{
							// color code by type
							if (uFilterBits & WC_FILTER_BIT_MOVEMENT)
							{
								if (uFilterBits & WC_FILTER_BIT_POWERS)
								{
									if (uFilterBits & WC_FILTER_BIT_TARGETING)
									{
										// blocks powers and targeting
										if (uFilterBits & WC_FILTER_BIT_CAMERA_FADE)
										{
											// fadey - light blue
											setVec3(mesh->verts[iVert].color, fColorVal*0.5f, fColorVal*0.75f, fColorVal);
										}
										else if (!(uFilterBits & WC_FILTER_BIT_CAMERA_BLOCKING))
										{
											// see-through - dark blue
											setVec3(mesh->verts[iVert].color, fColorVal*0.3f, fColorVal*0.3f, fColorVal*0.8f);
										}
										else
										{
											// totally normal collision - grey
											setVec3same(mesh->verts[iVert].color, fColorVal);
										}
									}
									else
									{
										// does not block targeting - yellow
										setVec3(mesh->verts[iVert].color, fColorVal, fColorVal, fColorVal*0.5f);
									}
								}
								else
								{
									// does not block powers - green
									setVec3(mesh->verts[iVert].color, fColorVal*0.5f, fColorVal, fColorVal*0.5f);
								}
							}
							else
							{
								// does not block movement - red
								setVec3(mesh->verts[iVert].color, fColorVal, fColorVal*0.5f, fColorVal*0.5f);
							}
						}

						if (gfx_state.debugDrawStaticCollision != ECollisionDebugDraw_PLAYABLEVOLUMEGEO) {
							mesh->verts[iVert].color[3] = 1.0f;
						}
					}
				}
				
				mesh->no_zwrite = 0;

				if (wcoGetMat(params->wco,mat))
				{
					rdrDrawListAddMeshPrimitive(draw_list, mesh, mat, RST_AUTO, RTYPE_PRIMITIVE_MESH);
				}
			}
		}
	}
#endif
}

void gfxDebugDrawCollision(void)
{
	if (gfx_state.debugDrawStaticCollision || gfx_state.debugDrawDynamicCollision)
	{
		U32 traverse_types = (gfx_state.debugDrawStaticCollision ? WCO_TRAVERSE_STATIC : 0) | (gfx_state.debugDrawDynamicCollision ? WCO_TRAVERSE_DYNAMIC : 0);

		WorldColl* pColl = worldGetActiveColl(PARTITION_CLIENT);
		Vec3 cam_pos,min,max;
		gfxGetActiveCameraPos(cam_pos);
		copyVec3(cam_pos,min);
		copyVec3(cam_pos,max);
		min[0] -= 100.0f;
		min[1] -= 100.0f;
		min[2] -= 100.0f;
		max[0] += 100.0f;
		max[1] += 100.0f;
		max[2] += 100.0f;

		wcTraverseObjects(pColl, drawCollisionObCB, NULL, min, max, /*unique=*/1, traverse_types);
	}
}

static void gfxDebugWorldCell(WorldCell *cell)
{
	WorldCell *current_cell = worldGetDebugCell();

	if (current_cell)
	{
		current_cell->debug_me = 0;
		worldSetDebugCell(NULL);
	}

	if (cell)
	{
		worldSetDebugCell(cell);
		cell->debug_me = 1;
	}
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void worldCellDebugParent(void)
{
	WorldCell *current_cell = worldGetDebugCell();
	if (current_cell && current_cell->parent)
		gfxDebugWorldCell(current_cell->parent);
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void worldCellDebug(int vis_dist_level)
{
	WorldRegion *region;
	Vec3 cam_pos;
	gfxGetActiveCameraPos(cam_pos);
	region = worldGetWorldRegionByPos(cam_pos);
	if (region && region->root_world_cell)
	{
		WorldCell *cell, *child_cell;
		IVec3 grid_pos;

		worldPosToGridPos(cam_pos, grid_pos, CELL_BLOCK_SIZE);

		for (cell = child_cell = region->root_world_cell; 
			 child_cell && child_cell->cell_state == WCS_OPEN && child_cell->vis_dist_level >= vis_dist_level; 
			 child_cell = worldCellGetChildForGridPos(cell, grid_pos, NULL, false))
		{
			cell = child_cell;
		}

		gfxDebugWorldCell(cell);
	}
}

static void gfxDebugWorldCellEntry(WorldDrawableEntry *entry)
{
	WorldDrawableEntry *current_entry = worldGetDebugDrawable();
	if (current_entry)
	{
		current_entry->debug_me = 0;
		worldSetDebugDrawable(NULL);
	}

	if (entry)
	{
		worldSetDebugDrawable(entry);
		entry->debug_me = 1;
	}
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void worldCellEntryDebug(const char *entry_type, const char *modelname)
{
	WorldCellEntryType find_type = WCENT_INVALID;
	bool near_fade = false;
	bool welded = false;
	Vec3 cam_pos;

	if (stricmp(entry_type, "model")==0)
	{
		find_type = WCENT_MODEL;
	}
	else if (stricmp(entry_type, "instanced")==0)
	{
		find_type = WCENT_MODELINSTANCED;
	}
	else if (stricmp(entry_type, "spline")==0)
	{
		find_type = WCENT_SPLINE;
	}
	else if (stricmp(entry_type, "nearfade")==0)
	{
		near_fade = true;
		find_type = WCENT_MODEL;
	}
	else if (stricmp(entry_type, "welded")==0)
	{
		welded = true;
		find_type = WCENT_MODELINSTANCED;
	}

	gfxGetActiveCameraPos(cam_pos);
	gfxDebugWorldCellEntry(worldFindNearestDrawableEntry(cam_pos, find_type, modelname, near_fade, welded));
}

static void gfxDisplayDebugWorldCellEntryInfo(void)
{
	WorldDrawableEntry *entry = worldGetDebugDrawable();
	WorldCellEntryBounds *bounds;
	WorldCellEntrySharedBounds *shared_bounds;
	Mat4 world_mat;
	Vec3 world_min, world_max;
	int	unit,zero,y=6;
	Vec3 rot, cam_pos;

	if (!entry)
		return;

	gfxGetActiveCameraPos(cam_pos);

	bounds = &entry->base_entry.bounds;
	shared_bounds = entry->base_entry.shared_bounds;

	entry->debug_me = 1;

	y++; // for the "Debug drawable hit" message
	y++; // for the "Debug drawable drawn" message

	copyMat4(bounds->world_matrix, world_mat);
	mulBoundsAA(shared_bounds->local_min, shared_bounds->local_max, bounds->world_matrix, world_min, world_max);

	unitZeroMat(world_mat,&unit,&zero);
	if (unit)
	{
		setVec3same(rot, 0);
	}
	else
	{
		Vec3 scale;
		extractScale(world_mat, scale); // also normalizes the matrix
		getMat3YPR(world_mat, rot);
	}

	y++;
	gfxXYprintf(1, y++, "Pos: % 4.4f, % 4.4f, % 4.4f", world_mat[3][0], world_mat[3][1], world_mat[3][2]);
	gfxXYprintf(1, y++, "PYR: % 4.4f, % 4.4f, % 4.4f", rot[0], rot[1], rot[2]);
	gfxXYprintf(1, y++, "Fade Dist: %f", distance3(entry->world_fade_mid, cam_pos) - entry->world_fade_radius);
	y++;
	gfxXYprintf(1, y++, "Min: % 4.4f, % 4.4f, % 4.4f", world_min[0], world_min[1], world_min[2]);
	gfxXYprintf(1, y++, "Max: % 4.4f, % 4.4f, % 4.4f", world_max[0], world_max[1], world_max[2]);
	y++;

	if (entry->base_entry.type > WCENT_BEGIN_DRAWABLES)
	{
		const Model *model = worldDrawableEntryGetModel((WorldDrawableEntry *)entry, NULL, NULL, NULL, NULL);
		if (model)
			gfxXYprintf(1, y++, "Model: %s", model->name);
	}

	gfxDrawBox3D(shared_bounds->local_min, shared_bounds->local_max, bounds->world_matrix, CreateColorRGB(255, 0, 255), 1);
}

F32 gfxGetApproxPassVertexShaderTime(RdrDrawListPassStats *pass_stats, VSCost costs[VS_COST_SIZE], bool high_detail_only, bool old_intel_times)
{
	struct {
		const char *name;
		int count;
		F32 value;
	} counts[] = {
		// Sorted roughly by cost
		{"Skinned", high_detail_only ? 0 : 
					pass_stats->triangles_drawn[ROC_CHARACTER],
					old_intel_times?0.000386:0.000049707},
		{"Terrain", high_detail_only ? 0 : 
					pass_stats->triangles_drawn[ROC_TERRAIN],
					old_intel_times?0.0006:0.00001196272},

					// I bumped the cost up to something that seems a little more accurate on my card (a GTX 260).  On further inspection,
					// this should not take into account z-prepass, or anything like that, since that would be a different set of values in
					// pass_stats [RMARR - 10/4/12]
		{"Regular", high_detail_only ? (pass_stats->triangles_drawn[ROC_WORLD_HIGH_DETAIL]) : 
					(pass_stats->triangles_drawn[ROC_WORLD] + 
					pass_stats->triangles_drawn[ROC_SKY]), 
					old_intel_times?0.000358:1.5e-5f},
		{"FX",		high_detail_only ? 0 : 
					pass_stats->triangles_drawn[ROC_FX],
					old_intel_times?0.000358:0.00001281546},
	};
	F32 ret=0;
	int i;
	STATIC_INFUNC_ASSERT(ARRAY_SIZE(counts) == VS_COST_SIZE);
	for (i=0; i<ARRAY_SIZE(counts); i++) {
		ret += counts[i].count * counts[i].value;
		if (costs) {
			costs[i].name = counts[i].name;
			costs[i].triangles = counts[i].count;
			costs[i].ms = counts[i].count * counts[i].value;
		}
	}
	return ret;
}

static Color colorFromFPS(F32 fps)
{
	if (fps < 30) {
		return ColorRed;
	} else if (fps < 35) {
		return ColorYellow;
	} else {
		return ColorGreen;
	}
}

static int gfxCheckMonitor(int y)
{
#if !PLATFORM_CONSOLE
	int indices[2];
	int count;
	int i;
	RdrDevice *device = gfxGetActiveDevice();
	typedef struct MonitorValues {
		int xpos, ypos, width, height, fullscreen, maximized, windowed_fullscreen;
	} MonitorValues;
	MonitorValues current = {0};
	static MonitorValues lastcheck;
	static int result;
	assert(device);

	rdrGetDeviceSize(device, &current.xpos, &current.ypos, &current.width, &current.height, NULL, &current.fullscreen, &current.maximized, &current.windowed_fullscreen);

	if (memcmp(&current, &lastcheck, sizeof(current))==0)
	{
		// Use cached
		gfx_state.timeSinceWindowChange += gfx_state.frame_time;
	} else {
		memcpy(&lastcheck, &current, sizeof(current));
		gfx_state.timeSinceWindowChange = 0;

		// Check that we're running on the appropriate monitor!
		count = multiMonGetMonitorIndices(rdrGetWindowHandle(device), indices, ARRAY_SIZE(indices));
		result = count > 1 || count==1 && indices[0] != device->primary_monitor;
	}
	gfx_state.shouldShowRestoreButtons = (current.fullscreen || current.windowed_fullscreen);

	if (result && !IsUsingVista()) // Vista/W7's display driver model seems to have no performance problems with spanning/switching monitors
	{
		char *messages[] = {"GraphicsLib.MultiMonPerfA0",
			"GraphicsLib.MultiMonPerfA1",
			"GraphicsLib.MultiMonPerfA2",
			"GraphicsLib.MultiMonPerfB0",
			"GraphicsLib.MultiMonPerfB1",
			"GraphicsLib.MultiMonPerfB2",
		};
		for (i=0; i<((count==1 && gfx_state.settings.monitor_index_plus_one == 0)?(int)ARRAY_SIZE(messages):3); i++) 
		{
			const char *translated = TranslateMessageKey(messages[i]);
			int len;
			if (!translated)
				translated = messages[i];
			len = (int)UTF8GetLength(translated);
			gfxXYprintfColor(TEXT_JUSTIFY + 80 - len,y++, 255, 127, 127, 255, "%s", translated);
		}
	}

	if (gfx_state.debug.check_window_placement && !current.maximized && !current.fullscreen && !current.windowed_fullscreen)
	{
		// Check that we're on a monitor, and can fit on the monitor
		MONITORINFOEX info;
		int maxw, maxh;
		bool bBad=false;

		MonitorValues new_pos = current;

		multiMonGetMonitorInfo(device->primary_monitor, &info);
		maxw = info.rcWork.right - info.rcWork.left - (GetSystemMetrics(SM_CXSIZEFRAME) * 2);
		maxh = info.rcWork.bottom - info.rcWork.top - (GetSystemMetrics(SM_CYSIZEFRAME) * 2 + GetSystemMetrics(SM_CYSIZE) + 1);
		if (maxw>0 && maxh>0)
		{
			if (new_pos.width > maxw)
			{
				new_pos.width = maxw;
				bBad = true;
			}
			if (new_pos.height > maxh)
			{
				new_pos.height = maxh;
				bBad = true;
			}
		}
		if (new_pos.xpos < info.rcWork.left)
		{
			new_pos.xpos = info.rcWork.left;
			bBad = true;
		}
		if (new_pos.xpos + new_pos.width + GetSystemMetrics(SM_CXSIZEFRAME)*2 > info.rcWork.right)
		{
			new_pos.xpos = info.rcWork.right - new_pos.width - GetSystemMetrics(SM_CXSIZEFRAME)*2;
			bBad = true;
		}
		if (new_pos.ypos < info.rcWork.top)
		{
			new_pos.ypos = info.rcWork.top;
			bBad = true;
		}
		if (new_pos.ypos + new_pos.height + GetSystemMetrics(SM_CYSIZE) + GetSystemMetrics(SM_CYSIZEFRAME)*2 > info.rcWork.bottom)
		{
			new_pos.ypos = info.rcWork.bottom - new_pos.height - GetSystemMetrics(SM_CYSIZE) - GetSystemMetrics(SM_CYSIZEFRAME)*2;
			bBad = true;
		}

		if (bBad)
		{
			// Check other monitors
			for (i=0; i<multiMonGetNumMonitors(); i++)
			{
				bool bThisBad=false;
				multiMonGetMonitorInfo(i, &info);
				maxw = info.rcWork.right - info.rcWork.left - (GetSystemMetrics(SM_CXSIZEFRAME) * 2);
				maxh = info.rcWork.bottom - info.rcWork.top - (GetSystemMetrics(SM_CYSIZEFRAME) * 2 + GetSystemMetrics(SM_CYSIZE) + 1);
				if (maxw>0 && maxh>0)
				{
					if (current.width > maxw)
						bThisBad = true;
					if (current.height > maxh)
						bThisBad = true;
				}
				if (current.xpos < info.rcWork.left)
					bThisBad = true;
				if (current.xpos + current.width + GetSystemMetrics(SM_CXSIZEFRAME)*2 > info.rcWork.right)
					bThisBad = true;
				if (current.ypos < info.rcWork.top)
					bThisBad = true;
				if (current.ypos + current.height + GetSystemMetrics(SM_CYSIZE) + GetSystemMetrics(SM_CYSIZEFRAME)*2 > info.rcWork.bottom)
					bThisBad = true;

				if (!bThisBad)
					bBad = false;
			}
		}

		if (bBad && gfx_state.debug.check_window_placement == 1)
		{
			PERFINFO_AUTO_START("gfxScreenSetPosAndSize", 1);
			// Handle it
			gfxScreenSetPosAndSize(new_pos.xpos, new_pos.ypos, new_pos.width, new_pos.height);
			PERFINFO_AUTO_STOP();
		}
		gfx_state.debug.check_window_placement--;
	}
#endif
	return y;
}

static void texGenSpeedTest(void)
{
	U32 *data = calloc(512*512/8, 1);
	U8 *data_out = malloc(512*512/2);
	int i;
	int timer = timerAlloc();
	for (i=0; i<512*512; i++)
	{
		if (i%2)
			SETB(data, i);
	}

	while (true)
	{
		F32 elapsed;
		timerStart(timer);
		for (i=0; i<100; i++)
		{
			dxtCompressBitfield(data, 512, 512, ColorMagenta, data_out, 512*512/2);
		}
		assert(data_out[0] == 0);
		assert(data_out[2] == 0x1f);
		assert(data_out[3] == 0xf8);
		assert(data_out[4] == 0x77);
		assert(data_out[5] == 0x77);
		assert(data_out[6] == 0x77);
		assert(data_out[7] == 0x77);
		elapsed = timerElapsed(timer);
		printf("%1.5fms\n", elapsed * 1000);
	}

	free(data);
	free(data_out);
	timerFree(timer);
}

static void gfxDXTTexGenTest(void)
{
	static BasicTexture *btex=NULL;
	if (!btex)
	{
		int x, y, fibx[3]={1,1,1};
		int ix, iy;
		bool bx=false;
		U32 *data = calloc(512*512/8, 1);
		U8 *data_out = memrefAlloc(512*512/2);
		btex = texGenNew(512, 512, "test", TEXGEN_NORMAL, WL_FOR_UI);
		for (x=0; x<512; )
		{
			bx=!bx;
			fibx[0] = fibx[1] + fibx[2];
			fibx[2] = fibx[1]; fibx[1] = fibx[0];
			for (ix=0; ix<fibx[2] && x<512; ix++, x++)
			{
				int fiby[3] = {1,1,1};
				bool by = true;
				for (y=0; y<512;)
				{
					by = !by;
					fiby[0] = fiby[1] + fiby[2];
					fiby[2] = fiby[1]; fiby[1] = fiby[0];
					for (iy=0; iy<fiby[2] && y<512; iy++, y++)
					{
						if (bx^by)
							SETB(data, (x+y*512));
					}
				}
			}
		}
		dxtCompressBitfield(data, 512, 512, ColorMagenta, data_out, 512*512/2);
		texGenUpdate(btex, data_out, RTEX_2D, RTEX_DXT1, 1, false, false, false, true);
		memrefDecrement(data_out);
		free(data);
	}

	display_sprite_tex(btex, 0, 0, GRAPHICSLIB_Z, 1, 1, 0xffffffff);
}

static int enableFontTest = 0;
AUTO_CMD_INT(enableFontTest, enableFontTest);

static void gfxDoFontTest()
{
	if (enableFontTest == 1)
	{
		GfxFont* myFont = gfxFontCreateFromName("Fonts/Game.font");
		int testSizes[] = {8,9,10,11,12,13,14,15,16,17,18,19,32, 48, 64, 128, 200}; 
		int i;
		F32 curY = 5;
		bool colorMode = false;

		for (i = 0; i < ARRAY_SIZE(testSizes); i++)
		{
			char buffer[1024];
			myFont->renderSize = testSizes[i];
			myFont->outline = (i % 2) ? 1 : 0;
			if (i % 4) colorMode = !colorMode;

			if (colorMode)
			{
				myFont->color.uiTopLeftColor = RGBAFromColor(ColorYellow);
				myFont->color.uiTopRightColor = RGBAFromColor(ColorYellow);
				myFont->color.uiBottomLeftColor = RGBAFromColor(ColorOrange);
				myFont->color.uiBottomRightColor = RGBAFromColor(ColorOrange);
			}
			else
			{
				myFont->color.uiTopLeftColor = RGBAFromColor(ColorWhite);
				myFont->color.uiTopRightColor = RGBAFromColor(ColorWhite);
				myFont->color.uiBottomLeftColor = RGBAFromColor(ColorWhite);
				myFont->color.uiBottomRightColor = RGBAFromColor(ColorWhite);
			}

			if (testSizes[i] == 128)
			{
				myFont->dropShadow = 1;
				myFont->dropShadowOffset[0] = 10;
				myFont->dropShadowOffset[1] = 10;
				myFont->softShadow = 1;
				myFont->softShadowSpread = 10;
			}
			else if (testSizes[i] == 200)
			{
				myFont->dropShadow = 1;
				myFont->softShadow = 0;
				myFont->dropShadowOffset[0] = 10;
				myFont->dropShadowOffset[1] = 10;
			}
			else
			{
				myFont->dropShadow = 0;
			}
			sprintf(buffer, "%d: %s", testSizes[i], "Egggle_jjjT.");
			gfxDrawLine(10, curY, GRAPHICSLIB_Z, 600, curY, ColorMagenta);
			gfxFontPrint(myFont, 10, curY, GRAPHICSLIB_Z, buffer, NULL);
			curY += gfxFontGetPixelHeight(myFont);
		}

		myFont->renderSize = 54;
		myFont->outline = 1;
		myFont->outlineWidth = 1;

		myFont->dropShadowOffset[0] = 2;
		myFont->dropShadowOffset[1] = 2;

		myFont->color.uiTopLeftColor = RGBAFromColor(ColorRed);
		myFont->color.uiTopRightColor = RGBAFromColor(ColorRed);
		myFont->color.uiBottomLeftColor = RGBAFromColor(ColorBlue);
		myFont->color.uiBottomRightColor = RGBAFromColor(ColorBlue);
		myFont->italicize = 1;
		gfxFontPrint(myFont, 10, curY, GRAPHICSLIB_Z, "Italic Test.", NULL);
		curY += gfxFontGetPixelHeight(myFont);

		myFont->italicize = 0;
		gfxFontPrint(myFont, 10, curY, GRAPHICSLIB_Z, "Italic Test.", NULL);
		curY += gfxFontGetPixelHeight(myFont);

		myFont->softShadow = 1;
		myFont->bold = 1;
		gfxFontPrint(myFont, 10, curY, GRAPHICSLIB_Z, "Italic Test.", NULL);
		curY += gfxFontGetPixelHeight(myFont);

		gfxFontFree(myFont);

	}
	else
	{
		GfxFont* myFont = gfxFontCreateFromName("Fonts/Game.font");
		F32 curY = 0;
		int i = 0;

		for (i = 0; i < 70; i++)
		{
			myFont->renderSize = 12;
			myFont->outline = 1;
			myFont->dropShadow = 1;

			gfxFontPrint(myFont, 10, curY, GRAPHICSLIB_Z, "GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG", NULL);
			curY += gfxFontGetPixelHeight(myFont);
		}

		gfxFontFree(myFont);
	}
}

typedef struct {
	U64 lightCombo;
	U32 lightComboCount;
}LightComboUsage;

int sortDebugLightComboUsage(const LightComboUsage **combo1, const LightComboUsage **combo2)
{
	if ((*combo1)->lightComboCount > (*combo2)->lightComboCount)
	{
		return 1;
	}
	if ((*combo1)->lightComboCount < (*combo2)->lightComboCount)
	{
		return -1;
	}
	return 0;

}

static int gfxDebugDrawLightComboUsage(int y)
{
	StashTable stLightComboUsage = rdrDrawListGetLightComboUsage();
	static LightComboUsage **eaLightComboUsage = NULL;
	int i;
	U64 total=0;

	eaClearEx(&eaLightComboUsage,NULL);
	FOR_EACH_IN_STASHTABLE2(stLightComboUsage, elem)
	{
		RdrMaterialShader key;
		LightComboUsage *lightComboContainer = calloc(1,sizeof(LightComboUsage));
		U64 val = stashElementGetInt(elem);
		key.lightMask = stashElementGetIntKey(elem) - 1;
		if (key.lightMask & LIGHT_MATERIAL_SHADER_SHADOW_BUFFER)
		{
			key.lightMask &= ~LIGHT_MATERIAL_SHADER_SHADOW_BUFFER;
			key.shaderMask = MATERIAL_SHADER_SHADOW_BUFFER;
		} else {
			key.shaderMask = 0;
		}
		total += val;
		lightComboContainer->lightCombo = key.key;
		lightComboContainer->lightComboCount = val;
		eaPush(&eaLightComboUsage, lightComboContainer);
	}
	FOR_EACH_END;

	eaQSort(eaLightComboUsage,sortDebugLightComboUsage);

	gfxXYprintf(1,y++,"CCLighting Stats:");
	for (i=0; i<MIN(30, eaSize(&eaLightComboUsage)); i++)
	{
		U64 keyval = eaLightComboUsage[eaSize(&eaLightComboUsage) - i - 1]->lightCombo;
		RdrMaterialShader key;
		U32 val = eaLightComboUsage[eaSize(&eaLightComboUsage) - i - 1]->lightComboCount;
		int j;
		int x0 = 17;
		key.key = keyval;
		if (key.shaderMask & MATERIAL_SHADER_SHADOW_BUFFER)
			gfxXYprintf(x0,y,"SB");
		for (j=0; j<MAX_NUM_OBJECT_LIGHTS; j++)
		{
			static const char *light_type_name[] = {"None", "Dir", "Point", "Spot", "Proj", "ERROR"};
			RdrLightType light_type = rdrGetLightType(key.lightMask, j);
			if (!light_type)
				continue;
			gfxXYprintf(x0 + 3 + 8*j,y,"%s%s", light_type_name[rdrGetSimpleLightType(light_type)], rdrIsShadowedLightType(light_type)?"Sh":"");
		}
		gfxXYprintf(4,y++,"%d (%1.0f%%)", val, val*100.f/ (F32)total);
	}

	return y;
}

static const char * gfxGetDisplayStyleTypeString(const DisplayParams * display)
{
	const char * style = NULL;
	if (display->fullscreen)
		style = "Fullscreen";
	else
	{
		if (display->windowed_fullscreen)
			style = "Windowed Fullscreen";
		else
		if (display->maximize)
			style = "Maximized";
		else
			style = "Windowed";
	}

	return style;
}

static const F32 LOADERHIST_PIXEL_PER_SECOND = 256.0f;

void gfxDrawFileLoaderHistory(F32 y)
{
	FileLoaderStats stats;
	Color loadActTypes[ 4 ] = { { 0xff, 0, 0, 0xff }, { 0, 0xff, 0, 0xff }, 
		{ { 0, 0, 0xff, 0xff } }, { 0xff, 0, 0xff, 0xff  } };
	U32 actionCounter, action;
	S64 cpuTicksNow;
	int width, height;
	int valueK = 1;

	gfxGetActiveSurfaceSizeInline(&width, &height);

	y = height - y;
	fileLoaderGetStats(&stats);
	action = stats.historyPos;
	cpuTicksNow = timerCpuTicks64();

	for (actionCounter = MAX_ACTION_HIST; actionCounter; --actionCounter)
	{
		const FileLoaderActionTracking * actionPtr = stats.actionHist + action;
		F32 actStart, actEnd;

		actStart = timerSeconds64(cpuTicksNow - actionPtr->startTime);
		actEnd = timerSeconds64(cpuTicksNow - actionPtr->endTime);

		gfxDrawBox(width - actStart * LOADERHIST_PIXEL_PER_SECOND, y - (actionPtr->sizeBytes ? log10(actionPtr->sizeBytes) * 32 : 32), width - actEnd * LOADERHIST_PIXEL_PER_SECOND + 1, y, 0.0f, loadActTypes[actionPtr->actionType]);

		action = (action + 1) % MAX_ACTION_HIST;
	}
	for (actionCounter = 0; actionCounter < 30; ++actionCounter)
	{
		F32 tickMarkSeconds = actionCounter * -0.25f;
		F32 tickMarkX = width + tickMarkSeconds * LOADERHIST_PIXEL_PER_SECOND;

		gfxDrawLine(tickMarkX, y - 16, 0.0f, tickMarkX, y + 16, ColorBlue);
		if (!(actionCounter & 1))
			gfxXYZFprintfColor_s(tickMarkX, y + 16, GRAPHICSLIB_Z+1, ColorWhite, "%4.2f s", tickMarkSeconds);
	}
	for (actionCounter = 0; actionCounter < 6; ++actionCounter, valueK = valueK * 10)
	{
		F32 barY = y - actionCounter * 32;
		gfxDrawLine(0, barY, 0.0f, width, barY, ColorRed);
		gfxXYZFprintfColor_s(16, barY + 8, GRAPHICSLIB_Z+1, ColorWhite, "%dK", valueK);
	}
}

void gfxDebugDrawMonitorLayout(F32 x, F32 y)
{
	int monitor_index, monitor_count = multiMonGetNumMonitors();
	DisplayParams display;
	RECT totalRect = { 0 }, wnd_rect;
	RdrDevice * currentDevice = NULL;
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
	F32 scale;

	currentDevice = gfxGetActiveDevice();

	for (monitor_index = 0; monitor_index < monitor_count; ++monitor_index)
	{
		MONITORINFOEX moninfo;
		multiMonGetMonitorInfo(monitor_index, &moninfo);
		if (monitor_index)
			UnionRect(&totalRect, &totalRect, &moninfo.rcMonitor);
		else
			totalRect = moninfo.rcMonitor;
	}
	totalRect.right -= totalRect.left;
	totalRect.bottom -= totalRect.top;
	scale = 256.0f / (totalRect.right);
	for (monitor_index = 0; monitor_index < monitor_count; ++monitor_index)
	{
		MONITORINFOEX moninfo;
		bool bIsDeviceOutputOnThisMonitor = device_infos[currentDevice->display_nonthread.preferred_adapter]->monitor_index == monitor_index;
		multiMonGetMonitorInfo(monitor_index, &moninfo);

		moninfo.rcMonitor.left -= totalRect.left;
		moninfo.rcMonitor.right -= totalRect.left;
		moninfo.rcMonitor.top -= totalRect.top;
		moninfo.rcMonitor.bottom -= totalRect.top;
		--moninfo.rcMonitor.right;
		--moninfo.rcMonitor.bottom;
		gfxDrawBox(x + scale * moninfo.rcMonitor.left, y + scale * moninfo.rcMonitor.top,
			x + scale * moninfo.rcMonitor.right, y + scale * moninfo.rcMonitor.bottom, GRAPHICSLIB_Z+1, 
			moninfo.dwFlags & MONITORINFOF_PRIMARY ? ColorGreen : ColorWhite);
		gfxXYZFprintfColor_s(x + scale * ( moninfo.rcMonitor.left + moninfo.rcMonitor.right ) * 0.5f,
			y + scale * ( moninfo.rcMonitor.top + moninfo.rcMonitor.bottom ) * 0.5f,
			GRAPHICSLIB_Z+1, bIsDeviceOutputOnThisMonitor ? ColorRed : ColorWhite, "%d", monitor_index + 1);
	}

	// show window position; shrunk 1 pixel to fit inside monitor rects
	rdrGetDeviceSizeEx(currentDevice, &display);
	display.xpos -= totalRect.left;
	display.ypos -= totalRect.top;
	gfxDrawBox(x + scale * display.xpos + 1, y + scale * display.ypos + 1, 
		x + scale * (display.xpos+display.width) - 1, y + scale * (display.ypos+display.height-1) - 1, GRAPHICSLIB_Z+1, 
		ColorRed);

	// show actual window position reported by Windows; shrunk 2 pixel to fit inside monitor rects & the engine's
	// tracking rect
	GetWindowRect(rdrGetWindowHandle(currentDevice), &wnd_rect);
	wnd_rect.left -= totalRect.left;
	wnd_rect.right -= totalRect.left;
	wnd_rect.top -= totalRect.top;
	wnd_rect.bottom -= totalRect.top;
	gfxDrawBox(x + scale * wnd_rect.left + 2, y + scale * wnd_rect.top + 2, 
		x + scale * wnd_rect.right - 3, y + scale * wnd_rect.bottom - 5, GRAPHICSLIB_Z+1, 
		ColorBlue);
}

#if RDR_ENABLE_DRAWLIST_HISTOGRAMS
static int gfxDrawBarGraph(int x, int y, const char *label, const int *distribution, size_t distribution_size, const int *buckets)
{
	size_t i;
	int barY = gfxXYgetY(y);

	gfxXYprintf(x, y, "%s", label);
	++y;

	for (i = 0; i < distribution_size; ++i)
	{
		int barX = gfxXYgetX((10 + i * 5));

		gfxDrawBox(barX - 10.0f, barY - distribution[i] * 0.1f, 
			barX + 10.0f, barY, GRAPHICSLIB_Z+1, 
			ColorBlue);
		gfxXYprintf((10 + i * 5), y, "%d", distribution[i]);
		gfxXYprintf((10 + i * 5), y + 1, "%d", buckets[i]);
	}
	++y;

	return y;
}
#endif

void gfxDisplayDebugInterface2D(int in_editor)
{
	GfxPerDeviceState *device_state = gfx_state.currentDevice;
	int bShowVerboseIndicators = !(gfxConsoleVisible() /* || game_state.disable2Dgraphics || demoIsPlaying() */);
	GfxCameraView *camera_view = gfxGetActiveCameraView();
	WorldRegion *region = NULL;
	Vec3 cam_pos;
	int y=1;
	bool bIsDevelopmentMode = isDevelopmentMode();

	PERFINFO_AUTO_START_FUNC_PIX();

	gfxGetActiveCameraPos(cam_pos);
	if(!gbNo3DGraphics)
		region = worldGetWorldRegionByPos(cam_pos);

#if DEBUG_TEXLOADCHECK
	gfxDebugShowTexLoad();
#endif

#define TEST_PRINTOUT(var) \
	if (dbg_state.test##var)											\
		gfxXYprintf(1, y, "test" #var " %f", dbg_state.test##var);		\
	y++;

	TEST_PRINTOUT(1);
	TEST_PRINTOUT(2);
	TEST_PRINTOUT(3);
	TEST_PRINTOUT(4);
	TEST_PRINTOUT(5);
	TEST_PRINTOUT(6);
	TEST_PRINTOUT(7);
	TEST_PRINTOUT(8);
	TEST_PRINTOUT(9);
	
	if (gfx_state.debug.show_settings)
	{
		const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
		RdrDevice * curDev = device_state->rdr_device;
		RdrSurface * primSurf = rdrGetPrimarySurface(curDev);
		DisplayParams * display = &curDev->display_nonthread;
		int inpSize[2];

		inpGetDeviceScreenSize(inpSize);
		gfxXYprintf(1,y++,"Settings %s %s: (%d, %d) %d x %d InpSize %d x %d PrimSurf %d x %d", 
			device_infos[curDev->display_nonthread.preferred_adapter]->type,
			gfxGetDisplayStyleTypeString(display),
			display->xpos, display->ypos, display->width, display->height, inpSize[0], inpSize[1], 
			primSurf->width_nonthread, primSurf->height_nonthread);
		gfxDebugDrawMonitorLayout(gfxXYgetX(1), gfxXYgetY(y));
	}

	if (gfx_state.debug.show_exposure_transform)
	{
		const char * exposure_xfrm_debug_state = "";
		const char * lum_max_debug_state = "";

		if (gfx_state.debug.hdr_lock_ldr_xform && gfx_state.debug.hdr_lock_hdr_xform)
			exposure_xfrm_debug_state = " [locked]";
		else
		if (gfx_state.debug.hdr_lock_hdr_xform)
			exposure_xfrm_debug_state = " [hdr locked]";
		else
		if (gfx_state.debug.hdr_lock_ldr_xform)
			exposure_xfrm_debug_state = " [ldr locked]";

		// the priority here should match the override priority in updateLuminanceQuery
		if (gfx_state.debug.hdr_force_luminance_measurement)
			lum_max_debug_state = " [forced]";
		else
		if (gfx_state.debug.hdr_use_immediate_luminance_measurement)
			lum_max_debug_state = " [immediate]";

		gfxXYprintf(1,y++,"Exposure Transform%s: %.4f, %.4f, %.4f, %.4f", exposure_xfrm_debug_state,
			gfx_state.debug.exposure_transform[0], gfx_state.debug.exposure_transform[1], gfx_state.debug.exposure_transform[2], gfx_state.debug.exposure_transform[3]);
		gfxXYprintf(1,y++,"Luminance max%s: %.4f", lum_max_debug_state, gfx_state.debug.measured_luminance);
	}
	else
		y++;

	if (gfx_state.debug.show_light_range && camera_view)
		gfxXYprintf(1,y++,"Light Range: %.4f (adapted) / %.4f (desired)", camera_view->adapted_light_range, camera_view->desired_light_range);
	else
		y++;

	y++;

	if (gfx_state.debug.show_vs_time)
	{
		RdrDrawListPassStats *visual_pass_stats = &gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL];
		gfxXYprintf(1,y++,"Est 7800 VS cost for visual pass:     %7.1fms", gfxGetApproxPassVertexShaderTime(visual_pass_stats, NULL, false, false));
		gfxXYprintf(1,y++,"Est 7800 VS cost for high detail:     %7.1fms", gfxGetApproxPassVertexShaderTime(visual_pass_stats, NULL, true, false));
		gfxXYprintf(1,y++,"Est Intel VS cost for visual pass:     %7.1fms", gfxGetApproxPassVertexShaderTime(visual_pass_stats, NULL, false, true));
	}

#if RDR_ENABLE_DRAWLIST_HISTOGRAMS
	if (gfx_state.debug.show_draw_list_histograms)
	{
		RdrDrawListPassStats *visual_pass_stats = &gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL];
		RdrDrawListPassStats *zprepass_pass_stats = &gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_ZPREPASS];
		RdrDrawListPassStats *shadow_pass_stats = &gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_SHADOW];
		RdrDrawListPassStats *hdr_pass_stats = &gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL_HDR];

		int i;
		int depthBuckets[RDR_DEPTH_BUCKET_COUNT];
		int sizeBuckets[RDR_DEPTH_BUCKET_COUNT];

		for (i = 0; i < RDR_DEPTH_BUCKET_COUNT; ++i)
			depthBuckets[i] = (int)rdr_state.depthHistConfig.getBucketsSplitFn(rdr_state.depthHistConfig.scale, rdr_state.depthHistConfig.initial, i);
		y = 8 + gfxDrawBarGraph(1, y, "Depths", zprepass_pass_stats->depth_histogram, RDR_DEPTH_BUCKET_COUNT, depthBuckets);

		for (i = 0; i < RDR_SIZE_BUCKET_COUNT; ++i)
			sizeBuckets[i] = (int)rdr_state.sizeHistConfig.getBucketsSplitFn(rdr_state.sizeHistConfig.scale, rdr_state.sizeHistConfig.initial, i);
		y = 8 + gfxDrawBarGraph(1, y, "Sizes", zprepass_pass_stats->size_histogram, RDR_SIZE_BUCKET_COUNT, sizeBuckets);
	}
#endif

	if (gfx_state.debug.show_frame_counters)
	{
		int firstY = y;
		RdrDrawListPassStats *visual_pass_stats = &gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL];
		RdrDrawListPassStats *zprepass_pass_stats = &gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_ZPREPASS];
		RdrDrawListPassStats *shadow_pass_stats = &gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_SHADOW];
		RdrDrawListPassStats *hdr_pass_stats = &gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL_HDR];

		// to support scrolling:
		y -= gfx_state.debug.show_frame_counters - 1;

		gfxXYprintf(1,y++,"Objects drawn (non shadow casters):   %7d (%7d opaque, %7d alpha)", visual_pass_stats->opaque_objects_drawn + visual_pass_stats->alpha_objects_drawn, visual_pass_stats->opaque_objects_drawn, visual_pass_stats->alpha_objects_drawn);
		gfxXYprintf(1,y++,"  - world:                            %7d", visual_pass_stats->objects_drawn[ROC_WORLD]);
		gfxXYprintf(1,y++,"  - world (high detail):              %7d", visual_pass_stats->objects_drawn[ROC_WORLD_HIGH_DETAIL]);
		gfxXYprintf(1,y++,"  - terrain:                          %7d", visual_pass_stats->objects_drawn[ROC_TERRAIN]);
		gfxXYprintf(1,y++,"  - characters:                       %7d", visual_pass_stats->objects_drawn[ROC_CHARACTER]);
		gfxXYprintf(1,y++,"  - fx:                               %7d", visual_pass_stats->objects_drawn[ROC_FX]);
		gfxXYprintf(1,y++,"  - sky:                              %7d", visual_pass_stats->objects_drawn[ROC_SKY]);
		gfxXYprintf(1,y++,"  - primitives:                       %7d", visual_pass_stats->objects_drawn[ROC_PRIMITIVE]);
		gfxXYprintf(1,y++,"  - editor only:                      %7d", visual_pass_stats->objects_drawn[ROC_EDITOR_ONLY]);
		gfxXYprintf(1,y++,"  - renderer:                         %7d", visual_pass_stats->objects_drawn[ROC_RENDERER]);
		gfxXYprintf(1,y++,"Objects drawn (shadow casters):       %7d", shadow_pass_stats->opaque_objects_drawn + shadow_pass_stats->alpha_objects_drawn);
		gfxXYprintf(1,y++,"Objects drawn (z-prepass):            %7d", zprepass_pass_stats->opaque_objects_drawn + zprepass_pass_stats->alpha_objects_drawn);
		gfxXYprintf(1,y++,"Objects drawn (HDR pass):             %7d (%7d opaque, %7d alpha)", hdr_pass_stats->opaque_objects_drawn + hdr_pass_stats->alpha_objects_drawn, hdr_pass_stats->opaque_objects_drawn, hdr_pass_stats->alpha_objects_drawn);
		gfxXYprintf(1,y++,"Objects drawn (Total):                %7d", gfx_state.debug.last_frame_counts.opaque_objects_drawn + gfx_state.debug.last_frame_counts.alpha_objects_drawn);
		gfxXYprintf(1,y++,"Objects failed:                       %7d", gfx_state.debug.last_frame_counts.draw_list_stats.failed_draw_this_frame);
		gfxXYprintf(1,y++,"Instanced objects (opaque):           %7d", gfx_state.debug.last_frame_counts.draw_list_stats.opaque_instanced_objects);
		gfxXYprintf(1,y++,"Instanced objects (alpha):            %7d", gfx_state.debug.last_frame_counts.draw_list_stats.alpha_instanced_objects);
		gfxXYprintf(1,y++,"Triangles drawn (non shadow casters): %7d (%7d opaque, %7d alpha)", visual_pass_stats->opaque_triangles_drawn + visual_pass_stats->alpha_triangles_drawn, visual_pass_stats->opaque_triangles_drawn, visual_pass_stats->alpha_triangles_drawn);
		gfxXYprintf(1,y++,"  - world:                            %7d", visual_pass_stats->triangles_drawn[ROC_WORLD]);
		gfxXYprintf(1,y++,"  - world (high detail):              %7d", visual_pass_stats->triangles_drawn[ROC_WORLD_HIGH_DETAIL]);
		gfxXYprintf(1,y++,"  - terrain:                          %7d", visual_pass_stats->triangles_drawn[ROC_TERRAIN]);
		gfxXYprintf(1,y++,"  - characters:                       %7d", visual_pass_stats->triangles_drawn[ROC_CHARACTER]);
		gfxXYprintf(1,y++,"  - fx:                               %7d", visual_pass_stats->triangles_drawn[ROC_FX]);
		gfxXYprintf(1,y++,"  - sky:                              %7d", visual_pass_stats->triangles_drawn[ROC_SKY]);
		gfxXYprintf(1,y++,"  - primitives:                       %7d", visual_pass_stats->triangles_drawn[ROC_PRIMITIVE]);
		gfxXYprintf(1,y++,"  - editor only:                      %7d", visual_pass_stats->triangles_drawn[ROC_EDITOR_ONLY]);
		gfxXYprintf(1,y++,"  - renderer:                         %7d", visual_pass_stats->triangles_drawn[ROC_RENDERER]);
		gfxXYprintf(1,y++,"Triangles drawn (shadow casters):     %7d", shadow_pass_stats->opaque_triangles_drawn + shadow_pass_stats->alpha_triangles_drawn);
		gfxXYprintf(1,y++,"Triangles drawn (z-prepass):          %7d", zprepass_pass_stats->opaque_triangles_drawn + zprepass_pass_stats->alpha_triangles_drawn);
		gfxXYprintf(1,y++,"Triangles drawn (HDR pass):           %7d (%7d opaque, %7d alpha)", hdr_pass_stats->opaque_triangles_drawn + hdr_pass_stats->alpha_triangles_drawn, hdr_pass_stats->opaque_triangles_drawn, hdr_pass_stats->alpha_triangles_drawn);
		gfxXYprintf(1,y++,"Triangles drawn (Total):              %7"FORM_LL"d", gfx_state.debug.last_frame_counts.opaque_triangles_drawn + gfx_state.debug.last_frame_counts.alpha_triangles_drawn);
		gfxXYprintf(1,y++,"Sprites drawn:                        %7d", gfx_state.debug.last_frame_counts.sprites_drawn);
		gfxXYprintf(1,y++,"Sprite Primitives drawn:              %7d", gfx_state.debug.last_frame_counts.sprite_primitives_drawn);
		gfxXYprintf(1,y++,"Postprocess calls:                    %7d", gfx_state.debug.last_frame_counts.postprocess_calls);
		gfxXYprintf(1,y++,"Est 7800 VS cost for visual pass:     %7.1fms", gfxGetApproxPassVertexShaderTime(visual_pass_stats, NULL, false, false));
		gfxXYprintf(1,y++,"Est 7800 VS cost for high detail:     %7.1fms", gfxGetApproxPassVertexShaderTime(visual_pass_stats, NULL, true, false));
		gfxXYprintf(1,y++,"Est Intel VS cost for visual pass:     %7.1fms", gfxGetApproxPassVertexShaderTime(visual_pass_stats, NULL, false, true));
		gfxXYprintf(1,y++,"Est Intel VS cost for high detail:     %7.1fms", gfxGetApproxPassVertexShaderTime(visual_pass_stats, NULL, true, true));
		y++;
		gfxXYprintf(1,y++,"Lights drawn:                         %7d", gfx_state.debug.last_frame_counts.lights_drawn);
		gfxXYprintf(1,y++,"Device locks:                         %7d", gfx_state.debug.last_frame_counts.device_locks);
		gfxXYprintf(1,y++,"Unique shader graphs referenced:      %7d", gfx_state.debug.last_frame_counts.unique_shader_graphs_referenced);
		gfxXYprintf(1,y++,"Unique shaders referenced:            %7d", gfx_state.debug.last_frame_counts.unique_shaders_referenced);
		gfxXYprintf(1,y++,"Unique materials referenced:          %7d", gfx_state.debug.last_frame_counts.unique_materials_referenced);
		gfxXYprintf(1,y++,"ms/f:                                 %7.1f", gfx_state.debug.last_frame_counts.ms);
		gfxXYprintf(1,y++,"GPU Bound this frame:                 %7d", gfx_state.debug.last_frame_counts.gpu_bound);
		gfxXYprintf(1,y++,"Total Skeletons:                      %7d", gfx_state.debug.last_frame_counts.total_skeletons);
		gfxXYprintf(1,y++,"Drawn Skeletons:                      %7d", gfx_state.debug.last_frame_counts.drawn_skeletons);
		gfxXYprintf(1,y++,"Drawn Skeletons:                      %7d", gfx_state.debug.last_frame_counts.drawn_skeleton_shadows);
		gfxXYprintf(1,y++,"World Animation Updates:              %7d", gfx_state.debug.last_frame_counts.world_animation_updates);
		y++;
		gfxXYprintf(1,y++,"World Cell hits:                      %7d", gfx_state.debug.last_frame_counts.cell_hits);
		gfxXYprintf(1,y++,"World Cell tests:                     %7d", gfx_state.debug.last_frame_counts.cell_tests);
		gfxXYprintf(1,y++,"World Cell fog culls:                 %7d", gfx_state.debug.last_frame_counts.cell_fog_culls);
		gfxXYprintf(1,y++,"World Cell culls:                     %7d", gfx_state.debug.last_frame_counts.cell_culls);
		gfxXYprintf(1,y++,"World Cell zo culls:                  %7d", gfx_state.debug.last_frame_counts.cell_zoculls);
		y++;
		gfxXYprintf(1,y++,"World Cell Entry hits:                %7d", gfx_state.debug.last_frame_counts.entry_hits);
		gfxXYprintf(1,y++,"World Cell Entry tests:               %7d", gfx_state.debug.last_frame_counts.entry_tests);
		gfxXYprintf(1,y++,"World Cell Entry dist culls:          %7d", gfx_state.debug.last_frame_counts.entry_dist_culls);
		gfxXYprintf(1,y++,"World Cell Entry fog culls:           %7d", gfx_state.debug.last_frame_counts.entry_fog_culls);
		gfxXYprintf(1,y++,"World Cell Entry culls:               %7d", gfx_state.debug.last_frame_counts.entry_culls);
		gfxXYprintf(1,y++,"World Cell Entry zo culls:            %7d", gfx_state.debug.last_frame_counts.entry_zoculls);
		y++;
		gfxXYprintf(1,y++,"World Cell Welded Entry hits:         %7d", gfx_state.debug.last_frame_counts.welded_entry_hits);
		gfxXYprintf(1,y++,"World Cell Welded Inst hits:          %7d", gfx_state.debug.last_frame_counts.welded_instance_hits);
		gfxXYprintf(1,y++,"World Cell Welded Inst dist culls:    %7d", gfx_state.debug.last_frame_counts.welded_instance_dist_culls);
		gfxXYprintf(1,y++,"World Cell Welded Inst tests:         %7d", gfx_state.debug.last_frame_counts.welded_instance_tests);
		gfxXYprintf(1,y++,"World Cell Welded Inst culls:         %7d", gfx_state.debug.last_frame_counts.welded_instance_culls);
		gfxXYprintf(1,y++,"World Cell Welded Inst draws:         %7d", gfx_state.debug.last_frame_counts.welded_instance_draws);
		y++;
		gfxXYprintf(1,y++,"Terrain hits:                         %7d", gfx_state.debug.last_frame_counts.terrain_hits);
		gfxXYprintf(1,y++,"Terrain tests:                        %7d", gfx_state.debug.last_frame_counts.terrain_tests);
		gfxXYprintf(1,y++,"Terrain culls:                        %7d", gfx_state.debug.last_frame_counts.terrain_culls);
		gfxXYprintf(1,y++,"Terrain zo culls:                     %7d", gfx_state.debug.last_frame_counts.terrain_zoculls);
		gfxXYprintf(1,y++,"World Cell Binned Entry hits:         %7d", gfx_state.debug.last_frame_counts.binned_entry_hits);
		gfxXYprintf(1,y++,"World Cell Binned Entry tests:        %7d", gfx_state.debug.last_frame_counts.binned_entry_tests);
		gfxXYprintf(1,y++,"World Cell Binned Entry culls:        %7d", gfx_state.debug.last_frame_counts.binned_entry_culls);
		gfxXYprintf(1,y++,"World Cell Binned Entry zo culls:     %7d", gfx_state.debug.last_frame_counts.binned_entry_zoculls);
		
#if TRACK_FRUSTUM_VISIBILITY_HISTOGRAM
		if (gfx_state.debug.show_frame_counters >= 2)
		{
			// this code analyzes the subsets of objects in various frustums/draw passes, e.g. the cascaded
			// shadowmap levels
			const U16 * hist = gfx_state.debug.last_frame_counts.cell_frustum_overlap_hist;
			int frustTotal[4] = {0}, frustIndex, subsetIndex, frustumBit, frustAll = 0;
			for (frustIndex = 1, frustumBit = 1; frustIndex < 4; ++frustIndex, frustumBit <<= 1)
			{
				// subsetIndex iterates over all the other subsets
				for (subsetIndex = 0; subsetIndex < 7; ++subsetIndex)
				{
					// insert 1 into the subset number at frustIndex, frustumBit happens to be in the right spot!
					int lowPart = subsetIndex & (frustumBit - 1);
					int highPart = (subsetIndex & ~(frustumBit - 1)) << 1;
					int subsetNum = highPart | frustumBit | lowPart;
					frustTotal[frustIndex] += hist[subsetNum];
				}
			}

			//Frustum vs entry visibility flag mask for four frustums
			//		0	1	2	3	4	5	6	7	8	9	10	11	12	13	14	15
			//B0		x		x		x		x		x		x		x		x
			//B1			x	x			x	x			x	x			x	x
			//B2					x	x	x	x					x	x	x	x
			//B3									x	x	x	x	x	x	x	x
			// count total individual objects
			for (subsetIndex = 0; subsetIndex < 16; ++subsetIndex)
			{
				// skip visual bucket
				if (!(subsetIndex & 1))
					frustAll += hist[subsetIndex];
			}

			if (frustAll)
			{
				// build a histogram of how many objects can be drawn with multiple viewports
				int objectDrawCallCounts[4] = {0}, totalDCs = 0, totalCombinedDCs = 0;
				for (subsetIndex = 0; subsetIndex < 16; ++subsetIndex)
				{
					// ignore visual pass
					if (!(subsetIndex & 1))
					{
						int drawCallsForBucket = get_num_bits_set(subsetIndex >> 1);
						if (drawCallsForBucket)
							objectDrawCallCounts[drawCallsForBucket - 1] += (int)hist[subsetIndex];
					}
				}
				totalCombinedDCs = objectDrawCallCounts[0] + objectDrawCallCounts[1] + objectDrawCallCounts[2];
				// count total draw calls to draw objects in all passes
				for (subsetIndex = 0; subsetIndex < 16; ++subsetIndex)
				{
					// skip visual bucket
					if (!(subsetIndex & 1))
					{
						int drawCallsForBucket = get_num_bits_set(subsetIndex >> 1);
						totalDCs += hist[subsetIndex] * drawCallsForBucket;
					}
				}
				gfxXYprintf(1,y++,"Dir shadowmap draw calls: 1 map %4d 2 maps %4d 3 maps %4d, %4d DCs / %4d DCs = %.2f pct %d obj", 
					objectDrawCallCounts[0], objectDrawCallCounts[1], objectDrawCallCounts[2], 
					totalCombinedDCs, totalDCs, totalCombinedDCs * 100.0f / totalDCs, frustAll );

				/* 
				// Display overlap efficiency for cascaded shadow map. Caveat: old code that assumes static ordering
				int overlap12 = hist[1] + hist[2] + hist[3] + hist[5] + hist[6] + hist[7],
					overlap23 = hist[2] + hist[3] + hist[4] + hist[5] + hist[6] + hist[7],
					overlap13 = hist[1] + hist[3] + hist[4] + hist[5] + hist[6] + hist[7];
				int totalEfficiency = frustTotal[0] + frustTotal[1] + frustTotal[2] + frustTotal[3];
				if (overlap12)
					overlap12 = (hist[3] + hist[7]) * 100 / overlap12;
				if (overlap23)
					overlap23 = (hist[6] + hist[7]) * 100 / overlap23;
				if (overlap13)
					overlap13 = (hist[5] + hist[7]) * 100 / overlap13;
				if (totalEfficiency)
					totalEfficiency = frustAll * 100 / totalEfficiency;

				gfxXYprintf(1,y++,"World Cell overlap hist: %4d %4d %4d %4d %4d %4d %4d %4d", 
					hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7] );

				gfxXYprintf(1,y++,"World Cell overlap efficiency: 1&2 share %2d%% 2&3 share %2d%% 1&3 share %2d%%", 
					overlap12, overlap23, overlap13);
				gfxXYprintf(1,y++,"World Cell overlap efficiency: %4d[%2d%%] %4d[%2d%%] %4d[%2d%%] / (%4d) - %2d%% Reduction", 
					frust1, frust1 * 100 / frustAll,
					frust2, frust2 * 100 / frustAll,
					frust3, frust3 * 100 / frustAll, frustAll, 100 - totalEfficiency);
				*/
			}
		}
#endif

		{
			static char *rdr_perf_debug_text=NULL;
			estrClear(&rdr_perf_debug_text);
			ParserWriteText(&rdr_perf_debug_text, parse_RdrDevicePerformanceValues, &gfx_state.currentDevice->rdr_perf_values, 0, 0, 0);
			gfxXYprintf(110, firstY - gfx_state.debug.show_frame_counters + 1, "Renderer Performance:%s", rdr_perf_debug_text);
		}

		y = firstY + 1;
	}

	if (rdr_state.showSpriteCounters)
	{
		RdrDeviceOperationPerfValues *op_perf = &gfx_state.currentDevice->rdr_perf_values.operations;
		RdrDeviceStateChangePerfValues *state_perf = &gfx_state.currentDevice->rdr_perf_values.state_changes;
		int i;

		gfxXYprintf(1, y++, "Sprite Draw Time:     %7.2f ms (estimated)", op_perf->sprite_draw_time * 1000);
		gfxXYprintf(1, y++, "Sprite Triangles:     %7d", op_perf->sprite_triangle_count);
		gfxXYprintf(1, y++, "Sprite Draw Calls:    %7d", op_perf->sprite_draw_call_count);
		gfxXYprintf(1, y++, "Sprite Pixels:        %7d", op_perf->sprite_pixel_count);
		gfxXYprintf(1, y++, "Scissor Rect Changes: %7d", state_perf->scissor_rect);

		for (i = 0; i < (RDR_SPRITE_SIZE_BUCKET_COUNT - 1); ++i)
		{
			gfxXYprintf(110, y++, "Sprite Size < %6d: %7d", round(sprite_histogram_sizes[i]), op_perf->sprite_count_histogram[i]);
		}
		gfxXYprintf(110, y++, "Sprite Size > %6d: %7d", round(sprite_histogram_sizes[i-1]), op_perf->sprite_count_histogram[i]);
	}

	if (gfx_state.debug.show_file_counters)
	{
		FileStats file_stats;
		static FileStats file_stats_total;
		fileGetStatsAndClear(&file_stats);
		shDoOperation(STRUCTOP_ADD, parse_FileStats, &file_stats_total, &file_stats);

#define DOIT(field) gfxXYprintf(1, y++, "%20s %3d (%5d)", #field, file_stats.field, file_stats_total.field);
		DOIT(fread_count);
		DOIT(fwrite_count);
		DOIT(fopen_count);
		DOIT(pig_reads);
		DOIT(pig_unzips);
		DOIT(fileloader_queues);
#undef DOIT
		gfxXYprintf(1, y++, "fileLoader loads pending: %3d", fileLoaderLoadsPending());
		y++;
	}

	if (gfx_state.debug.show_frame_counters || gfx_state.debug.show_frame_times)
	{
		F32 msPerTick = wlPerfGetMsPerTick();
		F32 left = gfx_state.debug.show_frame_counters ? 60 : 0;
		gfxXYprintf(left, y++, "Draw %f", gfx_state.debug.last_frame_counts.world_perf_counts.time_draw * msPerTick);
		gfxXYprintf(left, y++, "Queue %f", gfx_state.debug.last_frame_counts.world_perf_counts.time_queue * msPerTick);
		gfxXYprintf(left, y++, "WorldTrav %f", gfx_state.debug.last_frame_counts.world_perf_counts.time_queue_world * msPerTick);
		gfxXYprintf(left, y++, "Dyn %f", gfx_state.debug.last_frame_counts.world_perf_counts.time_anim * msPerTick);
		gfxXYprintf(left, y++, "Net %f", gfx_state.debug.last_frame_counts.world_perf_counts.time_net * msPerTick);
		gfxXYprintf(left, y++, "Wait %f", gfx_state.debug.last_frame_counts.world_perf_counts.time_wait_gpu * msPerTick);
		gfxXYprintf(left, y++, "UI %f", gfx_state.debug.last_frame_counts.world_perf_counts.time_ui * msPerTick);
		gfxXYprintf(left, y++, "Misc %f", gfx_state.debug.last_frame_counts.world_perf_counts.time_misc * msPerTick);
	}

	if (rdr_state.cclightingStats)
	{
		y = gfxDebugDrawLightComboUsage(y);
	}

	// Should be last of those who use "y"

	PERFINFO_AUTO_START("gfxStatusLineDraw", 1);

	gfxStatusLineDraw(); // Queues text to be drawn

	PERFINFO_AUTO_STOP_START("gfxDisplayThreadPerformance", 1);

	if (gfx_state.debug.threadPerf || gfx_state.debug.threadPerfAll)
		gfxDisplayThreadPerformance();

	PERFINFO_AUTO_STOP_START("gfxDisplayPerformanceInfo", 1);

	PERFINFO_AUTO_STOP_START("Misc Debug info", 1);

	gfxDisplayDebugWorldCellEntryInfo();

	gfxDisplayAccessLevelWarnings(in_editor);

	FOR_EACH_IN_EARRAY(gfx_state.debug.debug_sprites, const char, sprite_name)
	{
		AtlasTex *tex = atlasLoadTexture(sprite_name);
		BasicTexture *btex = texLoadBasic(sprite_name, TEX_LOAD_IN_BACKGROUND, WL_FOR_UTIL_BITINDEX);
		display_sprite(tex, gfxXYgetX(1), gfxXYgetY(y), GRAPHICSLIB_Z, 1, 1, 0xffffffff);
		display_sprite_tex(btex, gfxXYgetX(40), gfxXYgetY(y), GRAPHICSLIB_Z, 1, 1, 0xffffffff);
		display_sprite_NinePatch_test(tex, gfxXYgetX(70), gfxXYgetY(y), GRAPHICSLIB_Z, tex->width, tex->height, 0xffffffff);
		display_sprite_NinePatch_test(tex, gfxXYgetX(110), gfxXYgetY(y), GRAPHICSLIB_Z, 100, 50, 0xffffffff);
		y+=MAX(tex->height, 50) / 12 + 1;
	}
	FOR_EACH_END;

	if (gfx_state.debug.debug_sprite_3d.name)
	{
		BasicTexture *btex = texLoadBasic(gfx_state.debug.debug_sprite_3d.name, TEX_LOAD_IN_BACKGROUND, WL_FOR_UTIL_BITINDEX);
		display_sprite_effect_ex(NULL, btex, NULL, NULL,
			gfx_state.debug.debug_sprite_3d.x, gfx_state.debug.debug_sprite_3d.y, gfx_state.debug.debug_sprite_3d.z,
			gfx_state.debug.debug_sprite_3d.w / btex->width, gfx_state.debug.debug_sprite_3d.h / btex->height,
			0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
			0, 0, 1, 1,
			0, 0, 1, 1,
			0, 0, clipperGetCurrent(), RdrSpriteEffect_None, 1, SPRITE_3D | SPRITE_IGNORE_Z_TEST);
	}

	//gfxDXTTexGenTest();

	if(gfx_state.showMouseCoords)
	{
		int mx,my;
		mousePos(&mx, &my);
		gfxXYprintf(1,50,"Mouse Coords: %i, %i", mx, my);
	}

	if (gfx_state.debug.gfx_debug_info) {
		FileLoaderSummaryStats fileLoaderStats;
		int texLoadsTotal = texLoadsPending(1);
		int texLoads = texLoadsPending(0);
		int geoLoadsTotal = geoLoadsPending(1);
		int geoLoads = geoLoadsPending(0);
		int yvalue = TEXT_JUSTIFY + 60 - (gfx_status_line.index?1:0);
		fileLoaderGetSummaryStats(&fileLoaderStats);

		// counts
		gfxXYprintf(0, yvalue, "O:%d T:%d ShadowO:%d T:%d TexLoads: %d/%d RdrTexLoads: %d/%d %d GeoLoads: %d/%d RdrGeoLoads %d/%d %d ShaderCompiles: %d MiscLoads: %d  Tmplts: %d  Shaders: %d  Mtls: %d",
			gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL].opaque_objects_drawn + gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL].alpha_objects_drawn,
			gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL].opaque_triangles_drawn + gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL].alpha_triangles_drawn,
			gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_SHADOW].opaque_objects_drawn + gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_SHADOW].alpha_objects_drawn,
			gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_SHADOW].opaque_triangles_drawn + gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_SHADOW].alpha_triangles_drawn,
			texLoads, texLoadsTotal - texLoads,
			texSentThisFrame(), texLoadsQuota(), texRenderLoadsPending(),
			geoLoads, geoLoadsTotal - geoLoads,
			gfxGeoSentThisFrame(), gfxGeoQuotaPerFrame(), gfxGeoNumLoadsPending(),
			rdrShaderGetBackgroundShaderCompileCount() + gfxMaterialPreloadGetLoadingCount(),
			utilitiesLibLoadsPending(),
			gfx_state.debug.last_frame_counts.unique_shader_graphs_referenced,
			gfx_state.debug.last_frame_counts.unique_shaders_referenced,
			gfx_state.debug.last_frame_counts.unique_materials_referenced);
		gfxXYprintf(0, yvalue - 1, "Tex Mem: %.2f Loaded + %.2f Loading = %.2f MB / %.2fMB",
			texMemoryUsage[TEX_MEM_VIDEO] / (1024.0f * 1024),
			texMemoryUsage[TEX_MEM_LOADING] / (1024.0f * 1024),
			(texMemoryUsage[TEX_MEM_VIDEO] + texMemoryUsage[TEX_MEM_LOADING]) / (1024.0f * 1024), 
			tex_memory_allowed / (1024.0f * 1024));

		yvalue -= 3;
		gfxXYprintf(0, yvalue, "Acts/s: %f Load KB/s: %8.2f Non-idle Load KB/s: %8.2f Decomp KB/s: %8.2f Disk Idle %%/s: %5.2f",
			fileLoaderStats.actionsPerSec, fileLoaderStats.loadKBPerSec, fileLoaderStats.loadKBPerSecNonIdle, 
			fileLoaderStats.decompKBPerSec, fileLoaderStats.idleDiskPerSec * 100);
		yvalue += 6;
		gfxDrawFileLoaderHistory(yvalue > TEXT_JUSTIFY ? yvalue - TEXT_JUSTIFY : yvalue);
	}

	if (gfx_state.debug.fpsgraph_show) {
		int i;
		int width, height;
		gfxGetActiveSurfaceSizeInline(&width, &height);
		for (i=0; i<ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist); i++) {
			int index = (gfx_state.debug.fpsgraph.hist_index - i - 1 + ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist)) % ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist);
			float value = gfx_state.debug.fpsgraph.spfhist[index];
			float value_main = gfx_state.debug.fpsgraph.spfhist_main[index];
			Color color;
			if (value < 0.5) {
				color = colorFromRGBA(lerpRGBAColors(0x00FF00e0, 0xFFFF00e0, value * 2));
			} else {
				color = colorFromRGBA(lerpRGBAColors(0xFFFF00e0, 0xFF0000e0, CLAMP(value, 0, 1) * 2 - 1));
			}
			if (gfx_state.debug.fpsgraph.stallhist[index])
				color = ColorMagenta;
			gfxDrawLine2(width - i, height, GRAPHICSLIB_Z, width-i, height * (1-value), ColorDarkenPercent(color, 0.5), color);
			if (value_main < 0.5) {
				color = colorFromRGBA(lerpRGBAColors(0x0000FFe0, 0xFFFF00e0, value_main * 2));
			} else {
				color = colorFromRGBA(lerpRGBAColors(0xFFFF00e0, 0xFF0000e0, CLAMP(value_main, 0, 1) * 2 - 1));
			}
			if (gfx_state.debug.fpsgraph.stallhist[index])
				color = ColorMagenta;
			gfxDrawLine2(width - i, height, GRAPHICSLIB_Z+1, width-i, MAX(height * (1-value_main), 0), ColorDarkenPercent(color, 0.5), color);
		}
	}
	if (gfx_state.debug.fpsgraph_showHistogram) {
		int i;
		int width, height;
		float sum=0;
		int bFoundPercentile=0;
		gfxGetActiveSurfaceSizeInline(&width, &height);
		for (i=0; i<ARRAY_SIZE(gfx_state.debug.fpsgraph.mspfhistogram); i++) {
			float value = i/1000.f;
			Color color;
			if (value < 0.5) {
				color = colorFromRGBA(lerpRGBAColors(0x00FF00ff, 0xFFFF00ff, value * 2));
			} else {
				color = colorFromRGBA(lerpRGBAColors(0xFFFF00ff, 0xFF0000ff, CLAMP(value, 0, 1) * 2 - 1));
			}
			value = gfx_state.debug.fpsgraph.mspfhistogram[i] / (float)gfx_state.debug.fpsgraph.mspfhistogram_total;
			sum+=value;
			if (gfx_state.debug.fpsgraph.mspfhistogram[i]) {
				value = log(gfx_state.debug.fpsgraph.mspfhistogram[i]) / log((float)gfx_state.debug.fpsgraph.mspfhistogram_total);
			} else {
				value = 0;
			}
			gfxDrawLine2(i+bFoundPercentile, height, GRAPHICSLIB_Z, i+bFoundPercentile, height * (1-value), ColorDarkenPercent(color, 0.5), color);
			if (sum >= 0.98 && !bFoundPercentile) {
				char buf[64];
				int colors[] = {-1, -1, 0x7f7f7fff, 0x7f7f7fff};
				bFoundPercentile = 1;
				gfxDrawLine2(i+bFoundPercentile, height, GRAPHICSLIB_Z, i+bFoundPercentile, height * 0.5, ColorDarkenPercent(ColorBlue, 0.5), ColorBlue);
				sprintf(buf, "98th percentile: %d ms/f (%1.2f fps)", i, 1000.f/i);
				gfxfont_PrintEx(&g_font_Sans, i, height*0.7, GRAPHICSLIB_Z, 1.0, 1.0, 0, buf, (int)strlen(buf), colors, NULL);
			}
		}
	}

	if (gfx_state.debug.show_luminance_history && camera_view)
	{
		int i;
		int width, height;
		gfxGetActiveSurfaceSizeInline(&width, &height);
		for (i=0; i<ARRAY_SIZE(camera_view->avg_luminance_query.history); i++) {
			int index = (camera_view->avg_luminance_query.idx - i - 1 + ARRAY_SIZE(camera_view->avg_luminance_query.history)) % ARRAY_SIZE(camera_view->avg_luminance_query.history);
			float val = camera_view->avg_luminance_query.history[index] * 0.2f;
			Color color;
			color = colorFromRGBA(lerpRGBAColors(0xFFFF00ff, 0xFF0000ff, CLAMP(val, 0, 1)));
			gfxDrawLine2(width - i, height, GRAPHICSLIB_Z, width-i, height * (1-val), ColorDarkenPercent(color, 0.5), color);
		}
	}

#if !PLATFORM_CONSOLE
	if (gfx_state.debug.show_frame_delay) {
		static int current_index;
		static int colors[] = {
			0x000000ff,
			0x00007Fff,
			0x0000FFff,
			0x007F00ff,
			0x007F7Fff,
			0x007FFFff,
			0x00FF00ff,
			0x00FF7Fff,
			0x00FFFFff,
			0xFF0000ff,
			0xFF007Fff,
			0xFF00FFff,
			0xFF7F00ff,
			0xFF7F7Fff,
			0xFF7FFFff,
			0xFFFF00ff,
			0xFFFF7Fff,
			0xFFFFFFff,
		};
		display_sprite(white_tex_atlas, 0, 0, GRAPHICSLIB_Z + 1000, 8.f/white_tex_atlas->width, 8.f/white_tex_atlas->height, colors[current_index++]);
		current_index %= ARRAY_SIZE(colors);

		{
			HDC hdc;
			hdc = GetDC(NULL); // Get the DrawContext of the desktop
			if (hdc) {
				int i;
				COLORREF cr;
				POINT pCursor;
				Color c;
				int rgba;
				int drawn_index=-1;
				//GetCursorPos(&pCursor);
				pCursor.x = 4;
				pCursor.y = 4;
				ClientToScreen(rdrGetWindowHandle(gfx_state.currentDevice->rdr_device), &pCursor);
				cr = GetPixel(hdc, pCursor.x, pCursor.y);
				ReleaseDC(NULL, hdc);
				setColorFromABGR(&c, cr);
				c.a = 0xFF;
				rgba = RGBAFromColor(c);
				for (i=0; i<ARRAY_SIZE(colors); i++) {
					if(colors[i] == rgba)
						drawn_index = i;
				}
				if (drawn_index == -1) {
					gfxXYprintf(0, 2, "Could not detect frame delay (0x%08x).", rgba);
				} else {
					gfxXYprintf(0, 2, "%d frame delay", (current_index - drawn_index + ARRAY_SIZE(colors)) % ARRAY_SIZE(colors));
				}
			}
		}
	}
#endif

	if (gfx_state.debug.show_stages)
	{
		int i;
		for (i=0; i<eaSize(&gfx_state.debug.show_stages_text); i++)
		{
			bool b = !!gfx_state.debug.show_stages_value[i];
			gfxXYprintfColor(1, y++, b?0:255, b?255:0, 0, 255, "%s", gfx_state.debug.show_stages_text[i]);
		}
	}

	{
		static bool enabled_force_tint=false;
		if (rdr_state.show_alpha)
		{
			gfxXYprintfColor(1, y++, 255, 255, 255, 255, "show_alpha key:");
			gfxXYprintfColor(3, y++, 127, 255, 127, 255, "opaque (best)");
			gfxXYprintfColor(3, y++, 127, 255, 255, 255, "alpha cutout");
			gfxXYprintfColor(3, y++, 255, 255, 127, 255, "alpha to coverage");
			gfxXYprintfColor(3, y++, 255, 127, 255, 255, "soft alpha (worst)");
			if (!enabled_force_tint)
			{
				enabled_force_tint = true;
				ShaderTestN(2, "FORCE_TINT");
			}
		} else if (enabled_force_tint) {
			enabled_force_tint = false;
			ShaderTestN(2, "");
		}
	}

	PERFINFO_AUTO_STOP_START("WorldCellVisualize",1);

		if (gfx_state.debug.world_cell || gfx_state.debug.world_cell_3d)
		{
			if (region && region->root_world_cell)
			{
				if (gfx_state.debug.world_cell)
				{
					int pixwidth = 6;
					for (y = 0; y < (int)region->root_world_cell->vis_dist_level; ++y)
						pixwidth = 4 + 2 * pixwidth;
					gfxDebugDrawWorldCell(75, 75+pixwidth, region->root_world_cell, pixwidth, colorFromRGBA(0xffffffff));
				}
				if (gfx_state.debug.world_cell_3d)
				{
					gfxDebugDrawWorldCell3D(region->root_world_cell, colorFromRGBA(0xffffffff), 0);
				}
			}
		}

	PERFINFO_AUTO_STOP_START("DbgWatches",1);

		for (y = 0; y < eaSize(&dbg_state.active_debug_watches); ++y)
		{
			DbgWatch *watch = dbg_state.active_debug_watches[y];
			if (!watch->table)
			{
				gfxXYprintf(10, 20+y, "%s: %d", watch->watch_name, *((int *)watch->data));
				if (watch->needs_reset)
					*((int *)watch->data) = 0;
			}
		}

	PERFINFO_AUTO_STOP_START("gfxDebugCheckFrameGrab",1);

		gfxDebugCheckFrameGrab();

	PERFINFO_AUTO_STOP_START("misc",1);

	//////////////////////////////////////////////////////////////////////////
	// Right-side of the screen (above is left)

	y = 1 + gfxShouldShowRestoreButtons()*2;

	if (gfx_state.showfps && gfx_state.fps) {
		Color c;
		PERFINFO_AUTO_START("showfps",1);
		c = colorFromFPS(gfx_state.fps);
		if (nearSameF32(gfx_state.fps, device_state->per_device_fps) || eaSize(&gfx_state.devices)==1) {
			gfxXYZprintfColor(TEXT_JUSTIFY + 61,y++, GRAPHICSLIB_Z+100, c.r, c.g, c.b, c.a, "% 2.2f (%0.1fms/fm)", gfx_state.fps, 1000.f/gfx_state.fps );
		} else {
			// Per-device FPS
			gfxXYZprintf(TEXT_JUSTIFY + 57,y++,GRAPHICSLIB_Z+100,"App: % 2.2f (%0.1fms/fm)", gfx_state.fps, 1000.f/gfx_state.fps );
			gfxXYZprintf(TEXT_JUSTIFY + 57,y++,GRAPHICSLIB_Z+100,"Win: % 2.2f (%0.1fms/fm)", device_state->per_device_fps, 1000.f/device_state->per_device_fps );
		}
		{
			int pps = texWordGetPixelsPerSecond();
			if (pps) {
				gfxXYprintf(TEXT_JUSTIFY + 61,y++,"% 9d TexWordsPPS", pps);
			}
		}
		if (0) { // Moved to GCL
			int patching = fileLoaderPatchesPending();
			if (patching)
				gfxXYprintf(TEXT_JUSTIFY + 51,y++,"% 9d Files patching", patching);
		}
		PERFINFO_AUTO_STOP();
	}
	if (gfx_state.debug.runNVPerf) {
		y = gfxNVPerfDisplay(y);
	}
	if (gfx_state.showmem && gfx_state.mem_usage_actual) {
		U32 vid_mem_usage_tracked = gfxGetTotalTrackedVideoMemory();
		U32 vid_mem_usage_actual = gfxNVPerfGetGPUMemUsage() - gfxNVPerfGetGPUStartupMemUsage();
		gfxXYprintf(TEXT_JUSTIFY + 46,y++,"        Memory Actual: %s", friendlyBytes(gfx_state.mem_usage_actual));

		gfxXYprintf(TEXT_JUSTIFY + 46,y++,"Tracked (est + video): %s", friendlyBytes(gfx_state.mem_usage_tracked));
		gfxXYprintf(TEXT_JUSTIFY + 46,y++,"  Video Memory NV Est: %s", friendlyBytes(vid_mem_usage_actual*1024LL));
		gfxXYprintf(TEXT_JUSTIFY + 46,y++," Video Memory Tracked: %s", friendlyBytes(vid_mem_usage_tracked));
	}
	if (gfx_state.showTime) {
		gfxXYprintf(TEXT_JUSTIFY + 44,y++,"Time: %.2f (server), %.2f (client)", wlTimeGet(), gfx_state.cur_time);
	}
	if (wlTimeGetStepScale() != 1 && !in_editor) {
		gfxXYprintf(TEXT_JUSTIFY + 50,y++,"TIMESTEP SCALE: %f", wlTimeGetStepScale());
	}
	if (gfx_state.showCamPos && gfx_state.currentCameraView)
	{
		const char *region_name = NULL;
		const char *zmap_name = NULL;
		const char *gs_id_and_partition = NULL;
		Vec3 pyr;

		if(!gbNo3DGraphics)
		{
			region_name = worldRegionGetRegionName(region);
			zmap_name = zmapInfoGetPublicName(zmapGetInfo(worldRegionGetZoneMap(region)));
			gs_id_and_partition = GetGameServerIDAndPartitionString();
		}

		y++;
		gfxXYprintf(TEXT_JUSTIFY + 50,y++,"% 3.2f, % 3.2f, % 3.2f", cam_pos[0], cam_pos[1], cam_pos[2]);

		gfxGetActiveCameraYPR(pyr);
		gfxXYprintf(TEXT_JUSTIFY + 50,y++,"% 3.2f, % 3.2f, % 3.2f", DEG(pyr[0]), DEG(pyr[1]), DEG(pyr[2]));

		gfxXYprintf(TEXT_JUSTIFY + 50,y++,"Region: %s", region_name ? region_name : "<Default>");
		gfxXYprintf(TEXT_JUSTIFY + 50,y++,"ZoneMap: %s", zmap_name ? zmap_name : "<Unknown>");
		gfxXYprintf(TEXT_JUSTIFY + 50,y++, "GS/Ptn ID: %s", gs_id_and_partition ? gs_id_and_partition : "<Unknown>");
	}
	if (gfx_state.showTerrainPos && gfx_state.currentCameraView)
	{
		IVec2 grid_pos;
		F32 grid_size = 256.f * (1 << (gfx_state.showTerrainPos - 1));
		y++;
		grid_pos[0] = round(floor(cam_pos[0] / grid_size));
		grid_pos[1] = round(floor(cam_pos[2] / grid_size));
		gfxXYprintf(TEXT_JUSTIFY + 50,y++,"(%d, %d) % 3.2f, % 3.2f", 
			grid_pos[0], grid_pos[1], 
			cam_pos[0] - grid_pos[0] * grid_size, 
			cam_pos[2] - grid_pos[1] * grid_size);
	}
	if (!gfx_state.debug.disable_multimon_warning)
	{
		PERFINFO_AUTO_START("gfxCheckMonitor",1);
		y = gfxCheckMonitor(y);
		PERFINFO_AUTO_STOP();
	}

	if (gfx_state.debug.show_model_binning_message) {
		static float g_time_processing_lods = 0;
		if (geo2BinningModelLODs()) {
			g_time_processing_lods += gfx_state.frame_time;
			gfxXYprintf(TEXT_JUSTIFY+40,y++,"Processing model LODs (%.02f secs)...", g_time_processing_lods);
		} else {
			g_time_processing_lods = 0;
		}
	}

	if (!in_editor && bIsDevelopmentMode && gfx_state.debug.too_many_sprites_for_16bit_idx)
	{
		y++;
		gfxXYprintf(TEXT_JUSTIFY + 27,y++,"There are too many sprites on screen to use the most");
		gfxXYprintf(TEXT_JUSTIFY + 27,y++,"efficient rendering method. Please try to use less");
		gfxXYprintf(TEXT_JUSTIFY + 27,y++,"sprites for better performance.");
	}

	if (gfx_state.debug.queued_debug_text_count)
	{
		char* debug_text = gfx_state.debug.queued_debug_text.buff;
		int desirable_size = MAX(2 * gfx_state.debug.queued_debug_text.idx, 4*1024);

		y++;
		while (gfx_state.debug.queued_debug_text_count)
		{
			GfxDebugTextEntry * pEntry = (GfxDebugTextEntry *)debug_text;
			debug_text += sizeof(GfxDebugTextEntry);
			gfxXYprintfColor2(TEXT_JUSTIFY + 80 - pEntry->iLength,y++,pEntry->iColor,"%s",debug_text);
			debug_text += pEntry->iLength+1;
			gfx_state.debug.queued_debug_text_count--;
		}
		gfx_state.debug.queued_debug_text_count = 0;
		clearStuffBuff(&gfx_state.debug.queued_debug_text);
		// If more than 50% unused and more than 4K memory, resize it
		if (gfx_state.debug.queued_debug_text.size > desirable_size)
			resizeStuffBuff(&gfx_state.debug.queued_debug_text,desirable_size);
	}

	PERFINFO_AUTO_START("gfxDebugDrawDynDebug2D",1);
	gfxDebugDrawDynDebug2D();
	PERFINFO_AUTO_STOP();

	{
		bool careAboutPerfEffects = gfx_state.debug.runNVPerf ||
			gfx_state.debug.threadPerf ||
			gfx_state.debug.fpsgraph_show ||
			gfx_state.debug.fpsgraph_showHistogram ||
			gfx_state.debug.bShowTimingBars ||
			gfx_state.showfps > 1 ||
			gfx_state.debug.show_frame_counters ||
			gfx_state.debug.show_frame_times
			;
		F32 fLodScale = gfxGetLodScale();

		// Display a warning if the user appears to be investigating performance, and
		// /frameRateStabilizer is enabled, which might not be what they want
		if (rdr_state.frameRateStabilizer && careAboutPerfEffects)
		{
			gfxXYprintf(TEXT_JUSTIFY + 14,y++,"Warning: /frameRateStabilizer is enabled, performance affected.");
		}
		if (rdr_state.perFrameSleep && careAboutPerfEffects)
		{
			gfxXYprintf(TEXT_JUSTIFY + 14,y++,"Warning: /perFrameSleep is enabled, performance affected.");
		}
		if (gfx_state.settings.maxFps && careAboutPerfEffects)
		{
			gfxXYprintf(TEXT_JUSTIFY + 14,y++,"Warning: /maxFps is enabled, performance affected.");
		}
		// out for now.  Annoying and not all that useful
		/*if (bIsDevelopmentMode && gfx_state.settings.worldDetailLevel > 1.0f)
		{
			gfxXYprintf(TEXT_JUSTIFY + 14,y++,"Warning: /visScale > 1.0, performance affected.");
		}*/
		// also hoping this doesn't stay like this
		if (gfx_state.inEditor && fLodScale != 1.0f)
		{
			gfxXYprintf(TEXT_JUSTIFY + 40,y++,"editorVisScale: %.2f",fLodScale*fLodScale);
		}
	}

	if (bIsDevelopmentMode && gfx_state.debug.last_frame_counts.draw_list_stats.failed_draw_this_frame)
	{
		gfxXYprintf(TEXT_JUSTIFY + 14,y++,"Warning: Draw list is full, dropping some objects");
	}

	if (bIsDevelopmentMode && dynDebugState.bTooManyFastParticles)
	{
		gfxXYprintf(TEXT_JUSTIFY + 14,y++,"Warning: Too many allocated non-environment fast particles");
	}

	if (bIsDevelopmentMode && dynDebugState.bTooManyFastParticlesEnvironment)
	{
		gfxXYprintf(TEXT_JUSTIFY + 14,y++,"Warning: Too many allocated environment fast particles");
	}

	if (gfx_state.debug.zocclusion_enabledebug && gfx_state.currentCameraView && gfx_state.currentCameraView->occlusion_buffer)
		zoShowDebug(gfx_state.currentCameraView->occlusion_buffer, gfx_state.debug.zocclusion_enabledebug);

	if (gfx_state.debug.atlas_stats)
		atlasDisplayStats();

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_FUNC_PIX();

	if (enableFontTest)
		gfxDoFontTest();
}

// Shows the mouse coordinates on screen
AUTO_CMD_INT(gfx_state.showMouseCoords,mousecoords) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug, Graphics);

static void gfxDebugDrawDynNodeTree(const DynNode* pNode, bool bDrawAxes, bool bDrawNonCritical)
{
	DynNode* pLink = pNode->pChild;
	Vec3 vPos[2];
	U32 uiLineColor = 0xFFFF0000;
	U32 uiNonCriticalLineColor = 0xFF0000FF;
	if (bDrawAxes && (bDrawNonCritical || pNode->uiCriticalBone))
	{
		Mat4 mat;
		dynNodeGetWorldSpaceMat(pNode, mat, false);
		gfxDrawAxes3D(mat, 0.2f);
	}
	while (pLink)
	{
		bool bDrawBone = true;
		if (dynDebugState.bPrintBoneUnderMouse && dynDebugState.pBoneUnderMouse)
		{
			if (dynDebugState.pBoneUnderMouse == pLink)
				bDrawBone = false;
			if (pLink->pParent && pLink->pParent == dynDebugState.pBoneUnderMouse)
				bDrawBone = false;
		}

		if (bDrawBone)
		{
			if (pLink->uiCriticalBone)
			{
				dynNodeGetWorldSpacePos(pNode, vPos[0]);
				dynNodeGetWorldSpacePos(pLink, vPos[1]);
				gfxDrawLine3DWidthARGB(vPos[0], vPos[1], uiLineColor, uiLineColor, 2.0f);
			}
			else if (bDrawNonCritical)
			{
				dynNodeGetWorldSpacePos(pNode, vPos[0]);
				dynNodeGetWorldSpacePos(pLink, vPos[1]);
				gfxDrawLine3DWidthARGB(vPos[0], vPos[1], uiNonCriticalLineColor, uiNonCriticalLineColor, 2.0f);
			}
		}
		gfxDebugDrawDynNodeTree(pLink, bDrawAxes, bDrawNonCritical);
		pLink = pLink->pSibling;
	}
}

void dynFxDrawDebugData(DynFx* pFx, void* pUserData)
{
	U32 uiFlags = *((U32*)(pUserData));
	DynParticle* pParticle = dynFxGetParticle(pFx);

	if (!pParticle)
		return;

	if (uiFlags & 0x1) // Draw transform
	{
		Mat4 mat;
		dynNodeGetWorldSpaceMat(&pParticle->pDraw->node, mat, true);
		gfxDrawAxes3D(mat, 1.0f);
	}
	if (uiFlags & 0x2) // Draw visibility spheres
	{
		Vec3 vMid;
		dynNodeGetWorldSpacePos(&pParticle->pDraw->node, vMid);
		gfxDrawSphere3DARGB(vMid, pParticle->fVisibilityRadius, 12, 0xFFFFFF00, 0.1f);
	}
	dynFxForEachChild(pFx, dynFxDrawDebugData, pUserData);
}

static void drawTriggerImpact(Vec3 vPos, Vec3 vDir, U32 uiARGB)
{
	Mat4 mat;
	Vec3 vForward;
	copyVec3(upvec, vForward);
	if (fabsf(dotVec3(vForward, vDir)) > 0.99f)
	{
		copyVec3(forwardvec, vForward);
	}
	orientMat3ToNormalAndForward(mat, vDir, vForward);
	copyVec3(vPos, mat[3]);
	gfxDrawCone3D(mat, 1.0f, RAD(20.0f), 12, ARGBToColor(uiARGB));
}

void gfxDebugDrawDynDebug3D(void)
{
	const DynSkeleton* pDebugSkeleton;
	if (dynDebugState.pDebugSkeleton)
		pDebugSkeleton = dynDebugState.pDebugSkeleton;
	else
	{
		const DynDrawSkeleton* pDebugDrawSkeleton = dynDebugGetDebugSkeleton();
		pDebugSkeleton = pDebugDrawSkeleton?pDebugDrawSkeleton->pSkeleton:NULL;
	}
	if (pDebugSkeleton)
	{
		if( dynDebugState.bDrawSkeleton && pDebugSkeleton->pRoot)
		{
			gfxSetPrimZTest(0);
			gfxDebugDrawDynNodeTree(pDebugSkeleton->pRoot, dynDebugState.bDrawSkeletonAxes, dynDebugState.bDrawSkeletonNonCritical);
			{
				DynNode* pRootiest = pDebugSkeleton->pRoot;
				Mat4 mRootiest;

				while (pRootiest->pParent)
					pRootiest = pRootiest->pParent;
				dynNodeGetWorldSpaceMat(pRootiest, mRootiest, false);
				gfxDrawAxes3D(mRootiest, 1.0f);
			}
			gfxSetPrimZTest(1);
		}
		if (dynDebugState.bDrawCollisionExtents)
		{
			WLCostume* pCostume = GET_REF(pDebugSkeleton->hCostume);
			if (pCostume)
			{
				Vec3 vCollMin, vCollMax;
				dynSkeletonGetCollisionExtents(pDebugSkeleton, vCollMin, vCollMax);
				gfxDrawBox3DARGB(vCollMin, vCollMax, unitmat, 0x50FF0000, 0.0f);
			}
		}
		if (dynDebugState.bDrawVisibilityExtents)
		{
			{
				//blue = data from update transform
				Mat4 mRoot;
				Vec3 vMin, vMax;
				dynNodeGetWorldSpaceMat(pDebugSkeleton->pRoot, mRoot, false);
				dynSkeletonGetVisibilityExtents(pDebugSkeleton, vMin, vMax, true);
				gfxDrawBox3DARGB(vMin, vMax, mRoot, 0x500000FF, 0.0f);
				gfxDrawSphere3DARGB(mRoot[3], pDebugSkeleton->fStaticVisibilityRadius, 12, 0x5000FF00, 0.1f);
			}

			{
				//red = data on extents node
				Mat4 mExtents;
				Vec3 vExtentsMin, vExtentsMax;
				dynNodeGetWorldSpaceMat(pDebugSkeleton->pExtentsNode, mExtents, true);
				vExtentsMax[0] = vExtentsMax[1] = vExtentsMax[2] = 0.5;
				vExtentsMin[0] = vExtentsMin[1] = vExtentsMin[2] = -0.5;
				gfxDrawBox3DARGB(vExtentsMin, vExtentsMax, mExtents, 0x50FF0000, 0.0f);
			}
		}
		if (dynDebugState.bDrawRagdollDataGfx || dynDebugState.bDrawRagdollDataAnim)
		{
			WLCostume* pCostume = GET_REF(pDebugSkeleton->hCostume);
			if (pCostume)
			{
				SkelInfo* pSkelInfo = GET_REF(pCostume->hSkelInfo);
				if (pSkelInfo)
				{
					DynRagdollData* pData = NULL;
					if (dynDebugState.bDrawRagdollDataAnim) {
						pData = GET_REF(pSkelInfo->hRagdollDataHD);
					}
					if (!pData) {
						pData = GET_REF(pSkelInfo->hRagdollData);
					}
					if (pData)
					{
						FOR_EACH_IN_EARRAY(pData->eaShapes, DynRagdollShape, pShape)
							const DynNode* pBone = dynSkeletonFindNode(pDebugSkeleton, pShape->pcBone);
							int iRagdollColor = pDebugSkeleton->ragdollState.bRagdollOn ?
													0x40AF00AF :
													pShape->bTorsoBone ? 0x8000AF00 : 0x80AFAF00;
							if (pBone)
							{
								DynTransform shapeTransform;
								Quat qTemp;
								Vec3 vRotatedOffset;
								dynNodeGetWorldSpaceTransform(pBone, &shapeTransform);
								quatMultiplyInline(pShape->qRotation, shapeTransform.qRot, qTemp);
								copyQuat(qTemp, shapeTransform.qRot);


								switch (pShape->eShape)
								{
									xcase eRagdollShape_Box:
									{
										Mat4 mShapeMat;
										Vec3 vScaledOffset;
										mulVecVec3(pShape->vOffset, shapeTransform.vScale, vScaledOffset);
										quatRotateVec3Inline(shapeTransform.qRot, vScaledOffset, vRotatedOffset);
										addVec3(shapeTransform.vPos, vRotatedOffset, shapeTransform.vPos);

										dynTransformToMat4(&shapeTransform, mShapeMat);
										gfxDrawBox3DARGB(pShape->vMin, pShape->vMax, mShapeMat, iRagdollColor, 0.0f);
									}
									xcase eRagdollShape_Capsule:
									{
										Vec3 vPoint1, vPoint2, vTemp;
										Vec3 vScale;
										F32 fLengthScale = 1.0f;
										F32 fWidthScale = 1.0f;

										quatRotateVec3Inline(shapeTransform.qRot, pShape->vOffset, vRotatedOffset);
										addVec3(shapeTransform.vPos, vRotatedOffset, shapeTransform.vPos);
										copyVec3(shapeTransform.vScale, vScale);

										fLengthScale = fabsf(vScale[1]);
										fWidthScale = MAX(fabsf(vScale[0]), fabsf(vScale[2]));

										unitVec3(shapeTransform.vScale);

										vTemp[0] = vTemp[2] = 0.0f;
										vTemp[1] = pShape->fHeightMax * fLengthScale;
										dynTransformApplyToVec3(&shapeTransform, vTemp, vPoint1);
										vTemp[0] = vTemp[2] = 0.0f;
										vTemp[1] = pShape->fHeightMin * fLengthScale;
										dynTransformApplyToVec3(&shapeTransform, vTemp, vPoint2);

										gfxDrawCapsule3D(vPoint1, vPoint2, pShape->fRadius * fWidthScale, 12, ARGBToColor(iRagdollColor), 0.0f);
									}
								}
							}
						FOR_EACH_END;

						if (pDebugSkeleton->ragdollState.bRagdollOn)
						{
							U32 uiPart;
							for (uiPart = 0; uiPart < pDebugSkeleton->ragdollState.uiNumParts; ++uiPart)
							{
								DynRagdollPartState* pPart = &pDebugSkeleton->ragdollState.aParts[uiPart];
								const DynNode *pNode = dynSkeletonFindNode(pDebugSkeleton, pPart->pcBoneName);
								Vec3 xPosWS, yPosWS, zPosWS;
								Mat3 mPart;
								F32 fAxisScale = 1.0f;
								
								if (pNode) {
									DynTransform xNode;
									dynNodeGetWorldSpaceTransform(pNode, &xNode);
									quatToMat(xNode.qRot, mPart);
									fAxisScale = sqrtf(dotVec3(xNode.vScale, xNode.vScale));

									scaleAddVec3(mPart[0], 0.3f*fAxisScale, pPart->vWorldSpace, xPosWS);
									scaleAddVec3(mPart[1], 0.3f*fAxisScale, pPart->vWorldSpace, yPosWS);
									scaleAddVec3(mPart[2], 0.3f*fAxisScale, pPart->vWorldSpace, zPosWS);

									gfxDrawLine3D(pPart->vWorldSpace, xPosWS, ARGBToColor(0xFFFF0000));
									gfxDrawLine3D(pPart->vWorldSpace, yPosWS, ARGBToColor(0xFFFF0000));
									gfxDrawLine3D(pPart->vWorldSpace, zPosWS, ARGBToColor(0xFFFF0000));

									scaleAddVec3(mPart[0], 0.3f*fAxisScale, xNode.vPos, xPosWS);
									scaleAddVec3(mPart[1], 0.3f*fAxisScale, xNode.vPos, yPosWS);
									scaleAddVec3(mPart[2], 0.3f*fAxisScale, xNode.vPos, zPosWS);

									gfxDrawLine3D(xNode.vPos, xPosWS, ARGBToColor(0xFF880000));
									gfxDrawLine3D(xNode.vPos, yPosWS, ARGBToColor(0xFF880000));
									gfxDrawLine3D(xNode.vPos, zPosWS, ARGBToColor(0xFF880000));
								}

								quatToMat(pPart->qWorldSpace, mPart);
								scaleAddVec3(mPart[0], 0.35f*fAxisScale, pPart->vWorldSpace, xPosWS);
								scaleAddVec3(mPart[1], 0.35f*fAxisScale, pPart->vWorldSpace, yPosWS);
								scaleAddVec3(mPart[2], 0.35f*fAxisScale, pPart->vWorldSpace, zPosWS);

								gfxDrawLine3D(pPart->vWorldSpace, xPosWS, ARGBToColor(0xAA0000FF));
								gfxDrawLine3D(pPart->vWorldSpace, yPosWS, ARGBToColor(0xAA0000FF));
								gfxDrawLine3D(pPart->vWorldSpace, zPosWS, ARGBToColor(0xAA0000FF));
							}
						}
					}
				}
			}
		}
	}
	if (dynDebugState.bPrintBoneUnderMouse &&
		dynDebugState.pBoneUnderMouse &&
		dynSkeletonIsNodeAttached(dynDebugState.pDebugSkeleton, dynDebugState.pBoneUnderMouse))
	{
		const DynNode* pParent = dynDebugState.pBoneUnderMouse->pParent;
		const DynNode* pChild = dynDebugState.pBoneUnderMouse->pChild;
		Vec3 vPos[2];
		U32 uiParentColor =		0xFFFF00FF;
		U32 uiNodeColor =		0xFF00FF00;
		U32 uiChildrenColor =	0xFF00FFFF;
		gfxSetPrimZTest(0);
		dynNodeGetWorldSpacePos(dynDebugState.pBoneUnderMouse, vPos[0]);
		gfxDrawSphere3DARGB(vPos[0], 0.1f + 0.03f * sinf((F32)gfx_state.frame_count * 0.2f), 16, uiNodeColor, 0.0f);
		if (pParent)
		{
			dynNodeGetWorldSpacePos(pParent, vPos[1]);
			gfxDrawLine3DWidthARGB(vPos[0], vPos[1], uiNodeColor, uiParentColor, 2.0f);
		}
		while (pChild)
		{
			dynNodeGetWorldSpacePos(pChild, vPos[1]);
			gfxDrawLine3DWidthARGB(vPos[0], vPos[1], uiNodeColor, uiChildrenColor, 2.0f);
			pChild = pChild->pSibling;
		}
		gfxSetPrimZTest(1);
	}

	if (dynDebugState.cloth.bDrawCollision)
	{

		int j = 0;
		DynClothObject ***peaClothObjects = dynClothGetAllClothObjects();

		for(j = 0; j < eaSize(peaClothObjects); j++) {

			if(!(*peaClothObjects)[j]->pGeo || dynDebugState.cloth.pDebugClothObject == (*peaClothObjects)[j]) {

				int i;
				DynClothObject *pClothObject = (*peaClothObjects)[j];
				Vec4 color;

				color[3] = 0.4f;

				for (i=0; i<pClothObject->NumCollidables; i++) {

					Mat4 mat;
					DynClothMesh* pColMesh = dynClothGetCollidableMesh(pClothObject, i, mat);

					if(dynClothGetCollidableInsideVolume(pClothObject, i)) {
						color[0] = 0.0f;
						color[1] = 0.6f;
						color[2] = 0.6f;
					} else {
						color[0] = 0.3f;
						color[1] = 0.0f;
						color[2] = 0.8f;
					}

					if (pColMesh)
					{
						gfxDrawClothMeshPrimitive(mat, color, pColMesh);
					}
				}
			}
		}

	}

	if (dynDebugState.bDrawWindGrid)
	{
		Vec3 vCenterPos = { 0.0f, 0.0f, 0.0f};
		Vec3 vOffsetX = { 1.0f, 0.0f, 0.0f };
		Vec3 vOffsetZ = { 0.0f, 0.0f, 1.0f };
		F32 fGridSize = dynWindGetSampleGridDivSize();
		int iSide = (int)(dynWindGetSampleGridExtents() *2.0f / fGridSize);
		int iHalfSide = iSide / 2;
		int x, z;

		copyVec3(gfx_state.currentCameraView->frustum.cammat[3], vCenterPos);
		if(dynDebugState.fDrawWindGridForceYLevel) {
			vCenterPos[1] = dynDebugState.fDrawWindGridForceYLevel;
		} else {
			vCenterPos[1] -= 8.0f;
		}

		vCenterPos[0] = floor(vCenterPos[0] / fGridSize) * fGridSize;
		vCenterPos[2] = floor(vCenterPos[2] / fGridSize) * fGridSize;

		for (x=-iHalfSide; x<=iHalfSide; ++x)
		{
			for (z=-iHalfSide; z<=iHalfSide; ++z)
			{
				//normal
				{
					Vec3 vPoint;
					Vec3 vWind;
					F32 fMag;
					scaleAddVec3(vOffsetX, x*fGridSize, vCenterPos, vPoint);
					scaleAddVec3(vOffsetZ, z*fGridSize, vPoint, vPoint);
					fMag = dynWindGetAtPosition(vPoint, vWind, false);
					scaleAddVec3(vWind, fMag, vPoint, vWind);
					if (fMag)
						gfxDrawLine3DWidthARGB(vPoint, vWind, 0xFFFF0000, 0xFF0000FF, 1.0f);
				}
				//small objects
				{
					Vec3 vPoint;
					Vec3 vWind;
					F32 fMag;
					scaleAddVec3(vOffsetX, x*fGridSize, vCenterPos, vPoint);
					scaleAddVec3(vOffsetZ, z*fGridSize, vPoint, vPoint);
					fMag = dynWindGetAtPosition(vPoint, vWind, true);
					scaleAddVec3(vWind, fMag, vPoint, vWind);
					if (fMag)
						gfxDrawLine3DWidthARGB(vPoint, vWind, 0xFF00FF00, 0xFFFFFF00, 1.0f);
				}
			}
		}
	}

	if (dynDebugState.bDrawSkyVolumes)
		gfxSkyVolumeDebugDraw();

	if (dynDebugState.bDrawBodysockAtlas)
		gfxImposterAtlasDrawBodysockAtlas();

	if (dynDebugState.bDrawFxTransforms || dynDebugState.bDrawFxVisibility)
	{
		U32 uiFlags = 0;
		if (dynDebugState.bDrawFxTransforms)
			uiFlags |= 0x1;
		if (dynDebugState.bDrawFxVisibility)
			uiFlags |= 0x2;
		dynFxForEachFx(dynFxDrawDebugData, &uiFlags);
	}

	if (dynDebugState.bDrawImpactTriggers)
	{
		dynFxTriggerImpactDrawEach(drawTriggerImpact);
	}

	if (dynDebugState.bDrawMeshTrailDebugInfo)
	{
		FOR_EACH_IN_EARRAY(dynDebugState.eaMeshTrailDebugInfo, DynMeshTrailDebugInfo, pDebugInfo)
		{
			if (GET_REF(pDebugInfo->hFx))
			{
				Mat4 mTrail;
				int i;
				orientMat3ToNormalAndForward(mTrail, pDebugInfo->vDir, upvec);
				copyVec3(pDebugInfo->vPos, mTrail[3]);
				gfxDrawCone3D(mTrail, 1.0f, RAD(20.0f), 12, ColorMagenta);
				assert(eafSize(&pDebugInfo->eafPoints) % 12 == 0);
				for (i=0; i<eafSize(&pDebugInfo->eafPoints); i+= 12)
				{
					Vec3 vControlPoints[4];
					memcpy(vControlPoints, &pDebugInfo->eafPoints[i], sizeof(F32) * 12);
					if (i % 24)
						gfxDrawBezier3D(vControlPoints, ColorRed, ColorRed, 0.0f);
					else
						gfxDrawBezier3D(vControlPoints, ColorGreen, ColorGreen, 0.0f);
					gfxDrawSphere3D(vControlPoints[0], 0.05f, 12, ColorCyan, 0);
					gfxDrawSphere3D(vControlPoints[1], 0.05f, 12, ColorYellow, 0);
					gfxDrawSphere3D(vControlPoints[2], 0.05f, 12, ColorMagenta, 0);
					gfxDrawSphere3D(vControlPoints[3], 0.05f, 12, ColorBlack, 0);
				}
			}
			else
			{
				dynMeshTrailDebugInfoFree(pDebugInfo);
				eaRemoveFast(&dynDebugState.eaMeshTrailDebugInfo, ipDebugInfoIndex);
			}
		}
		FOR_EACH_END;
	}
}

#define DYNDEBUGWRITEWITHXY(a, x, y) gfxfont_PrintEx(&g_font_Sans, 30 + (x), iFirstLine + iPixPerLine*(y), 0, 1.1, 1.1, 0, a, (int)strlen(a), colors, NULL)
#define DYNDEBUGWRITEWITHX(a, x) DYNDEBUGWRITEWITHXY(a, x, iCurrentLine)
#define DYNDEBUGWRITELINE(a) DYNDEBUGWRITEWITHXY(a, 0, (iCurrentLine++))
#define DYNDEBUGGETSTRINGWIDTH(a) gfxfont_StringWidth(&g_font_Sans,1.1, 1.1, a)

int dynDebugBitSort(const DynDebugBit** a, const DynDebugBit** b)
{
	F32 fResult = (*b)->fTimeSinceSet - (*a)->fTimeSinceSet;
	if (fResult == 0.0f)
		return stricmp((*b)->pcBitName, (*a)->pcBitName); 
	return SIGN(fResult);
}


// Returns a list of fx that match the given substring
AUTO_COMMAND ACMD_CATEGORY(dynFx) ACMD_NAME("dfxSearch", "dfxs");
void dfxSearch(  ACMD_SENTENCE substring )
{
	const char** eaFXNames = NULL;
	StashTableIterator iter;
	StashElement elem;
	dynFxInfoGetFlagsIterator(&iter);
	while (stashGetNextElement(&iter, &elem))
	{
		const char* pcFxName = stashElementGetStringKey(elem);
		bool bFound = true;

		//meaningless call to reset strTok
		char* pcSubStr = strTokWithSpacesAndPunctuation(NULL, NULL);

		while (pcSubStr = strTokWithSpacesAndPunctuation(substring, " "))
		{
			if (!strstri(pcFxName, pcSubStr))
			{
				bFound = false;
				break;
			}
		}
		if (bFound)
			eaPush(&eaFXNames, pcFxName);
	}
	conPrintf("\n\n------------------------");
	conPrintf("%d FX name matches found\n", eaSize(&eaFXNames));
	conPrintf("------------------------\n\n");
	eaQSortG(eaFXNames, strCmp);
	FOR_EACH_IN_EARRAY_FORWARDS(eaFXNames, const char, pcFxName)
		conPrintf("%s\n", pcFxName);
	FOR_EACH_END;
	conPrintf("\n");
	eaDestroy(&eaFXNames);
}

#define DYNDEBUGWRITEWITHXANDRECORDMAX(a, b) DYNDEBUGWRITEWITHX(a, b); iCurrentLine++; MAX1(iMaxX, DYNDEBUGGETSTRINGWIDTH(a));

extern U32 uiMaxDebris;

#define DYNDEBUG_TABLE_SPACE "  |  "
#define DYNDEBUG_RGBA_TITLE 0xAAAAAAFF
#define DYNDEBUG_RGBA_VALUE 0xFFFFFFFF
#define DYNDEBUG_RGBA_VALUEBLEND 0xFFFFFF88
#define DYNDEBUG_RGBA_VALUENEWA DYNDEBUG_RGBA_VALUE
#define DYNDEBUG_RGBA_VALUENEWB 0x44FF44FF
#define DYNDEBUG_RGBA_VALUEOLDA 0XFF444400
#define DYNDEBUG_RGBA_VALUEOLDB 0xFF4444FF
#define DYNDEBUG_RGBA_REASON 0x8888FFFF
#define DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING(t, v, c) sprintf(pcBitBuffer, t, v), setVec4same(colors, c), DYNDEBUGWRITEWITHX(pcBitBuffer, indent), indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer)
#define DYNDEBUG_PRETTYPRINT_TEXT(t, c)               sprintf(pcBitBuffer, t),    setVec4same(colors, c), DYNDEBUGWRITEWITHX(pcBitBuffer, indent), indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer)

static int gfxDebugDrawDynDebug2DNewAnimBitsRecursive(const DynSkeleton* pDebugSkeleton, int iFirstLine, int iPixPerLine, int iCurrentLine, int *colors, int numIndentChars)
{
	//statement blocked to prevent memory problems on recursion
	{
		DynSkeletonDebugNewAnimSys *pDebugSkeletonLog = pDebugSkeleton->pDebugSkeleton;
		char pcBitBuffer[1024], pcIndentBuffer[32];
		int curIndentChar;
		F32 indent;

		//setup the indent buffer for tabbing over sub-skeletons
		for (curIndentChar = 0; curIndentChar < numIndentChars; curIndentChar++)
			pcIndentBuffer[curIndentChar] = ' ';
		pcIndentBuffer[curIndentChar] = '\0';

		//Output the skeleton's name data line
		{
			indent = 0.01f;
			DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%sEntity: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
			DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s",
				(pDebugSkeleton->pRoot->pParent && pDebugSkeleton->pRoot->pParent->pcTag) ? pDebugSkeleton->pRoot->pParent->pcTag : "Unknown",
				DYNDEBUG_RGBA_VALUE);
			iCurrentLine++;
		}

		if (dynDebugState.audioShowAnimBits ||
			(!dynDebugState.danimShowBitsHideMainSkeleton && numIndentChars == 0) ||
			( dynDebugState.danimShowBitsShowSubSkeleton  && numIndentChars >  0))
		{

			if (!dynDebugState.danimShowBitsHideHead && !dynDebugState.audioShowAnimBits)
			{
				//output the critical node list override (if active)
				{
					const WLCostume *pCostume = GET_REF(pDebugSkeleton->hCostume);
					const SkelInfo *pSkelInfo = pCostume ? GET_REF(pCostume->hSkelInfo) : NULL;
					const char *pcCritNodeList = pSkelInfo ? REF_HANDLE_GET_STRING(pSkelInfo->hCritNodeList) : NULL;
					if (pcCritNodeList) {
						indent = 0.f;
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%sCritical Node List: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pcCritNodeList, DYNDEBUG_RGBA_VALUE);
						iCurrentLine++;
					}
				}

				//output the flags (if active)
				{
					indent = 0.0f;
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%sFlags: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
					if (pDebugSkeleton->bForceVisible) {
						DYNDEBUG_PRETTYPRINT_TEXT("Force Visible ", DYNDEBUG_RGBA_VALUE);
					}
					if (pDebugSkeleton->bWasForceVisible) {
						DYNDEBUG_PRETTYPRINT_TEXT("Was Force Visible ", DYNDEBUG_RGBA_VALUE);
					}
					iCurrentLine++;
				}

				//output the scale data
				{
					indent = 0.0f;
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%sHipsHeightAdjustmentFactor: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", pDebugSkeleton->scaleCollection.fHipsHeightAdjustmentFactor, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / HeightScale: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", pDebugSkeleton->fHeightScale, DYNDEBUG_RGBA_VALUE);
					iCurrentLine++;
				}

				//output the location & orientation data line
				{
					indent = 0.0f;
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%sSpeed XZ: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.1f", pDebugSkeleton->fCurrentSpeedXZ, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Speed Y: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.1f", pDebugSkeleton->fCurrentSpeedY, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Angle: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.1f", DEG(pDebugSkeleton->fMovementAngle), DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Bank: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.1f", DEG(pDebugSkeleton->fMovementBank), DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Bank Blend: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.1f", pDebugSkeleton->fMovementBankBlendFactor, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Terrain Tilt: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.1f", DEG(pDebugSkeleton->fTerrainPitch), DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Terrain Tilt Blend: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.1f", pDebugSkeleton->fTerrainTiltBlend, DYNDEBUG_RGBA_VALUE);
					iCurrentLine++;

					indent = 0.0f;
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%sRun'n'Gun Bone: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pDebugSkeleton->eaRunAndGunBones[0]->pcRGBoneName, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Parent: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pDebugSkeleton->eaRunAndGunBones[0]->pcRGParentBoneName, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Blend: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%f", pDebugSkeleton->fTorsoPointingBlendFactor, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Multi-Joint Blend: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%f", pDebugSkeleton->fRunAndGunMultiJointBlend, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / MovementYawFS: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%f", pDebugSkeleton->fMovementYawFS, DYNDEBUG_RGBA_VALUE);
					iCurrentLine++;
				}

				//output the blends data line
				{
					indent = 0.0f;
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%sOverride All Bones: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", pDebugSkeleton->fOverrideAllBlendFactor, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / LowerBody Blend: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", pDebugSkeleton->fLowerBodyBlendFactor, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Movement Override: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", pDebugSkeleton->fMovementSystemOverrideFactor, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / RegisterWeapon: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", pDebugSkeleton->fWepRegisterBlend, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Ragdoll: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", pDebugSkeleton->ragdollState.fBlend, DYNDEBUG_RGBA_VALUE);
					iCurrentLine++;

					if (eaSize(&pDebugSkeleton->eaGroundRegLimbs) > 0)
					{
						indent = 0.0f;
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s[GroundReg]", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
						FOR_EACH_IN_EARRAY_FORWARDS(pDebugSkeleton->eaGroundRegLimbs, DynGroundRegLimb, pLimb)
						{
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING(" / %s: ", dynSkeletonGetGroundRegLimbName(pLimb), DYNDEBUG_RGBA_TITLE);
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", dynSkeletonGetGroundRegLimbRelWeight(pLimb), DYNDEBUG_RGBA_VALUE);
						}
						FOR_EACH_END;
						iCurrentLine++;

						indent = 0.0f;
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s[GroundOff]", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
						FOR_EACH_IN_EARRAY_FORWARDS(pDebugSkeleton->eaGroundRegLimbs, DynGroundRegLimb, pLimb)
						{
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING(" / %s: ", dynSkeletonGetGroundRegLimbName(pLimb), DYNDEBUG_RGBA_TITLE);
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", dynSkeletonGetGroundRegLimbOffset(pLimb), DYNDEBUG_RGBA_VALUE);
						}
						FOR_EACH_END;
						iCurrentLine++;
					}

					indent = 0.0f;
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s[HeightBump]: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", pDebugSkeleton->fHeightBump, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Blend: ", DYNDEBUG_RGBA_TITLE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%f (all bones)", pDebugSkeleton->fGroundRegBlendFactor, DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%f (upper body)", pDebugSkeleton->fGroundRegBlendFactorUpperBody, DYNDEBUG_RGBA_VALUE);
					iCurrentLine++;
				}

				//output the enable-bits
				{
					indent = 0.0f;
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%sOverride All Bit: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
					if (pDebugSkeleton->bOverrideAll) DYNDEBUG_PRETTYPRINT_TEXT("ON",  DYNDEBUG_RGBA_VALUE);
					else                              DYNDEBUG_PRETTYPRINT_TEXT("OFF", DYNDEBUG_RGBA_VALUE);
					DYNDEBUG_PRETTYPRINT_TEXT(" / Override Movement Bit: ", DYNDEBUG_RGBA_TITLE);
					if (pDebugSkeleton->bOverrideMovement) DYNDEBUG_PRETTYPRINT_TEXT("ON",  DYNDEBUG_RGBA_VALUE);
					else                                   DYNDEBUG_PRETTYPRINT_TEXT("OFF", DYNDEBUG_RGBA_VALUE);
					iCurrentLine++;
				}

				//output the stance data line
				{
					U32 printColor = 0;
					indent = 0.0f;
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%sStances: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
					FOR_EACH_IN_EARRAY_FORWARDS(pDebugSkeletonLog->eaDebugStances, DynSkeletonDebugStance, testStance)
					{
						if (testStance->iState == DDNAS_STANCE_STATE_CURRENT)
						{
							printColor = DYNDEBUG_RGBA_VALUE;
						}
						else if (testStance->iState == DDNAS_STANCE_STATE_NEW)
						{
							F32 blend = (DDNAS_STANCE_MAXTIME_NEW - testStance->fTimeInState) / (F32)DDNAS_STANCE_MAXTIME_NEW;
							printColor = lerpRGBAColors(DYNDEBUG_RGBA_VALUENEWA, DYNDEBUG_RGBA_VALUENEWB, blend);
						}
						else if (testStance->iState == DDNAS_STANCE_STATE_OLD)
						{
							F32 blend = (DDNAS_STANCE_MAXTIME_OLD - testStance->fTimeInState) / (F32)DDNAS_STANCE_MAXTIME_OLD;
							printColor = lerpRGBAColors(DYNDEBUG_RGBA_VALUEOLDA, DYNDEBUG_RGBA_VALUEOLDB, blend);
						}

						if (itestStanceIndex != 0) DYNDEBUG_PRETTYPRINT_TEXT(", ", printColor);
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", testStance->pcStanceName, printColor);
						if (testStance->iState == DDNAS_STANCE_STATE_NEW ||
							testStance->iState == DDNAS_STANCE_STATE_CURRENT)
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING(" %.1f", testStance->fTimeActive, printColor);
					}
					FOR_EACH_END;
					iCurrentLine++;
				}
			}

			FOR_EACH_IN_EARRAY_FORWARDS(pDebugSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater)
			{
				if (eaSize(&pDebugSkeletonLog->eaGraphUpdaters) > ipUpdaterIndex &&
					((!dynDebugState.danimShowBitsHideMainSequencer || dynDebugState.audioShowAnimBits)  && ipUpdaterIndex == 0) ||
					(gConf.bUseMovementGraphs && (!dynDebugState.danimShowBitsHideMovement || dynDebugState.audioShowAnimBits) && ipUpdaterIndex == 1) ||
					(!dynDebugState.audioShowAnimBits && dynDebugState.danimShowBitsShowSubSequencer     && ipUpdaterIndex > (gConf.bUseMovementGraphs?1:0) && !dynAnimGraphUpdaterIsOverlay(pUpdater)) ||
					(!dynDebugState.audioShowAnimBits && dynDebugState.danimShowBitsShowOverlaySequencer && ipUpdaterIndex > (gConf.bUseMovementGraphs?1:0) &&  dynAnimGraphUpdaterIsOverlay(pUpdater)))
				{
					F32 weight=0;
					int i, j, tabTable[14];
					int iStartLine = iCurrentLine;
					const DynAnimChartStack* pChartStack = dynAnimGraphUpdaterGetChartStack(pUpdater);
					S32	firstLine = 1;
					pcBitBuffer[0] = 0;
					indent = 0.0f;
					if (pChartStack)
					{
						S32 iChartStackSize = dynAnimGraphUpdaterGetChartStackSize(pUpdater);
						S32 iChartStackLoop;
						const char* lastChartName = NULL;

						for (iChartStackLoop = 0; iChartStackLoop < iChartStackSize; iChartStackLoop++)
						{
							const DynAnimChartRunTime *pChart = dynAnimGraphUpdaterGetChartStackChart(pUpdater, iChartStackLoop);
							char stanceWords[1024];
							dynAnimChartGetStanceWords(pChart, SAFESTR(stanceWords));

							if(!pcBitBuffer[0]){
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s  Updater Chart Stack", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
								if (TRUE_THEN_RESET(firstLine)) DYNDEBUG_PRETTYPRINT_TEXT(": ", DYNDEBUG_RGBA_TITLE);
								else                            DYNDEBUG_PRETTYPRINT_TEXT(" (cont): ", DYNDEBUG_RGBA_TITLE);
							}

							if(	!lastChartName ||
								stricmp(lastChartName, dynAnimChartGetName(pChart)))
							{
								if (lastChartName) DYNDEBUG_PRETTYPRINT_TEXT("], ", DYNDEBUG_RGBA_VALUE);
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s[", dynAnimChartGetName(pChart), DYNDEBUG_RGBA_VALUE);
							}
							else
							{
								DYNDEBUG_PRETTYPRINT_TEXT(", ", DYNDEBUG_RGBA_VALUE);
							}

							if (stanceWords[0]) DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("(%s)", stanceWords, DYNDEBUG_RGBA_VALUE);
							else                DYNDEBUG_PRETTYPRINT_TEXT("Default", DYNDEBUG_RGBA_VALUE);

							if(strlen(pcBitBuffer) >= 200){
								iCurrentLine++;
								pcBitBuffer[0] = 0;
								lastChartName = NULL;
							}

							lastChartName = dynAnimChartGetName(pChart);
						}
					}
					if(pcBitBuffer[0]){
						DYNDEBUG_PRETTYPRINT_TEXT("]", DYNDEBUG_RGBA_VALUE);
						iCurrentLine++;
					}

					//output the overlay blend data
					if (dynAnimGraphUpdaterIsOverlay(pUpdater))
					{
						indent = 0.0f;
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s    OverlayBlend: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%.2f", dynAnimGraphUpdaterGetOverlayBlend(pUpdater), DYNDEBUG_RGBA_VALUE);
						iCurrentLine++;
					}

					//output the keyword data line
					{
						indent = 0.0f;
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s    Keywords: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
						FOR_EACH_IN_EARRAY_FORWARDS(pDebugSkeletonLog->eaGraphUpdaters[ipUpdaterIndex]->eaKeywords, DynSkeletonDebugKeyword, testKeyword)
						{
							U32 printColor;
							if (testKeyword->fTimeSinceTriggered > 0.0) {
								F32 blend = (DDNAS_KEYWORD_MAXTIME - testKeyword->fTimeSinceTriggered) / (F32)DDNAS_KEYWORD_MAXTIME;
								printColor = lerpRGBAColors(DYNDEBUG_RGBA_VALUEOLDA, DYNDEBUG_RGBA_VALUEOLDB, blend);
							} else {
								printColor = DYNDEBUG_RGBA_VALUE;
							}
							if (itestKeywordIndex != 0) DYNDEBUG_PRETTYPRINT_TEXT(", ", printColor);
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", testKeyword->pcKeyword, printColor);
							if (dynDebugState.danimShowBitsShowTrackingIds) DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("(%u)", testKeyword->uid, printColor);
						}
						FOR_EACH_END;
						iCurrentLine++;
					}

					//output the flag data line
					{
						indent = 0.0f;
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s    Flags: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
						FOR_EACH_IN_EARRAY_FORWARDS(pDebugSkeletonLog->eaGraphUpdaters[ipUpdaterIndex]->eaFlags, DynSkeletonDebugFlag, testFlag)
						{
							U32 printColor;
							if (testFlag->fTimeSinceTriggered > 0.0) {
								F32 blend = (DDNAS_FLAG_MAXTIME - testFlag->fTimeSinceTriggered) / (F32)DDNAS_FLAG_MAXTIME;
								printColor = lerpRGBAColors(DYNDEBUG_RGBA_VALUEOLDA, DYNDEBUG_RGBA_VALUEOLDB, blend);
							} else {
								printColor = DYNDEBUG_RGBA_VALUE;
							}
							if (itestFlagIndex != 0) DYNDEBUG_PRETTYPRINT_TEXT(", ", printColor);
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", testFlag->pcFlag, printColor);
							if (dynDebugState.danimShowBitsShowTrackingIds) DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("(%u)", testFlag->uid, printColor);
						}
						FOR_EACH_END;
						iCurrentLine++;
					}

					//output the FX data line
					{
						indent = 0.0f;
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s    FX: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
						FOR_EACH_IN_EARRAY_FORWARDS(pDebugSkeletonLog->eaGraphUpdaters[ipUpdaterIndex]->eaFX, DynSkeletonDebugFX, testFX)
						{
							U32 printColor;
							if (testFX->fTimeSinceTriggered > 0.0) {
								F32 blend = (DDNAS_FLAG_MAXTIME - testFX->fTimeSinceTriggered) / (F32)DDNAS_FLAG_MAXTIME;
								if (testFX->bPlayed) {
									printColor = lerpRGBAColors(DYNDEBUG_RGBA_VALUENEWA, DYNDEBUG_RGBA_VALUENEWB, blend);
								} else {
									printColor = lerpRGBAColors(DYNDEBUG_RGBA_VALUEOLDA, DYNDEBUG_RGBA_VALUEOLDB, blend);
								}
							} else {
								if (testFX->bPlayed) {
									printColor = DYNDEBUG_RGBA_VALUE;
								} else {
									printColor = DYNDEBUG_RGBA_VALUEOLDA;
								}
							}
							if (itestFXIndex != 0) DYNDEBUG_PRETTYPRINT_TEXT(", ", printColor);
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", testFX->pcFXName, printColor);
						}
						FOR_EACH_END;
						iCurrentLine++;
					}

					j = 0;
					weight = 0.0f;
					ZeroArray(tabTable);
					for (i = -1; i < DDNAS_ANIMGRAPH_MOVE_MAXCOUNT;)
					{
						if (i == -1)
						{
							indent = 0.0f;
							sprintf(pcBitBuffer, "%s    Graph Data: ", pcIndentBuffer);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[0], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "Graph");
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[1], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, DYNDEBUG_TABLE_SPACE);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[2], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "Node");
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[3], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, DYNDEBUG_TABLE_SPACE);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[4], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "Move");
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[5], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, DYNDEBUG_TABLE_SPACE);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[6], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "Reason");
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[7], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, DYNDEBUG_TABLE_SPACE);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[8], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "Blend - Amount");
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[9], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, DYNDEBUG_TABLE_SPACE);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[10], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "Blend - Frame");
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[11], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, DYNDEBUG_TABLE_SPACE);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[12], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "Time");
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[13], indent);

							i++;
						}
						else
						{
							const DynAnimGraphUpdaterNode* pNode = dynAnimGraphUpdaterGetNode(pUpdater, j);
							if (pNode && (j < 2 || (1.0-weight)*dynAnimGraphNodeGetBlendFactor(pNode) >= 0.01f))
							{
								F32 showBlend = (1.0-weight)*dynAnimGraphNodeGetBlendFactor(pNode);

								indent = 0.0f;
								sprintf(pcBitBuffer, "%s      (%s):  ", pcIndentBuffer, j ? "Blend": "Current");
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[0], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "%s", dynAnimGraphNodeGetGraphName(pNode));
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[1], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "%s", dynAnimGraphNodeGetName(pNode));
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[3], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "%s", dynAnimGraphNodeGetMoveName(pNode));
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[5], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "%s, %s", dynAnimGraphNodeGetReason(pNode), dynAnimGraphNodeGetReasonDetails(pNode));
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[7], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "(%.2f, %.2f)", showBlend, dynAnimGraphNodeGetBlendFactor(pNode));
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[9], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "(%.2f/%.2f)", dynAnimGraphNodeGetBlendTime(pNode), dynAnimGraphNodeGetBlendTotalTime(pNode));
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[11], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "(%.2f/%.2f)", dynAnimGraphNodeGetFrameTime(pNode), dynAnimGraphNodeGetMoveTotalTime(pNode));
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[13], indent);

								weight += showBlend;
								j++;
							}
							else if (eaSize(&pDebugSkeletonLog->eaGraphUpdaters[ipUpdaterIndex]->eaNodes) > ++i)
							{
								const DynSkeletonDebugGraphNode* pDebugNode = pDebugSkeletonLog->eaGraphUpdaters[ipUpdaterIndex]->eaNodes[i];

								indent = 0.0f;
								sprintf(pcBitBuffer, "%s      (%s):  ", pcIndentBuffer, "Previous");
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[0], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "%s", pDebugNode->pcGraphName);
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[1], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "%s", pDebugNode->pcNodeName);
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[3], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "%s", pDebugNode->pcMoveName);
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[5], indent);

								indent = 0.0f;
								sprintf(pcBitBuffer, "%s, %s", pDebugNode->reason, pDebugNode->reasonDetails);
								indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
								MAX1(tabTable[7], indent);
							}
						}
					}
					j = 0;
					weight = 0.0f;
					for (i = 1; i < 14; i++) tabTable[i] += tabTable[i-1];
					indent = tabTable[0];
					while (indent < tabTable[13])
					{
						DYNDEBUG_PRETTYPRINT_TEXT("-", DYNDEBUG_RGBA_TITLE);
						indent += DYNDEBUGGETSTRINGWIDTH("-");
					}
					iCurrentLine++;
					for (i = -1; i < DDNAS_ANIMGRAPH_MOVE_MAXCOUNT;)
					{
						if (i == -1)
						{
							indent = 0.0f;
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s    Graph Data: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[0];
							DYNDEBUG_PRETTYPRINT_TEXT("Graph", DYNDEBUG_RGBA_TITLE);
							indent = tabTable[1];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[2];
							DYNDEBUG_PRETTYPRINT_TEXT("Node", DYNDEBUG_RGBA_TITLE);
							indent = tabTable[3];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[4];
							DYNDEBUG_PRETTYPRINT_TEXT("Move", DYNDEBUG_RGBA_TITLE);
							indent = tabTable[5];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[6];
							DYNDEBUG_PRETTYPRINT_TEXT("Reason", DYNDEBUG_RGBA_TITLE);
							indent = tabTable[7];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[8];
							DYNDEBUG_PRETTYPRINT_TEXT("Blend - Amount", DYNDEBUG_RGBA_TITLE);
							indent = tabTable[9];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[10];
							DYNDEBUG_PRETTYPRINT_TEXT("Blend - Frame", DYNDEBUG_RGBA_TITLE);
							indent = tabTable[11];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[12];
							DYNDEBUG_PRETTYPRINT_TEXT("Time", DYNDEBUG_RGBA_TITLE);
							indent = tabTable[13];
							iCurrentLine++;
							i++;
						}
						else
						{
							const DynAnimGraphUpdaterNode* pNode = dynAnimGraphUpdaterGetNode(pUpdater, j);
							F32 showBlend = (1.0-weight)*dynAnimGraphNodeGetBlendFactor(pNode);
							if (pNode && (j < 2 || showBlend >= 0.01f))
							{
								indent = 0.0f;
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s      ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("(%s):  ", j ? "Blend" : "Current", DYNDEBUG_RGBA_TITLE);
								indent = tabTable[0];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", dynAnimGraphNodeGetGraphName(pNode), j == 0 || showBlend >= 0.01f ? DYNDEBUG_RGBA_VALUE : DYNDEBUG_RGBA_VALUEBLEND);
								indent = tabTable[1];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[2];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", dynAnimGraphNodeGetName(pNode),  j == 0 || showBlend >= 0.01f ? DYNDEBUG_RGBA_VALUE : DYNDEBUG_RGBA_VALUEBLEND);
								indent = tabTable[3];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[4];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", dynAnimGraphNodeGetMoveName(pNode),  j == 0 || showBlend >= 0.01f ? DYNDEBUG_RGBA_VALUE : DYNDEBUG_RGBA_VALUEBLEND);
								indent = tabTable[5];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[6];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s, ", dynAnimGraphNodeGetReason(pNode), DYNDEBUG_RGBA_REASON);
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", dynAnimGraphNodeGetReasonDetails(pNode), DYNDEBUG_RGBA_REASON);
								indent = tabTable[7];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[8];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("(%.2f,", showBlend, DYNDEBUG_RGBA_TITLE);
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING(" %.2f)", dynAnimGraphNodeGetBlendFactor(pNode), DYNDEBUG_RGBA_TITLE);
								indent = tabTable[9];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[10];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("(%.2f", dynAnimGraphNodeGetBlendTime(pNode), DYNDEBUG_RGBA_TITLE);
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("/%.2f)", dynAnimGraphNodeGetBlendTotalTime(pNode), DYNDEBUG_RGBA_TITLE);
								indent = tabTable[11];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[12];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("(%.2f", dynAnimGraphNodeGetFrameTime(pNode), DYNDEBUG_RGBA_TITLE);
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("/%.2f)", dynAnimGraphNodeGetMoveTotalTime(pNode), DYNDEBUG_RGBA_TITLE);
								indent = tabTable[13];
								weight += showBlend;
								j++;
								iCurrentLine++;
							}
							else if (eaSize(&pDebugSkeletonLog->eaGraphUpdaters[ipUpdaterIndex]->eaNodes) > ++i)
							{
								const DynSkeletonDebugGraphNode* pDebugNode = pDebugSkeletonLog->eaGraphUpdaters[ipUpdaterIndex]->eaNodes[i];

								indent = 0.0f;
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s      ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
								DYNDEBUG_PRETTYPRINT_TEXT("(Previous):  ", DYNDEBUG_RGBA_TITLE);
								indent = tabTable[0];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pDebugNode->pcGraphName, DYNDEBUG_RGBA_VALUEOLDB);
								indent = tabTable[1];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[2];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pDebugNode->pcNodeName, DYNDEBUG_RGBA_VALUEOLDB);
								indent = tabTable[3];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[4];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pDebugNode->pcMoveName, DYNDEBUG_RGBA_VALUEOLDB);
								indent = tabTable[5];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[6];
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s, ", pDebugNode->reason, DYNDEBUG_RGBA_REASON);
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pDebugNode->reasonDetails, DYNDEBUG_RGBA_REASON);
								indent = tabTable[7];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[8];
								iCurrentLine++;
							}
						}
					}
					indent = tabTable[0];
					while (indent < tabTable[13])
					{
						DYNDEBUG_PRETTYPRINT_TEXT("-", DYNDEBUG_RGBA_TITLE);
						indent += DYNDEBUGGETSTRINGWIDTH("-");
					}
					iCurrentLine++;
				}
			}
			FOR_EACH_END;

			if (!gConf.bUseMovementGraphs &&
				(	!dynDebugState.danimShowBitsHideMovement ||
					dynDebugState.audioShowAnimBits))
			{
				int i = 0, tabTable[9];

				//output the Move FX data line
				{
					indent = 0.0f;
					DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s    Move FX: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
					FOR_EACH_IN_EARRAY_FORWARDS(pDebugSkeletonLog->eaMovementFX, DynSkeletonDebugFX, testMoveFX)
					{
						U32 printColor;
						if (testMoveFX->fTimeSinceTriggered > 0.0) {
							F32 blend = (DDNAS_FLAG_MAXTIME - testMoveFX->fTimeSinceTriggered) / (F32)DDNAS_FLAG_MAXTIME;
							if (testMoveFX->bPlayed) {
								printColor = lerpRGBAColors(DYNDEBUG_RGBA_VALUENEWA, DYNDEBUG_RGBA_VALUENEWB, blend);
							} else {
								printColor = lerpRGBAColors(DYNDEBUG_RGBA_VALUEOLDA, DYNDEBUG_RGBA_VALUEOLDB, blend);
							}
						} else {
							if (testMoveFX->bPlayed) {
								printColor = DYNDEBUG_RGBA_VALUE;
							} else {
								printColor = DYNDEBUG_RGBA_VALUEOLDA;
							}
						}
						if (itestMoveFXIndex != 0) DYNDEBUG_PRETTYPRINT_TEXT(", ", printColor);
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", testMoveFX->pcFXName, printColor);
					}
					FOR_EACH_END;
					iCurrentLine++;
				}

				//output the move table
				ZeroArray(tabTable);
				for (i = -1; i < DDNAS_MOVEMENTBLOCK_MAXCOUNT; i++)
				{
					if (i == -1)
					{
						indent = 0.0f;
						sprintf(pcBitBuffer, "%s  Moves: ", pcIndentBuffer);
						indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
						MAX1(tabTable[0] ,indent);

						indent = 0.0f;
						sprintf(pcBitBuffer, "Type");
						indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
						MAX1(tabTable[1], indent);

						indent = 0.0f;
						sprintf(pcBitBuffer, DYNDEBUG_TABLE_SPACE);
						indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
						MAX1(tabTable[2], indent);

						indent = 0.0f;
						sprintf(pcBitBuffer, "Name");
						indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
						MAX1(tabTable[3], indent);

						indent = 0.0f;
						sprintf(pcBitBuffer, DYNDEBUG_TABLE_SPACE);
						indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
						MAX1(tabTable[4], indent);

						indent = 0.0f;
						sprintf(pcBitBuffer, "Stances");
						indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
						MAX1(tabTable[5], indent);

						indent = 0.0f;
						sprintf(pcBitBuffer, DYNDEBUG_TABLE_SPACE);
						indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
						MAX1(tabTable[6], indent);

						indent = 0.0f;
						sprintf(pcBitBuffer, "(blend, time)");
						indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
						MAX1(tabTable[7], indent);

						indent = 0.0f;
						sprintf(pcBitBuffer, DYNDEBUG_TABLE_SPACE);
						indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
						MAX1(tabTable[8], indent);
					}
					else
					{
						indent = 0.0f;
						sprintf(pcBitBuffer, "%s  Move %d: ", pcIndentBuffer, i);
						indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
						MAX1(tabTable[0] ,indent);

						if (i < eaSize(&pDebugSkeleton->movement.eaBlocks))
						{
							DynMovementBlock *b = pDebugSkeleton->movement.eaBlocks[i];
							const DynAnimChartRunTime*	c = dynMovementBlockGetChart(b);
							char						stanceWords[200];

							stanceWords[0] = 0;

							if(c){
								dynAnimChartGetStanceWords(c, SAFESTR(stanceWords));
							}

							indent = 0.0f;
							sprintf(pcBitBuffer, "%s", dynMovementBlockGetMovementType(b));
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[1], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "%s", dynMovementBlockGetMoveName(b));
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[3], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "%s", stanceWords);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[5], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "(%.2f, %.2f)", dynMovementBlockGetBlendFactor(b), dynMovementBlockGetFrameTime(b));
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[7], indent);
						}
						else if (i < eaSize(&pDebugSkeletonLog->eaMovementBlocks))
						{
							const char *type = pDebugSkeletonLog->eaMovementBlocks[i]->pcType;
							const char *name = pDebugSkeletonLog->eaMovementBlocks[i]->pcName;
							const char *cdes = pDebugSkeletonLog->eaMovementBlocks[i]->pcCDes;
							char stanceWords[200];

							stanceWords[0] = 0;

							if (cdes && strlen(cdes)){
								dynAnimChartGetStanceWords(dynAnimChartGet(cdes), SAFESTR(stanceWords));
							}

							indent = 0.0f;
							sprintf(pcBitBuffer, "%s", type);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[1], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "%s", name);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[3], indent);

							indent = 0.0f;
							sprintf(pcBitBuffer, "%s", stanceWords);
							indent += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
							MAX1(tabTable[5], indent);
						}
					}
				}
				for (i = 1; i < 9; i++) tabTable[i] += tabTable[i-1];
				indent = tabTable[0];
				while (indent < tabTable[8])
				{
					DYNDEBUG_PRETTYPRINT_TEXT("-", DYNDEBUG_RGBA_TITLE);
					indent += DYNDEBUGGETSTRINGWIDTH("-");
				}
				iCurrentLine++;
				for (i = -1; i < DDNAS_MOVEMENTBLOCK_MAXCOUNT; i++)
				{
					if (i == -1)
					{
						indent = 0.0f;
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s  Moves: ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
						indent = tabTable[0];
						DYNDEBUG_PRETTYPRINT_TEXT("Type", DYNDEBUG_RGBA_TITLE);
						indent = tabTable[1];
						DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
						indent = tabTable[2];
						DYNDEBUG_PRETTYPRINT_TEXT("Name", DYNDEBUG_RGBA_TITLE);
						indent = tabTable[3];
						DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
						indent = tabTable[4];
						DYNDEBUG_PRETTYPRINT_TEXT("Stances", DYNDEBUG_RGBA_TITLE);					
						indent = tabTable[5];
						DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
						indent = tabTable[6];
						DYNDEBUG_PRETTYPRINT_TEXT("(blend, time)", DYNDEBUG_RGBA_TITLE);
						iCurrentLine++;
					}
					else
					{
						indent = 0.0f;
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s  Move ", pcIndentBuffer, DYNDEBUG_RGBA_TITLE);
						DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%d: ", i, DYNDEBUG_RGBA_TITLE);

						if (i < eaSize(&pDebugSkeleton->movement.eaBlocks))
						{
							DynMovementBlock*			b = pDebugSkeleton->movement.eaBlocks[i];
							const DynAnimChartRunTime*	c = dynMovementBlockGetChart(b);
							char						stanceWords[200];

							stanceWords[0] = 0;

							if(c){
								dynAnimChartGetStanceWords(c, SAFESTR(stanceWords));
							}

							indent = tabTable[0];
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", dynMovementBlockGetMovementType(b), DYNDEBUG_RGBA_VALUE);
							indent = tabTable[1];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[2];
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", dynMovementBlockGetMoveName(b), DYNDEBUG_RGBA_VALUE);
							indent = tabTable[3];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[4];
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", stanceWords, DYNDEBUG_RGBA_VALUE);
							indent = tabTable[5];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[6];
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("(%.2f", dynMovementBlockGetBlendFactor(b), DYNDEBUG_RGBA_TITLE);
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING(", %.2f)", dynMovementBlockGetFrameTime(b), DYNDEBUG_RGBA_TITLE);
							if(dynMovementBlockIsInTransition(b)){
								indent = tabTable[7];
								DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
								indent = tabTable[8];
								dynMovementBlockGetTransitionString(dynMovementBlockGetTransition(b), SAFESTR(stanceWords));
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING(" TRANSITION[%s]", stanceWords, DYNDEBUG_RGBA_TITLE);
							}
						}
						else if (i < eaSize(&pDebugSkeletonLog->eaMovementBlocks))
						{
							const char *type = pDebugSkeletonLog->eaMovementBlocks[i]->pcType;
							const char *name = pDebugSkeletonLog->eaMovementBlocks[i]->pcName;
							const char *cdes = pDebugSkeletonLog->eaMovementBlocks[i]->pcCDes;
							const char *tdes = pDebugSkeletonLog->eaMovementBlocks[i]->pcTDes;
							char stanceWords[200];

							stanceWords[0] = 0;

							indent = tabTable[0];
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", type, DYNDEBUG_RGBA_VALUEOLDB);
							indent = tabTable[1];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							indent = tabTable[2];
							DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", name, DYNDEBUG_RGBA_VALUEOLDB);
							indent = tabTable[3];
							DYNDEBUG_PRETTYPRINT_TEXT(DYNDEBUG_TABLE_SPACE, DYNDEBUG_RGBA_TITLE);
							if (cdes && strlen(cdes)) {
								indent = tabTable[4];
								dynAnimChartGetStanceWords(dynAnimChartGet(cdes), SAFESTR(stanceWords));
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", stanceWords, DYNDEBUG_RGBA_VALUEOLDB);
							}
							if(tdes && strlen(tdes)){
								indent = tabTable[8];
								dynMovementBlockGetTransitionString(dynMoveTransitionGet(tdes), SAFESTR(stanceWords));
								DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING(" TRANSITION[%s]", stanceWords, DYNDEBUG_RGBA_TITLE);
							}
						}

						iCurrentLine++;
					}
				}
				indent = tabTable[0];
				while (indent < tabTable[8])
				{
					DYNDEBUG_PRETTYPRINT_TEXT("-", DYNDEBUG_RGBA_TITLE);
					indent += DYNDEBUGGETSTRINGWIDTH("-");
				}
				iCurrentLine++;
			}
		}
	}

	//make recursive calls
	if (dynDebugState.danimShowBitsShowSubSkeleton && !dynDebugState.audioShowAnimBits)
	{
		FOR_EACH_IN_EARRAY(pDebugSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton)
		{
			iCurrentLine = gfxDebugDrawDynDebug2DNewAnimBitsRecursive(pChildSkeleton, iFirstLine, iPixPerLine, ++iCurrentLine, colors, numIndentChars+5);
		}
		FOR_EACH_END;
	}

	return iCurrentLine;
}

static void gfxDebugDrawDynDebug2DCostumeSkeletonFiles(const DynSkeleton *pSkeleton, const int iFirstLine, int iPixPerLine, int iCurrentLine, int *colors)
{
	char pcBitBuffer[1024];
	F32 indent;
	U32 shownCharacters;

	//Output the skeleton's name
	indent = 0.0f;
	DYNDEBUG_PRETTYPRINT_TEXT("Entity: ", DYNDEBUG_RGBA_TITLE);
	DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s",
		(pSkeleton->pRoot->pParent && pSkeleton->pRoot->pParent->pcTag) ? pSkeleton->pRoot->pParent->pcTag : "Unknown",
		DYNDEBUG_RGBA_VALUE);
	iCurrentLine++;
	
	//Output the entity's costume's name
	indent = 20.0f;
	DYNDEBUG_PRETTYPRINT_TEXT("Costume Name: ", DYNDEBUG_RGBA_TITLE);
	DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pSkeleton->debugCostumeInfo.pcDebugCostume, DYNDEBUG_RGBA_VALUE);
	iCurrentLine++;

	//Output the entity's costume's file
	indent = 20.0f;
	DYNDEBUG_PRETTYPRINT_TEXT("Costume File: ", DYNDEBUG_RGBA_TITLE);
	DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pSkeleton->debugCostumeInfo.pcDebugCostumeFilename, DYNDEBUG_RGBA_VALUE);
	iCurrentLine++;

	//Output the entity's cskel's name
	indent = 20.0f;
	DYNDEBUG_PRETTYPRINT_TEXT("CSkel Name: ", DYNDEBUG_RGBA_TITLE);
	DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pSkeleton->debugCostumeInfo.pcDebugCSkel, DYNDEBUG_RGBA_VALUE);
	iCurrentLine++;

	//Output the entity's cskel's file
	indent = 20.0f;
	DYNDEBUG_PRETTYPRINT_TEXT("CSkel File: ", DYNDEBUG_RGBA_TITLE);
	DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pSkeleton->debugCostumeInfo.pcDebugCSkelFilename, DYNDEBUG_RGBA_VALUE);
	iCurrentLine++;

	//Output the entity's species's name
	indent = 20.0f;
	DYNDEBUG_PRETTYPRINT_TEXT("Species Name: ", DYNDEBUG_RGBA_TITLE);
	DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pSkeleton->debugCostumeInfo.pcDebugSpecies, DYNDEBUG_RGBA_VALUE);
	iCurrentLine++;

	//Output the entity's species's file
	indent = 20.0f;
	DYNDEBUG_PRETTYPRINT_TEXT("Species File: ", DYNDEBUG_RGBA_TITLE);
	DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s", pSkeleton->debugCostumeInfo.pcDebugSpeciesFilename, DYNDEBUG_RGBA_VALUE);
	iCurrentLine++;

	//Output the required click bone names
	indent = 20.0f;
	shownCharacters = 0;
	DYNDEBUG_PRETTYPRINT_TEXT("Required (Fx-Material) Click Bone Names: ", DYNDEBUG_RGBA_TITLE);
	iCurrentLine++;
	indent = 40.0f;
	FOR_EACH_IN_EARRAY_FORWARDS(pSkeleton->debugCostumeInfo.eaDebugRequiredClickBoneNames, const char, pcBoneName)
	{
		DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s",pcBoneName,DYNDEBUG_RGBA_VALUE);
		shownCharacters += strlen(pcBoneName)+2;
		if (shownCharacters < 100 &&
			ipcBoneNameIndex != eaSize(&pSkeleton->debugCostumeInfo.eaDebugRequiredClickBoneNames)-1) {
			DYNDEBUG_PRETTYPRINT_TEXT(", ",DYNDEBUG_RGBA_VALUE);
		} else {
			indent = 40.0f;
			iCurrentLine++;
			shownCharacters = 0;
		}
	}
	FOR_EACH_END;
	if (shownCharacters != 0)
		iCurrentLine++;

	//Output the optional click bone names
	indent = 20.0f;
	shownCharacters = 0;
	DYNDEBUG_PRETTYPRINT_TEXT("Optional (Fx-Material) Click Bone Names: ", DYNDEBUG_RGBA_TITLE);
	iCurrentLine++;
	indent = 40.0f;
	FOR_EACH_IN_EARRAY_FORWARDS(pSkeleton->debugCostumeInfo.eaDebugOptionalClickBoneNames, const char, pcBoneName)
	{
		DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s",pcBoneName,DYNDEBUG_RGBA_VALUE);
		shownCharacters += strlen(pcBoneName)+2;
		if (shownCharacters < 100 &&
			ipcBoneNameIndex != eaSize(&pSkeleton->debugCostumeInfo.eaDebugOptionalClickBoneNames)-1) {
			DYNDEBUG_PRETTYPRINT_TEXT(", ",DYNDEBUG_RGBA_VALUE);
		} else {
			indent = 40.0f;
			iCurrentLine++;
			shownCharacters = 0;
		}
	}
	FOR_EACH_END;
	if (shownCharacters != 0)
		iCurrentLine++;

	//Output the maintained FX (shows attached grunt packs + other fx)
	indent = 20.0f;
	DYNDEBUG_PRETTYPRINT_TEXT("Maintained FX: ", DYNDEBUG_RGBA_TITLE);
	iCurrentLine++;
	if (pSkeleton->pFxManager) {
		FOR_EACH_IN_EARRAY(pSkeleton->pFxManager->eaMaintainedFx, DynFxMaintained, pFxMaintained)
		{
			DynFxInfo *pFxInfo;
			if (pFxInfo = GET_REF(pFxMaintained->hInfo)) {
				indent = 40.0f;
				DYNDEBUG_PRETTYPRINT_FORMATTEDSTRING("%s",
					pFxInfo->pcDynName,
					DYNDEBUG_RGBA_VALUE);
				iCurrentLine++;
			}
		}
		FOR_EACH_END;
	}
}

void  gfxDebugDrawDynDebug2D(void)
{
	// Draw all sorts of information about the player's sequencer
	const DynSkeleton* pDebugSkeleton;
	const int iFirstLine = 170;
	const int iPixPerLine = 20;
	int iCurrentLine = 0;
	int colors[] = {-1, -1, -1, -1};

	if (dynDebugState.pDebugSkeleton)
		pDebugSkeleton = dynDebugState.pDebugSkeleton;
	else
	{
		const DynDrawSkeleton* pDebugDrawSkeleton = dynDebugGetDebugSkeleton();
		pDebugSkeleton = pDebugDrawSkeleton?pDebugDrawSkeleton->pSkeleton:NULL;
	}

	g_font_Sans.bold = 0;
	g_font_Sans.italicize = 0;
	g_font_Sans.outlineWidth = 1;

	if (pDebugSkeleton && (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits || dynDebugState.costumeShowSkeletonFiles))
	{
		dynSkeletonValidateAll();

		if (dynDebugState.costumeShowSkeletonFiles) {
			gfxDebugDrawDynDebug2DCostumeSkeletonFiles(pDebugSkeleton, iFirstLine, iPixPerLine, iCurrentLine, colors);
		}

		if (dynDebugState.danimShowBits && !gConf.bNewAnimationSystem)
		{

			static const DynSkeleton** eaSkelsToDisplay = NULL;
			int x = 0;
			int iMaxX = 0;
			char pcBitBuffer[1024];
			eaPush(&eaSkelsToDisplay, pDebugSkeleton);
			eaPushEArray(&eaSkelsToDisplay, &pDebugSkeleton->eaDependentSkeletons);

			sprintf(pcBitBuffer, "Entity: %s", (pDebugSkeleton->pRoot->pParent && pDebugSkeleton->pRoot->pParent->pcTag)?pDebugSkeleton->pRoot->pParent->pcTag:"Unknown");
			DYNDEBUGWRITELINE(pcBitBuffer);

			FOR_EACH_IN_EARRAY_FORWARDS(eaSkelsToDisplay, const DynSkeleton, pDisplaySkeleton)
			{
				const char* pcActionName;
				static char *pchChange = NULL;
				char *pchCheck;
				iCurrentLine = 1;

				x = iMaxX?iMaxX + 10:0;
				iMaxX = 0;

				assert(eaSize(&pDisplaySkeleton->eaSqr) > 0);

				DYNDEBUGWRITEWITHXANDRECORDMAX("Sequencers:", x);
				FOR_EACH_IN_EARRAY_FORWARDS(pDisplaySkeleton->eaSqr, DynSequencer, pSqr)
				{
					char achActionBuffer[1024];
					sprintf(achActionBuffer,"%d: %s",ipSqrIndex+1, dynSeqGetName(pSqr));
					DYNDEBUGWRITEWITHXANDRECORDMAX(achActionBuffer, x);
				}
				FOR_EACH_END;
				++iCurrentLine;
				if (pDisplaySkeleton->bTorsoPointing)
				{
					sprintf(pcBitBuffer, "LowerBody %.2f", pDisplaySkeleton->fLowerBodyBlendFactor);
					DYNDEBUGWRITEWITHXANDRECORDMAX(pcBitBuffer, x);
				}

				sprintf(pcBitBuffer, "OverrideAll %d: %.2f", pDisplaySkeleton->iOverrideSeqIndex, pDisplaySkeleton->fOverrideAllBlendFactor);
				DYNDEBUGWRITEWITHXANDRECORDMAX(pcBitBuffer, x);

				sprintf(pcBitBuffer, "Wep Registration %.2f", pDisplaySkeleton->fWepRegisterBlend);
				DYNDEBUGWRITEWITHXANDRECORDMAX(pcBitBuffer, x);
				sprintf(pcBitBuffer, "MovementSync %.2f", pDisplaySkeleton->fMovementSyncPercentage);
				DYNDEBUGWRITEWITHXANDRECORDMAX(pcBitBuffer, x);
				//sprintf(pcBitBuffer, "OverlayBlend %.2f", pDisplaySkeleton->fOverlayBlend);
				//DYNDEBUGWRITEWITHXANDRECORDMAX(pcBitBuffer, x);
				++iCurrentLine;
				DYNDEBUGWRITEWITHXANDRECORDMAX("Bits:", x);
				{
					F32 fBitX = 0.0f;
					eaQSortG(dynDebugState.eaSkelBits, dynDebugBitSort);
					FOR_EACH_IN_EARRAY(dynDebugState.eaSkelBits, DynDebugBit, pDebugBit)
					{
						sprintf(pcBitBuffer, "%s ", pDebugBit->pcBitName);
						strupr(pcBitBuffer);
						if (pDebugBit->fTimeSinceSet < 0.0f)
							setVec4same(colors, -1);
						else
						{
							int iColor = lerpRGBAColors(0xFF0000FF, 0x000000FF, CLAMP(pDebugBit->fTimeSinceSet / DYN_DEBUG_MAX_BIT_DISPLAY_TIME, 0.0f, 1.0f));
							setVec4same(colors, iColor);
						}
						DYNDEBUGWRITEWITHX(pcBitBuffer, fBitX + x);
						fBitX += DYNDEBUGGETSTRINGWIDTH(pcBitBuffer);
					}
					FOR_EACH_END;
					MAX1(iMaxX, fBitX);
					++iCurrentLine;
					setVec4same(colors, -1);
				}
				//dynBitFieldWriteBitString(pcBitBuffer, ARRAY_SIZE_CHECKED(pcBitBuffer), dynSeqGetBits(pDisplaySkeleton->eaSqr[0]));
				++iCurrentLine;
				DYNDEBUGWRITEWITHXANDRECORDMAX("Sequence:", x);
				FOR_EACH_IN_EARRAY_FORWARDS(pDisplaySkeleton->eaSqr, DynSequencer, pSqr)
				{
					pcActionName = dynSeqGetCurrentSequenceName(pSqr);
					if ( pcActionName )
					{
						char achActionBuffer[1024];
						if (dynSeqIsOverlay_DbgOnly(pSqr)) {
							sprintf(achActionBuffer,"%d: %s (blend %f)",ipSqrIndex+1, pcActionName, dynSeqGetBlend(pSqr));
						} else {
							sprintf(achActionBuffer,"%d: %s",ipSqrIndex+1, pcActionName);
						}
						DYNDEBUGWRITEWITHXANDRECORDMAX(achActionBuffer, x);
					}
				}
				FOR_EACH_END;
				++iCurrentLine;
				DYNDEBUGWRITEWITHXANDRECORDMAX("Action:", x);
				FOR_EACH_IN_EARRAY_FORWARDS(pDisplaySkeleton->eaSqr, DynSequencer, pSqr)
				{
					pcActionName = dynSeqGetCurrentActionName(pSqr);
					if ( pcActionName )
					{
						char achActionBuffer[1024];
						sprintf(achActionBuffer,"%d: %2.2f %s",ipSqrIndex+1, dynSeqGetCurrentActionFrame(pSqr),pcActionName);
						DYNDEBUGWRITEWITHXANDRECORDMAX(achActionBuffer, x);
					}
				}
				FOR_EACH_END;
				++iCurrentLine;
				DYNDEBUGWRITEWITHXANDRECORDMAX("Move:", x);
				FOR_EACH_IN_EARRAY_FORWARDS(pDisplaySkeleton->eaSqr, DynSequencer, pSqr)
				{
					pcActionName = dynSeqGetCurrentMoveName(pSqr);
					if ( pcActionName )
					{
						char achActionBuffer[1024];
						sprintf(achActionBuffer,"%d: %s",ipSqrIndex+1, pcActionName);
						DYNDEBUGWRITEWITHXANDRECORDMAX(achActionBuffer, x);
					}
				}
				FOR_EACH_END;
				++iCurrentLine;

				//Detect changes
				if(!pchChange)
					estrCreate(&pchChange);

				estrStackCreate(&pchCheck);

				dynBitFieldWriteBitString(pcBitBuffer, ARRAY_SIZE_CHECKED(pcBitBuffer), dynSeqGetBits(pDisplaySkeleton->eaSqr[0]));
				estrPrintf(&pchCheck,"Bits: %s",pcBitBuffer);

				if(strcmp(pchCheck,pchChange) != 0)
				{
					estrPrintf(&pchChange,"%s",pchCheck);
					printf("Bit Field Change! %s\n",pchChange);
				}

				estrDestroy(&pchCheck);

			}
			FOR_EACH_END;
			eaClear(&eaSkelsToDisplay);
		}
		else if ((dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits) && gConf.bNewAnimationSystem)
		{
			iCurrentLine = gfxDebugDrawDynDebug2DNewAnimBitsRecursive(pDebugSkeleton,  dynDebugState.costumeShowSkeletonFiles ? iFirstLine + 250 : iFirstLine, iPixPerLine, iCurrentLine, colors, 0);
		}
	}

	if (dynDebugState.bRecordFXProfile)
	{
		const char* pcRecordingMsg = "FX RECORDING";
		setVec4same(colors, 0xFF0000FF);
		gfxfont_PrintEx(&g_font_Sans, 200, 200, 0, 3, 3, 0, pcRecordingMsg, (int)strlen(pcRecordingMsg), colors, NULL);
		setVec4same(colors, -1);
	}

	if ( dynDebugState.bDrawNumFx )
	{
		U32 uiNumFx = dynFxDebugFxCount();
		char cNumFx[1024];
		setVec4same(colors, -1);
		sprintf(cNumFx, "DynFx: %d", uiNumFx);
		DYNDEBUGWRITELINE(cNumFx);

		sprintf(cNumFx, "Fast Particles Updated: %d", dynDebugState.uiNumFastParticles);
		DYNDEBUGWRITELINE(cNumFx);

		sprintf(cNumFx, "Entity Fast Particles Allocated: %d / %d", dynDebugState.uiNumAllocatedFastParticlesEntities, MAX_ALLOCATED_FAST_PARTICLES_ENTITY);
		if (dynDebugState.uiNumAllocatedFastParticlesEntities >= MAX_ALLOCATED_FAST_PARTICLES_ENTITY)
			setVec4same(colors, 0xFF0000FF);
		DYNDEBUGWRITELINE(cNumFx);

		sprintf(cNumFx, "Environment Fast Particles Allocated: %d / %d", dynDebugState.uiNumAllocatedFastParticlesEnvironment, MAX_ALLOCATED_FAST_PARTICLES_ENVIRONMENT);
		if (dynDebugState.uiNumAllocatedFastParticlesEnvironment >= MAX_ALLOCATED_FAST_PARTICLES_ENVIRONMENT)
			setVec4same(colors, 0xFF0000FF);
		DYNDEBUGWRITELINE(cNumFx);

		setVec4same(colors, -1);
		sprintf(cNumFx, "Fast Particle Sets Updated: %d", dynDebugState.uiNumFastParticleSets);
		DYNDEBUGWRITELINE(cNumFx);

		sprintf(cNumFx, "Fast Particle Sets Allocated: %d / %d", dynDebugState.uiNumAllocatedFastParticleSets, MAX_FAST_PARTICLE_SETS);
		if (dynDebugState.uiNumAllocatedFastParticleSets >= MAX_FAST_PARTICLE_SETS)
			setVec4same(colors, 0xFF0000FF);
		DYNDEBUGWRITELINE(cNumFx);

		setVec4same(colors, -1);
		sprintf(cNumFx, "Drawn FX Objects: %d / %d", dynDebugState.frameCounters.uiNumDrawnFx, dynDebugState.uiMaxDrawn);
		if (dynDebugState.frameCounters.uiNumDrawnFx >= dynDebugState.uiMaxDrawn)
			setVec4same(colors, 0xFF0000FF);
		DYNDEBUGWRITELINE(cNumFx);

		setVec4same(colors, -1);
		sprintf(cNumFx, "Drawn Costume FX: %d", dynDebugState.frameCounters.uiNumDrawnCostumeFx);
		DYNDEBUGWRITELINE(cNumFx);

		sprintf(cNumFx, "Drawn Fast Particles / Sets: %d / %d", dynDebugState.frameCounters.uiNumDrawnFastParticles, dynDebugState.frameCounters.uiNumDrawnFastParticleSets);
		DYNDEBUGWRITELINE(cNumFx);

		sprintf(cNumFx, "Debris: %d / %d", dynDebugState.uiNumDebris, uiMaxDebris);
		DYNDEBUGWRITELINE(cNumFx);

		sprintf(cNumFx, "Drawn Debris: %d", dynDebugState.frameCounters.uiNumDrawnDebris);
		DYNDEBUGWRITELINE(cNumFx);

		sprintf(cNumFx, "Num DynNodes: %d", dynNodeGetAllocCount());
		DYNDEBUGWRITELINE(cNumFx);

		sprintf(cNumFx, "Sort Buckets: %d", dynSortBucketCount());
		DYNDEBUGWRITELINE(cNumFx);

		sprintf(cNumFx, "Sound Starts / Moves: %d / %d", dynDebugState.uiNumFxSoundStarts, dynDebugState.uiNumFxSoundMoves);
		DYNDEBUGWRITELINE(cNumFx);

		if (dynDebugState.uiNumPhysicsObjects-dynDebugState.uiNumDebris > 200 || dynDebugState.uiNumExcessivePhysicsObjectsFX)
		{
			setVec4same(colors, 0xff0000ff);
		}
		if (dynDebugState.uiNumExcessivePhysicsObjectsFX)
		{
			sprintf(cNumFx, "Physics objects: %d, %d FX with more than 100 physics objects!", dynDebugState.uiNumPhysicsObjects, dynDebugState.uiNumExcessivePhysicsObjectsFX);
		}
		else
		{
			sprintf(cNumFx, "Physics objects: %d", dynDebugState.uiNumPhysicsObjects);
		}
		DYNDEBUGWRITELINE(cNumFx);
		setVec4same(colors, -1);
	}

	// Draw FX labels.
	if(dynDebugState.bLabelDebugFX || dynDebugState.bLabelAllFX) {

		int i;
		Vec3 vLastPos = {8e16, 8e16, 8e16};
		int iConsecutiveThingsOnOneSpot = 0;

		for(i = 0; i < eaSize(&dynDebugState.eaFXWorldSpaceMessages); i++) {

			Vec3 vWorldPos;
			Vec3 vScreenPos;
			Vec3 vScreenPixPos;
			int screenWidth, screenHeight;
			int rgba[4] = {-1, -1, -1, -1};
			F32 fScale = 1;
			
			// Convert world coordinates to screen coords.
			copyVec3(dynDebugState.eaFXWorldSpaceMessages[i]->vPos, vWorldPos);

			mulVecMat4(vWorldPos, gfx_state.currentCameraView->frustum.viewmat, vScreenPos);
			mulVec3ProjMat44(vScreenPos, gfx_state.currentCameraView->projection_matrix, vScreenPixPos);

			gfxGetActiveSurfaceSizeInline(&screenWidth, &screenHeight);

			vScreenPixPos[0] = ((vScreenPixPos[0] + 1.0) / 2.0) * screenWidth;
			vScreenPixPos[1] = ((2.0 - (vScreenPixPos[1] + 1.0)) / 2.0) * screenHeight;
			fScale = 30 * (1.0 - vScreenPixPos[2]);

			// As long as we get a bunch of messages on the same spot, move
			// them so they stack vertically instead of just drawing all on top
			// of each other.
			if(nearSameVec3(vLastPos, dynDebugState.eaFXWorldSpaceMessages[i]->vPos)) {
				iConsecutiveThingsOnOneSpot++;
				vScreenPixPos[1] += 15.0 * fScale * (F32)iConsecutiveThingsOnOneSpot;
			} else {
				iConsecutiveThingsOnOneSpot = 0;
			}

			copyVec3(dynDebugState.eaFXWorldSpaceMessages[i]->vPos, vLastPos);

			// Convert color.
			// FIXME: Endian horribleness?
			rgba[0] =
				(int)(dynDebugState.eaFXWorldSpaceMessages[i]->color.a) |
				((int)(dynDebugState.eaFXWorldSpaceMessages[i]->color.b) << 8) |
				((int)(dynDebugState.eaFXWorldSpaceMessages[i]->color.g) << 16) | 
				((int)(dynDebugState.eaFXWorldSpaceMessages[i]->color.r) << 24);

			rgba[1] = rgba[2] = rgba[0];

			// Draw the text.
			if(vScreenPixPos[2] > 0 && vScreenPixPos[2] <= 1)
				gfxfont_PrintEx(
					&g_font_Sans,
					vScreenPixPos[0],
					vScreenPixPos[1],
					1.0-vScreenPixPos[2],
					fScale,
					fScale,
					0,
					dynDebugState.eaFXWorldSpaceMessages[i]->pcMessage, 
					strlen(dynDebugState.eaFXWorldSpaceMessages[i]->pcMessage),
					rgba, NULL);
		}

	}
	
	// Clean up FX labels, whether we showed them or not.
	dtIterateWorldSpaceMessages(gfxGetFrameTime());
	
	if ( dynDebugState.bDrawNumSkels )
	{
		char cNumFx[1024];
		int i;
		U32 uiTotalSkelsUpdated = 0;
		U32 uiTotalBonesUpdated = 0;
		const WorldRegionLODSettings* pLODSettings = worldLibGetLODSettings();

		for (i=0; i<5; ++i)
		{
			uiTotalSkelsUpdated += dynDebugState.uiNumUpdatedSkels[i];
			uiTotalBonesUpdated += dynDebugState.uiNumBonesUpdated[i];
		}
		sprintf(cNumFx, "Total Skeletons: %d", dynSkeletonGetTotalCount());
		DYNDEBUGWRITELINE(cNumFx);
		sprintf(cNumFx, "Drawn Skeletons: %d", dynDebugState.frameCounters.uiNumDrawnVisualPassSkels);
		DYNDEBUGWRITELINE(cNumFx);
		sprintf(cNumFx, "Drawn Skeleton Shadow Passes: %d", dynDebugState.frameCounters.uiNumDrawnShadowPassSkels);
		DYNDEBUGWRITELINE(cNumFx);
		sprintf(cNumFx, "Invisible Skeletons: %d", dynDebugState.uiNumCulledSkels);
		DYNDEBUGWRITELINE(cNumFx);
		{
			U32 uiLevel;
			U32 uiMaxLODSlots = 0;
			sprintf(cNumFx, "Animated Skeletons: %d   ", uiTotalSkelsUpdated);
			for (uiLevel=0; uiLevel < pLODSettings->uiNumLODLevels; ++uiLevel)
				strcatf(cNumFx, "L%d: %d  ", uiLevel, dynDebugState.uiNumUpdatedSkels[uiLevel]);
			DYNDEBUGWRITELINE(cNumFx);

			for (uiLevel=0; uiLevel < pLODSettings->uiNumLODLevels; ++uiLevel)
				uiMaxLODSlots += pLODSettings->MaxLODSkelSlots[uiLevel];
			sprintf(cNumFx, "Max Skeletons: %d   ", uiMaxLODSlots);
			for (uiLevel=0; uiLevel < pLODSettings->uiNumLODLevels; ++uiLevel)
				strcatf(cNumFx, "L%d: %d  ", uiLevel, pLODSettings->MaxLODSkelSlots[uiLevel]);
			DYNDEBUGWRITELINE(cNumFx);
		}
		if (getNumEntities() >= 0)
		{
			sprintf(cNumFx, "Number of Entities on Client: %d", getNumEntities());
			DYNDEBUGWRITELINE(cNumFx);
		}
		if (getNumClientOnlyEntities() >= 0)
		{
			sprintf(cNumFx, "Number of Client-Only Entities: %d", getNumClientOnlyEntities());
			DYNDEBUGWRITELINE(cNumFx);
		}
		sprintf(cNumFx, "Allocated Skeletons (Unpooled): %d  (%d)", dynSkeletonGetAllocationCount(), dynSkeletonGetUnpooledCount());
		DYNDEBUGWRITELINE(cNumFx);
		sprintf(cNumFx, "Max Bones Per Skeleton: %d", uiMaxBonesPerSkeleton);
		DYNDEBUGWRITELINE(cNumFx);
		sprintf(cNumFx, "Drawn Character Models: %d normal, %d skinned, %d cloth", dynDebugState.frameCounters.uiNumDrawnModels, dynDebugState.frameCounters.uiNumDrawnSkinnedModels, dynDebugState.frameCounters.uiNumDrawnClothMeshes);
		DYNDEBUGWRITELINE(cNumFx);
		sprintf(cNumFx, "Num Imposter Atlases: %d", gfxImposterDebug.uiNumImposterAtlas);
		DYNDEBUGWRITELINE(cNumFx);
		sprintf(cNumFx, "Num Imposter Sections Used: %d", gfxImposterDebug.uiNumImposters);
		DYNDEBUGWRITELINE(cNumFx);
		{
			U32 uiLevel;
			U32 uiMaxLODSlots = 0;
			sprintf(cNumFx, "Num Bones Updated: %d   ", uiTotalBonesUpdated);
			for (uiLevel=0; uiLevel < pLODSettings->uiNumLODLevels; ++uiLevel)
				strcatf(cNumFx, "L%d: %d  ", uiLevel, dynDebugState.uiNumBonesUpdated[uiLevel]);
			DYNDEBUGWRITELINE(cNumFx);
		}
		sprintf(cNumFx, "Num Bones Animated: %d Skinned: %d", dynDebugState.uiNumAnimatedBones, dynDebugState.uiNumSkinnedBones);
		DYNDEBUGWRITELINE(cNumFx);
		sprintf(cNumFx, "Anim Track Cache Size (KB): %d / %d", dynDebugState.uiAnimCacheSizeUsed, dynDebugState.uiAnimCacheSize);
		DYNDEBUGWRITELINE(cNumFx);
		{
			F32 fAverage = 0.0f;
			for (i=0; i<16; ++i)
				fAverage += dynDebugState.uiNumSeqDataCacheMisses[i];
			fAverage /= 16.0f;
			sprintf(cNumFx, "Average SeqData Cache Misses: %.2f", fAverage);
			DYNDEBUGWRITELINE(cNumFx);
		}
		sprintf(cNumFx, "Extra Animation Threads: %d", dynDebugState.iNumAnimThreads);
		DYNDEBUGWRITELINE(cNumFx);
	}

	if (dynDebugState.bPrintBoneUnderMouse)
	{
		int xp, yp;
		char cBoneInfo[1024];
		int boneColors[4];
		int childColors[4];
		int parentColors[4];
		U32 uiChildCount = 0;
		bool bNodeAttached = false;
		const DynNode* pChild = NULL;
		const DynNode* pParent = NULL;
		F32 fScaleOffset = 0.f; // sinf((F32)gfx_state.frame_count * 0.2f) * 0.1f; // this made the bone names hard to read
		setVec4same(boneColors, 0x00FF00FF);
		setVec4same(childColors, 0x00FFFFFF);
		setVec4same(parentColors, 0xFF00FFFF);
		mousePos(&xp, &yp);
		if (dynSkeletonIsNodeAttached(dynDebugState.pDebugSkeleton, dynDebugState.pBoneUnderMouse)) {
			bNodeAttached = true;
			pChild  = dynDebugState.pBoneUnderMouse->pChild;
			pParent = dynDebugState.pBoneUnderMouse->pParent;
		}
		if (pParent)
		{
			sprintf(cBoneInfo, "%s", pParent->pcTag?pParent->pcTag:"No Name");
			gfxfont_PrintEx(&g_font_Sans, xp + 20, yp - 10, 0, 1.1, 1.1, 0, cBoneInfo, (int)strlen(cBoneInfo), parentColors, NULL);
			sprintf(cBoneInfo, "|_");
			gfxfont_PrintEx(&g_font_Sans, xp + 30, yp + 10, 0, 1.1, 1.1, 0, cBoneInfo, (int)strlen(cBoneInfo), boneColors, NULL);
		}

		sprintf(cBoneInfo, "%s", (bNodeAttached && dynDebugState.pBoneUnderMouse->pcTag)?dynDebugState.pBoneUnderMouse->pcTag:"None");
		gfxfont_PrintEx(&g_font_Sans, xp + 45, yp + 20, 0, 1.6+fScaleOffset, 1.6+fScaleOffset, 0, cBoneInfo, (int)strlen(cBoneInfo), boneColors, NULL);
		if (bNodeAttached)
		{
			Vec3 vBonePos;
			size_t iNameLen = strlen(cBoneInfo);
			dynNodeGetLocalPosInline(dynDebugState.pBoneUnderMouse, vBonePos);
			sprintf(cBoneInfo, " -- ( %.2f,  %.2f,  %.2f )", vBonePos[0], vBonePos[1], vBonePos[2]);
			gfxfont_PrintEx(&g_font_Sans, xp + 65 + iNameLen * 15, yp + 20, 0, 1.6+fScaleOffset, 1.6+fScaleOffset, 0, cBoneInfo, (int)strlen(cBoneInfo), boneColors, NULL);
		}
		while (pChild)
		{
			sprintf(cBoneInfo, "|_");
			gfxfont_PrintEx(&g_font_Sans, xp + 55, yp + 40 + (15 * uiChildCount), 0, 1.1, 1.1, 0, cBoneInfo, (int)strlen(cBoneInfo), childColors, NULL);
			sprintf(cBoneInfo, "%s", pChild->pcTag?pChild->pcTag:"None");
			gfxfont_PrintEx(&g_font_Sans, xp + 68, yp + 47 + (15 * uiChildCount), 0, 1.1, 1.1, 0, cBoneInfo, (int)strlen(cBoneInfo), childColors, NULL);
			pChild = pChild->pSibling;
			++uiChildCount;
		}

	}

	gfx_state.debug.frame_counts.total_skeletons = dynSkeletonGetTotalCount();
	gfx_state.debug.frame_counts.drawn_skeletons = dynDebugState.frameCounters.uiNumDrawnVisualPassSkels;
	gfx_state.debug.frame_counts.drawn_skeleton_shadows = dynDebugState.frameCounters.uiNumDrawnShadowPassSkels;
}

// Enables postprocessing, outlining, depth of field, and shadows.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_CMDLINEORPUBLIC;
void comicShading(int on)
{
	rdr_state.echoShaderPreloadLog = 0; // This will certainly load a bunch of non-preloaded shader variations
	if (rdr_state.showDebugLoadingPixelShader==2)
		rdr_state.showDebugLoadingPixelShader = 0;
	if (on)
	{
		globCmdParse("shadows 1");
		globCmdParse("postprocessing 1");
		globCmdParse("outlining 1");
 		globCmdParse("dof 1");
		globCmdParse("ssao 1");
		globCmdParse("soft_particles 1");
		globCmdParse("bloomQuality 2");
		globCmdParse("msaa 4");
		globCmdParse("water 1");
	}
	else
	{
		globCmdParse("shadows 0");
		globCmdParse("postprocessing 0");
		globCmdParse("outlining 0");
 		globCmdParse("dof 0");
		globCmdParse("ssao 0");
		globCmdParse("soft_particles 0");
		globCmdParse("bloomQuality 0");
		globCmdParse("scattering 0");
		globCmdParse("msaa 0");
		globCmdParse("water 0");
	}
}

AUTO_COMMAND ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance, Debug);
void ati9800(int on)
{
	if (on)
	{
		useSM20(1);
		globCmdParse("disableF16s 1");
		globCmdParse("disableNP2 1");
		globCmdParse("disableF24DepthTexture 1");
		// implied: uberlighting 0 -maxLightsPerObject 2
		globCmdParse("dof 0");
		globCmdParse("water 0");
		globCmdParse("MaterialSetFeatures SM20");
		globCmdParse("disableMSAASurfaceTypes 1");
	}
}

void gfxDebugUpdateGrabbedFrameTimestamp(int index)
{
	gfx_state.debug.framegrab_timestamp[index] = gfx_state.frame_count;
}

void gfxDebugGrabFrame(int index)
{
#if !PLATFORM_CONSOLE // No easy way to get the backbuffer from last frame?
	gfx_state.debug.framegrab_discard_counter[index] = 0; // If this was set up to be freed, forget about it
	if (gfx_state.debug.framegrab_timestamp[index] == gfx_state.frame_count)
		return;
	if (1)
	{
		RdrSurface *surface = rdrGetPrimarySurface(gfx_state.currentDevice->rdr_device);
		U8 *data;
		int width = surface->width_nonthread, height = surface->height_nonthread;
		if (gfx_state.debug.framegrab_texture[index] == NULL || texWidth(gfx_state.debug.framegrab_texture[index]) != width || texHeight(gfx_state.debug.framegrab_texture[index]) != height)
		{
			if (gfx_state.debug.framegrab_texture[index])
				texGenFree(gfx_state.debug.framegrab_texture[index]);
			gfx_state.debug.framegrab_texture[index] = texGenNew(width, height, "Screen", TEXGEN_NORMAL, WL_FOR_UTIL);
		}
		if (gfx_state.anaglyph_hack) {
			if (gfx_state.debug.framegrab_texture2 == NULL || texWidth(gfx_state.debug.framegrab_texture2) != width || texHeight(gfx_state.debug.framegrab_texture2) != height)
			{
				if (gfx_state.debug.framegrab_texture2)
					texGenFree(gfx_state.debug.framegrab_texture2);
				if (gfx_state.debug.framegrab_texture3)
					texGenFree(gfx_state.debug.framegrab_texture3);
				gfx_state.debug.framegrab_texture2 = texGenNew(width, height, "Screen", TEXGEN_NORMAL, WL_FOR_UTIL);
				gfx_state.debug.framegrab_texture3 = texGenNew(width, height, "Screen", TEXGEN_NORMAL, WL_FOR_UTIL);
			}
		}
		if (0) // Waaaay to slow.
		{
			gfxSetActiveSurface(rdrGetPrimarySurface(gfx_state.currentDevice->rdr_device));
			gfxLockActiveDevice();
			data = rdrGetActiveSurfaceData(gfx_state.currentDevice->rdr_device, SURFDATA_RGB, width, height);
			texGenUpdate(gfx_state.debug.framegrab_texture[index], data, RTEX_2D, RTEX_BGR_U8, 1, true, false, false, false);
			free(data);
			gfxUnlockActiveDevice();
		} else if (1) {
			if (gfx_state.anaglyph_hack) {
				if (gfx_state.frame_count % 2) {
					BasicTexture *t = gfx_state.debug.framegrab_texture[index];
					gfx_state.debug.framegrab_texture[index] = gfx_state.debug.framegrab_texture3;
					gfx_state.debug.framegrab_texture3 = t;
					texGenUpdateFromWholeSurface(gfx_state.debug.framegrab_texture2, surface, SBUF_0);
				} else {
					texGenUpdateFromWholeSurface(gfx_state.debug.framegrab_texture[index], surface, SBUF_0);
				}
			} else {
				gfxSetActiveSurface(rdrGetPrimarySurface(gfx_state.currentDevice->rdr_device));
				gfxLockActiveDevice();
				texGenUpdateFromWholeSurface(gfx_state.debug.framegrab_texture[index], surface, SBUF_0);
				gfxUnlockActiveDevice();
			}
		}
	}
	gfxDebugUpdateGrabbedFrameTimestamp(index);
#endif
}

void gfxDebugShowGrabbedFrame(int index, U32 color, F32 effect_weight)
{
	if (gfx_state.anaglyph_hack) {
		display_sprite_effect_tex(gfx_state.debug.framegrab_texture3, 0, 0, 1, 1.f, 1.f, 0xE50000FF, 1, RdrSpriteEffect_Desaturate, 1.f);
		display_sprite_effect_tex(gfx_state.debug.framegrab_texture2, 0, 0, 0, 1.f, 1.f, 0x00FFFFFF, 0, RdrSpriteEffect_Desaturate, 1.f);
	} else if (gfx_state.debug.framegrab_texture[index]) {
		//display_sprite_tex(gfx_state.debug.framegrab_texture[index], 0, texHeight(gfx_state.debug.framegrab_texture[index]), 0, 1.f, -1.f, color);
		//display_sprite_tex(gfx_state.debug.framegrab_texture[index], 0, 0, 0, 1.f, 1.f, color);
		display_sprite_effect_tex(gfx_state.debug.framegrab_texture[index], 0, 0, 0, 1.f, 1.f, color, 0, RdrSpriteEffect_Desaturate, effect_weight);
	}
}

BasicTexture *gfxDebugGetGrabbedFrame(int index)
{
	return gfx_state.debug.framegrab_texture[index];
}


void gfxDebugDoneWithGrabbedFrame(int index)
{
	gfx_state.debug.framegrab_discard_counter[index] = 30; // Free this if not used in 30 frames
}


void gfxDebugCheckFrameGrab(void)
{
	int index;
	if (gfx_state.debug.framegrab_show) {
		U8 alpha = (U8)CLAMP(gfx_state.debug.framegrab_show * 255, 0, 255);
		index = CLAMP(ceilf(gfx_state.debug.framegrab_show)-1, 0, MAX_FRAMEGRAB_TEXTURES-1);
		gfxDebugShowGrabbedFrame(index, 0xFFFFFF00 | alpha, 0);
		gfx_state.debug.framegrab_discard_counter[index] = 0;
	} else if (gfx_state.anaglyph_hack) {
		gfxDebugShowGrabbedFrame(0, 0xFFFFFFFF, 0);
		gfx_state.debug.framegrab_discard_counter[0] = 0;
	}
	if (gfx_state.debug.framegrab_grabnextframe)
	{
		if (gfx_state.debug.framegrab_grabnextframe==1)
			gfx_state.debug.framegrab_grabnextframe = 0;
		gfxDebugGrabFrame(0);
	}
	for (index=0; index<MAX_FRAMEGRAB_TEXTURES; index++)
	{
		if (gfx_state.debug.framegrab_discard_counter[index])
		{
			gfx_state.debug.framegrab_discard_counter[index]--;
			if (!gfx_state.debug.framegrab_discard_counter[index]) {
				if (gfx_state.debug.framegrab_texture[index])
					texGenFree(gfx_state.debug.framegrab_texture[index]);
				gfx_state.debug.framegrab_texture[index] = NULL;
				if (index == 0)
				{
					if (gfx_state.debug.framegrab_texture2)
						texGenFree(gfx_state.debug.framegrab_texture2);
					gfx_state.debug.framegrab_texture2 = NULL;
					if (gfx_state.debug.framegrab_texture3)
						texGenFree(gfx_state.debug.framegrab_texture3);
					gfx_state.debug.framegrab_texture3 = NULL;
				}
			}
		}
	}
}

const char *gfxGetBottleneckString(int val)
{
	static const char *strs[] = {
		"GPU bound",
		"Misc",
		"Draw",
		"Queue",
		"QueueWorld",
		"Anim/FX",
		"Networking",
		"UI",
	};
	assert(val >= 0 && val < ARRAY_SIZE(strs));
	return strs[val];
}

int gfxGetBottleneck(void)
{
	F32 msPerTick = wlPerfGetMsPerTick();
	if (gfx_state.debug.last_frame_counts.world_perf_counts.time_wait_gpu * msPerTick > 1.0f) {
		// More than a ms of waiting, most likely GPU bound!
		return GfxBottleneck_GPUBound;
	} else {
		struct {
			GfxBottleneck name;
			F32 value;
		} parts[] = {
			{GfxBottleneck_Misc, gfx_state.debug.last_frame_counts.world_perf_counts.time_misc * msPerTick},
			{GfxBottleneck_Draw, gfx_state.debug.last_frame_counts.world_perf_counts.time_draw * msPerTick},
			{GfxBottleneck_Queue, gfx_state.debug.last_frame_counts.world_perf_counts.time_queue * msPerTick},
			{GfxBottleneck_QueueWorld, gfx_state.debug.last_frame_counts.world_perf_counts.time_queue_world * msPerTick},
			{GfxBottleneck_AnimFX, gfx_state.debug.last_frame_counts.world_perf_counts.time_anim * msPerTick},
			{GfxBottleneck_Networking, gfx_state.debug.last_frame_counts.world_perf_counts.time_net * msPerTick},
			{GfxBottleneck_UI, gfx_state.debug.last_frame_counts.world_perf_counts.time_ui * msPerTick},
		};
		int maxIndex=0;
		int i;
		for (i=1; i<ARRAY_SIZE(parts); i++)
			if (parts[i].value > parts[maxIndex].value)
				maxIndex = i;
		return parts[maxIndex].name;
	}
}

void gfxDummyFrameSequenceStart(GfxDummyFrameInfo * frame_info)
{
	frame_info->was_in_ui = world_perf_info.in_time_ui;
	frame_info->was_in_misc = world_perf_info.in_time_misc;
	frame_info->was_ignoringInput = inpIsIgnoringInput();

	if (world_perf_info.in_time_ui)
		wlPerfEndUIBudget();
	if (world_perf_info.in_time_misc)
		wlPerfEndMiscBudget();
}

void gfxDummyFrameSequenceEnd(GfxDummyFrameInfo * frame_info)
{
	if (frame_info->was_in_misc)
	{
		frame_info->was_in_misc = false;
		wlPerfStartMiscBudget();
	}
	if (frame_info->was_in_ui)
	{
		frame_info->was_in_ui = false;
		wlPerfStartUIBudget();
	}
}

void gfxDummyFrameTopEx(GfxDummyFrameInfo * frame_info, F32 elapsed, bool allow_offscreen_render)
{
	frame_info->preframe_assert_on_misnested_budgets = world_perf_info.assert_on_misnested_budgets;
	world_perf_info.assert_on_misnested_budgets = false;
	gfxResetFrameCounters();

	wlPerfStartMiscBudget();

	gfxOncePerFrame(elapsed, elapsed, false, allow_offscreen_render);
	gfxSetActiveDevice(gfxGetActiveOrPrimaryDevice());
	gfx_state.currentDevice->doNotSkipThisFrame = 1;
	inpUpdateEarly(gfxGetActiveInputDevice());
	gfxDisplayDebugInterface2D(true);
}

void gfxDummyFrameBottom(GfxDummyFrameInfo * frame_info, WorldRegion **regions, bool load_all)
{
	int i;
	Vec3 pos = {0,0,0};
	inpUpdateLate(gfxGetActiveInputDevice());
	gfxStartMainFrameAction(false, false, true, false, false);
	for( i=0; i < eaSize(&regions); i++ )
		worldGridOpenAllCellsForCameraPos(PARTITION_CLIENT, regions[i], pos, 1, NULL, load_all, false, gfx_state.settings.draw_high_detail, gfx_state.settings.draw_high_fill_detail, gfxGetFrameCount(), 1.0f);
	gfxFillDrawList(false, NULL);
	gfxDrawFrame();
	gfxProcessInputOnInactiveDevices();
	gfxOncePerFrameEnd(false);
	world_perf_info.assert_on_misnested_budgets = frame_info->preframe_assert_on_misnested_budgets;
	worldGridClearTempTrackers();

	wlPerfEndMiscBudget();
}

void gfxShowPleaseWaitMessage(const char *message) // For slow things like /reloadshaders
{
	if (!gfx_state.debug.no_please_wait && !gfx_state.currentAction && gfx_state.currentDevice)
	{
		int i;
		GfxDummyFrameInfo frame_loop_info = { 0 };

		gfxDummyFrameSequenceStart(&frame_loop_info);
		gfxDebugGrabFrame(1);
		for (i=0; i<2; i++) {
			gfxDummyFrameTop(&frame_loop_info, 0.0f);
			//gfxDebugShowGrabbedFrame(0x7f7f7f7fFF);
			gfxDebugShowGrabbedFrame(1, 0xFFFFFFFF, 0.9);
			gfxfont_SetFontEx(&g_font_Sans, 0, 0, 1, 0, 0xffffffFF, 0xAfAfAfFF);
			gfxfont_Print(gfx_state.screenSize[0]/2, gfx_state.screenSize[1]/2, GRAPHICSLIB_Z+2, 2, 2, CENTER_XY, message);
			gfxDummyFrameBottom(&frame_loop_info, NULL, false);
		}
		gfxDebugUpdateGrabbedFrameTimestamp(1);
		gfxDebugDoneWithGrabbedFrame(1);
		gfxDummyFrameSequenceEnd(&frame_loop_info);
	}
}

void gfxDebugViewport(IVec2 viewportMin, IVec2 viewportMax)
{
	int width, height;
	gfxGetActiveDeviceSize(&width, &height);
	gfx_state.debug.offsetFromTop = viewportMin[1];
	gfx_state.debug.offsetFromRight = width - viewportMax[0];
	gfx_state.debug.offsetFromLeft = viewportMin[0];
	gfx_state.debug.offsetFromBottom = height - viewportMax[1];
	
#if PLATFORM_CONSOLE
	gfx_state.debug.offsetFromRight += 56;
	gfx_state.debug.offsetFromLeft += 20;
	gfx_state.debug.offsetFromBottom += 18;
	gfx_state.debug.offsetFromTop += 24;
#else
	if (rdr_state.usingNVPerfHUD)
		gfx_state.debug.offsetFromBottom += 14;
	if (rdr_state.usingNVPerfHUD && gfx_state.debug.offsetFromTop==0)
		gfx_state.debug.offsetFromTop += 16;
#endif
}

static int g_gfx_errorf_delay=2; // Start with one frame to allow loading to material preloading to happen first
void gfxSetErrorfDelay(int delayFrames)
{
	g_gfx_errorf_delay = delayFrames;
}

bool gfxDelayingErrorf(void)
{
	if (g_gfx_errorf_delay)
	{
		g_gfx_errorf_delay--;
		return true;
	} else {
		return false;
	}

}

const char *DEFAULT_LATELINK_GetGameServerIDAndPartitionString(void)
{
	return "unknowable";
}

void gfxModelSetLODOffset(int lod_offset)
{
	gfx_state.debug.model_lod_offset = CLAMP(lod_offset, -4, 4);
}

int gfxModelGetLODOffset()
{
	return gfx_state.debug.model_lod_offset;
}

void gfxModelSetLODForce(int lod_force)
{
	gfx_state.debug.model_lod_force = CLAMP(lod_force, 0, 4);
}

int gfxModelGetLODForce()
{
	return gfx_state.debug.model_lod_force;
}

// CommandScriptInterpreter and associated csiXXXXXX functions, and several auto-commands, implement an 
// interactive interpreter for auto-command-based scripts. The interpreter executes an input 
// script over a number of engine  frames, in a manner somewhat analogous to delayedCmd. 
// The interpreter stops execution when after the last command, or if any command returns a
// failure, such as invalid arguments.
// The command rate is gated by the nextCmdDelay setting.
// See the following auto-commands to use the interpreter in the client:
// csiExecFile (filename), csiExecStop, csiExecPause, csiExecResume, csiEcho.

typedef struct CommandScriptInterpreter
{
	// The allocated memory block of text data. This may have newlines separating commands 
	// on disk, but NULLs in memory due to use of strtok to split commands into an EArray.
	char * scriptFileRawData;
	// The auto-command array, one command per array entry. References memory from 
	// scriptFileRawData but does not own the memory.
	char ** eaCommands;
	// The current command or "instruction pointer" of the script. This is the command that
	// will be executed on the next tick.
	int iCurrCmd;
	// Indicates if the script is currently executing.
	bool bRunning;
	// Option to display each command in the graphics console before it is executed.
	bool bEchoOn;
	// The number of seconds of real time until the interpreter will execute another command.
	// When this timer expires, the interpreter will use the current state of nextCmdDelay to
	// restart the timer. This decoupling allows the command-rate to vary during script execution.
	float nextTickTimer;
	// The setting of the number of seconds between each command execution, which is independent
	// of the current value of the timer, nextTimeTimer.
	float nextCmdDelay;
} CommandScriptInterpreter;

void ignoreEArrayEntry(void* pIgnored)
{
}

// Terminates script execution and cleans up memory.
void csiReset(CommandScriptInterpreter *pCSI)
{
	eaDestroyEx(&pCSI->eaCommands, ignoreEArrayEntry);
	pCSI->iCurrCmd = 0;
	pCSI->bRunning = false;
	pCSI->nextCmdDelay = 1.0f;
	pCSI->nextTickTimer = 0.0f;
	if (pCSI->scriptFileRawData)
	{
		fileFree(pCSI->scriptFileRawData);
		pCSI->scriptFileRawData = NULL;
	}
}

bool csiIsRunning(const CommandScriptInterpreter *pCSI)
{
	return eaSize(&pCSI->eaCommands) > 0;
}

void csiRun(CommandScriptInterpreter *pCSI, int iIterations)
{
	char *estrReturnString = NULL;
	for (; iIterations; --iIterations)
	{
		if (pCSI->bEchoOn)
			conPrintf("CSI: %d %s", pCSI->iCurrCmd, pCSI->eaCommands[pCSI->iCurrCmd]);
		if (!globCmdParseAndReturn(pCSI->eaCommands[pCSI->iCurrCmd], &estrReturnString, 0, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL))
		{
			// emit error
			conPrintf("CSI terminated; command failed: %s\n%s", 
				pCSI->eaCommands[pCSI->iCurrCmd], estrReturnString);
			// alert to main window
			gfxStatusPrintf("CSI terminated; command failed: %s\n%s", 
				pCSI->eaCommands[pCSI->iCurrCmd], estrReturnString);
			// terminate script
			estrDestroy(&estrReturnString);
			csiReset(pCSI);
			break;
		}
		if (pCSI->bEchoOn && estrLength(&estrReturnString))
			conPrintf("CSI: %s", estrReturnString);

		++pCSI->iCurrCmd;
		if (pCSI->iCurrCmd >= eaSize(&pCSI->eaCommands))
		{
			// Done!
			csiReset(pCSI);
			break;
		}
	}
}

void csiPause(CommandScriptInterpreter *pCSI)
{
	pCSI->bRunning = false;
}

void csiResume(CommandScriptInterpreter *pCSI)
{
	pCSI->bRunning = true;
}

void csiSetEcho(CommandScriptInterpreter *pCSI, bool bEchoOn)
{
	pCSI->bEchoOn = bEchoOn;
}

void csiTick(CommandScriptInterpreter *pCSI, F32 fDeltaTime)
{
	int iNumCmds = 0;
	if (!pCSI->bRunning)
		return;

	pCSI->nextTickTimer -= fDeltaTime;
	if (pCSI->nextTickTimer > 0.0f)
		return;

	iNumCmds = 1 - pCSI->nextTickTimer / pCSI->nextCmdDelay;
	csiRun(pCSI, iNumCmds);

	pCSI->nextTickTimer += pCSI->nextCmdDelay;
}

// Executes each line in a file as a slash command, in an interpreted state,
// over multiple frames.
int csiSetupScript(CommandScriptInterpreter *pCSI, const char *filename)
{
	int lenp;
	char *data = fileAlloc(filename, &lenp);
	char *str;
	char *last;
	bool found_cmd=true;
	bool good_args=true;

	csiReset(pCSI);

	if (!data)
	{
		if (!strEndsWith(filename, ".txt")){
			char buffer[MAX_PATH];
			sprintf(buffer, "%s.txt", filename);
			data = fileAlloc(buffer, &lenp);
		}
		if (!data)
			return 0;
	}

	for(str = strtok_r(data, "\r\n", &last);
		str;
		str = strtok_r(NULL, "\r\n", &last))
	{
		const char* cmd_str = removeLeadingWhiteSpaces(str);

		if(	cmd_str[0] == '#'
			||
			cmd_str[0] == ';'
			||
			cmd_str[0] == '/' &&
			cmd_str[1] == '/')
		{
			continue;
		}
		eaPush(&pCSI->eaCommands, (char*)cmd_str);
	}

	return 1;
}


CommandScriptInterpreter clientDebugScriptInterpreter;

AUTO_CMD_FLOAT(clientDebugScriptInterpreter.nextCmdDelay, csiCmdTime) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(DEBUG) ACMD_HIDE;

// Executes each line in a file as a slash command, in an interpreted state,
// over multiple frames.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(DEBUG) ACMD_HIDE;
void csiExecFile(const char *filename)
{
	if (csiSetupScript(&clientDebugScriptInterpreter, filename))
		csiResume(&clientDebugScriptInterpreter);
	else
		gfxStatusPrintf("CSI couldn't open or parse \"%s\".", filename);
}

// Terminates any running script.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(DEBUG) ACMD_HIDE;
void csiExecStop()
{
	csiReset(&clientDebugScriptInterpreter);
}

// Pauses a running script.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(DEBUG) ACMD_HIDE;
void csiExecPause()
{
	csiPause(&clientDebugScriptInterpreter);
}

// Resumes a running script.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(DEBUG) ACMD_HIDE;
void csiExecResume()
{
	csiResume(&clientDebugScriptInterpreter);
}

// Pauses a running script.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(DEBUG) ACMD_HIDE;
void csiEcho(int bOn)
{
	csiSetEcho(&clientDebugScriptInterpreter, !!bOn);
}

void csiOncePerFrame(F32 fDeltaTime)
{
	csiTick(&clientDebugScriptInterpreter, fDeltaTime);
}

void gfxDebugOncePerFrame(F32 fDeltaTime)
{
	csiOncePerFrame(fDeltaTime);
}