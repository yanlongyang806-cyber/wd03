#include "RdrState.h"
#include "RdrDevice.h"
#include "cmdparse.h"
#include "wininclude.h"
#include "file.h"
#include "ContinuousBuilderSupport.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

RdrLibState rdr_state;

// Force client to log (to the console) all display modes on Direct3D9 and Direct3D11. Useful for diagnosing fullscreen support bugs.
AUTO_CMD_INT(rdr_state.bLogAllAdapterModes, logAllAdapterModes) ACMD_ACCESSLEVEL(0) ACMD_COMMANDLINE ACMD_HIDE ACMD_CATEGORY(DEBUG);

// Support Direct3D 11/DXGI high-precision fullscreen display formats, such as 10-10-10-2 RGBA modes.
// 0 = Disable high-precision display (default), 1 = Enumerate high-precision formats, 2 8-bit BGRA, 
// 3 = 8-bit RGBA, 4 = SRGB 8-bit BGRA, 5 = SRGB 8-bit RGBA,
// 6 = 10-bit RGBA, 7 = 10-bit Extended range RGBA, 8 = 16-bit float RGBA.

AUTO_CMD_INT(rdr_state.supportHighPrecisionDisplays, supportHighPrecisionDisplays) ACMD_ACCESSLEVEL(0) ACMD_COMMANDLINE ACMD_HIDE ACMD_CATEGORY(DEBUG);

// Disables memory logging of surface create/destroy/modify or other lifetime events. Useful when
// debugging window state, to have a simpler log of window operations.
AUTO_CMD_INT(rdr_state.disableSurfaceLifetimeLog, disableSurfaceLifetimeLog) ACMD_CATEGORY(Debug) ACMD_CMDLINEORPUBLIC;

// Tints alpha objects
AUTO_CMD_INT(rdr_state.show_alpha, show_alpha) ACMD_CMDLINE ACMD_CATEGORY(Useful);

// Tints alpha objects
AUTO_CMD_INT(rdr_state.show_alpha, show_alpha_cutout) ACMD_CMDLINE ACMD_CATEGORY(Useful);

// Tints double sided objects
AUTO_CMD_INT(rdr_state.show_double_sided, show_double_sided) ACMD_CMDLINE ACMD_CATEGORY(Useful);

// Forces buffer recreation every frame
AUTO_CMD_INT(rdr_state.pbuftest, pbuftest) ACMD_CATEGORY(Debug);

// Test Z-Prepass on xbox
AUTO_CMD_INT(rdr_state.testZPass, zpasstest) ACMD_CATEGORY(Debug);

// Writes resulting shaders after post processing to disk
AUTO_CMD_INT(rdr_state.writeProcessedShaders, writeProcessedShaders) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Writes shaders after compiling to microcode to disk
AUTO_CMD_INT(rdr_state.writeCompiledShaders, writeCompiledShaders) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// On Xbox, writes re-assembled shaders for debugging D3DX crashes
AUTO_CMD_INT(rdr_state.writeReassembledShaders, writeReassembledShaders) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Fills code disabled by preprocessor with whitespace to make writeProcessedShader output slightly more readable
AUTO_CMD_INT(rdr_state.stripDeadShaderCode, stripDeadShaderCode) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables shader profiling (may avoid D3DX9 crash)
AUTO_CMD_INT(rdr_state.disableShaderProfiling, disableShaderProfiling) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Decreases the amount of optimization done by the NVIDIA driver
AUTO_CMD_INT(rdr_state.nvidiaLowerOptimization_commandLineOverride, nvidiaLowerOptimization) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Profiling);

// Sets the D3DCompiler optimization level.  Use with /disableShaderCache and /reloadShaders to make it stick.
AUTO_CMD_INT(rdr_state.d3dcOptimization, d3dcOptimization) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Profiling);

// Sets the D3DCompiler optimization level to debug (1) or default (0). Useful for command line options since -1 gets interpreted as a switch.
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINEORPUBLIC;
void d3dcShaderDebug(int enabled)
{
	rdr_state.d3dcOptimization = enabled ? -1 : 0;
}

// Sets a flag to validate textures after each rdr command.  Only
// valid on the command line.
AUTO_CMD_INT(rdr_state.validateTextures, validateTextures) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables clipping of the cursor to a sigle monitor when running in fullscreen on PC
AUTO_CMD_INT(rdr_state.noClipCursor, noClipCursor) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);

// Disables going into full-screen windowed mode when maximized
AUTO_CMD_INT(rdr_state.disable_windowed_fullscreen, disable_windowed_fullscreen) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);

// Disable custom cursors, will just use the default Win32 cursor on PC
AUTO_CMD_INT(rdr_state.noCustomCursor, noCustomCursor) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);

// Compile shaders in the background thread
AUTO_CMD_INT(rdr_state.backgroundShaderCompile, backgroundShaderCompile) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Compile shaders on the shader server
AUTO_CMD_INT(rdr_state.useShaderServer, useShaderServer) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Adds delay to background shader compiles
AUTO_CMD_INT(rdr_state.delayShaderCompileTime, delayShaderCompileTime) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables caching of compiled shaders
AUTO_CMD_INT(rdr_state.disableShaderCache, disableShaderCache) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

// Wipes the shader cache at startup
AUTO_CMD_INT(rdr_state.wipeShaderCache, wipeShaderCache) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

// Does only a quick check to see if a UPDB file exists, may not be the correct file
AUTO_CMD_INT(rdr_state.quickUPDB, quickUPDB) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Don't generate UPDBs to get around XDK bug
AUTO_CMD_INT(rdr_state.noGenerateUPDBs, noGenerateUPDBs) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Does not write out UPDB files when loading cached shaders
AUTO_CMD_INT(rdr_state.noWriteCachedUPDBs, noWriteUPDBs) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Number of frames to allow the renderer to get
AUTO_CMD_INT(rdr_state.max_frames_ahead, rdrMaxFramesAhead) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC;

// Number of frames to allow the GPU to get from the renderer, 0 to disable
AUTO_CMD_INT(rdr_state.max_gpu_frames_ahead, rdrMaxGPUFramesAhead) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC;

// Enables echoing of all shader compilation/binding messages
AUTO_CMD_INT(rdr_state.echoShaderPreloadLog, echoShaderPreloadLog) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Applies the sort_opaque command to the visual pass as well as the zprepass
AUTO_CMD_INT(rdr_state.sortOpaqueAppliesToVisual, sortOpaqueAppliesToVisual) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Sorts shadowmap objects for rendering by state instead of distance
AUTO_CMD_INT(rdr_state.sortShadowmapsByState, sortShadowmapsByState) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables rendering a specific sort bucket
AUTO_CMD_INT(rdr_state.skipSortBucket, skipSortBucket) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);

// Disables optimization where we disable depth writes during the visual pass after the z-prepass
AUTO_CMD_INT(rdr_state.dontDisableDepthWritesAfterZPrepass, dontDisableDepthWritesAfterZPrepass) ACMD_CATEGORY(Debug);

// Disables xbox microcode level reassembly of shaders to remove constant jump instructions
AUTO_CMD_INT(rdr_state.disableMicrocodeReassembly, disableMicrocodeReassembly) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Disables depth bias
AUTO_CMD_INT(rdr_state.disableDepthBias, disableDepthBias) ACMD_CATEGORY(Debug);

// Disables our hack which resets the vertex texture state whenever binding the exposure transform texture
AUTO_CMD_INT(rdr_state.disableVertexTextureResetHack, disableVertexTextureResetHack) ACMD_CATEGORY(Debug);

// Changes the rendering to swap buffers immediately at the end of the frame instead of waiting until the beginning of the next
AUTO_CMD_INT(rdr_state.swapBuffersAtEndOfFrame, swapBuffersAtEndOfFrame) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Clears all buffers at the beginning of each frame
AUTO_CMD_INT(rdr_state.clearAllBuffersEachFrame, clearAllBuffersEachFrame) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Uses a noticeable debug loading pixel shader while shaders are loading
AUTO_CMD_INT(rdr_state.showDebugLoadingPixelShader, showDebugLoadingPixelShader) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Disables the two-pass algorithm for rendering depth-dependent (water/refractive/depth fade) alpha objects.
AUTO_CMD_INT(rdr_state.disable_two_pass_depth_alpha,disable_two_pass_depth_alpha)  ACMD_CATEGORY(Debug);

// Disables the conglomeration of sort nodes in depth-only passes for subobjects without alpha cutout.
AUTO_CMD_INT(rdr_state.disable_depth_sort_nodes,disable_depth_sort_nodes) ACMD_CATEGORY(Debug);

// Sets the maximum draw calls for the opaque hdr pass when bloom quality is set to medium
AUTO_CMD_INT(rdr_state.maxOpaqueHDRDrawCalls,maxOpaqueHDRDrawCalls) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Sets the maximum draw calls for the decal hdr pass when bloom quality is set to medium
AUTO_CMD_INT(rdr_state.maxDecalHDRDrawCalls,maxDecalHDRDrawCalls) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Sets the maximum draw calls for the alpha hdr pass when bloom quality is set to medium
AUTO_CMD_INT(rdr_state.maxAlphaHDRDrawCalls,maxAlphaHDRDrawCalls) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Sets the maximum triangle count for the opaque hdr pass when bloom quality is set to medium
AUTO_CMD_INT(rdr_state.maxOpaqueHDRTriangles,maxOpaqueHDRTriangles) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Sets the maximum triangle count for the decal hdr pass when bloom quality is set to medium
AUTO_CMD_INT(rdr_state.maxDecalHDRTriangles,maxDecalHDRTriangles) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Sets the maximum triangle count for the alpha hdr pass when bloom quality is set to medium
AUTO_CMD_INT(rdr_state.maxAlphaHDRTriangles,maxAlphaHDRTriangles) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Disables using a simpler shader when objects are being lit by a single directional light
AUTO_CMD_INT(rdr_state.noSimpleDirLightShader, noSimpleDirLightShader) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// 0=off, 1=on, 2=default (hopefully smart)
AUTO_CMD_INT(rdr_state.hwInstancingMode, hwInstancingMode) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Disables instancing at the draw list level
AUTO_CMD_INT(rdr_state.disableSWInstancing, disableSWInstancing) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Disables sorting instances by distance
AUTO_CMD_INT(rdr_state.disableInstanceSorting, disableInstanceSorting) ACMD_CATEGORY(Debug);

// Disables shader timeout
AUTO_CMD_INT(rdr_state.disableShaderTimeout, disableShaderTimeout) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Disables the separate decal rendering buckets and forces decals to render with the alpha objects
AUTO_CMD_INT(rdr_state.disableSeparateDecalRenderBucket, disableSeparateDecalRenderBucket) ACMD_CATEGORY(Debug);

// Disables F16 texture coordinates for testing low-end cards and for debugging possible texture coodinate precision problems.  COMMAND-LINE ONLY.
AUTO_CMD_INT(rdr_state.disableF16s, disableF16s) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables 24-bit depth textures to simulate low-end cards
AUTO_CMD_INT(rdr_state.disableF24DepthTexture, disableF24DepthTexture) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables MSAA surfaces to simulate low-end cards
AUTO_CMD_INT(rdr_state.disableMSAASurfaceTypes, disableMSAASurfaceTypes) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables the stencil buffer (stencil value is always 0) to simulate using DF16 and DF24 depth textures.
AUTO_CMD_INT(rdr_state.disableStencilDepthTexture, disableStencilDepthTexture) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables occlusion queries to simulate low-end cards
AUTO_CMD_INT(rdr_state.disableOcclusionQueries, disableOcclusionQueries) ACMD_CMDLINE ACMD_CATEGORY(Debug);


// Disables MSAA depth resolve to simulate old cards, but ones that are still SM3.0
AUTO_CMD_INT(rdr_state.disableMSAADepthResolve, disableMSAADepthResolve) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Draws all objects as single triangles
AUTO_CMD_INT(rdr_state.drawSingleTriangles, drawSingleTriangles) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Enables defining of MRT2 and MRT4 in shaders
AUTO_CMD_INT(rdr_state.allowMRTshaders, allowMRTshaders) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// Disables asynchronous loading of vertex shaders
AUTO_CMD_INT(rdr_state.disableAsyncVshaderLoad, disableAsyncVshaderLoad) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Preloads all possible vertex shaders (slow), instead of just the small set
AUTO_CMD_INT(rdr_state.preloadAllVertexShaders, preloadAllVertexShaders) ACMD_CATEGORY(Debug, Performance) ACMD_CMDLINE;

// Turns off two pass refraction
AUTO_CMD_INT(rdr_state.disableTwoPassRefraction, disableTwoPassRefraction) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Turns off low res alpha rendering
AUTO_CMD_INT(rdr_state.disableLowResAlpha, disableLowResAlpha) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Controls the max distance for low res alpha rendering
AUTO_CMD_FLOAT(rdr_state.lowResAlphaMaxDist, lowResAlphaMaxDist) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Controls the min distance for low res alpha rendering.  zdist can be negative!
AUTO_CMD_FLOAT(rdr_state.lowResAlphaMinDist, lowResAlphaMinDist) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Turns off depth testing on wireframe drawing
AUTO_CMD_INT(rdr_state.disableWireframeDepthTest, disableWireframeDepthTest) ACMD_CATEGORY(Debug);

// Disables drawing of any geo not selected in the budgets window.
AUTO_CMD_INT(rdr_state.disableUnselectedGeo, disableUnselectedGeo) ACMD_CATEGORY(Debug);

// Sets an new value for forcing far depth, default of 0.9999960f, 0.9999999f originally
AUTO_CMD_FLOAT(rdr_state.overrideFarDepthScale, overrideFarDepthScale) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Turns on wireframe drawing for sprites, 1 = half wireframe, 2 = full wireframe
AUTO_CMD_INT(rdr_state.spriteWireframe, spriteWireframe) ACMD_CATEGORY(Debug);

AUTO_CMD_INT(rdr_state.noErrorfOnShaderErrors, noErrorfOnShaderErrors) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Size of the texture load ring buffer
AUTO_CMD_INT(rdr_state.texLoadPoolSize, texLoadPoolSize) ACMD_CMDLINE ACMD_CATEGORY(Debug);

void rdrDisableFeature(RdrFeature feature, bool disable)
{
	if (disable)
		rdr_state.features_disabled |= feature;
	else
		rdr_state.features_disabled &= ~feature;
}

// The mirrored ring buffer is currently disabled.
#define DEFAULT_TEXLOAD_RINGBUFFER_SIZE	(0 * 1024 * 1024)

#define ATI_TESSELLATION_ACCEPTABLE_DRIVER 100984

AUTO_RUN_EARLY;
void initRdrStateSettings(void)
{
	rdr_state.backgroundShaderCompile = 1;
	rdr_state.nvidiaLowerOptimization_commandLineOverride = -1;
	rdr_state.nvidiaLowerOptimization = rdr_state.nvidiaLowerOptimization_default = 2; // LOW (not MIN)
#if !PLATFORM_CONSOLE
	rdr_state.max_gpu_frames_ahead = -1; // Set to actual initial value in rdrStartup()
#endif
	rdr_state.maxOpaqueHDRDrawCalls = 75;
	rdr_state.maxDecalHDRDrawCalls = 20;
	rdr_state.maxAlphaHDRDrawCalls = 75;
	rdr_state.maxOpaqueHDRTriangles = 32000;
	rdr_state.maxDecalHDRTriangles = 3000;
	rdr_state.maxAlphaHDRTriangles = 8000;
	rdr_state.d3dcOptimization = 1;
	rdr_state.texLoadPoolSize = DEFAULT_TEXLOAD_RINGBUFFER_SIZE;
#if _PS3
	rdr_state.backgroundShaderCompile = 0;
    rdr_state.disableAsyncVshaderLoad = 1;
    rdr_state.preloadAllVertexShaders = 1;
    rdr_state.disableShaderProfiling = 1;
    rdr_state.disableHWInstancing = 1;

#elif _XBOX
#if !ENABLE_XBOX_HW_INSTANCING
	rdr_state.disableHWInstancing = 1;
#endif
	rdr_state.disable_two_pass_depth_alpha = 1; // No need for two pass, we are reading from a resolved surface
#	ifdef PROFILE
		rdr_state.noWriteCachedUPDBs = 0;
#	else
		rdr_state.noWriteCachedUPDBs = 1; // Not useful unless running profile anyway!
#	endif
#endif

#if !PLATFORM_CONSOLE
	if (g_isContinuousBuilder || isDevelopmentMode())
		rdr_state.useShaderServer = 1;
#endif

	rdr_state.overrideFarDepthScale = 0.9999991f; // JE: Decreased this number from 0.9999999f because some sky domes (MIL vista plane) were getting clipped if > ...960, Increased for STO shoeboxes (to ...991), still some clipping in MIL
	rdr_state.nvidiaCSAAMode = 1; //normal

	rdr_state.swapBuffersAtEndOfFrame = 1; // Greatly increases framerate and reduces latency when CPU bound.  This *might* decrease performance on GPU-bound single-core systems, but I was unable to reproduce any case in which it did.
	if (system_specs.videoCardVendorID == VENDOR_ATI && system_specs.videoDriverVersionNumber < ATI_TESSELLATION_ACCEPTABLE_DRIVER)
		rdr_state.disableTessellation = 1;

	rdr_state.unicodeRendererWindow = 1;

	rdr_state.disableSurfaceLifetimeLog = !isDevelopmentMode();

	rdr_state.bProcessMessagesOnlyBetweenFrames = true;

	rdr_state.spriteCurrentEffect = RdrSpriteEffect_Undefined;

#if RDR_ENABLE_DRAWLIST_HISTOGRAMS
	rdr_state.depthHistConfig.scale = 100.0f;
	rdr_state.depthHistConfig.initial = 100.0f;
	rdr_state.depthHistConfig.getBucketFn = histoGetLinearBucket;
	rdr_state.depthHistConfig.getBucketsSplitFn = histoGetLinearBucketSplit;
	rdr_state.sizeHistConfig.scale = 10.0f;
	rdr_state.sizeHistConfig.initial = 5.0f;
	rdr_state.sizeHistConfig.getBucketFn = histoGetLinearBucket;
	rdr_state.sizeHistConfig.getBucketsSplitFn = histoGetLinearBucketSplit;
#endif
}

// Enable or disable drawing of normal objects.  Pass in -1 to toggle.
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;
void dbgShowNormal(int enabled)
{
	if (enabled == -1)
		rdr_state.dbg_type_flags ^= RDRTYPE_NONSKINNED;
	else if (enabled)
		rdr_state.dbg_type_flags &= ~RDRTYPE_NONSKINNED;
	else
		rdr_state.dbg_type_flags |= RDRTYPE_NONSKINNED;
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dbgShowNormal);
void dbgShowNormalQuery(CmdContext *cmd)
{
	// Make it look just like an AUTO_CMD_INT
	estrPrintf(cmd->output_msg, "dbgShowNormal %d", (int)(!(rdr_state.dbg_type_flags & RDRTYPE_NONSKINNED)));
}

// Enable or disable drawing of skinned objects.  Pass in -1 to toggle.
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;
void dbgShowSkinned(int enabled)
{
	if (enabled == -1)
		rdr_state.dbg_type_flags ^= RDRTYPE_SKINNED;
	else if (enabled)
		rdr_state.dbg_type_flags &= ~RDRTYPE_SKINNED;
	else
		rdr_state.dbg_type_flags |= RDRTYPE_SKINNED;
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dbgShowSkinned);
void dbgShowSkinnedQuery(CmdContext *cmd)
{
	// Make it look just like an AUTO_CMD_INT
	estrPrintf(cmd->output_msg, "dbgShowSkinned %d", (int)(!(rdr_state.dbg_type_flags & RDRTYPE_SKINNED)));
}


// Enable or disable drawing of terrain objects.  Pass in -1 to toggle.
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;
void dbgShowTerrain(int enabled)
{
	if (enabled == -1)
		rdr_state.dbg_type_flags ^= RDRTYPE_TERRAIN;
	else if (enabled)
		rdr_state.dbg_type_flags &= ~RDRTYPE_TERRAIN;
	else
		rdr_state.dbg_type_flags |= RDRTYPE_TERRAIN;
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dbgShowTerrain);
void dbgShowTerrainQuery(CmdContext *cmd)
{
	// Make it look just like an AUTO_CMD_INT
	estrPrintf(cmd->output_msg, "dbgShowTerrain %d", (int)(!(rdr_state.dbg_type_flags & RDRTYPE_TERRAIN)));
}


// Enable or disable drawing of sprites.  Pass in -1 to toggle.
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;
void dbgShowSprite(int enabled)
{
	if (enabled == -1)
		rdr_state.dbg_type_flags ^= RDRTYPE_SPRITE;
	else if (enabled)
		rdr_state.dbg_type_flags &= ~RDRTYPE_SPRITE;
	else
		rdr_state.dbg_type_flags |= RDRTYPE_SPRITE;
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dbgShowSprite);
void dbgShowSpriteQuery(CmdContext *cmd)
{
	// Make it look just like an AUTO_CMD_INT
	estrPrintf(cmd->output_msg, "dbgShowSprite %d", (int)(!(rdr_state.dbg_type_flags & RDRTYPE_SPRITE)));
}


// Enable or disable drawing of particles.  Pass in -1 to toggle.
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;
void dbgShowParticle(int enabled)
{
	if (enabled == -1)
		rdr_state.dbg_type_flags ^= RDRTYPE_PARTICLE;
	else if (enabled)
		rdr_state.dbg_type_flags &= ~RDRTYPE_PARTICLE;
	else
		rdr_state.dbg_type_flags |= RDRTYPE_PARTICLE;
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dbgShowParticle);
void dbgShowParticleQuery(CmdContext *cmd)
{
	// Make it look just like an AUTO_CMD_INT
	estrPrintf(cmd->output_msg, "dbgShowParticle %d", (int)(!(rdr_state.dbg_type_flags & RDRTYPE_PARTICLE)));
}


// Enable or disable drawing of primitives.  Pass in -1 to toggle.
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;
void dbgShowPrimitive(int enabled)
{
	if (enabled == -1)
		rdr_state.dbg_type_flags ^= RDRTYPE_PRIMITIVE;
	else if (enabled)
		rdr_state.dbg_type_flags &= ~RDRTYPE_PRIMITIVE;
	else
		rdr_state.dbg_type_flags |= RDRTYPE_PRIMITIVE;
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dbgShowPrimitive);
void dbgShowPrimitiveQuery(CmdContext *cmd)
{
	// Make it look just like an AUTO_CMD_INT
	estrPrintf(cmd->output_msg, "dbgShowPrimitive %d", (int)(!(rdr_state.dbg_type_flags & RDRTYPE_PRIMITIVE)));
}


AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_COMMANDLINE;
void dbgDisableRendering(int enabled)
{
	// leave sprites on so we get the pop-up telling us performance numbers
	if (enabled == -1)
		rdr_state.dbg_type_flags ^= ~RDRTYPE_SPRITE;
	else if (enabled)
		rdr_state.dbg_type_flags |= ~RDRTYPE_SPRITE;
	else
		rdr_state.dbg_type_flags &= RDRTYPE_SPRITE;
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dbgDisableRendering);
void dbgDisableRenderingQuery(CmdContext *cmd)
{
	// Make it look just like an AUTO_CMD_INT
	estrPrintf(cmd->output_msg, "dbgDisableRendering %d", (int)((rdr_state.dbg_type_flags & ~RDRTYPE_SPRITE) == ~RDRTYPE_SPRITE));
}

// Shows per frame sprite stat counters
AUTO_CMD_INT(rdr_state.showSpriteCounters, show_sprite_counters) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE ACMD_CATEGORY(Performance, Debug);

// Disables yielding the CPU while waiting for the GPU
AUTO_CMD_INT(rdr_state.noSleepWhileWaitingForGPU, noSleepWhileWaitingForGPU) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Performance, Debug);

// Does world+character alpha objects before DoF pas
AUTO_CMD_INT(rdr_state.alphaInDOF, alphaInDOF) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

// Traces query use to the debugger.
AUTO_CMD_INT(rdr_state.traceQueries, traceQueries) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

// Disables a 10-percent boost in the HDR tone curve for intensities above 80% bright.
AUTO_CMD_INT(rdr_state.disableToneCurve10PercentBoost, disableToneCurve10PercentBoost) ACMD_ACCESSLEVEL(9) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

// Collects lighting on CCLighting usage
AUTO_CMD_INT(rdr_state.cclightingStats, cclightingStats) ACMD_ACCESSLEVEL(9) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

// Put fast particles in a different bucket to work around DOF issues
AUTO_CMD_INT(rdr_state.fastParticlesInPreDOF, fastParticlesInPreDOF) ACMD_ACCESSLEVEL(9) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

// Manually change the display mode for fullscreen settings, rather than allowing Direct3D to make the mode switch.
AUTO_CMD_INT(rdr_state.useManualDisplayModeChange, useManualDisplayModeChange) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

// Turns tessellation off in the render thread.
AUTO_CMD_INT(rdr_state.disableTessellation, disableTessellation) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_CATEGORY(Debug);
// Allows the user to see what it would look like if everything was using tessellation.
AUTO_CMD_INT(rdr_state.tessellateEverything, tessellateEverything) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Uses a Unicode-compatible window for rendering, to support Windows Unicode features.
AUTO_CMD_INT(rdr_state.unicodeRendererWindow, unicodeRendererWindow) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);

// Prevents processing the device thread message queue except between frames.
AUTO_CMD_INT(rdr_state.bProcessMessagesOnlyBetweenFrames, processMessagesOnlyBetweenFrames) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);

// Allows GameClient code to manage the game rendering window under Direct3D 9, instead of Direct3D.
// This is the prior default application behavior, but is now off by default.
AUTO_CMD_INT(rdr_state.bD3D9ClientManagesWindow, rdrD3D9ClientManagesWindow) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);

