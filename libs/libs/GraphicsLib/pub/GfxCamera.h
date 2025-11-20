#pragma once
GCC_SYSTEM

#include "Frustum.h"

typedef struct GfxSkyData GfxSkyData;
typedef struct SceneInfo SceneInfo;
typedef struct GfxOcclusionBuffer GfxOcclusionBuffer;
typedef struct GfxCameraController GfxCameraController;
typedef struct WorldVolumeQueryCache WorldVolumeQueryCache;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct WorldVolumeWater WorldVolumeWater;
typedef struct SkyInfoGroup SkyInfoGroup;
typedef struct RdrOcclusionQueryResult RdrOcclusionQueryResult;
typedef struct WorldRegion WorldRegion;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndRunningQuery) AST_STRIP_UNDERSCORES WIKI("Camera View Doc");
typedef struct GfxRunningQuery
{
	RdrOcclusionQueryResult **queries;	NO_AST//AST( NAME(Queries) WIKI("The set of in-flight occlusion queries") )
	F32 history[10];					AST( NAME(History) WIKI("The last ten results which can be used to gauge the brightness of the scene") )
	int idx;							AST( NAME(Index) WIKI("The last finished luminance history array position") )
	int count;							AST( NAME(Count) WIKI("The quantity of luminance history values") )
} GfxRunningQuery;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndCameraView) AST_STRIP_UNDERSCORES WIKI("Camera View Doc");
typedef struct GfxCameraView
{
	S32 initialized;								NO_AST

	// Matrices sent to the appropriate surfaces
	Mat44 projection_matrix;						NO_AST //AST( NAME(ProjectionMatrix) WIKI("The camera projection matrix") )
	Mat44 far_projection_matrix;						NO_AST //AST( NAME(ProjectionMatrix) WIKI("The camera projection matrix") )
	Mat44 sky_projection_matrix;						NO_AST //AST( NAME(ProjectionMatrix) WIKI("The camera projection matrix") )

	Frustum frustum;								NO_AST //AST( NAME(Frustum) WIKI("This camera's previous frustum") )
	Frustum new_frustum;							NO_AST //AST( NAME(NewFrustum) WIKI("This camera's most recent frustum") )
	GfxOcclusionBuffer *occlusion_buffer;			NO_AST //AST( NAME(OcclusionBuffer) WIKI("The occlusion buffer") )

	SkyInfoGroup *last_region_sky_group;			NO_AST
	SkyInfoGroup *last_region_override_sky_group;	NO_AST
	F32 last_region_override_sky_fade_rate;			NO_AST

	GfxSkyData *sky_data;							NO_AST //AST( NAME(SkyData) WIKI("Sky Data - updated during gfxUpdateActiveCameraViewVolumes") )

	WorldVolumeQueryCache *volume_query;			NO_AST //AST( NAME(WorldVolumeQuery) WIKI("World query volumes retrieved from updating active view volumes") )
	WorldVolumeEntry *last_sky_fade_volume_entry;	NO_AST//AST( NAME(LastSkyFadeVolumeEntry) WIKI("the last sky fade volume entry from gfxUpdateActiveCameraViewVolumes") )

	SkyInfoGroup **fx_sky_groups;					NO_AST

	U32 sky_update_timestamp;						AST( NAME(SkyUpdateTimestamp) WIKI("Sky Update Timestamp applied during gfxDrawFrame") )

	Vec4 clear_color;								AST( NAME(ClearColor) WIKI("Clear color used for the color buffer (the background color)") )

	WorldVolumeWater *in_water_this_frame;			NO_AST
	float in_water_completely_distance;				AST( NAME(InWaterCompletelyDistance) WIKI("If the camera is in the water completely, the keeps track of how far it is below the surface") )
	Vec3 water_plane_pos;							AST( NAME(WaterPlanePosition) WIKI("Position of the water plane") )
	F32 in_water_completely_fade_time;				AST( NAME(InWaterCompletelyFadeTime) WIKI("How long it takes to fade the camera when entering water") )

	U32 last_light_range_update_time;				AST( NAME(LastLightRangeUpdateTime) WIKI("Last time the light range for luminance was updated") )
	Vec4 exposure_transform;						AST( NAME(ExposureTransform) WIKI("Values for the exposure level - internal use only") )
	F32 desired_light_range;						AST( NAME(DesiredLightRange) WIKI("Should be handled internally only") )
	F32 adapted_light_range;						AST( NAME(AdaptedLightRange) WIKI("Should be handled internally only") )
	bool adapted_light_range_inited;				AST( NAME(AdaptedLightRangeInited) WIKI("Should be handled internally only") )

	bool can_see_outdoors;							AST( NAME(CanSeeOutdoors) WIKI("Determines whether or not to draw terrain") )

	GfxRunningQuery avg_luminance_query;
	GfxRunningQuery hdr_point_query;
} GfxCameraView;

void gfxInitRunningQuery(GfxRunningQuery *query);
void gfxDeinitRunningQuery(GfxRunningQuery *query);
void gfxCameraSaveSettings(GfxCameraView * backup_camera, GfxCameraView * active_camera_state);
void gfxCameraRestoreSettings(GfxCameraView * backup_camera, GfxCameraView * active_camera_state);

void gfxInitCameraView(GfxCameraView *camera);
void gfxCameraViewCreateOcclusion(GfxCameraView *camera);
void gfxDeinitCameraView(GfxCameraView *camera);

void gfxCameraViewNotifySkyGroupFreed(SkyInfoGroup *sky_group);

void gfxSetActiveCameraView(SA_PARAM_OP_VALID GfxCameraView *camera, bool set_as_primary);
GfxCameraView *gfxGetActiveCameraView(void);

bool gfxWorldToScreenSpaceVector(SA_PARAM_NN_VALID GfxCameraView *pCameraView, const Vec3 vWorld, Vec2 vScreenOut, bool bClamp);

void gfxUpdateActiveCameraViewVolumes(WorldRegion* region);

void gfxSetActiveProjection(F32 fovy, F32 aspect);
void gfxSetActiveProjectionOrtho(F32 ortho_zoom, F32 aspect);
void gfxSetActiveProjectionOrthoEx(F32 width, F32 height, F32 cull_width, F32 cull_height, F32 near_dist, F32 far_dist, F32 aspect);
void gfxSetActiveCameraMatrix(const Mat4 camera_matrix, bool immediate);
void gfxGetActiveCameraMatrix(Mat4 camera_matrix);
void gfxGetNextActiveCameraMatrix(Mat4 camera_matrix);
void gfxGetActiveCameraPos(Vec3 pos);
void gfxSetActiveCameraYPRPos(const Vec3 pyr, const Vec3 pos, bool immediate );
void gfxSetActiveCameraYPR(const Vec3 pyr);
void gfxGetActiveCameraYPR(Vec3 pyr);
F32 gfxGetActiveCameraFOV(void);
F32 gfxGetActiveCameraNearPlaneDist(void);

void gfxFlipCameraFrustum(GfxCameraView *cameraView);

typedef void (*GfxCameraControllerFunc)(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed);

AUTO_STRUCT;
typedef struct GfxMartinCam {
	F32 accSecondsInterp;
	Vec3 interpSourcePYR;
	Vec3 interpTargetPYR;
	U32 interpIsSet : 1;
} GfxMartinCam;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndCameraController) AST_STRIP_UNDERSCORES WIKI("Camera Controller Doc");
typedef struct GfxCameraController
{
	GfxCameraView *last_view;				AST( NAME(LastView) WIKI("The last camera view used for this camera") )
	Mat4 last_camera_matrix;				AST( NAME(LastCameraMatrix) WIKI("The last camera matrix from the frustum (cammat)") )

	SkyInfoGroup *sky_group_override;		AST( NAME(SkyGroupOverride) WIKI("What sky group to override for this camera") )
	F32 time_override;

	GfxCameraControllerFunc camera_func;	AST( INT, NAME(CameraFunction) WIKI("Function Pointer to the Camera Controller Function - which determines what to do with this camera's data per update") )

	F32 projection_fov;						AST( NAME(ProjectionFOV) WIKI("Vertical FOV angle (FOVy)") )
	F32 target_projection_fov;				AST( NAME(TargetProjectionFOV) WIKI("Target value for the vertical FOV angle (FOVy)") )
	F32 default_projection_fov;				AST( NAME(DefaultProjectionFOV) WIKI("Default value for the vertical FOV angle (FOVy)") )
	bool ortho_mode;						AST( NAME(OrthoMode) WIKI("Is this camera in Orthographic Mode?") )
	F32 ortho_zoom;							AST( NAME(OrthoZoom) WIKI("The amount of zoom in ortho mode, otherwise the camera uses camdist") )

	bool ortho_mode_ex;						AST( NAME(OrthoModeEx) WIKI("Orthographic Mode Override to specify custom projection parameters") )
	F32 ortho_aspect;						AST( NAME(OrthoAspect) WIKI("Used in conjunction with ortho_mode_ex - aspect ratio of the frustum") )
	F32 ortho_width;						AST( NAME(OrthoWidth) WIKI("Used in conjunction with ortho_mode_ex - width of the frustum") )
	F32 ortho_height;						AST( NAME(OrthoHeight) WIKI("Used in conjunction with ortho_mode_ex - height of the frustum") )
	F32 ortho_cull_width;					AST( NAME(OrthoCullWidth) WIKI("Used in conjunction with ortho_mode_ex - cull width") )
	F32 ortho_cull_height;					AST( NAME(OrthoCullHeight) WIKI("Used in conjunction with ortho_mode_ex - cull height") )
	F32 ortho_near;							AST( NAME(OrthoNear) WIKI("Used in conjunction with ortho_mode_ex - near clip plane") )
	F32 ortho_far;							AST( NAME(OrthoFar) WIKI("Used in conjunction with ortho_mode_ex - far clip plane") )

	F32 camdist;							AST( NAME(CameraDistance) WIKI("Distance of the camera to the target. (How far the camera is from the camcenter)") )
	F32 last_camdist;						AST( NAME(LastCameraDistance) WIKI("Last frame's distance of the camera to the target.  Updated automatically.") )
	F32 targetdist;							AST( NAME(TargetDist) WIKI("Distance the camera is trying to achieve") )
	F32 delay;								AST( NAME(Delay) WIKI("Acceleration delay value (How long to delay before the camera starts to accelerate the target - after the camera starts moving )") )
	F32 speed;								AST( NAME(Speed) WIKI("How fast the camera is allowed to move.") )
	F32 pan_speed;							AST( NAME(PanSpeed) WIKI("Multiplyer for middle mouse move in stander camera mode.  0 = 1") )
	F32 zoomstep;							AST( NAME(ZoomStep) WIKI("How much to zoom per step(frame)") )
	Vec3 camcenter;							AST( NAME(CamCenter) WIKI("Position of where the camera is looking (target). (The point around which the camera pivots)") )
	Vec3 camfocus;							AST( NAME(CamFocus) WIKI("Position of where the current region should be determined from.  Usually set to CamCenter in the camera controller update function.") )
	Vec3 lockedcenter;						AST( NAME(LockedCenter) WIKI("Position to lock the camera at") )
	Vec3 campyr;							AST( NAME(CamPYR) WIKI("Pitch Yaw and Roll of the camera") )
	Vec3 targetpyr;							AST( NAME(TargetPYR) WIKI("Pitch Yaw and Roll the camera is trying to achieve") )
	Vec3 centeroffset;						AST( NAME(CenterOffset) WIKI("How much to offset the center of the camera by") )
	Vec3 camPos;							AST( NAME(CameraPosition) WIKI("Position of the camera") )

	F32 start_speed;						AST( NAME(StartSpeed) WIKI("Start speed of the camera") )
	F32 max_speed;							AST( NAME(MaxSpeed) WIKI("Max speed of the camera") )
	F32 acc_delay;							AST( NAME(AccelerationDelay) WIKI("How much to delay the camera before accelerating again") )
	F32 acc_rate;							AST( NAME(AccelerationRate) WIKI("The rate of acceleration") )
	F32 block_time;							AST( NAME(BlockTime) WIKI("The time the camera has been blocked.") )
	F32 autoAdjustTimeout;					AST( NAME(AutoAdjustTimeout) WIKI("The time until the camera auto-adjust kicks back in") )

	U32 lock_pivot : 1;						AST( NAME(LockPivot) WIKI("Whether the camera should respect negative camera distances") )
	U32 mode_switch : 1;					AST( NAME(ModeSwitch) WIKI("Camera flag - Should the camera switch modes") )
	U32 multi_drag : 1;						AST( NAME(MultiDrag) WIKI("Camera flag - Is the camera multi-dragging - used for new editor camera controls that require 3+ chords, which aren't currently supported in the keybind system") )
	U32 forward : 1;						AST( NAME(Forward) WIKI("Camera flag - Is the camera moving forward") )
	U32 backward : 1;						AST( NAME(Backward) WIKI("Camera flag - Is the camera moving backward") )
	U32 left : 1;							AST( NAME(Left) WIKI("Camera flag - Is the camera moving left") )
	U32 right : 1;							AST( NAME(Right) WIKI("Camera flag - Is the camera moving right") )
	U32 up : 1;								AST( NAME(Up) WIKI("Camera flag - Is the camera moving up") )
	U32 down : 1;							AST( NAME(Down) WIKI("Camera flag - Is the camera moving down") )
	U32 zoom : 1;							AST( NAME(Zoom) WIKI("Camera flag - Is the camera zooming") )
	U32 zoom_fast : 1;						AST( NAME(ZoomFast) WIKI("Camera flag - Is the camera zooming fast") )
	U32 setcenter : 1;						AST( NAME(SetCenter) WIKI("Camera flag - Should the camera set its center position (target)") )
	U32 pan : 1;							AST( NAME(Pan) WIKI("Camera flag - Is the camera panning") )
	U32 pan_fast : 1;						AST( NAME(PanFast) WIKI("Camera flag - Is the camera panning fast") )
	U32 rotate : 1;							AST( NAME(Rotate) WIKI("Camera flag - Is the camera rotating") )
	U32 rotate_left : 1;					AST( NAME(RotateL) WIKI("Camera flag - Is the camera rotating left (used for keyboard turning)") )
	U32 rotate_right : 1;					AST( NAME(RotateR) WIKI("Camera flag - Is the camera rotating right (used for keyboard turning)") )
	U32 rotate_up : 1;						AST( NAME(RotateU) WIKI("Camera flag - Is the camera pitching up (used for keyboard looking)") )
	U32 rotate_down : 1;					AST( NAME(RotateD) WIKI("Camera flag - Is the camera pitching down (used for keyboard looking)") )
	U32 freelook : 1;						AST( NAME(FreeLook) WIKI("Camera flag - Is the camera in free look mode") )
	U32 locked : 1;							AST( NAME(Locked) WIKI("Camera flag - Whether the mouse is locked due to moving the camera") )
	U32 lockedtotarget : 1;					AST( NAME(LockedToTarget) WIKI("Camera flag - Is the camera locked to the target") )
	U32 lockControllerCamControl : 1;		AST( NAME(LockControllerCameraControl) WIKI("Camera flag - Whether controller camera control is locked") )
	U32 lockAutoAdjust : 1;					AST( NAME(LockAutoAdjust) WIKI("Camera flag - Whether controller camera control is locked") )
	U32 useHorizontalFOV : 1;				AST( NAME(UseHorizontalFOV) WIKI("Camera flag - Use a horizontal FOV instead of vertical") )

	U32 inited:1;							AST( NAME(Initialized) WIKI("Camera flags - Is the camera initialized") )
	U32 pyr_preinit:1;						AST( NAME(PyrPreInit) WIKI("Camera flag - Is the camera pyr pre-initialized") )

	U32 do_shadow_scaling:1;				AST( NAME(DoShadowScaling) WIKI("Camera flag - Scales the shadow quality up when the camdist is small") )

	U32 override_no_fog_clip:1;				AST( NAME(OverrideNoFogClip) WIKI("Rendering override flag") )
	U32 override_bg_color:1;				AST( NAME(OverrideBackgroundColor) WIKI("Don't let the sky update the backgroundcolor") )
	U32 override_disable_shadows:1;			AST( NAME(OverrideDisableShadows) WIKI("Don't do shadows") )
	U32 override_disable_3D:1;				AST( NAME(OverrideDisable3D) WIKI("Don't render 3D geometry") )
	U32	override_time:1;					AST( NAME(OverrideTime) WIKI("Force use of the time_override field as the current time when computing the sky values") ) 
	U32 override_hide_editor_objects:1;		AST( NAME(OverrideHideEditorObjects) WIKI("Don't render editor objects") )
	U32 override_disable_shadow_pulse:1;	AST( NAME(OverrideDisableShadowPulse) WIKI("Rendering override flag") )

	Vec4 clear_color;						AST( NAME(ClearColor) WIKI("Clear color used for the color buffer (the background color)") )

	void *user_data;						NO_AST //AST( NAME(CameraSettings) WIKI("Holds this camera's settings -> defined in gclCamera.h") )

	GfxMartinCam martinCam;
} GfxCameraController;


void gfxInitCameraController(SA_PARAM_NN_VALID GfxCameraController *camera, GfxCameraControllerFunc func, void *user_data);
void gfxDeinitCameraController(SA_PARAM_NN_VALID GfxCameraController *camera);

void gfxResetCameraControllerState(GfxCameraController* camera);

void gfxCameraControllerSetSkyGroupOverride(SA_PARAM_NN_VALID GfxCameraController *camera, const char **sky_names, const char **blame_filenames);
void gfxCameraControllerSetSkyOverride(SA_PARAM_NN_VALID GfxCameraController *camera, const char *sky_name, const char *blame_filename);

void gfxCameraClearLastRegionData(SA_PARAM_NN_VALID GfxCameraView *camera_view);

void gfxActiveCameraControllerSetFOV(F32 fov_y);

void gfxSetActiveCameraController(SA_PARAM_NN_VALID GfxCameraController *camera, bool set_as_primary);
GfxCameraController *gfxGetActiveCameraController(void);

bool gfxIsActiveCameraControllerRotating(void);
void gfxCameraControllerLookAt(const Vec3 eye, const Vec3 target, const Vec3 up );
void gfxCameraControllerSetTarget(GfxCameraController *gfx_camera, const Vec3 target);
void gfxCameraControllerCopyPosPyr(const GfxCameraController *src, GfxCameraController *dst);

void gfxActiveCameraControllerOverrideClearColor(const Vec4 clear_color);

void gfxRunActiveCameraController(F32 aspect_ratio, WorldRegion* region); // Call either this or gfxSetActiveCameraMatrix once per frame per device
void gfxTellWorldLibCameraPosition(void); // Call this only on the primary camera

// Built in camera controller functions
void gfxEditorCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed);
void gfxFreeCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed);
void gfxDefaultEditorCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed);
void gfxDemoCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed);
void gclCutsceneCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed);
void gfxNullCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed);

// Editor cam auxiliary functions
void gfxCameraSwitchMode(SA_PARAM_NN_VALID GfxCameraController *camera, bool mode);
void gfxCameraHalt(SA_PARAM_NN_VALID GfxCameraController *camera);

// Custom depth of field functions
void gfxSkySetCustomDOF(GfxSkyData *sky_data, F32 nearDist, F32 nearValue, F32 focusDist, F32 focusValue, F32 farDist, F32 farValue, bool fade_in, F32 fade_rate);
void gfxSkyUnsetCustomDOF(GfxSkyData *sky_data, bool fade_out, F32 fade_rate);

// Custom fog functions
void gfxSkySetCustomFog(GfxSkyData *sky_data, const Vec3 color_hsv, float fog_dist_near, float fog_dist_far, float fog_max, bool fade_in, F32 fade_rate);
void gfxSkyUnsetCustomFog(GfxSkyData *sky_data, bool fade_out, F32 fade_rate);

void gfxSkyVolumePush(const char*** sky_names_earray_ptr, WorldRegion* region, Vec3 start, Vec3 end, F32 radius, F32 weight, bool linear_falloff);
void gfxSkyVolumeListOncePerFrame(void);
void gfxSkyVolumeDebugDraw(void);

bool gfxWillWaitForZOcclusion(void);
void gfxStartEarlyZOcclusionTest(void);

float gfxGetDefaultFOV(void);
void gfxSetDefaultFOV(float fFov);

void gfxEnableCameraLight(bool enable);
void gfxUpdateCameraLight(void);

void CommandCameraHalt(void);
void gfxSpawnPlayerAtCameraAndLeaveFreecam(void);
