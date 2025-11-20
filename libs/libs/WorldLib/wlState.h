#pragma once
GCC_SYSTEM

#include "WorldLib.h"
#include "Frustum.h"
#include "Materials.h"

C_DECLARATIONS_BEGIN

extern bool gbMakeBinsAndExit;

typedef struct WorldCell WorldCell;
typedef struct WorldDrawableEntry WorldDrawableEntry;
typedef struct GenesisZoneNodeLayout GenesisZoneNodeLayout;
typedef struct WorldRegionLODSettings WorldRegionLODSettings;

typedef struct WorldLibState {
	U32		initialized : 1;

	U32 	frame_count; // how many times has worldLibUpdate been called?
	F32 	frame_time;
	F32 	time; // 12 = noon
	U32     time_is_forced : 1;
	F32 	timerate; // in hours per second ( e.g. time += time * timestep )
	F32 	timescale;
	F32 	timeStepScaleDebug; // In seconds per second, pass timeStepScaleDebug*timeStepScaleGame to frameLockedTimerStartNewFrame()
	F32		timeStepScaleGame;
	F32		timeStepScaleLocal; // Not sync'd between server and client
	F32 	serverTimeDiff; // Set on client only, time difference to the server.
	Frustum	last_camera_frustum;
	const WorldRegionLODSettings* current_lod_settings;

	WorldLibLoadFlags load_flags;
	U32 	is_server : 1;
	U32		dont_load_beacons	: 1;
	U32		dont_load_beacons_if_space : 1;
	U32		dynEnabled			: 1;
	U32		delete_hoggs		: 1;
	U32		dont_take_photos	: 1;
	U32		water_reloaded_this_frame : 1;
	U32		enableDiffuseWarpTex : 1;
	U32		stop_map_transfer : 1;  // If set, don't let the server transfer us automatically
	U32		genesis_fail_flag : 1;
	U32		no_sound_for_temp_group : 1;
	U32		interactibles_use_character_light : 1;
	bool	matte_materials;
	int		photo_iterations;

	U32		controller_script_wait_map_load : 1;

	bool	binAllGeos;
	bool	verifyAllGeoBins;
	bool	checkForCorruptLODs;

	F32 	lod_scale, terrain_lod_scale, world_cell_load_scale;
	bool	keep_cell_data_loaded;
	
	int		selected_wireframe;
	Vec4	selectedTintColor;

	bool	allow_all_private_maps;
	bool	allow_group_private_maps;

	bool	draw_high_detail;
	bool	draw_high_fill_detail;

	bool	player_faction_changed;

	bool	editor_UI_needs_update;

	U32		desired_quality;

	wleIsActorDisabledFunc wle_is_actor_disabled_func;
	wlePatrolPointGetMatFunc wle_patrol_point_get_mat_func;

	GenesisZoneNodeLayout *genesis_node_layout; // For visualization only
	GenesisGenerateFunc genesis_generate_func;
	GenesisGenerateMissionsFunc genesis_generate_missions_func;
	GenesisGenerateEpisodeMissionFunc genesis_generate_episode_mission_func;
	GenesisGetSpawnPositionsFunc genesis_get_spawn_pos_func;
	bool genesis_error_on_encounter1;
	UGCGetDefaultVariableFunc ugc_get_default_variables_func;

	UpdateLightFunc update_light_func;
	RemoveLightFunc remove_light_func;
	UpdateAmbientLightFunc update_ambient_light_func;
	RemoveAmbientLightFunc remove_ambient_light_func;

	RoomLightingUpdateFunc	room_lighting_update_func;

	CreateStaticLightCacheFunc create_static_light_cache_func;
	FreeStaticLightCacheFunc free_static_light_cache_func;
	CreateDynLightCacheFunc create_dyn_light_cache_func;
	FreeDynLightCacheFunc free_dyn_light_cache_func;
	ForceUpdateLightCachesFunc force_update_light_caches_func;
	InvalidateLightCacheFunc invalidate_light_cache_func;
	ComputeStaticLightingFunc compute_static_lighting_func;
	ComputeTerrainLightingFunc compute_terrain_lighting_func;
	UpdateIndoorVolumeFunc update_indoor_volume_func;

	DynFxUpdateLightFunc dfx_update_light_func;
	DynFxScreenShakeFunc dfx_screen_shake_func;
	DynFxCameraMatrixOverrideFunc dfx_camera_matrix_override_func;
	DynFxCameraFOVFunc dfx_camera_fov_func;
	DynFxSetCameraDelayFunc dfx_camera_delay_func;
	DynFxSetCameraLookAtFunc dfx_camera_lookAt_func;
    DynFxWaterAgitateFunc dfx_water_agitate_func;
	DynFxSkyVolumePushFunc dfx_sky_volume_push_func;
	DynFxSkyVolumeOncePerFrameFunc dfx_sky_volume_once_per_frame_func;
	DynFxSetAlienColor dfx_set_alien_color;

	DynFxHitReactImpactFunc dfx_hit_react_impact_func;
	DynAnimHitReactImpactFunc danim_hit_react_impact_func;

	CheckSkeletonVisibiltyFunc check_skeleton_visibility_func;
	CheckParticleVisibiltyFunc check_particle_visibility_func;

	ForceCostumeReloadFunc force_costume_reload_func;

	GetNumEntitiesFunc get_num_entities_func;
	GetNumEntitiesFunc get_num_client_only_entities_func;

	CreateSoundFunc create_sound_func;
	RemoveSoundFunc remove_sound_func;
	SoundValidateFunc sound_validate_func;
	SoundRadiusFunc sound_radius_func;
	SoundVolumeUpdateFunc sound_volume_update_func;
	SoundVolumeDestroyFunc sound_volume_destroy_func;
	SoundConnUpdateFunc sound_conn_update_func;
	SoundConnDestroyFunc sound_conn_destroy_func;
	SoundEventExistsFunc sound_event_exists_func;
	SoundGetProjectFileByEventFunc sound_get_project_file_by_event_func;

	PlayableCreateFunc playable_create_func;
	PlayableDestroyFunc playable_destroy_func;

	CivGenCreateFunc civgen_create_func;
	CivGenDestroyFunc civgen_destroy_func;

	CivilianVolumeCreateFunc civvolume_create_func;
	CivilianVolumeDestroyFunc civvolume_destroy_func;

	AIGetStaticCheckExprContextFunc ai_static_check_expr_context_func;

	TexFindAndFlagFunc tex_find_func;
	TexGetNameFunc tex_name_func;
	TexGetNameFunc tex_fullname_func;
	TexIsFunc tex_is_normalmap_func;
	TexIsFunc tex_is_dxt5nm_func;
	TexIsFunc tex_is_cubemap_func;
	TexIsFunc tex_is_volume_func;
	TexIsFunc tex_is_alpha_bordered_func;

	MaterialCheckOccluderFunc material_check_occluder_func;
	MaterialCheckSwapsFunc material_check_swaps_func;
	MaterialApplySwapsFunc material_apply_swaps_func;
	MaterialDrawFixupFunc materialdraw_fixup_func;
	MaterialInitFunc material_init_func;

	MaterialValidateForFx material_validate_for_fx;

	NotifySkyGroupFreedFunc notify_sky_group_freed_func;

	CheckEntityExistsFunc check_entity_exists_func;
	CheckEntityHasExistedFunc check_entity_has_existed_func;

	CheckEnemyFactionFunc check_enemy_faction_func;

	InteractableTestFunc is_consumable_func;
	InteractableTestFunc is_traversable_func;

	InteractionNodeFunc interaction_node_free_func;

	LinkInfoFromIDFunc link_info_from_id_func;

	WL_DrawLine3D_2Func drawLine3D_2_func;
	WL_DrawBox3DFunc drawBox3D_func;
	WL_DrawModelFunc drawModel_func;
	WL_DrawAxesFromTransform_Func drawAxesFromTransform_func;

	// Useful for drawing debug information without regard to being on server or client
	WL_AddPointClientFunc drawPointClient_func;
	WL_AddLineClientFunc drawLineClient_func;
	WL_AddBoxClientFunc drawBoxClient_func;
	WL_AddTriClientFunc drawTriClient_func;
	WL_AddQuadClientFunc drawQuadClient_func;

	FreeWorldNodeCallback free_world_node_callback;

	UnloadMapGameCallback *unload_map_callbacks;
	LoadMapGameCallback *load_map_callbacks;
	LoadMapGameCallback *reload_map_callbacks;

	EntRefreshCallback ent_refresh_callback;

	// Change these to the above EArray?
	UnloadMapGameCallback unload_map_game_callback;
	LoadMapGameCallback load_map_game_callback;
	LoadMapGameCallback reload_map_game_callback;
	LoadMapGameCallback edit_map_game_callback;
	LoadMapGameCallback save_map_game_callback;
	bool disable_game_callbacks;
	bool HACK_disable_game_callbacks; //< just a hack until a better solution is found -- MJF

	LoadMapBeginEndCallback load_map_begin_end_callback;

	UnloadMapGameCallback unload_map_civ_callback;
	LoadMapGameCallback load_map_civ_callback;

	UnloadMapGameCallback unload_map_bcn_callback;
	LoadMapGameCallback load_map_bcn_callback;

	LayerChangedModeCallback layer_mode_callback;
	RenameTerrainSourceCallback rename_terrain_source_callback;
	SaveTerrainSourceCallback save_terrain_source_callback;
	AddTerrainSourceCallback add_terrain_source_callback;
	CreateEncounterInfoCallback create_encounter_info_callback;

	AllocWorldGraphicsDataFunc alloc_world_graphics_data;
	FreeWorldGraphicsDataFunc free_world_graphics_data;

	AllocWorldRegionGraphicsDataFunc alloc_region_graphics_data;
	FreeWorldRegionGraphicsDataFunc free_region_graphics_data;
	GfxTickSkyData tick_sky_data_func;

	GfxDynamicParticleMemUsageCallback particle_mem_usage_callback;

	GfxSettingDetailCallback gfx_setting_world_detail_callback;
	GfxSettingDetailCallback gfx_setting_character_detail_callback;
	GfxSettingCallback gfx_get_cluster_load_setting_callback;

	GfxSplatDestroyCallback gfx_splat_destroy_callback;

	ParseTable *type_worldCellGfxData;

	GfxAddMapPhotoFunc gfx_add_map_photo_func;
	GfxTakeMapPhotosFunc gfx_take_map_photos_func;
	GfxMapPhotoRegisterFunc gfx_map_photo_register;
	GfxMapPhotoUnregisterFunc gfx_map_photo_unregister;
	GfxUpdateMapPhotoFunc gfx_update_map_photo;
	GfxDownRezMapPhotoFunc gfx_downrez_map_photo;

	GfxBodysockTextureCreateCallback gfx_bodysock_texture_create_callback;
	GfxBodysockTextureReleaseCallback gfx_bodysock_texture_release_callback;

	GfxCheckModelLoadedCallback gfx_check_model_loaded_callback;

	GfxMaterialHasTransparency gfx_material_has_transparency;
	GfxMaterialGetTextures gfx_material_get_textures;
	GfxTextureSaveAsPNG gfx_texture_save_as_png;
	GfxGenericCallback gfx_material_reload_all;

	UGCBeforeClientZoneMapLoadFunc before_client_zone_map_load_fn;
	UGCAfterClientZoneMapLoadFunc after_client_zone_map_load_fn;

	GetSimplygonMaterialIdFromTable get_simplygon_material_id_from_table;
	GetMaterialClusterTexturesFromMaterial get_material_cluster_textures_from_material;
	CalculatVertexLightingForGMesh calculate_vertex_lighting_for_gmesh;
	GfxCheckClusterLoadedCallback gfx_check_cluster_loaded;
	GfxWCGFreeClusterTexSwaps gfx_cluster_close_tex_swaps;
	GfxGenericSetBoolCallback gfx_cluster_set_cluster_state;
	GfxGenericGetBoolCallback gfx_cluster_get_cluster_state;

	EncounterHackSaveLayerFunc encounter_hack_callback; // TomY ENCOUNTER_HACK

	LoadMapGameCallback load_map_editorserver_callback;

	MastermindDefUpdatedCallback mastermind_def_updated_callback;
	MastermindSpawnerUpdateFunc mastermind_room_update_func;
	MastermindSpawnerUpdateFunc mastermind_room_destroy_func;


	volatile GeoMemUsage geo_mem_usage;

	struct
	{
		U32		loaded_model_count[WL_FOR_MAXCOUNT];
		U32		loaded_model_size[WL_FOR_MAXCOUNT]; // number of bytes for models loaded into system memory

		int		world_animation_update_count;

		WorldCell *world_cell;
		WorldDrawableEntry *drawable_entry;

		bool	disable_world_animation;

		bool	disable_associated_regions;

		bool	hide_encounter_2_actors;
		bool	hide_encounter_2_patrols;

		struct
		{
			U32 beaconDebug : 1;
		} flags;
	} debug;

} WorldLibState;

extern WorldLibState wl_state;

__forceinline static bool wlIsServer(void)
{
	return wl_state.is_server;
}

__forceinline static bool wlIsClient(void)
{
	return !wl_state.is_server;
}

__forceinline static void wlSetInteractibleUseCharacterLighting()
{
	wl_state.interactibles_use_character_light = true;
}

void wlSetDesiredMaterialScore(int desired_score);

C_DECLARATIONS_END
