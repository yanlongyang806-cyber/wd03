#include "objContainer.h"
#include "GenericWorkerThread.h"
#include "ThreadManager.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "WorkerThread.h"

#include "wininclude.h"
#include "earray.h"
#include "ThreadManager.h"
#include "ScratchStack.h"
#include "EventTimingLog.h"
#include "ThreadSafeMemoryPool.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "GWTCmdQueue.h"

// PantsTodo: Add support for streaming in data to support Alex's desired AUTO feature

#if _PS3
#elif _XBOX
    #include <ppcintrinsics.h>
    #define memcpy memcpy_fast
#else
	// PC does not need a memory barrier because it doesn't allow out-of-order writes.
	#ifdef MemoryBarrier
		#undef MemoryBarrier
	#endif
	#define MemoryBarrier()
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define SAFETY_CHECKS_ON 0

#define OVERRUN_CHECKS_ON 1

#define WT_OVERRUN_VALUE 0x4BADBEEF

typedef enum
{
	THREAD_NOTSTARTED=0,
	THREAD_RUNNING,
	THREAD_TOLDTOEXIT,
	THREAD_EXITED,
} ThreadState;

enum {
	WAIT_LOCK_WORKER_THREAD_IS_WAITING = BIT(0),
	WAIT_LOCK_SENDING_THREAD_ADDED_CMD = BIT(1),
};

typedef struct GenericWorkerThreadInternal
{
	// thread info
	U32 threadIndex;
	ManagedThread *threadPtr;
	volatile ThreadState threadState;
	int processorIndex;
	GenericWorkerThreadManager *manager;
	GWTMsgQueue msgQueue;
	HANDLE			dataQueued; // event signaling that data is queued, wakes up sleeping thread
	HANDLE			dataProcessed; // event signaling that data is finished processing, wakes up sleeping main thread
	U32				waitLock;
	
} GenericWorkerThreadInternal;

typedef struct GenericWorkerThreadManager
{
	U32 runThreaded : 1;
	U32 flushRequested : 1;
	U32 debug : 1;
	U32 skipIfFull : 1;
	U32 noAutoTimer : 1; // disables the auto timer frame start and stop on threaded WorkerThreads
	LockStyle cmdLockstyle;

	U32 numThreads;

	U32 maxMsgs;
	GWTCmdQueue cmdQueue;
	GWTCmdQueue msgQueue; //Used only if threading is off.

	// for wtQueueAllocCmd/wtQueueSendCmd
	int currActiveCmdtype;

	GenericWorkerThreadInternal **internalThreadArray;

	// dispatch functions and user data
	void *userData;
	GWTDispatchCallback *cmdDispatchTable;
	GWTDispatchCallback *msgDispatchTable;
	GWTDefaultDispatchCallback cmdDefaultDispatch;
	GWTDefaultDispatchCallback msgDefaultDispatch;
	GWTPostCmdCallback postCmdCallback;

	// for queueing commands (which can be queued from multiple source threads)
	CRITICAL_SECTION cmdQueueCriticalsection;
	CRITICAL_SECTION cmdReadCriticalsection;
	// for queueing messages (which can be queued from in and out of the thread)
	// queuing messages should only happen in a particular thread.
	//CRITICAL_SECTION msgCriticalsection;

	int relativePriority;

	EventOwner *eventOwner;
	const char *name;
	U32 processCounter;

	U64 latencySumLastSecond;
	S64 latencyCountLastSecond;
	S64 latencyRunningSum;
	S64 latencyRunningCount;
} GenericWorkerThreadManager;

static bool gwtObjectLockCallback(GWTQueuedCmd *cmd);
static void gwtProcessObjectLockInstances(GWTQueuedCmd *cmd);
static void gwtDispatchMessages(GenericWorkerThreadManager *wt);

void gwtWakeThreads(GenericWorkerThreadManager* manager)
{
	int i;
	PERFINFO_AUTO_START_FUNC();
	for (i=(int)manager->numThreads-1; i>=0; --i)
	{
		GenericWorkerThreadInternal* internal = manager->internalThreadArray[i];
		ResetEvent(internal->dataProcessed);
		if(	internal->dataQueued
			&&
			_InterlockedOr(&internal->waitLock, WAIT_LOCK_SENDING_THREAD_ADDED_CMD) ==
			WAIT_LOCK_WORKER_THREAD_IS_WAITING)
		{
			// BG thread set it, and FG never set it, so BG will be waiting for data_queued.
			SetEvent(internal->dataQueued);
		}
	}
	PERFINFO_AUTO_STOP();
}

bool gwtInWorkerThread(GenericWorkerThreadManager *wt)
{
	if (wt && eaSize(&wt->internalThreadArray))
	{
		DWORD threadid = GetCurrentThreadId();
		EARRAY_FOREACH_BEGIN(wt->internalThreadArray, i);
			if(tmGetThreadId(wt->internalThreadArray[i]->threadPtr) == threadid)
				return true;
		EARRAY_FOREACH_END;
	}
	return false;
}

void gwtAssertWorkerThread(GenericWorkerThreadManager *wt)
{
	bool foundThread = false;
	if (wt && eaSize(&wt->internalThreadArray))
	{
		DWORD threadid = GetCurrentThreadId();
		EARRAY_FOREACH_BEGIN(wt->internalThreadArray, i);
			if(tmGetThreadId(wt->internalThreadArray[i]->threadPtr) == threadid)
				foundThread = true;
		EARRAY_FOREACH_END;
		assert(foundThread);
	}
}

void gwtAssertNotWorkerThread(GenericWorkerThreadManager *wt)
{
	bool foundThread = false;
	if (wt && eaSize(&wt->internalThreadArray))
	{
		DWORD threadid = GetCurrentThreadId();
		EARRAY_FOREACH_BEGIN(wt->internalThreadArray, i);
			if(tmGetThreadId(wt->internalThreadArray[i]->threadPtr) == threadid)
				foundThread = true;
		EARRAY_FOREACH_END;
		assert(!foundThread);
	}
}

void gwtSetFlushRequested(GenericWorkerThreadManager *wt, int flush_requested)
{
	wt->flushRequested = !!flush_requested;
}

void gwtSetEventOwner(GenericWorkerThreadManager *wt, EventOwner *event_owner)
{
	wt->eventOwner = event_owner;
}


void gwtSetSkipIfFull(GenericWorkerThreadManager *wt, int skip_if_full)
{
	wt->skipIfFull = skip_if_full;
}

__forceinline static void gwtReqFlush(GenericWorkerThreadManager *wt, bool sleep)
{
	U32 i;
	wt->flushRequested = 1;
	for(i = 0; i < wt->numThreads; ++i)
	{
		GenericWorkerThreadInternal *thread = wt->internalThreadArray[i];
		if (thread->dataProcessed)
			ResetEvent(thread->dataProcessed);
		if (thread->dataQueued)
			SetEvent(thread->dataQueued);
		if (sleep)
		{
			if (thread->dataProcessed)
			{
				WaitForEvent(thread->dataProcessed, 1);
			}
			else
				Sleep(1);
		}
	}
	wt->flushRequested = 0;
}

__forceinline static void gwtHandleQueuedCmd(GenericWorkerThreadManager* wt, GWTQueuedCmd *cmd, GWTDispatchCallback *dispatch_table, GWTDefaultDispatchCallback default_dispatch, void *user_data,
	GWTCmdPacket * packet)
{
	handleQueuedGWTCmd(cmd, dispatch_table, default_dispatch, user_data, packet);

	if (wt->postCmdCallback)
	{
		wt->postCmdCallback(user_data);
	}
}

static GWTQueuedCmd *getNextMessage(GenericWorkerThreadManager *wt, int *threadIndex)
{
	int i;
	GWTQueuedCmd *result = NULL;
	// maximum number of queued messages per thread times the number of threads.
	// The extra factor of 2 is to limit edge cases.
	U32 processOrderLimit = 2 * wt->numThreads * wt->msgQueue.maxQueued;
	assert(threadIndex);
	
	PERFINFO_AUTO_START_FUNC_L2();

	*threadIndex = -1;
	result = lockStoredGWTCmd(0, &wt->msgQueue);

	for(i = 0; i < (int)wt->numThreads; ++i)
	{
		GWTQueuedCmd *test = lockStoredGWTCmd(0, &wt->internalThreadArray[i]->msgQueue);
		if(!test)
			continue;

		if(!result)
		{
			*threadIndex = i;
			result = test;
			continue;
		}

		if(result->processOrder - test->processOrder < processOrderLimit)
		{
			if(*threadIndex == -1)
				unlockStoredGWTCmd(&wt->msgQueue, result);
			else
				unlockStoredGWTCmd(&wt->internalThreadArray[*threadIndex]->msgQueue, result);
			*threadIndex = i;
			result = test;
			continue;
		}

		unlockStoredGWTCmd(&wt->internalThreadArray[i]->msgQueue, test);
	}
	PERFINFO_AUTO_STOP_L2();
	return result;
}

static void gwtDispatchMessages(GenericWorkerThreadManager *wt)
{
	GWTQueuedCmd *cmd;
	GWTCmdPacket packet = { 0 };
	int threadIndex;

	PERFINFO_AUTO_START_FUNC();
#if SAFETY_CHECKS_ON
	gwtAssertNotWorkerThread(wt);
#endif

	packet.manager = wt;
	while (cmd = getNextMessage(wt, &threadIndex))
	{
		ADD_MISC_COUNT(1, "Processing Message");
		gwtHandleQueuedCmd(wt, cmd, wt->msgDispatchTable, wt->msgDefaultDispatch, wt->userData, &packet);
		removeStoredGWTCmd(&wt->internalThreadArray[threadIndex]->msgQueue, cmd);
	}
	PERFINFO_AUTO_STOP();
}

static bool gwtHasThreadPointers(GenericWorkerThreadManager *wt)
{
	if(!eaSize(&wt->internalThreadArray))
		return false;

	EARRAY_FOREACH_BEGIN(wt->internalThreadArray, i);
	{
		if(wt->internalThreadArray[i]->threadPtr)
			return true;
	}
	EARRAY_FOREACH_END;
	return false;
}

void gwtFlushMessages(GenericWorkerThreadManager *wt)
{
	bool firstPass = true;
	while (wt->runThreaded && gwtHasThreadPointers(wt))
	{
		bool empty = true;
		U32 i;
		if(!firstPass)
			Sleep(1);

		for(i = 0; i < wt->numThreads; ++i)
		{
			empty &= isGWTCmdQueueEmpty(&wt->internalThreadArray[i]->msgQueue);
		}
		if(empty)
			break;
		gwtWakeThreads(wt);
		gwtMonitor(wt);
	}
}

void gwtFlushEx(GenericWorkerThreadManager *wt, bool dispatch_messages)
{
#if SAFETY_CHECKS_ON
	gwtAssertNotWorkerThread(wt);
#endif
	while (wt->runThreaded && gwtHasThreadPointers(wt))
	{
		if (isGWTCmdQueueEmpty(&wt->cmdQueue))
			break;
		gwtReqFlush(wt, true);
		if (dispatch_messages)
			gwtDispatchMessages(wt);
	}
}

static int gLastUpdateTime = 0;

void gwtUpdateLatencyAverager(GenericWorkerThreadManager *wt)
{
#if _WIN64
	int curTime = timeSecondsSince2000();
	if(gLastUpdateTime < curTime)
	{
		U32 latencyCount = InterlockedExchange64(&wt->latencyCountLastSecond, 0);
		U64 latencySum = InterlockedExchange64(&wt->latencySumLastSecond, 0);
		U64 scaleFactor = (60 - (curTime - gLastUpdateTime));
		if(scaleFactor <= 0)
			scaleFactor = 1;

		wt->latencyRunningCount = (wt->latencyRunningCount * scaleFactor) / 60 + latencyCount;
		wt->latencyRunningSum = (wt->latencyRunningSum * scaleFactor) / 60 + latencySum;
		gLastUpdateTime = curTime;
	}
#endif
}

void gwtMonitor(GenericWorkerThreadManager *wt)
{
#if SAFETY_CHECKS_ON
	gwtAssertNotWorkerThread(wt);
#endif
	gwtUpdateLatencyAverager(wt);
	gwtDispatchMessages(wt);
}

void gwtMonitorAndSleep(GenericWorkerThreadManager *wt)
{
#if SAFETY_CHECKS_ON
	gwtAssertNotWorkerThread(wt);
#endif
	gwtUpdateLatencyAverager(wt);
	gwtReqFlush(wt, true);
	gwtDispatchMessages(wt);
}


GenericWorkerThreadManager *gwtCreate_dbg(int maxCmds, int maxMsgs, int numThreads, void *userData, const char *name, LockStyle cmdLockstyle MEM_DBG_PARMS)
{
	GenericWorkerThreadManager *wt = scalloc(1, sizeof(*wt));
	assert(!(maxCmds & (maxCmds - 1)) && maxCmds);
	assert(!(maxMsgs & (maxMsgs - 1)) && maxMsgs);
	wt->userData = userData;
	wt->name = name;
	wt->numThreads = numThreads;
	wt->cmdLockstyle = cmdLockstyle;
	wt->maxMsgs = maxMsgs;
	initGWTCmdQueue(&wt->cmdQueue, maxCmds, numThreads, &wt->processCounter MEM_DBG_PARMS_CALL);
	initGWTCmdQueue(&wt->msgQueue, maxMsgs, numThreads, &wt->processCounter MEM_DBG_PARMS_CALL);
	InitializeCriticalSection(&wt->cmdQueueCriticalsection);
	InitializeCriticalSection(&wt->cmdReadCriticalsection);
	return wt;
}

void gwtSetProcessor(GenericWorkerThreadInternal *internal, int processorIndex)
{
	internal->processorIndex = processorIndex;
	if (internal->threadPtr && processorIndex >= 0)
		tmSetThreadProcessorIdx(internal->threadPtr, processorIndex);
}

void gwtDestroyInternal(GenericWorkerThreadManager *wt, GenericWorkerThreadInternal *internal)
{
	tmDestroyThread(internal->threadPtr, false);
	free(internal->msgQueue.queue);
#if _PS3
	DestroyEvent(internal->dataQueued);
	DestroyEvent(internal->dataProcessed);
#else
	CloseHandle(internal->dataQueued);
	CloseHandle(internal->dataProcessed);
#endif
}

void gwtDestroyEx(GenericWorkerThreadManager **wt)
{
	gwtFlush(*wt);

	// Tell all threads to exit.
	EARRAY_FOREACH_BEGIN((*wt)->internalThreadArray, i);
	{
		GenericWorkerThreadInternal *internal = (*wt)->internalThreadArray[i];
		internal->threadState = THREAD_TOLDTOEXIT;
	}
	EARRAY_FOREACH_END;

	// Wait until they have all exited.
	EARRAY_FOREACH_BEGIN((*wt)->internalThreadArray, i);
	{
		GenericWorkerThreadInternal *internal = (*wt)->internalThreadArray[i];
		if(internal->threadState != THREAD_EXITED)
		{
			while (internal->threadState != THREAD_EXITED)
				gwtReqFlush((*wt), true);
		}
	}
	EARRAY_FOREACH_END;

	// Now we can clean them up.
	EARRAY_FOREACH_BEGIN((*wt)->internalThreadArray, i);
	{
		GenericWorkerThreadInternal *internal = (*wt)->internalThreadArray[i];
		gwtDestroyInternal((*wt), (*wt)->internalThreadArray[i]);
	}
	EARRAY_FOREACH_END;

	eaDestroyEx(&(*wt)->internalThreadArray, NULL);

	DeleteCriticalSection(&(*wt)->cmdQueueCriticalsection);
	DeleteCriticalSection(&(*wt)->cmdReadCriticalsection);
	free((*wt)->cmdQueue.queue);
	ZeroStruct(*wt);
	free(*wt);
	*wt = NULL;
}

void gwtRegisterCmdDispatch(GenericWorkerThreadManager *wt, int cmd_type, GWTDispatchCallback dispatch_callback)
{
	assert(cmd_type >= WT_CMD_USER_START);
	if (cmd_type >= eaSize((void ***)&wt->cmdDispatchTable))
		eaSetSize((void ***)&wt->cmdDispatchTable, cmd_type+1);
	wt->cmdDispatchTable[cmd_type] = dispatch_callback;
}

void gwtRegisterMsgDispatch(GenericWorkerThreadManager *wt, int msg_type, GWTDispatchCallback dispatch_callback)
{
	assert(msg_type >= WT_CMD_USER_START);
	if (msg_type >= eaSize((void ***)&wt->msgDispatchTable))
		eaSetSize((void ***)&wt->msgDispatchTable, msg_type+1);
	wt->msgDispatchTable[msg_type] = dispatch_callback;
}

void gwtSetDefaultCmdDispatch(GenericWorkerThreadManager *wt, GWTDefaultDispatchCallback dispatch_callback)
{
	wt->cmdDefaultDispatch = dispatch_callback;
}

void gwtSetDefaultMsgDispatch(GenericWorkerThreadManager *wt, GWTDefaultDispatchCallback dispatch_callback)
{
	wt->msgDefaultDispatch = dispatch_callback;
}

void gwtSetThreaded(GenericWorkerThreadManager *wt, bool runThreaded, int relativePriority, bool noAutoTimer)
{
	if (gwtHasThreadPointers(wt)) {
		assertmsg(0, "Trying to change threadedness of a WorkerThread after creation, probably won't work"); // Doesn't work for RenderThread anyway, because of thread window ownership
	}

	wt->runThreaded = !!runThreaded;
	wt->noAutoTimer = !!noAutoTimer;

	wt->relativePriority = relativePriority;
}

int gwtIsThreaded(GenericWorkerThreadManager *wt)
{
	return wt->runThreaded;
}

const char *gwtGetName(GenericWorkerThreadManager *wt)
{
	return wt->name;
}

HANDLE gwtGetThreadHandle(GenericWorkerThreadManager *wt, int i)
{
	if(i < 0 || i >= eaSize(&wt->internalThreadArray))
		return NULL;

	if (!wt->runThreaded || !(wt->internalThreadArray[i] && wt->internalThreadArray[i]->threadPtr))
		return NULL;
	return tmGetThreadHandle(wt->internalThreadArray[i]->threadPtr);
}

U32 gwtGetThreadID(GenericWorkerThreadManager *wt, int i)
{
	if(i < 0 || i >= eaSize(&wt->internalThreadArray))
		return 0;

	if (!wt->runThreaded || !(wt->internalThreadArray[i] && wt->internalThreadArray[i]->threadPtr))
		return 0;
	return tmGetThreadId(wt->internalThreadArray[i]->threadPtr);
}

void gwtSetDebug(GenericWorkerThreadManager *wt, bool debug)
{
	wt->debug = !!debug;
}

bool gwtIsDebug(GenericWorkerThreadManager *wt)
{
	return wt->debug;
}

void gwtSetPostCmdCallback(GenericWorkerThreadManager *wt, WTPostCmdCallback callback)
{
	wt->postCmdCallback = callback;
}

static void gwtAddLatencyCount(GenericWorkerThreadManager *manager, GWTQueuedCmd *cmd)
{
#if _WIN64
	S64 endingTicks;
	endingTicks = timerCpuTicks64();

	InterlockedIncrement64(&manager->latencyCountLastSecond);
	InterlockedExchangeAdd64(&manager->latencySumLastSecond, endingTicks - cmd->queueTimeTicks);
#endif
}

static DWORD WINAPI gwtThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN

	GenericWorkerThreadInternal *internal;
	GWTQueuedCmd *cmd,last;
	GWTCmdPacket packet = { 0 };
	
	internal = (GenericWorkerThreadInternal *)lpParam;
	while (!internal->threadPtr) // Let assignment finish in main thread
		SleepEx(1, TRUE);
	assert(internal->threadPtr);
	if (internal->processorIndex >= 0)
		tmSetThreadProcessorIdx(internal->threadPtr, internal->processorIndex);

	internal->dataQueued = CreateEvent(0,0,0,0);
	internal->dataProcessed = CreateEvent(0,0,0,0);

	packet.thread = internal;
	packet.manager = internal->manager;

	internal->threadState = THREAD_RUNNING;

	for(;;)
	{
		if (!internal->manager->noAutoTimer)
			autoTimerThreadFrameBegin(internal->manager->name);
		PERFINFO_AUTO_START("GenericWorkerThreadLoop", 1);
		
		if (internal->threadState == THREAD_TOLDTOEXIT)
		{
			internal->threadState = THREAD_EXITED;
			ExitThread(0);
		}

		if (internal->manager->debug && !internal->manager->flushRequested && internal->manager->runThreaded)
			cmd = 0;
		else
			cmd = lockStoredGWTCmdEx(internal->threadIndex, &internal->manager->cmdQueue, internal->manager->cmdLockstyle == GWT_LOCKSTYLE_OBJECTLOCK ? gwtObjectLockCallback : NULL);
		if (!cmd)
		{
			if (internal->dataQueued)
			{
				if(isGWTCmdQueueEmpty(&internal->manager->cmdQueue))
				{
					if(!_InterlockedOr(&internal->waitLock, WAIT_LOCK_WORKER_THREAD_IS_WAITING))
					{
						// Sending thread hasn't queued anything, so wait for it.
						if (internal->manager->flushRequested && internal->dataProcessed)
							SetEvent(internal->dataProcessed);
						if (internal->manager->eventOwner)
							etlAddEvent(internal->manager->eventOwner, "Wait for data", ELT_CODE, ELTT_BEGIN);
						WaitForSingleObjectEx(internal->dataQueued, INFINITE, TRUE, "-EmptyQueue");
						if (internal->manager->eventOwner)
							etlAddEvent(internal->manager->eventOwner, "Wait for data", ELT_CODE, ELTT_END);
					}
					InterlockedExchange(&internal->waitLock, 0);
				}
				else
				{
					// Even if the queue is not empty, sleep since there was nothing to do.
					WaitForSingleObjectEx(internal->dataQueued, 1, TRUE, "-FrameWait");
				}
			}
			PERFINFO_AUTO_STOP();
			continue;
		}

		PERFINFO_AUTO_START("gwtHandleQueuedCmd", 1);
		gwtHandleQueuedCmd(internal->manager, cmd, internal->manager->cmdDispatchTable, internal->manager->cmdDefaultDispatch, internal->manager->userData, &packet);
		PERFINFO_AUTO_STOP();
		
		gwtAddLatencyCount(internal->manager, cmd);

		last = *cmd;
		if(internal->manager->cmdLockstyle == GWT_LOCKSTYLE_OBJECTLOCK)
			gwtProcessObjectLockInstances(cmd);

		removeStoredGWTCmd(&internal->manager->cmdQueue, cmd);

		PERFINFO_AUTO_STOP();
		if (!internal->manager->noAutoTimer)
			autoTimerThreadFrameEnd();
	}
	return 0; 
	EXCEPTION_HANDLER_END
} 

void gwtStartInternal(GenericWorkerThreadManager *wt, GenericWorkerThreadInternal *internal, int stack_size)
{
	if (!wt->runThreaded)
		return;

	internal->threadPtr = tmCreateThreadDebug(gwtThread, internal, stack_size, 0, wt->name?wt->name:"wtThread", __FILE__, __LINE__);
	assert(internal->threadPtr);

	tmSetThreadRelativePriority(internal->threadPtr, wt->relativePriority);

	while (internal->threadState != THREAD_RUNNING)
		Sleep(1);
}

void gwtStartEx(GenericWorkerThreadManager *wt, int stack_size MEM_DBG_PARMS)
{
	U32 i;
	if (!wt->runThreaded)
		return;

	//don't recreate threads
	if(eaSize(&wt->internalThreadArray))
		return;

	for(i = 0; i < wt->numThreads; ++i)
	{
		GenericWorkerThreadInternal *internal;
		internal = callocStruct(GenericWorkerThreadInternal);
		internal->threadIndex = i;
		internal->manager = wt;
		eaPush(&wt->internalThreadArray, internal);
		initGWTCmdQueue(&internal->msgQueue, wt->maxMsgs, 1, &wt->processCounter MEM_DBG_PARMS_CALL);
		gwtStartInternal(wt, internal, stack_size);
	}
}

void gwtQueueDebugCmd(GenericWorkerThreadManager *wt,WTDebugCallback callback_func,void *data,int size)
{
	WTDebugCallback *cb_data = _alloca(size+sizeof(WTDebugCallback));
	*cb_data = callback_func;
	memcpy(cb_data+1,data,size);
	gwtQueueCmd(wt,WT_CMD_DEBUGCALLBACK,cb_data,size+sizeof(WTDebugCallback));
}

bool gwtFreeBlocksCallback(GenericWorkerThreadManager *wt)
{
	if (wt->skipIfFull)
		return 1;
		
	// wait for worker thread to free some blocks
	gwtReqFlush(wt, true);
	gwtDispatchMessages(wt);
	return 0;
}


GWTQueuedCmd* gwtAllocCmdPkt(GenericWorkerThreadManager *wt, int cmd_type, int size, int dataSize)
{
	GWTQueuedCmd* packet_header;
	void* ret;
	EnterCriticalSection(&wt->cmdQueueCriticalsection);
	assert(!wt->currActiveCmdtype);
	wt->currActiveCmdtype = cmd_type;
#if SAFETY_CHECKS_ON
	gwtAssertNotWorkerThread(wt);
#endif
#if OVERRUN_CHECKS_ON
	size+=4;
#endif
	do 
	{
		packet_header = allocStoredGWTCmdEx(&wt->cmdQueue, cmd_type, size, false, dataSize);
		if (!packet_header)
		{
			if (wt->skipIfFull)
			{
				wt->currActiveCmdtype = 0;
				LeaveCriticalSection(&wt->cmdQueueCriticalsection);
				return 0;
			}
			// wait for worker thread to free some blocks
			gwtReqFlush(wt, false);
			gwtDispatchMessages(wt);
		}
	} while (!packet_header);

	ret = gwtGetQueuedCmdData(packet_header);
	packet_header->queueTimeTicks = timerCpuTicks64();
#if OVERRUN_CHECKS_ON
	*(U32*)(((U8*)ret) + size - 4) = WT_OVERRUN_VALUE;
#endif
	LeaveCriticalSection(&wt->cmdQueueCriticalsection);
	return packet_header;
}

#define gwtAllocCmd(wt, cmdType, size) gwtAllocCmdEx(wt, cmdType, size, size)
void *gwtAllocCmdEx(GenericWorkerThreadManager *wt, int cmdType, int size, int dataSize)
{
	GWTQueuedCmd * packet_header = gwtAllocCmdPkt(wt, cmdType, size, dataSize);

	if (!packet_header)
		return 0;
	return gwtGetQueuedCmdData(packet_header);
}

// PantsTodo: This needs to have less access to the internals of cmd_queue
void gwtSendCmd(GenericWorkerThreadManager *wt)
{
	GWTCmdPacket packet = { 0 };
#if OVERRUN_CHECKS_ON
	U8 *data;
	GWTQueuedCmd *cmd = &wt->cmdQueue.queue[wt->cmdQueue.end];
	if (cmd->type == WT_CMD_NOP)
		cmd = &wt->cmdQueue.queue[0];
 	data = gwtGetQueuedCmdData(cmd);
	assertmsg(*(U32*)(data + cmd->size - 4) == WT_OVERRUN_VALUE, "This command overran the CmdQueue");
#endif
	packet.manager = wt;
	wt->currActiveCmdtype = 0;
	if (!wt->runThreaded)
	{
		PERFINFO_AUTO_START("gwtHandleQueuedCmd", 1);
		gwtHandleQueuedCmd(wt, &wt->cmdQueue.queue[0], wt->cmdDispatchTable, wt->cmdDefaultDispatch, 
			wt->userData, &packet);
		PERFINFO_AUTO_STOP();
		return;
	}

	// We must have the command data written before the command queue end changes
	MemoryBarrier();

	wt->cmdQueue.end = wt->cmdQueue.nextEnd;
	gwtWakeThreads(wt); //Must wake threads
#if SAFETY_CHECKS_ON
	gwtAssertNotWorkerThread(wt);
#endif
}

void gwtCancelCmd(GenericWorkerThreadManager *wt)
{
	GWTQueuedCmd	*cmd = &wt->cmdQueue.queue[wt->cmdQueue.end];

	wt->currActiveCmdtype = 0;
	if (cmd->data)
	{
		free(cmd->data);
		cmd->data = 0;
	}
}

int gwtQueueCmd(GenericWorkerThreadManager *wt,int cmd_type,const void *cmd_data,int size)
{
	void	*mem;
#if SAFETY_CHECKS_ON
	gwtAssertNotWorkerThread(wt);
#endif
	assertmsg(wt->cmdLockstyle != GWT_LOCKSTYLE_OBJECTLOCK, "For an object lock GenericWorkerThread, all command queues must use gwtQueueCmd_ObjectLock or variants");
	EnterCriticalSection(&wt->cmdQueueCriticalsection);
	mem = gwtAllocCmd(wt,cmd_type,size);
	if (!mem)
		return 0;
	if (size)
		memcpy(mem,cmd_data,size);
	gwtSendCmd(wt);
	LeaveCriticalSection(&wt->cmdQueueCriticalsection);
	return 1;
}

//packet->thread is null if called from the main thread, non-null otherwise
//PantsTodo: Does this need locking in case of queuing commands from multiple threads when internal threading is off?
// PantsTodo: This needs to have less access to the internals of cmd_queue
void gwtQueueMsg(GWTCmdPacket *packet,int msg_type,void *msg_data,int size)
{
	void *mem;
	GWTQueuedCmd * cmd = NULL;
	GWTCmdQueue *msgQueue;
	int allocsize = size;

	if (!packet)
		return;

	if(packet->thread)
		msgQueue = &packet->thread->msgQueue;
	else
		msgQueue = &packet->manager->msgQueue;

#if OVERRUN_CHECKS_ON
	allocsize+=4;
#endif
	while(!cmd)
	{
		cmd = allocStoredGWTCmd(msgQueue, msg_type, allocsize, false);
	}
	mem = gwtGetQueuedCmdData(cmd);
#if OVERRUN_CHECKS_ON
	*(U32*)(((U8*)mem) + allocsize - 4) = WT_OVERRUN_VALUE;
#endif
	memcpy(mem,msg_data,size);

	// We must have the command data written before the command queue end changes
	MemoryBarrier();

	msgQueue->end = msgQueue->nextEnd;
}

U32 gwtGetThreadIndex(GWTCmdPacket *packet)
{
	return packet->thread->threadIndex;
}

S64 gwtGetAverageCommandLatency(GenericWorkerThreadManager *wt)
{
#if _WIN64
	return wt->latencyRunningCount ? wt->latencyRunningSum / wt->latencyRunningCount : 0;
#else
	return 0;
#endif
}

S64 gwtGetLastMinuteCount(GenericWorkerThreadManager *wt)
{
#if _WIN64
	return wt->latencyRunningCount;
#else
	return 0;
#endif
}

//CMD_LOCKSTYLE_OBJECTLOCK
typedef struct ObjectLock
{
	U32 queued;
	U32 processed;
	U32 full_lock;
	U32 *lockCounter; // If non-null, this will be incremented on full locks and decremented when full locks are released
	union
	{	// This chunk of memory is for identifying information for the ObjectLock to improve debugging. If you want something other
		// than a ContainerRef here, add it to the union and add an accessor method to populate that data. 
		ContainerRef conRef;
	};
} ObjectLock;

// doesn't need a TSMP for ObjectLockInstance because we never allocate these; we just store directly in the GWT queue
typedef struct ObjectLockInstance
{
	ObjectLock *lock;
	U32 position;
} ObjectLockInstance;

TSMP_DEFINE(ObjectLock);

ObjectLock *initializeObjectLock(U32 *lockCounter)
{
	ObjectLock *lock;

	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(ObjectLock, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	lock = TSMP_CALLOC(ObjectLock);
	lock->lockCounter = lockCounter;
	return lock;
}

void SetObjectLockContainerRef(ObjectLock *lock, GlobalType containerType, ContainerID containerID)
{
	lock->conRef.containerType = containerType;
	lock->conRef.containerID = containerID;
}

//PantsTodo: Add an array of instances to clean them up somehow
void destroyObjectLock(ObjectLock **lock)
{
	assertmsg(0, "Do not destroy these as we have no way of cleaning up instances.");
	assert(lock && *lock);
	TSMP_FREE(ObjectLock, *lock);
}

void fullyLockObjectLock(ObjectLock *lock)
{
	while(InterlockedIncrement(&lock->full_lock) > 1)
	{
		InterlockedDecrement(&lock->full_lock);
		Sleep(0);
	}
	if(lock->lockCounter)
		InterlockedIncrement(lock->lockCounter);
}

void fullyUnlockObjectLock(ObjectLock *lock)
{
	if(lock->lockCounter)
		InterlockedDecrement(lock->lockCounter);
	InterlockedDecrement(&lock->full_lock);
}

void initializeObjectLockInstance(ObjectLockInstance *instance, ObjectLock *lock)
{
	assert(lock);
	instance->lock = lock;
	instance->position = InterlockedIncrement(&lock->queued);
}

bool isObjectLockInstanceReady(ObjectLockInstance *instance)
{
	return instance->position == (instance->lock->processed + 1);
}

void processObjectLockInstance(ObjectLockInstance *instance)
{
	U32 result;
	result = InterlockedIncrement(&instance->lock->processed);
	assert(result == instance->position);
}

void fullyLockObjectLockInstance(ObjectLockInstance *instance)
{
	fullyLockObjectLock(instance->lock);
}

void fullyUnlockObjectLockInstance(ObjectLockInstance *instance)
{
	fullyUnlockObjectLock(instance->lock);
}

int gwtQueueCmd_ObjectLockInternal(GenericWorkerThreadManager *wt,int cmdType,const void *cmdData,int size, int lockCount, va_list args)
{
	void	*mem;
	char	*iter;
	ObjectLock *lock;
	int		actualLockCount = 0;
	int totalSize = size + sizeof(lockCount) + sizeof(ObjectLockInstance)*lockCount;
#if SAFETY_CHECKS_ON
	gwtAssertNotWorkerThread(wt);
#endif
	PERFINFO_AUTO_START_FUNC_L2();
	EnterCriticalSection(&wt->cmdQueueCriticalsection);
	mem = gwtAllocCmdEx(wt,cmdType,totalSize, size);
	if (!mem)
	{
		LeaveCriticalSection(&wt->cmdQueueCriticalsection);
		PERFINFO_AUTO_STOP_L2();
		return 0;
	}
	if (size)
		memcpy(mem,cmdData,size);
	iter = (char*)mem + size;
	memcpy(iter, &lockCount, sizeof(int));
	iter += sizeof(int);

	for (actualLockCount = 0, lock = va_arg(args, ObjectLock *); actualLockCount < lockCount; lock = va_arg(args, ObjectLock *))
	{
		actualLockCount++;
		initializeObjectLockInstance((ObjectLockInstance*)iter, lock);
		iter += sizeof(ObjectLockInstance);
	}

	assert(actualLockCount == lockCount);
		
	gwtSendCmd(wt);
	LeaveCriticalSection(&wt->cmdQueueCriticalsection);
	PERFINFO_AUTO_STOP_L2();
	return 1;
}

int gwtQueueCmd_ObjectLock(GenericWorkerThreadManager *wt,int cmdType,const void *cmdData,int size, int lockCount, ...)
{
	int result;
	va_list args;

	assertmsg(wt->cmdLockstyle == GWT_LOCKSTYLE_OBJECTLOCK, "Do not use this unless the locktype is CMD_LOCKSTYLE_OBJECTLOCK.");
	va_start(args, lockCount);
	result = gwtQueueCmd_ObjectLockInternal(wt, cmdType, cmdData, size, lockCount, args);
	va_end(args);
	return result;
}

int gwtQueueCmd_ObjectLockArray(GenericWorkerThreadManager *wt,int cmdType,const void *cmdData,int size, ObjectLock **objectLocks)
{
	void	*mem;
	char	*iter;
	int		actualLockCount = 0;
	int		lockCount = eaSize(&objectLocks);
	int totalSize = size + sizeof(lockCount) + sizeof(ObjectLockInstance)*lockCount;
#if SAFETY_CHECKS_ON
	gwtAssertNotWorkerThread(wt);
#endif
	assertmsg(wt->cmdLockstyle == GWT_LOCKSTYLE_OBJECTLOCK, "Do not use this unless the locktype is CMD_LOCKSTYLE_OBJECTLOCK.");

	PERFINFO_AUTO_START_FUNC_L2();
	EnterCriticalSection(&wt->cmdQueueCriticalsection);
	mem = gwtAllocCmdEx(wt,cmdType,totalSize, size);
	if (!mem)
	{
		LeaveCriticalSection(&wt->cmdQueueCriticalsection);
		PERFINFO_AUTO_STOP_L2();
		return 0;
	}
	if (size)
		memcpy(mem,cmdData,size);
	iter = (char*)mem + size;
	memcpy(iter, &lockCount, sizeof(int));
	iter += sizeof(int);

	for (actualLockCount = 0; actualLockCount < lockCount; ++actualLockCount)
	{
		initializeObjectLockInstance((ObjectLockInstance*)iter, objectLocks[actualLockCount]);
		iter += sizeof(ObjectLockInstance);
	}

	assert(actualLockCount == lockCount);
		
	gwtSendCmd(wt);
	LeaveCriticalSection(&wt->cmdQueueCriticalsection);
	PERFINFO_AUTO_STOP_L2();
	return 1;
}

static bool gwtObjectLockCallback(GWTQueuedCmd *cmd)
{
	void *data;
	int lockCount;
	int i;
	PERFINFO_AUTO_START_FUNC_L2();
	if(cmd->type < GWT_CMD_USER_START)
	{
		PERFINFO_AUTO_STOP_L2();
		return true;
	}
	data = getDataFromGWTCmd(cmd);
	lockCount = *((int*)((char*)data + cmd->dataSize));
	for(i = 0; i < lockCount; ++i)
	{
		ObjectLockInstance *instance = (ObjectLockInstance*)((char*)data + cmd->dataSize + sizeof(int)) + i;
		if(!isObjectLockInstanceReady(instance))
		{
			PERFINFO_AUTO_STOP_L2();
			return false;
		}
	}

	// Lock the containers only after determining that they are all ready.
	for(i = 0; i < lockCount; ++i)
	{
		ObjectLockInstance *instance = (ObjectLockInstance*)((char*)data + cmd->dataSize + sizeof(int)) + i;
		fullyLockObjectLockInstance(instance);
	}

	PERFINFO_AUTO_STOP_L2();
	return true;
}

static void gwtProcessObjectLockInstances(GWTQueuedCmd *cmd)
{
	void *data;
	int lockCount;
	int i;
	PERFINFO_AUTO_START_FUNC_L2();
	if(cmd->type < GWT_CMD_USER_START)
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}
	data = getDataFromGWTCmd(cmd);
	lockCount = *((int*)((char*)data + cmd->dataSize));
	// Unlock the containers before incrementing their processed count, because they're not "ready" to be
	// acted upon until they're unlocked. This avoids unnecessary sleeping if another thread finds the
	// container "ready" but still locked.
	for(i = 0; i < lockCount; ++i)
	{
		ObjectLockInstance *instance = (ObjectLockInstance*)((char*)data + cmd->dataSize + sizeof(int)) + i;
		fullyUnlockObjectLockInstance(instance);
	}
	for(i = 0; i < lockCount; ++i)
	{
		ObjectLockInstance *instance = (ObjectLockInstance*)((char*)data + cmd->dataSize + sizeof(int)) + i;
		processObjectLockInstance(instance);
	}
	PERFINFO_AUTO_STOP_L2();
}
