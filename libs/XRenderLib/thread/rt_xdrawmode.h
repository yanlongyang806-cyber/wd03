#ifndef _RT_XDRAWMODE_H_
#define _RT_XDRAWMODE_H_

#include "../xdevice.h"
#include "rt_xshader.h"
#include "rt_xshaderdata.h"

typedef enum
{
	DRAWBIT_ZERO,

	DRAWBIT_SKINNED = 1<<0,
	DRAWBIT_BEND = 1<<1,
	DRAWBIT_MORPH = 1<<2,
	DRAWBIT_VERTEX_LIGHT = 1<<3,
	DRAWBIT_NOPIXELSHADER = 1<<4,
	DRAWBIT_DEPTHONLY = 1<<5,
	DRAWBIT_WORLD_TEX_COORDS = 1<<6,
	DRAWBIT_VERTEX_COLORS = 1<<7,
	DRAWBIT_SINGLE_DIRLIGHT = 1<<8,
	DRAWBIT_NO_NORMALMAP = 1<<9,
	DRAWBIT_WIND = 1<<10,
	DRAWBIT_INSTANCED = 1<<11,
	DRAWBIT_ALPHA_FADE_PLANE = 1<<12,
	// This space now empty, used to be -> DRAWBIT_FAR_DEPTH_RANGE = 1<<13,
	DRAWBIT_VERTEX_ONLY_LIGHTING = 1<<14,
#ifdef FORCEFARDEPTH_IN_VS
	// This space now empty -> DRAWBIT_FORCE_FAR_DEPTH = 1<<15,
#endif
	DRAWBIT_SCREEN_TEX_COORDS = 1<<16,
	DRAWBIT_VERTEX_ONLY_LIGHTING_ONLY_ONE_LIGHT = 1<<17,
	DRAWBIT_SKINNED_ONLY_TWO_BONES = 1<<18,
	DRAWBIT_VS_TEXCOORD_SPLAT = 1<<19,
	DRAWBIT_TRUNK_WIND = 1<<20,
	DRAWBIT_NO_NORMAL_NO_TEXCOORD = 1 << 21,
	DRAWBIT_TESSELLATION = 1 << 22,
	DRAWBIT_MAX = 1<<23,
} DrawModeBits;

STATIC_ASSERT(DRAWBIT_MAX <= (1<<PROGRAMDEF_DEFINES_NUM));

#define SHADER_PATH "shaders/D3D/"

// The minimal list of vertex shaders (preloaded in EXE memory)
enum
{
	VS_MINIMAL_ERROR,
	VS_MINIMAL_SPRITE,
	VS_MINIMAL_SPRITE_SRGB,
	VS_MINIMAL_PRIMITIVE,
	VS_MINIMAL_PRIMITIVE_SRGB,

	VS_MINIMAL_MAX,
};

// The normal vertex shaders
enum
{
	VS_SPECIAL_POSTPROCESS_SHAPE,
	VS_SPECIAL_POSTPROCESS_SHAPE_NO_OFFSET,
	VS_SPECIAL_POSTPROCESS_SCREEN,
	VS_SPECIAL_POSTPROCESS_SCREEN_NO_OFFSET,

	VS_SPECIAL_PARTICLE,
	VS_SPECIAL_PARTICLE_NO_NORMALMAP,
	VS_SPECIAL_CYLINDER_TRAIL,

	VS_SPECIAL_PARTICLE_FAR_DEPTH_RANGE,
	VS_SPECIAL_PARTICLE_NO_NORMALMAP_FAR_DEPTH_RANGE,
	VS_SPECIAL_CYLINDER_TRAIL_FAR_DEPTH_RANGE,

	VS_SPECIAL_FAST_PARTICLE_CPU,
	VS_SPECIAL_FAST_PARTICLE_CPU_FAR_DEPTH_RANGE,

	VS_SPECIAL_FAST_PARTICLE,
	VS_SPECIAL_FAST_PARTICLE_LINKSCALE,
	VS_SPECIAL_FAST_PARTICLE_STREAK,

	VS_SPECIAL_FAST_PARTICLE_FAR_DEPTH_RANGE,
	VS_SPECIAL_FAST_PARTICLE_LINKSCALE_FAR_DEPTH_RANGE,
	VS_SPECIAL_FAST_PARTICLE_STREAK_FAR_DEPTH_RANGE,

	VS_SPECIAL_FAST_PARTICLE_RGB_BLEND,
	VS_SPECIAL_FAST_PARTICLE_LINKSCALE_RGB_BLEND,
	VS_SPECIAL_FAST_PARTICLE_STREAK_RGB_BLEND,

	VS_SPECIAL_FAST_PARTICLE_FAR_DEPTH_RANGE_RGB_BLEND,
	VS_SPECIAL_FAST_PARTICLE_LINKSCALE_FAR_DEPTH_RANGE_RGB_BLEND,
	VS_SPECIAL_FAST_PARTICLE_STREAK_FAR_DEPTH_RANGE_RGB_BLEND,

	VS_SPECIAL_FAST_PARTICLE_ANIMATEDTEXTURE,
	VS_SPECIAL_FAST_PARTICLE_LINKSCALE_ANIMATEDTEXTURE,
	VS_SPECIAL_FAST_PARTICLE_STREAK_ANIMATEDTEXTURE,

	VS_SPECIAL_FAST_PARTICLE_FAR_DEPTH_RANGE_ANIMATEDTEXTURE,
	VS_SPECIAL_FAST_PARTICLE_LINKSCALE_FAR_DEPTH_RANGE_ANIMATEDTEXTURE,
	VS_SPECIAL_FAST_PARTICLE_STREAK_FAR_DEPTH_RANGE_ANIMATEDTEXTURE,

	VS_SPECIAL_FAST_PARTICLE_RGB_BLEND_ANIMATEDTEXTURE,
	VS_SPECIAL_FAST_PARTICLE_LINKSCALE_RGB_BLEND_ANIMATEDTEXTURE,
	VS_SPECIAL_FAST_PARTICLE_STREAK_RGB_BLEND_ANIMATEDTEXTURE,

	VS_SPECIAL_FAST_PARTICLE_FAR_DEPTH_RANGE_RGB_BLEND_ANIMATEDTEXTURE,
	VS_SPECIAL_FAST_PARTICLE_LINKSCALE_FAR_DEPTH_RANGE_RGB_BLEND_ANIMATEDTEXTURE,
	VS_SPECIAL_FAST_PARTICLE_STREAK_FAR_DEPTH_RANGE_RGB_BLEND_ANIMATEDTEXTURE,

	VS_SPECIAL_POSTPROCESS_NORMAL_PS,

	VS_SPECIAL_STARFIELD,
	VS_SPECIAL_STARFIELD_CAMERA_FACING,

	VS_SPECIAL_STARFIELD_VERTEXONLYLIGHTING,
	VS_SPECIAL_STARFIELD_VERTEXONLYLIGHTING_CAMERA_FACING,

	VS_SPECIAL_MAX,
};

enum
{
	VS_NORMAL,
	VS_TERRAIN,

	VS_STANDARD_MAX,
};

enum
{
	PS_ERROR,
	PS_LOADING,
	PS_LOADING_DEBUG,

	PS_DEFAULT,
	PS_DEFAULT_NULL,						// Despite not being referenced anywhere, this shader is used implicitly by a handle of -1

	PS_DEFAULT_PARTICLE,					// Regular particles, mesh trails, etc, matches with normal vertex shader
	PS_CYLINDER_TRAIL,

	PS_FAST_PARTICLE,						// Fast particles only, needs the particle vertex shader
	PS_FAST_PARTICLE_SOFT,					// For particles that fade with surface intersection-depth

	PS_FAST_PARTICLE_NO_TONEMAP,			// Fast particles only, needs the particle vertex shader
	PS_FAST_PARTICLE_SOFT_NO_TONEMAP,		// For particles that fade with surface intersection-depth

	PS_FAST_PARTICLE_DEPTH_TEX,				// Fast particles only, needs the particle vertex shader
	PS_FAST_PARTICLE_SOFT_DEPTH_TEX,		// For particles that fade with surface intersection-depth

	PS_FAST_PARTICLE_NO_TONEMAP_DEPTH_TEX,	// Fast particles only, needs the particle vertex shader
	PS_FAST_PARTICLE_SOFT_NO_TONEMAP_DEPTH_TEX, // For particles that fade with surface intersection-depth

	// 2D sprites (no fog, tonemapping, etc)
	// Note, don't insert things in the middle of this, it maps to the enum
	PS_SPRITE_DEFAULT,						// RdrSpriteEffect_None
	PS_SPRITE_TWOTEX,						// RdrSpriteEffect_TwoTex
	PS_SPRITE_EFFECT_DESATURATE,			// RdrSpriteEffect_Desaturate
	PS_SPRITE_EFFECT_DESATURATE_TWOTEX,		// RdrSpriteEffect_Desaturate_TwoTex
	PS_SPRITE_EFFECT_SMOOTH,				// RdrSpriteEffect_Smooth
	PS_SPRITE_DISTFIELD_1LAYER,				// RdrSpriteEffect_DistField1Layer
	PS_SPRITE_DISTFIELD_2LAYER,				// RdrSpriteEffect_DistField2Layer
	PS_SPRITE_DISTFIELD_1LAYER_GRAD,		// RdrSpriteEffect_DistField1LayerGradient
	PS_SPRITE_DISTFIELD_2LAYER_GRAD,		// RdrSpriteEffect_DistField2LayerGradient
	// Note, don't insert things in the middle of the above list, it maps to the enum

	PS_DEFAULT_SPRITE,						// For primitives

	PS_PERF_TEST,

	PS_WIREFRAME,

	PS_DEPTH_MSAA_2_RESOLVE,
	PS_DEPTH_MSAA_4_RESOLVE,
	PS_DEPTH_MSAA_8_RESOLVE,
	PS_DEPTH_MSAA_16_RESOLVE,

	PS_NOALPHA,

	PS_MAX = PS_NOALPHA+6,
};

static RxbxProgramDef pixelShaderDefs[] = 
{
	{ SHADER_PATH "error.phl", "error_pixelshader", { 0 } },
	{ SHADER_PATH "error.phl", "loading_pixelshader", { 0 } },
	{ SHADER_PATH "error.phl", "loading_debug_pixelshader", { 0 } },

	{ SHADER_PATH "default.phl", "default_pixelshader", { 0 } },
	{ SHADER_PATH "default.phl", "null_pixelshader", { 0 } },

	{ SHADER_PATH "default.phl", "default_particle_pixelshader", { 0 } },
	{ SHADER_PATH "default.phl", "cylinder_trail_pixelshader", { 0 } },

	{ SHADER_PATH "particles.phl", "particle_pixelshader", { 0 } },
	{ SHADER_PATH "particles.phl", "particle_pixelshader", { "SOFT_PARTICLE", 0 } },

	{ SHADER_PATH "particles.phl", "particle_pixelshader", { "NO_TONEMAPPING", 0 } },
	{ SHADER_PATH "particles.phl", "particle_pixelshader", { "SOFT_PARTICLE", "NO_TONEMAPPING", 0 } },

	{ SHADER_PATH "particles.phl", "particle_pixelshader", { "MANUAL_DEPTH_TEST", 0 } },
	{ SHADER_PATH "particles.phl", "particle_pixelshader", { "SOFT_PARTICLE", "MANUAL_DEPTH_TEST", 0 } },

	{ SHADER_PATH "particles.phl", "particle_pixelshader", { "NO_TONEMAPPING", "MANUAL_DEPTH_TEST", 0 } },
	{ SHADER_PATH "particles.phl", "particle_pixelshader", { "SOFT_PARTICLE", "NO_TONEMAPPING", "MANUAL_DEPTH_TEST", 0 } },

	{ SHADER_PATH "sprite_2d.phl", "sprite_2d_pixelshader", { 0 } }, // Actually loaded from rt_xshaderdata.c
	{ SHADER_PATH "sprite_2d.phl", "sprite_2d_pixelshader", { "TWOTEXSPRITE", 0 } },
	{ SHADER_PATH "sprite_2d.phl", "sprite_2d_pixelshader", { "DESATURATE", 0 } },
	{ SHADER_PATH "sprite_2d.phl", "sprite_2d_pixelshader", { "DESATURATE", "TWOTEXSPRITE", 0 } },
	{ SHADER_PATH "sprite_2d.phl", "sprite_2d_pixelshader", { "SMOOTH", 0 } },
	{ SHADER_PATH "dist_field_sprite_2d.phl", "dist_field_sprite_2d", { "ONE_LAYER", 0 } },
	{ SHADER_PATH "dist_field_sprite_2d.phl", "dist_field_sprite_2d", { "TWO_LAYERS", 0 } },
	{ SHADER_PATH "dist_field_sprite_2d.phl", "dist_field_sprite_2d", { "ONE_LAYER", "VERTICAL_GRADIENT", 0 } },
	{ SHADER_PATH "dist_field_sprite_2d.phl", "dist_field_sprite_2d", { "TWO_LAYERS", "VERTICAL_GRADIENT", 0 } },

	{ SHADER_PATH "sprite.phl", "default_sprite_pixelshader", { 0 } },

	{ SHADER_PATH "perfTest.phl", "main_output", { 0 } },

	{ SHADER_PATH "wireframe.phl", "wireframe_pixelshader", { 0 } },

	{ SHADER_PATH "effects/resolve_msaa_2_depth.phl", "main_output", { 0 }, true },
	{ SHADER_PATH "effects/resolve_msaa_4_depth.phl", "main_output", { 0 }, true },
	{ SHADER_PATH "effects/resolve_msaa_8_depth.phl", "main_output", { 0 }, true },
	{ SHADER_PATH "effects/resolve_msaa_16_depth.phl", "main_output", { 0 }, true },

	{ SHADER_PATH "effects/NoAlphaBlit.phl", "main_output", { "POSTPROCESSING", 0 } },
	{ SHADER_PATH "effects/NoAlphaBlit.phl", "main_output", { "POSTPROCESSING", "DOUBLE", 0 } },
	{ SHADER_PATH "effects/NoAlphaBlit.phl", "main_output", { "POSTPROCESSING", "QUADRUPLE", 0 } },

	{ SHADER_PATH "effects/NoAlphaBlit.phl", "main_output", { "POSTPROCESSING", "DEPTH", 0 } },
	{ SHADER_PATH "effects/NoAlphaBlit.phl", "main_output", { "POSTPROCESSING", "DOUBLE", "DEPTH", 0 } },
	{ SHADER_PATH "effects/NoAlphaBlit.phl", "main_output", { "POSTPROCESSING", "QUADRUPLE", "DEPTH", 0 } },

};

STATIC_ASSERT(ARRAY_SIZE(pixelShaderDefs) == PS_MAX);

int rxbxCompileMinimalVertexShaders(RdrDeviceDX *device);
void rxbxCompileMinimalShaders(RdrDeviceDX *device);

void rxbxSetupPrimitiveDrawMode(RdrDeviceDX *device);
void rxbxSetupPrimitiveVdecl(RdrDeviceDX *device, const VertexComponentInfo * components);
void rxbxSetupPostProcessScreenDrawMode(RdrDeviceDX *device, const VertexComponentInfo * components, RdrVertexDeclarationObj * vertex_declaration, bool no_offset);
void rxbxSetupPostProcessShapeDrawMode(RdrDeviceDX *device, const VertexComponentInfo * components, RdrVertexDeclarationObj * vertex_declaration, bool no_offset);
void rxbxSetupSpriteDrawMode(RdrDeviceDX *device, RdrVertexDeclarationObj vertex_declaration);
void rxbxSetupSpriteVdecl(RdrDeviceDX *device, const VertexComponentInfo * components, RdrVertexDeclarationObj * vertex_declaration);
void rxbxSetupParticleDrawMode(RdrDeviceDX *device, const VertexComponentInfo * components, RdrVertexDeclarationObj * vertex_declaration, bool no_normalmap, bool far_depth_range);
void rxbxSetupFastParticleCPUDrawMode(RdrDeviceDX *device, RdrVertexDeclarationObj vertex_declaration, bool far_depth_range);
void rxbxSetupFastParticleDrawMode(RdrDeviceDX *device, const VertexComponentInfo *components, RdrVertexDeclarationObj *vertex_declaration, bool bLinkedScale, bool bStreak, bool far_depth_range, bool bRGBBlend, bool bAnimatedTexture);
bool rxbxSetupTerrainDrawMode(RdrDeviceDX *device, RdrGeoUsage const * pUsage, DrawModeBits bits);
bool rxbxSetupNormalDrawMode(RdrDeviceDX *device, RdrGeoUsage const * pUsage, DrawModeBits bits);
bool rxbxSetupNormalClothDrawMode(RdrDeviceDX *device, const VertexComponentInfo * components, DrawModeBits bits, RdrVertexDeclarationObj * vertex_declaration);
void rxbxSetupPostprocessNormalPsDrawMode(RdrDeviceDX *device, const VertexComponentInfo * components, RdrVertexDeclarationObj * vertex_declaration);
void rxbxSetupCylinderTrailDrawMode(RdrDeviceDX *device, const VertexComponentInfo *components, RdrVertexDeclarationObj *vertex_declaration, bool far_depth_range);
void rxbxSetupStarFieldDrawMode(RdrDeviceDX *device, RdrGeoUsage const * pUsage, bool camera_facing, bool vertex_only_lighting);

void rxbxReloadDefaultShadersDirect(RdrDeviceDX *device, void *data_UNUSED, WTCmdPacket *packet);

void rxbxSetupSkinning(RdrDeviceDX *device, RdrDrawableSkinnedModel* draw_skin, bool bCopyScale);
void rxbxSetDepthResolveMode(RdrDeviceDX *device, int multisample_count);

void rxbxSetDefaultBlendMode(RdrDeviceDX *device);
void rxbxSetLoadingPixelShader(RdrDeviceDX *device);
void rxbxSetWireframePixelShader(RdrDeviceDX *device);
void rxbxSetPrimitiveBlendMode(RdrDeviceDX *device);
void rxbxSetSpriteEffectBlendMode(RdrDeviceDX *device, RdrSpriteEffect sprite_effect);
void rxbxSetParticleBlendMode(RdrDeviceDX *device);
void rxbxSetFastParticleBlendMode(RdrDeviceDX *device, bool no_tonemap, bool soft_particles, bool cpu_particles, bool manual_depth_test);
void rxbxSetCylinderTrailBlendMode(RdrDeviceDX *device);
ShaderHandle rxbxGetCopyBlendMode(RdrDeviceDX *device, RxbxSurfaceType type, int has_depth);
ShaderHandle rxbxGetPerfTestShader(RdrDeviceDX *device);
void rxbxBindBlendModeTextures(RdrDeviceDX *device, TexHandle *textures, U32 tex_count);
int rxbxBindTessellationShaders(RdrDeviceDX *device, RdrNonPixelMaterial *rdr_domain_material, DrawModeBits bits);
__forceinline void rxbxUnbindTessellationShaders(RdrDeviceDX *device);

int rxbxBindMaterial(RdrDeviceDX *device, RdrMaterial *rdr_material, 
					 RdrLightData *lights[MAX_NUM_OBJECT_LIGHTS], RdrLightColorType color_type, 
					 ShaderHandle handle, RdrMaterialShader uberlight_shader_num, 
					 bool uses_shadowbuffer, bool force_no_shadows, bool manual_depth_test); // Returns 0 if no shader was bound

int rxbxBindMaterialForDepth(RdrDeviceDX *device, RdrMaterial *rdr_material, ShaderHandle handle);

void rxbxPreloadVertexShaders(RdrDeviceDX *device, void *data_UNUSED, WTCmdPacket *packet);

void rxbxPreloadHullShaders(RdrDeviceDX *device, void *data_UNUSED, WTCmdPacket *packet);

void rxbxPreloadDomainShaders(RdrDeviceDX *device, void *data_UNUSED, WTCmdPacket *packet);

RxbxProgramDef* initVertexShaderDef(RdrDeviceDX *device, int shader_num);

#if !_PS3
void rxbxSetupQuadIndexList(RdrDeviceDX *device, U16 quad_count);
#endif
#endif //_RT_XDRAWMODE_H_


