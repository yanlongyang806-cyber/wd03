#include "GfxCommandParse.h"
#include "Expression.h"
#include "GraphicsLibPrivate.h"
#include "GfxTextures.h"
#include "GfxMaterials.h"
#include "GfxTerrain.h"
#include "GfxSky.h"
#include "RdrState.h"
#include "GfxConsole.h"
#include "WorldLib.h"
#include "FolderCache.h"
#include "file.h"
#include "RdrShader.h"
#include "GfxDebug.h"
#include "GfxSurface.h"
#include "GfxDrawFrame.h"
#include "GfxLights.h"
#include "GfxLightCache.h"
#include "Materials.h"
#include "earray.h"
#include "wlPerf.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

AUTO_RUN;
void initGfxState(void)
{
	gfx_state.near_plane_dist = 0.73f;
	gfx_state.far_plane_dist = 30000.f;
	gfx_state.force_fog_clip_dist = 1000.f;
	gfx_state.sky_fade_rate = 0.75f;
	gfx_state.time_rate = 0.2f;
	gfx_state.surface_frames_unused_max = 5;
	gfx_state.surface_frames_unused_max_lowres = 60*10; // Want at least 10 seconds worth, these are only a couple K, want to prevent fragmentation
	gfx_state.debug.overrideTime = -1;
	gfx_state.debug.wait_for_zocclusion = 2; // start immediately after camera controller runs
}

void genericSettingChangeCallback(Cmd *cmd, CmdContext *cmd_context)
{
	if (gfx_state.currentDevice)
	{
		rdr_state.echoShaderPreloadLog = 0; // various options that call this will cause new shaders to load - e.g. lightingQuality
		gfxApplySettings(gfx_state.currentDevice->rdr_device, &gfx_state.settings, ApplySettings_PastStartup, cmd_context->access_level);
	}
}


// Sets the latency, in frames, before GraphicsLib releases temp surfaces.
AUTO_CMD_INT(gfx_state.surface_frames_unused_max, surface_frames_unused_max) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_HIDE;

// Sets the latency, in frames, before GraphicsLib releases temp surfaces 64x64 or smaller.
AUTO_CMD_INT(gfx_state.surface_frames_unused_max_lowres, surface_frames_unused_max_lowres) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_HIDE;

// Disables all renderable depth textures to simulate low-end cards
AUTO_CMD_INT(rdr_state.disableDepthTextures, disableDepthTextures) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CALLBACK(genericSettingChangeCallback);

// Disables all surface types other than RGBA8 to simulate low-end cards
AUTO_CMD_INT(rdr_state.disableNonTrivialSurfaceTypes, disableNonTrivialSurfaceTypes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Sets distance to near clipping plane.
AUTO_CMD_FLOAT(gfx_state.near_plane_dist, nearPlane);

// Sets distance to far clipping plane.
AUTO_CMD_FLOAT(gfx_state.far_plane_dist, farPlane);

// Sets distance to the fog clip plane when visscale is set to 0.6
AUTO_CMD_FLOAT(gfx_state.force_fog_clip_dist, fogClipDist);

// Sets the cross fade rate of the sky from one frame to the next.
AUTO_CMD_FLOAT(gfx_state.sky_fade_rate, skyFadeRate);

// Sets the rate (in hours per second) that the time changes between the map's time blocks
AUTO_CMD_FLOAT(gfx_state.time_rate, timeRate);

AUTO_CMD_FLOAT(gfx_state.project_special_material_param, gfxSpecialParam);

// Forces the time used by the sky system to the specified value.  Set to -1 to disable.
AUTO_CMD_FLOAT(gfx_state.debug.overrideTime, overrideTime) ACMD_CMDLINE;

// Dev mode loading only one font
AUTO_CMD_INT(gfx_state.quickLoadFonts, quickLoadFonts) ACMD_CMDLINE ACMD_CATEGORY(CommandLine);

// Sets the maximum allowed framerate
AUTO_CMD_INT(gfx_state.settings.maxFps,maxfps) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Performance); 

// Adds a per-frame sleep to artificially reduce CPU/GPU usage to help with overheating (will also slow the game down)
AUTO_CMD_INT(gfx_state.settings.perFrameSleep,perFrameSleep) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Options); 

// Sets the maximum allowed framerate when the application is not in the foreground
AUTO_CMD_FLOAT(gfx_state.settings.maxInactiveFps,maxInactiveFps) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(UsefulCommandLine) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// If available, users higher detail settings when in character creation/customization interfaces
AUTO_CMD_INT(gfx_state.settings.higherSettingsInTailor,higherSettingsInTailor) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(UsefulCommandLine) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Sets the quality level for character textures.  Normal values range from 0.5 to 10.0.
AUTO_CMD_FLOAT(gfx_state.settings.entityTexLODLevel,entityTexLODLevel) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options) ACMD_CALLBACK(texLodLevelChangedCallback);

// Sets the quality level for world textures.  Normal values range from 0.5 to 10.0.
AUTO_CMD_FLOAT(gfx_state.settings.worldTexLODLevel,worldTexLODLevel) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options) ACMD_CALLBACK(texLodLevelChangedCallback);
void texLodLevelChangedCallback(CMDARGS)
{
	texResetReduceTimestamps();
	genericSettingChangeCallback(cmd, cmd_context);
}

// Sets the maximum amount of video memory (in hundreds of MB) we will try to use.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void videoMemoryMax(Cmd *cmd, CmdContext *cmd_context, int hundred_mbs)
{
	gfx_state.settings.videoMemoryMax = hundred_mbs;
	genericSettingChangeCallback(cmd, cmd_context);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(videoMemoryMax);
void videoMemoryMaxDisplay(void)
{
	conPrintf("videoMemoryMax: %d %s", gfx_state.settings.videoMemoryMax, gfx_state.settings.videoMemoryMax?"hundred MBs":"(auto)");
	conPrintf("/showmem 1 will give more details if over");
}

// Instructs GraphicsLib to unload every texture and model which is not use on this frame. Set to 2 to do so at every frame.
AUTO_CMD_INT(gfx_state.unloadAllNotUsedThisFrame, gfxUnloadAllNotUsedThisFrame) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Debug);

// Disables sky drawing
AUTO_CMD_INT(gfx_state.debug.no_sky, noSky) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Sets exposure to not change pixel values
AUTO_CMD_INT(gfx_state.debug.no_exposure, noExposure) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Shows relative performance of materials
AUTO_CMD_INT(gfx_state.debug.show_material_cost, showMaterialCost) ACMD_CATEGORY(Debug) ACMD_CALLBACK(showMaterialCostCallback);
void showMaterialCostCallback(Cmd *cmd, CmdContext *cmd_context)
{
	if (gfx_state.debug.show_material_cost) {
		rdr_state.disableShaderProfiling = 0;
		if (!rdr_state.disableShaderCache) {
			gfxShowPleaseWaitMessage("Please wait, enabling shader profiling...");
			rdr_state.disableShaderCache = 1;
			rdr_state.useShaderServer = 0;
			globCmdParse("reloadMaterials");
		}
	}
}

// Forces all templates to have at least this reflection level for debugging
AUTO_CMD_INT(gfx_state.debug.forceReflectionLevel, forceReflectionLevel) ACMD_CATEGORY(Debug) ACMD_CALLBACK(forceReflectionLevelCallback);
void forceReflectionLevelCallback(Cmd *cmd, CmdContext *cmd_context)
{
	gfxMaterialMaxReflectionChanged();
}

// Disables the "please wait" screen that comes up on shader reloads, etc
AUTO_CMD_INT(gfx_state.debug.no_please_wait, no_please_wait) ACMD_CATEGORY(Debug);

// Enables preloading with multiple lights for longer load times but slightly less stalls
AUTO_CMD_INT(gfx_state.debug.preload_multiple_lights, preload_multiple_lights) ACMD_CATEGORY(Debug) ACMD_COMMANDLINE;

// Shows various graphics debug information
AUTO_CMD_INT(gfx_state.debug.gfx_debug_info, gfxDebugInfo) ACMD_CMDLINE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Useful);

// Hacky anaglpyh image rendering mode
AUTO_CMD_INT(gfx_state.anaglyph_hack, anaglyph) ACMD_CMDLINE ACMD_CATEGORY(Options);

// Forces render thread to stall until a synchronous transaction is queued
AUTO_CMD_INT(gfx_state.debug.renderThreadDebug, renderThreadDebug) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(CommandLine) ACMD_CALLBACK(renderThreadDebugCallback);
void renderThreadDebugCallback(Cmd *cmd, CmdContext *cmd_context)
{
	if (gfx_state.currentDevice && gfx_state.currentDevice->rdr_device &&
		gfx_state.currentDevice->rdr_device->worker_thread)
	{
		wtSetDebug(gfx_state.currentDevice->rdr_device->worker_thread, gfx_state.debug.renderThreadDebug);
	}
}

// Displays simple timing bars instead of detailed profiler UI
AUTO_CMD_INT(gfx_state.debug.bShowTimingBars, showTimingBars) ACMD_CMDLINE ACMD_CATEGORY(Performance) ACMD_CATEGORY(Useful);

// Emits PIX labels for each skeleton model
AUTO_CMD_INT(gfx_state.debug.danimPIXLabelModels,danimPIXLabelModels); ACMD_CATEGORY(Performance) ACMD_CATEGORY(Debug);

// Smooth particle intersections with geometry by fading out near the intersection
AUTO_CMD_INT(gfx_state.settings.soft_particles,soft_particles) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(UsefulCommandLine) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Displays per-thread performance
AUTO_CMD_INT(gfx_state.debug.threadPerf, threadPerf) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Useful);

// Displays per-thread performance for all threads (/threadPerf shows only "interesting" threads)
AUTO_CMD_INT(gfx_state.debug.threadPerfAll, threadPerfAll) ACMD_CMDLINE ACMD_CATEGORY(Performance);

// Runs various NVPerfAPI functions to analyze a frame
AUTO_CMD_INT(gfx_state.debug.runNVPerf, runNVPerf) ACMD_CATEGORY(Performance);

// Disables objects going into the opaque_onepass bucket
AUTO_CMD_INT(gfx_state.debug.noOnepassObjects, noOnepassObjects) ACMD_CMDLINE ACMD_CATEGORY(Performance) ACMD_CATEGORY(Debug);

// Sets the distance at which things are put into the opaque_onepass bucket
AUTO_CMD_FLOAT(gfx_state.debug.onepassDistance, onepassDistance) ACMD_CMDLINE ACMD_CATEGORY(Performance) ACMD_CATEGORY(Debug);

// Sets the screen space requirement for world objects for the opaque_onepass bucket
AUTO_CMD_FLOAT(gfx_state.debug.onepassScreenSpace, onepassScreenSpace) ACMD_CMDLINE ACMD_CATEGORY(Performance) ACMD_CATEGORY(Debug);

// Prints messages whenever temporary surfaces are created or destroyed
AUTO_CMD_INT(gfx_state.debug.echoTempSurfaceCreation, echoTempSurfaceCreation) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables non-accesslevel 0 warnings
AUTO_CMD_INT(gfx_state.debug.no_nalz_warnings, noNalzWarnings) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Enables various texWords debug flags
AUTO_CMD_INT(gfx_state.debug.texWordVerbose, texWordVerbose) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Forces sorting of opaque objects by distance instead of shader/material/model
AUTO_CMD_INT(gfx_state.debug.sort_opaque, sort_opaque) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// 0 = scattering off, 1 = scattering on high res, 2 = scattering low res
AUTO_CMD_INT(gfx_state.settings.scattering, scattering) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// 0 = use log average luminance measurement, non-zero = use maximum luminance
AUTO_CMD_INT(gfx_state.settings.hdr_max_luminance_mode, hdr_max_luminance_adaptation) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

// 0 = simple targeting graphics, 1 = glowing outline/inline effect
AUTO_CMD_INT(gfx_state.target_highlight, target_highlight) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Enables and disables screen space ambient occlusion
AUTO_CMD_INT(gfx_state.settings.ssao, ssao) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Enables and disables soft shadows (poisson filtering)
AUTO_CMD_INT(gfx_state.settings.poissonShadows, softShadows) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Enables and disables soft shadows (poisson filtering)
AUTO_CMD_INT(gfx_state.settings.poissonShadows, poissonShadows) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Changes lens flare quality level. 0 = simple, 1 = soft z occlusion
AUTO_CMD_INT(gfx_state.settings.lensflare_quality, lensflare_quality) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Prints out the amount of time a stall took whenever a stall occurs
AUTO_CMD_INT(gfx_state.debug.printStallTimes, printStallTimes) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0);

// Enables a graph showing recent frame times
AUTO_CMD_INT(gfx_state.debug.fpsgraph_show, fpsgraph) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0);

// Enables a histogram of frame times
AUTO_CMD_INT(gfx_state.debug.fpsgraph_showHistogram, fpshisto) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0);

// Shows the number of frames of delay between the main loop and showing up on screen
AUTO_CMD_INT(gfx_state.debug.show_frame_delay, showFrameDelay) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

// Allows/disallows preloading of materials (option must be enabled as well)
AUTO_CMD_INT(gfx_state.allow_preload_materials, preloadMaterials) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CALLBACK(preloadMaterialsCallback);

// Enables preloading (or at least compiling) of all possible shader combinations (for build scripts)
AUTO_CMD_INT(rdr_state.compile_all_shader_types, compileAllShaderTypes) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CALLBACK(CompileAllShaderTypesCB);
void CompileAllShaderTypesCB(CMDARGS)
{
#if PLATFORM_CONSOLE
	//on xbox, ps3, never change compile_all_shader_types
	rdr_state.compile_all_shader_types = 0;
#else
	if (rdr_state.compile_all_shader_types) // Command line -compileAllShaderTypes option, set to defaults
		rdr_state.compile_all_shader_types = CompileShaderType_PC;
#endif
}

void gfxSetCompileAllShaderTypes(int iSet)
{
#if !PLATFORM_CONSOLE
	rdr_state.compile_all_shader_types = iSet;
#endif
}

// Enables compiling of xbox shaders
AUTO_COMMAND ACMD_NAME(compileXBOXshaders) ACMD_CMDLINE ACMD_CATEGORY(Debug);
void CompileXBOXShaders(int set)
{
#if !PLATFORM_CONSOLE
	if(set)
		rdr_state.compile_all_shader_types |= CompileShaderType_Xbox;
	else
		rdr_state.compile_all_shader_types &=~CompileShaderType_Xbox;
#endif
}

// Enables preloading of all shaders early, instead of after the login screen.  Increases initial load time, but decreases load time into the first map.
AUTO_CMD_INT(gfx_state.preload_all_early, preloadAllEarly) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_HIDE ACMD_ACCESSLEVEL(0);

void preloadMaterialsCallback(CMDARGS)
{
	if (gfx_state.allow_preload_materials) {
		gfx_state.force_preload_materials = 1;
	}
	if (gfx_state.currentDevice) {
		bool b=false;
		if (world_perf_info.in_time_misc) {
			wlPerfEndMiscBudget();
			b = true;
		}
		gfxPreloadCheckStartGlobal();
		gfxPreloadCheckStartMapSpecific(false);
		if (b)
			wlPerfStartMiscBudget();
	}
}

// Used to disable erroring on non-preloaded materials
AUTO_CMD_INT(gfx_state.debug.error_on_non_preloaded_materials, errorOnNonPreloadedMaterials) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disable setting the window to always on top while in the foreground
AUTO_CMD_INT(gfx_state.disableAutoAlwaysOnTop, disableAutoAlwaysOnTop) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);

// Enables visualizing the view volume culling
AUTO_CMD_INT(gfx_state.debug.frustum_debug.frustum_debug_mode, frustum_debug_mode) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Tunes depth-of-field focal distance parameters for debugging; override current values from sky file.
AUTO_COMMAND ACMD_CATEGORY(Debug);
void dof_debug( F32 nearDist, F32 focusDist, F32 farDist )
{
	gfx_state.debug.dof_debug.nearDist = nearDist;
	gfx_state.debug.dof_debug.focusDist = focusDist;
	gfx_state.debug.dof_debug.farDist = farDist;
}

// Tunes depth-of-field parameters for debugging; override current values from sky file.
AUTO_COMMAND ACMD_CATEGORY(Debug);
void dof_debug_values( F32 nearValue, F32 focusValue, F32 farValue, F32 skyValue )
{
	gfx_state.debug.dof_debug.nearValue = nearValue;
	gfx_state.debug.dof_debug.focusValue = focusValue;
	gfx_state.debug.dof_debug.farValue = farValue;
	gfx_state.debug.dof_debug.skyValue = skyValue;
}

// Sets the percentage of the display resolution to render the 3D world at
AUTO_COMMAND ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void renderScale(F32 scalar)
{
	if (scalar >= 0 && scalar < 16) {
		U32 oldRenderSize[2];
		U32 newRenderSize[2];
		gfxGetRenderSizeFromScreenSize(oldRenderSize);
		setVec2(gfx_state.settings.renderScale, scalar, scalar);
		setVec2(gfx_state.settings.renderScaleSetBySize, 0, 0);
		gfxGetRenderSizeFromScreenSize(newRenderSize);

		if (oldRenderSize[0] != newRenderSize[0] || oldRenderSize[1] != newRenderSize[1])
		{
			gfxFreeSpecificSizedTempSurfaces(gfx_state.currentDevice, oldRenderSize, true);
		}
	}
}

// Sets the percentage of the horizontal display resolution to render the 3D world at
AUTO_CMD_FLOAT(gfx_state.settings.renderScale[0], renderScaleX) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(renderScaleXCallback) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void renderScaleXCallback(CMDARGS)
{
	gfx_state.settings.renderScaleSetBySize[0] = 0;
}

// Sets the percentage of the vertical display resolution to render the 3D world at
AUTO_CMD_FLOAT(gfx_state.settings.renderScale[1], renderScaleY) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(renderScaleYCallback) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void renderScaleYCallback(CMDARGS)
{
	gfx_state.settings.renderScaleSetBySize[1] = 0;
}


// Sets the pixel resolution to render the 3D world at
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void renderSize(int X, int Y)
{
	if (X > 0 && Y > 0) {
		U32 oldRenderSize[2];
		U32 newRenderSize[2];
		gfxGetRenderSizeFromScreenSize(oldRenderSize);
		setVec2(gfx_state.settings.renderSize, X, Y);
		setVec2(gfx_state.settings.renderScaleSetBySize, 1, 1);
		gfxGetRenderSizeFromScreenSize(newRenderSize);

		if (oldRenderSize[0] != newRenderSize[0] || oldRenderSize[1] != newRenderSize[1])
		{
			gfxFreeSpecificSizedTempSurfaces(gfx_state.currentDevice, oldRenderSize, true);
		}
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(renderSize) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(renderScale);
void getRenderSize(void) {
	int i;
	for (i=0; i<2; i++) {
		U32 size;
		F32 scale;
		if (gfx_state.settings.renderScaleSetBySize[i]) {
			size = gfx_state.settings.renderSize[i];
			scale = size / (F32)gfx_activeSurfaceSize[i];
		} else {
			scale = gfx_state.settings.renderScale[i]?gfx_state.settings.renderScale[i]:1;
			size = scale * gfx_activeSurfaceSize[i];
		}
		conPrintf("%c : %d (%1.2f%%)", "XY"[i], size, scale*100);
	}
}

// Sets the horizontal pixel resolution to render the 3D world at
AUTO_CMD_INT(gfx_state.settings.renderSize[0], renderSizeX) ACMD_CALLBACK(renderSizeXCallback) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void renderSizeXCallback(CMDARGS)
{
	gfx_state.settings.renderScaleSetBySize[0] = 1;
}

// Sets the horizontal pixel resolution to render the 3D world at
AUTO_CMD_INT(gfx_state.settings.renderSize[1], renderSizeY) ACMD_CALLBACK(renderSizeYCallback) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void renderSizeYCallback(CMDARGS)
{
	gfx_state.settings.renderScaleSetBySize[1] = 1;
}

// Use the specified renderer device type (valid options: Direct3D11, Direct3D9Ex, Direct3D9)
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Options);
void deviceType(Cmd *cmd, CmdContext *cmd_context, const char *str)
{
	gfx_state.settings.device_type = allocAddString(str);
	genericSettingChangeCallback(cmd, cmd_context);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(deviceType);
void deviceTypeGet(void)
{
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
	conPrintf("DeviceType setting : %s\n", gfx_state.settings.device_type);
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(gfx_state.devices, GfxPerDeviceState, gfxDevice)
	{
		const RdrDevice * rdrDevice = gfxDevice->rdr_device;
		int deviceTypeIndex = rdrDevice->display_nonthread.preferred_adapter;
		conPrintf("Device %d type: %s\n", igfxDeviceIndex, device_infos[deviceTypeIndex]->type);
	}
	FOR_EACH_END;
}

// Use the Direct3D 9 renderer device type
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Options);
void d3d9(Cmd *cmd, CmdContext *cmd_context, int useit)
{
	if (useit)
		gfx_state.settings.device_type = "Direct3D9";
	else
		gfx_state.settings.device_type = "Direct3D11";
	genericSettingChangeCallback(cmd, cmd_context);
}

// Use the Direct3D 9Ex renderer device type
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Options);
void d3d9ex(Cmd *cmd, CmdContext *cmd_context, int useit)
{
	if (useit)
		gfx_state.settings.device_type = "Direct3D9Ex";
	else
		gfx_state.settings.device_type = "Direct3D11";
	genericSettingChangeCallback(cmd, cmd_context);
}

// Use the Direct3D 11 renderer device type
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Options);
void d3d11(Cmd *cmd, CmdContext *cmd_context, int useit)
{
	if (useit)
		gfx_state.settings.device_type = "Direct3D11";
	else
		gfx_state.settings.device_type = "Direct3D9";
	genericSettingChangeCallback(cmd, cmd_context);
}


// Sets point sampling for /renderscale resizing
AUTO_CMD_INT(gfx_state.render_scale_pointsample, renderScalePointSample) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_CATEGORY(Options);

// Forces off-screen rendering, may resolve rendering issues on some platforms (WINE)
AUTO_CMD_INT(gfx_state.forceOffScreenRendering, forceOffScreenRendering) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Options);

// Enables a final postprocessing stage on everything including UI
AUTO_CMD_INT(gfx_state.ui_postprocess, ui_postprocess) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_CATEGORY(Debug) ACMD_MAXVALUE(3);

// Sets the aspect ratio.  Common values are 0 (auto), 4:3, 16:9 (widescreen TVs), 16:10 (widescreen monitors)
AUTO_COMMAND ACMD_NAME(aspectRatio) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Options);
void aspectRatio(ACMD_SENTENCE str, Cmd* cmd, CmdContext* cmd_context)
{
	char buf[1024];
	strcpy(buf, str);
	strchrReplace(buf, ':', '/');
	gfx_state.settings.aspectRatio = exprEvaluateRawStringSafe(buf);
	if (gfx_state.currentDevice)
		gfxApplySettings(gfx_state.currentDevice->rdr_device, &gfx_state.settings, ApplySettings_PastStartup, cmd_context->access_level); // Just to get it to save the values
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(aspectRatio);
void aspectRatioGet(void) {
	char buf[64];
	buf[0] = 0;
	if (gfx_state.settings.aspectRatio == 0) {
		int width;
		int height;
		gfxSettingsGetScreenSize(&gfx_state.settings, &width, &height);
		sprintf(buf, " (Auto; %d:%d = %1.3f)", width, height, width / (F32)height);
	} else if (nearf(gfx_state.settings.aspectRatio, 16.f/9.f)) {
		sprintf(buf, " (16:9)");
	} else if (nearf(gfx_state.settings.aspectRatio, 16.f/10.f)) {
		sprintf(buf, " (16:10)");
	} else if (nearf(gfx_state.settings.aspectRatio, 4.f/3.f)) {
		sprintf(buf, " (4:3)");
	}

	conPrintf("aspectRatio: %f%s", gfx_state.settings.aspectRatio, buf);
}

// Tints instanced objects.
AUTO_CMD_INT(gfx_state.debug.show_instanced_objects,show_instanced_objects) ACMD_CATEGORY(DEBUG);

// Disables z-prepass rendering
AUTO_CMD_INT(gfx_state.disable_zprepass, zprepassOff) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Forcibly enables z-prepass rendering
AUTO_CMD_INT(gfx_state.force_enable_zprepass, zprepassOn) ACMD_CMDLINE ACMD_CATEGORY(Debug);

AUTO_COMMAND ACMD_CMDLINE ACMD_CATEGORY(Debug);
void zprepass(int on)
{
	if (on == -1)
	{
		gfx_state.disable_zprepass = 0;
		gfx_state.force_enable_zprepass = 0;
	} else {
		gfx_state.disable_zprepass = !on;
		gfx_state.force_enable_zprepass = on;
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(zprepass) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(zprepassOn) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(zprepassOff);
void getZPrepass(void)
{
	conPrintf("Z-Prepass:");
	conPrintf("  force off: %d", gfx_state.disable_zprepass);
	conPrintf("  force on : %d", gfx_state.force_enable_zprepass);
	conPrintf("  currently: %d", gfx_state.zprepass_last_on);
}


// Enables or disables z-prepass debugging. Z-prepass debugging displays the z-prepass color buffers in debug thumbnails.
AUTO_CMD_INT(gfx_state.debug.zprepass, zprepass_debug) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Enables rendering z-prepass to an RGBA buffer instead of R16G16 on PC
AUTO_CMD_INT(gfx_state.debug.zprepass_to_rgba, zprepass_to_rgba) ACMD_CMDLINE ACMD_CATEGORY(Debug);

AUTO_CMD_INT(gfx_state.disable_cam_light, disable_cam_light) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Enables shadow buffer debugging (displays shadow buffer in a debug thumbnail).
AUTO_CMD_INT(gfx_state.debug.shadow_buffer, shadow_buffer_debug) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Enables rendering of various post-processing effects to RGBA buffers
AUTO_CMD_INT(gfx_state.debug.postprocess_rgba_buffer, postprocess_rgba_buffer) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Forces post-processing buffers to all use higher precision
AUTO_CMD_INT(gfx_state.debug.postprocess_force_float, postprocess_force_float) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Forces debug thumbnails to draw with point sampling instead of bilinear filtering.
AUTO_CMD_INT(gfx_state.debug.thumbnail.point_sample, thumbnailPointSample) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Skip sliding of thumbnails
AUTO_CMD_INT(gfx_state.debug.thumbnail.snap, thumbnailSnap) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Fix the thumbnail display to the currently selected one
AUTO_CMD_INT(gfx_state.debug.thumbnail.fixed, thumbnailFixed) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Set which thumbnail to be displayed full-screen
AUTO_CMD_INT(gfx_state.debug.thumbnail.thumbnail_display, thumbnailDisplay) ACMD_CMDLINE ACMD_MAXVALUE(3) ACMD_CATEGORY(Debug) ACMD_CALLBACK(thumbnailDisplayCallback) ACMD_NONSTATICINTERNALCMD;
void thumbnailDisplayCallback(Cmd *cmd, CmdContext *cmd_context)
{
	gfx_state.debug.thumbnail.fixed = true;
}


// Enables hack that seems to stabilize the framerate on some NVIDIA cards
AUTO_CMD_INT(gfx_state.settings.frameRateStabilizer, frameRateStabilizer) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(genericSettingChangeCallback);

// Auto-enables /frameRateStabilizer as it feels appropriate
AUTO_CMD_INT(gfx_state.settings.autoEnableFrameRateStabilizer, autoEnableFrameRateStabilizer) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(genericSettingChangeCallback);

void setMSAA(GfxAntialiasingMode mode, int samples)
{
	switch (mode) {
	case 0:
	case 1:
		if (samples >= 16)
			samples = 16;
		else if (samples >= 8)
			samples = 8;
		else
	case 2:
		if (samples >= 4)
			samples = 4;
		else if (samples >= 2)
			samples = 2;
		else
			samples = 1;
	}

	gfx_state.antialiasingMode = gfx_state.settings.antialiasingMode = mode;
	gfx_state.antialiasingQuality = gfx_state.settings.antialiasing = samples;
}

// Enables/disables multisample antialiasing
AUTO_COMMAND ACMD_NAME(msaa) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Options);
void setMSAACommand(int samples, Cmd* cmd, CmdContext* cmd_context)
{
	setMSAA(GAA_MSAA, samples);
	if (gfx_state.currentDevice)
		gfxApplySettings(gfx_state.currentDevice->rdr_device, &gfx_state.settings, ApplySettings_PastStartup, cmd_context->access_level);
}

// Enables/Disables TXAA
AUTO_COMMAND ACMD_NAME(txaa) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Options);
void setTXAACommand(int samples, Cmd* cmd, CmdContext* cmd_context)
{
	setMSAA(GAA_TXAA, samples);
	if (gfx_state.currentDevice)
		gfxApplySettings(gfx_state.currentDevice->rdr_device, &gfx_state.settings, ApplySettings_PastStartup, cmd_context->access_level);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(msaa);
void getMSAA(void)
{
	const char *mode = "None";
	switch (gfx_state.antialiasingMode) {
	xcase GAA_MSAA:
		mode = "MSAA";
	xcase GAA_TXAA:
		mode = "TXAA";
	}
	conPrintf("%s: %d", mode, gfx_state.antialiasingQuality);
}

// Controls if NVIDIA CSAA modes are used instead of MSAA
AUTO_COMMAND ACMD_NAME(nvidiaCSAAMode) ACMD_CATEGORY(Debug, Performance) ACMD_CMDLINE;
void setNvidiaCSAAModeCommand(int mode, Cmd* cmd, CmdContext* cmd_context)
{
	rdr_state.nvidiaCSAAMode = mode;
	//force all the temp surfaces to get remade
	gfxFreeTempSurfaces(gfx_state.currentDevice);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(nvidiaCSAAMode);
void getNvidiaCSAAMode(void)
{
	conPrintf("nvidiaCSAAMode: %d", rdr_state.nvidiaCSAAMode);
}

//Forces a reload of textures
AUTO_COMMAND ACMD_NAME(reloadTextures, texunload) ACMD_CATEGORY(Debug);
void reloadTexturesCallback(Cmd *cmd, CmdContext *cmd_context)
{
	texFreeAll();
}

//Puts the FolderCache in filesystem only mode
AUTO_COMMAND ACMD_CATEGORY(Debug);
void FolderCacheFilesystemOnly(int filesystem_only)
{
	if (filesystem_only)
		FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
	else
	{
        if(!g_xbox_local_hoggs_hack)
		    FolderCacheSetMode(FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC);
        else
            FolderCacheSetMode(FOLDER_CACHE_MODE_I_LIKE_PIGS);
	}
}

//Forces a reload of shaders and materials.
AUTO_COMMAND ACMD_NAME(reloadShaders,reloadMaterials) ACMD_CATEGORY(Debug);
void reloadShadersCallback(Cmd *cmd, CmdContext *cmd_context)
{
#if _XBOX
	if (fileIsUsingDevData())
	{
		if (!g_xbox_local_hoggs_hack)
			FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
		else {
			static bool warned_once=false;
			if (!warned_once) {
				gfxStatusPrintf("Trying to reload shaders, but we're in -xbUseLocalHoggs mode, which won't work right");
				gfxStatusPrintf("Run /FolderCacheFilesystemOnly to force into filesytem only mode (some file loads may fail)");
				warned_once = true;
			}
		}
	}
#endif
	if (gfx_state.pastStartup) {
		gfxMaterialsReloadAll();
	}
}

//Forces a reload of vertex shaders
AUTO_COMMAND ACMD_NAME(reloadVShaders) ACMD_CATEGORY(Debug);
void reloadVShadersCallback(Cmd* cmd, CmdContext* cmd_context )
{
    rdrReloadShaders( gfx_state.currentDevice->rdr_device);
}

void visScaleCallback(Cmd *cmd, CmdContext *cmd_context)
{
	if (gfx_state.currentDevice)
		gfxApplySettings(gfx_state.currentDevice->rdr_device, &gfx_state.settings, ApplySettings_PastStartup, cmd_context->access_level);
	worldLibSetLodScale(gfxGetLodScale(), gfx_state.settings.terrainDetailLevel, gfx_state.settings.worldLoadScale, gfx_state.settings.reduceFileStreaming);
}

void recalcLightsCallback(Cmd *cmd, CmdContext *cmd_context)
{
	globCmdParse("recalcLights");
}


// Sets bloom quality, range = [0, 3]
AUTO_COMMAND ACMD_NAME(bloomQuality) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void setBloomQuality(int quality, Cmd* cmd, CmdContext* cmd_context)
{
	bool needs_shader_reload = false;

#if _XBOX
	// Bloom quality 3 broken because of various changes which do not currently work with predicated tiling
	//  *might* work in conjunction with /renderscale 0.9 or less
	if (quality == 3)
		conPrintf("Bloom quality 3 not supported on Xbox");
	quality = CLAMP(quality, 0, 2);
#else
	quality = CLAMP(quality, 0, GBLOOM_MAX_BLOOM_DEFERRED);
#endif
	if (quality == GBLOOM_HIGH_BLOOM_FULLHDR && quality != gfx_state.settings.bloomQuality)
	{
		needs_shader_reload = true;
		rdr_state.allowMRTshaders = true;
	}
	gfx_state.settings.bloomQuality = quality;
	if (gfx_state.currentDevice)
		gfxApplySettings(gfx_state.currentDevice->rdr_device, &gfx_state.settings, ApplySettings_PastStartup, cmd_context->access_level);
	if (needs_shader_reload)
		reloadShadersCallback(NULL, NULL);
}

AUTO_COMMAND ACMD_NAME(fxQuality) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void setFxQuality(int quality, Cmd* cmd, CmdContext* cmd_context)
{
	quality = CLAMP(quality, 0, 2);
	gfx_state.settings.fxQuality = quality;
	if (gfx_state.currentDevice)
		gfxApplySettings(gfx_state.currentDevice->rdr_device, &gfx_state.settings, ApplySettings_PastStartup, cmd_context->access_level);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(bloomQuality);
void getBloomQuality(void)
{
	conPrintf("bloomQuality: %d", gfx_state.settings.bloomQuality);
}

// Reduces the resolution of textures to only use the reduced (mip-map) textures.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void reduce_mip(int amount)
{
	gfx_state.reduce_mip_override = amount;
	texResetReduceTimestamps();
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(reduce_mip);
void getReduceMip(void)
{
	conPrintf("reduce_mip_override: %d", gfx_state.reduce_mip_override);
	conPrintf("reduce_mip_world: %d (implict from world tex LOD level of %f)", gfx_state.reduce_mip_world, gfx_state.settings.worldTexLODLevel);
	conPrintf("reduce_mip_entity: %d (implict from entity tex LOD level of %f)", gfx_state.reduce_mip_entity, gfx_state.settings.entityTexLODLevel);
}

AUTO_CMD_INT(gfx_state.pssm_shadowmap_size, pssm_shadowmap_size) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Debug);

// Sets the amount of anisotropic filtering to use, reloads textures
AUTO_CMD_INT(gfx_state.settings.texAniso,texAniso) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(texAnisoCallback) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void texAnisoCallback(Cmd *cmd, CmdContext *cmd_context)
{
	texResetAnisotropic();
	if (gfx_state.currentDevice)
		gfxApplySettings(gfx_state.currentDevice->rdr_device, &gfx_state.settings, ApplySettings_PastStartup, cmd_context->access_level);
}

// Enables/disables high quality depth of field
AUTO_CMD_INT(gfx_state.settings.highQualityDOF,highQualityDOF) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Grabs the current frame to be shown with /gfxShowGrabbedFrame
AUTO_COMMAND ACMD_CATEGORY(Debug);
void gfxGrabFrame(U8 value)
{
	gfx_state.debug.framegrab_show = 0;
	gfx_state.debug.framegrab_grabnextframe = value;
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(gfxGrabFrame);
void gfxGrabFrameDefault()
{
	gfxGrabFrame(1);
}

// Shows the frame grabbed with /gfxGrabFrame
AUTO_CMD_FLOAT(gfx_state.debug.framegrab_show, gfxShowGrabbedFrame) ACMD_CATEGORY(Debug) ACMD_MAXVALUE(1);

// Selects next texture atlas to display with atlas_stats.
AUTO_COMMAND ACMD_CATEGORY(Debug);
void atlas_display_next(void)
{
	gfx_state.debug.atlas_display++;
}

// Selects previous texture atlas to display with atlas_stats.
AUTO_COMMAND ACMD_CATEGORY(Debug);
void atlas_display_prev(void)
{
	gfx_state.debug.atlas_display--;
}

static void enableFeature(GfxFeature feature, int enable, int access_level)
{
	rdr_state.echoShaderPreloadLog = 0; // This will liekly load a bunch of non-preloaded shader variations
	if (rdr_state.showDebugLoadingPixelShader==2)
		rdr_state.showDebugLoadingPixelShader = 0;

	if (enable) {
		gfx_state.settings.features_desired |= feature;
		gfx_state.settings.features_desired_by_command_line |= feature;
		gfx_state.settings.features_disabled_by_command_line &= ~feature;
	} else {
		gfx_state.settings.features_desired &= ~feature;
		gfx_state.settings.features_desired_by_command_line &= ~feature;
		gfx_state.settings.features_disabled_by_command_line |= feature;
	}
	if (gfx_state.currentDevice)
		gfxApplySettings(gfx_state.currentDevice->rdr_device, &gfx_state.settings, ApplySettings_PastStartup, access_level);
}

void toggleFeature(GfxFeature feature, int access_level)
{
	if (gfx_state.settings.features_desired & feature)
	{
		enableFeature(feature,false,access_level);
	} else {
		enableFeature(feature,true,access_level);
	}
}

// Enable lighting in linear space
AUTO_COMMAND ACMD_NAME(linearLighting) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void enableLinearLighting(int enable, Cmd* cmd, CmdContext* cmd_context)
{
	if (enable) {
		gfxSetFeatures(gfxGetFeatures() | GFEATURE_LINEARLIGHTING);
	}

	enableFeature(GFEATURE_LINEARLIGHTING, enable, cmd_context->access_level);
	texFreeAll();
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(linearLighting);
void linearLightingPrintIsEnabled(void) {
	conPrintf("linearLighting : %d", (int)gfxFeatureEnabled(GFEATURE_LINEARLIGHTING));
}

// Enable shadows
AUTO_COMMAND ACMD_NAME(shadows) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options) ACMD_CMDLINEORPUBLIC;
void enableShadows(int enable, Cmd* cmd, CmdContext* cmd_context)
{
	enableFeature(GFEATURE_SHADOWS, enable, cmd_context->access_level);
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(shadows);
void shadowsPrintIsEnabled(void) {
	conPrintf("shadows : %d", (int)gfxFeatureEnabled(GFEATURE_SHADOWS));
}

// Use a shadow buffer for shadowing (needs /shadows on as well)
AUTO_CMD_INT(gfx_state.shadow_buffer, shadowBuffer) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Use an ubershader for lighting instead of unique assembled shaders
AUTO_CMD_INT(gfx_state.uberlighting, uberLighting) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CALLBACK(uberlightingCallback);
void uberlightingCallback(Cmd *cmd, CmdContext *cmd_context)
{
	rdr_state.echoShaderPreloadLog = 0; // This will liekly load a bunch of non-preloaded shader variations
	if (gfx_state.uberlighting)
		gfx_state.cclighting = 0;
}

// When not using uberlighting, use predefined light combinations (Constrained Combinatorial Lighting)
AUTO_CMD_INT(gfx_state.cclighting, light_combos) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CALLBACK(cclightingCallback);

// When not using uberlighting, use predefined light combinations (Constrained Combinatorial Lighting)
AUTO_CMD_INT(gfx_state.cclighting, cclighting) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CALLBACK(cclightingCallback);
void cclightingCallback(Cmd *cmd, CmdContext *cmd_context)
{
	rdr_state.echoShaderPreloadLog = 0; // This will liekly load a bunch of non-preloaded shader variations
	if (gfx_state.cclighting)
		gfx_state.uberlighting = 0;
}

// Disables loading htex files
AUTO_CMD_INT(gfx_state.no_htex, no_htex) ACMD_HIDE ACMD_CATEGORY(Debug);

// Enables the vertex-lighting only path for low-end cards
AUTO_CMD_INT(gfx_state.vertexOnlyLighting, vertexOnlyLighting) ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Performance) ACMD_CMDLINEORPUBLIC;

// turns off drawing of vertex lights (but they still get created)
AUTO_CMD_INT(gfx_state.debug.disable_vertex_lights, disable_vertex_lights) ACMD_CATEGORY(Debug);

// turns off flushing of 3D textures after device loss. Flush and reload takes longer but fixes problems on ATI and Intel GPUs
AUTO_CMD_INT(gfx_state.debug.disable_3d_texture_flush, disable_3d_texture_flush) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(DEBUG) ACMD_CATEGORY(Performance);

// Sets various shader related rendering settings, only some values are allowed (0=low, 10=high)
AUTO_CMD_INT(gfx_state.settings.lighting_quality, lightingQuality) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Options) ACMD_CATEGORY(Performance) ACMD_CALLBACK(genericSettingChangeCallback);

// Changes the maximum allowed reflection quality, 0=no reflections, 1=simple reflections, 2=cubemaps
AUTO_CMD_INT(gfx_state.settings.maxReflection, maxReflection) ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Options) ACMD_CATEGORY(Performance) ACMD_CALLBACK(genericSettingChangeCallback);

// Forces skinning to only two bones to improve performance ("Simple Skinning" in the Options screen)
AUTO_CMD_INT(gfx_state.settings.useFullSkinning, useFullSkinning) ACMD_CATEGORY(Options) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Performance) ACMD_CALLBACK(genericSettingChangeCallback);

// Enables GPU-accelerated particle systems for increased performance on some systems
AUTO_CMD_INT(gfx_state.settings.gpuAcceleratedParticles, gpuAcceleratedParticles) ACMD_CATEGORY(Options) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Performance) ACMD_CALLBACK(genericSettingChangeCallback);

// Sets the maximum number of physics simulated debris objects
AUTO_CMD_INT(gfx_state.settings.maxDebris, maxDebris) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynFx) ACMD_CALLBACK(genericSettingChangeCallback);

// Enable water effects
AUTO_COMMAND ACMD_NAME(water) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options) ACMD_CMDLINEORPUBLIC;
void enableWater(int enable, Cmd* cmd, CmdContext* cmd_context)
{
    enableFeature(GFEATURE_WATER, enable, cmd_context->access_level);
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(water);
void waterPrintIsEnabled(void) {
    conPrintf( "water : %d", (int)gfxFeatureEnabled( GFEATURE_WATER ));
}
                                                                                                                   
// Enable comic outlining
AUTO_COMMAND ACMD_NAME(outlining) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void enableOutlining(int enable, Cmd* cmd, CmdContext* cmd_context)
{
	if (cmd_context->access_level > 0 && enable == 2)
		gfx_state.features_allowed |= GFEATURE_OUTLINING;
	enableFeature(GFEATURE_OUTLINING, enable, cmd_context->access_level);
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(outlining);
void outliningPrintIsEnabled(void) {
	conPrintf("outlining : %d", (int)gfxFeatureEnabled(GFEATURE_OUTLINING));
}

AUTO_COMMAND ACMD_NAME(dofToggle) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);
void dofToggle(Cmd* cmd, CmdContext* cmd_context)
{
	toggleFeature(GFEATURE_DOF, cmd_context->access_level);
}

// Enable depth-of-field rendering
AUTO_COMMAND ACMD_NAME(dof) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void enableDepthOfField(int enable, Cmd* cmd, CmdContext* cmd_context)
{
	enableFeature(GFEATURE_DOF, enable, cmd_context->access_level);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dof);
void dofPrintIsEnabled(void) {
	conPrintf("dof : %d", (int)gfxFeatureEnabled(GFEATURE_DOF));
}

// Enable postprocessing
AUTO_COMMAND ACMD_NAME(postProcessing) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);
void enablePostProcessing(int enable, Cmd* cmd, CmdContext* cmd_context)
{
	enableFeature(GFEATURE_POSTPROCESSING, enable, cmd_context->access_level);
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(postProcessing);
void postProcessingPrintIsEnabled(void) {
	conPrintf("postProcessing : %d", (int)gfxFeatureEnabled(GFEATURE_POSTPROCESSING));
}

// Various shader debugging parameters
AUTO_COMMAND ACMD_CMDLINE ACMD_CATEGORY(Debug);
void ShaderTest(const char *define_name ACMD_NAMELIST(g_all_shader_defines, STASHTABLE) )
{
	gfxShowPleaseWaitMessage("Please wait, reloading shaders...");
	rdrShaderSetTestDefine(0, define_name);
	reloadShadersCallback(NULL, NULL);
}

// Various shader debugging parameters
AUTO_COMMAND ACMD_CMDLINE ACMD_CATEGORY(Debug);
void ShaderTestN(int index, ACMD_NAMELIST(g_all_shader_defines, STASHTABLE) const char *define_name)
{
	gfxShowPleaseWaitMessage("Please wait, reloading shaders...");
	rdrShaderSetTestDefine(index, define_name);
	reloadShadersCallback(NULL, NULL);
}

// Used to overrides/disable a global shader define, usually only called by project-specific initialization code.
AUTO_COMMAND ACMD_CMDLINE ACMD_NAME(rdrShaderSetGlobalDefine) ACMD_CATEGORY(Debug);
void gfxShaderSetGlobalDefine(int index, ACMD_NAMELIST(g_all_shader_defines, STASHTABLE) const char *define_name)
{
	gfxShowPleaseWaitMessage("Please wait, reloading shaders...");
	rdrShaderSetGlobalDefine(index, define_name);
	reloadShadersCallback(NULL, NULL);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(ShaderTest) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(ShaderTestN);
void ShaderTestDisplay(void)
{
	int i;
	for (i=0; i<rdrShaderGetTestDefineCount(); i++) {
		const char *str = rdrShaderGetTestDefine(i);
		if (str) {
			if (i==0)
				conPrintf("ShaderTest : %s", str);
			else
				conPrintf("ShaderTestN %d : %s", i, str);
		}
	}

	for (i=0; i<rdrShaderGetGlobalDefineCount(); i++) {
		const char *str = rdrShaderGetGlobalDefine(i);
		if (str) {
			conPrintf("Global/Project Define: %s", str);
		}
	}
}

// Sets world detail scaling
AUTO_CMD_FLOAT(gfx_state.settings.worldDetailLevel,visscale) ACMD_CALLBACK(visScaleCallback) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options) ACMD_CMDLINEORPUBLIC;

// Sets world detail scaling
AUTO_CMD_FLOAT(gfx_state.settings.worldDetailLevel,WorldDetail) ACMD_CALLBACK(visScaleCallback) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Sets terrain detail scaling
AUTO_CMD_FLOAT(gfx_state.settings.terrainDetailLevel,TerrainDetail) ACMD_CALLBACK(visScaleCallback) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Sets entity detail scaling
AUTO_CMD_FLOAT(gfx_state.settings.entityDetailLevel,CharacterDetail) ACMD_CALLBACK(visScaleCallback) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Turns off splat shadows
AUTO_CMD_INT(gfx_state.settings.disableSplatShadows,disableSplatShadows) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(genericSettingChangeCallback) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Disables use of multiple render targets
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);
void disableMRT(int disable)
{
	rdrDisableFeature(FEATURE_MRT2, disable);
	rdrDisableFeature(FEATURE_MRT4, disable);
	globCmdParse("reloadshaders");
}


// Uses only SM20
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);
void useSM20(int enable)
{
	if (enable)
	{
		gfx_state.skip_system_specs_check = true;
		rdrDisableFeature(FEATURE_SM2B, 1);
		rdrDisableFeature(FEATURE_SM30, 1);
		rdrDisableFeature(FEATURE_VFETCH, 1);
		if( gfx_state.currentDevice && gfx_state.currentDevice->rdr_device ) {
			rdrUpdateMaterialFeatures(gfx_state.currentDevice->rdr_device);
		}
		globCmdParse("reloadshaders");
	}
}

// Uses only SM2B and lower
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);
void useSM2B(int enable)
{
	if (enable)
	{
		gfx_state.skip_system_specs_check = true;
		rdrDisableFeature(FEATURE_SM2B, 0);
		rdrDisableFeature(FEATURE_SM30, 1);
		rdrDisableFeature(FEATURE_VFETCH, 1);
		if( gfx_state.currentDevice && gfx_state.currentDevice->rdr_device ) {
			rdrUpdateMaterialFeatures(gfx_state.currentDevice->rdr_device);
		}
		globCmdParse("reloadshaders");
	}
}

// Disables use of shader model 2.0b and higher for the renderer only, leaving full-featured materials
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);
void rdrDisableSM2B(int disable)
{
	gfx_state.skip_system_specs_check = true;
	rdrDisableFeature(FEATURE_SM2B, disable);
	rdrDisableFeature(FEATURE_SM30, disable);
	rdrDisableFeature(FEATURE_VFETCH, disable);
	globCmdParse("reloadshaders");
}


// Uses full SM30
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);
void useSM30(int enable)
{
	if (enable)
	{
		gfx_state.skip_system_specs_check = true;
		rdrDisableFeature(FEATURE_SM2B, 0);
		rdrDisableFeature(FEATURE_SM30, 0);
		rdrDisableFeature(FEATURE_VFETCH, 0);
		if( gfx_state.currentDevice && gfx_state.currentDevice->rdr_device ) {
			rdrUpdateMaterialFeatures(gfx_state.currentDevice->rdr_device);
		}
		globCmdParse("reloadshaders");
	}
}

// Enables VS30 vertex texture fetch for terrain atlas heights
AUTO_COMMAND ACMD_NAME(enableVFetch) ACMD_CMDLINE ACMD_CATEGORY(Debug);
void enableVFetch(int enable)
{
	gfx_state.skip_system_specs_check = true;
	rdrDisableFeature( FEATURE_VFETCH, !enable );
	if( gfx_state.currentDevice && gfx_state.currentDevice->rdr_device ) {
		rdrUpdateMaterialFeatures(gfx_state.currentDevice->rdr_device);
	}
	globCmdParse("reloadVShaders");
}

// Disables sending of tangent space bases
AUTO_CMD_INT(gfx_state.noTangentSpace, noTangentSpace) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables prompting about old system specs at startup, even after new patches, etc
AUTO_CMD_INT(gfx_state.skip_system_specs_check, skipSystemSpecsCheck) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0);

// Reloads sky files
AUTO_COMMAND ACMD_CATEGORY(Debug);
void reloadSkies(void)
{
	gfxSkyReloadAllSkies();
}

// Sets the current camera's field of view
AUTO_COMMAND ACMD_NAME(fov) ACMD_CATEGORY(Debug);
void CmdSetFov(float fov)
{
	if (fov >= 1 && fov < 179)
		gfx_state.currentCameraController->default_projection_fov = gfx_state.currentCameraController->target_projection_fov = gfx_state.currentCameraController->projection_fov = fov;
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(fov) ACMD_HIDE;
void CmdSetFovError(void) {
	conPrintf("fov : %f\n", gfx_state.currentCameraController->default_projection_fov);
}

// Turns on wireframe rendering
AUTO_CMD_INT(gfx_state.wireframe,wireframe) ACMD_MAXVALUE(2) ACMD_CATEGORY(Useful);

// Turns on wireframe rendering for the sky
AUTO_CMD_INT(gfx_state.sky_wireframe,skyWireframe) ACMD_MAXVALUE(2) ACMD_CATEGORY(Useful);

// disables fog while the gfx clips like the fog is still on
AUTO_CMD_INT(gfx_state.debug.disableFog, disableFog) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// disables fog while the gfx clips like the fog is still on
AUTO_CMD_INT(gfx_state.debug.disableFog, noFog) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// turns off directional lights
AUTO_CMD_INT(gfx_state.debug.disableDirLights, disableDirLights) ACMD_CALLBACK(recalcLightsCallback) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// turns off searching for (but not updating/creating) dynamic (from fx) lights. Probabably use /dynamicLights instead
AUTO_CMD_INT(gfx_state.debug.disableDynamicLights, debugDisableDynamicLights) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// turns off sprites
AUTO_CMD_INT(gfx_state.debug.disableSprites, disableSprites) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Displays the stats for the texture atlas system
AUTO_CMD_INT(gfx_state.debug.atlas_stats, atlas_stats) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// Selects which texture atlas to display with atlas_stats
AUTO_CMD_INT(gfx_state.debug.atlas_display, atlas_display) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// Command line option to disable texture atlasing
AUTO_CMD_INT(gfx_state.debug.dont_atlas, dont_atlas) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Turn off auto lods
AUTO_CMD_INT(gfx_state.debug.no_auto_lods, no_auto_lods) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Disables Z-Occlusion
AUTO_CMD_INT(gfx_state.debug.noZocclusion, noZocclusion) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Selects between one or two Z-Occlusion rendering threads.
AUTO_CMD_INT(gfx_state.debug.two_zo_draw_threads, two_zo_draw_threads) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Waits for Z-Occlusion to finish before rendering the main view using the same camera.  1 starts in main action, 2 starts after camera controller is run.
AUTO_CMD_INT(gfx_state.debug.wait_for_zocclusion, wait_for_zocclusion) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Bezier debug
AUTO_CMD_INT(gfx_state.debug.draw_bezier_control_points, drawBezierControlPoints) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// Bezier debug
AUTO_CMD_INT(gfx_state.debug.no_draw_bezier, noDrawBezier) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// Disables displaying a warning about which monitor we're rendering on
AUTO_CMD_INT(gfx_state.debug.disable_multimon_warning, disable_multimon_warning) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug) ACMD_CMDLINEORPUBLIC;

// Checks and then repairs window size and placement if off the screen or too large to fit (usually called internally)
AUTO_CMD_INT(gfx_state.debug.check_window_placement, check_window_placement) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug) ACMD_HIDE ACMD_CMDLINEORPUBLIC;

// Shows a message when models are being binned
AUTO_CMD_INT(gfx_state.debug.show_model_binning_message, show_model_binning_message) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// turns off particle system
//AUTO_CMD_INT(gfx_state.debug.noparticles, noparticles) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// white particles
//AUTO_CMD_INT(gfx_state.debug.whiteParticles, whiteParticles) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// use white textures
AUTO_CMD_INT(gfx_state.debug.whiteTextures, whiteTextures) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// use "simple lighting" even on cards that support better
AUTO_CMD_INT(gfx_state.debug.simpleLighting, simpleLighting) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// use a simple material
AUTO_CMD_STRING(gfx_state.debug.simpleMaterials, simpleMaterials) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// override all textures
AUTO_CMD_STRING(gfx_state.debug.textureOverride, textureOverride) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// override textures with colored textures to show their distance values
AUTO_CMD_INT(gfx_state.debug.showTextureDistance, showTextureDistance) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// override textures with checkered textures to show their resolution
AUTO_CMD_INT(gfx_state.debug.showTextureDensity, showTextureDensity) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// texture name/wildcard to not override when /showTextureDensity is set
AUTO_CMD_STRING(gfx_state.debug.showTextureDensityExclude, showTextureDensityExclude) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// override textures to show which mip level is being used
AUTO_CMD_INT(gfx_state.debug.showMipLevels, showMipLevels) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Turn on/off loading textures near the camera focus, in addition to just near the camera.
AUTO_CMD_INT(gfx_state.texLoadNearCamFocus, texLoadNearCamFocus) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(DEBUG) ACMD_CATEGORY(Performace);

// Repeat each material preload for a given number of frames for debugging, print the material name.
AUTO_CMD_INT(gfx_state.debug.material_preload_debug, material_preload_debug) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);


// Disables clipping of terrain
AUTO_CMD_INT(gfx_state.debug.no_clip_terrain, noClipTerrain) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables drawing of terrain
AUTO_CMD_INT(gfx_state.debug.no_draw_terrain, noDrawTerrain) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Disables drawing of the static world
AUTO_CMD_INT(gfx_state.debug.no_draw_static_world, noDrawStaticWorld) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Disables drawing of the dynamics system
AUTO_CMD_INT(gfx_state.debug.no_draw_dynamics, noDynamics) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Shadow debugging
AUTO_CMD_INT(gfx_state.debug.shadow_debug, shadowDebug) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't do rendering into shadowmaps.  For performance testing.
AUTO_CMD_INT(gfx_state.debug.no_render_shadowmaps, noShadowRender) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Forces point light shadow culling to use a single sphere for all directions instead of multiple frustums.  For performance testing.
AUTO_CMD_INT(gfx_state.debug.disable_multiple_point_light_shadow_passes, disableMultiplePointLightShadowPasses) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Disables the shadowmap sliding scale quality degredation and forces all shadowmaps to the specified quality (0.2-1).
AUTO_CMD_FLOAT(gfx_state.debug.forceShadowQuality, forceShadowQuality) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Draws ragdoll shadow casters in visual pass as well as in shadow passes.
AUTO_CMD_INT(gfx_state.debug.drawRagdollShadowsForVisualPass, drawRagdollShadowsForVisualPass)  ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// WorldCell visualization
AUTO_CMD_INT(gfx_state.debug.world_cell, worldCellVisualize) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// WorldCell visualization, in 3D
AUTO_CMD_INT(gfx_state.debug.world_cell_3d, worldCellVisualize3D) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// Postprocessing debugging
AUTO_CMD_INT(gfx_state.debug.postprocessing_debug, postProcessingDebug) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Useful);

// Postprocessing LUT debugging
AUTO_CMD_INT(gfx_state.debug.postprocessing_lut_debug, postProcessingLUTDebug) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Useful);

// Bloom debugging
AUTO_CMD_INT(gfx_state.debug.bloom_debug, bloomDebug) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Useful);

// Debugging auxiliary visual pass, e.g. for target highlight rendering
AUTO_CMD_INT(gfx_state.debug.aux_visual_pass_debug,aux_visual_pass_debug);

// Debugging allocated surfaces
AUTO_CMD_INT(gfx_state.debug.surface_debug,surface_debug);

// TXAA debug mode. 1 displays the TXAA snapshots. 2-4 activate the various built-in debugging modes.
AUTO_CMD_INT(gfx_state.debug.txaa_debug, txaa_debug) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Useful);

// Deferred lighting debugging
AUTO_CMD_INT(gfx_state.debug.deferredlighting_debug, deferredLightingDebug) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Useful);

// Show light volumes used for deferred lighting
AUTO_CMD_INT(gfx_state.debug.show_light_volumes, show_light_volumes) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// occlusion debugging
AUTO_CMD_INT(gfx_state.debug.zocclusion_enabledebug, zocclusionDebug) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// occlusion debugging
AUTO_CMD_INT(gfx_state.debug.zocclusion_hier_max, zocclusionHeirMax) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);


// Room debugging
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void showRooms(int show_rooms) 
{
	gfx_state.debug.show_rooms = show_rooms;
	if (show_rooms)
		gfx_state.debug.show_room_partitions = 0;
}

// MSAA Disabling
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);
void msaaDisable(void)
{
	gfx_state.antialiasingQuality = 1;
}

// Room partition debugging
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void showRoomPartitions(int show_partitions) 
{
	gfx_state.debug.show_room_partitions = show_partitions;
	if (show_partitions)
		gfx_state.debug.show_rooms = 0;
}

//Show the graph used to priortize indoor shadow casters. 0 = off, 1 = shadow lights, 2 = all lights, 3 = animate creation
AUTO_CMD_INT(gfx_state.debug.show_room_shadow_graph, showRoomShadowGraph) ACMD_CATEGORY(Debug);

// Turns off restrictions on which objects are used as occluders
AUTO_CMD_INT(gfx_state.debug.zocclusion_norestrict, zocclusionNoRestrict) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Turns on display of terrain tile bounding boxes
AUTO_CMD_INT(terrain_state.show_terrain_bounds, show_terrain_bounds) ACMD_CATEGORY(Debug);

// Turns on display of terrain tile occlusion mesh
AUTO_CMD_INT(terrain_state.show_terrain_occlusion, show_terrain_occlusion) ACMD_CATEGORY(Debug);

// Turns on terrain occlusion
AUTO_CMD_INT(terrain_state.terrain_occlusion_disable, terrain_occlusion_disable) ACMD_CATEGORY(Debug);

// Displays frame rate
AUTO_CMD_FLOAT(gfx_state.showfps, showfps) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance);

// Displays process memory usage
AUTO_CMD_INT(gfx_state.showmem, showmem) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance);

// Displays current virtual time
AUTO_CMD_INT(gfx_state.showTime, showTime) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Displays the camera's position
AUTO_CMD_INT(gfx_state.showCamPos, showCamPos) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug); // May want this to be AccessLevel 9 when we're not displaying it by default

// Displays the camera's position in terrain coordinates
AUTO_CMD_INT(gfx_state.showTerrainPos, showTerrainPos) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Enables second (improved) version of HDR algorithm.
AUTO_CMD_INT(gfx_state.debug.hdr_2,hdr_2) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Forces direct readback of luminance from the 1x1 RG16F luminance texture, for debugging
// luminance measurements, and testing effects of latency in using queries.
AUTO_CMD_INT(gfx_state.debug.hdr_use_immediate_luminance_measurement,hdr_use_immediate_luminance_measurement) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Forces a particular luminance measurement value, to make HDR adapt to a given luminance value.
AUTO_CMD_FLOAT(gfx_state.debug.hdr_force_luminance_measurement,hdr_force_luminance_measurement) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Locks the physical luminance <-> LDR image transform to a particular luminance.
AUTO_CMD_FLOAT(gfx_state.debug.hdr_lock_ldr_xform,hdr_lock_ldr_xform);

// Locks the physical luminance <-> HDR image transform to a particular luminance.
AUTO_CMD_FLOAT(gfx_state.debug.hdr_lock_hdr_xform,hdr_lock_hdr_xform);

// Reserved. Changes the luminance threshold for pixels to be considered in the high part of the dynamic range.
AUTO_CMD_FLOAT(gfx_state.debug.hdr_luminance_point,hdr_luminance_point) ACMD_ACCESSLEVEL(9);

// Displays current exposure transform
AUTO_CMD_INT(gfx_state.debug.show_exposure_transform, showexposuretransform) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// Displays current adapted and desired light ranges
AUTO_CMD_INT(gfx_state.debug.show_light_range, showLightRange) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// Displays sampled luminance history
AUTO_CMD_INT(gfx_state.debug.show_luminance_history, showLuminanceHistory) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// Turns off all lighting on objects and sets the ambient to the specified value
AUTO_CMD_FLOAT(gfx_state.debug.force_unlit_value, unlit) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug) ACMD_CALLBACK(gfxForceUnlitValue);
void gfxForceUnlitValue(CMDARGS)
{
	MAX1(gfx_state.debug.force_unlit_value, 0);

	gfxSetUnlitLightValue(gfx_state.debug.force_unlit_value ? gfx_state.debug.force_unlit_value : 1);
	gfxInvalidateAllLightCaches();
}

// sets the maximum lights per object
AUTO_CMD_INT(gfx_state.settings.maxLightsPerObject, maxLightsPerObject) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options) ACMD_CMDLINEORPUBLIC;

// sets the maximum shadow casting lights per frame
AUTO_CMD_INT(gfx_state.settings.maxShadowedLights, maxShadowedLights) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options) ACMD_CMDLINEORPUBLIC;

// Enables dynamic lights
AUTO_CMD_INT(gfx_state.settings.dynamicLights, dynamicLights) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Enables high detail objects
AUTO_CMD_INT(gfx_state.settings.draw_high_detail, highDetail) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Enables high fill detail objects
AUTO_CMD_INT(gfx_state.settings.draw_high_fill_detail, highFillDetail) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance) ACMD_CATEGORY(Options);

// Shows per frame graphics stat counters
AUTO_CMD_INT(gfx_state.debug.show_frame_counters, show_frame_counters) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Performance, Debug);

// Shows per frame draw list stat counters
AUTO_CMD_INT(gfx_state.debug.show_draw_list_histograms, show_draw_list_histograms) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Performance, Debug);

// Shows per frame file access counters
AUTO_CMD_INT(gfx_state.debug.show_file_counters, show_file_counters) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Performance, Debug);

// Shows per frame vertex shader estimated time
AUTO_CMD_INT(gfx_state.debug.show_vs_time, show_vs_time) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Performance, Debug);

// Emulate frame time taken on old cards
AUTO_CMD_INT(gfx_state.debug.emulate_vs_time, emulate_vs_time) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Performance, Debug);

// If emulating frame time taken on old cards, use Intel performance numbers
AUTO_CMD_INT(gfx_state.debug.emulate_vs_time_use_old_intel, emulate_vs_time_use_old_intel) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Performance, Debug);

// Shows per frame high level timers
AUTO_CMD_INT(gfx_state.debug.show_frame_times, show_frame_times) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Performance, Debug);

// Shows which stages are currently enabled
AUTO_CMD_INT(gfx_state.debug.show_stages, show_stages) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Performance, Debug);

// Turns on/off volume fog
AUTO_CMD_INT(gfx_state.volumeFog, volume_fog) ACMD_ACCESSLEVEL(9) ACMD_CALLBACK(genericSettingChangeCallback) ACMD_CMDLINE ACMD_CATEGORY(Performance);

// Changes whether screenshots are saved before or after doing renderscaling
AUTO_CMD_INT(gfx_state.screenshot_after_renderscale, screenshotAfterRenderscale) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);

// used as default screenshot name
static char s_ScreenshotName[1024] = "";
void gfxScreenshotName(const char *name)
{
	strcpy(s_ScreenshotName, name);
}

static	char	last_datestr[100];
static	int		subsecond;

static char* gfxScreenshotDefaultFname(char* buffer, int buffer_size)
{
	if(s_ScreenshotName[0])
	{
		sprintf_s(SAFESTR2(buffer), "%s.tga", s_ScreenshotName);
	}
	else
	{
		char *s;
		char datestr[100];

		timeMakeLocalDateStringFromSecondsSince2000(datestr,timeSecondsSince2000());
		for(s=datestr;*s;s++)
		{
			if (*s == ':' || *s == ' ')
				*s = '-';
		}
		if (stricmp(datestr,last_datestr)==0)
			sprintf_s(SAFESTR2(buffer),"screenshot_%s_%d.tga",datestr,++subsecond);
		else
		{
			sprintf_s(SAFESTR2(buffer),"screenshot_%s.tga",datestr);
			subsecond = 0;
		}
		strcpy(last_datestr,datestr);
	}

    return buffer;
}

// Save a screenshot
AUTO_COMMAND ACMD_NAME(screenshot) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Useful);
void gfxSaveScreenshot3dOnly(const char *filename)
{
	if (!filename)
		return;

	changeFileExt(filename, ".tga", gfx_state.screenshot_filename);
	gfx_state.screenshot_type = SCREENSHOT_3D_ONLY;
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(screenshot);
void gfxSaveScreenshot3dOnlyDefault(void)
{
	char fname[1000];
	gfxSaveScreenshot3dOnly(gfxScreenshotDefaultFname(SAFESTR(fname)));
}

// Save a screenshot of a thumbnail.
AUTO_COMMAND ACMD_NAME(thumbnailScreenshot) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);
void gfxThumbnailsScreenshot(const char *title, const char *filename)
{
	estrCopy2(&gfx_state.debug.thumbnail.strScreenshotRequestTitle, title);
	estrCopy2(&gfx_state.debug.thumbnail.strScreenshotFilename, filename);
}


// Save a screenshot with the UI included
AUTO_COMMAND ACMD_NAME(screenshot_ui) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Useful);
void gfxSaveScreenshotWithUI(const char *filename)
{
	if (!filename)
		return;
	changeFileExt(filename, ".tga", gfx_state.screenshot_filename);
	gfx_state.screenshot_type = SCREENSHOT_WITH_DEBUG;
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(screenshot_ui);
void gfxSaveScreenshotWithUIDefault(void)
{
	char fname[1000];
    gfxSaveScreenshotWithUI(gfxScreenshotDefaultFname(SAFESTR(fname)));
}

// Save a screenshot with the depth only
AUTO_COMMAND ACMD_NAME(screenshot_depth) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Useful);
void gfxSaveScreenshotDepth(const char *filename, float min, float max)
{
	if (!filename)
		return;
	changeFileExt(filename, ".tga", gfx_state.screenshot_filename);
	gfx_state.screenshot_type = SCREENSHOT_DEPTH;
	gfx_state.screenshot_depth_min = min;
	gfx_state.screenshot_depth_max = max;
	gfx_state.screenshot_depth_power = 1;
}

// Check if a screenshot save is pending for this frame
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsScreenshotPending);
bool exprIsScreenshotPending()
{
	return gfx_state.screenshot_type != SCREENSHOT_NONE;
}

// Sets the min depth for thumbnail depth copies
AUTO_CMD_FLOAT(gfx_state.screenshot_depth_min, thumbnail_depth_min) ACMD_CATEGORY(Debug);
// Sets the max depth for thumbnail depth copies
AUTO_CMD_FLOAT(gfx_state.screenshot_depth_max, thumbnail_depth_max) ACMD_CATEGORY(Debug);
// Sets the depth exponent for thumbnail depth copies
AUTO_CMD_FLOAT(gfx_state.screenshot_depth_power, thumbnail_depth_power) ACMD_CATEGORY(Debug);

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(screenshot_depth);
void gfxSaveScreenshotDepthDefault(void)
{
	char fname[1000];
	gfxSaveScreenshotDepth(gfxScreenshotDefaultFname(SAFESTR(fname)), 0.1, 0.25);
}

// Save a screenshot
AUTO_COMMAND ACMD_NAME(screenshot_jpg) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Useful);
void gfxSaveJPGScreenshot3dOnly(const char *filename)
{
	if (!filename)
		return;
	changeFileExt(filename, ".jpg", gfx_state.screenshot_filename);
	gfx_state.screenshot_type = SCREENSHOT_3D_ONLY;
	gfx_state.jpegQuality = 95;
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(screenshot_jpg);
void gfxSaveJPGScreenshot3dOnlyDefault(void)
{
	char fname[1000];
	gfxSaveJPGScreenshot3dOnly(gfxScreenshotDefaultFname(SAFESTR(fname)));
}

void gfxSaveJPGScreenshot3dOnlyOverride(const char *filename, int jpegQuality)
{
	if (!filename)
		return;
	changeFileExt(filename, ".jpg", gfx_state.screenshot_filename);
	gfx_state.screenshot_type = SCREENSHOT_3D_ONLY;
	gfx_state.jpegQuality = jpegQuality;
}

// Save a screenshot with the UI included
AUTO_COMMAND ACMD_NAME(screenshot_ui_jpg) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Useful);
void gfxSaveJPGScreenshotWithUI(const char *filename)
{
	if (!filename)
		return;
	changeFileExt(filename, ".jpg", gfx_state.screenshot_filename);
	gfx_state.screenshot_type = SCREENSHOT_WITH_DEBUG;
	gfx_state.jpegQuality = 95;
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(screenshot_ui_jpg);
void gfxSaveJPGScreenshotWithUIDefault(void)
{
	char fname[1000];
	gfxSaveJPGScreenshotWithUI(gfxScreenshotDefaultFname(SAFESTR(fname)));
}

void gfxSaveJPGScreenshotWithUIOverride(const char *filename, int jpegQuality)
{
	if (!filename)
		return;
	changeFileExt(filename, ".jpg", gfx_state.screenshot_filename);
	gfx_state.screenshot_type = SCREENSHOT_WITH_DEBUG;
	gfx_state.jpegQuality = jpegQuality;
}

void gfxSaveJPGScreenshotWithUIOverrideCallback(const char *filename, int jpegQuality, GfxScreenshotCallBack *callback, void *userdata)
{
	if (!filename)
		return;
	changeFileExt(filename, ".jpg", gfx_state.screenshot_filename);
	gfx_state.screenshot_type = SCREENSHOT_WITH_DEBUG;
	gfx_state.jpegQuality = jpegQuality;
	
	if(callback) {
		gfx_state.screenshot_CB = callback;
		gfx_state.screenshot_CB_userData = userdata;
	}
}

// Save a set of cubemap images of the current scene
AUTO_COMMAND ACMD_NAME(makeCubemap) ACMD_CATEGORY(Debug);
void gfxSaveCubeMap(const char *filename)
{
	if (!filename)
		return;
	changeFileExt(filename, "", gfx_state.make_cubemap_filename);
	gfx_state.make_cubemap = 1;
}

// Save a screenshot of a material rendered with current in-game settings, baking all the settings into an output texture.
AUTO_CMD_STRING(gfx_state.screenshot_material_name, screenshot_material) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// Run the game in "Safe Mode", with lowest graphics settings to ensure the ability to get into the game to fix bad settings
AUTO_CMD_INT(gfx_state.safemode, safemode) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Options) ACMD_COMMANDLINE;

// Sets or displays the current screen resolution.  Usage: /screen Width Height
AUTO_COMMAND ACMD_NAME(screen) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Useful) ACMD_CATEGORY(Performance);
void gfxScreenSetSize(int width, int height)
{
#if !PLATFORM_CONSOLE
	if (!gfx_state.currentDevice) {
		return;
	} else {
		DisplayParams display_settings = gfx_state.currentDevice->rdr_device->display_nonthread;
		display_settings.width = width;
		display_settings.height = height;
		// DX11 TODO DJR - make this support current fullscreen mode by searching for a 
		// new fullscreen mode matching the given res so this command can switch fullscreen modes
		display_settings.fullscreen = 0;
		rdrSetSize(gfx_state.currentDevice->rdr_device, &display_settings);
		gfxSettingsSaveSoon();
	}
#endif
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(screen);
void gfxScreenGetSize(void) {
	conPrintf("Screen resolution: %d %d", gfx_activeSurfaceSize[0], gfx_activeSurfaceSize[1]);
}

// Sets the current screen position and resolution.  Usage: /screen_pos_size X Y Width Height
AUTO_COMMAND ACMD_NAME(screen_pos_size) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Performance);
void gfxScreenSetPosAndSize(int x0, int y0, int width, int height)
{
#if !PLATFORM_CONSOLE
	if (!gfx_state.currentDevice) {
		return;
	} else {
		DisplayParams display_settings = gfx_state.currentDevice->rdr_device->display_nonthread;
		display_settings.xpos = x0;
		display_settings.ypos = y0;
		display_settings.width = width;
		display_settings.height = height;
		// DX11 TODO DJR - make this support current fullscreen mode by searching for a 
		// new fullscreen mode matching the given res so this command can switch fullscreen modes
		display_settings.fullscreen = 0;
		rdrSetPosAndSize(gfx_state.currentDevice->rdr_device, &display_settings);
		gfxSettingsSaveSoon();
	}
#endif
}

// Toggles fullscreen
AUTO_COMMAND ACMD_NAME(togglefullscreen) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Useful) ACMD_CATEGORY(Performance);
void gfxToggleFullscreen(void)
{
#if !PLATFORM_CONSOLE
	if (!gfx_state.currentDevice) {
		return;
	} else {
		DisplayParams display_settings = gfx_state.currentDevice->rdr_device->display_nonthread;

		if(display_settings.fullscreen) {

			display_settings.fullscreen = false;

			// Going from fullscreen to windowed maximized mode is kind of confusing, so
			// go straight to normal windowed mode.
			display_settings.windowed_fullscreen = false;

		} else {

			display_settings.fullscreen = true;
		}

		rdrSetSize(gfx_state.currentDevice->rdr_device, &display_settings);
		gfxSettingsSaveSoon();
	}
#endif
}


// Sets the frame rate percentage of a specific device (0 to disable).
// This is useful when an application is running with multiple devices and you want one to
//  only use a fraction of the CPU.
AUTO_COMMAND ACMD_CATEGORY(Performance) ACMD_NAME(frameRatePercentSpecific);
void gfxSetFrameRatePercentSpecific(int index, F32 percent)
{
	if (index < 0 || index >= eaSize(&gfx_state.devices) || !gfx_state.devices[index])
		return;
	percent = CLAMP(percent, 0, 1.0);
	gfx_state.devices[index]->frameRatePercent = percent;
	gfx_state.devices[index]->frames_counted = 0;
	gfx_state.devices[index]->frames_rendered = 0;
}

// Sets the frame rate percentage of the current device (0 to disable).
// This is useful when an application is running with multiple devices and you want one to
//  only use a fraction of the CPU.
AUTO_COMMAND ACMD_CATEGORY(Performance) ACMD_NAME(frameRatePercent);
void gfxSetFrameRatePercent(F32 percent)
{
	if (gfx_state.currentDevice)
		gfxSetFrameRatePercentSpecific(gfx_state.currentRendererIndex, percent);
}


// Sets the frame rate percentage of a specific device when not in the foreground (0 to just use value from frameRatePercentage).
// This is useful when an application is running with multiple devices and you want one to
//  only use a fraction of the CPU based on what the user is doing.
AUTO_COMMAND ACMD_CATEGORY(Performance) ACMD_NAME(frameRatePercentSpecificBG);
void gfxSetFrameRatePercentSpecificBG(int index, F32 percent)
{
	if (index < 0 || index >= eaSize(&gfx_state.devices) || !gfx_state.devices[index])
		return;
	percent = CLAMP(percent, 0, 1.0);
	gfx_state.devices[index]->frameRatePercentBG = percent;
	gfx_state.devices[index]->frames_counted = 0;
	gfx_state.devices[index]->frames_rendered = 0;
}

// Sets the frame rate percentage of the current device when not in the foreground (0 to just use value from frameRatePercentage).
// This is useful when an application is running with multiple devices and you want one to
//  only use a fraction of the CPU based on what the user is doing.
AUTO_COMMAND ACMD_CATEGORY(Performance) ACMD_NAME(frameRatePercentBG);
void gfxSetFrameRatePercentBG(F32 percent)
{
	if (gfx_state.currentDevice)
		gfxSetFrameRatePercentSpecificBG(gfx_state.currentRendererIndex, percent);
}

// Don't draw volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_all_volumes, hide_all_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw occlusion volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_occlusion_volumes, hide_occlusion_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw audio volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_audio_volumes, hide_audio_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw skyfade volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_skyfade_volumes, hide_skyfade_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw neighborhood volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_neighborhood_volumes, hide_neighborhood_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw optionalaction volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_interaction_volumes, hide_optionalaction_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw landmark volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_landmark_volumes, hide_landmark_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw power volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_power_volumes, hide_power_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw warp volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_warp_volumes, hide_warp_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw occlusion volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_genesis_volumes, hide_genesis_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw exclusion volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_exclusion_volumes, hide_exclusion_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw aggro volumes in the editor
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_aggro_volumes, hide_aggro_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw volumes that have no type
AUTO_CMD_INT(gfx_state.debug.vis_settings.hide_untyped_volumes, hide_untyped_volumes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't do postprocess color correction
AUTO_CMD_INT(gfx_state.debug.disableColorCorrection, disableColorCorrection) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw the second pass of sky objects for bloom quality 2.
AUTO_CMD_INT(gfx_state.debug.disableSkyBloomPass, disableSkyBloomPass) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw the second pass of opaque objects for bloom quality 2.
AUTO_CMD_INT(gfx_state.debug.disableOpaqueBloomPass, disableOpaqueBloomPass) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't draw the second pass of non-deferred and alpha objects for bloom quality 2.
AUTO_CMD_INT(gfx_state.debug.disableNonDeferredBloomPass, disableNonDeferredBloomPass) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't copy depth from the LDR buffer to the HDR buffer for bloom quality 2.
AUTO_CMD_INT(gfx_state.debug.disableLDRToHDRDepthCopy, disableLDRToHDRDepthCopy) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Don't copy color from the LDR buffer to the HDR buffer for bloom quality 2.
AUTO_CMD_INT(gfx_state.debug.disableLDRToHDRColorCopy, disableLDRToHDRColorCopy) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Force the antialiasing level of the HDR buffer to the specified amount for bloom quality 2.
AUTO_CMD_INT(gfx_state.debug.overrideHDRAntialiasing, overrideHDRAntialiasing) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Turns off region culling.
AUTO_CMD_INT(gfx_state.debug.draw_all_regions, drawAllRegions) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Turns off using pre-swapped materials for characters. Supports performance testing.
AUTO_CMD_INT(gfx_state.debug.dynDrawNoPreSwappedMaterials, dynDrawNoPreSwappedMaterials) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Delays sending light data to renderer until drawing actions (this is the old functionality).
AUTO_CMD_INT(gfx_state.debug.delay_sending_lights, delay_sending_lights) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Display debugging info for low res alpha rendering pass.
AUTO_CMD_INT(gfx_state.debug.debug_low_res_alpha, debug_low_res_alpha) ACMD_CATEGORY(Debug);

// Turns off sky fade volumes.
AUTO_CMD_INT(gfx_state.debug.disable_sky_volumes, disable_sky_volumes) ACMD_CATEGORY(Debug);

// Forces the sprite system to use a 32-bit index buffer to draw the sorted sprites even if they would fit in 32-bit one.
AUTO_CMD_INT(gfx_state.debug.force_32bit_sprite_idx_buffer, force_32bit_sprite_idx_buffer) ACMD_CATEGORY(Debug) ACMD_CMDLINE;


// Adds a sprite to be displayed for debugging
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void gfxDebugSpriteAdd(const char *sprite)
{
	eaPush(&gfx_state.debug.debug_sprites, strdup(sprite));
}

// Removes debug sprites
AUTO_COMMAND ACMD_CATEGORY(Debug);
void gfxDebugSpriteClear(void)
{
	eaClearEx(&gfx_state.debug.debug_sprites, freeWrapper);
	gfx_state.debug.debug_sprite_3d.name = NULL;
}

// Sets a single debug 3D sprite
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void gfxDebugSprite3D(const char *name, F32 x, F32 y, F32 z, F32 w, F32 h)
{
	gfx_state.debug.debug_sprite_3d.name = name?allocAddString(name):NULL;
	gfx_state.debug.debug_sprite_3d.x = x;
	gfx_state.debug.debug_sprite_3d.y = y;
	gfx_state.debug.debug_sprite_3d.z = z;
	gfx_state.debug.debug_sprite_3d.w = w;
	gfx_state.debug.debug_sprite_3d.h = h;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("gfxDoingPostProcessing");
bool gfxDoingPostprocessing(void)
{
	return gfxFeatureEnabled(GFEATURE_POSTPROCESSING) && !gfx_state.debug.show_material_cost;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("gfxDoingOutlining");
bool gfxDoingOutlining(void)
{
	return gfxFeatureEnabled(GFEATURE_OUTLINING);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("gfxDoingTargetHighlight");
bool gfxDoingTargetHighlight(void)
{
	return gfx_state.target_highlight && gfxDoingPostprocessing();
}

AUTO_CMD_INT(gfx_state.visualize_screenshot_depth, visualize_screenshot_depth);

AUTO_COMMAND ACMD_NAME(visualize_depth_range);
void gfxVisualizeDepthRange(float depth_min, float depth_max)
{
	gfx_state.screenshot_depth_min = depth_min;
	gfx_state.screenshot_depth_max = depth_max;
}

// Use a software cursor instead of hardware cursors (fixes issues on some video card configurations, but is less responsive)
AUTO_CMD_INT(gfx_state.settings.softwareCursor, SoftwareCursor) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Settings) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(genericSettingChangeCallback);

// Set the maximum cubemap resolutionf or ambient cubemap lighting
AUTO_CMD_INT(gfx_state.ambient_cube_res, ambient_cube_res) ACMD_CATEGORY(Debug);

// Modifies the world detail scale factor automatically applied inside the editor.
AUTO_CMD_FLOAT(gfx_state.editorVisScale,editorVisScale) ACMD_CMDLINEORPUBLIC;

AUTO_CMD_INT(gfx_state.debug.model_lod_offset, debug_model_lod_offset) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(DEBUG);
AUTO_CMD_INT(gfx_state.debug.model_lod_force, debugModelForceLOD) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);

// Displays active rendering and input device settings. Shows the desktop layout including multi-monitor layout, location
// of the device windows, and size of the rendering window client area and input system client area. Enable to validate
// consistency of Cryptic engine state and Windows system state of game client window.
AUTO_CMD_INT(gfx_state.debug.show_settings, show_display_settings) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug) ACMD_HIDE;


// This command allows programmatic transferring of focus away from the client, to automate
// Alt-Tab or other application focus testing. See rwinSetFocusAway for requirements.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(DEBUG) ACMD_HIDE;
void gfxSetFocusAway()
{
	if (!rwinSetFocusAwayFromClient())
		conPrintf("Couldn't find task bar\n");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(DEBUG) ACMD_HIDE;
void gfxSetDebugBuffer(Vec4 parms) {
	rdrSetDebugBuffer(gfx_state.currentDevice->rdr_device,parms);
}
