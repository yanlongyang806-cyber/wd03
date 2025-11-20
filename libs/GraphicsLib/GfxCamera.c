#include "GfxCamera.h"
#include "GraphicsLibPrivate.h"
#include "GfxSky.h"
#include "WorldLib.h"
#include "inputMouse.h"
#include "inputGamepad.h"
#include "inputLib.h"
#include "GfxOcclusion.h"
#include "GfxRecord.h"
#include "GfxLights.h"
#include "GfxWorld.h"
#include "WorldGrid.h"
#include "TimedCallback.h"
#include "dynFxManager.h"
#include "GfxPostprocess.h"
#include "GfxLightsPrivate.h"
#include "LineDist.h"
#include "partition_enums.h"
#include "GfxPrimitive.h"
#include "wininclude.h"
#include "GfxConsole.h"

#define FAR_PROJECTION_SCALE		75
#define SKY_CLIP_PROJECTION_SCALE	4250
#define SKY_PROJECTION_SCALE		8250

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

typedef struct DynFxSkyOverrideVolume
{
	WorldRegion *region;
	Vec3 start, dirvec;
	F32 veclen, radius;
	const char **sky_names; // string pooled
	const char *blame_filename; // pooled filename
	F32 sky_weight;
	bool linear_falloff;

	// TODO : falloff?

} DynFxSkyOverrideVolume;

#define MAX_FX_SKY_OVERRIDES 32

static DynFxSkyOverrideVolume fx_sky_overrides[MAX_FX_SKY_OVERRIDES];
static int num_sky_overrides = 0;


void gfxSkyVolumePush(const char*** sky_names_earray_ptr, WorldRegion *region, Vec3 start, Vec3 end, F32 radius, F32 weight, bool linear_falloff)
{
	if (num_sky_overrides < MAX_FX_SKY_OVERRIDES)
	{
		DynFxSkyOverrideVolume* pNewSkyVolume = &fx_sky_overrides[num_sky_overrides++];
		eaCopy(&pNewSkyVolume->sky_names, sky_names_earray_ptr);
		pNewSkyVolume->region = region;
		copyVec3(start, pNewSkyVolume->start);
		subVec3(end, start, pNewSkyVolume->dirvec);
		pNewSkyVolume->veclen = normalVec3(pNewSkyVolume->dirvec);
		pNewSkyVolume->radius = radius;
		pNewSkyVolume->sky_weight = weight;
		pNewSkyVolume->linear_falloff = linear_falloff;
	}
}

void gfxSkyVolumeListOncePerFrame(void)
{
	// Clear existing earrays
	int i;
	for (i=0; i<num_sky_overrides; ++i)
	{
		eaClear(&fx_sky_overrides[i].sky_names);
	}
	num_sky_overrides = 0;
}

void gfxSkyVolumeDebugDraw(void)
{
	int i;
	for (i=0; i<num_sky_overrides; ++i)
	{
		DynFxSkyOverrideVolume* pSkyVolume = &fx_sky_overrides[i];
		Vec3 vEnd;
		scaleAddVec3(pSkyVolume->dirvec, pSkyVolume->veclen, pSkyVolume->start, vEnd);
		gfxDrawCapsule3D(pSkyVolume->start, vEnd, pSkyVolume->radius, 12, ARGBToColor(0x80FF0000), 0.1f);
	}
}

/*
AUTO_COMMAND;
void testFxSkyOverride(const char *sky_name)
{
	DynFxSkyOverrideVolume *fx_override;

	eaDestroyEx(&fx_sky_overrides, NULL);

	if (!sky_name || !sky_name[0] || !gfxSkyCheckSkyExists(sky_name))
		return;

	fx_override = calloc(1, sizeof(*fx_override));
	fx_override->sky_name = allocAddString(sky_name);
	fx_override->sky_weight = 0.75f;
	fx_override->radius = 30;
	fx_override->veclen = 20;
	setVec3(fx_override->dirvec, 0, 1, 0);

	eaPush(&fx_sky_overrides, fx_override);
}
*/

//////////////////////////////////////////////////////////////////////////



static U32 sky_fade_volume_type;
static U32 water_volume_type;
static U32 fx_volume_type;

// this is made static, so that all camera views share the same volume cache.
// This is intended so that an active camera switch is equivalent to a camera move
// for the purposes of tracking volume overlaps in the volume query cache.
static WorldVolumeQueryCache *fx_volume_query;
static GfxCameraView* last_fx_volume_camera;  // This is so we can tell if the camera changed, and if so, do hard kills.
static bool fx_active_camera_changed;

static bool enable_cam_light = true;

static GfxCameraView **all_camera_views;

typedef enum SkyPriorities
{
	// top of the list has lowest priority
	REGION_SKY_PRIORITY,
	REGION_OVERRIDE_SKY_FADEOUT_PRIORITY,
	REGION_OVERRIDE_SKY_FADEIN_PRIORITY,
	VOLUME_SKY_FADEOUT_PRIORITY,
	VOLUME_SKY_FADEIN_PRIORITY,
	FX_SKY_FADE_PRIORITY,
	// bottom of the list has highest priority
} SkyPriorities;

// fade rate in percent (0-1) per second
#define FX_SKY_FADE_RATE 100


AUTO_RUN;
void gfxSetupVolumeTypeFlags( void )
{
    sky_fade_volume_type = wlVolumeTypeNameToBitMask("SkyFade");
    water_volume_type = wlVolumeTypeNameToBitMask( "Water" );
    fx_volume_type = wlVolumeTypeNameToBitMask( "FX" );
}

//////////////////////////////////////////////////////////////////////////
// camera view

static void cameraEnteredFXVolume(WorldVolume *volume, WorldVolumeQueryCache *query_cache)
{
	if (wlVolumeIsType(volume, fx_volume_type))
	{
		WorldVolumeEntry *entry = wlVolumeGetVolumeData(volume);
		WorldFXVolumeProperties *fx_volume_properties = entry->client_volume.fx_volume_properties;
		if (fx_volume_properties)
		{
			if (fx_volume_properties->fx_filter == WorldFXVolumeFilter_Camera2D || fx_volume_properties->fx_filter == WorldFXVolumeFilter_Camera3D)
			{
				if (GET_REF(fx_volume_properties->fx_entrance))
				{
					DynAddFxParams params = {0};
					params.fHue = fx_volume_properties->fx_entrance_hue;
					params.eSource = eDynFxSource_Volume;
					dynAddFx(dynFxGetUiManager(fx_volume_properties->fx_filter == WorldFXVolumeFilter_Camera3D), 
						REF_STRING_FROM_HANDLE_KNOWN_NONNULL(fx_volume_properties->fx_entrance), 
						&params);
				}
				if (GET_REF(fx_volume_properties->fx_maintained))
				{
					dynFxManAddMaintainedFX(dynFxGetUiManager(fx_volume_properties->fx_filter == WorldFXVolumeFilter_Camera3D), 
						REF_STRING_FROM_HANDLE_KNOWN_NONNULL(fx_volume_properties->fx_maintained), NULL,
						fx_volume_properties->fx_maintained_hue, 0, eDynFxSource_Volume);
				}
			}
		}
	}
}

static void cameraExitedFXVolume(WorldVolume *volume, WorldVolumeQueryCache *query_cache)
{
	if (wlVolumeIsType(volume, fx_volume_type))
	{
		WorldVolumeEntry *entry = wlVolumeGetVolumeData(volume);
		WorldFXVolumeProperties *fx_volume_properties = entry->client_volume.fx_volume_properties;
		if (fx_volume_properties)
		{
			if (fx_volume_properties->fx_filter == WorldFXVolumeFilter_Camera2D || fx_volume_properties->fx_filter == WorldFXVolumeFilter_Camera3D)
			{
				if (GET_REF(fx_volume_properties->fx_exit))
				{
					DynAddFxParams params = {0};
					params.fHue = fx_volume_properties->fx_exit_hue;
					params.eSource = eDynFxSource_Volume;
					dynAddFx(dynFxGetUiManager(fx_volume_properties->fx_filter == WorldFXVolumeFilter_Camera3D), 
						REF_STRING_FROM_HANDLE_KNOWN_NONNULL(fx_volume_properties->fx_exit), 
						&params);
				}
				if (GET_REF(fx_volume_properties->fx_maintained))
				{
					dynFxManRemoveMaintainedFX(dynFxGetUiManager(fx_volume_properties->fx_filter == WorldFXVolumeFilter_Camera3D), 
						REF_STRING_FROM_HANDLE_KNOWN_NONNULL(fx_volume_properties->fx_maintained), 
						fx_active_camera_changed);
				}
			}
		}
	}
}

static void gfxSkyVolumeFadeOut(GfxSkyData *sky_data, WorldSkyVolumeProperties *old_props)
{
	if (sky_data)
		gfxSkyGroupFadeTo(sky_data, &old_props->sky_group, -1, 0, old_props->fade_out_rate, VOLUME_SKY_FADEOUT_PRIORITY, false);
}

static void cameraExitedVolume(WorldVolume *volume, WorldVolumeQueryCache *query_cache)
{
	GfxCameraView *camera_view = wlVolumeQueryCacheGetData(query_cache);
	if (wlVolumeIsType(volume, sky_fade_volume_type))
	{
		WorldVolumeEntry *sky_entry = wlVolumeGetVolumeData(volume);
		if (sky_entry && camera_view->last_sky_fade_volume_entry == sky_entry)
		{
			// volume was freed, clear it from the camera
			GfxSkyData *sky_data = camera_view->sky_data;
			WorldSkyVolumeProperties *old_props = sky_entry->client_volume.sky_volume_properties;

			gfxSkyVolumeFadeOut(sky_data, old_props);
			camera_view->last_sky_fade_volume_entry = NULL;
		}
	}
}

void gfxInitRunningQuery(GfxRunningQuery *query)
{
	query->queries = 0;
	ZeroArray(query->history);
	query->idx = query->count = 0;
}

void gfxDeinitRunningQuery(GfxRunningQuery *query)
{
	eaDestroyEx(&query->queries, rdrFreeOcclusionQuery);
	query->count = 0;
	query->idx = 0;
}

void gfxOrphanRunningQueryData(GfxRunningQuery *query)
{
	// clear the pointers & counts as an existing (but different) object will remain
	// the owner of the data
	query->queries = NULL;
	query->count = 0;
	query->idx = 0;
}

void gfxInitCameraView(GfxCameraView *camera)
{
	static U32 camera_view_volume_query_type;
	
	camera->initialized = 1;

	if (!camera_view_volume_query_type)
		camera_view_volume_query_type = wlVolumeQueryCacheTypeNameToBitMask("GfxCameraView");

	if (!camera->sky_data)
		camera->sky_data = gfxSkyCreateSkyData();

	if (!camera->volume_query)
	{
		camera->volume_query = wlVolumeQueryCacheCreate(PARTITION_CLIENT, camera_view_volume_query_type, camera);
		wlVolumeQuerySetCallbacks(camera->volume_query, NULL, cameraExitedVolume, NULL);
	}
	
	if (!fx_volume_query)
	{
		fx_volume_query = wlVolumeQueryCacheCreate(PARTITION_CLIENT, camera_view_volume_query_type, NULL);
		wlVolumeQuerySetCallbacks(fx_volume_query, cameraEnteredFXVolume, cameraExitedFXVolume, NULL);
	}

	camera->can_see_outdoors = true;
	camera->desired_light_range = 2;

	gfxInitRunningQuery(&camera->avg_luminance_query);
	gfxInitRunningQuery(&camera->hdr_point_query);
	
	copyMat4(unitmat, camera->frustum.cammat);
	copyMat4(unitmat, camera->frustum.viewmat);
	copyMat4(unitmat, camera->frustum.inv_viewmat);
	
	copyMat44(unitmat44, camera->projection_matrix);
	copyMat44(unitmat44, camera->far_projection_matrix);
	copyMat44(unitmat44, camera->sky_projection_matrix);

	
	camera->new_frustum = camera->frustum;

	eaPushUnique(&all_camera_views, camera);
}

void gfxCameraViewCreateOcclusion(GfxCameraView *camera)
{
	if (!camera->occlusion_buffer)
		camera->occlusion_buffer = zoCreate(&camera->new_frustum, true);
}

void gfxDeinitCameraView(GfxCameraView *camera)
{
	int i, j;

	for (i = 0; i < eaSize(&all_camera_views); ++i)
	{
		if (camera != all_camera_views[i] && camera->avg_luminance_query.queries)
		{
			assert(camera->avg_luminance_query.queries != all_camera_views[i]->avg_luminance_query.queries);
		}
	}

	for (i = 0; i < eaSize(&gfx_state.devices); ++i)
	{
		GfxPerDeviceState *device = gfx_state.devices[i];
		if (device)
		{
			assert(!device->main_frame_action || device->main_frame_action->cameraView != camera);
			for (j = 0; j < device->next_available_offscreen_action; ++j)
			{
				GfxRenderAction *action = device->offscreen_actions[j];
				assert(action->cameraView != camera);
			}
		}
	}

	gfxSkyDestroySkyData(camera->sky_data);
	camera->sky_data = NULL;

	zoDestroy(camera->occlusion_buffer);
	camera->occlusion_buffer = NULL;

	wlVolumeQueryCacheFree(camera->volume_query);
	camera->volume_query = NULL;

	gfxDeinitRunningQuery(&camera->avg_luminance_query);
	gfxDeinitRunningQuery(&camera->hdr_point_query);

	eaDestroyEx(&camera->fx_sky_groups, gfxSkyGroupFree);

	StructDestroySafe(parse_SkyInfoGroup, &camera->last_region_override_sky_group);

	eaFindAndRemoveFast(&all_camera_views, camera);
}

void gfxCameraSaveSettings(GfxCameraView * backup_camera, GfxCameraView * active_camera_state)
{
	*backup_camera = *active_camera_state;

	// Restore does not save the query state
	gfxOrphanRunningQueryData(&backup_camera->avg_luminance_query);
	gfxOrphanRunningQueryData(&backup_camera->hdr_point_query);
	backup_camera->fx_sky_groups = NULL;
}

void gfxCameraRestoreSettings(GfxCameraView * backup_camera, GfxCameraView * active_camera_state)
{
	int i;
	for (i = 0; i < eaSize(&all_camera_views); ++i)
	{
		if (backup_camera != all_camera_views[i] && backup_camera->avg_luminance_query.queries)
		{
			assert(backup_camera->avg_luminance_query.queries != all_camera_views[i]->avg_luminance_query.queries);
		}
	}

	gfxDeinitRunningQuery(&backup_camera->avg_luminance_query);
	gfxDeinitRunningQuery(&backup_camera->hdr_point_query);

	eaDestroyEx(&backup_camera->fx_sky_groups, gfxSkyGroupFree);

	// Restore does not replace the query state
	backup_camera->avg_luminance_query = active_camera_state->avg_luminance_query;
	backup_camera->hdr_point_query = active_camera_state->hdr_point_query;
	backup_camera->fx_sky_groups = active_camera_state->fx_sky_groups;

	*active_camera_state = *backup_camera;
}

void gfxSetActiveCameraView(GfxCameraView *camera, bool set_as_primary)
{
	if (!camera && gfx_state.currentDevice)
		camera = &gfx_state.currentDevice->autoCameraView;
	if(camera && !camera->initialized)
		gfxInitCameraView(camera);
	gfx_state.currentCameraView = camera;
	if (set_as_primary && gfx_state.currentDevice)
		gfx_state.currentDevice->primaryCameraView = camera;
}

void gfxFlipCameraFrustum(GfxCameraView *cameraView)
{
	globMovementLog("[gfx] Flipping to new frustum (%f,%f,%f) from old (%f,%f,%f).",
					vecParamsXYZ(cameraView->new_frustum.cammat[3]),
					vecParamsXYZ(cameraView->frustum.cammat[3]));

	globMovementLogCamera("camera.beforeFlip", cameraView->frustum.cammat);

	frustumCopy(&cameraView->frustum, &cameraView->new_frustum);

	globMovementLogCamera("camera.afterFlip", cameraView->frustum.cammat);
}

GfxCameraView *gfxGetActiveCameraView(void)
{
	return gfx_state.currentCameraView;
}

bool gfxWorldToScreenSpaceVector(SA_PARAM_NN_VALID GfxCameraView *pCameraView, const Vec3 vWorld, Vec2 vScreenOut, bool bClamp)
{
	bool bOnScreen = true;
	S32 iWidth, iHeight;
	Vec3 vView, vViewProj;
	
	gfxGetActiveSurfaceSizeInline(&iWidth, &iHeight);

	mulVecMat4(vWorld, pCameraView->frustum.viewmat, vView);
	mulVec3ProjMat44(vView, pCameraView->projection_matrix, vViewProj);

	if (vViewProj[2] <= -1.0f || vViewProj[2] >= 1.0f)	// glitch occurs around very large values of Z
	{
		vViewProj[0] *= -1.0f;	//special case where it needs to be reversed
		vViewProj[1] *= -1.0f;
	}

	if ( vViewProj[0] < -1.0f || vViewProj[0] > 1.0f || vViewProj[1] < -1.0f || vViewProj[1] > 1.0f )
	{
		if ( bClamp )
		{
			vViewProj[0] = CLAMPF32(vViewProj[0],-1.0f,1.0f);
			vViewProj[1] = CLAMPF32(vViewProj[1],-1.0f,1.0f);
		}
		bOnScreen = false;
	}

	vScreenOut[0] = (vViewProj[0] * 0.5f + 0.5f) * iWidth;
	vScreenOut[1] = iHeight - (vViewProj[1] * 0.5f + 0.5f) * iHeight;

	return bOnScreen;
}

F32 gfxGetAspectRatioOverride(F32 override_aspect)
{
	F32 aspect = override_aspect;
	if (aspect <= 0)
		aspect = gfxGetAspectRatio();
	return aspect;
}

void gfxSetActiveProjection(F32 fovy, F32 aspect)
{
	Mat44 scale_mat;
	Mat44 temp_mat;
	aspect = gfxGetAspectRatioOverride(aspect);

	rdrSetupPerspectiveProjection(gfx_state.currentCameraView->projection_matrix, fovy, aspect, gfx_state.near_plane_dist, gfx_state.far_plane_dist);
	rdrSetupPerspectiveProjection(temp_mat, fovy, aspect, gfx_state.near_plane_dist, gfx_state.far_plane_dist * FAR_PROJECTION_SCALE);
	copyMat44(unitmat44,scale_mat);
	scaleByMat44(scale_mat,FAR_PROJECTION_SCALE);
	scale_mat[3][3] = 1;
	mulMat44Inline(temp_mat,scale_mat,gfx_state.currentCameraView->far_projection_matrix);
	rdrSetupPerspectiveProjection(temp_mat, fovy, aspect, gfx_state.near_plane_dist, gfx_state.far_plane_dist * SKY_CLIP_PROJECTION_SCALE);
	copyMat44(unitmat44,scale_mat);
	scaleByMat44(scale_mat,SKY_PROJECTION_SCALE);
	scale_mat[3][3] = 1;
	mulMat44Inline(temp_mat,scale_mat,gfx_state.currentCameraView->sky_projection_matrix);
	frustumSet(&gfx_state.currentCameraView->new_frustum, fovy, aspect, gfx_state.near_plane_dist, gfx_state.far_plane_dist);
}

void gfxSetActiveProjectionOrtho(F32 ortho_zoom, F32 aspect)
{
	aspect = gfxGetAspectRatioOverride(aspect);

	rdrSetupOrthographicProjection(gfx_state.currentCameraView->projection_matrix, aspect, ortho_zoom);
	frustumSetOrtho(&gfx_state.currentCameraView->new_frustum, aspect, ortho_zoom, 0, 50000);
}

void gfxSetActiveProjectionOrthoEx(F32 width, F32 height, F32 cull_width, F32 cull_height, F32 near_dist, F32 far_dist, F32 aspect)
{
	F32 ortho_zoom = 1;
	aspect = gfxGetAspectRatioOverride(aspect);

	rdrSetupOrthoDX(gfx_state.currentCameraView->projection_matrix, 
					-aspect*width/2.0f, aspect*width/2.0f, 
					-height/2.0f, height/2.0f, 
					near_dist, far_dist);

	if(cull_width==0.0f || cull_height==0.0f)
	{
		ortho_zoom = MAX(width, height)/2.0f;
	}
	else
	{
		aspect = cull_width/cull_height;
		ortho_zoom = cull_height/2.0f;
	}

	frustumSetOrtho(&gfx_state.currentCameraView->new_frustum, aspect, ortho_zoom, near_dist, far_dist);
}

void gfxSetActiveCameraMatrix(const Mat4 camera_matrix, bool immediate)
{
	if (immediate)
	{
		frustumSetCameraMatrix(&gfx_state.currentCameraView->frustum, camera_matrix);
		globMovementLog("[gfx] Setting immediate camera matrix at pos (%f,%f,%f).",
						vecParamsXYZ(camera_matrix[3]));
		globMovementLogCamera("camera.immediate", camera_matrix);
	}
	else
	{
		frustumSetCameraMatrix(&gfx_state.currentCameraView->new_frustum, camera_matrix);
		globMovementLog("[gfx] Setting new camera matrix at pos (%f,%f,%f).",
						vecParamsXYZ(camera_matrix[3]));
		globMovementLogCamera("camera.new", camera_matrix);
	}
}

void gfxGetActiveCameraMatrix(Mat4 camera_matrix)
{
	copyMat4(gfx_state.currentCameraView->frustum.cammat, camera_matrix);
}

void gfxGetNextActiveCameraMatrix(Mat4 camera_matrix)
{
	copyMat4(gfx_state.currentCameraView->new_frustum.cammat, camera_matrix);
}

void gfxGetActiveCameraPos(Vec3 pos)
{
	if ( gfx_state.currentCameraView )
		copyVec3(gfx_state.currentCameraView->frustum.cammat[3], pos);
}

void gfxSetActiveCameraYPR(const Vec3 pyr)
{
	Mat4 camera_matrix;
	createMat3YPR(camera_matrix, pyr);
	gfxGetActiveCameraPos(camera_matrix[3]);
	gfxSetActiveCameraMatrix(camera_matrix, false);
}

void gfxSetActiveCameraYPRPos(const Vec3 pyr, const Vec3 pos, bool immediate)
{
    Mat4 camera_matrix;
    createMat3YPR(camera_matrix, pyr);
    copyVec3(pos, camera_matrix[3]);
	gfxSetActiveCameraMatrix(camera_matrix, immediate);
}

void gfxGetActiveCameraYPR(Vec3 pyr)
{
	getMat3YPR(gfx_state.currentCameraView->frustum.cammat, pyr);
}

static SkyInfoGroup *createSkyGroup(const char *sky_name, const char *blame_filename)
{
	SkyInfoGroup *sky_group;

	if (!sky_name)
		return NULL;

	sky_group = StructCreate(parse_SkyInfoGroup);
	gfxSkyGroupAddOverride(sky_group, sky_name);
	if (blame_filename && !gfxSkyCheckSkyExists(sky_name))
		ErrorFilenamef(blame_filename, "Unknown sky file \"%s\".", sky_name);

	return sky_group;
}

void gfxCameraClearLastRegionData(GfxCameraView *camera_view) {
	camera_view->last_region_sky_group = NULL;
	camera_view->last_region_override_sky_group = NULL;
	camera_view->in_water_this_frame = NULL;
	camera_view->last_sky_fade_volume_entry = NULL;
	gfxSkyClearActiveSkyGroups(camera_view->sky_data);
}

void gfxUpdateActiveCameraViewVolumes(WorldRegion* region)
{
	static SkyInfoGroup **new_fx_sky_groups;
	GfxCameraView *camera_view = gfx_state.currentCameraView;
	GfxCameraController *camera_controller = gfx_state.currentCameraController;
	SkyInfoGroup *region_sky_group, *region_override_sky_group;
	GfxSkyData *sky_data = camera_view->sky_data;
	bool prevInWaterCompletely = camera_view->in_water_completely_distance > 0;
	F32 region_sky_override_fade_percent, region_sky_override_fade_rate;
	int i, j;

	if (!region)
		region = worldGetWorldRegionByPos(camera_controller->camfocus);
	region_sky_group = worldRegionGetSkyGroup(region);
	region_override_sky_group = worldRegionGetSkyOverride(region, &region_sky_override_fade_percent, &region_sky_override_fade_rate);

	if (camera_controller->sky_group_override)
	{
		gfxSkyClearActiveSkyGroups(sky_data);
		camera_view->last_region_sky_group = NULL;
		region_sky_group = camera_controller->sky_group_override;
		region_override_sky_group = NULL;
		camera_view->in_water_this_frame = NULL;
		camera_view->last_sky_fade_volume_entry = NULL;
	}
	else
	{
		WorldVolumeEntry *sky_entry = NULL;
		WorldSkyVolumeProperties *new_props = NULL;
		F32 fZProjection = -FLT_MAX;
		const WorldVolume **volumes;

		// first do a check for water volumes, since those are callback based and handled differently
		fx_active_camera_changed = (last_fx_volume_camera != camera_view);
		last_fx_volume_camera = camera_view;
		wlVolumeCacheQuerySphereByType(fx_volume_query, camera_view->frustum.cammat[3], 0, fx_volume_type);

		// clear the volume callbacks while doing the query, because we want to handle the changes here
		wlVolumeQuerySetCallbacks(camera_view->volume_query, NULL, NULL, NULL);

		volumes = wlVolumeCacheQuerySphereByType(camera_view->volume_query, camera_view->frustum.cammat[3], 2 * gfx_state.near_plane_dist, water_volume_type);
		camera_view->in_water_this_frame = NULL;
		for (i = 0; i < eaSize(&volumes); ++i)
		{
			if (wlVolumeIsType(volumes[i], water_volume_type))
			{
				WorldVolumeEntry *entry = wlVolumeGetVolumeData(volumes[i]);
				if(!entry->fx_condition || entry->fx_condition_state)
				{
					camera_view->in_water_this_frame = wlVolumeWaterFromKey(SAFE_MEMBER(entry->client_volume.water_volume_properties, water_def));
					wlVolumeGetWorldMinMax(volumes[i], NULL, camera_view->water_plane_pos);
					break;
				}
			}
		}
	    
		volumes = wlVolumeCacheQuerySphereByType(camera_view->volume_query, camera_view->frustum.cammat[3], 0, sky_fade_volume_type);
		for (i = 0; i < eaSize(&volumes); ++i)
		{
			if (wlVolumeIsType(volumes[i], sky_fade_volume_type))
			{
				WorldVolumeEntry *new_entry = wlVolumeGetVolumeData(volumes[i]);
				if (new_entry->client_volume.sky_volume_properties)
				{
					sky_entry = new_entry;
					new_props = SAFE_MEMBER(sky_entry, client_volume.sky_volume_properties);
					if (new_props->positional_fade)
					{
						fZProjection = wlVolumeGetProgressZ(volumes[i],camera_view->frustum.cammat[3]);
					}
					break;
				}
			}
		}

		if (gfx_state.debug.disable_sky_volumes)
			sky_entry = NULL;

		// set the volume callback again, so if a volume is freed we handle it correctly
		wlVolumeQuerySetCallbacks(camera_view->volume_query, NULL, cameraExitedVolume, NULL);

		if (sky_entry != camera_view->last_sky_fade_volume_entry)
		{
			WorldSkyVolumeProperties *old_props = SAFE_MEMBER(camera_view->last_sky_fade_volume_entry, client_volume.sky_volume_properties);
			bool same_sky_group = old_props && new_props && cmpSkyInfoGroup(&old_props->sky_group, &new_props->sky_group) == 0;
			F32 start_percent = -1;

			if (old_props)
			{
				if (same_sky_group) {
					start_percent = gfxSkyRemoveSkyGroup(sky_data, &old_props->sky_group); // just remove this one and start the new one at the same percentage
				} else {
					gfxSkyVolumeFadeOut(sky_data, old_props);
				}
			}

			if (new_props)
			{
				if (new_props->positional_fade)
				{
					gfxSkyGroupFadeTo(sky_data, &new_props->sky_group, start_percent, new_props->weight, -1.0f, VOLUME_SKY_FADEIN_PRIORITY, false);
				}
				else
				{
					gfxSkyGroupFadeTo(sky_data, &new_props->sky_group, start_percent, new_props->weight, new_props->fade_in_rate, VOLUME_SKY_FADEIN_PRIORITY, false);
				}
			}
			
			camera_view->last_sky_fade_volume_entry = sky_entry;
		}
		
		if (new_props && new_props->positional_fade)
		{
			// update the fade
   			gfxSkyGroupUpdatePositionalFade(sky_data, &new_props->sky_group,fZProjection*new_props->weight);
		}
	}

	// Update dependent water data
	camera_view->in_water_completely_distance = gfxInWaterCompletelyDistance(&camera_view->frustum);
	camera_view->in_water_completely_fade_time -= gfx_state.frame_time;
	MAX1(camera_view->in_water_completely_fade_time, 0); 
	if (prevInWaterCompletely != camera_view->in_water_completely_distance > 0)
	{
		camera_view->in_water_completely_fade_time = GFX_WATER_ADAPTATION_TIME;
	}

	// update region sky group if it has changed
	if (camera_view->last_region_sky_group != region_sky_group)
	{
		gfxSkyClearVisibleSky(sky_data); // pop to new sky, don't fade
		gfxSkyRemoveSkyGroup(sky_data, camera_view->last_region_sky_group);

		// set new sky
		gfxSkyGroupFadeTo(sky_data, region_sky_group, 1, 1, 1, REGION_SKY_PRIORITY, false);
		camera_view->last_region_sky_group = region_sky_group;
	}

	// update region override sky group
	{
		bool same_override_sky = camera_view->last_region_override_sky_group && region_override_sky_group && cmpSkyInfoGroup(camera_view->last_region_override_sky_group, region_override_sky_group) == 0;

		if (same_override_sky)
		{
			// change fade percent and fade rate
			F32 start_percent = gfxSkyRemoveSkyGroup(sky_data, camera_view->last_region_override_sky_group);
			camera_view->last_region_override_sky_fade_rate = region_sky_override_fade_rate;
			gfxSkyGroupFadeTo(sky_data, camera_view->last_region_override_sky_group, start_percent, region_sky_override_fade_percent, camera_view->last_region_override_sky_fade_rate, REGION_OVERRIDE_SKY_FADEIN_PRIORITY, false);
		}
		else
		{
			if (camera_view->last_region_override_sky_group)
			{
				// fade out and remove old sky group
				gfxSkyGroupFadeTo(sky_data, camera_view->last_region_override_sky_group, -1, 0, camera_view->last_region_override_sky_fade_rate, REGION_OVERRIDE_SKY_FADEOUT_PRIORITY, true);
				camera_view->last_region_override_sky_group = NULL; // not a memory leak, it was queued to be freed by the fade out call above
			}

			if (region_override_sky_group)
			{
				// copy and fade in the new sky group
				camera_view->last_region_override_sky_group = StructClone(parse_SkyInfoGroup, region_override_sky_group);
				camera_view->last_region_override_sky_fade_rate = region_sky_override_fade_rate;
				gfxSkyGroupFadeTo(sky_data, camera_view->last_region_override_sky_group, 0, region_sky_override_fade_percent, camera_view->last_region_override_sky_fade_rate, REGION_OVERRIDE_SKY_FADEIN_PRIORITY, false);
			}
		}

	}

	if(!camera_controller->sky_group_override) {

		// update FX sky overrides
		eaSetSize(&new_fx_sky_groups, 0);
		for (i = 0; i < num_sky_overrides; ++i)
		{
			SkyInfoGroup *sky_group = NULL;
			F32 weight;

			if (fx_sky_overrides[i].region == NULL || fx_sky_overrides[i].region != region)
				continue;

			if (fx_sky_overrides[i].radius > 0.0f) // a radius of 0 means global
			{
				weight = PointLineDistSquared(camera_view->frustum.cammat[3], fx_sky_overrides[i].start, fx_sky_overrides[i].dirvec, fx_sky_overrides[i].veclen, NULL);

				if (weight >= SQR(fx_sky_overrides[i].radius))
					continue;

				if (fx_sky_overrides[i].linear_falloff)
				{
					weight = sqrtf(weight) / MAX(fx_sky_overrides[i].radius, 0.0001f);
					weight = fx_sky_overrides[i].sky_weight * saturate(1 - weight);
				}
				else
					weight = fx_sky_overrides[i].sky_weight;
			}
			else
				weight = fx_sky_overrides[i].sky_weight;

			for (j = 0; j < eaSize(&camera_view->fx_sky_groups); ++j)
			{
				// Check if an existing sky group is the same as our new fx_sky_override, by comparing the list of names (same names, same order)
				bool bMisMatch = false;

				// Make sure the lists have the same number of elements
				if (eaSize(&camera_view->fx_sky_groups[j]->override_list) != eaSize(&fx_sky_overrides[i].sky_names))
					continue;

				// Compare each element in order
				FOR_EACH_IN_EARRAY_FORWARDS(camera_view->fx_sky_groups[j]->override_list, SkyInfoOverride, override)
				{
					const char* sky_name = REF_STRING_FROM_HANDLE(override->sky);
					const char* fx_sky_name = fx_sky_overrides[i].sky_names[ioverrideIndex];
					if (sky_name != fx_sky_name)
					{
						bMisMatch = true;
						break;
					}
				}
				FOR_EACH_END;

				if (bMisMatch)
					continue;

				// Found perfect match.
				sky_group = eaRemoveFast(&camera_view->fx_sky_groups, j);
				break;
			}

			if (!sky_group)
			{
				sky_group = createSkyGroup(fx_sky_overrides[i].sky_names[0], fx_sky_overrides[i].blame_filename);

				FOR_EACH_IN_EARRAY_FORWARDS(fx_sky_overrides[i].sky_names, const char, sky_name)
				{
					if (isky_nameIndex)
						gfxSkyGroupAddOverride(sky_group, sky_name);
				}
				FOR_EACH_END;
			}

			if (!sky_group)
				continue;

			gfxSkyGroupFadeTo(camera_view->sky_data, sky_group, -1, weight, FX_SKY_FADE_RATE, FX_SKY_FADE_PRIORITY, false);
			eaPush(&new_fx_sky_groups, sky_group);
		}

		// fade out old fx sky overrides (no leak here, the last parameter tells the sky system to free the sky group when it is faded out)
		for (j = 0; j < eaSize(&camera_view->fx_sky_groups); ++j)
			gfxSkyGroupFadeTo(camera_view->sky_data, camera_view->fx_sky_groups[j], -1, 0, FX_SKY_FADE_RATE, FX_SKY_FADE_PRIORITY, true);

		if (camera_view->fx_sky_groups) // don't allocate the fx_sky_groups earray unless necessary
			eaSetSize(&camera_view->fx_sky_groups, 0);
	
		if (eaSize(&new_fx_sky_groups) > 0) // don't allocate the fx_sky_groups earray unless necessary
			eaCopy(&camera_view->fx_sky_groups, &new_fx_sky_groups);
	}
}

void gfxCameraViewNotifySkyGroupFreed(SkyInfoGroup *sky_group)
{
	int i;
	for (i = 0; i < eaSize(&all_camera_views); ++i)
	{
		if (all_camera_views[i]->last_region_sky_group == sky_group)
			all_camera_views[i]->last_region_sky_group = NULL;
	}
}

//////////////////////////////////////////////////////////////////////////
// camera controller

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.forward") ACMD_HIDE;
void CommandCameraForward(int val)
{
	gfxGetActiveCameraController()->forward = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.backward") ACMD_HIDE;
void CommandCameraBackward(int val)
{
	gfxGetActiveCameraController()->backward = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.left") ACMD_HIDE;
void CommandCameraLeft(int val)
{
	gfxGetActiveCameraController()->left = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.right") ACMD_HIDE;
void CommandCameraRight(int val)
{
	gfxGetActiveCameraController()->right = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.up") ACMD_HIDE;
void CommandCameraUp(int val)
{
	gfxGetActiveCameraController()->up = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.down") ACMD_HIDE;
void CommandCameraDown(int val)
{
	gfxGetActiveCameraController()->down = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.zoom") ACMD_HIDE;
void CommandCameraZoom(int val)
{
	gfxGetActiveCameraController()->zoom = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.zoomstep") ACMD_HIDE;
void CommandCameraZoomStep(F32 zoomstep)
{
	gfxGetActiveCameraController()->zoomstep = zoomstep;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.pan") ACMD_HIDE;
void CommandCameraPan(int val)
{
	gfxGetActiveCameraController()->pan = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.panfast") ACMD_HIDE;
void CommandCameraPanFast(int val)
{
	gfxGetActiveCameraController()->pan = !!val;
	gfxGetActiveCameraController()->pan_fast = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.rotate") ACMD_HIDE;
void CommandCameraRotate(int val)
{
	gfxGetActiveCameraController()->rotate = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.freelook") ACMD_HIDE;
void CommandCameraFreeLook(int val)
{
	gfxGetActiveCameraController()->freelook = !!val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.notfreelook") ACMD_HIDE;
void CommandCameraNotFreeLook(int val)
{
	gfxGetActiveCameraController()->freelook = !val;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Camera.center") ACMD_HIDE;
void CommandCameraCenter(void)
{
	gfxGetActiveCameraController()->setcenter = 1;
}

AUTO_COMMAND ACMD_NAME("Camera.ortho");
void CommandCameraOrtho(bool ortho)
{
	gfxGetActiveCameraController()->ortho_mode = !!ortho;
}

AUTO_COMMAND ACMD_NAME("Camera.switch_mode");
void CommandCameraSwitchMode(bool mode)
{
	GfxCameraController *camera = gfxGetActiveCameraController();
	gfxCameraSwitchMode(camera, !camera->mode_switch);
}

AUTO_COMMAND ACMD_NAME("Camera.start_speed");
void CommandCameraStartSpeed(bool start_speed)
{
	gfxGetActiveCameraController()->start_speed = start_speed;
}
AUTO_COMMAND ACMD_NAME("Camera.max_speed");
void CommandCameraMaxSpeed(bool max_speed)
{
	gfxGetActiveCameraController()->max_speed = max_speed;
}
AUTO_COMMAND ACMD_NAME("Camera.acc_delay");
void CommandCameraAccDelay(bool acc_delay)
{
	gfxGetActiveCameraController()->acc_delay = acc_delay;
}
AUTO_COMMAND ACMD_NAME("Camera.acc_rate");
void CommandCameraAccRate(bool acc_rate)
{
	gfxGetActiveCameraController()->acc_rate = acc_rate;
}

static void gfxCamMultiDragPerFrame(GfxCameraController *camera)
{
	static bool right_click_rotating = false;

	// reset drag settings
	camera->pan = 0;
	camera->pan_fast = 0;
	camera->rotate = 0;
	camera->zoom = 0;
	camera->zoom_fast = 0;

	// don't need any of this input processing in freecam mode
	if (camera->mode_switch)
		camera->multi_drag = 0;

	// detect and process input chords
	if (camera->multi_drag)
	{
		bool inp_shift = !!inpLevelPeek(INP_SHIFT);
		bool inp_ctrl = !!inpLevelPeek(INP_CONTROL);
		bool inp_alt = !!inpLevelPeek(INP_ALT);
		if (mouseIsDown(MS_RIGHT))
		{
			camera->rotate = 1;
			right_click_rotating = true;
		}
		else if (right_click_rotating)
		{
			right_click_rotating = false;
			camera->multi_drag = false;
		}
		else
		{
			if (inp_ctrl && inp_alt)
			{
				camera->zoom = 1;
				if (inp_shift)
					camera->zoom_fast = 1;
			}
			else if (inp_alt)
				camera->rotate = 1;
			else
			{
				camera->pan = 1;
				if (inp_shift)
					camera->pan_fast = 1;
			}
		}
	} else {
		right_click_rotating = false;
	}
}

// this is the commmand used to support the new editor camera controls
// that require 3+ chords, which aren't currently supported in the keybind system
AUTO_COMMAND ACMD_NAME("Camera.multidrag");
void CommandCameraMultiDrag(int dragging)
{
	GfxCameraController *camera = gfxGetActiveCameraController();

	camera->multi_drag = !!dragging;
}

void gfxCameraHalt(GfxCameraController *camera)
{
	camera->multi_drag = 0;
	camera->forward = 0;
	camera->backward = 0;
	camera->left = 0;
	camera->right =0;
	camera->up = 0;
	camera->down = 0;
	camera->zoom = 0;
	camera->zoom_fast = 0;
	camera->pan = 0;
	camera->pan_fast = 0;
	camera->rotate = 0;
	camera->rotate_left = 0;
	camera->rotate_right = 0;
	camera->locked = 0;
}

// this resets all camera movement flags
AUTO_COMMAND ACMD_NAME("Camera.halt") ACMD_ACCESSLEVEL(4);
void CommandCameraHalt(void)
{
	gfxCameraHalt(gfxGetActiveCameraController());
}

AUTO_COMMAND ACMD_NAME("Camera.spawn_player");
void gfxSpawnPlayerAtCamera(void)
{
	Mat4 cam;
	Vec3 pyr;
	
	gfxGetActiveCameraMatrix(cam);

	getMat3YPR(cam, pyr);
	globCmdParsef("setpos %f %f %f", vecParamsXYZ(cam[3]));
	globCmdParsef("setpyr 0 %f 0", addAngle(pyr[1], PI));
	globCmdParsef("setGameCamPYR %f %f %f", vecParamsXYZ(pyr));
}

AUTO_COMMAND ACMD_NAME("FreeCamera.spawn_player") ACMD_ACCESSLEVEL(4);
void gfxSpawnPlayerAtCameraAndLeaveFreecam(void)
{
	gfxSpawnPlayerAtCamera();
	TimedCallback_RunCmdParse("freecam 0", 0.5f);
}

void gfxEnableCameraLight(bool enable)
{
	enable_cam_light = !!enable;
}

void gfxUpdateCameraLight(void)
{
	GfxCameraView *cam = gfx_state.currentCameraView;
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
    const WorldRegion *world_region;
    const CamLightRules *cam_light_rules;

	if (!cam || !world_data) {
		return;
	}

    world_region = worldGetWorldRegionByPos(cam->frustum.cammat[3]);
    cam_light_rules = worldRegionRulesGetCamLightRules(worldRegionGetRules(world_region));

	if (enable_cam_light && !gfx_state.disable_cam_light && cam_light_rules->enabled) {
        LightData light_data = {0};

		light_data.light_type = WL_LIGHT_POINT;
		light_data.key_light = 1;
		light_data.dynamic = 1;
		light_data.inner_radius = cam_light_rules->innerRadius;
		light_data.outer_radius = cam_light_rules->outerRadius;

		copyVec3(unitmat[0], light_data.world_mat[0]);
		copyVec3(unitmat[1], light_data.world_mat[1]);
		copyVec3(unitmat[2], light_data.world_mat[2]);
		mulVecMat4(cam_light_rules->offset, cam->frustum.cammat, light_data.world_mat[3]);

		copyVec3(cam_light_rules->diffuseHsv, light_data.diffuse_hsv);
		copyVec3(cam_light_rules->specularHsv, light_data.specular_hsv);
		copyVec3(cam_light_rules->ambientHsv, light_data.ambient_hsv);

		world_data->cam_light = gfxUpdateLight(world_data->cam_light, &light_data, 10000.0f, NULL);
		world_data->cam_light->key_override = cam_light_rules->keyOverride;
	} else if (world_data->cam_light) {
		gfxRemoveLight(world_data->cam_light);
		world_data->cam_light = NULL;
	}
}

//sets up the camera controller
//user_data stores user settings
void gfxInitCameraController(GfxCameraController *camera, GfxCameraControllerFunc func, void *user_data )
{	
	camera->default_projection_fov = camera->projection_fov = camera->target_projection_fov = gfxGetDefaultFOV();

	// default parameters
	camera->speed = camera->start_speed = 1.f;
	camera->delay = 0;

#if _PS3
	camera->max_speed = 30;
#elif _XBOX
	camera->max_speed = 30;
#else
	camera->max_speed = 120;
#endif
	camera->acc_delay = 0;
	camera->acc_rate = 12;
	camera->inited = 0;
	camera->camera_func = func;

	camera->user_data = user_data;
}

void gfxDeinitCameraController(GfxCameraController *camera)
{
	int i, j;

	for (i = 0; i < eaSize(&gfx_state.devices); ++i)
	{
		GfxPerDeviceState *device = gfx_state.devices[i];
		if (device)
		{
			assert(!device->main_frame_action || device->main_frame_action->cameraController != camera);
			for (j = 0; j < device->next_available_offscreen_action; ++j)
			{
				GfxRenderAction *action = device->offscreen_actions[j];
				assert(action->cameraController != camera);
			}
		}
	}

	if (camera->sky_group_override)
	{
		gfxSkyGroupFree(camera->sky_group_override);
		camera->sky_group_override = NULL;
	}

	ZeroStruct(camera);
}

// Return a camera to use its default values
void gfxResetCameraControllerState(GfxCameraController* camera)
{
	camera->rotate = 0;
	camera->rotate_down = 0;
	camera->rotate_left = 0;
	camera->rotate_right = 0;
	camera->rotate_up = 0;
}

void gfxCameraControllerSetSkyGroupOverride(GfxCameraController *camera, const char **sky_names, const char **blame_filenames)
{
	int i;
	if (camera->sky_group_override)
	{
		if(eaSize(&sky_names) == eaSize(&camera->sky_group_override->override_list))
		{
			bool changed = false;
			for( i=0; i < eaSize(&sky_names); i++ )
			{
				const char *cur_sky_name = REF_STRING_FROM_HANDLE(camera->sky_group_override->override_list[0]->sky);
				if (!cur_sky_name || stricmp(cur_sky_name, sky_names[i]) != 0)
				{
					changed = true;
					break;
				}
			}
			if(!changed)
				return;
		}
		gfxSkyGroupFree(camera->sky_group_override);
		camera->sky_group_override = NULL;
	}

	if (sky_names)
	{
		if(blame_filenames && eaSize(&sky_names) != eaSize(&blame_filenames))
			return;

		camera->sky_group_override = StructCreate(parse_SkyInfoGroup);
		for( i=0; i < eaSize(&sky_names) ; i++ )
		{
			gfxSkyGroupAddOverride(camera->sky_group_override, sky_names[i]);
			if (blame_filenames && blame_filenames[i] && !gfxSkyCheckSkyExists(sky_names[i]))
				ErrorFilenamef(blame_filenames[i], "Unknown sky file \"%s\".", sky_names[i]);
		}
	}
}

void gfxCameraControllerSetSkyOverride(GfxCameraController *camera, const char *sky_name, const char *blame_filename)
{
	if (camera->sky_group_override)
	{
		if (camera->sky_group_override && sky_name && eaSize(&camera->sky_group_override->override_list) > 0)
		{
			const char *cur_sky_name = REF_STRING_FROM_HANDLE(camera->sky_group_override->override_list[0]->sky);
			if (cur_sky_name && stricmp(cur_sky_name, sky_name) == 0)
			{
				// no change
				return;
			}
		}

		gfxSkyGroupFree(camera->sky_group_override);
		camera->sky_group_override = NULL;
	}

	camera->sky_group_override = createSkyGroup(sky_name, blame_filename);
}

void gfxSetActiveCameraController(GfxCameraController *camera, bool set_as_primary)
{
	gfx_state.currentCameraController = camera;
	if (set_as_primary && gfx_state.currentDevice)
		gfx_state.currentDevice->primaryCameraController = camera;
}

GfxCameraController *gfxGetActiveCameraController(void)
{
	return gfx_state.currentCameraController;
}

void gfxActiveCameraControllerSetFOV(F32 fov_y)
{
	if (gfx_state.currentCameraController)
		gfx_state.currentCameraController->projection_fov = fov_y;
}

void gfxCameraControllerLookAt(const Vec3 eye, const Vec3 target, const Vec3 up)
{
	Vec3 towards;
	Vec3* pyr = &(gfx_state.currentCameraController->campyr);
	Vec3* pos = &(gfx_state.currentCameraController->camcenter);

	subVec3(eye, target, towards);
	gfx_state.currentCameraController->camdist = lengthVec3(towards);

	normalVec3(towards);
	getVec3YP( towards, &((*pyr)[1]), &((*pyr)[0]) );
	setVec3( *pos, eye[0], eye[1], eye[2] );

	gfx_state.currentCameraController->inited = true;
}

static bool setcampos;
static bool setcampyr;
static Vec3 setcampos_position;
static Vec3 setcampyr_pyr;

// Sets the camera's position
AUTO_COMMAND ACMD_NAME(setcampos) ACMD_CATEGORY(Debug);
void gfxSetCamPos(const Vec3 position)
{
	copyVec3(position, setcampos_position);
	setcampos = true;
}

// Set the camera's orientation by PYR
AUTO_COMMAND ACMD_NAME(setcampyr) ACMD_CATEGORY(Debug);
void gfxSetCamPYR(const Vec3 pyr)
{
	copyVec3(pyr, setcampyr_pyr);
	setcampyr = true;
}

void gfxRunActiveCameraController(F32 aspect_ratio, WorldRegion* region)
{
	PERFINFO_AUTO_START_FUNC_PIX();

	if (gfx_state.currentCameraController->projection_fov <= 0)
		gfx_state.currentCameraController->projection_fov = gfxGetDefaultFOV();

	if (gfx_state.currentCameraController->ortho_mode)
	{
		MAX1(gfx_state.currentCameraController->ortho_zoom, 1);
		gfxSetActiveProjectionOrtho(gfx_state.currentCameraController->ortho_zoom, aspect_ratio);
	}
	else if (gfx_state.currentCameraController->ortho_mode_ex)
	{
		gfxSetActiveProjectionOrthoEx(	gfx_state.currentCameraController->ortho_width, 
										gfx_state.currentCameraController->ortho_height, 
										gfx_state.currentCameraController->ortho_cull_width, 
										gfx_state.currentCameraController->ortho_cull_height, 
										gfx_state.currentCameraController->ortho_near,
										gfx_state.currentCameraController->ortho_far,
										gfx_state.currentCameraController->ortho_aspect);
	}
	else if (!gbNoGraphics)
	{
		F32 fov = gfx_state.currentCameraController->projection_fov;
		if (gfx_state.currentCameraController->useHorizontalFOV)
		{
			fov = DEG(2.0f * atan(tan(RAD(fov) * 0.5f) / gfxGetAspectRatioOverride(aspect_ratio)));
		}
		gfxSetActiveProjection(fov, aspect_ratio);
	}

	if (setcampos)
	{
		copyVec3(setcampos_position, gfx_state.currentCameraController->camcenter);
		setcampos = false;
	}

	if (setcampyr)
	{
		scaleVec3(setcampyr_pyr, (PI/180), gfx_state.currentCameraController->campyr);
		copyVec3(gfx_state.currentCameraController->campyr, gfx_state.currentCameraController->targetpyr);
		setcampyr = false;
	}

	if (gfx_state.currentCameraController->camera_func)
	{
		PERFINFO_AUTO_START("camera_func", 1);
		gfx_state.currentCameraController->camera_func(gfx_state.currentCameraController, gfx_state.currentCameraView, gfx_state.frame_time, gfx_state.real_frame_time);
		PERFINFO_AUTO_STOP();
	}

	if (gbNoGraphics)
	{
		PERFINFO_AUTO_STOP_FUNC_PIX();
		return;
	}

	copyMat4(gfx_state.currentCameraView->frustum.cammat, gfx_state.currentCameraController->last_camera_matrix);
	gfx_state.currentCameraController->last_view = gfx_state.currentCameraView;

	gfxUpdateActiveCameraViewVolumes(region);

	if (!gfxCheckIsPointIndoors(gfx_state.currentCameraView->frustum.cammat[3], &gfx_state.currentCameraView->desired_light_range, &gfx_state.currentCameraView->can_see_outdoors))
	{
		//gfx_state.currentCameraView->desired_light_range = gfxSkyGetVisibleSky(gfx_state.currentCameraView->sky_data)->sunValues.lightRange;
		gfx_state.currentCameraView->desired_light_range = gfxSkyGetVisibleSky(gfx_state.currentCameraView->sky_data)->lightBehaviorValues.lightRange;
		gfx_state.currentCameraView->can_see_outdoors = true;
	}
	else
	{
		const BlendedSkyInfo* sky_info = gfxSkyGetVisibleSky(gfx_state.currentCameraView->sky_data);
		if(sky_info) {
			gfx_state.currentCameraView->desired_light_range = sky_info->lightBehaviorValues.lightRange;
		} else {
			gfx_state.currentCameraView->desired_light_range = 2;
		}
	}

	if (!gfx_state.currentCameraView->desired_light_range)
		gfx_state.currentCameraView->desired_light_range = 2;

	if (gfx_state.currentCameraController->override_bg_color)
		copyVec4(gfx_state.currentCameraController->clear_color, gfx_state.currentCameraView->clear_color);

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

F32 gfxGetActiveCameraFOV(void)
{
	return gfx_state.currentCameraController->projection_fov;
}

F32 gfxGetActiveCameraNearPlaneDist(void)
{
	if (gfx_state.currentCameraController && gfx_state.currentCameraController->last_view)
		return gfx_state.currentCameraController->last_view->frustum.znear;

	return -0.73f;
}

bool gfxIsActiveCameraControllerRotating(void)
{
	return gfx_state.currentCameraController->rotate;
}

void gfxTellWorldLibCameraPosition(void)
{
	if (gfx_state.currentCameraView)
		worldLibSetCameraFrustum(&gfx_state.currentCameraView->frustum);
}


void gfxNullCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed)
{
	Mat4 camera_matrix;

	// Actually load it into the matrix, by transforming the camera space movement into world space 
	createMat3YPR(camera_matrix, camera->campyr);
	{
		Vec3 local_offset;
		setVec3(local_offset, 0, 0, camera->camdist);
		mulVecMat3(local_offset, camera_matrix, camera_matrix[3]);
		addVec3(camera_matrix[3], camera->camcenter, camera_matrix[3]);
		copyVec3(camera->camcenter, camera->camfocus);
	}

	frustumSetCameraMatrix(&camera_view->new_frustum, camera_matrix);
}

static F32 camera_accel=1;

AUTO_CMD_INT(camera_accel, camera_accel);

F32	autoScale(F32 val)
{
	F32		scale;

	if (!camera_accel)
		return val;
	scale = pow(fabs(val),1.15);
	if (val < 0)
		return -scale;
	return scale;
}

void gfxEditorCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed)
{
	Vec3 camlocaloffset;
	int mxdiff, mydiff;
	Mat4 camera_matrix;
	bool mouseShouldBeLocked = false;

	if (!camera->inited) {
		camera->camdist = 40;
		setVec3(camera->camcenter, 0,10,0);
		setVec3(camera->campyr, 0,0,0);
		camera->inited = 1;
	}

	mouseDiffLegacy(&mxdiff, &mydiff);
	mxdiff *= input_state.invertX?-1:1;

	{
		F32 xdiff, ydiff;
		gamepadGetLeftStick(&xdiff, &ydiff);
		camera->campyr[1] = addAngle(camera->campyr[1], real_elapsed * xdiff);
		camera->campyr[0] = addAngle(camera->campyr[0], real_elapsed * ydiff);

		gamepadGetRightStick(&xdiff, &ydiff);
		xdiff *= input_state.invertX?-1:1;;
		ydiff *= input_state.invertY?-1:1;;
		camera->camdist -= real_elapsed * ydiff * 15.f;
	}

	if (camera->pan)
	{
		mouseShouldBeLocked = true;
		camera->locked = 1;
	}
	else if (camera->rotate)
	{
		camera->campyr[1] = addAngle(camera->campyr[1], mxdiff * 0.005f);
		camera->campyr[0] = addAngle(camera->campyr[0], mydiff * 0.005f * (input_state.invertY?-1:1));
		mouseShouldBeLocked = true;
		camera->locked = 1;
	}
	else if (camera->zoom)
	{
		if (camera->ortho_mode)
			camera->ortho_zoom += (autoScale(mydiff) * 0.1f * (camera->zoom_fast ? 3 : 1));
		else
			camera->camdist += (autoScale(mydiff) * 0.25f * (camera->zoom_fast ? 3 : 1));
		mouseShouldBeLocked = true;
		camera->locked = 1;
	}
	else if (camera->zoomstep)
	{
		if (camera->ortho_mode)
			camera->ortho_zoom += camera->zoomstep;
		else
			camera->camdist += camera->zoomstep;
	}
	else if (camera->locked)
	{
		camera->locked = 0;
	}

#define EPSILON 0.00001f
	if (camera->campyr[0] > PI*0.5f - EPSILON)
		camera->campyr[0] = PI*0.5f - EPSILON;
	if (camera->campyr[0] < -PI*0.5f + EPSILON)
		camera->campyr[0] = -PI*0.5f + EPSILON;

	createMat3YPR(camera_matrix, camera->campyr);
	if (camera->pan)
	{
		Vec3 local_offset, world_offset;
		F32 scale = (camera->pan_fast ? 0.75f : 0.25f) * (camera->pan_speed ? camera->pan_speed : 1.0f);
		setVec3(local_offset, autoScale(mxdiff) * scale, autoScale(mydiff) * scale, 0);
		mulVecMat3(local_offset, camera_matrix, world_offset);
		addVec3(world_offset, camera->camcenter, camera->camcenter);
	}

	setVec3(camlocaloffset, 0, 0, camera->camdist);
	mulVecMat3(camlocaloffset, camera_matrix, camera_matrix[3]);
	addVec3(camera_matrix[3], camera->camcenter, camera_matrix[3]);
	copyVec3(camera->camcenter, camera->camfocus);

	if (camera->setcenter || ((camera->zoom || camera->zoomstep) && camera->camdist < 0 && !camera->lock_pivot))
	{
		copyVec3(camera_matrix[3], camera->camcenter);
		camera->camdist = 0;
		camera->setcenter = 0;
	}

	frustumSetCameraMatrix(&camera_view->new_frustum, camera_matrix);
	camera->zoomstep = 0.0f;

	if( mouseShouldBeLocked ) {
		mouseLockThisFrame();
	}
}

static F32 free_cam_movement_scale = 10.f;

// Sets the movement speed for freecam.  Defaults to 10.
AUTO_CMD_FLOAT(free_cam_movement_scale, freeCamSpeed) ACMD_CATEGORY(Debug);

void gfxFreeCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed)
{
	// The free cam has no target... just make the distance 0 and manipulate the matrix directly
	F32 fKeyboardMovementScale = free_cam_movement_scale * real_elapsed;
	F32 fGamePadMovementScale = free_cam_movement_scale * real_elapsed;
	F32 fMouseRotationScale = 0.005f;
	F32 fGamePadRotationScale = 1.5f * real_elapsed;
	Mat4 camera_matrix;

	int mouse_x, mouse_y;
	F32 gamePadL_x, gamePadL_y;
	F32 gamePadR_x, gamePadR_y;
	F32 fXMovement=0.0f;
	F32 fYMovement=0.0f;
	F32 fZMovement=0.0f;
	if (!camera->inited)
	{
		setVec3(camera->camcenter, 0, 10, 40);
		setVec3(camera->campyr, 0, 0, 0);
		camera->inited = 1;
	}

	mouseDiffLegacy(&mouse_x, &mouse_y);
	mouse_x *= input_state.invertX?-1:1;;
	mouse_y *= input_state.invertY?-1:1;;
	gamepadGetLeftStick(&gamePadL_x, &gamePadL_y);
	gamepadGetRightStick(&gamePadR_x, &gamePadR_y);
	gamePadR_x *= input_state.invertX?-1:1;;
	gamePadR_y *= input_state.invertY?-1:1;;

	// First handle any orientation changes for all inputs

	// Mouse
	if (camera->rotate)
	{
		camera->campyr[1] = addAngle(camera->campyr[1], mouse_x * fMouseRotationScale);
		camera->campyr[0] = addAngle(camera->campyr[0], mouse_y * fMouseRotationScale);
		mouseLockThisFrame();
		camera->locked = 1;
	}
	else if (camera->zoom)
	{
		if (camera->ortho_mode)
			camera->ortho_zoom += mouse_y * 0.1f;
		mouseLockThisFrame();
		camera->locked = 1;
	}
	else if (camera->locked)
	{
		camera->locked = 0;
	}

	// GamePad
	camera->campyr[1] = addAngle(camera->campyr[1], gamePadR_x * fGamePadRotationScale);
	camera->campyr[0] = addAngle(camera->campyr[0], -gamePadR_y * fGamePadRotationScale);

	if (camera->campyr[0] > PI*0.5f - EPSILON)
		camera->campyr[0] = PI*0.5f - EPSILON;
	if (camera->campyr[0] < -PI*0.5f + EPSILON)
		camera->campyr[0] = -PI*0.5f + EPSILON;


	// Now calculate camera-space changes in motion, put in local_offset

	// Keypad
	fXMovement += ((F32)camera->left - (F32)camera->right) * fKeyboardMovementScale;
	fYMovement += ((F32)camera->up - (F32)camera->down) * fKeyboardMovementScale;
	fZMovement += ((F32)camera->backward - (F32)camera->forward) * fKeyboardMovementScale;

	// GamePad
	if ( inpLevel(INP_LB) )
		fYMovement += gamePadL_y * fGamePadMovementScale;
	else
		fZMovement -= gamePadL_y * fGamePadMovementScale;
	fXMovement -= gamePadL_x * fGamePadMovementScale;

	// F2-like movement speed scaling
	if (fXMovement || fYMovement || fZMovement){
		camera->delay += real_elapsed;
		if (camera->delay > camera->acc_delay)
			camera->speed += real_elapsed*camera->acc_rate;
	} else {
		camera->speed = camera->start_speed;
		camera->delay = 0;
	}

	MIN1(camera->speed, camera->max_speed);

	fXMovement *= camera->speed;
	fYMovement *= camera->speed;
	fZMovement *= camera->speed;

	// Actually load it into the matrix, by transforming the camera space movement into world space 
	createMat3YPR(camera_matrix, camera->campyr);
	{
		Vec3 local_offset, world_offset;
		setVec3(local_offset, fXMovement, fYMovement, fZMovement);
		mulVecMat3(local_offset, camera_matrix, world_offset);
		addVec3(world_offset, camera->camcenter, camera->camcenter);
	}
	copyVec3(camera->camcenter, camera_matrix[3]);
	copyVec3(camera->camcenter, camera->camfocus);

	frustumSetCameraMatrix(&camera_view->new_frustum, camera_matrix);
}

// switchable between freecam and editorcam controls
void gfxDefaultEditorCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed)
{
	if (!camera->mode_switch)
	{
		gfxCamMultiDragPerFrame(camera);
		gfxEditorCamFunc(camera, camera_view, elapsed, real_elapsed);
	}
	else
		gfxFreeCamFunc(camera, camera_view, elapsed, real_elapsed);
}

void gfxDemoCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed)
{
	// The demo cam has no target... just make the distance 0 and manipulate the matrix directly
	if (!camera->inited)
	{
		setVec3(camera->camcenter, 0, 10, 40);
		camera->speed = 1;
		camera->inited = 1;
	}

	gfxInterpolateCameraFrames(elapsed);
	if (camera->last_view) // Update PYR for popping in/out of freecam
	{
		globMovementLog("[gfx] Setting last_view->new_frustum's pyr for some reason.");
		
		getMat3YPR(camera->last_view->new_frustum.cammat, camera->campyr);
	}

	copyVec3(camera_view->new_frustum.cammat[3], camera->camfocus);
}

void gfxCameraControllerSetTarget(GfxCameraController *camera, const Vec3 target)
{
 	if (target)
	{
		copyVec3(target, camera->camcenter);
		addVec3(camera->camcenter, camera->centeroffset, camera->camcenter);
	}
}

void gfxCameraControllerCopyPosPyr(const GfxCameraController *src, GfxCameraController *dst)
{
	if (src->last_view)
		copyVec3(src->last_view->frustum.cammat[3], dst->camcenter);
	else
		copyVec3(src->camcenter, dst->camcenter);
	copyVec3(src->campyr, dst->campyr);
	copyVec3(src->campyr, dst->targetpyr);
	dst->camdist = 0;
	dst->speed = 1;
	dst->inited = 1;
}

void gfxActiveCameraControllerOverrideClearColor(const Vec4 clear_color)
{
	if (clear_color)
	{
		gfx_state.currentCameraController->override_bg_color = 1;
		copyVec4(clear_color, gfx_state.currentCameraController->clear_color);
		copyVec4(clear_color, gfx_state.currentCameraView->clear_color);
	}
	else
	{
		gfx_state.currentCameraController->override_bg_color = 0;
	}
}

// Editor cam helper functions
void gfxCameraSwitchMode(GfxCameraController *camera, bool mode)
{
	if (!camera->locked && camera->mode_switch != !!mode)
	{
		camera->mode_switch = !!mode;

		// reset any existing motion
		camera->pan = 0;
		camera->pan_fast = 0;
		camera->rotate = 0;
		camera->zoom = 0;
		camera->zoom_fast = 0;
		camera->left = camera->right = camera->forward = camera->backward = camera->up = camera->down = 0;

		// recenter camera
		if (camera->last_view)
			copyVec3(camera->last_view->frustum.cammat[3], camera->camcenter);
		camera->camdist = 0;
	}
}

bool gfxWillWaitForZOcclusion(void)
{
	return !!gfx_state.debug.wait_for_zocclusion;
}

void gfxStartEarlyZOcclusionTest(void)
{
	if(gfx_state.debug.wait_for_zocclusion==2)
	{
		PERFINFO_AUTO_START_FUNC();
		if(!gfx_state.debug.noZocclusion
			&& !gfx_state.currentCameraController->ortho_mode
			&& !gfx_state.currentCameraController->ortho_mode_ex)
		{
			gfxCameraViewCreateOcclusion(gfx_state.currentCameraView);
			zoInitFrame(gfx_state.currentCameraView->occlusion_buffer, gfx_state.currentCameraView->projection_matrix, &gfx_state.currentCameraView->new_frustum);
		}
		PERFINFO_AUTO_STOP();
	}
}

#define CAMERA_DEFAULT_FOV 55.0

float gfxGetDefaultFOV(void) {
	if(!gfx_state.settings.defaultFOV) {
		return CAMERA_DEFAULT_FOV;
	}
	return gfx_state.settings.defaultFOV;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("gfxSetDefaultFOV") ACMD_HIDE;
void gfxSetDefaultFOV(float fFov) {

	GfxCameraController *camController = gfx_state.currentDevice->primaryCameraController;

	if(fFov <= 15.0 || fFov >= 160.0)
		fFov = CAMERA_DEFAULT_FOV;

	if(camController) {
		camController->default_projection_fov = fFov;
		camController->target_projection_fov = fFov;
		camController->projection_fov = fFov;
	}

	gfx_state.settings.defaultFOV = fFov;
	gfxSettingsSaveSoon();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(gfxSetDefaultFOV) ACMD_HIDE;
void gfxSetDefaultFOVError(void) {
	conPrintf("Current FOV  : %f\n", gfx_state.currentCameraController->projection_fov);
	conPrintf("Default FOV  : %f\n", gfx_state.settings.defaultFOV);
	conPrintf("Standard FOV : %f\n", CAMERA_DEFAULT_FOV);
}

#include "GfxCamera_h_ast.c"
