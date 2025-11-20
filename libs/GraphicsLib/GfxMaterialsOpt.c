// Per-frame materials system calls go in here (and have optimization on)
#include "GfxMaterialsOpt.h"
#include "GfxMaterials.h"
#include "GfxTexturesInline.h"
#include "GfxShader.h"
#include "GfxDynamics.h"
#include "GfxLightOptions.h"
#include "GfxCommandParse.h"

#include "Materials.h"

#include "WorldGrid.h"

#include "MemoryPool.h"
#include "LinearAllocator.h"
#include "../WorldLib/AutoGen/WorldCellEntry_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););

MP_DEFINE(ShaderGraphHandleData);

static ShaderGraphHandleData **queuedShaderGraphLoads;
static bool bAddedSomethingToQueuedLoads=false;
static bool bBadTimeToQueueLoads=false;
static int tempMaterialRenderInfoCount;

static BasicTexture *cubemap_override;

void setConstantMapping(MaterialConstantMapping *constant_mapping, const char *constant_name, const Vec4 new_value);

AUTO_RUN;
void addGfxMaterialsOptWatches(void)
{
	dbgAddClearedIntWatch("TempMaterialRenderInfoCount", tempMaterialRenderInfoCount);
}

void destroyShaderGraphHandleData(ShaderGraphHandleData *handle_data)
{
	if (handle_data->sent_to_renderer || handle_data->sent_to_dcache) {
		handle_data->freeme = 1;
	} else {
		FOR_EACH_IN_EARRAY(queuedShaderGraphCompileResults, PrecompiledShaderData, psd)
			if (psd->handle_data == handle_data)
				eaRemoveFast(&queuedShaderGraphCompileResults, ipsdIndex);
		FOR_EACH_END;
		SAFE_FREE(handle_data->performance_values);
		eaFindAndRemoveFast(&queuedShaderGraphLoads, handle_data);
		MP_FREE(ShaderGraphHandleData, handle_data);
	}
}

__forceinline static void gfxShaderGraphIncrementUsedCount(ShaderGraphRenderInfo *graph_render_info, int inc, U32 tri_count)
{
	if (graph_render_info->graph_last_used_swap_frame != gfx_state.frame_count) {
		graph_render_info->graph_last_used_count_swapped = (graph_render_info->graph_last_used_swap_frame == gfx_state.frame_count - 1)?
			graph_render_info->graph_last_used_count:
			0;
		graph_render_info->graph_last_used_tricount_swapped = (graph_render_info->graph_last_used_swap_frame == gfx_state.frame_count - 1)?
			graph_render_info->graph_last_used_tricount:
			0;
		graph_render_info->graph_last_used_swap_frame = gfx_state.frame_count;
		graph_render_info->graph_last_used_count = inc;
		graph_render_info->graph_last_used_tricount = tri_count;
		gfx_state.debug.frame_counts.unique_shader_graphs_referenced++;
	} else {
		if (!graph_render_info->graph_last_used_count)
			gfx_state.debug.frame_counts.unique_shader_graphs_referenced++;
		graph_render_info->graph_last_used_count += inc;
		graph_render_info->graph_last_used_tricount += tri_count;
	}
}

static void gfxDemandLoadShaderGraphAtDrawTime(ShaderGraphHandleData *handle_data)
{
	handle_data->graph_render_info->graph_last_used_time_stamp = gfx_state.client_frame_timestamp;

	if (handle_data->load_state & gfx_state.currentRendererFlag || handle_data->loading)
		return;

	// Load/generate the shader
	gfxDemandLoadShaderGraphInternal(handle_data);
}

static void gfxDemandLoadShaderGraphs(bool earlyFrameManualCall) // Called once per frame per device
{
	int i;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	// This list should be fairly small most of the time, but possibly very big on the first frame.
	for (i=eaSize(&queuedShaderGraphLoads)-1; i>=0; i--) {
		int needed=false;
		if (queuedShaderGraphLoads[i]->load_needed) {
			gfxDemandLoadShaderGraphAtDrawTime(queuedShaderGraphLoads[i]);
			if (queuedShaderGraphLoads[i]->load_state != gfx_state.allRenderersFlag)
				needed = true;
			else
				queuedShaderGraphLoads[i]->load_needed = 0;
		}
		if (!needed) {
			// Remove it
			eaRemoveFast(&queuedShaderGraphLoads, i);
		}
	}
	// Shrink EArray to conserve memory (will thrash on first frame)
	if (eaCapacity(&queuedShaderGraphLoads) > 32 && eaSize(&queuedShaderGraphLoads) * 5 < eaCapacity(&queuedShaderGraphLoads)) {
		int newsize = MAX(16, eaSize(&queuedShaderGraphLoads)*2);
		// Using only 20% capacity or less and we've got more than 32 elements
		eaSetCapacity(&queuedShaderGraphLoads, newsize);
	}
	bAddedSomethingToQueuedLoads = false;
	if (!earlyFrameManualCall) // If calling mid-frame for some hacky purpose, don't assert
		bBadTimeToQueueLoads = true; // Not valid for multiple devices

	// After above loop, since it may add things to this (possibly only when background loading is disabled)
	FOR_EACH_IN_EARRAY(queuedShaderGraphCompileResults, PrecompiledShaderData, psd)
	{
		if (psd->rendererIndex == gfx_state.currentRendererIndex) {
			gfxDemandLoadPrecompiled(psd);
			eaRemoveFast(&queuedShaderGraphCompileResults, ipsdIndex);
		}
	}
	FOR_EACH_END;

	PERFINFO_AUTO_STOP();
}

static void setupScroll(F32* dest, MaterialConstantMapping *constant_mapping, U32 value_count)
{
	U32 i;

	if (  constant_mapping->last_updated_timestamp == gfx_state.client_frame_timestamp
		  && constant_mapping->last_updated_action_idx == gfx_state.currentActionIdx)
		return;
	constant_mapping->last_updated_timestamp = gfx_state.client_frame_timestamp;
	constant_mapping->last_updated_action_idx = gfx_state.currentActionIdx;

	for (i = 0; i < value_count; i++)
	{
		if (constant_mapping->scroll.values[i]!=0) {
			if (constant_mapping->scroll.isIncremental && !gfx_state.debug.disableIncrementalTex) {
				dest[i] += constant_mapping->scroll.values[i] * gfx_state.frame_time;
			} else {
				dest[i] = constant_mapping->scroll.values[i] * gfx_state.client_loop_timer;
			}
			dest[i] -= round(dest[i]); // Range of -1-1
		} else
			dest[i] = 0;

		 constant_mapping->scroll.lastValues[i] = dest[i];

	}
}

__forceinline static void setupTimeGradient(F32 *dest, MaterialConstantMapping *constant_mapping)
{
	F32 time;
	F32 v;

	constant_mapping->last_updated_timestamp = gfx_state.client_frame_timestamp;
	constant_mapping->last_updated_action_idx = gfx_state.currentActionIdx;
	time = gfxGetTime();

	// Possible performance improvement: add an invStartFadeLength to multiply by instead of doing division
	if (time > constant_mapping->timeGradient.start && time < constant_mapping->timeGradient.startFade)
	{
		// Fading up
		v = (time - constant_mapping->timeGradient.start) / (constant_mapping->timeGradient.startFade - constant_mapping->timeGradient.start);
	} else if (time + 24.f < constant_mapping->timeGradient.startFade) // Start wraps
	{
		// Fading up
		v = (time + 24.f - constant_mapping->timeGradient.start) / (constant_mapping->timeGradient.startFade - constant_mapping->timeGradient.start);
	} else if (time > constant_mapping->timeGradient.end && time < constant_mapping->timeGradient.endFade)
	{
		// Fading down
		v = 1.0 - (time - constant_mapping->timeGradient.end) / (constant_mapping->timeGradient.endFade - constant_mapping->timeGradient.end);
	} else if (time + 24.f < constant_mapping->timeGradient.endFade) // End wraps
	{
		// Fading down
		v = 1.0 - (time + 24.f - constant_mapping->timeGradient.end) / (constant_mapping->timeGradient.endFade - constant_mapping->timeGradient.end);
	} else if (constant_mapping->timeGradient.start < constant_mapping->timeGradient.end) {
		if (time < constant_mapping->timeGradient.start || time > constant_mapping->timeGradient.end) {
			// Off
			v = 0;
		} else {
			// On
			v = 1;
		}
	} else {
		if (time >= constant_mapping->timeGradient.end && time <= constant_mapping->timeGradient.start) {
			// Off
			v = 0;
		} else {
			// On
			v = 1;
		}
	}
	v = v * (constant_mapping->timeGradient.maximum - constant_mapping->timeGradient.minimum) + constant_mapping->timeGradient.minimum;
	*dest = v;
}

__forceinline static void setupUVRotation(F32* dest, MaterialConstantMapping *constant_mapping)
{
	F32 c, s;
	if (  constant_mapping->last_updated_timestamp == gfx_state.client_frame_timestamp
		  && constant_mapping->last_updated_action_idx == gfx_state.currentActionIdx)
		return;
	constant_mapping->last_updated_timestamp = gfx_state.client_frame_timestamp;
	constant_mapping->last_updated_action_idx = gfx_state.currentActionIdx;

	//constant_mapping->uvrotation.rotation += constant_mapping->uvrotation.rotationRate * gfx_state.frame_time;
	if (constant_mapping->uvrotation.rotationRate)
		constant_mapping->uvrotation.rotation = constant_mapping->uvrotation.rotationRate * gfx_state.client_loop_timer;

	sincosf(constant_mapping->uvrotation.rotation, &s, &c);
	dest[0] = c*constant_mapping->uvrotation.scale[0];
	dest[1] = -s*constant_mapping->uvrotation.scale[0];
	dest[2] = s*constant_mapping->uvrotation.scale[1];
	dest[3] = c*constant_mapping->uvrotation.scale[1];
}

__forceinline static void setupOscillator(F32* dest, MaterialConstantMapping *constant_mapping)
{
	if (  constant_mapping->last_updated_timestamp == gfx_state.client_frame_timestamp
		  && constant_mapping->last_updated_action_idx == gfx_state.currentActionIdx)
		return;
	constant_mapping->last_updated_timestamp = gfx_state.client_frame_timestamp;
	constant_mapping->last_updated_action_idx = gfx_state.currentActionIdx;
	*dest = constant_mapping->oscillator.amplitude * qsin(gfx_state.client_loop_timer*constant_mapping->oscillator.frequency + constant_mapping->oscillator.phase);
}

__forceinline static void setupTextureScreenColor(TexHandle *pTexHandle, MaterialConstantMapping *constant_mapping)
{
	if (gfx_state.currentAction && gfx_state.currentAction->snapshotLDR.surface) {
		*pTexHandle = rdrSurfaceToTexHandleEx(gfx_state.currentAction->snapshotLDR.surface, gfx_state.currentAction->snapshotLDR.buffer, gfx_state.currentAction->snapshotLDR.snapshot_idx, 0, false);
	} else  // Should only happen in the first half of the frame in which a device is created, and get overridden with correct values before actual drawing
		*pTexHandle = texDemandLoadFixed(white_tex);
}

__forceinline static void setupTextureCube(TexHandle *pTexHandle, MaterialConstantMapping *constant_mapping)
{
	if (cubemap_override) {
		*pTexHandle = texDemandLoadFixed(cubemap_override);
	} else {
		// Leave the one on the material
	}
}

__forceinline static void setupTextureScreenColorHDR(TexHandle *pTexHandle, MaterialConstantMapping *constant_mapping)
{
	if (gfx_state.currentAction && gfx_state.currentAction->snapshotHDR.surface) {
		*pTexHandle = rdrSurfaceToTexHandleEx(gfx_state.currentAction->snapshotHDR.surface, gfx_state.currentAction->snapshotHDR.buffer, gfx_state.currentAction->snapshotHDR.snapshot_idx, 0, false);
	} else {
		*pTexHandle = texDemandLoadFixed(black_tex);
	}
}


__forceinline static void setupTextureScreenDepth(TexHandle *pTexHandle, MaterialConstantMapping *constant_mapping)
{
	if (gfx_state.currentAction && gfx_state.currentAction->opaqueDepth.surface && (gfx_state.currentAction->opaqueDepth.surface->params_nonthread.flags & SF_DEPTH_TEXTURE)) {
		*pTexHandle = rdrSurfaceToTexHandleEx(gfx_state.currentAction->opaqueDepth.surface, gfx_state.currentAction->opaqueDepth.buffer, gfx_state.currentAction->opaqueDepth.snapshot_idx, 0, false);
	} else {
		*pTexHandle = texDemandLoadFixed(white_tex);
	}
}

__forceinline static void setupTextureScreenOutline(TexHandle *pTexHandle, MaterialConstantMapping *constant_mapping)
{
	RdrSurface * outline_surface;
	if (gfx_state.target_highlight && gfxDoingPostprocessing())
		outline_surface = SAFE_MEMBER2(gfx_state.currentAction, bufferHighlight, surface);
	else
		outline_surface = SAFE_MEMBER2(gfx_state.currentAction, bufferOutline, surface);
	if (outline_surface) {
		*pTexHandle = DRAWLIST_DEFINED_TEX__OUTLINE;
	} else {
		*pTexHandle = texDemandLoadFixed(white_tex);
	}
}

__forceinline static void setupTextureScreenColorBlurred(TexHandle *pTexHandle, MaterialConstantMapping *constant_mapping)
{
	RdrSurface *surface;
	if (gfx_state.currentAction && gfxIsStageEnabled(GFX_DEPTH_OF_FIELD) && (surface=gfxGetStageTemp(GFX_DEPTH_OF_FIELD, 0)))
	{
		*pTexHandle = rdrSurfaceToTexHandleEx(surface, SBUF_0, 0, 0, false);
	}
	else if (gfx_state.currentAction && gfx_state.currentAction->snapshotLDR.surface)
	{
		*pTexHandle = rdrSurfaceToTexHandleEx(gfx_state.currentAction->snapshotLDR.surface, gfx_state.currentAction->snapshotLDR.buffer, gfx_state.currentAction->snapshotLDR.snapshot_idx, 0, false);
	} else  // Should only happen in the first half of the frame in which a device is created, and get overridden with correct values before actual drawing
		*pTexHandle = texDemandLoadFixed(white_tex);
}


__forceinline static void setupTextureDiffuseWarp(MaterialRenderInfo *render_info, TexHandle *pTexHandle, MaterialConstantMapping *constant_mapping)
{
	assert(gfx_lighting_options.enableDiffuseWarpTex); // render info should not have a diffuse warp texture if the option was not enabled

	if (*pTexHandle != tex_from_sky_file->tex_handle)
		return;

	if (gfx_state.currentAction)
	{
		if (render_info->usage_flags & WL_FOR_ENTITY)
			*pTexHandle = texDemandLoadFixed(gfx_state.currentAction->gdraw.diffuse_warp_texture_character);
		else
			*pTexHandle = texDemandLoadFixed(gfx_state.currentAction->gdraw.diffuse_warp_texture_world);
	} else {
		*pTexHandle = texDemandLoadFixed(texFindAndFlag("default_diffuse_warp", 1, WL_FOR_UTIL));
	}
}

__forceinline static void setupTextureAmbientCube(MaterialRenderInfo *render_info, TexHandle *pTexHandle, MaterialConstantMapping *constant_mapping)
{
	BasicTexture *tex = default_ambient_cube;
	if (gfx_state.currentAction)
		tex = gfx_state.currentAction->gdraw.ambient_cube;

	*pTexHandle = texDemandLoadFixed(tex);

	// Add mip biasing
	if (!gfx_state.ambient_cube_res)
		gfx_state.ambient_cube_res = 8;

	{
		int res = tex->realWidth;
		U32 bias=0;
		while (res > gfx_state.ambient_cube_res)
		{
			bias++;
			res >>= 1;
		}
		MIN1(bias, 7);
		rdrAddRemoveTexHandleFlags(pTexHandle, RTF_MAXMIPLEVEL_1*bias, (RTF_MAXMIPLEVEL_1|RTF_MAXMIPLEVEL_2|RTF_MAXMIPLEVEL_4));
	}
}

static void setupTextureReflection(MaterialRenderInfo *render_info, TexHandle *pTexHandle, MaterialConstantMapping *constant_mapping)
{
	BasicTexture *tex=NULL;
	bool isCubeMap = false;
	ShaderGraphReflectionType reflection_type = MIN(gfx_state.settings.maxReflection, render_info->graph_reflection_type);
	if (reflection_type == SGRAPH_REFLECT_CUBEMAP)
		isCubeMap = true;
	else if (reflection_type == SGRAPH_REFLECT_SIMPLE) {
		// nothing
	} else
	{
		// This happens only when the options set maxReflection down to 0, but we're using a material
		//  which wants reflections.  The shader will not have any sampler of any kind, so just use a dummy texture
		tex = white_tex;
	}

	// Get cubemap/spheremap from action
	if (isCubeMap)
	{
		if (!tex)
			tex = render_info->override_cubemap_texture;
		if (!tex)
			tex = SAFE_MEMBER(gfx_state.currentAction, env_cubemap_from_region);
		if (!tex)
			tex = SAFE_MEMBER(gfx_state.currentAction, gdraw.env_cubemap_from_sky);
		if (!tex)
			tex = default_env_cubetex;
	} else {
		if (!tex)
			tex = render_info->override_spheremap_texture;
		if (!tex)
			tex = SAFE_MEMBER(gfx_state.currentAction, env_spheremap_from_region);
		if (!tex)
			tex = SAFE_MEMBER(gfx_state.currentAction, gdraw.env_spheremap_from_sky);
		if (!tex)
			tex = default_env_spheretex;
	}
	devassert(tex); // Oterhwise the errortex below will be bad/wrong type

	*pTexHandle = texDemandLoadInline(tex, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY, white_tex);
	// Add mip biasing
	if (render_info->max_reflect_resolution)
	{
		U32 res = tex->realWidth;
		U32 bias=0;
		U32 maxres = (1 << render_info->max_reflect_resolution);
		while (res > maxres)
		{
			bias++;
			res >>= 1;
		}
		MIN1(bias, 7);
		rdrAddRemoveTexHandleFlags(pTexHandle, RTF_MAXMIPLEVEL_1*bias, (RTF_MAXMIPLEVEL_1|RTF_MAXMIPLEVEL_2|RTF_MAXMIPLEVEL_4));
	}
}

static void setupDynamicConstants(MaterialRenderInfo *render_info)
{
	int i;
	MaterialConstantMapping *constant_mapping = render_info->constant_mapping;
	for (i=0; i<(int)render_info->constant_mapping_count; i++, constant_mapping++) {
		switch(constant_mapping->data_type) {
		//xcase SDT_TEXTURECUBE:
		//	setupTextureCube(&render_info->rdr_material.textures[constant_mapping->constant_index], constant_mapping);
		xcase SDT_TEXTURENORMAL_ISDXT5NM:
			// Maybe this should happen elsewhere?  Only needs to happen once per texture change
			render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex] =
				(((render_info->textures[constant_mapping->dxt5nm_index]->bt_texopt_flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT) == COMPRESSION_DXT5NM);
		xcase SDT_SCROLL:
			setupScroll(&render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex], constant_mapping, 2);
		xcase SDT_SINGLE_SCROLL:
			setupScroll(&render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex], constant_mapping, 1);
		xcase SDT_TIMEGRADIENT:
			setupTimeGradient(&render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex], constant_mapping);
		xcase SDT_OSCILLATOR:
			setupOscillator(&render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex], constant_mapping);
		xcase SDT_TEXTURE_SCREENCOLOR:
			setupTextureScreenColor(&render_info->rdr_material.textures[constant_mapping->constant_index], constant_mapping);
		xcase SDT_TEXTURE_SCREENCOLORHDR:
			setupTextureScreenColorHDR(&render_info->rdr_material.textures[constant_mapping->constant_index], constant_mapping);
		xcase SDT_TEXTURE_SCREENDEPTH:
			setupTextureScreenDepth(&render_info->rdr_material.textures[constant_mapping->constant_index], constant_mapping);
		xcase SDT_TEXTURE_SCREENOUTLINE:
			render_info->rdr_material.surface_texhandle_fixup = 1;
			setupTextureScreenOutline(&render_info->rdr_material.textures[constant_mapping->constant_index], constant_mapping);
		xcase SDT_TEXTURE_DIFFUSEWARP:
			setupTextureDiffuseWarp(render_info, &render_info->rdr_material.textures[constant_mapping->constant_index], constant_mapping);
		xcase SDT_LIGHTBLEEDVALUE:
			copyVec2(constant_mapping->lightBleed, &render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex]);
		xcase SDT_FLOORVALUES:
			copyVec4(constant_mapping->floorValues, &render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex]);
		xcase SDT_TEXTURE_SCREENCOLOR_BLURRED:
			setupTextureScreenColorBlurred(&render_info->rdr_material.textures[constant_mapping->constant_index], constant_mapping);
		xcase SDT_TEXTURE_REFLECTION:
			setupTextureReflection(render_info, &render_info->rdr_material.textures[constant_mapping->constant_index], constant_mapping);
		xcase SDT_TEXTURE_AMBIENT_CUBE:
			setupTextureAmbientCube(render_info, &render_info->rdr_material.textures[constant_mapping->constant_index], constant_mapping);
		xcase SDT_CHARACTERBACKLIGHTCOLOR:
			if (gfx_state.currentAction)
				copyVec3(gfx_state.currentAction->gdraw.character_backlight, &render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex]);
			else
				setVec3( &render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex],
						 0, 0, 0 );
		xcase SDT_SKYCLEARCOLOR:
			if (gfx_state.currentAction)
				copyVec3(gfx_state.currentAction->cameraView->clear_color, &render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex]);
			else
				setVec3(&render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex],
						0, 0, 0 );
		xcase SDT_SPECULAREXPONENTRANGE:
			copyVec2(constant_mapping->specExpRange, &render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex]);

		xcase SDT_PROJECT_SPECIAL:
			render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex] = gfx_state.project_special_material_param;


		xcase SDT_INVMODELVIEWMATRIX:
		acase SDT_VOLUMEFOGMATRIX:
		acase SDT_UNIQUEOFFSET:
			// This code could clear the matrices to zero, but they will be overwritten by the drawable

		xcase SDT_SUNDIRECTIONVIEWSPACE:
			zeroVec3(&render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex]);

		xcase SDT_SUNCOLOR:
			zeroVec3(&render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex]);

		xcase SDT_MODELPOSITIONVIEWSPACE:
			zeroVec4(render_info->rdr_material.constants[constant_mapping->constant_index]);

		xcase SDT_ROTATIONMATRIX:
			setupUVRotation(&render_info->rdr_material.constants[constant_mapping->constant_index][constant_mapping->constant_subindex], constant_mapping);

		xdefault:
			assertmsg(0, "Found a constant mapping without associated code");
		}
	}
}

void gfxDemandLoadMaterialAtDrawTime(MaterialRenderInfo *render_info, F32 dist, F32 uv_density)
{
	unsigned int i;
	// Load shader
	// done already: gfxDemandLoadShaderGraphAtDrawTime(render_info->graph_render_info);
	// Load textures
	bool bNeedDebugChecks = gfx_state.debug.whiteTextures || gfx_state.debug.textureOverride[0] || gfx_state.debug.showTextureDistance ||
                            gfx_state.debug.showTextureDensity || gfx_state.debug.showMipLevels;

	//for (i=0; i<render_info->rdr_material.tex_count; i++) {
	for (i=0; i<render_info->rdr_material.tex_count; i++) {
		BasicTexture *texture = render_info->textures[i];
		if (bNeedDebugChecks)
		{
			if (gfx_state.debug.whiteTextures)
			{
				texture = (texture && texIsCubemap(texture)) ? dummy_cube_tex : white_tex;
				render_info->rdr_material.textures[i] = texDemandLoadFixed(texture);
			} else {
				if (texture)
				{
					if (texIsCubemap(texture))
					{
						texture = dummy_cube_tex;
					} else if (gfx_state.debug.showTextureDistance) {
						g_needTextureBudgetInfo = 1;
						texDemandLoadInline(texture, dist, uv_density, white_tex);
						if (texture->loaded_data->min_draw_dist < 10)
							texture = texLoadBasic("256", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (texture->loaded_data->min_draw_dist < 50)
							texture = texLoadBasic("128", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (texture->loaded_data->min_draw_dist < 100)
							texture = texLoadBasic("64", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (texture->loaded_data->min_draw_dist < 200)
							texture = texLoadBasic("32", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (texture->loaded_data->min_draw_dist < 400)
							texture = texLoadBasic("16", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else
							texture = texLoadBasic("1", TEX_LOAD_IN_BACKGROUND, texture->use_category);
					} else if (gfx_state.debug.showMipLevels) {
						int size = (texture->realWidth + texture->realHeight) >> 1;

						// make sure the original texture has its timestamps correct
						texDemandLoadInline(texture, dist, uv_density, white_tex);

						if (texture->bt_texopt_flags & TEXOPT_NOMIP)
							texture = texLoadBasic("miptest_16", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (texture->realWidth == 256 && texture->realHeight == 512)
							texture = texLoadBasic("miptest_256_512", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (texture->realWidth == 512 && texture->realHeight == 256)
							texture = texLoadBasic("miptest_512_256", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (texture->realWidth == 512 && texture->realHeight == 1024)
							texture = texLoadBasic("miptest_512_1024", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (texture->realWidth == 1024 && texture->realHeight == 512)
							texture = texLoadBasic("miptest_1024_512", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (texture->realWidth == 1024 && texture->realHeight == 2048)
							texture = texLoadBasic("miptest_1024_2048", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (texture->realWidth == 2048 && texture->realHeight == 1024)
							texture = texLoadBasic("miptest_2048_1024", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (size <= 16)
							texture = texLoadBasic("miptest_16", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (size <= 32)
							texture = texLoadBasic("miptest_32", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (size <= 64)
							texture = texLoadBasic("miptest_64", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (size <= 128)
							texture = texLoadBasic("miptest_128", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (size <= 256)
							texture = texLoadBasic("miptest_256", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (size <= 512)
							texture = texLoadBasic("miptest_512", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else if (size <= 1024)
							texture = texLoadBasic("miptest_1024", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						else
							texture = texLoadBasic("miptest_2048", TEX_LOAD_IN_BACKGROUND, texture->use_category);
					} else if (gfx_state.debug.showTextureDensity) {
						if (gfx_state.debug.showTextureDensityExclude[0] && gfx_state.debug.showTextureDensityExclude[1] &&
							simpleMatchExact(gfx_state.debug.showTextureDensityExclude, texture->name))
						{
							// Skip it!
						} else {
							int size = (texture->width + texture->height) >> 1;
							if (texture->width == 256 && texture->height == 512)
								texture = texLoadBasic("checker_256_512", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (texture->width == 512 && texture->height == 256)
								texture = texLoadBasic("checker_512_256", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (texture->width == 512 && texture->height == 1024)
								texture = texLoadBasic("checker_512_1024", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (texture->width == 1024 && texture->height == 512)
								texture = texLoadBasic("checker_1024_512", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (texture->width == 1024 && texture->height == 2048)
								texture = texLoadBasic("checker_1024_2048", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (texture->width == 2048 && texture->height == 1024)
								texture = texLoadBasic("checker_2048_1024", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (size <= 16)
								texture = texLoadBasic("checker_16", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (size <= 32)
								texture = texLoadBasic("checker_32", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (size <= 64)
								texture = texLoadBasic("checker_64", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (size <= 128)
								texture = texLoadBasic("checker_128", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (size <= 256)
								texture = texLoadBasic("checker_256", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (size <= 512)
								texture = texLoadBasic("checker_512", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else if (size <= 1024)
								texture = texLoadBasic("checker_1024", TEX_LOAD_IN_BACKGROUND, texture->use_category);
							else
								texture = texLoadBasic("checker_2048", TEX_LOAD_IN_BACKGROUND, texture->use_category);
						}
					} else if (!texIsNormalmap(texture)) {
						texture = texLoadBasic(gfx_state.debug.textureOverride, TEX_LOAD_IN_BACKGROUND, texture->use_category);
					}
				}
				render_info->rdr_material.textures[i] = texDemandLoadFixed(texture);
			}
		}
		else
		{
			TexHandle tex_handle = render_info->rdr_material.textures[i];
			if (cubemap_override && texture && (texture->bt_texopt_flags & TEXOPT_CUBEMAP))
				texture = cubemap_override;
			if (texture && g_needTextureBudgetInfo && texture->loaded_data)
				texture->loaded_data->tex_last_used_count += render_info->material_last_used_count;
			if (tex_handle >= RDR_FIRST_TEXTURE_GEN || tex_handle < RDR_FIRST_SURFACE_FIXUP_TEXTURE)
			{
				// normal texture
				render_info->rdr_material.textures[i] = texDemandLoadInline(texture, dist, uv_density, white_tex);
			}
		}

		DEBUG_CLEAR_TEXTURE_QUEUED_FOR_DRAW(texture);
	}
	// Update dynamic constants
	setupDynamicConstants(render_info);
	// Setup domain shader textures.
	if (render_info->rdr_material.flags & RMATERIAL_TESSELLATE) {
		RdrNonPixelMaterial *tess_material = render_info->rdr_domain_material;
		if (render_info->heightmap) {
			tess_material->textures[0] = texDemandLoadInline(render_info->heightmap, dist, uv_density, white_tex);
			tess_material->tex_count = 1;
			tess_material->tessellation_flags = TESSELLATE_HAS_HEIGHTMAP;
			tess_material->constants[0][0] = render_info->heightmap_scale;
			tess_material->const_count = 1;
		} else {
			tess_material->tessellation_flags = TESSELLATE_PN_TRIANGLES;
		}
	}
}

static MaterialRenderInfo **queuedMaterialLoads;
void gfxDemandLoadMaterials(bool earlyFrameManualCall)  // Once per frame per device per action, after queueing, before drawing
{
	int i;
	int this_frame_usage = eaSize(&queuedMaterialLoads);

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	gfxDemandLoadShaderGraphs(earlyFrameManualCall);

	if (!gfx_state.currentAction)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_START("gfxDemandLoadMaterialAtDrawTime", eaSize(&queuedMaterialLoads));
	// This list should contain all materials used this frame.
	for (i=eaSize(&queuedMaterialLoads)-1; i>=0; i--) {
		assertmsg((!queuedMaterialLoads[i]->constant_mapping) ||
			(queuedMaterialLoads[i]->constant_mapping[0].data_type < SDT_NON_DRAWABLE_END ||
			(queuedMaterialLoads[i]->constant_mapping[0].data_type >= SDT_DRAWABLE_START &&
			queuedMaterialLoads[i]->constant_mapping[0].data_type <= SDT_DRAWABLE_END)),"Invalid Constant Mapping Data Type");

		if (queuedMaterialLoads[i]->material_min_draw_dist[gfx_state.currentAction->action_type] != FLT_MAX) {
            gfxDemandLoadMaterialAtDrawTime(queuedMaterialLoads[i], queuedMaterialLoads[i]->material_min_draw_dist[gfx_state.currentAction->action_type],
                queuedMaterialLoads[i]->material_min_uv_density + queuedMaterialLoads[i]->uv_scale);
		}
	}
	PERFINFO_AUTO_STOP();

	// Shrink EArray to conserve memory
	if (eaCapacity(&queuedMaterialLoads) > 2048 && this_frame_usage * 4 < eaCapacity(&queuedMaterialLoads)) {
		int newsize = MAX(64, this_frame_usage*2);
		// Using only 25% capacity or less and we've got more than 128 elements
		eaSetCapacity(&queuedMaterialLoads, newsize);
	}

	PERFINFO_AUTO_STOP();
}

void gfxMaterialsAssertTexNotInDrawList(const BasicTexture *bind)
{
	int i;
	for (i=eaSize(&queuedMaterialLoads)-1; i>=0; i--) {
		MaterialRenderInfo *render_info = queuedMaterialLoads[i];
		unsigned int j;
		for (j=0; j<render_info->rdr_material.tex_count; j++) {
			// If this goes off, because of a call to texFree, then someone is
			// requesting something to be freed in the same frame as they requested
			// it to be drawn.  Either they need to not do that, or we implement
			// a 1-frame delay between free requests and the actual frees (on
			// top of the per-device queuing that already happens)
			assert(bind != render_info->textures[j]);
		}
	}
}



static LinearAllocator *tempMaterialRenderInfoAllocator;

// Significantly faster if *not* inlined on Xbox anyway
/*
BTH: Commented sections in this function deal with tessellation.  Currently, texture swapping is not supported.
Once, it is, these sections will need to be uncommented and further refined.
*/
static MaterialRenderInfo *allocMaterialRenderInfoForThisFrame(MaterialRenderInfo *render_info, CONST_EARRAY_OF(BasicTexture) override_textures, const Vec4 *override_constants, const MaterialConstantMapping *override_constant_mappings)
{
	MaterialRenderInfo *new_render_info;
	int tex_size, tex_handle_size, tex_count = render_info->rdr_material.tex_count;
	int constant_size, const_count = render_info->rdr_material.const_count;
	int drawconst_size, drawconst_count = render_info->rdr_material.drawable_const_count;
	int constant_mapping_size, constant_mapping_count = render_info->constant_mapping_count;
	U8 *mem;
	//int domain_shader_size, domain_tex_handle_size, domain_tex_count = (render_info->rdr_domain_material?render_info->rdr_domain_material->tex_count:0),
	//	domain_constant_size, domain_const_count = (render_info->rdr_domain_material?render_info->rdr_domain_material->const_count:0);

	if (!tempMaterialRenderInfoAllocator)
		tempMaterialRenderInfoAllocator = linAllocCreate(sizeof(MaterialRenderInfo)*256, false);
	new_render_info = linAlloc(tempMaterialRenderInfoAllocator, sizeof(MaterialRenderInfo));

	tempMaterialRenderInfoCount++;

	*new_render_info = *render_info;
	linAllocDone(tempMaterialRenderInfoAllocator);

	tex_size = tex_count * sizeof(render_info->textures[0]);
	tex_handle_size = tex_count * sizeof(render_info->rdr_material.textures[0]);
	constant_size = const_count * sizeof(render_info->rdr_material.constants[0]);
	drawconst_size = drawconst_count * sizeof(render_info->rdr_material.drawable_constants[0]);
	constant_mapping_size = constant_mapping_count * sizeof(render_info->constant_mapping[0]);
	//if (render_info->rdr_domain_material) {
	//	domain_shader_size = sizeof(RdrNonPixelMaterial);
	//	domain_tex_handle_size = domain_tex_count * sizeof(render_info->rdr_domain_material->textures[0]);
	//	domain_constant_size = const_count * sizeof(render_info->rdr_domain_material->constants[0]);
	//} else {
	//	domain_shader_size = 0;
	//	domain_tex_handle_size = 0;
	//	domain_constant_size = 0;
	//}

	mem = linAlloc(tempMaterialRenderInfoAllocator, tex_size + tex_handle_size + constant_size + drawconst_size + constant_mapping_size);// + domain_shader_size + domain_tex_handle_size + domain_constant_size);

	if (tex_size)
	{
		new_render_info->textures = (void *)mem;
		memcpy_fast(new_render_info->textures, override_textures?override_textures:render_info->textures, tex_size);
		mem += tex_size;
	}
	if (tex_handle_size)
	{
		new_render_info->rdr_material.textures = (void *)mem;
		memset(new_render_info->rdr_material.textures, 0, tex_handle_size);
		mem += tex_handle_size;
	}
	if (constant_size)
	{
		new_render_info->rdr_material.constants = (void*)mem;
		memcpy_fast(new_render_info->rdr_material.constants, override_constants?override_constants:render_info->rdr_material.constants, constant_size);
		mem += constant_size;
	}
	if (drawconst_size)
	{
		new_render_info->rdr_material.drawable_constants = (void*)mem;
		memcpy_fast(new_render_info->rdr_material.drawable_constants, render_info->rdr_material.drawable_constants, drawconst_size);
		mem += drawconst_size;
	}
	if (constant_mapping_size)
	{
		new_render_info->constant_mapping = (void*)mem;
		memcpy_fast(new_render_info->constant_mapping, override_constant_mappings?override_constant_mappings:render_info->constant_mapping, constant_mapping_size);
		mem += constant_mapping_size;
	}

	// This will most likely need to be implemented once we wish to do texture swaps with the domain shader.  However, until that happens, it would be best to leave it alone so it will pull from the current domain material information being provided.
	//if (domain_shader_size) {
	//	new_render_info->rdr_domain_material = (void*)mem;
	//	memset(new_render_info->rdr_domain_material, 0, domain_shader_size);
	//	mem += domain_shader_size;
	//	if (domain_tex_handle_size) {
	//		new_render_info->rdr_domain_material->textures = (void*)mem;
	//		memset(new_render_info->rdr_domain_material->textures, 0, domain_tex_handle_size);
	//		mem += domain_tex_handle_size;
	//	}
	//	if (domain_constant_size) {
	//		new_render_info->rdr_domain_material->constants = (void*)mem;
	//		memset(new_render_info->rdr_domain_material->constants, 0, domain_constant_size);
	//		mem += domain_constant_size;
	//	}
	//}
	linAllocDone(tempMaterialRenderInfoAllocator);

	return new_render_info;
}

static __forceinline RdrMaterialShader filterShaderNum(RdrMaterialShader shader_num)
{
	// Also called when compiling shaders for other platforms/low-end cards
	if (shader_num.shaderMask & MATERIAL_SHADER_UBERLIGHT) {
		shader_num.lightMask = 0;
	}
	if (shader_num.key & MATERIAL_SHADER_VERTEX_ONLY_LIGHTING)
	{
		assert(!shader_num.lightMask);
	}
	return shader_num;
}

RdrMaterialShader gfxShaderFilterShaderNum(RdrMaterialShader shader_num)
{
	return filterShaderNum(shader_num);
}

// ShaderGraphs: Queue only those that need to be loaded on at least one renderer
ShaderHandle gfxMaterialFillShader(ShaderGraphRenderInfo *graph_render_info, RdrMaterialShader shader_num, U32 tri_count)
{
	ShaderGraphHandleData *handle_data = NULL;
	static RdrDevice *lastDeviceChecked = NULL;
	static RdrMaterialShaderMask extraFlags = 0;

	graph_render_info->graph_last_used_time_stamp = gfx_state.client_frame_timestamp;
	if (lastDeviceChecked != gfx_state.currentDevice->rdr_device) {
		lastDeviceChecked = gfx_state.currentDevice->rdr_device;
		if (stricmp(rdrGetDeviceProfileName(gfx_state.currentDevice->rdr_device), "D3D11") == 0) {
			extraFlags |= MATERIAL_SHADER_D3D11;
		} else {
			extraFlags &= ~MATERIAL_SHADER_D3D11;
		}
	}
	if (graph_render_info->shader_graph->graph_flags & SGRAPH_ALPHA_TO_COVERAGE)
	{
		if (gfx_state.antialiasingQuality <= 1)
			shader_num.shaderMask |= MATERIAL_SHADER_COVERAGE_DISABLE;
	}
	else if (shader_num.shaderMask & MATERIAL_SHADER_COVERAGE_DISABLE)
		shader_num.shaderMask &= ~MATERIAL_SHADER_COVERAGE_DISABLE;

	if (!(rdrGetMaterialRenderMode(shader_num) == MATERIAL_RENDERMODE_DEPTH_ONLY) && // Don't double-count because of depth
			tri_count) // Don't count calls to this which are just trying to fill in the performance values
		gfxShaderGraphIncrementUsedCount(graph_render_info, 1, tri_count);
	shader_num = filterShaderNum(shader_num);
	if(   !gfx_disable_auto_force_2_lights
		  && !(systemSpecsMaterialSupportedFeatures() & SGFEAT_SM30_PLUS)
		  && (shader_num.shaderMask & (MATERIAL_SHADER_UBERLIGHT | MATERIAL_SHADER_UBERSHADOW))) {
		shader_num.shaderMask |= MATERIAL_SHADER_FORCE_2_LIGHTS;
	}
	shader_num.shaderMask |= extraFlags;
	if (shader_num.shaderMask & MATERIAL_SHADER_FORCE_SM20)
	{
		// strip the D3D11 flag BACK off, cause we don't support compiling for SM20 and D3D11 at the same time.  This is done here, because extraFlags is a lazily
		// updated static.  [RMARR - 9/26/12]
		shader_num.shaderMask &= ~MATERIAL_SHADER_D3D11;
	}

	// valid for expected settings:  (exception for special FX gets hit at least during preloading)
	//assert(!(rdrGetMaterialRenderMode(shader_num)==MATERIAL_RENDERMODE_HAS_ALPHA || rdrGetMaterialRenderMode(shader_num)==MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE)  || (shader_num & MATERIAL_SHADER_SHADOW_BUFFER) ||
	//	strEndsWith(graph_render_info->shader_graph->filename, "FxParticle.Material") ||
	//	strEndsWith(graph_render_info->shader_graph->filename, "WaterVolume.Material"));

	// not valid because of ScreenDepth shaders which are alpha but run a depth-only pass:
	//assert(!(graph_render_info->shader_graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY) || rdrGetMaterialRenderMode(shader_num)!=MATERIAL_RENDERMODE_DEPTH_ONLY);

	if (graph_render_info->cached_last_key.key == shader_num.key + 1)
	{
		handle_data = graph_render_info->cached_last_value;
	} else {
		graph_render_info->cached_last_key.key = shader_num.key + 1;
		if (!stashFindPointer(graph_render_info->shader_handles, &(graph_render_info->cached_last_key.key), &handle_data))
		{
			MP_CREATE(ShaderGraphHandleData, 512);
			handle_data = MP_ALLOC(ShaderGraphHandleData);
			handle_data->handle = rdrGenShaderHandle();
			handle_data->shader_num = shader_num;
			handle_data->shader_HashKey = shader_num.key + 1;
			handle_data->graph_render_info = graph_render_info;
			assert(stashAddPointer(graph_render_info->shader_handles, &(handle_data->shader_HashKey), handle_data, false));
		}
		graph_render_info->cached_last_value = handle_data;
	}

	assert(handle_data->graph_render_info != (ShaderGraphRenderInfo*)(intptr_t)0xFAFAFAFA);

	if (graph_render_info->graph_last_updated_time_stamp != gfx_state.client_frame_timestamp)
	{
		// first time this frame
		graph_render_info->graph_last_updated_time_stamp = gfx_state.client_frame_timestamp;
	}

	if (handle_data->load_state != gfx_state.allRenderersFlag) {
		// Need to load this!
		if (handle_data->graph_data_last_used_time_stamp != gfx_state.client_frame_timestamp) {
			if (gfx_state.currentDevice->rdr_device->is_locked_nonthread) // Flag instead?
			{
				assert(gfx_state.currentDevice->rdr_device->is_locked_nonthread);
				gfxDemandLoadShaderGraphAtDrawTime(handle_data);
			} else {
				handle_data->graph_data_last_used_time_stamp = gfx_state.client_frame_timestamp;
				// assert(-1==eaFind(&queuedShaderGraphLoads, handle_data)); // Should not be in the list
				if (bBadTimeToQueueLoads && eaSize(&gfx_state.devices)==1) // Not a valid check on multiple devices
					assertmsg(0, "Queuing a new material to be loaded, but we already demand loaded them for this frame");
				gfxVerifyLightCombos(shader_num);
				eaPush(&queuedShaderGraphLoads, handle_data);
				bAddedSomethingToQueuedLoads = true;
			}
		}
		handle_data->load_needed = 1;
	}
	if (handle_data->graph_data_last_used_time_stamp != gfx_state.client_frame_timestamp && tri_count) {
		gfx_state.debug.frame_counts.unique_shaders_referenced++;
		handle_data->graph_data_last_used_time_stamp = gfx_state.client_frame_timestamp;
	}
	assert(handle_data->handle);
	return handle_data->handle;
}

static MaterialRenderInfo *makeMaterialCostRenderInfo(const Material *orig_material, MaterialRenderInfo *render_info, int swaps_applied)
{
	F32 score;
	if (!swaps_applied)
	{
		render_info = allocMaterialRenderInfoForThisFrame(render_info, NULL, NULL, NULL);
		swaps_applied = 1;
	}
	score = gfxMaterialGetPerformanceScore(orig_material);
	assert(render_info->rdr_material.const_count==1);
	assert(render_info->constant_names[0*4][0]=='C'); // Constant 0 should be named ColorValue1
	if (score == 0) {
		// Not computed yet
		setVec4(render_info->rdr_material.constants[0], 0, 0, 1, 1);
	} else if (score < 0.5) {
		setVec4(render_info->rdr_material.constants[0], score*2, 1, 0, 1);
	} else {
		setVec4(render_info->rdr_material.constants[0], 1, (1-score)*2, 0, 1);
	}
	return render_info;
}

__forceinline static F32 * f32cpy(F32 *__restrict dest, const F32 *__restrict source, int count)
{
	for (; count; --count, ++dest, ++source)
		*dest = *source;
	return dest;
}

void setupMaterialRenderInfoTexReflection(MaterialRenderInfo *render_info, const char *tex_input_name)
{
	char texname[MAX_PATH];
	char *s;
	strcpy(texname, tex_input_name);
	if (s=strchr(texname, '.'))
		*s = '\0';
	if (strEndsWith(texname, "_cube")) {
		texname[strlen(texname)-5] = '\0';
	} else if (strEndsWith(texname, "_spheremap")) {
		texname[strlen(texname)-10] = '\0';
	} else {
		// Checked during loading
		//Errorf("Texture specifies \"%s\" for a reflection texture, and it is neither a cubemap nor spheremap.",
		//	texname);
		strcpy(texname, "test_cube");
	}
	strcat(texname, "_cube");
	render_info->override_cubemap_texture = texFind(texname, true);
	strcpy_unsafe(&texname[strlen(texname)-5], "_spheremap");
	render_info->override_spheremap_texture = texFind(texname, true);
}

static void checkSwapReflectionOverrides(MaterialRenderInfo * render_info, const Material * material)
{
	int k;
	// Swapping textures, may need to apply/determine cubemap overrides
	for (k=0; k<(int)render_info->constant_mapping_count; k++)
	{
		if (render_info->constant_mapping[k].data_type == SDT_TEXTURE_REFLECTION)
		{
			int index = render_info->constant_mapping[k].constant_index;
			BasicTexture *swapped_tex = render_info->textures[index];
			if (swapped_tex && swapped_tex != tex_from_sky_file)
			{
				ShaderGraphReflectionType refl_type = MIN(gfx_state.settings.maxReflection, render_info->graph_reflection_type);
				if (refl_type == SGRAPH_REFLECT_NONE) {
					// Do nothing, this texture is ignored
				} else if (refl_type == SGRAPH_REFLECT_SIMPLE) {
					if (swapped_tex->bt_texopt_flags & TEXOPT_CUBEMAP) {
						// Wrong one
						setupMaterialRenderInfoTexReflection(render_info, swapped_tex->name);
						// cannot modify the draw_material, it's hashed!
						// // Save it for next frame
						// draw_material->textures[index] = draw_material->temp_render_info->override_spheremap_texture;
					} else {
						// We're good
						render_info->override_spheremap_texture = swapped_tex;
					}
				} else {
					// Cube
					if (swapped_tex->bt_texopt_flags & TEXOPT_CUBEMAP) {
						// We're good
						render_info->override_cubemap_texture = swapped_tex;
					} else {
						// Wrong one
						setupMaterialRenderInfoTexReflection(render_info, swapped_tex->name);
						// cannot modify the draw_material, it's hashed!
						// // Save it for next frame
						// draw_material->textures[index] = draw_material->temp_render_info->override_cubemap_texture;
					}
				}
			}
		}
	}

}

// Materials: Queue all materials used in a frame, so they can have texDemandLoad called on them
void gfxDemandLoadMaterialAtQueueTimeEx(RdrSubobject *subobject, Material *material, 
										BasicTexture **eaTextureSwaps, const MaterialNamedConstant **eaNamedConstants, 
										const MaterialNamedTexture **eaNamedTextures, const MaterialNamedDynamicConstant **eaNamedDynamicConstants, 
										bool apply_use_flags_to_textures, Vec4 instance_param,
										F32 dist, F32 uv_density)
{
	int i, swaps_applied = 0, num;
	U8 j;

	// TODO: LOD scheme
	MaterialRenderInfo *render_info;
	Material *orig_material = material;
	bool bFoundInstanceParam=false;

	PERFINFO_AUTO_START_FUNC_L3();

	if (!material)
		material = default_material;
	
	// TODO? Move this so it happens as a callback for materialFind?  Or materialGetData?
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial(material, true);

	if (gfx_state.debug.simpleMaterials[0] || gfx_state.debug.show_material_cost) {
		char *swapName = gfx_state.debug.simpleMaterials;
		bool using_alpha = false;
		if(material->graphic_props.needs_alpha_sort && gfx_state.debug.simpleAlphaMaterials[0])
		{
			swapName = gfx_state.debug.simpleAlphaMaterials;
			using_alpha = true;
		}
		if (gfx_state.debug.show_material_cost)
			swapName = "PerformanceMetric";
		if (swapName[0] != '0') {
			// this code is duplicated and that is dangerous
			static Material *simple_material;
			if (!simple_material || stricmp(simple_material->material_name, swapName) != 0) {
				simple_material = materialFindNoDefault(swapName, WL_FOR_UTIL);
				if (!simple_material) {
					if (material->graphic_props.render_info->rdr_material.no_normalmap)
					{
						simple_material = materialFind("test_simple", WL_FOR_UTIL);
					}
					else
					{
						simple_material = materialFind("test_bump", WL_FOR_UTIL);
					}
				}
				assert(simple_material);
				if (gfx_state.debug.simpleMaterials[0] && !using_alpha)
					strcpy(gfx_state.debug.simpleMaterials, simple_material->material_name); // In case of it not existing and we use some other material
				else if (using_alpha)
					strcpy(gfx_state.debug.simpleAlphaMaterials, simple_material->material_name); // In case of it not existing and we use some other material
			}

			material = simple_material;
			eaTextureSwaps = NULL;
			eaNamedConstants = NULL;
			eaNamedTextures = NULL;

			// TODO? Move this so it happens as a callback for materialFind?  Or materialGetData?
			if (!material->graphic_props.render_info)
				gfxMaterialsInitMaterial(material, true);
			
		} else {
			gfx_state.debug.simpleMaterials[0] = '\0';
			gfx_state.debug.simpleAlphaMaterials[0] = '\0';
		}
	}

	assert(material->graphic_props.render_info);
	render_info = material->graphic_props.render_info;
	assert(render_info);

	if (render_info->rdr_material.instance_param_index==-1)
	{
		bFoundInstanceParam = true;
		setVec4(instance_param, 1, 1, 0, 1); // Should not be used anywhere
	}

	// see if swaps need to be applied.  if so, make a new render_info
	num = eaSize(&eaNamedTextures);
	for (i = num-1; i >= 0; --i)
	{
		for (j=0; j<render_info->rdr_material.tex_count; j++) {
			if (render_info->texture_names[j*2] == eaNamedTextures[i]->op && 
				render_info->texture_names[j*2+1] == eaNamedTextures[i]->input &&
				eaNamedTextures[i]->texture != render_info->textures[j])
			{
				// DJR - trying to convert later crashes touching these textures into an assert
				// where I can determine the bad asset
				assert(eaNamedTextures[i]->texture && eaNamedTextures[i]->texture->actualTexture);

				if (!swaps_applied)
				{
					render_info = allocMaterialRenderInfoForThisFrame(render_info, NULL, NULL, NULL);
					swaps_applied = 1;
				}

				render_info->textures[j] = eaNamedTextures[i]->texture;
			}
		}
	}
	num = eaSize(&eaTextureSwaps);
	for (i = num-2; i >= 0; i -= 2)
	{
		for (j = 0; j < render_info->rdr_material.tex_count; j++)
		{
			if (eaTextureSwaps[i] == render_info->textures[j])
			{
				// DJR - trying to convert later crashes touching these textures into an assert
				// where I can determine the bad asset
				assert(eaTextureSwaps[i+1] && eaTextureSwaps[i+1]->actualTexture);
#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
				assert(!texIsFreed(eaTextureSwaps[i+1]));
#endif
				if (!swaps_applied)
				{
					render_info = allocMaterialRenderInfoForThisFrame(render_info, NULL, NULL, NULL);
					swaps_applied = 1;
				}

				render_info->textures[j] = eaTextureSwaps[i+1];
			}
		}
	}
	if (swaps_applied)
		checkSwapReflectionOverrides(render_info, material);

	if (!(render_info->rdr_material.flags & RMATERIAL_NOTINT)) {
		num = eaSize(&eaNamedConstants);
		for (i=num-1; i>=0; --i) {
			// Check for constants with this name
			for (j=0; j<render_info->rdr_material.const_count*4; j++) {
				if (render_info->constant_names[j] == eaNamedConstants[i]->name) {
					const F32* namedValue = eaNamedConstants[i]->value;
					F32* constValue = &((F32*)render_info->rdr_material.constants)[j];
					int swizzleCount = materialConstantSwizzleCount( render_info, j );

					if (j==render_info->rdr_material.instance_param_index)
					{
						bFoundInstanceParam = true;
						f32cpy(instance_param, namedValue, swizzleCount);
					} else {
						if (!sameVecN( namedValue, constValue, swizzleCount ))
						{
							if (!swaps_applied)
							{
								render_info = allocMaterialRenderInfoForThisFrame(render_info, NULL, NULL, NULL);
								swaps_applied = 1;
								constValue = &((F32*)render_info->rdr_material.constants)[j];
							}
							f32cpy(constValue, namedValue, swizzleCount);
						}
					}
				}
			}
			// Check for special dynamic constants with this name
			for (j=0; j<render_info->constant_mapping_count; j++) {
				if (render_info->constant_mapping[j].constant_subindex!=-1 &&
					render_info->constant_names[render_info->constant_mapping[j].constant_index*4+render_info->constant_mapping[j].constant_subindex] == eaNamedConstants[i]->name)
				{
					if (!swaps_applied)
					{
						render_info = allocMaterialRenderInfoForThisFrame(render_info, NULL, NULL, NULL);
						swaps_applied = 1;
					}
					if (render_info->constant_mapping[j].data_type == SDT_ROTATIONMATRIX)
					{
						setConstantMapping(&render_info->constant_mapping[j], cms_scale, eaNamedConstants[i]->value);
						setConstantMapping(&render_info->constant_mapping[j], cms_rotation, &eaNamedConstants[i]->value[2]);
						setConstantMapping(&render_info->constant_mapping[j], cms_rotationRate, &eaNamedConstants[i]->value[3]);
					}
				}
			}
		}
		if (gfx_state.debug.eaNamedConstantOverrides) {
			for (i=0; i<eaSize(&gfx_state.debug.eaNamedConstantOverrides); i++) {
				for (j=0; j<render_info->rdr_material.const_count*4; j++) {
					if (render_info->constant_names[j] == gfx_state.debug.eaNamedConstantOverrides[i]->name) {
						const F32* namedValue = gfx_state.debug.eaNamedConstantOverrides[i]->value;
						F32* constValue = &((F32*)render_info->rdr_material.constants)[j];
						int swizzleCount = materialConstantSwizzleCount( render_info, j );

						if (!sameVecN( namedValue, constValue, swizzleCount ))
						{
							if (!swaps_applied)
							{
								render_info = allocMaterialRenderInfoForThisFrame(render_info, NULL, NULL, NULL);
								swaps_applied = 1;
								constValue = &((F32*)render_info->rdr_material.constants)[j];
							}
							f32cpy(constValue, namedValue, swizzleCount);
						}
					}
				}
			}
		}
	}

	num = eaSize(&eaNamedDynamicConstants);
	for (i=num-1; i>=0; --i) {
		for (j=0; j<render_info->constant_mapping_count; j++) {
			if (render_info->constant_mapping[j].data_type == eaNamedDynamicConstants[i]->data_type) {
				if (!swaps_applied)
				{
					render_info = allocMaterialRenderInfoForThisFrame(render_info, NULL, NULL, NULL);
					swaps_applied = 1;
				}
				setConstantMapping(&render_info->constant_mapping[j], eaNamedDynamicConstants[i]->name, eaNamedDynamicConstants[i]->value);
			}
		}
	}

	if (gfx_state.debug.show_material_cost) {
		render_info = makeMaterialCostRenderInfo(orig_material, render_info, swaps_applied);
		swaps_applied = 1;
	}

	// Set return values
	subobject->material = &render_info->rdr_material;
	subobject->graph_render_info = render_info->graph_render_info;

#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
	for (i=0; i<(int)render_info->rdr_material.tex_count; i++)
		DEBUG_MARK_TEXTURE_QUEUED_FOR_DRAW(render_info->textures[i]);
#endif

	if (apply_use_flags_to_textures)
	{
		assert(material->world_props.usage_flags);
		for (i=0; i<(int)render_info->rdr_material.tex_count; i++) {
			if (!render_info->textures[i])
				continue;
			if (gfx_state.currentAction->override_usage_flags)
			{
				render_info->textures[i]->use_category |= gfx_state.currentAction->override_usage_flags;
			}
			else
			{
				render_info->textures[i]->use_category &= ~WL_FOR_OVERRIDE;
				render_info->textures[i]->use_category |= material->world_props.usage_flags;
			}
		}
	}

	if (!bFoundInstanceParam)
	{
		// Extract it here for non-swapped materials
		int instance_index = render_info->rdr_material.instance_param_index;
		const F32* constValue = &((F32*)render_info->rdr_material.constants)[instance_index];
		int swizzleCount = materialConstantSwizzleCount( render_info, instance_index );
		f32cpy(instance_param, constValue, swizzleCount);
	}

	render_info->usage_flags |= material->world_props.usage_flags;
	if (!swaps_applied && render_info->material_last_used_time_stamp == gfx_state.client_frame_timestamp)
	{
		// Second+ time this frame
		render_info->material_last_used_count++; // Counting duplicates, not total (= total - 1)
		MIN1F(render_info->material_min_draw_dist[gfx_state.currentAction->action_type], dist);
		MIN1F(render_info->material_min_uv_density, uv_density);
		PERFINFO_AUTO_STOP_L3();
		return;
	}
	// Else: first time this frame
	render_info->material_last_used_count=0;
	render_info->material_last_used_time_stamp = gfx_state.client_frame_timestamp;
	for (i = 0; i < GfxAction_MAX; ++i) {
		render_info->material_min_draw_dist[i] = FLT_MAX;
	}
	render_info->material_min_draw_dist[gfx_state.currentAction->action_type] = dist;
	render_info->material_min_uv_density = uv_density;
	// Done automatically in rdrDrawListAddModel: gfxDemandLoadShaderGraphAtQueueTime(render_info->graph_render_info, &render_info->rdr_material, ...?);
	eaPush(&queuedMaterialLoads, render_info);
	gfx_state.debug.frame_counts.unique_materials_referenced++;

	PERFINFO_AUTO_STOP_L3();
}

// Materials: Queue all materials used in a frame, so they can have texDemandLoad called on them
// This will modify materials on the passed in MaterialDraw
MaterialRenderInfo *gfxDemandLoadPreSwappedMaterialAtQueueTime(RdrSubobject *subobject, MaterialDraw *draw_material, F32 dist, F32 uv_density)
{
	bool swaps_applied = false;
	int i;

	// TODO: LOD scheme
	MaterialRenderInfo *render_info;
	const Material *orig_material = draw_material->material;
	const Material *material = draw_material->material;

	PERFINFO_AUTO_START_FUNC_L3();
	
	// TODO? Move this so it happens as a callback for materialFind?  Or materialGetData?
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial((Material*)material, true); // This is apparently okay to do

	if (gfx_state.debug.simpleMaterials[0] || gfx_state.debug.show_material_cost) {
		char *swapName = gfx_state.debug.simpleMaterials;
		bool using_alpha = false;
		if(material->graphic_props.needs_alpha_sort && gfx_state.debug.simpleAlphaMaterials[0])
		{
			swapName = gfx_state.debug.simpleAlphaMaterials;
			using_alpha = true;
		}
		if (gfx_state.debug.show_material_cost)
			swapName = "PerformanceMetric";
		if (swapName[0] != '0') {
			// this code is duplicated and that is dangerous
			static Material *simple_material; 
			if (!simple_material || stricmp(simple_material->material_name, swapName) != 0) {
				simple_material = materialFindNoDefault(swapName, WL_FOR_UTIL);
				if (!simple_material) {
					if (material->graphic_props.render_info->rdr_material.no_normalmap)
					{
						simple_material = materialFind("test_simple", WL_FOR_UTIL);
					}
					else
					{
						simple_material = materialFind("test_bump", WL_FOR_UTIL);
					}
				}
				assert(simple_material);
				if (gfx_state.debug.simpleMaterials[0] && !using_alpha)
					strcpy(gfx_state.debug.simpleMaterials, simple_material->material_name); // In case of it not existing and we use some other material
				else if (using_alpha)
					strcpy(gfx_state.debug.simpleAlphaMaterials, simple_material->material_name); // In case of it not existing and we use some other material
			}
			
			material = simple_material;
			
			// TODO? Move this so it happens as a callback for materialFind?  Or materialGetData?
			if (!material->graphic_props.render_info)
				gfxMaterialsInitMaterial((Material*)material, true); // This is apparently okay to do
		} else {
			gfx_state.debug.simpleMaterials[0] = '\0';
			gfx_state.debug.simpleAlphaMaterials[0] = '\0';
		}
	}

	assert(material->graphic_props.render_info);
	render_info = material->graphic_props.render_info;
	assert(render_info);

	// apply pre-swapped data
	if (!gfx_state.debug.simpleMaterials[0] && !gfx_state.debug.show_material_cost && draw_material->has_swaps && (draw_material->constants || draw_material->textures || draw_material->constant_mappings))
	{
		if (!draw_material->temp_render_info)
		{
			draw_material->temp_render_info = allocMaterialRenderInfoForThisFrame(render_info, draw_material->textures, draw_material->constants, draw_material->constant_mappings);
			if (draw_material->textures)
			{
				checkSwapReflectionOverrides(draw_material->temp_render_info, material);
			}
			swaps_applied = true;
		}
		render_info = draw_material->temp_render_info;
	}

	if (gfx_state.debug.show_material_cost) {
		render_info = makeMaterialCostRenderInfo(orig_material, render_info, swaps_applied);
		swaps_applied = true;
	}

	// Set return values
	subobject->material = &render_info->rdr_material;
	subobject->graph_render_info = render_info->graph_render_info;
	
	//render_info->rdr_material.tex_count = draw_material->tex_count;
	//for (i = 0; i < draw_material->tex_count; i++)
	for (i=0; i<(int)render_info->rdr_material.tex_count; i++)
	{
		if (gfx_state.currentAction->override_usage_flags)
		{
			if (render_info->textures[i])
				render_info->textures[i]->use_category |= gfx_state.currentAction->override_usage_flags;
		}
		else
		{
			assert(material->world_props.usage_flags);
			if (render_info->textures[i])
			{
				render_info->textures[i]->use_category &= ~WL_FOR_OVERRIDE;
				render_info->textures[i]->use_category |= material->world_props.usage_flags;
			}
		}
	}

	render_info->usage_flags |= material->world_props.usage_flags;
	if (!swaps_applied && render_info->material_last_used_time_stamp == gfx_state.client_frame_timestamp)
	{
		// Second+ time this frame
		MIN1F(render_info->material_min_draw_dist[gfx_state.currentAction->action_type], dist);
		MIN1F(render_info->material_min_uv_density, uv_density);
		render_info->material_last_used_count++; // Counting duplicates, not total (= total - 1)
		PERFINFO_AUTO_STOP_L3();
		return render_info;
	}
	// Else: first time this frame
	render_info->material_last_used_count=0;
	render_info->material_last_used_time_stamp = gfx_state.client_frame_timestamp;
	for (i = 0; i < GfxAction_MAX; ++i) {
        render_info->material_min_draw_dist[i] = FLT_MAX;
	}
	render_info->material_min_draw_dist[gfx_state.currentAction->action_type] = dist;
	render_info->material_min_uv_density = uv_density;
	// Done automatically in rdrDrawListAddModel: gfxDemandLoadShaderGraphAtQueueTime(render_info->graph_render_info, &render_info->rdr_material, ...?);
	eaPush(&queuedMaterialLoads, render_info);

	PERFINFO_AUTO_STOP_L3();

	return render_info;
}

static const char *cubemap_debug_override;
// Sets a debug cubemap override
AUTO_COMMAND ACMD_CATEGORY(Debug);
void gfxSetCubemapOverride(const char *s)
{
	if (s && *s)
		cubemap_debug_override = allocAddString(s);
	else
		cubemap_debug_override = NULL;
}

void gfxMaterialSetOverrideCubeMap(const char *cubemap_name)
{
	BasicTexture *cube;
	if (cubemap_debug_override)
		cubemap_name = cubemap_debug_override;
	assert(gfx_state.currentAction);
	if (cubemap_name && (cube = texFindAndFlag(cubemap_name, true, WL_FOR_WORLD|WL_FOR_ENTITY)))
	{
		BasicTexture *spheremap;
		char spheremap_name[MAX_PATH];
		char *s;
		gfx_state.currentAction->env_cubemap_from_region = cube;
		strcpy(spheremap_name, cubemap_name);
		s = strrchr(spheremap_name, '_');
		if (s)
		{
			assert(s);
			strcpy_s(s, ARRAY_SIZE(spheremap_name) - (s - spheremap_name), "_spheremap");
		} // otherwise, use the same texture, maybe it's not a cubemap anyway
		if (spheremap = texFindAndFlag(spheremap_name, true, WL_FOR_WORLD|WL_FOR_ENTITY))
		{
			gfx_state.currentAction->env_spheremap_from_region = spheremap;
		} else {
			gfx_state.currentAction->env_spheremap_from_region = NULL;
		}
		cubemap_override = cube;
	} else {
		gfx_state.currentAction->env_cubemap_from_region = NULL;
		gfx_state.currentAction->env_spheremap_from_region = NULL;
		cubemap_override = NULL;
	}
}

void gfxMaterialsOncePerFrame(void)
{
	PERFINFO_AUTO_START_FUNC_PIX();

	// Called at *end* of frame
	// Must call at the end of the frame, because we have an assert in texture
	// freeing that the texture being freed is not in the list of materials
	// being referenced this frame, and freeing textures after the end of a
	// frame but before the beginning of the next frame is valid.
	eaSetSize(&queuedMaterialLoads, 0);
	linAllocClear(tempMaterialRenderInfoAllocator);
	worldCellEntryClearTempMaterials();
	gfxDynDrawModelClearTempMaterials(false);
	//assert(!bAddedSomethingToQueuedLoads);
	eaSetSize(&queuedShaderGraphLoads, 0); // Otherwise duplicates will show up next frame
	bBadTimeToQueueLoads = false;
	if (gfx_state.shadersReloadedThisFrame)
		gfx_state.shadersReloadedThisFrame--;

	PERFINFO_AUTO_STOP_FUNC_PIX();
}
