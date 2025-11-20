#ifndef GFXPOSTPROCESS_H
#define GFXPOSTPROCESS_H
#pragma once
GCC_SYSTEM

#include "RdrSurface.h"
#include "GfxEnums.h"
#include "RdrEnums.h"

typedef struct RdrDevice RdrDevice;
typedef struct RdrSurface RdrSurface;
typedef struct RdrLight RdrLight;
typedef int ShaderHandle;
typedef struct BlendedSkyInfo BlendedSkyInfo;
typedef struct RdrLightDefinition RdrLightDefinition;
typedef struct Frustum Frustum;
typedef struct DOFValues DOFValues;
typedef struct SkyTimeColorCorrection SkyTimeColorCorrection;
typedef struct RdrDrawList RdrDrawList;
typedef struct RdrScreenPostProcess RdrScreenPostProcess;
typedef struct Material Material;
typedef struct RdrQuadDrawable RdrQuadDrawable;
typedef U64 TexHandle;

typedef enum GfxSpecialShader {
	GSS_DEFERRED_UNLIT,
	GSS_DEFERRED_UNLIT_AND_LIGHT,
	GSS_DEFERRED_LIGHT,

	// Shaders above this are deprecated
	GSS_BeginShadowBufferShadersToPreload,

	// These are all preloaded/used similarly
	GSS_DEFERRED_SSAO_PREPASS,
	GSS_DEFERRED_SHADOW,
	GSS_DEFERRED_SHADOW_STEREOSCOPIC,
	GSS_DEFERRED_SHADOW_POISSON,
	GSS_DEFERRED_SHADOW_POISSON_STEREOSCOPIC,
	GSS_DEFERRED_SHADOW_SSAO,
	GSS_DEFERRED_SHADOW_SSAO_STEREOSCOPIC,
	GSS_DEFERRED_SHADOW_SSAO_POISSON,
	GSS_DEFERRED_SHADOW_SSAO_POISSON_STEREOSCOPIC,
	GSS_CALCULATE_SCATTERING,
	GSS_CALCULATE_SCATTERING_STEREOSCOPIC,

	// All things before this take parameters and can't be trivially preloaded
	GSS_BeginSimpleShaders,
	// All things after this are "simple" and can be trivially preloaded

	GSS_OUTLINING_NO_NORMALS,
	GSS_OUTLINING_NO_NORMALS_DEPTH,
	GSS_OUTLINING_WITH_NORMALS,
	GSS_OUTLINING_WITH_NORMALS_DEPTH,
	GSS_OUTLINING_NO_NORMALS_USE_Z,
	GSS_OUTLINING_APPLY_WITH_NORMALS,
	GSS_DEPTHOFFIELD,
    GSS_DEPTHOFFIELD_SCALE,
	GSS_DEPTHOFFIELD_EDGES,
	GSS_DEPTHOFFIELD_DEPTHADJUST,
	GSS_DEPTHOFFIELD_EDGES_DEPTHADJUST,
	GSS_SHRINK4,
	GSS_SHRINK4_MAX,
	GSS_SHRINK4_MAX_LUMINANCE,
	GSS_SHRINK4_LOG,
	GSS_SHRINK4_EXP,
	GSS_SHRINK4_HIGHPASS,
	GSS_SHRINK4_BLOOM_CURVE,
	GSS_SHRINK4_DEPTH,
	GSS_SHRINK4_2,
	GSS_SHRINK4_2_MAX,
	GSS_SHRINK4_2_LOG,
	GSS_SHRINK4_2_EXP,
	GSS_SHRINK4_2_HIGHPASS,
	GSS_SHRINK4_2_BLOOM_CURVE,
	GSS_SHRINK4_2_DEPTH,
	GSS_BLUR_3_H,
	GSS_BLUR_START = GSS_BLUR_3_H,
	GSS_BLUR_3_V,
	GSS_BLUR_5_H,
	GSS_BLUR_5_V,
	GSS_BLUR_7_H,
	GSS_BLUR_7_V,
	GSS_BLUR_9_H,
	GSS_BLUR_9_V,
	GSS_BLUR_11_H,
	GSS_BLUR_11_V,
	GSS_SMART_BLUR_3_H,
	GSS_SMART_BLUR_START = GSS_SMART_BLUR_3_H,
	GSS_SMART_BLUR_3_V,
	GSS_SMART_BLUR_5_H,
	GSS_SMART_BLUR_5_V,
	GSS_SMART_BLUR_7_H,
	GSS_SMART_BLUR_7_V,
	GSS_SMART_BLUR_9_H,
	GSS_SMART_BLUR_9_V,
	GSS_SMART_BLUR_11_H,
	GSS_SMART_BLUR_11_V,
	GSS_DOF_BLUR_3_H,
	GSS_DOF_BLUR_START = GSS_DOF_BLUR_3_H,
	GSS_DOF_BLUR_3_V,
	GSS_DOF_BLUR_5_H,
	GSS_DOF_BLUR_5_V,
	GSS_DOF_BLUR_7_H,
	GSS_DOF_BLUR_7_V,
	GSS_DOF_BLUR_9_H,
	GSS_DOF_BLUR_9_V,
	GSS_DOF_BLUR_11_H,
	GSS_DOF_BLUR_11_V,
	GSS_GLARE,
	GSS_CALC_TONE_CURVE,
	GSS_CALC_TONE_CURVE_NO_BOOST,
	GSS_CALC_COLOR_CURVE,
	GSS_CALC_INTENSITY_TINT,
	GSS_CALC_BLUESHIFT,
	GSS_FINAL_POSTPROCESS,
	GSS_FINAL_POSTPROCESS_COLOR_CORRECT,
	GSS_FINAL_POSTPROCESS_TINT,
	GSS_FINAL_POSTPROCESS_COLOR_CORRECT_TINT,
	GSS_FINAL_POSTPROCESS_LUT_STAGE,
	GSS_ALPHABLIT,
	GSS_ALPHABLIT_DISCARD_EDGE,
	GSS_PCFDEPTHBLIT,
	GSS_NOALPHABLIT,
	GSS_SHOWALPHA,
	GSS_SHOWALPHA_AS_COLOR,
	GSS_SHOWTEXTUREMIP,
	GSS_TONEMAP_LDR_TO_HDR,
	GSS_TONEMAP_LDR_TO_HDR_COLORONLY,
	GSS_BLOOMCURVE,
	GSS_ADDTEX,
	GSS_BRIGHTPASS,
	GSS_MEASURE_LUMINANCE,
	GSS_MEASURE_LUMINANCE_HDR2,
	GSS_MEASURE_HDR_POINT,
	GSS_FILL,
	GSS_FILL_HDR,
	GSS_FILL_HDR_DEPTH,
	GSS_FILL_HDR_DEPTHSTENCIL,
	GSS_FILL_CUBEMAP,
	GSS_DEFERRED_SCATTERING,
	GSS_TESTZOCCLUSION,
	GSS_RECOVER_ALPHA_FOR_HEADSHOT,
	GSS_LOW_RES_ALPHA_EDGE_DETECT,
	GSS_LOW_RES_ALPHA_EDGE_DETECT_ADDITIVE_BLEND,
	GSS_DEPTH_AS_COLOR,
	GSS_DEPTH_AS_COLOR_MSAA_2,
	GSS_DEPTH_AS_COLOR_MSAA_4,
	GSS_DEPTH_AS_COLOR_MSAA_8,
	GSS_DEPTH_AS_COLOR_MSAA_16,
	GSS_TARGET_HIGHLIGHT_ALPHATOWHITE,
	GSS_TARGET_HIGHLIGHT_SMOOTHING,
	GSS_TARGET_HIGHLIGHT_OUTLINE,
	GSS_UI_COLORBLIND_PRO,
	GSS_UI_COLORBLIND_DEU,
	GSS_UI_COLORBLIND_TRI,
	GSS_UI_DESATURATE,
	GSS_UI_TV,

	// Note: if adding a non-simple shader (takes parameters/calls DemandLoadSpecialShaderEx) it must go near the beginning!
	GSS_Max,
} GfxSpecialShader;

typedef enum GfxBlurType
{
	GBT_GAUSSIAN,
	GBT_BOX,
	GBT_SMART,
	GBT_DOF,

	GBT_Max
} GfxBlurType;

void gfxPostprocessScreen(RdrScreenPostProcess *ppscreen);
void gfxPostprocessScreenPart(RdrScreenPostProcess *ppscreen, Vec2 dest_top_left, Vec2 dest_bottom_right);

void gfxSetExposureTransform(void);

void gfxReloadSpecialShaders(void);
ShaderHandle gfxDemandLoadSpecialShaderEx(GfxSpecialShader shader, int constants_used, int textures_used, RdrMaterialShader shader_num, const RdrLightDefinition **light_def);
#define gfxDemandLoadSpecialShader(shader) gfxDemandLoadSpecialShaderEx(shader, -1, -1, getRdrMaterialShaderByKey(0), NULL)

// specific post processing functions
void gfxDoOutliningEarly(const BlendedSkyInfo *sky_info);
void gfxDoOutliningLate(const BlendedSkyInfo *sky_info);

void gfxDoLensZOSample(const BlendedSkyInfo *sky_info);

void gfxDoDepthOfField(GfxStages stage, const DOFValues *dof_values, const SkyTimeColorCorrection *color_correction_values, GfxSpecialShader dof_shader, const Vec4 *texcoords);
void gfxDoHDR(const BlendedSkyInfo *sky_info, const Frustum *frustum);
void gfxDoSoftwareLightAdaptation(const BlendedSkyInfo *sky_info);
void gfxCalcHDRTransform(const BlendedSkyInfo *sky_info);
void gfxDoHDRPassOpaque(RdrDrawList *draw_list);
void gfxDoHDRPassNonDeferred(RdrDrawList *draw_list);

void gfxDoCalculateScattering(const RdrLightDefinition **light_defs, const BlendedSkyInfo *sky_info);
void gfxDoScattering(const BlendedSkyInfo *sky_info);

// water postprocessing and other water utilities
void gfxWaterCalculateTexcoords(const Frustum *frustum, Vec4 texcoords[4]);
float gfxInWaterCompletelyDistance(const Frustum *frustum);
void gfxDoWaterVolumeShader(GfxStages stage, const Frustum *frustum);
bool gfxMaybeDoWaterVolumeShaderLowEnd(int screenWidth, int screenHeight, RdrQuadDrawable *outQuad);
#define GFX_WATER_ADAPTATION_TIME 3.0
#define GFX_WATER_MAX_SHALLOW_DIST 5.0

// generic post processing functions
void gfxDoBlur(GfxBlurType blur_type, RdrSurface *dest_surface, RdrSurface *temp_surface, 
			   RdrSurface *source_surface, RdrSurfaceBuffer source_buffer, TexHandle depth_tex_handle, TexHandle lc_blur_tex_handle, 
			   F32 kernel_size, F32 smart_blur_threshold, const DOFValues *dof_values, const SkyTimeColorCorrection *color_correction_values);
#define gfxDoGaussianBlurInPlace(dest_surface, temp_surface, kernel_size) gfxDoBlur(GBT_GAUSSIAN, (dest_surface), (temp_surface), (dest_surface), SBUF_0, 0, 0, (kernel_size), 0, NULL, 0)
#define gfxDoMeanBlurInPlace(dest_surface, temp_surface, kernel_size) gfxDoBlur(GBT_BOX, (dest_surface), (temp_surface), (dest_surface), SBUF_0, 0, 0, (kernel_size), 0, NULL, 0)
#define gfxDoSmartBlurInPlace(dest_surface, temp_surface, depth_tex_handle, kernel_size, smart_blur_threshold) gfxDoBlur(GBT_SMART, (dest_surface), (temp_surface), (dest_surface), SBUF_0, (depth_tex_handle), 0, (kernel_size), (smart_blur_threshold), NULL, 0)
void gfxScaleBuffer(RdrSurface *source_surface, bool viewport_independent_textures);
void gfxShrink4(RdrSurface *source_surface, RdrSurfaceBuffer source_buffer, int source_set_index, bool viewport_independent_textures);
void gfxShrink4Depth(RdrSurface *source_surface, RdrSurfaceBuffer source_buffer, int source_set_index);
void gfxCopyDepthIntoRGB(RdrSurface *source_surface, int snapshot_idx, float depth_min, float depth_max, float depth_power);
void gfxPostprocessOneTex(RdrSurface *source_surface, TexHandle source_tex, int shader_num, Vec4 *constants, int num_constants, RdrPPBlendType blend_type);
void gfxPostprocessTwoTex(RdrSurface *primary_source_surface, 
	TexHandle primary_source_tex, TexHandle secondary_source_tex, int shader_num,
	Vec4 *constants, int num_constants, bool write_depth, RdrPPDepthTestMode depth_test, 
	RdrPPBlendType blend_type, bool viewport_independent_textures, 
	bool offset_for_downsample);

void gfxMeasureMaterialBrightness(SA_PARAM_NN_VALID Material *material, Vec3 brightness_values);

void gfxClearActiveSurfaceHDR(const Vec4 clear_color, F32 clear_depth, bool bShouldClearDepth);
#define gfxClearActiveSurface(clear_color0, clear_color1, clear_color2, clear_color3, clear_depth)	\
	gfxClearActiveSurfaceEx((clear_color0), (clear_color1), (clear_color2), (clear_color3), (clear_depth), MASK_SBUF_ALL)
void gfxClearActiveSurfaceEx(const Vec4 clear_color0, const Vec4 clear_color1, const Vec4 clear_color2, const Vec4 clear_color3, F32 clear_depth, U32 clear_flags);
void gfxClearActiveSurfaceRestoreDepth(RdrSurface *dest_surface, const Vec4 clear_color);
void gfxDoRecoverAlphaForHeadshot( RdrSurface* surface, TexHandle blackBGTexture, TexHandle grayBGTexture, Color bgColor );

void gfxPostprocessDoPreloadNextFrame(void);
void gfxPostprocessCheckPreload(void);

#define SBUF_DEFERRED_UNLIT888_UNLITMULT8 SBUF_0
#define SBUF_DEFERRED_NORMAL1616 SBUF_1
#define SBUF_DEFERRED_ALBEDO888_SPEC8 SBUF_2
#define SBUF_DEFERRED_DEPTH16_GLOSS16 SBUF_3

#ifdef _XBOX
#define SCREENCOLOR_SNAPSHOT_IDX 0
#define SCREENCOLOR_NOSKY_SNAPSHOT_IDX 1
#else
#define SCREENCOLOR_SNAPSHOT_IDX 2
#define SCREENCOLOR_NOSKY_SNAPSHOT_IDX 3
#endif

#ifdef _XBOX
#define SBUF_INDEX_OUTLINE_DEPTH 1
#define SBUF_OUTLINE_DEPTH_MASK MASK_SBUF_DEPTH
#define SBUF_OUTLINE_DEPTH_CONTINUE_TILING 1
#else
#define SBUF_INDEX_OUTLINE_DEPTH 2
#define SBUF_OUTLINE_DEPTH_MASK MASK_SBUF_DEPTH
#define SBUF_OUTLINE_DEPTH_CONTINUE_TILING 1
#endif

#define SCREENCOLOR_TXAA_SNAPSHOT_IDX 4
#define SCREENCOLOR_TXAA_SNAPSHOT_LAST_FRAME_IDX 5

#endif
