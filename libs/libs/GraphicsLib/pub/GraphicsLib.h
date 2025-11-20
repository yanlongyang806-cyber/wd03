#ifndef GRAPHICSLIB_H
#define GRAPHICSLIB_H
#pragma once
GCC_SYSTEM

#include "GfxTextureEnums.h"
#include "WorldLibEnums.h"
#include "GfxSettings.h"
#include "RdrEnums.h"
#include "RdrTextureEnums.h"
#include "../texUnload.h"
#include "Timing.h"
#include "ImageTypes.h"
#include "wlPerf.h"
#include "../GfxEnums.h" // TODO: Move this header file to be public

#define GRAPHICSLIB_Z 30000 // Must be above UI_TOP_Z and UI_PANE_Z and UI_INFINITE_Z
#define MAX_MATERIAL_BRIGHTNESS 4.9875f

typedef struct RdrDevice RdrDevice;
typedef struct RdrSurface RdrSurface;
typedef struct RdrDevicePerfTimes RdrDevicePerfTimes;
typedef struct GroupDef GroupDef;
typedef struct GfxCameraController GfxCameraController;
typedef struct GfxCameraView GfxCameraView;
typedef struct GfxPerDeviceState GfxPerDeviceState;
typedef struct InputDevice InputDevice;
typedef struct Model Model;
typedef struct Material Material;
typedef struct MaterialSwap MaterialSwap;
typedef struct WindowCreateParams WindowCreateParams;
typedef struct DynDrawSkeleton DynDrawSkeleton;
typedef struct DynFxFastParticleSet DynFxFastParticleSet;
typedef struct SceneInfo SceneInfo;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct LightData LightData;
typedef struct WLCostume WLCostume;
typedef struct WorldRegion WorldRegion;
typedef struct WorldDrawableLod WorldDrawableLod;
typedef struct WorldDrawableList WorldDrawableList;
typedef struct DynDrawParams DynDrawParams;
typedef struct SkyInfoGroup SkyInfoGroup;
typedef struct SkyInfoOverride SkyInfoOverride;
typedef struct ModelLoadTracker ModelLoadTracker;
typedef struct Frustum Frustum;
typedef struct RdrSubobject RdrSubobject;

extern struct ParseTable parse_BasicTexture[];

bool gfxStartPerformanceTest(GfxPerDeviceState *gfx_device);
bool gfxGetPerformanceTestResults(RdrDevicePerfTimes *perf_output);

void gfxDisplayLogo(RdrDevice *rdr_device, const void *logo_jpg_data, int logo_data_size, const char *window_title); // this function can be run before gfxStartup
void gfxDisplayLogoProgress(RdrDevice *rdr_device, const void *logo_jpg_data, int logo_data_size, const char *window_title, float progress, float spin, bool bShowOrFreeLoadingText); // call with null rdr_device to free resources

void gfxSetFeatures(GfxFeature features_allowed);
GfxFeature gfxGetFeatures(void);
bool gfxGetShadowBufferEnabled(void);
void gfxModelSetIgnoredBoneFor2BoneSkinning(const char *bone);

void gfxStartupPreWorldLib(void); // Loads textures (needed by WorldLib to do verification)
void gfxStartup(void); // Loads material definitions, initializes globals, etc
void gfxPretendLibIsNotHere(void); // Unregisters callbacks from WorldLib as if we didn't link with GraphicsLib
void gfxRegisterDevice(RdrDevice *rdr_device, InputDevice *inputdev, bool allowShow);
void gfxUnregisterDevice(RdrDevice *rdr_device);
void gfxShutdown(RdrDevice *rdr_device);
void gfxOncePerFrame(F32 fFrameTime, F32 fRealTime, int in_editor, int allow_offscreen_render);
void gfxOncePerFrameEnd(bool close_old_regions);
void gfxSetActiveDevice(RdrDevice *rdr_device);
RdrDevice *gfxGetActiveDevice(void);
RdrDevice *gfxGetActiveOrPrimaryDevice(void); // Gets the active device if there is one, otherwise, returns the "primary" device (first created)
InputDevice *gfxGetActiveInputDevice(void);
void gfxSetActiveSurfaceEx(RdrSurface *surface, RdrSurfaceBufferMaskBits write_mask, RdrSurfaceFace face);
__forceinline static void gfxSetActiveSurface(RdrSurface *surface)
{
	gfxSetActiveSurfaceEx(surface, (RdrSurfaceBufferMaskBits)0, (RdrSurfaceFace)0);
}
void gfxOverrideDepthSurface(RdrSurface *override_depth_surface);
bool gfxStereoscopicActive(void);

RdrSurface *gfxGetActiveSurface(void);
void gfxLockActiveDeviceEx(bool do_begin_scene);
#define gfxLockActiveDevice() gfxLockActiveDeviceEx(false)
void gfxUnlockActiveDeviceEx(bool do_xlive_callback, bool do_end_scene, bool do_buffer_swap);
#define gfxUnlockActiveDevice() gfxUnlockActiveDeviceEx(false,false,false)
RdrDevice *gfxGetPrimaryDevice(void);
GfxPerDeviceState *gfxGetPrimaryGfxDevice(void);
void gfxClearGraphicsData(void); // clears some map-specific values, such as adapted luminance
void gfxSyncActiveDevice(void);

typedef void (*GfxAuxDeviceCallback)(RdrDevice* element, void *userData);
typedef bool (*GfxAuxDeviceCloseCallback)(RdrDevice* element, void *userData);
// pass window_name of NULL if you don't want position, etc remembered
RdrDevice *gfxAuxDeviceAdd(WindowCreateParams *default_params, const char *window_name, GfxAuxDeviceCloseCallback close_callback, GfxAuxDeviceCallback per_frame_callback, void *userData);
typedef void (*UIUpdateFunc)(F32 frame_time, RdrDevice *device);
void gfxAuxDeviceDefaultTop(RdrDevice *rdr_device, int flags, UIUpdateFunc ui_update_func);
void gfxAuxDeviceDefaultBottom(RdrDevice *rdr_device, int flags);
void gfxAuxDeviceRemove(RdrDevice *rdr_device);
void gfxAuxDeviceForEach(GfxAuxDeviceCallback callback, void *userData);
void gfxRunAuxDevices(void);
RdrDevice *gfxNextDevice(RdrDevice *rdr_device);
S32 gfxDeviceCount(void);

void gfxInitGlobalSettingsForGameClient(void);
void gfxSetTitle(const char *title);

void gfxGetActiveSurfaceSize(int *width, int *height);
void gfxGetActiveDeviceSize(int *width, int *height);
void gfxGetActiveDevicePosition(int *x, int *y);

void gfxSetHideDetailInEditorBit(U8 val);
U8 gfxGetHideDetailInEditorBit(void);

bool gfxGetSoftwareCursorForce(void);

void gfxSetFog(F32 near_dist, F32 far_dist, const Vec3 color);
void gfxSetTargetEntityDepth(F32 target_entity_depth);
F32 gfxGetTargetEntityDepth(void);

F32 gfxGetFrameTime(void);
F32 gfxGetClientLoopTimer(void); // Timer use for scrolling textures, etc.  Loops back to 0 at 64K seconds
U32 gfxGetFrameCount(void);
F32 gfxGetTime(void); // takes into account the time override that may be on the current camera
bool gfxGetDrawHighDetailSetting(void);
bool gfxGetDrawHighFillDetailSetting(void);

#undef gfxStatusPrintf
int gfxStatusPrintf(FORMAT_STR char const *fmt, ...); // For debug messages
#define gfxStatusPrintf(fmt, ...) gfxStatusPrintf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

void gfxSetCompileAllShaderTypes(int iSet);

typedef struct SMMaterialSwap
{
	Material *orig_material;
	Material *replace_material;
} SMMaterialSwap;

typedef struct SingleModelParams
{
	Model *model;
	ModelLoadTracker *model_tracker;
	Material *material_replace;
	Material **eaMaterialSwaps;
	BasicTexture **eaTextureSwaps;
	U32 num_vertex_shader_constants;
	Vec4 *vertex_shader_constants;
	Mat4 world_mat;
	F32 dist;
	F32 dist_offset;
	SkinningMat4 *bone_infos;
	U8 num_bones;
	U8 wireframe;
	bool unlit;
	bool double_sided;
	Vec3 color;		// Use gfxQueueSingleModelTinted
	MaterialNamedConstant **eaNamedConstants;	// Use gfxQueueSingleModelTinted
	U8 alpha;		// Use gfxQueueSingleModelTinted
	Vec3 ambient;
	Vec3 sky_light_color_front;
	Vec3 sky_light_color_back;
	Vec3 sky_light_color_side;
	bool force_lod;
	U32 lod_override;
	F32 override_visscale;
	bool bRetainRdrObjects; // Keep the subobjects and the RdrDrawableGeo for the duration of this frame
	RdrSubobject ** subObjects;
	RdrDrawableGeo * pDrawableGeo;
} SingleModelParams;

void gfxQueueSingleModel(SingleModelParams *smparams);
void gfxQueueSingleModelTinted(SingleModelParams *smparams, RdrGeometryType overrideType);
void gfxClearAllFxGrids(void);
void gfxQueueSingleDynDrawSkeleton(SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkeleton, SA_PARAM_OP_VALID WorldRegion* pOverrideRegion, bool bDrawParticlesToo, bool bForceSkinnedShadows, float* overrideModColor);
void gfxEnsureAssetsLoadedForSkeleton(DynDrawSkeleton *pDrawSkeleton);
void gfxQueueSingleFastParticleSet(SA_PARAM_NN_VALID DynFxFastParticleSet* pSet, const DynDrawParams* draw_params, F32 fFadeOut);
void gfxQueueClearEditorSets(void);
void gfxQueueFastParticleFromEditor(DynFxFastParticleSet* pSet, bool bScreenSpace);
void gfxGetSkeletonMemoryUsage(DynDrawSkeleton* pDrawSkeleton, U32* modelMemory, U32* materialMemory);


typedef void (*GfxHookBeginFrame)(bool in_editor, bool z_occlusion, bool hide_world,
	bool draw_all_directions, bool draw_sky);
typedef bool (*GfxHookEndFrame)();

bool gfxHookFrame(GfxHookBeginFrame pfnNewBeginFrameHook, GfxHookEndFrame pfnNewEndFrameHook);
bool gfxIsHookedFrame();
bool gfxIsHookedNextFrame();

void gfxStartMainFrameAction(bool in_editor, bool z_occlusion, bool hide_world, bool draw_all_directions, bool draw_sky);
bool gfxStartAction(RdrSurface *output_surface, int output_snapshot_idx, WorldRegion *region, 
					GfxCameraView *camera_view, GfxCameraController *camera_controller, 
					bool in_editor, bool z_occlusion, bool hide_world, bool draw_all_directions, 
					bool draw_sky, bool allow_shadows, bool allow_zprepass, bool allow_outlines,
					bool allow_targeting_halo, GfxActionType action_type, bool force_sun_indoors,
					F32 override_lod_scale);
bool gfxStartActionQuery(GfxActionType action_type, int num_actions);

typedef F32 (*WorldCellScaleFunc)(Vec3 pos);
void gfxFillDrawList(bool draw_world, WorldCellScaleFunc func);
void gfxFinishAction(void);
void gfxDrawFrame(void);

void gfxCheckPerRegionBudgets(void);

void gfxDrawFrameToSurface(RdrSurface *surface, bool debugDrawPrims);

void gfxProcessInputOnInactiveDevices(void);

void gfxCheckForMakeCubeMap(void);

// returns 0 if the extents are not visible, 1 if they are visible, and 2 if the camera is inside the bounds
// screen_min and screen_max are in the range [0,1]
// screen_z is in linear space [0,far_dist]
int gfxGetScreenExtents(const Frustum *frustum, const Mat44 projection_mat, const Mat4 local_to_world_mat,
						const Vec3 world_min, const Vec3 world_max, 
						Vec2 screen_min, Vec2 screen_max, F32 *screen_z, bool bClipToScreen);
int gfxCapsuleGetScreenExtents(const Frustum* pFrustum, const Mat44 xProjection, const Mat4 xLocalToWorld, 
							   const Vec3 vWorldMin, const Vec3 vWorldMax, 
							   Vec2 vScreenMin, Vec2 vScreenMax, F32* pfScreenZ, 
							   bool bFitInsideBox, bool bClipToScreen);

// Textures
typedef struct BasicTexture BasicTexture;
typedef U64 TexHandle;
BasicTexture *texLoadBasic(const char *name, TexLoadHow mode, WLUsageFlags use_category);
BasicTexture *texFindAndFlag(const char *name, int isRequired, WLUsageFlags use_category);
#define texFind(name, isRequired) texFindAndFlag(name, isRequired, (WLUsageFlags)0)
U32 texWidth(const BasicTexture *texbind);
U32 texHeight(const BasicTexture *texbind);
const char *texFindDirName(char *buf, size_t buf_size, const BasicTexture *texbind);
const char *texFindFullPath(const BasicTexture *texbind);
const char *texFindName(const BasicTexture *texbind);
void texDynamicUnload(TexUnloadMode enable);
long texLoadsPending(int include_misc);
void texFindFullName(const BasicTexture *texBind, char *filename, size_t filename_size);
BasicTexture *texAllocateScratch(const char* name, int width, int height, WLUsageFlags use);
void texFree( BasicTexture *bind, int freeRawData );
void texGetTexNames(const char*** peaTexNamesOut);
extern int g_needTextureBudgetInfo;

// TexGen
BasicTexture *texGenNew(int width,int height, const char *name, TexGenMode tex_gen_mode, WLUsageFlags use_category);
void texGenFree(BasicTexture *bind);
void texGenFreeNextFrame(BasicTexture *bind);
void texGenUpdateFromWholeSurface(BasicTexture *bind, RdrSurface *src_surface, RdrSurfaceBuffer buffer_num);

void texGenUpdate_dbg(BasicTexture *bind, U8 *tex_data, RdrTexType tex_type, RdrTexFormat pixel_format, int levels, bool clamp, bool mirror, bool pointsample, bool refcount_data MEM_DBG_PARMS);
#define texGenUpdate(bind, tex_data, tex_type, pixel_format, levels, clamp, mirror, pointsample, refcount_data) texGenUpdate_dbg(bind, tex_data, tex_type, pixel_format, levels, clamp, mirror, pointsample, refcount_data MEM_DBG_PARMS_INIT)

// Models
#define NUM_MODELTODRAWS 3
typedef struct ModelLOD ModelLOD;
typedef struct Model Model;
typedef int GeoHandle;

typedef struct ModelToDraw {
	WorldDrawableLod *draw;
	ModelLOD *model;
	GeoHandle geo_handle_primary;
	F32 alpha;
	F32 morph;
	U32 lod_index;
} ModelToDraw;

int gfxDemandLoadModel(SA_PARAM_OP_VALID Model *model,
					   ModelToDraw *models, int models_size, 
					   F32 dist, F32 lod_scale, S32 force_lod_level, 
					   SA_PARAM_OP_VALID ModelLoadTracker *model_tracker, 
					   F32 radius);

int gfxDemandLoadPreSwappedModel(SA_PARAM_NN_VALID WorldDrawableList *draw_list, 
								 ModelToDraw *models, int models_size, 
								 F32 dist, F32 lod_scale, S32 force_lod_level, 
								 SA_PARAM_OP_VALID ModelLoadTracker *model_tracker, 
								 F32 radius, bool is_clustered);

// check options
F32 gfxGetLodScale(void);
F32 gfxGetDrawScale(void);
F32 gfxGetEntityDetailLevel(void);
U32 gfxGetClusterLoadSetting(void);
void gfxSetSafeMode(int safemode);
int gfxGetSafeMode(void);
F32 gfxGetAspectRatio(void);
bool gfxGetFullscreen(void);
void *gfxGetWindowHandle(void);

extern BasicTexture *white_tex, *black_tex, *invisible_tex, *dummy_bump_tex, *dummy_cube_tex, *dummy_volume_tex, *default_ambient_cube;

// Enable to track a histogram of how world cell entries overlap multiple frustums and shadow volumes.
// Then run "show_frame_counters 20" to enable display of the histogram. Note the 20 scrolls the output
// sufficiently to put the output onscreen, as it's at the very bottom of the world cell stats block.
// See show_frame_counters code in GfxDebug.c, and bucketing in GfxWorld.c:drawEntries.
#define TRACK_FRUSTUM_VISIBILITY_HISTOGRAM 0

AUTO_STRUCT;
typedef struct FrameCounts
{
	int	cell_hits;
	int	cell_tests;
	int cell_fog_culls;
	int	cell_culls;
	int cell_zoculls;
#if TRACK_FRUSTUM_VISIBILITY_HISTOGRAM
	// Histogram for first four frustums/volumes. The 16 entries form a partition of the set of objects 
	// visible in all four frustums (each object is in one and only one bucket). Each bucket is for one
	// kind of overlap. For example, an object may be visible in frustum 1, but not any of the others,
	// or may be visible in frustum 2 & 3, but not 1 & 4. Each combination of overlaps has a separate bucket.
	// A subset of entry frustum visibility bits is equivalent to the bucket number.
	U16 cell_frustum_overlap_hist[16]; NO_AST
#endif

	int entry_hits;
	int entry_tests;
	int entry_dist_culls;
	int entry_fog_culls;
	int	entry_culls;
	int entry_zoculls;

	int	welded_entry_hits;
	int	welded_instance_hits;
	int welded_instance_dist_culls;
	int welded_instance_tests;
	int welded_instance_culls;
	int welded_instance_draws;

	int	terrain_hits;
	int terrain_tests;
	int terrain_culls;
	int terrain_zoculls;
	int terrain_draw_hits;

	int	binned_entry_hits;
	int binned_entry_tests;
	int binned_entry_culls;
	int binned_entry_zoculls;

	int world_animation_updates;

	RdrDrawListStats draw_list_stats;	AST( STRUCT(parse_RdrDrawListStats) )
	int sprites_drawn;
	int sprite_primitives_drawn;

	int unique_shader_graphs_referenced;
	int unique_shaders_referenced;
	int unique_materials_referenced;
	int lights_drawn;
	int device_locks;
	F32 ms;
	F32 fps;
	F32 cpu_ms;
	F32 gpu_ms;
	int total_skeletons;
	int drawn_skeletons;
	int drawn_skeleton_shadows;
	int postprocess_calls;

	WorldPerfFrameCounts world_perf_counts;	AST( STRUCT(parse_WorldPerfFrameCounts) )
	int zo_occluders;
	int zo_cull_tests;
	int zo_culls;

	int gpu_bound;

	int over_budget_mem;
	int over_budget_perf;

	int mem_usage_mbs;

	int stalled; // Delayed by 5 frames or so
	F32 stall_time; // Delayed by 5 frames or so

	// Calculated from above:
	S64 triangles_in_scene;
	S64 opaque_triangles_drawn;
	S64 alpha_triangles_drawn;
	int objects_in_scene;
	int opaque_objects_drawn;
	int alpha_objects_drawn;
	Vec3 cam_pos;
	Vec3 cam_pyr;
} FrameCounts;
extern ParseTable parse_FrameCounts[];
#define TYPE_parse_FrameCounts FrameCounts

AUTO_STRUCT;
typedef struct FrameCountsHist
{
	FrameCounts minvalues;
	FrameCounts maxvalues;
	FrameCounts sum;
	FrameCounts avg;
	int count;
	int invalid; // Set to invalid if some event such as maxInactiveFPS is skewing the values

	// I apologize for the ping and ip being here, Alex said it was the easiest thing to do.
	U32 ping;
	U32 ip;
} FrameCountsHist;
extern ParseTable parse_FrameCountsHist[];
#define TYPE_parse_FrameCountsHist FrameCountsHist

void gfxResetFrameCounters(void);
void gfxInvalidateFrameCounters(void);
void gfxGetFrameCounts(SA_PRE_NN_FREE SA_POST_NN_VALID FrameCounts *counts);
void gfxGetFrameCountsHistAndReset(SA_PRE_NN_FREE SA_POST_NN_VALID FrameCountsHist *hist);
FrameCounts *gfxGetFrameCountsForModification(void);
int gfxGetMspfPercentile(float percentile);
void gfxResetInterFrameCounts(void);

void gfxDrawLightWireframe(LightData *light_data, int nsegs);

void gfxLoadAllCells(int i);
bool gfxIsLoadingAllCells(void);

void gfxPreloadCheckStartGlobal(void);
void gfxPreloadCheckStartMapSpecific(bool bEarly);
void gfxMaterialFlagPreloadedAllTemplates(void);
int gfxMaterialPreloadGetLoadingCount(void);
void gfxMaterialPreloadOncePerFrame(bool bEarly);

char **gfxGetAllSkyNames(bool duplicate_strings);
void gfxSkyGroupAddOverride(SkyInfoGroup* sky_group, const char *new_sky);
void gfxSkyGroupSetOverride(SkyInfoGroup* sky_group, SkyInfoOverride *sky_override, const char *new_sky);
void gfxSkyNotifySkyGroupFreed(SkyInfoGroup* sky_group);
void gfxSkyGroupFree(SA_PRE_OP_VALID SA_POST_P_FREE SkyInfoGroup *sky_group);


bool gfxIsInactiveApp(void);

bool gfxSkipSystemSpecsCheck(void);

// This will eventually contain a lot of the data from gfx_state.debug pertaining
// to visualization settings when the visualization panel comes online
AUTO_STRUCT;
typedef struct GfxVisualizationSettings
{
	bool	hide_all_volumes;
	bool	hide_occlusion_volumes;
	bool	hide_audio_volumes;
	bool	hide_skyfade_volumes;
	bool	hide_neighborhood_volumes;
	bool	hide_interaction_volumes;
	bool	hide_landmark_volumes;
	bool	hide_power_volumes;
	bool	hide_warp_volumes;
	bool	hide_genesis_volumes;
	bool	hide_exclusion_volumes;
	bool	hide_aggro_volumes;

	bool	hide_untyped_volumes;
} GfxVisualizationSettings;

SA_RET_NN_VALID GfxVisualizationSettings *gfxGetVisSettings(void);

void gfxGetSettingsString(char *buf, size_t buf_size);
void gfxGetSettingsStringCSV(char *buf, size_t buf_size); // Will contain carriage returns

void gfxWaterAgitate(F32 magnitude);


extern bool gbNoGraphics;

extern bool gbNo3DGraphics;

void gfxCheckAutoFrameRateStabilizer(void);
void gfxResetFrameRateStabilizerCounts(void);

bool gfxFeatureDesired(GfxFeature feature);
bool gfxFeatureEnabledPublic(GfxFeature feature); // Non-inlined/public version

int gfxNumLoadsPending(bool includePatching);
bool gfxIsStillLoading(bool includePatching);
bool gfxInTailor(void);

void gfxNotifyDoneLoading(void); // Call this once into a regular game state
void gfxNotifyStartingLoading(void); // Call this when we drop back into a loading state

typedef int (*GetAccessLevelFunc)(void);
void gfxSetGetAccessLevelFunc(GetAccessLevelFunc f);
//access level "for display" is used when deciding whether to print the access level on the screen,
//and should be zero whenever there is no entity, and thus no "real" access level
void gfxSetGetAccessLevelForDisplayFunc(GetAccessLevelFunc f);
int gfxGetAccessLevel(void);
int gfxGetAccessLevelForDisplay(void);

typedef void GfxScreenshotCallBack(char *pFileName, void *pUserData);

//NULL filename means create a temporary file and then delete it
void gfxRequestScreenshot(char *pFileName, bool bInclude2DElements, GfxScreenshotCallBack *pCB, void *pUserData);

// Demo hooks:
typedef void (*GfxRecordCamMatPostProcessFn)(Mat4 cam_mat, bool is_absolute);
void gfxSetRecordCamMatPostProcessFn( GfxRecordCamMatPostProcessFn fn );


typedef bool (*GfxIsInTailorCheck)(void);
void gfxSetIsInTailorFunc(GfxIsInTailorCheck func);

// Enable this flag when measuring two light instruction counts
extern bool gfx_disable_auto_force_2_lights;

extern U32 gfx_activeSurfaceSize[2];
__forceinline static void gfxGetActiveSurfaceSizeInline(int *width, int *height) { if (width) *width = gfx_activeSurfaceSize[0]; if (height) *height = gfx_activeSurfaceSize[1]; }

void gfxFMVPlayFullscreen(const char *name);
bool gfxFMVDone(void);
void gfxFMVClose(void);
void gfxFMVSetVolume(F32 volume);

// functions on whether or not to display skies contained within volumes.
bool gfxGetDisableVolumeSkies(void);
void gfxSetDisableVolumeSkies(bool bValue);

// Temporarily enable or disable indoor rendering for the current
// action.
void gfxSetCurrentActionAllowIndoors(bool bAllowIndoors);
bool gfxGetCurrentActionAllowIndoors(void);

void gfxSetClusterState(bool cluster_state);
bool gfxGetClusterState(void);

// Allow some editors (like the LOD editor) to ignore all vis scale, because it interferes with their function
void gfxDisableVisScale(bool bDisable);

#endif
