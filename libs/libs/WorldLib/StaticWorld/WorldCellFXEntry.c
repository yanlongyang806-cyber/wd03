/***************************************************************************



***************************************************************************/

#include "WorldCellEntryPrivate.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldCell.h"
#include "dynFxInterface.h"
#include "dynFxInfo.h"
#include "partition_enums.h"
#include "WorldCellStreaming.h"
#include "wlState.h"

#include "AutoGen/dynFxInfo_h_ast.h"

#include "timing.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

static const char *s_Geo, *s_TintColor, *s_TintAlpha;

AUTO_RUN;
void allocWorlCellFXEntryStrings(void)
{
	s_Geo = allocAddString("Geo");
	s_TintColor = allocAddString("TintColor");
	s_TintAlpha = allocAddString("TintAlpha");
}

WorldFXCondition *getWorldFXCondition(ZoneMap *zmap, const char *fx_condition, WorldFXEntry *fx_entry, WorldVolumeEntry *water_entry)
{
	WorldFXCondition *condition = NULL;
	
	if (!zmap->world_cell_data.fx_condition_hash)
		zmap->world_cell_data.fx_condition_hash = stashTableCreateWithStringKeys(256, StashDeepCopyKeys_NeverRelease);

	if (!stashFindPointer(zmap->world_cell_data.fx_condition_hash, fx_condition, &condition))
	{
		condition = calloc(1, sizeof(WorldFXCondition));
		condition->name = strdup(fx_condition);
		stashAddPointer(zmap->world_cell_data.fx_condition_hash, fx_condition, condition, false);
	}

	if (fx_entry)
		eaPushUnique(&condition->fx_entries, fx_entry);
	if (water_entry)
		eaPushUnique(&condition->water_entries, water_entry);

	return condition;
}

static void removeFXCondition(WorldFXEntry *fx_entry)
{
	if (!fx_entry)
		return;
	fx_entry->fx_condition = NULL;
}

static void removeWaterCondition(WorldVolumeEntry *water_entry)
{
	if (!water_entry)
		return;
	water_entry->fx_condition = NULL;
}

static void freeCondition(WorldFXCondition *condition)
{
	if (!condition)
		return;
	eaDestroyEx(&condition->fx_entries, removeFXCondition);
	eaDestroyEx(&condition->water_entries, removeWaterCondition);
	SAFE_FREE(condition->name);
	free(condition);
}

static DynParamBlock *createFxParamBlockFromFxEntry(const char *fxName, WorldFXEntry *fx_entry) {
	
	DynParamBlock *pBlock = dynParamBlockCreate();
	char *fxParamsStr = fx_entry->fx_params;
	ParserReadText(fxParamsStr, parse_DynParamBlock, pBlock, 0);
	pBlock->bRunTimeAllocated = true;

	// if this fx entry has a faction, pass in the "IsEnemyFaction" to the FX so it can decide what to do with it
	if (fx_entry->faction_name && wl_state.check_enemy_faction_func)
	{
		bool isEnemy = wl_state.check_enemy_faction_func(fx_entry->faction_name);
		DynDefineParam *pParam = StructAlloc(parse_DynDefineParam);
		MultiValSetFloat(&pParam->mvVal, isEnemy);
		pParam->pcParamName = allocAddString("IsEnemyFaction");
		eaPush(&pBlock->eaDefineParams,pParam);
	}

	return pBlock;
}

static void createFx(WorldFXEntry *fx_entry, const char* pcFx, F32 hue, bool one_shot)
{
	DynParamBlock *block = createFxParamBlockFromFxEntry(pcFx, fx_entry);
	
	if (!fx_entry->node_guid)
	{
		fx_entry->node_guid = dtNodeCreate();
		dtNodeSetFromMat4(fx_entry->node_guid, fx_entry->base_entry.bounds.world_matrix);
	}

	if (fx_entry->has_target_node && !fx_entry->target_node_guid)
	{
		Mat4 childMat;
		fx_entry->target_node_guid = dtNodeCreate();
		mulMat4(fx_entry->base_entry.bounds.world_matrix, fx_entry->target_node_mat, childMat);
		dtNodeSetFromMat4(fx_entry->target_node_guid, childMat);
	} 
	else if(!fx_entry->has_target_node && fx_entry->target_node_guid)
	{
		dtNodeDestroy(fx_entry->target_node_guid);
		fx_entry->target_node_guid = 0;
	}

	if (!fx_entry->fx_manager)
		fx_entry->fx_manager = dtFxManCreate(eFxManagerType_World, fx_entry->node_guid, fx_entry, false, wl_state.no_sound_for_temp_group);

	if (one_shot)
		dtAddFx(fx_entry->fx_manager, pcFx, block, fx_entry->target_node_guid, 0, hue, 0, NULL, eDynFxSource_Environment, NULL, NULL);
	else
		dtFxManAddMaintainedFx(fx_entry->fx_manager, pcFx, block, hue, fx_entry->target_node_guid, eDynFxSource_Environment);
}

static void createDebrisFx(WorldFXEntry *fx_entry)
{
	DynParamBlock *pParamBlock;
	DynDefineParam *pParam;
	Vec3 tint_color;
	F32 tint_alpha;

	if (!fx_entry->node_guid)
	{
		Mat4 mWorldMatrix;
		fx_entry->node_guid = dtNodeCreate();
		copyMat4(fx_entry->base_entry.bounds.world_matrix, mWorldMatrix);
		scaleMat3Vec3(mWorldMatrix, fx_entry->debris_scale);
		dtNodeSetFromMat4(fx_entry->node_guid, mWorldMatrix);
	}

	pParamBlock = dynParamBlockCreate();
	pParam = StructAlloc(parse_DynDefineParam);
	pParam->pcParamName = s_Geo;
	MultiValSetString(&pParam->mvVal, fx_entry->debris_model_name);
	eaPush(&pParamBlock->eaDefineParams, pParam);

	pParam = StructAlloc(parse_DynDefineParam);
	pParam->pcParamName = s_TintColor;
	scaleVec3(fx_entry->debris_tint_color, 255, tint_color);
	MultiValSetVec3(&pParam->mvVal, &tint_color);
	eaPush(&pParamBlock->eaDefineParams, pParam);

	pParam = StructAlloc(parse_DynDefineParam);
	pParam->pcParamName = s_TintAlpha;
	tint_alpha = fx_entry->debris_tint_color[3] * 255;
	MultiValSetFloat(&pParam->mvVal, tint_alpha);
	eaPush(&pParamBlock->eaDefineParams, pParam);

	fx_entry->debris_fx_guid = dtAddFx(worldRegionGetGlobalFXManager(SAFE_MEMBER(fx_entry->base_entry_data.cell, region)), "PhysObject", pParamBlock, 0, fx_entry->node_guid, 0, 0, NULL, eDynFxSource_Environment, NULL, NULL);

	if (fx_entry->debris_draw_list)
		dtFxSetInstanceData(fx_entry->debris_fx_guid, fx_entry->debris_draw_list, fx_entry->debris_instance_param_list, worldGetResetCount(true));

	fx_entry->debris_needs_reset = false;
}

void setupFXEntryDictionary(ZoneMap *zmap)
{
	if (zmap->world_cell_data.fx_entry_dict)
		return;

	sprintf(zmap->world_cell_data.fx_entry_dict_name, "WorldFXEntry_%d", eaFind(&world_grid.maps, zmap));
	zmap->world_cell_data.fx_entry_dict = RefSystem_GetDictionaryHandleFromNameOrHandle(zmap->world_cell_data.fx_entry_dict_name);
	if (!zmap->world_cell_data.fx_entry_dict)
		zmap->world_cell_data.fx_entry_dict = RefSystem_RegisterDictionaryWithIntRefData(zmap->world_cell_data.fx_entry_dict_name, NULL, NULL, false);
}
static void addToDictionary(ZoneMap *zmap, WorldFXEntry *fx_entry)
{
	setupFXEntryDictionary(zmap);
	assert(zmap->world_cell_data.fx_id_counter > fx_entry->id);
	RefSystem_AddIntKeyReferent(zmap->world_cell_data.fx_entry_dict, fx_entry->id, fx_entry);
}

WorldFXEntry *createWorldFXEntry(const Mat4 world_matrix, const Vec3 world_mid, F32 radius, F32 far_lod_far_dist, const GroupInfo *info)
{
	WorldFXEntry *fx_entry = calloc(1, sizeof(WorldFXEntry));
	WorldCellEntryBounds *bounds = &fx_entry->base_entry.bounds;

	fx_entry->base_entry.type = WCENT_FX;
	copyMat4(world_matrix, bounds->world_matrix);

	if (world_mid)
		copyVec3(world_mid, bounds->world_mid);
	else
		copyVec3(bounds->world_matrix[3], bounds->world_mid);


	fx_entry->id = info->zmap->world_cell_data.fx_id_counter++;
	addToDictionary(info->zmap, fx_entry);

	fx_entry->low_detail = !!info->low_detail;
	fx_entry->high_detail = !!info->high_detail;
	fx_entry->high_fill_detail = !!info->high_fill_detail;

	fx_entry->fx_alpha = 1.0f;

	if (info->animation_entry)
	{
		setupWorldAnimationEntryDictionary(info->zmap);
		SET_HANDLE_FROM_INT(info->zmap->world_cell_data.animation_entry_dict_name, info->animation_entry->id, fx_entry->animation_controller_handle);

		worldAnimationEntryModifyBounds(info->animation_entry, 
			NULL, NULL, NULL, 
			bounds->world_mid, &radius);

		fx_entry->controller_relative_matrix_inited = worldAnimationEntryInitChildRelativeMatrix(info->animation_entry, bounds->world_matrix, fx_entry->controller_relative_matrix);
	}

	fx_entry->base_entry.shared_bounds = createSharedBoundsSphere(info->zmap, radius, 0, far_lod_far_dist, far_lod_far_dist);

	return fx_entry;
}

void worldFxReloadEntriesMatchingFxName(const char* fx_name)
{
	RefDictIterator iter;
	WorldFXEntry* fx_entry;
	WorldFXEntry** eaToRemove = NULL;
	bool bFoundOne = false;
	int i;

	for (i = 0; i < eaSize(&world_grid.maps); ++i)
	{
		ZoneMap *zmap = world_grid.maps[i];
		if (!zmap->world_cell_data.fx_entry_dict)
			continue;

		RefSystem_InitRefDictIterator(zmap->world_cell_data.fx_entry_dict, &iter);
		while (fx_entry = RefSystem_GetNextReferentFromIterator(&iter))
		{
			if (fx_entry->fx_name && stricmp(fx_name, fx_entry->fx_name) == 0)
			{
				worldCellSetEditable();
				bFoundOne = true;
				break;
			}
		}

		if (!bFoundOne)
			continue;

		RefSystem_InitRefDictIterator(zmap->world_cell_data.fx_entry_dict, &iter);
		while (fx_entry = RefSystem_GetNextReferentFromIterator(&iter))
		{
			if (fx_entry->fx_name && stricmp(fx_name, fx_entry->fx_name) == 0)
				eaPush(&eaToRemove, fx_entry);
		}

		// If we found any, process them now, not in the stash iterator (since we'll be adding entries)
		if (eaSize(&eaToRemove) > 0)
		{
			worldCellSetEditable();

			if (dynFxInfoExists(fx_name))
			{
				F32 fDistance = dynFxInfoGetDrawDistance(fx_name);
				if (fDistance <= 0.0f)
					fDistance = 300.0f;
				FOR_EACH_IN_EARRAY(eaToRemove, WorldFXEntry, fx_entry_to_remove)
					WorldCellEntryData* data = worldCellEntryGetData(&fx_entry_to_remove->base_entry);
					WorldRegion* region = data->cell->region;

					// Need to reload this one
					worldRemoveCellEntry(&fx_entry_to_remove->base_entry);

					// update shared bounds
					fx_entry_to_remove->base_entry.shared_bounds = createSharedBoundsCopy(fx_entry_to_remove->base_entry.shared_bounds, true);
					setSharedBoundsVisDist(fx_entry_to_remove->base_entry.shared_bounds, 0, fDistance, fDistance);
					fx_entry_to_remove->base_entry.shared_bounds = lookupSharedBounds(region->zmap_parent, fx_entry_to_remove->base_entry.shared_bounds);

					fx_entry_to_remove->fx_is_continuous = fx_entry_to_remove->fx_name?!dynFxInfoSelfTerminates(fx_entry_to_remove->fx_name):false;

					worldAddCellEntry(region, &fx_entry_to_remove->base_entry);
				FOR_EACH_END;
				eaDestroy(&eaToRemove);
			}
		}
	}
}

void initWorldFXEntry(ZoneMap *zmap, WorldFXEntry *fx_entry, F32 hue, const char *fx_condition, const char *fx_name, const char *fx_params, bool has_target_node, bool target_no_anim, Mat4 target_node_mat, const char *faction_name, const char *filename)
{
	fx_entry->fx_hue = hue;
	fx_entry->fx_condition = fx_condition ? getWorldFXCondition(zmap, fx_condition, fx_entry, /*water_entry=*/NULL) : NULL;
	fx_entry->fx_name = fx_name?strdup(fx_name):NULL;
	fx_entry->fx_params = fx_params?strdup(fx_params):NULL;
	fx_entry->faction_name = faction_name?strdup(faction_name):NULL;
	fx_entry->fx_is_continuous = fx_entry->fx_name?!dynFxInfoSelfTerminates(fx_entry->fx_name):false;
	fx_entry->has_target_node = has_target_node;
	fx_entry->target_no_anim = target_no_anim;
	if(has_target_node)
		copyMat4(target_node_mat, fx_entry->target_node_mat);
	else
		identityMat4(fx_entry->target_node_mat);

	if (fx_entry->fx_name && !fx_entry->fx_is_continuous && !fx_entry->fx_condition && filename)
		ErrorFilenameGroupRetroactivef(filename, "Owner", 7, 6, 22, 2009, "Non-continuing world FX node found without a trigger condition, FX (%s) will not play! \nIf this is supposed to be a continuing FX please fix the fxinfo file.", fx_entry->fx_name);
}

void initWorldFXEntryDebris(WorldFXEntry *fx_entry, Model *model, const Vec3 model_scale, const GroupInfo *info, GroupDef *def, 
							const char ***texture_swaps, const char ***material_swaps, 
							MaterialNamedConstant **material_constants)
{
	if (!model_scale)
		model_scale = unitvec3;
	groupQuantizeScale(model_scale, fx_entry->debris_scale);

	if (model)
	{
		fx_entry->debris_model_name = model->name?strdup(model->name):NULL;
		fx_entry->debris_draw_list = worldCreateDrawableList(	model, model_scale, info, def, 
																info->material_replace, texture_swaps, material_swaps, 
																NULL, material_constants, 
																&info->zmap->world_cell_data.drawable_pool, true, WL_FOR_WORLD, &fx_entry->debris_instance_param_list);
		worldCellEntryApplyTintColor(info, fx_entry->debris_tint_color);
	}

}

WorldCellEntry *createWorldFXEntryFromParsed(ZoneMap *zmap, WorldFXEntryParsed *entry_parsed)
{
	WorldFXEntry *fx_entry = calloc(1, sizeof(WorldFXEntry));
	WorldStreamingPooledInfo *streaming_pooled_info = zmap->world_cell_data.streaming_pooled_info;
	WorldAnimationEntry *animation_controller;
	const char *fx_condition = NULL, *fx_name = NULL;

	fx_entry->base_entry.type = WCENT_FX;

	worldEntryInitBoundsFromParsed(streaming_pooled_info, &fx_entry->base_entry, &entry_parsed->base_data);

	fx_entry->id = entry_parsed->id;
	addToDictionary(zmap, fx_entry);

	fx_entry->low_detail = entry_parsed->low_detail;
	fx_entry->high_detail = entry_parsed->high_detail;
	fx_entry->high_fill_detail = entry_parsed->high_fill_detail;

	fx_entry->interaction_node_owned = entry_parsed->interaction_node_owned;

	if (entry_parsed->animation_entry_id)
	{
		setupWorldAnimationEntryDictionary(zmap);
		SET_HANDLE_FROM_INT(zmap->world_cell_data.animation_entry_dict_name, entry_parsed->animation_entry_id, fx_entry->animation_controller_handle);
	}

	if (animation_controller = GET_REF(fx_entry->animation_controller_handle))
		fx_entry->controller_relative_matrix_inited = worldAnimationEntryInitChildRelativeMatrix(animation_controller, entry_parsed->base_data.entry_bounds.world_matrix, fx_entry->controller_relative_matrix);

	initWorldFXEntry(zmap, fx_entry, entry_parsed->fx_hue, 
		worldGetStreamedString(streaming_pooled_info, entry_parsed->fx_condition_idx), 
		worldGetStreamedString(streaming_pooled_info, entry_parsed->fx_name_idx), 
		worldGetStreamedString(streaming_pooled_info, entry_parsed->fx_params_idx),
		entry_parsed->has_target_node, entry_parsed->target_no_anim, entry_parsed->target_node_mat, 
		worldGetStreamedString(streaming_pooled_info, entry_parsed->fx_faction_idx), NULL);

	if (entry_parsed->debris)
	{
		const char *model_name = worldGetStreamedString(streaming_pooled_info, entry_parsed->debris->model_idx);

		fx_entry->debris_model_name = model_name?strdup(model_name):NULL;
		copyVec3(entry_parsed->debris->scale, fx_entry->debris_scale);
		copyVec4(entry_parsed->debris->tint_color, fx_entry->debris_tint_color);

		if (entry_parsed->debris->draw_list_idx >= 0)
		{
			assert(entry_parsed->debris->draw_list_idx < eaSize(&streaming_pooled_info->drawable_lists));
			fx_entry->debris_draw_list = streaming_pooled_info->drawable_lists[entry_parsed->debris->draw_list_idx];
			addDrawableListRef(fx_entry->debris_draw_list MEM_DBG_PARMS_INIT);
		}

		if (entry_parsed->debris->instance_param_list_idx >= 0)
		{
			assert(entry_parsed->debris->instance_param_list_idx < eaSize(&streaming_pooled_info->instance_param_lists));
			fx_entry->debris_instance_param_list = streaming_pooled_info->instance_param_lists[entry_parsed->debris->instance_param_list_idx];
			addInstanceParamListRef(fx_entry->debris_instance_param_list MEM_DBG_PARMS_INIT);
		}
	}

	return &fx_entry->base_entry;
}

void uninitWorldFXEntry(WorldFXEntry *fx_entry)
{
	if (fx_entry->fx_condition)
		eaFindAndRemoveFast(&fx_entry->fx_condition->fx_entries, fx_entry);

	SAFE_FREE(fx_entry->fx_name);
	SAFE_FREE(fx_entry->interaction_fx_name);
	SAFE_FREE(fx_entry->debris_model_name);
	SAFE_FREE(fx_entry->faction_name);
	
	removeDrawableListRefDbg(fx_entry->debris_draw_list MEM_DBG_PARMS_INIT);
	fx_entry->debris_draw_list = NULL;

	removeInstanceParamListRef(fx_entry->debris_instance_param_list MEM_DBG_PARMS_INIT);
	fx_entry->debris_instance_param_list = NULL;

	if (IS_HANDLE_ACTIVE(fx_entry->animation_controller_handle))
		REMOVE_HANDLE(fx_entry->animation_controller_handle);

	RefSystem_RemoveReferent(fx_entry, false);
}

__forceinline static bool shouldFXPlay(WorldFXEntry *fx_entry)
{
	return !(fx_entry->high_detail && !wl_state.draw_high_detail || fx_entry->low_detail && wl_state.draw_high_detail || fx_entry->high_fill_detail && !wl_state.draw_high_fill_detail);
}

void startWorldFX(WorldFXEntry *fx_entry)
{
	if (!shouldFXPlay(fx_entry))
		return;

	if (fx_entry->fx_name && fx_entry->fx_is_continuous && (!fx_entry->fx_condition || fx_entry->fx_condition->state))
		createFx(fx_entry, fx_entry->fx_name, fx_entry->fx_hue, false);

	if (fx_entry->interaction_fx_name)
		createFx(fx_entry, fx_entry->interaction_fx_name, 0, false);

	if (fx_entry->debris_model_name)
		createDebrisFx(fx_entry);

	fx_entry->started = true;
}

void stopWorldFX(WorldFXEntry *fx_entry)
{
	if (fx_entry->fx_manager)
	{
		if (fx_entry->fx_name && fx_entry->fx_is_continuous && (!fx_entry->fx_condition || fx_entry->fx_condition->state))
			dtFxManRemoveMaintainedFx(fx_entry->fx_manager, fx_entry->fx_name);

		if (fx_entry->interaction_fx_name)
		{
			dtFxManRemoveMaintainedFx(fx_entry->fx_manager, fx_entry->interaction_fx_name);
			setVec3same(fx_entry->ambient_offset, 0);
		}

		dtFxManDestroy(fx_entry->fx_manager);
		fx_entry->fx_manager = 0;
	}

	if (fx_entry->debris_fx_guid)
	{
		dtFxKill(fx_entry->debris_fx_guid);
		fx_entry->debris_fx_guid = 0;
	}

	if (fx_entry->node_guid)
	{
		dtNodeDestroy(fx_entry->node_guid);
		fx_entry->node_guid = 0;
	}

	fx_entry->started = false;
}

static U32 last_fx_update_timestamp;
void worldFXUpdateOncePerFrame(F32 loop_timer, U32 update_timestamp)
{
	RefDictIterator iter;
	WorldFXEntry *fx_entry;
	int i;

	if (last_fx_update_timestamp == update_timestamp)
		return;

	last_fx_update_timestamp = update_timestamp;

	for (i = 0; i < eaSize(&world_grid.maps); ++i)
	{
		ZoneMap *zmap = world_grid.maps[i];
		if (!zmap->world_cell_data.fx_entry_dict)
			continue;

		RefSystem_InitRefDictIterator(zmap->world_cell_data.fx_entry_dict, &iter);
		while (fx_entry = RefSystem_GetNextReferentFromIterator(&iter))
		{
			WorldAnimationEntry *animation_controller;

			if (!worldCellEntryIsPartitionOpen(&fx_entry->base_entry, PARTITION_CLIENT) || worldCellEntryIsPartitionDisabled(&fx_entry->base_entry, PARTITION_CLIENT))
				continue;
						
			if (wl_state.player_faction_changed && fx_entry->faction_name && fx_entry->started)
			{	// the player's faction has changed, if this fx has a faction and has has been started
				// restart the fx
				stopWorldFX(fx_entry);
				startWorldFX(fx_entry);
			}
			
			if (fx_entry->node_guid && (animation_controller = GET_REF(fx_entry->animation_controller_handle)))
			{
				Mat4 world_matrix;
				if (animation_controller->last_update_timestamp != update_timestamp)
					worldAnimationEntryUpdate(animation_controller, loop_timer, update_timestamp);
				if (animation_controller->animation_properties.local_space)
				{
					mulMat4Inline(fx_entry->base_entry.bounds.world_matrix, animation_controller->full_matrix, world_matrix);
				}
				else
				{
					if (!fx_entry->controller_relative_matrix_inited)
						fx_entry->controller_relative_matrix_inited = worldAnimationEntryInitChildRelativeMatrix(animation_controller, fx_entry->base_entry.bounds.world_matrix, fx_entry->controller_relative_matrix);
					mulMat4Inline(animation_controller->full_matrix, fx_entry->controller_relative_matrix, world_matrix);
				}
				dtNodeSetFromMat4(fx_entry->node_guid, world_matrix);
				if(fx_entry->target_node_guid && !fx_entry->target_no_anim)
				{
					Mat4 target_matrix;
					mulMat4(world_matrix, fx_entry->target_node_mat, target_matrix);
					dtNodeSetFromMat4(fx_entry->target_node_guid, target_matrix);
				}
			}

			if (fx_entry->high_fill_detail || fx_entry->high_detail || fx_entry->low_detail)
			{
				bool should_play = shouldFXPlay(fx_entry);
				if (should_play && !fx_entry->started)
					startWorldFX(fx_entry);
				else if (!should_play && fx_entry->started)
					stopWorldFX(fx_entry);
			}

			if (fx_entry->debris_fx_guid && fx_entry->debris_model_name && fx_entry->debris_needs_reset)
			{
				dtFxKill(fx_entry->debris_fx_guid);
				fx_entry->debris_fx_guid = 0;
				if (shouldFXPlay(fx_entry))
					createDebrisFx(fx_entry);
			}
		}
	}

	wl_state.player_faction_changed = false;
}

void worldFXClearPostDrawing(void)
{
	RefDictIterator iter;
	WorldFXEntry *fx_entry;
	int i;
	
	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < eaSize(&world_grid.maps); ++i)
	{
		ZoneMap *zmap = world_grid.maps[i];
		if (!zmap->world_cell_data.fx_entry_dict)
			continue;

		RefSystem_InitRefDictIterator(zmap->world_cell_data.fx_entry_dict, &iter);
		while (fx_entry = RefSystem_GetNextReferentFromIterator(&iter))
		{
			fx_entry->material = NULL;
			fx_entry->add_material = 0;
			fx_entry->fx_alpha = 1.0f;
		}
	}
	
	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME("UpdateTriggerCondFx") ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void worldSetFXCondition(const char *fx_condition, bool state)
{
	int i, j;

	for (j = 0; j < eaSize(&world_grid.maps); ++j)
	{
		ZoneMap *zmap = world_grid.maps[j];
		WorldFXCondition *condition = getWorldFXCondition(zmap, fx_condition, /*fx_entry=*/NULL, /*water_entry=*/NULL);

		if (condition->state == state)
			continue;

		condition->state = state;
		for (i = 0; i < eaSize(&condition->fx_entries); ++i)
		{
			WorldFXEntry *fx_entry = condition->fx_entries[i];
			if (worldCellEntryIsPartitionOpen(&fx_entry->base_entry, PARTITION_CLIENT) && !worldCellEntryIsPartitionDisabled(&fx_entry->base_entry, PARTITION_CLIENT) && fx_entry->fx_name && 
				shouldFXPlay(fx_entry))
			{
				if (fx_entry->fx_is_continuous)
				{
					if (state)
					{
						createFx(fx_entry, fx_entry->fx_name, fx_entry->fx_hue, false);
					}
					else if (fx_entry->fx_manager)
					{
						dtFxManRemoveMaintainedFx(fx_entry->fx_manager, fx_entry->fx_name);
						setVec3same(fx_entry->ambient_offset, 0);
					}
				}
				else if (state)
				{
					// state changed from false to true, trigger one-shot FX
					createFx(fx_entry, fx_entry->fx_name, fx_entry->fx_hue, true);
				}
			}
		}

		for (i = 0; i < eaSize(&condition->water_entries); ++i)
		{
			WorldVolumeEntry *water_entry = condition->water_entries[i];
			if(worldCellEntryIsPartitionOpen(&water_entry->base_entry, PARTITION_CLIENT) && !worldCellEntryIsPartitionDisabled(&water_entry->base_entry, PARTITION_CLIENT))
				water_entry->fx_condition_state = state;
		}
	}
}

void worldCellFXReset(ZoneMap *zmap)
{
	if (zmap->world_cell_data.fx_condition_hash)
	{
		stashTableDestroyEx(zmap->world_cell_data.fx_condition_hash, NULL, freeCondition);
		zmap->world_cell_data.fx_condition_hash = NULL;
	}

	if (zmap->world_cell_data.fx_entry_dict)
		RefSystem_ClearDictionary(zmap->world_cell_data.fx_entry_dict, true);
	zmap->world_cell_data.fx_entry_dict = NULL;

	zmap->world_cell_data.fx_id_counter = 1;
}

void worldFXSetIDCounter(ZoneMap *zmap, U32 id_counter)
{
	MAX1(zmap->world_cell_data.fx_id_counter, id_counter);
}

void worldInteractionEntrySetFX(int iPartitionIdx, WorldInteractionEntry *entry, const char *fx_name, const char* unique_fx_name)
{
	int i;
	int iFX = 0;

	for (i=0;i<eaSize(&entry->child_entries);i++)
	{
		if (entry->child_entries[i]->type == WCENT_INTERACTION)
		{
			worldInteractionEntrySetFX(iPartitionIdx, (WorldInteractionEntry *)entry->child_entries[i], fx_name, NULL);
		}
		else if (entry->child_entries[i]->type == WCENT_FX)
		{
			WorldFXEntry * fx_entry = (WorldFXEntry *)entry->child_entries[i];
			const char* pchFXNameToUse = fx_name;
			if (unique_fx_name && iFX == 0)
				pchFXNameToUse = unique_fx_name;
			if (fx_entry->interaction_node_owned && stricmp(fx_entry->interaction_fx_name, pchFXNameToUse)!=0)
			{
				// stop the old interaction fx
				if (fx_entry->fx_manager && fx_entry->interaction_fx_name)
				{
					dtFxManRemoveMaintainedFx(fx_entry->fx_manager, fx_entry->interaction_fx_name);
					setVec3same(fx_entry->ambient_offset, 0);
					SAFE_FREE(fx_entry->interaction_fx_name);
				}

				fx_entry->interaction_fx_name = pchFXNameToUse?strdup(pchFXNameToUse):NULL;
				if (fx_entry->interaction_fx_name && worldCellEntryIsPartitionOpen(&fx_entry->base_entry, iPartitionIdx) && !worldCellEntryIsPartitionDisabled(&fx_entry->base_entry, iPartitionIdx) && shouldFXPlay(fx_entry))
					createFx(fx_entry, fx_entry->interaction_fx_name, 0, false);
			}
			iFX++;
		}
	}

}
