#include "memlog.h"
#include "EventTimingLog.h"
#include "SystemSpecs.h"
#include "utils.h"

#include "RdrState.h"
#include "RenderLib.h"
#include "rt_xsurface.h"
#include "rt_xtextures.h"
#include "rt_xdrawmode.h"
#include "rt_xpostprocessing.h"
#include "rt_xprimitive.h"
#include "Vec4H.h"
#include "ImageUtil.h"

#include "StructDefines.h"

#if !_XBOX
#include "nvapi_wrapper.h"
#endif

// TXAA_TODO integrate actual lib from Nvidia when available, and then remove it from GameClient.props.
//#pragma comment(lib, "../../3rdparty/NvidiaTXAA/Txaa.win32.lib")

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

#define HORIZONTAL_TILES 0
#if !_XBOX
	#define D3DCLEAR_ALLTARGETS D3DCLEAR_TARGET
#endif

#define ZPASS_BIGZBUFFER 0
#define DEBUG_LOG_MRT_TO_DISK 0

#if DEBUG_LOG_MRT_TO_DISK
void rxbxDumpSurfaces( RdrDeviceDX *device );
#endif

#if _XBOX
static const DWORD resolve_flags[] = {
	D3DRESOLVE_RENDERTARGET0,
	D3DRESOLVE_RENDERTARGET1,
	D3DRESOLVE_RENDERTARGET2,
	D3DRESOLVE_RENDERTARGET3,
	D3DRESOLVE_DEPTHSTENCIL | D3DRESOLVE_FRAGMENT0,
};
#endif

__forceinline static int rxbxReleaseSurface(RdrDeviceDX *device, RdrSurfaceObj *d3d_surface)
{
	int ref_count;
	if (device->d3d11_device)
		ref_count = IDirect3DSurface9_Release(d3d_surface->surface9);
	else
		ref_count = ID3D11View_Release(d3d_surface->view11);
	rxbxLogReleaseSurface(device, *d3d_surface, ref_count);
	d3d_surface->typeless_surface = NULL;
	return ref_count;
}

void rxbxSurfaceFreeRenderTargetTextures(RdrDeviceDX *device, RdrSurfaceDX *surface, bool free_texture_snapshot_arrays);

__forceinline static int bytesFromSBT(RdrSurfaceBufferType type)
{
	return imgMinPitch(rxbxGetTexFormatForSBT(type).format, 1);
}

RdrTexFormatObj rxbxGetTexFormatForSBT(RdrSurfaceBufferType sbt)
{
	RdrTexFormatObj ret_obj;
	RdrTexFormat ret=RTEX_INVALID_FORMAT;

	switch (sbt & SBT_TYPE_MASK)
	{
		xcase SBT_RGBA:
			ret = RTEX_BGRA_U8;

		xcase SBT_RGBA10:
			ret = RTEX_RGBA10;

		xcase SBT_RGBA_FIXED:
			ret = RTEX_A16B16G16R16;

		xcase SBT_RGBA_FLOAT:
			ret = RTEX_RGBA_F16;

		xcase SBT_RGBA_FLOAT32:
			ret = RTEX_RGBA_F32;

		xcase SBT_RG_FIXED:
			ret = RTEX_G16R16;

		xcase SBT_RG_FLOAT:
			ret = RTEX_G16R16F;

		xcase SBT_FLOAT:
			ret = RTEX_R_F32;

		xcase SBT_RGB16:
			ret = RTEX_R5G6B5;

		xcase SBT_BGRA:
			ret = RTEX_RGBA_U8;

		xdefault:
			assert(0);
	}
	ret_obj.format = ret;
	return ret_obj;
}

void rxbxSetSurfaceFogDirect(RdrDeviceDX *device, RdrSetFogData *data, WTCmdPacket *packet)
{
	RdrSurfaceDX *xsurface = (RdrSurfaceDX *)data->surface;
	rxbxFogMode(device, &xsurface->state, data->bVolumeFog);
	rxbxFogRange(device, &xsurface->state, 
		data->low_near_dist, data->low_far_dist, data->low_max_fog,
		data->high_near_dist, data->high_far_dist, data->high_max_fog,
		data->low_height, data->high_height);
	rxbxFogColor(device, &xsurface->state, data->low_fog_color, data->high_fog_color);
}

// Clears the copied depth surface values so that freeing/reiniting doesn't free things twice, etc
static void rxbxResetDepthSurface(RdrSurfaceDX *surface)
{
	if (surface->depth_surface) {
		surface->rendertarget[SBUF_DEPTH].d3d_surface.typeless_surface = NULL;
		if (surface->rendertarget[SBUF_DEPTH].texture_count) { // Only on Xbox?
			// DX11TODO - DJR - decide if we need to reset the the DX11 texture buffer as well
			surface->rendertarget[SBUF_DEPTH].textures[0].d3d_texture.typeless = NULL;
			ZeroStructForce(&surface->rendertarget[SBUF_DEPTH].textures[0].tex_handle);
		}
	}
}

// Restores copied depth surface values
static void rxbxRestoreDepthSurface(RdrSurfaceDX *surface)
{
	if (surface->depth_surface) {
		surface->rendertarget[SBUF_DEPTH].d3d_surface = surface->depth_surface->rendertarget[SBUF_DEPTH].d3d_surface;
		if (surface->rendertarget[SBUF_DEPTH].texture_count) { // Only on Xbox?
			// DX11TODO - DJR - decide if we need to copy the the DX11 texture buffer as well
			surface->rendertarget[SBUF_DEPTH].textures[0].d3d_texture = surface->depth_surface->rendertarget[SBUF_DEPTH].textures[0].d3d_texture;
			surface->rendertarget[SBUF_DEPTH].textures[0].tex_handle = surface->depth_surface->rendertarget[SBUF_DEPTH].textures[0].tex_handle;
		}
	}
}

void rxbxSurfaceGrowSnapshotTextures(RdrDeviceDX *device, RdrSurfaceDX *surface, int buffer_index, int snapshot_index, const char *name)
{
	assert(buffer_index >= 0 && buffer_index < SBUF_MAX);
	dynArrayFitStructs(&surface->rendertarget[buffer_index].textures, &surface->rendertarget[buffer_index].texture_count, snapshot_index);
	assert(name && *name);
	surface->rendertarget[buffer_index].textures[snapshot_index].name = name;
}

void rxbxSurfaceSetDepthSurfaceDirect(RdrDeviceDX *device, RdrSurfaceDX **surfaces, WTCmdPacket *packet)
{
#if _XBOX
	// on xbox, the render targets don't really hold the data. instead, make the surfaces share the destination texture
	if (surfaces[0]->rendertarget[SBUF_DEPTH].texture_count == 0) {
		rxbxSurfaceGrowSnapshotTextures(device, surfaces[0], SBUF_DEPTH, 0, "Depth auto-resolve");
	}

	if (surfaces[0]->depth_surface)
	{
		surfaces[0]->depth_surface = NULL;

		surfaces[0]->rendertarget[SBUF_DEPTH].textures[0].d3d_texture.typeless = NULL;
		ZeroStructForce(&surfaces[0]->rendertarget[SBUF_DEPTH].textures[0].tex_handle);
		surfaces[0]->rendertarget[SBUF_DEPTH].borrowed_texture = false;
	}

	if (!surfaces[1])
		return;

	assert(!surfaces[0]->rendertarget[SBUF_DEPTH].textures[0].d3d_texture.typeless);
	assert(surfaces[1]->rendertarget[SBUF_DEPTH].textures[0].d3d_texture.typeless && surfaces[1]->rendertarget[SBUF_DEPTH].textures[0].d3d_texture.typeless);
	surfaces[0]->rendertarget[SBUF_DEPTH].textures[0].d3d_texture = surfaces[1]->rendertarget[SBUF_DEPTH].textures[0].d3d_texture;
	surfaces[0]->rendertarget[SBUF_DEPTH].textures[0].tex_handle = surfaces[1]->rendertarget[SBUF_DEPTH].textures[0].tex_handle;
	surfaces[0]->rendertarget[SBUF_DEPTH].borrowed_texture = true;
	surfaces[0]->depth_surface = surfaces[1];
#else
	if (surfaces[0]->depth_surface)
	{
		rxbxResetDepthSurface(surfaces[0]);
		surfaces[0]->depth_surface = NULL;
		surfaces[0]->rendertarget[SBUF_DEPTH].borrowed_texture = false;
	}

	if (!surfaces[1])
		return;

	assert(!surfaces[0]->rendertarget[SBUF_DEPTH].d3d_surface.typeless_surface);
	assert(surfaces[1]->rendertarget[SBUF_DEPTH].d3d_surface.typeless_surface);
	surfaces[0]->depth_surface = surfaces[1];
	surfaces[0]->rendertarget[SBUF_DEPTH].d3d_surface = surfaces[1]->rendertarget[SBUF_DEPTH].d3d_surface;
	surfaces[0]->rendertarget[SBUF_DEPTH].borrowed_texture = true;
	assert(!surfaces[0]->rendertarget[SBUF_DEPTH].textures || !surfaces[0]->rendertarget[SBUF_DEPTH].textures[0].d3d_texture.typeless);
#endif
}

// DX11 TODO DJR test HDR buffer problem without this complete swap
int completeSnapshotSwap = 0;
AUTO_CMD_INT(completeSnapshotSwap,completeSnapshotSwap) ACMD_CATEGORY(DEBUG) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

int rdrSwapTexDataWhenSwappingSnapshots = 1;
AUTO_CMD_INT(rdrSwapTexDataWhenSwappingSnapshots, rdrSwapTexDataWhenSwappingSnapshots) ACMD_CATEGORY(DEBUG) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

void rxbxSurfaceSwapSnapshotsDirect(RdrDeviceDX *device, RdrSwapSnapshotData *params, WTCmdPacket *packet)
{
	RdrSurfaceDX *surface0 = (RdrSurfaceDX*)params->surface0;
	RdrSurfaceDX *surface1 = (RdrSurfaceDX*)params->surface1;
	RxbxSurfacePerTargetState *state0 = &surface0->rendertarget[params->buffer0];
	RxbxSurfacePerTargetState *state1 = &surface1->rendertarget[params->buffer1];

	if (completeSnapshotSwap)
	{
		// DX11 TODO DJR test HDR buffer problem without this complete swap
		RdrPerSnapshotState * temp_textures_array = state0->textures;
		int temp_texture_count = state0->texture_count;

		state0->textures = state1->textures;
		state0->texture_count = state1->texture_count;

		state1->textures = temp_textures_array;
		state1->texture_count = temp_texture_count;
	}
	else
	{
		if (params->index0 < (U32)state0->texture_count &&
			params->index1 < (U32)state1->texture_count)
		{
			RdrPerSnapshotState temp;

			rxbxClearTextureStateOfSurface(device, surface0, params->buffer0, params->index0);
			rxbxClearTextureStateOfSurface(device, surface1, params->buffer1, params->index1);

			temp = state0->textures[params->index0];
			state0->textures[params->index0] = state1->textures[params->index1];
			state1->textures[params->index1] = temp;

			if(rdrSwapTexDataWhenSwappingSnapshots) {

				RdrTextureDataDX *texdata1 = NULL;
				RdrTextureDataDX *texdata2 = NULL;

				stashIntFindPointer(
					device->texture_data,
					state0->textures[params->index0].tex_handle.texture.hash_value,
					&texdata1);

				stashIntFindPointer(
					device->texture_data,
					state1->textures[params->index1].tex_handle.texture.hash_value,
					&texdata2);

				if(texdata1 && texdata2) {
					RdrTextureObj tmp;
					tmp = texdata1->texture;
					texdata1->texture = texdata2->texture;
					texdata2->texture = tmp;
				}
			}
		}
	}
}

void rxbxUpdateSurfaceMatricesDirect(RdrDeviceDX *device, RdrSurfaceUpdateMatrixData *data, WTCmdPacket *packet)
{
	RdrSurfaceDX *surface = (RdrSurfaceDX *)data->surface;
	rxbxSet3DProjection(device, &surface->state, data->projection, data->fardepth_projection, data->sky_projection, data->znear, data->zfar, data->far_znear, data->viewport_x, data->viewport_width, data->viewport_y, data->viewport_height, data->camera_pos);
	rxbxSetViewMatrix(device, &surface->state, data->view_mat, data->inv_view_mat);

	mulMat4(data->fog_mat, data->inv_view_mat, surface->state.fog_mat);
}

#if !_PS3
#if _XBOX
	#define copyVec4X( src, dest )	copyVec4( src, dest.v )
#else
	#define copyVec4X( src, dest )	copyVec4( src, &dest.x )
#endif
#endif

//__forceinline static 
void D3DClearSurface( RdrDeviceDX * device, int targets, Vec4 clear_color, float clear_depth, int clear_stencil )
{
#if _PS3
    if(targets&(D3DCLEAR_TARGET)) {
        uint32_t color = MAKE_DWORD(
            round(saturate(clear_color[3]) * 255.0f ),
            round(saturate(clear_color[0]) * 255.0f ),
            round(saturate(clear_color[1]) * 255.0f ),
            round(saturate(clear_color[2]) * 255.0f )
        );
        
        assert(gcm_surface_usage & (D3DCLEAR_TARGET));

        if(
            gcm_surface.colorFormat == CELL_GCM_SURFACE_F_W16Z16Y16X16 ||
            gcm_surface.colorFormat == CELL_GCM_SURFACE_F_X32
        ) {
            targets &= ~D3DCLEAR_TARGET;

            assert(color==0);

            {
                CellGcmSurface s;
                s = gcm_surface;

                s.colorFormat = CELL_GCM_SURFACE_A8R8G8B8;

                if(gcm_surface.colorFormat == CELL_GCM_SURFACE_F_W16Z16Y16X16)
                    s.width *= 2;

                s.depthFormat = CELL_GCM_SURFACE_Z24S8;
                s.depthLocation = CELL_GCM_LOCATION_LOCAL;
                s.depthOffset = 0;
                s.depthPitch = 64;

                cellGcmSetSurfaceWindow(device, &s, CELL_GCM_WINDOW_ORIGIN_TOP, CELL_GCM_WINDOW_PIXEL_CENTER_HALF);

                cellGcmSetClearColor(device, 0);
                cellGcmSetClearSurface(device, D3DCLEAR_TARGET);

                cellGcmSetSurfaceWindow(device, &gcm_surface, CELL_GCM_WINDOW_ORIGIN_TOP, CELL_GCM_WINDOW_PIXEL_CENTER_HALF);
            }

        } else {
            cellGcmSetClearColor(device, color);
        }
    }
    if(targets&(D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL)) {
        uint32_t ds = ((uint32_t)(clear_depth*(float)0xffffff)<<8);
        
        assert(gcm_surface_usage & (D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL));

        cellGcmSetClearDepthStencil(device, ds);
    }
    if(targets&(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL)) {
        cellGcmSetClearSurface(device, targets);
    }
#elif _XBOX
	D3DVECTOR4 clear_color_DX;
	copyVec4X(clear_color, clear_color_DX);
	IDirect3DDevice9_ClearF(device, targets, NULL, &clear_color_DX, 1.f - clear_depth, 0);
#else
	if (device->d3d11_device)
	{
		int mrt, mrt_mask = MASK_SBUF_0;
		if (device->device_state.targets_driver[SBUF_DEPTH].depth_stencil_view11)
			ID3D11DeviceContext_ClearDepthStencilView(device->d3d11_imm_context, device->device_state.targets_driver[SBUF_DEPTH].depth_stencil_view11, D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, clear_depth, 0);
		for (mrt = 0; mrt < SBUF_MAXMRT; ++mrt, mrt_mask <<= 1)
		{
			if ((device->active_surface->active_buffer_write_mask & mrt_mask) && device->device_state.targets_driver[mrt].render_target_view11)
				ID3D11DeviceContext_ClearRenderTargetView(device->d3d11_imm_context, device->device_state.targets_driver[mrt].render_target_view11, clear_color);
		}
	}
	else
	{
		D3DCOLOR colorClear;
		colorClear = D3DCOLOR_RGBA( 
			round(saturate(clear_color[0]) * 255.0f ),
			round(saturate(clear_color[1]) * 255.0f ),
			round(saturate(clear_color[2]) * 255.0f ),
			round(saturate(clear_color[3]) * 255.0f ));
		CHECKX(IDirect3DDevice9_Clear(device->d3d_device, 0, NULL, targets, colorClear, clear_depth, 0));
	}
#endif
}

static D3DCUBEMAP_FACES getActiveD3DCubemapFace(RdrSurfaceDX *surface)
{
	D3DCUBEMAP_FACES d3d_face;

	if (!(surface->creation_flags & SF_CUBEMAP))
		return 0;

	switch (surface->active_face)
	{
		xcase RSF_POSITIVE_X:
			d3d_face = D3DCUBEMAP_FACE_POSITIVE_X;

		xcase RSF_POSITIVE_Y:
			d3d_face = D3DCUBEMAP_FACE_POSITIVE_Y;

		xcase RSF_POSITIVE_Z:
			d3d_face = D3DCUBEMAP_FACE_POSITIVE_Z;

		xcase RSF_NEGATIVE_X:
			d3d_face = D3DCUBEMAP_FACE_NEGATIVE_X;

		xcase RSF_NEGATIVE_Y:
			d3d_face = D3DCUBEMAP_FACE_NEGATIVE_Y;

		xcase RSF_NEGATIVE_Z:
			d3d_face = D3DCUBEMAP_FACE_NEGATIVE_Z;

		xdefault:
			assertmsg(0, "Unknown surface face type.");
	}

	return d3d_face;
}

void rxbxSetActiveSurfaceFace(RdrDeviceDX *device, RdrSurfaceFace face)
{
	RdrSurfaceDX *surface = device->active_surface;

	if (!(surface->creation_flags & SF_CUBEMAP))
		return;

	if (surface->active_face == face)
		return;

	surface->active_face = face;

	if (surface->use_render_to_texture)
	{
		D3DCUBEMAP_FACES d3d_face = getActiveD3DCubemapFace(surface);
		int bufIndex;

		for (bufIndex = SBUF_0; bufIndex < SBUF_MAXMRT; ++bufIndex)
		{
			if (surface->rendertarget[bufIndex].texture_count && surface->rendertarget[bufIndex].textures[0].d3d_texture.typeless)
			{
				if (surface->rendertarget[bufIndex].d3d_surface.typeless_surface)
					rxbxReleaseSurface(device, &surface->rendertarget[bufIndex].d3d_surface);
				if (device->d3d11_device)
				{
					// DX11TODO - DJR - create render target views only once - maybe use GS to output to all cube map faces
					HRESULT hr;
					D3D11_RENDER_TARGET_VIEW_DESC cubeface_render_target_desc;
					int set_index = 0;

					cubeface_render_target_desc.Format = rxbxGetGPUSurfaceFormat11(rxbxGetTexFormatForSBT(surface->buffer_types[bufIndex]), false);
					cubeface_render_target_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
					cubeface_render_target_desc.Texture2DArray.MipSlice = 0;
					cubeface_render_target_desc.Texture2DArray.FirstArraySlice = d3d_face;
					cubeface_render_target_desc.Texture2DArray.ArraySize = 1;

					hr = ID3D11Device_CreateRenderTargetView(device->d3d11_device, surface->rendertarget[bufIndex].textures[set_index].d3d_buffer.resource_d3d11, &cubeface_render_target_desc,
						&surface->rendertarget[bufIndex].d3d_surface.render_target_view11);
					rxbxFatalHResultErrorf(device, hr, "creating a render target view", "");
				}
				else
				{
					CHECKX(IDirect3DCubeTexture9_GetCubeMapSurface(surface->rendertarget[bufIndex].textures[0].d3d_texture.texture_cube_d3d9, d3d_face, 0, &surface->rendertarget[bufIndex].d3d_surface.surface9));
				}
				if (surface->rendertarget[bufIndex].d3d_surface.surface9)
					rxbxLogCreateSurface(device, surface->rendertarget[bufIndex].d3d_surface);
			}
		}
	}
}

void rxbxSetActiveSurfaceBufferWriteMask(RdrDeviceDX *device, RdrSurfaceBufferMaskBits buffer_write_mask, RdrSurfaceFace face)
{
	RdrSurfaceDX *surface = device->active_surface;
	int bufIndex, mask = MASK_SBUF_0, targetIndex = 0;
	RdrSurfaceObj targets[SBUF_MAXMRT] = {0};
	RdrSurfaceObj depth_target = {0};

	CHECKDEVICELOCK(device);
	CHECKTHREAD;

	buffer_write_mask &= MASK_SBUF_ALL | MASK_SBUF_STENCIL;

	if (!surface)
		return;

	rxbxSetActiveSurfaceFace(device, face);

	// figure out which render targets to set

	if (!buffer_write_mask)
		buffer_write_mask = MASK_SBUF_ALL;

	for (bufIndex = SBUF_0; bufIndex < SBUF_MAXMRT; ++bufIndex, mask <<= 1)
	{
		if ((buffer_write_mask & mask) && surface->rendertarget[bufIndex].d3d_surface.typeless_surface)
		{
			targets[targetIndex] = surface->rendertarget[bufIndex].d3d_surface;
			/* DX11TODO - DJR - finish read-only view support
			if (!surface->bind_count || !surface->rendertarget[bufIndex].d3d_surface_readonly.typeless_surface)
				targets[targetIndex] = surface->rendertarget[bufIndex].d3d_surface;
			else
				targets[targetIndex] = surface->rendertarget[bufIndex].d3d_surface_readonly;
			*/
			++targetIndex;
		}
	}

	if (buffer_write_mask == MASK_SBUF_STENCIL)
	{
		// activate depth target, but turn off depth writes so we modify
		// just the stencil buffer
		buffer_write_mask = MASK_SBUF_DEPTH;
		rxbxDepthWrite(device, false);
	}
	else
		rxbxDepthWrite(device, true);
	if (buffer_write_mask & MASK_SBUF_DEPTH)
	{
		if (device->override_depth_surface)
			depth_target = device->override_depth_surface->rendertarget[SBUF_DEPTH].d3d_surface;
		else
			depth_target = surface->rendertarget[SBUF_DEPTH].d3d_surface;
	}

	// set the render targets
	for (bufIndex = 0; bufIndex < SBUF_MAXMRT; ++bufIndex)
		device->device_state.targets[bufIndex] = targets[bufIndex];
	device->device_state.targets[SBUF_DEPTH] = depth_target;

	if(rxbxShouldApplyTexturesAndStatesTogether()) {
		rxbxApplyTargetsAndTextures(device);
	} else {
		rxbxApplyTargets(device);
	}

	surface->active_buffer_write_mask = buffer_write_mask;

	rxbxResetViewport(device, surface->width_thread, surface->height_thread);
}

void rxbxClearActiveSurfaceDirect(RdrDeviceDX *device, RdrClearParams *params, WTCmdPacket *packet)
{
	DWORD clear_flags=0;
	RdrSurfaceDX *surface = device->active_surface;
	int buffer_count = 0;
	RdrSurfaceBufferMaskBits write_mask = params->bits & MASK_SBUF_ALL;
	RdrSurfaceBufferMaskBits old_write_mask;

	CHECKDEVICELOCK(device);
	CHECKTHREAD;

	if (!surface)
		return;

	etlAddEvent(surface->event_timer, "Clear", ELT_CODE, ELTT_BEGIN);

	old_write_mask = surface->active_buffer_write_mask;
	write_mask = (params->bits & MASK_SBUF_ALL) & old_write_mask;

	if (!surface->rendertarget[SBUF_DEPTH].d3d_surface.typeless_surface)
	{
		write_mask &= ~MASK_SBUF_DEPTH;
		params->bits &= ~CLEAR_STENCIL;
	}

	if (surface->rendertarget[SBUF_DEPTH].d3d_surface.typeless_surface && (params->bits & CLEAR_STENCIL))
		clear_flags |= D3DCLEAR_STENCIL;

	if (!surface->rendertarget[SBUF_0].d3d_surface.typeless_surface && !surface->rendertarget[SBUF_1].d3d_surface.typeless_surface &&
		!surface->rendertarget[SBUF_2].d3d_surface.typeless_surface && !surface->rendertarget[SBUF_3].d3d_surface.typeless_surface)
	{
		write_mask &= ~MASK_SBUF_ALL_COLOR;
	}


	if (write_mask & MASK_SBUF_DEPTH)
	{
		clear_flags |= D3DCLEAR_ZBUFFER;
		buffer_count++;
	}

	if (write_mask & MASK_SBUF_ALL_COLOR)
	{
		clear_flags |= D3DCLEAR_ALLTARGETS;
		if (surface->rendertarget[SBUF_0].d3d_surface.typeless_surface)
			buffer_count++;
		if (surface->rendertarget[SBUF_1].d3d_surface.typeless_surface)
			buffer_count++;
		if (surface->rendertarget[SBUF_2].d3d_surface.typeless_surface)
			buffer_count++;
		if (surface->rendertarget[SBUF_3].d3d_surface.typeless_surface)
			buffer_count++;
	}

	if ( !clear_flags )
	{
		etlAddEvent(surface->event_timer, "Clear", ELT_CODE, ELTT_END);
		return;
	}

	rxbxSetActiveSurfaceBufferWriteMask(device, write_mask, surface->active_face);

#if _PS3
    // rxbxSetActiveSurfaceBufferWriteMask always does rxbxResetViewport
    cellGcmSetScissorInline(device->d3d_device, 0, 0, 4095, 4095);
#endif

	rxbxResetViewportEx(device, device->active_surface->width_thread, device->active_surface->height_thread, true);
	D3DClearSurface(device, clear_flags, params->clear_color, params->clear_depth, 0);
	rxbxResetViewportEx(device, device->active_surface->width_thread, device->active_surface->height_thread, false);
	INC_CLEARS(surface->width_thread, surface->height_thread, buffer_count);

	rxbxSetActiveSurfaceBufferWriteMask(device, old_write_mask, surface->active_face);

	device->active_surface->draw_calls_since_resolve |= (write_mask & MASK_SBUF_ALL_COLOR & (~device->active_surface->auto_resolve_disable_mask));
	{
		RdrSurfaceDX* depth_surf = device->override_depth_surface ? device->override_depth_surface : device->active_surface;
		depth_surf->draw_calls_since_resolve |= (write_mask & MASK_SBUF_DEPTH & (~depth_surf->auto_resolve_disable_mask));
	}
	

	etlAddEvent(surface->event_timer, "Clear", ELT_CODE, ELTT_END);
}

//////////////////////////////////////////////////////////////////////////

static int sameBufferTypes(RdrSurfaceParams *params, RdrSurfaceDX *surface)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(params->buffer_types); i++)
	{
		if (surface->buffer_types[i] != params->buffer_types[i])
			return 0;
	}

	return 1;
}

#if _XBOX
#define RESTRICT __restrict
typedef D3DSURFACE_PARAMETERS * RESTRICT D3D_CREATERENDERTARGETPARAM;
#else
#define RESTRICT
typedef HANDLE * RESTRICT D3D_CREATERENDERTARGETPARAM;
#endif

int rxbxCalcSurfaceMemUsage(int width, int height, int multisample_count, RdrTexFormatObj surface_format)
{
	if (!multisample_count)
		multisample_count = 1;
	return (int)(rxbxGetSurfaceSize(width, height, surface_format) * multisample_count); // * 1.022f); // Inflate for overhead (empirical number based on 1280x720 surface texture);
}

int rxbxCreateRenderTargets(RdrDeviceDX * RESTRICT device, RdrSurfaceDX * RESTRICT surface, 
	const RdrSurfaceParams * RESTRICT params, const RdrTexFormatObj * RESTRICT surface_format,
	int width, int height, int multisample_count, DWORD multisample_quality, D3D_CREATERENDERTARGETPARAM crtParam, int nSurfaceBufCount)
{
	int nSurfaceBuf; 
	int surfaceSize = 0;
	int supports_post_pixel_ops = 1;

	for (nSurfaceBuf = 0; nSurfaceBuf < nSurfaceBufCount; ++nSurfaceBuf)
	{
		HRESULT hr;

		if (device->d3d11_device)
		{
			D3D11_RENDER_TARGET_VIEW_DESC render_target_desc;
			RdrTextureDataDX * surface_texture;
			int set_index=0;
			rxbxSurfaceGrowSnapshotTextures(device, surface, nSurfaceBuf, set_index, "rxbxCreateRenderTargets");

			surface_texture = rxbxMakeTextureForSurface(device, 
				&surface->rendertarget[nSurfaceBuf].textures[set_index].tex_handle,
				surface_format[nSurfaceBuf], surface->params_thread.width, surface->params_thread.height, 
				!!(surface->buffer_types[nSurfaceBuf] & SBT_SRGB), 
				RSBF_RENDERTARGET | RSBF_TEX2D,
				!!(surface->creation_flags & SF_CUBEMAP), 
				0,
				multisample_count);
			supports_post_pixel_ops = supports_post_pixel_ops && 
				device->surface_types_post_pixel_supported[surface->buffer_types[nSurfaceBuf] & SBT_TYPE_MASK];

			surface->rendertarget[nSurfaceBuf].textures[set_index].d3d_texture = surface_texture->texture;
			surface->rendertarget[nSurfaceBuf].textures[set_index].d3d_buffer = surface_texture->d3d11_data;

			render_target_desc.Format = rxbxGetGPUSurfaceFormat11(surface_format[nSurfaceBuf], 
				!!(surface->buffer_types[nSurfaceBuf] & SBT_SRGB));
			render_target_desc.ViewDimension = multisample_count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
			render_target_desc.Texture2D.MipSlice = 0;

			hr = ID3D11Device_CreateRenderTargetView(device->d3d11_device, surface_texture->d3d11_data.resource_d3d11, &render_target_desc,
				&surface->rendertarget[nSurfaceBuf].d3d_surface.render_target_view11);
			rxbxFatalHResultErrorf(device, hr, "creating a render target view", "");
		}
		else
		{
			hr = IDirect3DDevice9_CreateRenderTarget(device->d3d_device, 
				width, height, rxbxGetGPUFormat9(surface_format[ nSurfaceBuf ]),
				multisample_count, multisample_quality, FALSE, &surface->rendertarget[nSurfaceBuf].d3d_surface.surface9, crtParam);
		}

        if (!FAILED(hr))
			rxbxLogCreateSurface(device, surface->rendertarget[nSurfaceBuf].d3d_surface);
		supports_post_pixel_ops = supports_post_pixel_ops && 
			device->surface_types_post_pixel_supported[params->buffer_types[nSurfaceBuf] & SBT_TYPE_MASK];

		rxbxFatalHResultErrorf(device, hr, "creating a render target", "size %d x %d (%s)", 
			width, height, rdrTexFormatName(surface_format[ nSurfaceBuf ]));

		// TODO: If out of memory, call out of memory callback and retry

#if _XBOX
		crtParam->Base += surface->rendertarget[nSurfaceBuf].d3d_surface->Size / GPU_EDRAM_TILE_SIZE;

		surface->rendertarget[nSurfaceBuf].size = 0; // Render targets don't actually take memory, they're virtual (but their textures do)
#else
		surface->rendertarget[nSurfaceBuf].size = rxbxCalcSurfaceMemUsage(width, height, multisample_count, surface_format[nSurfaceBuf]);
		rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:RenderTarget", 1, surface->rendertarget[nSurfaceBuf].size);
#endif
	}
	surface->supports_post_pixel_ops = supports_post_pixel_ops;

	return surfaceSize;
}

static void verifySurfaceTextures(RdrSurfaceDX *surface)
{
	RdrDeviceDX *device = (RdrDeviceDX*)surface->surface_base.device;
	int bufferIndex;

	for (bufferIndex=0; bufferIndex<ARRAY_SIZE(surface->rendertarget); bufferIndex++) {
		int set_index=0;
		for (set_index = 0; set_index < surface->rendertarget[bufferIndex].texture_count; set_index++) {
			RdrTextureDataDX *tex_data = NULL;
			if (surface->rendertarget[bufferIndex].textures[set_index].tex_handle.texture.hash_value) {
				if (stashIntFindPointer(device->texture_data, surface->rendertarget[bufferIndex].textures[set_index].tex_handle.texture.hash_value, &tex_data))
				{
					assert(tex_data->texture.texture_2d_d3d9 == surface->rendertarget[bufferIndex].textures[set_index].d3d_texture.texture_2d_d3d9);
				} else {
					assert(!surface->rendertarget[bufferIndex].textures[set_index].d3d_texture.typeless);
				}
			} else {
				assert(!surface->rendertarget[bufferIndex].textures[set_index].d3d_texture.typeless);
			}
		}
	}
}

void rxbxSurfaceAllocateSets(RdrSurfaceDX *surface, int num_sets, const char *name, int buffer_mask, bool bMakeRenderTargetSnapshot, bool bSRGBSnapshot)
{
	RdrDeviceDX *device = (RdrDeviceDX*)surface->surface_base.device;
	int set_index;
	int bufferIndex;

	verifySurfaceTextures(surface);

	for (bufferIndex = SBUF_0; bufferIndex < SBUF_MAX; bufferIndex++)
	{
		bool srgb = false;

		if (bufferIndex < SBUF_MAXMRT)
		{
#pragma warning(suppress:6201) // /analyze complains about the line below, even with the right ANALYSIS_ASSUME directive
			RdrSurfaceBufferType origBufferType = surface->buffer_types[bufferIndex];
			srgb = !!(origBufferType & SBT_SRGB) || bSRGBSnapshot;
		}

		if (surface->rendertarget[bufferIndex].d3d_surface.typeless_surface) {
			RdrTexFormatObj tex_format = {RTEX_INVALID_FORMAT};
			//if we aren't using render to texture slot 0 isn't the actual rendertarget texture. It's just a snapshot we need to copy the surface
			//to to read it so we can create it here
			//int startIdx = (!surface->use_render_to_texture) ? 0 : 1;

			if (bufferIndex < SBUF_MAXMRT)
			{
				ANALYSIS_ASSUME(bufferIndex < SBUF_MAXMRT);
#pragma warning(suppress:6201) // /analyze complains about the line below, even with the right ANALYSIS_ASSUME directive
				tex_format = rxbxGetTexFormatForSBT(surface->buffer_types[bufferIndex]);
			}
			else if (bufferIndex == SBUF_DEPTH)
				rxbxGetDepthFormat(device, surface->creation_flags, &tex_format);
			else
				assert(0);

#if !PLATFORM_CONSOLE
			if ((surface->creation_flags & SF_DEPTHONLY) && device->null_supported)
			{
				tex_format.format = RTEX_NULL;
			}
#endif

			if (!(buffer_mask & (1 << bufferIndex)))
				continue;

			assert(name && *name);
			if (surface->rendertarget[bufferIndex].texture_count < num_sets) {
				rxbxSurfaceGrowSnapshotTextures(device, surface, bufferIndex, num_sets - 1, name);
			} else {
				surface->rendertarget[bufferIndex].textures[num_sets-1].name = name;
			}
			// only create sets for depth surface if we need a depth texture
			if (bufferIndex == SBUF_DEPTH && (surface->type == SURF_PRIMARY || !(surface->creation_flags & SF_DEPTH_TEXTURE)))
				continue;

			// Just allocate num_sets-1, leave the rest empty
			for (set_index=num_sets-1; set_index<num_sets; set_index++) {
				if (!surface->rendertarget[bufferIndex].textures[set_index].d3d_texture.typeless)
				{
					RdrTextureDataDX * surface_texture;
					RdrSurfaceBindFlags buffer_bind_flags = RSBF_TEX2D;
					int multisample_count = 0;
					if (bufferIndex == SBUF_DEPTH && device->d3d11_device && surface->multisample_count > 1)
					{
						// DX11: auto-resolve snapshot from MSAA depth needs depth-stencil view to write depth during the manual resolve
						buffer_bind_flags |= RSBF_DEPTHSTENCIL;
						if (set_index == 0)
							// if we are creating the surface texture itself, the texture buffer & view must be multi-sampled
							multisample_count = surface->multisample_count;
					}
					else
					if (bufferIndex == SBUF_0 && device->d3d11_device && bMakeRenderTargetSnapshot)
					{
						buffer_bind_flags |= RSBF_RENDERTARGET;
					}

					surface_texture = rxbxMakeTextureForSurface(device,
						&surface->rendertarget[bufferIndex].textures[set_index].tex_handle,
						tex_format, surface->width_thread, surface->height_thread,
						srgb, 
						buffer_bind_flags, 
						bufferIndex != SBUF_DEPTH && (surface->creation_flags & SF_CUBEMAP), 
						0,
						multisample_count);

					assert(surface_texture->texture.typeless);
					surface->rendertarget[bufferIndex].textures[set_index].d3d_texture = surface_texture->texture;
					surface->rendertarget[bufferIndex].textures[set_index].d3d_buffer = surface_texture->d3d11_data;

					if (device->d3d11_device && (buffer_bind_flags & (RSBF_DEPTHSTENCIL|RSBF_RENDERTARGET)))
					{
						if (bufferIndex == SBUF_DEPTH)
						{
							RdrSurfaceBindFlags surf_usage = RSBF_DEPTHSTENCIL;
							D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_desc;
							HRESULT hr;

							depth_stencil_desc.Format = rxbxGetGPUSurfaceFormat11(tex_format, false);
							depth_stencil_desc.ViewDimension = multisample_count > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
							depth_stencil_desc.Flags = 0;
							depth_stencil_desc.Texture2D.MipSlice = 0;
							hr = ID3D11Device_CreateDepthStencilView(device->d3d11_device, surface_texture->d3d11_data.resource_d3d11, &depth_stencil_desc,
								&surface->rendertarget[SBUF_DEPTH].textures[set_index].snapshot_view.depth_stencil_view11);
							rxbxFatalHResultErrorf(device, hr, "creating a snapshot depth-stencil view", "");
						}
						else
						{
							D3D11_RENDER_TARGET_VIEW_DESC render_target_desc = { 0 };
							HRESULT hr;
							render_target_desc.Format = rxbxGetGPUSurfaceFormat11(tex_format, srgb);
							render_target_desc.ViewDimension = multisample_count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
							render_target_desc.Texture2D.MipSlice = 0;

							hr = ID3D11Device_CreateRenderTargetView(device->d3d11_device, surface_texture->d3d11_data.resource_d3d11, &render_target_desc,
								&surface->rendertarget[bufferIndex].textures[set_index].snapshot_view.render_target_view11);
							rxbxFatalHResultErrorf(device, hr, "creating a snapshot render target view", "");
						}
					}
				}
			}
		}
	}

	verifySurfaceTextures(surface);
}

// DEPTH_FORMAT
//
// The ordering logic here is distributed throughout the code.  Search
// for DEPTH_FORMAT to find all the places.
bool rxbxGetDepthFormat(RdrDeviceDX *device, RdrSurfaceFlags surface_creation_flags, RdrTexFormatObj *depth_format)
{
	depth_format->format = RTEX_D24S8;
	if (device->d3d11_device)
	{
		if (!(device->rdr_caps_new.features_supported & FEATURE_24BIT_DEPTH_TEXTURE))
			depth_format->format = RTEX_D16;
		return true;
	}

	#if PLATFORM_CONSOLE
	{
		return true;
	}
	#else
	{
		if (system_specs.videoCardVendorID == VENDOR_NV && (surface_creation_flags & SF_SHADOW_MAP))
		{
			return true;
		}
		else if (device->nvidia_intz_supported)
		{
			depth_format->format = RTEX_NVIDIA_INTZ;
			return true;
		}
		else if (device->nvidia_rawz_supported)
		{
			depth_format->format = RTEX_NVIDIA_RAWZ;
			return true;
		}
		else if (device->ati_df24_supported)
		{
			depth_format->format = RTEX_ATI_DF24;
			return true;
		}
		else if (device->ati_df16_supported)
		{
			depth_format->format = RTEX_ATI_DF24;
			return true;
		}
		return false;
	}
	#endif
}

__forceinline static void rxbxMemlogSurfaceParams(RdrSurfaceDX * surface, const char * label)
{
	if (!rdr_state.disableSurfaceLifetimeLog)
		memlog_printf(NULL, "%s(%p) %s (%d x %d MRT %d MSAA %d/%d Format %s Depth %d %s %s %s)", label, surface, 
			surface->params_thread.name, surface->params_thread.width, surface->params_thread.height,
			rxbxGetSurfaceMRTCount(surface),
			surface->params_thread.desired_multisample_level, surface->params_thread.required_multisample_level,
			rdrGetSurfaceBufferTypeNameString(surface->params_thread.buffer_types[0]),
			surface->params_thread.depth_bits,
			surface->params_thread.flags & SF_DEPTHONLY ? "DepthOnly" : "",
			surface->params_thread.flags & SF_DEPTH_TEXTURE ? "DepthTex" : "",
			surface->params_thread.flags & SF_SHADOW_MAP ? "Shadowmap" : ""
			);
}

void rxbxReinitSurfaceDirect(RdrDeviceDX *device, RdrSurfaceDX *surface)
{
	RdrTexFormatObj surface_format[SBUF_MAXMRT];
	RdrTexFormatObj texture_format[SBUF_MAXMRT];
	int bufferIndex, i, tile_size[2];
#if _XBOX
	D3DSURFACE_PARAMETERS d3d_surface_params = {0};
	D3DSURFACE_PARAMETERS * d3d_createRenderTargetParam = &d3d_surface_params;
#else
	HANDLE * d3d_createRenderTargetParam = NULL;
#endif

	RxbxSurfaceType new_type;
	int multisample_count;
	DWORD multisample_quality;
	int nSurfaceBufCount;
	bool use_render_to_texture = false;
	bool disable_depth_texture = false;
	bool no_color_targets = false;
	bool depth_only = false;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (device->isLost != RDR_OPERATING)
	{
		// wait until device not lost
		rxbxMemlogSurfaceParams(surface, "Skipping Reinit due to lost device.");
		return;
	}

	eaPushUnique(&device->surfaces, surface);

	// if 565 not supported change 565 to 8888
	if (!(device->surface_types_post_pixel_supported[SBT_RGB16] & RSBS_AsSurface))
	{
		for (i = 0; i < SBUF_MAXMRT; ++i)
		{
			if ((surface->params_thread.buffer_types[i] & SBT_TYPE_MASK) == SBT_RGB16)
				surface->params_thread.buffer_types[i] = SBT_RGBA |
				(surface->params_thread.buffer_types[i] & ~SBT_TYPE_MASK);
		}
	}

	assert(surface->type != SURF_PRIMARY);
	if (surface->params_thread.flags & SF_MRT4)
	{
		int bytes = bytesFromSBT(surface->params_thread.buffer_types[0]);
		new_type = SURF_QUADRUPLE;
		for (i = 1; i < 4; i++)
			assert(bytes == bytesFromSBT(surface->params_thread.buffer_types[i]));
		nSurfaceBufCount = 4;

// 		assert(!(surface->params_thread.flags & SF_DEPTHONLY));
// 		assert(!(surface->params_thread.flags & SF_DEPTH_TEXTURE));
	}
	else if (surface->params_thread.flags & SF_MRT2)
	{
		int bytes = bytesFromSBT(surface->params_thread.buffer_types[0]);
		new_type = SURF_DOUBLE;
		assert(bytes == bytesFromSBT(surface->params_thread.buffer_types[1]));
		surface->params_thread.buffer_types[2] = surface->params_thread.buffer_types[0];
		surface->params_thread.buffer_types[3] = surface->params_thread.buffer_types[0];
		nSurfaceBufCount = 2;

// 		assert(!(surface->params_thread.flags & SF_DEPTHONLY));
// 		assert(!(surface->params_thread.flags & SF_DEPTH_TEXTURE));
	}
	else
	{
		new_type = SURF_SINGLE;
		for (i = 1; i < ARRAY_SIZE(surface->params_thread.buffer_types); i++)
			surface->params_thread.buffer_types[i] = surface->params_thread.buffer_types[0];
		nSurfaceBufCount = 1;

		if (surface->params_thread.flags & (SF_DEPTHONLY|SF_DEPTH_TEXTURE))
		{
			surface->params_thread.depth_bits = 24;
#if !PLATFORM_CONSOLE
			if (!rdrSupportsFeature(&device->device_base, FEATURE_DEPTH_TEXTURE_MSAA))
				surface->params_thread.desired_multisample_level = 1;
#endif
		}
	}

	multisample_count = rxbxSurfaceGetMultiSampleCount(surface, &multisample_quality);
	use_render_to_texture = (multisample_count == 0) ? true : false;

	// Determine if we need to re-create this pbuffer or just re-use it
	if (surface->type != SURF_UNINITED
		&& new_type == surface->type
		&& surface->params_thread.flags == surface->creation_flags
		&& sameBufferTypes(&surface->params_thread, surface)
		&& surface->params_thread.width == (U32)surface->width_thread
		&& surface->params_thread.height == (U32)surface->height_thread
		&& multisample_count == surface->multisample_count
		&& multisample_quality == surface->multisample_quality
		&& !rdr_state.pbuftest)
	{
			// Re-use it!
			return;
	}

	rxbxMemlogSurfaceParams(surface, __FUNCTION__);

	etlAddEvent(surface->event_timer, "Init", ELT_RESOURCE, ELTT_INSTANT);

	rxbxResetDepthSurface(surface);

	verifySurfaceTextures(surface);

	rxbxSurfaceFreeRenderTargetTextures(device, surface, false);

	verifySurfaceTextures(surface);

	rxbxRestoreDepthSurface(surface);
#if _XBOX
	SAFE_FREE(surface->tile_rects);
#endif
	surface->creation_flags = surface->params_thread.flags;

	depth_only = !!(surface->creation_flags & SF_DEPTHONLY);

#if _PS3
	no_color_targets = depth_only;
#elif _XBOX
	use_render_to_texture = false;
	no_color_targets = depth_only;
#else
	no_color_targets = false;
	if (!(device->rdr_caps_new.features_supported & FEATURE_DEPTH_TEXTURE))
	{
		disable_depth_texture = true;
	}
#endif

	surface->type = new_type;
	for (i = 0; i < ARRAY_SIZE(surface->params_thread.buffer_types); i++)
	{
		surface->buffer_types[i] = surface->params_thread.buffer_types[i];
		surface_format[i] = rxbxGetTexFormatForSBT(surface->buffer_types[i]);
		texture_format[i] = rxbxGetTexFormatForSBT(surface->buffer_types[i]);

#if !PLATFORM_CONSOLE
		if (depth_only && device->null_supported)
		{
			surface_format[i].format = RTEX_NULL;
			texture_format[i].format = RTEX_NULL;
		}
#endif
#if _XBOX
		// CD: D3D9 uses D3DRS_SRGBWRITEENABLE instead of changing the surface format

		if (surface->buffer_types[i] & SBT_GAMMA_SPACE_WRITES)
		{
			assert(surface_format[i] == D3DFMT_A8B8G8R8 || surface_format[i] == D3DFMT_A2B10G10R10F_EDRAM); // validate that the buffer type is compatable with sRGB conversion
			surface_format[i] = MAKESRGBFMT(surface_format[i]);
		}
#endif
	}

	tile_size[0] = surface->params_thread.width;
	tile_size[1] = surface->params_thread.height;

	if (surface->params_thread.depth_bits)
	{
		int zbufWidth = tile_size[0], zbufHeight = tile_size[1];
		RdrTexFormatObj depth_format = {RTEX_D24S8};
		HRESULT hr;

#if _XBOX
		d3d_surface_params.HierarchicalZBase = 0;
#endif

#if ZPASS_BIGZBUFFER
		if (surface->do_zpass)
		{
			zbufWidth = surface->params_thread.width;
			zbufHeight = surface->params_thread.height;
		}
#endif

		if (disable_depth_texture || (!use_render_to_texture || !(surface->creation_flags & SF_DEPTH_TEXTURE)))
		{
			if (device->d3d11_device)
			{
				RdrSurfaceBindFlags surf_usage = RSBF_DEPTHSTENCIL;
				int tex_flags = RTF_MAG_POINT|RTF_MIN_POINT;
				RdrTextureDataDX * surface_texture;
				D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_desc;
				int set_index=0;

				rxbxSurfaceGrowSnapshotTextures(device, surface, SBUF_DEPTH, set_index, "rxbxReinitSurfaceDirect");

				if (device->d3d11_feature_level >= D3D_FEATURE_LEVEL_10_1)
					// DX11: auto-resolve snapshot needs texture view to read depth during the manual resolve
					surf_usage |= RSBF_TEX2D;

				surface_texture = rxbxMakeTextureForSurface(device,
					&surface->rendertarget[SBUF_DEPTH].textures[0].tex_handle,
					depth_format,
					surface->params_thread.width, surface->params_thread.height, 0,
					surf_usage, 
					false, 
					tex_flags,
					multisample_count);
				surface->rendertarget[SBUF_DEPTH].textures[set_index].d3d_texture = surface_texture->texture;
				surface->rendertarget[SBUF_DEPTH].textures[set_index].d3d_buffer = surface_texture->d3d11_data;

				depth_stencil_desc.Format = rxbxGetGPUSurfaceFormat11(depth_format, false);
				depth_stencil_desc.ViewDimension = multisample_count > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
				depth_stencil_desc.Flags = 0;
				depth_stencil_desc.Texture2D.MipSlice = 0;
				hr = ID3D11Device_CreateDepthStencilView(device->d3d11_device, surface_texture->d3d11_data.resource_d3d11, &depth_stencil_desc,
					&surface->rendertarget[SBUF_DEPTH].d3d_surface.depth_stencil_view11);
				rxbxFatalHResultErrorf(device, hr, "creating a depth-stencil view", "");

				if (device->d3d11_feature_level > D3D_FEATURE_LEVEL_10_1)
				{
 					depth_stencil_desc.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
 					hr = ID3D11Device_CreateDepthStencilView(device->d3d11_device, surface_texture->d3d11_data.resource_d3d11, &depth_stencil_desc,
 						&surface->rendertarget[SBUF_DEPTH].d3d_surface_readonly.depth_stencil_view11);
 					rxbxFatalHResultErrorf(device, hr, "creating a read-only depth-stencil view", "");
				}
			}
			else
				hr = IDirect3DDevice9_CreateDepthStencilSurface(device->d3d_device, 
					zbufWidth, zbufHeight, rxbxGetGPUFormat9(depth_format), multisample_count, multisample_quality, FALSE, 
					&surface->rendertarget[SBUF_DEPTH].d3d_surface.surface9, d3d_createRenderTargetParam);

			rxbxFatalHResultErrorf(device, hr, "creating a depth/stencil surface", "");
			if (!FAILED(hr))
				rxbxLogCreateSurface(device, surface->rendertarget[SBUF_DEPTH].d3d_surface);

			surface->rendertarget[SBUF_DEPTH].size = rxbxCalcSurfaceMemUsage(zbufWidth, zbufHeight, multisample_count, depth_format);
			rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:RenderTargetDepth", 1, surface->rendertarget[SBUF_DEPTH].size);
		} else {
			RdrSurfaceBindFlags surf_usage = RSBF_DEPTHSTENCIL;
			int tex_flags = RTF_MAG_POINT|RTF_MIN_POINT;
			int set_index=0;
			RdrTextureDataDX * surface_texture;

			rxbxSurfaceGrowSnapshotTextures(device, surface, SBUF_DEPTH, set_index, "rxbxReinitSurfaceDirect");

			if (rxbxGetDepthFormat(device, surface->creation_flags, &depth_format))
			{
#if !_XBOX
				surf_usage = RSBF_DEPTHSTENCIL | RSBF_TEX2D;
				if (surface->creation_flags & SF_DEPTHONLY)
					tex_flags = 0;
#else
				if (surface->creation_flags & SF_DEPTHONLY)
				{
					surf_usage = RSBF_DEPTHSTENCIL | RSBF_TEX2D;
					tex_flags = 0;
				}
#endif
			}


			surface_texture = rxbxMakeTextureForSurface(device,
				&surface->rendertarget[SBUF_DEPTH].textures[set_index].tex_handle,
				depth_format,
				surface->params_thread.width, surface->params_thread.height, 0,
				surf_usage, 
				false, 
				tex_flags,
				multisample_count);
			surface->rendertarget[SBUF_DEPTH].textures[set_index].d3d_texture = surface_texture->texture;
			surface->rendertarget[SBUF_DEPTH].textures[set_index].d3d_buffer = surface_texture->d3d11_data;

#if !_XBOX
			if (device->d3d11_device)
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_desc;
				depth_stencil_desc.Format = rxbxGetGPUSurfaceFormat11(depth_format, false);
				depth_stencil_desc.ViewDimension = multisample_count > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
				depth_stencil_desc.Flags = 0;
				depth_stencil_desc.Texture2D.MipSlice = 0;
				hr = ID3D11Device_CreateDepthStencilView(device->d3d11_device, surface_texture->d3d11_data.resource_d3d11, &depth_stencil_desc,
					&surface->rendertarget[SBUF_DEPTH].d3d_surface.depth_stencil_view11);
				rxbxFatalHResultErrorf(device, hr, "creating a depth-stencil view", "");
			}
			else
			{
				assert(!surface->rendertarget[SBUF_DEPTH].d3d_surface.typeless_surface);
				CHECKX(IDirect3DTexture9_GetSurfaceLevel(surface->rendertarget[SBUF_DEPTH].textures[0].d3d_texture.texture_2d_d3d9, 0, &surface->rendertarget[SBUF_DEPTH].d3d_surface.surface9));
			}
			rxbxLogCreateSurface(device, surface->rendertarget[SBUF_DEPTH].d3d_surface);

#endif
			// This is tracked as a texture, not a rendertarget
			surface->rendertarget[SBUF_DEPTH].size = 0;
		}
	}


	if (depth_only && device->null_supported) //the nvidia null surfaces must be made through CreateRenderTargets
	{
		rxbxCreateRenderTargets(device, surface, &surface->params_thread, surface_format, 
			tile_size[0], tile_size[1], multisample_count, multisample_quality, d3d_createRenderTargetParam, nSurfaceBufCount);
	}
	else if (!no_color_targets)
	{
		if (!use_render_to_texture)
		{
			rxbxCreateRenderTargets(device, surface, &surface->params_thread, surface_format, 
				tile_size[0], tile_size[1], multisample_count, multisample_quality, d3d_createRenderTargetParam, nSurfaceBufCount);
		}
		else
		{
			int supports_post_pixel_ops = 1;

			for (bufferIndex=0; bufferIndex<nSurfaceBufCount; bufferIndex++)
			{
				RdrTextureDataDX * surface_texture;
				int set_index=0;
				rxbxSurfaceGrowSnapshotTextures(device, surface, bufferIndex, set_index, "rxbxReinitSurfaceDirect");

				surface_texture = rxbxMakeTextureForSurface(device, 
					&surface->rendertarget[bufferIndex].textures[set_index].tex_handle,
					texture_format[bufferIndex], surface->params_thread.width, surface->params_thread.height, 
					!!(surface->buffer_types[bufferIndex] & SBT_SRGB), 
					RSBF_RENDERTARGET | RSBF_TEX2D,
					!!(surface->creation_flags & SF_CUBEMAP), 
					0,
					multisample_count);
				supports_post_pixel_ops = supports_post_pixel_ops && 
					device->surface_types_post_pixel_supported[surface->buffer_types[bufferIndex] & SBT_TYPE_MASK];

				surface->rendertarget[bufferIndex].textures[set_index].d3d_texture = surface_texture->texture;
				surface->rendertarget[bufferIndex].textures[set_index].d3d_buffer = surface_texture->d3d11_data;
				if (device->d3d11_device)
				{
					HRESULT hr;
					D3D11_RENDER_TARGET_VIEW_DESC render_target_desc;
					render_target_desc.Format = rxbxGetGPUSurfaceFormat11(texture_format[bufferIndex], 
						!!(surface->buffer_types[bufferIndex] & SBT_SRGB));
					render_target_desc.ViewDimension = multisample_count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
					render_target_desc.Texture2D.MipSlice = 0;

					hr = ID3D11Device_CreateRenderTargetView(device->d3d11_device, surface_texture->d3d11_data.resource_d3d11, &render_target_desc,
						&surface->rendertarget[bufferIndex].d3d_surface.render_target_view11);
					rxbxFatalHResultErrorf(device, hr, "creating a render target view", "");
				}
				else
				{
					if (surface->creation_flags & SF_CUBEMAP)
					{
						CHECKX(IDirect3DCubeTexture9_GetCubeMapSurface(surface->rendertarget[bufferIndex].textures[0].d3d_texture.texture_cube_d3d9, surface->active_face, 0, &surface->rendertarget[bufferIndex].d3d_surface.surface9));
					}
					else
					{
						CHECKX(IDirect3DTexture9_GetSurfaceLevel(surface->rendertarget[bufferIndex].textures[0].d3d_texture.texture_2d_d3d9, 0, &surface->rendertarget[bufferIndex].d3d_surface.surface9));
					}
				}
				rxbxLogCreateSurface(device, surface->rendertarget[bufferIndex].d3d_surface);
			}

			surface->supports_post_pixel_ops = supports_post_pixel_ops;
		}
	}

	surface->width_thread = surface->params_thread.width;
	surface->height_thread = surface->params_thread.height;
	surface->multisample_count = multisample_count;
	surface->multisample_quality = multisample_quality;
	surface->use_render_to_texture = use_render_to_texture;

	// CD: this was supposed to fix a luminance texture bug, but I don't think it is needed anymore
	if (1)
	{
		RdrClearParams clear_params = {0};
		RdrSurfaceDX *active_surface = device->active_surface;
		rxbxSetSurfaceActiveDirectSimple(device, surface);

		clear_params.bits = CLEAR_ALL;
		clear_params.clear_depth = 1;
		rxbxClearActiveSurfaceDirect(device, &clear_params, NULL);

		rxbxSetSurfaceActiveDirectSimple(device, active_surface);
	}

	verifySurfaceTextures(surface);
}

void rxbxInitSurfaceDirect(RdrDeviceDX *device, RxbxSurfaceParams *xparams, WTCmdPacket *packet)
{
	RdrSurfaceDX *surface = xparams->surface;
	surface->params_thread = xparams->params;
	rxbxReinitSurfaceDirect( device, surface );
}

void rxbxSurfaceFreeRenderTargetTextures(RdrDeviceDX *device, RdrSurfaceDX *surface, bool free_texture_snapshot_arrays)
{
	int bufferIndex, i;

	for (bufferIndex = 0; bufferIndex < ARRAY_SIZE(surface->rendertarget); bufferIndex++)
	{
		RxbxSurfacePerTargetState * target = surface->rendertarget + bufferIndex;
		if (target->d3d_surface.typeless_surface)
		{
			rxbxReleaseSurface(device, &target->d3d_surface);
			rdrTrackUserMemoryDirect(&device->device_base, (bufferIndex==SBUF_DEPTH)?"VideoMemory:RenderTargetDepth":"VideoMemory:RenderTarget", 1, -target->size);
		}

		if(target->d3d_surface_readonly.depth_stencil_view11) {
			ID3D11DepthStencilView_Release(target->d3d_surface_readonly.depth_stencil_view11);
			target->d3d_surface_readonly.depth_stencil_view11 = NULL;
		}

		for (i=target->texture_count-1; i>=0; i--)
		{
			RdrPerSnapshotState * snap = target->textures + i;
			if (snap->snapshot_view.depth_stencil_view11)
			{
				ID3D11DepthStencilView_Release(snap->snapshot_view.depth_stencil_view11);
				snap->snapshot_view.depth_stencil_view11 = NULL;
			}

			if (snap->d3d_texture.typeless) {
				if (!target->borrowed_texture)
					rxbxFreeSurfaceTexture(device, snap->tex_handle, snap->d3d_texture);
				ZeroStructForce(&snap->tex_handle);
				snap->d3d_texture.typeless = NULL;
			} else {
				if(snap->d3d_buffer.typeless) {

					RdrTextureDataDX *tex_data = NULL;

					ID3D11Texture2D_Release(snap->d3d_buffer.texture_2d_d3d11);
					snap->d3d_buffer.texture_2d_d3d11 = NULL;

					rxbxFreeTexData(device, snap->tex_handle);

					ZeroStructForce(&snap->tex_handle);
				}
			}
		}
		if (free_texture_snapshot_arrays && target->textures)
		{
			if (target->textures)
			{
				ANALYSIS_ASSUME(target->textures);
				free(target->textures);
				target->textures = NULL;
			}
			target->texture_count = 0;
		}
		target->borrowed_texture = false;
	}
	surface->nv_depth_handle = surface->nv_tex_handle = NULL;
}

void rxbxSurfaceCleanupDirect(RdrDeviceDX *device,RdrSurfaceDX *surface, WTCmdPacket *packet)
{
	rxbxResetDepthSurface(surface);
	rxbxSurfaceFreeRenderTargetTextures(device, surface, false);

	surface->type = SURF_UNINITED;
	surface->state_inited = 0;
}

void rxbxFreeSurfaceDirect(RdrDeviceDX *device, RdrSurfaceDX **surface_ptr, WTCmdPacket *packet)
{
	RdrSurfaceDX *surface = *surface_ptr;

	rxbxMemlogSurfaceParams(surface, __FUNCTION__);

	etlAddEvent(surface->event_timer, "Free", ELT_RESOURCE, ELTT_INSTANT);

	if (device->active_surface == surface)
		rxbxUnsetSurfaceActiveDirect(device);

	rdrRemoveSurfaceTexHandle((RdrSurface *)surface);

	rxbxResetDepthSurface(surface);

	rxbxSurfaceFreeRenderTargetTextures(device, surface, true);

#if _XBOX
    SAFE_FREE(surface->tile_rects);
#endif

	etlFreeEventOwner(surface->event_timer);
	surface->event_timer = NULL;

	ZeroStruct(surface);
	eaFindAndRemoveFast(&device->surfaces, surface);
	free(surface);
}

#if _XBOX
static void rxbxStartPredication(RdrDeviceDX *device)
{
	RdrSurfaceDX *surface = device->active_surface;

	rxbxDisownManagedParams(device);

	if (surface->tile_count > 1)
	{
		DWORD tiling_flags = D3DTILING_SKIP_FIRST_TILE_CLEAR;
		if (surface->width_thread <= 1280 && surface->height_thread <= 720 && 
			surface->multisample_count == 2)
			tiling_flags |= D3DTILING_ONE_PASS_ZPASS;
		IDirect3DDevice9_BeginTiling(device->d3d_device, tiling_flags, surface->tile_count, surface->tile_rects, NULL, 0, 0);
	}

	rxbxResetViewport(device, surface->width_thread, surface->height_thread);

	rxbxSetPredicationConstantsAndManagedParams(device);
}
#endif


void rxbxSetSurfaceActiveDirect(RdrDeviceDX *device, RdrSurfaceActivateParams *params)
{
	RdrSurfaceDX *surface = (RdrSurfaceDX*)SAFE_MEMBER(params, surface);
	RdrSurfaceBufferMaskBits write_mask = SAFE_MEMBER(params, write_mask);
	RdrSurfaceFace face = SAFE_MEMBER(params, face);
	RdrSurfaceDX *prev_active_surface = device->active_surface;
	bool needs_resolve, same_surface;

	CHECKDEVICELOCK(device);
	if (!device->hWindow)
		return;

	if (!surface)
	{
		rxbxUnsetSurfaceActiveDirect(device);
		return;
	}

	assert(surface->surface_base.device == ((RdrDevice *)device));

	same_surface = prev_active_surface == surface;
	needs_resolve = !same_surface || ((surface->creation_flags & SF_CUBEMAP) && surface->active_face != face);
	if (!needs_resolve && surface->active_buffer_write_mask == write_mask)
		return;

	if (!device->primary_surface.rendertarget[0].d3d_surface.typeless_surface) {
		if (!rdr_state.disableSurfaceLifetimeLog)
			memlog_printf(0, "SetSurfaceActive failing due to no primary surface Device:0x%08p Surface:0x%08p", device, surface);
		return;
	}

	INC_SURFACE_ACTIVE();

	if (prev_active_surface && needs_resolve)
		rxbxUnsetSurfaceActiveDirect(device);

	if ( surface->type == SURF_UNINITED )
	{
		if (!rdr_state.disableSurfaceLifetimeLog)
			memlog_printf(0, "Setting uninited surface active Device:0x%08p Surface:0x%08p", device, surface);
		rxbxReinitSurfaceDirect( device, surface );
	}

	if (needs_resolve)
		etlAddEvent(surface->event_timer, "Active", ELT_RESOURCE, ELTT_BEGIN);

	device->active_surface = surface;
	if (surface->multisample_count <= 1)
		// disable alpha-to-coverage when MSAA is off
		device->material_disable_flags |= RMATERIAL_ALPHA_TO_COVERAGE;
	else
		device->material_disable_flags &= ~RMATERIAL_ALPHA_TO_COVERAGE;

#if _PS3 && _DEBUG && 0
    {
        const char *name = surface->params_thread.name;
        if(!name || !name[0])
            name = surface->surface_base.params_nonthread.name;
        if(!name || !name[0])
            cellGcmSetPerfMonMarker(device->d3d_device, "unnamed surface");
        else
            cellGcmSetPerfMonMarker(device->d3d_device, name);
    }
#endif

	if (needs_resolve)
	{
		rxbxSetStateActive(device, &surface->state, !surface->state_inited, surface->width_thread, surface->height_thread);
		surface->state_inited = 1;
	}
	rxbxSetActiveSurfaceBufferWriteMask(device, write_mask, face);

#if !_PS3

#if _XBOX
	if (needs_resolve)
	{
		rxbxStartPredication(device);
		if (surface->do_zpass)
			IDirect3DDevice9_BeginZPass(device->d3d_device, 0);
	}
#endif

#if _XBOX
	IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_HIGHPRECISIONBLENDENABLE, surface->buffer_types[0] == SBT_RGBA10);
	IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_HIGHPRECISIONBLENDENABLE1, surface->buffer_types[1] == SBT_RGBA10);
	IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_HIGHPRECISIONBLENDENABLE2, surface->buffer_types[2] == SBT_RGBA10);
	IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_HIGHPRECISIONBLENDENABLE3, surface->buffer_types[3] == SBT_RGBA10);
#endif

	if (device->d3d_device) {
		CHECKX(IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_SRGBWRITEENABLE, (surface->buffer_types[0] & SBT_SRGB) != 0));
	}
#endif
}

void rxbxOverrideDepthSurfaceDirect(RdrDeviceDX *device, RdrSurface **override_depth_surface, WTCmdPacket *packet)
{
	RdrSurfaceDX *surface = (RdrSurfaceDX*)*override_depth_surface;
	
	CHECKDEVICELOCK(device);
	device->override_depth_surface = surface;

	if ( surface && surface->type == SURF_UNINITED )
	{
		if (!rdr_state.disableSurfaceLifetimeLog)
			memlog_printf(0, "Setting uninited surface active Device:0x%08p Surface:0x%08p", device, surface);
		rxbxReinitSurfaceDirect( device, surface );
	}

	// if the depth surface has changed, we may need to update the disabled stencil
	rxbxSetStateActive(device, &device->active_surface->state, !device->active_surface->state_inited, device->active_surface->width_thread, device->active_surface->height_thread);
	device->active_surface->state_inited = 1;
	
	rxbxSetActiveSurfaceBufferWriteMask(device, device->active_surface->active_buffer_write_mask, device->active_surface->active_face);
}

#if DEBUG_LOG_MRT_TO_DISK
int g_bDebugLogPPEffects = 0;

static DWORD numberOfPixelsDrawn;

void rxbxDumpSurfaces( RdrDeviceDX *device )
{
	rxbxFlushGPUDirect(device, NULL, NULL);

#if _PS3
#else
	if ( device->active_surface->rendertarget[SBUF_0].d3d_surface )
		D3DXSaveSurfaceToFile( "c:\\temp\\sbuf_0.bmp", 
			D3DXIFF_BMP,
			device->active_surface->rendertarget[SBUF_0].d3d_surface,
			NULL, NULL );
	if ( device->active_surface->rendertarget[SBUF_1].d3d_surface )
		D3DXSaveSurfaceToFile( "c:\\temp\\sbuf_1.bmp", 
			D3DXIFF_BMP,
			device->active_surface->rendertarget[SBUF_1].d3d_surface,
			NULL, NULL );
	if ( device->active_surface->rendertarget[SBUF_2].d3d_surface )
		D3DXSaveSurfaceToFile( "c:\\temp\\sbuf_2.bmp", 
			D3DXIFF_BMP,
			device->active_surface->rendertarget[SBUF_2].d3d_surface,
			NULL, NULL );
	if ( device->active_surface->rendertarget[SBUF_3].d3d_surface )
		D3DXSaveSurfaceToFile( "c:\\temp\\sbuf_3.bmp", 
			D3DXIFF_BMP,
			device->active_surface->rendertarget[SBUF_3].d3d_surface,
			NULL, NULL );
	if ( device->active_surface->rendertarget[SBUF_DEPTH].d3d_surface )
		D3DXSaveSurfaceToFile( "c:\\temp\\sbuf_depth.bmp", 
			D3DXIFF_BMP,
			device->active_surface->rendertarget[SBUF_DEPTH].d3d_surface,
			NULL, NULL );
#endif
}
#endif

// DX11TODO - DJR - surface format debug
#define SURFACE_FORMAT_DEBUG 1

#if !_XBOX
static void D3D11ResolveOrCopyResource(RdrDeviceDX * device, 
	RdrTextureBufferObj pDstTexture, const D3D11_TEXTURE2D_DESC *tex_desc,
	RdrTextureBufferObj pSrcResource, const D3D11_TEXTURE2D_DESC *src_tex_desc,
	int width, int height);

void D3D11ResolveDepthSurface(RdrDeviceDX * device, RdrSurfaceObj pSrcSurface, RdrTextureBufferObj pDstTexture, RdrSurfaceFace face, int width, int height, bool bIsCubemapTexture)
{
	RdrTextureBufferObj pSrcResource = { NULL };
	D3D11_DEPTH_STENCIL_VIEW_DESC src_desc;
	D3D11_TEXTURE2D_DESC tex_desc;
	D3D11_TEXTURE2D_DESC src_tex_desc;
	ID3D11DepthStencilView_GetResource(pSrcSurface.depth_stencil_view11, &pSrcResource.resource_d3d11);

#if SURFACE_FORMAT_DEBUG
	// DX11TODO - DJR - surface format debug
	// DX11TODO DJR can we pass in instead of querying from surface?
	ID3D11DepthStencilView_GetDesc(pSrcSurface.depth_stencil_view11, &src_desc);
	ID3D11Texture2D_GetDesc(pDstTexture.texture_2d_d3d11, &tex_desc);
	ID3D11Texture2D_GetDesc(pSrcResource.texture_2d_d3d11, &src_tex_desc);
#endif

	D3D11ResolveOrCopyResource(device, pDstTexture, &tex_desc, pSrcResource, &src_tex_desc, width, height);

	ID3D11Resource_Release(pSrcResource.resource_d3d11);
}

static void rxbxSetResolveSurfaceParams(ResolveSurfaceInfo *params, 
	const D3D11_TEXTURE2D_DESC * tex_desc, const D3D11_TEXTURE2D_DESC * src_tex_desc,
	int width, int height)
{
	params->tex_desc = *tex_desc;
	params->src_tex_desc = *src_tex_desc;
	params->width = width;
	params->height = height;
}

void D3D11ResolveSurface(RdrDeviceDX * device, RdrSurfaceObj pSrcSurface, RdrTextureBufferObj pDstTexture, RdrSurfaceFace face, int width, int height, bool bIsCubemapTexture)
{
	RdrTextureBufferObj pSrcResource = { NULL };
	D3D11_RENDER_TARGET_VIEW_DESC src_desc;
	D3D11_TEXTURE2D_DESC tex_desc;
	D3D11_TEXTURE2D_DESC src_tex_desc;
	ID3D11RenderTargetView_GetResource(pSrcSurface.render_target_view11, &pSrcResource.resource_d3d11);

#if SURFACE_FORMAT_DEBUG
	// DX11TODO - DJR - surface format debug
	// DX11TODO DJR can we pass in instead of querying from surface?
	ID3D11RenderTargetView_GetDesc(pSrcSurface.render_target_view11, &src_desc);
	ID3D11Texture2D_GetDesc(pDstTexture.texture_2d_d3d11, &tex_desc);
	ID3D11Texture2D_GetDesc(pSrcResource.texture_2d_d3d11, &src_tex_desc);
#endif

	D3D11ResolveOrCopyResource(device, pDstTexture, &tex_desc, pSrcResource, &src_tex_desc, width, height);

	ID3D11Resource_Release(pSrcResource.resource_d3d11);
}

static void D3D11ResolveOrCopyResource(RdrDeviceDX * device, 
	RdrTextureBufferObj pDstTexture, const D3D11_TEXTURE2D_DESC *tex_desc,
	RdrTextureBufferObj pSrcResource, const D3D11_TEXTURE2D_DESC *src_tex_desc,
	int width, int height)
{
	if (src_tex_desc->SampleDesc.Count != 1)
	{
		assert(width == -1 && height == -1);
		if (src_tex_desc->Width != tex_desc->Width || src_tex_desc->Height != tex_desc->Height)
		{
			if (!device->bResolveSubresourceValidationErrorIssued)
			{
				device->bResolveSubresourceValidationErrorIssued = true;
				rxbxSetResolveSurfaceParams(&device->lastResolveSubresourceParams, tex_desc, src_tex_desc, width, height);
				ErrorDetailsf("Dst (%d x %d, Fmt %d); Src (%d x %d Fmt %d)",
					tex_desc->Width, tex_desc->Height, tex_desc->Format, src_tex_desc->Width, src_tex_desc->Height, src_tex_desc->Format);
				Errorf("ResolveSubresource resource size mismatch. ResolveSubresource may fail and potentially trigger device removal.");
			}
		}
		ID3D11DeviceContext_ResolveSubresource(device->d3d11_imm_context, pDstTexture.resource_d3d11, 0, pSrcResource.resource_d3d11, 0, tex_desc->Format);
	}
	else
	if (width >= 0 && height >= 0)
	{
		// IMPORTANT! Clamp the source region, because attempting to access outside the bounds
		// of the source resource causes a serious device failure and the device will be "removed"
		// inside the driver. This leads to DXGI_ERROR_DEVICE_REMOVED errors in later DX calls, and
		// is currently an unrecoverable error.
		D3D11_BOX src_box = {0, 0, 0, width, height, 1};
		if (width > (int)src_tex_desc->Width || height > (int)src_tex_desc->Height)
		{
			if (!device->bCopySubresourceRegionValidationErrorIssued)
			{
				device->bCopySubresourceRegionValidationErrorIssued = true;
				rxbxSetResolveSurfaceParams(&device->lastCopySubresourceRegionParams, tex_desc, src_tex_desc, width, height);
				ErrorDetailsf("Dst (%d x %d, Fmt %d); Src (%d x %d Fmt %d) resource bounds (%d,%d)-(%d, %d)",
					tex_desc->Width, tex_desc->Height, tex_desc->Format, src_tex_desc->Width, src_tex_desc->Height, src_tex_desc->Format,
					0, 0, width, height);
				Errorf("CopySubresourceRegion size mismatch. Bounds will be clamped, but CopySubresourceRegion may still fail and trigger device removal.");
			}
			MIN1(width, (int)src_tex_desc->Width);
			MIN1(height, (int)src_tex_desc->Height);
			src_box.right = width;
			src_box.bottom = height;
		}
		ID3D11DeviceContext_CopySubresourceRegion(device->d3d11_imm_context, pDstTexture.resource_d3d11, 0, 0, 0, 0, pSrcResource.resource_d3d11, 0, &src_box);
	}
	else
	{
		if (src_tex_desc->Width != tex_desc->Width || src_tex_desc->Height != tex_desc->Height)
		{
			if (!device->bCopyResourceValidationErrorIssued)
			{
				device->bCopyResourceValidationErrorIssued = true;
				rxbxSetResolveSurfaceParams(&device->lastCopyResourceParams, tex_desc, src_tex_desc, width, height);
				ErrorDetailsf("Dst (%d x %d, Fmt %d); Src (%d x %d Fmt %d)",
					tex_desc->Width, tex_desc->Height, tex_desc->Format, src_tex_desc->Width, src_tex_desc->Height, src_tex_desc->Format);
				Errorf("CopyResource resource size mismatch. CopyResource may fail and potentially trigger device removal.");
			}
		}
		ID3D11DeviceContext_CopyResource(device->d3d11_imm_context, pDstTexture.resource_d3d11, pSrcResource.resource_d3d11);
	}
}

void D3D9ResolveSurface(RdrDeviceDX * device, RdrSurfaceObj pSrcSurface, RdrTextureObj pDstTexture, RdrSurfaceFace face, int width, int height, bool bIsCubemapTexture)
{
	HRESULT hr;
	IDirect3DDevice9 * d3d_device = device->d3d_device;
	IDirect3DSurface9 * pTextureSrf = NULL;

#if _PS3
    {
        uint32_t bpp = 4;

        assert(pSrcSurface->format == pDstTexture->format);
        assert(GCM_LOCATION(pSrcSurface->ppixels) == CELL_GCM_LOCATION_LOCAL);
        assert(GCM_LOCATION(pDstTexture->ppixels) == CELL_GCM_LOCATION_LOCAL);

        assert(pSrcSurface->width == pDstTexture->t.width);
        assert(pSrcSurface->height == pDstTexture->t.height);

        switch(pSrcSurface->format) {

        case D3DFMT_LIN_D24S8:
        case D3DFMT_LIN_A8R8G8B8:
        case D3DFMT_LIN_X8R8G8B8:
            break;

        default:
            assert(!"unsupported format");
        }

        cellGcmSetTransferImage(d3d_device,
            CELL_GCM_TRANSFER_LOCAL_TO_LOCAL,
            GCM_OFFSET_LOCAL(pDstTexture->ppixels),
            pDstTexture->t.pitch,
            0,
            0,
            GCM_OFFSET_LOCAL(pSrcSurface->ppixels),
            pSrcSurface->pitch,
            0,
            0,
            pSrcSurface->width,
            pSrcSurface->height,
            bpp
        );

        pDstTexture->is_valid = 1;
    }
#else
	if (bIsCubemapTexture)
	{
		hr = IDirect3DCubeTexture9_GetCubeMapSurface(pDstTexture.texture_cube_d3d9, face, 0, &pTextureSrf);
	}
	else
	{
		hr = IDirect3DTexture9_GetSurfaceLevel(pDstTexture.texture_2d_d3d9, 0, &pTextureSrf);
	}

	if (pTextureSrf)
	{
		if (width > 0 && height > 0)
		{
			RECT rect = { 0, 0, width, height };
			hr = IDirect3DDevice9_StretchRect(d3d_device, pSrcSurface.surface9, &rect, pTextureSrf, NULL, D3DTEXF_POINT);
		}
		else
		{
			hr = IDirect3DDevice9_StretchRect(d3d_device, pSrcSurface.surface9, NULL, pTextureSrf, NULL, D3DTEXF_POINT);
		}
#if _FULLDEBUG
		if (FAILED(hr) && 0) // DX11TODO: This is firing because we are resolving a depth buffer on DX9
		{
			D3DSURFACE_DESC src_desc, dst_desc;
			CHECKX(IDirect3DSurface9_GetDesc(pSrcSurface.surface9, &src_desc));
			CHECKX(IDirect3DSurface9_GetDesc(pTextureSrf, &dst_desc));
			OutputDebugStringf("StretchRect failed %s\n", rxbxGetStringForHResult(hr));
		}
#endif
		IDirect3DSurface9_Release(pTextureSrf);
	}
#endif

}


void rxbxResolve(RdrDeviceDX * device, RdrSurfaceObj pSrcSurface, RdrPerSnapshotState * snapshot, RdrSurfaceFace face, int width, int height, bool bIsCubemapTexture)
{
	if (device->d3d11_device)
		D3D11ResolveSurface(device, pSrcSurface, snapshot->d3d_buffer, face, width, height, bIsCubemapTexture);
	else
		D3D9ResolveSurface(device, pSrcSurface, snapshot->d3d_texture, face, width, height, bIsCubemapTexture);
}

void rxbxResolveDepth(RdrDeviceDX * device, RdrSurfaceObj pSrcSurface, RdrPerSnapshotState * snapshot, RdrSurfaceFace face, int width, int height, bool bIsCubemapTexture)
{
	if (device->d3d11_device)
		D3D11ResolveDepthSurface(device, pSrcSurface, snapshot->d3d_buffer, face, width, height, bIsCubemapTexture);
	else
		D3D9ResolveSurface(device, pSrcSurface, snapshot->d3d_texture, face, width, height, bIsCubemapTexture);
}

void rxbxResolveToTexture(RdrDeviceDX * device, RdrSurfaceObj pSrcSurface, RdrTextureDataDX * pDstTexture, RdrSurfaceFace face, int width, int height, bool bIsCubemapTexture)
{
	if (device->d3d11_device)
		D3D11ResolveSurface(device, pSrcSurface, pDstTexture->d3d11_data, face, width, height, bIsCubemapTexture);
	else
		D3D9ResolveSurface(device, pSrcSurface, pDstTexture->texture, face, width, height, bIsCubemapTexture);
}

// TODO DX11 DJR - disable/remove/move to standard feature location after DX11 MSAA porting is "done"
static int d3d11_disable_msaa_depth_resolve = 0;
// Use to test if DX11 MSAA resolve code path is hammering render states.
AUTO_CMD_INT(d3d11_disable_msaa_depth_resolve, d3d11_disable_msaa_depth_resolve) ACMD_CATEGORY(DEBUG) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;

// TODO DX11 DJR - DJR fix resource hazards under DX11 debug non-MSAA

// Used in preserving and restoring matrix information for the vertex shader after MSAADepth has been resolved.
#define VS_MATRIX_REGISTER_START 7
#define VS_MATRIX_REGISTER_END 30

int rdrDisableStateChangeForMSAADepthResolve = 1;
AUTO_CMD_INT(rdrDisableStateChangeForMSAADepthResolve, rdrDisableStateChangeForMSAADepthResolve) ACMD_CATEGORY(DEBUG) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

void D3DResolveMSAADepth(RdrDeviceDX * device, RdrSurfaceDX *pSrcSurface, int bufferIndex, int snapshotIndex)
{
#if !PLATFORM_CONSOLE
	if (device->d3d11_device)
	{
		if (pSrcSurface->multisample_count > 1)
		{
			// very invalid to request asking DX10 mode to resolve MSAA depth
			if (device->d3d11_feature_level >= D3D_FEATURE_LEVEL_10_1 && !d3d11_disable_msaa_depth_resolve)
			{
				ID3D11RenderTargetView * null_targets[SBUF_MAXMRT] = { 0 };
				ID3D11DepthStencilView * null_depthbuf = NULL;
				RdrPostprocessExVertex vertices[3] = { 0 };
				float w = pSrcSurface->width_thread, h = pSrcSurface->height_thread;
				const RdrPerSnapshotState * dest_texture = pSrcSurface->rendertarget[SBUF_DEPTH].textures + snapshotIndex;

				RdrVertexShaderObj push_vs = device->device_state.vertex_shader;
				RdrPixelShaderObj push_ps = device->device_state.pixel_shader;
				RdrVertexDeclarationObj push_declaration = device->device_state.vertex_declaration;
				RdrSurfaceStateDX *current_state = rxbxGetCurrentState(device);
				RdrTexHandle push_tex0 = current_state->textures[TEXTURE_PIXELSHADER][0].bound_id;
				RdrTexHandle push_tex1 = current_state->textures[TEXTURE_PIXELSHADER][1].bound_id;

				int push_width_2d_mode = current_state->width_2d;
				int push_height_2d_mode = current_state->height_2d;
				int push_need_model_mat = current_state->need_model_mat;
				U32 push_vertex_stream_0_freq = device->device_state.vertex_stream_frequency[0];
				RdrVertexStreamState push_vertex_stream_0_state = device->device_state.vertex_stream[0];
				RdrVertexStreamState push_vertex_stream_1_state = device->device_state.vertex_stream[1];
				RdrVertexStreamState push_vertex_stream_2_state = device->device_state.vertex_stream[2];

				Vec4H modelViewMat[MAX_VS_CONSTANTS];
				int i;

				D3DPERF_BeginEvent(0, L"D3D11 MSAA resolve");

				// Draw one oversized triangle to avoid having a seam in the middle of the screen (fixes one issue with GF7 cards with writing depth into an MSAA buffer)
				// CD: the extra 0.5 boundary is there to fix edge multisampling errors when postprocessing to a msaa enabled surface
				setVec3(vertices[0].pos, -0.5f, -h-0.5f, 0);
				setVec3(vertices[1].pos, -0.5f, h+0.5f, 0);
				setVec3(vertices[2].pos, w*2+0.5f, h+0.5f, 0);

				// This is to preserve all necessary values needed to draw a material to the screen since constants_desired is about to be trashed by set2dmode and set3dmode.
				for (i = VS_MATRIX_REGISTER_START; i <= VS_MATRIX_REGISTER_END; i ++)
					copyVec4H(vec4toVec4H_aligned(device->device_state.vs_constants_desired[i]),modelViewMat[i]);

				rxbxSet2DMode(device, pSrcSurface->width_thread, pSrcSurface->height_thread);
				rxbxSetupPostProcessScreenDrawModeAndDecl(device);
				rxbxResetViewport( device, pSrcSurface->width_thread, pSrcSurface->height_thread );

				rxbxSetCullMode(device, CULLMODE_NONE);

				device->device_state.textures[TEXTURE_PIXELSHADER][0].texture = pSrcSurface->rendertarget[SBUF_DEPTH].textures[0].d3d_texture;
				device->device_state.textures[TEXTURE_PIXELSHADER][1].texture.typeless = NULL;

				rxbxDepthWritePush(device, true);
				rxbxDepthTestPush(device, DEPTHTEST_OFF);

				rxbxSetDepthResolveMode(device, pSrcSurface->multisample_count);

				rxbxSetVertexStreamNormal(device, 0);

				// TODO DX11 DJR can we do something more efficient than clearing all targets?
				// this fixes D3D debug resource hazard warnings, but wastes some state changes
				ID3D11DeviceContext_OMSetRenderTargets(device->d3d11_imm_context, SBUF_MAXMRT, 
					null_targets, 
					null_depthbuf);
				rxbxApplyPixelTextureStateEx(device, device->d3d_device);
				ID3D11DeviceContext_OMSetRenderTargets(device->d3d11_imm_context, SBUF_MAXMRT, 
					null_targets, 
					pSrcSurface->rendertarget[SBUF_DEPTH].textures[snapshotIndex].snapshot_view.depth_stencil_view11);
				D3DPERF_BeginEvent(0, L"D3D11 MSAA resolve draw");

				if(rdrDisableStateChangeForMSAADepthResolve) {
					device->disable_texture_and_target_apply = true;
				}

				rxbxDrawVerticesUP(device, D3DPT_TRIANGLESTRIP, 3, vertices, 
					sizeof(vertices[0]));

				if(rdrDisableStateChangeForMSAADepthResolve) {
					device->disable_texture_and_target_apply = false;
				}

				D3DPERF_EndEvent();

				rxbxSetVertexStreamSource(device, 0, push_vertex_stream_0_state.source, push_vertex_stream_0_state.stride, push_vertex_stream_0_state.offset);
				rxbxSetVertexStreamSource(device, 1, push_vertex_stream_1_state.source, push_vertex_stream_1_state.stride, push_vertex_stream_1_state.offset);
				rxbxSetVertexStreamSource(device, 2, push_vertex_stream_2_state.source, push_vertex_stream_2_state.stride, push_vertex_stream_2_state.offset);
				rxbxRestoreVertexStreamFrequencyState(device, 0, push_vertex_stream_0_freq);

				rxbxDepthWritePop(device);
				rxbxDepthTestPop(device);
				rxbxBindVertexShaderObject(device, push_vs);
				rxbxBindPixelShaderObject(device, push_ps);
				rxbxSetVertexDeclaration(device, push_declaration);

				// TODO DX11 DJR can we do something more efficient than clearing all targets?
				// this fixes D3D debug resource hazard warnings, but wastes some state changes
				ID3D11DeviceContext_OMSetRenderTargets(device->d3d11_imm_context, SBUF_MAXMRT, 
					null_targets, 
					null_depthbuf);
				// TODO DX11 DJR unwind recursive tex bind here
				// force rebind by clearing the cached bound ID
				current_state->textures[TEXTURE_PIXELSHADER][0].bound_id = push_tex0;
				current_state->textures[TEXTURE_PIXELSHADER][0].bound_id.texture.hash_value = ~push_tex0.texture.hash_value;
				current_state->textures[TEXTURE_PIXELSHADER][1].bound_id = push_tex1;
				current_state->textures[TEXTURE_PIXELSHADER][1].bound_id.texture.hash_value = ~push_tex1.texture.hash_value;
				rxbxBindTextureEx(device, 0, RdrTexHandleToTexHandle(push_tex0), TEXTURE_PIXELSHADER);
				rxbxBindTextureEx(device, 1, RdrTexHandleToTexHandle(push_tex1), TEXTURE_PIXELSHADER);
				rxbxApplyTextureSlot(device, TEXTURE_PIXELSHADER, 0);
				rxbxApplyTextureSlot(device, TEXTURE_PIXELSHADER, 1);
				rxbxApplyTargets(device);
				rxbxResetViewport( device, device->active_surface->width_thread, device->active_surface->height_thread );

				if (push_width_2d_mode)
					rxbxSet2DMode(device, push_width_2d_mode, push_height_2d_mode);
				else
					rxbxSet3DMode(device, push_need_model_mat);

				// Restore values so the remainder of the snapshot may be rendered properly.
				for (i = VS_MATRIX_REGISTER_START; i <= VS_MATRIX_REGISTER_END; i ++)
					copyVec4H(modelViewMat[i], *(Vec4H*)(&device->device_state.vs_constants_desired[i]));

				D3DPERF_EndEvent();
			}
		}
		else
			D3D11ResolveDepthSurface(device, pSrcSurface->rendertarget[bufferIndex].d3d_surface, pSrcSurface->rendertarget[bufferIndex].textures[snapshotIndex].d3d_buffer, RSF_POSITIVE_X, -1, -1, false);
	}
	else
	if (device->nv_api_support)
	{
		if (!pSrcSurface->nv_tex_handle)
		{
			D3DTexture * pDstTexture = pSrcSurface->rendertarget[bufferIndex].textures[snapshotIndex].d3d_texture.texture_2d_d3d9;
			NVObject src_depth_srf = NVDX_OBJECT_NONE;
			NVObject dest_tex = NVDX_OBJECT_NONE;
			NvAPI_Status depth_status, dest_status;

			CHECKX(IDirect3DDevice9_SetRenderTarget(device->d3d_device, 0, pSrcSurface->rendertarget[SBUF_0].d3d_surface.surface9));
			CHECKX(IDirect3DDevice9_SetDepthStencilSurface(device->d3d_device, pSrcSurface->rendertarget[SBUF_DEPTH].d3d_surface.surface9));

			depth_status = rxbxGetNVZBufferHandle(device->d3d_device, &src_depth_srf);
			dest_status = rxbxGetNVTextureHandle(pDstTexture, &dest_tex);
			if (depth_status == NVAPI_OK && dest_status == NVAPI_OK)
			{
				pSrcSurface->nv_tex_handle = dest_tex;
				pSrcSurface->nv_depth_handle = src_depth_srf;
			}
		}
		if (pSrcSurface->nv_tex_handle)
		{
			//copy the back buffer to the depth texture
			NvAPI_Status status = rxbxNVStretchRect(device->d3d_device,
				pSrcSurface->nv_depth_handle, NULL, pSrcSurface->nv_tex_handle, NULL, D3DTEXF_POINT);

			// nvidia API hammers the vertex declaration
			rxbxApplyVertexDeclaration(device);

			rxbxCountQueryDrawCall(device);

		}

		CHECKX(IDirect3DDevice9_SetRenderTarget(device->d3d_device, 0, device->device_state.targets_driver[SBUF_0].surface9));
		CHECKX(IDirect3DDevice9_SetDepthStencilSurface(device->d3d_device, device->device_state.targets_driver[SBUF_DEPTH].surface9));
	}
	else
	if (device->ati_resolve_msaa_z_supported)
	{
		Vec3 vDummyPoint = {0.0f, 0.0f, 0.0f};
		D3DDevice * d3d_device = device->d3d_device;
		RdrVertexShaderObj push_vs = device->device_state.vertex_shader;
		RdrPixelShaderObj push_ps = device->device_state.pixel_shader;
		RdrVertexDeclarationObj push_declaration = device->device_state.vertex_declaration;
		RdrTextureObj push_tex0 = device->device_state.textures[TEXTURE_PIXELSHADER][0].texture;

		rxbxCountQueryDrawCall(device);
		CHECKX(IDirect3DDevice9_SetRenderTarget(device->d3d_device, 0, pSrcSurface->rendertarget[SBUF_0].d3d_surface.surface9));
		CHECKX(IDirect3DDevice9_SetDepthStencilSurface(device->d3d_device, pSrcSurface->rendertarget[SBUF_DEPTH].d3d_surface.surface9));

		rxbxBindVertexShader(device, device->minimal_vertex_shaders[PS_SPRITE_DEFAULT]);
		rxbxBindPixelShader(device, device->default_pixel_shaders[PS_SPRITE_DEFAULT], NULL);

		// set simple, position-only, vertex format
		rxbxSetupPostProcessScreenDrawModeAndDecl(device);
		rxbxDepthWritePush(device, FALSE);
		rxbxColorWritePush(device, FALSE);
		CHECKX(IDirect3DDevice9_SetRenderState(d3d_device, D3DRS_ZENABLE, FALSE));
		rxbxApplyStatePreDraw(device);

		// Bind depth stencil texture to texture sampler 0
		CHECKX(IDirect3DDevice9_SetTexture(device->d3d_device, 0, pSrcSurface->rendertarget[bufferIndex].textures[snapshotIndex].d3d_texture.texture_base_d3d9));

		// dummy draw call to ensure state changes are flushed to GPU; prevents reordering of states
		// due to D3D shadow copies
		CHECKX(IDirect3DDevice9_DrawPrimitiveUP(d3d_device, D3DPT_POINTLIST, 1, &vDummyPoint, 
			sizeof(Vec3)));

		CHECKX(IDirect3DDevice9_SetRenderState(d3d_device, D3DRS_ZENABLE, TRUE));

		// Trigger the depth buffer resolve; after this call texture sampler 0
		// will contain the contents of the resolve operation
		CHECKX(IDirect3DDevice9_SetRenderState(d3d_device, D3DRS_POINTSIZE, ATI_RESZ_CODE));

		// dummy draw call to ensure state changes are flushed to GPU; prevents reordering of states
		// due to D3D shadow copies
		CHECKX(IDirect3DDevice9_DrawPrimitiveUP(d3d_device, D3DPT_POINTLIST, 1, &vDummyPoint, 
			sizeof(Vec3)));
		rxbxDepthWritePop(device);
		rxbxColorWritePop(device);
		CHECKX(IDirect3DDevice9_SetRenderState(d3d_device, D3DRS_ZENABLE, TRUE));

		rxbxBindVertexShaderObject(device, push_vs);
		rxbxBindPixelShaderObject(device, push_ps);
		rxbxSetVertexDeclaration(device, push_declaration);
		CHECKX(IDirect3DDevice9_SetTexture(device->d3d_device, 0, push_tex0.texture_base_d3d9));

		// force setting stream source again since DrawXXXXXUP leaves them indeterminate
		rxbxResetStreamSource(device);

		CHECKX(IDirect3DDevice9_SetRenderTarget(device->d3d_device, 0, device->device_state.targets_driver[SBUF_0].surface9));
		CHECKX(IDirect3DDevice9_SetDepthStencilSurface(device->d3d_device, device->device_state.targets_driver[SBUF_DEPTH].surface9));

		rxbxApplyStatePreDraw(device);

		CHECKX(IDirect3DDevice9_SetRenderState(d3d_device, D3DRS_POINTSIZE, 0x3f800000));
	}


#endif
}
#endif

#if _XBOX
// Note that bits set in buffer_mask prevent that buffer from resolving.
static void rxbxResolveSurfacesMasked(RdrDeviceDX *device, int set_index, int buffer_mask, int force_depth_tex0)
{
	RdrSurfaceDX *surface = device->active_surface;
	int tile_num, bufferIndex, buffer_bit, buffer_count = 0, effectiveBufferIndex;

	for (tile_num = 0; tile_num < surface->tile_count; ++tile_num)
	{
		if (surface->tile_count > 1)
			IDirect3DDevice9_SetPredication(device->d3d_device, D3DPRED_TILE(tile_num));

		for (bufferIndex = 0, effectiveBufferIndex = 0, buffer_bit = 1; bufferIndex < ARRAY_SIZE(surface->rendertarget); 
			bufferIndex++, buffer_bit <<= 1)
		{
			if (buffer_mask & buffer_bit)
				continue;

			if (!(surface->active_buffer_write_mask & buffer_bit))
				continue;

			if (bufferIndex == SBUF_DEPTH)
				effectiveBufferIndex = SBUF_DEPTH;

			if (eaGet(&surface->rendertarget[bufferIndex].d3d_textures, set_index))
			{
				int texture_index = set_index;
				if (force_depth_tex0 && bufferIndex==SBUF_DEPTH)
					texture_index = 0;

				//assert(bufferIndex != SBUF_DEPTH || set_index != 0); // Should only use SBUF_DEPTH if doing a predicated Snapshot

				STATIC_INFUNC_ASSERT(ARRAY_SIZE(surface->rendertarget) == ARRAY_SIZE(resolve_flags));
				IDirect3DDevice9_Resolve(device->d3d_device, resolve_flags[effectiveBufferIndex], &surface->tile_rects[tile_num],
					(D3DBaseTexture *)surface->rendertarget[bufferIndex].d3d_textures[texture_index],
					(D3DPOINT*)&surface->tile_rects[tile_num], 0, getActiveD3DCubemapFace(surface), NULL, 0, 0, NULL);
				if (tile_num == 0)
					buffer_count++;
			}

			effectiveBufferIndex++;
		}
	}

	INC_RESOLVES(surface->width_thread, surface->height_thread, buffer_count);
	surface->draw_calls_since_resolve &= buffer_mask; //buffer mask in inverted here
}

static void rxbxEndPredication(RdrDeviceDX *device, int set_index)
{
	if (device->active_surface->tile_count > 1)
	{
		IDirect3DDevice9_SetPredication(device->d3d_device, 0);
		IDirect3DDevice9_EndTiling(device->d3d_device, 0, NULL, NULL, NULL, 0, 0, NULL);
	}
}
#endif

void rxbxUnsetSurfaceActiveDirect(RdrDeviceDX *device)
{
	RdrSurfaceDX *surface = device->active_surface;

	CHECKDEVICELOCK(device);

	if (!surface)
	{
		rxbxSetStateActive(device, 0, 0, 0, 0);
		device->active_surface = NULL;
		device->material_disable_flags = 0;
		return;
	}

	CHECKTHREAD;

#if _PS3

    assert(device->active_surface == surface);

#elif !_XBOX


#if DEBUG_LOG_MRT_TO_DISK
	if ( g_bDebugLogPPEffects )
	{
		int idx = surface->d3d_textures_write_index;
		rxbxFlushGPUDirect(device, NULL, NULL);
#define SAVE_TEXTURE_TO_FILE(texture, filename)	\
		if ( texture )							\
			D3DXSaveTextureToFile( filename,	\
				D3DXIFF_BMP,					\
				(D3DBaseTexture*)texture,		\
				NULL );

		MAX1(idx, 0);
		if (eaSize(&surface->rendertarget[SBUF_0].d3d_textures) > idx)
		{
			SAVE_TEXTURE_TO_FILE(surface->rendertarget[SBUF_0].d3d_textures[idx], "c:\\temp\\sbuf_0.bmp");
		}
		if (eaSize(&surface->rendertarget[SBUF_1].d3d_textures) > idx)
		{
			SAVE_TEXTURE_TO_FILE(surface->rendertarget[SBUF_1].d3d_textures[idx], "c:\\temp\\sbuf_1.bmp");
		}
		if (eaSize(&surface->rendertarget[SBUF_2].d3d_textures) > idx)
		{
			SAVE_TEXTURE_TO_FILE(surface->rendertarget[SBUF_2].d3d_textures[idx], "c:\\temp\\sbuf_2.bmp");
		}
		if (eaSize(&surface->rendertarget[SBUF_3].d3d_textures) > idx)
		{
			SAVE_TEXTURE_TO_FILE(surface->rendertarget[SBUF_3].d3d_textures[idx], "c:\\temp\\sbuf_3.bmp");
		}

		//rxbxDumpSurfaces( device );
	}
#endif

#else
	if (surface->do_zpass)
		IDirect3DDevice9_EndZPass(device->d3d_device);

	assert(device->active_surface == surface);

	surface->draw_calls_since_resolve = MASK_SBUF_ALL; //force everything to get resolved
	rxbxUpdateMSAAResolveOrSurfaceCopyTexture(device, surface, ~0, 0);

	rxbxEndPredication(device, surface->d3d_textures_write_index);
#endif

	device->active_surface = NULL;
	device->material_disable_flags = 0;
	rxbxSetStateActive(device, 0, 0, 0, 0);

	etlAddEvent(surface->event_timer, "Active", ELT_RESOURCE, ELTT_END);
}

void rxbxGetSurfaceDataDirect(RdrSurfaceDX *surface, RdrSurfaceData *params)
{
	void *pBits = NULL;
	int Pitch=0;
	RdrDeviceDX * device = (RdrDeviceDX*)surface->surface_base.device;
	HRESULT hr;
	RdrTexFormatObj dummy_format = {RTEX_BGRA_U8};
	ID3D11Texture2D *pCompatableTexture=NULL;
	IDirect3DSurface9 *pSystemMemorySurface=NULL;
	bool bNeedsRelease=false;
	RdrDXTexFormatObj surface_format;

	CHECKTHREAD;
	CHECKDEVICELOCK(surface->surface_base.device);
	CHECKSURFACEACTIVE(surface);

	PERFINFO_AUTO_START("rxbxGetSurfaceDataDirect", 1);

	etlAddEvent(surface->event_timer, "Get", ELT_RESOURCE, ELTT_INSTANT);

	if (device->d3d11_device)
	{
		D3D11_TEXTURE2D_DESC dsc;
		D3D11_RESOURCE_DIMENSION dim;
		ID3D11Texture2D *pBackBuffer;
		D3D11_MAPPED_SUBRESOURCE mapped_subresource = {0};

		if (surface->type == SURF_PRIMARY)
		{
			IDXGISwapChain_GetBuffer(device->d3d11_swapchain, 0, &IID_ID3D11Texture2D, &pBackBuffer);
			bNeedsRelease = true;
		} else {
			pBackBuffer = surface->rendertarget[SBUF_0].textures[0].d3d_buffer.texture_2d_d3d11;
		}
		pCompatableTexture = pBackBuffer;
		ID3D11Texture2D_GetDesc(pBackBuffer, &dsc);
		ID3D11Texture2D_GetType(pBackBuffer, &dim);

		surface_format.format_d3d11 = dsc.Format;
		// special case msaa textures
		if ( dsc.SampleDesc.Count > 1) {
			D3D11_TEXTURE2D_DESC dsc_new = dsc;
			ID3D11Texture2D *resolveTexture;
			dsc_new.SampleDesc.Count = 1;
			dsc_new.SampleDesc.Quality = 0;
			dsc_new.Usage = D3D11_USAGE_DEFAULT;
			dsc_new.BindFlags = 0;
			dsc_new.CPUAccessFlags = 0;
			hr = ID3D11Device_CreateTexture2D(device->d3d11_device, &dsc_new, NULL, &resolveTexture);
			assert(!FAILED(hr));
			ID3D11DeviceContext_ResolveSubresource(device->d3d11_imm_context, (ID3D11Resource*)resolveTexture, 0, (ID3D11Resource*)pBackBuffer, 0, dsc.Format);
			if (bNeedsRelease)
				ID3D11Texture2D_Release(pBackBuffer);
			pCompatableTexture = resolveTexture;
			ID3D11Texture2D_GetDesc(pCompatableTexture, &dsc);
			bNeedsRelease = true;
		}

		// Resolve to staging texture
		{
			D3D11_TEXTURE2D_DESC dsc_new = dsc;
			ID3D11Texture2D *resolveTexture;
			D3D11_BOX box = {0, 0, 0, dsc.Width, dsc.Height, 1};
			dsc_new.SampleDesc.Count = 1;
			dsc_new.SampleDesc.Quality = 0;
			dsc_new.Usage = D3D11_USAGE_STAGING;
			dsc_new.BindFlags = 0;
			dsc_new.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			hr = ID3D11Device_CreateTexture2D(device->d3d11_device, &dsc_new, NULL, &resolveTexture);
			assert(!FAILED(hr));
			ID3D11DeviceContext_CopySubresourceRegion(device->d3d11_imm_context, (ID3D11Resource*)resolveTexture, 0, 0, 0, 0, (ID3D11Resource*)pCompatableTexture, 0, &box);
			if (bNeedsRelease)
				ID3D11Texture2D_Release(pCompatableTexture);
			pCompatableTexture = resolveTexture;
			bNeedsRelease = true;
		}

		hr = ID3D11DeviceContext_Map(device->d3d11_imm_context, (ID3D11Resource*)pCompatableTexture, 0, D3D11_MAP_READ, 0, &mapped_subresource);
		pBits = mapped_subresource.pData;
		Pitch = mapped_subresource.RowPitch;

		devassert(SUCCEEDED(hr));
	} else {
		D3DSURFACE_DESC surfaceAttrs;
		D3DLOCKED_RECT surfaceLock = { 0 };

		CHECKX(IDirect3DSurface9_GetDesc( surface->rendertarget[SBUF_0].d3d_surface.surface9, &surfaceAttrs ));
		surface_format.format_d3d9 = surfaceAttrs.Format;

		hr = IDirect3DDevice9_CreateRenderTarget( device->d3d_device, 
			params->width, params->height, surfaceAttrs.Format, D3DMULTISAMPLE_NONE, 0,
			TRUE, &pSystemMemorySurface, NULL );

		devassert(SUCCEEDED(hr));

		//if (!FAILED(hr))
		//	rxbxLogCreateSurface(device, pSystemMemorySurface);

		if ( pSystemMemorySurface )
		{
			RECT lockArea;
			RECT sourceRect;

			rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:RenderTarget", 1, rxbxCalcSurfaceMemUsage(params->width, params->height, D3DMULTISAMPLE_NONE, dummy_format));

			PERFINFO_AUTO_START("D3D::StretchRect", 1);
			sourceRect.left = 0;
			sourceRect.top = 0;
			sourceRect.right = sourceRect.left + params->width;
			sourceRect.bottom = sourceRect.top + params->height;
			lockArea.left = 0;
			lockArea.top = 0;
			lockArea.right = params->width;
			lockArea.bottom = params->height;
			hr = IDirect3DDevice9_StretchRect( device->d3d_device, surface->rendertarget[SBUF_0].d3d_surface.surface9, &sourceRect, 
				pSystemMemorySurface, &lockArea, D3DTEXF_POINT );
			devassertmsgf(SUCCEEDED(hr), "StretchRect failed %s", rxbxGetStringForHResult(hr));
			PERFINFO_AUTO_STOP();

			hr = CHECKX(IDirect3DSurface9_LockRect( pSystemMemorySurface, &surfaceLock, &lockArea, D3DLOCK_READONLY ));
			if (!FAILED(hr)) {
				pBits = surfaceLock.pBits;
				Pitch = surfaceLock.Pitch;
			}
		}
	}
	if (pBits)
	{
		switch (params->type)
		{
			xcase SURFDATA_RGB:
			{
				int y;
				// read surface memory top-down
				U8 * pDestScan = params->data + params->width * 3 * ( params->height - 1 );

				for ( y = params->height; y; --y )
				{
					const U8 * pSourceScan = pBits;
					int x;

					for ( x = params->width; x; --x )
					{
						pDestScan[ 0 ] = pSourceScan[ 2 ];
						pDestScan[ 1 ] = pSourceScan[ 1 ];
						pDestScan[ 2 ] = pSourceScan[ 0 ];
						pDestScan += 3;
						pSourceScan += 4;
					}

					// top-down
					pDestScan -= params->width * 3 * 2;

					pBits = ( (U8*)pBits ) + Pitch;
				}
			}
			xcase SURFDATA_RGBA:
			{
				int y;
				// read surface memory top-down
				U32 * pDestScan = (U32*)params->data + params->width * ( params->height - 1 );

				for ( y = params->height; y; --y )
				{
					const U32 * pSourceScan = pBits;
					int x;

					for ( x = params->width; x; --x )
					{
						// JE: Inverting R and B because mouse cursors were getting them swapped... I *think* this is the place it should be swapped...
						U32 value = *pSourceScan;
						((char*)pDestScan)[0] = (value>>16)&0xff;
						((char*)pDestScan)[1] = (value>>8)&0xff;
						((char*)pDestScan)[2] = value&0xff;
						((char*)pDestScan)[3] = (value>>24)&0xff;
						++pDestScan;
						++pSourceScan;
					}

					// top-down
					pDestScan -= params->width * 2;
					
					pBits = ( (U8*)pBits ) + Pitch;
				}
			}
			xcase SURFDATA_BGRA:		// This is identical to the case above without the byte reordering.
			{
				int y;
				// read surface memory top-down
				U32 * pDestScan = (U32*)params->data + params->width * ( params->height - 1 );

				for ( y = params->height; y; --y )
				{
					const U32 * pSourceScan = pBits;
					int x;

					for ( x = params->width; x; --x )
					{
						*pDestScan = *pSourceScan;
						++pDestScan;
						++pSourceScan;
					}

					// top-down
					pDestScan -= params->width * 2;

					pBits = ( (U8*)pBits ) + Pitch;
				}
			}
			xcase SURFDATA_DEPTH:
				break;
			xcase SURFDATA_STENCIL:
				break;
			xcase SURFDATA_RGB_F32:
			{
				int y;
				// read surface memory top-down
				F32 * pDestScan = (F32 *)(params->data + params->width * 3 * sizeof(F32) * ( params->height - 1 ));

				for ( y = params->height; y; --y )
				{
					if (!device->d3d11_device && surface_format.format_d3d11 == D3DFMT_G16R16F ||
						device->d3d11_device && surface_format.format_d3d9 == DXGI_FORMAT_R16G16_FLOAT)
					{
						const F16 * pSourceScan = pBits;
						int x;

						for ( x = params->width; x; --x )
						{
							pDestScan[ 0 ] = F16toF32(pSourceScan[ 0 ]);
							pDestScan[ 1 ] = F16toF32(pSourceScan[ 1 ]);
							pDestScan[ 2 ] = 0.0f;
							pDestScan += 3;
							pSourceScan += 2;
						}
					}
					else
					{
						const F32 * pSourceScan = pBits;
						int x;

						for ( x = params->width; x; --x )
						{
							pDestScan[ 0 ] = pSourceScan[ 0 ];
							pDestScan[ 1 ] = pSourceScan[ 1 ];
							pDestScan[ 2 ] = pSourceScan[ 2 ];
							pDestScan += 3;
							pSourceScan += 4;
						}
					}

					// top-down
					pDestScan -= params->width * 3 * 2;

					pBits = ( (U8*)pBits ) + Pitch;
				}
			}
		}
	}

	if (device->d3d11_device)
	{
		if (pCompatableTexture)
		{
			ID3D11DeviceContext_Unmap(device->d3d11_imm_context, (ID3D11Resource*)pCompatableTexture, 0);

			if (bNeedsRelease)
			{
				ID3D11Texture2D_Release(pCompatableTexture);
			}
		}
	} else {
		if (pSystemMemorySurface)
		{
			CHECKX(IDirect3DSurface9_UnlockRect( pSystemMemorySurface ));

			IDirect3DSurface9_Release( pSystemMemorySurface );
			//rxbxLogReleaseSurface(device, pSystemMemorySurface, ref_count);
			rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:RenderTarget", 1, -rxbxCalcSurfaceMemUsage(params->width, params->height, D3DMULTISAMPLE_NONE, dummy_format));
		}
	}

	PERFINFO_AUTO_STOP();
}

#if _XBOX
static void rxbxRestoreSurfaceAfterSnapshot(RdrDeviceDX *device, int src_set_index, RdrSurfaceBufferMaskBits mask)
{
	RdrSurfaceDX *surface = device->active_surface;
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[ARRAY_SIZE(surface->rendertarget)];
	int bufferIndex;
	ppscreen.tex_width = surface->width_thread;
	ppscreen.tex_height = surface->height_thread;
	ppscreen.material.const_count = 0;
	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.write_depth = (mask&MASK_SBUF_DEPTH)?1:0;
	for (bufferIndex=0; bufferIndex<ARRAY_SIZE(surface->rendertarget); bufferIndex++)
	{
		if (!(textures[bufferIndex] = eai64Get((TexHandle **)&surface->rendertarget[bufferIndex].tex_handles, src_set_index)))
		{
			textures[bufferIndex] = device->white_tex_handle;
		}
	}
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = rxbxGetCopyBlendMode(device, surface->type, ppscreen.write_depth);
	ppscreen.blend_type = RPPBLEND_REPLACE;
	ppscreen.rdr_internal = 1; // Don't count triangles, as it's going to fail to match up with the GraphicsLib count, maybe need a "restore" count to track this...

	rxbxColorWritePush(device, (mask & MASK_SBUF_0)?true:false);
	rxbxPostProcessScreenDirect(device, &ppscreen, NULL);
	rxbxColorWritePop(device);
}
#endif

#if _XBOX
// not needed on the PC
void rxbxSurfaceRestoreAfterSetActiveDirect(RdrDeviceDX *device, RdrSurfaceBufferMaskBits *pmask, WTCmdPacket *packet)
{
	rxbxRestoreSurfaceAfterSnapshot(device, 0, *pmask);
}
#endif

#if RDR_NVIDIA_TXAA_SUPPORT
static void rxbxSurfaceResolveTXAA(RdrDeviceDX *device, RdrSurfaceDX *surface, RdrSurfaceSnapshotData *snapshot_data, int debug_mode)
{
	Mat44 mvp_this;
	TxaaU4 resolve_mode;

	RxbxSurfacePerTargetState *sbuf_visual = &surface->rendertarget[SBUF_0];
	RxbxSurfacePerTargetState *sbuf_depth = &surface->rendertarget[SBUF_DEPTH];

	rxbxDirtyTargets(device);
	rxbxDirtyShaders(device);
	rxbxDirtyBlend(device);
	rxbxDirtyStreamSources(device);
	rxbxDirtyVertexDeclaration(device);

	if (surface->multisample_count == 2) {
		if (debug_mode == 2) {
			resolve_mode = TXAA_MODE_DEBUG_2xMSAA;
		} else if (debug_mode == 3) {
			resolve_mode = TXAA_MODE_DEBUG_2xMSAAx1T_DIFF;
		} else if (debug_mode == 4) {
			resolve_mode = TXAA_MODE_DEBUG_VIEW_MV;
		} else {
			resolve_mode = TXAA_MODE_2xMSAAx1T;
		}
	} else {
		assert(surface->multisample_count == 4);
		if (debug_mode == 2) {
			resolve_mode = TXAA_MODE_DEBUG_4xMSAA;
		} else if (debug_mode == 3) {
			resolve_mode = TXAA_MODE_DEBUG_4xMSAAx1T_DIFF;
		} else if (debug_mode == 4) {
			resolve_mode = TXAA_MODE_DEBUG_VIEW_MV;
		} else {
			resolve_mode = TXAA_MODE_4xMSAAx1T;
		}
	}

	mulMat44Inline(surface->state.projection_mat3d, surface->state.viewmat, mvp_this);
	transposeMat44(mvp_this);
	TxaaResolveDX(
		&device->txaa_context, 
		device->d3d11_imm_context,         // DX11 device context.

		// Destination texture:
		sbuf_visual->textures[snapshot_data->dest_set_index].snapshot_view.render_target_view11,

		// Source MSAA texture shader resource view:
		sbuf_visual->textures[0].d3d_texture.texture_view_d3d11,

		// Resolved depth (min depth of samples in pixel):
		sbuf_depth->textures[1].d3d_texture.texture_view_d3d11,

		sbuf_visual->textures[snapshot_data->txaa_prev_set_index].d3d_texture.texture_view_d3d11,

		resolve_mode,

		// Source/destination texture dimensions in pixels:
		surface->width_thread,
		surface->height_thread,
		0.0f,             // first depth position 0-1 in Z buffer
		1.0f,             // second depth position 0-1 in Z buffer 
		16.0f, // first depth position motion limit in pixels
		16.0f, // second depth position motion limit in pixels
		mvp_this[0], // matrix for world to view projection (current frame)
		surface->state.last_mvp_mat3d[0]);  // matrix for world to view projection (prior frame)

	copyMat44(mvp_this, surface->state.last_mvp_mat3d);
}
#endif	//RDR_NVIDIA_TXAA_SUPPORT

void rxbxSurfaceSnapshotDirect(RdrDeviceDX *device, RdrSurfaceSnapshotData *snapshot_data, WTCmdPacket *packet)
{
	RdrSurfaceDX *surface = (RdrSurfaceDX *)snapshot_data->surface;
	RdrSurfaceFace oldFace = surface->active_face;
	int buffer_count = 0;
	int dest_set_index = snapshot_data->dest_set_index; //index 1 used for MSAA resolves
	int buffer_mask = snapshot_data->buffer_mask;
	int txaa_resolve = snapshot_data->txaa_prev_set_index > 0;
	if (!buffer_mask)
		buffer_mask = MASK_SBUF_ALL;

	if (device->d3d11_device)
	{
		if (dest_set_index == 0 && surface->params_thread.desired_multisample_level <= 1)
			// can use the source surface without snapshot
			return;
		if (dest_set_index == 0)
			// resolve to auto-resolve snapshot
			dest_set_index = 1;
	}
	else
	{
		if (dest_set_index == 1 && surface->params_thread.desired_multisample_level <= 1)
			// can use the source surface without snapshot
			return;
	}

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	//LM: only depth requires this assert so I added it below.
	//assert(device->active_surface == surface);

    // Make sure there's space
	if (txaa_resolve) {
		rxbxSurfaceAllocateSets(surface, snapshot_data->txaa_prev_set_index+1, snapshot_data->name, buffer_mask,
			1, !!snapshot_data->force_srgb);
	}
	rxbxSurfaceAllocateSets(surface, dest_set_index+1, snapshot_data->name, buffer_mask,
		txaa_resolve, !!snapshot_data->force_srgb);

	etlAddEvent(surface->event_timer, "Snapshot", ELT_RESOURCE, ELTT_INSTANT);

#if !_XBOX
	//assert(surface->d3d_textures_write_index != snapshot_data->dest_set_index); // This is valid on Xbox

#if RDR_NVIDIA_TXAA_SUPPORT
	if (txaa_resolve)
	{
		if (device->d3d11_imm_context)
			rxbxSurfaceResolveTXAA(device, surface, snapshot_data, snapshot_data->txaa_debug_mode);
	}
	else
#endif
    {
		// PC: Copy from current set to the specified one
		//   Current cannot be the same as the dest!
		int bufferIndex;
		for (bufferIndex=0; bufferIndex <= SBUF_MAXMRT; bufferIndex++)
		{
			if (!(buffer_mask & (1 << bufferIndex)))
				continue;

			if (surface->rendertarget[bufferIndex].d3d_surface.typeless_surface && surface->rendertarget[bufferIndex].texture_count)
			{
				if (0 && (surface->creation_flags & SF_CUBEMAP)) // CD: this is turned off right now because I'm not sure how to get the xbox to behave the same way
				{
					RdrSurfaceFace face;
					for (face = 0; face < 6; ++face)
					{
						rxbxSetActiveSurfaceFace(device, face);
						rxbxCountQueryDrawCall(device);
						rxbxResolve( device,
							surface->rendertarget[bufferIndex].d3d_surface,
							surface->rendertarget[bufferIndex].textures + dest_set_index,
							surface->active_face, -1, -1,
							true
						);
						buffer_count++;
					}
				}
				else
				{
					if (bufferIndex == SBUF_DEPTH)
					{
	#if _PS3
	#else
						assert(device->d3d11_device || device->active_surface == surface || device->override_depth_surface == surface); //we need to have the surface active to resolve depth
						// if the source surface has MSAA, resolve the depth buffer using NV_StretchRect
						if (surface->params_thread.desired_multisample_level > 1)
						{
							D3DResolveMSAADepth(device, surface, bufferIndex, dest_set_index);
						}
						else
						{
							assert(surface->rendertarget[bufferIndex].textures[dest_set_index].d3d_texture.typeless);
							// DX11TODO: DX9 did not used to do a resolve here
							rxbxCountQueryDrawCall(device);
							rxbxResolveDepth( device,
								surface->rendertarget[bufferIndex].d3d_surface,
								surface->rendertarget[bufferIndex].textures + dest_set_index,
								0, -1, -1,
								false);
						}
	#endif
					}
					else
					{
						assert(surface->rendertarget[bufferIndex].textures[dest_set_index].d3d_texture.typeless);
						rxbxCountQueryDrawCall(device);
						rxbxResolve( device,
							surface->rendertarget[bufferIndex].d3d_surface,
							surface->rendertarget[bufferIndex].textures + dest_set_index,
							0, -1, -1,
							false);
					}
					buffer_count++;
				}
			}
		}
    }
	rxbxSetActiveSurfaceFace(device, oldFace);
	INC_RESOLVES(surface->width_thread, surface->height_thread, buffer_count);
	//surface->d3d_textures_read_index = snapshot_data->dest_set_index;
#else
	// Xbox
	//  if not tiling: resolve to texture, no need to have multiple textures, but we will just to keep things simple
	//  if tiling: end predicated tiling, resolve to texture, start tiling again, draw color and depth,
	//   and do next resolve to a different texture
	if (surface->tile_count == 1)
	{
		// Not doing tiling, can simply resolve? (rxbxResolveSurfacesMasked takes the inverse mask)
		rxbxResolveSurfacesMasked(device, dest_set_index, ~snapshot_data->buffer_mask, true);
	} else {
		assert(!surface->do_zpass); // Don't know what to do about this.
		assert(eaGet(&surface->rendertarget[SBUF_DEPTH].d3d_textures, snapshot_data->dest_set_index));
		
		if (snapshot_data->continue_tiling)
		{
			// End predicated tiling and resolve to the desired texture
			rxbxResolveSurfacesMasked(device, dest_set_index, ~snapshot_data->buffer_mask, false);
		}
		else
		{
			// End predicated tiling and resolve to the desired texture
			rxbxResolveSurfacesMasked(device, dest_set_index, 0, false);
			rxbxEndPredication(device, dest_set_index);

			// Now, begin predication and draw the entire image back to the buffer (including depth!)
			rxbxStartPredication(device);

			assert(device->active_surface == surface);
			rxbxRestoreSurfaceAfterSnapshot(device, dest_set_index, MASK_SBUF_ALL);
		}
	}
#endif
	if (dest_set_index == 0)
	{
		surface->draw_calls_since_resolve &= ~snapshot_data->buffer_mask;
	}
	PERFINFO_AUTO_STOP();
}

void rxbxSurfaceSetAutoResolveDisableMaskDirect(RdrDeviceDX *device, RdrSurfaceSetAutoResolveDisableMaskData *data, WTCmdPacket *packet)
{
	RdrSurfaceDX *surface = (RdrSurfaceDX *)data->surface;
	RdrSurfaceBufferMaskBits old_mask = surface->auto_resolve_disable_mask;
	CHECKTHREAD;
	surface->auto_resolve_disable_mask = data->buffer_mask;
	//set bits that have just been turned off to dirty so they will get resolved next time
	surface->draw_calls_since_resolve |= (old_mask & ~surface->auto_resolve_disable_mask);
}

void rxbxSurfacePushAutoResolveMaskDirect(RdrDeviceDX *device, RdrSurfaceSetAutoResolveDisableMaskData *data, WTCmdPacket *packet)
{
	RdrSurfaceDX *surface = (RdrSurfaceDX *)data->surface;
	CHECKTHREAD;
	assert(surface->draw_calls_since_resolve_stack == 0);
	surface->draw_calls_since_resolve_stack = surface->draw_calls_since_resolve;
	surface->draw_calls_since_resolve &= ~data->buffer_mask;
}

void rxbxSurfacePopAutoResolveMaskDirect(RdrDeviceDX *device, RdrSurfaceSetAutoResolveDisableMaskData *data, WTCmdPacket *packet)
{
	RdrSurfaceDX *surface = (RdrSurfaceDX *)data->surface;
	CHECKTHREAD;
	surface->draw_calls_since_resolve = surface->draw_calls_since_resolve_stack;
	surface->draw_calls_since_resolve_stack = 0;
}

void rxbxBindSurfaceDirect(RdrSurfaceDX *surface, bool is_vertex_texture, int tex_unit, RdrSurfaceBuffer buffer, int set_index, RdrTexFlags sampler_flags)
{
	RdrDeviceDX *device = (RdrDeviceDX *)surface->surface_base.device;

	CHECKDEVICELOCK(device);
	assert(set_index >= 0);
	
	if (!surface->bind_count)
		etlAddEvent(surface->event_timer, "Bind", ELT_RESOURCE, ELTT_BEGIN);
	surface->bind_count++;
	if (set_index >= surface->rendertarget[buffer].texture_count)
	{
		if (is_vertex_texture)
			rxbxBindVertexTexture(device, tex_unit, 0);
		else
			rxbxBindTexture(device, tex_unit, 0);
	}
	else
	{
		RdrTexHandle tex_handle = surface->rendertarget[buffer].textures[set_index].tex_handle;
		tex_handle.sampler_flags |= sampler_flags;
		++surface->rendertarget[buffer].textures[set_index].bind_count;
		if (is_vertex_texture)
			rxbxBindVertexTexture(device, tex_unit, RdrTexHandleToTexHandle(tex_handle));
		else
			rxbxBindTexture(device, tex_unit, RdrTexHandleToTexHandle(tex_handle));
	}
}

void rxbxReleaseSurfaceDirect(RdrSurfaceDX *surface, bool is_vertex_texture, int tex_unit, RdrSurfaceBuffer buffer, int set_index)
{
	RdrDeviceDX *device = (RdrDeviceDX *)surface->surface_base.device;
	CHECKDEVICELOCK(device);
	if (surface->bind_count)
	{
		if (set_index < surface->rendertarget[buffer].texture_count)
			--surface->rendertarget[buffer].textures[set_index].bind_count;
		surface->bind_count--;
		if (!surface->bind_count)
			etlAddEvent(surface->event_timer, "Bind", ELT_RESOURCE, ELTT_END);
	}
	if (is_vertex_texture)
		rxbxBindVertexTexture(device, tex_unit, 0);
	else
		rxbxBindWhiteTexture(device, tex_unit);
}

#if _XBOX
void rxbxSurfaceResolve(RdrDeviceDX* device, RdrSurfaceResolveData* resolve_data, WTCmdPacket* packet)
{
	D3DRECT sourceRect;
	D3DPOINT destPoint;
	D3DBaseTexture* pDestTexture;
	
	rxbxSurfaceAllocateSets(resolve_data->dest->surface_xbox, 1, MASK_SBUF_0, false);
	pDestTexture = (D3DBaseTexture*)resolve_data->dest->surface_xbox->rendertarget[SBUF_0].d3d_textures[0];

	if (resolve_data->hasSourceRect)
	{
		sourceRect.x1 = resolve_data->sourceRect[0];
		sourceRect.y1 = resolve_data->sourceRect[1];
		sourceRect.x2 = resolve_data->sourceRect[2];
		sourceRect.y2 = resolve_data->sourceRect[3];
	}
	if (resolve_data->hasDestPoint)
	{
		destPoint.x = resolve_data->destPoint[0];
		destPoint.y = resolve_data->destPoint[1];
	}

	assert(pDestTexture);

	IDirect3DDevice9_Resolve(device->d3d_device, D3DRESOLVE_RENDERTARGET0 | D3DRESOLVE_ALLFRAGMENTS, resolve_data->hasSourceRect?&sourceRect:NULL, pDestTexture, resolve_data->hasDestPoint?&destPoint:NULL, 0, 0, NULL, 0, 0, NULL);
}
#endif

void rxbxUpdateMSAAResolveOrSurfaceCopyTexture(RdrDeviceDX *device, RdrSurfaceDX* surface, RdrSurfaceBufferMaskBits buffers, int dest_set_index)
{
	RdrSurfaceDX* old_active_surface = device->override_depth_surface;
	RdrSurfaceSnapshotData snapshot_data = {0};
	bool bDidPush=false;

	if (!(surface->draw_calls_since_resolve & buffers))
	{
		//it hasn't been modified since last time so leave it alone
		return;
	}

	//for depth resolves the surface must be active
#if !PLATFORM_CONSOLE
	if (device->d3d_device)
	{
		if (old_active_surface != surface || (buffers & MASK_SBUF_DEPTH) && device->active_surface != surface)
		{
			rxbxColorWritePush(device, TRUE); // Pushing anything, gets overridden in rxbxSetActiveSurfaceBufferWriteMask, need to pop later
			rxbxDepthWritePush(device, TRUE); // Pushing anything, gets overridden in rxbxSetActiveSurfaceBufferWriteMask, need to pop later
			bDidPush = true;
		}
		if ((buffers & MASK_SBUF_DEPTH) && device->active_surface != surface)
		{
			old_active_surface = device->override_depth_surface;
			device->override_depth_surface = surface;
			rxbxSetActiveSurfaceBufferWriteMask(device, device->active_surface->active_buffer_write_mask, device->active_surface->active_face);
		}
	}
#endif
	snapshot_data.surface = &surface->surface_base;
	snapshot_data.continue_tiling = true;
	snapshot_data.dest_set_index = dest_set_index;
	snapshot_data.buffer_mask = buffers;
	snapshot_data.name = "rxbxUpdateMSAAResolveOrSurfaceCopyTexture";
	rxbxSurfaceSnapshotDirect(device, &snapshot_data, NULL);

#if !PLATFORM_CONSOLE
	if (device->d3d_device && old_active_surface != surface)
	{
		device->override_depth_surface = old_active_surface;
		rxbxSetActiveSurfaceBufferWriteMask(device, device->active_surface->active_buffer_write_mask, device->active_surface->active_face);
	}
	if (bDidPush)
	{
		rxbxDepthWritePop(device);
		rxbxColorWritePop(device);
	}
#endif
	surface->draw_calls_since_resolve &= ~buffers;
}

#if !PLATFORM_CONSOLE
RdrNVIDIACSAAMode rxbxSurfaceGetMinCSAALevel(const RdrSurfaceDX * surface)
{
	RdrDeviceDX* device = surface->surface_base.device->device_xbox;
	U32 mrtCount = rxbxGetSurfaceMRTCount(surface);
	RdrNVIDIACSAAMode mode = RdrNVIDIACSAAMode_AllBits;
	U32 i;

	for (i = 0; i < mrtCount; i++)
	{
		mode &= device->surface_types_nvidia_csaa_supported[surface->buffer_types[i] & SBT_TYPE_MASK];
	}

	return mode;
}
#endif

int rxbxSurfaceGetMultiSampleCount(const RdrSurfaceDX * surface, DWORD* outQuality)
{
	int multisample_count;
	RdrDeviceDX *device = (RdrDeviceDX *)surface->surface_base.device;
#if !PLATFORM_CONSOLE
	RdrNVIDIACSAAMode mode;
#endif
	*outQuality = 0;
#if !PLATFORM_CONSOLE

	mode = rxbxSurfaceGetMinCSAALevel(surface);
	if (surface->depth_surface)
		mode &= rxbxSurfaceGetMinCSAALevel(surface->depth_surface);

	// allow render to texture even on MSAA surfaces if Nvidia SDK is available
	//LM: the csaa values are from the table here: http://developer.nvidia.com/object/coverage-sampled-aa.html
	if (surface->params_thread.desired_multisample_level >= 16)
	{
		if (rdr_state.nvidiaCSAAMode >= 2 && (mode & RNVCSAA_Quality))
		{
			multisample_count = 8;
			if (device->d3d11_device)
				*outQuality = 16;
			else
				*outQuality = 2;
		}
		else if (rdr_state.nvidiaCSAAMode >= 1 && (mode & RNVCSAA_Standard))
		{
			multisample_count = 4;
			if (device->d3d11_device)
				*outQuality = 16;
			else
				*outQuality = 4;
		}
		else
		{
			multisample_count = 16;
		}
	}
	else if (surface->params_thread.desired_multisample_level >= 8)
	{
		if (rdr_state.nvidiaCSAAMode >= 2 && (mode & RNVCSAA_Quality))
		{
			multisample_count = 8;
			if (device->d3d11_device)
				*outQuality = 8;
			else
				*outQuality = 0;
		}
		else if (rdr_state.nvidiaCSAAMode >= 1 && (mode & RNVCSAA_Standard))
		{
			multisample_count = 4;
			if (device->d3d11_device)
				*outQuality = 8;
			else
				*outQuality = 2;
		}
		else
		{
			multisample_count = 8;
		}
	}
	else
#endif
	if (surface->params_thread.desired_multisample_level >= 4)
	{
		multisample_count = 4;
	}
	else if (surface->params_thread.desired_multisample_level >= 2)
	{
		multisample_count = 2;
	}
	else
	{
		multisample_count = 0;
	}
	assert(multisample_count != 1); // Should be 0, needed for DX9
	return multisample_count;
}
