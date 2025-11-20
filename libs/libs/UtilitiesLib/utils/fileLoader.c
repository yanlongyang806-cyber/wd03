#include "fileLoader.h"
#include "fileLoaderStats.h"
#include "MemoryPool.h"
#include "earray.h"
#include "file.h"
#include "timing.h"
#include "XboxThreads.h"
#include "ThreadManager.h"
#include "hoglib.h"
#include "utils.h"
#include "timing_profiler_interface.h"
#include "cpu_count.h"
#include "StringCache.h"

PatchStreamingRequestPatchCallback fileloader_patch_req_callback;
PatchStreamingNeedsPatchingCallback fileloader_patch_needs_callback;
PatchStreamingProcessCallback fileloader_patch_proc_callback;

void fileLoaderSetPatchStreamingCallbacks(PatchStreamingRequestPatchCallback req, PatchStreamingNeedsPatchingCallback needs, PatchStreamingProcessCallback proc)
{
	fileloader_patch_req_callback = req;
	fileloader_patch_needs_callback = needs;
	fileloader_patch_proc_callback = proc;
}


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:fileLoaderThread", BUDGET_EngineMisc););

static ManagedThread *fileLoader_ptr;
static CRITICAL_SECTION fileLoader_CritSect;
static DWORD fileLoader_tid;

// a file this many queues old will be bumped up one level in priority
#define PRIORITY_AGE_MULTIPLIER 20

typedef enum FileLoaderActionType {
	FILELOADER_LOAD,
	FILELOADER_LOAD_FROM_HOGG,
	FILELOADER_LOAD_FROM_HOGG_UNCOMPRESSED,
	FILELOADER_EXEC,
} FileLoaderActionType;

typedef struct FileLoaderAction {
	SLIST_ENTRY slist;
	int age; // must be signed int, relying on this to deal with wraparound issues // actually birthday/uid, not age
	int priority;
	bool bNeedsPatching;
	bool bPatchingStarted;
	FileLoaderActionType type;
	const char *filename; // Needs to be static
	union {
		struct { // LOAD
			FileLoaderFunc loadedCallbackFunc;
			void *data;
			int dataSize;
		};
		struct { // LOAD_FROM_HOGG*
			FileLoaderHoggFunc loadedHoggBackgroundFunc;
			FileLoaderHoggFunc loadedHoggCallbackFunc;
			void *uncompressed;
			int uncompressedSize;
			void *compressed;
			int compressedSize;
			U32 checksum;
		};
		FileLoaderExecFunc callbackFunc; // EXEC
	};
	void *userData;
	
	bool dont_count; // don't increment the file counter

	HogFile * hogg;
	char hogg_name[MAX_PATH];
} FileLoaderAction;
static bool fileLoader_inited=false;
static FileLoaderAction **fileLoader_actions; // ONLY modified from the fileLoaderThread
static FileLoaderAction **fileLoader_actions_need_patching; // ONLY modified from the fileLoaderThread
static FileLoaderAction **fileLoader_results_queue; // Accessed from multiple threads
static FileLoaderAction **fileLoader_results; // Accessed only from main thread
static int fileLoaderAge=0;
static volatile int fileLoader_num_actions_queued;
static volatile int fileLoader_total_actions_queued;
static int fileLoader_num_patching_actions;
static SLIST_HEADER fileLoaderNewElems; // list of eelemnts being sent to the thread, accessed in multiple threads
static HANDLE fileLoaderEvent; // event to awake sleeping fileLoaderThread


FileLoaderStats g_stats;

// Mark the start of a data load in the current history entry.
void fileLoaderStartLoad()
{
	FileLoaderActionTracking * nextAction = g_stats.actionHist + g_stats.historyPos;
	nextAction->actionType = FILELOADER_LOAD;
	nextAction->sizeBytes = 0;

	nextAction->startTime = timerCpuTicks64();
	nextAction->endTime = nextAction->startTime;
}

// Mark the completion and size of a data load in the current history entry, and move to the next entry.
void fileLoaderCompleteLoad(U64 sizeBytes)
{
	FileLoaderActionTracking * nextAction = g_stats.actionHist + g_stats.historyPos;

	nextAction->sizeBytes = sizeBytes;
	nextAction->endTime = timerCpuTicks64();

	g_stats.historyPos = (g_stats.historyPos + 1) % MAX_ACTION_HIST;
}

// Mark a complete (with time span and size) data load in the current history entry, and move to the next entry.
void fileLoaderStatsAddFileLoad(U64 startTime, U64 endTime, U64 sizeBytes)
{
	FileLoaderActionTracking * nextAction = g_stats.actionHist + g_stats.historyPos;
	nextAction->startTime = startTime;
	nextAction->endTime = endTime;
	nextAction->actionType = FILELOADER_LOAD;
	nextAction->sizeBytes = sizeBytes;

	g_stats.historyPos = (g_stats.historyPos + 1) % MAX_ACTION_HIST;
}

// Mark a complete (with time span and size) data file decompress in the current history entry, and move to the next entry.
void fileLoaderStatsAddFileDecompress(U64 startTime, U64 endTime, U64 sizeBytes)
{
	FileLoaderActionTracking * nextAction = g_stats.actionHist + g_stats.historyPos;
	nextAction->startTime = startTime;
	nextAction->endTime = endTime;
	nextAction->actionType = FILELOADER_LOAD_FROM_HOGG;
	nextAction->sizeBytes = sizeBytes;

	g_stats.historyPos = (g_stats.historyPos + 1) % MAX_ACTION_HIST;
}

// Mark a complete (with time span and size) background process execution in the current history entry, and move to the next entry.
void fileLoaderStatsAddExec(U64 startTime, U64 endTime)
{
	FileLoaderActionTracking * nextAction = g_stats.actionHist + g_stats.historyPos;
	nextAction->startTime = startTime;
	nextAction->endTime = endTime;
	nextAction->actionType = FILELOADER_EXEC;
	nextAction->sizeBytes = 0;

	g_stats.historyPos = (g_stats.historyPos + 1) % MAX_ACTION_HIST;
}

// Generate statistics for the set of actions in the history, generally done
// after each action.
void fileLoaderAccumStats()
{
	U64 oldestEventStartTime;
	U64 latestEventEndTime;
	U64 totalLoadedBytes = 0;
	U64 totalLoadingTime = 0;
	U64 totalDecompBytes = 0;
	U64 totalDecompTime = 0;
	U64 historyTimeLength;
	F32 historyTimeLengthSecs;

	U32 eventNum;
	U32 finalEvent = g_stats.historyPos > 0 ? g_stats.historyPos - 1 : MAX_ACTION_HIST - 1;

	if (g_stats.actionHist[finalEvent].endTime == 0)
		// Just one event so far
		finalEvent = g_stats.historyPos;

	oldestEventStartTime = g_stats.actionHist[g_stats.historyPos].startTime;
	latestEventEndTime = g_stats.actionHist[finalEvent].startTime;
	historyTimeLength = latestEventEndTime - oldestEventStartTime;
	for (eventNum = 0; eventNum < MAX_ACTION_HIST; ++eventNum)
	{
		const FileLoaderActionTracking * action = g_stats.actionHist + eventNum;
		if (action->actionType == FILELOADER_LOAD)
		{
			totalLoadedBytes += action->sizeBytes;
			totalLoadingTime += action->endTime - action->startTime;
		}
		else
		if (action->actionType == FILELOADER_LOAD_FROM_HOGG)
		{
			totalDecompBytes += action->sizeBytes;
			totalDecompTime += action->endTime - action->startTime;
		}
	}

	if (historyTimeLength)
	{
		historyTimeLengthSecs = timerSeconds64(historyTimeLength);
		g_stats.summary.idleDiskPerSec = 1.0f - timerSeconds64(totalLoadingTime) / historyTimeLengthSecs;
		g_stats.summary.loadKBPerSec = totalLoadedBytes / (historyTimeLengthSecs * 1024);
		g_stats.summary.loadKBPerSecNonIdle = totalLoadingTime ? totalLoadedBytes / (timerSeconds64(totalLoadingTime) * 1024) : 0.0f;
		g_stats.summary.decompKBPerSec = totalDecompTime ? totalDecompBytes / (timerSeconds64(totalDecompTime) * 1024) : 0.0f;
		g_stats.summary.actionsPerSec = MAX_ACTION_HIST / historyTimeLengthSecs;
	}
}

// Retrieve the statistical summary for the current loader action history.
void fileLoaderGetSummaryStats(FileLoaderSummaryStats * stats_out)
{
	*stats_out = g_stats.summary;
}

// Retrieve the complete current loader action history (the last MAX_ACTION_HIST actions) and statistical summary.
void fileLoaderGetStats(FileLoaderStats * stats_out)
{
	*stats_out = g_stats;
}

MP_DEFINE(FileLoaderAction);

static FileLoaderAction *fileLoaderActionCreate(int priority)
{
	FileLoaderAction *action;
	EnterCriticalSection(&fileLoader_CritSect);
	action = MP_ALLOC(FileLoaderAction);
	action->age = ++fileLoaderAge;
	action->priority = priority;
	LeaveCriticalSection(&fileLoader_CritSect);
	return action;
}

static void fileLoaderActionDestroy(FileLoaderAction *action)
{
	EnterCriticalSection(&fileLoader_CritSect);
	MP_FREE(FileLoaderAction, action);
	LeaveCriticalSection(&fileLoader_CritSect);
}

static int findNextLoad(void)
{
	int i, ret = -1, ret_priority = -1, ret_age=-1;
	int currentAge = fileLoaderAge;

	PERFINFO_AUTO_START_FUNC();

	for (i=eaSize(&fileLoader_actions)-1; i>=0; i--) {
		FileLoaderAction *action = fileLoader_actions[i];
		int effective_priority = action->priority; //  * PRIORITY_AGE_MULTIPLIER + !action->dont_promote * (currentAge - action->age);
		if (action->bNeedsPatching)
			continue;
		if (effective_priority > ret_priority || effective_priority == ret_priority && (ret_age - action->age > 0))
		{
			ret = i;
			ret_priority = effective_priority;
			ret_age = action->age;
		}
	}
	PERFINFO_AUTO_STOP();
	return ret;
}

static int cmpFileLoaderAction(const void *v0, const void *v1)
{
	FileLoaderAction *a0 = *(FileLoaderAction**)v0;
	FileLoaderAction *a1 = *(FileLoaderAction**)v1;
	if (a0->priority > a1->priority)
		return 1;
	if (a1->priority > a0->priority)
		return -1;
	if (a1->age - a0->age > 0)
		return 1;
	if (a0->age - a1->age > 0)
		return -1;
	return 0;
}

static void fileLoaderCheckPatchingStatus()
{
	int i;
	bool bAnyWasFull=false;

	PERFINFO_AUTO_START_FUNC();

	eaQSort(fileLoader_actions_need_patching, cmpFileLoaderAction);

	for (i=eaSize(&fileLoader_actions_need_patching)-1; i>=0; i--)
	{
		FileLoaderAction *action = fileLoader_actions_need_patching[i];
		assert(action->bNeedsPatching);
		if (!action->bPatchingStarted)
		{
			PatchStreamingStartResult r;
			//verbose_printf("findNextLoad: was not started: %s\n", action->filename);
			if (bAnyWasFull)
				r = PSSR_Full;
			else
				// Try to start it
				r = fileloader_patch_req_callback(action->filename);
			if (r == PSSR_Started)
			{
				//verbose_printf("findNextLoad: started: %s\n", action->filename);
				action->bPatchingStarted = true;
			}
			else if (r == PSSR_AlreadyStarted || r == PSSR_NotNeeded)
			{
				// Could happen if it was full, we queued this, then another load for the same
				//  file came in, and that one successfully started it
				// PSSR_NotNeeded implies a separate action already caused this file to be loaded,
				// before we could start it, probably because xfers were full.
				action->bPatchingStarted = true;
				if (r == PSSR_NotNeeded)
					++i;
			}
			else if (r == PSSR_Full)
			{
				bAnyWasFull = true;
				//verbose_printf("findNextLoad: still full: %s\n", action->filename);
				// Still full
			}
			else
				assert(0);
			continue;
		}
		action->bNeedsPatching = fileloader_patch_needs_callback(action->filename);
		if (!action->bNeedsPatching)
		{
			//verbose_printf("findNextLoad: finished: %s\n", action->filename);
			fileLoader_num_patching_actions--;
			eaRemoveFast(&fileLoader_actions_need_patching, i);
		}
	}

	PERFINFO_AUTO_STOP();
}

static void queueResult(FileLoaderAction *action)
{
	EnterCriticalSection(&fileLoader_CritSect);
	eaPush(&fileLoader_results_queue, action);
	LeaveCriticalSection(&fileLoader_CritSect);
}

static void doLoadFromHogg(FileLoaderAction *action)
{
	HogFileIndex hfi;
	U64 startTime, endTime;

	action->uncompressedSize = 0;
	action->compressedSize = 0;
	action->uncompressed = NULL;
	action->compressed = NULL;
	action->checksum = 0;

	if(!action->hogg)
	{
		devassert(0);
		return;
	}
	hfi = hogFileFind(action->hogg, action->filename);
	if(hfi != HOG_INVALID_INDEX)
	{
		// TODO: don't do two reads
		startTime = timerCpuTicks64();
		action->compressed = hogFileExtractCompressed(action->hogg, hfi, &action->compressedSize);
		endTime = timerCpuTicks64();
		if(!action->compressed || action->type == FILELOADER_LOAD_FROM_HOGG_UNCOMPRESSED)
		{
			// either already uncompressed or requested load as uncompressed
			startTime = timerCpuTicks64();
			action->uncompressed = hogFileExtract(action->hogg, hfi, &action->uncompressedSize, NULL);
			endTime = timerCpuTicks64();
			action->checksum = hogFileGetFileChecksum(action->hogg, hfi);

			// loaded in uncompressed form, not quite as accurate if decompressing as loading
			fileLoaderStatsAddFileLoad(startTime, endTime, action->uncompressedSize);
		}
		else
			// loaded in compressed form
			fileLoaderStatsAddFileLoad(startTime, endTime, action->compressedSize);
	}
	if(action->loadedHoggBackgroundFunc)
		action->loadedHoggBackgroundFunc(action->filename, action->uncompressed, action->uncompressedSize,
										 action->compressed, action->compressedSize, action->checksum, action->userData);
}

static bool doAction(FileLoaderAction *action)
{
	bool dont_count = action->dont_count;
	U64 startTime, endTime;
	switch(action->type) {
	xcase FILELOADER_LOAD:
		PERFINFO_AUTO_START("doAction:LOAD", 1);

		startTime = timerCpuTicks64();
		action->data = fileAlloc(action->filename, &action->dataSize);
		endTime = timerCpuTicks64();
		fileLoaderStatsAddFileLoad(startTime, endTime, action->dataSize);

		queueResult(action);
	xcase FILELOADER_LOAD_FROM_HOGG:
	 case FILELOADER_LOAD_FROM_HOGG_UNCOMPRESSED:
		PERFINFO_AUTO_START("doAction:LOAD_FROM_HOGG", 1);
		doLoadFromHogg(action);
		queueResult(action);
	xcase FILELOADER_EXEC:
		PERFINFO_AUTO_START("doAction:EXEC", 1);
		startTime = timerCpuTicks64();
		action->callbackFunc(action->filename, action->userData);
		endTime = timerCpuTicks64();
		fileLoaderStatsAddExec(startTime, endTime);
		fileLoaderActionDestroy(action);
	xdefault:
		assert(0);
	}
	PERFINFO_AUTO_STOP();
	fileLoaderAccumStats();
	return !dont_count;
}

static int fileLoaderForceSingleCoreMode=-1;
// Forces to use or not use the single-core fileLoader mode
AUTO_CMD_INT(fileLoaderForceSingleCoreMode, fileLoaderForceSingleCoreMode) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;

static DWORD WINAPI fileLoaderThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
		int last_total_actions_queued = 0;
		fileLoader_tid = GetCurrentThreadId();
		autoTimerThreadFrameBegin("fileLoaderThread");
			for(;;)
			{
				FileLoaderAction *action;
				int count;
				bool bDidSomething=false;
				if (count=eaSize(&fileLoader_actions)) {
					// We've got stuff to do, choose one and do it!
					int index;

					if (eaSize(&fileLoader_actions_need_patching))
						fileLoaderCheckPatchingStatus();
					
					index = findNextLoad();
					if (index != -1)
					{
						action = eaRemoveFast(&fileLoader_actions, index);
						assert(-1==eaFind(&fileLoader_actions_need_patching, action));
						assert(!action->bNeedsPatching);
						if (doAction(action))
							InterlockedDecrement(&fileLoader_num_actions_queued);
						bDidSomething = true;
					}
				}
				if (!count) // Only yield if we have nothing to do
					WaitForEvent(fileLoaderEvent, INFINITE);
				if (!bDidSomething && count && fileloader_patch_proc_callback) // Some patches are pending, pump it here, the main thread may (or may not) also be pumping it
					fileloader_patch_proc_callback();
				while (action = (FileLoaderAction*)InterlockedPopEntrySList(&fileLoaderNewElems))
				{
					PERFINFO_AUTO_START("queuing new action", 1);
					// if patching required, start them patching!
					if (fileloader_patch_req_callback &&
						action->filename &&
						!action->dont_count &&
						action->type != FILELOADER_LOAD_FROM_HOGG && action->type != FILELOADER_LOAD_FROM_HOGG_UNCOMPRESSED &&
						!fileIsAbsolutePath(action->filename) &&
						strchr(action->filename, '/'))
					{
						// File might be something patchable
						PatchStreamingStartResult r = fileloader_patch_req_callback(action->filename);
						if (r == PSSR_NotNeeded)
						{
							//verbose_printf("queueaction: not needed: %s\n", action->filename);
							action->bNeedsPatching = false;
						}
						else if (r == PSSR_Started) // just started
						{
							//verbose_printf("queueaction: started: %s\n", action->filename);
							fileLoader_num_patching_actions++;
							action->bPatchingStarted = true;
							action->bNeedsPatching = true;
							eaPush(&fileLoader_actions_need_patching, action);
						}
						else if (r == PSSR_AlreadyStarted)
						{
							// This can happen if we're loading the same file in two different ways,
							//  e.g. white texture as both a regular texture and as raw data for an
							//  atlas.
							fileLoader_num_patching_actions++;
							action->bPatchingStarted = true;
							action->bNeedsPatching = true;
							eaPush(&fileLoader_actions_need_patching, action);
						}
						else if (r == PSSR_Full)
						{
							//verbose_printf("queueaction: full: %s\n", action->filename);
							fileLoader_num_patching_actions++;
							action->bPatchingStarted = false;
							action->bNeedsPatching = true;
							eaPush(&fileLoader_actions_need_patching, action);
						}
					} else
						action->bNeedsPatching = false;
					eaPush(&fileLoader_actions, action);
					PERFINFO_AUTO_STOP();
				}
			}
		autoTimerThreadFrameEnd();
		return 0; 
	EXCEPTION_HANDLER_END
} 

void fileLoaderInit(void)
{

	if (!fileLoader_inited) {
		// Tricky logic for thread-safe lazy init
		static volatile int num_people_initing=0;
		int result = InterlockedIncrement(&num_people_initing);
		if (result != 1) {
			while (!fileLoader_inited)
				Sleep(1);
		} else {
			// First person trying to init this
			InitializeCriticalSection(&fileLoader_CritSect);
			MP_CREATE(FileLoaderAction, 32);
			InitializeSListHead(&fileLoaderNewElems);
			fileLoaderEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
			fileLoader_ptr = tmCreateThreadEx(fileLoaderThread, NULL, 256<<10, THREADINDEX_DATASTREAMING);
			assert(fileLoader_ptr);
			//tmSetThreadRelativePriority(fileLoader_ptr, 1); // Now doing too much CPU work (model unpacking) to be high priority
			fileLoader_inited = true;
		}
	}
}

void fileLoaderRequestAsyncLoad(const char *filename, FileLoaderPriority priority, FileLoaderFunc callback, void *userData)
{
	FileLoaderAction *action;
	fileLoaderInit();
	assert(fileLoader_tid != GetCurrentThreadId()); // Shouldn't call an async load from the fileloader thread
	devassert(fileExists(filename) && !fileIsAbsolutePath(filename)); // Should pass complete relative paths
	action = fileLoaderActionCreate(priority);
	assert(filename == allocAddString(filename));
	action->filename = filename;
	action->type = FILELOADER_LOAD;
	action->loadedCallbackFunc = callback;
	action->userData = userData;
	InterlockedIncrement(&fileLoader_num_actions_queued);
	InterlockedIncrement(&fileLoader_total_actions_queued);
	InterlockedPushEntrySList(&fileLoaderNewElems, &action->slist);
	g_file_stats.fileloader_queues++;
	SetEvent(fileLoaderEvent);
}

void fileLoaderRequestAsyncLoadFromHogg(HogFile *hogg, const char *filename, FileLoaderPriority priority, bool uncompressed,
										FileLoaderHoggFunc background, FileLoaderHoggFunc foreground, void *userData)
{
	FileLoaderAction * action;
	fileLoaderInit();
	assert(fileLoader_tid != GetCurrentThreadId()); // Shouldn't call an async load from the fileloader thread
	action = fileLoaderActionCreate(priority);
	action->filename = allocAddString(filename);
	action->type = uncompressed ? FILELOADER_LOAD_FROM_HOGG_UNCOMPRESSED : FILELOADER_LOAD_FROM_HOGG;
	action->hogg = hogg;
	strcpy(action->hogg_name, hogFileGetArchiveFileName(hogg));
	action->loadedHoggBackgroundFunc = background;
	action->loadedHoggCallbackFunc = foreground;
	action->userData = userData;
	InterlockedIncrement(&fileLoader_num_actions_queued);
	InterlockedIncrement(&fileLoader_total_actions_queued);
	InterlockedPushEntrySList(&fileLoaderNewElems, &action->slist);
	g_file_stats.fileloader_queues++;
	SetEvent(fileLoaderEvent);
}

void fileLoaderRequestAsyncExec(const char *filename, FileLoaderPriority priority, bool dontCount, FileLoaderExecFunc callback, void *userData)
{
	FileLoaderAction *action;
	fileLoaderInit();
	assert(fileLoader_tid != GetCurrentThreadId()); // Shouldn't call an async load from the fileloader thread
	action = fileLoaderActionCreate(priority);
	action->filename = filename;
	action->type = FILELOADER_EXEC;
	action->callbackFunc = callback;
	action->userData = userData;
	action->dont_count = dontCount;
	if (!action->dont_count)
	{
		InterlockedIncrement(&fileLoader_num_actions_queued);
		InterlockedIncrement(&fileLoader_total_actions_queued);
		g_file_stats.fileloader_queues++;
	}
	InterlockedPushEntrySList(&fileLoaderNewElems, &action->slist);
	SetEvent(fileLoaderEvent);
}

void fileLoaderCheck(void)
{
	int i;
	if (!fileLoader_inited)
		return;

	PERFINFO_AUTO_START_FUNC();

	// TODO: timing code so that we only spend X ms per frame doing these
	EnterCriticalSection(&fileLoader_CritSect);
	eaPushEArray(&fileLoader_results, &fileLoader_results_queue);
	eaSetSize(&fileLoader_results_queue, 0);
	//TODO: compactMemoryPool(MP_NAME(FileLoaderAction));
	LeaveCriticalSection(&fileLoader_CritSect);
	for (i=0; i<eaSize(&fileLoader_results); i++) {
		FileLoaderAction *action = fileLoader_results[i];
		switch (action->type) {
		xcase FILELOADER_LOAD:
			action->loadedCallbackFunc(action->filename, action->data, action->dataSize, action->userData);
		xcase FILELOADER_LOAD_FROM_HOGG:
		 case FILELOADER_LOAD_FROM_HOGG_UNCOMPRESSED:
			action->loadedHoggCallbackFunc( action->filename, action->uncompressed, action->uncompressedSize,
											action->compressed, action->compressedSize, action->checksum, action->userData );
		xdefault: // FILELOADER_EXEC
			assert(0); // Shouldn't get here
		}
		fileLoaderActionDestroy(action);
		
	}
	eaSetSize(&fileLoader_results, 0);

	PERFINFO_AUTO_STOP();
}

int fileLoaderLoadsPending(void)
{
	int ret=0;
	if (fileLoader_inited) {
		ret += eaSize(&fileLoader_results);
		EnterCriticalSection(&fileLoader_CritSect);
		ret += eaSize(&fileLoader_results_queue);
		LeaveCriticalSection(&fileLoader_CritSect);
		ret += fileLoader_num_actions_queued;
	}
	return ret;
}

int fileLoaderPatchesPending(void)
{
	return fileLoader_num_patching_actions;
}
