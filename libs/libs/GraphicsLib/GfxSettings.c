#include "GfxSettings.h"
#include "GfxConsole.h"
#include "GraphicsLibPrivate.h"
#include "GfxTextures.h"
#include "GfxDrawFrame.h"
#include "GfxSurface.h"
#include "GfxCommandParse.h"
#include "GfxMaterials.h"
#include "GfxLightOptions.h"
#include "GfxLightCache.h"
#include "GfxHeadshot.h"
#include "RdrState.h"
#include "RdrShader.h"
#include "TokenStore.h"

#include "../StaticWorld/WorldCell.h"
#include "Materials.h"
#include "MaterialEnums.h"

#include "osdependent.h"
#include "Prefs.h"
#include "TimedCallback.h"
#include "cpu_count.h"
#include "trivia.h"
#include "winutil.h"
#include "sysutil.h"

#define DEBUG_TRACE_SETTINGS 1

#if DEBUG_TRACE_SETTINGS
#include "memlog.h"
#endif

#if DEBUG_TRACE_SETTINGS
#define TRACE_SETTINGS( FMT, ... )	memlog_printf( NULL, FMT, __VA_ARGS__ )
#else
#define TRACE_SETTINGS( FMT, ... )
#endif


#include "AutoGen/GfxSettings_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

static void gfxSettingsSetCardSpecificFeatures(RdrDevice* device);
static void gfxEnforceSettingsConstraints(RdrDevice *device, GfxSettings *gfxSettings, int accessLevel);
static void gfxFindDisplayModeForSettings(DisplayParams *params, const GfxSettings * gfxSettings);

const ResolutionRule * gfxSettingsFindResolutionForFill(F32 fillValue);

static U8 force_prodModeDefaultSettings = 0;
static U8 force_windowed;
static U8 force_fullscreen;
static int windowed_width, windowed_height;

AUTO_CMD_INT(force_prodModeDefaultSettings, gfxForceProdModeDefaultSettings) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0);
static bool gfxUseDevelopmentModeSettings()
{
	return !force_prodModeDefaultSettings && isDevelopmentMode();
}

// These values must also be values that quantize to what the UI slider displays!
#define TEX_LOD_LEVEL_FOR_INCREASE_MIP_1 1.65	// We do not set a LOD bias in code, so this is a waste unless there is a driver lod bias
#define TEX_LOD_LEVEL_FOR_REDUCE_MIP_1 0.80
#define TEX_LOD_LEVEL_FOR_REDUCE_MIP_2 0.60

// These defines might belong in different file...
#define PSSM_SHADOW_MAP_SIZE_ULTRA_HIGH 2048
#define PSSM_SHADOW_MAP_SIZE_HIGH		1024
#define PSSM_SHADOW_MAP_SIZE_MED		512
#define PSSM_SHADOW_MAP_SIZE_LOW		256

#define GFXSETTINGS_DEFAULT_DEVICETYPE_AUTO NULL

static int mipReduceFromLODLevel(F32 lod_level)
{
	if (lod_level > TEX_LOD_LEVEL_FOR_INCREASE_MIP_1)
		return -1;
	if (lod_level > TEX_LOD_LEVEL_FOR_REDUCE_MIP_1)
		return 0;
	if (lod_level > TEX_LOD_LEVEL_FOR_REDUCE_MIP_2)
		return 1;
	return 2;
}

// Starts the game in windowed mode, regardless of previously set options
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(windowed) ACMD_CMDLINE ACMD_CATEGORY(UsefulCommandLine);
void gfxForceWindowed(int windowed)
{
	force_windowed = windowed;
	windowed_width = 0;
	windowed_height = 0;
}

// Starts the game in windowed mode at the specified resolution
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(windowedSize) ACMD_CMDLINE ACMD_CATEGORY(UsefulCommandLine);
void gfxForceWindowedSize(int width, int height)
{
	force_windowed = 1;
	windowed_width = width;
	windowed_height = height;
}

AUTO_RUN_EARLY;
void gfxSettingsInit(void)
{
	gfx_state.settings.frameRateStabilizer = -1;
	gfx_state.settings.maxFps = -1;
}

static bool settings_reset=false;

// Reset all graphics settings and options to default values
AUTO_CMD_INT(settings_reset, resetOptions) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Options) ACMD_COMMANDLINE;

static bool disableNP2=false;
// Override to disable support for NP2 textures even if the card claims to support them
AUTO_CMD_INT(disableNP2, disableNP2) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Options) ACMD_COMMANDLINE;

F32 gfxFakeFillValue = 0.0f;
AUTO_CMD_FLOAT(gfxFakeFillValue, gfxFakeFillValue) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

int gfxUseScaledFillRateForEverything = 0;
AUTO_CMD_INT(gfxUseScaledFillRateForEverything, gfxUseScaledFillRateForEverything) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

F32 gfxFakeMSAAValue = 0.0f;
AUTO_CMD_FLOAT(gfxFakeMSAAValue, gfxFakeMSAAValue) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

F32 gfxMSAAThresholdAlpha = 0.1f;
AUTO_CMD_FLOAT(gfxMSAAThresholdAlpha, gfxMSAAThresholdAlpha) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

bool gfxForceRedoSettings = false;
AUTO_CMD_INT(gfxForceRedoSettings, gfxForceRedoSettings) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

GfxSettings globalGfxSettingsForNextTime;

static bool app_default_maximized = true;
static int app_default_width = 1024;
static int app_default_height = 768;

// Changing this does not reset people's settings, it simply causes gfxGetRecommendedSettings to be called.
// Normally, gfxGetRecommendedSettings is only called if a new element is added to the ParseTable below.
#define SETTINGS_VERSION 6

// The initial settings data interpretation version.
#define SETTINGS_DATA_VERSION_INITIAL_20130620 0
// Change this to force settings data member conversion code to run when reading settings data that does
// not conform to current code interpretation of saved settings fields. See gfxSettingsUpdateDataVersion.
#define SETTINGS_DATA_VERSION_CURRENT 1

void gfxSettingsUpdateDataVersion(GfxSettings *settingsStruct)
{
	if (settingsStruct->settingsDataVersion < SETTINGS_DATA_VERSION_CURRENT)
	{
		// Convert stored monitor_index_plus_one, which is really saved "enumerated adapter index," to 
		// format using separate device type and actual monitor index plus one.
		// Prior format assumed D3D9, D3D11 as hard-coded device types (where available) in that order
		// with combinations of device and monitors listed out in order:
		// D3D9 #1, D3D9 #2, D3D11 #1, D3D11 #2. Reset to default device type if necessary.
		const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
		if (settingsStruct->monitor_index_plus_one)
		{
			int preferred_adapter = settingsStruct->monitor_index_plus_one - 1;
			int numMonitors = multiMonGetNumMonitors();
			// This assume the monitor count hasn't changed.
			int preferred_monitor = preferred_adapter % numMonitors;

			// find which adapter to use
			if (settingsStruct->device_type)
			{
				bool bFoundMonitor = false; 
				// reset to default, unless we find an exact match of the saved
				// device type and the "likely" saved monitor number
				preferred_adapter = -1;
				FOR_EACH_IN_CONST_EARRAY_FORWARDS(device_infos, RdrDeviceInfo, device)
				{
					if (!strcmp(device->type, settingsStruct->device_type))
					{
						preferred_adapter = ideviceIndex;
						if (preferred_monitor == device->monitor_index)
						{
							bFoundMonitor = true;
							break;
						}
					}
				}
				FOR_EACH_END;
				if (!bFoundMonitor)
					// reset to default
					preferred_monitor = 0;
			}
			else
			{
				// assume monitor count hasn't changed, and the enumerated device types
				// haven't changed
				preferred_adapter = preferred_adapter / numMonitors;
				// handle reversed order of D3D 9 vs 11.
				if (preferred_adapter == 0)
					preferred_adapter = 2;
				else
				if (preferred_adapter == 1)
					preferred_adapter = 0;
				else
					// Was D3D9Ex 
					preferred_adapter = 1;
			}

			// any problems and we reset to default
			if (preferred_adapter >= eaSize(&device_infos) || preferred_monitor >= numMonitors)
			{
				// back to default
				preferred_adapter = 0;
				preferred_monitor = 0;
			}

			if (!settingsStruct->device_type)
				settingsStruct->device_type = device_infos[preferred_adapter]->type;

			settingsStruct->monitor_index_plus_one = preferred_monitor + 1;
		}
		settingsStruct->settingsDataVersion = SETTINGS_DATA_VERSION_CURRENT;
	}
}

// Load always, Save these only if not in safe-mode
ParseTable gfx_settings_parseinfo[] = {
	{ "ScreenXPos",				TOK_INT(GfxSettings,screenXPos,0)},
	{ "ScreenYPos",				TOK_INT(GfxSettings,screenYPos,0)},
	{ "Maximized",				TOK_INT(GfxSettings,maximized,0)},
	{ "Fullscreen",				TOK_INT(GfxSettings,fullscreen,0)},
	{ "MonitorIndex",			TOK_INT(GfxSettings,monitor_index_plus_one,0)},
	{ "DeviceType",				TOK_POOL_STRING|TOK_STRING(GfxSettings,device_type,0)},
	{ "RefreshRate",			TOK_INT(GfxSettings,refreshRate,0)},
	{ "Gamma",					TOK_F32(GfxSettings,gamma,0)},
	{ "RenderScaleX",			TOK_F32(GfxSettings,renderScale[0],0)},
	{ "RenderScaleY",			TOK_F32(GfxSettings,renderScale[1],0)},
	{ "AspectRatio",			TOK_F32(GfxSettings,aspectRatio,0)},

	{ "SettingsVersion",		TOK_INT(GfxSettings,settingsVersion,0)},
	{ "SettingsDataVersion",	TOK_INT(GfxSettings,settingsDataVersion,0)},
	{ "SettingsDefaultsVersion",TOK_INT(GfxSettings,settingsDefaultVersion,0)},
	{ "VideoCardVendorID",		TOK_INT(GfxSettings,videoCardVendorID,0)},
	{ "VideoCardDeviceID",		TOK_INT(GfxSettings,videoCardDeviceID,0)},

	{ "ShowAdvanced",			TOK_INT(GfxSettings,showAdvanced,0)},
	{ "GraphicsQuality",		TOK_F32(GfxSettings,slowUglyScale,0)},

	{ "VideoMemoryMaxHMBs",		TOK_INT(GfxSettings,videoMemoryMax,0)},
	{ "WorldTexLODLevel",		TOK_F32(GfxSettings,worldTexLODLevel,0)},
	{ "CharacterTexLODLevel",	TOK_F32(GfxSettings,entityTexLODLevel,0)},
	{ "WorldDetailLevel",		TOK_F32(GfxSettings,worldDetailLevel,0)},
	{ "CharacterDetailLevel",	TOK_F32(GfxSettings,entityDetailLevel,0)},
	{ "TerrainDetailLevel",		TOK_F32(GfxSettings,terrainDetailLevel,1)},
	{ "WorldLoadScale",			TOK_F32(GfxSettings,worldLoadScale,1)},
	{ "ReduceFileStreaming",	TOK_INT(GfxSettings,reduceFileStreaming,0)},

	{ "TexAnisotropy",			TOK_INT(GfxSettings,texAniso,0)},
	{ "SoftParticles",			TOK_INT(GfxSettings,soft_particles,0)},
	{ "FxQuality",				TOK_INT(GfxSettings,fxQuality,0)},
	{ "UseVSync2",				TOK_INT(GfxSettings,useVSync,0)},
	{ "Antialiasing",			TOK_INT(GfxSettings,antialiasing,0)},
	{ "AntialiasingMode",		TOK_AUTOINT(GfxSettings,antialiasingMode,GAA_MSAA), GfxAntialiasingModeEnum},
	{ "SoftwareCursor2",		TOK_INT(GfxSettings,softwareCursor,0)},

	{ "DynamicLights",			TOK_INT(GfxSettings,dynamicLights,0)},
	{ "DrawHighDetail",			TOK_INT(GfxSettings,draw_high_detail,1)},
	{ "DrawHighFillDetail",		TOK_INT(GfxSettings,draw_high_fill_detail,0)},

	{ "DisableSplatShadows",	TOK_INT(GfxSettings,disableSplatShadows,0)},

	{ "GfxFeaturesDesired",		TOK_INT(GfxSettings,features_desired,0)},
	{ "PreloadMaterials",		TOK_INT(GfxSettings,preload_materials,0)},
	{ "FrameRateStabilizer2",	TOK_INT(GfxSettings,frameRateStabilizer,0)},
	{ "AutoEnableFrameRateStabilizer",	TOK_INT(GfxSettings,autoEnableFrameRateStabilizer,0)},
	{ "MaxInactiveFPS",			TOK_F32(GfxSettings,maxInactiveFps,0)},
	{ "RenderThread",			TOK_INT(GfxSettings,renderThread,0)},
	{ "BloomQuality",			TOK_INT(GfxSettings,bloomQuality,2)},
	{ "BloomIntensity",			TOK_F32(GfxSettings,bloomIntensity,1)},
	{ "Scattering",				TOK_INT(GfxSettings,scattering,0)},
	{ "SSAO",					TOK_INT(GfxSettings,ssao,0)},
	{ "LightingQuality",		TOK_INT(GfxSettings,lighting_quality,GLIGHTING_QUALITY_HIGH)},
	{ "MaxReflection",			TOK_INT(GfxSettings,maxReflection, 0)},

	{ "MaxLightsPerObject",		TOK_INT(GfxSettings,maxLightsPerObject,0)},
	{ "MaxShadowedLights",		TOK_INT(GfxSettings,maxShadowedLights,0)},

	{ "HighQualityDOF2",		TOK_INT(GfxSettings,highQualityDOF,0)},
	{ "UseFullSkinning",		TOK_INT(GfxSettings,useFullSkinning,1)},
	{ "GPUAccelParticles",		TOK_INT(GfxSettings,gpuAcceleratedParticles,1)},
	{ "MaxDebris",				TOK_INT(GfxSettings,maxDebris,0)},
	{ "PoissonShadows",			TOK_INT(GfxSettings,poissonShadows,0)},

	{ "LastRecordedPerf",		TOK_F32(GfxSettings,last_recorded_perf,0)},
	{ "LastRecordedMSAAPerf",	TOK_F32(GfxSettings,last_recorded_msaa_perf,0)},
	{ "MinDepthBiasFix",		TOK_INT(GfxSettings,minDepthBiasFix,0)},
	{ "HigherSettingsInTailor",	TOK_INT(GfxSettings,higherSettingsInTailor,0)},

	{ "PerFrameSleep",			TOK_INT(GfxSettings,perFrameSleep,0)},
	{ "MaxFPS",					TOK_INT(GfxSettings,maxFps,0)},
	{ "LenseFlare",				TOK_INT(GfxSettings,lensflare_quality,0)},

	{ "DefaultFov",				TOK_F32(GfxSettings,defaultFOV,0.0)},
	{ "HWInstancing",			TOK_INT(GfxSettings,hwInstancing,1)},

	{ "DesiredMatQuality",		TOK_AUTOINT(GfxSettings, desired_Mat_quality, 0), ShaderGraphQualityEnum },
	{ "RecommendedMatQuality",	TOK_AUTOINT(GfxSettings, recommended_Mat_quality, 0), ShaderGraphQualityEnum },

	{ "HasSelectedFullscreenMode",	TOK_INT(GfxSettings,hasSelectedFullscreenMode,0)},
	{ "FullScreenWidth",		TOK_INT(GfxSettings,fullscreenWidth,0)},
	{ "FullScreenHeight",		TOK_INT(GfxSettings,fullscreenHeight,0)},
	{ "WindowedWidth",			TOK_INT(GfxSettings,windowedWidth,0)},
	{ "WindowedHeight",			TOK_INT(GfxSettings,windowedHeight,0)},

	{ "", 0, 0 }
};

AUTO_RUN;
void InitGfxSettingsParse(void)
{
	ParserSetTableInfo(gfx_settings_parseinfo,sizeof(GfxSettings),"GfxSettings",NULL,__FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}

void gfxUpdateSettingsFromDevice(RdrDevice *device, GfxSettings *gfxSettings)
{
	int maximized, windowed_fullscreen;
	int width, height;
	rdrGetDeviceSize(
		device,
		&gfxSettings->screenXPos, &gfxSettings->screenYPos,
		&width, &height,
		&gfxSettings->refreshRate, &gfxSettings->fullscreen, &maximized, &windowed_fullscreen);

	if(gfxSettings->fullscreen) {
		gfxSettings->fullscreenWidth = width;
		gfxSettings->fullscreenHeight = height;
	} else {
		gfxSettings->windowedWidth = width;
		gfxSettings->windowedHeight = height;
	}

	gfxSettings->maximized = windowed_fullscreen | maximized;
	gfxSettings->videoCardVendorID = system_specs.videoCardVendorID;
	gfxSettings->videoCardDeviceID = system_specs.videoCardDeviceID;
}

void gfxGetSettings(GfxSettings * gfxSettings)
{
	*gfxSettings = gfx_state.settings;
}

static void gfxGetSettingsForNextTime(RdrDevice *device, GfxSettings * gfxSettings)
{
	*gfxSettings = globalGfxSettingsForNextTime;

	gfxUpdateSettingsFromDevice(device, gfxSettings);

	//Attempt to figure out why you sometimes don't get the screen up.
	if( gfxSettings->fullscreenWidth <= 0 )
		gfxSettings->fullscreenWidth = 100;
	if( gfxSettings->fullscreenHeight <= 0 )
		gfxSettings->fullscreenHeight = 100;
	if( gfxSettings->windowedWidth <= 0 )
		gfxSettings->windowedWidth = 100;
	if( gfxSettings->windowedHeight <= 0 )
		gfxSettings->windowedHeight = 100;
}

// New new measurement (no longer resolution dependent, runs on SM20):
// (NV)
// FX5950Ultra	420
// 6200			480
// 6800			1710 or 2700
// Go7700		948 or 2108 (Martin's laptop)
// 7900GS		3600
// 7800GTX		startup: 2587, run-time: 3984
// 7950GT		2300-2600 or 3700-5400
// 7950GTX		2600 or 5100-5200
// 8600GTS		4300
// 8800GTS		9700-10000
// 8800GT		12300-15800  MBaranenko: 7200
// GTX 295		20850
// GTX 480		25000
//
// (ATI)
// 9800 XT		880
// HD5450		1668 (Jimb's tablet PC on battery)
// HD3450		1900 (CLAB)
// X1800		2240 (CLAB)
// X850 XT		2270
// HD5450		3892 (Jimb's tablet PC on A/C)
// HD4650		14370 (CLAB)
// HD3850		16500
// HD4800		23000-27000
// HD5850		74250
// HD5870		90000
//
// (Intel)
// HD200 (HuronLake/SandyBridge) 7100 (\\SandyBridge)
// Havendale	523-895 (\\LMagderWin7)
// GM45			405 (\\IntelDev1)
// G965			120 (\\inteltest)
//
// (S3G)
// Chrome440GTX	2319

#define MSAA_DEGREDATION_ALLOWED 0.95

DefaultSettingsRules default_settings_rules;

#define SETTINGS_FILENAME "client/DefaultSettingsRules.txt"

AUTO_FIXUPFUNC;
TextParserResult DefaultSettingsRules_Fixup(DefaultSettingsRules *rules, enumTextParserFixupType eFixupType, void *pExtraData)
{
	TextParserResult ret = PARSERESULT_SUCCESS;

	switch (eFixupType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			int i;
			FOR_EACH_IN_EARRAY(rules->resolution, ResolutionRule, res)
			{
				if (!res->w || !res->h)
				{
					ErrorFilenamef(SETTINGS_FILENAME, "Invalid width or height specified in resolution rule");
					ret = PARSERESULT_ERROR;
				}
			}
			FOR_EACH_END;
			if (1) // Disable this when checking in new code, re-enable after new data is checked in
			{
				// Verify all thresholds set
				FORALL_PARSETABLE(parse_DefaultSettingsRules, i)
				{
					if (TOK_GET_TYPE(parse_DefaultSettingsRules[i].type) == TOK_F32_X)
					{
						if (!TokenStoreGetF32(parse_DefaultSettingsRules, i, rules, 0, NULL))
						{
							ErrorFilenamef(SETTINGS_FILENAME, "Missing value for field %s", parse_DefaultSettingsRules[i].name );
							ret = PARSERESULT_ERROR;
						}
					}
				}
			}
		}
	}
	return ret;
}

void gfxSettingsLoadConfig(void)
{
	StructInit(parse_DefaultSettingsRules, &default_settings_rules);
	if (isProductionMode() && fileExists(SETTINGS_FILENAME)) // Allow from-text override in production mode without needing to bin first
		ParserLoadFiles(NULL, SETTINGS_FILENAME, NULL, 0, parse_DefaultSettingsRules, &default_settings_rules);
	else
		ParserLoadFiles(NULL, SETTINGS_FILENAME, "bin/DefaultSettingsRules.bin", 0, parse_DefaultSettingsRules, &default_settings_rules);
}


static eCardClass getCardClass(const GfxSettings *settings)
{
	return settings->logical_card_class;
}

static F32 getFillValue(const GfxSettings *settings)
{
	if(gfxFakeFillValue > 0.0f) {
		return gfxFakeFillValue;
	}
	return settings->logical_perf_values.pixelShaderFillValue;
}

static F32 scaleFillValueByResolutionEx(F32 fillValue, const GfxSettings *settings, bool useRenderScale, F32 renderScaleX, F32 renderScaleY)
{
	int width;
	int height;
	float realWidth;
	float realHeight;
	float sizeRatio = 1.0f;
	const ResolutionRule * resolutionAllowedForFill = gfxSettingsFindResolutionForFill(fillValue);

	if (settings->bUseSettingsResolution)
		gfxSettingsGetScreenSize(settings, &width, &height);
	else
		gfxGetActiveDeviceSize(&width, &height);

	realWidth = (F32)width;
	realHeight = (F32)height;
	if(useRenderScale) {
		if(settings->renderScale[0] && settings->renderScale[1]) {
			realWidth *= renderScaleX;
			realHeight *= renderScaleY;
		}
	}

	// Note! Now with the flexible resolution allowance set in the default settings file, we calculate
	// effective fill rate relative to the default full screen resolution rating for the given fill rate,
	// not the old Cryptic Engine default of 1024x768. This allows us to boost the resolution allowance
	// of higher-end cards, making it easier (possible) to tune them separately from low-end cards. The 
	// resolution allowance, in effect, allows encoding of some of our knowledge of the features we will
	// turn on for those cards based on fill rate.
	sizeRatio = (realWidth * realHeight) / (float)(resolutionAllowedForFill->w * resolutionAllowedForFill->h);

	if(sizeRatio) {
		fillValue = fillValue / sizeRatio;
	}

	return fillValue;
}

static F32 scaleFillValueByResolution(F32 fillValue, const GfxSettings *settings, bool useRenderScale) {

	return scaleFillValueByResolutionEx(fillValue, settings, useRenderScale, settings->renderScale[0], settings->renderScale[1]);

}

static F32 scaleFillValueByResolutionAndRenderScale(F32 fillValue, const GfxSettings *settings, F32 renderScale)
{
	return scaleFillValueByResolutionEx(fillValue, settings, true, renderScale, renderScale);
}

static F32 getScaledFillValue(const GfxSettings *settings, bool useRenderScale) {
	return scaleFillValueByResolution(getFillValue(settings), settings, useRenderScale);
}

static F32 getEffectiveFillValue(const GfxSettings *settings, bool useRenderScale)
{
	F32 fill = 0;

	if(default_settings_rules.use_scaled_fill_rate || gfxUseScaledFillRateForEverything) {
		fill = getScaledFillValue(settings, useRenderScale);
	} else {
		fill = getFillValue(settings);
	}

	return fill;
}

//This uses the recommended render scale to scale the card's fill value
static F32 getRecommendedFillValue(const GfxSettings *settings)
{
	F32 fill = 0;

	if(default_settings_rules.use_scaled_fill_rate || gfxUseScaledFillRateForEverything) {
		fill = scaleFillValueByResolutionAndRenderScale(getFillValue(settings), settings, gfxSettingsGetRecommendedRenderScale(settings));
	} else {
		fill = getFillValue(settings);
	}

	return fill;
}

void gfxSettingsSetRenderScaleByResolutionAndFill(GfxSettings *gfxSettings) {

	// If this is the first run (or we're resetting stuff) then we need to do
	// appropriate fill-rate and resolution based renderscale stuff.
	if(default_settings_rules.use_scaled_fill_rate) {

		// We need an effective fill value that's NOT influenced by the renderscale
		// we're messing with now, so just reset the renderscale.
		F32 recommendedRenderScale = gfxSettingsGetRecommendedRenderScale(gfxSettings);

		setVec2(gfxSettings->renderScale, recommendedRenderScale, recommendedRenderScale);
	}
}

F32 gfxSettingsGetRecommendedRenderScale(const GfxSettings *gfxSettings)
{
	F32 fEffectiveFill = getEffectiveFillValue(gfxSettings, false);
	F32 fRecommendedRenderScale = 1.0f;

	// The auto renderscale from resolution overage based on the new "fill-rate allowance" 
	// operates only with the use-scale-fill-rate-for-everything feature.
	if(default_settings_rules.use_scaled_fill_rate) {
		F32 effectiveFillLoss;
		effectiveFillLoss = fsqrt((float)fEffectiveFill / getFillValue(gfxSettings));
		if (effectiveFillLoss < 0.9f)
		{
			F32 renderScale = effectiveFillLoss;
			F32 renderScaleTenths = floorf(effectiveFillLoss * 10) / 10.0f;
			F32 renderScaleEights = floorf(effectiveFillLoss * 8) / 8.0f;
			if (fabs(renderScale - renderScaleTenths) < fabs(renderScale - renderScaleEights))
				renderScale = renderScaleTenths;
			else
				renderScale = renderScaleEights;
			fRecommendedRenderScale = renderScale;
		}
	}

	return fRecommendedRenderScale;
}

const ResolutionRule * gfxSettingsFindResolutionForFill(F32 fillValue) {
	const ResolutionRule * resolutionForFill = default_settings_rules.resolution[0];
	FOR_EACH_IN_EARRAY(default_settings_rules.resolution, ResolutionRule, res)
	{
		if (fillValue >= res->fill)
		{
			resolutionForFill = res;
			break;
		}
	}
	FOR_EACH_END;
	return resolutionForFill;
}

void gfxSettingsSetResolutionBySpecifiedFill(GfxSettings *gfxSettings, F32 fillValue) {
	GfxResolution *desktop_resolution;
	GfxResolution **resolutions = rdrGetSupportedResolutions(&desktop_resolution, multimonGetPrimaryMonitor(), gfxSettings->device_type);
	const ResolutionRule * selectedResolution = NULL;
#if _XBOX
	xboxFixSettings(gfxSettings);
#else
	selectedResolution = gfxSettingsFindResolutionForFill(fillValue);
	gfxSettings->fullscreenWidth = selectedResolution->w;
	gfxSettings->fullscreenHeight = selectedResolution->h;
	setVec2(gfxSettings->renderScale, selectedResolution->renderscale, selectedResolution->renderscale);
#endif

	gfxSettings->hasSelectedFullscreenMode = true;
}

void gfxSettingsSetResolutionByFill(GfxSettings *gfxSettings) {
	gfxSettingsSetResolutionBySpecifiedFill(gfxSettings, getFillValue(gfxSettings));
}

S32 gfxGetEffectiveFill(GfxSettings *gfxSettings) {
	return getRecommendedFillValue(&gfx_state.settings);
}

S32 gfxGetFillMax(void)
{
	return default_settings_rules.fill_max;
}

static F32 getMSAAValue()
{
	if(gfxFakeMSAAValue > 0.0f) {
		return gfxFakeMSAAValue;
	}
	return gfx_state.currentDevice->rdr_perf_times.msaaPerformanceValue;
}

static void gfxGetRecommendedAdvancedSettingsForFill(RdrDevice *device, GfxSettings * gfxSettings, eCardClass cardClass, F32 pixelShaderFill)
{
	if (cardClass == CC_XBOX)
	{
		// Use defaults from above
		gfxSettings->poissonShadows	= 0;

	} else {

		if (pixelShaderFill > 0.1) // Has a value, and has not gone horribly wrong
		{
			if (pixelShaderFill < default_settings_rules.fill_no_effects)
			{
				gfxSettings->features_desired &= ~(GFEATURE_SHADOWS|GFEATURE_POSTPROCESSING|GFEATURE_OUTLINING|GFEATURE_DOF|GFEATURE_WATER|GFEATURE_SCATTERING);
			}

			if (pixelShaderFill < default_settings_rules.fill_lightingquality_low)
			{
				gfxSettings->lighting_quality = GLIGHTING_QUALITY_VERTEX_ONLY_LIGHTING;
			} else {
				gfxSettings->lighting_quality = GLIGHTING_QUALITY_HIGH;
			}

			if (pixelShaderFill < default_settings_rules.fill_no_shadows)
			{
				gfxSettings->disableSplatShadows = 1;
				gfxSettings->features_desired &= ~GFEATURE_SHADOWS;
			} else if (pixelShaderFill < default_settings_rules.fill_splat_shadows)
			{
				gfxSettings->disableSplatShadows = 0;
				gfxSettings->features_desired &= ~GFEATURE_SHADOWS;
			} else {
				gfxSettings->disableSplatShadows = 0;
				gfxSettings->features_desired |= GFEATURE_SHADOWS;
			}

			if (pixelShaderFill < default_settings_rules.fill_detail_05)
			{
				gfxSettings->worldDetailLevel	= 0.25;
				gfxSettings->entityDetailLevel	= 0.5;
				gfxSettings->terrainDetailLevel = 1.0;
			} else if (pixelShaderFill < default_settings_rules.fill_detail_075) {
				gfxSettings->worldDetailLevel	= 0.625;
				gfxSettings->entityDetailLevel	= 0.75;
				gfxSettings->terrainDetailLevel = 1.0;
			} else {
				gfxSettings->worldDetailLevel	= 1.0;
				gfxSettings->entityDetailLevel	= 1.0;
				gfxSettings->terrainDetailLevel = 1.0;
			}

			gfxSettings->bloomIntensity		= 1.0;
			gfxSettings->worldLoadScale		= 1.0;
			gfxSettings->reduceFileStreaming	= system_specs.physicalMemoryMax >= 2000*1024*1024; // just under 2 GB to account for errors in the reporting

			if (pixelShaderFill < default_settings_rules.fill_terrain_low)
				gfxSettings->terrainDetailLevel = 0.8;

			gfxSettings->highQualityDOF = 0;

			if (pixelShaderFill < default_settings_rules.fill_two_bone_skinning)
			{
				gfxSettings->useFullSkinning = 0;
			} else {
				gfxSettings->useFullSkinning = 1;
			}

			if (pixelShaderFill < default_settings_rules.fill_gpu_particles)
			{
				gfxSettings->gpuAcceleratedParticles = 0;
			} else {
				gfxSettings->gpuAcceleratedParticles = 1;
			}

			{
				float msaaTestValue =
				    (pixelShaderFill / default_settings_rules.fill_msaa) * (1.0f - gfxMSAAThresholdAlpha) +
				    (getMSAAValue() / MSAA_DEGREDATION_ALLOWED) * gfxMSAAThresholdAlpha;

				if(rdrSupportsFeature(device, FEATURE_DEPTH_TEXTURE_MSAA) &&
				   msaaTestValue >= 1.0f)
				{
					gfxSettings->antialiasing = 4;
					gfxSettings->antialiasingMode = GAA_MSAA;
				} else {
					gfxSettings->antialiasing = 1;
				}
			}

			if (pixelShaderFill > default_settings_rules.fill_ssao)
			{
				gfxSettings->ssao = 1;
			} else {
				gfxSettings->ssao = 0;
			}

			if (pixelShaderFill > default_settings_rules.fill_lensflare)
			{
				gfxSettings->lensflare_quality = 2;
			} else {
				gfxSettings->lensflare_quality = 0;
			}

			if (pixelShaderFill < default_settings_rules.fill_vfx_low)
			{
				gfxSettings->fxQuality = 0;
			} else if (pixelShaderFill < default_settings_rules.fill_vfx_medium)
			{
				gfxSettings->fxQuality = 1;
			} else {
				gfxSettings->fxQuality = 2;
			}

			if (pixelShaderFill < default_settings_rules.shader_quality.mid)
			{
				gfxSettings->recommended_Mat_quality = SGRAPH_QUALITY_LOW;
			}
			else if (pixelShaderFill < default_settings_rules.shader_quality.high)
			{
				gfxSettings->recommended_Mat_quality = SGRAPH_QUALITY_MID;
			}
			else if (pixelShaderFill < default_settings_rules.shader_quality.veryHigh)
			{
				gfxSettings->recommended_Mat_quality = SGRAPH_QUALITY_HIGH;
			}
			else
			{
				gfxSettings->recommended_Mat_quality = SGRAPH_QUALITY_VERY_HIGH;
			}

			if (pixelShaderFill < default_settings_rules.fill_scattering)
				gfxSettings->scattering = 0;
			else
				gfxSettings->scattering = 1;

			if(default_settings_rules.use_scaled_fill_rate || gfxUseScaledFillRateForEverything) {

				// Soft particles
				if(pixelShaderFill < default_settings_rules.fill_soft_particles) {
					gfxSettings->soft_particles = 0;
				} else {
					gfxSettings->soft_particles = 1;
				}

				// Dynamic lights
				if(pixelShaderFill < default_settings_rules.fill_dynamic_lights) {
					gfxSettings->dynamicLights = 0;
				} else {
					gfxSettings->dynamicLights = 1;
				}

				// Draw high detail
				if(pixelShaderFill < default_settings_rules.fill_draw_high_detail) {
					gfxSettings->draw_high_detail = 0;
				} else {
					gfxSettings->draw_high_detail = 1;
				}

				// Draw high fill detail
				if(pixelShaderFill < default_settings_rules.fill_draw_high_fill_detail) {
					gfxSettings->draw_high_fill_detail = 0;
				} else {
					gfxSettings->draw_high_fill_detail = 1;
				}

				// Bloom quality
				if(pixelShaderFill < default_settings_rules.fill_bloom_quality1) {
					gfxSettings->bloomQuality = GBLOOM_OFF;
				} else if(pixelShaderFill < default_settings_rules.fill_bloom_quality2) {
					gfxSettings->bloomQuality = GBLOOM_LOW_BLOOMWHITE;
				} else {
					gfxSettings->bloomQuality = GBLOOM_MED_BLOOM_SMALLHDR;
				}

				// Poisson shadows
				if(pixelShaderFill < default_settings_rules.fill_poisson_shadows) {
					gfxSettings->poissonShadows = 0;
				} else {
					gfxSettings->poissonShadows = 1;
				}

				// Anisotropic filtering
				if(pixelShaderFill < default_settings_rules.fill_tex_aniso4) {
					gfxSettings->texAniso = 1;
				} else if(pixelShaderFill < default_settings_rules.fill_tex_aniso16) {
					gfxSettings->texAniso = 4;
				} else {
					gfxSettings->texAniso = 16;
				}

				// Lights and number of shadowed lights.
				{
					F32 minLights = gfx_lighting_options.max_static_lights_per_object;
					F32 maxLights = rdrMaxSupportedObjectLights();
					F32 numLightsScale =
						(maxLights - minLights) *
						CLAMP(((pixelShaderFill - default_settings_rules.fill_min_lights_per_object) /
							   (default_settings_rules.fill_max_lights_per_object - default_settings_rules.fill_min_lights_per_object)), 0, 1);
					int selectedNumLights = round(numLightsScale) + minLights;

					F32 minShadowedLights = 1;
					F32 maxShadowedLights = gfx_lighting_options.max_shadowed_lights_per_frame;
					F32 numShadowedLightsScale =
						(maxShadowedLights - minShadowedLights) *
						CLAMP(((pixelShaderFill - default_settings_rules.fill_min_shadowed_lights) /
							   (default_settings_rules.fill_max_shadowed_lights - default_settings_rules.fill_min_shadowed_lights)), 0, 1);
					int selectedNumShadowedLights = round(numShadowedLightsScale) + minShadowedLights;

					gfxSettings->maxLightsPerObject = selectedNumLights;
					gfxSettings->maxShadowedLights = selectedNumShadowedLights;
				}
			}
		}
	}
}

static void gfxGetRecommendedAdvancedSettingsFromPerftimes(RdrDevice *device, GfxSettings * gfxSettings, eCardClass cardClass)
{
	gfxGetRecommendedAdvancedSettingsForFill(device, gfxSettings, cardClass, getEffectiveFillValue(gfxSettings, true));
}

static bool isIntelExtreme(int vendorID, int deviceID)
{
	// taken from http://cvs.sourceforge.net/viewcvs.py/advancemame/advcd/doc/cardcd.d?rev=1.4
	int IntelExtremeDeviceIDs[][2] = {
		{0x8086, 0x7121},//	+82810 810 Chipset Graphics Controller 
		{0x1014, 0x7121},//		+OEM IBM {1014} : 82810 Graphics Controller 
		{0x4c53, 0x7121},//		+OEM SBS Technologies (AKA: SBS-Or Industrial Computers) {4c53} : CL7 mainboard 
		{0x8086, 0x7123},//	+82810-DC100 810 Chipset Graphics Controller [7123] 
		{0x1014, 0x7123},//		+OEM IBM {1014} : 82810-DC100 Graphics Controller 
		{0x8086, 0x7125},//	+82810E 810e Graphics Controller [7125] 
		{0x1014, 0x7125},//		+OEM IBM {1014} : 82810E Graphics Controller 
		{0x110a, 0x7125},//		+OEM Siemens PC Systeme GmBH {110a} : 82810E 810e Chipset Graphics Controller 
		{0x1de1, 0x7125},//		+OEM Tekram Technology {1de1} : TRM-S381 Fe Mainboard 
		{0x8086, 0x1102},//	+82815 815/E (Solano) Internal GUI Accelerator [1102] 
		{0x8086, 0x1112},//	+82815 815/E (Solano) Internal GUI Accelerator [1112] 
		{0x8086, 0x1132},//	+82815/EM/EP/P 815/EM/EP/P (Solano) Interal GUI Accelerator [1132] 
		{0x1025, 0x1132},//		+OEM Acer Inc {1025} : Travelmate 612 TX 
		{0x103c, 0x1132},//		+OEM Hewlett-Packard Company {103c} : Vectra VL400DT Integrated Video 
		{0x1462, 0x1132},//		+OEM Micro-Star International Co Ltd (MSI) {1462} : MS-6337 Internal Graphics Device 
		{0x104d, 0x1132},//		+OEM Sony Corp {104d} : Vaio PCG-FX290 notebook Integrated Video 
		{0x8086, 0x3577},//	+82830M/MG Integrated Video [3577] 
		{0x1014, 0x3577},//		+OEM IBM {1014} : ThinkPad A/T/X Series 
		{0x8086, 0x2562},//	+82845G/GL/GV/GE/PE Integrated Graphics Device [2562] 
		{0x1014, 0x2562},//		+OEM IBM {1014} : NetVista A30p 
		{0x8086, 0x3582},//	+82852GM/GME/GMV/PM, 855GM/GME Montara Integrated Graphics Device [3582]
		{0x1028, 0x3582},//		+OEM Dell Computer Corp {1028} : Latitude D505 
		{0x4c53, 0x3582},//		+OEM SBS Technologies (AKA: SBS-Or Industrial Computers) {4c53} : CL9 mainboard 
		{0x8086, 0x2572},//	+82865G Integrated Graphics Device [2572] 
		{0x8086, 0x7800},//	+Intel740 AGP Graphics Accelerator [7800] 
		{0x1043, 0x7800},//		+OEM ASUSTeK Computer Inc {1043} : AGP-V2740 
		{0x003d, 0x7800},//		+OEM Real 3D (Lockheed Martin-Marietta Corp) {003d} : Real 3D Starfighter/Starfighter AGP
		{0x10b4, 0x7800},//		+OEM STB Systems {10b4} : Lightspeed 740 
	};
	int i;
	for (i=0; i<ARRAY_SIZE(IntelExtremeDeviceIDs); i++) {
		if (vendorID == IntelExtremeDeviceIDs[i][0] &&
			deviceID == IntelExtremeDeviceIDs[i][1])
		{
			system_specs.videoCardVendorID = VENDOR_INTEL; // Write this as being an Intel card
			return true;
		}
	}
	return false;
}

static eCardClass getDeviceCardClass(RdrDevice *device)
{
	eCardClass cardClass;
	systemSpecsInit();

	cardClass = CC_UNKNOWN;

	if( system_specs.videoCardVendorID == VENDOR_ATI )
	{
		// Can find a list here: http://listing.driveragent.com/pci/1002/
		int ATIDeviceIds[][2] = {
			{ 0x3150,	CC_NV3_OR_ATI8500 },	// Mobility X600
			{ 0x3152,	CC_NV3_OR_ATI8500 },	// Mobility X300
			{ 0x3154,	CC_NV3_OR_ATI8500 },	// Mobility FireGL V3200
			{ 0x3e50,	CC_NV4_OR_ATI9500 },	// X600
			{ 0x3e54,	CC_NV4_OR_ATI9500 },	// FireGL V3200
			{ 0x4136,	CC_NV3_OR_ATI8500 },	// IGP 320
			{ 0x4137,	CC_NV3_OR_ATI8500 },	// IGP 340
			{ 0x4144,	CC_NV4_OR_ATI9500 },	// 9500
			{ 0x4145,	CC_NV4_OR_ATI9500 },	// 9700 (R300)
			{ 0x4146,	CC_NV4_OR_ATI9500 },	// 9600TX
			{ 0x4147,	CC_NV4_OR_ATI9500 },	// FireGL Z1 (9500?)
			{ 0x4148,	CC_NV4_OR_ATI9500 },	// 9800 SE
			{ 0x4149,	CC_NV4_OR_ATI9500 },	// 9500
			{ 0x414a,	CC_NV4_OR_ATI9500 },	// 9800 Family
			{ 0x414b,	CC_NV4_OR_ATI9500 },	// Fire GL X2
			{ 0x4150,	CC_NV4_OR_ATI9500 },	// 9600
			{ 0x4151,	CC_NV4_OR_ATI9500 },	// 9600
			{ 0x4152,	CC_NV4_OR_ATI9500 },	// 9600
			{ 0x4153,	CC_NV4_OR_ATI9500 },	// 9550
			{ 0x4154,	CC_NV4_OR_ATI9500 },	// FireGL T2
			{ 0x4155,	CC_NV4_OR_ATI9500 },	// FireGL T2
			{ 0x4156,	CC_NV4_OR_ATI9500 },	// FireGL T2
			{ 0x4157,	CC_NV4_OR_ATI9500 },	// FireGL T2
			{ 0x4164,	CC_NV4_OR_ATI9500 },	// 9500
			{ 0x4167,	CC_NV4_OR_ATI9500 },	// 9500
			{ 0x4170,	CC_NV4_OR_ATI9500 },	// 9600
			{ 0x4242,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x4964,	CC_NV3_OR_ATI8500 },	// 9000
			{ 0x4965,	CC_NV3_OR_ATI8500 },	// 9000
			{ 0x4966,	CC_NV3_OR_ATI8500 },	// 9000
			{ 0x4967,	CC_NV3_OR_ATI8500 },	// 9000
			{ 0x496e,	CC_NV3_OR_ATI8500 },	// 9000
			{ 0x4a49,	CC_NV4_OR_ATI9500 },	// X800
			{ 0x4c57,	CC_INTELEXTREME   },	// Mobility Radeon 7500
			{ 0x4c58,	CC_INTELEXTREME   },	// FireGL Mobility (7800?)
			{ 0x4c64,	CC_NV3_OR_ATI8500 },	// 9000
			{ 0x4c65,	CC_NV3_OR_ATI8500 },	// 9000
			{ 0x4c66,	CC_NV3_OR_ATI8500 },	// 9000
			{ 0x4c67,	CC_NV3_OR_ATI8500 },	// 9000
			{ 0x4c6e,	CC_NV3_OR_ATI8500 },	// 9000
			{ 0x4e44,	CC_NV4_OR_ATI9500 },	// 9700
			{ 0x4e45,	CC_NV4_OR_ATI9500 },	// 9700
			{ 0x4e46,	CC_NV4_OR_ATI9500 },	// 9700
			{ 0x4e47,	CC_NV4_OR_ATI9500 },	// 9700
			{ 0x4e48,	CC_NV4_OR_ATI9500 },	// 9800
			{ 0x4e49,	CC_NV4_OR_ATI9500 },	// 9800
			{ 0x4e4a,	CC_NV4_OR_ATI9500 },	// 9800
			{ 0x4e64,	CC_NV4_OR_ATI9500 },	// 9700
			{ 0x4e65,	CC_NV4_OR_ATI9500 },	// 9700
			{ 0x4e66,	CC_NV4_OR_ATI9500 },	// 9700
			{ 0x4e67,	CC_NV4_OR_ATI9500 },	// 9700
			{ 0x4e68,	CC_NV4_OR_ATI9500 },	// 9800
			{ 0x4e69,	CC_NV4_OR_ATI9500 },	// 9800
			{ 0x4e6a,	CC_NV4_OR_ATI9500 },	// 9800
			{ 0x5144,	CC_INTELEXTREME   },	// Radeon 7200
			{ 0x5145,	CC_INTELEXTREME   },	// Radeon QE (7200?)
			{ 0x5146,	CC_INTELEXTREME   },	// Radeon QF (7200)
			{ 0x5147,	CC_INTELEXTREME   },	// 7200
			{ 0x5148,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x5149,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x514a,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x514b,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x514c,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x514d,	CC_NV3_OR_ATI8500 },	// 9100
			{ 0x514e,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x514f,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x5157,	CC_INTELEXTREME   },	// Radeon 7500 (RV200)
			{ 0x5158,	CC_INTELEXTREME   },	// 7500
			{ 0x5159,	CC_INTELEXTREME   },	// 7000
			{ 0x515a,	CC_INTELEXTREME   },	// 7000
			{ 0x515e,	CC_INTELEXTREME   },	// Radeon ES1000
			{ 0x5168,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x5169,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x516a,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x516b,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x516c,	CC_NV3_OR_ATI8500 },	// 8500
			{ 0x5830,	CC_NV3_OR_ATI8500 },	// IGP 9100
			{ 0x5831,	CC_NV3_OR_ATI8500 },	// IGP 9100
			{ 0x5832,	CC_NV3_OR_ATI8500 },	// IGP 9100
			{ 0x5833,	CC_NV3_OR_ATI8500 },	// IGP 9100
			{ 0x5834,	CC_NV3_OR_ATI8500 },	// IGP 9100
			{ 0x5835,	CC_NV3_OR_ATI8500 },	// IGP 9100
			{ 0x5836,	CC_NV3_OR_ATI8500 },	// IGP 9100
			{ 0x5837,	CC_NV3_OR_ATI8500 },	// IGP 9100
			{ 0x5838,	CC_NV3_OR_ATI8500 },	// IGP 9100
			{ 0x5940,	CC_NV3_OR_ATI8500 },	// Radeon 9200 Pro Secondary
			{ 0x5941,	CC_NV3_OR_ATI8500 },	// ATI Radeon 9200 Secondary (RV280)
			{ 0x5954,	CC_INTELEXTREME   },	// ATI Radeon Xpress 200 (RS482)
			{ 0x5960,	CC_NV3_OR_ATI8500 },	// Radeon 9200 Pro
			{ 0x5961,	CC_NV3_OR_ATI8500 },	// Radeon 9200 SE (RV280)
			{ 0x5964,	CC_NV3_OR_ATI8500 },	// Radeon 9200 SE
			{ 0x5a33,	CC_INTELEXTREME   },	// Radeon Xpress 200 (Northbridge)
			{ 0x5a42,	CC_INTELEXTREME   },	// ATI Radeon Xpress Series (R400M)
			{ 0x5a61,	CC_INTELEXTREME   },	// ATI Radeon Xpress 200 (RC410)
			{ 0x5d44,	CC_NV3_OR_ATI8500 },	// Radeon 9200 SE Secondary (RV280)
			{ 0x6704,	CC_NV8            },	// AMD FirePro V7900 (FireGL V)
			{ 0x6707,	CC_NV8            },	// AMD FirePro V5900 (FireGL V)
			{ 0x6718,	CC_NV8            },	// AMD Radeon HD 6900 Series
			{ 0x6719,	CC_NV8            },	// AMD Radeon HD 6900 Series
			{ 0x671D,	CC_NV8            },	// AMD Radeon HD 6900 Series
			{ 0x671F,	CC_NV8            },	// AMD Radeon HD 6900 Series
			{ 0x6720,	CC_NV3_OR_ATI8500 },	// AMD Radeon HD 6900M Series
			{ 0x6738,	CC_NV8            },	// AMD Radeon HD 6800 Series
			{ 0x6738,	CC_NV8            },	// AMD Radeon HD 6870 X2
			{ 0x6739,	CC_NV8            },	// AMD Radeon HD 6800 Series
			{ 0x6739,	CC_NV8            },	// AMD Radeon HD 6850 X2
			{ 0x673E,	CC_NV3_OR_ATI8500 },	// AMD Radeon HD 6700 Series
			{ 0x6740,	CC_NV3_OR_ATI8500 },	// AMD Radeon HD 6700M Series
			{ 0x6741,	CC_NV3_OR_ATI8500 },	// AMD Radeon 6600M and 6700M Series
			{ 0x6742,	CC_NV8            },	// AMD Radeon HD 7500/7600 Series
			{ 0x6749,	CC_NV8            },	// AMD FirePro V4900 (FireGL V)
			{ 0x674A,	CC_NV8            },	// AMD FirePro V3900 (ATI FireGL)
			{ 0x6750,	CC_NV8            },	// AMD Radeon HD 6600A Series
			{ 0x6758,	CC_NV8            },	// AMD Radeon HD 6670
			{ 0x6758,	CC_NV8            },	// AMD Radeon HD 7670
			{ 0x6759,	CC_NV8            },	// AMD Radeon HD 6570 Graphics
			{ 0x6759,	CC_NV8            },	// AMD Radeon HD 7570
			{ 0x6759,	CC_NV8            },	// AMD Radeon HD 7570 Series
			{ 0x675B,	CC_NV8            },	// AMD Radeon HD 7600 Series
			{ 0x675D,	CC_NV8            },	// AMD Radeon HD 7570
			{ 0x675F,	CC_NV8            },	// AMD Radeon HD 5500 Series
			{ 0x675F,	CC_NV8            },	// AMD Radeon HD 6510 Series
			{ 0x675F,	CC_NV8            },	// AMD Radeon HD 6530 Series
			{ 0x675F,	CC_NV8            },	// AMD Radeon HD 7510
			{ 0x6760,	CC_NV3_OR_ATI8500 },	// AMD Radeon HD 6400M Series
			{ 0x6761,	CC_NV3_OR_ATI8500 },	// AMD Radeon HD 6430M
			{ 0x6761,	CC_NV8            },	// AMD Radeon HD 6450
			{ 0x6763,	CC_NV3_OR_ATI8500 },	// AMD Radeon E6460
			{ 0x6770,	CC_NV8            },	// AMD Radeon HD 6400 Series
			{ 0x6778,	CC_NV8            },	// AMD Radeon HD 7000 series
			{ 0x6779,	CC_NV8            },	// AMD Radeon HD 6450
			{ 0x6779,	CC_NV8            },	// AMD RADEON HD 7450
			{ 0x677B,	CC_NV3_OR_ATI8500 },	// AMD Radeon 7400
			{ 0x677B,	CC_NV8            },	// AMD Radeon HD 7400 Series
			{ 0x6798,	CC_NV8            },	// AMD Radeon HD 7900 Series
			{ 0x679A,	CC_NV8            },	// AMD Radeon HD 7900 Series
			{ 0x6800,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7970M
			{ 0x6818,	CC_NV8            },	// AMD Radeon HD 7800 Series
			{ 0x6819,	CC_NV8            },	// AMD Radeon HD 7800 Series
			{ 0x6825,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7800M Series
			{ 0x6827,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7800M Series
			{ 0x682D,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7700M Series
			{ 0x682F,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7700M Series
			{ 0x683D,	CC_NV8            },	// AMD Radeon HD 7700 Series
			{ 0x683F,	CC_NV8            },	// AMD Radeon HD 7700 Series
			{ 0x6840,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7600M Series
			{ 0x6841,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7500M/7600M Series
			{ 0x6842,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7000M Series
			{ 0x6843,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7670M
			{ 0x6888,	CC_NV8            },	// ATI FirePro V8800 (FireGL) Graphics Adapter
			{ 0x6889,	CC_NV8            },	// ATI FirePro V7800 (FireGL) Graphics Adapter
			{ 0x688A,	CC_NV8            },	// ATI FirePro V9800 (FireGL V)
			{ 0x688C,	CC_NV8            },	// AMD FireStream 9370
			{ 0x688D,	CC_NV8            },	// AMD FireStream 9350
			{ 0x6898,	CC_NV8            },	// AMD Radeon HD 6800 Series
			{ 0x6898,	CC_NV8            },	// ATI Radeon HD 5800 Series
			{ 0x6899,	CC_NV8            },	// AMD Radeon HD 6850
			{ 0x6899,	CC_NV8            },	// ATI Radeon HD 5800 Series
			{ 0x6899,	CC_NV8            },	// ATI Radeon HD 5850X2
			{ 0x689B,	CC_NV8            },	// AMD Radeon HD 6800 Series
			{ 0x689C,	CC_NV8            },	// ATI Radeon HD 5900 Series
			{ 0x689E,	CC_NV8            },	// ATI Radeon HD 5800 Series
			{ 0x68A0,	CC_NV8            },	// ATI Mobility Radeon HD 5800 Series
			{ 0x68A1,	CC_NV8            },	// ATI Mobility Radeon HD 5800 Series
			{ 0x68A8,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 6800M Series
			{ 0x68A9,	CC_NV8            },	// ATI FirePro V5800 (FireGL V)
			{ 0x68B8,	CC_NV8            },	// AMD Radeon HD 6700 Series
			{ 0x68B8,	CC_NV8            },	// ATI Radeon HD 5700 Series
			{ 0x68B9,	CC_NV8            },	// ATI Radeon HD 5600/5700
			{ 0x68BA,	CC_NV8            },	// AMD Radeon HD 6700 Series
			{ 0x68BE,	CC_NV8            },	// AMD Radeon HD 6750 Graphics
			{ 0x68BE,	CC_NV8            },	// ATI Radeon HD 5700 Series
			{ 0x68BE,	CC_NV8            },	// ATI Radeon HD 6750
			{ 0x68BF,	CC_NV8            },	// AMD Radeon HD 6700 Series
			{ 0x68BF,	CC_NV8            },	// AMD Radeon HD 6750
			{ 0x68C0,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 6570M/5700 Series
			{ 0x68C1,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 6500M/5600/5700 Series
			{ 0x68C1,	CC_NV8            },	// ATI Radeon HD 5000 Series
			{ 0x68C7,	CC_NV4_OR_ATI9500 },	// ATI Mobility Radeon HD 5570
			{ 0x68C8,	CC_NV8            },	// ATI FirePro V4800 (FireGL) Graphics Adapter
			{ 0x68C9,	CC_NV8            },	// ATI FirePro 3800 (FireGL) Graphics Adapter
			{ 0x68D8,	CC_NV8            },	// ATI Radeon HD 5670
			{ 0x68D8,	CC_NV8            },	// ATI Radeon HD 5690
			{ 0x68D8,	CC_NV8            },	// ATI Radeon HD 5730
			{ 0x68D9,	CC_NV8            },	// ATI Radeon HD 5570
			{ 0x68D9,	CC_NV8            },	// ATI Radeon HD 5630
			{ 0x68D9,	CC_NV8            },	// ATI Radeon HD 6510
			{ 0x68D9,	CC_NV8            },	// ATI Radeon HD 6610
			{ 0x68DA,	CC_NV8            },	// ATI Radeon HD 5500 Series
			{ 0x68DA,	CC_NV8            },	// ATI Radeon HD 5570
			{ 0x68DA,	CC_NV8            },	// ATI Radeon HD 5630
			{ 0x68DA,	CC_NV8            },	// ATI Radeon HD 6390
			{ 0x68DA,	CC_NV8            },	// ATI Radeon HD 6490
			{ 0x68E0,	CC_NV4_OR_ATI9500 },	// ATI Mobility Radeon HD 5400 Series
			{ 0x68E0,	CC_NV8            },	// ATI Radeon HD 5400 Series
			{ 0x68E0,	CC_NV8            },	// ATI Radeon HD 5450
			{ 0x68E1,	CC_NV4_OR_ATI9500 },	// ATI Mobility Radeon HD 5000 Series
			{ 0x68E1,	CC_NV4_OR_ATI9500 },	// ATI Mobility Radeon HD 5400 Series
			{ 0x68E1,	CC_NV8            },	// AMD Radeon HD 6230
			{ 0x68E1,	CC_NV8            },	// AMD Radeon HD 7350
			{ 0x68E1,	CC_NV8            },	// ATI Radeon HD 6350
			{ 0x68E1,	CC_NV8            },	// ATI Radeon HD 5400 Series
			{ 0x68E1,	CC_NV8            },	// ATI Radeon HD 5470
			{ 0x68E1,	CC_NV8            },	// ATI Radeon HD 6230
			{ 0x68E1,	CC_NV8            },	// ATI Radeon HD 6250
			{ 0x68E1,	CC_NV8            },	// ATI Radeon HD 6350
			{ 0x68E1,	CC_NV8            },	// ATI Radeon HD 7350
			{ 0x68E4,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 6300M Series
			{ 0x68E5,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 6300M Series
			{ 0x68E5,	CC_NV8            },	// ATI Radeon HD 5400 Series
			{ 0x68E5,	CC_NV8            },	// ATI Radeon HD 6350
			{ 0x68E5,	CC_NV8            },	// ATI Radeon HD 7350
			{ 0x68F1,	CC_NV8            },	// AMD FirePro 2460
			{ 0x68F2,	CC_NV8            },	// AMD FirePro 2270 (ATI FireGL)
			{ 0x68F9,	CC_NV8            },	// AMD Radeon HD 6350
			{ 0x68F9,	CC_NV8            },	// AMD Radeon HD 7350
			{ 0x68F9,	CC_NV8            },	// ATI Radeon HD 5450
			{ 0x68F9,	CC_NV8            },	// ATI Radeon HD 5470
			{ 0x68F9,	CC_NV8            },	// ATI Radeon HD 5490
			{ 0x68F9,	CC_NV8            },	// ATI Radeon HD 5530
			{ 0x68F9,	CC_NV8            },	// ATI Radeon HD 6230
			{ 0x68F9,	CC_NV8            },	// ATI Radeon HD 6250
			{ 0x68F9,	CC_NV8            },	// ATI Radeon HD 6290
			{ 0x68F9,	CC_NV8            },	// ATI Radeon HD 6350 Graphics
			{ 0x68F9,	CC_NV8            },	// ATI Radeon HD 7350
			{ 0x68FA,	CC_NV8            },	// AMD Radeon HD 7350
			{ 0x9640,	CC_NV8            },	// AMD Radeon HD 6550D
			{ 0x9641,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 6620G
			{ 0x9642,	CC_NV8            },	// AMD Radeon HD 6370D
			{ 0x9643,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 6380G
			{ 0x9644,	CC_NV8            },	// AMD Radeon HD 6410D
			{ 0x9645,	CC_NV8            },	// AMD Radeon HD 6410D
			{ 0x9647,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 6520G
			{ 0x9648,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 6480G
			{ 0x9649,	CC_NV4_OR_ATI9500 },	// AMD Radeon(TM) HD 6480G
			{ 0x964A,	CC_NV8            },	// AMD Radeon HD 6530D
			{ 0x9802,	CC_NV8            },	// AMD Radeon HD 6310 Graphics
			{ 0x9803,	CC_NV8            },	// AMD Radeon HD 6250 Graphics
			{ 0x9804,	CC_NV8            },	// AMD Radeon HD 6250 Graphics
			{ 0x9805,	CC_NV8            },	// AMD Radeon HD 6250 Graphics
			{ 0x9806,	CC_NV8            },	// AMD Radeon HD 6320 Graphics
			{ 0x9807,	CC_NV8            },	// AMD Radeon HD 6290 Graphics
			{ 0x9808,	CC_NV8            },	// AMD Radeon HD 7340 Graphics
			{ 0x9809,	CC_NV8            },	// AMD Radeon HD 7310 Graphics
			{ 0x9900,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7660G
			{ 0x9901,	CC_NV8            },	// AMD Radeon HD 7660D
			{ 0x9903,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7640G
			{ 0x9904,	CC_NV8            },	// AMD Radeon HD 7560D
			{ 0x9906,	CC_NV8            },	// AMD FirePro A300 Series (FireGL V) Graphics Adapter
			{ 0x9907,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7620G
			{ 0x9907,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7620G
			{ 0x9908,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7600G
			{ 0x990A,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7500G
			{ 0x9910,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7660G
			{ 0x9913,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7640G
			{ 0x9990,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7520G
			{ 0x9991,	CC_NV8            },	// AMD Radeon HD 7540D
			{ 0x9992,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7420G
			{ 0x9993,	CC_NV8            },	// AMD Radeon HD 7480D
			{ 0x9994,	CC_NV4_OR_ATI9500 },	// AMD Radeon HD 7400G
		};

		int i;
		for (i=0; i<ARRAY_SIZE(ATIDeviceIds); i++) {
			if (ATIDeviceIds[i][0] == system_specs.videoCardDeviceID) {
				cardClass = ATIDeviceIds[i][1];
				break;
			}
		}

		// Catch-all for cards not in the list.
		if(cardClass == CC_UNKNOWN &&
		   (rdrSupportsFeature(device, FEATURE_SM2B) ||
			rdrSupportsFeature(device, FEATURE_SM30))) {

			cardClass = CC_UNKNOWN_SM2PLUS;

			if(rdrSupportsFeature(device, FEATURE_DX10_LEVEL_CARD)) {

				// DX10 and above cards should just go into the NV8 bucket if we don't
				// have any specific information for them.
				cardClass = CC_NV8;
			}
		}
	}
	else if( system_specs.videoCardVendorID == VENDOR_NV )
	{
		if (rdrSupportsFeature(device, FEATURE_DX10_LEVEL_CARD))
			cardClass = CC_NV8;
		else if( system_specs.videoCardDeviceID >= 0x0203/* || isGeForce6(system_specs.videoCardVendorID, system_specs.videoCardDeviceID)*/)
			cardClass = CC_NV4_OR_ATI9500;
		else if( system_specs.videoCardDeviceID >= 0x0200 )
			cardClass = CC_NV3_OR_ATI8500;
		else
			cardClass = CC_NV4_OR_ATI9500;
	}
	else if (system_specs.videoCardVendorID == VENDOR_XBOX360)
	{
		cardClass = CC_XBOX;
	}
	else if (system_specs.videoCardVendorID == VENDOR_WINE)
	{
		cardClass = CC_NV8;
	}
	else
	{
		if (isIntelExtreme(system_specs.videoCardVendorID, system_specs.videoCardDeviceID)) {
			cardClass = CC_INTELEXTREME;
			system_specs.videoCardVendorID = VENDOR_INTEL;
		} else {
			if (system_specs.videoCardVendorID == 0x1106) { // Via
				system_specs.videoCardVendorID = VENDOR_S3G;
			}
		}
		if (cardClass == CC_UNKNOWN) {
			// Might be an Intel GMA/Grantsdale card?
			cardClass = CC_NV4_OR_ATI9500;
		}
	}
	if (system_specs.videoCardVendorID == VENDOR_S3G) {
		cardClass = CC_NV4_OR_ATI9500;
	}

	return cardClass;
}

bool gfxSettingsIsKnownCardClass(RdrDevice *device) {
	eCardClass cardClass = getDeviceCardClass(device);
	return !(cardClass == CC_UNKNOWN || cardClass == CC_UNKNOWN_SM2PLUS);
}

void gfxSettingsGetGPUDataCSV(RdrDevice *device, char **esCardData) {

	char *featureList = NULL;
	unsigned int i = 1;

	// Generate CSV columns for supported features for whatever video card this is.
	while(i <= FEATURE_TESSELLATION) {
		estrConcatf(&featureList, "%d,", rdrSupportsFeature(device, i));
		i <<= 1;
	}

	// Generate CSV columns for all the information we have about this video card.
	estrPrintf(
		esCardData,
		"%s,%s,%.8x,%.8x,%.8x,%s,%.8x,%.8x,%s,%f,%f",
		GetProductName(),
		system_specs.videoCardName,
		system_specs.videoCardNumeric,
		system_specs.videoCardVendorID,
		system_specs.videoCardDeviceID,
		system_specs.videoDriverVersion,
		system_specs.videoDriverVersionNumber,
		system_specs.videoMemory,
		featureList,
		gfx_state.settings.last_recorded_perf,
		gfx_state.settings.last_recorded_msaa_perf);
}

bool gfxSettingsIsSupportedHardwareEarly(void)
{
	systemSpecsInit();
	if (system_specs.videoCardVendorID == VENDOR_S3G ||
		system_specs.videoCardVendorID == VENDOR_INTEL ||
		system_specs.videoCardVendorID == VENDOR_ATI ||
		system_specs.videoCardVendorID == VENDOR_NV ||
		system_specs.videoCardVendorID == VENDOR_WINE)
	{
		return true;
	}
	return false;
}

bool gfxSettingsIsSupportedHardware(RdrDevice *device)
{
	eCardClass cardClass;
	if (!gfxSettingsIsSupportedHardwareEarly())
		return true; // Only return false once
	systemSpecsInit();
	cardClass = getDeviceCardClass(device);
	if (cardClass == CC_NV3_OR_ATI8500 ||
		cardClass == CC_NV2 ||
		cardClass == CC_INTELEXTREME)
	{
		return false;
	}
	if (system_specs.videoCardVendorID == VENDOR_S3G)
	{
		if (rdrSupportsFeature(device, FEATURE_SM30))
			return true;
		return false;
	}
	else if (system_specs.videoCardVendorID == VENDOR_INTEL)
	{
		if (rdrSupportsFeature(device, FEATURE_SM30))
			return true;
		return false;
	}
	else if (system_specs.videoCardVendorID == VENDOR_NV)
	{
		if (rdrSupportsFeature(device, FEATURE_SM30))
			return true;
		return false;
	} else if (system_specs.videoCardVendorID == VENDOR_ATI)
	{
		if (systemSpecsMaterialSupportedFeatures() & (SGFEAT_SM30_PLUS | SGFEAT_SM30 | SGFEAT_SM20_PLUS))
			return true;
		else
			return false;
	} else if (system_specs.videoCardVendorID == VENDOR_WINE)
	{
		if (rdrSupportsFeature(device, FEATURE_SM30))
			return true;
		return false;
	} else {
		return false;
	}
}

#if _XBOX
void xboxFixSettings(GfxSettings *settings)
{
	XVIDEO_MODE VideoMode;
	XGetVideoMode(&VideoMode);
	settings->screenWidth = MIN(VideoMode.dwDisplayWidth, 1280);
	settings->screenHeight = MIN(VideoMode.dwDisplayHeight, 720);

	// Force 1280x720 rendering resolution
	if (settings->renderScale[1] != 1) {  // -renderScale 1 should disable it
		Vec2 desired;
		if (settings->screenHeight < 720) {
			setVec2(desired,
				1280.f/ settings->screenWidth,
				720.f / settings->screenHeight);
		} else {
			setVec2same(desired, 0);
		}

		if (!sameVec2(desired, settings->renderScale))
		{
			// Xbox resolution changed, default to current widescreenness
			settings->aspectRatio = VideoMode.fIsWideScreen?(16.f/9.f):(4.f/3.f);
			copyVec2(desired, settings->renderScale);
		}
	}
}
#endif

static void gfxCopyBasicSettings(GfxSettings *dst, GfxSettings *src)
{
	dst->fullscreenWidth = src->fullscreenWidth;
	dst->fullscreenHeight = src->fullscreenHeight;
	dst->windowedWidth = src->windowedWidth;
	dst->windowedHeight = src->windowedHeight;

	dst->hasSelectedFullscreenMode = src->hasSelectedFullscreenMode;
	dst->refreshRate = src->refreshRate;
	dst->screenXPos = src->screenXPos;
	dst->screenYPos = src->screenYPos;
	dst->maximized = src->maximized;
	dst->fullscreen = src->fullscreen;
	dst->monitor_index_plus_one = src->monitor_index_plus_one;
	dst->device_type = src->device_type;
	dst->gamma = src->gamma;
	dst->aspectRatio = src->aspectRatio;
	dst->useVSync = src->useVSync;
	dst->renderScale[0] = src->renderScale[0];
	dst->renderScale[1] = src->renderScale[1];
	dst->renderSize[0] = src->renderSize[0];
	dst->renderSize[1] = src->renderSize[1];
	dst->renderScaleSetBySize[0] = src->renderScaleSetBySize[0];
	dst->renderScaleSetBySize[1] = src->renderScaleSetBySize[1];
	dst->renderScaleFilter = src->renderScaleFilter;
	dst->defaultFOV = src->defaultFOV;
}

void gfxGetScaledSettings(RdrDevice *device, GfxSettings * gfxSettings, F32 scale);

void gfxGetRecommendedSettings(RdrDevice *device, GfxSettings * gfxSettings)
{
	gfxGetScaledSettings(device, gfxSettings, -1);
}

void gfxGetScaledSettings(RdrDevice *device, GfxSettings * gfxSettings, F32 scale)
{
	eCardClass cardClass = getCardClass(gfxSettings);

	systemSpecsInit();

	// caller must arrange the logical (i.e. potentially-emulated) perf test results beforehand
	assert(gfxSettings->logical_perf_values.bFilledIn);

	gfxSettings->features_desired = gfx_state.features_allowed;

	gfxSettings->frameRateStabilizer	= 0;
	gfxSettings->autoEnableFrameRateStabilizer=0;

	if(!gfxIsHeadshotServer())
	{
		if (gfxUseDevelopmentModeSettings())
			gfxSettings->maxInactiveFps = 5;
		else
			gfxSettings->maxInactiveFps = 30;
	}

	gfxSettings->renderThread = getNumRealCpus()>1;
	gfxSettings->preload_materials=1;
	gfxSettings->softwareCursor = 0;
	gfxSettings->maxReflection=SGRAPH_REFLECT_CUBEMAP;
	gfxSettings->maxDebris = (getNumRealCpus()>1)?100:10;
	gfxSettings->higherSettingsInTailor = gConf.bTailorDefaultLowQuality?0:1;
	gfxSettings->perFrameSleep			= 0;
	gfxSettings->maxFps				= 60;

	// Default settings which will be set by perftimes
	// XBOX uses these (no perftimes)
	gfxSettings->antialiasingMode			= GAA_NONE;
	gfxSettings->antialiasing	= 1;
	gfxSettings->lighting_quality		= GLIGHTING_QUALITY_HIGH;
	gfxSettings->disableSplatShadows	= 0;
	gfxSettings->features_desired		&= ~GFEATURE_SHADOWS; // Re-enabled based on perf times
	gfxSettings->worldDetailLevel		= 1.0;
	gfxSettings->entityDetailLevel		= 1.0;
	gfxSettings->terrainDetailLevel	= 1.0;
	gfxSettings->worldTexLODLevel		= 1.0;
	gfxSettings->worldLoadScale		= 1.0;
	gfxSettings->reduceFileStreaming	= system_specs.physicalMemoryMax >= 2000*1024*1024;
	gfxSettings->highQualityDOF		= 0;
	gfxSettings->useFullSkinning		= 1;
	gfxSettings->gpuAcceleratedParticles = 1;
	gfxSettings->poissonShadows		= 0;
	gfxSettings->bloomIntensity		= 1.0;
	gfxSettings->ssao					= 0;
	gfxSettings->fxQuality				= 2;
	gfxSettings->lensflare_quality		= 2;
	gfxSettings->bloomQuality			= GBLOOM_MED_BLOOM_SMALLHDR;

	// Settings based on measured performance
	if (scale == -1)
	{
		gfxGetRecommendedAdvancedSettingsFromPerftimes(device, gfxSettings, cardClass);
	}
	else
	{
		gfxGetRecommendedAdvancedSettingsForFill(device, gfxSettings, cardClass, scale * gfxGetFillMax() + 1);
	}
	
	gfxSettings->gamma							= 1.0;
	gfxSettings->refreshRate					= 60;
	gfxSettings->screenXPos						= 0;
	gfxSettings->screenYPos						= 0;
	gfxSettings->maximized						= gfxUseDevelopmentModeSettings()?0:1; 
	gfxSettings->fullscreen						= 0;

	gfxSettings->videoMemoryMax		= 0; // Auto

	/////////////////////////////////////////////////////////////////////////
	if( cardClass == CC_INTELEXTREME || cardClass == CC_NV2 || cardClass == CC_NV3_OR_ATI8500)
	{
		gfxSettings->worldTexLODLevel	= TEX_LOD_LEVEL_FOR_REDUCE_MIP_1;
		gfxSettings->hwInstancing       = true;

		if(!default_settings_rules.use_scaled_fill_rate && !gfxUseScaledFillRateForEverything) {
			gfxSettings->texAniso           = 1;
			gfxSettings->soft_particles     = 0;
			gfxSettings->maxLightsPerObject = gfx_lighting_options.max_static_lights_per_object; // minimum allowed value
			gfxSettings->maxShadowedLights  = 1; // minimum allowed value
			gfxSettings->dynamicLights      = 0;
			gfxSettings->draw_high_detail   = 0;
			gfxSettings->draw_high_fill_detail = 0;
			gfxSettings->bloomQuality       = GBLOOM_OFF;
		}

	}
	else if( cardClass == CC_NV4_OR_ATI9500 || cardClass == CC_UNKNOWN || cardClass == CC_UNKNOWN_SM2PLUS )
	{
		// GF7 and lower
		gfxSettings->hwInstancing       = true;

		if(!default_settings_rules.use_scaled_fill_rate && !gfxUseScaledFillRateForEverything) {
			gfxSettings->texAniso           = 4;
			gfxSettings->soft_particles     = 0;
			gfxSettings->maxLightsPerObject = gfx_lighting_options.max_static_lights_per_object; // minimum allowed value
			gfxSettings->maxShadowedLights  = gfx_lighting_options.max_shadowed_lights_per_frame; // maximum allowed value

			// FIXME: 10000 for the fill performance cutoff is picked just because it's the
			// estimated value for something that would end up in the CC_NV8 bucket. This
			// value may need adjusting.
			if(getFillValue(gfxSettings) > 10000) {
				gfxSettings->soft_particles   = 1;
				gfxSettings->dynamicLights    = 1;
				gfxSettings->draw_high_detail = 1;
			} else {
				gfxSettings->soft_particles   = 0;
				gfxSettings->dynamicLights    = 0;
				gfxSettings->draw_high_detail = 0;
			}

			gfxSettings->draw_high_fill_detail = 0;
		}
	}
	else if( cardClass == CC_NV8 || cardClass == CC_XBOX )
	{
		// GF8 and higher
		gfxSettings->hwInstancing       = true;

		if(!default_settings_rules.use_scaled_fill_rate && !gfxUseScaledFillRateForEverything) {
			gfxSettings->texAniso           = 16;
			gfxSettings->soft_particles     = 1;
			gfxSettings->maxLightsPerObject = rdrMaxSupportedObjectLights(); // maximum allowed value
			gfxSettings->maxShadowedLights  = gfx_lighting_options.max_shadowed_lights_per_frame; // maximum allowed value
			gfxSettings->dynamicLights      = 1;
			gfxSettings->draw_high_detail   = 1;
			gfxSettings->draw_high_fill_detail = 0;
			gfxSettings->poissonShadows     = cardClass != CC_XBOX ? 1 : 0;
		}
	}

    if(!gfxSettings->hasSelectedFullscreenMode) {
		gfxSettingsSetResolutionByFill(gfxSettings);
		gfxSettingsSetRenderScaleByResolutionAndFill(gfxSettings);
	}

	gfxSettings->showAdvanced = 0;
	if (gConf.bUseLegacyGraphicsQualitySlider)
	{
		gfxSettings->slowUglyScale = 0.667;
	}
	else
	{
		if (scale == -1)
		{
			gfxSettings->slowUglyScale = CLAMP(getEffectiveFillValue(gfxSettings, true) / (F32)gfxGetFillMax(), 0, 1);
		}
		else
		{
			gfxSettings->slowUglyScale = scale;
		}
	}

	// Do not recommend outlining if we're under 800x600 actual pixels
	{
		int effw, effh;
		gfxSettingsGetScreenSize(gfxSettings, &effw, &effh);

		if (gfxSettings->renderScale[0])
			effw *= gfxSettings->renderScale[0];
		if (gfxSettings->renderScale[1])
			effh *= gfxSettings->renderScale[1];
		if (effw * effh < 800 * 600)
			gfxSettings->features_desired &= ~GFEATURE_OUTLINING;
	}


    if (cardClass == CC_XBOX) {
		gfxSettings->worldTexLODLevel = TEX_LOD_LEVEL_FOR_REDUCE_MIP_1;
	} else {
		if ( system_specs.videoMemory) {
			if (system_specs.videoMemory <= 256 ) {
				// Low
				gfxSettings->worldTexLODLevel = TEX_LOD_LEVEL_FOR_REDUCE_MIP_2;
			} else if (system_specs.videoMemory <= 512) {
				// Medium
				gfxSettings->worldTexLODLevel = TEX_LOD_LEVEL_FOR_REDUCE_MIP_1;
			} else {
				// High
				gfxSettings->worldTexLODLevel = 1.0f;
			}
		}
	}

	gfxSettings->entityTexLODLevel = gfxSettings->worldTexLODLevel;

	gfxSettings->minDepthBiasFix = 0;
}

static void gfxGetMaxAdvancedSettings(RdrDevice *device, GfxSettings * gfxSettings)
{
	gfxSettings->hwInstancing           = true;
	gfxSettings->worldDetailLevel		= 1.0f;
	gfxSettings->entityDetailLevel		= 2.0f;
	gfxSettings->terrainDetailLevel	= 1.0;
	gfxSettings->videoMemoryMax		= 8;
	gfxSettings->worldTexLODLevel		= 2.0f;
	gfxSettings->entityTexLODLevel		= 2.0f;
	gfxSettings->worldLoadScale		= 1.0;
	gfxSettings->reduceFileStreaming	= system_specs.physicalMemoryMax >= 2000*1024*1024; // just under 2 GB to account for errors in the reporting
	gfxSettings->soft_particles		= 1;
	if (rdrSupportsFeature(device, FEATURE_DEPTH_TEXTURE_MSAA)) {
		gfxSettings->antialiasingMode = GAA_MSAA;
		gfxSettings->antialiasing = 4;
	} else {
		gfxSettings->antialiasing = 1;
	}
	gfxSettings->softwareCursor		= 0;
	gfxSettings->texAniso				= 16;
	gfxSettings->features_desired		= gfx_state.features_allowed;
	gfxSettings->maxLightsPerObject	= rdrMaxSupportedObjectLights(); // maximum allowed value
	gfxSettings->maxShadowedLights		= gfx_lighting_options.max_shadowed_lights_per_frame; // maximum allowed value
	gfxSettings->disableSplatShadows	= 0;
	gfxSettings->dynamicLights			= 1;
	gfxSettings->draw_high_detail		= 1;
	gfxSettings->draw_high_fill_detail = 0;
	gfxSettings->preload_materials		= 1;
	gfxSettings->frameRateStabilizer	= 0;
	gfxSettings->autoEnableFrameRateStabilizer=0;
	if (gfxUseDevelopmentModeSettings())
		gfxSettings->maxInactiveFps = 5;
	else
		gfxSettings->maxInactiveFps = 30;
	gfxSettings->renderThread			= getNumRealCpus()>1;
	gfxSettings->bloomQuality			= GBLOOM_MED_BLOOM_SMALLHDR;
	gfxSettings->bloomIntensity		= 1.f;
	gfxSettings->scattering			= 1;
	gfxSettings->ssao					= 1;
	gfxSettings->poissonShadows		= 1;
	gfxSettings->lighting_quality		= GLIGHTING_QUALITY_HIGH;
	gfxSettings->maxReflection			= SGRAPH_REFLECT_CUBEMAP;
	gfxSettings->highQualityDOF		= 0;
	gfxSettings->useFullSkinning		= 1;
	gfxSettings->gpuAcceleratedParticles = 1;
	gfxSettings->maxDebris				= 200;
	gfxSettings->fxQuality				= 2;
	gfxSettings->minDepthBiasFix		= 0;
	gfxSettings->higherSettingsInTailor= 1;
	gfxSettings->perFrameSleep			= 0;
	gfxSettings->maxFps				= 0;
}


static void gfxGetMinAdvancedSettings(RdrDevice *device, GfxSettings * gfxSettings)
{
	gfxSettings->hwInstancing           = true;
	gfxSettings->worldDetailLevel		= 0.25f;
	gfxSettings->entityDetailLevel		= 0.25f;
	gfxSettings->terrainDetailLevel	= 0.8;
	gfxSettings->videoMemoryMax		= 1;
	gfxSettings->worldTexLODLevel		= TEX_LOD_LEVEL_FOR_REDUCE_MIP_2;
	gfxSettings->entityTexLODLevel		= TEX_LOD_LEVEL_FOR_REDUCE_MIP_2;
	gfxSettings->worldLoadScale		= 1.0;
	gfxSettings->reduceFileStreaming	= 0;
	gfxSettings->soft_particles		= 0;
	gfxSettings->antialiasingMode			= GAA_NONE;
	gfxSettings->antialiasing	= 1;
	gfxSettings->softwareCursor		= 0;
	gfxSettings->texAniso				= 1;
	gfxSettings->features_desired		= 0;
	gfxSettings->maxLightsPerObject	= gfx_lighting_options.max_static_lights_per_object; // minimum allowed value
	gfxSettings->maxShadowedLights		= 1; // minimum allowed value
	gfxSettings->disableSplatShadows	= 1;
	gfxSettings->dynamicLights			= 0;
	gfxSettings->draw_high_detail		= 0;
	gfxSettings->draw_high_fill_detail = 0;
	gfxSettings->preload_materials		= 1;
	gfxSettings->frameRateStabilizer	= 0;
	gfxSettings->autoEnableFrameRateStabilizer=0;
	if (gfxUseDevelopmentModeSettings())
		gfxSettings->maxInactiveFps = 5;
	else
		gfxSettings->maxInactiveFps = 30;
	gfxSettings->renderThread			= getNumRealCpus()>1;
	gfxSettings->bloomQuality			= GBLOOM_OFF;
	gfxSettings->bloomIntensity		= 1.f;
	gfxSettings->scattering			= 0;
	gfxSettings->ssao					= 0;
	gfxSettings->lighting_quality		= GLIGHTING_QUALITY_VERTEX_ONLY_LIGHTING;
	gfxSettings->maxReflection			= SGRAPH_REFLECT_NONE;
	gfxSettings->highQualityDOF		= 0;
	gfxSettings->useFullSkinning		= 0;
	gfxSettings->gpuAcceleratedParticles = 0;
	gfxSettings->maxDebris				= 0;
	gfxSettings->fxQuality				= 0;
	gfxSettings->minDepthBiasFix		= 0;
	gfxSettings->higherSettingsInTailor= 0;
	gfxSettings->perFrameSleep			= 0;
	gfxSettings->maxFps				= 60;
}



static void gfxGetRecommendedLowAdvancedSettings(RdrDevice *device, GfxSettings * gfxSettings)
{
	GfxSettings recommended = *gfxSettings;
	gfxGetRecommendedSettings(device, &recommended);
	gfxGetMinAdvancedSettings(device, gfxSettings);
	// Just those different from Minimum:
	gfxSettings->worldDetailLevel		= 0.5;
	gfxSettings->entityDetailLevel		= 0.5f;
	gfxSettings->terrainDetailLevel	= 1;
	gfxSettings->worldLoadScale		= 1.0;
	gfxSettings->reduceFileStreaming	= system_specs.physicalMemoryMax >= 2000*1024*1024; // just under 2 GB to account for errors in the reporting
	gfxSettings->videoMemoryMax		= recommended.videoMemoryMax;
	gfxSettings->worldTexLODLevel		= TEX_LOD_LEVEL_FOR_REDUCE_MIP_1;
	gfxSettings->entityTexLODLevel		= TEX_LOD_LEVEL_FOR_REDUCE_MIP_1;
	gfxSettings->disableSplatShadows	= 0;
	gfxSettings->maxReflection			= SGRAPH_REFLECT_SIMPLE;
	gfxSettings->useFullSkinning		= recommended.useFullSkinning;
	gfxSettings->gpuAcceleratedParticles = recommended.gpuAcceleratedParticles;
	gfxSettings->maxDebris				= 10;
	gfxSettings->fxQuality				= 1;
	gfxSettings->higherSettingsInTailor= 1;
	gfxSettings->lensflare_quality		= 0;
}


void gfxGetScaledAdvancedSettings(RdrDevice *device, GfxSettings *gfxSettings, F32 scale)
{
	GfxSettings saved = *gfxSettings;

	if (gConf.bUseLegacyGraphicsQualitySlider)
	{
		if (scale < 0.20) {
			// 0.0
			gfxGetMinAdvancedSettings(device, gfxSettings);
			gfxSettings->entityDetailLevel = 0.50f;
		} else if (scale < 0.5) { // 0.333
			gfxGetRecommendedLowAdvancedSettings(device, gfxSettings);
		} else if (scale < 0.75) { // 0.666
			gfxGetRecommendedSettings(device, gfxSettings);
		} else {
			gfxGetMaxAdvancedSettings(device, gfxSettings);
		}
	}
	else
	{
		gfxGetScaledSettings(device, gfxSettings, scale);
	}

	// Restore non-advanced settings (resolution, etc)
	gfxCopyBasicSettings(gfxSettings, &saved);
	
	gfxEnforceSettingsConstraints(device, gfxSettings, ACCESS_USER); // All settings should be set to user-allowed settings here anyway?
}

void gfxGetSupportedFeatures(RdrDevice *device, GfxSettings *settings)
{
	settings->features_supported = 0;

	if (rdrSupportsFeature(device, FEATURE_SM30) && rdrSupportsFeature(device, FEATURE_DEPTH_TEXTURE) && 
		rdrSupportsSurfaceType(device, SBT_RG_FIXED) && rdrSupportsSurfaceType(device, SBT_RGB16)) // surface types used by shadowmaps and shadow lookup textures
	{
		settings->features_supported |= GFEATURE_SHADOWS;
		settings->features_supported |= GFEATURE_SCATTERING;
	}

	if (rdrSupportsFeature(device, FEATURE_DEPTH_TEXTURE))
	{
		settings->features_supported |= GFEATURE_OUTLINING;
	}

	if (rdrSupportsFeature(device, FEATURE_SM30) && rdrSupportsFeature(device, FEATURE_DEPTH_TEXTURE) && rdrSupportsFeature(device, FEATURE_SBUF_FLOAT_FORMATS))
	{
		settings->features_supported |= GFEATURE_POSTPROCESSING;
		settings->features_supported |= GFEATURE_DOF; // Tied to postprocessing currently (maybe doesn't need to be?)
	}

	if (rdrSupportsFeature(device, FEATURE_SRGB))
	{
		settings->features_supported |= GFEATURE_LINEARLIGHTING;
	}

	settings->features_supported |= GFEATURE_WATER;

	// Device-based constraints
	if (!rdrSupportsFeature(device, FEATURE_DEPTH_TEXTURE) || gfx_lighting_options.disableHighBloomQuality)
	{
		settings->maxBloomQuality = GBLOOM_MAX_BLOOM_DEFERRED;
	} else {
		settings->maxBloomQuality = GBLOOM_MED_BLOOM_SMALLHDR;
	}
}

static void gfxSettingsSaveCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	gfxSettingsSave(NULL);
}


static void gfxEnforceSettingsConstraints(RdrDevice *device, GfxSettings *gfxSettings, int accessLevel)
{
	assert(gfxSettings->maxBloomQuality >= GBLOOM_LOW_BLOOMWHITE && gfxSettings->maxBloomQuality <= GBLOOM_MAX);
	if (accessLevel < ACCESS_GM)
	{
		gfxSettings->worldDetailLevel = CLAMP(gfxSettings->worldDetailLevel, 0.25f, 2.0f);
		gfxSettings->terrainDetailLevel = CLAMP(gfxSettings->terrainDetailLevel, 0.8f, 3.f);
		gfxSettings->worldLoadScale = CLAMP(gfxSettings->worldLoadScale, 1, 10);
		gfxSettings->bloomIntensity = CLAMP(gfxSettings->bloomIntensity, 0.f, 5.f);

		if (gfxSettings->entityDetailLevel < 0.25f)
			gfxSettings->entityDetailLevel = 0.75f;
		else if (gfxSettings->entityDetailLevel > 2.0f)
			gfxSettings->entityDetailLevel = 2.0f;

		gfxSettings->worldTexLODLevel = CLAMP(gfxSettings->worldTexLODLevel, 0.25f, 2.0f);
		gfxSettings->entityTexLODLevel = CLAMP(gfxSettings->entityTexLODLevel, 0.25f, 2.0f);

#if PLATFORM_CONSOLE
		// We use renderscale > 1.0 on Xbox to compensate for different sized devices
		gfxSettings->renderScale[0] = CLAMP(gfxSettings->renderScale[0], 0, 2);
		gfxSettings->renderScale[1] = CLAMP(gfxSettings->renderScale[1], 0, 2);
#else
		gfxSettings->renderScale[0] = CLAMP(gfxSettings->renderScale[0], 0, 1);
		gfxSettings->renderScale[1] = CLAMP(gfxSettings->renderScale[1], 0, 1);
#endif
		gfxSettings->bloomQuality = CLAMP(gfxSettings->bloomQuality, 0, gfxSettings->maxBloomQuality);

		gfxSettings->maxLightsPerObject = CLAMP(gfxSettings->maxLightsPerObject, gfx_lighting_options.max_static_lights_per_object, rdrMaxSupportedObjectLights());
	}
	else
	{
		MAX1(gfxSettings->renderScale[0], 0);
		MAX1(gfxSettings->renderScale[1], 0);
		gfxSettings->bloomQuality = CLAMP(gfxSettings->bloomQuality, 0, GBLOOM_MAX);
		if(gfxSettings->bloomQuality > GBLOOM_MAX) {
			gfxSettings->bloomQuality = CLAMP(gfxSettings->bloomQuality, 0, gfxSettings->maxBloomQuality);
		}

		if (!rdrSupportsFeature(device, FEATURE_DEPTH_TEXTURE))
			gfxSettings->bloomQuality = CLAMP(gfxSettings->bloomQuality, 0, GBLOOM_LOW_BLOOMWHITE);
		gfxSettings->maxLightsPerObject = CLAMP(gfxSettings->maxLightsPerObject, 0, rdrMaxSupportedObjectLights());
	}

	if (!rdrSupportsFeature(device, FEATURE_DEPTH_TEXTURE))
	{
		gfxSettings->soft_particles = false;
	}

    gfxSettings->antialiasing = CLAMP(gfxSettings->antialiasing, 1, rdrSupportsSurfaceType(device, SBT_RGBA));
	gfxSettings->antialiasing = pow2(gfxSettings->antialiasing);

	if (gfxStereoscopicActive())
	{
		gfxSettings->antialiasingMode = GAA_NONE;
		gfxSettings->antialiasingEnabled = 0;
	}
	else
	{
		gfxSettings->antialiasingEnabled = 1;
	}

	if (gfxSettings->antialiasingMode == GAA_NONE) {
		gfxSettings->antialiasing = 1;
	} else if (gfxSettings->antialiasingMode == GAA_TXAA) {
		MIN1(gfxSettings->antialiasing, 4);
		if (!rdrSupportsFeature(device, FEATURE_TXAA)) {
			gfxSettings->antialiasingMode = GAA_MSAA;
		}
	}

	gfxSettings->maxShadowedLights = CLAMP(gfxSettings->maxShadowedLights, 1, gfx_lighting_options.max_shadowed_lights_per_frame);
	gfxSettings->scattering = CLAMP(gfxSettings->scattering, 0, 4);
	if (!rdrSupportsFeature(device, FEATURE_ANISOTROPY))
		gfxSettings->texAniso = 1;
	else
		gfxSettings->texAniso = CLAMP(gfxSettings->texAniso, 1, 16);

	gfxSettings->aspectRatio = CLAMP(gfxSettings->aspectRatio, 0, 4);
	gfxSettings->renderThread = CLAMP(gfxSettings->renderThread, 0, 1);

	if (rdrSupportsFeature(device, FEATURE_SM30))
	{
		MIN1(gfxSettings->maxReflection, SGRAPH_REFLECT_CUBEMAP);
	}
	else
	{
		MIN1(gfxSettings->maxReflection, SGRAPH_REFLECT_SIMPLE);
	}

	gfxSettings->features_desired &= gfxSettings->features_supported;
	if (!gfxSupportsVfetch() || getIsTransgaming())
		gfxSettings->gpuAcceleratedParticles = 0;
	if (!(gfxSettings->features_supported & GFEATURE_POSTPROCESSING))
	{
		gfxSettings->ssao = 0;
		gfxSettings->bloomQuality = GBLOOM_OFF;
		gfxSettings->soft_particles = 0;
	}
	if (!(systemSpecsMaterialSupportedFeatures() & SGFEAT_SM30_PLUS))
	{
		gfxSettings->ssao = 0;
	}

	switch (gfxSettings->lighting_quality)
	{
		case GLIGHTING_QUALITY_VERTEX_ONLY_LIGHTING:
		case GLIGHTING_QUALITY_HIGH:
			// Valid
			break;
		default:
			gfxSettings->lighting_quality = GLIGHTING_QUALITY_HIGH;
	}

	if(system_specs.material_hardware_supported_features & SGFEAT_SM30_PLUS) {
		// Shadow buffers default to on if they're supported.
		gfx_state.shadow_buffer = true;
	}

	{
		//these require post processing if you have antialiasing
		bool allowEffectsThatUseDepth = gfxSettingsAllowsDepthEffects(gfxSettings);

		if (!allowEffectsThatUseDepth)
		{
			gfxSettings->ssao = 0;
			gfxSettings->soft_particles = 0;			
			gfxSettings->features_desired &= ~(GFEATURE_OUTLINING | GFEATURE_DOF | GFEATURE_WATER);

			// Shadows could still be supported, but shadow buffers aren't.
			gfx_state.shadow_buffer = false;

			//only bloom level 1 is supported
			gfxSettings->maxBloomQuality = CLAMP(gfxSettings->maxBloomQuality, 0, 1);
			gfxSettings->bloomQuality = CLAMP(gfxSettings->bloomQuality, 0, gfxSettings->maxBloomQuality);
		}
	}

	if (gfxSettings->frameRateStabilizer < 0)
		gfxSettings->frameRateStabilizer = 0;
	if (gfxSettings->maxFps < 0.0f)
		gfxSettings->maxFps = 0.0f;

	if (getNumRealCpus() == 1)
		gfxSettings->renderThread = 0;

#if PLATFORM_CONSOLE
	gfxSettings->softwareCursor = true;
#endif
}

void gfxSettingsSaveSoon(void)
{
	globalGfxSettingsForNextTime = gfx_state.settings;
	TimedCallback_Run(gfxSettingsSaveCallback, NULL, 10.f);
}

void gfxSetCurrentSettingsForNextTime(void)
{
	globalGfxSettingsForNextTime = gfx_state.settings;
}

void gfxSetUIUpdateCB(void(*settingsUIUpdateCallback)(void))
{
	gfx_state.gfxUIUpdateCallback = settingsUIUpdateCallback;
}

// pastStartup is only needed when called after the initial set up.
// onlyDynamic specifies to only change things that can quickly be changed on the fly
void gfxApplySettings(RdrDevice *device, GfxSettings *gfxSettings, ApplySettingsFlags flags, int accessLevel)
{
	int mipdiff, entmipdiff, anisodiff;
	int fullscreendiff, threadeddiff;
	bool screenSizeDiff, refreshDiff, maximizedDiff, adapterDiff;
	U32 oldRenderSize[2];
	U32 newRenderSize[2];

	if (flags & ApplySettings_OnlyDynamic)
		gfx_state.applied_dynamic_settings_timestamp = gfx_state.client_frame_timestamp;

	mipdiff = mipReduceFromLODLevel(gfx_state.settings.worldTexLODLevel) - mipReduceFromLODLevel(gfxSettings->worldTexLODLevel);
	entmipdiff = mipReduceFromLODLevel(gfx_state.settings.entityTexLODLevel) - mipReduceFromLODLevel(gfxSettings->entityTexLODLevel);
	anisodiff = gfx_state.settings.texAniso - gfxSettings->texAniso;
	fullscreendiff = gfx_state.settings.fullscreen - gfxSettings->fullscreen;
	threadeddiff = gfx_state.settings.renderThread - gfxSettings->renderThread;

	{
		int width1, height1;
		int width2, height2;
		gfxSettingsGetScreenSize(gfxSettings, &width1, &height1);
		gfxSettingsGetScreenSize(&gfx_state.settings, &width2, &height2);
		screenSizeDiff = (width1 != width2) || (height1 != height2);
	}

	maximizedDiff = gfx_state.settings.maximized != gfxSettings->maximized;
	refreshDiff = gfx_state.settings.refreshRate != gfxSettings->refreshRate;
	adapterDiff = gfx_state.settings.monitor_index_plus_one != gfxSettings->monitor_index_plus_one;

	gfxGetSupportedFeatures(device, gfxSettings);

	gfxEnforceSettingsConstraints(device, gfxSettings, accessLevel);

	gfxGetRenderSizeFromScreenSize(oldRenderSize);
	gfx_state.settings = *gfxSettings;
	gfx_state.antialiasingQuality = gfx_state.settings.antialiasing;
	gfx_state.antialiasingMode = gfx_state.settings.antialiasingMode;
	gfxGetRenderSizeFromScreenSize(newRenderSize);

	if (oldRenderSize[0] != newRenderSize[0] || oldRenderSize[1] != newRenderSize[1] || screenSizeDiff || maximizedDiff || fullscreendiff)
	{
		gfxFreeSpecificSizedTempSurfaces(gfx_state.currentDevice, oldRenderSize, true);
	}

	if (!(flags & ApplySettings_OnlyDynamic))
	{
		globalGfxSettingsForNextTime = *gfxSettings;
	}

	switch(gfxSettings->lighting_quality)
	{
		xcase GLIGHTING_QUALITY_VERTEX_ONLY_LIGHTING:
			gfx_state.vertexOnlyLighting = 1;
		xcase GLIGHTING_QUALITY_HIGH:
			gfx_state.vertexOnlyLighting = 0;
		xdefault:
			assert(0); // Need to fill in the lighting_quality value
	}

	gfx_state.reduce_mip_world = mipReduceFromLODLevel(gfxSettings->worldTexLODLevel);
	gfx_state.reduce_mip_entity = mipReduceFromLODLevel(gfxSettings->entityTexLODLevel);

	// TODO: hook this into the settings somewhere
	gfx_state.pssm_shadowmap_size = PSSM_SHADOW_MAP_SIZE_HIGH;
	if (system_specs.videoMemory) {
		if (system_specs.videoMemory <= 256) {
            gfx_state.pssm_shadowmap_size = PSSM_SHADOW_MAP_SIZE_LOW;
		} else if (system_specs.videoMemory <= 512) {
            gfx_state.pssm_shadowmap_size = PSSM_SHADOW_MAP_SIZE_MED;
		}
		else if (system_specs.videoMemory >= 1024) {
			gfx_state.pssm_shadowmap_size = PSSM_SHADOW_MAP_SIZE_ULTRA_HIGH;
		}
	}

	gfxLockActiveDevice();
	if (!(flags & ApplySettings_OnlyDynamic))
		rdrSetVsync(device, gfx_state.settings.useVSync);
	
	//Since this sets the gamma for the entire screen I'm pretty sure you dont want this in windowed mode
	//if (gfx_state.settings.fullscreen || gfx_state.settings.maximized)
		rdrSetGamma(device, gfx_state.settings.gamma);
	//else
	//	rdrSetGamma(device, 1.0);

	gfxUnlockActiveDevice();

	gfxClearGraphicsData();

	if ((flags & ApplySettings_STARTUP_MASK) == ApplySettings_PastStartup)
	{
/*		int old_mode = game_state.game_mode;

		if (!onlyDynamic && old_mode != SHOW_LOAD_SCREEN)
		{
			game_state.game_mode = SHOW_LOAD_SCREEN;
			loadBG();
			loadScreenResetBytesLoaded();
		}
*/
		if (anisodiff)
		{
			texResetAnisotropic();
		}

		gfxMaterialMaxReflectionChanged();

/*		if (!onlyDynamic && old_mode != SHOW_LOAD_SCREEN)
		{
			finishLoadScreen();
			game_state.game_mode = old_mode;	
		}
*/
		if (!(flags & ApplySettings_OnlyDynamic)) {
			gfxSettingsSaveSoon();
		}

		gfxResetFrameRateStabilizerCounts();

		if (adapterDiff || maximizedDiff || fullscreendiff || (screenSizeDiff || refreshDiff) && gfxSettings->fullscreen) {
			DisplayParams display_settings = gfx_state.currentDevice->rdr_device->display_nonthread;
			display_settings.fullscreen = gfxSettings->fullscreen;
			if (rdr_state.disable_windowed_fullscreen)
			{
				display_settings.maximize = gfxSettings->maximized;
				display_settings.windowed_fullscreen = 0;
			}
			else
			{
				display_settings.maximize = 0;
				display_settings.windowed_fullscreen = gfxSettings->maximized;
			}
			if (gfxSettings->fullscreen)
			{
				int preferred_adapter;
				int preferred_monitor;
				const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
				MONITORINFOEX mon_info;

				preferred_adapter = gfxSettings->fullscreenRes->adapter;
				if (preferred_adapter == -1 || preferred_adapter >= eaSize(&device_infos))
					preferred_adapter = multimonGetPrimaryMonitor();
				preferred_adapter = CLAMP(preferred_adapter, 0, eaSize(&device_infos) - 1);
				preferred_monitor = device_infos[preferred_adapter]->monitor_index;

				display_settings.preferred_adapter = preferred_adapter;
				display_settings.preferred_monitor = preferred_monitor;
				display_settings.preferred_fullscreen_mode = gfxSettings->fullscreenRes->displayMode;
				
				multiMonGetMonitorInfo(preferred_monitor, &mon_info);
				display_settings.xpos = mon_info.rcMonitor.left;
				display_settings.ypos = mon_info.rcMonitor.top;
				display_settings.width = mon_info.rcMonitor.right - mon_info.rcMonitor.left;
				display_settings.height = mon_info.rcMonitor.bottom - mon_info.rcMonitor.top;
			}

			TRACE_SETTINGS("Changing display settings %s Adapt %d Mon %d Mode %d", display_settings.fullscreen ? "FS" : "Windowed",
				display_settings.preferred_adapter, display_settings.preferred_monitor, display_settings.preferred_fullscreen_mode);
			if (display_settings.fullscreen)
				TRACE_SETTINGS("(%d, %d) %d x %d", display_settings.xpos, display_settings.ypos, gfxSettings->fullscreenRes->width, gfxSettings->fullscreenRes->height);
			else
				TRACE_SETTINGS("(%d, %d) %d x %d", display_settings.xpos, display_settings.ypos, display_settings.width, display_settings.height);

			rdrSetSize(gfx_state.currentDevice->rdr_device, &display_settings);
		}
		else
		{
			TRACE_SETTINGS("Not changing settings for some reason");
		}


		if (entmipdiff || mipdiff)
			texResetReduceTimestamps();

		if (fullscreendiff || threadeddiff)
		{
			// TODO: recreate the device
			//wtChangeThreaded(gfx_state.currentDevice->rdr_device->worker_thread, gfx_state.settings.renderThread);
		}
		gfxInvalidateAllLightCaches();
	} else if ((flags & ApplySettings_STARTUP_MASK) == ApplySettings_AtStartup) {
		// only apply standard width/height setting if command line params don't require
		// a specific resolution
		if ( !windowed_width && !windowed_height && !force_fullscreen )
		{
			DisplayParams display_settings = gfx_state.currentDevice->rdr_device->display_nonthread;
			int width;
			int height;
			gfxSettingsGetScreenSize(gfxSettings, &width, &height);
			if (width != display_settings.width || height != display_settings.height ||
				gfxSettings->fullscreen != display_settings.fullscreen)
			{
				display_settings.width = width;
				display_settings.height = height;
				display_settings.fullscreen = gfxSettings->fullscreen;

				// we won't have a mode selected by the UI - so if the device's current settings don't
				// match the requested settings, issue a switch
				gfxFindDisplayModeForSettings(&display_settings, gfxSettings);
				rdrSetSize(gfx_state.currentDevice->rdr_device, &display_settings);
			}
		}
	} else if ((flags & ApplySettings_STARTUP_MASK) == ApplySettings_AtStartupHidden) {
	} else {
		assert(0);
	}

	// hwInstancing is now on for all cards that support it.  If we did some extensive testing, and found that it mattered,
	// we could change the code in rt_xmodel.c to allow us to change the "instancing count" threshold which I am adding,
	// which might conceivably be different on different hardware.  [RMARR - 4/5/13]
	rdr_state.hwInstancingMode = gfx_state.settings.hwInstancing ? 2 : 0;
	rdr_state.perFrameSleep = gfx_state.settings.perFrameSleep;
	rdr_state.frameRateStabilizer = gfx_state.settings.frameRateStabilizer;
	rdr_state.forceTwoBonedSkinning = !gfx_state.settings.useFullSkinning;
	rdr_state.forceCPUFastParticles = !gfx_state.settings.gpuAcceleratedParticles;
	rdr_state.msaaQuality = gfx_state.antialiasingQuality;
	dynFxSetMaxDebris(gfx_state.settings.maxDebris);
	dynDebugState.iFxQuality = gfx_state.settings.fxQuality;

	triviaPrintStruct("GfxSettings:", gfx_settings_parseinfo, &gfx_state.settings);
	// Update those trivia entries we want in buckets
	triviaPrintf("GfxSettings:CharacterDetailLevel", "%1.1f", gfx_state.settings.entityDetailLevel);
	triviaPrintf("GfxSettings:CharacterTexLODLevel", "%1.1f", gfx_state.settings.entityTexLODLevel);
	triviaPrintf("GfxSettings:TerrainDetailLevel", "%1.1f", gfx_state.settings.terrainDetailLevel);
	triviaPrintf("GfxSettings:WorldDetailLevel", "%1.1f", gfx_state.settings.worldDetailLevel);
	triviaPrintf("GfxSettings:WorldTexLODLevel", "%1.1f", gfx_state.settings.worldTexLODLevel);
	triviaPrintf("GfxSettings:Gamma", "%1.1f", gfx_state.settings.gamma);
	if (gfx_state.settings.last_recorded_perf < 100)
		triviaPrintf("GfxSettings:LastRecordedPerf", "%d", ((int)gfx_state.settings.last_recorded_perf)/10*10);
	else if (gfx_state.settings.last_recorded_perf < 1000)
		triviaPrintf("GfxSettings:LastRecordedPerf", "%d", ((int)gfx_state.settings.last_recorded_perf)/100*100);
	else
		triviaPrintf("GfxSettings:LastRecordedPerf", "%d", ((int)gfx_state.settings.last_recorded_perf)/1000*1000);
	triviaRemoveEntry("GfxSettings:RenderScaleY");

	triviaPrintf("GfxSettings:FullscreenHeight", "%d", gfx_state.settings.fullscreenHeight/100*100);
	triviaPrintf("GfxSettings:FullscreenWidth", "%d", gfx_state.settings.fullscreenWidth/100*100);
	triviaPrintf("GfxSettings:WindowedHeight", "%d", gfx_state.settings.windowedHeight/100*100);
	triviaPrintf("GfxSettings:WindowedWidth", "%d", gfx_state.settings.windowedWidth/100*100);

	triviaRemoveEntry("GfxSettings:ScreenXPos");
	triviaRemoveEntry("GfxSettings:ScreenYPos");

	worldCellResetCachedEntries();
}

// This is the public interface for applying saved or auto-matically generated
// graphics settings during client startup. See gfxApplySettings for the interface
// for changing settings through in-game commands or UI.
void gfxSettingsApplyInitialSettings(RdrDevice * rdr_device, bool allowShow)
{
	gfxApplySettings(rdr_device, &gfx_state.settings, (allowShow?ApplySettings_AtStartup:ApplySettings_AtStartupHidden), gfxGetAccessLevel());
}

void gfxSettingsSetAppDefault(bool maximized, int width, int height)
{
	app_default_maximized = maximized;
	app_default_width = width;
	app_default_height = height;
}

int gfxSettingsLoadPrefs(GfxSettings *pSettingsStruct)
{
	int getPrefStructResult = GamePrefGetStruct("GfxSettings", gfx_settings_parseinfo, pSettingsStruct);
	gfxSettingsUpdateDataVersion(pSettingsStruct);
	return getPrefStructResult;
}

void gfxSettingsLoadMinimal(void) // Loads just what is needed before device creation
{
#if PLATFORM_CONSOLE
	gfx_state.settings.screenWidth = 1280;
	gfx_state.settings.screenHeight = 720;
	gfx_state.settings.refreshRate = 60;
	gfx_state.settings.fullscreen = 1;
	gfx_state.settings.renderThread = 1;
#if _XBOX
	{
		XVIDEO_MODE VideoMode;
		XGetVideoMode(&VideoMode);
		gfx_state.settings.aspectRatio = VideoMode.fIsWideScreen?(16.f/9.f):(4.f/3.f);
	}
#endif
#else
	GfxSettings settingsFromCommandLine;
	GfxSettings gfxSettings_temp = {0};
	memcpy(&settingsFromCommandLine, &gfx_state.settings, sizeof(gfx_state.settings));
	gfxSettings_temp.fullscreenWidth = app_default_width;
	gfxSettings_temp.fullscreenHeight = app_default_height;
	gfxSettings_temp.windowedWidth = app_default_width;
	gfxSettings_temp.windowedHeight = app_default_height;
	gfxSettings_temp.refreshRate = 60;
	gfxSettings_temp.maximized = gfxUseDevelopmentModeSettings()?0:app_default_maximized;
	gfxSettings_temp.renderThread = getNumRealCpus()>1;
	gfxSettings_temp.monitor_index_plus_one = 0;
	gfxSettings_temp.device_type = GFXSETTINGS_DEFAULT_DEVICETYPE_AUTO;
	if (!settings_reset)
		gfxSettingsLoadPrefs(&gfxSettings_temp);
#define COPYFIELD(x) gfx_state.settings.x = gfxSettings_temp.x
	COPYFIELD(fullscreenWidth);
	COPYFIELD(fullscreenHeight);
	COPYFIELD(windowedWidth);
	COPYFIELD(windowedHeight);
	COPYFIELD(screenXPos);
	COPYFIELD(screenYPos);
	COPYFIELD(refreshRate);
	COPYFIELD(fullscreen);
	COPYFIELD(maximized);
	COPYFIELD(useVSync);
	COPYFIELD(renderThread);
	COPYFIELD(monitor_index_plus_one);
	COPYFIELD(device_type);
	COPYFIELD(defaultFOV);
#undef COPYFIELD

	if (!gfx_state.safemode)
	{
		if (!gfx_state.settings.fullscreen)
		{
			int i;
			IVec2 minv={0}, maxv={0};
			// Make sure it's on-screen
			for (i=0; i<multiMonGetNumMonitors(); i++)
			{
				MONITORINFOEX info;
				multiMonGetMonitorInfo(i, &info);
				MIN1(minv[0], info.rcMonitor.left);
				MIN1(minv[1], info.rcMonitor.top);
				MAX1(maxv[0], info.rcMonitor.right);
				MAX1(maxv[1], info.rcMonitor.bottom);
			}
			// Clamp screen pos to this
			MIN1(gfx_state.settings.fullscreenWidth, maxv[0] - minv[0]);
			MIN1(gfx_state.settings.fullscreenHeight, maxv[1] - minv[1]);
			MIN1(gfx_state.settings.windowedWidth, maxv[0] - minv[0]);
			MIN1(gfx_state.settings.windowedHeight, maxv[1] - minv[1]);
			gfx_state.settings.screenXPos = CLAMP(gfx_state.settings.screenXPos, minv[0], maxv[0] - gfx_state.settings.windowedWidth);
			gfx_state.settings.screenYPos = CLAMP(gfx_state.settings.screenYPos, minv[1], maxv[1] - gfx_state.settings.windowedHeight);
		}
		if (gfx_state.settings.renderThread && getNumRealCpus()==1)
			gfx_state.settings.renderThread = 0;
	}
#endif

	if (gfx_state.safemode)
	{
		gfx_state.settings.fullscreenWidth = 800;
		gfx_state.settings.fullscreenHeight = 600;
		gfx_state.settings.windowedWidth = 800;
		gfx_state.settings.windowedHeight = 600;

		gfx_state.settings.screenXPos = 0;
		gfx_state.settings.screenYPos = 0;
		gfx_state.settings.refreshRate = 60;
		gfx_state.settings.maximized = 0;
		gfx_state.settings.fullscreen = 0;
		gfx_state.settings.useVSync = 0;
		gfx_state.settings.renderThread = getNumRealCpus()>1;
		gfx_state.settings.maxFps = 60;
		gfx_state.settings.device_type = GFXSETTINGS_DEFAULT_DEVICETYPE_AUTO;
	}

	if (settingsFromCommandLine.device_type)
		gfx_state.settings.device_type = settingsFromCommandLine.device_type;
}

typedef enum GfxSettingsLoadStateNumbers
{
	GSLSN_AttemptLoadSavedPrefsAndCheckForReset,
	GSLSN_KickOffPerformanceMeasurement,
	GSLSN_WaitForPerformanceMeasurement,
	GSLSN_GetRecommendedSettingsAndApplyCmdLine,
	GSLSN_LoadComplete,
} GfxSettingsLoadStateNumbers;

typedef struct GfxSettingsLoadState
{
	GfxSettingsLoadStateNumbers loadStateNumber;

	GfxSettings settingsFromCommandLine;
	bool hasRunThisApp;
	bool isFirstRun;
	bool settingsChanged;
	bool resetToDefaults;
	bool resetMSAA;
	bool resetShaderQuality;
	bool redoGetRecommendedSettings;
} GfxSettingsLoadState;

static GfxSettingsLoadState s_gfx_settings_load_state;

static bool gfxSettingsLoadFirst(GfxPerDeviceState *gfx_device)
{
	RdrDevice * device = gfx_device->rdr_device;
	bool isFirstRun = true;
	bool settingsChanged = false;
	bool resetToDefaults = settings_reset;
	GfxSettings settingsFromCommandLine;
	int oldSettingsVersion;
	settingsFromCommandLine = gfx_state.settings;

#if !PLATFORM_CONSOLE
#if 0
	if (isDevelopmentMode())
	{
		char path[MAX_PATH];
		char systemSpecsCSV[2048];
		FILE *f;

		// Log perf times for a quick survey
		loadstart_printf("Querying performance...");
		gfxQueryPerfTimes(device);
		sprintf(path, "//somnus/data/users/jimb/PerfTimes4/%s.txt", getComputerName());
		f = fopen(path, "ab");
		if (f) {
			systemSpecsGetCSVString(SAFESTR(systemSpecsCSV));
			fprintf(f, "%s\n%s, Fill,%1.4f, MeasureTime,%1.3f\n", systemSpecsCSV, GetUsefulVersionString(), getFillValue(), gfx_state.currentDevice->timeSpentMeasuringPerf);
			fclose(f);
		}
		loadend_printf("done (%1.1f).", getFillValue());
	}
#endif
#endif


	{
		GfxSettings gfxSettings_temp = {0};
		int width;
		gfxSettingsLoadPrefs(&gfxSettings_temp);
		gfxSettingsGetScreenSize(&gfxSettings_temp, &width, NULL);
		if (width > 0)
			isFirstRun = false;
		if (gfxSettings_temp.videoCardVendorID && (gfxSettings_temp.videoCardDeviceID != system_specs.videoCardDeviceID ||
			gfxSettings_temp.videoCardVendorID != system_specs.videoCardVendorID))
			resetToDefaults = true;
	}

	if (!resetToDefaults && !gfxIsHeadshotServer())
	{
		settingsChanged = !gfxSettingsLoadPrefs(&gfx_state.settings);
		{
			// use saved values
			gfx_state.settings.logical_perf_values.bFilledIn = true;
			gfx_state.settings.logical_perf_values.pixelShaderFillValue = gfx_state.settings.last_recorded_perf;
			gfx_state.settings.logical_perf_values.msaaPerformanceValue = gfx_state.settings.last_recorded_msaa_perf;
			gfx_device->rdr_perf_times = gfx_state.settings.logical_perf_values;
		}
	}
	gfx_state.settings.logical_card_class = getDeviceCardClass(device);

	oldSettingsVersion = gfx_state.settings.settingsVersion;

	if (oldSettingsVersion <= 3) // reset the settings if they are from before we reorganized the settings UI
	{
		resetToDefaults = true;
	}

	if (gfx_state.settings.settingsDefaultVersion != default_settings_rules.defaults_version_number)
	{
		// Not showing advanced, and currently using defaults, give them the new defaults!
		if (!gfx_state.settings.showAdvanced &&
			((gConf.bUseLegacyGraphicsQualitySlider && gfx_state.settings.slowUglyScale>0.5 && gfx_state.settings.slowUglyScale < 0.75) ||
			(!gConf.bUseLegacyGraphicsQualitySlider && ABS(gfx_state.settings.slowUglyScale - getEffectiveFillValue(&gfx_state.settings, true)) < .05) ||
			(!gConf.bUseLegacyGraphicsQualitySlider && gfx_state.settings.settingsDefaultVersion <= 2)))
		{
			resetToDefaults = true;
		}
	}

	if(gfxForceRedoSettings) {
		resetToDefaults = true;
		gfx_state.settings.hasSelectedFullscreenMode = false;
	}

	s_gfx_settings_load_state.isFirstRun = isFirstRun;
	s_gfx_settings_load_state.settingsChanged = settingsChanged;
	s_gfx_settings_load_state.resetToDefaults = resetToDefaults;
	s_gfx_settings_load_state.settingsFromCommandLine = settingsFromCommandLine;
	s_gfx_settings_load_state.redoGetRecommendedSettings =
		settingsChanged || oldSettingsVersion != SETTINGS_VERSION || resetToDefaults ||
		!gfx_state.settings.logical_perf_values.pixelShaderFillValue || !gfx_state.settings.logical_perf_values.msaaPerformanceValue;
	s_gfx_settings_load_state.resetMSAA = oldSettingsVersion <= 4;
	s_gfx_settings_load_state.resetShaderQuality = oldSettingsVersion <= 5;

	return true;
}

static bool gfxSettingsLoadSecond(GfxPerDeviceState *gfx_device)
{
	RdrDevice * device = gfx_device->rdr_device;
	if (s_gfx_settings_load_state.redoGetRecommendedSettings)
	{
		// redo the perf test
		gfxStartPerformanceTest(gfx_device);
	}
	return true;
}

static bool gfxSettingsLoadThird(GfxPerDeviceState *gfx_device)
{
	RdrDevice * device = gfx_device->rdr_device;
	if (s_gfx_settings_load_state.redoGetRecommendedSettings)
	{
		if (!gfxGetPerformanceTestResults(&gfx_device->rdr_perf_times))
			return false;
		else
		{
			gfx_state.settings.last_recorded_perf = gfx_device->rdr_perf_times.pixelShaderFillValue;
			gfx_state.settings.last_recorded_msaa_perf = gfx_device->rdr_perf_times.msaaPerformanceValue;
			gfx_state.settings.logical_perf_values = gfx_device->rdr_perf_times;

			if(!gfxSettingsIsKnownCardClass(device)) {
				char *esCardData = NULL;
				gfxSettingsGetGPUDataCSV(device, &esCardData);
				ErrorDetailsf("%s", esCardData);
				Errorf("System has an unknown video card. Possibly unsupported.");
				estrDestroy(&esCardData);
			}

		}
	}
	return true;
}

static bool gfxSettingsLoadFourth(GfxPerDeviceState *gfx_device)
{
	RdrDevice * device = gfx_device->rdr_device;
	if (s_gfx_settings_load_state.redoGetRecommendedSettings)
	{
		gfxGetRecommendedSettings(device, &gfx_state.settings);

		// Load user's settings over the recommended (recommended only fills in new settings unless we're resetting them all)
		if (!s_gfx_settings_load_state.resetToDefaults)
			s_gfx_settings_load_state.settingsChanged = !gfxSettingsLoadPrefs(&gfx_state.settings);

		if (s_gfx_settings_load_state.resetMSAA) //this was before the MSAA fix
		{
			if (!rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_DEPTH_TEXTURE_MSAA))
			{
				gfx_state.settings.antialiasing = 1;  //if it would have never worked before, disable it
			}
			else
			{
				//otherwise lower it to 4 if it was over.
				gfx_state.settings.antialiasing = CLAMP(gfx_state.settings.antialiasing, 1, 4);
			}
		}

		if (s_gfx_settings_load_state.resetShaderQuality && gfx_state.settings.desired_Mat_quality <= SGRAPH_QUALITY_OLD_MAX)
		{
			gfx_state.settings.recommended_Mat_quality = CLAMP((gfx_state.settings.recommended_Mat_quality * SGRAPH_QUALITY_INTERVAL_SIZE) + SGRAPH_QUALITY_LOW, 0, SGRAPH_QUALITY_MAX_VALUE);
			gfx_state.settings.desired_Mat_quality = CLAMP((gfx_state.settings.desired_Mat_quality * SGRAPH_QUALITY_INTERVAL_SIZE) + SGRAPH_QUALITY_LOW, 0, SGRAPH_QUALITY_MAX_VALUE);
		}
	}

	gfxGetSupportedFeatures(device, &gfx_state.settings);
	if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING)) {
        rdrShaderSetGlobalDefine(rdrShaderGetGlobalDefineCount(), "SRGB_VERTEX_COLOR");
	}

	gfxEnforceSettingsConstraints(device, &gfx_state.settings, 0);

#if _XBOX
	xboxFixSettings(&gfx_state.settings);
#endif

	if (!s_gfx_settings_load_state.hasRunThisApp)
	{
		// Apply settings that were set on the command line
#define COPYFIELD(field) memcpy(&gfx_state.settings.field, &s_gfx_settings_load_state.settingsFromCommandLine.field, sizeof(gfx_state.settings.field));
#define IF_SET_COPYFIELD(field)										\
	if (s_gfx_settings_load_state.settingsFromCommandLine.field)	\
		COPYFIELD(field);

		if (s_gfx_settings_load_state.settingsFromCommandLine.renderScale[0] || 
			s_gfx_settings_load_state.settingsFromCommandLine.renderSize[0])
		{
			COPYFIELD(renderScale);
			COPYFIELD(renderSize);
			COPYFIELD(renderScaleFilter);
			COPYFIELD(renderScaleSetBySize);
		}
		IF_SET_COPYFIELD(worldDetailLevel);
		IF_SET_COPYFIELD(terrainDetailLevel);
		IF_SET_COPYFIELD(maxDebris);
		IF_SET_COPYFIELD(maxLightsPerObject);
		if (s_gfx_settings_load_state.settingsFromCommandLine.frameRateStabilizer != -1)
			COPYFIELD(frameRateStabilizer);

		IF_SET_COPYFIELD(softwareCursor);
		IF_SET_COPYFIELD(ssao);
		IF_SET_COPYFIELD(lighting_quality);
		IF_SET_COPYFIELD(higherSettingsInTailor);
		IF_SET_COPYFIELD(perFrameSleep);
		if (s_gfx_settings_load_state.settingsFromCommandLine.maxFps >= 0.0f)
			COPYFIELD(maxFps);
		if (s_gfx_settings_load_state.settingsFromCommandLine.device_type)
			gfx_state.settings.device_type = s_gfx_settings_load_state.settingsFromCommandLine.device_type;
#undef COPYFIELD
#undef IF_SET_COPYFIELD
	}

	if (gfx_state.safemode)
	{
		gfx_state.settings.fullscreenWidth = 800;
		gfx_state.settings.fullscreenHeight = 600;
		gfx_state.settings.windowedWidth = 800;
		gfx_state.settings.windowedHeight = 600;
		gfx_state.settings.refreshRate = 60;
		gfx_state.settings.screenXPos = 0;
		gfx_state.settings.screenYPos = 0;
		gfx_state.settings.maximized = 0;
		gfx_state.settings.fullscreen = 0;
		gfx_state.settings.showAdvanced = 0;
		gfx_state.settings.slowUglyScale = 0;
		gfx_state.settings.device_type = GFXSETTINGS_DEFAULT_DEVICETYPE_AUTO;

		// All of the rest of the graphical settings (the "advanced" options) are in GetMinSettings
		gfxGetMinAdvancedSettings(device, &gfx_state.settings);
	}

	//Make sure the gfx settings are valid (TO DO do more validating...)
	if( gfx_state.settings.worldDetailLevel <= 0.01f ||
		gfx_state.settings.terrainDetailLevel < 0.5f ||
		gfx_state.settings.windowedWidth <= 0 || gfx_state.settings.windowedHeight <= 0 || 
		gfx_state.settings.fullscreenWidth <= 0 || gfx_state.settings.fullscreenHeight <= 0 || 
		gfx_state.settings.gamma <= 0)
	{
		rdrSafeAlertMsg(TranslateMessageKeyDefault("GraphicsLib_RegistryMunged", "[UNTRANSLATED]Graphics settings corrupt"));
		gfxGetRecommendedSettings(device, &gfx_state.settings);
	}

	if (gfx_state.antialiasingQuality) {
		// Command line override
		setMSAA(gfx_state.antialiasingMode, gfx_state.antialiasingQuality);
	} else {
		setMSAA(gfx_state.settings.antialiasingMode, gfx_state.settings.antialiasing);
	}

	gfxSettingsSetCardSpecificFeatures(device);

	gfx_state.settings.features_desired &= ~gfx_state.settings.features_disabled_by_command_line;
	gfx_state.settings.features_desired |= gfx_state.settings.features_desired_by_command_line;

#if !PLATFORM_CONSOLE
	if (system_specs.videoCardVendorID == VENDOR_NV &&
		!rdrSupportsFeature(device, FEATURE_DX10_LEVEL_CARD))
	{
		// NV7 card, need to use F16 scatter textures
		// Also shrink lookup table to conserve memory since it's bigger now
		texSetVolumeGlobalParams(true, 5);
	} else if (system_specs.videoMemory < 256 && rdrSupportsFeature(device, FEATURE_NONSQUARETEXTURES))
	{
		// Less memory, use smaller lookup table
		texSetVolumeGlobalParams(true, 1);
	}
#endif

	s_gfx_settings_load_state.hasRunThisApp = true;

	return true;
}

bool gfxSettingsLoad(GfxPerDeviceState *gfx_device)
{
	if (s_gfx_settings_load_state.loadStateNumber == GSLSN_AttemptLoadSavedPrefsAndCheckForReset)
	{
		if (gfxSettingsLoadFirst(gfx_device))
			s_gfx_settings_load_state.loadStateNumber = GSLSN_KickOffPerformanceMeasurement;
	}
	else
	if (s_gfx_settings_load_state.loadStateNumber == GSLSN_KickOffPerformanceMeasurement)
	{
		if (gfxSettingsLoadSecond(gfx_device))
			s_gfx_settings_load_state.loadStateNumber = GSLSN_WaitForPerformanceMeasurement;
	}
	else
	if (s_gfx_settings_load_state.loadStateNumber == GSLSN_WaitForPerformanceMeasurement)
	{
		if (gfxSettingsLoadThird(gfx_device))
			s_gfx_settings_load_state.loadStateNumber = GSLSN_GetRecommendedSettingsAndApplyCmdLine;
	}
	else
	if (s_gfx_settings_load_state.loadStateNumber == GSLSN_GetRecommendedSettingsAndApplyCmdLine)
	{
		if (gfxSettingsLoadFourth(gfx_device))
			s_gfx_settings_load_state.loadStateNumber = GSLSN_LoadComplete;
	}
	return s_gfx_settings_load_state.loadStateNumber == GSLSN_LoadComplete;
}


void gfxSettingsSave(RdrDevice *device)
{
	GfxSettings settings;
	if (!device) {
		if (!gfx_state.currentDevice)
			return;
		device = gfx_state.currentDevice->rdr_device;
	}
	gfxGetSettingsForNextTime(device, &settings);
	settings.settingsVersion = SETTINGS_VERSION;
	settings.settingsDefaultVersion = default_settings_rules.defaults_version_number;
	GamePrefStoreStruct("GfxSettings", gfx_settings_parseinfo, &settings);
}

// Starts the game in fullscreen mode at the specified resolution, regardless of previously set options
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(fullscreen) ACMD_CMDLINE ACMD_CATEGORY(UsefulCommandLine);
void gfxForceFullscreen(int width, int height)
{
	force_fullscreen = 1;
}

AUTO_RUN;
void gfxDoTransgamingInit(void) {
	if(getIsTransgaming()) {
		force_fullscreen = 1;
	}
}

static int force_vsync, vsync_setting;

// Turns on or off vsync
AUTO_CMD_INT(gfx_state.settings.useVSync, vsync) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(UsefulCommandLine) ACMD_CALLBACK(gfxSetVsync);

// Callback for /vsync
void gfxSetVsync(CMDARGS)
{
	if (!gfx_state.currentDevice) {
		// Must be on command line
		force_vsync = 1;
		vsync_setting = gfx_state.settings.useVSync;
	} else {
		gfxLockActiveDevice();
		rdrSetVsync(gfx_state.currentDevice->rdr_device, gfx_state.settings.useVSync);
		gfxUnlockActiveDevice();
		gfxSettingsSaveSoon();
	}
}


// Changes the gamma
AUTO_CMD_FLOAT(gfx_state.settings.gamma, gamma) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(gfxSetGamma);

// Callback for /gamma
void gfxSetGamma(CMDARGS)
{
	gfxLockActiveDevice();
	//Since this sets the gamma for the entire screen I'm pretty sure you dont want this in windowed mode
	if (gfx_state.settings.fullscreen || gfx_state.settings.maximized)
		rdrSetGamma(gfx_state.currentDevice->rdr_device, gfx_state.settings.gamma);
	else
		rdrSetGamma(gfx_state.currentDevice->rdr_device, 1.0);
	gfxUnlockActiveDevice();
	gfxSettingsSaveSoon();
}

bool gfxSettingsIsVsync(void)
{
	bool ret = !!gfx_state.settings.useVSync;
	if (force_windowed)
		ret = false;
	if (force_fullscreen)
		ret = true;
	if (force_vsync)
		ret = !!vsync_setting;
	return ret;
}

bool gfxSettingsIsFullscreen(void)
{
	bool ret = !!gfx_state.settings.fullscreen;
	if (force_windowed)
		ret = false;
	if (force_fullscreen)
		ret = true;
	return ret;
}

void gfxSettingsGetSupportedResolutionsForDeviceType(GfxResolution **desktop_res, GfxResolution *** supported_resolutions, const char * device_type)
{
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
	int deviceNum, deviceMax;
	for (deviceNum = 0, deviceMax = eaSize(&device_infos); deviceNum < deviceMax; ++deviceNum)
	{
		GfxResolution **device_resolutions = NULL;
		const RdrDeviceInfo * device_info = device_infos[deviceNum];
		if (stricmp(device_info->type, device_type)==0)
		{
			device_resolutions = rdrGetSupportedResolutions(desktop_res, device_info->monitor_index, device_type);
			eaPushEArray(supported_resolutions, &device_resolutions);
		}
	}
}

void gfxSettingsGetSupportedResolutionsForDevice(GfxResolution **desktop_res, GfxResolution *** supported_resolutions, const RdrDeviceInfo * device_info)
{
	GfxResolution **device_resolutions = NULL;
	device_resolutions = rdrGetSupportedResolutions(desktop_res, device_info->monitor_index, device_info->type);
	eaPushEArray(supported_resolutions, &device_resolutions);
}

int gfxFindDisplayMode(const RdrDeviceInfo *device_info, int newWidth, int newHeight, int newRefreshRate)
{
	// match the mode
	float closest_dist = FLT_MAX;
	int mode_index, mode_count;
	int best_mode_index = -1;
	for (mode_index = 0, mode_count = eaSize(&device_info->display_modes); mode_index < mode_count; ++mode_index)
	{
		float res_dist;
		const RdrDeviceMode * mode = device_info->display_modes[mode_index];
		if (rdr_state.supportHighPrecisionDisplays >= 2 && mode->display_format != rdr_state.supportHighPrecisionDisplays - 2 )
			continue;
		if (mode->width == newWidth && mode->height == newHeight && mode->refresh_rate == newRefreshRate)
		{
			best_mode_index = mode_index;
			break;
		}
		
		res_dist = sqrtf(SQR(mode->width - newWidth) + SQR(mode->height - newHeight));
		if (res_dist < closest_dist)
		{
			closest_dist = res_dist;
			best_mode_index = mode_index;
		}
	}

	return best_mode_index;
}

static void gfxFindDisplayModeForSettings(DisplayParams *params, const GfxSettings * gfxSettings)
{
	int preferred_adapter;
	int preferred_monitor;
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
	const RdrDeviceMode * display_mode = NULL;

	// NOTE! The name of the stored setting monitor_index_plus_one is deceptive; it's actually
	// the stored Cryptic Engine device number, which used to correspond roughly to the DX9 adapter
	// number.

	// Use the stored device preference to restore the preferred monitor.
	preferred_adapter = gfxSettings->monitor_index_plus_one - 1;
	if (preferred_adapter == -1 || preferred_adapter >= eaSize(&device_infos))
		preferred_adapter = multimonGetPrimaryMonitor();
	preferred_adapter = CLAMP(preferred_adapter, 0, eaSize(&device_infos) - 1);
	preferred_monitor = device_infos[preferred_adapter]->monitor_index;

	// Use the actual device type setting and monitor pair (device type, monitor) to select a 
	// matching enumerated device.
	preferred_adapter = rdrGetDeviceForMonitor(preferred_monitor, gfx_state.settings.device_type);

	params->preferred_adapter = preferred_adapter;
	params->preferred_monitor = preferred_monitor;

	if (params->fullscreen)
	{
		int best_mode = -1;
		best_mode = gfxFindDisplayMode(device_infos[preferred_adapter], params->width, params->height, params->refreshRate);
		if (best_mode == -1)
		{
			params->fullscreen = 0;
		}
		else
		{
			display_mode = device_infos[preferred_adapter]->display_modes[best_mode];
			params->preferred_fullscreen_mode = best_mode;
			if (params->preferred_monitor >=0)
			{
				MONITORINFOEX info;

				multiMonGetMonitorInfo(preferred_monitor, &info);
				params->xpos = info.rcMonitor.left;
				params->ypos = info.rcMonitor.top;
			} else {
				params->xpos = 0;
				params->ypos = 0;
			}
		}
	}
}

void gfxSettingsFixUnavailableDeviceType()
{
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
	int deviceNum, deviceMax;
	for (deviceNum = 0, deviceMax = eaSize(&device_infos); deviceNum < deviceMax; ++deviceNum)
	{
		GfxResolution **device_resolutions = NULL;
		const RdrDeviceInfo * device_info = device_infos[deviceNum];
		if (stricmp(device_info->type, gfx_state.settings.device_type)==0)
		{
			return;
		}
	}
	gfx_state.settings.device_type = GFXSETTINGS_DEFAULT_DEVICETYPE_AUTO;
}

void gfxGetWindowSettings(WindowCreateParams *create_params)
{
	GfxResolution *desktop_resolution, *fullscreen_res = NULL;
	int preferred_adapter;
	int preferred_monitor;
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
	const RdrDeviceMode * display_mode = NULL;
	DisplayParams * params = &create_params->display;

	gfxSettingsLoadMinimal();
	gfxSettingsFixUnavailableDeviceType();

	// Use the stored device preference to restore the preferred monitor.
	preferred_adapter = gfx_state.settings.monitor_index_plus_one - 1;
	if (preferred_adapter == -1 || preferred_adapter >= eaSize(&device_infos))
	{
		// use whatever device info matches the selected device type and the primary monitor
		preferred_monitor = multimonGetPrimaryMonitor();
	}
	else
	{
		preferred_adapter = CLAMP(preferred_adapter, 0, eaSize(&device_infos) - 1);
		preferred_monitor = device_infos[preferred_adapter]->monitor_index;
	}

	// Use the actual device type setting and monitor pair (device type, monitor) to select a 
	// matching enumerated device.
	preferred_adapter = rdrGetDeviceForMonitor(preferred_monitor, gfx_state.settings.device_type);

	rdrGetSupportedResolutions(&desktop_resolution, preferred_monitor, gfx_state.settings.device_type);

	ZeroStruct(params);

	params->preferred_monitor = preferred_monitor;
	params->preferred_adapter = preferred_adapter;

#if _XBOX
#else
	if (!rdrIsDeviceTypeAuto(gfx_state.settings.device_type))
		create_params->device_type = gfx_state.settings.device_type;

	gfxSettingsGetScreenSize(&gfx_state.settings, &params->width, &params->height);

	params->xpos = gfx_state.settings.screenXPos;
	params->ypos = gfx_state.settings.screenYPos;
	params->refreshRate = gfx_state.settings.refreshRate;
	params->fullscreen = gfxSettingsIsFullscreen();
	if (rdr_state.disable_windowed_fullscreen)
	{
		params->maximize = gfx_state.settings.maximized;
		params->windowed_fullscreen = 0;
	} else {
		params->maximize = 0;
		params->windowed_fullscreen = gfx_state.settings.maximized;
	}
	params->vsync = gfxSettingsIsVsync();
	params->srgbBackBuffer = (gfx_state.features_allowed & GFEATURE_LINEARLIGHTING) != 0;

	if (force_windowed) {
		if (windowed_width)
		{
			params->width = windowed_width;
			// requesting a specific dimension forces no Maximize
			params->maximize = 0;
			params->windowed_fullscreen = 0;
		}
		if (windowed_height)
		{
			params->height = windowed_height;
			// requesting a specific dimension forces no Maximize
			params->maximize = 0;
			params->windowed_fullscreen = 0;
		}
	}

	if (params->fullscreen)
	{
		int best_mode = -1;
		best_mode = gfxFindDisplayMode(device_infos[preferred_adapter], params->width, params->height, params->refreshRate);
		if (best_mode == -1)
		{
			params->fullscreen = 0;
		}
		else
		{
			display_mode = device_infos[preferred_adapter]->display_modes[best_mode];
			params->preferred_fullscreen_mode = best_mode;
			if (params->preferred_monitor >=0)
			{
				MONITORINFOEX info;

				multiMonGetMonitorInfo(preferred_monitor, &info);
				params->xpos = info.rcMonitor.left;
				params->ypos = info.rcMonitor.top;
			} else {
				params->xpos = 0;
				params->ypos = 0;
			}
		}
	}
	if (gfx_state.settings.fullscreen)
		TRACE_SETTINGS("Saved settings %dx%d @ %d on adapter %d, mon %d",
			gfx_state.settings.fullscreenWidth, gfx_state.settings.fullscreenHeight, gfx_state.settings.refreshRate,
			gfx_state.settings.monitor_index_plus_one - 1, params->preferred_monitor);
	else
		TRACE_SETTINGS("Saved settings Windowed on adapter %d, mon %d",
			gfx_state.settings.monitor_index_plus_one - 1, params->preferred_monitor);
	if (params->fullscreen)
		TRACE_SETTINGS("Restored settings %dx%d @ %d on adapter %d, mon %d",
			display_mode->width, display_mode->height, params->refreshRate,
			params->preferred_adapter, params->preferred_monitor);
	else
		TRACE_SETTINGS("Restored settings Windowed on adapter %d, mon %d",
			params->preferred_adapter, params->preferred_monitor);
#endif

	if (!params->width || !params->height)
	{
		params->width = desktop_resolution->width;
		params->height = desktop_resolution->height;
	}

	create_params->threaded = gfx_state.settings.renderThread;
}

static void gfxSettingsSetCardSpecificFeatures(RdrDevice* device)
{
	gfx_state.dxt_non_pow2 = !!rdrSupportsFeature(device, FEATURE_NONPOW2TEXTURES) && !disableNP2;
	// TODO: disable/enable specific features here

/* 	if (debugRuntime) */
/* 	{ */
/* 		gfx_state.settings.features_desired &= ~(GFEATURE_OUTLINING | GFEATURE_SHADOWS); */
/* 	} */
}

// Called when running on old drivers or unsupported hardware
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Options);
void gfxSettingsSetMinimalOptions(void)
{
	globCmdParse("shadows 0");
	globCmdParse("disableSplatShadows 0");
	globCmdParse("postprocessing 0");
	globCmdParse("outlining 0");
	globCmdParse("dof 0");
	globCmdParse("water 0");
}

struct {
	char *name;
	U32 eff_pixelfill;
	F32 msaa_pixelfill_scale;
	int shadermodel;
	ShaderGraphFeatures sgfeat;
	eCardClass cardclass;
	F32 texLODLevel;
	U32 videoMemoryHMBs;
	bool enableFramerateEmu;
	bool framerateEmuIsIntel;
	bool disableDepthTextures;
	bool disableStencilDepthTexture;
} emulatecard_options[] = {
	{"your", 0, 0.0f, 0x30, 0, 1, false, false, false, false},
	
	{"HighEnd", 20000, 1.0f, 0x30, SGFEAT_SM30_PLUS, CC_NV8, 1, 8, false, false, false, false},
	{"Xbox", 10000, 1.0f, 0x30, SGFEAT_SM30_PLUS, CC_XBOX, 1, 2, false, false, false, false},
	{"8800", 10000, 1.0f, 0x30, SGFEAT_SM30_PLUS, CC_NV8, 1, 5, true, false, false, false},
	{"7800", 3500, 0.25f, 0x30, SGFEAT_SM30, CC_NV4_OR_ATI9500, 1, 2, true, false, false, false},\
	{"6800", 1710, 0.0f, 0x30, SGFEAT_SM30, CC_NV4_OR_ATI9500, 1, 2, true, false, false, false},
	{"LowEndIntel", 400, 0.0f, 0x30, SGFEAT_SM20, CC_UNKNOWN_SM2PLUS, TEX_LOD_LEVEL_FOR_REDUCE_MIP_1, 2, true, true, true, false},
	{"LowEndATI", 1000, 0.0f, 0x20, SGFEAT_SM20_PLUS, CC_UNKNOWN_SM2PLUS, TEX_LOD_LEVEL_FOR_REDUCE_MIP_2, 1, true, true, false, true},
};

static int gfxCardEmulated = 0; //< keep in sync with "None"

// Sets the graphics options to match those used on a specific class of cards
AUTO_COMMAND ACMD_CATEGORY(Debug);
void gfxEmulateCard(const char *card)
{
	int i;
	for (i=0; i<ARRAY_SIZE(emulatecard_options); i++)
	{
		if (stricmp(emulatecard_options[i].name, card)==0)
		{
			GfxSettings origSettings = {0};
			GfxSettings settings = {0};
			// Set the options!
			gfx_state.debug.suppressReloadShadersMessage = true;
			switch(emulatecard_options[i].shadermodel)
			{
				xcase 0x20:
					useSM20(1);
				xcase 0x2B:
					useSM2B(1);
				xcase 0x30:
					useSM30(1);
				xdefault:
					assert(0);
			}
			system_specs.material_hardware_supported_features = (emulatecard_options[i].sgfeat << 1) - 1;
			gfx_state.debug.suppressReloadShadersMessage = false;

			rdr_state.disableDepthTextures = emulatecard_options[i].disableDepthTextures;
			rdr_state.disableStencilDepthTexture = emulatecard_options[i].disableStencilDepthTexture;

			// Set some options based on estimated performance numbers
			gfxGetSettings(&origSettings);
			gfxGetSettings(&settings); // Seed with current settings to get window positions, etc
			settings.logical_perf_values.bFilledIn = true;
			settings.logical_perf_values.pixelShaderFillValue = emulatecard_options[i].eff_pixelfill;
			settings.logical_perf_values.msaaPerformanceValue = emulatecard_options[i].msaa_pixelfill_scale;
			settings.logical_card_class = emulatecard_options[i].cardclass;
			gfxGetRecommendedSettings(gfxGetPrimaryDevice(), &settings);
			{
				GfxPerDeviceState * gfx_device = gfxGetPrimaryGfxDevice();
				RdrDevice * rdr_device = gfx_device->rdr_device;
				settings.logical_card_class = getDeviceCardClass(rdr_device);
				settings.logical_perf_values = gfx_device->rdr_perf_times;
			}

#define PRESERVE(field) settings.field = origSettings.field
			PRESERVE(fullscreenWidth);
			PRESERVE(fullscreenHeight);
			PRESERVE(windowedWidth);
			PRESERVE(windowedHeight);
			PRESERVE(fullscreen);
			PRESERVE(maximized);
#undef PRESERVE
			settings.videoMemoryMax = emulatecard_options[i].videoMemoryHMBs;
			settings.worldTexLODLevel = settings.entityTexLODLevel = emulatecard_options[i].texLODLevel;
			gfxApplySettings(gfxGetPrimaryDevice(), &settings, ApplySettings_PastStartup, gfxGetAccessLevel());
			gfx_state.debug.emulate_vs_time = emulatecard_options[i].enableFramerateEmu;
			gfx_state.debug.emulate_vs_time_use_old_intel = emulatecard_options[i].framerateEmuIsIntel;

			gfxCardEmulated = i;
			if (stricmp(emulatecard_options[i].name, "xbox")==0)
				gfxStatusPrintf( "Now displaying with Xbox 360 settings.");
			else
				gfxStatusPrintf( "Now displaying as %s card.", emulatecard_options[i].name );
			return;
		}
	}
	Errorf("Unknown card passed to gfxEmulateCard: %s", card);
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void xboxSettings(void)
{
	if (gfxCardEmulated == 0)
		gfxEmulateCard("xbox");
	else
		gfxEmulateCard("your");
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void gfxEmulateCardNext( void )
{
	gfxEmulateCard( emulatecard_options[(gfxCardEmulated + 1) % ARRAY_SIZE( emulatecard_options )].name );
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void gfxEmulateCardPrev( void )
{
	gfxEmulateCard( emulatecard_options[(gfxCardEmulated - 1 + ARRAY_SIZE( emulatecard_options )) % ARRAY_SIZE( emulatecard_options )].name );
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void gfxEmulateCardToggle( void )
{
	if( gfxCardEmulated != 0 ) {
		gfxEmulateCard( emulatecard_options[ 0 ].name );
	} else {
		gfxEmulateCard( emulatecard_options[ ARRAY_SIZE( emulatecard_options ) - 1 ].name );
	}
}

bool gfxSupportsVfetch(void)
{
	return !!(gfx_state.allRenderersFeatures & FEATURE_VFETCH);
}

void gfxSetTargetHighlight( bool target_highlight_enabled )
{
	gfx_state.target_highlight = target_highlight_enabled;
}

void gfxSettingsSetScattering(GfxScatterQuality scatter_quality)
{
	gfx_state.settings.scattering = scatter_quality;
}

void gfxSettingsSetHDRMaxLuminanceMode(bool enable)
{
	gfx_state.settings.hdr_max_luminance_mode = enable;
}


void gfxSettingsSetGamma(float fVal)
{
	gfx_state.settings.gamma = fVal;
}

float gfxSettingsGetGamma(void)
{
	return gfx_state.settings.gamma;
}

void gfxSettingsSetSpecialMaterialParam(F32 param)
{
	gfx_state.project_special_material_param = CLAMP(param, 0.f, 1.f);
}

F32 gfxSettingsGetSpecialMaterialParam(void)
{
	return gfx_state.project_special_material_param;
}

bool gfxSettingsJustAppliedDynamic(void)
{
	return gfx_state.applied_dynamic_settings_timestamp && (gfx_state.client_frame_timestamp - gfx_state.applied_dynamic_settings_timestamp) < 10*timerCpuSpeed();
}

bool gfxSettingsMSAAEnabled(const GfxSettings *gfxSettings)
{
	return gfxSettings->antialiasingMode != GAA_NONE && gfxSettings->antialiasing > 1;
}

bool gfxSettingsAllowsDepthEffects(const GfxSettings *gfxSettings)
{
	bool allowEffectsThatUseDepth = !gfxSettingsMSAAEnabled(gfxSettings) || //either no MSAA
		rdrSupportsFeature(gfxGetPrimaryDevice(), FEATURE_DEPTH_TEXTURE_MSAA); //or depth resolve support
	return allowEffectsThatUseDepth;
}

void gfxSettingsGetScreenSize(const GfxSettings *gfxSettings, int *width, int *height) {
	if(gfxSettings->fullscreen) {
		if(width) *width = gfxSettings->fullscreenWidth;
		if(height) *height = gfxSettings->fullscreenHeight;
	}
	else
	{
		if(width) *width = gfxSettings->windowedWidth;
		if(height) *height = gfxSettings->windowedHeight;
	}
}

// Generate a text value of the member named by the value of strFieldName of structure struct_mem, given
// the ParseTable tpi, and store the text in the EString strFieldTextDest.
static bool ParserConvertMemberToText(char **strFieldTextDest, ParseTable *tpi, const void *struct_mem, const char *strFieldName)
{
	bool bFoundFieldAndConverted = false;
	int fieldColumn = -1;

	estrClear(strFieldTextDest);
	if (ParserFindColumn(tpi, strFieldName, &fieldColumn))
	{
		bFoundFieldAndConverted = TokenWriteText(tpi, fieldColumn, struct_mem, strFieldTextDest, 0);
	}

	return bFoundFieldAndConverted;
}

typedef struct ReportField
{
	const char *strFieldName;
	int fieldMaskForGFeature;
	const char *strFieldBitName;
} ReportField;

void gfxReportSettings(FILE *reportFile, const int * fillRates, int numFillRates, const int resolutions[][2], int numResolutions, const ReportField * pReportField)
{
	GfxSettings estimatedDefaultSettings = { 0 };
	int resolutionIndex, rateIndex;
	char *strFieldValueText=NULL;
	estrClear(&strFieldValueText);
	for (resolutionIndex = 0; resolutionIndex < numResolutions; ++resolutionIndex)
	{
		int testWidth = resolutions[resolutionIndex][0];
		int testHeight = resolutions[resolutionIndex][1];

		estrPrintf(&strFieldValueText, "%dx%d", testWidth, testHeight);
		fprintf(reportFile, "%12s, ", strFieldValueText);

		for (rateIndex = 0; rateIndex < numFillRates; ++rateIndex)
		{
			int testFillRate = fillRates[rateIndex];
			int effectiveFill;

			estimatedDefaultSettings = gfx_state.settings;
			estimatedDefaultSettings.logical_perf_values.pixelShaderFillValue = testFillRate;
			estimatedDefaultSettings.logical_perf_values.msaaPerformanceValue = 0.95f;
			estimatedDefaultSettings.bUseSettingsResolution = 1;
			estimatedDefaultSettings.fullscreenWidth = testWidth;
			estimatedDefaultSettings.fullscreenHeight = testHeight;
			estimatedDefaultSettings.windowedWidth = testWidth;
			estimatedDefaultSettings.windowedHeight = testHeight;
			// force render scale selection
			estimatedDefaultSettings.hasSelectedFullscreenMode = false;
			effectiveFill = scaleFillValueByResolution(testFillRate, &estimatedDefaultSettings, true);

			// force render scale selection
			estimatedDefaultSettings.hasSelectedFullscreenMode = false;
			gfxGetRecommendedSettings(gfxGetPrimaryDevice(), &estimatedDefaultSettings);
			
			if (pReportField->strFieldName) 
			{
				if (pReportField->fieldMaskForGFeature)
					estrPrintf(&strFieldValueText, "%c", estimatedDefaultSettings.features_desired & pReportField->fieldMaskForGFeature ? 'Y' : 'N');
				else
					ParserConvertMemberToText(&strFieldValueText, gfx_settings_parseinfo, &estimatedDefaultSettings, pReportField->strFieldName);
			}
			else
			{
				estrPrintf(&strFieldValueText, "%.2f %dx%d %d", estimatedDefaultSettings.renderScale[0] ? estimatedDefaultSettings.renderScale[0] : 1.0f, 
					estimatedDefaultSettings.renderScale[0] ? round(estimatedDefaultSettings.renderScale[0] * testWidth) : testWidth,
					estimatedDefaultSettings.renderScale[1] ? round(estimatedDefaultSettings.renderScale[1] * testHeight) : testHeight,
					effectiveFill
					);
			}
			fprintf(reportFile, "%12s, ", strFieldValueText);
		}
		fprintf(reportFile, "\n");
	}
	estrDestroy(&strFieldValueText);
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void gfxSettingsGenerateDefaultSettingsReport(const char *strReportFilename)
{
	// iterate combinations of fills and resolutions
	const char * fillNames[] = { "Low Intel GM45", "Low ATI", "6800", "7800", "HD 3000", "HD 4000", "Low 8800", "8800 GT", "8800 GTX?", "NV 660 M", "NV GTX 560 Ti", "NV GTX 650 Ti", "Super high" };
	const ReportField settingsFieldsToReport[] =
	{
		{ NULL, 0 }, // This means resolution summary
		{ "RenderScaleX", 0 },
		{ "GraphicsQuality", 0 },
		{ "WorldTexLODLevel", 0 },
		{ "CharacterTexLODLevel", 0 },
		{ "WorldDetailLevel", 0 },
		{ "CharacterDetailLevel", 0 },
		{ "TerrainDetailLevel", 0 },
		{ "WorldLoadScale", 0 },

		{ "TexAnisotropy", 0 },
		{ "SoftParticles", 0 },
		{ "FxQuality", 0 },
		{ "Antialiasing", 0 },

		{ "DynamicLights", 0 },
		{ "MaxLightsPerObject", 0 },
		{ "GfxFeaturesDesired", GFEATURE_SHADOWS, "Shadows" },
		{ "MaxShadowedLights", 0 },
		{ "GfxFeaturesDesired", GFEATURE_LINEARLIGHTING, "Linear lighting" },

		{ "GfxFeaturesDesired", GFEATURE_POSTPROCESSING, "Postprocessing" },
		{ "GfxFeaturesDesired", GFEATURE_OUTLINING, "Outlining" },
		{ "GfxFeaturesDesired", GFEATURE_DOF, "Depth of field" },
		{ "GfxFeaturesDesired", GFEATURE_WATER, "Water" },
		{ "Scattering", 0 },
		{ "SSAO", 0 },
		{ "BloomQuality", 0 },
		{ "LightingQuality", 0 },
		{ "MaxReflection", 0 },

		{ "DrawHighDetail", 0 },
		{ "DrawHighFillDetail", 0 },

		{ "LenseFlare", 0 },
		{ "HWInstancing", 0 },

		{ "DesiredMatQuality", 0 },
		{ "RecommendedMatQuality", 0 },
	};
	const int fillRates[] = { 942, 1000, 1910, 3500, 3851, 7746, 10000, 12000, 15000, 17552, 32800, 40000, 60000 };
	const int resolutions[][2] = { { 1024, 768 }, { 1280, 800 }, { 1400, 900 }, { 1280, 1024 }, { 1600, 900 }, { 1680, 1050 }, { 1920, 1080 }, { 2560, 1024 } };
	int rateIndex, settingsIndex;
	// initializing with current settings to get device info
	FILE * reportFile = fopen(strReportFilename, "w");
	if (!reportFile)
		conPrintf("Couldn't open report file \"%s\" for writing.\n", strReportFilename);
	else
	{
		// header lines
		fprintf(reportFile, "%12s, ", "GPU");
		for (rateIndex = 0; rateIndex < ARRAY_SIZE(fillRates); ++rateIndex)
		{
			fprintf(reportFile, "%12s, ", fillNames[rateIndex]);
		}
		fprintf(reportFile, "\n");

		fprintf(reportFile, "%12s, ", "Fill Rate");
		for (rateIndex = 0; rateIndex < ARRAY_SIZE(fillRates); ++rateIndex)
		{
			fprintf(reportFile, "%12d, ", fillRates[rateIndex]);
		}
		fprintf(reportFile, "\n");

		fprintf(reportFile, "%12s, ", "Base Res");
		for (rateIndex = 0; rateIndex < ARRAY_SIZE(fillRates); ++rateIndex)
		{
			const ResolutionRule * res = gfxSettingsFindResolutionForFill(fillRates[rateIndex]);
			char strResolution[12];
			sprintf(strResolution, "%dx%d", res->w, res->h);
			fprintf(reportFile, "%12s, ", strResolution);
		}
		fprintf(reportFile, "\n");



		for (settingsIndex = 0 ; settingsIndex < ARRAY_SIZE(settingsFieldsToReport); ++settingsIndex)
		{
			const ReportField *pReportField = &settingsFieldsToReport[settingsIndex];
			const char * strSetting = pReportField->strFieldBitName ? pReportField->strFieldBitName : pReportField->strFieldName;
			fprintf(reportFile, "%s,\n", strSetting ? strSetting : "Resolution");
			gfxReportSettings(reportFile, fillRates, ARRAY_SIZE(fillRates), resolutions, ARRAY_SIZE(resolutions), pReportField);
			fprintf(reportFile, "\n");
		}

		fprintf(reportFile, "Desktop Res,\n");
		fprintf(reportFile, ",\n");
		fprintf(reportFile, "The cells contain the following:,\n default render scale, scaled resolution, scaled or effective fill rate, msaa, ssao,");

		fclose(reportFile);

		conPrintf("Settings report saved in \"%s\".\n", strReportFilename);
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(gfxSettingsGenerateDefaultSettingsReport);
void gfxSettingsGenerateDefaultSettingsReportDefault(void)
{
	gfxSettingsGenerateDefaultSettingsReport("c:\\resolutionReport.csv");
}

