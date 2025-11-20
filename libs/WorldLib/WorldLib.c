#define GENESIS_ALLOW_OLD_HEADERS
#include "WorldLib.h"
#include "utilitiesLib.h"

#include "wlState.h"
#include "WorldCell.h"
#include "wlModelLoad.h"
#include "wlModelData.h"
#include "wlModelReload.h"
#include "AutoLOD.h"
#include "ObjectLibrary.h"
#include "wlCommandParse.h"
#include "wlTimePrivate.h"
#include "wlSkelInfo.h"
#include "wlGenesis.h"
#include "dynEngine.h"
#include "EditorServerMain.h"
#include "wlPhysicalProperties.h"
#include "MaterialsPrivate.h"
#include "wlBeacon.h"
#include "wlVolumes.h"
#include "PhysicsSDK.h"
#include "dynAnimTrack.h"
#include "dynCostume.h"
#include "dynFxInterface.h"
#include "partition_enums.h"
#include "StringCache.h"
#include "ReferenceSystem_Internal.h"
#include "WorldCellStreamingPrivate.h"
#include "AutoGen/WorldLib_autogen_QueuedFuncs.h"

AUTO_RUN_ANON(memBudgetAddMapping("worldlibstructs.h", BUDGET_World););
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

U32 wl_frame_timestamp;

// Whoever calls this first (GraphicsLib on the client, WorldLib on the server) gets
// to update it once, so that GfxLib and WorldLib are using the same timestamp for each frame
U32 wlCalcNewFrameTimestamp(int *master)
{
	static bool assigned_master;
	if (*master == 0) {
		if (!assigned_master) {
			assigned_master = true;
			*master = 1;
		} else
			*master = 2;
	}
	if (*master == 1)
		wl_frame_timestamp = timerCpuTicks();
	return wl_frame_timestamp;
}

int wlDontLoadBeacons(void)
{
	if(gbMakeBinsAndExit)
		return true;

	if(wl_state.dont_load_beacons_if_space)
	{
		WorldRegion **regions = worldGetAllWorldRegions();
		int i;

		for(i=0; i<eaSize(&regions); i++)
		{
			if(worldRegionGetType(regions[i])==WRT_Space ||
				worldRegionGetType(regions[i])==WRT_SectorSpace)
			{
				return true;
			}
		}
	}

	return wl_state.dont_load_beacons;
}

void wlSetLoadFlags(WorldLibLoadFlags load_flags)
{
	wl_state.load_flags |= load_flags;
}

WorldLibLoadFlags wlGetLoadFlags()
{
	return wl_state.load_flags;
}

void checkForCoreData(void)
{
	// Sanity check for having C:\Core or C:\Project\CoreData in the gameDataDir list.
	if (isDevelopmentMode() && !beaconIsBeaconizer()) {
		if (!fileExists("materials/system/white.Material") && !fileExists("materials/system/engine/white.Material")) {
			// If this file is not found, then something has gone wrong setting up the game data directories.
			Errorf("Cannot find required Core data files.  Try running a Get Latest Version .bat file to get latest on Cryptic, Core, and the current project.  Exiting...");
			assert(0);
		}
	}
}

void wlSetIsServer(bool is_server)
{
	wl_state.is_server = !!is_server;
}

void wlSetDeleteHoggs(bool delete_hoggs)
{
	wl_state.delete_hoggs = !!delete_hoggs;
}

// If graphicslib isn't linked in, it'll hit this dependency
AUTO_STARTUP(GraphicsLibEarly);
void fakeGfxEarly(void)
{

}

AUTO_STARTUP(GraphicsLib);
void fakeGfxStartup(void)
{

}

AUTO_STARTUP(Entity);
void fakeEntityStartup(void)
{

}

AUTO_STARTUP(Sound);
void fakeSoundStartup(void)
{

}

AUTO_STARTUP(PetContactLists);
void fakePetContactListsStartup(void)
{

}

AUTO_STARTUP(Interaction);
void fakeInteractionStartup(void)
{

}

AUTO_STARTUP(WorldLibMain) ASTRT_DEPS(GraphicsLibEarly, Entity, Sound, WorldOptionalActionCategories, AS_ActivityLogEntryTypes, DynAnimStances);
void worldLibStartup(void)
{
	if (IsAppServerBasedType())//!IsGameServerBasedType() && !IsClient())
	{
		return;
	}

	loadstart_printf("WorldLibMain startup...");

	verifyWorldCellCRCHasNotChanged();

	wl_state.initialized = 1;
	wl_state.selected_wireframe = 2;
	wl_frame_timestamp = timerCpuTicks();

	checkForCoreData();

	utilitiesLibStartup();

	if (!RefSystem_DoesDictionaryExist("SkyInfo"))
		createServerSkyDictionary(); // need a SkyInfo dictionary for the ZoneMaps to parse correctly

	if (isDevelopmentMode() && IsGameServerSpecificallly_NotRelatedTypes())
		worldGridDeleteAllOutdatedBins();

#if !PSDK_DISABLED
	psdkInit(1); // Before wlLoadModelHeaders
#endif

	if(!(wl_state.load_flags & WL_NO_LOAD_PROFILES))
	{
		physicalPropertiesLoad();	
	}

	if (!(wl_state.load_flags & WL_NO_LOAD_MATERIALS)) {
		materialLoad();
	}
	if (!(wl_state.load_flags & WL_NO_LOAD_MODELS)) {
		geoLoadInit();
		lodinfoLoad();
		modelReloadInit();
		wlLoadModelHeaders();
		lodinfoVerify();
	}

    wlVolumeStartup();

	if (!IsClient() || gbMakeBinsAndExit)
		objectLibraryLoad();

	wl_state.current_lod_settings = &defaultLODSettings;
	dynStartup();

	if (!(wl_state.load_flags & WL_NO_LOAD_COSTUMES) && ( isDevelopmentMode() || !IsGameServerBasedType() ) )
	{
		assert(!(wl_state.load_flags & WL_NO_LOAD_MATERIALS)); // Depends on Materials
		wlSkelInfoLoadAll();
	}
	/*
	if(GetAppGlobalType() != GLOBALTYPE_GAMESERVER || !stringCacheSharingEnabled())
	{
		dynAnimTrackUnloadAllUnreferenced(); // needs to happen after wlSkelInfoLoadAll for the scale animations
	}
	*/
	worldLibSetLodScale(1, 1, 1, false);

	if (!(wl_state.load_flags & WL_NO_LOAD_MATERIALS)) {
		materialCheckDependencies();
	}

	loadend_printf("WorldLibMain startup done.");
}

AUTO_STARTUP(WorldLibZone) ASTRT_DEPS(WorldLib, PetContactLists, Interaction, MissionPlayTypes);
void worldLibZoneStartup(void)
{
	loadstart_printf("WorldLibZone startup...");

	if( wl_state.before_client_zone_map_load_fn ) {
		wl_state.before_client_zone_map_load_fn();
	}

	if (!(wl_state.load_flags & WL_NO_LOAD_ZONEMAPS))
	{
		worldLoadZoneMaps();
		worldResetWorldGrid();

		if (wl_state.is_server)
		{
			worldCreatePartition(1, false); // Pre-create first collisions space for use while loading map
		}
		else
		{
			worldCreatePartition(PARTITION_CLIENT, false); // Pre-create first collisions space for use while loading map
		}

		worldLoadEmptyMap();
	}
	editorServerInit();
	wlTimeInit();

	wlInteractionSystemStartup();

	beaconInit();

	if( wl_state.after_client_zone_map_load_fn ) {
		wl_state.after_client_zone_map_load_fn();
	}

	loadend_printf("WorldLibZone startup done.");
}

void worldLibShutdown(void)
{
}

AUTO_STARTUP(WorldLib) ASTRT_DEPS(GraphicsLib, WorldLibMain, MissionPlayTypes);
void worldLibStartupPostGraphicsLib(void)
{
	if (!(wl_state.load_flags & WL_NO_LOAD_COSTUMES) && !(wl_state.load_flags & WL_NO_LOAD_DYNFX) ) {
		dynCostumeInfoLoadAll(); // costumes used for FX
	}
	#ifndef NO_EDITORS
	{
		if (wlIsServer() && areEditorsAllowed()) {
			genesisResourceLoad();
		}
	}
	#endif
}

AUTO_STARTUP(Dynamics) ASTRT_DEPS(WorldLib);
void worldLibStartupDynamics(void)
{
	wl_state.dynEnabled = 1;
}

void worldLibOncePerFrame(F32 fFrameTime)
{
	static int timestamp_master;
	PERFINFO_AUTO_START("WorldLibUpdate", 1);
	++wl_state.frame_count;
	wl_state.frame_time = fFrameTime;
	wlCalcNewFrameTimestamp(&timestamp_master);
	dynSystemUpdate(fFrameTime);

	if (!(wl_state.load_flags & WL_NO_LOAD_MODELS)) {
		PERFINFO_AUTO_START("model loading", 1);
		geoLoadCheckThreadLoader();
		modelReloadCheck();
		if (getModelReloadRetryCount() == 0)
			objectLibraryDoneReloading();
		checkLODInfoReload();
		modelDataOncePerFrame();
		PERFINFO_AUTO_STOP_CHECKED("model loading");
	}
	wlTimeUpdate();
	wlInteractionOncePerFrame(fFrameTime);
	worldGridOncePerFrame();

	if (wlIsServer())
		editorServerSendQueuedReplies();
	
	PERFINFO_AUTO_STOP_CHECKED("WorldLibUpdate");
}

void worldLibSetLightFunctions(UpdateLightFunc update_light_func, RemoveLightFunc remove_light_func, 
							   UpdateAmbientLightFunc update_ambient_light_func, RemoveAmbientLightFunc remove_ambient_light_func, 
							   RoomLightingUpdateFunc room_lighting_update_func)
{
	wl_state.update_light_func = update_light_func;
	wl_state.remove_light_func = remove_light_func;
	wl_state.update_ambient_light_func = update_ambient_light_func;
	wl_state.remove_ambient_light_func = remove_ambient_light_func;
	wl_state.room_lighting_update_func = room_lighting_update_func;
}

void worldLibSetLightCacheFunctions(CreateStaticLightCacheFunc create_static_light_cache_func, FreeStaticLightCacheFunc free_static_light_cache_func, 
									CreateDynLightCacheFunc create_dyn_light_cache_func, FreeDynLightCacheFunc free_dyn_light_cache_func, 
									ForceUpdateLightCachesFunc force_update_light_caches_func, InvalidateLightCacheFunc invalidate_light_cache_func,
									ComputeStaticLightingFunc compute_static_lighting_func, ComputeTerrainLightingFunc compute_terrain_lighting_func,
									UpdateIndoorVolumeFunc update_indoor_volume_func)
{
	wl_state.create_static_light_cache_func = create_static_light_cache_func;
	wl_state.free_static_light_cache_func = free_static_light_cache_func;
	wl_state.create_dyn_light_cache_func = create_dyn_light_cache_func;
	wl_state.free_dyn_light_cache_func = free_dyn_light_cache_func;
	wl_state.force_update_light_caches_func = force_update_light_caches_func;
	wl_state.invalidate_light_cache_func = invalidate_light_cache_func;
	wl_state.compute_static_lighting_func = compute_static_lighting_func;
	wl_state.compute_terrain_lighting_func = compute_terrain_lighting_func;
	wl_state.update_indoor_volume_func = update_indoor_volume_func;
}

void dynFxSetUpdateLightFunc(DynFxUpdateLightFunc dfx_update_light_func)
{
	wl_state.dfx_update_light_func = dfx_update_light_func;
}

void dynFxSetScreenShakeFunc(DynFxScreenShakeFunc dfx_screen_shake_func)
{
	wl_state.dfx_screen_shake_func = dfx_screen_shake_func;
}

void dynFxSetCameraMatrixOverrideFunc(DynFxCameraMatrixOverrideFunc dfx_camera_matrix_override_func)
{
	wl_state.dfx_camera_matrix_override_func = dfx_camera_matrix_override_func;
}

void dynFxSetCameraFOVFunc(DynFxCameraFOVFunc dfx_camera_fov_func)
{
	wl_state.dfx_camera_fov_func = dfx_camera_fov_func;
}

void dynFxSetCameraDelayFunc(DynFxSetCameraDelayFunc dfx_camera_delay_func)
{
	wl_state.dfx_camera_delay_func = dfx_camera_delay_func;
}

void dynFxSetCameraLookAtFunc(DynFxSetCameraLookAtFunc dfx_camera_lookat_func)
{
	wl_state.dfx_camera_lookAt_func = dfx_camera_lookat_func;
}

void dynFxSetSetAlienColorFunc(DynFxSetAlienColor dfx_set_alien_color)
{
	wl_state.dfx_set_alien_color = dfx_set_alien_color;
}

void dynFxSetSkyVolumeFunctions(DynFxSkyVolumePushFunc dfx_sky_volume_push_func, DynFxSkyVolumeOncePerFrameFunc dfx_sky_volume_once_per_frame_func )
{
	wl_state.dfx_sky_volume_push_func = dfx_sky_volume_push_func;
	wl_state.dfx_sky_volume_once_per_frame_func = dfx_sky_volume_once_per_frame_func;
}

void dynFxSetWaterAgitateFunc(DynFxWaterAgitateFunc dfx_water_agitate_func)
{
    wl_state.dfx_water_agitate_func = dfx_water_agitate_func;
}

void dynSetHitReactImpactFuncs(DynFxHitReactImpactFunc dfx_hit_react_impact_func, DynAnimHitReactImpactFunc danim_hit_react_impact_func)
{
	wl_state.dfx_hit_react_impact_func = dfx_hit_react_impact_func;
	wl_state.danim_hit_react_impact_func = danim_hit_react_impact_func;
}

void wlSetGfxSplatDestroyCallback(GfxSplatDestroyCallback gfx_splat_destroy_callback)
{
	wl_state.gfx_splat_destroy_callback = gfx_splat_destroy_callback;
}

void wlSetRdrMaterialHasTransparency(GfxMaterialHasTransparency rdr_material_has_transparency)
{
	wl_state.gfx_material_has_transparency = rdr_material_has_transparency;
}

void wlSetMaterialGetTextures(GfxMaterialGetTextures gfx_material_get_textures)
{
	wl_state.gfx_material_get_textures = gfx_material_get_textures;
}

void wlSetGfxTextureFuncs(GfxTextureSaveAsPNG gfx_texture_save_as_png)
{
	wl_state.gfx_texture_save_as_png = gfx_texture_save_as_png;
}

void wlSetWorldCellGfxDataType(ParseTable *type_worldCellGfxData)
{
	wl_state.type_worldCellGfxData = type_worldCellGfxData;
}

void wlSetGfxTakeMapPhotosFuncs(GfxAddMapPhotoFunc gfx_add_map_photo_func, GfxTakeMapPhotosFunc gfx_take_map_photos_func, GfxMapPhotoRegisterFunc gfx_map_photo_register, GfxMapPhotoUnregisterFunc gfx_map_photo_unregister, GfxUpdateMapPhotoFunc gfx_update_map_photo, GfxDownRezMapPhotoFunc gfx_downrez_map_photo)
{
	wl_state.gfx_add_map_photo_func = gfx_add_map_photo_func;
	wl_state.gfx_take_map_photos_func = gfx_take_map_photos_func;
	wl_state.gfx_map_photo_register = gfx_map_photo_register;
	wl_state.gfx_map_photo_unregister = gfx_map_photo_unregister;
	wl_state.gfx_update_map_photo = gfx_update_map_photo;
	wl_state.gfx_downrez_map_photo = gfx_downrez_map_photo;
}

void wlSetGfxBodysockTextureFuncs( GfxBodysockTextureCreateCallback gfx_bodysock_texture_create_callback, GfxBodysockTextureReleaseCallback gfx_bodysock_texture_release_callback )
{
	wl_state.gfx_bodysock_texture_create_callback = gfx_bodysock_texture_create_callback;
	wl_state.gfx_bodysock_texture_release_callback = gfx_bodysock_texture_release_callback;
}


void worldLibSetCheckVisibiltyFunc(CheckSkeletonVisibiltyFunc check_skeleton_visibility_func, CheckParticleVisibiltyFunc check_particle_visibility_func)
{
	wl_state.check_skeleton_visibility_func = check_skeleton_visibility_func;
	wl_state.check_particle_visibility_func = check_particle_visibility_func;
}

void worldLibSetForceCostumeReloadFunc(ForceCostumeReloadFunc force_costume_reload_func)
{
	wl_state.force_costume_reload_func = force_costume_reload_func;
}

void wlSetEntityNumFuncs(GetNumEntitiesFunc num_ent, GetNumEntitiesFunc num_coe)
{
	wl_state.get_num_entities_func = num_ent;
	wl_state.get_num_client_only_entities_func = num_coe;
}

void worldLibSetGfxModelFuncs(GfxCheckModelLoadedCallback check_model_loaded_callback)
{
	wl_state.gfx_check_model_loaded_callback = check_model_loaded_callback;
}

void worldLibSetWorldGraphicsDataFunctions(AllocWorldGraphicsDataFunc world_alloc_func, FreeWorldGraphicsDataFunc world_free_func,
										   AllocWorldRegionGraphicsDataFunc region_alloc_func, FreeWorldRegionGraphicsDataFunc region_free_func, 
										   GfxTickSkyData tick_sky_data_func)
{
	wl_state.alloc_world_graphics_data = world_alloc_func;
	wl_state.free_world_graphics_data = world_free_func;
	wl_state.alloc_region_graphics_data = region_alloc_func;
	wl_state.free_region_graphics_data = region_free_func;
	wl_state.tick_sky_data_func = tick_sky_data_func;
}

SoundRadiusFunc wlSoundRadiusFunc;
SoundDirFunc wlSoundDirFunc;
void worldLibSetSoundFunctions(	CreateSoundFunc create_sound_func, 
								RemoveSoundFunc remove_sound_func, 
								SoundValidateFunc sound_validate_func,
								SoundRadiusFunc sound_radius_func,
								SoundDirFunc sound_dir_func,
								SoundVolumeUpdateFunc sound_volume_update_func,
								SoundVolumeDestroyFunc sound_volume_destroy_func,
								SoundConnUpdateFunc sound_conn_update_func,
								SoundConnDestroyFunc sound_conn_destroy_func,
								SoundEventExistsFunc sound_event_exists_func,
								SoundGetProjectFileByEventFunc sound_get_project_file_by_event_func)
{
	wl_state.create_sound_func = create_sound_func;
	wl_state.remove_sound_func = remove_sound_func;
	wl_state.sound_validate_func = sound_validate_func;
	wl_state.sound_radius_func = sound_radius_func;
	wlSoundRadiusFunc = sound_radius_func;
	wlSoundDirFunc = sound_dir_func;
	wl_state.sound_volume_update_func = sound_volume_update_func;
	wl_state.sound_volume_destroy_func = sound_volume_destroy_func;
	wl_state.sound_conn_update_func = sound_conn_update_func;
	wl_state.sound_conn_destroy_func = sound_conn_destroy_func;
	wl_state.sound_event_exists_func = sound_event_exists_func;
	wl_state.sound_get_project_file_by_event_func = sound_get_project_file_by_event_func;
}

void worldLibSetAISpawnerFunctions(MastermindDefUpdatedCallback defupdated_callback,
								   MastermindSpawnerUpdateFunc room_create_func, 
								   MastermindSpawnerUpdateFunc room_destroy_func )
{
	wl_state.mastermind_def_updated_callback = defupdated_callback;
	wl_state.mastermind_room_update_func = room_create_func;
	wl_state.mastermind_room_destroy_func = room_destroy_func;
}

void worldLibSetPlayableFunctions(PlayableCreateFunc pcf, PlayableDestroyFunc pdf)
{
	wl_state.playable_create_func = pcf;
	wl_state.playable_destroy_func = pdf;
}

void worldLibSetCivGenFunctions(CivGenCreateFunc cgcf, CivGenDestroyFunc cgdf)
{
	wl_state.civgen_create_func = cgcf;
	wl_state.civgen_destroy_func = cgdf;
}

void worldLibSetAIGetStaticCheckExprContextFunc(AIGetStaticCheckExprContextFunc func)
{
	wl_state.ai_static_check_expr_context_func = func;
}

void worldLibSetCivilianVolumeFunctions(CivilianVolumeCreateFunc createFunc, CivilianVolumeDestroyFunc destroyFunc)
{
	wl_state.civvolume_create_func = createFunc;
	wl_state.civvolume_destroy_func = destroyFunc;
}

void worldLibSetIsConsumableFunc(InteractableTestFunc cb)
{
	wl_state.is_consumable_func = cb;
}

void worldLibSetIsTraversableFunc(InteractableTestFunc cb)
{
	wl_state.is_traversable_func = cb;
}

void worldLibSetInteractionNodeFreeFunc(InteractionNodeFunc cb)
{
	wl_state.interaction_node_free_func = cb;
}

void worldLibSetLinkInfoFromIDFunc(LinkInfoFromIDFunc cb)
{
	wl_state.link_info_from_id_func = cb;
}

void worldLibRegisterMapUnloadCallback(UnloadMapGameCallback unload_map_callback)
{
	eaPush((cEArrayHandle*)&wl_state.unload_map_callbacks, unload_map_callback);
}

void worldLibRegisterMapLoadCallback(LoadMapGameCallback load_map_callback)
{
	eaPush((cEArrayHandle*)&wl_state.load_map_callbacks, load_map_callback);
}

void worldLibRegisterMapReloadCallback(LoadMapGameCallback reload_map_callback)
{
	eaPush((cEArrayHandle*)&wl_state.reload_map_callbacks, reload_map_callback);
}

void worldLibSetEntRefreshCallback(EntRefreshCallback cb)
{
	wl_state.ent_refresh_callback = cb;
}

void worldLibSetGameCallbacks(UnloadMapGameCallback unload_map_callback, LoadMapGameCallback load_map_callback, LoadMapGameCallback reload_map_callback, LoadMapGameCallback edit_map_callback, LoadMapGameCallback save_map_callback)
{
	wl_state.unload_map_game_callback = unload_map_callback;
	wl_state.load_map_game_callback = load_map_callback;
	wl_state.reload_map_game_callback = reload_map_callback;
	wl_state.edit_map_game_callback = edit_map_callback;
	wl_state.save_map_game_callback = save_map_callback;
}

void worldLibSetLoadMapBeginEndCallback(LoadMapBeginEndCallback load_map_begin_end_callback)
{
	wl_state.load_map_begin_end_callback = load_map_begin_end_callback;
}


void worldLibSetWorldNodeFreeCallback(FreeWorldNodeCallback reload_interaction_callback)
{
	wl_state.free_world_node_callback = reload_interaction_callback;
}


void worldLibSetCivCallbacks(UnloadMapGameCallback unload_map_callback, LoadMapGameCallback load_map_callback)
{
	wl_state.load_map_civ_callback = load_map_callback;
	wl_state.unload_map_civ_callback = unload_map_callback;
}

void worldLibSetBcnCallbacks(UnloadMapGameCallback unload_map_callback, LoadMapGameCallback load_map_callback)
{
	wl_state.unload_map_bcn_callback = unload_map_callback;
	wl_state.load_map_bcn_callback = load_map_callback;
}

void worldLibSetEditorServerCallbacks(LoadMapGameCallback load_map_callback)
{
	wl_state.load_map_editorserver_callback = load_map_callback;
}

void worldlibSetCreateEncounterInfoCallback(CreateEncounterInfoCallback load_map_callback)
{
	wl_state.create_encounter_info_callback = load_map_callback;
}

void worldLibSetTerrainEditorCallbacks(LayerChangedModeCallback layer_mode_callback,
									   RenameTerrainSourceCallback rename_source_callback, 
									   SaveTerrainSourceCallback save_source_callback,
									   AddTerrainSourceCallback add_source_callback)
{
	wl_state.layer_mode_callback = layer_mode_callback;
	wl_state.rename_terrain_source_callback = rename_source_callback;
	wl_state.save_terrain_source_callback = save_source_callback;
	wl_state.add_terrain_source_callback = add_source_callback;
}

void worldLibSetGfxDynamicsCallbacks(GfxDynamicParticleMemUsageCallback particle_mem_usage_callback)
{
	wl_state.particle_mem_usage_callback = particle_mem_usage_callback;
}

void worldLibSetGfxSettingsCallbacks(GfxSettingDetailCallback gfx_setting_world_detail_callback,
		GfxSettingDetailCallback gfx_setting_character_detail_callback,
		GfxSettingCallback gfx_get_cluster_load_setting_callback,
		GfxGenericCallback gfx_materials_reload_all)
{
	wl_state.gfx_setting_world_detail_callback = gfx_setting_world_detail_callback;
	wl_state.gfx_setting_character_detail_callback = gfx_setting_character_detail_callback;
	wl_state.gfx_get_cluster_load_setting_callback = gfx_get_cluster_load_setting_callback;
	wl_state.gfx_material_reload_all = gfx_materials_reload_all;
}

void worldLibSetMaterialFunctions(TexFindAndFlagFunc tex_find_func, TexGetNameFunc tex_name_func,
	TexGetNameFunc tex_fullname_func, TexIsFunc tex_is_normalmap_func, TexIsFunc tex_is_dxt5nm_func,
	TexIsFunc tex_is_cubemap_func, TexIsFunc tex_is_volume_func, TexIsFunc tex_is_alpha_bordered_func,
	MaterialCheckOccluderFunc material_check_occluder_func, MaterialCheckSwapsFunc material_check_swaps_func,
	MaterialApplySwapsFunc material_apply_swaps_func, MaterialDrawFixupFunc materialdraw_fixup_func,
	MaterialValidateForFx material_validate_for_fx_func, MaterialInitFunc material_init_func)
{
	wl_state.tex_find_func = tex_find_func;
	wl_state.tex_name_func = tex_name_func;
	wl_state.tex_fullname_func = tex_fullname_func;
	wl_state.tex_is_normalmap_func = tex_is_normalmap_func;
	wl_state.tex_is_dxt5nm_func = tex_is_dxt5nm_func;
	wl_state.tex_is_cubemap_func = tex_is_cubemap_func;
	wl_state.tex_is_volume_func = tex_is_volume_func;
	wl_state.tex_is_alpha_bordered_func = tex_is_alpha_bordered_func;
	wl_state.material_check_occluder_func = material_check_occluder_func;
	wl_state.material_check_swaps_func = material_check_swaps_func;
	wl_state.material_apply_swaps_func = material_apply_swaps_func;
	wl_state.materialdraw_fixup_func = materialdraw_fixup_func;
	wl_state.material_validate_for_fx = material_validate_for_fx_func;
	wl_state.material_init_func = material_init_func;
}

void worldLibSetNotifySkyGroupFreedFunc(NotifySkyGroupFreedFunc notify_sky_group_freed_func)
{
	wl_state.notify_sky_group_freed_func = notify_sky_group_freed_func;
}

void worldLibSetCheckEntityExistsFunc(CheckEntityExistsFunc check_entity_exists_func)
{
	wl_state.check_entity_exists_func = check_entity_exists_func;
}

void worldLibSetCheckEntityHasExistedFunc(CheckEntityHasExistedFunc check_entity_exists_func)
{
	wl_state.check_entity_has_existed_func = check_entity_exists_func;
}

void worldLibSetEnemyFactionCheckFunc(CheckEnemyFactionFunc check_enemy_faction_func)
{
	wl_state.check_enemy_faction_func = check_enemy_faction_func;
}

void worldNotifyPlayerFactionChanged()
{
	if (wl_state.check_enemy_faction_func)
	{
		wl_state.player_faction_changed = true;
	}
}

// TomY ENCOUNTER_HACK
void worldLibSetEncounterHackSaveLayerFunc(EncounterHackSaveLayerFunc func)
{
	wl_state.encounter_hack_callback = func;
}

void worldLibSetSimplygonFunctions(
	GetSimplygonMaterialIdFromTable get_simplygon_material_id_from_table_func,
	GetMaterialClusterTexturesFromMaterial get_material_cluster_textures_from_material,
	CalculatVertexLightingForGMesh calculate_vertex_lighting_for_gmesh,
	GfxCheckClusterLoadedCallback gfx_check_cluster_loaded,
	GfxWCGFreeClusterTexSwaps gfx_cluster_close_tex_swaps,
	GfxGenericSetBoolCallback gfx_cluster_set_cluster_state,
	GfxGenericGetBoolCallback gfx_cluster_get_cluster_state) {

	wl_state.get_simplygon_material_id_from_table = get_simplygon_material_id_from_table_func;
	wl_state.get_material_cluster_textures_from_material = get_material_cluster_textures_from_material;
	wl_state.calculate_vertex_lighting_for_gmesh = calculate_vertex_lighting_for_gmesh;
	wl_state.gfx_check_cluster_loaded = gfx_check_cluster_loaded;
	wl_state.gfx_cluster_close_tex_swaps = gfx_cluster_close_tex_swaps;
	wl_state.gfx_cluster_set_cluster_state = gfx_cluster_set_cluster_state;
	wl_state.gfx_cluster_get_cluster_state = gfx_cluster_get_cluster_state;
}


static int wlStatusPrintfDefault(const char *fmt, ...)
{
	int result;
	va_list argptr;

	va_start(argptr, fmt);
	result = vprintf_timed(fmt, argptr);
	va_end(argptr);
	printf("\n");

	return result;
}

PrintfFunc wlStatusPrintf = wlStatusPrintfDefault;
void wlSetStatusPrintf(PrintfFunc printf_func)
{
	wlStatusPrintf = printf_func;
}


LoadUpdateFunc wlLoadUpdate;
void wlSetLoadUpdateFunc(LoadUpdateFunc callback)
{
	wlLoadUpdate = callback;
}

void wlSetDrawLine3D_2Func(WL_DrawLine3D_2Func callback)
{
	wl_state.drawLine3D_2_func = callback;
}

void wlSetDrawAxesFromTransformFunc(WL_DrawAxesFromTransform_Func callback)
{
	wl_state.drawAxesFromTransform_func = callback;
}

void wlDrawLine3D_2(const Vec3 p1, int argb1, const Vec3 p2, int argb2)
{
	if(wl_state.drawLine3D_2_func){
		wl_state.drawLine3D_2_func(p1, p2, argb1, argb2);
	}
}

void wlDrawLine3D_2_Mat(const Mat4 mat, const Vec3 p1, int argb1, const Vec3 p2, int argb2)
{
	if(wl_state.drawLine3D_2_func)
	{
		if(!mat)
		{
			wlDrawLine3D_2(p1, argb1, p2, argb2);
		}
		else
		{
			Vec3 p1_2;
			Vec3 p2_2;
			mulVecMat4(p1, mat, p1_2);
			mulVecMat4(p2, mat, p2_2);
			wl_state.drawLine3D_2_func(p1_2, p2_2, argb1, argb2);
		}
	}
}

void wlSetDrawBox3DFunc(WL_DrawBox3DFunc callback)
{
	wl_state.drawBox3D_func = callback;
}

void wlDrawBox3D(const Vec3 min, const Vec3 max, const Mat4 mat, int argb, F32 line_width)
{
	if(wl_state.drawBox3D_func)
	{
		wl_state.drawBox3D_func(min, max, mat, argb, line_width);
	}
}

void wlSetDrawModelFunc(WL_DrawModelFunc callback)
{
	wl_state.drawModel_func = callback;
}

void wlDrawModel(Model* model, const Mat4 mat)
{
	if(wl_state.drawModel_func)
	{
		wl_state.drawModel_func(model, mat);
	}
}

void wlSetDrawPointClientFunc(WL_AddPointClientFunc func)
{
	wl_state.drawPointClient_func = func;
}

void wlSetDrawLineClientFunc(WL_AddLineClientFunc func)
{
	wl_state.drawLineClient_func = func;
}

void wlSetDrawBoxClientFunc(WL_AddBoxClientFunc func)
{
	wl_state.drawBoxClient_func = func;
}

void wlSetDrawTriClientFunc(WL_AddTriClientFunc func)
{
	wl_state.drawTriClient_func = func;
}

void wlSetDrawQuadClientFunc(WL_AddQuadClientFunc func)
{
	wl_state.drawQuadClient_func = func;
}

void wlAddClientPoint(Entity *e, Vec3 pt, U32 color)
{
	if(wl_state.drawPointClient_func)
		wl_state.drawPointClient_func(e, pt, color);
}

void wlAddClientLine(Entity *e, Vec3 start, Vec3 end, U32 color)
{
	if(wl_state.drawLineClient_func)
		wl_state.drawLineClient_func(e, start, end, color);
}

void wlAddClientBox(Entity *e, const Vec3 local_min, const Vec3 local_max, const Mat4 mat, U32 color)
{
	if(wl_state.drawBoxClient_func)
		wl_state.drawBoxClient_func(e, local_min, local_max, mat, color);
}

void wlAddClientTri(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, U32 color, bool filled)
{
	if(wl_state.drawTriClient_func)
		wl_state.drawTriClient_func(e, p1, p2, p3, color, filled);
}

void wlAddClientQuad(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, const Vec3 p4, U32 color)
{
	if(wl_state.drawQuadClient_func)
		wl_state.drawQuadClient_func(e, p1, p2, p3, p4, color);
}

static F32 worldGetDistanceYToCollision(WorldColl* wc, const Vec3 pos, F32 dy, S32* hitSomething)
{
	Vec3 pos2;
	WorldCollCollideResults results;
	F32 retVal = 0;

	copyVec3(pos, pos2);
	pos2[1] -= dy;

	if(wcRayCollide(wc, pos, pos2, WC_FILTER_BIT_MOVEMENT, &results)){
		retVal = fabs(results.posWorldImpact[1] - pos[1]);
	}

	if(hitSomething)
		*hitSomething = results.hitSomething;
	if(!results.hitSomething)
		retVal = fabs(dy);

	return retVal;
}

F32 worldGetPointFloorDistance(WorldColl* wc, Vec3 pos, F32 height, F32 maxDist, SA_PARAM_OP_VALID S32* floorFound)
{
	Vec3 tempPos;
	F32 dist;

	PERFINFO_AUTO_START_FUNC();
	copyVec3(pos, tempPos);
	vecY(tempPos) += height;

	// maxDist should be positive here
	dist = worldGetDistanceYToCollision(wc, tempPos, maxDist, floorFound);

	PERFINFO_AUTO_STOP();

	// Returns a negative number if the floor is below pos, a positive number if it's above
	return height - dist;
}

// Helper function; take a position and move it up or down so that it rests on the closest surface
F32 worldSnapPosToGround(int iPartitionIdx, Vec3 posInOut, F32 height, F32 depth, S32 *floorFound)
{
	F32 floorDistance;
	F32 rayLength = height - depth;

	assert(height >= 0 && depth <= 0);

	PERFINFO_AUTO_START("worldSnapPosToGround", 1);
		// Find a floor looking down from height feet above the current position 
		floorDistance = worldGetPointFloorDistance(worldGetActiveColl(iPartitionIdx), posInOut, height, rayLength, floorFound);

		if(fabs(floorDistance) < 0.05 || floorDistance < depth + 0.05)
			floorDistance = 0;

		vecY(posInOut) += floorDistance;
	PERFINFO_AUTO_STOP();

	return floorDistance;
}


void worldLibSetCameraFrustum(const Frustum *frustum) // Camera frustum used only for "squishy" things (e.g. FX)
{
	globMovementLog("[gfx] Calling %s.", __FUNCTION__);
	frustumCopy(&wl_state.last_camera_frustum, frustum);
	
	// Update the FX system's camera node.
	dtUpdateCameraInfo();
}

void worldLibSetLodScale(F32 lod_scale, F32 terrain_lod_scale, F32 world_cell_load_scale, bool keep_cell_data_loaded)
{
	if (lod_scale != wl_state.lod_scale)
	{
		wl_state.lod_scale = lod_scale;
		worldCellResetCachedEntries();
	}

	wl_state.terrain_lod_scale = terrain_lod_scale;

	if (keep_cell_data_loaded)
		world_cell_load_scale = 1;
	wl_state.world_cell_load_scale = world_cell_load_scale;

	if (keep_cell_data_loaded != wl_state.keep_cell_data_loaded)
	{
		wl_state.keep_cell_data_loaded = keep_cell_data_loaded;
		if (!keep_cell_data_loaded)
			worldCellUnloadUnusedCellData();
	}
}

void worldLibSetLODSettings(const WorldRegionLODSettings* pLODSettings, F32 fDetail)
{
	U32 i;
	WorldRegionLODSettings* pNonConstLODSettingsHack = (WorldRegionLODSettings*)pLODSettings;
	wl_state.current_lod_settings = pLODSettings;
	assert(wl_state.current_lod_settings);
	for (i=0; i<wl_state.current_lod_settings->uiNumLODLevels; ++i)
	{
		// Nonconst hack, but better than the alternatives at the moment
		pNonConstLODSettingsHack->MaxLODSkelSlots[i] = ceilf(pNonConstLODSettingsHack->DefaultMaxLODSkelSlots[i] * fDetail);
	}
}

const WorldRegionLODSettings* worldLibGetLODSettings(void)
{
	return wl_state.current_lod_settings;
}


U32 worldGetLoadedModelCount(U32 *counts, WLUsageFlags flags_for_total)
{
	int i;
	U32 total = 0;
	for (i = 0; i < WL_FOR_MAXCOUNT; ++i)
	{
		counts[i] = wl_state.debug.loaded_model_count[i];
		if ((1 << i) & flags_for_total)
			total += counts[i];
	}
	return total;
}

U32 worldGetLoadedModelSize(U32 *sizes, WLUsageFlags flags_for_total)
{
	int i;
	U32 total = 0;
	for (i = 0; i < WL_FOR_MAXCOUNT; ++i)
	{
		sizes[i] = wl_state.debug.loaded_model_size[i];
		if ((1 << i) & flags_for_total)
			total += sizes[i];
	}
	return total;
}

void worldSetSelectedWireframe(int selected_wireframe, int selected_tint)
{
	int wiremode = ( selected_tint ? 2 : 0 ) | (selected_wireframe ? 1 : 0);
	wl_state.selected_wireframe = wiremode;
}

void worldGetSelectedTintColor(Vec4 selected_tint_color)
{
	copyVec4(wl_state.selectedTintColor, selected_tint_color);
}

void worldSetSelectedTintColor(Vec4 selected_tint_color)
{
	copyVec4(selected_tint_color, wl_state.selectedTintColor);
}

int worldGetSelectedWireframe(void)
{
	return wl_state.selected_wireframe;
}

WorldDrawableEntry *worldGetDebugDrawable(void)
{
	return wl_state.debug.drawable_entry;
}

void worldSetDebugDrawable(WorldDrawableEntry *entry)
{
	wl_state.debug.drawable_entry = entry;
}

WorldCell *worldGetDebugCell(void)
{
	return wl_state.debug.world_cell;
}

void worldSetDebugCell(WorldCell *cell)
{
	wl_state.debug.world_cell = cell;
}

int worldGetAnimationUpdateCount(void)
{
	int ret = wl_state.debug.world_animation_update_count;
	wl_state.debug.world_animation_update_count = 0;
	return ret;
}

U32 wlGetFrameCount(void)
{
	return wl_state.frame_count;
}

/// Define here any dictionary that must exist.
///
/// This should contain a list of all dictionaries in
/// wlGroupPropertyStructs.h.
AUTO_RUN_LATE;
void worldLibDefineMinimalDictionaries(void)
{
	char* requiredDicts[] = {
		"AIAnimList",
		"Contact",
		"CritterDef",
		"CritterOverrideDef",
		"Cutscene",
		"DoorTransitionSequenceDef",
		"EncounterDef",
		"EncounterTemplate",
		"FSM",
		"InteractionDef",
		"ItemDef",
		"MissionDef",
		"PowerDef",
		"QueueDef",
		"RewardTable",
	};

	int it;
	for( it = 0; it != ARRAY_SIZE( requiredDicts ); ++it ) {
		if( !RefDictionaryFromNameOrHandle( requiredDicts[ it ])) {
			RefSystem_RegisterSelfDefiningDictionary( requiredDicts[ it ], false, NULL, true, false, NULL );
		}
	}
}

static Vec4 s_OcclusionVolumeColor = { 1.0f, 0.0f, 0.5f, 0.5f, };
static Vec4 s_AudioVolumeColor = { 0.0f, 1.0f, 0.0f, 0.5f };
static Vec4 s_SkyFadeVolumeColor = { 0.0f, 0.5f, 1.0f, 0.5f };
static Vec4 s_NeighborhoodVolumeColor = { 0.0f, 0.0f, 1.0f, 0.5f };
static Vec4 s_InteractionVolumeColor = { 0.5f, 0.0f, 1.0f, 0.5f };
static Vec4 s_LandmarkVolumeColor = { 1.0f, 1.0f, 0.0f, 0.5f };
static Vec4 s_PowerVolumeColor = { 1.0f, 0.0f, 0.0f, 0.5f };
static Vec4 s_WarpVolumeColor = { 1.0f, 0.0f, 1.0f, 0.5f };
static Vec4 s_GenesisVolumeColor = { 0.0f, 1.0f, 0.5f, 0.5f };
static Vec4 s_TerrainFilterVolumeColor = { 0.5f, 0.5f, 0.5f, 0.5f };
static Vec4 s_TerrainExclusionVolumeColor = { 0.5f, 0.0f, 0.25f, 0.5f };

void worldGetOcclusionVolumeColor(Vec4 color)
{
	copyVec4(s_OcclusionVolumeColor, color);
}

void worldSetOcclusionVolumeColor(Vec4 color)
{
	copyVec4(color, s_OcclusionVolumeColor);
}

void worldGetAudioVolumeColor(Vec4 color)
{
	copyVec4(s_AudioVolumeColor, color);
}

void worldSetAudioVolumeColor(Vec4 color)
{
	copyVec4(color, s_AudioVolumeColor);
}

void worldGetSkyFadeVolumeColor(Vec4 color)
{
	copyVec4(s_SkyFadeVolumeColor, color);
}

void worldSetSkyFadeVolumeColor(Vec4 color)
{
	copyVec4(color, s_SkyFadeVolumeColor);
}

void worldGetNeighborhoodVolumeColor(Vec4 color)
{
	copyVec4(s_NeighborhoodVolumeColor, color);
}

void worldSetNeighborhoodVolumeColor(Vec4 color)
{
	copyVec4(color, s_NeighborhoodVolumeColor);
}

void worldGetInteractionVolumeColor(Vec4 color)
{
	copyVec4(s_InteractionVolumeColor, color);
}

void worldSetInteractionVolumeColor(Vec4 color)
{
	copyVec4(color, s_InteractionVolumeColor);
}

void worldGetLandmarkVolumeColor(Vec4 color)
{
	copyVec4(s_LandmarkVolumeColor, color);
}

void worldSetLandmarkVolumeColor(Vec4 color)
{
	copyVec4(color, s_LandmarkVolumeColor);
}

void worldGetPowerVolumeColor(Vec4 color)
{
	copyVec4(s_PowerVolumeColor, color);
}

void worldSetPowerVolumeColor(Vec4 color)
{
	copyVec4(color, s_PowerVolumeColor);
}

void worldGetWarpVolumeColor(Vec4 color)
{
	copyVec4(s_WarpVolumeColor, color);
}

void worldSetWarpVolumeColor(Vec4 color)
{
	copyVec4(color, s_WarpVolumeColor);
}

void worldGetGenesisVolumeColor(Vec4 color)
{
	copyVec4(s_GenesisVolumeColor, color);
}

void worldSetGenesisVolumeColor(Vec4 color)
{
	copyVec4(color, s_GenesisVolumeColor);
}

void worldGetTerrainFilterVolumeColor(Vec4 color)
{
	copyVec4(s_TerrainFilterVolumeColor, color);
}

void worldSetTerrainFilterVolumeColor(Vec4 color)
{
	copyVec4(color, s_TerrainFilterVolumeColor);
}

void worldGetTerrainExclusionVolumeColor(Vec4 color)
{
	copyVec4(s_TerrainExclusionVolumeColor, color);
}

void worldSetTerrainExclusionVolumeColor(Vec4 color)
{
	copyVec4(color, s_TerrainExclusionVolumeColor);
}

AUTO_RUN;
void WorldLibErrorStartup(void)
{
	ErrorAddInterchangeableFilenameExtensionPair(ROOTMODS_EXTENSION, MODELNAMES_EXTENSION);
}

#include "autogen/partition_enums_h_ast.c"
#include "autogen/worldlib_h_ast.c"
#include "autogen/worldlibenums_h_ast.c"
#include "autogen/worldlibstructs_h_ast.c"
#include "WorldLib_autogen_QueuedFuncs.c"
