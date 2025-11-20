#include "GfxSky.h"

#include "FolderCache.h"
#include "fileutil.h"
#include "rgb_hsv.h"
#include "structHist.h"
#include "ScratchStack.h"
#include "strings_opt.h"
#include "qsortG.h"
#include "utilitiesLib.h"
#include "Quat.h"
#include "gimmeDLLWrapper.h"

#include "WorldLib.h"
#include "dynWind.h"
#include "MemoryPool.h"

#include "GfxMaterialProfile.h"
#include "GfxWorld.h"
#include "GfxMaterials.h"
#include "GfxTexturesInline.h"
#include "GfxStarField.h"
#include "GfxLights.h"
#include "GfxGeo.h"
#include "GfxCommandParse.h"
#include "Materials.h"

#include "AutoGen/GfxSky_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

MP_DEFINE(BlendedSkyDome);

// this struct is used to track sky groups on a sky data;
// the sky group is not used directly because multiple sky datas
// can have the same sky group active at once.
typedef struct SkyInfoGroupInstantiated
{
	SkyInfoGroup *sky_group;
	U32 priority;						// Higher priority groups get blended over top of lower priority ones
	F32 percent;						// Visibility Percentage
	F32 desired_percent;				// Desired visibility percentage. Setting this value to 0 will eventually remove the SkyInfoGroup
	F32 fade_rate;						// (0.0 - 1.0) The percent at which to move towards the desired_percent per frame
	bool delete_on_fade;				// If true then when the desired_percent and the percent are both 0 then it will be deleted and removed
} SkyInfoGroupInstantiated;

//Dictionary for all the Skies
static DictionaryHandle gfx_sky_dict;

//Used to send a reload message to all GfxSkyData
//Increment to send a reload message
static int sky_needs_reload=1;

//Flag set to true when we are reloading skies so that we don't set of a second reload while already reloading.
static bool reloading_all_skies = false;

//All SkyInfo sub-structures should start with a F32 that corresponds to a time of day
//So we casting to a F32 to grab the time property of the struct
#define GET_SKY_TIME(x) (**((F32**)(x)))

//Name of the sky used for filling in defaults
#define DEFAULT_SKY "default_sky"

//This is a list of struct parser columns for various SkyInfo variables 
//They are filled with an AUTO_RUN function called gfxSkyInitSkyColumnVars()
//This allows us to call parser functions on these SkyInfo variables without spending the time finding the column
static int tonemap_blueshift_hsv_column, tonemap_usedfield_column;
static int fog_highFogColorHSV_column, fog_highFogDist_column, fog_highFogMax_column, fog_fogHeight_column, fog_lowFogColorHSV_column, fog_lowFogDist_column, fog_lowFogMax_column, fog_clipFogColorHSV_column, fog_clipBackgroundColorHSV_column, fog_usedfield_column;
static int star_field_color_min_column, star_field_color_max_column, star_field_slice_axis_column, star_field_usedfield_column;
static int dof_nearDist_column, dof_nearValue_column, dof_focusDist_column, dof_focusValue_column, dof_farDist_column, dof_farValue_column, dof_usedfield_column;
static int dof_depthadjust_fg_column, dof_depthadjust_bg_column, dof_depthadjust_sky_column;
static int bloom_low_start_column, bloom_low_multi_column, bloom_low_pow_column, bloom_usedfield_column;
static int sky_dome_time_alpha_column, sky_dome_time_scale_column, sky_dome_time_tint_hsv_column, sky_dome_time_ambient_hsv_column, sky_dome_time_pos_column, sky_dome_time_usedfield_column;
static int sky_dome_rot_axis_column, sky_dome_atmosphere_sun_hsv_column, sky_dome_usedfield_column;

static GfxSkyData **all_sky_datas;

static const char *lensFlareOcclusionTextureName = NULL;

//Fill our list of struct parser columns
AUTO_RUN;
void gfxSkyInitSkyColumnVars(void)
{
	assert(ParserFindColumn(parse_SkyTimeLightBehavior, "BlueshiftHSV",               &tonemap_blueshift_hsv_column));
	assert(ParserFindColumn(parse_SkyTimeLightBehavior, "bfParamsSpecified",          &tonemap_usedfield_column));

	assert(ParserFindColumn(parse_SkyTimeFog,         "HighFogColorHSV",              &fog_highFogColorHSV_column));
	assert(ParserFindColumn(parse_SkyTimeFog,         "HighFogDist",                  &fog_highFogDist_column));
	assert(ParserFindColumn(parse_SkyTimeFog,         "HighFogMax",                   &fog_highFogMax_column));
	assert(ParserFindColumn(parse_SkyTimeFog,         "FogHeight",                    &fog_fogHeight_column));
	assert(ParserFindColumn(parse_SkyTimeFog,		  "FogColorHSV",				  &fog_lowFogColorHSV_column));
	assert(ParserFindColumn(parse_SkyTimeFog,		  "ClipFogColorHSV",			  &fog_clipFogColorHSV_column));
	assert(ParserFindColumn(parse_SkyTimeFog,		  "ClipBackgroundColorHSV",		  &fog_clipBackgroundColorHSV_column));
	assert(ParserFindColumn(parse_SkyTimeFog,		  "FogDist",					  &fog_lowFogDist_column));
	assert(ParserFindColumn(parse_SkyTimeFog,		  "FogMax",						  &fog_lowFogMax_column));
	assert(ParserFindColumn(parse_SkyTimeFog,         "bfParamsSpecified",            &fog_usedfield_column));

	assert(ParserFindColumn(parse_StarField,          "StarColorHSVMin",              &star_field_color_min_column));
	assert(ParserFindColumn(parse_StarField,          "StarColorHSVMax",              &star_field_color_max_column));
	assert(ParserFindColumn(parse_StarField,          "SliceAxis",                    &star_field_slice_axis_column));
	assert(ParserFindColumn(parse_StarField,          "bfParamsSpecified",            &star_field_usedfield_column));

	assert(ParserFindColumn(parse_DOFValues,          "nearDist",                     &dof_nearDist_column));
	assert(ParserFindColumn(parse_DOFValues,          "nearValue",                    &dof_nearValue_column));
	assert(ParserFindColumn(parse_DOFValues,          "focusDist",                    &dof_focusDist_column));
	assert(ParserFindColumn(parse_DOFValues,          "focusValue",                   &dof_focusValue_column));
	assert(ParserFindColumn(parse_DOFValues,          "farDist",                      &dof_farDist_column));
	assert(ParserFindColumn(parse_DOFValues,          "farValue",                     &dof_farValue_column));
	assert(ParserFindColumn(parse_DOFValues,          "depthAdjustFgHSV",             &dof_depthadjust_fg_column));
	assert(ParserFindColumn(parse_DOFValues,          "depthAdjustBgHSV",             &dof_depthadjust_bg_column));
	assert(ParserFindColumn(parse_DOFValues,          "depthAdjustSkyHSV",            &dof_depthadjust_sky_column));
	assert(ParserFindColumn(parse_DOFValues,          "bfParamsSpecified",            &dof_usedfield_column));

	assert(ParserFindColumn(parse_SkyTimeBloom,       "LowQualityBloomStart",         &bloom_low_start_column));
	assert(ParserFindColumn(parse_SkyTimeBloom,       "LowQualityBloomMultiplier",    &bloom_low_multi_column));
	assert(ParserFindColumn(parse_SkyTimeBloom,       "LowQualityBloomPower",         &bloom_low_pow_column));
	assert(ParserFindColumn(parse_SkyTimeBloom,       "bfParamsSpecified",            &bloom_usedfield_column));

	assert(ParserFindColumn(parse_SkyDomeTime,        "Alpha",                        &sky_dome_time_alpha_column));
	assert(ParserFindColumn(parse_SkyDomeTime,        "Scale",                        &sky_dome_time_scale_column));
	assert(ParserFindColumn(parse_SkyDomeTime,        "TintHSV",                      &sky_dome_time_tint_hsv_column));
	assert(ParserFindColumn(parse_SkyDomeTime,        "AmbientHSV",                   &sky_dome_time_ambient_hsv_column));
	assert(ParserFindColumn(parse_SkyDomeTime,        "Position",                     &sky_dome_time_pos_column));
	assert(ParserFindColumn(parse_SkyDomeTime,        "bfParamsSpecified",            &sky_dome_time_usedfield_column));

	assert(ParserFindColumn(parse_SkyDome,            "RotationAxis",                 &sky_dome_rot_axis_column));
	assert(ParserFindColumn(parse_SkyDome,            "AtmosphereSunHSV",             &sky_dome_atmosphere_sun_hsv_column));
	assert(ParserFindColumn(parse_SkyDome,            "bfParamsSpecified",            &sky_dome_usedfield_column));
}

AUTO_RUN;
void gfxInitLensFlareData(void) {
	lensFlareOcclusionTextureName = allocAddStaticString("Texture2");
}

//////////////////////////////////////////////////////////////////////////
//	General Helper Functions
//////////////////////////////////////////////////////////////////////////

//Do an operation on all the common values within a SkyInfo.  Possibly interacts with a BlendedSkyInfo.
void gfxSkyDoOperationOnSky(gfxSkyOpOnSkyFunc sky_function, SkyInfo *sky, BlendedSkyInfo *blended_sky, void *user_data)
{
	sky_function(parse_SkyTimeSun,					SKY_SUN_VALUES,				sky, blended_sky, sky->sunValues,			&blended_sky->sunValues,			user_data);
	sky_function(parse_SkyTimeSecondarySun,			SKY_SCND_SUN_VALUES,		sky, blended_sky, sky->secSunValues,		&blended_sky->secSunValues,			user_data);
	sky_function(parse_SkyTimeCloudShadows,			SKY_CLOUD_SHADOW_VALUES,	sky, blended_sky, sky->cloudShadowValues,	&blended_sky->cloudShadowValues,	user_data);
	sky_function(parse_SkyTimeShadowFade,			SKY_SHADOW_FADE_VALUES,		sky, blended_sky, sky->shadowFadeValues,	&blended_sky->shadowFadeValues,		user_data);
	sky_function(parse_SkyTimeTint,					SKY_TINT_VALUES,			sky, blended_sky, sky->tintValues,			&blended_sky->tintValues,			user_data);
	sky_function(parse_SkyTimeOutline,				SKY_OUTLINE_VALUES,			sky, blended_sky, sky->outlineValues,		&blended_sky->outlineValues,		user_data);
	sky_function(parse_SkyTimeFog,					SKY_FOG_VALUES,				sky, blended_sky, sky->fogValues,			&blended_sky->fogValues,			user_data);
	sky_function(parse_SkyTimeLightBehavior,		SKY_TONE_MAPPING_VALUES,	sky, blended_sky, sky->lightBehaviorValues,	&blended_sky->lightBehaviorValues,	user_data);
	sky_function(parse_SkyTimeBloom,				SKY_BLOOM_VALUES,			sky, blended_sky, sky->bloomValues,			&blended_sky->bloomValues,			user_data);
	sky_function(parse_DOFValues,					SKY_DOF_VALUES,				sky, blended_sky, sky->dofValues,			&blended_sky->dofValues,			user_data);
	sky_function(parse_SkyTimeAmbientOcclusion,		SKY_OCCLUSION_VALUES,		sky, blended_sky, sky->occlusionValues,		&blended_sky->occlusionValues,		user_data);
	sky_function(parse_SkyTimeCharacterLighting,	SKY_CHAR_LIGHT_VALUES,		sky, blended_sky, sky->charLightingValues,	&blended_sky->charLightingValues,	user_data);
	sky_function(parse_SkyTimeColorCorrection,		SKY_COLOR_CORRECTION_VALUES,sky, blended_sky, sky->colorCorrectionValues,&blended_sky->colorCorrectionValues,user_data);
	sky_function(parse_SkyTimeWind,					SKY_WIND_VALUES,			sky, blended_sky, sky->windValues,			&blended_sky->windValues,			user_data);
	sky_function(parse_SkyTimeScattering,			SKY_SCATTERING_VALUES,		sky, blended_sky, sky->scatteringValues,	&blended_sky->scatteringValues,		user_data);
	sky_function(parse_ShadowRules,					SKY_SHADOW_VALUES,			sky, blended_sky, sky->shadowValues,		&blended_sky->shadowValues,			user_data);
}

//Do an operation on all the common values within a BlendedSkyInfo.  Possibly interacts with another BlendedSkyInfo.
void gfxSkyDoOperationOnBlendedSky(gfxSkyOpOnBlendedSkyFunc sky_function, BlendedSkyInfo *sky_1, BlendedSkyInfo *sky_2, void *user_data)
{
	sky_function(parse_SkyTimeSun,					SKY_SUN_VALUES,				sky_1, sky_2, &sky_1->sunValues,			&sky_2->sunValues,			user_data);
	sky_function(parse_SkyTimeSecondarySun,			SKY_SCND_SUN_VALUES,		sky_1, sky_2, &sky_1->secSunValues,			&sky_2->secSunValues,		user_data);
	sky_function(parse_SkyTimeCloudShadows,			SKY_CLOUD_SHADOW_VALUES,	sky_1, sky_2, &sky_1->cloudShadowValues,	&sky_2->cloudShadowValues,	user_data);
	sky_function(parse_SkyTimeShadowFade,			SKY_SHADOW_FADE_VALUES,		sky_1, sky_2, &sky_1->shadowFadeValues,		&sky_2->shadowFadeValues,	user_data);
	sky_function(parse_SkyTimeTint,					SKY_TINT_VALUES,			sky_1, sky_2, &sky_1->tintValues,			&sky_2->tintValues,			user_data);
	sky_function(parse_SkyTimeOutline,				SKY_OUTLINE_VALUES,			sky_1, sky_2, &sky_1->outlineValues,		&sky_2->outlineValues,		user_data);
	sky_function(parse_SkyTimeFog,					SKY_FOG_VALUES,				sky_1, sky_2, &sky_1->fogValues,			&sky_2->fogValues,			user_data);
	sky_function(parse_SkyTimeLightBehavior,		SKY_TONE_MAPPING_VALUES,	sky_1, sky_2, &sky_1->lightBehaviorValues,	&sky_2->lightBehaviorValues,user_data);
	sky_function(parse_SkyTimeBloom,				SKY_BLOOM_VALUES,			sky_1, sky_2, &sky_1->bloomValues,			&sky_2->bloomValues,		user_data);
	sky_function(parse_DOFValues,					SKY_DOF_VALUES,				sky_1, sky_2, &sky_1->dofValues,			&sky_2->dofValues,			user_data);
	sky_function(parse_SkyTimeAmbientOcclusion,		SKY_OCCLUSION_VALUES,		sky_1, sky_2, &sky_1->occlusionValues,		&sky_2->occlusionValues,	user_data);
	sky_function(parse_SkyTimeCharacterLighting,	SKY_CHAR_LIGHT_VALUES,		sky_1, sky_2, &sky_1->charLightingValues,	&sky_2->charLightingValues,	user_data);
	sky_function(parse_SkyTimeColorCorrection,		SKY_COLOR_CORRECTION_VALUES,sky_1, sky_2, &sky_1->colorCorrectionValues,&sky_2->colorCorrectionValues,user_data);
	sky_function(parse_SkyTimeWind,					SKY_WIND_VALUES,			sky_1, sky_2, &sky_1->windValues,			&sky_2->windValues,			user_data);
	sky_function(parse_SkyTimeScattering,			SKY_SCATTERING_VALUES,		sky_1, sky_2, &sky_1->scatteringValues,		&sky_2->scatteringValues,	user_data);
	sky_function(parse_ShadowRules,					SKY_SHADOW_VALUES,			sky_1, sky_2, &sky_1->shadowValues,			&sky_2->shadowValues,		user_data);
}

static bool gfxSkyUseFallback()
{
	if(!gfxDoingPostprocessing())
		return true;
	if(!(systemSpecsMaterialSupportedFeatures() & (SGFEAT_SM30 | SGFEAT_SM30_PLUS | SGFEAT_SM30_HYPER)))
		return true;
	return false;
}

//Ensure that a given time is between 0 and 24
static F32 gfxSkyRepairTime(F32 a)
{
	while(a >= 24.f)
		a -= 24.f;
	while(a < 0)
		a += 24.f;
	return a;
}

//Compare two materials for sorting purpose
//Don't really care what order just as long as it is same while fixing up the sky domes
static int gfxSkyCompareMaterials(const MaterialNamedConstant **mat_prop1, const MaterialNamedConstant **mat_prop2)
{
	if( (**mat_prop1).name == (**mat_prop2).name )
		return 0;
	if( (**mat_prop1).name > (**mat_prop2).name )
		return 1;
	return -1;
}

//Return a SkyInfo from a name
//WARNING: Keep in mind that the pointer returned is from a dictionary and you should not hold onto this pointer for too long
SkyInfo *gfxSkyFindSky(const char *sky_filename)
{
	SkyInfo *sky;
	char sky_name[256];

	if(!sky_filename)
		return NULL;

	getFileNameNoExt(sky_name, sky_filename);
	sky = RefSystem_ReferentFromString(gfx_sky_dict, sky_name);
	return sky;	
}

bool gfxSkyCheckSkyExists(const char *sky_filename)
{
	return !!gfxSkyFindSky(sky_filename);
}

//Return the sky currently being drawn to the screen
const BlendedSkyInfo* gfxSkyGetVisibleSky(const GfxSkyData *sky_data)
{
	return sky_data->visible_sky;
}

//////////////////////////////////////////////////////////////////////////
//	Debug Code
//////////////////////////////////////////////////////////////////////////

char *sky_debug_text=NULL;

//Returns the text description of the current sky.
char *gfxSkyGetDebugText()
{
	return sky_debug_text;
}

//Turns Debug flag on or off
void gfxSkySetFillingDebugTextFlag(bool val)
{
	gfx_state.debug.filling_sky_debug_text = val;
}

//Print out debug info
void gfxSkyFillDebugText(GfxSkyData *sky_data)
{
	int i, j;
	static char *sky_text=NULL;

	if(!gfx_state.debug.filling_sky_debug_text)
		return;

	PERFINFO_AUTO_START_FUNC();

	estrClear(&sky_debug_text);
	estrPrintf(&sky_debug_text, "");

	//Draw SkyInfoGroups
	for( i=0; i < eaSize(&sky_data->active_sky_groups); i++ )
	{
		SkyInfoGroupInstantiated *active_sky_group = sky_data->active_sky_groups[i];
		SkyInfoGroup *sky_group = active_sky_group->sky_group;
		estrPrintf(&sky_text, "  Sky Group: (%3.0f%%)\n", 100*active_sky_group->percent);
		estrConcatString(&sky_debug_text, sky_text, estrLength(&sky_text));
		//Draw Overrides
		for( j=0; j < eaSize(&sky_group->override_list); j++ )
		{
			SkyInfo *sky = GET_REF(sky_group->override_list[j]->sky);
			if(sky)
			{
				SkyInfo *sky_fallback;
				if(gfxSkyUseFallback() && (sky_fallback = GET_REF(sky->noPPFallback))) {
					estrPrintf(&sky_text, "      Sky File: %s (Falling back to: %s)\n", sky->filename, sky_fallback->filename);
				} else {
					estrPrintf(&sky_text, "      Sky File: %s\n", sky->filename);
				}
				estrConcatString(&sky_debug_text, sky_text, estrLength(&sky_text));
			}
		}
	}

	//Draw Time
	estrPrintf(&sky_text, "  Time: %1.3f\n", sky_data->time);
	estrConcatString(&sky_debug_text, sky_text, estrLength(&sky_text));

	//Draw Light Dir
	estrPrintf(&sky_text, "  Light Dir: %1.3f,%1.3f,%1.3f\n", 
							sky_data->visible_sky->outputWorldLightDir[0],
							sky_data->visible_sky->outputWorldLightDir[1],
							sky_data->visible_sky->outputWorldLightDir[2]);
	estrConcatString(&sky_debug_text, sky_text, estrLength(&sky_text));

	//Draw Light 2 Dir
	estrPrintf(&sky_text, "  Light 2 Dir: %1.3f,%1.3f,%1.3f\n", 
		sky_data->visible_sky->outputWorldLightDir2[0],
		sky_data->visible_sky->outputWorldLightDir2[1],
		sky_data->visible_sky->outputWorldLightDir2[2]);
	estrConcatString(&sky_debug_text, sky_text, estrLength(&sky_text));

	//Draw Visible Sky Data
	estrClear(&sky_text);
	ParserWriteText(&sky_text, parse_BlendedSkyInfo, sky_data->visible_sky, 0, 0, 0);
	for( i=0; i < (int)estrLength(&sky_text) ; i++ )
	{
		if(sky_text[i] == '\t')
		{
			sky_text[i] = ' ';
			estrInsert(&sky_text, i, "   ", 3);
		}
	}
	estrConcatString(&sky_debug_text, sky_text, estrLength(&sky_text));

	//Draw Sky Dome Data
	for( i=0; i < sky_data->visible_sky->skyDomeCount; i++ )
	{
		estrPrintf(&sky_text, "\n%s:\n    Alpha(%g)\n    Sort Order(%g)\n    High Detail(%d)\n    Draw Percent(%g)\n    Scale(%g)\n    Angle(%g)\n    Ambient(%.2f, %.2f, %.2f)\n    Tint(%.2f, %.2f, %.2f)\n    Position(%.2f, %.2f, %.2f)\n", 
			sky_data->visible_sky->skyDomes[i]->dome->name, 
			sky_data->visible_sky->skyDomes[i]->alpha,
			sky_data->visible_sky->skyDomes[i]->sort_order,
			sky_data->visible_sky->skyDomes[i]->high_detail, 
			sky_data->visible_sky->skyDomes[i]->group_percent,
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.scale,
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.angle,
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.ambientHSV[0],
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.ambientHSV[1],
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.ambientHSV[2],
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.tintHSV[0],
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.tintHSV[1],
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.tintHSV[2],
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.pos[0],
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.pos[1],
			sky_data->visible_sky->skyDomes[i]->dome->current_dome_values.pos[2]);
		estrConcatString(&sky_debug_text, sky_text, estrLength(&sky_text));
	}
	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////
//	Constructors and Destructors
//////////////////////////////////////////////////////////////////////////

//Destroy a MaterialNamedConstant
void gfxSkyDestroyMaterialNamedConstant(MaterialNamedConstant *mat_prop)
{
	StructDestroy(parse_MaterialNamedConstant, mat_prop);
}

//Create a SkyDrawable
static SkyDrawable *gfxSkyCreateDrawable(const char *name, StarField *star_field, WorldAtmosphereProperties *atmosphere)
{
	SkyDrawable	*node;
	Model		*model = NULL;
	Material	*material = NULL;
	WLUsageFlags use_flags = WL_FOR_UTIL|WL_FOR_WORLD; // Not just FOR_WORLD because those get unloaded at map unload...

	if (star_field)
	{
		material = materialFind(name, use_flags);
		assert(material);
	}
	else if (atmosphere)
	{
		model = groupModelFind("_Sky_Atmosphere", use_flags);
	}
	else
	{
		model = groupModelFind(name, use_flags);
		assert(model);// Data inconsistency (corrupt .bins?), we say it's in this geo file, but this geo file doesn't have it.
	}

	node = calloc(1, sizeof(*node));
	node->model = model;
	node->material = material;
	node->star_field = star_field;
	node->atmosphere = atmosphere;
	node->geo_render_info = NULL;
	node->star_data = NULL;

	copyMat4(unitmat, node->mat);

	return node;
}

//Destroy a SkyDrawable
void gfxSkyDestroyDrawable(SkyDrawable *sky_drawable)
{
	gfxFreeGeoRenderInfo(sky_drawable->geo_render_info, false);
	eaDestroyEx(&sky_drawable->star_data, NULL);
	free(sky_drawable);
}

//Create a BlendedSkyDome
BlendedSkyDome* gfxSkyCreateBlendedSkyDome(SkyDome *sky_dome, F32 sort_order, bool high_detail, F32 percent, bool as_refrence)
{
	BlendedSkyDome *blended_sky_dome;
	SkyDomeTime **values_backup;

	if(!sky_dome)
		return NULL;

	MP_CREATE(BlendedSkyDome, 32);
	blended_sky_dome = MP_ALLOC(BlendedSkyDome);
	blended_sky_dome->is_refrence = as_refrence;
	if(as_refrence)
	{
		blended_sky_dome->dome = sky_dome;
	}
	else
	{
		blended_sky_dome->dome = StructCreate(parse_SkyDome);
		//We never want to copy all the values over, just the current time's one
		values_backup = sky_dome->dome_values;
		sky_dome->dome_values = NULL;
		StructCopyAll(parse_SkyDome, sky_dome, blended_sky_dome->dome);
		sky_dome->dome_values = values_backup;
	}
	blended_sky_dome->alpha = sky_dome->current_dome_values.alpha;
	blended_sky_dome->sort_order = sort_order;
	blended_sky_dome->high_detail = high_detail;
	blended_sky_dome->group_percent = percent;

	return blended_sky_dome;
}

//Destroy a BlendedSkyDome
void gfxSkyDestroyBlendedSkyDome(BlendedSkyDome *blended_sky_dome)
{
	if(blended_sky_dome)
	{
		if(blended_sky_dome->drawable)
			gfxSkyDestroyDrawable(blended_sky_dome->drawable);

		if(!blended_sky_dome->is_refrence)
			StructDestroy(parse_SkyDome, blended_sky_dome->dome);

		MP_FREE(BlendedSkyDome, blended_sky_dome);
	}
}

//Destroy the sky domes on a blended sky info
void gfxSkyDestroyBlendedSkyInfoDomes(BlendedSkyInfo *blended_sky_info)
{
	int i;
	for( i=0; i < blended_sky_info->skyDomeCount ; i++ )
	{
		gfxSkyDestroyBlendedSkyDome(blended_sky_info->skyDomes[i]);
	}
	blended_sky_info->skyDomeCount = 0;
}

//Destroy a BlendedSkyInfo
void gfxSkyDestroyBlendedSkyInfo(BlendedSkyInfo *blended_sky_info)
{
	if(blended_sky_info)
	{
		gfxSkyDestroyBlendedSkyInfoDomes(blended_sky_info);
		StructDestroy(parse_BlendedSkyInfo, blended_sky_info);
	}
}

//Create a GfxSkyData
GfxSkyData *gfxSkyCreateSkyData(void)
{
	GfxSkyData *newSkyData = calloc(1, sizeof(*newSkyData));
	newSkyData->visible_sky = StructCreate(parse_BlendedSkyInfo);
	eaPushUnique(&all_sky_datas, newSkyData);
	return newSkyData;
}

//Destroy a GfxSkyData
void gfxSkyDestroySkyData(GfxSkyData *sky_data)
{
	if(sky_data)
	{
		gfxSkyDestroyBlendedSkyInfo(sky_data->visible_sky);
		//If we used a custom override then remove from the system
		if(sky_data->custom_dof_sky_name)
		{
			SkyInfo *sky = RefSystem_ReferentFromString(gfx_sky_dict, sky_data->custom_dof_sky_name);
			if(sky)
				RefSystem_RemoveReferent(sky, false);
			StructFreeString(sky_data->custom_dof_sky_name);
		}
		if(sky_data->custom_fog_sky_name)
		{
			SkyInfo *sky = RefSystem_ReferentFromString(gfx_sky_dict, sky_data->custom_fog_sky_name);
			if(sky)
				RefSystem_RemoveReferent(sky, false);
			StructFreeString(sky_data->custom_fog_sky_name);
		}
		eaFindAndRemoveFast(&all_sky_datas, sky_data);
		free(sky_data);
	}
}

//Add an Override to the end of a SkyInfoGroup override list
void gfxSkyGroupAddOverride(SkyInfoGroup* sky_group, const char *new_sky)
{
	SkyInfo *sky_info = gfxSkyFindSky(new_sky);
	if(sky_info)
	{
		SkyInfoOverride *new_override = calloc(1, sizeof(*new_override));
		SET_HANDLE_FROM_REFERENT(gfx_sky_dict, sky_info, new_override->sky);
		eaPush(&sky_group->override_list, new_override);
	}
}

void gfxSkyGroupSetOverride(SkyInfoGroup* sky_group, SkyInfoOverride *sky_override, const char *new_sky)
{
	SkyInfo *sky_info = gfxSkyFindSky(new_sky);
	if (sky_info && sky_override)
	{
		SET_HANDLE_FROM_REFERENT(gfx_sky_dict, sky_info, sky_override->sky);
	}
}

//////////////////////////////////////////////////////////////////////////
//	Public Sky Group Helper Functions
//////////////////////////////////////////////////////////////////////////

void gfxSkyGroupUpdatePositionalFade(GfxSkyData *sky_data,SkyInfoGroup* sky_group, F32 fPercent)
{
	//Find it
	int i;
	for( i=0; i < eaSize(&sky_data->active_sky_groups); i++ )
	{
		if(sky_data->active_sky_groups[i]->sky_group == sky_group)
		{
			sky_data->active_sky_groups[i]->desired_percent = fPercent;
			return;
		}
	}
}

//Adds a Sky Group to the draw list based on priority and Fade Rate
void gfxSkyGroupFadeTo(GfxSkyData *sky_data, SkyInfoGroup* sky_group, F32 start_percent, F32 desired_percent, F32 fade_rate, U32 priority, bool delete_on_fade)
{
	SkyInfoGroupInstantiated *active_sky_group;
	F32 last_percent;
	int i;

	if(!sky_group)
		return;

	last_percent = gfxSkyRemoveSkyGroup(sky_data, sky_group);

	active_sky_group = calloc(1, sizeof(SkyInfoGroupInstantiated));
	active_sky_group->sky_group = sky_group;
	active_sky_group->desired_percent = desired_percent;
	active_sky_group->fade_rate = fade_rate;
	active_sky_group->priority = priority;
	active_sky_group->delete_on_fade = delete_on_fade;
	active_sky_group->percent = start_percent >= 0 ? start_percent : last_percent;

	//Find position based on priority
	for( i=0; i < eaSize(&sky_data->active_sky_groups); i++ )
	{
		if(active_sky_group->priority <= sky_data->active_sky_groups[i]->priority)
			break;
	}
	eaInsert(&sky_data->active_sky_groups, active_sky_group, i);
}

// Removes everything from active_sky_groups on a GfxSkyData.
void gfxSkyClearActiveSkyGroups(GfxSkyData *sky_data) {
	while(eaSize(&(sky_data->active_sky_groups))) {			
		SkyInfoGroupInstantiated *activeGroup = sky_data->active_sky_groups[0];
		gfxSkyRemoveSkyGroup(sky_data, activeGroup->sky_group);
	}
}

//Removes a SkyInfoGroup from the draw list without fading it out
F32 gfxSkyRemoveSkyGroup(GfxSkyData *sky_data, SkyInfoGroup* sky_group)
{
	F32 last_percent = 0;

	if (!sky_group)
		return last_percent;

	if (sky_data)
	{
		FOR_EACH_IN_EARRAY(sky_data->active_sky_groups, SkyInfoGroupInstantiated, active_sky_group)
		{
			if (active_sky_group->sky_group == sky_group)
			{
				eaRemove(&sky_data->active_sky_groups, FOR_EACH_IDX(sky_data->active_sky_groups, active_sky_group));
				MAX1(last_percent, active_sky_group->percent);
				free(active_sky_group);
			}
		}
		FOR_EACH_END;
	}
	else
	{
		FOR_EACH_IN_EARRAY(all_sky_datas, GfxSkyData, data)
		{
			FOR_EACH_IN_EARRAY(data->active_sky_groups, SkyInfoGroupInstantiated, active_sky_group)
			{
				if (active_sky_group->sky_group == sky_group)
				{
					eaRemove(&data->active_sky_groups, FOR_EACH_IDX(data->active_sky_groups, active_sky_group));
					MAX1(last_percent, active_sky_group->percent);
					free(active_sky_group);
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		gfxCameraViewNotifySkyGroupFreed(sky_group);
	}

	return last_percent;
}

void gfxSkyNotifySkyGroupFreed(SkyInfoGroup* sky_group)
{
	gfxSkyRemoveSkyGroup(NULL, sky_group);
}

void gfxSkyGroupFree(SkyInfoGroup *sky_group)
{
	if (!sky_group)
		return;
	gfxSkyNotifySkyGroupFreed(sky_group);
	StructDestroy(parse_SkyInfoGroup, sky_group);
}

bool gfxSkyGroupIsIndoor(SkyInfoGroup *sky_group)
{
	int i, j;
	if(!sky_group)
		return false;
	for( i=0; i < eaSize(&sky_group->override_list); i++ )
	{
		SkyInfo *sky = GET_REF(sky_group->override_list[i]->sky);
		if(sky)
		{
			for( j=0; j < eaSize(&sky->sunValues); j++ )
			{
				if (sky->sunValues[j]->diffuseHSV[2] > 0.01f || 
					sky->sunValues[j]->specularHSV[2] > 0.01f ||
					sky->sunValues[j]->secondaryDiffuseHSV[2] > 0.01f )
					return false;
			}
		}
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
//	Blending Functions
//////////////////////////////////////////////////////////////////////////

//If you think of each structure as a key frame,
//this function finds the key frame right before (sky_early_idx)
//and the frame right after (sky_late_idx) the inputed time (curTime)
void gfxSkyFindValuesSurroundingTime(const void IN **sky_data, int IN array_size, F32 IN curTime, int OUT *sky_early_idx, int OUT *sky_late_idx, F32 OUT *ratio)
{
	int lateSkyInfoIdx=0, earlySkyInfoIdx=0;
	F32 earlyTime=0, lateTime=0;
	int i;

	//Check for Valid Time
	devassert( curTime <= 24.0 && curTime >= 0.0 );

	//If only one object then just use first element for both
	if( array_size == 1 )
	{
		earlySkyInfoIdx = 0;
		lateSkyInfoIdx = 0;
		earlyTime = GET_SKY_TIME(sky_data);
		lateTime = GET_SKY_TIME(sky_data);
	}
	else
	{
		//Loop through the objects
		for( i=0 ; i < array_size; i++ )
		{
			earlySkyInfoIdx = i;
			if( i == (array_size - 1) ) //on the last one, wrap
				lateSkyInfoIdx  = 0;
			else
				lateSkyInfoIdx  = i+1;

			earlyTime = GET_SKY_TIME(sky_data + earlySkyInfoIdx);
			lateTime = GET_SKY_TIME(sky_data + lateSkyInfoIdx);

			if( earlyTime < lateTime )
			{
				if( curTime >= earlyTime && curTime < lateTime )
					break;
			}
			else if( earlyTime > lateTime )
			{
				if( curTime >= earlyTime || curTime < lateTime )
					break;
			}
			else assertmsg( 0, "Bad SkyInfo: duplicate time error" );
		}
		assertmsg( i < array_size, "Your SkyInfo makes no sense" );
	}

	//Get early and late times and the ratio between them
	*sky_early_idx = earlySkyInfoIdx;
	*sky_late_idx = lateSkyInfoIdx;

	//Find ratio
	if( array_size > 1 )
	{
		F32 t_dist, t_len;

		t_dist = gfxSkyRepairTime(curTime - earlyTime); 
		t_len = gfxSkyRepairTime(lateTime - earlyTime);
		if (!t_len)
			*ratio = 0;
		else
			*ratio = t_dist / t_len;
		*ratio = MINMAX( *ratio, 0, 1 );
	}
	else
	{
		*ratio = 1;
	}
}

//Blend the Material Properties of a Sky dome
static void gfxSkyBlendMaterialProperties(MaterialNamedConstant **start, MaterialNamedConstant **end, F32 ratio, MaterialNamedConstant **dest)
{
	int i;

	//Load procedure should ensure similar mat lists
	assert(eaSize(&start) == eaSize(&end) && eaSize(&end) == eaSize(&dest));

	//For each mat
	for(i=0; i < eaSize(&start); i++)
	{
		//Blend Values
		lerpVec4(end[i]->value, ratio, start[i]->value, dest[i]->value);
	}
}

//Blend rotation so that it only ever moves in the positive direction
//Assumes values are between 0 and 360
static F32 gfxSkyBlendRotaionAngle(F32 start, F32 end, F32 ratio)
{
	F32 result;

	if(nearSameF32(start, end))
		return end;

	if(start > end)
		end += 360.0f;

	result = lerp(start, end, ratio);
	if(result > 360.0f)
		result -= 360.0f; 

	return result;
}

//Blend the values of a SkyDome 
void gfxSkyBlendSkyDomeTimes(SkyDomeTime *start, SkyDomeTime *end, F32 ratio, SkyDomeTime *result)
{
	result->time  = lerp(start->time,  end->time,  ratio);
	result->alpha = lerp(start->alpha, end->alpha, ratio);
	result->scale = lerp(start->scale, end->scale, ratio);
	lerpVec3(end->pos, ratio, start->pos, result->pos);
	result->angle = gfxSkyBlendRotaionAngle(start->angle, end->angle, ratio);
	hsvLerp(start->tintHSV, end->tintHSV, ratio, result->tintHSV);
	if (start->ambient_weight && end->ambient_weight)
		hsvLerp(start->ambientHSV, end->ambientHSV, ratio, result->ambientHSV);
	else if (start->ambient_weight)
		copyVec3(start->ambientHSV, result->ambientHSV);
	else if (end->ambient_weight)
		copyVec3(end->ambientHSV, result->ambientHSV);
	else
		setVec3same(result->ambientHSV, 0);
	result->ambient_weight = lerp(start->ambient_weight, end->ambient_weight, ratio);
	gfxSkyBlendMaterialProperties(start->mat_props, end->mat_props, ratio, result->mat_props);
}

//Blend the SkyDomeTime values of a sky dome into the blended container of a sky dome
void gfxSkyDomeBlendToTime(BlendedSkyInfo *blended_sky, SkyDome *sky_dome, SkyDomeTime **sky_values, SkyDomeTime *blended_sky_values, F32 time)
{
	int early_idx;
	int late_idx;
	F32 ratio;

	if(eaSize(&sky_values) == 0)
		return;
	assert(blended_sky_values);

	//Indicate that sky domes are specified
	blended_sky->specified_values |= SKY_DOME_VALUES;

	//Get the early and late time values
	gfxSkyFindValuesSurroundingTime(sky_values, eaSize(&sky_values), time, &early_idx, &late_idx, &ratio);

	//Blend values to current time
	gfxSkyBlendSkyDomeTimes(sky_values[early_idx], sky_values[late_idx], ratio, blended_sky_values);

	//Handle motion loop if specified
	if(sky_dome->motion_loop_cnt > 0)
	{
		F32 loop_time = 24.0f/sky_dome->motion_loop_cnt;
		F32 loop_ratio = fmod(time, loop_time)/loop_time;
		lerpVec3(sky_dome->end_pos, loop_ratio, sky_dome->start_pos, blended_sky_values->pos);
		if(sky_dome->loop_fade_percent > 0.0f)
		{
			if(loop_ratio < sky_dome->loop_fade_percent)
			{
				blended_sky_values->alpha *= (2.0f/sky_dome->loop_fade_percent)*loop_ratio - 1;
			}
			else if((1.0f-loop_ratio) < sky_dome->loop_fade_percent)
			{
				blended_sky_values->alpha *= (2.0f/sky_dome->loop_fade_percent)*(1.0f-loop_ratio) - 1;
			}
		}
	}
}

//Blend the common values of a sky into a blended sky
void gfxSkyValuesBlendToTime(ParseTable pti[], U32 flag, SkyInfo *sky, BlendedSkyInfo *blended_sky, void **sky_values, void *blended_sky_values, F32 *time)
{
	int early_idx;
	int late_idx;
	F32 ratio;

	if(eaSize(&sky_values) == 0)
		return;
	assert(blended_sky_values);

	//Indicate that that these values are specified
	blended_sky->specified_values |= flag;

	//Get the early and late time values
	gfxSkyFindValuesSurroundingTime(sky_values, eaSize(&sky_values), (*time), &early_idx, &late_idx, &ratio);

	//Blend values to current time
	StructCopyVoid(pti, sky_values[early_idx], blended_sky_values, 0, 0, TOK_USEROPTIONBIT_1);
	shDoOperationSetFloat(ratio);
	shDoOperation(STRUCTOP_LERP, pti, blended_sky_values, sky_values[late_idx]);
}

//Blend the values of two blended sky infos together
void gfxSkyValuesBlend(ParseTable pti[], U32 flag, BlendedSkyInfo *sky_1, BlendedSkyInfo *sky_2, void *values_1, void *values_2, F32 *ratio)
{
	//If not specified in sky_2, don't blend
	if(!(sky_2->specified_values & flag))
		return;

	//Indicate in sky_1 that these values are now specified
	sky_1->specified_values |= flag;

	//Blend the values
	shDoOperationSetFloat(*ratio);
	shDoOperation(STRUCTOP_LERP, pti, values_1, values_2);
}

//Blend the Sky to the time and insert into result
void gfxSkyBlendToTime(SkyInfo *sky, BlendedSkyInfo *result, F32 time, F32 sort_depth, F32 percent)
{
	int i;
	if(!sky)
		return;

	//Blend structs to the current time
	gfxSkyDoOperationOnSky(gfxSkyValuesBlendToTime, sky, result, &time);
	
	//Blend general values to the current time
	result->cloudShadowTexture = sky->cloudShadowTexture;
	result->diffuseWarpTextureCharacter = sky->diffuseWarpTextureCharacter;
	result->diffuseWarpTextureWorld = sky->diffuseWarpTextureWorld;
	result->ambientCube = sky->ambientCube;
	result->reflectionCube = sky->reflectionCube;
	result->ignoreFogClipFar = sky->ignoreFogClipFar;
	result->fogClipFar = sky->fogClipFar;
	result->ignoreFogClipLow = sky->ignoreFogClipLow;
	result->fogClipLow = sky->fogClipLow;

	//Blend SkyDomes to the current time
	for( i=0; i < eaSize(&sky->skyDomes); i++ )
	{
		SkyDome *sky_dome = sky->skyDomes[i];
		if(!sky_dome->bHasErrors)
		{
			gfxSkyDomeBlendToTime(result, sky_dome, sky_dome->dome_values, &sky_dome->current_dome_values, time);
			assert(result->skyDomeCount < MAX_SKY_DOMES);
			result->skyDomes[result->skyDomeCount] = gfxSkyCreateBlendedSkyDome(sky_dome, sky_dome->sort_order + sort_depth, sky_dome->highDetail, percent, true);
			result->skyDomeCount++;
		}
	}
}

//Blend two BlendedSkyInfos together
typedef enum BlendSkyDomesMode {
	BSDM_STEAL_AND_ADD=0,		//Steal BlendedSkyDomes from the blend_from and add them to result's list
	BSDM_STEAL_AND_BLEND,		//Blend Duplicate Domes, Fade out missing domes, and STEAL_AND_ADD the new Domes
	BSDM_COPY_AND_BLEND,		//Blend Duplicate Domes, Fade out missing domes, and make deep copies of new Domes and add drawables
} BlendSkyDomesMode;
void gfxSkyBlendSkies(BlendedSkyInfo *result, BlendedSkyInfo *blend_from, F32 ratio, BlendSkyDomesMode mode)
{
	int i, j, k;
	int dome_count;
	U8 *found_domes;

	/////////////////////////////
	//Blend Structs
	/////////////////////////////
	gfxSkyDoOperationOnBlendedSky(gfxSkyValuesBlend, result, blend_from, &ratio);

	/////////////////////////////
	//Blend General Values
	/////////////////////////////
	if(ratio > 0.5)
	{
		if (blend_from->cloudShadowTexture)
			result->cloudShadowTexture = blend_from->cloudShadowTexture;
		if (blend_from->diffuseWarpTextureCharacter)
			result->diffuseWarpTextureCharacter = blend_from->diffuseWarpTextureCharacter;
		if (blend_from->diffuseWarpTextureWorld)
			result->diffuseWarpTextureWorld = blend_from->diffuseWarpTextureWorld;
		if (blend_from->ambientCube)
			result->ambientCube = blend_from->ambientCube;
		if (blend_from->reflectionCube)
			result->reflectionCube = blend_from->reflectionCube;
		if (!blend_from->ignoreFogClipFar)
		{
			result->fogClipFar = blend_from->fogClipFar;
		}
		if (!blend_from->ignoreFogClipLow)
		{
			result->fogClipLow = blend_from->fogClipLow;
		}
	}
	//Blend light direction
	if(mode == BSDM_STEAL_AND_BLEND && (blend_from->specified_values & SKY_DOME_VALUES))
	{
		Quat quat_from, quat_to, quat_result;
		Mat4 light_mat;
		quatLookAt(zerovec3, blend_from->outputWorldLightDir, quat_from);
		quatLookAt(zerovec3, result->outputWorldLightDir, quat_to);
		quatInterp(ratio, quat_to, quat_from, quat_result);
		quatToMat(quat_result, light_mat);
		copyVec3(light_mat[2], result->outputWorldLightDir);

		quatLookAt(zerovec3, blend_from->outputWorldLightDir2, quat_from);
		quatLookAt(zerovec3, result->outputWorldLightDir2, quat_to);
		quatInterp(ratio, quat_to, quat_from, quat_result);
		quatToMat(quat_result, light_mat);
		copyVec3(light_mat[2], result->outputWorldLightDir2);

		result->secondaryLightType2 = blend_from->secondaryLightType2;
	}
	else if(mode == BSDM_COPY_AND_BLEND)
	{
		copyVec3(blend_from->outputWorldLightDir, result->outputWorldLightDir);
		copyVec3(blend_from->outputWorldLightDir2, result->outputWorldLightDir2);
		result->secondaryLightType2 = blend_from->secondaryLightType2;
	}

	/////////////////////////////
	//Blend the BlendedSkyDomes
	/////////////////////////////

	//If steal and add, just append to the end and be done
	if(mode == BSDM_STEAL_AND_ADD)
	{
		//If no sky domes in blend_from, do not blend
		if(!(blend_from->specified_values & SKY_DOME_VALUES))
			return;
		for( i=0; i < blend_from->skyDomeCount; i++ )
		{
			assert(result->skyDomeCount < MAX_SKY_DOMES);
			result->skyDomes[result->skyDomeCount] = blend_from->skyDomes[i];
			result->skyDomeCount++;
		}
		blend_from->skyDomeCount = 0;
		//Sky domes are now specified
		result->specified_values |= SKY_DOME_VALUES;
		return;
	}

	//If doing BSDM_STEAL_AND_BLEND and there are no sky domes in blend_from, do not blend
	if(mode == BSDM_STEAL_AND_BLEND && !(blend_from->specified_values & SKY_DOME_VALUES))
		return;

	//Sky domes are now specified
	result->specified_values |= SKY_DOME_VALUES;

	//Make an array for keeping track of what sky domes are no longer used
	dome_count = result->skyDomeCount;
	found_domes = ScratchAlloc(sizeof(*found_domes) * dome_count);
	for( i=0; i < dome_count; i++ )
	{
		found_domes[i] = false;
	}

	//For each new dome
	for( i=0; i < blend_from->skyDomeCount; i++ )
	{
		BlendedSkyDome *blended_sky_dome = blend_from->skyDomes[i];
		bool dup_found = false;

		//Search for a duplicate sky dome
		for( j=0; j < result->skyDomeCount; j++ )
		{
			//If found then blend the two
			if(result->skyDomes[j]->dome->sky_uid == blended_sky_dome->dome->sky_uid)
			{
				result->skyDomes[j]->alpha = lerp(result->skyDomes[j]->alpha, blended_sky_dome->alpha, ratio);
				result->skyDomes[j]->group_percent = blended_sky_dome->group_percent;
				result->skyDomes[j]->sort_order = blended_sky_dome->sort_order;
				//If this is not just a reference then we need to update any values changes
				if(!result->skyDomes[j]->is_refrence)
				{
					//blend values
					gfxSkyBlendSkyDomeTimes(&result->skyDomes[j]->dome->current_dome_values, 
											&blended_sky_dome->dome->current_dome_values, 
											ratio, 
											&result->skyDomes[j]->dome->current_dome_values);
				}
				found_domes[j] = true;
				dup_found = true;
				break;
			}
		}
		//If not found then add to our result
		if(!dup_found)
		{
			BlendedSkyDome *new_dome;
			//If stealing then just remove from one then add to the other
			if(mode == BSDM_STEAL_AND_BLEND)
			{
				new_dome = blend_from->skyDomes[i];
				blend_from->skyDomeCount--;
				for( k=i; k < blend_from->skyDomeCount ; k++ )
				{
					blend_from->skyDomes[k] = blend_from->skyDomes[k+1];
				}
				assert(result->skyDomeCount < MAX_SKY_DOMES);
				result->skyDomes[result->skyDomeCount] = new_dome;
				result->skyDomeCount++;
				i--;
			}
			//Otherwise if copying then make a copy
			else if(mode == BSDM_COPY_AND_BLEND)
			{
				new_dome = gfxSkyCreateBlendedSkyDome(blended_sky_dome->dome, blended_sky_dome->sort_order, blended_sky_dome->high_detail, blended_sky_dome->group_percent, false);
				new_dome->drawable = gfxSkyCreateDrawable(new_dome->dome->name, new_dome->dome->star_field, new_dome->dome->atmosphere);
				assert(result->skyDomeCount < MAX_SKY_DOMES);
				new_dome->alpha = blended_sky_dome->alpha;
				result->skyDomes[result->skyDomeCount] = new_dome;
				result->skyDomeCount++;
			}
			else
			{
				assert(0);
			}
			//Blend alpha away from 0
			new_dome->alpha *= ratio;
		}
	}

	//Search for unused domes
	for( i=dome_count-1; i >= 0; i-- )
	{
		//If dome no longer used
		if(!found_domes[i])
		{
			assert(i < result->skyDomeCount);
			//Blend alpha towards 0
			result->skyDomes[i]->alpha *= (1-ratio);
			//If alpha = 0 then remove and delete
			if(nearSameF32(0.0f, result->skyDomes[i]->alpha))
			{
				BlendedSkyDome *delete_dome = result->skyDomes[i];
				result->skyDomeCount--;
				for( k=i; k < result->skyDomeCount ; k++ )
				{
					result->skyDomes[k] = result->skyDomes[k+1];
				}
				gfxSkyDestroyBlendedSkyDome(delete_dome);
			}
		}
	}

	ScratchFree(found_domes);
}

#define SKYDOME_MIN_BOUNDING_RADIUS 2200
#define SKYDOME_MAX_BOUNDING_RADIUS 22000

static void gfxSkyDomeGetMatrix(SkyDome *dome, Mat4 mat_out)
{
	Vec3 rotation_axis;
	Mat4 pos_mat, rot_mat, axis_mat, result_mat;
	F32 domeScale = dome->current_dome_values.scale;
	F32 angle = dome->current_dome_values.angle;

	//Init Matrices
	copyMat4(unitmat, mat_out);
	copyMat4(unitmat, pos_mat);
	copyMat4(unitmat, rot_mat);
	copyMat4(unitmat, axis_mat);

	if(!dome->luminary && !dome->luminary2)
	{
		ModelHeader* model = wlModelHeaderFromName(dome->name);
		if (model)
		{
			F32 practicalRadius = model->radius * dome->current_dome_values.scale;

			if (model->radius <= 0.0f || dome->current_dome_values.scale <= 0.0f) {
				Errorf("Sky contains a dome %s which has an invalid %s that is less than or equal to zero - not allowed",
					dome->name, dome->current_dome_values.scale <= 0.0f ? "dome scale" : "model radius");
			}
			else if (practicalRadius < SKYDOME_MIN_BOUNDING_RADIUS) {
				domeScale *= SKYDOME_MIN_BOUNDING_RADIUS / practicalRadius;
			} else if (practicalRadius > SKYDOME_MAX_BOUNDING_RADIUS) {
				domeScale *= SKYDOME_MAX_BOUNDING_RADIUS / practicalRadius;
			}
		}
	}

	//Scale the Dome
	scaleMat3(pos_mat, pos_mat, domeScale);

	//If this is a Sun or a Moon then position and rotation is different
	if(dome->luminary || dome->luminary2)
	{
		setVec3(pos_mat[3], 8000, 0, 0);
		angle += 90.0f;
	}

	//Get the matrix for rotating around the Z-axis
	rollMat3(PI*angle/180.0f, rot_mat);

	//Get the axis of rotation's matrix
	copyVec3(dome->rotation_axis, rotation_axis);
	normalVec3(rotation_axis);
	orientMat3(axis_mat, rotation_axis);

	//Rotate around the Z-axis to the angle of the current time
	mulMat4(rot_mat, pos_mat, result_mat);
	//Rotate the result to face in the direction of the rotation_axis
	mulMat4(axis_mat, result_mat, mat_out);

	//Set the position
	if(dome->luminary || dome->luminary2)
		addToVec3(dome->current_dome_values.pos, mat_out[3]);
	else
		copyVec3(dome->current_dome_values.pos, mat_out[3]);
}

static void gfxSkyCalcLightDir(BlendedSkyInfo *sky_info)
{
	int i;
	bool light_found_1 = false;
	bool light_found_2 = false;

	for( i=0; i < sky_info->skyDomeCount; i++ )
	{
		SkyDome *dome = sky_info->skyDomes[i]->dome;
		if(!light_found_1 && dome->luminary) {
			Mat4 mat;
			gfxSkyDomeGetMatrix(dome, mat);
			copyVec3(mat[3], sky_info->outputWorldLightDir);
			normalVec3(sky_info->outputWorldLightDir);
			light_found_1 = true;
		}
		if(!light_found_2 && dome->luminary2) {
			Mat4 mat;
			gfxSkyDomeGetMatrix(dome, mat);
			copyVec3(mat[3], sky_info->outputWorldLightDir2);
			normalVec3(sky_info->outputWorldLightDir2);
			sky_info->secondaryLightType2 = (dome->character_only ? WL_LIGHTAFFECT_DYNAMIC : WL_LIGHTAFFECT_ALL);
			light_found_2 = true;
		}
	}
	if(!light_found_1) {
		setVec3(sky_info->outputWorldLightDir, -1, 1, 1);
		normalVec3(sky_info->outputWorldLightDir);
	} 
	if(!light_found_2) {
		setVec3(sky_info->outputWorldLightDir2, -1, 1, 1);
		normalVec3(sky_info->outputWorldLightDir2);
	}
}

//////////////////////////////////////////////////////////////////////////
//	Custom DOF Override
//////////////////////////////////////////////////////////////////////////

void gfxSkySetCustomDOF(GfxSkyData *sky_data, F32 nearDist, F32 nearValue, F32 focusDist, F32 focusValue, F32 farDist, F32 farValue, bool fade_in, F32 fade_rate)
{
	//Used for giving Skies created for custom DOF overrides a unique name
	static int custom_dof_sky_idx=0;
	bool preexisting = false;
	SkyInfo *dof_sky;
	DOFValues *dof_values;

	//If we already are using an override, then do nothing
	if(sky_data->custom_dof_sky_group)
		preexisting = true;

	//If we have not used a custom DOF for this sky data yet, then we need to make a sky
	if(!sky_data->custom_dof_sky_name)
	{
		char buf[255];
		SkyInfo *new_sky;

		//Make a unique name for the sky
		sprintf(buf, "CUSTOM_DOF_SKY_NUM_%d", custom_dof_sky_idx);
		custom_dof_sky_idx++;
		sky_data->custom_dof_sky_name = StructAllocString(buf);

		//Create the sky
		new_sky = StructAlloc(parse_SkyInfo);
		new_sky->filename_no_path = allocAddString(sky_data->custom_dof_sky_name);

		//Add a DOF value set and set all the values as specified
		dof_values = StructAlloc(parse_DOFValues);
		TokenSetSpecified(parse_DOFValues, dof_nearDist_column,   dof_values, dof_usedfield_column, true);
		TokenSetSpecified(parse_DOFValues, dof_nearValue_column,  dof_values, dof_usedfield_column, true);
		TokenSetSpecified(parse_DOFValues, dof_focusDist_column,  dof_values, dof_usedfield_column, true);
		TokenSetSpecified(parse_DOFValues, dof_focusValue_column, dof_values, dof_usedfield_column, true);
		TokenSetSpecified(parse_DOFValues, dof_farDist_column,    dof_values, dof_usedfield_column, true);
		TokenSetSpecified(parse_DOFValues, dof_farValue_column,   dof_values, dof_usedfield_column, true);
		eaPush(&new_sky->dofValues, dof_values);

		//Add to our dictionary
		RefSystem_AddReferent(gfx_sky_dict, sky_data->custom_dof_sky_name, new_sky);
	}

	//Get the sky
	dof_sky = gfxSkyFindSky(sky_data->custom_dof_sky_name);
	assert(dof_sky && eaSize(&dof_sky->dofValues) == 1);
	dof_values = dof_sky->dofValues[0];

	//Fill in custom values
	dof_values->nearDist   = nearDist;
	dof_values->nearValue  = nearValue;
	dof_values->focusDist  = focusDist;
	dof_values->focusValue = focusValue;
	dof_values->farDist    = farDist;
	dof_values->farValue   = farValue;
	dof_values->skyValue   = farValue;

	if(!preexisting)
	{
		//Add the sky to the draw list
		sky_data->dof_needs_reload = !fade_in;
		sky_data->custom_dof_sky_group = StructCreate(parse_SkyInfoGroup);
		gfxSkyGroupAddOverride(sky_data->custom_dof_sky_group, sky_data->custom_dof_sky_name);
		gfxSkyGroupFadeTo(sky_data, sky_data->custom_dof_sky_group, -1, 1, fade_in?fade_rate:-1.0f, 1000, false);
	}
}

void gfxSkyUnsetCustomDOF(GfxSkyData *sky_data, bool fade_out, F32 fade_rate)
{
	//Remove the Sky from the draw list
	if(!sky_data->custom_dof_sky_group)
		return;
	sky_data->dof_needs_reload = !fade_out;
	gfxSkyGroupFadeTo(sky_data, sky_data->custom_dof_sky_group, -1, 0, fade_out?fade_rate:-1.0f, 1000, true);
	sky_data->custom_dof_sky_group = NULL;
}


//////////////////////////////////////////////////////////////////////////
//	Custom Fog Override
//////////////////////////////////////////////////////////////////////////
void gfxSkySetCustomFog(GfxSkyData *sky_data, const Vec3 color_hsv, float fog_dist_near, float fog_dist_far, float fog_max, bool fade_in, F32 fade_rate)
{
	//Used for giving Skies created for custom fog overrides a unique name
	static int custom_fog_sky_idx=0;

	SkyInfo *dof_sky;
	SkyTimeFog *fog_values;

	//If we already are using an override, then do nothing
	if(sky_data->custom_fog_sky_group)
		return;

	//If we have not used a custom fog for this sky data yet, then we need to make a sky
	if(!sky_data->custom_fog_sky_name)
	{
		char buf[255];
		SkyInfo *new_sky;

		//Make a unique name for the sky
		sprintf(buf, "CUSTOM_FOG_SKY_NUM_%d", custom_fog_sky_idx);
		custom_fog_sky_idx++;
		sky_data->custom_fog_sky_name = StructAllocString(buf);

		//Create the sky
		new_sky = StructAlloc(parse_SkyInfo);
		new_sky->filename_no_path = allocAddString(sky_data->custom_fog_sky_name);

		//Add a SkyTimeFog set and set all the values as specified
		fog_values = StructAlloc(parse_SkyTimeFog);
		TokenSetSpecified(parse_SkyTimeFog, fog_lowFogColorHSV_column,	fog_values, fog_usedfield_column, true);
		TokenSetSpecified(parse_SkyTimeFog, fog_lowFogDist_column,		fog_values, fog_usedfield_column, true);
		TokenSetSpecified(parse_SkyTimeFog, fog_lowFogMax_column,		fog_values, fog_usedfield_column, true);
		TokenSetSpecified(parse_SkyTimeFog, fog_highFogColorHSV_column,	fog_values, fog_usedfield_column, true);
		TokenSetSpecified(parse_SkyTimeFog, fog_highFogDist_column,		fog_values, fog_usedfield_column, true);
		TokenSetSpecified(parse_SkyTimeFog, fog_highFogMax_column,		fog_values, fog_usedfield_column, true);
		eaPush(&new_sky->fogValues, fog_values);

		//Add to our dictionary
		RefSystem_AddReferent(gfx_sky_dict, sky_data->custom_fog_sky_name, new_sky);
	}

	//Get the sky
	dof_sky = gfxSkyFindSky(sky_data->custom_fog_sky_name);
	assert(dof_sky && eaSize(&dof_sky->fogValues) == 1);
	fog_values = dof_sky->fogValues[0];

	//Fill in custom values
	copyVec3(color_hsv, fog_values->lowFogColorHSV);
	copyVec3(color_hsv, fog_values->highFogColorHSV);
	copyVec3(color_hsv, fog_values->clipFogColorHSV);
	copyVec3(color_hsv, fog_values->clipBackgroundColorHSV);
	fog_values->highFogDist[ 0 ] = fog_values->lowFogDist[ 0 ] = fog_dist_near;
	fog_values->highFogDist[ 1 ] = fog_values->lowFogDist[ 1 ] = fog_dist_far;
	fog_values->highFogMax		 = fog_values->lowFogMax	   = fog_max;

	//Add the sky to the draw list
	sky_data->fog_needs_reload = !fade_in;
	sky_data->custom_fog_sky_group = StructCreate(parse_SkyInfoGroup);
	gfxSkyGroupAddOverride(sky_data->custom_fog_sky_group, sky_data->custom_fog_sky_name);
	gfxSkyGroupFadeTo(sky_data, sky_data->custom_fog_sky_group, -1, 1, fade_in?fade_rate:-1.0f, 1000, false);
}

void gfxSkyUnsetCustomFog(GfxSkyData *sky_data, bool fade_out, F32 fade_rate)
{
	//Remove the Sky from the draw list
	sky_data->fog_needs_reload = !fade_out;
	gfxSkyGroupFadeTo(sky_data, sky_data->custom_fog_sky_group, -1, 0, fade_out?fade_rate:-1.0f, 1000, true);
	sky_data->custom_fog_sky_group = NULL;
}

//////////////////////////////////////////////////////////////////////////
//	Updating and Applying Values
//////////////////////////////////////////////////////////////////////////

//Apply Fog
static void gfxSkyApplyFog(BlendedSkyInfo *sky_work)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	F32 lod_scale = gdraw->lod_scale;

	if (lod_scale <= 0.6f)
	{
		F32 fog_clip_dist = gfx_state.force_fog_clip_dist * worldRegionGetEffectiveScale(gdraw->regions[0]) * lod_scale / 0.6f * (1 + sky_work->fogValues.clipFogDistanceAdjust);
		F32 orig_fog_max = sky_work->fogValues.lowFogMax;

		sky_work->fogClipFar = true;
		sky_work->fogValues.highFogMax = sky_work->fogValues.lowFogMax = 1;

		if (sky_work->fogValues.lowFogDist[1] > fog_clip_dist)
		{
			F32 near_far_ratio = (sky_work->fogValues.lowFogDist[1] - sky_work->fogValues.lowFogDist[0]) / sky_work->fogValues.lowFogDist[1];
			sky_work->fogValues.lowFogDist[1] = fog_clip_dist;
			sky_work->fogValues.lowFogDist[0] = fog_clip_dist * (1 - near_far_ratio);
		}

		if (sky_work->fogValues.highFogDist[1] > fog_clip_dist)
		{
			F32 near_far_ratio = (sky_work->fogValues.highFogDist[1] - sky_work->fogValues.highFogDist[0]) / sky_work->fogValues.highFogDist[1];
			sky_work->fogValues.highFogDist[1] = fog_clip_dist;
			sky_work->fogValues.highFogDist[0] = fog_clip_dist * (1 - near_far_ratio);
		}

		// JE: This is making fog mostly disappear in foggy zones on low settings,
		//   so let's only do this when the fog_max was such that we are expecting to see through the fog well?
		if (orig_fog_max < 0.5)
		{
			// CD: push the near fog distance out so people can see; this is necessary because the max fog was set back to 1 above.
			MAX1(sky_work->fogValues.lowFogDist[0], sky_work->fogValues.lowFogDist[1] * 0.5f);
			MAX1(sky_work->fogValues.highFogDist[0], sky_work->fogValues.highFogDist[1] * 0.5f);
		}
	}

	if (gdraw)
	{
		if ((sky_work->fogClipFar || sky_work->fogClipLow) && !gfx_state.currentCameraController->override_no_fog_clip)
		{
			gdraw->clip_distance = MAX(sky_work->fogValues.lowFogDist[1], sky_work->fogValues.highFogDist[1]);
			gdraw->clip_by_distance = sky_work->fogClipFar || sky_work->fogClipLow;
			gdraw->clip_only_below_camera = sky_work->fogClipLow;
		}
		else
		{
			gdraw->clip_distance = 1e16;
			gdraw->clip_by_distance = false;
			gdraw->clip_only_below_camera = false;
		}
	}

	if (gfx_state.currentCameraController->override_bg_color)
	{
		copyVec4(gfx_state.currentCameraController->clear_color, gfx_state.currentCameraView->clear_color);
	}
	else
	{
		if ((sky_work->fogClipFar || sky_work->fogClipLow) && !gfx_state.currentCameraController->override_no_fog_clip)
		{
			copyVec3(sky_work->fogValues.clipBackgroundColorHSV, sky_work->sunValues.backgroundColorHSV);
			copyVec3(sky_work->fogValues.clipFogColorHSV, sky_work->fogValues.highFogColorHSV);
			copyVec3(sky_work->fogValues.clipFogColorHSV, sky_work->fogValues.lowFogColorHSV);
		}
		hsvToRgb(sky_work->sunValues.backgroundColorHSV, gfx_state.currentCameraView->clear_color);
	}
}

//Apply Lighting
static void gfxSkyApplyLighting(WorldGraphicsData *world_data, GfxGlobalDrawParams *gdraw, BlendedSkyInfo *new_sky_info)
{
	Vec3 hsv_temp;

	if (gdraw)
		hsvToRgb(new_sky_info->charLightingValues.backlightHSV, gdraw->character_backlight);

	if (gdraw)
	{
		gdraw->diffuse_warp_texture_character = texFindAndFlag(new_sky_info->diffuseWarpTextureCharacter, 1, WL_FOR_ENTITY);
		gdraw->diffuse_warp_texture_world = texFindAndFlag(new_sky_info->diffuseWarpTextureWorld, 1, WL_FOR_WORLD);
		gdraw->ambient_cube = texFindAndFlag(new_sky_info->ambientCube, 1, WL_FOR_WORLD);
		gdraw->env_cubemap_from_sky = texFindAndFlag(new_sky_info->reflectionCube, 1, WL_FOR_WORLD);
		if (!gdraw->diffuse_warp_texture_character)
			gdraw->diffuse_warp_texture_character = texFind("default_diffuse_warp", 1);
		if (!gdraw->diffuse_warp_texture_world)
			gdraw->diffuse_warp_texture_world = texFind("default_diffuse_warp", 1);
		if (!gdraw->ambient_cube)
			gdraw->ambient_cube = default_ambient_cube;
		if (!gdraw->env_cubemap_from_sky)
			gdraw->env_cubemap_from_sky = default_env_cubetex;

		// determine spheremap from cubemap
		if (gdraw->env_cubemap_from_sky) {
			BasicTexture *spheremap;
			char spheremap_name[MAX_PATH];
			char *s;
			strcpy(spheremap_name, gdraw->env_cubemap_from_sky->name);
			s = strrchr(spheremap_name, '_');
			if (s)
			{
				assert(s);
				strcpy_s(s, ARRAY_SIZE(spheremap_name) - (s - spheremap_name), "_spheremap");
			} // otherwise, use the same texture, maybe it's not a cubemap anyway
			if (spheremap = texFindAndFlag(spheremap_name, true, WL_FOR_WORLD|WL_FOR_ENTITY))
			{
				gdraw->env_spheremap_from_sky = spheremap;
			} else {
				gdraw->env_spheremap_from_sky = NULL;
			}
		} else {
			gdraw->env_cubemap_from_sky = NULL;
			gdraw->env_spheremap_from_sky = NULL;
		}
	}

	// Set character light offsets
	copyVec3(new_sky_info->charLightingValues.ambientHSVOffset, world_data->character_ambient_light_offset_hsv);
	copyVec3(new_sky_info->charLightingValues.skyLightHSVOffset, world_data->character_sky_light_offset_hsv);
	copyVec3(new_sky_info->charLightingValues.groundLightHSVOffset, world_data->character_ground_light_offset_hsv);
	copyVec3(new_sky_info->charLightingValues.sideLightHSVOffset, world_data->character_side_light_offset_hsv);
	copyVec3(new_sky_info->charLightingValues.diffuseHSVOffset, world_data->character_diffuse_light_offset_hsv);
	copyVec3(new_sky_info->charLightingValues.secondaryDiffuseHSVOffset, world_data->character_secondary_diffuse_light_offset_hsv);
	copyVec3(new_sky_info->charLightingValues.shadowColorHSVOffset, world_data->character_shadow_color_light_offset_hsv);
	copyVec3(new_sky_info->charLightingValues.specularHSVOffset, world_data->character_specular_light_offset_hsv);


	// Apply ambient lighting
	// Short-term hack: alternate ambient being used if dynamic lights are off, long term should add some flags for when to use this
	if (new_sky_info->sunValues.ambientHSValternate[2] > 0 && (!gfx_state.settings.dynamicLights || gfx_state.settings.maxLightsPerObject <= 2 || gfx_state.vertexOnlyLighting))
		gfxHsvToRgb(new_sky_info->sunValues.ambientHSValternate, world_data->outdoor_ambient_light->ambient[RLCT_WORLD]);
	else
		gfxHsvToRgb(new_sky_info->sunValues.ambientHSV, world_data->outdoor_ambient_light->ambient[RLCT_WORLD]);
	gfxHsvToRgb(new_sky_info->sunValues.skyLightHSV, world_data->outdoor_ambient_light->sky_light_color_front[RLCT_WORLD]);
	gfxHsvToRgb(new_sky_info->sunValues.groundLightHSV, world_data->outdoor_ambient_light->sky_light_color_back[RLCT_WORLD]);
	gfxHsvToRgb(new_sky_info->sunValues.sideLightHSV, world_data->outdoor_ambient_light->sky_light_color_side[RLCT_WORLD]);

	addVec3(new_sky_info->sunValues.ambientHSV, world_data->character_ambient_light_offset_hsv, hsv_temp);
	hsvMakeLegal(hsv_temp, false);
	gfxHsvToRgb(hsv_temp, world_data->outdoor_ambient_light->ambient[RLCT_CHARACTER]);

	addVec3(new_sky_info->sunValues.skyLightHSV, world_data->character_sky_light_offset_hsv, hsv_temp);
	hsvMakeLegal(hsv_temp, false);
	gfxHsvToRgb(hsv_temp, world_data->outdoor_ambient_light->sky_light_color_front[RLCT_CHARACTER]);

	addVec3(new_sky_info->sunValues.groundLightHSV, world_data->character_ground_light_offset_hsv, hsv_temp);
	hsvMakeLegal(hsv_temp, false);
	gfxHsvToRgb(hsv_temp, world_data->outdoor_ambient_light->sky_light_color_back[RLCT_CHARACTER]);

	addVec3(new_sky_info->sunValues.sideLightHSV, world_data->character_side_light_offset_hsv, hsv_temp);
	hsvMakeLegal(hsv_temp, false);
	gfxHsvToRgb(hsv_temp, world_data->outdoor_ambient_light->sky_light_color_side[RLCT_CHARACTER]);


	// Apply sun lighting
	{
		LightData light_data = {0};
		light_data.light_type = WL_LIGHT_DIRECTIONAL;
		light_data.cast_shadows = 1;
		light_data.is_sun = 1;
		copyVec3(new_sky_info->sunValues.diffuseHSV, light_data.diffuse_hsv);
		copyVec3(new_sky_info->sunValues.specularHSV, light_data.specular_hsv);
		copyVec3(new_sky_info->sunValues.secondaryDiffuseHSV, light_data.secondary_diffuse_hsv);
		copyVec3(new_sky_info->sunValues.shadowColorHSV, light_data.shadow_color_hsv);
		copyVec3(new_sky_info->outputWorldLightDir, light_data.world_mat[1]);

		if(light_data.world_mat[1][1] < 0)
			light_data.world_mat[1][1] = -light_data.world_mat[1][1];

		light_data.min_shadow_val = new_sky_info->sunValues.shadowMinValue;

		light_data.shadow_fade_val = new_sky_info->shadowFadeValues.fadeValue;
		light_data.shadow_fade_time = new_sky_info->shadowFadeValues.fadeTime;
		light_data.shadow_fade_dark_time = new_sky_info->shadowFadeValues.darkTime;
		light_data.shadow_pulse_amount = new_sky_info->shadowFadeValues.pulseAmount;
		light_data.shadow_pulse_rate = new_sky_info->shadowFadeValues.pulseRate;

		if (new_sky_info->cloudShadowValues.layer1Multiplier || new_sky_info->cloudShadowValues.layer2Multiplier)
		{
			light_data.cloud_texture_name = new_sky_info->cloudShadowTexture;
		}
		if (new_sky_info->cloudShadowValues.layer1Multiplier)
		{
			light_data.cloud_layers[0].multiplier = new_sky_info->cloudShadowValues.layer1Multiplier;
			light_data.cloud_layers[0].scale = MAX(0.1f, new_sky_info->cloudShadowValues.layer1Scale);
			copyVec2(new_sky_info->cloudShadowValues.layer1ScrollRate, light_data.cloud_layers[0].scroll_rate);
		}
		if (new_sky_info->cloudShadowValues.layer2Multiplier)
		{
			light_data.cloud_layers[1].multiplier = new_sky_info->cloudShadowValues.layer2Multiplier;
			light_data.cloud_layers[1].scale = MAX(0.1f, new_sky_info->cloudShadowValues.layer2Scale);
			copyVec2(new_sky_info->cloudShadowValues.layer2ScrollRate, light_data.cloud_layers[1].scroll_rate);
		}

		gfxUpdateSunLight(&world_data->sun_light, &light_data, new_sky_info);
	}

	// Apply secondary sun lighting
	{
		LightData light_data = {0};
		light_data.light_type = WL_LIGHT_DIRECTIONAL;
		light_data.light_affect_type = new_sky_info->secondaryLightType2;
		light_data.is_sun = 1;
		copyVec3(new_sky_info->secSunValues.diffuseHSV, light_data.diffuse_hsv);
		copyVec3(new_sky_info->secSunValues.specularHSV, light_data.specular_hsv);
		copyVec3(new_sky_info->secSunValues.secondaryDiffuseHSV, light_data.secondary_diffuse_hsv);
		copyVec3(new_sky_info->outputWorldLightDir2, light_data.world_mat[1]);

		if(light_data.world_mat[1][1] < 0)
			light_data.world_mat[1][1] = -light_data.world_mat[1][1];

		gfxUpdateSunLight(&world_data->sun_light_2, &light_data, new_sky_info);
	}
}

//Update SkyDome positions
static void gfxSkyUpdateSkyDomes(BlendedSkyInfo *sky_info, F32 ratio)
{
	int i;
	for( i=0; i < sky_info->skyDomeCount ; i++ )
	{
		Mat4 mat;
		BlendedSkyDome *sky_dome = sky_info->skyDomes[i];
		gfxSkyDomeGetMatrix(sky_dome->dome, mat);
		copyMat4(mat, sky_dome->drawable->mat);
	}
}

static void gfxSkyApplyWind(BlendedSkyInfo* sky_info)
{
	DynWindSettings settings;
	memcpy(&settings, dynWindGetCurrentSettings(), sizeof(DynWindSettings));

	settings.bDisabled = false;
	settings.fMag = sky_info->windValues.speed;
	settings.fMagRange = sky_info->windValues.speedVariation;
	copyVec3(sky_info->windValues.direction, settings.vDir);
	copyVec3(sky_info->windValues.directionVariation, settings.vDirRange);
	settings.fChangeRate = sky_info->windValues.turbulence;
	settings.fSpatialScale = sky_info->windValues.variationScale;

	dynWindSetCurrentSettings(&settings);
}

//Blend the New Sky into the Visible Sky and apply resulting values.
static void gfxSkyApplyValues(GfxCameraView *camera_view, BlendedSkyInfo *new_sky_info)
{
	GfxSkyData *sky_data = camera_view->sky_data;
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	F32 ratio = 1.0f;

	//If we need to reload then clear the visible sky
	if(sky_data->sky_needs_reload < sky_needs_reload)
	{
		gfxSkyDestroyBlendedSkyInfo(sky_data->visible_sky);
		sky_data->visible_sky = StructCreate(parse_BlendedSkyInfo);
		sky_data->sky_needs_reload = sky_needs_reload;
	}

	//Blend New Sky into Visible Sky
	new_sky_info->specified_values = ~0; // Blend all fields, even those that are now 0s, otherwise temporary sky values on now unspecified fields get stuck (e.g. when FX starts a sky file)
	gfxSkyBlendSkies(sky_data->visible_sky, new_sky_info, ratio, BSDM_COPY_AND_BLEND);

	//If DOF needs to reload then just copy the new values full
	if(sky_data->dof_needs_reload)
	{
		shDoOperationSetFloat(1.0f);
		shDoOperation(STRUCTOP_LERP, parse_DOFValues, &sky_data->visible_sky->dofValues, &new_sky_info->dofValues);
		sky_data->dof_needs_reload = false;
	}
	if(sky_data->fog_needs_reload)
	{
		shDoOperationSetFloat(1.0f);
		shDoOperation(STRUCTOP_LERP, parse_SkyTimeFog, &sky_data->visible_sky->fogValues, &new_sky_info->fogValues);
		sky_data->fog_needs_reload = false;
	}

	//Update Sky Dome Positions
	gfxSkyUpdateSkyDomes(sky_data->visible_sky, ratio);

	//Apply Fog
	if (gdraw)
		gfxSkyApplyFog(sky_data->visible_sky);

	//Apply Lighting
	gfxSkyApplyLighting(world_data, gdraw, sky_data->visible_sky);

	//Apply Wind
	if (gfx_state.currentAction && !gfx_state.currentAction->is_offscreen) //the wind state is global instead of per-region so we don't want to update with offscreen stuff
		gfxSkyApplyWind(sky_data->visible_sky);
}

//Update Sky Info to reflect the current time
void gfxSkyUpdate(GfxCameraView *camera_view, F32 time)
{
	int i, j;
	GfxSkyData *sky_data = camera_view->sky_data;
	BlendedSkyInfo output;
	StructInit(parse_BlendedSkyInfo, &output);

	//Set New Time
	time = gfxSkyRepairTime(time);
	camera_view->sky_data->time = time;

	//Use the DEFAULT_SKY as a base
	//Blend it to the current time and set as our output
	gfxSkyBlendToTime(gfxSkyFindSky(DEFAULT_SKY), &output, time, 0.0f, 0.0f);
	gfxSkyCalcLightDir(&output);

	//For each SkyInfoGroup
	for( i=0; i < eaSize(&sky_data->active_sky_groups); i++ )
	{
		BlendedSkyInfo blended_sky_group;
		SkyInfoGroupInstantiated *active_sky_group = sky_data->active_sky_groups[i];
		SkyInfoGroup *sky_group = active_sky_group->sky_group;

		// Update Blend Percentage
		if (active_sky_group->fade_rate < 0)
		{
			active_sky_group->percent = active_sky_group->desired_percent;
		}
		else if (active_sky_group->desired_percent < active_sky_group->percent)
		{
			active_sky_group->percent -= active_sky_group->fade_rate * gfx_state.frame_time;
			MAX1(active_sky_group->percent, active_sky_group->desired_percent);
		}
		else if (active_sky_group->desired_percent > active_sky_group->percent)
		{
			active_sky_group->percent += active_sky_group->fade_rate * gfx_state.frame_time;
			MIN1(active_sky_group->percent, active_sky_group->desired_percent);
		}

		if (nearSameF32(active_sky_group->percent, active_sky_group->desired_percent))
		{
			active_sky_group->percent = active_sky_group->desired_percent;
			if(active_sky_group->percent == 0.0f)
			{
				//If completely faded out, remove and goto the next one on the list
				if(active_sky_group->delete_on_fade)
					StructDestroy(parse_SkyInfoGroup, sky_group);
				gfxSkyRemoveSkyGroup(sky_data, sky_group);
				i--;
				continue;
			}
		}

		StructInit(parse_BlendedSkyInfo, &blended_sky_group);

		//For each Override 
		for( j=0; j < eaSize(&sky_group->override_list); j++ )
		{
			SkyInfoOverride *override_sky = sky_group->override_list[j];
			F32 override_time = (override_sky->override_time ? gfxSkyRepairTime(override_sky->time) : time);
			BlendedSkyInfo blended_sky;
			SkyInfo *sky_info;
			StructInit(parse_BlendedSkyInfo, &blended_sky);

			sky_info = GET_REF(override_sky->sky);
			if(gfxSkyUseFallback() && sky_info) {
				SkyInfo *sky_fallback = GET_REF(sky_info->noPPFallback);
				if(sky_fallback)
					sky_info = sky_fallback;
			}

			//Blend to the current time
			gfxSkyBlendToTime(sky_info, &blended_sky, override_time, (j+1)*100.0f + i*5000.0f, active_sky_group->percent);
			//Blend specified values into the sky group output
			gfxSkyBlendSkies(&blended_sky_group, &blended_sky, 1.0f, BSDM_STEAL_AND_ADD);

			gfxSkyDestroyBlendedSkyInfoDomes(&blended_sky);
		}
		//Find Light Dir
		gfxSkyCalcLightDir(&blended_sky_group);
		//Blend specified values of the sky group into the output
		gfxSkyBlendSkies(&output, &blended_sky_group, active_sky_group->percent, BSDM_STEAL_AND_BLEND);

		gfxSkyDestroyBlendedSkyInfoDomes(&blended_sky_group);
	}

	//Blend the Output Sky into the Visible Sky
	gfxSkyApplyValues(camera_view, &output);

	gfxSkyDestroyBlendedSkyInfoDomes(&output);
}

//////////////////////////////////////////////////////////////////////////
//	Drawing Sky Domes
//////////////////////////////////////////////////////////////////////////

// DJR todo make flare model data-driven?
static Model * quad_model = NULL;

static void gfxDrawLensFlareModel(RdrDrawList *draw_list, Model *model, Mat4 transform,
	Material *override_material, BasicTexture *override_texture, const Vec4 tint_color, int lens_flare_num)
{
	// use LOD 0
	ModelToDraw models[NUM_MODELTODRAWS];
	int model_count, j;
	RdrAddInstanceParams instance_params={0};
	RdrLightParams light_params;
	RdrInstancePerDrawableData per_drawable_data;
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	F32 alpha;
	static MaterialNamedConstant **ea_named_constants = NULL;


	instance_params.force_no_shadow_receive = 1; // don't receive shadows
	instance_params.distance_offset = -256;
	instance_params.frustum_visible = 1;
	instance_params.wireframe = CLAMP(gfx_state.sky_wireframe, 0, 3);
	instance_params.screen_area = 1;
	instance_params.skyDepth = 1;
	setVec3same(instance_params.ambient_multiplier, 1);
	copyVec4(tint_color, instance_params.instance.color);
	alpha = tint_color[3];

	copyMat4(transform, instance_params.instance.world_matrix);
	addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

	// lighting
	gfxGetSunLight(&light_params, false);
	instance_params.light_params = &light_params;

	model_count = gfxDemandLoadModel(model, models, ARRAY_SIZE(models), 0, 1, 0, NULL, model->radius);
	for (j=0; j<model_count; j++)
	{
		int i;
		RdrDrawableGeo *geo_draw;
		MaterialNamedConstant *texxfrm2;

		if (!models[j].geo_handle_primary) {
			assert(0);
			continue;
		}

		instance_params.instance.color[3] = alpha * models[j].alpha;
		if (instance_params.instance.color[3] < 0.001f)
			continue;

		geo_draw = rdrDrawListAllocGeo(draw_list, RTYPE_MODEL, models[j].model, models[j].model->geo_render_info->subobject_count, 0, 0);
		if (!geo_draw)
			continue;

		geo_draw->geo_handle_primary = models[j].geo_handle_primary;

		SETUP_INSTANCE_PARAMS;
		RDRALLOC_SUBOBJECTS(draw_list, instance_params, models[j].model, i);

		// Offset for the z occlusion test.
		// If the named constant doesn't exist, make it.
		if(!ea_named_constants) {
			texxfrm2 = calloc(sizeof(MaterialNamedConstant), 1);
			texxfrm2->name = allocAddString("Texxfrm2");
			eaPush(&ea_named_constants, texxfrm2);
		}
		
		// Set up to use the right "slot" in the Z occlusion test surface.
		texxfrm2 = ea_named_constants[0];
		texxfrm2->value[0] = 0;
		texxfrm2->value[1] = 1;
		texxfrm2->value[2] = 0.25 * (float)lens_flare_num + (0.5/4.0);
		texxfrm2->value[3] = 1;

		for (i = 0; i < geo_draw->subobject_count; ++i)
		{
			unsigned int occlusionIndex = 0;

			RdrMaterial * material = NULL;
			Material *actual_material = override_material ? override_material : models[j].model->materials[i];
			gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], 
				actual_material, 
				NULL, ea_named_constants, NULL, NULL, instance_params.per_drawable_data[i].instance_param, 0.f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);
			material = instance_params.subobjects[i]->material;

			// Find the slot to put the occlusion texture into.
			for(occlusionIndex = 0; occlusionIndex < material->tex_count; occlusionIndex++) {
				if(actual_material->graphic_props.render_info->texture_names[occlusionIndex * 2] == lensFlareOcclusionTextureName) {
					break;
				}
			}
			
			if (material && occlusionIndex < material->tex_count)
			{
				material->textures[ occlusionIndex ] = texDemandLoadFixed(white_tex);
				if (gfxDoingPostprocessing() && gfx_state.settings.lensflare_quality > 1 && lens_flare_num < 4)
				{
					material->surface_texhandle_fixup = 1;
					// force to use the occlusion-generated alpha tex
					material->textures[ occlusionIndex ] = DRAWLIST_DEFINED_TEX__LENSFLARE_ZO_DEPTH_TEX;
				}
			}
		}

		instance_params.add_material_flags |= RMATERIAL_NOZWRITE | RMATERIAL_FORCEFARDEPTH | RMATERIAL_DOUBLESIDED;
		if (!gfx_state.volumeFog)
			instance_params.add_material_flags |= RMATERIAL_NOFOG;

		rdrDrawListAddGeoInstance(draw_list, geo_draw, &instance_params, RST_ALPHA, ROC_SKY, true);

		gfxGeoIncrementUsedCount(models[j].model->geo_render_info, models[j].model->geo_render_info->subobject_count, true);
	}
}

static const F32 DEFAULT_FLARE_SIZE = 128 / 1024.0f;

__forceinline static U32 gfxLensFlareGetFlareCount(const LensFlare *lens_flare)
{
	return eaSize(&lens_flare->flares);
}

void gfxSkyDrawLensFlare(LensFlarePiece **flare_pieces, U32 num_flares, const Frustum *camera_frustum,
	const Vec3 light_pos_ws, const Vec3 tint_color, float alpha_fade, int lens_flare_num)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	Mat4 transform;
	F32 look_fade;
	Vec3 light_v;
	F32 flare_dist;
	U32 flare_component;


	if (!quad_model)
	{
		WLUsageFlags use_flags = WL_FOR_UTIL|WL_FOR_WORLD;
		quad_model = groupModelFind("sys_unit_quad", use_flags);
	}

	// fade alpha based on viewing angle (between light source and the view forward vector)
	subVec3(light_pos_ws, camera_frustum->cammat[3], light_v);

	flare_dist = lengthVec3(light_v);
	look_fade = -dotVec3(light_v, camera_frustum->cammat[2]) / flare_dist;
	if (look_fade <= 0.0f)
		return;

	look_fade *= look_fade;
	look_fade *= look_fade;

	look_fade *= alpha_fade;

	// Now take the camera-to-light vector into view space. Flatten the Z-axis 
	// so we can step along a vector parallel to the screen and place the
	// flare sprites. Then transform back to world space.
	mulVecMat3(light_v, camera_frustum->viewmat, transform[0]);
	flare_dist = -transform[0][2];
	transform[0][2] = 0.0f;
	mulVecMat3(transform[0], camera_frustum->inv_viewmat, light_v);
	scaleVec3(light_v, -2.0f, light_v);

	for (flare_component = 0; flare_component < num_flares; ++flare_component)
	{
		LensFlarePiece * flare_piece = flare_pieces[flare_component];
		F32 flare_size = flare_piece->size;
		F32 pos_offset;
		Vec4 tint;
		if (!flare_size)
			flare_size = DEFAULT_FLARE_SIZE;
		flare_size *= flare_dist;

		if (!flare_piece->material && flare_piece->material_name)
			flare_piece->material = materialFind(flare_piece->material_name, WL_FOR_WORLD);
		if (!flare_piece->texture && flare_piece->texture_name)
			flare_piece->texture = texFindAndFlag(flare_piece->texture_name, 1, WL_FOR_WORLD);

		// camera-facing
		scaleVec3(camera_frustum->cammat[0], -flare_size, transform[0]);
		scaleVec3(camera_frustum->cammat[2], flare_size, transform[1]);
		scaleVec3(camera_frustum->cammat[1], flare_size, transform[2]);

		if (flare_component && flare_piece->position)
			pos_offset = flare_piece->position;
		else
			pos_offset = flare_component / (F32)num_flares;

		scaleAddVec3(light_v, pos_offset, light_pos_ws, transform[3]);
		gfxHsvToRgb(flare_piece->hsv_color, tint);
		mulVecVec3(tint, tint_color, tint);
		tint[3] = look_fade;

		gfxDrawLensFlareModel(gdraw->draw_list, quad_model, transform,
			flare_piece->material, flare_piece->texture, tint, lens_flare_num);
	}
}

//Adds a sky dome to the draw list
static void gfxSkyDrawSkyDome(BlendedSkyInfo *sky_info, BlendedSkyDome *sky_dome, Frustum *camera_frustum, int lens_flare_num)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	ModelToDraw models[NUM_MODELTODRAWS];
	RdrAddInstanceParams instance_params={0};
	RdrLightParams light_params = {0};
	RdrInstancePerDrawableData per_drawable_data;
	Vec3 tint_color;
	int model_count;
	int j;
	RdrDrawList *draw_list;

	if (gdraw->sky_draw_list_filled || !sky_dome->drawable || sky_dome->high_detail && gfxGetLodScale() <= 0.6f)
		return;

	for (j = 0; j < eaSize(&sky_dome->dome->tex_swaps); ++j)
	{
		if (!sky_dome->dome->tex_swaps[j]->texture)
			sky_dome->dome->tex_swaps[j]->texture = texFindAndFlag(sky_dome->dome->tex_swaps[j]->texture_name, true, WL_FOR_WORLD);
	}

	gfxHsvToRgb(sky_dome->dome->current_dome_values.tintHSV, tint_color);

	instance_params.force_no_shadow_receive = 1; // don't receive shadows
	instance_params.distance_offset = sky_dome->sort_order;
	instance_params.frustum_visible = 1;
	instance_params.wireframe = CLAMP(gfx_state.sky_wireframe, 0, 3);
	instance_params.screen_area = 1;
	instance_params.skyDepth = 1;
	setVec3same(instance_params.ambient_multiplier, 1);
	copyVec3(tint_color, instance_params.instance.color);

	copyMat3(sky_dome->drawable->mat, instance_params.instance.world_matrix);
	addVec3(sky_dome->drawable->mat[3], camera_frustum->inv_viewmat[3], instance_params.instance.world_matrix[3]);

	// lighting
	instance_params.light_params = &light_params;
	if (sky_dome->dome->sunlit)
	{
		if (sky_dome->drawable->atmosphere)
		{
			Vec3 sun_diffuse_rgb;
			gfxHsvToRgb(sky_dome->dome->atmosphereSunHSV, sun_diffuse_rgb);
			gfxGetOverrideLight(&light_params, false, sun_diffuse_rgb, sky_info->outputWorldLightDir);
		}
		else
		{
			gfxGetSunLight(&light_params, false);
		}
	}
	else
	{
		gfxGetUnlitLight(&light_params);
	}

	if (sky_dome->dome->current_dome_values.ambient_weight > 0)
	{
		Vec3 ambient_rgb;
		gfxHsvToRgb(sky_dome->dome->current_dome_values.ambientHSV, ambient_rgb);
		lerpVec3(ambient_rgb, sky_dome->dome->current_dome_values.ambient_weight, light_params.ambient_light->ambient[RLCT_WORLD], ambient_rgb); // note that lerpVec3 works backwards from lerp and lerpHSV
		light_params.ambient_light = gfxGetOverrideAmbientLight(ambient_rgb, zerovec3, zerovec3, zerovec3);
	}

	if (sky_dome->drawable->material && gfxMaterialNeedsScreenGrab(sky_dome->drawable->material))
		draw_list = gdraw->draw_list;
	else
		draw_list = gdraw->draw_list_sky;

	if (sky_dome->drawable->model)
	{
		WorldAtmosphereProperties *atmosphere = sky_dome->drawable->atmosphere;
		int mat_prop_size = eaSize(&sky_dome->dome->current_dome_values.mat_props);

		if (atmosphere)
		{
			atmosphereBuildDynamicConstantSwaps(atmosphere->atmosphere_thickness, atmosphere->planet_radius + atmosphere->atmosphere_thickness, &sky_dome->dome->current_dome_values.mat_props);
			scaleMat3(unitmat, instance_params.instance.world_matrix, atmosphere->planet_radius + atmosphere->atmosphere_thickness);
			instance_params.instance.world_matrix[3][1] -= atmosphere->planet_radius;
		}

		// use LOD 0
		model_count = gfxDemandLoadModel(sky_dome->drawable->model, models, ARRAY_SIZE(models), 0, 1, 0, NULL, sky_dome->drawable->model->radius);
		for (j=0; j<model_count; j++)
		{
			int i;
			RdrDrawableGeo *geo_draw;
			int subobject_count;

			if (!models[j].geo_handle_primary) {
				assert(0);
				continue;
			}

			instance_params.instance.color[3] = sky_dome->alpha * models[j].alpha;
			if (instance_params.instance.color[3] < 0.001f)
				continue;

			subobject_count = models[j].model->geo_render_info->subobject_count;
			if (!sky_dome->drawable->material)
			{
				int iNeedGrabCount=0;
				for (i=0; i<subobject_count; i++)
				{
					if (gfxMaterialNeedsScreenGrab(models[j].model->materials[i]))
						iNeedGrabCount++;
				}
				if (iNeedGrabCount == 0)
					draw_list = gdraw->draw_list_sky;
				else if (iNeedGrabCount == subobject_count)
					draw_list = gdraw->draw_list;
				else {
					Errorf("Sky contains a dome %s which has some subobjects with refraction, and some without - not allowed", sky_dome->dome->name);
					draw_list = gdraw->draw_list;
				}
			}

			geo_draw = rdrDrawListAllocGeo(draw_list, RTYPE_MODEL, models[j].model, subobject_count, 0, 0);

			if (!geo_draw)
				continue;

			geo_draw->geo_handle_primary = models[j].geo_handle_primary;

			SETUP_INSTANCE_PARAMS;
			RDRALLOC_SUBOBJECTS(draw_list, instance_params, models[j].model, i);

			for (i = 0; i < subobject_count; ++i)
			{
				gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], 
					sky_dome->drawable->material?sky_dome->drawable->material:models[j].model->materials[i], 
					NULL, 
					sky_dome->dome->current_dome_values.mat_props, 
					sky_dome->dome->tex_swaps, 
					NULL,
					instance_params.per_drawable_data[i].instance_param, 0.f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);
			}

			instance_params.add_material_flags |= RMATERIAL_NOZWRITE | RMATERIAL_FORCEFARDEPTH;
			if (!gfx_state.volumeFog)
				instance_params.add_material_flags |= RMATERIAL_NOFOG;

			if (nearSameVec3(sky_dome->drawable->mat[3], zerovec3) && !sky_dome->dome->pos_specified)
				instance_params.camera_centered = 1;

			rdrDrawListAddGeoInstance(draw_list, geo_draw, &instance_params, RST_ALPHA, ROC_SKY, true);

			gfxGeoIncrementUsedCount(models[j].model->geo_render_info, models[j].model->geo_render_info->subobject_count, true);
		}

		eaSetSize(&sky_dome->dome->current_dome_values.mat_props, mat_prop_size);

		if (sky_dome->dome->lens_flare && gfx_state.settings.lensflare_quality > 0)
		{
			SkyDrawable * drawable = sky_dome->drawable;
			Vec3 light_pos_ws;
			LensFlare * lens_flare = sky_dome->dome->lens_flare;
			addVec3(drawable->mat[3], camera_frustum->inv_viewmat[3], light_pos_ws);

			gdraw->num_sky_lens_flares++;

			gfxSkyDrawLensFlare(lens_flare->flares, gfxLensFlareGetFlareCount(lens_flare),
				camera_frustum, light_pos_ws, tint_color, sky_dome->alpha, lens_flare_num);
		}
	}
	else if (sky_dome->drawable->star_field && sky_dome->drawable->material)
	{
		int tri_count;
		RdrDrawableGeo *geo_draw;
		Vec4 starfield_param;
		bool camera_facing = false;
		GeoHandle geo_handle_primary;

		instance_params.instance.color[3] = sky_dome->alpha;
		if (instance_params.instance.color[3] < 0.001f)
			return;

		geo_handle_primary = gfxDemandLoadStarField(sky_dome->drawable, gfxGetActiveCameraFOV(), &tri_count, starfield_param, &camera_facing);

		if (!geo_handle_primary) {
			assert(0);
			return;
		}

		instance_params.instance.camera_facing = !!camera_facing;

		geo_draw = rdrDrawListAllocGeo(draw_list, RTYPE_STARFIELD, NULL, 1, 1, 0);

		geo_draw->geo_handle_primary = geo_handle_primary;
		copyVec4(starfield_param, geo_draw->vertex_shader_constants[0]);

		instance_params.per_drawable_data = &per_drawable_data;

		RDRALLOC_SUBOBJECT_PTRS(instance_params, 1);
		instance_params.subobjects[0] = rdrDrawListAllocSubobject(draw_list, tri_count);

		gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[0], 
			sky_dome->drawable->material, 
			NULL, 
			sky_dome->dome->current_dome_values.mat_props, 
			sky_dome->dome->tex_swaps, 
			NULL, 
			instance_params.per_drawable_data[0].instance_param, 0.f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		instance_params.add_material_flags |= RMATERIAL_NOZWRITE | RMATERIAL_FORCEFARDEPTH;
		if (!gfx_state.volumeFog)
			instance_params.add_material_flags |= RMATERIAL_NOFOG;

		if (nearSameVec3(sky_dome->drawable->mat[3], zerovec3) && !sky_dome->dome->pos_specified)
			instance_params.camera_centered = 1;

		// Would not expect a starfield to use a normal map, and we hardcode that in the vertex shader
		devassert(instance_params.subobjects[0]->material->no_normalmap);

		rdrDrawListAddGeoInstance(draw_list, geo_draw, &instance_params, RST_ALPHA, ROC_SKY, true);
	}
}

//Queue up sky domes to be drawn
void gfxSkyQueueDrawables(GfxCameraView *camera_view)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	GfxSkyData *sky_data = camera_view->sky_data;
	BlendedSkyInfo *visible_sky = sky_data->visible_sky;

	//Draw SkyDomes
	if (!gfx_state.debug.no_sky && !gdraw->sky_draw_list_filled)
	{
		RdrDrawListLightingMode light_mode;
		int i;
		int lensFlareNum = 0;

		if (gfx_state.vertexOnlyLighting)
			light_mode = RDLLM_VERTEX_ONLY;
		else if (gfx_state.uberlighting)
			light_mode = RDLLM_UBERLIGHTING;
		else if (!rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_SM30) || gfx_state.debug.simpleLighting)
			light_mode = RDLLM_SIMPLE;
		else if (gfx_state.cclighting && rdrMaxSupportedObjectLights() == MAX_NUM_OBJECT_LIGHTS)
			light_mode = RDLLM_CCLIGHTING;
		else
			light_mode = RDLLM_NORMAL;

		//Setup Draw List for adding sky domes later
		rdrDrawListBeginFrame(gdraw->draw_list_sky, false, false, false, 
			gfx_state.debug.sort_opaque, gdraw->do_shadow_buffer,
			gfx_state.shadow_buffer && ((gfxFeatureEnabled(GFEATURE_SHADOWS) && !gfx_state.vertexOnlyLighting) || gdraw->do_ssao),
			gdraw->do_ssao,
			light_mode,
			gdraw->do_hdr_pass, gdraw->has_hdr_texture, camera_view->adapted_light_range, 8e16, 1, 0);

		rdrDrawListAddPass(gdraw->draw_list_sky, &camera_view->frustum, RDRSHDM_VISUAL, NULL, 0, -1, false);

		for (i=0; i < visible_sky->skyDomeCount ; i++) {
			gfxSkyDrawSkyDome(visible_sky, visible_sky->skyDomes[i], &camera_view->frustum, lensFlareNum);
			if(visible_sky->skyDomes[i]->dome->lens_flare) lensFlareNum++;
		}

		gdraw->sky_draw_list_filled = true;
	}
}

//Draw all sky drawables
void gfxSkyDraw(GfxSkyData *sky_data, bool hdr_pass)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;

	if (!gfx_state.debug.no_sky && gdraw->draw_list_sky)
	{
		F32 aspect = (F32)gfx_activeSurfaceSize[0] / gfx_activeSurfaceSize[1];

		// Draw the drawables (queued in gfxSkyQueueDrawables)
		gfxBeginSection(__FUNCTION__);

		rdrDrawListSendVisualPassesToRenderer(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, gfxGetLightDefinitionArray(rdrGetDeviceIdentifier(gfx_state.currentDevice->rdr_device)));
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_OPAQUE_ONEPASS, hdr_pass, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_DEFERRED, hdr_pass, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_NONDEFERRED, hdr_pass, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_ALPHA_PREDOF_NEEDGRAB, hdr_pass, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_ALPHA_PREDOF, hdr_pass, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_ALPHA, hdr_pass, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_ALPHA_LOW_RES_ALPHA, hdr_pass, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_ALPHA_LOW_RES_ADDITIVE, hdr_pass, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_ALPHA_LOW_RES_SUBTRACTIVE, hdr_pass, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_ALPHA_LATE, hdr_pass, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_sky, RST_ALPHA_POST_GRAB_LATE, hdr_pass, false, false);

		rdrDrawListAddStats(gdraw->draw_list_sky, &gfx_state.debug.frame_counts.draw_list_stats);

		gfxEndSection();
	}
}

//////////////////////////////////////////////////////////////////////////
//	Fixup Functions
//////////////////////////////////////////////////////////////////////////

//Given a SkyInfo sub-structure, sort the values by time
bool gfxSkySort(void **data, int array_size)
{
	int i,j;
	bool ret=true;
	for(i=0; i<array_size; i++)
	{
		for(j=i+1; j<array_size; j++)
		{
			F32 j_time = gfxSkyRepairTime(GET_SKY_TIME(data + j));
			if(j_time < GET_SKY_TIME(data + i))
			{
				eaSwap(&data,i,j);
			}
			else if(j_time == GET_SKY_TIME(data + i))
			{	
				//Errors will be generated so just repair the time for now. 
				GET_SKY_TIME(data + j) = j_time + 0.0001;
				ret=false;
			}
		}
		GET_SKY_TIME(data + i) = gfxSkyRepairTime(GET_SKY_TIME(data + i));
	}
	return ret;
}

//Sort sky values by time, and throw errors if duplicates are found
void gfxSkyValuesSortSkyInfo(ParseTable pti[], U32 flag, SkyInfo *sky, BlendedSkyInfo *blended_sky, void **sky_values, void *blended_sky_values, void *unused)
{
	if(!gfxSkySort(sky_values, eaSize(&sky_values)))
		Alertf("Warning: %s has duplicate %s times.", sky->filename, ParserGetTableName(pti));
}

//Fill a BlendedSkyInfo with the values from the default sky.
void gfxSkyValuesGetDefaultData(ParseTable pti[], U32 flag, SkyInfo *sky, BlendedSkyInfo *blended_sky, void **sky_values, void *blended_sky_values, void *unused)
{
	if(eaSize(&sky_values) < 1)
	{
		Errorf("Error: The Default Sky is missing values for %s", ParserGetTableName(pti));
		return;
	}

	StructCopyAllVoid(pti, sky_values[0], blended_sky_values);
}

//Apply and non specified values from the default sky to this sky
void gfxSkyValuesFillWithDefaultData(ParseTable pti[], U32 flag, SkyInfo *sky, BlendedSkyInfo *blended_sky, void **sky_values, void *default_sky_values, void *unused)
{
	int i;
	if(!sky_values)
		return;

	for( i=0; i < eaSize(&sky_values); i++ )
	{
		StructApplyDefaults(pti, sky_values[i], default_sky_values, 0, 1, false);
	}
}

//Ensures that material lists inside a skyDome look similar for easier blending later
void gfxSkyFixupSkyDomeMaterials(SkyDome *sky_dome)
{
	int j, k, l;

	//Destroy old mats if any
	if(sky_dome->current_dome_values.mat_props)
		eaDestroyEx(&sky_dome->current_dome_values.mat_props, gfxSkyDestroyMaterialNamedConstant);
	sky_dome->current_dome_values.mat_props = NULL;

	//Find all the mats used by this skyDome and put into a main list
	for(j=0; j < eaSize(&sky_dome->dome_values); j++)
	{
		//Sort this mat list
		eaQSort(sky_dome->dome_values[j]->mat_props, gfxSkyCompareMaterials);

		//For each mat
		for(k=0; k < eaSize(&sky_dome->dome_values[j]->mat_props); k++)
		{			
			//Look for existing mat
			for(l=0; l < eaSize(&sky_dome->current_dome_values.mat_props); l++)
			{
				//If found then no need to add
				if(sky_dome->dome_values[j]->mat_props[k]->name == sky_dome->current_dome_values.mat_props[l]->name)
					break;
			}

			//If not found, then add
			if(l >= eaSize(&sky_dome->current_dome_values.mat_props))
			{
				MaterialNamedConstant *newMat = StructCreate(parse_MaterialNamedConstant);
				StructCopyAll(parse_MaterialNamedConstant, sky_dome->dome_values[j]->mat_props[k], newMat);
				setVec4(newMat->value,0,0,0,0);//TODO: Change to find Defaults
				eaPush(&sky_dome->current_dome_values.mat_props, newMat);
			}
		}
	}
	//Sort main list
	eaQSort(sky_dome->current_dome_values.mat_props, gfxSkyCompareMaterials);

	//Make all lists look the same
	for(j=0; j < eaSize(&sky_dome->dome_values); j++)
	{
		//For each mat in the main list
		for(l=0; l < eaSize(&sky_dome->current_dome_values.mat_props); l++)
		{
			//If missing, then add
			if(l >= eaSize(&sky_dome->dome_values[j]->mat_props) || sky_dome->dome_values[j]->mat_props[l]->name != sky_dome->current_dome_values.mat_props[l]->name)
			{
				MaterialNamedConstant *newMat = StructCreate(parse_MaterialNamedConstant);
				StructCopyAll(parse_MaterialNamedConstant, sky_dome->current_dome_values.mat_props[l], newMat);
				eaInsert(&sky_dome->dome_values[j]->mat_props, newMat,l);
			}
		}
	}
}

//Ensure we have a valid SkyDome data and fix up what we can
void gfxSkyFixupSkyDomes(SkyInfo *sky_info)
{
	int i, j;
	const char *blame_filename = sky_info->filename;

	//For each SkyDome
	for (i=eaSize(&sky_info->skyDomes)-1; i>=0; i--)
	{
		char buf[255];
		SkyDome *sky_dome = sky_info->skyDomes[i];
		sky_dome->bHasErrors = false;

		//Validate name
		if (!sky_dome->name) 
		{
			ErrorFilenamef(blame_filename, "SkyDome does not reference any geometry.");
			sky_dome->bHasErrors = true;
			continue;
		} 
		else if (sky_dome->star_field) 
		{
			// Verify the the material exists.
			if (!(wlGetLoadFlags() & WL_NO_LOAD_MATERIALS) && !materialExists(sky_dome->name))
			{
				ErrorFilenamef(blame_filename, "SkyDome references non-existent star field material \"%s\"", sky_dome->name);
				sky_dome->bHasErrors = true;
				continue;
			}

			if (sky_dome->star_field->count >= 16384)
			{
				// More than 16k apparently loops around in the vertex buffer or something
				ErrorFilenamef(blame_filename, "SkyDome %s StarField has count > 16k", sky_dome->name);
			}

			//Apply default StarField values for those required.
			//if (!TokenIsSpecified(parse_StarField, star_field_color_min_column, sky_dome->star_field, star_field_usedfield_column))
			//	setVec3(sky_dome->star_field->color_min, 0, 0, 1);
			//if (!TokenIsSpecified(parse_StarField, star_field_color_max_column, sky_dome->star_field, star_field_usedfield_column))
			//	setVec3(sky_dome->star_field->color_max, 0, 0, 1);
			//if (!TokenIsSpecified(parse_StarField, star_field_slice_axis_column, sky_dome->star_field, star_field_usedfield_column))
			//	setVec3(sky_dome->star_field->slice_axis, 0, 1, 0);
		}
		else if (sky_dome->atmosphere)
		{
			sky_dome->sunlit = true;
//			if (!TokenIsSpecified(parse_SkyDome, sky_dome_atmosphere_sun_hsv_column, sky_dome, sky_dome_usedfield_column))
//				setVec3(sky_dome->atmosphereSunHSV, 0, 0, 2);

			// Verify the the _Sky_Atmosphere object exists.
			if(!(wlGetLoadFlags() & WL_NO_LOAD_MODELS) && !modelExists("_Sky_Atmosphere"))
			{
				ErrorFilenamef(blame_filename, "SkyDome \"%s\" of type Atmosphere depends upon non-existent geometry \"_Sky_Atmosphere\" in \"%s\"", sky_dome->name, blame_filename);
				sky_dome->bHasErrors = true;
				continue;
			}
		}
		else
		{
			// Verify the the object exists.
			if(!(wlGetLoadFlags() & WL_NO_LOAD_MODELS) && !modelExists(sky_dome->name))
			{
				ErrorFilenamef(blame_filename, "SkyDome references non-existent geometry \"%s\"", sky_dome->name);
				sky_dome->bHasErrors = true;
				continue;
			}
		}

		//Add UID
		sprintf(buf, "%s_%s_%g", sky_info->filename_no_path, sky_dome->name, sky_dome->sort_order);
		sky_dome->sky_uid = allocAddString(buf);

		//Unify Material Lists
		gfxSkyFixupSkyDomeMaterials(sky_info->skyDomes[i]);

		//Apply default Sky Dome Time Values
		for(j=0; j < eaSize(&sky_dome->dome_values); j++)
		{
//			if (!TokenIsSpecified(parse_SkyDomeTime, sky_dome_time_alpha_column, sky_dome->dome_values[j], sky_dome_time_usedfield_column))
//				sky_dome->dome_values[j]->alpha = 1.0f;
//			if (!TokenIsSpecified(parse_SkyDomeTime, sky_dome_time_scale_column, sky_dome->dome_values[j], sky_dome_time_usedfield_column))
//				sky_dome->dome_values[j]->scale = 1.0f;
//			if (!TokenIsSpecified(parse_SkyDomeTime, sky_dome_time_tint_hsv_column, sky_dome->dome_values[j], sky_dome_time_usedfield_column))
//				setVec3(sky_dome->dome_values[j]->tintHSV, 0, 0, 1);
			if (TokenIsSpecified(parse_SkyDomeTime, sky_dome_time_ambient_hsv_column, sky_dome->dome_values[j], sky_dome_time_usedfield_column))
				sky_dome->dome_values[j]->ambient_weight = 1;
			else
				sky_dome->dome_values[j]->ambient_weight = 0;

			if (sky_dome->motion_loop_cnt > 0 || TokenIsSpecified(parse_SkyDomeTime, sky_dome_time_pos_column, sky_dome->dome_values[j], sky_dome_time_usedfield_column))
				sky_dome->pos_specified = true;
		}

		//Apply general default values
		if (!TokenIsSpecified(parse_SkyDome, sky_dome_rot_axis_column, sky_dome, sky_dome_usedfield_column))
			setVec3(sky_dome->rotation_axis, 0, 0, 1);


		//Sort the info by time, and throw errors if duplicates are found
		if(!gfxSkySort(sky_dome->dome_values, eaSize(&sky_info->skyDomes[i]->dome_values)))
			Alertf("Warning: %s has duplicate SkyDome times for %s.", sky_info->filename, (sky_dome->name ? sky_dome->name : "a SkyDome"));

		// Look for domes with only 1 time, and that 1 time has alpha of 0
		if (eaSize(&sky_dome->dome_values)==1 && sky_dome->dome_values[0]->alpha == 0)
		{
			ErrorFilenamef(blame_filename, "SkyDome %s has only one SkyDomeTime and that one time has an alpha of 0, nothing will be rendered.",
				sky_dome->name);
		}
	}

	// Look for domes with identical sort orders
	for (i=0; i<eaSize(&sky_info->skyDomes); i++)
	{
		for (j=i+1; j<eaSize(&sky_info->skyDomes); j++)
		{
			if (nearSameF32(sky_info->skyDomes[i]->sort_order, sky_info->skyDomes[j]->sort_order))
			{
				ErrorFilenamef(blame_filename, "SkyDomes %s and %s have identical sort orders.",
					sky_info->skyDomes[i]->name, sky_info->skyDomes[j]->name);
			}
		}
	}
}

//Ensure we have a valid SkyInfo and fix up what we can
void gfxSkyFixup(SkyInfo *sky_info)
{
	SkyInfo *default_sky;
	int i;

	if(!sky_info)
		return;

	//Get the default sky
	default_sky = gfxSkyFindSky(DEFAULT_SKY);
	if(!default_sky)
	{
		Errorf("Error: Cannot find the default sky. File Name: %s", DEFAULT_SKY);
		return;
	}

	//Sort sky values by time, and throw errors if duplicates are found
	gfxSkyDoOperationOnSky(gfxSkyValuesSortSkyInfo, sky_info, NULL, NULL);

	for (i = 0; i < eaSize(&sky_info->bloomValues); ++i)
	{
		if (!TokenIsSpecified(parse_SkyTimeBloom, bloom_low_start_column, sky_info->bloomValues[i], bloom_usedfield_column))
			sky_info->bloomValues[i]->lowQualityBloomStart = 0.95f;
		if (!TokenIsSpecified(parse_SkyTimeBloom, bloom_low_multi_column, sky_info->bloomValues[i], bloom_usedfield_column))
			sky_info->bloomValues[i]->lowQualityBloomMultiplier = 3.5f;
		if (!TokenIsSpecified(parse_SkyTimeBloom, bloom_low_pow_column, sky_info->bloomValues[i], bloom_usedfield_column))
			sky_info->bloomValues[i]->lowQualityBloomPower = 1.5f;
	}

	for (i = 0; i < eaSize(&sky_info->fogValues); ++i)
	{
		if (!TokenIsSpecified(parse_SkyTimeFog, fog_clipFogColorHSV_column, sky_info->fogValues[i], fog_usedfield_column))
			copyVec3(sky_info->fogValues[i]->lowFogColorHSV, sky_info->fogValues[i]->clipFogColorHSV);
		if (!TokenIsSpecified(parse_SkyTimeFog, fog_clipBackgroundColorHSV_column, sky_info->fogValues[i], fog_usedfield_column))
			copyVec3(sky_info->fogValues[i]->clipFogColorHSV, sky_info->fogValues[i]->clipBackgroundColorHSV);
	}

	for (i=0; i<eaSize(&sky_info->dofValues); i++)
	{
		if (!TokenIsSpecified(parse_DOFValues, dof_depthadjust_fg_column, sky_info->dofValues[i], dof_usedfield_column))
			setVec3(sky_info->dofValues[i]->depthAdjustFgHSV, 0, 1, 1);
		if (!TokenIsSpecified(parse_DOFValues, dof_depthadjust_bg_column, sky_info->dofValues[i], dof_usedfield_column))
			setVec3(sky_info->dofValues[i]->depthAdjustBgHSV, 0, 1, 1);
		if (!TokenIsSpecified(parse_DOFValues, dof_depthadjust_sky_column, sky_info->dofValues[i], dof_usedfield_column))
			setVec3(sky_info->dofValues[i]->depthAdjustSkyHSV, 0, 1, 1);
	}

	//Fixup the SkyDomes
	gfxSkyFixupSkyDomes(sky_info);
}

void gfxSkyValidateDefaultSky(ParseTable pti[], U32 flag, SkyInfo *sky, BlendedSkyInfo *blended_sky, void **sky_values, void *blended_sky_values, F32 *time)
{
	//Some blocks are ok if they all default to zero, so we can ignore them.
	if(pti == parse_SkyTimeCharacterLighting)
		return;
	if(pti == parse_SkyTimeWind)
		return;
	if(pti == parse_SkyTimeScattering)
		return;
	if(pti == parse_SkyTimeSecondarySun)
		return;
	if(pti == parse_ShadowRules)
		return;
	
	if(eaSize(&sky_values) == 0) {
		ErrorFilenamef(sky->filename, "Default Sky is missing %s data.", pti->name);
	}
}

//Fills in the filename_no_path of the sky during load
AUTO_FIXUPFUNC;
TextParserResult gfxSkyFixupFunc(SkyInfo *sky, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			char name[256];
			char *pathStart;
			char *nameStart;

			if (sky->filename && !sky->bFilenameNoPathShouldNotBeOverwritten)
			{
				getFileNameNoExt(name, sky->filename);
				sky->filename_no_path = allocAddString(name);
			
				strcpy(name, sky->filename);
				forwardSlashes(name);
				pathStart = name;
				if(strStartsWith(name, "Environment/Skies/"))
					pathStart += 18;
				nameStart = getFileName(pathStart);
				if(nameStart != pathStart && *(nameStart-1) == '/')
					nameStart--;
				nameStart[0] = '\0';
				sky->scope = allocAddString(pathStart);
			}
		}
		if(stricmp(sky->filename_no_path, DEFAULT_SKY)==0) {
			gfxSkyDoOperationOnSky(gfxSkyValidateDefaultSky, sky, NULL, NULL);
		}
		// do some validation
		if (sky->cloudShadowTexture && !texFind(sky->cloudShadowTexture, false))
		{
			ErrorFilenamef(sky->filename, "Reference to non-existent texture \"%s\"", sky->cloudShadowTexture);
		}
		if (sky->diffuseWarpTextureCharacter)
		{
			BasicTexture *tex;
			if (!(tex = texFind(sky->diffuseWarpTextureCharacter, false)))
			{
				ErrorFilenamef(sky->filename, "Reference to non-existent texture \"%s\"", sky->diffuseWarpTextureCharacter);
			} else {
				if (!(tex->bt_texopt_flags & TEXOPT_CLAMPS))
				{
					ErrorFilenamef(sky->filename, "Texture \"%s\" is being used for a DiffuseWarp, but does not have the CLAMPS TexOpt enabled", sky->diffuseWarpTextureCharacter);
				}
			}
		}
		if (sky->diffuseWarpTextureWorld)
		{
			BasicTexture *tex;
			if (!(tex=texFind(sky->diffuseWarpTextureWorld, false)))
			{
				ErrorFilenamef(sky->filename, "Reference to non-existent texture \"%s\"", sky->diffuseWarpTextureWorld);
			} else {
				if (!(tex->bt_texopt_flags & TEXOPT_CLAMPS))
				{
					ErrorFilenamef(sky->filename, "Texture \"%s\" is being used for a DiffuseWarp, but does not have the CLAMPS TexOpt enabled", sky->diffuseWarpTextureWorld);
				}
			}
		}
		if (sky->ambientCube)
		{
			BasicTexture *tex = texFind(sky->ambientCube, false);
			if (!tex)
			{
				ErrorFilenamef(sky->filename, "Reference to non-existent texture \"%s\"", sky->ambientCube);
			} else if (!texIsCubemap(tex))
			{
				ErrorFilenamef(sky->filename, "Reference to non-cubemap texture \"%s\" for an ambient cube", sky->ambientCube);
			}
		}
		if (sky->reflectionCube)
		{
			BasicTexture *tex = texFind(sky->reflectionCube, false);
			if (!tex)
			{
				ErrorFilenamef(sky->filename, "Reference to non-existent texture \"%s\"", sky->reflectionCube);
			} else if (!texIsCubemap(tex))
			{
				ErrorFilenamef(sky->filename, "Reference to non-cubemap texture \"%s\" for a reflection cube", sky->reflectionCube);
			}
		}
	}
	return PARSERESULT_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
//	Loading and Reloading Functions
//////////////////////////////////////////////////////////////////////////

//Send a message to all GfxSkyData to clear the visible sky
void gfxSkyClearAllVisibleSkies(void)
{
	sky_needs_reload++;
}

//Causes a GfxSkyData to clear its visible sky
void gfxSkyClearVisibleSky(GfxSkyData *sky_data)
{
	sky_data->sky_needs_reload--;
}

//Reload Skies Callback for when Skies are touched outside the game.
void gfxSkyReloadSkiesCB(const char* relpath, int when)
{
	loadstart_printf( "Reloading Sky Info (%s)...", relpath);

	//Reload the sky
	fileWaitForExclusiveAccess( relpath );
	errorLogFileIsBeingReloaded(relpath);
	ParserReloadFileToDictionary( relpath, gfx_sky_dict );

	//Clear the Visible Skies
	gfxSkyClearAllVisibleSkies();

	loadend_printf( " done." );

	//If we just reloaded the default sky, reload all the skies so that the defaults will be applied correctly
	if (!reloading_all_skies && strEndsWith(relpath, "/default_sky.sky"))
		gfxSkyReloadAllSkies();
}

//Callback used for when gfxSkyReloadAllSkies is called
//Reloads a sky
static FileScanAction gfxSkyLoadSkiesCB(char *dir, struct _finddata32_t *data, void *pUserData)
{
	static char *ext = ".sky";
	static int ext_len = 4; // strlen(ext);
	char filename[MAX_PATH];
	int len;

	if (data->name[0]=='_') return FSA_NO_EXPLORE_DIRECTORY;
	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;
	len = (int)strlen(data->name);
	if (len<ext_len || strnicmp(data->name + len - ext_len, ext, ext_len)!=0)
		return FSA_EXPLORE_DIRECTORY;

	STR_COMBINE_SSS(filename, dir, "/", data->name);

	// Do actual processing
	gfxSkyReloadSkiesCB(filename, 0);

	return FSA_EXPLORE_DIRECTORY;
}

//Causes all skies to be reloaded from disk
void gfxSkyReloadAllSkies(void)
{
	reloading_all_skies = true;
	fileScanAllDataDirs("environment/skies", gfxSkyLoadSkiesCB, NULL);
	reloading_all_skies = false;
}

//Reload Skies Callback for when scene files are touched outside the game.
static void gfxSkySceneReloadedCB(void)
{
	worldUpdateBounds(true, true);
	gfxSkyClearAllVisibleSkies();
}

static int skyInfo_ValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, SkyInfo *pSkyInfo, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
			gfxSkyFixup(pSkyInfo);
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void gfxInitSkyDictionary(void)
{
	gfx_sky_dict = RefSystem_RegisterSelfDefiningDictionary(GFX_SKY_DICTIONARY, false, parse_SkyInfo, true, true, "Sky");
}

//Initial loading of the data for skies
void gfxSkyLoadSkies(void)
{
	int sky_count;

	resDictMaintainInfoIndex(gfx_sky_dict, ".FNNoPath", ".Scope", ".Tags", ".Notes", NULL);

	loadstart_printf("Loading Sky Info...");

	//Load SkyInfos
	ParserLoadFilesToDictionary( "environment/skies", ".sky", "Skies.bin", PARSER_BINS_ARE_SHARED | RESOURCELOAD_USERDATA, gfx_sky_dict );
	FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "environment/skies/*.sky", gfxSkyReloadSkiesCB );

	// do this last because the validate wants object library pieces loaded
	resDictManageValidation(gfx_sky_dict, skyInfo_ValidateCB);

	sky_count = RefSystem_GetDictionaryNumberOfReferents( gfx_sky_dict );

	loadend_printf( " done (%d Skies).", sky_count );
}

void gfxSkyValidateSkies(void)
{
	RefDictIterator it;
	SkyInfo *sky;

	//Validate SkyInfos
	RefSystem_InitRefDictIterator(GFX_SKY_DICTIONARY, &it);
	while (sky = (SkyInfo*)RefSystem_GetNextReferentFromIterator(&it))
		gfxSkyFixup(sky);
}

char **gfxGetAllSkyNames(bool duplicate_strings)
{
	RefDictIterator it;
	SkyInfo *sky;
	char **names = NULL;

	RefSystem_InitRefDictIterator(GFX_SKY_DICTIONARY, &it);
	while(sky = (SkyInfo*)RefSystem_GetNextReferentFromIterator(&it))
	{
		eaPush(&names, duplicate_strings?strdup(sky->filename_no_path):sky->filename_no_path);
	}

	eaQSortG(names, strCmp);

	return names;
}

//////////////////////////////////////////////////////////////////////////

/*
#include "fileutil2.h"
#include "../StaticWorld/WorldGridLoadPrivate.h"
#include "GfxAtmospherics.h"

static void poolLibFileLoadAtmosphere(LibFileLoad *file, const char *filename)
{
	static const char *s_default_atmosphere;
	bool changed = false;
	int i, j;

	if (!file)
		return;

	if (fileCoreDataDir())
	{
		char fullname[MAX_PATH];
		fileLocateWrite(filename, fullname);
		if (strStartsWith(fullname, fileCoreDataDir()))
		{
			StructDestroy(parse_LibFileLoad, file);
			return;
		}
	}

	if (!s_default_atmosphere)
		s_default_atmosphere = allocAddString("Sky_Atmosphere_Default_voltex");

	for (i = 0; i < eaSize(&file->defs); ++i)
	{
		DefLoad *def = file->defs[i];
		if (def->planet_properties && def->planet_properties->has_atmosphere)
		{
			const char *tex_name = gfxAtmospherePoolAtmosphere(&def->planet_properties->atmosphere, false, filename);
			TexSwapLoad *tex_swap = NULL;

			for (j = eaSize(&def->tex_swap) - 1; j >= 0; --j)
			{
				if (def->tex_swap[j]->orig_swap_name == s_default_atmosphere)
				{
					if (tex_swap)
					{
						StructDestroy(parse_TexSwapLoad, def->tex_swap[j]);
						eaRemove(&def->tex_swap, j);
					}
					else
					{
						tex_swap = def->tex_swap[j];
					}
				}
			}

			if (!tex_swap)
			{
				tex_swap = StructAlloc(parse_TexSwapLoad);
				tex_swap->orig_swap_name = s_default_atmosphere;
				eaPush(&def->tex_swap, tex_swap);
			}

			tex_swap->rep_swap_name = tex_name; // already pooled
			changed = true;
		}
	}

	if (changed)
	{
		GimmeErrorValue ret = gimmeDLLDoOperation(filename, GIMME_CHECKOUT, GIMME_QUIET);
		if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_ALREADY_DELETED)
		{
			printf("Unable to checkout file %s.\n", filename);
		}
		else
		{
			ParserWriteTextFile(filename, parse_LibFileLoad, file, 0, 0);
		}
	}

	StructDestroy(parse_LibFileLoad, file);
}

// TomY - I'm breaking this, but it appears to be gone already.
static void poolMapAtmosphere(ZoneMapInfo *zminfo)
{
	int k;

	for (k = 0; k < zmapGetLayerCount(zmap); ++k)
	{
		const char *filename;
		LibFileLoad *file;

		ZoneMapLayer *layer = zmapGetLayer(zmap, k);
		if (!layer)
			continue;

		filename = layerGetFilename(layer);

		file = loadLayerFromDisk(filename);
		poolLibFileLoadAtmosphere(file, filename);
	}
}

void gfxPoolAtmospheres(void)
{
	const char *s_Atmos = allocAddString("Atmos");
	const char *s_AtmosphericScatterLUT = allocAddString("AtmosphericScatterLUT");
	RefDictIterator it;
	SkyInfo *sky;
	int i, count;
	int j;

	gfxAtmosphereLoadAtmospheres();

	RefSystem_InitRefDictIterator(GFX_SKY_DICTIONARY, &it);
	while (sky = (SkyInfo*)RefSystem_GetNextReferentFromIterator(&it))
	{
		bool changed = false;

		if (fileCoreDataDir())
		{
			char fullname[MAX_PATH];
			fileLocateWrite(sky->filename, fullname);
			if (strStartsWith(fullname, fileCoreDataDir()))
				continue;
		}					

		for (i = 0; i < eaSize(&sky->skyDomes); ++i)
		{
			SkyDome *dome = sky->skyDomes[i];
			if (dome->atmosphere)
			{
				const char *tex_name = gfxAtmospherePoolAtmosphere(dome->atmosphere, true, sky->filename);
				MaterialNamedTexture *tex_swap = NULL;

				for (j = eaSize(&dome->tex_swaps) - 1; j >= 0; --j)
				{
					if (dome->tex_swaps[j]->op == s_Atmos && dome->tex_swaps[j]->input == s_AtmosphericScatterLUT)
					{
						if (tex_swap)
						{
							StructDestroy(parse_MaterialNamedTexture, dome->tex_swaps[j]);
							eaRemove(&dome->tex_swaps, j);
						}
						else
						{
							tex_swap = dome->tex_swaps[j];
						}
					}
				}

				if (!tex_swap)
				{
					tex_swap = StructAlloc(parse_MaterialNamedTexture);
					tex_swap->op = s_Atmos;
					tex_swap->input = s_AtmosphericScatterLUT;
					eaPush(&dome->tex_swaps, tex_swap);
				}

				tex_swap->texture_name = tex_name;

				changed = true;
			}
		}

		if (changed)
		{
			GimmeErrorValue ret = gimmeDLLDoOperation(sky->filename, GIMME_CHECKOUT, GIMME_QUIET);
			if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_ALREADY_DELETED)
			{
				printf("Unable to checkout file %s.\n", sky->filename);
			}
			else
			{
				ParserWriteTextFileFromSingleDictionaryStruct(sky->filename, GFX_SKY_DICTIONARY, sky, 0, 0);
			}
		}

	}

	worldForEachMap(poolMapAtmosphere);

	count = objectLibraryFileCount();
	for (i = 0; i < count; ++i)
	{
		GroupFileEntry *gf = objectLibraryEntryFromIdx(i);
		LibFileLoad *file = loadLibFromDisk(gf->name);
		poolLibFileLoadAtmosphere(file, gf->name);
	}

	gfxAtmosphereWritePooled();
}
*/

AUTO_COMMAND;
void convertAllStarFields(void)
{
	StashTable htMaterials = stashTableCreateWithStringKeys(64, StashDeepCopyKeys_NeverRelease);
	FOR_EACH_IN_REFDICT(gfx_sky_dict, SkyInfo, sky_info)
	{
		bool changed=false;
		FOR_EACH_IN_EARRAY(sky_info->skyDomes, SkyDome, sky_dome)
		{
			if (sky_dome->star_field)
			{
				Vec3 rgb_scale;
				const char *texturename=NULL;
				const char *material_type = "Regular";
				bool bSkipIt=false;
				assert(!sky_dome->sunlit);

				if (stricmp(sky_dome->name, "white")==0)
					bSkipIt = true; // Don't care about this one
				if (stricmp(sky_dome->name, "Neb_Cloud_08_Round_Sub")==0)
					bSkipIt = true; // Doing something interesting
				if (stricmp(sky_dome->name, "Neb_Cloud_Edge_01_Sub")==0)
					bSkipIt = true; // using Diffuse_Tint_Unlit, which is as cheap, and does not do alpha
				if (stricmp(sky_dome->name, "Neb_Cloud_05_Long")==0)
					bSkipIt = true; // using Diffuse_Tint_Unlit, which is as cheap, and does not do alpha
				if (stricmp(sky_dome->name, "Sky_Add")==0)
					bSkipIt = true; // Already had the conversion run on it
				if (stricmp(sky_dome->name, "Sky_Regular")==0)
					bSkipIt = true; // Already had the conversion run on it
				if (stricmp(sky_dome->name, "Sky_Sub")==0)
					bSkipIt = true; // Already had the conversion run on it

				if (!bSkipIt)
				{
					Material *material = materialFind(sky_dome->name, WL_FOR_WORLD);
					MaterialData *data;
					const char *fn;
					ShaderOperationValues *values=NULL;
					materialGetData(material);
					data = material->material_data;
					fn = data->filename;

					changed = true;

					// Find diffuse scale
					if (!values)
						values = materialFindOperationValues(data, &data->graphic_props.default_fallback, "MultiplyDiffuse");
					if (!values)
						values = materialFindOperationValues(data, &data->graphic_props.default_fallback, "DiffuseMultiply");
					if (!values)
						values = materialFindOperationValues(data, &data->graphic_props.default_fallback, "Multiply1");

					if (!values && stricmp(data->graphic_props.shader_template->template_name, "Diffuse_NoAlphaCutout")==0)
					{
						setVec3(rgb_scale, 1, 1, 1);
					} else {
						assert(values); // One of those should match
						assert(eaSize(&values->values)==1);
						assert(eafSize(&values->values[0]->fvalues)>=3);

						copyVec3(values->values[0]->fvalues, rgb_scale);
					}

					// Find texture name
					FOR_EACH_IN_EARRAY(data->graphic_props.default_fallback.shader_values, ShaderOperationValues, values2)
					{
						if (strstri(values2->op_name, "Halftone"))
							continue;
						if (eaSize(&values2->values) == 1 && values2->values[0]->svalues)
						{
							assert(eaSize(&values2->values[0]->svalues) == 1); // Just one string
							assert(!texturename); // Just one texture per materials
							texturename = values2->values[0]->svalues[0];
							assert(texFind(texturename, 1));
						}
					}
					FOR_EACH_END;
					assert(texturename);

					if (data->graphic_props.flags & RMATERIAL_ADDITIVE)
						material_type = "Add";
					else if (data->graphic_props.flags & RMATERIAL_SUBTRACTIVE)
						material_type = "Sub";

					if (!stashFindInt(htMaterials, sky_dome->name, NULL))
					{
						stashAddInt(htMaterials, sky_dome->name, 1, false);

						printf("Material: %s / %s (%1.1f, %1.1f, %1.1f, %s, %s), first referenced in %s\n", data->graphic_props.shader_template->template_name, sky_dome->name,
							rgb_scale[0], rgb_scale[1], rgb_scale[2], texturename, material_type,
							sky_info->filename);
					}

					// Update color, material, and texture swaps
					FOR_EACH_IN_EARRAY(sky_dome->dome_values, SkyDomeTime, sdt)
					{
						Vec3 temp;
						hsvToRgb(sdt->tintHSV, temp);
						mulVecVec3(temp, rgb_scale, temp);
						rgbToHsv(temp, sdt->tintHSV);
						assert(!eaSize(&sdt->mat_props));
					}
					FOR_EACH_END;
					assert(!eaSize(&sky_dome->tex_swaps));
					{
						MaterialNamedTexture *tex = calloc(sizeof(*tex), 1);
						tex->op = allocAddString("Texture1");
						tex->input = allocAddString("Texture");
						tex->texture_name = allocAddString(texturename);
						tex->texture = texFind(texturename, 1);
						eaPush(&sky_dome->tex_swaps, tex);
					}
					{
						char materialname[1024];
						sprintf(materialname, "Sky_%s", material_type);
						StructFreeStringSafe(&sky_dome->name);
						sky_dome->name = StructAllocString(materialname);
					}
				}
			}

			if (eaSize(&sky_dome->dome_values) == 1 && sky_dome->dome_values[0]->alpha == 0)
			{
				// All alpha == 0, all the time, just remove it!
				changed = true;
				printf("Removing alpha = 0 dome %s from %s\n", sky_dome->name, sky_info->filename_no_path);
				StructDestroy(parse_SkyDomeTime, eaRemove(&sky_dome->dome_values, 0));
				StructDestroy(parse_SkyDome, eaRemove(&sky_info->skyDomes, isky_domeIndex));
			} else
			{
				// Look for identical sort orders
				bool bDidSomething;
				do
				{
					bDidSomething = false;
					FOR_EACH_IN_EARRAY(sky_info->skyDomes, SkyDome, sky_dome2)
					{
						if (sky_dome2 == sky_dome)
							continue;
						if (nearSameF32(sky_dome->sort_order, sky_dome2->sort_order))
						{
							changed = true;
							bDidSomething = true;
							sky_dome->sort_order += 0.1;
						}
					}
					FOR_EACH_END;
				} while (bDidSomething);
			}
		}
		FOR_EACH_END;
		if (changed)
		{
			// write it out!
			assert(ParserWriteTextFileFromDictionary(sky_info->filename, "SkyInfo", 0, 0));
		}
	}
	FOR_EACH_END;
	stashTableDestroy(htMaterials);
}
