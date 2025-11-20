/***************************************************************************



***************************************************************************/

#include "GfxDeferredShadows.h"
#include "earray.h"
#include "qsortG.h"
#include "rand.h"
#include "ScratchStack.h"
#include "MemRef.h"

#include "RdrLightAssembly.h"
#include "RdrDrawList.h"

#include "GfxLightsPrivate.h"
#include "GfxPostprocess.h"
#include "GfxTexturesInline.h"
#include "GfxSky.h"
#include "GfxSplat.h"
#include "GfxMaterialProfile.h"
#include "GfxSurface.h"
#include "GfxDebug.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

#define SSAO_REFLECTION_RES 64

bool shadowBlur = true;

// Enables shadow buffer blur.
AUTO_CMD_INT(shadowBlur, shadowBlur) ACMD_COMMANDLINE;

static Vec3 *ssao_vectors;
static float *ssao_vector_lengths;

__forceinline static int cmpLightTypes(const GfxLight **light1, const GfxLight **light2)
{
	if (!(*light2) || !((*light2)->rdr_light.light_type))
		return -1;
	if (!(*light1) || !((*light1)->rdr_light.light_type))
		return 1;
	return ((int)(*light1)->rdr_light.light_type) - ((int)(*light2)->rdr_light.light_type);
}

static void updateReflectionTexture(void)
{
	bool changed = !gfx_state.currentDevice->ssao_reflection_lookup_tex || !ssao_vectors;
	int i;

	if (changed)
		loadstart_printf("Generating SSAO lookups...");

	if (!gfx_state.currentDevice->ssao_reflection_lookup_tex)
	{
		Vec3 *poisson_points;
		U8 *data, *dataptr;

		gfx_state.currentDevice->ssao_reflection_lookup_tex = texGenNew(SSAO_REFLECTION_RES, SSAO_REFLECTION_RES, "SSAO reflection lookup texture", TEXGEN_NORMAL, WL_FOR_UTIL);
		dataptr = data = memrefAlloc(SSAO_REFLECTION_RES * SSAO_REFLECTION_RES * 4 * sizeof(U8));
		poisson_points = ScratchAlloc(SSAO_REFLECTION_RES * SSAO_REFLECTION_RES * sizeof(Vec3));

		randomPoissonSphereShellSpatialSeeded(NULL, RandType_Mersenne, 0.5f, SSAO_REFLECTION_RES, SSAO_REFLECTION_RES, poisson_points);

		for (i = 0; i < SSAO_REFLECTION_RES * SSAO_REFLECTION_RES; ++i)
		{
			Vec3 plane;
			int ival;
			addVec3same(poisson_points[i], 0.5f, plane);
			ival = round(plane[0] * 255);
			*(dataptr++) = CLAMP(ival, 0, 255);
			ival = round(plane[1] * 255);
			*(dataptr++) = CLAMP(ival, 0, 255);
			ival = round(plane[2] * 255);
			*(dataptr++) = CLAMP(ival, 0, 255);
			ival = round((randomF32Seeded(NULL, RandType_Mersenne) * 0.25f + 0.75f) * 255);
			*(dataptr++) = CLAMP(ival, 0, 255);
		}

		texGenUpdate(gfx_state.currentDevice->ssao_reflection_lookup_tex, data, RTEX_2D, RTEX_BGRA_U8, 1, false, false, true, true);
		memrefDecrement(data);
		ScratchFree(poisson_points);
	}

	if (!ssao_vectors)
	{
		ssao_vectors = calloc(NUM_SSAO_SAMPLES, sizeof(Vec3));
		ssao_vector_lengths = calloc(NUM_SSAO_SAMPLES, sizeof(float));
		randomPoissonSphereShellSeeded(NULL, RandType_Mersenne, 1, NUM_SSAO_SAMPLES, 0.75f, ssao_vectors);
		for (i = 0; i < NUM_SSAO_SAMPLES; i++)
			ssao_vector_lengths[i] = 0.1f+0.9f*randomPositiveF32Seeded(NULL, RandType_Mersenne);
	}

	if (changed)
		loadend_printf(" done.");
}

static void drawAODrawList()
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;

	if (gdraw->do_ssao && gdraw->draw_list_ao)
	{
		RdrSortType st;

		gfxBeginSection(__FUNCTION__);

		rdrDrawListSendVisualPassesToRenderer(gfx_state.currentDevice->rdr_device, gdraw->draw_list_ao, gfxGetLightDefinitionArray(rdrGetDeviceIdentifier(gfx_state.currentDevice->rdr_device)));
		for (st = 0; st < RST_AUTO; st++)
		{
			rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, gdraw->draw_list_ao, st, false, false, false);
		}

		rdrDrawListAddStats(gdraw->draw_list_ao, &gfx_state.debug.frame_counts.draw_list_stats);

		gfxEndSection();
	}
}

static void gfxRenderSSAOPrepass(const BlendedSkyInfo *sky_info)
{
	Vec4 constants[NUM_SSAO_CONSTANTS + NUM_SSAO_SAMPLES];
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	int i;
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[3];
	RdrSurface *input_surface = gfxGetStageInputSurface(GFX_SHADOW_BUFFER, 0);
	RdrMaterialShader shader_num = {0};
	GfxSpecialShader special_shader = GSS_DEFERRED_SSAO_PREPASS;

	PERFINFO_AUTO_START_FUNC();
	gfxBeginSection(__FUNCTION__);

	textures[0] = gfxGetStageInputTexHandle(GFX_SHADOW_BUFFER, 0);
	textures[1] = texDemandLoadFixed(gfx_state.currentDevice->ssao_reflection_lookup_tex);
	ppscreen.material.tex_count = 2; // Must line up to value in gfxPostprocessCheckPreload()

	//do this after in case the depth surface was already bound so we dont unbind it, bind it, resolve, unbind it
	gfxSetActiveSurface(gfx_state.currentAction->bufferSSAOPrepass->surface);

	rdrClearActiveSurface(gfx_state.currentDevice->rdr_device, MASK_SBUF_ALL_COLOR, zerovec4, 1);

	ppscreen.material.textures = textures;

	ppscreen.tex_width = input_surface->width_nonthread;
	ppscreen.tex_height = input_surface->height_nonthread;

	ppscreen.blend_type = RPPBLEND_REPLACE;

	//////////////////////////////////////////////////////////////////////////
	// Warning: If you change any of method for determining the parameters to
	//  gfxDemandLoadSpecialShaderEx (e.g. texture count or constant count),
	//  you must also update gfxPostprocessCheckPreload().
	//////////////////////////////////////////////////////////////////////////

	ppscreen.material.const_count = ARRAY_SIZE(constants); // Must line up to value in gfxPostprocessCheckPreload()
	ppscreen.material.constants = constants;

	getMatRow(gfx_state.currentCameraView->projection_matrix, 0, constants[0]);
	getMatRow(gfx_state.currentCameraView->projection_matrix, 1, constants[1]);
	setVec4(constants[2], input_surface->width_nonthread / (F32)SSAO_REFLECTION_RES, input_surface->height_nonthread / (F32)SSAO_REFLECTION_RES, 0, sky_info->occlusionValues.worldSampleRadius);
	setVec4(constants[3], sky_info->occlusionValues.overallScale, sky_info->occlusionValues.overallOffset, sky_info->occlusionValues.worldSampleFalloff, sky_info->occlusionValues.worldSampleScale);
	// the dimensions of the depth buffer we are going to sample
	setVec4(constants[4], 1.0f/input_surface->width_nonthread, 1.0f/input_surface->height_nonthread, 0,0);

	for (i = 0; i < NUM_SSAO_SAMPLES; ++i)
	{
		copyVec3(ssao_vectors[i], constants[i+NUM_SSAO_CONSTANTS]);
		constants[i+NUM_SSAO_CONSTANTS][3] = ssao_vector_lengths[i];
	}

	ZeroArray(ppscreen.lights);

	ppscreen.shader_handle = gfxDemandLoadSpecialShaderEx(special_shader, 
				ppscreen.material.const_count, ppscreen.material.tex_count, 
				shader_num, NULL);

	gfxPostprocessScreen(&ppscreen);

	gfxEndSection();

//	gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentAction->bufferSSAOPrepass->surface,
	//		SBUF_0, 0, "ssao prepass", 1);

	PERFINFO_AUTO_STOP();
}

void gfxRenderShadowBuffer(GfxLight **lights_in, int light_count, 
	const RdrLightDefinition **light_defs, const BlendedSkyInfo *sky_info, int scattering_stage)
{
	static GfxLight **lights = NULL;
	Vec4 constants[NUM_SSAO_CONSTANTS + NUM_SSAO_SAMPLES];
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	int i, j, start_idx = 0;
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[3];
	RdrLight *rdr_lights[MAX_NUM_OBJECT_LIGHTS];
	int render_stage = scattering_stage ? GFX_CALCULATE_SCATTERING : GFX_SHADOW_BUFFER;
	RdrSurface *input_surface = gfxGetStageInputSurface(render_stage, 0);
	bool need_to_do_ssao = gdraw->do_ssao && !scattering_stage;
	const Frustum * view_frustum = scattering_stage ? &gfx_state.currentAction->cameraView->frustum : NULL;

	if (need_to_do_ssao)
	{
		updateReflectionTexture();
		gfxRenderSSAOPrepass(sky_info);
	}

	PERFINFO_AUTO_START_FUNC();
	gfxBeginSection(__FUNCTION__);


	if (light_count <= 0 && !gdraw->do_ssao)
	{
		gfxSetStageActiveSurface(render_stage);
		rdrClearActiveSurface(gfx_state.currentDevice->rdr_device, MASK_SBUF_ALL_COLOR, zerovec4, 1);
		gfxEndSection();
		PERFINFO_AUTO_STOP();
		return;
	}

	eaSetSize(&lights, 0);
	eaPushArray(&lights, lights_in, light_count);
	if (light_count > 1 && !scattering_stage)
		eaQSortG(lights, cmpLightTypes);
	if (scattering_stage && light_count > 1)
		light_count = 1;

	textures[0] = gfxGetStageInputTexHandle(render_stage, 0);

	//do this after in case the depth surface was already bound so we dont unbind it, bind it, resolve, unbind it
	gfxSetStageActiveSurface(render_stage);

	ppscreen.material.textures = textures;

	ppscreen.tex_width = input_surface->width_nonthread;
	ppscreen.tex_height = input_surface->height_nonthread;

	ppscreen.blend_type = RPPBLEND_REPLACE;

	ppscreen.shadow_buffer_render = 1;

	while (start_idx < light_count || need_to_do_ssao)
	{
		RdrMaterialShader shader_num = {0};
		GfxSpecialShader special_shader = gfx_state.settings.poissonShadows ? GSS_DEFERRED_SHADOW_POISSON : GSS_DEFERRED_SHADOW;
		int max_lights = rdrMaxSupportedObjectLights();

		//////////////////////////////////////////////////////////////////////////
		// Warning: If you change any of method for determining the parameters to
		//  gfxDemandLoadSpecialShaderEx (e.g. texture count or constant count),
		//  you must also update gfxPostprocessCheckPreload().
		//////////////////////////////////////////////////////////////////////////

		ppscreen.material.const_count = 0;
		ppscreen.material.tex_count = 1; // Must line up to value in gfxPostprocessCheckPreload()

		if (scattering_stage)
		{
			float shadow_far_fade = 0.0f;
			float scatter_max = 0.0f;
			if (start_idx < light_count)
			{
				int scatter_light;
				shadow_far_fade = 0.0f;
				for (scatter_light = start_idx; scatter_light < light_count; ++scatter_light)
				{
					const RdrLight *light = &lights[scatter_light]->rdr_light;
					if (light)
					{
						float far_fade;
						if  (rdrGetSimpleLightType(light->light_type) == RDRLIGHT_DIRECTIONAL)
							far_fade = light->shadowmap[1].far_fade;
						else
							far_fade = light->point_spot_params.outer_radius;
						if (shadow_far_fade < far_fade)
							shadow_far_fade = far_fade;
					}
				}
			}

			special_shader = GSS_CALCULATE_SCATTERING;
			ppscreen.material.const_count = 1;
			ppscreen.material.constants = constants;
			copyVec2(sky_info->scatteringValues.scatterParameters, constants[0]);
			// scatter rate ratio
			constants[0][2] = -constants[0][0] / constants[0][1];
			// one over maximum scatter value inside volume we care about, for
			// normalizing scatter buffer to preserve precision
			scatter_max = constants[0][2] * (1 - exp(constants[0][1] * shadow_far_fade));
			constants[0][3] = 1.0f / scatter_max;

			gdraw->scatter_max = scatter_max * sky_info->scatteringValues.scatterParameters[2];
		}
		else if (need_to_do_ssao)
		{
			max_lights--;
			// override the previously selected special shader to one that includes SSAO
			special_shader = gfx_state.settings.poissonShadows ? GSS_DEFERRED_SHADOW_SSAO_POISSON : GSS_DEFERRED_SHADOW_SSAO;
			ppscreen.material.const_count = ARRAY_SIZE(constants); // Must line up to value in gfxPostprocessCheckPreload()
			ppscreen.material.constants = constants;

			ppscreen.material.tex_count = 3; // Must line up to value in gfxPostprocessCheckPreload()
			textures[1] = texDemandLoadFixed(gfx_state.currentDevice->ssao_reflection_lookup_tex);
			textures[2] = rdrSurfaceToTexHandle(gfx_state.currentAction->bufferSSAOPrepass->surface, SBUF_0);

			getMatRow(gfx_state.currentCameraView->projection_matrix, 0, constants[0]);
			getMatRow(gfx_state.currentCameraView->projection_matrix, 1, constants[1]);
			setVec4(constants[2], ppscreen.tex_width / (F32)SSAO_REFLECTION_RES, ppscreen.tex_height / (F32)SSAO_REFLECTION_RES, 0, sky_info->occlusionValues.worldSampleRadius);
			setVec4(constants[3], sky_info->occlusionValues.overallScale, sky_info->occlusionValues.overallOffset, sky_info->occlusionValues.worldSampleFalloff, sky_info->occlusionValues.worldSampleScale);
			setVec4(constants[4], 1.0f/ppscreen.tex_width, 1.0f/ppscreen.tex_height, 0,0);

			for (i = 0; i < NUM_SSAO_SAMPLES; ++i)
			{
				copyVec3(ssao_vectors[i], constants[i+NUM_SSAO_CONSTANTS]);
				constants[i+NUM_SSAO_CONSTANTS][3] = ssao_vector_lengths[i];
			}
			//update the output.phl direct light factor
			rdrSetSSAODirectIllumFactor(gfx_state.currentDevice->rdr_device, sky_info->occlusionValues.litAmount);
			need_to_do_ssao = false;
		}

		ZeroArray(ppscreen.lights);
		ZeroArray(rdr_lights);

		for (i = 0; i < max_lights; ++i, ++start_idx)
		{
			if (start_idx < light_count)
			{
				RdrLight *light = &lights[start_idx]->rdr_light;
				RdrLightData *light_data = _alloca(sizeof(RdrLightData));

				ZeroStructForce(light_data);

				rdr_lights[i] = light;
				ppscreen.lights[i] = light_data;
				light_data->light_type = light->light_type;
				shader_num.lightMask |= rdrGetMaterialShaderType(light->light_type, i);
			}
		}

		if (gfxStereoscopicActive())	// All stereoscopic shadow PP shaders are one iteration above their non-stereoscopic counterparts.
		{
			special_shader ++;
			ppscreen.stereoscopic = 1;
		}

		ppscreen.shader_handle = gfxDemandLoadSpecialShaderEx(special_shader, 
					ppscreen.material.const_count, ppscreen.material.tex_count, 
					shader_num, NULL);

		for (i = 0; i < max_lights; ++i)
		{
			if (rdr_lights[i])
			{
				RdrLight *light = rdr_lights[i];
				RdrLightData *light_data = ppscreen.lights[i];
				const RdrLightDefinition *light_def = light_defs[rdrGetSimpleLightType(light->light_type)];

				rdrLightAddConstAndTexCounts(light_def, light->light_type,
					&light_data->normal[0].const_count, &light_data->normal[0].tex_count,
					&light_data->shadow_test.const_count, &light_data->shadow_test.tex_count, false);

				// put the shadow_test data in instead of the normal light data
				light_data->normal[0].const_count = light_data->shadow_test.const_count;
				light_data->normal[0].tex_count = light_data->shadow_test.tex_count;
				light_data->shadow_test.const_count = 0;
				light_data->shadow_test.tex_count = 0;

				for (j = 0; j < RLCT_COUNT; ++j)
				{
					if (j > 0)
					{
						light_data->normal[j].const_count = light_data->normal[0].const_count;
						light_data->normal[j].tex_count = light_data->normal[0].tex_count;
					}

					if (light_data->normal[j].const_count)
					{
						light_data->normal[j].constants = _alloca(sizeof(Vec4) * light_data->normal[j].const_count);
						rdrLightFillMaterialConstants(light_data->normal[j].constants, light_def, light, light->light_type, 
							view_frustum ? view_frustum->viewmat : unitmat, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
					}

					if (light_data->normal[j].tex_count)
					{
						light_data->normal[j].tex_handles = _alloca(sizeof(TexHandle) * light_data->normal[j].tex_count);
						rdrLightFillMaterialTextures(light_data->normal[j].tex_handles, light_def, light, light->light_type, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
					}
				}
			}
		}

		ppscreen.add_half_pixel = true;
		gfxPostprocessScreen(&ppscreen);

		// all passes after the first should add instead of replace
		ppscreen.blend_type = RPPBLEND_ADD;
	}

	//draw the ambient occlusion shadow splats
	if (!scattering_stage)
		drawAODrawList();


	if (shadowBlur && !scattering_stage && (gfx_state.settings.poissonShadows || gdraw->do_ssao))
	{
		gfxDoSmartBlurInPlace(gfxGetStageOutput(GFX_SHADOW_BUFFER), gfxGetStageTemp(GFX_SHADOW_BUFFER, 0), textures[0], -9, 1);
	}
	else
	if (scattering_stage)
	{
		gfxDoGaussianBlurInPlace(gfxGetStageOutput(GFX_CALCULATE_SCATTERING), gfxGetStageTemp(GFX_CALCULATE_SCATTERING, 0), 3);
	}

	gfxEndSection();
	PERFINFO_AUTO_STOP();
}

void gfxSetShadowBufferTexture(RdrDevice *rdr_device, RdrSurface *shadow_buffer)
{
	if (!rdr_device || !rdr_device->is_locked_nonthread)
		return;
	if (!shadow_buffer || gfx_state.currentSurface == shadow_buffer)
		rdrSetShadowBufferTexture(gfx_state.currentDevice->rdr_device, texDemandLoadFixed(invisible_tex));
	else
		rdrSetShadowBufferTexture(gfx_state.currentDevice->rdr_device, rdrSurfaceToTexHandle(shadow_buffer, SBUF_0));
}

