/***************************************************************************



***************************************************************************/

#include "WorldCellEntryPrivate.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldCell.h"
#include "wlState.h"
#include "wlCostume.h"
#include "WorldColl.h"
#include "wlEncounter.h"
#include "dynFxInterface.h"
#include "bounds.h"

#include "StringCache.h"
#include "HashFunctions.h"
#include "partition_enums.h"
#include "qsortG.h"
#include "timing.h"
#include "strings_opt.h"
#include "logging.h"

extern int gLogInteractionChanges;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

extern const char* pcDefaultSkeleton;

#define INTERACTION_COSTUME_FORMAT "IC%d"

//////////////////////////////////////////////////////////////////////////

static U32 hashInteractionCostume(const WorldInteractionCostume *costume, int hashSeed)
{
	U32 hash = hashSeed;
	int i;

	hash = burtlehash2((void *)&costume->hand_pivot[0][0], sizeof(Mat4)/4, hash);
	hash = burtlehash2((void *)&costume->mass_pivot[0][0], sizeof(Mat4)/4, hash);

	if (costume->carry_anim_bit)
		hash = burtlehash(costume->carry_anim_bit, (U32)strlen(costume->carry_anim_bit), hash);

	for (i = 0; i < eaSize(&costume->costume_parts); ++i)
	{
		WorldInteractionCostumePart *part = costume->costume_parts[i];
		hash = burtlehash2((void *)&part->collision, sizeof(int)/4, hash);
		hash = burtlehash2((void *)&part->model, sizeof(part->model)/4, hash);
		hash = burtlehash2((void *)&part->matrix[0][0], sizeof(Mat4)/4, hash);
		hash = burtlehash2((void *)&part->tint_color[0], sizeof(Vec4)/4, hash);
		if (part->draw_list)
		{
			hash = hashDrawableList(STRUCT_NOCONST(WorldDrawableList, part->draw_list), hash);
		}
		if (part->instance_param_list)
		{
			hash = hashInstanceParamList(part->instance_param_list, hash);
		}
	}

	return hash;
}

static int cmpInteractionCostume(const WorldInteractionCostume *costume1, const WorldInteractionCostume *costume2)
{
	int it, i;
	intptr_t pt;

	it = memcmp(&costume1->hand_pivot[0][0], &costume2->hand_pivot[0][0], sizeof(Mat4));
	if (it)
		return SIGN(it);

	it = memcmp(&costume1->mass_pivot[0][0], &costume2->mass_pivot[0][0], sizeof(Mat4));
	if (it)
		return SIGN(it);

	pt = (intptr_t)costume1->carry_anim_bit - (intptr_t)costume2->carry_anim_bit;
	if (pt)
		return SIGN(pt);

	it = (int)eaSize(&costume1->costume_parts) - (int)eaSize(&costume2->costume_parts);
	if (it)
		return SIGN(it);

	for (i = 0; i < eaSize(&costume1->costume_parts); ++i)
	{
		WorldInteractionCostumePart *part1 = costume1->costume_parts[i];
		WorldInteractionCostumePart *part2 = costume2->costume_parts[i];

		it = part1->collision - part2->collision;
		if (it)
			return SIGN(it);

		pt = (intptr_t)part1->model - (intptr_t)part2->model;
		if (pt)
			return SIGN(pt);

		it = memcmp(&part1->matrix[0][0], &part2->matrix[0][0], sizeof(Mat4));
		if (it)
			return SIGN(it);

		it = memcmp(&part1->tint_color[0], &part2->tint_color[0], sizeof(Vec4));
		if (it)
			return SIGN(it);

		pt = (intptr_t)part1->draw_list - (intptr_t)part2->draw_list;
		if (pt)
			return SIGN(pt);

		pt = (intptr_t)part1->instance_param_list - (intptr_t)part2->instance_param_list;
		if (pt)
			return SIGN(pt);
	}

	return 0;
}

static int cmpInteractionCostumeName(const WorldInteractionCostume **pcostume1, const WorldInteractionCostume **pcostume2)
{
	return (*pcostume1)->name_id - (*pcostume2)->name_id;
}

static WorldInteractionCostume *allocInteractionCostume(SA_PARAM_NN_VALID ZoneMap *zmap, int name_id_override, const Mat4 hand_pivot, const Mat4 mass_pivot, const char *carry_anim_bit)
{
	WorldInteractionCostume *costume = calloc(1, sizeof(WorldInteractionCostume));

	costume->zmap = zmap;

	if (!zmap->world_cell_data.interaction_costume_hash_table)
		zmap->world_cell_data.interaction_costume_hash_table = stashTableCreateExternalFunctions(4096, StashDefault, hashInteractionCostume, cmpInteractionCostume);

	if (!zmap->world_cell_data.interaction_to_costume_hash)
		zmap->world_cell_data.interaction_to_costume_hash = stashTableCreateInt(4096);

	if (name_id_override < 0)
	{
		costume->name_id = zmap->world_cell_data.interaction_costume_idx++;
	}
	else
	{
		costume->name_id = name_id_override;
		if (name_id_override >= zmap->world_cell_data.interaction_costume_idx)
			zmap->world_cell_data.interaction_costume_idx = name_id_override + 1;
	}

	++zmap->world_cell_data.total_costume_count;

	copyMat4(hand_pivot, costume->hand_pivot);
	copyMat4(mass_pivot, costume->mass_pivot);
	costume->carry_anim_bit = allocAddString(carry_anim_bit);

	return costume;
}

static void freeInteractionCostume(SA_PRE_NN_VALID SA_POST_P_FREE WorldInteractionCostume *costume, bool remove_from_hash_table MEM_DBG_PARMS)
{
	ZoneMap *zmap = costume->zmap;
	WLCostume *wl_costume;
	char costume_name[512];
	int i;

	assert(costume->ref_count == 0);

	if (remove_from_hash_table)
		stashRemovePointer(zmap->world_cell_data.interaction_costume_hash_table, costume, NULL);

	for (i = 0; i < eaSize(&costume->costume_parts); ++i)
	{
		removeDrawableListRef(costume->costume_parts[i]->draw_list);
		removeInstanceParamListRef(costume->costume_parts[i]->instance_param_list MEM_DBG_PARMS_CALL);
	}

	sprintf(costume_name, INTERACTION_COSTUME_FORMAT, costume->name_id);
	if (wl_costume = wlCostumeFromName(costume_name))
	{
		for (i = 0; i < eaSize(&wl_costume->eaCostumeParts); ++i)
		{
			removeDrawableListRef(wl_costume->eaCostumeParts[i]->pWorldDrawableList);
			wl_costume->eaCostumeParts[i]->pWorldDrawableList = NULL;

			removeInstanceParamListRef(wl_costume->eaCostumeParts[i]->pInstanceParamList MEM_DBG_PARMS_CALL);
			wl_costume->eaCostumeParts[i]->pInstanceParamList = NULL;
		}
		wlCostumeRemoveByName(costume_name);
	}

	sprintf(costume_name, INTERACTION_COSTUME_FORMAT " HandPivot", costume->name_id);
	if (wl_costume = wlCostumeFromName(costume_name))
	{
		for (i = 0; i < eaSize(&wl_costume->eaCostumeParts); ++i)
		{
			removeDrawableListRef(wl_costume->eaCostumeParts[i]->pWorldDrawableList);
			wl_costume->eaCostumeParts[i]->pWorldDrawableList = NULL;

			removeInstanceParamListRef(wl_costume->eaCostumeParts[i]->pInstanceParamList MEM_DBG_PARMS_CALL);
			wl_costume->eaCostumeParts[i]->pInstanceParamList = NULL;
		}
		wlCostumeRemoveByName(costume_name);
	}

	sprintf(costume_name, INTERACTION_COSTUME_FORMAT " MassPivot", costume->name_id);
	if (wl_costume = wlCostumeFromName(costume_name))
	{
		for (i = 0; i < eaSize(&wl_costume->eaCostumeParts); ++i)
		{
			removeDrawableListRef(wl_costume->eaCostumeParts[i]->pWorldDrawableList);
			wl_costume->eaCostumeParts[i]->pWorldDrawableList = NULL;

			removeInstanceParamListRef(wl_costume->eaCostumeParts[i]->pInstanceParamList MEM_DBG_PARMS_CALL);
			wl_costume->eaCostumeParts[i]->pInstanceParamList = NULL;
		}
		wlCostumeRemoveByName(costume_name);
	}

	eaDestroyEx(&costume->costume_parts, NULL);
	eaiDestroy(&costume->interaction_uids);
	SAFE_FREE(costume);

	--zmap->world_cell_data.total_costume_count;
}

static WorldInteractionCostume *lookupInteractionCostume(ZoneMap *zmap, WorldInteractionCostume *costume, int interaction_uid MEM_DBG_PARMS)
{
	WorldInteractionCostume *existing_costume = NULL;

	if (stashFindPointer(zmap->world_cell_data.interaction_costume_hash_table, costume, &existing_costume))
	{
		freeInteractionCostume(costume, false MEM_DBG_PARMS_CALL);
		costume = existing_costume;
	}
	else
	{
		stashAddPointer(zmap->world_cell_data.interaction_costume_hash_table, costume, costume, false);
	}

	if (interaction_uid)
		assert(stashIntAddPointer(zmap->world_cell_data.interaction_to_costume_hash, interaction_uid, costume, false));

	++costume->ref_count;

	return costume;
}

void removeInteractionCostumeRef(WorldInteractionCostume *costume, int interaction_uid)
{
	ZoneMap *zmap;

	if (!costume)
		return;

	zmap = costume->zmap;
	if (interaction_uid && zmap->world_cell_data.interaction_to_costume_hash)
		stashIntRemovePointer(zmap->world_cell_data.interaction_to_costume_hash, interaction_uid, NULL);

	costume->ref_count--;
	if (costume->ref_count <= 0)
		freeInteractionCostume(costume, true MEM_DBG_PARMS_INIT);
}

void getAllInteractionCostumes(ZoneMap *zmap, WorldInteractionCostume ***costume_array)
{
	StashTableIterator iter;
	StashElement elem;

	if (zmap->world_cell_data.interaction_costume_hash_table)
	{
		int initial_size = eaSize(costume_array);

		stashGetIterator(zmap->world_cell_data.interaction_costume_hash_table, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			WorldInteractionCostume *costume = stashElementGetPointer(elem);
			if (costume && eaFind(costume_array, costume) < 0)
			{
				eaPush(costume_array, costume);
				++costume->ref_count;
			}
		}

		qsortG((*costume_array) + initial_size, eaSize(costume_array) - initial_size, sizeof(WorldInteractionCostume *), cmpInteractionCostumeName);
	}

	if (zmap->world_cell_data.interaction_to_costume_hash)
	{
		stashGetIterator(zmap->world_cell_data.interaction_to_costume_hash, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			WorldInteractionCostume *costume = stashElementGetPointer(elem);
			int interaction_uid = stashElementGetIntKey(elem);
			if (costume && interaction_uid)
			{
				// keep the uids sorted
				int i;
				for (i = 0; i < eaiSize(&costume->interaction_uids); ++i)
				{
					if (interaction_uid < costume->interaction_uids[i])
						break;
				}
				eaiInsert(&costume->interaction_uids, interaction_uid, i);
			}
		}
	}
}

void interactionCostumeLeaveStreamingMode(ZoneMap *zmap)
{
	stashTableDestroy(zmap->world_cell_data.interaction_to_costume_hash);
	zmap->world_cell_data.interaction_to_costume_hash = NULL;
}

void worldCellInteractionReset(ZoneMap *zmap)
{
	StashTableIterator iter;
	StashElement elem;

	if (zmap->world_cell_data.interaction_costume_hash_table)
	{
		stashGetIterator(zmap->world_cell_data.interaction_costume_hash_table, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			WorldInteractionCostume *costume = stashElementGetPointer(elem);
			if (costume)
				freeInteractionCostume(costume, false MEM_DBG_PARMS_INIT);
		}
	}

	stashTableDestroy(zmap->world_cell_data.interaction_costume_hash_table);
	zmap->world_cell_data.interaction_costume_hash_table = NULL;

	stashTableDestroy(zmap->world_cell_data.interaction_to_costume_hash);
	zmap->world_cell_data.interaction_to_costume_hash = NULL;

	zmap->world_cell_data.interaction_costume_idx = 0;

	if (zmap->world_cell_data.interaction_node_hash)
	{
		assert(stashGetCount(zmap->world_cell_data.interaction_node_hash) == 0);
		stashTableDestroy(zmap->world_cell_data.interaction_node_hash);
		zmap->world_cell_data.interaction_node_hash = NULL;
	}
}

WorldInteractionCostume *createInteractionCostumeFromParsed(ZoneMap *zmap, WorldInteractionCostumeParsed *costume_parsed, int idx)
{
	WorldInteractionCostume *costume;
	WorldStreamingPooledInfo *streaming_pooled_info = zmap->world_cell_data.streaming_pooled_info;
	int i;

	costume = allocInteractionCostume(	zmap, idx, 
										worldGetStreamedMatrix(streaming_pooled_info, costume_parsed->hand_pivot_idx), 
										worldGetStreamedMatrix(streaming_pooled_info, costume_parsed->mass_pivot_idx), 
										worldGetStreamedString(streaming_pooled_info, costume_parsed->carry_anim_bit_string_idx));

	for (i = 0; i < eaSize(&costume_parsed->costume_parts); ++i)
	{
		WorldInteractionCostumePartParsed *part_parsed = costume_parsed->costume_parts[i];
		WorldInteractionCostumePart *part = calloc(1, sizeof(WorldInteractionCostumePart));

		part->collision = part_parsed->collision;
		part->model = groupModelFind(worldGetStreamedString(streaming_pooled_info, part_parsed->model_idx), 0);
		copyVec4(part_parsed->tint_color, part->tint_color);

		part->draw_list = (part_parsed->draw_list_idx < 0) ? NULL : streaming_pooled_info->drawable_lists[part_parsed->draw_list_idx];
		addDrawableListRef(part->draw_list MEM_DBG_PARMS_INIT);

		part->instance_param_list = (part_parsed->instance_param_list_idx < 0) ? NULL : streaming_pooled_info->instance_param_lists[part_parsed->instance_param_list_idx];
		addInstanceParamListRef(part->instance_param_list MEM_DBG_PARMS_INIT);

		if (!sameVec3(part_parsed->scale, unitvec3))
		{
			Mat4 scale_matrix;
			setVec3(scale_matrix[0], part_parsed->scale[0], 0, 0);
			setVec3(scale_matrix[1], 0, part_parsed->scale[1], 0);
			setVec3(scale_matrix[2], 0, 0, part_parsed->scale[2]);
			setVec3(scale_matrix[3], 0, 0, 0);
			mulMat4Inline(worldGetStreamedMatrix(streaming_pooled_info, part_parsed->matrix_idx), scale_matrix, part->matrix);
		}
		else
		{
			copyMat4(worldGetStreamedMatrix(streaming_pooled_info, part_parsed->matrix_idx), part->matrix);
		}

		eaPush(&costume->costume_parts, part);
	}

	costume = lookupInteractionCostume(zmap, costume, 0 MEM_DBG_PARMS_INIT);

	for (i = 0; i < eaiSize(&costume_parsed->interaction_uids); ++i)
		assert(stashIntAddPointer(zmap->world_cell_data.interaction_to_costume_hash, costume_parsed->interaction_uids[i], costume, false));

	return costume;
}

__forceinline static F32 quantizeF32(F32 val, F32 scale, F32 inv_scale)
{
	if (val < 0)
		val = ceilf(val * scale) * inv_scale;
	else
		val = floorf(val * scale) * inv_scale;
	if (val == -0)
		val = 0;
	return val;
}

static void quantizeMat4(Mat4 matrix, F32 ang_scale, F32 ang_inv_scale, F32 pos_scale, F32 inv_pos_scale)
{
	Vec3 mat_scale, mat_pyr;
	int i;

	extractScale(matrix, mat_scale);
	getMat3YPR(matrix, mat_pyr);

	for (i = 0; i < 3; ++i)
		mat_pyr[i] = quantizeF32(mat_pyr[i], ang_scale, ang_inv_scale);

	for (i = 0; i < 3; ++i)
		mat_scale[i] = round(mat_scale[i] * 10.f) * 0.1f;

	createMat3YPR(matrix, mat_pyr);
	scaleMat3Vec3(matrix, mat_scale);

	for (i = 0; i < 3; ++i)
		matrix[3][i] = quantizeF32(matrix[3][i], pos_scale, inv_pos_scale);
}

static WorldInteractionCostume *createInteractionCostume(SA_PARAM_NN_VALID ZoneMap *zmap, WorldInteractionEntry *entry)
{
	WorldInteractionCostume *costume = allocInteractionCostume(zmap, -1, unitmat, unitmat, NULL);
	Mat4 inv_world_mat;
	bool has_hand_pivot = false, has_mass_pivot = false;
	Vec3 center_of_mass = {0,0,0};
	F32 total_mass = 0;
	int i;

	invertMat4Copy(entry->base_entry.bounds.world_matrix, inv_world_mat);

	for (i = 0; i < eaSize(&entry->child_entries); ++i)
	{
		WorldCellEntry *child = entry->child_entries[i];
		WorldCellEntryData *child_data = worldCellEntryGetData(child);

		if (child_data->interaction_child_idx >=0 && entry->initial_selected_child >= 0 && child_data->interaction_child_idx != entry->initial_selected_child) {
			continue;
		}

		if (child->type == WCENT_MODEL)
		{
			WorldModelEntry *model_entry = (WorldModelEntry *)child;

			if (model_entry->base_drawable_entry.draw_list && !model_entry->base_drawable_entry.editor_only)
			{
				WorldInteractionCostumePart *part = calloc(1, sizeof(WorldInteractionCostumePart));

				part->draw_list = model_entry->base_drawable_entry.draw_list;
				addDrawableListRef(part->draw_list MEM_DBG_PARMS_INIT);

				part->instance_param_list = model_entry->base_drawable_entry.instance_param_list;
				addInstanceParamListRef(part->instance_param_list MEM_DBG_PARMS_INIT);

				part->collision = false;
				part->model = worldDrawableEntryGetModel(&model_entry->base_drawable_entry, NULL, NULL, NULL, NULL);
				copyVec4(model_entry->base_drawable_entry.color, part->tint_color);
				mulMat4Inline(inv_world_mat, child->bounds.world_matrix, part->matrix);
				if (model_entry->scaled)
					scaleMat3Vec3(part->matrix, model_entry->model_scale);
				quantizeMat4(part->matrix, 100.f, 0.01f, 1000.f, 0.001f);

				eaPush(&costume->costume_parts, part);
			}
		}
		else if (child->type == WCENT_COLLISION)
		{
			WorldCollisionEntry *coll_entry = (WorldCollisionEntry *)child;

			if (coll_entry->model && coll_entry->filter.shapeGroup == WC_SHAPEGROUP_WORLD_BASIC)
			{
				WorldInteractionCostumePart *part = calloc(1, sizeof(WorldInteractionCostumePart));
				Vec3 mid;
				F32 mass;

				part->collision = true;
				part->model = coll_entry->model;
				mulMat4Inline(inv_world_mat, child->bounds.world_matrix, part->matrix);
				scaleMat3Vec3(part->matrix, coll_entry->scale);
				quantizeMat4(part->matrix, 100.f, 0.01f, 100.f, 0.01f);

				mulVecMat4(child->bounds.world_mid, inv_world_mat, mid);
				mass = boxCalcSize(child->shared_bounds->local_min, child->shared_bounds->local_max);
				scaleAddVec3(mid, mass, center_of_mass, center_of_mass);
				total_mass += mass;

				eaPush(&costume->costume_parts, part);
			}
		}
		else if (child->type == WCENT_ALTPIVOT)
		{
			WorldAltPivotEntry *ap_entry = (WorldAltPivotEntry *)child;
			Mat4 temp_mat;

			if (ap_entry->hand_pivot && !has_hand_pivot)
			{
				has_hand_pivot = true;
				mulMat4Inline(inv_world_mat, child->bounds.world_matrix, temp_mat);
				invertMat4Copy(temp_mat, costume->hand_pivot);
				quantizeMat4(costume->hand_pivot, 10.f, 0.1f, 100.f, 0.01f);
			}

			if (ap_entry->mass_pivot && !has_mass_pivot)
			{
				has_mass_pivot = true;
				mulMat4Inline(inv_world_mat, child->bounds.world_matrix, temp_mat);
				invertMat4Copy(temp_mat, costume->mass_pivot);
				quantizeMat4(costume->mass_pivot, 10.f, 0.1f, 100.f, 0.01f);
			}

			if (ap_entry->carry_anim_bit && !costume->carry_anim_bit)
				costume->carry_anim_bit = ap_entry->carry_anim_bit;
		}
	}

	if (!has_mass_pivot && total_mass > 0)
	{
		// auto-calculate center of mass
		scaleVec3(center_of_mass, -1.f / total_mass, costume->mass_pivot[3]);
		quantizeMat4(costume->mass_pivot, 10.f, 0.1f, 100.f, 0.01f);
	}

	return lookupInteractionCostume(zmap, costume, entry->uid MEM_DBG_PARMS_INIT);
}

const char *worldInteractionGetCarryAnimationBitName(const char *interaction_name)
{
	WorldInteractionCostume *interaction_costume = NULL;
	int interaction_uid;
	ZoneMap *zmap;

	if (!wlInteractionUidFromName(interaction_name, &zmap, &interaction_uid))
		return NULL;

	stashIntFindPointer(zmap->world_cell_data.interaction_to_costume_hash, interaction_uid, &interaction_costume);

	return SAFE_MEMBER(interaction_costume, carry_anim_bit);
}

WLCostume *worldInteractionGetWLCostume(const char *interaction_name_in)
{
	WorldInteractionCostume *interaction_costume = NULL;
	char interaction_name[512], costume_name[512];
	bool hand_pivot, mass_pivot;
	WLCostume *costume;
	int i, interaction_uid;
	ZoneMap *zmap;

	if (!interaction_name_in)
		return NULL;

	PERFINFO_AUTO_START_FUNC();

	strcpy(interaction_name, interaction_name_in);
	hand_pivot = strEndsWith(interaction_name, " HandPivot");
	mass_pivot = strEndsWith(interaction_name, " MassPivot");

	if (hand_pivot || mass_pivot)
		interaction_name[strlen(interaction_name) - strlen(" HandPivot")] = 0;

	if (!wlInteractionUidFromName(interaction_name, &zmap, &interaction_uid))
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	stashIntFindPointer(zmap->world_cell_data.interaction_to_costume_hash, interaction_uid, &interaction_costume);

	if (!interaction_costume)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	sprintf(costume_name, INTERACTION_COSTUME_FORMAT, interaction_costume->name_id);
	if (hand_pivot)
		strcat(costume_name, " HandPivot");
	else if (mass_pivot)
		strcat(costume_name, " MassPivot");

	if (costume = wlCostumeFromName(costume_name))
	{
		PERFINFO_AUTO_STOP();
		return costume;
	}

	costume = StructAlloc(parse_WLCostume);
	costume->pcName = allocAddString(costume_name);
	costume->bWorldLighting = !wl_state.interactibles_use_character_light;
	costume->bCollision = true;
	costume->bNoAutoCleanup = true;
	costume->bComplete = true;

	if (!SET_HANDLE_FROM_STRING("SkelInfo", pcDefaultSkeleton, costume->hSkelInfo))
	{
		Errorf("Unable to find default skeleton for autogenerated interaction costume. It should be named %s", pcDefaultSkeleton);
		StructDestroySafe(parse_WLCostume, &costume);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	for (i = 0; i < eaSize(&interaction_costume->costume_parts); ++i)
	{
		WorldInteractionCostumePart *interaction_part = interaction_costume->costume_parts[i];
		WLCostumePart *costume_part = StructAlloc(parse_WLCostumePart);

		if (interaction_part->model)
		{
			costume_part->pchGeometry = interaction_part->model->header->filename;
			costume_part->pcModel = allocAddString(interaction_part->model->name);
		}
		costume_part->pchBoneName = allocAddString("Root");

		if (!sameVec4(interaction_part->tint_color, unitvec4))
		{
			MaterialNamedConstant *mnc = StructAlloc(parse_MaterialNamedConstant);
			mnc->name = allocAddString("Color0");
			copyVec4(interaction_part->tint_color, mnc->value);
			eaPush(&costume_part->eaMatConstant, mnc);
		}

		if (hand_pivot)
			mulMat4(interaction_costume->hand_pivot, interaction_part->matrix, costume_part->mTransform);
		else if (mass_pivot)
			mulMat4(interaction_costume->hand_pivot, interaction_part->matrix, costume_part->mTransform);
		else
			copyMat4(interaction_part->matrix, costume_part->mTransform);

		costume_part->bCollisionOnly = interaction_part->collision;

		costume_part->pWorldDrawableList = interaction_part->draw_list;
		addDrawableListRef(costume_part->pWorldDrawableList MEM_DBG_PARMS_INIT);

		costume_part->pInstanceParamList = interaction_part->instance_param_list;
		addInstanceParamListRef(costume_part->pInstanceParamList MEM_DBG_PARMS_INIT);

		costume_part->uiRequiredLOD = 4;

		eaPush(&costume->eaCostumeParts, costume_part);

	}

	wlCostumeAddToDictionary(costume, costume->pcName);

	PERFINFO_AUTO_STOP();

	return costume;
}

//////////////////////////////////////////////////////////////////////////

void worldCellEntrySetDisabled(int iPartitionIdx, WorldCellEntry *entry, bool disabled)
{
	WorldCellEntryData *data = NULL;
	WorldCellCullHeader *cell_header;
	WorldCell *cell;

	if (disabled == worldCellEntryIsPartitionDisabled(entry, iPartitionIdx))
		return;

	if (!data)
		data = worldCellEntryGetData(entry);

	cell = data->cell;

	if (disabled)
	{
		worldCellEntrySetPartitionDisabled(entry, iPartitionIdx, true);
		if (cell)
			worldCellEntryClose(iPartitionIdx, entry);
	}
	else
	{
		worldCellEntrySetPartitionDisabled(entry, iPartitionIdx, false);
		if (cell && cell->cell_state == WCS_OPEN)
			worldCellEntryOpen(iPartitionIdx, entry, cell->region);
	}

	// push to the hot data header array, if it exists
	cell_header = worldCellGetHeader(cell);
	if (cell_header && entry->type > WCENT_BEGIN_DRAWABLES)
	{
		WorldDrawableEntry *draw = (WorldDrawableEntry*)entry;
		WorldCellEntryCullHeader *headers;
		int i, header_count;

		if (draw->editor_only)
		{
			header_count = CULL_HEADER_COUNT(cell_header->editor_only_entry_headers);
			headers = CULL_HEADER_START(cell_header->editor_only_entry_headers);
		}
		else
		{
			header_count = CULL_HEADER_COUNT(cell_header->entry_headers);
			headers = CULL_HEADER_START(cell_header->entry_headers);
		}

		for (i = 0; i < header_count; ++i)
		{
			if (ENTRY_HEADER_ENTRY_PTR(&headers[i]) == draw)
				worldCellEntryHeaderSetActive(&headers[i], worldCellEntryIsPartitionOpen(entry, iPartitionIdx) && !worldCellEntryIsPartitionDisabled(entry, iPartitionIdx));
		}
	}

}

void worldCellEntrySetDisabledAll(WorldCellEntry *entry, bool disabled)
{
	int i;
	for(i=eaSize(&world_grid.eaWorldColls)-1; i>=0; --i) {
		if (world_grid.eaWorldColls[i]) {
			worldCellEntrySetDisabled(i, entry, disabled);
		}
	}
}

void worldInteractionEntryDisableFX(WorldInteractionEntry *entry)
{
	int i;
	for (i = eaSize(&entry->child_entries) - 1; i >= 0; --i)
	{
		if (entry->child_entries[i]->type == WCENT_INTERACTION)
		{
			worldInteractionEntryDisableFX((WorldInteractionEntry *)entry->child_entries[i]);
		}
		else if (entry->child_entries[i]->type == WCENT_FX)
		{
			WorldFXEntry * fx_entry = (WorldFXEntry *)entry->child_entries[i];
			if (fx_entry->interaction_node_owned && fx_entry->fx_manager)
			{
				dtFxClearWorldModel(fx_entry->fx_manager);
			}
		}
	}
}

void worldInteractionEntrySetDisabled(int iPartitionIdx, WorldInteractionEntry *entry, bool disabled)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	worldCellEntrySetDisabled(iPartitionIdx, &entry->base_entry, disabled);

	for (i = eaSize(&entry->child_entries) - 1; i >= 0; --i)
	{
		WorldCellEntry *child_entry = entry->child_entries[i];


		worldCellEntrySetDisabled(iPartitionIdx, child_entry, disabled);
	}

	// Turn off extra FX.
	{
		// Find the parent interact node
 		WorldInteractionEntry *pCurrentEntry = entry;
 		while(pCurrentEntry->base_entry_data.parent_entry)
		{
			pCurrentEntry = pCurrentEntry->base_entry_data.parent_entry;
		}

		// now turn off all the interact FX for that node and its children
		worldInteractionEntryDisableFX(pCurrentEntry);
	}

	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////

WorldInteractionEntry *createWorldInteractionEntry(GroupDef **def_chain, int *idxs_in_parent, GroupDef *def, const GroupInfo *info)
{
	WorldInteractionEntry *entry = calloc(1, sizeof(WorldInteractionEntry));
	WorldCellEntryBounds *bounds = &entry->base_entry.bounds;
	GroupChild **def_children = groupGetChildren(def);
	F32 near_lod_near_dist, far_lod_near_dist, far_lod_far_dist;
	char *name;

	entry->base_entry.type = WCENT_INTERACTION;

	entry->parent_def = def;

	name = handleStringFromDefChain(def_chain, idxs_in_parent); // this function allocates the string
	if (name && info->layer->dummy_layer)
	{
		char new_filename[512];
		ANALYSIS_ASSUME(name);
		sprintf(new_filename, "\"%s\":%s", layerGetFilename(info->layer), name);
		free(name);
		name = strdup(new_filename);
	}

	entry->initial_selected_child = -1;
	if (def->property_structs.physical_properties.bIsChildSelect)
	{
		entry->initial_selected_child = def->property_structs.physical_properties.iChildSelectIdx;
		entry->visible_child_count = eaSize(&def_children);
	}

	groupGetDrawVisDistRecursive(def, info->ignore_lod_override ? NULL : info->lod_override, info->lod_scale, &near_lod_near_dist, &far_lod_near_dist, &far_lod_far_dist);

	if (info->spline)
		entry->base_entry.shared_bounds = createSharedBounds(info->zmap, def->model, info->world_min, info->world_max, info->radius, 0, far_lod_far_dist, far_lod_far_dist);
	else
		entry->base_entry.shared_bounds = createSharedBounds(info->zmap, def->model, def->bounds.min, def->bounds.max, info->radius, 0, far_lod_far_dist, far_lod_far_dist);

	copyVec3(info->world_mid, bounds->world_mid);
	copyMat4(info->world_matrix, bounds->world_matrix);

	if (def->property_structs.interaction_properties)
	{
		if (wlIsServer())
		{
			entry->full_interaction_properties = StructClone(parse_WorldInteractionProperties, def->property_structs.interaction_properties);
			assert(entry->full_interaction_properties);
		}

		entry->base_interaction_properties = wlInteractionCreateBaseProperties(def->property_structs.interaction_properties);
		assert(entry->base_interaction_properties);
	}

	if (name)
	{
		if (!info->zmap->world_cell_data.interaction_node_hash)
			info->zmap->world_cell_data.interaction_node_hash = stashTableCreateInt(1024);

		strupr(name);
	
		entry->uid = burtlehash(name, (U32)strlen(name), 0);
		while (!entry->uid || !stashIntAddPointer(info->zmap->world_cell_data.interaction_node_hash, entry->uid, entry, false))
			entry->uid++;

		wlInteractionAddToDictionary(info->zmap, entry);

		entry->costume = createInteractionCostume(info->zmap, entry);

		SAFE_FREE(name);
	}


	return entry;
}

WorldCellEntry *createWorldInteractionEntryFromServerParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldInteractionEntryServerParsed *entry_parsed, bool streaming_mode, bool parsed_will_be_freed)
{
	WorldInteractionEntry *entry = calloc(1, sizeof(WorldInteractionEntry));

	entry->base_entry.type = WCENT_INTERACTION;

	worldEntryInitBoundsFromParsed(streaming_pooled_info, &entry->base_entry, &entry_parsed->base_data);

	if (parsed_will_be_freed)
	{
		entry->full_interaction_properties = entry_parsed->full_interaction_properties; // transfer ownership
		entry_parsed->full_interaction_properties = NULL;
	}
	else
	{
		entry->full_interaction_properties = StructClone(parse_WorldInteractionProperties, entry_parsed->full_interaction_properties);
	}

	entry->base_interaction_properties = wlInteractionCreateBaseProperties(entry->full_interaction_properties);

	entry->uid = entry_parsed->uid;
	entry->visible_child_count = entry_parsed->visible_child_count;
	entry->initial_selected_child = entry_parsed->initial_selected_child;

	return &entry->base_entry;
}

WorldCellEntry *createWorldInteractionEntryFromClientParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldInteractionEntryClientParsed *entry_parsed, bool streaming_mode, bool parsed_will_be_freed)
{
	WorldInteractionEntry *entry = calloc(1, sizeof(WorldInteractionEntry));

	entry->base_entry.type = WCENT_INTERACTION;

	worldEntryInitBoundsFromParsed(streaming_pooled_info, &entry->base_entry, &entry_parsed->base_data);

	if (parsed_will_be_freed)
	{
		entry->base_interaction_properties = entry_parsed->base_interaction_properties; // transfer ownership
		entry_parsed->base_interaction_properties = NULL;
	}
	else
	{
		entry->base_interaction_properties = StructClone(parse_WorldBaseInteractionProperties, entry_parsed->base_interaction_properties);
	}

	entry->uid = entry_parsed->uid;
	entry->visible_child_count = entry_parsed->visible_child_count;
	entry->initial_selected_child = entry_parsed->initial_selected_child;

	return &entry->base_entry;
}

void addWorldInteractionEntryPostParse(ZoneMap *zmap, WorldInteractionEntry *entry, int interactable_id)
{
	WorldStreamingInfo *streaming_info = zmap->world_cell_data.streaming_info;

	if (entry->uid)
	{
		if (!zmap->world_cell_data.interaction_node_hash)
			zmap->world_cell_data.interaction_node_hash = stashTableCreateInt(1024);
		assert(stashIntAddPointer(zmap->world_cell_data.interaction_node_hash, entry->uid, entry, false));
		wlInteractionAddToDictionary(zmap, entry);
	}

	// fixup pointer from interactable to this entry
	if (streaming_info && streaming_info->id_to_interactable)
	{
		WorldNamedInteractable *interactable;
		if (stashIntFindPointer(streaming_info->id_to_interactable, interactable_id, &interactable))
			interactable->entry = entry;
	}
}

static void clearParentEntry(WorldCellEntry *entry)
{
	WorldCellEntryData *entry_data;

	if (!entry)
		return;

	entry_data = worldCellEntryGetData(entry);
	entry_data->parent_entry = NULL;
}

void freeWorldInteractionEntryData(ZoneMap *zmap, WorldInteractionEntry *ent)
{
	if (ent->uid && ent->hasInteractionNode)
		wlInteractionCloseForEntry(ent);
	eaDestroyEx(&ent->child_entries, clearParentEntry);
	StructDestroySafe(parse_WorldBaseInteractionProperties, &ent->base_interaction_properties);
	StructDestroySafe(parse_WorldInteractionProperties, &ent->full_interaction_properties);
	if (ent->costume)
	{
		removeInteractionCostumeRef(ent->costume, ent->uid);
		ent->costume = NULL;
	}
	if (ent->uid)
	{
		stashIntRemovePointer(zmap->world_cell_data.interaction_node_hash, ent->uid, NULL);
		RefSystem_RemoveReferent(ent, false); // This is removed here because it may have never been opened
	}
}

// validate children are in child cells
bool validateInteractionChildren(WorldInteractionEntry *parent, int *fail_index)
{
	int i;
	U8 root_level = 0;
	if (parent->base_entry_data.cell->region->root_world_cell)
	{
		root_level = parent->base_entry_data.cell->region->root_world_cell->vis_dist_level;
		assert(parent->base_entry_data.cell->vis_dist_level <= root_level);
	}
	for (i = 0; i < eaSize(&parent->child_entries); ++i)
	{
		WorldCellEntryData *child_data = worldCellEntryGetData(parent->child_entries[i]);
		WorldCell *parent_cell;

		if (parent->base_entry_data.cell->region->root_world_cell)
			assert(child_data->cell->vis_dist_level <= root_level);
		if (child_data->cell->vis_dist_level > parent->base_entry_data.cell->vis_dist_level)
		{
			if (fail_index)
				*fail_index = i;
			return false;
		}

		for (parent_cell = child_data->cell; parent_cell; parent_cell = parent_cell->parent)
		{
			if (parent_cell == parent->base_entry_data.cell)
				break;
		}
		if (!parent_cell)
		{
			if (fail_index)
				*fail_index = i;
			return false;
		}
	}

	return true;
}

void addChildToInteractionEntry(WorldCellEntry *child, WorldInteractionEntry *parent, int interaction_child_idx)
{
	WorldCellEntryData *entry_data = worldCellEntryGetData(child);

	assert(child != &parent->base_entry);

	entry_data->parent_entry = parent;
	entry_data->interaction_child_idx = interaction_child_idx;
	if (parent)
	{
		ZoneMap *zmap = parent->base_entry_data.cell->region->zmap_parent;
		bool disabled = false;

		// If the region is a temp region, it's not associated with
		// any zone map -- try using the loaded map
		if( !zmap ) {
			zmap = worldGetActiveMap();
		}

		eaPushUnique(&parent->child_entries, child);

		// recreate dummy costume
		if (!parent->base_entry.streaming_mode)
		{
			removeInteractionCostumeRef(parent->costume, parent->uid);
			parent->costume = createInteractionCostume(zmap, parent);
		}

 		for (; parent; parent = parent->base_entry_data.parent_entry)
 			disabled = disabled || worldCellEntryIsAnyPartitionDisabled(&parent->base_entry);
 
 		worldCellEntrySetDisabledAll(child, disabled);
	}
}

//////////////////////////////////////////////////////////////////////////

WorldInteractionEntry *worldGetInteractionEntryFromUid(ZoneMap *zmap, int uid, bool closed_ok)
{
	WorldInteractionEntry *entry = NULL;
	if (!zmap->world_cell_data.interaction_node_hash || !uid)
		return NULL;
	if (stashIntFindPointer(zmap->world_cell_data.interaction_node_hash, uid, &entry))
	{
		if (closed_ok || worldCellEntryIsAnyPartitionOpen(&entry->base_entry))
			return entry;
	}
	return NULL;
}

bool worldInteractionEntryIsTraversable(WorldInteractionEntry *entry)
{
	if (wl_state.is_traversable_func)
		return wl_state.is_traversable_func(entry);
	return false;
}
