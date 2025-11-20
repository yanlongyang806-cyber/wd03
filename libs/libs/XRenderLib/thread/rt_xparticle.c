#include "rt_xparticle.h"

#include "RdrState.h"
#include "ScratchStack.h"
#include "rt_xdrawmode.h"
#include "rt_xfastparticles.h"
#include "rt_xprimitive.h"

#include "qsortG.h"

static VertexComponentInfo velements_particle[] = 
{
	{ offsetof(RdrParticleVertex,point), VPOS },
	{ offsetof(RdrParticleVertex,normal), VNORMAL32 },
	{ offsetof(RdrParticleVertex,texcoord), VTEXCOORD32 },
	{ offsetof(RdrParticleVertex,color), VCOLOR },
	{ offsetof(RdrParticleVertex,tangent), VTANGENT32 },
	{ offsetof(RdrParticleVertex,binormal), VBINORMAL32 },
	{ offsetof(RdrParticleVertex,tightenup), VTIGHTENUP },
	{ 0, VTERMINATE },
};

static VertexComponentInfo velements_fast_particle[] = 
{
	{ offsetof(RdrFastParticleVertex,point), VPOS },
	{ offsetof(RdrFastParticleVertex,corner_nodeidx), VSMALLIDX },
	{ offsetof(RdrFastParticleVertex,time), VFLOAT },
	{ offsetof(RdrFastParticleVertex,seed), VTIGHTENUP },
	{ offsetof(RdrFastParticleVertex,alpha), VALPHA },
	{ 0, VTERMINATE },
};

static VertexComponentInfo velements_fast_particle_streak[] = 
{
	{ offsetof(RdrFastParticleStreakVertex,point), VPOS },
	{ offsetof(RdrFastParticleStreakVertex,streak_dir), VNORMAL32 },
	{ offsetof(RdrFastParticleStreakVertex,corner_nodeidx), VSMALLIDX },
	{ offsetof(RdrFastParticleStreakVertex,time), VFLOAT },
	{ offsetof(RdrFastParticleStreakVertex,seed), VTIGHTENUP },
	{ offsetof(RdrFastParticleStreakVertex,alpha), VALPHA },
	{ 0, VTERMINATE },
};

static VertexComponentInfo velements_cylindertrail[] = 
{
	//{ 0, offsetof(RdrCylinderTrailVertex,point), VPOS },
	//{ 0, offsetof(RdrCylinderTrailVertex,texcoord), VTEXCOORD0 },
	//{ 0, offsetof(RdrCylinderTrailVertex,color), VCOLOR },
	{ offsetof(RdrCylinderTrailVertex,boneidx), VSMALLIDX },
	{ offsetof(RdrCylinderTrailVertex,angle), VFLOAT },
	//{ 0, offsetof(RdrCylinderTrailVertex,normal), VNORMAL32 },
	//{ 0, offsetof(RdrCylinderTrailVertex,binormal), VBINORMAL32 },
	//{ 0, offsetof(RdrCylinderTrailVertex,tangent), VTANGENT32 },
	{ 0, VTERMINATE },
};


__forceinline static U32 drawParticlesInline(RdrDeviceDX *device, RdrMaterial *material, RdrDrawableParticle *particle_state, int particle_count, void *vertices, RdrSortBucketType sort_bucket_type, bool is_low_res_edge_pass, RdrSortNode *sort_node)
{
	int vertex_count = particle_count * 4;
	RdrMaterialFlags flags = material->flags | particle_state->blend_flags;
	U32 i;

	PERFINFO_AUTO_START_FUNC_L2();

	rxbxSetupParticleDrawMode(device, velements_particle, &device->particle_vertex_declaration, sort_bucket_type == RSBT_WIREFRAME || material->no_normalmap, sort_node->uses_far_depth_range);

	if (sort_bucket_type != RSBT_WIREFRAME)
	{
		rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
		for (i=0; i<material->tex_count; i++)
		{
			if (i == 0 && particle_state->tex_handle0)
				rxbxBindTexture(device, i, particle_state->tex_handle0);
			else if (i == 1 && particle_state->tex_handle1)
				rxbxBindTexture(device, i, particle_state->tex_handle1);
			else
				rxbxBindTexture(device, i, material->textures[i]);
		}
		rxbxSetShadowBufferTextureActive(device, sort_node->uses_shadowbuffer);
		rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);

		if (material->const_count)
			rxbxPixelShaderConstantParameters(device, 0, material->constants, material->const_count, PS_CONSTANT_BUFFER_MATERIAL);

		if (sort_bucket_type == RSBT_ALPHA_LOW_RES_ALPHA)
		{
			// Technically could happen, so don't actually assert
			rxbxBlendFuncPushNop(device);
			rxbxBlendFuncSeparate(device, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD,
								  D3DBLEND_ZERO, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD );
		}
		else if ((sort_bucket_type == RSBT_ALPHA_LOW_RES_ADDITIVE || sort_bucket_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE) && !is_low_res_edge_pass)
		{
			rxbxBlendFuncPushNop(device);
			rxbxBlendFuncSeparate(device, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD,
								  D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD );
			rxbxFogColorPush(device, zerovec4, zerovec4);
		}
		else if (flags & RMATERIAL_ADDITIVE)
		{
			rxbxBlendFunc(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD);
			rxbxFogColorPush(device, zerovec4, zerovec4);
		}
		else if (flags & RMATERIAL_SUBTRACTIVE)
		{
			rxbxBlendFunc(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT);
			rxbxFogColorPush(device, zerovec4, zerovec4);
		}
		else
		{
			rxbxBlendFunc(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		}
	}

	rxbxDrawQuadsUP(device, particle_count, vertices, sizeof(RdrParticleVertex), true);

	if (sort_bucket_type != RSBT_WIREFRAME && (flags & (RMATERIAL_ADDITIVE|RMATERIAL_SUBTRACTIVE)))
		rxbxFogColorPop(device);

	PERFINFO_AUTO_STOP_L2();

	return vertex_count;
}

static int cmpParticleLink(const RdrDrawableParticleLinkList **plink1, const RdrDrawableParticleLinkList **plink2)
{
	F32 d = (*plink2)->zdist - (*plink1)->zdist; // sort back to front
	if (!d)
		return 0;
	return SIGN(d);
}

static RdrParticleVertex *copyVerts(RdrParticleVertex *verts, RdrDrawableParticleLinkList *particle_links, int *particle_count, RdrDrawableParticleLinkList **sorted_links)
{
	int count = particle_links->particle_count;
	*particle_count += count;

	if (particle_links->particle_count == 1)
	{
		memcpy(verts, particle_links->verts, 4 * sizeof(RdrParticleVertex));
	}
	else
	{
		int i = 0;

		while (particle_links)
		{
			sorted_links[i++] = particle_links;
			particle_links = particle_links->next;
		}
		assert(i == count);

		// CD: this will be sorting more than once for any particles drawn more than once (like low-res alpha), 
		// so the sorting should really be moved into rt_xdrawlist.c
		qsortG(sorted_links, count, sizeof(*sorted_links), cmpParticleLink);
		
		for (i = 0; i < count; ++i)
			memcpy(verts + i * 4, sorted_links[i]->verts, 4 * sizeof(RdrParticleVertex));
	}

	return verts + count * 4;
}

void rxbxDrawParticlesDirect(RdrDeviceDX *device, RdrSortNode **nodes, int node_count, 
							 RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data, 
							 bool is_low_res_edge_pass, bool manual_depth_test)
{
#if !_PS3
	int i, j, particle_max = 0, single_particle_max = 1, stencil_mode = 0, next_stencil_mode;
	RdrDrawableParticleLinkList **sorted_links = NULL;
	RdrParticleVertex *verts, *vert_ptr;
	U8 * pBuffer;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (!node_count || (rdr_state.dbg_type_flags & RDRTYPE_PARTICLE))
		return;

	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < node_count; ++i)
	{
		RdrDrawableParticle *particle = (RdrDrawableParticle*)nodes[i]->drawable;
		particle_max += particle->particles->particle_count;
		MAX1(single_particle_max, particle->particles->particle_count);
	}

	pBuffer = ScratchAlloc(particle_max * 4 * sizeof(RdrParticleVertex) + 0xf);
	verts = AlignPointerUpPow2(pBuffer,16);
	if (single_particle_max > 1)
		sorted_links = ScratchAlloc(single_particle_max * sizeof(RdrDrawableParticleLinkList *));

	if (((RdrDrawableParticle*)nodes[0]->drawable)->is_screen_space)
	{
		// JE: Disable depth testing for 2D particle systems for performance and because we don't clear the depth buffer for 2D drawing
		rxbxDepthTest(device, DEPTHTEST_OFF);
	}

	if (sort_bucket_type == RSBT_WIREFRAME)
	{
		rxbxSetWireframePixelShader(device);
		rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
		rxbxBindWhiteTexture(device, 0);
		rxbxBindWhiteTexture(device, 1);
		rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);

		rxbxFogPush(device, 0);
		rxbxSetCullMode(device, CULLMODE_NONE);
		rxbxSetFillMode(device, D3DFILL_WIREFRAME);
		rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		rxbxDepthWritePush(device, TRUE);
		rxbxColorWritePush(device, TRUE);

		rxbxColorf(device, unitvec4);
		rxbxInstanceParam(device, unitvec4);
		rxbxSetupAmbientLight(device, NULL, NULL, NULL, NULL, RLCT_WORLD);
	}
	else
	{
		rxbxFogPush(device, 1);
		rxbxDepthWritePush(device, FALSE);
		rxbxSetCullMode(device, CULLMODE_NONE);
		rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		rxbxColorWritePush(device, TRUE);

		rxbxSetupAmbientLight(device, NULL, NULL, NULL, NULL, RLCT_WORLD);
	}

	for (i = 0; i < node_count; ++i)
	{
		bool bOkayToDraw = true;
		RdrDrawableParticle *particle = (RdrDrawableParticle*)nodes[i]->drawable;
		int node_material_flags, particle_count = 0;

		if (particle->is_screen_space)
			rxbxSet2DMode(device, 1, 1);
		else
			rxbxSet3DMode(device, 0);

		vert_ptr = copyVerts(verts, particle->particles, &particle_count, sorted_links);

		for (j = i+1; j < node_count; ++j)
		{
			RdrDrawableParticle *particle2 = (RdrDrawableParticle*)nodes[j]->drawable;

			if (sort_bucket_type == RSBT_WIREFRAME)
			{
				if (particle->is_screen_space != particle2->is_screen_space)
				{
					break;
				}
			}
			else
			{
				int node_material_flags1 = SAFE_MEMBER(nodes[i]->material, flags) | nodes[i]->add_material_flags;
				int node_material_flags2 = SAFE_MEMBER(nodes[j]->material, flags) | nodes[j]->add_material_flags;
				if (particle->is_screen_space != particle2->is_screen_space ||
					nodes[i]->draw_shader_handle != nodes[j]->draw_shader_handle ||
					nodes[i]->material != nodes[j]->material || 
					nodes[i]->uses_far_depth_range != nodes[j]->uses_far_depth_range || 
					particle->blend_flags != particle2->blend_flags ||
					particle->tex_handle0 != particle2->tex_handle0 ||
					particle->tex_handle1 != particle2->tex_handle1 ||
					node_material_flags1 != node_material_flags2)
				{
					break;
				}
			}

			vert_ptr = copyVerts(vert_ptr, particle2->particles, &particle_count, sorted_links);
		}
		i = j-1;

		node_material_flags = SAFE_MEMBER(nodes[i]->material, flags) | nodes[i]->add_material_flags;
		next_stencil_mode = rdrMaterialFlagsUnpackStencilMode(node_material_flags);
		if (next_stencil_mode != stencil_mode)
		{
			stencil_mode = next_stencil_mode;
			if (stencil_mode)
				rxbxStencilMode(device, stencil_mode, nodes[i]->stencil_value);
			else
				rxbxStencilMode(device, RDRSTENCILMODE_NONE, 0);
		}
		rxbxColorWrite(device, node_material_flags & RMATERIAL_NOCOLORWRITE ? FALSE : TRUE);
		if (nodes[i]->instances)
			rxbxInstanceParam(device, nodes[i]->instances->per_drawable_data.instance_param);
		else
			rxbxInstanceParam(device, unitvec4);

		if (sort_bucket_type != RSBT_WIREFRAME)
			if (!rxbxBindPixelShader(device, nodes[i]->draw_shader_handle[pass_data->shader_mode], NULL))
				bOkayToDraw = false;
		if (bOkayToDraw)
			drawParticlesInline(device, nodes[i]->material, particle, particle_count, verts, sort_bucket_type, is_low_res_edge_pass, nodes[i]);
	}

	// reset these
	rxbxBlendFuncPop(device);
	rxbxSetCullMode(device, CULLMODE_BACK);
	rxbxDepthWritePop(device);
	rxbxColorWritePop(device);
	rxbxFogPop(device);

	if (sort_bucket_type == RSBT_WIREFRAME)
	{
		rxbxSetFillMode(device, D3DFILL_SOLID);
	}

	if (stencil_mode)
		rxbxStencilMode(device, RDRSTENCILMODE_NONE, 0);

	if (sorted_links)
		ScratchFree(sorted_links);
	ScratchFree(pBuffer);

	PERFINFO_AUTO_STOP();
#endif
}

void rxbxDrawFastParticlesDirect(RdrDeviceDX *device, RdrDrawableFastParticles *particle_set, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, bool is_low_res_edge_pass, bool manual_depth_test, bool is_underexposed_pass)
{
	bool cpuFastParticles = (rdr_state.forceCPUFastParticles
							 || particle_set->cpu_fast_particles
							 || !rxbxSupportsFeature((RdrDevice*)device, FEATURE_VFETCH));
		
	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (rdr_state.dbg_type_flags & RDRTYPE_PARTICLE)
		return;

	PERFINFO_AUTO_START_FUNC();

	// FIXME: The MSAA depth resolve and the fast particle state do not get along well, so
	// let's just bind the depth texture here to prevent conflicts later.
  #define SOFT_PARTICLE_DEPTH_TEXTURE_INDEX 1
	if(sort_bucket_type != RSBT_WIREFRAME && particle_set->soft_particles && rxbxGetCurrentState(device)->soft_particle_depth_tex_handle) {
		rxbxBindTexture(device, SOFT_PARTICLE_DEPTH_TEXTURE_INDEX, rxbxGetCurrentState(device)->soft_particle_depth_tex_handle);
	}

    if (particle_set->is_screen_space)
    {
        rxbxSet2DMode(device, 1, 1);
		// JE: Disable depth testing for 2D particle systems for performance and because we don't clear the depth buffer for 2D drawing
		rxbxDepthTest(device, DEPTHTEST_OFF);
	}
    else
    {
        rxbxSet3DMode(device, 0);
    }

	if (cpuFastParticles)
	{
		rxbxSetupPrimitiveDrawState(device, true);
		rxbxSetupFastParticleCPUDrawMode(device, device->primitive_vertex_textured_declaration, sort_node->uses_far_depth_range);
	}
	else
	{
		rxbxSetFastParticleSetInfo(device, particle_set);
		//rxbxPushModelMatrix(device, particle_set->world_matrix, false, false);
		if (particle_set->streak)
			rxbxSetupFastParticleDrawMode(device, velements_fast_particle_streak, &device->fast_particle_streak_vertex_declaration,
				particle_set->link_scale, particle_set->streak, sort_node->uses_far_depth_range, particle_set->rgb_blend, particle_set->animated_texture);
		else
			rxbxSetupFastParticleDrawMode(device, velements_fast_particle, &device->fast_particle_vertex_declaration,
				particle_set->link_scale, particle_set->streak, sort_node->uses_far_depth_range, particle_set->rgb_blend, particle_set->animated_texture);
	}

	if (sort_bucket_type == RSBT_WIREFRAME)
	{
		rxbxSetWireframePixelShader(device);
		rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
		rxbxBindWhiteTexture(device, 0);
		rxbxBindVertexTexture(device, 0, particle_set->noise_tex);
		rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);

		rxbxFogPush(device, 0);
		rxbxSetCullMode(device, CULLMODE_NONE);
		rxbxSetFillMode(device, D3DFILL_WIREFRAME);
		rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		rxbxDepthWritePush(device, TRUE);

		rxbxColorf(device, unitvec4);
		rxbxInstanceParam(device, unitvec4);
	}
	else
	{
		rxbxDepthWritePush(device, FALSE);

		// Game specific special parameter. Goes in the second material constant slot
		// for the fast particle pixel shader.
		rxbxPixelShaderConstantParameters(device, 2, (const Vec4*)particle_set->special_params, 1, PS_CONSTANT_BUFFER_MATERIAL);

		rxbxSetCullMode(device, CULLMODE_NONE);
		if (is_underexposed_pass && particle_set->no_tonemap)
		{
			// Really want to draw with color == black here, but let's just tonemap instead
			rxbxSetFastParticleBlendMode(device, false, particle_set->soft_particles, cpuFastParticles, manual_depth_test);
		} else {
			rxbxSetFastParticleBlendMode(device, particle_set->no_tonemap, particle_set->soft_particles, cpuFastParticles, manual_depth_test);
		}

		rxbxFogPush(device, 1);
		if (sort_bucket_type == RSBT_ALPHA_LOW_RES_ALPHA)
		{
			// Technically could happen, so don't actually assert
			rxbxBlendFuncPushNop(device);
			rxbxBlendFuncSeparate(device, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD,
								  D3DBLEND_ZERO, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD );
		}
		else if ((sort_bucket_type == RSBT_ALPHA_LOW_RES_ADDITIVE || sort_bucket_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE) && !is_low_res_edge_pass)
		{
			rxbxBlendFuncPushNop(device);
			rxbxBlendFuncSeparate(device, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD,
								  D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD );
			rxbxFogColorPush(device, zerovec4, zerovec4);
		}
		else if (particle_set->blendmode & RMATERIAL_ADDITIVE)
		{
			rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD);
			rxbxFogColorPush(device, zerovec4, zerovec4);
		}
		else if (particle_set->blendmode & RMATERIAL_SUBTRACTIVE)
		{
			rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT);
			rxbxFogColorPush(device, zerovec4, zerovec4);
		}
		else
		{
			rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		}

		rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
		rxbxMarkTexturesUnused(device, TEXTURE_VERTEXSHADER);
		rxbxBindTexture(device, 0, particle_set->tex_handle);

		{
			#define MANUAL_DEPTH_TEST_TEXTURE_INDEX 15
			#define DEPTH_SURFACE_SCALE_INDEX 1
			bool needsScreenScale = false;
			if (particle_set->soft_particles && rxbxGetCurrentState(device)->soft_particle_depth_tex_handle)
			{
				rxbxBindTexture(device, SOFT_PARTICLE_DEPTH_TEXTURE_INDEX, rxbxGetCurrentState(device)->soft_particle_depth_tex_handle);
				needsScreenScale = true;
			}
			if (manual_depth_test)
			{
				assert( device && device->active_surface && device->active_surface->rendertarget[SBUF_DEPTH].textures );
				rxbxBindTexture(device, MANUAL_DEPTH_TEST_TEXTURE_INDEX, RdrTexHandleToTexHandle(device->active_surface->rendertarget[SBUF_DEPTH].textures[0].tex_handle));
				needsScreenScale = true;
			}

			if (needsScreenScale)
			{
				Vec4 screen_scale = { 1.0f / device->active_surface->width_thread, 
									  1.0f / device->active_surface->height_thread, 0.0f, 0.0f };
				screen_scale[ 2 ] = 0.5f * screen_scale[ 0 ];
				screen_scale[ 3 ] = 0.5f * screen_scale[ 1 ];
				rxbxPixelShaderConstantParameters(device, DEPTH_SURFACE_SCALE_INDEX, (const Vec4*)screen_scale, 1, PS_CONSTANT_BUFFER_MATERIAL);
			}
		}

		if (!cpuFastParticles)
			rxbxBindVertexTexture(device, 0, particle_set->noise_tex);
		rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);
		rxbxUnbindUnusedTextures(device, TEXTURE_VERTEXSHADER);
	}

	if (!cpuFastParticles)
	{
		// draw
		if (particle_set->streak)
			rxbxDrawQuadsUP(device, particle_set->particle_count, particle_set->streakverts, sizeof(RdrFastParticleStreakVertex), true);
		else
			rxbxDrawQuadsUP(device, particle_set->particle_count, particle_set->verts, sizeof(RdrFastParticleVertex), true);
	}
	else
	{
		RdrPrimitiveTexturedVertex* verts = ScratchAlloc(sizeof(*verts) * particle_set->particle_count * 4);
		rxbxFastParticlesVertexShader(verts, particle_set);
		rxbxDrawQuadsUP(device, particle_set->particle_count, verts, sizeof(*verts), true);
		ScratchFree(verts);
	}


	// reset these
	rxbxUnbindVertexTextures(device);
	rxbxBlendFuncPop(device);
	rxbxSetCullMode(device, CULLMODE_BACK);
	rxbxDepthWritePop(device);
	if (sort_bucket_type != RSBT_WIREFRAME && particle_set->blendmode & (RMATERIAL_ADDITIVE|RMATERIAL_SUBTRACTIVE))
		rxbxFogColorPop(device);
	rxbxFogPop(device);
	//rxbxPopModelMatrix(device);

	if (sort_bucket_type == RSBT_WIREFRAME)
	{
		rxbxSetFillMode(device, D3DFILL_SOLID);
	}

	PERFINFO_AUTO_STOP();
}

void rxbxDrawTriStripDirect(RdrDeviceDX *device, RdrDrawableTriStrip *tristrip, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data)
{
	bool bOkayToDraw=true;
	RdrMaterialFlags material_flags = SAFE_MEMBER(sort_node->material, flags) | sort_node->add_material_flags;
	PERFINFO_AUTO_START_FUNC();

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (rdr_state.dbg_type_flags & RDRTYPE_PARTICLE)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if(tristrip->is_screen_space) {
		rxbxSet2DMode(device, 1, 1);
	} else {
		rxbxSet3DMode(device, 0);
	}
	rxbxSetupParticleDrawMode(device, velements_particle, &device->particle_vertex_declaration, true, sort_node->uses_far_depth_range);

	if (sort_bucket_type == RSBT_WIREFRAME)
	{
		rxbxSetWireframePixelShader(device);
		rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
		rxbxBindWhiteTexture(device, 0);
		rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);

		rxbxFogPush(device, 0);
		rxbxSetCullMode(device, CULLMODE_NONE);
		rxbxSetFillMode(device, D3DFILL_WIREFRAME);
		rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		rxbxDepthWritePush(device, !tristrip->is_screen_space);

		rxbxColorWritePush(device, TRUE);
		rxbxColorf(device, unitvec4);

		if (sort_node->instances) {
			rxbxInstanceParam(device, sort_node->instances->per_drawable_data.instance_param);
		} else {
			rxbxInstanceParam(device, unitvec4);
		}
	}
	else
	{
		rxbxSetParticleBlendMode(device);

		// setup state
		rxbxDepthWritePush(device, FALSE);
		rxbxSetCullMode(device, CULLMODE_NONE);

		if(material_flags & RMATERIAL_NOFOG) {
			rxbxFogPush(device, 0);
		} else {
			rxbxFogPush(device, 1);
		}

		if (tristrip->add_material_flags & RMATERIAL_ADDITIVE)
		{
			rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD);
			rxbxFogColorPush(device, zerovec4, zerovec4);
		}
		else if (tristrip->add_material_flags & RMATERIAL_SUBTRACTIVE)
		{
			rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT);
			rxbxFogColorPush(device, zerovec4, zerovec4);
		}
		else
		{
			rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		}

		rxbxColorWritePush(device, TRUE);

		if (sort_node->instances) {
			rxbxInstanceParam(device, sort_node->instances->per_drawable_data.instance_param);
		} else {
			rxbxInstanceParam(device, unitvec4);
		}

		rxbxSetupAmbientLight(device, NULL, NULL, NULL, NULL, RLCT_WORLD);

		if (sort_bucket_type != RSBT_WIREFRAME)
			if (!rxbxBindPixelShader(device, sort_node->draw_shader_handle[pass_data->shader_mode], NULL))
				bOkayToDraw = false;

		// Textures
		{
			U32 i;

			rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
			for (i=0; i<sort_node->material->tex_count; i++)
			{
				if (i == 0 && tristrip->tex_handle0)
					rxbxBindTexture(device, i, tristrip->tex_handle0);
				else if (i == 1 && tristrip->tex_handle1)
					rxbxBindTexture(device, i, tristrip->tex_handle1);
				else
					rxbxBindTexture(device, i, sort_node->material->textures[i]);
			}
			rxbxSetShadowBufferTextureActive(device, sort_node->uses_shadowbuffer);
			rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);
		}
		
		if (sort_node->material->const_count)
			rxbxPixelShaderConstantParameters(device, 0, sort_node->material->constants, sort_node->material->const_count, PS_CONSTANT_BUFFER_MATERIAL);

	}

	// draw
	if (bOkayToDraw)
		rxbxDrawTriangleStripUP(device, tristrip->vert_count, tristrip->verts, sizeof(RdrParticleVertex));

	// reset these
	if (sort_bucket_type != RSBT_WIREFRAME && (tristrip->add_material_flags & (RMATERIAL_ADDITIVE | RMATERIAL_SUBTRACTIVE)))
		rxbxFogColorPop(device);
	rxbxFogPop(device);
	rxbxBlendFuncPop(device);
	rxbxSetCullMode(device, CULLMODE_BACK);
	rxbxDepthWritePop(device);
	rxbxColorWritePop(device);

	if (sort_bucket_type == RSBT_WIREFRAME)
	{
		rxbxSetFillMode(device, D3DFILL_SOLID);
	}

	PERFINFO_AUTO_STOP();
}

void rxbxDrawCylinderTrailDirect(RdrDeviceDX *device, RdrDrawableCylinderTrail *cyltrail, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data)
{
	bool bOkayToDraw=true;
	PERFINFO_AUTO_START_FUNC();

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (rdr_state.dbg_type_flags & RDRTYPE_PARTICLE)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	rxbxSet3DMode(device, 0);

	rxbxSetCylinderTrailParameters(device, cyltrail->vertex_shader_constants, cyltrail->num_constants);
	rxbxSetupCylinderTrailDrawMode(device, velements_cylindertrail, &device->cylinder_trail_vertex_declaration, sort_node->uses_far_depth_range);

	if (sort_bucket_type == RSBT_WIREFRAME)
	{
		rxbxSetWireframePixelShader(device);
		rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
		rxbxBindWhiteTexture(device, 0);
		rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);

		rxbxFogPush(device, 0);
		rxbxSetCullMode(device, CULLMODE_NONE);
		rxbxSetFillMode(device, D3DFILL_WIREFRAME);
		rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		rxbxDepthWritePush(device, TRUE);

		rxbxColorf(device, unitvec4);
		rxbxInstanceParam(device, unitvec4);
	}
	else
	{
		//rxbxSetCylinderTrailBlendMode(device);


		// setup state
		rxbxDepthWritePush(device, FALSE);
		//rxbxSetCullMode(device, CULLMODE_NONE);
		rxbxFogPush(device, 1);


		if (cyltrail->add_material_flags & RMATERIAL_ADDITIVE)
		{
			rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD);
			rxbxFogColorPush(device, zerovec4, zerovec4);
		}
		else if (cyltrail->add_material_flags & RMATERIAL_SUBTRACTIVE)
		{
			rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT);
			rxbxFogColorPush(device, zerovec4, zerovec4);
		}
		else
		{
			rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		}

		rxbxInstanceParam(device, unitvec4);

		rxbxSetupAmbientLight(device, NULL, NULL, NULL, NULL, RLCT_WORLD);

		if (sort_bucket_type != RSBT_WIREFRAME)
			if (!rxbxBindPixelShader(device, sort_node->draw_shader_handle[pass_data->shader_mode], NULL))
				bOkayToDraw = false;

		// Textures
		{
			U32 i;

			rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
			for (i=0; i<sort_node->material->tex_count; i++)
			{
				if (i == 0 && cyltrail->tex_handle0)
					rxbxBindTexture(device, i, cyltrail->tex_handle0);
				else if (i == 1 && cyltrail->tex_handle1)
					rxbxBindTexture(device, i, cyltrail->tex_handle1);
				else
					rxbxBindTexture(device, i, sort_node->material->textures[i]);
			}
			rxbxSetShadowBufferTextureActive(device, sort_node->uses_shadowbuffer);
			rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);
		}

		if (sort_node->material->const_count)
			rxbxPixelShaderConstantParameters(device, 0, sort_node->material->constants, sort_node->material->const_count, PS_CONSTANT_BUFFER_MATERIAL);

	}

	// draw
	if (bOkayToDraw)
		rxbxDrawIndexedTrianglesUP(device, cyltrail->index_count/3, cyltrail->idxs, cyltrail->vert_count, cyltrail->verts, sizeof(*cyltrail->verts));

	// reset these
	if (sort_bucket_type != RSBT_WIREFRAME && (cyltrail->add_material_flags & (RMATERIAL_ADDITIVE | RMATERIAL_SUBTRACTIVE)))
		rxbxFogColorPop(device);
	rxbxFogPop(device);
	rxbxBlendFuncPop(device);
	rxbxDepthWritePop(device);
	if (sort_bucket_type == RSBT_WIREFRAME)
	{
		rxbxSetCullMode(device, CULLMODE_BACK);
		rxbxSetFillMode(device, D3DFILL_SOLID);
	}

	PERFINFO_AUTO_STOP();
}


