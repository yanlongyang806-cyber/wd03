#include "GraphicsLib.h"
#include "GfxMaterials.h"
#include "GfxMaterialPreload.h"
#include "GfxTexAtlasPrivate.h"
#include "GfxTexAtlas.h"
#include "GfxDynamics.h"
#include "GfxDebug.h"
#include "GfxModel.h"
#include "GfxMapSnap.h"
#include "GfxGeo.h"
#include "GfxSky.h"
#include "GfxCommandParse.h"
#include "GfxOcclusion.h"
#include "GfxUnload.h"
#include "GfxFontPrivate.h"
#include "GfxMaterialProfile.h"
#include "GfxPostprocess.h"
#include "GfxLoadScreens.h"
#include "GfxWorld.h"
#include "GfxPrimitive.h"
#include "GfxConsole.h"
#include "GfxDeferredShadows.h"
#include "GfxTextureTools.h"
#include "GfxHeadshot.h"
#include "GfxSurface.h"
#include "GfxDrawFrame.h"
#include "GfxLights.h"
#include "GfxLightCache.h"
#include "GfxLCD.h"
#include "GfxAlien.h"
#include "GfxNVPerf.h"
#include "GlobalStateMachine.h"
#include "texWords.h"
#include "texUnload.h"
#include "TexOpts.h"
#include "tex_gen.h"
#include "GfxShadows.h"
#include "GfxSplat.h"
#include "GfxImposter.h"
#include "GfxPrimitivePrivate.h"
#include "GfxTexturesInline.h"
#include "GfxFont.h"
#include "GfxSpriteList.h"
#include "GfxSprite.h"
#include "GfxTerrain.h"
#include "GfxSettings.h"

#include "RdrShader.h"
#include "RdrState.h"
#include "RdrTexture.h"
#include "RdrFMV.h"

#include "inputLib.h"

#include "WorldLib.h"
#include "wlTime.h"
#include "Materials.h"
#include "wlAutoLOD.h"
#include "dynDraw.h"

#include "MemoryMonitor.h"
#include "sysutil.h"
#include "EventTimingLog.h"
#include "crypt.h"
#include "rgb_hsv.h"
#include "utilitiesLib.h"
#include "MemoryPool.h"
#include "memlog.h"
#include "jpeg.h"
#include "FolderCache.h"
#include "structHist.h"
#include "trivia.h"
#include "MemReport.h"
#include "timing_profiler_interface.h"
#include "fileLoader.h"
#include "MemRef.h"
#include "wlPerf.h"
#include "bounds.h"
#include "TimedCallback.h"
#include "DynamicCache.h"

#include "AutoGen/GraphicsLib_h_ast.c"
#include "AutoGen/GfxEnums_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

GfxState gfx_state;
void gfxNotifyDeviceLost( RdrDevice* device );
void gfxDisplayParamsChanged( RdrDevice * device );
static void gfxFMVUpdateParams(void);

void gfxSetClusterState(bool cluster_state)
{
	gfx_state.cluster_load = cluster_state;
}

bool gfxGetClusterState(void)
{
	return gfx_state.cluster_load;
}

void assertBeforeGfxLibStartupVoid(void)
{
	assertmsg(0, "Calling a function that needs to be reflected in GraphicsLib before calling gfxStartup().");
}

#define PERF_TEST_ENABLE_TRACE 1
#if PERF_TEST_ENABLE_TRACE
void gfxTracePerfTest(const char * format, ...)
{
	va_list va;
	va_start(va, format);
	OutputDebugStringv(format, va);
	va_end(va);
}
#else
#define gfxTracePerfTest(fmt, ...)
#endif

AUTO_RUN;
int gfxAutoStartup(void)
{
	// Happens before getting into main()  CANNOT call any file access functions here!
	modelLODSetDestroyCallback((Destructor)assertBeforeGfxLibStartupVoid);
	modelSetFreeAllCallback((ModelFreeAllCallback)assertBeforeGfxLibStartupVoid);
	modelSetReloadCallback((ModelReloadCallback)assertBeforeGfxLibStartupVoid);
	g_texture_name_fixup = texFixName;
#define DEFAULT_EDITOR_VISSCALE_FACTOR 0.5f
	gfx_state.editorVisScale = DEFAULT_EDITOR_VISSCALE_FACTOR;
	gfx_state.debug.model_lod_force = -1;

	return 1;
}

void gfxPretendLibIsNotHere(void)
{
	modelLODSetDestroyCallback(NULL);
	modelSetFreeAllCallback(NULL);
	modelSetReloadCallback(NULL);
}

static void gfxDrawModelWorldLibWrapper(Model* model,
	const Mat4 mat)
{
	SingleModelParams smparams = {0};
	smparams.model = model;
	copyMat4(mat, smparams.world_mat);
	smparams.dist = -1;
	smparams.wireframe = gfx_state.wireframe;
	gfxQueueSingleModel(&smparams);
}

void gfxSetHideDetailInEditorBit(U8 val)
{
	gfx_state.hide_detail_in_editor = val;
}

U8 gfxGetHideDetailInEditorBit(void)
{
	return gfx_state.hide_detail_in_editor;
}

bool gfxGetSoftwareCursorForce(void)
{
	return gfx_state.settings.softwareCursor;
}

bool gfxGetDisableVolumeSkies(void) {
	return gfx_state.debug.disable_sky_volumes;
}

bool gfxStereoscopicActive(void)
{
	return gfx_state.currentDevice ? gfx_state.currentDevice->rdr_device->display_nonthread.stereoscopicActive : false;
}

void gfxSetDisableVolumeSkies(bool bValue) {
	gfx_state.debug.disable_sky_volumes = bValue;
}

bool gfxGetCurrentActionAllowIndoors(void) {
	return gfx_state.currentAction ? gfx_state.currentAction->allow_indoors : false;
}

void gfxSetCurrentActionAllowIndoors(bool bAllowIndoors) {
	if(gfx_state.currentAction) {
		gfx_state.currentAction->allow_indoors = bAllowIndoors;
	}
}

static void gfxShellExecute(HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd )
{
	rdrShellExecute(gfx_state.currentDevice->rdr_device, hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd, NULL);
}

static void gfxShellExecuteWithCallback(HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd, ShellExecuteCallback callback)
{
	rdrShellExecute(gfx_state.currentDevice->rdr_device, hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd, callback);
}

#define VIRTUAL_ADDRESS_DEFAULT_32_BIT_OS (2147418134)
#define VIRTUAL_ADDRESS_3GBVA_BOOT_OPTION_32_BIT_OS (3*1024*1024*1024UL)

static bool bDidPreWorldLibStartup = false;
AUTO_STARTUP(GraphicsLibEarly);
void gfxStartupPreWorldLib(void)
{
	bDidPreWorldLibStartup = true;

	// We check the virtual address size, not whether or not the OS is 32- or 64-bit, because 
	// if the client is not large-address-aware, it will not have a 4GB address space. Also,
	// with special boot options, the 32-bit OS may support a 3GB client address space.
	if (!tex_memory_allowed)
	{
		if (system_specs.virtualAddressSpace <= VIRTUAL_ADDRESS_DEFAULT_32_BIT_OS)
			tex_memory_allowed = DEFAULT_TEX_MEMORY_ALLOWED_32_BIT_OS;
		else
		if (system_specs.virtualAddressSpace <= VIRTUAL_ADDRESS_3GBVA_BOOT_OPTION_32_BIT_OS)
			tex_memory_allowed = DEFAULT_TEX_MEMORY_ALLOWED_3GBVA_BOOT_OPTION_32_BIT_OS;
		else
			tex_memory_allowed = DEFAULT_TEX_MEMORY_ALLOWED_64_BIT_OS;
	}

	loadstart_printf("GraphicsLib pre-WorldLib startup...");

	checkForCoreData();

	utilitiesLibStartup();

	cryptMD5Init(); // Before texWordsStartup (should move outside somewhere?)

	if(!gbNoGraphics)
	{
		texoptLoad(); // Loads TexOpts
		gfxLoadTextures(); // Loads headers of individual texture files

		//load the fonts, this needs to be after texures are loaded since the fonts use textures
		//but before texwords since it uses fonts
		gfxFontLoadFonts(); 

		// After loading textures, (re-calls texWordFind)
		texWordsStartup();
	}

	worldLibSetMaterialFunctions(texFindAndFlag, texGetName, texGetFullname, texIsNormalmap, texIsDXT5nm, texIsCubemap, texIsVolume, texIsAlphaBordered, NULL, NULL, NULL, NULL,  // Last 4 not available yet
		gfxMaterialValidateForFx, gfxMaterialsInitMaterial);

	materialAddCallbacks(NULL, NULL, NULL, NULL, gfxMaterialValidateMaterialData); // First ones not available yet

	utilitiesLibSetShellExecuteFunc(gfxShellExecute, gfxShellExecuteWithCallback);

	gfxLightsStartup();

	gfxSkyLoadSkies(); // needed for Object Library validation

	loadend_printf("GraphicsLib pre-WorldLib startup done.");
}

AUTO_RUN_EARLY; // Early so that a command line flag can disable them
void gfxInitGlobalSettings(void)
{
	gfx_state.shadow_buffer = 1; // Gets disabled by command line or low-end cards
	gfx_state.debug.sort_opaque = 1;
	if (showDevUI()) {
		gfx_state.showfps = 1;
		gfx_state.showCamPos = 1;
	}
	gfx_state.debug.onepassDistance = 50.f;
	gfx_state.debug.onepassScreenSpace = 0.001f;
	gfx_state.cclighting = 1; // Gets disabled by command line or low-end cards
	gfx_state.texLoadNearCamFocus = 1;

	if (getWineVersion())
	{
		gfx_state.forceOffScreenRendering = 1;
	}
}

// Called in an AUTO_RUN_EARLY from the GameClient projects only, not utilities like AssetManager/GetVRML
void gfxInitGlobalSettingsForGameClient(void)
{
	gfx_state.allow_preload_materials = 1;
}

void gfxSetFeatures(GfxFeature features_allowed)
{
	gfx_state.features_allowed = features_allowed;
}
GfxFeature gfxGetFeatures(void)
{
	return gfx_state.features_allowed;
}

bool gfxGetShadowBufferEnabled(void)
{
	return gfx_state.shadow_buffer;
}


void gfxMapLoadBeginendCallback(bool beginning)
{
	if (beginning)
		gfxNoErrorOnNonPreloadedInternal(true);
	else
		gfxNoErrorOnNonPreloadedInternal(false);
}

AUTO_STARTUP(GraphicsLib) ASTRT_DEPS(WorldLibMain, GraphicsLibEarly);
void gfxStartup(void)
{
	assert(bDidPreWorldLibStartup);

	loadstart_printf("GraphicsLib Startup...");

	if (!(wlGetLoadFlags() & WL_NO_LOAD_MATERIALS)) {
		gfxLoadMaterialAssemblerProfiles();
		gfxLoadMaterials(); // Loads GraphicsLib side of material definitions
		worldLibSetMaterialFunctions(texFindAndFlag, texGetName, texGetFullname, texIsNormalmap, texIsDXT5nm, texIsCubemap, texIsVolume, texIsAlphaBordered, gfxMaterialCanOcclude, gfxMaterialCheckSwaps, gfxMaterialApplySwaps, gfxMaterialDrawFixup,
			gfxMaterialValidateForFx, gfxMaterialsInitMaterial);
	}

	modelLODSetDestroyCallback(gfxModelLODDestroyCallback);
	modelSetFreeAllCallback(gfxModelFreeAllCallback);
	modelSetReloadCallback(gfxModelReloadCallback);

	if(gbNoGraphics)
	{
		loadend_printf("GraphicsLib startup done (NO GRAPHICS).");
		return;
	}

	texMakeWhite();

	if (!gbNo3DGraphics)
		gfxSkyValidateSkies(); // Object Library must be loaded for validation

	rdrSetStatusPrintf(gfxStatusPrintf);
	wlSetStatusPrintf(gfxStatusPrintf);
	wlSetLoadUpdateFunc(gfxLoadUpdate);
	wlSetDrawLine3D_2Func(gfxDrawLine3D_2ARGB);
	wlSetDrawAxesFromTransformFunc(gfxDrawAxesFromTransform);
	wlSetDrawBox3DFunc(gfxDrawBox3DARGB);
	wlSetDrawModelFunc(gfxDrawModelWorldLibWrapper);
	rdrSetFillShaderCallback(gfxMaterialFillShader);
	rdrSetSkinningMatDecFunc(dynSkinningMatSetDecrementRefCount);
	wlSetGfxSplatDestroyCallback(gfxDestroySplat);
	wlSetGfxBodysockTextureFuncs(gfxImposterAtlasGetBodysockTexture, gfxImposterAtlasReleaseBodysockTexture);
	wlSetGfxTakeMapPhotosFuncs(gfxAddMapPhoto, gfxTakeMapPhotos, gfxMapPhotoRegister, gfxMapPhotoUnregister, gfxUpdateMapPhoto, gfxDownRezMapPhoto);
	worldLibSetLoadMapBeginEndCallback(gfxMapLoadBeginendCallback);
	wlSetRdrMaterialHasTransparency(gfxMaterialHasTransparency);
	wlSetWorldCellGfxDataType(parse_WorldCellGraphicsData);

	wlSetRdrMaterialHasTransparency(gfxMaterialHasTransparency);
	wlSetMaterialGetTextures(gfxMaterialGetTextures);
	wlSetGfxTextureFuncs(gfxSaveTextureAsPNG);

	gfxSettingsLoadConfig();
	gfxModelStartup();
	gfxDynamicsStartup();

	systemSpecsInit();
	gfxSettingsLoadMinimal();

	gfxLCDInit();
	gfxAlienInit();

	worldLibSetCheckVisibiltyFunc(gfxSkeletonIsVisible, gfxParticleIsVisible);

	worldLibSetGfxModelFuncs(gfxDemandLoadModelLODCheck);

	worldLibSetLightFunctions(	gfxUpdateLight, gfxRemoveLight, 
		gfxUpdateAmbientLight, gfxRemoveAmbientLight, 
		gfxUpdateRoomLightOwnership);

	worldLibSetLightCacheFunctions(	gfxCreateStaticLightCache, gfxFreeStaticLightCache,
		gfxCreateDynLightCache, gfxFreeDynLightCache, 
		gfxForceUpdateLightCaches, gfxInvalidateLightCache, 
		gfxComputeStaticLightingForBinning, gfxComputeTerrainLightingForBinning,
		gfxUpdateIndoorVolume);

	worldLibSetWorldGraphicsDataFunctions(	gfxAllocWorldGraphicsData, gfxFreeWorldGraphicsData, 
		gfxAllocWorldRegionGraphicsData, gfxFreeWorldRegionGraphicsData, gfxTickSkyData);

	worldLibSetGfxDynamicsCallbacks(gfxDynamicsGetParticleMemUsage);
	worldLibSetGfxSettingsCallbacks(gfxGetLodScale, gfxGetEntityDetailLevel, gfxGetClusterLoadSetting, gfxMaterialsReloadAll);
	worldLibSetNotifySkyGroupFreedFunc(gfxSkyNotifySkyGroupFreed);

	worldLibSetSimplygonFunctions(
		gfxGetSimplygonMaterialIdFromTable,
		gfxMaterialClusterTexturesFromMaterial,
		gfxCalcVertexLightingForTransformedGMesh,
		gfxCheckClusterLoaded,
		gfxWorldCellGraphicsFreeClusterTexSwaps,
		gfxSetClusterState,
		gfxGetClusterState);

	conCreate();

	inpSetConsoleInputProcessor(gfxConsoleProcessInput);

	gfx_state.pastStartup = 1;

	texDynamicUnload(true);

	loadend_printf("GraphicsLib startup done.");
}

static void waitFps(F32 fps)
{
	static int frame_timer = -1;
	F32		elapsed,frame_time,extra;

	if (0 && rdr_state.perFrameSleep) // This function is only called if /maxfps is also specified - leaving just a sleep in the render thread instead
	{
		int amount = rdr_state.perFrameSleep-1;
		PERFINFO_AUTO_START_BLOCKING("perFrameSleep", 1);
		amount = CLAMP(amount, 0, 500);
		Sleep(amount);
		PERFINFO_AUTO_STOP();
	}

	if (!fps)
	{
		//Sleep(0);
		return;
	}
	frame_time = 1.f / fps;
	if (frame_timer < 0)
	{
		frame_timer = timerAlloc();
		timerStart(frame_timer);
	}
	elapsed = timerElapsed(frame_timer);
	extra = frame_time - elapsed;
	// complicated series of machinations to achieve an unnecessarily precise level of framerate locking [RMARR - 9/13/12]
	if (extra > 0.0f)
	{
		Sleep(extra * 1000.0f);

		while (frame_time - timerElapsed(frame_timer) > 2e-4f)
		{
			Sleep(0);
		}

		while (frame_time - timerElapsed(frame_timer) > 0.0f)
		{
		}
	}

	timerStart(frame_timer);

	if (fps < 30) // Allow default options of 30/60 maxfps to still log perf data
		gfxInvalidateFrameCounters();
}

// TODO: djr move these calculations into graphicslib or application-specific code/data
// 
// radius: Specify the radius of the sphere.
// theta: angle away from the slihouette edge of the sphere, towards 
static float calculateLightTravelDistInAtmosphere( float radius, float atmosphereHeight, float theta)
{
	// Note: a = 1
	float b = 2 * radius * sinf( theta );
	float c = -( 2 * radius + atmosphereHeight ) * atmosphereHeight;

	return ( -b + sqrt( b * b - 4 * c ) ) * 0.5f;
}

U32 gfxGetTotalTrackedVideoMemory(void)
{
	int i;
	static RdrSurfaceQueryAllData query_data;
	U32 total_mem=0;
	GeoMemUsage geo_usage;

	PERFINFO_AUTO_START_FUNC();

	rdrQueryAllSurfaces(gfx_state.currentDevice->rdr_device, &query_data);
	for (i=0; i<query_data.nsurfaces; i++)
		total_mem += query_data.details[i].total_mem_size;
	wlGeoGetMemUsageQuick(&geo_usage);
	total_mem += geo_usage.loadedVideoTotal;
	total_mem += texMemoryUsage[TEX_MEM_VIDEO];

	PERFINFO_AUTO_STOP();
	return total_mem;
}

static void gfxCalcGlobalFPS(void)
{
	U32	delta;
	F32 time;

	// Calc FPS over a few frames
	delta = gfx_state.client_frame_timestamp - gfx_state.last_fps_ticks;
	time = (F32)delta / (F32)timerCpuSpeed();
	if (time > (gfx_state.showfps?gfx_state.showfps:1))
	{
		gfx_state.fps = gfx_state.show_fps_frame_count / time;
		gfx_state.show_fps_frame_count = 0;
		gfx_state.last_fps_ticks = gfx_state.client_frame_timestamp;
		if (gfx_state.showmem) {
			gfx_state.mem_usage_tracked = memMonitorGetTotalTrackedMemory();
			gfx_state.mem_usage_actual = getProcessPageFileUsage();
		}
		// Save this information to the trivia for additional debugging ability and for error reports about framerate
		{
			char fpshist[128];
			const char *lastfps;
			lastfps = triviaGetValue("FPSHistory");
			if (gfx_state.fps > 10)
			{
				snprintf(fpshist, _TRUNCATE, "%1.0f %s", gfx_state.fps, lastfps);
			} else {
				snprintf(fpshist, _TRUNCATE, "%1.3f %s", gfx_state.fps, lastfps);
			}
			triviaPrintf("FPSHistory", "%s", fpshist);
		}
		// Cycle and save bottleneck history
		{
			char bottleneckhist[128];
			char this_bottleneck_buf[1024];
			const char *lastbottleneck;
			int summation[GfxBottleneck_MAX] = {0};
			int total=0;
			int i, j;

			this_bottleneck_buf[0] = 0;
			for (i=ARRAY_SIZE(gfx_state.debug.bottleneck_hist)-1; i>=0; i--)
			{
				for (j=0; j<GfxBottleneck_MAX; j++)
				{
					summation[j] += gfx_state.debug.bottleneck_hist[i][j];
					total += gfx_state.debug.bottleneck_hist[i][j];
					if (i<ARRAY_SIZE(gfx_state.debug.bottleneck_hist)-1)
					{
						ANALYSIS_ASSUME(i<ARRAY_SIZE(gfx_state.debug.bottleneck_hist)-1);
#pragma warning(suppress:6200) // /analyze ignoring the ANALYSIS_ASSUME above
						gfx_state.debug.bottleneck_hist[i+1][j] = gfx_state.debug.bottleneck_hist[i][j];
						gfx_state.debug.bottleneck_hist[i][j] = 0;
					}
				}
			}
			for (i=0; i<GfxBottleneck_MAX; i++)
			{
				if (summation[i] * 100 > total) // More than 1%
				{
					if (this_bottleneck_buf[0])
						strcat(this_bottleneck_buf, " ");
					strcatf(this_bottleneck_buf, "%s:%1.0f", gfxGetBottleneckString(i), summation[i] *100.f/ (float)total);
				}
			}
			lastbottleneck = triviaGetValue("BottleneckHistory");
			snprintf(bottleneckhist, _TRUNCATE, "(%s) %s", this_bottleneck_buf, lastbottleneck);
			triviaPrintf("BottleneckHistory", "%s", bottleneckhist);
		}
	}
	gfx_state.show_fps_frame_count++;

	gfx_state.frame_count++;
}

static void updateStallForIndex(int index)
{
#define ADJ_FRAMES 4
	int i;
	float spfbefore=0;
	int count;
	float spfafter=0;
	count=0;

	if (gfx_state.debug.fpsgraph.spfhist[index] < 1/30.f) {
		// Stall detection breaks down at high framerates, and stalls < 1 frame at high framerates are not a big deal
		gfx_state.debug.fpsgraph.stallhist[index] = false;
		gfx_state.debug.frame_counts.stalled = 0;
		gfx_state.debug.frame_counts.stall_time = 0;
		return;
	}

	for (i=index - ADJ_FRAMES; i<index; i++) {
		int newindex = (i + ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist)) % ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist);
		if (gfx_state.debug.fpsgraph.spfhist[newindex] == 0) {
			// irrelevant frame
			continue;
		}
		if (gfx_state.debug.fpsgraph.stallhist[newindex]) {
			// It was a stall, don't count it
			continue;
		}
		spfbefore = (spfbefore * count + gfx_state.debug.fpsgraph.spfhist[newindex]) / (count+1);
		count++;
	}
	if (count == 0) // Nothing good, just use this value (force it not to be a stall)
		spfbefore = gfx_state.debug.fpsgraph.spfhist[index];
	count=0;
	for (i=index + 1; i<=index+ADJ_FRAMES; i++) {
		int newindex = (i + ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist)) % ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist);
		int idnext = (newindex + 1) % ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist);
		int idprev = (newindex - 1 + ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist)) % ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist);
		F32 spfme, spfnext, spfprev;
		if (gfx_state.debug.fpsgraph.spfhist[newindex] == 0) {
			// irrelevant frame
			continue;
		}
		// Loose stall detection:
		spfme = gfx_state.debug.fpsgraph.spfhist[newindex];
		spfprev = gfx_state.debug.fpsgraph.spfhist[idprev];
		spfnext = gfx_state.debug.fpsgraph.spfhist[idnext];
		if (spfme > 1.2 * spfprev && spfme > 1.2 * spfnext) {
			// Probably a stall
			continue;
		}
		spfafter = (spfafter * count + gfx_state.debug.fpsgraph.spfhist[newindex]) / (count+1);
		count++;
	}
	if (count == 0) // Nothing good, just use this value (force it not to be a stall)
		spfafter = gfx_state.debug.fpsgraph.spfhist[index];
	if (gfx_state.debug.fpsgraph.spfhist[index] > 1.3 * spfbefore &&
		gfx_state.debug.fpsgraph.spfhist[index] > 1.3 * spfafter)
	{
		gfx_state.debug.fpsgraph.stallhist[index] = true;
		gfx_state.debug.frame_counts.stalled = 1;
		gfx_state.debug.frame_counts.stall_time = gfx_state.debug.fpsgraph.spfhist[index] - MAX(spfbefore, spfafter);
		if (gfx_state.debug.printStallTimes)
			printf("Detected stall of %0.1fms\n", gfx_state.debug.frame_counts.stall_time * 1000);
	} else {
		gfx_state.debug.fpsgraph.stallhist[index] = false;
		gfx_state.debug.frame_counts.stalled = 0;
		gfx_state.debug.frame_counts.stall_time = 0;
	}
}

static void gfxUpdateStallGraph(void)
{
	int index = (gfx_state.debug.fpsgraph.hist_index - ADJ_FRAMES - 2 + ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist)) % ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist);
	updateStallForIndex(index);
	if (!gfx_state.debug.fpsgraph.stallhist[index]) {
		int mspf = (int)(gfx_state.debug.fpsgraph.spfhist[index] * 1000);
		MIN1(mspf, ARRAY_SIZE(gfx_state.debug.fpsgraph.mspfhistogram)-1);
		gfx_state.debug.fpsgraph.mspfhistogram_total++;
		gfx_state.debug.fpsgraph.mspfhistogram[mspf]++;
	}
	gfx_state.debug.stalls_per_frame = 0;
	for (index=0; index<ARRAY_SIZE(gfx_state.debug.fpsgraph.stallhist); index++) {
		if (gfx_state.debug.fpsgraph.stallhist[index])
			gfx_state.debug.stalls_per_frame+= 1.f/ARRAY_SIZE(gfx_state.debug.fpsgraph.stallhist);
	}
}

void gfxResetFrameRateStabilizerCounts(void)
{
	gfx_state.debug.resetFrameRateStabilizer = true;
}

void gfxCheckAutoFrameRateStabilizer(void)
{
#if !PLATFORM_CONSOLE
	static int last_frame=0;
	static int lastW, lastH;
	static int countdown=ARRAY_SIZE(gfx_state.debug.fpsgraph.stallhist);
	static int count=0;
	if (gfx_state.settings.autoEnableFrameRateStabilizer && !gfx_state.settings.frameRateStabilizer)
	{
		int w, h;
		gfxGetActiveDeviceSize(&w, &h);
		if (gfx_state.frame_count != last_frame+1 || gfx_state.settings.useVSync || gfxIsInactiveApp() ||
			eaSize(&gfx_state.devices)!=1 || gfx_state.debug.resetFrameRateStabilizer || w!=lastW || h!=lastH)
		{
			// Skipped some frames, reset, or Vsync is on, which makes the stall detection confused
			countdown=ARRAY_SIZE(gfx_state.debug.fpsgraph.stallhist);
			count = 0;
			gfx_state.debug.resetFrameRateStabilizer = false;
			lastW = w;
			lastH = h;
		} else {
			countdown--;
			if (gfx_state.debug.stalls_per_frame >= 5/256.f) {
				if (countdown < 0)
				{
					if (count==10) { // About a minute
						gfxStatusPrintf("Detected stuttery framerate, enabling /frameRateStabilizer");
						gfx_state.settings.frameRateStabilizer = 1;
						gfxApplySettings(gfx_state.currentDevice->rdr_device, &gfx_state.settings, ApplySettings_PastStartup, 9);
					} else {
						count++;
						countdown=ARRAY_SIZE(gfx_state.debug.fpsgraph.stallhist);
					}
				}
			} else {
				// Reset
				gfx_state.debug.resetFrameRateStabilizer = true;
			}
		}
		last_frame = gfx_state.frame_count;
	}
#endif
}

extern int disable_dir_shadow_threshold;
static void gfxUpdateTimers(F32 fFrameTime, F32 fRealFrameTime)
{
	static int timestamp_master;
	int nextIndex;
	ZoneMapTimeBlock **time_blocks = zmapInfoGetTimeBlocks(NULL);
	int iv;
	int i;
	F32 fMsPerTick = wlPerfGetMsPerTick();
	static S64 iLastLastGPUWait = 0;

	gfx_state.client_frame_timestamp = wlCalcNewFrameTimestamp(&timestamp_master);
	assert(timestamp_master == 1);
	gfx_state.client_loop_timer += fFrameTime;
	if (gfx_state.client_loop_timer > 65536.f)
		gfx_state.client_loop_timer -= 65536.f;
	gfx_state.frame_time = fFrameTime;
	gfx_state.real_frame_time = fRealFrameTime;
	gfx_state.debug.frame_counts.ms = fRealFrameTime * 1000.f;
	gfx_state.debug.frame_counts.fps = fRealFrameTime?(1/fRealFrameTime):1000;

	// add up all the GPU times, except idle
	for (i=(int)EGfxPerfCounter_MISC;i<(int)EGfxPerfCounter_COUNT;i++)
	{
		gfx_state.debug.frame_counts.gpu_ms += rdrGfxPerfCounts_Last.afTimers[i];
	}
	
	// note, we use the last gpu wait time here.  The current one is 0
	if (iLastLastGPUWait * fMsPerTick > 1.0f)
	{
		if (gfx_state.debug.frame_counts.gpu_ms <= 0.0f)
		{
			// This should only happen if the timer queries in rdrGfxPerfCounts_Last are failing
			gfx_state.debug.frame_counts.gpu_ms = gfx_state.debug.frame_counts.ms;
		}
		gfx_state.debug.frame_counts.cpu_ms = gfx_state.debug.frame_counts.ms-iLastLastGPUWait * fMsPerTick;
		if (gfx_state.debug.frame_counts.cpu_ms < 0.0f)
		{
			gfx_state.debug.frame_counts.cpu_ms = 0.0f;
		}
	}
	else
	{
		gfx_state.debug.frame_counts.cpu_ms = gfx_state.debug.frame_counts.ms;
	}

	gfx_state.debug.fpsgraph.spfhist[gfx_state.debug.fpsgraph.hist_index] = fRealFrameTime;
	gfx_state.debug.fpsgraph.spfhist_main[gfx_state.debug.fpsgraph.hist_index] += fRealFrameTime;
	nextIndex = (gfx_state.debug.fpsgraph.hist_index + 1) % ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist);
	gfx_state.debug.fpsgraph.spfhist_main[nextIndex] = -iLastLastGPUWait * fMsPerTick * 0.001f;
	gfx_state.debug.fpsgraph.stallhist[gfx_state.debug.fpsgraph.hist_index] = 0;
	gfx_state.debug.fpsgraph.hist_index++;
	gfx_state.debug.fpsgraph.hist_index %= ARRAY_SIZE(gfx_state.debug.fpsgraph.spfhist);

	iLastLastGPUWait = gfx_state.debug.last_frame_counts.world_perf_counts.time_wait_gpu;

	gfx_state.water_ripple_scale = MAX( 0, gfx_state.water_ripple_scale - 0.5 * fFrameTime );

	if (gfx_state.debug.overrideTime >= 0)
	{
		gfx_state.cur_time = gfx_state.debug.overrideTime;
	}
	else if (eaSize(&time_blocks) > 0 && disable_dir_shadow_threshold)
	{
		static int last_reset_count = -1;
		F32 desired_time = wlTimeGetClientTime();

		if (last_reset_count != worldGetResetCount(false) || wlTimeIsForced())
		{
			last_reset_count = worldGetResetCount(false);
			gfx_state.cur_time = desired_time;
			wlTimeClearIsForced();
		}
		else
		{
			// interpolate at gfx_state.time_rate rate to desired time:
			if (gfx_state.cur_time > desired_time)
				desired_time += 24;
			gfx_state.cur_time += gfx_state.time_rate * gfx_state.frame_time;
			MIN1(gfx_state.cur_time, desired_time);
		}

	}
	else
	{
		gfx_state.cur_time = wlTimeGet();
	}

	iv = (int)(gfx_state.cur_time / 24);
	gfx_state.cur_time = gfx_state.cur_time - iv*24.f;
	while (gfx_state.cur_time < 0)
		gfx_state.cur_time += 24.0f;

	// Look at frame history and flag stalled frames
	gfxUpdateStallGraph();

	gfx_state.debug.bottleneck_hist[0][gfxGetBottleneck()]++;

	PERFINFO_AUTO_START_FUNC();
	//If the frame rate is locked, slow down to match it.
	if (gfx_state.settings.maxInactiveFps && gfxIsInactiveApp()) {
		waitFps( gfx_state.settings.maxInactiveFps);
	} else if (gfx_state.settings.maxFps) {
		waitFps( gfx_state.settings.maxFps);
	} else if (gfx_state.debug.emulate_vs_time) {
		RdrDrawListPassStats *visual_pass_stats = &gfx_state.debug.last_frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL];
		F32 ms = gfxGetApproxPassVertexShaderTime(visual_pass_stats, NULL, false, gfx_state.debug.emulate_vs_time_use_old_intel);
		if (ms)
		{
			F32 emu_fps = 1000 / ms;
			waitFps(emu_fps);
		}
	}
	PERFINFO_AUTO_STOP();
}

void gfxResetFrameCounters(void)
{
	S64 subTimes = 0;
	int category, pass;
	static int cycle;

	world_perf_info.pCounts = &gfx_state.debug.frame_counts.world_perf_counts;

	// visual pass stats, not including editor only, primitives, and renderer draw calls
	gfx_state.debug.frame_counts.objects_in_scene = 0;
	gfx_state.debug.frame_counts.triangles_in_scene = 0;
	for (category = 0; category < ROC_COUNT; ++category)
	{
		if (category != ROC_PRIMITIVE && category != ROC_EDITOR_ONLY && category != ROC_RENDERER)
		{
			gfx_state.debug.frame_counts.objects_in_scene += gfx_state.debug.frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL].objects_drawn[category];
			gfx_state.debug.frame_counts.triangles_in_scene += gfx_state.debug.frame_counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL].triangles_drawn[category];
		}
	}

	// all draw calls
	gfx_state.debug.frame_counts.opaque_objects_drawn = gfx_state.debug.frame_counts.alpha_objects_drawn = 0;
	gfx_state.debug.frame_counts.opaque_triangles_drawn = gfx_state.debug.frame_counts.alpha_triangles_drawn = 0;
	for (pass = 0; pass < RDRSHDM_COUNT; ++pass)
	{
		gfx_state.debug.frame_counts.opaque_objects_drawn += gfx_state.debug.frame_counts.draw_list_stats.pass_stats[pass].opaque_objects_drawn;
		gfx_state.debug.frame_counts.alpha_objects_drawn += gfx_state.debug.frame_counts.draw_list_stats.pass_stats[pass].alpha_objects_drawn;
		gfx_state.debug.frame_counts.opaque_triangles_drawn += gfx_state.debug.frame_counts.draw_list_stats.pass_stats[pass].opaque_triangles_drawn;
		gfx_state.debug.frame_counts.alpha_triangles_drawn += gfx_state.debug.frame_counts.draw_list_stats.pass_stats[pass].alpha_triangles_drawn;
	}

	// Note time_wait_gpu is included in time_draw
	// Note time_queue_world is included in time_queue

	subTimes = gfx_state.debug.frame_counts.world_perf_counts.time_queue + 
		gfx_state.debug.frame_counts.world_perf_counts.time_draw + 
		gfx_state.debug.frame_counts.world_perf_counts.time_anim +
		gfx_state.debug.frame_counts.world_perf_counts.time_ui +
		gfx_state.debug.frame_counts.world_perf_counts.time_net +
		gfx_state.debug.frame_counts.world_perf_counts.time_fx +
		gfx_state.debug.frame_counts.world_perf_counts.time_sound;

	devassert(gfx_state.debug.frame_counts.world_perf_counts.time_misc >= subTimes);
	gfx_state.debug.frame_counts.world_perf_counts.time_misc -= subTimes;

	devassert(gfx_state.debug.frame_counts.world_perf_counts.time_draw >= gfx_state.debug.frame_counts.world_perf_counts.time_wait_gpu);
	gfx_state.debug.frame_counts.world_perf_counts.time_draw -= gfx_state.debug.frame_counts.world_perf_counts.time_wait_gpu;

	devassert(gfx_state.debug.frame_counts.world_perf_counts.time_queue >= gfx_state.debug.frame_counts.world_perf_counts.time_queue_world);
	gfx_state.debug.frame_counts.world_perf_counts.time_queue -= gfx_state.debug.frame_counts.world_perf_counts.time_queue_world;

	gfx_state.debug.frame_counts.world_animation_updates = worldGetAnimationUpdateCount();

	gfx_state.debug.frame_counts.mem_usage_mbs = (int)(memMonitorGetTotalTrackedMemoryEstimate() >> 20);

	gfx_state.debug.last_frame_counts = gfx_state.debug.frame_counts;

	memcpy(gfx_state.debug.last_frame_counts.world_perf_counts.time_gpu, rdrGfxPerfCounts_Last.afTimers, 
		sizeof(gfx_state.debug.last_frame_counts.world_perf_counts.time_gpu));

	ZeroStruct(&gfx_state.debug.frame_counts);

	// Calculate min/max/avgs
	if (gfx_state.debug.accumulated_frame_counts.count == 0) {
		StructCopyAll(parse_FrameCounts, &gfx_state.debug.last_frame_counts, &gfx_state.debug.accumulated_frame_counts.maxvalues);
		StructCopyAll(parse_FrameCounts, &gfx_state.debug.last_frame_counts, &gfx_state.debug.accumulated_frame_counts.minvalues);
		StructCopyAll(parse_FrameCounts, &gfx_state.debug.last_frame_counts, &gfx_state.debug.accumulated_frame_counts.sum);
		gfx_state.debug.accumulated_frame_counts.count++;
		cycle = 0;
	} else {
		if (cycle == 0)
		{
			shDoOperation(STRUCTOP_MAX, parse_FrameCounts, &gfx_state.debug.accumulated_frame_counts.maxvalues, &gfx_state.debug.last_frame_counts);
			cycle++;
		} else if (cycle == 1) {
			shDoOperation(STRUCTOP_MIN, parse_FrameCounts, &gfx_state.debug.accumulated_frame_counts.minvalues, &gfx_state.debug.last_frame_counts);
			cycle++;
		} else {
			shDoOperation(STRUCTOP_ADD, parse_FrameCounts, &gfx_state.debug.accumulated_frame_counts.sum, &gfx_state.debug.last_frame_counts);
			cycle = 0;
			gfx_state.debug.accumulated_frame_counts.count++; // Must only increase the count when we add to the sum or the average will be wrong
		}
	}
}

static void restartTimingFrameCallback(const char *filename_unused, void *dwParam)
{
	PERFINFO_AUTO_START_FUNC();
	autoTimerThreadFrameEnd();
	autoTimerThreadFrameBegin("fileLoaderThread");
	PERFINFO_AUTO_STOP();
}

void gfxOncePerFrame(F32 fFrameTime, F32 fRealFrameTime, int in_editor, int allow_offscreen_render)
{
	PERFINFO_AUTO_START_FUNC();
	//assertHeapValidateAll();

	gfx_state.inEditor = !!in_editor;

	fileLoaderRequestAsyncExec("timer", FILE_HIGHEST_PRIORITY, true, restartTimingFrameCallback, NULL);

	if (!gbNoGraphics)
	{
		// Check these *before* updating the frame timestamp, lest we unload everything in view while at the debugger!
		gfxUnloadCheck();
	}

	gfxUpdateTimers(fFrameTime, fRealFrameTime);

	gfxDebugOncePerFrame(fRealFrameTime);

	if (!gbNoGraphics)
	{
		worldLibSetLodScale(gfxGetLodScale(), gfx_state.settings.terrainDetailLevel, gfx_state.settings.worldLoadScale, gfx_state.settings.reduceFileStreaming);
		texWordsCheckReload();
	}

	//ttStringDimensionsCacheOncePerFrame();

	if (!gbNoGraphics)
	{
		atlasDoFrame();
		gfxGeoOncePerFrame();
		rdr_state.white_tex_handle = texDemandLoadFixed(white_tex);
	}

	gfxCalcGlobalFPS();
	gfxLCDOncePerFrame();

	// stuff that queues data loads:
	if (gfx_state.currentDevice)
	{
		if (!gbNo3DGraphics)
			gfxLightsDataOncePerFrame(); // Queues models to be loaded

		//gfxDisplayDebugInterface2D(in_editor); // Moved to GCLBaseStates

		// MUST be last!
		if (allow_offscreen_render)
		{
			gfxHeadshotOncePerFrame();
			gfxImposterAtlasOncePerFrame();
		}
		else
		{
			gfxHeadshotOncePerFrameMinimal();
		}
	}
	else
	{
		gfxHeadshotOncePerFrameMinimal();
	}

	texGenDoDelayFree();

	PERFINFO_AUTO_STOP_FUNC();
}

void gfxOncePerFrameEnd(bool close_old_regions)
{
	PERFINFO_AUTO_START_FUNC_PIX();

	gfxMaterialsOncePerFrame();
	gfxClearSpriteLists(); // Clear all devices' sprite lists

	if (close_old_regions)
		worldCloseAllOldRegions(gfx_state.frame_count);

	// Use gfx_state camera focus to set the world lib lod settings for characters
	if (!gbNo3DGraphics && !gbNoGraphics)
	{
		F32 fDetail = gfxGetEntityDetailLevel();
		WorldRegion* world_region = worldGetWorldRegionByPos(gfx_state.currentCameraFocus);
		const WorldRegionLODSettings* lod_settings = worldRegionRulesGetLODSettings(worldRegionGetRules(world_region));

		worldLibSetLODSettings(lod_settings, fDetail);
	}

	PERFINFO_AUTO_STOP_FUNC_PIX();
}


void gfxUpdateAllRenderersFlag(void)
{
	int i;
	gfx_state.allRenderersFlag = 0;
	for (i=eaSize(&gfx_state.devices)-1; i>=0; i--) {
		if (gfx_state.devices[i]) {
			gfx_state.allRenderersFlag |= (1 << i);
		}
	}
}

void gfxClearSpriteLists(void)
{
	int i;
	for (i=eaSize(&gfx_state.devices)-1; i>=0; i--) {
		if (gfx_state.devices[i] && gfx_state.devices[i]->sprite_list) {
			gfxClearSpriteList(gfx_state.devices[i]->sprite_list);
		}
	}
}

static void gfxHandleNewDevice(void)
{
	gfx_state.client_frame_timestamp++; // Pretend it's a new frame (for materials and other systems)
	gfxUpdateAllRenderersFlag();
	atlasFreeAll(); // Before texGenDestroy
	texGenDestroyVolatile();
	gfxMaterialsHandleNewDevice();
	//fontCacheFreeAll();
	texMakeDummies();
	//gfxTerrainFreeAll(); // Don't need to free this (as TexGen textures are already only on a single device, freeing this doesn't solve anything)
}

void gfxPreloadCheckStartGlobal(void)
{
	if (gbNoGraphics)
		return;

	if (rdr_state.compile_all_shader_types || gfx_state.preload_all_early)
		gfxMaterialFlagPreloadedAllTemplates();
	else
		gfxMaterialFlagPreloadedGlobalTemplates();

	if (!gfx_state.allow_preload_materials)
		return;

	// Only allow disabling of preload materials in development mode
	if (!gfx_state.force_preload_materials && isDevelopmentMode() && !gfx_state.settings.preload_materials && !rdr_state.compile_all_shader_types)
		return;

	if (!gfx_state.debug.did_preload_materials)
	{
		gfx_state.debug.did_preload_materials = true;
		gfxMaterialPreloadTemplates();
	}
}

void gfxPreloadCheckStartMapSpecific(bool bEarly)
{
	if (gbNoGraphics)
		return;

	gfxMaterialFlagPreloadedGlobalTemplates();
	gfxMaterialFlagPreloadedMapSpecificTemplates(bEarly);

	if (!gfx_state.allow_preload_materials)
		return;

	// Only allow disabling of preload materials in development mode
	if (!gfx_state.force_preload_materials && isDevelopmentMode() && !gfx_state.settings.preload_materials && !rdr_state.compile_all_shader_types)
		return;

	if (rdr_state.compile_all_shader_types || gfx_state.preload_all_early)
		gfxMaterialFlagPreloadedAllTemplates();

	gfxMaterialPreloadTemplates();
}

GetAccessLevelFunc gfx_access_level_func;
void gfxSetGetAccessLevelFunc(GetAccessLevelFunc f)
{
	gfx_access_level_func = f;
}

int gfxGetAccessLevel(void)
{
	if (gfx_access_level_func)
		return gfx_access_level_func();
	return 9;
}

GetAccessLevelFunc gfx_access_level_for_display_func;
void gfxSetGetAccessLevelForDisplayFunc(GetAccessLevelFunc f)
{
	gfx_access_level_for_display_func = f;
}

int gfxGetAccessLevelForDisplay(void)
{
	if (gfx_access_level_for_display_func)
		return gfx_access_level_for_display_func();
	return 0;
}


void gfxRegisterDevice(RdrDevice *rdr_device, InputDevice *input_device, bool allowShow)
{
	int empty_slot=-1;
	int i;
	GfxPerDeviceState *device_state;
	char buffer[1024];
	bool bIsFirstDevice = eaSize(&gfx_state.devices)==0;

	// See if it exists and find the first empty slot
	for (i=eaSize(&gfx_state.devices)-1; i>=0; i--) {
		if (!gfx_state.devices[i]) {
			empty_slot = i;
		} else {
			if (gfx_state.devices[i]->rdr_device == rdr_device) {
				assertmsg(0, "Registering the same device twice!");
			}
		}
	}
	device_state = callocStruct(GfxPerDeviceState);
	device_state->rdr_device = rdr_device;
	if (rdr_device->worker_thread)
		wtSetDebug(rdr_device->worker_thread, gfx_state.debug.renderThreadDebug);
	sprintf(buffer, "GfxDevice %d", empty_slot<0 ? eaSize(&gfx_state.devices) : empty_slot);
	device_state->event_timer = etlCreateEventOwner(buffer, "GfxDevice", "GraphicsLib");
	gfxInitCameraView(&device_state->autoCameraView);
	device_state->primaryCameraView = &device_state->autoCameraView;
	gfxInitCameraController(&device_state->autoCameraController, gfxEditorCamFunc, NULL);
	device_state->autoCameraController.override_no_fog_clip = 1;
	device_state->primaryCameraController = &device_state->autoCameraController;
	device_state->input_device = input_device;
	device_state->draw_list2d = rdrCreateDrawList(false);
	//we dont save the contents of the list across frames so there is no need to store the texture ptrs
	//in addition to the handles. Also, mark it as non-sprite cache so that it will use memory pool to hand of buffers
	//to the render thread
	device_state->sprite_list = gfxCreateSpriteList(1000, false, false); 
	if (empty_slot==-1) {
		eaPush(&gfx_state.devices, device_state);
	} else {
		gfx_state.devices[empty_slot] = device_state;
	}
	gfxUpdateAllRenderersFlag();

	gfxSetActiveDevice(rdr_device);

	gfxHandleNewDevice();

	// Must be before gfxSettingsLoad
	for (i=0; i<32; i++)
	{
		if (bIsFirstDevice)
			gfx_state.allRenderersFeatures |= ((rdrSupportsFeature(rdr_device, (1<<i)))?(1<<i):0);
		else
			gfx_state.allRenderersFeatures &= ((rdrSupportsFeature(rdr_device, (1<<i)))?(1<<i):0);
	}

	rdr_device->gfx_lib_device_lost_cb = gfxNotifyDeviceLost;
	rdr_device->gfx_lib_display_params_change_cb = gfxDisplayParamsChanged;

	if (!(system_specs.material_hardware_supported_features & SGFEAT_SM30_PLUS))
	{
		gfx_state.cclighting = 0;
		gfx_state.uberlighting = 0;
		gfx_state.shadow_buffer = 0;
	}

	//gfxPreloadCheckStartGlobal();
	//gfxPreloadCheckStartMapSpecific();
}

void gfxUnregisterDevice(RdrDevice *rdr_device)
{
	int i;
	if (gfx_state.currentDevice && gfx_state.currentDevice->rdr_device == rdr_device) {
		gfxSetActiveDevice(NULL); // Could set an arbitrary existing device active?
	}
	for (i=eaSize(&gfx_state.devices)-1; i>=0; i--) {
		if (gfx_state.devices[i] && gfx_state.devices[i]->rdr_device == rdr_device) {
			GfxPerDeviceState *device_state = gfx_state.devices[i];
			if (device_state->ssao_reflection_lookup_tex)
				texGenFree(device_state->ssao_reflection_lookup_tex);
			device_state->ssao_reflection_lookup_tex = NULL;
			texClearAllForDevice(i);
			gfxModelClearAllForDevice(i);
			gfxMaterialsClearAllForDevice(i);
			gfxFreeActions(device_state);
			gfxFreeTempSurfaces(device_state);
			gfxDeinitCameraView(&device_state->autoCameraView);
			gfxDeinitCameraController(&device_state->autoCameraController);
			gfxDestroySpriteList(device_state->sprite_list);
			rdrFreeDrawList(device_state->draw_list2d);

			//			if (device_state->input_device)
			//				inpDestroyInputDevice(device_state->input_device);
			SAFE_FREE(device_state->auxDevice.auxDeviceName);
			etlFreeEventOwner(device_state->event_timer);
			device_state->event_timer = NULL;
			free(device_state);
			gfx_state.devices[i] = NULL;
			gfxUpdateAllRenderersFlag();
			return;
		}
	}
	assertmsg(0, "Unregistering a device that doesn't exist!");
}

void gfxShutdown(RdrDevice *rdr_device) {

	gfxAlienClose();

	if(rdr_device) {
		gfxSetCurrentSettingsForNextTime();
		gfxSettingsSave(rdr_device);
		gfxUnregisterDevice(rdr_device);
		rdrShaderLibShutdown(rdr_device);
	}
	dynamicCacheSafeDestroy(&gfx_state.shaderCache);
}


static void gfxApplySurfaceSkyParams(const BlendedSkyInfo *sky_info)
{
	Vec3 lowfogcolor, highfogcolor;
	gfxHsvToRgb(sky_info->fogValues.lowFogColorHSV, lowfogcolor);
	gfxHsvToRgb(sky_info->fogValues.highFogColorHSV, highfogcolor);

	if (gfx_state.debug.disableFog)
	{
		rdrSurfaceSetFog(gfx_state.currentSurface, 
			gfx_state.far_plane_dist, gfx_state.far_plane_dist+1, 0,
			gfx_state.far_plane_dist, gfx_state.far_plane_dist+1, 0,
			0, 1, 
			lowfogcolor, highfogcolor, false);
	}
	else
	{
		if (0 && gfx_state.volumeFog) // TODO(DJR)
		{
			// 			const Vec2 * const fogParams = &sky_info->fogValues.fogdensity;
			// 			fogcolor[ 3 ] = calculateLightTravelDistInAtmosphere( 3963.19f * 5280, 
			// 				6.21371192f * 5280, sky_info->sunValues.sunAngle );
			// 			rdrSurfaceSetFog(gfx_state.currentSurface, (*fogParams)[0], (*fogParams)[1], lowfogcolor, highfogcolor, true);
		}
		else
		{
			rdrSurfaceSetFog(gfx_state.currentSurface, sky_info->fogValues.lowFogDist[0], sky_info->fogValues.lowFogDist[1], sky_info->fogValues.lowFogMax,
				sky_info->fogValues.highFogDist[0], sky_info->fogValues.highFogDist[1], sky_info->fogValues.highFogMax,
				sky_info->fogValues.fogHeight[0], sky_info->fogValues.fogHeight[1], 
				lowfogcolor, highfogcolor, false);
		}
	}
}

void gfxSetActiveSurfaceEx(RdrSurface *surface, RdrSurfaceBufferMaskBits write_mask, RdrSurfaceFace face)
{
	RdrSurface *last_surface = gfx_state.currentSurface; // For debugging

	if (gfx_state.currentSurface == surface && 
		gfx_state.currentSurfaceWriteMask == write_mask && gfx_state.currentSurfaceSet &&
		gfx_state.currentSurfaceFace == face)
		return;

	gfx_activeSurfaceSize[0] = surface->width_nonthread;
	gfx_activeSurfaceSize[1] = surface->height_nonthread;
	assert(surface->device == gfx_state.currentDevice->rdr_device); // Can't set a surface from another device active!
	gfx_state.currentSurface = surface;
	gfx_state.currentSurfaceSet = true;
	gfx_state.currentSurfaceWriteMask = write_mask;
	gfx_state.currentSurfaceFace = face;
	rdrSurfaceSetActive(surface, write_mask, face);
	gfxApplySurfaceSkyParams(gfxSkyGetVisibleSky(gfx_state.currentCameraView->sky_data));
	gfxSetExposureTransform();
	gfxSetShadowBufferTexture(gfx_state.currentDevice->rdr_device, SAFE_MEMBER2(gfx_state.currentAction, bufferDeferredShadows, surface));
	gfxSetCubemapLookupTexture(gfx_state.currentDevice->rdr_device, SAFE_MEMBER(gfx_state.currentAction, gdraw.do_shadows));

	{
		TexHandle depth_tex = 0;
		if (gfx_state.currentAction && gfx_state.settings.soft_particles)
		{
			int depthSnapshotIndex = 1;
			depth_tex = rdrSurfaceToTexHandleEx(gfx_state.currentAction->opaqueDepth.surface,
				gfx_state.currentAction->opaqueDepth.buffer,
				depthSnapshotIndex,
				RTF_MIN_POINT|RTF_MAG_POINT|RTF_CLAMP_U|RTF_CLAMP_V, false);
		}
		gfxSetSoftParticleDepthTexture(gfx_state.currentDevice->rdr_device, depth_tex);
	}
}

void gfxOverrideDepthSurface(RdrSurface *override_depth_surface)
{
	rdrOverrideDepthSurface(gfx_state.currentDevice->rdr_device, override_depth_surface);
}

__forceinline static void rdrSurfaceUnsetActive(RdrDevice *device)
{
	RdrSurfaceActivateParams params = { NULL, 0 };
	device->nonthread_active_surface = NULL;
	if (device->is_locked_nonthread)
		wtQueueCmd(device->worker_thread, RDRCMD_SURFACE_SETACTIVE, &params, sizeof(params));
}

void gfxUnsetActiveSurface(RdrDevice *device)
{
	rdrSurfaceUnsetActive(device);
	gfx_state.currentSurfaceSet = false;
	gfx_state.currentSurface = NULL;
}


void gfxSetActiveDevice(RdrDevice *rdr_device)
{
	int i;
	for (i=eaSize(&gfx_state.devices)-1; i>=0; i--) {
		if (gfx_state.devices[i] && gfx_state.devices[i]->rdr_device == rdr_device) {
			gfx_state.currentDevice = gfx_state.devices[i];
			gfx_state.currentRendererIndex = i;
			gfx_state.currentRendererFlag = 1 << i;
			gfxSetActiveCameraView(gfx_state.currentDevice->primaryCameraView, false);
			gfxSetActiveCameraController(gfx_state.currentDevice->primaryCameraController, false);
			gfxSetActiveSurface(rdrGetPrimarySurface(rdr_device));
			gfx_state.screenSize[0] = gfx_activeSurfaceSize[0];
			gfx_state.screenSize[1] = gfx_activeSurfaceSize[1];
			gfxUpdateSettingsFromDevice(rdr_device, &gfx_state.settings);

			//Since this sets the gamma for the entire screen I'm pretty sure you dont want this in windowed mode
			//if (gfx_state.settings.fullscreen || gfx_state.settings.maximized)
			rdrSetGamma(rdr_device, gfx_state.settings.gamma);
			//else
			//	rdrSetGamma(rdr_device, 1.0);

			gfxSetCurrentSpriteList(gfx_state.currentDevice->sprite_list);

			inpSetActive(gfx_state.currentDevice->input_device);

			return;
		}
	}
	if (rdr_device == NULL) {
		gfx_state.currentDevice = NULL;
		gfx_state.currentCameraView = NULL;
		gfx_state.currentCameraController = NULL;
		gfx_state.currentSurface = NULL;
		gfxSetCurrentSpriteList(NULL);
		inpSetActive(NULL);
	} else {
		assertmsg(0, "Trying to set an unregistered device active");
	}
}

// Called pretty much automatically, but could be user-invoked if desired
void gfxOncePerFramePerDevice(void)
{
	U32	delta;
	F32 time;
	F32 frameRatePercent;
	GfxPerDeviceState *device_state = gfx_state.currentDevice;
	int inactive;
	if (device_state->frame_count_of_last_update == gfx_state.frame_count)
		return;
	device_state->frame_count_of_last_update = gfx_state.frame_count;

	PERFINFO_AUTO_START_FUNC_PIX();

	// Calc FPS over a few frames
	delta = gfx_state.client_frame_timestamp - device_state->per_device_last_fps_ticks;
	time = (F32)delta / (F32)timerCpuSpeed();
	if (time > (gfx_state.showfps?gfx_state.showfps:1))
	{
		device_state->per_device_fps = device_state->per_device_show_fps_frame_count / time;
		device_state->per_device_show_fps_frame_count = 0;
		device_state->per_device_last_fps_ticks = gfx_state.client_frame_timestamp;
	}

	// Determine if we should draw this frame

	inactive = inpIsInactiveWindow(device_state->input_device) && !gfxIsInactiveApp();
	if (inactive != device_state->isInactive) {
		device_state->isInactive = inactive;
		device_state->frames_counted = 0;
		device_state->frames_rendered = 0;
	}

	frameRatePercent = (inactive && device_state->frameRatePercentBG)?device_state->frameRatePercentBG:device_state->frameRatePercent;
	if (frameRatePercent && (!device_state->frames_counted ||
		((device_state->frames_rendered / (F32)device_state->frames_counted) >  frameRatePercent)) &&
		!device_state->doNotSkipThisFrame)
	{
		device_state->skipThisFrame = 1;
	} else {
		device_state->skipThisFrame = 0;
	}
	device_state->doNotSkipThisFrame = 0; // Clear this if it was set

	if (!device_state->skipThisFrame) {
		device_state->per_device_show_fps_frame_count++;
		device_state->frames_rendered++;
		device_state->per_device_frame_count++;
	}
	device_state->frames_counted++;

	// Process and draw debug console
	gfxConsoleRender();

	gfxFMVUpdateParams();
	gfxTempSurfaceOncePerFramePerDevice();
	gfxPrimitiveDoDataLoading();

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

void gfxLockActiveDeviceEx(bool do_begin_scene)
{
	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_START("top", 1);
	etlAddEvent(gfx_state.currentDevice->event_timer, "Lock", ELT_RESOURCE, ELTT_BEGIN);
	if (do_begin_scene && world_perf_info.in_time_draw)
		wlPerfStartWaitGPUBudget();
	rdrLockActiveDevice(gfxGetActiveDevice(), do_begin_scene);
	if (do_begin_scene && world_perf_info.in_time_draw)
		wlPerfEndWaitGPUBudget();
	gfx_state.currentDevice->rdr_perf_values = gfx_state.currentDevice->rdr_device->perf_values_last_frame;
	gfx_state.currentSurfaceSet = false;
	gfxSetExposureTransform();
	gfxSetShadowBufferTexture(gfx_state.currentDevice->rdr_device, SAFE_MEMBER2(gfx_state.currentAction, bufferDeferredShadows, surface));
	gfxSetCubemapLookupTexture(gfx_state.currentDevice->rdr_device, SAFE_MEMBER(gfx_state.currentAction, gdraw.do_shadows));
	PERFINFO_AUTO_STOP();

	// Need to do this here since during this frame we may try to add to one of these atlases (cursor code)

	texOncePerFramePerDevice(); // Loads/unloads textures on this renderer
	gfxOncePerFramePerDevice();

	rdrUpdateGlobalsOnDeviceLock(gfx_state.currentDevice->rdr_device);

	gfx_state.debug.frame_counts.device_locks++;

	PERFINFO_AUTO_STOP();
}

void gfxUnlockActiveDeviceEx(bool do_xlive_callback, bool do_end_scene, bool do_buffer_swap)
{
	bool bDidWait;
	PERFINFO_AUTO_START_FUNC();
	if (do_buffer_swap)
		wlPerfStartWaitGPUBudget();
	bDidWait = rdrUnlockActiveDevice(gfxGetActiveDevice(), do_xlive_callback, do_end_scene, do_buffer_swap);
	if (do_buffer_swap) {
		wlPerfEndWaitGPUBudget();
		gfx_state.debug.frame_counts.gpu_bound = bDidWait;
	}
	etlAddEvent(gfx_state.currentDevice->event_timer, "Lock", ELT_RESOURCE, ELTT_END);
	gfx_state.currentSurfaceSet = false;
	PERFINFO_AUTO_STOP();
}


RdrDevice *gfxGetActiveDevice(void)
{
	if (!gfx_state.currentDevice)
		return NULL;
	return gfx_state.currentDevice->rdr_device;
}

RdrDevice *gfxGetPrimaryDevice(void)
{
	return ((eaSize(&gfx_state.devices) && gfx_state.devices[0]) ? gfx_state.devices[0]->rdr_device : NULL);
}

GfxPerDeviceState *gfxGetPrimaryGfxDevice(void)
{
	return ((eaSize(&gfx_state.devices) && gfx_state.devices[0]) ? gfx_state.devices[0] : NULL);
}

RdrDevice *gfxGetActiveOrPrimaryDevice(void)
{
	RdrDevice *ret = gfxGetActiveDevice();
	if (ret)
		return ret;
	return gfxGetPrimaryDevice();
}

InputDevice *gfxGetActiveInputDevice(void)
{
	return gfx_state.currentDevice->input_device;
}

void gfxSetTargetEntityDepth(F32 target_entity_depth)
{
	gfx_state.target_entity_depth = target_entity_depth;
}

F32 gfxGetTargetEntityDepth(void)
{
	return gfx_state.target_entity_depth;
}

F32 gfxGetFrameTime(void)
{
	return gfx_state.frame_time;
}

F32 gfxGetClientLoopTimer(void) // Timer use for scrolling textures, etc
{
	return gfx_state.client_loop_timer;
}

U32 gfxGetFrameCount(void)
{
	return gfx_state.frame_count;
}

bool gfxGetDrawHighDetailSetting(void)
{
	return !!gfx_state.settings.draw_high_detail;
}

bool gfxGetDrawHighFillDetailSetting(void)
{
	return !!gfx_state.settings.draw_high_fill_detail;
}

RdrSurface *gfxGetActiveSurface(void)
{
	return gfx_state.currentSurface;
}

void gfxSetTitle(const char *title)
{
	// This could queue it and do it to all devices if that makes more sense
	if (gfx_state.currentDevice)
		rdrSetTitle(gfx_state.currentDevice->rdr_device, title);
}

void gfxCheckForMakeCubeMap(void)
{
	if (gfx_state.make_cubemap) {
		struct {
			const char *ext;
			Vec3 pyr;
		} faces[] = {
			{"_posx.tga", {0, -PI/2, 0}},
			{"_negx.tga", {0, PI/2, 0}},
			{"_posz.tga", {PI, 0, PI}},
			{"_negz.tga", {0, 0, 0}},
			{"_posy.tga", {-PI/2, 0, PI}},
			{"_negy.tga", {PI/2, 0, PI}},
		};
		int i;
		F32 saved_time = gfx_state.frame_time;
		GfxCameraView saved_camera;
		saved_camera = *gfxGetActiveCameraView();

		gfx_state.make_cubemap = 0;
		gfx_state.frame_time = 0;
		gfxSetActiveProjection(90.0f, 1.0f);

		for (i=0; i<ARRAY_SIZE(faces); i++) {
			int j;
			for (j=0; j<3; ) { // Do at least 3 frames, just in case of loading and camera buffering, etc.
				gfx_state.screenshot_type = SCREENSHOT_3D_ONLY;
				sprintf(gfx_state.screenshot_filename, "%s%s", gfx_state.make_cubemap_filename, faces[i].ext);
				gfx_state.client_frame_timestamp++;
				gfxSetActiveCameraYPR(faces[i].pyr);
				gfxTellWorldLibCameraPosition(); // Call this only on the primary camera

				gfxStartMainFrameAction(false, false, false, false, true);
				gfxFillDrawList(true, NULL);
				gfxDrawFrame();
				gfxMaterialsOncePerFrame();

				if (!gfxIsStillLoading(true))
					j++;

				geoForceBackgroundLoaderToFinish();
				texForceTexLoaderToComplete(1);
			}
		}
		gfx_state.frame_time = saved_time;
		*gfxGetActiveCameraView() = saved_camera;
	}
}

F32 gfxGetTime(void)
{
	GfxCameraController *camera = gfx_state.currentAction ? gfx_state.currentAction->cameraController : gfx_state.currentCameraController;

	if (camera->override_time)
		return camera->time_override;

	return gfx_state.cur_time;
}

void gfxClearGraphicsData(void)
{
	gfxInvalidateFrameCounters();

	if (!gfx_state.currentDevice)
		return;
}

static F32 initRdrAddParamsForSingleModel(RdrAddInstanceParams *instance_params, RdrLightParams *light_params, SingleModelParams *smparams)
{
	F32 zdist;
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;

	copyMat4(smparams->world_mat, instance_params->instance.world_matrix);
	addVec3(instance_params->instance.world_matrix[3], gdraw->pos_offset, instance_params->instance.world_matrix[3]);
	mulVecMat4(smparams->model->mid, instance_params->instance.world_matrix, instance_params->instance.world_mid);
	zdist = rdrDrawListCalcZDist(gdraw->draw_list, instance_params->instance.world_mid);

	if (smparams->dist < 0) {
		Vec3 cam_pos;
		gfxGetActiveCameraPos(cam_pos);
		// TODO(CD) need to subtract the model's radius from the distance
		smparams->dist = distance3(instance_params->instance.world_mid, cam_pos);
	}
	instance_params->distance_offset = smparams->dist_offset;
	instance_params->frustum_visible = 0xffffffff;
	instance_params->wireframe = CLAMP(smparams->wireframe, 0, 3);
	if (gfx_state.wireframe)
		instance_params->wireframe = CLAMP(gfx_state.wireframe, 0, 3);
	copyVec3(smparams->color, instance_params->instance.color);
	setVec3same(instance_params->ambient_multiplier, 1);

	if (smparams->unlit)
	{
		gfxGetUnlitLight(light_params);
	}
	else
	{
		gfxGetObjectLightsUncached(light_params, NULL, instance_params->instance.world_mid, smparams->model->radius, !!smparams->num_bones);

		if (smparams->ambient[0] > 0 || smparams->ambient[1] > 0 || smparams->ambient[2] > 0 ||
			smparams->sky_light_color_front[0] > 0 || smparams->sky_light_color_front[1] > 0 || smparams->sky_light_color_front[2] > 0 ||
			smparams->sky_light_color_back[0] > 0 || smparams->sky_light_color_back[1] > 0 || smparams->sky_light_color_back[2] > 0 ||
			smparams->sky_light_color_side[0] > 0 || smparams->sky_light_color_side[1] > 0 || smparams->sky_light_color_side[2] > 0)
		{
			light_params->ambient_light = gfxGetOverrideAmbientLight(smparams->ambient, smparams->sky_light_color_front, smparams->sky_light_color_back, smparams->sky_light_color_side);
		}
	}

	return zdist;
}

void gfxQueueSingleModelTinted(SingleModelParams *smparams, RdrGeometryType overrideType)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	int i, j, k;
	RdrAddInstanceParams instance_params={0};
	RdrLightParams light_params={0};
	RdrInstancePerDrawableData per_drawable_data;
	ModelToDraw models[NUM_MODELTODRAWS];
	int model_count;
	BasicTexture **eaTextureSwaps = NULL;
	Material **eaMaterialSwaps = NULL;
	F32 zdist;

	if (gfx_state.currentDevice->skipThisFrame)
		return;

	instance_params.light_params = &light_params;
	zdist = initRdrAddParamsForSingleModel(&instance_params, &light_params, smparams);

	model_count = gfxDemandLoadModel(smparams->model, models, ARRAY_SIZE(models), smparams->dist,
									 smparams->override_visscale > 0 ? smparams->override_visscale : gfxGetLodScale(),
									 smparams->force_lod ? smparams->lod_override : -1,
									 smparams->model_tracker, smparams->model->radius);
	if (!model_count)
		return;

	eaCopy(&eaMaterialSwaps, &smparams->eaMaterialSwaps);
	eaCopy(&eaTextureSwaps, &smparams->eaTextureSwaps);

	// Draw each LOD model
	for (j=0; j<model_count; j++) 
	{
		RdrDrawableGeo *geo_draw;
		int orig_mat_swap_size = eaSize(&eaMaterialSwaps);
		int orig_tex_swap_size = eaSize(&eaTextureSwaps);
		int subobject_count = models[j].model->geo_render_info->subobject_count;
		bool use_fallback_materials = false;

		if (!models[j].geo_handle_primary) {
			assert(0);
			continue;
		}

		if (smparams->model->lod_info && ((int)models[j].lod_index) < eaSize(&smparams->model->lod_info->lods))
		{
			AutoLOD *auto_lod = smparams->model->lod_info->lods[models[j].lod_index];
			for (k = 0; k < eaSize(&auto_lod->material_swaps); ++k)
			{
				eaPush(&eaMaterialSwaps, materialFind(auto_lod->material_swaps[k]->orig_name, WL_FOR_UTIL));
				eaPush(&eaMaterialSwaps, materialFind(auto_lod->material_swaps[k]->replace_name, WL_FOR_UTIL));
			}
			for (k = 0; k < eaSize(&auto_lod->texture_swaps); ++k)
			{
				eaPush(&eaTextureSwaps, texFindAndFlag(auto_lod->texture_swaps[k]->orig_name, false, WL_FOR_UTIL));
				eaPush(&eaTextureSwaps, texFindAndFlag(auto_lod->texture_swaps[k]->replace_name, false, WL_FOR_UTIL));
			}
			use_fallback_materials = auto_lod->use_fallback_materials;
		}

		assert(models[j].model->geo_render_info);

		geo_draw = smparams->pDrawableGeo;
		if (geo_draw == NULL)
		{
			if (smparams->num_bones)
			{
				RdrDrawableSkinnedModel *skin_draw = rdrDrawListAllocSkinnedModel(gdraw->draw_list,
					overrideType < 0 ? RTYPE_SKINNED_MODEL : overrideType, models[j].model, 
					subobject_count, smparams->num_vertex_shader_constants, smparams->num_bones, NULL);
				if (!skin_draw)
					continue;
				memcpy(skin_draw->skinning_mat_array, smparams->bone_infos, sizeof(SkinningMat4) * skin_draw->num_bones);
				geo_draw = &skin_draw->base_geo_drawable;
			}
			else
			{
				geo_draw = rdrDrawListAllocGeo(gdraw->draw_list, overrideType < 0 ? RTYPE_MODEL : overrideType, models[j].model, 
					subobject_count, smparams->num_vertex_shader_constants, 0);
				if (!geo_draw)
					continue;
				instance_params.instance.morph = models[j].morph;

				if (smparams->bRetainRdrObjects)
					smparams->pDrawableGeo = geo_draw;
			}

			geo_draw->geo_handle_primary = models[j].geo_handle_primary;
		}

		{
			Vec4_aligned eye_bounds[8];
			Vec3 world_min;
			Vec3 world_max;
			scaleAddVec3(onevec3, -2 * models[j].model->model_parent->radius, smparams->world_mat[3], world_min);
			scaleAddVec3(onevec3, 2 * models[j].model->model_parent->radius, smparams->world_mat[3], world_max);
			mulBounds(world_min, world_max, gdraw->visual_pass->viewmat, eye_bounds);
			gfxGetScreenSpace(gdraw->visual_pass->frustum, gdraw->nextVisProjection, 1, eye_bounds, &instance_params.screen_area);
		}

		if (geo_draw->num_vertex_shader_constants)
			memcpy(geo_draw->vertex_shader_constants, smparams->vertex_shader_constants, sizeof(Vec4) * geo_draw->num_vertex_shader_constants);

		SETUP_INSTANCE_PARAMS;

		if (smparams->subObjects && smparams->subObjects[0])
		{
			devassert(models[j].model->data->tex_count == subobject_count);
			RDRALLOC_SUBOBJECT_PTRS(instance_params, models[j].model->data->tex_count);
			for (i = 0; i < subobject_count; i++)
			{
				instance_params.subobjects[i] = smparams->subObjects[i];
			}
		}
		else
		{
			RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[j].model, i);

			if (smparams->bRetainRdrObjects)
			{
				devassert(smparams->subObjects);
				for (i = 0; i < subobject_count; i++)
				{
					smparams->subObjects[i] = instance_params.subobjects[i];
				}
			}
		}

		for (i = 0; i < geo_draw->subobject_count; i++)
		{
			Material *material = models[j].model->materials[i];
			if (smparams->material_replace)
			{
				material = smparams->material_replace;
			}
			else
			{
				for (k=eaSize(&eaMaterialSwaps)-1; k>=1; k-=2)
				{
					if (material == eaMaterialSwaps[k-1])
					{
						material = eaMaterialSwaps[k];
						break;
					}
				}
				if (use_fallback_materials && !strEndsWith(material->material_name, ":Fallback"))
				{
					char fallback_name[1024];
					sprintf(fallback_name, "%s:Fallback", material->material_name);
					material = materialFind(fallback_name, WL_FOR_UTIL);
				}
			}

			gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], material, eaTextureSwaps, smparams->eaNamedConstants, NULL, NULL,
				instance_params.per_drawable_data[i].instance_param, zdist - smparams->model->radius, models[j].model->uv_density);
		}

		if (smparams->double_sided)
			instance_params.add_material_flags |= RMATERIAL_DOUBLESIDED;

		instance_params.instance.color[3] = models[j].alpha * smparams->alpha * U8TOF32_COLOR;

		instance_params.ignore_vertex_colors = !!(models[j].model->data->process_time_flags & MODEL_PROCESSED_HAS_WIND);

		if (smparams->num_bones)
			rdrDrawListAddSkinnedModel(gdraw->draw_list, (RdrDrawableSkinnedModel*)geo_draw, &instance_params, RST_AUTO, ROC_EDITOR_ONLY);
		else
			rdrDrawListAddGeoInstance(gdraw->draw_list, geo_draw, &instance_params, RST_AUTO, ROC_EDITOR_ONLY, true);

		gfxGeoIncrementUsedCount(models[j].model->geo_render_info, models[j].model->geo_render_info->subobject_count, true);

		eaSetSize(&eaMaterialSwaps, orig_mat_swap_size);
		eaSetSize(&eaTextureSwaps, orig_tex_swap_size);
	}

	eaDestroy(&eaMaterialSwaps);
	eaDestroy(&eaTextureSwaps);
}

void gfxQueueSingleModel(SingleModelParams *smparams)
{
	setVec3same(smparams->color, 1);
	smparams->eaNamedConstants = gfxMaterialStaticTintColorArray(onevec3);
	smparams->alpha = 255;
	gfxQueueSingleModelTinted(smparams, -1);
}

U32 gfx_activeSurfaceSize[2];

void gfxGetActiveSurfaceSize(int *width, int *height)
{
	if (!gbNoGraphics)
	{
		// Update this since the window may have resized since setting the surface active
		gfx_activeSurfaceSize[0] = gfx_state.currentSurface->width_nonthread;
		gfx_activeSurfaceSize[1] = gfx_state.currentSurface->height_nonthread;
		gfxGetActiveSurfaceSizeInline(width, height);
	}
	else
	{
		if (width)
			*width = 640;
		if (height)
			*height = 480;
	}

}

void gfxGetActiveDeviceSize(int *width, int *height)
{
	if (gfx_state.currentDevice && gfx_state.currentDevice->rdr_device)
		gfxUpdateSettingsFromDevice(gfx_state.currentDevice->rdr_device, &gfx_state.settings);
	gfxSettingsGetScreenSize(&gfx_state.settings, width, height);
}

void gfxGetActiveDevicePosition(int *x, int *y)
{
	if (gfx_state.currentDevice && gfx_state.currentDevice->rdr_device)
		gfxUpdateSettingsFromDevice(gfx_state.currentDevice->rdr_device, &gfx_state.settings);
	if (x)
		*x = gfx_state.settings.screenXPos;
	if (y)
		*y = gfx_state.settings.screenYPos;
}

F32 gfxGetLodScale(void)
{
	if (gfx_state.bDisableVisScale)
		return 1.0f;

	if (gfx_state.inEditor)
		return sqrt(gfx_state.editorVisScale);

	//	if (gfx_state.settings.worldDetailLevel < 1)
	//		return 1 - ((1 - gfx_state.settings.worldDetailLevel) * 0.5f);
	return sqrt(gfx_state.settings.worldDetailLevel);
}


F32 gfxGetDrawScale(void)
{
	//	if (gfx_state.settings.worldDetailLevel < 1)
	//		return gfx_state.settings.worldDetailLevel;
	return 1;
}

F32 gfxGetEntityDetailLevel(void)
{
	return gfx_state.settings.entityDetailLevel;
}

U32 gfxGetClusterLoadSetting(void)
{
	S32 scaleSlider = round(gfx_state.settings.slowUglyScale * gfxGetFillMax());

	return scaleSlider <= default_settings_rules.fill_cluster_low;
}

F32 gfxGetAspectRatio(void)
{
	F32 aspect = gfx_state.settings.aspectRatio;
	if (aspect <= 0)
	{
		aspect = (F32)gfx_activeSurfaceSize[0] / gfx_activeSurfaceSize[1];
		if (0) // Code to "fix" 1280x1024 to have the right aspect ratio, as it has non-square pixels on CRTs.  On LCDs, it actually gives a 5:4 viewing area, so we don't want to do this.
		{
			if (aspect == 5.f/4.f && gfx_state.settings.fullscreen)
				aspect = 4.f/3.f; // Correct for 1280x1024 fullscreen being the wrong aspect ratio if derived from pixels
		}
	}
	return aspect;
}

bool gfxGetFullscreen(void)
{
	return gfx_state.settings.fullscreen;
}

void *gfxGetWindowHandle(void)
{
	if (gfx_state.currentDevice && gfx_state.currentDevice->rdr_device)
		return rdrGetWindowHandle(gfx_state.currentDevice->rdr_device);
	return NULL;
}

void gfxSetSafeMode(int safemode)
{
	gfx_state.safemode = safemode;
}

int gfxGetSafeMode(void)
{
	return gfx_state.safemode;
}

void gfxProcessInputOnInactiveDevices(void)
{
	GfxPerDeviceState *current_device = gfx_state.currentDevice;
	int i;
	for (i=0; i<eaSize(&gfx_state.devices); i++) {
		GfxPerDeviceState *device = gfx_state.devices[i];
		if (device && device != current_device) {
			gfxSetActiveDevice(device->rdr_device);
			inpUpdateEarly(gfxGetActiveInputDevice());
		}
	}
	gfxSetActiveDevice(current_device->rdr_device);
}

GfxTempSurface* gfxGetPerfTimeMultiSampleSurface(RdrDevice *device, int multiSampleLevel)
{
	RdrSurface *currentSurface = rdrGetPrimarySurface(gfx_state.currentDevice->rdr_device);
	RdrSurfaceParams surfaceparams = {0};

	surfaceparams.name = "Multi Sample Perf Surface";
	rdrSurfaceParamSetSizeSafe(&surfaceparams, currentSurface->width_nonthread,
		currentSurface->height_nonthread);
	surfaceparams.desired_multisample_level = multiSampleLevel;
	surfaceparams.required_multisample_level = multiSampleLevel;

	{
		surfaceparams.buffer_types[0] = SBT_RGBA;
		surfaceparams.buffer_types[1] = SBT_RGBA;
	}

	rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

	return gfxGetTempSurface(&surfaceparams);
}

__forceinline F32 gfxGetPerfTime(RdrDevice *rdr_device, int multiSampleLevel) {
	int i;
	int finalCounter=2;
	F32 curMax=0;
	static bool sawSpeedIncrease=false;
	GfxTempSurface *multiTempSurface = NULL;
	RdrSurface *perfSurface;
	
	if (multiSampleLevel && !rdrSupportsFeature(rdr_device, FEATURE_DEPTH_TEXTURE_MSAA))
		return 0.0;	// requesting antialiasing when not supported so return a terrible score.
	if (multiSampleLevel) {
		multiTempSurface = gfxGetPerfTimeMultiSampleSurface(rdr_device, multiSampleLevel);
		if (!multiTempSurface)
			return 0.0;	// requesting antialiasing when not supported so return a terrible score.
		perfSurface = multiTempSurface->surface;
	} else {
		perfSurface = rdrGetPrimarySurface(rdr_device);
	}
	// The first N times seem to run slower, and then it speeds up to "actual" values, so run it a few times
	for (i=0; i<20; i++) 
	{
		RdrDevicePerfTimes perf_times = {0};
		gfxSetActiveDevice(rdr_device);
		rdrLockActiveDevice(rdr_device, true);
		gfxSetActiveSurface(perfSurface);
		rdrQueryPerfTimes(rdr_device, &perf_times);
		rdrUnlockActiveDevice(rdr_device, false, true, false);
		rdrFlush(rdr_device, false);
		assert(perf_times.bFilledIn == true);
		if (i!=0) {
			if (perf_times.pixelShaderFillValue > curMax * 1.2f) {
				// Increase of more than 20%, we must have hit the high speed
				sawSpeedIncrease = true;
			}
		}
		MAX1(curMax, perf_times.pixelShaderFillValue);
		if (sawSpeedIncrease)
			finalCounter--;
		if (finalCounter<=0 && i>=3)
			break;
	}
	sawSpeedIncrease = true; // Whether or not we did, let's not run it exhaustively anymore
	if (multiTempSurface) {
		gfxReleaseTempSurface(multiTempSurface);
	}
	return curMax;
}

typedef enum GfxPerformanceTestRunningState
{
	PERF_TEST_STATE_NEVER_RAN,

	PERF_TEST_STATE_NOT_RUNNING,
	PERF_TEST_STATE_RUNNING,
	PERF_TEST_STATE_PASS_COMPLETE,
	PERF_TEST_STATE_COMPLETE
} GfxPerformanceTestRunningState;

static const int PERF_TEST_FRAME_ITERATIONS = 20;
static const int PERF_TEST_FRAME_EARLY_OUT_MIN = 3;

typedef struct GfxPerformanceTestState
{
	GfxPerformanceTestRunningState currentState;
	int framesIssued;
	int framesComplete;
	int speedIncreasedFrameCount;
	float curMaxFill;
	bool sawSpeedIncrease;
	int multiSampleLevel;
	GfxPerDeviceState *gfx_device;
	RdrDevicePerfTimes perf_times;
	RdrDevicePerfTimes temp_perf_times;

	void *logo_jpg_data;
	int logo_data_size;
} GfxPerformanceTestState;

static GfxPerformanceTestState g_performanceTestState;

void gfxPerformanceTestFrameCompleteHandler(RdrDevice *device, RdrCmdFenceData *fenceData)
{
	GfxPerDeviceState *gfx_device = g_performanceTestState.gfx_device;

	++g_performanceTestState.framesComplete;
	gfxTracePerfTest("Perf frame complete MT callback %d / %d\n", g_performanceTestState.framesComplete, PERF_TEST_FRAME_ITERATIONS);

	assert(g_performanceTestState.temp_perf_times.bFilledIn == true);	
	if (g_performanceTestState.framesComplete != 0 )
	{
		if (g_performanceTestState.temp_perf_times.pixelShaderFillValue > g_performanceTestState.curMaxFill * 1.2f)
		{
			// Increase of more than 20%, we must have hit the high speed
			g_performanceTestState.sawSpeedIncrease = true;
		}
	}

	MAX1(g_performanceTestState.curMaxFill, g_performanceTestState.temp_perf_times.pixelShaderFillValue);
}

void gfxPerformanceTestMainFrameBegin(bool in_editor, bool z_occlusion, bool hide_world,
	bool draw_all_directions, bool draw_sky)
{
}

bool gfxPerformanceTestMainFrameEnd()
{
	GfxPerDeviceState *gfx_device = g_performanceTestState.gfx_device;
	RdrDevice *rdr_device = gfx_device->rdr_device;
	bool bIssueMoreFrames = false;
	
	bIssueMoreFrames = g_performanceTestState.framesIssued < PERF_TEST_FRAME_ITERATIONS;
	if (g_performanceTestState.sawSpeedIncrease)
		g_performanceTestState.speedIncreasedFrameCount--;

	if (g_performanceTestState.speedIncreasedFrameCount <= 0 && g_performanceTestState.framesComplete >= PERF_TEST_FRAME_EARLY_OUT_MIN)
		// early out when we record a consistent speed increase over sufficient frames
		bIssueMoreFrames = false;

	if (!bIssueMoreFrames)
	{
		if (g_performanceTestState.framesComplete == g_performanceTestState.framesIssued)
			// early out after completed frames catches up with issued frames
			g_performanceTestState.currentState = PERF_TEST_STATE_PASS_COMPLETE;
		else
			gfxTracePerfTest("Waiting for issues perf test frames to complete %d / %d\n",
				g_performanceTestState.framesComplete, g_performanceTestState.framesIssued);
	}

	if (g_performanceTestState.currentState == PERF_TEST_STATE_PASS_COMPLETE)
	{
		if (g_performanceTestState.multiSampleLevel)
			g_performanceTestState.perf_times.msaaPerformanceValue = g_performanceTestState.temp_perf_times.pixelShaderFillValue / g_performanceTestState.perf_times.pixelShaderFillValue;
		else
			g_performanceTestState.perf_times.pixelShaderFillValue = g_performanceTestState.temp_perf_times.pixelShaderFillValue;

		if (!g_performanceTestState.multiSampleLevel &&
			rdrSupportsFeature(rdr_device, FEATURE_DEPTH_TEXTURE_MSAA))
		{
			// retry with MSAA
			bIssueMoreFrames = true;

			memset(&g_performanceTestState.temp_perf_times, 0, sizeof(g_performanceTestState.temp_perf_times));
			g_performanceTestState.currentState = PERF_TEST_STATE_RUNNING;
			g_performanceTestState.framesIssued = 0;
			g_performanceTestState.framesComplete = 0;
			g_performanceTestState.speedIncreasedFrameCount = 2;
			g_performanceTestState.curMaxFill = 0.0f;
			g_performanceTestState.multiSampleLevel = 4;
		}
		else
		{
			// advance state to finished
			g_performanceTestState.currentState = PERF_TEST_STATE_COMPLETE;
			g_performanceTestState.perf_times.bFilledIn = true;
		}
	}

	if (bIssueMoreFrames)
	{
		GfxTempSurface *multiTempSurface = NULL;
		RdrSurface *perfSurface;
		int multiSampleLevel = g_performanceTestState.multiSampleLevel;

		if (multiSampleLevel) {
			multiTempSurface = gfxGetPerfTimeMultiSampleSurface(rdr_device, multiSampleLevel);
			if (!multiTempSurface) {
				// advance state to finished
				g_performanceTestState.currentState = PERF_TEST_STATE_COMPLETE;
				g_performanceTestState.perf_times.bFilledIn = true;
				return false;	// requesting antialiasing when not supported so return a terrible score.
			}
			perfSurface = multiTempSurface->surface;
		} else {
			perfSurface = rdrGetPrimarySurface(rdr_device);
		}

		gfxSetActiveDevice(rdr_device);
		rdrLockActiveDevice(rdr_device, true);
		gfxSetActiveSurface(perfSurface);
		rdrQueryPerfTimes(rdr_device, &g_performanceTestState.temp_perf_times);

		gfxTracePerfTest("Perf query issue %d / %d\n", (g_performanceTestState.framesIssued + 1), PERF_TEST_FRAME_ITERATIONS);


		rdrUnlockActiveDevice(rdr_device, false, true, true);
		rdrCmdFence(rdr_device, "Perf frame", gfxPerformanceTestFrameCompleteHandler, NULL );
		rdrFlush(rdr_device, false);

		if (multiTempSurface)
		{
			gfxReleaseTempSurface(multiTempSurface);
			multiTempSurface = NULL;
		}

		++g_performanceTestState.framesIssued;
	}
	else
	{
		gfxSetActiveDevice(rdr_device);
		rdrLockActiveDevice(rdr_device, true);
		rdrUnlockActiveDevice(rdr_device, false, true, true);
	}

	if (g_performanceTestState.currentState == PERF_TEST_STATE_COMPLETE)
	{
		gfx_device->rdr_perf_times.pixelShaderFillValue = g_performanceTestState.perf_times.pixelShaderFillValue;
		gfx_device->rdr_perf_times.msaaPerformanceValue = g_performanceTestState.perf_times.msaaPerformanceValue;

		gfxTracePerfTest("Pixel Shader Fill Value: %g\n", gfx_device->rdr_perf_times.pixelShaderFillValue);
		gfxTracePerfTest("MSAA Performance Value: %g\n", gfx_device->rdr_perf_times.msaaPerformanceValue);
		conPrintf("Pixel Shader Fill Value: %g", gfx_device->rdr_perf_times.pixelShaderFillValue);
		conPrintf("MSAA Performance Value: %g", gfx_device->rdr_perf_times.msaaPerformanceValue);
	}

	return g_performanceTestState.currentState != PERF_TEST_STATE_COMPLETE;
}

bool gfxStartPerformanceTest(GfxPerDeviceState *gfx_device)
{
	bool bStarted = false;
	if (g_performanceTestState.currentState == PERF_TEST_STATE_RUNNING)
		return false;
	bStarted = gfxHookFrame(gfxPerformanceTestMainFrameBegin, gfxPerformanceTestMainFrameEnd);

	if (bStarted)
	{
		gfxTracePerfTest("Perf test start\n");

		g_performanceTestState.currentState = PERF_TEST_STATE_RUNNING;
		g_performanceTestState.framesIssued = 0;
		g_performanceTestState.framesComplete = 0;
		g_performanceTestState.speedIncreasedFrameCount = 2;
		g_performanceTestState.curMaxFill = 0.0f;
		g_performanceTestState.multiSampleLevel = 0;
		g_performanceTestState.gfx_device = gfx_device;
		memset(&g_performanceTestState.perf_times, 0, sizeof(g_performanceTestState.perf_times));
	}

	return bStarted;
}

bool gfxGetPerformanceTestResults(RdrDevicePerfTimes *perf_output)
{
	bool bDone = false;
	if (g_performanceTestState.currentState == PERF_TEST_STATE_COMPLETE)
	{
		g_performanceTestState.currentState = PERF_TEST_STATE_NOT_RUNNING;

		gfxTracePerfTest("Perf test complete!\n");

		bDone = true;
		*perf_output = g_performanceTestState.perf_times;
	}

	return bDone;
}

void gfxQueryPerfTimes(RdrDevice *rdr_device)
{
	int timer;
	if (!gfx_state.currentDevice)
		return;
	timer = timerAlloc();
	if (!rdr_device)
		rdr_device = gfx_state.currentDevice->rdr_device;
	ZeroStruct(&gfx_state.currentDevice->rdr_perf_times);
	gfx_state.currentDevice->rdr_perf_times.pixelShaderFillValue = gfxGetPerfTime(rdr_device,false);
	if (rdrSupportsFeature(rdr_device, FEATURE_DEPTH_TEXTURE_MSAA))
		gfx_state.currentDevice->rdr_perf_times.msaaPerformanceValue = gfxGetPerfTime(rdr_device,4) / gfxGetPerfTime(rdr_device,1);
	else
		gfx_state.currentDevice->rdr_perf_times.msaaPerformanceValue = 0.0;
	gfx_state.currentDevice->timeSpentMeasuringPerf += timerElapsed(timer);
	timerFree(timer);
	gfx_state.settings.last_recorded_perf = gfx_state.currentDevice->rdr_perf_times.pixelShaderFillValue;

	gfxTracePerfTest("Pixel Shader Fill Value: %g\n", gfx_state.currentDevice->rdr_perf_times.pixelShaderFillValue);
	gfxTracePerfTest("MSAA Performance Value: %g\n", gfx_state.currentDevice->rdr_perf_times.msaaPerformanceValue);
}

// Queries performance times for the primary device
AUTO_COMMAND ACMD_CATEGORY(Performance) ACMD_NAME(gfxQueryPerfTimesMultiFrame);
void gfxQueryPerfTimesMultiFrameCmd(void)
{
	if (gfxStartPerformanceTest(gfx_state.currentDevice))
		gfxTracePerfTest("Perf test started\n");
}

// Queries performance times for the primary device
AUTO_COMMAND ACMD_CATEGORY(Performance) ACMD_NAME(gfxQueryPerfTimes);
void gfxQueryPerfTimesCmd(void)
{
	gfxQueryPerfTimes(gfxGetPrimaryDevice());
	conPrintf("Pixel Shader Fill Value: %g", gfx_state.currentDevice->rdr_perf_times.pixelShaderFillValue);
	conPrintf("MSAA Performance Value: %g", gfx_state.currentDevice->rdr_perf_times.msaaPerformanceValue);
}

//////////////////////////////////////////////////////////////////////////
// this function can be run before gfxStartup

void gfxDisplayLogo(RdrDevice *rdr_device, const void *logo_jpg_data, int logo_data_size, const char *window_title)
{
	gfxDisplayLogoProgress(rdr_device,logo_jpg_data,logo_data_size,window_title,-1,0, false);
	gfxDisplayLogoProgress(NULL,NULL,0,NULL,-1,0,true);
}

static void gfxLoadJpgTexture(RdrDevice *rdr_device, char *image_name, TexHandle *handle, int *width, int *height)
{
	if (fileExists(image_name))
	{
		int tex_width, tex_height, tex_size;
		void *tex_data;
		RdrTexParams *tex_params;
		char *jpg_data;
		int jpg_size;
		int data_size;

		jpg_data = fileAlloc(image_name, &jpg_size);
		if (jpg_data &&
			jpegLoadMemRef((char *)jpg_data, jpg_size, &tex_data, &tex_width, &tex_height, &tex_size))
		{
			if(!*handle)
			{
				*handle = rdrGenTexHandle(RTF_CLAMP_U|RTF_CLAMP_V);
			}
			tex_params = rdrStartUpdateTexture(rdr_device, *handle, RTEX_2D, RTEX_BGR_U8, tex_width, tex_height, 1, 1, &data_size, "Textures:Misc", tex_data);
			tex_params->is_srgb = (gfx_state.features_allowed & GFEATURE_LINEARLIGHTING) != 0;
			rdrEndUpdateTexture(rdr_device);
			memrefDecrement(tex_data);
			*width = tex_width;
			*height = tex_height;
		}
	}
}

// Perhaps this should reuse the white_tex_handle on the render device
static void gfxMakeWhiteTexture(RdrDevice *rdr_device, TexHandle *handle)
{
	void *tex_data;
	RdrTexParams *tex_params;
	U32 data_size;
	if(!*handle)
	{
		*handle = rdrGenTexHandle(RTF_CLAMP_U|RTF_CLAMP_V);
	}
	tex_data = memrefAlloc(4);
	*(U32*)tex_data = 0xFFFFFFff;
	tex_params = rdrStartUpdateTexture(rdr_device, *handle, RTEX_2D, RTEX_BGRA_U8, 1, 1, 1, 1, &data_size, "Textures:Misc", tex_data);
	assert(data_size == 4);
	rdrEndUpdateTexture(rdr_device);
	memrefDecrement(tex_data);
}

// progress: [0,1] amount to fill bar, negative for no bar
// spin: [0,1] grayscale value for the bar
// call with NULL for rdr_device to free resources
void gfxDisplayLogoProgress(RdrDevice *rdr_device, const void *logo_jpg_data, int logo_data_size, const char *window_title, float progress, float spin, bool bShowOrFreeLoadingText)
{
	static RdrDevice *logo_tex_device;
	static TexHandle logo_tex_handle;
	static int tex_width, tex_height;
	static TexHandle loading_text_handle;
	static TexHandle white_tex_handle;
	static int loading_tex_width, loading_tex_height=18; // Default height of 18 so that logo is placed where it will match the loading text image
	static const int loading_bar_pad = 5;
	static const int loading_bar_height = 21;
	static TexHandle loading_bar_handles[6];
	static int loading_bar_sizes[6][2];

	int data_size, screen_width, screen_height;
	RdrTexParams *tex_params;
	RdrSpriteState *sprite_state;
	RdrSpriteVertex *sprite_verts;
	int x1, x2, y1, y2;
	int i;
	int yLogoBottom;
	int yLoadingTextBottom;

	if(!rdr_device)
	{
		bool bHasLoadingText=!!loading_text_handle;
		for (i=0; i<ARRAY_SIZE(loading_bar_handles); i++)
			bHasLoadingText |= !!loading_bar_handles[i];
		if(logo_tex_device && (logo_tex_handle || bHasLoadingText && bShowOrFreeLoadingText))
		{
			rdrLockActiveDevice(logo_tex_device, false);
			if (logo_tex_handle)
				rdrFreeTexture(logo_tex_device, logo_tex_handle);
			if (white_tex_handle)
				rdrFreeTexture(logo_tex_device, white_tex_handle);
			if (bShowOrFreeLoadingText)
			{
				if (loading_text_handle)
				{
					rdrFreeTexture(logo_tex_device, loading_text_handle);
					loading_text_handle = 0;
					loading_tex_width = 0;
				}
				for (i=0; i<ARRAY_SIZE(loading_bar_handles); i++)
				{
					rdrFreeTexture(logo_tex_device, loading_bar_handles[i]);
					loading_bar_handles[i] = 0;
					loading_bar_sizes[i][0] = 0;
					loading_bar_sizes[i][1] = 0;
				}
			}
			rdrUnlockActiveDevice(logo_tex_device, false, false, false);
		}
		logo_tex_device = NULL;
		white_tex_handle = 0;
		logo_tex_handle = 0;
		tex_width = tex_height = 0;
		return;
	}
	assert(logo_tex_device == NULL || logo_tex_device == rdr_device);

	if(window_title)
		rdrSetTitle(rdr_device, window_title);

	rdrProcessMessages(rdr_device);
	if (rdrIsDeviceInactive(rdr_device))
	{
		return;
	}

#if !PLATFORM_CONSOLE
	{
		static bool bDidShowWindow=false;
		if (!bDidShowWindow)
		{
			int fullscreen, maximized, windowed_fullscreen;
			rdrLockActiveDevice(rdr_device, false);
			rdrGetDeviceSize(rdr_device, NULL, NULL, NULL, NULL, NULL, &fullscreen, &maximized, &windowed_fullscreen);
			rdrShowWindow(rdr_device, !fullscreen && maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
			rdrUnlockActiveDevice(rdr_device, false, false, false);
			bDidShowWindow = true;
		}
	}
#endif

	rdrLockActiveDevice(rdr_device, true);
	rdrClearActiveSurface(rdr_device, CLEAR_ALL, zerovec4, 1);
	screen_width = rdrGetPrimarySurface(rdr_device)->width_nonthread;
	screen_height = rdrGetPrimarySurface(rdr_device)->height_nonthread;

	// update texture
	if(!logo_tex_handle)
	{
		void *tex_data;
		int tex_data_size;
		if( logo_jpg_data &&
			jpegLoadMemRef((char *)logo_jpg_data, logo_data_size, &tex_data, &tex_width, &tex_height, &tex_data_size) )
		{
			//if(!logo_tex_handle)
			{
				logo_tex_device = rdr_device;
				logo_tex_handle = rdrGenTexHandle(RTF_CLAMP_U|RTF_CLAMP_V);
			}
			tex_params = rdrStartUpdateTexture(rdr_device, logo_tex_handle, RTEX_2D, RTEX_BGR_U8, tex_width, tex_height, 1, 1, &data_size, "Textures:Misc", tex_data);
			tex_params->is_srgb = (gfx_state.features_allowed & GFEATURE_LINEARLIGHTING) != 0;
			rdrEndUpdateTexture(rdr_device);
			memrefDecrement(tex_data);
		}
	}

	if (bShowOrFreeLoadingText)
	{
		if (!loading_text_handle)
		{
			static const char *progress_bar_prefx = "texture_library/UI/StartupScreens/Loadscreen_Progress_Bar_";
			static const char *progress_bar_names[] = {
				"Empty_Left.jpg",
				"Empty_Middle.jpg",
				"Empty_Right.jpg",
				"Full_Left.jpg",
				"Full_Middle.jpg",
				"Full_Right.jpg",
			};
			// Load "Loading..." text
			char text_image_name[MAX_PATH];
			STATIC_INFUNC_ASSERT(ARRAY_SIZE(loading_bar_handles) == ARRAY_SIZE(progress_bar_names));
			STATIC_INFUNC_ASSERT(ARRAY_SIZE(loading_bar_handles) == ARRAY_SIZE(loading_bar_sizes));
			//extern int loadedGameDataDirs;
			//assert(loadedGameDataDirs); // Can't be called before we've done filesystem startup or this will stall
			sprintf(text_image_name, "texture_library/UI/StartupScreens/Loading_Text_%s.jpg", locGetName(getCurrentLocale()));
			if (!fileExists(text_image_name))
				sprintf(text_image_name, "texture_library/UI/StartupScreens/Loading_Text_%s.jpg", locGetName(DEFAULT_LOCALE_ID));
			gfxLoadJpgTexture(rdr_device, text_image_name, &loading_text_handle, &loading_tex_width, &loading_tex_height);
			// Load progress bar images
			for (i=0; i<ARRAY_SIZE(loading_bar_handles); i++)
			{
				sprintf(text_image_name, "%s%s", progress_bar_prefx, progress_bar_names[i]);
				gfxLoadJpgTexture(rdr_device, text_image_name, &loading_bar_handles[i], &loading_bar_sizes[i][0], &loading_bar_sizes[i][1]);
			}
		}
	}

	// draw sprite
	if(logo_tex_handle)
	{
		F32 screen_aspect, new_aspect, tex_aspect;
		int num_sprites = (bShowOrFreeLoadingText&&loading_text_handle)?2:1;

		RdrSpritesPkg* pkg = rdrStartDrawSpritesImmediate(rdr_device, num_sprites, screen_width, screen_height);
		sprite_state = pkg->states;
		sprite_verts = pkg->vertices;

		for (i=0; i<num_sprites; i++)
		{
			sprite_state[i].sprite_effect = RdrSpriteEffect_None;
			sprite_state[i].use_scissor = 0;
			sprite_state[i].tex_handle1 = (i==0)?logo_tex_handle:loading_text_handle;
			sprite_state[i].tex_handle2 = 0;
			sprite_state[i].additive = 0;
			setVec2(sprite_verts[i*4+0].texcoords, 0, 1);
			setSpriteVertColorFromRGBA(&sprite_verts[i*4+0], 0xFFFFFFFF);
			setVec2(sprite_verts[i*4+1].texcoords, 0, 0);
			setSpriteVertColorFromRGBA(&sprite_verts[i*4+1], 0xFFFFFFFF);
			setVec2(sprite_verts[i*4+2].texcoords, 1, 0);
			setSpriteVertColorFromRGBA(&sprite_verts[i*4+2], 0xFFFFFFFF);
			setVec2(sprite_verts[i*4+3].texcoords, 1, 1);
			setSpriteVertColorFromRGBA(&sprite_verts[i*4+3], 0xFFFFFFFF);
		}

		tex_aspect = tex_height / (F32)tex_width;

		// center logo on screen
		if (tex_width <= screen_width)
		{
			x1 = (screen_width - tex_width) / 2;
			x2 = x1 + tex_width;
		}
		else
		{
			x1 = 0;
			x2 = screen_width;
		}
		if (tex_height + loading_tex_height + loading_bar_height + loading_bar_pad <= screen_height)
		{
			y1 = (screen_height - (tex_height + loading_tex_height + loading_bar_height + loading_bar_pad)) / 2 + loading_tex_height + loading_bar_height + loading_bar_pad;
			y2 = y1 + tex_height;
		}
		else
		{
			y1 = loading_tex_height + loading_bar_height + loading_bar_pad;
			y2 = screen_height;
		}

		yLogoBottom = y1;

		// maintain aspect ratio
		screen_aspect = screen_height / (F32)screen_width;
		new_aspect = (y2 - y1) / (F32)(x2 - x1);
		if (!nearSameF32(tex_aspect, new_aspect))
		{
			if (new_aspect > tex_aspect)
			{
				F32 new_height = (x2 - x1) * tex_aspect;
				y1 = (screen_height - new_height) / 2;
				y2 = y1 + new_height;
			}
			else
			{
				F32 new_width = (y2 - y1) / tex_aspect;
				x1 = (screen_width - new_width) / 2;
				x2 = x1 + new_width;
			}
		}

		setVec2(sprite_verts[0].point, x1, y1);
		setVec2(sprite_verts[1].point, x1, y2);
		setVec2(sprite_verts[2].point, x2, y2);
		setVec2(sprite_verts[3].point, x2, y1);

		if (num_sprites > 1)
		{
			x1 = (screen_width - loading_tex_width) / 2;
			x2 = screen_width - x1;
			y1 = (yLogoBottom + loading_tex_height + loading_bar_height + loading_bar_pad)/2;
			yLoadingTextBottom = y1 - loading_tex_height;
			setVec2(sprite_verts[4+0].point, x1, yLoadingTextBottom);
			setVec2(sprite_verts[4+1].point, x1, y1);
			setVec2(sprite_verts[4+2].point, x2, y1);
			setVec2(sprite_verts[4+3].point, x2, yLoadingTextBottom);
		} else {
			yLoadingTextBottom = yLogoBottom;
		}

		pkg->sprite_count = num_sprites;
		rdrEndDrawSpritesImmediate(rdr_device);
	} else {
		yLoadingTextBottom = 5*screen_height/6;
	}

	// draw progress bar
	if(progress >= 0)
	{
		x1 = screen_width/12;
		x2 = 11*screen_width/12;
		y2 = yLoadingTextBottom - loading_bar_pad;
		y1 = y2 - loading_bar_height;

		if (!loading_bar_handles[0])
		{
			int num_sprites = (progress < 1.f)?3:2;
			int x2temp = x1 + (x2 - x1)*progress;
			RdrSpritesPkg* pkg;
			if (!white_tex_handle)
				gfxMakeWhiteTexture(rdr_device, &white_tex_handle);
			pkg = rdrStartDrawSpritesImmediate(rdr_device, num_sprites, screen_width, screen_height);
			sprite_state = pkg->states;
			sprite_verts = pkg->vertices;

			for (i=0; i<num_sprites; i++)
			{
				int my_x1 = x1;
				int my_x2 = x2;
				int my_y1 = y1;
				int my_y2 = y2;
				Color color;
				Vec4 temp;
				switch (i)
				{
					xcase 0: // border
				color = ColorWhite;
				my_x1-=2;
				my_x2+=2;
				my_y1-=2;
				my_y2+=2;
				xcase 1: // filling bar
				MINMAX1(spin,0,1);
				setVec4(temp, spin, spin, spin, 1);
				vec4ToColor(&color, temp);
				my_x2 = x2temp;
				xcase 2: // black out unfilled bar
				color = ColorBlack;
				my_x1 = x2temp;
				}
				sprite_state[i].sprite_effect = RdrSpriteEffect_None;
				sprite_state[i].tex_handle1 = white_tex_handle;
				sprite_state[i].tex_handle2 = 0;
				sprite_state[i].additive = 0;
				setVec2(sprite_verts[i*4+0].texcoords, 0, 1);
				setSpriteVertColorFromRGBA(&sprite_verts[i*4+0], RGBAFromColor(color));
				setVec2(sprite_verts[i*4+1].texcoords, 0, 0);
				setSpriteVertColorFromRGBA(&sprite_verts[i*4+1], RGBAFromColor(color));
				setVec2(sprite_verts[i*4+2].texcoords, 1, 0);
				setSpriteVertColorFromRGBA(&sprite_verts[i*4+2], RGBAFromColor(color));
				setVec2(sprite_verts[i*4+3].texcoords, 1, 1);
				setSpriteVertColorFromRGBA(&sprite_verts[i*4+3], RGBAFromColor(color));
				sprite_state[i].use_scissor = 0;

				setVec2(sprite_verts[i*4+0].point, my_x1, my_y1);
				setVec2(sprite_verts[i*4+1].point, my_x1, my_y2);
				setVec2(sprite_verts[i*4+2].point, my_x2, my_y2);
				setVec2(sprite_verts[i*4+3].point, my_x2, my_y1);
			}

			pkg->sprite_count = num_sprites;
			rdrEndDrawSpritesImmediate(rdr_device);
		} else {
			// Have sprites, use them
			int num_sprites = ARRAY_SIZE(loading_bar_handles);
			int xpos[4];
			int x2temp = x1 + (x2 - x1)*progress;
			RdrSpritesPkg* pkg = rdrStartDrawSpritesImmediate(rdr_device, num_sprites, screen_width, screen_height);
			sprite_state = pkg->states;
			sprite_verts = pkg->vertices;

			for (i=0; i<num_sprites; i++)
			{
				sprite_state[i].sprite_effect = RdrSpriteEffect_None;
				sprite_state[i].tex_handle1 = loading_bar_handles[i];
				sprite_state[i].tex_handle2 = 0;
				sprite_state[i].additive = 0;
				setVec2(sprite_verts[i*4+0].texcoords, 0, 1);
				setSpriteVertColorFromRGBA(&sprite_verts[i*4+0], 0xFFFFFFFF);
				setVec2(sprite_verts[i*4+1].texcoords, 0, 0);
				setSpriteVertColorFromRGBA(&sprite_verts[i*4+1], 0xFFFFFFFF);
				setVec2(sprite_verts[i*4+2].texcoords, 1, 0);
				setSpriteVertColorFromRGBA(&sprite_verts[i*4+2], 0xFFFFFFFF);
				setVec2(sprite_verts[i*4+3].texcoords, 1, 1);
				setSpriteVertColorFromRGBA(&sprite_verts[i*4+3], 0xFFFFFFFF);
				if (i>=3)
				{
					sprite_state[i].use_scissor = 1;
					sprite_state[i].scissor_x = 0;
					sprite_state[i].scissor_width = x2temp;
					sprite_state[i].scissor_y = 0;
					sprite_state[i].scissor_height = screen_height;
				} else {
					sprite_state[i].use_scissor = 0;
				}
			}

			xpos[0] = x1;
			xpos[1] = x1 + loading_bar_sizes[0][0];
			xpos[2] = x2 - loading_bar_sizes[0][0];
			xpos[3] = x2;

			for (i=0; i<num_sprites; i++)
			{
				setVec2(sprite_verts[i*4+0].point, xpos[i%3], y1);
				setVec2(sprite_verts[i*4+1].point, xpos[i%3], y2);
				setVec2(sprite_verts[i*4+2].point, xpos[i%3+1], y2);
				setVec2(sprite_verts[i*4+3].point, xpos[i%3+1], y1);
			}

			pkg->sprite_count = num_sprites;
			rdrEndDrawSpritesImmediate(rdr_device);
		}
	}

	rdrUnlockActiveDevice(rdr_device, true, true, true);

#if _PS3
	rdrSyncDevice(rdr_device);
#else
	// one more time so the buffer gets swapped
	rdrLockActiveDevice(rdr_device, true);
	rdrUnlockActiveDevice(rdr_device, false, true, false);
#endif
}

void gfxSyncActiveDevice(void)
{
	RdrDevice *rdr_device = gfxGetActiveDevice();

	rdrSyncDevice(rdr_device);
}

// Toggles the window between restored and maximized
AUTO_COMMAND ACMD_NAME(window_restore) ACMD_ACCESSLEVEL(0);
void gfxWindowRestoreToggle(void)
{
#if !PLATFORM_CONSOLE
	if (gfx_state.currentDevice)
	{
		int fullscreen;
		rdrGetDeviceSize(gfx_state.currentDevice->rdr_device, NULL, NULL, NULL, NULL, NULL, &fullscreen, NULL, NULL);
		if (fullscreen)
		{
			gfxToggleFullscreen();
		} else {
			rdrShowWindow(gfx_state.currentDevice->rdr_device, SW_RESTORE);
		}
	}
#endif
}

// Minimizes the window
AUTO_COMMAND ACMD_NAME(window_minimize) ACMD_ACCESSLEVEL(0);
void gfxWindowMinimize(void)
{
#if !PLATFORM_CONSOLE
	if (gfx_state.currentDevice)
		rdrShowWindow(gfx_state.currentDevice->rdr_device, SW_MINIMIZE);
#endif
}


void gfxUnloadAllNotUsedThisFrame_check(void)
{
	if (gfx_state.unloadAllNotUsedThisFrame) {
		gfxUnloadAllNotUsedThisFrame();
		if (gfx_state.unloadAllNotUsedThisFrame==1)
			gfx_state.unloadAllNotUsedThisFrame = 0;
	}
}

void gfxGetFrameCounts(FrameCounts *counts)
{
	*counts = gfx_state.debug.last_frame_counts;
}

void gfxInvalidateFrameCounters(void)
{
	gfx_state.debug.accumulated_frame_counts.invalid = 2; // Skip 2 reportings
}

void gfxGetFrameCountsHistAndReset(FrameCountsHist *hist)
{
	if (gfx_state.debug.accumulated_frame_counts.count)
	{
		shDoOperationSetInt(gfx_state.debug.accumulated_frame_counts.count);
		StructCopyAll(parse_FrameCounts, &gfx_state.debug.accumulated_frame_counts.sum, &gfx_state.debug.accumulated_frame_counts.avg);
		shDoOperation(STRUCTOP_DIV, parse_FrameCounts, &gfx_state.debug.accumulated_frame_counts.avg, OPERAND_INT);
	}
	*hist = gfx_state.debug.accumulated_frame_counts;
	gfx_state.debug.accumulated_frame_counts.count = 0;
	if (gfx_state.debug.accumulated_frame_counts.invalid)
		gfx_state.debug.accumulated_frame_counts.invalid--;
}


FrameCounts *gfxGetFrameCountsForModification(void)
{
	return &gfx_state.debug.frame_counts;
}

int gfxGetMspfPercentile(float percentile)
{
	int i;
	float sum=0;
	for (i=0; i<ARRAY_SIZE(gfx_state.debug.fpsgraph.mspfhistogram); i++) {
		float value = gfx_state.debug.fpsgraph.mspfhistogram[i] / (float)gfx_state.debug.fpsgraph.mspfhistogram_total;
		sum+=value;
		if (sum >= percentile) {
			return i;
		}
	}
	return -1; // Invalid percentile specified?
}

void gfxResetInterFrameCounts(void)
{
	ZeroStruct(&gfx_state.debug.fpsgraph);
}

bool gbNoGraphics = FALSE;

bool gbNo3DGraphics = false;

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void nographics(int val)
{
	if (val)
	{
		freeEmergencyMemory();
		FolderCacheAddIgnore("server");
		FolderCacheAddIgnore("texture_library");
		FolderCacheAddIgnore("character_library");
		FolderCacheAddIgnore("object_library");
		wlSetLoadFlags(WL_NO_LOAD_MATERIALS|WL_NO_LOAD_DYNFX|WL_NO_LOAD_DYNANIMATIONS|WL_NO_LOAD_MODELS|WL_NO_LOAD_COSTUMES|WL_NO_LOAD_PROFILES);
	}
	gbNoGraphics = val;
}

extern bool gbIgnoreTextureErrors;

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void headless(int val)
{
	gbNoGraphics = val;
	gbIgnoreTextureErrors = true;
	system_specs.material_hardware_override = 1;
}

bool gfxIsInactiveApp(void)
{
	int i;
	bool active=false;
	for (i=0; i<eaSize(&gfx_state.devices); i++) 
		if (gfx_state.devices[i] && gfx_state.devices[i]->input_device)
			if (!inpIsInactiveApp(gfx_state.devices[i]->input_device))
				active = true;
	return !active;
}

bool gfxSkipSystemSpecsCheck(void)
{
	return gfx_state.skip_system_specs_check;
}


void gfxGetSettingsString(char *buf, size_t buf_size)
{
	int i;
	bool bAnyFeatureEnabled=false;
	U32 renderSize[2];
	// TODO: MSAA
	gfxGetRenderSizeFromScreenSize(renderSize);
	sprintf_s(SAFESTR2(buf), "%dx%d %s %s Vsync:%s RenderSize:%dx%d\n", gfx_state.screenSize[0], gfx_state.screenSize[1], gfx_state.settings.device_type, gfxSettingsIsFullscreen()?"Fullscreen":"Windowed", gfxSettingsIsVsync()?"On":"Off", renderSize[0], renderSize[1]);
	strcatf_s(SAFESTR2(buf), "DynLights:%d Textures:%1.1f/%1.1f WorldDetail:%1.2f TerrainDetail:%1.2f EntityDetail:%1.2f MaxLightsPerObj:%d MaxShadowedLights:%d Aniso:%d PreloadMats:%d BloomQuality:%d\n",
		gfx_state.settings.dynamicLights,
		gfx_state.settings.worldTexLODLevel, gfx_state.settings.entityTexLODLevel,
		gfx_state.settings.worldDetailLevel,
		gfx_state.settings.terrainDetailLevel,
		gfx_state.settings.entityDetailLevel,
		gfx_state.settings.maxLightsPerObject,
		gfx_state.settings.maxShadowedLights,
		gfx_state.settings.texAniso,
		gfx_state.settings.preload_materials && gfx_state.allow_preload_materials,
		gfx_state.settings.bloomQuality);
	strcatf_s(SAFESTR2(buf), "RenderThread:%d FRStabilizer:%d HighDetail:%d HighFillDetail:%d Scattering:%d SSAO:%d DXTNonPow2:%d HighQualityDOF:%d\n",
		gfx_state.settings.renderThread,
		gfx_state.settings.frameRateStabilizer,
		gfx_state.settings.draw_high_detail,
		gfx_state.settings.draw_high_fill_detail,
		gfx_state.settings.scattering,
		gfx_state.settings.ssao,
		gfx_state.dxt_non_pow2,
		gfx_state.settings.highQualityDOF);
	strcatf_s(SAFESTR2(buf), "SoftParticles:%d FXQuality:%d LightingQuality:%d MaxReflection:%d 2BoneSkinng:%d MSAA:%d GPUFaPa:%d MaxDebris:%d PoissonShad:%d BloomMul:%.1f MinDepthBiasFix:%d\n",
		gfx_state.settings.soft_particles, 
		gfx_state.settings.fxQuality, 
		gfx_state.settings.lighting_quality, 
		gfx_state.settings.maxReflection,
		!gfx_state.settings.useFullSkinning,
		gfx_state.settings.antialiasing,
		gfx_state.settings.gpuAcceleratedParticles,
		gfx_state.settings.maxDebris,
		gfx_state.settings.poissonShadows,
		gfx_state.settings.bloomIntensity,
		gfx_state.settings.minDepthBiasFix);
	strcatf_s(SAFESTR2(buf), "HigherTailor:%d PerFrameSleep:%d MaxFPS:%d LenseFlare:%d VidMemMax:%d HWInstancing:%d\n",
		gfx_state.settings.higherSettingsInTailor,
		gfx_state.settings.perFrameSleep,
		gfx_state.settings.maxFps,
		gfx_state.settings.lensflare_quality,
		gfx_state.settings.videoMemoryMax,
		gfx_state.settings.hwInstancing);
	strcat_s(SAFESTR2(buf), "EnabledFeatures: ");

	// Add FEATURES
	for (i=0; i<32; i++) {
		if (gfxFeatureEnabled(1<<i)) {
			const char *featurename = StaticDefineIntRevLookup(GfxFeatureEnum, 1<<i);
			assert(featurename);
			if (bAnyFeatureEnabled)
				strcat_s(SAFESTR2(buf), " ");
			strcat_s(SAFESTR2(buf), featurename);
			bAnyFeatureEnabled = true;
		}
	}
	if (!bAnyFeatureEnabled)
		strcat_s(SAFESTR2(buf), "None");
	strcat_s(SAFESTR2(buf), "\n");
}

void gfxGetSettingsStringCSV(char *buf, size_t buf_size)
{
	gfxGetSettingsString(buf, buf_size);
	strchrReplace(buf, ' ', ',');
	strchrReplace(buf, '\t', ',');
	strchrReplace(buf, ':', ',');
	while (strstriReplace_s(SAFESTR2(buf), ",,", ","));
}


GfxVisualizationSettings *gfxGetVisSettings(void)
{
	return &gfx_state.debug.vis_settings;
}

void gfxWaterAgitate(F32 magnitude)
{
	gfx_state.water_ripple_scale = MIN( 1, magnitude );
}

bool gfxFeatureDesired(GfxFeature feature)
{
	return !!(gfx_state.features_allowed & feature);
}

bool gfxFeatureEnabledPublic(GfxFeature feature)
{
	return gfxFeatureEnabled(feature);
}


void gfxRequestScreenshot(char *pFileName, bool bInclude2DElements, GfxScreenshotCallBack *pCB, void *pUserData)
{
	if (pFileName)
	{
		strcpy(gfx_state.screenshot_filename, pFileName);
	}
	else
	{
		gfx_state.screenshot_filename[0] = 0;
	}
	gfx_state.screenshot_type = bInclude2DElements ? SCREENSHOT_WITH_DEBUG : SCREENSHOT_3D_ONLY;
	gfx_state.screenshot_CB = pCB;
	gfx_state.screenshot_CB_userData = pUserData;

	gfxResetFrameRateStabilizerCounts();
}

int gfxNumLoadsPending(bool includePatching)
{
	int texLoadsTotal = texLoadsPending(1);
	int geoLoadsTotal = geoLoadsPending(1) + gfxGeoNumLoadsPending();
	int shaderLoads = rdrShaderGetBackgroundShaderCompileCount() + gfxMaterialPreloadGetLoadingCount();
	int miscLoads = utilitiesLibLoadsPending() + worldZoneMapPatching(false) - ((!includePatching)?fileLoaderPatchesPending():0);

	return texLoadsTotal + geoLoadsTotal + shaderLoads + miscLoads;
}

bool gfxIsStillLoading(bool includePatching)
{
	return gfxNumLoadsPending(includePatching) != 0;
}

void gfxNotifyDoneLoading(void)
{
	gfx_state.allowAutoAlwaysOnTop = true;
	gfxResetFrameRateStabilizerCounts();
}

void gfxNotifyStartingLoading(void)
{
	gfx_state.allowAutoAlwaysOnTop = false;
	gfxResetFrameRateStabilizerCounts();
}

void gfxNotifyDeviceLost( RdrDevice* device )
{
	memlog_printf(NULL, "gfxNotifyDeviceLost sent");
	gfxHeadshotRefreshAll();
	gfxDynDrawModelClearAllBodysocks();
	gfxShadowFlushCubemapLookupSurfaceOnDeviceLoss();
	gfxResetFrameRateStabilizerCounts();

	if (!gfx_state.debug.disable_3d_texture_flush)
		texFreeAll3D();
}

void gfxDisplayParamsChanged( RdrDevice * device )
{
	if (gfx_state.gfxUIUpdateCallback)
		gfx_state.gfxUIUpdateCallback();
}

AUTO_COMMAND;
void gfxHammerNotifyDeviceLost()
{
	TimedCallback_Run( (TimedCallbackFunc)gfxNotifyDeviceLost, NULL, 0 );
}

void gfxSetRecordCamMatPostProcessFn( GfxRecordCamMatPostProcessFn fn )
{
	gfx_state.record_cam_pp_fn = fn;
}


static GfxIsInTailorCheck gfx_is_in_tailor;
void gfxSetIsInTailorFunc(GfxIsInTailorCheck func)
{
	gfx_is_in_tailor = func;
}

bool gfxInTailor(void)
{
	if (!gfx_state.settings.higherSettingsInTailor)
		return false; // pretend we're never in the tailor
	if (gfx_is_in_tailor)
		return gfx_is_in_tailor();
	return false;
}

RdrFMV *glob_fmv;

static void gfxFMVUpdateParams(void)
{
	if (glob_fmv && gfxGetActiveDevice() == gfxGetPrimaryDevice())
	{
		int screenw, screenh;
		int fmvw, fmvh;
		F32 aspect = gfxGetAspectRatio();
		F32 desiredw, desiredh;
		F32 scale;
		F32 pixelaspect;
		gfxGetActiveDeviceSize(&screenw, &screenh);
		pixelaspect = aspect / (screenw / (float)screenh);
		rdrFMVGetSize(glob_fmv, &fmvw, &fmvh);
		desiredw = fmvw;
		desiredh = fmvh*pixelaspect;
		scale = MIN(screenw / desiredw, screenh / desiredh);
		desiredw*=scale;
		desiredh*=scale;
		rdrFMVSetDrawParams(glob_fmv,
			(screenw - desiredw)/2, (screenh - desiredh)/2, desiredw / fmvw, desiredh / fmvh,
			1);
	}
}

// Plays the specified FMV
AUTO_COMMAND;
void gfxFMVPlayFullscreen(const char *name)
{
	if (glob_fmv)
	{
		rdrFMVClose(&glob_fmv);
	}
	glob_fmv = rdrFMVOpen(gfxGetPrimaryDevice(), name);
	if (glob_fmv)
	{
		rdrFMVPlay(glob_fmv, 0, 0, 1, 1, 1);
		gfxFMVUpdateParams();
	}
}

AUTO_COMMAND;
void gfxFMVClose(void)
{
	if (glob_fmv)
	{
		rdrFMVClose(&glob_fmv);
	}
}

bool gfxFMVDone(void)
{
	if (glob_fmv)
		return rdrFMVDone(glob_fmv);
	return true;
}

void gfxFMVSetVolume(F32 volume)
{
	if (glob_fmv)
	{
		rdrFMVSetVolume(glob_fmv, volume);
	}
}

void gfxDisableVisScale(bool bDisable)
{
	gfx_state.bDisableVisScale = bDisable;
}