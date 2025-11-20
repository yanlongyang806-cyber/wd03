#ifndef WORLDLIB_H
#define WORLDLIB_H
GCC_SYSTEM

#include "stdtypes.h"
#include "WorldLibEnums.h"
#include "wlLight.h"

C_DECLARATIONS_BEGIN
typedef struct AtlasTex AtlasTex;
typedef struct BasicTexture BasicTexture;
typedef struct CivilianGenerator CivilianGenerator;
typedef struct DynFxRegion DynFxRegion;
typedef struct DynNode DynNode;
typedef struct DynParticle DynParticle;
typedef struct DynSkeleton DynSkeleton;
typedef struct DynTransform DynTransform;
typedef struct Entity Entity;
typedef struct FrameLockedTimer FrameLockedTimer;
typedef struct Frustum Frustum;
typedef struct GenesisEpisode GenesisEpisode;
typedef struct GfxSplat GfxSplat;
typedef struct HeightMap HeightMap;
typedef struct HeightMapTracker HeightMapTracker;
typedef struct Material Material;
typedef struct MaterialDraw MaterialDraw;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct MaterialNamedDynamicConstant MaterialNamedDynamicConstant;
typedef struct MaterialNamedTexture MaterialNamedTexture;
typedef struct Model Model;
typedef struct ModelLOD ModelLOD;
typedef struct RdrAmbientLight RdrAmbientLight;
typedef struct Room Room;
typedef struct RoomInstanceMapSnapAction RoomInstanceMapSnapAction;
typedef struct RoomPortal RoomPortal;
typedef struct SkyInfoGroup SkyInfoGroup;
typedef struct SoundSpace SoundSpace;
typedef struct SoundSpaceConnector SoundSpaceConnector;
typedef struct TextParserBinaryBlock TextParserBinaryBlock;
typedef struct WLCostume WLCostume;
typedef struct WorldAnimationEntry WorldAnimationEntry;
typedef struct WorldAtmosphereProperties WorldAtmosphereProperties;
typedef struct WorldCivilianGenerator WorldCivilianGenerator;
typedef struct WorldColl WorldColl;
typedef struct WorldCollObject WorldCollObject;
typedef struct WorldCollObjectMsg WorldCollObjectMsg;
typedef struct WorldCollCollideResults WorldCollCollideResults;
typedef struct WorldCollRayCollideMultiResult WorldCollRayCollideMultiResult;
typedef struct WorldDrawableEntry WorldDrawableEntry;
typedef struct WorldGraphicsData WorldGraphicsData;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldRegion WorldRegion;
typedef struct WorldRegionGraphicsData WorldRegionGraphicsData;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct ZoneMap ZoneMap;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct WorldCell WorldCell;
typedef struct WorldRegionLODSettings WorldRegionLODSettings;
typedef struct GfxStaticObjLightCache GfxStaticObjLightCache;
typedef struct GfxDynObjLightCache GfxDynObjLightCache;
typedef struct GfxLightCacheBase GfxLightCacheBase;
typedef struct NetLink NetLink;
typedef struct ResourceCache ResourceCache;
typedef struct WorldEncounterProperties WorldEncounterProperties;
typedef struct WorldActorProperties WorldActorProperties;
typedef struct WorldPatrolProperties WorldPatrolProperties;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct BinFileList BinFileList;
typedef struct GroupTrackerChildSelect GroupTrackerChildSelect;
typedef struct MapSnapRoomPartitionData MapSnapRoomPartitionData;
typedef U32 EntityRef;
typedef struct SimplygonMaterialTable SimplygonMaterialTable;
typedef struct TextureSwap TextureSwap;
typedef struct HogFile HogFile;
typedef struct WorldClusterState WorldClusterState;
typedef struct GMesh GMesh;
typedef struct ModelClusterTextures ModelClusterTextures;
typedef struct StashTableImp StashTableImp;
typedef struct MaterialSwap	MaterialSwap;
typedef struct RemeshAssetSwap RemeshAssetSwap;
typedef struct TaskProfile TaskProfile;
typedef struct WorldClusterGeoStats WorldClusterGeoStats;
typedef struct WorldCellGraphicsData WorldCellGraphicsData;


#define ZENI_CURRENT_VERSION 7
#define ZENISNAP_CURRENT_VERSION 2

int wlDontLoadBeacons(void);
void wlSetLoadFlags(WorldLibLoadFlags load_flags);
WorldLibLoadFlags wlGetLoadFlags(void);
void wlSetIsServer(bool is_server);
void wlSetDeleteHoggs(bool delete_hoggs);
 
void checkForCoreData(void);

void worldLibStartup(void);
void worldLibShutdown(void);
void worldLibStartupPostGraphicsLib(void);
void worldLibOncePerFrame(F32 fFrameTime); // Pass in frame time, or should WorldLib calc the TIMESTEP

typedef F32 (*GfxSettingDetailCallback)(void);
typedef U32 (*GfxSettingCallback)(void);
typedef void (*GfxGenericCallback)(void);
typedef void (*GfxGenericSetBoolCallback)(bool bSetting);
typedef bool (*GfxGenericGetBoolCallback)(void);
void worldLibSetGfxSettingsCallbacks(GfxSettingDetailCallback gfx_setting_world_detail_callback, GfxSettingDetailCallback gfx_setting_character_detail_callback, GfxSettingCallback gfx_setting_ugly_scale_detail_callback, GfxGenericCallback gfx_materials_reload_all);

typedef GfxLight * (*UpdateLightFunc)(GfxLight *light, const LightData *data, F32 vis_dist, WorldAnimationEntry *animation_controller);
typedef void (*RemoveLightFunc)(GfxLight *light);
typedef RdrAmbientLight * (*UpdateAmbientLightFunc)(RdrAmbientLight * light, const Vec3 ambient, const Vec3 sky_light_color_front, const Vec3 sky_light_color_back, const Vec3 sky_light_color_side);
typedef void (*RemoveAmbientLightFunc)(RdrAmbientLight *ambient_light);
typedef void (*ComputeStaticLightingFunc)(WorldDrawableEntry *entry, WorldRegion *region, BinFileList *file_list);
typedef void (*ComputeTerrainLightingFunc)(ZoneMap *zmap, const char ***file_list, BinFileList *file_list2);
typedef void (*RoomLightingUpdateFunc)(Room* room);
void worldLibSetLightFunctions(UpdateLightFunc update_light_func, RemoveLightFunc remove_light_func, 
							   UpdateAmbientLightFunc create_ambient_light_func, RemoveAmbientLightFunc free_ambient_light_func, 
							   RoomLightingUpdateFunc room_lighting_update_func);

typedef GfxStaticObjLightCache * (*CreateStaticLightCacheFunc)(WorldDrawableEntry *entry, WorldRegion *region);
typedef void (*FreeStaticLightCacheFunc)(GfxStaticObjLightCache *light_cache);
typedef GfxDynObjLightCache * (*CreateDynLightCacheFunc)(WorldRegionGraphicsData *graphics_data, 
														 const Vec3 local_min, const Vec3 local_max, const Mat4 world_matrix, 
														 LightCacheType cache_type);
typedef void (*FreeDynLightCacheFunc)(GfxDynObjLightCache *light_cache);
typedef void (*ForceUpdateLightCachesFunc)(const Vec3 bounds_min, const Vec3 bounds_max, const Mat4 world_matrix, 
										   bool update_dynamic_caches, bool update_static_caches,
										   LightCacheInvalidateType invalidate_types, GfxLight* remove_light);
typedef void (*InvalidateLightCacheFunc)(GfxLightCacheBase *light_cache, LightCacheInvalidateType invalidate_types);
typedef void (*UpdateIndoorVolumeFunc)(const Vec3 bounds_min, const Vec3 bounds_max, const Mat4 world_matrix, bool streaming_mode);
void worldLibSetLightCacheFunctions(CreateStaticLightCacheFunc create_static_light_cache_func, FreeStaticLightCacheFunc free_static_light_cache_func, 
									CreateDynLightCacheFunc create_dyn_light_cache_func, FreeDynLightCacheFunc free_dyn_light_cache_func, 
									ForceUpdateLightCachesFunc force_update_light_caches_func, InvalidateLightCacheFunc invalidate_light_cache_func,
									ComputeStaticLightingFunc compute_static_lighting_func, ComputeTerrainLightingFunc compute_terrain_lighting_func,
									UpdateIndoorVolumeFunc update_indoor_volume_func);

typedef int (*GetSimplygonMaterialIdFromTable)(SimplygonMaterialTable *table, Material *material, TextureSwap **eaTextureSwaps, const char *tempDir, TaskProfile *texExport, 
	WorldClusterGeoStats *inputGeoStats);
typedef void (*GetMaterialClusterTexturesFromMaterial)(Material *material, RemeshAssetSwap **eaTextureSwaps, 
	RemeshAssetSwap **eaMaterialSwaps, const char *tempDir, 
	ModelClusterTextures ***mcTextures, StashTableImp *materialTextures, TaskProfile *texExport, 
	WorldClusterGeoStats *inputGeoStats);
typedef void (*CalculatVertexLightingForGMesh)(GMesh *mesh);
typedef bool (*GfxCheckClusterLoadedCallback)(WorldCell *cell, bool startLoad);
typedef void (*GfxWCGFreeClusterTexSwaps)(WorldCellGraphicsData *pWCGD);
void worldLibSetSimplygonFunctions(
	GetSimplygonMaterialIdFromTable get_simplygon_material_id_from_table_func,
	GetMaterialClusterTexturesFromMaterial get_material_cluster_textures_from_material,
	CalculatVertexLightingForGMesh calculate_vertex_lighting_for_gmesh,
	GfxCheckClusterLoadedCallback gfx_check_cluster_loaded,
	GfxWCGFreeClusterTexSwaps gfx_cluster_close_tex_swaps,
	GfxGenericSetBoolCallback gfx_cluster_set_cluster_state,
	GfxGenericGetBoolCallback gfx_cluster_get_cluster_state);

typedef void (*DynFxUpdateLightFunc)(DynParticle *pParticle, GfxLight **ppLight);
void dynFxSetUpdateLightFunc(DynFxUpdateLightFunc dfx_update_light_func);

typedef void (*DynFxScreenShakeFunc)( F32 time, F32 magnitude, F32 vertical, F32 pan, F32 speed);
void dynFxSetScreenShakeFunc(DynFxScreenShakeFunc dfx_screen_shake_func);

typedef void (*DynFxCameraMatrixOverrideFunc)(Mat4 xMatrix, bool bEnable, F32 fInfluence);
void dynFxSetCameraMatrixOverrideFunc(DynFxCameraMatrixOverrideFunc dfx_camera_matrix_override_func);

typedef void (*DynFxCameraFOVFunc)( F32 fov );
void dynFxSetCameraFOVFunc(DynFxCameraFOVFunc dfx_camera_fov_func);

typedef void (*DynFxSetCameraDelayFunc)( F32 speed, F32 distanceBasis );
void dynFxSetCameraDelayFunc(DynFxSetCameraDelayFunc dfx_camera_delay_func);

typedef void (*DynFxSetCameraLookAtFunc)(const Vec3 vPos, F32 fSpeed);
void dynFxSetCameraLookAtFunc(DynFxSetCameraLookAtFunc dfx_camera_lookat_func);

typedef void (*DynFxSetAlienColor)(Color c);
void dynFxSetSetAlienColorFunc(DynFxSetAlienColor dfx_set_alien_color);

typedef void (*DynFxSkyVolumePushFunc)(const char*** sky_name_earray_ptr, WorldRegion *region, Vec3 start, Vec3 end, F32 radius, F32 weight, bool linear_falloff);
typedef void (*DynFxSkyVolumeOncePerFrameFunc)(void);
void dynFxSetSkyVolumeFunctions(DynFxSkyVolumePushFunc dfx_sky_volume_push_func, DynFxSkyVolumeOncePerFrameFunc dfx_sky_volume_once_per_frame_func );

typedef void (*DynFxWaterAgitateFunc)( F32 magnitude );
void dynFxSetWaterAgitateFunc(DynFxWaterAgitateFunc dfx_water_agitate_func);

typedef void (*DynFxHitReactImpactFunc)(U32 uiHitReactID, Vec3 vPos, Vec3 vVel);
typedef void (*DynAnimHitReactImpactFunc)(EntityRef uiEntityRef, U32 uid, Vec3 vPos, Vec3 vVel);
void dynSetHitReactImpactFuncs(DynFxHitReactImpactFunc dfx_hit_react_impact_func, DynAnimHitReactImpactFunc danim_hit_react_impact_func);
	

typedef void (*GfxSplatDestroyCallback)(GfxSplat* pSplat);
void wlSetGfxSplatDestroyCallback(GfxSplatDestroyCallback gfx_splat_destroy_callback);

typedef void (*GfxAddMapPhotoFunc)(const char *image_prefix, MapSnapRoomPartitionData * pPartitionData, Vec3 room_min, Vec3 room_mid, Vec3 room_max, RoomInstanceMapSnapAction **action_list, WorldRegion *region, const char *debug_filename, const char *debug_def_name, bool override_image, const char *override_name);
typedef bool (*GfxTakeMapPhotosFunc)(const char *path, char ***output_list, bool debug_run);
typedef AtlasTex* (*GfxMapPhotoRegisterFunc)(const char *name);
typedef void (*GfxMapPhotoUnregisterFunc)(AtlasTex *tex);
typedef void (*GfxUpdateMapPhotoFunc)(U8 *new_data, U8 *old_data, int data_size);
typedef void (*GfxDownRezMapPhotoFunc)(U8 *data, int *data_size);
void wlSetGfxTakeMapPhotosFuncs(GfxAddMapPhotoFunc gfx_add_map_photo_func, GfxTakeMapPhotosFunc gfx_take_map_photos_func, GfxMapPhotoRegisterFunc gfx_map_photo_register, GfxMapPhotoUnregisterFunc gfx_map_photo_unregister, GfxUpdateMapPhotoFunc gfx_update_map_photo, GfxDownRezMapPhotoFunc gfx_downrez_map_photo);

typedef BasicTexture* (*GfxBodysockTextureCreateCallback)(WLCostume* pCostume, S32* piSectionIndex, Vec4 vTexXfrm);
typedef bool (*GfxBodysockTextureReleaseCallback)(BasicTexture* pTexture, S32 iSectionIndex);
void wlSetGfxBodysockTextureFuncs( GfxBodysockTextureCreateCallback gfx_bodysock_texture_create_callback, GfxBodysockTextureReleaseCallback gfx_bodysock_texture_release_callback );


typedef bool (*CheckSkeletonVisibiltyFunc)(DynSkeleton* skeleton);
typedef bool (*CheckParticleVisibiltyFunc)(DynParticle* particle, DynFxRegion* pFxRegion);
void worldLibSetCheckVisibiltyFunc(CheckSkeletonVisibiltyFunc check_skeleton_visibility_func, CheckParticleVisibiltyFunc check_particle_visibility_func);

typedef void (*ForceCostumeReloadFunc)(void);
void worldLibSetForceCostumeReloadFunc(ForceCostumeReloadFunc force_costume_reload_func);

typedef S32 (*GetNumEntitiesFunc)(void);
void wlSetEntityNumFuncs(GetNumEntitiesFunc num_ent, GetNumEntitiesFunc num_coe);

typedef bool (*GfxCheckModelLoadedCallback)(ModelLOD *model);
void worldLibSetGfxModelFuncs(GfxCheckModelLoadedCallback check_model_loaded_callback);

typedef bool (*GfxMoveRemeshImagesToHogg)(HogFile *hog_file, const char *image_name, const char* image_filename);
typedef bool (*GfxMaterialHasTransparency)(Material *material);
typedef int (*GfxMaterialGetTextures)(Material* material, const BasicTexture ***textureHolder);
typedef bool (*GfxTextureSaveAsPNG)(const char *texName, const char *fname, bool invertVerticalAxis, SA_PRE_NN_NN_VALID TaskProfile *saveProfile);

typedef F32 (*SoundRadiusFunc)(const char *event_name);
typedef void (*SoundDirFunc)(SoundSpaceConnector *conn, Vec3 dirOut);
typedef SoundSpace *(*CreateSoundFunc)(const char *event_name, const char *excluder_str, const char *dsp_str, 
									   const char *editor_group_str, const char *sound_group_str, 
									   const char *sound_group_ord, const Mat4 world_mat);
typedef void (*RemoveSoundFunc)(SoundSpace *sphere);
typedef void (*SoundValidateFunc)(const char *event_name, const char* tracker_handle, const char *layer_file);
typedef SoundSpace *(*SoundVolumeUpdateFunc)(Room *room);
typedef void (*SoundVolumeDestroyFunc)(SoundSpace *space);
typedef SoundSpaceConnector *(*SoundConnUpdateFunc)(RoomPortal *portal);
typedef void (*SoundConnDestroyFunc)(RoomPortal *portal);
typedef U32 (*SoundEventExistsFunc)(const char* event_name);
typedef bool (*SoundGetProjectFileByEventFunc)(const char *pchEventPath, char **ppchFilePath);
void worldLibSetSoundFunctions(CreateSoundFunc create_sound_func, 
							   RemoveSoundFunc remove_sound_func, 
							   SoundValidateFunc sound_validate_func,
							   SoundRadiusFunc sound_radius_func,
							   SoundDirFunc sound_dir_func,
							   SoundVolumeUpdateFunc sound_volume_create_func,
							   SoundVolumeDestroyFunc sound_volume_destroy_func,
							   SoundConnUpdateFunc sound_conn_create_func,
							   SoundConnDestroyFunc sound_conn_destroy_func,
							   SoundEventExistsFunc sound_event_exists_func,
							   SoundGetProjectFileByEventFunc sound_get_project_file_by_event_func);
extern SoundRadiusFunc wlSoundRadiusFunc;
extern SoundDirFunc wlSoundDirFunc;

typedef void (*MastermindDefUpdatedCallback)(const char *pszDef);
typedef void (*MastermindSpawnerUpdateFunc)(RoomPortal* room);
void worldLibSetAISpawnerFunctions(MastermindDefUpdatedCallback defupdated_callback,
								   MastermindSpawnerUpdateFunc room_create_func,
								   MastermindSpawnerUpdateFunc room_destroy_func);


typedef void (*PlayableCreateFunc)(WorldVolumeEntry *entry);
typedef void (*PlayableDestroyFunc)(WorldVolumeEntry *entry);
void worldLibSetPlayableFunctions(PlayableCreateFunc pcf, PlayableDestroyFunc pdf);

typedef CivilianGenerator* (*CivGenCreateFunc)(Vec3 world_mat);
typedef void (*CivGenDestroyFunc)(CivilianGenerator *civgen);
void worldLibSetCivGenFunctions(CivGenCreateFunc cgcf, CivGenDestroyFunc cgdf);
CivilianGenerator* worldGetCivilianGenerator(WorldCivilianGenerator *wlcivgen);
void worldSetCivilianGenerator(WorldCivilianGenerator *wlcivgen, CivilianGenerator *civgen);

typedef void (*CivilianVolumeCreateFunc)(WorldVolumeEntry *entry);
typedef void (*CivilianVolumeDestroyFunc)(WorldVolumeEntry *entry);
void worldLibSetCivilianVolumeFunctions(CivilianVolumeCreateFunc createFunc, CivilianVolumeDestroyFunc destroyFunc);

typedef bool (*InteractableTestFunc)(WorldInteractionEntry *clickableInteractionEntry);
void worldLibSetIsConsumableFunc(InteractableTestFunc cb);
void worldLibSetIsTraversableFunc(InteractableTestFunc cb);

typedef void (*InteractionNodeFunc)(WorldInteractionNode *node);
void worldLibSetInteractionNodeFreeFunc(InteractionNodeFunc cb);

typedef void (*LinkInfoFromIDFunc)(U32 linkID, NetLink** out_ppNetLink, ResourceCache** out_ppCache);
void worldLibSetLinkInfoFromIDFunc(LinkInfoFromIDFunc cb);

typedef void (*UnloadMapGameCallback)(void);
typedef void (*LoadMapBeginEndCallback)(bool beginning);
typedef void (*LoadMapGameCallback)(ZoneMap *zmap);
typedef void (*EntRefreshCallback)(void);
typedef void (*FreeWorldNodeCallback)(void);
typedef void (*CreateEncounterInfoCallback)(ZoneMap *zmap, const char *filename);
void worldLibRegisterMapUnloadCallback(UnloadMapGameCallback unload_map_callback);
void worldLibRegisterMapLoadCallback(LoadMapGameCallback load_map_callback);
void worldLibRegisterMapReloadCallback(LoadMapGameCallback reload_map_callback);
void worldLibSetEntRefreshCallback(EntRefreshCallback cb);
void worldLibSetGameCallbacks(UnloadMapGameCallback unload_map_callback, LoadMapGameCallback load_map_callback, LoadMapGameCallback reload_map_callback, LoadMapGameCallback edit_map_callback, LoadMapGameCallback save_map_callback);
void worldLibSetWorldNodeFreeCallback(FreeWorldNodeCallback reload_interaction_callback);
void worldLibSetBcnCallbacks(UnloadMapGameCallback unload_map_callback, LoadMapGameCallback load_map_callback);
void worldLibSetCivCallbacks(UnloadMapGameCallback unload_map_callback, LoadMapGameCallback load_map_callback);
void worldLibSetLoadMapBeginEndCallback(LoadMapBeginEndCallback load_map_begin_end_callback);
void worldLibSetEditorServerCallbacks(LoadMapGameCallback load_map_callback);
void worldlibSetCreateEncounterInfoCallback(CreateEncounterInfoCallback load_map_callback);

typedef void (*LayerChangedModeCallback)(ZoneMapLayer *layer, ZoneMapLayerMode mode, bool asynchronous);
typedef void (*RenameTerrainSourceCallback)(ZoneMapLayer *layer);
typedef bool (*SaveTerrainSourceCallback)(ZoneMapLayer *layer, bool force, bool asynchronous);
typedef bool (*AddTerrainSourceCallback)(ZoneMapLayer *layer, IVec2 min, IVec2 max);
void worldLibSetTerrainEditorCallbacks(LayerChangedModeCallback layer_mode_callback,
									   RenameTerrainSourceCallback rename_source_callback, 
									   SaveTerrainSourceCallback save_source_callback,
									   AddTerrainSourceCallback add_source_callback);

typedef WorldGraphicsData* (*AllocWorldGraphicsDataFunc)(void);
typedef void (*FreeWorldGraphicsDataFunc)(WorldGraphicsData *data);
typedef WorldRegionGraphicsData* (*AllocWorldRegionGraphicsDataFunc)(WorldRegion *region, const Vec3 min, const Vec3 max);
typedef void (*FreeWorldRegionGraphicsDataFunc)(WorldRegion *region, bool remove_sky_group);
typedef void (*GfxTickSkyData)(void);
void worldLibSetWorldGraphicsDataFunctions(AllocWorldGraphicsDataFunc world_alloc_func, FreeWorldGraphicsDataFunc world_free_func,
										   AllocWorldRegionGraphicsDataFunc region_alloc_func, FreeWorldRegionGraphicsDataFunc region_free_func, 
										   GfxTickSkyData tick_sky_data_func);


typedef int (*GfxDynamicParticleMemUsageCallback)(DynParticle* pParticle);
void worldLibSetGfxDynamicsCallbacks(GfxDynamicParticleMemUsageCallback particle_mem_usage_callback);

typedef BasicTexture * (*TexFindAndFlagFunc)(const char *name, int isRequired, WLUsageFlags use_category);
typedef const char * (*TexGetNameFunc)(const BasicTexture *texture);
typedef bool (*TexIsFunc)(const BasicTexture *texture);
typedef bool (*MaterialCheckOccluderFunc)(Material *material);
typedef bool (*MaterialCheckSwapsFunc)(Material *material, const char **eaTextureSwaps, const MaterialNamedConstant **eaNamedConstants, const MaterialNamedTexture **eaNamedTextures, const MaterialNamedDynamicConstant **eaNamedDynamicConstants, int *texturesNeeded, int *constantsNeeded, int *constantMappingsNeeded, Vec4 instance_param);
typedef void (*MaterialApplySwapsFunc)(MaterialDraw *draw_material, Material *material, const char **eaTextureSwaps, const MaterialNamedConstant **eaNamedConstants, const MaterialNamedTexture **eaNamedTextures, const MaterialNamedDynamicConstant **eaNamedDynamicConstants, WLUsageFlags use_category);
typedef void (*MaterialDrawFixupFunc)(MaterialDraw *draw_material);
typedef bool (*MaterialValidateForFx)(Material *material, const char **ppcTemplateName, bool isForSplat);
typedef void (*MaterialInitFunc)(Material *material, int bInitTextures);
void worldLibSetMaterialFunctions(TexFindAndFlagFunc tex_find_func, TexGetNameFunc tex_name_func,
	TexGetNameFunc tex_fullname_func, TexIsFunc tex_is_normalmap_func, TexIsFunc tex_is_dxt5nm_func,
	TexIsFunc tex_is_cubemap_func, TexIsFunc tex_is_volume_func, TexIsFunc tex_is_alpha_bordered_func,
	MaterialCheckOccluderFunc material_check_occluder_func, MaterialCheckSwapsFunc material_check_swaps_func,
	MaterialApplySwapsFunc material_apply_swaps_func, MaterialDrawFixupFunc materialdraw_fixup_func,
	MaterialValidateForFx material_validate_for_fx_func, MaterialInitFunc material_init_func);

typedef void (*NotifySkyGroupFreedFunc)(SkyInfoGroup* sky_group);
void worldLibSetNotifySkyGroupFreedFunc(NotifySkyGroupFreedFunc notify_sky_group_freed_func);

typedef void *(*CheckEntityExistsFunc)(EntityRef entity_ref);
void worldLibSetCheckEntityExistsFunc(CheckEntityExistsFunc check_entity_exists_func);

typedef bool(*CheckEntityHasExistedFunc)(EntityRef entity_ref);
void worldLibSetCheckEntityHasExistedFunc(CheckEntityHasExistedFunc check_entity_exists_func);

typedef bool(*CheckEnemyFactionFunc)(const char *pchFaction);
void worldLibSetEnemyFactionCheckFunc(CheckEnemyFactionFunc check_enemy_faction_func);
void worldNotifyPlayerFactionChanged();

typedef int (*PrintfFunc)(FORMAT_STR char const *fmt, ...);
void wlSetStatusPrintf(PrintfFunc printf_func);
extern PrintfFunc wlStatusPrintf;

void wlSetRdrMaterialHasTransparency(GfxMaterialHasTransparency rdr_material_has_transparency);
void wlSetMaterialGetTextures(GfxMaterialGetTextures gfx_material_get_textures);
void wlSetGfxTextureFuncs(GfxTextureSaveAsPNG gfx_texture_save_as_png);

void wlSetWorldCellGfxDataType(ParseTable *type_worldCellGfxData);

typedef void (*LoadUpdateFunc)(int num_bytes);
void wlSetLoadUpdateFunc(LoadUpdateFunc callback);
extern LoadUpdateFunc wlLoadUpdate;

typedef void (*WL_DrawLine3D_2Func)(const Vec3 p1, const Vec3 p2, int argb1, int argb2);
typedef void(*WL_DrawAxesFromTransform_Func)(const DynTransform* pxTransform, F32 fLength);
void wlSetDrawLine3D_2Func(WL_DrawLine3D_2Func callback);
void wlDrawLine3D_2(const Vec3 p1, int argb1, const Vec3 p2, int argb2);
void wlDrawLine3D_2_Mat(const Mat4 mat, const Vec3 p1, int argb1, const Vec3 p2, int argb2);
void wlSetDrawAxesFromTransformFunc(WL_DrawAxesFromTransform_Func callback);

typedef void (*WL_DrawBox3DFunc)(const Vec3 min, const Vec3 max, const Mat4 mat, int argb, F32 line_width);
void wlSetDrawBox3DFunc(WL_DrawBox3DFunc callback);
void wlDrawBox3D(const Vec3 min, const Vec3 max, const Mat4 mat, int argb, F32 line_width);

typedef void (*WL_DrawModelFunc)(Model* model, const Mat4 mat);
void wlSetDrawModelFunc(WL_DrawModelFunc callback);
void wlDrawModel(Model* model, const Mat4 mat);

typedef void (*WL_AddPointClientFunc)(Entity *e, const Vec3 pt, U32 color);
typedef void (*WL_AddLineClientFunc)(Entity *e, const Vec3 start, const Vec3 end, U32 color);
typedef void (*WL_AddBoxClientFunc)(Entity *e, const Vec3 local_min, const Vec3 local_max, const Mat4 world_mat, U32 color);
typedef void (*WL_AddTriClientFunc)(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, U32 color, bool filled);
typedef void (*WL_AddQuadClientFunc)(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, const Vec3 p4, U32 color);
void wlSetDrawPointClientFunc(WL_AddPointClientFunc func);
void wlSetDrawLineClientFunc(WL_AddLineClientFunc func);
void wlSetDrawBoxClientFunc(WL_AddBoxClientFunc func);
void wlSetDrawTriClientFunc(WL_AddTriClientFunc func);
void wlSetDrawQuadClientFunc(WL_AddQuadClientFunc func);
void wlAddClientPoint(Entity *e, Vec3 pt, U32 color);
void wlAddClientLine(Entity *e, Vec3 start, Vec3 end, U32 color);
void wlAddClientBox(Entity *e, const Vec3 local_min, const Vec3 local_max, const Mat4 mat, U32 color);
void wlAddClientTri(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, U32 color, bool filled);
void wlAddClientQuad(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, const Vec3 p4, U32 color);

// Collision.

S32 worldCollideRay(int iPartitionIdx,
					SA_PRE_NN_RELEMS(3) const Vec3 source,
					SA_PRE_NN_RELEMS(3) const Vec3 target,
					U32 filterBits,
					SA_PRE_OP_FREE SA_POST_OP_VALID WorldCollCollideResults* results);

typedef S32 (*WorldCollCollideResultsCB)(	void* userPointer,
											const WorldCollCollideResults* results);

S32 worldCollideRayMultiResult(	int iPartitionIdx,
								SA_PRE_NN_RELEMS(3) const Vec3 source,
								SA_PRE_NN_RELEMS(3) const Vec3 target,
								U32 filterBits,
								WorldCollCollideResultsCB callback,
								void* userPointer);
					
S32 worldCollideCapsule(int iPartitionIdx,
						SA_PRE_NN_RELEMS(3) const Vec3 source,
						SA_PRE_NN_RELEMS(3) const Vec3 target,
						U32 filterBits,
						SA_PRE_OP_FREE SA_POST_OP_VALID WorldCollCollideResults* results);

S32 worldCollideCapsuleEx(int iPartitionIdx,
						  SA_PRE_NN_RELEMS(3) const Vec3 source,
						  SA_PRE_NN_RELEMS(3) const Vec3 target,
						  U32 filterBits,
						  F32 height,
						  F32 radius,
						  SA_PRE_OP_FREE SA_POST_OP_VALID WorldCollCollideResults* results);

F32 worldGetPointFloorDistance(WorldColl* wc, Vec3 pos, F32 height, F32 maxDist, SA_PARAM_OP_VALID S32* floorFound);
F32 worldSnapPosToGround(int iPartitionIdx, Vec3 posInOut, F32 height, F32 depth, SA_PARAM_OP_VALID S32* floorFound);

void worldLibSetCameraFrustum(const Frustum *frustum); // Camera frustum used only for "squishy" things (e.g. FX)
void worldLibSetLodScale(F32 lod_scale, F32 terrain_lod_scale, F32 world_cell_load_scale, bool keep_cell_data_loaded);
void worldLibSetLODSettings(const WorldRegionLODSettings* pLODSettings, F32 fDetail);
const WorldRegionLODSettings* worldLibGetLODSettings(void);

U32 worldGetLoadedModelCount(U32 *counts, WLUsageFlags flags_for_total);
U32 worldGetLoadedModelSize(U32 *sizes, WLUsageFlags flags_for_total);

void worldSetSelectedWireframe(int selected_wireframe, int selected_tint);
int worldGetSelectedWireframe(void);
void worldGetSelectedTintColor(Vec4 selected_tint_color);
void worldSetSelectedTintColor(Vec4 selected_tint_color);

WorldDrawableEntry *worldGetDebugDrawable(void);
void worldSetDebugDrawable(WorldDrawableEntry *entry);
WorldCell *worldGetDebugCell(void);
void worldSetDebugCell(WorldCell *cell);

int worldGetAnimationUpdateCount(void);

S32 worldCollisionEntryIsTerrainFromWCO(WorldCollObject* wco);
void entryCollObjectMsgHandler(const WorldCollObjectMsg* msg);
void heightMapCollObjectMsgHandler(const WorldCollObjectMsg *msg);
void volumeCollObjectMsgHandler(const WorldCollObjectMsg* msg);
void wcPrintActors(	int iPartitionIdx, S32 cellx, S32 cellz);

//moved from WorldCellStreamingPrivate.h because the log parser needs to be able to load the zone bounds files
AUTO_STRUCT;
typedef struct WorldRegionBounds
{
	const char *region_name;					AST( POOL_STRING )
	Vec3 world_min;
	Vec3 world_max;
} WorldRegionBounds;

AUTO_STRUCT;
typedef struct ZoneMapRegionBounds
{
	WorldRegionBounds **regions;
} ZoneMapRegionBounds;
extern ParseTable parse_ZoneMapRegionBounds[];
#define TYPE_parse_ZoneMapRegionBounds ZoneMapRegionBounds

U32 wlCalcNewFrameTimestamp(int *master);
extern U32 wl_frame_timestamp;

typedef struct GeoMemUsage
{
	U32 loadedSystem[WL_FOR_MAXCOUNT];
	U32 loadedSystemTotal;
	U32 countSystem[WL_FOR_MAXCOUNT];
	U32 countSystemTotal;

	U32 loadedVideo[WL_FOR_MAXCOUNT];
	U32 loadedVideoTotal;
	U32 recentVideo[WL_FOR_MAXCOUNT];
	U32 recentVideoTotal;
	U32 countVideo[WL_FOR_MAXCOUNT];
	U32 countVideoTotal;

	WLUsageFlags flags_for_total; // Internal
	U32 recent_time; // Internal
} GeoMemUsage;
extern volatile GeoMemUsage wl_geo_mem_usage;

void wlGeoGetMemUsageQuick(SA_PRE_NN_FREE SA_POST_NN_VALID GeoMemUsage *usage);
U32 wlGetFrameCount(void);

int collCacheSetDisabled(int disabled);

typedef bool (*wleIsActorDisabledFunc)(WorldEncounterProperties *encounter, WorldActorProperties *actor);
typedef void (*wlePatrolPointGetMatFunc)(WorldPatrolProperties *patrol, int pointIdx, Mat4 worldMat, Mat4 outMat, bool snapToY, bool validDrawMat, bool movingUseWorldMat);

typedef void (*GenesisGenerateFunc)(int iPartitionIdx, ZoneMap *zmap, bool preview_mode, bool write_layers);
typedef void (*GenesisGenerateMissionsFunc)(ZoneMapInfo *zmap_info);
typedef void (*GenesisGenerateEpisodeMissionFunc)(const char *episode_root, GenesisEpisode *episode);
typedef void (*GenesisGetSpawnPositionsFunc)(int iPartitionIdx, WorldRegionType region_type, int idx, Vec3 spawn_pos_ret);
typedef WorldVariableDef** (*UGCGetDefaultVariableFunc)(void);
typedef void (*UGCBeforeClientZoneMapLoadFunc)(void);
typedef void (*UGCAfterClientZoneMapLoadFunc)(void);

// Func to get the AI Static Check Expr Context
typedef struct ExprContext ExprContext;
typedef ExprContext* (*AIGetStaticCheckExprContextFunc)();
void worldLibSetAIGetStaticCheckExprContextFunc(AIGetStaticCheckExprContextFunc func);

// TomY ENCOUNTER_HACK
typedef void (*EncounterHackSaveLayerFunc)();
void worldLibSetEncounterHackSaveLayerFunc(EncounterHackSaveLayerFunc func);

// Needed by UGC:
U32 worldCellGetOverrideRebinCRC(void);


void trackerDestroyChildSelect(GroupTrackerChildSelect *select);

extern void worldGetOcclusionVolumeColor(Vec4 color);
extern void worldSetOcclusionVolumeColor(Vec4 color);
extern void worldGetAudioVolumeColor(Vec4 color);
extern void worldSetAudioVolumeColor(Vec4 color);
extern void worldGetSkyFadeVolumeColor(Vec4 color);
extern void worldSetSkyFadeVolumeColor(Vec4 color);
extern void worldGetNeighborhoodVolumeColor(Vec4 color);
extern void worldSetNeighborhoodVolumeColor(Vec4 color);
extern void worldGetInteractionVolumeColor(Vec4 color);
extern void worldSetInteractionVolumeColor(Vec4 color);
extern void worldGetLandmarkVolumeColor(Vec4 color);
extern void worldSetLandmarkVolumeColor(Vec4 color);
extern void worldGetPowerVolumeColor(Vec4 color);
extern void worldSetPowerVolumeColor(Vec4 color);
extern void worldGetWarpVolumeColor(Vec4 color);
extern void worldSetWarpVolumeColor(Vec4 color);
extern void worldGetGenesisVolumeColor(Vec4 color);
extern void worldSetGenesisVolumeColor(Vec4 color);
extern void worldGetTerrainFilterVolumeColor(Vec4 color);
extern void worldSetTerrainFilterVolumeColor(Vec4 color);
extern void worldGetTerrainExclusionVolumeColor(Vec4 color);
extern void worldSetTerrainExclusionVolumeColor(Vec4 color);

C_DECLARATIONS_END

#endif
