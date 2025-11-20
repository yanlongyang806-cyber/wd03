#include "wlModelData.h"
#include "wlModelLoad.h"
#include "wlModelInline.h"
#include "Materials.h"
#include "timing.h"
#include "qsortG.h"
#include "UnitSpec.h"
#include "GenericMesh.h"
#include "memlog.h"
#include "serialize.h"
#include "textparser.h"
#include "bounds.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art););

static Model **temp_models; // Protected by model_lod_init_cs
extern StashTable ht_models; // Unpacked models, Protected by model_lod_init_cs

const char * modelLODGetDebugName(ModelLOD *model)
{
	return model->debug_name;
}

const char * modelLODGetModelName(ModelLOD *model)
{
	return model->model_parent->name;
}

Material *modelGetMaterialByTri(ModelLOD *model, U32 tri)
{
	int j = 0;
	if (!model || !model->data)
		return NULL;

	while (j < model->data->tex_count && tri >= model->data->tex_idx[j].count)
	{
		tri -= model->data->tex_idx[j].count;
		j++;
	}

	if (j < model->data->tex_count)
		return model->materials[model->data->tex_idx[j].id];

	return NULL;
}

Material *modelGetCollisionMaterialByTriEx(Model *model, U32 tri, const char **matSwaps)
{
	int j = 0;
	if (!model || !(model->collision_data.loadstate == GEO_LOADED || model->collision_data.loadstate == GEO_LOADED_LOST_DATA))
		return NULL;

	while (j < model->collision_data.tex_count && tri >= model->collision_data.tex_idx[j].count)
	{
		tri -= model->collision_data.tex_idx[j].count;
		j++;
	}

	// TODO: cache the materialFind result on the Model*?

	if (j < model->collision_data.tex_count)
	{
		const char *matName = model->collision_data.tex_names[model->collision_data.tex_idx[j].id];
		Material *defaultMat = materialFindNoDefault(matName, 0);
		Material *swapMat = NULL;

		if(matSwaps)
		{
			int i;
			int count = eaSize(&matSwaps);

			for(i = 0; i < (count - 1); i+=2)
				if(stricmp(matSwaps[i], matName) == 0)
				{
					swapMat = materialFindNoDefault(matSwaps[i + 1], 0);
					break;
				}
		}

		return swapMat ? swapMat : defaultMat;
	}

	return NULL;
}

static ModelFreeAllCallback wlModelFreeAllCallback;
void modelSetFreeAllCallback(ModelFreeAllCallback callback)
{
	wlModelFreeAllCallback = callback;
}

static void modelFreeColData(Model *model, bool bWillFreeModel)
{
	GeoLoadState loadstate = model->collision_data.loadstate;
	if (loadstate == GEO_LOADED || loadstate == GEO_LOADED_LOST_DATA || (loadstate == GEO_LOADING && bWillFreeModel))
	{
		if (loadstate == GEO_LOADING && bWillFreeModel)
		{
			PERFINFO_AUTO_START("Freeing Model Col Data which is still loading", 1);
			// Wait for it to finish loading
			while (model->collision_data.loadstate == GEO_LOADING)
				Sleep(1);
			PERFINFO_AUTO_STOP();
		}
		EnterCriticalSection(&model_lod_init_cs);
		if (model->collision_data.loadstate == GEO_LOADED || model->collision_data.loadstate == GEO_LOADED_LOST_DATA)
		{
			SAFE_FREE(model->collision_data.unpacked_data);
			model->collision_data.unpacked_size = 0;
			SAFE_FREE(model->collision_data.tex_idx);
			SAFE_FREE(*(char***)&model->collision_data.tex_names);
			model->collision_data.loadstate = GEO_NOT_LOADED;
		}
		LeaveCriticalSection(&model_lod_init_cs);
	}

	eaDestroyEx(&model->psdkScaledConvexMeshes, modelDestroyScaledCollision);
	eaDestroyEx(&model->psdkScaledTriangleMeshes, modelDestroyScaledCollision);
}

void modelFreeData(Model *model)
{
	int i;
	for (i=0; i<eaSize(&model->model_lods); i++)
		modelLODFreeData(model->model_lods[i], false);

	modelFreeColData(model, false);
}

bool modelDestroy(Model *model)
{
	int i;
	bool bCanDestroy=true;
	// Decrease references
	for (i=0; i<eaSize(&model->model_lods); i++)
	{
		if (model->model_lods[i]->actual_lod)
			model->model_lods[i]->actual_lod->reference_count--;
	}
	// Check references
	for (i=0; i<eaSize(&model->model_lods); i++)
	{
		if (model->model_lods[i]->reference_count)
			bCanDestroy = false;
		if (model->model_lods[i]->loadstate == GEO_LOADING)
			bCanDestroy = false; // Can't free it!
	}
	if (!bCanDestroy)
	{
		// Reset references
		for (i=0; i<eaSize(&model->model_lods); i++)
		{
			if (model->model_lods[i]->actual_lod)
				model->model_lods[i]->actual_lod->reference_count++;
		}
		return false;
	}

	eaDestroyEx(&model->model_lods, modelLODDestroy);

	modelFreeColData(model, true);

	// Remove from dictionary
	EnterCriticalSection(&model_lod_init_cs);
	{
		Model *oldmodel;
		verify(stashRemovePointer(ht_models, model->name, &oldmodel));
		assert(oldmodel == model);
	}
	LeaveCriticalSection(&model_lod_init_cs);
	free(model);
	return true;
}

void modelForEachModel(ModelCallback callback, intptr_t userdata, bool includeTempModels)
{
	PERFINFO_AUTO_START_FUNC();

	EnterCriticalSection(&model_lod_init_cs);
	FOR_EACH_IN_STASHTABLE(ht_models, Model, model)
	{
		if (!callback(model, userdata))
			break;
		// model* might be invalid after callback
	}
	FOR_EACH_END;

	if (includeTempModels)
	{
		FOR_EACH_IN_EARRAY(temp_models, Model, model)
		{
			if (!callback(model, userdata))
				break;
		}
		FOR_EACH_END;
	}
	LeaveCriticalSection(&model_lod_init_cs);

	PERFINFO_AUTO_STOP();
}

void modelForEachModelLOD(ModelLODCallback callback, intptr_t userdata, bool includeTempModels)
{
	bool stop=false;
	ModelLOD *walk;
	PERFINFO_AUTO_START_FUNC();

	for (walk = model_lod_list_head; walk; walk = walk->next)
	{
		if (!callback(walk, userdata))
			break;
	}

	if (includeTempModels)
	{
		EnterCriticalSection(&model_lod_init_cs);
		FOR_EACH_IN_EARRAY(temp_models, Model, model)
		{
			FOR_EACH_IN_EARRAY(model->model_lods, ModelLOD, model_lod)
			{
				if (!callback(model_lod, userdata))
				{
					stop = true;
					break;
				}
			}
			FOR_EACH_END;
			if (stop)
				break;
		}
		FOR_EACH_END;
		LeaveCriticalSection(&model_lod_init_cs);
	}

	PERFINFO_AUTO_STOP();
}

static bool modelFreeAllUnpackedCallback(ModelLOD *model_lod, intptr_t userData)
{
	modelLODFreeUnpacked(model_lod);
	return true;
}

AUTO_COMMAND ACMD_NAME(ModelUnloadUnpacked);
void modelFreeAllUnpacked(void)
{
	geoForceBackgroundLoaderToFinish();

	// Free z-occlusion model references
	if (wlModelFreeAllCallback)
		wlModelFreeAllCallback();

	modelForEachModelLOD(modelFreeAllUnpackedCallback, 0, false);
}

typedef struct ModelFreeAllCacheCallbackData
{
	bool didCallback;
	WLUsageFlags unuse_flags;
} ModelFreeAllCacheCallbackData;

static bool modelFreeAllCacheCallback(Model *model, intptr_t userData)
{
	int i;
	ModelFreeAllCacheCallbackData *data = (ModelFreeAllCacheCallbackData*)userData;
	if (!data->didCallback)
	{
		data->didCallback = true;
		geoForceBackgroundLoaderToFinish();
		if (wlModelFreeAllCallback)
			wlModelFreeAllCallback();
	}

	model->use_flags &= ~data->unuse_flags;
	if (!model->use_flags)
	{
		// Nothing is referencing this model anymore, free it!
		modelDestroy(model);
	} else {
		// Just free the data, leave the structures around
		for (i=0; i<eaSize(&model->model_lods); i++) 
			modelLODFreeData(model->model_lods[i], false);
		
		modelFreeColData(model, false);
	}
	return true;
}

// Free all data we can free - unpacked, packed, collision, etc
void modelFreeAllCache(WLUsageFlags unuse_type)
{
	ModelFreeAllCacheCallbackData data = {0};
	data.didCallback = false;
	data.unuse_flags = unuse_type;

	PERFINFO_AUTO_START("modelFreeAllCache", 1);

	modelForEachModel(modelFreeAllCacheCallback, (intptr_t)&data, false);

	PERFINFO_AUTO_STOP();
}

static bool modelUnloadCheckCallback(ModelLOD *model_lod, intptr_t userData)
{
	if (model_lod->unpacked_last_used_timestamp && !model_lod->unpacked_in_use)
	{
		U32 delta = wl_frame_timestamp - model_lod->unpacked_last_used_timestamp;
		if (delta >= (U32)userData)
		{
			memlog_printf(geoGetMemLog(), "%u: Unloading model unpacked data %s/%s, age of %5.2f", wl_frame_timestamp, model_lod->model_parent->header->filename, model_lod->model_parent->name, (float)(wl_frame_timestamp - model_lod->unpacked_last_used_timestamp)/timerCpuSpeed());
			modelLODFreeUnpacked(model_lod);
		}
	}
	if (!model_lod->unpacked_last_used_timestamp && !model_lod->geo_render_info && modelLODIsLoaded(model_lod))
	{
		// Also has no graphics data, maybe free packed data too
		U32 delta = wl_frame_timestamp - model_lod->data_last_used_timestamp;
		assert(model_lod->data_last_used_timestamp);
		if (delta >= (U32)userData*10) // Keep packed data for 10s vs unpacked data's 1s
		{
			memlog_printf(geoGetMemLog(), "%u: Unloading model packed data %s/%s", wl_frame_timestamp, model_lod->model_parent->header->filename, model_lod->model_parent->name);
			modelLODFreeData(model_lod, true);
		}
	}
	return true;
}

void modelUnloadCheck(U32 unloadTime)
{
	PERFINFO_AUTO_START_FUNC();

// 	// FIXME: This can still unload the packed data which is being used in another thread if there was a long frame
// 	//  that just ended and the other thread flagged it with the timestamp of the long frame, so it's instantly
// 	//  more than 2 seconds old despite being accessed only ms ago.
// 	// Possible solutions: also store a frame count or some other cookie on the unpacked data so that we don't
// 	//  unload anything which is currently being referenced.  Alternatively, anyone unpacking data could enter
// 	//  the CS and only leave it when it's done with the data (maybe slower but should be reliable).
	// Fixed by adding ModelLOD::unpacked_in_use which should be set (inside the critical section) whenever
	//  something requests the data, and then released when done.
	EnterCriticalSection(&model_lod_init_cs);
	EnterCriticalSection(&model_unpack_cs);
	in_model_unpack_cs++;
	{
		// 	modelForEachModelLOD(modelUnloadCheckCallback, (intptr_t)unloadTime, false);
		int iCount;
#define NUM_PER_CALL 64
		if (!model_lod_list_cursor)
			model_lod_list_cursor = model_lod_list_head;
		for (iCount=NUM_PER_CALL; model_lod_list_cursor && iCount; iCount--, model_lod_list_cursor = model_lod_list_cursor->next)
		{
			if (!modelUnloadCheckCallback(model_lod_list_cursor, (intptr_t)unloadTime))
				break;
		}
	}
	in_model_unpack_cs--;
	LeaveCriticalSection(&model_unpack_cs);
	LeaveCriticalSection(&model_lod_init_cs);
	modelUnloadCollisionsCheck(unloadTime);

	PERFINFO_AUTO_STOP();
}

void modelDataOncePerFrame(void)
{
	modelUnloadCheck(timerCpuSpeed() * 1);
}


void modelPrintCollisionCounts(Model *model, U32 *cmTotalBytes, int *wcoTotalCount, U32 *wcoTotalBytes, bool show_header);

static int cmpModelColCount(const Model **pm1, const Model **pm2)
{
	const Model *m1 = *pm1;
	const Model *m2 = *pm2;
	return (eaSize(&m1->psdkScaledTriangleMeshes) + eaSize(&m1->psdkScaledConvexMeshes)) - (eaSize(&m2->psdkScaledTriangleMeshes) + eaSize(&m2->psdkScaledConvexMeshes));
}

typedef struct ModelCollisionCountsData
{
	Model **models;
	int cmTotalCount;
} ModelCollisionCountsData;

static bool modelPrintAllCollisionCountsCallback(Model *m, intptr_t userData)
{
	ModelCollisionCountsData *data = (ModelCollisionCountsData*)userData;
	int col_count = eaSize(&m->psdkScaledTriangleMeshes) + eaSize(&m->psdkScaledConvexMeshes);
	if (col_count > 0)
	{
		eaPush(&data->models, m);
		data->cmTotalCount += col_count;
	}

	return true;
}

// Prints out how many collision objects are in use for each model
AUTO_COMMAND ACMD_NAME(debugModelCol) ACMD_CATEGORY(Debug);
void modelPrintAllCollisionCounts(void)
{
	ModelCollisionCountsData data = {0};
	int i;
	U32 cmTotalBytes = 0, wcoTotalBytes = 0;
	int wcoTotalCount = 0;

	modelForEachModel(modelPrintAllCollisionCountsCallback, (intptr_t)&data, true);

	eaQSortG(data.models, cmpModelColCount);

	for (i = 0; i < eaSize(&data.models); ++i)
		modelPrintCollisionCounts(data.models[i], &cmTotalBytes, &wcoTotalCount, &wcoTotalBytes, i == 0);
	printf("Total Cooked Meshes: %3d (%s)\n", data.cmTotalCount, friendlyBytes(cmTotalBytes));
	printf("Total WCOs: %3d (%s)\n\n", wcoTotalCount, friendlyBytes(wcoTotalBytes));

	eaDestroy(&data.models);
}

bool g_rebuilt_models_materials;
bool g_checking_for_rebuilt_materials;

static bool modelLODRebuildMaterialsCallback(ModelLOD *model_lod, intptr_t userData_UNUSED)
{
	if (model_lod->loadstate == GEO_LOADED)
	{
		model_lod->loadstate = GEO_LOADED_NEED_INIT;
		// Do re-init
		modelLODIsLoaded(model_lod);
	}
	return true;
}

bool modelRebuildMaterials(void)
{
	EnterCriticalSection(&model_lod_init_cs);
	g_checking_for_rebuilt_materials = true;
	g_rebuilt_models_materials = false;
	modelForEachModelLOD(modelLODRebuildMaterialsCallback, 0, false);
	g_checking_for_rebuilt_materials = false;
	LeaveCriticalSection(&model_lod_init_cs);
	return g_rebuilt_models_materials;
}

bool modelIsTemp(Model* model)
{
	return model && !model->header;
}

Model *tempModelAlloc_dbg(const char *name, Material **materials, int material_count, ModelDataLoadFunc data_load_callback, void *user_data, bool load_in_foreground, WLUsageFlags use_flags MEM_DBG_PARMS)
{
	Model *model;
	ModelLOD *model_lod;

	model = scallocStruct(Model);
	model->name = name;
	model->use_flags = use_flags;
	model->collision_data.loadstate = GEO_NOT_LOADED;
	model->collision_data.data_load_callback = data_load_callback;
	model->collision_data.data_load_user_data = user_data;

	model_lod = modelAllocLOD(model, 0 MEM_DBG_PARMS_CALL);
	model_lod->data_load_callback = data_load_callback;
	model_lod->data_load_user_data = user_data;
	model_lod->load_in_foreground = load_in_foreground;
	model_lod->actual_lod = model_lod;
	model_lod->reference_count = 1;

	seaPush(&model->model_lods, model_lod);

	if (!data_load_callback)
	{
		model_lod->data = scalloc(sizeof(ModelLODData) + material_count * sizeof(model_lod->data->tex_idx[0]), 1);
		model_lod->data->tex_idx = (TexID*)(model_lod->data + 1);
		model_lod->data->tex_count = material_count;
	}

	model_lod->materials = scalloc(sizeof(Material*), material_count);
	if (materials)
	{
		memcpy(model_lod->materials, materials, material_count * sizeof(Material*));
	}
	else
	{
		int i;
		for (i = 0; i < material_count; ++i)
			model_lod->materials[i] = default_material;
	}

	EnterCriticalSection(&model_lod_init_cs);
	seaPush(&temp_models, model);
	LeaveCriticalSection(&model_lod_init_cs);

	return model;
}

bool tempModelLoadHeader(SimpleBufHandle header_buf, ModelLoadHeader * header)
{
	// modelAddGMeshToGLD defines this format: { U32 data_size; [ ModelLoadHeader header ]; }
	U32 data_size = 0;
	if (SimpleBufReadU32(&data_size, header_buf) != sizeof(U32))
		return false;
	if (data_size < sizeof(ModelLoadHeader))
		return false;

	return SimpleBufRead(header, sizeof(ModelLoadHeader), header_buf) == sizeof(ModelLoadHeader) ? true : false;
}

bool tempModelSkipLoadHeader(SimpleBufHandle header_buf)
{
	// modelAddGMeshToGLD defines this format: { U32 data_size; [ ModelLoadHeader header ]; }
	U32 data_size = 0;
	ModelLoadHeader temp_header;
	SimpleBufReadU32(&data_size, header_buf);
	if (data_size < sizeof(ModelLoadHeader))
		return false;

	return SimpleBufRead(&temp_header, sizeof(ModelLoadHeader), header_buf) == sizeof(ModelLoadHeader) ? true : false;
}

Model *tempModelLoad_dbg(const char *name, SimpleBufHandle header_buf, ModelDataLoadFunc data_load_callback, void *user_data, WLUsageFlags use_flags MEM_DBG_PARMS)
{
	ModelLOD *model_lod;
	Model *model;
	ModelLoadHeader model_header;

	// modelAddGMeshToGLD defines this format.
	if (!tempModelLoadHeader(header_buf, &model_header))
		return NULL;

	assert(data_load_callback);
	model = tempModelAlloc_dbg(name, NULL, model_header.material_count, data_load_callback, user_data, false, use_flags MEM_DBG_PARMS_CALL);
	model_lod = model->model_lods[0];

	model_lod->tri_count = model_header.tri_count;
	model_lod->vert_count = model_header.vert_count;
	copyVec3(model_header.minBBox, model->min);
	copyVec3(model_header.maxBBox, model->max);

	model->radius = boxCalcMid(model->min, model->max, model->mid);

	modelLODUpdateMemUsage(model_lod);

	return model;
}

void tempModelFree(Model **model_ptr)
{
	Model *model = *model_ptr;
	ModelLOD *model_lod;
	bool bDestroyed;
	// ASSERT_CALLED_IN_SINGLE_THREAD; // modelLODDestroy and callbacks wouldn't be happy otherwise.

	if (!model)
		return;

	EnterCriticalSection(&model_lod_init_cs);
	eaFindAndRemoveFast(&temp_models, model);
	LeaveCriticalSection(&model_lod_init_cs);

	assert(eaSize(&model->model_lods) == 1);
	assert(model->model_lods);
	model_lod = model->model_lods[0];
	model_lod->reference_count--;
	bDestroyed = modelLODDestroy(model_lod);
	assert(bDestroyed); // Otherwise we've got a ModelLOD and GeoRenderInfo hanging around still referencing this!
	eaDestroy(&model->model_lods);
	modelFreeColData(model, true);
	free(model);
	*model_ptr = NULL;
}


GMeshParsed *modelToParsedFormat(Model *model)
{
	GMeshParsed *mesh_parsed;
	int i,j;
	ModelLOD *model_lod;
	const U8 *v4;
	const U32 *tris;
	const Vec3 *v3;
	const Vec2 *v2;
	const TexID *texid;

	if (!model)
		return NULL;
	model_lod = model->model_lods[0];
	assert(model_lod);
	assert(model_lod->data);

	mesh_parsed = StructAlloc(parse_GMeshParsed);

#define DOIT4(fieldout, fieldin) \
	if (!!(v4 = modelGet##fieldin(model_lod))) { \
		eaiSetCapacity(&mesh_parsed->fieldout, model_lod->data->vert_count*4); \
		for (i=0; i<model_lod->data->vert_count*4; i++) {\
			eaiPush(&mesh_parsed->fieldout, v4[i]);\
		}\
	}
#define DOIT3(fieldout, fieldin) \
	if (!!(v3 = modelGet##fieldin(model_lod))) { \
		eafSetCapacity(&mesh_parsed->fieldout, model_lod->data->vert_count*3); \
		for (i=0; i<model_lod->data->vert_count; i++) {\
			eafPush3(&mesh_parsed->fieldout, v3[i]);\
		}\
	}
#define DOIT2(fieldout, fieldin) \
	if (!!(v2 = modelGet##fieldin(model_lod))) { \
		eafSetCapacity(&mesh_parsed->fieldout, model_lod->data->vert_count*2); \
		for (i=0; i<model_lod->data->vert_count; i++) {\
			eafPush2(&mesh_parsed->fieldout, v2[i]);\
		}\
	}

	DOIT3(positions, Verts);
	DOIT3(positions2, Verts2);
	DOIT3(normals, Norms);
	DOIT3(normals2, Norms2);
	DOIT3(binormals, Binorms);
	DOIT3(tangents, Tangents);
	DOIT2(tex1s, Sts);
	DOIT2(tex2s, Sts3);
	assert(!modelGetWeights(model_lod)); // Could be done, but don't think it's used?
	DOIT4(bonemats, Matidxs);
	DOIT4(colors, Colors);
#undef DOIT2
#undef DOIT3
#undef DOIT4

	tris = modelGetTris(model_lod);
	texid = model_lod->data->tex_idx;
	j = texid->count;
	for (i = 0; i < model_lod->data->tri_count; ++i)
	{
		if (j==0)
		{
			texid++;
			j = texid->count;
		}
		eaiPush(&mesh_parsed->tris, tris[i*3+0]);
		eaiPush(&mesh_parsed->tris, tris[i*3+1]);
		eaiPush(&mesh_parsed->tris, tris[i*3+2]);
		eaiPush(&mesh_parsed->tris, texid->id);
		j--;
	}

	return mesh_parsed;
}

void modelFromGmesh(Model *model, GMesh *mesh)
{
	int i, j, last_tex;
	GTriIdx *meshtri;
	int *modeltri;
	ModelLOD *model_lod;

	PERFINFO_AUTO_START_FUNC();

	model_lod = model->model_lods[0];
	assert(model_lod);

	model_lod->data->tri_count = mesh->tri_count;
	model_lod->data->vert_count = mesh->vert_count;

	// model->geo_render_info->load_in_foreground = load_in_foreground; - moved to gfxFillModelRenderInfo()

	model_lod->unpack.verts = realloc(model_lod->unpack.verts, mesh->vert_count * sizeof(Vec3));
	memcpy(model_lod->unpack.verts, mesh->positions, mesh->vert_count * sizeof(Vec3));
	if (mesh->normals)
	{
		model_lod->unpack.norms = realloc(model_lod->unpack.norms, mesh->vert_count * sizeof(Vec3));
		memcpy(model_lod->unpack.norms, mesh->normals, mesh->vert_count * sizeof(Vec3));
	}
	if (mesh->normals2)
	{
		model_lod->unpack.norms2 = realloc(model_lod->unpack.norms2, mesh->vert_count * sizeof(Vec3));
		memcpy(model_lod->unpack.norms2, mesh->normals2, mesh->vert_count * sizeof(Vec3));
	}
	if (mesh->positions2)
	{
		model_lod->unpack.verts2 = realloc(model_lod->unpack.verts2, mesh->vert_count * sizeof(Vec3));
		memcpy(model_lod->unpack.verts2, mesh->positions2, mesh->vert_count * sizeof(Vec3));
	}
	if (mesh->tangents)
	{
		model_lod->unpack.tangents = realloc(model_lod->unpack.tangents, mesh->vert_count * sizeof(Vec3));
		memcpy(model_lod->unpack.tangents, mesh->tangents, mesh->vert_count * sizeof(Vec3));
	}
	if (mesh->binormals)
	{
		model_lod->unpack.binorms = realloc(model_lod->unpack.binorms, mesh->vert_count * sizeof(Vec3));
		memcpy(model_lod->unpack.binorms, mesh->binormals, mesh->vert_count * sizeof(Vec3));
	}
	if (mesh->tex1s)
	{
		model_lod->unpack.sts = realloc(model_lod->unpack.sts, mesh->vert_count * sizeof(Vec2));
		memcpy(model_lod->unpack.sts, mesh->tex1s, mesh->vert_count * sizeof(Vec2));
	}
	if (mesh->tex2s)
	{
		model_lod->unpack.sts3 = realloc(model_lod->unpack.sts3, mesh->vert_count * sizeof(Vec2));
		memcpy(model_lod->unpack.sts3, mesh->tex2s, mesh->vert_count * sizeof(Vec2));
	}
	if (mesh->colors || mesh->varcolors)
	{
		model_lod->unpack.colors = realloc(model_lod->unpack.colors, mesh->vert_count * 4 * sizeof(U8));
		for (i = 0; i < mesh->vert_count; ++i)
		{
			if (mesh->colors)
			{
				model_lod->unpack.colors[i*4+0] = mesh->colors[i].r;
				model_lod->unpack.colors[i*4+1] = mesh->colors[i].g;
				model_lod->unpack.colors[i*4+2] = mesh->colors[i].b;
				model_lod->unpack.colors[i*4+3] = mesh->colors[i].a;
			}
			else
			{
				for (j = 0; j < 4; ++j)
				{
					if (j < mesh->varcolor_size)
						model_lod->unpack.colors[i*4+j] = mesh->varcolors[i*mesh->varcolor_size+j];
					else
						model_lod->unpack.colors[i*4+j] = 0;
				}
			}
		}
	}
	if (mesh->bonemats)
	{
		model_lod->unpack.matidxs = realloc(model_lod->unpack.matidxs, mesh->vert_count * 4 * sizeof(U8));
		model_lod->unpack.weights = realloc(model_lod->unpack.weights, mesh->vert_count * 4 * sizeof(U8));
		for (i = 0; i < mesh->vert_count; ++i)
		{
			copyVec4(mesh->bonemats[i], &model_lod->unpack.matidxs[i*4]);
			scaleVec4(mesh->boneweights[i], 255.f, &model_lod->unpack.weights[i*4]);
		}
	}

	modeltri = model_lod->unpack.tris = realloc(model_lod->unpack.tris, mesh->tri_count * sizeof(int) * 3);

	for (i = 0; i < model_lod->data->tex_count; ++i)
		model_lod->data->tex_idx[i].id = i;

	gmeshSortTrisByTexID(mesh, NULL);
	meshtri = mesh->tris;

	last_tex = 0;
	for (i = 0; i < mesh->tri_count; i++)
	{
		for (j = last_tex; j < model_lod->data->tex_count; j++)
		{
			if (model_lod->data->tex_idx[j].id == meshtri->tex_id)
			{
				model_lod->data->tex_idx[j].count++;
				last_tex = j;
				break;
			}
		}
		if (model_lod->data->tex_count)
			assert(j < model_lod->data->tex_count); // GMeshes coming there here and their tempModels should each just have one material

		*(modeltri++) = meshtri->idx[0];
		*(modeltri++) = meshtri->idx[1];
		*(modeltri++) = meshtri->idx[2];
		meshtri++;
	}

	// remove subobjects with no triangles
	for (i = 0; i < model_lod->data->tex_count; ++i)
	{
		if (!model_lod->data->tex_idx[i].count)
		{
			if (i < model_lod->data->tex_count-1)
				memmove(&model_lod->data->tex_idx[i], &model_lod->data->tex_idx[i+1], (model_lod->data->tex_count - i - 1) * sizeof(TexID));
			--model_lod->data->tex_count;
			--i;
		}
	}

	modelLODInitFromData(model_lod);

	// Need to init materials?  Set to _NEED_INIT?
	model_lod->loadstate = GEO_LOADED;

	PERFINFO_AUTO_STOP();
}
