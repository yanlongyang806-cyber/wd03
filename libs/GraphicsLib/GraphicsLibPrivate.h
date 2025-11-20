#pragma once
GCC_SYSTEM

#include "GfxCamera.h"
#include "GfxSettings.h" // TODO: Just store a pointer to GfxSettings to not require this include in every file
#include "WorldLibEnums.h"
#include "timing.h"
#include "RenderLib.h"
#include "GraphicsLib.h"
#include "wlVolumes.h"
#include "GfxEnums.h"
#include "RdrEnums.h"
#include "Vec4H.h"
#include "earray.h"
#include "file.h"
#include "rgb_hsv.h"

typedef U64 TexHandle;
typedef int ShaderHandle;
typedef struct BasicTexture BasicTexture;
typedef struct DynamicCache DynamicCache;
typedef struct EventOwner EventOwner;
typedef struct Frustum Frustum;
typedef struct GeoRenderInfo GeoRenderInfo;
typedef struct GfxOcclusionBuffer GfxOcclusionBuffer;
typedef struct GfxSpecialShaderData GfxSpecialShaderData;
typedef struct InputDevice InputDevice;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct OctreeDrawEntry OctreeDrawEntry;
typedef struct RdrDevice RdrDevice;
typedef struct RdrDrawList RdrDrawList;
typedef struct RdrLight RdrLight;
typedef struct RdrSurface RdrSurface;
typedef struct RdrSpritesPkg RdrSpritesPkg;
typedef struct WorldRegion WorldRegion;
typedef struct WorldRegionGraphicsData WorldRegionGraphicsData;
typedef struct GfxLight GfxLight;
typedef struct BlockRange BlockRange;
typedef struct GMesh GMesh;
typedef struct GfxSplat GfxSplat;
typedef struct GfxSpriteList GfxSpriteList;

typedef void (*GfxAuxDeviceCallback)(RdrDevice* element, void *userData);

typedef struct CamViewMat
{
	Mat44H cammat;
	Mat44H viewmat;
} CamViewMat;

#define MAX_CLIP_PLANES 256

typedef struct CachedFrustumDataAligned
{
	CamViewMat * matrices;
	Vec4PH * clip_planes;
	int * plane_counts;
	int num_planes;
	U8 plane_volume_index[MAX_CLIP_PLANES];
} CachedFrustumDataAligned;

#define PSSM_SPLIT_COUNT 3

typedef struct PSSMSettings
{
	F32 near_distances[PSSM_SPLIT_COUNT];
	F32 far_distances[PSSM_SPLIT_COUNT];
} PSSMSettings;

typedef struct GfxGlobalDrawParams
{
	Mat44				nextVisProjection; // first for alignment

	RdrDrawListPassData **passes;	// from draw_list
	int					pass_count;
	CachedFrustumDataAligned * frustum_data_aligned;
	U32					all_frustum_bits;
	U32					visual_frustum_bit;
	U32					shadow_frustum_bits;
	RdrDrawListPassData *visual_pass;

	F32					clip_distance;
	U32					clip_by_distance:1;
	U32					clip_only_below_camera:1;

	U32					in_editor:1;
	U32					global_wireframe:2;
	U32					use_far_depth_range:1;
	U32					z_occlusion:1;
	U32					hide_world:1;
	U32					draw_all_directions:1;
	U32					do_shadows:1;
	U32					do_splat_shadows:1;
	U32					do_zprepass:1;
	U32					do_hdr_pass:1;
	U32					do_shadow_buffer:1;
	U32					do_ssao:1;
	U32					do_outlining:1;
	U32					has_hdr_texture:1;
	U32					do_aux_visual_pass:1;
	U32					disable_vertex_lights:1;
	Vec4				selectedTintColor;

	Vec3				cam_pos;
	Vec3				pos_offset;
	int					hidden_object_id;
	F32					lod_scale;

	RdrDrawList			*draw_list;
	RdrDrawList			*draw_list_sky;
	RdrDrawList			*draw_list_ao;

	bool				sky_draw_list_filled;
	GfxOcclusionBuffer	*occlusion_buffer;

	Vec4				visual_pass_view_z;

	F32					dof_distance;

	WorldRegion			**regions;
	WorldRegionGraphicsData **region_gdatas;
	F32					*camera_positions;
	int					*hidden_object_ids;
	Vec3				region_min, region_max;

	BasicTexture		**scene_texture_swaps;

	Frustum				*nextVisFrustum;

	BlockRange			**terrain_blocks;

	GfxLight			**this_frame_lights;
	GfxLight			**this_frame_shadowmap_lights;

	BasicTexture		*diffuse_warp_texture_character;
	BasicTexture		*diffuse_warp_texture_world;
	BasicTexture		*ambient_cube;
	BasicTexture		*env_cubemap_from_sky;
	BasicTexture		*env_spheremap_from_sky;

	Vec3				character_backlight;

	//wind stuff (stored here for better cache coherency)
	F32					wind_maxdistance_from_cam;
	F32					wind_current_client_loop_timer;
	bool				wind_disabled;

	F32					scatter_max;

	Vec3				lens_flare_positions[4]; // Positions for lens flares on world objects.
	int					num_object_lens_flares; // Number of lens flares on world objects.
	int					num_sky_lens_flares; // Number of lens flares on skydomes.

	PSSMSettings		pssm_settings;

} GfxGlobalDrawParams;
STATIC_ASSERT(IS_ALIGNED(offsetof(GfxGlobalDrawParams, nextVisProjection), 16));

typedef struct GfxStageInput
{
	RdrSurface *surface;
	RdrSurfaceBuffer buffer;
	int snapshot_idx;
} GfxStageInput;

typedef struct GfxStageOutput
{
	RdrSurface *surface;
	RdrSurfaceBufferMaskBits write_mask;
} GfxStageOutput;

typedef struct GfxStage
{
	bool enabled;
	GfxStageInput inputs[SBUF_MAX];
	GfxStageOutput output;
	RdrSurface *temps[SBUF_MAX];
} GfxStage;

typedef struct GfxTempSurface
{
	bool in_use;
	U32 last_used_frame;
	RdrSurfaceParams creation_params;
	RdrSurface *surface;
} GfxTempSurface;

typedef struct GfxRenderAction
{
	GfxGlobalDrawParams gdraw; // First for alignment

	bool is_offscreen;
	bool draw_sky;
	GfxActionType action_type;
	RdrSurface *outputSurface;
	int outputSnapshotIdx;

	U32 renderSize[2];
	F32 texReduceResolutionScale;

	GfxCameraView *cameraView;
	GfxCameraController *cameraController;

	GfxTempSurface *bufferSSAOPrepass; // deferred shadow buffer
	GfxTempSurface *bufferDeferredShadows; // deferred shadow buffer
	GfxTempSurface *bufferMRT; // for deferred lighting
	GfxTempSurface *bufferLDR; // for exposed lit output
	GfxTempSurface *bufferHDR; // for underexposed lit output
	GfxTempSurface *bufferLDRandHDR; // for both if doing high quality bloom
	GfxTempSurface *bufferTempFullSize; // for blurs and other temporary needs, set to renderSize
	GfxTempSurface *bufferTempDepthMSAA; // for low res alpha's depth+stencil if msaa is on
	GfxTempSurface *bufferFinal;		// for final 3d output if renderSize != screenSize
	GfxTempSurface *bufferOutline;		// for outline pixels if using outline antialiasing, or two-step outlining
	GfxTempSurface *bufferOutlinePCA;	// for line eqn data
	GfxTempSurface *bufferScatter;		// for scattering weights
	GfxTempSurface *bufferUI;			// for renderscale/UI if doing final_postprocess

	GfxTempSurface *bufferHighlight;	// for targeting highlights

	GfxTempSurface *bloom[2], *bloom_med[2], *bloom_low[2];
	GfxTempSurface *lum64, *lum16, *lum4, *lum1, *lum_measure;
	GfxTempSurface *blueshift_lut, *tonecurve_lut, *colorcurve_lut, *intensitytint_lut;
	GfxTempSurface *postprocess_all_lut;
	GfxTempSurface *lens_zo_64, *lens_zo_16, *lens_zo_4, *lens_zo_1;
	GfxTempSurface *low_res_alpha_edge;
	GfxTempSurface *low_res_alpha_scratch;	// for other temporary needs, set to renderSize / lowResFactor

	GfxTempSurface *freeMe;

	BasicTexture *env_cubemap_from_region, *env_spheremap_from_region;

	GfxStage render_stages[GFX_NUM_STAGES];
	GfxStageInput snapshotLDR;
	GfxStageInput snapshotHDR;
	GfxStageInput opaqueDepth;

	// If present, at the end of gfxUFDoAction
	BasicTexture* postRenderBlitOutputTexture;
	RdrSurface *postRenderBlitOutputSurface;
	
	Vec4 postRenderBlitViewport; // Width, Height, X, Y  - [0..1]
	IVec2 postRenderBlitDestPixel; // X, Y pixels of destination
	bool postRenderBlitDebugThumbnail;

	Vec4 renderViewport; // Width, Height, X, Y in pixels

	WLUsageFlags override_usage_flags;
	U32 override_time : 1;	//< If set, will fix the time value
	U32 allow_indoors : 1;
	U32 allow_bloom : 1;
	U32 use_sun_indoors : 1;
	U32 no_indoor_ambient : 1;

	U32 bTXAAEnable : 1;		// TXAA is enabled in this action.
	U32 nTXAABufferFlip : 1;	// A buffer ping-pong toggle bit for flipping between the last and current TXAA snapshot, since it requires prior TXAA results as input. 
} GfxRenderAction;

typedef struct GfxPerDeviceState
{
	RdrDevice *rdr_device;
	InputDevice *input_device;

	GfxCameraView *primaryCameraView;
	GfxCameraController *primaryCameraController;

	GfxRenderAction **actions;

	GfxTempSurface **temp_buffers;

	GfxSpecialShaderData **special_shaders;

	GfxSpriteList *sprite_list;
    RdrDrawList *draw_list2d;

	// pooled data to reduce memory allocations
	GfxRenderAction *main_frame_action;
	GfxRenderAction **offscreen_actions;
	int next_available_offscreen_action;

	GfxTempSurface *cubemap_lookup_surface;
	U32 cubemap_rendered_frame_start;
	BasicTexture *ssao_reflection_lookup_tex;

	EventOwner *event_timer;

	struct {
		char *auxDeviceName;
		GfxAuxDeviceCloseCallback auxCloseCallback;
		GfxAuxDeviceCallback auxPerFrameCallback;
		void *auxUserData;
	} auxDevice;

	GfxCameraView autoCameraView; // auto-allocated default camera for this device.  Don't use this directly! (gets automatically pointed to)
	GfxCameraController autoCameraController; // auto-allocated default camera for this device.  Don't use this directly! (gets automatically pointed to)

	F32 frameRatePercent; // How many frames to render for each run through the main loop (0..1]
	F32 frameRatePercentBG; // Different rate for when it's in the background (0 = same as FG)
	U32 frames_counted; // Count of frames checked
	U32 frames_rendered; // How many of those were rendered (based on frameRatePercent)
	U32 frame_count_of_last_update; // Used for checking once per device things

	U32 per_device_frame_count; // Like frame_count on gfx_state, but per device

	U32 per_device_show_fps_frame_count; // For FPS calculations
	U32 per_device_last_fps_ticks;	// For FPS calculations
	F32 per_device_fps;

	U32 lastRenderSize[2]; // The last size we were rendering at, to detect resizes and free surfaces efficiently

	U32 skipThisFrame:1; // Set to true if frameRatePercentage dictates (or for other reasons?)
	U32 doNotSkipThisFrame:1; // Set to true to override frameRatePercentage skips for 1 frame
	U32 isAuxDevice:1;
	U32 isInactive:1;

	RdrDevicePerformanceValues rdr_perf_values;
	F32 timeSpentMeasuringPerf;
	RdrDevicePerfTimes rdr_perf_times;

} GfxPerDeviceState;


typedef struct GfxDebugThumbnail {
	TexHandle tex_handle;
	Vec4 viewport;
	const char *title;
	GfxTempSurface *temp_surface; // NULL if not a temp surface copy
	int show_alpha;
	bool is_shadow_map;
	S8 force_mip;
} GfxDebugThumbnail;

typedef enum GfxScreenshotType {
    SCREENSHOT_NONE,
    SCREENSHOT_3D_ONLY,
    SCREENSHOT_WITH_DEBUG,
	SCREENSHOT_DEPTH,
} GfxScreenshotType;

// See gfxHookFrame.
typedef struct GfxHookFrameState
{
	GfxHookBeginFrame pfnHookBeginFrame;
	GfxHookEndFrame pfnHookEndFrame;
} GfxHookFrameState;

typedef struct GfxDebugTextEntry
{
	int iColor;
	int iLength;
} GfxDebugTextEntry;

// Global graphics state
typedef struct GfxState
{
	// See gfxHookFrame.
	GfxHookFrameState currentHookState;
	GfxHookFrameState nextHookState;

	GfxRenderAction *currentAction;
	int currentActionIdx;
	Frustum *currentVisFrustum;
	Vec3 currentCameraFocus;
	GfxCameraView *currentCameraView;
	GfxCameraController *currentCameraController;

	U32 screenSize[2];

	// renderers and surfaces
	U32 currentRendererIndex;
	U32 currentRendererFlag; // (1 << currentRendererIndex)
	RdrSurface *currentSurface;
	RdrSurfaceBufferMaskBits currentSurfaceWriteMask;
	RdrSurfaceFace currentSurfaceFace;
	bool currentSurfaceSet;
	GfxPerDeviceState *currentDevice;
	GfxPerDeviceState **devices;
	RdrDevice **remove_devices; // devices to be removed at the end of the update loop;

	U32 client_frame_timestamp; // in CPU ticks
	F32 client_loop_timer, frame_time;
	F32 cur_time;
	F32 real_frame_time; // Only use this for things that MUST be realtime, like camera control
	U32 frame_count; // actual frames elapsed
	U32 show_fps_frame_count; // For FPS calculations
	U32 last_fps_ticks;	// For FPS calculations
	U32 rendererNeedsShaderReload; // bitmask
	U32 rendererNeedsAnisoReset; // bitmask
	U32 shadersReloadedThisFrame;
	U32 applied_dynamic_settings_timestamp; // timestamp of applying dynamic settings
	U32 allRenderersFlag; // Flag for all valid renderers
	U32 allRenderersFeatures; // Minimum set of supported features across all attached renderers at STARTUP
						// - do not use for testing things like FEATURE_SM30 which can be disabled at run-time

	U32 surface_frames_unused_max;
	U32 surface_frames_unused_max_lowres;

	F32     water_ripple_scale;

	GfxSettings settings;
	GfxFeature features_allowed; // set per project

	U32		hide_detail_in_editor:1;

	U32		make_cubemap:1;
    char	make_cubemap_filename[MAX_PATH];

    // screenshot processing
	char	screenshot_filename[MAX_PATH];
    GfxScreenshotType screenshot_type;
	float screenshot_depth_min;
	float screenshot_depth_max;
	float screenshot_depth_power;
	int jpegQuality;
	bool	screenshot_after_renderscale;
	GfxScreenshotCallBack *screenshot_CB;
	void	*screenshot_CB_userData;
	bool visualize_screenshot_depth;

	char	screenshot_material_name[MAX_PATH];

	char	bodysockatlas_filename[MAX_PATH];
	bool	bodysockatlas_screenshot;

	int		unloadAllNotUsedThisFrame;
	int		quickLoadFonts;
	int		reduce_mip_override;
	U32		reduce_mip_entity;
	U32		reduce_mip_world;
	int		pssm_shadowmap_size;
	int		pssm_shadowmap_size_override;
	int		ambient_cube_res;

	F32		fps, showfps;
	int		showmem;
	size_t	mem_usage_tracked;
	size_t	mem_usage_actual;
	int		showMouseCoords;
	int		showCamPos;
	int		showTerrainPos;
	int		showTime;

	int		wireframe;
	int		sky_wireframe;
	int		safemode;

	GfxAntialiasingMode	antialiasingMode;
	int		antialiasingQuality;

	bool	disable_zprepass;
	bool	force_enable_zprepass;
	bool	zprepass_last_on;
	bool	disable_cam_light;
	bool	stereoscopic_active;

	U8		target_highlight;
	bool	no_htex;
	bool	dxt_non_pow2;
	bool	volumeFog;
	bool	noTangentSpace;
	bool	skip_system_specs_check;
	bool	anaglyph_hack;
	bool	shadow_buffer;
	bool	vertexOnlyLighting;
	bool	uberlighting;
	bool	cclighting; // When not doing uberlighting, use only predefined combinations of lights (constrained combinatorial lighting)
	bool	allow_preload_materials;
	bool	force_preload_materials;
	bool	preload_all_early;
	bool	disableAutoAlwaysOnTop;
	bool	allowAutoAlwaysOnTop;
	bool	shouldShowRestoreButtons;
	U32		compile_all_shader_types_timestamp;
	F32		timeSinceWindowChange;

	int		ui_postprocess;

	bool	render_scale_pointsample;
	bool	forceOffScreenRendering;
	bool	debugDrawStaticCollision;
	bool	debugDrawDynamicCollision;

	DynamicCache *shaderCache;
	DynamicCache *shaderCacheXboxOnPC; // On the PC, where we put the Xbox shaders for -compileAllShaderTypes

	int texLoadNearCamFocus;

	F32 near_plane_dist, far_plane_dist;
	F32 force_fog_clip_dist;

	F32 sky_fade_rate;
	F32 time_rate;

	F32 project_special_material_param;
	F32 target_entity_depth;

	struct {
		int		offsetFromTop;
		int		offsetFromBottom;
		int		offsetFromLeft;
		int		offsetFromRight;
		bool	renderThreadDebug;
		bool	gfx_debug_info;

		bool	disableSprites;

		bool	dont_atlas, atlas_stats;
		int		atlas_display;
		bool	no_auto_lods;
		bool	noZocclusion;
		bool	two_zo_draw_threads;
		bool	wait_for_zocclusion; // 1 - zo starts in gfxStartMainFrameAction, 2 - zo starts after camera controller is run

		bool	disableFog;
		bool	disableDirLights;
		bool	disableDynamicLights;

		bool	noparticles;
		bool	whiteParticles;
		bool	simpleLighting;
		char	simpleMaterials[64]; // Contains a material name to override with
		char	simpleAlphaMaterials[64]; // Only works if simpleMaterials is set. Contains a material name to override alpha sorted materials with.
		bool	whiteTextures;
		bool	showTextureDistance;
		bool	showTextureDensity;
		bool	showMipLevels;
		char	textureOverride[64]; // Texture name to override with
		char	showTextureDensityExclude[64]; // Texture name/wildcard to not swap
		bool	disableIncrementalTex;
		bool	suppressReloadShadersMessage;

		bool	sort_opaque;

		bool	no_fade_in;

		bool	no_clip_terrain;
		bool	no_draw_terrain;
		bool	no_draw_static_world;
		bool	no_draw_dynamics;

		bool	draw_bezier_control_points;
		bool	no_draw_bezier;

		bool	disable_multimon_warning;
		int		check_window_placement;	
		bool	show_model_binning_message;

		bool	show_light_volumes;
		bool	deferredlighting_debug;
		bool	postprocessing_debug;
		bool	postprocessing_lut_debug;
		bool	bloom_debug;
		bool	aux_visual_pass_debug;
		bool	surface_debug;
		int		txaa_debug;

		bool	shadow_debug;
		bool	no_render_shadowmaps;
		bool	shadow_force_ortho_only;
		bool	disable_multiple_point_light_shadow_passes;
		F32		forceShadowQuality;
		bool	drawRagdollShadowsForVisualPass;

		bool	world_cell;
		int		world_cell_3d;

		bool	hdr_2;
		bool	hdr_use_immediate_luminance_measurement;
		bool	show_exposure_transform;
		Vec4	exposure_transform;
		F32		measured_luminance;
		F32		hdr_force_luminance_measurement;
		F32		hdr_lock_ldr_xform;
		F32		hdr_lock_hdr_xform;
		F32		hdr_luminance_point;

		bool	show_light_range;
		bool	show_luminance_history;

		F32		force_unlit_value;

		bool	zprepass_to_rgba;
		bool	zprepass;
		bool	shadow_buffer;
		bool	filling_sky_debug_text;
		bool	no_sky;
		bool	no_exposure;
		bool	show_material_cost;
		bool	no_please_wait;
		bool	preload_multiple_lights;

		bool	texWordVerbose;
		bool	postprocess_force_float;
		U32		postprocess_rgba_buffer;

		U32		loaded_geo_count[WL_FOR_MAXCOUNT];
		U32		loaded_geo_size[WL_FOR_MAXCOUNT]; // number of bytes for geometry loaded into video memory

		U8		bShowTimingBars;
		U8		threadPerf;
		bool	threadPerfAll;

		bool	runNVPerf;

		bool	noOnepassObjects;
		F32		onepassDistance;
		F32		onepassScreenSpace;

		bool	echoTempSurfaceCreation;

		bool	no_nalz_warnings;
		F32		no_nalz_warnings_timeout;

		U8		material_preload_debug;
		bool	did_preload_materials;
		bool	error_on_non_preloaded_materials;
		bool	show_frame_delay;

		bool	emulate_vs_time;
		bool	emulate_vs_time_use_old_intel;
		bool	show_vs_time;
		bool	show_settings;
		bool	show_draw_list_histograms;
		bool	show_frame_counters;
		bool	show_file_counters;
		FrameCounts last_frame_counts;
		FrameCounts frame_counts;
		FrameCountsHist accumulated_frame_counts;

		const char **show_stages_text;
		int *show_stages_value;
		bool show_stages;

		bool	show_frame_times;

		struct 
		{
			int frustum_debug_mode;
			Frustum static_frustum;
			Mat44 static_projection_matrix;
			Vec3 static_camera_focus;
		} frustum_debug;

		DOFValues	dof_debug;

		GPolySet	*polyset;
		GMesh		*gmesh;

		struct {
			GfxDebugThumbnail *thumbnails;
			int thumbnails_count, thumbnails_max;
			int thumbnail_display;
			F32 stretch;
			int thumbnail_zoomout;
			F32 stretchout;
			bool point_sample;
			bool snap;
			bool fixed;
			char * strScreenshotFilename;
			char * strScreenshotRequestTitle;
		} thumbnail;

		U8		zocclusion_enabledebug;
		U8		zocclusion_hier_max;
		U8		zocclusion_norestrict;

		U8		show_instanced_objects;

		bool	draw_all_regions;

		U8		framegrab_grabnextframe;
		F32		framegrab_show;
#define MAX_FRAMEGRAB_TEXTURES 3
		BasicTexture *framegrab_texture[MAX_FRAMEGRAB_TEXTURES];
		BasicTexture *framegrab_texture2;
		BasicTexture *framegrab_texture3;
		U32		framegrab_timestamp[MAX_FRAMEGRAB_TEXTURES];
		U32		framegrab_discard_counter[MAX_FRAMEGRAB_TEXTURES];

		StuffBuff queued_debug_text;
		int queued_debug_text_count;
		char** debug_sprites;
		struct {
			const char *name;
			F32 x, y, z, w, h;
		} debug_sprite_3d;

		MaterialNamedConstant **eaNamedConstantOverrides;

		bool resetFrameRateStabilizer;

		bool printStallTimes;
		bool fpsgraph_show;
		bool fpsgraph_showHistogram;
		struct {
			int hist_index;
			float spfhist[256];
			float spfhist_main[256]; // Main thread (total - GPU wait)
			bool stallhist[256];
			int mspfhistogram[256];
			int mspfhistogram_total;
		} fpsgraph;
		F32 stalls_per_frame;

		int bottleneck_hist[5][GfxBottleneck_MAX]; // last 5 seconds

		GfxVisualizationSettings vis_settings;

		bool disableColorCorrection;
		bool disableSkyBloomPass;
		bool disableOpaqueBloomPass;
		bool disableNonDeferredBloomPass;
		bool disableLDRToHDRDepthCopy;
		bool disableLDRToHDRColorCopy;
		int overrideHDRAntialiasing;

		bool bodysockTexAA;

		bool show_rooms;
		bool show_room_partitions;
		int show_room_shadow_graph; //1 = shadow lights, 2 = all lights, 3 = animate

		F32 overrideTime;

		int	forceReflectionLevel;
		bool dynDrawNoPreSwappedMaterials;
		bool danimPIXLabelModels;
		bool delay_sending_lights;
		bool debug_low_res_alpha;
		bool disable_sky_volumes;

		bool too_many_sprites_for_16bit_idx;
		bool force_32bit_sprite_idx_buffer;

		bool disable_vertex_lights;

		bool disable_3d_texture_flush;
		S8 model_lod_offset;
		S8 model_lod_force;
		bool bHideDeferredUnlit;
		bool bOneDeferredLight;
	} debug;

	GfxRecordCamMatPostProcessFn record_cam_pp_fn;

	U32 pastStartup:1;
	U32 inEditor:1;
	U32 bDisableVisScale:1; // for the LOD editor
	U32 cluster_load:1;
	F32 editorVisScale;
	void(*gfxUIUpdateCallback)(void);
} GfxState;

extern GfxState gfx_state;

void gfxClearSpriteLists(void);
void gfxUnsetActiveSurface(RdrDevice *device);
void gfxOncePerFramePerDevice(void);

void gfxQueryPerfTimes(RdrDevice *rdr_device);

__forceinline static bool gfxFeatureEnabled(GfxFeature feature)
{
	return !!(gfx_state.settings.features_supported & gfx_state.settings.features_desired & gfx_state.features_allowed & feature);
}

__forceinline static void gfxBeginSection(const char *name)
{
#if defined(PROFILE) && _XBOX
	if (wtIsThreaded(gfx_state.currentDevice->rdr_device->worker_thread))
		PIXBeginNamedEvent(0, name);
#endif
	rdrBeginNamedEvent(gfx_state.currentDevice->rdr_device, name);
}

__forceinline static void gfxGpuMarker(int iTimerIdx)
{
	if (isDevelopmentMode())
		wtQueueCmd(gfx_state.currentDevice->rdr_device->worker_thread, RDRCMD_GPU_MARKER, &iTimerIdx, sizeof(iTimerIdx));
}

__forceinline static void gfxEndSection(void)
{
	rdrEndNamedEvent(gfx_state.currentDevice->rdr_device);
#if defined(PROFILE) && _XBOX
	if (wtIsThreaded(gfx_state.currentDevice->rdr_device->worker_thread))
		PIXEndNamedEvent();
#endif
}

void gfxUnloadAllNotUsedThisFrame(void);
void gfxUnloadAllNotUsedThisFrame_check(void);

__forceinline static RdrSurface *gfxGetStageOutput(GfxStages stage)
{
	return gfx_state.currentAction->render_stages[stage].output.surface;
}

__forceinline static RdrSurface *gfxGetStageInputSurface(GfxStages stage, int input_idx)
{
	return gfx_state.currentAction->render_stages[stage].inputs[input_idx].surface;
}

__forceinline static RdrSurfaceBuffer gfxGetStageInputBuffer(GfxStages stage, int input_idx)
{
	return gfx_state.currentAction->render_stages[stage].inputs[input_idx].buffer;
}

__forceinline static TexHandle gfxGetStageInputTexHandle(GfxStages stage, int input_idx)
{
	return rdrSurfaceToTexHandleEx(	gfx_state.currentAction->render_stages[stage].inputs[input_idx].surface, 
									gfx_state.currentAction->render_stages[stage].inputs[input_idx].buffer,
									gfx_state.currentAction->render_stages[stage].inputs[input_idx].snapshot_idx,
									0, false);
}

__forceinline static void gfxSetStageActiveSurface(GfxStages stage)
{
	gfxSetActiveSurfaceEx(	gfx_state.currentAction->render_stages[stage].output.surface, 
							gfx_state.currentAction->render_stages[stage].output.write_mask,
							0);
}

__forceinline static void gfxSetStageActiveSurfaceEx(GfxStages stage, int write_mask)
{
	gfxSetActiveSurfaceEx(	gfx_state.currentAction->render_stages[stage].output.surface, 
							write_mask,
							0);
}

__forceinline static RdrSurface *gfxGetStageTemp(GfxStages stage, int temp_idx)
{
	return gfx_state.currentAction->render_stages[stage].temps[temp_idx];
}

__forceinline static TexHandle gfxGetStageTempTexHandle(GfxStages stage, int temp_idx)
{
	return rdrSurfaceToTexHandle(gfx_state.currentAction->render_stages[stage].temps[temp_idx], SBUF_0);
}

__forceinline static bool gfxIsStageEnabled(GfxStages stage)
{
	return gfx_state.currentAction->render_stages[stage].enabled;
}

__forceinline static bool gfxActionAllowsIndoors(const GfxRenderAction *action)
{
	return !action || action->allow_indoors;
}

__forceinline static bool gfxActionAllowsBloom(const GfxRenderAction *action)
{
	return !action || action->allow_bloom;
}

__forceinline static bool gfxActionUseIndoorAmbient(const GfxRenderAction *action)
{
	return !action || !action->no_indoor_ambient;
}

__forceinline static void gfxGlobalDrawRemoveLight(GfxGlobalDrawParams *gdraw, GfxLight *light)
{
	eaFindAndRemoveFast(&gdraw->this_frame_lights, light);
	eaFindAndRemoveFast(&gdraw->this_frame_shadowmap_lights, light);
}

__forceinline static void gfxActionRemoveLight(GfxRenderAction *action, GfxLight *light)
{
	gfxGlobalDrawRemoveLight(&action->gdraw, light);
}

__forceinline static void gfxDeviceRemoveLightFromShadowmapHistory(GfxPerDeviceState *device, GfxLight *light)
{
	gfxActionRemoveLight(device->main_frame_action, light);
}

__forceinline static void gfxActionClearShadowmapLightHistory(GfxRenderAction *action)
{
	if (action)
		eaClear(&action->gdraw.this_frame_shadowmap_lights);
}

__forceinline static void gfxClearDeviceShadowmapLightHistory(GfxPerDeviceState *device)
{
	if (device)
	{
		int i;
		for (i = 0; i < eaSize(&device->actions); ++i)
			gfxActionClearShadowmapLightHistory(device->actions[i]);
		gfxActionClearShadowmapLightHistory(device->main_frame_action);
	}
}

__forceinline static void gfxClearCurrentDeviceShadowmapLightHistory()
{
	gfxClearDeviceShadowmapLightHistory(gfx_state.currentDevice);
}

__forceinline static void gfxHsvToRgb(const Vec3 in, Vec3 out)
{
	if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING)) {
		hsvToLinearRgb(in, out);
	} else {
		hsvToRgb(in, out);
	}
}

#define DRAWLIST_DEFINED_TEX__LENSFLARE_ZO_DEPTH_TEX	(RDR_FIRST_SURFACE_FIXUP_TEXTURE+0)
#define DRAWLIST_DEFINED_TEX__OUTLINE					(RDR_FIRST_SURFACE_FIXUP_TEXTURE+1)

#define SETUP_INSTANCE_PARAMS	{													\
			if (geo_draw->subobject_count==1)										\
				instance_params.per_drawable_data = &per_drawable_data;				\
			else																	\
				instance_params.per_drawable_data = _alloca(geo_draw->subobject_count * sizeof(instance_params.per_drawable_data[0])); }

bool gfxUFIsCurrentActionBloomDebug();
