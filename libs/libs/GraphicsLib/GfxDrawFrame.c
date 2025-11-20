#include "GraphicsLibPrivate.h"
#include "GfxPostprocess.h"
#include "GfxSky.h"
#include "GfxSurface.h"
#include "GfxDebug.h"
#include "GfxLights.h"
#include "GfxShadows.h"
#include "GfxSpriteList.h"
#include "GfxMaterialProfile.h"
#include "GfxPrimitivePrivate.h"
#include "GfxOcclusion.h"
#include "GfxWorld.h"
#include "GfxDynamics.h"
#include "GfxGeo.h"
#include "GfxNVPerf.h"
#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxSprite.h"
#include "GfxMaterials.h"
#include "GfxCommandParse.h"
#include "GfxHeadshot.h"
#include "GfxImposter.h"
#include "GfxPrimitive.h"
#include "GfxTextures.h"
#include "GfxTerrain.h"
#include "GfxLightDebugger.h"
#include "../StaticWorld/WorldCell.h"

#include "SystemSpecs.h"
#include "RdrState.h"
#include "RdrTexture.h"
#include "partition_enums.h"

#include "inputLib.h"

#include "MemoryBudget.h"
#include "jpeg.h"
#include "tga.h"
#include "WorldLib.h"

#include "RoomConn.h"
#include "utf8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

static int max_actions_per_frame[] = {
	0, //Unused
	1, //Primary (unused)
	10, //Headshots
	1, //ImposterAtlases
};

// Sets the maximum number of headshot draw actions per frame
AUTO_CMD_INT(max_actions_per_frame[GfxAction_Headshot], max_actions_headshot) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;
// Sets the maximum number of bodysock draw actions per frame
AUTO_CMD_INT(max_actions_per_frame[GfxAction_ImpostorAtlas], max_actions_bodysock) ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

// flag for whether we should load all world cells instead of just the nearby ones
static bool s_OpenAllWorldCells = false;

AUTO_COMMAND ACMD_NAME(LoadAllCells);
void gfxLoadAllCells(int i)
{
	s_OpenAllWorldCells = (i > 0);
}

bool gfxIsLoadingAllCells(void)
{
	return s_OpenAllWorldCells;
}

void gfxActionReleaseSurfaces(GfxRenderAction *action)
{
	gfxReleaseTempSurface(action->bufferFinal);
	gfxReleaseTempSurface(action->bufferLDR);
	gfxReleaseTempSurface(action->bufferLDRandHDR);
	gfxReleaseTempSurface(action->bufferHDR);
	gfxReleaseTempSurface(action->bufferMRT);
	gfxReleaseTempSurface(action->bufferOutline);
	gfxReleaseTempSurface(action->bufferOutlinePCA);
	gfxReleaseTempSurface(action->bufferScatter);
	gfxReleaseTempSurface(action->bufferHighlight);
	gfxReleaseTempSurface(action->bufferSSAOPrepass);
	gfxReleaseTempSurface(action->bufferDeferredShadows);
	gfxReleaseTempSurface(action->bufferTempFullSize);
	gfxReleaseTempSurface(action->bufferTempDepthMSAA);
	gfxReleaseTempSurface(action->bloom[0]);
	gfxReleaseTempSurface(action->bloom[1]);
	gfxReleaseTempSurface(action->bloom_med[0]);
	gfxReleaseTempSurface(action->bloom_med[1]);
	gfxReleaseTempSurface(action->bloom_low[0]);
	gfxReleaseTempSurface(action->bloom_low[1]);
	gfxReleaseTempSurface(action->lum64);
	gfxReleaseTempSurface(action->lum16);
	gfxReleaseTempSurface(action->lum4);
	gfxReleaseTempSurface(action->lum1);
	gfxReleaseTempSurface(action->lum_measure);
	gfxReleaseTempSurface(action->lens_zo_64);
	gfxReleaseTempSurface(action->lens_zo_16);
	gfxReleaseTempSurface(action->lens_zo_4);
	gfxReleaseTempSurface(action->lens_zo_1);
	gfxReleaseTempSurface(action->blueshift_lut);
	gfxReleaseTempSurface(action->tonecurve_lut);
	gfxReleaseTempSurface(action->colorcurve_lut);
	gfxReleaseTempSurface(action->intensitytint_lut);
	gfxReleaseTempSurface(action->postprocess_all_lut);
	gfxReleaseTempSurface(action->low_res_alpha_edge);
	gfxReleaseTempSurface(action->low_res_alpha_scratch);
	gfxReleaseTempSurface(action->bufferUI);

	gfxReleaseTempSurface(action->freeMe);

	if (action->gdraw.do_shadows)
		gfxShadowsEndFrame();
}

static void clearAction(GfxRenderAction *action)
{
	GfxRenderAction local_copy;

	gfxActionReleaseSurfaces(action);

	rdrDrawListEndFrame(action->gdraw.draw_list);
	rdrDrawListEndFrame(action->gdraw.draw_list_sky);
	rdrDrawListEndFrame(action->gdraw.draw_list_ao);

	eaClear(&action->gdraw.regions);
	eaClear(&action->gdraw.region_gdatas);
	eafClear(&action->gdraw.camera_positions);
	eaiClear(&action->gdraw.hidden_object_ids);
	eaClear(&action->gdraw.terrain_blocks);
	eaClear(&action->gdraw.this_frame_lights);
	// If main action, leave the prior frame's shadowmap lights so we can ensure we fade them out
	if (action->is_offscreen)
		gfxActionClearShadowmapLightHistory(action);

	// Once we're all done drawing (and Z occlusion computation for the next
	// frame has started) we can update the camera frustum to be used by the
	// next frame's drawing.
	gfxFlipCameraFrustum(action->cameraView);
	if(action->cameraController)
		action->cameraController->last_camdist = action->cameraController->camdist;

	// zero out everything except the cached memory allocations
	CopyStructs(&local_copy, action, 1);
	ZeroStructForce(action);
	action->is_offscreen = local_copy.is_offscreen;
	action->nTXAABufferFlip = local_copy.nTXAABufferFlip;
	action->gdraw.draw_list = local_copy.gdraw.draw_list;
	action->gdraw.draw_list_sky = local_copy.gdraw.draw_list_sky;
	action->gdraw.draw_list_ao = local_copy.gdraw.draw_list_ao;
	action->gdraw.regions = local_copy.gdraw.regions;
	action->gdraw.region_gdatas = local_copy.gdraw.region_gdatas;
	action->gdraw.camera_positions = local_copy.gdraw.camera_positions;
	action->gdraw.terrain_blocks = local_copy.gdraw.terrain_blocks;
	action->gdraw.hidden_object_ids = local_copy.gdraw.hidden_object_ids;
	action->gdraw.this_frame_lights = local_copy.gdraw.this_frame_lights;
	action->gdraw.this_frame_shadowmap_lights = local_copy.gdraw.this_frame_shadowmap_lights;
}

static void freeAction(GfxRenderAction *action)
{
	if (!action)
		return;

	rdrFreeDrawList(action->gdraw.draw_list);
	rdrFreeDrawList(action->gdraw.draw_list_sky);
	rdrFreeDrawList(action->gdraw.draw_list_ao);

	eaDestroy(&action->gdraw.regions);
	eaDestroy(&action->gdraw.region_gdatas);
	eafDestroy(&action->gdraw.camera_positions);
	eaiDestroy(&action->gdraw.hidden_object_ids);
	eaDestroy(&action->gdraw.terrain_blocks);
	eaDestroy(&action->gdraw.this_frame_lights);
	eaDestroy(&action->gdraw.this_frame_shadowmap_lights);

	free(action);
}

void gfxFreeActions(GfxPerDeviceState *device_state)
{
	eaDestroyEx(&device_state->actions, clearAction);

	eaDestroyEx(&device_state->offscreen_actions, freeAction);
	freeAction(device_state->main_frame_action);
	device_state->main_frame_action = NULL;
}

void gfxSaveScreenshotInternal(char *filename)
{
	RdrSurface *surface = rdrGetPrimarySurface(gfx_state.currentDevice->rdr_device);
	U8 *data = rdrGetActiveSurfaceData(gfx_state.currentDevice->rdr_device, SURFDATA_RGBA, gfx_activeSurfaceSize[0], gfx_activeSurfaceSize[1]);
	if (data)
	{
		char destpath[MAX_PATH];
		U32 x, y, line_size = gfx_activeSurfaceSize[0] * 4;
#if !_PS3
		U8 *data_flipped = malloc(gfx_activeSurfaceSize[1] * line_size);
		for (y = 0; y < gfx_activeSurfaceSize[1]; ++y)
		{
			U32 idx1 = y * line_size;
			U32 idx2 = (gfx_activeSurfaceSize[1] - y - 1) * line_size;
			for (x = 0; x < gfx_activeSurfaceSize[0]; ++x)
			{
				data_flipped[idx2+0] = data[idx1+0];
				data_flipped[idx2+1] = data[idx1+1];
				data_flipped[idx2+2] = data[idx1+2];
				data_flipped[idx2+3] = 255;
				idx1+=4;
				idx2+=4;
			}
		}
#else
        U8 *data_flipped = data;
#endif
		if (fileIsAbsolutePath(filename)) {
			strcpy(destpath, filename);
		} else {
			fileSpecialDir("screenshots", SAFESTR(destpath));
			strcat(destpath, "/");
			strcat(destpath, filename);
		}
		if (strEndsWith(destpath, ".jpg") || strEndsWith(destpath, ".jpeg"))
		{
			jpgSave(destpath, data_flipped, 4, gfx_activeSurfaceSize[0], gfx_activeSurfaceSize[1], gfx_state.jpegQuality);
		}
		else
		{
			char destpathtga[MAX_PATH];
			changeFileExt(destpath, ".tga", destpathtga);
			tgaSave(destpathtga, data_flipped, gfx_activeSurfaceSize[0], gfx_activeSurfaceSize[1], 4);
		}
		free(data);
#if !_PS3
		free(data_flipped);
#endif
	}
}

static void setupCubeMapFrustumForFace(Frustum *frustum, const Vec3 cam_pos, int dir)
{
	Mat4 camera_matrix;
	static Vec3 pyrs[] = {
		{0, -PI/2, 0},
		{0, PI/2, 0},
		{PI, 0, PI},
		{0, 0, 0},
		{-PI/2, 0, PI},
		{PI/2, 0, PI},
	};

	assert(dir >= 0 && dir < ARRAY_SIZE(pyrs));

	//	rdrSetupPerspectiveProjection(projection_matrix, 90, 1.0, gfx_state.near_plane_dist, gfx_state.far_plane_dist);
	frustumSet(frustum, 90, 1.0f, gfx_state.near_plane_dist, gfx_state.far_plane_dist);
	createMat3YPR(camera_matrix, pyrs[dir]);
	copyVec3(cam_pos, camera_matrix[3]);
	frustumSetCameraMatrix(frustum, camera_matrix);
}

__forceinline static void enableStage(GfxStages stage)
{
	gfx_state.currentAction->render_stages[stage].enabled = true;
}

__forceinline static void setStageInput(GfxStages stage, int input_idx, RdrSurface *surface, RdrSurfaceBuffer buffer, int snapshot_idx)
{
	gfx_state.currentAction->render_stages[stage].inputs[input_idx].surface = surface;
	gfx_state.currentAction->render_stages[stage].inputs[input_idx].buffer = buffer;
	gfx_state.currentAction->render_stages[stage].inputs[input_idx].snapshot_idx = snapshot_idx;
}

__forceinline static void setStageOutput(GfxStages stage, RdrSurface *surface, RdrSurfaceBufferMaskBits write_mask)
{
	gfx_state.currentAction->render_stages[stage].output.surface = surface;
	gfx_state.currentAction->render_stages[stage].output.write_mask = write_mask;
}

__forceinline static void copyStageOutput(GfxStages stage_src, GfxStages stage_dst)
{
	gfx_state.currentAction->render_stages[stage_dst].output.surface = gfx_state.currentAction->render_stages[stage_src].output.surface;
	gfx_state.currentAction->render_stages[stage_dst].output.write_mask = gfx_state.currentAction->render_stages[stage_src].output.write_mask;
}

__forceinline static void setStageTemp(GfxStages stage, int temp_idx, RdrSurface *surface)
{
	gfx_state.currentAction->render_stages[stage].temps[temp_idx] = surface;
}

static void setupRenderStages(void)
{
	GfxPerDeviceState *device_state = gfx_state.currentDevice;
	GfxRenderAction *action = gfx_state.currentAction;
	GfxGlobalDrawParams *gdraw = &action->gdraw;
	RdrSurface *finalOutput = action->outputSurface;
	int bloomQuality = gfx_state.settings.bloomQuality;
	int depthSnapshotIndex = 1;
	int finalSnapshotIndex;

	ZeroArray(action->render_stages);
	ZeroStructForce(&action->snapshotLDR);
	ZeroStructForce(&action->snapshotHDR);
	ZeroStructForce(&action->opaqueDepth);

	if (!action->is_offscreen)
	{
		if (gfx_state.ui_postprocess && gfxDoingPostprocessing())
		{
			enableStage(GFX_UI_POSTPROCESS);
			setStageInput(GFX_UI_POSTPROCESS, 0, action->bufferUI->surface, SBUF_0, 0);
			setStageOutput(GFX_UI_POSTPROCESS, finalOutput, 0);
			finalOutput = action->bufferUI->surface;
		}

		enableStage(GFX_UI);
		setStageOutput(GFX_UI, finalOutput, 0);
	}

	enableStage(GFX_OPAQUE_ONEPASS);
	enableStage(GFX_OPAQUE_AFTER_ZPRE);
	if (action->draw_sky)
		enableStage(GFX_SKY);
	enableStage(GFX_NONDEFERRED_POST_OUTLINING);
	enableStage(GFX_ALPHA_PREDOF);
	enableStage(GFX_ALPHA);

	if (action->bufferFinal)
	{
		enableStage(GFX_RENDERSCALE);
		setStageInput(GFX_RENDERSCALE, 0, action->bufferFinal->surface, SBUF_0, 0);
		setStageOutput(GFX_RENDERSCALE, finalOutput, 0);
		finalOutput = action->bufferFinal->surface;
	}

	if (gfxDoingPostprocessing())
	{
		switch (bloomQuality)
		{
		case GBLOOM_OFF:
		case GBLOOM_LOW_BLOOMWHITE:
		case GBLOOM_MED_BLOOM_SMALLHDR:
			setStageOutput(GFX_ALPHA, action->bufferLDR->surface, 0);
			break;

		case GBLOOM_HIGH_BLOOM_FULLHDR:
			setStageOutput(GFX_ALPHA, action->bufferLDRandHDR->surface, 0);
			break;

		case GBLOOM_MAX_BLOOM_DEFERRED:
			enableStage(GFX_DEFERRED_COMBINE);
			setStageInput(GFX_DEFERRED_COMBINE, 0, action->bufferMRT->surface, SBUF_0, 0);
			setStageInput(GFX_DEFERRED_COMBINE, 1, action->bufferMRT->surface, SBUF_1, 0);
			setStageInput(GFX_DEFERRED_COMBINE, 2, action->bufferMRT->surface, SBUF_2, 0);
			setStageInput(GFX_DEFERRED_COMBINE, 3, action->bufferMRT->surface, SBUF_3, 0);
			setStageInput(GFX_DEFERRED_COMBINE, 4, action->bufferMRT->surface, SBUF_DEPTH, 0);
			setStageOutput(GFX_DEFERRED_COMBINE, action->bufferLDR->surface, 0);
			setStageOutput(GFX_ALPHA, action->bufferLDR->surface, 0);
			break;
		}
	}

	copyStageOutput(GFX_ALPHA, GFX_ALPHA_PREDOF);
	copyStageOutput(GFX_ALPHA, GFX_NONDEFERRED_POST_OUTLINING);
	copyStageOutput(GFX_ALPHA, GFX_SKY);


	if (gfxDoingPostprocessing())
	{
		if (bloomQuality == GBLOOM_MAX_BLOOM_DEFERRED)
		{
			setStageOutput(GFX_OPAQUE_AFTER_ZPRE, action->bufferMRT->surface, MASK_SBUF_ALL);
			copyStageOutput(GFX_OPAQUE_AFTER_ZPRE, GFX_OPAQUE_ONEPASS);
		}
		else
		{
			copyStageOutput(GFX_ALPHA, GFX_OPAQUE_ONEPASS);
			copyStageOutput(GFX_ALPHA, GFX_OPAQUE_AFTER_ZPRE);
		}
	}

	if (!gfxGetStageOutput(GFX_OPAQUE_ONEPASS))
		setStageOutput(GFX_OPAQUE_ONEPASS, finalOutput, 0);
	if (!gfxGetStageOutput(GFX_OPAQUE_AFTER_ZPRE))
		setStageOutput(GFX_OPAQUE_AFTER_ZPRE, finalOutput, 0);

	if (!action->opaqueDepth.surface)
	{
		action->opaqueDepth.surface = gfxGetStageOutput(GFX_OPAQUE_AFTER_ZPRE);
		action->opaqueDepth.buffer = SBUF_DEPTH;
		action->opaqueDepth.snapshot_idx = depthSnapshotIndex;
	}

	if (gfxDoingPostprocessing() && gfx_state.settings.lensflare_quality > 1)
	{
		enableStage(GFX_LENSFLARE_ZO);
		setStageInput(GFX_LENSFLARE_ZO, 0, action->opaqueDepth.surface, action->opaqueDepth.buffer, depthSnapshotIndex);
		//setStageInput(GFX_LENSFLARE_ZO, gfxGetStageOutput(GFX_OPAQUE_AFTER_ZPRE));

#if 0
		setStageOutput(GFX_LENSFLARE_ZO, action->lens_zo_64->surface, 0);
#else
		setStageOutput(GFX_LENSFLARE_ZO, action->lens_zo_16->surface, 0);
#endif
	}

	if (!gfxGetStageOutput(GFX_SKY))
		setStageOutput(GFX_SKY, finalOutput, 0);

	if (!gfxGetStageOutput(GFX_NONDEFERRED_POST_OUTLINING))
		setStageOutput(GFX_NONDEFERRED_POST_OUTLINING, finalOutput, 0);

	if (!gfxGetStageOutput(GFX_ALPHA_PREDOF))
		setStageOutput(GFX_ALPHA_PREDOF, finalOutput, 0);

	if (!gfxGetStageOutput(GFX_ALPHA))
		setStageOutput(GFX_ALPHA, finalOutput, 0);

	if (gdraw->do_zprepass)
	{
		// use the output buffer depth for the z-prepass
		enableStage(GFX_ZPREPASS_EARLY);
		setStageOutput(GFX_ZPREPASS_EARLY, gfxGetStageOutput(GFX_OPAQUE_ONEPASS), MASK_SBUF_DEPTH);
		if (gdraw->do_outlining)
		{
			// Also do late zprepass stage
			enableStage(GFX_ZPREPASS_LATE);
			setStageOutput(GFX_ZPREPASS_LATE, gfxGetStageOutput(GFX_OPAQUE_AFTER_ZPRE), MASK_SBUF_DEPTH);
		}
	}

	if (rdrDrawListHasAuxPassObjects(gdraw->draw_list))
	{
		enableStage(GFX_AUX_VISUAL_PASS);

		setStageOutput(GFX_AUX_VISUAL_PASS, action->bufferHighlight->surface, 0);
	}

	if (gdraw->do_shadow_buffer && action->opaqueDepth.surface)
	{
		enableStage(GFX_SHADOW_BUFFER);
		setStageOutput(GFX_SHADOW_BUFFER, action->bufferDeferredShadows->surface, 0);
		setStageInput(GFX_SHADOW_BUFFER, 0, action->opaqueDepth.surface, action->opaqueDepth.buffer, depthSnapshotIndex);
		setStageTemp(GFX_SHADOW_BUFFER, 0, action->bufferTempFullSize->surface);
	}

	// depth of field
	

	if (gfx_state.settings.scattering && gfxDoingPostprocessing())
	{
		enableStage(GFX_CALCULATE_SCATTERING);

		setStageOutput(GFX_CALCULATE_SCATTERING, action->bufferScatter->surface, 0);

		setStageInput(GFX_CALCULATE_SCATTERING, 0, action->opaqueDepth.surface, SBUF_DEPTH, depthSnapshotIndex);


		enableStage(GFX_APPLY_SCATTERING);

		setStageOutput(GFX_APPLY_SCATTERING, gfxGetStageOutput(GFX_OPAQUE_AFTER_ZPRE), 0);
		setStageInput(GFX_APPLY_SCATTERING, 0, gfxGetStageOutput(GFX_CALCULATE_SCATTERING), SBUF_0, 0);
		setStageTemp(GFX_APPLY_SCATTERING, 0, action->bufferTempFullSize->surface);
	}


	action->snapshotLDR.surface = gfxGetStageOutput(GFX_SKY);
	action->snapshotLDR.buffer = SBUF_0;
    action->snapshotLDR.snapshot_idx = SCREENCOLOR_SNAPSHOT_IDX;

	if (action->bTXAAEnable) {
		if (action->nTXAABufferFlip) {
			finalSnapshotIndex = SCREENCOLOR_TXAA_SNAPSHOT_LAST_FRAME_IDX;
		} else {
			finalSnapshotIndex = SCREENCOLOR_TXAA_SNAPSHOT_IDX;
		}
	} else {
		finalSnapshotIndex = SCREENCOLOR_SNAPSHOT_IDX;
	}

	if (bloomQuality == GBLOOM_HIGH_BLOOM_FULLHDR)
	{
		action->snapshotHDR.surface = gfxGetStageOutput(GFX_SKY);
		action->snapshotHDR.buffer = SBUF_1;
		action->snapshotHDR.snapshot_idx = SCREENCOLOR_SNAPSHOT_IDX;
	}

	if (action->opaqueDepth.surface)
	{
		// outlining
		if (gdraw->do_outlining)
		{
			enableStage(GFX_OUTLINING_EARLY);

			setStageInput(GFX_OUTLINING_EARLY, 1, action->opaqueDepth.surface, action->opaqueDepth.buffer, 
				depthSnapshotIndex);

			if (action->opaqueDepth.surface)
			{
				RdrSurface * color_normal_buf = gfxGetStageOutput(GFX_ALPHA);
				enableStage(GFX_OUTLINING_LATE);
				setStageOutput(GFX_OUTLINING_EARLY, action->bufferOutline->surface, 0);

				setStageInput(GFX_OUTLINING_LATE, 0, gfxGetStageOutput(GFX_OUTLINING_EARLY), SBUF_0, 0);
				//only do the normal based outlines if not MSAA
				if (gfx_state.antialiasingQuality <= 1)
				{
					setStageInput(GFX_OUTLINING_LATE, 1, color_normal_buf, SBUF_DEPTH, 0);
					setStageInput(GFX_OUTLINING_LATE, 2, color_normal_buf, SBUF_0, 0);
				}
				setStageOutput(GFX_OUTLINING_LATE, gfxGetStageOutput(GFX_SKY), MASK_SBUF_0|MASK_SBUF_DEPTH); // write to LDR only
			}
			else
			{
				setStageOutput(GFX_OUTLINING_EARLY, gfxGetStageOutput(GFX_SKY), 0);
				setStageInput(GFX_OUTLINING_EARLY, 0, SAFE_MEMBER(action->bufferMRT, surface), SBUF_DEFERRED_NORMAL1616, 0);
			}
		}

		// depth of field
		if (gfxFeatureEnabled(GFEATURE_DOF) && gfxDoingPostprocessing())
		{
			enableStage(GFX_DEPTH_OF_FIELD);
#if _PS3
			setStageOutput(GFX_DEPTH_OF_FIELD, gfxGetStageOutput(GFX_ALPHA), 0);
#elif _XBOX
			setStageOutput(GFX_DEPTH_OF_FIELD, gfxGetStageOutput(GFX_ALPHA), 0);
#else
			setStageOutput(GFX_DEPTH_OF_FIELD, gfxGetStageOutput(GFX_ALPHA), MASK_SBUF_0|MASK_SBUF_DEPTH); // write to LDR only
#endif

			setStageInput(GFX_DEPTH_OF_FIELD, 0, action->snapshotLDR.surface, action->snapshotLDR.buffer, finalSnapshotIndex);
			setStageInput(GFX_DEPTH_OF_FIELD, 1, action->snapshotHDR.surface, action->snapshotHDR.buffer, action->snapshotHDR.snapshot_idx);
			setStageInput(GFX_DEPTH_OF_FIELD, 2, action->opaqueDepth.surface, action->opaqueDepth.buffer, depthSnapshotIndex);

			setStageTemp(GFX_DEPTH_OF_FIELD, 0, action->bloom[0]->surface);
			setStageTemp(GFX_DEPTH_OF_FIELD, 1, action->bloom[1]->surface);
			if (gfx_state.settings.highQualityDOF)
				setStageTemp(GFX_DEPTH_OF_FIELD, 2, action->bufferTempFullSize->surface);
		}

		if( !rdrDrawListSortBucketIsEmpty( gdraw->draw_list, RSBT_ALPHA_LOW_RES_ALPHA ) ||
			!rdrDrawListSortBucketIsEmpty( gdraw->draw_list, RSBT_ALPHA_LOW_RES_ADDITIVE ) ||
			!rdrDrawListSortBucketIsEmpty( gdraw->draw_list, RSBT_ALPHA_LOW_RES_SUBTRACTIVE ))
		{
			enableStage(GFX_ALPHA_LOW_RES);

			copyStageOutput(GFX_ALPHA, GFX_ALPHA_LOW_RES);
			setStageInput(GFX_ALPHA_LOW_RES, 0, action->opaqueDepth.surface, action->opaqueDepth.buffer, action->opaqueDepth.snapshot_idx);

			setStageTemp(GFX_ALPHA_LOW_RES, 0, action->low_res_alpha_scratch->surface);
			setStageTemp(GFX_ALPHA_LOW_RES, 1, action->low_res_alpha_edge->surface);
		}

		if (action->cameraView->in_water_this_frame && gfxFeatureEnabled(GFEATURE_WATER))
		{
			if (!gfxIsStageEnabled(GFX_RENDERSCALE))
				finalOutput = SAFE_MEMBER(action->bufferTempFullSize, surface);

			if (gfxFeatureEnabled(GFEATURE_DOF) && gfxDoingPostprocessing())
			{
				enableStage(GFX_WATER_DOF);
				setStageInput(GFX_WATER_DOF, 0, finalOutput, SBUF_0, SCREENCOLOR_SNAPSHOT_IDX);
				setStageInput(GFX_WATER_DOF, 2, action->opaqueDepth.surface, action->opaqueDepth.buffer, depthSnapshotIndex);
				setStageTemp(GFX_WATER_DOF, 0, action->bloom[0]->surface);
				setStageTemp(GFX_WATER_DOF, 1, action->bloom[1]->surface);

				setStageOutput(GFX_WATER_DOF, finalOutput, 0);
			}

			enableStage(GFX_WATER);
			setStageInput(GFX_WATER, 0, finalOutput, SBUF_0, SCREENCOLOR_SNAPSHOT_IDX);
			setStageInput(GFX_WATER, 1, action->opaqueDepth.surface, action->opaqueDepth.buffer, depthSnapshotIndex);
			setStageOutput(GFX_WATER, finalOutput, 0);

			enableStage(GFX_RENDERSCALE);
			setStageInput(GFX_RENDERSCALE, 0, finalOutput, SBUF_0, 0);
			setStageOutput(GFX_RENDERSCALE, action->outputSurface, 0);
		}
	}

	if (gfxDoingPostprocessing())
	{
		if (bloomQuality == GBLOOM_MAX_BLOOM_DEFERRED)
		{
			enableStage(GFX_SHRINK_HDR);
			setStageInput(GFX_SHRINK_HDR, 0, action->bufferMRT->surface, SBUF_0,  0);
			setStageOutput(GFX_SHRINK_HDR, action->bloom[1]->surface, 0);

			enableStage(GFX_SHRINK_HDR);
			setStageInput(GFX_SHRINK_HDR, 0, action->bufferMRT->surface, SBUF_0,  0);
			setStageOutput(GFX_SHRINK_HDR, action->bloom[1]->surface, 0);
		}
		else
		if (bloomQuality == GBLOOM_HIGH_BLOOM_FULLHDR)
		{
			enableStage(GFX_SHRINK_HDR);
			setStageInput(GFX_SHRINK_HDR, 0, action->bufferLDRandHDR->surface, SBUF_1, 0);
			setStageOutput(GFX_SHRINK_HDR, action->bloom[1]->surface, 0);
		}
		else if (bloomQuality == GBLOOM_MED_BLOOM_SMALLHDR)
		{
			enableStage(GFX_SEPARATE_HDR_PASS);
			setStageInput(GFX_SEPARATE_HDR_PASS, 0, action->bufferLDR->surface, SBUF_0, SCREENCOLOR_NOSKY_SNAPSHOT_IDX);
			setStageInput(GFX_SEPARATE_HDR_PASS, 1, action->opaqueDepth.surface, action->opaqueDepth.buffer, depthSnapshotIndex);
			setStageOutput(GFX_SEPARATE_HDR_PASS, action->bufferHDR->surface, 0);

			enableStage(GFX_SHRINK_HDR);
			setStageInput(GFX_SHRINK_HDR, 0, action->bufferHDR->surface, SBUF_0, 0);
			setStageOutput(GFX_SHRINK_HDR, action->bloom[1]->surface, 0);
		}
		else if (bloomQuality == GBLOOM_LOW_BLOOMWHITE)
		{
			enableStage(GFX_SHRINK_HDR);
			setStageInput(GFX_SHRINK_HDR, 0, action->bufferLDR->surface, SBUF_0,  0);
			setStageOutput(GFX_SHRINK_HDR, action->bloom[1]->surface, 0);
		}

		if (bloomQuality > GBLOOM_OFF)
		{
			enableStage(GFX_BLOOM);
			setStageInput(GFX_BLOOM, 0, action->bloom[1]->surface, SBUF_0, 0);
			setStageOutput(GFX_BLOOM, action->bloom[0]->surface, 0);
		}

		enableStage(GFX_TONEMAP);
		setStageInput(GFX_TONEMAP, 0, gfxGetStageOutput(GFX_ALPHA), SBUF_0, 0);
		setStageInput(GFX_TONEMAP, 1, gfxGetStageOutput(GFX_BLOOM), SBUF_0, 0);
		setStageOutput(GFX_TONEMAP, finalOutput, 0);
	}

	if (gfx_state.debug.show_stages)
	{
		int i;
		eaClear(&gfx_state.debug.show_stages_text);
		eaiClear(&gfx_state.debug.show_stages_value);
		for (i=0; i<GFX_NUM_STAGES; i++) 
		{
			eaPush(&gfx_state.debug.show_stages_text, StaticDefineIntRevLookup(GfxStagesEnum, i));
			eaiPush(&gfx_state.debug.show_stages_value, gfxIsStageEnabled(i));
		}
	}
}

static void gfxGetFogMatrix(Mat4 fog_matrix)
{
	const BlendedSkyInfo *sky_info;
	if (gfx_state.volumeFog)
	{
		sky_info = gfxSkyGetVisibleSky(gfx_state.currentCameraView->sky_data);

		copyMat4(unitmat, fog_matrix);
		negateVec3(sky_info->fogValues.volumeFogPos, fog_matrix[3]);
		fog_matrix[0][0] = 1.0f / sky_info->fogValues.volumeFogScale[0];
		fog_matrix[1][1] = 1.0f / sky_info->fogValues.volumeFogScale[1];
		fog_matrix[2][2] = 1.0f / sky_info->fogValues.volumeFogScale[2];
		fog_matrix[3][0] *= fog_matrix[0][0];
		fog_matrix[3][1] *= fog_matrix[1][1];
		fog_matrix[3][2] *= fog_matrix[2][2];
	}
	else
	{
		copyMat4(unitmat, fog_matrix);
	}
}

static void copyCameraToSurface(GfxCameraView *camera, RdrSurface *surface)
{
	Mat4 fogmat;
	gfxGetFogMatrix(fogmat);
	rdrSurfaceUpdateMatricesFromFrustumEX(surface, camera->projection_matrix,camera->far_projection_matrix,camera->sky_projection_matrix,
										fogmat, &camera->frustum,
										gfx_state.currentAction->renderViewport[2],
										gfx_state.currentAction->renderViewport[0],
										gfx_state.currentAction->renderViewport[3],
										gfx_state.currentAction->renderViewport[1],
										camera->frustum.cammat[3]);
}

static void gfxUFSetSurface(void)
{
	PERFINFO_AUTO_START_FUNC();

	if (gfx_state.currentAction->bufferDeferredShadows)
	{
		if (gfx_state.currentAction->bufferSSAOPrepass)
		{
			copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferSSAOPrepass->surface);
		}
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferDeferredShadows->surface);
	}

	if (gfx_state.currentAction->bufferMRT)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferMRT->surface);

	if (gfx_state.currentAction->bufferOutline)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferOutline->surface);

	if (gfx_state.currentAction->bufferOutlinePCA)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferOutlinePCA->surface);

	if (gfx_state.currentAction->bufferHighlight)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferHighlight->surface);

	if (gfx_state.currentAction->bufferScatter)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferScatter->surface);

	if (gfx_state.currentAction->bufferLDR)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferLDR->surface);

	if (gfx_state.currentAction->bufferHDR)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferHDR->surface);

	if (gfx_state.currentAction->bufferLDRandHDR)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferLDRandHDR->surface);

	if (gfx_state.currentAction->bufferTempFullSize)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferTempFullSize->surface);

	if (gfx_state.currentAction->low_res_alpha_scratch)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->low_res_alpha_scratch->surface);
	
	if (gfx_state.currentAction->low_res_alpha_edge)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->low_res_alpha_edge->surface);

	if (gfx_state.currentAction->bufferFinal)
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bufferFinal->surface);

	if (gfx_state.currentAction->bloom[0])
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bloom[0]->surface);

	if (gfx_state.currentAction->bloom[1])
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->bloom[1]->surface);

	copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentAction->outputSurface);

	PERFINFO_AUTO_STOP();
}

#define ONE_QUARTER_F 0.25f
#define ONE_SIXTEENTH_F 0.0625f
#define ONE_SIXTYFOURTH_F 0.015625f

static void gfxUFSetupHDRSurfaces(void)
{
	GfxRenderAction *action = gfx_state.currentAction;
	GfxGlobalDrawParams *gdraw = &action->gdraw;

#if _PS3
	bool bForceFloat = false;
#elif _XBOX
	bool bForceFloat = true; // Doesn't seem to affect performance at all
#else
	bool bForceFloat = gfx_state.debug.postprocess_force_float;
#endif
	RdrSurfaceBufferType luminance_buffer_type = SBT_RG_FLOAT;
	RdrSurfaceParams surfaceparams = {0};
	surfaceparams.desired_multisample_level = 1;
	surfaceparams.required_multisample_level = 1;
	surfaceparams.flags = 0;

#if _PS3
    luminance_buffer_type = SBT_RGBA;
#else
	if (gfx_state.debug.postprocess_rgba_buffer & 16)
		luminance_buffer_type = SBT_RGBA;
#endif

	if (gfxDoingPostprocessing() || gfxFeatureEnabled(GFEATURE_WATER) && (gfx_state.settings.features_supported & GFEATURE_POSTPROCESSING))
	{
		// bloom
		rdrSurfaceParamSetSizeSafe(&surfaceparams, round(action->renderSize[0] * ONE_QUARTER_F),
			round(action->renderSize[1] * ONE_QUARTER_F));
		if (!bForceFloat || (gfx_state.debug.postprocess_rgba_buffer & 1)) {
			surfaceparams.buffer_types[0] = SBT_RGBA;
		} else {
			surfaceparams.buffer_types[0] = SBT_RGBA_FIXED;
		}
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] &= ~(RTF_MAG_POINT|RTF_MIN_POINT); // linear filter

		surfaceparams.name = "Bloom 0";
		action->bloom[0] = gfxGetTempSurface(&surfaceparams);

		surfaceparams.name = "Bloom 1";
		action->bloom[1] = gfxGetTempSurface(&surfaceparams);


		rdrSurfaceParamSetSizeSafe(&surfaceparams, round(action->renderSize[0] * ONE_SIXTEENTH_F),
			round(action->renderSize[1] * ONE_SIXTEENTH_F));
		if (!bForceFloat || (gfx_state.debug.postprocess_rgba_buffer & 2)) {
			surfaceparams.buffer_types[0] = SBT_RGBA;
		} else {
			surfaceparams.buffer_types[0] = SBT_RGBA_FIXED;
		}
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] &= ~(RTF_MAG_POINT|RTF_MIN_POINT); // linear filter

		surfaceparams.name = "Bloom med 0";
		action->bloom_med[0] = gfxGetTempSurface(&surfaceparams);

		surfaceparams.name = "Bloom med 1";
		action->bloom_med[1] = gfxGetTempSurface(&surfaceparams);


		rdrSurfaceParamSetSizeSafe(&surfaceparams, round(action->renderSize[0] * ONE_SIXTYFOURTH_F),
			round(action->renderSize[1] * ONE_SIXTYFOURTH_F));
		if (!bForceFloat || (gfx_state.debug.postprocess_rgba_buffer & 4)) {
			surfaceparams.buffer_types[0] = SBT_RGBA;
		} else {
			surfaceparams.buffer_types[0] = SBT_RGBA_FIXED;
		}
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] &= ~(RTF_MAG_POINT|RTF_MIN_POINT); // linear filter

		surfaceparams.name = "Bloom low 0";
		action->bloom_low[0] = gfxGetTempSurface(&surfaceparams);

		surfaceparams.name = "Bloom low 1";
		action->bloom_low[1] = gfxGetTempSurface(&surfaceparams);


#if USE_GLARE
		// glare
		rdrSurfaceParamSetSizeSafe(&surfaceparams, round(action->renderSize[0] * ONE_QUARTER_F),
			round(action->renderSize[1] * ONE_QUARTER_F));
		surfaceparams.buffer_types[0] = SBT_RGBA;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] |= RTF_BLACK_BORDER;

		surfaceparams.name = "Glare";
		action->glare = gfxGetTempSurface(&surfaceparams);
#endif

		// luminance
		rdrSurfaceParamSetSizeSafe(&surfaceparams, 64, 64);
		surfaceparams.buffer_types[0] = luminance_buffer_type;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.name = "Luminance 64x64";
		action->lum64 = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 16, 16);
		surfaceparams.buffer_types[0] = luminance_buffer_type;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.name = "Luminance 16x16";
		action->lum16 = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 4, 4);
		surfaceparams.buffer_types[0] = luminance_buffer_type;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.name = "Luminance 4x4";
		action->lum4 = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 1, 1);
		surfaceparams.buffer_types[0] = luminance_buffer_type;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.name = "Luminance 1x1";
		action->lum1 = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 1, 1);
		surfaceparams.buffer_types[0] = SBT_RGBA;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.name = "Luminance Measurement 1x1";
		action->lum_measure = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 256, 1);
		if (gfx_state.debug.postprocess_rgba_buffer & 32)
			surfaceparams.buffer_types[0] = SBT_RGBA;
		else
			surfaceparams.buffer_types[0] = SBT_RGBA_FLOAT;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] &= ~(RTF_MAG_POINT|RTF_MIN_POINT);
		surfaceparams.name = "Blueshift LUT";
		action->blueshift_lut = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 512, 1);
		surfaceparams.buffer_types[0] = SBT_FLOAT;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] &= ~(RTF_MAG_POINT|RTF_MIN_POINT);
		surfaceparams.name = "Tone Curve LUT";
		action->tonecurve_lut = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 256, 1);
		if (gfx_state.debug.postprocess_rgba_buffer & 64)
			surfaceparams.buffer_types[0] = SBT_RGBA;
		else
			surfaceparams.buffer_types[0] = SBT_RGBA_FLOAT;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] &= ~(RTF_MAG_POINT|RTF_MIN_POINT);
		surfaceparams.name = "Color Curve LUT";
		action->colorcurve_lut = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 256, 1);
		if (gfx_state.debug.postprocess_rgba_buffer & 128)
			surfaceparams.buffer_types[0] = SBT_RGBA;
		else
			surfaceparams.buffer_types[0] = SBT_RGBA_FLOAT;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] &= ~(RTF_MAG_POINT|RTF_MIN_POINT);
		surfaceparams.name = "Intensity Tint and Saturation LUT";
		action->intensitytint_lut = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 256, 16);
		surfaceparams.buffer_types[0] = SBT_RGBA;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] &= ~(RTF_MAG_POINT|RTF_MIN_POINT);
		surfaceparams.name = "Postprocessing All LUT";
		action->postprocess_all_lut = gfxGetTempSurface(&surfaceparams);
	}
}

static void gfxUFSetupZOcclusionSurface(void)
{
	GfxRenderAction *action = gfx_state.currentAction;
	GfxGlobalDrawParams *gdraw = &action->gdraw;

#if _PS3
	bool bForceFloat = false;
#elif _XBOX
	bool bForceFloat = true; // Doesn't seem to affect performance at all
#else
	bool bForceFloat = gfx_state.debug.postprocess_force_float;
#endif

	RdrSurfaceParams surfaceparams = {0};
	surfaceparams.desired_multisample_level = 1;
	surfaceparams.required_multisample_level = 1;
	surfaceparams.flags = 0;

	if (gfxDoingPostprocessing() && gfx_state.settings.lensflare_quality > 1)
	{
		// z occlusion sampling
#if 0 // using 16x16 for STO demo, will do higher quality later
		rdrSurfaceParamSetSizeSafe(&surfaceparams, 64, 64);
		surfaceparams.buffer_types[0] = SBT_RGBA;
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.name = "Lens Z Occlusion 64x64";
		action->lens_zo_64 = gfxGetTempSurface(&surfaceparams);
#endif

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 64, 16);
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.name = "Lens Z Occlusion 64x16";
		action->lens_zo_16 = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 16, 4);
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.name = "Lens Z Occlusion 16x4";
		action->lens_zo_4 = gfxGetTempSurface(&surfaceparams);

		rdrSurfaceParamSetSizeSafe(&surfaceparams, 4, 1);
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] &= ~(RTF_MAG_POINT|RTF_MIN_POINT);
		surfaceparams.name = "Lens Z Occlusion 4x1";
		action->lens_zo_1 = gfxGetTempSurface(&surfaceparams);

		rdrSetDrawListSurfaceTexHandleFixup(action->gdraw.draw_list,
			DRAWLIST_DEFINED_TEX__LENSFLARE_ZO_DEPTH_TEX, 
			rdrSurfaceToTexHandle(action->lens_zo_1->surface, SBUF_0)); 
	}
}

static int lowResFactor = 4;
AUTO_CMD_INT(lowResFactor, lowResFactor);

static void gfxUFSetupSurfaces(void)
{
	GfxRenderAction *action = gfx_state.currentAction;
	GfxGlobalDrawParams *gdraw = &action->gdraw;
	int width, height;

	PERFINFO_AUTO_START_FUNC();

	width = action->outputSurface->width_nonthread;
	height = action->outputSurface->height_nonthread;

	// Setup Surfaces

	if (gfxDoingPostprocessing() && gfx_state.settings.lensflare_quality > 1)
	{
		gfxUFSetupZOcclusionSurface();
	}

	if (gdraw->do_outlining)
	{
		RdrSurfaceParams surfaceparams = {0};
		surfaceparams.name = "Outline results";
		rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0], action->renderSize[1]);
		surfaceparams.desired_multisample_level = 1;
		surfaceparams.required_multisample_level = 1;

//		surfaceparams.depth_bits = 24;
		surfaceparams.flags = 0;
// #ifdef _XBOX
//		surfaceparams.flags |= SF_DEPTH_TEXTURE;
// #endif

		surfaceparams.buffer_types[0] = SBT_RGBA; // Really only need 8-bit...
		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
			surfaceparams.buffer_types[0] |= SBT_SRGB;

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

		action->bufferOutline = gfxGetTempSurface(&surfaceparams);

		if (!gdraw->do_aux_visual_pass && action->bufferOutline)
		{
			rdrSetDrawListSurfaceTexHandleFixup(action->gdraw.draw_list, DRAWLIST_DEFINED_TEX__OUTLINE, 
				rdrSurfaceToTexHandleEx(action->bufferOutline->surface, SBUF_0, 0, 0, false)); 
		}
	}

	if (gfx_state.settings.scattering && gfxDoingPostprocessing())
	{
		RdrSurfaceParams surfaceparams = {0};
		int surface_res_factor = gfx_state.settings.scattering == 1 ? 4 : 2;
		if (gfx_state.settings.scattering == 3)
			surface_res_factor = 8;
		else
		if (gfx_state.settings.scattering == 4)
			surface_res_factor = 16;
		surfaceparams.name = "Scatter strength";
		rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0] / surface_res_factor,
			action->renderSize[1] / surface_res_factor);
		surfaceparams.desired_multisample_level = 1;
		surfaceparams.required_multisample_level = 1;

		surfaceparams.buffer_types[0] = SBT_RGBA;
		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
			surfaceparams.buffer_types[0] |= SBT_SRGB;

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

		action->bufferScatter = gfxGetTempSurface(&surfaceparams);
	}

	if (gdraw->do_aux_visual_pass)
	{
		RdrSurfaceParams surfaceparams = {0};
		surfaceparams.name = "Target highlight";
		rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0] / 4,
			action->renderSize[1] / 4);
		surfaceparams.desired_multisample_level = 1;
		surfaceparams.required_multisample_level = 1;
		if (gfxStereoscopicActive())
			surfaceparams.depth_bits = 24;

		surfaceparams.buffer_types[0] = SBT_RGBA;
		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
			surfaceparams.buffer_types[0] |= SBT_SRGB;

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

		action->bufferHighlight = gfxGetTempSurface(&surfaceparams);

		rdrSetDrawListSurfaceTexHandleFixup(action->gdraw.draw_list, DRAWLIST_DEFINED_TEX__OUTLINE, 
			rdrSurfaceToTexHandleEx(action->bufferHighlight->surface, SBUF_0, 0, 0, false)); 
	}

	if (gfxDoingPostprocessing() && gfx_state.settings.bloomQuality == GBLOOM_MAX_BLOOM_DEFERRED)
	{
		// prototype deferred shading
		RdrSurfaceBufferType surfaceFormat = SBT_RGBA;
		RdrSurfaceParams surfaceparams = {0};
		surfaceparams.name = "LDR deferred";
		rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0], action->renderSize[1]);
		surfaceparams.desired_multisample_level = gfx_state.antialiasingQuality;
		surfaceparams.required_multisample_level = 1;

		// Set up parameters for HDR surface
		surfaceparams.depth_bits = 24;
		surfaceparams.flags = SF_MRT4;

#if !PLATFORM_CONSOLE
		if (surfaceparams.desired_multisample_level <= 1 || rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_DEPTH_TEXTURE_MSAA))
			surfaceparams.flags |= SF_DEPTH_TEXTURE; //we can only use this if the hardware supports it otherwise the render thread will silently turn off AA
#else
		surfaceparams.flags |= SF_DEPTH_TEXTURE;
#endif

		/* TODO check rdr_state.supportHighPrecisionDisplays?
		// if (rdr_state.supportHighPrecisionDisplays >= 2 && rdr_state.supportHighPrecisionDisplays <= 9)
		if (rdrSupportsSurfaceType(gfx_state.currentDevice->rdr_device, SBT_RGBA_FLOAT))
			surfaceFormat = SBT_RGBA_FLOAT;
		else
		if (rdrSupportsSurfaceType(gfx_state.currentDevice->rdr_device, SBT_RGBA10))
			surfaceFormat = SBT_RGBA10;
		*/
		surfaceparams.buffer_types[0] = surfaceFormat;
		surfaceparams.buffer_types[1] = surfaceFormat;
		surfaceparams.buffer_types[2] = surfaceFormat;
		surfaceparams.buffer_types[3] = surfaceFormat;

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

		action->bufferMRT = gfxGetTempSurface(&surfaceparams);
	}

	if (gfxDoingPostprocessing() && gfx_state.settings.bloomQuality == GBLOOM_HIGH_BLOOM_FULLHDR)
	{
		RdrSurfaceParams surfaceparams = {0};
		surfaceparams.name = "LDR and HDR Color (MRT2)";
		rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0], action->renderSize[1]);
		surfaceparams.desired_multisample_level = gfx_state.antialiasingQuality;
		surfaceparams.required_multisample_level = 1;

		// Set up parameters for HDR surface
		surfaceparams.depth_bits = 24;
		surfaceparams.flags = SF_MRT2;

#if !PLATFORM_CONSOLE
		if (surfaceparams.desired_multisample_level <= 1 || rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_DEPTH_TEXTURE_MSAA))
			surfaceparams.flags |= SF_DEPTH_TEXTURE; //we can only use this if the hardware supports it otherwise the render thread will silently turn off AA
#else
		surfaceparams.flags |= SF_DEPTH_TEXTURE;
#endif

		//if (rdrSupportsSurfaceType(gfx_state.currentDevice->rdr_device, SBT_RGBA10))
		//{
		//	surfaceparams.buffer_types[0] = SBT_RGBA10;
		//	surfaceparams.buffer_types[1] = SBT_RGBA10;
		//}
		//else
		{
			surfaceparams.buffer_types[0] = SBT_RGBA;
			surfaceparams.buffer_types[1] = SBT_RGBA;
		}

		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
		{
			surfaceparams.buffer_types[0] |= SBT_SRGB;
			surfaceparams.buffer_types[1] |= SBT_SRGB;
		}

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

		action->bufferLDRandHDR = gfxGetTempSurface(&surfaceparams);
	}

	if (gfxDoingPostprocessing() && (gfx_state.settings.bloomQuality < GBLOOM_HIGH_BLOOM_FULLHDR || gfx_state.settings.bloomQuality == GBLOOM_MAX_BLOOM_DEFERRED))
	{
		RdrSurfaceParams surfaceparams = {0};
		surfaceparams.name = "LDR Color";
		rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0], action->renderSize[1]);
		surfaceparams.desired_multisample_level = gfx_state.antialiasingQuality;
		surfaceparams.required_multisample_level = 1;

		if (gfx_state.antialiasingMode == GAA_TXAA && !action->is_offscreen)
			action->bTXAAEnable = true;

		surfaceparams.depth_bits = 24;
		surfaceparams.flags = 0;
		if ((gfx_state.settings.bloomQuality > GBLOOM_LOW_BLOOMWHITE || 
			gfxFeatureEnabled(GFEATURE_DOF|GFEATURE_OUTLINING) ||
			gdraw->do_shadow_buffer ||
			gdraw->do_ssao ||
			rdrDrawListNeedsScreenDepthGrab(gdraw->draw_list))
			&& rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_DEPTH_TEXTURE))
		{
#if !PLATFORM_CONSOLE
			if (surfaceparams.desired_multisample_level <= 1 || rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_DEPTH_TEXTURE_MSAA))
				surfaceparams.flags |= SF_DEPTH_TEXTURE; //we can only use this if the hardware supports it otherwise the render thread will silently turn off AA
#else
			surfaceparams.flags |= SF_DEPTH_TEXTURE;
#endif
		} else {
			surfaceparams.ignoreFlags |= SF_DEPTH_TEXTURE;
		}

		surfaceparams.buffer_types[0] = SBT_RGBA;
		if (rdr_state.supportHighPrecisionDisplays >= 2 && rdr_state.supportHighPrecisionDisplays <= 9)
		{
			RdrSurfaceBufferType sbt = SBT_RGBA;
			RdrSurfaceBufferType displayFormatSBTs[7] = {SBT_RGBA, SBT_RGBA, 
				SBT_RGBA | SBT_SRGB, SBT_RGBA | SBT_SRGB, SBT_RGBA10, SBT_RGBA10, SBT_RGBA_FLOAT};
			int displayFormat = rdr_state.supportHighPrecisionDisplays - 2;

			if (rdrSupportsSurfaceType(gfx_state.currentDevice->rdr_device, displayFormatSBTs[displayFormat]))
			{
				surfaceparams.buffer_types[0] = displayFormatSBTs[displayFormat];
			}
		}

		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
			surfaceparams.buffer_types[0] |= SBT_SRGB;

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

		action->bufferLDR = gfxGetTempSurface(&surfaceparams);
	}

	if (gfx_state.currentAction->gdraw.do_shadow_buffer)
	{
		RdrSurfaceParams surfaceparams = {0};
		surfaceparams.name = "Shadow Buffer";
		rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0], action->renderSize[1]);
		surfaceparams.desired_multisample_level = 1;
		surfaceparams.required_multisample_level = 1;

		// Set up parameters for shadow buffer surface
		surfaceparams.flags = 0;
		surfaceparams.buffer_types[0] = SBT_RGBA;

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

		action->bufferDeferredShadows = gfxGetTempSurface(&surfaceparams);

		if (gfx_state.currentAction->gdraw.do_ssao)
		{
			surfaceparams.name = "SSAO_Prepass";
			rdrSurfaceParamSetSizeSafe(&surfaceparams, ceil(action->renderSize[0] * ONE_QUARTER_F),
				ceil(action->renderSize[1] * ONE_QUARTER_F));
			//surfaceparams.depth_bits = 24;
			surfaceparams.desired_multisample_level = 1;
			surfaceparams.required_multisample_level = 1;
			surfaceparams.flags = 0;// SF_DEPTHONLY | SF_DEPTH_TEXTURE | SF_SHADOW_MAP;
			surfaceparams.buffer_types[0] = SBT_RGBA;

			rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

			action->bufferSSAOPrepass = gfxGetTempSurface(&surfaceparams);
		}
	}

	if (gfxDoingPostprocessing() && gfx_state.settings.bloomQuality == GBLOOM_MED_BLOOM_SMALLHDR)
	{
		RdrSurfaceParams surfaceparams = {0};
		surfaceparams.name = "HDR Color";
		rdrSurfaceParamSetSizeSafe(&surfaceparams, round(action->renderSize[0] * ONE_QUARTER_F),
			round(action->renderSize[1] * ONE_QUARTER_F));
		surfaceparams.desired_multisample_level = 4;
		surfaceparams.required_multisample_level = 1;

		if (gfx_state.debug.overrideHDRAntialiasing)
			surfaceparams.desired_multisample_level = CLAMP(gfx_state.debug.overrideHDRAntialiasing, 1, 4);

		surfaceparams.depth_bits = 24;
		surfaceparams.flags = 0;
#if PLATFORM_CONSOLE
		surfaceparams.flags |= SF_DEPTH_TEXTURE;
#endif
		surfaceparams.flags |= SF_DEPTH_TEXTURE; // Need depth texture so that refraction objects that need depth access can work right in the underexposed pass

		surfaceparams.buffer_types[0] = SBT_RGBA;

		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
			surfaceparams.buffer_types[0] |= SBT_SRGB;

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

		action->bufferHDR = gfxGetTempSurface(&surfaceparams);
	}

	if (gfx_state.currentAction->gdraw.do_shadow_buffer ||
		(gfx_state.settings.scattering || gfxFeatureEnabled(GFEATURE_DOF)) && gfxDoingPostprocessing() ||
		action->cameraView->in_water_this_frame && gfxFeatureEnabled(GFEATURE_WATER) ||
		rdr_state.lowResAlphaHighResNeedsManualDepthTest && gfx_state.antialiasingQuality<=1)
	{
		RdrSurfaceParams surfaceparams = {0};
		surfaceparams.name = "Temp Buffer";
		rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0], action->renderSize[1]);
		surfaceparams.desired_multisample_level = 1;
		surfaceparams.required_multisample_level = 1;
		surfaceparams.depth_bits = 24;

		surfaceparams.flags = 0;
		surfaceparams.buffer_types[0] = SBT_RGBA;

		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
			surfaceparams.buffer_types[0] |= SBT_SRGB;

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

		action->bufferTempFullSize = gfxGetTempSurface(&surfaceparams);
	}

	if (!rdrDrawListSortBucketIsEmpty( gdraw->draw_list, RSBT_ALPHA_LOW_RES_ALPHA ) ||
		!rdrDrawListSortBucketIsEmpty( gdraw->draw_list, RSBT_ALPHA_LOW_RES_ADDITIVE ) ||
		!rdrDrawListSortBucketIsEmpty( gdraw->draw_list, RSBT_ALPHA_LOW_RES_SUBTRACTIVE ))
	{
		{
			RdrSurfaceParams surfaceparams = {0};
			surfaceparams.name = "LowResAlpha Scratch";
			rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0] / MAX(lowResFactor, 1),
				action->renderSize[1] / MAX(lowResFactor, 1));
			surfaceparams.desired_multisample_level = 1;
			surfaceparams.required_multisample_level = 1;
			surfaceparams.depth_bits = 24;

			// Soft particles require a depth texture to render correctly.
			if (gfx_state.settings.soft_particles || gfx_state.debug.debug_low_res_alpha)
			{
				surfaceparams.flags |= SF_DEPTH_TEXTURE;
			}
			
			surfaceparams.buffer_types[0] = SBT_RGBA;

			if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
				surfaceparams.buffer_types[0] |= SBT_SRGB;

			rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

			action->low_res_alpha_scratch = gfxGetTempSurface(&surfaceparams);

			if (rdr_state.lowResAlphaHighResNeedsManualDepthTest && gfx_state.antialiasingQuality>1)
			{
				surfaceparams.name = "MSAA Depth Buffer";
				rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0], action->renderSize[1]);
				surfaceparams.desired_multisample_level = gfx_state.antialiasingQuality;
				surfaceparams.required_multisample_level = 1;
				surfaceparams.depth_bits = 24;

				surfaceparams.flags = 0;
				surfaceparams.buffer_types[0] = SBT_RGBA; // Don't need any color buffer - use NULL surface instead?

				rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

				action->bufferTempDepthMSAA = gfxGetTempSurface(&surfaceparams);
			}
		}

		{
			RdrSurfaceParams surfaceparams = {0};
			surfaceparams.name = "LowResAlpha Edge";
			rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0] / MAX(lowResFactor, 1),
				action->renderSize[1] / MAX(lowResFactor, 1));
			surfaceparams.desired_multisample_level = 1;
			surfaceparams.required_multisample_level = 1;
			
			surfaceparams.buffer_types[0] = SBT_RGB16;

			rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

			action->low_res_alpha_edge = gfxGetTempSurface(&surfaceparams);
		}
	}

	// we should really create this one first, if it's in use, it's where primary rendering is going to
	if (action->renderSize[0] != width || action->renderSize[1] != height ||
			(!gfxDoingPostprocessing() &&
				(gfx_state.antialiasingQuality > 1 ||
				gdraw->do_shadow_buffer ||
				gdraw->do_outlining ||
				action->cameraView->in_water_this_frame ||
				rdrDrawListNeedsScreenGrab(gdraw->draw_list) ||
				rdrDrawListNeedsScreenGrabLate(gdraw->draw_list) ||
				rdrDrawListNeedsBlurredScreenGrab(gdraw->draw_list) ||
				rdrLowResAlphaEnabled())
			) ||
			gfx_state.forceOffScreenRendering
		)
	{
		static int iForceMultiSampleLevel = 0;
		RdrSurfaceParams surfaceparams = {0};
		surfaceparams.name = "Final";
		rdrSurfaceParamSetSizeSafe(&surfaceparams, action->renderSize[0], action->renderSize[1]);
		if (iForceMultiSampleLevel)
		{
			surfaceparams.desired_multisample_level = iForceMultiSampleLevel;
		}
		else
			surfaceparams.desired_multisample_level = gfx_state.antialiasingQuality;
		surfaceparams.required_multisample_level = 1;

		// Set up parameters for final rendersize surface
		surfaceparams.depth_bits = 24;
		// For just renderscale or low-end cards, can't use a DEPTH_TEXTURE here, won't run on old cards
		if (!gfxDoingPostprocessing() && (gfx_state.antialiasingQuality || gdraw->do_shadow_buffer || gdraw->do_outlining || action->cameraView->in_water_this_frame || rdrDrawListNeedsScreenDepthGrab(gdraw->draw_list)) && rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_DEPTH_TEXTURE))
		{
#if !PLATFORM_CONSOLE
			if (surfaceparams.desired_multisample_level <= 1 || rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_DEPTH_TEXTURE_MSAA))
				surfaceparams.flags |= SF_DEPTH_TEXTURE; //we can only use this if the hardware supports it otherwise the render thread will silently turn off AA
#else
			surfaceparams.flags |= SF_DEPTH_TEXTURE;
#endif

		}
		surfaceparams.buffer_types[0] = SBT_RGBA;
		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
			surfaceparams.buffer_types[0] |= SBT_SRGB;

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		if (gfx_state.render_scale_pointsample)
			surfaceparams.buffer_default_flags[0] |= RTF_MAG_POINT|RTF_MIN_POINT; // point sample

		action->bufferFinal = gfxGetTempSurface(&surfaceparams);
	}

	if (gfx_state.ui_postprocess && !action->is_offscreen && gfxDoingPostprocessing())
	{
		RdrSurfaceParams surfaceparams = {0};
		int deviceWidth, deviceHeight;
		surfaceparams.name = "UIBuffer";
		gfxGetActiveDeviceSize(&deviceWidth, &deviceHeight);
		rdrSurfaceParamSetSizeSafe(&surfaceparams, deviceWidth, deviceHeight);
		surfaceparams.desired_multisample_level = 1;
		surfaceparams.required_multisample_level = 1;
		surfaceparams.depth_bits = 0;
		surfaceparams.buffer_types[0] = SBT_RGBA;
		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
			surfaceparams.buffer_types[0] |= SBT_SRGB;

		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] |= RTF_MAG_POINT|RTF_MIN_POINT; // point sample
		action->bufferUI = gfxGetTempSurface(&surfaceparams);
	}

	gfxUFSetupHDRSurfaces();

	gfxUFSetSurface();

	setupRenderStages();

	PERFINFO_AUTO_STOP();
}

bool gfxUFIsCurrentActionBloomDebug()
{
	return (!gfx_state.currentAction->is_offscreen ^ gfxGetHeadshotDebug()) && (gfx_state.debug.postprocessing_debug || gfx_state.debug.bloom_debug);
}

static void gfxUFDeferredLighting(void)
{
	if (gfxIsStageEnabled(GFX_DEFERRED_COMBINE))
	{
		gfxBeginSection(__FUNCTION__);
		PERFINFO_AUTO_START(__FUNCTION__,1);

		gfxSetStageActiveSurface(GFX_DEFERRED_COMBINE);
		copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentSurface);

		gfxClearActiveSurfaceHDR(gfx_state.currentCameraView->clear_color, 1, true);
		gfxDoDeferredLighting(gfx_state.currentCameraView->frustum.viewmat, gfxGetStageInputTexHandle(GFX_DEFERRED_COMBINE, SBUF_DEPTH));

		PERFINFO_AUTO_STOP();
		gfxEndSection();
	}
}

static void gfxUFDrawOpaque(RdrDrawList *draw_list, const RdrLightDefinition **light_defs, const BlendedSkyInfo *sky_info)
{
	bool bNeedColorRestore=false;
	bool bNeedDepthRestore=false;
	const int depthBufferIndex = 0;
	const int depthSnapshotIndex = 1;
	PERFINFO_AUTO_START_FUNC();

	// If doing predicated tiling on the Xbox, this clear should be moved to happen after z-pre
	gfxBeginSection("Clear surface");
	// This is doing a resolve on the depth buffer here, but it's resolving the
	//  last shadowmap rendered, so that's okay, nothing to be alarmed by.
	gfxSetStageActiveSurface(GFX_OPAQUE_AFTER_ZPRE);
#if PLATFORM_CONSOLE
	// Don't need to clear color here if not doing OPAQUE_ONEPASS
	if (!gfxIsStageEnabled(GFX_OPAQUE_ONEPASS))
	{
		// Just clear depth, the color will be overwritten anyway
		rdrClearActiveSurface(gfx_state.currentDevice->rdr_device, MASK_SBUF_DEPTH|CLEAR_STENCIL, unitvec4, 1);
	}
	else
#endif
	{
		gfxClearActiveSurfaceHDR(gfx_state.currentCameraView->clear_color, 1, true);
	}
	gfxEndSection();

	if (draw_list)
	{
		rdrDrawListSendVisualPassesToRenderer(gfx_state.currentDevice->rdr_device, draw_list, light_defs);

		if (gfxIsStageEnabled(GFX_ZPREPASS_EARLY))
		{
			gfxBeginSection("Z-Prepass Early");
			gfxGpuMarker(EGfxPerfCounter_ZPREPASS);
			gfxSetStageActiveSurface(GFX_ZPREPASS_EARLY);

#if PLATFORM_CONSOLE
			//rdrClearActiveSurface(gfx_state.currentDevice->rdr_device, MASK_SBUF_DEPTH|CLEAR_STENCIL, unitvec4, 1);
#endif

			rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ZPREPASS, false, false, false);

			if (gfx_state.debug.zprepass && !gfx_state.currentAction->is_offscreen)
			{
				gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_DEPTH, depthBufferIndex, "Z-Prepass", 0);
			}

			gfxGpuMarker(EGfxPerfCounter_MISC);
			//gfxUnsetActiveSurface(gfx_state.currentDevice->rdr_device); // Does a resolve
			gfxEndSection();
		}


		if (gfxIsStageEnabled(GFX_OPAQUE_ONEPASS))
		{
			gfxBeginSection("Opaque One-pass");
			gfxGpuMarker(EGfxPerfCounter_OPAQUE_ONEPASS);
			gfxSetStageActiveSurface(GFX_OPAQUE_ONEPASS);

			rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_OPAQUE_ONEPASS, false, false, false);

			if (gfx_state.debug.zprepass && !gfx_state.currentAction->is_offscreen)
			{
				gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_0, 0, "Opaque One-pass", 0);
			}

			//gfxUnsetActiveSurface(gfx_state.currentDevice->rdr_device);  // Resolves color and depth
			gfxGpuMarker(EGfxPerfCounter_MISC);
			gfxEndSection();
		}

		if (gfxIsStageEnabled(GFX_OUTLINING_EARLY))
		{
			// If we want this to work well with tiling, this step should simply
			//  resolve the depth, within the tiling bracket, so we don't need to end
			//  tiling until the GFX_OUTLINING_LATE step.
			// Performance TODO: if z-prepass is disabled (low-end cards?) we can skip
			//  using a temporary texture and just apply back to the framebuffer for slightly
			//  better performance.
			gfxUnsetActiveSurface(gfx_state.currentDevice->rdr_device);  // Resolves color and depth
			gfxDoOutliningEarly(sky_info);
			if (!gfx_state.currentAction->is_offscreen && gfx_state.debug.postprocessing_debug)
			{
				gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_0, 0, "Outlining", 1);
			}
			bNeedColorRestore = true; // Depth is untouched, it's past the point we've written in EDRAM
		}

		if (gfxIsStageEnabled(GFX_ZPREPASS_LATE))
		{
			int any_geo_rendered = 0;
			assert(gfxIsStageEnabled(GFX_ZPREPASS_EARLY)); // Code just checks if _EARLY is enabled below
			gfxBeginSection("Z-Prepass Late");
			gfxSetStageActiveSurface(GFX_ZPREPASS_LATE);

			any_geo_rendered = rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ZPREPASS_NO_OUTLINE, false, false, false);

			if (gfx_state.debug.zprepass && !gfx_state.currentAction->is_offscreen)
			{
				gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_DEPTH, depthBufferIndex, "Z-Prepass Late", 0);
			}

			gfxUnsetActiveSurface(gfx_state.currentDevice->rdr_device); // Resolves just depth
			gfxEndSection();
		}

		if (gfxIsStageEnabled(GFX_SHADOW_BUFFER))
		{
			gfxGpuMarker(EGfxPerfCounter_SHADOW_BUFFER);
			gfxSetStageActiveSurface(GFX_SHADOW_BUFFER);
			copyCameraToSurface(gfx_state.currentCameraView, gfx_state.currentSurface);
			gfxDoDeferredShadows(light_defs, sky_info);

			if (gfx_state.debug.shadow_buffer)
			{
				gfxDebugThumbnailsAddSurface(gfx_state.currentSurface, SBUF_0, 0, "Shadow Buffer", 0);
				if (gfx_state.currentAction->gdraw.do_ssao)
					gfxDebugThumbnailsAddSurface(gfx_state.currentSurface, SBUF_0, 0, "SSAO", 2);
			}

			gfxUnsetActiveSurface(gfx_state.currentDevice->rdr_device); // Resolves color texture of ShadowBuffer
			bNeedColorRestore = true;
			gfxGpuMarker(EGfxPerfCounter_MISC);
		}
	}

#if _XBOX
	gfxSetStageActiveSurface(GFX_OPAQUE_AFTER_ZPRE);
	if (bNeedColorRestore || bNeedDepthRestore)
	{
		if (!gfxIsStageEnabled(GFX_OPAQUE_ONEPASS)) {
			// We have not written any color yet, just clear and restore depth
			if (bNeedDepthRestore) {
				gfxClearActiveSurfaceRestoreDepth(gfxGetStageOutput(GFX_OPAQUE_AFTER_ZPRE), gfx_state.currentCameraView->clear_color);
			} else {
				// Just clear
				gfxClearActiveSurfaceHDR(gfx_state.currentCameraView->clear_color, 1, false);
			}
		} else {
			// Restore color written in _ONEPASS
			// Restore depth written in ZPREPASS and ONEPASS - but depth isn't actually overwritten
			gfxBeginSection("Restore surfaces");
			rdrSurfaceRestoreAfterSetActive(gfx_state.currentSurface, (bNeedDepthRestore?MASK_SBUF_DEPTH:0) | (bNeedColorRestore?MASK_SBUF_ALL_COLOR:0));
			gfxEndSection();
		}
	}
	else
	{
		// Nothing happened above, and we cleared the surface above
		//gfxBeginSection("Clear surface");
		//gfxClearActiveSurfaceHDR(gfx_state.currentCameraView->clear_color, 1, true);
		//gfxEndSection();
	}
#endif

	if (draw_list)
	{
		// sort and draw world objects
		if (gfxIsStageEnabled(GFX_OPAQUE_AFTER_ZPRE))
		{
			gfxBeginSection("Draw opaque");
			gfxGpuMarker(EGfxPerfCounter_OPAQUE);
			gfxSetStageActiveSurface(GFX_OPAQUE_AFTER_ZPRE);
			rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_DEFERRED, false, false, false);
			gfxGpuMarker(EGfxPerfCounter_MISC);
			gfxEndSection();
		}

		if (gfx_state.settings.bloomQuality == GBLOOM_MAX_BLOOM_DEFERRED)
			gfxUFDeferredLighting();
		if (gfxIsStageEnabled(GFX_SKY))
		{
			gfxSetStageActiveSurface(GFX_SKY);
			gfxSkyDraw(gfx_state.currentCameraView->sky_data, false);
			gfxSetStageActiveSurface(GFX_OPAQUE_AFTER_ZPRE);
		}

		if (gfxIsStageEnabled(GFX_OUTLINING_LATE))
			gfxDoOutliningLate(sky_info);
		if (!gfx_state.currentAction->is_offscreen && gfx_state.debug.postprocessing_debug && (gfxIsStageEnabled(GFX_OUTLINING_LATE)))
			gfxDebugThumbnailsAddSurfaceCopy(gfxGetActiveSurface(), SBUF_0, 0, "Color after outlining", 0);

		if (gfxIsStageEnabled(GFX_NONDEFERRED_POST_OUTLINING))
		{
			gfxBeginSection("Draw non-deferred");
			gfxGpuMarker(EGfxPerfCounter_OPAQUE);
			gfxSetStageActiveSurface(GFX_NONDEFERRED_POST_OUTLINING);
			rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_NONDEFERRED, false, false, false);
			gfxGpuMarker(EGfxPerfCounter_MISC);
			gfxEndSection();
		}

		if (gfxIsStageEnabled(GFX_SEPARATE_HDR_PASS)) {
			gfxBeginSection("Snapshot for HDRPass");
			rdrSurfaceSnapshot(gfxGetActiveSurface(), "Snapshot for HDRPass", SCREENCOLOR_NOSKY_SNAPSHOT_IDX);
			gfxEndSection();
		}
	}

	if (gfxUFIsCurrentActionBloomDebug()) {
		gfxDebugThumbnailsAddSurfaceCopy(gfxGetActiveSurface(), SBUF_0, 0, "Color after all opaque", 0);
	}

	PERFINFO_AUTO_STOP();
}

static void gfxUFPostProcess1(RdrDrawList *draw_list, const BlendedSkyInfo *sky_info)
{
	PERFINFO_AUTO_START_FUNC();

	gfxPostprocessCheckPreload(); // While preloading, we want to draw a bunch of arbitrary postprocessing shaders here

#if _XBOX
	// "screen grab" is just a resolve, which will happen automatically when the surface is inactivated
	if (!gfxIsStageEnabled(GFX_DEPTH_OF_FIELD) && !rdrDrawListSortBucketIsEmpty(draw_list, RSBT_ALPHA_NEED_GRAB))
	{
		gfxSetStageActiveSurface(GFX_SKY);
		rdrSurfaceSnapshot(gfxGetActiveSurface(), SCREENCOLOR_SNAPSHOT_IDX);
	}
#else
	// on PC & PS3 we can't render from the same texture we're rendering to, so we must grab the buffers
	// and store them in a secondary texture (but don't need to save/restore depth)
	if (gfxIsStageEnabled(GFX_DEPTH_OF_FIELD) || !rdrDrawListSortBucketIsEmpty(draw_list, RSBT_ALPHA_NEED_GRAB) ||
		(!gfxIsStageEnabled(GFX_DEPTH_OF_FIELD) && rdrDrawListNeedsBlurredScreenGrab(draw_list)))
	{
		gfxSetStageActiveSurface(GFX_SKY);
		rdrSurfaceSnapshot(gfxGetActiveSurface(), "gfxUFPostProcess1:SCREENCOLOR_SNAPSHOT_IDX", SCREENCOLOR_SNAPSHOT_IDX);
	}
#endif

	if (gfxIsStageEnabled(GFX_SEPARATE_HDR_PASS))
		gfxDoHDRPassOpaque(draw_list);

	if (gfxIsStageEnabled(GFX_DEPTH_OF_FIELD))
		gfxDoDepthOfField(GFX_DEPTH_OF_FIELD, &sky_info->dofValues, &sky_info->colorCorrectionValues, GSS_DEPTHOFFIELD, NULL);

#if _PS3
#elif _XBOX
	if (gfxIsStageEnabled(GFX_SEPARATE_HDR_PASS) && !gfxIsStageEnabled(GFX_DEPTH_OF_FIELD))
	{
		// the HDR pass switched surfaces, so we need to restore color and depth to the LDR surface
		gfxBeginSection("Restore surface");
		gfxSetStageActiveSurface(GFX_ALPHA);
		rdrSurfaceRestoreAfterSetActive(gfx_state.currentSurface, MASK_SBUF_ALL);
		gfxEndSection();
	}
	else if (gfxIsStageEnabled(GFX_ZPREPASS_EARLY))
	{
		// Previous passes were not full-screen and depth, so the depth is actually still left intact
		// Not true if we're doing tiling
		//gfxBeginSection("Restore depth");
		//gfxClearActiveSurfaceRestoreDepth(gfxGetStageOutput(GFX_ALPHA), NULL);
		//gfxEndSection();
	}
#endif


	PERFINFO_AUTO_STOP();
}

static void gfxUFDraw3DDebug(const Vec3 cam_pos)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;

	gfxBeginSection(__FUNCTION__);
	PERFINFO_AUTO_START_FUNC();
	// Put debugging code that needs to draw into the 3D world here
	// (2D debugging code goes into gfxUFDraw2D)

	//gfxSetStageActiveSurface(GFX_ALPHA);

	if (gfx_state.debug.polyset)
		gfxDrawGPolySet(gfx_state.debug.polyset, cam_pos);

	if (gfx_state.debug.show_rooms || gfx_state.debug.show_room_partitions)
	{
		int i, j, k;
		for (k = 0; k < eaSize(&gdraw->regions); ++k)
		{
			WorldRegion *region = gdraw->regions[k];
			RoomConnGraph *graph = worldRegionGetRoomConnGraph(region);

			if (graph)
			{
				// draw room gpsets and connecting lines
				for (i = 0; i < eaSize(&graph->rooms); i++)
				{
					Room *room = graph->rooms[i];

					if (gfx_state.debug.show_rooms && room->union_mesh)
						gfxDrawGMesh(room->union_mesh, (i % 2) ? ColorBlue : ColorYellow, false);

					else if (gfx_state.debug.show_room_partitions)
					{
						for (j = 0; j < eaSize(&room->partitions); j++)
						{
							RoomPartition *partition = room->partitions[j];
							if (partition->mesh)
								gfxDrawGMesh(partition->mesh, (j%2)?ColorBlue:ColorYellow, false);
						}
					}

					for (j = 0; j < eaSize(&room->portals); j++)
					{
						if (room->portals[j]->neighbor)
						{
							gfxDrawLine3D(room->bounds_mid, room->portals[j]->world_mid, ColorGreen);
							gfxDrawLine3D(room->portals[j]->world_mid, room->portals[j]->neighbor->world_mid, ColorGreen);
						}
					}
				}
			}
		}
	}

	if (gfx_state.debug.show_room_shadow_graph)
		gfxDebugDrawShadowGraph();

	if (gfx_state.debug.gmesh)
		gfxDrawGMesh(gfx_state.debug.gmesh, ColorYellow, false);

	gfxDebugDrawDynDebug3D();

	if (gfx_state.currentDevice && !rdrTestDeviceInactiveDontReactivate(gfx_state.currentDevice->rdr_device))
		gfxDebugDrawCollision();
	PERFINFO_AUTO_STOP();
	gfxEndSection();
}

static void gfxUFDrawAlphaPreDOF(RdrDrawList *draw_list)
{
	if (gfxIsStageEnabled(GFX_ALPHA_PREDOF))
	{
		gfxBeginSection("Draw pre-DOF alpha");
		gfxSetStageActiveSurface(GFX_ALPHA_PREDOF);

#if _XBOX
		// "screen grab" is just a resolve, which will happen automatically when the surface is inactivated
#else
		// on PC & PS3 we can't render from the same texture we're rendering to, so we must grab the buffers
		// and store them in a secondary texture (but don't need to save/restore depth)
		if (!rdrDrawListSortBucketIsEmpty(draw_list, RSBT_ALPHA_NEED_GRAB_PRE_DOF))
		{
			rdrSurfaceSnapshot(gfxGetActiveSurface(), "gfxUFDrawAlphaPreDOF:SCREENCOLOR_SNAPSHOT_IDX", SCREENCOLOR_SNAPSHOT_IDX);
		}
#endif

		//some of the alpha objects both read and write depth (in separate passes) so we need disable the resolve-on-dirty to prevent a resolve between each object
		rdrSurfaceSetAutoResolveDisableMask(gfxGetActiveSurface(), MASK_SBUF_DEPTH);

		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA_PREDOF_NEEDGRAB, false, false, false);

		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA_PREDOF, false, false, false);

		rdrSurfaceSetAutoResolveDisableMask(gfxGetActiveSurface(), 0);

		gfxEndSection();

		if (!gfx_state.currentAction->is_offscreen && gfx_state.debug.postprocessing_debug)
			gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_0, 0, "After pre-DOF alpha", 0);
	}
}

static void gfxUFDrawLowResAlphaEarly(RdrDrawList *draw_list)
{
	RdrDevice* device = gfx_state.currentDevice->rdr_device;
	
	gfxBeginSection( __FUNCTION__ );
	{
		RdrSurface* highResDepth = gfxGetStageInputSurface(GFX_ALPHA_LOW_RES, 0);
		RdrSurface* lowResOutput = gfxGetStageTemp(GFX_ALPHA_LOW_RES, 0);
		TexHandle lowResOutputTex = rdrSurfaceToTexHandleEx(lowResOutput, SBUF_0, 1, 0, false);
		RdrSurface* lowResEdge = gfxGetStageTemp(GFX_ALPHA_LOW_RES, 1);
		TexHandle lowResEdgeTex = rdrSurfaceToTexHandleEx(lowResEdge, SBUF_0, 1, 0, false);
		RdrSurface* output = gfxGetStageOutput(GFX_ALPHA_LOW_RES);
		
		gfxSetActiveSurface(lowResOutput);
		
		// Downsample the depth buffer
		gfxShrink4Depth(highResDepth, SBUF_DEPTH, 0);
		if (gfx_state.debug.debug_low_res_alpha)
		{
			gfxDebugThumbnailsAddSurfaceCopy(lowResOutput, SBUF_DEPTH, 0, "Low Res Depth Copy", 0);
		}
	}
	gfxEndSection();
}

static bool lraNoOverride = false;
AUTO_CMD_INT( lraNoOverride, lraNoOverride );

#define LOWRESALPHA_STENCIL_BIT			1
#define LOWRESALPHA_STENCIL_BIT_MASK	( 1 << LOWRESALPHA_STENCIL_BIT )
#define LOWRESALPHA_STENCIL_REF			LOWRESALPHA_STENCIL_BIT_MASK

static void gfxShowStencil(RdrDevice * device, U32 stencil_value, const Vec4 color)
{
	// limit color writes to specific stencil value
	rdrStencilFunc(device, RDRSTENCIL_ENABLE, RPPDEPTHTEST_EQUAL, stencil_value, RDRSTENCIL_NO_MASK);
	rdrStencilOp(device, RDRSTENCILOP_KEEP, RDRSTENCILOP_KEEP, RDRSTENCILOP_KEEP);

	gfxClearActiveSurfaceEx(color, zerovec4, zerovec4, zerovec4, 0.0f, MASK_SBUF_0);

	// reset stencil state
	rdrStencilOp(device, RDRSTENCILOP_KEEP, RDRSTENCILOP_KEEP, RDRSTENCILOP_KEEP);
	rdrStencilFunc(device, RDRSTENCIL_DISABLE, RPPDEPTHTEST_OFF, 0, RDRSTENCIL_NO_MASK);
}

static int debug_stencil_value = 0;

static const int GFX_LOWRESALPHA_TEMP_COLOR = 2;

static void gfxUFDrawLowResAlpha(RdrDrawList *draw_list, RdrSortType sort_type, RdrSortBucketType sb_type, SA_PARAM_NN_VALID bool* is_first_call)
{
	static Vec4 blackvec_onealpha = {0,0,0,1};
	
	RdrDevice* device = gfx_state.currentDevice->rdr_device;
	RdrPPBlendType blend_type = RPPBLEND_LOW_RES_ALPHA_PP;
	const float* clear_color = blackvec_onealpha;
	GfxSpecialShader edge_detect_shader = GSS_LOW_RES_ALPHA_EDGE_DETECT;
	bool needDepthOverride = rdr_state.lowResAlphaHighResNeedsManualDepthTest;

	if( rdrDrawListSortBucketIsEmpty( draw_list, sb_type )) {
		return;
	}

	switch( sort_type )
	{
		case RST_ALPHA_LOW_RES_ALPHA:
			blend_type = RPPBLEND_LOW_RES_ALPHA_PP;
			clear_color = blackvec_onealpha;
			edge_detect_shader = GSS_LOW_RES_ALPHA_EDGE_DETECT;
		xcase RST_ALPHA_LOW_RES_ADDITIVE:
			blend_type = RPPBLEND_ADD;
			clear_color = zerovec4;
			edge_detect_shader = GSS_LOW_RES_ALPHA_EDGE_DETECT_ADDITIVE_BLEND;
		xcase RST_ALPHA_LOW_RES_SUBTRACTIVE:
			blend_type = RPPBLEND_SUBTRACT;
			clear_color = zerovec4;
			edge_detect_shader = GSS_LOW_RES_ALPHA_EDGE_DETECT_ADDITIVE_BLEND;
		xdefault:
			FatalErrorf( "%s: unsupported sort type %d", __FUNCTION__, sort_type );
	}
	
	gfxBeginSection( __FUNCTION__ );
	{
		RdrSurface* highResDepth = gfxGetStageInputSurface(GFX_ALPHA_LOW_RES, 0);
		RdrSurface* lowResOutput = gfxGetStageTemp(GFX_ALPHA_LOW_RES, 0);
		TexHandle lowResOutputTex = rdrSurfaceToTexHandleEx(lowResOutput, SBUF_0, GFX_LOWRESALPHA_TEMP_COLOR, 0, false);
		RdrSurface* lowResEdge = gfxGetStageTemp(GFX_ALPHA_LOW_RES, 1);
		TexHandle lowResEdgeTex = rdrSurfaceToTexHandleEx(lowResEdge, SBUF_0, GFX_LOWRESALPHA_TEMP_COLOR, 0, false);
		RdrSurface* output = gfxGetStageOutput(GFX_ALPHA_LOW_RES);

		gfxSetActiveSurface(lowResOutput);

		rdrClearActiveSurface( device, MASK_SBUF_0, clear_color, 0.0 );
		
		// Draw low res objects
		rdrDrawListDrawObjects( device, draw_list, sort_type, false, false, false );
		rdrSurfaceSnapshot( lowResOutput, __FUNCTION__":GFX_LOWRESALPHA_TEMP_COLOR", GFX_LOWRESALPHA_TEMP_COLOR );

		if (gfx_state.debug.debug_low_res_alpha)
			gfxDebugThumbnailsAddSurfaceCopy( lowResOutput, SBUF_0, GFX_LOWRESALPHA_TEMP_COLOR, "Low Res", false );

		// Calculate edges
		gfxSetActiveSurface( lowResEdge );
		{
			Vec4 ppTexScale;
			setVec4( ppTexScale, 1.f / lowResOutput->width_nonthread, 1.f / lowResOutput->height_nonthread, 0, 0 );
			gfxPostprocessOneTex( lowResOutput, lowResOutputTex,
								  edge_detect_shader, &ppTexScale, 1,
								  RPPBLEND_REPLACE );
		}
		rdrSurfaceSnapshot( lowResEdge, __FUNCTION__":GFX_LOWRESALPHA_TEMP_COLOR", GFX_LOWRESALPHA_TEMP_COLOR );

		if (gfx_state.debug.debug_low_res_alpha)
			gfxDebugThumbnailsAddSurfaceCopy( lowResEdge, SBUF_0, GFX_LOWRESALPHA_TEMP_COLOR, "Low Res Edges", false );
		
		// Final output, start with only stencil writes to clear stencil
		gfxSetStageActiveSurfaceEx(GFX_ALPHA_LOW_RES, MASK_SBUF_STENCIL);
		if( needDepthOverride && !lraNoOverride )
		{
			gfxBeginSection( "gfxUFDrawLowResAlpha - depth override" );
			if (gfx_state.currentAction->bufferTempDepthMSAA)
			{
				gfxOverrideDepthSurface(gfx_state.currentAction->bufferTempDepthMSAA->surface);
			} else {
				gfxOverrideDepthSurface(gfx_state.currentAction->bufferTempFullSize->surface);
			}
		}

		// stencil set up to clear specified bits
		rdrStencilFunc(device, RDRSTENCIL_ENABLE, RPPDEPTHTEST_OFF, 0, LOWRESALPHA_STENCIL_BIT_MASK);
		rdrStencilOp(device, RDRSTENCILOP_KEEP, RDRSTENCILOP_KEEP, RDRSTENCILOP_ZERO);
		// no color/depth writes, just stencil writes masked to two's place bit
		gfxClearActiveSurfaceEx(zerovec4, zerovec4, zerovec4, zerovec4, 1.0f, MASK_SBUF_STENCIL);


		// enable color writes
		gfxSetStageActiveSurface(GFX_ALPHA_LOW_RES);

		// Combine into main buffer, setup stencil for edge cases
		rdrStencilFunc(device, RDRSTENCIL_ENABLE, RPPDEPTHTEST_OFF, LOWRESALPHA_STENCIL_REF, LOWRESALPHA_STENCIL_BIT_MASK);
		rdrStencilOp(device, RDRSTENCILOP_KEEP, RDRSTENCILOP_KEEP, RDRSTENCILOP_REPLACE);
		
		rdrAddRemoveTexHandleFlags( &lowResOutputTex, 0, RTF_MIN_POINT | RTF_MAG_POINT ); //< force a bilinear filter
		rdrAddRemoveTexHandleFlags( &lowResEdgeTex, RTF_MIN_POINT | RTF_MAG_POINT, 0 );
		gfxPostprocessTwoTex( lowResOutput, lowResOutputTex, lowResEdgeTex,
							  GSS_ALPHABLIT_DISCARD_EDGE, NULL, 0,
							  false, RPPDEPTHTEST_OFF,
							  blend_type, false, false );

		if (gfx_state.debug.debug_low_res_alpha == sort_type - RST_ALPHA_LOW_RES_ALPHA + 1)
		{
			gfxShowStencil(device, debug_stencil_value % 256, 
				unitmat44[sort_type - RST_ALPHA_LOW_RES_ALPHA]);
		}

		// Draw high detail when necessary
		rdrStencilFunc(device, RDRSTENCIL_ENABLE, RPPDEPTHTEST_EQUAL, 0, LOWRESALPHA_STENCIL_BIT_MASK);
		rdrStencilOp(device, RDRSTENCILOP_KEEP, RDRSTENCILOP_KEEP, RDRSTENCILOP_KEEP);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, sort_type, false, true, needDepthOverride && !lraNoOverride);

		if( needDepthOverride && !lraNoOverride)
		{
			// Fake a clear to, presumably, reset the Hi-Z on, at least, ATI 3850 cards to prevent video corruption
			// This should write no pixels, and change nothing
			rdrStencilFunc(device, RDRSTENCIL_ENABLE, RPPDEPTHTEST_EQUAL, 255, RDRSTENCIL_NO_MASK);
			rdrStencilOp(device, RDRSTENCILOP_KEEP, RDRSTENCILOP_KEEP, RDRSTENCILOP_KEEP);
			rdrClearActiveSurface(gfx_state.currentDevice->rdr_device, CLEAR_STENCIL, zerovec4, 0.0f);
		}
		// Reset to default stencil
		rdrStencilFunc(device, RDRSTENCIL_DISABLE, RPPDEPTHTEST_OFF, 0, RDRSTENCIL_NO_MASK);

		if( needDepthOverride && !lraNoOverride )
		{
			gfxOverrideDepthSurface(NULL);
			gfxEndSection();
		}
	}
	gfxEndSection();

	*is_first_call = false;
}

static void gfxUFDrawAlpha(RdrDrawList *draw_list)
{
	if (draw_list)
	{		
		int any_geo_rendered = 0;

		if (gfxIsStageEnabled(GFX_ALPHA))
		{
			gfxBeginSection("Draw alpha");
			gfxSetStageActiveSurface(GFX_ALPHA);
			
			//some of the alpha objects both read and write depth (in separate passes) so we need disable the resolve-on-dirty to prevent a resolve between each object
			rdrSurfaceSetAutoResolveDisableMask(gfxGetActiveSurface(), MASK_SBUF_DEPTH);
			
			any_geo_rendered += rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA, false, false, false);

			rdrSurfaceSetAutoResolveDisableMask(gfxGetActiveSurface(), 0);

			if (!gfx_state.currentAction->is_offscreen && gfx_state.debug.postprocessing_debug)
				gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_0, 0, "After alpha", 0);
		}

		if( gfxIsStageEnabled( GFX_ALPHA_LOW_RES ) && rdrLowResAlphaEnabled()) {
			bool first_time = true;

			gfxUFDrawLowResAlphaEarly(draw_list);
			gfxUFDrawLowResAlpha(draw_list, RST_ALPHA_LOW_RES_ALPHA, RSBT_ALPHA_LOW_RES_ALPHA, &first_time);
			gfxUFDrawLowResAlpha(draw_list, RST_ALPHA_LOW_RES_ADDITIVE, RSBT_ALPHA_LOW_RES_ADDITIVE, &first_time);
			gfxUFDrawLowResAlpha(draw_list, RST_ALPHA_LOW_RES_SUBTRACTIVE, RSBT_ALPHA_LOW_RES_SUBTRACTIVE, &first_time);

			if (!gfx_state.currentAction->is_offscreen && gfx_state.debug.postprocessing_debug)
				gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_0, 0, "After low res alpha", 0);
		}

		if (gfxIsStageEnabled(GFX_ALPHA))
		{
			gfxSetStageActiveSurface(GFX_ALPHA);

			//some of the alpha objects both read and write depth so we need disable the resolve-on-dirty to prevent a resolve between each object (this causes rendering errors but we dont care)
			rdrSurfaceSetAutoResolveDisableMask(gfxGetActiveSurface(), MASK_SBUF_DEPTH);


			any_geo_rendered += rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA_LATE, false, false, false);
						
			if (rdrDrawListNeedsScreenGrabLate(draw_list))
				rdrSurfaceSnapshot(gfxGetActiveSurface(), "gfxUFDrawAlpha:SCREENCOLOR_SNAPSHOT_IDX", SCREENCOLOR_SNAPSHOT_IDX);
			
			// Still call the draw function even if we don't have any refraction objects, we might have wireframe!
			any_geo_rendered += rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, draw_list, RST_ALPHA_POST_GRAB_LATE, false, false, false);

			rdrSurfaceSetAutoResolveDisableMask(gfxGetActiveSurface(), 0);

			if (!gfx_state.currentAction->is_offscreen && gfx_state.debug.postprocessing_debug)
				gfxDebugThumbnailsAddSurface(gfx_state.currentSurface, SBUF_0, 0, "After alpha LATE", 0);

			gfxEndSection();
		}

		if (gfx_state.debug.zprepass && !gfx_state.currentAction->is_offscreen)
		{
			gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_DEPTH, 0, "Final Depth", 0);
		}

		if (gfxIsStageEnabled(GFX_SEPARATE_HDR_PASS))
			gfxDoHDRPassNonDeferred(draw_list);
	}
}

static void gfxDoScreenshotWithCallbacksAndStuff(void)
{
	bool bTempFile = false;
	bool bIsDepth = gfx_state.screenshot_type == SCREENSHOT_DEPTH;

	if (!gfx_state.screenshot_filename[0])
	{
		bTempFile = true;
		sprintf(gfx_state.screenshot_filename, "%s/tempSS_%u.jpg", fileTempDir(), timeSecondsSince2000());
	}

	gfxSaveScreenshotInternal(gfx_state.screenshot_filename);

	if (gfx_state.screenshot_CB)
	{
		gfx_state.screenshot_CB(gfx_state.screenshot_filename, gfx_state.screenshot_CB_userData);
		gfx_state.screenshot_CB = NULL;
		gfx_state.screenshot_CB_userData = NULL;
	}

	if (bTempFile)
	{
		DeleteFile_UTF8(gfx_state.screenshot_filename);
	}

	gfx_state.screenshot_type = SCREENSHOT_NONE;
}

static void gfxUFPostProcess2(const BlendedSkyInfo *sky_info)
{
	PERFINFO_AUTO_START_FUNC();

	if (gfxUFIsCurrentActionBloomDebug())
	{
		if (gfx_state.currentAction->bufferLDR)
			gfxDebugThumbnailsAddSurfaceMaybeCopy(gfx_state.currentAction->bufferLDR->surface, SBUF_0, 0, "Exposed Scene", 0, gfx_state.currentAction->action_type != GfxAction_Primary);

		if (gfx_state.currentAction->bufferHDR)
			gfxDebugThumbnailsAddSurfaceMaybeCopy(gfx_state.currentAction->bufferHDR->surface, SBUF_0, 0, "Underexposed Scene", 0, gfx_state.currentAction->action_type != GfxAction_Primary);

		if (gfx_state.currentAction->bufferLDRandHDR)
		{
			gfxDebugThumbnailsAddSurfaceMaybeCopy(gfx_state.currentAction->bufferLDRandHDR->surface, SBUF_0, 0, "Exposed Scene", 0, gfx_state.currentAction->action_type != GfxAction_Primary);
			gfxDebugThumbnailsAddSurfaceMaybeCopy(gfx_state.currentAction->bufferLDRandHDR->surface, SBUF_1, 0, "Underexposed Scene", 0, gfx_state.currentAction->action_type != GfxAction_Primary);
		}
		else
		if (gfx_state.currentAction->bufferMRT)
		{
			gfxDebugThumbnailsAddSurfaceMaybeCopy(gfx_state.currentAction->bufferMRT->surface, SBUF_0, 0, "Albedo", 0, gfx_state.currentAction->action_type != GfxAction_Primary);
			gfxDebugThumbnailsAddSurfaceMaybeCopy(gfx_state.currentAction->bufferMRT->surface, SBUF_1, 0, "Normal", 0, gfx_state.currentAction->action_type != GfxAction_Primary);
			gfxDebugThumbnailsAddSurfaceMaybeCopy(gfx_state.currentAction->bufferMRT->surface, SBUF_2, 0, "Spec+Shiny", 0, gfx_state.currentAction->action_type != GfxAction_Primary);
			gfxDebugThumbnailsAddSurfaceMaybeCopy(gfx_state.currentAction->bufferMRT->surface, SBUF_3, 0, "Emis+DiffW", 0, gfx_state.currentAction->action_type != GfxAction_Primary);
		}
	}

	if (gfxIsStageEnabled(GFX_TONEMAP))
	{
		// Post-process HDR buffer into final buffer
		gfxDoHDR(sky_info, &gfx_state.currentCameraView->frustum);
	}
	else
	{
		gfxDoSoftwareLightAdaptation(sky_info);
	}

	if (gfxIsStageEnabled(GFX_WATER_DOF))
	{
		const WorldVolumeWater* waterData = gfx_state.currentCameraView->in_water_this_frame;
		RdrScreenPostProcess ppscreen = { 0 };
		
		gfxWaterCalculateTexcoords(&gfx_state.currentCameraView->frustum, ppscreen.texcoords);
		ppscreen.use_texcoords = true;
		
		rdrSurfaceSnapshot(gfxGetStageInputSurface(GFX_WATER_DOF, SBUF_0), "water DOF:SCREENCOLOR_SNAPSHOT_IDX", SCREENCOLOR_SNAPSHOT_IDX);
		gfxDoDepthOfField(GFX_WATER_DOF, &waterData->dofValues, 0, GSS_DEPTHOFFIELD_SCALE, ppscreen.texcoords);
	}

	if (gfxIsStageEnabled(GFX_WATER))
	{
		gfxDoWaterVolumeShader(GFX_WATER, &gfx_state.currentCameraView->frustum);
	}

	// calculate next frame's exposure transform
	gfxCalcHDRTransform(sky_info);

	if (gfxIsStageEnabled(GFX_RENDERSCALE))
	{
		if (!gfx_state.currentAction->is_offscreen && (gfx_state.screenshot_type == SCREENSHOT_3D_ONLY || gfx_state.screenshot_type == SCREENSHOT_DEPTH) && !gfx_state.screenshot_after_renderscale)
		{
			gfxSetActiveSurface(gfxGetStageInputSurface(GFX_RENDERSCALE, 0));

			gfxDoScreenshotWithCallbacksAndStuff();			
		}

		gfxSetStageActiveSurface(GFX_RENDERSCALE);
		gfxScaleBuffer(gfxGetStageInputSurface(GFX_RENDERSCALE, 0), false);
	}

	PERFINFO_AUTO_STOP();
}

static void gfxUFBefore2D(void)
{
	PERFINFO_AUTO_START_FUNC();
	if (!gfx_state.currentAction->is_offscreen && (gfx_state.screenshot_type == SCREENSHOT_3D_ONLY || gfx_state.screenshot_type == SCREENSHOT_DEPTH))
	{
		gfxSetStageActiveSurface(GFX_UI);
		gfxDoScreenshotWithCallbacksAndStuff();
	}

	if (gfx_state.anaglyph_hack)
		gfxDebugGrabFrame(0);
	PERFINFO_AUTO_STOP();
}

static void gfxUFDraw2D(void)
{
	RdrDrawList* drawList = gfx_state.currentDevice->draw_list2d;

	PERFINFO_AUTO_START_FUNC();

	if (!gfx_state.currentAction->is_offscreen && gfxIsStageEnabled(GFX_UI))
	{
		gfxSetStageActiveSurface(GFX_UI);

		// JE: Moving this before UI because most of the screen-space FX (called by power FX) were designed for this
		//   If we later want to use FX in the UI again
		// draw above-sprite particles -- they were queued earlier to allow textures to load.
		gfxBeginSection("Draw UI FX");
		rdrDrawListSendVisualPassesToRenderer(gfx_state.currentDevice->rdr_device, drawList, NULL);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, drawList, RST_OPAQUE_ONEPASS, false, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, drawList, RST_DEFERRED, false, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, drawList, RST_NONDEFERRED, false, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, drawList, RST_ALPHA_PREDOF_NEEDGRAB, false, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, drawList, RST_ALPHA_PREDOF, false, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, drawList, RST_ALPHA, false, false, false);
		rdrDrawListDrawObjects( gfx_state.currentDevice->rdr_device, drawList, RST_ALPHA_LOW_RES_ALPHA, false, false, false);
		rdrDrawListDrawObjects( gfx_state.currentDevice->rdr_device, drawList, RST_ALPHA_LOW_RES_ADDITIVE, false, false, false);
		rdrDrawListDrawObjects( gfx_state.currentDevice->rdr_device, drawList, RST_ALPHA_LOW_RES_SUBTRACTIVE, false, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, drawList, RST_ALPHA_LATE, false, false, false);
		rdrDrawListDrawObjects(gfx_state.currentDevice->rdr_device, drawList, RST_ALPHA_POST_GRAB_LATE, false, false, false);
		rdrDrawListAddStats(drawList, &gfx_state.debug.frame_counts.draw_list_stats);
		rdrDrawListEndFrame(drawList);
		gfxEndSection();

		// Debugging stuff under the sprites
		gfxDebugThumbnailsDisplay();

		// draw sprites
		if (!gfx_state.debug.disableSprites)
		{
			//clear the sprite list and render in one operation since it's more efficent
			gfxRenderSpriteList(gfx_state.currentDevice->sprite_list, drawList, true);
		}
	}

	PERFINFO_AUTO_STOP();
}

static void gfxUFFinalPostProcess(void)
{
	PERFINFO_AUTO_START_FUNC();
	if (gfxIsStageEnabled(GFX_UI_POSTPROCESS))
	{
		RdrSurface *source_surface = gfxGetStageInputSurface(GFX_UI_POSTPROCESS, 0);
		Vec4 v[2] = {
			{1.f/source_surface->width_nonthread, 1.f/source_surface->height_nonthread, source_surface->width_nonthread, source_surface->height_nonthread},
			{1,0.9,gfx_state.client_loop_timer, 1 - MINMAX((fabs(fmod(gfx_state.client_loop_timer, 4) - 2)-0.1)*15, 0, 1)}
		};
		gfx_state.ui_postprocess = CLAMP(gfx_state.ui_postprocess, 1, 5);
		gfxSetStageActiveSurface(GFX_UI_POSTPROCESS);
		gfxPostprocessOneTex( source_surface,
			gfxGetStageInputTexHandle(GFX_UI_POSTPROCESS, 0),
			GSS_UI_COLORBLIND_PRO+gfx_state.ui_postprocess-1, v, ARRAY_SIZE(v),
			RPPBLEND_REPLACE );
	}
	PERFINFO_AUTO_STOP();
}


static void gfxUFDrawAuxiliaryVisualPass(void)
{
	GfxRenderAction *action = gfx_state.currentAction;
	GfxGlobalDrawParams *gdraw = &action->gdraw;
	RdrDevice *rdr_device = gfx_state.currentDevice->rdr_device;

	gfxSetStageActiveSurface(GFX_AUX_VISUAL_PASS);
	gfxClearActiveSurface(zerovec4, zerovec4, zerovec4, unitvec4, 1);
	rdrDrawListDrawAuxVisualPassObjects(rdr_device, gdraw->draw_list);

	if (gfx_state.debug.aux_visual_pass_debug)
		gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_0, 0, "Aux Visual Pass", 1);
}

static void gfxUFPrepareTargetHighlightBuffer()
{
	GfxRenderAction *action = gfx_state.currentAction;
	GfxGlobalDrawParams *gdraw = &action->gdraw;
	RdrDevice *rdr_device = gfx_state.currentDevice->rdr_device;
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[1];

	gfxSetStageActiveSurface(GFX_AUX_VISUAL_PASS);

	ppscreen.tex_width = gfx_state.currentSurface->width_nonthread;
	ppscreen.tex_height = gfx_state.currentSurface->height_nonthread;

	textures[ 0 ] = rdrSurfaceToTexHandleEx(gfx_state.currentSurface, SBUF_0, 0, RTF_CLAMP_U|RTF_CLAMP_V, false);
	ppscreen.material.tex_count = ARRAY_SIZE(textures);
	ppscreen.material.textures = textures;

	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_TARGET_HIGHLIGHT_ALPHATOWHITE);
	ppscreen.blend_type = RPPBLEND_REPLACE;

	gfxPostprocessScreen(&ppscreen);

	if (gfx_state.debug.aux_visual_pass_debug)
		gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_0, 0, "Target Highlight", 1);

	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_TARGET_HIGHLIGHT_SMOOTHING);
	gfxDoBlur(GBT_BOX, gfx_state.currentSurface, NULL, 
		gfx_state.currentSurface, SBUF_0, RDR_NULL_TEXTURE, RDR_NULL_TEXTURE, 
		3 + (gfxDoingPostprocessing()?gfx_state.target_highlight:0) * 2, 0, NULL, NULL);

	if (gfx_state.debug.aux_visual_pass_debug)
		gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_0, 0, "Target Smoothed", 0);
}

static void gfxUFDoAction(void)
{
	const BlendedSkyInfo *sky_info = gfxSkyGetVisibleSky(gfx_state.currentCameraView->sky_data);
	GfxRenderAction *action = gfx_state.currentAction;
	GfxGlobalDrawParams *gdraw = &action->gdraw;
	RdrDevice *rdr_device = gfx_state.currentDevice->rdr_device;

	float old_cur_time = gfx_state.cur_time;
	float old_frame_time = gfx_state.frame_time;
	float old_client_loop_timer = gfx_state.client_loop_timer;
	
	int origBloomQuality = gfx_state.settings.bloomQuality;

	if( action->override_time ) {
		gfx_state.cur_time = 12;
		gfx_state.frame_time = 0;
		gfx_state.client_loop_timer = 0;
	}	

	if(!gfxActionAllowsBloom(action)) {
		gfx_state.settings.bloomQuality = GBLOOM_OFF;
	}

	switch( action->action_type ) {
		case GfxAction_Primary:
			gfxBeginSection( "Primary Action" );
		xcase GfxAction_Headshot:
			gfxBeginSection( "Headshot Action" );
		xcase GfxAction_ImpostorAtlas:
			gfxBeginSection( "ImposterAtlas Action" );
		xdefault:
			gfxBeginSection( "Unknown Action" );
	}

	gfxUFSetupSurfaces();

	gfxDemandLoadMaterials(false);

	gfxDoLightLoading();
	gfxUnloadAllNotUsedThisFrame_check();

	if (!rdrDrawListIsEmpty(gdraw->draw_list) || !rdrDrawListIsEmpty(gdraw->draw_list_sky) || !rdrDrawListIsEmpty(gdraw->draw_list_ao) || !action->is_offscreen && gfxGetQueuedPrimCount() > 0)
	{
		const RdrLightDefinition **light_defs = gfxGetLightDefinitionArray(rdrGetDeviceIdentifier(rdr_device));
		RdrQuadDrawable waterQuad = {0};
		bool waterQuadIsDrawn = false;

		if (!action->is_offscreen)
			gfxDrawQueuedPrims();

		waterQuadIsDrawn = gfxMaybeDoWaterVolumeShaderLowEnd(action->outputSurface->width_nonthread, action->outputSurface->height_nonthread, &waterQuad);

		gfxGpuMarker(EGfxPerfCounter_SHADOWS);
		if (gdraw->do_shadows)
		{
			rdrDrawListSendShadowPassesToRenderer(rdr_device, gdraw->draw_list, light_defs);
			gfxDoLightShadowsDraw(gfx_state.currentVisFrustum, gdraw->nextVisProjection, gdraw->draw_list);
		}
		gfxGpuMarker(EGfxPerfCounter_MISC);

		if (rdrDrawListHasAuxPassObjects(gdraw->draw_list))
		{
			gfxBeginSection("Draw Auxiliary Visual Pass");
			gfxUFDrawAuxiliaryVisualPass();
			gfxEndSection();

			if (gfx_state.target_highlight && gfxDoingPostprocessing())
			{
				gfxBeginSection("Target Highlight Buffer");
				gfxUFPrepareTargetHighlightBuffer();
				gfxEndSection();
			}
		}


		gfxUFDrawOpaque(gdraw->draw_list, light_defs, sky_info);

		if (gfx_state.settings.scattering && gfxDoingPostprocessing())
		{
			gfxDoCalculateScattering(light_defs, sky_info);
			if (gfx_state.debug.postprocessing_debug)
				gfxDebugThumbnailsAddSurfaceCopy(gfx_state.currentSurface, SBUF_0, 0, "Scattering", 0);
		}
	
		if (gfxDoingPostprocessing() && gfx_state.settings.lensflare_quality > 1)
		{
			gfxDoLensZOSample(sky_info);
		}

		gfxGpuMarker(EGfxPerfCounter_ALPHA);
		gfxUFDrawAlphaPreDOF(gdraw->draw_list);
		gfxGpuMarker(EGfxPerfCounter_POSTPROCESS);

		if (gfx_state.currentAction->bTXAAEnable)
		{
			int dst_idx, prev_idx;

			gfxBeginSection("TXAA resolve");

			if (gfx_state.currentAction->nTXAABufferFlip) {
				dst_idx = SCREENCOLOR_TXAA_SNAPSHOT_LAST_FRAME_IDX;
				prev_idx = SCREENCOLOR_TXAA_SNAPSHOT_IDX;
			} else {
				dst_idx = SCREENCOLOR_TXAA_SNAPSHOT_IDX;
				prev_idx = SCREENCOLOR_TXAA_SNAPSHOT_LAST_FRAME_IDX;
			}
			gfx_state.currentAction->nTXAABufferFlip = !gfx_state.currentAction->nTXAABufferFlip;

			rdrSurfaceSnapshotExTXAA(gfxGetActiveSurface(), "TXAA Resolve", dst_idx, prev_idx, MASK_SBUF_0 | MASK_SBUF_DEPTH, gfx_state.debug.txaa_debug);
			if (gfx_state.debug.txaa_debug) {
                gfxDebugThumbnailsAddSurface(gfxGetActiveSurface(), SBUF_0, dst_idx, "TXAA resolve", 0);
                gfxDebugThumbnailsAddSurface(gfxGetActiveSurface(), SBUF_0, prev_idx, "TXAA resolve last frame", 0);
			}
			gfxEndSection();
		}

		if (gfx_state.settings.scattering && gfxDoingPostprocessing())
		{
			gfxDoScattering(sky_info);
		}

		gfxUFPostProcess1(gdraw->draw_list, sky_info);

		if (!action->is_offscreen && (gfx_state.screenshot_type == SCREENSHOT_DEPTH || gfx_state.visualize_screenshot_depth))
		{
			RdrSurface* activeSurface = gfxGetActiveSurface();
		
			gfxSetStageActiveSurface(GFX_UI);
			gfxCopyDepthIntoRGB(activeSurface, 0, gfx_state.screenshot_depth_min, gfx_state.screenshot_depth_max, gfx_state.screenshot_depth_power);
		}
		else
		{
		
			gfxUFDrawAlpha(gdraw->draw_list);

			if (waterQuadIsDrawn)
			{
				if(action->bufferLDR)
					gfxSetActiveSurface(action->bufferLDR->surface);
				else if(action->bufferLDRandHDR)
					gfxSetActiveSurface(action->bufferLDRandHDR->surface);
				else
					// uh... this shouldn't happen...
					;
				rdrDrawQuad(rdr_device, &waterQuad);
			}
			
			gfxUFPostProcess2(sky_info);
		}
	}
	else
	{
		gfxBeginSection("Clear surface");
		gfxSetActiveSurface(action->outputSurface);
		gfxDoSoftwareLightAdaptation(sky_info);
		gfxCalcHDRTransform(sky_info);
		if (gfx_state.currentCameraController->override_bg_color)
			rdrClearActiveSurface(rdr_device, CLEAR_ALL, gfx_state.currentCameraController->clear_color, 1);
		else
			gfxClearActiveSurfaceHDR(gfx_state.currentCameraView->clear_color, 1, true);
		gfxEndSection();
		if (!action->is_offscreen)
			gfxUFDraw3DDebug(gdraw->cam_pos);

		gfxPostprocessCheckPreload(); // While preloading, we want to draw a bunch of arbitrary postprocessing shaders here
	}


	gfxGpuMarker(EGfxPerfCounter_2D);
	gfxUFBefore2D();
	gfxUFDraw2D();

	gfxUFFinalPostProcess();

	rdrDrawListAddStats(gdraw->draw_list, &gfx_state.debug.frame_counts.draw_list_stats);
	zoAccumulateStats(gdraw->occlusion_buffer, &gfx_state.debug.frame_counts);

	if (action->outputSnapshotIdx && action->is_offscreen)
	{
		gfxSetActiveSurface(action->outputSurface); // should be redundant, but just in case
		rdrSurfaceSnapshot(gfxGetActiveSurface(), "outputSnapshotIdx && is_offscreen", action->outputSnapshotIdx);
	}

	if (action->postRenderBlitOutputSurface)
	{
#if _XBOX
		IVec4 sourceRect;
		sourceRect[0] = sourceRect[1] = 0;
		sourceRect[2] = action->outputSurface->width_nonthread;
		sourceRect[3] = action->outputSurface->height_nonthread;
		rdrSurfaceResolve(rdr_device, action->postRenderBlitOutputSurface, sourceRect, action->postRenderBlitDestPixel);
#else
		Mat4 fogmat;
		// Set it up to render to the blitout surface
		gfxSetActiveSurface(action->postRenderBlitOutputSurface);

		// Set up the camera matrices for our frustum and viewport
		gfxGetFogMatrix(fogmat);
		rdrSurfaceUpdateMatricesFromFrustum(action->postRenderBlitOutputSurface, gfx_state.currentCameraView->projection_matrix, fogmat, &gfx_state.currentCameraView->frustum, action->postRenderBlitViewport[2], action->postRenderBlitViewport[0], action->postRenderBlitViewport[3], action->postRenderBlitViewport[1], gfx_state.currentCameraView->frustum.cammat[3]);

		// Blit it to the new surface
		if (gfx_state.debug.bodysockTexAA)
			gfxShrink4(action->outputSurface, SBUF_0, 0, true);
		else
			gfxScaleBuffer(action->outputSurface, true);
#endif
	}
	else if (action->postRenderBlitOutputTexture)
	{
		// Resolve into texture
		BasicTexture* texture = action->postRenderBlitOutputTexture;
		RdrSurface* actionSurface = action->outputSurface;
		U32 imageByteCount = 0;
				
		RdrTexParams* rtex = rdrStartUpdateTextureFromSurface(
				rdr_device, texture->tex_handle, RTEX_BGRA_U8,
				actionSurface, SBUF_0,
				round(actionSurface->width_nonthread * action->postRenderBlitViewport[0]),
				round(actionSurface->height_nonthread * action->postRenderBlitViewport[1]),
				&imageByteCount );		
		rtex->debug_texture_backpointer = texture;		
		rdrEndUpdateTexture( rdr_device );
		texRecordNewMemUsage( texture, TEX_MEM_VIDEO, getTextureMemoryUsageEx( RTEX_2D, RTEX_BGRA_U8, texture->realWidth, texture->realHeight, texGetDepth( texture ), false, false, false ));

		if( action->postRenderBlitDebugThumbnail ) {
			gfxDebugThumbnailsAdd( texture->tex_handle, "Postrender", false, false );
		}
	}

	gfxActionReleaseSurfaces(action);

	gfx_state.cur_time = old_cur_time;
	gfx_state.frame_time = old_frame_time;
	gfx_state.client_loop_timer = old_client_loop_timer;
	
	gfxEndSection();

	gfx_state.settings.bloomQuality = origBloomQuality;

	gfxGpuMarker(EGfxPerfCounter_IDLE);
}

static void gfxSetActiveFrustum(GfxActionType action_type)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;

	if (gfx_state.debug.frustum_debug.frustum_debug_mode == 1)
	{
		gfx_state.debug.frustum_debug.frustum_debug_mode = 2;
		globMovementLog("[gfx] Calling %s (frustum_debug_mode == 1).", __FUNCTION__);
		frustumCopy(&gfx_state.debug.frustum_debug.static_frustum, &gfx_state.currentCameraView->frustum);
		copyVec3(gfx_state.currentCameraController->camfocus, gfx_state.debug.frustum_debug.static_camera_focus);
		copyMat44(gfx_state.currentCameraView->projection_matrix, gfx_state.debug.frustum_debug.static_projection_matrix);
	}

	if (gfx_state.debug.frustum_debug.frustum_debug_mode == 2)
	{
		gfx_state.currentVisFrustum = &gfx_state.debug.frustum_debug.static_frustum;
		copyVec3(gfx_state.debug.frustum_debug.static_camera_focus, gfx_state.currentCameraFocus);
		gdraw->nextVisFrustum = &gfx_state.debug.frustum_debug.static_frustum;
		copyMat44(gfx_state.debug.frustum_debug.static_projection_matrix, gdraw->nextVisProjection);
	}
	else
	{
		if (gdraw->draw_all_directions)
		{
			static Frustum frust = {0};
			Vec3 cam_pos;

			// draw a different face every frame
			gfxGetActiveCameraPos(cam_pos);
			setupCubeMapFrustumForFace(&frust, cam_pos, gfx_state.frame_count % 6);
			gfx_state.currentVisFrustum = &frust;
			gdraw->nextVisFrustum = &frust;
		}
		else
		{
			if(action_type==GfxAction_Primary)
			{
				// Set the current vis frustum to the new frustum if the zo isn't threaded
				int zoThreads = zoGetThreadCount(gdraw->occlusion_buffer);
				if(!zoThreads)
					gfxFlipCameraFrustum(gfx_state.currentCameraView);
			}
			gfx_state.currentVisFrustum = &gfx_state.currentCameraView->frustum;
			gdraw->nextVisFrustum = &gfx_state.currentCameraView->new_frustum;
		}
		copyVec3(gfx_state.currentCameraController->camfocus, gfx_state.currentCameraFocus);
		copyMat44(gfx_state.currentCameraView->projection_matrix, gdraw->nextVisProjection);
	}
}

static void gfxStartZOcclusionTest(void)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;

	PERFINFO_AUTO_PIX_START(__FUNCTION__);
	if (gdraw->z_occlusion)
		zoInitFrame(gdraw->occlusion_buffer, gdraw->nextVisProjection, gdraw->nextVisFrustum);
	PERFINFO_AUTO_PIX_STOP();
}

static void gfxStartZCreateZHierarchy(void)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;

	PERFINFO_AUTO_PIX_START(__FUNCTION__);
	if (gdraw->z_occlusion)
		zoCreateZHierarchy(gdraw->occlusion_buffer);
	PERFINFO_AUTO_PIX_STOP();
}

void gfxGetRenderSizeFromScreenSize(U32 renderSize[2])
{
	int i;
	for (i=0; i<2; i++) {
		if (gfx_state.settings.renderScaleSetBySize[i] && gfx_state.settings.renderSize[i] > 0) {
			renderSize[i] = gfx_state.settings.renderSize[i];
		} else if (gfx_state.settings.renderScale[i] > 0) {
			renderSize[i] = MAX(1, gfx_state.screenSize[i] * gfx_state.settings.renderScale[i]);
		} else {
			renderSize[i] = gfx_state.screenSize[i];
		}
	}
}

static void gfxAnaglyphCameraHack(void)
{
	static Mat44 saved;
	if (gfx_state.frame_count % 2)
	{
		Vec3 addend = {-0.2, 0, 0};
		Vec3 rotate = {0, 0, 0};
		Mat4 mat;
		Vec4 saved_row;
		mat44to43(saved, mat);
		saved_row[0] = saved[0][3];
		saved_row[1] = saved[1][3];
		saved_row[2] = saved[2][3];
		saved_row[3] = saved[3][3];
		rotateMat3(rotate, mat);
		mat43to44(mat, gfx_state.currentAction->cameraView->projection_matrix);
		addVec3(gfx_state.currentAction->cameraView->projection_matrix[3], addend, gfx_state.currentAction->cameraView->projection_matrix[3]);
		gfx_state.currentAction->cameraView->projection_matrix[0][3] = saved_row[0];
		gfx_state.currentAction->cameraView->projection_matrix[1][3] = saved_row[1];
		gfx_state.currentAction->cameraView->projection_matrix[2][3] = saved_row[2];
		gfx_state.currentAction->cameraView->projection_matrix[3][3] = saved_row[3];
	} else {
		copyMat44(gfx_state.currentAction->cameraView->projection_matrix, saved);
	}
}

void gfxSetCurrentRegion(int region_idx)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	int i;

	if (gdraw)
	{
		// restore old frustum settings
		for (i = 0; i < gdraw->pass_count; ++i)
		{
			RdrDrawListPassData *pass = gdraw->passes[i];
			addVec3(pass->frustum->cammat[3], gdraw->pos_offset, pass->frustum->cammat[3]);
			// don't need to apply to view matrix yet, we can just do it once below
		}

		if (region_idx < 0)
		{
			copyVec3(gdraw->camera_positions, gdraw->cam_pos);
			gdraw->hidden_object_id = gdraw->hidden_object_ids[0];
			zeroVec3(gdraw->pos_offset);
			gdraw->use_far_depth_range = 0;
			gfxMaterialSetOverrideCubeMap(NULL);
		}
		else
		{
			copyVec3(&gdraw->camera_positions[region_idx*3], gdraw->cam_pos);
			gdraw->hidden_object_id = gdraw->hidden_object_ids[region_idx];
			subVec3(gdraw->camera_positions, gdraw->cam_pos, gdraw->pos_offset);
			// This check used to be whether the region type is different from the active region.
			// That was unintuitive and problematic, so now we just test against WRT_SystemMap.
			// If there is some other case, perhaps we should add a region flag. --TomY
			gdraw->use_far_depth_range = worldRegionGetType(gdraw->regions[region_idx]) == WRT_SystemMap;
			gfxMaterialSetOverrideCubeMap(worldRegionGetOverrideCubeMap(gdraw->regions[region_idx]));
		}

		// change the view matrix used for visibility culling
		for (i = 0; i < gdraw->pass_count; ++i)
		{
			RdrDrawListPassData *pass = gdraw->passes[i];
			subVec3(pass->frustum->cammat[3], gdraw->pos_offset, pass->frustum->cammat[3]);
			makeViewMatrix(pass->frustum->cammat, pass->frustum->viewmat, pass->frustum->inv_viewmat);
			copyMat4(pass->frustum->viewmat, pass->viewmat);
		}
	}
	else
	{
		//gfxMaterialSetOverrideCubeMap(NULL); // JE: disabling this because it gets called before we ever *use* the override cubemap - this effectively means only one region per scene (the last one) is affecting the override cubemap.
	}
}

__forceinline static void gfxFrameCountRecordPrimaryCameraLocation()
{
	const GfxCameraView * camera_view = gfx_state.currentDevice->primaryCameraView;
	copyVec3(camera_view->frustum.cammat[3], gfx_state.debug.frame_counts.cam_pos);
	getMat3YPR(camera_view->frustum.cammat, gfx_state.debug.frame_counts.cam_pyr);
}

// This function overrides GraphicsLib's frame rendering by replacing the GlobalStateMachine's 
// gclBaseState BeginFrame and EndFrame state code with custom function callbacks. This is a 
// fairly low-level override. It is the caller's responsibility to lock and unlock the device 
// to present frames in the EndFrame hook, for example. See gfxStartPerformanceTest for an 
// example.
//
// Callers may request hooks at any time, but only while the system does not have any pending
// hook requests. A pending request will activate only at the end of the current frame, to prevent
// hooks from controlling only half the frame. One hook request may be pending while an existing
// hook is controlling rendering. The hook terminates when the end-frame hook returns false, and
// GraphicsLib then transitions to the next state.
bool gfxHookFrame(GfxHookBeginFrame pfnNewBeginFrameHook, GfxHookEndFrame pfnNewEndFrameHook)
{
	// can't change hook if there already is a pending change
	if (gfx_state.nextHookState.pfnHookBeginFrame || gfx_state.nextHookState.pfnHookEndFrame)
		return false;

	// queue change as next state to prevent changing in middle of frame
	gfx_state.nextHookState.pfnHookBeginFrame = pfnNewBeginFrameHook;
	gfx_state.nextHookState.pfnHookEndFrame = pfnNewEndFrameHook;
	return true;
}

bool gfxIsStateHooked(const GfxHookFrameState *hookState)
{
	return hookState->pfnHookBeginFrame || hookState->pfnHookEndFrame;
}

bool gfxIsHookedFrame()
{
	return gfxIsStateHooked(&gfx_state.currentHookState);
}

bool gfxIsHookedNextFrame()
{
	return gfxIsStateHooked(&gfx_state.nextHookState);
}

void gfxStartMainFrameAction(bool in_editor, bool z_occlusion, bool hide_world,
	bool draw_all_directions, bool draw_sky)
{
	if (gfx_state.currentHookState.pfnHookBeginFrame)
	{
		gfx_state.currentHookState.pfnHookBeginFrame(in_editor, z_occlusion, hide_world,
			draw_all_directions, draw_sky);
		return;
	}
	gfxFrameCountRecordPrimaryCameraLocation();
	gfxStartAction(	rdrGetPrimarySurface(gfx_state.currentDevice->rdr_device), 0, NULL, 
		gfx_state.currentDevice->primaryCameraView, gfx_state.currentDevice->primaryCameraController, 
		in_editor, z_occlusion, hide_world, draw_all_directions, 
		draw_sky, true, true, true, gfx_state.target_highlight && gfxDoingPostprocessing(),
		GfxAction_Primary, false, 0 );
	gfx_state.zprepass_last_on = gfx_state.currentAction->gdraw.do_zprepass;
}

static GfxRenderAction *getNextAction(bool is_offscreen)
{
	GfxRenderAction *action;

	if (is_offscreen)
	{
		action = eaGet(&gfx_state.currentDevice->offscreen_actions, gfx_state.currentDevice->next_available_offscreen_action);
		if (!action)
		{
			action = aligned_calloc(1, sizeof(GfxRenderAction), 16);
			action->gdraw.draw_list = rdrCreateDrawList(false);
			action->gdraw.draw_list_sky = rdrCreateDrawList(false);
			action->gdraw.draw_list_ao = rdrCreateDrawList(false);
			action->is_offscreen = true;
			eaInsert(&gfx_state.currentDevice->offscreen_actions, action, gfx_state.currentDevice->next_available_offscreen_action);
		}
		gfx_state.currentDevice->next_available_offscreen_action++;
	}
	else
	{
		int i;

		if (!gfx_state.currentDevice->main_frame_action)
		{
			gfx_state.currentDevice->main_frame_action = aligned_calloc(1, sizeof(GfxRenderAction), 16);
			gfx_state.currentDevice->main_frame_action->gdraw.draw_list = rdrCreateDrawList(true);
			gfx_state.currentDevice->main_frame_action->gdraw.draw_list_sky = rdrCreateDrawList(false);
			gfx_state.currentDevice->main_frame_action->gdraw.draw_list_ao = rdrCreateDrawList(false);
		}

		gfx_state.currentDevice->main_frame_action->allow_bloom       = true;
		gfx_state.currentDevice->main_frame_action->allow_indoors     = true;
		gfx_state.currentDevice->main_frame_action->use_sun_indoors   = false;
		gfx_state.currentDevice->main_frame_action->no_indoor_ambient = false;

		for (i = 0; i < eaSize(&gfx_state.currentDevice->actions); ++i)
		{
			assertmsg(gfx_state.currentDevice->actions[i] != gfx_state.currentDevice->main_frame_action, "Only one RenderAction is allowed to draw to the primary surface!");
		}

		action = gfx_state.currentDevice->main_frame_action;
	}

	action->postRenderBlitOutputTexture = NULL;
	action->postRenderBlitDebugThumbnail = false;

	return action;
}

bool gfxIsActionFull(GfxActionType action_type)
{
	int count=0;
	assert(action_type > GfxAction_Unspecified && action_type < GfxAction_MAX);
	FOR_EACH_IN_EARRAY(gfx_state.currentDevice->actions, GfxRenderAction, action)
	{
		assert(action->action_type != GfxAction_Unspecified);
		if (action->action_type == action_type)
			count++;
	}
	FOR_EACH_END;
	return count >= max_actions_per_frame[action_type];
}

//////////////////////////////////////////////////////////////////////////
// This function sets up everything needed before world traversal:
// - initializes GlobalDrawParams (gdraw)
// - starts RdrDrawList frame
// - sets up visual pass (and zprepass, if enabled)
// - starts zocclusion testing phase
// - updates sky (if needed)
// - updates lights
// - sets up shadow passes
//
// No render passes are allowed to be added to an action outside of this function.
//
//////////////////////////////////////////////////////////////////////////
bool gfxStartAction(RdrSurface *output_surface, int output_snapshot_idx, WorldRegion *region, 
					GfxCameraView *camera_view, GfxCameraController *camera_controller, 
					bool in_editor, bool z_occlusion, bool hide_world, bool draw_all_directions, 
					bool draw_sky, bool allow_shadows, bool allow_zprepass, bool allow_outlines,
					bool allow_targeting_halo, GfxActionType action_type, bool force_sun_indoors,
					F32 override_lod_scale)
{
	const BlendedSkyInfo *sky_info;
	GfxGlobalDrawParams *gdraw;
	bool is_offscreen = (output_surface != rdrGetPrimarySurface(gfx_state.currentDevice->rdr_device) || action_type != GfxAction_Primary);
	bool wait_for_zocclusion;
	RdrDrawListLightingMode light_mode;
	int i;
	WorldRegionType shadow_region_type = WRT_Ground;
	WorldRegion* shadow_region = region;

	PERFINFO_AUTO_START_FUNC_PIX();

	PERFINFO_AUTO_START("top", 1);

	gfxOncePerFramePerDevice();

	assert(!gfx_state.currentAction);

	if (gfx_state.currentDevice->skipThisFrame || (is_offscreen && gfxIsActionFull(action_type))) {
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP_FUNC_PIX();
		return false;
	}

	gfx_state.currentAction = getNextAction(is_offscreen);
	gfx_state.currentAction->action_type = action_type;
	gdraw = &gfx_state.currentAction->gdraw;
	gfx_state.currentActionIdx = eaPush(&gfx_state.currentDevice->actions, gfx_state.currentAction);

	gfx_state.currentAction->outputSurface = output_surface;
	gfx_state.currentAction->draw_sky = draw_sky;
	if (is_offscreen)
	{
		gfx_state.currentAction->outputSnapshotIdx = output_snapshot_idx;
		gfx_state.currentAction->renderSize[0] = output_surface->width_nonthread;
		gfx_state.currentAction->renderSize[1] = output_surface->height_nonthread;
	}
	else
	{
		gfxGetRenderSizeFromScreenSize(gfx_state.currentAction->renderSize);
		if (!sameVec2(gfx_state.currentAction->renderSize, gfx_state.currentDevice->lastRenderSize)) {
			if (gfx_state.currentDevice->lastRenderSize[0]) {
				gfxFreeSpecificSizedTempSurfaces(gfx_state.currentDevice, gfx_state.currentDevice->lastRenderSize, true);
			}
			copyVec2(gfx_state.currentAction->renderSize, gfx_state.currentDevice->lastRenderSize);
		}
	}
	setVec4(gfx_state.currentAction->renderViewport, 1, 1, 0, 0);

	gfx_state.currentAction->cameraView = camera_view;
	gfx_state.currentAction->cameraController = camera_controller;
	gfxSetActiveCameraView(camera_view, false);
	gfxSetActiveCameraController(camera_controller, false);

    gfx_state.currentAction->texReduceResolutionScale = log2f(gfx_state.currentAction->renderSize[1] * camera_view->projection_matrix[1][1]);

	if (draw_all_directions) {
		// force it to reset it's counter while in this mode by calling it twice in the same frame
		gfxCheckAutoFrameRateStabilizer();
		gfxCheckAutoFrameRateStabilizer();
	}
	
	gfx_state.currentAction->use_sun_indoors = force_sun_indoors;
	gfx_state.currentAction->allow_indoors |= force_sun_indoors;

	// setup this frame's state
	gdraw->z_occlusion = z_occlusion && !gfx_state.debug.noZocclusion && !gfx_state.currentCameraController->ortho_mode && !gfx_state.currentCameraController->ortho_mode_ex;
	gdraw->hide_world = hide_world;
	gdraw->draw_all_directions = draw_all_directions;
	gdraw->do_shadows = gfxFeatureEnabled(GFEATURE_SHADOWS) && !gfx_state.currentCameraController->override_disable_shadows && 
						gfx_state.currentRendererIndex == 0 && allow_shadows && 
						!gfx_state.currentCameraController->ortho_mode && 
						!gfx_state.currentCameraController->ortho_mode_ex && !
						gfx_state.vertexOnlyLighting;
	gdraw->do_splat_shadows = ( !gdraw->do_shadows && !gfx_state.settings.disableSplatShadows && allow_shadows ) || gfx_state.settings.disableSplatShadows == 2;
	gdraw->do_hdr_pass = gfxDoingPostprocessing() && gfx_state.settings.bloomQuality == GBLOOM_MED_BLOOM_SMALLHDR;
	gdraw->has_hdr_texture = gfxDoingPostprocessing() && gfx_state.settings.bloomQuality == GBLOOM_HIGH_BLOOM_FULLHDR;
	gdraw->do_shadow_buffer = gfx_state.shadow_buffer && (gdraw->do_shadows | gfx_state.settings.ssao) && !gfx_state.disable_zprepass;
	gdraw->do_ssao = gfx_state.settings.ssao && gdraw->do_shadow_buffer;

	// TEMP - SSAO does not work in ortho projection mode. [RMARR - 12/13/11]
	if (gfx_state.currentCameraController->ortho_mode || gfx_state.currentCameraController->ortho_mode_ex)
	{
		gdraw->do_ssao = 0;
	}

	// JE: Although I'm enabling z-prepass if any of these other options are on, I *think*
	//   the only one that strictly requires it is shadow buffered shadows.  Outlining
	//   would only if we were using the normal channel.  Even if no features are
	//   enabled that require it, it still gives a performance boost in some situations,
	//   so we enable it on the high-end cards that have these features enabled.
	gdraw->do_zprepass = !gfx_state.disable_zprepass && (
		gdraw->do_shadow_buffer || gfxFeatureEnabled(GFEATURE_OUTLINING) || gfxDoingPostprocessing() && gfxFeatureEnabled(GFEATURE_DOF) ||
		!gfx_state.vertexOnlyLighting || // Generally high-end and probably pixel-shader bound, so z-prepass will help
		gfx_state.force_enable_zprepass);
	gdraw->do_outlining = allow_outlines && gfxFeatureEnabled(GFEATURE_OUTLINING);

	worldGetSelectedTintColor(gdraw->selectedTintColor);

	gfxSetActiveFrustum(action_type);

	wait_for_zocclusion = (gdraw->z_occlusion && gfx_state.debug.wait_for_zocclusion && action_type==GfxAction_Primary && zoGetThreadCount(gdraw->occlusion_buffer));
	if(wait_for_zocclusion)
		gfxFlipCameraFrustum(gfx_state.currentCameraView); // Going to wait for zo to finish, so we can flip now

	if (region)
	{
		eaPush(&gdraw->regions, region);
		eafPush3(&gdraw->camera_positions, gfx_state.currentVisFrustum->cammat[3]);
		eaiPush(&gdraw->hidden_object_ids, 0);
	}
	else if (hide_world)
	{
		eaPush(&gdraw->regions, worldGetEditorWorldRegion());
		eafPush3(&gdraw->camera_positions, gfx_state.currentVisFrustum->cammat[3]);
		eaiPush(&gdraw->hidden_object_ids, 0);
	}
	else if (gfx_state.debug.draw_all_regions)
	{
		WorldRegion **zmap_regions = zmapInfoGetWorldRegions(NULL);
		for (i = 0; i < eaSize(&zmap_regions); ++i)
		{
			if (!worldRegionIsEditorRegion(zmap_regions[i]))
			{
				eaPush(&gdraw->regions, zmap_regions[i]);
				eafPush3(&gdraw->camera_positions, gfx_state.currentVisFrustum->cammat[3]);
				eaiPush(&gdraw->hidden_object_ids, 0);
			}
		}
	}
	else
	{
		eaPush(&gdraw->regions, worldGetWorldRegionByPos(gfx_state.currentCameraFocus));
		eafPush3(&gdraw->camera_positions, gfx_state.currentVisFrustum->cammat[3]);
		eaiPush(&gdraw->hidden_object_ids, 0);
	}

	assert(eaSize(&gdraw->regions) > 0);

	worldRegionGetAssociatedRegionList(&gdraw->regions, &gdraw->camera_positions, &gdraw->hidden_object_ids);
	gfxSetCurrentRegion(0);

	if (gdraw->z_occlusion)
	{
		gfxCameraViewCreateOcclusion(gfx_state.currentCameraView);
		gdraw->occlusion_buffer = gfx_state.currentCameraView->occlusion_buffer;
	}
	else
	{
		gdraw->occlusion_buffer = NULL;
	}

	gdraw->in_editor = in_editor && !gfx_state.currentCameraController->override_hide_editor_objects;
	if( override_lod_scale > 0 ) {
		gdraw->lod_scale = override_lod_scale;
	} else {
		gdraw->lod_scale = gfxGetLodScale();
	}
	gdraw->global_wireframe = gfx_state.wireframe;
	gdraw->do_aux_visual_pass = allow_targeting_halo;

	if (gfx_state.vertexOnlyLighting && !gfxInTailor())
		light_mode = RDLLM_VERTEX_ONLY;
	else if (gfx_state.uberlighting)
		light_mode = RDLLM_UBERLIGHTING;
	else if (!rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_SM30) || gfx_state.debug.simpleLighting)
		light_mode = RDLLM_SIMPLE;
	else if (gfx_state.cclighting && rdrMaxSupportedObjectLights() == MAX_NUM_OBJECT_LIGHTS)
		light_mode = RDLLM_CCLIGHTING;
	else
		light_mode = RDLLM_NORMAL;

	gfxUpdateCameraLight();

	PERFINFO_AUTO_STOP_START("sky", 1);

	gfxSkyUpdate(gfx_state.currentCameraView, gfxGetTime());
	if (!is_offscreen)
		gfxSkyFillDebugText(gfx_state.currentCameraView->sky_data);
	gfx_state.currentCameraView->sky_update_timestamp = gfx_state.client_frame_timestamp;

	sky_info = gfxSkyGetVisibleSky(gfx_state.currentCameraView->sky_data);

	// set dof_distance
	if (sky_info->dofValues.farValue >= 0.15f)
	{
		// figure out where the dof value goes to .15
		gdraw->dof_distance = sky_info->dofValues.focusDist + (sky_info->dofValues.farDist - sky_info->dofValues.focusDist) * 0.15f / sky_info->dofValues.farValue;
	}
	else
	{
		gdraw->dof_distance = 8e16;
	}

	PERFINFO_AUTO_STOP_START("rdrDrawListBeginFrame calls", 1);

	rdrDrawListBeginFrame(gdraw->draw_list,
		gdraw->do_zprepass,
		rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_24BIT_DEPTH_TEXTURE), 
		gdraw->do_outlining,
		gfx_state.debug.sort_opaque,
		gdraw->do_shadow_buffer,
		gfx_state.shadow_buffer && ((gfxFeatureEnabled(GFEATURE_SHADOWS) && !gfx_state.vertexOnlyLighting) || gdraw->do_ssao),
		gdraw->do_ssao,
		light_mode,
		gdraw->do_hdr_pass, 
		gdraw->has_hdr_texture, 
		gfx_state.currentCameraView->adapted_light_range,
		gdraw->dof_distance,
		SQR(gdraw->lod_scale),
		allow_targeting_halo);
	rdrDrawListAddPass(gdraw->draw_list, gfx_state.currentVisFrustum, RDRSHDM_VISUAL, NULL, 0, -1, false);

	if (gdraw->do_ssao)
	{
		rdrDrawListBeginFrame(gdraw->draw_list_ao,
			false,
			rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_24BIT_DEPTH_TEXTURE), 
			false,
			gfx_state.debug.sort_opaque,
			gdraw->do_shadow_buffer,
			gfx_state.shadow_buffer && gfxFeatureEnabled(GFEATURE_SHADOWS) && !gfx_state.vertexOnlyLighting,
			gdraw->do_ssao,
			light_mode,
			gdraw->do_hdr_pass, 
			gdraw->has_hdr_texture, 
			gfx_state.currentCameraView->adapted_light_range,
			gdraw->dof_distance,
			SQR(gdraw->lod_scale),
			false);
		rdrDrawListAddPass(gdraw->draw_list_ao, gfx_state.currentVisFrustum, RDRSHDM_VISUAL, NULL, 0, -1, false);
	}
	
	PERFINFO_AUTO_STOP_START("zo", 1);

	if(wait_for_zocclusion)
	{
		if(gfx_state.debug.wait_for_zocclusion==1) // 1 starts here, 2 stats after camera controller is run
			gfxStartZOcclusionTest();
		zoWaitUntilOccludersDrawn(gdraw->occlusion_buffer);
		zoWaitUntilDone(gdraw->occlusion_buffer);
	}
	else
	{
		zoWaitUntilDone(gdraw->occlusion_buffer);
		gfxStartZOcclusionTest();
	}

	PERFINFO_AUTO_STOP_START("misc", 1);

	// figure out region_type - passed in region first, then camera frustum, then first 'default' region specified above
	if (!shadow_region)
	{
		if (camera_view)
		{
			shadow_region = worldGetWorldRegionByPos(camera_view->frustum.cammat[3]);
		}

		if (!shadow_region)
		{
			shadow_region = gdraw->regions[0];
		}
	}

	shadow_region_type = worldRegionGetType(shadow_region);

	assert(shadow_region_type > WRT_None && shadow_region_type < WRT_COUNT);

	// use sky or region shadow rules to set up pssm_settings
	gfxGetPSSMSettingsFromSkyOrRegion(&sky_info->shadowValues, shadow_region_type, &gdraw->pssm_settings);

	setVec3same(gdraw->region_min, 8e16);
	setVec3same(gdraw->region_max, -8e16);
	for (i = 0; i < eaSize(&gdraw->regions); ++i)
	{
		Vec3 region_min, region_max;
		worldRegionGetVisibleBounds(gdraw->regions[i], region_min, region_max);
		vec3RunningMin(region_min, gdraw->region_min);
		vec3RunningMax(region_max, gdraw->region_max);

		eaPush(&gdraw->region_gdatas, worldRegionGetGraphicsData(gdraw->regions[i]));
	}

	gdraw->visual_pass = rdrDrawListGetPasses(gdraw->draw_list, &gdraw->passes, gdraw->visual_pass_view_z);
	gdraw->visual_frustum_bit = SAFE_MEMBER(gdraw->visual_pass, frustum_set_bit);
	gdraw->pass_count = eaSize(&gdraw->passes);

	gdraw->all_frustum_bits = 0;
	gdraw->shadow_frustum_bits = 0;
	for (i = 0; i < gdraw->pass_count; ++i)
	{
		RdrDrawListPassData *pass = gdraw->passes[i];
		gdraw->all_frustum_bits |= pass->frustum_set_bit;
		if (pass->shadow_light)
			gdraw->shadow_frustum_bits |= pass->frustum_set_bit;
	}

	PERFINFO_AUTO_STOP_START("gfxDoLightPreDraw", 1);

	gfxDoLightPreDraw();

	PERFINFO_AUTO_STOP_START("shadows", 1);
	if (gdraw->do_shadows)
	{
		wlPerfStartQueueBudget();
		gfxDoLightShadowsPreDraw(gfx_state.currentVisFrustum, gfx_state.currentCameraFocus, gdraw->draw_list);

		// shadows add passes, so get this data again
		gdraw->visual_pass = rdrDrawListGetPasses(gdraw->draw_list, &gdraw->passes, gdraw->visual_pass_view_z);
		gdraw->visual_frustum_bit = SAFE_MEMBER(gdraw->visual_pass, frustum_set_bit);
		gdraw->pass_count = eaSize(&gdraw->passes);

		gdraw->all_frustum_bits = 0;
		gdraw->shadow_frustum_bits = 0;
		for (i = 0; i < gdraw->pass_count; ++i)
		{
			RdrDrawListPassData *pass = gdraw->passes[i];
			gdraw->all_frustum_bits |= pass->frustum_set_bit;
			if (pass->shadow_light)
				gdraw->shadow_frustum_bits |= pass->frustum_set_bit;
		}
		wlPerfEndQueueBudget();
	}
	else
	{
		gfxClearShadowSearchData();
	}

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP_FUNC_PIX();

	return true;
}

bool gfxStartActionQuery(GfxActionType action_type, int num_actions)
{
	int count=0;

	assert(action_type > GfxAction_Unspecified && action_type < GfxAction_MAX);

	if (gfx_state.currentDevice->skipThisFrame)
		return false;

	assert(max_actions_per_frame[action_type] >= num_actions); // Otherwise this could never succeed, maybe should allow == 0 for debugging?

	FOR_EACH_IN_EARRAY(gfx_state.currentDevice->actions, GfxRenderAction, action)
	{
		assert(action->action_type != GfxAction_Unspecified);
		if (action->action_type == action_type)
			count++;
	}
	FOR_EACH_END;

	if (count + num_actions - 1 >= max_actions_per_frame[action_type])
	{
		return false;
	} else {
		return true;
	}
}

void gfxFinishAction(void)
{
	if (!gfx_state.currentAction)
	{
		gfxSetActiveCameraView(gfx_state.currentDevice->primaryCameraView, false);
		gfxSetActiveCameraController(gfx_state.currentDevice->primaryCameraController, false);
		return;
	}

	if (!gfx_state.currentAction->is_offscreen)
	{
		gfxQueueSimplePrims();
	}

	// reset frustum matrices
	gfxSetCurrentRegion(0);

	gfxStartZCreateZHierarchy();

	if (gfx_state.currentAction->gdraw.do_shadows)
		gfxDoLightShadowsCalcProjMatrices(gfx_state.currentVisFrustum);

	if (!gfx_state.debug.delay_sending_lights)
	{
		const RdrLightDefinition **light_defs = gfxGetLightDefinitionArray(rdrGetDeviceIdentifier(gfx_state.currentDevice->rdr_device));
		rdrDrawListSendLightsToRenderer(gfx_state.currentDevice->rdr_device, gfx_state.currentAction->gdraw.draw_list, light_defs);
		rdrDrawListSendLightsToRenderer(gfx_state.currentDevice->rdr_device, gfx_state.currentAction->gdraw.draw_list_sky, light_defs);
		rdrDrawListSendLightsToRenderer(gfx_state.currentDevice->rdr_device, gfx_state.currentAction->gdraw.draw_list_ao, light_defs);
	}

	gfx_state.currentAction = NULL;
	gfx_state.currentActionIdx = 0;
	gfxSetActiveCameraView(gfx_state.currentDevice->primaryCameraView, false);
	gfxSetActiveCameraController(gfx_state.currentDevice->primaryCameraController, false);

	gfxSetCurrentRegion(-1);
}

static void gfxAddEditableTerrainToList(WorldRegion *region, BlockRange ***terrain_blocks)
{
	int i, j, k;
	BlockRange **full_list = NULL;
	static BlockRange **orphaned_list = NULL;

	//Add all editable blocks to the list
	for ( i=0; i < eaSize(&world_grid.maps); i++ )
	{
		ZoneMap *zone_map = world_grid.maps[i];
		for (j = zmapGetLayerCount(zone_map)-1; j >= 0; --j)
		{
			ZoneMapLayer *layer = zmapGetLayer(zone_map, j);
			if (layer && region == layerGetWorldRegion(layer))
			{
				for (k = layerGetTerrainBlockCount(layer)-1; k >= 0; --k)
				{
					if(layerGetMode(layer) >= LAYER_MODE_TERRAIN)
						eaPush(terrain_blocks, (BlockRange*)layerGetTerrainBlock(layer, k));
					eaPush(&full_list, (BlockRange*)layerGetTerrainBlock(layer, k));
				}
			}
		}
	}

	//Add all heightmaps that don't belong to any layer into the list
	if(region->atlases)
	{
		for ( j=0; j < eaSize(&region->atlases->all_atlases); j++ )
		{
			HeightMapAtlas *atlas = region->atlases->all_atlases[j];
			//Only care about tree leaves
			if(!atlas->children[0] && !atlas->children[1] && !atlas->children[2] && !atlas->children[3]) 
			{
				bool found = false;
				//See if this exists in a layer
				for ( k=0; k < eaSize(&full_list); k++ )
				{
					if(	atlas->local_pos[0] >= full_list[k]->min_block[0] &&
						atlas->local_pos[0] <= full_list[k]->max_block[0] &&
						atlas->local_pos[1] >= full_list[k]->min_block[2] &&
						atlas->local_pos[1] <= full_list[k]->max_block[2] )
					{
						found = true;
						break;
					}
				}
				if(!found)
				{
					BlockRange *block = NULL;
					//Check if we already have a block for this height map
					for ( k=0; k < eaSize(&orphaned_list); k++ )
					{
						BlockRange *check_block = orphaned_list[k];
						if(	atlas->local_pos[0] == check_block->min_block[0] &&
							atlas->local_pos[1] == check_block->min_block[2] )
						{
							block = check_block;
							break;
						}
					}
					//Make one if we don't have it
					if(!block)
					{
						block = calloc(1, sizeof(BlockRange));
						block->min_block[0] = block->max_block[0] = atlas->local_pos[0];
						block->min_block[2] = block->max_block[2] = atlas->local_pos[1];
						eaPush(&orphaned_list, block);
					}
					eaPush(terrain_blocks, block);
				}
			}
		}
		
	}

	eaDestroy(&full_list);
}

//////////////////////////////////////////////////////////////////////////
// This function fills the draw list for the current action:
// - queues sky objects
// - opens/closes world cells (streaming)
// - traverses and queues static world objects
// - traverses and queues dynamic objects
//////////////////////////////////////////////////////////////////////////
void gfxFillDrawList(bool draw_world, WorldCellScaleFunc cell_scale_func)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	int i;

	if (!gdraw || gfx_state.currentDevice->skipThisFrame)
		return;

	PERFINFO_AUTO_START_FUNC_PIX();

	// queue UI fx for use by main action and add primitive geometry to draw list
	if (!gfx_state.currentAction->is_offscreen)
	{
		rdrDrawListBeginFrame(gfx_state.currentDevice->draw_list2d, false, false, false, false, false, false, false, RDLLM_NORMAL, false, false, 0, 8e16, 1, 0);
		rdrDrawListAddPass(gfx_state.currentDevice->draw_list2d, NULL, RDRSHDM_VISUAL, NULL, 0, -1, false);
		gfxDynamicsQueueAllUI(gfx_state.currentDevice->draw_list2d);
	}

	if (gfx_state.currentAction->draw_sky)
		gfxSkyQueueDrawables(gfx_state.currentCameraView);

	if (draw_world && !gfx_state.currentCameraController->override_disable_3D && eaSize(&gdraw->regions))
	{
		wlPerfStartQueueBudget();
		for (i = 0; i < eaSize(&gdraw->regions); ++i)
		{
			int world_cells_have_changed;
			F32 scale = cell_scale_func ? cell_scale_func(&gdraw->camera_positions[i*3]) : 1.0f;
			
			eaClear(&gdraw->terrain_blocks);

			if (terrain_state.source_data)
				gfxAddEditableTerrainToList(gdraw->regions[i], &gdraw->terrain_blocks);

			world_cells_have_changed = worldGridOpenAllCellsForCameraPos(PARTITION_CLIENT, gdraw->regions[i],
				&gdraw->camera_positions[i*3], 1, gdraw->terrain_blocks, s_OpenAllWorldCells, false,
				gfx_state.settings.draw_high_detail, 
				gfx_state.settings.draw_high_fill_detail, gfx_state.frame_count, scale);
			if (world_cells_have_changed)
				gfxLightDebuggerClear();
		}
		worldFXUpdateOncePerFrame(gfx_state.client_loop_timer, gfx_state.client_frame_timestamp);
		wlPerfStartQueueWorldBudget();
		gfxDrawWorldCombined();

		if (gfx_state.screenshot_material_name[0])
		{
			Material * capture_material = materialFindNoDefault(gfx_state.screenshot_material_name, WL_FOR_UTIL);
			if (!capture_material)
				ErrorFilenamef(gfx_state.screenshot_material_name, "Couldn't find material \"%s\".",
					gfx_state.screenshot_material_name);
			else
				gfxHeadshotCaptureMaterial( "MaterialCapture", 1024, 1024, capture_material, ColorTransparent);
			strcpy(gfx_state.screenshot_material_name, "");
		}

		wlPerfEndQueueWorldBudget();

		gfxRenderSpriteList3D(gfx_state.currentDevice->sprite_list, gdraw->draw_list, false);

		wlPerfEndQueueBudget();
	}

	if (!gfx_state.currentAction->is_offscreen)
		gfxUFDraw3DDebug(gdraw->cam_pos);

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

void gfxCheckPerRegionBudgets(void)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	if (gdraw && eaSize(&gdraw->regions))
	{
		static WorldRegionType last_wrt = -1;
		WorldRegionType wrt = worldRegionGetType(gdraw->regions[0]);
		if (wrt != last_wrt)
		{
			const char *type_name;
			char new_budget_file[MAX_PATH];
			strcpy(new_budget_file, "client/budgets/BudgetsGameClient.txt");
			last_wrt = wrt;
			type_name = StaticDefineIntRevLookup(WorldRegionTypeEnum, wrt);
			if (type_name)
			{
				sprintf(new_budget_file, "client/budgets/Budgets%s.txt", type_name);
				if (!fileExists(new_budget_file))
					strcpy(new_budget_file, "client/budgets/BudgetsGameClient.txt");
			}
			memBudgetOverrideBudgets(new_budget_file);
		}
	}
}

// Basic operation:  See http://code:8081/display/cs/GraphicsLibDoc
void gfxDrawFrame(void)
{
	int i;

	// Please put no complicated control logic in this function

	wlPerfStartDrawBudget();
	PERFINFO_AUTO_START_FUNC_PIX();

	// DO NOT MOVE THIS CALL. sometimes (!) additional input will
	// happen at the end of gfxDrawFrame and we don't want it to
	// be erased at the beginning of the next frame. --TomY
	//
	// Note: I moved this call into the lib so that other
	// solutions will still work.
	inpClearBuffer(gfxGetActiveInputDevice());

	gfxOncePerFramePerDevice();

	if (gfx_state.currentAction)
		gfxFinishAction();

	if (gfx_state.currentDevice->skipThisFrame) {
		goto cleanup_and_return;
	}

	if (rdrIsDeviceInactive(gfx_state.currentDevice->rdr_device))
	{
		// Can't render until we can reactivate the device. However, some functions
		// must still execute. That code must reside here if it is not handled in
		// the cleanup case.
		gfxLockActiveDeviceEx(false); // lock the device
		gfxGeoOncePerFramePerDevice();
		texOncePerFramePerDevice();
		gfxUnlockActiveDeviceEx(true, false, false); // unlock
		goto cleanup_and_return;
	}

	gfx_state.currentAction = NULL;
	gfx_state.currentActionIdx = 0;

	//////////////////////////////////////////////////////////////////////////
	// device is locked after this line
	//////////////////////////////////////////////////////////////////////////

	// drawing
	g_no_sprites_allowed = true;


	if (gfx_state.currentHookState.pfnHookEndFrame)
	{
		if (!gfx_state.currentHookState.pfnHookEndFrame())
		{
			// transition to next state (which may be reset to normal state)
			gfx_state.currentHookState = gfx_state.nextHookState;
			gfx_state.nextHookState.pfnHookBeginFrame = NULL;
			gfx_state.nextHookState.pfnHookEndFrame = NULL;
		}
	}
	else
	do {
		gfxNVPerfStartFrame();
		gfxSetActiveSurface(rdrGetPrimarySurface(gfx_state.currentDevice->rdr_device));
		gfxLockActiveDeviceEx(true); // lock the device

		gfxGeoOncePerFramePerDevice();

		for (i = 0; i < eaSize(&gfx_state.currentDevice->actions); ++i)
		{
			gfx_state.currentAction = gfx_state.currentDevice->actions[i];
			gfx_state.currentActionIdx = i;

			// Needs to be done with viewporting
			if (gfx_state.currentAction->action_type == GfxAction_Primary)
			{
				devassert(i == (eaSize(&gfx_state.currentDevice->actions)-1)); //the primary action must be the last action or else we need to do extra blits on xbox
				gfx_state.currentAction = NULL;
				gfx_state.currentActionIdx = 0;
				gfxUFHeadshotPostRendering(); //this must be done before the primary action but after the headshot actions
				gfx_state.currentAction = gfx_state.currentDevice->actions[i];
				gfx_state.currentActionIdx = i;
			}

			if (gfx_state.anaglyph_hack)
				gfxAnaglyphCameraHack();

			gfxSetActiveCameraView(gfx_state.currentAction->cameraView, false);
			gfxSetActiveCameraController(gfx_state.currentAction->cameraController, false);
			
			gfxUFDoAction();

			
		}

		gfx_state.currentAction = NULL;
		gfx_state.currentActionIdx = 0;

		gfxSetActiveSurface(rdrGetPrimarySurface(gfx_state.currentDevice->rdr_device));
		
		if (gfx_state.screenshot_type == SCREENSHOT_WITH_DEBUG)
		{
			PERFINFO_AUTO_START("screenshot_ui",1);
			gfxDoScreenshotWithCallbacksAndStuff();		
			PERFINFO_AUTO_STOP();
		}
		if (gfx_state.bodysockatlas_screenshot)
		{
			if (gfxImposterAtlasDumpBodysockAtlasToFile(gfx_state.bodysockatlas_filename))
				gfx_state.bodysockatlas_screenshot = false;
		}

		gfxUnlockActiveDeviceEx(true, true, true); // unlock and present
		gfxNVPerfEndFrame();

	} while (gfxNVPerfContinue());

	if (gfxIsHookedNextFrame())
	{
		// transition to next state (which may be reset to normal state)
		gfx_state.currentHookState = gfx_state.nextHookState;
	}

	g_no_sprites_allowed = false;

cleanup_and_return:
	// free geos after drawing, just in case they were in the draw list somehow
	gfxGeoDoQueuedFrees();

	eaClearEx(&gfx_state.currentDevice->actions, clearAction);
	gfx_state.currentDevice->next_available_offscreen_action = 0;
	wlVolumeWaterClearReloadedThisFrame();
	gfxUFHeadshotCleanup();
	gfxPrimitiveCleanupPerFrame();

	PERFINFO_AUTO_STOP_FUNC_PIX();
	wlPerfEndDrawBudget();
}
