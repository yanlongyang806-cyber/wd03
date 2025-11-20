#ifndef _RT_XSTATE_H_
#define _RT_XSTATE_H_
#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "RdrSurface.h"
#include "..\xdx.h"
#include "RdrShader.h"
#include "../Renderers/RdrDevicePrivate.h"

typedef struct RdrSurfaceDX RdrSurfaceDX;
typedef struct RdrDeviceDX RdrDeviceDX;
typedef struct RxbxVertexShader RxbxVertexShader;
typedef struct RxbxPixelShader RxbxPixelShader;
typedef struct RxbxHullShader RxbxHullShader;
typedef struct RxbxDomainShader RxbxDomainShader;
typedef struct RdrStencilFuncParams RdrStencilFuncParams;
typedef struct RdrStencilOpParams RdrStencilOpParams;
typedef struct RdrTextureDataDX RdrTextureDataDX;

#define VERIFY_STATEMANAGEMENT 0

#define MAX_TEXTURE_UNITS_TOTAL	16
#define MAX_HULL_TEXTURE_UNITS_TOTAL 1
#define MAX_DOMAIN_TEXTURE_UNITS_TOTAL 1	// BTH: Currently set to one where idx 0 = heightmap
#define MAX_VERTEX_TEXTURE_UNITS_TOTAL 4
#define MAX_TEXTURE_COORDS 8
#define MAX_VS_CONSTANTS 256
#define MAX_DS_CONSTANTS 256
#define MAX_PS_CONSTANTS 129
#define MAX_PS2X_CONSTANTS 32
#define MAX_PS_BOOL_CONSTANTS 16

#define LARGEST_MAX_TEXTURE_UNITS MAX(MAX(MAX(MAX_TEXTURE_UNITS_TOTAL, MAX_VERTEX_TEXTURE_UNITS_TOTAL),MAX_HULL_TEXTURE_UNITS_TOTAL),MAX_DOMAIN_TEXTURE_UNITS_TOTAL)

#define MAX_VERTEX_STREAMS 5

typedef struct RdrSurfaceStateDX RdrSurfaceStateDX;

#if !_XBOX		
// PC has closer depth values decreasing
#define D3DZCMP_L	D3DCMP_LESS
#define D3DZCMP_LE	D3DCMP_LESSEQUAL
#else
// Xbox has closer depth values increasing
#define D3DZCMP_L	D3DCMP_GREATER
#define D3DZCMP_LE	D3DCMP_GREATEREQUAL
#endif

typedef enum DepthTestMode
{
	DEPTHTEST_OFF = D3DCMP_ALWAYS,
	DEPTHTEST_LESS = D3DZCMP_L,
	DEPTHTEST_LEQUAL = D3DZCMP_LE,
	DEPTHTEST_EQUAL = D3DCMP_EQUAL,
} DepthTestMode;

typedef enum TextureStateType
{
	TEXTURE_PIXELSHADER,
	TEXTURE_VERTEXSHADER,
	TEXTURE_HULLSHADER,
	TEXTURE_DOMAINSHADER,
	TEXTURE_TYPE_COUNT
} TextureStateType;

#ifdef _XBOX
//We need to use these special modes to get the same VFACE behavior as DX9
typedef enum CullMode
{
	CULLMODE_NONE = GPUCULL_NONE_FRONTFACE_CW,
	CULLMODE_FRONT = GPUCULL_FRONT_FRONTFACE_CW,
	CULLMODE_BACK = GPUCULL_BACK_FRONTFACE_CW,
} CullMode;

#else

typedef enum CullMode
{
	CULLMODE_NONE = D3DCULL_NONE,
	CULLMODE_FRONT = D3DCULL_CW,
	CULLMODE_BACK = D3DCULL_CCW,
} CullMode;

#endif

//////////////////////////////////////////////////////////////////////////


void rxbxSetStateActive(RdrDeviceDX *device, RdrSurfaceStateDX *state, int firsttime, int surface_width, int surface_height);
void rxbxResetViewport(RdrDeviceDX *device, int surface_width, int surface_height);
void rxbxResetViewportEx(RdrDeviceDX *device, int surface_width, int surface_height, bool reset_for_clear);
void rxbxResetStateDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);
RdrSurfaceStateDX *rxbxGetCurrentState(RdrDeviceDX *device);

void rxbxApplyPixelTextureStateEx(RdrDeviceDX *device, D3DDevice *d3d_device);
void rxbxApplyTargets(RdrDeviceDX *device);
void rxbxApplyTargetsAndTextures(RdrDeviceDX *device);
void rxbxDirtyTargets(RdrDeviceDX *device);

void rxbxApplyTextureSlot(RdrDeviceDX *device, int tex_type, int tex_unit);
void rxbxApplyStatePreDraw(RdrDeviceDX *device);
void rxbxResetDeviceState(RdrDeviceDX *device);
void rxbxNotifyTextureFreed(RdrDeviceDX *device, RdrTextureDataDX *tex_data);

void rxbxDisownManagedParams(RdrDeviceDX *device);

// matrices
void rxbxSet3DProjection(RdrDeviceDX *device, RdrSurfaceStateDX *state, const Mat44 projection, const Mat44 fardepth_projection, const Mat44 sky_projection,F32 znear, F32 zfar, F32 far_znear, F32 viewport_x, F32 viewport_width, F32 viewport_y, F32 viewport_height, const Vec3 camera_pos);
void rxbxSetViewMatrix(RdrDeviceDX *device, RdrSurfaceStateDX *state, const Mat4 view_mat, const Mat4 inv_view_mat);
void rxbxSet3DMode(RdrDeviceDX *device, int need_model_mat);
void rxbxSet2DMode(RdrDeviceDX *device, int width, int height);
void rxbxPushModelMatrix(RdrDeviceDX *device, const Mat4 model_mat, bool is_skinned, bool camera_centered);
void rxbxPopModelMatrix(RdrDeviceDX *device);
void rxbxSetBoneInfo(RdrDeviceDX *device, U32 bone_num, const SkinningMat4 bone_info);
void rxbxSetBoneMatricesBatch(RdrDeviceDX *device, RdrDrawableSkinnedModel* draw_skin, bool bCopyScale);
void rxbxSetPPTextureSize(RdrDeviceDX *device, int width, int height, float alpha_x, float alpha_y, int actual_width, int actual_height, F32 offset_x, F32 offset_y, bool addHalfPixel2);
void rxbxSetMorphAndVertexLightParams(RdrDeviceDX *device, F32 morph_amt, F32 vlight_multiplier, F32 vlight_offset);
void rxbxSetWindParams(RdrDeviceDX *device, const Vec4 wind_params);
void rxbxSetSpecialShaderParameters(RdrDeviceDX *device, int num_vertex_shader_constants, const Vec4 *vertex_shader_constants);
void rxbxSetCylinderTrailParameters(RdrDeviceDX *device, const Vec4 *params, int count);
void rxbxSetSingleDirectionalLightParameters(RdrDeviceDX *device, RdrLightData *light_data, RdrLightColorType light_color_type);
void rxbxSetAllVertexLightParameters(RdrDeviceDX *device, int lightnum, RdrLightData *light_data, RdrLightColorType light_color_type);
void rxbxSetWorldTexParams(RdrDeviceDX *device, const Vec4 vecs[2]);

// stencil
void rxbxStencilFunc(RdrDeviceDX *device, D3DCMPFUNC func, S32 ref, U32 mask);
void rxbxStencilOp(RdrDeviceDX *device, D3DSTENCILOP fail, D3DSTENCILOP zfail, D3DSTENCILOP zpass);
void rxbxStencilFuncHandler(RdrDeviceDX *device, RdrStencilFuncParams * params, WTCmdPacket * packet);
void rxbxStencilOpHandler(RdrDeviceDX *device, RdrStencilOpParams * params, WTCmdPacket * packet);
void rxbxStencilMode(RdrDeviceDX *device, int stencil_mode, int stencil_ref);


// depth
void rxbxDepthWrite(RdrDeviceDX *device, BOOL enabled);
void rxbxDepthWritePush(RdrDeviceDX *device, BOOL enabled);
void rxbxDepthWritePop(RdrDeviceDX *device);
void rxbxDirtyDepthStencil(RdrDeviceDX *device);
void rxbxDepthTestPush(RdrDeviceDX *device, DepthTestMode depthMode);
void rxbxDepthTestPop(RdrDeviceDX *device);

#define rxbxDepthTest(device, mode) device->device_state.depth_stencil.depth_func = mode

#define rxbxAlphaToCoverage(device, on) device->device_state.blend.alpha_to_coverage_enable = (on)

// depth bias
void rxbxDepthBias(RdrDeviceDX *device, F32 depth_bias, F32 slope_scale_depth_bias);
void rxbxDepthBiasPush(RdrDeviceDX *device, F32 depth_bias, F32 slope_scale_depth_bias);
void rxbxDepthBiasPushAdd(RdrDeviceDX *device, F32 depth_bias, F32 slope_scale_depth_bias);
void rxbxDepthBiasPop(RdrDeviceDX *device);

// color
void rxbxColorWrite(RdrDeviceDX *device, BOOL enabled);
void rxbxColorWritePush(RdrDeviceDX *device, BOOL enabled);
void rxbxColorWritePop(RdrDeviceDX *device);

void rxbxColor(RdrDeviceDX *device, const Color color);
void rxbxColorf(RdrDeviceDX *device, const Vec4 color);
void rxbxInstanceParam(RdrDeviceDX *device, const Vec4 param);

// fast particle info
void rxbxSetFastParticleSetInfo(RdrDeviceDX *device, RdrDrawableFastParticles *fast_particles);

// textures
void rxbxBindWhiteTexture(RdrDeviceDX *device, U32 tex_unit);
void rxbxBindBlackTexture(RdrDeviceDX *device, U32 tex_unit);
void rxbxBindTexture(RdrDeviceDX *device, U32 tex_unit, TexHandle tex_handle);
void rxbxBindVertexTexture(RdrDeviceDX *device, U32 tex_unit, TexHandle tex_handle);
void rxbxBindHullTexture(RdrDeviceDX *device, U32 tex_unit, TexHandle tex_handle);
void rxbxBindDomainTexture(RdrDeviceDX *device, U32 tex_unit, TexHandle tex_handle);
void rxbxBindTextureEx(RdrDeviceDX *device, U32 tex_unit, TexHandle tex_handle, TextureStateType texture_type);
void rxbxTexLODBias(RdrDeviceDX *device, U32 tex_unit, F32 newbias);
void rxbxMarkTexturesUnused(RdrDeviceDX *device, TextureStateType texture_type);
void rxbxUnbindUnusedTextures(RdrDeviceDX *device, TextureStateType texture_type);
void rxbxUnbindVertexTextures(RdrDeviceDX *device);
void rxbxBindStereoscopicTexture(RdrDeviceDX *device);
void rxbxSetShadowBufferTextureActive(RdrDeviceDX *device, bool active);
void rxbxSetCubemapLookupTextureActive(RdrDeviceDX *device, bool active);
void rxbxDirtyTextures(RdrDeviceDX *device);

// fog
void rxbxFog(RdrDeviceDX *device, U32 on);
void rxbxFogPush(RdrDeviceDX *device, U32 on);
void rxbxFogPop(RdrDeviceDX *device);
void rxbxFogRange(RdrDeviceDX *device, RdrSurfaceStateDX *state, 
				  F32 low_near_dist, F32 low_far_dist, F32 low_max_fog,
				  F32 high_near_dist, F32 high_far_dist, F32 high_max_fog,
				  F32 low_height, F32 high_height);
void rxbxFogColor(RdrDeviceDX *device, RdrSurfaceStateDX *state, const Vec3 low_fog_color, const Vec3 high_fog_color);
void rxbxFogColorPush(RdrDeviceDX *device, const Vec3 low_fog_color, const Vec3 high_fog_color);
void rxbxFogColorPop(RdrDeviceDX *device);
void rxbxFogMode(RdrDeviceDX *device, RdrSurfaceStateDX *state, U32 bVolumeFog);

void rxbxSetPredicationConstantsAndManagedParams(RdrDeviceDX *device);


// vertex/hull/domain/pixel shaders
int rxbxBindVertexShader(RdrDeviceDX *device, ShaderHandle shader);
int rxbxBindHullShader(RdrDeviceDX *device, ShaderHandle shader);
int rxbxBindDomainShader(RdrDeviceDX *device, ShaderHandle shader);
int rxbxBindPixelShader(RdrDeviceDX *device, ShaderHandle shader, const BOOL boolean_constants[MAX_PS_BOOL_CONSTANTS]); // Returns 0 if the shader is not available
void rxbxDirtyShaders(RdrDeviceDX *device);
#define rxbxBindVertexShaderObject(device, vertex_shader_obj)\
	(device)->device_state.vertex_shader = (vertex_shader_obj)
#define rxbxBindHullShaderObject(device, hull_shaderx)\
	(device)->device_state.hull_shader = (hull_shaderx)
#define rxbxBindDomainShaderObject(device, domain_shaderx)\
	(device)->device_state.domain_shader = (domain_shaderx)
#define rxbxBindPixelShaderObject(device, pixel_shader_obj)\
	(device)->device_state.pixel_shader = (pixel_shader_obj)

void rxbxPixelShaderConstantParameters(RdrDeviceDX *device, U32 param_idx, const Vec4 *params, const int count, const int constant_type);
void rxbxPixelShaderLightParameters(RdrDeviceDX *device, U32 param_idx, const Vec4 *params, const int mat_const_offset, const int count);

void rxbxEmptyTessellationShaders(RdrDeviceDX *device);

#define rxbxSetVertexDeclaration(device, declaration)\
	(device)->device_state.vertex_declaration = (declaration)
void rxbxSetVertexStreamSource(RdrDeviceDX *device, int stream, RdrVertexBufferObj source, U32 stride, U32 start_offset);
#define rxbxSetVertexStreamNormal(device, stream)\
	(device)->device_state.vertex_stream_frequency[(stream)] = 1
#define rxbxSetVertexStreamIndexed(device, stream, num_instances)\
	/* maybe pass around our own flags and move this masking to when making the DX9 call? */\
	(device)->device_state.vertex_stream_frequency[(stream)] = (D3DSTREAMSOURCE_INDEXEDDATA | (num_instances))
#define rxbxSetVertexStreamInstanced(device, stream)\
	/* maybe pass around our own flags and move this masking to when making the DX9 call? */\
	(device)->device_state.vertex_stream_frequency[(stream)] = (D3DSTREAMSOURCE_INSTANCEDATA | 1ul)
#define rxbxRestoreVertexStreamFrequencyState(device, stream, pushed_state)\
	(device)->device_state.vertex_stream_frequency[(stream)] = (pushed_state);
void rxbxNotifyVertexBufferFreed(RdrDeviceDX *device, RdrVertexBufferObj vertex_buffer);

#define rxbxSetIndices(device, indices, is_32bit)\
	(((device)->device_state.index_buffer = (indices)),((device)->device_state.index_buffer_is_32bit = (is_32bit)))
void rxbxNotifyIndexBufferFreed(RdrDeviceDX *device, RdrIndexBufferObj index_buffer);
void rxbxResetStreamSource(RdrDeviceDX *device);
void rxbxResetStreamSourceAndIndex(RdrDeviceDX *device);
void rxbxDirtyStreamSources(RdrDeviceDX *device);

void rxbxApplyVertexDeclaration(RdrDeviceDX *device);
void rxbxDirtyVertexDeclaration(RdrDeviceDX *device);

// blend function
void rxbxBlendFunc(RdrDeviceDX *device, bool blend_enable, D3DBLEND sfactor, D3DBLEND dfactor, D3DBLENDOP op);
void rxbxBlendFuncSeparate(RdrDeviceDX *device, D3DBLEND clr_sfactor, D3DBLEND clr_dfactor, D3DBLENDOP clr_op,
						   D3DBLEND alpha_sfactor, D3DBLEND alpha_dfactor, D3DBLENDOP alpha_op);
void rxbxBlendFuncPush(RdrDeviceDX *device, bool blend_enable, D3DBLEND sfactor, D3DBLEND dfactor, D3DBLENDOP op);
void rxbxBlendFuncPushNop(RdrDeviceDX *device);
void rxbxBlendFuncPop(RdrDeviceDX *device);
void rxbxBlendStackFreeze(RdrDeviceDX *device, int freeze);
void rxbxDirtyBlend(RdrDeviceDX *device);

// lighting
void rxbxSetExposureTransform(RdrDeviceDX *device, const Vec4 *exposure_transform, WTCmdPacket *packet);
void rxbxSetShadowBufferTexture(RdrDeviceDX *device, TexHandle *tex_ptr, WTCmdPacket *packet);
void rxbxSetCubemapLookupTexture(RdrDeviceDX *device, TexHandle *tex_ptr, WTCmdPacket *packet);
void rxbxSetSoftParticleDepthTexture(RdrDeviceDX *device, TexHandle *tex_ptr, WTCmdPacket *packet);
void rxbxSetSSAODirectIllumFactor(RdrDeviceDX *device, const F32 *illum_factor, WTCmdPacket *packet);
void rxbxSetupAmbientLight(RdrDeviceDX *device, 
						   SA_PARAM_OP_VALID const RdrAmbientLight *light, 
						   SA_PRE_OP_RBYTES(sizeof(Vec3)) const Vec3 ambient_multiplier, 
						   SA_PRE_OP_RBYTES(sizeof(Vec3)) const Vec3 ambient_offset, 
						   SA_PRE_OP_RBYTES(sizeof(Vec3)) const Vec3 ambient_override, 
						   RdrLightColorType light_color_type);


// gpr allocation
void rxbxSetDefaultGPRAllocation(RdrDeviceDX *device);
void rxbxSetNormalGPRAllocation(RdrDeviceDX *device);
void rxbxSetPostprocessGPRAllocation(RdrDeviceDX *device);


// render states
#define rxbxSetCullMode(device, mode)\
	(device)->device_state.rasterizer.cull_mode = (mode)
#define rxbxSetFillMode(device, mode)\
	(device)->device_state.rasterizer.fill_mode = (mode)
#define rxbxSetScissorTest(device, enable)\
	(device)->device_state.rasterizer.scissor_test = (enable)
void rxbxSetForceFarDepth(RdrDeviceDX *device, bool enable, int depthLevel);


// drawing
void rxbxSetInstancedParams(RdrDeviceDX *device, const Mat4 world_matrix, const Vec4 tint_color, const Vec4 instance_param, bool affect_state_directly);
void rxbxDrawIndexedTriangles(RdrDeviceDX *device, int tri_base, int tri_count, int tri_index_range, int vertex_offset, bool apply_state);
void rxbxDrawIndexedTrianglesUP(RdrDeviceDX *device, int tri_count, U16 *indices, int vertex_count, void *vertices, int stride);
void rxbxDrawQuadsUP(RdrDeviceDX *device, int quad_count, void *vertices, int stride, bool apply_state);
void rxbxDrawIndexedQuadsUP(RdrDeviceDX *device, int quad_count, U16* indices, void *vertices, int vertices_count, int stride, bool apply_state, bool calc_min_max);
void rxbxDrawIndexedQuads32UP(RdrDeviceDX *device, int quad_count, U32* indices, void *vertices, int vertices_count, int stride, bool apply_state, bool calc_min_max);
void rxbxDrawIndexedQuads(RdrDeviceDX *device, int quad_count, int base_vertex_index, int start_index, int min_index, int max_index, bool apply_state);
void rxbxDrawTriangleStripUP(RdrDeviceDX *device, int vertex_count, void *vertices, int stride);
void rxbxDrawIndexedTriangleStripUP(RdrDeviceDX *device, int index_count, U16 *indices, int vertex_count, void *vertices, int stride);

// Tessellation related
__forceinline void rxbxDomainShaderParameters(RdrDeviceDX *device, U32 param_idx, const Vec4 *params, const int count);

static __forceinline DWORD F2DW(F32 f) { return *((DWORD*)&f); }

//////////////////////////////////////////////////////////////////////////

const char *rxbxGetStringForHResult(HRESULT hr);
void rxbxFatalHResultErrorf(RdrDeviceDX *device, HRESULT hr, const char *action_str, FORMAT_STR const char *detail_str, ...);

// Enabled to allow complete development mode validation on tip code.
// DIRECTX_HRESULT_CHECK_LEVEL 0 disables all validation.
// DIRECTX_HRESULT_CHECK_LEVEL 1 enables asserts on failed HRESULTs.
// DIRECTX_HRESULT_CHECK_LEVEL 2 enables asserts with information about all call points.
#define DIRECTX_HRESULT_CHECK_LEVEL 1
//#define DIRECTX_HRESULT_CHECK_LEVEL (1 && _FULLDEBUG)

#if DIRECTX_HRESULT_CHECK_LEVEL
#include "../RdrDevicePrivate.h"
#include "file.h"
#include "memlog.h"

#if DIRECTX_HRESULT_CHECK_LEVEL <= 1 || defined(_FULLDEBUG)
	#define ASSERT_DEVELOPMENT
#else
	#define ASSERT_DEVELOPMENT assert(IsDebuggerPresent() || isDevelopmentMode());
#endif
#define DOING_CHECKX

#if DIRECTX_HRESULT_CHECK_LEVEL >= 2
__forceinline static HRESULT checkx_func(HRESULT result, const char *strFunc, const char *strCall)
{
	// This level of verification is only for development mode or when debugging.
	ASSERT_DEVELOPMENT
	//rdrCheckThread();
	if (FAILED(result) || result != S_OK)
		memlog_printf(NULL, "%s %s = 0x%x\n", strFunc, strCall, result);
	assert(!FAILED(result));
	return result;
}
#define CHECKX(func) (checkx_func(func, __FUNCTION__, #func))
#else
__forceinline static HRESULT checkx_func(HRESULT result)
{
	// Disabled to allow this validation path to run in production mode and on builders
	//ASSERT_DEVELOPMENT
	//rdrCheckThread();
	assert(!FAILED(result));
	return result;
}
#define CHECKX(func) (checkx_func(func))
#endif

#define ASSERTX(expr) ASSERT_DEVELOPMENT assert(expr)
#define CHECKTHREAD ASSERT_DEVELOPMENT rdrCheckThread()
#define CHECKNOTTHREAD ASSERT_DEVELOPMENT rdrCheckNotThread()
#define CHECKDEVICELOCK(device) ASSERT_DEVELOPMENT assert(((RdrDevice *)device)->is_locked_thread)
#define CHECKSURFACEACTIVE(surface) ASSERT_DEVELOPMENT assert(((RdrDeviceDX *)((RdrSurface *)surface)->device)->active_surface == surface)
#define CHECKSURFACENOTACTIVE(surface) ASSERT_DEVELOPMENT assert(((RdrDeviceDX *)((RdrSurface *)surface)->device)->active_surface != surface)
#else
#define ASSERTX(expr)
#define CHECKX(func) func
#define CHECKTHREAD
#define CHECKNOTTHREAD
#define CHECKDEVICELOCK(device)
#define CHECKSURFACEACTIVE(surface)
#define CHECKSURFACENOTACTIVE(surface)
#endif

#if 1
#define HAVE_INC_STATE_CHANGE 1
#define INC_STATE_CHANGE(state, count) device->device_base.perf_values.state_changes.state += count
#else
#define INC_STATE_CHANGE(state, count)
#endif

#define INC_OPERATION(op, count) device->device_base.perf_values.operations.op += count
#define INC_DRAW_CALLS(tri_count) { device->device_base.perf_values.operations.draw_call_count++; device->device_base.perf_values.operations.triangle_count += (tri_count); }
#define INC_RESOLVES(surface_width, surface_height, buffer_count) { device->device_base.perf_values.operations.resolve_count++; device->device_base.perf_values.operations.resolve_pixel_count += (surface_width) * (surface_height) * (buffer_count); }
#define INC_CLEARS(surface_width, surface_height, buffer_count) { device->device_base.perf_values.operations.clear_count++; device->device_base.perf_values.operations.clear_pixel_count += (surface_width) * (surface_height) * (buffer_count); }
#define INC_SURFACE_ACTIVE() device->device_base.perf_values.operations.surface_active_count++
#define INC_SCREEN_POSTPROCESS(surface_width, surface_height, buffer_count) {device->device_base.perf_values.operations.postprocess_count++; device->device_base.perf_values.operations.postprocess_pixel_count += (surface_width) * (surface_height) * (buffer_count); }

typedef struct RdrSamplerState
{
	union
	{
		U32 comparevalue;
		struct {
			//F32 mip_lod_bias; // Never actually used currently.
			U32 address_u:2;
			U32 address_v:2;
			U32 address_w:2;
			U32 mag_filter:2;
			U32 min_filter:2;
			U32 mip_filter:2; // Ignored in D3D11 (whether or not we have mips is just determined by the texture bound?)
			U32 max_anisotropy:5; // could save a bit by storing -1
			U32 max_mip_level:3; // This is a float in DX11, may want to access this as a float for smooth texture streaming
			U32 srgb:1;			// ignored in d3d11, part of the resource view

			// Comparison state stuff.
			U32 use_comparison_mode:1;
			U32 comparison_func:3;
		};
	};
} RdrSamplerState;
STATIC_ASSERT(sizeof(RdrSamplerState) == SIZEOF2(RdrSamplerState, comparevalue));

typedef enum RdrManagedValueState
{
	RMVS_UNKNOWN,	// driver value is unknown and there is no desired value
	RMVS_CLEAN,		// driver value equals desired value
	RMVS_DIRTY,		// desired value has changed
	RMVS_FORCE_UPDATE,
} RdrManagedValueState;

typedef struct RdrDeviceTextureStateDX
{
	RdrTextureObj texture;
	RdrSamplerState sampler_state;
} RdrDeviceTextureStateDX;

typedef U32 StateFlagType;
#define STATEFLAGTYPE_BITS (sizeof(StateFlagType) * CHAR_BIT)

#define FLAG_BITS_PER_STATE 2
#define STATE_FLAG_BITMASK ((1<<FLAG_BITS_PER_STATE) - 1)

#define STATE_FLAG_ENTRY_FROM_STATE(state_count) ((state_count)*FLAG_BITS_PER_STATE/STATEFLAGTYPE_BITS)
#define NUM_STATE_FLAG_ENTRIES(state_count) ((state_count*FLAG_BITS_PER_STATE+STATEFLAGTYPE_BITS-1)/STATEFLAGTYPE_BITS)
#define NUM_STATE_FLAGS_PER_ENTRY (STATEFLAGTYPE_BITS/FLAG_BITS_PER_STATE)

__forceinline static StateFlagType STATE_FLAG_SHIFT(U32 flag_index)
{
	return (flag_index % NUM_STATE_FLAGS_PER_ENTRY) * FLAG_BITS_PER_STATE;
}

__forceinline static StateFlagType STATE_FLAG_MASK(U32 flag_index)
{
	return STATE_FLAG_BITMASK << STATE_FLAG_SHIFT(flag_index);
}

__forceinline static StateFlagType STATE_FLAG_VALUE(U32 flag_index, StateFlagType flag_value)
{
	return flag_value << STATE_FLAG_SHIFT(flag_index);
}

__forceinline static StateFlagType STATE_FLAG_ENTRY_VALUE(U32 entry, U32 flag_index)
{
	return (entry >> STATE_FLAG_SHIFT(flag_index)) & STATE_FLAG_BITMASK;
}

__forceinline static int MaskForFlagsUpTo(int flag_index)
{
	return (1 << ((flag_index * FLAG_BITS_PER_STATE) % STATEFLAGTYPE_BITS)) - 1;
}

#define DEFINE_STATE_FLAGS(state_mem, flag_count)											\
	U32 state_mem##_modified_flags[NUM_STATE_FLAG_ENTRIES(flag_count)]
#define STATE_FLAG_ENTRY(state_mem, flag_index)												\
	device->device_state.state_mem##_modified_flags[STATE_FLAG_ENTRY_FROM_STATE(flag_index)]

#define STATE_FLAG(state, flag_index)													\
	STATE_FLAG_ENTRY_VALUE(STATE_FLAG_ENTRY(state, flag_index), (flag_index))
#define MASKED_STATE_FLAG(state, flag_index)											\
	(STATE_FLAG_ENTRY(state, flag_index) & ~STATE_FLAG_MASK(flag_index))
#define SET_STATE_FLAG(state, flag_index, flag_value)									\
	STATE_FLAG_ENTRY(state, flag_index) = MASKED_STATE_FLAG(state, flag_index) |		\
		STATE_FLAG_VALUE(flag_index, flag_value)

#define AND_STATE_FLAG(state, flag_index, flag_value)									\
	STATE_FLAG_ENTRY(state, flag_index) &= STATE_FLAG_VALUE(flag_index, flag_value)
#define OR_STATE_FLAG(state, flag_index, flag_value)									\
	STATE_FLAG_ENTRY(state, flag_index) |= STATE_FLAG_VALUE(flag_index, flag_value)

#define CLEAN_STATE_FLAG(state, flag_index)												\
	AND_STATE_FLAG(state, flag_index, 0)
#define DIRTY_STATE_FLAG(state, flag_index)												\
	OR_STATE_FLAG(state, flag_index, 1)
#define FORCE_STATE_FLAG(state, flag_index)												\
	OR_STATE_FLAG(state, flag_index, 2)

__forceinline static void ClearFlagStateRange(int * flags, 
											  int start_flag, int flag_count)
{
	int first_flag_entry = STATE_FLAG_ENTRY_FROM_STATE(start_flag);
	int last_flag_entry = STATE_FLAG_ENTRY_FROM_STATE(start_flag + flag_count - 1);
	int mask = MaskForFlagsUpTo(start_flag);
	int last_mask = ~MaskForFlagsUpTo(start_flag + flag_count);
	if (first_flag_entry == last_flag_entry)
		mask |= last_mask;
	for ( ; first_flag_entry <= last_flag_entry; )
	{
		flags[ first_flag_entry ] &= mask;
		mask = 0;
		++first_flag_entry;
		if (first_flag_entry == last_flag_entry)
			mask = last_mask;
	}
}

#define VS_STATE_FLAG_ENTRY_COUNT NUM_STATE_FLAG_ENTRIES(MAX_VS_CONSTANTS)
#define PS_STATE_FLAG_ENTRY_COUNT NUM_STATE_FLAG_ENTRIES(MAX_PS_CONSTANTS)

typedef union RdrRasterizerState
{
	U64 comparevalue[2];
	struct
	{
		F32 depth_bias; // Is an INT in D3D11, does it still go through F2DW
		//FLOAT DepthBiasClamp; We don't ever change this
		F32 slope_scale_depth_bias;
		/*D3DFILLMODE*/ U32 fill_mode:2;
		/*D3DCULL*/ U32 cull_mode:2;
		//U32 FrontCounterClockwise:1; We don't ever change this
		//U32 DepthClipEnable:1; We don't ever change this
		U32 scissor_test:1;
		//U32 MultisampleEnable:1; We don't ever change this?
		//U32 AntialiasedLineEnable:1; We don't ever change this
	};
} RdrRasterizerState;
STATIC_ASSERT(sizeof(RdrRasterizerState) == SIZEOF2(RdrRasterizerState, comparevalue));

typedef union RdrBlendState
{
	U32 comparevalue;
	struct {
		U32 alpha_to_coverage_enable:1;
		//U32 IndependentBlendEnable:1; // off, we only track/support one blend equation
		U32 blend_enable:1; // we used to determine this implicitly from blend ops and surface type
		/*D3DBLEND*/ U32 src_blend:4;
		/*D3DBLEND*/ U32 dest_blend:4;
		/*D3DBLENDOP*/ U32 blend_op:3;
		U32 blend_alpha_separate:1;
		/*D3DBLEND*/ U32 src_blend_alpha:4;
		/*D3DBLEND*/U32 dest_blend_alpha:4;
		/*D3DBLENDOP*/U32 blend_op_alpha:3;
		U32 write_mask:4; // RGBA write mask
	};
} RdrBlendState;
STATIC_ASSERT(sizeof(RdrBlendState) == SIZEOF2(RdrBlendState, comparevalue));

typedef union RdrDepthStencilState {
	U64 comparevalue;
	struct  
	{
		U32 stencil_ref:8;
		// U32 DepthEnable:1; // always on in our code, we set the function to always instead?
		U32 stencil_read_mask:8;
		U32 stencil_write_mask:8;
		U32 depth_write:1;
		/*D3DCMPFUNC*/ U32 depth_func:4;
		U32 stencil_enable:1;

		// Consume last two bits so the stencil fail doesn't straddle two words/bytes
		U32 pad:2;

		// D3D11 has separate back face stencil ops as well, we use the same for both
		/*D3DSTENCILOP*/ U32 stencil_fail_op:4;
		/*D3DSTENCILOP*/ U32 stencil_depth_fail_op:4;
		/*D3DSTENCILOP*/ U32 stencil_pass_op:4;
		/*D3DCMPFUNC*/ U32 stencil_func:4;
	};
} RdrDepthStencilState;
STATIC_ASSERT(sizeof(RdrDepthStencilState) == SIZEOF2(RdrDepthStencilState, comparevalue));

typedef union RdrVertexStreamState {
#ifdef _M_X64
	U64 comparevalue[2];
#else
	U64 comparevalue;
#endif
	struct {
		RdrVertexBufferObj source;
		U32 offset:24;
		U32 stride:8;
	};
} RdrVertexStreamState;
STATIC_ASSERT(sizeof(RdrVertexStreamState) == SIZEOF2(RdrVertexStreamState, comparevalue));

// The number of buffers accurately reflects the number of buffers contained in d3d11_vs_constants.hlsl
typedef enum VsConstantBufferEnum {
	VS_CONSTANT_BUFFER_SKY,
	VS_CONSTANT_BUFFER_FRAME,
	VS_CONSTANT_BUFFER_OBJECT,
	VS_CONSTANT_BUFFER_SPECIAL_OBJECT,
	VS_CONSTANT_BUFFER_SPECIAL_PARAMS,
	VS_CONSTANT_BUFFER_PARTICLE_AND_ANIMATION,
	VS_CONSTANT_BUFFER_COUNT,
} VsConstantBufferEnum;

// Size matters!  Reflects the number of vec4s each buffer consumes in device->device_state.vs_constants_desired
// This is also used to determine the size of each constant buffer generated in function rxbxLazyInitConstantBuffers.
// Array vsConstantBufferSizes containing values is located in rt_xstate.c which must be modified should order or number of buffers is altered.
typedef enum {
	VS_CONSTANT_BUFFER_SKY_SIZE = 7,
	VS_CONSTANT_BUFFER_FRAME_SIZE = 10,
	VS_CONSTANT_BUFFER_OBJECT_SIZE = 23,
	VS_CONSTANT_BUFFER_SPECIAL_OBJECT_SIZE = 5,
	VS_CONSTANT_BUFFER_SPECIAL_PARAMS_SIZE = 5,
	VS_CONSTANT_BUFFER_PARTICLE_AND_ANIMATION_SIZE = 156,
	VS_CONSTANT_BUFFER_TOTAL_SIZE = VS_CONSTANT_BUFFER_SKY_SIZE + \
		VS_CONSTANT_BUFFER_FRAME_SIZE + \
		VS_CONSTANT_BUFFER_OBJECT_SIZE + \
		VS_CONSTANT_BUFFER_SPECIAL_OBJECT_SIZE + \
		VS_CONSTANT_BUFFER_SPECIAL_PARAMS_SIZE + \
		VS_CONSTANT_BUFFER_PARTICLE_AND_ANIMATION_SIZE,
} VsConstantBufferSizeEnum;

// The number of buffers accurately reflects the number of buffers contained in d3d11_vs_constants.hlsl
typedef enum DsConstantBufferEnum {
	DS_CONSTANT_BUFFER_PROJECTIONS,
	DS_CONSTANT_BUFFER_SCALER_AND_OFFSET,
	DS_CONSTANT_BUFFER_COUNT,
} DsConstantBufferEnum;

// Size matters!  Reflects the number of vec4s each buffer consumes in device->device_state.vs_constants_desired
// This is also used to determine the size of each constant buffer generated in function rxbxLazyInitConstantBuffers.
// Array vsConstantBufferSizes containing values is located in rt_xstate.c which must be modified should order or number of buffers is altered.
typedef enum {
	DS_CONSTANT_BUFFER_PROJECTIONS_SIZE = 12,
	DS_CONSTANT_BUFFER_SCALER_AND_OFFSET_SIZE = 1,
	DS_CONSTANT_BUFFER_TOTAL_SIZE = DS_CONSTANT_BUFFER_PROJECTIONS_SIZE + \
		DS_CONSTANT_BUFFER_SCALER_AND_OFFSET_SIZE,
} DsConstantBufferSizeEnum;

// The number of buffers accurately reflects the number of buffers contained in d3d11_vs_constants.hlsl
typedef enum PsConstantBufferType {
	PS_CONSTANT_BUFFER_VIEWPF,
	PS_CONSTANT_BUFFER_SKY,
	PS_CONSTANT_BUFFER_MATERIAL,
	PS_CONSTANT_BUFFER_LIGHT,
	PS_CONSTANT_BUFFER_MISCPF,
	PS_CONSTANT_BUFFER_COUNT,
	PS_CONSTANT_BUFFER_DEBUG = PS_CONSTANT_BUFFER_COUNT,	// if this value changes, it must be reflected with DebugPF in d3d11_ps_constants.hlsl
} PsConstantBufferType;

#define SHADER_INITIAL_CONSTANT_SIZE 2
#define SHADER_CONSTANT_FLEXIBLE_BUFFER_BUCKETS 7
#define SHADER_CONSTANT_FLEXIBLE_BUFFER_INSTANCES 2
#define SHADER_CONSTANT_BUFFER_FLEXIBLE_MAX (SHADER_INITIAL_CONSTANT_SIZE << SHADER_CONSTANT_FLEXIBLE_BUFFER_BUCKETS)

typedef enum {
	PS_CONSTANT_BUFFER_VIEWPF_SIZE = 4,
	PS_CONSTANT_BUFFER_SKY_SIZE = 7,
	PS_CONSTANT_BUFFER_MATERIAL_SIZE = SHADER_CONSTANT_BUFFER_FLEXIBLE_MAX,
	PS_CONSTANT_BUFFER_LIGHT_SIZE = SHADER_CONSTANT_BUFFER_FLEXIBLE_MAX,
	PS_CONSTANT_BUFFER_MISCPF_SIZE = 5,
	PS_CONSTANT_BUFFER_DEBUG_SIZE = 1,
	PS_CONSTANT_BUFFER_TOTAL_SIZE = PS_CONSTANT_BUFFER_VIEWPF_SIZE + \
		PS_CONSTANT_BUFFER_SKY_SIZE + PS_CONSTANT_BUFFER_MISCPF_SIZE + \
		PS_CONSTANT_BUFFER_MATERIAL_SIZE + PS_CONSTANT_BUFFER_LIGHT_SIZE,
} PsConstantBufferSizeEnum;

#define PAD_CONSTANT(num) (((num + 31) / 32) * 32)	// Sets the value to the next multiple of 32

typedef struct VECALIGN RdrDeviceStateDX
{
	DEFINE_STATE_FLAGS(vs_constants, MAX_VS_CONSTANTS);
	Vec4 vs_constants_driver[MAX_VS_CONSTANTS];
	Vec4 vs_constants_desired[MAX_VS_CONSTANTS];
	bool vs_constants_dirty[PAD_CONSTANT(VS_CONSTANT_BUFFER_COUNT)];
	U8 vs_constant_buffer_LUT[PAD_CONSTANT(MAX_VS_CONSTANTS)];

	Vec4 ds_constants_driver[MAX_DS_CONSTANTS];
	Vec4 ds_constants_desired[MAX_DS_CONSTANTS];
	bool ds_constants_dirty[PAD_CONSTANT(DS_CONSTANT_BUFFER_COUNT)];
	U8 ds_constant_buffer_LUT[PAD_CONSTANT(MAX_DS_CONSTANTS)];


	Vec4 ps_constants_driver[MAX_PS_CONSTANTS];
	union {
		struct { // used in D3D9
			Vec4 ps_constants_desired[MAX_PS_CONSTANTS];
		};
		struct { // used in D3D11
			Vec4 ps_constants_desired_filled[PS_CONSTANT_BUFFER_TOTAL_SIZE];	// Just to contain the data in an aligned fashion.  It should always be accessed though through the ps_constants_desired field below.
			Vec4 ps_constants_debug[PS_CONSTANT_BUFFER_DEBUG_SIZE];
		};
	};
	Vec4 *ps_constants_desired_d3d11[PS_CONSTANT_BUFFER_COUNT];
	U8 ps_constants_dirty[PAD_CONSTANT(PS_CONSTANT_BUFFER_COUNT)];

	// New DX11-style state structures

	struct {
		DX11BufferPoolObj *d3d11_psconstants_buffer_pool;
		DX11BufferObj *d3d11_psconstants[PS_CONSTANT_BUFFER_COUNT];

		DX11BufferPoolObj *d3d11_debug_psconstants_buffer_pool;
		DX11BufferObj *d3d11_debug_psconstants;
	};

	DX11BufferObj d3d11_vsconstants[PAD_CONSTANT(VS_CONSTANT_BUFFER_COUNT)];
	DX11BufferObj d3d11_dsconstants[PAD_CONSTANT(DS_CONSTANT_BUFFER_COUNT)];

	U32 ps_bool_constants;
	U32 ps_bool_constants_driver;

	RdrDeviceTextureStateDX textures[TEXTURE_TYPE_COUNT][LARGEST_MAX_TEXTURE_UNITS];
	RdrDeviceTextureStateDX textures_driver[TEXTURE_TYPE_COUNT][LARGEST_MAX_TEXTURE_UNITS];

	RxbxVertexShader * active_vertex_shader_wrapper;
	RxbxPixelShader * active_pixel_shader_wrapper;
	RdrVertexShaderObj vertex_shader;
	RdrVertexShaderObj vertex_shader_driver;

	RdrPixelShaderObj pixel_shader;
	RdrPixelShaderObj pixel_shader_driver;

	RxbxHullShader * active_hull_shader_wrapper;
	ID3D11HullShader *hull_shader;
	ID3D11HullShader *hull_shader_driver;

	RxbxDomainShader * active_domain_shader_wrapper;
	ID3D11DomainShader *domain_shader;
	ID3D11DomainShader *domain_shader_driver;

	RdrVertexDeclarationObj vertex_declaration;
	RdrVertexDeclarationObj vertex_declaration_driver;

	RdrVertexStreamState vertex_stream[MAX_VERTEX_STREAMS];
	RdrVertexStreamState vertex_stream_driver[MAX_VERTEX_STREAMS];
	U32 vertex_stream_frequency[MAX_VERTEX_STREAMS];
	U32 vertex_stream_frequency_driver[MAX_VERTEX_STREAMS];

	RdrIndexBufferObj index_buffer;
	RdrIndexBufferObj index_buffer_driver;
	bool index_buffer_is_32bit;
	bool index_buffer_is_32bit_driver;

	D3D11_PRIMITIVE_TOPOLOGY prim_topo;
	D3D11_PRIMITIVE_TOPOLOGY prim_topo_driver;

	RdrSurfaceObj targets[SBUF_MAX];
	RdrSurfaceObj targets_driver[SBUF_MAX];

	// Grouped state blocks
	RdrRasterizerState rasterizer;
	RdrRasterizerState rasterizer_driver;

	RdrBlendState blend;
	RdrBlendState blend_driver;

	RdrDepthStencilState depth_stencil;
	RdrDepthStencilState depth_stencil_driver;
#ifdef _M_X64
	U32 pad[2]; // padding to align up to vector alignment
#endif
} RdrDeviceStateDX;

typedef struct RdrFogDistance
{
	F32 low_near_dist;
	F32 low_far_dist;
	F32 low_max_fog;
	F32 high_near_dist;
	F32 high_far_dist;
	F32 high_max_fog;
	F32 low_height;
	F32 high_height;
} RdrFogDistance;

typedef struct RdrFogColor
{
	Vec3 low_color;
	Vec3 high_color;
} RdrFogColor;

typedef struct RdrTextureStateDX
{
	RdrTexHandle bound_id;
	RdrSurfaceDX *bound_surface;
	RdrSurfaceBuffer bound_surface_buffer;
	int bound_surface_set_index;
	bool is_unused;
} RdrTextureStateDX;

typedef struct RdrSurfaceStateDX
{
	Mat44	projection_mat3d;
	Mat44	far_depth_projection_mat3d;
	Mat44	sky_projection_mat3d;
	Mat44	inv_projection_mat3d;
	Mat44	last_mvp_mat3d;
	Mat44	viewmat;
	Mat4	viewmat4;
	Mat4	inv_viewmat;
	Vec3	camera_pos_ws;
	Mat44	modelmat;
	Mat4	fog_mat;
	Vec4	depth_range;
	Vec4	viewport;

	int surface_width, surface_height;

	int width_2d, height_2d;
	int need_model_mat;
	int has_model_matrix_pushed;
	bool force_far_depth;

	struct
	{
		D3DCMPFUNC func;
		S32 ref;
		U32 mask;
		D3DSTENCILOP fail;
		D3DSTENCILOP zfail;
		D3DSTENCILOP zpass;
		int    enabled;
	} stencil;

	RdrTextureStateDX textures[TEXTURE_TYPE_COUNT][MAX(MAX_TEXTURE_UNITS_TOTAL, MAX_VERTEX_TEXTURE_UNITS_TOTAL)];

	struct 
	{
		struct
		{
			bool blend_enable;
			bool separate_alpha;
			D3DBLEND sfactor, dfactor;
			D3DBLENDOP op;
			D3DBLEND alpha_sfactor, alpha_dfactor;
			D3DBLENDOP alpha_op;
		} stack[32];
		int stack_idx;
		int stack_frozen;
	} blend_func;

	struct
	{
		U32 on:1;
		U32 force_off:1;
		U32 volume:1;
		RdrFogDistance current, old;
		U32 stack[4];
		int stack_depth;
		RdrFogColor color_stack[4];
		int color_stack_depth;
	} fog;

	struct 
	{
		BOOL stack[8];
		int stack_depth;
	} depth_write;

	struct 
	{
		BOOL stack[8];
		int stack_depth;
	} depth_test;

	struct 
	{
		BOOL stack[4];
		int stack_depth;
	} color_write;

	struct 
	{
		struct
		{
			F32 depth_bias;
			F32 slope_scale_depth_bias;
		} stack[4];
		int stack_depth;
	} depth_bias;

	TexHandle shadow_buffer_tex_handle;
	TexHandle cubemap_lookup_tex_handle;
	TexHandle soft_particle_depth_tex_handle;

	Vec4 inv_screen_params; // [1/screen_width, 1/screen_height, (tile_x+0.5)/screen_width, (tile_y+0.5)/screen_height]

} RdrSurfaceStateDX;

typedef union ShaderHandleAndFlags
{
	U32 value;
	struct {
		U32 handle:31; // Not actually using all these bits
		U32 loaded:1; // Has at least been sent off to get loaded, the vertex shader will be filled in when it's ready
	};
} ShaderHandleAndFlags;

void rxbxCheckStateDirect(RdrDeviceDX *device);

void rxbxClearTextureStateOfSurface(RdrDeviceDX *device, RdrSurfaceDX *surface, RdrSurfaceBuffer buffer, int set_index);

void rxbxInitConstantBuffers(RdrDeviceDX *device);
void rxbxDestroyConstantBuffers(RdrDeviceDX *device);

bool rxbxShouldApplyTexturesAndStatesTogether(void);

void rxbxApplyDebugBuffer(RdrDeviceDX *device, Vec4 *debug_constants);

#endif //_RT_XSTATE_H_
