/***************************************************************************



***************************************************************************/

#include "objContainerIO.h"
#include "objIndex.h"

#include "logging.h"

#include "Alerts.h"
#include "hoglib.h"
#include "hogutil.h"
#include "piglib_internal.h"
#include "zutils.h"
#include "EString.h"
#include "strings_opt.h"
#include "StringUtil.h"
#include "fileutil.h"
#include "tokenstore.h"
#include "tokenstore_inline.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "ScratchStack.h"
#include "StashTable.h"
#include "FloatAverager.h"
#include "TimedCallback.h"

#include "XboxThreads.h"
#include "WorkerThread.h"
#include "MultiWorkerThread.h"
#include "FreeThread.h"
#include "cpu_count.h"
#include "winInclude.h"
#include "sysutil.h"
#include "Semaphore.h"
#include "cmdparse.h"
#include "MemTrackLog.h"
#include "ThreadSafeMemoryPool.h"
#include "ScratchStack.h"
#include "MemAlloc.h"
#include "file.h"

#include "autogen/objContainer_h_ast.h"
#include "autogen/objcontainerio_h_ast.h"
#include "autogen/objcontainerio_c_ast.h"
#include "autogen/objpath_h_ast.h"
#include "GlobalTypes_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Container IO

//Complete, loadable db snapshots
#define SNAPSHOT_BACKUP_SUFFIX "_inc_snapshot.backup"
#define SNAPSHOT_HOG_SUFFIX "_inc_snapshot"
#define SNAPSHOT_HOG_PREFIX "_0_"

#define NEWINC_HOG_SUFFIX "_inc_current"
#define NEWINC_HOG_PREFIX "_3_"

//A closed incremental hog
#define INCREMENTAL_HOG_SUFFIX "_inc_pending"
#define INCREMENTAL_HOG_PREFIX "_1_"

//The merged suffix is the same length as the pending suffix, for coding convenience.
#define MERGED_HOG_SUFFIX "_inc_#merged"
#define MERGED_HOG_PREFIX "_2_"

//for files that do not contribute to the merge.
#define OLDINC_HOG_SUFFIX "_inc_skipped"
#define OLDINC_HOG_PREFIX MERGED_HOG_PREFIX

#define BADSTOP_HOG_SUFFIX "_inc_badstop"
#define BADSTOP_HOG_PREFIX MERGED_HOG_PREFIX

#define OFFLINE_HOG_SUFFIX "_offline"
#define OFFLINE_HOG_PREFIX "_4_"

//#define DB_UPDATE_THREAD_QUEUE_SIZE 131072 // 128k
#define DB_UPDATE_THREAD_QUEUE_SIZE 1048576 // 1MB

ContainerIOConfig gContainerIOConfig;
ContainerSource gContainerSource;

char *malloc_FileData(size_t size)
{
	return malloc_special_heap(size, CRYPTIC_CONTAINER_HEAP);
}

// Print a DB load time report.
static bool gbDbLoadTimeReport = 0;
AUTO_CMD_INT(gbDbLoadTimeReport, DbLoadTimeReport) ACMD_COMMANDLINE;

// Compression level for piglib.
// TODO: Make this a parameter to the ContainerStore external interface in objContainerIO.h.
static unsigned suContainerCompressionLevel = 5;
AUTO_CMD_INT(suContainerCompressionLevel, ContainerCompressionLevel) ACMD_COMMANDLINE;

// Compression level for repacking/unloading.
// TODO: Make this a parameter to the ContainerStore external interface in objContainerIO.h.
static unsigned suContainerRepackingCompressionLevel = 3;
AUTO_CMD_INT(suContainerRepackingCompressionLevel, ContainerRepackingCompressionLevel) ACMD_COMMANDLINE;

typedef struct ContainerUpdate {
	Container *container; // May be a copy
	char *estring;
	char fileName[MAX_PATH];
	bool bIsDelete;
	U32 iDeletedTime;
	HogFile *hog;
	F32 serviceTime;
	bool useEntry;
	NewPigEntry entry;

	// Used for multithreaded saving	
	GlobalType containerType;
	ContainerID containerID;
	
	Container *sourceContainer;
	U32 inUse;
} ContainerUpdate;

TSMP_DEFINE(ContainerUpdate);

ContainerUpdate *AllocContainerUpdate(void)
{
	ATOMIC_INIT_BEGIN;
	TSMP_CREATE(ContainerUpdate, 1024);
	ATOMIC_INIT_END;

	return TSMP_CALLOC(ContainerUpdate);
}

void DestroyContainerUpdate(ContainerUpdate *conup)
{
	TSMP_FREE(ContainerUpdate, conup);
}

__inline F32 objServiceTime(void)
{
	//InterlockedIncrement(&pendingWriteCount);
	return timerGetSecondsAsFloat();
}

ContainerUpdate *PopulateContainerUpdate(Container *object)
{
	ContainerUpdate *conup = AllocContainerUpdate();
	conup->containerType = object->containerType;
	conup->containerID = object->containerID;
	conup->sourceContainer = object;
			
	assert(!object->savedUpdate); // If there's a saved update two are running at once
	conup->container = object;
	conup->serviceTime = objServiceTime();
	conup->hog = NULL;
	object->savedUpdate = conup;
	return conup;
}

// This limits the number of outstanding container saves that might be "in-flight" at any particular time.
static CrypticSemaphore sContainerSavesOutstanding = NULL;

// Default for sMaxContainerSavesOutstanding
#define OBJECTDB_DEFAULT_MAX_CONTAINER_SAVES_OUTSTANDING 100

// If true, disable code to throttle saves based on sContainerSavesOutstanding.
static bool sbDisableContainerSaveSemaphore = true;
AUTO_CMD_INT(sbDisableContainerSaveSemaphore, DisableContainerSaveSemaphore) ACMD_CMDLINE;

// Maximum number of simultaneous container save requests to process.
static long sMaxContainerSavesOutstanding = OBJECTDB_DEFAULT_MAX_CONTAINER_SAVES_OUTSTANDING;
AUTO_CMD_INT(sMaxContainerSavesOutstanding, MaxContainerSavesOutstanding) ACMD_AUTO_SETTING(Misc, OBJECTDB, CLONEOBJECTDB, CLONEOFCLONE) ACMD_CALLBACK(MaxContainerSavesOutstandingChanged);

// Adjust the number of maximum container saves outstanding.
void MaxContainerSavesOutstandingChanged(CMDARGS)
{
	static long last_value = OBJECTDB_DEFAULT_MAX_CONTAINER_SAVES_OUTSTANDING;

	if (sbDisableContainerSaveSemaphore)
		return;

	// Maximum number of outstanding saves must be positive.
	if (sMaxContainerSavesOutstanding <= 0)
	{
		Errorf("Value must be positive");
		sMaxContainerSavesOutstanding = last_value;
		return;
	}

	// Initialize semaphore.
	if (!sContainerSavesOutstanding)
		semaphoreInit(&sContainerSavesOutstanding, sMaxContainerSavesOutstanding, 0);

	// Set max, if different from the above.
	semaphoreSetMax(sContainerSavesOutstanding, sMaxContainerSavesOutstanding);
	last_value = sMaxContainerSavesOutstanding;
}

void FreeConupEstr(char **str)
{
	if(!str)
		return;

	estrDestroy(str);
}

static void FreeWithSemaphoreSignal(char **data)
{
	if(!data)
		return;

	free(*data);
	*data = NULL;
	if (!sbDisableContainerSaveSemaphore)
		semaphoreSignal(sContainerSavesOutstanding);
}

static void FreeWithSemaphoreSignalCB(char *data)
{
	FreeWithSemaphoreSignal(&data);
}

static void FreeConupEstrSemaphoreSignal(char **str)
{
	FreeConupEstr(str);
	if (!sbDisableContainerSaveSemaphore)
		semaphoreSignal(sContainerSavesOutstanding);
}

static void FreeConupEstrCB(char *str)
{
	FreeConupEstr(&str);
}

// Like FreeConupEstrCB, but signal the container save semaphore also.
static void FreeConupEstrSemaphoreSignalCB(char *str)
{
	FreeConupEstrSemaphoreSignal(&str);
}

static char *AllocConupEstr()
{
	char *str = NULL;
	estrStackCreateSize(&str, 10 * 1024 * 1024);
	return str;
}

// Like AllocConupEstr(), but for container save request.
static char *AllocConupEstrAndSemphoreWait()
{
	if (!sbDisableContainerSaveSemaphore)
		semaphoreWait(sContainerSavesOutstanding);
	return AllocConupEstr();
}

typedef struct ContainerSourceUpdate {
	HogFile *hog;
	GlobalType type;
} ContainerSourceUpdate;

AUTO_STRUCT;
typedef struct PruneFile {
	char *filename;
	__time32_t time;
} PruneFile;

AUTO_STRUCT;
typedef struct PruneList {
	EARRAY_OF(PruneFile) incrementals;
	EARRAY_OF(PruneFile) snapshots;
	EARRAY_OF(PruneFile) logs;
	EARRAY_OF(PruneFile) offlines;
} PruneList;

//sort by time descending
static int CompareFiletime(const PruneFile **a, const PruneFile **b)
{
	return (b[0]->time - a[0]->time);
}

AUTO_STRUCT;
typedef struct LineCount
{
	int index;
	int count;
	int time;
} LineCount;

static int CompareLineCount(const LineCount **a, const LineCount **b)
{
	return ((*b)->count - (*a)->count);
}

static int CompareTimestamp(const LineCount **a, const LineCount **b)
{
	return ((*a)->time - (*b)->time);
}

static int s_PruneDeletedContainers = 1;
AUTO_CMD_INT(s_PruneDeletedContainers,PruneDeleted) ACMD_COMMANDLINE;

AUTO_RUN_EARLY;
void InitCLParams(void)
{
	StructInitFields(parse_ContainerSource, &gContainerSource);

	gContainerSource.hogUseDiskOrder = 1;

	//snapshot keep times for old dir
	gContainerSource.snapshotMinutesToKeep = 120;
	gContainerSource.snapshotHoursToKeep = 24;
	gContainerSource.snapshotDaysToKeep = 30;
	gContainerSource.offlineDaysToKeep = 30;

	//incremental hog and log keep times for old dir
	gContainerSource.pendingHoursToKeep = 24;
	gContainerSource.journalHoursToKeep = 24*30;
}

//AUTO_CMD_INT(gContainerSource.hogMerge,HogMerge) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.strictMerge,StrictMerge) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.generateLogReport,GenerateLogReport) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.skipperiodiccontainersaves,SkipPeriodicContainerSaves) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.savecontainerflushperiod,SaveContainerFlushPeriod) ACMD_COMMANDLINE;


AUTO_CMD_INT(gContainerSource.hogUseDiskOrder, UseDiskOrder) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.snapshotMinutesToKeep, OldSnapshotMinutes) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.snapshotHoursToKeep, OldSnapshotHours) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.snapshotDaysToKeep, OldSnapshotDays) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.offlineDaysToKeep, OldOfflineDays) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.pendingHoursToKeep, OldPendingHours) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.journalHoursToKeep, OldJournalHours) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.unloadOnHeaderCreate, UnloadOnHeaderCreate) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.purgeHogOnFailedContainer, PurgeHogOnFailedContainer) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.containerPreloadThresholdHours, ContainerPreloadThresholdHours) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.containerPlayedUnloadThresholdHours, ContainerPlayedUnloadThresholdHours) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.containerAccessUnloadThresholdHours, ContainerAccessUnloadThresholdHours) ACMD_COMMANDLINE;
AUTO_CMD_INT(gContainerSource.enableStaleContainerCleanup, EnableStaleContainerCleanup) ACMD_COMMANDLINE;

int diskLoadingMode;
AUTO_CMD_INT(diskLoadingMode, DiskLoadingMode) ACMD_COMMANDLINE;

enum
{
	HOGUPDATETHREAD_CMDSAVECONTAINERINFO = WT_CMD_USER_START,
	HOGUPDATETHREAD_CMDSAVECONTAINERSCHEMA,
	HOGUPDATETHREAD_CMDSAVECONTAINER,
	//HOGUPDATETHREAD_CMDDELETECONTAINER, // body of actual function is commented out...
	HOGUPDATETHREAD_CMDFLUSHHOGCONTAINERS,
	HOGUPDATETHREAD_CMDSETSEQUENCENUMBER,
	HOGUPDATETHREAD_CMDSETTIMESTAMP,
	HOGUPDATETHREAD_CMDSETMAXID,
	HOGUPDATETHREAD_CMDCLOSEHOG,
	HOGUPDATETHREAD_CMDCREATEANDSWAP,

	HOGUPDATETHREAD_MSGDESTROYCONTAINER,
	HOGUPDATETHREAD_MSGPOPQUEUEDROTATE
};

void objSetSkipPeriodicContainerSaves(bool enabled)
{
	gContainerSource.skipperiodiccontainersaves = enabled;
}

//private functions
static HogFile* objOpenIncrementalHogFile(char *hogfile);
static void objSaveContainerSourceInfo(HogFile *the_hog);
static bool objSaveSchemaSource(GlobalType type, HogFile *the_hog);
static void objSaveModifiedContainersThreaded(bool bRotateAfterSaving);
static void objFlushModifiedContainersThreaded(void);
static void ContainerHogRemoveDeletedMark(HogFile *snapshotHog, GlobalType conType, ContainerID conID);
static bool objParseContainerFilename(const char *filename, GlobalType *type, ContainerID *id, U64 *seq, bool* deleted);

//Commands to be executed on the hog thread
void wt_objSaveContainerSourceInfo(void *user_data, void *data, WTCmdPacket *packet);
void wt_objSaveContainerSchema(void *user_data, void *data, WTCmdPacket *packet);
void wt_objSaveContainer(void *user_data, void *data, WTCmdPacket *packet);
void wt_objPermanentlyDeleteContainer(void *user_data, void *data, WTCmdPacket *packet);
void wt_objFlushHogContainers(void *user_data, void *data, WTCmdPacket *packet);
void wt_objSetSequence(void *user_data, void *data, WTCmdPacket *packet);
void wt_objSetTimeStamp(void *user_data, void *data, WTCmdPacket *packet);
void wt_objSetMaxID(void *user_data, void *data, WTCmdPacket *packet);
void wt_objCloseHogFile(void *user_data, void *data, WTCmdPacket *packet);

//Commands to be run on the hog rotate thread
void wt_objCreateSwapSyncAndCloseIncrementalHogFile(void *user_data, void *data, WTCmdPacket *packet);


//Callbacks to be run on the main thread
void mt_objDestroyContainerDispatch(void *user_data, void *data, WTCmdPacket *packet);
void mt_PopQueuedRotate(void *user_data, void *data, WTCmdPacket *packet);

void RenameCurrentHoggToPending(char *old_path);

#ifdef TRACK_MEAN_SERVICE_TIME

static StashTable diskTimes;
FloatAverager *gMeanServiceTimeAve;

#endif

volatile int pendingWriteCount;

// Multi threaded saving
static int s_pendingSaveCount;
static MultiWorkerThreadManager * s_saveThreadManager = NULL;
static bool s_bRotateWhenDoneSaving = false;
U64 s_pendingSequenceNumber;

//If there are more than this number of pending writes, don't start another atomic save.
#define containerWriteThrottle 256

void objSetRepositoryMaxIDs()
{
	EARRAY_FOREACH_BEGIN(gContainerSource.sourceInfo->ppRepositoryTypeInfo, i);
	{
		ContainerRepositoryTypeInfo *pTypeInfo = gContainerSource.sourceInfo->ppRepositoryTypeInfo[i];
		ContainerStore *store = objFindContainerStoreFromType(pTypeInfo->eGlobalType);
		if(store)
		{
			if(pTypeInfo->iMaxContainerID > store->maxID && (!gMaxContainerID || pTypeInfo->iMaxContainerID < gMaxContainerID))
				store->maxID = pTypeInfo->iMaxContainerID;
		}
	}
	EARRAY_FOREACH_END;
}

void objSaveContainerStoreMaxIDs(void)
{
	int i;
	// Do this here, because we only need these values when writing to the disk.
	for(i = 0; i < GLOBALTYPE_MAX; ++i)
	{
		if(gContainerRepository.containerStores[i].containerSchema)
		{
			objContainerSetMaxID(i, gContainerRepository.containerStores[i].maxID);
		}
	}
}

AUTO_RUN_EARLY;
void InitMST(void)
{
	pendingWriteCount = 0;
	if (gAppGlobalType != GLOBALTYPE_CLIENT)
	{
#ifdef TRACK_MEAN_SERVICE_TIME
		diskTimes = stashTableCreateAddress(1 << 20);	//1024k
		gMeanServiceTimeAve = FloatAverager_Create(AVERAGE_MINUTE);
#endif
		//faster/bigger
		if (IsThisObjectDB())
			PigSetCompression(suContainerCompressionLevel);
	}

	gContainerSource.savecontainerflushperiod = 60;	//seconds
}


//HogLib callbacks
void EStringFreeCallback(char *string)
{
	estrDestroy(&string);
}

void NoFreeCallback(char *string)
{
}

void DataTimedFreeCallback(void *data)
{
	if (gAppGlobalType != GLOBALTYPE_CLIENT)
	{
#ifdef TRACK_MEAN_SERVICE_TIME
		F32 time;
		if (stashAddressRemoveFloat(diskTimes, data, &time))
		{
			F32 currentTime = timerGetSecondsAsFloat();
			FloatAverager_AddDatapoint(gMeanServiceTimeAve, (currentTime - time));
			InterlockedDecrement(&pendingWriteCount);
		}
#endif
	}
	SAFE_FREE(data);
}

void objDecrementServiceTime(void)
{
	//InterlockedDecrement(&pendingWriteCount);
}

#define objFileModifyUpdateNamed(handle, relpath, data, size, timestamp, serviceTime) objFileModifyUpdateNamedEx(handle, relpath, data, size, timestamp, serviceTime, NULL)

int objFileModifyUpdateNamedEx(HogFile *handle, const char *relpath, char *data, U32 size, U32 timestamp, F32 serviceTime, DataFreeCallback free_callback)
{
	int result;
#ifdef TRACK_MEAN_SERVICE_TIME
	NewPigEntry entry = {0};
	entry.data = (U8*)data;
	entry.size = size;
	entry.fname = relpath;
	entry.timestamp = timestamp;
	entry.must_pack = 1;

	entry.free_callback = EStringFreeCallback;
	if(free_callback)
		entry.free_callback = free_callback;


	entry.header_data = pigGetHeaderData(&entry, &entry.header_data_size);
	pigChecksumAndPackEntry(&entry);
	if (entry.free_callback)
	{
		U8 *uncompressed_data = strdup(entry.data);
		entry.free_callback(entry.data);
		entry.data = uncompressed_data;
	}

	entry.free_callback = DataTimedFreeCallback;

	stashAddressAddFloat(diskTimes, entry.data, serviceTime, 0);


	result = hogFileModifyUpdateNamedAsync2(handle, &entry);
#else
	result = hogFileModifyUpdateNamed(handle, relpath, data, size, timestamp, free_callback ? free_callback : EStringFreeCallback);
	objDecrementServiceTime();
#endif


	//DataTimedFreeCallback(entry.data);
	return result;
}

int objFileModifyUpdateNamedZippedEx(HogFile* handle, ContainerUpdate *conup, U32 timestamp, F32 serviceTime, DataFreeCallback free_callback)
{
	//conup->entry.fname might point to a version from another thread. Reset it.
	conup->entry.fname = conup->fileName;
	return hogFileModifyUpdateNamedAsync2(handle, &conup->entry);
}

void objZipContainerData(ContainerUpdate *conup, U32 timestamp, DataFreeCallback free_callback)
{
	PERFINFO_AUTO_START_FUNC();
	conup->entry.data = (U8*)conup->estring;
	conup->entry.size = estrLength(&conup->estring)+1;
	conup->entry.fname = conup->fileName;
	conup->entry.timestamp = timestamp;

	conup->entry.free_callback = EStringFreeCallback;
	if(free_callback)
		conup->entry.free_callback = free_callback;

	pigChecksumAndPackEntryEx(&conup->entry, CRYPTIC_CHURN_HEAP);
	if(conup->entry.free_callback)
	{
		//It didn't free it, so that means it is keeping it.
		//Dup the data
		U8* uncompressedData = strdup(conup->entry.data);
		conup->entry.free_callback(conup->entry.data);
		conup->entry.data = uncompressedData;
		conup->entry.free_callback = NULL;
	}

	conup->entry.free_callback = FreeWithSemaphoreSignalCB;

	//The entry now owns it.
	conup->estring = NULL;
	conup->useEntry = true;
	PERFINFO_AUTO_STOP();
}

F32 MeanServiceTime(void)
{
#ifdef TRACK_MEAN_SERVICE_TIME
	if (gAppGlobalType != GLOBALTYPE_CLIENT)
	{
		return FloatAverager_Query(gMeanServiceTimeAve, AVERAGE_MINUTE);
	}
	else
#endif
	{
		return 0.0f;
	}
}

U32 PendingWrites(void)
{
	return pendingWriteCount;	
}

ContainerIOConfig *objGetContainerIOConfig(ContainerIOConfig *copyfromandfree)
{
	static bool inited = false;
	if (!inited)
	{
		StructInit(parse_ContainerIOConfig, &gContainerIOConfig);
		if (copyfromandfree)
		{
			StructCopyAll(parse_ContainerIOConfig, copyfromandfree, &gContainerIOConfig);
			StructDestroy(parse_ContainerIOConfig, copyfromandfree);
		}
		inited = true;
	}
	return &gContainerIOConfig;
}

static void objInitializeHogThread(void)
{
	U32 queueSize = isProductionMode() ? DB_UPDATE_THREAD_QUEUE_SIZE : SQR(1024) / 16;

	// Initialize the semaphore limiting the number of outstanding saves.
	// The container saving code will queue up all saves into a MultiWorkerThread buffer, but that buffer will be processed such that
	// sMaxContainerSavesOutstanding is not exceeded.
	if (!sbDisableContainerSaveSemaphore)
		semaphoreInit(&sContainerSavesOutstanding, sMaxContainerSavesOutstanding, 0);
	
	//Initialize the incremental hog's critical section
	InitializeCriticalSection(&gContainerSource.hog_access);

	gContainerSource.hog_update_thread = wtCreate(queueSize, queueSize, NULL, "hogIOThread");
	wtRegisterCmdDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINERINFO, wt_objSaveContainerSourceInfo);
	wtRegisterCmdDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINERSCHEMA, wt_objSaveContainerSchema);
	wtRegisterCmdDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINER, wt_objSaveContainer);
	wtRegisterCmdDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDFLUSHHOGCONTAINERS, wt_objFlushHogContainers);
	wtRegisterCmdDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSETSEQUENCENUMBER, wt_objSetSequence);
	wtRegisterCmdDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSETTIMESTAMP, wt_objSetTimeStamp);
	wtRegisterCmdDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSETMAXID, wt_objSetMaxID);
	//wtRegisterCmdDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDDELETECONTAINER, wt_objPermanentlyDeleteContainer);
	wtRegisterCmdDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDCLOSEHOG, wt_objCloseHogFile);
	wtRegisterCmdDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDCREATEANDSWAP, wt_objCreateSwapSyncAndCloseIncrementalHogFile);

	wtRegisterMsgDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_MSGDESTROYCONTAINER, mt_objDestroyContainerDispatch);
	wtRegisterMsgDispatch(gContainerSource.hog_update_thread, HOGUPDATETHREAD_MSGPOPQUEUEDROTATE, mt_PopQueuedRotate);

	wtSetProcessor(gContainerSource.hog_update_thread, THREADINDEX_MISC);
	wtSetThreaded(gContainerSource.hog_update_thread, true, 0, false);
	wtStart(gContainerSource.hog_update_thread);

	gContainerSource.thread_initialized = true;
	
}

void wt_objCloseHogFile(void *user_data, void *data, WTCmdPacket *packet)
{
	HogFile *the_hog;
	if (data)
	{
		the_hog = ((HogFile**)data)[0];
	
		PERFINFO_AUTO_START_FUNC();
		EnterCriticalSection(&gContainerSource.hog_access);
		do {
			fprintf(fileGetStderr(),"Flushing/Closing hog file: %s\n", hogFileGetArchiveFileName(the_hog));
			hogFileDestroy(the_hog, false /*JE: Unsure if this is leaking, leaving as it was before to be safe (might leak)*/);
		} while (false);
		LeaveCriticalSection(&gContainerSource.hog_access);
		PERFINFO_AUTO_STOP();
	}
}

void wt_objSaveContainerSourceInfo(void *user_data, void *data, WTCmdPacket *packet)
{
	char *dataString = NULL;
	PERFINFO_AUTO_START("objSaveContainerSourceInfo",1);

	EnterCriticalSection(&gContainerSource.hog_access);
	do {
		HogFile *the_hog = NULL;
		F32 serviceTime = objServiceTime();
			
		if ((the_hog = ((HogFile**)data)[0]) != NULL) {}
		else if ((the_hog = gContainerSource.incrementalHog) != NULL) {}	//the_hog is set
		else if ((the_hog = gContainerSource.fullHog) != NULL) {}		//the_hog is set
		else 
		{
			objDecrementServiceTime();
			break;
		}
		if (!ParserWriteText(&dataString,parse_ContainerRepositoryInfo, gContainerSource.sourceInfo, 0, 0, 0))
		{
			estrDestroy(&dataString);
			objDecrementServiceTime();
			break;
		}

		objFileModifyUpdateNamed(the_hog, CONTAINER_INFO_FILE, dataString,estrLength(&dataString)+1,objContainerGetSystemTimestamp(),serviceTime);				
	} while (false);
	LeaveCriticalSection(&gContainerSource.hog_access);

	PERFINFO_AUTO_STOP();
}

void wt_objSaveContainerSchema(void *user_data, void *data, WTCmdPacket *packet)
{
	ContainerSourceUpdate *conup = (ContainerSourceUpdate*)data;
	HogFile *the_hog = NULL;
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&gContainerSource.hog_access);
	objSaveSchemaSource(conup->type, conup->hog);
	LeaveCriticalSection(&gContainerSource.hog_access);
	PERFINFO_AUTO_STOP();
}

#define HEADER_WRITE_BYTES(val) \
	estrConcat(estr, (const U8*)&val, sizeof(val)); \
	size += sizeof(val);
#define HEADER_WRITE_STRING(str) \
	if(header->str && strlen(header->str)) \
	{						\
		estrConcat(estr, header->str, fileHeader.str##Len+1); \
		size += fileHeader.str##Len+1; \
	}		

#define FILEHEADER_STRING_SIZE(strLen) ((strLen) ? (strLen) + 1 : 0)

static int objContainerWriteHeader(ContainerStore *store, ObjectIndexHeader *header, char **estr, U32 deletedTime)
{
	if(header || deletedTime)
	{
		U32 size = 0;
		ContainerFileHeader containerHeader = {0};

		containerHeader.headerVersion = CONTAINER_FILE_HEADER_CURRENT_VERSION;
		containerHeader.deletedTime = deletedTime;

		if(header)
		{
			ObjectIndexFileHeader fileHeader = {0};
			containerHeader.objextIndexHeaderType = store->objectIndexHeaderType;
			fileHeader.crc = headerCRC;

			//OBJ_HEADER_CONTAINERID
			fileHeader.accountId = header->accountId;
			//OBJ_HEADER_ACCOUNTID
			fileHeader.containerId = header->containerId;
			//OBJ_HEADER_CREATEDTIME
			fileHeader.createdTime = header->createdTime;
			//OBJ_HEADER_LEVEL
			fileHeader.level = header->level;
			//OBJ_HEADER_FIXUP_VERSION
			fileHeader.fixupVersion = header->fixupVersion;
			//OBJ_HEADER_LAST_MODIFIED_TIME
			fileHeader.lastPlayedTime = header->lastPlayedTime;

			//OBJ_HEADER_PUB_ACCOUNTNAME
			fileHeader.pubAccountNameLen = header->pubAccountName ?
				(U32)strlen(header->pubAccountName) : 0;
			//OBJ_HEADER_PRIV_ACCOUNTNAME
			fileHeader.privAccountNameLen = header->privAccountName ?
				(U32)strlen(header->privAccountName) : 0;
			//OBJ_HEADER_SAVEDNAME
			fileHeader.savedNameLen = header->savedName ?
				(U32)strlen(header->savedName) : 0;

			fileHeader.extraDataCrc = extraDataCRC;

			//OBJ_HEADER_EXTRA_DATA_1
			fileHeader.extraData1Len = header->extraData1 ?
				(U32)strlen(header->extraData1) : 0;
			//OBJ_HEADER_EXTRA_DATA_2
			fileHeader.extraData2Len = header->extraData2 ?
				(U32)strlen(header->extraData2) : 0;
			//OBJ_HEADER_EXTRA_DATA_3
			fileHeader.extraData3Len = header->extraData3 ?
				(U32)strlen(header->extraData3) : 0;
			//OBJ_HEADER_EXTRA_DATA_4
			fileHeader.extraData4Len = header->extraData4 ?
				(U32)strlen(header->extraData4) : 0;
			//OBJ_HEADER_EXTRA_DATA_5
			fileHeader.extraData5Len = header->extraData5 ?
				(U32)strlen(header->extraData5) : 0;

			//OBJ_HEADER_VIRTUAL_SHARD_ID
			fileHeader.virtualShardId = header->virtualShardId;

			containerHeader.headerSize = sizeof(containerHeader) + sizeof(fileHeader) + FILEHEADER_STRING_SIZE(fileHeader.privAccountNameLen) +
				FILEHEADER_STRING_SIZE(fileHeader.pubAccountNameLen) + FILEHEADER_STRING_SIZE(fileHeader.savedNameLen) + 
				FILEHEADER_STRING_SIZE(fileHeader.extraData1Len) + FILEHEADER_STRING_SIZE(fileHeader.extraData2Len) + 
				FILEHEADER_STRING_SIZE(fileHeader.extraData3Len) + FILEHEADER_STRING_SIZE(fileHeader.extraData4Len) +
				FILEHEADER_STRING_SIZE(fileHeader.extraData5Len);

			HEADER_WRITE_BYTES(containerHeader);
			HEADER_WRITE_BYTES(fileHeader);

			//OBJ_HEADER_PUB_ACCOUNTNAME
			HEADER_WRITE_STRING(pubAccountName);
			//OBJ_HEADER_PRIV_ACCOUNTNAME
			HEADER_WRITE_STRING(privAccountName);
			//OBJ_HEADER_SAVEDNAME
			HEADER_WRITE_STRING(savedName);
			//OBJ_HEADER_EXTRA_DATA_1
			HEADER_WRITE_STRING(extraData1);
			//OBJ_HEADER_EXTRA_DATA_2
			HEADER_WRITE_STRING(extraData2);
			//OBJ_HEADER_EXTRA_DATA_3
			HEADER_WRITE_STRING(extraData3);
			//OBJ_HEADER_EXTRA_DATA_4
			HEADER_WRITE_STRING(extraData4);
			//OBJ_HEADER_EXTRA_DATA_5
			HEADER_WRITE_STRING(extraData5);
		}
		else
		{
			containerHeader.headerSize = sizeof(containerHeader);
			HEADER_WRITE_BYTES(containerHeader);
		}

		assert(containerHeader.headerSize == size);
		return true;
	}
	else
	{
		return false;
	}
}

#define HEADER_WRITE_STRING_OLD(str) \
	estrConcat(estr, header->str, fileHeader.str##Len+1); \
	size += fileHeader.str##Len+1; 

static int objContainerWriteHeader_VersionOne_DoNotUse(ObjectIndexHeader *header, char **estr)
{
	if(header)
	{
		U32 size = 0;
		OriginalObjectIndexFileHeader fileHeader = {0};
		fileHeader.crc = CONTAINER_FILE_HEADER_ORIGINAL_VERSION;

		//OBJ_HEADER_CONTAINERID
		fileHeader.accountId = header->accountId;
		//OBJ_HEADER_ACCOUNTID
		fileHeader.containerId = header->containerId;
		//OBJ_HEADER_CREATEDTIME
		fileHeader.createdTime = header->createdTime;
		//OBJ_HEADER_LEVEL
		fileHeader.level = header->level;
		//OBJ_HEADER_FIXUP_VERSION
		fileHeader.fixupVersion = header->fixupVersion;
		//OBJ_HEADER_LAST_MODIFIED_TIME
		fileHeader.lastPlayedTime = header->lastPlayedTime;

		//OBJ_HEADER_PUB_ACCOUNTNAME
		fileHeader.pubAccountNameLen = header->pubAccountName ?
			(U32)strlen(header->pubAccountName) : 0;
		//OBJ_HEADER_PRIV_ACCOUNTNAME
		fileHeader.privAccountNameLen = header->privAccountName ?
			(U32)strlen(header->privAccountName) : 0;
		//OBJ_HEADER_SAVEDNAME
		fileHeader.savedNameLen = header->savedName ?
			(U32)strlen(header->savedName) : 0;

		fileHeader.headerSize = sizeof(fileHeader) + fileHeader.privAccountNameLen+1 +
			fileHeader.pubAccountNameLen+1 + fileHeader.savedNameLen+1;
		HEADER_WRITE_BYTES(fileHeader);

		//OBJ_HEADER_PUB_ACCOUNTNAME
		HEADER_WRITE_STRING_OLD(pubAccountName);
		//OBJ_HEADER_PRIV_ACCOUNTNAME
		HEADER_WRITE_STRING_OLD(privAccountName);
		//OBJ_HEADER_SAVEDNAME
		HEADER_WRITE_STRING_OLD(savedName);

		assert(fileHeader.headerSize == size);
		return true;
	}
	else
	{
		return false;
	}
}

static int objContainerWriteHeader_VersionTwo_DoNotUse(ContainerStore *store, ObjectIndexHeader *header, char **estr, U32 deletedTime)
{
	if(header || deletedTime)
	{
		U32 size = 0;
		ContainerFileHeader containerHeader = {0};

		containerHeader.headerVersion = CONTAINER_FILE_HEADER_CURRENT_VERSION;
		containerHeader.deletedTime = deletedTime;

		if(header)
		{
			ObjectIndexFileHeaderTwo fileHeader = {0};
			containerHeader.objextIndexHeaderType = store->objectIndexHeaderType;
			fileHeader.crc = OBJECT_FILE_HEADER_CRC_TWO;

			//OBJ_HEADER_CONTAINERID
			fileHeader.accountId = header->accountId;
			//OBJ_HEADER_ACCOUNTID
			fileHeader.containerId = header->containerId;
			//OBJ_HEADER_CREATEDTIME
			fileHeader.createdTime = header->createdTime;
			//OBJ_HEADER_LEVEL
			fileHeader.level = header->level;
			//OBJ_HEADER_FIXUP_VERSION
			fileHeader.fixupVersion = header->fixupVersion;
			//OBJ_HEADER_LAST_MODIFIED_TIME
			fileHeader.lastPlayedTime = header->lastPlayedTime;

			//OBJ_HEADER_PUB_ACCOUNTNAME
			fileHeader.pubAccountNameLen = header->pubAccountName ?
				(U32)strlen(header->pubAccountName) : 0;
			//OBJ_HEADER_PRIV_ACCOUNTNAME
			fileHeader.privAccountNameLen = header->privAccountName ?
				(U32)strlen(header->privAccountName) : 0;
			//OBJ_HEADER_SAVEDNAME
			fileHeader.savedNameLen = header->savedName ?
				(U32)strlen(header->savedName) : 0;

			//OBJ_HEADER_VIRTUAL_SHARD_ID
			fileHeader.virtualShardId = header->virtualShardId;

			containerHeader.headerSize = sizeof(containerHeader) + sizeof(fileHeader) + fileHeader.privAccountNameLen+1 +
				fileHeader.pubAccountNameLen+1 + fileHeader.savedNameLen+1;

			HEADER_WRITE_BYTES(containerHeader);
			HEADER_WRITE_BYTES(fileHeader);

			//OBJ_HEADER_PUB_ACCOUNTNAME
			HEADER_WRITE_STRING_OLD(pubAccountName);
			//OBJ_HEADER_PRIV_ACCOUNTNAME
			HEADER_WRITE_STRING_OLD(privAccountName);
			//OBJ_HEADER_SAVEDNAME
			HEADER_WRITE_STRING_OLD(savedName);
		}
		else
		{
			containerHeader.headerSize = sizeof(containerHeader);
			HEADER_WRITE_BYTES(containerHeader);
		}

		assert(containerHeader.headerSize == size);
		return true;
	}
	else
	{
		return false;
	}
}

static ContainerHeaderInfo *objContainerReadHeaderInternal_VersionOne(const U8 *headerPtr, U32 headerSize)
{
	const OriginalObjectIndexFileHeader *fileHeader = (const OriginalObjectIndexFileHeader *)headerPtr;
	ContainerHeaderInfo *containerHeader = callocStruct(ContainerHeaderInfo);
	containerHeader->objectIndexHeaderType = OBJECT_INDEX_HEADER_TYPE_ENTITYPLAYER;

	if (headerSize >= 4 && fileHeader->crc == CONTAINER_FILE_HEADER_ORIGINAL_VERSION)
	{
		ObjectIndexHeader *header = NULL;
		const U8 *strings = headerPtr + sizeof(*fileHeader);
		size_t offset = 0;

		header = callocStruct(ObjectIndexHeader);
		containerHeader->objectHeader = header;

		// header field enum included for find in files when adding new fields

		//OBJ_HEADER_CONTAINERID
		header->containerId = fileHeader->containerId;
		//OBJ_HEADER_ACCOUNTID
		header->accountId = fileHeader->accountId;
		//OBJ_HEADER_CREATEDTIME
		header->createdTime = fileHeader->createdTime;
		//OBJ_HEADER_LEVEL
		header->level = fileHeader->level;
		//OBJ_HEADER_FIXUP_VERSION
		header->fixupVersion = fileHeader->fixupVersion;
		//OBJ_HEADER_LAST_MODIFIED_TIME
		header->lastPlayedTime = fileHeader->lastPlayedTime;

		//OBJ_HEADER_PUB_ACCOUNTNAME
		header->pubAccountName = strdup(strings + offset);
		offset += fileHeader->pubAccountNameLen + 1;
		//OBJ_HEADER_PRIV_ACCOUNTNAME
		header->privAccountName = strdup(strings + offset);
		offset += fileHeader->privAccountNameLen + 1;
		//OBJ_HEADER_SAVEDNAME
		header->savedName = strdup(strings + offset);

		header->extraDataOutOfDate = 1;
	}
	return containerHeader;
}

static ObjectIndexHeader *objContainerReadHeaderInternal_VersionTwo(const U8 *headerPtr, const ContainerFileHeader *containerFileHeader, const ObjectIndexFileHeader *fileHeader)
{
	const ObjectIndexFileHeaderTwo *fileHeader2 = (ObjectIndexFileHeaderTwo*) fileHeader;
	const U8 *strings = headerPtr + sizeof(*containerFileHeader) + sizeof(*fileHeader2);
	ObjectIndexHeader *header = NULL;
	size_t offset = 0;

	header = callocStruct(ObjectIndexHeader);

	// header field enum included for find in files when adding new fields

	//OBJ_HEADER_CONTAINERID
	header->containerId = fileHeader2->containerId;
	//OBJ_HEADER_ACCOUNTID
	header->accountId = fileHeader2->accountId;
	//OBJ_HEADER_CREATEDTIME
	header->createdTime = fileHeader2->createdTime;
	//OBJ_HEADER_LEVEL
	header->level = fileHeader2->level;
	//OBJ_HEADER_FIXUP_VERSION
	header->fixupVersion = fileHeader2->fixupVersion;
	//OBJ_HEADER_LAST_MODIFIED_TIME
	header->lastPlayedTime = fileHeader2->lastPlayedTime;

	//OBJ_HEADER_PUB_ACCOUNTNAME
	header->pubAccountName = strdup(strings + offset);
	offset += fileHeader2->pubAccountNameLen + 1;
	//OBJ_HEADER_PRIV_ACCOUNTNAME
	header->privAccountName = strdup(strings + offset);
	offset += fileHeader2->privAccountNameLen + 1;
	//OBJ_HEADER_SAVEDNAME
	header->savedName = strdup(strings + offset);

	//OBJ_HEADER_VIRTUAL_SHARD_ID
	header->virtualShardId = fileHeader2->virtualShardId;

	// This is an old version without extra data at all. Just mark it out of date.
	header->extraDataOutOfDate = 1;

	return header;
}

#define HEADER_READ_STRING(str) \
	if(fileHeader->str##Len) \
	{						\
		header->str = strdup(strings + offset);	\
		offset += fileHeader->str##Len + 1;		\
	}


static ObjectIndexHeader *objContainerReadHeaderInternal(const U8 *headerPtr, const ContainerFileHeader *containerFileHeader, const ObjectIndexFileHeader *fileHeader)
{
	const U8 *strings = headerPtr + sizeof(*containerFileHeader) + sizeof(*fileHeader);
	size_t offset = 0;
	ObjectIndexHeader *header = NULL;

	header = callocStruct(ObjectIndexHeader);

	// header field enum included for find in files when adding new fields

	//OBJ_HEADER_CONTAINERID
	header->containerId = fileHeader->containerId;
	//OBJ_HEADER_ACCOUNTID
	header->accountId = fileHeader->accountId;
	//OBJ_HEADER_CREATEDTIME
	header->createdTime = fileHeader->createdTime;
	//OBJ_HEADER_LEVEL
	header->level = fileHeader->level;
	//OBJ_HEADER_FIXUP_VERSION
	header->fixupVersion = fileHeader->fixupVersion;
	//OBJ_HEADER_LAST_MODIFIED_TIME
	header->lastPlayedTime = fileHeader->lastPlayedTime;

	// These three strings need not to be NULL
	//OBJ_HEADER_PUB_ACCOUNTNAME
	HEADER_READ_STRING(pubAccountName);
	if(!header->pubAccountName)
		header->pubAccountName = strdup("");
	//OBJ_HEADER_PRIV_ACCOUNTNAME
	HEADER_READ_STRING(privAccountName);
	if(!header->privAccountName)
		header->privAccountName = strdup("");
	//OBJ_HEADER_SAVEDNAME
	HEADER_READ_STRING(savedName);
	if(!header->savedName)
		header->savedName = strdup("");

	//OBJ_HEADER_VIRTUAL_SHARD_ID
	header->virtualShardId = fileHeader->virtualShardId;

	if(fileHeader->extraDataCrc == extraDataCRC)
	{
		//OBJ_HEADER_EXTRA_1
		HEADER_READ_STRING(extraData1);
		//OBJ_HEADER_EXTRA_2
		HEADER_READ_STRING(extraData2);
		//OBJ_HEADER_EXTRA_3
		HEADER_READ_STRING(extraData3);
		//OBJ_HEADER_EXTRA_4
		HEADER_READ_STRING(extraData4);
		//OBJ_HEADER_EXTRA_5
		HEADER_READ_STRING(extraData5);
	}
	else
	{
		header->extraDataOutOfDate = 1;
	}

	return header;
}

static ContainerHeaderInfo *objContainerReadHeader(const U8 *headerPtr, U32 headerSize)
{
	ContainerHeaderInfo *containerHeader = NULL;
	ObjectIndexHeader *header = NULL;
	const ContainerFileHeader *containerFileHeader = (const ContainerFileHeader *)headerPtr;

	if (headerSize >= 4)
	{
		switch (containerFileHeader->headerVersion)
		{
			case CONTAINER_FILE_HEADER_CURRENT_VERSION:
			{
				containerHeader = callocStruct(ContainerHeaderInfo);

				containerHeader->deletedTime = containerFileHeader->deletedTime;

				switch (containerFileHeader->objextIndexHeaderType)
				{
					case OBJECT_INDEX_HEADER_TYPE_ENTITYPLAYER:
					{
						const ObjectIndexFileHeader *fileHeader = (ObjectIndexFileHeader*)(headerPtr + sizeof(*containerFileHeader));
						containerHeader->objectIndexHeaderType = containerFileHeader->objextIndexHeaderType;
						if(fileHeader->crc == OBJECT_FILE_HEADER_CRC_TWO)
						{
							containerHeader->objectHeader = objContainerReadHeaderInternal_VersionTwo(headerPtr, containerFileHeader, fileHeader);
						}
						else if(fileHeader->crc == headerCRC)
						{
							containerHeader->objectHeader = objContainerReadHeaderInternal(headerPtr, containerFileHeader, fileHeader);
						}
					}
					break;
				}
			}
			break;
			case CONTAINER_FILE_HEADER_ORIGINAL_VERSION:
			{
				containerHeader = objContainerReadHeaderInternal_VersionOne(headerPtr, headerSize);
			}
			break;
		}
	}

	return containerHeader;
}

#if 0
// Used for testing out the writing and reading of old header formats. Leave commented out unless you need to test it.
AUTO_COMMAND;
void TestReadingOfOldHeaders(ContainerID containerID)
{
	// Get the container and write out the header in old formats. Then, read it back in and see that the correct things happen
	Container *con;
	ContainerStore *store = objFindContainerStoreFromType(GLOBALTYPE_ENTITYPLAYER);
	ContainerHeaderInfo *info = NULL;
	char *estr = NULL;

	if(!store)
	{
		printf("EntityPlayer ContainerStore does not exist\n");
		return;
	}

	con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, containerID, true, false, true);

	if(!con)
	{
		printf("EntityPlayer %u does not exist\n", containerID);
		return;
	}

	if(!con->header)
	{
		printf("EntityPlayer %u has no header", containerID);
		objUnlockContainer(&con);
		return;
	}

	objContainerWriteHeader_VersionOne_DoNotUse(con->header, &estr);
	info = objContainerReadHeader(estr, estrLength(&estr));
	estrClear(&estr);

	objContainerWriteHeader_VersionTwo_DoNotUse(store, con->header, &estr, 0);
	info = objContainerReadHeader(estr, estrLength(&estr));
	estrClear(&estr);

	objContainerWriteHeader(store, con->header, &estr, 0);
	info = objContainerReadHeader(estr, estrLength(&estr));
	estrDestroy(&estr);
	objUnlockContainer(&con);
}
#endif

int verifyHeaders;

AUTO_CMD_INT(verifyHeaders, verifyHeaders) ACMD_COMMANDLINE;

#define objContainerToEstring(conup) objContainerToEstring_dbg(conup, MEM_DBG_PARMS_INIT_VOID)

static int objContainerToEstring_dbg(ContainerUpdate *conup, MEM_DBG_PARMS_VOID)
{
	Container *con = conup->container;
	const char *ext;
	int retval;

	PERFINFO_AUTO_START_FUNC();

	if(con->fileData && !con->containerData)
	{
		objUnpackContainer(con->containerSchema, con, false, false, true);
	}

	if(verifyHeaders && con->header)
		objVerifyHeader(con, con->containerData, con->containerSchema);

	if(con->rawHeader)
	{
		SAFE_FREE(con->rawHeader);
		con->headerSize = 0;
	}

	if(objContainerWriteHeader(objFindContainerStoreFromType(con->containerType), con->header, &conup->estring, conup->iDeletedTime))
	{
		ext = ".co2";
		con->headerSize = estrLength(&conup->estring) + 1;
		con->rawHeader = malloc(con->headerSize);
		memcpy(con->rawHeader, conup->estring, con->headerSize);
	}
	else
		ext = ".con";


	sprintf(conup->fileName,"%s/%d%s", GlobalTypeToName(con->containerType),
		con->containerID,ext);
	retval = ParserWriteText_dbg(&conup->estring,con->containerSchema->classParse,con->containerData,0, TOK_PERSIST,0, MEM_DBG_PARMS_CALL_VOID);

	PERFINFO_AUTO_STOP();
	return retval;
}

static int sUnloadedContainers[GLOBALTYPE_MAXTYPES];

int objCountUnloadedContainersOfType(GlobalType eType)
{
	if (eType >= GLOBALTYPE_MAXTYPES) return 0;
	return sUnloadedContainers[eType];
}

void objUnloadContainer(Container *con)
{
	U32 checksum[4] = {0};
	ContainerUpdate conup = {0};

	if(!con || !con->containerData)
		return;

	PERFINFO_AUTO_START_FUNC();

	conup.container = con;
	conup.estring = AllocConupEstr();
	objContainerToEstring(&conup);
	con->fileSize = estrLength(&conup.estring) + 1;
	con->fileSizeHint = con->fileSize;
	pigChecksumData(conup.estring,con->fileSize,checksum);
	con->checksum = checksum[0];
	con->fileData = zipDataEx(conup.estring, con->fileSize, &con->bytesCompressed, suContainerRepackingCompressionLevel, false, CRYPTIC_CONTAINER_HEAP);
	FreeConupEstr(&conup.estring);
	++sUnloadedContainers[GLOBALTYPE_NONE];
	++sUnloadedContainers[con->containerType];

	objDeInitContainerObject(con->containerSchema,con->containerData);
	objDestroyContainerObject(con->containerSchema,con->containerData);
	con->containerData = NULL;

	PERFINFO_AUTO_STOP();
}

typedef struct StaleContainerRef
{
	GlobalType containerType;
	ContainerID containerID;
	U32 accessCutoff;
} StaleContainerRef;

TSMP_DEFINE(StaleContainerRef);

StaleContainerRef *AllocateStaleContainerRef(void)
{
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(StaleContainerRef, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	return TSMP_CALLOC(StaleContainerRef);
}

void FreeStaleContainerRef(StaleContainerRef **ref)
{
	if(!ref || !*ref)
		return;

	TSMP_FREE(StaleContainerRef, *ref);
	*ref = NULL;
}

typedef struct StaleObjectTrackerType
{
	GlobalType myType;
	CutoffIndexCallback cutoffIndexCB;
	MakeExtraCutoffsCallback extraCutoffsCB;
	NotStaleCallback notStaleCB;
	StaleContainerRef **staleContainers;

	// This is an index directly into the container store's container array. We 
	// don't need precision for this, so if we miss some as the array is rearranged, it is okay.
	int iterator;
} StaleObjectTrackerType;

typedef struct StaleObjectTracker
{
	StaleObjectTrackerType **typeSpecificTrackers;

	// This is an index directly into the tracker's type tracker array. We 
	// don't need precision for this, so if we miss some as the array is rearranged, it is okay.
	int iterator;
	bool hasStaleContainers;
} StaleObjectTracker;

#define IS_ACTIVE(con, accessCutoff, hasLock) (!con->containerData || con->isSubscribed || \
	con->savedUpdate || (!hasLock && con->updateLocked) || con->isTemporary || \
	con->lastAccessed > accessCutoff || \
	(IsTypeMasterObjectDB(serverType) && \
		(con->meta.containerOwnerType != serverType || con->meta.containerOwnerID != serverID || con->meta.containerState != CONTAINERSTATE_OWNED)))

// Maximum number of stale containers to unload per second
int staleContainerUnloadRate = 1000;
AUTO_CMD_INT(staleContainerUnloadRate, ContainerUnloadMaxRate) ACMD_COMMANDLINE ACMD_AUTO_SETTING(ObjectDB, OBJECTDB, CLONEOBJECTDB, CLONEOFCLONE);

// Maximum time to spend unloading stale containers of one type per second (in milliseconds)
static S64 unloadTypeProcessTimeMs = 10;
AUTO_CMD_INT(unloadTypeProcessTimeMs, ContainerUnloadTypeProcessTime) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB, CLONEOBJECTDB, CLONEOFCLONE);

// Maximum time to spend unloading stale containers per second (in milliseconds)
static S64 unloadOverallProcessTimeMs = 50;
AUTO_CMD_INT(unloadOverallProcessTimeMs, ContainerUnloadProcessTime) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB, CLONEOBJECTDB, CLONEOFCLONE);

bool sDebugMakeRemovesSlow = false;
AUTO_CMD_INT(sDebugMakeRemovesSlow, debugMakeRemovesSlow) ACMD_COMMANDLINE ACMD_HIDE;

int dbPauseZippedDataCachingInSaveThreads = false;
AUTO_CMD_INT(dbPauseZippedDataCachingInSaveThreads, PauseZippedDataCachingInSaveThreads) ACMD_CMDLINE;

int dbDisableZippedDataCachingInSaveThreads = false;
AUTO_CMD_INT(dbDisableZippedDataCachingInSaveThreads, DisableZippedDataCachingInSaveThreads) ACMD_CMDLINE;

static int sFreeRepacks[GLOBALTYPE_MAXTYPES];

int objCountFreeRepacksOfType(GlobalType eType)
{
	if (eType >= GLOBALTYPE_MAXTYPES) return 0;
	return sFreeRepacks[eType];
}

typedef struct QueuedDestroyContainerObjectData
{
	ContainerSchema* schema;
	void *containerData;
} QueuedDestroyContainerObjectData;

TSMP_DEFINE(QueuedDestroyContainerObjectData);

static void DestroyContainerObject_CB(QueuedDestroyContainerObjectData *userData)
{
	objDeInitContainerObject(userData->schema,userData->containerData);
	objDestroyContainerObject(userData->schema,userData->containerData);
	TSMP_FREE(QueuedDestroyContainerObjectData, userData);
}

void QueueDestroyContainerObjectInBackground(ContainerSchema *schema, void *containerData)
{
	QueuedDestroyContainerObjectData *userData;
	ATOMIC_INIT_BEGIN;
	TSMP_CREATE(QueuedDestroyContainerObjectData, 256);
	ATOMIC_INIT_END;

	userData = TSMP_ALLOC(QueuedDestroyContainerObjectData);
	userData->schema = schema;
	userData->containerData = containerData;
	FreeThreadQueue(userData, DestroyContainerObject_CB);
}

void objRemoveStaleContainers(TimedCallback *callback, F32 timeSinceLastCallback, void *userdata)
{
	static PERFINFO_TYPE *perfs[GLOBALTYPE_MAXTYPES];
	StaleObjectTracker *tracker = userdata;
	GlobalType serverType = GetAppGlobalType();
	U32 serverID = GetAppGlobalID();
	int count = staleContainerUnloadRate;
	S64 singleTypeTrackerProcessTimeTicks = unloadTypeProcessTimeMs * timerCpuSpeed64() / 1000;
	S64 overallTrackerProcessTimeTicks = unloadOverallProcessTimeMs * timerCpuSpeed64() / 1000;
	S64 removeStartTime = timerCpuTicks64();

	if(s_pendingSaveCount > 0)
		return;

	PERFINFO_AUTO_START_FUNC();

	if(tracker->iterator >= eaSize(&tracker->typeSpecificTrackers))
		tracker->iterator = 0;

	do
	{
		volatile S64 typeTrackerStartingTime = timerCpuTicks64();
		StaleObjectTrackerType *typeTracker = tracker->typeSpecificTrackers[tracker->iterator];

		if(!eaSize(&typeTracker->staleContainers))
		{
			tracker->iterator++;
			if(tracker->iterator >= eaSize(&tracker->typeSpecificTrackers))
				tracker->iterator = 0;
			continue;
		}

		do
		{
			void *containerData = NULL;
			StaleContainerRef *ref = eaPop(&typeTracker->staleContainers);
			U32 accessCutoff = ref->accessCutoff;
			Container *con = objGetContainerEx(ref->containerType, ref->containerID, false, false, true);
			ContainerSchema *schema;
			FreeStaleContainerRef(&ref);

			if(!con)
				continue;

			if(IS_ACTIVE(con, accessCutoff, false))
			{
				objUnlockContainer(&con);
				continue;
			}

			// don't bother checking the spinlock if this doesn't count as active
			while (InterlockedIncrement(&con->updateLocked) > 1)
			{
				InterlockedDecrement(&con->updateLocked);
				Sleep(0);
			}

			//Check active again in case a background thread just unpacked it.
			if(IS_ACTIVE(con, accessCutoff, true))
			{
				InterlockedDecrement(&con->updateLocked);
				objUnlockContainer(&con);
				continue;
			}

			PERFINFO_AUTO_START_STATIC(GlobalTypeToName(typeTracker->myType), &perfs[typeTracker->myType], 1);
			// if we have saved the fileData, just throw away the containerData
			if(!dbDisableZippedDataCachingInSaveThreads && con->fileData && con->containerData)
			{
				ADD_MISC_COUNT(1, "Free repack");
				containerData = con->containerData;
				con->containerData = NULL;
				++sFreeRepacks[GLOBALTYPE_NONE];
				++sFreeRepacks[typeTracker->myType];
			}
			else
			{
				PERFINFO_AUTO_START("Repack", 1);
				objUnloadContainer(con);
				PERFINFO_AUTO_STOP();
			}

			schema = con->containerSchema;

			InterlockedDecrement(&con->updateLocked);
			objUnlockContainer(&con);

			if(containerData)
			{
				QueueDestroyContainerObjectInBackground(schema, containerData);
			}

			PERFINFO_AUTO_STOP();

			if(--count <= 0)
				break;

			while (sDebugMakeRemovesSlow && timerCpuTicks64() - typeTrackerStartingTime < singleTypeTrackerProcessTimeTicks);
		} while(eaSize(&typeTracker->staleContainers) > 0 && (timerCpuTicks64() - typeTrackerStartingTime < singleTypeTrackerProcessTimeTicks));

		tracker->iterator++;
		if(tracker->iterator >= eaSize(&tracker->typeSpecificTrackers))
			tracker->iterator = 0;
	}  while(timerCpuTicks64() - removeStartTime < overallTrackerProcessTimeTicks);

	// Only run this callback until you run out of containers,
	// then wait for objFindStaleContainers to run again
	tracker->hasStaleContainers = false;
	EARRAY_FOREACH_BEGIN(tracker->typeSpecificTrackers, i);
	{
		if (eaSize(&tracker->typeSpecificTrackers[i]->staleContainers) > 0)
		{
			tracker->hasStaleContainers = true;
			break;
		}
	}
	EARRAY_FOREACH_END;

	if (!tracker->hasStaleContainers)
		callback->remove = true;

	PERFINFO_AUTO_STOP();
}

void addStaleContainer(StaleObjectTracker *tracker, StaleObjectTrackerType *typeTracker, GlobalType conType, ContainerID conID, U32 accessCutoff)
{
	StaleContainerRef *ref = AllocateStaleContainerRef();
	ref->containerType = conType;
	ref->containerID = conID;
	ref->accessCutoff = accessCutoff;
	eaPush(&typeTracker->staleContainers, ref);
	tracker->hasStaleContainers = true;
}

// Maximum time to spend queueing stale containers of one type for unloading each check interval (in milliseconds)
static S64 staleFindTypeProcessTimeMs = 10;
AUTO_CMD_INT(staleFindTypeProcessTimeMs, StaleContainerTypeProcessTime) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB, CLONEOBJECTDB, CLONEOFCLONE);

// Maximum time to spend queueing stale containers for unloading each check interval (in milliseconds)
static S64 staleFindOverallProcessTimeMs = 50;
AUTO_CMD_INT(staleFindOverallProcessTimeMs, StaleContainerProcessTime) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB, CLONEOBJECTDB, CLONEOFCLONE);

static U32 shortenStaleContainerThreshold = 0; //Instead of multiplying the stale container cleanup by days, multiply by minutes
AUTO_CMD_INT(shortenStaleContainerThreshold,debugshortenStaleContainerThreshold) ACMD_COMMANDLINE;

static void ClampTracker(StaleObjectTracker *tracker)
{
	if(tracker->iterator >= eaSize(&tracker->typeSpecificTrackers))
		tracker->iterator = 0;
}

static void IncrementTracker(StaleObjectTracker *tracker)
{
	tracker->iterator++;
	ClampTracker(tracker);
}

static void ClampTypeTracker(StaleObjectTrackerType *typeTracker)
{
	if(typeTracker->iterator >= objCountTotalContainersWithType(typeTracker->myType))
		typeTracker->iterator = 0;
}

static void IncrementTypeTracker(StaleObjectTrackerType *typeTracker)
{
	typeTracker->iterator++;
	ClampTypeTracker(typeTracker);
}

void objFindStaleContainers(StaleObjectTracker *tracker)
{
	int trackerStartingIterator;
	ObjectIndexKey *key = NULL;
	U32 iThresholdMultiplier = (shortenStaleContainerThreshold ? SECONDS_PER_MINUTE : SECONDS_PER_HOUR);
	U32 now = timeSecondsSince2000();
	U32 newPlayedCutoff = now -
		gContainerSource.containerPlayedUnloadThresholdHours * iThresholdMultiplier;
	U32 newAccessedCutoff = now -
		gContainerSource.containerAccessUnloadThresholdHours * iThresholdMultiplier;
	GlobalType serverType = GetAppGlobalType();
	U32 serverID = GetAppGlobalID();
	S64 singleTypeTrackerProcessTimeTicks = staleFindTypeProcessTimeMs * timerCpuSpeed64() / 1000;
	S64 overallTrackerProcessTimeTicks = staleFindOverallProcessTimeMs * timerCpuSpeed64() / 1000;
	volatile S64 overallTrackerStartingtime = timerCpuTicks64();

	if(!eaSize(&tracker->typeSpecificTrackers))
		return;

	PERFINFO_AUTO_START_FUNC();

	ClampTracker(tracker);

	trackerStartingIterator = tracker->iterator;
	
	do
	{
		int startingIterator;
		volatile S64 typeTrackerStartingtime = timerCpuTicks64();
		StaleObjectTrackerType *typeTracker = tracker->typeSpecificTrackers[tracker->iterator];
		ContainerStore *miscStore = objFindContainerStoreFromType(typeTracker->myType);
		U32 extraCutoffs[MAX_STALE_CUTOFFS] = {0};

		if(!miscStore || !miscStore->lazyLoad || !objCountTotalContainersWithType(typeTracker->myType))
		{
			IncrementTracker(tracker);
			continue;
		}

		if(typeTracker->extraCutoffsCB)
		{
			typeTracker->extraCutoffsCB(now, iThresholdMultiplier, newPlayedCutoff, extraCutoffs);
		}

		objLockContainerStore_ReadOnly(miscStore);

		if(!objCountTotalContainersWithType(typeTracker->myType))
		{
			objUnlockContainerStore_ReadOnly(miscStore);
			IncrementTracker(tracker);
			continue;
		}

		ClampTypeTracker(typeTracker);

		startingIterator = typeTracker->iterator;

		do
		{
			Container *con = miscStore->containers[typeTracker->iterator];
			U32 playedCutoffToUse = newPlayedCutoff;
			U32 accessCutoffToUse = newAccessedCutoff;
			if(typeTracker->cutoffIndexCB)
			{
				int cutoffIndex = typeTracker->cutoffIndexCB(con);
				if(cutoffIndex >= 0)
					playedCutoffToUse = extraCutoffs[cutoffIndex];
			}

			if(!IS_ACTIVE(con, accessCutoffToUse, false) && !(typeTracker->notStaleCB && typeTracker->notStaleCB(con, playedCutoffToUse)))
				addStaleContainer(tracker, typeTracker, typeTracker->myType, con->containerID, accessCutoffToUse);

			IncrementTypeTracker(typeTracker);
		} while(typeTracker->iterator != startingIterator && (timerCpuTicks64() - typeTrackerStartingtime < singleTypeTrackerProcessTimeTicks));
		objUnlockContainerStore_ReadOnly(miscStore);

		IncrementTracker(tracker);
	}  while(tracker->iterator != trackerStartingIterator && timerCpuTicks64() - overallTrackerStartingtime < overallTrackerProcessTimeTicks);

	PERFINFO_AUTO_STOP();
}

void objCleanStaleContainers(TimedCallback *callback, F32 timeSinceLastCallback, void *userdata)
{
	StaleObjectTracker *tracker = userdata;

	if(tracker->hasStaleContainers)
		return;

	objFindStaleContainers(tracker);

	if(tracker->hasStaleContainers)
		TimedCallback_Add(objRemoveStaleContainers, tracker, 1);
}

StaleObjectTracker gStaleObjectTracker;

// Interval between stale container checks (changes take effect after running DeactivateStaleContainerCleanup and then ActivateStaleContainerCleanup)
int staleContainerCheckInterval = 60;
AUTO_CMD_INT(staleContainerCheckInterval, StaleContainerCheckInterval) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB, CLONEOBJECTDB, CLONEOFCLONE);

static TimedCallback *CleanStaleContainersCallback = NULL;

AUTO_COMMAND;
void activateStaleContainerCleanup(void)
{
	if(!CleanStaleContainersCallback)
	{
		CleanStaleContainersCallback = TimedCallback_Add(objCleanStaleContainers, &gStaleObjectTracker, staleContainerCheckInterval);
	}
}

AUTO_COMMAND;
void deactivateStaleContainerCleanup(void)
{
	if(CleanStaleContainersCallback)
	{
		TimedCallback_Remove(CleanStaleContainersCallback);
		CleanStaleContainersCallback = NULL;
	}
}

void objStaleContainerTypeAdd(GlobalType containerType, CutoffIndexCallback cutoffIndexCB, MakeExtraCutoffsCallback extraCutoffsCB, NotStaleCallback notStaleCB)
{
	StaleObjectTrackerType *typeTracker;
	typeTracker = callocStruct(StaleObjectTrackerType);
	typeTracker->myType = containerType;
	typeTracker->cutoffIndexCB = cutoffIndexCB;
	typeTracker->extraCutoffsCB = extraCutoffsCB;
	typeTracker->notStaleCB = notStaleCB;
	eaPushUnique(&gStaleObjectTracker.typeSpecificTrackers, typeTracker);
}

void CacheZippedContainerData(ContainerUpdate *conup)
{
	if(!dbPauseZippedDataCachingInSaveThreads && !dbDisableZippedDataCachingInSaveThreads && conup->useEntry && conup->container)
	{
		ContainerStore *store = objFindContainerStoreFromType(conup->containerType);
		if(!store || !store->lazyLoad)
			return;

		PERFINFO_AUTO_START_FUNC();
		conup->container->bytesCompressed = conup->entry.pack_size;
		conup->container->fileSize = conup->entry.size;
		conup->container->fileSizeHint = conup->container->fileSize;
		conup->container->checksum = conup->entry.checksum[0];
		if(conup->entry.pack_size)
		{
			conup->container->fileData = malloc_FileData(conup->entry.pack_size);
			memcpy(conup->container->fileData, conup->entry.data, conup->entry.pack_size);
		}
		else if(conup->entry.size)
		{
			conup->container->fileData = malloc_FileData(conup->entry.size);
			memcpy(conup->container->fileData, conup->entry.data, conup->entry.size);
		}
		PERFINFO_AUTO_STOP();
	}
}

//Write the string formatted data to the hog file. The data should be stringified in the main thread.
void wt_objSaveContainer(void *user_data, void *data, WTCmdPacket *packet)
{
	ContainerUpdate *conup = (ContainerUpdate*)data;
	HogFile *the_hog = NULL;
	GlobalType containerType;
	ContainerID containerID;
	bool writeSuccess = true;
	bool allocated_string = false;

	PERFINFO_AUTO_START("objSaveContainer",1);
	containerType = conup->containerType;
	containerID = conup->containerID;

	// If we already have a string just send it, which happens if multithreaded saving is on
	if (!estrLength(&conup->estring) && !conup->useEntry)
	{
		conup->iDeletedTime = objGetDeletedTime(containerType, containerID);
		// The multiworkerthread queued version will have estring set, the main thread one will not

		if (!sbDisableContainerSaveSemaphore)
			semaphoreWait(sContainerSavesOutstanding);

		while (conup->container && InterlockedIncrement(&conup->container->updateLocked) > 1)
		{
			InterlockedDecrement(&conup->container->updateLocked);
			Sleep(0);
		}

		conup->estring = AllocConupEstr();
		writeSuccess = objContainerToEstring(conup);

		if (conup->container->isTemporary)
		{
			objDestroyContainer((Container *)conup->container);
			conup->container = NULL;
		}
		
		if (!writeSuccess)
		{
			log_printf(LOG_CONTAINER, "Failed to write container %s[%d]: Can't Write Data", GlobalTypeToName(conup->container->containerType), conup->container->containerID);
			if(conup->container)
				InterlockedDecrement(&conup->container->updateLocked);
			conup->container = NULL;
			FreeConupEstrSemaphoreSignal(&conup->estring);
			conup->estring = NULL;
			objDecrementServiceTime();
			PERFINFO_AUTO_STOP();
			return;
		}

		objZipContainerData(conup, objContainerGetSystemTimestamp(), FreeConupEstrCB);
		CacheZippedContainerData(conup);
		if(conup->container)
			InterlockedDecrement(&conup->container->updateLocked);
		conup->container = NULL;
	}

	if (containerType <= 0 || containerType >= GLOBALTYPE_LAST)
	{
		log_printf(LOG_CONTAINER, "Failed to write container %s[%d]: Invalid container type", GlobalTypeToName(containerType), containerID);
		conup->container = NULL;
		FreeConupEstr(&conup->estring);
		conup->estring = NULL;
		if(conup->entry.free_callback && conup->entry.data)
		{
			conup->entry.free_callback(conup->entry.data);
		}
		else
		{
			SAFE_FREE(conup->entry.data);
		}

		PERFINFO_AUTO_STOP();
		objDecrementServiceTime();
		return;	
	}

	verbose_printf("Writing Container %s[%d]\n",GlobalTypeToName(containerType),containerID);

	//sprintf(fileName,"%s/%d.co2",GlobalTypeToName(containerType),containerID);

	EnterCriticalSection(&gContainerSource.hog_access);
	do {
		if ((the_hog = conup->hog) != NULL) {}								//the)hot is set
		else if ((the_hog = gContainerSource.incrementalHog) != NULL) {}	//the_hog is set
		else if ((the_hog = gContainerSource.fullHog) != NULL) {}			//the_hog is set
		else 
		{
			FreeConupEstr(&conup->estring);
			conup->estring = NULL;
			if(conup->entry.free_callback && conup->entry.data)
			{
				conup->entry.free_callback(conup->entry.data);
			}
			else
			{
				SAFE_FREE(conup->entry.data);
			}
			objDecrementServiceTime();
			break;
		}

		assert(conup->useEntry);
		if (objFileModifyUpdateNamedZippedEx(the_hog, conup, objContainerGetSystemTimestamp(),conup->serviceTime, NULL) != 0)
		{ 
			log_printf(LOG_CONTAINER, "Failed to write container %s[%d]: Can't Modify Hog", GlobalTypeToName(containerType), containerID);
			break;
		}

		if (conup->bIsDelete)
		{
			ContainerHogMarkDeleted(the_hog,containerType,containerID, 1);
		}
		else
		{
			ContainerHogRemoveDeletedMark(the_hog,containerType,containerID);
		}
	} while(false);
	
	objSaveSchemaSource(containerType, the_hog);

	LeaveCriticalSection(&gContainerSource.hog_access);

	PERFINFO_AUTO_STOP();
}

//Mark incremental hog container as deleted, remove from full hog, or delete file.
void wt_objPermanentlyDeleteContainer(void *user_data, void *data, WTCmdPacket *packet)
{
	// This is apparently defunct -BZ
/*	ContainerUpdate *conup = (ContainerUpdate*)data;
	char fileName[MAX_PATH];

	PERFINFO_AUTO_START("objPermanentlyDeleteContainer",1);

	if (!ParserWriteText(&conup->estring,conup->container->containerSchema->classParse,conup->container->containerData,0, TOK_PERSIST,0))
	{
		Errorf("Could not write container data.\n");
		FreeConupEstr(&conup->estring);
		conup->estring = NULL;
		PERFINFO_AUTO_STOP();
		return;
	}

	if (!gContainerSource.useHogs)
	{
		log_printf(LOG_CONTAINER, "Permanently deleting Container %s[%d]", GlobalTypeToName(conup->container->containerType),conup->container->containerID);
		verbose_printf("Permanently deleting Container %s[%d]\n",GlobalTypeToName(conup->container->containerType),conup->container->containerID);
		sprintf(fileName,"%s/%s/%d.con",gContainerSource.sourcePath,GlobalTypeToName(conup->container->containerType),conup->container->containerID);
		fileForceRemove(fileName);
		wt_objSaveContainerSourceInfo(NULL,NULL,NULL);
		FreeConupEstr(&conup->estring);
		conup->estring = NULL;
	}
	else
	{
		HogFile *the_hog = NULL;
		// Get the file to mark for deletion.
		sprintf(fileName,"%s/%d.con",GlobalTypeToName(conup->container->containerType),conup->container->containerID);
		
		EnterCriticalSection(&gContainerSource.hog_access);
		do {
			if ((the_hog = gContainerSource.incrementalHog) != NULL) {
				if (objFileModifyUpdateNamed(the_hog,fileName,conup->estring,estrLength(&conup->estring)+1,objContainerGetSystemTimestamp(),conup->serviceTime) != 0)
				{	// 0 is success
					break;
				}			
			}
			else if ((the_hog = gContainerSource.fullHog) != NULL) {
				log_printf(LOG_CONTAINER, "Deleting Container %s[%d] from fullhog", GlobalTypeToName(conup->container->containerType),conup->container->containerID);
				verbose_printf("Deleting Container %s[%d] from fullhog.\n",GlobalTypeToName(conup->container->containerType),conup->container->containerID);
				objFileModifyUpdateNamed(the_hog,fileName,NULL,0,0,conup->serviceTime);

				
			}
			else break;
		} while (false);
		LeaveCriticalSection(&gContainerSource.hog_access);
	}
	wtQueueMsg(gContainerSource.hog_update_thread, HOGUPDATETHREAD_MSGDESTROYCONTAINER, conup, sizeof(Container*));

	PERFINFO_AUTO_STOP();*/
}


//Create and swap the incremental hog in the secondary thread.
void wt_objCreateSwapSyncAndCloseIncrementalHogFile(void *user_data, void *data, WTCmdPacket *packet)
{
	char *new_path = (char*)data;
	char old_path[MAX_PATH];
	HogFile *new_inc = NULL; 
	HogFile *old_inc = NULL;

	PERFINFO_AUTO_START_FUNC();

	strcpy(old_path, gContainerSource.incrementalPath);

	//Create
	//opens a new hog and sets the gContainerSource.incrementalHog reference to it.
	CONTAINER_LOGPRINT("Opening new incremental hogg file %s.\n", new_path);
	new_inc = objOpenIncrementalHogFile(new_path);
	assertmsg(new_inc, "Could not open incremental hog file");
	
	EnterCriticalSection(&gContainerSource.hog_access);
	do {
		old_inc = gContainerSource.incrementalHog;

		//Sync (since we're in the hog thread, just call directly.)
		if (old_inc)
			hogFileModifyFlush(old_inc);

		//wt_objSaveContainerSourceInfo(NULL, NULL, NULL);
		
		//Swap
		CONTAINER_LOGPRINT("Switching to use new incremental hog file %s.\n", new_path);
		gContainerSource.incrementalHog = new_inc;
		strcpy(gContainerSource.incrementalPath, new_path);

		//closes the old hog if it exists
		if (old_inc)
		{

			//Close/Flush/Delete the old hog. 
			CONTAINER_LOGPRINT("Closing old incremental hog file %s.\n", old_path);
			hogFileDestroy(old_inc, false /*JE: Unsure if this is leaking, leaving as it was before to be safe (might leak)*/);

			RenameCurrentHoggToPending(old_path);
		} 
	} while (false);
	wtQueueMsg(gContainerSource.hog_update_thread, HOGUPDATETHREAD_MSGPOPQUEUEDROTATE, NULL, 0);
	LeaveCriticalSection(&gContainerSource.hog_access);

	PERFINFO_AUTO_STOP();

	CONTAINER_LOGPRINT("Rotation complete.\n");
}

//Flush any hogfile changes, this should only be qu
void wt_objFlushHogContainers(void *user_data, void *data, WTCmdPacket *packet)
{
	HogFile *the_hog = NULL;

	PERFINFO_AUTO_START_FUNC();

	EnterCriticalSection(&gContainerSource.hog_access);
	do {
		if ((the_hog = gContainerSource.incrementalHog) != NULL) {}	//the_hog is set
		else if ((the_hog = gContainerSource.fullHog) != NULL) {}		//the_hog is set
		else break;

		wt_objSaveContainerSourceInfo(NULL,&the_hog,NULL);
		hogFileModifyFlush(the_hog);
	} while (false);
	LeaveCriticalSection(&gContainerSource.hog_access);

	PERFINFO_AUTO_STOP();
}

void wt_objSetTimeStamp(void *user_data, void *data, WTCmdPacket *packet)
{
	U32 timeStamp = ((U32*)data)[0];

	PERFINFO_AUTO_START_FUNC();

	EnterCriticalSection(&gContainerSource.hog_access);
	do {
		if (gContainerSource.sourceInfo)
		{
			gContainerSource.sourceInfo->iLastTimeStamp = timeStamp;
		}
	} while (false);
	LeaveCriticalSection(&gContainerSource.hog_access);

	PERFINFO_AUTO_STOP();
}

void wt_objSetMaxID(void *user_data, void *data, WTCmdPacket *packet)
{
	ContainerRepositoryTypeInfo *typeInfo = ((ContainerRepositoryTypeInfo*)*((void**)data));

	PERFINFO_AUTO_START_FUNC();

	EnterCriticalSection(&gContainerSource.hog_access);
	{
		if (gContainerSource.sourceInfo)
		{
			ContainerRepositoryTypeInfo *foundInfo = eaIndexedGetUsingInt(&gContainerSource.sourceInfo->ppRepositoryTypeInfo, typeInfo->eGlobalType);

			if(foundInfo)
			{
				if(typeInfo->iMaxContainerID > foundInfo->iMaxContainerID)
					foundInfo->iMaxContainerID = typeInfo->iMaxContainerID;
			}
			else
			{
				eaPush(&gContainerSource.sourceInfo->ppRepositoryTypeInfo, typeInfo);
				typeInfo = NULL;
			}
		}
	}
	LeaveCriticalSection(&gContainerSource.hog_access);

	StructDestroySafe(parse_ContainerRepositoryTypeInfo, &typeInfo);

	PERFINFO_AUTO_STOP();
}

//Handle the sequence number incrementing in the second thread so the numbers are ordered in the thread queue.
void wt_objSetSequence(void *user_data, void *data, WTCmdPacket *packet)
{
	U64 sequence = ((U64*)data)[0];

	PERFINFO_AUTO_START_FUNC();

	EnterCriticalSection(&gContainerSource.hog_access);
	do {
		if (gContainerSource.sourceInfo)
		{
			gContainerSource.sourceInfo->iLastSequenceNumber = sequence;
		}
	} while (false);
	LeaveCriticalSection(&gContainerSource.hog_access);

	PERFINFO_AUTO_STOP();
}

void RenameCurrentHoggToPending(char *old_path)
{
	char move_path[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	strcpy(move_path, old_path);
	objConvertToPendingFilename(move_path);

	fprintf(fileGetStderr(), "Renaming %s to %s. \n", old_path, move_path);
	if (fileMove(old_path, move_path) != 0 && isProductionMode()) AssertOrAlert("OBJECTDB_RENAME_FAILED","Failed Renaming %s to %s. ", old_path, move_path);

	PERFINFO_AUTO_STOP();
}

//Destroy container data on the main thread.
void mt_objDestroyContainerDispatch(void *user_data, void *data, WTCmdPacket *packet)
{
	ContainerUpdate *conup = (ContainerUpdate*)data;
	PERFINFO_AUTO_START_FUNC();
	if (conup && conup->container != NULL)
	{
		objDestroyContainer((Container *)conup->container);
	}
	PERFINFO_AUTO_STOP();
}

void objSetContainerIgnoreSource(bool ignore)
{
	gContainerSource.ignoreContainerSource = ignore;
}

void objSetContainerForceSnapshot(bool forceSnapshot)
{
	gContainerSource.forceSnapshot = forceSnapshot;
}

static void objSaveContainerSourceInfo(HogFile *the_hog)
{	
	if (!gContainerSource.useHogs)
	{
		char fileName[MAX_PATH];
		sprintf(fileName,"%s/%s",gContainerSource.sourcePath,CONTAINER_INFO_FILE);

		if (!ParserWriteTextFile(fileName,parse_ContainerRepositoryInfo, gContainerSource.sourceInfo, 0, 0))
		{		
			return;
		}
	}
	else
	{
		gContainerSource.needsRotate = true;
		wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINERINFO, &the_hog, sizeof(HogFile*));
	}
	return;
}

static bool objMoveToOldFilesDir(char *filename)
{
	char *movename = addFilePrefix(filename, "old/");
	char movedir[MAX_PATH];
	strcpy(movedir, movename);
	getDirectoryName(movedir);
	makeDirectories(movedir);
	
	fprintf(fileGetStderr(), "Moving hog to old dir:%s\n", filename);
	if (fileMove(filename, movename) != 0 && isProductionMode())
	{
		AssertOrAlert("OBJECTDB_RENAME_FAILED", "Failed Moving hog to old dir:%s\n", filename);
		return false;
	}

	return true;
}


// JDRAGO - This function appears to detect if "filename" is related to gContainerSource.sourcePath, by chopping
//          gContainerSource.sourcePath into pieces and appending things like _0_ or _1_, and seeing if the 
//          resultant substring exists in "filename". 
static bool objStringIsKindOfFilename(const char *filename, const char *prefix, const char *suffix, const char *optionalExt)
{
	char fname_prefix[MAX_PATH];
	char fname_suffix[MAX_PATH];
	char *ext = FindExtensionFromFilename(gContainerSource.sourcePath);

	getFileNameNoExt(fname_prefix, gContainerSource.sourcePath);
	strcat(fname_prefix, prefix);
	fname_suffix[0] = '\0';
	strcat(fname_suffix, suffix);
	if (optionalExt && optionalExt[0])
		strcat(fname_suffix, optionalExt);
	else
		strcat(fname_suffix,ext);
	
	return (strStartsWith(getFileNameConst(filename), fname_prefix) && strEndsWith(getFileNameConst(filename), fname_suffix));
}

static bool objStringIsProcessedFilename(char *filename)
{
	return 
		objStringIsKindOfFilename(filename, MERGED_HOG_PREFIX, MERGED_HOG_SUFFIX, NULL) ||
		objStringIsKindOfFilename(filename, OLDINC_HOG_PREFIX, OLDINC_HOG_SUFFIX, NULL);
}

static bool objStringIsBadStopFilename(char *filename)
{
	return 
		objStringIsKindOfFilename(filename, BADSTOP_HOG_PREFIX, BADSTOP_HOG_SUFFIX, NULL);
}

static bool objStringIsMergedFilename(char *filename)
{
	return objStringIsKindOfFilename(filename, MERGED_HOG_PREFIX, MERGED_HOG_SUFFIX, NULL);
}

static bool objStringIsPendingFilename(const char *filename)
{
	return objStringIsKindOfFilename(filename, INCREMENTAL_HOG_PREFIX, INCREMENTAL_HOG_SUFFIX, NULL);
}

static bool objStringIsPendingLogname(char *filename)
{
	return 
		objStringIsKindOfFilename(filename, INCREMENTAL_HOG_PREFIX, INCREMENTAL_HOG_SUFFIX, ".log") || 
		objStringIsKindOfFilename(filename, INCREMENTAL_HOG_PREFIX, INCREMENTAL_HOG_SUFFIX, ".lcg");
}

static bool objStringIsDatabaseDumpname(char *filename)
{
	return objStringIsKindOfFilename(filename, INCREMENTAL_HOG_PREFIX, INCREMENTAL_HOG_SUFFIX, ".dump");
}

static bool objStringIsCurrentFilename(char *filename)
{
	return objStringIsKindOfFilename(filename, NEWINC_HOG_PREFIX, NEWINC_HOG_SUFFIX, NULL);
}

bool objStringIsSnapshotFilename(const char *filename)
{
	return 
		objStringIsKindOfFilename(filename, SNAPSHOT_HOG_PREFIX, SNAPSHOT_HOG_SUFFIX, NULL) ||
		objStringIsKindOfFilename(filename, SNAPSHOT_HOG_PREFIX, SNAPSHOT_BACKUP_SUFFIX, NULL);
}

bool objStringIsOfflineFilename(char *filename)
{
	return 
		objStringIsKindOfFilename(filename, OFFLINE_HOG_PREFIX, OFFLINE_HOG_SUFFIX, NULL);
}

bool objStringIsTempOfflineFilename(char *filename)
{
	return 
		objStringIsKindOfFilename(filename, OFFLINE_HOG_PREFIX, OFFLINE_HOG_SUFFIX, ".temp.hogg");
}

static void objCleanupBadStopFile(char *fdir, int fdir_size, const char *name)
{
	strcat_s(SAFESTR2(fdir), "/");
	strcat_s(SAFESTR2(fdir), name);
	objMoveToOldFilesDir(fdir);
}

static void objCleanupProcessedFile(char *fdir, int fdir_size, const char *name)
{
	bool oldlikecheese = true;
	HogFile *the_hog;
	HogFileIndex hfi;
	strcat_s(SAFESTR2(fdir), "/");
	strcat_s(SAFESTR2(fdir), name);

	the_hog = hogFileReadEx(fdir,NULL,PIGERR_QUIET,NULL,HOG_NOCREATE|HOG_NO_STRING_CACHE, DB_HOG_DATALIST_JOURNAL_SIZE);
	if (!the_hog)
		hfi = HOG_INVALID_INDEX;
	else
		hfi = hogFileFind(the_hog, CONTAINER_INFO_FILE);
	if (hfi != HOG_INVALID_INDEX)
	{
		int fileSize;
		char *fileData = hogFileExtract(the_hog,hfi,&fileSize, NULL);
		if (fileData) {
			ContainerRepositoryInfo *hogInfo = StructCreate(parse_ContainerRepositoryInfo);
			if (ParserReadText(fileData,parse_ContainerRepositoryInfo,hogInfo, 0))
			{
				oldlikecheese = (gContainerSource.scanSnapshotTime && hogInfo->iCurrentTimeStamp < (int)(gContainerSource.scanSnapshotTime) - (gContainerSource.incrementalHoursToKeep * 3600));
			}
			free(fileData);
			StructDestroy(parse_ContainerRepositoryInfo, hogInfo);
		}
	}
	if (the_hog)
		hogFileDestroy(the_hog, false /*JE: Unsure if this is leaking, leaving as it was before to be safe (might leak)*/);
	if (oldlikecheese) 
	{
		objMoveToOldFilesDir(fdir);
	}
}

//Takes a string with a pending incremental filename in it and converts it to a merged filename.
static void objConvertToMergedFilename(char *pendingFilename)
{
	char *filename = pendingFilename;
	char *replace;
	replace = strstri(pendingFilename, INCREMENTAL_HOG_PREFIX);
	if (!replace) return;
	memcpy(replace, MERGED_HOG_PREFIX, sizeof(MERGED_HOG_PREFIX)-1);
	replace = strstri(pendingFilename, INCREMENTAL_HOG_SUFFIX);
	memcpy(replace, MERGED_HOG_SUFFIX, sizeof(MERGED_HOG_SUFFIX)-1);
}

//Takes a string with a current incremental filename and converts it to a badstop filename.
static void objConvertToBadstopFilename(char *currentFilename)
{
	char *filename = currentFilename;
	char *replace;
	replace = strstri(currentFilename, NEWINC_HOG_PREFIX);
	if (!replace) return;
	memcpy(replace, BADSTOP_HOG_PREFIX, sizeof(BADSTOP_HOG_PREFIX)-1);
	replace = strstri(currentFilename, NEWINC_HOG_SUFFIX);
	memcpy(replace, BADSTOP_HOG_SUFFIX, sizeof(BADSTOP_HOG_SUFFIX)-1);
}

//Takes a string with a pending incremental filename in it and converts it to a skipped filename.
static void objConvertToSkippedFilename(char *pendingFilename)
{
	char *filename = pendingFilename;
	char *replace;
	replace = strstri(pendingFilename, INCREMENTAL_HOG_PREFIX);
	if (!replace) return;
	memcpy(replace, OLDINC_HOG_PREFIX, sizeof(OLDINC_HOG_PREFIX)-1);
	replace = strstri(pendingFilename, INCREMENTAL_HOG_SUFFIX);
	memcpy(replace, OLDINC_HOG_SUFFIX, sizeof(OLDINC_HOG_SUFFIX)-1);
}

//Takes a string with a current incremental filename in it and converts it to a pending filename.
static void objConvertToPendingFilename(char *incrementalFilename)
{	
	char *filename = incrementalFilename;
	char *replace;
	replace = strstri(incrementalFilename, NEWINC_HOG_PREFIX);
	if (!replace) return;
	memcpy(replace, INCREMENTAL_HOG_PREFIX, sizeof(INCREMENTAL_HOG_PREFIX)-1);
	replace = strstri(incrementalFilename, NEWINC_HOG_SUFFIX);
	memcpy(replace, INCREMENTAL_HOG_SUFFIX, sizeof(INCREMENTAL_HOG_SUFFIX)-1);
}

static void objMakeIncrementalFilename(char **estr, U32 atTime, char *optionalExt)
{
	char *fname_orig = gContainerSource.sourcePath;
	char *dotStart = FindExtensionFromFilename(fname_orig);
	char timestring[MAX_PATH];
	int namelen = dotStart - fname_orig;

	if (gContainerSource.sourcePath)
	{
		if (namelen <=0)
			namelen = (int)strlen(fname_orig);

		estrAppendUnescapedCount(estr, fname_orig, namelen);
	}
	else
	{
		estrConcatf(estr, "db");
	}
	
	timeMakeFilenameDateStringFromSecondsSince2000(timestring, atTime);

	estrConcatf(estr,"%s%s%s%s%s",NEWINC_HOG_PREFIX, timestring, NEWINC_HOG_SUFFIX,(optionalExt?".":""), (optionalExt?optionalExt:dotStart));
}

static void objMakeSnapshotFilename(char **estr, U32 atTime, char *optionalExt)
{
	char *fname_orig = gContainerSource.sourcePath;
	char *dotStart = FindExtensionFromFilename(fname_orig);
	char timestring[MAX_PATH];
	int namelen = dotStart - fname_orig;

	if (gContainerSource.sourcePath)
	{
		if (namelen <=0)
			namelen = (int)strlen(fname_orig);

		estrAppendUnescapedCount(estr, fname_orig, namelen);
	}
	else
	{
		estrConcatf(estr, "db");
	}

	timeMakeFilenameDateStringFromSecondsSince2000(timestring, atTime);

	estrConcatf(estr,"%s%s%s%s%s", SNAPSHOT_HOG_PREFIX, timestring, SNAPSHOT_HOG_SUFFIX,(optionalExt?".":""), (optionalExt?optionalExt:dotStart));
}

HogFile* objOpenIncrementalHogFile(char *hogfile)
{
	HogFile *newHog = NULL;

	assert(gContainerSource.validSource && gContainerSource.useHogs);
	
	//create the new hog file
	newHog = hogFileReadEx(hogfile,NULL,PIGERR_ASSERT,NULL,HOG_MUST_BE_WRITABLE|HOG_NO_STRING_CACHE, DB_HOG_DATALIST_JOURNAL_SIZE);

	if (newHog == NULL)
		return NULL;
	else
	{
		hogFileSetSingleAppMode(newHog, true);
		hogFileReserveFiles(newHog, 11000);

		return newHog;
	}
}

// Set a hog file to load containers from
bool objSetContainerSourceToHogFile(const char *hogfile, U32 incrementalMinutes, 
									ContainerIOCB rolloverCB, ContainerIOCB shutdownCB)
{
	strcpy(gContainerSource.sourcePath,hogfile);

	if (!incrementalMinutes)
	{
		// If we are not using the incremental flow, this file must be writable
		// Only ticket tracker should be using this flow
		gContainerSource.fullHog = hogFileReadEx(hogfile,NULL,PIGERR_ASSERT,NULL,HOG_DEFAULT|HOG_NO_STRING_CACHE|HOG_MUST_BE_WRITABLE, DB_HOG_DATALIST_JOURNAL_SIZE);
		hogFileSetSingleAppMode(gContainerSource.fullHog,true);
		hogFileReserveFiles(gContainerSource.fullHog,1000000);
		if (!gContainerSource.fullHog)
		{
			gContainerSource.validSource = false;
		}
		else
		{
			gContainerSource.validSource = true;
		}
	}
	else
	{
		gContainerSource.validSource = true;
	}
	gContainerSource.sourceInfo = StructCreate(parse_ContainerRepositoryInfo);
	gContainerSource.sourceInfo->iBaseContainerID = gBaseContainerID;
	gContainerSource.sourceInfo->iMaxContainerID = gMaxContainerID;
	gContainerSource.useHogs = true;
	gContainerSource.incrementalMinutes = incrementalMinutes;
	gContainerSource.incrementalRolloverCB = rolloverCB;
	gContainerSource.incrementalShutdownCB = shutdownCB;

	gContainerSource.forceUnpackThreshold = timeSecondsSince2000() - 
		gContainerSource.containerPreloadThresholdHours * SECONDS_PER_HOUR;

	if (gContainerSource.thread_initialized == false)
		objInitializeHogThread();

	return gContainerSource.validSource;
}

// Gets the path of the source directory of hog file
const char * objGetContainerSourcePath(void)
{
	if (gContainerSource.validSource)
		return gContainerSource.sourcePath;
	return NULL;
}

// Set a directory to load containers from
bool objSetContainerSourceToDirectory(const char *directory)
{
	strcpy(gContainerSource.sourcePath,directory);
	gContainerSource.validSource = makeDirectories(directory);
	gContainerSource.sourceInfo = StructCreate(parse_ContainerRepositoryInfo);
	gContainerSource.useHogs = 0;
	gContainerSource.incrementalMinutes = 0;
	gContainerSource.incrementalRolloverCB = NULL;
	gContainerSource.incrementalShutdownCB = NULL;
	gContainerSource.sourceInfo->iBaseContainerID = gBaseContainerID;
	gContainerSource.sourceInfo->iMaxContainerID = gMaxContainerID;

	if (gContainerSource.thread_initialized == false)
		objInitializeHogThread();

	return gContainerSource.validSource;
}

void objSetDumpWebDataMode(char* dumpfile)
{
	if (gContainerSource.dumpWebData) free(gContainerSource.dumpWebData);
	gContainerSource.dumpWebData = strdup(dumpfile);
}

void objSetSnapshotMode(bool snapshotMode)
{
	gContainerSource.createSnapshot = snapshotMode;
}

void objSetDumpMode(int mode)
{
	gContainerSource.dumpload = mode;
}

void objSetDumpType(GlobalType type)
{
	gContainerSource.dumpContainerType = type;
}

void objSetDumpLoadedContainerCallback(DumpLoadedContainerCallback cb)
{
	gContainerSource.dumpLoadedContainerCallback = cb;
}

void objSetPreSaveCallback(PreSaveCallback cb)
{
	gContainerSource.preSaveCallback = cb;
}

void objSetMultiThreadedLoadThreads(int count)
{
	gContainerSource.loadThreads = count;
}

void objSetPurgeHogOnFailedContainer(bool purgehog)
{
	gContainerSource.purgeHogOnFailedContainer = purgehog;
}

bool objCloseContainerSource(void)
{
	if (!gContainerSource.validSource)
	{
		return false;
	}

	objSaveModifiedContainersThreaded(false);
	objFlushContainers();
	objCloseAllWorkingFiles();

	gContainerSource.validSource = false;

	return true;
}

void objCloseHogOnHogThread(HogFile *the_hog)
{
	wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDCLOSEHOG, &the_hog, sizeof(HogFile*));
}

void objContainerLoadingFinished(void)
{
	HogFile *the_hog = NULL;
	if (!gContainerSource.validSource)
	{
		return;
	}
	if ((the_hog = gContainerSource.fullHog) && gContainerSource.incrementalMinutes)
	{
		objInitIncrementalHogRotation();
		gContainerSource.fullHog = NULL;
		hogFileDestroy(the_hog, false /*JE: Unsure if this is leaking, leaving as it was before to be safe (might leak)*/);
	}
}

bool objCloseIncrementalHogFile(void)
{
	HogFile *hog_to_destroy;
	if (!gContainerSource.incrementalHog)
	{
		return false;
	}

	EnterCriticalSection(&gContainerSource.hog_access);
	do {
		char movepath[MAX_PATH];
		char frompath[MAX_PATH];
		strcpy(movepath, hogFileGetArchiveFileName(gContainerSource.incrementalHog));
		strcpy(frompath, hogFileGetArchiveFileName(gContainerSource.incrementalHog));

		hog_to_destroy = gContainerSource.incrementalHog;
		gContainerSource.incrementalHog = NULL;
		sprintf(gContainerSource.incrementalPath,"");	

		hogFileDestroy(hog_to_destroy, false /*JE: Unsure if this is leaking, leaving as it was before to be safe (might leak)*/);

		objConvertToPendingFilename(movepath);
		CONTAINER_LOGPRINT("Renaming %s to %s. \n", frompath, movepath);
		if(fileMove(frompath, movepath) != 0 && isProductionMode()) AssertOrAlert("OBJECTDB_RENAME_FAILED","Failed Renaming %s to %s. ", frompath, movepath);
	} while(false);
	LeaveCriticalSection(&gContainerSource.hog_access);

	return true;
}

typedef struct DiskLoadingInfo
{
	ContainerRef key;
	HogFileIndex hogIndex;
	U64 offset;
	U32 fileSize;
	U32 bytesCompressed;
} DiskLoadingInfo;

HogFile* diskLoadingHog;
StashTable diskLoadingTable;

void diskLoadingAddToTable(ContainerLoadRequest* request, const char* filename)
{
	DiskLoadingInfo* info;
	int added;
	devassert(request->the_hog == diskLoadingHog);
	info = callocStruct(DiskLoadingInfo);
	info->key.containerType = request->containerType;
	info->key.containerID = request->containerID;
	info->hogIndex = request->hog_index;
	info->offset = request->hog_file_offset;
	info->fileSize = request->fileSize;
	info->bytesCompressed = request->bytesCompressed;
	if(!(added = stashAddPointer(diskLoadingTable, &info->key, info, false)))
	{
		devassertmsgf(added, "Failed to add %s[%d] (filename %s) to the diskLoadingTable, is there a duplicate?", GlobalTypeToName(info->key.containerType), info->key.containerID, filename);
		CONTAINER_LOGPRINT("Failed to add %s[%d] (filename %s) to the diskLoadingTable, is there a duplicate?\n", GlobalTypeToName(info->key.containerType), info->key.containerID, filename);
	}
}

void diskLoadingRemoveFromTable(ContainerLoadRequest* request, const char* filename)
{
	ContainerRef key;
	DiskLoadingInfo* info;
	key.containerType = request->containerType;
	key.containerID = request->containerID;

	if(diskLoadingTable && stashRemovePointer(diskLoadingTable, &key, &info))
		free(info);
}

bool diskLoadingHogFileCB(HogFile* hog, HogFileIndex hogIndex, const char* filename, void* userdata)
{
	U64 seq = 0;
	bool deleted;
	ContainerLoadRequest request = {0};

	if(objParseContainerFilename(filename, &request.containerType, &request.containerID, &seq, &deleted))
	{
		request.the_hog = hog;
		request.hog_index = hogIndex;
		hogFileGetSizes(hog, hogIndex, &request.fileSize, &request.bytesCompressed);
		request.hog_file_offset = hogFileGetOffset(hog, hogIndex);
		diskLoadingAddToTable(&request, filename);
	}

	return true;
}

void diskLoadingSetDiskLoadingHog(HogFile* hog, int scan)
{
	if(diskLoadingHog)
		stashTableClear(diskLoadingTable);
	else
		diskLoadingTable = stashTableCreateFixedSize(4096, sizeof(ContainerRef));

	diskLoadingHog = hog;
	if(scan)
		hogScanAllFiles(hog, diskLoadingHogFileCB, NULL);
}

bool diskLoadingFindHogLocation(ContainerLoadRequest* request)
{
	ContainerRef key;
	DiskLoadingInfo* info;
	key.containerType = request->containerType;
	key.containerID = request->containerID;

	if(!stashFindPointer(diskLoadingTable, &key, &info))
	{
		CONTAINER_LOGPRINT("Failed to find %s[%d] in the diskLoadingHog lookup table\n", GlobalTypeToName(request->containerType), request->containerID);
		return false;
	}

	request->the_hog = diskLoadingHog;
	request->hog_index = info->hogIndex;
	request->fileSize = info->fileSize;
	request->bytesCompressed = info->bytesCompressed;
	request->hog_file_offset = info->offset;

	return true;
}

bool objLoadFileData(ContainerLoadRequest* request)
{
	if (!gContainerSource.useHogs)
	{
		request->fileData = fileAlloc(request->fileName,&request->fileSize);
		request->bytesCompressed = 0;
	}
	else
	{
		if(!request->the_hog)
		{
			devassertmsg(diskLoadingMode, "This codepath only makes sense if you are trying to load from disk during normal operation");
			if(!diskLoadingFindHogLocation(request))
				return false;
			devassert(request->the_hog && request->hog_index != HOG_INVALID_INDEX);
		}

		if (request->hog_index == HOG_INVALID_INDEX )
		{			
			if (!request->the_hog)
			{
				log_printf(LOG_CONTAINER, "Failed to read container %s[%d]: No Hog Available", GlobalTypeToName(request->containerType), request->containerID);
				return false;
			}
			request->hog_index = hogFileFind(request->the_hog,request->fileName);
		}

		if (request->hog_index == HOG_INVALID_INDEX)
		{
			return false;
		}
		else
		{
			U32 bytesRead;
			int special_heap = 0;

			if(request->lazyLoad)
				special_heap = CRYPTIC_CONTAINER_HEAP;

			PERFINFO_AUTO_START("conLoadHogExtract",1);
			devassertmsg(request->fileSize, "Should have a file size at this point");
			//hogFileGetSizes(request->the_hog, request->hog_index, &request->fileSize, &request->bytesCompressed);
			if (request->bytesCompressed)
			{
				request->fileData = hogFileExtractCompressedEx(request->the_hog, request->hog_index,&bytesRead, request->hog_file_offset, request->fileSize, request->bytesCompressed, special_heap);	
			}
			else
			{
				request->fileData = hogFileExtractEx(request->the_hog,request->hog_index,&bytesRead, NULL, request->hog_file_offset, request->fileSize, request->bytesCompressed, special_heap);
			}
			PERFINFO_AUTO_STOP();
		}
	}

	return true;
}

bool objLoadContainer(ContainerLoadRequest *request)
{
	ContainerSchema *schema = objFindContainerSchema(request->containerType);
	Container *newContainer;

	ContainerHeaderInfo *containerHeader = NULL;
	ObjectIndexHeader *header = NULL;
	const U8 *headerPtr;
	U32 headerSize;
	bool parse = false;
	bool forceUnpack = false;

	if (!schema || !gContainerSource.validSource)
	{
		log_printf(LOG_CONTAINER, "Failed to read container %s[%d]: Invalid State", GlobalTypeToName(request->containerType), request->containerID);
		goto failed_early;
	}

	if (!request->containerID)
	{
		log_printf(LOG_CONTAINER, "Failed to read container %s[0]: Invalid ID 0", GlobalTypeToName(request->containerType));
		goto failed_early;
	}

	PERFINFO_AUTO_START_FUNC();

	verbose_printf("Reading Container %s[%d]\n",GlobalTypeToName(request->containerType),request->containerID);

	if(!request->fileData && !request->skipFileData)
	{
		if(!objLoadFileData(request))
			goto failed;
	}

	if (!request->skipFileData && (!request->fileData || !request->fileSize))
	{
		log_printf(LOG_CONTAINER, "Failed to read container %s[%d]: No Data On Disk", GlobalTypeToName(request->containerType), request->containerID);
		goto failed;
	}

	headerPtr = hogFileGetHeaderData(request->the_hog, request->hog_index, &headerSize); 

	parse = !request->skipFileData && (!request->lazyLoad || request->useHeader && !headerSize);

	if(headerSize)
	{
		containerHeader = objContainerReadHeader(headerPtr, headerSize);

		if(!parse && (!containerHeader || (containerHeader->objectIndexHeaderType != OBJECT_INDEX_HEADER_TYPE_NONE && !containerHeader->objectHeader)))
		{
			parse = true;
		}
	}


	newContainer = objCreateContainer(schema);
	newContainer->containerID = request->containerID;

	request->newContainer = newContainer;
	request->deletedTime = containerHeader ? containerHeader->deletedTime : 0;
	newContainer->header = containerHeader && request->useHeader ? containerHeader->objectHeader : NULL;

	if(containerHeader) free(containerHeader);

	newContainer->fileData = request->fileData;
	request->fileData = NULL; // now owned by container
	newContainer->fileSize = request->fileSize;
	newContainer->fileSizeHint = newContainer->fileSize;
	newContainer->bytesCompressed = request->bytesCompressed;
	newContainer->headerSize = headerSize;
	if(newContainer->headerSize)
	{
		newContainer->rawHeader = malloc(headerSize);
		memcpy(newContainer->rawHeader, headerPtr, headerSize);
	}
	newContainer->checksum = hogFileGetFileChecksum(request->the_hog, request->hog_index);
	newContainer->oldFileNoDevassert = hogFileGetFileTimestamp(request->the_hog, request->hog_index) < 333961199;

	if (!request->skipFileData && gContainerSource.forceUnpackThreshold && newContainer->header)
		forceUnpack = newContainer->header->lastPlayedTime > gContainerSource.forceUnpackThreshold;

	if (parse || forceUnpack)
	{
		int keepData = (ForceKeepLazyLoadFileData(request->containerType)) || (request->useHeader && parse && gContainerSource.unloadOnHeaderCreate);
		if (objUnpackContainer(schema, newContainer, keepData, false, false))
		{
			ContainerID key;

			//Check if the container was deleted.
			objGetKeyInt(schema->classParse,newContainer->containerData,&key);
			if (key == 0)
			{	//The container was marked for deletion in the hog. 
				request->removeContainer = true;
			}
		}
		else
		{
			log_printf(LOG_CONTAINER, "Failed to read container %s[%d]: Bad TextParser Data", GlobalTypeToName(request->containerType), request->containerID);
			goto failed;
		}
	}
	else if (newContainer->header && !newContainer->header->containerId)
		request->removeContainer = true;

	if (request->useHeader && parse)
	{
		if(!objGenerateHeader(newContainer, schema))
		{
			log_printf(LOG_CONTAINER, "Failed to read container %s[%d]: Failed to parse header", GlobalTypeToName(request->containerType), request->containerID);
			goto failed;
		}

		if (newContainer->header)
		{
			request->headerAdded = true;

			if (gContainerSource.unloadOnHeaderCreate && newContainer->header &&
				newContainer->header->lastPlayedTime < gContainerSource.forceUnpackThreshold)
			{
				objDeInitContainerObject(newContainer->containerSchema,newContainer->containerData);
				objDestroyContainerObject(newContainer->containerSchema,newContainer->containerData);
				request->newContainer->containerData = NULL;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return true;

failed:
	PERFINFO_AUTO_STOP();
failed_early:
	request->invalid = true;
	if (request->fileData)
		SAFE_FREE(request->fileData);
	return false;
}

typedef struct objLoadStats
{
	int invalid;
	int removed;
	int success;
	int unpacked;
} objLoadStats;

static objLoadStats sLoadStats[GLOBALTYPE_MAXTYPES];

int objCountPreloadedContainersOfType(GlobalType eType)
{
	if (eType >= GLOBALTYPE_MAXTYPES) return 0;
	return sLoadStats[eType].unpacked;
}

static StashTable DumpedContainerStash = NULL;
bool AlreadyDumpedContainer(GlobalType type, ContainerID id)
{
	bool retval = false;
	char *key = NULL;
	int result;
	if(!DumpedContainerStash)
		return false;

	estrPrintf(&key, "%s[%u]", GlobalTypeToName(type), id);

	retval = stashFindInt(DumpedContainerStash, key, &result);
	estrDestroy(&key);
	return retval;
}

void RegisterDumpedContainer(GlobalType type, ContainerID id)
{
	char *key = NULL;
	if(!DumpedContainerStash)
	{
		DumpedContainerStash = stashTableCreateWithStringKeys(8, StashDefault);
	}

	estrPrintf(&key, "%s[%u]", GlobalTypeToName(type), id);

	stashAddInt(DumpedContainerStash, key, 1, 0);
}

AUTO_COMMAND;
void objPrintLoadStats()
{
	fprintf(fileGetStderr(), "%d successes, %d invalid, %d removed\n", sLoadStats[GLOBALTYPE_NONE].success, sLoadStats[GLOBALTYPE_NONE].invalid, sLoadStats[GLOBALTYPE_NONE].removed);
	fprintf(fileGetStderr(), "Unpacked %d containers (%2.0f%%)\n", sLoadStats[GLOBALTYPE_NONE].unpacked, 100 * (float)sLoadStats[GLOBALTYPE_NONE].unpacked / MAX(sLoadStats[GLOBALTYPE_NONE].success, 1));
}

static bool alertedForEmptyInvalidRequest = false;

static bool gbUseCrazyBigDefaultContainerStoreSize = 0;
AUTO_CMD_INT(gbUseCrazyBigDefaultContainerStoreSize, UseCrazyBigDefaultContainerStoreSize) ACMD_COMMANDLINE;

static void objProcessFinishedLoadRequest(ContainerLoadRequest* request)
{
	PERFINFO_AUTO_START_FUNC();
	
	if (request->invalid)
	{
		++sLoadStats[GLOBALTYPE_NONE].invalid;
		++sLoadStats[request->containerType].invalid;
		if(request->newContainer)
		{
			objRegisterInvalidContainer(request->newContainer);
			objDestroyContainer(request->newContainer);
		}
		else if(!alertedForEmptyInvalidRequest)
		{
			TriggerAlertf("OBJECTDB.DATA_CORRUPTION",ALERTLEVEL_CRITICAL,ALERTCATEGORY_NETOPS,0, 0, 0, GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0,
				"ObjectDB failed early when loading some containers. Check the container logs for details.");
			alertedForEmptyInvalidRequest = true;
		}
	}
	else if (request->removeContainer)
	{
		Container *con = objGetContainerEx(request->containerType, request->containerID, false, false, true);
		++sLoadStats[GLOBALTYPE_NONE].removed;
		++sLoadStats[request->containerType].removed;
		if (con != NULL)
		{
			objRemoveContainerFromRepositoryAndHog(request->containerType, request->containerID, false);
			// The container will be unlocked inside objRemoveContainerFromRepositoryAndHog
		}
		else if (con = objGetDeletedContainerEx(request->containerType, request->containerID, false, false, true))
		{
			objRemoveDeletedContainerFromRepositoryAndHog(request->containerType, request->containerID, false);
			// The container will be unlocked inside objRemoveDeletedContainerFromRepositoryAndHog
		}
		else if(gContainerSource.useHogs)
		{
			// This line of code was added to help clean up incompletely deleted containers in the hogg.
			// Because of the delayed call to delete this from the hogg, it can create a race condition
			// that allows a container being restored to be incorrectly deleted at load time. Fixing
			// that problem without causing other thread/locking based issue is complicated, so
			// I am removing this for now.
			// objRemoveContainerFromHog(&request->newContainer);
		}
		// Throw out the temporary object, since we aren't planning on adding it to a store
		objDestroyContainer(request->newContainer);
	}
	else if (gContainerSource.dumpLoadedContainerCallback)
	{
		//success, callback set. fire callback, then cleanup container
		if(!AlreadyDumpedContainer(request->containerType, request->containerID))
		{
			gContainerSource.dumpLoadedContainerCallback(request->newContainer, hogFileGetFileTimestamp(request->the_hog, request->hog_index));
			RegisterDumpedContainer(request->containerType, request->containerID);
		}

		objDestroyContainer(request->newContainer);
	}
	else
	{	//success, add it
		ContainerSchema *schema = request->newContainer->containerSchema;
		ContainerStore *store = objFindContainerStoreFromType(request->containerType);
		int index = -1;
		bool indexIsDeleted = false;
		bool markModified = false;
		bool addBlocked = false;

		++sLoadStats[GLOBALTYPE_NONE].success;
		++sLoadStats[request->containerType].success;

		if (request->newContainer->containerData || !request->newContainer->fileData)
		{
			++sLoadStats[GLOBALTYPE_NONE].unpacked;
			++sLoadStats[request->containerType].unpacked;
		}

		assert(schema);
		
		if (!store)
		{
			U32 storesize = isProductionMode() ? (gbUseCrazyBigDefaultContainerStoreSize ? 1 << 22 : 1024) : 100;
			store = objCreateContainerStoreSize(schema, storesize);
		}
		else
		{
			// only hold the lock briefly since we don't actually access the array using the index
			objLockContainerStore_ReadOnly(store);
			index = objContainerStoreFindIndex(store, request->containerID);
			objUnlockContainerStore_ReadOnly(store);

			if(index < 0)
			{
				// only hold the lock briefly since we don't actually access the array using the index
				objLockContainerStoreDeleted_ReadOnly(store);
				index = objContainerStoreFindDeletedIndex(store, request->containerID);
				objUnlockContainerStoreDeleted_ReadOnly(store);
				indexIsDeleted = true;
			}
		}

		if (index >= 0)
		{
			if (request->overwrite)
			{
				verbose_printf(" Container exists. overwriting Container %s[%d].\n", GlobalTypeToName(request->containerType), request->containerID);
				if(indexIsDeleted)
					objRemoveDeletedContainerFromRepositoryAndHog(request->containerType, request->containerID, false);
				else
					objRemoveContainerFromRepositoryAndHog(request->containerType, request->containerID, false);
			}
			else
			{
				//container exists, don't overwrite
				Errorf("Trying to load an already existing container %s[%d] without overwrite flag.",
					GlobalTypeToName(request->containerType), request->containerID);
				objDestroyContainer(request->newContainer);
				addBlocked = true;
			}
		}
		else
			verbose_printf(" Adding Container %s[%d] to store.\n", GlobalTypeToName(request->containerType), request->containerID);

		if(!addBlocked)
		{
			if (request->headerAdded)
			{
				if (store->saveAllHeaders)
					markModified = true;
				else
				{
					ContainerRef *ref = StructCreate(parse_ContainerRef);
					ref->containerID = request->newContainer->containerID;
					ref->containerType = request->newContainer->containerType;
					eaPush(&gContainerSource.throttledSaveQueue, ref);
				}
			}

			if (!objAddToContainerStore(store,request->newContainer,request->containerID,markModified,request->deletedTime))
			{
				log_printf(LOG_CONTAINER, "Failed to load container %s[%d]: Can't Add to ContainerStore", GlobalTypeToName(request->containerType), request->containerID);
				objRegisterInvalidContainer(request->newContainer);
				objDestroyContainer(request->newContainer);
			}
		}
	}
	InterlockedIncrement(request->completeCount);

	PERFINFO_AUTO_STOP();
}

// Record statistics about container loading times.
static void objRecordLoadStats(ContainerLoadThreadState *threadState, ContainerLoadRequest *request)
{
	U64 now;

	PERFINFO_AUTO_START_FUNC();

	// Don't record data if disabled.
	if (!gbDbLoadTimeReport)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Get time.
	GET_CPU_TICKS_64(now);
	if (!threadState->uLastCpuTick)
	{
		threadState->uLastCpuTick = now;
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Resize array if necessary.
	if (ea64Size(&threadState->ppContainerLoadCpuTicks) <= request->containerType)
		ea64SetSize(&threadState->ppContainerLoadCpuTicks, request->containerType * 2);

	// Add this time to the container time.
	devassert(threadState->ppContainerLoadCpuTicks);
	threadState->ppContainerLoadCpuTicks[request->containerType] += now - threadState->uLastCpuTick;

	// Save this time.
	threadState->uLastCpuTick = now;

	PERFINFO_AUTO_STOP_FUNC();
}

Container *objLoadTemporaryContainer(GlobalType containerType, ContainerID containerID, HogFile* the_hog, HogFileIndex hogIndex, bool autoLoadArchivedSchema)
{
	char *fileData;
	int fileSize = 0;
	char fileName[MAX_PATH];
	char fakeFileName[MAX_PATH];
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	ContainerSchema *schema = objFindContainerSchema(containerType);
	Container *newContainer;
	const U8 *headerPtr;
	U32 headerSize = 0;

	fileName[0] = 0;

	if (!containerID)
	{
		log_printf(LOG_CONTAINER, "Failed to read container %s[0]: Invalid ID 0", GlobalTypeToName(containerType));
		return NULL;
	}

	PERFINFO_AUTO_START("objLoadTemporaryContainer",1);

	if(!schema && autoLoadArchivedSchema)
	{
		objLoadSchemaSource(containerType, the_hog);
		schema = objFindContainerSchema(containerType);
	}

	if(!schema)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	if (hogIndex == HOG_INVALID_INDEX )
	{			
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	fileData = hogFileExtract(the_hog,hogIndex,&fileSize, NULL);
	if (fileSize == 0)
	{
		log_printf(LOG_CONTAINER, "Container %s[%d] is empty: No Data On Disk", GlobalTypeToName(containerType), containerID);
		return NULL;
	}

	if (!fileData || !fileData[0])
	{
		log_printf(LOG_CONTAINER, "Failed to read container %s[%d]: No Data On Disk", GlobalTypeToName(containerType), containerID);
		PERFINFO_AUTO_STOP();
		if (fileData) free(fileData);
		return NULL;
	}

	headerPtr = hogFileGetHeaderData(the_hog, hogIndex, &headerSize);

	newContainer = objCreateContainer(schema);
	newContainer->containerData = objCreateContainerObject(schema, "newcontainer created in objLoadTemporaryContainer");
	newContainer->containerID = containerID;

	sprintf(fakeFileName, "%s[%d]", GlobalTypeToName(containerType), containerID);

	if (ParserReadTextForFile(fileData + headerSize, fakeFileName, schema->classParse,newContainer->containerData, 0))
	{
		free(fileData);
		PERFINFO_AUTO_STOP();
		return newContainer;
	}
	else
	{
		objDestroyContainer(newContainer);
		free(fileData);
		PERFINFO_AUTO_STOP();
		return NULL;
	}
}

static int sDestroyedContainers[GLOBALTYPE_MAXTYPES];

int objCountDestroyedContainersOfType(GlobalType eType)
{
	if (eType >= GLOBALTYPE_MAXTYPES) return 0;
	return sDestroyedContainers[eType];
}

#define objPermanentlyDeleteContainer(container) objDispatchContainerUpdate(container, NULL, true)

static int objDispatchContainerUpdate(Container *container, HogFile* the_hog, bool bIsDelete)
{
	bool retval = true;
	ContainerUpdate conup = {0};

	if (gContainerSource.createSnapshot || bIsDelete)
	{
		conup.container = container;
	}
	else
	{
		conup.container = objContainerCopy(container, true, "copy created in objDispatchContainerUpdate");
	}
	conup.bIsDelete = bIsDelete;
	conup.hog = the_hog;
	conup.serviceTime = objServiceTime();
	conup.containerType = container->containerType;
	conup.containerID = container->containerID;

	if (bIsDelete)
	{
		//Taking ownership to save a destroy
		conup.container->isTemporary = !!true;
		// if gContainerSource.useHogs is not true, does this actually remove the container?
		if(gContainerSource.useHogs)
		{
			//set the containerID to be saved to 0 to mark it as deleted.
			ParseTable *tpi = container->containerSchema->classParse;
			TextParserResult ok = PARSERESULT_SUCCESS;
			int keycolumn;
			verbose_printf("Marking Container %s[%d] for deletion.\n",GlobalTypeToName(container->containerType),container->containerID);

			//set the ID to 0
			if ((keycolumn = ParserGetTableKeyColumn(tpi))  < 0)
			{
				objDecrementServiceTime();
				AssertOrAlert("OBJECTDB_HOG_DELETE_FAILED", "Could not find container key column for type %s", GlobalTypeToName(container->containerType));
				retval = false;
			}
			else
			{
				if(!conup.container->containerData)
				{
					ContainerStore *store = objFindContainerStoreFromType(container->containerType);
					if(store)
						objUnpackContainer(store->containerSchema, conup.container, ForceKeepLazyLoadFileData(conup.container->containerType), false, false);
				}

				if(conup.container->containerData)
					TokenStoreSetInt_inline(tpi, &tpi[keycolumn], keycolumn, conup.container->containerData, 0, 0, &ok, NULL);		
				else
					ok = PARSERESULT_ERROR;

				if (conup.container->header)
					conup.container->header->containerId = 0;
				if (ok != PARSERESULT_SUCCESS)
				{
					objDecrementServiceTime();
					AssertOrAlert("OBJECTDB_HOG_DELETE_FAILED", "Failed to set container key to 0, attempting to delete %s[%u]", GlobalTypeToName(container->containerType), container->containerID);
					retval = false;
				}
			}
		}

		++sDestroyedContainers[GLOBALTYPE_NONE];
		++sDestroyedContainers[container->containerType];
	}

	gContainerSource.needsRotate = true;		
	wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINER, &conup, sizeof(ContainerUpdate));

	return retval;
}

bool objDeleteInvalidContainer(GlobalType containerType, ContainerID containerID)
{
	Container *removed;
	int i;

	if (objDoesContainerExist(containerType, containerID))
	{
		return false;
	}

	objLockGlobalContainerRepository();
	for (i = eaSize(&gContainerRepository.invalidContainers) - 1; i>= 0; i--)
	{
		removed = gContainerRepository.invalidContainers[i];
		if (removed->containerType == containerType && removed->containerID == containerID)
		{
			eaRemove(&gContainerRepository.invalidContainers, i);
			objUnlockGlobalContainerRepository();
			return objPermanentlyDeleteContainer(removed);
		}
	}
	objUnlockGlobalContainerRepository();
	return false;
}

// Doesn't need to lock the repository because its callers do.
int objPermanentlyDeleteRemovedContainers(void)
{
	int i;
	PERFINFO_AUTO_START("objPermanentlyDeleteRemovedContainers",1);
	for (i = 0; i < eaSize(&gContainerRepository.removedContainersLocking.trackedContainers); i++)
	{
		TrackedContainer *trackedCon = gContainerRepository.removedContainersLocking.trackedContainers[i];
		objPermanentlyDeleteContainer(trackedCon->container);
	}
	ClearTrackedContainerArray(&gContainerRepository.removedContainersLocking);
	PERFINFO_AUTO_STOP();
	return i;
}


bool objImportContainer(const char *fileName, U32 *containerID, GlobalType *containerType, char **resultString)
{
	char fileDetails[MAX_PATH];
	char *typeext;
	ContainerSchema *schema;
	ContainerStore *store;
	Container *newContainer;
	char *fileData = NULL;
	int len = 0;
	bool success = true;

	if (!fileExists(fileName))
	{
		if (resultString) estrPrintf(resultString, "Could not find file: %s", fileName);
		return false;
	}
	if (!strEndsWith(fileName, ".con"))
	{
		if (resultString) estrPrintf(resultString, "File must be of type .ENTITYTYPE.con");
		return false;
	}

	strcpy(fileDetails, fileName);
	len = (int)strlen(fileDetails);

	//chop off the .con
	fileDetails[len-4] = '\0';
	typeext = FindExtensionFromFilename(fileDetails);

	if (!typeext)
	{
		if (resultString) estrPrintf(resultString, "File must be of type .ENTITYTYPE.con");
		return false;
	}

	*containerType = NameToGlobalType(typeext);

	if (!*containerType)
	{
		if (resultString) estrPrintf(resultString, "Did not recognize container type \"%s\" for file: %s", typeext, fileName);
		return false;
	}

	schema = objFindContainerSchema(*containerType);
	store = objFindContainerStoreFromType(*containerType);

	devassertmsg (schema && store, STACK_SPRINTF("Could not get container store or schema for type: %s", typeext));

	fileData = fileAlloc(fileName,&len);
	if (!fileData)
	{
		if (resultString) estrPrintf(resultString, "Error reading file: %s", fileName);
		return false;
	}

	newContainer = objCreateContainer(schema);
	newContainer->containerData = objCreateContainerObject(schema, "Creating container in objImportContainer");
	newContainer->containerID = 0;

	if (ParserReadTextForFile(fileData, fileName, schema->classParse,newContainer->containerData, 0))
	{
		if (!objAddToContainerStore(store,newContainer,0,true, 0))
		{
			if (resultString) estrPrintf(resultString, "Error importing container file: %s.\nThe container data in this file is invalid and may be corrupt.", fileName);
			objRegisterInvalidContainer(newContainer);
			objDestroyContainer(newContainer);
			success = false;
		}
		else
		{
			*containerID = newContainer->containerID;
			success = true;
		}
	}
	else
	{
		if (resultString) estrPrintf(resultString, "Error parsing container file: %s.", fileName);
		success = false;
	}

	free(fileData);
	return success;
}

//This return value is meaningless as the command is executed asynchronously.
bool objSaveContainer(Container *container)
{
	if (!container || !gContainerSource.validSource)
	{
		return false;
	}
	if (!gContainerSource.useHogs)
	{
		char fileName[MAX_PATH];
		GlobalType containerType = objGetContainerType(container);
		ContainerID containerID = objGetContainerID(container);

		sprintf(fileName,"%s/%s/%d.con",gContainerSource.sourcePath,GlobalTypeToName(containerType),containerID);

		if (!ParserWriteTextFile(fileName,container->containerSchema->classParse,container->containerData,TOK_PERSIST,0))
		{	
			log_printf(LOG_CONTAINER, "Failed to write container %s[%d]: Can't Write Data", GlobalTypeToName(container->containerType), container->containerID);
			return false;
		}
	}
	else
	{
		objDispatchContainerUpdate(container, NULL, false);
	}
	return true;
}

bool objLoadSchemaSource(GlobalType type, HogFile *the_hog)
{
	if (!type || objFindContainerSchema(type)) return false;

	if (the_hog)
	{
		char *path = objSchemaFileNameFromName(GlobalTypeToName(type));
		HogFileIndex hfi = HOG_INVALID_INDEX;
		char *fileData;
		int fileSize;

		hfi = hogFileFind(the_hog, path);
		if (hfi == HOG_INVALID_INDEX)
			return false;

		fileData = hogFileExtract(the_hog,hfi,&fileSize, NULL);

		if (fileData)
		{
			objLoadArchivedSchema(type, fileData);
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		return false;
	}
}

static bool objSaveSchemaSource(GlobalType type, HogFile *the_hog)
{
	SchemaSource *source = objFindContainerSchemaSource(type);
	char *estr = NULL;
	HogFileIndex hfi = HOG_INVALID_INDEX;
	F32 serviceTime;
	
	if (!source)
	{
		return false;
	}

	if (the_hog == NULL)
	{
		if(!gContainerSource.validSource || !gContainerSource.useHogs)
			return false;

		if ((the_hog = gContainerSource.incrementalHog) != NULL) {}			//the_hog is set
		else if ((the_hog = gContainerSource.fullHog) != NULL) {}			//the_hog is set
		else
		{
			return false;
		}
	}

	hfi = hogFileFind(the_hog, source->fileName);

	if (hfi != HOG_INVALID_INDEX)
	{
		U32 lastsaved = hogFileGetFileTimestamp(the_hog, hfi);
		__time32_t timestamp[1] = { timeSecondsSince2000() };
		
		//if the schema is less than a half hour old, don't write it again.
		if (lastsaved + 1800 > (U32)_time32(timestamp))
			return false;
	}

	serviceTime = objServiceTime();
	estrCreate(&estr);

	objSchemaToText(source, &estr);
	if (!estr || !estr[0])
	{
		estrDestroy(&estr);
		objDecrementServiceTime();
		return false;
	}
	
	if (objFileModifyUpdateNamed(the_hog,source->fileName,estr,estrLength(&estr)+1,objContainerGetSystemTimestamp(),serviceTime) != 0)
	{ 
		log_printf(LOG_CONTAINER, "Failed to write container schema for %s: Can't Modify Hog", GlobalTypeToName(type));
		return false;
	}
	
	return true;
}

bool objUpdateAllSchemas(const char *hoggFilename)
{
	HogFile *the_hog = hogFileReadEx(hoggFilename, NULL, PIGERR_PRINTF, NULL, HOG_DEFAULT|HOG_NO_STRING_CACHE, DB_HOG_DATALIST_JOURNAL_SIZE);
	if(the_hog)
	{
		int i, count = objNumberOfContainerSchemas();
		for(i=0; i<count; i++)
		{
			GlobalType type = objGetContainerSchemaGlobalTypeByIndex(i);
			objSaveSchemaSource(type, the_hog);
		}
		hogFileDestroy(the_hog, true);
		return true;
	}
	return false;
}

bool objSaveContainerToHog(Container *container, HogFile *the_hog)
{
	if (!container)
	{
		return false;
	}

	objDispatchContainerUpdate(container, the_hog, false);

	return true;
}

void objFlushContainers(void)
{
	if (!gContainerSource.validSource) 
		return;

	// Don't save to the file if we are just dumping loaded container information
	if(gContainerSource.dumpLoadedContainerCallback)
		return;

	objFlushModifiedContainersThreaded();

	if (gContainerSource.useHogs)
	{
		wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDFLUSHHOGCONTAINERS, NULL, 0);
		wtFlush(gContainerSource.hog_update_thread);
	}
}

static FileScanAction ContainerFileLoadCallback(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char containerTypeString[MAX_PATH];
	char containerIDString[MAX_PATH];
	char *ext;
	char *lastdir;
	FileScanAction retval = FSA_EXPLORE_DIRECTORY;
	static ContainerLoadRequest request;

	// Ignore all directories.
	if(data->attrib & _A_SUBDIR)
	{
		return retval;
	}

	// Ignore all .bak files.
	if(strEndsWith(data->name, ".bak"))
	{
		return retval;
	}

	ext = strstri(data->name,".con");
	if (!ext)
	{
		ext = strstri(data->name,".co2");
		if (!ext)
		{
			return retval;
		}
	}

	lastdir = strrchr(dir,'/');
	if (!lastdir)
	{
		return retval;
	}
	lastdir++;

	ZeroStruct(&request);

	strcpy(containerTypeString,lastdir);
	request.containerType = NameToGlobalType(containerTypeString);

	strncpy(containerIDString,data->name,ext - data->name + 1);
	request.containerID = atoi(containerIDString);

	request.fileName = data->name;

	objLoadContainer(&request);

	objProcessFinishedLoadRequest(&request);

	return retval;
}

int objUnpackContainerEx(ContainerSchema *schema, Container *con, void **containerData, int keepFileData, int onlyGetFileData, int hasLock)
{
	static PERFINFO_TYPE *perfs[GLOBALTYPE_MAXTYPES];
	char *fileData = NULL;
	char fakeFileName[MAX_PATH];
	U8 *unzippedData = NULL;
	U32 origSize = con->fileSize;
	U32 sizeOut = 0;
	U32 retval;
	bool dataOnScratchStack = false;

	assert(containerData);

	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_START_STATIC(GlobalTypeToName(con->containerType), &perfs[con->containerType], 1);

	// If the calling function hasn't already, lock the container
	if(!hasLock)
	{
		while (InterlockedIncrement(&con->updateLocked) > 1)
		{
			InterlockedDecrement(&con->updateLocked);
			Sleep(0);
		}
	}

	if(diskLoadingMode && !con->fileData)
	{
		ContainerLoadRequest request = {0};
		request.containerType = con->containerType;
		request.containerID = con->containerID;
		objLoadFileData(&request);
		con->fileData = request.fileData;
		con->bytesCompressed = request.bytesCompressed;
		origSize = con->fileSizeHint = con->fileSize = request.fileSize;
	}

	if(onlyGetFileData)
	{
		retval = !!con->fileData;
		goto cleanup;
	}

	if (con->fileData)
	{
		if (con->bytesCompressed)
		{
			PERFINFO_AUTO_START("conLoadHogDecompress",1);
			unzippedData = ScratchAlloc(con->fileSize);
			dataOnScratchStack = true;

			if(unzipData(unzippedData,
				&origSize,
				con->fileData,
				con->bytesCompressed))
			{
				log_printf(LOG_CONTAINER, "Can't decompress zipped container: %s[%d].",GlobalTypeToName(con->containerType), con->containerID);
				retval = false;
				PERFINFO_AUTO_STOP();
				goto cleanup;
			}
			PERFINFO_AUTO_STOP();
		}
		else
		{
			unzippedData = con->fileData;
		}
	}
	else
	{
		retval = false;
		goto cleanup;
	}

	if (!unzippedData || origSize != con->fileSize || !con->fileSize)
	{
		retval = false;
		goto cleanup;
	}

	*containerData = objCreateContainerObject(schema, "Creating container in objUnpackContainer");

	sprintf(fakeFileName, "%s[%d]", GlobalTypeToName(con->containerType), con->containerID);

	retval = ParserReadTextForFile(unzippedData + con->headerSize, fakeFileName, schema->classParse, *containerData, PARSER_IGNORE_ALL_UNKNOWN);

	if(con->header && con->header->extraDataOutOfDate)
	{
		objGenerateHeaderExtraData(*containerData, &con->header, schema);
	}

	if (!keepFileData)
	{
		fileData = con->fileData;
		con->fileData = NULL;

		con->fileSize = 0;
		con->bytesCompressed = 0;
	}

	con->lastAccessed = timeSecondsSince2000();

cleanup:

	if(unzippedData && dataOnScratchStack)
		ScratchFree(unzippedData);

	if(!hasLock)
		InterlockedDecrement(&con->updateLocked);

	if(fileData)
		SAFE_FREE(fileData);

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();

	return retval;
}

static void CheckContainerData(Container *con, void *unused)
{
	if(!con->containerData && !con->fileData)
	{
		AssertOrAlert("BadContainer", "Container with no file data or container data %s[%d]", GlobalTypeToName(con->containerType), con->containerID);
	}
}

AUTO_COMMAND;
void objUnpackContainersOfType(const char* globalTypeName)
{
	GlobalType type = NameToGlobalType(globalTypeName);
	ContainerStore *store = objFindContainerStoreFromType(type);

	if (!store)
		return;

	ForEachContainerOfType(type, CheckContainerData, NULL, true);
}

static void wt_objLoadContainer(ContainerLoadRequest* request)
{
	bool queueResult = false;
	int retries = 0;

	objLoadContainer(request);

	PERFINFO_AUTO_START("wt_objLoadContainer:mwtQueueOutput", 1);
	while (!(queueResult = mwtQueueOutput(request->threadManager, request)))
	{
		Sleep(0);
		retries++;
	}
	PERFINFO_AUTO_STOP();
}

static void mt_objLoadContainer(ContainerLoadRequest *request)
{
	objProcessFinishedLoadRequest(request);
}

// this is currently a selective copy and paste of QueueContainerHogLoad's filename parsing
// code, which I'm sad about but I can't test it enough to want to make it actually call this
// instead
// Hopefully I'll get a chance to look into deletes anyway (since they don't seem to work
// 100% right now) and fix this then
static bool objParseContainerFilename(const char *filename, GlobalType *type, ContainerID *id, U64 *seq, bool *deletedOut)
{
	char containerTypeString[MAX_PATH];
	char containerIDString[MAX_PATH];

	char *ext;
	char *dot;
	char *lastdir;
	char *versionstart;
	bool deleted = false;

	ext = strstri(filename,".con");
	if (!ext)
		ext = strstri(filename,".co2");
	if (!ext)
	{
		ext = strstri(filename,".del");
		deleted = true;
	}
	if (!ext)
		return false;

	lastdir = strrchr(filename,'/');
	if (!lastdir)
	{
		return false;
	}
	lastdir++;

	strncpy(containerTypeString,filename,lastdir - filename - 1);
	dot = strchr(lastdir, '.');
	strncpy(containerIDString,lastdir, dot - lastdir);
	*type = NameToGlobalType(containerTypeString);
	*id = atoi(containerIDString);

	if(deleted)
		*deletedOut = true;
	else
	{
		*deletedOut = false;
		versionstart = strstri(dot, ".v");
		if (versionstart)
		{
			char version[MAX_PATH];
			versionstart+= strlen(".v");
			strcpy(version,versionstart);
			dot = strchr(version, '.');
			if(dot)
				*dot = '\0';
			*seq = atoi64(version);
		}
	}

	return true;
}

bool QueueContainerHogLoad(HogFile *handle, HogFileIndex hogIndex, const char* filename, void *userData)
{
	U64 containerSequence = 0;
	bool deleted;

	U32 unpacked, packed;

	GlobalType containerType;
	ContainerID containerID;
	ContainerLoadRequest *request = NULL;
	ContainerLoadThreadState *threadState = (ContainerLoadThreadState*)userData;
	ContainerStore *store;

	assert(threadState);

	if (!handle)
	{
		Errorf("Failed to load container hog: %s", filename);
		return true;
	}

	if(!objParseContainerFilename(filename, &containerType, &containerID, &containerSequence, &deleted))
		return true;

	if(deleted && !threadState->bReadDeletes)
		return true;

	hogFileGetSizes(handle, hogIndex, &unpacked, &packed);

	if(gContainerSource.purgeHogOnFailedContainer && !unpacked)
	{
		hogFileModifyDelete(handle, hogIndex);
		return true;
	}

	if (!containerType || !containerID)
	{
		// Return error somehow?
		return true;
	}

	if (threadState->requiredType && threadState->requiredType != containerType)
	{
		//skip
		return true;
	}
	if (containerSequence > threadState->maxSequence)
	{
		//fprintf(fileGetStderr(), "Skipping container %s[%d] version %"FORM_LL"d because it is higher than hog sequence %"FORM_LL"d\n", GlobalTypeToName(containerType), containerID, containerSequence, threadState->maxSequence);
		return true;
	}

	request = callocStruct(ContainerLoadRequest);	
	request->containerType = containerType;
	request->containerID = containerID;
	request->threadManager = threadState->threadManager;
	request->sequenceNumber = containerSequence;
	request->deleted = deleted;
	// Delete markers get the sequence number of the hog for sorting purposes. We always want them to be first in the list
	if(request->deleted)
		request->sequenceNumber = threadState->maxSequence;
	
	request->the_hog = handle;
	request->hog_index = hogIndex;
	request->hog_file_offset = hogFileGetOffset(handle, hogIndex);
	request->fileSize = unpacked;
	request->bytesCompressed = packed;

	if(threadState->addToDiskStorageHog)
		diskLoadingAddToTable(request, filename);
	else if(diskLoadingMode)
		diskLoadingRemoveFromTable(request, filename);

	store = objFindContainerStoreFromType(containerType);
	if(store)
	{
		request->useHeader = store->requiresHeaders;
		request->lazyLoad = store->lazyLoad;
		request->hasIndex = store->indices && !store->disableIndexing;
		request->skipFileData = threadState->addToDiskStorageHog && (!request->hasIndex || request->useHeader);
	}

	eaPush(&threadState->ppRequests, request);

	return true;
}


static void ContainerHogLoadAndOverwrite(ContainerLoadThreadState *threadState, ContainerLoadRequest *request, bool bOverwrite)
{
	GlobalType containerType = request->containerType;
	ContainerID containerID = request->containerID;
	
	assert(threadState && request);
			
	PERFINFO_AUTO_START_FUNC();

	threadState->requestCount++;

	request->completeCount = &threadState->completeCount;
	request->overwrite = bOverwrite;

	{
		DWORD now;

		//PERFINFO_AUTO_START("completeCount", 1);
		PERFINFO_AUTO_START("conLoadHogExtract",1);
		if (gContainerSource.hogUseDiskOrder == 2)
			objLoadFileData(request);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("conLoadQueueInput",1);
		mwtQueueInput(threadState->threadManager, request, true);
		PERFINFO_AUTO_STOP();

		if (threadState->requestCount % 1024 == 0)
		{
			PERFINFO_AUTO_START("loadPrint",1);
			now = GetTickCount();
			if (threadState->lastUpdate + 5000 <= now)
			{
				threadState->lastUpdate = now;
				fprintf(fileGetStderr(),"\r\t\t\t\t\t\t%d containers queued for load.", threadState->requestCount);
			}
			PERFINFO_AUTO_STOP();
		}
	}

	PERFINFO_AUTO_STOP();

	return;
}

#define OBJ_HOG_BUFFER_SIZE 256*1024*1024

bool objHogHasValidContainerSourceInfo(char *filename)
{
	HogFile *the_hog;
	HogFileIndex hfi;
	bool result = false;

	hogSetMaxBufferSize(OBJ_HOG_BUFFER_SIZE);
	the_hog = hogFileReadEx(filename,NULL,PIGERR_ASSERT,NULL,HOG_NOCREATE|HOG_READONLY|HOG_NO_STRING_CACHE, DB_HOG_DATALIST_JOURNAL_SIZE);
	if (!the_hog)
		return false;
	
	hfi = hogFileFind(the_hog, CONTAINER_INFO_FILE);

	if (hfi != HOG_INVALID_INDEX)
	{
		ContainerRepositoryInfo *hogInfo = StructCreate(parse_ContainerRepositoryInfo);
		char *fileData;
		int fileSize;
		fileData = hogFileExtract(the_hog,hfi,&fileSize, NULL);
		//Check for a valid source info
		if (ParserReadText(fileData,parse_ContainerRepositoryInfo,hogInfo, 0))
		{
			if (hogInfo->iLastSequenceNumber && hogInfo->iLastTimeStamp)
				result = true;
		}
		StructDestroy(parse_ContainerRepositoryInfo, hogInfo);
		free(fileData);
	}
	hogFileDestroy(the_hog, true);
	return result;
}

static int sortContainerLoadRequests(const ContainerLoadRequest **pRequest1, const ContainerLoadRequest **pRequest2)
{
	S32 diff = (*pRequest1)->containerType - (*pRequest2)->containerType;
	if (diff)
		return diff;

	diff = (S32)((S64)(*pRequest1)->containerID - (S64)(*pRequest2)->containerID);
	if (diff)
		return diff;

	if ((*pRequest1)->sequenceNumber > (*pRequest2)->sequenceNumber)
		return -1;
	else if ((*pRequest1)->sequenceNumber < (*pRequest2)->sequenceNumber)
		return 1;
	else
		return (*pRequest2)->deleted - (*pRequest1)->deleted;
}

int sortContainerLoadRequestsByFile(const ContainerLoadRequest **pRequest1, const ContainerLoadRequest **pRequest2)
{
	const ContainerLoadRequest *lhs = *pRequest1;
	const ContainerLoadRequest *rhs = *pRequest2;

	if (lhs->the_hog < rhs->the_hog)
		return -1;
	else if (lhs->the_hog > rhs->the_hog)
		return 1;

	//Reminder: Can't return difference of U64s
	if (lhs->hog_file_offset < rhs->hog_file_offset)
		return -1;
	else if (lhs->hog_file_offset > rhs->hog_file_offset)
		return 1;
	else
		return 0;
}

static void WaitForLoadsToComplete(MultiWorkerThreadManager *threadManager)
{
	// Make sure the output queue is empty before sleeping to prevent deadlocks
	PERFINFO_AUTO_START("dequeueContainerLoad",1);
	mwtProcessOutputQueue(threadManager);
	PERFINFO_AUTO_STOP();
	mwtSleepUntilDone(threadManager);
	PERFINFO_AUTO_START("dequeueContainerLoad",1);
	mwtProcessOutputQueue(threadManager);
	PERFINFO_AUTO_STOP();
}

static void ContainerHogCreateWorkerThreads(ContainerLoadThreadState *threadState, int maxQueueSize)
{
	int threadCount = getNumRealCpus() - 1;
	int maxOutputQueueSize;
	
	if(!maxQueueSize)
		maxQueueSize = threadCount;
	maxOutputQueueSize = maxQueueSize;

	if (gContainerSource.createSnapshot)
	{
		threadCount--;
		if (threadCount < 1) threadCount = 1;
	}

	if (gContainerSource.loadThreads)
	{
		threadCount = gContainerSource.loadThreads;
	}

	// For processes that use this callback, they will have a single thread (main) that is processing
	// one container at a time, and then throwing it out. This ensures that the worker threads performing
	// the load don't outpace the single processing thread and cause memory mayhem. 
	if (gContainerSource.dumpLoadedContainerCallback)
	{
		maxOutputQueueSize = threadCount + 1;
	}

	fprintf(fileGetStderr(),"Loading with %d worker threads. Thread Queue Sizes: [%d, %d]\n", threadCount, maxQueueSize, maxOutputQueueSize);
	threadState->threadManager = mwtCreate(maxQueueSize, maxOutputQueueSize, threadCount, NULL, NULL, wt_objLoadContainer, mt_objLoadContainer, "ObjectLoadThread");
}

// returns true if there are more after this one
static bool ContainerHogServiceThreads(ContainerLoadThreadState *threadState, bool once)
{
	F32 timefence = timerGetSecondsAsFloat();
	U32 precomplete = threadState->completeCount;

	while (threadState->requestCount > threadState->completeCount)
	{
		autoTimerThreadFrameBegin("MainObjectDBLoadThread");

		PERFINFO_AUTO_START("dequeueContainerLoad",1);
		while (!mwtProcessOutputQueueEx(threadState->threadManager, once ? 1 : 0) && once)
		{
			Sleep(0);
		}
		PERFINFO_AUTO_STOP();

		if (threadState->completeCount % 1024 == 0)
		{
			DWORD now = GetTickCount();

			PERFINFO_AUTO_START("loadPrint",1);
			if (threadState->lastUpdate + 5000 <= now)
			{
				F32 newfence = timerGetSecondsAsFloat(); 

				if(newfence - timefence >= 1.0f)
				{
					F32 conpersec = ((F32)(threadState->completeCount - precomplete))/(newfence - timefence);

					threadState->lastUpdate = now;
					fprintf(fileGetStderr(),"\r%d containers loaded (%8.2fcon/s).", threadState->completeCount, conpersec);
					timefence = newfence;
					precomplete = threadState->completeCount;
				}
			}
			PERFINFO_AUTO_STOP();
		}

		autoTimerThreadFrameEnd();

		if (once)
		{
			return threadState->requestCount > threadState->completeCount;
		}
		else if (mwtInputQueueIsEmpty(threadState->threadManager))
		{
			WaitForLoadsToComplete(threadState->threadManager);
			return false;
		}
	}

	return false;
}

static ContainerRepositoryInfo * ContainerHogInfo(HogFile *the_hog)
{
	ContainerRepositoryInfo *hogInfo = NULL;
	HogFileIndex hfi = hogFileFind(the_hog, CONTAINER_INFO_FILE);
	char *fileData = NULL;
	int fileSize = 0;

	if (hfi == HOG_INVALID_INDEX)
	{
		return NULL;
	}

	hogInfo = StructCreate(parse_ContainerRepositoryInfo);
	fileData = hogFileExtract(the_hog, hfi, &fileSize, NULL);

	if (fileData)
	{
		if (!ParserReadText(fileData, parse_ContainerRepositoryInfo, hogInfo, 0))
		{
			StructDestroySafe(parse_ContainerRepositoryInfo, &hogInfo);
		}

		free(fileData);
		fileData = NULL;
	}

	return hogInfo;
}

static HogFile * ContainerHogOpen(const char *filename, bool isDiskLoadingSnapshot)
{
	HogFile *the_hog = NULL;
	the_hog = hogFileReadEx(filename, NULL, PIGERR_ASSERT, NULL, HOG_NOCREATE|HOG_NO_STRING_CACHE, DB_HOG_DATALIST_JOURNAL_SIZE);
	if (!the_hog)
	{
		FatalErrorf("Could not open hog:%s\n", filename);
		return NULL;
	}

	if (!isDiskLoadingSnapshot)
	{
		hogFileSetSingleAppMode(the_hog, true);
	}

	return the_hog;
}

static void ContainerHogClose(HogFile *the_hog, bool isDiskLoadingSnapshot)
{
	if (!the_hog)
	{
		return;
	}

	if (!isDiskLoadingSnapshot)
	{
		hogFileDestroy(the_hog, false /*JE: Unsure if this is leaking, leaving as it was before to be safe (might leak)*/);
	}
	else
	{
		hogReleaseHeaderData(the_hog);
		hogReleaseFileData(the_hog);
	}
}

static int gDealWithDeleteMarkersOnLoad = true;
AUTO_CMD_INT(gDealWithDeleteMarkersOnLoad, DealWithDeleteMarkersOnLoad);

typedef enum { CLHT_NORMAL, CLHT_OFFLINE } ContainerLoadHoggType;

typedef struct ContainerHogLoadState
{
	bool loadingCurrent;
	bool failed;
	HogFile *the_hog;
	ContainerLoadThreadState *threadState;
	ContainerLoadHoggType hoggType;
	bool isDiskLoadingSnapshot;
	IncrementalLoadState *loadstate;
	U32 invalidcount;
	char *filename;
	bool isIncremental;
} ContainerHogLoadState;

static ContainerHogLoadState * ContainerHogLoadAllContainersStart(const char *filename,
	IncrementalLoadState *loadstate, bool snapshot, ContainerLoadHoggType hoggType)
{
	bool isDiskLoadingSnapshot = diskLoadingMode && snapshot;
	GlobalType lastType = 0;
	ContainerID lastID = 0;
	GlobalType lastDeletedType = GLOBALTYPE_NONE;
	ContainerID lastDeletedID = 0;
	ContainerLoadRequest **ppFileSortedRequests = NULL;
	HogFile *the_hog = NULL;
	ContainerRepositoryInfo *hogInfo = NULL;
	ContainerHogLoadState * state = NULL;

	PERFINFO_AUTO_START_FUNC();

	loadstart_printf("Loading %s...", filename);
	loadstart_printf("Opening hog...");
	the_hog = ContainerHogOpen(filename, false);
	if (!the_hog)
	{
		loadend_printf("failed");

		PERFINFO_AUTO_STOP();
		return NULL;
	}
	loadend_printf("done");

	if (hoggType == CLHT_NORMAL)
	{
		if (loadstate->dumptype)
		{
			if (!objLoadSchemaSource(loadstate->dumptype, the_hog))
			{
				ContainerHogClose(the_hog, false);
				the_hog = NULL;

				loadend_printf("failed to find schema");

				PERFINFO_AUTO_STOP();
				return NULL;
			}
		}

		loadstart_printf("Reading repository info...");
		hogInfo = ContainerHogInfo(the_hog);
		if (!hogInfo)
		{
			FileRenameRequest *mv = callocStruct(FileRenameRequest);

			mv->filename = strdup(filename);
			mv->moveToOld = true;
			eaPush(&loadstate->renames, mv);

			ContainerHogClose(the_hog, false);
			the_hog = NULL;

			fprintf(fileGetStderr(),"Hog did not contain valid container source info:%s. \n", filename);
			log_printf(LOG_CONTAINER,"Hog did not contain valid container source info:%s. ", filename);
			loadend_printf("failed");

			PERFINFO_AUTO_STOP();
			return NULL;
		}
		loadend_printf("done");
	}

	state = callocStruct(ContainerHogLoadState);
	state->the_hog = the_hog;
	state->hoggType = hoggType;
	state->isDiskLoadingSnapshot = isDiskLoadingSnapshot;
	state->loadstate = loadstate;
	state->invalidcount = objCountInvalidContainers();
	state->filename = strdup(filename);

	EnterCriticalSection(&gContainerSource.hog_access);

	if (hoggType == CLHT_OFFLINE
		|| gContainerSource.ignoreContainerSource
		|| hogInfo->iLastSequenceNumber > gContainerSource.sourceInfo->iLastSequenceNumber
		|| gContainerSource.dumpload)
	{
		state->isIncremental = false;

		state->loadingCurrent = true;

		if (hoggType == CLHT_NORMAL)
		{
			// Ignore current, because only the written stuff is relevant when loading
			hogInfo->iCurrentSequenceNumber = hogInfo->iLastSequenceNumber;
			hogInfo->iCurrentTimeStamp = hogInfo->iLastTimeStamp;
			StructDestroy(parse_ContainerRepositoryInfo, gContainerSource.sourceInfo);
			gContainerSource.sourceInfo = hogInfo;

			// Checking the base and max containerIDs to see if they have changed.

			if(gContainerSource.sourceInfo->iBaseContainerID != gBaseContainerID || gContainerSource.sourceInfo->iMaxContainerID != gMaxContainerID)
			{
				if(gAllowChangeOfBaseContainerID)
				{
					gContainerSource.sourceInfo->iBaseContainerID = gBaseContainerID;
					gContainerSource.sourceInfo->iMaxContainerID = gMaxContainerID;
				}
				else if(!gContainerSource.sourceInfo->iBaseContainerID && !gContainerSource.sourceInfo->iMaxContainerID)
				{
					// If the base id values are both 0, set anyway since this is our first run at it.
					gContainerSource.sourceInfo->iBaseContainerID = gBaseContainerID;
					gContainerSource.sourceInfo->iMaxContainerID = gMaxContainerID;
				}
				else
				{
					// Otherwise, don't allow the change
					assertmsg(0, "The command-line argument determing the base ContainerID range has changed without the command-line argument -AllowChangeOfBaseContainerID. If you really mean it, add this command-line argument and restart the shard.");
				}
			}
			else
			{
				if(gAllowChangeOfBaseContainerID)
				{
					ErrorOrCriticalAlert("OBJECTDB.UNNEEDEDCONTAINERIDCHANGE", "The flag -AllowChangeOfBaseContainerID is set with no change happening. Remove this flag to prevent accidental changes.");
				}
			}

			objSetRepositoryMaxIDs();

			state->isIncremental = objStringIsPendingFilename(filename);
			if (state->isIncremental) 
			{
				fprintf(fileGetStderr(),"Merging incremental hog: %s\n", filename);
				log_printf(LOG_CONTAINER,"Merging incremental hog: %s\n", filename);
			}
			fprintf(fileGetStderr(),"\n");
		}

		state->threadState = callocStruct(ContainerLoadThreadState);
		if (hoggType == CLHT_NORMAL)
		{
			// Make the threadState load deletedFiles
			if(gDealWithDeleteMarkersOnLoad)
				state->threadState->bReadDeletes = true;

			state->threadState->maxSequence = hogInfo->iLastSequenceNumber;

			if(isDiskLoadingSnapshot)
			{
				diskLoadingSetDiskLoadingHog(the_hog, false);
				state->threadState->addToDiskStorageHog = true;
			}
		}

		if (loadstate->dumptype)
			state->threadState->requiredType = loadstate->dumptype;

		// Create worker threads if appropriate
		{
			int maxQueueSize = hogFileGetNumUserFiles(the_hog);
			ContainerHogCreateWorkerThreads(state->threadState, maxQueueSize);
		}

		// Scan the hogg for container files
		loadstart_printf("Scanning hog for containers...");
		hogScanAllFiles(the_hog, QueueContainerHogLoad, state->threadState);
		loadend_printf("done");

		// Do the actual loading (if single-threaded) or queue for multi-threading
		loadstart_printf("Loading containers...\n");
		if (eaSize(&state->threadState->ppRequests))
		{
			eaQSort(state->threadState->ppRequests, sortContainerLoadRequests);

			if (gContainerSource.hogUseDiskOrder) eaCreate(&ppFileSortedRequests);

			EARRAY_CONST_FOREACH_BEGIN(state->threadState->ppRequests, iCurRequest, iNumRequests);
			{
				ContainerLoadRequest *pRequest = state->threadState->ppRequests[iCurRequest];

				if (pRequest->containerType == lastType && pRequest->containerID == lastID)
				{
					// We already loaded a newer one
					continue;
				}

				if (hoggType == CLHT_NORMAL)
				{
					if (pRequest->deleted)
					{
						// If there is a delete marker for a container, remember it
						lastDeletedType = pRequest->containerType;
						lastDeletedID = pRequest->containerID;
						continue;
					}

					if (pRequest->containerType == lastDeletedType && pRequest->containerID == lastDeletedID)
					{
						// If this container had a delete marker in the hogg, mark it for removal
						// This allows us to keep deleted containers out of the repository even if they are lazyloaded
						pRequest->removeContainer = true;
					}
				}

				if (gContainerSource.hogUseDiskOrder)
				{
					eaPush(&ppFileSortedRequests, pRequest);
				}
				else
				{
					ContainerHogLoadAndOverwrite(state->threadState, pRequest, true);
				}

				lastType = pRequest->containerType;
				lastID = pRequest->containerID;
			}
			EARRAY_FOREACH_END;

			if (gContainerSource.hogUseDiskOrder) 
			{
				ContainerLoadRequest *pRequest = NULL;

				eaQSort(ppFileSortedRequests, sortContainerLoadRequestsByFile);
				EARRAY_FOREACH_BEGIN(ppFileSortedRequests, j);
				{
					ContainerHogLoadAndOverwrite(state->threadState, ppFileSortedRequests[j], true);
				}
				EARRAY_FOREACH_END;

				eaDestroy(&ppFileSortedRequests);
			}
		}

		{
			fprintf(fileGetStderr(),"\r\t\t\t\t\t\t%d containers queued for load.", state->threadState->requestCount);
			mwtWakeThreads(state->threadState->threadManager);
		}
	}
	else
	{
		bool oldlikecheese = (gContainerSource.scanSnapshotTime && hogInfo->iCurrentTimeStamp < (int)(gContainerSource.scanSnapshotTime) - (gContainerSource.incrementalHoursToKeep * 3600));

		StructDestroySafe(parse_ContainerRepositoryInfo, &hogInfo);

		fprintf(fileGetStderr(),"Skipping old hog:%s\n", filename);
		log_printf(LOG_CONTAINER,"Skipping old hog:%s\n", filename);

		//do rename
		if (gContainerSource.createSnapshot && objStringIsPendingFilename(filename))
		{
			char mergename[MAX_PATH];
			FileRenameRequest *mv = callocStruct(FileRenameRequest);

			strcpy(mergename, filename);
			objConvertToSkippedFilename(mergename);
			mv->filename = strdup(filename);
			mv->newname = strdup(mergename);
			mv->moveToOld = true;
			eaPush(&loadstate->renames, mv);
		}
		else
		{
			if (oldlikecheese) 
			{
				FileRenameRequest *mv = callocStruct(FileRenameRequest);

				mv->filename = strdup(filename);
				mv->moveToOld = true;
				eaPush(&loadstate->renames, mv);
			}
		}

		state->failed = true;
	}

	return state;
}

static bool ContainerHogLoadAllContainersContinue(ContainerHogLoadState *state, bool once)
{
	if (!state) return false;

	if (state->loadingCurrent)
	{
		// Wait for the loading threads to finish (if multi-threaded)
		return ContainerHogServiceThreads(state->threadState, once);
	}

	return false;
}

static bool ContainerHogLoadAllContainersFinish(ContainerHogLoadState *state)
{
	bool result = false;
	
	if (!state) return false;

	result = !state->failed;

	if (state->loadingCurrent)
	{
		ContainerHogServiceThreads(state->threadState, false);
		mwtDestroy(state->threadState->threadManager);
		fprintf(fileGetStderr(),"\r%d containers loaded.                \n", state->threadState->completeCount);

		loadend_printf("done");

		// Make sure we loaded the correct number
		MemoryBarrier();
		state->invalidcount = objCountInvalidContainers() - state->invalidcount;
		state->loadstate->containersRequested += state->threadState->requestCount;
		state->loadstate->containersLoaded += state->threadState->completeCount - state->invalidcount;

		if (state->invalidcount || state->threadState->requestCount != state->threadState->completeCount)
		{
			ErrorOrAlert("DATABASE.LOADFAILURE", "ObjectDB could not load all containers (%u invalid, %u loaded of %u) from incremental hog: %s\n",
				state->invalidcount, state->threadState->completeCount, state->threadState->requestCount, state->filename);
		}

		// Print load time report if requested.
		if (gbDbLoadTimeReport)
		{
			int j;
			char *report = NULL;

			estrStackCreate(&report);
			puts("\nDB Load Time Report");
			for (j = 0; j != ea64Size(&state->threadState->ppContainerLoadCpuTicks); ++j)
			{
				if (state->threadState->ppContainerLoadCpuTicks[j])
				{
					estrConcatf(&report, "%s\t%"FORM_LL"u\n", GlobalTypeToName(j), state->threadState->ppContainerLoadCpuTicks[j]);
				}
			}
			log_printf(LOG_CONTAINER, "%s", report);
			puts(report);
			estrDestroy(&report);
		}

		ea64Destroy(&state->threadState->ppContainerLoadCpuTicks);
		eaDestroyEx(&state->threadState->ppRequests, NULL);
		free(state->threadState);

		if (state->hoggType == CLHT_NORMAL)
		{
			if (gContainerSource.createSnapshot && state->isIncremental)
			{
				char mergename[MAX_PATH];
				FileRenameRequest *mv = callocStruct(FileRenameRequest);

				strcpy(mergename, state->filename);
				objConvertToMergedFilename(mergename);
				mv->filename = strdup(state->filename);
				mv->newname = strdup(mergename);
				eaPush(&state->loadstate->renames, mv);
			}

			if (gContainerSource.ignoreContainerSource)
			{
				gContainerSource.sourceInfo->iCurrentSequenceNumber = gContainerSource.sourceInfo->iLastSequenceNumber = 1;
				gContainerSource.sourceInfo->iCurrentTimeStamp = gContainerSource.sourceInfo->iLastTimeStamp = timeSecondsSince2000();
				objSaveContainerSourceInfo(NULL);
			}
		}
	}

	LeaveCriticalSection(&gContainerSource.hog_access);

	ContainerHogClose(state->the_hog, false);

	loadend_printf("done");

	if (state->filename)
	{
		free(state->filename);
	}
	free(state);
	state = NULL;

	PERFINFO_AUTO_STOP();
	return result;
}

//Merges an incremental hog or snapshot and returns true if no errors.
static bool objLoadIncrementalHogFileEx(const char *filename, IncrementalLoadState *loadstate, bool snapshot)
{
	bool result = true;
	ContainerHogLoadState * state = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	assert(loadstate);
		
	state = ContainerHogLoadAllContainersStart(filename, loadstate, snapshot, CLHT_NORMAL);
	ContainerHogLoadAllContainersContinue(state, false);
	result = ContainerHogLoadAllContainersFinish(state);

	PERFINFO_AUTO_STOP();
	return result;
}

static bool objMergeIncrementalHogFileEx(HogFile **snapshotHog, const char *filename, IncrementalLoadState *loadstate, ContainerLoadThreadState *threadState, bool snapshot, bool bAppend)
{
	int i;
	bool retVal = false;
	HogFileIndex hfi;
	assert(snapshotHog);
	hogSetMaxBufferSize(OBJ_HOG_BUFFER_SIZE);
	if(bAppend)
		*snapshotHog = hogFileReadEx(filename,NULL,PIGERR_ASSERT,NULL,HOG_NOCREATE|HOG_MUST_BE_WRITABLE|HOG_APPEND_ONLY|HOG_NO_STRING_CACHE, DB_HOG_DATALIST_JOURNAL_SIZE);
	else
		*snapshotHog = hogFileReadEx(filename,NULL,PIGERR_ASSERT,NULL,HOG_NOCREATE|HOG_MUST_BE_WRITABLE|HOG_NO_STRING_CACHE, DB_HOG_DATALIST_JOURNAL_SIZE);

	hogFileSetSingleAppMode(*snapshotHog, true);
			
	if (!(*snapshotHog))
	{
		FatalErrorf("Could not open hog:%s\n", filename);
		return retVal;
	}
	hfi = hogFileFind(*snapshotHog, CONTAINER_INFO_FILE);

	if (hfi != HOG_INVALID_INDEX)
	{
		ContainerRepositoryInfo *hogInfo = StructCreate(parse_ContainerRepositoryInfo);
		char *fileData;
		int fileSize;
		U32 fileCount;
		bool result = true;
		fileData = hogFileExtract(*snapshotHog,hfi,&fileSize, NULL);
		fileCount = hogFileGetNumUserFiles(*snapshotHog) - 1;
		//Check for a valid source info
		if (ParserReadText(fileData,parse_ContainerRepositoryInfo,hogInfo, 0))
		{
			StructDestroy(parse_ContainerRepositoryInfo, gContainerSource.sourceInfo);
			gContainerSource.sourceInfo = hogInfo;
			objSetRepositoryMaxIDs();

			threadState->maxSequence = hogInfo->iLastSequenceNumber;

			hogScanAllFiles(*snapshotHog, QueueContainerHogLoad, threadState);

			for (i = 0; i < eaSize(&threadState->ppRequests); i++)
			{
				ContainerLoadRequest *pRequest = threadState->ppRequests[i];
				pRequest->fromSnapshot = 1;
				if(pRequest->deleted)
				{
					// Delete marks in snapshots get the sequence number of the snapshot for sorting purposes.
					pRequest->sequenceNumber = hogInfo->iLastSequenceNumber;
				}
			}
			retVal = true;
		}
		if (fileData) free(fileData);
	}
	return retVal;
}

//Merges an incremental hog or snapshot and returns true if no errors.
static bool objLoadOfflineHogFileEx(const char *filename, IncrementalLoadState *loadstate)
{
	bool result = true;
	ContainerHogLoadState * state = NULL;

	PERFINFO_AUTO_START_FUNC();
	assert(loadstate);

	state = ContainerHogLoadAllContainersStart(filename, loadstate, false, CLHT_OFFLINE);
	ContainerHogLoadAllContainersContinue(state, false);
	result = ContainerHogLoadAllContainersFinish(state);

	PERFINFO_AUTO_STOP();
	return result;
}

bool objLoadContainersFromHoggForDumpEx(const char *filename, GlobalType type)
{
	IncrementalLoadState loadState = {0, 0, 0, NULL, NULL, type};
	return objLoadIncrementalHogFileEx(filename,&loadState,true);
}

bool objLoadContainersFromOfflineHoggForDumpEx(const char *filename, GlobalType type)
{
	IncrementalLoadState loadState = {0, 0, 0, NULL, NULL, type};
	return objLoadOfflineHogFileEx(filename,&loadState);
}

Container * gCurrentIterContainer = NULL;
U32 gCurrentIterContainerModifiedTime = 0;

static bool ContainerLoadIterOnLoad(Container *con, U32 uContainerModifiedTime)
{
	if (gCurrentIterContainer)
	{
		objDestroyContainer(gCurrentIterContainer);
		gCurrentIterContainer = NULL;
	}
	gCurrentIterContainer = objContainerCopy(con, true, NULL);
	gCurrentIterContainerModifiedTime = uContainerModifiedTime;
	return true;
}

void objContainerLoadIteratorInit(ContainerLoadIterator *iter, const char *filename, const char *offlineFilename, GlobalType type, int numThreads)
{
	iter->offlineFilename = strdup(offlineFilename);

	iter->loadstate = callocStruct(IncrementalLoadState);
	iter->loadstate->dumptype = type;

	objSetContainerSourceToHogFile(filename, 0, NULL, NULL);
	if(numThreads)
		objSetMultiThreadedLoadThreads(numThreads);

	objSetDumpMode(true);
	objSetDumpType(type);
	objSetDumpLoadedContainerCallback(ContainerLoadIterOnLoad);

	iter->state = ContainerHogLoadAllContainersStart(filename, iter->loadstate, true, CLHT_NORMAL);
}

Container * objGetNextContainerFromLoadIterator(ContainerLoadIterator *iter, U32 * uLastModifiedTimeOut)
{
	if (!iter->state) return NULL;

	if (gCurrentIterContainer)
	{
		objDestroyContainer(gCurrentIterContainer);
		gCurrentIterContainer = NULL;
		gCurrentIterContainerModifiedTime = 0;
	}

	if (!ContainerHogLoadAllContainersContinue(iter->state, true))
	{
		if (!iter->loadingOfflineHog && iter->offlineFilename && iter->offlineFilename[0])
		{
			ContainerHogLoadAllContainersFinish(iter->state);
			free(iter->loadstate);
			iter->loadstate = callocStruct(IncrementalLoadState);
			iter->loadstate->dumptype = iter->type;
			iter->state = ContainerHogLoadAllContainersStart(iter->offlineFilename, iter->loadstate, false, CLHT_OFFLINE);
			iter->loadingOfflineHog = true;

			if (!gCurrentIterContainer)
			{
				return objGetNextContainerFromLoadIterator(iter, uLastModifiedTimeOut);
			}
		}
	}
	if (uLastModifiedTimeOut) *uLastModifiedTimeOut = gCurrentIterContainerModifiedTime;
	return gCurrentIterContainer;
}

void objClearContainerLoadIterator(ContainerLoadIterator *iter)
{
	if (iter->state)
	{
		ContainerHogLoadAllContainersFinish(iter->state);
		iter->state = NULL;
	}

	if (gCurrentIterContainer)
	{
		objDestroyContainer(gCurrentIterContainer);
		gCurrentIterContainer = NULL;
		gCurrentIterContainerModifiedTime = 0;
	}

	if (iter->offlineFilename)
	{
		free(iter->offlineFilename);
		iter->offlineFilename = NULL;
	}

	if (iter->loadstate)
	{
		free(iter->loadstate);
		iter->loadstate = NULL;
	}

	objCloseContainerSource();
}

//Delete files in old directory older than 10 days.
static FileScanAction OldCleanupFileScanCallback(char *dir, struct _finddata32_t* data, PruneList *list)
{
	FileScanAction retval = FSA_NO_EXPLORE_DIRECTORY;
	do {
		// Ignore all directories.
		if(data->attrib & _A_SUBDIR) break;
		if (objStringIsPendingFilename(data->name))
		{
			PruneFile *file = StructCreate(parse_PruneFile);
			file->filename = strdupf("%s/%s", dir, data->name);
			file->time = data->time_write;
			eaPush(&list->incrementals, file);
		}
		else if (objStringIsProcessedFilename(data->name) || objStringIsBadStopFilename(data->name))
		{
			PruneFile *file = StructCreate(parse_PruneFile);
			file->filename = strdupf("%s/%s", dir, data->name);
			file->time = data->time_write;
			eaPush(&list->incrementals, file);
		}
		else if (objStringIsSnapshotFilename(data->name))
		{
			PruneFile *file = StructCreate(parse_PruneFile);
			file->filename = strdupf("%s/%s", dir, data->name);
			file->time = data->time_write;
			eaPush(&list->snapshots, file);
		}
		else if (objStringIsPendingLogname(data->name))
		{
			PruneFile *file = StructCreate(parse_PruneFile);
			file->filename = strdupf("%s/%s", dir, data->name);
			file->time = data->time_write;
			eaPush(&list->logs, file);
		}
		else if (objStringIsOfflineFilename(data->name))
		{
			PruneFile *file = StructCreate(parse_PruneFile);
			file->filename = strdupf("%s/%s", dir, data->name);
			file->time = data->time_write;
			eaPush(&list->offlines, file);
		}
	} while (false);

	return retval;
}

static void objPruneOldDirectory(char *dir)
{
	PruneList files = {0};
	StructInit(parse_PruneList, &files);

	PERFINFO_AUTO_START("PruningOldIncrementals",1);
	fileScanAllDataDirs(dir, OldCleanupFileScanCallback, &files);
	eaQSort(files.incrementals, CompareFiletime);
	if (eaSize(&files.incrementals) > 0) { //incrementals
		int hoursToKeep = (gContainerSource.pendingHoursToKeep?gContainerSource.pendingHoursToKeep:24);
		__time32_t cutoff = _time32(NULL) - hoursToKeep*3600;
		int i;
		fprintf(fileGetStderr(),"Cleaning up incrementals in /old.\n");
		for (i = 0; i < eaSize(&files.incrementals); i++)
		{
			if (files.incrementals[i]->time < cutoff)
			{
				fprintf(fileGetStderr(),"\tPruning old file: %s.\n", files.incrementals[i]->filename);
				fileForceRemove(files.incrementals[i]->filename);
			}
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("PruningOldLogs",1);
	eaQSort(files.logs, CompareFiletime);
	if (eaSize(&files.logs) > 0) { //incrementals
		int hoursToKeep = (gContainerSource.journalHoursToKeep?gContainerSource.journalHoursToKeep:24*30);
		__time32_t cutoff = _time32(NULL) - hoursToKeep*3600;
		int i;
		fprintf(fileGetStderr(),"Cleaning up logs in /old.\n");
		for (i = 0; i < eaSize(&files.logs); i++)
		{
			if (files.logs[i]->time < cutoff)
			{
				fprintf(fileGetStderr(), "\tPruning old file: %s.\n", files.logs[i]->filename);
				fileForceRemove(files.logs[i]->filename);
			}
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("PruningOldSnapshots",1);
	eaQSort(files.snapshots, CompareFiletime);
	if (eaSize(&files.snapshots) > 0) { //snapshots
		int i = 0;
		int count = eaSize(&files.snapshots);
		__time32_t lasttime = 0;
		__time32_t prunetime = timeClampTimeToHour(_time32(NULL));
		__time32_t window;
		__time32_t cutoff;

		int minutesToKeep = (gContainerSource.snapshotMinutesToKeep?gContainerSource.snapshotMinutesToKeep:120);
		int hoursToKeep = (gContainerSource.snapshotHoursToKeep?gContainerSource.snapshotHoursToKeep:24);
		int daysToKeep = (gContainerSource.snapshotDaysToKeep?gContainerSource.snapshotDaysToKeep:30);

		fprintf(fileGetStderr(), "Pruning up snapshots in /old.\n");

		window = _time32(NULL) - minutesToKeep * 60;	//keep all for 120 minutes
		lasttime = window;
		while (i < count && files.snapshots[i]->time > window)
		{	//keep everything within 120minutes
			fprintf(fileGetStderr(), "Keeping old file: %s. [%4.2fm]\n", files.snapshots[i]->filename,((prunetime - files.snapshots[i]->time)/60.0f));
			lasttime = files.snapshots[i]->time;
			i++;
		}
		
		verbose_printf("Pruning entries older than %d minutes.\n\n", minutesToKeep);

		//Prune the last day to 1 per hour
		cutoff = prunetime - hoursToKeep * 60 * 60;
		do {
			window = timeClampTimeToHour(lasttime);
			if (window == lasttime) window -= 3600;
			if (window < cutoff) break;

			while (i < count && files.snapshots[i]->time > window)
			{	//remove everything until the next window
				fprintf(fileGetStderr(), "\tPruning old file: %s. [%4.2fh]\n", files.snapshots[i]->filename,((prunetime - files.snapshots[i]->time)/3600.0f));
				fileForceRemove(files.snapshots[i]->filename);
				i++;
			}
			if (i < count)
			{
				lasttime = files.snapshots[i]->time;
				fprintf(fileGetStderr(), "Keeping old file: %s. [%4.2fh]\n", files.snapshots[i]->filename,((prunetime - files.snapshots[i]->time)/3600.0f));
				i++; //The current file is older than the window; keep it.
			}
		} while (i < count);

		verbose_printf("Pruning entries older than %d hours.\n\n", hoursToKeep);

		//Prune the last 30 days to 1 per day
		cutoff = prunetime - daysToKeep * 24 * 60 * 60;
		do {
			window = timeClampTimeToDay(lasttime);
			if (window == lasttime) window -= 24 * 60 * 60;
			if (window < cutoff) break;

			while (i < count && files.snapshots[i]->time > window)
			{	//remove everything until the next window
				fprintf(fileGetStderr(), "\tPruning old file: %s. [%4.2fd]\n", files.snapshots[i]->filename,((prunetime - files.snapshots[i]->time)/86400.0f));
				fileForceRemove(files.snapshots[i]->filename);
				i++;
			}
			if (i < count)
			{
				lasttime = files.snapshots[i]->time;
				fprintf(fileGetStderr(), "Keeping old file: %s. [%4.2fd]\n", files.snapshots[i]->filename,((prunetime - files.snapshots[i]->time)/86400.0f));
				i++; //The current file is older than the window; keep it.
			}
		} while (i < count);

		verbose_printf("Pruning entries older than %d days.\n\n", daysToKeep);
		
		//Prune the rest (older than 30 days)
		for (; i < count; i++)
		{
			fprintf(fileGetStderr(), "\tPruning old file: %s. [%4.2fd]\n", files.snapshots[i]->filename,((prunetime - files.snapshots[i]->time)/86400.0f));
			fileForceRemove(files.snapshots[i]->filename);
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("PruningOldOfflines",1);
	eaQSort(files.offlines, CompareFiletime);
	if (eaSize(&files.offlines) > 0) { //offlines
		int daysToKeep = (gContainerSource.offlineDaysToKeep?gContainerSource.offlineDaysToKeep:30);
		__time32_t cutoff = _time32(NULL) - daysToKeep * 60 * 60 * 24;
		int i;
		fprintf(fileGetStderr(),"Cleaning up offlines in /old.\n");
		for (i = 0; i < eaSize(&files.offlines); i++)
		{
			if (files.offlines[i]->time < cutoff)
			{
				fprintf(fileGetStderr(), "\tPruning old file: %s.\n", files.offlines[i]->filename);
				fileForceRemove(files.offlines[i]->filename);
			}
		}
	}
	PERFINFO_AUTO_STOP();

	StructDeInit(parse_PruneList, &files);
}


//Scan the data dir for snapshots. After scanning, the estr in pUserData should have the last one.
static FileScanAction SnapshotHogFileScanCallback(char *dir, struct _finddata32_t* data, char **lastSnapshot)
{
	const char *current = "";
	char fdir[MAX_PATH];
	FileScanAction retval = FSA_NO_EXPLORE_DIRECTORY;

	strcpy(fdir, dir);
	//getDirectoryName(fdir);

	do {
		// Ignore all directories.
		if(data->attrib & _A_SUBDIR)
		{
			break;
		}

		strcat(fdir, "/");
		strcat(fdir, data->name);

		//Check for snapshots
		if (objStringIsSnapshotFilename(data->name))
		{
			if (objHogHasValidContainerSourceInfo(fdir))
			{
				if (fileExists(*lastSnapshot)) 
					objMoveToOldFilesDir(*lastSnapshot);

				estrCopy2(lastSnapshot, fdir);
			}
		}

		//Check for orphaned current hogs.
		
		if (gContainerSource.incrementalHog)
		{
			current = hogFileGetArchiveFileName(gContainerSource.incrementalHog);
		}
		if (objStringIsCurrentFilename(data->name) && stricmp(fdir, current) && !gContainerSource.createSnapshot)
		{
			char movedname[MAX_PATH];
			fprintf(fileGetStderr(), "Found an orphaned current hog file (%s), moving aside.\nObjectDB may not have shut down correctly last time. Don't worry. Be happy!\n\n\n", data->name);
			log_printf(LOG_CONTAINER,"Found an orphaned current hog file (%s), moving aside.\nObjectDB may not have shut down correctly last time. Don't worry. Be happy!\n\n\n", data->name);
			
			strcpy(movedname, fdir);
			objConvertToBadstopFilename(movedname);
			if(fileMove(fdir, movedname) != 0 && isProductionMode()) AssertOrAlert("OBJECTDB_RENAME_FAILED", "Failed moving orphaned hog file %s",data->name);
		}
	} while (false);

	return retval;
}

static FileScanAction IncrementalHogFileLoadCallback(char* dir, struct _finddata32_t* data, IncrementalLoadState *state)
{
	char fdir[MAX_PATH];
	FileScanAction retval = FSA_NO_EXPLORE_DIRECTORY;

	strcpy(fdir, dir);
	//getDirectoryName(fdir);

	// Ignore all directories.
	if(data->attrib & _A_SUBDIR)
	{
		return retval;
	}

	if (objStringIsPendingLogname(data->name))
	{
		FILE *file;
		char buf[20];

		strcat(fdir, "/");
		strcat(fdir, data->name);

		file = fopen(fdir, "r");
		if (file)
		{
			IncrementalLogRef *logref = NULL;
			if (fread(buf, 20, 1, file))
			{ 
				logref = StructCreate(parse_IncrementalLogRef);
				logref->sequenceNum = atoi64(buf);
				logref->time_write = data->time_write;
				estrPrintf(&logref->filename, "%s", fdir);
					
				eaPush(&(state->ppLogs), logref);
			}
			fclose(file);

				
			if (logref && !gContainerSource.createSnapshot && strEndsWith(logref->filename, "lcg"))
			{	//rename lcg files.
				char newname[MAX_PATH];
				char *ext = NULL;
				strcpy(newname, logref->filename);
				ext = FindExtensionFromFilename(newname);
				ext[0] = '\0';
				strcat(newname, ".log");
				fprintf(fileGetStderr(), "Renaming %s to %s.\n", logref->filename, newname);
				if (fileMove(logref->filename, newname) != 0 && isProductionMode()) AssertOrAlert("OBJECTDB_RENAME_FAILED","Failed Renaming %s to %s.\n", logref->filename, newname);
				ext = FindExtensionFromFilename(logref->filename);
				ext[2] = 'o';
			}
		}
		else
			Errorf("Could not open log file for merging:%s\n", fdir);
	}
	else if (objStringIsPendingFilename(data->name))
	{
		strcat(fdir, "/");
		strcat(fdir, data->name);
		if (objLoadIncrementalHogFileEx(fdir, state, false))
		{
			retval = 1;
			state->fileCount++;
		}
	}
	//clean up old processed hogs.
	else if (objStringIsProcessedFilename(data->name))
	{
		objCleanupProcessedFile(SAFESTR(fdir), data->name);
	}
	else if(objStringIsBadStopFilename(data->name))
	{
		objCleanupBadStopFile(SAFESTR(fdir), data->name);
	}

	return retval;
}

//Grab the sequence number from the buffer.
static U64 parseSeq(char **estr)
{
	U64 seq;
	if (*estr[0] < '0' || *estr[0] > '9')
		return 0;
	if (sscanf_s(*estr, "%"FORM_LL"u ", &seq) != 1)
		return 0;
	estrRemoveUpToFirstOccurrence(estr,' ');
	return seq;
}

#define PARSE_TIME_FAIL UINT_MAX

//This timestring parsing function is kind of inefficent...
//Parse out a time stamp and return it or UINT_MAX on fail; Consumes the parsed time stamp characters from the string.
static U32 parseTime(char **estr, bool strictMerge)
{
	U32 timestamp;
	int year,month,day,hour,min,sec;
	
	if (strictMerge)
	{
		if (sscanf_s(*estr, "%04u-%02u-%02u %02u:%02u:%02u: ",&year,&month,&day,&hour,&min,&sec) != 6)
		{
			return PARSE_TIME_FAIL;
		}
		else
		{
			char *temp = 0;
			estrStackCreate(&temp);
			estrPrintf(&temp, "%04u-%02u-%02u %02u:%02u:%02u:", year, month, day, hour, min, sec);
			timestamp = timeGetSecondsSince2000FromDateString(temp);
			estrDestroy(&temp);

			estrRemoveUpToFirstOccurrence(estr, ':');
			estrRemoveUpToFirstOccurrence(estr, ':');
			estrRemoveUpToFirstOccurrence(estr, ' ');

			return timestamp;
		}
	}
	else
	{
		estrRemoveUpToFirstOccurrence(estr, ' ');
		estrRemoveUpToFirstOccurrence(estr, ' ');
		return 0;
	}
}

#define READ_BUFFER_SIZE 256
//Grab the next line and stick it into the estring buffer.
static int logReadLine(FileWrapper *fw, char **estr)
{
	char buffer[READ_BUFFER_SIZE];
	char c;
	int i = 0;
	int count = 0;
	
	while ( (c = getc(fw)) != '\n' && c != EOF)
	{
		buffer[i++] = c;
		count++;
		if (i == READ_BUFFER_SIZE-2)
		{
			buffer[i] = '\0';
			estrConcatString(estr, buffer, i);
			i = 0;
		}
	}
	if (i)
	{
		buffer[i] = '\0';
		estrConcatString(estr, buffer, i);
		i = 0;
	}
	if (c == '\n') count++;
	return count;
}

static void objReplayTransaction(char *trans, U64 sequenceNumber, U32 timestamp, bool shouldPrint)
{
	if (shouldPrint)
	{
		fprintf(fileGetStderr(), "[%"FORM_LL"u]", sequenceNumber);
		verbose_printf("%u:%s", timestamp, trans);
	}
	assertmsg(gContainerSource.replayCB, "Cannot replay logged transaction without setting ObjectDB callback!\n");
	gContainerSource.replayCB(trans, sequenceNumber, timestamp);
}

static int objReplayLogWithRestart(char *filename, FileWrapper **file)
{
	char buf[MAX_PATH];
	int transactionsReplayed = 0;
	S64 processTimeTicks = 10 * timerCpuSpeed64() / 1000;
	S64 timerStartTime = timerCpuTicks64();

	if(!file)
		return 0;

	if(!*file)	
		*file = fileOpenRAMCached(filename, "r!");

	if (*file)
	{
		if(gContainerSource.logFileReplayCB && !gContainerSource.logFileReplayCB(filename))
		{
			fclose(*file);
			return 0;
		}

		if (strEndsWith(filename, "log"))
			fprintf(fileGetStderr(), "Replaying incremental log: %s\n", filename);
		else if (!gContainerSource.createSnapshot && strEndsWith(filename, "lcg"))
		{
			char *ext = NULL;
			strcpy(buf, filename);
			ext = FindExtensionFromFilename(buf);
			ext[0] = '\0';
			strcat(buf, ".log");
			fprintf(fileGetStderr(), "Renaming %s to %s.\n", filename, buf);
			if(fileMove(filename, buf) != 0 && isProductionMode()) AssertOrAlert("OBJECTDB_RENAME_FAILED", "Failed Renaming %s to %s.\n", filename, buf);
			filename = buf;
			fprintf(fileGetStderr(), "Replaying incremental log: %s\n", filename);
		}
		else if (strEndsWith(filename, "dump"))
			fprintf(fileGetStderr(), "Loading dump file: %s\n", filename);
		else
			fprintf(fileGetStderr(), "Replaying log file of unknown type: %s\n", filename);

		log_printf(LOG_CONTAINER,"Replaying log file: %s\n", filename);

		do {
			int linenum = 0;
			bool didPrint = false;

			//eString for processing lines
			char *line = NULL;

			//eString for accumulating commands
			static char *trans = NULL;
			static U64 seq = 0;
			static U32 timestamp = 0;
			U64 seqParse = 0;
			U32 timestampParse = 0;
			
			estrStackCreateSize(&line, 150);

			if (!trans)
				estrHeapCreate(&trans, 4500,0);

			while (logReadLine(*file, &line))
			{
				linenum++;

				estrTrimLeadingAndTrailingWhitespace(&line);
				if (estrLength(&line) == 0) continue;

				if ( (seqParse = parseSeq(&line)) != 0 )
				{
					timestampParse = parseTime(&line, gContainerSource.strictMerge);
					if ( timestamp == PARSE_TIME_FAIL )
					{
						Alertf("Failed to parse log file: %s\nline %d: %s", filename, linenum, line);
					}
					else
					{
						bool print = (!didPrint || seq % 1000 == 0);
						assert(!seq || !gContainerSource.sourceInfo->iLastSequenceNumber || seq == gContainerSource.sourceInfo->iLastSequenceNumber + 1);
						if (seq > gContainerSource.sourceInfo->iLastSequenceNumber)
						{
							didPrint = true;
							objReplayTransaction(trans, seq, timestamp, print);
							transactionsReplayed++;
							if (print) fprintf(fileGetStderr(), "                                                                        \r");
						}
						/*else if (seq > 0)
						{
							assertmsgf(0,"Bad log playback, lastloaded = %"FORM_LL"d, requested = %"FORM_LL"d. line = %s", gContainerSource.sourceInfo->iLastSequenceNumber, seq, trans);
						}*/
						estrClear(&trans);

						seq = seqParse;
						timestamp = timestampParse;
					}
				}
				estrAppend(&trans, &line);
				estrConcat(&trans, "\n", 1);
				estrClear(&line);

				if(timerCpuTicks64() - timerStartTime >= processTimeTicks)
				{
					estrDestroy(&line);
					return transactionsReplayed;
				}
			}
			
			//the last one.
			if (seq > gContainerSource.sourceInfo->iLastSequenceNumber)
			{
				objReplayTransaction(trans, seq, timestamp, true);
				transactionsReplayed++;
				fprintf(fileGetStderr(), "\n");
			}

			seq = 0;
			timestamp = 0;
			estrDestroy(&trans);
			estrDestroy(&line);
			
		} while (false);
		fclose(*file);
		*file = NULL;
	}
	else
	{
		Errorf("Could not open log file for replay:%s\n", filename);
	}

	return transactionsReplayed;
}

int objReplayLog(char *filename)
{
	char buf[MAX_PATH];
	FileWrapper *file;
	int transactionsReplayed = 0;

	file = fopen(filename, "r!");
	if(file)
	{
		if(gContainerSource.logFileReplayCB && !gContainerSource.logFileReplayCB(filename))
		{
			fclose(file);
			return 0;
		}

		if (strEndsWith(filename, "log"))
			fprintf(fileGetStderr(), "Replaying incremental log: %s\n", filename);
		else if (!gContainerSource.createSnapshot && strEndsWith(filename, "lcg"))
		{
			char *ext = NULL;
			strcpy(buf, filename);
			ext = FindExtensionFromFilename(buf);
			ext[0] = '\0';
			strcat(buf, ".log");
			fprintf(fileGetStderr(), "Renaming %s to %s.\n", filename, buf);
			if(fileMove(filename, buf) != 0 && isProductionMode()) AssertOrAlert("OBJECTDB_RENAME_FAILED", "Failed Renaming %s to %s.\n", filename, buf);
			filename = buf;
			fprintf(fileGetStderr(), "Replaying incremental log: %s\n", filename);
		}
		else if (strEndsWith(filename, "dump"))
			fprintf(fileGetStderr(), "Loading dump file: %s\n", filename);
		else
			fprintf(fileGetStderr(), "Replaying log file of unknown type: %s\n", filename);

		log_printf(LOG_CONTAINER,"Replaying log file: %s\n", filename);

		do {
			int linenum = 0;
			bool didPrint = false;

			//eString for processing lines
			char *line = NULL;

			//eString for accumulating commands
			char *trans = NULL;
			U64 seqParse, seq = 0;
			U32 timestampParse, timestamp = 0;
			
			estrStackCreateSize(&line, 150);
			estrStackCreateSize(&trans, 4500);

			while (logReadLine(file, &line))
			{
				linenum++;

				estrTrimLeadingAndTrailingWhitespace(&line);
				if (estrLength(&line) == 0) continue;

				if ( (seqParse = parseSeq(&line)) != 0 )
				{
					timestampParse = parseTime(&line, gContainerSource.strictMerge);
					if ( timestamp == PARSE_TIME_FAIL )
					{
						Alertf("Failed to parse log file: %s\nline %d: %s", filename, linenum, line);
					}
					else
					{
						bool print = (!didPrint || seq % 1000 == 0);
						if (!gContainerSource.sourceInfo || seq > gContainerSource.sourceInfo->iLastSequenceNumber)
						{
							if(!nullStr(trans))
							{
								didPrint = true;
								objReplayTransaction(trans, seq, timestamp, print);
								transactionsReplayed++;
								if (print) fprintf(fileGetStderr(), "                                                                        \r");
							}
						}
						/*else if (seq > 0)
						{
							assertmsgf(0,"Bad log playback, lastloaded = %"FORM_LL"d, requested = %"FORM_LL"d. line = %s", gContainerSource.sourceInfo->iLastSequenceNumber, seq, trans);
						}*/
						estrClear(&trans);

						seq = seqParse;
						timestamp = timestampParse;
					}
				}
				estrAppend(&trans, &line);
				estrConcat(&trans, "\n", 1);
				estrClear(&line);
			}
			
			//the last one.
			if (!gContainerSource.sourceInfo || seq > gContainerSource.sourceInfo->iLastSequenceNumber)
			{
				if(!nullStr(trans))
				{
					objReplayTransaction(trans, seq, timestamp, true);
					transactionsReplayed++;
				}
				fprintf(fileGetStderr(), "\n");
			}

			estrDestroy(&trans);
			estrDestroy(&line);
			
		} while (false);
		fclose(file);
	}
	else
	{
		Errorf("Could not open log file for replay:%s\n", filename);
	}

	return transactionsReplayed;
}

AUTO_STRUCT;
typedef struct ReplayTransactionData
{
	U64 sequence;
	U32 timestamp;
	char *estrTransaction;	 AST(ESTRING)
	char *estrBufferedLine;	 AST(ESTRING)
} ReplayTransactionData;

static ReplayTransactionData *objReadReplayTransactionData(FileWrapper *file, ReplayTransactionData *pPreviousReplayTransactionData)
{
	char *line = NULL;
	char *trans = NULL;
	char *fullLine = NULL;
	ReplayTransactionData *pReplayTransactionData = NULL;
	U32 timestamp = 0;
	U64 seq = 0;

	estrStackCreateSize(&line, 150);
	estrStackCreateSize(&fullLine, 150);
	estrStackCreateSize(&trans, 1000);

	while((pPreviousReplayTransactionData && !nullStr(pPreviousReplayTransactionData->estrBufferedLine)) || logReadLine(file, &line))
	{
		U64 seqParse = 0;

		if(pPreviousReplayTransactionData && !nullStr(pPreviousReplayTransactionData->estrBufferedLine))
		{
			estrCopy2(&line, pPreviousReplayTransactionData->estrBufferedLine);
			estrDestroy(&pPreviousReplayTransactionData->estrBufferedLine);
		}

		estrTrimLeadingAndTrailingWhitespace(&line);
		if(estrLength(&line) == 0) continue;
		estrCopy2(&fullLine, line);

		if((seqParse = parseSeq(&line)) != 0)
		{
			U32 timestampParse = 0;
			if((timestampParse = parseTime(&line, gContainerSource.strictMerge)) != PARSE_TIME_FAIL)
			{
				if(!nullStr(trans))
				{
					pReplayTransactionData = StructCreate(parse_ReplayTransactionData);
					pReplayTransactionData->sequence = seq;
					pReplayTransactionData->timestamp = timestamp;
					estrCopy2(&pReplayTransactionData->estrBufferedLine, fullLine);
					estrCopy2(&pReplayTransactionData->estrTransaction, trans);
					estrClear(&trans);
					break;
				}

				seq = seqParse;
				timestamp = timestampParse;
			}
		}

		estrAppend(&trans, &line);
		estrConcat(&trans, "\n", 1);
		estrClear(&line);
	}

	// last one
	if(!nullStr(trans))
	{
		pReplayTransactionData = StructCreate(parse_ReplayTransactionData);
		pReplayTransactionData->sequence = seq;
		pReplayTransactionData->timestamp = timestamp;
		estrCopy2(&pReplayTransactionData->estrTransaction, trans);
	}

	estrDestroy(&line);
	estrDestroy(&fullLine);
	estrDestroy(&trans);

	if(pPreviousReplayTransactionData)
		StructDestroySafe(parse_ReplayTransactionData, &pPreviousReplayTransactionData);

	return pReplayTransactionData;
}

int objReplayLogThrottled(char *filename, FileWrapper **file, int iMaxTransactions, bool bReplaySyntheticLogDataInRealtime, U32 iStopSyntheticReplayAtTimestamp)
{
	static ReplayTransactionData *pReplayTransactionData = NULL;

	int transactionsReplayed = 0;

	if(!*file)
	{
		if(strEndsWith(filename, ".gz"))
			*file = fopen(filename, "rzb");
		else
			*file = fopen(filename, "rb");
		if(*file)
		{
			if(gContainerSource.logFileReplayCB && !gContainerSource.logFileReplayCB(filename))
			{
				if(*file)
				{
					fclose(*file);
					*file = NULL;
				}
				return 0;
			}

			fprintf(fileGetStderr(), "\nReplaying synthetic log file: %s\n", filename);
		}
		else
		{
			Errorf("\nCould not open synthetic log file for replay: %s\n", filename);
		}
	}

	if(*file)
	{
		static bool lastReplaySyntheticLogDataInRealtime = false;
		static U32 lastTime = 0;
		static U32 lastTimestamp = 0;

		if(bReplaySyntheticLogDataInRealtime && bReplaySyntheticLogDataInRealtime > lastReplaySyntheticLogDataInRealtime)
		{
			lastTime = 0;
			lastTimestamp = 0;
		}
		lastReplaySyntheticLogDataInRealtime = bReplaySyntheticLogDataInRealtime;

		while((pReplayTransactionData && !nullStr(pReplayTransactionData->estrTransaction)) || (pReplayTransactionData = objReadReplayTransactionData(*file, pReplayTransactionData)))
		{
			bool bPlay = true;
			U32 thisTime = 0;

			if(bReplaySyntheticLogDataInRealtime)
			{
				thisTime = timerCpuSeconds();
				bPlay = (0 == lastTime || 0 == lastTimestamp || (thisTime - lastTime) >= (pReplayTransactionData->timestamp - lastTimestamp));
			}
			else if(iMaxTransactions > 0)
			{
				bPlay = (transactionsReplayed < iMaxTransactions);
			}

			if(iStopSyntheticReplayAtTimestamp)
				bPlay = pReplayTransactionData->timestamp < iStopSyntheticReplayAtTimestamp;

			if(bPlay)
			{
				bool print = (pReplayTransactionData->sequence % 1000 == 0);
				objReplayTransaction(pReplayTransactionData->estrTransaction, pReplayTransactionData->sequence, pReplayTransactionData->timestamp, print);
				transactionsReplayed++;
				if(print) fprintf(fileGetStderr(), "                                                                        \r");

				estrDestroy(&pReplayTransactionData->estrTransaction);
				if(bReplaySyntheticLogDataInRealtime)
				{
					lastTimestamp = pReplayTransactionData->timestamp;
					lastTime = thisTime;
				}
			}
			else
				break;
		}

		if(!pReplayTransactionData || nullStr(pReplayTransactionData->estrTransaction))
		{
			if(pReplayTransactionData)
				StructDestroySafe(parse_ReplayTransactionData, &pReplayTransactionData);

			fclose(*file);
			*file = NULL;
		}
	}

	return transactionsReplayed;
}

static int objLoadReplayLogs(IncrementalLoadState *loadState, bool dbupgrade)
{
	int replayedTransactions = 0;
	IncrementalLogRef **ppLogBatch = NULL;
	PERFINFO_AUTO_START("objLoadIncrementalHogs:LogReplay",1);
	//Replay late logs
	eaCreate(&ppLogBatch);
	if (eaSize(&loadState->ppLogs) > 0 && gContainerSource.replayCB && !dbupgrade)
	{
		__time32_t cutofftime = _time32(NULL) - timeSecondsSince2000() + gContainerSource.scanSnapshotTime - gContainerSource.incrementalHoursToKeep * 3600;
		U64 lastSeq = (U64)-1;
		IncrementalLogRef *lastLog;

		fprintf(fileGetStderr(), "Replaying logs...\n");

		//Work backwards to the log ending just after the last hog seq#.
		do {
			lastLog = eaPop(&loadState->ppLogs);
			lastSeq = lastLog->sequenceNum;
			eaPush(&ppLogBatch, lastLog);
		} while (lastSeq > gContainerSource.sourceInfo->iLastSequenceNumber + 1 && eaSize(&loadState->ppLogs) > 0);
		
		//Clean up old logs
		while (eaSize(&loadState->ppLogs) > 0)
		{
			lastLog = eaPop(&loadState->ppLogs);
			if (lastLog->time_write < cutofftime)
			{
				FileRenameRequest *mv = callocStruct(FileRenameRequest);
				mv->filename = strdup(lastLog->filename);
				mv->moveToOld = true;
				eaPush(&loadState->renames, mv);
				//objMoveToOldFilesDir(lastLog->filename);
			}
			StructDestroy(parse_IncrementalLogRef, lastLog);
		}

		//If we found any logs, replay them. Don't clean up here in case the replay fails.
		if (eaSize(&ppLogBatch) > 0)
		{
			if (lastSeq > gContainerSource.sourceInfo->iLastSequenceNumber + 1)
				Errorf("Cannot replay logs. Earliest log seq#:%"FORM_LL"u\n", lastSeq);
			
			while (eaSize(&ppLogBatch) > 0)
			{
				lastLog = eaPop(&ppLogBatch);

				replayedTransactions += objReplayLog(lastLog->filename);

				StructDestroy(parse_IncrementalLogRef, lastLog);
			}
		}
	}
	else if (eaSize(&loadState->ppLogs) > 0 && !gContainerSource.replayCB)
	{
		Errorf("Replay callback must be set to replay logs!\n");
	}

	eaDestroyStruct(&ppLogBatch, parse_IncrementalLogRef);

	PERFINFO_AUTO_STOP();
	return replayedTransactions;
}

static void RenameFiles(FileRenameRequest ***renames)
{
	assert(renames);
	if(eaSize(renames))
	{
		FileRenameRequest *mv = NULL;
		fprintf(fileGetStderr(), "Moving incremental files.\n");
		while (mv = eaPop(renames))
		{
			if (mv->filename)
			{
				if (mv->newname)
				{
					CONTAINER_LOGPRINT("Renaming %s to %s.\n", mv->filename, mv->newname);
					if (fileMove(mv->filename, mv->newname) != 0)
					{
						CONTAINER_LOGPRINT("Failed Renaming %s to %s.\n", mv->filename, mv->newname);
						if (isProductionMode())
							AssertOrAlert("OBJECTDB_RENAME_FAILED","Failed Renaming %s to %s. ", mv->filename, mv->newname);
					}
				}
				if (mv->moveToOld)
				{
					char *filename = mv->newname?mv->newname:mv->filename;
					fprintf(fileGetStderr(), "Moving %s to old directory.\n", filename);
					objMoveToOldFilesDir(filename);
				}
				fprintf(fileGetStderr(), "\n");
				free(mv->filename);
			}
			if (mv->newname) free(mv->newname);
			free(mv);
		}
		eaDestroy(renames);
	}
}

static void objLoadIncrementalHogs()
{
	IncrementalLoadState loadState = {0, 0, 0, NULL, NULL, GLOBALTYPE_NONE};
	char *lastSnapshot = 0;
	char *databaseDir = NULL;
	bool createSnapshot = false;
	bool snapshotLoaded = false;
	bool dbupgrade = false;
	char conInfo[MAX_PATH];
	int replayedTransactions = 0;

	PERFINFO_AUTO_START("objLoadIncrementalHogs:HogMerge",1);

	eaCreate(&loadState.ppLogs);
	eaCreate(&loadState.renames);

	estrStackCreate(&databaseDir);
	estrCopy2(&databaseDir, gContainerSource.sourcePath);
	//stupid destructive getter function.
	getDirectoryName(databaseDir);
	estrSetSize(&databaseDir, (unsigned int)strlen(databaseDir));

	fprintf(fileGetStderr(), "Loading incremental hogs from:%s \n", databaseDir);
	fprintf(fileGetStderr(), "Multi-threaded Load Enabled.");
	fprintf(fileGetStderr(), "\n");

	//This should block all container persist updates until we're done merging.
	EnterCriticalSection(&gContainerSource.hog_access);
	do {
		//Find the last snapshot.
		fileScanAllDataDirs(databaseDir, SnapshotHogFileScanCallback, &lastSnapshot);
		if (lastSnapshot)
		{
			fprintf(fileGetStderr(), "Loading snapshot:%s ...", lastSnapshot);
			log_printf(LOG_CONTAINER,"Loading snapshot:%s ...", lastSnapshot);
			if (objLoadIncrementalHogFileEx(lastSnapshot,&loadState,true)) 
			{
				gContainerSource.scanSnapshotTime = gContainerSource.sourceInfo->iCurrentTimeStamp;
				gContainerSource.scanSnapshotSeq = gContainerSource.sourceInfo->iCurrentSequenceNumber;
				snapshotLoaded = true;
				fprintf(fileGetStderr(), "done.\n\n");
			}
			else
			{
				ErrorOrAlert("DATABASE.LOADFAILURE", "Could not load snapshot file %s.", lastSnapshot);
			}
		}
		else
		{
			if (isProductionMode())
				ErrorOrCriticalAlert("DATABASE.LOADFAILURE", "Could not find a suitable snapshot. If this a fresh shard you can ignore this alert.");
		}

		//Scan the logs, merge the hogs, feed the frogs.
		fileScanAllDataDirs(databaseDir, IncrementalHogFileLoadCallback, &loadState);
		fprintf(fileGetStderr(), "%d incremental hogs merged. Last sequence number was %"FORM_LL"u.\n", loadState.fileCount, gContainerSource.sourceInfo->iLastSequenceNumber);


		//If there's no hogs and no logs, then replay the db.log log if it exists.
		if (loadState.fileCount == 0 && eaSize(&loadState.ppLogs) == 0 && !snapshotLoaded)
		{
			sprintf(conInfo,"%s",gContainerSource.sourcePath);
			getDirectoryName(conInfo);
			strcat(conInfo, "/");
			strcat(conInfo, CONTAINER_INFO_FILE);

			if (fileExists(conInfo))
			{
				dbupgrade = true;
				createSnapshot = true;
			}
		}
		else if (loadState.containersLoaded != loadState.containersRequested)
		{
			TriggerAlertf("OBJECTDB.PARTIAL_LOAD_FAILURE",
				(gContainerSource.createSnapshot?ALERTLEVEL_WARNING:ALERTLEVEL_CRITICAL),ALERTCATEGORY_NETOPS,
				0, 0, 0, GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0,
				"Could not load all containers (loaded %"FORM_LL"d of %"FORM_LL"d) from incremental hogs.\n"
				"See CONTAINER log for load errors.\n", 
				loadState.containersLoaded, loadState.containersRequested);
		}
	

		if (!gContainerSource.incrementalHog)
		{
			if (gContainerSource.createSnapshot)
			{
				char *workhog = 0;
				//Overcomplicated method to create
				objMakeIncrementalFilename(&workhog, timeSecondsSince2000(), NULL); objConvertToPendingFilename(workhog); objConvertToSkippedFilename(workhog);
				gContainerSource.incrementalHog = objOpenIncrementalHogFile(workhog);
			}
			else
			{
				objInitIncrementalHogRotation();
			}
		}

		if (dbupgrade)
		{
			Alertf("ObjectDB did not find any incremental data to load.\n Attempting to load static data:%s.\n Invalid containers will not be loaded.\n No futher action is required to update your database.\nThank you and have a nice day.", conInfo);
			objUpgradeNonHoggedDatabase();
			objSaveAllContainers();
		}


		PERFINFO_AUTO_STOP();
	} while (false);
	LeaveCriticalSection(&gContainerSource.hog_access);

	replayedTransactions = objLoadReplayLogs(&loadState, dbupgrade);

	//clean up
	eaDestroyStruct(&loadState.ppLogs,parse_IncrementalLogRef);

	makeDirectories(databaseDir);

	if (gContainerSource.createSnapshot || createSnapshot)
	{
		//Flush the work hog or the incremental hog of replays
		if (gContainerSource.incrementalHog) 
			hogFileModifyFlush(gContainerSource.incrementalHog);

		//Create Snapshot if there were changes or there was no snapshot loaded.
		if (gContainerSource.forceSnapshot || replayedTransactions > 0 || loadState.fileCount > 0 || !snapshotLoaded)
		{
			if (!gContainerSource.forceSnapshot && (gContainerSource.sourceInfo->iCurrentSequenceNumber <= gContainerSource.scanSnapshotSeq && lastSnapshot))
			{
				fprintf(fileGetStderr(), "Skipping snapshot creation, previous snapshot is up to date: %s\n", lastSnapshot);
			}
			else
			{
				objSaveSnapshotHog(gContainerSource.createSnapshot);
				gContainerSource.scanSnapshotTime = gContainerSource.sourceInfo->iCurrentTimeStamp;
				gContainerSource.scanSnapshotSeq = gContainerSource.sourceInfo->iCurrentSequenceNumber;
			}
		}
		else
		{
			fprintf(fileGetStderr(), "Skipping snapshot creation, existing last snapshot:%s\n", lastSnapshot);
		}
	}
	
	if (gContainerSource.dumpWebData)
	{
		fprintf(fileGetStderr(), "Dumping web data...");
		objSaveWebDataToFile(gContainerSource.dumpWebData);		
	}

	RenameFiles(&loadState.renames);

	estrConcatf(&databaseDir, "/%s", "old");
	objPruneOldDirectory(databaseDir);

	estrDestroy(&databaseDir);

	if (gContainerSource.createSnapshot)
	{
		fprintf(fileGetStderr(), "Waiting for IO to complete.\n");

		objFlushContainers();

		objCloseAllWorkingFiles();

		if (gContainerSource.generateLogReport)
		{
			fprintf(fileGetStderr(), "Generating log report.\n");
			objGenerateLogReport();
		}

		fprintf(fileGetStderr(), "Snapshot created... exiting in 30 seconds.\n");
		Sleep(30000);
	}
}


static FileScanAction ScanLogForQueriesCallback(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char fdir[MAX_PATH];
	StringQueryList *list = (StringQueryList*)pUserData;
	FILE *file;
	char *line = 0;
	char *trans = 0;
	U32 linenum = 0;
	U32 currentTime = timeSecondsSince2000();

	// Ignore all directories.
	if(data->attrib & _A_SUBDIR)
	{
		return FSA_NO_EXPLORE_DIRECTORY;
	}

	if (!objStringIsPendingLogname(data->name))
	{
		return FSA_NO_EXPLORE_DIRECTORY;
	}

	strcpy(fdir, dir);
	strcat(fdir, "/");
	strcat(fdir, data->name);

	file = fopen(fdir, "r");
	if (!file)
	{
		Errorf("Could not open log file for scanning:%s\n", fdir);
	}

	estrStackCreateSize(&line, 150);

	while (logReadLine(file, &line))
	{
		U64 seqParse, seq = 0;
		U32 timestamp = 0;
		linenum++;

		estrTrimLeadingAndTrailingWhitespace(&line);
		if (estrLength(&line) == 0) continue;

		if ( (seqParse = parseSeq(&line)) != 0 )
		{
			timestamp = parseTime(&line, true);
			if ( timestamp == PARSE_TIME_FAIL )
			{
				Alertf("Failed to parse log file: %s\nline %d: %s", fdir, linenum, line);
			}
			else if (!gContainerSource.createSnapshot || currentTime - gContainerSource.incrementalHoursToKeep * 3600 < timestamp)
			{
				EARRAY_FOREACH_BEGIN(list->eaCommandList,i);
				{
					if (strStartsWith(trans, list->eaCommandList[i]))
					{
						eaPush(&list->eaList, strdup(trans));
						ea32Push(&list->eaTimes, timestamp);
						break;
					}
				}
				EARRAY_FOREACH_END;

				estrClear(&trans);
			}
		}
		estrAppend(&trans, &line);
		estrConcat(&trans, "\n", 1);
		estrClear(&line);
	}
	fclose(file);
	
	estrDestroy(&line);
	estrDestroy(&trans);
	
	return FSA_NO_EXPLORE_DIRECTORY;
}

static void objUpgradeNonHoggedDatabase()
{
	char fileName[MAX_PATH];
	sprintf(fileName,"%s",gContainerSource.sourcePath);
	getDirectoryName(fileName);
	strcat(fileName, "/");
	strcat(fileName, CONTAINER_INFO_FILE);

	if (fileExists(fileName))
	{
		if (!ParserReadTextFile(fileName, parse_ContainerRepositoryInfo, gContainerSource.sourceInfo, 0))
		{
			Alertf("Could not load non-hogged database. ContainerInfo file was invalid.");
			return;
		}
	}
	else
	{
		Alertf("Could not load non-hogged database. ContainerInfo file was not found.");
	}

	EnterCriticalSection(&gContainerSource.hog_access);
	strcpy(fileName, gContainerSource.sourcePath);
	getDirectoryName(gContainerSource.sourcePath);
	gContainerSource.useHogs = false;
	fileScanAllDataDirs(gContainerSource.sourcePath, ContainerFileLoadCallback, NULL);
	gContainerSource.useHogs = true;
	strcpy(gContainerSource.sourcePath, fileName);
	LeaveCriticalSection(&gContainerSource.hog_access);
}

static void objIncrementalLoadAllContainers(void)
{
	if (!gContainerSource.validSource)
	{
		return;
	}
	if (!gContainerSource.useHogs)
	{
		char fileName[MAX_PATH];
		sprintf(fileName,"%s/%s",gContainerSource.sourcePath,CONTAINER_INFO_FILE);

		if (fileExists(fileName))
		{
			ParserReadTextFile(fileName, parse_ContainerRepositoryInfo, gContainerSource.sourceInfo, 0);
		}

		fileScanAllDataDirs(gContainerSource.sourcePath, ContainerFileLoadCallback, NULL);
	}
	else
	{		
		if (!gContainerSource.ignoreContainerSource && (gContainerSource.incrementalMinutes || gContainerSource.createSnapshot))
		{
			//reset the container source info;
			gContainerSource.sourceInfo->iCurrentSequenceNumber = 0;
			gContainerSource.sourceInfo->iCurrentTimeStamp = 0;
			gContainerSource.sourceInfo->iLastSequenceNumber = 0;
			gContainerSource.sourceInfo->iLastTimeStamp = 0;

			objLoadIncrementalHogs();
		}
		else
		{
			gContainerSource.dumpload = true;
			objLoadContainersFromHoggForDump(gContainerSource.sourcePath, gContainerSource.dumpContainerType);
		}
	}
}

void objSetIncrementalHoursToKeep(U32 hours)
{
	gContainerSource.incrementalHoursToKeep = hours;
}

void objSetCommandLogFileReplayCallback(CommandLogFileReplayCB logFileReplayCB)
{
	gContainerSource.logFileReplayCB = logFileReplayCB;
}

void objSetCommandReplayCallback(CommandReplayCB replayCB)
{
	gContainerSource.replayCB = replayCB;
}

void objLoadAllContainers(void)
{
	// Any process that loads a CrypticDB should do memops logging.
	enableMemTrackOpsLogging();

	objIncrementalLoadAllContainers();
}

static FileScanAction ReplayPendingLogsCallback(char* dir, struct _finddata32_t* data, IncrementalLoadState *state)
{
	char fdir[MAX_PATH];

	strcpy(fdir, dir);

	if (objStringIsPendingLogname(data->name))
	{
		FILE *file;
		char buf[20];

		strcat(fdir, "/");
		strcat(fdir, data->name);

		file = fopen(fdir, "r");
		if (file)
		{
			IncrementalLogRef *logref = NULL;
			if (fread(buf, 20, 1, file))
			{ 
				logref = StructCreate(parse_IncrementalLogRef);
				logref->sequenceNum = atoi64(buf);
				logref->time_write = data->time_write;
				estrPrintf(&logref->filename, "%s", fdir);
					
				eaPush(&(state->ppLogs), logref);
			}
			fclose(file);

		}
		else
			Errorf("Could not open log file for merging:%s\n", fdir);
	}
	return FSA_NO_EXPLORE_DIRECTORY;
}

IncrementalLoadState replayLoadState;

void objFindLogsToReplay()
{
	IncrementalLogRef **ppLogBatch = NULL;
	char *databaseDir = NULL;

	if(eaSize(&replayLoadState.ppLogs))
		return;

	eaCreate(&replayLoadState.ppLogs);
	eaCreate(&replayLoadState.renames);

	estrStackCreate(&databaseDir);
	estrCopy2(&databaseDir, gContainerSource.sourcePath);
	//stupid destructive getter function.
	getDirectoryName(databaseDir);
	estrSetSize(&databaseDir, (unsigned int)strlen(databaseDir));
	fileScanAllDataDirs(databaseDir, ReplayPendingLogsCallback, &replayLoadState);

	estrDestroy(&databaseDir);
}

void objReplayLogs()
{
	static FileWrapper *file = NULL;
	PERFINFO_AUTO_START("objReplayLogs",1);

	if (eaSize(&replayLoadState.ppLogs) > 0)
	{
		while (eaSize(&replayLoadState.ppLogs) > 0)
		{
			static IncrementalLogRef *lastLog;
			if(!file)
			{
				StructDestroy(parse_IncrementalLogRef, lastLog);
				lastLog = eaRemove(&replayLoadState.ppLogs, 0);
			}

			objReplayLogWithRestart(lastLog->filename, &file);
		}
	}
	PERFINFO_AUTO_STOP();
}

typedef struct SaveWebDataInfo
{
	FILE *fout;
	char *buf;
	char *resultString;
	int minimumLevel;
} SaveWebDataInfo;

void SaveWebDataToFile_ForEachContainer(Container *temp, SaveWebDataInfo *info)
{
	//object paths
	char * level_path = ".pInventoryV2.ppLiteBags[Numeric].ppIndexedLiteSlots[Level].count";
	char * charname_path = ".Psaved.Savedname";
	char * displayname_path = ".Pplayer.Displayname";

	ParseTable *table;
	int col, ind;
	void *strptr;
	TextParserResult result = PARSERESULT_SUCCESS;
	int level;
	const char *cname;
	const char *dname;

	estrClear(&info->buf);

	//Get the level
	if (!ParserResolvePath(level_path, temp->containerSchema->classParse, temp->containerData, &table, &col, &strptr, &ind, &info->resultString, NULL, 0))
	{
		estrPrintf(&info->buf, "# Error getting level for container: %d; %s\n", temp->containerID, info->resultString);
		fprintf(info->fout, "%s", info->buf);
		return;
	}
	level = TokenStoreGetInt(table, col, strptr, ind, &result);
	if (result < PARSERESULT_SUCCESS)
	{
		estrPrintf(&info->buf, "# Error getting level for container: %d; %d\n", temp->containerID, result);
		fprintf(info->fout, "%s", info->buf);
		return;
	}
	if (level < info->minimumLevel) return;
				
	//Get the charname
	if (!ParserResolvePath(charname_path, temp->containerSchema->classParse, temp->containerData, &table, &col, &strptr, &ind, &info->resultString, NULL, 0))
	{
		estrPrintf(&info->buf, "# Error getting character name for container: %d; %s\n", temp->containerID, info->resultString);
		fprintf(info->fout, "%s", info->buf);
		return;
	}
	cname = TokenStoreGetString(table, col, strptr, ind, &result);
	if (result < PARSERESULT_SUCCESS)
	{
		estrPrintf(&info->buf, "# Error getting character name for container: %d; %d\n", temp->containerID, result);
		fprintf(info->fout, "%s", info->buf);
		return;
	}

	//Get the displayname
	if (!ParserResolvePath(displayname_path, temp->containerSchema->classParse, temp->containerData, &table, &col, &strptr, &ind, &info->resultString, NULL, 0))
	{
		estrPrintf(&info->buf, "# Error getting display name for container: %d; %s\n", temp->containerID, info->resultString);
		fprintf(info->fout, "%s", info->buf);
		return;
	}
	dname = TokenStoreGetString(table, col, strptr, ind, &result);
	if (result < PARSERESULT_SUCCESS)
	{
		estrPrintf(&info->buf, "# Error getting display name for container: %d; %d\n", temp->containerID, result);
		fprintf(info->fout, "%s", info->buf);
		return;
	}
				
	estrPrintf(&info->buf, "%s:%d:%s\n", cname, temp->containerID, dname);
	fprintf(info->fout, "%s", info->buf);

}

//charname:containerid:displayname
//  > 5
void objSaveWebDataToFile(char *file)
{
	SaveWebDataInfo info = {0};

	//char timestring[MAX_PATH];
	char *filepath = 0;

	info.minimumLevel = 6;

	if (!gContainerSource.validSource)
	{
		return;
	}
	PERFINFO_AUTO_START("SaveWebDataToFile",1);

	//make the filename
	estrStackCreate(&filepath);
	//timeMakeFilenameDateStringFromSecondsSince2000(timestring, timeSecondsSince2000());
	estrPrintf(&filepath, "%s.txt", file);

	//open the file
	info.fout = fileOpen(filepath, "w");

	fprintf(fileGetStderr(), "Writing Web data to: %s\n", filepath);

	//clean up filename
	estrDestroy(&filepath);
	if (!info.fout)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	estrStackCreate(&info.buf);
	estrStackCreate(&info.resultString);
	do 
	{
		ContainerStore *store = objFindContainerStoreFromType(GLOBALTYPE_ENTITYPLAYER);
		if (!store->containerSchema) 
		{
			Errorf("EntityPlayer container store did not have a valid schema!");
			continue;
		}

		ForEachContainerOfType(GLOBALTYPE_ENTITYPLAYER, SaveWebDataToFile_ForEachContainer, &info, true);
	} while (false);
	estrDestroy(&info.buf);
	estrDestroy(&info.resultString);

	//close the file
	fileClose(info.fout);


	PERFINFO_AUTO_STOP();
}

typedef struct SaveContainerRecord
{
	int storeCount;
	int saved;
	HogFile *snapshot;
} SaveContainerRecord;

void ForEachContainer_SaveContainer(Container *con, SaveContainerRecord *record)
{
	if (GlobalTypeSchemaType(con->containerType) == SCHEMATYPE_PERSISTED)
	{
		con->isModified = false;
		objSaveContainer(con);
		record->saved++;
		record->storeCount++;
	}
}

void objSaveAllContainers(void)
{
	SaveContainerRecord record = {0};
	U64 sequence;
	int i;
	if (!gContainerSource.validSource)
	{
		return;
	}
	PERFINFO_AUTO_START("objSaveAllContainers",1);
	record.saved = 0;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		ContainerStore *store = &gContainerRepository.containerStores[i];
		record.storeCount = 0;
		if (!store->containerSchema) continue;

		ForEachContainerOfType(i, ForEachContainer_SaveContainer, &record, true);

		if (store->containerSchema && record.storeCount)
		{
			ContainerSourceUpdate conup = {0};
			conup.hog = NULL;
			conup.type = i;
			wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINERSCHEMA, &conup, sizeof(ContainerSourceUpdate));
		}
	}

	objLockGlobalContainerRepository();
	ClearTrackedContainerArray(&gContainerRepository.modifiedEntityPlayers);
	ClearTrackedContainerArray(&gContainerRepository.modifiedEntitySavedPets);
	ClearTrackedContainerArray(&gContainerRepository.modifiedContainers);

	record.saved += objPermanentlyDeleteRemovedContainers();
	objUnlockGlobalContainerRepository();

	sequence = gContainerSource.sourceInfo->iCurrentSequenceNumber;
	wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSETSEQUENCENUMBER, &sequence, sizeof(sequence));

	if (record.saved) objSaveContainerSourceInfo(NULL);
	PERFINFO_AUTO_STOP();
}


AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
bool dbDumpSchemaToSQL(const char *namedtype, const char *to_dir)
{
	char *estr = NULL;
	char *filename = NULL;
	FileWrapper *outfile = NULL;
	SchemaSource *source = NULL;
	GlobalType type = NameToGlobalType(namedtype);
	if (!type) return false;

	source = objFindContainerSchemaSource(type);
	if(!source) return false;

	estrStackCreate(&filename);
	estrPrintf(&filename, "%s", to_dir);
	if (filename[estrLength(&filename) - 1] != '\\')
		estrConcatf(&filename, "\\");
	estrConcatf(&filename, "%s.sql", source->className);
	outfile = fopen(filename, "w");
	estrDestroy(&filename);

	if (!outfile) return false;
	
	fprintf(fileGetStderr(), "Dumping SQL schema for %s to %s.\n", namedtype, filename);

	estrCreate(&estr);
	if (source->parseTables)
	{
		ParseTableWriteSQL(&estr,source->parseTables[0],source->className);
	}
	else
	{
		ParseTableWriteSQL(&estr,source->staticTable,source->className);
	}

	fprintf(outfile, "%s", estr);

	fclose(outfile);

	estrDestroy(&estr);
	return true;
}

typedef struct DumpDatabaseToSQLInfo
{
	FILE *file;
	U32 saved;
	U64 uuids;
} DumpDatabaseToSQLInfo;

static void DumpContainerToSQL(Container *con, DumpDatabaseToSQLInfo *info)
{
	char *conbuf = NULL;

	estrStackCreate(&conbuf);
	ParserWriteSQL(&conbuf, con->containerSchema->classParse, con->containerData, &info->uuids, 0);

	fwrite(conbuf, sizeof(char), strlen(conbuf), info->file);
	info->saved++;
	estrDestroy(&conbuf);
}

AUTO_COMMAND ACMD_CATEGORY(Debug, ObjectDB) ACMD_ACCESSLEVEL(9);
char * objDumpDatabaseToSQL(const char *namedtype, char *path)
{
	DumpDatabaseToSQLInfo info = {0};
	char *conbuf = NULL;
	static char buf[1024];
	char fdir[MAX_PATH];
	ContainerStore *store;
	GlobalType type = NameToGlobalType(namedtype);
	if (!type) return "Bad named type.";

	store = objFindContainerStoreFromType(type);
	if (!store)
		return "Type had no schema.";

	estrCreate(&conbuf);

	strcpy(fdir, path);
	strcat(fdir, "/");
	getDirectoryName(fdir);

	estrPrintf(&conbuf, "%s_dump.sql", namedtype);

	makeDirectories(fdir);

	strcat(fdir, "/");
	strcat(fdir, getFileName(conbuf));

	if (fileIsAbsolutePath(fdir))
		info.file = fopen(fdir, "w");
	else
		info.file = fileOpen(fdir, "w");

	if (!info.file)
	{
		estrDestroy(&conbuf);
		sprintf(buf, "Could not open file for writing: %s\n", fdir);
		return buf;
	}

	estrClear(&conbuf);

	fprintf(fileGetStderr(), "Dumping all containers to sql text.\n");

	PERFINFO_AUTO_START("objSaveAllContainers",1);

	ForEachContainerOfType(type, DumpContainerToSQL, &info, true);

	estrDestroy(&conbuf);

	fclose(info.file);

	sprintf(buf, "\n%d containers dumped to :%s.\n", info.saved, fdir);
	PERFINFO_AUTO_STOP();

	return buf;
}

typedef struct DumpDatabaseInfo
{
	FILE *file;
	char timestring[20];
	U32 saved;
} DumpDatabaseInfo;

static void DumpContainer(Container *con, DumpDatabaseInfo *info)
{
	char *conbuf = NULL;
	estrStackCreate(&conbuf);
	estrPrintf(&conbuf, "%u %s: dbUpdateContainer %u %u ",
		info->saved+1,
		info->timestring,
		con->containerType,
		con->containerID);

	StructTextDiffWithNull_Verify(&conbuf, con->containerSchema->classParse, con->containerData, "", 0, 0, 0, 0);

	estrConcatf(&conbuf, "\r\n");

	fwrite(conbuf, sizeof(char), strlen(conbuf), info->file);

	info->saved ++;
	if (info->saved % 100 == 0)
		fprintf(fileGetStderr(), "\r%d containers saved.", info->saved);
	estrDestroy(&conbuf);
}

AUTO_COMMAND ACMD_CATEGORY(Debug, ObjectDB) ACMD_ACCESSLEVEL(9);
char * objDumpDatabase(char *path)
{
	DumpDatabaseInfo info = {0};
	int i;
	char *conbuf = NULL;
	static char buf[1024];
	char fdir[MAX_PATH];
	estrCreate(&conbuf);

	timeMakeDateStringFromSecondsSince2000(info.timestring, timeSecondsSince2000());

	strcpy(fdir, path);
	strcat(fdir, "/");
	getDirectoryName(fdir);

	objMakeIncrementalFilename(&conbuf, timeSecondsSince2000(), "dump");
	objConvertToPendingFilename(conbuf);

	makeDirectories(fdir);

	strcat(fdir, "/");
	strcat(fdir, getFileName(conbuf));

	if (fileIsAbsolutePath(fdir))
		info.file = fopen(fdir, "w");
	else
		info.file = fileOpen(fdir, "w");

	if (!info.file)
	{
		estrDestroy(&conbuf);
		sprintf(buf, "Could not open file for writing: %s\n", fdir);
		return buf;
	}

	estrClear(&conbuf);

	fprintf(fileGetStderr(), "Dumping all containers to diff text.\n");
	
	PERFINFO_AUTO_START("objSaveAllContainers",1);
	info.saved = 0;

	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		int maxconsize = 0;
		ForEachContainerOfType(i, DumpContainer, &info, true);
	}

	estrDestroy(&conbuf);

	fclose(info.file);

	sprintf(buf, "\n%d containers dumped to :%s.\n", info.saved, fdir);
	PERFINFO_AUTO_STOP();

	return buf;
}

AUTO_COMMAND ACMD_CATEGORY(Debug, ObjectDB) ACMD_ACCESSLEVEL(9);
char * objLoadDatabaseDump(char *filename)
{
	U64 seq = gContainerSource.sourceInfo->iLastSequenceNumber;
	static char buf[1024];
	U32 result;

	if (!objStringIsDatabaseDumpname(filename))
		fprintf(fileGetStderr(), "%s does not appear to be an ObjectDB dump file.\n", filename);
	
	gContainerSource.sourceInfo->iLastSequenceNumber = 0;
	
	result = objReplayLog(filename);

	gContainerSource.sourceInfo->iLastSequenceNumber = seq + 1;
	objSaveAllContainers();

	sprintf(buf, "%u containers loaded from dump file: %s\n", result, filename);

	return buf;
}

void ForEachContainer_SaveContainerToHog(Container *con, SaveContainerRecord *record)
{
	if (GlobalTypeSchemaType(con->containerType) == SCHEMATYPE_PERSISTED)
	{
		verbose_printf("Adding container %s[%d] to snapshot:%s        \r", GlobalTypeToName(con->containerType), con->containerID, hogFileGetArchiveFileName(record->snapshot));
		objSaveContainerToHog(con, record->snapshot);
		record->saved++;
		record->storeCount++;
	}
}

void objSaveSnapshotHog(bool waitForFlush)
{
	SaveContainerRecord record = {0};
	char *filename = 0;
	int i;
	
	PERFINFO_AUTO_START("objSaveSnapshotHog",1);

	estrStackCreate(&filename);

	//objMakeSnapshotFilename(&filename, timeSecondsSince2000(), NULL);
	objMakeSnapshotFilename(&filename, objContainerGetTimestamp(), NULL);
	record.snapshot = objOpenIncrementalHogFile(filename);

	fprintf(fileGetStderr(), "Creating incremental snapshot: %s\n", filename);
	log_printf(LOG_CONTAINER,"Creating incremental snapshot: %s\n", filename);
	
	estrDestroy(&filename);


	do {
		record.saved = 0;
		for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
		{
			ContainerStore *store = objFindContainerStoreFromType(i);
			record.storeCount = 0;

			if (GlobalTypeSchemaType(i) == SCHEMATYPE_PERSISTED)
			{
				ForEachContainerOfType(i, ForEachContainer_SaveContainerToHog, &record, true);
			}

			if (store->containerSchema && record.storeCount)
			{
				ContainerSourceUpdate conup = {0};
				conup.hog = record.snapshot;
				conup.type = i;
				wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINERSCHEMA, &conup, sizeof(ContainerSourceUpdate));
			}
		}

		//Save the container source info last so we know that we've written all the containers.
		objSaveContainerSourceInfo(record.snapshot);

	} while (false);

	fprintf(fileGetStderr(), "\n Writing %d containers...", record.saved);

	objCloseHogOnHogThread(record.snapshot);
	if (waitForFlush) wtFlush(gContainerSource.hog_update_thread);

	fprintf(fileGetStderr(), "\n%d containers saved. done.\n", record.saved);

	PERFINFO_AUTO_STOP();
}

void objRotateIncrementalHog(void)
{
	//U32 currentTime = objContainerGetTimestamp();
	U32 currentTime;
	
	if (gContainerSource.createSnapshot)
		return;

	currentTime = timeSecondsSince2000();

	PERFINFO_AUTO_START_FUNC();

	if (currentTime > gContainerSource.nextRotateTime && !gContainerSource.rotateQueued) {
		objSaveModifiedContainersThreaded(true);

		// Finish last save if needed

		objFlushModifiedContainersThreaded();
	}

	PERFINFO_AUTO_STOP();
}

void objForceRotateIncrementalHog(void)
{
	PERFINFO_AUTO_START("objRotateIncrementalHog",1);

	if (gContainerSource.rotateQueued) return;

	gContainerSource.needsRotate = true;

	objSaveModifiedContainersThreaded(true);

	objFlushModifiedContainersThreaded();

	PERFINFO_AUTO_STOP();
}

void objScanLogsForQueries(StringQueryList *list)
{
	char scandir[MAX_PATH];
	strcpy(scandir, gContainerSource.sourcePath);
	fprintf(fileGetStderr(), "Scanning directory for Queries:%s\n\n", scandir);
	fileScanAllDataDirs(getDirectoryName(scandir), ScanLogForQueriesCallback, list);
}

void GenerateLineStats(StringQueryList *list, FileWrapper *out)
{
	LineCount **counts = NULL;
	LineCount **costs = NULL;
	int count = eaSize(&list->eaList);
	float ave = 0.0f;
	float stddev = 0.0f;
	float running_updates = 100.0f;
	float running_lines = 0.0f;
	float running_step = 90.0f;
	int lines = 0;
	int number = 0;
	int i;
	char **queries = NULL;
	char buf[1024];
	float totalTime;
	float cmdInFlight = 0.0f;
	float maxCmdSec = 0.0f;
	float maxUpdSec = 0.0f;
	int csBegin = 0;
	int csEnd = 0;
	bool csCont = true;

	if (!count)
	{
		fprintf(out, "No stats parsed from logs.\n");
		return;
	}

	eaCreate(&counts);
	
	for (i = 0; i < count; i++)
	{
		LineCount *lc = StructCreate(parse_LineCount);
		lc->index = i;
		lc->count = StringCountLines(list->eaList[i]);
		lc->time = list->eaTimes[i];
		eaPush(&counts, lc);
	}

	//Get time stats (crude local average)
	eaQSort(counts, CompareTimestamp);
	cmdInFlight = counts[0]->count;
	while (csEnd < count && csCont)
	{
		double window;
		while (counts[csEnd]->time - counts[csBegin]->time < 5 && csEnd < count-1)
		{
			csEnd++;
			cmdInFlight += counts[csEnd]->count;
		}
		//take the 5-ish second average
		window = (counts[csEnd]->time - counts[csBegin]->time);
		if (window < 1.0f) window = 1.0f;

		if (cmdInFlight/window > maxCmdSec) 
			maxCmdSec = cmdInFlight/window;
		if (((float)(csEnd-csBegin))/window > maxUpdSec)
			maxUpdSec = ((float)(csEnd-csBegin))/window;

		
		while ( counts[csEnd]->time - counts[csBegin]->time >= 5 && csBegin < csEnd)
		{
			cmdInFlight -= counts[csBegin]->count;
			csBegin++;
		}

		if (csEnd == count-1)
			csCont = false;
	}

	fprintf(out, "Stats for log range: %s", timeMakeDateStringFromSecondsSince2000(buf, counts[0]->time));
	fprintf(out, " to %s\n", timeMakeDateStringFromSecondsSince2000(buf, counts[count-1]->time));
	totalTime = counts[count-1]->time - counts[0]->time;

	//find min/max/median
	eaQSort(counts, CompareLineCount);

	eaCreate(&queries);
	eaSetCapacity(&queries, count);

	//reorder and find ave
	for (i = 0; i < count; i++)
	{
		int index = counts[i]->index;
		eaPush(&queries, list->eaList[index]);
		ave += counts[i]->count;
	}
	eaClear(&list->eaList);
	eaCopy(&list->eaList, &queries);
	ave = ave/count;

	//find stddev
	for (i = 0; i < count; i++)
	{
		float dev = ave - counts[i]->count;
		stddev += dev*dev;
	}
	stddev = sqrt(stddev/count);
	
	fprintf(out, "ObjectDB update stats for %d updates:\n", count);
	fprintf(out, "Average updates per second: %6.2f over %6.0f seconds\n", (count/totalTime), totalTime);
	fprintf(out, "Average commands per second: %6.2f\n", (count*ave/totalTime));
	fprintf(out, "Peak updates/commands per second: %6.2f/%6.2f (5 second interval)\n", maxUpdSec, maxCmdSec);
	fprintf(out, "Median commands per update: %d\n", counts[count/2]->count);
	fprintf(out, "Average commands per update: %6.2f; stddev: %6.2f\n", ave, stddev);
	fprintf(out, "Total commands: %d\n", (int)(count*ave));
	

	eaCreate(&costs);
	//histo
	for (i = 0; i < count; i++)
	{
		if (counts[i]->count != lines)
		{
			if (number)
			{
				LineCount *cost = StructCreate(parse_LineCount);
				float updatepct = number*100.0f/count;
				float linepct = number*lines*100.0f/(ave*count);
				fprintf(out, "%d update%s (%6.2f%% updates|%6.2f%% commands)", number, (number>1?"s":""), updatepct, linepct);

				cost->count = number * lines;
				cost->index = i;
				eaPush(&costs, cost);

				running_updates -= updatepct;
				running_lines += linepct;

				if (running_updates < running_step)
				{
					while (running_updates < running_step)
						running_step -= 5.0f;
					fprintf(out, "\t\t[top %2.0f%%-ile by size; %6.2f%% of commands]", running_updates, running_lines);
				}
			}
			lines = counts[i]->count;
			number = 1;
			fprintf(out, "\n\t%d lines:", lines);
		}
		else
		{
			number++;
		}
	}
	fprintf(out, "%d update%s (%6.2f%% updates|%6.2f%% commands)\n", number, (number>1?"s":""), number*100.0f/count, number*lines*100.0f/(ave*count));

	eaQSort(costs, CompareLineCount);
	fprintf(out, "\n\n\n 20 largest updates by cost:\n");
	for (i = 0; i < 20 && i < eaSize(&costs); i++)
	{
		fprintf(out, "========== #%d: %d aggregate lines ==========\n", i+1, costs[i]->count);
		fprintf(out, "%s\n", list->eaList[costs[i]->index]);
		fprintf(out, "<<<<<<<<<< #%d <<<<<<<<<<\n\n\n\n\n\n\n\n\n\n", i+1);
	}
	eaDestroyStruct(&costs, parse_LineCount);

	fprintf(out, "20 largest updates by size:\n");
	for (i = 0; i < 20 && i < count; i++)
	{
		fprintf(out, "========== #%d: %d lines ==========\n", i+1, counts[i]->count);
		fprintf(out, "%s\n", list->eaList[i]);
		fprintf(out, "<<<<<<<<<< #%d <<<<<<<<<<\n\n\n\n\n\n\n\n\n\n", i+1);
	}

	eaDestroyStruct(&counts, parse_LineCount);
}

void objCloseAllWorkingFiles(void)
{
	if(gContainerSource.hog_update_thread)
		wtFlush(gContainerSource.hog_update_thread);

	objCloseIncrementalHogFile();
	gContainerSource.nextRotateTime = UINT_MAX;
	gContainerSource.needsRotate = false;
	if (gContainerSource.incrementalShutdownCB)
	{
		gContainerSource.incrementalShutdownCB(NULL,NULL,NULL);
	}
}

void objGenerateLogReport(void)
{
	StringQueryList *list = StructCreate(parse_StringQueryList);
	char scandir[MAX_PATH];
	FileWrapper *outfile = NULL;
	strcpy(scandir, gContainerSource.sourcePath);

	eaPush(&list->eaCommandList, strdup("dbUpdateContainer "));
	eaPush(&list->eaCommandList, strdup("dbUpdateContainerOwner "));
	
	fileScanAllDataDirs(getDirectoryName(scandir), ScanLogForQueriesCallback, list);

	strcat(scandir, "\\dbUpdateReport.txt");
	outfile = fopen(scandir, "w");

	if (!outfile)
	{
		Alertf("Merger could not open report file: %s for writing.", scandir);
		return;
	}
	GenerateLineStats(list, outfile);

	fclose(outfile);
}


void objInitIncrementalHogRotation(void)
{
	if (!gContainerSource.nextRotateTime)
	{
		gContainerSource.lastRotateTime = gContainerSource.nextRotateTime;
		gContainerSource.nextRotateTime = 
			//objContainerGetTimestamp() 
			timeSecondsSince2000();
			//+ (gContainerSource.incrementalMinutes?gContainerSource.incrementalMinutes:5) * 60;
		gContainerSource.needsRotate = true;		
		objInternalRotateIncrementalHog();
	}
}

static U32 *pTimesOfPendingRotates = NULL;
static U32 iMaxNumberOfPendingRotates = 1;
AUTO_CMD_INT(iMaxNumberOfPendingRotates, MaxNumberOfPendingRotates) ACMD_COMMANDLINE;
static U32 iMaxDelayOfPendingRotates = 0;
AUTO_CMD_INT(iMaxDelayOfPendingRotates, MaxDelayOfPendingRotates) ACMD_COMMANDLINE;
static U32 iSlowSaveTickAlertThreshold = 0;
AUTO_CMD_INT(iSlowSaveTickAlertThreshold, SlowSaveTickAlertThreshold) ACMD_COMMANDLINE;

bool gbSlowRotateAlertSent = false;
bool gbSlowSaveTickAlertSent = false;

//Destroy container data on the main thread.
void mt_PopQueuedRotate(void *user_data, void *data, WTCmdPacket *packet)
{
	PERFINFO_AUTO_START_FUNC();
	eaiRemove(&pTimesOfPendingRotates, 0);
	PERFINFO_AUTO_STOP();
}

//This function will create and swap gContainerSource.incrementalHog on the hog thread.
// XXXXXX: If useHogs == false, it will still create the new hog and set it!
//         I don't know if anyone depends on that behavior, so I didn't change it.
static void objInternalRotateIncrementalHog(void)
{	
	//not sure why this was set to the container timestamp...
	//U32 currentTime = objContainerGetTimestamp();
	U32 currentTime = timeSecondsSince2000();

	if (gContainerSource.incrementalMinutes)
	{
		char *fname_final = 0;
		bool openResult = false;

		PERFINFO_AUTO_START_FUNC();

		//EnterCriticalSection(&gContainerSource.hog_access);
		if (!gContainerSource.needsRotate)
		{
			fprintf(fileGetStderr(), "[%s]:No ObjectDB updates, skipping rotation.\n", timeGetDateStringFromSecondsSince2000(currentTime));
			gContainerSource.nextRotateTime = currentTime + gContainerSource.incrementalMinutes * 60;
			gContainerSource.rotateQueued = false;
			PERFINFO_AUTO_STOP();
			return;
		}
		gContainerSource.lastRotateTime = currentTime;
		gContainerSource.nextRotateTime = currentTime + gContainerSource.incrementalMinutes * 60;
		gContainerSource.rotateQueued = false;

		objMakeIncrementalFilename(&fname_final, currentTime, NULL);


		// Tell it to roll over log files
		if (gContainerSource.incrementalRolloverCB) 
		{
			char logpath[MAX_PATH];
			PERFINFO_AUTO_START("Calling rollover callback", 1);
			strcpy(logpath, fname_final);
			objConvertToPendingFilename(logpath);

			//XXXXXX: dbLogRotateCB ignores the first two parameters here...
			gContainerSource.incrementalRolloverCB(gContainerSource.sourcePath, gContainerSource.incrementalPath, logpath);
			PERFINFO_AUTO_STOP();
		}
			
		// Save container info right before rotate
		PERFINFO_AUTO_START("Saving source info", 1);
		objSaveContainerSourceInfo(NULL);
		PERFINFO_AUTO_STOP();

		gContainerSource.needsRotate = false;

		PERFINFO_AUTO_START("Queueing rotation", 1);
		CONTAINER_LOGPRINT("[%s]:Rotating to incremental hog: %s\n", timeGetDateStringFromSecondsSince2000(currentTime), fname_final);
		//Dispatch the swap and sync in the other thread. (wt_objCreateSwapSyncAndCloseIncrementalHogFile)
		eaiPush(&pTimesOfPendingRotates, currentTime);
		gbSlowRotateAlertSent = false;
		wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDCREATEANDSWAP, fname_final, (int)strlen(fname_final)+1);
		PERFINFO_AUTO_STOP();
		//LeaveCriticalSection(&gContainerSource.hog_access);
		
		estrDestroy(&fname_final);

		PERFINFO_AUTO_STOP();
	}
	else 
	{
		currentTime = timeClampSecondsSince2000ToMinutes(currentTime, 5);
		gContainerSource.lastRotateTime = currentTime;
		gContainerSource.nextRotateTime = currentTime + 5 * 60;
	}
}

// Eventually, most of this will happen in a separate container saving thread
void objContainerSaveTick(void)
{
	U32 currentTime;

	PERFINFO_AUTO_START_FUNC();
	
	currentTime = timeSecondsSince2000();

	PERFINFO_AUTO_START("HogIOThreadMonitor", 1);
	//Complete callbacks from hog thread.
	wtMonitor(gContainerSource.hog_update_thread);
	PERFINFO_AUTO_STOP();

	if (s_saveThreadManager)
		mwtProcessOutputQueue(s_saveThreadManager);
	
	if (!gContainerSource.savecontainernextflush)
	{
		gContainerSource.savecontainernextflush = currentTime + gContainerSource.savecontainerflushperiod;
	}

	if ( !gbSlowRotateAlertSent && ((iMaxNumberOfPendingRotates && eaiUSize(&pTimesOfPendingRotates) > iMaxNumberOfPendingRotates)
		|| (iMaxDelayOfPendingRotates && eaiSize(&pTimesOfPendingRotates) && (currentTime - eaiGet(&pTimesOfPendingRotates, 0) > iMaxDelayOfPendingRotates))))
	{
		gbSlowRotateAlertSent = true;
		CONTAINER_LOGPRINT("Hogg rotation started at %s has not yet completed and there is another rotation queued.\n", timeGetLocalDateStringFromSecondsSince2000(eaiGet(&pTimesOfPendingRotates, 0)));
		ErrorOrCriticalAlert("OBJECTDB_SLOW_ROTATION", "Hogg rotation started at %s has not yet completed and there is another rotation queued.", timeGetLocalDateStringFromSecondsSince2000(eaiGet(&pTimesOfPendingRotates, 0)));
	}

	if(!iSlowSaveTickAlertThreshold)
	{
		iSlowSaveTickAlertThreshold = 2 * gContainerSource.incrementalMinutes * SECONDS_PER_MINUTE;
	}

	if(!gbSlowSaveTickAlertSent && (gContainerSource.lastSaveTick && (currentTime > gContainerSource.lastSaveTick + iSlowSaveTickAlertThreshold)))
	{
		gbSlowSaveTickAlertSent = true;
		CONTAINER_LOGPRINT("Save tick started at %d has not finished.\n", gContainerSource.lastSaveTick);
		ErrorOrCriticalAlert("OBJECTDB_SLOW_SAVE_TICK", "Save tick started at %d has not finished.", gContainerSource.lastSaveTick);
	}

	if (currentTime > gContainerSource.nextRotateTime && !gContainerSource.rotateQueued) 
	{
		PERFINFO_AUTO_START("objRotateIncrementalHog",1);

		gContainerSource.lastSaveTick = currentTime;
		gbSlowSaveTickAlertSent = false;
		objSaveModifiedContainersThreaded(true);

		gContainerSource.savecontainernextflush = currentTime + gContainerSource.savecontainerflushperiod;

		PERFINFO_AUTO_STOP();
	}
	else if (!gContainerSource.skipperiodiccontainersaves && currentTime > gContainerSource.savecontainernextflush)
	{
		gContainerSource.lastSaveTick = currentTime;
		gbSlowSaveTickAlertSent = false;
		objSaveModifiedContainersThreaded(false);

		gContainerSource.savecontainernextflush = currentTime + gContainerSource.savecontainerflushperiod; 
	}

	PERFINFO_AUTO_STOP();
}

static void objClearModifiedContainerArray(TrackedContainerArray *modifiedContainers)
{
	Container *object;
	int size;
	int i;
	PERFINFO_AUTO_START("objClearModifiedContainers",1);

	size = eaSize(&modifiedContainers->trackedContainers);

	for (i = 0; i < size; i++)
	{
		TrackedContainer *trackedCon = modifiedContainers->trackedContainers[i];
		object = objGetContainer(trackedCon->conRef.containerType, trackedCon->conRef.containerID);

		if (object && object->isModified)
		{
			object->isModified = false;
		}
	}
	ClearTrackedContainerArray(modifiedContainers);
	PERFINFO_AUTO_STOP();
}

// This should not be called on the ObjectDB because it does not do container locking
void objClearModifiedContainers(void)
{
	PERFINFO_AUTO_START("objClearModifiedContainers",1);
	objLockGlobalContainerRepository();
	objClearModifiedContainerArray(&gContainerRepository.modifiedContainers);
	objClearModifiedContainerArray(&gContainerRepository.modifiedEntityPlayers);
	objClearModifiedContainerArray(&gContainerRepository.modifiedEntitySavedPets);
	objUnlockGlobalContainerRepository();
	PERFINFO_AUTO_STOP();
}

U32 objContainerGetTimestamp(void)
{
	if (gContainerSource.sourceInfo)
	{
		if (gContainerSource.sourceInfo->iLastTimeStamp)
			return gContainerSource.sourceInfo->iLastTimeStamp;
		else
			return timeSecondsSince2000();
	}
	return 0;
}

U32 objContainerGetSystemTimestamp()
{
	if (gContainerSource.sourceInfo)
	{
		__time32_t timestamp[1] = { gContainerSource.sourceInfo->iLastTimeStamp };
		return _time32(timestamp);
	}
	return 0;
}

void objContainerSetTimestamp(U32 timeStamp)
{
	if (gContainerSource.sourceInfo && gContainerSource.sourceInfo->iCurrentTimeStamp < timeStamp)
	{
		gContainerSource.sourceInfo->iCurrentTimeStamp = timeStamp;
		wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSETTIMESTAMP, &timeStamp, sizeof(timeStamp));
	}
}

void objContainerSetMaxID(GlobalType eGlobalType, ContainerID iMaxID)
{
	if (gContainerSource.sourceInfo)
	{
		ContainerRepositoryTypeInfo *pTypeInfo = StructCreate(parse_ContainerRepositoryTypeInfo);
		pTypeInfo->eGlobalType = eGlobalType;
		pTypeInfo->iMaxContainerID = iMaxID;
		wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSETMAXID, &pTypeInfo, sizeof(ContainerRepositoryTypeInfo*));
	}
}


U64 objContainerGetSequence(void)
{
	if (gContainerSource.sourceInfo)
	{
		return gContainerSource.sourceInfo->iCurrentSequenceNumber;
	}
	return 0;
}

void objContainerSetSequence(U64 sequence)
{
	if (gContainerSource.sourceInfo)
	{
		gContainerSource.sourceInfo->iCurrentSequenceNumber = MAX(sequence, gContainerSource.sourceInfo->iCurrentSequenceNumber);
	}
}

U32 objGetLastIncrementalRotation(void)
{
	return gContainerSource.lastRotateTime;
}

U32 objGetLastSnapshot(void)
{
	return gContainerSource.scanSnapshotTime;
}

static FileScanAction IsDatabaseHogCallback(char *dir, struct _finddata32_t* data, DatabaseFileList *list)
{
	if(data->attrib & _A_SUBDIR);
	else if (//objStringIsPendingFilename(data->name) || 
		//objStringIsMergedFilename(data->name) || 
		objStringIsSnapshotFilename(data->name) ||
		objStringIsOfflineFilename(data->name))
	{
		eaPush(&list->files, strdupf("%s/%s", dir, data->name));
	}
	return FSA_EXPLORE_DIRECTORY;
}

//This returns a list of only the snapshots in every directory under /objectdb
DatabaseFileList *objFindAllDatabaseHogs(void)
{
	DatabaseFileList *list = StructCreate(parse_DatabaseFileList);
	char scandir[MAX_PATH];
	strcpy(scandir, gContainerSource.sourcePath);
	fileScanAllDataDirs(getDirectoryName(scandir), IsDatabaseHogCallback, list);

	return list;
}

/*
static FileScanAction ContainerFileFindCallback(char* dir, struct _finddata32_t* data, void *pUserData)
{
	ContainerRSInfo *rsinfo = (ContainerRSInfo*)pUserData;
	FileScanAction retval = FSA_EXPLORE_DIRECTORY;
	
	char fdir[MAX_PATH];
	HogFile *the_hog = NULL;
	char *fileData = NULL;

	strcpy(fdir, dir);

	do {
		bool isSnap = false;
		// Ignore all directories.
		if(data->attrib & _A_SUBDIR) break;

		isSnap = objStringIsSnapshotFilename(data->name);
		if (objStringIsPendingFilename(data->name) || objStringIsMergedFilename(data->name) || isSnap)
		{
			bool created;
			HogFileIndex hfi;
			U64 seq = 0;
			char buf[MAX_PATH];
			strcat(fdir, "/");
			strcat(fdir, data->name);

			the_hog = hogFileRead(fdir,&created,PIGERR_QUIET,NULL,HOG_READONLY|HOG_NOCREATE);
			if (created != 0 || !the_hog) break;
			
			sprintf(buf, "%s", rsinfo->filename);
			hfi = hogFileFind(the_hog, rsinfo->filename);
			if (hfi == HOG_INVALID_INDEX)
			{
				U32 size = 0;
				char *seqstr = NULL;
				hfi = hogFileFind(the_hog, rsinfo->linkname);
				if (hfi == HOG_INVALID_INDEX) break;
				seqstr = hogFileExtract(the_hog, hfi, &size, NULL);
				if (!seq) break;
				
				sprintf(buf, "%s.v%s", rsinfo->filename, seqstr);
				seq = atoi64(seqstr);
				free(seqstr);

				hfi = hogFileFind(the_hog, buf);
				if (hfi == HOG_INVALID_INDEX) break;
			}
			
			{
				int fileSize;
				ContainerRestoreState *rs;
				U32 timestamp = 0;

				hfi = hogFileFind(the_hog, CONTAINER_INFO_FILE);
				if (hfi == HOG_INVALID_INDEX) break;

				fileData = hogFileExtract(the_hog,hfi,&fileSize, NULL);
				if (fileData) 
				{
					ContainerRepositoryInfo *hogInfo = StructCreate(parse_ContainerRepositoryInfo);
					if (ParserReadText(fileData,parse_ContainerRepositoryInfo,hogInfo, 0))
					{
						timestamp = hogInfo->iCurrentTimeStamp;
						if (!seq) seq = hogInfo->iCurrentSequenceNumber;
					}
					StructDestroy(parse_ContainerRepositoryInfo, hogInfo);
				} else {
					break;
				}

				rs = StructCreate(parse_ContainerRestoreState);
				estrPrintf(&rs->hog_file, "%s", fdir);
				estrPrintf(&rs->filename, "%s", buf);
				rs->timestamp = timestamp;
				rs->isSnap = isSnap;
				rs->seq = seq;
				eaPush(&rsinfo->ppStates, rs);
			}
		}

	} while (false);
	if (the_hog)
	{
		hogFileDestroy(the_hog, false **JE: Unsure if this is leaking, leaving as it was before to be safe (might leak)**);
		the_hog = NULL;
	}
	if (fileData) free(fileData);


	return retval;
}
*/


typedef struct HogFindInfo
{
	GlobalType containerType;
	ContainerID containerId;

	char fileName[MAX_PATH];
	U64 seq; 
	HogFileIndex hfi;

	bool success;
}HogFindInfo;

static bool ContainerHogFind(HogFile *handle, HogFileIndex hogIndex, const char* filename, void *userData)
{
	GlobalType containerType;
	ContainerID containerId;
	U64 seq;
	bool deleted;
	HogFindInfo *info = userData;

	if (!objParseContainerFilename(filename, &containerType, &containerId, &seq, &deleted) || deleted)
		return true;

	if (containerType == info->containerType && containerId == info->containerId)
	{
		info->seq = seq;
		info->hfi = hogIndex;
		strcpy(info->fileName, filename);
		info->success = true;
		return false;
	}

	return true;
}

static ContainerRestoreState *objCheckRestoreState_internal(SA_PARAM_NN_STR const char *filename, HogFile* the_hog, GlobalType containerType, ContainerID containerID)
{
	HogFileIndex hfi;
	char buf[MAX_PATH];
	U64 seq = 0;

	if (!the_hog)
		return NULL;
	

	sprintf(buf, "%s/%d.co2",GlobalTypeToName(containerType),containerID);
	hfi = hogFileFind(the_hog, buf);

	if (hfi == HOG_INVALID_INDEX)
	{
		sprintf(buf, "%s/%d.con",GlobalTypeToName(containerType),containerID);
		hfi = hogFileFind(the_hog, buf);
	}

	if (hfi == HOG_INVALID_INDEX)
	{
		U32 size = 0;
		char *seqstr = NULL;
		sprintf(buf, "%s/%d.lcon",GlobalTypeToName(containerType),containerID);

		hfi = hogFileFind(the_hog, buf);

		if (hfi == HOG_INVALID_INDEX)
		{
			return NULL;
		}
		
		seqstr = hogFileExtract(the_hog, hfi, &size, NULL);
		seq = atoi64(seqstr);
		free(seqstr);

		if (!seq)
		{
			return NULL;
		}

		sprintf(buf, "%s/%d.v%" FORM_LL "u.co2",GlobalTypeToName(containerType),containerID,seq);
		hfi = hogFileFind(the_hog, buf);
	}

	if (hfi == HOG_INVALID_INDEX)
	{
		sprintf(buf, "%s/%d.v%" FORM_LL "u.con",GlobalTypeToName(containerType),containerID,seq);
		hfi = hogFileFind(the_hog, buf);
	}

	if (hfi == HOG_INVALID_INDEX)
	{
		HogFindInfo info = {0};
		info.containerType = containerType;
		info.containerId = containerID;
		hogScanAllFiles(the_hog, ContainerHogFind, &info);

		if(info.success)
		{
			hfi = info.hfi;
			seq = info.seq;
			strcpy(buf, info.fileName);
		}
	}

	if (hfi != HOG_INVALID_INDEX)
	{	
		ContainerRestoreState *rs;

		rs = StructCreate(parse_ContainerRestoreState);
		estrPrintf(&rs->hog_file, "%s", filename);
		estrPrintf(&rs->filename, "%s", buf);
		rs->isSnap = objStringIsSnapshotFilename(filename);

		return rs;
	}
	return NULL;
}

ContainerRestoreState *objCheckRestoreState(const char *file, GlobalType containerType, ContainerID containerID)
{
	U32 fileModifiedTimestamp;
	HogFile *the_hog;
	ContainerRestoreState *rs = NULL;

	char filename[MAX_PATH];

	sprintf(filename, "%s", file);
	fileModifiedTimestamp = fileLastChangedSS2000(filename);
	// Timestamp of 0 means last modified before 2000. This means the file was most likely the product of a bad copy
	if(fileModifiedTimestamp == 0)
		return NULL;

	the_hog = hogFileRead(filename, NULL, PIGERR_QUIET,NULL,HOG_READONLY|HOG_NOCREATE|HOG_NO_STRING_CACHE|HOG_NO_REPAIR);
	if (!the_hog)
		return NULL;
	
	rs = objCheckRestoreState_internal(filename, the_hog, containerType, containerID);

	hogFileDestroy(the_hog, true);
	return rs;
}

ContainerRestoreState *objCheckOfflineRestoreState(GlobalType containerType, ContainerID containerID)
{
	if(!gContainerSource.offlinePath || !gContainerSource.offlineHog)
		return NULL;

	return objCheckRestoreState_internal(gContainerSource.offlinePath, gContainerSource.offlineHog, containerType, containerID);
}

SchemaSource *objLoadSchemaSourceFromHog(GlobalType type, HogFile *the_hog)
{
	HogFileIndex hfi = HOG_INVALID_INDEX;
	void *schemadata;
	SchemaSource *schema;
	U32 size;

	hfi = hogFileFind(the_hog, objSchemaFileNameFromName(GlobalTypeToName(type)));
	if (hfi == HOG_INVALID_INDEX) return NULL;
	
	schemadata = hogFileExtract(the_hog,hfi,&size, NULL);
	if (!schemadata || !size) return NULL;
	
	schema = objGetArchivedSchemaSource(type, schemadata);

	free (schemadata);

	return schema;
}

void DEFAULT_LATELINK_objGetDependentContainers(GlobalType containerType, Container **con, ContainerRef ***pppRefs, bool recursive)
{
	if (recursive && !(*con)->isTemporary)
		objUnlockContainer(con);
}

void * objOpenRestoreStateHog(ContainerRestoreState *rs)
{
	U32 fileModifiedTimestamp;
	HogFile *the_hog = NULL;
	//Open the hogfile once and keep a reference here so that we're not slamming the IO with multiple hogFileReads.

	if (!rs->hog_file || !fileExists(rs->hog_file))
	{
		return NULL;
	}

	fileModifiedTimestamp = fileLastChangedSS2000(rs->hog_file);
	// Timestamp of 0 means last modified before 2000. This means the file was most likely the product of a bad copy
	if(fileModifiedTimestamp == 0)
		return NULL;

	the_hog = hogFileRead(rs->hog_file,NULL,PIGERR_QUIET,NULL,HOG_READONLY|HOG_NOCREATE|HOG_NO_STRING_CACHE|HOG_NO_REPAIR);
	if (!the_hog)
	{
		if (the_hog) hogFileDestroy(the_hog, true);
		return NULL;
	}
	return the_hog;
}

void objCloseRestoreStateHog(void *the_hog)
{
	hogFileDestroy(the_hog, true);
}

static Container *objContainerGetRestoreContainer(GlobalType containerType, ContainerID containerID, ContainerRestoreState *rs, ContainerStore *store, U32 *size_out)
{
	HogFile *the_hog = NULL;
	HogFileIndex hfi = HOG_INVALID_INDEX;
	Container *restoreCon = NULL;
	char *containertext = NULL;
	int error;
	U32 headerSize;
	U32 size_unused = 0;
	// Attempt to open hog.
	the_hog = hogFileRead(rs->hog_file,NULL,PIGERR_QUIET,&error,HOG_READONLY|HOG_NOCREATE|HOG_NO_STRING_CACHE|HOG_NO_REPAIR);
	if (!the_hog)
	{
		Errorf("Could not open file %s for restoring container %s[%u]: hog error %d%s",
			rs->hog_file, GlobalTypeToName(containerType), containerID, error,
			fileExists(rs->hog_file) ? "" : ", file does not exist");
		return NULL;
	}

	hfi = hogFileFind(the_hog, rs->filename);
	if (hfi == HOG_INVALID_INDEX)
	{
		Errorf("Could not find container %s[%u] in file %s for restoring.",
			GlobalTypeToName(containerType), containerID, rs->hog_file);
		if (the_hog) hogFileDestroy(the_hog, true);
		return NULL;
	}

	containertext = hogFileExtract(the_hog, hfi, size_out ? size_out : &size_unused, NULL);

	hogFileGetHeaderData(the_hog, hfi, &headerSize);

	restoreCon = objCreateContainer(store->containerSchema);
	restoreCon->isTemporary = true;

	restoreCon->containerData = objCreateTempContainerObject(store->containerSchema, "Creating temp container in objContainerGetRestoreStringFromState");

	if (ParserReadText(containertext+headerSize, store->containerSchema->classParse, restoreCon->containerData, TOK_PERSIST | PARSER_IGNORE_ALL_UNKNOWN))
	{
		free(containertext);
		hogFileDestroy(the_hog, true);
	}
	else
	{	//native schema could not load correctly. try archived schema
		void *condata = NULL;
		SchemaSource *source = NULL;
		ContainerSchema *schema = NULL;
		char *estr = NULL;

		source = objLoadSchemaSourceFromHog(containerType, the_hog);
		if (source)
		{
			schema = source->containerSchema;
		}
		if (!schema || !schema->classParse)
		{
			Errorf("Archived schema load failed: hog %s, container %s[%u], source_loaded %d, schema_loaded %d.",
				rs->hog_file, GlobalTypeToName(containerType), containerID,
				!!source, !!schema);

			if (the_hog) hogFileDestroy(the_hog, true);
			if (source) objDestroyArchivedSchemaSource(source);
			if (containertext) free(containertext);
			objDestroyContainer(restoreCon);
			return NULL;
		}

		condata = StructAllocVoid(schema->classParse);
		if (condata)
		{
			StructInitVoid(schema->classParse, condata);
		}
		else
		{
			Errorf("Create Struct data from schema for restoring container %s[%u].",
				GlobalTypeToName(containerType), containerID);

			if (the_hog) hogFileDestroy(the_hog, true);
			if (source) objDestroyArchivedSchemaSource(source);
			if (containertext) free(containertext);
			objDestroyContainer(restoreCon);
			return NULL;
		}
		hogFileDestroy(the_hog, true);

		if (!ParserReadText(containertext+headerSize, schema->classParse, condata, TOK_PERSIST))
		{
			Errorf("Could not load container %s[%u] in file %s for restoring.",
				GlobalTypeToName(containerType), containerID, rs->hog_file);

			if (containertext) free(containertext);
			if (condata)  {
				StructDeInitVoid(schema->classParse, condata);
				free(condata);
			}
			if (source) objDestroyArchivedSchemaSource(source);
			objDestroyContainer(restoreCon);
			return NULL;
		}
		free(containertext);

		StructResetVoid(store->containerSchema->classParse, restoreCon->containerData);
		estrStackCreate(&estr);
		if (StructCopyFields2tpis(schema->classParse, condata, store->containerSchema->classParse, restoreCon->containerData, TOK_PERSIST, 0, &estr) == COPY2TPIRESULT_FAILED_FIELDS)
		{
			Errorf("Could not upgrade container %s[%u] in file %s for restoring: %s",
				GlobalTypeToName(containerType), containerID, rs->hog_file, estr);
			estrDestroy(&estr);

			if (condata)  {
				StructDeInitVoid(schema->classParse, condata);
				free(condata);
			}
			if (source) objDestroyArchivedSchemaSource(source);

			objDestroyContainer(restoreCon);
			return NULL;
		}
		estrDestroy(&estr);
		estr = NULL;

		if (condata)  {
			StructDeInitVoid(schema->classParse, condata);
			free(condata);
		}
		if (source) objDestroyArchivedSchemaSource(source);
	}

	if(containerType == GLOBALTYPE_ENTITYPLAYER)
		objPathSetInt(".psaved.timeLastRestored", store->containerSchema->classParse, restoreCon->containerData, timeSecondsSince2000(), true);

	objSetNewContainerID(restoreCon,containerID,-1);

	return restoreCon;
}

bool objContainerGetRestoreStringFromState(GlobalType containerType, ContainerID containerID, ContainerRestoreState *rs, char **diffstr, ContainerRef ***pppRefs, bool recursive, bool alertOnExistingContainer)
{
	ContainerStore *store = objFindOrCreateContainerStoreFromType(containerType);
	Container *con = NULL;
	Container *restoreCon = NULL;
	char *estr = NULL;
	U32 sizeHint = 0;

	if (!store)
	{
		Errorf("Could not find container store for container type: %s", GlobalTypeToName(containerType));
		return false;
	}

	if (!diffstr)
	{
		Errorf("Invalid output string.");
		return false;
	}

	restoreCon = objContainerGetRestoreContainer(containerType, containerID, rs, store, &sizeHint);

	if(!restoreCon)
		return false;

	con = objGetContainerEx(containerType, containerID, true, false, true);
	if(con && alertOnExistingContainer)
	{
		TriggerAlertf("OBJECTDB.RESTORE.CONTAINEREXISTS",ALERTLEVEL_WARNING,ALERTCATEGORY_PROGRAMMER,0, 0, 0, GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0,
			"Trying to restore " CON_PRINTF_STR ", but it unexpectedly already exists.", CON_PRINTF_ARG(containerType, containerID));
	}

	estrReserveCapacity(&estr, sizeHint ? sizeHint : 1024*1024);
	if (con)
	{
		StructWriteTextDiff(&estr,store->containerSchema->classParse, con->containerData, restoreCon->containerData, "", TOK_PERSIST,0,0);
	}
	else
	{
		StructTextDiffWithNull_Verify(&estr,store->containerSchema->classParse, restoreCon->containerData, "", 0, TOK_PERSIST,0,0);
	}

	estrTrimLeadingAndTrailingWhitespace(&estr);

	if(con)
		objUnlockContainer(&con);

	objGetDependentContainers(containerType, &restoreCon, pppRefs, recursive);

	objDestroyContainer(restoreCon);

	if (estrLength(&estr) != 0)
	{
		*diffstr = estr;
		estr = NULL;
	}
	else
	{
		estrDestroy(&estr);
	}

	return true;
}

bool objContainerGetDependentContainersFromState(GlobalType containerType, ContainerID containerID, ContainerRestoreState *rs, ContainerRef ***pppRefs, bool recursive)
{
	ContainerStore *store = objFindOrCreateContainerStoreFromType(containerType);
	Container *restoreCon = NULL;

	if (!store)
	{
		Errorf("Could not find container store for container type: %s", GlobalTypeToName(containerType));
		return false;
	}

	restoreCon = objContainerGetRestoreContainer(containerType, containerID, rs, store, NULL);

	if(!restoreCon)
		return false;

	objGetDependentContainers(containerType, &restoreCon, pppRefs, recursive);

	objDestroyContainer(restoreCon);

	return true;
}

// FAST MERGER
// Version of merger that doesn't load any logs or decompress

static FileScanAction IncrementalHogMergeCallback(char* dir, struct _finddata32_t* data, IncrementalLoadState *state)
{
	ContainerLoadThreadState *threadState = state->loadThreadState;
	char fdir[MAX_PATH];
	FileScanAction retval = FSA_NO_EXPLORE_DIRECTORY;

	strcpy(fdir, dir);
	//getDirectoryName(fdir);

	// Ignore all directories.
	if(data->attrib & _A_SUBDIR)
	{
		return retval;
	}
				
	//clean up old processed hogs.
	if (objStringIsProcessedFilename(data->name))
	{
		objCleanupProcessedFile(SAFESTR(fdir), data->name);
	}
	else if(objStringIsBadStopFilename(data->name))
	{
		objCleanupBadStopFile(SAFESTR(fdir), data->name);
	}
	else if (objStringIsPendingFilename(data->name))
	{
		bool incrementalLoaded = false, incrementalSkipped = false;
		HogFile *incrementalHog;
		HogFileIndex hfi;
		char *filename;

		strcat(fdir, "/");
		strcat(fdir, data->name);			
		filename = fdir;			

		CONTAINER_LOGPRINT("Loading Incremental:%s ...", filename);

		hogSetMaxBufferSize(OBJ_HOG_BUFFER_SIZE);
		incrementalHog = hogFileReadEx(filename,NULL,PIGERR_ASSERT,NULL,HOG_NOCREATE|HOG_NO_STRING_CACHE, DB_HOG_DATALIST_JOURNAL_SIZE);
		hogFileSetSingleAppMode(incrementalHog, true);

		if (!incrementalHog)
		{
			FatalErrorf("Could not open hog:%s\n", filename);
			return retval;
		}
		hfi = hogFileFind(incrementalHog, CONTAINER_INFO_FILE);

		if (hfi != HOG_INVALID_INDEX)
		{
			ContainerRepositoryInfo *hogInfo = StructCreate(parse_ContainerRepositoryInfo);
			char *fileData;
			int fileSize;
			U32 fileCount;
			bool result = true;
			fileData = hogFileExtract(incrementalHog,hfi,&fileSize, NULL);
			fileCount = hogFileGetNumUserFiles(incrementalHog) - 1;
			//Check for a valid source info
			if (ParserReadText(fileData,parse_ContainerRepositoryInfo,hogInfo, 0))
			{
				int i;

				if (hogInfo->iLastSequenceNumber <= gContainerSource.sourceInfo->iLastSequenceNumber)
				{
					incrementalLoaded = false;
					incrementalSkipped = true;
					// This incremental already got merged! we need to not merge it again
					CONTAINER_LOGPRINT("Failed, because incremental was previously merged\n");
				}
				else
				{
			
					MAX1(gContainerSource.sourceInfo->iLastSequenceNumber, hogInfo->iLastSequenceNumber);
					MAX1(gContainerSource.sourceInfo->iLastTimeStamp, hogInfo->iLastTimeStamp);

					hogInfo->iCurrentSequenceNumber = hogInfo->iLastSequenceNumber;
					hogInfo->iCurrentTimeStamp = hogInfo->iLastTimeStamp;

					MAX1(gContainerSource.sourceInfo->iCurrentSequenceNumber, hogInfo->iCurrentSequenceNumber);
					MAX1(gContainerSource.sourceInfo->iCurrentTimeStamp, hogInfo->iCurrentTimeStamp);

					EARRAY_FOREACH_BEGIN(hogInfo->ppRepositoryTypeInfo, k);
					{
						ContainerRepositoryTypeInfo *typeInfo = hogInfo->ppRepositoryTypeInfo[k];
						bool bFound = false;
						ContainerRepositoryTypeInfo *foundInfo = eaIndexedGetUsingInt(&gContainerSource.sourceInfo->ppRepositoryTypeInfo, typeInfo->eGlobalType);

						if(foundInfo)
						{
							if(typeInfo->iMaxContainerID > foundInfo->iMaxContainerID)
								foundInfo->iMaxContainerID = typeInfo->iMaxContainerID;
						}
						else
						{
							eaPush(&gContainerSource.sourceInfo->ppRepositoryTypeInfo, typeInfo);
							hogInfo->ppRepositoryTypeInfo[k] = NULL;
						}
					}
					EARRAY_FOREACH_END;

					incrementalLoaded = true;

					threadState->maxSequence = hogInfo->iLastSequenceNumber;

					hogScanAllFiles(incrementalHog, QueueContainerHogLoad, threadState);

					CONTAINER_LOGPRINT("%d merger load requests", eaSize(&threadState->ppRequests));

					for (i = 0; i < eaSize(&threadState->ppRequests); i++)
					{
						ContainerLoadRequest *pRequest = threadState->ppRequests[i];
						if (pRequest->the_hog == incrementalHog)
						{					
							// Load requests from incrementals get the sequence number of the hog
							pRequest->sequenceNumber = hogInfo->iLastSequenceNumber;
						}
					}
					CONTAINER_LOGPRINT("done.\n\n");
				}

			}
			if (hogInfo) StructDestroy(parse_ContainerRepositoryInfo, hogInfo);
			if (fileData) free(fileData);
		}

		if (!incrementalLoaded)
		{
			FileRenameRequest *mv = callocStruct(FileRenameRequest);
			mv->filename = strdup(filename);
			mv->moveToOld = true;
			eaPush(&state->renames, mv);
			CONTAINER_LOGPRINT("Hog was invalid:%s. ", filename);

			if (incrementalHog)
				hogFileDestroy(incrementalHog, false /*JE: Unsure if this is leaking, leaving as it was before to be safe (might leak)*/);

			if (!incrementalSkipped)
				ErrorOrAlert("DATABASE.LOADFAILURE", "Could not load incremental file %s, possibly because it was previously merged or is empty", filename);				
		}
		else
		{
			//do rename if it's an incremental, don't if it's a snapshot.
			char mergename[MAX_PATH];
			FileRenameRequest *mv = callocStruct(FileRenameRequest);
			strcpy(mergename, filename);
			objConvertToMergedFilename(mergename);
			mv->filename = strdup(filename);
			mv->newname = strdup(mergename);
			eaPush(&state->renames, mv);


			eaPush(&state->openedIncrementals, incrementalHog);
			retval = 1;
			state->fileCount++;
		}		
	}

	return retval;
}


void ContainerHogCopyToSnapshot(HogFile *snapshotHog, ContainerLoadThreadState *threadState, ContainerLoadRequest *request, bool writeSequenceNumber)
{
	GlobalType containerType = request->containerType;
	ContainerID containerID = request->containerID;
	HogFile *handle = request->the_hog;
	HogFileIndex hogIndex = request->hog_index;
	char fileName[MAX_PATH];
	NewPigEntry entry = {0};

	assert(threadState && request);

	objLoadFileData(request);
	entry.header_data = hogFileGetHeaderData(handle, hogIndex, &entry.header_data_size);

	if (request->bytesCompressed)
		entry.must_pack = 1;
	else
		entry.dont_pack = 1;

	if(writeSequenceNumber)
		sprintf(fileName,"%s/%d.v%"FORM_LL"d.%s",GlobalTypeToName(containerType),containerID, request->sequenceNumber, entry.header_data_size ? "co2" : "con");
	else
		sprintf(fileName,"%s/%d.%s",GlobalTypeToName(containerType),containerID, entry.header_data_size ? "co2" : "con");

	threadState->requestCount++;

	entry.data = (U8*)request->fileData;
	entry.size = request->fileSize;
	entry.pack_size = request->bytesCompressed;
	entry.fname = fileName;
	entry.timestamp = objContainerGetSystemTimestamp();
	entry.checksum[0] = hogFileGetFileChecksum(handle, hogIndex);

	entry.free_callback = NULL;

	if (hogFileModifyUpdateNamedAsync2(snapshotHog, &entry))
	{
		threadState->completeCount++;
		if (threadState->completeCount%1000 == 0)
			fprintf(fileGetStderr(), "\r%d containers merged.", threadState->completeCount);
	}	
}

void ContainerHogMarkLatest(HogFile *snapshotHog, GlobalType conType, ContainerID conID, U64 seq)
{
	char fileName[MAX_PATH];
	char *data = NULL;
	sprintf(fileName,"%s/%d.lcon",GlobalTypeToName(conType),conID);

	estrPrintf(&data, "%"FORM_LL"u", seq);

	hogFileModifyUpdateNamed(snapshotHog, fileName, data, estrLength(&data)+1, objContainerGetSystemTimestamp(), EStringFreeCallback);
}

static void ContainerHogDeleteLatestMark(HogFile *snapshotHog, GlobalType conType, ContainerID conID)
{
	char fileName[MAX_PATH];
	sprintf(fileName,"%s/%d.lcon",GlobalTypeToName(conType),conID);

	hogFileModifyDeleteNamed(snapshotHog, fileName);
}

void ContainerHogMarkDeleted(HogFile *snapshotHog, GlobalType conType, ContainerID conID, U64 seq)
{
	char *data = NULL;
	char fileName[MAX_PATH];
	sprintf(fileName,"%s/%d.del",GlobalTypeToName(conType),conID);

	estrPrintf(&data, "%"FORM_LL"u", seq);

	hogFileModifyUpdateNamed(snapshotHog, fileName, data, estrLength(&data)+1, objContainerGetSystemTimestamp(), EStringFreeCallback);
}

static void ContainerHogRemoveDeletedMark(HogFile *snapshotHog, GlobalType conType, ContainerID conID)
{
	char fileName[MAX_PATH];
	sprintf(fileName,"%s/%d.del",GlobalTypeToName(conType),conID);

	hogFileModifyDeleteNamed(snapshotHog, fileName);
}

static void ContainerHogDeleteFromSnapshot(ContainerLoadThreadState *threadState, ContainerLoadRequest *request)
{
	GlobalType containerType = request->containerType;
	ContainerID containerID = request->containerID;
	U32 fileSize = 0;
	HogFileIndex hogIndex = request->hog_index;

	assert(threadState && request);
	assert(request->fromSnapshot);
	
	verbose_printf("Removing old container %s from snapshot hog\n", hogFileGetFileName(request->the_hog, request->hog_index));

	hogFileModifyDelete(request->the_hog, request->hog_index);
}

void objDefragLatestSnapshot()
{
	char *lastSnapshot = NULL;
	char *databaseDir = NULL;

	estrStackCreate(&databaseDir);
	estrCopy2(&databaseDir, gContainerSource.sourcePath);
	//stupid destructive getter function.
	getDirectoryName(databaseDir);
	estrSetSize(&databaseDir, (unsigned int)strlen(databaseDir));

	servLog(LOG_MISC, "SnapshotDefragStarting", "Time %d", timeSecondsSince2000());
	//This should block all container persist updates until we're done merging.
	EnterCriticalSection(&gContainerSource.hog_access);
	loadstart_printf("Defragging snapshot...");
	//Find the last snapshot.
	fileScanAllDataDirs(databaseDir, SnapshotHogFileScanCallback, &lastSnapshot);
	if (lastSnapshot)
	{
		char *filename = lastSnapshot;
		hogDefrag(filename, DB_HOG_DATALIST_JOURNAL_SIZE, HogDefrag_RunDiff);
	}
	loadend_printf("Finished Defrag");
	LeaveCriticalSection(&gContainerSource.hog_access);

	servLog(LOG_MISC, "SnapshotDefragFinished", "Time %d", timeSecondsSince2000());
	estrDestroy(&databaseDir);
}

void BackupSnapshot(const char *nextSnapshot, bool bMoveBackupToOld)
{
	char *backupHog = NULL;
	char *tempHog = NULL;
	estrStackCreate(&backupHog);
	estrStackCreate(&tempHog);

	objMakeSnapshotFilename(&backupHog, objContainerGetTimestamp(), "backup.hogg");

	estrPrintf(&tempHog, "%s.temp", backupHog);

	CONTAINER_LOGPRINT("Making Backup of %s as %s. \n", nextSnapshot, tempHog);

	if (fileCopy(nextSnapshot, tempHog) == 0)
	{
		CONTAINER_LOGPRINT("Renaming backup to %s. \n", backupHog);
		if (fileMove(tempHog, backupHog) != 0 && isProductionMode())
		{
			AssertOrAlert("OBJECTDB_BACKUP_FAILED", "Failed to copy backup of %s to %s. \n", tempHog, backupHog);
		}
		else
		{
			if (gContainerIOConfig.MergerScript[0])
			{
				int pid = 0;
				char *buf = NULL;
				char *backupHogOldDir = NULL;
#ifndef _XBOX
				backupHogOldDir = addFilePrefix(backupHog, "old/");

				estrStackCreate(&buf);
				estrPrintf(&buf, "%s", gContainerIOConfig.MergerScript);

				if(bMoveBackupToOld && objMoveToOldFilesDir(backupHog))
					estrReplaceOccurrences(&buf, "%FILE%", backupHogOldDir);
				else
					estrReplaceOccurrences(&buf, "%FILE%", backupHog);

				log_printf(LOG_CONTAINER, "Running Merger backup script: %s", buf);

				pid = system_detach(buf, 0, 0);

				fprintf(fileGetStderr(), "Running Merger backup script [pid:%d]:\n%s\n",pid,buf);

				estrDestroy(&buf);
#endif
			}
		}
	}
	else if (isProductionMode()) 
	{
		AssertOrAlert("OBJECTDB_BACKUP_FAILED", "Failed to make backup of %s as %s. \n", nextSnapshot, tempHog);
	}

	estrDestroy(&backupHog);
	estrDestroy(&tempHog);
}

void objModifyIncrementalSnapshot(HogFile *snapshotHog, IncrementalLoadState *loadState, ContainerLoadThreadState *threadState, const char *lastSnapshot, char **nextSnapshot)
{
	int i;
	GlobalType lastType = 0;
	ContainerID lastID = 0;
	GlobalType lastDeleteType = 0;
	ContainerID lastDeleteID = 0;
	FileRenameRequest *mv;
	int numRemoved = 0;
	int numDeletedPruned = 0;
	U64 lastSeq = 0;
	U64 lastDeletedSeq = 0;
	bool keepDeletedMarker = false;
	bool deleteRest = false;

	ContainerLoadRequest **ppUpdateContainers = NULL;
	ContainerLoadRequest **ppDeleteContainers = NULL;
	int timer = timerAlloc();

	loadstart_printf("Modifying incremental snapshot: %s\n", lastSnapshot);
	log_printf(LOG_CONTAINER,"Modifying incremental snapshot: %s\n", lastSnapshot);

	CONTAINER_LOGPRINT("Processing %d containers from incrementals\n", eaSize(&threadState->ppRequests));

	eaQSort(threadState->ppRequests, sortContainerLoadRequests);

	for (i = 0; i < eaSize(&threadState->ppRequests); i++)
	{
		ContainerLoadRequest *pRequest = threadState->ppRequests[i];
		if (pRequest->containerType == lastType && pRequest->containerID == lastID)
		{
			//fprintf(fileGetStderr(), "Ignoring container %d:%d version %d\n", pRequest->containerType, pRequest->containerID, pRequest->sequenceNumber);
			// We already loaded a newer one
			continue;
		}

		if (pRequest->deleted)
		{
			if (!pRequest->fromSnapshot)
			{					
				lastDeleteType = pRequest->containerType;
				lastDeleteID = pRequest->containerID;
			}
			// Skip deleted markers
			continue;
		}
				
		if (lastType && pRequest->containerType != lastType)
		{
			ContainerStore *store = &gContainerRepository.containerStores[lastType];
			// Write out schema

			if (store->containerSchema)
			{
				ContainerSourceUpdate conup = {0};
				conup.hog = snapshotHog;
				conup.type = lastType;
				wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINERSCHEMA, &conup, sizeof(ContainerSourceUpdate));
			}
		}
		lastType = pRequest->containerType;
		lastID = pRequest->containerID;

		if (pRequest->fromSnapshot)
		{
			// it's already in the snapshot, don't bother
			continue;
		}

		if (lastDeleteType == pRequest->containerType && lastDeleteID == pRequest->containerID)
		{
			pRequest->snapshotMarkDeleted = true;
			++numRemoved;
		}
		eaPush(&ppUpdateContainers, pRequest);
	}

	lastType = 0;
	lastID = 0;

	CONTAINER_LOGPRINT("Found %d unique, updated containers, %d of which are to be deleted\n", eaSize(&ppUpdateContainers), numRemoved);

	numRemoved = 0;

	eaQSort(ppUpdateContainers, sortContainerLoadRequestsByFile);

	for (i = 0; i < eaSize(&ppUpdateContainers); i++)
	{
		ContainerLoadRequest *pRequest = ppUpdateContainers[i];

		//fprintf(fileGetStderr(), "Writing container %d:%d version %d\n", pRequest->containerType, pRequest->containerID, pRequest->sequenceNumber);

		// MB/s display
		if (timerElapsed(timer) > 1 || i == eaSize(&ppUpdateContainers)-1)
		{
			F32 write, read;
			F32 write_avg, read_avg;
			timerStart(timer);
			hogGetGlobalStats(&read, &write, &read_avg, &write_avg);
			fprintf(fileGetStderr(), "%d/%d   Disk I/O: Read: %1.1fKB/s (avg:%1.1fKB/s)  Write:%1.1fKB/s (avg:%1.1fKB/s)      \r",
				i+1, eaSize(&ppUpdateContainers),
				read/1024.f, read_avg/1024.f, write/1024.f, write_avg/1024.f);
		}

		ContainerHogMarkLatest(snapshotHog,
			pRequest->containerType,
			pRequest->containerID,
			pRequest->sequenceNumber);

		assert(pRequest->the_hog != snapshotHog);
		ContainerHogCopyToSnapshot(snapshotHog, threadState, pRequest, true);
		if(pRequest->snapshotMarkDeleted)
		{
			// If we found a delete marker, save that to the snapshot
			ContainerHogMarkDeleted(snapshotHog, pRequest->containerType, pRequest->containerID, 1);
		}
	}

	eaDestroy(&ppUpdateContainers);

	if (lastType)
	{
		ContainerStore *store = &gContainerRepository.containerStores[lastType];
		// Write out schema

		if (store->containerSchema)
		{
			ContainerSourceUpdate conup = {0};
			conup.hog = snapshotHog;
			conup.type = lastType;
			wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINERSCHEMA, &conup, sizeof(ContainerSourceUpdate));
		}
	}

	loadend_printf("Merged %d Containers from incrementals\n", threadState->completeCount);

	objSaveContainerSourceInfo(snapshotHog);

	gContainerSource.scanSnapshotTime = gContainerSource.sourceInfo->iCurrentTimeStamp;
	gContainerSource.scanSnapshotSeq = gContainerSource.sourceInfo->iCurrentSequenceNumber;
			

	loadstart_printf("Removing old versions of containers from snapshot...\n");
	lastType = 0;
	lastID = 0;
	for (i = 0; i < eaSize(&threadState->ppRequests); i++)
	{
		ContainerLoadRequest *pRequest = threadState->ppRequests[i];

		if(lastType != pRequest->containerType || lastID != pRequest->containerID)
		{
			keepDeletedMarker = false;
			deleteRest = false;

			if (pRequest->deleted)
			{
				if (pRequest->fromSnapshot)
				{
					// The most current version is a delete in the snapshot, so remove the latest marker.
					pRequest->removeLatestMarker = true;
				}
				else
				{
					keepDeletedMarker = true;
					lastType = pRequest->containerType;
					lastID = pRequest->containerID;
					lastSeq = _I64_MAX; // lastSeq is used to check for the same non-delete container in the list twice
					continue;
				}
			}
		}

		if (pRequest->deleted)
		{
			// Skip incremental delete notifications (so there is a one snapshot lag time),
			// but register snapshot delete requests as "newer" so it deletes the old one
			if (pRequest->fromSnapshot && s_PruneDeletedContainers && !keepDeletedMarker)
			{					
				lastType = pRequest->containerType;
				lastID = pRequest->containerID;
				lastSeq = _I64_MAX; // lastSeq is used to check for the same non-delete container in the list twice
				deleteRest = true;
				eaPush(&ppDeleteContainers, pRequest);
			}
			continue;
		}

		if (!deleteRest)
		{
			lastType = pRequest->containerType;
			lastID = pRequest->containerID;
			lastSeq = pRequest->sequenceNumber;
			deleteRest = true;
			// Skip first one, it's the new one
			continue;
		}
		if (pRequest->sequenceNumber == lastSeq)
		{
			// Got the same exact container update twice in a row. Something is wrong so let's NOT delete anything
			// so we don't accidentally delete the wrong container
			continue;
		}
		if (pRequest->fromSnapshot)
		{
			// Delete outdated files from snapshot
			eaPush(&ppUpdateContainers, pRequest);
		}
	}

	eaQSort(ppUpdateContainers, sortContainerLoadRequestsByFile);
	eaQSort(ppDeleteContainers, sortContainerLoadRequestsByFile);

	CONTAINER_LOGPRINT("Found %d containers to remove from the snapshot.\n", eaSize(&ppUpdateContainers));

	// Make sure to remove all .del containers after the corresponding .co* files.

	for (i = 0; i < eaSize(&ppUpdateContainers); i++)
	{
		ContainerLoadRequest *pRequest = ppUpdateContainers[i];
		// ppUpdateContainers only contains requests from the snapShot
		// Delete outdated files from snapshot
		ContainerHogDeleteFromSnapshot(threadState, pRequest);
		numRemoved++;
	}

	for (i = 0; i < eaSize(&ppDeleteContainers); i++)
	{
		ContainerLoadRequest *pRequest = ppDeleteContainers[i];
		// ppDeleteContainers only contains deleted requests from the snapShot
		ContainerHogDeleteFromSnapshot(threadState, pRequest);
		if(pRequest->removeLatestMarker)
		{
			// Remove marker files
			ContainerHogDeleteLatestMark(snapshotHog, pRequest->containerType, pRequest->containerID);
		}
		numDeletedPruned++;
	}

	eaDestroy(&ppUpdateContainers);
	eaDestroy(&ppDeleteContainers);

	CONTAINER_LOGPRINT("%d removed, %d deleted containers pruned.\n", numRemoved, numDeletedPruned);
	loadend_printf("done\n");

	estrClear(nextSnapshot);

	objMakeSnapshotFilename(nextSnapshot, objContainerGetTimestamp(), NULL);

	mv = callocStruct(FileRenameRequest);

	mv->filename = strdup(lastSnapshot);
	mv->newname = strdup(*nextSnapshot);
	eaPush(&loadState->renames, mv);		
	timerFree(timer);
}

void objMergeIncrementalHogs(bool bWriteBackup, bool bAppend, bool bMoveBackupToOld)
{
	IncrementalLoadState loadState = {0, 0, 0, NULL, NULL, GLOBALTYPE_NONE};
	char *lastSnapshot = 0;
	char *nextSnapshot = 0;
	char *databaseDir = NULL;
	bool createSnapshot = false;
	bool snapshotLoaded = false;
	int replayedTransactions = 0;
	HogFile *snapshotHog;
	ContainerLoadThreadState *threadState = callocStruct(ContainerLoadThreadState);
	int i;

	if (!gContainerSource.validSource)
	{
		free(threadState);
		return;
	}

	PERFINFO_AUTO_START("objMergeIncrementalHogs:HogMerge",1);

	gContainerSource.sourceInfo->iCurrentSequenceNumber = 0;
	gContainerSource.sourceInfo->iCurrentTimeStamp = 0;
	gContainerSource.sourceInfo->iLastSequenceNumber = 0;
	gContainerSource.sourceInfo->iLastTimeStamp = 0;

	eaCreate(&loadState.renames);
	loadState.loadThreadState = threadState;
	threadState->bReadDeletes = true;

	estrStackCreate(&databaseDir);
	estrCopy2(&databaseDir, gContainerSource.sourcePath);
	//stupid destructive getter function.
	getDirectoryName(databaseDir);
	estrSetSize(&databaseDir, (unsigned int)strlen(databaseDir));

	fprintf(fileGetStderr(), "Writing FAST snapshot.\n");
	log_printf(LOG_CONTAINER,"Writing FAST snapshot.\n");

	fprintf(fileGetStderr(), "Loading incremental hogs from:%s \n", databaseDir);
	fprintf(fileGetStderr(), "Multi-threaded Load Enabled.");
	fprintf(fileGetStderr(), "\n");

	//This should block all container persist updates until we're done merging.
	EnterCriticalSection(&gContainerSource.hog_access);
	do {
		loadstart_printf("Loading snapshot...");
		//Find the last snapshot.
		fileScanAllDataDirs(databaseDir, SnapshotHogFileScanCallback, &lastSnapshot);
		if (lastSnapshot)
		{
			char *filename = lastSnapshot;			

			fprintf(fileGetStderr(), "Loading snapshot:%s ...", lastSnapshot);
			log_printf(LOG_CONTAINER,"Loading snapshot:%s ...", lastSnapshot);

			if(objMergeIncrementalHogFileEx(&snapshotHog, filename, &loadState, threadState, true, bAppend))
			{
				gContainerSource.scanSnapshotTime = gContainerSource.sourceInfo->iCurrentTimeStamp;
				gContainerSource.scanSnapshotSeq = gContainerSource.sourceInfo->iCurrentSequenceNumber;
				snapshotLoaded = true;

				fprintf(fileGetStderr(), "done.\n\n");
			}

			if (!snapshotLoaded)
			{
				FileRenameRequest *mv = callocStruct(FileRenameRequest);
				mv->filename = strdup(filename);
				mv->moveToOld = true;
				eaPush(&loadState.renames, mv);
				fprintf(fileGetStderr(), "Hog was invalid:%s. ", filename);
				log_printf(LOG_CONTAINER,"Hog was invalid:%s. ", filename);

				if (snapshotHog)
					hogFileDestroy(snapshotHog, false /*JE: Unsure if this is leaking, leaving as it was before to be safe (might leak)*/);

				ErrorOrAlert("DATABASE.LOADFAILURE", "Could not load snapshot file %s.", lastSnapshot);
				return;
			}
		}
		else
		{
			char *filename;
			estrStackCreate(&filename);

			objMakeSnapshotFilename(&filename, objContainerGetTimestamp(), NULL);
			snapshotHog = objOpenIncrementalHogFile(filename);

			fprintf(fileGetStderr(), "Creating incremental snapshot: %s\n", filename);
			log_printf(LOG_CONTAINER,"Creating incremental snapshot: %s\n", filename);

			estrDestroy(&filename);
			if (isProductionMode())
				ErrorOrAlert("DATABASE.LOADFAILURE", "Could not find a suitable snapshot. If this a fresh shard you can ignore this alert.");
		}

		loadend_printf("");

		loadstart_printf("Merging incremental hogs");

		//Scan the logs, merge the hogs, feed the frogs.
		fileScanAllDataDirs(databaseDir, IncrementalHogMergeCallback, &loadState);
		fprintf(fileGetStderr(), "%d incremental hogs merged. Last sequence number was %"FORM_LL"u.\n", loadState.fileCount, gContainerSource.sourceInfo->iLastSequenceNumber);

		
		if (loadState.containersLoaded != loadState.containersRequested)
		{
			TriggerAlertf("OBJECTDB.PARTIAL_LOAD_FAILURE",ALERTLEVEL_WARNING,ALERTCATEGORY_NETOPS,0, 0, 0, GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0,
				"Could not load all containers (loaded %"FORM_LL"u of %"FORM_LL"u) from incremental hogs.\n"
				"See CONTAINER log for load errors.\n", loadState.containersLoaded, loadState.containersRequested);
		}

		loadend_printf("");
		
		PERFINFO_AUTO_STOP();
	} while (false);
	LeaveCriticalSection(&gContainerSource.hog_access);

	//Create Snapshot if there were changes or there was no snapshot loaded.
	if (gContainerSource.forceSnapshot || replayedTransactions > 0 || loadState.fileCount > 0 || !snapshotLoaded)
	{
		if (!gContainerSource.forceSnapshot && (gContainerSource.sourceInfo->iCurrentSequenceNumber <= gContainerSource.scanSnapshotSeq && lastSnapshot))
		{
			bWriteBackup = false;
			fprintf(fileGetStderr(), "Skipping snapshot creation, previous snapshot is up to date: %s\n", lastSnapshot);
		}
		else
		{
			objModifyIncrementalSnapshot(snapshotHog, &loadState, threadState, lastSnapshot, &nextSnapshot);
		}
	}
	else
	{
		bWriteBackup = false;
		fprintf(fileGetStderr(), "Skipping snapshot creation, existing last snapshot:%s\n", lastSnapshot);
	}

	free(threadState);

	//Force write all schemas out to the hog; Just do it blindly here since the new merger somehow skips this step in certain cases.
	if(snapshotHog)
	{
		int s, count = objNumberOfContainerSchemas();
		for(s=0; s<count; s++)
		{
			GlobalType type = objGetContainerSchemaGlobalTypeByIndex(s);
			ContainerSourceUpdate conup = {0};
			conup.hog = snapshotHog;
			conup.type = type;
			wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINERSCHEMA, &conup, sizeof(ContainerSourceUpdate));
		}

		if(snapshotLoaded)
		{
			HogFileIndex hfi;

			wtFlush(gContainerSource.hog_update_thread);

			hfi = hogFileFind(snapshotHog, CONTAINER_INFO_FILE);

			if (hfi != HOG_INVALID_INDEX)
			{
				ContainerRepositoryInfo *hogInfo = StructCreate(parse_ContainerRepositoryInfo);
				char *fileData;
				int fileSize;
				fileData = hogFileExtract(snapshotHog,hfi,&fileSize, NULL);
				//Check for a valid source info
				if (ParserReadText(fileData,parse_ContainerRepositoryInfo,hogInfo, 0))
				{
					if (hogInfo->iLastSequenceNumber == 0 &&
						hogInfo->iCurrentSequenceNumber == 0 && 
						hogInfo->iCurrentTimeStamp == 0 &&
						hogInfo->iLastTimeStamp == 0 &&
						!eaSize(&hogInfo->ppRepositoryTypeInfo))
					{
						ErrorOrCriticalAlert("OBJECTDB.MERGER.EMPTYCONTAINERINFO", "The merger created snapshot %s with an empty container info. If this a fresh shard you can ignore this alert.", hogFileGetArchiveFileName(snapshotHog));
					}
				}
				free(fileData);
				StructDestroy(parse_ContainerRepositoryInfo, hogInfo);
			}
		}
	}

	objCloseHogOnHogThread(snapshotHog);

	for (i = 0; i < eaSize(&loadState.openedIncrementals); i++)
	{
		objCloseHogOnHogThread(loadState.openedIncrementals[i]);
	}
	eaDestroy(&loadState.openedIncrementals);

	objFlushContainers();

	RenameFiles(&loadState.renames);

	//clean up old files
	estrConcatf(&databaseDir, "/%s", "old");
	makeDirectories(databaseDir);
	objPruneOldDirectory(databaseDir);

	estrDestroy(&databaseDir);

	fprintf(fileGetStderr(), "Waiting for IO to complete.\n");


	objCloseAllWorkingFiles();

	if (gContainerSource.generateLogReport)
	{
		fprintf(fileGetStderr(), "Generating log report.\n");
		objGenerateLogReport();
	}

	if (bWriteBackup)
	{
		BackupSnapshot(nextSnapshot, bMoveBackupToOld);
	}
	
	estrDestroy(&lastSnapshot);
	estrDestroy(&nextSnapshot);

	log_printf(LOG_CONTAINER, "Snapshot created.");
	fprintf(fileGetStderr(), "Snapshot created... exiting in 30 seconds.\n");
	Sleep(30000);
}

// FAST multithreaded container saving

// Debug command: lag this many milliseconds before saving each container
static U32 siLagOnContainerSave = 0;
AUTO_CMD_INT(siLagOnContainerSave, LagOnContainerSave) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

static void mwtInput_objSaveContainer(ContainerUpdate *request)
{
	static PERFINFO_TYPE *perfs[GLOBALTYPE_MAXTYPES];
	int retries = 0;
	int writeSuccess;
	Container *sourceContainer = NULL;
	ObjectLock *lock = NULL;

	ScratchStackSetThreadSize(16*1024*1024);

	PERFINFO_AUTO_START_FUNC();

	// First, grab throttle semaphore.
	if (!sbDisableContainerSaveSemaphore)
	{
		semaphoreWait(sContainerSavesOutstanding);
	}

	if (siLagOnContainerSave)
	{
		Sleep(siLagOnContainerSave);
	}

	PERFINFO_AUTO_START_STATIC(GlobalTypeToName(request->containerType), &perfs[request->containerType], 1);

	// Second, acquire the source container locks, if appropriate.
	lock = objLockContainerByTypeAndID(request->containerType, request->containerID);
	sourceContainer = request->sourceContainer;

	while (sourceContainer && InterlockedIncrement(&sourceContainer->updateLocked) > 1)
	{
		InterlockedDecrement(&sourceContainer->updateLocked);
		Sleep(0);
	}

	if (!request->sourceContainer)
	{
		// Oh hey, the source container disappeared! This means an update happened, so request->container is now a temp copy
		// So we can release the locks and go about our business
		if (sourceContainer)
		{
			InterlockedDecrement(&sourceContainer->updateLocked);
		}

		objUnlockContainerLock(&lock);
		sourceContainer = NULL;
	}

	// Wait for main thread to finish copying
	request->estring = AllocConupEstr();

	writeSuccess = objContainerToEstring(request);

	assert(request->container->containerType == request->containerType && request->container->containerID == request->containerID);

	if (request->container->isTemporary)
	{
		objDestroyContainer((Container *)request->container);
		request->container = NULL;
	}
	
	if (!writeSuccess)
	{
		log_printf(LOG_CONTAINER, "Failed to write container %s[%d]: Can't Write Data", GlobalTypeToName(request->container->containerType), request->container->containerID);
		FreeConupEstrSemaphoreSignal(&request->estring);
		request->estring = NULL;
	}

	if (request->estring)
	{
		objZipContainerData(request, objContainerGetSystemTimestamp(), FreeConupEstrCB);
	}

	//Checks container
	CacheZippedContainerData(request);

	request->container = NULL;

	if (sourceContainer)
	{
		request->sourceContainer = NULL;
		sourceContainer->savedUpdate = NULL;
		InterlockedDecrement(&sourceContainer->updateLocked);
		objUnlockContainerLock(&lock);
	}
	PERFINFO_AUTO_STOP();

	while (!mwtQueueOutput(s_saveThreadManager, request) && retries < 1000000)
	{
		retries++;
		Sleep(1);
	}
	assertmsg(retries < 1000000, "Main thread can't keep up with container saves, something is wrong");
	PERFINFO_AUTO_STOP();
}

static U32 gThreadedSaveStartTime = 0;
static U32 gThreadedSaveCopyCount = 0;

static void mwtOutput_objSaveContainer(ContainerUpdate *request)
{
	s_pendingSaveCount--;

	if (estrLength(&request->estring) || request->useEntry)
	{	
		gContainerSource.needsRotate = true;
		wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSAVECONTAINER, request, sizeof(ContainerUpdate));

		request->estring = NULL; // freed by hog update thread
		request->entry.data = NULL; //freed by hog update thread
		request->entry.header_data = NULL; //freed by hog update thread
	}

	if (s_pendingSaveCount == 0)
	{		
		wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSETSEQUENCENUMBER, &s_pendingSequenceNumber, sizeof(s_pendingSequenceNumber));
		s_pendingSequenceNumber = 0;
		if(gbSlowSaveTickAlertSent)
		{
			ErrorOrCriticalAlert("OBJECTDB_SLOW_SAVE_TICK", "Slow Save tick started at %d has now finished, with at total time of %d seconds.", gContainerSource.lastSaveTick, timeSecondsSince2000() - gContainerSource.lastSaveTick);
			gbSlowSaveTickAlertSent = false;
		}
		gContainerSource.lastSaveTick = 0;
		if (gContainerSource.rotateQueued)
		{
			objInternalRotateIncrementalHog();
		}
		servLog(LOG_CONTAINER, "SaveTickStats", "duration %u copies %u", timeSecondsSince2000() - gThreadedSaveStartTime, gThreadedSaveCopyCount);
	}
	DestroyContainerUpdate(request);
}

static void objFlushModifiedContainersThreaded(void)
{
	if (s_saveThreadManager)
	{	
		PERFINFO_AUTO_START_FUNC();
		do {
			mwtProcessOutputQueue(s_saveThreadManager);
		} while (mwtProcessQueueDirect(s_saveThreadManager)); // Finish last save if needed
		mwtWakeThreads(s_saveThreadManager);
		mwtSleepUntilDone(s_saveThreadManager);
		mwtProcessOutputQueue(s_saveThreadManager);
		PERFINFO_AUTO_STOP();
	}
}

int numSaveThreads = 2;

AUTO_CMD_INT(numSaveThreads, numSaveThreads) ACMD_CMDLINE;

static int objSaveModifiedContainersThreaded_Internal(TrackedContainerArray *modifiedContainers)
{
	int saveCount = 0;
	int i;
	int size = eaSize(&modifiedContainers->trackedContainers);

	if(!size)
		return size;

	for (i = 0; i < size; i++)
	{
		Container *object;
		TrackedContainer *trackedCon = modifiedContainers->trackedContainers[i];
		object = objGetContainerEx(trackedCon->conRef.containerType, trackedCon->conRef.containerID, false, false, true);

		if (object)
		{
			if(object->isModified)
			{
				ContainerUpdate *conup = PopulateContainerUpdate(object);
			
				mwtQueueInput(s_saveThreadManager, conup, true);
				s_pendingSaveCount++;

				object->isModified = false;
				saveCount++;
			}
			objUnlockContainer(&object);
		}
		else
		{
			object = objGetDeletedContainerEx(trackedCon->conRef.containerType, trackedCon->conRef.containerID, false, false, true);
			if (object)
			{
				if(object->isModified)
				{
					ContainerUpdate *conup = PopulateContainerUpdate(object);
					conup->iDeletedTime = objGetDeletedTime(object->containerType, object->containerID);
								
					mwtQueueInput(s_saveThreadManager, conup, true);
					s_pendingSaveCount++;

					object->isModified = false;
					saveCount++;
				}
				objUnlockContainer(&object);
			}
		}
	}
	ClearTrackedContainerArray(modifiedContainers);
	return saveCount;
}

static void objSaveModifiedContainersThreaded(bool rotateAfterSaving)
{
	int size = 0;
	int saveCount = 0, deleteCount = 0;
	U32 currentTime = objContainerGetTimestamp();

	PERFINFO_AUTO_START_FUNC();

	if (!s_saveThreadManager)
	{	
		int thread_count;	

		if(numSaveThreads)
			thread_count = numSaveThreads;
		else
			thread_count = getNumRealCpus()/2;

		if(isProductionMode())
			s_saveThreadManager = mwtCreate(10*1024*1024, 10*1024*1024, thread_count, NULL, NULL, mwtInput_objSaveContainer, mwtOutput_objSaveContainer, "ObjectSaveThread");
		else
			s_saveThreadManager = mwtCreate(512*1024, 512*1024, thread_count, NULL, NULL, mwtInput_objSaveContainer, mwtOutput_objSaveContainer, "ObjectSaveThread");
	}

	if (s_pendingSaveCount > 0)
	{	
		objFlushModifiedContainersThreaded();
	}
	assert (s_pendingSaveCount == 0);

	if (gContainerSource.preSaveCallback)
	{
		gContainerSource.preSaveCallback();
	}

	gContainerSource.rotateQueued = rotateAfterSaving;
	gThreadedSaveStartTime = timeSecondsSince2000();
	InterlockedExchange(&gThreadedSaveCopyCount, 0);

	objLockGlobalContainerRepository();
	deleteCount = objPermanentlyDeleteRemovedContainers();
	objSaveContainerStoreMaxIDs();
	s_pendingSequenceNumber = gContainerSource.sourceInfo->iCurrentSequenceNumber;

	size += eaSize(&gContainerRepository.modifiedEntityPlayers.trackedContainers);
	saveCount += objSaveModifiedContainersThreaded_Internal(&gContainerRepository.modifiedEntityPlayers);
	size += eaSize(&gContainerRepository.modifiedEntitySavedPets.trackedContainers);
	saveCount += objSaveModifiedContainersThreaded_Internal(&gContainerRepository.modifiedEntitySavedPets);
	size += eaSize(&gContainerRepository.modifiedContainers.trackedContainers);
	saveCount += objSaveModifiedContainersThreaded_Internal(&gContainerRepository.modifiedContainers);

	objUnlockGlobalContainerRepository();
	if (size > 0) 
	{
		verbose_printf("Saved %d modified containers.\n", saveCount);
	}
	if (!saveCount)
	{	
		// rotate in main thread because nothing was queued.
		wtQueueCmd(gContainerSource.hog_update_thread, HOGUPDATETHREAD_CMDSETSEQUENCENUMBER, &s_pendingSequenceNumber, sizeof(s_pendingSequenceNumber));
		s_pendingSequenceNumber = 0;
		if (rotateAfterSaving)
		{
			objInternalRotateIncrementalHog();
		}
	}

	PERFINFO_AUTO_STOP();	
}

void objHandleUpdateDuringSave(Container *con)
{
	if (con->savedUpdate)
	{
		while (InterlockedIncrement(&con->updateLocked) > 1) // stall until writing done
		{
			InterlockedDecrement(&con->updateLocked);
			Sleep(0);
		}

		// If there's a pending update, make a write copy of the container and then clear the saved update
		// This will mean that the save thread no longer cares about the actual container object
		if(con->savedUpdate)
		{
			// When the save thread gets to it, it will use the copy; source container no longer required
			InterlockedIncrement(&gThreadedSaveCopyCount);
			con->savedUpdate->container = objContainerCopy(con, true, "Creating copy in objHandleUpdateDuringSave");
			con->savedUpdate->sourceContainer = NULL;
			con->savedUpdate = NULL;
		}
		InterlockedDecrement(&con->updateLocked);
	}
}

void objHandleDestroyDuringSave(Container *con)
{
	objHandleUpdateDuringSave(con);
}

void objMakeOfflineFilename(char **estr, U32 atTime)
{
	char *fname_orig = gContainerSource.sourcePath;
	char *dotStart = FindExtensionFromFilename(fname_orig);
	char timestring[MAX_PATH];
	int namelen = dotStart - fname_orig;

	if (gContainerSource.sourcePath)
	{
		if (namelen <=0)
			namelen = (int)strlen(fname_orig);

		estrAppendUnescapedCount(estr, fname_orig, namelen);
	}
	else
	{
		estrConcatf(estr, "db");
	}
	
	timeMakeFilenameDateStringFromSecondsSince2000(timestring, atTime);

	estrConcatf(estr,"%s%s%s%s",OFFLINE_HOG_PREFIX, timestring, OFFLINE_HOG_SUFFIX, dotStart);
}

//Scan the data dir for snapshots. After scanning, the estr in pUserData should have the last one.
static FileScanAction OfflineHogFileScanCallback(char *dir, struct _finddata32_t* data, char **lastOffline)
{
	const char *current = "";
	char fdir[MAX_PATH];
	FileScanAction retval = FSA_NO_EXPLORE_DIRECTORY;

	strcpy(fdir, dir);
	//getDirectoryName(fdir);

	if(!(data->attrib & _A_SUBDIR))
	{
		strcat(fdir, "/");
		strcat(fdir, data->name);

		//Check for offline hogs
		if (objStringIsOfflineFilename(data->name))
		{
			if (fileExists(*lastOffline)) 
				objMoveToOldFilesDir(*lastOffline);

			estrCopy2(lastOffline, fdir);
		}
	}

	return retval;
}

char *GetCurrentOfflineHogFilename(void)
{
	char *filename = NULL;
	char *databaseDir = NULL;
	estrStackCreate(&databaseDir);

	estrCopy2(&databaseDir, gContainerSource.sourcePath);
	//stupid destructive getter function.
	getDirectoryName(databaseDir);
	estrSetSize(&databaseDir, (unsigned int)strlen(databaseDir));

	fileScanAllDataDirs(databaseDir, OfflineHogFileScanCallback, &filename);

	estrDestroy(&databaseDir);
	return filename;
}

//Scan the data dir for snapshots. After scanning, the estr in pUserData should have the last one.
static FileScanAction TempOfflineHogFileScanCallback(char *dir, struct _finddata32_t* data, void *unused)
{
	const char *current = "";
	char fdir[MAX_PATH];
	FileScanAction retval = FSA_NO_EXPLORE_DIRECTORY;

	strcpy(fdir, dir);
	//getDirectoryName(fdir);

	if(!(data->attrib & _A_SUBDIR))
	{
		strcat(fdir, "/");
		strcat(fdir, data->name);

		//Check for offline hogs
		if (objStringIsTempOfflineFilename(data->name))
		{
			fprintf(fileGetStderr(),"\tRemoving temp file: %s.\n", data->name);
			fileForceRemove(fdir);
		}
	}

	return retval;
}

void CleanupTempOfflineHogFiles(void)
{
	char *databaseDir = NULL;
	estrStackCreate(&databaseDir);

	estrCopy2(&databaseDir, gContainerSource.sourcePath);
	//stupid destructive getter function.
	getDirectoryName(databaseDir);
	estrSetSize(&databaseDir, (unsigned int)strlen(databaseDir));

	fileScanAllDataDirs(databaseDir, TempOfflineHogFileScanCallback, NULL);

	estrDestroy(&databaseDir);
}

HogFile *OpenOfflineHogg(bool bAllowWrites, bool bAppendMode, bool *bCreated)
{
	char *filename = NULL;
	HogFile *hogfile = NULL;

	filename = GetCurrentOfflineHogFilename();

	if(!filename)
	{
		objMakeOfflineFilename(&filename, timeSecondsSince2000());
	}

	if(filename)
	{
		if(bAllowWrites)
		{
			HogFileCreateFlags flags = HOG_MUST_BE_WRITABLE|HOG_NO_STRING_CACHE;
			if(bAppendMode)
				flags |= HOG_APPEND_ONLY;
			hogfile = hogFileReadEx(filename, bCreated, PIGERR_ASSERT,NULL,flags, DB_HOG_DATALIST_JOURNAL_SIZE);
		}
		else
		{
			hogfile = hogFileReadEx(filename, NULL, PIGERR_ASSERT,NULL,HOG_READONLY|HOG_NO_STRING_CACHE|HOG_NOCREATE, DB_HOG_DATALIST_JOURNAL_SIZE);
		}

		strcpy(gContainerSource.offlinePath, filename);
		gContainerSource.offlineHog = hogfile;
		estrDestroy(&filename);
	}

	return hogfile;
}

void CloseOfflineHogg()
{
	hogFileDestroy(gContainerSource.offlineHog, true);
	gContainerSource.offlineHog = NULL;
}

void objWriteContainerToOfflineHog(GlobalType containerType, ContainerID containerID)
{
	bool writeSuccess = false;
	ContainerUpdate conup = {0};

	PERFINFO_AUTO_START_FUNC();

	conup.containerType = containerType;
	conup.containerID = containerID;

	conup.container = objGetContainerEx(containerType, containerID, false, true, false);

	assert(conup.container);

	while (conup.container && InterlockedIncrement(&conup.container->updateLocked) > 1)
	{
		InterlockedDecrement(&conup.container->updateLocked);
		Sleep(0);
	}

	if(conup.container->containerData)
	{
		estrCreate(&conup.estring);
		writeSuccess = objContainerToEstring(&conup);

		if (!writeSuccess)
		{
			log_printf(LOG_CONTAINER, "Failed to write container %s[%d]: Can't Write Data", GlobalTypeToName(conup.container->containerType), conup.container->containerID);
			if(conup.container)
				InterlockedDecrement(&conup.container->updateLocked);
			conup.container = NULL;
			estrDestroy(&conup.estring);
			conup.estring = NULL;
			PERFINFO_AUTO_STOP();
			objDecrementServiceTime();
			return;
		}

		if (objFileModifyUpdateNamed(gContainerSource.offlineHog, conup.fileName, conup.estring, estrLength(&conup.estring)+1, objContainerGetSystemTimestamp(), conup.serviceTime) != 0)
		{ 
			log_printf(LOG_CONTAINER, "Failed to write container %s[%d]: Can't Modify Hog", GlobalTypeToName(containerType), containerID);
		}
	}
	else if(conup.container->fileData)
	{
		const char *ext;
		NewPigEntry entry = {0};
		if(conup.container->header)
			ext = ".co2";
		else
			ext = ".con";
		sprintf(conup.fileName,"%s/%d%s", GlobalTypeToName(conup.container->containerType), conup.container->containerID,ext);
		
		if(conup.container->bytesCompressed)
		{
			entry.data = malloc(conup.container->bytesCompressed);
			memcpy(entry.data, conup.container->fileData, conup.container->bytesCompressed);
		}
		else
		{
			entry.data = malloc(conup.container->fileSize);
			memcpy(entry.data, conup.container->fileData, conup.container->fileSize);
		}

		entry.size = conup.container->fileSize;
		entry.pack_size = conup.container->bytesCompressed;
		entry.fname = conup.fileName;
		entry.timestamp = objContainerGetSystemTimestamp();
		entry.header_data_size = conup.container->headerSize;
		if(conup.container->headerSize)
		{
			U8 *header_data = malloc(conup.container->headerSize);
			memcpy(header_data, conup.container->rawHeader, conup.container->headerSize);
			entry.header_data = header_data;
		}

		entry.free_callback = NULL;
		entry.checksum[0] = conup.container->checksum;
		if(conup.container->oldFileNoDevassert) // Only devassert if the container is more recent than 8/2010
			entry.no_devassert = 1;

		if(hogFileModifyUpdateNamedAsync2(gContainerSource.offlineHog, &entry) != 0)
		{ 
			log_printf(LOG_CONTAINER, "Failed to write container %s[%d]: Can't Modify Hog", GlobalTypeToName(containerType), containerID);
		}
	}
	else
	{
		assertmsg(0, "Trying to write out a container with no data.");
		if(conup.container)
			InterlockedDecrement(&conup.container->updateLocked);
		return;
	}

	if(conup.container)
		InterlockedDecrement(&conup.container->updateLocked);

	conup.container = NULL;

	PERFINFO_AUTO_STOP();
}

void objRemoveContainerFromOfflineHog(GlobalType containerType, ContainerID containerID)
{
	bool writeSuccess = false;
	char fileName[MAX_PATH];
	HogFileIndex hogIndex;
	ContainerStore * store = objFindContainerStoreFromType(containerType);
	const char *ext;

	PERFINFO_AUTO_START_FUNC();

	if(store && store->requiresHeaders)
		ext = ".co2";
	else
		ext = ".con";
	sprintf(fileName,"%s/%d%s", GlobalTypeToName(containerType),
		containerID,ext);

	hogIndex = hogFileFind(gContainerSource.offlineHog, fileName);

	if (hogIndex == HOG_INVALID_INDEX)
		return;

	if (hogFileModifyDeleteNamed(gContainerSource.offlineHog, fileName) != 0)
	{ 
		log_printf(LOG_CONTAINER, "Failed to delete container %s[%d]: Can't Modify Hog", GlobalTypeToName(containerType), containerID);
	}

	PERFINFO_AUTO_STOP();
}

void FlushOfflineHogg()
{
	hogFileModifyFlush(gContainerSource.offlineHog);
}

void LaunchOfflineBackupScript()
{
	if (gContainerIOConfig.OfflineBackupScript[0])
	{
		int pid = 0;
		char *buf = NULL;
		char *backupHogOldDir = NULL;

		estrStackCreate(&buf);
		estrPrintf(&buf, "%s", gContainerIOConfig.OfflineBackupScript);

		estrReplaceOccurrences(&buf, "%FILE%", gContainerSource.offlinePath);

		log_printf(LOG_CONTAINER, "Running offline backup script: %s", buf);

		pid = system_detach(buf, 0, 0);

		fprintf(fileGetStderr(), "Running offline backup script [pid:%d]:\n%s\n",pid,buf);

		estrDestroy(&buf);
	}
}

void RenameOfflineHogg()
{
	char *databaseDir = NULL;
	char *filename = NULL;
	char *newfilename = NULL;
	HogFile *hogfile = NULL;

	estrStackCreate(&databaseDir);

	estrCopy2(&databaseDir, gContainerSource.sourcePath);
	//stupid destructive getter function.
	getDirectoryName(databaseDir);
	estrSetSize(&databaseDir, (unsigned int)strlen(databaseDir));

	fileScanAllDataDirs(databaseDir, OfflineHogFileScanCallback, &filename);
	
	if(filename)
	{
		objMakeOfflineFilename(&newfilename, timeSecondsSince2000());
		if(newfilename)
		{
			if(fileMove(filename, newfilename) != 0 && isProductionMode()) AssertOrAlert("OBJECTDB_RENAME_FAILED", "Failed Renaming %s to %s.\n", filename, newfilename);
		}
	}

	estrDestroy(&databaseDir);
}

static bool gbSkipOfflineHoggReopenForReplay = false;
AUTO_CMD_INT(gbSkipOfflineHoggReopenForReplay, SkipOfflineHoggReopenForReplay) ACMD_CMDLINE;

void ReOpenOfflineHoggForRestores()
{
	if(gbSkipOfflineHoggReopenForReplay)
		return;

	CloseOfflineHogg();
	RenameOfflineHogg();
	OpenOfflineHogg(false, false, NULL);
}

AUTO_FIXUPFUNC;
TextParserResult fixupContainerRepositoryInfo(ContainerRepositoryInfo* info, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		eaIndexedEnable(&info->ppRepositoryTypeInfo, parse_ContainerRepositoryTypeInfo);
	}

	return 1;
}

void FreeZippedContainerData(Container *con)
{
	char *data;

	if(dbDisableZippedDataCachingInSaveThreads)
		return;

	if(!con)
		return;

	//Don't throw away fileData if there isn't containerData
	if(!con->containerData)
		return;

	//Don't get the lock if there isn't fileData to remove
	if(!con->fileData)
		return;

	PERFINFO_AUTO_START_FUNC();
	while (InterlockedIncrement(&con->updateLocked) > 1)
	{
		InterlockedDecrement(&con->updateLocked);
		Sleep(0);
	}

	data = con->fileData;
	con->fileData = NULL;

	InterlockedDecrement(&con->updateLocked);

	SAFE_FREE(data);
	PERFINFO_AUTO_STOP();
}

U32 GetDefragDay()
{
	SYSTEMTIME	t;
	U32 iTime = timeSecondsSince2000();

	timerLocalSystemTimeFromSecondsSince2000(&t,iTime);
	t.wHour = t.wMinute = t.wSecond =  t.wMilliseconds = 0;

	return timerSecondsSince2000FromLocalSystemTime(&t);
}
static bool HourIsBetween(U32 now, U32 start, U32 end)
{
	if(start <= end)
		return now >= start && now <= end;
	else
		return now <= end || now >= start;
}

bool TimeToDefrag(U32 iDaysBetweenDefrags, U32 lastDefragDay, U32 iTargetDefragWindowStart, U32 iTargetDefragWindowDuration)
{
	if(iDaysBetweenDefrags)
	{
		SYSTEMTIME	t;
		U32 iTime = timeSecondsSince2000();
		U32 iToday;
		U32 iCurrentHour;

		timerLocalSystemTimeFromSecondsSince2000(&t,iTime);
		iCurrentHour = t.wHour;
		t.wHour = t.wMinute = t.wSecond =  t.wMilliseconds = 0;

		iToday = timerSecondsSince2000FromLocalSystemTime(&t);
		if(iToday >= lastDefragDay + iDaysBetweenDefrags*60*60*24)
		{
			return HourIsBetween(iCurrentHour, iTargetDefragWindowStart, iTargetDefragWindowStart + iTargetDefragWindowDuration);
		}
	}

	return false;
}

void AddRequestsToThreadState(const char *filename, ContainerLoadThreadState *threadState)
{
	bool incrementalLoaded = false;
	HogFileIndex hfi;
	HogFile *snapshot;
	hogSetMaxBufferSize(OBJ_HOG_BUFFER_SIZE);
	snapshot = hogFileReadEx(filename,NULL,PIGERR_ASSERT,NULL,HOG_NOCREATE|HOG_NO_STRING_CACHE, DB_HOG_DATALIST_JOURNAL_SIZE);
	hogFileSetSingleAppMode(snapshot, true);

	if (!snapshot)
	{
		FatalErrorf("Could not open hog:%s\n", filename);
		return;
	}
	hfi = hogFileFind(snapshot, CONTAINER_INFO_FILE);

	if (hfi != HOG_INVALID_INDEX)
	{
		ContainerRepositoryInfo *hogInfo = StructCreate(parse_ContainerRepositoryInfo);
		char *fileData;
		int fileSize;
		U32 fileCount;
		bool result = true;
		fileData = hogFileExtract(snapshot,hfi,&fileSize, NULL);
		fileCount = hogFileGetNumUserFiles(snapshot) - 1;
		//Check for a valid source info
		if (ParserReadText(fileData,parse_ContainerRepositoryInfo,hogInfo, 0))
		{
			int i;

			MAX1(gContainerSource.sourceInfo->iLastSequenceNumber, hogInfo->iLastSequenceNumber);
			MAX1(gContainerSource.sourceInfo->iLastTimeStamp, hogInfo->iLastTimeStamp);

			hogInfo->iCurrentSequenceNumber = hogInfo->iLastSequenceNumber;
			hogInfo->iCurrentTimeStamp = hogInfo->iLastTimeStamp;

			MAX1(gContainerSource.sourceInfo->iCurrentSequenceNumber, hogInfo->iCurrentSequenceNumber);
			MAX1(gContainerSource.sourceInfo->iCurrentTimeStamp, hogInfo->iCurrentTimeStamp);

			EARRAY_FOREACH_BEGIN(hogInfo->ppRepositoryTypeInfo, k);
			{
				ContainerRepositoryTypeInfo *typeInfo = hogInfo->ppRepositoryTypeInfo[k];
				bool bFound = false;
				ContainerRepositoryTypeInfo *foundInfo = eaIndexedGetUsingInt(&gContainerSource.sourceInfo->ppRepositoryTypeInfo, typeInfo->eGlobalType);

				if(foundInfo)
				{
					if(typeInfo->iMaxContainerID > foundInfo->iMaxContainerID)
						foundInfo->iMaxContainerID = typeInfo->iMaxContainerID;
				}
				else
				{
					eaPush(&gContainerSource.sourceInfo->ppRepositoryTypeInfo, typeInfo);
					hogInfo->ppRepositoryTypeInfo[k] = NULL;
				}
			}
			EARRAY_FOREACH_END;

			incrementalLoaded = true;

			threadState->maxSequence = hogInfo->iLastSequenceNumber;

			hogScanAllFiles(snapshot, QueueContainerHogLoad, threadState);

			CONTAINER_LOGPRINT("%d merger load requests", eaSize(&threadState->ppRequests));

			for (i = 0; i < eaSize(&threadState->ppRequests); i++)
			{
				ContainerLoadRequest *pRequest = threadState->ppRequests[i];
				if (pRequest->the_hog == snapshot)
				{					
					// Load requests from incrementals get the sequence number of the hog
					pRequest->sequenceNumber = hogInfo->iLastSequenceNumber;
				}
			}
			CONTAINER_LOGPRINT("done.\n\n");
		}
	}
}

void AddOfflineRequestsToThreadState(const char *filename, ContainerLoadThreadState *threadState)
{
	bool incrementalLoaded = false;
	HogFile *snapshot;
	hogSetMaxBufferSize(OBJ_HOG_BUFFER_SIZE);
	snapshot = hogFileReadEx(filename,NULL,PIGERR_ASSERT,NULL,HOG_NOCREATE|HOG_NO_STRING_CACHE, DB_HOG_DATALIST_JOURNAL_SIZE);
	hogFileSetSingleAppMode(snapshot, true);

	if (!snapshot)
	{
		FatalErrorf("Could not open hog:%s\n", filename);
		return;
	}

	hogScanAllFiles(snapshot, QueueContainerHogLoad, threadState);

	CONTAINER_LOGPRINT("%d merger load requests", eaSize(&threadState->ppRequests));

	CONTAINER_LOGPRINT("done.\n\n");
}

// Only use this for non-EntityPlayers
void objWriteContainerToSnapshot(HogFile *outputSnapshot, Container *container, U64 sequenceNumber)
{
	bool writeSuccess = false;
	ContainerUpdate *newUpdate= NULL;
	newUpdate = PopulateContainerUpdate(container);
	writeSuccess = objContainerToEstring(newUpdate);
	if(sequenceNumber)
	{
		sprintf(newUpdate->fileName,"%s/%d.v%"FORM_LL"u.con",GlobalTypeToName(container->containerType),container->containerID, sequenceNumber);
		ContainerHogMarkLatest(outputSnapshot, container->containerType, container->containerID, sequenceNumber);
	}
	objZipContainerData(newUpdate, objContainerGetSystemTimestamp(), NULL);
	objFileModifyUpdateNamedZippedEx(outputSnapshot, newUpdate, objContainerGetSystemTimestamp(),newUpdate->serviceTime, NULL);
}

#include "autogen/objcontainerio_h_ast.c"
#include "autogen/objcontainerio_c_ast.c"
