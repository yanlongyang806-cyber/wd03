#include "EventTimingLog.h"

#include "rt_xdrawmode.h"
#include "rt_xgeo.h"
#include "rt_xdevice.h"

static const RdrIndexBufferObj null_index_buffer = { NULL };

VertexComponentInfo velements_postprocess[] = 
{
#if _PS3
	{ offsetof(RdrPostprocessVertex,pos), VPOS },
#else
	{ offsetof(RdrPostprocessExVertex,pos), VPOS },
#endif
	{ 0, VTERMINATE },
};

VertexComponentInfo velements_postprocess_shape[] = 
{
	{ offsetof(RdrPrimitiveVertex,pos), VPOS },
	{ 0, VTERMINATE },
};

VertexComponentInfo velements_postprocess_ex[] =
{
	{ offsetof(RdrPostprocessExVertex,pos), VPOS },
	{ offsetof(RdrPostprocessExVertex,texcoord), V2TEXCOORD32 },
	{ 0, VTERMINATE },
};

#define DEBUG_LOG_MRT_TO_DISK 0
#if DEBUG_LOG_MRT_TO_DISK
extern int g_bDebugLogPPEffects;
extern void rxbxDumpSurfaces( RdrDeviceDX *device );
#endif

void rxbxSetupPostProcessScreenDrawModeAndDecl(RdrDeviceDX *device)
{
	rxbxSetupPostProcessScreenDrawMode(device, velements_postprocess, &device->postprocess_screen_vertex_sprite_declaration, true);
}

__forceinline float evaluateIncrementPlane(const Vec3 position, float w, float delta)
{
	return ( position[ 0 ] + position[ 1 ] * w + 1 ) * delta;
}


static void calcOrthoTexGradients(const RdrPostprocessExVertex * verts, Vec4 * texGradients)
{
	Vec3 o, u, v, tex_coord_gradient_plane;
	int tex_coord;

	copyVec2(verts[0].pos, o);
	subVec2(verts[1].pos, o, u);
	subVec2(verts[2].pos, o, v);
	for (tex_coord = 0; tex_coord < 4; ++tex_coord)	
	{
		float grad_length_inv;
		o[2] = verts[0].texcoord[tex_coord];
		u[2] = verts[1].texcoord[tex_coord] - verts[0].texcoord[tex_coord];
		v[2] = verts[2].texcoord[tex_coord] - verts[0].texcoord[tex_coord];

		crossVec3(u, v, tex_coord_gradient_plane);
		grad_length_inv = 1.0f / lengthVec3(tex_coord_gradient_plane);
		scaleVec3(tex_coord_gradient_plane, grad_length_inv, tex_coord_gradient_plane);

		texGradients[0][tex_coord] = tex_coord_gradient_plane[0];
		texGradients[1][tex_coord] = tex_coord_gradient_plane[1];
	}
}

void rxbxPostProcessScreenDirect(RdrDeviceDX *device, RdrScreenPostProcess *ppscreen, WTCmdPacket *packet)
{
	RdrPostprocessExVertex * vertices;
	RdrSurfaceStateDX *current_state;
	int w, h;
	int i, buffer_count = 0;
	U32 j;
	bool bDrawTriangle = true;
	bool bUseTexCoords = ppscreen->use_texcoords;

	PERFINFO_AUTO_START_FUNC();

	CHECKDEVICELOCK(device);
	CHECKTHREAD;

	vertices = (RdrPostprocessExVertex*)AlignPointerUpPow2(_alloca(sizeof(RdrPostprocessExVertex)*4+0x10),16);

	etlAddEvent(device->device_base.event_timer, "Postprocess (screen)", ELT_CODE, ELTT_BEGIN);
	rxbxCompileMinimalShaders(device);

	if (!rxbxBindMaterial(device, &ppscreen->material, ppscreen->lights, RLCT_WORLD, ppscreen->shader_handle, ppscreen->uberlight_shader_num, false, false, false))
	{
		etlAddEvent(device->device_base.event_timer, "Postprocess (screen)", ELT_CODE, ELTT_END);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (ppscreen->stereoscopic && device->display_thread.stereoscopicActive)
		rxbxBindStereoscopicTexture(device);

	if (ppscreen->measurement_mode)
	{
		bDrawTriangle = true;
		bUseTexCoords = true;
	}
	else
		bDrawTriangle = !ppscreen->use_texcoords;

    if (ppscreen->use_normal_vertex_shader || bUseTexCoords)
    {
        if( bUseTexCoords )
        {
            rxbxSetupPostprocessNormalPsDrawMode(device, velements_postprocess_ex, &device->postprocess_screen_ex_vertex_declaration);
        }
        else
        {
            rxbxSetupPostprocessNormalPsDrawMode(device, velements_postprocess, &device->postprocess_screen_vertex_declaration);
        }
    }
    else
    {
        rxbxSetupPostProcessScreenDrawMode(device, velements_postprocess, &device->postprocess_screen_vertex_sprite_declaration, ppscreen->no_offset);
    }

	rxbxSetIndices(device, null_index_buffer, false);

	rxbxSetCullMode(device, CULLMODE_NONE);

	switch (ppscreen->blend_type)
	{
		xcase RPPBLEND_REPLACE:
			rxbxBlendFunc(device, false, D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD);

		xcase RPPBLEND_ALPHA:
			rxbxBlendFunc(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);

		xcase RPPBLEND_ADD:
			rxbxBlendFunc(device, true, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD);

		xcase RPPBLEND_SUBTRACT:
			rxbxBlendFunc(device, true, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT);

		xcase RPPBLEND_LOW_RES_ALPHA_PP:
			rxbxBlendFunc(device, true, D3DBLEND_ONE, D3DBLEND_SRCALPHA, D3DBLENDOP_ADD);
	}

	switch (ppscreen->depth_test_mode)
	{
		xcase RPPDEPTHTEST_OFF:
			rxbxDepthTest(device, DEPTHTEST_OFF);

		xcase RPPDEPTHTEST_LEQUAL:
			rxbxDepthTest(device, DEPTHTEST_LEQUAL);

		xcase RPPDEPTHTEST_LESS:
			rxbxDepthTest(device, DEPTHTEST_LESS);

		xcase RPPDEPTHTEST_EQUAL:
			rxbxDepthTest(device, DEPTHTEST_EQUAL);
	}

	rxbxDepthWritePush(device, ppscreen->write_depth?TRUE:FALSE);

	if (ppscreen->shadow_buffer_render)
		rxbxSetCubemapLookupTextureActive(device, true);

	current_state = rxbxGetCurrentState(device);
	w = device->active_surface->width_thread * current_state->viewport[1];
	h = device->active_surface->height_thread * current_state->viewport[3];
	rxbxSet2DMode(device, w, h);
	if ( ppscreen->fog )
		// override rxbxSet2DMode, which disables fog
		rxbxFog(device, 1);

	if (ppscreen->viewport_independent_textures)
		rxbxSetPPTextureSize(device, ppscreen->tex_width, ppscreen->tex_height, 
			1.0f, 1.0f, w, h,
			ppscreen->tex_offset_x, ppscreen->tex_offset_y,
			ppscreen->add_half_pixel);
	else
		rxbxSetPPTextureSize(device, 
			ppscreen->tex_width * current_state->viewport[1], ppscreen->tex_height * current_state->viewport[3], 
			current_state->viewport[1], current_state->viewport[3], 
			w, h,
			ppscreen->tex_offset_x, ppscreen->tex_offset_y,
			ppscreen->add_half_pixel);

	rxbxSetupAmbientLight(device, NULL, NULL, NULL, ppscreen->ambient, RLCT_WORLD);
	rxbxColorf(device, unitvec4);
	rxbxInstanceParam(device, unitvec4);

	for (i=0; i < SBUF_MAXMRT; i++)
	{
		if (device->active_surface->rendertarget[i].d3d_surface.typeless_surface)
			++buffer_count;
	}
	INC_SCREEN_POSTPROCESS(device->active_surface->width_thread, device->active_surface->height_thread, buffer_count);


	if (bDrawTriangle)
	{
		// Draw one oversize triangle to avoid having a seam in the middle of the screen (fixes one issue with GF7 cards with writing depth into an MSAA buffer)
		// CD: the extra 0.5 boundary is there to fix edge multisampling errors when postprocessing to a msaa enabled surface
		setVec3(vertices[0].pos, -0.5f, -h-0.5f, 0);
		setVec3(vertices[1].pos, -0.5f, h+0.5f, 0);
		setVec3(vertices[2].pos, w*2+0.5f, h+0.5f, 0);

		if (ppscreen->measurement_mode)
		{
			// replace tex coords with the interpolated range of luminance test values
			float delta = ppscreen->const_increment;
			vertices[0].texcoord[0] = evaluateIncrementPlane(vertices[0].pos, w, delta);
			vertices[1].texcoord[0] = evaluateIncrementPlane(vertices[1].pos, w, delta);
			vertices[2].texcoord[0] = evaluateIncrementPlane(vertices[2].pos, w, delta);

			ppscreen->draw_count = 1;
		}

		rxbxSetupVBODrawVerticesUP(device, 3, vertices, sizeof(vertices[0]), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    }
    else
    {
		static const U8 pp_vert_order[ 4 ] = { 0, 1, 3, 2 };
		static const Vec2 offsets[4] = { {0.5f, -0.5f}, {-0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f} };

		Vec4 texGradients[2];

		setVec3(vertices[0].pos, ppscreen->dest_quad[1][0]*w, ppscreen->dest_quad[1][1]*h, 0); // lr
		setVec3(vertices[1].pos, ppscreen->dest_quad[0][0]*w, ppscreen->dest_quad[1][1]*h, 0); // ll
		setVec3(vertices[2].pos, ppscreen->dest_quad[1][0]*w, ppscreen->dest_quad[0][1]*h, 0); // ur
		setVec3(vertices[3].pos, ppscreen->dest_quad[0][0]*w, ppscreen->dest_quad[0][1]*h, 0); // ul

		copyVec4(ppscreen->texcoords[pp_vert_order[0]], vertices[0].texcoord);
		copyVec4(ppscreen->texcoords[pp_vert_order[1]], vertices[1].texcoord);
		copyVec4(ppscreen->texcoords[pp_vert_order[2]], vertices[2].texcoord);
		copyVec4(ppscreen->texcoords[pp_vert_order[3]], vertices[3].texcoord);

		// Expand the quad vertex positions by an extra 0.5 boundary to completely cover the pixels, to prevent multisampling coverage seams when 
		// postprocessing to a msaa enabled surface
		if (!ppscreen->exact_quad_coverage)
		{
			int vertex;

			calcOrthoTexGradients(vertices, texGradients);

			// Shift positions and texels to account for the increasing the surface coverage.
			for (vertex = 0; vertex < 4; ++vertex)
			{
				addVec2(vertices[vertex].pos, offsets[vertex], vertices[vertex].pos);
#define scaleAddscaleAddVec4(a, sa, b, sb, c, r) ((r)[0] = ((a)[0]*(sa)+(b)[0]*(sb)+(c)[0]), (r)[1] = ((a)[1]*(sa)+(b)[1]*(sb)+(c)[1]), (r)[2] = ((a)[2]*(sa)+(b)[2]*(sb)+(c)[2]), (r)[3] = ((a)[3]*(sa)+(b)[3]*(sb)+(c)[3]))
				scaleAddscaleAddVec4(texGradients[0], offsets[vertex][0], texGradients[1], offsets[vertex][1], ppscreen->texcoords[pp_vert_order[vertex]], vertices[vertex].texcoord);
			}
		}

		rxbxSetupVBODrawVerticesUP(device, 4, vertices, sizeof(vertices[0]), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	}



	rxbxApplyStatePreDraw(device);

	if (!ppscreen->draw_count)
		ppscreen->draw_count = 1;

	if (ppscreen->occlusion_query)
	{
		rxbxStartOcclusionQueryDirect(device, NULL, ppscreen->occlusion_query);
		ppscreen->occlusion_query->max_pixel_count = ppscreen->draw_count * w * h;
	}

	for (j = 0; j < ppscreen->draw_count; ++j)
	{
#if _XBOX
		if (device->active_surface->tile_count > 1) {
			for (i=0; i<device->active_surface->tile_count; i++) {
				D3DRECT *rect = &device->active_surface->tile_rects[i];
				setVec3(vertices[0].pos, rect->x2, rect->y1, 0);
				setVec3(vertices[1].pos, rect->x1, rect->y1, 0);
				setVec3(vertices[2].pos, rect->x1, rect->y2, 0);
				setVec3(vertices[3].pos, rect->x2, rect->y2, 0);

				if (ppscreen->use_texcoords)
				{
					int it;
					for (it = 0; it != 4; ++it)
					{
						vertices[it].texcoord[0] = lerp(ppscreen->texcoords[1][0], ppscreen->texcoords[0][0], vertices[it].pos[0]);
						vertices[it].texcoord[1] = lerp(ppscreen->texcoords[1][1], ppscreen->texcoords[2][1], vertices[it].pos[1]);
						vertices[it].texcoord[2] = lerp(ppscreen->texcoords[1][2], ppscreen->texcoords[0][2], vertices[it].pos[0]);
						vertices[it].texcoord[3] = lerp(ppscreen->texcoords[1][3], ppscreen->texcoords[2][3], vertices[it].pos[1]);
					}
				}
	            
				IDirect3DDevice9_SetPredication(device->d3d_device, D3DPRED_TILE(i));
				IDirect3DDevice9_DrawVerticesUP(device->d3d_device, D3DPT_TRIANGLEFAN, 4,
					vertices, sizeof(vertices[0]));
			}
			IDirect3DDevice9_SetPredication(device->d3d_device, 0);
		} else
#endif
		{
			VALIDATE_DEVICE_DEBUG();
			if (!ppscreen->use_texcoords)
			{
				if (device->d3d11_device)
					ID3D11DeviceContext_Draw(device->d3d11_imm_context, 3, 0);
				else
					CHECKX(IDirect3DDevice9_DrawPrimitive(device->d3d_device, D3DPT_TRIANGLESTRIP, 0, 1));
            }
            else
            {
				if (device->d3d11_device)
					ID3D11DeviceContext_Draw(device->d3d11_imm_context, 4, 0);
				else
					CHECKX(IDirect3DDevice9_DrawPrimitive(device->d3d_device, D3DPT_TRIANGLESTRIP, 0, 2));
			}
		}

		if (!ppscreen->rdr_internal) {
			INC_DRAW_CALLS(2);
		}

		if (j+1 < ppscreen->draw_count && ppscreen->material.const_count)
		{
			F32 increment = ppscreen->const_increment * (j+1);
			Vec4 constants[1];
			addVec4same(ppscreen->material.constants[0], increment, constants[0]);
			rxbxPixelShaderConstantParameters(device, 0, constants, 1, PS_CONSTANT_BUFFER_MATERIAL);
			rxbxApplyStatePreDraw(device);
		}
	}
	// flush stream source since DrawXXXXXUP leaves them indeterminate

	if (ppscreen->occlusion_query)
		rxbxFinishOcclusionQueryDirect(device, device->last_rdr_query);

	rxbxSetCullMode(device, CULLMODE_BACK);
	rxbxDepthTest(device, DEPTHTEST_LEQUAL);
	rxbxDepthWritePop(device);

#if DEBUG_LOG_MRT_TO_DISK
	if ( g_bDebugLogPPEffects )
	{
		rxbxDumpSurfaces( device );
	}
#endif

	etlAddEvent(device->device_base.event_timer, "Postprocess (screen)", ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP();
}

void rxbxPostProcessShapeDirect(RdrDeviceDX *device, RdrShapePostProcess *ppshape, WTCmdPacket *packet)
{
	RdrGeometryDataDX *geo_data;
	int w, h;
	const RdrSurfaceStateDX *current_state;

	PERFINFO_AUTO_START_FUNC();

	CHECKDEVICELOCK(device);
	CHECKTHREAD;

	etlAddEvent(device->device_base.event_timer, "Postprocess (shape)", ELT_CODE, ELTT_BEGIN);

	geo_data = rxbxGetGeoDataDirect(device, ppshape->geometry);
	if (!geo_data)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	rxbxSetupPostProcessShapeDrawMode(device, velements_postprocess_shape, &device->postprocess_shape_vertex_declaration, ppshape->no_offset);

	if (!rxbxBindMaterial(device, &ppshape->material, ppshape->lights, RLCT_WORLD, ppshape->shader_handle, ppshape->uberlight_shader_num, false, false, false))
	{
		etlAddEvent(device->device_base.event_timer, "Postprocess (shape)", ELT_CODE, ELTT_END);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (ppshape->draw_back_faces)
		rxbxSetCullMode(device, CULLMODE_FRONT);

	switch (ppshape->blend_type)
	{
		xcase RPPBLEND_REPLACE:
			rxbxBlendFunc(device, false, D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD);

		xcase RPPBLEND_ALPHA:
			rxbxBlendFunc(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);

		xcase RPPBLEND_ADD:
			rxbxBlendFunc(device, true, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD);
	}

	switch (ppshape->depth_test_mode)
	{
		xcase RPPDEPTHTEST_OFF:
			rxbxDepthTest(device, DEPTHTEST_OFF);

		xcase RPPDEPTHTEST_LEQUAL:
			rxbxDepthTest(device, DEPTHTEST_LEQUAL);

		xcase RPPDEPTHTEST_LESS:
			rxbxDepthTest(device, DEPTHTEST_LESS);

		xcase RPPDEPTHTEST_EQUAL:
			rxbxDepthTest(device, DEPTHTEST_EQUAL);
	}

	rxbxDepthWritePush(device, ppshape->write_depth?TRUE:FALSE);

	if (ppshape->shadow_buffer_render)
		rxbxSetCubemapLookupTextureActive(device, true);

	rxbxSet3DMode(device, 0);

	current_state = rxbxGetCurrentState(device);
	w = device->active_surface->width_thread * current_state->viewport[1];
	h = device->active_surface->height_thread * current_state->viewport[3];
	rxbxSetPPTextureSize(device, 
		ppshape->tex_width * current_state->viewport[1], ppshape->tex_height * current_state->viewport[3], 
		current_state->viewport[1], current_state->viewport[3], 
		w, h,
		ppshape->tex_offset_x, ppshape->tex_offset_y,
		false);

	rxbxSetupAmbientLight(device, NULL, NULL, NULL, ppshape->ambient, RLCT_WORLD);
	rxbxColorf(device, unitvec4);
	rxbxInstanceParam(device, unitvec4);

	rxbxPushModelMatrix(device, ppshape->world_matrix, false, false);
	rxbxSetVertexStreamSource(device, 0, geo_data->aVertexBufferInfos[0].buffer, geo_data->aVertexBufferInfos[0].iStride, 0);
	rxbxSetIndices(device, geo_data->index_buffer, false);

	if (ppshape->occlusion_query)
		rxbxStartOcclusionQueryDirect(device, NULL, ppshape->occlusion_query);

	rxbxDrawIndexedTriangles(device, 0, geo_data->base_data.tri_count, geo_data->base_data.vert_count, 0, true); 

	if (ppshape->occlusion_query)
		rxbxFinishOcclusionQueryDirect(device, device->last_rdr_query);

	rxbxPopModelMatrix(device);

	if (ppshape->draw_back_faces)
		rxbxSetCullMode(device, CULLMODE_BACK);

	rxbxDepthTest(device, DEPTHTEST_LEQUAL);

	rxbxDepthWritePop(device);

	etlAddEvent(device->device_base.event_timer, "Postprocess (shape)", ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP();
}


