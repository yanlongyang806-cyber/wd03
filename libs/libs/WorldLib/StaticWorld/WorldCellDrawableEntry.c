/***************************************************************************



***************************************************************************/

#include "WorldCellEntryPrivate.h"
#include "WorldCellStreamingPrivate.h"
#include "wlModelInline.h"
#include "wlState.h"
#include "wlAutoLOD.h"
#include "dynFxInterface.h"
#include "dynThread.h"

#include "HashFunctions.h"
#include "DebugState.h"
#include "qsortG.h"
#include "WorldCellEntry_h_ast.h"
#include "memlog.h"

MemLog modelDrawLog = {0};

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#define TERRAIN_VIS_DIST (10000)
#define MAX_DEBUG_MATERIAL_DRAW_LIST_SIZE 3000
#define MAX_DEBUG_WORLD_DRAW_LIST_SIZE 1000

bool gbDebugMaterialDraw = false;
AUTO_CMD_INT(gbDebugMaterialDraw, DebugLogMaterialDraw) ACMD_CMDLINE;

bool gbDebugModelDraw = false;
AUTO_CMD_INT(gbDebugModelDraw, DebugLogModelDraw) ACMD_CMDLINE;

bool gbDebugDrawableSubObjects = false;
AUTO_CMD_INT(gbDebugDrawableSubObjects, DebugLogDrawableSubObjects) ACMD_CMDLINE;

bool gbDebugDrawableLists = false;
AUTO_CMD_INT(gbDebugDrawableLists, DebugLogDrawableLists) ACMD_CMDLINE;

bool gbDebugInstanceParamLists = false;
AUTO_CMD_INT(gbDebugInstanceParamLists, DebugLogInstanceParamLists) ACMD_CMDLINE;

static char* s_estrDebugDrawError = NULL;
static WorldDrawDebugLogs* s_pWorldDebugLogs = NULL;

static void DebugGetDrawLogString(WorldDrawDebugLogEntry** eaEntries, int iCurIndex, const char* pchLogType, char** pestrOut)
{
	int i, start, s = eaSize(&eaEntries);
	int leaked = 0;
	U32* puiIDs = NULL;
	U32* puiLeakedCounts = NULL;
	
	estrConcatf(pestrOut, "%s Log Size: %d\n", pchLogType, s);

	i = start = (iCurIndex >= s ? 0 : iCurIndex);
	while (i < s)
	{
		WorldDrawDebugLogEntry *debug = eaEntries[i];
		estrConcatf(pestrOut, "%s[%d] %s %s %u, refcount is now %d\n", 
			debug->caller_fname, debug->caller_line,
			debug->removed ? "REMOVED" : "ADDED", 
			pchLogType,
			debug->id, 
			debug->ref_count);
		i = SEQ_NEXT(i, 0, s);
		if (i == start)
			break;
	}
	i = start = SEQ_PREV(iCurIndex, 0, s);
	while (i >= 0)
	{
		WorldDrawDebugLogEntry *debug = eaEntries[i];
		int idx = (int)eaiBFind(puiIDs, debug->id);
		
		if (idx == eaiSize(&puiIDs) || puiIDs[idx] != debug->id)
		{
			eaiInsert(&puiIDs, debug->id, idx);
			eaiInsert(&puiLeakedCounts, debug->ref_count, idx);

			if (debug->ref_count > 0)
			{
				leaked++;
			}
		}
		i = SEQ_PREV(i, 0, s);
		if (i == start)
			break;
	}

	estrConcatf(pestrOut, "\nLeaked %s: %d\n", pchLogType, leaked);
	if (leaked > 0)
	{
		for (i = 0; i < eaiSize(&puiIDs); i++)
		{
			if (puiLeakedCounts[i] > 0)
			{
				estrConcatf(pestrOut, "%s %u has refcount %d\n", pchLogType, puiIDs[i], puiLeakedCounts[i]);
			}
		}
	}
	eaiDestroy(&puiIDs);
	eaiDestroy(&puiLeakedCounts);
	estrConcatf(pestrOut, "\n");
}

static void DebugGetWorldDrawLogStringAll(char** pestrOut)
{
	if (s_pWorldDebugLogs)
	{
		DebugGetDrawLogString(s_pWorldDebugLogs->eaMaterialDraws, s_pWorldDebugLogs->iCurMatDrawIndex, "MaterialDraw", pestrOut);
		DebugGetDrawLogString(s_pWorldDebugLogs->eaModelDraws, s_pWorldDebugLogs->iCurModelDrawIndex, "ModelDraw", pestrOut);
		DebugGetDrawLogString(s_pWorldDebugLogs->eaSubObjects, s_pWorldDebugLogs->iCurSubObjIndex, "DrawableSubObject", pestrOut);
		DebugGetDrawLogString(s_pWorldDebugLogs->eaDrawLists, s_pWorldDebugLogs->iCurDrawListIndex, "DrawableList", pestrOut);
		DebugGetDrawLogString(s_pWorldDebugLogs->eaInstanceParamLists, s_pWorldDebugLogs->iCurInstParamListIndex, "InstanceParamList", pestrOut);
	}
}

AUTO_COMMAND;
void DebugPrintWorldDrawLogsToFile(const char* pchFilename)
{
	if (s_pWorldDebugLogs) 
	{
		FILE* log = fopen(pchFilename, "w");
		char* estrLog = NULL;
		estrStackCreate(&estrLog);
		DebugGetWorldDrawLogStringAll(&estrLog);
		fprintf(log, "%s", estrLog);
		estrDestroy(&estrLog);
		fclose(log);
	}
}

static U32 hashMaterialDraw(const NOCONST(MaterialDraw) *draw_const, int hashSeed)
{
	int i;
	NOCONST(MaterialDraw)* draw = (NOCONST(MaterialDraw)*)draw_const;
	if (draw->hash == 0)
	{
		U32 hash = burtlehash((void *)&draw->has_swaps, sizeof(draw->has_swaps), hashSeed);
		draw->hash_debug[0] = hash;
		hash = burtlehash2((void *)&draw->material, sizeof(Material *) / sizeof(U32), hash);
		draw->hash_debug[1] = hash;
		if (draw->tex_count)
		{
			hash = burtlehash2((void *)draw->textures, draw->tex_count * sizeof(BasicTexture *) / sizeof(U32), hash);
			draw->hash_debug[2] = hash;
		}
		else
		{
			draw->hash_debug[2] = 0;
		}
		if (draw->const_count)
		{
			hash = burtlehash2((void *)draw->constants, draw->const_count * sizeof(Vec4) / sizeof(U32), hash);
			draw->hash_debug[3] = hash;
		}
		else
		{
			draw->hash_debug[3] = 0;
		}
		for (i = 0; i < draw->const_mapping_count; ++i)
		{
			hash = burtlehash2((void *)&draw->constant_mappings[i].data_type, sizeof(draw->constant_mappings[i].data_type) / sizeof(U32), hash);
			hash = burtlehash2((void *)&draw->constant_mappings[i].constant_index, sizeof(draw->constant_mappings[i].constant_index) / sizeof(U32), hash);
			hash = burtlehash2((void *)&draw->constant_mappings[i].constant_subindex, sizeof(draw->constant_mappings[i].constant_subindex) / sizeof(U32), hash);
			hash = burtlehash2((void *)draw->constant_mappings[i].values, MAX_MATERIAL_CONSTANT_MAPPING_VALUES * sizeof(F32) / sizeof(U32), hash);
		}
		draw->hash = hash;
	}
	return draw->hash;
}

static int cmpMaterialDraw(const MaterialDraw *draw1, const MaterialDraw *draw2)
{
	intptr_t pt;
	int i;

	pt = (int)draw1->has_swaps - (int)draw2->has_swaps;
	if (pt)
		return SIGN(pt);

	pt = (intptr_t)draw1->material - (intptr_t)draw2->material;
	if (pt)
		return SIGN(pt);

	pt = (int)draw1->tex_count - (int)draw2->tex_count;
	if (pt)
		return SIGN(pt);

	pt = (int)draw1->const_count - (int)draw2->const_count;
	if (pt)
		return SIGN(pt);

	pt = (int)draw1->const_mapping_count - (int)draw2->const_mapping_count;
	if (pt)
		return SIGN(pt);

	if (draw1->tex_count)
	{
		pt = memcmp(draw1->textures, draw2->textures, draw1->tex_count * sizeof(BasicTexture *));
		if (pt)
			return SIGN(pt);
	}

	if (draw1->const_count)
	{
		pt = memcmp(draw1->constants, draw2->constants, draw1->const_count * sizeof(Vec4));
		if (pt)
			return SIGN(pt);
	}

	for (i = 0; i < draw1->const_mapping_count; ++i)
	{
		pt = (int)draw1->constant_mappings[i].data_type - (int)draw2->constant_mappings[i].data_type;
		if (pt)
			return SIGN(pt);

		pt = (int)draw1->constant_mappings[i].constant_index - (int)draw2->constant_mappings[i].constant_index;
		if (pt)
			return SIGN(pt);

		pt = (int)draw1->constant_mappings[i].constant_subindex - (int)draw2->constant_mappings[i].constant_subindex;
		if (pt)
			return SIGN(pt);

		pt = memcmp(draw1->constant_mappings[i].values, draw2->constant_mappings[i].values, MAX_MATERIAL_CONSTANT_MAPPING_VALUES * sizeof(F32));
		if (pt)
			return SIGN(pt);
	}

	return 0;
}

static int cmpMaterialDrawUID(const MaterialDraw **pdraw1, const MaterialDraw **pdraw2)
{
	return (int)(*pdraw1)->uid - (int)(*pdraw2)->uid;
}

static void createMaterialDrawDebugLogEntry(NOCONST(MaterialDraw) *draw, bool removed MEM_DBG_PARMS)
{
	WorldDrawDebugLogEntry* debug;

	if (!s_pWorldDebugLogs)
		s_pWorldDebugLogs = (WorldDrawDebugLogs*)calloc(1, sizeof(WorldDrawDebugLogs));

	debug = eaGet(&s_pWorldDebugLogs->eaMaterialDraws, s_pWorldDebugLogs->iCurMatDrawIndex);

	if (!debug)
	{
		debug = calloc(1, sizeof(WorldDrawDebugLogEntry));
		eaPush(&s_pWorldDebugLogs->eaMaterialDraws, debug);
	}
	debug->caller_fname = caller_fname;
	debug->caller_line = line;
	debug->ref_count = draw->ref_count;
	debug->id = draw->uid;
	debug->removed = !!removed;
	
	s_pWorldDebugLogs->iCurMatDrawIndex = SEQ_NEXT(s_pWorldDebugLogs->iCurMatDrawIndex, 0, MAX_DEBUG_MATERIAL_DRAW_LIST_SIZE);
}

static void createModelDrawDebugLogEntry(NOCONST(ModelDraw) *draw, bool removed MEM_DBG_PARMS)
{
	WorldDrawDebugLogEntry* debug;

	if (!s_pWorldDebugLogs)
		s_pWorldDebugLogs = (WorldDrawDebugLogs*)calloc(1, sizeof(WorldDrawDebugLogs));

	debug = eaGet(&s_pWorldDebugLogs->eaModelDraws, s_pWorldDebugLogs->iCurModelDrawIndex);

	if (!debug)
	{
		debug = calloc(1, sizeof(WorldDrawDebugLogEntry));
		eaPush(&s_pWorldDebugLogs->eaModelDraws, debug);
	}
	debug->caller_fname = caller_fname;
	debug->caller_line = line;
	debug->ref_count = draw->ref_count;
	debug->id = draw->uid;
	debug->removed = !!removed;
	
	s_pWorldDebugLogs->iCurModelDrawIndex = SEQ_NEXT(s_pWorldDebugLogs->iCurModelDrawIndex, 0, MAX_DEBUG_WORLD_DRAW_LIST_SIZE);
}

static void createDrawableSubObjectDebugLogEntry(NOCONST(WorldDrawableSubobject) *subobj, bool removed MEM_DBG_PARMS)
{
	WorldDrawDebugLogEntry* debug;

	if (!s_pWorldDebugLogs)
		s_pWorldDebugLogs = (WorldDrawDebugLogs*)calloc(1, sizeof(WorldDrawDebugLogs));

	debug = eaGet(&s_pWorldDebugLogs->eaSubObjects, s_pWorldDebugLogs->iCurSubObjIndex);

	if (!debug)
	{
		debug = calloc(1, sizeof(WorldDrawDebugLogEntry));
		eaPush(&s_pWorldDebugLogs->eaSubObjects, debug);
	}
	debug->caller_fname = caller_fname;
	debug->caller_line = line;
	debug->ref_count = subobj->ref_count;
	debug->id = subobj->uid;
	debug->removed = !!removed;
	
	s_pWorldDebugLogs->iCurSubObjIndex = SEQ_NEXT(s_pWorldDebugLogs->iCurSubObjIndex, 0, MAX_DEBUG_WORLD_DRAW_LIST_SIZE);
}

static void createDrawableListDebugLogEntry(NOCONST(WorldDrawableList) *drawlist, bool removed MEM_DBG_PARMS)
{
	WorldDrawDebugLogEntry* debug;

	if (!s_pWorldDebugLogs)
		s_pWorldDebugLogs = (WorldDrawDebugLogs*)calloc(1, sizeof(WorldDrawDebugLogs));

	debug = eaGet(&s_pWorldDebugLogs->eaDrawLists, s_pWorldDebugLogs->iCurDrawListIndex);

	if (!debug)
	{
		debug = calloc(1, sizeof(WorldDrawDebugLogEntry));
		eaPush(&s_pWorldDebugLogs->eaDrawLists, debug);
	}
	debug->caller_fname = caller_fname;
	debug->caller_line = line;
	debug->ref_count = drawlist->ref_count;
	debug->id = drawlist->uid;
	debug->removed = !!removed;
	
	s_pWorldDebugLogs->iCurDrawListIndex = SEQ_NEXT(s_pWorldDebugLogs->iCurDrawListIndex, 0, MAX_DEBUG_WORLD_DRAW_LIST_SIZE);
}

static void createInstanceParamListDebugLogEntry(WorldInstanceParamList *paramlist, bool removed MEM_DBG_PARMS)
{
	WorldDrawDebugLogEntry* debug;

	if (!s_pWorldDebugLogs)
		s_pWorldDebugLogs = (WorldDrawDebugLogs*)calloc(1, sizeof(WorldDrawDebugLogs));

	debug = eaGet(&s_pWorldDebugLogs->eaInstanceParamLists, s_pWorldDebugLogs->iCurInstParamListIndex);

	if (!debug)
	{
		debug = calloc(1, sizeof(WorldDrawDebugLogEntry));
		eaPush(&s_pWorldDebugLogs->eaInstanceParamLists, debug);
	}
	debug->caller_fname = caller_fname;
	debug->caller_line = line;
	debug->ref_count = paramlist->ref_count;
	debug->id = paramlist->uid;
	debug->removed = !!removed;
	
	s_pWorldDebugLogs->iCurInstParamListIndex = SEQ_NEXT(s_pWorldDebugLogs->iCurInstParamListIndex, 0, MAX_DEBUG_WORLD_DRAW_LIST_SIZE);
}

SA_RET_NN_VALID static NOCONST(MaterialDraw) *allocMaterialDraw(SA_PARAM_OP_VALID NOCONST(WorldDrawableListPool) *pool, bool has_swaps, int tex_count, int const_count, int const_mapping_count)
{
	NOCONST(MaterialDraw) *draw = calloc(1, sizeof(NOCONST(MaterialDraw)));

	draw->has_swaps = !!has_swaps;

	draw->tex_count = tex_count;
	draw->const_count = const_count;
	draw->const_mapping_count = const_mapping_count;

	if (draw->tex_count)
		draw->textures = calloc(draw->tex_count, sizeof(BasicTexture*));

	if (draw->const_count)
		draw->constants = calloc(draw->const_count, sizeof(Vec4));

	if (draw->const_mapping_count)
		draw->constant_mappings = calloc(draw->const_mapping_count, sizeof(MaterialConstantMapping));

	if (pool)
	{
		if (!pool->material_draw_hash_table)
			pool->material_draw_hash_table = stashTableCreateExternalFunctions(4096, StashDefault, hashMaterialDraw, cmpMaterialDraw);
		++pool->total_material_draw_count;
		draw->uid = pool->material_draw_uid_counter++;
	}

	return draw;
}

static void freeMaterialDraw(SA_PARAM_OP_VALID NOCONST(WorldDrawableListPool) *pool, SA_PRE_NN_VALID SA_POST_P_FREE NOCONST(MaterialDraw) *draw, bool remove_from_hash_table)
{
	assert(draw->ref_count == 0);

	if (pool)
	{
		if (remove_from_hash_table)
		{
			U32 old_hash = draw->hash;
			U16 old_hashes_debug[] = { draw->hash_debug[0], draw->hash_debug[1], draw->hash_debug[2], draw->hash_debug[3] };

			stashRemovePointer(pool->material_draw_hash_table, draw, NULL);
			eaFindAndRemoveFast(&pool->material_draw_list, draw); // JE: This could be rather slow, but I don't think this gets called in production run-time.  If it causes editor problems, we can do a stashtable-only code path for editors

			// Validate that hashes have not changed
			draw->hash = 0;
			hashMaterialDraw(draw, DEFAULT_HASH_SEED);
			if (old_hash != draw->hash)
			{
				ErrorDetailsf("OLD HASHES %X/%X/%X/%X/%X, NEW HASHES %X/%X/%X/%X/%X",
					old_hashes_debug[0], old_hashes_debug[1], old_hashes_debug[2], old_hashes_debug[3], old_hash,
					draw->hash_debug[0], draw->hash_debug[1], draw->hash_debug[2], draw->hash_debug[3], draw->hash);
				Errorf("Material draw hashes no longer identical!");
			}
		}
		--pool->total_material_draw_count;
	}

	SAFE_FREE(draw->textures);
	SAFE_FREE(draw->constants);
	SAFE_FREE(draw->constant_mappings);
	SAFE_FREE(draw);
}

MaterialDraw *createMaterialDrawFromParsed(ZoneMap *zmap, MaterialDrawParsed *draw_parsed)
{
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, &zmap->world_cell_data.drawable_pool);
	NOCONST(MaterialDraw) *draw = allocMaterialDraw(pool, draw_parsed->has_swaps, eaiSize(&draw_parsed->texture_idxs), eafSize(&draw_parsed->constants) / 4, eaSize(&draw_parsed->constant_mappings));
	NOCONST(MaterialDraw) *existing_draw;
	const char *material_name;
	char fallback_name[1024];
	int i;

	ANALYSIS_ASSUME(!draw->tex_count || draw_parsed->texture_idxs);
	ANALYSIS_ASSUME(!draw->const_count || draw_parsed->constants);
	ANALYSIS_ASSUME(!draw->const_mapping_count || draw_parsed->constant_mappings);

	assert(draw_parsed->material_name_idx >= 0);
	material_name = worldGetStreamedString(zmap->world_cell_data.streaming_pooled_info, draw_parsed->material_name_idx);
	if (draw_parsed->fallback_idx < 0)
	{
		sprintf(fallback_name, "%s:Fallback", material_name);
		material_name = fallback_name;
	}
	else if (draw_parsed->fallback_idx > 0)
	{
		sprintf(fallback_name, "%s:Fallback:%d", material_name, draw_parsed->fallback_idx);
		material_name = fallback_name;
	}
	draw->material = materialFindEx(material_name, WL_FOR_WORLD, false);

	for (i = 0; i < draw->tex_count; ++i)
		draw->textures[i] = (draw_parsed->texture_idxs[i] < 0) ? NULL : wl_state.tex_find_func(worldGetStreamedString(zmap->world_cell_data.streaming_pooled_info, draw_parsed->texture_idxs[i]), true, WL_FOR_WORLD);

	// Fix up materials that were binned prior to the reflection texture being added to the list of material textures
	if (wl_state.materialdraw_fixup_func)
		wl_state.materialdraw_fixup_func(STRUCT_RECONST(MaterialDraw,draw));

	for (i = 0; i < draw->const_count; ++i)
		copyVec4(&draw_parsed->constants[i*4], draw->constants[i]);

	for (i = 0; i < draw->const_mapping_count; ++i)
		CopyStructs(&(draw->constant_mappings[i]), draw_parsed->constant_mappings[i], 1);

	draw->is_occluder = draw_parsed->is_occluder;

	if (stashFindPointer(pool->material_draw_hash_table, draw, &existing_draw))
	{
		freeMaterialDraw(pool, draw, false);
		draw = existing_draw;
	}
	else
	{
		stashAddPointer(pool->material_draw_hash_table, draw, draw, false);
		eaPush(&pool->material_draw_list, draw);
	}

	draw->ref_count++;

	if (gbDebugMaterialDraw)
		createMaterialDrawDebugLogEntry(draw, false MEM_DBG_PARMS_INIT);

	return STRUCT_RECONST(MaterialDraw, draw);
}

void removeMaterialDrawRefDbg(WorldDrawableListPool *pool_const, MaterialDraw *draw_const MEM_DBG_PARMS)
{
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, pool_const);
	NOCONST(MaterialDraw) *draw = STRUCT_NOCONST(MaterialDraw, draw_const);
	if (!draw)
		return;
	draw->ref_count--;
	if (pool && gbDebugMaterialDraw)
		createMaterialDrawDebugLogEntry(draw, true MEM_DBG_PARMS_CALL);
	if (draw->ref_count <= 0)
		freeMaterialDraw(pool, draw, true);
}

void getAllMaterialDraws(ZoneMap *zmap, MaterialDraw ***draw_array)
{
	StashTableIterator iter;
	StashElement elem;
	int initial_size = eaSize(draw_array);
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, &zmap->world_cell_data.drawable_pool);

	if (!pool->material_draw_hash_table)
		return;

	stashGetIterator(pool->material_draw_hash_table, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		MaterialDraw *draw_const = stashElementGetPointer(elem);
		NOCONST(MaterialDraw) *draw = STRUCT_NOCONST(MaterialDraw, draw_const);
		if (draw_const && eaFind(draw_array, draw_const) < 0)
		{
			eaPush(draw_array, draw_const);
			++draw->ref_count;
		}
	}

	qsortG((*draw_array) + initial_size, eaSize(draw_array) - initial_size, sizeof(MaterialDraw *), cmpMaterialDrawUID);
}

//////////////////////////////////////////////////////////////////////////

static U32 hashModelDraw(const NOCONST(ModelDraw) *model_draw, int hashSeed)
{
	U32 hash = hashSeed;

	if (model_draw->model)
		hash = burtlehash2((void *)&model_draw->model, sizeof(void *) / sizeof(U32), hash);
	hash = burtlehash2((void *)&model_draw->lod_idx, 1, hash);

	return hash;
}

static int cmpModelDraw(const ModelDraw *model_draw1, const ModelDraw *model_draw2)
{
	intptr_t pt;

	pt = (intptr_t)model_draw1->model - (intptr_t)model_draw2->model;
	if (pt)
		return SIGN(pt);

	return (int)model_draw1->lod_idx - (int)model_draw2->lod_idx;
}

static int cmpModelDrawUID(const ModelDraw **pmodel_draw1, const ModelDraw **pmodel_draw2)
{
	return (int)(*pmodel_draw1)->uid - (int)(*pmodel_draw2)->uid;
}

SA_RET_NN_VALID static NOCONST(ModelDraw) *allocModelDraw(SA_PARAM_OP_VALID NOCONST(WorldDrawableListPool) *pool, Model *model, int lod_idx)
{
	NOCONST(ModelDraw) *draw = calloc(1, sizeof(NOCONST(ModelDraw)));

	draw->model = model;
	draw->lod_idx = lod_idx;

	if (pool)
	{
		if (!pool->model_draw_hash_table)
			pool->model_draw_hash_table = stashTableCreateExternalFunctions(2048, StashDefault, hashModelDraw, cmpModelDraw);
		++pool->total_model_draw_count;
		draw->uid = pool->model_draw_uid_counter++;
	}

	return draw;
}

static void freeModelDraw(SA_PARAM_OP_VALID NOCONST(WorldDrawableListPool) *pool, SA_PRE_NN_VALID SA_POST_P_FREE NOCONST(ModelDraw) *draw, bool remove_from_hash_table)
{
	assert(draw->ref_count == 0);

	if (pool)
	{
		if (remove_from_hash_table)
		{
			stashRemovePointer(pool->model_draw_hash_table, draw, NULL);
			eaFindAndRemoveFast(&pool->model_draw_list, draw); // JE: This could be rather slow, but I don't think this gets called in production run-time.  If it causes editor problems, we can do a stashtable-only code path for editors
		}
		--pool->total_model_draw_count;
	}

	memlog_printf(&modelDrawLog, "%s is being freed.", draw->model->name);

	SAFE_FREE(draw);
}

ModelDraw *createModelDrawFromParsed(ZoneMap *zmap, ModelDrawParsed *draw_parsed)
{
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, &zmap->world_cell_data.drawable_pool);
	NOCONST(ModelDraw) *draw = allocModelDraw(pool, groupModelFind(worldGetStreamedString(zmap->world_cell_data.streaming_pooled_info, draw_parsed->model_idx), 0), draw_parsed->lod_idx);
	NOCONST(ModelDraw) *existing_draw;

	if (stashFindPointer(pool->model_draw_hash_table, draw, &existing_draw))
	{
		freeModelDraw(pool, draw, false);
		draw = existing_draw;
		memlog_printf(&modelDrawLog, "%s is adding a ref.", draw->model->name);
	}
	else
	{
		stashAddPointer(pool->model_draw_hash_table, draw, draw, false);
		eaPush(&pool->model_draw_list, draw);
		memlog_printf(&modelDrawLog, "%s is being created.", draw->model->name);
	}

	draw->ref_count++;
	memlog_printf(&modelDrawLog, "%s ref count is bumped up to %d.", draw->model->name, draw->ref_count);

	if (gbDebugModelDraw)
		createModelDrawDebugLogEntry(draw, false MEM_DBG_PARMS_INIT);

	return STRUCT_RECONST(ModelDraw, draw);
}

void removeModelDrawRef(WorldDrawableListPool *pool_const, ModelDraw *draw_const MEM_DBG_PARMS)
{
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, pool_const);
	NOCONST(ModelDraw) *draw = STRUCT_NOCONST(ModelDraw, draw_const);
	if (!draw)
		return;

	draw->ref_count--;
	memlog_printf(&modelDrawLog, "%s is removing a ref, count is now %d.", draw->model->name, draw->ref_count);

	if (pool && gbDebugModelDraw)
		createModelDrawDebugLogEntry(draw, true MEM_DBG_PARMS_CALL);
	if (draw->ref_count <= 0)
		freeModelDraw(pool, draw, true);
}

void getAllModelDraws(ZoneMap *zmap, ModelDraw ***draw_array)
{
	StashTableIterator iter;
	StashElement elem;
	int initial_size = eaSize(draw_array);
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, &zmap->world_cell_data.drawable_pool);

	if (!pool->model_draw_hash_table)
		return;

	stashGetIterator(pool->model_draw_hash_table, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		ModelDraw *draw_const = stashElementGetPointer(elem);
		NOCONST(ModelDraw) *draw = STRUCT_NOCONST(ModelDraw, draw_const);
		if (draw_const && eaFind(draw_array, draw_const) < 0)
		{
			eaPush(draw_array, draw_const);
			++draw->ref_count;
			memlog_printf(&modelDrawLog, "%s ModelDraw is having a ref added. Count: %d.", draw->model->name, draw->ref_count);
		}
	}

	qsortG((*draw_array) + initial_size, eaSize(draw_array) - initial_size, sizeof(ModelDraw *), cmpModelDrawUID);
}

//////////////////////////////////////////////////////////////////////////

static U32 hashDrawableSubobject(const NOCONST(WorldDrawableSubobject) *subobject, int hashSeed)
{
	U32 hash = hashSeed;
	int i;

	for (i=0; i<eaSize(&subobject->material_draws); i++)
		hash = hashMaterialDraw(subobject->material_draws[i], hash);
	hash = hashModelDraw(subobject->model, hash);
	hash = burtlehash2((void *)&subobject->subobject_idx, 1, hash);

	return hash;
}

static int cmpDrawableSubobject(const WorldDrawableSubobject *subobject1, const WorldDrawableSubobject *subobject2)
{
	int i;
	intptr_t pt;

	// ModelDraws are already pooled at this point, no need to compare the data
	pt = (intptr_t)subobject1->model - (intptr_t)subobject2->model;
	if (pt)
		return SIGN(pt);

	pt = eaSize(&subobject1->material_draws) - eaSize(&subobject2->material_draws);
	if (pt)
		return SIGN(pt);

	// MaterialDraws are already pooled at this point, no need to compare the data
	for (i=0; i<eaSize(&subobject1->material_draws); i++)
	{
		pt = (intptr_t)subobject1->material_draws[i] - (intptr_t)subobject2->material_draws[i];
		if (pt)
			return SIGN(pt);
	}

	return (int)subobject1->subobject_idx - (int)subobject2->subobject_idx;
}

static int cmpDrawableSubobjectUID(const WorldDrawableSubobject **psubobject1, const WorldDrawableSubobject **psubobject2)
{
	return (int)(*psubobject1)->uid - (int)(*psubobject2)->uid;
}

static NOCONST(WorldDrawableSubobject) *allocDrawableSubobject(SA_PARAM_OP_VALID NOCONST(WorldDrawableListPool) *pool, U32 subobject_idx)
{
	NOCONST(WorldDrawableSubobject) *subobject = calloc(1, sizeof(NOCONST(WorldDrawableSubobject)));

	subobject->subobject_idx = subobject_idx;

	if (pool)
	{
		if (!pool->subobject_hash_table)
			pool->subobject_hash_table = stashTableCreateExternalFunctions(4096, StashDefault, hashDrawableSubobject, cmpDrawableSubobject);
		++pool->total_subobject_count;
		subobject->uid = pool->subobject_uid_counter++;
	}

	return subobject;
}

static void freeDrawableSubobject(SA_PARAM_OP_VALID NOCONST(WorldDrawableListPool) *pool, SA_PRE_NN_VALID SA_POST_P_FREE NOCONST(WorldDrawableSubobject) *subobject, bool remove_from_hash_table MEM_DBG_PARMS)
{
	assert(subobject->ref_count == 0);

	if (pool)
	{
		assert(stashGetCount(pool->subobject_hash_table) <= (U32)pool->total_subobject_count);

		if (remove_from_hash_table)
		{
			assert(stashRemovePointer(pool->subobject_hash_table, subobject, NULL));
			eaFindAndRemoveFast(&pool->subobject_list, subobject); // JE: This could be rather slow, but I don't think this gets called in production run-time.  If it causes editor problems, we can do a stashtable-only code path for editors
		}

		removeModelDrawRef(STRUCT_RECONST(WorldDrawableListPool, pool), STRUCT_RECONST(ModelDraw, subobject->model) MEM_DBG_PARMS_CALL);
		FOR_EACH_IN_EARRAY(subobject->material_draws, NOCONST(MaterialDraw), matDraw)
		{
			removeMaterialDrawRef(STRUCT_RECONST(WorldDrawableListPool, pool), STRUCT_RECONST(MaterialDraw, matDraw));
		}
		FOR_EACH_END;
	}
	else
	{
		assert(subobject->model->ref_count == 1);
		subobject->model->ref_count = 0;
		memlog_printf(&modelDrawLog, "%s ModelDraw is being freed by parent subobject.", subobject->model->model->name);
		freeModelDraw(pool, subobject->model, false);

		FOR_EACH_IN_EARRAY(subobject->material_draws, NOCONST(MaterialDraw), matDraw)
		{
			assert(matDraw->ref_count == 1);
			matDraw->ref_count = 0;
			freeMaterialDraw(pool, matDraw, false);
		}
		FOR_EACH_END;
	}
	eaDestroy(&subobject->material_draws);

	free(subobject);

	if (pool)
		--pool->total_subobject_count;
}

static void addDrawableSubobjectRef(NOCONST(WorldDrawableSubobject) *subobject MEM_DBG_PARMS)
{
	if (!subobject)
		return;
	subobject->ref_count++;

	if (gbDebugDrawableSubObjects)
		createDrawableSubObjectDebugLogEntry(subobject, false MEM_DBG_PARMS_CALL);
}

void removeDrawableSubobjectRefDbg(WorldDrawableListPool *pool_const, WorldDrawableSubobject *subobject_const MEM_DBG_PARMS)
{
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, pool_const);
	NOCONST(WorldDrawableSubobject) *subobject = STRUCT_NOCONST(WorldDrawableSubobject, subobject_const);
	if (!subobject)
		return;
	subobject->ref_count--;
	if (pool && gbDebugDrawableSubObjects)
		createDrawableSubObjectDebugLogEntry(subobject, true MEM_DBG_PARMS_CALL);
	if (subobject->ref_count <= 0)
		freeDrawableSubobject(pool, subobject, true MEM_DBG_PARMS_CALL);
}

static NOCONST(WorldDrawableSubobject) *lookupDrawableSubobject(NOCONST(WorldDrawableListPool) *pool, NOCONST(WorldDrawableSubobject) *subobject MEM_DBG_PARMS)
{
	NOCONST(WorldDrawableSubobject) *existing_subobject = NULL;

	if (stashFindPointer(pool->subobject_hash_table, subobject, &existing_subobject))
	{
		assert(subobject != existing_subobject);
		freeDrawableSubobject(pool, subobject, false MEM_DBG_PARMS_CALL);
		subobject = existing_subobject;
	}
	else
	{
		stashAddPointer(pool->subobject_hash_table, subobject, subobject, false);
		eaPush(&pool->subobject_list, subobject);
	}

	assert(stashGetCount(pool->subobject_hash_table) <= (U32)pool->total_subobject_count);

	++subobject->ref_count;

	return subobject;
}

WorldDrawableSubobject *createDrawableSubobjectFromParsed(ZoneMap *zmap, WorldDrawableSubobjectParsed *subobject_parsed)
{
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, &zmap->world_cell_data.drawable_pool);
	NOCONST(WorldDrawableSubobject) *subobject = allocDrawableSubobject(pool, subobject_parsed->subobject_idx);
	int i;

	assert(subobject_parsed->modeldraw_idx >= 0);
	subobject->model = STRUCT_NOCONST(ModelDraw, zmap->world_cell_data.streaming_pooled_info->model_draws[subobject_parsed->modeldraw_idx]);
	if (subobject->model)
	{
		subobject->model->ref_count++;
		memlog_printf(&modelDrawLog, "%s ModelDraw is being created by parent subobject.  Count: %d.", subobject->model->model->name, subobject->model->ref_count);

		if (gbDebugModelDraw)
			createModelDrawDebugLogEntry(subobject->model, false MEM_DBG_PARMS_INIT);
	}

	assert(eaSize(&subobject->material_draws)==0);
	for (i=0; i<eaiSize(&subobject_parsed->materialdraw_idxs); i++)
	{
		MaterialDraw *matDrawConst;
		NOCONST(MaterialDraw) *matDraw;
		assert(subobject_parsed->materialdraw_idxs[i] >= 0);
		matDrawConst = zmap->world_cell_data.streaming_pooled_info->material_draws[subobject_parsed->materialdraw_idxs[i]];
		matDraw = STRUCT_NOCONST(MaterialDraw, matDrawConst);
		if (matDraw)
		{
			matDraw->ref_count++;

			if (gbDebugMaterialDraw)
				createMaterialDrawDebugLogEntry(matDraw, false MEM_DBG_PARMS_INIT);
		}
		eaPush(&subobject->material_draws, matDraw);
	}

	return STRUCT_RECONST(WorldDrawableSubobject, lookupDrawableSubobject(pool, subobject MEM_DBG_PARMS_INIT));
}

void getAllDrawableSubobjects(ZoneMap *zmap, WorldDrawableSubobject ***subobject_array)
{
	StashTableIterator iter;
	StashElement elem;
	int initial_size = eaSize(subobject_array);
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, &zmap->world_cell_data.drawable_pool);

	if (!pool->subobject_hash_table)
		return;

	stashGetIterator(pool->subobject_hash_table, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		WorldDrawableSubobject *subobject_const = stashElementGetPointer(elem);
		NOCONST(WorldDrawableSubobject) *subobject = STRUCT_NOCONST(WorldDrawableSubobject, subobject_const);
		if (subobject_const && eaFind(subobject_array, subobject_const) < 0)
		{
			eaPush(subobject_array, subobject_const);
			++subobject->ref_count;
		}
	}

	qsortG((*subobject_array) + initial_size, eaSize(subobject_array) - initial_size, sizeof(WorldDrawableSubobject *), cmpDrawableSubobjectUID);
}

//////////////////////////////////////////////////////////////////////////

U32 hashDrawableList(const NOCONST(WorldDrawableList) *draw_list, int hashSeed)
{
	U32 hash = hashSeed;
	U32 temp;
	int i, j;

	temp = !!draw_list->no_fog;
	hash = burtlehash2((void *)&temp, 1, hash);

	temp = !!draw_list->high_detail_high_lod;
	hash = burtlehash2((void *)&temp, 1, hash);

	for (i = 0; i < draw_list->lod_count; ++i)
	{
		NOCONST(WorldDrawableLod) *lod = &draw_list->drawable_lods[i];
		hash = burtlehash2((void *)&lod->subobject_count, 1, hash);
		hash = burtlehash2((void *)&lod->near_dist, 1, hash);
		hash = burtlehash2((void *)&lod->far_dist, 1, hash);
		hash = burtlehash2((void *)&lod->far_morph, 1, hash);
		hash = burtlehash2((void *)&lod->occlusion_materials, 1, hash);
		temp = !!lod->no_fade;
		hash = burtlehash2((void *)&temp, 1, hash);
		if (lod->subobjects)
		{
			for (j = 0; j < lod->subobject_count; ++j)
				hash = hashDrawableSubobject(lod->subobjects[j], hash);
		}
	}
	return hash;
}

static int cmpDrawableList(const WorldDrawableList *draw_list1, const WorldDrawableList *draw_list2)
{
	int it, i, j;
	intptr_t pt;
	F32 t;

	it = (int)draw_list1->lod_count - (int)draw_list2->lod_count;
	if (it)
		return SIGN(it);

	it = (int)draw_list1->no_fog - (int)draw_list2->no_fog;
	if (it)
		return SIGN(it);

	it = (int)draw_list1->high_detail_high_lod - (int)draw_list2->high_detail_high_lod;
	if (it)
		return SIGN(it);

	for (i = 0; i < draw_list1->lod_count; ++i)
	{
		WorldDrawableLod *lod1 = &draw_list1->drawable_lods[i];
		WorldDrawableLod *lod2 = &draw_list2->drawable_lods[i];

		t = lod1->near_dist - lod2->near_dist;
		if (t)
			return SIGN(t);

		t = lod1->far_dist - lod2->far_dist;
		if (t)
			return SIGN(t);

		t = lod1->far_morph - lod2->far_morph;
		if (t)
			return SIGN(t);

		it = (int)lod1->subobject_count - (int)lod2->subobject_count;
		if (it)
			return SIGN(it);

		it = (int)lod1->occlusion_materials - (int)lod2->occlusion_materials;
		if (it)
			return SIGN(it);

		it = (int)lod1->no_fade - (int)lod2->no_fade;
		if (it)
			return SIGN(it);

		for (j = 0; j < lod1->subobject_count; ++j)
		{
			// WorldDrawableSubobjects are already pooled at this point, no need to compare the data
			pt = (intptr_t)lod1->subobjects[j] - (intptr_t)lod2->subobjects[j];
			if (pt)
				return SIGN(pt);
		}
	}

	return 0;
}

static int cmpDrawableListUID(const WorldDrawableList **pdraw_list1, const WorldDrawableList **pdraw_list2)
{
	return (int)(*pdraw_list1)->uid - (int)(*pdraw_list2)->uid;
}

static NOCONST(WorldDrawableList) *allocDrawableList(SA_PARAM_OP_VALID NOCONST(WorldDrawableListPool) *pool, int lod_count)
{
	NOCONST(WorldDrawableList) *draw_list = calloc(1, sizeof(NOCONST(WorldDrawableList)) + lod_count * sizeof(NOCONST(WorldDrawableLod)));
	draw_list->drawable_lods = (NOCONST(WorldDrawableLod) *)(draw_list+1);
	draw_list->lod_count = lod_count;
	draw_list->pool = pool;

	if (pool)
	{
		if (!pool->draw_list_hash_table)
			pool->draw_list_hash_table = stashTableCreateExternalFunctions(4096, StashDefault, hashDrawableList, cmpDrawableList);
		++pool->total_draw_list_count;
		draw_list->uid = pool->draw_list_uid_counter++;
	}

	return draw_list;
}

static void checkForPointer(WorldDrawableListPool *pool, WorldDrawableList *draw_list)
{
	FOR_EACH_IN_STASHTABLE(pool->draw_list_hash_table, WorldDrawableList, existing_draw_list)
	{
		assert(existing_draw_list != draw_list);
		assert(existing_draw_list->ref_count > 0);
	}
	FOR_EACH_END;
}

void freeDrawableListDbg(WorldDrawableList *draw_list_const, bool remove_from_hash_table MEM_DBG_PARMS)
{
	NOCONST(WorldDrawableList)* draw_list = STRUCT_NOCONST(WorldDrawableList, draw_list_const);
	NOCONST(WorldDrawableListPool) *pool;
	int i, j;

	if (!draw_list)
		return;

	pool = draw_list->pool;
	assert(draw_list->ref_count == 0);

	if (pool)
	{
		assert(stashGetCount(pool->draw_list_hash_table) <= (U32)pool->total_draw_list_count);

		if (remove_from_hash_table)
			assert(stashRemovePointer(pool->draw_list_hash_table, draw_list, NULL));

		// checkForPointer(pool, draw_list);
	}

	for (i = 0; i < draw_list->lod_count; ++i)
	{
		NOCONST(WorldDrawableLod) *lod = &draw_list->drawable_lods[i];
		if (lod->subobjects)
		{
			if (pool)
			{
				for (j = 0; j < lod->subobject_count; ++j)
					removeDrawableSubobjectRef(STRUCT_RECONST(WorldDrawableListPool, pool), STRUCT_RECONST(WorldDrawableSubobject, lod->subobjects[j]));
			}
			else
			{
				for (j = 0; j < lod->subobject_count; ++j)
				{
					// should not be shared
					assert(lod->subobjects[j]->ref_count == 1);
					lod->subobjects[j]->ref_count = 0;
					freeDrawableSubobject(pool, lod->subobjects[j], false MEM_DBG_PARMS_CALL);
				}
			}
			free(lod->subobjects);
		}
	}

	free(draw_list); // also frees lod->subobjects, since they are allocated as one block

	if (pool)
		--pool->total_draw_list_count;
}

void addDrawableListRef(WorldDrawableList *draw_list_const MEM_DBG_PARMS)
{
	NOCONST(WorldDrawableList) *draw_list = STRUCT_NOCONST(WorldDrawableList, draw_list_const);
	if (!draw_list)
		return;
	assert(draw_list->ref_count > 0);
	draw_list->ref_count++;

	if (gbDebugDrawableLists)
		createDrawableListDebugLogEntry(draw_list, false MEM_DBG_PARMS_CALL);
}

void removeDrawableListRefDbg(WorldDrawableList *draw_list_const MEM_DBG_PARMS)
{
	NOCONST(WorldDrawableList)* draw_list = STRUCT_NOCONST(WorldDrawableList, draw_list_const);

	if (!draw_list)
		return;
	draw_list->ref_count--;
	if (gbDebugDrawableLists)
		createDrawableListDebugLogEntry(draw_list, true MEM_DBG_PARMS_CALL);
	if (draw_list->ref_count <= 0)
		freeDrawableList(draw_list_const, true);
}

void removeDrawableListRefCB(WorldDrawableList *draw_list)
{
	removeDrawableListRefDbg(draw_list MEM_DBG_PARMS_INIT);
}

static NOCONST(WorldDrawableList) *lookupDrawableList(NOCONST(WorldDrawableList) *draw_list MEM_DBG_PARMS)
{
	NOCONST(WorldDrawableList) *existing_draw_list = NULL;
	NOCONST(WorldDrawableListPool) *pool = draw_list->pool;

	if (stashFindPointer(pool->draw_list_hash_table, draw_list, &existing_draw_list))
	{
		assert(draw_list != existing_draw_list);
		freeDrawableList(STRUCT_RECONST(WorldDrawableList, draw_list), false);
		draw_list = existing_draw_list;
	}
	else
	{
		stashAddPointer(pool->draw_list_hash_table, draw_list, draw_list, false);
	}

	assert(stashGetCount(pool->draw_list_hash_table) <= (U32)pool->total_draw_list_count);

	++draw_list->ref_count;

	return draw_list;
}

WorldDrawableList *createDrawableListFromParsed(ZoneMap *zmap, WorldDrawableListParsed *draw_list_parsed)
{
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, &zmap->world_cell_data.drawable_pool);
	NOCONST(WorldDrawableList) *draw_list = allocDrawableList(pool, eaSize(&draw_list_parsed->drawable_lods));
	int i, j;

	ANALYSIS_ASSUME(!draw_list->lod_count || draw_list_parsed->drawable_lods);

	draw_list->no_fog = !!draw_list_parsed->no_fog;
	draw_list->high_detail_high_lod = !!draw_list_parsed->high_detail_high_lod;

	for (i = 0; i < draw_list->lod_count; ++i)
	{
		NOCONST(WorldDrawableLod) *lod = &draw_list->drawable_lods[i];
		WorldDrawableLodParsed *lod_parsed = draw_list_parsed->drawable_lods[i];

		lod->near_dist = lod_parsed->near_dist;
		lod->far_dist = lod_parsed->far_dist;
		lod->far_morph = lod_parsed->far_morph;
		lod->occlusion_materials = lod_parsed->occlusion_materials;
		lod->no_fade = !!lod_parsed->no_fade;

		lod->subobject_count = lod_parsed->subobject_count;
		assert(eaiSize(&lod_parsed->subobject_idxs) == lod_parsed->subobject_count);
		lod->subobjects = calloc(lod->subobject_count, sizeof(NOCONST(WorldDrawableSubobject) *));
		for (j = 0; j < lod->subobject_count; ++j)
		{
			assert(lod_parsed->subobject_idxs[j] >= 0);
			lod->subobjects[j] = STRUCT_NOCONST(WorldDrawableSubobject, zmap->world_cell_data.streaming_pooled_info->subobjects[lod_parsed->subobject_idxs[j]]);
			if (lod->subobjects[j])
			{
				lod->subobjects[j]->ref_count++;

				if (gbDebugDrawableSubObjects)
					createDrawableSubObjectDebugLogEntry(lod->subobjects[j], false MEM_DBG_PARMS_INIT);
			}
		}
	}

	return STRUCT_RECONST(WorldDrawableList, lookupDrawableList(draw_list MEM_DBG_PARMS_INIT));
}

void getAllDrawableLists(ZoneMap *zmap, WorldDrawableList ***draw_list_array)
{
	StashTableIterator iter;
	StashElement elem;
	int initial_size = eaSize(draw_list_array);
	WorldDrawableListPool *pool = &zmap->world_cell_data.drawable_pool;

	if (!pool->draw_list_hash_table)
		return;

	stashGetIterator(pool->draw_list_hash_table, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		WorldDrawableList *draw_list_const = stashElementGetPointer(elem);
		NOCONST(WorldDrawableList) *draw_list = STRUCT_NOCONST(WorldDrawableList, draw_list_const);
		if (draw_list_const && eaFind(draw_list_array, draw_list_const) < 0)
		{
			eaPush(draw_list_array, draw_list_const);
			++draw_list->ref_count;
		}
	}

	qsortG((*draw_list_array) + initial_size, eaSize(draw_list_array) - initial_size, sizeof(WorldDrawableList *), cmpDrawableListUID);
}

//////////////////////////////////////////////////////////////////////////

U32 hashInstanceParamList(const WorldInstanceParamList *param_list, int hashSeed)
{
	int i, j, k;
	U32 hash = hashSeed;
	for (i = 0; i < param_list->lod_count; ++i)
	{
		WorldInstanceParamPerLod *lod_param = &param_list->lod_params[i];
		for (j = 0; j < lod_param->subobject_count; ++j)
		{
			WorldInstanceParamPerSubObj *subobj_param = &lod_param->subobject_params[j];
			for (k = 0; k < subobj_param->fallback_count; ++k)
				hash = burtlehash2((U32*)&subobj_param->fallback_params[k].instance_param[0], 4, hash);
		}
	}
	return hash;
}

int cmpInstanceParamList(const WorldInstanceParamList *param_list1, const WorldInstanceParamList *param_list2)
{
	int i, j, k, t;

	t = param_list1->lod_count - param_list2->lod_count;
	if (t)
		return SIGN(t);

	for (i = 0; i < param_list1->lod_count; ++i)
	{
		WorldInstanceParamPerLod *lod_param1 = &param_list1->lod_params[i];
		WorldInstanceParamPerLod *lod_param2 = &param_list2->lod_params[i];

		t = lod_param1->subobject_count - lod_param2->subobject_count;
		if (t)
			return SIGN(t);

		for (j = 0; j < lod_param1->subobject_count; ++j)
		{
			WorldInstanceParamPerSubObj *subobj_param1 = &lod_param1->subobject_params[j];
			WorldInstanceParamPerSubObj *subobj_param2 = &lod_param2->subobject_params[j];

			t = subobj_param1->fallback_count - subobj_param2->fallback_count;
			if (t)
				return SIGN(t);

			for (k = 0; k < subobj_param1->fallback_count; ++k)
			{
				t = cmpVec4(subobj_param1->fallback_params[k].instance_param, subobj_param2->fallback_params[k].instance_param);
				if (t)
					return SIGN(t);
			}
		}
	}

	return 0;
}

static int cmpInstanceParamListUID(const WorldInstanceParamList **pparam_list1, const WorldInstanceParamList **pparam_list2)
{
	return (int)(*pparam_list1)->uid - (int)(*pparam_list2)->uid;
}

static WorldInstanceParamList *allocInstanceParamList(SA_PARAM_OP_VALID NOCONST(WorldDrawableListPool) *pool, int lod_count)
{
	WorldInstanceParamList *param_list = calloc(1, sizeof(WorldInstanceParamList) + lod_count * sizeof(WorldInstanceParamPerLod));
	param_list->lod_params = (WorldInstanceParamPerLod *)(param_list+1);
	param_list->lod_count = lod_count;
	param_list->pool = STRUCT_RECONST(WorldDrawableListPool, pool);

	if (pool)
	{
		if (!pool->instance_data_hash_table)
			pool->instance_data_hash_table = stashTableCreateExternalFunctions(4096, StashDefault, hashInstanceParamList, cmpInstanceParamList);
		++pool->total_instance_data_count;
		param_list->uid = pool->instance_data_uid_counter++;
	}

	return param_list;
}

void freeInstanceParamList(WorldInstanceParamList *param_list, bool remove_from_hash_table)
{
	NOCONST(WorldDrawableListPool) *pool;
	int i, j;

	if (!param_list)
		return;

	pool = STRUCT_NOCONST(WorldDrawableListPool, param_list->pool);
	assert(param_list->ref_count == 0);
	
	if (pool)
	{
		assert(stashGetCount(pool->instance_data_hash_table) <= (U32)pool->total_instance_data_count);

		if (remove_from_hash_table)
			assert(stashRemovePointer(pool->instance_data_hash_table, param_list, NULL));

		--pool->total_instance_data_count;
	}

	for (i = 0; i < param_list->lod_count; ++i)
	{
		WorldInstanceParamPerLod *lod_param = &param_list->lod_params[i];
		for (j = 0; j < lod_param->subobject_count; ++j)
		{
			WorldInstanceParamPerSubObj *subobj_param = &lod_param->subobject_params[j];
			free(subobj_param->fallback_params);
		}
		free(lod_param->subobject_params);
	}

	free(param_list); // also frees param_list->lod_params, since they are from the same allocation

}

void addInstanceParamListRef(WorldInstanceParamList *param_list MEM_DBG_PARMS)
{
	if (!param_list)
		return;
	assert(param_list->ref_count > 0);
	param_list->ref_count++;

	if (gbDebugInstanceParamLists)
		createInstanceParamListDebugLogEntry(param_list, false MEM_DBG_PARMS_CALL);
}

void removeInstanceParamListRef(WorldInstanceParamList *param_list MEM_DBG_PARMS)
{
	if (!param_list)
		return;
	param_list->ref_count--;
	if (gbDebugInstanceParamLists)
		createInstanceParamListDebugLogEntry(param_list, true MEM_DBG_PARMS_CALL);
	if (param_list->ref_count <= 0)
		freeInstanceParamList(param_list, true);
}

void removeInstanceParamListRefCB(WorldInstanceParamList *param_list)
{
	removeInstanceParamListRef(param_list MEM_DBG_PARMS_INIT);
}

static WorldInstanceParamList *lookupInstanceParamList(WorldInstanceParamList *param_list)
{
	WorldInstanceParamList *existing_param_list = NULL;
	WorldDrawableListPool *pool = param_list->pool;

	if (stashFindPointer(pool->instance_data_hash_table, param_list, &existing_param_list))
	{
		assert(param_list != existing_param_list);
		freeInstanceParamList(param_list, false);
		param_list = existing_param_list;
	}
	else
	{
		stashAddPointer(pool->instance_data_hash_table, param_list, param_list, false);
	}

	assert(stashGetCount(pool->instance_data_hash_table) <= (U32)pool->total_instance_data_count);

	++param_list->ref_count;

	return param_list;
}

WorldInstanceParamList *createInstanceParamListFromParsed(ZoneMap *zmap, WorldInstanceParamListParsed *param_list_parsed)
{
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, &zmap->world_cell_data.drawable_pool);
	WorldInstanceParamList *param_list = allocInstanceParamList(pool, param_list_parsed->lod_count);
	int i, j, k, idx = 0;

	for (i = 0; i < param_list->lod_count; ++i)
	{
		WorldInstanceParamPerLod *lod_param = &param_list->lod_params[i];
		lod_param->subobject_count = ((int *)param_list_parsed->instance_params)[idx++];
		lod_param->subobject_params = calloc(sizeof(*lod_param->subobject_params), lod_param->subobject_count);

		for (j = 0; j < lod_param->subobject_count; ++j)
		{
			WorldInstanceParamPerSubObj *subobj_param = &lod_param->subobject_params[j];
			subobj_param->fallback_count = ((int *)param_list_parsed->instance_params)[idx++];
			subobj_param->fallback_params = calloc(sizeof(*subobj_param->fallback_params), subobj_param->fallback_count);

			for (k = 0; k < subobj_param->fallback_count; ++k)
			{
				subobj_param->fallback_params[k].instance_param[0] = param_list_parsed->instance_params[idx++];
				subobj_param->fallback_params[k].instance_param[1] = param_list_parsed->instance_params[idx++];
				subobj_param->fallback_params[k].instance_param[2] = param_list_parsed->instance_params[idx++];
				subobj_param->fallback_params[k].instance_param[3] = param_list_parsed->instance_params[idx++];
			}
		}
	}

	assert(idx == eafSize(&param_list_parsed->instance_params));

	return lookupInstanceParamList(param_list);
}

int cmpInstanceParamListToParsed(const WorldInstanceParamList *param_list1, const WorldInstanceParamListParsed *param_list2)
{
	int i, j, k, t;
	F32 *param_ptr = param_list2->instance_params;

	t = param_list1->lod_count - param_list2->lod_count;
	if (t)
		return SIGN(t);

	for (i = 0; i < param_list1->lod_count; ++i)
	{
		WorldInstanceParamPerLod *lod_param1 = &param_list1->lod_params[i];
		int subobject_count2 = *((int*)param_ptr++);

		t = lod_param1->subobject_count - subobject_count2;
		if (t)
			return SIGN(t);

		for (j = 0; j < lod_param1->subobject_count; ++j)
		{
			WorldInstanceParamPerSubObj *subobj_param1 = &lod_param1->subobject_params[j];
			int fallback_count2 = *((int*)param_ptr++);

			t = subobj_param1->fallback_count - fallback_count2;
			if (t)
				return SIGN(t);

			for (k = 0; k < subobj_param1->fallback_count; ++k)
			{
				Vec4 param2;
				param2[0] = *(param_ptr++);
				param2[1] = *(param_ptr++);
				param2[2] = *(param_ptr++);
				param2[3] = *(param_ptr++);
				t = cmpVec4(subobj_param1->fallback_params[k].instance_param, param2);
				if (t)
					return SIGN(t);
			}
		}
	}

	return 0;
}

static F32 distInstanceParamListToParsed(const WorldInstanceParamList *param_list1, const WorldInstanceParamListParsed *param_list2)
{
	int i, j, k;
	F32 dist = 0;
	F32 *param_ptr = param_list2->instance_params;

	if (param_list1->lod_count != param_list2->lod_count)
		return 8e16;

	for (i = 0; i < param_list1->lod_count; ++i)
	{
		WorldInstanceParamPerLod *lod_param1 = &param_list1->lod_params[i];
		int subobject_count2 = *((int*)param_ptr++);

		if (lod_param1->subobject_count != subobject_count2)
			return 8e16;

		for (j = 0; j < lod_param1->subobject_count; ++j)
		{
			WorldInstanceParamPerSubObj *subobj_param1 = &lod_param1->subobject_params[j];
			int fallback_count2 = *((int*)param_ptr++);

			if (subobj_param1->fallback_count != fallback_count2)
				return 8e16;

			for (k = 0; k < subobj_param1->fallback_count; ++k)
			{
				Vec4 param2;
				param2[0] = *(param_ptr++);
				param2[1] = *(param_ptr++);
				param2[2] = *(param_ptr++);
				param2[3] = *(param_ptr++);
				dist += distance3(subobj_param1->fallback_params[k].instance_param, param2);
				dist += ABS(subobj_param1->fallback_params[k].instance_param[3] - param2[3]);
			}
		}
	}

	return dist;	
}

void getAllInstanceParamLists(ZoneMap *zmap, WorldInstanceParamList ***param_list_array, WorldStreamingPooledInfo *old_pooled_info)
{
	StashTableIterator iter;
	StashElement elem;
	int initial_size = eaSize(param_list_array);
	WorldDrawableListPool *pool = &zmap->world_cell_data.drawable_pool;

	if (!pool->instance_data_hash_table)
		return;

	stashGetIterator(pool->instance_data_hash_table, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		WorldInstanceParamList *param_list = stashElementGetPointer(elem);
		if (param_list && eaFind(param_list_array, param_list) < 0)
		{
			eaPush(param_list_array, param_list);
			++param_list->ref_count;
		}
	}

	qsortG((*param_list_array) + initial_size, eaSize(param_list_array) - initial_size, sizeof(WorldInstanceParamList *), cmpInstanceParamListUID);

	if (old_pooled_info && old_pooled_info->packed_info)
	{
		*param_list_array = (WorldInstanceParamList**)maintainOldIndices(*param_list_array, old_pooled_info->packed_info->instance_param_lists_parsed, 
																		 cmpInstanceParamListToParsed, distInstanceParamListToParsed, 5);
	}
}

//////////////////////////////////////////////////////////////////////////

static int clearTempRenderInfo(StashElement element)
{
	MaterialDraw *draw = stashElementGetKey(element);
	if (draw)
		draw->temp_render_info = NULL;
	return 1;
}

void worldDrawableListPoolClearMaterialCache(WorldDrawableListPool *pool)
{
	PERFINFO_AUTO_START_FUNC();
// 	if (pool->material_draw_hash_table)
// 		stashForEachElement(pool->material_draw_hash_table, clearTempRenderInfo);
	FOR_EACH_IN_EARRAY(pool->material_draw_list, MaterialDraw, draw)
	{
		draw->temp_render_info = NULL;
	}
	FOR_EACH_END;
	PERFINFO_AUTO_STOP();
}

void worldCellEntryClearTempMaterials(void)
{
	int i;
	for (i = 0; i < eaSize(&world_grid.maps); ++i)
		worldDrawableListPoolClearMaterialCache(&world_grid.maps[i]->world_cell_data.drawable_pool);
}

static int clearTempGeoDraw(StashElement element)
{
	ModelDraw *model_draw = stashElementGetKey(element);
	if (model_draw)
		model_draw->temp_geo_draw = NULL;
	return 1;
}

static int clearTempSubobject(StashElement element)
{
	WorldDrawableSubobject *subobject = stashElementGetKey(element);
	if (subobject)
		subobject->temp_rdr_subobject = NULL;
	return 1;
}

void worldDrawableListPoolClearGeoCache(WorldDrawableListPool *pool)
{
	PERFINFO_AUTO_START_FUNC();
// 	if (pool->model_draw_hash_table)
// 		stashForEachElement(pool->model_draw_hash_table, clearTempGeoDraw);
	FOR_EACH_IN_EARRAY(pool->model_draw_list, ModelDraw, model_draw)
	{
		model_draw->temp_geo_draw = NULL;
	}
	FOR_EACH_END;
// 	if (pool->subobject_hash_table)
// 		stashForEachElement(pool->subobject_hash_table, clearTempSubobject);
	FOR_EACH_IN_EARRAY(pool->subobject_list, WorldDrawableSubobject, subobject)
	{
		subobject->temp_rdr_subobject = NULL;
	}
	FOR_EACH_END;
	PERFINFO_AUTO_STOP();
}

void worldCellEntryClearTempGeoDraws(void)
{
	int i;
	for (i = 0; i < eaSize(&world_grid.maps); ++i)
		worldDrawableListPoolClearGeoCache(&world_grid.maps[i]->world_cell_data.drawable_pool);
}

void worldDrawableListPoolReset(WorldDrawableListPool *pool_const)
{
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, pool_const);
	if (dtInitialized())
		dtFxKillAll();

	if (gbDebugMaterialDraw && pool->total_material_draw_count > 0)
	{
		DebugGetWorldDrawLogStringAll(&s_estrDebugDrawError);
	}
	assert(!pool->total_material_draw_count);
	stashTableDestroy(pool->material_draw_hash_table);
	eaDestroy(&pool->material_draw_list);
	pool->material_draw_hash_table = NULL;
	pool->material_draw_uid_counter = 0;

	assert(!pool->total_model_draw_count);
	stashTableDestroy(pool->model_draw_hash_table);
	eaDestroy(&pool->model_draw_list);
	pool->model_draw_hash_table = NULL;
	pool->model_draw_uid_counter = 0;

	assert(!pool->total_subobject_count);
	stashTableDestroy(pool->subobject_hash_table);
	eaDestroy(&pool->subobject_list);
	pool->subobject_hash_table = NULL;
	pool->subobject_uid_counter = 0;

	assert(!pool->total_draw_list_count);
	stashTableDestroy(pool->draw_list_hash_table);
	pool->draw_list_hash_table = NULL;
	pool->draw_list_uid_counter = 0;

	assert(!pool->total_instance_data_count);
	stashTableDestroy(pool->instance_data_hash_table);
	pool->instance_data_hash_table = NULL;
	pool->instance_data_uid_counter = 0;
}

static NOCONST(WorldDrawableSubobject) *applyMaterialSwaps( SA_PARAM_OP_VALID NOCONST(WorldDrawableListPool) *pool, ModelLOD *lod_model, int lod_index_override, 
															int subobject_idx, SA_PARAM_OP_VALID GroupDef *def, 
															WorldInstanceParamPerSubObj *instance_subobj_param, bool *no_fog, 
															const Material **src_materials, 
															const char **texture_swaps, const char **material_swaps, const char *material_replace, 
															MaterialNamedTexture **named_texture_swaps, MaterialNamedConstant **material_constants, 
															bool use_fallback_materials, WLUsageFlags use_flags MEM_DBG_PARMS)
{
	NOCONST(WorldDrawableSubobject) *subobject;
	bool material_changed;
	int need_textures, need_constants, need_constant_mappings;
	const char *material_name;
	Material *material;
	int j;
	int num_fallbacks;

	PERFINFO_AUTO_START_FUNC();

	subobject = allocDrawableSubobject(pool, subobject_idx);

	material_changed = false;

	if (material_replace)
	{
		material_name = material_replace;
		material_changed = true;
	}
	else
	{
		material_name = src_materials[subobject->subobject_idx]->material_name;
	}

	for (j = eaSize(&material_swaps) - 2; j >= 0; j -= 2)
	{
		if (stricmp(material_name, material_swaps[j])==0)
		{
			material_name = material_swaps[j+1];
			material_changed = true;
		}
	}
	worldDepsReportMaterial(material_name, def);

	if (use_fallback_materials && !strEndsWith(material_name, ":Fallback"))
	{
		char fallback_name[1024];
		material_changed = true;
		sprintf(fallback_name, "%s:Fallback", material_name);
		material = materialFind(fallback_name, use_flags);
	}
	else
	{
		material = materialFind(material_name, use_flags);
	}

	if (use_fallback_materials || strEndsWith(material_name, ":Fallback"))
		num_fallbacks = 1;
	else
		num_fallbacks = eaSize(&materialGetData(material)->graphic_props.fallbacks) + 1;

	instance_subobj_param->fallback_count = num_fallbacks;
	assert(!instance_subobj_param->fallback_params);
	instance_subobj_param->fallback_params = calloc(sizeof(instance_subobj_param->fallback_params[0]), num_fallbacks);

	*no_fog = *no_fog || !!(material->graphic_props.flags & RMATERIAL_NOFOG);

	for (j=0; j<num_fallbacks; j++)
	{
		NOCONST(MaterialDraw) *matDraw;

		// Pre-swap for all fallbacks
		if (j>0)
		{
			char fallback_name[1024];
			sprintf(fallback_name, "%s:Fallback:%d", material_name, j);
			material = materialFind(fallback_name, use_flags);
			*no_fog = *no_fog || !!(material->graphic_props.flags & RMATERIAL_NOFOG);
			material_changed = false;
		} else if (num_fallbacks>1) {
			if (gbMakeBinsAndExit)
				assertmsg(!material->incompatible, "Doing binning but we got a fallback material, perhaps running with incorrect graphics settings or a card that does not support all materials?");
		}

		material_changed |= wl_state.material_check_swaps_func(material, texture_swaps, material_constants, 
															   named_texture_swaps, NULL, 
															   &need_textures, &need_constants, &need_constant_mappings, 
															   instance_subobj_param->fallback_params[j].instance_param); 

		matDraw = allocMaterialDraw(pool, material_changed, need_textures, need_constants, need_constant_mappings);
		eaPush(&subobject->material_draws, matDraw);

		if (material_changed)
		{
			wl_state.material_apply_swaps_func(CONTAINER_RECONST(MaterialDraw, matDraw), material, texture_swaps, material_constants, 
											   named_texture_swaps, NULL, use_flags);

			if (wl_state.tex_name_func)
			{
				if (need_textures)
				{
					int i;
					// There were texture swaps
					for (i = 0; i < matDraw->tex_count; ++i)
						worldDepsReportTexture(wl_state.tex_name_func(matDraw->textures[i]));
				}
			}
		}
		else
		{
			matDraw->material = material;
			matDraw->is_occluder = wl_state.material_check_occluder_func(material);
		}


		// pool material
		if (pool)
		{
			NOCONST(MaterialDraw) *existing_material_draw = NULL;
			if (stashFindPointer(pool->material_draw_hash_table, matDraw, &existing_material_draw))
			{
				eaPop(&subobject->material_draws);
				freeMaterialDraw(pool, matDraw, false);
				matDraw = existing_material_draw;
				eaPush(&subobject->material_draws, matDraw);
			}
			else
			{
				stashAddPointer(pool->material_draw_hash_table, matDraw, matDraw, false);
				eaPush(&pool->material_draw_list, matDraw);
			}
		}

		matDraw->ref_count++;
		
		if (pool && gbDebugMaterialDraw)
			createMaterialDrawDebugLogEntry(matDraw, false MEM_DBG_PARMS_CALL);
	}
	material = NULL;


	// pool model
	subobject->model = allocModelDraw(pool, lod_model->model_parent, lod_index_override < 0 ? lod_model->lod_index : lod_index_override);
	if (pool)
	{
		NOCONST(ModelDraw) *existing_model_draw = NULL;
		if (stashFindPointer(pool->model_draw_hash_table, subobject->model, &existing_model_draw))
		{
			freeModelDraw(pool, subobject->model, false);
			subobject->model = existing_model_draw;
		}
		else
		{
			stashAddPointer(pool->model_draw_hash_table, subobject->model, subobject->model, false);
			eaPush(&pool->model_draw_list, subobject->model);
		}
	}

	subobject->model->ref_count++;
	memlog_printf(&modelDrawLog, "%s model's count is getting incremented: %d.", subobject->model->model->name, subobject->model->ref_count);

	if (pool && gbDebugModelDraw)
		createModelDrawDebugLogEntry(subobject->model, false MEM_DBG_PARMS_CALL);

	// pool subobject
	if (pool)
	{
		NOCONST(WorldDrawableSubobject) *existing_subobject = NULL;
		if (stashFindPointer(pool->subobject_hash_table, subobject, &existing_subobject))
		{
			freeDrawableSubobject(pool, subobject, false MEM_DBG_PARMS_CALL);
			subobject = existing_subobject;
		}
		else
		{
			stashAddPointer(pool->subobject_hash_table, subobject, subobject, false);
			eaPush(&pool->subobject_list, subobject);
		}
	}

	subobject->ref_count++;

	if (pool && gbDebugDrawableSubObjects)
		createDrawableSubObjectDebugLogEntry(subobject, false MEM_DBG_PARMS_CALL);

	PERFINFO_AUTO_STOP();

	return subobject;
}

static void copyDrawableDataFromParsed(ZoneMap *zmap, WorldDrawableEntry *draw, WorldDrawableEntryParsed *draw_parsed, WorldCell *cell_debug, bool parsed_will_be_freed)
{
	WorldAnimationEntry *animation_controller;
	WorldStreamingPooledInfo *streaming_pooled_info = zmap->world_cell_data.streaming_pooled_info;
	int i;

	draw->camera_facing = draw_parsed->bitfield.camera_facing;
	draw->axis_camera_facing = draw_parsed->bitfield.axis_camera_facing;
	draw->should_cluster = draw_parsed->is_clustered ? CLUSTERED : NON_CLUSTERED;
	copyVec4(draw_parsed->color, draw->color);

	if (draw_parsed->draw_list_idx >= 0)
	{
		assert(draw_parsed->draw_list_idx < eaSize(&streaming_pooled_info->drawable_lists));
		draw->draw_list = streaming_pooled_info->drawable_lists[draw_parsed->draw_list_idx];
		addDrawableListRef(draw->draw_list MEM_DBG_PARMS_INIT);
	}

	if (draw_parsed->instance_param_list_idx >= 0)
	{
		assert(draw_parsed->instance_param_list_idx < eaSize(&streaming_pooled_info->instance_param_lists));
		draw->instance_param_list = streaming_pooled_info->instance_param_lists[draw_parsed->instance_param_list_idx];
		addInstanceParamListRef(draw->instance_param_list MEM_DBG_PARMS_INIT);
	}

	if (draw_parsed->fx_entry_id)
	{
		setupFXEntryDictionary(zmap);
		SET_HANDLE_FROM_INT(zmap->world_cell_data.fx_entry_dict_name, draw_parsed->fx_entry_id, draw->fx_parent_handle);
	}

	if (draw_parsed->animation_entry_id)
	{
		setupWorldAnimationEntryDictionary(zmap);
		SET_HANDLE_FROM_INT(zmap->world_cell_data.animation_entry_dict_name, draw_parsed->animation_entry_id, draw->animation_controller_handle);
	}
	if (animation_controller = GET_REF(draw->animation_controller_handle))
		draw->controller_relative_matrix_inited = worldAnimationEntryInitChildRelativeMatrix(animation_controller, draw_parsed->base_data.entry_bounds.world_matrix, draw->controller_relative_matrix);

	draw->occluder = draw_parsed->bitfield.occluder;
	draw->double_sided_occluder = draw_parsed->bitfield.double_sided_occluder;
	draw->unlit = draw_parsed->bitfield.unlit;
	draw->low_detail = draw_parsed->bitfield.low_detail;
	draw->high_detail = draw_parsed->bitfield.high_detail;
	draw->high_fill_detail = draw_parsed->bitfield.high_fill_detail;
	draw->map_snap_hidden = draw_parsed->bitfield.map_snap_hidden;
	draw->no_shadow_cast = draw_parsed->bitfield.no_shadow_cast;
	draw->no_shadow_receive = draw_parsed->bitfield.no_shadow_receive;
	draw->force_trunk_wind = draw_parsed->bitfield.force_trunk_wind;
	draw->no_vertex_lighting = draw_parsed->bitfield.no_vertex_lighting;
	draw->use_character_lighting = draw_parsed->bitfield.use_character_lighting;


	if (draw_parsed->fade_radius)
		draw->world_fade_radius = draw_parsed->fade_radius;
	else
		draw->world_fade_radius = draw->base_entry.shared_bounds->radius;

	if (draw_parsed->fade_mid_idx >= 0)
	{
		WorldCellLocalMidParsed *fade_mid;
		assert(draw_parsed->fade_mid_idx < eaSize(&streaming_pooled_info->packed_info->shared_local_mids));
		fade_mid = streaming_pooled_info->packed_info->shared_local_mids[draw_parsed->fade_mid_idx];
		mulVecMat4(fade_mid->local_mid, draw->base_entry.bounds.world_matrix, draw->world_fade_mid);
	}
	else
	{
		copyVec3(draw->base_entry.bounds.world_mid, draw->world_fade_mid);
	}

	if (!draw->no_vertex_lighting)
	{
		eaSetSize(&draw->lod_vertex_light_colors, eaSize(&draw_parsed->lod_vertex_light_colors));
		for(i = 0; i < eaSize(&draw_parsed->lod_vertex_light_colors); i++)
		{
			WorldDrawableEntryVertexLightColors* new_colors = malloc(sizeof(WorldDrawableEntryVertexLightColors));
			WorldDrawableEntryVertexLightColorsParsed* parsed_colors = draw_parsed->lod_vertex_light_colors[i];
			new_colors->multipler = parsed_colors->multipler;
			new_colors->offset = parsed_colors->offset;

			if (parsed_will_be_freed)
			{
				// transfer ownership;
				new_colors->vertex_light_colors = parsed_colors->vertex_light_colors;
				parsed_colors->vertex_light_colors = NULL;
			}
			else
			{
				new_colors->vertex_light_colors = NULL;
				ea32Copy(&new_colors->vertex_light_colors, &parsed_colors->vertex_light_colors);
			}

			draw->lod_vertex_light_colors[i] = new_colors;
		}
	}
}

int g_bDebugAllowCancelLoads = 0;
AUTO_CMD_INT(g_bDebugAllowCancelLoads,debugAllowCancelDrawList);

WorldDrawableList *worldCreateDrawableListDbg(Model *model, const Vec3 model_scale, const GroupInfo *info, GroupDef *def, 
											  const char *material_replace, const char ***texture_swaps, const char ***material_swaps, 
											  MaterialNamedTexture **named_texture_swaps, MaterialNamedConstant **material_constants,
											  WorldDrawableListPool *pool_const, 
											  int bWaitForLoad, WLUsageFlags use_flags,
											  WorldInstanceParamList **instance_param_list MEM_DBG_PARMS)
{
	NOCONST(WorldDrawableListPool) *pool = STRUCT_NOCONST(WorldDrawableListPool, pool_const);
	NOCONST(WorldDrawableList) *draw_list;
	int i, j;
	AutoLODTemplate **lod_distances = SAFE_MEMBER2(info, lod_override, lod_distances);
	F32 lod_scale = info ? info->lod_scale : 1.0f;

	if (info && info->ignore_lod_override)
		lod_distances = NULL;

	assert(eaSize(&model->model_lods));
	draw_list = allocDrawableList(pool, eaSize(&model->model_lods));

	if (model->lod_info && model->lod_info->high_detail_high_lod)
		draw_list->high_detail_high_lod = true;

	for (i = 0; i < draw_list->lod_count; ++i)
	{
		ModelLOD *lod_model = modelLODLoadAndMaybeWait(model, 0, bWaitForLoad);
		int material_swap_count = eaSize(material_swaps);
		int texture_swap_count = eaSize(texture_swaps);
		bool use_fallback_materials = false;
		NOCONST(WorldDrawableLod) *drawable_lod = &draw_list->drawable_lods[i];
		bool is_redirect = false;
		
		if ((g_bDebugAllowCancelLoads || !bWaitForLoad) && !lod_model)
		{
			// load not complete, cancel building the draw list
			freeDrawableList(STRUCT_RECONST(WorldDrawableList, draw_list), false);
			freeInstanceParamList(*instance_param_list, false);
			draw_list = NULL;
			*instance_param_list = NULL;
			break;
		}
		if (model->lod_info)
		{
			ModelLOD *lod_model2;
			AutoLOD *lod;

			ANALYSIS_ASSUME(model->lod_info->lods);
			lod = model->lod_info->lods[i];
			is_redirect = lod->modelname_specified;

			drawable_lod->no_fade = lodinfoGetDistances(model, 
				lod_distances, lod_scale, model_scale, i, &drawable_lod->near_dist,
				&drawable_lod->far_dist);
			drawable_lod->far_morph = lod->lod_farmorph;

			if (lod_model2 = modelLODLoadAndMaybeWait(model, i, bWaitForLoad))
				lod_model = lod_model2;

			if ((g_bDebugAllowCancelLoads || !bWaitForLoad) && !lod_model2)
			{
				// load not complete, cancel building the draw list
				freeDrawableList(STRUCT_RECONST(WorldDrawableList, draw_list), false);
				freeInstanceParamList(*instance_param_list, false);
				draw_list = NULL;
				*instance_param_list = NULL;
				break;
			}

			for (j = 0; j < eaSize(&lod->material_swaps); ++j)
			{
				eaPush(material_swaps, lod->material_swaps[j]->orig_name);
				eaPush(material_swaps, lod->material_swaps[j]->replace_name);
			}

			for (j = 0; j < eaSize(&lod->texture_swaps); ++j)
			{
				eaPush(texture_swaps, lod->texture_swaps[j]->orig_name);
				eaPush(texture_swaps, lod->texture_swaps[j]->replace_name);
			}

			use_fallback_materials = lod->use_fallback_materials;
		}
		else
		{
			drawable_lod->near_dist = drawable_lod->far_dist = 5000;
		}

		drawable_lod->subobject_count = SAFE_MEMBER2(lod_model, data, tex_count);

		if (lod_model)
		{
			U32 bit;

			if (!(*instance_param_list))
				(*instance_param_list) = allocInstanceParamList(pool, draw_list->lod_count);

			(*instance_param_list)->lod_params[i].subobject_count = drawable_lod->subobject_count;
			(*instance_param_list)->lod_params[i].subobject_params = calloc(sizeof((*instance_param_list)->lod_params[i].subobject_params[0]), (*instance_param_list)->lod_params[i].subobject_count);

			drawable_lod->subobjects = calloc(drawable_lod->subobject_count, sizeof(WorldDrawableSubobject *));

			for (j = 0, bit = 1; j < drawable_lod->subobject_count; ++j, bit <<= 1)
			{
				drawable_lod->subobjects[j] = applyMaterialSwaps(pool, lod_model, is_redirect ? -1 : i, j, def, 
													&(*instance_param_list)->lod_params[i].subobject_params[j], &draw_list->no_fog, 
													lod_model->materials, 
													*texture_swaps, *material_swaps, material_replace, 
													named_texture_swaps, material_constants, 
													use_fallback_materials, use_flags MEM_DBG_PARMS_CALL);

				if (drawable_lod->subobjects[j]->material_draws[0]->is_occluder)
					drawable_lod->occlusion_materials |= bit;
			}
		}

		if (*material_swaps)
			eaSetSize(material_swaps, material_swap_count);
		if (*texture_swaps)
			eaSetSize(texture_swaps, texture_swap_count);
	}

	if (pool)
	{
		if (draw_list)
			draw_list = lookupDrawableList(draw_list MEM_DBG_PARMS_CALL);
		if (*instance_param_list)
			*instance_param_list = lookupInstanceParamList(*instance_param_list);
	}

	return STRUCT_RECONST(WorldDrawableList, draw_list);
}

static void initDrawableDataForModel(WorldDrawableEntry *draw, Model *model, const Vec3 model_scale, 
									 const GroupInfo *info, GroupDef *def, 
									 const char ***texture_swaps, const char ***material_swaps, 
									 MaterialNamedConstant **material_constants)
{
	ZoneMap *zmap = info ? info->zmap : worldGetActiveMap();
	F32 near_lod_near_dist, far_lod_near_dist, far_lod_far_dist;

	draw->draw_list = worldCreateDrawableList(	model, model_scale, info, def, 
												SAFE_MEMBER(info, material_replace), texture_swaps, material_swaps, 
												NULL, material_constants,
												&zmap->world_cell_data.drawable_pool, true, WL_FOR_WORLD, &draw->instance_param_list);

	far_lod_far_dist = loddistFromLODInfo(model, NULL, 
										  (info && !info->ignore_lod_override) ? SAFE_MEMBER(info->lod_override, lod_distances) : NULL, 
										  SAFE_MEMBER(info, lod_scale), model_scale, true, &near_lod_near_dist, &far_lod_near_dist);

	assert(!draw->base_entry.shared_bounds);
	draw->base_entry.shared_bounds = createSharedBoundsSphere(zmap, SAFE_MEMBER(info, radius), near_lod_near_dist, far_lod_near_dist, far_lod_far_dist);
}

WorldDrawableEntry *createWorldModelEntry(GroupDef *def, Model *model, const GroupInfo *info, 
										  const char ***texture_swaps, const char ***material_swaps, 
										  MaterialNamedConstant **material_constants, 
										  bool is_occluder, const Vec3 model_scale, 
										  const WorldAtmosphereProperties *atmosphere)
{
	WorldModelEntry *entry = calloc(1, sizeof(WorldModelEntry));
	WorldDrawableEntry *draw = &entry->base_drawable_entry;
	WorldCellEntryBounds *bounds = &draw->base_entry.bounds;

	entry->base_drawable_entry.base_entry.type = WCENT_MODEL;

	entry->model_tracker.fade_out_lod = entry->model_tracker.fade_in_lod = -1;

	draw->occluder = is_occluder && !info->high_fill_detail && !info->high_detail && !info->low_detail && !info->editor_only;
	draw->double_sided_occluder = draw->occluder && info->double_sided_occluder;
	draw->no_vertex_lighting = !!def->property_structs.physical_properties.bNoVertexLighting;
	draw->use_character_lighting = !!def->property_structs.physical_properties.bUseCharacterLighting;

	initDrawableDataForModel(draw, model, model_scale, info, def, 
							 texture_swaps, material_swaps, 
							 material_constants);

	entry->wind_params[0] = def->property_structs.wind_properties.fEffectAmount;
	entry->wind_params[1] = def->property_structs.wind_properties.fBendiness;
	entry->wind_params[2] = def->property_structs.wind_properties.fPivotOffset;
	entry->wind_params[3] = def->property_structs.wind_properties.fRustling;

	copyVec3(model_scale, entry->model_scale);
	entry->scaled = !sameVec3InMem(entry->model_scale, unitvec3);
	copyMat4(info->world_matrix, bounds->world_matrix);

	if (nearSameVec3(bounds->world_matrix[0], zerovec3) ||
		nearSameVec3(bounds->world_matrix[1], zerovec3) ||
		nearSameVec3(bounds->world_matrix[2], zerovec3) ||
		fabs(mat3Determinant(bounds->world_matrix)) < 0.0001f)
	{
		ErrorFilenamef(def->filename, "Model entry for %s (%X) has invalid world matrix.", def->name_str, def->name_uid);
		identityMat4(bounds->world_matrix);
	}

	return draw;
}

WorldCellEntry *createWorldModelEntryFromParsed(ZoneMap *zmap, WorldModelEntryParsed *entry_parsed, WorldCell *cell_debug, bool parsed_will_be_freed)
{
	WorldModelEntry *entry = calloc(1, sizeof(WorldModelEntry));

	entry->base_drawable_entry.base_entry.type = WCENT_MODEL;

	worldEntryInitBoundsFromParsed(zmap->world_cell_data.streaming_pooled_info, &entry->base_drawable_entry.base_entry, &entry_parsed->base_drawable.base_data);
	copyDrawableDataFromParsed(zmap, &entry->base_drawable_entry, &entry_parsed->base_drawable, cell_debug, parsed_will_be_freed);

	entry->model_tracker.fade_out_lod = entry->model_tracker.fade_in_lod = -1;
	entry->base_drawable_entry.should_cluster = entry_parsed->base_drawable.is_clustered ? CLUSTERED : NON_CLUSTERED;

	copyVec3(entry_parsed->model_scale, entry->model_scale);
	entry->scaled = !sameVec3InMem(entry->model_scale, unitvec3);

	copyVec4(entry_parsed->wind_params, entry->wind_params);

	return &entry->base_drawable_entry.base_entry;
}

WorldModelInstanceInfo *createInstanceInfo(const Mat4 world_matrix, const Vec3 tint_color, const Model *model, WorldPhysicalProperties *props, GroupDef *def)
{
	WorldModelInstanceInfo *ret = calloc(1, sizeof(WorldModelInstanceInfo));
	Vec3 scale_vec;
	F32 max_scale;

	getMatRow(world_matrix, 0, ret->world_matrix_x);
	getMatRow(world_matrix, 1, ret->world_matrix_y);
	getMatRow(world_matrix, 2, ret->world_matrix_z);

	if (nearSameVec3(ret->world_matrix_x, zerovec3) ||
		nearSameVec3(ret->world_matrix_y, zerovec3) ||
		nearSameVec3(ret->world_matrix_z, zerovec3) ||
		fabs(mat3Determinant(world_matrix)) < 0.0001f)
	{
		ErrorFilenamef(def->filename, "Group %s (%X) has invalid world matrix.", def->name_str, def->name_uid);
		setVec3(ret->world_matrix_x, 1, 0, 0);
		setVec3(ret->world_matrix_y, 0, 1, 0);
		setVec3(ret->world_matrix_z, 0, 0, 1);
	}

	copyVec3(tint_color, ret->tint_color);
	ret->tint_color[3] = 1;

	mulVecMat4(model->mid, world_matrix, ret->world_mid);

	getScale(world_matrix, scale_vec);
	max_scale = MAX(scale_vec[0], scale_vec[1]);
	MAX1(max_scale, scale_vec[2]);

	ret->inst_radius = quantBoundsMax(model->radius * max_scale);

	ret->camera_facing = props->bCameraFacing;
	ret->axis_camera_facing = props->bAxisCameraFacing;

	return ret;
}

WorldModelInstanceInfo *createInstanceInfoFromModelEntry(const WorldModelEntry *model_entry)
{
	WorldModelInstanceInfo *ret = calloc(1, sizeof(WorldModelInstanceInfo));
	Mat4 world_matrix;
	F32 max_scale;

	copyMat4(model_entry->base_drawable_entry.base_entry.bounds.world_matrix, world_matrix);
	scaleMat3Vec3(world_matrix, model_entry->model_scale);

	getMatRow(world_matrix, 0, ret->world_matrix_x);
	getMatRow(world_matrix, 1, ret->world_matrix_y);
	getMatRow(world_matrix, 2, ret->world_matrix_z);

	if (nearSameVec3(ret->world_matrix_x, zerovec3) ||
		nearSameVec3(ret->world_matrix_y, zerovec3) ||
		nearSameVec3(ret->world_matrix_z, zerovec3) ||
		fabs(mat3Determinant(world_matrix)) < 0.0001f)
	{
		Errorf("Model entry has invalid world matrix.");
		setVec3(ret->world_matrix_x, 1, 0, 0);
		setVec3(ret->world_matrix_y, 0, 1, 0);
		setVec3(ret->world_matrix_z, 0, 0, 1);
	}

	copyVec4(model_entry->base_drawable_entry.color, ret->tint_color);

	copyVec3(model_entry->base_drawable_entry.world_fade_mid, ret->world_mid);

	max_scale = MAX(model_entry->model_scale[0], model_entry->model_scale[1]);
	MAX1(max_scale, model_entry->model_scale[2]);
	ret->inst_radius = quantBoundsMax(model_entry->base_drawable_entry.world_fade_radius);

	ret->camera_facing = model_entry->base_drawable_entry.camera_facing;
	ret->axis_camera_facing = model_entry->base_drawable_entry.axis_camera_facing;

	ret->instance_param_list = model_entry->base_drawable_entry.instance_param_list;
	addInstanceParamListRef(ret->instance_param_list MEM_DBG_PARMS_INIT);

	return ret;
}

WorldModelInstanceInfo *worldDupModelInstanceInfo(WorldModelInstanceInfo *info)
{
	WorldModelInstanceInfo *ret = malloc(sizeof(WorldModelInstanceInfo));

	memcpy(ret, info, sizeof(WorldModelInstanceInfo));
	addInstanceParamListRef(ret->instance_param_list MEM_DBG_PARMS_INIT);

	return ret;
}

void worldFreeModelInstanceInfo(WorldModelInstanceInfo *info)
{
	if (!info)
		return;

	removeInstanceParamListRef(info->instance_param_list MEM_DBG_PARMS_INIT);
	free(info);
}

void initWorldModelInstanceEntry(WorldModelInstanceEntry *entry, GroupDef *def, const GroupInfo *info, 
								 const char ***texture_swaps, const char ***material_swaps, 
								 MaterialNamedConstant **material_constants)
{
	WorldDrawableEntry *draw = &entry->base_drawable_entry;
	WorldCellEntryBounds *bounds = &draw->base_entry.bounds;
	Model *model = entry->model;

	assert(model);

	entry->base_drawable_entry.base_entry.type = WCENT_MODELINSTANCED;

	draw->occluder = false;
	draw->double_sided_occluder = false;

	initDrawableDataForModel(draw, model, NULL, info, def, texture_swaps, material_swaps, material_constants);

	copyMat4(info->world_matrix, bounds->world_matrix);
}

WorldCellEntry *createWorldModelInstanceEntryFromParsed(ZoneMap *zmap, WorldModelInstanceEntryParsed *entry_parsed, bool parsed_will_be_freed)
{
	WorldModelInstanceEntry *entry = calloc(1, sizeof(WorldModelInstanceEntry));
	int i;

	entry->base_drawable_entry.base_entry.type = WCENT_MODELINSTANCED;
	entry->base_drawable_entry.should_cluster = entry_parsed->base_drawable.is_clustered ? CLUSTERED : NON_CLUSTERED;

	worldEntryInitBoundsFromParsed(zmap->world_cell_data.streaming_pooled_info, &entry->base_drawable_entry.base_entry, &entry_parsed->base_drawable.base_data);
	copyDrawableDataFromParsed(zmap, &entry->base_drawable_entry, &entry_parsed->base_drawable, NULL, parsed_will_be_freed);

	eaSetSize(&entry->instances, eaSize(&entry_parsed->instances));
	for (i = 0; i < eaSize(&entry_parsed->instances); ++i)
	{
		WorldModelInstanceInfo *instance_dst = calloc(1, sizeof(WorldModelInstanceInfo));
		WorldModelInstanceInfoParsed *instance_src = entry_parsed->instances[i];
		Mat4 instance_matrix;

		copyVec4(instance_src->world_matrix_x, instance_dst->world_matrix_x);
		copyVec4(instance_src->world_matrix_y, instance_dst->world_matrix_y);
		copyVec4(instance_src->world_matrix_z, instance_dst->world_matrix_z);

		copyVec4(instance_src->tint_color, instance_dst->tint_color);
		instance_dst->inst_radius = instance_src->inst_radius;
		instance_dst->axis_camera_facing = instance_src->axis_camera_facing;
		instance_dst->camera_facing = instance_src->camera_facing;

		setMatRow(instance_matrix, 0, instance_dst->world_matrix_x);
		setMatRow(instance_matrix, 1, instance_dst->world_matrix_y);
		setMatRow(instance_matrix, 2, instance_dst->world_matrix_z);

		assert(instance_src->local_mid_idx < eaSize(&zmap->world_cell_data.streaming_pooled_info->packed_info->shared_local_mids) && zmap->world_cell_data.streaming_pooled_info->packed_info->shared_local_mids);
		mulVecMat4(zmap->world_cell_data.streaming_pooled_info->packed_info->shared_local_mids[instance_src->local_mid_idx]->local_mid, instance_matrix, instance_dst->world_mid);

		if (instance_src->instance_param_list_idx >= 0)
		{
			assert(instance_src->instance_param_list_idx < eaSize(&zmap->world_cell_data.streaming_pooled_info->instance_param_lists));
			instance_dst->instance_param_list = zmap->world_cell_data.streaming_pooled_info->instance_param_lists[instance_src->instance_param_list_idx];
			addInstanceParamListRef(instance_dst->instance_param_list MEM_DBG_PARMS_INIT);
		}

		entry->instances[i] = instance_dst;
	}

	copyVec4(entry_parsed->wind_params, entry->wind_params);
	entry->lod_idx = entry_parsed->lod_idx;

	return &entry->base_drawable_entry.base_entry;
}

WorldDrawableEntry *createWorldSplinedModelEntry(GroupDef *def, Model *model, const GroupInfo *info, 
												 const char ***texture_swaps, const char ***material_swaps, 
												 MaterialNamedConstant **material_constants)
{
	WorldSplinedModelEntry *entry = calloc(1, sizeof(WorldSplinedModelEntry));
	WorldDrawableEntry *draw = &entry->base_drawable_entry;
	WorldCellEntryBounds *bounds = &draw->base_entry.bounds;

	entry->base_drawable_entry.base_entry.type = WCENT_SPLINE;
	entry->model_tracker.fade_out_lod = entry->model_tracker.fade_in_lod = -1;
	mat4toSkinningMat4(info->spline->spline_matrices[0], entry->spline_mats[0]);
	mat4toSkinningMat4(info->spline->spline_matrices[1], entry->spline_mats[1]);

	initDrawableDataForModel(draw, model, NULL, info, def, texture_swaps, material_swaps, material_constants);

	copyMat4(info->world_matrix, bounds->world_matrix);

	return draw;
}

WorldCellEntry *createWorldSplinedModelEntryFromParsed(ZoneMap *zmap, WorldSplinedModelEntryParsed *entry_parsed, bool parsed_will_be_freed)
{
	WorldSplinedModelEntry *entry = calloc(1, sizeof(WorldSplinedModelEntry));

	entry->base_drawable_entry.base_entry.type = WCENT_SPLINE;

	worldEntryInitBoundsFromParsed(zmap->world_cell_data.streaming_pooled_info, &entry->base_drawable_entry.base_entry, &entry_parsed->base_drawable.base_data);
	copyDrawableDataFromParsed(zmap, &entry->base_drawable_entry, &entry_parsed->base_drawable, NULL, parsed_will_be_freed);

	entry->model_tracker.fade_out_lod = entry->model_tracker.fade_in_lod = -1;

	setMat34Col(entry->spline_mats[0], 0, entry_parsed->spline.param0);
	setMat34Col(entry->spline_mats[0], 1, entry_parsed->spline.param1);
	setMat34Col(entry->spline_mats[0], 2, entry_parsed->spline.param2);
	setMat34Col(entry->spline_mats[0], 3, entry_parsed->spline.param3);

	setMat34Col(entry->spline_mats[1], 0, entry_parsed->spline.param4);
	setMat34Col(entry->spline_mats[1], 1, entry_parsed->spline.param5);
	setMat34Col(entry->spline_mats[1], 2, entry_parsed->spline.param6);
	setMat34Col(entry->spline_mats[1], 3, entry_parsed->spline.param7);

	return &entry->base_drawable_entry.base_entry;
}

WorldDrawableEntry *createWorldOcclusionEntry(GroupDef *def, GroupDef *parent_volume_def, Model *customOccluderModel, GroupInfo *info, Vec4 tint_color, bool no_occlusion, bool in_editor, bool doubleSideOccluder)
{
	WorldOcclusionEntry *entry = calloc(1, sizeof(WorldOcclusionEntry));
	WorldDrawableEntry *draw = &entry->base_drawable_entry;
	WorldCellEntryBounds *bounds = &draw->base_entry.bounds;
	Model *model = def->model;
	bool is_volume = !!(def->property_structs.volume);
	F32 vis_dist;

	if (!parent_volume_def)
		parent_volume_def = def;

	// volume or occlusion geometry
	entry->base_drawable_entry.base_entry.type = WCENT_OCCLUSION;

	if (!is_volume)
	{
		// presence of custom occluder implies bOcclusionOnly for this entry, otherwise, use prior logic
		entry->model = customOccluderModel ? 
			customOccluderModel : 
			(def->property_structs.physical_properties.bOcclusionOnly?model:NULL);
	}
	entry->volume_faces = (groupIsVolumeType(parent_volume_def, "Occluder")?parent_volume_def->property_structs.physical_properties.iOccluderFaces:0);

	if (groupIsVolumeType(def, "Occluder"))
		entry->type_flags |= VOL_TYPE_OCCLUDER;
	if (def->property_structs.sound_sphere_properties)
		entry->type_flags |= VOL_TYPE_SOUND;
	if (groupIsVolumeType(def, "SkyFade"))
		entry->type_flags |= VOL_TYPE_SKY;
	if (parent_volume_def->property_structs.server_volume.neighborhood_volume_properties)
		entry->type_flags |= VOL_TYPE_NEIGHBORHOOD;
	if (parent_volume_def->property_structs.server_volume.interaction_volume_properties)
		entry->type_flags |= VOL_TYPE_INTERACTION;
	if (parent_volume_def->property_structs.server_volume.landmark_volume_properties)
		entry->type_flags |= VOL_TYPE_LANDMARK;
	if (parent_volume_def->property_structs.server_volume.power_volume_properties)
		entry->type_flags |= VOL_TYPE_POWER;
	if (parent_volume_def->property_structs.server_volume.warp_volume_properties)
		entry->type_flags |= VOL_TYPE_WARP;
	if (groupIsVolumeType(parent_volume_def, "GenesisNode"))
		entry->type_flags |= VOL_TYPE_GENESIS;
	if (groupIsVolumeType(parent_volume_def, "TerrainFilter"))
		entry->type_flags |= VOL_TYPE_BLOB_FILTER;
	if (groupIsVolumeType(parent_volume_def, "TerrainExclusion"))
		entry->type_flags |= VOL_TYPE_EXCLUSION;

	draw->occluder = (entry->model || groupIsVolumeType(parent_volume_def, "Occluder")) && !no_occlusion;
	draw->double_sided_occluder = (draw->occluder && info->double_sided_occluder) || doubleSideOccluder;

	if (in_editor)
		vis_dist = info->radius * WORLD_OCCLUSION_VISDIST_EDITOR_MULTIPLIER;
	else
		vis_dist = (info->radius + WORLD_OCCLUSION_VISDIST_ADDER) * WORLD_OCCLUSION_VISDIST_MULTIPLIER;
	
	assert(!draw->base_entry.shared_bounds);
	draw->base_entry.shared_bounds = createSharedBoundsSphere(info->zmap, info->radius, 0, vis_dist, vis_dist);

	copyMat4(info->world_matrix, bounds->world_matrix);

	if (def->group_flags & GRP_HAS_TINT)
	{
		copyVec3(info->color, tint_color);
		tint_color[3] = 0.5f;
		entry->type_flags |= VOL_TYPE_TINTED;
	}
	else
		setVec4(tint_color, 1.0f, 0, 0.5f, 0.5f); // R_b

	if (is_volume)
	{
		if (def->property_structs.volume->eShape == GVS_Sphere)
		{
			entry->volume_radius = def->property_structs.volume->fSphereRadius;
		}
		else
		{
			copyVec3(def->property_structs.volume->vBoxMin, entry->volume_min);
			copyVec3(def->property_structs.volume->vBoxMax, entry->volume_max);
		}

	} else {

		worldOcclusionEntrySetScale(entry, def->model_scale);
	}


	return draw;
}

WorldOcclusionEntry *createWorldOcclusionEntryFromMesh(GMesh *mesh, const Mat4 world_matrix, bool double_sided)
{
	WorldOcclusionEntry *entry = calloc(1, sizeof(WorldOcclusionEntry));
	WorldDrawableEntry *draw = &entry->base_drawable_entry;
	WorldCellEntryBounds *bounds = &draw->base_entry.bounds;
	Vec3 local_mid, local_min, local_max;
	F32 radius, vis_dist;

	// occlusion geometry
	entry->base_drawable_entry.base_entry.type = WCENT_OCCLUSION;

	draw->occluder = 1;
	draw->double_sided_occluder = double_sided;

	radius = gmeshGetBounds(mesh, local_min, local_max, local_mid);
	mulVecMat4(local_mid, world_matrix, bounds->world_mid);
	vis_dist = (radius + WORLD_OCCLUSION_VISDIST_ADDER) * WORLD_OCCLUSION_VISDIST_MULTIPLIER;
	draw->base_entry.shared_bounds = createSharedBounds(worldGetActiveMap(), NULL, local_min, local_max, radius, 0, vis_dist, vis_dist);

	copyMat4(world_matrix, bounds->world_matrix);

	setVec4(draw->color, 0.5, 1, 0.5f, 0.5f);

	// create model
	entry->model = tempModelAlloc("Room Occlusion", &default_material, 1, WL_FOR_WORLD);
	modelFromGmesh(entry->model, mesh);
	gmeshFreeData(mesh);
	free(mesh);
	entry->owns_model = true;

	return entry;
}

WorldCellEntry *createWorldOcclusionEntryFromParsed(ZoneMap *zmap, WorldOcclusionEntryParsed *entry_parsed)
{
	WorldOcclusionEntry *entry = calloc(1, sizeof(WorldOcclusionEntry));

	entry->base_drawable_entry.base_entry.type = WCENT_OCCLUSION;

	worldEntryInitBoundsFromParsed(zmap->world_cell_data.streaming_pooled_info, &entry->base_drawable_entry.base_entry, &entry_parsed->base_data);

	entry->base_drawable_entry.occluder = entry_parsed->occluder;
	entry->base_drawable_entry.double_sided_occluder = entry_parsed->double_sided_occluder;

	entry->volume_faces = entry_parsed->volume_faces;
	entry->type_flags = entry_parsed->type_flags;
	entry->volume_radius = entry_parsed->volume_radius;
	copyVec3(entry_parsed->volume_min, entry->volume_min);
	copyVec3(entry_parsed->volume_max, entry->volume_max);

	copyVec3(entry->base_drawable_entry.base_entry.bounds.world_mid, entry->base_drawable_entry.world_fade_mid);
	entry->base_drawable_entry.world_fade_radius = entry->base_drawable_entry.base_entry.shared_bounds->radius;

	setVec4(entry->base_drawable_entry.color, 1.0f, 0, 0.5f, 0.5f); // R_b

	if (entry_parsed->gmesh)
	{
		// Make this faster by making a modelFromParsedFormat convert directly
		GMesh *gmesh = gmeshFromParsedFormat(entry_parsed->gmesh);
		if (gmesh)
		{
			entry->model = tempModelAlloc("Occlusion (From Parsed)", &default_material, 1, WL_FOR_WORLD);
			modelFromGmesh(entry->model, gmesh);
			gmeshFreeData(gmesh);
			free(gmesh);

			entry->owns_model = true;
		}
	}
	else
	{
		entry->model = groupModelFind(worldGetStreamedString(zmap->world_cell_data.streaming_pooled_info, entry_parsed->model_idx), 0);
	}

	return &entry->base_drawable_entry.base_entry;
}

const Model *worldDrawableEntryGetModel(WorldDrawableEntry *draw, int *idx, Mat4 spline_mats[2], bool *has_spline, F32 **model_scale)
{
	int i, j, start_idx;

	if (idx)
		*idx = 0;

	if (has_spline)
		*has_spline = false;

	if (model_scale)
		*model_scale = NULL;

	if (draw->base_entry.type == WCENT_MODEL)
	{
		if (((WorldModelEntry *)draw)->scaled && model_scale)
			*model_scale = ((WorldModelEntry *)draw)->model_scale;
	}
	else if (draw->base_entry.type == WCENT_SPLINE)
	{
		if (spline_mats && has_spline)
		{
			SkinningMat4 *spline_mats_src = ((WorldSplinedModelEntry *)draw)->spline_mats;
			skinningMat4toMat4(spline_mats_src[0], spline_mats[0]);
			skinningMat4toMat4(spline_mats_src[1], spline_mats[1]);
			*has_spline = true;
		}
	}
	else if (draw->base_entry.type == WCENT_OCCLUSION)
	{
		if (((WorldOcclusionEntry*)draw)->model && (((WorldOcclusionEntry *)draw)->owns_model || modelIsTemp(((WorldOcclusionEntry*)draw)->model)))
			return NULL;
		return ((WorldOcclusionEntry*)draw)->model;
	}
	else if (draw->base_entry.type == WCENT_MODELINSTANCED && ((WorldModelInstanceEntry*)draw)->model)
	{
		if (idx)
			*idx = ((WorldModelInstanceEntry*)draw)->lod_idx;
		return ((WorldModelInstanceEntry*)draw)->model;
	}

	if (!draw->draw_list)
		return NULL;

	if (draw->base_entry.type == WCENT_MODELINSTANCED)
		start_idx = ((WorldModelInstanceEntry*)draw)->lod_idx;
	else
		start_idx = 0;

	for (i = start_idx; i < draw->draw_list->lod_count; ++i)
	{
		WorldDrawableLod *lod = &draw->draw_list->drawable_lods[i];
		for (j = 0; j < lod->subobject_count; ++j)
		{
			if (lod->subobjects[j]->model && lod->subobjects[j]->model->model)
			{
				if (idx)
					*idx = lod->subobjects[j]->model->lod_idx;
				return lod->subobjects[j]->model->model;
			}
		}
	}

	return NULL;
}
