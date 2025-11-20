#include "gclOptions.h"

#include "GraphicsLib.h"
#include "GfxLightOptions.h"

#include "RenderLib.h"
#include "RdrState.h"

#include "file.h"
#include "Message.h"
#include "cpu_count.h"
#include "qsortG.h"
#include "winutil.h"

#include "UIGen.h"
#include "Prefs.h"
#include "wlState.h"
#include "WorldLibEnums.h"
#include "GlobalTypes.h"

#define DEBUG_TRACE_SETTINGS 1

#if DEBUG_TRACE_SETTINGS
#include "memlog.h"
#endif

#if DEBUG_TRACE_SETTINGS
#define TRACE_SETTINGS( FMT, ... )	memlog_printf( NULL, FMT, __VA_ARGS__ )
#else
#define TRACE_SETTINGS( FMT, ... )
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

//////////////////////////////////////////////////////////////////////////
// Graphics specific

#define GRAPHICS_CATEGORY "Graphics"
#define UI_SCALE_RESOLUTION_WIDTH 1024	// the resolution that ui scaling should automatically be applied
#define UI_SCALE_RESOLUTION_HEIGHT 768

AUTO_STRUCT;
typedef struct RefreshRate
{
	int iRefreshRate; AST(KEY) // Refresh Rate in Hz
	GfxResolution** eaResolutions; NO_AST // EArray of resolutions that support this refresh rate
} RefreshRate;

#include "AutoGen/gclGraphicsOptions_c_ast.h"

static GfxSettings gcl_gfx_settings;
static int shadow_type;
static GfxResolution **supported_resolutions=0;
static GfxResolution *desktop_res=0;
static int resolution_selection;
static RefreshRate **refreshRates=NULL;
static int refreshrate_selection;
static int fullscreen_mode;
static int halfResRender;
static F32 inv_gamma;
static F32 world_texture_quality_slider;
static F32 entity_texture_quality_slider;
static int terrain_detail_slider;
static int aspect_ratio_index;
static int lighting_quality;
static U32 reset_settings_frame_count; // The timestamp at which we last reset settings
static F32 renderScale;
static F32 slowUglyScale;
static int showAdvanced;
static int monitor_index;
static int limitInactiveFPS;
static int antialiasing;
static int texAniso;
static int doPerFrameSleep;
static int maxFPS_index;
static int videoMemoryMax_index;
static int matDesiredScore;
static OptionSetting* ui_scale_setting = NULL;

static OptionSetting* settingFullscreen = NULL;
static OptionSetting* settingResolution = NULL;
static OptionSetting* settingRefreshRate = NULL;
static OptionSetting* settingGamma = NULL;
static OptionSetting* settingHalfRes = NULL;
static OptionSetting* settingAspect_Ratio = NULL;
static OptionSetting* settingRenderScale = NULL;
static OptionSetting* settingShow_Advanced = NULL;
static OptionSetting* settingSlow_Ugly_Scale = NULL;
static OptionSetting* settingDevice = NULL;
static OptionSetting* settingV_Sync = NULL;
static OptionSetting* settingAntialiasing = NULL;
static OptionSetting* settingDynamic_Lights = NULL;
static OptionSetting* settingWorld_Texture_Quality = NULL;
static OptionSetting* settingCharacter_Texture_Quality = NULL;
static OptionSetting* settingWorld_Detail_Level = NULL;
static OptionSetting* settingTerrain_Detail_Level = NULL;
static OptionSetting* settingCharacter_Detail_Level = NULL;
static OptionSetting* settingHigh_Detail_Objects = NULL;
static OptionSetting* settingMax_Debris = NULL;
static OptionSetting* settingLighting_Quality = NULL;
static OptionSetting* settingMax_Lights_Per_Object = NULL;
static OptionSetting* settingMax_Shadowed_Lights = NULL;
static OptionSetting* settingTexture_Anisotropic_Filtering = NULL;
static OptionSetting* settingPreload_Materials = NULL;
static OptionSetting* settingFrameRateStabilizer = NULL;
static OptionSetting* settingAutoEnableFrameRateStabilizer = NULL;
static OptionSetting* settingMax_Inactive_FPS = NULL;
static OptionSetting* settingRender_Thread = NULL;
static OptionSetting* settingShadows = NULL;
static OptionSetting* settingPostprocessing = NULL;
static OptionSetting* settingBloom_Quality = NULL;
static OptionSetting* settingBloom_Intensity = NULL;
static OptionSetting* settingScreen_Space_Ambient_Occlusion = NULL;
static OptionSetting* settingOutlining = NULL;
static OptionSetting* settingDepth_of_Field = NULL;
static OptionSetting* settingWater = NULL;
static OptionSetting* settingFX_Quality = NULL;
static OptionSetting* settingSoft_Particles = NULL;
static OptionSetting* settingScattering_Quality = NULL;
static OptionSetting* settingMax_Reflection = NULL;
static OptionSetting* settingUseFullSkinning = NULL;
static OptionSetting* settingGPUParticles = NULL;
static OptionSetting* settingReduceFileStreaming = NULL;
static OptionSetting* settingMaterialDesiredScore = NULL;


static int maxFPS_options[] = {
	0, 30, 60, 120
};

static int videoMemoryMax_options[] = {
	0, 1, 2, 4, 6, 8
};

static struct {
	float v;
	const char *name;
} aspect_options[] = {
	{0, "Auto"},
	{4.f/3.f, "4:3"},
	{16.f/9.f, "16:9"},
	{16.f/10.f, "16:10"}
};

// Slider goes 50% to 200%
// Return [0.50-1.00-2.00] mapped to [0.25-1.0-10]
F32 texReduceFromTexSlider(F32 val)
{
	if (val > 1)
		return val;
	return 0.25f + 0.75f * (val - 0.5) / 0.5;
}
// Return [0.25-1.0-10] mapped to [0.50-1.00-2.00]
F32 texSliderFromTexReduce(F32 val)
{
	if (val > 1)
		return val;
	return MAX(0.5, 0.5f + 0.5 * (val - 0.25f) / 0.75f);
}

F32 gclGraphics_GetPixelFill(void)
{
	return gfxGetEffectiveFill(&gcl_gfx_settings);
}

F32 gclGraphics_GetRecommendedRenderScale(void)
{
	return gfxSettingsGetRecommendedRenderScale(&gcl_gfx_settings);
}

static void uiScaleChanged(OptionSetting *setting)
{
	ui_GenSetBaseScale(setting->fFloatValue);
	GamePrefStoreFloat("UIScale", setting->fFloatValue);
}

static void uiScaleUpdate(OptionSetting *setting)
{
	F32 fScale = ui_GenGetBaseScale();
	if (fScale != setting->fFloatValue)
	{
		setting->fFloatValue = fScale;
		GamePrefStoreFloat("UIScale", setting->fFloatValue);
	}
}

static void uiScaleDefault(OptionSetting *setting)
{
	F32 fScale = GamePrefGetFloat("UIScale", 1.f);
	ui_GenSetBaseScale(fScale);
	if (setting)
		setting->fFloatValue = fScale;
}

static int calcDesiredMatScore(F32 slowUglyScaleVal)
{
	if (gConf.bEnableShaderQualitySlider == SHADER_QUALITY_SLIDER_LABEL)
	{
		if (showAdvanced)
		{
			return CLAMP(matDesiredScore,0,SGRAPH_QUALITY_MAX_VALUE);
		}
		else
		{
			U32 scaleCase = (gConf.bUseLegacyGraphicsQualitySlider ? round(slowUglyScaleVal) : 2);	// 0.1 is to correct for floating point error.
			switch (scaleCase)
			{
			case 0: matDesiredScore = SGRAPH_QUALITY_LOW;
				break;
			case 1: matDesiredScore = CLAMP(gcl_gfx_settings.recommended_Mat_quality - 1,0,SGRAPH_QUALITY_MAX_VALUE);
				break;
			case 2: matDesiredScore = gcl_gfx_settings.recommended_Mat_quality;
				break;
			case 3: matDesiredScore = SGRAPH_QUALITY_VERY_HIGH;
				break;
			default: matDesiredScore = SGRAPH_QUALITY_LOW;
			}
			return matDesiredScore;
		}
	}
	return 0;
}

static void preApply(void)
{
	// Takes local settings and puts them into gcl_gfx_settings to be passed to gfxApplySettings()

	gcl_gfx_settings.antialiasing = 1 << antialiasing;
	gcl_gfx_settings.texAniso = 1 << texAniso;
	
	gcl_gfx_settings.perFrameSleep = doPerFrameSleep ? 10 : 0;
	gcl_gfx_settings.maxFps = maxFPS_options[maxFPS_index];
	gcl_gfx_settings.videoMemoryMax = videoMemoryMax_options[videoMemoryMax_index];
	
	gcl_gfx_settings.disableSplatShadows = 0;
	gcl_gfx_settings.features_desired &= ~GFEATURE_SHADOWS;
	gcl_gfx_settings.poissonShadows = 0;

	if (shadow_type >= 2 && gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_SHADOWS))
	{
		gcl_gfx_settings.features_desired |= GFEATURE_SHADOWS;
		if (shadow_type == 3)
			gcl_gfx_settings.poissonShadows = 1;
	}
	else if (shadow_type == 0)
	{
		gcl_gfx_settings.disableSplatShadows = 1;
	}

	if (lighting_quality == 0)
		gcl_gfx_settings.lighting_quality = GLIGHTING_QUALITY_VERTEX_ONLY_LIGHTING;
	else if (lighting_quality == 1)
		gcl_gfx_settings.lighting_quality = GLIGHTING_QUALITY_HIGH;

	if (resolution_selection >= 0)
	{
		gcl_gfx_settings.fullscreenRes = supported_resolutions[resolution_selection];
		gcl_gfx_settings.monitor_index_plus_one = gcl_gfx_settings.fullscreenRes->adapter + 1;
		gcl_gfx_settings.fullscreenWidth = supported_resolutions[resolution_selection]->width;
		gcl_gfx_settings.fullscreenHeight = supported_resolutions[resolution_selection]->height;
	}
	else
	{
		gcl_gfx_settings.fullscreenRes = desktop_res;
		gcl_gfx_settings.monitor_index_plus_one = gcl_gfx_settings.fullscreenRes->adapter + 1;
		gcl_gfx_settings.fullscreenWidth = desktop_res->width;
		gcl_gfx_settings.fullscreenHeight = desktop_res->height;
	}

	if (refreshrate_selection >= 0 && refreshrate_selection < eaSize(&refreshRates))
	{
		gcl_gfx_settings.refreshRate = refreshRates[refreshrate_selection]->iRefreshRate;
	}
	else
	{
		gcl_gfx_settings.refreshRate = 60;
	}
	
	//only mess with this value if its not set to some custom thing
	if (nearf(gcl_gfx_settings.renderScale[0], 0) || nearf(gcl_gfx_settings.renderScale[0], 1) || nearf(gcl_gfx_settings.renderScale[0], 0.5))
	{
		if (halfResRender) 
		{
			setVec2(gcl_gfx_settings.renderScale, 0.5, 0.5);
			setVec2(gcl_gfx_settings.renderScaleSetBySize, 0, 0);
		}
		else
		{
			setVec2(gcl_gfx_settings.renderScale, 0, 0);
			setVec2(gcl_gfx_settings.renderScaleSetBySize, 0, 0);
		}
	}
	{
		setVec2(gcl_gfx_settings.renderScale, renderScale, renderScale);
		setVec2(gcl_gfx_settings.renderScaleSetBySize, 0, 0);
	}
	gcl_gfx_settings.maxInactiveFps = limitInactiveFPS ? 5 : 0;

	gcl_gfx_settings.gamma = 2 - inv_gamma;
	if (gcl_gfx_settings.gamma >= 0.97 && gcl_gfx_settings.gamma <= 1.03)
		gcl_gfx_settings.gamma = inv_gamma = 1;

	gcl_gfx_settings.worldTexLODLevel = texReduceFromTexSlider(world_texture_quality_slider);
	gcl_gfx_settings.entityTexLODLevel = texReduceFromTexSlider(entity_texture_quality_slider);
	gcl_gfx_settings.terrainDetailLevel = terrain_detail_slider/10.f;

	assert(INRANGE(aspect_ratio_index, 0, ARRAY_SIZE(aspect_options)));
	gcl_gfx_settings.aspectRatio = aspect_options[aspect_ratio_index].v;

	if (monitor_index)
	{
		const RdrDeviceInfo * const * devices = rdrEnumerateDevices();
		if (monitor_index - 1 < eaSize(&devices))
			gcl_gfx_settings.device_type = devices[monitor_index - 1]->type;
		else
		{
			// back to default
			monitor_index = 0;
			gcl_gfx_settings.device_type = "Direct3D9";
		}
		gcl_gfx_settings.monitor_index_plus_one = monitor_index;
	}
	else
		gcl_gfx_settings.device_type = "Direct3D9";
	if (fullscreen_mode == 0)
	{
		gcl_gfx_settings.fullscreen = 0;
		gcl_gfx_settings.maximized = 0;
	} else if (fullscreen_mode == 1) {
		gcl_gfx_settings.fullscreen = 1;
		gcl_gfx_settings.maximized = 0;
	} else if (fullscreen_mode == 2) {
		rdr_state.disable_windowed_fullscreen = 0;
		gcl_gfx_settings.fullscreen = 0;
		gcl_gfx_settings.maximized = 1;
	}

	if (!showAdvanced && (!nearf(slowUglyScale, gcl_gfx_settings.slowUglyScale * (gConf.bUseLegacyGraphicsQualitySlider ? 3.0f : 1.0f)) || showAdvanced != gcl_gfx_settings.showAdvanced))
	{
		gcl_gfx_settings.slowUglyScale = slowUglyScale / (gConf.bUseLegacyGraphicsQualitySlider ? 3.0f : 1.0f);
		gfxGetScaledAdvancedSettings(gfxGetPrimaryDevice(), &gcl_gfx_settings, gcl_gfx_settings.slowUglyScale);
	}

	gcl_gfx_settings.desired_Mat_quality = calcDesiredMatScore(slowUglyScale);

	gcl_gfx_settings.showAdvanced = showAdvanced;
}

static void postGet(void)
{
	RdrDevice *active_rdr_device = gfxGetActiveDevice();
	int i;
	float closest_dist = FLT_MAX;
	// Just called gfxGetSettings, take values from gcl_gfx_settings and put them
	// into locals for the UI
	antialiasing = log2(gcl_gfx_settings.antialiasing);
	texAniso = log2(gcl_gfx_settings.texAniso);
	doPerFrameSleep = !!gcl_gfx_settings.perFrameSleep;
	maxFPS_index = 0;
	for (i=0; i<ARRAY_SIZE(maxFPS_options); i++)
	{
		if (gcl_gfx_settings.maxFps <= maxFPS_options[i])
		{
			maxFPS_index = i;
			break;
		}
	}
	for (i=0; i<ARRAY_SIZE(videoMemoryMax_options); i++)
	{
		if (gcl_gfx_settings.videoMemoryMax <= videoMemoryMax_options[i])
		{
			videoMemoryMax_index = i;
			break;
		}
	}
	if (gcl_gfx_settings.features_desired & GFEATURE_SHADOWS)
	{
		shadow_type = gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_SHADOWS) ? 2 : 1;
		if (shadow_type == 2 && gcl_gfx_settings.poissonShadows)
			shadow_type = 3;
	}
	else if (gcl_gfx_settings.disableSplatShadows)
		shadow_type = 0;
	else
		shadow_type = 1;

	if (gcl_gfx_settings.lighting_quality == GLIGHTING_QUALITY_VERTEX_ONLY_LIGHTING)
	{
		lighting_quality = 0;
		MIN1(shadow_type, 1); // Can't have MED or HIGH shadows with vertex lighting
	} else 
		lighting_quality = 1;

	gcl_gfx_settings.fullscreenRes = NULL;
	resolution_selection = -1;

	FOR_EACH_IN_EARRAY(supported_resolutions, GfxResolution, res)
	{
		if ((res->width == gcl_gfx_settings.fullscreenWidth &&
			res->height == gcl_gfx_settings.fullscreenHeight) ||
			(res->height == gcl_gfx_settings.fullscreenWidth &&
			res->width == gcl_gfx_settings.fullscreenHeight))
		{
			resolution_selection = iresIndex;
			gcl_gfx_settings.fullscreenRes = res;
			break;
		}
		else
		{
			float dist = sqrtf(SQR(res->width - gcl_gfx_settings.fullscreenWidth) + SQR(res->height - gcl_gfx_settings.fullscreenHeight));
			if (closest_dist > dist)
			{
				closest_dist = dist;
				resolution_selection = iresIndex;
				gcl_gfx_settings.fullscreenRes = res;
			}
		}
	}
	FOR_EACH_END;
	if (gcl_gfx_settings.fullscreenRes)
		TRACE_SETTINGS("Setting res index to %d: %d x %d, Adap %d", resolution_selection, gcl_gfx_settings.fullscreenRes->width, gcl_gfx_settings.fullscreenRes->height, gcl_gfx_settings.fullscreenRes->adapter);

	refreshrate_selection = -1;
	for (i=0; i<eaSize(&refreshRates); i++)
	{
		if (gcl_gfx_settings.refreshRate == refreshRates[i]->iRefreshRate || refreshRates[i]->iRefreshRate==60 && refreshrate_selection==-1)
			refreshrate_selection = i;
	}

	if (nearf(gcl_gfx_settings.renderScale[0], 0) || nearf(gcl_gfx_settings.renderScale[0], 1))
		halfResRender = 0;
	else
		halfResRender = 1;

	limitInactiveFPS = gcl_gfx_settings.maxInactiveFps ? 1 : 0;

	inv_gamma = 2 - gcl_gfx_settings.gamma;

	renderScale = gcl_gfx_settings.renderScale[0];

	slowUglyScale = gcl_gfx_settings.slowUglyScale * (gConf.bUseLegacyGraphicsQualitySlider ? 3.0f : 1.0f);
	showAdvanced = gcl_gfx_settings.showAdvanced;
	world_texture_quality_slider = texSliderFromTexReduce(gcl_gfx_settings.worldTexLODLevel);
	entity_texture_quality_slider = texSliderFromTexReduce(gcl_gfx_settings.entityTexLODLevel);
	terrain_detail_slider = round(gcl_gfx_settings.terrainDetailLevel*10.f);

	{
		int besti=0;
		for (i=1; i<ARRAY_SIZE(aspect_options); i++) {
			if (ABS(gcl_gfx_settings.aspectRatio - aspect_options[i].v) < ABS(gcl_gfx_settings.aspectRatio - aspect_options[besti].v))
				besti = i;
		}
		aspect_ratio_index = besti;
	}

	monitor_index = gcl_gfx_settings.monitor_index_plus_one;

	if (gcl_gfx_settings.fullscreen)
		fullscreen_mode = 1;
	else if (gcl_gfx_settings.maximized && !rdr_state.disable_windowed_fullscreen)
		fullscreen_mode = 2;
	else
		fullscreen_mode = 0;

	if (gConf.bEnableShaderQualitySlider)
	{
		if (showAdvanced) {
			matDesiredScore = gcl_gfx_settings.desired_Mat_quality;
		} else {
			calcDesiredMatScore(slowUglyScale);
		}
		wlSetDesiredMaterialScore(matDesiredScore);
	}

	// Update UI
	OptionSettingSetActive(settingResolution, gcl_gfx_settings.fullscreen ? true : false);
	OptionSettingSetActive(settingRefreshRate, gcl_gfx_settings.fullscreen ? true : false);
	OptionSettingSetActive(settingGamma, true);
	OptionSettingSetActive(settingMax_Shadowed_Lights, (shadow_type <= 1) ? false : true);
	{
		//these require postprocessing if you have antialiasing
		bool allowEffectsThatUseDepth = gfxSettingsAllowsDepthEffects(&gcl_gfx_settings);
		bool allowSSAO = (systemSpecsMaterialSupportedFeatures() & SGFEAT_SM30_PLUS) && gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_POSTPROCESSING) && gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_SHADOWS);

		//If allow allowEffectsThatUseDepth == false what's the reason we show? (either post processing is off, or msaa is on)
		const char* depthFailMessage = "Options_Setting_Graphics_Disabled_Tooltip_Depth_MSAA";
		const char* noGPUSupportMessage = "Options_Setting_Graphics_Disabled_Tooltip_GPU";
		
		OptionSettingSetActive(settingScreen_Space_Ambient_Occlusion, allowEffectsThatUseDepth && allowSSAO);
		OptionSettingSetActive(settingOutlining, allowEffectsThatUseDepth && gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_OUTLINING));
		OptionSettingSetActive(settingDepth_of_Field, allowEffectsThatUseDepth && gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_DOF));
		OptionSettingSetActive(settingWater, allowEffectsThatUseDepth  && gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_WATER));
		OptionSettingSetActive(settingSoft_Particles, allowEffectsThatUseDepth && gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_POSTPROCESSING));

		//setup disabled messages (these are only displayed if disabled)
		//either display the reason why you cant use the depth (if they are supported) or display a "your gpu does not support this" message)
		OptionSettingSetDisabledTooltip(settingScreen_Space_Ambient_Occlusion, allowSSAO ? depthFailMessage : noGPUSupportMessage);
		OptionSettingSetDisabledTooltip(settingOutlining, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_OUTLINING) ? depthFailMessage : noGPUSupportMessage);
		OptionSettingSetDisabledTooltip(settingDepth_of_Field, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_DOF) ? depthFailMessage : noGPUSupportMessage);
		OptionSettingSetDisabledTooltip(settingWater, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_WATER) ? depthFailMessage : noGPUSupportMessage);
		OptionSettingSetDisabledTooltip(settingSoft_Particles, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_POSTPROCESSING) ? depthFailMessage : noGPUSupportMessage);
	}

}

static void gfxSettingsUpdateHiddenChoices()
{
	int i,j;
	// Hide any resolution less than the game's minimum-supported resolution
	{
		FOR_EACH_IN_EARRAY_FORWARDS(supported_resolutions, GfxResolution, res)
		{
			if(res->height < gConf.iMinimumFullscreenResolutionHeight || res->width < gConf.iMinimumFullscreenResolutionWidth )
			{
				OptionSettingComboChoice* pChoice = settingResolution->eaComboBoxOptions[iresIndex];
				// Mark it not to be shown
				pChoice->bHideChoice = true;
			}
		}
		FOR_EACH_END;
	}

	// Hide all duplicate resolutions
	if( gConf.bShowOnlyOneCopyOfEachResolution )
	{
		for(i=0; i<eaSize(&settingResolution->eaComboBoxOptions); ++i)
		{
			OptionSettingComboChoice* pChoice = settingResolution->eaComboBoxOptions[i];
			// If we haven't already hidden this choice
			if( !pChoice->bHideChoice )
			{
				// Hide every matching choice after this one
				for( j=i+1; j<eaSize(&settingResolution->eaComboBoxOptions); ++j)
				{
					OptionSettingComboChoice* pChoice2 = settingResolution->eaComboBoxOptions[j];
					if( pChoice2->pchName == pChoice->pchName )
					{
						pChoice2->bHideChoice = true;
					}
				}
			}
		}
	}

	// Hide all refresh rates that aren't supported by the current resolution choice
	if( resolution_selection >= 0 && resolution_selection < eaSize(&supported_resolutions) )
	{
		int iWidth = supported_resolutions[resolution_selection]->width;
		int iHeight = supported_resolutions[resolution_selection]->height;

		// Hide all refresh rates
		for(i=0; i<eaSize(&settingRefreshRate->eaComboBoxOptions); ++i)
		{
			OptionSettingComboChoice* pChoice = settingRefreshRate->eaComboBoxOptions[i];
			pChoice->bHideChoice = true;
		}

		// Show all refresh rates supported by this resolution
		for(i=0; i<eaSize(&settingRefreshRate->eaComboBoxOptions); ++i)
		{
			OptionSettingComboChoice* pChoice = settingRefreshRate->eaComboBoxOptions[i];
			RefreshRate *pRefreshRate = refreshRates[pChoice->iIndex];
			// If any of the resolutions match the current width & height, show the choice
			for(j=0; j<eaSize(&pRefreshRate->eaResolutions); ++j)
			{
				GfxResolution *res = pRefreshRate->eaResolutions[j];
				if( res->width == iWidth && res->height == iHeight )
				{
					pChoice->bHideChoice = false;
					break;
				}
			}
		}

		// Change the refresh rate so it's one of the valid choices for the current resolution.
		if( refreshrate_selection >= 0 && refreshrate_selection < eaSize(&refreshRates) )
		{
			GfxResolution *pCurrentResolution = supported_resolutions[resolution_selection];
			RefreshRate *pCurrentRefreshRate = refreshRates[refreshrate_selection];
			bool bRefreshRateIsSupported = false;
			for(j=0; j<eaSize(&pCurrentRefreshRate->eaResolutions); ++j)
			{
				if( pCurrentRefreshRate->eaResolutions[j]->width == pCurrentResolution->width && 
					pCurrentRefreshRate->eaResolutions[j]->height == pCurrentResolution->height )
				{
					bRefreshRateIsSupported = true;
					break;
				}
			}
			if( !bRefreshRateIsSupported )
			{
				// Refresh rate not supported, so pick a new one that is.
				int iNewRefreshRate = pCurrentResolution->refreshRates[0];
				refreshrate_selection = eaIndexedFindUsingInt(&refreshRates, iNewRefreshRate); // TODO This may have to update the UI
			}
		}
	}
}

static void gfxSettingsChangedCallback(OptionSetting *setting_UNUSED)
{
	TRACE_SETTINGS("%s apply callback start", __FUNCTION__);
	preApply();
	gfxApplySettings(gfxGetPrimaryDevice(), &gcl_gfx_settings, ApplySettings_PastStartup | ApplySettings_OnlyDynamic, gfxGetAccessLevel());
	postGet();
	TRACE_SETTINGS("%s apply callback complete", __FUNCTION__);
	OptionsUpdateCategory(GRAPHICS_CATEGORY);
	gfxSettingsUpdateHiddenChoices();
}

static void gfxSettingsCommittedCallback(OptionSetting *setting_UNUSED)
{
	TRACE_SETTINGS("%s commit callback start", __FUNCTION__);
	preApply();
	gfxApplySettings(gfxGetPrimaryDevice(), &gcl_gfx_settings, ApplySettings_PastStartup, gfxGetAccessLevel());
	postGet();
	TRACE_SETTINGS("%s commit callback complete", __FUNCTION__);
}

static void gfxSettingsUpdateCallback(OptionSetting *setting_UNUSED)
{
	TRACE_SETTINGS("%s update callback start", __FUNCTION__);
	preApply();
	gfxGetSettings(&gcl_gfx_settings);
	postGet();
	TRACE_SETTINGS("%s update callback complete", __FUNCTION__);
}

static void gfxSettingsRestoreDefaultsCallback(OptionSetting *setting_UNUSED)
{
	if (reset_settings_frame_count != gfxGetFrameCount())
	{
		reset_settings_frame_count = gfxGetFrameCount();

 		preApply();
		gfxSettingsSetResolutionByFill(&gcl_gfx_settings);
		gfxSettingsSetRenderScaleByResolutionAndFill(&gcl_gfx_settings);
 		gfxGetRecommendedSettings(gfxGetPrimaryDevice(), &gcl_gfx_settings);
 		postGet();
	}
}

static int greatestCommonFactor(int x, int y)
{
	x = labs(x);
	y = labs(y);
	while (x > 0)
	{
		int temp = x;
		x = y % x;
		y = temp;
	}
	return y;
}

static void reduceFraction(int w, int h, int *n, int *d)
{
	int factor = greatestCommonFactor(w, h);
	*n = w / factor;
	*d = h / factor;
}

void displayAnisoFilter(OptionSetting *setting)
{
	if (setting->iIntValue == 0)
		estrCopy2(&setting->pchStringValue, TranslateMessageKeySafe("Options_Setting_Graphics_Shadows_Combo_Off"));
	else
		estrPrintf(&setting->pchStringValue, "%dx", 1 << setting->iIntValue);
}

void inputAnisoFilter(OptionSetting *setting)
{
	//get the lower base 2 exponent
	frexpf(setting->iIntValue,&setting->iIntValue);
	if ( setting->iIntValue > 0 )
		setting->iIntValue--;
}

void displayMatDesiredScore(OptionSetting *setting)
{
	setting->iIntValue = CLAMP(setting->iIntValue, 0, SGRAPH_QUALITY_MAX_VALUE);
	estrPrintf(&setting->pchStringValue, "%s", (char*)StaticDefineIntRevLookup(ShaderGraphQualityEnum, setting->iIntValue));
}

void inputMatDesiredScore(OptionSetting *setting)
{
	setting->iIntValue = CLAMP(setting->iIntValue, 0, SGRAPH_QUALITY_MAX_VALUE);
}

void displayIntAsPercentage(OptionSetting *setting)
{
	estrPrintf(&setting->pchStringValue, "%d%%", setting->iIntValue * 10);
}

void gclGraphicsOptions_Init(void)
{
	int i;
	const char **options=NULL;
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();

	assert(!refreshRates);

	gfxGetSettings(&gcl_gfx_settings);
	
	gfxSettingsGetSupportedResolutionsForDevice(&desktop_res, &supported_resolutions, 
		device_infos[gfxGetActiveDevice()->display_nonthread.preferred_adapter]);

	// Build a list of refresh rates supported by the current resolution. Run this every time resolution_selection changes.
	eaIndexedEnable(&refreshRates, parse_RefreshRate);
	FOR_EACH_IN_EARRAY(supported_resolutions, GfxResolution, res)
	{
		if (res->depth == 32)
		{
			for (i=0; i<eaiSize(&res->refreshRates); i++)
			{
				RefreshRate * pRefreshRate = eaIndexedGetUsingInt(&refreshRates, res->refreshRates[i]);
				if( pRefreshRate == NULL )
				{
					pRefreshRate = StructCreate(parse_RefreshRate);
					pRefreshRate->iRefreshRate = res->refreshRates[i];
					eaIndexedAdd(&refreshRates, pRefreshRate);
				}
				eaPushUnique(&pRefreshRate->eaResolutions, res);
			}
		}
	}
	FOR_EACH_END;

	gfxGetSettings(&gcl_gfx_settings);
	postGet();

	uiScaleDefault(NULL);
	ui_scale_setting = OptionSettingAddSlider("Basic", "UIScale", 0.5f, 2.f, 1.f, NULL, uiScaleChanged, uiScaleUpdate, NULL, NULL, NULL, NULL);

	// Note text for this is in \data\ui\gens\Options.uigen.ms

	//////////////////////////////////////////////////////////////////////////
	// Non-advanced options first

#if !PLATFORM_CONSOLE
	
	options = NULL;
	eaPush(&options, "Windowed");
	eaPush(&options, "Fullscreen");
	eaPush(&options, "Windowed_Fullscreen");

	settingFullscreen = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Fullscreen", &options, true, &fullscreen_mode, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	// Resolution
	options = NULL;
	FOR_EACH_IN_EARRAY_FORWARDS(supported_resolutions, GfxResolution, res)
	{
		assert(res->depth == 32); // Otherwise resolution_selection logic (index into array) doesn't work
		{
			const RdrDeviceInfo * device_info = device_infos[res->adapter];
			const RdrDeviceMode * res_mode = device_info->display_modes[res->displayMode];
			char buf[1024];
			int d, n;
			reduceFraction(res->width, res->height, &d, &n);
			if (d==8)  // should be 16:something
			{
				d*=2;
				n*=2;
			}
			if (d>16)
			{
				// Something non-standard, perhaps better to round
				// 1366x768 does this (actual is 683:384, want 16:9 to display)
				F32 frac = res->width / (float)res->height;
				if (nearSameF32Tol(frac, 16.f/9.f, 0.02))
				{
					d = 16; n = 9;
				}
				if (nearSameF32Tol(frac, 16.f/10.f, 0.02))
				{
					d = 16; n = 10;
				}
			}
			if( !gConf.bShowOnlyOneCopyOfEachResolution )
			{
				if (res->width == desktop_res->width && res->height == desktop_res->height)
				{
					sprintf(buf, "%s %s (%d:%d)", res_mode->name, TranslateMessageKeySafe("Options_Setting_Graphics_Resolution_Desktop"), d, n);
				} else {
					sprintf(buf, "%s (%d:%d)", res_mode->name, d, n);
				}
			}
			else
			{
				if (res->width == desktop_res->width && res->height == desktop_res->height)
				{
					sprintf(buf, "%d x %d %s", res->width, res->height, TranslateMessageKeySafe("Options_Setting_Graphics_Resolution_Desktop"));
				} else {
					sprintf(buf, "%d x %d", res->width, res->height);
				}
			}

			if (res->adapter == gcl_gfx_settings.monitor_index_plus_one - 1 &&
				res_mode->width == gcl_gfx_settings.fullscreenWidth && res_mode->height == gcl_gfx_settings.fullscreenHeight
				)
			{
				gcl_gfx_settings.fullscreenRes = res;
			}

			eaPush(&options, allocAddCaseSensitiveString(buf));
		}
	}
	FOR_EACH_END;
	settingResolution = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Resolution", &options, false, &resolution_selection, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true); // gcl_gfx_settings.fullscreen);

	options = NULL;
	{
		for (i=0; i<eaSize(&refreshRates); i++)
		{
			char buf[1024];
			sprintf(buf, "%d Hz", refreshRates[i]->iRefreshRate);
			eaPush(&options, allocAddCaseSensitiveString(buf));
		}
 
		settingRefreshRate = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "RefreshRate", &options, false, &refreshrate_selection, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true); // gcl_gfx_settings.fullscreen);
	}


	settingGamma = autoSettingsAddFloatSlider(GRAPHICS_CATEGORY, "Gamma", 0, 1.9, &inv_gamma, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, displayFloatAsPercentage, inputFloatPercentage, true);
	if (!gConf.bUseRenderScaleSlider)
		settingHalfRes = autoSettingsAddBit(GRAPHICS_CATEGORY, "HalfRes", &halfResRender, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

#endif

	options = NULL;
	for (i=0; i<ARRAY_SIZE(aspect_options); i++)
		eaPush(&options, aspect_options[i].name);
	settingAspect_Ratio = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Aspect_Ratio", &options, true, &aspect_ratio_index, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
	if (gConf.bUseRenderScaleSlider)
		settingRenderScale = autoSettingsAddFloatSlider(GRAPHICS_CATEGORY, "Render_Scale", .5, 1, &renderScale, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, NULL, true);

	settingShow_Advanced = autoSettingsAddBit(GRAPHICS_CATEGORY, "Show_Advanced", &showAdvanced, true, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
	settingSlow_Ugly_Scale = autoSettingsAddFloatSlider(GRAPHICS_CATEGORY, "Slow_Ugly_Scale", 0, (gConf.bUseLegacyGraphicsQualitySlider ? 3.0f : 1.0f), &slowUglyScale, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, NULL, true);

	//////////////////////////////////////////////////////////////////////////
	// Advanced options after slider (slider is in .uigen file)

#if !PLATFORM_CONSOLE
	// Device selection
	{
		const RdrDeviceInfo * const * devices = rdrEnumerateDevices();
		options = NULL;
		eaPush(&options, TranslateMessageKey("DeviceType_Auto"));
		FOR_EACH_IN_EARRAY_FORWARDS(devices, const RdrDeviceInfo, device)
		{
			char key[1024];
			char buf[1024];
			//if (stricmp(device->type, "Direct3D11")==0)
			//	break; // Have to break; here, not continue;, otherwise indices will be off
			sprintf(key, "DeviceType_%s", device->type);
			snprintf(buf, _TRUNCATE, "%s #%d (%s)", device->name, device->monitor_index + 1, TranslateMessageKey(key));
			eaPush(&options, allocAddCaseSensitiveString(buf));
		}
		FOR_EACH_END;
		if (eaSize(&options) > 2)
		{
			// More than just one display device + "auto"
			settingDevice = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Device", &options, false, &monitor_index, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
		}
	}
#endif

	settingV_Sync = autoSettingsAddBit(GRAPHICS_CATEGORY, "V_Sync", &gcl_gfx_settings.useVSync, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);


#if !PLATFORM_CONSOLE
	// Antialiasing selection
	{
		options = NULL;
		eaPush(&options, allocAddCaseSensitiveString("None"));
		if (rdrSupportsSurfaceType(gfxGetPrimaryDevice(), SBT_RGBA) >= 2)
			eaPush(&options, allocAddCaseSensitiveString("2x"));
		if (rdrSupportsSurfaceType(gfxGetPrimaryDevice(), SBT_RGBA) >= 4)
			eaPush(&options, allocAddCaseSensitiveString("4x"));
		if (rdrSupportsSurfaceType(gfxGetPrimaryDevice(), SBT_RGBA) >= 8)
			eaPush(&options, allocAddCaseSensitiveString("8x"));
		if (rdrSupportsSurfaceType(gfxGetPrimaryDevice(), SBT_RGBA) >= 16)
			eaPush(&options, allocAddCaseSensitiveString("16x"));
		settingAntialiasing = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Antialiasing", &options, false, &antialiasing, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, rdrSupportsSurfaceType(gfxGetPrimaryDevice(), SBT_RGBA) >= 2);
		OptionSettingSetDisabledTooltip(settingAntialiasing, "Options_Setting_Graphics_Disabled_Tooltip_GPU");
	}
#endif

	settingDynamic_Lights = autoSettingsAddBit(GRAPHICS_CATEGORY, "Dynamic_Lights", &gcl_gfx_settings.dynamicLights, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
	
	settingWorld_Texture_Quality = autoSettingsAddFloatSliderEx(GRAPHICS_CATEGORY, "World_Texture_Quality", 0.5, 2.0, 0.125, &world_texture_quality_slider, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, displayFloatAsPercentage, inputFloatPercentage, true);
	settingCharacter_Texture_Quality = autoSettingsAddFloatSliderEx(GRAPHICS_CATEGORY, "Character_Texture_Quality", 0.5, 2.0, 0.125, &entity_texture_quality_slider, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, displayFloatAsPercentage, inputFloatPercentage, true);

	//autoSettingsAddFloatSlider(GRAPHICS_CATEGORY, "Gamma", -0.7, 3.4, &gcl_gfx_settings.gamma, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback,  true);

	settingWorld_Detail_Level = autoSettingsAddFloatSliderEx(GRAPHICS_CATEGORY, "World_Detail_Level", 0.25, 2.0, 0.125, &gcl_gfx_settings.worldDetailLevel, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, displayFloatAsPercentage, inputFloatPercentage, true);
	settingTerrain_Detail_Level = autoSettingsAddIntSlider(GRAPHICS_CATEGORY, "Terrain_Detail_Level", 8, 30, &terrain_detail_slider, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, displayIntAsPercentage, inputIntPercentageScaled, true);
	settingCharacter_Detail_Level = autoSettingsAddFloatSliderEx(GRAPHICS_CATEGORY, "Character_Detail_Level", 0.25, 2.0, 0.125, &gcl_gfx_settings.entityDetailLevel, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, displayFloatAsPercentage, inputFloatPercentage, true);
	settingHigh_Detail_Objects = autoSettingsAddBit(GRAPHICS_CATEGORY, "High_Detail_Objects", &gcl_gfx_settings.draw_high_detail, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
	settingMax_Debris = autoSettingsAddIntSlider(GRAPHICS_CATEGORY, "Max_Debris", 0, 200, &gcl_gfx_settings.maxDebris, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, NULL, true);

	options = NULL;
	eaPush(&options, "Low");
	eaPush(&options, "High");
	settingLighting_Quality = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Lighting_Quality", &options, true, &lighting_quality, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	settingMax_Lights_Per_Object = autoSettingsAddIntSlider(GRAPHICS_CATEGORY, "Max_Lights_Per_Object", gfx_lighting_options.max_static_lights_per_object, rdrMaxSupportedObjectLights(), &gcl_gfx_settings.maxLightsPerObject, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, NULL, true);
	settingMax_Shadowed_Lights = autoSettingsAddIntSlider(GRAPHICS_CATEGORY, "Max_Shadowed_Lights", 1, gfx_lighting_options.max_shadowed_lights_per_frame, &gcl_gfx_settings.maxShadowedLights, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, NULL, true);

	settingTexture_Anisotropic_Filtering = autoSettingsAddIntSlider(GRAPHICS_CATEGORY, "Texture_Anisotropic_Filtering", 0, 4, &texAniso, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, displayAnisoFilter, inputAnisoFilter, true);

	if (gConf.bEnableShaderQualitySlider)
		settingMaterialDesiredScore = autoSettingsAddIntSlider(GRAPHICS_CATEGORY, "Material_Quality_Maximum", 0, SGRAPH_QUALITY_MAX_VALUE, &matDesiredScore, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, displayMatDesiredScore, inputMatDesiredScore, true);

	if (isDevelopmentMode()) {
		settingPreload_Materials = autoSettingsAddBit(GRAPHICS_CATEGORY, "Preload_Materials", (int*)&gcl_gfx_settings.preload_materials, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
	}
	settingFrameRateStabilizer = autoSettingsAddBit(GRAPHICS_CATEGORY, "FrameRateStabilizer", (int*)&gcl_gfx_settings.frameRateStabilizer, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
	settingAutoEnableFrameRateStabilizer = autoSettingsAddBit(GRAPHICS_CATEGORY, "AutoEnableFrameRateStabilizer", (int*)&gcl_gfx_settings.autoEnableFrameRateStabilizer, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	settingMax_Inactive_FPS = autoSettingsAddBit(GRAPHICS_CATEGORY, "Max_Inactive_FPS", &limitInactiveFPS, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	settingRender_Thread = autoSettingsAddBit(GRAPHICS_CATEGORY, "Render_Thread", (int*)&gcl_gfx_settings.renderThread, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, getNumVirtualCpus()>1);
	OptionSettingSetDisabledTooltip(settingRender_Thread, "Options_Setting_Graphics_Disabled_Tooltip_CPU");


	if (gfxFeatureDesired(GFEATURE_SHADOWS))
	{
		options = NULL;
		eaPush(&options, "Off");
		eaPush(&options, "Simple");
		if (gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_SHADOWS))
		{
			eaPush(&options, "Shadowmaps");
			if (gfxGetShadowBufferEnabled()) // low-end with shadowBuffer off won't see anything with this option
				eaPush(&options, "Soft Shadowmaps");
		}
		settingShadows = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Shadows", &options, true, &shadow_type, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
	}
	if (gfxFeatureDesired(GFEATURE_POSTPROCESSING))
	{
		settingPostprocessing = autoSettingsAddBit(GRAPHICS_CATEGORY, "Postprocessing", (int*)&gcl_gfx_settings.features_desired, GFEATURE_POSTPROCESSING, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_POSTPROCESSING));
		options = NULL;

		eaPush(&options, "Off");

		if(gfx_lighting_options.disableHighBloomQuality) {
			eaPush(&options, "On");
		} else {
			eaPush(&options, "Low");
			if(gcl_gfx_settings.maxBloomQuality >= 2) {
				eaPush(&options, "High");
			}
		}

		settingBloom_Quality = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Bloom_Quality", &options, true, &gcl_gfx_settings.bloomQuality, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_POSTPROCESSING));
		settingBloom_Intensity = autoSettingsAddFloatSliderEx(
			GRAPHICS_CATEGORY, "Bloom_Intensity", 0,
			gfx_lighting_options.disableHighBloomIntensity ? 1.0f : 5.0f,
			0.1f, &gcl_gfx_settings.bloomIntensity, gfxSettingsChangedCallback,
			gfxSettingsCommittedCallback, gfxSettingsUpdateCallback,
			gfxSettingsRestoreDefaultsCallback, displayFloatAsPercentage,
			inputFloatPercentage,
			gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_POSTPROCESSING));

		{
			bool allowSSAO = (systemSpecsMaterialSupportedFeatures() & SGFEAT_SM30_PLUS) && gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_POSTPROCESSING) && gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_SHADOWS);
			settingScreen_Space_Ambient_Occlusion =  autoSettingsAddBit(GRAPHICS_CATEGORY, "Screen_Space_Ambient_Occlusion", &gcl_gfx_settings.ssao, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, allowSSAO);
		}
		OptionSettingSetDisabledTooltip(settingPostprocessing, "Options_Setting_Graphics_Disabled_Tooltip_GPU");
		OptionSettingSetDisabledTooltip(settingBloom_Quality, "Options_Setting_Graphics_Disabled_Tooltip_GPU");
		OptionSettingSetDisabledTooltip(settingBloom_Intensity, "Options_Setting_Graphics_Disabled_Tooltip_GPU");
		OptionSettingSetDisabledTooltip(settingScreen_Space_Ambient_Occlusion, "Options_Setting_Graphics_Disabled_Tooltip_GPU");
	}
	if (gfxFeatureDesired(GFEATURE_OUTLINING))
	{
		settingOutlining = autoSettingsAddBit(GRAPHICS_CATEGORY, "Outlining", (int*)&gcl_gfx_settings.features_desired, GFEATURE_OUTLINING, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_OUTLINING));
		OptionSettingSetDisabledTooltip(settingOutlining, "Options_Setting_Graphics_Disabled_Tooltip_GPU");
	}
	if (gfxFeatureDesired(GFEATURE_DOF))
	{
		settingDepth_of_Field = autoSettingsAddBit(GRAPHICS_CATEGORY, "Depth_of_Field", (int*)&gcl_gfx_settings.features_desired, GFEATURE_DOF, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_DOF));
		OptionSettingSetDisabledTooltip(settingDepth_of_Field, "Options_Setting_Graphics_Disabled_Tooltip_GPU");
	}
	if (gfxFeatureDesired(GFEATURE_WATER))
	{
		settingWater = autoSettingsAddBit(GRAPHICS_CATEGORY, "Water", (int*)&gcl_gfx_settings.features_desired, GFEATURE_WATER, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_WATER));
		OptionSettingSetDisabledTooltip(settingWater, "Options_Setting_Graphics_Disabled_Tooltip_GPU");
	}
	options = NULL;
	eaPush(&options, "Off");
	eaPush(&options, "Low");
	if (gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_POSTPROCESSING))
	{
		eaPush(&options, "High");
	} else
		MIN1(gcl_gfx_settings.lensflare_quality, 1);
	autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Lensflare_Quality", &options, true, &gcl_gfx_settings.lensflare_quality, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	options = NULL;
	eaPush(&options, "Low");
	eaPush(&options, "Medium");
	eaPush(&options, "High");
	settingFX_Quality = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "FX_Quality", &options, true, &gcl_gfx_settings.fxQuality, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
	if (gfxFeatureDesired(GFEATURE_POSTPROCESSING))
	{
		settingSoft_Particles = autoSettingsAddBit(GRAPHICS_CATEGORY, "Soft_Particles", &gcl_gfx_settings.soft_particles, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_POSTPROCESSING));
		OptionSettingSetDisabledTooltip(settingSoft_Particles, "Options_Setting_Graphics_Disabled_Tooltip_GPU");
	}
	if (isDevelopmentMode())
	{
		if (gfxFeatureDesired(GFEATURE_SCATTERING))
		{
			options = NULL;
			eaPush(&options, "Off");
			eaPush(&options, "Medium");
			eaPush(&options, "High");
			settingScattering_Quality = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Scattering_Quality", &options, true, &gcl_gfx_settings.scattering, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, gfxSettingsFeatureSupported(&gcl_gfx_settings, GFEATURE_SCATTERING));
			OptionSettingSetDisabledTooltip(settingScattering_Quality, "Options_Setting_Graphics_Disabled_Tooltip_GPU");
		}
	}

	options = NULL;
	eaPush(&options, "None");
	eaPush(&options, "Simple");
	eaPush(&options, "Cubemaps");
	settingMax_Reflection = autoSettingsAddComboBox(GRAPHICS_CATEGORY, "Max_Reflection", &options, true, &gcl_gfx_settings.maxReflection, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	autoSettingsAddBit(GRAPHICS_CATEGORY, "HigherSettingsInTailor", &gcl_gfx_settings.higherSettingsInTailor, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	settingUseFullSkinning = autoSettingsAddBit(GRAPHICS_CATEGORY, "UseFullSkinning", &gcl_gfx_settings.useFullSkinning, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
	settingGPUParticles = autoSettingsAddBit(GRAPHICS_CATEGORY, "GPUParticles", &gcl_gfx_settings.gpuAcceleratedParticles, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, gfxSupportsVfetch());
	OptionSettingSetDisabledTooltip(settingGPUParticles, "Options_Setting_Graphics_Disabled_Tooltip_GPU");

	settingReduceFileStreaming = autoSettingsAddBit(GRAPHICS_CATEGORY, "ReduceFileStreaming", &gcl_gfx_settings.reduceFileStreaming, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	autoSettingsAddBit(GRAPHICS_CATEGORY, "SoftwareCursor", &gcl_gfx_settings.softwareCursor, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);
	
	autoSettingsAddBit(GRAPHICS_CATEGORY, "MinDepthBiasFix", &gcl_gfx_settings.minDepthBiasFix, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	autoSettingsAddBit(GRAPHICS_CATEGORY, "PerFrameSleep", &doPerFrameSleep, 1, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	options = NULL;
	for (i=0; i<ARRAY_SIZE(maxFPS_options); i++)
	{
		char buf[6];
		sprintf(buf, "%d", maxFPS_options[i]);
		eaPush(&options, allocAddString(buf));
	}
	autoSettingsAddComboBox(GRAPHICS_CATEGORY, "MaxFPS", &options, true, &maxFPS_index, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	options = NULL;
	for (i=0; i<ARRAY_SIZE(videoMemoryMax_options); i++)
	{
		char buf[6];
		sprintf(buf, "%d", videoMemoryMax_options[i]);
		eaPush(&options, allocAddString(buf));
	}
	autoSettingsAddComboBox(GRAPHICS_CATEGORY, "VideoMemoryMax", &options, true, &videoMemoryMax_index, gfxSettingsChangedCallback, gfxSettingsCommittedCallback, gfxSettingsUpdateCallback, gfxSettingsRestoreDefaultsCallback, NULL, true);

	// Make sure everything is properly shown/hidden
	gfxSettingsUpdateHiddenChoices();
}

#include "AutoGen/gclGraphicsOptions_c_ast.c"
