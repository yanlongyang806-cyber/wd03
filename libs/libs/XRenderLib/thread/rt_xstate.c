#include <math.h>
#include <float.h>

#include "rt_xstate.h"
#include "Color.h"
#include "RdrLightAssembly.h"
#include "RdrState.h"
#include "RenderLib.h"
#include "ScratchStack.h"
#include "UnitSpec.h"
#include "XRenderLib.h"
#include "memlog.h"
#include "MemReport.h"
#include "LinearAllocator.h"
#include "rt_xdrawmode.h"
#include "rt_xshader.h"
#include "rt_xsurface.h"
#include "rt_xtextures.h"
#include "sysutil.h"
#include "xdevice.h"
#include "trivia.h"
#include "file.h"
#include "Vec4H.h"
#include "rt_xStateEnums.h"

#include "stringutil.h"
#include "endian.h"
#include "VirtualMemory.h"

// Checks that state management is correct before and after applying state
#define VERIFY_STATE 0

#if VERIFY_STATE
int do_verify_state=1;
#else
// Shouldn't be referenced, but lets zero it in case someone makes a mistake
int do_verify_state=0;
#endif
// If VERIFY_STATE is defined, disable/enable verifying of state
AUTO_CMD_INT(do_verify_state, rdrVerifyState) ACMD_COMMANDLINE ACMD_CATEGORY(Debug);

// In some tests, it was slightly faster not to forcibly inline these functions
#if 0
#define STATE_APPLY_INLINE __forceinline
#else
#define STATE_APPLY_INLINE
#endif

static const int DEFAULT_ALPHA_REFERENCE = 153;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

static const char *g_last_bound_pixel_shader;
static const char *g_last_bound_vertex_shader;
static const char *g_last_bound_hull_shader;
static const char *g_last_bound_domain_shader;

static void * const g_dirty_state_ptr = (void *)0xdeadbeef;

void handleBadState(RdrDeviceDX *device, const char *check);

#define ENABLE_STATEMANAGEMENT 1
#define USE_BLACK_TEX_FOR_UNBOUND 0

#define DEBUG_TRACE_SHADER 0
#if DEBUG_TRACE_SHADER
#define SHADER_LOG OutputDebugStringf
#else
#define SHADER_LOG( ... )
#endif

#define D3DCOLORWRITEENABLE_NONE 0
#if !_XBOX
#define D3DCOLORWRITEENABLE_ALL (D3DCOLORWRITEENABLE_ALPHA|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_RED)
#endif

// Notes for this array can be found  with the enum VsConstantBufferSizeEnum
// Order is extremely important!
const unsigned int vsConstantBufferSizes[] = {
	VS_CONSTANT_BUFFER_SKY_SIZE,
	VS_CONSTANT_BUFFER_FRAME_SIZE,
	VS_CONSTANT_BUFFER_OBJECT_SIZE,
	VS_CONSTANT_BUFFER_SPECIAL_OBJECT_SIZE,
	VS_CONSTANT_BUFFER_SPECIAL_PARAMS_SIZE,
	VS_CONSTANT_BUFFER_PARTICLE_AND_ANIMATION_SIZE,
	VS_CONSTANT_BUFFER_TOTAL_SIZE,
};
const unsigned int dsConstantBufferSizes[] = {
	DS_CONSTANT_BUFFER_PROJECTIONS_SIZE,
	DS_CONSTANT_BUFFER_SCALER_AND_OFFSET,
	DS_CONSTANT_BUFFER_TOTAL_SIZE,
};
const unsigned int psConstantBufferSizes[] = {
	PS_CONSTANT_BUFFER_VIEWPF_SIZE,
	PS_CONSTANT_BUFFER_SKY_SIZE,
	PS_CONSTANT_BUFFER_MATERIAL_SIZE,
	PS_CONSTANT_BUFFER_LIGHT_SIZE,
	PS_CONSTANT_BUFFER_MISCPF_SIZE,
};
const unsigned int psConstantBufferOffsets[] = {
	PS_START_VIEWPF_CONSTANTS,
	PS_CONSTANT_SKY_OFFSET,
	PS_CONSTANT_MATERIAL_PARAM_OFFSET,
	PS_CONSTANT_MATERIAL_PARAM_OFFSET,
	PS_CONSTANT_MISCPF_OFFSET,
};

STATIC_ASSERT(ARRAY_SIZE(vsConstantBufferSizes) == VS_CONSTANT_BUFFER_COUNT + 1);	// Ensure the above array stays consistent with the vs buffer count enum.

__forceinline static void assertValidRdrTextureObj(RdrDeviceDX * device, RdrTextureObj * texture)
{
	// hit the memory to ensure the texture is actually accessible
	if (texture && texture->typeless)
	{
		if (device->d3d11_device)
		{
			assert( !IsBadReadPtr( texture->texture_view_d3d11, sizeof( DWORD ) ) );
			ID3D11View_AddRef(texture->texture_view_d3d11);
			ID3D11View_Release(texture->texture_view_d3d11);
		}
		else
		{
			assert( !IsBadReadPtr( texture->texture_base_d3d9, sizeof( DWORD ) ) );
			IDirect3DBaseTexture9_AddRef(texture->texture_base_d3d9);
			IDirect3DBaseTexture9_Release(texture->texture_base_d3d9);
		}
	}
}

__forceinline static void assertValidTexturePointer(RdrDeviceDX * device, RdrTextureDataDX * texture)
{
	assertValidRdrTextureObj(device, &texture->texture);
}

#if USE_BLACK_TEX_FOR_UNBOUND
__forceinline static TexHandle RdrUnboundTextureHandle(RdrSurfaceStateDX *current_state)
{
	return RdrTexHandleToTexHandle(current_state->black_tex_handle);
}
#else
__forceinline static TexHandle RdrUnboundTextureHandle(RdrSurfaceStateDX *current_state)
{
	return RDR_NULL_TEXTURE;
}
#endif

// New state management
#define GET_DESIRED_VALUE(state_name) (device->device_state.state_name)

// Old state manager

#define SET_VSCONST_VALUE4(state_name, new_value) { copyVec4((new_value), device->device_state.state_name); DIRTY_STATE_FLAG(vs_constants,param_idx + i); }
#define SET_PSCONST_VALUE4(state_name, new_value) { copyVec4((new_value), device->device_state.state_name); DIRTY_STATE_FLAG(ps_constants,param_idx + i); }


__forceinline static DWORD F2DWDepth(F32 f)
{
#if _XBOX
	f = -f;
#endif
	return *((DWORD*)&f);
}

RdrSurfaceStateDX *current_state = NULL;
RdrSurfaceStateDX null_current_state = {0};

#define STEREOSCOPIC_TEX_UNIT	13
#define CUBEMAP_TEX_UNIT		14
#define SHADOWBUFFER_TEX_UNIT	15

__forceinline static bool activeDepthIsDepthTexture(RdrDeviceDX *device)
{
	const RdrSurfaceParams* params;
	if (device->override_depth_surface)
	{
		params = &device->override_depth_surface->params_thread;
	}
	else
	{
		params = &device->active_surface->params_thread;
	}

	return (params->depth_bits > 0 && (params->flags & SF_DEPTH_TEXTURE));
}

__forceinline static void rxbxVSBufferSetDirty(RdrDeviceDX *device, const U32 index) {
	device->device_state.vs_constants_dirty[device->device_state.vs_constant_buffer_LUT[index]] = true;
}

__forceinline static void rxbxDSBufferSetDirty(RdrDeviceDX *device, const U32 index) {
	device->device_state.ds_constants_dirty[device->device_state.ds_constant_buffer_LUT[index]] = true;
}

// The param_idx and (param_idx + count) must remain in the same constant buffer to ensure update.
// If this isn't the case, then the buffers need to be rearranged to make sure it IS the case.
__forceinline static void rxbxVertexShaderParameters(RdrDeviceDX *device, U32 param_idx, const Vec4 *params, const int count)
{
	int i;
#ifdef _FULLDEBUG
	devassert(param_idx + count <= MAX_VS_CONSTANTS);
#endif
	if (device->d3d11_device) {
		rxbxVSBufferSetDirty(device, param_idx);
		for (i = 0; i < count; ++i) {
			copyVec4(params[i], device->device_state.vs_constants_desired[param_idx + i]);
		}
	} else {
		for (i = 0; i < count; ++i)
		{
			SET_VSCONST_VALUE4(vs_constants_desired[param_idx + i], params[i]);
		}
	}
}

__forceinline void rxbxDomainShaderParameters(RdrDeviceDX *device, U32 param_idx, const Vec4 *params, const int count)
{
	int i;
#ifdef _FULLDEBUG
	devassert(param_idx + count <= MAX_DS_CONSTANTS);
#endif
	rxbxDSBufferSetDirty(device, param_idx);
	for (i = 0; i < count; ++i) {
		copyVec4(params[i], device->device_state.ds_constants_desired[param_idx + i]);
	}
}

__forceinline static void rxbxPixelShaderParameters(RdrDeviceDX *device, U32 param_idx, const Vec4 *params, const int count)
{
	int i;
#ifdef _FULLDEBUG
	devassert(param_idx + count <= MAX_PS_CONSTANTS);
#endif
	for (i = 0; i < count; ++i)
	{
		copyVec4(params[i], device->device_state.ps_constants_desired[param_idx + i]);
	}
}

__forceinline static void rxbxCopyVec4Buffer(Vec4 *destBuffer, const Vec4 *srcBuffer, const U32 idx, const U32 count)
{
	U32 i;
	for (i = 0; i < count; ++i) {
		copyVec4(srcBuffer[i], destBuffer[idx + i]);
	}
}

__forceinline static
void rxbxPixelShaderBufferParameters(RdrDeviceDX *device, int param_idx, const Vec4 *params, const int count,
									const int bufferSize, Vec4 *buffer, const int bufferIdx, const int bufferOffset)
{
	int maxAllowed = param_idx + count;

	// Possibly demote to fulldebug later.
	devassert(maxAllowed<= bufferSize);
	device->device_state.ps_constants_dirty[bufferIdx] = device->device_state.ps_constants_dirty[bufferIdx] < maxAllowed ? maxAllowed : device->device_state.ps_constants_dirty[bufferIdx];

	if (device->d3d11_device) {
		rxbxCopyVec4Buffer(buffer, params, param_idx, count);
	} else {
		rxbxPixelShaderParameters(device, param_idx + bufferOffset, params, count);
	}
}

void rxbxPixelShaderConstantParameters(RdrDeviceDX *device, U32 param_idx, const Vec4 *params, const int count, const int constant_type)
{
	rxbxPixelShaderBufferParameters(device, param_idx, params, count, psConstantBufferSizes[constant_type],
		device->device_state.ps_constants_desired_d3d11[constant_type], constant_type, psConstantBufferOffsets[constant_type]);
}

void rxbxPixelShaderLightParameters(RdrDeviceDX *device, U32 param_idx, const Vec4 *params, const int mat_const_offset, const int count)
{
	int maxAllowed = param_idx + count;
#ifdef _FULLDEBUG
	devassert(maxAllowed <= SHADER_CONSTANT_BUFFER_FLEXIBLE_MAX);
#endif
	device->device_state.ps_constants_dirty[PS_CONSTANT_BUFFER_LIGHT] = device->device_state.ps_constants_dirty[PS_CONSTANT_BUFFER_LIGHT] < maxAllowed ? maxAllowed : device->device_state.ps_constants_dirty[PS_CONSTANT_BUFFER_LIGHT];

	if (device->d3d11_device) {
		rxbxCopyVec4Buffer(device->device_state.ps_constants_desired_d3d11[PS_CONSTANT_BUFFER_LIGHT], params, param_idx, count);
	} else {
		rxbxPixelShaderParameters(device, param_idx + PS_CONSTANT_MATERIAL_PARAM_OFFSET + mat_const_offset, params, count);
	}
}

__forceinline static void setDSMatrix(RdrDeviceDX *device, int constant_idx, const Mat44 mat)
{
	rxbxDomainShaderParameters(device, constant_idx, mat, 4);
}

__forceinline static void setMatrix(RdrDeviceDX *device, int constant_idx, const Mat44 mat)
{
	rxbxVertexShaderParameters(device, constant_idx, mat, 4);
}

__forceinline static void setMatrixUnit(RdrDeviceDX *device, int constant_idx)
{
	rxbxVertexShaderParameters(device, constant_idx, unitmat44, 4);
}

// If this is ever used for Light parameters, a special case will have to be made for it.
__forceinline static void setMatrixPS4x3(RdrDeviceDX *device, int constant_idx, const Mat4 mat, PsConstantBufferType type)
{
	Vec4 rowsAsColumns[3];
	getMatRow(mat, 0, rowsAsColumns[0]);
	getMatRow(mat, 1, rowsAsColumns[1]);
	getMatRow(mat, 2, rowsAsColumns[2]);
	assertmsg(type != PS_CONSTANT_BUFFER_LIGHT,"This function is not designed to work with the Light buffer type.");
	rxbxPixelShaderConstantParameters(device, constant_idx, rowsAsColumns, 3, type);
}

RdrSurfaceStateDX *rxbxGetCurrentState(RdrDeviceDX *device)
{
	return current_state;
}

static void rxbxSetManagedParamsOnlyFromWithinSetPredicationConstants(RdrDeviceDX *device)
{
	Vec4 proj_z, proj_w;

	getMatRow(current_state->projection_mat3d, 2, proj_z);
	getMatRow(current_state->projection_mat3d, 3, proj_w);

#if _XBOX
	if (!device->owns_constants)
	{
		IDirect3DDevice9_GpuOwnPixelShaderConstantF(device->d3d_device, PS_START_VIEWPF_CONSTANTS, PS_END_VIEWPF_CONSTANTS - PS_START_VIEWPF_CONSTANTS);
		device->owns_constants = 1;
	}
    {
	    D3DVECTOR4* pWriteCombined = NULL;
	    IDirect3DDevice9_GpuBeginPixelShaderConstantF4(device->d3d_device, PS_START_VIEWPF_CONSTANTS, &pWriteCombined, PS_END_VIEWPF_CONSTANTS - PS_START_VIEWPF_CONSTANTS);
	    copyVec4(current_state->inv_screen_params, pWriteCombined[0].v);
	    copyVec4(proj_z, pWriteCombined[1].v);
	    copyVec4(proj_w, pWriteCombined[2].v);
	    copyVec4(current_state->depth_range, pWriteCombined[3].v);
    }
	IDirect3DDevice9_GpuEndPixelShaderConstantF4(device->d3d_device);
	INC_STATE_CHANGE(ps_constants, 4);
#else
	// Everything contained within constant buffer 0 is here.
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_INV_SCREEN_PARAMS, &current_state->inv_screen_params, 1, PS_CONSTANT_BUFFER_VIEWPF);
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_PROJ_MAT_Z, &proj_z, 1, PS_CONSTANT_BUFFER_VIEWPF);
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_PROJ_MAT_W, &proj_w, 1, PS_CONSTANT_BUFFER_VIEWPF);
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_DEPTH_RANGE, &current_state->depth_range, 1, PS_CONSTANT_BUFFER_VIEWPF);
#endif
}

void rxbxDisownManagedParams(RdrDeviceDX *device)
{
#if _XBOX
	if (device->owns_constants)
	{
		IDirect3DDevice9_GpuDisownPixelShaderConstantF(device->d3d_device, PS_START_VIEWPF_CONSTANTS, PS_END_VIEWPF_CONSTANTS - PS_START_VIEWPF_CONSTANTS);
		device->owns_constants = 0;
	}
#endif
}

void rxbxSetDefaultGPRAllocation(RdrDeviceDX *device)
{
#if _XBOX
	IDirect3DDevice9_SetShaderGPRAllocation(device->d3d_device, 0, 64, 64);
#endif
}

void rxbxSetNormalGPRAllocation(RdrDeviceDX *device)
{
#if _XBOX
	IDirect3DDevice9_SetShaderGPRAllocation(device->d3d_device, 0, 48, 80);
#endif
}

void rxbxSetPostprocessGPRAllocation(RdrDeviceDX *device)
{
#if _XBOX
	IDirect3DDevice9_SetShaderGPRAllocation(device->d3d_device, 0, 32, 96);
#endif
}

__forceinline static void releaseSurface(int tex_unit, TextureStateType texture_type)
{
	RdrSurfaceDX * currentBoundSurface = current_state->textures[texture_type][tex_unit].bound_surface;
	RdrSurfaceBuffer currentSurfaceBuffer = current_state->textures[texture_type][tex_unit].bound_surface_buffer;
	int currentSurfaceBufferIndex = current_state->textures[texture_type][tex_unit].bound_surface_set_index;
	// Clear first so that this doesn't happen recursively!
	current_state->textures[texture_type][tex_unit].bound_surface = 0;
	current_state->textures[texture_type][tex_unit].bound_surface_buffer = 0;
	current_state->textures[texture_type][tex_unit].bound_surface_set_index = 0;
	rxbxReleaseSurfaceDirect(currentBoundSurface, texture_type==TEXTURE_VERTEXSHADER, tex_unit, currentSurfaceBuffer, currentSurfaceBufferIndex);
}

__forceinline static void resetTextureState(RdrDeviceDX *device, int unbind_textures)
{
	int i, j;
	if (current_state)
	{
		for (j=0; j<TEXTURE_TYPE_COUNT; j++)
		{
			for (i = 0; i < ARRAY_SIZE(current_state->textures[j]); i++)
			{
				if (!unbind_textures)
				{
					TexHandle unknown_handle = RDR_UNKNOWN_TEXTURE;
					// put the bound texture in an unknown state to force an init
					current_state->textures[j][i].bound_id = TexHandleToRdrTexHandle(unknown_handle);
				}

				if (current_state->textures[j][i].bound_surface)
					releaseSurface(i, j);

				if (unbind_textures)
				{
					TexHandle null_handle = RDR_NULL_TEXTURE;
					// put the bound texture in an unknown state to force an init
					current_state->textures[j][i].bound_id = TexHandleToRdrTexHandle(null_handle);

					device->device_state.textures[j][i].texture.typeless = NULL;
					current_state->textures[j][i].is_unused = true;
				}
			}
		}
	}
}

void rxbxResetStateDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
	CHECKTHREAD;

	if (device->isLost || !current_state)
		return;

	// stencil
	current_state->stencil.func = -1;
	current_state->stencil.ref = -1;
	current_state->stencil.mask = -1;
	current_state->stencil.fail = -1;
	current_state->stencil.zfail = -1;
	current_state->stencil.zpass = -1;
	current_state->stencil.enabled = -1;

	// textures
	resetTextureState(device, 0);

	// blend function stack
	current_state->blend_func.stack_frozen = 0;
	current_state->blend_func.stack_idx = 0;

	// fog
	current_state->fog.on = -1;
	current_state->fog.old.low_near_dist = -1;
	current_state->fog.old.low_far_dist = -1;
	current_state->fog.old.high_near_dist = -1;
	current_state->fog.old.high_far_dist = -1;
	current_state->fog.old.low_height = -1;
	current_state->fog.old.high_height= -1;

	// projection
	current_state->width_2d = 0;
	current_state->need_model_mat = 0;
	setMatrix(device, VS_CONSTANT_PROJ_MAT, current_state->projection_mat3d);
}

__forceinline static void setFog(RdrDeviceDX *device, const RdrFogColor *color, const RdrFogDistance *dist)
{
	Vec4 fogvec;
	F32 low_height = dist->low_height;
	F32 high_height = dist->high_height;

	if (high_height < low_height + 0.01f)
		high_height = low_height + 0.01f;

	fogvec[0] = dist->low_near_dist;
	fogvec[1] = 1.f / AVOID_DIV_0(dist->low_far_dist - dist->low_near_dist);
	fogvec[2] = dist->high_near_dist;
	fogvec[3] = 1.f / AVOID_DIV_0(dist->high_far_dist - dist->high_near_dist);
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_FOG_DIST, &fogvec, 1, PS_CONSTANT_BUFFER_SKY);
	rxbxVertexShaderParameters(device, VS_CONSTANT_FOGDIST, &fogvec, 1);

	fogvec[0] = low_height;
	fogvec[1] = 1.f / (high_height - low_height);
	fogvec[2] = fogvec[3] = 0;
	rxbxVertexShaderParameters(device, VS_CONSTANT_FOG_HEIGHT_PARAMS, &fogvec, 1);

	copyVec3(color->low_color, fogvec);
	fogvec[3] = dist->low_max_fog;
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_FOG_COLOR_LOW, &fogvec, 1, PS_CONSTANT_BUFFER_SKY);
	rxbxVertexShaderParameters(device, VS_CONSTANT_FOGCOLOR_LOW, &fogvec, 1);

	copyVec3(color->high_color, fogvec);
	fogvec[3] = dist->high_max_fog;
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_FOG_COLOR_HIGH, &fogvec, 1, PS_CONSTANT_BUFFER_SKY);
	rxbxVertexShaderParameters(device, VS_CONSTANT_FOGCOLOR_HIGH, &fogvec, 1);
}

__forceinline static void setDepthWrite(RdrDeviceDX *device, BOOL enabled)
{
	device->device_state.depth_stencil.depth_write = enabled;
}

__forceinline static void setColorWrite(RdrDeviceDX *device, BOOL enabled)
{
	device->device_state.blend.write_mask = enabled?0xf:0;
}

__forceinline static void setDepthBias(RdrDeviceDX *device, F32 depth_bias, F32 slope_scale_depth_bias)
{
	if (rdr_state.disableDepthBias)
	{
		depth_bias = 0;
		slope_scale_depth_bias = 0;
	}

	device->device_state.rasterizer.depth_bias = depth_bias;
	device->device_state.rasterizer.slope_scale_depth_bias = slope_scale_depth_bias;
}

__forceinline static int rxbxBufferTypeIsFloat(RdrSurfaceBufferType type)
{
	type &= SBT_TYPE_MASK;
	return type == SBT_FLOAT || type == SBT_RG_FLOAT || type == SBT_RGBA_FLOAT || type == SBT_RGBA_FLOAT32;
}

__forceinline static int rxbxSurfaceIsFloat(RdrSurfaceDX * surface)
{
	return rxbxBufferTypeIsFloat(surface->buffer_types[0]) ||
		rxbxBufferTypeIsFloat(surface->buffer_types[1]) ||
		rxbxBufferTypeIsFloat(surface->buffer_types[2]) ||
		rxbxBufferTypeIsFloat(surface->buffer_types[3]);
}

__forceinline static void setBlend(RdrDeviceDX *device, bool blend_enable, D3DBLEND sfactor, D3DBLEND dfactor, D3DBLENDOP op)
{
	if (!device->active_surface || !device->active_surface->supports_post_pixel_ops)
	{
		// force blend off if the surface doesn't support post-pixel ops (alpha blend, test, etc)
		blend_enable = false;
		sfactor = D3DBLEND_ONE;
		dfactor = D3DBLEND_ZERO;
		op = D3DBLENDOP_ADD;
	}
	device->device_state.blend.blend_enable = blend_enable;
	device->device_state.blend.src_blend = sfactor;
	device->device_state.blend.dest_blend = dfactor;
	device->device_state.blend.blend_op = op;
	device->device_state.blend.blend_alpha_separate = 0;
}

__forceinline static void setBlendSeparate(RdrDeviceDX *device, D3DBLEND clr_sfactor, D3DBLEND clr_dfactor, D3DBLENDOP clr_op,
										   D3DBLEND alpha_sfactor, D3DBLEND alpha_dfactor, D3DBLENDOP alpha_op)
{
	if (!device->active_surface || !device->active_surface->supports_post_pixel_ops)
	{
		// force blend off if the surface doesn't support post-pixel ops (alpha blend, test, etc)
		setBlend(device, false, D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD);
		return;
	}
	device->device_state.blend.blend_enable = true;
	device->device_state.blend.src_blend = clr_sfactor;
	device->device_state.blend.dest_blend = clr_dfactor;
	device->device_state.blend.blend_op = clr_op;
	device->device_state.blend.blend_alpha_separate = 1;
	device->device_state.blend.src_blend_alpha = alpha_sfactor;
	device->device_state.blend.dest_blend_alpha = alpha_dfactor;
	device->device_state.blend.blend_op_alpha = alpha_op;
}

static const F32 MSAA4_ofs = 0.5f;
static const F32 MSAA2_ofs = 0.5f;
static const F32 MSAA1_ofs = 0.5f;
int msaa_scale = 1;

void rxbxResetViewport(RdrDeviceDX *device, int surface_width, int surface_height)
{
	rxbxResetViewportEx(device, surface_width, surface_height, false);
}

void rxbxResetViewportEx(RdrDeviceDX *device, int surface_width, int surface_height, bool reset_for_clear)
{
	Vec4 sizeparam;
	D3DVIEWPORT9 viewport;

	CHECKTHREAD;

    {
		if (reset_for_clear)
		{
			// Don't be greedy.  You only need to clear outside the
			// viewport because of blurring and downsampling in
			// postprocessing, which shouldn't need more than ~16
			// pixels, give or take.  Double that should be very safe.
			viewport.X = MAX(0, round(current_state->viewport[0] * surface_width) - 32);
			viewport.Width = MIN(surface_width, round(current_state->viewport[1] * surface_width) + 32);
			viewport.Y = MAX(0, round(current_state->viewport[2] * surface_width) - 32);
			viewport.Height = MIN(surface_height, round(current_state->viewport[3] * surface_height) + 32);
		}
		else
		{
			viewport.X = round(current_state->viewport[0] * surface_width);
			viewport.Width = round(current_state->viewport[1] * surface_width);
			viewport.Y = round(current_state->viewport[2] * surface_height);
			viewport.Height = round(current_state->viewport[3] * surface_height);
		}
    #if !_XBOX
	    viewport.MinZ = 0;
	    viewport.MaxZ = 1;
    #else
	    viewport.MinZ = 1; // to enable HiZ
	    viewport.MaxZ = 0;
    #endif

		if (device->d3d11_device)
		{
			D3D11_VIEWPORT viewport11;
			D3D11_RECT scissor_rect;

			viewport11.TopLeftX = viewport.X;
			viewport11.TopLeftY = viewport.Y;
			viewport11.Width = viewport.Width;
			viewport11.Height = viewport.Height;
			viewport11.MinDepth = viewport.MinZ;
			viewport11.MaxDepth = viewport.MaxZ;
			scissor_rect.left = viewport.X;
			scissor_rect.top = viewport.Y;
			scissor_rect.right = viewport.X + viewport.Width;
			scissor_rect.bottom = viewport.Y + viewport.Height;
			ID3D11DeviceContext_RSSetViewports(device->d3d11_imm_context, 1, &viewport11);
			ID3D11DeviceContext_RSSetScissorRects(device->d3d11_imm_context, 1, &scissor_rect);
		}
		else
		if (device->d3d_device)
		{
		    CHECKX(IDirect3DDevice9_SetViewport(device->d3d_device, &viewport));
		}
    }

	INC_STATE_CHANGE(viewport, 1);

	current_state->surface_width = surface_width;
	current_state->surface_height = surface_height;

	// transform from screen coordinates [-0.5, width - 0.5] to texture coordinates [0, 1]
	setVec4(sizeparam, 0.5f, -0.5f, 0.5f + 0.5f / viewport.Width, 0.5f + 0.5f / viewport.Height);
	rxbxVertexShaderParameters(device, VS_CONSTANT_SCREEN_SIZE, &sizeparam, 1);

	rxbxSetPredicationConstantsAndManagedParams(device);
}

void rxbxSetStateActive(RdrDeviceDX *device, RdrSurfaceStateDX *state, int firsttime, int surface_width, int surface_height)
{
	int i, j;

	ADD_MISC_COUNT( 1, "SetStateActive" );

	if (current_state)
	{
		for (j=0; j<TEXTURE_TYPE_COUNT; j++)
		{
			for (i = 0; i < ARRAY_SIZE(current_state->textures[j]); i++)
			{
 				if (current_state->textures[j][i].bound_surface)
					rxbxBindTextureEx(device, i, RDR_NULL_TEXTURE, j); // Bind NULL texture
			}
		}
	}
	if (!state)
	{
		current_state = &null_current_state;
		return;
	}

	current_state = state;

	if (!state)
		return;

	CHECKTHREAD;

	if (firsttime)
	{
		rxbxResetDeviceState(device);
		rxbxResetStateDirect(device, NULL, NULL);

		copyMat44(unitmat44, current_state->viewmat);
		copyMat4(unitmat, current_state->viewmat4);
		copyMat4(unitmat, current_state->inv_viewmat);
		copyMat44(unitmat44, current_state->projection_mat3d);
		setVec4(current_state->viewport, 0, 1, 0, 1);
		current_state->width_2d = 1; // force the state manager to load the new matrices
		rxbxSet3DMode(device, 0);

		if (!device->d3d11_device)
		{
			CHECKX(IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_ZENABLE, TRUE));

			// These two are only used by AlphaToCoverage render operations when AtoC is disabled
			CHECKX(IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL));
			CHECKX(IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_ALPHAREF, DEFAULT_ALPHA_REFERENCE)); // 0.6
		} else {
			// These are the defaults and are applied when we apply our first render state
		}

		rxbxDepthTest(device, DEPTHTEST_LEQUAL);
		rxbxColorWrite(device, TRUE);
		rxbxDepthBias(device, 0, 0);

		rxbxFog(device, 1);
		rxbxFogColor(device, current_state, zerovec4, zerovec4);

		rxbxResetViewport(device, surface_width, surface_height);

		return;
	}

	// viewport
	rxbxResetViewport(device, surface_width, surface_height);

	// projection
	if (current_state->width_2d)
	{
		int width = current_state->width_2d;
		int height = current_state->height_2d;
		current_state->width_2d = current_state->height_2d = 0;
		rxbxSet2DMode(device, width, height);
	}
	else
	{
		current_state->width_2d = 1; // force the state manager to load matrices
		rxbxSet3DMode(device, 0);
	}

	// stencil
	if (current_state->stencil.enabled == 0)
	{
		device->device_state.depth_stencil.stencil_enable = 0;
	}
	else if (current_state->stencil.enabled == 1)
	{
		device->device_state.depth_stencil.stencil_enable = 1;
	}
	if (current_state->stencil.func != -1)
	{
		U32 stencil_mask = 0;
		device->device_state.depth_stencil.stencil_func = current_state->stencil.func;
		device->device_state.depth_stencil.stencil_ref = current_state->stencil.ref;
		if(!rdr_state.disableStencilDepthTexture || !activeDepthIsDepthTexture(device))
			stencil_mask = current_state->stencil.mask;
		device->device_state.depth_stencil.stencil_read_mask = stencil_mask;
		device->device_state.depth_stencil.stencil_write_mask = stencil_mask;
	}
	if (current_state->stencil.fail != -1)
	{
		device->device_state.depth_stencil.stencil_fail_op = current_state->stencil.fail;
		device->device_state.depth_stencil.stencil_depth_fail_op = current_state->stencil.zfail;
		device->device_state.depth_stencil.stencil_pass_op = current_state->stencil.zpass;
	}

	// textures
	resetTextureState(device, 0);

	// blend function
	if (device->active_surface && !device->active_surface->supports_post_pixel_ops)
		// force off
		setBlend(device, false, D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD);
	else
	if (current_state->blend_func.stack_idx >= 0)
		setBlend(device, current_state->blend_func.stack[current_state->blend_func.stack_idx].blend_enable, current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].op);
	else
		// default is alpha blend
		setBlend(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);

	// fog
	setFog(device, &current_state->fog.color_stack[current_state->fog.color_stack_depth], &current_state->fog.current);

	// depth
	setDepthWrite(device, current_state->depth_write.stack[current_state->depth_write.stack_depth]);

	// color
	setColorWrite(device, current_state->color_write.stack[current_state->color_write.stack_depth]);

	// depth bias
	setDepthBias(device, current_state->depth_bias.stack[current_state->depth_bias.stack_depth].depth_bias, current_state->depth_bias.stack[current_state->depth_bias.stack_depth].slope_scale_depth_bias);

	rxbxSetNormalGPRAllocation(device);
}

//////////////////////////////////////////////////////////////////////////

void rxbxSet3DProjection(RdrDeviceDX *device, RdrSurfaceStateDX *state, 
						 const Mat44 projection, const Mat44 fardepth_projection, const Mat44 sky_projection,
						 F32 znear, F32 zfar, F32 far_znear, 
						 F32 viewport_x, F32 viewport_width, F32 viewport_y, F32 viewport_height, 
						 const Vec3 camera_pos)
{
	Vec4 viewport;

	copyVec3(camera_pos, state->camera_pos_ws);
	copyMat44(projection, state->projection_mat3d);
	copyMat44(fardepth_projection, state->far_depth_projection_mat3d);
	copyMat44(sky_projection, state->sky_projection_mat3d);
	copyMat44(projection, state->projection_mat3d);
	invertMat44Copy(projection, state->inv_projection_mat3d);

	// MS: ABS(zfar) removed on next line for internal compiler error using April 2007 XDK.
	state->depth_range[0] = fabs(zfar);
	state->depth_range[1] = 1.f / AVOID_DIV_0(state->depth_range[0]);
	state->depth_range[2] = 1.f / projection[0][0];
	state->depth_range[3] = 1.f / projection[1][1];

	setVec4(viewport, saturate(viewport_x), saturate(viewport_width), saturate(viewport_y), saturate(viewport_height));
	if (state == current_state && !sameVec4(viewport, current_state->viewport))
	{
		copyVec4(viewport, current_state->viewport);
		rxbxResetViewport(device, current_state->surface_width, current_state->surface_height);
	}
	else
	{
		copyVec4(viewport, state->viewport);
	}

	if (state == current_state)
	{
		current_state->width_2d = 1; // force the state manager to load the new projection matrix
		rxbxSet3DMode(device, 0);
	}
}

void rxbxSet3DMode(RdrDeviceDX *device, int need_model_mat)
{
	CHECKTHREAD;

#if ENABLE_STATEMANAGEMENT
	if (current_state->width_2d || current_state->has_model_matrix_pushed)
#endif
	{
		Vec4 view_to_world_Y;

		rxbxSetPredicationConstantsAndManagedParams(device); // Was calling rxbxSetManagedParams() before, bad!

		if (device->d3d11_device) {
			setDSMatrix(device, DS_CONSTANT_MODELVIEW_MAT, current_state->viewmat);
		}
		setMatrix(device, VS_CONSTANT_MODELVIEW_MAT, current_state->viewmat);
		setMatrixUnit(device, VS_CONSTANT_MODEL_MAT); // Actual matrix set later by caller

		setMatrix(device, VS_CONSTANT_VIEW_MAT, current_state->viewmat);
		getMatRow(current_state->inv_viewmat, 1, view_to_world_Y);
		rxbxVertexShaderParameters(device, VS_CONSTANT_VIEW_TO_WORLD_Y, &view_to_world_Y, 1);
		if (current_state->force_far_depth)
		{
			Mat44 projection_mat3d;
			copyMat44(current_state->projection_mat3d, projection_mat3d);
			projection_mat3d[0][2] = projection_mat3d[0][3];
			projection_mat3d[1][2] = projection_mat3d[1][3];
			projection_mat3d[2][2] = projection_mat3d[2][3];
			projection_mat3d[3][2] = projection_mat3d[3][3];
			if (device->d3d11_device)
				setDSMatrix(device, DS_CONSTANT_PROJ_MAT, projection_mat3d);
			setMatrix(device, VS_CONSTANT_PROJ_MAT, projection_mat3d);
		}
		else
		{
			if (device->d3d11_device)
				setDSMatrix(device, DS_CONSTANT_PROJ_MAT, current_state->projection_mat3d);
			setMatrix(device, VS_CONSTANT_PROJ_MAT, current_state->projection_mat3d);
		}
		setMatrixPS4x3(device, PS_CONSTANT_INVVIEW_MAT, current_state->inv_viewmat, PS_CONSTANT_BUFFER_MISCPF); // Should only do this on non-SM30 cards?

		current_state->width_2d = 0;
		current_state->height_2d = 0;
		current_state->has_model_matrix_pushed = 0;
	}

	current_state->need_model_mat = need_model_mat;
	rxbxFog(device, 1);
}

static const F32 D3D9_HALF_PIXEL_SPRITE_OFFSET_X = -0.5f;
static const F32 D3D9_HALF_PIXEL_SPRITE_OFFSET_Y = +0.5f;

static const float ORTHO_NEAR_Z = -1.0f;
static const float ORTHO_FAR_Z = 1000.0f;

void rxbxSet2DMode(RdrDeviceDX *device, int width, int height)
{
	CHECKTHREAD;

#if ENABLE_STATEMANAGEMENT
	if (width != current_state->width_2d || height != current_state->height_2d)
#endif
	{
		Mat44 projection;

        rdrSetupOrthoDX(projection, 0, (F32)width, 0, (F32)height, ORTHO_NEAR_Z, ORTHO_FAR_Z);

		if (device->d3d_device)
		{
			int surface_width = 1, surface_height = 1;
			if (device->active_surface)
			{
				surface_width = device->active_surface->width_thread;
				surface_height = device->active_surface->height_thread;
			}
			projection[3][0] += D3D9_HALF_PIXEL_SPRITE_OFFSET_X * width / surface_width * projection[0][0];
			projection[3][1] += D3D9_HALF_PIXEL_SPRITE_OFFSET_Y * height / surface_height * projection[1][1];
		}

		setMatrixUnit(device, VS_CONSTANT_MODELVIEW_MAT);
        setMatrixUnit(device, VS_CONSTANT_MODEL_MAT);
		setMatrixUnit(device, VS_CONSTANT_VIEW_MAT);
		rxbxVertexShaderParameters(device, VS_CONSTANT_VIEW_TO_WORLD_Y, &upvec, 1);
		setMatrix(device, VS_CONSTANT_PROJ_MAT, projection);

		current_state->width_2d = width;
		current_state->height_2d = height;
	}

	current_state->need_model_mat = 0;
	rxbxFog(device, 0);
}

void rxbxSetViewMatrix(RdrDeviceDX *device, RdrSurfaceStateDX *state, const Mat4 view_mat, const Mat4 inv_view_mat)
{
	mat43to44(view_mat, state->viewmat);
	copyMat4(view_mat, state->viewmat4);
	copyMat4(inv_view_mat, state->inv_viewmat);

	if (state == current_state)
	{
		Vec4 view_to_world_Y;

		CHECKTHREAD;

		setMatrixUnit(device, VS_CONSTANT_MODEL_MAT);
		setMatrix(device, VS_CONSTANT_MODELVIEW_MAT, current_state->viewmat);
		setMatrix(device, VS_CONSTANT_VIEW_MAT, current_state->viewmat);
		getMatRow(current_state->inv_viewmat, 1, view_to_world_Y);
		rxbxVertexShaderParameters(device, VS_CONSTANT_VIEW_TO_WORLD_Y, &view_to_world_Y, 1);
		setMatrixPS4x3(device, PS_CONSTANT_INVVIEW_MAT, current_state->inv_viewmat, PS_CONSTANT_BUFFER_MISCPF);
	}
}

void rxbxPushModelMatrix(RdrDeviceDX *device, const Mat4 model_mat, bool is_skinned, bool camera_centered)
{
	Vec4 v;
	CHECKTHREAD;

	mat43to44(model_mat, current_state->modelmat);

	if (is_skinned)
	{
		copyVec3(model_mat[3], v);
		v[3] = 1;
		rxbxVertexShaderParameters(device, VS_CONSTANT_BASEPOSE_OFFSET, &v, 1);
	}
	else
	{
		mulVecMat44(current_state->camera_pos_ws, current_state->viewmat, v);
		v[3] = 1;
		rxbxVertexShaderParameters(device, VS_CONSTANT_CAMERA_POSITION_VS, &v, 1);
	}

	setMatrix(device, VS_CONSTANT_MODEL_MAT, current_state->modelmat);

	if (current_state->width_2d) // If we're in 2d mode, we don't want to use the view matrix
	{
		setMatrix(device, VS_CONSTANT_MODELVIEW_MAT, current_state->modelmat);
	}
	else
	{
		Mat44 modelviewmat44;
		mulMat44Inline(current_state->viewmat, current_state->modelmat, modelviewmat44);
		if (camera_centered)
			setVec3same(modelviewmat44[3], 0);
		setMatrix(device, VS_CONSTANT_MODELVIEW_MAT, modelviewmat44);
	}

	current_state->has_model_matrix_pushed = 1;
}

void rxbxPopModelMatrix(RdrDeviceDX *device)
{
	CHECKTHREAD;

	copyMat44(unitmat44, current_state->modelmat);

	setMatrixUnit(device, VS_CONSTANT_MODEL_MAT);
	setMatrix(device, VS_CONSTANT_MODELVIEW_MAT, current_state->viewmat);

	current_state->has_model_matrix_pushed = 0;
}

float rdrShadowBufferHalfTexelOffset = 0.0f;
AUTO_CMD_FLOAT(rdrShadowBufferHalfTexelOffset, rdrShadowBufferHalfTexelOffset);

void rxbxSetPPTextureSize(
	RdrDeviceDX *device,
	int width, int height,
	float alpha_x, float alpha_y,
	int actual_width, int actual_height,
	F32 offset_x, F32 offset_y,
	bool addHalfPixel2)
{
	int maybe_half_pixel_offset = (device->d3d11_device) ? 0 : 1;
	int manual_half_pixel_offset = addHalfPixel2 ? 1 : 0;

	float halfVal = -(float)(manual_half_pixel_offset) * rdrShadowBufferHalfTexelOffset;

	Vec4 tex_size = {
		width, height,
		(offset_x + halfVal) / actual_width,
		(offset_y + halfVal) / actual_height};

	Vec4 sizeparam;

	// transform from screen coordinates [-0.5, width - 0.5] to texture coordinates [0, 1]
	setVec4(sizeparam, alpha_x * 0.5f, -alpha_y * 0.5f,
		alpha_x * (float)(actual_width + maybe_half_pixel_offset) / (actual_width * 2),
		alpha_y * (float)(actual_height + maybe_half_pixel_offset) / (actual_height * 2));

	rxbxVertexShaderParameters(device, VS_CONSTANT_SCREEN_SIZE, &sizeparam, 1);

	rxbxVertexShaderParameters(device, VS_CONSTANT_PP_TEX_SIZE, &tex_size, 1);
}

void rxbxSetMorphAndVertexLightParams(RdrDeviceDX *device, F32 morph_amt, F32 vlight_multiplier, F32 vlight_offset)
{
	Vec4 morph_vec = {morph_amt, vlight_multiplier, vlight_offset, 0};
	rxbxVertexShaderParameters(device, VS_CONSTANT_MORPH_AND_VLIGHT, &morph_vec, 1);
}

void rxbxSetWindParams(RdrDeviceDX *device, const Vec4 wind_params)
{
	rxbxVertexShaderParameters(device, VS_CONSTANT_WIND_PARAMS, (const Vec4 *)wind_params, 1);
}

void rxbxSetSpecialShaderParameters(RdrDeviceDX *device, int num_vertex_shader_constants, const Vec4 *vertex_shader_constants)
{
	rxbxVertexShaderParameters(device, VS_CONSTANT_SPECIAL_PARAMETERS, vertex_shader_constants, num_vertex_shader_constants);
}

void rxbxSetCylinderTrailParameters(RdrDeviceDX *device, const Vec4 *params, int count)
{
	rxbxVertexShaderParameters(device, VS_CONSTANT_CYLINDER_TRAIL, params, count);
}

void rxbxSetSingleDirectionalLightParameters(RdrDeviceDX *device, RdrLightData *light_data, RdrLightColorType light_color_type)
{
	// I added this check to prevent crashes.  It might be better to handle it earlier and remove the check [RMARR - 9/27/12]
	if (light_data)
		rxbxVertexShaderParameters(device, VS_CONSTANT_VERTEX_ONLY_LIGHT_PARAMS, light_data->single_dir_light[light_color_type].constants, RDR_VERTEX_LIGHT_CONSTANT_COUNT);
}

void rxbxSetAllVertexLightParameters(RdrDeviceDX *device, int lightnum, RdrLightData *light_data, RdrLightColorType light_color_type)
{
	static const Vec4 zerovecs[RDR_VERTEX_LIGHT_CONSTANT_COUNT] = {0};

	if (light_data)
	{
		rxbxVertexShaderParameters(device, VS_CONSTANT_VERTEX_ONLY_LIGHT_PARAMS + lightnum * RDR_VERTEX_LIGHT_CONSTANT_COUNT, 
									light_data->vertex_lighting[light_color_type].constants, RDR_VERTEX_LIGHT_CONSTANT_COUNT);
	}
	else
	{
		rxbxVertexShaderParameters(device, VS_CONSTANT_VERTEX_ONLY_LIGHT_PARAMS + lightnum * RDR_VERTEX_LIGHT_CONSTANT_COUNT, 
									zerovecs, RDR_VERTEX_LIGHT_CONSTANT_COUNT);
	}
}

void rxbxSetWorldTexParams(RdrDeviceDX *device, const Vec4 vecs[2])
{
	rxbxVertexShaderParameters(device, VS_CONSTANT_WORLD_TEX_PARAMS, vecs, 2);
}



//////////////////////////////////////////////////////////////////////////

__forceinline void rxbxEnableStencilTest(RdrDeviceDX *device, int enable)
{
	device->device_state.depth_stencil.stencil_enable = enable;
	current_state->stencil.enabled = enable;
}

void rxbxStencilFunc(RdrDeviceDX *device, D3DCMPFUNC func, S32 ref, U32 mask)
{
#if ENABLE_STATEMANAGEMENT
	if (current_state->stencil.func != func || current_state->stencil.ref != ref || current_state->stencil.mask != mask)
#endif
	{
		U32 stencil_mask = 0;
		if(!rdr_state.disableStencilDepthTexture || !activeDepthIsDepthTexture(device))
			stencil_mask = mask;

		device->device_state.depth_stencil.stencil_func = func;
		device->device_state.depth_stencil.stencil_ref = ref;
		device->device_state.depth_stencil.stencil_read_mask = stencil_mask;
		device->device_state.depth_stencil.stencil_write_mask = stencil_mask;

		current_state->stencil.func = func;
		current_state->stencil.ref = ref;
		current_state->stencil.mask = mask;
	}
}

void rxbxStencilOp(RdrDeviceDX *device, D3DSTENCILOP fail, D3DSTENCILOP zfail, D3DSTENCILOP zpass)
{
#if ENABLE_STATEMANAGEMENT
	if (current_state->stencil.fail != fail || current_state->stencil.zfail != zfail || current_state->stencil.zpass != zpass)
#endif
	{
		device->device_state.depth_stencil.stencil_fail_op = fail;
		device->device_state.depth_stencil.stencil_depth_fail_op = zfail;
		device->device_state.depth_stencil.stencil_pass_op = zpass;

		current_state->stencil.fail = fail;
		current_state->stencil.zfail = zfail;
		current_state->stencil.zpass = zpass;
	}
}

void rxbxStencilFuncHandler(RdrDeviceDX *device, RdrStencilFuncParams * params, WTCmdPacket * packet)
{
	D3DCMPFUNC d3dstencilfunc;
	switch (params->func)
	{
	xcase RPPDEPTHTEST_OFF:
	default:
		d3dstencilfunc = D3DCMP_ALWAYS;

	xcase RPPDEPTHTEST_LEQUAL:
		d3dstencilfunc = D3DCMP_LESSEQUAL;

	xcase RPPDEPTHTEST_LESS:
		d3dstencilfunc = D3DCMP_LESS;

	xcase RPPDEPTHTEST_EQUAL:
		d3dstencilfunc = D3DCMP_EQUAL;

	xcase RPPDEPTHTEST_DEFAULT:
		d3dstencilfunc = D3DZCMP_LE;
	}

	rxbxEnableStencilTest(device, params->enable);
	rxbxStencilFunc(device, d3dstencilfunc, params->ref, params->mask);
}

void rxbxStencilOpHandler(RdrDeviceDX *device, RdrStencilOpParams * params, WTCmdPacket * packet)
{
	D3DSTENCILOP fail = params->fail == RDRSTENCILOP_DEFAULT ? D3DSTENCILOP_KEEP : params->fail;
	D3DSTENCILOP zfail = params->zfail == RDRSTENCILOP_DEFAULT ? D3DSTENCILOP_KEEP : params->zfail;
	D3DSTENCILOP pass = params->pass == RDRSTENCILOP_DEFAULT ? D3DSTENCILOP_KEEP : params->pass;

	rxbxStencilOp(device, fail, zfail, pass);
}

typedef enum RdrStencilBits
{
	RDRSTENCIL_BIT0_MASK  = 1 << 0,
	RDRSTENCIL_BIT1_MASK  = 1 << 1,
} RdrStencilBits;

void rxbxStencilMode(RdrDeviceDX *device, int stencil_mode, int stencil_ref)
{
	switch (stencil_mode)
	{
	case RDRSTENCILMODE_NONE:
		rxbxStencilFunc(device, D3DCMP_ALWAYS, 0, RDRSTENCIL_NO_MASK);
		rxbxStencilOp(device, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP);
		rxbxEnableStencilTest(device, RDRSTENCIL_DISABLE);

	xcase RDRSTENCILMODE_WRITEVALUE:
		rxbxStencilOp(device, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE);
		rxbxStencilFunc(device, D3DCMP_ALWAYS, stencil_ref, RDRSTENCIL_BIT0_MASK);
		rxbxEnableStencilTest(device, RDRSTENCIL_ENABLE);

	xcase RDRSTENCILMODE_MASKEQUAL:
		rxbxStencilOp(device, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP);
		rxbxStencilFunc(device, D3DCMP_EQUAL, 1, RDRSTENCIL_BIT0_MASK);
		rxbxEnableStencilTest(device, RDRSTENCIL_ENABLE);

	xcase RDRSTENCILMODE_MASKNOTEQUAL:
		rxbxStencilOp(device, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP);
		rxbxStencilFunc(device, D3DCMP_NOTEQUAL, 1, RDRSTENCIL_BIT0_MASK);
		rxbxEnableStencilTest(device, RDRSTENCIL_ENABLE);

	xcase RDRSTENCILMODE_WRITE255:
		rxbxStencilOp(device, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE);
		rxbxStencilFunc(device, D3DCMP_ALWAYS, 1, RDRSTENCIL_BIT0_MASK);
		rxbxEnableStencilTest(device, RDRSTENCIL_ENABLE);
	}
}

//////////////////////////////////////////////////////////////////////////

void rxbxDepthWrite(RdrDeviceDX *device, BOOL enabled)
{
	CHECKTHREAD;

	if (current_state->depth_write.stack_depth >= ARRAY_SIZE(current_state->depth_write.stack))
		return;

	current_state->depth_write.stack[current_state->depth_write.stack_depth] = enabled;
	setDepthWrite(device, current_state->depth_write.stack[current_state->depth_write.stack_depth]);
}

void rxbxDepthWritePush(RdrDeviceDX *device, BOOL enabled)
{
	CHECKTHREAD;

	++current_state->depth_write.stack_depth;
	if (current_state->depth_write.stack_depth >= ARRAY_SIZE(current_state->depth_write.stack))
		printf("Depth write stack overflow");

	rxbxDepthWrite(device, enabled);
}

void rxbxDepthWritePop(RdrDeviceDX *device)
{
	CHECKTHREAD;

	if (current_state->depth_write.stack_depth==0)
		printf("Depth write stack underflow");
	else
		--current_state->depth_write.stack_depth;

	rxbxDepthWrite(device, current_state->depth_write.stack[current_state->depth_write.stack_depth]);
}

void rxbxDepthTestPush(RdrDeviceDX *device, DepthTestMode depthMode)
{
	CHECKTHREAD;

	assertmsg(current_state->depth_test.stack_depth < ARRAY_SIZE(current_state->depth_test.stack),"Depth test stack overflow");

	current_state->depth_test.stack[current_state->depth_test.stack_depth] = device->device_state.depth_stencil.depth_func;

	++current_state->depth_test.stack_depth;

	rxbxDepthTest(device, depthMode);
}

void rxbxDepthTestPop(RdrDeviceDX *device)
{
	CHECKTHREAD;

	assertmsg(current_state->depth_test.stack_depth>0,"Depth test stack underflow");

	--current_state->depth_test.stack_depth;

	rxbxDepthTest(device, current_state->depth_test.stack[current_state->depth_test.stack_depth]);
}

//////////////////////////////////////////////////////////////////////////

void rxbxDepthBias(RdrDeviceDX *device, F32 depth_bias, F32 slope_scale_depth_bias)
{
	CHECKTHREAD;

	if (current_state->depth_bias.stack_depth >= ARRAY_SIZE(current_state->depth_bias.stack))
		return;

	current_state->depth_bias.stack[current_state->depth_bias.stack_depth].depth_bias = depth_bias;
	current_state->depth_bias.stack[current_state->depth_bias.stack_depth].slope_scale_depth_bias = slope_scale_depth_bias;
	setDepthBias(device, current_state->depth_bias.stack[current_state->depth_bias.stack_depth].depth_bias, current_state->depth_bias.stack[current_state->depth_bias.stack_depth].slope_scale_depth_bias);
}

void rxbxDepthBiasPush(RdrDeviceDX *device, F32 depth_bias, F32 slope_scale_depth_bias)
{
	CHECKTHREAD;

	++current_state->depth_bias.stack_depth;
	if (current_state->depth_bias.stack_depth >= ARRAY_SIZE(current_state->depth_bias.stack))
		printf("Depth bias stack overflow");

	rxbxDepthBias(device, depth_bias, slope_scale_depth_bias);
}

void rxbxDepthBiasPushAdd(RdrDeviceDX *device, F32 depth_bias, F32 slope_scale_depth_bias)
{
	CHECKTHREAD;

	++current_state->depth_bias.stack_depth;
	if (current_state->depth_bias.stack_depth >= ARRAY_SIZE(current_state->depth_bias.stack))
		printf("Depth bias stack overflow");

	rxbxDepthBias(device, current_state->depth_bias.stack[current_state->depth_bias.stack_depth-1].depth_bias + depth_bias, current_state->depth_bias.stack[current_state->depth_bias.stack_depth-1].slope_scale_depth_bias + slope_scale_depth_bias);
}

void rxbxDepthBiasPop(RdrDeviceDX *device)
{
	CHECKTHREAD;

	if (current_state->depth_bias.stack_depth==0)
		printf("Depth bias stack underflow");
	else
		--current_state->depth_bias.stack_depth;

	rxbxDepthBias(device, current_state->depth_bias.stack[current_state->depth_bias.stack_depth].depth_bias, current_state->depth_bias.stack[current_state->depth_bias.stack_depth].slope_scale_depth_bias);
}


//////////////////////////////////////////////////////////////////////////

void rxbxColorWrite(RdrDeviceDX *device, BOOL enabled)
{
	CHECKTHREAD;

	if (current_state->color_write.stack_depth >= ARRAY_SIZE(current_state->color_write.stack))
		return;

	current_state->color_write.stack[current_state->color_write.stack_depth] = enabled;
	setColorWrite(device, current_state->color_write.stack[current_state->color_write.stack_depth]);
}

void rxbxColorWritePush(RdrDeviceDX *device, BOOL enabled)
{
	CHECKTHREAD;

	++current_state->color_write.stack_depth;
	if (current_state->color_write.stack_depth >= ARRAY_SIZE(current_state->color_write.stack))
		printf("Color write stack overflow");

	rxbxColorWrite(device, enabled);
}

void rxbxColorWritePop(RdrDeviceDX *device)
{
	CHECKTHREAD;

	if (current_state->color_write.stack_depth==0)
		printf("Color write stack underflow");
	else
		--current_state->color_write.stack_depth;

	rxbxColorWrite(device, current_state->color_write.stack[current_state->color_write.stack_depth]);
}

void rxbxColor(RdrDeviceDX *device, const Color color)
{
	Vec4 v;
	colorToVec4(v, color);
	rxbxVertexShaderParameters(device, VS_CONSTANT_COLOR0, &v, 1);
}

void rxbxColorf(RdrDeviceDX *device, const Vec4 color)
{
	rxbxVertexShaderParameters(device, VS_CONSTANT_COLOR0, (const Vec4 *)color, 1);
}

void rxbxInstanceParam(RdrDeviceDX *device, const Vec4 param)
{
	rxbxVertexShaderParameters(device, VS_CONSTANT_GLOBAL_INSTANCE_PARAM, (const Vec4 *)param, 1);
}

//////////////////////////////////////////////////////////////////////////

void rxbxFogMode(RdrDeviceDX *device, RdrSurfaceStateDX *state, U32 bVolumeFog)
{
	bVolumeFog = false; // TODO(DJR)

	if ( state->fog.volume != bVolumeFog )
	{
		state->fog.volume = bVolumeFog;

		if (state == current_state)
		{
			CHECKTHREAD;
			setFog(device, &state->fog.color_stack[state->fog.color_stack_depth], &state->fog.current);
		}
	}
}

void rxbxFogColor(RdrDeviceDX *device, RdrSurfaceStateDX *state, const Vec3 low_fog_color, const Vec3 high_fog_color)
{
	if (state->fog.color_stack_depth >= ARRAY_SIZE(state->fog.color_stack))
		return;

	copyVec3(low_fog_color, state->fog.color_stack[state->fog.color_stack_depth].low_color);
	copyVec3(high_fog_color, state->fog.color_stack[state->fog.color_stack_depth].high_color);
	if (state == current_state)
	{
		CHECKTHREAD;
		setFog(device, &state->fog.color_stack[state->fog.color_stack_depth], &state->fog.current);
	}
}

void rxbxFogColorPush(RdrDeviceDX *device, const Vec3 low_fog_color, const Vec3 high_fog_color)
{
	CHECKTHREAD;

	++current_state->fog.color_stack_depth;
	if (current_state->fog.color_stack_depth >= ARRAY_SIZE(current_state->fog.color_stack))
		printf("Fog color stack overflow");

	rxbxFogColor(device, current_state, low_fog_color, high_fog_color);
}

void rxbxFogColorPop(RdrDeviceDX *device)
{
	CHECKTHREAD;

	if (current_state->fog.color_stack_depth==0)
		printf("Fog color stack underflow");
	else
		--current_state->fog.color_stack_depth;

	rxbxFogColor(device, current_state, current_state->fog.color_stack[current_state->fog.color_stack_depth].low_color, current_state->fog.color_stack[current_state->fog.color_stack_depth].high_color);
}

void rxbxFogRange(RdrDeviceDX *device, RdrSurfaceStateDX *state, 
				  F32 low_near_dist, F32 low_far_dist, F32 low_max_fog,
				  F32 high_near_dist, F32 high_far_dist, F32 high_max_fog,
				  F32 low_height, F32 high_height)
{
	if (!state->fog.on)
	{
		// Fog is off, set the restore parameter
		state->fog.old.low_near_dist = low_near_dist;
		state->fog.old.low_far_dist = low_far_dist;
		state->fog.old.low_max_fog = low_max_fog;
		state->fog.old.high_near_dist = high_near_dist;
		state->fog.old.high_far_dist = high_far_dist;
		state->fog.old.high_max_fog = high_max_fog;
		state->fog.old.low_height = low_height;
		state->fog.old.high_height = high_height;
	}
	else if (	state->fog.old.low_near_dist != low_near_dist ||
				state->fog.old.low_far_dist != low_far_dist ||
				state->fog.old.low_max_fog != low_max_fog ||
				state->fog.old.high_near_dist != high_near_dist ||
				state->fog.old.high_far_dist != high_far_dist ||
				state->fog.old.high_max_fog != high_max_fog ||
				state->fog.old.low_height != low_height ||
				state->fog.old.high_height != high_height)
	{
		state->fog.old.low_near_dist = state->fog.current.low_near_dist = low_near_dist;
		state->fog.old.low_far_dist = state->fog.current.low_far_dist = low_far_dist;
		state->fog.old.low_max_fog = state->fog.current.low_max_fog = low_max_fog;
		state->fog.old.high_near_dist = state->fog.current.high_near_dist = high_near_dist;
		state->fog.old.high_far_dist = state->fog.current.high_far_dist = high_far_dist;
		state->fog.old.high_max_fog = state->fog.current.high_max_fog = high_max_fog;
		state->fog.old.low_height = state->fog.current.low_height = low_height;
		state->fog.old.high_height = state->fog.current.high_height = high_height;
		if (state == current_state)
		{
			CHECKTHREAD;
			setFog(device, &state->fog.color_stack[state->fog.color_stack_depth], &state->fog.current);
		}
	}
}

void rxbxFog(RdrDeviceDX *device, U32 on)
{
	CHECKTHREAD;

	if (current_state->fog.force_off)
		on = 0;

	if (on == current_state->fog.on)
		return;

	if (on)
	{
		if (current_state->fog.old.low_near_dist==-1 && current_state->fog.old.low_far_dist==-1)
		{
			// State has been reset, restore defaults!
			current_state->fog.old.low_near_dist = 1000;
			current_state->fog.old.low_far_dist = 2000;
			current_state->fog.old.low_max_fog = 1;
			current_state->fog.old.high_near_dist = 1000;
			current_state->fog.old.high_far_dist = 2000;
			current_state->fog.old.high_max_fog = 1;
			current_state->fog.old.low_height = 1000;
			current_state->fog.old.high_height = 2000;
		}
		current_state->fog.current = current_state->fog.old;
	}
	else
	{
		current_state->fog.old = current_state->fog.current;
		current_state->fog.current.low_near_dist = 1000000;
		current_state->fog.current.low_far_dist = 1000001;
		current_state->fog.current.low_max_fog = 0;
		current_state->fog.current.high_near_dist = 1000000;
		current_state->fog.current.high_far_dist = 1000001;
		current_state->fog.current.high_max_fog = 0;
		current_state->fog.current.low_height = 0;
		current_state->fog.current.high_height = 1;
	}
	current_state->fog.on = on;
	setFog(device, &current_state->fog.color_stack[current_state->fog.color_stack_depth], &current_state->fog.current);
}

#if _XBOX
void rxbxSetPredicationConstantsAndManagedParams(RdrDeviceDX *device)
{
	RdrSurfaceDX *surface = device->active_surface;

	CHECKTHREAD;

	if (surface && surface->tile_rects)
	{
		int tile_num;
		for (tile_num=0; tile_num<surface->tile_count; tile_num++)
		{
			if (surface->tile_count > 1)
				IDirect3DDevice9_SetPredication(device->d3d_device, D3DPRED_TILE(tile_num));
			setVec4(current_state->inv_screen_params, 1.f/AVOID_DIV_0(current_state->surface_width), 1.f/AVOID_DIV_0(current_state->surface_height), 
				(surface->tile_rects[tile_num].x1+0.5f)/AVOID_DIV_0(current_state->surface_width), (surface->tile_rects[tile_num].y1+0.5f)/AVOID_DIV_0(current_state->surface_height));
			rxbxSetManagedParamsOnlyFromWithinSetPredicationConstants(device);
		}
		if (surface->tile_count > 1)
			IDirect3DDevice9_SetPredication(device->d3d_device, 0);
	}
	else
	{
		setVec4(current_state->inv_screen_params, 1.f/AVOID_DIV_0(current_state->surface_width), 1.f/AVOID_DIV_0(current_state->surface_height), 0.5f/AVOID_DIV_0(current_state->surface_width), 0.5f/AVOID_DIV_0(current_state->surface_height));
		rxbxSetManagedParamsOnlyFromWithinSetPredicationConstants(device);
	}
}
#else
void rxbxSetPredicationConstantsAndManagedParams(RdrDeviceDX *device)
{
	float oow = 1.f/AVOID_DIV_0(current_state->surface_width);
	float ooh = 1.f/AVOID_DIV_0(current_state->surface_height);
	CHECKTHREAD;

    // see getVPos and getVPosNormalized in ps_inc.hlsl
    setVec4(current_state->inv_screen_params, oow, ooh, 0.5f*oow, 0.5f*ooh);
	rxbxSetManagedParamsOnlyFromWithinSetPredicationConstants(device);
}
#endif

void rxbxFogPush(RdrDeviceDX *device, U32 on)
{
	CHECKTHREAD;

	if (current_state->fog.stack_depth >= ARRAY_SIZE(current_state->fog.stack))
		printf("Fog stack overflow");
	else
		current_state->fog.stack[current_state->fog.stack_depth] = current_state->fog.on;
	rxbxFog(device, on);
	current_state->fog.stack_depth++;
}

void rxbxFogPop(RdrDeviceDX *device)
{
	CHECKTHREAD;

	if (current_state->fog.stack_depth==0)
		printf("Fog stack underflow");
	else
		current_state->fog.stack_depth--;
	rxbxFog(device, current_state->fog.stack[current_state->fog.stack_depth]);
}

//////////////////////////////////////////////////////////////////////////

static void rxbxBindTextureFromData(RdrDeviceDX *device, U32 tex_unit, TextureStateType texture_type, RdrTextureDataDX *texture, RdrTexFlags flags)
{
	PERFINFO_AUTO_START_FUNC();
	ANALYSIS_ASSUME(texture_type>=0 && texture_type<TEXTURE_TYPE_COUNT);

	if (texture)
	{
#if !PLATFORM_CONSOLE
		flags &= ~(device->unsupported_tex_flags | rdr_state.disabled_tex_flags);
#else
		flags &= ~rdr_state.disabled_tex_flags;
#endif
		assertValidTexturePointer(device, texture);
		device->device_state.textures[texture_type][tex_unit].texture = texture->texture;
		device->device_state.textures[texture_type][tex_unit].sampler_state.max_anisotropy = texture->anisotropy;
		device->device_state.textures[texture_type][tex_unit].sampler_state.address_u = (flags&RTF_MIRROR_U)?D3DTADDRESS_MIRROR:((flags&RTF_CLAMP_U)?(D3DTADDRESS_CLAMP):D3DTADDRESS_WRAP);
		device->device_state.textures[texture_type][tex_unit].sampler_state.address_v = (flags&RTF_MIRROR_V)?D3DTADDRESS_MIRROR:((flags&RTF_CLAMP_V)?(D3DTADDRESS_CLAMP):D3DTADDRESS_WRAP);
		device->device_state.textures[texture_type][tex_unit].sampler_state.address_w = (flags&RTF_MIRROR_W)?D3DTADDRESS_MIRROR:((flags&RTF_CLAMP_W)?(D3DTADDRESS_CLAMP):D3DTADDRESS_WRAP);
		device->device_state.textures[texture_type][tex_unit].sampler_state.srgb = texture->srgb;

		// Set comparison mode if necessary.
		if(flags & RTF_COMPARISON_LESS_EQUAL) {
			device->device_state.textures[texture_type][tex_unit].sampler_state.comparison_func = D3D11_COMPARISON_LESS_EQUAL;
			device->device_state.textures[texture_type][tex_unit].sampler_state.use_comparison_mode = true;
		} else {
			device->device_state.textures[texture_type][tex_unit].sampler_state.comparison_func = D3D11_COMPARISON_NEVER;
			device->device_state.textures[texture_type][tex_unit].sampler_state.use_comparison_mode = false;
		}

		if ((flags&RTF_MAG_POINT) || texture_type==TEXTURE_VERTEXSHADER)
		{
			assert(texture->anisotropy <= 1); // Only really applies to vertex textures?
			device->device_state.textures[texture_type][tex_unit].sampler_state.mag_filter = D3DTEXF_POINT;
		}
		else
		{
#if PLATFORM_CONSOLE
			device->device_state.textures[texture_type][tex_unit].sampler_state.mag_filter = (texture->anisotropy>1)?D3DTEXF_ANISOTROPIC:D3DTEXF_LINEAR;
#else
			device->device_state.textures[texture_type][tex_unit].sampler_state.mag_filter = D3DTEXF_LINEAR;
#endif
		}

		if ((flags&RTF_MIN_POINT) || texture_type==TEXTURE_VERTEXSHADER)
		{
			device->device_state.textures[texture_type][tex_unit].sampler_state.min_filter = D3DTEXF_POINT;
		}
		else
		{
			if (texture->anisotropy<=1 || RdrTexHasMaxMipLevelFromFlags(flags)) // MaxMipLevel and anisotropy seem to do bad things together!
			{
				device->device_state.textures[texture_type][tex_unit].sampler_state.min_filter = D3DTEXF_LINEAR;
			}
			else
			{
				device->device_state.textures[texture_type][tex_unit].sampler_state.min_filter = D3DTEXF_ANISOTROPIC;
			}
		}

		if (texture->levels_inited > 1)
		{
			device->device_state.textures[texture_type][tex_unit].sampler_state.mip_filter = D3DTEXF_LINEAR;
		}
		else
		{
			device->device_state.textures[texture_type][tex_unit].sampler_state.mip_filter = D3DTEXF_NONE;
		}

		if (texture_type==TEXTURE_VERTEXSHADER)
		{
			device->device_state.textures[texture_type][tex_unit].sampler_state.max_mip_level = 0;
		}
		else
		{
			U32 maxLevel = texture->max_levels - texture->levels_inited;
			MAX1(maxLevel, (U32)RdrTexMaxMipLevelFromFlags(flags));
			device->device_state.textures[texture_type][tex_unit].sampler_state.max_mip_level = maxLevel;
		}
	}
	else
	{
		device->device_state.textures[texture_type][tex_unit].texture.typeless = NULL;
		current_state->textures[texture_type][tex_unit].is_unused = true;
	}
	PERFINFO_AUTO_STOP();
}

void rxbxClearTextureStateOfSurface(RdrDeviceDX *device, RdrSurfaceDX *surface, RdrSurfaceBuffer buffer, int set_index)
{
	RdrTextureStateDX *tex_state_array;
	TextureStateType texture_type = TEXTURE_PIXELSHADER;
	U32 tex_state_array_size = MAX_TEXTURE_UNITS_TOTAL;
	U32 i;
	tex_state_array = current_state->textures[texture_type];
	// if this buffer is already bound somewhere, unbind it
	for (i = 0; i < tex_state_array_size; i++)
	{
		if (
			tex_state_array[i].bound_surface == surface &&
			tex_state_array[i].bound_surface_buffer == buffer &&
			tex_state_array[i].bound_surface_set_index == set_index)
		{
			releaseSurface(i, texture_type);
			rxbxBindTextureFromData(device, i, texture_type, NULL, 0);
			ZeroStructForce(&tex_state_array[i].bound_id);
		}
	}
}

int rxbxConvertSurfaceSetAlias(const RdrDeviceDX *device, const RdrSurfaceDX * surface, int set_index)
{
	if (device->d3d11_device)
	{
		// on DX11, can't bind as texture at the same time as bound as a surface
		if (surface == device->active_surface)
		{
			// if not using a snapshot, automatic resolve into a snapshot
			if (set_index == 0)
				set_index = 1;
		}
		else
		{
			if (surface->params_thread.desired_multisample_level <= 1)
			{
				// MSAA is off, so
				if (set_index == 1)
					// we can use the source surface without snapshot
					set_index = 0;
			}
			else
			{
				// MSAA is on
				if (!surface->use_render_to_texture && set_index == 0)
					// use auto-resolved texture
					set_index = 1;
			}
		}
	}
	else
	{
		if (surface->params_thread.desired_multisample_level <= 1)
		{
			// MSAA is off, so
			if (set_index == 1)
				// we can use the source surface without snapshot
				set_index = 0;
		}
		else
		{
			// MSAA is on
			if (!surface->use_render_to_texture && set_index == 0)
				// use auto-resolved texture
				set_index = 1;
		}
	}
	return set_index;
}

void rxbxBindTextureEx(RdrDeviceDX *device, U32 tex_unit, TexHandle tex_handle, TextureStateType texture_type)
{
	RdrSurfaceBuffer buffer = 0;
	int set_index=-1; // guaranteed to be set before use, but the compiler is whining about it
	RdrSurfaceDX *surface = NULL;
	RdrTexHandle tex = TexHandleToRdrTexHandle(tex_handle);
	RdrTextureStateDX *tex_state_array;
	U32 tex_state_array_size;

	PERFINFO_AUTO_START_FUNC_L2();

	// Set comparison mode.
	if(tex.sampler_flags & RTF_COMPARISON_LESS_EQUAL) {
		device->device_state.textures[texture_type][tex_unit].sampler_state.comparison_func = D3D11_COMPARISON_LESS_EQUAL;
		device->device_state.textures[texture_type][tex_unit].sampler_state.use_comparison_mode = true;
	} else {
		device->device_state.textures[texture_type][tex_unit].sampler_state.comparison_func = D3D11_COMPARISON_NEVER;
		device->device_state.textures[texture_type][tex_unit].sampler_state.use_comparison_mode = false;
	}


	if (texture_type==TEXTURE_VERTEXSHADER) {
		ADD_MISC_COUNT( 1, "BindVertexTexture" );
		tex_state_array_size = MAX_VERTEX_TEXTURE_UNITS_TOTAL;
	} else if (texture_type==TEXTURE_PIXELSHADER) {
		ADD_MISC_COUNT( 1, "BindTexture" );
		tex_state_array_size = MAX_TEXTURE_UNITS_TOTAL;
	} else if (texture_type==TEXTURE_HULLSHADER) {
		ADD_MISC_COUNT( 1, "BindTexture" );
		tex_state_array_size = MAX_HULL_TEXTURE_UNITS_TOTAL;
	} else if (texture_type==TEXTURE_DOMAINSHADER) {
		ADD_MISC_COUNT( 1, "BindTexture" );
		tex_state_array_size = MAX_DOMAIN_TEXTURE_UNITS_TOTAL;
	} else {
		assert(0);
	}

	CHECKTHREAD;

	tex_state_array = current_state->textures[texture_type];

	if (tex_unit >= tex_state_array_size)
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}

#if USE_BLACK_TEX_FOR_UNBOUND
	tex_state_array[tex_unit].is_unused = false;

	if (tex_handle == RDR_NULL_TEXTURE)
		tex = current_state->black_tex_handle;
#else
	tex_state_array[tex_unit].is_unused = tex_handle == RDR_NULL_TEXTURE;
#endif

	if (tex.is_surface)
	{
		surface = (RdrSurfaceDX *)rdrGetSurfaceForTexHandle(tex);
		buffer = tex.surface.buffer;
		set_index = tex.surface.set_index;

#if !_XBOX //we cant do this here on xbox since the surface needs to be resolved before its evicted from the framebuffer
		if (surface && !tex.surface.no_autoresolve)
		{
			set_index = rxbxConvertSurfaceSetAlias(device, surface, set_index);
			if (set_index == 1)
				rxbxUpdateMSAAResolveOrSurfaceCopyTexture(device, surface, 1<<buffer, set_index);
		}
#endif
		//if (set_index == RDRSURFACE_SET_INDEX_DEFAULT)
		//	set_index = surface->d3d_textures_read_index;
		if (!surface)
		{
			if(tex.sampler_flags & RTF_COMPARISON_LESS_EQUAL) {
				tex_handle = device->white_depth_tex_handle;
			} else {
#if USE_BLACK_TEX_FOR_UNBOUND
				tex = current_state->black_tex_handle;
#else
				tex_handle = RDR_NULL_TEXTURE;
			}

			tex = TexHandleToRdrTexHandle(tex_handle);
#endif
		}
#if ENABLE_STATEMANAGEMENT
		else if (surface == tex_state_array[tex_unit].bound_surface &&
				buffer == tex_state_array[tex_unit].bound_surface_buffer &&
				set_index == tex_state_array[tex_unit].bound_surface_set_index &&
				 !!(tex.sampler_flags & RTF_COMPARISON_LESS_EQUAL) == !!(
					 device->device_state.textures[texture_type][tex_unit].sampler_state.comparison_func == D3D11_COMPARISON_LESS_EQUAL &&
					 device->device_state.textures[texture_type][tex_unit].sampler_state.use_comparison_mode))
		{
			PERFINFO_AUTO_STOP_L2();
			return;
		}
#endif
	}

	if (tex_state_array[tex_unit].bound_surface)
	{
		// assert(tex_state_array[tex_unit].bound_id != tex);
		assert(!device->bind_texture_recursive_call);
		releaseSurface(tex_unit, texture_type);
	}

	device->bind_texture_recursive_call = true;

	if (surface)
	{
		U32 i;
		// if this buffer is already bound somewhere else, unbind it
		for (i = 0; i < tex_state_array_size; i++)
		{
			if (i != tex_unit &&
				tex_state_array[i].bound_surface == surface &&
				tex_state_array[i].bound_surface_buffer == buffer &&
				tex_state_array[i].bound_surface_set_index == set_index)
			{
				releaseSurface(i, texture_type);
				rxbxBindTextureFromData(device, i, texture_type, NULL, 0);
				ZeroStructForce(&tex_state_array[i].bound_id);
			}
		}

		// bind the surface
		rxbxBindSurfaceDirect(surface, texture_type==TEXTURE_VERTEXSHADER, tex_unit, buffer, set_index, tex.sampler_flags);
		tex_state_array[tex_unit].bound_surface = surface;
		tex_state_array[tex_unit].bound_surface_buffer = buffer;
		tex_state_array[tex_unit].bound_surface_set_index = set_index;
		device->bind_texture_recursive_call = false;
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	assert(!tex.is_surface);

#if ENABLE_STATEMANAGEMENT
	if (RdrTexHandleToTexHandle(tex_state_array[tex_unit].bound_id) != RdrTexHandleToTexHandle(tex))
#endif
	{
		RdrTextureDataDX *texdata = NULL;
		if (tex.texture.hash_value)
			stashIntFindPointer(device->texture_data, tex.texture.hash_value, &texdata);

		if (texdata && texdata->created_while_dev_lost)
		{
			if (!(device->warnCountBindDevLostTex % 1024))
				memlog_printf(0, "Binding texture created while device was lost Tex 0x%p\n", texdata);
			++device->warnCountBindDevLostTex;
		}

		rxbxBindTextureFromData(device, tex_unit, texture_type, texdata, tex.sampler_flags);

		tex_state_array[tex_unit].bound_id = tex;
	}
	device->bind_texture_recursive_call = false;
	PERFINFO_AUTO_STOP_L2();
}

void rxbxBindTexture(RdrDeviceDX *device, U32 tex_unit, TexHandle tex_handle)
{
	rxbxBindTextureEx(device, tex_unit, tex_handle, TEXTURE_PIXELSHADER);
}

void rxbxBindVertexTexture(RdrDeviceDX *device, U32 tex_unit, TexHandle tex_handle)
{
	rxbxBindTextureEx(device, tex_unit, tex_handle, TEXTURE_VERTEXSHADER);
}

void rxbxBindHullTexture(RdrDeviceDX *device, U32 tex_unit, TexHandle tex_handle)
{
	rxbxBindTextureEx(device, tex_unit, tex_handle, TEXTURE_HULLSHADER);
}

void rxbxBindDomainTexture(RdrDeviceDX *device, U32 tex_unit, TexHandle tex_handle)
{
	rxbxBindTextureEx(device, tex_unit, tex_handle, TEXTURE_DOMAINSHADER);
}

void rxbxBindWhiteTexture(RdrDeviceDX *device, U32 tex_unit)
{
	rxbxBindTexture(device, tex_unit, device->white_tex_handle);
}

void rxbxBindBlackTexture(RdrDeviceDX *device, U32 tex_unit)
{
	rxbxBindTexture(device, tex_unit, device->black_tex_handle);
}

#if 0
void rxbxTexLODBias(RdrDeviceDX *device, U32 tex_unit, F32 newbias)
{
	device->device_state.textures[TEXTURE_PIXELSHADER][tex_unit].sampler_state.mip_lod_bias = MIN(0, newbias);
}
#endif

void rxbxMarkTexturesUnused(RdrDeviceDX *device, TextureStateType texture_type)
{
	U32 i;
	U32 j;
	CHECKTHREAD;
	switch(texture_type) {
	case TEXTURE_PIXELSHADER:
		j = MAX_TEXTURE_UNITS_TOTAL;
		break;
	case TEXTURE_VERTEXSHADER:
		j = MAX_VERTEX_TEXTURE_UNITS_TOTAL;
		break;
	case TEXTURE_HULLSHADER:
		j = MAX_HULL_TEXTURE_UNITS_TOTAL;
		break;
	case TEXTURE_DOMAINSHADER:
		j = MAX_DOMAIN_TEXTURE_UNITS_TOTAL;
		break;				  
	default:
		assert(0);
	}
	for (i = 0; i < j; ++i)
		current_state->textures[texture_type][i].is_unused = true;
}

void rxbxUnbindUnusedTextures(RdrDeviceDX *device, TextureStateType texture_type)
{
	U32 i;
	CHECKTHREAD;
	for (i = 0; i < MAX_TEXTURE_UNITS_TOTAL; ++i)
	{
		if (current_state->textures[texture_type][i].is_unused
#if ENABLE_STATEMANAGEMENT
			&& RdrTexHandleToTexHandle(current_state->textures[texture_type][i].bound_id) != RdrUnboundTextureHandle(current_state)
#endif
			)
		{
			if(current_state->textures[texture_type][i].bound_id.sampler_flags & RTF_COMPARISON_LESS_EQUAL) {
				// Original texture needed comparison sampling, so just use the 1x1 white depth texture here so we still
				// have our sampler comparison mode.
				rxbxBindTextureEx(device, i, device->white_depth_tex_handle, texture_type);
			} else {
				rxbxBindTextureEx(device, i, RDR_NULL_TEXTURE, texture_type);
			}
		}
	}
}

void rxbxUnbindVertexTextures(RdrDeviceDX *device)
{
	U32 i;
	CHECKTHREAD;
	for (i = 0; i < MAX_VERTEX_TEXTURE_UNITS_TOTAL; ++i)
		rxbxBindVertexTexture(device, i, 0);
}

//////////////////////////////////////////////////////////////////////////


int rxbxBindVertexShader(RdrDeviceDX *device, ShaderHandle shader)
{
	RxbxVertexShader *vshader = NULL;
	RdrVertexShaderObj dx_vshader = {NULL};

	stashIntFindPointer(device->vertex_shaders, shader, &vshader);
	SHADER_LOG("Bind Vertex Shader %d 0x%08x\n", shader, vshader);

	if (vshader)
		dx_vshader = vshader->shader;
	device->device_state.active_vertex_shader_wrapper = vshader;

	if(vshader) {
		g_last_bound_vertex_shader = vshader->debug_name;
	} else {
		g_last_bound_vertex_shader = NULL;
	}
	rxbxBindVertexShaderObject(device, dx_vshader);
	return !!dx_vshader.typeless;
}

int rxbxBindHullShader(RdrDeviceDX *device, ShaderHandle shader)
{
	RxbxHullShader *hshader = NULL;
	ID3D11HullShader *dx_hshader = NULL;

	stashIntFindPointer(device->hull_shaders, shader, &hshader);
	SHADER_LOG("Bind Hull Shader %d 0x%08x\n", shader, hshader);

	if (hshader)
		dx_hshader = hshader->shader;
	device->device_state.active_hull_shader_wrapper = hshader;

	if(hshader) {
		g_last_bound_hull_shader = hshader->debug_name;
	} else {
		g_last_bound_hull_shader = NULL;
	}
	rxbxBindHullShaderObject(device, dx_hshader);
	return !!dx_hshader;
}

int rxbxBindDomainShader(RdrDeviceDX *device, ShaderHandle shader)
{
	RxbxDomainShader *dshader = NULL;
	ID3D11DomainShader *dx_dshader = NULL;

	stashIntFindPointer(device->domain_shaders, shader, &dshader);
	SHADER_LOG("Bind Domain Shader %d 0x%08x\n", shader, dshader);

	if (dshader)
		dx_dshader = dshader->shader;
	device->device_state.active_domain_shader_wrapper = dshader;

	if(dshader) {
		g_last_bound_domain_shader = dshader->debug_name;
	} else {
		g_last_bound_domain_shader = NULL;
	}
	rxbxBindDomainShaderObject(device, dx_dshader);
	return !!dx_dshader;
}

void rxbxDirtyShaders(RdrDeviceDX *device)
{
	device->device_state.vertex_shader_driver.typeless = g_dirty_state_ptr;
	device->device_state.pixel_shader_driver.typeless = g_dirty_state_ptr;
	device->device_state.domain_shader_driver = g_dirty_state_ptr;
	device->device_state.hull_shader_driver = g_dirty_state_ptr;
}

void rxbxSetFastParticleSetInfo(RdrDeviceDX *device, RdrDrawableFastParticles *fast_particles)
{
	rxbxVertexShaderParameters(device, VS_CONSTANT_FAST_PARTICLE_INFO, fast_particles->constants, RDR_NUM_FAST_PARTICLE_CONSTANTS);
	rxbxVertexShaderParameters(device, VS_CONSTANT_FAST_PARTICLE_INFO + RDR_NUM_FAST_PARTICLE_CONSTANTS, &fast_particles->time_info, 1);
	rxbxVertexShaderParameters(device, VS_CONSTANT_FAST_PARTICLE_INFO + RDR_NUM_FAST_PARTICLE_CONSTANTS + 1, &fast_particles->scale_info, 1);	
	rxbxVertexShaderParameters(device, VS_CONSTANT_FAST_PARTICLE_INFO + RDR_NUM_FAST_PARTICLE_CONSTANTS + 2, &fast_particles->hsv_info, 1);
	rxbxVertexShaderParameters(device, VS_CONSTANT_FAST_PARTICLE_INFO + RDR_NUM_FAST_PARTICLE_CONSTANTS + 3, &fast_particles->modulate_color, 1);

	rxbxVertexShaderParameters(device, VS_CONSTANT_FAST_PARTICLE_INFO + RDR_NUM_FAST_PARTICLE_CONSTANTS + 4, 
		fast_particles->bone_infos[0], fast_particles->num_bones * 3);
}

void rxbxSetBoneMatricesBatch(RdrDeviceDX *device, RdrDrawableSkinnedModel* draw_skin, bool bCopyScale)
{
#if _XBOX
	int i, const_index;
	D3DVECTOR4 * pConstantsD3D = NULL;
	D3DVECTOR4 * pGPUConstantsWriteCombinedD3D = NULL;
	D3DVECTOR4 * pConstants = NULL;
	D3DVECTOR4 * pGPUConstantsWriteCombined = NULL;
	Vec4 * vs_constants_desired = device->device_state.vs_constants_desired + VS_CONSTANT_BONE_MATRIX_START;
	Vec4 * vs_constants_driver = device->device_state.vs_constants_driver + VS_CONSTANT_BONE_MATRIX_START;
	int bone_const_count = draw_skin->num_bones * 3;
	int cache_line_left;
	U32 cache_lines[ 4 ] = {0, 0, 0, 0};
	uintptr_t base_cache_line = PTR_TO_UINT(draw_skin->skinning_mat_array) / DATACACHE_LINE_SIZE_BYTES;

	bone_const_count = RoundUpToGranularity(bone_const_count, 4);
	IDirect3DDevice9_BeginVertexShaderConstantF4(device->d3d_device, VS_CONSTANT_BONE_MATRIX_START,
		 &pConstantsD3D, &pGPUConstantsWriteCombinedD3D, bone_const_count);

	pConstants = pConstantsD3D;
	pGPUConstantsWriteCombined = pGPUConstantsWriteCombinedD3D;

	ClearFlagStateRange(device->device_state.vs_constants_modified_flags, 
		VS_CONSTANT_BONE_MATRIX_START, bone_const_count);

	if (draw_skin->skinning_mat_indices)
	{
		int prefetch_lines_left = 4;
		for (i = 0; i < draw_skin->num_bones && prefetch_lines_left; ++i)
		{
			const U8 * bone_addr = (const U8*)draw_skin->skinning_mat_array[draw_skin->skinning_mat_indices[i]];
			uintptr_t line = PTR_TO_UINT(bone_addr) / DATACACHE_LINE_SIZE_BYTES - base_cache_line;
			if (!(cache_lines[line / 32] & (1 << (line%32))))
			{
				cache_lines[line / 32] |= 1 << (line%32);
				PREFETCH(bone_addr);
				--prefetch_lines_left;
			}
		}
	}

	cache_line_left = DATACACHE_LINE_SIZE_BYTES;
	for (i = 0; i < draw_skin->num_bones; ++i)
	{
		const D3DVECTOR4 * bonemat = (D3DVECTOR4*)( draw_skin->skinning_mat_array + 
			( draw_skin->skinning_mat_indices ? draw_skin->skinning_mat_indices[i] : i ) );
		D3DVECTOR4 boneMatRow0, boneMatRow1, boneMatRow2;
		if (i+3 < draw_skin->num_bones)
		{
			const D3DVECTOR4 * next_bonemat = (D3DVECTOR4*)( draw_skin->skinning_mat_array + 
				( draw_skin->skinning_mat_indices ? draw_skin->skinning_mat_indices[i+3] : i+3 ) );
			uintptr_t line = PTR_TO_UINT(next_bonemat) / DATACACHE_LINE_SIZE_BYTES - base_cache_line;

			if (!(cache_lines[line / 32] & (1 << (line%32))))
			{
				cache_lines[line / 32] |= 1 << (line%32);
				PREFETCH(bonemat);
			}
		}

		boneMatRow0 = bonemat[0];
		boneMatRow1 = bonemat[1];
		boneMatRow2 = bonemat[2];
		*(D3DVECTOR4*)vs_constants_driver[0] = boneMatRow0;
		*(D3DVECTOR4*)vs_constants_desired[0] = boneMatRow0;
		pConstants[0]					= boneMatRow0;
		pGPUConstantsWriteCombined[0]	= boneMatRow0;

		*(D3DVECTOR4*)vs_constants_driver[1] = boneMatRow1;
		*(D3DVECTOR4*)vs_constants_desired[1] = boneMatRow1;
		pConstants[1]					= boneMatRow1;
		pGPUConstantsWriteCombined[1]	= boneMatRow1;

		*(D3DVECTOR4*)vs_constants_driver[2] = boneMatRow2;
		*(D3DVECTOR4*)vs_constants_desired[2] = boneMatRow2;
		pConstants[2]					= boneMatRow2;
		pGPUConstantsWriteCombined[2]	= boneMatRow2;

		pConstants += 3;
		pGPUConstantsWriteCombined += 3;
		vs_constants_driver += 3;
		vs_constants_desired += 3;
		cache_line_left -= sizeof(D3DVECTOR4) * 3;
	}

	// fill trailing entries with data from the state manager constant table
	const_index = VS_CONSTANT_BONE_MATRIX_START + draw_skin->num_bones * 3;
	bone_const_count += VS_CONSTANT_BONE_MATRIX_START;
	for (; const_index < bone_const_count; ++const_index)
	{
		D3DVECTOR4 constant_value = *(D3DVECTOR4*)vs_constants_driver[0];
		*pConstants = constant_value;
		*pGPUConstantsWriteCombined = constant_value;

		++pConstants;
		++pGPUConstantsWriteCombined;
		++vs_constants_driver;
	}
	IDirect3DDevice9_EndVertexShaderConstantF4(device->d3d_device);
#else
	ClearFlagStateRange(device->device_state.vs_constants_modified_flags, 
		VS_CONSTANT_BONE_MATRIX_START, draw_skin->num_bones * 3);

	if (draw_skin->skinning_mat_indices)
	{
		int i, const_index;
		Vec4 * vs_constants_driver = device->device_state.vs_constants_driver + VS_CONSTANT_BONE_MATRIX_START;
		Vec4 * vs_constants_desired = device->device_state.vs_constants_desired + VS_CONSTANT_BONE_MATRIX_START;
		for (i = 0, const_index = VS_CONSTANT_BONE_MATRIX_START; i < draw_skin->num_bones; ++i, const_index += 3)
		{
			const SkinningMat4 * bonemat = draw_skin->skinning_mat_array + 
				draw_skin->skinning_mat_indices[i];

			vec4Hcpy_dual((Vec4H*)vs_constants_driver, (Vec4H*)vs_constants_desired, (Vec4H*)bonemat, 3);

			vs_constants_driver += 3;
			vs_constants_desired += 3;
		}

		CHECKX(IDirect3DDevice9_SetVertexShaderConstantF(device->d3d_device, VS_CONSTANT_BONE_MATRIX_START,
			(float*)&device->device_state.vs_constants_driver[ VS_CONSTANT_BONE_MATRIX_START ][0], 
			draw_skin->num_bones * 3));
	}
	else
	{
		Vec4 * vs_constants_driver = device->device_state.vs_constants_driver + VS_CONSTANT_BONE_MATRIX_START;
		Vec4 * vs_constants_desired = device->device_state.vs_constants_desired + VS_CONSTANT_BONE_MATRIX_START;
		const SkinningMat4 * bonemat = draw_skin->skinning_mat_array;

		vec4Hcpy_dual((Vec4H*)vs_constants_driver, (Vec4H*)vs_constants_desired, (Vec4H*)bonemat, 3 * draw_skin->num_bones);

		CHECKX(IDirect3DDevice9_SetVertexShaderConstantF(device->d3d_device, VS_CONSTANT_BONE_MATRIX_START,
			draw_skin->skinning_mat_array[0][0], draw_skin->num_bones * 3));
	}
#endif

	INC_STATE_CHANGE(vs_constants, draw_skin->num_bones * 3);
}

void rxbxSetBoneInfo(RdrDeviceDX *device, U32 bone_num, const SkinningMat4 bone_info)
{
	int i;

	//assert(bone_num < 50);
	
	for (i = 0; i < 3; i++)     
	{
		rxbxVertexShaderParameters(device, VS_CONSTANT_BONE_MATRIX_START + bone_num * 3 + i, bone_info + i, 1);
	}
	/*
	if (bCopyScale)
		rxbxVertexShaderParameters(device, VS_CONSTANT_BONE_MATRIX_START + bone_num * 4 + 3, bone_info->scale, 1);
	*/
}

__forceinline static void rxbxDoneWithPixelShaderInternal(RdrDeviceDX *device)
{
	if (device->last_pshader_query)
		rxbxFinishOcclusionQueryDirect(device, device->last_pshader_query);
}

#if _XBOX

static RdrPixelShaderObj reassembleMicrocode(RdrDeviceDX *device, RxbxPixelShader *pshader, const BOOL boolean_constants[16])
{
	RdrPixelShaderObj dx_pshader = {NULL};
	LPD3DXBUFFER compiled = NULL, errormsgs = NULL;
	HRESULT hr;
	char *microcode_text;
	char *microcode_write_ptr;
	int block_start_offset = 0;
	int i;

	PERFINFO_AUTO_START("Reassemble microcode", 1);
	microcode_text = ScratchAlloc(pshader->microcode_text_len);
	microcode_write_ptr = microcode_text;

	// strip out static branch microcode
	for (i = 0; i < eaSize(&pshader->microcode_jumps); ++i)
	{
		MicrocodeJump *jump = pshader->microcode_jumps[i];
		if (jump->src_offset >= block_start_offset)
		{
			char *block_end = pshader->microcode_text + jump->src_offset;
			int bool_index = jump->bool_constant;
			bool_index = bool_index >= 128 ? bool_index - 128 : bool_index;
			assert(bool_index >= 0 && bool_index < 16);

			// copy current microcode block
			if (jump->src_offset > block_start_offset)
			{
				memcpy(microcode_write_ptr, pshader->microcode_text + block_start_offset, jump->src_offset - block_start_offset);
				microcode_write_ptr += jump->src_offset - block_start_offset;
			}

			if (!boolean_constants[bool_index] == jump->inverted)
			{
				// take the jump
				block_start_offset = jump->dst_offset;
			}
			else
			{
				// skip the jump instruction
				block_start_offset = jump->skip_offset;
			}
		}
	}

	// copy final microcode block
	if (pshader->microcode_text_len > block_start_offset)
	{
		memcpy(microcode_write_ptr, pshader->microcode_text + block_start_offset, pshader->microcode_text_len - block_start_offset);
		microcode_write_ptr += pshader->microcode_text_len - block_start_offset;
	}
	else
	{
		*microcode_write_ptr = 0;
	}

	if (rdr_state.writeReassembledShaders)
	{
		static int uid=0;
		char filename[MAX_PATH];
		FILE *f;
		if (uid==0)
			mkdir("devkit:\\assembled");
		sprintf(filename, "devkit:/assembled/assembled_%d.asm", uid++);
		f = fileOpen(filename, "wb");
		if (f)
		{
			fwrite(microcode_text, 1, strlen(microcode_text), f);
			fclose(f);
		}
	}
	hr = D3DXAssembleShader(microcode_text, strlen(microcode_text), NULL, NULL, 0, &compiled, &errormsgs);
	if (!FAILED(hr))
	{
		dx_pshader = rxbxCreateD3DPixelShader(device, pshader, pshader->debug_name, compiled->lpVtbl->GetBufferPointer(compiled), compiled->lpVtbl->GetBufferSize(compiled), false, true, -1, 0);
	}
	else
	{
		// TODO(CD) print error messages
	}

	if (compiled)
		compiled->lpVtbl->Release(compiled);
	if (errormsgs)
		errormsgs->lpVtbl->Release(errormsgs);

	ScratchFree(microcode_text);
	PERFINFO_AUTO_STOP();

	return dx_pshader;
}

#endif

__forceinline static 
void rxbxBindPixelShaderInternal(RdrDeviceDX *device, RxbxPixelShader *pshader, const BOOL boolean_constants[MAX_PS_BOOL_CONSTANTS])
{
	RdrPixelShaderObj dx_pshader = {NULL};
	U32 boolean_bitfield = 0;
	dx_pshader.typeless = SAFE_MEMBER(pshader, shader.typeless);

	if (!boolean_constants)
	{
    	static BOOL zeroed_boolean_constants[MAX_PS_BOOL_CONSTANTS] = {0};
		boolean_constants = zeroed_boolean_constants;
	}
	else
	{
		U32 bit;
		int i;
		for (i = 0, bit = 1; i < MAX_PS_BOOL_CONSTANTS; ++i)
		{
			if (boolean_constants[i])
				boolean_bitfield |= bit;
			bit = bit << 1;
		}
	}

#if _XBOX
	if (pshader && pshader->microcode_text && !rdr_state.disableMicrocodeReassembly)
	{
		if (!stashIntFindPointer(pshader->shader_variations, 1+boolean_bitfield, &dx_pshader))
		{
			dx_pshader = reassembleMicrocode(device, pshader, boolean_constants);
			stashIntAddPointer(pshader->shader_variations, 1+boolean_bitfield, dx_pshader, false);
		}
	}
#endif

	rxbxDoneWithPixelShaderInternal(device);

	if (pshader)
	{
		g_last_bound_pixel_shader = pshader->debug_name;

		if (!pshader->used_shader && dx_pshader.typeless) {
			if (!rdrShaderPreloadSkip(pshader->debug_name) && !rdr_state.echoShaderPreloadLogIgnoreVirgins)
				rdrShaderPreloadLog("Using virgin shader(%p, %s)", pshader, pshader->debug_name);
			PERFINFO_AUTO_START("FirstUseOfANewShader", 1);
			PERFINFO_AUTO_STOP();
			pshader->used_shader = 1;
		} else if (!dx_pshader.typeless) {
			rdrShaderPreloadLog("Trying to use shader still waiting for compile to finish (%p, %s)", pshader, pshader->debug_name);
		}
	} else {
		g_last_bound_pixel_shader = NULL;
	}
	device->device_state.active_pixel_shader_wrapper = pshader;

	device->device_state.pixel_shader = dx_pshader;
	device->device_state.ps_bool_constants = boolean_bitfield;

	if (pshader && rdr_state.do_occlusion_queries) {
		rxbxStartOcclusionQueryDirect(device, pshader, NULL);
	}
}

int rxbxBindPixelShader(RdrDeviceDX *device, ShaderHandle shader, const BOOL boolean_constants[MAX_PS_BOOL_CONSTANTS])
{
	RxbxPixelShader *pshader = NULL;

	stashIntFindPointer(device->pixel_shaders, shader, &pshader);
	if (!pshader && shader > 0)
	{
		rdrShaderPreloadLog("Tried to bind invalid shader handle %d (not yet sent to renderer?)", shader);
		SHADER_LOG("Invalid shader %d\n", shader);
		return 0;
	}

#if 0
	// Limit to one new shader per frame... doesn't help
	if (!pshader->used_shader) {
		static U32 last_virgin_shader=0;
		if (device->frame_count > 10 && device->frame_count == last_virgin_shader) {
			return 0;
		}
		last_virgin_shader = device->frame_count;
	}
#endif

	SHADER_LOG("Bind Pixel Shader %d 0x%08x\n", shader, SAFE_MEMBER(pshader, shader));

	rxbxBindPixelShaderInternal(device, pshader, boolean_constants);

	return (shader == -1) ? 2 : !!device->device_state.pixel_shader.typeless;
}

//////////////////////////////////////////////////////////////////////////

void rxbxBlendFunc(RdrDeviceDX *device, bool blend_enable, D3DBLEND sfactor, D3DBLEND dfactor, D3DBLENDOP op)
{
	CHECKTHREAD;

	current_state->blend_func.stack[current_state->blend_func.stack_idx].blend_enable = blend_enable;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].separate_alpha = false;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor = sfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor = dfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].op = op;

	setBlend(device, blend_enable, sfactor, dfactor, op);
}

void rxbxBlendFuncSeparate(RdrDeviceDX *device, D3DBLEND clr_sfactor, D3DBLEND clr_dfactor, D3DBLENDOP clr_op,
						   D3DBLEND alpha_sfactor, D3DBLEND alpha_dfactor, D3DBLENDOP alpha_op)
{
	CHECKTHREAD;

	current_state->blend_func.stack[current_state->blend_func.stack_idx].blend_enable = true;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].separate_alpha = true;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor = clr_sfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor = clr_dfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].op = clr_op;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].alpha_sfactor = alpha_sfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].alpha_dfactor = alpha_dfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].alpha_op = alpha_op;

	setBlendSeparate(device, clr_sfactor, clr_dfactor, clr_op, alpha_sfactor, alpha_dfactor, alpha_op);
}

void rxbxBlendFuncPush(RdrDeviceDX *device, bool blend_enable, D3DBLEND sfactor, D3DBLEND dfactor, D3DBLENDOP op)
{
	CHECKTHREAD;

	if (current_state->blend_func.stack_frozen)
		return;

	current_state->blend_func.stack_idx++;
	assert(current_state->blend_func.stack_idx < 32);
	current_state->blend_func.stack[current_state->blend_func.stack_idx].blend_enable = blend_enable;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].separate_alpha = false;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor = sfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor = dfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].op = op;

	setBlend(device, blend_enable, sfactor, dfactor, op);
}

void rxbxBlendFuncPushNop(RdrDeviceDX *device)
{
	CHECKTHREAD;

	if (current_state->blend_func.stack_frozen)
		return;

	current_state->blend_func.stack_idx++;
	assert(current_state->blend_func.stack_idx < 32);
	current_state->blend_func.stack[current_state->blend_func.stack_idx] = current_state->blend_func.stack[current_state->blend_func.stack_idx-1];
}

void rxbxBlendFuncPop(RdrDeviceDX *device)
{
	CHECKTHREAD;

	if (current_state->blend_func.stack_frozen)
		return;

	if (current_state->blend_func.stack_idx > 0)
	{
		current_state->blend_func.stack_idx--;

		if (current_state->blend_func.stack[current_state->blend_func.stack_idx].separate_alpha)
			setBlendSeparate(device, current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].op, current_state->blend_func.stack[current_state->blend_func.stack_idx].alpha_sfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].alpha_dfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].alpha_op);
		else
			setBlend(device, current_state->blend_func.stack[current_state->blend_func.stack_idx].blend_enable, current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].op);
	}
}

void rxbxDirtyBlend(RdrDeviceDX *device) 
{
	memset(&device->device_state.blend_driver, 0, sizeof(device->device_state.blend_driver));
}

void rxbxBlendStackFreeze(RdrDeviceDX *device, int freeze)
{
	CHECKTHREAD;
	current_state->blend_func.stack_frozen = freeze;
}

//////////////////////////////////////////////////////////////////////////

void rxbxSetExposureTransform(RdrDeviceDX *device, const Vec4 *exposure_transform, WTCmdPacket *packet)
{
	CHECKTHREAD;
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_EXPOSURE_TRANSFORM, exposure_transform, 1, PS_CONSTANT_BUFFER_SKY);
	rxbxVertexShaderParameters(device, VS_CONSTANT_EXPOSURE_TRANSFORM, exposure_transform, 1);
}

void rxbxSetShadowBufferTexture(RdrDeviceDX *device, TexHandle *tex_ptr, WTCmdPacket *packet)
{
	CHECKTHREAD;
	current_state->shadow_buffer_tex_handle = *tex_ptr;
}

void rxbxSetSoftParticleDepthTexture(RdrDeviceDX *device, TexHandle *tex_ptr, WTCmdPacket *packet)
{
	CHECKTHREAD;
	current_state->soft_particle_depth_tex_handle = *tex_ptr;
}

void rxbxSetSSAODirectIllumFactor(RdrDeviceDX *device, const F32 *illum_factor, WTCmdPacket *packet)
{
	CHECKDEVICELOCK(device);
	device->ssao_direct_illum_factor = *illum_factor;
}

void rxbxBindStereoscopicTexture(RdrDeviceDX *device)
{
	CHECKTHREAD;
	rxbxBindTexture(device, STEREOSCOPIC_TEX_UNIT, device->stereoscopic_tex);
}

void rxbxSetShadowBufferTextureActive(RdrDeviceDX *device, bool active)
{
	CHECKTHREAD;
	if (active)
		rxbxBindTexture(device, SHADOWBUFFER_TEX_UNIT, current_state->shadow_buffer_tex_handle);
	else
		rxbxBindTexture(device, SHADOWBUFFER_TEX_UNIT, device->black_tex_handle);
}

void rxbxSetCubemapLookupTexture(RdrDeviceDX *device, TexHandle *tex_ptr, WTCmdPacket *packet)
{
	CHECKTHREAD;
	current_state->cubemap_lookup_tex_handle = *tex_ptr;
}

void rxbxSetCubemapLookupTextureActive(RdrDeviceDX *device, bool active)
{
	CHECKTHREAD;
	rxbxBindTexture(device, CUBEMAP_TEX_UNIT, active ? current_state->cubemap_lookup_tex_handle : 0L);
}

void rxbxSetupAmbientLight(RdrDeviceDX *device, const RdrAmbientLight *light, const Vec3 ambient_multiplier, const Vec3 ambient_offset, const Vec3 ambient_override, RdrLightColorType light_color_type)
{
	Vec4 temp_vec;
	Vec2 tangent;

	CHECKDEVICELOCK(device);

	if (ambient_override)
		copyVec3(ambient_override, temp_vec);
	else if (light)
		copyVec3(light->ambient[light_color_type], temp_vec);
	else
		setVec3same(temp_vec, 1);
	if (ambient_multiplier)
		mulVecVec3(temp_vec, ambient_multiplier, temp_vec);
	if (ambient_offset)
		addVec3(temp_vec, ambient_offset, temp_vec);
	temp_vec[3] = 0;
	rxbxVertexShaderParameters(device, VS_CONSTANT_AMBIENT_LIGHT, &temp_vec, 1);
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_AMBIENT_LIGHT, &temp_vec, 1, PS_CONSTANT_BUFFER_MISCPF); // Should only do this on non-SM30 cards?

	mulVecMat3(upvec, current_state->viewmat4, temp_vec);
	temp_vec[3] = 0;
	rxbxVertexShaderParameters(device, VS_CONSTANT_SKY_DOME_DIRECTION, &temp_vec, 1);

	setVec2(tangent, temp_vec[1], -temp_vec[0]);
	normalVec2(tangent);
	
	if (light)
		copyVec3(light->sky_light_color_front[light_color_type], temp_vec);
	else
		zeroVec3(temp_vec);
	temp_vec[3] = tangent[0];
	rxbxVertexShaderParameters(device, VS_CONSTANT_SKY_DOME_COLOR_FRONT, &temp_vec, 1);
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_SKY_DOME_COLOR_FRONT, &temp_vec, 1, PS_CONSTANT_BUFFER_SKY);

	if (light)
		copyVec3(light->sky_light_color_back[light_color_type], temp_vec);
	else
		zeroVec3(temp_vec);
	temp_vec[3] = tangent[1];
	rxbxVertexShaderParameters(device, VS_CONSTANT_SKY_DOME_COLOR_BACK, &temp_vec, 1);
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_SKY_DOME_COLOR_BACK, &temp_vec, 1, PS_CONSTANT_BUFFER_SKY);

	if (light)
		copyVec3(light->sky_light_color_side[light_color_type], temp_vec);
	else
		zeroVec3(temp_vec);
	temp_vec[3] = device->ssao_direct_illum_factor; //this is unrelated but we needed to jam it somewhere
	rxbxVertexShaderParameters(device, VS_CONSTANT_SKY_DOME_COLOR_SIDE, &temp_vec, 1);
	rxbxPixelShaderConstantParameters(device, PS_CONSTANT_SKY_DOME_COLOR_SIDE, &temp_vec, 1, PS_CONSTANT_BUFFER_SKY);
}

void rxbxSetVertexStreamSource(RdrDeviceDX *device, int stream, RdrVertexBufferObj source, U32 stride, U32 start_offset)
{
#ifdef _FULLDEBUG
	devassert(stream >= 0 && stream < MAX_VERTEX_STREAMS);
#endif
	device->device_state.vertex_stream[stream].source = source;
	device->device_state.vertex_stream[stream].offset = start_offset;
	assert(device->device_state.vertex_stream[stream].offset == start_offset); // Checking fitting in packed bits
	device->device_state.vertex_stream[stream].stride = stride;
	assert(device->device_state.vertex_stream[stream].stride == stride); // Checking fitting in packed bits
}

void rxbxDirtyStreamSources(RdrDeviceDX *device)
{
	memset(&device->device_state.vertex_stream_driver, 0, sizeof(device->device_state.vertex_stream_driver));
}

void rxbxSetForceFarDepth(RdrDeviceDX *device, bool enable, int depthLevel)
{
#if ENABLE_STATEMANAGEMENT
	if (current_state->force_far_depth != enable)
#endif
	{
		current_state->force_far_depth = enable;

		if (current_state->force_far_depth)
		{
			Mat44 projectionMat;

			if (depthLevel > 1) {
				copyMat44(current_state->sky_projection_mat3d,projectionMat);
			} else {
				copyMat44(current_state->far_depth_projection_mat3d,projectionMat);
			}

			setMatrix(device, VS_CONSTANT_PROJ_MAT, projectionMat);
		}
		else
		{
			setMatrix(device, VS_CONSTANT_PROJ_MAT, current_state->projection_mat3d);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

static void FORCEINLINE rxbxApplyIndexBuffer(RdrDeviceDX *device)
{
 	if (device->d3d11_device)
		ID3D11DeviceContext_IASetIndexBuffer(device->d3d11_imm_context, device->device_state.index_buffer.index_buffer_d3d11, device->device_state.index_buffer_is_32bit?DXGI_FORMAT_R32_UINT:DXGI_FORMAT_R16_UINT, 0);
 	else
		CHECKX(IDirect3DDevice9_SetIndices(device->d3d_device, device->device_state.index_buffer.index_buffer_d3d9));
	device->device_state.index_buffer_driver = device->device_state.index_buffer;
	device->device_state.index_buffer_is_32bit_driver = device->device_state.index_buffer_is_32bit;
	INC_STATE_CHANGE(index_buffer, 1);
}

static void FORCEINLINE rxbxApplyVertexStream(RdrDeviceDX *device, int stream)
{
	if (device->d3d11_device)
	{
		U32 offset = device->device_state.vertex_stream[stream].offset;
		U32 stride = device->device_state.vertex_stream[stream].stride;
		
		ID3D11DeviceContext_IASetVertexBuffers(device->d3d11_imm_context, stream, 1, &device->device_state.vertex_stream[stream].source.vertex_buffer_d3d11, &stride, &offset);
	}
	else
		CHECKX(IDirect3DDevice9_SetStreamSource(device->d3d_device, stream, device->device_state.vertex_stream[stream].source.vertex_buffer_d3d9, device->device_state.vertex_stream[stream].offset, device->device_state.vertex_stream[stream].stride));
	device->device_state.vertex_stream_driver[stream] = device->device_state.vertex_stream[stream];
	INC_STATE_CHANGE(vertex_stream, 1);
}

static void FORCEINLINE rxbxApplyVertexStreamFrequency(RdrDeviceDX *device, int stream)
{
	if (device->d3d11_device)
		return;
	assert(device->device_state.vertex_stream_frequency[stream] != 0); // Should default to 1
	CHECKX(IDirect3DDevice9_SetStreamSourceFreq(device->d3d_device, stream, device->device_state.vertex_stream_frequency[stream]));
	device->device_state.vertex_stream_frequency_driver[stream] = device->device_state.vertex_stream_frequency[stream];
	INC_STATE_CHANGE(vertex_stream_frequency, 1);
}

static const byte identity_slot_map[ MAX_TEXTURE_UNITS_TOTAL ] = 
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
STATIC_ASSERT_MESSAGE(ARRAY_SIZE(identity_slot_map) == MAX_TEXTURE_UNITS_TOTAL, "identity_slot_map must define the right number of entries");

#define D3DTEXUNIT(tex_type, tex_unit) (((tex_type) == TEXTURE_VERTEXSHADER)?((tex_unit)+D3DVERTEXTEXTURESAMPLER0):(tex_unit))
STATE_APPLY_INLINE static void rxbxApplyTexture(RdrDeviceDX *device, int tex_type, int tex_unit, int driver_tex_unit, int d3d_tex_unit)
{
	ANALYSIS_ASSUME(tex_type == TEXTURE_PIXELSHADER || tex_type == TEXTURE_VERTEXSHADER || tex_type == TEXTURE_HULLSHADER || tex_type == TEXTURE_DOMAINSHADER);
	ANALYSIS_ASSUME(tex_unit < MAX_TEXTURE_UNITS_TOTAL);
	// TODO DJR - make this optional after tracking down invalid texture object use
	assertValidRdrTextureObj(device, &device->device_state.textures[tex_type][tex_unit].texture);
	if (device->d3d11_device)
	{
		// DX11TODO: Move this call higher up, make just 1 call when setting state - perhaps after removing texture mapping
		if (tex_type == TEXTURE_PIXELSHADER)
			ID3D11DeviceContext_PSSetShaderResources(device->d3d11_imm_context, driver_tex_unit, 1, &device->device_state.textures[tex_type][tex_unit].texture.texture_view_d3d11);
		else if (tex_type == TEXTURE_VERTEXSHADER)
			ID3D11DeviceContext_VSSetShaderResources(device->d3d11_imm_context, driver_tex_unit, 1, &device->device_state.textures[tex_type][tex_unit].texture.texture_view_d3d11);
		else if (tex_type == TEXTURE_HULLSHADER)
			ID3D11DeviceContext_HSSetShaderResources(device->d3d11_imm_context, driver_tex_unit, 1, &device->device_state.textures[tex_type][tex_unit].texture.texture_view_d3d11);
		else
			ID3D11DeviceContext_DSSetShaderResources(device->d3d11_imm_context, driver_tex_unit, 1, &device->device_state.textures[tex_type][tex_unit].texture.texture_view_d3d11);
	} else {
		CHECKX(IDirect3DDevice9_SetTexture(device->d3d_device, d3d_tex_unit, device->device_state.textures[tex_type][tex_unit].texture.texture_base_d3d9));
	}

	device->device_state.textures_driver[tex_type][driver_tex_unit].texture = device->device_state.textures[tex_type][tex_unit].texture;
	if (tex_unit == TEXTURE_PIXELSHADER)
		INC_STATE_CHANGE(texture, 1);
	else
		INC_STATE_CHANGE(vertex_texture, 1);
}

void rxbxApplyTextureSlot(RdrDeviceDX *device, int tex_type, int tex_unit)
{
	rxbxApplyTexture(device, tex_type, tex_unit, tex_unit, D3DTEXUNIT((tex_type == TEXTURE_VERTEXSHADER ? 1 : 0), tex_unit));
}

void rxbxNotifyTextureFreed(RdrDeviceDX *device, RdrTextureDataDX *tex_data)
{
	int i, j;
    U32 tex_hash_value = tex_data->tex_hash_value;
    RdrTextureObj d3d_texture = tex_data->texture;

	PERFINFO_AUTO_START_FUNC();

	CHECKDEVICELOCK(device);

	for (j=0; j<TEXTURE_TYPE_COUNT; j++)
	{
		for (i = 0; i < ARRAY_SIZE(current_state->textures[j]); ++i)
		{
			if (d3d_texture.typeless && GET_DESIRED_VALUE(textures[j][i].texture.typeless) == d3d_texture.typeless)
			{
				if (device->device_state.textures[j][i].texture.typeless != device->device_state.textures_driver[j][i].texture.typeless)
				{
					memlog_printf(0, "Detected possible leaking state to deleted texture 0x%p\n", d3d_texture.typeless );
				}
				device->device_state.textures[j][i].texture.typeless = NULL;
			}

			if (d3d_texture.typeless && device->device_state.textures_driver[j][i].texture.typeless==d3d_texture.typeless)
			{
				rxbxApplyTexture(device, j, i, i, D3DTEXUNIT(j, i));
			}

			if (current_state && !current_state->textures[j][i].bound_id.is_surface && current_state->textures[j][i].bound_id.texture.hash_value == tex_hash_value)
			{
				ZeroStructForce(&current_state->textures[j][i].bound_id);
				if (current_state->textures[j][i].bound_surface)
					releaseSurface(i, j);
				device->device_state.textures[j][i].texture.typeless = NULL;
				current_state->textures[j][i].is_unused = true;
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

void rxbxNotifyVertexBufferFreed(RdrDeviceDX *device, RdrVertexBufferObj vertex_buffer)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	// this function does not require the device to be locked since it does not touch the current_state global variable

	for (i = 0; i < ARRAY_SIZE(device->device_state.vertex_stream); ++i)
	{
		if (vertex_buffer.typeless_vertex_buffer && device->device_state.vertex_stream[i].source.typeless_vertex_buffer == vertex_buffer.typeless_vertex_buffer)
		{
			device->device_state.vertex_stream[i].source.typeless_vertex_buffer = NULL;
			device->device_state.vertex_stream[i].stride = 0;
			device->device_state.vertex_stream[i].offset = 0;
		}
		if (vertex_buffer.typeless_vertex_buffer && device->device_state.vertex_stream_driver[i].source.typeless_vertex_buffer == vertex_buffer.typeless_vertex_buffer)
		{
			assert(device->device_state.vertex_stream[i].source.typeless_vertex_buffer != vertex_buffer.typeless_vertex_buffer);
			rxbxApplyVertexStream(device, i);
		}
	}

	PERFINFO_AUTO_STOP();
}

void rxbxNotifyIndexBufferFreed(RdrDeviceDX *device, RdrIndexBufferObj index_buffer)
{
	PERFINFO_AUTO_START_FUNC();

	// this function does not require the device to be locked since it does not touch the current_state global variable

	if (index_buffer.typeless_index_buffer)
	{
		if (device->device_state.index_buffer.typeless_index_buffer == index_buffer.typeless_index_buffer)
		{
			device->device_state.index_buffer.typeless_index_buffer = NULL;
		}
		if (device->device_state.index_buffer_driver.typeless_index_buffer == index_buffer.typeless_index_buffer)
		{
			rxbxApplyIndexBuffer(device);
			assert(device->device_state.index_buffer_driver.typeless_index_buffer != index_buffer.typeless_index_buffer);
		}
	}

	PERFINFO_AUTO_STOP();
}

__forceinline static void rxbxForceSetAllVSConstants(RdrDeviceDX *device)
{
	memset(device->device_state.vs_constants_modified_flags, ~0, sizeof(device->device_state.vs_constants_modified_flags));
}

#define D3DSetRenderState(state, v) CHECKX(IDirect3DDevice9_SetRenderState(device->d3d_device, state, v))

static int depthBiasToInt(float depth_bias)
{
	// DX11TODO - after we have shadows somewhat working, caller should specify
	//   as int, and we convert the other way around for DX9?
	// For now, just mapping the values we use in DX9 to some hardcoded values
	if (!depth_bias)
		return 0;
	if (depth_bias == -0.000001f)
		return -1;
	if (depth_bias == -0.000002f) // above plus wireframe offset
		return -2;
	if (depth_bias == -0.0001f)
		return -3;
	if (depth_bias == -0.000101f) // above plus wireframe offset
		return -4;
	if (depth_bias == -0.00010800000f || depth_bias == -0.00010999999f) // not sure where this comes from - DJR: seems to come from rxbxDepthBiasPushAdd offsetting base opaque pass or HDR pass bias
		return -5;
	if (depth_bias <= -1.0f || depth_bias >= 1.0f)
		// assume integer value request when |depth_bias| >= 1
		return round(depth_bias);
	if (depth_bias < 0 && depth_bias >= -1e-4f)
		return -8;

	// 0.1 / range = 1.0e-3f
	// 1 / range = 1.0e-3f / 0.1
	// range = 0.1 / 1.0e-3f
	// range = 0.1 * 1.0e+3f
	// range = 1.0e+2f

	// (0.001, 1) -> near shadow map range 0.1 / [0.1, 100]
	if (depth_bias >= 1.0e-3f && depth_bias <= 1.0f)
		return round(depth_bias/1.0e-3f);
	// [0, 0.001) -> far shadow map range 0.1 / [100, 10000]
	if (depth_bias >= 0.0f && depth_bias <= 1.0e-3f)
		return round(depth_bias/1.0e-5f);

	// (-1, 0) -> (-1000, 1)
	return round(depth_bias * 1000);
}

static ID3D11RasterizerState *createRasterizerState11(RdrDeviceDX *device)
{
	ID3D11RasterizerState *state;
	// Create a persisted key for the hash table
	RdrRasterizerState *newkey;
	D3D11_RASTERIZER_DESC desc = {0};

	newkey = linAlloc(device->d3d11_state_keys, sizeof(*newkey));
	*newkey = device->device_state.rasterizer;
	// Create D3D object
	desc.FillMode = newkey->fill_mode;
	desc.CullMode = newkey->cull_mode;
	desc.FrontCounterClockwise = FALSE;
	desc.DepthBias = depthBiasToInt(newkey->depth_bias);
	desc.DepthBiasClamp = 0.0f;
	desc.SlopeScaledDepthBias = newkey->slope_scale_depth_bias;
	desc.DepthClipEnable = TRUE;
	desc.ScissorEnable = newkey->scissor_test;
	desc.MultisampleEnable = TRUE; // Defaulting on, seems to cause no harm when rendering to a non-MSAA surface
	desc.AntialiasedLineEnable = FALSE;
	verify(SUCCEEDED(ID3D11Device_CreateRasterizerState(device->d3d11_device, &desc, &state)));
#if VERIFY_STATE
	if (do_verify_state) {
		D3D11_RASTERIZER_DESC desc2;
		ID3D11RasterizerState_GetDesc(state, &desc2);
		if (memcmp(&desc, &desc2, sizeof(desc))!=0)
			handleBadState(device, "created rasterizer state didn't match specification");
	}
#endif
	// Save D3D object
	verify(stashAddPointer(device->d3d11_rasterizer_states, newkey, state, false));
	return state;
}

STATE_APPLY_INLINE static void rxbxApplyStateRasterizer(RdrDeviceDX *device)
{
	if (device->d3d11_device)
	{
		ID3D11RasterizerState *state;
		if (!stashFindPointer(device->d3d11_rasterizer_states, &device->device_state.rasterizer, &state))
			state = createRasterizerState11(device);
		ID3D11DeviceContext_RSSetState(device->d3d11_imm_context, state);
	} else {
		D3DSetRenderState(D3DRS_CULLMODE, device->device_state.rasterizer.cull_mode);
		D3DSetRenderState(D3DRS_FILLMODE, device->device_state.rasterizer.fill_mode);
		D3DSetRenderState(D3DRS_DEPTHBIAS, F2DWDepth(device->device_state.rasterizer.depth_bias));
		D3DSetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, F2DWDepth(device->device_state.rasterizer.slope_scale_depth_bias));
		D3DSetRenderState(D3DRS_SCISSORTESTENABLE, device->device_state.rasterizer.scissor_test);
	}

	device->device_state.rasterizer_driver = device->device_state.rasterizer;

	INC_STATE_CHANGE(rasterizer, 1);
}

static bool FORCEINLINE calcEffectiveAlphaBlendEnable(RdrDeviceDX *device, RdrBlendState *blend)
{
	int opaque = blend->src_blend == D3DBLEND_ONE &&
		blend->dest_blend == D3DBLEND_ZERO &&
		blend->blend_op == D3DBLENDOP_ADD;
	int alpha_opaque = !blend->blend_alpha_separate ||
		(blend->src_blend_alpha == D3DBLEND_ONE &&
		blend->dest_blend_alpha == D3DBLEND_ZERO &&
		blend->blend_op_alpha == D3DBLENDOP_ADD);
	bool bBlend = blend->blend_enable && (!opaque || !alpha_opaque) && !blend->alpha_to_coverage_enable;
	if (device->active_surface && rxbxSurfaceIsInited(device->active_surface) && !device->active_surface->supports_post_pixel_ops)
		assertmsg(!bBlend, "Enabling alpha blend on a surface which does not support it.");
	return bBlend;
}

static ID3D11BlendState *createBlendState11(RdrDeviceDX *device)
{
	ID3D11BlendState *state;
	// Create a persisted key for the hash table
	RdrBlendState newkey;
	D3D11_BLEND_DESC desc = {0};

	newkey = device->device_state.blend;
	// Create D3D object
	desc.AlphaToCoverageEnable = newkey.alpha_to_coverage_enable;
	desc.RenderTarget[0].BlendEnable = calcEffectiveAlphaBlendEnable(device, &device->device_state.blend);
	desc.RenderTarget[0].SrcBlend = newkey.src_blend;
	desc.RenderTarget[0].DestBlend = newkey.dest_blend;
	desc.RenderTarget[0].BlendOp = newkey.blend_op;
	if (newkey.blend_alpha_separate)
	{
		desc.RenderTarget[0].SrcBlendAlpha = newkey.src_blend_alpha;
		desc.RenderTarget[0].DestBlendAlpha = newkey.dest_blend_alpha;
		desc.RenderTarget[0].BlendOpAlpha = newkey.blend_op_alpha;
	} else {
		desc.RenderTarget[0].SrcBlendAlpha = newkey.src_blend;
		desc.RenderTarget[0].DestBlendAlpha = newkey.dest_blend;
		desc.RenderTarget[0].BlendOpAlpha = newkey.blend_op;
	}
	desc.RenderTarget[0].RenderTargetWriteMask = newkey.write_mask;
	desc.RenderTarget[1].RenderTargetWriteMask = newkey.write_mask;
	desc.RenderTarget[2].RenderTargetWriteMask = newkey.write_mask;
	desc.RenderTarget[3].RenderTargetWriteMask = newkey.write_mask;

	verify(SUCCEEDED(ID3D11Device_CreateBlendState(device->d3d11_device, &desc, &state)));
	// Save D3D object
	verify(stashIntAddPointer(device->d3d11_blend_states, newkey.comparevalue, state, false));
	return state;
}

STATE_APPLY_INLINE static void rxbxApplyStateBlend(RdrDeviceDX *device)
{
	if (device->d3d11_device)
	{
		ID3D11BlendState *state;
		const static FLOAT blend_factor[4] = {1, 1, 1, 1}; // Not used anywhere
		if (!stashIntFindPointer(device->d3d11_blend_states, device->device_state.blend.comparevalue, &state))
			state = createBlendState11(device);
		ID3D11DeviceContext_OMSetBlendState(device->d3d11_imm_context, state, blend_factor, 0xFFFFFFFF);
	} else {
		D3DSetRenderState(D3DRS_SRCBLEND, device->device_state.blend.src_blend);
		D3DSetRenderState(D3DRS_DESTBLEND, device->device_state.blend.dest_blend);
		D3DSetRenderState(D3DRS_BLENDOP, device->device_state.blend.blend_op);

		D3DSetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, device->device_state.blend.blend_alpha_separate);
		if (device->device_state.blend.blend_alpha_separate)
		{
			D3DSetRenderState(D3DRS_SRCBLENDALPHA, device->device_state.blend.src_blend_alpha);
			D3DSetRenderState(D3DRS_DESTBLENDALPHA, device->device_state.blend.dest_blend_alpha);
			D3DSetRenderState(D3DRS_BLENDOPALPHA, device->device_state.blend.blend_op_alpha);
		}

		D3DSetRenderState(D3DRS_ALPHABLENDENABLE, calcEffectiveAlphaBlendEnable(device, &device->device_state.blend));

		D3DSetRenderState(D3DRS_COLORWRITEENABLE, device->device_state.blend.write_mask);
		if (
#if defined(_FULLDEBUG) && !_XBOX 
			device->rdr_caps_new.supports_independent_write_masks
#else
			true // D3D will just silently fail if we try to set a renderstate that's not available
#endif
			)
		{
			D3DSetRenderState(D3DRS_COLORWRITEENABLE1, device->device_state.blend.write_mask);
			D3DSetRenderState(D3DRS_COLORWRITEENABLE2, device->device_state.blend.write_mask);
			D3DSetRenderState(D3DRS_COLORWRITEENABLE3, device->device_state.blend.write_mask);
		}

		if (device->device_state.blend.alpha_to_coverage_enable != device->device_state.blend_driver.alpha_to_coverage_enable)
		{
			if (device->device_state.blend.alpha_to_coverage_enable)
			{
				if (device->alpha_to_coverage_supported_nv) // And MSAA surface?
					D3DSetRenderState(D3DRS_ADAPTIVETESS_Y, D3DFMT_NVIDIA_ATOC);
				else if (device->alpha_to_coverage_supported_ati)
					D3DSetRenderState(D3DRS_POINTSIZE, ATI_ATOC_ENABLE);
				D3DSetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
			} else {
				if (device->alpha_to_coverage_supported_nv)
					D3DSetRenderState(D3DRS_ADAPTIVETESS_Y, D3DFMT_UNKNOWN);
				else if (device->alpha_to_coverage_supported_ati)
					D3DSetRenderState(D3DRS_POINTSIZE, ATI_ATOC_DISABLE);
				D3DSetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			}
		}
	}

	device->device_state.blend_driver = device->device_state.blend;

	INC_STATE_CHANGE(blend, 1);
}

static ID3D11DepthStencilState *createDepthStencilState11(RdrDeviceDX *device, RdrDepthStencilState hash_key)
{
	ID3D11DepthStencilState *state;
	// Create a persisted key for the hash table
	RdrDepthStencilState *newkey;
	D3D11_DEPTH_STENCIL_DESC desc = {0};

	newkey = linAlloc(device->d3d11_state_keys, sizeof(*newkey));
	*newkey = hash_key;
	// Create D3D object
	desc.DepthEnable = TRUE;
	desc.DepthWriteMask = newkey->depth_write?D3D11_DEPTH_WRITE_MASK_ALL:0;
	desc.DepthFunc = newkey->depth_func;
	desc.StencilEnable = newkey->stencil_enable;
	desc.StencilReadMask = newkey->stencil_read_mask;
	desc.StencilWriteMask = newkey->stencil_write_mask;
	desc.FrontFace.StencilFailOp = newkey->stencil_fail_op;
	desc.FrontFace.StencilDepthFailOp = newkey->stencil_depth_fail_op;
	desc.FrontFace.StencilPassOp = newkey->stencil_pass_op;
	desc.FrontFace.StencilFunc = newkey->stencil_func;
	desc.BackFace = desc.FrontFace;

	verify(SUCCEEDED(ID3D11Device_CreateDepthStencilState(device->d3d11_device, &desc, &state)));
	// Save D3D object
	verify(stashAddPointer(device->d3d11_depth_stencil_states, newkey, state, false));
	return state;
}

STATE_APPLY_INLINE static void rxbxApplyStateDepthStencil(RdrDeviceDX *device)
{
	if (device->d3d11_device)
	{
		ID3D11DepthStencilState *state;
		RdrDepthStencilState hash_key = device->device_state.depth_stencil;
		hash_key.stencil_ref = 0; // Mask out stencil_ref, it is not used to create the state object
		if (!stashFindPointer(device->d3d11_depth_stencil_states, &hash_key, &state))
			state = createDepthStencilState11(device, hash_key);
		ID3D11DeviceContext_OMSetDepthStencilState(device->d3d11_imm_context, state, device->device_state.depth_stencil.stencil_ref);
	} else {
		D3DSetRenderState(D3DRS_ZFUNC, device->device_state.depth_stencil.depth_func);
		D3DSetRenderState(D3DRS_ZWRITEENABLE, device->device_state.depth_stencil.depth_write);
		D3DSetRenderState(D3DRS_STENCILENABLE, device->device_state.depth_stencil.stencil_enable);
		D3DSetRenderState(D3DRS_STENCILMASK, device->device_state.depth_stencil.stencil_read_mask);
		D3DSetRenderState(D3DRS_STENCILWRITEMASK, device->device_state.depth_stencil.stencil_write_mask);
		D3DSetRenderState(D3DRS_STENCILFAIL, device->device_state.depth_stencil.stencil_fail_op);
		D3DSetRenderState(D3DRS_STENCILZFAIL, device->device_state.depth_stencil.stencil_depth_fail_op);
		D3DSetRenderState(D3DRS_STENCILPASS, device->device_state.depth_stencil.stencil_pass_op);
		D3DSetRenderState(D3DRS_STENCILFUNC, device->device_state.depth_stencil.stencil_func);
		D3DSetRenderState(D3DRS_STENCILREF, device->device_state.depth_stencil.stencil_ref);
	}

	device->device_state.depth_stencil_driver = device->device_state.depth_stencil;
	INC_STATE_CHANGE(depth_stencil, 1);
}

#if !PLATFORM_CONSOLE
__forceinline HRESULT IDirect3DDevice9_SetRenderTargetEx(RdrDeviceDX * device, int bufferMRT, RdrSurfaceObj surface)
{
	return IDirect3DDevice9_SetRenderTarget(device->d3d_device, bufferMRT, surface.surface9);
}
#endif

void rxbxApplyTargets(RdrDeviceDX *device)
{
	RdrSurfaceObj * targets = device->device_state.targets;
#if !PLATFORM_CONSOLE
	if (device->d3d11_device)
	{
		ID3D11DeviceContext_OMSetRenderTargets(device->d3d11_imm_context, SBUF_MAXMRT, &targets[0].render_target_view11, targets[SBUF_DEPTH].depth_stencil_view11);
	}
	else
#endif
	{
		RdrSurfaceObj null_surface = { 0 };
		// clear the MRT render targets first so we don't hit the error saying a rendertarget is bound more than once
		if (rdrSupportsFeature(&device->device_base, FEATURE_MRT2))
		{
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 1, null_surface));
		}

		if (rdrSupportsFeature(&device->device_base, FEATURE_MRT4))
		{
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 2, null_surface));
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 3, null_surface));
		}

	#if _XBOX
		CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 0, targets[0]));
	#else
		if (!targets[0].surface9)
		{
			// the PC requires a render target to be bound, so just disable color writes
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 0, device->active_surface->rendertarget[0].d3d_surface));
			rxbxColorWrite(device, FALSE);
		}
		else
		{
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 0, targets[0]));
			rxbxColorWrite(device, TRUE);
		}
	#endif

		if (targets[1].surface9 && rdrSupportsFeature(&device->device_base, FEATURE_MRT2))
		{
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 1, targets[1]));
		}

		if (targets[2].surface9 && rdrSupportsFeature(&device->device_base, FEATURE_MRT4))
		{
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 2, targets[2]));
		}

		if (targets[3].surface9 && rdrSupportsFeature(&device->device_base, FEATURE_MRT4))
		{
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 3, targets[3]));
		}

		CHECKX(IDirect3DDevice9_SetDepthStencilSurface(device->d3d_device, targets[SBUF_DEPTH].surface9));
	}

	memcpy(device->device_state.targets_driver, device->device_state.targets, sizeof(device->device_state.targets_driver));
}

void rxbxDirtyTargets(RdrDeviceDX *device)
{
	memset(device->device_state.targets_driver, 0, sizeof(device->device_state.targets_driver));
}

static void FORCEINLINE rxbxApplyVertexShader(RdrDeviceDX *device)
{
	if (device->d3d11_device)
		ID3D11DeviceContext_VSSetShader(device->d3d11_imm_context, device->device_state.vertex_shader.vertex_shader_d3d11, NULL, 0);
	else
		CHECKX(IDirect3DDevice9_SetVertexShader(device->d3d_device, device->device_state.vertex_shader.vertex_shader_d3d9));
	device->device_state.vertex_shader_driver = device->device_state.vertex_shader;
	INC_STATE_CHANGE(vertex_shader, 1);
}

static void FORCEINLINE rxbxApplyHullShader(RdrDeviceDX *device)
{
	ID3D11DeviceContext_HSSetShader(device->d3d11_imm_context, device->device_state.hull_shader, NULL, 0);
	device->device_state.hull_shader_driver = device->device_state.hull_shader;
	INC_STATE_CHANGE(hull_shader, 1);
}

void rxbxEmptyTessellationShaders(RdrDeviceDX *device)
{
	device->device_state.hull_shader_driver = device->device_state.hull_shader = NULL;
	ID3D11DeviceContext_HSSetShader(device->d3d11_imm_context, device->device_state.hull_shader, NULL, 0);
	INC_STATE_CHANGE(hull_shader, 1);
	device->device_state.domain_shader_driver = device->device_state.domain_shader = NULL;
	ID3D11DeviceContext_DSSetShader(device->d3d11_imm_context, device->device_state.domain_shader, NULL, 0);
	INC_STATE_CHANGE(domain_shader, 1);
}

static void FORCEINLINE rxbxApplyDomainShader(RdrDeviceDX *device)
{
	ID3D11DeviceContext_DSSetShader(device->d3d11_imm_context, device->device_state.domain_shader, NULL, 0);
	device->device_state.domain_shader_driver = device->device_state.domain_shader;
	INC_STATE_CHANGE(domain_shader, 1);
}

static void FORCEINLINE rxbxApplyPixelShader(RdrDeviceDX *device)
{
	if (device->d3d11_device)
		ID3D11DeviceContext_PSSetShader(device->d3d11_imm_context, device->device_state.pixel_shader.pixel_shader_d3d11, NULL, 0);
	else
		CHECKX(IDirect3DDevice9_SetPixelShader(device->d3d_device, device->device_state.pixel_shader.pixel_shader_d3d9));
	device->device_state.pixel_shader_driver = device->device_state.pixel_shader;
	INC_STATE_CHANGE(pixel_shader, 1);
}

static void FORCEINLINE rxbxApplyVertexDeclarationInline(RdrDeviceDX *device)
{
	if (device->d3d11_device)
		ID3D11DeviceContext_IASetInputLayout(device->d3d11_imm_context, device->device_state.vertex_declaration.layout);
	else
		CHECKX(IDirect3DDevice9_SetVertexDeclaration(device->d3d_device, device->device_state.vertex_declaration.decl));
	device->device_state.vertex_declaration_driver = device->device_state.vertex_declaration;
	INC_STATE_CHANGE(vertex_declaration, 1);
}

#define INVALID_FILTER 255
static const U8 d3d11_filter_lookup[3][3] = {
	// min == point
	{
		D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR, INVALID_FILTER
	},
	// min == linear
	{
		D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR, D3D11_FILTER_MIN_MAG_MIP_LINEAR, INVALID_FILTER
	},
	// min == anisotropic
	{
		D3D11_FILTER_ANISOTROPIC,D3D11_FILTER_ANISOTROPIC,D3D11_FILTER_ANISOTROPIC
	}
};

static ID3D11SamplerState *createSamplerState11(RdrDeviceDX *device, RdrSamplerState sampler_state)
{
	ID3D11SamplerState *state;
	// Create a persisted key for the hash table
	RdrSamplerState newkey;
	D3D11_SAMPLER_DESC desc = {0};

	newkey = sampler_state;
	// Create D3D object
	assert(newkey.min_filter >=1 && newkey.min_filter<=3);
	assert(newkey.mag_filter >=1 && newkey.mag_filter<=3);
	desc.Filter = d3d11_filter_lookup[newkey.min_filter-1][newkey.mag_filter-1];
	assert(desc.Filter != INVALID_FILTER);
	desc.AddressU = newkey.address_u;
	desc.AddressV = newkey.address_v;
	desc.AddressW = newkey.address_w;
	desc.MipLODBias = 0.0f;
	if (desc.Filter!=D3D11_FILTER_ANISOTROPIC)
		desc.MaxAnisotropy = 0;
	else
		desc.MaxAnisotropy = (newkey.max_anisotropy==1)?0:newkey.max_anisotropy;

	desc.MinLOD = newkey.max_mip_level;
	desc.MaxLOD = FLT_MAX;

	desc.ComparisonFunc = newkey.comparison_func;
	if(newkey.use_comparison_mode) {
		// This is the only filtering mode that seems to work with comparison sampling (despite the others being present
		// in the documentation) so let's just force it to this.
		desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	}

	FP_NO_EXCEPTIONS_BEGIN;
		verify(SUCCEEDED(ID3D11Device_CreateSamplerState(device->d3d11_device, &desc, &state)));
	FP_NO_EXCEPTIONS_END;

#if VERIFY_STATE
	if (do_verify_state) {
		D3D11_SAMPLER_DESC desc2;
		ID3D11SamplerState_GetDesc(state, &desc2);
		if (memcmp(&desc, &desc2, sizeof(desc))!=0)
			handleBadState(device, "created sampler state didn't match specification");
	}
#endif
	// Save D3D object
	verify(stashIntAddPointer(device->d3d11_sampler_states, newkey.comparevalue, state, false));
	return state;
}

STATE_APPLY_INLINE static void rxbxApplySamplerStates(RdrDeviceDX *device, int tex_type, int tex_unit, int d3d_tex_unit, int count)
{
	int i;
	RdrDeviceTextureStateDX *tex_state = &device->device_state.textures[tex_type][tex_unit];
	ANALYSIS_ASSUME(tex_type == TEXTURE_PIXELSHADER || tex_type == TEXTURE_VERTEXSHADER);
	ANALYSIS_ASSUME(tex_unit < MAX_TEXTURE_UNITS_TOTAL);

	if (device->d3d11_device)
	{
		ID3D11SamplerState **states = alloca(count*sizeof(states[0]));
		for (i=0; i<count; i++, tex_state++)
		{
			if (!stashIntFindPointer(device->d3d11_sampler_states, tex_state->sampler_state.comparevalue, &states[i]))
				states[i] = createSamplerState11(device, tex_state->sampler_state);
		}
		if (tex_type == TEXTURE_PIXELSHADER)
			ID3D11DeviceContext_PSSetSamplers(device->d3d11_imm_context, tex_unit, count, states);
		else if (tex_type == TEXTURE_VERTEXSHADER)
			ID3D11DeviceContext_VSSetSamplers(device->d3d11_imm_context, tex_unit, count, states);
		else if (tex_type == TEXTURE_HULLSHADER)
			ID3D11DeviceContext_HSSetSamplers(device->d3d11_imm_context, tex_unit, count, states);
		else
			ID3D11DeviceContext_DSSetSamplers(device->d3d11_imm_context, tex_unit, count, states);
	} else {
		for (i=0; i<count; i++, d3d_tex_unit++, tex_state++)
		{
#define D3DSetSamplerState(state, value) IDirect3DDevice9_SetSamplerState(device->d3d_device, d3d_tex_unit, (state), (value));
			D3DSetSamplerState(D3DSAMP_ADDRESSU, tex_state->sampler_state.address_u);
			D3DSetSamplerState(D3DSAMP_ADDRESSV, tex_state->sampler_state.address_v);
			D3DSetSamplerState(D3DSAMP_ADDRESSW, tex_state->sampler_state.address_w);
			D3DSetSamplerState(D3DSAMP_MAGFILTER, tex_state->sampler_state.mag_filter);
			D3DSetSamplerState(D3DSAMP_MINFILTER, tex_state->sampler_state.min_filter);
			D3DSetSamplerState(D3DSAMP_MIPFILTER, tex_state->sampler_state.mip_filter);
			//D3DSetSamplerState(D3DSAMP_MIPMAPLODBIAS, *(DWORD*)&tex_state->sampler_state.mip_lod_bias);
			D3DSetSamplerState(D3DSAMP_MAXANISOTROPY, tex_state->sampler_state.max_anisotropy);
			D3DSetSamplerState(D3DSAMP_MAXMIPLEVEL, tex_state->sampler_state.max_mip_level);
			D3DSetSamplerState(D3DSAMP_SRGBTEXTURE, tex_state->sampler_state.srgb);
#undef D3DSetSamplerState
		}
	}

	// TODO: make sampler state array separate from textures and just use one memcpy her
	for (i=0; i<count; i++, tex_unit++)
	{
		device->device_state.textures_driver[tex_type][tex_unit].sampler_state = device->device_state.textures[tex_type][tex_unit].sampler_state;
	}
	if (tex_unit == TEXTURE_PIXELSHADER)
		INC_STATE_CHANGE(texture_sampler_state, 1);
	else if (tex_unit == TEXTURE_VERTEXSHADER)
		INC_STATE_CHANGE(vertex_texture_sampler_state, 1);
	else if (tex_unit == TEXTURE_HULLSHADER)
		INC_STATE_CHANGE(hull_texture_sampler_state, 1);
	else
		INC_STATE_CHANGE(domain_texture_sampler_state, 1);
}


STATE_APPLY_INLINE static void rxbxApplyPixelShaderBooleanConstants(RdrDeviceDX *device)
{
	assert(!device->d3d11_device);

	if (rxbxSupportsFeature((RdrDevice *)device, FEATURE_SM30))
	{
		int i;
		BOOL desired_bool_values[MAX_PS_BOOL_CONSTANTS];
		U32 bit;
		for (i = 0, bit = 1; i < MAX_PS_BOOL_CONSTANTS; ++i)
		{
			if (device->device_state.ps_bool_constants & bit)
				desired_bool_values[i] = TRUE;
			else
				desired_bool_values[i] = FALSE;
			bit = bit << 1;
		}
		CHECKX(IDirect3DDevice9_SetPixelShaderConstantB(device->d3d_device, 0, desired_bool_values, MAX_PS_BOOL_CONSTANTS));
		INC_STATE_CHANGE(ps_bool_constants, MAX_PS_BOOL_CONSTANTS);
	}
	device->device_state.ps_bool_constants_driver = device->device_state.ps_bool_constants;
}

void rxbxApplyVertexDeclaration(RdrDeviceDX *device)
{
	rxbxApplyVertexDeclarationInline(device);
}


// I combined rxbxResetDeviceState and rxbxSetDefaultDeviceState here
// I *think* this still does what we want in all instances, all of which are basically exceptions (device loss, etc)
// But, perhaps we need an option which simply re-applies the state without reseting to default values
void rxbxResetDeviceStateNew(RdrDeviceDX *device)
{
	int i;
	PERFINFO_AUTO_START_FUNC();

	device->device_state.rasterizer.depth_bias = 0.0f;
	device->device_state.rasterizer.slope_scale_depth_bias = 0.0f;
	device->device_state.rasterizer.fill_mode = D3DFILL_SOLID;
	device->device_state.rasterizer.cull_mode = D3DCULL_CCW;
	device->device_state.rasterizer.scissor_test = 1;
	rxbxApplyStateRasterizer(device);

	device->device_state.blend.src_blend = D3DBLEND_SRCALPHA;
	device->device_state.blend.dest_blend = D3DBLEND_INVSRCALPHA;
	device->device_state.blend.blend_op = D3DBLENDOP_ADD;
	device->device_state.blend.blend_alpha_separate = 0;
	device->device_state.blend.src_blend_alpha = D3DBLEND_SRCALPHA;
	device->device_state.blend.dest_blend_alpha = D3DBLEND_INVSRCALPHA;
	device->device_state.blend.blend_op_alpha = D3DBLENDOP_ADD;
	device->device_state.blend.write_mask = 0xf;
	rxbxApplyStateBlend(device);

	device->device_state.depth_stencil.depth_write = 1;
	device->device_state.depth_stencil.depth_func = D3DZCMP_LE;
	device->device_state.depth_stencil.stencil_enable = 0;
	device->device_state.depth_stencil.stencil_read_mask = 0xff;
	device->device_state.depth_stencil.stencil_write_mask = 0xff;

	device->device_state.depth_stencil.stencil_fail_op = D3DSTENCILOP_KEEP;
	device->device_state.depth_stencil.stencil_depth_fail_op = D3DSTENCILOP_KEEP;
	device->device_state.depth_stencil.stencil_pass_op = D3DSTENCILOP_KEEP;
	device->device_state.depth_stencil.stencil_func = D3DCMP_ALWAYS;
	rxbxApplyStateDepthStencil(device);

	device->device_state.vertex_shader.typeless = NULL;
	rxbxApplyVertexShader(device);
	if (device->d3d11_device) {
		device->device_state.hull_shader = NULL;
		rxbxApplyHullShader(device);
		device->device_state.domain_shader = NULL;
		rxbxApplyDomainShader(device);
	}
	device->device_state.pixel_shader.typeless = NULL;
	rxbxApplyPixelShader(device);
	device->device_state.ps_bool_constants = 0;
	if (!device->d3d11_device)
		rxbxApplyPixelShaderBooleanConstants(device);

	device->device_state.vertex_declaration = device->primitive_vertex_declaration;
	rxbxApplyVertexDeclarationInline(device);
	device->device_state.index_buffer.typeless_index_buffer = NULL;
	device->device_state.index_buffer_is_32bit = false;
	rxbxApplyIndexBuffer(device);

	for (i=0; i<MAX_VERTEX_STREAMS; i++)
	{
		device->device_state.vertex_stream_frequency[i] = 1;
		rxbxApplyVertexStreamFrequency(device, i);
		device->device_state.vertex_stream[i].source.typeless_vertex_buffer = NULL;
		device->device_state.vertex_stream[i].stride = 0;
		device->device_state.vertex_stream[i].offset = 0;
		rxbxApplyVertexStream(device, i);
	}

	{
		RdrSamplerState samp_state = {0};
		//samp_state.mip_lod_bias = 0;
		samp_state.address_u = D3DTADDRESS_WRAP;
		samp_state.address_v = D3DTADDRESS_WRAP;
		samp_state.address_w = D3DTADDRESS_WRAP;
		samp_state.mag_filter = D3DTEXF_LINEAR;
		samp_state.min_filter = D3DTEXF_POINT;
		samp_state.mip_filter = D3DTEXF_NONE;
		samp_state.max_anisotropy = 1;
		samp_state.max_mip_level = 0;

		for (i=0; i<MAX_TEXTURE_UNITS_TOTAL; i++)
		{
			device->device_state.textures[TEXTURE_PIXELSHADER][i].sampler_state = samp_state;
			device->device_state.textures[TEXTURE_PIXELSHADER][i].texture.typeless = NULL;
			rxbxApplyTexture(device, TEXTURE_PIXELSHADER, i, i, i);
		}
		rxbxApplySamplerStates(device, TEXTURE_PIXELSHADER, 0, 0, MAX_TEXTURE_UNITS_TOTAL);
		for (i=0; i<MAX_VERTEX_TEXTURE_UNITS_TOTAL; i++)
		{
			device->device_state.textures[TEXTURE_VERTEXSHADER][i].sampler_state = samp_state;
			device->device_state.textures[TEXTURE_VERTEXSHADER][i].texture.typeless = NULL;
			rxbxApplyTexture(device, TEXTURE_VERTEXSHADER, i, i, D3DVERTEXTEXTURESAMPLER0+i);
		}
		rxbxApplySamplerStates(device, TEXTURE_VERTEXSHADER, 0, D3DVERTEXTEXTURESAMPLER0, MAX_VERTEX_TEXTURE_UNITS_TOTAL);
	}

	PERFINFO_AUTO_STOP();
}

void rxbxResetDeviceState(RdrDeviceDX *device)
{
	PERFINFO_AUTO_START_FUNC();

	CHECKDEVICELOCK(device);

	rxbxDoneWithPixelShaderInternal(device);

	rxbxResetDeviceStateNew(device);

	rxbxForceSetAllVSConstants(device);

#if VERIFY_STATE
	if (do_verify_state)
		rxbxCheckStateDirect(device);
#endif

	PERFINFO_AUTO_STOP();
}

#if _XBOX
STATIC_ASSERT(PS_START_VIEWPF_CONSTANTS==0);
#endif

__forceinline static 
void rxbxApplyVertexTextureState(RdrDeviceDX *device, D3DDevice *d3d_device)
{
	int i;
	RdrDeviceTextureStateDX *textures = device->device_state.textures[TEXTURE_VERTEXSHADER];
	RdrDeviceTextureStateDX *textures_driver = device->device_state.textures_driver[TEXTURE_VERTEXSHADER];
	int first_dirty_sampler=-1;
	int dirty_sampler_count=0;
	for (i = 0; i < MAX_VERTEX_TEXTURE_UNITS_TOTAL; ++i, ++textures, ++textures_driver)
	{
		bool bApply = (i==MAX_VERTEX_TEXTURE_UNITS_TOTAL-1);
		if (textures->texture.typeless != textures_driver->texture.typeless)
			rxbxApplyTexture(device, TEXTURE_VERTEXSHADER, i, i, D3DVERTEXTEXTURESAMPLER0+i);

		if (textures->sampler_state.comparevalue != textures_driver->sampler_state.comparevalue)
		{
			if (first_dirty_sampler != -1)
				dirty_sampler_count++;
			else {
				first_dirty_sampler = i;
				dirty_sampler_count = 1;
			}
		} else {
			bApply = true;
		}

		if (bApply && (first_dirty_sampler != -1))
		{
			rxbxApplySamplerStates(device, TEXTURE_VERTEXSHADER, first_dirty_sampler, D3DVERTEXTEXTURESAMPLER0+first_dirty_sampler, dirty_sampler_count);
			first_dirty_sampler = -1;
		}

#define CHECK_RENDER_TARGET_TEXTURE_CONFLICT 0
#if CHECK_RENDER_TARGET_TEXTURE_CONFLICT
		if (current_state->textures[i].bound_surface)
		{
			RdrSurfaceBuffer bound_buf = current_state->textures[i].bound_surface_buffer;
			int bound_surface_set_index = current_state->textures[i].bound_surface_set_index;
			RdrTextureDataDX *texdata = NULL;
			LPDIRECT3DTEXTURE9 bound_texture = current_state->textures[i].bound_surface->rendertarget[bound_buf].d3d_textures[bound_surface_set_index];
			LPDIRECT3DSURFACE9 texture_surface = NULL;

			IDirect3DTexture9_GetSurfaceLevel(bound_texture, 0, &texture_surface);
			IDirect3DSurface9_Release(texture_surface);
			stashIntFindPointer(device->texture_data, current_state->textures[i].bound_id.texture.hash_value, &texdata);

			// check if the texture is from any active render target
			for (j = 0; j <= SBUF_DEPTH; ++j)
			{
				RdrSurfaceDX * activeSurf = device->active_surface;

				if (device->device_state.targets_driver[j])
				{
					if (device->device_state.targets_driver[j] == texture_surface)
					{
						OutputDebugStringf("Rendertarget Texture active both as texture %d 0x%p and surface 0x%p\n",
							i, texdata, device->device_state.targets_driver[j]);
					}
				}
			}
		}
#endif
	}
}

__forceinline static 
	void rxbxApplyDomainTextureState(RdrDeviceDX *device, D3DDevice *d3d_device)
{
	int i;
	RdrDeviceTextureStateDX *textures = device->device_state.textures[TEXTURE_DOMAINSHADER];
	RdrDeviceTextureStateDX *textures_driver = device->device_state.textures_driver[TEXTURE_DOMAINSHADER];
	const byte * texture_resource_slot = identity_slot_map;
	int first_dirty_sampler=-1;
	int dirty_sampler_count=0;

	if (device->d3d11_device && device->device_state.active_domain_shader_wrapper)
		texture_resource_slot = device->device_state.active_domain_shader_wrapper->texture_resource_slot;

	for (i = 0; i < MAX_DOMAIN_TEXTURE_UNITS_TOTAL; ++i, ++textures)
	{
		bool bApply = (i==MAX_DOMAIN_TEXTURE_UNITS_TOTAL-1);
		if (!textures->texture.typeless)
			continue;
		if (textures->texture.typeless != textures_driver[texture_resource_slot[i]].texture.typeless)
			rxbxApplyTexture(device, TEXTURE_DOMAINSHADER, i, texture_resource_slot[i], i);

		if (textures->sampler_state.comparevalue != textures_driver[i].sampler_state.comparevalue)
		{
			if (first_dirty_sampler != -1)
				dirty_sampler_count++;
			else {
				first_dirty_sampler = i;
				dirty_sampler_count = 1;
			}
		} else {
			bApply = true;
		}

		if (bApply && (first_dirty_sampler != -1))
		{
			rxbxApplySamplerStates(device, TEXTURE_DOMAINSHADER, first_dirty_sampler, first_dirty_sampler, dirty_sampler_count);
			first_dirty_sampler = -1;
		}
	}
}

__forceinline static 
	void rxbxApplyPixelTextureState(RdrDeviceDX *device, D3DDevice *d3d_device)
{
	int i;
	RdrDeviceTextureStateDX *textures = device->device_state.textures[TEXTURE_PIXELSHADER];
	RdrDeviceTextureStateDX *textures_driver = device->device_state.textures_driver[TEXTURE_PIXELSHADER];
	const byte * texture_resource_slot = identity_slot_map;
	int first_dirty_sampler=-1;
	int dirty_sampler_count=0;

	if (device->d3d11_device && device->device_state.active_pixel_shader_wrapper)
		texture_resource_slot = device->device_state.active_pixel_shader_wrapper->texture_resource_slot;

	for (i = 0; i < MAX_TEXTURE_UNITS_TOTAL; ++i, ++textures)
	{
		bool bApply = (i==MAX_TEXTURE_UNITS_TOTAL-1);
		if (textures->texture.typeless != textures_driver[texture_resource_slot[i]].texture.typeless)
			rxbxApplyTexture(device, TEXTURE_PIXELSHADER, i, texture_resource_slot[i], i);

		if (textures->sampler_state.comparevalue != textures_driver[i].sampler_state.comparevalue)
		{
			if (first_dirty_sampler != -1)
				dirty_sampler_count++;
			else {
				first_dirty_sampler = i;
				dirty_sampler_count = 1;
			}
		} else {
			bApply = true;
		}

		if (bApply && (first_dirty_sampler != -1))
		{
			rxbxApplySamplerStates(device, TEXTURE_PIXELSHADER, first_dirty_sampler, first_dirty_sampler, dirty_sampler_count);
			first_dirty_sampler = -1;
		}
	}
}

void rxbxApplyPixelTextureStateEx(RdrDeviceDX *device, D3DDevice *d3d_device)
{
	rxbxApplyPixelTextureState(device, d3d_device);
}

#if 0
static Vec4 const_last_vals[MAX_PS_CONSTANTS];
static int const_changes[MAX_PS_CONSTANTS];
static int const_changes2[MAX_PS_CONSTANTS][MAX_PS_CONSTANTS];
void logConstantChanges(const Vec4 *vals, int count)
{
	int i, j;
	int this_changes[MAX_PS_CONSTANTS];

	for (i=0; i<count; i++)
	{
		if (memcmp(vals[i], const_last_vals[i], 16)!=0)
		{
			const_changes[i]++;
			this_changes[i] = 1;
		} else
			this_changes[i] = 0;
	}
	for (i=0; i<count; i++)
	{
		if (!this_changes[i])
			continue;
		for (j=0; j<count; j++)
		{
			if (i==j)
				continue;
			if (!this_changes[j])
				continue;
			const_changes2[i][j]++;
		}
	}
	memcpy(const_last_vals, vals, count*16);
}

AUTO_COMMAND;
void printConstantChanges(void)
{
	int i, j;
	for (i=0; i<MAX_PS_CONSTANTS; i++)
	{
		if (const_changes[i])
			printf("%d\t%d\n", i, const_changes[i]);
	}
	for (i=0; i<MAX_PS_CONSTANTS; i++)
	{
		for (j=0; j<MAX_PS_CONSTANTS; j++)
		{
			printf("%d\t", const_changes2[i][j]);
		}
		printf("\n");
	}
}

#else
#define logConstantChanges(x, y)
#endif

#define TRACK_CONSTANT_STATS 0

static 
void rxbxApplyVSConstants9(RdrDeviceDX *device)
{
	U32 base_constant = 0;
	U32 sequential_constants = 0;
#if TRACK_CONSTANT_STATS
	int min_seq = device->device_base.perf_values.vs_constant_changes.min_seq;
	int max_seq = device->device_base.perf_values.vs_constant_changes.max_seq;
	int avg_seq = device->device_base.perf_values.vs_constant_changes.avg_seq;
	int num_seq = device->device_base.perf_values.vs_constant_changes.num_seq;
#endif
	StateFlagType state_flag_test = 0;
	U32 state_index = 0;
	int constants_changed = 0;
	Vec4 * vs_constants_driver = device->device_state.vs_constants_driver;
	const Vec4 * vs_constants_desired = device->device_state.vs_constants_desired;
	U32 i;

	for (i = 0; i < VS_STATE_FLAG_ENTRY_COUNT; ++i)
	{
		state_flag_test = device->device_state.vs_constants_modified_flags[i];
		state_index = i * NUM_STATE_FLAGS_PER_ENTRY;

		for (; state_flag_test; state_flag_test >>= FLAG_BITS_PER_STATE, ++state_index)
		{
			// use int memory compares since the floats are not yet in registers, 
			// instead of slow float register compares
			if ((state_flag_test & 3) && ((state_flag_test & 2) ||
				!sameVec4InMem(vs_constants_desired[state_index], vs_constants_driver[state_index])))
			{
				if (!sequential_constants)
					base_constant = state_index;
				++sequential_constants;
			}
			else
			if (sequential_constants)
			{
				constants_changed += sequential_constants;
#if TRACK_CONSTANT_STATS
				if (min_seq > sequential_constants)
					min_seq = sequential_constants;
				if (max_seq < sequential_constants)
					max_seq = sequential_constants;
				avg_seq += sequential_constants;
				++num_seq;
#endif

				vec4Hcpy((Vec4H*)vs_constants_driver[base_constant], (const Vec4H*)vs_constants_desired[base_constant], 
					sequential_constants);
				CHECKX(IDirect3DDevice9_SetVertexShaderConstantF(device->d3d_device, base_constant, 
					vs_constants_desired[base_constant], sequential_constants));

				sequential_constants = 0;
			}
		}

		if (sequential_constants && (base_constant + sequential_constants != state_index + NUM_STATE_FLAGS_PER_ENTRY))
		{
			constants_changed += sequential_constants;
#if TRACK_CONSTANT_STATS
			if (min_seq > sequential_constants)
				min_seq = sequential_constants;
			if (max_seq < sequential_constants)
				max_seq = sequential_constants;
			avg_seq += sequential_constants;
			++num_seq;
#endif

			vec4Hcpy((Vec4H*)vs_constants_driver[base_constant], (const Vec4H*)vs_constants_desired[base_constant], 
				sequential_constants);
			CHECKX(IDirect3DDevice9_SetVertexShaderConstantF(device->d3d_device, base_constant, 
				vs_constants_desired[base_constant], sequential_constants));

			sequential_constants = 0;
		}
	}

	if (sequential_constants)
	{
		constants_changed += sequential_constants;
#if TRACK_CONSTANT_STATS
		if (min_seq > sequential_constants)
			min_seq = sequential_constants;
		if (max_seq < sequential_constants)
			max_seq = sequential_constants;
		avg_seq += sequential_constants;
		++num_seq;
#endif

		vec4Hcpy((Vec4H*)vs_constants_driver[base_constant], (const Vec4H*)vs_constants_desired[base_constant], 
			sequential_constants);
		CHECKX(IDirect3DDevice9_SetVertexShaderConstantF(device->d3d_device, base_constant, 
			vs_constants_desired[base_constant], sequential_constants));
	}

	INC_STATE_CHANGE(vs_constants, constants_changed);
#if TRACK_CONSTANT_STATS
	device->device_base.perf_values.vs_constant_changes.min_seq = min_seq;
	device->device_base.perf_values.vs_constant_changes.max_seq = max_seq;
	device->device_base.perf_values.vs_constant_changes.avg_seq = avg_seq;
	device->device_base.perf_values.vs_constant_changes.num_seq = num_seq;
#endif

	// clean flags
	memset(device->device_state.vs_constants_modified_flags, 0, sizeof(device->device_state.vs_constants_modified_flags));
}

static 
void rxbxApplyPSConstants9(RdrDeviceDX *device)
{
	int i;
	int mat_and_light_dirty = device->device_state.ps_constants_dirty[PS_CONSTANT_BUFFER_MATERIAL] + device->device_state.ps_constants_dirty[PS_CONSTANT_BUFFER_LIGHT];
	Vec4 *const ps_constants_driver = device->device_state.ps_constants_driver;
	const Vec4 *const ps_constants_desired = device->device_state.ps_constants_desired;

	logConstantChanges(ps_constants_desired, MAX_PS_CONSTANTS);
	for (i = 0; i < PS_CONSTANT_BUFFER_COUNT; i++) {
		// Material and Light buffer is updated simultaneously since they are inextricably linked.  The Light buffer is automatically skipped because its dirty field is
		// wiped when the material buffer is updated.
		if (device->device_state.ps_constants_dirty[i]) {
			int bufferSize = (i == PS_CONSTANT_BUFFER_MATERIAL?mat_and_light_dirty:psConstantBufferSizes[i]);

			if (i == PS_CONSTANT_BUFFER_MATERIAL) {
				bufferSize = mat_and_light_dirty;
				device->device_state.ps_constants_dirty[PS_CONSTANT_BUFFER_LIGHT] = 0;
			} else {
				bufferSize = device->device_state.ps_constants_dirty[i];
			}
			vec4Hcpy((Vec4H*)ps_constants_driver[psConstantBufferOffsets[i]],
				(const Vec4H*)ps_constants_desired[psConstantBufferOffsets[i]], bufferSize);
			CHECKX(IDirect3DDevice9_SetPixelShaderConstantF(device->d3d_device, psConstantBufferOffsets[i],
				ps_constants_desired[psConstantBufferOffsets[i]], bufferSize));
			device->device_state.ps_constants_dirty[i] = 0;
		}
	}
}

// This does not truly use the index requested as it ignores instances.
// If you are using this and possess the actual index, divide it by SHADER_CONSTANT_FLEXIBLE_BUFFER_INSTANCES first.
static __forceinline
int rxbxPooledConstantBufferGetIndexSize(const U32 index)
{
	return (SHADER_INITIAL_CONSTANT_SIZE << index) * sizeof(Vec4);
}

__forceinline static int rxbxPooledConstantBufferGetIndex(const int bufferSize)
{
	int maxIndex = SHADER_CONSTANT_FLEXIBLE_BUFFER_BUCKETS - 1;
	int minIndex = -1;

#ifdef _FULLDEBUG
	assertmsg(bufferSize <= SHADER_CONSTANT_BUFFER_FLEXIBLE_MAX * sizeof(Vec4), "Shader buffer size being requested is too large.  May need to increase SHADER_CONSTANT_FLEXIBLE_BUFFER_BUCKETS.");
#endif

	while (maxIndex > (minIndex + 1)) {
		int midIndex = (maxIndex + minIndex) / 2;
		if (rxbxPooledConstantBufferGetIndexSize(midIndex) >= bufferSize)
			maxIndex = midIndex;
		else
			minIndex = midIndex;
	}

	return maxIndex * SHADER_CONSTANT_FLEXIBLE_BUFFER_INSTANCES;
}

__forceinline static void rxbxSetupPooledConstantBuffer(const int bufferSize, DX11BufferObj **buffer, DX11BufferPoolObj *bufferList)
{
	int index = rxbxPooledConstantBufferGetIndex(bufferSize);

	if (*buffer)
		((DX11BufferPoolObj*)(*buffer))->used = false;
	while (bufferList[index].used)
		index++;
	*buffer = &bufferList[index].obj;
	bufferList[index].used = true;
}

static __forceinline void rxbxSetupPoolConstantBuffer(RdrDeviceDX *device, int bufferIndex, const int bufferSize)
{
	rxbxSetupPooledConstantBuffer(bufferSize, &device->device_state.d3d11_psconstants[bufferIndex], device->device_state.d3d11_psconstants_buffer_pool);
}

static void rxbxCreateConstantBuffers(RdrDeviceDX *device, DX11BufferObj* bufferObj, const U32 size)
{
	D3D11_BUFFER_DESC desc = {0};

	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.ByteWidth = size * sizeof(Vec4);
	CHECKX(ID3D11Device_CreateBuffer(device->d3d11_device, &desc, NULL, &bufferObj->buffer ));
}

static DX11BufferPoolObj* rxbxCreateBufferPool(RdrDeviceDX *device)
{
	int i,j,k;
	DX11BufferPoolObj* bufferObj = calloc(SHADER_CONSTANT_FLEXIBLE_BUFFER_BUCKETS * SHADER_CONSTANT_FLEXIBLE_BUFFER_INSTANCES, sizeof(DX11BufferPoolObj));
	int poolOffset;

	for (i = SHADER_INITIAL_CONSTANT_SIZE, j = 0, poolOffset = 0;
		j < SHADER_CONSTANT_FLEXIBLE_BUFFER_BUCKETS;
		j++, i <<= 1, poolOffset += SHADER_CONSTANT_FLEXIBLE_BUFFER_INSTANCES)
	{
		for (k = 0; k < SHADER_CONSTANT_FLEXIBLE_BUFFER_INSTANCES; k++)
		{
			int activeOffset = poolOffset + k;
			bufferObj[activeOffset].used = false;
			rxbxCreateConstantBuffers(device, &bufferObj[activeOffset].obj, i);
		}
	}
	return bufferObj;
}

void rxbxInitConstantBuffers(RdrDeviceDX *device)
{
	D3D11_BUFFER_DESC desc = {0};
	U32 i, j, lutIndex;

	for (i = 0, j = 0; i < PS_CONSTANT_BUFFER_COUNT; i++) {
		device->device_state.ps_constants_desired_d3d11[i] = &device->device_state.ps_constants_desired_filled[j];
		j += psConstantBufferSizes[i];

		if (i != PS_CONSTANT_BUFFER_MATERIAL && i != PS_CONSTANT_BUFFER_LIGHT) {
			device->device_state.d3d11_psconstants[i] = calloc(1, sizeof(DX11BufferObj));
			rxbxCreateConstantBuffers(device, device->device_state.d3d11_psconstants[i], psConstantBufferSizes[i]);
		} else {
			device->device_state.d3d11_psconstants[i] = NULL;
		}
	}

	device->device_state.d3d11_debug_psconstants = calloc(1, sizeof(DX11BufferObj));
	rxbxCreateConstantBuffers(device, device->device_state.d3d11_debug_psconstants, PS_CONSTANT_BUFFER_DEBUG_SIZE);

	device->device_state.d3d11_psconstants_buffer_pool = rxbxCreateBufferPool(device);

	lutIndex = 0;
	for (i = 0; i < VS_CONSTANT_BUFFER_COUNT; i++) {
		rxbxCreateConstantBuffers(device, &device->device_state.d3d11_vsconstants[i], vsConstantBufferSizes[i]);
		for (j = 0; j < vsConstantBufferSizes[i]; j++)
		{
			device->device_state.vs_constant_buffer_LUT[lutIndex] = i;
			lutIndex++;
		}
	}

	lutIndex = 0;
	for (i = 0; i < DS_CONSTANT_BUFFER_COUNT; i++) {
		rxbxCreateConstantBuffers(device, &device->device_state.d3d11_dsconstants[i], dsConstantBufferSizes[i]);
		for (j = 0; j < vsConstantBufferSizes[i]; j++)
		{
			device->device_state.ds_constant_buffer_LUT[lutIndex] = i;
			lutIndex++;
		}
	}
}

void rxbxConstantBufferFreePool(DX11BufferPoolObj **pool, U32 poolSize)
{
	U32 i;

	for (i = 0; i < poolSize; i++)
		ID3D11Buffer_Release((*pool)->obj.buffer);
	free(*pool);
	*pool = NULL;
}

void rxbxDestroyConstantBuffers(RdrDeviceDX *device)
{
	U32 i;

	ID3D11Buffer_Release(device->device_state.d3d11_psconstants[PS_CONSTANT_BUFFER_VIEWPF]->buffer);
	free(device->device_state.d3d11_psconstants[PS_CONSTANT_BUFFER_VIEWPF]);
	ID3D11Buffer_Release(device->device_state.d3d11_psconstants[PS_CONSTANT_BUFFER_MISCPF]->buffer);
	free(device->device_state.d3d11_psconstants[PS_CONSTANT_BUFFER_MISCPF]);
	ID3D11Buffer_Release(device->device_state.d3d11_psconstants[PS_CONSTANT_BUFFER_SKY]->buffer);
	free(device->device_state.d3d11_psconstants[PS_CONSTANT_BUFFER_SKY]);
	rxbxConstantBufferFreePool(&device->device_state.d3d11_psconstants_buffer_pool, SHADER_CONSTANT_FLEXIBLE_BUFFER_BUCKETS * SHADER_CONSTANT_FLEXIBLE_BUFFER_INSTANCES);

	for (i = 0; i < VS_CONSTANT_BUFFER_COUNT; i++) {
		ID3D11Buffer_Release(device->device_state.d3d11_vsconstants[i].buffer);
	}

	for (i = 0; i < DS_CONSTANT_BUFFER_COUNT; i++) {
		ID3D11Buffer_Release(device->device_state.d3d11_dsconstants[i].buffer);
	}

	ID3D11Buffer_Release(device->device_state.d3d11_debug_psconstants->buffer);
	free(device->device_state.d3d11_debug_psconstants);
}

__forceinline static 
void rxbxApplyVSConstants11(RdrDeviceDX *device)
{
	int i, startRegister = 0;

	for (i = 0; i < VS_CONSTANT_BUFFER_COUNT; i ++)
	{
		// Update the buffer
		if (device->device_state.vs_constants_dirty[i]) {
			device->device_state.vs_constants_dirty[i] = false;
			ID3D11DeviceContext_UpdateSubresource(device->d3d11_imm_context, device->device_state.d3d11_vsconstants[i].resource, 0, NULL,
				&device->device_state.vs_constants_desired[startRegister], 0, 0);

			ID3D11DeviceContext_VSSetConstantBuffers(device->d3d11_imm_context, i, 1, &device->device_state.d3d11_vsconstants[i].buffer);
		}
		startRegister += vsConstantBufferSizes[i];
	}
}

__forceinline static 
	void rxbxApplyDSConstants(RdrDeviceDX *device)
{
	int i, startRegister = 0;

	for (i = 0; i < DS_CONSTANT_BUFFER_COUNT; i ++)
	{
		// Update the buffer
		if (device->device_state.ds_constants_dirty[i]) {
			device->device_state.ds_constants_dirty[i] = false;
			ID3D11DeviceContext_UpdateSubresource(device->d3d11_imm_context, device->device_state.d3d11_dsconstants[i].resource, 0, NULL,
				&device->device_state.ds_constants_desired[startRegister], 0, 0);

			ID3D11DeviceContext_DSSetConstantBuffers(device->d3d11_imm_context, i, 1, &device->device_state.d3d11_dsconstants[i].buffer);
			if (i == 0)	// Hull Shader uses the same projection matrices as the domain shader.
				ID3D11DeviceContext_HSSetConstantBuffers(device->d3d11_imm_context, i, 1, &device->device_state.d3d11_dsconstants[i].buffer);
		}
		startRegister += dsConstantBufferSizes[i];
	}
}

static __forceinline void rxbxUpdateD3D11Resource(RdrDeviceDX *device, DX11BufferObj *bufferObj, const Vec4 *desiredConstants, const int reg_num)
{
	ID3D11DeviceContext_UpdateSubresource(device->d3d11_imm_context, bufferObj->resource, 0, NULL,
		desiredConstants, 0, 0);
}

//__forceinline static 
void rxbxApplyPSConstants11(RdrDeviceDX *device)
{
	static ID3D11Buffer **psBufferSet = NULL;
	int i;

	if (!psBufferSet) {
		psBufferSet = calloc(PS_CONSTANT_BUFFER_COUNT, sizeof(ID3D11Buffer*));
	}
	logConstantChanges(device->device_state.ps_constants_desired, MAX_PS_CONSTANTS);

	for (i = 0; i < PS_CONSTANT_BUFFER_COUNT; i++)
	{
		if (device->device_state.active_pixel_shader_wrapper && device->device_state.active_pixel_shader_wrapper->buffer_sizes[i])
		{
			if (device->device_state.ps_constants_dirty[i])
			{
				if (i == PS_CONSTANT_BUFFER_MATERIAL || i == PS_CONSTANT_BUFFER_LIGHT)
					rxbxSetupPoolConstantBuffer(device,i,device->device_state.active_pixel_shader_wrapper->buffer_sizes[i]);
				rxbxUpdateD3D11Resource(device, device->device_state.d3d11_psconstants[i], device->device_state.ps_constants_desired_d3d11[i], i);
				device->device_state.ps_constants_dirty[i] = 0;
			}
			psBufferSet[i] = device->device_state.d3d11_psconstants[i]?device->device_state.d3d11_psconstants[i]->buffer:NULL;
		}
		else
			if (i == PS_CONSTANT_BUFFER_MATERIAL || i == PS_CONSTANT_BUFFER_LIGHT)
				psBufferSet[i] = NULL;
	}
	ID3D11DeviceContext_PSSetConstantBuffers(device->d3d11_imm_context, 0, PS_CONSTANT_BUFFER_COUNT, psBufferSet);
}

void rxbxApplyDebugBuffer(RdrDeviceDX *device, Vec4 *debug_constants)
{
	if (device->d3d11_device)
	{
		rxbxUpdateD3D11Resource(device, device->device_state.d3d11_debug_psconstants, debug_constants, PS_CONSTANT_BUFFER_DEBUG);
		ID3D11DeviceContext_PSSetConstantBuffers(device->d3d11_imm_context, PS_CONSTANT_BUFFER_DEBUG, 1, &device->device_state.d3d11_debug_psconstants->buffer);
	}
}


// when adding states to this function, they must also be added to rxbxResetDeviceState
void rxbxApplyStatePreDraw(RdrDeviceDX *device)
{
	int i;
    int shaders_changed = 0;

	PERFINFO_AUTO_START_FUNC();

#if VERIFY_STATE
	if (do_verify_state)
		rxbxCheckStateDirect(device);
#endif

#if ENABLE_STATEMANAGEMENT
#	define SM(cond) (cond)
#else
#	define SM(cond) true
#endif
	CHECKDEVICELOCK(device);

	// New state management
	if (SM(device->device_state.rasterizer.comparevalue[0] != device->device_state.rasterizer_driver.comparevalue[0] ||
		device->device_state.rasterizer.comparevalue[1] != device->device_state.rasterizer_driver.comparevalue[1]))
		rxbxApplyStateRasterizer(device);
	if (SM(device->device_state.blend.comparevalue != device->device_state.blend_driver.comparevalue))
		rxbxApplyStateBlend(device);
#if VERIFY_STATE
	if (do_verify_state)
		if (device->active_surface)
			assert(device->active_surface->supports_post_pixel_ops || !device->device_state.blend.blend_enable);
#endif
	if (SM(device->device_state.depth_stencil.comparevalue != device->device_state.depth_stencil_driver.comparevalue))
		rxbxApplyStateDepthStencil(device);
	if (SM(device->device_state.vertex_shader.typeless != device->device_state.vertex_shader_driver.typeless))
	{
		rxbxApplyVertexShader(device);
		shaders_changed = 1;
	}
	if (SM(device->device_state.hull_shader != device->device_state.hull_shader_driver))
	{
		rxbxApplyHullShader(device);
		shaders_changed = 1;
	}
	if (SM(device->device_state.domain_shader != device->device_state.domain_shader_driver))
	{
		rxbxApplyDomainShader(device);
		shaders_changed = 1;
	}
	if (SM(device->device_state.pixel_shader.typeless != device->device_state.pixel_shader_driver.typeless))
	{
		rxbxApplyPixelShader(device);
		shaders_changed = 1;
	}
	if (!device->d3d11_device)
		if (SM(device->device_state.ps_bool_constants != device->device_state.ps_bool_constants_driver))
			rxbxApplyPixelShaderBooleanConstants(device);
	if (SM(device->device_state.vertex_declaration.decl != device->device_state.vertex_declaration_driver.decl))
		rxbxApplyVertexDeclarationInline(device);
	if (SM(device->device_state.index_buffer.typeless_index_buffer != device->device_state.index_buffer_driver.typeless_index_buffer ||
		device->device_state.index_buffer_is_32bit != device->device_state.index_buffer_is_32bit_driver))
		rxbxApplyIndexBuffer(device);
	for (i = 0; i < MAX_VERTEX_STREAMS; ++i)
	{
#ifdef _M_X64
		if (SM(device->device_state.vertex_stream[i].comparevalue[0] != device->device_state.vertex_stream_driver[i].comparevalue[0] ||
			device->device_state.vertex_stream[i].comparevalue[1] != device->device_state.vertex_stream_driver[i].comparevalue[1]))
			rxbxApplyVertexStream(device, i);
#else
		if (SM(device->device_state.vertex_stream[i].comparevalue != device->device_state.vertex_stream_driver[i].comparevalue))
			rxbxApplyVertexStream(device, i);
#endif
		if (SM(device->device_state.vertex_stream_frequency[i] != device->device_state.vertex_stream_frequency_driver[i]))
			rxbxApplyVertexStreamFrequency(device, i);
	}

	// Old state management

	if(rxbxShouldApplyTexturesAndStatesTogether() && device->d3d11_device) {
		rxbxApplyTargetsAndTextures(device);
	} else {
		rxbxApplyPixelTextureState(device, device->d3d_device);
	}

	rxbxApplyVertexTextureState(device, device->d3d_device);

	if (device->d3d11_device)
	{
		//rxbxApplyHullTextureState(device, device->d3d_device);
		rxbxApplyDomainTextureState(device, device->d3d_device);

		rxbxApplyVSConstants11(device);
		rxbxApplyPSConstants11(device);
		//rxbxApplyHSConstants(device);
		rxbxApplyDSConstants(device);
	} else {
		rxbxApplyVSConstants9(device);
		rxbxApplyPSConstants9(device);
	}

#if _DEBUG && !_XBOX
    if (!device->d3d11_device && (shaders_changed & 0x30))
	{
        short wideEventNameBuffer[256];

        if(g_last_bound_vertex_shader)
	        UTF8ToWideStrConvert(g_last_bound_vertex_shader, wideEventNameBuffer, ARRAY_SIZE(wideEventNameBuffer));
        else
            wideEventNameBuffer[0]='V',wideEventNameBuffer[1]='S',wideEventNameBuffer[2]=0;

        D3DPERF_SetMarker(D3DCOLOR_ARGB(0xff,0xff,0x00,0x00), wideEventNameBuffer);

        if(g_last_bound_pixel_shader)
	        UTF8ToWideStrConvert(g_last_bound_pixel_shader, wideEventNameBuffer, ARRAY_SIZE(wideEventNameBuffer));
        else
            wideEventNameBuffer[0]='P',wideEventNameBuffer[1]='S',wideEventNameBuffer[2]=0;

        D3DPERF_SetMarker(D3DCOLOR_ARGB(0xff,0x00,0x00,0xff), wideEventNameBuffer);
    }
#endif

	//We need to look at the auto_resolve_disable_mask to make sure we dont trigger unwanted resolves
	if (device->device_state.blend.write_mask && device->active_surface)
	{
		device->active_surface->draw_calls_since_resolve |= (MASK_SBUF_ALL_COLOR & (~device->active_surface->auto_resolve_disable_mask));
	}

	if (device->device_state.depth_stencil.depth_write && (device->override_depth_surface || device->active_surface))
	{
		RdrSurfaceDX* surf = device->override_depth_surface ? device->override_depth_surface : device->active_surface;
		surf->draw_calls_since_resolve |= (MASK_SBUF_DEPTH & (~surf->auto_resolve_disable_mask));
	}

#if VERIFY_STATE
	if (do_verify_state)
		rxbxCheckStateDirect(device);
#endif

	PERFINFO_AUTO_STOP();
}

void handleBadState(RdrDeviceDX *device, const char *check)
{
	// Put a breakpoint here, or change to an assert as required
	printf("Bad state: %s\n", check);
}


void rxbxCheckStateDirect(RdrDeviceDX *device)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	CHECKDEVICELOCK(device);

#define CHECK_STATE(b) if (!(b)) handleBadState(device, #b);

	PERFINFO_AUTO_START("Vertex streams",1);
	// vertices
	{
		for (i = 0; i < MAX_VERTEX_STREAMS; ++i)
		{
			if (1)
			{
				RdrVertexBufferObj v1;
				UINT v2, v3;
				if (device->d3d11_device)
				{
					ID3D11DeviceContext_IAGetVertexBuffers(device->d3d11_imm_context, i, 1, &v1.vertex_buffer_d3d11, &v2, &v3);
					CHECK_STATE(v1.vertex_buffer_d3d11 == device->device_state.vertex_stream_driver[i].source.vertex_buffer_d3d11);
					CHECK_STATE(!v1.vertex_buffer_d3d11 || v2 == device->device_state.vertex_stream_driver[i].stride);
					CHECK_STATE(!v1.vertex_buffer_d3d11 || v3 == device->device_state.vertex_stream_driver[i].offset);
					if (v1.vertex_buffer_d3d11)
						ID3D11InputLayout_Release(v1.vertex_buffer_d3d11);
				}
				else
				{
					IDirect3DDevice9_GetStreamSource(device->d3d_device, i, &v1.vertex_buffer_d3d9, &v2, &v3);
					CHECK_STATE(v1.typeless_vertex_buffer == device->device_state.vertex_stream_driver[i].source.typeless_vertex_buffer);
					CHECK_STATE(!v1.typeless_vertex_buffer || v2 == device->device_state.vertex_stream_driver[i].offset);
					CHECK_STATE(!v1.typeless_vertex_buffer || v3 == device->device_state.vertex_stream_driver[i].stride);
					if (v1.typeless_vertex_buffer)
						IDirect3DVertexBuffer9_Release(v1.vertex_buffer_d3d9);
				}
			}

#if !_XBOX
			if (1)
			{
				if (!device->d3d11_device)
				{
					DWORD frequency;
					IDirect3DDevice9_GetStreamSourceFreq(device->d3d_device, i, &frequency);
					CHECK_STATE(frequency == device->device_state.vertex_stream_frequency_driver[i]);
				}
			}
#endif
		}
	}

	PERFINFO_AUTO_STOP_START("Vertex declarations", 1);

	if (1)
	{
		RdrVertexDeclarationObj vertex_declaration = { NULL };
		if (device->d3d11_device)
		{
			ID3D11DeviceContext_IAGetInputLayout(device->d3d11_imm_context, &vertex_declaration.layout);
			CHECK_STATE(vertex_declaration.layout == device->device_state.vertex_declaration_driver.layout);
			if (vertex_declaration.layout)
				ID3D11InputLayout_Release(vertex_declaration.layout);
		}
		else
		{
			IDirect3DDevice9_GetVertexDeclaration(device->d3d_device, &vertex_declaration.decl);
			CHECK_STATE(vertex_declaration.decl == device->device_state.vertex_declaration_driver.decl);
			if (vertex_declaration.decl)
				IDirect3DVertexDeclaration9_Release(vertex_declaration.decl);
		}
	}
	PERFINFO_AUTO_STOP_START("Index buffers", 1);
	if (1)
	{
		RdrIndexBufferObj index_buffer = { NULL };
		if (device->d3d11_device)
		{
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			U32 offset = 0;
			ID3D11DeviceContext_IAGetIndexBuffer(device->d3d11_imm_context, &index_buffer.index_buffer_d3d11, &format, &offset);
			CHECK_STATE(index_buffer.index_buffer_d3d11 == device->device_state.index_buffer_driver.index_buffer_d3d11);
			if (index_buffer.index_buffer_d3d11)
				ID3D11VertexShader_Release(index_buffer.index_buffer_d3d11);
		}
		else
		{
			IDirect3DDevice9_GetIndices(device->d3d_device, &index_buffer.index_buffer_d3d9);
			CHECK_STATE(index_buffer.index_buffer_d3d9 == device->device_state.index_buffer_driver.index_buffer_d3d9);
			if (index_buffer.index_buffer_d3d9)
				IDirect3DIndexBuffer9_Release(index_buffer.index_buffer_d3d9);
		}
	}

	PERFINFO_AUTO_STOP_START("Shaders", 1);
	// shaders
	if (1)
	{
		RdrVertexShaderObj vertex_shader={NULL};
		RdrPixelShaderObj pixel_shader={NULL};
		if (device->d3d11_device)
		{
			ID3D11DeviceContext_VSGetShader(device->d3d11_imm_context, &vertex_shader.vertex_shader_d3d11, NULL, NULL);
			CHECK_STATE(vertex_shader.typeless == device->device_state.vertex_shader_driver.typeless);
			if (vertex_shader.typeless)
				ID3D11VertexShader_Release(vertex_shader.vertex_shader_d3d11);

			ID3D11DeviceContext_PSGetShader(device->d3d11_imm_context, &pixel_shader.pixel_shader_d3d11, NULL, NULL);
			CHECK_STATE(pixel_shader.typeless == device->device_state.pixel_shader_driver.typeless);
			if (pixel_shader.typeless)
				ID3D11PixelShader_Release(pixel_shader.pixel_shader_d3d11);
		}
		else
		{
			IDirect3DDevice9_GetVertexShader(device->d3d_device, &vertex_shader.vertex_shader_d3d9);
			CHECK_STATE(vertex_shader.typeless == device->device_state.vertex_shader_driver.typeless);
			if (vertex_shader.typeless)
				IDirect3DVertexShader9_Release(vertex_shader.vertex_shader_d3d9);

			IDirect3DDevice9_GetPixelShader(device->d3d_device, &pixel_shader.pixel_shader_d3d9);
			CHECK_STATE(pixel_shader.typeless == device->device_state.pixel_shader_driver.typeless);
			if (pixel_shader.typeless)
				IDirect3DPixelShader9_Release(pixel_shader.pixel_shader_d3d9);
		}
	}

	PERFINFO_AUTO_STOP_START("Textures and samplers", 1);

	if (1)
	{
		int j;
		for (j=0; j<2; j++)
		{
			RdrDeviceTextureStateDX *textures_driver = device->device_state.textures_driver[j];
			int max_units = ((j==TEXTURE_PIXELSHADER)?MAX_TEXTURE_UNITS_TOTAL:MAX_VERTEX_TEXTURE_UNITS_TOTAL);
			ID3D11ShaderResourceView *textures[MAX_TEXTURE_UNITS_TOTAL];
			ID3D11SamplerState *samplers[MAX_TEXTURE_UNITS_TOTAL];
			D3D11_SAMPLER_DESC desc;
			RdrSamplerState last_sampler_state = {0};
			ID3D11SamplerState *last_sampler=NULL;

			PERFINFO_AUTO_START("top", 1);
			if (device->d3d11_device)
			{
				if (j==TEXTURE_PIXELSHADER)
				{
					ID3D11DeviceContext_PSGetShaderResources(device->d3d11_imm_context, 0, max_units, textures);
					ID3D11DeviceContext_PSGetSamplers(device->d3d11_imm_context, 0, max_units, samplers);
				} else {
					ID3D11DeviceContext_VSGetShaderResources(device->d3d11_imm_context, 0, max_units, textures);
					ID3D11DeviceContext_VSGetSamplers(device->d3d11_imm_context, 0, max_units, samplers);
				}
				PERFINFO_AUTO_STOP_START("bottom", 1);
			}
			for (i = 0; i < max_units; ++i, ++textures_driver)
			{
				if (device->d3d11_device)
				{

					CHECK_STATE(textures[i] == textures_driver->texture.texture_view_d3d11);
					if (textures[i])
						ID3D11ShaderResourceView_Release(textures[i]);

					assert(samplers[i]);
					if (samplers[i] == last_sampler && last_sampler_state.comparevalue == textures_driver->sampler_state.comparevalue)
					{
						// Cached and same as the last one
					} else {
						ID3D11SamplerState_GetDesc(samplers[i], &desc);
						CHECK_STATE((U32)desc.AddressU == textures_driver->sampler_state.address_u);
						CHECK_STATE((U32)desc.AddressV == textures_driver->sampler_state.address_v);
						CHECK_STATE((U32)desc.AddressW == textures_driver->sampler_state.address_w);
						assert(textures_driver->sampler_state.min_filter >= 1 && textures_driver->sampler_state.min_filter <= 3);
						assert(textures_driver->sampler_state.mag_filter >= 1 && textures_driver->sampler_state.mag_filter <= 3);
						CHECK_STATE(desc.Filter == d3d11_filter_lookup[textures_driver->sampler_state.min_filter-1][textures_driver->sampler_state.mag_filter-1]);
						CHECK_STATE((desc.MaxAnisotropy?desc.MaxAnisotropy:1) == ((desc.Filter==D3D11_FILTER_ANISOTROPIC)?textures_driver->sampler_state.max_anisotropy:1));
						CHECK_STATE(desc.MinLOD == textures_driver->sampler_state.max_mip_level);
						last_sampler = samplers[i];
						last_sampler_state = textures_driver->sampler_state;
					}
					ID3D11SamplerState_Release(samplers[i]);
				} else {
					DWORD dwSamplerState;
					int tex_unit = i + ((j==TEXTURE_VERTEXSHADER)?D3DVERTEXTEXTURESAMPLER0:0);
					D3DBaseTexture *tex=NULL;
					IDirect3DDevice9_GetTexture(device->d3d_device, tex_unit, &tex);
					CHECK_STATE(tex == textures_driver->texture.texture_base_d3d9);
					if (tex)
						IDirect3DBaseTexture9_Release(tex);

#define CHECK_SAMPLER_STATE(state, var) \
						IDirect3DDevice9_GetSamplerState(device->d3d_device, tex_unit, state, &dwSamplerState);	\
						CHECK_STATE(dwSamplerState == var);
					CHECK_SAMPLER_STATE(D3DSAMP_ADDRESSU, textures_driver->sampler_state.address_u);
					CHECK_SAMPLER_STATE(D3DSAMP_ADDRESSV, textures_driver->sampler_state.address_v);
					CHECK_SAMPLER_STATE(D3DSAMP_ADDRESSW, textures_driver->sampler_state.address_w);
					CHECK_SAMPLER_STATE(D3DSAMP_MAGFILTER, textures_driver->sampler_state.mag_filter);
					CHECK_SAMPLER_STATE(D3DSAMP_MINFILTER, textures_driver->sampler_state.min_filter);
					CHECK_SAMPLER_STATE(D3DSAMP_MIPFILTER, textures_driver->sampler_state.mip_filter);
					//CHECK_SAMPLER_STATE(D3DSAMP_MIPMAPLODBIAS, *(DWORD*)&textures_driver->sampler_state.mip_lod_bias);
					CHECK_SAMPLER_STATE(D3DSAMP_MAXANISOTROPY, textures_driver->sampler_state.max_anisotropy);
					CHECK_SAMPLER_STATE(D3DSAMP_MAXMIPLEVEL, textures_driver->sampler_state.max_mip_level);
#undef CHECK_SAMPLER_STATE
				}
			}
			PERFINFO_AUTO_STOP();
		}
	}

	// TODO: check rxbxApplyVSConstants(device);
	// TODO: check rxbxApplyPSConstants(device);

	PERFINFO_AUTO_STOP_START("Boolean constants", 1);

#if !_XBOX
	if (!device->d3d11_device && rxbxSupportsFeature((RdrDevice *)device, FEATURE_SM30))
#endif
	{
		BOOL driver_bool_values[MAX_PS_BOOL_CONSTANTS];
		BOOL directx_bool_values[MAX_PS_BOOL_CONSTANTS];
		U32 bit;
		for (i = 0, bit = 1; i < MAX_PS_BOOL_CONSTANTS; ++i)
		{
			if (device->device_state.ps_bool_constants_driver & bit)
				driver_bool_values[i] = TRUE;
			else
				driver_bool_values[i] = FALSE;
			bit = bit << 1;
		}
		IDirect3DDevice9_GetPixelShaderConstantB(device->d3d_device, 0, directx_bool_values, MAX_PS_BOOL_CONSTANTS);
		for (i=0; i<MAX_PS_BOOL_CONSTANTS; i++)
		{
			CHECK_STATE(driver_bool_values[i] == directx_bool_values[i]);
		}
	}

	// render states

#define D3DGetRenderState(state, var) IDirect3DDevice9_GetRenderState(device->d3d_device, state, &var)
	PERFINFO_AUTO_STOP_START("Rasterizer state", 1);
	// Rasterizer
	if (1)
	{
		if (device->d3d11_device)
		{
			ID3D11RasterizerState *state;
			D3D11_RASTERIZER_DESC desc;
			ID3D11DeviceContext_RSGetState(device->d3d11_imm_context, &state);
			ID3D11RasterizerState_GetDesc(state, &desc);
			ID3D11RasterizerState_Release(state);
			CHECK_STATE((U32)desc.CullMode == device->device_state.rasterizer_driver.cull_mode);
			CHECK_STATE((U32)desc.FillMode == device->device_state.rasterizer_driver.fill_mode);
			CHECK_STATE(desc.DepthBias == depthBiasToInt(device->device_state.rasterizer_driver.depth_bias));
			CHECK_STATE(desc.SlopeScaledDepthBias == device->device_state.rasterizer_driver.slope_scale_depth_bias);
			CHECK_STATE((U32)desc.ScissorEnable == device->device_state.rasterizer_driver.scissor_test);
		} else {
			DWORD cull_mode=0;
			DWORD fill_mode=0;
			DWORD depth_bias;
			DWORD slope_scale_depth_bias;
			DWORD scissor_test;
			D3DGetRenderState(D3DRS_CULLMODE, cull_mode);
			CHECK_STATE(cull_mode == device->device_state.rasterizer_driver.cull_mode);
			D3DGetRenderState(D3DRS_FILLMODE, fill_mode);
			CHECK_STATE(fill_mode == device->device_state.rasterizer_driver.fill_mode);
			D3DGetRenderState(D3DRS_DEPTHBIAS, depth_bias);
			CHECK_STATE(depth_bias == F2DWDepth(device->device_state.rasterizer_driver.depth_bias));
			D3DGetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, slope_scale_depth_bias);
			CHECK_STATE(slope_scale_depth_bias == F2DWDepth(device->device_state.rasterizer_driver.slope_scale_depth_bias));
			D3DGetRenderState(D3DRS_SCISSORTESTENABLE, scissor_test);
			CHECK_STATE(scissor_test == device->device_state.rasterizer_driver.scissor_test);
		}
	}

	PERFINFO_AUTO_STOP_START("Blend state", 1);
	// Blend
	if (1)
	{
		if (device->d3d11_device)
		{
			ID3D11BlendState *state;
			D3D11_BLEND_DESC desc;
			FLOAT blend_factor[4];
			DWORD sample_mask;
			ID3D11DeviceContext_OMGetBlendState(device->d3d11_imm_context, &state, blend_factor, &sample_mask);
			ID3D11BlendState_GetDesc(state, &desc);
			ID3D11BlendState_Release(state);
			CHECK_STATE(desc.RenderTarget[0].BlendEnable == calcEffectiveAlphaBlendEnable(device, &device->device_state.blend_driver));
			if (desc.RenderTarget[0].BlendEnable)
			{
				CHECK_STATE((U32)desc.RenderTarget[0].SrcBlend == device->device_state.blend_driver.src_blend);
				CHECK_STATE((U32)desc.RenderTarget[0].DestBlend == device->device_state.blend_driver.dest_blend);
				CHECK_STATE((U32)desc.RenderTarget[0].BlendOp == device->device_state.blend_driver.blend_op);
				if (device->device_state.blend_driver.blend_alpha_separate)
				{
					CHECK_STATE((U32)desc.RenderTarget[0].SrcBlendAlpha == device->device_state.blend_driver.src_blend_alpha);
					CHECK_STATE((U32)desc.RenderTarget[0].DestBlendAlpha == device->device_state.blend_driver.dest_blend_alpha);
					CHECK_STATE((U32)desc.RenderTarget[0].BlendOpAlpha == device->device_state.blend_driver.blend_op_alpha);
				} else {
					CHECK_STATE((U32)desc.RenderTarget[0].SrcBlendAlpha == device->device_state.blend_driver.src_blend);
					CHECK_STATE((U32)desc.RenderTarget[0].DestBlendAlpha == device->device_state.blend_driver.dest_blend);
					CHECK_STATE((U32)desc.RenderTarget[0].BlendOpAlpha == device->device_state.blend_driver.blend_op);
				}
			}
			CHECK_STATE(desc.RenderTarget[0].RenderTargetWriteMask == device->device_state.blend_driver.write_mask);
		} else {
			DWORD blend_enable;
			DWORD src_blend;
			DWORD dest_blend;
			DWORD blend_op;
			DWORD blend_alpha_separate;
			DWORD src_blend_alpha;
			DWORD dest_blend_alpha;
			DWORD blend_op_alpha;
			DWORD write_mask;
			D3DGetRenderState(D3DRS_ALPHABLENDENABLE, blend_enable);
			CHECK_STATE((bool)blend_enable == calcEffectiveAlphaBlendEnable(device, &device->device_state.blend_driver));
			D3DGetRenderState(D3DRS_SRCBLEND, src_blend);
			CHECK_STATE(src_blend == device->device_state.blend_driver.src_blend);
			D3DGetRenderState(D3DRS_DESTBLEND, dest_blend);
			CHECK_STATE(dest_blend == device->device_state.blend_driver.dest_blend);
			D3DGetRenderState(D3DRS_BLENDOP, blend_op);
			CHECK_STATE(blend_op == device->device_state.blend_driver.blend_op);
			D3DGetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, blend_alpha_separate);
			CHECK_STATE(blend_alpha_separate == device->device_state.blend_driver.blend_alpha_separate);
			if (blend_alpha_separate)
			{
				D3DGetRenderState(D3DRS_SRCBLENDALPHA, src_blend_alpha);
				CHECK_STATE(src_blend_alpha == device->device_state.blend_driver.src_blend_alpha);
				D3DGetRenderState(D3DRS_DESTBLENDALPHA, dest_blend_alpha);
				CHECK_STATE(dest_blend_alpha == device->device_state.blend_driver.dest_blend_alpha);
				D3DGetRenderState(D3DRS_BLENDOPALPHA, blend_op_alpha);
				CHECK_STATE(blend_op_alpha == device->device_state.blend_driver.blend_op_alpha);
			}
			D3DGetRenderState(D3DRS_COLORWRITEENABLE, write_mask);
			CHECK_STATE(write_mask == device->device_state.blend_driver.write_mask);
		}
	}

	PERFINFO_AUTO_STOP_START("DepthStencil state", 1);
	// DepthStencil
	if (1)
	{
		if (device->d3d11_device)
		{
			ID3D11DepthStencilState *state;
			D3D11_DEPTH_STENCIL_DESC desc;
			UINT stencil_ref;
			ID3D11DeviceContext_OMGetDepthStencilState(device->d3d11_imm_context, &state, &stencil_ref);
			ID3D11DepthStencilState_GetDesc(state, &desc);
			ID3D11DepthStencilState_Release(state);
			CHECK_STATE((U32)desc.DepthWriteMask == device->device_state.depth_stencil_driver.depth_write);
			CHECK_STATE((U32)desc.DepthFunc == device->device_state.depth_stencil_driver.depth_func);
			CHECK_STATE((U32)desc.StencilEnable == device->device_state.depth_stencil_driver.stencil_enable);
			CHECK_STATE(desc.StencilReadMask == device->device_state.depth_stencil_driver.stencil_read_mask);
			CHECK_STATE(desc.StencilWriteMask == device->device_state.depth_stencil_driver.stencil_write_mask);
			CHECK_STATE((U32)desc.FrontFace.StencilFailOp == device->device_state.depth_stencil_driver.stencil_fail_op);
			CHECK_STATE((U32)desc.FrontFace.StencilDepthFailOp == device->device_state.depth_stencil_driver.stencil_depth_fail_op);
			CHECK_STATE((U32)desc.FrontFace.StencilPassOp == device->device_state.depth_stencil_driver.stencil_pass_op);
			CHECK_STATE((U32)desc.FrontFace.StencilFunc == device->device_state.depth_stencil_driver.stencil_func);
			CHECK_STATE(stencil_ref == device->device_state.depth_stencil_driver.stencil_ref);
		} else {
			DWORD depth_func;
			DWORD depth_write;
			DWORD stencil_enable;
			DWORD stencil_read_mask;
			DWORD stencil_write_mask;
			DWORD stencil_fail_op;
			DWORD stencil_depth_fail_op;
			DWORD stencil_pass_op;
			DWORD stencil_func;
			DWORD stencil_ref;
			D3DGetRenderState(D3DRS_ZWRITEENABLE, depth_write);
			CHECK_STATE(depth_write == device->device_state.depth_stencil_driver.depth_write);
			D3DGetRenderState(D3DRS_ZFUNC, depth_func);
			CHECK_STATE(depth_func == device->device_state.depth_stencil_driver.depth_func);
			D3DGetRenderState(D3DRS_STENCILENABLE, stencil_enable);
			CHECK_STATE(stencil_enable == device->device_state.depth_stencil_driver.stencil_enable);
			D3DGetRenderState(D3DRS_STENCILMASK, stencil_read_mask);
			CHECK_STATE(stencil_read_mask == device->device_state.depth_stencil_driver.stencil_read_mask);
			D3DGetRenderState(D3DRS_STENCILWRITEMASK, stencil_write_mask);
			CHECK_STATE(stencil_write_mask == device->device_state.depth_stencil_driver.stencil_write_mask);
			D3DGetRenderState(D3DRS_STENCILFAIL, stencil_fail_op);
			CHECK_STATE(stencil_fail_op == device->device_state.depth_stencil_driver.stencil_fail_op);
			D3DGetRenderState(D3DRS_STENCILZFAIL, stencil_depth_fail_op);
			CHECK_STATE(stencil_depth_fail_op == device->device_state.depth_stencil_driver.stencil_depth_fail_op);
			D3DGetRenderState(D3DRS_STENCILPASS, stencil_pass_op);
			CHECK_STATE(stencil_pass_op == device->device_state.depth_stencil_driver.stencil_pass_op);
			D3DGetRenderState(D3DRS_STENCILFUNC, stencil_func);
			CHECK_STATE(stencil_func == device->device_state.depth_stencil_driver.stencil_func);
			D3DGetRenderState(D3DRS_STENCILREF, stencil_ref);
			CHECK_STATE(stencil_ref == device->device_state.depth_stencil_driver.stencil_ref);
		}
	}
	PERFINFO_AUTO_STOP();

#undef CHECK_STATE
	PERFINFO_AUTO_STOP();
}

void rxbxSetInstancedParams(RdrDeviceDX *device, const Mat4 world_matrix, const Vec4 tint_color, const Vec4 instance_param, bool affect_state_directly)
{
	Mat44 modelviewmat44;
	Mat44 modelmat;
	mat43to44(world_matrix, modelmat);
	mulMat44Inline(current_state->viewmat, modelmat, modelviewmat44);

	if (affect_state_directly && !device->d3d11_device) // DX11TODO: if we use this code path, want a quick way of updating these constants?
	{
		CHECKX(IDirect3DDevice9_SetVertexShaderConstantF(device->d3d_device, VS_CONSTANT_MODEL_MAT, &modelmat[0][0], 4));
		CHECKX(IDirect3DDevice9_SetVertexShaderConstantF(device->d3d_device, VS_CONSTANT_MODELVIEW_MAT, &modelviewmat44[0][0], 4));
		CHECKX(IDirect3DDevice9_SetVertexShaderConstantF(device->d3d_device, VS_CONSTANT_COLOR0, tint_color, 1));
		CHECKX(IDirect3DDevice9_SetVertexShaderConstantF(device->d3d_device, VS_CONSTANT_GLOBAL_INSTANCE_PARAM, instance_param, 1));
		INC_STATE_CHANGE(vs_constants, 10);
		FORCE_STATE_FLAG(vs_constants,VS_CONSTANT_MODEL_MAT);
		FORCE_STATE_FLAG(vs_constants,VS_CONSTANT_MODEL_MAT+1);
		FORCE_STATE_FLAG(vs_constants,VS_CONSTANT_MODEL_MAT+2);
		FORCE_STATE_FLAG(vs_constants,VS_CONSTANT_MODEL_MAT+3);
		FORCE_STATE_FLAG(vs_constants,VS_CONSTANT_MODELVIEW_MAT);
		FORCE_STATE_FLAG(vs_constants,VS_CONSTANT_MODELVIEW_MAT+1);
		FORCE_STATE_FLAG(vs_constants,VS_CONSTANT_MODELVIEW_MAT+2);
		FORCE_STATE_FLAG(vs_constants,VS_CONSTANT_MODELVIEW_MAT+3);
		FORCE_STATE_FLAG(vs_constants,VS_CONSTANT_COLOR0);
		FORCE_STATE_FLAG(vs_constants,VS_CONSTANT_GLOBAL_INSTANCE_PARAM);
	}
	else
	{
		setMatrix(device, VS_CONSTANT_MODEL_MAT, modelmat);
		setMatrix(device, VS_CONSTANT_MODELVIEW_MAT, modelviewmat44);
		rxbxVertexShaderParameters(device, VS_CONSTANT_COLOR0, (const Vec4 *)tint_color, 1);
		rxbxVertexShaderParameters(device, VS_CONSTANT_GLOBAL_INSTANCE_PARAM, (const Vec4 *)instance_param, 1);
		if (affect_state_directly && device->d3d11_device)
		{
			rxbxApplyVSConstants11(device);
		}
	}
}

#if DEBUG_DRAW_CALLS
int rxbxGateDrawCall(RdrDeviceDX *device)
{
	int bDraw = 0;
	int drawCallDebugMode = device->drawCallDebugMode;
	int drawCallNumber = device->drawCallNumber;
	int drawCallNumberIsolate = device->drawCallNumberIsolate;

	if (!drawCallDebugMode)
		return 1;
	switch ((device->drawCallDebugMode - 1 ) % 4)
	{
	case DCDM_LessEqual:
		bDraw = drawCallNumber <= drawCallNumberIsolate;

	xcase DCDM_Equal:
		bDraw = drawCallNumber == drawCallNumberIsolate;

	xcase DCDM_GreaterEqual:
		bDraw = drawCallNumber >= drawCallNumberIsolate;

	xcase DCDM_NotEqual:
		bDraw = drawCallNumber != drawCallNumberIsolate;
	}

	if (drawCallDebugMode > 4 && ((device->frame_count / 15) % 2))
		bDraw = 1;

	++device->drawCallNumber;
	return bDraw;
}

#define GATE_DRAW_CALL()	\
	rxbxCountQueryDrawCall(device);	\
	if (rxbxGateDrawCall(device))
#else
#define GATE_DRAW_CALL()	\
	rxbxCountQueryDrawCall(device);
#endif

void rxbxDirtyVertexDeclaration(RdrDeviceDX *device)
{
	memset(&device->device_state.vertex_declaration_driver, 0, sizeof(device->device_state.vertex_declaration_driver));
}

void rxbxResetStreamSource(RdrDeviceDX *device)
{
	// force setting desired stream source states again since DrawXXXXXUP leaves them indeterminate
	rxbxApplyVertexStream(device, 0);
}

void rxbxResetStreamSourceAndIndex(RdrDeviceDX *device)
{
	// force setting desired stream source and indices states again since DrawIndexedXXXXXUP leaves them indeterminate
	rxbxApplyVertexStream(device, 0);
	rxbxApplyIndexBuffer(device);
}

void rxbxDrawIndexedTriangles(RdrDeviceDX *device, int tri_base, int tri_count, int tri_index_range, int vertex_offset, bool apply_state)
{
	PERFINFO_AUTO_START_FUNC();
	if (apply_state)
		rxbxApplyStatePreDraw(device);
	VALIDATE_DEVICE_DEBUG();

#if !_XBOX
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
#endif
	{
		int num_instances = device->device_state.vertex_stream_frequency[0] & ~D3DSTREAMSOURCE_INDEXEDDATA;
		devassert(num_instances >= 1);
		MAX1(num_instances, 1);
		if (rdr_state.drawSingleTriangles)
			tri_count = 1;

#if _XBOX
		if (num_instances > 1)
		{
			Vec4 param = {tri_count * 3, tri_base * 3, 0, 0};
			IDirect3DDevice9_SetVertexShaderConstantF(device->d3d_device, VS_CONSTANT_XBOX_INSTANCE_DATA, param, 1);

			GATE_DRAW_CALL()
			IDirect3DDevice9_DrawVertices(device->d3d_device, D3DPT_TRIANGLELIST, vertex_offset * 3, param[0] * num_instances);

			INC_STATE_CHANGE(vs_constants, 1);
		}
		else
#endif
		{
			if (device->d3d11_device)
			{
				if (device->device_state.hull_shader)
					ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
				else
					ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				GATE_DRAW_CALL()
				ID3D11DeviceContext_DrawIndexedInstanced(device->d3d11_imm_context, tri_count * 3, num_instances, tri_base * 3, vertex_offset * 3, 0);
			}
			else
			{
				GATE_DRAW_CALL()
				CHECKX(IDirect3DDevice9_DrawIndexedPrimitive(device->d3d_device, D3DPT_TRIANGLELIST, vertex_offset * 3, 0, tri_index_range, tri_base*3, tri_count));
			}
		}

		INC_DRAW_CALLS(tri_count * num_instances);
	}
	PERFINFO_AUTO_STOP();
}

void rxbxDrawIndexedTrianglesUP(RdrDeviceDX *device, int tri_count, U16 *indices, int vertex_count, void *vertices, int stride)
{
	int ibo_offset;
	PERFINFO_AUTO_START_FUNC();

	ibo_offset = rxbxSetupVBODrawIndexed16VerticesUP(device, tri_count * 3, indices,
		vertex_count, vertices, stride);

	rxbxApplyStatePreDraw(device);
	VALIDATE_DEVICE_DEBUG();
#if !_XBOX
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
#endif
	{
		if (rdr_state.drawSingleTriangles)
			tri_count = 1;
		{
			if (device->d3d11_device)
			{
				// DX11TODO - DJR - make "lightly" state managed
				ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				GATE_DRAW_CALL()
				ID3D11DeviceContext_DrawIndexed(device->d3d11_imm_context, tri_count*3, ibo_offset / 2, 0);
			}
			else
			{
				GATE_DRAW_CALL()
				CHECKX(IDirect3DDevice9_DrawIndexedPrimitive(device->d3d_device, D3DPT_TRIANGLELIST, 0, 0, vertex_count, ibo_offset / 2, 
					tri_count));
			}
		}

		INC_DRAW_CALLS(tri_count);
	}
	PERFINFO_AUTO_STOP();
}

void rxbxDrawQuadsUP(RdrDeviceDX *device, int quad_count, void *vertices, int stride, bool apply_state)
{
	int vertex_count = quad_count * 4;
	PERFINFO_AUTO_START_FUNC();
#if !PLATFORM_CONSOLE
	rxbxSetupQuadIndexList(device, quad_count);

	rxbxSetupVBODrawVerticesUP(device, vertex_count, vertices, stride, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	if (apply_state)
		rxbxApplyStatePreDraw(device);
	VALIDATE_DEVICE_DEBUG();
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
	{
		if (device->d3d11_device)
		{
			ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			GATE_DRAW_CALL()
			ID3D11DeviceContext_DrawIndexed(device->d3d11_imm_context, quad_count * 6, 0, 0);
		}
		else
		{
			GATE_DRAW_CALL()
			CHECKX(IDirect3DDevice9_DrawIndexedPrimitive(device->d3d_device, D3DPT_TRIANGLELIST, 0, 0, quad_count * 4, 0, quad_count * 2));
		}

		INC_DRAW_CALLS(quad_count * 2);
	}
#else
	if (apply_state)
	{
		rxbxApplyStatePreDraw(device);
		VALIDATE_DEVICE_DEBUG();
	}
#if !_XBOX
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
#endif
	{
		GATE_DRAW_CALL()
		IDirect3DDevice9_DrawVerticesUP(device->d3d_device, D3DPT_QUADLIST, vertex_count, vertices, stride);

		rxbxResetStreamSource(device);

		INC_DRAW_CALLS(quad_count * 2);
	}
#endif
	PERFINFO_AUTO_STOP();
}

void rxbxDrawIndexedQuadsUP(RdrDeviceDX *device, int quad_count, U16* indices, void *vertices, int vertices_count, int stride, bool apply_state, bool calc_min_max)
{
	int minVert = 0;
	int maxVert = vertices_count;
#if !PLATFORM_CONSOLE
	int idx_count = quad_count*6;
#else
	int idx_count = quad_count*4;
#endif
	int ibo_offset;
	PERFINFO_AUTO_START_FUNC();

	if (calc_min_max)
	{
		int i;
		PERFINFO_AUTO_START("Calc idx range", 1);
		for (i = 0; i < idx_count; ++i)
		{
			int idx = indices[i];
			MIN1(minVert, idx);
			MAX1(maxVert, idx);
		}
		PERFINFO_AUTO_STOP();
	}

	ibo_offset = rxbxSetupVBODrawIndexed16VerticesUP(device, idx_count, indices,
		vertices_count, vertices, stride);

#if !PLATFORM_CONSOLE
	if (apply_state)
		rxbxApplyStatePreDraw(device);
	VALIDATE_DEVICE_DEBUG();
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
	{
		PERFINFO_AUTO_START("IDirect3DDevice9_DrawIndexedVerticesUP", 1);

		if (device->d3d11_device)
		{
			ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			GATE_DRAW_CALL()
			ID3D11DeviceContext_DrawIndexed(device->d3d11_imm_context, quad_count * 6, ibo_offset / 2, 0);
		}
		else
		{
			GATE_DRAW_CALL()
			CHECKX(IDirect3DDevice9_DrawIndexedPrimitive(device->d3d_device, D3DPT_TRIANGLELIST, 0, minVert, maxVert - minVert, ibo_offset / 2, quad_count * 2));
		}

		rxbxResetStreamSourceAndIndex(device);

		PERFINFO_AUTO_STOP();
		INC_DRAW_CALLS(quad_count * 2);
	}
#else
	if (apply_state)
	{
		rxbxApplyStatePreDraw(device);
		VALIDATE_DEVICE_DEBUG();
	}
#if !_XBOX
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
#endif
	{
		GATE_DRAW_CALL()
		IDirect3DDevice9_DrawIndexedVerticesUP(device->d3d_device, D3DPT_QUADLIST, minVert, maxVert, quad_count * 4, indices, D3DFMT_INDEX16, vertices, stride);

		rxbxResetStreamSourceAndIndex(device);

		INC_DRAW_CALLS(quad_count * 2);
	}
#endif
	PERFINFO_AUTO_STOP();
}

void rxbxDrawIndexedQuads32UP(RdrDeviceDX *device, int quad_count, U32* indices, void *vertices, int vertices_count, int stride, bool apply_state, bool calc_min_max)
{
	int minVert = 0;
	int maxVert = vertices_count;
	int ibo_offset;
#if !PLATFORM_CONSOLE
	int idx_count = quad_count*6;
#else
	int idx_count = quad_count*4;
#endif
	PERFINFO_AUTO_START_FUNC();

	ibo_offset = rxbxSetupVBODrawIndexed32VerticesUP(device, idx_count, indices,
		vertices_count, vertices, stride);

	if (calc_min_max)
	{
		int i;
		PERFINFO_AUTO_START("Calc idx range", 1);
		for (i = 0; i < idx_count; ++i)
		{
			int idx = indices[i];
			MIN1(minVert, idx);
			MAX1(maxVert, idx);
		}
		PERFINFO_AUTO_STOP();
	}

#if !PLATFORM_CONSOLE
	if (apply_state)
		rxbxApplyStatePreDraw(device);
	VALIDATE_DEVICE_DEBUG();
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
	{
		PERFINFO_AUTO_START("IDirect3DDevice9_DrawIndexedVerticesUP", 1);
		if (device->d3d11_device)
		{
			ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			GATE_DRAW_CALL()
			ID3D11DeviceContext_DrawIndexed(device->d3d11_imm_context, quad_count * 6, ibo_offset / 4, 0);
		}
		else
		{
			GATE_DRAW_CALL()
			CHECKX(IDirect3DDevice9_DrawIndexedPrimitiveUP(device->d3d_device, D3DPT_TRIANGLELIST,minVert, maxVert, quad_count * 2, indices, D3DFMT_INDEX32, vertices, stride));
		}

		rxbxResetStreamSourceAndIndex(device);

		PERFINFO_AUTO_STOP();
		INC_DRAW_CALLS(quad_count * 2);
	}
#else
	if (apply_state)
	{
		rxbxApplyStatePreDraw(device);
		VALIDATE_DEVICE_DEBUG();
	}
#if !_XBOX
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
#endif
	{
		GATE_DRAW_CALL()
		IDirect3DDevice9_DrawIndexedVerticesUP(device->d3d_device, D3DPT_QUADLIST, minVert, maxVert, quad_count * 4, indices, D3DFMT_INDEX32, vertices, stride);

		rxbxResetStreamSourceAndIndex(device);

		INC_DRAW_CALLS(quad_count * 2);
	}
#endif
	PERFINFO_AUTO_STOP();
}


void rxbxDrawIndexedQuads(RdrDeviceDX *device, int quad_count, int base_vertex_index, int start_index, int min_index, int max_index, bool apply_state)
{
	PERFINFO_AUTO_START_FUNC();

#if !PLATFORM_CONSOLE
	if (apply_state)
		rxbxApplyStatePreDraw(device);
	VALIDATE_DEVICE_DEBUG();
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
	{
		if (device->d3d11_device)
		{
			ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			GATE_DRAW_CALL()
			ID3D11DeviceContext_DrawIndexed(device->d3d11_imm_context, quad_count * 6, start_index, base_vertex_index);
		}
		else
		{
			GATE_DRAW_CALL()
			CHECKX(IDirect3DDevice9_DrawIndexedPrimitive(device->d3d_device, D3DPT_TRIANGLELIST, base_vertex_index, min_index, max_index - min_index, start_index, quad_count*2));
		}
		INC_DRAW_CALLS(quad_count * 2);
	}
#else
	if (apply_state)
	{
		rxbxApplyStatePreDraw(device);
		VALIDATE_DEVICE_DEBUG();
	}
#if !_XBOX
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
#endif
	{
		GATE_DRAW_CALL()
		IDirect3DDevice9_DrawIndexedPrimitive(device->d3d_device, D3DPT_QUADLIST, base_vertex_index, min_index, max_index - min_index, start_index, quad_count);
		INC_DRAW_CALLS(quad_count * 2);
	}
#endif
	PERFINFO_AUTO_STOP();
}

void rxbxDrawTriangleStripUP(RdrDeviceDX *device, int vertex_count, void *vertices, int stride)
{
	PERFINFO_AUTO_START_FUNC();
	rxbxSetupVBODrawVerticesUP(device, vertex_count, vertices, stride, D3DPT_TRIANGLESTRIP);
	rxbxApplyStatePreDraw(device);
	VALIDATE_DEVICE_DEBUG();
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
	{
		if (rdr_state.drawSingleTriangles)
			vertex_count = 3;
		if (device->d3d11_device)
		{
			ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			GATE_DRAW_CALL()
			ID3D11DeviceContext_Draw(device->d3d11_imm_context, vertex_count, 0);
		}
		else
		{
			GATE_DRAW_CALL()
			CHECKX(IDirect3DDevice9_DrawPrimitive(device->d3d_device, D3DPT_TRIANGLESTRIP, 0, vertex_count-2));
		}

		INC_DRAW_CALLS(vertex_count - 2);
	}
	PERFINFO_AUTO_STOP();
}

void rxbxDrawIndexedTriangleStripUP(RdrDeviceDX *device, int index_count, U16 *indices, int vertex_count, void *vertices, int stride)
{
	int ibo_offset;
	PERFINFO_AUTO_START_FUNC();

	ibo_offset = rxbxSetupVBODrawIndexed16VerticesUP(device, index_count, indices,
		vertex_count, vertices, stride);

	rxbxApplyStatePreDraw(device);
	VALIDATE_DEVICE_DEBUG();
#if !_XBOX
	if (GET_DESIRED_VALUE(pixel_shader.typeless))
#endif
	{
		if (device->d3d11_device)
		{
			// DX11TODO - DJR - make "lightly" state managed
			ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			GATE_DRAW_CALL()
			ID3D11DeviceContext_DrawIndexed(device->d3d11_imm_context, index_count, ibo_offset / 2, 0);
		}
		else
		{
			GATE_DRAW_CALL()
			CHECKX(IDirect3DDevice9_DrawIndexedVertices(device->d3d_device, D3DPT_TRIANGLESTRIP, 0, ibo_offset / 2, index_count));
		}

		rxbxResetStreamSourceAndIndex(device);

		INC_DRAW_CALLS(MAX(index_count - 2, 0));
	}
	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////

const char *rxbxGetStringForHResult(HRESULT hr)
{
	static char errstr[1024];
#define ERRTOSTR(err) if (hr == err) strcpy( errstr, #err );

	sprintf(errstr, "UNKNOWN: 0x%08x", hr);

	ERRTOSTR(D3D_OK);
	ERRTOSTR(D3DERR_WRONGTEXTUREFORMAT);
	ERRTOSTR(D3DERR_UNSUPPORTEDCOLOROPERATION);
	ERRTOSTR(D3DERR_UNSUPPORTEDCOLORARG);
	ERRTOSTR(D3DERR_UNSUPPORTEDALPHAOPERATION);
	ERRTOSTR(D3DERR_UNSUPPORTEDALPHAARG);
	ERRTOSTR(D3DERR_TOOMANYOPERATIONS);
	ERRTOSTR(D3DERR_CONFLICTINGTEXTUREFILTER);
	ERRTOSTR(D3DERR_UNSUPPORTEDFACTORVALUE);
	ERRTOSTR(D3DERR_CONFLICTINGRENDERSTATE);
	ERRTOSTR(D3DERR_UNSUPPORTEDTEXTUREFILTER);
	ERRTOSTR(D3DERR_DRIVERINTERNALERROR);
	ERRTOSTR(D3DERR_NOTFOUND);
	ERRTOSTR(D3DERR_MOREDATA);
	ERRTOSTR(D3DERR_DEVICELOST);
	ERRTOSTR(D3DERR_DEVICENOTRESET);
	ERRTOSTR(D3DERR_NOTAVAILABLE);
	ERRTOSTR(D3DERR_OUTOFVIDEOMEMORY);
	ERRTOSTR(D3DERR_INVALIDDEVICE);
	ERRTOSTR(D3DERR_INVALIDCALL);
	ERRTOSTR(D3DOK_NOAUTOGEN);

#if !_XBOX
	ERRTOSTR(D3DERR_CONFLICTINGTEXTUREPALETTE);
	ERRTOSTR(D3DERR_DRIVERINVALIDCALL);
	ERRTOSTR(D3DERR_WASSTILLDRAWING);
	ERRTOSTR(D3DERR_DEVICEREMOVED);
	ERRTOSTR(D3DERR_DEVICEHUNG);
	ERRTOSTR(D3DERR_UNSUPPORTEDOVERLAY);
	ERRTOSTR(D3DERR_UNSUPPORTEDOVERLAYFORMAT);
	ERRTOSTR(D3DERR_CANNOTPROTECTCONTENT);
	ERRTOSTR(D3DERR_UNSUPPORTEDCRYPTO);
	ERRTOSTR(D3DERR_PRESENT_STATISTICS_DISJOINT);
#endif

	ERRTOSTR(E_UNEXPECTED);
	ERRTOSTR(E_NOTIMPL);
	ERRTOSTR(E_OUTOFMEMORY);
	ERRTOSTR(E_INVALIDARG);
	ERRTOSTR(E_NOINTERFACE);
	ERRTOSTR(E_POINTER);
	ERRTOSTR(E_HANDLE);
	ERRTOSTR(E_ABORT);
	ERRTOSTR(E_FAIL);
	ERRTOSTR(E_ACCESSDENIED);

	ERRTOSTR(DXGI_STATUS_OCCLUDED);
	ERRTOSTR(DXGI_STATUS_CLIPPED);
	ERRTOSTR(DXGI_STATUS_NO_REDIRECTION);
	ERRTOSTR(DXGI_STATUS_NO_DESKTOP_ACCESS);
	ERRTOSTR(DXGI_STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE);
	ERRTOSTR(DXGI_STATUS_MODE_CHANGED);
	ERRTOSTR(DXGI_STATUS_MODE_CHANGE_IN_PROGRESS);
	ERRTOSTR(DXGI_ERROR_INVALID_CALL);
	ERRTOSTR(DXGI_ERROR_NOT_FOUND);
	ERRTOSTR(DXGI_ERROR_MORE_DATA);
	ERRTOSTR(DXGI_ERROR_UNSUPPORTED);
	ERRTOSTR(DXGI_ERROR_DEVICE_REMOVED);
	ERRTOSTR(DXGI_ERROR_DEVICE_HUNG);
	ERRTOSTR(DXGI_ERROR_DEVICE_RESET);
	ERRTOSTR(DXGI_ERROR_WAS_STILL_DRAWING);
	ERRTOSTR(DXGI_ERROR_FRAME_STATISTICS_DISJOINT);
	ERRTOSTR(DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE);
	ERRTOSTR(DXGI_ERROR_DRIVER_INTERNAL_ERROR);
	ERRTOSTR(DXGI_ERROR_NONEXCLUSIVE);
	ERRTOSTR(DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);
	ERRTOSTR(DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED);
	ERRTOSTR(DXGI_ERROR_REMOTE_OUTOFMEMORY);

	ERRTOSTR(D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS);
	ERRTOSTR(D3D11_ERROR_FILE_NOT_FOUND);
	ERRTOSTR(D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS);
	ERRTOSTR(D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD);

	return errstr;
}

__forceinline int isMemoryErrorHR(HRESULT hr)
{
	return hr == D3DERR_OUTOFVIDEOMEMORY || hr == E_OUTOFMEMORY;
}

void rxbxFatalHResultErrorf(RdrDeviceDX *device, HRESULT hr, const char *action_str, const char *detail_str, ...)
{
	VirtualMemStats currentVMStats = { 0 };
	char buffer[1024];
	int result;
	va_list argptr;

	if (!FAILED(hr))
		return;

	va_start(argptr, detail_str);
	result = vsprintf(buffer, detail_str, argptr);
	va_end(argptr);

	// DJR Note: we're skipping E_OUTOFMEMORY and INVALIDCALL because we're going to temporarily include
	// a memory report with the bug.
	if (/*hr == E_OUTOFMEMORY || hr == D3DERR_INVALIDCALL ||*/ hr == D3DERR_DRIVERINVALIDCALL || hr == D3DERR_DRIVERINTERNALERROR || 
		hr == D3DERR_NOTAVAILABLE)
		systemSpecsUpdateMemTrivia(0);

	if (hr == E_OUTOFMEMORY)
	{
		char friendlyRAM[32], friendlyVirt[32];
		U64 availableMem, availVirtual;

		getPhysicalMemory64Ex(NULL, &availableMem, &availVirtual);
		if (buffer[0])
			strcat(buffer, ", ");
		strcatf(buffer, "%s RAM, %s virtual space available", friendlyBytesBuf(availableMem, friendlyRAM), friendlyBytesBuf(availVirtual, friendlyVirt));
	}
	if (hr == D3DERR_INVALIDCALL || hr == E_INVALIDARG)
	{
		const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
		strcatf(buffer, " %s", device_infos[device->device_info_index]->name);
	}

	triviaPrintf("CrashWasVideoRelated", "1");
	rxbxDumpDeviceStateOnError(device, hr);

	if (device->d3d11_device)
	{
		HRESULT hrDeviceRemovedReason = ID3D11Device_GetDeviceRemovedReason(device->d3d11_device);
		strcatf(buffer, "D3D11 Device removed due to %s 0x%0x",
			rxbxGetStringForHResult(hrDeviceRemovedReason), hrDeviceRemovedReason);

		if (device->bResolveSubresourceValidationErrorIssued)
		{
			strcatf(buffer, "ResolveSubresource validation error: Dst size (%d x %d, Fmt %d); Src size (%d x %d Fmt %d)",
				device->lastResolveSubresourceParams.tex_desc.Width, 
				device->lastResolveSubresourceParams.tex_desc.Height, 
				device->lastResolveSubresourceParams.tex_desc.Format, 
				device->lastResolveSubresourceParams.src_tex_desc.Width, 
				device->lastResolveSubresourceParams.src_tex_desc.Height, 
				device->lastResolveSubresourceParams.src_tex_desc.Format);
		}
		if (device->bCopySubresourceRegionValidationErrorIssued)
		{
			strcatf(buffer, "Dst size (%d x %d, Fmt %d); Src size (%d x %d Fmt %d) resource bounds (%d,%d)-(%d, %d)",
				device->lastCopySubresourceRegionParams.tex_desc.Width, 
				device->lastCopySubresourceRegionParams.tex_desc.Height, 
				device->lastCopySubresourceRegionParams.tex_desc.Format, 
				device->lastCopySubresourceRegionParams.src_tex_desc.Width, 
				device->lastCopySubresourceRegionParams.src_tex_desc.Height, 
				device->lastCopySubresourceRegionParams.src_tex_desc.Format,
				0, 0, device->lastCopySubresourceRegionParams.width, device->lastCopySubresourceRegionParams.height);
		}
		if (device->bCopyResourceValidationErrorIssued)
		{
			strcatf(buffer, "Dst (%d x %d, Fmt %d); Src (%d x %d Fmt %d)",
				device->lastCopyResourceParams.tex_desc.Width, 
				device->lastCopyResourceParams.tex_desc.Height, 
				device->lastCopyResourceParams.tex_desc.Format, 
				device->lastCopyResourceParams.src_tex_desc.Width, 
				device->lastCopyResourceParams.src_tex_desc.Height, 
				device->lastCopyResourceParams.src_tex_desc.Format);
		}

	}

	ErrorDetailsf("%s", buffer);
	triviaPrintf("details", "%s", buffer);
	if (hr == D3DERR_OUTOFVIDEOMEMORY)
	{
		FatalErrorf("There is not enough available video memory (%s) while %s. Please choose lower resolution settings, or close other graphics applications.", rxbxGetStringForHResult(hr), action_str);
	}
	else
	{
		if (hr == E_OUTOFMEMORY)
		{
			virtualMemoryAnalyzeStats(&currentVMStats);
			virtualMemoryMemlogStats(NULL, "E_OUTOFMEMORY", &currentVMStats);

			includeMemoryReportInCrashDump();
		}
		FatalErrorf("Direct3D driver returned error code (%s) while %s.", rxbxGetStringForHResult(hr), action_str);
	}
}


void rxbxClearDeviceRenderTargetState(RdrDeviceDX *device) {

	int i;

	// Clear actual device.
	if (device->d3d11_device)
	{
		ID3D11RenderTargetView *nullJunk[SBUF_MAXMRT] = {0};
		ID3D11DeviceContext_OMSetRenderTargets(
			device->d3d11_imm_context,
			SBUF_MAXMRT, nullJunk,
			NULL);

	} else {

		RdrSurfaceObj null_surface = { 0 };
		CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 0, null_surface));
		if(rdrSupportsFeature(&device->device_base, FEATURE_MRT2)) {
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 1, null_surface));
		}
		if(rdrSupportsFeature(&device->device_base, FEATURE_MRT4)) {
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 2, null_surface));
			CHECKX(IDirect3DDevice9_SetRenderTargetEx(device, 3, null_surface));
		}

	}

	// Clear shadow state.
	for(i = 0; i < SBUF_MAXMRT; i++) {
		device->device_state.targets_driver[i].typeless_surface = NULL;
	}

}

void rxbxClearDeviceTextureState(RdrDeviceDX *device) {

	int i;
	int j;

	// Clear actual stuff on the device.
	if (device->d3d11_device)
	{
		ID3D11ShaderResourceView *nullViews[LARGEST_MAX_TEXTURE_UNITS] = {0};
		ID3D11DeviceContext_PSSetShaderResources(
			device->d3d11_imm_context,
			0,
			LARGEST_MAX_TEXTURE_UNITS,
			nullViews);
		ID3D11DeviceContext_GSSetShaderResources(
			device->d3d11_imm_context,
			0,
			LARGEST_MAX_TEXTURE_UNITS,
			nullViews);
		ID3D11DeviceContext_HSSetShaderResources(
			device->d3d11_imm_context,
			0,
			LARGEST_MAX_TEXTURE_UNITS,
			nullViews);
		ID3D11DeviceContext_VSSetShaderResources(
			device->d3d11_imm_context,
			0,
			LARGEST_MAX_TEXTURE_UNITS,
			nullViews);

	} else {
		for(i = 0; i < LARGEST_MAX_TEXTURE_UNITS; i++) {
			CHECKX(IDirect3DDevice9_SetTexture(device->d3d_device, i,NULL));
		}
	}

	// Clear shadow state.
	for(i = 0; i < TEXTURE_TYPE_COUNT; i++) {
		for(j = 0; j < LARGEST_MAX_TEXTURE_UNITS; j++) {
			device->device_state.textures_driver[i][j].texture.typeless = NULL;
		}
	}
}

void rxbxApplyTargetsAndTextures(RdrDeviceDX *device) {
	if(!device->disable_texture_and_target_apply) {
		rxbxClearDeviceTextureState(device);
		rxbxApplyTargets(device);
		rxbxApplyPixelTextureState(device, device->d3d_device);
	}
}

int rdrApplyTexturesAndTargetsTogether = 1;
AUTO_CMD_INT(rdrApplyTexturesAndTargetsTogether, rdrApplyTexturesAndTargetsTogether) ACMD_CATEGORY(DEBUG) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

bool rxbxShouldApplyTexturesAndStatesTogether(void) {
	return rdrApplyTexturesAndTargetsTogether;
}

// Keep this as the last entry in this file, it confuses CTAGS (WWhiz)
STATIC_ASSERT(PS_CONSTANT_MATERIAL_PARAM_MAX < MAX_PS_CONSTANTS);
