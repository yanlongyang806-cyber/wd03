#include "wlModelLoad.h"
#include "wlModelBinning.h"
#include "wlState.h"
#include "wlAutoLOD.h"
#include "wlModelInline.h"
#include "wlModelReload.h"
#include "timing.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "ScratchStack.h"
#include "crypt.h"
#include "EventTimingLog.h"
#include "../../3rdparty/zlib/zlib.h"
#include "StringCache.h"
#include "PhysicsSDK.h"
#include "memlog.h"
#include "fileLoader.h"
#include "fileLoaderStats.h"
#include "UnitSpec.h"
#include "strings_opt.h"
#include "trivia.h"
#include "serialize.h"
#include "WorldGridPrivate.h"
#include "AppRegCache.h"
#include "UtilitiesLib.h"
#include "ContinuousBuilderSupport.h"
#include "GenericMesh.h"

#include "wlModel_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:backgroundLoadingThread", BUDGET_EngineMisc););

DictionaryHandle hModelHeaderDict;

ModelHeader globalDummyHeader = { 0 };
bool bGlobalDummyHeaderInit = false;

ModelLOD *model_lod_list_head; // List of regular ModelLODs (not tempModels)
ModelLOD *model_lod_list_tail; // List of regular ModelLODs (not tempModels)
ModelLOD *model_lod_list_cursor; // For unloading, always NULL or pointing to a valid model

static StashTable ht_nonexistent = 0;
static volatile int number_of_models_in_background_loader = 0;
static StashTable ht_charlib;
volatile int number_of_execs_in_background_loader;
static int delay_geo_loading=0;
StashTable ht_models; // Unpacked models, Protected by model_lod_init_cs

CRITICAL_SECTION model_unpack_cs;
CRITICAL_SECTION model_lod_init_cs;
volatile int in_model_unpack_cs;

static EventOwner *geo_event_timer;

static MemLog geo_memlog;
volatile GeoMemUsage wl_geo_mem_usage;

FILE *geo2OpenDataFile(Model *model, void *unused);

// Adds a delay to simulate slow loading of geometry
AUTO_CMD_INT(delay_geo_loading, delayGeoLoading) ACMD_COMMANDLINE;

static int disable_precooked_data=0;
// Disables using precooked geometry collision meshes
AUTO_CMD_INT(disable_precooked_data, disable_precooked_data) ACMD_COMMANDLINE;

AUTO_RUN;
void modelLoadInitCS(void)
{
	geo_memlog.careAboutThreadId = 1;
	InitializeCriticalSection(&model_unpack_cs);
	InitializeCriticalSection(&model_lod_init_cs);
}

static int compareModelNamesAndLengths(const char* name1, int len1, const char* name2, int len2);
static void modelLODInitMaterials(ModelLOD *model);
void modelColRequestBackgroundLoad(Model *model);
static void wlCharacterModelKey_s(char *buf, size_t buf_size, const char* fname, const char* modelname);

MemLog *geoGetMemLog(void)
{
	return &geo_memlog;
}

void modelLODFixupNoData(ModelLOD *model_lod)
{
	// This model has no data, probably because the LOD could not be generated, use the next higher one
	model_lod->loadstate = GEO_LOADED_NULL_DATA;
}

bool modelLODIsLoaded(ModelLOD *model_lod)
{
	if (model_lod->loadstate == GEO_LOADED || model_lod->loadstate == GEO_LOADED_NULL_DATA)
		return true;
	if (model_lod->loadstate == GEO_NOT_LOADED || model_lod->loadstate == GEO_LOADING)
		return false;
	EnterCriticalSection(&model_lod_init_cs);
	if (model_lod->loadstate == GEO_LOADED_NEED_INIT)
	{
		assert(wl_frame_timestamp);
		model_lod->data_last_used_timestamp = wl_frame_timestamp;
		if (model_lod->data == NULL)
		{
			modelLODFixupNoData(model_lod);
			LeaveCriticalSection(&model_lod_init_cs);
			return false;
		} else {
			modelLODInitFromData(model_lod);
			modelLODInitMaterials(model_lod);
			LeaveCriticalSection(&model_lod_init_cs);
			return true;
		}
	}
	LeaveCriticalSection(&model_lod_init_cs);
	return false;
}

bool modelColGetData(Model *model, void **data, U32 *data_size)
{
	bool bRet = false;
	if (model->collision_data.loadstate == GEO_LOADED)
	{
		EnterCriticalSection(&model_lod_init_cs);
		if (model->collision_data.loadstate == GEO_LOADED)
		{
			*data = model->collision_data.unpacked_data;
			*data_size = model->collision_data.unpacked_size;

			model->collision_data.unpacked_data = NULL;
			model->collision_data.unpacked_size = 0;
			model->collision_data.loadstate = GEO_LOADED_LOST_DATA;
			bRet = true;
		}
		LeaveCriticalSection(&model_lod_init_cs);

	}
	if (model->collision_data.loadstate == GEO_LOADED_NULL_DATA)
	{
		*data = NULL;
		*data_size = 0;
		bRet = true;
	}
	return bRet;
}

bool modelLODHasUnpacked(ModelLOD *model_lod)
{
	return model_lod->unpack.tris || model_lod->unpack.verts; // Could be others, but at least one of these two would be used
}



ModelLOD *modelLoadLOD(const Model *model, int lod_index)
{
	ModelLOD *model_lod = modelGetLOD(model, lod_index);
	if (!model_lod)
		return NULL;
	model_lod->data_last_used_timestamp = wl_frame_timestamp;
	if (model_lod->loadstate == GEO_NOT_LOADED)
	{
		modelLODRequestBackgroundLoad(model_lod);
	}
	return model_lod;
}

ModelLOD *modelLoadColLOD(Model *model)
{
	ModelLOD *model_lod = eaGet(&model->model_lods, 0);
	if (!model_lod)
		return NULL;
	if (model_lod->loadstate == GEO_NOT_LOADED)
	{
		modelLODRequestBackgroundLoad(model_lod);
	}
	return model_lod;
}

ModelLOD *modelFindLOD(const char *modelName, int lod_index, bool load, WLUsageFlags use_flags)
{
	Model *model = modelFind(modelName, load, use_flags);
	if (!model)
		return NULL;
	return modelGetLOD(model, lod_index);
}

ModelLOD *modelAllocLOD(Model *model, int lod_index MEM_DBG_PARMS)
{
	ModelLOD *lod;
	assert(model->use_flags);
	lod = scallocStruct(ModelLOD);
	lod->model_parent = model;
	lod->lod_index = lod_index;
	lod->loadstate = GEO_NOT_LOADED;
	lod->tri_count = lod->vert_count = -1;
	lod->mem_usage_bitindex = wlGetUsageFlagsBitIndex(model->use_flags);
	lod->data_load_callback = geo2OpenDataFile;
	return lod;
}

static void modelLODDataDestroy(ModelLODData *data)
{
	// It's (almost) all one big chunk, easy to free, yay!
	data->process_time_flags = MODEL_DEBUG_HAS_BEEN_FREED;
	free((void *)data->tex_names);
	data->tex_names = (void*)MODEL_DEBUG_HAS_BEEN_FREED;
	free(data);
}

static Destructor destroyModelLODCallback;
void modelLODSetDestroyCallback(Destructor destructor)
{
	destroyModelLODCallback = destructor;
}

void modelLODFreeData(ModelLOD *model, bool caller_in_model_unpack_cs)
{
	ModelLODData *data;

	if (destroyModelLODCallback)
	{
		if (caller_in_model_unpack_cs)
		{
			// Leaving the CS to prevent deadlock with zo-thread (both our caller and the zo-thread are
			//  in the CS simply to prevent per-model calls into it, I believe).
			assert(in_model_unpack_cs==1);
			in_model_unpack_cs--;
			LeaveCriticalSection(&model_unpack_cs);
		}

		destroyModelLODCallback(model);

		if (caller_in_model_unpack_cs)
		{
			EnterCriticalSection(&model_unpack_cs);
			in_model_unpack_cs++;
		}
	}
	
	if (!modelLODIsLoaded(model))
	{
		// Free unpacked regardless, packed might not be still loaded
		modelLODFreeUnpacked(model);
		return;
	}

	// Free this data *after* the callback, since it syncs with other threads in GraphicsLib
	modelLODFreeUnpacked(model);

	data = model->data;
	model->loadstate = GEO_NOT_LOADED;
	model->data = NULL;
	if (data)
		modelLODDataDestroy(data);
	modelLODUpdateMemUsage(model);
}

void modelLODFreeUnpacked(ModelLOD *model)
{
	SAFE_FREE(model->unpack.tris);
	SAFE_FREE(model->unpack.verts);
	SAFE_FREE(model->unpack.norms);
	SAFE_FREE(model->unpack.binorms);
	SAFE_FREE(model->unpack.tangents);
	SAFE_FREE(model->unpack.sts);
	SAFE_FREE(model->unpack.sts3);
	SAFE_FREE(model->unpack.colors);
	SAFE_FREE(model->unpack.matidxs);
	SAFE_FREE(model->unpack.weights);
	SAFE_FREE(model->unpack.verts2);
	SAFE_FREE(model->unpack.norms2);
	model->unpacked_last_used_timestamp = 0;
	modelLODUpdateMemUsage(model);
}

void modelLODFreePacked(ModelLOD *model)
{
	int new_data_size;

	if (!model->data)
		return;

	new_data_size = sizeof(*model->data) + model->data->tex_count * sizeof(TexID);
	if (new_data_size == model->data->data_size)
		return;

	model->data->data_size = new_data_size;
	model->data = realloc(model->data, model->data->data_size);

	// Fixup anything that may have moved from the realloc
	model->data->tex_idx = (void*)(model->data + 1);
	ZeroStructForce(&model->data->pack);

	SAFE_FREE(model->data_load_user_data);

	modelLODUpdateMemUsage(model);
}

// Parent must remove from their LOD list
bool modelLODDestroy(ModelLOD *model)
{
	// ASSERT_CALLED_IN_SINGLE_THREAD;
	if (model->loadstate == GEO_LOADING || model->reference_count)
		return false;

	if (destroyModelLODCallback)
		destroyModelLODCallback(model);

	assert(!model->geo_render_info);

	// Free packed
	modelLODFreeData(model, false);

	// Free any unpacked
	modelLODFreeUnpacked(model);

	SAFE_FREE(model->data_load_user_data);

	// Remove from global list if it's there
	if (model->next)
		model->next->prev = model->prev;
	if (model->prev)
		model->prev->next = model->next;
	if (model_lod_list_head == model)
		model_lod_list_head = model->next;
	if (model_lod_list_tail == model)
		model_lod_list_tail = model->prev;
	assert(!model_lod_list_head == !model_lod_list_tail);
	if (model_lod_list_cursor == model)
		model_lod_list_cursor = model->next;

	// Remove final memory usage
	InterlockedExchangeAdd(&wl_geo_mem_usage.loadedSystem[model->mem_usage_bitindex], -model->last_mem_usage);
	InterlockedExchangeAdd(&wl_geo_mem_usage.loadedSystemTotal, -model->last_mem_usage);

	// Free model
	SAFE_FREE(model->materials);
	SAFE_FREE(model);
	return true;
}


bool modelInitLODs(Model *model, intptr_t userdata)
{
	// ASSERT_CALLED_IN_SINGLE_THREAD;
	if (!model->lod_info)
	{
		model->lod_info = lodinfoFromModel(model);
		assert(model->lod_info);
	}

	// Free old model->lods array on reload
	modelFreeData(model);
	eaDestroyEx(&model->model_lods, modelLODDestroy);

	eaSetSize(&model->model_lods, eaSize(&model->lod_info->lods));

	FOR_EACH_IN_EARRAY_FORWARDS(model->lod_info->lods, AutoLOD, autolod)
	{
		ModelLOD *model_lod;
		int lod_index = iautolodIndex;

		// setup convenience runtime bools - this is wasteful and error-prone
		autolod->modelname_specified = !!autolod->lod_modelname;
		autolod->null_model = (autolod->flags == LOD_ERROR_NULL_MODEL);
		autolod->do_remesh = (autolod->flags == LOD_ERROR_REMESH);

		model_lod = model->model_lods[lod_index] = modelAllocLOD(model, lod_index MEM_DBG_PARMS_INIT);
		modelLODUpdateMemUsage(model_lod);

		// Add to the tail of the global lists
		if (model_lod_list_tail)
			model_lod_list_tail->next = model_lod;
		else // No tail or head
			model_lod_list_head = model_lod;
		model_lod->prev = model_lod_list_tail;
		model_lod_list_tail = model_lod;

		if (autolod->modelname_specified && stricmp(autolod->lod_modelname, model->name)!=0)
		{
			Model *lodmodel;
			// LOD from a different model - hope this doesn't infinitely recurse...
			assert(autolod->lod_modelname);
			lodmodel = modelFindEx(autolod->lod_filename, autolod->lod_modelname, false, model->use_flags);
			if (lodmodel) {
				int i;
				// Adjust our model's parameters in case a lower LOD is larger
				MAX1F(model->radius, lodmodel->radius);
				for (i=0; i<3; i++) {
					MAX1F(model->max[i], lodmodel->max[i]);
					MIN1F(model->min[i], lodmodel->min[i]);
				}
				addVec2(model->min, model->max, model->mid);
				scaleVec2(model->mid, 0.5f, model->mid);

				model->model_lods[lod_index]->actual_lod = modelGetLOD(lodmodel, 0);
				// JE: It's possible that this is specified to refer to another model, but that model is a NullModel LOD
				//assert(model->model_lods[lod_index]->actual_lod);
			} else {
				model->model_lods[lod_index]->actual_lod = NULL;
			}
		} else if (autolod->null_model) {
			model->model_lods[lod_index]->actual_lod = NULL;
		} else {
			// Our own LOD
			model->model_lods[lod_index]->actual_lod = model->model_lods[lod_index];
		}
		if (model->model_lods[lod_index]->actual_lod)
			model->model_lods[lod_index]->actual_lod->reference_count++;
	}
	FOR_EACH_END;

	if (eaSize(&model->model_lods) >= 2)
	{
		if (model->header->high_detail_high_lod)
			model->model_lods[0]->is_high_detail_lod = 1;
	}

	return true;
}

static bool modelReInitLODs(Model *model, intptr_t userdata)
{
	const char *path = (const char *)userdata;

	assert(model->header);
	if (!model->use_flags)
		model->use_flags = WL_FOR_WORLD; // Maybe an LOD of a different model and not flagged or something?
	if (model->header && model->header->filename == path)
		modelHeaderReloaded(model);
	return true;
}

static void lodinfoReloadCallback(const char *path)
{
	// LOD reloading - just reinit, also re-bin?
	char headerpath[MAX_PATH];
	changeFileExt(path, ".ModelHeader", headerpath);
	path = allocAddFilename(headerpath);
	modelForEachModel(modelReInitLODs, (intptr_t)path, false);

	wl_state.HACK_disable_game_callbacks = true;
	worldUpdateBounds(true, true);
	wl_state.HACK_disable_game_callbacks = false;

	// Callback after change (since trackers were closed and reopened)
	if(wl_state.edit_map_game_callback)
		wl_state.edit_map_game_callback(worldGetPrimaryMap());

	worldCheckForNeedToOpenCells();
}

void geoLoadCheckThreadLoader(void)
{
	// Does nothing anymore, background thread and lazy updates on flags
}

ModelLOD *modelLODLoadAndMaybeWait(const Model *model, int lod_index, int bWaitForLoad)
{
	ModelLOD *model_lod = NULL;
	PERFINFO_AUTO_START_FUNC();
	while(1)
	{
		model_lod = modelLoadLOD(model, lod_index);
		if (!model_lod)
			break;
		if (modelLODIsLoaded(model_lod))
			break;
		if (model_lod->loadstate == GEO_LOAD_FAILED)
		{
			model_lod = NULL; // Do this?  Or just return the model that failed?
			break;
		}
		if (!bWaitForLoad)
		{
			// return NULL to indicate not loaded
			model_lod = NULL;
			break;
		}
		Sleep(1);
	}
	PERFINFO_AUTO_STOP_FUNC();
	return model_lod;
}

// Gives ownership of data to caller
void *modelColLoadAndMaybeWait(Model *model, U32 *data_size, int bWaitForLoad)
{
	ModelLOD *data = NULL;
	PERFINFO_AUTO_START_FUNC();
	while(1)
	{
		if (modelColGetData(model, &data, data_size))
			break;
		modelColRequestBackgroundLoad(model);
		if (!bWaitForLoad)
		{
			// return NULL to indicate not loaded
			data = NULL;
			break;
		}
		Sleep(1);
	}
	PERFINFO_AUTO_STOP_FUNC();
	return data;
}

static void patchPackPtr(PackData *pack, void *base)
{
	if (pack->unpacksize)
		pack->data_ptr = pack->data_offs + (U8*)base;
}

static z_stream* zStream; // Protected by model_unpack_cs

static void* geoZAlloc(void* opaque, U32 items, U32 size)
{
	return malloc(items * size);
}

static void geoZFree(void* opaque, void* address)
{
	free(address);
}

static void geoInitZStream()
{
	assert(in_model_unpack_cs); // M
	if(!zStream)
	{
		zStream	= calloc(1, sizeof(*zStream));

		zStream->zalloc	= geoZAlloc;
		zStream->zfree	= geoZFree;
	}
	else
	{
		inflateEnd(zStream);
	}

	inflateInit(zStream);
}

void geoUncompress(void* outbuf, U32 outsize, const void* inbuf, U32 insize, const char *modelname, const char *filename, const char *msetForErrors)
{
	int ret;

	geoInitZStream();

	zStream->avail_out		= outsize;
	zStream->next_out		= outbuf;
	zStream->next_in		= (char*)inbuf;
	zStream->avail_in		= insize;
	ret = inflate(zStream, Z_FINISH);
 	if (!((ret == Z_OK || ret == Z_STREAM_END) && (zStream->avail_out == 0)))
	{
		char errorDetails[MAX_PATH * 2];
		if (msetForErrors)
			errorIsDuringDataLoadingInc(msetForErrors);
		sprintf(errorDetails, "geoUncompress failed, inflate() = %d @ %d/%d, for ",
			ret, zStream->avail_in, insize);
		if (modelname)
			strcatf(errorDetails, "model %s in ", modelname);
		strcatf(errorDetails, "file %s", filename ? filename : "unknown");
		triviaPrintf("geoUncompressDetails", "%s", errorDetails);
		ErrorDetailsf("%s", errorDetails);

		assertmsg(0, modelname ? "geoUncompress failed for model" : "geoUncompress failed for file");
		if (msetForErrors)
			errorIsDuringDataLoadingDec();
	}
}

void geoUnpackDeltas(PackData *pack, void *data, int stride, int count, PackType type, const char *modelname, const char *filename, const char *msetForErrors)
{
	if (!pack->unpacksize)
		return;

	PERFINFO_AUTO_START("geoUnpackDeltas", 1);

	if (pack->packsize)
	{
		if (pack->unpacksize < 0)
		{
			geoUncompress(data, -pack->unpacksize, pack->data_ptr, pack->packsize, modelname, filename, msetForErrors);
			endianSwapArray(data, stride * count, type);
		}
		else
		{
			U8 *unzip_buf;
			unzip_buf = ScratchAlloc(pack->unpacksize);
			geoUncompress(unzip_buf, pack->unpacksize, pack->data_ptr, pack->packsize, modelname, filename, msetForErrors);
			wlUncompressDeltas(data, unzip_buf, stride, count, type);
			ScratchFree(unzip_buf);
		}
	}
	else
	{
		if (pack->unpacksize < 0)
		{
			memcpy(data, pack->data_ptr, -pack->unpacksize);
			endianSwapArray(data, stride * count, type);
		}
		else
		{
			wlUncompressDeltas(data, pack->data_ptr, stride, count, type);
		}
	}

	PERFINFO_AUTO_STOP();
}

void geoUnpack(PackData *pack, void *data, const char *modelname, const char *filename, const char *msetForErrors)
{
	if (!pack->unpacksize)
		return;

	PERFINFO_AUTO_START("geoUnpack", 1);

	assert(pack->unpacksize > 0);

	if (pack->packsize)
		geoUncompress(data, pack->unpacksize, pack->data_ptr, pack->packsize, modelname, filename, msetForErrors);
	else
		memcpy(data, pack->data_ptr, pack->unpacksize);

	PERFINFO_AUTO_STOP();
}

void modelLODInitFromData(ModelLOD *model_lod)
{
	// Replicated oft-used fields, or fields needed after the data is freed
	model_lod->tri_count = model_lod->data->tri_count;
	model_lod->vert_count = model_lod->data->vert_count;

	if (model_lod->data->process_time_flags & MODEL_PROCESSED_NO_TANGENT_SPACE)
		model_lod->noTangentSpace = 1;

	// Fix up dynamically created models
	if (model_lod->data->tex_count == 1 && model_lod->data->tex_idx[0].count == 0)
		model_lod->data->tex_idx[0].count = model_lod->data->tri_count;

	if (model_lod->data->texel_density_stddev != 0.0f) {
		// old style mesh. The values in texel_density_stddev and texel_density_avg are not useful, so assume it has a very low uv density.
		model_lod->uv_density = GMESH_LOG_MIN_UV_DENSITY;
	} else {
		model_lod->uv_density = model_lod->data->texel_density_avg;
	}

	modelLODUpdateMemUsage(model_lod);
}

extern bool g_rebuilt_models_materials;
extern bool g_checking_for_rebuilt_materials;

// Called only inside model_lod_init_cs
static void modelLODInitMaterials(ModelLOD *model)
{
	int j;
	ModelLODData *data = model->data;
	assert(data);
	assert(model->loadstate == GEO_LOADED_NEED_INIT);

	model->materials = realloc(model->materials, data->tex_count * sizeof(model->materials[0]));

	for (j = 0; j < data->tex_count; j++)
	{
		Material *old_material = model->materials[j];
		int bindid = data->tex_idx[j].id;
		const char* tex_name = data->tex_names[bindid];

		if (!tex_name)
			tex_name = "white";
		else
		{
			assert(!strchr(tex_name, '.')); // Shouldn't have an extension
		}

		model->materials[j] = materialFindNoDefault(tex_name, model->model_parent->use_flags);
		if (model->materials[j] == NULL) {
			// Verified at startup: ErrorFilenamef(debug_filename, "Geometry contains a reference to an invalid/unknown material: \"%s\"", tex_name);
			model->materials[j] = default_material;
		}
		assert(model->materials[j]);
		if (g_checking_for_rebuilt_materials && old_material != model->materials[j])
			g_rebuilt_models_materials = true;
	}
	model->loadstate = GEO_LOADED;
}

static void *memNext(U8 *mem, int *mem_pos, int add, int headersize)
{
	if (*mem_pos + add <= headersize)
	{
		*mem_pos += add;
		return &mem[*mem_pos - add];
	}

	assert(0);
	return NULL;
}

static int memInt(U8 *mem, int *mem_pos, int headersize)
{
	int *iptr = memNext(mem, mem_pos, sizeof(int), headersize);
	if (iptr)
		return *iptr;
	return 0;
}

/*Geo Loader thread: All I do is sleep, waiting to be given work by QueueUserAPC*/
static DWORD WINAPI backgroundLoadingThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
		PERFINFO_AUTO_START("backgroundLoadingThread", 1);
			for(;;)
			{
				SleepEx(INFINITE, TRUE);
			}
		PERFINFO_AUTO_STOP();
	return 0; 
	EXCEPTION_HANDLER_END
} 

static FileScanAction addCharacterLibraryModel(char *dir, struct _finddata32_t *data, void *pUserData)
{
	char filename[MAX_PATH];

	if (data->name[0]=='_')
		return FSA_NO_EXPLORE_DIRECTORY;

	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;

	if (isBigEndian())
	{
		if (!strEndsWith(data->name, ".bgeo"))
			return FSA_EXPLORE_DIRECTORY;
	}
	else
	{
		if (!strEndsWith(data->name, ".lgeo"))
			return FSA_EXPLORE_DIRECTORY;
	}

	STR_COMBINE_SSS(filename, dir, "/", data->name);

	stashAddPointer(ht_charlib, data->name, allocAddFilename(filename), true);

	return FSA_EXPLORE_DIRECTORY;
}

static void refreshCharacterLibraryModel(const char *relpath, int when)
{
	const char *filename;
	char *str = NULL;

	if (!relpath || !relpath[0])
		return;

	if (isBigEndian())
	{
		if (!strEndsWith(relpath, ".bgeo"))
			return;
	}
	else
	{
		if (!strEndsWith(relpath, ".lgeo"))
			return;
	}

	filename = getFileNameConst(relpath);

	stashRemovePointer(ht_charlib, filename, &str);

	if (!(when & FOLDER_CACHE_CALLBACK_DELETE))
		stashAddPointer(ht_charlib, filename, allocAddFilename(relpath), true);
}

void geoLoadInit(void)
{
	loadstart_printf("Loading geometry...");
	if (!geo_event_timer)
	{
		geo_event_timer = etlCreateEventOwner("GeoLoader", "BackgroundLoader", "WorldLib");
	}

	if (!ht_charlib)
	{
		// build hashtable of character library geo filename -> relative path
		ht_charlib = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);
		fileScanAllDataDirs("character_library", addCharacterLibraryModel, NULL);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, isBigEndian()?"character_library/*.bgeo":"character_library/*.lgeo", refreshCharacterLibraryModel);
	}

	lodinfoSetReloadCallback(lodinfoReloadCallback);
	loadend_printf(" done.");
}

// Callback that the background loader is given to do
static void heyThreadLoadAGeo(const char *filename_unused, void *dwParam)
{
	FILE *f;
	ModelLOD *model_lod;

	PERFINFO_AUTO_START_FUNC();

		if (delay_geo_loading)
			Sleep(delay_geo_loading);

		etlAddEvent(geo_event_timer, "Load Geo", ELT_CODE, ELTT_BEGIN);

		model_lod = dwParam;

		assert(!errorIsDuringDataLoadingGet());
		assert(model_lod->data_load_callback);
		// This callback should override the errorIsDuringDataLoading file name with a good path
		f = model_lod->data_load_callback(model_lod->model_parent, model_lod->data_load_user_data);
		assert(errorIsDuringDataLoadingGet()); // Callback must call errorIsDuringDataLoadingInc

		assert(!model_lod->data);
		if (!model_lod->model_parent->msetForErrors)
			model_lod->model_parent->msetForErrors = allocAddString(f->nameptr);
		model_lod->data = geo2LoadLODData(f, model_lod->model_parent->name, model_lod->lod_index, SAFE_MEMBER(model_lod->model_parent->header, filename));
		// data can be NULL
		// fixup?

		if (f)
			fclose(f);

		errorIsDuringDataLoadingDec();
		assert(!errorIsDuringDataLoadingGet()); // Make sure we cleaned up correctly

		if (model_lod->data && wlLoadUpdate)
			wlLoadUpdate(model_lod->data->data_size);
		modelLODUpdateMemUsage(model_lod);

		model_lod->loadstate = GEO_LOADED_NEED_INIT;

		etlAddEvent(geo_event_timer, "Load Geo", ELT_CODE, ELTT_END);

		InterlockedDecrement(&number_of_models_in_background_loader);

	PERFINFO_AUTO_STOP();
}


// Callback that the background loader is given to do
static void heyThreadLoadAColData(const char *filename_unused, void *dwParam)
{
	FILE *f;
	Model *model;
	PackData temp_data = {0};

	PERFINFO_AUTO_START_FUNC();

	if (delay_geo_loading)
		Sleep(delay_geo_loading);

	etlAddEvent(geo_event_timer, "Load ColData", ELT_CODE, ELTT_BEGIN);

	model = dwParam;

	assert(!errorIsDuringDataLoadingGet());
	assert(model->collision_data.data_load_callback);
	// This callback should override the errorIsDuringDataLoading file name with a good path
	f = model->collision_data.data_load_callback(model, model->collision_data.data_load_user_data);
	assert(errorIsDuringDataLoadingGet()); // Callback must call errorIsDuringDataLoadingInc

	assert(!model->collision_data.unpacked_data);
	geo2LoadColData(f, model->name, model, &temp_data);
	// data can be NULL
	// fixup?

	if (f)
		fclose(f);

	errorIsDuringDataLoadingDec();
	assert(!errorIsDuringDataLoadingGet()); // Make sure we cleaned up correctly

	if (temp_data.unpacksize && wlLoadUpdate)
		wlLoadUpdate(ABS(temp_data.unpacksize));

	if (temp_data.data_ptr)
	{
		int unpacksize = ABS(temp_data.unpacksize);
		assert(temp_data.data_ptr);
		if (temp_data.packsize)
		{
			void *unpacked = malloc(unpacksize);
			EnterCriticalSection(&model_unpack_cs);
			in_model_unpack_cs++;
			geoUnpack(&temp_data, unpacked, model->name, SAFE_MEMBER(model->header, filename), model->msetForErrors);
			in_model_unpack_cs--;
			LeaveCriticalSection(&model_unpack_cs);
			model->collision_data.unpacked_data = unpacked;
			SAFE_FREE(temp_data.data_ptr);
			temp_data.data_ptr = NULL;
		} else {
			model->collision_data.unpacked_data = temp_data.data_ptr;
			temp_data.data_ptr = NULL;
		}
		model->collision_data.unpacked_size = unpacksize;
	}
	else
	{
		model->collision_data.unpacked_data = NULL;
		model->collision_data.unpacked_size = 0;
	}

	model->collision_data.loadstate = model->collision_data.unpacked_data?GEO_LOADED:GEO_LOADED_NULL_DATA;

	etlAddEvent(geo_event_timer, "Load ColData", ELT_CODE, ELTT_END);

	InterlockedDecrement(&number_of_models_in_background_loader);

	PERFINFO_AUTO_STOP();
};

static void bubbleCallback(const char *filename_unused, void *dwParam)
{
	PERFINFO_AUTO_START_FUNC();
	printf("geoQueueBubble sleeping...\n");
	Sleep((intptr_t)dwParam);
	printf("geoQueueBubble done sleeping.\n");
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND;
void geoQueueBubble(int ms)
{
	fileLoaderRequestAsyncExec("Bubble", FILE_LOW_PRIORITY, false, bubbleCallback, (void*)(intptr_t)ms );
}

typedef struct CallBackData {
	GeoCallbackFunc pfcAPC;
	GeoRenderInfo *geo_render_info;
	void *parent;
	int param;
	char *caller;
	char *callee;
} CallBackData;

static int backgroundExecTimer=0;

static void backgroundExecFunc(const char *filename_unused, void *dwParam)
{
	char buffer[1024];
	CallBackData *data = dwParam;
	PERFINFO_AUTO_START_FUNC();
	sprintf(buffer, "%s: %s", data->caller, data->callee);
	if (!backgroundExecTimer)
		backgroundExecTimer = timerAlloc();
	timerStart(backgroundExecTimer);
	etlAddEvent(geo_event_timer, buffer, ELT_CODE, ELTT_BEGIN);
	data->pfcAPC(data->geo_render_info, data->parent, data->param);
	etlAddEvent(geo_event_timer, buffer, ELT_CODE, ELTT_END);
	if (timerElapsed(backgroundExecTimer) > 0.1f) {
		memlog_printf(NULL, "%s(%08p) took %1.2fs (requested in %s)", data->callee, data->parent, timerElapsed(backgroundExecTimer), data->caller);
	}
	free(data);
	InterlockedDecrement(&number_of_execs_in_background_loader);
	PERFINFO_AUTO_STOP();
}

void geoRequestBackgroundExecEx(GeoCallbackFunc pfnAPC, const char *filename, GeoRenderInfo *geo_render_info, void *parent, int param, FileLoaderPriority priority, char *caller, char *callee)
{
	CallBackData * data = malloc(sizeof(*data));
	InterlockedIncrement(&number_of_execs_in_background_loader);
	data->pfcAPC = pfnAPC;
	data->geo_render_info = geo_render_info;
	data->parent = parent;
	data->param = param;
	data->caller = caller;
	data->callee = callee;
	fileLoaderRequestAsyncExec(filename?filename:"geo", priority, false, backgroundExecFunc, data);
}

void geoForceBackgroundLoaderToFinish_dbg(const char *blamee)
{
	static DWORD mainThread;
    memlog_printf(NULL, "Stall waiting for background loader caused by %s::geoForceBackgroundLoaderToFinish", blamee);
	fileDiskAccessCheck();
	if (!mainThread)
		mainThread = GetCurrentThreadId();
	assert(mainThread == GetCurrentThreadId());
	while(number_of_models_in_background_loader || number_of_execs_in_background_loader)
	{
		Sleep(1);
	}
}

// Forces a stall waiting for the background data loader to finish.
AUTO_COMMAND ACMD_NAME("forceBackgroundLoaderToFinish") ACMD_CATEGORY(Debug);
void geoForceBackgroundLoaderToFinishCmd(void)
{
	geoForceBackgroundLoaderToFinish();
}

long geoLoadsPending(int include_misc)
{
	if (include_misc) {
		return number_of_models_in_background_loader + number_of_execs_in_background_loader;
	} else {
		return number_of_models_in_background_loader;
	}
}

void modelLODRequestBackgroundLoad(ModelLOD *model)
{
	PERFINFO_AUTO_START_FUNC();
	// assert(PERFINFO_IS_GOOD_THREAD);
	model->data_last_used_timestamp = wl_frame_timestamp;
	if(model->loadstate == GEO_NOT_LOADED)
	{
		EnterCriticalSection(&model_lod_init_cs);
		if(model->loadstate == GEO_NOT_LOADED)
		{
			assert(!model->data);
			if (model->model_parent->header && stricmp(model->model_parent->header->filename, "OrphanedModelHeader")==0)
			{
				assert(!isProductionMode()); // Can only happen on reload, shouldn't be possible in production unless there's bad data or something
				// Trying to load a model which no longer exists
				model->loadstate = GEO_LOAD_FAILED;
				memlog_printf(&geo_memlog, "modelLODRequestBackgroundLoad(%s) FAILED - Orphaned model", model->model_parent->name);
			} else {
				model->loadstate = GEO_LOADING;
				memlog_printf(&geo_memlog, "modelLODRequestBackgroundLoad(%s)", model->model_parent->name);

				// STREAMINGTODO: If we want to stream models, this needs an actual filename here (or call the fopen callback async)
				fileLoaderRequestAsyncExec("model", FILE_MEDIUM_PRIORITY, false, heyThreadLoadAGeo, model); // Cannot be higher priority than geo2UpdateBinsForGeo
				InterlockedIncrement(&number_of_models_in_background_loader);
			}
		}
		LeaveCriticalSection(&model_lod_init_cs);
	}
	PERFINFO_AUTO_STOP();
}

void modelColRequestBackgroundLoad(Model *model)
{
	PERFINFO_AUTO_START_FUNC();
	// assert(PERFINFO_IS_GOOD_THREAD);
	assert(model->collision_data.loadstate!=0);
	if(model->collision_data.loadstate == GEO_NOT_LOADED || model->collision_data.loadstate == GEO_LOADED_LOST_DATA)
	{
		EnterCriticalSection(&model_lod_init_cs);
		if(model->collision_data.loadstate == GEO_NOT_LOADED || model->collision_data.loadstate == GEO_LOADED_LOST_DATA)
		{
			if (model->header && stricmp(model->header->filename, "OrphanedModelHeader")==0)
			{
				// Trying to load a model which no longer exists
				model->collision_data.loadstate = GEO_LOAD_FAILED;
				memlog_printf(&geo_memlog, "modelColRequestBackgroundLoad(%s) FAILED - Orphaned model", model->name);
			} else {
				model->collision_data.loadstate = GEO_LOADING;
				memlog_printf(&geo_memlog, "modelColRequestBackgroundLoad(%s)", model->name);

				// STREAMINGTODO: If we want to stream models, this needs an actual filename here (or call the fopen callback async)
				fileLoaderRequestAsyncExec("modelCol", FILE_HIGH_PRIORITY, false, heyThreadLoadAColData, model); // Cannot be higher priority than geo2UpdateBinsForGeo
				InterlockedIncrement(&number_of_models_in_background_loader);
			}
		}
		LeaveCriticalSection(&model_lod_init_cs);
	}
	PERFINFO_AUTO_STOP();
}

typedef struct ModelSearchData {
	const char*	name;
	int		namelen;
} ModelSearchData;

/// Compares two model names.
///
/// This code is based off of the (no longer used) opt_strnicmp.
static int modelNameCmp( const char* src, const char* dst, int count )
{
	if ( count )
	{
		//		if ( __lc_handle[LC_CTYPE] == _CLOCALEHANDLE ) {
		int f, l;
		do {

			if ( ((f = (unsigned char)(*(src++))) >= 'A') &&
				(f <= 'Z') )
				f -= 'A'-'a';

			if ( ((l = (unsigned char)(*(dst++))) >= 'A') &&
				(l <= 'Z') )
				l -= 'A'-'a';

            if ( f == '-' )
                f = '_';
            if( l == '-' )
                l = '_';

		} while ( --count && f && (f == l) );
		//		}
		//		else { // localized version?
		//			int f,l;
		//			do {
		//				f = tolower( (unsigned char)(*(dst++)) );
		//				l = tolower( (unsigned char)(*(src++)) );
		//			} while (--count && f && (f == l) );
		//		}

		return( f - l ); // note: that's not a "one" it's an "L"
	}
	return 0;
}

static int compareModelNamesAndLengths(const char* name1, int len1, const char* name2, int len2)
{
	int ret;
	
	ret = modelNameCmp(name1, name2, min(len1, len2));
	
	if(!ret && len1 != len2)
	{
		if(len1 < len2)
		{
			ret = -1;
		}
		else
		{
			ret = 1;
		}
	}
	
	return ret;	
}

// Reloaded or initial load
void modelHeaderReloaded(Model *model)
{
	ModelHeader *model_header = model->header;
	// Asynchronously make sure the bin files are up to date by the time a load request comes in
	if (!model_header->missing_data)
		if (!isProductionMode())
			geo2UpdateBinsForModel(model_header);
	model->radius = model_header->radius;
	model->name = model_header->modelname;
	model->header = model_header;
	copyVec3(model_header->min, model->min);
	copyVec3(model_header->max, model->max);
	addVec3(model_header->min, model_header->max, model->mid);
	scaleVec3(model->mid, 0.5, model->mid);
	// Setup LODs
	modelInitLODs(model, 0);
}

WLUsageFlags model_override_use_flags;
Model *modelFromHeader(ModelHeader *model_header, bool load, WLUsageFlags use_flags)
{
	Model *model;
	//assert(!(use_flags & WL_FOR_NOTSURE));
	EnterCriticalSection(&model_lod_init_cs);
	if (!stashFindPointer(ht_models, model_header->modelname, &model))
	{
		// Allocate us a Model and fill it in
		model = calloc(sizeof(*model), 1);
		model->header = model_header;
		verify(stashAddPointer(ht_models, model_header->modelname, model, false));
		model->use_flags = use_flags;
		model->collision_data.loadstate = GEO_NOT_LOADED;
		model->collision_data.data_load_callback = geo2OpenDataFile;
		model->collision_data.data_load_user_data = NULL;
		modelHeaderReloaded(model);
	}
	LeaveCriticalSection(&model_lod_init_cs);
	if( model_override_use_flags )
	{
		model->use_flags = model_override_use_flags;
	}
	else
	{
		model->use_flags &= ~WL_FOR_OVERRIDE;
		model->use_flags |= use_flags;
	}
	
	assert(model);
	// Start loading if needed
	// I'm not completely sure we really want a load parameter at all in the
	//  new world order of Model and ModelLOD handling...
	if (load)
		modelLoadLOD(model, 0);
	return model;
}

Model *modelFind(const char *modelname, bool load, WLUsageFlags use_flags)
{
	ModelHeader *model_header = wlModelHeaderFromName(modelname);
	if (model_header)
		return modelFromHeader(model_header, load, use_flags);
	return NULL;
}

Model *modelFindEx(const char *filename, const char *modelname, bool load, WLUsageFlags use_flags)
{
	char buf[512];
	Model *ret = NULL;
	if (filename)
	{
		wlCharacterModelKey_s(SAFESTR(buf), filename, modelname);
		ret = modelFind(buf, load, use_flags);
	}
	if (!ret)
		ret = modelFind(modelname, load, use_flags);
	return ret;
}


bool modelExists(const char *modelname)
{
	return !!wlModelHeaderFromName(modelname);
}

void geoProcessTempData(GeoMeshHandler meshHandler,
						void* userPointer,
						const Model *model,
						int lod_index,
						const Vec3 scale,
						S32 useTris,
						S32 useTexCoords,
						S32 useWeights,
						S32 useNormals,
						const GroupSplineParams* spline)
{
	Vec3*	verts = NULL;
	U32		vertsMemorySize;
	GeoMeshTempData td = {0};
	ModelLOD *model_lod;

	if (!model)
		return;
		
	PERFINFO_AUTO_START_FUNC();

	model_lod = modelLODWaitForLoad(model, lod_index);
	if (!model_lod || !model_lod->data)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	modelLockUnpacked(model_lod);
	assert(model_lod->data);
	td.model = model_lod;
	td.vert_count = model_lod->vert_count;
	td.verts = modelGetVerts(model_lod);

	vertsMemorySize = model_lod->vert_count * sizeof(Vec3);

	if (useTris)
	{
		td.tri_count = model_lod->data->tri_count;
		td.tris = modelGetTris(model_lod);
	}
	if (useTexCoords)
	{
		td.sts = modelGetSts(model_lod);
	}
	if (useWeights)
	{
		td.weights = modelGetWeights(model_lod);
		td.boneidxs = modelGetMatidxs(model_lod);
	}
	if (useNormals)
	{
		td.norms = modelGetNorms(model_lod);
	}

	if (spline || scale)
	{
		td.verts = verts = ScratchAlloc(vertsMemorySize);
		memcpy(verts, model_lod->unpack.verts, vertsMemorySize);
	}
	
	if (spline)
	{
		int v;
		Vec3 in, out, up, tangent;
		
		PERFINFO_AUTO_START("spline", 1);
		
		for (v = 0; v < model_lod->vert_count; v++)
		{
			setVec3(in, verts[v][0], verts[v][1], 0);
			splineEvaluate(spline->spline_matrices, -verts[v][2]/spline->spline_matrices[1][2][0], in, out, up, tangent);
			if (out[0] < -1e6 || out[0] > 1e6 ||
				out[1] < -1e6 || out[1] > 1e6 ||
				out[2] < -1e6 || out[2] > 1e6)
				setVec3(out, 0, 0, 0);
			copyVec3(out, verts[v]);
		}
		
		PERFINFO_AUTO_STOP();
	}

	if (scale)
	{
		int v;
		PERFINFO_AUTO_START("scale", 1);
		for (v = 0; v < model_lod->vert_count; v++)
		{
			mulVecVec3(verts[v], scale, verts[v]);
		}
		PERFINFO_AUTO_STOP();
	}
	
	PERFINFO_AUTO_START("meshHandler", 1);
	meshHandler(userPointer, &td);
	PERFINFO_AUTO_STOP();

	modelUnlockUnpacked(model_lod);

	if (verts)
		ScratchFree(verts);

	PERFINFO_AUTO_STOP();
}

void geoGetCookedMeshName(char *buffer, int buffer_size, char *detail_buf, int detail_buf_size, const Model *model, const GroupSplineParams* spline, const Vec3 scale)
{
	devassert(model);
	sprintf_s(SAFESTR2(buffer), "%p", model);

	if(detail_buf)
		sprintf_s(SAFESTR2(detail_buf), "%s", model->name);

	if (spline)
	{
		U32 spline_crc = cryptAdler32((U8*)spline, sizeof(GroupSplineParams));
		strcatf_s(SAFESTR2(buffer), " SPLINE(%d)", spline_crc);
		if(detail_buf)
			strcatf_s(SAFESTR2(detail_buf), " SPLINE(%d)", spline_crc);
	}
	else if (scale)
	{
		Vec3 model_scale;
		groupQuantizeScale(scale, model_scale);
		if (!sameVec3(model_scale, unitvec3))
		{
			strcatf_s(SAFESTR2(buffer), " SCALE(%.1f %.1f %.1f)", model_scale[0], model_scale[1], model_scale[2]);
			if(detail_buf)
				strcatf_s(SAFESTR2(detail_buf), " SCALE(%.1f %.1f %.1f)", model_scale[0], model_scale[1], model_scale[2]);
		}
	}
}

typedef struct GeoCookMeshHelperData {
	char						name[200];
	PSDKCookedMesh**			mesh;
	bool						no_error_msg;
} GeoCookMeshHelperData;

static void geoCookMeshHelper(	GeoCookMeshHelperData* hd,
								const GeoMeshTempData* td)
{
	#if !PSDK_DISABLED
	{
		PSDKMeshDesc	meshDesc = {0};
		
		// Now cook the stream
		
		meshDesc.name = hd->name;
		meshDesc.vertArray = td->verts;
		meshDesc.vertCount = td->vert_count;
		meshDesc.triArray = td->tris;
		meshDesc.triCount = td->tri_count;
		meshDesc.sphereRadius = td->sphereRadius;
		meshDesc.no_error_msg = hd->no_error_msg;
		copyVec3(td->boxSize, meshDesc.boxSize);
	
		psdkCookedMeshCreate(hd->mesh, &meshDesc);
	}
	#endif
}

typedef struct ScaledCollision
{
	Vec3 scale;
	bool spline;
	Model *model;
	WorldCollisionEntry **entry_refs;
	PSDKCookedMesh* psdkCookedMesh;
	U32 last_used_timestamp;
} ScaledCollision;

static ScaledCollision **g_collisions_to_check_for_unload;

void modelPrintCollisionCounts(Model *model, U32 *cmTotalBytes, int *wcoTotalCount, U32 *wcoTotalBytes, bool show_header)
{
#if !PSDK_DISABLED
	int i;
#endif
	int convex = 0, scaled = 0, splined = 0, refs = 0;
	U32 cmBytes = 0, bytesPerWCO = 150 + 350;

	if (show_header)
		printf("CMs  (cvx,scl,spln,        mem), WCOs (        mem) - model name\n");

	if (!model)
		return;

#if !PSDK_DISABLED
	for (i = 0; i < eaSize(&model->psdkScaledConvexMeshes); ++i)
	{
		ScaledCollision *col = model->psdkScaledConvexMeshes[i];
		convex++;
		cmBytes += psdkCookedMeshGetBytes(col->psdkCookedMesh);
	}

	for (i = 0; i < eaSize(&model->psdkScaledTriangleMeshes); ++i)
	{
		ScaledCollision *col = model->psdkScaledTriangleMeshes[i];
		if (col->spline)
			splined++;
		else
			scaled++;
		refs += eaSize(&col->entry_refs);
		cmBytes += psdkCookedMeshGetBytes(col->psdkCookedMesh);
	}
#endif
	if (convex + scaled + splined > 0)
	{
		printf("% 4d (%d, % 2d, % 4d, % 11s)", convex + scaled + splined, convex, scaled, splined, friendlyBytes(cmBytes));
		printf(", % 4d (% 11s)", refs, friendlyBytes(refs * bytesPerWCO));
		printf(" - %s\n", model->name);
	}

	*cmTotalBytes += cmBytes;
	*wcoTotalCount += refs;
	*wcoTotalBytes += refs * bytesPerWCO;
}

static void verifyScaledCollision(ScaledCollision *col)
{
	int i;
	//assert(PERFINFO_IS_GOOD_THREAD);
	for (i=0; i<eaSize(&col->entry_refs); i++) {
		assert(col->entry_refs[i]);
		assert(col->entry_refs[i]->model_col == col);
	}
}

void modelDestroyScaledCollision(ScaledCollision *col)
{
	int i;

	if (!col)
		return;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	eaFindAndRemoveFast(&g_collisions_to_check_for_unload, col);

//	verifyScaledCollision(col);

	for (i = 0; i < eaSize(&col->entry_refs); ++i)
		col->entry_refs[i]->model_col = NULL;

#if !PSDK_DISABLED
	psdkCookedMeshDestroy(&col->psdkCookedMesh);
#endif
	eaDestroy(&col->entry_refs);
	free(col);

	PERFINFO_AUTO_STOP();
}

void modelUnloadCollisionsCheck(U32 unloadTime)
{
	FOR_EACH_IN_EARRAY(g_collisions_to_check_for_unload, ScaledCollision, col)
	{
		if (!eaSize(&col->entry_refs))
		{
			U32 delta = wl_frame_timestamp - col->last_used_timestamp;
			if (delta >= unloadTime)
			{
				eaFindAndRemoveFast(&col->model->psdkScaledTriangleMeshes, col);
				modelDestroyScaledCollision(col); // Also removes it from this array
			} else {
				// Leave in array
			}
		} else {
			// References appeared again, leave it around, remove from array
			eaRemoveFast(&g_collisions_to_check_for_unload, icolIndex);
		}
	}
	FOR_EACH_END;
}

PSDKCookedMesh* geoCookConvexMesh(Model *model, const Vec3 scale, int lod_index, bool no_error_msg)
{
	PSDKCookedMesh** mesh = NULL;
	Vec3 mesh_scale = {1,1,1};
	ScaledCollision *col = NULL;
	int i;
	
	if (scale)
		groupQuantizeScale(scale, mesh_scale);

	for (i = 0; i < eaSize(&model->psdkScaledConvexMeshes); ++i)
	{
		if (sameVec3(model->psdkScaledConvexMeshes[i]->scale, mesh_scale))
			col = model->psdkScaledConvexMeshes[i];
	}

	if (!col)
	{
		col = calloc(1, sizeof(ScaledCollision));
		col->model = model;
		copyVec3(mesh_scale, col->scale);
		eaPush(&model->psdkScaledConvexMeshes, col);
	}
//	verifyScaledCollision(col);

	mesh = &col->psdkCookedMesh;

	scale = mesh_scale;
	if (sameVec3(scale, unitvec3))
		scale = NULL;

	if (!*mesh)
	{
		GeoCookMeshHelperData hd = {0};
		hd.no_error_msg = no_error_msg;
		
		geoGetCookedMeshName(SAFESTR(hd.name), NULL, 0, model, NULL, scale);

		hd.mesh = mesh;

		geoProcessTempData(	geoCookMeshHelper,
			&hd,
			model,
			lod_index,
			scale,
			false,
			false,
			false,
			false,
			NULL);
	}

	return *mesh;
}

void geoScaledCollisionRemoveRef(WorldCollisionEntry *entry)
{
	//ASSERT_CALLED_IN_SINGLE_THREAD;
	ScaledCollision *col = SAFE_MEMBER(entry, model_col);

	if (!col)
		return;

//	verifyScaledCollision(col);

	eaFindAndRemoveFast(&col->entry_refs, entry);
	entry->model_col = NULL;

//	verifyScaledCollision(col);

	// actual cooked mesh data will be freed by modelUnloadCheck()
	col->last_used_timestamp = wl_frame_timestamp;

	if (eaSize(&col->entry_refs) == 0)
		eaPushUnique(&g_collisions_to_check_for_unload, col);
}

PSDKCookedMesh *geoScaledCollisionGetCookedMesh(ScaledCollision *col)
{
// 	if (col)
// 		verifyScaledCollision(col);
	return SAFE_MEMBER(col, psdkCookedMesh);
}

static int geoGetCollisionModelIndex(Model *model)
{
	ModelLOD *model_lod;
	int i = 0;

	if (model->lod_info && model->lod_info->high_detail_high_lod && eaSize(&model->model_lods) > 1)
		i = 1;

	for (; i<eaSize(&model->model_lods); i++) 
	{
		model_lod = modelLoadLOD(model, i);
		if (model_lod)
			return i;
	}

	assert(0); // At least one LOD in the model should have geometry, right?
	return 0;
}

#define MODEL_COLLISION_NAME_SUFFIX "_COLL"

Model * modelGetCollModel(Model * pRenderModel)
{
	Model * pModelToUse = NULL;
	char colModelName[1024];
	STR_COMBINE_SS(colModelName, pRenderModel->name, "_COLL");
	if ( (pModelToUse = modelFind(colModelName, true, pRenderModel->use_flags)) )
	{
		// Using this collision model instead
	} else {
		pModelToUse = pRenderModel;
	}
	
	return pModelToUse;
}

PSDKCookedMesh* geoCookMesh(Model *model, const Vec3 scale, const GroupSplineParams* spline, WorldCollisionEntry *entry, bool bDoCooking, bool bWaitForLoad)
{
	PSDKCookedMesh *meshptr = NULL;
	PSDKCookedMesh **mesh = &meshptr;
	Vec3 mesh_scale = {1,1,1};
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (model->lod_info && model->lod_info->high_detail_high_lod && eaSize(&model->model_lods) > 1)
	{
		char colModelName[1024];
		Model *model_to_use;
		STR_COMBINE_SS(colModelName, model->name, MODEL_COLLISION_NAME_SUFFIX);
		if ( (model_to_use = modelFind(colModelName, true, model->use_flags)) )
		{
			// Using this collision model instead, should have been baked in with it and will be used automatically below.
		} else {
			// Otherwise, use logic to use the next LOD down's collision
			ModelLOD *model_lod = modelLODLoadAndMaybeWait(model, 1, bWaitForLoad);
			if (!model_lod)
			{
				PERFINFO_AUTO_STOP();
				return NULL;
			}

			model = model_lod->actual_lod->model_parent;
		}
	}
	
	if (entry)
	{
		ScaledCollision *col = NULL;

		if (entry->model_col)
			col = entry->model_col;

		if (!spline && scale)
			groupQuantizeScale(scale, mesh_scale);

		for (i = 0; i < eaSize(&model->psdkScaledTriangleMeshes); ++i)
		{
			if (sameVec3(model->psdkScaledTriangleMeshes[i]->scale, mesh_scale) && !model->psdkScaledTriangleMeshes[i]->spline && !spline)
				col = model->psdkScaledTriangleMeshes[i];
		}

		if (!col)
		{
			col = calloc(1, sizeof(ScaledCollision));
			col->model = model;
			copyVec3(mesh_scale, col->scale);
			col->spline = !!spline;
			eaPush(&model->psdkScaledTriangleMeshes, col);
		}
// 		verifyScaledCollision(col);

		mesh = &col->psdkCookedMesh;

		if (entry->model_col != col)
		{
			assert(!entry->model_col);
			eaPush(&col->entry_refs, entry);
			entry->model_col = col;
		}
// 		verifyScaledCollision(col);

		scale = mesh_scale;
		if (sameVec3(scale, unitvec3) || spline)
			scale = NULL;
	}

	if (!*mesh && bDoCooking)
	{
		if (!disable_precooked_data && !spline && (!scale || sameVec3(scale, onevec3)))
		{
			U32 col_size;
			// releases ownership to us upon calling this function
			void *col_data = modelColLoadAndMaybeWait(model, &col_size, bWaitForLoad);
#if !PSDK_DISABLED
			char			buffer[200];
			PSDKMeshDesc	meshDesc = {0};
#endif
			if (!col_data)
			{
				PERFINFO_AUTO_STOP();
				return NULL;
			}

#if !PSDK_DISABLED
			geoGetCookedMeshName(SAFESTR(buffer), NULL, 0, model, spline, scale);

			// Load the precooked stream
			meshDesc.name = buffer;
			meshDesc.preCookedData = col_data;
			meshDesc.preCookedSize = col_size;

			psdkCookedMeshCreate(mesh, &meshDesc);
#endif
		} else {
			GeoCookMeshHelperData hd = {0};
			Model *model_to_use;
			int model_lod_index;
			ModelLOD *model_lod;

			model_to_use = modelGetCollModel(model);

			model_lod_index = geoGetCollisionModelIndex(model_to_use);
			model_lod = modelLODLoadAndMaybeWait(model_to_use, model_lod_index, bWaitForLoad);
			if (!model_lod)
			{
				PERFINFO_AUTO_STOP();
				return NULL;
			}

			hd.mesh = mesh;

			geoGetCookedMeshName(SAFESTR(hd.name), NULL, 0, model, spline, scale);

			geoProcessTempData(	geoCookMeshHelper,
								&hd,
								model_to_use,
								model_lod_index,
								scale,
								true,
								false,
								false,
								false,
								spline);
		}
	}

	PERFINFO_AUTO_STOP();

	return *mesh;
}

PSDKCookedMesh* geoCookSphere(F32 radius)
{
#if !PSDK_DISABLED
	PSDKCookedMesh*	mesh;
	PSDKMeshDesc	meshDesc = {0};
	
	meshDesc.name = "sphere";
	meshDesc.sphereRadius = radius;

	if(psdkCookedMeshCreate(&mesh, &meshDesc)){
		return mesh;
	}
#endif
	return NULL;
}

PSDKCookedMesh* geoCookCapsule(F32 radius, F32 height)
{
#if !PSDK_DISABLED
	PSDKCookedMesh*	mesh;
	PSDKMeshDesc	meshDesc = {0};
	
	meshDesc.name = "capsule";
	meshDesc.sphereRadius = radius;
	meshDesc.capsuleHeight = height;

	if(psdkCookedMeshCreate(&mesh, &meshDesc)){
		return mesh;
	}
#endif
	return NULL;
}

PSDKCookedMesh* geoCookBox(const Vec3 boxSize)
{
#if !PSDK_DISABLED
	PSDKCookedMesh*	mesh;
	PSDKMeshDesc	meshDesc = {0};
	
	meshDesc.name = "box";
	copyVec3(boxSize, meshDesc.boxSize);

	if(psdkCookedMeshCreate(&mesh, &meshDesc)){
		return mesh;
	}
#endif	
	return NULL;
}

// system memory used
int modelLODGetBytesCompressed(ModelLOD *model)
{
	int ret=sizeof(ModelLOD);
	if (model->data)
		ret += model->data->data_size;
	return ret;
}

// uncompressed size, only ever used temporarily, should tend to 0
int modelLODGetBytesUncompressed(ModelLOD *model)
{
	int ret =0;
#define DOIT(field, count)\
		if (model->unpack.field)\
			ret += sizeof(model->unpack.field[0])*model->count;
	DOIT(tris, tri_count*3);
	DOIT(verts, vert_count);
	DOIT(verts2, vert_count);
	DOIT(norms, vert_count);
	DOIT(norms2, vert_count);
	DOIT(binorms, vert_count);
	DOIT(tangents, vert_count);
	DOIT(sts, vert_count);
	DOIT(sts3, vert_count);
	DOIT(colors, vert_count*4);
	DOIT(weights, vert_count*4);
	DOIT(matidxs, vert_count*4);
#undef DOIT
	return ret;
}

// video memory used
int modelLODGetBytesUnpacked(ModelLOD *model)
{
	int vert_size = 0;
	if (!model->data)
		return 0;
	if (model->data->pack.verts.unpacksize)
		vert_size += sizeof(Vec3);
	if (model->data->pack.verts2.unpacksize)
		vert_size += sizeof(Vec3);
	if (model->data->pack.norms.unpacksize)
		vert_size += sizeof(Vec3_Packed);
	if (model->data->pack.norms2.unpacksize)
		vert_size += sizeof(Vec3_Packed);
	if (model->data->pack.binorms.unpacksize)
		vert_size += sizeof(Vec3_Packed);
	if (model->data->pack.tangents.unpacksize)
		vert_size += sizeof(Vec3_Packed);
	if (model->data->pack.sts.unpacksize)
	{
		if (model->data->process_time_flags & MODEL_PROCESSED_HIGH_PRECISCION_TEXCOORDS)
			vert_size += sizeof(Vec2);
		else
			vert_size += sizeof(F16)*2; // size on video card
	}
	if (model->data->pack.sts3.unpacksize)
	{
		if (model->data->process_time_flags & MODEL_PROCESSED_HIGH_PRECISCION_TEXCOORDS)
			vert_size += sizeof(Vec2);
		else
			vert_size += sizeof(F16)*2; // size on video card
	}
	if (model->data->pack.colors.unpacksize)
		vert_size += 4*sizeof(U8);
	if (model->data->pack.weights.unpacksize)
		vert_size += 4*sizeof(U8); // size on video card
	if (model->data->pack.matidxs.unpacksize)
		vert_size += 4*sizeof(U16); // size on video card

	return (model->data->pack.tris.unpacksize?(model->data->tri_count*3*sizeof(U16)):0) + model->data->vert_count*vert_size;
}

int modelLODGetBytesTotal(ModelLOD *model_lod)
{
	return modelLODGetBytesCompressed(model_lod) + modelLODGetBytesUnpacked(model_lod);
}

int modelLODGetBytesSystem(ModelLOD *model_lod)
{
	return modelLODGetBytesCompressed(model_lod);
}

WLUsageFlags modelGetUseFlags(const Model *model)
{
	return model->use_flags;
}

void modelLODUpdateMemUsage(ModelLOD *model_lod)
{
	U32 new_mem_usage = modelLODGetBytesSystem(model_lod);
	U32 old_mem_usage = InterlockedExchange(&model_lod->last_mem_usage, new_mem_usage);
	S32 delta = new_mem_usage - old_mem_usage;
	if (delta)
	{
		InterlockedExchangeAdd(&wl_geo_mem_usage.loadedSystem[model_lod->mem_usage_bitindex], delta);
		InterlockedExchangeAdd(&wl_geo_mem_usage.loadedSystemTotal, delta);
	}
}

void wlGeoGetMemUsageQuick(SA_PRE_NN_FREE SA_POST_NN_VALID GeoMemUsage *usage)
{
	*usage = wl_geo_mem_usage;
}

AUTO_RUN;
void registerModelHeaderDictionary(void)
{
	hModelHeaderDict = RefSystem_RegisterSelfDefiningDictionary("ModelHeader", false, parse_ModelHeader, true, true, NULL);
}

static StashTable stModelHeaderSets;

ModelHeaderSet *modelHeaderSetFindOrAlloc(const char *filename, bool alloc)
{
	static const char *last_filename;
	static const char *last_setname;
	static ModelHeaderSet *last_set;
	StashElement element;
	char setnamebuf[MAX_PATH];
	const char *setname;
	if (filename == last_filename)
		return last_set;
	getFileNameNoExt(setnamebuf, filename);
	if (alloc)
		setname = allocAddString(setnamebuf);
	else {
		setname = allocFindString(setnamebuf);
		if (!setname)
			return NULL;
	}
	ANALYSIS_ASSUME(setname);
	if (setname == last_setname)
		return last_set;
	if (!stModelHeaderSets) {
		stModelHeaderSets = stashTableCreateWithStringKeys(4096, StashDefault);
	}
	if (stashFindElement(stModelHeaderSets, setname, &element)) {
		last_filename = filename;
		last_setname = setname;
		last_set = stashElementGetPointer(element);
	} else {
		if (!alloc)
			return NULL;
		last_filename = filename;
		last_setname = setname;
		last_set = callocStruct(ModelHeaderSet);
		last_set->filename = filename;
		stashAddPointer(stModelHeaderSets, setname, last_set, false);
	}
	return last_set;
}

ModelHeaderSet *modelHeaderSetFind(const char *filename)
{
	filename = allocAddFilename(filename); // Ideally the callers should be doing this for performance
	return modelHeaderSetFindOrAlloc(filename, false);
}


void modelHeaderAddToSet(ModelHeader *model_header)
{
	ModelHeaderSet *set;
	// Only ever actually need this info for character library models
	if (!strStartsWith(model_header->filename, "character_library"))
		return;
	set = modelHeaderSetFindOrAlloc(model_header->filename, true);
	eaPushUnique(&set->model_headers, model_header);
}

static void modelHeaderRemoveFromSet(ModelHeader *model_header)
{
	ModelHeaderSet *set;
	set = modelHeaderSetFindOrAlloc(model_header->filename, false);
	if (set)
	{
		eaFindAndRemoveFast(&set->model_headers, model_header);
	}
}


static Model**orphanedModels;

static void orphanModel(ModelHeader *model_header)
{
	Model *model;
	memlog_printf(&geo_memlog, "orphanModel(%p-%s)", model_header, model_header->modelname);

	EnterCriticalSection(&model_lod_init_cs);
	if (stashRemovePointer(ht_models, model_header->modelname, &model))
	{
		model->header = (ModelHeader*)model_header->modelname;
		eaPush(&orphanedModels, model);
	} else {
		memlog_printf(&geo_memlog, "orphanModel(%p-%s) - was not in ht_models", model_header, model_header->modelname);
	}
	LeaveCriticalSection(&model_lod_init_cs);
}

static ModelHeader orphaned_model_header;

static void dealWithOrphanedModels(void)
{
	Model **models_to_process=NULL;
	EnterCriticalSection(&model_lod_init_cs);
	// Reloading finished.  Do we have any Models hanging around with no ModelHeaders now?
	FOR_EACH_IN_EARRAY(orphanedModels, Model, model)
	{
		ModelHeader *model_header = wlModelHeaderFromName((const char *)model->header);
		if (model_header) {
			memlog_printf(&geo_memlog, "dealWithOrphanedModels():%p-%s found model_header %p-%s", model, (const char *)model->header,
				model_header, model_header->modelname);
			model->header = model_header;
			verify(stashAddPointer(ht_models, model_header->modelname, model, false));
			if (!model->use_flags)
				model->use_flags = WL_FOR_NOTSURE;
			eaRemoveFast(&orphanedModels, imodelIndex);
			eaPush(&models_to_process, model);
		} else {
			memlog_printf(&geo_memlog, "dealWithOrphanedModels():%p-%s no header found, orphaned", model, (const char *)model->header);
			if (!orphaned_model_header.filename)
			{
				orphaned_model_header.filename = "OrphanedModelHeader";
				orphaned_model_header.modelname = "OrphanedModel";
			}
			model->header = &orphaned_model_header; // Hope it's not used anywhere?
		}
	}
	FOR_EACH_END;

	// Do this as a separate pass so that if a model and its LOD were reloaded, it un-orphans the LOD before re-initing the model
	FOR_EACH_IN_EARRAY(models_to_process, Model, model)
	{
		modelHeaderReloaded(model);
	}
	FOR_EACH_END;
	eaDestroy(&models_to_process);
	LeaveCriticalSection(&model_lod_init_cs);
}

static bool g_modelheaders_from_text=false;
static StashTable stModelHeaderWarnedFiles;
static bool g_modelheaders_reloading=false;
static const char **g_modelheaders_reloaded=NULL;
static bool g_model_initial_load;
AUTO_FIXUPFUNC;
TextParserResult ModelHeader_Fixup(ModelHeader *model_header, enumTextParserFixupType eFixupType, void *pExtraData)
{
	TextParserResult ret = PARSERESULT_SUCCESS;
	char geo2name[MAX_PATH];

	switch (eFixupType)
	{
	xcase FIXUPTYPE_POST_TEXT_READ:
		assert(model_header->modelname);
		if (g_modelheaders_reloading)
		{
			eaPush(&g_modelheaders_reloaded, model_header->modelname);
		}
		g_modelheaders_from_text = true;
		changeFileExt(model_header->filename, ".geo2", geo2name);
		if (!fileExists(geo2name)) {
			if (!stModelHeaderWarnedFiles)
				stModelHeaderWarnedFiles = stashTableCreateWithStringKeys(16, StashDefault);
			if (!stashFindElement(stModelHeaderWarnedFiles, model_header->filename, NULL))
			{
				ErrorFilenameGroupf(model_header->filename, "Art", 14, "ModelHeader exists, but missing .geo2 file.");
				stashAddInt(stModelHeaderWarnedFiles, model_header->filename, 1, true);
			}
			model_header->missing_data = 1;
			ret = PARSERESULT_ERROR;
		} else if (!model_header->radius && !model_header->min[0]) {
			if (!stModelHeaderWarnedFiles)
				stModelHeaderWarnedFiles = stashTableCreateWithStringKeys(16, StashDefault);
			if (!stashFindElement(stModelHeaderWarnedFiles, model_header->filename, NULL))
			{
				ErrorFilenameGroupf(model_header->filename, "Art", 14, "ModelHeader is old format, needs reprocessing.");
				stashAddInt(stModelHeaderWarnedFiles, model_header->filename, 1, true);
			}
			model_header->missing_data = 1;
			ret = PARSERESULT_ERROR;
		}
		
	xcase FIXUPTYPE_DESTRUCTOR:
		if (!g_model_initial_load)
		{
			// From reload?
			// Orphan/save the Model* for this ModelHeader - can't free, other code still references it
			orphanModel(model_header);
			// Remove from ModelHeaderSet
			modelHeaderRemoveFromSet(model_header);
		}
	xcase FIXUPTYPE_POST_RELOAD:
	{
		Model *model;
		EnterCriticalSection(&model_lod_init_cs);
		if (stashFindPointer(ht_models, model_header->modelname, &model))
		{
			memlog_printf(&geo_memlog, "ModelHeader_Fixup():reload:header:%p model:%p -%s doing reload", model_header, model, model_header->modelname);
			modelHeaderReloaded(model);
		}
		LeaveCriticalSection(&model_lod_init_cs);
	}
	}

	return ret;
}

static void modelHeadersCheckDups(void)
{
	char key[MAX_PATH];
	StashTable stGeoNameToFileName;
	StashTable stWarnedFilenames;
	if (isProductionMode())
		return;
	stGeoNameToFileName = stashTableCreateWithStringKeys(RefSystem_GetDictionaryNumberOfReferents(hModelHeaderDict), StashDefault|StashDeepCopyKeys_NeverRelease);
	stWarnedFilenames = stashTableCreateWithStringKeys(16, StashDefault);
	FOR_EACH_IN_REFDICT(hModelHeaderDict, ModelHeader, model_header)
	{
		StashElement element;
		getFileNameNoExt(key, model_header->filename);
		if (stashFindElement(stWarnedFilenames, model_header->filename, &element))
			continue;
		if (stashFindElement(stGeoNameToFileName, key, &element))
		{
			const char *value = stashElementGetPointer(element);
			if (value != model_header->filename)
			{
				ErrorFilenameDup(value, model_header->filename, key, "Geo file");
				stashAddInt(stWarnedFilenames, model_header->filename, 1, true);
			}
		} else {
			stashAddPointer(stGeoNameToFileName, key, model_header->filename, false);
		}
	}
	FOR_EACH_END;
	stashTableDestroy(stWarnedFilenames);
	stashTableDestroy(stGeoNameToFileName);
}

static void modelHeaderSetVerify(void)
{
	FOR_EACH_IN_REFDICT(hModelHeaderDict, ModelHeader, model_header)
	{
		if (strStartsWith(model_header->filename, "character_library")) {
			ModelHeaderSet *set = modelHeaderSetFindOrAlloc(model_header->filename, false); 
			assert(set);
			assert(eaFind(&set->model_headers, model_header)!=-1);
			//assertmsgf(model_header->filename == set->filename, "character_library duplicate model in two locations:\n%s\n  and\n%s", model_header->filename, set->filename);
			// It's possible this is just going to cause some other crash later, and should be revert to an assert with details like above
			if (model_header->filename != set->filename)
			{
				ErrorFilenameDup(model_header->filename, set->filename, set->filename, "character_library model header");
			}
		} else {
			ModelHeaderSet *set = modelHeaderSetFindOrAlloc(model_header->filename, false); 
			if (set)
			{
				ErrorFilenameTwof(model_header->filename, set->filename,
							"ModelHeaderSet conflict: adding \"%s\" but \"%s\" already exists.",
							model_header->filename,
							set->filename);
			}
		}
	}
	FOR_EACH_END;
	FOR_EACH_IN_STASHTABLE(stModelHeaderSets, ModelHeaderSet, header_set)
	{
		FOR_EACH_IN_EARRAY(header_set->model_headers, ModelHeader, model_header)
		{
			assert(RefSystem_DoesReferentExist(model_header));
		}
		FOR_EACH_END
	}
	FOR_EACH_END;
}

static void modelHeaderSetInit(void)
{
	FOR_EACH_IN_REFDICT(hModelHeaderDict, ModelHeader, model_header)
	{
		modelHeaderAddToSet(model_header);
	}
	FOR_EACH_END;
}

static void modelHeaderReloadCallback(const char *relpath, int when)
{

	loadstart_printf("Reloading ModelHeaders...");
	waitForGetVrmlLock(true);
	g_modelheaders_reloading = true;
	if (stModelHeaderWarnedFiles)
		stashTableClear(stModelHeaderWarnedFiles);
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	memlog_printf(&geo_memlog, "modelHeaderReloadCallback(%s)", relpath);

	if(!ParserReloadFileToDictionaryWithFlags(relpath, hModelHeaderDict, PARSER_IGNORE_EXTENSIONS))
	{
		if (fileExists(relpath))
			ErrorFilenamef(relpath, "Error reloading ModelHeader file: %s", relpath);
	}
	// Any orphaned Models without ModelHeaders staying around?
	dealWithOrphanedModels();

	modelHeaderSetInit();

	// Now make sure the reload callback gets called on everything that was reloaded
	EnterCriticalSection(&model_lod_init_cs);
	FOR_EACH_IN_EARRAY(g_modelheaders_reloaded, const char, model_name)
	{
		Model *model;
		if (stashFindPointer(ht_models, model_name, &model))
		{
			memlog_printf(&geo_memlog, "modelHeaderReloadCallback(%s) reloading %p-%s", relpath, model, model->name);
			if (!model->use_flags)
				model->use_flags = WL_FOR_WORLD; // For some reason these flags sometimes get lost on reload?  Fix it up so it won't crash at least.
			modelHeaderReloaded(model);
		} else {
			memlog_printf(&geo_memlog, "modelHeaderReloadCallback(%s) model %s NOT in ht_models", relpath, model_name);
		}
	}
	FOR_EACH_END;
	eaSetSize(&g_modelheaders_reloaded, 0);
	LeaveCriticalSection(&model_lod_init_cs);

	modelHeaderSetVerify();

	// // Only really need to do this if any materials changed or other similar things
	// TODO: do something to cause swaps to be re-evaluated, this works once: reloadFileLayer(NULL); // Need to re-bin/re-swap/etc

	g_modelheaders_reloading = false;
	releaseGetVrmlLock();
	loadend_printf("done");

	loadstart_printf("Checking for duplicate geo files...");
	modelHeadersCheckDups();
	loadend_printf("done.");
}

void wlLoadModelHeaders( void )
{
	loadstart_printf("Loading ModelHeaders...");
	if (stModelHeaderWarnedFiles)
		stashTableClear(stModelHeaderWarnedFiles);
	g_model_initial_load = true;
	ParserLoadFilesSharedToDictionary("SM_ModelHeaders", "object_library/;character_library/", MODELHEADER_EXTENSION, "ModelHeaders.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, hModelHeaderDict);
	g_model_initial_load = false;

	modelHeaderSetInit();

	if (!ht_models)
		ht_models = stashTableCreateWithStringKeys(4096, StashDefault);

	modelHeaderSetVerify();

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "object_library/*" MODELHEADER_EXTENSION, modelHeaderReloadCallback);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "character_library/*" MODELHEADER_EXTENSION, modelHeaderReloadCallback);
	}

	loadend_printf("done (%d ModelHeaders)", RefSystem_GetDictionaryNumberOfReferents(hModelHeaderDict));

	// Check for duplicate geometry file names
	if (g_modelheaders_from_text || 1) // Can't actually only do this when re-binning, as even with dups, it bins successfully :(
	{
		loadstart_printf("Checking for duplicate geo files...");
		modelHeadersCheckDups();
		loadend_printf("done.");
	}

	if (!gpcMakeBinsAndExitNamespace)
	{
		// Verify once on each builder the next time it runs with -makeBinsAndExit
		if (wl_state.verifyAllGeoBins || gbMakeBinsAndExit && regGetAppInt("DidVerifyAllGeos", 0)!=2)
		{
			geo2VerifyBinsForAllGeos();
			regPutAppInt("DidVerifyAllGeos", 2);
		}
		if (wl_state.binAllGeos || gbMakeBinsAndExit)
		{
			geo2UpdateBinsForAllGeos();
		}
	}
}

ModelHeader* wlModelHeaderFromName(const char* modelname )
{
	ModelHeader *ret = RefSystem_ReferentFromString(hModelHeaderDict, modelname);
	if (!ret || ret->missing_data)
		return NULL;
	return ret;
}

// Doesn't allocAddString
static void wlCharacterModelKey_s(char *buf, size_t buf_size, const char* fname, const char* modelname)
{
	char cGeoName[256];

	if (strchr(modelname, '.')) // Already in the right format
	{
		strcpy_s(SAFESTR2(buf), modelname);
	} else {
		getFileNameNoExt(cGeoName, fname);

		sprintf_s(SAFESTR2(buf), "%s.%s", cGeoName, modelname);
	}
}

ModelHeader* wlModelHeaderFromNameEx(const char *filename, const char* modelname)
{
	char buf[512];
	ModelHeader *ret = NULL;
	if (filename)
	{
		wlCharacterModelKey_s(SAFESTR(buf), filename, modelname);
		ret = wlModelHeaderFromName(buf);
	}
	if (!ret)
		ret = wlModelHeaderFromName(modelname);
	return ret;
}


const char* wlCharacterModelKey(const char* fname, const char* modelname)
{
	char cConcatName[512];
	wlCharacterModelKey_s(SAFESTR(cConcatName), fname, modelname);
	return allocAddString(cConcatName);
}

FILE *geo2OpenLODDataFileByName(const char *filename)
{
	FILE *ret;
	char relpath[MAX_PATH];
	char binpath[MAX_PATH];

	if (strEndsWith(filename, ".mset"))
	{
		// Already referencing a .mset file
		strcpy(binpath, filename);
	} else {
		fileRelativePath(filename, relpath);
		geo2BinNameFromGeo2Name(relpath, binpath, ARRAY_SIZE_CHECKED(binpath));
	}

	errorIsDuringDataLoadingInc(binpath);
	ret = fileOpen(binpath, "rb");
	assert(ret);
	return ret;
}

FILE *geo2OpenDataFile(Model *model, void *unused)
{
	return geo2OpenLODDataFileByName(model->header->filename);
}

void geo2CloseDataFile(FILE *f)
{
	errorIsDuringDataLoadingDec();
	fclose(f);
}


// Actually load the data, should be called only from the loading thread (but is entirely self-contained, so should be safe regardless)
ModelLODData *geo2LoadLODData(FILE *f, const char *modelname, int lod_index, const char *filename)
{
	U32 i, version, headersize, myModelOffs=0, myModelLength=0;
	char actualModelName[MAX_PATH], *s;
	ModelLODData *ret = NULL;
	U16 modelcount;
	U8 *data, *ptr;

	if (!f)
		return NULL;

	// Note: when updating this function and the data format, also update geo2PrintBinFileInfo

	// Assume this is already ran: geo2UpdateBinsForGeoDirectInternal(filename);

	if (s = strchr(modelname, '.')) {
		strcpy(actualModelName, s+1);
	} else {
		strcpy(actualModelName, modelname);
	}
	modelname = actualModelName;

	// Read header, find offset
	// Just the header size is little endian, everything else is big
	fread(&headersize, 4, 1, f);
	headersize = endianSwapIfBig(U32, headersize);
	assertmsgf(headersize <= MAX_MODEL_BIN_HEADER_SIZE, "Data corruption found in \"%s\"%s", filename ? filename : modelname, GetCBDataCorruptionComment());
	assertmsgf(headersize >= MIN_MODEL_BIN_HEADER_SIZE, "Data corruption found in \"%s\"%s", filename ? filename : modelname, GetCBDataCorruptionComment());
	ptr = data = ScratchAlloc(headersize);
	fread(data, 1, headersize - 4, f);
#define fread_u32(var) var = endianSwapIfNotBig(U32, *(U32*)ptr); ptr+=4;
#define fread_u16(var) var = endianSwapIfNotBig(U16, *(U16*)ptr); ptr+=2;
	fread_u32(version);
#if (GEO2_CL_BIN_VERSION == GEO2_OL_BIN_VERSION)
	assert(version == GEO2_OL_BIN_VERSION);
#else
	assert(version == GEO2_CL_BIN_VERSION || version == GEO2_OL_BIN_VERSION);
#endif
	fread_u32(version);
	assert(version == ParseTableCRC(parse_ModelLODData, NULL, 0) || version == 0x21c585bb);
	fread_u16(modelcount);
	for (i=0; i<modelcount; i++)
	{
		U32 j;
		bool bMyModel;
		U16 lodcount;
		U16 slen;
		fread_u16(slen);
		s = ptr;
		bMyModel = (slen == strlen(modelname) && strnicmp(s, modelname, slen)==0);
		ptr += slen;
		fread_u16(lodcount);
		for (j=0; j<lodcount; j++) {
			if (bMyModel && j == lod_index) {
				fread_u32(myModelOffs);
				fread_u32(myModelLength);
			} else {
				ptr += 8; 
			}
		}
		ptr += 12; // skip past collision header
	}
	ScratchFree(data);
	data = ptr = NULL;

	if (myModelOffs) {
		U16 texnamecount;
		fseek(f, myModelOffs - headersize, SEEK_CUR);
		ret = malloc(myModelLength);

		fileLoaderStartLoad();
		fread(ret, 1, myModelLength, f);
		fileLoaderCompleteLoad(myModelLength);

		if (!isBigEndian())
		{
			endianSwapStruct(parse_ModelLODData, ret);
		}
		assert(myModelLength >= sizeof(*ret) + sizeof(*ret->tex_idx)*ret->tex_count); // Otherwise we're overrunning
		ret->tex_idx = (void*)(ret + 1);
		if (!isBigEndian())
		{
			TexID *texid = ret->tex_idx;
			for (i=0; i<(U32)ret->tex_count; i++, texid++)
			{
				texid->count = endianSwapU16(texid->count);
				texid->id = endianSwapU16(texid->id);
			}
		}
		// texture name table
		ptr = ((U8*)ret) + ret->data_size;
		fread_u16(texnamecount);
		ret->tex_names = malloc(texnamecount*sizeof(char*));
		for (i=0; i<texnamecount; i++)
		{
			U16 slen;
			char *sbuf;
			fread_u16(slen);
			sbuf = ScratchAlloc(slen+1);
			strncpy_s(sbuf, slen+1, ptr, slen); ptr += slen;
			sbuf[slen] = '\0';
			ret->tex_names[i] = allocAddString(sbuf);
			ScratchFree(sbuf);
		}
		// Validate texids
		for (i=0; i<(U32)ret->tex_count; i++) {
			if (ret->tex_idx[i].id >= texnamecount) {
				assert(0); // Corrupt model file?
				ret->tex_idx[i].id = 0;
			}
		}

		// Patch pointers
		patchPackPtr(&ret->pack.tris, ret);
		patchPackPtr(&ret->pack.verts, ret);
		patchPackPtr(&ret->pack.norms, ret);
		patchPackPtr(&ret->pack.binorms, ret);
		patchPackPtr(&ret->pack.tangents, ret);
		patchPackPtr(&ret->pack.sts, ret);
		patchPackPtr(&ret->pack.sts3, ret);
		patchPackPtr(&ret->pack.colors, ret);
		patchPackPtr(&ret->pack.weights, ret);
		patchPackPtr(&ret->pack.matidxs, ret);
		patchPackPtr(&ret->pack.verts2, ret);
		patchPackPtr(&ret->pack.norms2, ret);

	} else {
		// No model data stored, perhaps it should use the next higher LOD, or it's a hand-built LOD
	}
#undef fread_u32
#undef fread_u16

	return ret;
}


void geo2LoadColData(FILE *f, const char *modelname, Model *model, PackData* outPackedData)
{
	U32 i, version, headersize, myModelOffs=0, myModelTotalLength=0, myModelDataLength=0;
	char actualModelName[MAX_PATH], *s;
	U16 modelcount;
	U8 *data, *ptr;

	if (!f)
		return;

	// Note: when updating this function and the data format, also update geo2PrintBinFileInfo

	// Assume this is already ran: geo2UpdateBinsForGeoDirectInternal(filename);

	if (s = strchr(modelname, '.')) {
		strcpy(actualModelName, s+1);
	} else {
		strcpy(actualModelName, modelname);
	}
	modelname = actualModelName;

	// Read header, find offset
	// Just the header size is little endian, everything else is big
	fread(&headersize, 4, 1, f);
	headersize = endianSwapIfBig(U32, headersize);
	assertmsgf(headersize <= MAX_MODEL_BIN_HEADER_SIZE, "Data corruption found in \"%s\"%s", model->header ? model->header->filename : modelname, GetCBDataCorruptionComment());
	ptr = data = ScratchAlloc(headersize);
	fread(data, 1, headersize - 4, f);
#define fread_u32(var) var = endianSwapIfNotBig(U32, *(U32*)ptr); ptr+=4;
#define fread_s32(var) var = endianSwapIfNotBig(S32, *(S32*)ptr); ptr+=4;
#define fread_u16(var) var = endianSwapIfNotBig(U16, *(U16*)ptr); ptr+=2;
	fread_u32(version);
#if (GEO2_CL_BIN_VERSION == GEO2_OL_BIN_VERSION)
	assert(version == GEO2_OL_BIN_VERSION);
#else
	assert(version == GEO2_CL_BIN_VERSION || version == GEO2_OL_BIN_VERSION);
#endif
	fread_u32(version);
	assert(version == ParseTableCRC(parse_ModelLODData, NULL, 0) || version == 0x21c585bb);
	fread_u16(modelcount);
	for (i=0; i<modelcount; i++)
	{
		bool bMyModel;
		U16 lodcount;
		U16 slen;
		fread_u16(slen);
		s = ptr;
		bMyModel = (slen == strlen(modelname) && strnicmp(s, modelname, slen)==0);
		ptr += slen;
		fread_u16(lodcount);
		// skip past all model LODs
		ptr += 8*lodcount; 
		// read collision data
		if (bMyModel)
		{
			fread_u32(myModelOffs);
			fread_u32(myModelDataLength);
			fread_u32(myModelTotalLength);
		}
		else
		{
			ptr += 12; // skip past collision header
		}
	}
	ScratchFree(data);
	data = ptr = NULL;

	//assert(!myModelOffs == !myModelTotalLength); // Current files can have 0 TotalLength and DataLength if they failed to cook
	//assert(!myModelTotalLength == !myModelDataLength); // Old files can have 0 DataLength if they failed to cook

	if (myModelOffs && myModelDataLength)
	{
		int packsize;
		int unpacksize;
		void *ret;
		U16 texidcount;
		U16 texnamecount;

		fseek(f, myModelOffs - headersize, SEEK_CUR);
		ptr = ret = malloc(myModelTotalLength);
		fread(ret, 1, myModelTotalLength, f);
		ptr += myModelDataLength;
		fread_s32(packsize);
		fread_s32(unpacksize);
		assert(packsize == myModelDataLength || ABS(unpacksize) == myModelDataLength);

		// Tex IDs
		fread_u16(texidcount);
		model->collision_data.tex_count = texidcount;
		SAFE_FREE(model->collision_data.tex_idx);
		model->collision_data.tex_idx = malloc(texidcount * sizeof(TexID));
		memcpy(model->collision_data.tex_idx, ptr, texidcount * sizeof(TexID));
		ptr += texidcount * sizeof(TexID);
		if (!isBigEndian())
		{
			TexID *texid = model->collision_data.tex_idx;
			for (i=0; i<(U32)texidcount; i++, texid++)
			{
				texid->count = endianSwapU16(texid->count);
				texid->id = endianSwapU16(texid->id);
			}
		}

		// Material names
		fread_u16(texnamecount);
		SAFE_FREE(*(char***)&model->collision_data.tex_names);
		model->collision_data.tex_names = malloc(texnamecount*sizeof(char*));
		for (i=0; i<texnamecount; i++)
		{
			U16 slen;
			char *sbuf;
			fread_u16(slen);
			sbuf = ScratchAlloc(slen+1);
			strncpy_s(sbuf, slen+1, ptr, slen); ptr += slen;
			sbuf[slen] = '\0';
			model->collision_data.tex_names[i] = allocAddString(sbuf);
			ScratchFree(sbuf);
		}
		// Validate texids
		for (i=0; i<(U32)model->collision_data.tex_count; i++) {
			if (model->collision_data.tex_idx[i].id >= texnamecount) {
				assert(0); // Corrupt model file?
				model->collision_data.tex_idx[i].id = 0;
			}
		}

		// Realloc off the extra memory
		ret = realloc(ret, packsize?packsize:ABS(unpacksize));
		outPackedData->packsize = packsize;
		outPackedData->unpacksize = unpacksize;
		outPackedData->data_ptr = ret;
	} else {
		// No collision model data stored, perhaps it will build it on demand
	}
#undef fread_u32
#undef fread_u16
}



#include "wlModel_h_ast.c"
