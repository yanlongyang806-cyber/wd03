#include "EventTimingLog.h"
#include "mathutil.h"
#include "Color.h"
#include "MemRef.h"

#include "RdrState.h"
#include "rt_xsprite.h"
#include "rt_xdrawmode.h"
#include "rt_xdevice.h"

//Make sure these are the same in GfxSpriteList.c
#if !PLATFORM_CONSOLE
const static int idxsPerQuad = 6; //no D3DPT_QUADLIST
#else
const static int idxsPerQuad = 4;
#endif

#define idx_buffer16or32_notnull (idx_buffer ? !!idx_buffer : !!idx_buffer32)
#define idx_buffer16or32(i) (idx_buffer ? (U32)(idx_buffer[i]) : idx_buffer32[i])

static void rxbxSetScissorRect(RdrDeviceDX * device, const RECT * scissor_rect)
{
	if (device->d3d11_device)
		ID3D11DeviceContext_RSSetScissorRects(device->d3d11_imm_context, 1, scissor_rect);
	else
		CHECKX(IDirect3DDevice9_SetScissorRect(device->d3d_device, scissor_rect));
}

void rxbxSpriteBindBlendSetup(RdrDeviceDX *device, RdrSpriteState *state, bool wireframe)
{
	if (wireframe)
	{
		rxbxBindWhiteTexture(device, 0);
		rxbxBindWhiteTexture(device, 1);
		rxbxBlendFunc(device, false, D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD);
	}
	else
	{
		rxbxBindTexture(device, 0, state->tex_handle1);
		//this member is in a union and is invalid if we are using the distance field effects
		if (state->sprite_effect < RdrSpriteEffect_DistField1Layer || state->sprite_effect > RdrSpriteEffect_DistField2LayerGradient)
			rxbxBindTexture(device, 1, state->tex_handle2);

		if (state->additive)
			rxbxBlendFunc(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD);
		else
			// JE: Changed this back, as sometimes for unknown reasons this would cause Alpha = 0 for all pixels in the framebuffer
			rxbxBlendFunc(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		// // JE:Changing blend function to maintain alpha in framebuffer (only needed for cursors... does not seem to affect performance...)
		//rxbxBlendFuncSeparate(device, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD,
		//	D3DBLEND_ONE, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
	}
}

__forceinline static U32 drawSpritesInline(RdrDeviceDX *device, RdrSpritesPkg *pkg, RdrSpriteState *state, U32 sprite_count, RdrSpriteVertex *vertices, U32 vertices_size, U16* idx_buffer, U32* idx_buffer32, int first_idx, int vbo_base_idx, bool wireframe, bool use_vbo_for_idx_draw)
{
	U32 i, j, vertex_count = sprite_count * 4;
	int total_area = 0;
	int old_state = -1;
	
	PERFINFO_AUTO_START_FUNC();

	if (rdr_state.showSpriteCounters)
	{
		PERFINFO_AUTO_START_L2("calc area", vertex_count >> 2);
		for (i = 0; i < sprite_count; i++)
		{
			// assume the sprites are rectangular (rotated is fine)
			RdrSpriteVertex* cur_verts = idx_buffer16or32_notnull ? pkg->vertices + idx_buffer16or32(i*idxsPerQuad+first_idx) : vertices + i*4;
			Vec2 a, b;
			F32 area;
			subVec2(cur_verts[1].point, cur_verts[0].point, a);
			subVec2(cur_verts[2].point, cur_verts[0].point, b);
			area = crossVec2(a, b); // = double the triangle area
			area = ABS(area);
			total_area += round(ceil(area));

			for (j = 0; j < RDR_SPRITE_SIZE_BUCKET_COUNT; ++j)
			{
				if (area < sprite_histogram_sizes[j])
				{
					INC_OPERATION(sprite_count_histogram[j], 1);
					break;
				}
			}
		}
		PERFINFO_AUTO_STOP_L2();
	}
	

	rxbxSpriteBindBlendSetup(device, state, wireframe);

	for (j = 0; j < sprite_count; )
	{
		int count;
		int cur_base_idx = (j * idxsPerQuad) + first_idx;
		RdrSpriteState* cur_state = idx_buffer16or32_notnull ? pkg->states + (idx_buffer16or32(cur_base_idx) >> 2) : state + j;
		int new_state;
		bool apply_state;

		if (cur_state->use_scissor && !wireframe)
		{
			RECT scissor_rect;
			PERFINFO_AUTO_START_L2("scissor", 1);

			scissor_rect.left = cur_state->scissor_x;
			scissor_rect.right = cur_state->scissor_x + cur_state->scissor_width;
			scissor_rect.top = pkg->screen_height - (cur_state->scissor_y + cur_state->scissor_height);
			scissor_rect.bottom = pkg->screen_height - cur_state->scissor_y;

			// clamp scissor to surface bounds
			scissor_rect.right = CLAMP(scissor_rect.right, 0, device->active_surface->width_thread);
			scissor_rect.bottom = CLAMP(scissor_rect.bottom, 0, device->active_surface->height_thread);
			scissor_rect.left = CLAMP(scissor_rect.left, 0, scissor_rect.right);
			scissor_rect.top = CLAMP(scissor_rect.top, 0, scissor_rect.bottom);

			rxbxSetScissorRect(device, &scissor_rect);

			INC_STATE_CHANGE(scissor_rect, 1);

			PERFINFO_AUTO_STOP_L2();

			count = 1;
			for (i = j+1; i < sprite_count; ++i)
			{
				int cur_base_idx2 = (i * idxsPerQuad) + first_idx;
				RdrSpriteState* cur_state2 = idx_buffer16or32_notnull ? pkg->states + (idx_buffer16or32(cur_base_idx2) >> 2) : state + i;
				if (!spriteScissorStatesEqual(cur_state, cur_state2))
					break;
				count++;
			}
			rxbxSetScissorTest(device, TRUE);
			new_state = 1-old_state;
		}
		else
		{
			count = sprite_count - j;
			rxbxSetScissorTest(device, FALSE);
			new_state = 0;
		}

		apply_state = (old_state != new_state ? (old_state = new_state, true) : false);

		if (idx_buffer16or32_notnull)
		{
			if (use_vbo_for_idx_draw)
			{
				rxbxDrawIndexedQuads(device, count, vbo_base_idx, cur_base_idx, 0, vertices_size, apply_state);
			}
			else
			{
				if (idx_buffer32)
					rxbxDrawIndexedQuads32UP(device, count, idx_buffer32 + cur_base_idx, pkg->vertices, vertices_size, sizeof(*vertices), apply_state, true);
				else
					rxbxDrawIndexedQuadsUP(device, count, idx_buffer + cur_base_idx, pkg->vertices, vertices_size, sizeof(*vertices), apply_state, true);
			}
		}
		else
		{
			rxbxDrawQuadsUP(device, count, vertices + j * 4, sizeof(*vertices), apply_state);
		}
		
		INC_OPERATION(sprite_draw_call_count, 1);
		INC_OPERATION(sprite_triangle_count, count * 2);

		j += count;
	}

	INC_OPERATION(sprite_pixel_count, total_area);

	PERFINFO_AUTO_STOP();

	//return the amount to move forward in whatever list (vertex or index)
	return idx_buffer16or32_notnull ? idxsPerQuad * sprite_count : vertex_count;
}

static VertexComponentInfo velements_sprite[] = 
{
	{ offsetof(RdrSpriteVertex,point), VPOSSPRITE },
	{ offsetof(RdrSpriteVertex,color), VCOLORU8_IDX0 },
	{ offsetof(RdrSpriteVertex,texcoords), V2TEXCOORD32 },
	{ 0, VTERMINATE }
};

void rxbxInitSpriteVertexDecl(RdrDeviceDX *device)
{
	rxbxSetupSpriteVdecl(device, velements_sprite, &device->sprite_vertex_declaration);
}

static __forceinline void prepareDistfieldShaderMadstepParams(F32 mn, F32 mx, Vec2 outParams)
{
	//want : saturate((v - mn) / (mx - mn))
	F32 delta = mx - mn;
	const F32 sharpen = 0.18; // increase tightness to get the same visual look/good AA
	mx -= delta*sharpen;
	mn += delta*sharpen;
	outParams[0] = 1.f/AVOID_DIV_0(mx - mn);
	outParams[1] = -mn * outParams[0];
}

static __forceinline void freeOrMemRefDecrement(void* data, bool isMemRef)
{
	if (isMemRef)
	{
		if (data)
			memrefDecrement(data);
	}
	else
	{
		free(data);
	}
}

void rxbxSpriteFlushCurrentEffect()
{
	rdr_state.spriteCurrentEffect = RdrSpriteEffect_Undefined;
}

void rxbxSpriteEffectSetup(RdrDeviceDX *device, RdrSpriteState *cur_state)
{
	static F32 current_effect_weight = -9e9;

	if (cur_state->sprite_effect != rdr_state.spriteCurrentEffect) 
	{
		rdr_state.spriteCurrentEffect = cur_state->sprite_effect;
		rxbxSetSpriteEffectBlendMode(device, rdr_state.spriteCurrentEffect);
		current_effect_weight = -9e9;
	}
	if (rdr_state.spriteCurrentEffect)	
	{
		if (rdr_state.spriteCurrentEffect == RdrSpriteEffect_DistField1Layer || rdr_state.spriteCurrentEffect == RdrSpriteEffect_DistField1LayerGradient) 
		{
			int paramOffset = 0;
			//this corresponds to the LAYER_INFO struct in dist_field_sprite_2d.phl
			Vec4 layerInfo[4];

			if (rdr_state.spriteCurrentEffect == RdrSpriteEffect_DistField1LayerGradient)
			{
				Vec4 gradInfo[3];
				float top = cur_state->df_grad_settings.startStopPts[0];
				float bottom = cur_state->df_grad_settings.startStopPts[1];
				rgbaToVec4(gradInfo[0], cur_state->df_grad_settings.rgbaTopColor);
				rgbaToVec4(gradInfo[1], cur_state->df_grad_settings.rgbaBottomColor);
				// x = -top/(bottom - top), y = 1/(bottom - top), z = unused, w = unused
				setVec4(gradInfo[2], -top/AVOID_DIV_0(bottom - top), 1.0f/AVOID_DIV_0(bottom - top), 0, 0);
				rxbxPixelShaderConstantParameters(device, paramOffset, gradInfo, 3, PS_CONSTANT_BUFFER_MATERIAL);
				paramOffset += 3;
			}

			setVec4(layerInfo[0], cur_state->df_layer_settings[0].offset[0], cur_state->df_layer_settings[0].offset[1], 0, 0);
			//skipping the maxDen parameter since the shader currently ignores it
			//minDen
			prepareDistfieldShaderMadstepParams(cur_state->df_layer_settings[0].densityRange[0] - cur_state->df_layer_settings[0].densityRange[2], cur_state->df_layer_settings[0].densityRange[0] + cur_state->df_layer_settings[0].densityRange[2], layerInfo[1]);
			//outlineDen
			prepareDistfieldShaderMadstepParams(cur_state->df_layer_settings[0].densityRange[1] - cur_state->df_layer_settings[0].densityRange[2], cur_state->df_layer_settings[0].densityRange[1] + cur_state->df_layer_settings[0].densityRange[2], layerInfo[1]+2);
			rgbaToVec4(layerInfo[2], cur_state->df_layer_settings[0].rgbaColorMain);
			rgbaToVec4(layerInfo[3], cur_state->df_layer_settings[0].rgbaColorOutline);

			rxbxPixelShaderConstantParameters(device, paramOffset, layerInfo, 4, PS_CONSTANT_BUFFER_MATERIAL);
			paramOffset += 4;
		}
		else if (rdr_state.spriteCurrentEffect == RdrSpriteEffect_DistField2Layer || rdr_state.spriteCurrentEffect == RdrSpriteEffect_DistField2LayerGradient)	
		{
			int paramOffset = 0;
			Vec4 layerInfo[8];

			if (rdr_state.spriteCurrentEffect == RdrSpriteEffect_DistField2LayerGradient)
			{
				Vec4 gradInfo[3];
				float top = cur_state->df_grad_settings.startStopPts[0];
				float bottom = cur_state->df_grad_settings.startStopPts[1];
				rgbaToVec4(gradInfo[0], cur_state->df_grad_settings.rgbaTopColor);
				rgbaToVec4(gradInfo[1], cur_state->df_grad_settings.rgbaBottomColor);
				// x = -top/(bottom - top), y = 1/(bottom - top), z = unused, w = unused
				setVec4(gradInfo[2], -top/AVOID_DIV_0(bottom - top), 1.0f/AVOID_DIV_0(bottom - top), 0, 0);
				rxbxPixelShaderConstantParameters(device, paramOffset, gradInfo, 3, PS_CONSTANT_BUFFER_MATERIAL);
				paramOffset += 3;
			}
			//layer 1
			setVec4(layerInfo[0], cur_state->df_layer_settings[0].offset[0], cur_state->df_layer_settings[0].offset[1], 0, 0);
			//skipping the maxDen parameter since the shader currently ignores it
			//minDen
			prepareDistfieldShaderMadstepParams(cur_state->df_layer_settings[0].densityRange[0] - cur_state->df_layer_settings[0].densityRange[2], cur_state->df_layer_settings[0].densityRange[0] + cur_state->df_layer_settings[0].densityRange[2], layerInfo[1]);
			//outlineDen
			prepareDistfieldShaderMadstepParams(cur_state->df_layer_settings[0].densityRange[1] - cur_state->df_layer_settings[0].densityRange[2], cur_state->df_layer_settings[0].densityRange[1] + cur_state->df_layer_settings[0].densityRange[2], layerInfo[1]+2);
			rgbaToVec4(layerInfo[2], cur_state->df_layer_settings[0].rgbaColorMain);
			rgbaToVec4(layerInfo[3], cur_state->df_layer_settings[0].rgbaColorOutline);

			//layer 2
			setVec4(layerInfo[4], cur_state->df_layer_settings[1].offset[0], cur_state->df_layer_settings[1].offset[1], 0, 0);
			//skipping the maxDen parameter since the shader currently ignores it
			//minDen
			prepareDistfieldShaderMadstepParams(cur_state->df_layer_settings[1].densityRange[0] - cur_state->df_layer_settings[1].densityRange[2], cur_state->df_layer_settings[1].densityRange[0] + cur_state->df_layer_settings[1].densityRange[2], layerInfo[5]);
			//outlineDen
			prepareDistfieldShaderMadstepParams(cur_state->df_layer_settings[1].densityRange[1] - cur_state->df_layer_settings[1].densityRange[2], cur_state->df_layer_settings[1].densityRange[1] + cur_state->df_layer_settings[1].densityRange[2], layerInfo[5]+2);
			rgbaToVec4(layerInfo[6], cur_state->df_layer_settings[1].rgbaColorMain);
			rgbaToVec4(layerInfo[7], cur_state->df_layer_settings[1].rgbaColorOutline);

			rxbxPixelShaderConstantParameters(device, paramOffset, layerInfo, 8, PS_CONSTANT_BUFFER_MATERIAL);
			paramOffset += 8;
		}
		else if (current_effect_weight != cur_state->sprite_effect_weight)
		{
			Vec4 parameter;
			current_effect_weight = cur_state->sprite_effect_weight;
			setVec4(parameter, current_effect_weight, 0, 0, 0);
			rxbxPixelShaderConstantParameters(device, 0, (const Vec4*)parameter, 1, PS_CONSTANT_BUFFER_MATERIAL);
		}
	}
}

void rxbxDrawSpritesDirect(RdrDeviceDX *device, RdrSpritesPkg *pkg, WTCmdPacket *packet)
{
	int k, draw_rep_start = 0, draw_rep_count = 1;
	bool useVBO = false;
	int vbo_offset;
	RdrVertexBufferObj tempVBO = { NULL };
	RdrIndexBufferObj tempIdxBuffer = { NULL };

	PERFINFO_AUTO_START_FUNC();

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (!(rdr_state.dbg_type_flags & RDRTYPE_SPRITE))
	{

		if (rdr_state.spriteWireframe)
		{
			draw_rep_count = 2;
			if (rdr_state.spriteWireframe == 2)
				draw_rep_start = 1;
		}

		etlAddEvent(device->device_base.event_timer, "Draw sprites", ELT_CODE, ELTT_BEGIN);

		if (rdr_state.max_gpu_frames_ahead)
		{
			if (device->sprite_draw_sync_query_frame_indices[device->cur_frame_index] == 0) // Only once per frame
				device->sprite_draw_sync_query_frame_indices[device->cur_frame_index] = 1+rxbxIssueSyncQuery(device);
		}

		rxbxSet2DMode(device, pkg->screen_width, pkg->screen_height);

		rxbxDepthTest(device, DEPTHTEST_OFF);
		rxbxDepthWritePush(device, FALSE);

		// all sprites use only max of two textures, so unbind the unused textures only once
		rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
		rxbxBindTexture(device, 0, 0);
		rxbxBindTexture(device, 1, 0);
		rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);

		if (pkg->indices || pkg->indices32)
		{
			assertmsg(!pkg->indices || !pkg->indices32, "You can't set both of these, it makes no sense.");
			useVBO = rxbxAllocTempVBOMemory(device, pkg->vertices, pkg->sprite_count*4*sizeof(*pkg->vertices), &tempVBO, &vbo_offset, false);
			if (useVBO)
			{
				tempIdxBuffer = rxbxGetSpriteIndexBuffer(device, device->current_sprite_index_buffer, pkg->indices, pkg->indices32, pkg->sprite_count * idxsPerQuad);
				rxbxSetVertexStreamSource(device, 0, tempVBO, sizeof(*pkg->vertices), vbo_offset);
				rxbxSetIndices(device, tempIdxBuffer, !!pkg->indices32);
				device->current_sprite_index_buffer++;
				if (device->current_sprite_index_buffer == ARRAY_SIZE(device->sprite_index_buffer_info))
					device->current_sprite_index_buffer = 0;
			}

		}
	
		for (k = draw_rep_start; k < draw_rep_count; ++k)
		{
			U32 i, j, offset = 0;
			RdrSpriteState	*states =  pkg->states;
			RdrSpriteVertex *verts = pkg->vertices;
			U16				*idx_buffer = pkg->indices;
			U32				*idx_buffer32 = pkg->indices32;

			rxbxSetupSpriteDrawMode(device, device->sprite_vertex_declaration);
			if (k != 0)
			{
				// wireframe drawing
				rxbxSetCullMode(device, CULLMODE_NONE);
				rxbxSetFillMode(device, D3DFILL_WIREFRAME);
			}

			rxbxSpriteFlushCurrentEffect();
			rxbxSetSpriteEffectBlendMode(device, RdrSpriteEffect_None);

			for (j = 0; j < pkg->sprite_count; )
			{
				int count = 1;
			
				RdrSpriteState* cur_state = idx_buffer16or32_notnull ? states + (idx_buffer16or32(offset) >> 2) : states + j;
				int cur_idx_pos2 = offset + idxsPerQuad;

				PERFINFO_AUTO_START_L2("spriteStatesEqual", 1);
				for (i = j+1; i < pkg->sprite_count; i++)
				{
					RdrSpriteState* cur_state2 = idx_buffer16or32_notnull ? states + (idx_buffer16or32(cur_idx_pos2) >> 2) : states + i;
				
					if (!spriteStatesEqual(cur_state, cur_state2))
						break;
					count++;
					cur_idx_pos2 += idxsPerQuad;
				}
				PERFINFO_AUTO_STOP_L2();

				if (k == 0)
				{
					rxbxSpriteEffectSetup(device, cur_state);
				}

				offset += drawSpritesInline(device, pkg, cur_state, count, verts + offset, pkg->sprite_count*4, idx_buffer, idx_buffer32, offset, 0, k==1, useVBO);
				j += count;
			}

			if (k == 1)
			{
				rxbxSetCullMode(device, CULLMODE_BACK);
				rxbxSetFillMode(device, D3DFILL_SOLID);
			}
		}

		// reset these
		rxbxSetScissorTest(device, FALSE);

		rxbxDepthTest(device, DEPTHTEST_LEQUAL);
		rxbxDepthWritePop(device);
	}

	//we need to free these since they don't use the linear allocator
	if (pkg->should_free_contiguous_block)
	{
		RdrSpritesPkg *realPkg = (RdrSpritesPkg *)pkg->states - 1;
		free(realPkg);
	}
	else
	{
		if (pkg->should_free_states)
			freeOrMemRefDecrement(pkg->states, pkg->arrays_are_memref);
		if (pkg->should_free_vertices)
			freeOrMemRefDecrement(pkg->vertices, pkg->arrays_are_memref);
		if (pkg->should_free_indices)
		{
			freeOrMemRefDecrement(pkg->indices, pkg->arrays_are_memref);
			freeOrMemRefDecrement(pkg->indices32, pkg->arrays_are_memref);
		}
	}

	etlAddEvent(device->device_base.event_timer, "Draw sprites", ELT_CODE, ELTT_END);

	PERFINFO_AUTO_STOP();
}

void rxbxFreeSpriteIndexBuffer(RdrDeviceDX *device, int index)
{
	if (!device->sprite_index_buffer_info[index].index_buffer.typeless_index_buffer)
		return;

	PERFINFO_AUTO_START_FUNC();
	rxbxNotifyIndexBufferFreed(device, device->sprite_index_buffer_info[index].index_buffer);
#if _XBOX
	IDirect3DIndexBuffer9_BlockUntilNotBusy(device->sprite_index_buffer_info[index].index_buffer);
#endif
	rxbxReleaseIndexBuffer(device, device->sprite_index_buffer_info[index].index_buffer);
	rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:SpriteIndexBuffer", 1, -(S32)device->sprite_index_buffer_info[index].index_buffer_size);

	device->sprite_index_buffer_info[index].index_buffer_size = 0;
	device->sprite_index_buffer_info[index].index_buffer.typeless_index_buffer = 0;
	device->sprite_index_buffer_info[index].is_32_bit = false;
	PERFINFO_AUTO_STOP();
}

static bool sprite_index_buffer_just_resized;
RdrIndexBufferObj rxbxGetSpriteIndexBuffer(RdrDeviceDX *device, int index, U16* data, U32* data32, int index_count)
{
	RdrIndexBufferObj ret;
	bool is_32 = data32 ? true : false;
	U32 new_size, copy_size;
	RdrIBOMemoryDX ibo_chunk = { 0 };
	HRESULT hr;
	PERFINFO_AUTO_START_FUNC();

	assert(data32 || index_count < 65536/4*idxsPerQuad);

	sprite_index_buffer_just_resized = false;
	new_size = is_32 ? index_count * sizeof(U32) : 0xFFFF/4 * idxsPerQuad * sizeof(U16); //since its 16-bit this is largest useful size since we can't repeat the same quads
	copy_size = is_32 ? index_count * sizeof(U32) : index_count * sizeof(U16);

	if (is_32 != device->sprite_index_buffer_info[index].is_32_bit || new_size > device->sprite_index_buffer_info[index].index_buffer_size)
		rxbxFreeSpriteIndexBuffer(device, index);

	if (!device->sprite_index_buffer_info[index].index_buffer.typeless_index_buffer)
	{
		sprite_index_buffer_just_resized = true;
		device->sprite_index_buffer_info[index].index_buffer_size = new_size;
		rxbxCreateIndexBuffer(device, new_size, BUF_DYNAMIC|(is_32?BUF_32BIT_INDEX:0), &ret);
		device->sprite_index_buffer_info[index].index_buffer = ret;
		device->sprite_index_buffer_info[index].is_32_bit = is_32;

		rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:SpriteIndexBuffer", 1, (S32)new_size);
	}
	else
	{
		ret = device->sprite_index_buffer_info[index].index_buffer;
	}

	ibo_chunk.device = device;
	ibo_chunk.used_bytes = 0;
	ibo_chunk.ibo = ret;
	ibo_chunk.size_bytes = device->sprite_index_buffer_info[index].index_buffer_size;
	hr = rxbxAppendIBOMemory(device, &ibo_chunk, is_32 ? (void*)data32 : (void*)data, copy_size);
	rxbxFatalHResultErrorf(device, hr, "Locking sprite index buffer", "");
	ADD_MISC_COUNT(copy_size, "bytes copied");

	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}
