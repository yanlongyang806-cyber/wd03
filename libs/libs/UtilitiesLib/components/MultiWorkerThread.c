#include "MultiWorkerThread.h"
#include "ThreadManager.h"
#include "timing.h"
#include "timing_profiler_interface.h"

#if _XBOX
#include "xmcore.h"
#pragma comment(lib, "xmcore.lib")
#else

//uses xmcore for 32bit, implements threadsafequeue for 64bit
#include "ThreadSafeQueue.h"

#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

typedef enum
{
	THREAD_NOTSTARTED=0,
	THREAD_RUNNING,
	THREAD_TOLDTOEXIT,
	THREAD_EXITED,
} ThreadState;

typedef struct MultiWorkerThread
{
	ThreadDataProcessFunc pProcessInputFunc;
	ThreadDataProcessFunc pProcessOutputFunc;
	ManagedThread*	pManagedThread;
	ThreadState		eThreadState;

	HANDLE			hDataQueued;
	HANDLE			hDataProcessed;

	XLOCKFREE_HANDLE hInputQueue;

	int				iProcessorIndex;	
	int				iPriority;

	volatile int	bThreadAsleep; // thread sleeps when it has no data queued
} MultiWorkerThread;

typedef struct MultiWorkerThreadManager
{
	XLOCKFREE_HANDLE	hInputQueue;
	char buf[1024];
	XLOCKFREE_HANDLE	hOutputQueue;
	MultiWorkerThread*	pThreads;
	int					iNumThreads;
	ThreadDataProcessFunc pProcessInputFunc;
	ThreadDataProcessFunc pProcessOutputFunc;
} MultiWorkerThreadManager;

bool mwtProcessQueueDirect(MultiWorkerThreadManager* pManager)
{
	void* pDataToProcess;
	HRESULT result;

	result = XLFQueueRemove(pManager->hInputQueue, &pDataToProcess);

	// If we can't find anything on the queue, shut down and signal that we are done
	if (result == XLOCKFREE_STRUCTURE_EMPTY)
		return false;

	pManager->pProcessInputFunc(pDataToProcess);

	return true;
}

bool mwtInputQueueIsEmpty(MultiWorkerThreadManager *pManager)
{
	return XLFQueueIsEmpty(pManager->hInputQueue);
}

static DWORD WINAPI mwtThreadFunction( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
	MultiWorkerThread* pMWT = (MultiWorkerThread *)lpParam;
	U32 uiNumLoops = 0;

	while (!pMWT->pManagedThread) // Let assignment finish in main thread
		SleepEx(1, TRUE);

	assert(pMWT->pManagedThread);
	pMWT->eThreadState = THREAD_RUNNING;
	pMWT->bThreadAsleep = true;

	if (pMWT->iProcessorIndex >= 0)
		tmSetThreadProcessorIdx(pMWT->pManagedThread, pMWT->iProcessorIndex);
    WaitForEvent(pMWT->hDataQueued, INFINITE);
	for(;;)
	{
		void* pDataToProcess;
		HRESULT result;

		if (pMWT->eThreadState == THREAD_TOLDTOEXIT)
		{
			pMWT->eThreadState = THREAD_EXITED;
#if _PS3
			DestroyEvent(pMWT->hDataQueued);
			DestroyEvent(pMWT->hDataProcessed);
#else
			CloseHandle(pMWT->hDataQueued);
			CloseHandle(pMWT->hDataProcessed);
#endif
			tmDestroyThread(pMWT->pManagedThread, false);
			ExitThread(0);
		}

		result = XLFQueueRemove(pMWT->hInputQueue, &pDataToProcess);

		// If we can't find anything on the queue, shut down and signal that we are done
		if (result == XLOCKFREE_STRUCTURE_EMPTY)
		{
			pMWT->bThreadAsleep = 1;
			SetEvent(pMWT->hDataProcessed);
			
			autoTimerThreadFrameEnd();
			autoTimerThreadFrameBegin(tmGetThreadName(pMWT->pManagedThread));

			// Now wait for the data queued signal to fire again
            WaitForEvent(pMWT->hDataQueued, INFINITE);

			// We're awake again!
			pMWT->bThreadAsleep = 0;
			++uiNumLoops;
			continue;
		}

		// Process the actual data
		pMWT->pProcessInputFunc(pDataToProcess);

		++uiNumLoops;
	}
	return 0; 
	EXCEPTION_HANDLER_END
}



static void mwtThreadStart(MultiWorkerThread* pMWT, XLOCKFREE_HANDLE hInputQueue, ThreadDataProcessFunc pProcessInputFunc, ThreadDataProcessFunc pProcessOutputFunc, const char* pcName, int iProcessorIdx, int iPriority)
{
	pMWT->hDataQueued = CreateEvent(0,0,0,0);
	assert(pMWT->hDataQueued);
	pMWT->hDataProcessed = CreateEvent(0,0,1,0);
	assert(pMWT->hDataProcessed);
	pMWT->hInputQueue = hInputQueue;
	pMWT->pProcessInputFunc = pProcessInputFunc;
	pMWT->pProcessOutputFunc = pProcessOutputFunc;
	pMWT->iProcessorIndex = iProcessorIdx;
	pMWT->iPriority = iPriority;
	pMWT->pManagedThread = tmCreateThreadDebug(mwtThreadFunction, pMWT, 0, iProcessorIdx, pcName?pcName:"mwtThread", __FILE__, __LINE__);
	assert(pMWT->pManagedThread);

	tmSetThreadRelativePriority(pMWT->pManagedThread, pMWT->iPriority);

	while (pMWT->eThreadState != THREAD_RUNNING)
		Sleep(1);
}



PVOID CALLBACK mwtAlloc(PVOID context, DWORD dwSize)
{
#if _PS3
    return malloc(dwSize);
#else
	return _aligned_malloc(dwSize, 4);
#endif
}

void CALLBACK mwtFree(PVOID context, PVOID pAddress)
{
#if _PS3
    free(pAddress);
#else
	_aligned_free(pAddress);
#endif
}


// If iMaxCmds is 0 or -1, there is no limit on the number of commands allocated
MultiWorkerThreadManager* mwtCreate(int iMaxInputCmds, int iMaxOutputCmds, int iNumThreads, int* pProcessorIndices, int* pThreadPriorities, ThreadDataProcessFunc pProcessInputFunc, ThreadDataProcessFunc pProcessOutputFunc, const char* pcThreadName)
{
	MultiWorkerThreadManager* pManager = (MultiWorkerThreadManager*)calloc(sizeof(*pManager), 1);
	static XLOCKFREE_CREATE XLFCreateInfo = { 0 };

	XLFCreateInfo.allocate = (XLockFreeMemoryAllocate)mwtAlloc;
	XLFCreateInfo.free = (XLockFreeMemoryFree)mwtFree;

	assert(pProcessInputFunc);


	XLFCreateInfo.maximumLength = iMaxInputCmds;
	XLFCreateInfo.structureSize = sizeof(XLFCreateInfo);
	XLFQueueCreate(&XLFCreateInfo, &pManager->hInputQueue);
	if (pProcessOutputFunc)
	{
		XLFCreateInfo.maximumLength = iMaxOutputCmds;
		XLFQueueCreate(&XLFCreateInfo, &pManager->hOutputQueue);
	}

	pManager->iNumThreads = iNumThreads;
	pManager->pProcessInputFunc = pProcessInputFunc;
	pManager->pProcessOutputFunc = pProcessOutputFunc;
	{
		int i;
		pManager->pThreads = (MultiWorkerThread*)calloc(sizeof(MultiWorkerThread), iNumThreads);
		for (i=0; i<iNumThreads; ++i)
		{
			const char* pcName = STACK_SPRINTF("%s:%d", pcThreadName, i);
			mwtThreadStart(&pManager->pThreads[i], pManager->hInputQueue, pProcessInputFunc, pProcessOutputFunc, pcName, pProcessorIndices?pProcessorIndices[i]:-1, pThreadPriorities?pThreadPriorities[i]:THREAD_PRIORITY_NORMAL);
		}
	}


	return pManager;
}

void mwtDestroy(MultiWorkerThreadManager* pManager)
{
	int i;
	for (i=pManager->iNumThreads-1; i>=0; --i)
	{
		MultiWorkerThread* pMWT = &pManager->pThreads[i];
		pMWT->eThreadState = THREAD_TOLDTOEXIT;
		SetEvent(pMWT->hDataQueued);
	}
	// this should only be called during debugging the threads, so i'm not too worried about the leaks
	// note: this leaks the event handles for each thread
	// note: this leaks the thread manager (and therefore thread handles) for each thread
	// free(pManager->pThreads);
	XLFQueueDestroy(pManager->hInputQueue);
	XLFQueueDestroy(pManager->hOutputQueue);
	ZeroStruct(pManager);
	free(pManager);
}

// I thought this might be cool, but didn't give any performance improvement over just not waking the threads at all until the queue was full [RMARR - 5/29/12]
#if 0
void mwtSmartWakeThreads(MultiWorkerThreadManager* pManager)
{
	// any time the number of objects in the queue exceeds the number of threads that are awake, wake another thread
	int iNumInQueue;
	int iAwakeThreads = 0;
	int i;
	PERFINFO_AUTO_START_FUNC();
	for (i=0;i<pManager->iNumThreads;i++)
	{
		MultiWorkerThread* pMWT = &pManager->pThreads[i];
		if (!pMWT->bThreadAsleep)
			iAwakeThreads++;
	}

	XLFQueueGetEntryCount(pManager->hInputQueue,&iNumInQueue);

	i=0;
	while (iNumInQueue > iAwakeThreads && i < pManager->iNumThreads)
	{
		MultiWorkerThread* pMWT = &pManager->pThreads[i];
		if (pMWT->bThreadAsleep)
		{
			ResetEvent(pMWT->hDataProcessed);
			SetEvent(pMWT->hDataQueued);
			iAwakeThreads++;
		}
		i++;
	}
	PERFINFO_AUTO_STOP();
}
#endif

void mwtWakeThreads(MultiWorkerThreadManager* pManager)
{
	int i;
	PERFINFO_AUTO_START_FUNC();
	for (i=pManager->iNumThreads-1; i>=0; --i)
	{
		MultiWorkerThread* pMWT = &pManager->pThreads[i];
		ResetEvent(pMWT->hDataProcessed);
		SetEvent(pMWT->hDataQueued);
	}
	PERFINFO_AUTO_STOP();
}


bool mwtQueueInput(MultiWorkerThreadManager* pManager, void* pData, bool bWakeThreads)
{
	HRESULT res = XLFQueueAdd(pManager->hInputQueue, pData);
	while (res == XLOCKFREE_STRUCTURE_FULL)
	{
		PERFINFO_AUTO_START("queue full", 1);
		Sleep(1);
		// It's full, just block until it isn't
		res = XLFQueueAdd(pManager->hInputQueue, pData);
		mwtWakeThreads(pManager); // Must wake threads, or we might deadlock
		PERFINFO_AUTO_STOP();
	}
	if (bWakeThreads)
		mwtWakeThreads(pManager);
	return res == S_OK;
}

bool mwtQueueOutput(MultiWorkerThreadManager* pManager, void* pData)
{
	return (XLFQueueAdd(pManager->hOutputQueue, pData) == S_OK);
}

S64 mwtProcessOutputQueueEx(MultiWorkerThreadManager* pManager, S64 count)
{
	void* pDataToProcess;
	S64 removedCount = 0;
	assert(pManager->pProcessOutputFunc);

	PERFINFO_AUTO_START_FUNC();

	if (!count)
		count = -1;

	while (XLFQueueRemove(pManager->hOutputQueue, &pDataToProcess) != XLOCKFREE_STRUCTURE_EMPTY)
	{
		pManager->pProcessOutputFunc(pDataToProcess);
		removedCount++;
		count--;
		if (!count)
			break;
	}

	PERFINFO_AUTO_STOP();
	return removedCount;
}

void mwtSleepUntilDone(MultiWorkerThreadManager* pManager)
{
	int i;
	// Only valid to call this when the input queue is empty
	// If this assert fires, run something like this first: while (mwtProcessQueueDirect(pManager));
	assert(mwtInputQueueIsEmpty(pManager));
	// Need to reset hDataProcessed - if the queue was added to after the last reset, a thread could
	// set the event and yet still be working on something
	mwtWakeThreads(pManager);
	
	for (i=0; i<pManager->iNumThreads; ++i)
	{
		MultiWorkerThread* pMWT = &pManager->pThreads[i];
        WaitForEvent(pMWT->hDataProcessed, INFINITE);
	}
	/*
	for (i=0; i<pManager->iNumThreads; ++i)
	{
		MultiWorkerThread* pMWT = &pManager->pThreads[i];
		assert(pMWT->bThreadAsleep);
	}
	*/
}
