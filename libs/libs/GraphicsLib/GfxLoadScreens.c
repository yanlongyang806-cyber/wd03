#include "GfxLoadScreens.h"
#include "Materials.h"
#include "wlModel.h"
#include "RdrShader.h"
#include "utilitiesLib.h"
#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxSprite.h"
#include "file.h"
#include "GfxSpriteText.h"
#include "GfxDebug.h"
#include "GraphicsLib.h"
#include "Message.h"
#include "ThreadManager.h"
#include "ContinuousBuilderSupport.h"
#include "ControllerScriptingSupport.h"

int g_NoLoadScreen = 0;

// No loading screen, so you can see the world while it loads
// (You may need to type "showgameui 0" as well)
AUTO_CMD_INT(g_NoLoadScreen, NoLoadScreen);

static BasicTexture *loadingData_splashScreen;
static const char *loadingData_message;
static int loadingData_timer;
static U32 loadingData_framecount;
static int loadingData_isWaiting=0;
static int loaded_DataCount;
static bool queue_unload = false;

void gfxLoadUpdate(int num_bytes)
{
	loaded_DataCount += num_bytes;
	// Called from multiple threads
}

void gfxLoadingStartWaiting(void)
{
	// loading implies discontinuity in light history
	// so ignore all prior lights
	gfxClearCurrentDeviceShadowmapLightHistory();

	if (!loadingData_isWaiting) {
		texDynamicUnload(TEXUNLOAD_DISABLE); // Do not do unloads while at the loading screen

		assert(!loadingData_timer);

		queue_unload = true;
		loadingData_framecount = 0;
		loadingData_timer = timerAlloc();
	}
	loadingData_isWaiting++;
	loaded_DataCount = 0;
}

void gfxLoadingFinishWaiting(void)
{
	assert(tmIsMainThread());
	assertmsg(loadingData_isWaiting>0, "Someone called gfxLoadingFinishWaiting without a matched gfxLoadingStartWating()");
	loadingData_isWaiting--;
	if (!loadingData_isWaiting) {
		timerFree(loadingData_timer);
		loadingData_timer = 0;
		texDynamicUnload(TEXUNLOAD_ENABLE_DEFAULT);
		materialDataReleaseAll();
		gfxResetInterFrameCounts();
	}
	assert(loadingData_isWaiting>=0);
	//printf("Total load count is %d\n", loaded_DataCount);
	loaded_DataCount = 0;
}

bool gfxLoadingIsWaiting(void)
{
    return (loadingData_isWaiting > 0);
}

void gfxLoadingSetLoadingMessage(SA_PARAM_OP_STR const char *message)
{
	loadingData_message = message;
}

int gfxLoadingGetFinishedLoadCount(void)
{
	return loaded_DataCount;
}

void gfxLoadingDisplayScreen(bool displayBackground)
{
	int texLoadsTotal = texLoadsPending(1);
	int geoLoadsTotal = geoLoadsPending(1);
	int geoLoads = geoLoadsPending(0);
	int shaderLoads = rdrShaderGetBackgroundShaderCompileCount() + gfxMaterialPreloadGetLoadingCount();
	static int lastShaderLoads = 0;
	int miscLoads = utilitiesLibLoadsPending();
	int width, height;
	gfxGetActiveSurfaceSize(&width, &height);

	if (!loadingData_splashScreen)
	{
		gfxLockActiveDevice();
		loadingData_splashScreen = texLoadBasic("loadscreen_default", TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD, WL_FOR_UI);
		gfxUnlockActiveDevice();
	}

	if (displayBackground)
	{
		// Keep initial image aspect ratio
		F32 texwidth = (F32)texWidth(loadingData_splashScreen);
		F32 texheight = (F32)texHeight(loadingData_splashScreen);
		F32 texAspect = texwidth / texheight;
		F32 screenAspect = gfxGetAspectRatio(); // not necessarily the same as width/height
		F32 effwidth = width;
		F32 effheight = height;
		F32 xscale, yscale;
		F32 xpos=0, ypos=0;
		if (screenAspect > texAspect)
		{
			effwidth = effwidth * texAspect / screenAspect;
			xpos = (width - effwidth) / 2;
		} else {
			effheight = effheight * screenAspect / texAspect;
			ypos = (height - effheight) / 2;
		}

		xscale = effwidth / texwidth;
		yscale = effheight / texheight;

		if (!g_NoLoadScreen)
		{
			// Black background
			display_sprite_tex(white_tex, 0, 0, -1, width / (F32)texWidth(white_tex),
				height / (F32)texHeight(white_tex), 0x000000FF);
			// Loading image
			display_sprite_tex(loadingData_splashScreen, xpos, ypos, 0, xscale,
				yscale, 0xFFFFFFFF);
		}

		gfxfont_SetFontEx(&g_font_Game, false, false, 1, 0, 0xFFFFFFFF, 0xFFFFFFFF);
		gfxfont_Print(width / 2, ypos + effheight - 8, 0.1, 2, 2, CENTER_X,
			TranslateMessageKeyDefault("Loading", "Loading..."));
	}

	if (isDevelopmentMode())
	{
		int y=55;
		U32 color = 0x9F9F9FFF;

		gfxfont_SetFontEx(&g_font_Mono, 0, 0, 1, 0, 0x9F9F9FFF, 0x9F9F9FFF);
		gfxXYprintf(5, TEXT_JUSTIFY + y++, "Loading Textures: %d", texLoadsTotal);
		gfxXYprintf(5, TEXT_JUSTIFY + y++, "Loading Geometry: %d", geoLoads);
		gfxXYprintf(5, TEXT_JUSTIFY + y++, "Loading Misc: %d", geoLoadsTotal - geoLoads + miscLoads);
		gfxXYprintf(5, TEXT_JUSTIFY + y++, "Loading Shaders: %d", shaderLoads);

		if (loadingData_message) {
			gfxfont_Printf(width * 0.5, height - 0.f, GRAPHICSLIB_Z, 1.f, 1.f, CENTER_X, "%s", loadingData_message);
		}
	}

	if (g_isContinuousBuilder)
	{
		if (lastShaderLoads && shaderLoads && lastShaderLoads != shaderLoads)
		{
			ControllerScript_TemporaryPause(10, "Shader loads");
		}

		lastShaderLoads = shaderLoads;
	}

	// once we've rendered the loading screen for a frame, drop all the old textures
	if (queue_unload) {
		texUnloadAllNotUsedThisFrame();
		queue_unload = false;
	}
}

bool gfxLoadingIsStillLoading(void)
{
    return gfxLoadingIsStillLoadingEx(0.25f, 10, 0, true);
}

bool gfxLoadingIsStillLoadingEx(float minElapsedTime, unsigned minElapsedFrame, int loadThreshold, bool includePatching)
{
	loadingData_framecount++;

	if (gfxNumLoadsPending(includePatching) <= loadThreshold)
	{
		if(	!loadingData_timer
			|| 
			timerElapsed(loadingData_timer) > minElapsedTime &&
			loadingData_framecount > minElapsedFrame)
		{
			return false;
		}
		// else, wait for time to pass
		return true;
	}
	else
	{
		// Some data is still loading
		if(loadingData_timer)
		{
			timerStart(loadingData_timer);
		}
		loadingData_framecount = 0;
		return true;
	}
}

bool gfxIsLoadingForContact(F32 fMinTimeSinceLoad) {

	static F32 fLastTimeWasLoading = 0;
	F32 fTime = gfx_state.client_loop_timer;

	if(gfxIsStillLoading(false)) {
		fLastTimeWasLoading = fTime;
		return true;
	}

	if(fLastTimeWasLoading > fTime || fTime - fLastTimeWasLoading >= (fMinTimeSinceLoad * 2.0f) + 2.0f) {
		
		// Been too long. Just let it think we're loading until we actually do start loading.
		fLastTimeWasLoading = fTime;
		
		return true;
	}

	if(fTime - fLastTimeWasLoading >= fMinTimeSinceLoad) {
		
		// Longer than the threshold since the last load.
		// Keep fLastTimeWasLoading trailing this point.
		fLastTimeWasLoading = fTime - fMinTimeSinceLoad;

		return false;
	}

	return true;

}


