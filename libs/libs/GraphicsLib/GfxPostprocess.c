#include "GfxPostProcess.h"

#include "rgb_hsv.h"
#include "crypt.h"
#include "DynamicCache.h"
#include "endian.h"
#include "structInternals.h"
#include "GenericPreProcess.h"
#include "rand.h"

#include "RdrState.h"

#include "../XRenderLib/pub/XWrapperInterface.h"
#include "../XRenderLib/pub/XRenderLib.h"
#include "../RdrShaderPrivate.h"


#include "GfxMaterialProfile.h"
#include "GfxDebug.h"
#include "GfxSky.h"
#include "GfxWorld.h"
#include "GfxMaterials.h"
#include "GfxShader.h"
#include "Materials.h"
#include "GfxMaterialsOpt.h"
#include "GfxTextureTools.h"
#include "GfxSurface.h"
#include "GfxDeferredShadows.h"
#include "GfxTexturesInline.h"
#include "GfxDrawFrame.h"
#include "GfxPrimitive.h"
#include "GfxLightOptions.h"
#include "GfxCommandParse.h"
#include "RdrTexture.h"

typedef enum ShrinkMode
{
	SM_NORMAL,
	SM_HIGHPASS,
	SM_BLOOM_CURVE,
	SM_LOG,
	SM_EXP,
	SM_MAX,
	SM_DEPTH,
	SM_MAX_LUMINANCE,
} ShrinkMode;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

void gfxPostprocessScreen(RdrScreenPostProcess *ppscreen)
{
	gfx_state.debug.frame_counts.postprocess_calls++;
	rdrPostProcessScreen(gfx_state.currentDevice->rdr_device, ppscreen);
}

void gfxPostprocessScreenPart(RdrScreenPostProcess *ppscreen, Vec2 dest_top_left, Vec2 dest_bottom_right)
{
	gfx_state.debug.frame_counts.postprocess_calls++;
	rdrPostProcessScreenPart(gfx_state.currentDevice->rdr_device, ppscreen, dest_top_left, dest_bottom_right);
}

#define DISABLE_GLARE 1

typedef struct GfxSpecialShaderDef
{
	GfxSpecialShader shader;
	char *filename;		//< .phl file is stored in
						//< data/shaders/D3D/effects
	const char *defines[5];
	bool assemble_lights;
	bool shadow_only;
} GfxSpecialShaderDef;

static GfxSpecialShaderDef gfx_special_shaders[] = {
	{GSS_DEFERRED_UNLIT,							"deferred_unlit", 	{ "UNLIT", 0 }, false, false},
	{GSS_DEFERRED_UNLIT_AND_LIGHT,					"deferred_unlit", 	{ 0 }, true, false},
	{GSS_DEFERRED_LIGHT,							"deferred_lit", 	{ 0 }, true, false},
	{GSS_DEFERRED_SSAO_PREPASS,						"ssao_prepass",		{ "SSAO", "SSAO_PREPASS", 0 }, false, false},
	{GSS_DEFERRED_SHADOW,							"deferred_shadow",	{ 0 }, true, true},
	{GSS_DEFERRED_SHADOW_STEREOSCOPIC,				"deferred_shadow",	{ "STEREOSCOPIC", 0 }, true, true},
	{GSS_DEFERRED_SHADOW_POISSON,					"deferred_shadow",	{ "POISSON_SHADOWS", 0 }, true, true},
	{GSS_DEFERRED_SHADOW_POISSON_STEREOSCOPIC,		"deferred_shadow",	{ "POISSON_SHADOWS", "STEREOSCOPIC", 0 }, true, true},
	{GSS_DEFERRED_SHADOW_SSAO,						"deferred_shadow",	{ "SSAO", 0 }, true, true},
	{GSS_DEFERRED_SHADOW_SSAO_STEREOSCOPIC,			"deferred_shadow",	{ "SSAO", "STEREOSCOPIC", 0 }, true, true},
	{GSS_DEFERRED_SHADOW_SSAO_POISSON,				"deferred_shadow",	{ "POISSON_SHADOWS", "SSAO", 0 }, true, true},
	{GSS_DEFERRED_SHADOW_SSAO_POISSON_STEREOSCOPIC,	"deferred_shadow",	{ "POISSON_SHADOWS", "SSAO", "STEREOSCOPIC", 0 }, true, true},
	{GSS_CALCULATE_SCATTERING,						"deferred_shadow",	{"POSTPROCESSING", "SCATTERING", 0}, true, true},
	{GSS_CALCULATE_SCATTERING_STEREOSCOPIC,			"deferred_shadow",	{"POSTPROCESSING", "SCATTERING", "STEREOSCOPIC", 0}, true, true},
	{GSS_OUTLINING_NO_NORMALS,						"postprocess1",		{0}, false, false},
	{GSS_OUTLINING_NO_NORMALS_DEPTH,				"postprocess1",		{ "DRAW_OUTLINES_AT_Z_DEPTH", 0}, false, false},
	{GSS_OUTLINING_NO_NORMALS_USE_Z,				"postprocess1",		{ "USE_Z" }, false, false},
	{GSS_OUTLINING_WITH_NORMALS,					"postprocess1",		{ "HAVE_NORMALS", 0}, false, false},
	{GSS_OUTLINING_WITH_NORMALS_DEPTH,				"postprocess1",		{ "HAVE_NORMALS", "DRAW_OUTLINES_AT_Z_DEPTH", 0}, false, false},
	{GSS_OUTLINING_APPLY_WITH_NORMALS,				"postprocess1",		{ "APPLY_WITH_NORMALS" }, false, false},
	{GSS_DEPTHOFFIELD,								"depth_of_field",	{0}, false, false},
    {GSS_DEPTHOFFIELD_SCALE,                        "depth_of_field",   {"DOF_SCALE", 0}, false, false},
	{GSS_DEPTHOFFIELD_EDGES,                        "depth_of_field",   {"DOF_EDGES", 0}, false, false},
	{GSS_DEPTHOFFIELD_DEPTHADJUST,                  "depth_of_field",   {"DEPTH_HSV", 0}, false, false},
	{GSS_DEPTHOFFIELD_EDGES_DEPTHADJUST,            "depth_of_field",   {"DOF_EDGES", "DEPTH_HSV", 0}, false, false},
	{GSS_SHRINK4,									"shrink4",			{0}, false, false},
	{GSS_SHRINK4_MAX,								"shrink4",			{"SHRINK_MAX", 0}, false, false},
	{GSS_SHRINK4_MAX_LUMINANCE,						"shrink4",			{"SHRINK_MAX", "MAX_LUMINANCE", 0}, false, false},
	{GSS_SHRINK4_LOG,								"shrink4",			{"SHRINK_LOG", 0}, false, false},
	{GSS_SHRINK4_EXP,								"shrink4",			{"SHRINK_EXP", 0}, false, false},
	{GSS_SHRINK4_HIGHPASS,							"shrink4",			{"SHRINK_HIGHPASS", 0}, false, false},
	{GSS_SHRINK4_BLOOM_CURVE,						"shrink4",			{"SHRINK_BLOOM_CURVE", 0}, false, false},
	{GSS_SHRINK4_DEPTH,								"shrink4",			{"SHRINK_MAX_DEPTH", 0}, false, false},
	{GSS_SHRINK4_2,									"shrink4",			{"SHRINK_2", 0}, false, false},
	{GSS_SHRINK4_2_MAX,								"shrink4",			{"SHRINK_2", "SHRINK_MAX", 0}, false, false},
	{GSS_SHRINK4_2_LOG,								"shrink4",			{"SHRINK_2", "SHRINK_LOG", 0}, false, false},
	{GSS_SHRINK4_2_EXP,								"shrink4",			{"SHRINK_2", "SHRINK_EXP", 0}, false, false},
	{GSS_SHRINK4_2_HIGHPASS,						"shrink4",			{"SHRINK_2", "SHRINK_HIGHPASS", 0}, false, false},
	{GSS_SHRINK4_2_BLOOM_CURVE,						"shrink4",			{"SHRINK_2", "SHRINK_BLOOM_CURVE", 0}, false, false},
	{GSS_SHRINK4_2_DEPTH,							"shrink4",			{"SHRINK_2", "SHRINK_MAX_DEPTH", 0}, false, false},
	{GSS_BLUR_3_H,									"blur",				{"KERNEL_3", 0}, false, false},
	{GSS_BLUR_3_V,									"blur",				{"KERNEL_3", "VERTICAL", 0}, false, false},
	{GSS_BLUR_5_H,									"blur",				{"KERNEL_5", 0}, false, false},
	{GSS_BLUR_5_V,									"blur",				{"KERNEL_5", "VERTICAL", 0}, false, false},
	{GSS_BLUR_7_H,									"blur",				{"KERNEL_7", 0}, false, false},
	{GSS_BLUR_7_V,									"blur",				{"KERNEL_7", "VERTICAL", 0}, false, false},
	{GSS_BLUR_9_H,									"blur",				{"KERNEL_9", 0}, false, false},
	{GSS_BLUR_9_V,									"blur",				{"KERNEL_9", "VERTICAL", 0}, false, false},
	{GSS_BLUR_11_H,									"blur",				{"KERNEL_11", 0}, false, false},
	{GSS_BLUR_11_V,									"blur",				{"KERNEL_11", "VERTICAL", 0}, false, false},
	{GSS_SMART_BLUR_3_H,							"smart_blur",		{"KERNEL_3", 0}, false, false},
	{GSS_SMART_BLUR_3_V,							"smart_blur",		{"KERNEL_3", "VERTICAL", 0}, false, false},
	{GSS_SMART_BLUR_5_H,							"smart_blur",		{"KERNEL_5", 0}, false, false},
	{GSS_SMART_BLUR_5_V,							"smart_blur",		{"KERNEL_5", "VERTICAL", 0}, false, false},
	{GSS_SMART_BLUR_7_H,							"smart_blur",		{"KERNEL_7", 0}, false, false},
	{GSS_SMART_BLUR_7_V,							"smart_blur",		{"KERNEL_7", "VERTICAL", 0}, false, false},
	{GSS_SMART_BLUR_9_H,							"smart_blur",		{"KERNEL_9", 0}, false, false},
	{GSS_SMART_BLUR_9_V,							"smart_blur",		{"KERNEL_9", "VERTICAL", 0}, false, false},
	{GSS_SMART_BLUR_11_H,							"smart_blur",		{"KERNEL_11", 0}, false, false},
	{GSS_SMART_BLUR_11_V,							"smart_blur",		{"KERNEL_11", "VERTICAL", 0}, false, false},
	{GSS_DOF_BLUR_3_H,								"smart_blur",		{"DOF_BLUR", "KERNEL_3", 0}, false, false},
	{GSS_DOF_BLUR_3_V,								"smart_blur",		{"DOF_BLUR", "KERNEL_3", "VERTICAL", 0}, false, false},
	{GSS_DOF_BLUR_5_H,								"smart_blur",		{"DOF_BLUR", "KERNEL_5", 0}, false, false},
	{GSS_DOF_BLUR_5_V,								"smart_blur",		{"DOF_BLUR", "KERNEL_5", "VERTICAL", 0}, false, false},
	{GSS_DOF_BLUR_7_H,								"smart_blur",		{"DOF_BLUR", "KERNEL_7", 0}, false, false},
	{GSS_DOF_BLUR_7_V,								"smart_blur",		{"DOF_BLUR", "KERNEL_7", "VERTICAL", 0}, false, false},
	{GSS_DOF_BLUR_9_H,								"smart_blur",		{"DOF_BLUR", "KERNEL_9", 0}, false, false},
	{GSS_DOF_BLUR_9_V,								"smart_blur",		{"DOF_BLUR", "KERNEL_9", "VERTICAL", 0}, false, false},
	{GSS_DOF_BLUR_11_H,								"smart_blur",		{"DOF_BLUR", "KERNEL_11", 0}, false, false},
	{GSS_DOF_BLUR_11_V,								"smart_blur",		{"DOF_BLUR", "KERNEL_11", "VERTICAL", 0}, false, false},
	{GSS_GLARE,										"glare",			{0}, false, false},
	{GSS_CALC_TONE_CURVE,							"tone_curve", 		{"BOOST_ABOVE_80PERCENT", 0}, false, false},
	{GSS_CALC_TONE_CURVE_NO_BOOST,					"tone_curve", 		{0}, false, false},
	{GSS_CALC_COLOR_CURVE,							"tone_curve", 		{"COLOR_CURVE", 0}, false, false},
	{GSS_CALC_INTENSITY_TINT,						"tone_curve", 		{"INTENSITY_TINT", 0}, false, false},
	{GSS_CALC_BLUESHIFT,							"blueshift", 		{0}, false, false},
	{GSS_FINAL_POSTPROCESS,							"postprocess2", 	{0}, false, false},
	{GSS_FINAL_POSTPROCESS_COLOR_CORRECT,			"postprocess2", 	{"COLOR_CORRECT", 0}, false, false},
	{GSS_FINAL_POSTPROCESS_TINT,					"postprocess2", 	{"TINT", 0}, false, false},
	{GSS_FINAL_POSTPROCESS_COLOR_CORRECT_TINT,		"postprocess2", 	{"TINT", "COLOR_CORRECT", 0}, false, false},
	{GSS_FINAL_POSTPROCESS_LUT_STAGE,				"postprocess3",		{"BLOOM_TONE_CURVE",0}, false, false},
	{GSS_ALPHABLIT,									"NoAlphaBlit",		{"USE_ALPHA", 0}, false, false},
	{GSS_ALPHABLIT_DISCARD_EDGE,					"NoAlphaBlit",		{"USE_ALPHA", "DISCARD_EDGE", 0}, false, false},
	{GSS_PCFDEPTHBLIT,								"NoAlphaBlit",		{"PCF_DEPTH_BLIT", 0}, false, false},
	{GSS_NOALPHABLIT,								"NoAlphaBlit",		{0}, false, false},
	{GSS_SHOWALPHA,									"NoAlphaBlit",		{"ALPHABLIT_SHOW_ALPHA", 0}, false, false},
	{GSS_SHOWALPHA_AS_COLOR,						"NoAlphaBlit",		{"ALPHA_AS_COLOR", 0}, false, false},
	{GSS_SHOWTEXTUREMIP,							"NoAlphaBlit",		{"SHOWTEXMIP", 0}, false, false},
	{GSS_TONEMAP_LDR_TO_HDR,						"NoAlphaBlit",		{"TONEMAP_LDR_TO_HDR", "DEPTH", 0}, false, false},
	{GSS_TONEMAP_LDR_TO_HDR_COLORONLY,				"NoAlphaBlit",		{"TONEMAP_LDR_TO_HDR", 0}, false, false},
	{GSS_BLOOMCURVE,								"NoAlphaBlit",		{"BLOOM_CURVE", 0}, false, false},
	{GSS_ADDTEX,									"NoAlphaBlit",		{"ADD_TEXTURES", 0}, false, false},
	{GSS_BRIGHTPASS,								"NoAlphaBlit",		{"BRIGHT_PASS", 0}, false, false},
	{GSS_MEASURE_LUMINANCE,							"NoAlphaBlit",		{"TEXKILL_CONSTANT", 0}, false, false},
	{GSS_MEASURE_LUMINANCE_HDR2,					"NoAlphaBlit",		{"TEXKILL_TEXCOORD", 0}, false, false},
	{GSS_MEASURE_HDR_POINT,							"NoAlphaBlit",		{"TEXKILL_CONSTANT", 0}, false, false},
	{GSS_FILL,										"fill",				{0}, false, false},
	{GSS_FILL_HDR,									"fill",				{"TONEMAP", "NO_DEPTH_WRITE", 0}, false, false},
	{GSS_FILL_HDR_DEPTH,							"fill",				{"TONEMAP", 0}, false, false},
	{GSS_FILL_HDR_DEPTHSTENCIL,						"fill",				{"TONEMAP", "RESTORE_DEPTH", 0}, false, false},
	{GSS_FILL_CUBEMAP,								"cubemap_fill",		{0}, false, false},
	{GSS_DEFERRED_SCATTERING,						"deferred_scatter",	{0}, false, false},
	{GSS_TESTZOCCLUSION,							"zocclusion_test",	{0}, false, false},
	{GSS_RECOVER_ALPHA_FOR_HEADSHOT,				"recover_alpha_for_headshot", {0}, false, false},
	{GSS_LOW_RES_ALPHA_EDGE_DETECT,					"low_res_alpha_edge_detect", {"NORMAL_ALPHA_BLEND", 0}, false, false},
	{GSS_LOW_RES_ALPHA_EDGE_DETECT_ADDITIVE_BLEND,	"low_res_alpha_edge_detect", {0}, false, false},
	{GSS_DEPTH_AS_COLOR,							"NoAlphaBlit",		{"DEPTH_AS_COLOR", 0 }, false, false },
	{GSS_DEPTH_AS_COLOR_MSAA_2,						"resolve_msaa_2_depth",		{"DEPTH_AS_COLOR", 0 }, false, false },
	{GSS_DEPTH_AS_COLOR_MSAA_4,						"resolve_msaa_4_depth",		{"DEPTH_AS_COLOR", 0 }, false, false },
	{GSS_DEPTH_AS_COLOR_MSAA_8,						"resolve_msaa_8_depth",		{"DEPTH_AS_COLOR", 0 }, false, false },
	{GSS_DEPTH_AS_COLOR_MSAA_16,					"resolve_msaa_16_depth",	{"DEPTH_AS_COLOR", 0 }, false, false },
	{GSS_TARGET_HIGHLIGHT_ALPHATOWHITE,				"target_halo",		{"ALPHATOWHITE", 0 }, false, false },
	{GSS_TARGET_HIGHLIGHT_SMOOTHING,				"target_halo",		{"SMOOTHING", 0 }, false, false },
	{GSS_TARGET_HIGHLIGHT_OUTLINE,					"target_halo",		{"OUTLINE", 0 }, false, false },
	{GSS_UI_COLORBLIND_PRO,							"ui_postprocess",	{"COLORBLIND", "PRO", 0 }, false, false },
	{GSS_UI_COLORBLIND_DEU,							"ui_postprocess",	{"COLORBLIND", "DEU", 0 }, false, false },
	{GSS_UI_COLORBLIND_TRI,							"ui_postprocess",	{"COLORBLIND", "TRI", 0 }, false, false },
	{GSS_UI_DESATURATE,								"ui_postprocess",	{"DESATURATE", 0 }, false, false },
	{GSS_UI_TV,										"ui_postprocess",	{"TV", 0 }, false, false },
};

char current_intrinsic_defines_crc[11];

typedef struct GfxSpecialShaderData
{
	bool loaded;
	ShaderHandle shader_handle;
	StashTable light_shader_handles;
	GfxSpecialShaderDef *def;
	S64 key;
} GfxSpecialShaderData;

#define PER_LIGHT_TAG "$PerLight$"
#define SPECIAL_SHADER_DEFINE_MAX 10

int gfxFindSpecialShaderIndexEx(GfxSpecialShader shader)
{
	int index;
	for (index=0; index<ARRAY_SIZE(gfx_special_shaders) && gfx_special_shaders[index].shader != shader; index++);
	assertmsg(index!=ARRAY_SIZE(gfx_special_shaders), "Enum not found in gfx_special_shaders list");
	return index;
}

ShaderHandle gfxDemandLoadSpecialShaderEx(GfxSpecialShader shader, int constants_used, int textures_used, RdrMaterialShader shader_num, const RdrLightDefinition **light_def)
{
	const char *device_profile_name = rdrGetDeviceProfileName(gfx_state.currentDevice->rdr_device);
	GfxSpecialShaderData *special_shader;
	bool bDoCaching = !rdr_state.disableShaderCache;

	// Check that the enums are in the right places
	if (constants_used == -1)
		assert(shader > GSS_BeginSimpleShaders);
	else
		assert(shader < GSS_BeginSimpleShaders);

	// TODO: cache bits that determine the defines in the special shaders, and reload if those bits change,
	//   possibly using the old shader until the new one has finished reloading.

	if (!(special_shader = eaGet(&gfx_state.currentDevice->special_shaders, shader)))
	{
		int index = gfxFindSpecialShaderIndexEx(shader);

		special_shader = calloc(1, sizeof(GfxSpecialShaderData));
		special_shader->shader_handle = rdrGenShaderHandle();

		if (eaSize(&gfx_state.currentDevice->special_shaders) <= shader)
			eaSetSize(&gfx_state.currentDevice->special_shaders, shader+1);
		eaSet(&gfx_state.currentDevice->special_shaders, special_shader, shader);

		special_shader->def = &gfx_special_shaders[index];
		if (special_shader->def->assemble_lights)
			special_shader->light_shader_handles = stashTableCreateFixedSize(1024, sizeof(S64));
	}

	if (!special_shader->def->assemble_lights && shader_num.key)
		shader_num.key = 0;

	if (shader_num.key)
	{
		GfxSpecialShaderData *sub_special_shader = NULL;
		stashFindPointer(special_shader->light_shader_handles, &shader_num.key, &sub_special_shader);
		if (!sub_special_shader)
		{
			sub_special_shader = calloc(1, sizeof(GfxSpecialShaderData));
			sub_special_shader->shader_handle = rdrGenShaderHandle();
			sub_special_shader->def = special_shader->def;
			sub_special_shader->key = shader_num.key;
			stashAddPointer(special_shader->light_shader_handles, &sub_special_shader->key, sub_special_shader, true);
		}
		special_shader = sub_special_shader;
	}

	if (!special_shader->loaded)
	{
		const char *defines[SPECIAL_SHADER_DEFINE_MAX];
		int num_defines;
		char filename[MAX_PATH];
		char error_filename[MAX_PATH];
		char *shader_data, *shader_text;
		const char *intrinsic_defines;
		RdrShaderParams *shader_params;
		char cache_filename[MAX_PATH];
		char cache_filename_xbox[MAX_PATH];
		bool loadedFromCache=false;
		const MaterialAssemblerProfile *mat_profile;

		mat_profile = getMaterialProfileForDevice(device_profile_name);

		special_shader->loaded = true;

		sprintf(cache_filename, "%s_%s", special_shader->def->filename, device_profile_name);
		if (shader_num.key) {
			strcatf(cache_filename, "_%X_%X_c%d_t%d", shader_num.shaderMask, shader_num.lightMask, constants_used, textures_used);
		}

		for (num_defines = 0; special_shader->def->defines[num_defines] && num_defines < ARRAY_SIZE(special_shader->def->defines); num_defines++) {
			defines[num_defines] = special_shader->def->defines[num_defines];
			strcatf(cache_filename, "_%s", special_shader->def->defines[num_defines]);
		}
		defines[num_defines] = NULL;

		strcpy(cache_filename_xbox, cache_filename);

		if (current_intrinsic_defines_crc[0])
		{
			strcat(cache_filename, current_intrinsic_defines_crc);
		} else {
			cryptAdler32Init();
			intrinsic_defines = rdrGetIntrinsicDefines(gfx_state.currentDevice->rdr_device);
			cryptAdler32Update(intrinsic_defines, (int)strlen(intrinsic_defines));
			strcatf(cache_filename, "__%08x", cryptAdler32Final());
		}

		if (bDoCaching && (rdr_state.compile_all_shader_types & CompileShaderType_Xbox))
		{
			assert(stricmp(rdrGetDeviceIdentifier(gfx_state.currentDevice->rdr_device), "D3D")==0);  // Must be PC DX renderer

			// Xbox shaders
			cryptAdler32Init();
			intrinsic_defines = gfx_state.currentDevice->rdr_device->getIntrinsicDefines(DEVICE_XBOX);
			cryptAdler32Update(intrinsic_defines, (int)strlen(intrinsic_defines));
			strcatf(cache_filename_xbox, "__%08x", cryptAdler32Final());
        }

		if (!rdrShaderPreloadSkip(cache_filename))
			rdrShaderPreloadLog("Postprocessing shader not loaded: %s", cache_filename);

		// Check cache
		if (bDoCaching)
		{
			int data_size;
			void *data;
			data = dynamicCacheGetSync(gfx_state.shaderCache, cache_filename, &data_size);
			if (data)
			{
				// Got pre-compiled data
				PrecompiledShaderHeader *header = data;
				header->updb_filename_size = endianSwapIfBig(U32, header->updb_filename_size);
				header->updb_size = endianSwapIfBig(U32, header->updb_size);
				header->data_size = endianSwapIfBig(U32, header->data_size);
				if (sizeof(PrecompiledShaderHeader) +
					header->data_size + 
					header->updb_filename_size +
					header->updb_size != data_size ||
					!header->updb_filename_size != !header->updb_size)
				{
					// Bad data, will be overwritten in the cache
				} else {
					// Send to renderer
					gfxLoadPrecompiledShaderInternal(header, special_shader->shader_handle, allocAddFilename(cache_filename));
					loadedFromCache = true;
				}

				SAFE_FREE(data);
			}
		}

		if (!loadedFromCache || rdr_state.compile_all_shader_types)
		{
			FileList file_list = NULL;
			// update shader
            sprintf(filename, FORMAT_OK(mat_profile->gen_special_shader_path), special_shader->def->filename);
            sprintf(error_filename, FORMAT_OK(mat_profile->gen_special_shader_path), "error");
			
			shader_text = shader_data = fileAlloc(filename, NULL);
			if (!shader_data) {
				gfxStatusPrintf("Failed to load %s", filename);
				shader_data = fileAlloc(error_filename, NULL);
				assertmsg(shader_data, "Could not load error shader");
			}
			if (bDoCaching) {
				FileListInsertChecksum(&file_list, filename, 0);
			}

			if (shader_text && shader_num.key)
			{
				char *shader_data_post_light = strstr(shader_data, PER_LIGHT_TAG);
				if (shader_data_post_light)
				{
					*shader_data_post_light = 0;
					shader_data_post_light += strlen(PER_LIGHT_TAG);
				}

				assertmsg(constants_used >= 0, "This special shader has lights assembled into it, you must call gfxDemandLoadSpecialShaderEx.");
				assertmsg(textures_used >= 0, "This special shader has lights assembled into it, you must call gfxDemandLoadSpecialShaderEx.");

				shader_text = gfxAssembleLights(shader_num, mat_profile, 
												constants_used, 0, textures_used,
												shader_data, (int)strlen(shader_data), 
												NULL, 0,
												shader_data_post_light, shader_data_post_light?((int)strlen(shader_data_post_light)):0,
												NULL, 0,
												special_shader->def->shadow_only, bDoCaching?&file_list:NULL);

				strcatf(filename, "_%d_%d", shader_num.shaderMask, shader_num.lightMask); // unique debug filename
			}
			else if (shader_text) //there are no lights so just remove the PER_LIGHT_TAG
			{
				char *shader_data_post_light = strstr(shader_data, PER_LIGHT_TAG);
				if (shader_data_post_light)
				{
					*shader_data_post_light = 0;
					shader_data_post_light += strlen(PER_LIGHT_TAG);
				}
				shader_text = estrCreateFromStr(shader_data);
				if (shader_data_post_light) estrAppend2(&shader_text, shader_data_post_light);
			}

#if !PLATFORM_CONSOLE
			// Update Xbox shaders if appropriate
			if (bDoCaching && (rdr_state.compile_all_shader_types & CompileShaderType_Xbox)) {

				// Xbox Shaders
				if (dynamicCacheIsFileUpToDateSync_WillStall(gfx_state.shaderCacheXboxOnPC, cache_filename_xbox))
				{
					// Already up to date, just touch the timestamp
					dynamicCacheTouchFile(gfx_state.shaderCacheXboxOnPC, cache_filename_xbox);
				} else {
					// Compile and save result
					char *programText = strdup(shader_text?shader_text:shader_data);
					U32 new_crc = 0;
					XWrapperCompileShaderData data = {0};
					char error_buffer[1024];
					char updb_buffer[MAX_PATH];
					char debug_fn[MAX_PATH];
					char debug_header[1000];
					int i;
					char tmpFilePath[MAX_PATH];

					error_buffer[0] = 0;

					rdrShaderEnterCriticalSection();
					rdrShaderResetCache(false);

					// Preprocess
                    genericPreProcReset();
					for (i=0; i<num_defines; i++)
						genericPreProcAddDefine(defines[i]);

					rxbxAddIntrinsicDefinesForXbox();
					rdrPreProcessShader(&programText, "shaders/D3D", filename, ".phl", "//", 0, &new_crc, &file_list, debug_fn, debug_header);
					rdrShaderResetCache(false);
					rdrShaderLeaveCriticalSection();

					data.programText = programText;
					data.programTextLen = (int)strlen(programText);
					data.entryPointName = "main_output";
					data.shaderModel = "ps_3_0";
					data.updbPath = updb_buffer;
					data.updbPath_size = ARRAY_SIZE(updb_buffer);
					data.errorBuffer = error_buffer;
					data.errorBuffer_size = ARRAY_SIZE(error_buffer);

                    fileSpecialDir("shaders_errors/xbox/", SAFESTR(tmpFilePath));
                    strcat(tmpFilePath, cache_filename_xbox);
                    data.errDumpLocation = tmpFilePath;
                    mkdirtree(tmpFilePath);

                    if (XWrapperCompileShader(&data)) {
						// Success!
						gfxShaderUpdateCache(gfx_state.shaderCacheXboxOnPC, cache_filename_xbox, data.updbPath, data.updbData, data.updbDataLen, data.compiledResult, data.compiledResultLen, &file_list);
					} else {
						Errorf("Error compiling Xbox shader %s:\n%s", cache_filename_xbox, data.errorBuffer);
					}
					SAFE_FREE(data.compiledResult);
					SAFE_FREE(data.updbData);
					SAFE_FREE(programText);
				}
            }

#endif

			// Update PC/actual shaders
			if (!loadedFromCache)
			{
				shader_params = rdrStartUpdateShader(gfx_state.currentDevice->rdr_device, special_shader->shader_handle, 
					SPT_FRAGMENT, defines, num_defines, shader_text?shader_text:shader_data);
				shader_params->shader_debug_name = allocAddFilename(filename);
				shader_params->shader_error_filename = allocAddFilename(error_filename);
				shader_params->noBackgroundCompile = 1;
				if (bDoCaching)
				{
					shader_params->finishedCallbackData = calloc(sizeof(*shader_params->finishedCallbackData) + sizeof(*shader_params->finishedCallbackData->userData), 1);
					shader_params->finishedCallbackData->finishedCallback = shaderCacheFinishedCompiling;
					shader_params->finishedCallbackData->userData = (GfxShaderFinishedData*)(shader_params->finishedCallbackData+1);
					shader_params->finishedCallbackData->userData->filename = allocAddFilename(cache_filename);
					shader_params->finishedCallbackData->userData->handle_data = NULL;
					shader_params->finishedCallbackData->file_list = file_list;
					file_list = NULL;
				}
				rdrEndUpdateShader(gfx_state.currentDevice->rdr_device);
			} else {
				FileListDestroy(&file_list);
			}
			assert(!file_list);

			if (shader_text != shader_data)
				estrDestroy(&shader_text);
			SAFE_FREE(shader_data);
		}
	}

	if (light_def && rdrGetLightType(shader_num.lightMask, 0))
		*light_def = gfxGetLightDefinition(rdrGetDeviceIdentifier(gfx_state.currentDevice->rdr_device), rdrGetLightType(shader_num.lightMask, 0));

	return special_shader->shader_handle;
}

void gfxReloadSpecialShaders(void)
{
	int i, j;

	for (i=eaSize(&gfx_state.devices)-1; i>=0; i--) {
		if (gfx_state.devices[i]) {
			for (j=eaiSize((S32**)&gfx_state.devices[i]->special_shaders)-1; j>=0; j--) {
				GfxSpecialShaderData *special_shader = gfx_state.devices[i]->special_shaders[j];
				if (special_shader) {
					special_shader->loaded = false;
					if (special_shader->light_shader_handles) {
						StashTableIterator iter;
						StashElement elem;
						stashGetIterator(special_shader->light_shader_handles, &iter);
						while (stashGetNextElement(&iter, &elem)) {
							GfxSpecialShaderData *sub_special_shader = stashElementGetPointer(elem);
							if (sub_special_shader)
								sub_special_shader->loaded = false;
						}
					}
				}
			}
		}
	}
}



void gfxEnableBloomToneCurve(bool enable)
{
	int shader_table_index = gfxFindSpecialShaderIndexEx(GSS_FINAL_POSTPROCESS_LUT_STAGE);
	gfx_special_shaders[shader_table_index].defines[0] = enable ? "BLOOM_TONE_CURVE" : NULL;
}


// DJR determine an acceptable limit here
int outline_smooth_passes = 1;
AUTO_CMD_INT(outline_smooth_passes,outline_smooth_passes);

int outline_stencil_mask_pca = 1;
AUTO_CMD_INT(outline_stencil_mask_pca,outline_stencil_mask_pca);

#define STENCIL_VALUE_DRAW_OUTLINE_PIXEL 1

__forceinline static float gfxSkyGetBloomScale(const BlendedSkyInfo *sky_info, int bReciprocal)
{
	float bloomPower = sky_info->bloomValues.lowQualityBloomPower;
	float base;
	if (bReciprocal)
		bloomPower = -bloomPower;
	base = (1.0f - sky_info->bloomValues.lowQualityBloomStart)*sky_info->bloomValues.lowQualityBloomMultiplier;
	
	if (base <= 0.0f)
		return 0.0f;

	return pow(base, bloomPower);
}

static int gfxSkyIsScatteringEnabled(const BlendedSkyInfo *sky_info)
{
	const GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	return sky_info->scatteringValues.scatterLightColorHSV[2];
}

void gfxDoCalculateScattering(const RdrLightDefinition **light_defs, const BlendedSkyInfo *sky_info)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);

	if (!gdraw || !gfxSkyIsScatteringEnabled(sky_info))
		return;

	// reuse shadow buffer code to accumulate scattering strength buffer
	gfxRenderShadowBuffer(gdraw->this_frame_shadowmap_lights, eaSize(&gdraw->this_frame_shadowmap_lights), light_defs, sky_info, 1);
}

void gfxDoScattering(const BlendedSkyInfo *sky_info)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[1];
	Vec4 vConstants[1];
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);

	if (!gdraw || !gfxSkyIsScatteringEnabled(sky_info) || !gdraw->scatter_max)
		return;

	gfxSetStageActiveSurface(GFX_APPLY_SCATTERING);

	ppscreen.tex_width = gfxGetStageInputSurface(GFX_APPLY_SCATTERING, 0)->width_nonthread;
	ppscreen.tex_height = gfxGetStageInputSurface(GFX_APPLY_SCATTERING, 0)->height_nonthread;

	textures[ 0 ] = gfxGetStageInputTexHandle(GFX_APPLY_SCATTERING, 0);
	rdrChangeTexHandleFlags(textures, RTF_CLAMP_U|RTF_CLAMP_V);
	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;

	gfxHsvToRgb(sky_info->scatteringValues.scatterLightColorHSV, vConstants[0]);
	vConstants[0][3] = gdraw->scatter_max;
	ppscreen.material.const_count = 1;
	ppscreen.material.constants = vConstants;


	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_DEFERRED_SCATTERING);
	ppscreen.blend_type = RPPBLEND_ADD;

	gfxPostprocessScreen(&ppscreen);
}

void gfxDoOutliningEarly(const BlendedSkyInfo *sky_info)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[2];
	Vec4 vConstants[4];
	const GfxCameraView * camera = gfx_state.currentCameraView;
	GfxSpecialShader shader_num;
 
	if (gfxIsStageEnabled(GFX_ZPREPASS_EARLY))
	{
		if (gfxGetStageInputSurface(GFX_OUTLINING_EARLY, 0))
		{
#if _XBOX
			// resolve SBUF_1 before we can use it
			rdrSurfaceSnapshotEx(gfxGetStageOutput(GFX_OUTLINING_EARLY), 0, SBUF_1, 1); // JE: This is actually resolving MASK_SBUF_0... is that what's needed?
#endif

			shader_num = GSS_OUTLINING_WITH_NORMALS;
		}
		else
		{
			if (sky_info->outlineValues.useZ)
			{
				shader_num = GSS_OUTLINING_NO_NORMALS_USE_Z;
			}
			else
			{
				shader_num = GSS_OUTLINING_NO_NORMALS;
			}
		}
		textures[0] = gfxGetStageInputTexHandle(GFX_OUTLINING_EARLY, 0); // HDR high exposure + packed normal
		textures[1] = gfxGetStageInputTexHandle(GFX_OUTLINING_EARLY, 1); // depth
	}
	else
	{
		shader_num = GSS_OUTLINING_WITH_NORMALS;
		textures[0] = gfxGetStageInputTexHandle(GFX_OUTLINING_EARLY, 0); // normal without z value
		textures[1] = gfxGetStageInputTexHandle(GFX_OUTLINING_EARLY, 1); // depth and gloss with normal z sign
	}

	gfxBeginSection("PP: gfxDoOutliningEarly");
	gfxSetStageActiveSurface(GFX_OUTLINING_EARLY);

	ppscreen.tex_width = gfxGetStageInputSurface(GFX_OUTLINING_EARLY, 1)->width_nonthread;
	ppscreen.tex_height = gfxGetStageInputSurface(GFX_OUTLINING_EARLY, 1)->height_nonthread;

	ppscreen.material.const_count = ARRAY_SIZE(vConstants);
	ppscreen.material.constants = vConstants;

	gfxHsvToRgb(sky_info->outlineValues.outlineHSV, vConstants[0]);

	if (sky_info->outlineValues.outlineLightRange1 < sky_info->outlineValues.outlineLightRange2)
	{
		GfxCameraView *camera_view = gfx_state.currentCameraView;
		F32 t = saturate((camera_view->adapted_light_range - sky_info->outlineValues.outlineLightRange1) / (sky_info->outlineValues.outlineLightRange2 - sky_info->outlineValues.outlineLightRange1));
		Vec3 outline_color_2;
		gfxHsvToRgb(sky_info->outlineValues.outlineHSV2, outline_color_2);
		lerpVec3(outline_color_2, t, vConstants[0], vConstants[0]);
	}
	else if (sky_info->outlineValues.outlineLightRange1 > sky_info->outlineValues.outlineLightRange2)
	{
		GfxCameraView *camera_view = gfx_state.currentCameraView;
		F32 t = saturate((camera_view->adapted_light_range - sky_info->outlineValues.outlineLightRange2) / (sky_info->outlineValues.outlineLightRange1 - sky_info->outlineValues.outlineLightRange2));
		Vec3 outline_color_2;
		gfxHsvToRgb(sky_info->outlineValues.outlineHSV2, outline_color_2);
		lerpVec3(vConstants[0], t, outline_color_2, vConstants[0]);
	}
	if (sky_info->outlineValues.outlineAlpha)
	{
		vConstants[0][3] = sky_info->outlineValues.outlineAlpha;
	}
	else
	{
		vConstants[0][3] = 1.0f;
	}
	setVec4( vConstants[1],
		sky_info->outlineValues.outlineThickness/ppscreen.tex_width, 0, 0,
		sky_info->outlineValues.outlineThickness/ppscreen.tex_height);
	
	setVec4( vConstants[2], 
		sky_info->outlineValues.outlineFade[ 1 ],
		sky_info->outlineValues.outlineFade[ 0 ],
		sky_info->outlineValues.outlineFade[ 3 ],
		sky_info->outlineValues.outlineFade[ 2 ]
		);
	vConstants[2][1] = 1.0f / (vConstants[2][1] - vConstants[2][0]);
	vConstants[2][3] = 1.0f / (vConstants[2][3] - vConstants[2][2]);

	setVec4(vConstants[3], MAX(vConstants[2][0], vConstants[2][2]), 0, 0, 0);

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(shader_num);

	ppscreen.blend_type = RPPBLEND_REPLACE; // If one-pass: RPPBLEND_ALPHA

	gfxPostprocessScreen(&ppscreen);

	gfxEndSection();
}


void gfxDoOutliningLate(const BlendedSkyInfo *sky_info)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[3];
	Vec4 vConstants[4] = { 0 };
	const GfxCameraView * camera = gfx_state.currentCameraView;

	if (!gfx_state.currentAction->is_offscreen && gfx_state.debug.postprocessing_debug)
	{
		static int show_alpha = 0;
		gfxDebugThumbnailsAddSurfaceCopy(gfxGetStageInputSurface(GFX_OUTLINING_LATE, 1),
			SBUF_0, 0, "Outlining Color/Normal", show_alpha);
	}

	textures[0] = gfxGetStageInputTexHandle(GFX_OUTLINING_LATE, 0);
	textures[1] = gfxGetStageInputTexHandle(GFX_OUTLINING_LATE, 1);
	rdrChangeTexHandleFlags(textures + 1, RTF_MIN_POINT|RTF_MAG_POINT|RTF_CLAMP_U|RTF_CLAMP_V);
	textures[2] = gfxGetStageInputTexHandle(GFX_OUTLINING_LATE, 2);
    rdrChangeTexHandleFlags(textures + 2, RTF_MIN_POINT|RTF_MAG_POINT|RTF_CLAMP_U|RTF_CLAMP_V);

	gfxBeginSection("PP: gfxDoOutliningLate");
	gfxSetStageActiveSurface(GFX_OUTLINING_LATE);

	ppscreen.material.const_count = ARRAY_SIZE(vConstants);
	ppscreen.material.constants = vConstants;

	setVec4( vConstants[2], 
		sky_info->outlineValues.outlineFade[ 1 ],
		sky_info->outlineValues.outlineFade[ 0 ],
		sky_info->outlineValues.outlineFade[ 3 ],
		sky_info->outlineValues.outlineFade[ 2 ]
	);
	vConstants[2][1] = 1.0f / (vConstants[2][1] - vConstants[2][0]);
	vConstants[2][3] = 1.0f / (vConstants[2][3] - vConstants[2][2]);

	ppscreen.tex_width = gfxGetStageInputSurface(GFX_OUTLINING_LATE, 0)->width_nonthread;
	ppscreen.tex_height = gfxGetStageInputSurface(GFX_OUTLINING_LATE, 0)->height_nonthread;

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(
		!textures[2] ? GSS_ALPHABLIT : GSS_OUTLINING_APPLY_WITH_NORMALS);

	ppscreen.blend_type = RPPBLEND_ALPHA;
	ppscreen.depth_test_mode = RPPDEPTHTEST_OFF;

	gfxPostprocessScreen(&ppscreen);

	gfxEndSection();
}

void gfxClearActiveSurfaceRestoreDepth(RdrSurface *dest_surface, const Vec4 clear_color)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[1];
	Vec4 vConstants[4];

	if (!gfx_state.currentAction->opaqueDepth.surface)
		return;

	gfxSetActiveSurfaceEx(dest_surface, clear_color?0:MASK_SBUF_DEPTH, 0);

	ppscreen.tex_width = gfx_state.currentAction->opaqueDepth.surface->width_nonthread;
	ppscreen.tex_height = gfx_state.currentAction->opaqueDepth.surface->height_nonthread;

	ppscreen.material.const_count = ARRAY_SIZE(vConstants);
	ppscreen.material.constants = vConstants;

	if (clear_color)
	{
		copyVec4(clear_color, vConstants[0]);
		copyVec4(clear_color, vConstants[1]);
		copyVec4(clear_color, vConstants[2]);
		copyVec4(clear_color, vConstants[3]);
	}
	textures[0] = rdrSurfaceToTexHandleEx(gfx_state.currentAction->opaqueDepth.surface, gfx_state.currentAction->opaqueDepth.buffer, gfx_state.currentAction->opaqueDepth.snapshot_idx, 0, false);

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_FILL_HDR_DEPTHSTENCIL);

	ppscreen.blend_type = RPPBLEND_REPLACE;
	ppscreen.write_depth = 1;
	gfxPostprocessScreen(&ppscreen);
}

void gfxScaleBuffer(RdrSurface *source_surface, bool viewport_independent_textures)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[1];

	gfxBeginSection("PP: gfxScaleBuffer");
	textures[0] = rdrSurfaceToTexHandleEx(source_surface, SBUF_0, 0, 0, false);

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_NOALPHABLIT);

	ppscreen.tex_width = source_surface->width_nonthread;
	ppscreen.tex_height = source_surface->height_nonthread;
	ppscreen.viewport_independent_textures = viewport_independent_textures;

	gfxPostprocessScreen(&ppscreen);
	gfxEndSection();
}

void gfxSetExposureTransform(void)
{
	GfxCameraView *camera_view = gfx_state.currentCameraView;
	RdrDevice *rdr_device = gfx_state.currentDevice->rdr_device;

	if (!rdr_device || !camera_view)
		return;
	if (!rdr_device->is_locked_nonthread)
		return;
	if (gfx_state.debug.no_exposure || gfx_state.debug.show_material_cost) {
		static Vec4 no_exposure_transform = {1, 1, 0, 0};
		rdrSetExposureTransform(rdr_device, no_exposure_transform);
	} else {
		rdrSetExposureTransform(rdr_device, camera_view->exposure_transform);
	}
}

void gfxCopyDepthIntoRGB(RdrSurface *source_surface, int snapshot_idx, float depth_min, float depth_max, float depth_power)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[1];
	Vec4 vConstants[1];
	GfxSpecialShader resolve_depth_shader = GSS_DEPTH_AS_COLOR;

	gfxBeginSection("PP: gfxCopyDepthIntoRGB");

	textures[0] = rdrSurfaceToTexHandleEx(source_surface, SBUF_DEPTH, snapshot_idx, 0, false);
	vConstants[0][0] = depth_min;
	vConstants[0][1] = depth_max;
	vConstants[0][2] = depth_power;

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;
	ppscreen.material.const_count = ARRAY_SIZE( vConstants );
	ppscreen.material.constants = vConstants;

	if (snapshot_idx == 0 && !strcmp(gfx_state.currentDevice->rdr_device->getProfileName(gfx_state.currentDevice->rdr_device), "D3D11"))
	{
		// doing the resolve with the shader, and we must have the unresolved MSAA surface as input
		textures[0] = rdrSurfaceToTexHandleEx(source_surface, SBUF_DEPTH, snapshot_idx, 0, true);
		if (source_surface->params_nonthread.desired_multisample_level == 2)
			resolve_depth_shader = GSS_DEPTH_AS_COLOR_MSAA_2;
		else
		if (source_surface->params_nonthread.desired_multisample_level == 4)
			resolve_depth_shader = GSS_DEPTH_AS_COLOR_MSAA_4;
		else
		if (source_surface->params_nonthread.desired_multisample_level == 8)
			resolve_depth_shader = GSS_DEPTH_AS_COLOR_MSAA_8;
		else
		if (source_surface->params_nonthread.desired_multisample_level == 16)
			resolve_depth_shader = GSS_DEPTH_AS_COLOR_MSAA_16;
	}
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(resolve_depth_shader);

	ppscreen.tex_width = source_surface->width_nonthread;
	ppscreen.tex_height = source_surface->height_nonthread;

	gfxPostprocessScreen(&ppscreen);
	gfxEndSection();
}

void gfxPostprocessOneTex(RdrSurface *source_surface, TexHandle source_tex, int shader_num, Vec4 *constants, int num_constants, RdrPPBlendType blend_type)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[1];

	textures[0] = source_tex;

	ppscreen.material.constants = constants;
	ppscreen.material.const_count = num_constants;

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(shader_num);

	ppscreen.blend_type = blend_type;

	ppscreen.tex_width = source_surface->width_nonthread;
	ppscreen.tex_height = source_surface->height_nonthread;

	gfxPostprocessScreen(&ppscreen);
}

static float PP_TWOTEX_SAMPLE_OFFSET = -0.375f;
AUTO_CMD_FLOAT(PP_TWOTEX_SAMPLE_OFFSET, PP_TWOTEX_SAMPLE_OFFSET);

void gfxPostprocessTwoTex(RdrSurface *primary_source_surface, TexHandle primary_source_tex, TexHandle secondary_source_tex, int shader_num, Vec4 *constants, int num_constants, bool write_depth, RdrPPDepthTestMode depth_test, RdrPPBlendType blend_type, bool viewport_independent_textures, bool offset_for_downsample)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[2];

	textures[0] = primary_source_tex;
	textures[1] = secondary_source_tex;

	ppscreen.material.constants = constants;
	ppscreen.material.const_count = num_constants;

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(shader_num);

	ppscreen.blend_type = blend_type;

	ppscreen.write_depth = write_depth;
	ppscreen.depth_test_mode = depth_test;

	ppscreen.tex_width = primary_source_surface->width_nonthread;
	ppscreen.tex_height = primary_source_surface->height_nonthread;
	if (offset_for_downsample)
	{
		ppscreen.tex_offset_x = PP_TWOTEX_SAMPLE_OFFSET;
		ppscreen.tex_offset_y = PP_TWOTEX_SAMPLE_OFFSET;
	}
	ppscreen.viewport_independent_textures = viewport_independent_textures;

	gfxPostprocessScreen(&ppscreen);
}

static void gfxPostprocessPreload(GfxSpecialShader shader_num)
{
#if PLATFORM_CONSOLE // No need to warm it up/draw with it.
	gfxDemandLoadSpecialShader(shader_num);
#else
	RdrSurface * activeSurface = gfxGetActiveSurface();
	bool bDeviceIsDX11 = !strcmp(gfx_state.currentDevice->rdr_device->getProfileName(gfx_state.currentDevice->rdr_device), "D3D11");
	if (shader_num == GSS_DEPTH_AS_COLOR_MSAA_2 && (!bDeviceIsDX11 || activeSurface->params_nonthread.desired_multisample_level != 2))
		return;
	if (shader_num == GSS_DEPTH_AS_COLOR_MSAA_4 && (!bDeviceIsDX11 || activeSurface->params_nonthread.desired_multisample_level != 4))
		return;
	if (shader_num == GSS_DEPTH_AS_COLOR_MSAA_8 && (!bDeviceIsDX11 || activeSurface->params_nonthread.desired_multisample_level != 8))
		return;
	if (shader_num == GSS_DEPTH_AS_COLOR_MSAA_16 && (!bDeviceIsDX11 || activeSurface->params_nonthread.desired_multisample_level != 16))
		return;
	gfxPostprocessTwoTex(activeSurface, texDemandLoadFixed(white_tex), texDemandLoadFixed(white_tex), shader_num,
						 NULL, 0, 0, RPPDEPTHTEST_OFF, RPPBLEND_REPLACE, false, false);
#endif
}

static bool g_postprocess_do_preload_next_frame;
void gfxPostprocessDoPreloadNextFrame(void)
{
	g_postprocess_do_preload_next_frame = true;
}

void gfxPostprocessCheckPreload(void)
{
	int i, j;
	int count=0;
	if (!g_postprocess_do_preload_next_frame)
		return;
	g_postprocess_do_preload_next_frame = false;
	if (!(system_specs.material_hardware_supported_features & SGFEAT_SM30))
		return;
	loadend_printf("done.");
	loadstart_printf("Preloading postprocessing shaders...");

	{
		const char *intrinsic_defines;
		cryptAdler32Init();
		intrinsic_defines = rdrGetIntrinsicDefines(gfx_state.currentDevice->rdr_device);
		cryptAdler32Update(intrinsic_defines, (int)strlen(intrinsic_defines));
		sprintf(current_intrinsic_defines_crc, "__%08x", cryptAdler32Final());
	}

	// Simple shaders, just bind them and draw something with them
	for (i=GSS_BeginSimpleShaders+1; i<GSS_Max; i++)
	{
		count++;
		gfxPostprocessPreload(i);
	}

	// Complex shaders, can be compiled with a bunch of different light types
	if( gfx_state.settings.features_supported & GFEATURE_SHADOWS )
	{
		for (i=GSS_BeginShadowBufferShadersToPreload+1; i<GSS_BeginSimpleShaders; i++) 
		{
			Vec4 constants[NUM_SSAO_CONSTANTS + NUM_SSAO_SAMPLES];
			TexHandle textures[3];
			RdrScreenPostProcess ppscreen = {0};
			int light[2];
			RdrSurface *active_surface = gfxGetActiveSurface();

			for (j=0; j<ARRAY_SIZE(constants); j++) 
				setVec4(constants[j], 0.5, 0.6, 0.7, 0.8);

			textures[0] = texDemandLoadFixed(white_tex);
			textures[1] = texDemandLoadFixed(white_tex); // texDemandLoadInline(dummy_cube_tex);
			textures[2] = texDemandLoadFixed(white_tex);
			// Use active surface size as it's most likely to match with what actually happens later
			ppscreen.tex_width = active_surface->width_nonthread;
			ppscreen.tex_height = active_surface->height_nonthread;
			ppscreen.material.textures = textures;
			ppscreen.material.tex_count = 1;
			ppscreen.blend_type = RPPBLEND_REPLACE;
			ppscreen.shadow_buffer_render = 1;
			ppscreen.material.constants = constants;
			ppscreen.material.const_count = 0;
			if (i==GSS_DEFERRED_SHADOW_SSAO || i==GSS_DEFERRED_SHADOW_SSAO_POISSON
				|| i== GSS_DEFERRED_SHADOW_SSAO_STEREOSCOPIC || i==GSS_DEFERRED_SHADOW_SSAO_POISSON_STEREOSCOPIC
				)
			{
				ppscreen.material.const_count = ARRAY_SIZE(constants);
				ppscreen.material.tex_count = 3;
			}
			else if (i==GSS_DEFERRED_SSAO_PREPASS)
			{
				ppscreen.material.const_count = ARRAY_SIZE(constants);
				ppscreen.material.tex_count = 2;
			}

			if (i == GSS_CALCULATE_SCATTERING || i == GSS_CALCULATE_SCATTERING_STEREOSCOPIC)
			{
				// Excluded, not doing scattering it will generate compile errors on pure SM30 cards
				continue;
			}
			else if (  i == GSS_DEFERRED_SHADOW_POISSON || i == GSS_DEFERRED_SHADOW_POISSON_STEREOSCOPIC
					|| i == GSS_DEFERRED_SHADOW_SSAO || i== GSS_DEFERRED_SHADOW_SSAO_STEREOSCOPIC 
					|| i == GSS_DEFERRED_SHADOW_SSAO_POISSON || i==GSS_DEFERRED_SHADOW_SSAO_POISSON_STEREOSCOPIC
				)
			{
				if (!(systemSpecsMaterialSupportedFeatures() & SGFEAT_SM30_PLUS))
					continue;
			}

			// Assume max of 2 lights
			for (light[0]=1; light[0]<RDRLIGHT_TYPE_MAX; light[0]++)
			{
				for (light[1]=0; light[1]<RDRLIGHT_TYPE_MAX; light[1]++)
				{
					RdrMaterialShader shader_num;
					RdrLight rdr_lights_data[MAX_NUM_OBJECT_LIGHTS] = {0};
					RdrLight *rdr_lights[MAX_NUM_OBJECT_LIGHTS] = {0};
					RdrLightData light_data[MAX_NUM_OBJECT_LIGHTS] = {0};

					shader_num.key = 0;

					if (light[1] && light[1]<light[0])
						continue;

					for (j=0; j<2; j++) {
						if (!light[j])
							continue;
						light_data[j].light_type = light[j];
						rdr_lights[j] = &rdr_lights_data[j];
						rdr_lights[j]->light_type = light[j];
						rdr_lights[j]->projector_params.projected_tex = texDemandLoadFixed(white_tex);
						ppscreen.lights[j] = &light_data[j];
						shader_num.lightMask |= rdrGetMaterialShaderType(light_data[j].light_type|RDRLIGHT_SHADOWED, j);
					}

					ppscreen.shader_handle = gfxDemandLoadSpecialShaderEx(i, 
																		  ppscreen.material.const_count, ppscreen.material.tex_count, 
																		  shader_num, NULL);
					#if PLATFORM_CONSOLE // No need to warm it up/draw with it.
					#else
					gfxPostprocessScreen(&ppscreen);
					#endif
					count++;
				}
			}
		}
	}
	current_intrinsic_defines_crc[0] = 0;
	loadend_printf("done (%d shaders).", count);
	loadstart_printf("Waiting for compiles/loads to finish...");
}

static void fillDofConstants(Vec4 constants[6], const DOFValues *dof_values, const SkyTimeColorCorrection *color_correction_values)
{
	F32 skyDepthFade = 50;
	F32 skyDepthStart = gfx_state.far_plane_dist - skyDepthFade;

	setVec4(constants[0],	dof_values->focusDist,
		dof_values->focusValue,
		// Only technically valid to avoid div zero here if the numerator is also 0, otherwise it's a bad sky file
		( dof_values->focusValue - dof_values->nearValue ) / AVOID_DIV_0( dof_values->focusDist - dof_values->nearDist ), 
		( dof_values->farValue - dof_values->focusValue ) / ( dof_values->farDist - dof_values->focusDist ));

	setVec4(constants[1],	-skyDepthStart / skyDepthFade, 
		1.f / skyDepthFade, 
		dof_values->skyValue, 
		SAFE_MEMBER(color_correction_values, local_contrast_scale));

	hsvToRgb(dof_values->borderColorHSV, constants[2]);
	scaleVec3(constants[2], dof_values->borderColorScale, constants[2]);
	constants[2][3] = dof_values->borderRamp;
	constants[3][0] = dof_values->borderBlur;
	constants[3][1] = randomF32BlornFixedSeed(gfx_state.frame_count);
	constants[3][2] = SAFE_MEMBER(color_correction_values, unsharp_amount);
	constants[3][3] = SAFE_MEMBER(color_correction_values, unsharp_threshold);

	constants[4][0] = dof_values->depthAdjustFgHSV[1];
	constants[4][1] = dof_values->depthAdjustFgHSV[2];
	constants[4][2] = dof_values->depthAdjustBgHSV[1];
	constants[4][3] = dof_values->depthAdjustBgHSV[2];
	constants[5][0] = dof_values->depthAdjustSkyHSV[1];
	constants[5][1] = dof_values->depthAdjustSkyHSV[2];
	constants[5][2] = dof_values->depthAdjustNearDist;
	if(dof_values->depthAdjustFadeDist != 0.f){
		constants[5][3] = 1.f/dof_values->depthAdjustFadeDist;
	}else{
		constants[5][3] = 0.f;
	}
}

static void gfxDoSeparableBlur(RdrSurface *source_surface, RdrSurfaceBuffer source_buffer, int source_snapshot_idx, 
							   TexHandle depth_tex_handle, TexHandle lc_blur_tex_handle, bool vertical, 
							   GfxBlurType blur_type, F32 kernel_size, F32 smart_blur_threshold, 
							   const DOFValues *dof_values, const SkyTimeColorCorrection *color_correction_values)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[3];
	Vec4 constants[7];
	GfxSpecialShader shader_num;

	if (blur_type == GBT_BOX || blur_type == GBT_GAUSSIAN)
		shader_num = GSS_BLUR_START;
	else if (blur_type == GBT_DOF)
		shader_num = GSS_DOF_BLUR_START;
	else
		shader_num = GSS_SMART_BLUR_START;

	if (kernel_size <= 3.f)
		shader_num += 0;
	else if (kernel_size <= 5.f)
		shader_num += 2;
	else if (kernel_size <= 7.f)
		shader_num += 4;
	else if (kernel_size <= 9.f)
		shader_num += 6;
	else
		shader_num += 8;

	if (vertical)
		shader_num++;

	switch (blur_type)
	{
		xcase GBT_DOF:
		{
			fillDofConstants(&constants[1], dof_values, color_correction_values);
		}

		xcase GBT_SMART:
		{
			zeroVec4(constants[1]);
			zeroVec4(constants[2]);
		}

		xcase GBT_GAUSSIAN:
		{
			if (kernel_size <= 3.f)
			{
				setVec4(constants[1], (1.f/4.f), (2.f/4.f), 0, 0);
				setVec4(constants[2], 0, 0, 0, 0);
			}
			else if (kernel_size <= 5.f)
			{
				setVec4(constants[1], (1.f/16.f), (4.f/16.f), (6.f/16.f), 0);
				setVec4(constants[2], 0, 0, 0, 0);
			}
			else if (kernel_size <= 7.f)
			{
				setVec4(constants[1], (1.f/64.f), (6.f/64.f), (15.f/64.f), (20.f/64.f));
				setVec4(constants[2], 0, 0, 0, 0);
			}
			else if (kernel_size <= 9.f)
			{
				setVec4(constants[1], (1.f/256.f), (8.f/256.f), (28.f/256.f), (56.f/256.f));
				setVec4(constants[2], (70.f/256.f), 0, 0, 0);
			}
			else
			{
				setVec4(constants[1], (1.f/1024.f), (10.f/1024.f), (45.f/1024.f), (120.f/1024.f));
				setVec4(constants[2], (210.f/1024.f), (252.f/1024.f), 0, 0);
			}
		}
		
		xcase GBT_BOX:
		{
			if (kernel_size <= 3.f)
			{
				setVec4(constants[1], (1.f/3.f), (1.f/3.f), 0, 0);
				setVec4(constants[2], 0, 0, 0, 0);
			}
			else if (kernel_size <= 5.f)
			{
				setVec4(constants[1], (1.f/5.f), (1.f/5.f), (1.f/5.f), 0);
				setVec4(constants[2], 0, 0, 0, 0);
			}
			else if (kernel_size <= 7.f)
			{
				setVec4(constants[1], (1.f/7.f), (1.f/7.f), (1.f/7.f), (1.f/7.f));
				setVec4(constants[2], 0, 0, 0, 0);
			}
			else if (kernel_size <= 9.f)
			{
				setVec4(constants[1], (1.f/9.f), (1.f/9.f), (1.f/9.f), (1.f/9.f));
				setVec4(constants[2], (1.f/9.f), 0, 0, 0);
			}
			else
			{
				setVec4(constants[1], (1.f/11.f), (1.f/11.f), (1.f/11.f), (1.f/11.f));
				setVec4(constants[2], (1.f/11.f), (1.f/11.f), 0, 0);
			}
		}
	}

	setVec4(constants[0], 
				1.f/source_surface->vwidth_nonthread, 
				1.f/source_surface->vheight_nonthread, 
				smart_blur_threshold ? (-1.f / (smart_blur_threshold * 4)) : 0, 
				smart_blur_threshold ? (10.f / (smart_blur_threshold * 5)) : 0);

	ppscreen.material.const_count = ARRAY_SIZE(constants);
	ppscreen.material.constants = constants;

	textures[0] = rdrSurfaceToTexHandleEx(source_surface, source_buffer, source_snapshot_idx, 0, false);
	textures[1] = depth_tex_handle;
	textures[2] = lc_blur_tex_handle;

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(shader_num);

	ppscreen.tex_width = source_surface->width_nonthread;
	ppscreen.tex_height = source_surface->height_nonthread;

	gfxPostprocessScreen(&ppscreen);
}

void gfxDoBlur(GfxBlurType blur_type, RdrSurface *dest_surface, RdrSurface *temp_surface, 
			   RdrSurface *source_surface, RdrSurfaceBuffer source_buffer, TexHandle depth_tex_handle, TexHandle lc_blur_tex_handle, 
			   F32 kernel_size, F32 smart_blur_threshold, const DOFValues *dof_values, const SkyTimeColorCorrection *color_correction_values)
{
	if (kernel_size <= 0)
	{
		// make kernel size depend on screen resolution (NOT surface resolution)
		U32 renderSize[2];
		gfxGetRenderSizeFromScreenSize(renderSize);
		kernel_size = -kernel_size * MAX(renderSize[0], renderSize[1]) / 1280.f;
	}

	gfxSetActiveSurface(temp_surface ? temp_surface : dest_surface);
	gfxDoSeparableBlur(source_surface, source_buffer, 0, depth_tex_handle, lc_blur_tex_handle, false, blur_type, kernel_size, smart_blur_threshold, dof_values, color_correction_values);

	if (temp_surface)
	{
		gfxSetActiveSurface(dest_surface);
		gfxDoSeparableBlur(temp_surface, SBUF_0, 0, depth_tex_handle, lc_blur_tex_handle, true, blur_type, kernel_size, smart_blur_threshold, dof_values, color_correction_values);
	}
	else
	{
		int snapshot_idx = 1; // is this a bad assumption?
		rdrSurfaceSnapshot(dest_surface, "gfxDoBlur", snapshot_idx);
		gfxDoSeparableBlur(dest_surface, SBUF_0, snapshot_idx, depth_tex_handle, lc_blur_tex_handle, true, blur_type, kernel_size, smart_blur_threshold, dof_values, color_correction_values);
	}
}

static void doShrink4Ex(RdrSurface *source_surface, TexHandle tex_handle, ShrinkMode mode, const Vec4 tex_transform, const Vec4 highpass_values, bool viewport_independent_textures)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[1];
	Vec4 constants[2];
	int special_shader;
	bool shrink2 = true; // !tex_transform;

	if (tex_transform)
	{
		copyVec4(tex_transform, constants[0]);
	}
	else
	{
		setVec4(constants[0], 0.5f/source_surface->vwidth_nonthread, 0.5f/source_surface->vheight_nonthread, 0, 0);
	}

	if (highpass_values)
		copyVec4(highpass_values, constants[1]);
	else
		copyVec4(zerovec4, constants[1]);

	switch (mode)
	{
		xcase SM_LOG:
			special_shader = shrink2?GSS_SHRINK4_2_LOG:GSS_SHRINK4_LOG;

		xcase SM_EXP:
			special_shader = shrink2?GSS_SHRINK4_2_EXP:GSS_SHRINK4_EXP;

		xcase SM_NORMAL:
			special_shader = shrink2?GSS_SHRINK4_2:GSS_SHRINK4;

		xcase SM_HIGHPASS:
			special_shader = shrink2?GSS_SHRINK4_2_HIGHPASS:GSS_SHRINK4_HIGHPASS;

		xcase SM_BLOOM_CURVE:
			special_shader = shrink2?GSS_SHRINK4_2_BLOOM_CURVE:GSS_SHRINK4_BLOOM_CURVE;

		xcase SM_MAX:
			shrink2 = false;
			special_shader = shrink2?GSS_SHRINK4_2_MAX:GSS_SHRINK4_MAX;

		xcase SM_MAX_LUMINANCE:
			shrink2 = false;
			special_shader = GSS_SHRINK4_MAX_LUMINANCE;

		xcase SM_DEPTH:
			special_shader = shrink2?GSS_SHRINK4_2_DEPTH:GSS_SHRINK4_DEPTH;
			ppscreen.write_depth = true;
			ppscreen.depth_test_mode = RPPDEPTHTEST_OFF;

		xdefault:
			devassertmsg(0, "Invalid shrink mode.");
			return;
	}

	ppscreen.material.constants = constants;
	ppscreen.material.const_count = ARRAY_SIZE(constants);

	textures[0] = tex_handle;
	if (shrink2)
		rdrAddRemoveTexHandleFlags(&textures[0], 0, RTF_MIN_POINT|RTF_MAG_POINT); // bilinear filter

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(special_shader);

	ppscreen.tex_width = source_surface->width_nonthread;
	ppscreen.tex_height = source_surface->height_nonthread;
	ppscreen.viewport_independent_textures = viewport_independent_textures;

	gfxPostprocessScreen(&ppscreen);
}

static void doShrink4(RdrSurface *source_surface, RdrSurfaceBuffer source_buffer, int source_set_index, ShrinkMode mode, Vec4 tex_transform)
{
	TexHandle tex_handle = rdrSurfaceToTexHandleEx(source_surface, source_buffer, source_set_index, 0, false);
	doShrink4Ex(source_surface, tex_handle, mode, tex_transform, NULL, false);
}

void gfxShrink4(RdrSurface *source_surface, RdrSurfaceBuffer source_buffer, int source_set_index, bool viewport_independent_textures)
{
	TexHandle tex_handle = rdrSurfaceToTexHandleEx(source_surface, source_buffer, source_set_index, 0, false);
	doShrink4Ex(source_surface, tex_handle, SM_NORMAL, NULL, NULL, viewport_independent_textures);
}

void gfxShrink4Depth(RdrSurface *source_surface, RdrSurfaceBuffer source_buffer, int source_set_index)
{
	doShrink4(source_surface, source_buffer, source_set_index, SM_DEPTH, NULL);
}

static void adaptLightRange(const BlendedSkyInfo *sky_info, F32 new_light_range)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	GfxCameraView *camera_view = gfx_state.currentCameraView;
	F32 timestep;

	if (!gfx_state.debug.hdr_2 && !gfx_state.settings.hdr_max_luminance_mode)
		// multiply by two to put measured luminance in the middle of the light range
		new_light_range *= 2;

	// apply light adaptation amount
	new_light_range = lerp(camera_view->desired_light_range, new_light_range, saturate(sky_info->lightBehaviorValues.lightAdaptationAmount));

	// apply exposure
	if (sky_info->lightBehaviorValues.exposure > 0.1f)
		new_light_range = new_light_range / sky_info->lightBehaviorValues.exposure;

	if (!camera_view->adapted_light_range_inited)
	{
		camera_view->adapted_light_range_inited = true;
		camera_view->adapted_light_range = new_light_range;
	}

	// step the light range linearly toward the new light range
	timestep = gfx_state.frame_time / MAX(0.001f, sky_info->lightBehaviorValues.lightAdaptationRate);
	if (new_light_range > camera_view->adapted_light_range)
	{
		camera_view->adapted_light_range += timestep;
		MIN1(camera_view->adapted_light_range, new_light_range);
	}
	else
	{
		camera_view->adapted_light_range -= timestep;
		MAX1(camera_view->adapted_light_range, new_light_range);
	}

	if (!FINITE(camera_view->adapted_light_range))
	{
		camera_view->adapted_light_range = 1;
		camera_view->adapted_light_range_inited = 0;
	}

	camera_view->adapted_light_range = MAX(0.01f, camera_view->adapted_light_range);
}

void gfxCalcHDRTransform(const BlendedSkyInfo *sky_info)
{
	GfxCameraView *camera_view = gfx_state.currentCameraView;

	if (gfx_state.debug.hdr_2)
	{
		// scale bloom range & offset by the current adaptation
		F32 max_lum = camera_view->adapted_light_range * ( 1 + sky_info->bloomValues.bloomRange + sky_info->bloomValues.bloomOffsetValue );

		// LDR transform
		camera_view->exposure_transform[0] = 2.f / max_lum;
		camera_view->exposure_transform[1] = max_lum * 0.5f;

		// HDR transform
		camera_view->exposure_transform[2] = 1.f / max_lum;
		camera_view->exposure_transform[3] = max_lum;
	}
	else
	{
		F32 max_lum = camera_view->adapted_light_range + sky_info->bloomValues.bloomRange + sky_info->bloomValues.bloomOffsetValue;

		// LDR transform
		camera_view->exposure_transform[0] = 1.f / camera_view->adapted_light_range;
		camera_view->exposure_transform[1] = camera_view->adapted_light_range;

		// HDR transform
		camera_view->exposure_transform[2] = 1.f / max_lum;
		camera_view->exposure_transform[3] = max_lum;
	}

	if (gfx_state.debug.hdr_lock_ldr_xform)
	{
		camera_view->exposure_transform[0] = 1.0f / gfx_state.debug.hdr_lock_ldr_xform;
		camera_view->exposure_transform[1] = gfx_state.debug.hdr_lock_ldr_xform;
	}
	if (gfx_state.debug.hdr_lock_hdr_xform)
	{
		camera_view->exposure_transform[2] = 1.0f / gfx_state.debug.hdr_lock_hdr_xform;
		camera_view->exposure_transform[3] = gfx_state.debug.hdr_lock_hdr_xform;
	}

	copyVec4(camera_view->exposure_transform, gfx_state.debug.exposure_transform);
}

void gfxDoSoftwareLightAdaptation(const BlendedSkyInfo *sky_info)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	GfxCameraView *camera_view = gfx_state.currentCameraView;

	// only adapt once per frame
	if (camera_view->last_light_range_update_time != gfx_state.client_frame_timestamp)
	{
		camera_view->last_light_range_update_time = gfx_state.client_frame_timestamp;
		adaptLightRange(sky_info, camera_view->desired_light_range);
	}
}

#define LUM_QUERY_INCREMENTS (0.1f)
static const int LUMINANCE_SAMPLES_PER_PIXEL = 256;

RdrOcclusionQueryResult * gfxUpdateRunningQuery(GfxRunningQuery * running_query, RdrSurface *source_surface, F32 default_result)
{
	RdrOcclusionQueryResult *finished_query = NULL, *next_query = NULL;
	F32 query_result;
	int i;

	// find most recent finished query
	for (i = 0; i < eaSize(&running_query->queries); ++i)
	{
		RdrOcclusionQueryResult *query = running_query->queries[i];
		if (query->device == gfx_state.currentDevice->rdr_device && 
			query->data_ready && !query->failed && 
			(!finished_query || query->frame_ready > finished_query->frame_ready) &&
			query->pixel_count <= query->max_pixel_count)
		{
			finished_query = query;
		}
	}

	for (i = 0; i < eaSize(&running_query->queries); ++i)
	{
		RdrOcclusionQueryResult *query = running_query->queries[i];
		if (query->device == gfx_state.currentDevice->rdr_device && 
			query->data_ready && query != finished_query && 
			(!next_query || query->frame_ready < next_query->frame_ready))
		{
			next_query = query;
		}
	}

	// calculate luminance value from last finished query and store in luminance history
	if (finished_query || !running_query->count)
	{
		int current_luminance_idx = running_query->idx;
		running_query->idx++;
		if (running_query->idx >= ARRAY_SIZE(running_query->history))
			running_query->idx = 0;

		query_result = default_result;
		if (finished_query)
		{
			query_result = finished_query->pixel_count * finished_query->query_user_data;
			// TODO DJR make a general option on the query result for the half-step offet
			// instead of tying it to hdr 2 enabled
			if (gfx_state.debug.hdr_2)
				query_result += 0.5f * finished_query->query_user_data;
		}

		running_query->history[running_query->idx] = query_result;
		running_query->count++;
	}

	running_query->count = CLAMP(running_query->count, 1, ARRAY_SIZE(running_query->history));

	if (!next_query && eaSize(&running_query->queries) < 40)
	{
		next_query = rdrAllocOcclusionQuery(gfx_state.currentDevice->rdr_device);
		eaPush(&running_query->queries, next_query);
	}

	if (next_query)
		next_query->data_ready = false;

	return next_query;
}

static F32 updateHDRPointQuery(GfxCameraView * camera_view, RdrSurface *source_surface)
{
	F32 luminance;
	RdrOcclusionQueryResult * next_query = gfxUpdateRunningQuery(&camera_view->hdr_point_query, 
		source_surface, 0.5f);

	// start next HDR point test
	if (next_query)
	{
		RdrScreenPostProcess ppscreen = {0};
		TexHandle textures[1];
		Vec4 constants[1];

		next_query->query_user_data = 1.0f / 4096;
		
		ppscreen.occlusion_query = next_query;

		zeroVec4(constants[0]);
		constants[0][0] = camera_view->adapted_light_range * gfx_state.debug.hdr_luminance_point;
		ppscreen.material.constants = constants;
		ppscreen.material.const_count = ARRAY_SIZE(constants);

		textures[0] = rdrSurfaceToTexHandle(source_surface, SBUF_0);
		ppscreen.material.tex_count = ARRAY_SIZE(textures);
		ppscreen.material.textures = textures;

		ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_MEASURE_HDR_POINT);

		ppscreen.tex_width = source_surface->width_nonthread;
		ppscreen.tex_height = source_surface->height_nonthread;

		// measure at fixed increments TODO(CD) use smaller increments around currently adapted light range
		ppscreen.draw_count = 1;
		ppscreen.const_increment = 0.0f;

		gfxPostprocessScreen(&ppscreen);
	}

	// return luminance depending on the relative percents of pixels above or below
	// the desired HDR point
	luminance = camera_view->hdr_point_query.history[camera_view->hdr_point_query.idx] - 0.05f;
	return luminance;
}

// Do not call this function directly.  Please use doLuminanceMeasurement instead.
static F32 updateLuminanceQuery(GfxCameraView * camera_view, RdrSurface *source_surface, int use_max_lum)
{
	F32 luminance;
	int i;
	RdrOcclusionQueryResult * next_query = gfxUpdateRunningQuery(&camera_view->avg_luminance_query, 
		source_surface, camera_view->adapted_light_range);

	// start next lum test
	if (next_query)
	{
		RdrScreenPostProcess ppscreen = {0};
		TexHandle textures[1];
		Vec4 constants[1];

		if (use_max_lum)
		{
			// get an accurate result up to 20% higher than current light range, clamped if greater
			F32 next_estimated_max_lum;
			next_estimated_max_lum = 
				camera_view->avg_luminance_query.count ? camera_view->avg_luminance_query.history[camera_view->avg_luminance_query.idx] :
				camera_view->adapted_light_range;
			if (!next_estimated_max_lum)
				next_estimated_max_lum = 1.0f;
			next_estimated_max_lum *= 1.2f / LUMINANCE_SAMPLES_PER_PIXEL;
			next_query->query_user_data = next_estimated_max_lum;
		}
		else
			next_query->query_user_data = LUM_QUERY_INCREMENTS;

		ppscreen.occlusion_query = next_query;

		zeroVec4(constants[0]);
		ppscreen.material.constants = constants;
		ppscreen.material.const_count = ARRAY_SIZE(constants);

		textures[0] = rdrSurfaceToTexHandle(source_surface, SBUF_0);
		ppscreen.material.tex_count = ARRAY_SIZE(textures);
		ppscreen.material.textures = textures;

		ppscreen.shader_handle = gfxDemandLoadSpecialShader(use_max_lum ? GSS_MEASURE_LUMINANCE_HDR2 : GSS_MEASURE_LUMINANCE);

		ppscreen.tex_width = source_surface->width_nonthread;
		ppscreen.tex_height = source_surface->height_nonthread;

		// measure at fixed increments TODO(CD) use smaller increments around currently adapted light range
		if (use_max_lum)
		{
			ppscreen.measurement_mode = 1;
			ppscreen.draw_count = 1;
			ppscreen.const_increment = next_query->query_user_data;
		}
		else
		{
			// measure at fixed increments TODO(CD) use smaller increments around currently adapted light range
			ppscreen.draw_count = round(ceilf(camera_view->desired_light_range * 1.5f / LUM_QUERY_INCREMENTS));
			ppscreen.const_increment = LUM_QUERY_INCREMENTS;
		}

		gfxPostprocessScreen(&ppscreen);
	}

	// run debug commands on the last calculated luminance
	if (camera_view->avg_luminance_query.count)
	{
		int current_luminance_idx = camera_view->avg_luminance_query.idx;

		// TODO DJR remove or disable (on a compile switch) the HDR debug code after testing
		if (gfx_state.debug.hdr_force_luminance_measurement)
			camera_view->avg_luminance_query.history[current_luminance_idx] = gfx_state.debug.hdr_force_luminance_measurement;
		else
		if (gfx_state.debug.hdr_use_immediate_luminance_measurement)
		{
			U8 *data;
			float actual_lum;
			GfxRenderAction *action = gfx_state.currentAction;

			gfxSetActiveSurface(action->lum1->surface);
			data = rdrGetActiveSurfaceData(gfx_state.currentDevice->rdr_device, SURFDATA_RGB_F32, 1, 1);
			actual_lum = *(const F32*)data;
			free(data);

			camera_view->avg_luminance_query.history[current_luminance_idx] = actual_lum;
		}
	}

	// find average luminance over the entire luminance history
	luminance = 0;
	for (i = 0; i < camera_view->avg_luminance_query.count; ++i)
		luminance += camera_view->avg_luminance_query.history[i];
	luminance /= camera_view->avg_luminance_query.count;

	return luminance;
}

static F32 doLuminanceMeasurement(GfxCameraView *camera_view, RdrSurface *source_surface, int use_max_lum)
{
	return gfx_state.settings.bloomQuality < GBLOOM_MED_BLOOM_SMALLHDR ? 0 : updateLuminanceQuery(camera_view, source_surface, use_max_lum);
}

static void gfxDoLightAdaptation(const BlendedSkyInfo *sky_info)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	GfxCameraView *camera_view = gfx_state.currentCameraView;
	GfxRenderAction *action = gfx_state.currentAction;
	RdrSurface* shrink_hdr_surf;
	Vec4 vec;
	int w, h, vw, vh;

	gfxBeginSection("PP: gfxDoLightAdaptation");
	PERFINFO_AUTO_START_FUNC();

	if (gfx_state.settings.bloomQuality < GBLOOM_MED_BLOOM_SMALLHDR && !action->is_offscreen)
	{
		gfxDoSoftwareLightAdaptation(sky_info);
	}
	else
	{
		// only adapt once per frame
		if (camera_view->last_light_range_update_time != gfx_state.client_frame_timestamp)
		{
			int use_max_lum = gfx_state.settings.hdr_max_luminance_mode;
			RdrSurface *src_surface = gfxGetStageInputSurface(GFX_SHRINK_HDR, 0);
			F32 measured_luminance;
			int shrink_first = SM_LOG, shrink_middle = SM_NORMAL, shrink_last = SM_EXP;
			if (use_max_lum)
			{
				shrink_first = SM_MAX_LUMINANCE;
				shrink_middle = shrink_last = SM_MAX;
			}
			
			camera_view->last_light_range_update_time = gfx_state.client_frame_timestamp;

			gfxSetActiveSurface(action->lum64->surface);
			w = src_surface->width_nonthread;
			h = src_surface->height_nonthread;
			vw = src_surface->vwidth_nonthread;
			vh = src_surface->vheight_nonthread;
			setVec4(vec, w/(8.f*64.f*vw), h/(8.f*64.f*vh), 0, 0);
			doShrink4(src_surface, SBUF_0, 0, shrink_first, vec);

			gfxSetActiveSurface(action->lum16->surface);
			doShrink4(action->lum64->surface, SBUF_0, RDRSURFACE_SET_INDEX_DEFAULT, shrink_middle, NULL);

			gfxSetActiveSurface(action->lum4->surface);
			doShrink4(action->lum16->surface, SBUF_0, RDRSURFACE_SET_INDEX_DEFAULT, shrink_middle, NULL);

			gfxSetActiveSurface(action->lum1->surface);
			doShrink4(action->lum4->surface, SBUF_0, RDRSURFACE_SET_INDEX_DEFAULT, shrink_last, NULL);

			if (gfxUFIsCurrentActionBloomDebug())
			{
				gfxDebugThumbnailsAddSurface(action->lum64->surface, SBUF_0, 0, "Lum64", 0);
				gfxDebugThumbnailsAddSurface(action->lum16->surface, SBUF_0, 0, "Lum16", 0);
				gfxDebugThumbnailsAddSurface(action->lum4->surface, SBUF_0, 0, "Lum4", 0);
				gfxDebugThumbnailsAddSurface(action->lum1->surface, SBUF_0, 0, "Lum1", 0);
			}

			gfxSetActiveSurface(use_max_lum ? action->lum16->surface : action->lum_measure->surface);
			if (use_max_lum && gfxUFIsCurrentActionBloomDebug())
				gfxClearActiveSurfaceHDR(zerovec4, 0, false);
			measured_luminance = doLuminanceMeasurement(camera_view, action->lum1->surface, use_max_lum);
			if (use_max_lum && gfxUFIsCurrentActionBloomDebug())
				gfxDebugThumbnailsAddSurfaceCopy(action->lum16->surface, SBUF_0, 0, "Lum test", 0);
			gfx_state.debug.measured_luminance = measured_luminance;

			adaptLightRange(sky_info, measured_luminance);
		}
	}

	gfxSetStageActiveSurface(GFX_SHRINK_HDR);
	if(   action->renderViewport[0] != 1 || action->renderViewport[1] != 1
		  || action->renderViewport[2] != 0 || action->renderViewport[3] != 0 ) {
		gfxClearActiveSurfaceHDR(zerovec4, 0, false);
	}
	shrink_hdr_surf = gfxGetStageInputSurface(GFX_SHRINK_HDR, 0);

	if (gfx_state.settings.bloomQuality == GBLOOM_MAX_BLOOM_DEFERRED)
	{
		Vec4 highpass_values;
		// run highpass on the LDR buffer
		setVec4(highpass_values,	sky_info->bloomValues.lowQualityBloomStart, 
			sky_info->bloomValues.lowQualityBloomMultiplier,
			sky_info->bloomValues.lowQualityBloomPower, 
			gfxSkyGetBloomScale(sky_info, true));
		doShrink4Ex(shrink_hdr_surf, gfxGetStageInputTexHandle(GFX_SHRINK_HDR, 0), SM_HIGHPASS, NULL, highpass_values, false);
	}
	else
	if (gfx_state.settings.bloomQuality == GBLOOM_HIGH_BLOOM_FULLHDR)
	{
		// shrink and bloom curve
		Vec4 bloom_curve;
		setVec4(bloom_curve, sky_info->bloomValues.bloomOffsetValue, sky_info->bloomValues.bloomRate, 0, 0);
		doShrink4Ex(shrink_hdr_surf, gfxGetStageInputTexHandle(GFX_SHRINK_HDR, 0), SM_BLOOM_CURVE, NULL, bloom_curve, false);
	}
	else if (gfx_state.settings.bloomQuality == GBLOOM_MED_BLOOM_SMALLHDR)
	{
		// run bloom curve
		Vec4 bloom_curve;
		setVec4(bloom_curve, sky_info->bloomValues.bloomOffsetValue, sky_info->bloomValues.bloomRate, 0, 0);
		gfxPostprocessOneTex(shrink_hdr_surf, gfxGetStageInputTexHandle(GFX_SHRINK_HDR, 0), GSS_BLOOMCURVE, &bloom_curve, 1, RPPBLEND_REPLACE);
		gfxDoMeanBlurInPlace(action->bloom[1]->surface, action->bloom[0]->surface, -3);
	}
	else if (gfx_state.settings.bloomQuality == GBLOOM_LOW_BLOOMWHITE)
	{
		Vec4 highpass_values;
		// run highpass on the LDR buffer
		setVec4(highpass_values,	sky_info->bloomValues.lowQualityBloomStart, 
									sky_info->bloomValues.lowQualityBloomMultiplier,
									sky_info->bloomValues.lowQualityBloomPower, 
									gfxSkyGetBloomScale(sky_info, true));
		doShrink4Ex(shrink_hdr_surf, gfxGetStageInputTexHandle(GFX_SHRINK_HDR, 0), SM_HIGHPASS, NULL, highpass_values, false);
	}
	else
	{
		assert(0);
	}

	if (gfxUFIsCurrentActionBloomDebug())
	{
		rdrSurfaceSnapshot(action->bloom[1]->surface, "Post Bloom Curve", 3);
		gfxDebugThumbnailsAddSurface(action->bloom[1]->surface, SBUF_0, 3, "Post Bloom Curve", 0);
	}

	PERFINFO_AUTO_STOP();
	gfxEndSection();
}

static void gfxDoLensZOTestZbuff(RdrSurface *source_surface, RdrSurface *dest_surface, Vec4 center, int flare_num)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[1];
	Vec4 constants[1];
	int w, h;
	float left_s, top_t, width_s, height_t;
	
	// Set up the area on the Z Occlusion surface that we'll be drawing to.
	Vec2 dest_top_left     = {0.25 * flare_num        + (0.5 / (float)dest_surface->width_nonthread), 1};
	Vec2 dest_bottom_right = {0.25 * flare_num + 0.25 - (0.5 / (float)dest_surface->width_nonthread), 0};

	ppscreen.material.const_count = ARRAY_SIZE(constants);

	setVec4(constants[ 0 ], center[2], 0.0f, 0.0f, 0.0f);
	ppscreen.material.constants = constants;


	textures[0] = rdrSurfaceToTexHandle(source_surface, SBUF_DEPTH);
	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;


	// set up the sample rect around the given area
	w = source_surface->width_nonthread;
	h = source_surface->height_nonthread;

	left_s = center[0] * 0.5f + 0.5f;
	top_t = 1.0 - center[1] * 0.5f - 0.5f;
	width_s = (float)32 / w;
	height_t = (float)32 / h;
	left_s -= width_s * 0.5f;
	top_t -= height_t * 0.5f;

	//< in the order: BR, BL, TL, TR
	ppscreen.use_normal_vertex_shader = true;
	ppscreen.use_texcoords = 1;
	setVec4(ppscreen.texcoords[0], left_s + width_s, top_t + height_t, 0.0f, 1.0f);
	setVec4(ppscreen.texcoords[1], left_s, top_t + height_t, 0.0f, 1.0f);
	setVec4(ppscreen.texcoords[2], left_s, top_t, 0.0f, 1.0f);
	setVec4(ppscreen.texcoords[3], left_s + width_s, top_t, 0.0f, 1.0f);


	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_TESTZOCCLUSION);

	ppscreen.tex_width = source_surface->width_nonthread;
	ppscreen.tex_height = source_surface->height_nonthread;

	ppscreen.write_depth = 0;
	ppscreen.exact_quad_coverage = 1;

	gfxPostprocessScreenPart(&ppscreen, dest_top_left, dest_bottom_right);
}

void gfxDoLensZOSample(const BlendedSkyInfo *sky_info)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	GfxCameraView *camera_view = gfx_state.currentCameraView;
	GfxRenderAction *action = gfx_state.currentAction;
	Vec4 vec;
	Vec4 center_of_light_source;
	const BlendedSkyDome * light_source;
	RdrSurface * opaque_surface;
	RdrSurface * sample_surface;
	int i;
	int numFlares = 0;

	gfxBeginSection("PP: gfxDoLensZOSample");
	PERFINFO_AUTO_START_FUNC();
	gfxSetStageActiveSurface(GFX_LENSFLARE_ZO);

	opaque_surface = gfxGetStageOutput(GFX_OPAQUE_AFTER_ZPRE);
	sample_surface = gfxGetStageOutput(GFX_LENSFLARE_ZO);

	for(i = 0; i < sky_info->skyDomeCount; i++) {

		// calculate the screen rectangle to sample the z
		light_source = sky_info->skyDomes[i];
		if(light_source && light_source->dome && light_source->dome->lens_flare) {

			addVec3(light_source->drawable->mat[3], camera_view->frustum.inv_viewmat[3], center_of_light_source);
			mulVecMat4(center_of_light_source, camera_view->frustum.viewmat, vec);
			mulVec3ProjMat44(vec, camera_view->projection_matrix, center_of_light_source);

			gfxDoLensZOTestZbuff(opaque_surface, sample_surface, center_of_light_source, numFlares++);
		}
	}

	for(i = 0; i < action->gdraw.num_object_lens_flares; i++) {
		Vec3 *lens_flare_pos = &(action->gdraw.lens_flare_positions[i]);

		mulVecMat4(*lens_flare_pos, camera_view->frustum.viewmat, vec);
		mulVec3ProjMat44(vec, camera_view->projection_matrix, center_of_light_source);

		gfxDoLensZOTestZbuff(opaque_surface, sample_surface, center_of_light_source, numFlares++);
	}

	action->gdraw.num_object_lens_flares = 0;
	action->gdraw.num_sky_lens_flares = 0;

	if (gfxDoingPostprocessing() && gfx_state.settings.lensflare_quality > 2)
	{
		gfxDebugThumbnailsAddSurface(sample_surface, SBUF_0, 0, "Lensflare ZO samples", 0);
		gfxDebugThumbnailsAddSurface(action->lens_zo_4->surface, SBUF_0, 0, "Lensflare ZO 4", 0);
		gfxDebugThumbnailsAddSurface(action->lens_zo_1->surface, SBUF_0, 0, "Lensflare ZO 1", 0);
	}

#if 0
	// 64x64 -> 16x16
	gfxSetActiveSurface(action->lens_zo_16->surface);
	doShrink4(opaque_surface, SBUF_0, RDRSURFACE_SET_INDEX_DEFAULT, SM_NORMAL, NULL);
#endif

	gfxSetActiveSurface(action->lens_zo_4->surface);
	doShrink4(action->lens_zo_16->surface, SBUF_0, RDRSURFACE_SET_INDEX_DEFAULT, SM_NORMAL, NULL);

	gfxSetActiveSurface(action->lens_zo_1->surface);
	doShrink4(action->lens_zo_4->surface, SBUF_0, RDRSURFACE_SET_INDEX_DEFAULT, SM_NORMAL, NULL);


	PERFINFO_AUTO_STOP();
	gfxEndSection();
}

static void gfxDoDofBlurCombine(const DOFValues *dof_values, const SkyTimeColorCorrection *color_correction_values, GfxSpecialShader dof_shader, RdrSurface *source_blurred_surface, 
								TexHandle depth_tex, TexHandle ldr_tex, TexHandle hdr_tex, const Vec4 *texcoords)
{
	RdrScreenPostProcess ppscreen = {0};
#ifdef DOF_GRIME
	TexHandle textures[5];
	Vec4 constants[8];
#else
	TexHandle textures[4];
	Vec4 constants[7];
#endif

	ppscreen.material.const_count = ARRAY_SIZE(constants);

	if (gfx_state.debug.dof_debug.nearDist)
		dof_values = &gfx_state.debug.dof_debug;

	setVec4(constants[ 0 ], 1.0f/source_blurred_surface->vwidth_nonthread, 1.0f/source_blurred_surface->vheight_nonthread, 0, 0);
	fillDofConstants(&constants[1], dof_values, color_correction_values);

	ppscreen.material.constants = constants;

	textures[0] = rdrSurfaceToTexHandle(source_blurred_surface, SBUF_0);
	textures[1] = depth_tex;
	textures[2] = ldr_tex;
	textures[3] = hdr_tex;
#ifdef DOF_GRIME
	copyVec3(gfx_state.currentCameraFocus, constants[7]);
	textures[4] = texDemandLoadFixed(texFind("test_grime", 1)); // grime texture
	ppscreen.shadow_buffer_render = 1; // TODO: rename this to "bind_cubemap_lookup" perhaps?
#endif

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;

	ppscreen.shader_handle = gfxDemandLoadSpecialShader(dof_shader);

	ppscreen.tex_width = source_blurred_surface->width_nonthread;
	ppscreen.tex_height = source_blurred_surface->height_nonthread;

	ppscreen.write_depth = 0;

    if( texcoords )
    {
        ppscreen.use_normal_vertex_shader = true;
        ppscreen.use_texcoords = true;
        memcpy( ppscreen.texcoords, texcoords, sizeof( ppscreen.texcoords ));
    }

	gfxPostprocessScreen(&ppscreen);
}

void gfxDoDepthOfField(GfxStages stage, const DOFValues *dof_values, const SkyTimeColorCorrection *color_correction_values, GfxSpecialShader dof_shader, const Vec4 *texcoords)
{
	DOFValues dof_values_temp;
	GfxRenderAction *action = gfx_state.currentAction;
	bool bHighQualityDOF = !!gfxGetStageTemp(stage, 2);

	gfxBeginSection("PP: gfxDoDepthOfField");
	PERFINFO_AUTO_START_FUNC();

	if (gfx_lighting_options.enableDOFCameraFade && stage == GFX_DEPTH_OF_FIELD)
	{
		Vec3 pyr;
		gfxGetActiveCameraYPR(pyr);
		dof_values_temp = *dof_values;
		dof_values_temp.nearValue = lerp(dof_values->nearValue, 0, CLAMP(-(pyr[0])/1, 0, 1));
		dof_values_temp.farValue = lerp(dof_values->farValue, 0, CLAMP(-(pyr[0])/1, 0, 1));
		dof_values_temp.skyValue = lerp(dof_values->skyValue, 0, CLAMP(-(pyr[0])/1, 0, 1));
		dof_values = &dof_values_temp;
	}


	if (dof_shader == GSS_DEPTHOFFIELD) // Also need this for water?
	{
		if (dof_values->borderRamp)
		{
			if (dof_values->depthAdjustNearDist)
				dof_shader = GSS_DEPTHOFFIELD_EDGES_DEPTHADJUST;
			else
				dof_shader = GSS_DEPTHOFFIELD_EDGES;
		} else {
			if (dof_values->depthAdjustNearDist)
				dof_shader = GSS_DEPTHOFFIELD_DEPTHADJUST;
		}
	}


	PERFINFO_AUTO_START("doShrink4",1);

	// downsample LDR
	gfxSetActiveSurface(gfxGetStageTemp(stage, 0));
	doShrink4Ex(gfxGetStageInputSurface(stage, 0), gfxGetStageInputTexHandle(stage, 0), SM_NORMAL, NULL, NULL, false);

	PERFINFO_AUTO_STOP_START("gfxDoBlur",1);

	// blur it
	gfxDoMeanBlurInPlace(gfxGetStageTemp(stage, 0), gfxGetStageTemp(stage, 1), -1);

	if (!action->is_offscreen && gfx_state.debug.postprocessing_debug)
	{
		rdrSurfaceSnapshot(gfxGetStageTemp(stage, 0), "DOF Blur", 2);
		gfxDebugThumbnailsAddSurface(gfxGetStageTemp(stage, 0), SBUF_0, 2, "DOF Blur", 0);
	}

	PERFINFO_AUTO_STOP_START("gfxDoDofBlurCombine",1);

	// variable blur into LDR data
	gfxSetStageActiveSurface(stage);
	if (bHighQualityDOF)
	{
		gfxDoBlur(GBT_DOF, gfxGetStageOutput(stage), gfxGetStageTemp(stage, 2), 
				  gfxGetStageInputSurface(stage, 0), gfxGetStageInputBuffer(stage, 0), 
				  gfxGetStageInputTexHandle(stage, 2), gfxGetStageTempTexHandle(stage, 0), 
				  -9, 0, dof_values, color_correction_values); // kernel size 9
	}
	else
	{
		gfxDoDofBlurCombine(dof_values, color_correction_values, dof_shader, gfxGetStageTemp(stage, 0),
							gfxGetStageInputTexHandle(stage, 2), // depth
							gfxGetStageInputTexHandle(stage, 0), // LDR color
							gfxGetStageInputTexHandle(stage, 1), // HDR color
							texcoords);
	}

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_FUNC();
	gfxEndSection();
}

void gfxDoHDRPassOpaque(RdrDrawList *draw_list)
{
	GfxCameraView *camera_view = gfx_state.currentCameraView;
	Vec4 exposure_transform;

	gfxBeginSection(__FUNCTION__);
	PERFINFO_AUTO_START_FUNC();

	gfxSetStageActiveSurface(GFX_SEPARATE_HDR_PASS);

	// temporarily override LDR exposure transform to HDR transform, 
	// but not inverse transform so refraction and the LDR to HDR conversion still work
	copyVec4(camera_view->exposure_transform, exposure_transform);
	camera_view->exposure_transform[0] = camera_view->exposure_transform[2];
	gfxSetExposureTransform();

	gfxClearActiveSurfaceHDR(gfx_state.currentCameraView->clear_color, 1, true);

	if (0)
	{
		// This does the same thing, but in two passes, which fixes part of the
		//  artifacts on GF7 cards - specifically, where there are depth artifacts
		//  the color will be the opaque color, instead of the clear color (black).
		//  But, the depth artifacts are still there, and that's the most noticeable
		//  part (alpha objects showing through depth holes), so this is disabled
		//  for now, hoping on fix from NVIDIA.  Also, not tested on Xbox, but
		//  single-pass is probably faster

		RdrSurface *source_surface = gfxGetStageInputSurface(GFX_SEPARATE_HDR_PASS, 0);
		RdrScreenPostProcess ppscreen = {0};
		TexHandle textures[2];

		ppscreen.material.tex_count = 1;
		ppscreen.material.textures = textures;
		ppscreen.tex_width = source_surface->width_nonthread;
		ppscreen.tex_height = source_surface->height_nonthread;

		// Copy color
		ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_TONEMAP_LDR_TO_HDR_COLORONLY);
		ppscreen.blend_type = RPPBLEND_REPLACE;
		ppscreen.write_depth = 0;
		textures[0] = gfxGetStageInputTexHandle(GFX_SEPARATE_HDR_PASS, 0);
		gfxPostprocessScreen(&ppscreen);

		if (!gfx_state.debug.disableLDRToHDRDepthCopy) {
			// Copy depth
			// TODO: verify this isn't actually outputting color?  Does
			//  postprocessing override the surface masks?
			gfxSetActiveSurfaceEx(gfxGetActiveSurface(), MASK_SBUF_DEPTH, 0);
			ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_TONEMAP_LDR_TO_HDR);
			ppscreen.blend_type = RPPBLEND_REPLACE;
			ppscreen.write_depth = 1;
			ppscreen.material.tex_count = 2;
			textures[1] = gfxGetStageInputTexHandle(GFX_SEPARATE_HDR_PASS, 1);
			gfxPostprocessScreen(&ppscreen);
			gfxSetActiveSurfaceEx(gfxGetActiveSurface(), MASK_SBUF_ALL, 0);
		}
	} 
	else
	if (!gfx_state.debug.disableLDRToHDRColorCopy)
	{
		gfxPostprocessTwoTex(	gfxGetStageInputSurface(GFX_SEPARATE_HDR_PASS, 0), gfxGetStageInputTexHandle(GFX_SEPARATE_HDR_PASS, 0), 
								gfxGetStageInputTexHandle(GFX_SEPARATE_HDR_PASS, 1),
								GSS_TONEMAP_LDR_TO_HDR, NULL, 0, !gfx_state.debug.disableLDRToHDRDepthCopy, 
								// DJR: The depth test mode "less" is required to remove the sky background from the LDR-to-HDR copy,
								// because the input to this stage contains the sky, despite being in a snapshot labeled 
								// SCREENCOLOR_NOSKY_SNAPSHOT_IDX.
								RPPDEPTHTEST_LESS,
								RPPBLEND_REPLACE, false, true);
	}
	/* to inspect the HDR buffer and source LDR
	if (gfxUFIsCurrentActionBloomDebug()) {
		gfxDebugThumbnailsAddSurfaceCopy(gfxGetActiveSurface(), SBUF_DEPTH, 1, "HDR depth after copy", 0);
		gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentAction->bufferLDR->surface, SBUF_DEPTH, 0, "LDR msaa depth", 0);
		gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentAction->bufferLDR->surface, SBUF_DEPTH, 1, "LDR source depth", 0);
	}
	*/

	if (!gfx_state.debug.disableSkyBloomPass)
		gfxSkyDraw(camera_view->sky_data, true);

	if (!gfx_state.debug.disableOpaqueBloomPass) {
		gfxBeginSection("HDR opaque");
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_OPAQUE_ONEPASS, true, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_DEFERRED, true, false, false);
		gfxEndSection();
	}

	// restore exposure transform
	copyVec4(exposure_transform, camera_view->exposure_transform);
	gfxSetExposureTransform();

	PERFINFO_AUTO_STOP();
	gfxEndSection();
}

static void swapSnapshots(bool push)
{
	// Must disable auto-resolve, because if buffers are switched and we auto-resolve, bad things happen!
	assert(gfx_state.currentAction->snapshotLDR.surface == gfx_state.currentAction->opaqueDepth.surface); // Otherwise need to different autoresolve lines
	if (push)
		rdrSurfacePushAutoResolveMask(gfx_state.currentAction->snapshotLDR.surface, MASK_SBUF_ALL_COLOR|MASK_SBUF_DEPTH);
	else
		rdrSurfacePopAutoResolveMask(gfx_state.currentAction->snapshotLDR.surface);
	rdrSurfaceSwapSnapshots(gfx_state.currentAction->snapshotLDR.surface, gfxGetActiveSurface(), SBUF_0, SBUF_0, SCREENCOLOR_SNAPSHOT_IDX, 1);
	rdrSurfaceSwapSnapshots(gfx_state.currentAction->opaqueDepth.surface, gfxGetActiveSurface(),
		gfx_state.currentAction->opaqueDepth.buffer, SBUF_DEPTH,
		gfx_state.currentAction->opaqueDepth.snapshot_idx, 0);
}

static const int RDR_SURFACE_AUTO_RESOLVE_MSAA_SNAPSHOT = 0;
static const int RDR_DRAWLIST_NEED_SCREENCOLOR_SNAPSHOT = 2;

void gfxDoHDRPassNonDeferred(RdrDrawList *draw_list)
{
	GfxCameraView *camera_view = gfx_state.currentCameraView;
	Vec4 exposure_transform;

	gfxBeginSection(__FUNCTION__);
	PERFINFO_AUTO_START_FUNC();

	gfxSetStageActiveSurface(GFX_SEPARATE_HDR_PASS);  // Xbox perf TODO: This causes a resolve of the primary buffer, but we do not actually need to resolve depth here, it's unneeded

#if _XBOX
	// On the XBox we need to restore the color and depth buffers.
	gfxBeginSection("Restore surface");
	rdrSurfaceRestoreAfterSetActive(gfx_state.currentSurface, MASK_SBUF_ALL);
	gfxEndSection();
#endif

	// temporarily override LDR exposure transform to HDR transform, but not inverse transform so refraction still works
	copyVec4(camera_view->exposure_transform, exposure_transform);
	camera_view->exposure_transform[0] = camera_view->exposure_transform[2];
	gfxSetExposureTransform();

	if (!gfx_state.debug.disableNonDeferredBloomPass)
	{
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_NONDEFERRED, true, false, false);
		if (rdr_state.alphaInDOF)
		{
			if (rdrDrawListNeedsScreenGrab(draw_list))
			{
				// DX11TODO: We do not think snapshotting of depth is needed here (at least under DX9)
				rdrSurfaceSnapshotEx(gfxGetActiveSurface(), "gfxDoHDRPassNonDeferred", RDR_SURFACE_AUTO_RESOLVE_MSAA_SNAPSHOT, MASK_SBUF_DEPTH, 0);
				rdrSurfaceSnapshotEx(gfxGetActiveSurface(), "gfxDoHDRPassNonDeferred", RDR_DRAWLIST_NEED_SCREENCOLOR_SNAPSHOT, MASK_SBUF_ALL_COLOR, 0);
				swapSnapshots(true);
			}
			rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA_PREDOF_NEEDGRAB, true, false, false);
			rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA_PREDOF, true, false, false);
		} else {
			rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA_PREDOF_NEEDGRAB, true, false, false);
			rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA_PREDOF, true, false, false);
			if (rdrDrawListNeedsScreenGrab(draw_list))
			{
				// DX11TODO: We do not think snapshotting of depth is needed here (at least under DX9)
				rdrSurfaceSnapshotEx(gfxGetActiveSurface(), "gfxDoHDRPassNonDeferred", RDR_SURFACE_AUTO_RESOLVE_MSAA_SNAPSHOT, MASK_SBUF_DEPTH, 0);
				rdrSurfaceSnapshotEx(gfxGetActiveSurface(), "gfxDoHDRPassNonDeferred", RDR_DRAWLIST_NEED_SCREENCOLOR_SNAPSHOT, MASK_SBUF_ALL_COLOR, 0);
				swapSnapshots(true);
			}
		}
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA, true, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA_LATE, true, false, false);
		if (rdrDrawListNeedsScreenGrab(draw_list))
		{
			// restore pointers in thread
			swapSnapshots(false);
		}
		if (rdrDrawListNeedsScreenGrabLate(draw_list))
		{
			rdrSurfaceSnapshotEx(gfxGetActiveSurface(), "gfxDoHDRPassNonDeferred", RDR_SURFACE_AUTO_RESOLVE_MSAA_SNAPSHOT, MASK_SBUF_DEPTH, 0);
			rdrSurfaceSnapshotEx(gfxGetActiveSurface(), "gfxDoHDRPassNonDeferred", RDR_DRAWLIST_NEED_SCREENCOLOR_SNAPSHOT, MASK_SBUF_ALL_COLOR, 0);
			swapSnapshots(true);
		}
		// MJF TODO -- do I need to draw RST_ALPHA_LOW_RES here?
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA_POST_GRAB_LATE, true, false, false);
		if (rdrDrawListNeedsScreenGrabLate(draw_list))
		{
			// restore pointers in thread
			swapSnapshots(false);
		}
	}

	// restore exposure transform
	copyVec4(exposure_transform, camera_view->exposure_transform);
	gfxSetExposureTransform();

	PERFINFO_AUTO_STOP();
	gfxEndSection();
}

static void gfxDoBloom(const BlendedSkyInfo *sky_info)
{
	GfxRenderAction *action = gfx_state.currentAction;
	Vec4 constants[1];

	gfxBeginSection("PP: gfxDoBloom");
	PERFINFO_AUTO_START_FUNC();

	// shrink by 4 -> bloom_med[0]
	gfxSetActiveSurface(action->bloom_med[0]->surface);
	doShrink4Ex(gfxGetStageInputSurface(GFX_BLOOM, 0), gfxGetStageInputTexHandle(GFX_BLOOM, 0), SM_NORMAL, NULL, NULL, false);
	gfxDoMeanBlurInPlace(action->bloom_med[0]->surface, action->bloom_med[1]->surface, -3);
	if (gfxUFIsCurrentActionBloomDebug())
		gfxDebugThumbnailsAddSurfaceCopy(action->bloom_med[0]->surface, SBUF_0, 0, "Pre-blur Med", 0);

	// shrink by 4 -> bloom_low[0]
	gfxSetActiveSurface(action->bloom_low[0]->surface);
	doShrink4(action->bloom_med[0]->surface, SBUF_0, RDRSURFACE_SET_INDEX_DEFAULT, SM_NORMAL, NULL);
	if (gfxUFIsCurrentActionBloomDebug())
		gfxDebugThumbnailsAddSurfaceCopy(action->bloom_low[0]->surface, SBUF_0, 0, "Pre-blur Low", 0);

	// blur bloom_low[0]
	gfxDoMeanBlurInPlace(action->bloom_low[0]->surface, action->bloom_low[1]->surface, -3);

	// upsample and add -> bloom_med[1]
	gfxSetActiveSurface(action->bloom_med[1]->surface);
	setVec4(constants[0], sky_info->bloomValues.bloomBlurAmount[1], sky_info->bloomValues.bloomBlurAmount[2], 0, 0);
	gfxPostprocessTwoTex(action->bloom_med[0]->surface, rdrSurfaceToTexHandle(action->bloom_med[0]->surface, SBUF_0), rdrSurfaceToTexHandle(action->bloom_low[0]->surface, SBUF_0), GSS_ADDTEX, constants, 1, false, RPPDEPTHTEST_OFF, RPPBLEND_REPLACE, false, false);

	// blur bloom_med[1]
	gfxDoMeanBlurInPlace(action->bloom_med[1]->surface, action->bloom_med[0]->surface, -3);

	// upsample and add -> bloom[0]
	gfxSetActiveSurface(action->bloom[0]->surface);
	setVec4(constants[0], sky_info->bloomValues.bloomBlurAmount[0], 1, 0, 0);
	gfxPostprocessTwoTex(gfxGetStageInputSurface(GFX_BLOOM, 0), gfxGetStageInputTexHandle(GFX_BLOOM, 0), rdrSurfaceToTexHandle(action->bloom_med[1]->surface, SBUF_0), GSS_ADDTEX, constants, 1, false, RPPDEPTHTEST_OFF, RPPBLEND_REPLACE, false, false);

	// blur bloom[0]
	assert(gfxGetStageOutput(GFX_BLOOM) == action->bloom[0]->surface);
	gfxDoMeanBlurInPlace(action->bloom[0]->surface, action->bloom[1]->surface, -3);

	if (gfxUFIsCurrentActionBloomDebug())
		gfxDebugThumbnailsAddSurface(action->bloom[0]->surface, SBUF_0, 0, "Bloom", 0);

	PERFINFO_AUTO_STOP();
	gfxEndSection();
}

void gfxDoRecoverAlphaForHeadshot( RdrSurface* surface, TexHandle blackBGTexture, TexHandle grayBGTexture, Color bgColor )
{
	Vec4 bgColorVec;
	colorToVec4(bgColorVec, bgColor);
	
	gfxPostprocessTwoTex(surface, blackBGTexture, grayBGTexture,
						 GSS_RECOVER_ALPHA_FOR_HEADSHOT, &bgColorVec, 1, false,
						 RPPDEPTHTEST_OFF, RPPBLEND_REPLACE, true, false);
}

#if !DISABLE_GLARE
static Vec3 aberration_colors[] = {
	{ 0.5f, 0.5f, 0.5f }, //white
	{ 0.8f, 0.3f, 0.3f },
	{ 1.0f, 0.2f, 0.2f }, //red
	{ 0.5f, 0.2f, 0.6f },
	{ 0.2f, 0.2f, 1.0f }, //blue
	{ 0.2f, 0.3f, 0.7f },
	{ 0.2f, 0.6f, 0.2f }, //green
	{ 0.3f, 0.5f, 0.3f }
};

static void gfxDoGlare(const SkyInfo *sky_info, const Frustum *frustum)
{
	int i, j;
	F32 uoffset, voffset, angle, radperline;
	RdrScreenPostProcess ppscreen = {0};
	Vec4 constants[12];
	TexHandle textures[1];
	const Glare *glare = &sky_info->bloomValues[0]->lens->glare;
	int glare_lines = round(glare->lines);
	int glare_passes = round(glare->passes);
	RdrSurface *source_surface = action->bloom[1]->surface;

	gfxBeginSection("PP: gfxDoGlare");
	PERFINFO_AUTO_START_FUNC();

	gfxSetActiveSurface(source_surface);
	gfxPostprocessOneTex(action->bloom[0], SBUF_0, GSS_BRIGHTPASS, NULL, 0, RPPBLEND_REPLACE);

	if (!action->is_offscreen && gfx_state.debug.postprocessing_debug)
		gfxDebugThumbnailsAddSurface(action->bloom[1]->surface, SBUF_0, 0, "Post Bright Pass", 0);

	gfxSetActiveSurface(action->glare);

	uoffset = 1.f/source_surface->vwidth_nonthread;
	voffset = 1.f/source_surface->vheight_nonthread;

	ppscreen.material.const_count = ARRAY_SIZE(constants);
	ppscreen.material.constants = constants;

	textures[0] = rdrSurfaceToTexHandle(source_surface, SBUF_0); // bright pass color

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_GLARE);

	ppscreen.tex_width = source_surface->width_nonthread;
	ppscreen.tex_height = source_surface->height_nonthread;

	radperline = RAD(360.f/glare_lines);
	angle = RAD(glare->angle);

	if (glare->rotate_with_camera)
	{
		Vec3 cam_angle;
		getMat3YPR(frustum->inv_viewmat, cam_angle);
		angle += glare->rotate_with_camera * cam_angle[1];
	}

	for (i = 0; i < glare_lines; ++i)
	{
		int pass;
		F32 sn, cs, ustep, vstep, attenuation_power;
		sincosf(angle, &sn, &cs);
		ustep = sn * uoffset * glare->initial_line_step;
		vstep = cs * voffset * glare->initial_line_step;

		attenuation_power = (atanf(RAD(frustum->fovy * 0.5f)) + 0.1f) * 1.2f * (160.0f + 120.0f) / (vw + vh); // these numbers are magic!

		// each pass draws a larger line centered at the same spot
		for (pass = 1; pass < glare_passes+1; ++pass)
		{
			F32 pass_white_scale = (glare_passes - pass) / (F32)glare_passes;
			for (j = 0; j < 8; ++j)
			{
				Vec2 texoffset;
				float lum = powf(glare->power, attenuation_power * j) * 0.5f * pass;

				if (j & 1)
					setVec2(texoffset, j * ustep, j * vstep);
				else
					setVec2(texoffset, j * ustep, j * vstep);

				lerpVec3(unitvec3, pass_white_scale, aberration_colors[j], constants[j+4]);
				lerpVec3(constants[j+4], glare->chromatic_aberration, unitvec3, constants[j+4]);
				scaleVec3(constants[j+4], lum, constants[j+4]);

				if (fabs(texoffset[0]) >= 0.9f || fabs(texoffset[1]) >= 0.9f )
				{
					setVec2(texoffset, 0, 0);
					setVec3(constants[j+4], 0, 0, 0);
				}

				if (j & 1)
					copyVec2(texoffset, &(constants[j>>1][2]));
				else
					copyVec2(texoffset, constants[j>>1]);
			}

			if (i == 0 && pass == 1)
				ppscreen.blend_type = RPPBLEND_REPLACE;
			else
				ppscreen.blend_type = BLEND_ADD;

			gfxPostprocessScreen(&ppscreen);

			ustep *= glare->line_step_multiplier;
			vstep *= glare->line_step_multiplier;
			attenuation_power *= 8;
		}

		angle += radperline;
	}

	if (!action->is_offscreen && gfx_state.debug.postprocessing_debug)
		gfxDebugThumbnailsAddSurface(action->glare, SBUF_0, 0, "Glare", 0);

	PERFINFO_AUTO_STOP();
	gfxEndSection();
}
#endif

__forceinline static F32 setupLevels(const SkyColorLevels *levels, Vec4 output)
{
	F32 gamma = CLAMP(levels->gamma, 0.1f, 9.99f);
	F32 input_diff = levels->input_range[1] - levels->input_range[0];

	MAX1(input_diff, 0.00000001f);
	setVec4(output, levels->input_range[0], 1.f / input_diff, levels->output_range[0], levels->output_range[1] - levels->output_range[0]);

	return 1.f / gamma;
}

static F32 evalCurve(F32 intensity, const Vec4 curve[4])
{
	Vec3 weights, vals;

	// curve[0] -> x0, x1, x2, 0.5 * (x1 + x2)
	// curve[1] -> 1/(mid-x0), 1/(x3-mid), 1/(x1-x0), 1/(x2-x1)
	// curve[2] -> 1/(x3-x2), weight_pow, m1, b1
	// curve[3] -> m2, b2, m3, b3

	if (intensity < curve[0][3])
		weights[1] = (intensity - curve[0][0]) * curve[1][0];
	else
		weights[1] = 1 - (intensity - curve[0][3]) * curve[1][1];

	if (intensity < curve[0][1])
	{
		weights[0] = 1 - (intensity - curve[0][0]) * curve[1][2];
		weights[2] = -2;
	}
	else if (intensity < curve[0][2])
	{
		weights[0] = -(intensity - curve[0][1]) * curve[1][3];
		weights[2] = -1 - weights[0];
	}
	else
	{
		weights[0] = -2;
		weights[2] = (intensity - curve[0][2]) * curve[2][0];
	}

	weights[0] = saturate(weights[0] * 0.5f + 0.5f);
	weights[1] = saturate(weights[1]);
	weights[2] = saturate(weights[2] * 0.5f + 0.5f);

	weights[0] = powf(weights[0], curve[2][1]);
	weights[1] = powf(weights[1], curve[2][1]);
	weights[2] = powf(weights[2], curve[2][1]);

	if (intensity < curve[0][1])
		weights[0] += 1 - (weights[0] + weights[1] + weights[2]);
	else if (intensity < curve[0][2])
		weights[1] += 1 - (weights[0] + weights[1] + weights[2]);
	else
		weights[2] += 1 - (weights[0] + weights[1] + weights[2]);

	vals[0] = intensity * curve[2][2] + curve[2][3];
	vals[1] = intensity * curve[3][0] + curve[3][1];
	vals[2] = intensity * curve[3][2] + curve[3][3];

	return dotVec3(weights, vals);
}

static void debugCurve(const Vec4 curve[4])
{
	F32 step = 1 / 255.f;
	F32 last_val;
	int i;

	for (i = 0; i < 256; ++i)
	{
		F32 val = evalCurve(i * step, curve);
		if (i > 0)
			gfxDrawLine((i-1) * 2 + 100, -last_val * 512 + 512, 1, i * 2 + 100, -val * 512 + 512, ColorWhite);
		last_val = val;
	}

	gfxDrawLine(curve[0][1] * 512 + 100, 0, 2, curve[0][1] * 512 + 100, 1024, ColorRed);
	gfxDrawLine(curve[0][3] * 512 + 100, 0, 2, curve[0][3] * 512 + 100, 1024, ColorBlue);
	gfxDrawLine(curve[0][2] * 512 + 100, 0, 2, curve[0][2] * 512 + 100, 1024, ColorRed);
}

static void setupCurve(const SkyColorCurve *curve, Vec4 outputs[4])
{
	int i, count = ARRAY_SIZE(curve->control_points);
	F32 curve_mid;
	Vec3 m, b;

	STATIC_INFUNC_ASSERT(ARRAY_SIZE(curve->control_points) == 4);

	for (i = 0; i < count; ++i)
		outputs[0][i] = curve->control_points[i][0];

	curve_mid = 0.5f * (outputs[0][1] + outputs[0][2]);

	for (i = 0; i < count-1; ++i)
	{
		m[i] = (curve->control_points[i+1][1] - curve->control_points[i][1]) / (outputs[0][i+1] - outputs[0][i]);
		b[i] = curve->control_points[i][1] - m[i] * outputs[0][i];
	}

	setVec4(outputs[1], 1.f / (curve_mid - outputs[0][0]), 1.f / (outputs[0][3] - curve_mid), 1.f / (outputs[0][1] - outputs[0][0]), 1.f / (outputs[0][2] - outputs[0][1]));
	setVec4(outputs[2], 1.f / (outputs[0][3] - outputs[0][2]), 1.95f, m[0], b[0]);
	setVec4(outputs[3], m[1], b[1], m[2], b[2]);
	outputs[0][3] = curve_mid;
}

__forceinline static void setupHSVCurve(const SkyColorCurveHSV *curve, Vec4 red_outputs[4], Vec4 green_outputs[4], Vec4 blue_outputs[4])
{
	SkyColorCurve rgb_curves[3];
	int i, j;

	STATIC_INFUNC_ASSERT(ARRAY_SIZE(curve->control_points) == ARRAY_SIZE(rgb_curves[0].control_points));

	for (j = 0; j < ARRAY_SIZE(curve->control_points); ++j)
	{
		Vec3 rgb_temp;
		gfxHsvToRgb(curve->hsv_vals[j], rgb_temp);
		for (i = 0; i < 3; ++i)
			setVec2(rgb_curves[i].control_points[j], curve->control_points[j], rgb_temp[i]);
	}

	setupCurve(&rgb_curves[0], red_outputs);
	setupCurve(&rgb_curves[1], green_outputs);
	setupCurve(&rgb_curves[2], blue_outputs);
}

static void gfxCalcToneCurveLUT(const BlendedSkyInfo *sky_info)
{
	GfxRenderAction *action = gfx_state.currentAction;
	RdrScreenPostProcess ppscreen = {0};
	Vec4 constants[6];
	SkyColorLevels default_levels_intensity;
	SkyColorCurve default_curve_intensity;
	const SkyColorLevels *intensity_levels = &sky_info->colorCorrectionValues.levels_intensity;
	const SkyColorCurve *intensity_curve = &sky_info->colorCorrectionValues.curve_intensity;

	gfxBeginSection("PP: gfxCalcToneCurveLUT");
	gfxSetActiveSurface(action->tonecurve_lut->surface);

	if (gfx_state.debug.disableColorCorrection)
	{
		setVec2(default_levels_intensity.input_range, 0, 1);
		setVec2(default_levels_intensity.output_range, 0, 1);
		default_levels_intensity.gamma = 1;

		intensity_levels = &default_levels_intensity;

		setVec2same(default_curve_intensity.control_points[0], 0);
		setVec2same(default_curve_intensity.control_points[1], 0.33f);
		setVec2same(default_curve_intensity.control_points[2], 0.66f);
		setVec2same(default_curve_intensity.control_points[3], 1);

		intensity_curve = &default_curve_intensity;
	}

	// brightness & contrast adjustments
	constants[1][0] = setupLevels(intensity_levels, constants[0]);
	constants[1][1] = constants[1][2] = constants[1][3] = 0;

	setupCurve(intensity_curve, constants + 2);

	ppscreen.material.const_count = ARRAY_SIZE(constants);
	ppscreen.material.constants = constants;

	ppscreen.shader_handle = gfxDemandLoadSpecialShader(rdr_state.disableToneCurve10PercentBoost ? GSS_CALC_TONE_CURVE_NO_BOOST : GSS_CALC_TONE_CURVE);

	ppscreen.tex_width = action->tonecurve_lut->surface->width_nonthread;
	ppscreen.tex_height = action->tonecurve_lut->surface->height_nonthread;

	gfxPostprocessScreen(&ppscreen);

	if (gfx_state.debug.postprocessing_lut_debug)
		gfxDebugThumbnailsAddSurface(action->tonecurve_lut->surface, SBUF_0, 0, "Tone Curve LUT", false);

	gfxEndSection();
}

static void setupCurveWithScale(const SkyColorCurve *curve, Vec4 outputs[4], F32 scale)
{
	SkyColorCurve temp_curve = {0};
	StructCopyAll(parse_SkyColorCurve, curve, &temp_curve); 
	temp_curve.control_points[0][1] *= scale;
	temp_curve.control_points[1][1] *= scale;
	temp_curve.control_points[2][1] *= scale;
	temp_curve.control_points[3][1] *= scale;
	setupCurve(&temp_curve, outputs);
}

static void gfxCalcColorCurveLUT(const BlendedSkyInfo *sky_info)
{
	GfxRenderAction *action = gfx_state.currentAction;
	RdrScreenPostProcess ppscreen = {0};
	Vec4 constants[16];
	Vec3 color_curve_multi;

	gfxBeginSection("PP: gfxCalcColorCurveLUT");
	gfxSetActiveSurface(action->colorcurve_lut->surface);

	// color adjustments
	constants[3][0] = setupLevels(&sky_info->colorCorrectionValues.levels_red, constants[0]);
	constants[3][1] = setupLevels(&sky_info->colorCorrectionValues.levels_green, constants[1]);
	constants[3][2] = setupLevels(&sky_info->colorCorrectionValues.levels_blue, constants[2]);
	constants[3][3] = 0;

	copyVec3(sky_info->colorCorrectionValues.color_curve_multi, color_curve_multi);
	if(vec3IsZero(color_curve_multi)) {
		setVec3same(color_curve_multi, 1.0);
	}
	setupCurveWithScale(&sky_info->colorCorrectionValues.curve_red,		constants + 4,	color_curve_multi[0]);
	setupCurveWithScale(&sky_info->colorCorrectionValues.curve_green,	constants + 8,	color_curve_multi[1]);
	setupCurveWithScale(&sky_info->colorCorrectionValues.curve_blue,	constants + 12,	color_curve_multi[2]);

	ppscreen.material.const_count = ARRAY_SIZE(constants);
	ppscreen.material.constants = constants;

	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_CALC_COLOR_CURVE);

	ppscreen.tex_width = action->colorcurve_lut->surface->width_nonthread;
	ppscreen.tex_height = action->colorcurve_lut->surface->height_nonthread;

	gfxPostprocessScreen(&ppscreen);

	if (gfx_state.debug.postprocessing_lut_debug)
		gfxDebugThumbnailsAddSurface(action->colorcurve_lut->surface, SBUF_0, 0, "Color Curve LUT", false);

	gfxEndSection();
}

static void gfxCalcIntensityTintLUT(const BlendedSkyInfo *sky_info)
{
	GfxRenderAction *action = gfx_state.currentAction;
	RdrScreenPostProcess ppscreen = {0};
	Vec4 constants[16];

	gfxBeginSection("PP: gfxCalcIntensityTintLUT");
	gfxSetActiveSurface(action->intensitytint_lut->surface);

	// saturation
	setupCurve(&sky_info->colorCorrectionValues.saturation_curve, constants);
	setupHSVCurve(&sky_info->colorCorrectionValues.tint_curve, constants + 4, constants + 8, constants + 12);

	ppscreen.material.const_count = ARRAY_SIZE(constants);
	ppscreen.material.constants = constants;

	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_CALC_INTENSITY_TINT);

	ppscreen.tex_width = action->intensitytint_lut->surface->width_nonthread;
	ppscreen.tex_height = action->intensitytint_lut->surface->height_nonthread;

	gfxPostprocessScreen(&ppscreen);

	if (gfx_state.debug.postprocessing_lut_debug)
	{
		gfxDebugThumbnailsAddSurface(action->intensitytint_lut->surface, SBUF_0, 0, "Intensity Tint LUT", false);
		gfxDebugThumbnailsAddSurface(action->intensitytint_lut->surface, SBUF_0, 0, "Saturation LUT", 2);
	}

	gfxEndSection();
}

static void gfxCalcBlueshiftLUT(const BlendedSkyInfo *sky_info)
{
	GfxRenderAction *action = gfx_state.currentAction;
	RdrScreenPostProcess ppscreen = {0};
	Vec4 constants[2];
	F32 m, b;
	F32 blueshiftMin = sky_info->lightBehaviorValues.blueshiftMin;
	F32 blueshiftMax = sky_info->lightBehaviorValues.blueshiftMax;

	gfxBeginSection("PP: gfxCalcBlueshiftLUT");
	gfxSetActiveSurface(action->blueshift_lut->surface);

	if (nearSameF32(blueshiftMin, blueshiftMax))
		blueshiftMax = blueshiftMin + 0.001f;

	m = 1 / (blueshiftMax - blueshiftMin);
	b = 1 - m * blueshiftMax;

	setVec4(constants[0], m, b, 0, 0);

	gfxHsvToRgb(sky_info->lightBehaviorValues.blueshiftHSV, constants[1]);
	constants[1][3] = 0.0f;

	ppscreen.material.const_count = ARRAY_SIZE(constants);
	ppscreen.material.constants = constants;

	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_CALC_BLUESHIFT);

	ppscreen.tex_width = action->blueshift_lut->surface->width_nonthread;
	ppscreen.tex_height = action->blueshift_lut->surface->height_nonthread;

	gfxPostprocessScreen(&ppscreen);

	if (gfx_state.debug.postprocessing_lut_debug)
		gfxDebugThumbnailsAddSurface(action->blueshift_lut->surface, SBUF_0, 0, "Blueshift LUT", false);

	gfxEndSection();
}

__forceinline static bool SkyIsTinted(const BlendedSkyInfo *sky_info)
{
	return !nearSameF32( distance3Squared( sky_info->tintValues.screenTintHSV, unitvec3 ) +
		lengthVec3Squared( sky_info->tintValues.screenTintOffsetHSV ), 0.0f);
}

static void gfxDoTonemappingAndTint(const BlendedSkyInfo *sky_info) {

	GfxRenderAction *action = gfx_state.currentAction;
	Vec4 constants[10];
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[4];
	bool bTint = false;
	GfxSpecialShader shader = GSS_FINAL_POSTPROCESS;
	float bloomIntensity = gfx_state.settings.bloomIntensity;

	gfxBeginSection("PP: gfxDoFinalLUT");

	gfxSetActiveSurface(action->postprocess_all_lut->surface);

	memset(constants, 0, ARRAY_SIZE(constants) * sizeof(Vec4));

	if(gfx_state.settings.bloomQuality == GBLOOM_LOW_BLOOMWHITE) {
		bloomIntensity *= gfxSkyGetBloomScale(sky_info, false);
	}

	setVec2(constants[0], bloomIntensity, sky_info->bloomValues.glareAmount);

#if DISABLE_GLARE
	constants[0][1] = 0;
#endif

	ppscreen.material.const_count = ARRAY_SIZE(constants);
	ppscreen.material.constants = constants;

	textures[0] = rdrSurfaceToTexHandle(action->blueshift_lut->surface, SBUF_0);
	textures[1] = rdrSurfaceToTexHandle(action->tonecurve_lut->surface, SBUF_0);

	textures[2] = textures[3] = 0;

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;

	if (SkyIsTinted(sky_info))
	{
		shader = GSS_FINAL_POSTPROCESS_TINT;
		copyVec3(sky_info->tintValues.screenTintHSV, constants[1]);
		copyVec3(sky_info->tintValues.screenTintOffsetHSV, constants[2]);
	}

	if (!gfx_state.debug.disableColorCorrection)
	{
		int i;

		if (shader == GSS_FINAL_POSTPROCESS_TINT)
			shader = GSS_FINAL_POSTPROCESS_COLOR_CORRECT_TINT;
		else
			shader = GSS_FINAL_POSTPROCESS_COLOR_CORRECT;
		textures[2] = rdrSurfaceToTexHandle(action->colorcurve_lut->surface, SBUF_0);
		textures[3] = rdrSurfaceToTexHandle(action->intensitytint_lut->surface, SBUF_0);

		// Set up per-color HSV adjustment constants.
		for(i = 0; i < 6; i++) {
			constants[i+4][0] = sky_info->colorCorrectionValues.specificHue[i];
			constants[i+4][1] = sky_info->colorCorrectionValues.specificSaturation[i] + 1;
			constants[i+4][2] = sky_info->colorCorrectionValues.specificValue[i] + 1;
		}
		constants[3][0] = sky_info->colorCorrectionValues.specificOverlap;
	}

	ppscreen.shader_handle = gfxDemandLoadSpecialShader(shader);

	ppscreen.tex_width  = action->postprocess_all_lut->surface->width_nonthread;
	ppscreen.tex_height = action->postprocess_all_lut->surface->height_nonthread;

	gfxPostprocessScreen(&ppscreen);

	if (gfx_state.debug.postprocessing_lut_debug)
		gfxDebugThumbnailsAddSurface(action->postprocess_all_lut->surface, SBUF_0, 0, "Final LUT", false);

	gfxEndSection();

}

static void gfxDoFinalLUT(const BlendedSkyInfo *sky_info)
{
	GfxRenderAction *action = gfx_state.currentAction;
	Vec4 constants[1];
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[4];
	GfxSpecialShader shader = GSS_FINAL_POSTPROCESS_LUT_STAGE;

	gfxBeginSection("PP: gfxDoTonemappingAndTint");

	gfxSetStageActiveSurface(GFX_TONEMAP);

	// Bloom constants
	memset(constants, 0, ARRAY_SIZE(constants) * sizeof(Vec4));
	setVec2(constants[0], gfx_state.settings.bloomIntensity, sky_info->bloomValues.glareAmount);
#if DISABLE_GLARE
	constants[0][1] = 0;
#endif

	// Inputs for exposed color and our generated lookup table.
	textures[0] = gfxGetStageInputTexHandle(GFX_TONEMAP, 0); // exposed color
	textures[1] = rdrSurfaceToTexHandle(action->postprocess_all_lut->surface, SBUF_0);

	// Bloom texture
	if (gfxGetStageInputSurface(GFX_TONEMAP, 1))
		textures[2] = gfxGetStageInputTexHandle(GFX_TONEMAP, 1);
	else
		textures[2] = 0;

	// Glare
#if !DISABLE_GLARE
	textures[3] = rdrSurfaceToTexHandle(action->glare, SBUF_0);
#else
	textures[3] = 0;
#endif

	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;

	ppscreen.shader_handle = gfxDemandLoadSpecialShader(shader);

	ppscreen.tex_width = gfxGetStageInputSurface(GFX_TONEMAP, 0)->width_nonthread;
	ppscreen.tex_height = gfxGetStageInputSurface(GFX_TONEMAP, 0)->height_nonthread;

	gfxPostprocessScreen(&ppscreen);

	gfxEndSection();
}

void gfxDoHDR(const BlendedSkyInfo *sky_info, const Frustum *frustum)
{
	gfxBeginSection("PP: gfxDoHDR");
	PERFINFO_AUTO_START_FUNC();

	assert(sky_info);

	gfxCalcBlueshiftLUT(sky_info);
	gfxCalcToneCurveLUT(sky_info);
	gfxCalcColorCurveLUT(sky_info);
	gfxCalcIntensityTintLUT(sky_info);

	// do HDR stuff
	if (gfx_state.settings.bloomQuality == GBLOOM_OFF)
	{
		gfxDoSoftwareLightAdaptation(sky_info);
	}
	else
	{
		gfxDoLightAdaptation(sky_info);
		gfxDoBloom(sky_info);
#if !DISABLE_GLARE
		gfxDoGlare(sky_info, frustum);
#endif
	}

	gfxDoTonemappingAndTint(sky_info);

	gfxDoFinalLUT(sky_info);

	PERFINFO_AUTO_STOP();
	gfxEndSection();
}

/// Debug water ripple scale
static F32 s_debugRippleScale = 0;
AUTO_CMD_FLOAT(s_debugRippleScale,waterRippleScale);

/// The pooled string, "TextureScreen" 
static const char* s_textureScreen;

/// The pooled string, "TextureScreenHDR"
static const char* s_textureScreenHdr;

/// The pooled string "TextureScreenDepth"
static const char* s_textureScreenDepth;

/// The pooled string, "Texture"
static const char* s_texture;

/// The pooled string "RefractDistort"
static const char* s_refractDistort;

/// The pooled string "WaterDepthFadeEnd"
static const char* s_waterDepthFadeEnd;

AUTO_RUN;
void initStringPool( void )
{
    s_textureScreen = allocAddStaticString( "TextureScreen" );
    s_textureScreenHdr = allocAddStaticString( "TextureScreenHDR" );
    s_textureScreenDepth = allocAddStaticString( "TextureScreenDepth" );
    s_texture = allocAddStaticString( "Texture" );
    s_refractDistort = allocAddStaticString( "RefractDistort" );
    s_waterDepthFadeEnd = allocAddStaticString( "WaterDepthFadeEnd" );
}

float gfxInWaterCompletelyDistance(const Frustum *frustum)
{
	if (!gfx_state.currentCameraView->in_water_this_frame)
	{
		return 0;
	}
	else
	{
		float zNear = frustum->znear;
		const Vec3 trPosCamera = {  zNear * frustum->htan, -zNear * frustum->vtan, zNear };
		const Vec3 volumeNormal = { 0, 1, 0 };
		const F32* volumePos = gfx_state.currentCameraView->water_plane_pos;
	
		Vec3 trPosWorld;
		Vec3 cornerTemp;
		float topDot;
	
		mulVecMat4( trPosCamera, frustum->inv_viewmat, trPosWorld );

		subVec3( trPosWorld, volumePos, cornerTemp );
		topDot = -dotVec3( cornerTemp, volumeNormal );

		if( topDot < 0 ) {
			return 0;
		} else {
			return topDot;
		}
	}
}

void gfxWaterCalculateTexcoords(const Frustum *frustum, Vec4 texcoords[4])
{
	float zNear = frustum->znear; 
	const Vec3 blPosCamera = { -zNear * frustum->htan,  zNear * frustum->vtan, zNear };
	const Vec3 brPosCamera = {  zNear * frustum->htan,  zNear * frustum->vtan, zNear };
	const Vec3 tlPosCamera = { -zNear * frustum->htan, -zNear * frustum->vtan, zNear };
	const Vec3 trPosCamera = {  zNear * frustum->htan, -zNear * frustum->vtan, zNear };
	const Vec3 volumeNormal = { 0, 1, 0 };
	const F32* volumePos = gfx_state.currentCameraView->water_plane_pos;

	Vec3 blPosWorld;
	Vec3 brPosWorld;
	Vec3 tlPosWorld;
	Vec3 trPosWorld;
	Vec3 cornerTemp;
	float topDot;
	float bottomDot;

	mulVecMat4( blPosCamera, frustum->inv_viewmat, blPosWorld );
	mulVecMat4( brPosCamera, frustum->inv_viewmat, brPosWorld );
	mulVecMat4( tlPosCamera, frustum->inv_viewmat, tlPosWorld );
	mulVecMat4( trPosCamera, frustum->inv_viewmat, trPosWorld );

	subVec3( brPosWorld, volumePos, cornerTemp );
	bottomDot = -dotVec3( cornerTemp, volumeNormal );

	subVec3( trPosWorld, volumePos, cornerTemp );
	topDot = -dotVec3( cornerTemp, volumeNormal );

	// assume the water volumes are always along the y-axis
	setVec4( texcoords[0], 1, 1, bottomDot, -topDot / (bottomDot - topDot));
	setVec4( texcoords[1], 0, 1, bottomDot, -topDot / (bottomDot - topDot));
	setVec4( texcoords[2], 0, 0, topDot, -topDot / (bottomDot - topDot));
	setVec4( texcoords[3], 1, 0, topDot, -topDot / (bottomDot - topDot));

	// this is the generic code, without the y-axis assumption 
	if( false )
	{
		subVec3( brPosWorld, volumePos, cornerTemp );
		setVec4( texcoords[0], -dotVec3( cornerTemp, volumeNormal ), 0.5, 0, 0 );

		subVec3( blPosWorld, volumePos, cornerTemp );
		setVec4( texcoords[1], -dotVec3( cornerTemp, volumeNormal ), 0.5, 0, 0 );

		subVec3( tlPosWorld, volumePos, cornerTemp );
		setVec4( texcoords[2], -dotVec3( cornerTemp, volumeNormal ), 0.5, 0, 0 );

		subVec3( trPosWorld, volumePos, cornerTemp );
		setVec4( texcoords[3], -dotVec3( cornerTemp, volumeNormal ), 0.5, 0, 0 );
	}
}

void gfxDoWaterVolumeShader(GfxStages stage, const Frustum *frustum)
{
    RdrScreenPostProcess ppscreen = { 0 };
    MaterialRenderInfo* renderInfo;
    TexHandle* textures;
    F32* constants;
    const WorldVolumeWater* waterData = gfx_state.currentCameraView->in_water_this_frame;
    const F32 rippleScale = (s_debugRippleScale ? s_debugRippleScale : gfx_state.water_ripple_scale);

    gfxBeginSection("PP: gfxDoWaterVolumeShader");

	gfxWaterCalculateTexcoords(frustum, ppscreen.texcoords);
	ppscreen.use_texcoords = true;

	// grab screen
	gfxSetStageActiveSurface(stage);
	rdrSurfaceSnapshot(gfxGetActiveSurface(), "gfxDoWaterVolumeShader", SCREENCOLOR_SNAPSHOT_IDX);

    {
        Material* material = materialFind( waterData->materialName, WL_FOR_WORLD );
        Vec2 oldValues;

        if( material->graphic_props.render_info == NULL ) {
            gfxMaterialsInitMaterial( material, true );
        }

        assert( material->graphic_props.render_info != NULL );
        renderInfo = material->graphic_props.render_info;

		if (renderInfo->constant_mapping_count) // Will not be on a) bad materials or b) fallback material
		{
			renderInfo->constant_mapping[ 0 ].scroll.isIncremental = true;
			copyVec2( renderInfo->constant_mapping[ 0 ].scroll.values, oldValues );
			scaleVec2( renderInfo->constant_mapping[ 0 ].scroll.values,
					   lerp( waterData->rippleMin, waterData->rippleMax, rippleScale ),
					   renderInfo->constant_mapping[ 0 ].scroll.values );
		}

        gfxDemandLoadMaterialAtDrawTime(renderInfo, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		if (renderInfo->constant_mapping_count) // Will not be on a) bad materials or b) fallback material
			copyVec2( oldValues, renderInfo->constant_mapping[ 0 ].scroll.values );

		ppscreen.shader_handle = gfxMaterialFillShader( renderInfo->graph_render_info, getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA,0), 0 );
        ppscreen.material = renderInfo->rdr_material;
    }
    ppscreen.use_normal_vertex_shader = true;

    textures = alloca( sizeof( TexHandle ) * ppscreen.material.tex_count );
    memcpy( textures, ppscreen.material.textures,
            sizeof( TexHandle ) * ppscreen.material.tex_count );
    {
        int it;
        for( it = 0; it != ppscreen.material.tex_count; ++it ) {
            if(   s_textureScreen == renderInfo->texture_names[ it * 2 + 0 ]
                  && s_texture == renderInfo->texture_names[ it * 2 + 1 ]) {
                textures[ it ] = gfxGetStageInputTexHandle(stage, 0);
            } else if( s_textureScreenDepth == renderInfo->texture_names[ it * 2 + 0 ]
                       && s_texture == renderInfo->texture_names[ it * 2 + 1 ]) {
				textures[ it ] = gfxGetStageInputTexHandle(stage, 1);
            }
        }
    }

    constants = alloca( sizeof( F32 ) * 4 * ppscreen.material.const_count );
    memcpy( constants, ppscreen.material.constants,
            sizeof( Vec4 ) * ppscreen.material.const_count );
    {
        int it;
        for( it = 0; it != ppscreen.material.const_count*4; ++it ) {
            if( s_refractDistort == renderInfo->constant_names[ it ]) {
                scaleVec2( &constants[ it ],
                           lerp( waterData->refractMin, waterData->refractMax, rippleScale ),
                           &constants[ it ]);
            } else if( s_waterDepthFadeEnd == renderInfo->constant_names[ it ]) {
                constants[ it ] /= fabs( gfx_state.currentCameraView->frustum.zfar );
            }
        }
    }

    {
        TexHandle* oldTextures = ppscreen.material.textures;
        Vec4* oldConstants = ppscreen.material.constants;
      
        ppscreen.material.textures = textures;
        ppscreen.material.constants = (Vec4*)constants;

        gfxSetStageActiveSurface(stage);
        gfxPostprocessScreen(&ppscreen);

        ppscreen.material.constants = oldConstants;
        ppscreen.material.textures = oldTextures;
    }

    gfxEndSection();
}

// If gfxDoWaterVolumeShaderLowEnd() returs true, then outQuadDrawable
// has been filled out and should be rendered after the alpha objects.
bool gfxMaybeDoWaterVolumeShaderLowEnd(int screenWidth, int screenHeight, RdrQuadDrawable *outQuad)
{
	bool quadIsDrawn = false;
	bool waterFogEnabled = false;
			
	if (gfx_state.currentCameraView->in_water_this_frame && !gfxFeatureEnabled(GFEATURE_WATER))
	{
		const WorldVolumeWaterLowEnd* lowEnd = &gfx_state.currentCameraView->in_water_this_frame->lowEnd;
		bool isCompletelySubmerged = gfx_state.currentCameraView->in_water_completely_distance > 0;
		if (isCompletelySubmerged)
		{
			float minDist = (0 - lowEnd->minPercent * lowEnd->maxDist) / (1 - lowEnd->minPercent);

			// gfxSkySetCustomFog() doesn't do anything if there's
			// already a custom fog, so I have to disable/re-enable it
			// when any of the values may have changed.
			if (wlVolumeWaterReloadedThisFrame()) {
				gfxSkyUnsetCustomFog(gfx_state.currentCameraView->sky_data, false, 0.0);
			}
			
			gfxSkySetCustomFog(gfx_state.currentCameraView->sky_data, lowEnd->waterColorHSV,
							   minDist, lowEnd->maxDist, lowEnd->maxPercent,
							   false, 0.0);
			waterFogEnabled = true;
		}

		if (!isCompletelySubmerged || gfx_state.currentCameraView->in_water_completely_fade_time > 0
			|| gfx_state.currentCameraView->in_water_completely_distance < GFX_WATER_MAX_SHALLOW_DIST)
		{
			Material* material = materialFind( lowEnd->materialNearSurface, WL_FOR_WORLD );
			int quadWidth;
			int quadHeight;
			Vec4 texcoords[4];
			Vec2 quadTexcoord;

			gfxWaterCalculateTexcoords(&gfx_state.currentCameraView->frustum, texcoords);
			quadWidth = screenWidth;
			quadHeight = screenHeight * (1 - texcoords[0][1]);
			quadTexcoord[1] = 1 - texcoords[0][1];
			quadTexcoord[0] = (float)screenWidth / screenHeight;

			if( material->graphic_props.render_info == NULL ) {
				gfxMaterialsInitMaterial( material, true );
			}
			assert( material->graphic_props.render_info );
			
			setVec2( outQuad->vertices[0].point, 0, 0 );
			setVec4( outQuad->vertices[0].texcoords, 0, 0, 0, 0 );
					
			setVec2( outQuad->vertices[1].point, quadWidth, 0 );
			setVec4( outQuad->vertices[1].texcoords, quadTexcoord[0], 0, 0, 0 );
						
			setVec2( outQuad->vertices[3].point, quadWidth, quadHeight );
			setVec4( outQuad->vertices[3].texcoords, quadTexcoord[0], -quadTexcoord[1], 0, 0 );

			setVec2( outQuad->vertices[2].point, 0, quadHeight );
			setVec4( outQuad->vertices[2].texcoords, 0, -quadTexcoord[1], 0, 0 );

			gfxHsvToRgb( lowEnd->waterColorHSV, outQuad->vertices[0].color );
			if (!isCompletelySubmerged)
				outQuad->vertices[0].color[3] = lowEnd->maxPercent;
			else
			{
				float minFade = ((GFX_WATER_MAX_SHALLOW_DIST - gfx_state.currentCameraView->in_water_completely_distance)
								 / GFX_WATER_MAX_SHALLOW_DIST);
				float fadeTimeFade = gfx_state.currentCameraView->in_water_completely_fade_time / GFX_WATER_ADAPTATION_TIME;
				outQuad->vertices[0].color[3] = lowEnd->maxPercent * MAX(minFade, fadeTimeFade);
			}
			copyVec4( outQuad->vertices[0].color, outQuad->vertices[1].color );
			copyVec4( outQuad->vertices[0].color, outQuad->vertices[2].color );
			copyVec4( outQuad->vertices[0].color, outQuad->vertices[3].color );
					
			{
				MaterialRenderInfo* renderInfo = material->graphic_props.render_info;
				gfxDemandLoadMaterialAtDrawTime(renderInfo, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);
				outQuad->material = renderInfo->rdr_material;
				outQuad->shader_handle = gfxMaterialFillShader( renderInfo->graph_render_info, getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA,0), 0 );
				outQuad->alpha_blend = true;
			}
					
			quadIsDrawn = true;
			outQuad->use_normal_vertex_shader = 1;
		}
	}
		
	if (!waterFogEnabled)
	{
		gfxSkyUnsetCustomFog(gfx_state.currentCameraView->sky_data, false, 0.0);
	}

	return quadIsDrawn;
}

void gfxClearActiveSurfaceHDR(const Vec4 clear_color, F32 clear_depth, bool bShouldClearDepth)
{
	GfxCameraView *camera_view = gfx_state.currentCameraView;
	RdrDevice *rdr_device = gfx_state.currentDevice->rdr_device;
	Vec4 clear_color_adapted;

	// LDR
	scaleVec3(clear_color, camera_view->exposure_transform[0], clear_color_adapted);
	clear_color_adapted[3] = clear_color[3];

	if (bShouldClearDepth)
		rdrClearActiveSurface(rdr_device, MASK_SBUF_0|MASK_SBUF_DEPTH|CLEAR_STENCIL, clear_color_adapted, clear_depth);
	else
		rdrClearActiveSurface(rdr_device, MASK_SBUF_0, clear_color_adapted, clear_depth);

	if (gfx_state.currentSurface->params_nonthread.flags & (SF_MRT2|SF_MRT4))
	{
		// HDR
		scaleVec3(clear_color, camera_view->exposure_transform[2], clear_color_adapted);
		rdrClearActiveSurface(rdr_device, MASK_SBUF_1, clear_color_adapted, clear_depth);
	}
}

void gfxClearActiveSurfaceEx(const Vec4 clear_color0, const Vec4 clear_color1, const Vec4 clear_color2, const Vec4 clear_color3, F32 clear_depth, U32 clear_flags)
{
	RdrDevice *rdr_device = gfx_state.currentDevice->rdr_device;
	Vec4 constants[5];
	RdrScreenPostProcess ppscreen = {0};

	copyVec4(clear_color0, constants[0]);
	copyVec4(clear_color1, constants[1]);
	copyVec4(clear_color2, constants[2]);
	copyVec4(clear_color3, constants[3]);
	setVec4same(constants[4], clear_depth);

	ppscreen.material.constants = constants;
	ppscreen.material.const_count = ARRAY_SIZE(constants);

	ppscreen.material.tex_count = 0;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_FILL);

	ppscreen.write_depth = clear_flags & MASK_SBUF_DEPTH ? 1 : 0;

	// Note: This will be *much* faster if we just call the Clear functions directly!  Do we actually use this at runtime?
	PERFINFO_AUTO_PIX_START("Performance problem!  Change gfxClearActiveSurface() to be better!");
	gfxPostprocessScreen(&ppscreen);
	PERFINFO_AUTO_PIX_STOP();
}

//////////////////////////////////////////////////////////////////////////


#define SURF_SIZE 1024

static GfxTempSurface *getTempBrightnessBuffer(void)
{
	RdrSurfaceParams surfaceparams = {0};
	surfaceparams.desired_multisample_level = 1;
	surfaceparams.required_multisample_level = 1;
	surfaceparams.flags = 0;
	rdrSurfaceParamSetSizeSafe(&surfaceparams, SURF_SIZE, SURF_SIZE);
	surfaceparams.depth_bits = 24;
	surfaceparams.buffer_types[0] = SBT_RGBA_FLOAT32;
	surfaceparams.name = "Temp Material Brightness Surface";
	rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
	return gfxGetTempSurface(&surfaceparams);
}

static void setupPPMaterial(RdrScreenPostProcess *ppscreen, Material *material, RdrMaterialShader materialShader)
{
	MaterialRenderInfo *render_info;

	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial(material, true);

	assert(material->graphic_props.render_info);

	render_info = material->graphic_props.render_info;
	gfxDemandLoadMaterialAtDrawTime(render_info, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);
	ppscreen->shader_handle = gfxMaterialFillShader(render_info->graph_render_info, materialShader, 0);
	ppscreen->material = render_info->rdr_material;
}

static void drawMaterial(Material *material, RdrMaterialShader materialShader)
{
	RdrScreenPostProcess ppscreen = {0};
	const MaterialData *material_data = materialGetData(material);

	setupPPMaterial(&ppscreen, material, materialShader);

	copyVec3(unitvec3, ppscreen.ambient);

	ppscreen.use_texcoords = 1;
	setVec4(ppscreen.texcoords[0], 2, 0, 0, 0);
	setVec4(ppscreen.texcoords[1], 0, 0, 0, 0);
	setVec4(ppscreen.texcoords[2], 0, 2, 0, 0);
	setVec4(ppscreen.texcoords[3], 2, 2, 0, 0);

	ppscreen.use_normal_vertex_shader = 1;

	gfxPostprocessScreen(&ppscreen);
}

static void measureBrightness(Material *material, RdrMaterialShader materialShader, Vec3 brightness)
{
	GfxCameraView *camera_view = gfx_state.currentCameraView;
	RdrDevice *rdr_device = gfx_state.currentDevice->rdr_device;
	Vec3 *data;
	int i;

	drawMaterial(material, materialShader);

	data = (Vec3*)rdrGetActiveSurfaceData(rdr_device, SURFDATA_RGB_F32, SURF_SIZE, SURF_SIZE);

	zeroVec3(brightness);
	for (i = 0; i < SURF_SIZE*SURF_SIZE; ++i)
	{
		MAX1(brightness[0], data[i][0]);
		MAX1(brightness[1], data[i][1]);
		MAX1(brightness[2], data[i][2]);
	}

	free(data);
}

static void ensureShaderIsLoaded(Material *material, RdrMaterialShader materialShader)
{
	const MaterialData *material_data = materialGetData(material);
	materialSetUsage(material, WL_FOR_WORLD);
	gfxMaterialsFillSpecificRenderValues(material_data->graphic_props.shader_template, material);
	assert(material->graphic_props.render_info);
	gfxDemandLoadMaterialAtDrawTime(material->graphic_props.render_info, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);
	gfxMaterialFillShader(material->graphic_props.render_info->graph_render_info, materialShader, 0);
	texOncePerFramePerDevice();
	gfxDemandLoadMaterials(true);
}

void gfxMeasureMaterialBrightness(Material *material, Vec3 brightness_values)
{
	GfxCameraView *camera_view = gfx_state.currentCameraView;
	RdrDevice *rdr_device = gfx_state.currentDevice->rdr_device;
	int old_bsc = rdr_state.backgroundShaderCompile;
	int old_dsc = rdr_state.disableShaderCache;
	Vec4 old_exposure_transform;
	const MaterialData *material_data = materialGetData(material);
	GfxTempSurface *temp_buffer;
	RdrMaterialShader materialPreviewShader = getRdrMaterialShader(0, rdrGetMaterialShaderType(RDRLIGHT_DIRECTIONAL, 0));

	copyVec4(camera_view->exposure_transform, old_exposure_transform);
	temp_buffer = getTempBrightnessBuffer();
	gfxSetActiveSurface(temp_buffer->surface);

	rdrClearActiveSurface(gfx_state.currentDevice->rdr_device, CLEAR_ALL, zerovec4, 1);

	rdr_state.backgroundShaderCompile = 0;
	rdr_state.disableShaderCache = 1;

	rdrShaderSetTestDefine(0, "MEASURE_BRIGHTNESS");
	gfxMaterialTemplateUnloadShaders(material_data->graphic_props.shader_template);
	ensureShaderIsLoaded(material, materialPreviewShader);
	material_data = materialGetData(material); // this pointer goes bad after calling ensureShaderIsLoaded
	measureBrightness(material, materialPreviewShader, brightness_values);

	gfxDebugThumbnailsAddSurfaceCopy(temp_buffer->surface, SBUF_0, 0, "Material Brightness", false);

	// restore settings
	rdr_state.disableShaderCache = old_dsc;
	rdrShaderSetTestDefine(0, NULL);
	gfxMaterialTemplateUnloadShaders(material_data->graphic_props.shader_template);
	rdr_state.backgroundShaderCompile = old_bsc;
	copyVec4(old_exposure_transform, camera_view->exposure_transform);
	gfxSetExposureTransform();
	gfxReleaseTempSurface(temp_buffer);
}

AUTO_COMMAND;
void gfxTestMaterialBrightness(void)
{
	bool needs_device_lock = !gfx_state.currentDevice->rdr_device->is_locked_nonthread;
	Vec3 brightness_values2 = {-1,-1,-1};
	int retry_count;
	Material *material = materialFind("white", WL_FOR_WORLD|WL_FOR_UTIL);
	assert(material);
	if (needs_device_lock)
		gfxLockActiveDevice();
	// Retry multiple times to help track down why this is happening on CBs
	// If this fails multiple times, hopefully we'll have something we can debug
	for (retry_count=0; retry_count < 4; retry_count++)
	{
		gfxMeasureMaterialBrightness(material, brightness_values2);
		if (brightness_values2[0] == 0 &&
				brightness_values2[1] == 1 && 
				brightness_values2[2] == 0)
			break;
	}
	if (needs_device_lock)
		gfxUnlockActiveDevice();
	assert(brightness_values2[0] == 0);
	assert(brightness_values2[1] == 1);
	assert(brightness_values2[2] == 0);
}
