#pragma once
GCC_SYSTEM

#include "RdrDevice.h"

typedef enum ShaderGraphQuality ShaderGraphQuality;
typedef struct RdrDevice RdrDevice;
typedef struct RdrDeviceInfo RdrDeviceInfo;
typedef struct WindowCreateParams WindowCreateParams;
typedef struct GfxResolution GfxResolution;
typedef struct GfxPerDeviceState GfxPerDeviceState;

AUTO_ENUM;
typedef enum GfxFeature
{
	GFEATURE_SHADOWS			= 1<<0,
	
	GFEATURE_POSTPROCESSING		= 1<<2,
	GFEATURE_OUTLINING			= 1<<3,
	GFEATURE_LINEARLIGHTING		= 1<<4,
	GFEATURE_DOF				= 1<<5,
    GFEATURE_WATER              = 1<<6,
	GFEATURE_SCATTERING			= 1<<7,
} GfxFeature;
extern StaticDefineInt GfxFeatureEnum[];

AUTO_ENUM;
typedef enum GfxLightingQuality
{
	GLIGHTING_QUALITY_VERTEX_ONLY_LIGHTING=0, // "Low"
	// Add other things here, perhaps selecting SM2.0-level shaders?
	GLIGHTING_QUALITY_HIGH=10,
} GfxLightingQuality;
extern StaticDefineInt GfxLightingQualityEnum[];

typedef enum GfxScatterQuality
{
	GSCATTER_OFF,
	GSCATTER_LOW_QUALITY,
	GSCATTER_HIGH_QUALITY,
} GfxScatterQuality;

typedef enum GfxBloomQualityLevels
{
	// 0 = off
	GBLOOM_OFF,
	// 1 = bloom white (above threshold) pixels 
	GBLOOM_LOW_BLOOMWHITE,
	// 2 = small secondary underexposed buffer
	GBLOOM_MED_BLOOM_SMALLHDR,
	// 3 = MRT2 underexposed buffer
	GBLOOM_HIGH_BLOOM_FULLHDR,

	// 4 = MRT4 deferred lighting mode, bloom like GBLOOM_LOW_BLOOMWHITE
	GBLOOM_MAX_BLOOM_DEFERRED,

	GBLOOM_MAX = GBLOOM_MAX_BLOOM_DEFERRED,
} GfxBloomQualityLevels;

typedef enum eCardClass
{
	CC_UNKNOWN,
	CC_UNKNOWN_SM2PLUS,
	CC_INTELEXTREME,
	CC_NV2,
	CC_NV3_OR_ATI8500,
	CC_NV4_OR_ATI9500,
	CC_NV8, // GF8 or higher

	CC_XBOX,
} eCardClass;

AUTO_ENUM;
typedef enum GfxAntialiasingMode {
	GAA_NONE,
	GAA_MSAA,
	GAA_TXAA,

	GAA_MAX_MODES,
} GfxAntialiasingMode;
extern StaticDefineInt GfxAntialiasingModeEnum[];

typedef struct GfxSettings
{
	int refreshRate;
	int screenXPos;
	int screenYPos;
	int maximized;
	int fullscreen;
	GfxResolution * fullscreenRes;
	int monitor_index_plus_one;
	const char *device_type;

	int showAdvanced;
	F32 slowUglyScale;

	F32	renderScale[2];
	int renderSize[2];
	int renderScaleSetBySize[2];
	int renderScaleFilter;

	F32 aspectRatio; // 0 = auto based on pixels

	int settingsDataVersion; // Indicates interpretation of data members, new code can bump version to indicate conversions are required on settings fields.
	int settingsVersion; // When this changes, we get defaults again
	int settingsDefaultVersion; // When this changes, we get defaults again, only if we're set to not showing advanced
	int videoCardVendorID; // Of when these settings were set
	int videoCardDeviceID;

	F32 worldDetailLevel;		//vis_scale.  1.0 = normal
	F32	entityDetailLevel;		//scale the LOD switch distances for entities.  1.0 = normal
	F32 terrainDetailLevel;		//scales the LOD switch distances for terrain.  1.0 = normal, below 1 cuts off far LODs
	F32 worldTexLODLevel;		//scale for TexReduce
	F32 entityTexLODLevel;		//scale for TexReduce
	F32 worldLoadScale;
	int videoMemoryMax;			//max amount of video memory to use
	int reduceFileStreaming;
	int soft_particles;
	int fxQuality;
	int useVSync;
	GfxAntialiasingMode antialiasingMode;
	int antialiasing;
	int softwareCursor;
	int texAniso;				// 1 = default, 16 = max on GF6
	GfxFeature features_desired;
	GfxFeature features_desired_by_command_line;
	GfxFeature features_disabled_by_command_line;
	int maxLightsPerObject;
	int maxShadowedLights;
	int disableSplatShadows;
	int dynamicLights; // Enable dynamically creating of FX lights, etc
	int draw_high_detail;
	int draw_high_fill_detail;
	int preload_materials;
	int frameRateStabilizer;
	int autoEnableFrameRateStabilizer;
	F32 maxInactiveFps;
	int renderThread;
	int bloomQuality;		// See GfxBloomQualityLevels. 
	F32 bloomIntensity;
	int maxBloomQuality;
	int scattering;
	int ssao;
	GfxLightingQuality lighting_quality;
	int maxReflection;
	int highQualityDOF;
	int useFullSkinning;
	int gpuAcceleratedParticles;
	int maxDebris;
	int poissonShadows;
	int minDepthBiasFix;
	int higherSettingsInTailor;
	int perFrameSleep;
	int maxFps;
	int lensflare_quality;
	int hdr_max_luminance_mode; // Project level option, not user-level
	int cluster_load;
	// When adding new options, make sure to update gfxGetSettingsString() and gfx_settings_parseinfo[] and gfxGetRecommended*

	F32 gamma;					//0.0 = normal -0.7 = lowest 3.4 = highest  

	// Saved measurements of actual installed device, only measured when
	// device ID or settings version changes, and on settings reset.
	F32 last_recorded_perf;
	F32 last_recorded_msaa_perf;

	// Measurements to use for automatic settings selection; allows override
	// for emulating cards with varying performance.
	eCardClass logical_card_class;
	RdrDevicePerfTimes logical_perf_values;

	GfxFeature features_supported;

	float defaultFOV;
	int hwInstancing;

	ShaderGraphQuality desired_Mat_quality;
	ShaderGraphQuality recommended_Mat_quality;

	int hasSelectedFullscreenMode;

	U8 bUseSettingsResolution : 1;
	U8 antialiasingEnabled : 1;
	int windowedWidth;
	int windowedHeight;
	int fullscreenWidth;
	int fullscreenHeight;

} GfxSettings;

AUTO_STRUCT AST_ENDTOK(EndResolution);
typedef struct ResolutionRule
{
	U32 w;
	U32 h;
	F32 fill; // fill perf must be > this value
	F32 fillIntel; // optional lower number for Intel cards to use this resolution (they seem to measure lower than they perform
	F32 renderscale;
} ResolutionRule;

AUTO_STRUCT AST_ENDTOK(EndRenderScale);
typedef struct RenderScaleRule
{
	F32 fill; // fill perf must be > this value
	F32 renderscale;
} RenderScaleRule;

AUTO_STRUCT AST_ENDTOK(EndShaderQuality);
typedef struct ShaderQualityRule
{	// Min fill rate for recommended shader quality
	F32 mid;
	F32 high;
	F32 veryHigh;
} ShaderQualityRule;

AUTO_STRUCT AST_STRIP_UNDERSCORES;
typedef struct DefaultSettingsRules
{
	ResolutionRule **resolution;
	RenderScaleRule **renderscale;

	F32 fill_two_bone_skinning;			AST(DEFAULT(2500))
	F32 fill_gpu_particles;				AST(DEFAULT(3500))
	F32 fill_no_shadows;				AST(DEFAULT(500))
	F32 fill_splat_shadows;				AST(DEFAULT(9100))
	F32 fill_no_effects;				AST(DEFAULT(3000))
	F32 fill_outlining_intel;			AST(DEFAULT(500))
	F32 fill_lightingquality_low;		AST(DEFAULT(5400))
	F32 fill_lightingquality_low_intel; AST(DEFAULT(500))
	F32 fill_detail_05;					AST(DEFAULT(1800))
	F32 fill_detail_075;				AST(DEFAULT(2500))
	F32 fill_terrain_low;				AST(DEFAULT(1000))
	F32 fill_high_quality_dof;			AST(DEFAULT(18000))
	F32 fill_ssao;						AST(DEFAULT(9100))
	F32 fill_msaa;						AST(DEFAULT(14000))
	F32 fill_vfx_low;					AST(DEFAULT(1500))
	F32 fill_vfx_medium;				AST(DEFAULT(4000))
	F32 fill_lensflare;					AST(DEFAULT(9100))

	F32 fill_soft_particles;			AST(DEFAULT(10000))
	F32 fill_draw_high_detail;			AST(DEFAULT(10000))
	F32 fill_dynamic_lights;			AST(DEFAULT(10000))
	F32 fill_draw_high_fill_detail;		AST(DEFAULT(20000))
	F32 fill_bloom_quality1;			AST(DEFAULT(10000))
	F32 fill_bloom_quality2;			AST(DEFAULT(20000))
	F32 fill_poisson_shadows;			AST(DEFAULT(15000))
	F32 fill_tex_aniso4;				AST(DEFAULT(10000))
	F32 fill_tex_aniso16;				AST(DEFAULT(20000))

	F32 fill_min_lights_per_object;		AST(DEFAULT(3000))
	F32 fill_max_lights_per_object;		AST(DEFAULT(8000))
	F32 fill_min_shadowed_lights;		AST(DEFAULT(8000))
	F32 fill_max_shadowed_lights;		AST(DEFAULT(16000))

	int defaults_version_number;
	int fill_cluster_low;				AST(DEFAULT(2000))
	int fill_cluster_mid;				AST(DEFAULT(8000))
	int fill_scattering;				AST(DEFAULT(14000))
	S32 fill_max;						AST(DEFAULT(21000))

	int use_scaled_fill_rate;			AST(DEFAULT(0))

	ShaderQualityRule shader_quality;

} DefaultSettingsRules;
extern ParseTable parse_DefaultSettingsRules[];
#define TYPE_parse_DefaultSettingsRules DefaultSettingsRules

extern DefaultSettingsRules default_settings_rules;

extern ParseTable gfx_settings_parseinfo[];

void gfxUpdateSettingsFromDevice(RdrDevice *device, GfxSettings *gfxSettings);
void gfxGetSettings(GfxSettings * gfxSettings);

void gfxGetRecommendedSettings(RdrDevice *device, GfxSettings *gfxSettings);
void gfxGetScaledAdvancedSettings(RdrDevice *device, GfxSettings *gfxSettings, F32 scale);

//Returns the fill value where all settings should be enabled
S32 gfxGetFillMax(void);
S32 gfxGetEffectiveFill(GfxSettings *gfxSettings);

typedef enum ApplySettingsFlags
{
	ApplySettings_AtStartup=0,
	ApplySettings_PastStartup=1,
	ApplySettings_AtStartupHidden=2,
	ApplySettings_STARTUP_MASK = 0x3,

	ApplySettings_OnlyDynamic=4, // specifies to only change things that can quickly be changed on the fly
} ApplySettingsFlags;
void gfxApplySettings(RdrDevice *device, GfxSettings *gfxSettings, ApplySettingsFlags flags, int accessLevel);

void gfxSettingsLoadMinimal(void); // Loads just what is needed before device creation

bool gfxSettingsLoad(GfxPerDeviceState *gfx_device);
void gfxSettingsApplyInitialSettings(RdrDevice * rdr_device, bool allowShow);
void gfxSettingsSave(RdrDevice *device);
void gfxSettingsSaveSoon(void); // Saves after 10 seconds if it doesn't crash
void gfxSetCurrentSettingsForNextTime(void);

void gfxSettingsGetSupportedResolutionsForDeviceType(GfxResolution **desktop_res, GfxResolution *** supported_resolutions, const char * device_type);
void gfxSettingsGetSupportedResolutionsForDevice(GfxResolution **desktop_res, GfxResolution *** supported_resolutions, const RdrDeviceInfo * device_info);
int gfxSettingsGetPreferredAdapter(int preferred_monitor);
void gfxGetWindowSettings(SA_PRE_NN_FREE SA_POST_NN_VALID WindowCreateParams *params);

bool gfxSettingsIsVsync(void); // Handles command-line overrides
bool gfxSettingsIsFullscreen(void); // Handles command-line overrides

bool gfxSettingsIsSupportedHardware(RdrDevice *device);
bool gfxSettingsIsSupportedHardwareEarly(void);
bool gfxSettingsIsKnownCardClass(RdrDevice *device);
void gfxSettingsGetGPUDataCSV(RdrDevice *device, char **esCardData);

void gfxSettingsSetAppDefault(bool maximized, int width, int height);

void gfxSettingsSetMinimalOptions(void);

__forceinline static bool gfxSettingsFeatureSupported(GfxSettings *gfxSettings, GfxFeature feature)
{
	return !!(gfxSettings->features_supported & feature);
}

void gfxEmulateCard(const char *card);
void gfxEmulateCardNext( void );
void gfxEmulateCardToggle( void );

bool gfxSupportsVfetch(void);
void gfxSetTargetHighlight( bool target_highlight_enabled );

void gfxSettingsLoadConfig(void);

void gfxSettingsSetScattering(GfxScatterQuality scatter_quality);
void gfxSettingsSetHDRMaxLuminanceMode(bool enable);

void gfxSettingsSetGamma(float fVal);
float gfxSettingsGetGamma(void);

void gfxSettingsSetSpecialMaterialParam(F32 param);
F32 gfxSettingsGetSpecialMaterialParam(void);

bool gfxSettingsJustAppliedDynamic(void);

bool gfxSettingsMSAAEnabled(const GfxSettings *gfxSettings);
bool gfxSettingsAllowsDepthEffects(const GfxSettings *gfxSettings);

void gfxEnableBloomToneCurve(bool enable);

void gfxSettingsGetScreenSize(const GfxSettings *gfxSettings, int *width, int *height);
void gfxSettingsSetRenderScaleByResolutionAndFill(GfxSettings *gfxSettings);
void gfxSettingsSetResolutionByFill(GfxSettings *gfxSettings);
F32 gfxSettingsGetRecommendedRenderScale(const GfxSettings *gfxSettings);

void gfxSetUIUpdateCB(void(*settingsUIUpdateCallback)(void));
