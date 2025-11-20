/***************************************************************************



***************************************************************************/

#include "FreeThread.h"
#include "MultiWorkerThread.h"
#include "ThreadSafeMemoryPool.h"
#include "timing_profiler.h"

static bool gbUseFreeThread = true;
AUTO_CMD_INT(gbUseFreeThread, UseFreeThread);

static int giFreeThreadInputQueueSize = 16384;
AUTO_CMD_INT(giFreeThreadInputQueueSize, FreeThreadInputQueueSize);

static int giFreeThreadNumThreads = 1;
AUTO_CMD_INT(giFreeThreadNumThreads, FreeThreadNumThreads);

void EnableFreeThread(bool enable)
{
	gbUseFreeThread = enable;
}

void FreeThreadSetNumThreads(int num)
{
	giFreeThreadNumThreads = num;
}

void FreeThreadSetInputQueueSize(int size)
{
	giFreeThreadInputQueueSize = size;
}

static MultiWorkerThreadManager *sFreeThread;

typedef struct FreeThreadData
{
	void *userData;
	FreeThreadFunction func;
	const char *funcName;
	PerfInfoStaticData **staticPtr;
} FreeThreadData;

TSMP_DEFINE(FreeThreadData);

static void FreeThreadAction(void *userData, FreeThreadFunction func, const char *funcName, PerfInfoStaticData **staticPtr)
{
	if(func)
	{
		PERFINFO_AUTO_START_STATIC(funcName, staticPtr, 1);
		func(userData);
		PERFINFO_AUTO_STOP();
	}
	else
	{
		PERFINFO_AUTO_START("Free", 1);
		free(userData);
		PERFINFO_AUTO_STOP();
	}
}

void mwtInput_FreeThread(FreeThreadData *data)
{
	PERFINFO_AUTO_START_FUNC();
	FreeThreadAction(data->userData, data->func, data->funcName, data->staticPtr);
	TSMP_FREE(FreeThreadData, data);
	PERFINFO_AUTO_STOP();
}

static void InitializeFreeThread(int size, int numThreads)
{
	sFreeThread = mwtCreate(MAX(size, 2), 0, numThreads, NULL, NULL, mwtInput_FreeThread, NULL, "FreeThread");
	TSMP_SMART_CREATE(FreeThreadData, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
}

bool FreeThreadQueueEx(void *userData, FreeThreadFunction func, const char *funcName, PerfInfoStaticData **staticPtr)
{
	bool retVal;
	FreeThreadData *data;

	if(!userData)
		return false;

	if(!gbUseFreeThread)
	{
		FreeThreadAction(userData, func, funcName, staticPtr);
		return true;
	}

	ATOMIC_INIT_BEGIN;
		InitializeFreeThread(giFreeThreadInputQueueSize, giFreeThreadNumThreads);
	ATOMIC_INIT_END;

	data = TSMP_ALLOC(FreeThreadData);

	data->userData = userData;
	data->func = func;
	data->funcName = funcName;
	data->staticPtr = staticPtr;

	retVal = mwtQueueInput(sFreeThread, data, true);

	return retVal;
}