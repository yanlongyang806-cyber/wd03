#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "RdrTextureEnums.h"
#include "RdrEnums.h"

#if _XBOX
    // slower on the xbox than not doing it
    #define ENABLE_XBOX_HW_INSTANCING 0
#endif

typedef U64 TexHandle;

// Global/debug variables that get set by rdrCommandParse

typedef enum
{
	RDRTYPE_NONSKINNED	= 1<<0,
	RDRTYPE_SKINNED		= 1<<1,
	RDRTYPE_TERRAIN		= 1<<2,
	RDRTYPE_SPRITE		= 1<<3,
	RDRTYPE_PARTICLE	= 1<<4,
	RDRTYPE_PRIMITIVE	= 1<<5,
} RdrDebugTypeFlags;

typedef enum RdrCompileShaderTypes
{
	CompileShaderType_PC=(1<<0),
	CompileShaderType_Xbox=(1<<1),
} RdrCompileShaderTypes;

typedef struct RdrLibState {
	bool supportHighPrecisionDisplays;
	bool disableSurfaceLifetimeLog;
	bool writeProcessedShaders; // Write shaders after preprocessing to disk	
	bool writeCompiledShaders; // Write shaders after compiling to microcode to disk
	bool writeReassembledShaders; // On Xbox, write reassembled shaders (only useful if D3DX is crashing)
	bool stripDeadShaderCode;
	bool runNvPerfShader; // Run NVPerfShader on resulting compiled shader
	bool show_alpha;
	bool show_double_sided;
	bool pbuftest;
	bool testZPass;
	bool noClipCursor;
	bool disable_windowed_fullscreen;
	bool backgroundShaderCompile;
	bool useShaderServer;
	U8 compile_all_shader_types;
	int delayShaderCompileTime;
	U32 features_disabled;
	RdrDebugTypeFlags dbg_type_flags; // Turns off drawing of a type
	U32 texLoadPoolSize;
	bool do_occlusion_queries;
	bool disableShaderCache;
	bool wipeShaderCache;
	bool quickUPDB;
	bool noWriteCachedUPDBs;
	bool noGenerateUPDBs; // Don't generate UPDBs to get around XDK bug
	int max_frames_ahead; // Number of frames to allow the render threads to get
	int max_gpu_frames_ahead;
	U32 gpu_count;
	bool disableShaderProfiling;
	bool noErrorfOnShaderErrors;
	int nvidiaLowerOptimization_commandLineOverride;
	int nvidiaLowerOptimization;
	int nvidiaLowerOptimization_last; // Currently set value
	int nvidiaLowerOptimization_default;
	int d3dcOptimization;
	bool echoShaderPreloadLog;
	bool echoShaderPreloadLogIgnoreVirgins;
	bool sortOpaqueAppliesToVisual;
	bool sortShadowmapsByState;
	int skipSortBucket;
	bool dontDisableDepthWritesAfterZPrepass;
	bool disableMicrocodeReassembly;
	bool disableDepthBias;
	int frameRateStabilizer;
	int perFrameSleep;
	bool usingNVPerfHUD;
	bool disableVertexTextureResetHack;
	bool swapBuffersAtEndOfFrame;
	bool clearAllBuffersEachFrame;
	bool showDebugLoadingPixelShader;
	bool disable_two_pass_depth_alpha;
	bool disable_depth_sort_nodes;
	int maxOpaqueHDRDrawCalls;
	int maxDecalHDRDrawCalls;
	int maxAlphaHDRDrawCalls;
	int maxOpaqueHDRTriangles;
	int maxDecalHDRTriangles;
	int maxAlphaHDRTriangles;
	bool noSimpleDirLightShader;
	int hwInstancingMode; // 0 = off, 1 = on, 2 = default (hopefully smart);
	bool disableSWInstancing;
	bool disableInstanceSorting;
    bool disableShaderTimeout;
	bool disableSeparateDecalRenderBucket;
	bool forceCPUFastParticles;
	bool forceTwoBonedSkinning;
	bool disableF16s; // To simulate low-end cards.  Also to test higher precision texture coords for debugging?
	bool disableF24DepthTexture; // To simulate low-end cards.
	bool disableStencilDepthTexture; // To simulate ATI cards using DF16/DF24
	bool disableDepthTextures;
	bool disableNonTrivialSurfaceTypes;
	bool disableMSAASurfaceTypes;
	bool disableOcclusionQueries;
	bool disableMSAADepthResolve; // To simulate old cards but ones that are still SM3.0
	bool validateTextures;
	bool drawSingleTriangles;
	bool allowMRTshaders;
	bool disableAsyncVshaderLoad;
	bool preloadAllVertexShaders;
	bool disableTwoPassRefraction;
	bool disableLowResAlpha;
	F32 lowResAlphaMaxDist;
	F32 lowResAlphaMinDist;
	bool lowResAlphaUnsupported;
	bool lowResAlphaHighResNeedsManualDepthTest;
	bool disableWireframeDepthTest;
	bool disableUnselectedGeo;
	bool noCustomCursor;
	F32	overrideFarDepthScale;
	int spriteWireframe;
	int nvidiaCSAAMode; //0 = off, 1 = normal, 2 = quality
	int nvidiaCSAASupported;
	int msaaQuality; //the same as gfx_state.msaa
	bool dx11Renderer; // at least one device is a DX11 renderer
	bool unicodeRendererWindow; // all render device windows use Unicode Win32 APIs
	bool showSpriteCounters;
	bool noSleepWhileWaitingForGPU;
	bool alphaInDOF;
	bool traceQueries;
	bool disableToneCurve10PercentBoost;
	bool cclightingStats;
	bool fastParticlesInPreDOF;
	bool useManualDisplayModeChange;
	bool disableTessellation;
	bool tessellateEverything;
	bool bLogAllAdapterModes;
	bool bProcessMessagesOnlyBetweenFrames;
	bool bD3D9ClientManagesWindow;
	RdrSpriteEffect spriteCurrentEffect;

	RdrTexFlags disabled_tex_flags;
	TexHandle white_tex_handle;
#if RDR_ENABLE_DRAWLIST_HISTOGRAMS
	HistogramConfig depthHistConfig;
	HistogramConfig sizeHistConfig;
#endif
} RdrLibState;

extern RdrLibState rdr_state;

// Note this enumeration must match GfxGPUTimes, and must match the time_gpu member of WorldPerfFrameCounts.
typedef enum EGfxPerfCounter
{
	EGfxPerfCounter_IDLE=0,
	EGfxPerfCounter_MISC,
	EGfxPerfCounter_SHADOWS,
	EGfxPerfCounter_ZPREPASS,
	EGfxPerfCounter_OPAQUE_ONEPASS,
	EGfxPerfCounter_SHADOW_BUFFER,
	EGfxPerfCounter_OPAQUE,
	EGfxPerfCounter_ALPHA,
	EGfxPerfCounter_POSTPROCESS,
	EGfxPerfCounter_2D,
	EGfxPerfCounter_COUNT
} EGfxPerfCounter;

// This struct must match the EGfxPerfCounter enumeration.
typedef struct GfxGPUTimes
{
	F32 fIdle;
	F32 fMisc;
	F32 fShadows;
	F32 fZPrePass;
	F32 fOpaqueOnePass;
	F32 fShadowBuffer;
	F32 fOpaque;
	F32 fAlpha;
	F32 fPostProcess;
	F32 f2D;
} GfxGPUTimes;

typedef struct GfxPerfCounts
{
	union
	{
		F32 afTimers[EGfxPerfCounter_COUNT];
		GfxGPUTimes gpu_times;
	};
} GfxPerfCounts;

extern GfxPerfCounts rdrGfxPerfCounts_Last;
extern GfxPerfCounts rdrGfxPerfCounts_Current;

void rdrDisableFeature(RdrFeature feature, bool disable);
__forceinline static bool rdrLowResAlphaEnabled(void) { return !rdr_state.disableLowResAlpha && !rdr_state.lowResAlphaUnsupported; }
__forceinline static bool rdrNvidiaCSAAEnabled(void) { return rdr_state.msaaQuality > 4 && rdr_state.nvidiaCSAAMode > 0 && rdr_state.nvidiaCSAASupported; }
