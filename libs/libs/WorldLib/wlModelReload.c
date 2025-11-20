/***************************************************************************



*/

#include "wlModelReload.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "timing.h"
#include "logging.h"
#include <fcntl.h>
#include <share.h>
#include "StringUtil.h"
#include "dynDraw.h"
#include "qsortG.h"
#include "StringCache.h"

#include "ObjectLibrary.h"
#include "WorldGridPrivate.h"
#include "WorldGridLoadPrivate.h"
#include "wlState.h"
#include "dynCloth.h"



AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Misc););

static bool modelReload_inited=false;
static bool modelReload_needsReload=false;
static int modelReload_numReloads=0;
static U32 modelReload_timer=0;
static int modelReload_retryCount=0;

#define MODELRELOAD_MAX_RETRIES 15

typedef enum ModelReloadType
{
	// geometry reloads
	MRT_MODELLIBRARY,
	MRT_OBJECTLIBRARY,
	MRT_PLAYERLIBRARY,
	MRT_PLAYERANIM,

	MRT_FILELAYER,
	
	MRT_COUNT,
} ModelReloadType;

typedef struct ModelReloadReq
{
	const char *filename;
} ModelReloadReq;

static ModelReloadReq **modelReload_requests[MRT_COUNT];

ModelReloadCallback wlModelReloadCallback;

void modelSetReloadCallback(ModelReloadCallback callback)
{
	wlModelReloadCallback = callback;
}

void modelsReloaded(void)
{
	if (wlModelReloadCallback)
		wlModelReloadCallback();
}

int getNumModelReloads(void)
{
	return modelReload_numReloads;
}

int getModelReloadRetryCount(void)
{
	return modelReload_retryCount;
}

static void addRequest(const char *file, ModelReloadType type)
{
	int i;
	ModelReloadReq *req = NULL;
	const char *file_alloced = allocAddFilename(file);

	for (i = 0; i < eaSize(&modelReload_requests[type]); i++)
	{
		ModelReloadReq *cur_req = modelReload_requests[type][i];
		if (cur_req->filename == file_alloced)
		{
			req = cur_req;
			break;
		}
	}

	if (!req)
	{
		req = calloc(1, sizeof(ModelReloadReq));
		req->filename = file_alloced;
		eaPush(&modelReload_requests[type], req);
	}

	modelReload_needsReload = 1;

	if (!modelReload_timer)
		modelReload_timer = timerAlloc();
	timerStart(modelReload_timer);
}

static void reloadPlayerLibraryGeo(const char *relpath)
{
	printf("  %s:\n", relpath);

	dynDrawSkeletonReloadAllUsingCostume(NULL, RESEVENT_RESOURCE_MODIFIED);
}

static void reloadPlayerLibraryGeoCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) return;
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	addRequest(relpath, MRT_PLAYERLIBRARY);
}
static void reloadPlayerLibraryAnimCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) return;
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	addRequest(relpath, MRT_PLAYERANIM);
}
static void reloadModelLibraryCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) return;
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	addRequest(relpath, MRT_MODELLIBRARY);
}
static void reloadObjectLibraryCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) return;
	errorLogFileIsBeingReloaded(relpath);
	addRequest(relpath, MRT_OBJECTLIBRARY);
}

static void reloadFileLayerCallback(const char *relpath, int when)
{
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	addRequest(relpath, MRT_FILELAYER);
}

void modelReloadInit(void)
{
	assert(!modelReload_inited);
	modelReload_inited = true;

	// Add callback for re-loading objects
 	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "object_library/*.geo2", reloadModelLibraryCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "object_library/*" MODELNAMES_EXTENSION, reloadObjectLibraryCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "object_library/*" ROOTMODS_EXTENSION, reloadObjectLibraryCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "object_library/*" OBJLIB_EXTENSION, reloadObjectLibraryCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "character_library/*.geo2", reloadPlayerLibraryGeoCallback);

	// Reload character library names too?  No, handled through ModelHeader reloads, I think
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "maps/*.layer", reloadFileLayerCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "maps/*.tlayer", reloadFileLayerCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "maps/*.clayer", reloadFileLayerCallback);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "character_library/*.anim",reloadPlayerLibraryAnimCallback);
}

static HANDLE getVrmlMutex;

void releaseGetVrmlLock(void)
{
#if !PLATFORM_CONSOLE
	assert(getVrmlMutex);
	ReleaseMutex(getVrmlMutex);
#endif
}

bool waitForGetVrmlLock(bool block_wait)
{
#if !PLATFORM_CONSOLE
	DWORD ret;
	if (!getVrmlMutex)
	{
		getVrmlMutex = CreateMutex(NULL, 0, L"Global\\CrypticGetVrmlLock");
		assert(getVrmlMutex);
	}

	WaitForSingleObjectWithReturn(getVrmlMutex, block_wait?INFINITE:1, ret);
	return block_wait || (ret != WAIT_TIMEOUT && ret != WAIT_FAILED);
#else
	return true;
#endif
}

static HANDLE getVrmlRenderMutex;

void releaseGetVrmlRenderLock(void)
{
#if !PLATFORM_CONSOLE
	assert(getVrmlRenderMutex);
	ReleaseMutex(getVrmlRenderMutex);
#endif
}

bool waitForGetVrmlRenderLock(bool block_wait)
{
#if !PLATFORM_CONSOLE
	DWORD ret;
	if (!getVrmlRenderMutex)
	{
		getVrmlRenderMutex = CreateMutex(NULL, 0, L"Global\\CrypticGetVrmlRenderLock");
		assert(getVrmlRenderMutex);
	}

	WaitForSingleObjectWithReturn(getVrmlRenderMutex, block_wait?INFINITE:1, ret);
	return block_wait || (ret != WAIT_TIMEOUT && ret != WAIT_FAILED);
#else
	return true;
#endif
}

void modelReloadCheck(void)
{
	int i, j;
	bool bReloaded = false;
	const char **objlib_files = NULL;
	const char **modellib_files = NULL;
	HANDLE resMutex;

	if (wlIsClient() && isDevelopmentMode())
	{
		PERFINFO_AUTO_START("waitForGetVrmlRenderLock",1);
		waitForGetVrmlRenderLock(true);
		releaseGetVrmlRenderLock();
		PERFINFO_AUTO_STOP();
	}

	if (!modelReload_needsReload || !modelReload_inited)
		return;

	// Wait for a little while after load requests stop coming
	if (timerElapsed(modelReload_timer) < 0.5f)
		return;

	if (!waitForGetVrmlLock(false))
		return;

	resMutex = resGetResourceMutex();

	loadstart_printf("Geometry reload initiated...\n");

	// Just finished waiting for the GetVRML lock, a few more updates may have
	// queued up, need to do those now (specifically at least .ModelHeader reloads!)
	FolderCacheDoCallbacks();

	geoForceBackgroundLoaderToFinish();

	// Dispatch requests
	for (i = 0; i < MRT_COUNT; ++i)
	{
		for (j = eaSize(&modelReload_requests[i])-1; j >= 0; --j)
		{
			ModelReloadReq *filereq = modelReload_requests[i][j];
			switch (i)
			{
			xcase MRT_MODELLIBRARY:
				// Just do general callbacks, actual data reloaded through ModelHeader reload
				eaPush(&modellib_files, filereq->filename);
			xcase MRT_OBJECTLIBRARY:
				log_printf(LOG_GETVRML,"Reloading Object Library...");
				reloadObjectLibraryFile(filereq->filename);
				eaPush(&objlib_files, filereq->filename);
			xcase MRT_PLAYERLIBRARY:
				log_printf(LOG_GETVRML,"Reloading Player Library...");
				reloadPlayerLibraryGeo(filereq->filename);
			xcase MRT_PLAYERANIM:
				// Just do general callbacks, actual data reloaded through ModelHeader reload
			xcase MRT_FILELAYER:
				log_printf(LOG_GETVRML,"Reloading layer file...");
				reloadFileLayer(filereq->filename);
			}
			wlStatusPrintf("File reloaded: %s", filereq->filename);
			bReloaded = true;

			eaRemove(&modelReload_requests[i], j);
			SAFE_FREE(filereq);
		}
	}

	resReleaseResourceMutex(resMutex);

	releaseGetVrmlLock();
//#ifdef SERVER
//	geoFreeAllGeoData();
//#endif
//

	modelsReloaded();
	//fxGeoResetAllCapes();

	if (bReloaded)
	{
		for (i = 0; i < eaSize((cEArrayHandle*)&wl_state.reload_map_callbacks); ++i)
			wl_state.reload_map_callbacks[i](worldGetPrimaryMap());

		if(wl_state.reload_map_game_callback)
			wl_state.reload_map_game_callback(worldGetPrimaryMap());

		dynClothObjectResetAll();
	}
	
	timerFree(modelReload_timer);
	modelReload_timer = 0;
	modelReload_needsReload = 0;
	modelReload_numReloads++;

/*
	if (!objectLibraryConsistencyCheck(modelReload_retryCount > MODELRELOAD_MAX_RETRIES))
	{
		printf("Failed consistency check! Retrying...\n");
		for (i = 0; i < eaSize(&modellib_files); i++)
			addRequest(modellib_files[i], MRT_MODELLIBRARY);
		for (i = 0; i < eaSize(&objlib_files); i++)
			addRequest(objlib_files[i], MRT_OBJECTLIBRARY);
		modelReload_retryCount++;
	}
	else
*/
	{
		modelReload_retryCount = 0;
	}
	eaDestroy(&objlib_files);
	eaDestroy(&modellib_files);

	loadend_printf("Done.");
}
