GCC_SYSTEM

#define TIMING_C
#include "stdtypes.h"

#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "wininclude.h"

#if !PLATFORM_CONSOLE
	#include <Tlhelp32.h>
	#include <psapi.h>
#endif

#include "utils.h"
#include "file.h"
#include "timing_profiler.h"
#include "timing_profiler_interface.h"
#include "mathutil.h"
#include "assert.h"
#include "MemoryPool.h"
#include "strings_opt.h"
#include "StashTable.h"
#include "timing.h"
#include "EArray.h"
#include "iocp.h"
#include "pipeServer.h"
#include "pipeClient.h"
#include "fragmentedBuffer.h"
#include "sysutil.h"
#include "net.h"
#include "mutex.h"
#include "CrypticPorts.h"
#include "process_util.h"
#include "stringcache.h"
#include "cpu_count.h"
#include "stashtable.h"
#include "MemoryMonitor.h"
#include "textparser.h"
#include "UTF8.h"

#if !DISABLE_PERFORMANCE_COUNTERS

#if _XBOX
	#define MIN_FRAME_CYCLES (1 * SQR(1000))
#else
	#define MIN_FRAME_CYCLES (20 * SQR(1000))
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define FT_TO_U64(ft) ((U64)ft.dwLowDateTime | ((U64)ft.dwHighDateTime << 32))

typedef struct TimerThreadData TimerThreadData;
typedef struct PerformanceInfo PerformanceInfo;

AutoTimersPublicState autoTimersPublicState;

//--- Options Begin --------------------------------------------------------------------------------

// Use RDTSC to get the current time.  If disabled, uses QPC.
#define USE_RDTSC					1

// Write extra data to verify that the FragmentedBuffer is working.
#define WRITE_CYCLE_CHECK_DATA		0

// Write strings to check that the FragmentedBuffer is being read correctly.
#define WRITE_CHECK_STRINGS			0

// Keep track of the last X timer stacks, for finding START/STOP mismatches.
#define TRACK_CALL_STACKS			0

// Check the top timer for correct thread ID when it is popped.
#define CHECK_THREAD_ID_ON_END		0

// Verify that all timers were written when a diff is sent.
#define VERIFY_ALL_WRITTEN_ON_DIFF	0

// Verify that a ReaderUpdateList is valid whenever it is modified.
#define VERIFY_READER_UPDATE_LIST	0

// Print a bunch of crap for debugging various things.
#define PRINT_DEBUG_STUFF			0

// Profiler server thread count.  Use > 1 to profile the server itself when idle.
#define TIMER_SERVER_THREAD_COUNT	1

// Disables using the OS cycles functions (still uses the crappy-accuracy tick functions).
#define DISABLE_OS_CYCLE_FUNCTIONS	0

//--- Options End ----------------------------------------------------------------------------------

#define PROFILER_FILE_VERSION		1

//--- File Format History Begin --------------------------------------------------------------------
// Version 1: Added depth to decoder state.
//--- File Format History End ----------------------------------------------------------------------

#if USE_RDTSC
	#define GET_CURRENT_CYCLES(x) GET_CPU_TICKS_64(x)
#else
	#define GET_CURRENT_CYCLES(x) {x = timerCpuTicks64();}
#endif

#if WRITE_CHECK_STRINGS
	#define WRITE_CHECK_STRING(fb, s) fbWriteString(fb, s)
	#define READ_CHECK_STRING(fbr, checkString)												\
		{																					\
			char stringRead[100] = "not set";												\
			if(!fbReadString(fbr, SAFESTR(stringRead))){									\
				assertmsg(0, "Didn't read a check string");									\
			}																				\
			else if(strcmp(stringRead, checkString)){										\
				assertmsgf(0, "Wrong check string, %s, not %s", stringRead, checkString);	\
			}																				\
		}
#else
	#define WRITE_CHECK_STRING(fb, s)
	#define READ_CHECK_STRING(fbr, checkString)
#endif

#if TRACK_CALL_STACKS
	typedef struct TimerThreadStacks {
		struct {
			struct {
				U32				cur;
				U32				hi;
				U32				start;
			} depth;

			PerformanceInfo*	info[30];
		} stack[100];
		S32						curStack;
	} TimerThreadStacks;
#endif

typedef struct PerformanceInfo {
	// Begin "stuff that needs to be in one cache line".

	PerfInfoStaticData**		staticData;
	PerformanceInfo*			childTreeRoot;
	PerfInfoGuard**				guardCurrent;

	struct {
		PerformanceInfo*		hi;				// Higher staticData.
		PerformanceInfo*		lo;				// Lower staticData.
	} siblingTree;

	struct {
		U64						cyclesTotal;
		U64						cyclesMax;
		U32						count;
	} currentFrame;

	struct {
		U64						cyclesBegin;
	} currentRun;

	struct {
		U32						isBreakpoint		: 1;
		U32						forcedOpen			: 1;
		U32						forcedClosed		: 1;
	} atomicFlags;

	struct {
		PerformanceInfoType		infoType			: 3;
		U32						isBlocking			: 1;
		
		U32						writtenPreviously	: 1;

		U32						writtenIsBreakPoint	: 1;
		U32						writtenForcedOpen	: 1;
		U32						writtenForcedClosed	: 1;
	} flags;

	// End "stuff that needs to be in one cache line".

	const char*					locName;
	U32							threadID;

	HANDLE						processHandle;
	U32							processID;

	U32							instanceID;
	
	U32							tdFrameWhenCreate;

	PerformanceInfo*			parent;			// My parent pi.
	PerformanceInfo*			nextSibling;	// My next sibling.

	struct {
		PerformanceInfo*		head;			// My first child.
		PerformanceInfo*		tail;			// My last child.
	} children;
} PerformanceInfo;

typedef struct OSTimes {
	U64							cycles;
	U64							ticksUser;
	U64							ticksKernel;
} OSTimes;

typedef struct TimerThreadDataRoot {
	PerformanceInfo*			childTreeRoot;

	struct {
		struct {
			U64					begin;
			U64					end;
		} cycles;
		
		OSTimes					osValues;
	} frame;

	struct {
		PerformanceInfo*		head;
		PerformanceInfo*		tail;
	} rootList;
} TimerThreadDataRoot;
	
typedef struct TimerThreadData {
	TimerThreadData*			nextThread;
	
	char*						name;

	U32							instanceID;
	U32							threadID;
	HANDLE						threadHandle;
	
	U32							frameCount;
	U32							frameWhenResetStack;
	
	U32							msWriteFrame;
	
	OSTimes						osValuesPrev;

	struct {
		U32						enabledCount;
		U32						recursionDisabledCount;
		U32						recursionDisabledExternallyCount;
		U32						resetOnNextCall;
		U32						lock;
	} special;
	
	struct {
		struct {
			U32					isAdded		: 1;
			U32					isDead		: 1;
		} flags;
	} cleanupThread;
	
	struct {
		OSTimes					osTimesPrev;
		U64						cyclesPrev;

		struct {
			U32					prevIsSet	: 1;
		} flags;
	} scanFrame;

	U32							startInstanceID;
	U32							lastTimerInstanceID;
	U32							timerFlagsChangedInstanceID;

	TimerThreadDataRoot			roots[2];
	TimerThreadDataRoot*		curRoot;
	
	struct {
		U32						maxDepth;
		U32						readerInstanceID;
		U32						timerFlagsChangedInstanceID;
	} written;

	struct {
		PerformanceInfo*		top;
		U32						topDepth;
		U32						curDepth;
	} timerStack;

	#if TRACK_CALL_STACKS
		TimerThreadStacks*		stacks;
	#endif

	struct {
		U32						hasNewChild				: 1;
		U32						wroteName				: 1;
		U32						properlyStartedFrame	: 1;
		U32						writingFrame			: 1;
		U32						osValuesPrevIsSet		: 1;
	} flags;
} TimerThreadData;

typedef struct AutoTimerCS {
	CrypticalSection			cs;
} AutoTimerCS;

typedef struct SharedReaderUpdate {
	FragmentedBuffer*			fb;
	U32							refCount;
	U32							oldReaderInstanceID;
	U32							newReaderInstanceID;
	
	struct {
		U32						sendToAll : 1;
	} flags;
} SharedReaderUpdate;

typedef struct ReaderUpdate ReaderUpdate;
typedef struct ReaderUpdate {
	ReaderUpdate*				next;
	SharedReaderUpdate*			sru;
} ReaderUpdate;

typedef struct ReaderUpdateList {
	ReaderUpdate*				head;
	ReaderUpdate*				tail;
	U32							count;
	U32							bytes;
} ReaderUpdateList;

typedef struct AutoTimerReaderStream {
	AutoTimerReader*			r;
	void*						userPointer;
	U32							instanceID;
} AutoTimerReaderStream;

enum {
	ATR_ATOMIC_FLAG_WAITING_FOR_NOTIFY		= BIT(0),
	ATR_ATOMIC_FLAG_NEW_BUFFER_AVAILABLE	= BIT(1),
};

typedef struct AutoTimerReader {
	AutoTimerReaderMsgHandler	msgHandler;
	void*						userPointer;

	AutoTimerReaderStream**		streams;
	U32							minInstanceID;
	U32							maxInstanceID;
	ReaderUpdateList			ruList;
	
	U32							atomicNotifyFlags;
	
	struct {
		AutoTimerReaderStream**	streams;
		void**					streamUserPointers;
	} newBufferData;
	
	struct {
		U32						bufferOverflowed : 1;
	} flags;
} AutoTimerReader;

typedef struct TrackedProcess {
	U32				pid;
	HANDLE			hProcess;
	const char*		exeFileName;
	OSTimes			osTimesPrev;
	OSTimes			osTimesCur;
	PERFINFO_TYPE**	piPtr;
	
	struct {
		U32			hasQueryHandle : 1;
	} flags;
} TrackedProcess;

typedef struct AutoTimers {
	S32							initialized;
	
	#if !PLATFORM_CONSOLE
		U32						pidSelf;
	#endif

	S32							ignoreDuringCreateThread;

	S32							assertShown;
	S32							disablePopupErrors;
	S32							disableConnectAtStartup;
	
	#if TRACK_CALL_STACKS
		S32						trackCallStacks;
	#endif

	U32							maxDepth;

	U32							startInstanceID;
	
	char*						appName;
	U32							appNameCount;

	struct {
		U32						data;
	} tlsIndex;

	struct {
		AutoTimerCS				threadInstance;
		AutoTimerCS				memPool;
		AutoTimerCS				sequencing;
	} cs;

	struct {
		TimerThreadData*		head;
		TimerThreadData*		tail;
		StashTable				stTIDToThread;
		U32						curThreadInstanceID;
		U32						lock;
		U32						lockThreadID;
	} threadList;
	
	struct {
		U32						threadID;
		HANDLE					eventNewThread;
		HANDLE**				allHandles;
	} cleanupThread;

	struct {
		U32						minInstanceID;
		U32						lastInstanceID;

		U32						streamCount;
		U32						lockStreamCount;
		
		AutoTimerReader**		readers;
	} readers;

	#if !PLATFORM_CONSOLE
	struct {
		HANDLE					eventHasReaders;
		S32						waitingForEvent;
		StashTable				stPIDToProcess;
		TrackedProcess**		tps;
		U32						scannerThreadID;
		WaitThreadGroup*		wtg;
	} processes;
	#endif
} AutoTimers;

static AutoTimers autoTimers;

timerConnectedCallback * g_ConnectedCallback = NULL;

#if !PLATFORM_CONSOLE
typedef BOOL	(WINAPI* GetThreadTimesFunc)(HANDLE hThread, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime, LPFILETIME lpUserTime);
typedef BOOL	(WINAPI* GetProcessTimesFunc)(HANDLE hProcess, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime, LPFILETIME lpUserTime);
typedef BOOL	(WINAPI* GetProcessMemoryInfoFunc)(HANDLE Process, PPROCESS_MEMORY_COUNTERS ppsmemCounters, DWORD cb);
typedef HANDLE	(WINAPI* OpenThreadFunc)(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId);
typedef BOOL	(WINAPI* QueryThreadCycleTimeFunc)(HANDLE ThreadHandle, PULONG64 CycleTime);
typedef BOOL	(WINAPI* QueryProcessCycleTimeFunc)(HANDLE ProcessHandle, PULONG64 CycleTime);
typedef BOOL	(WINAPI* QueryIdleProcessorCycleTimeFunc)(PULONG BufferLength, PULONG64 ProcessorIdleCycleTime);
typedef BOOL	(WINAPI* GetPerformanceInfoFunc)(PPERFORMANCE_INFORMATION pPerformanceInformation, DWORD cb);

static struct {
	S32										initialized;

	struct {
		HMODULE								hDLL;
		GetThreadTimesFunc					funcGetThreadTimes;
		GetProcessTimesFunc					funcGetProcessTimes;
		OpenThreadFunc						funcOpenThread;
		QueryThreadCycleTimeFunc			funcQueryThreadCycleTime;
		QueryProcessCycleTimeFunc			funcQueryProcessCycleTime;
		QueryIdleProcessorCycleTimeFunc		funcQueryIdleProcessorCycleTime;
		GetPerformanceInfoFunc				funcGetPerformanceInfo;
	} kernel32;

	struct {
		HMODULE								hDLL;
		GetProcessMemoryInfoFunc			funcGetProcessMemoryInfo;
		GetPerformanceInfoFunc				funcGetPerformanceInfo;
	} psAPI;
	
	GetPerformanceInfoFunc					funcGetPerformanceInfo;
} dlls;
#endif

static void autoTimerWriteFrame(TimerThreadData* td,
								S32 isFinalFrame);
								
static void autoTimerPopEntireThreadStack(	TimerThreadData* td,
											PerfInfoGuard** guard);

static void autoTimerWriteCount(	FragmentedBuffer* fb,
									U32 count)
{
	if(count < (1 << 3)){
		fbWriteBit(fb, 1);
		fbWriteU32(fb, 3, count);
		return;
	}

	fbWriteBit(fb, 0);

	if(count < (1 << 8)){
		fbWriteBit(fb, 1);
		fbWriteU32(fb, 8, count);
		return;
	}

	fbWriteBit(fb, 0);

	if(count < (1 << 10)){
		fbWriteBit(fb, 1);
		fbWriteU32(fb, 10, count);
		return;
	}

	fbWriteBit(fb, 0);

	if(count < (1 << 16)){
		fbWriteBit(fb, 1);
		fbWriteU32(fb, 16, count);
		return;
	}

	fbWriteBit(fb, 0);
	fbWriteU32(fb, 32, count);
}

static S32 autoTimerReadCount(	FragmentedBufferReader* fbr,
								U32* countOut)
{
	U32 flags;
	U32 count = 0;

	if(	fbReadBit(fbr, &flags) &&
		flags)
	{
		return fbReadU32(fbr, 3, countOut);
	}

	if(	fbReadBit(fbr, &flags) &&
		flags)
	{
		return fbReadU32(fbr, 8, countOut);
	}

	if(	fbReadBit(fbr, &flags) &&
		flags)
	{
		return fbReadU32(fbr, 10, countOut);
	}

	if(	fbReadBit(fbr, &flags) &&
		flags)
	{
		return fbReadU32(fbr, 16, countOut);
	}

	return fbReadU32(fbr, 32, countOut);
}

static void autoTimerWriteCycles(	FragmentedBuffer* fb,
									U64 cycles,
									U64* cyclesWrittenOut)
{
	WRITE_CHECK_STRING(fb, "cycles");

	if(cycles <= (U64)0xffffffff){
		U32 cycles32 = cycles;
		
		if(cycles32 <= BIT_RANGE(0, 15)){
			if(cycles32 <= BIT_RANGE(0, 7)){
				fbWriteU32(fb, 3, 0);
				fbWriteU32(fb, 8, cycles32);
			}else{
				fbWriteU32(fb, 3, 1);
				fbWriteU32(fb, 16, cycles32);
			}
		}
		else if(cycles32 <= BIT_RANGE(0, 23)){
			fbWriteU32(fb, 3, 2);
			fbWriteU32(fb, 24, cycles32);
		}else{
			fbWriteU32(fb, 3, 3);
			fbWriteU32(fb, 32, cycles32);
		}
	}else{
		U32 cycles32 = cycles >> 32;
		
		if(cycles32 <= BIT_RANGE(0, 15)){
			if(cycles32 <= BIT_RANGE(0, 7)){
				fbWriteU32(fb, 3, 4);
				fbWriteU64(fb, 32 + 8, cycles);
			}else{
				fbWriteU32(fb, 3, 5);
				fbWriteU64(fb, 32 + 16, cycles);
			}
		}
		else if(cycles32 <= BIT_RANGE(0, 23)){
			fbWriteU32(fb, 3, 6);
			fbWriteU64(fb, 32 + 24, cycles);
		}else{
			fbWriteU32(fb, 3, 7);
			fbWriteU64(fb, 32 + 32, cycles);
		}
	}
	
	#if WRITE_CYCLE_CHECK_DATA
	{
		while(cycles){
			fbWriteBit(fb, 1);
			fbWriteU64(fb, 8, cycles & 0xff);
			cycles >>= 8;
		}
		fbWriteBit(fb, 0);
	}
	#endif
}

static S32 autoTimerReadCycles(	FragmentedBufferReader* fbr,
								U64* cyclesOut)
{
	U32 flags;
	U64 cycles;
	S32 success;
	U32 bitCount;

	READ_CHECK_STRING(fbr, "cycles");
	
	if(!fbReadU32(fbr, 3, &flags)){
		return 0;
	}
	
	bitCount = BITS_PER_BYTE * (flags + 1);

	if(bitCount <= 32){
		U32 cycles32;
		success = fbReadU32(fbr, bitCount, &cycles32);
		cycles = cycles32;
	}else{
		success = fbReadU64(fbr, bitCount, &cycles);
	}
	
	if(!success){
		return 0;
	}
	
	if(cyclesOut){
		*cyclesOut = cycles;
	}

	#if WRITE_CYCLE_CHECK_DATA
	{
		U64 c = 0;
		U32 loBit = 0;

		while(1){
			U32 more;

			if(!fbReadBit(fbr, &more)){
				return 0;
			}
			else if(!more){
				break;
			}else{
				U64 b;
				if(!fbReadU64(fbr, BITS_PER_BYTE, &b)){
					break;
				}
				c |= b << loBit;
				loBit += BITS_PER_BYTE;
			}
		}

		assert(c == cycles);
	}
	#endif

	return 1;
}

static TimerThreadData* timerGetCurrentThreadDataOrFail(S32 alwaysCreate);
static void autoTimerRecursionDisableInc(TimerThreadData* td);
static void autoTimerRecursionDisableDec(TimerThreadData* td);

#define CURRENT_THREAD_ID						(GetCurrentThreadId())
#define THREAD_DATA								(timerGetCurrentThreadData())
#define THREAD_DATA_NO_FAIL						(timerGetCurrentThreadDataOrFail(1))
#define THREAD_TOP								(td->timerStack.top)
#define THREAD_CUR_DEPTH						(td->timerStack.curDepth)
#define THREAD_TOP_DEPTH						(td->timerStack.topDepth)

static S32 autoTimerCSEnter(AutoTimerCS* cs){
	csEnter(&cs->cs);

	return cs->cs.ownerEnterCount == 1;
}

static S32 autoTimerCSLeave(AutoTimerCS* cs){
	S32 ret = cs->cs.ownerEnterCount == 1;
	assert(cs->cs.ownerThreadID == CURRENT_THREAD_ID);
	assert(cs->cs.ownerEnterCount);
	csLeave(&cs->cs);
	return ret;
}

static void memoryPoolEnterCS(void){
	U32 tid = CURRENT_THREAD_ID;
	assert(autoTimers.cs.threadInstance.cs.ownerThreadID != tid);
	if(autoTimerCSEnter(&autoTimers.cs.memPool)){
		assert(autoTimers.cs.sequencing.cs.ownerThreadID != tid);
	}
}

static void memoryPoolLeaveCS(void){
	autoTimerCSLeave(&autoTimers.cs.memPool);
}

static void threadInstanceEnterCS(void){
	U32 tid = CURRENT_THREAD_ID;
	assert(autoTimers.cs.memPool.cs.ownerThreadID != tid);
	assert(autoTimers.cs.sequencing.cs.ownerThreadID != tid);
	autoTimerCSEnter(&autoTimers.cs.threadInstance);
}

static void threadInstanceLeaveCS(void){
	autoTimerCSLeave(&autoTimers.cs.threadInstance);
}

static void modifySequencingEnterCS(void){
	TimerThreadData* td = THREAD_DATA_NO_FAIL;
	autoTimerRecursionDisableInc(td);
	{
		//assert(autoTimers.cs.threadInstance.tid != CURRENT_THREAD_ID);
		if(autoTimerCSEnter(&autoTimers.cs.sequencing)){
			assert(autoTimers.cs.memPool.cs.ownerThreadID != CURRENT_THREAD_ID);
		}
		autoTimerCSEnter(&autoTimers.cs.memPool);
	}
	autoTimerRecursionDisableDec(td);
}

static void modifySequencingLeaveCS(void){
	TimerThreadData* td = THREAD_DATA_NO_FAIL;
	autoTimerRecursionDisableInc(td);
	{
		S32 leftMemPool = autoTimerCSLeave(&autoTimers.cs.memPool);
		S32 leftSequencing = autoTimerCSLeave(&autoTimers.cs.sequencing);
		assert(leftMemPool == leftSequencing);
	}
	autoTimerRecursionDisableDec(td);
}

MP_DEFINE(PerformanceInfo);

static PerformanceInfo* piCreate(const char* locName){
	PerformanceInfo* pi;
	
	memoryPoolEnterCS();
	{
		if(!MP_NAME(PerformanceInfo)){
			MP_NAME(PerformanceInfo) = createMemoryPoolNamed("PerformanceInfo", __FILE__, __LINE__);
			mpSetChunkAlignment(MP_NAME(PerformanceInfo), 64);
			initMemoryPool(MP_NAME(PerformanceInfo), (sizeof(PerformanceInfo) + 63) & ~63, 100);
			mpSetMode(MP_NAME(PerformanceInfo), ZeroMemoryBit);
		}
		pi = MP_ALLOC(PerformanceInfo);
		assert(!((intptr_t)pi & 63));
	}
	memoryPoolLeaveCS();

	pi->locName = locName;

	return pi;
}

static void destroyPerformanceInfo(PerformanceInfo* pi){
	if(pi){
		memoryPoolEnterCS();
		{
			MP_FREE(PerformanceInfo, pi);
		}
		memoryPoolLeaveCS();
	}
}

static void timerAddToThreadList(TimerThreadData* td){
	assert(!td->nextThread);

	if(!autoTimers.threadList.head){
		autoTimers.threadList.head = autoTimers.threadList.tail = td;
	}else{
		assert(!autoTimers.threadList.tail->nextThread);
		autoTimers.threadList.tail->nextThread = td;
		autoTimers.threadList.tail = td;
	}
}

static void autoTimerRecursionDisableInc(TimerThreadData* td){
	if(!td->special.recursionDisabledCount++){
		InterlockedIncrement(&td->special.enabledCount);
	}
}

static void autoTimerRecursionDisableDec(TimerThreadData* td){
	assert(td->special.recursionDisabledCount);
	
	if(!--td->special.recursionDisabledCount){
		assert(td->special.enabledCount);

		InterlockedDecrement(&td->special.enabledCount);
	}
}

static void autoTimerCreateNewThreadData(	TimerThreadData** tdOut,
											U32 threadID)
{
	TimerThreadData* td;

	td = callocStruct(TimerThreadData);
	
	autoTimerRecursionDisableInc(td);
	
	while(!++autoTimers.threadList.curThreadInstanceID);
	td->instanceID = autoTimers.threadList.curThreadInstanceID;

	td->threadID = threadID;
	
	td->curRoot = td->roots + td->flags.properlyStartedFrame;

	#if 0
	{
		printf(	"Created TimerThreadData 0x%8.8p, thread %d\n",
				td,
				td->threadID);
	}
	#endif

	if(threadID == CURRENT_THREAD_ID){
		autoTimerRecursionDisableInc(td);

		DuplicateHandle(	GetCurrentProcess(),
							GetCurrentThread(),
							GetCurrentProcess(),
							&td->threadHandle,
							0,
							FALSE,
							DUPLICATE_SAME_ACCESS);

		if(td->threadHandle){
			TlsSetValue(autoTimers.tlsIndex.data,
						td);
		}
	}else{
		td->threadHandle = OpenThread(	THREAD_ALL_ACCESS,
										FALSE,
										threadID);
	}
	
	if(td->threadHandle){
		timerAddToThreadList(td);
		
		if(tdOut){
			*tdOut = td;
		}

		SetEvent(autoTimers.cleanupThread.eventNewThread);

		autoTimerRecursionDisableDec(td);
	}else{
		SAFE_FREE(td);

		if(tdOut){
			*tdOut = NULL;
		}
	}
}

static struct {
	U32	lock;
	U32	threadID;
} threadCreateLock;

static TimerThreadData* timerGetCurrentThreadDataOrFail(S32 alwaysCreate){
	TimerThreadData* td = autoTimers.tlsIndex.data ?
							TlsGetValue(autoTimers.tlsIndex.data) :
							NULL;

	if(	!td &&
		autoTimers.tlsIndex.data)
	{
		const U32 curThreadID = CURRENT_THREAD_ID;
		
		while(1){
			if(threadCreateLock.lock){
				if(	threadCreateLock.threadID == curThreadID ||
					!alwaysCreate)
				{
					return NULL;
				}

				Sleep(1);
			}
			else if(InterlockedIncrement(&threadCreateLock.lock) == 1){
				threadCreateLock.threadID = curThreadID;
				break;
			}else{
				InterlockedDecrement(&threadCreateLock.lock);

				if(threadCreateLock.threadID == curThreadID){
					return NULL;
				}
				else if(!alwaysCreate){
					// Just give up if someone else (even the same thread) is creating too.
					// If this doesn't give up, a deadlock can occur in this circumstance:
					// Thread 1: Has this lock, tries to get heap lock in threadInstanceEnterCS.
					// Thread 2: Has heap lock, tries to get this lock.
					return NULL;
				}else{
					Sleep(1);
				}
			}
		}
			
		if(curThreadID != autoTimers.cleanupThread.threadID){
			writeLockU32(&autoTimers.threadList.lock, 0);
			autoTimers.threadList.lockThreadID = curThreadID;
		}

		threadInstanceEnterCS();
		{
			if(autoTimers.ignoreDuringCreateThread){
				// This means the pi was re-entrant (probably called from memory
				// allocator) so just return.

				threadInstanceLeaveCS();
				return NULL;
			}
			
			// Check if it already exists but was created from another thread so TLS isn't set.
			
			for(td = autoTimers.threadList.head; td; td = td->nextThread){
				if(td->threadID == curThreadID){
					break;
				}
			}
			
			if(td){
				// Make sure this isn't a dead thread with the same ID that hasn't been cleaned up.
				
				switch(WaitForSingleObject(td->threadHandle, 0)){
					xcase WAIT_OBJECT_0:
					acase WAIT_ABANDONED:{
						td = NULL;
					}
				}
			}
			
			if(td){
				autoTimerRecursionDisableInc(td);

				TlsSetValue(autoTimers.tlsIndex.data,
							td);
			}else{
				autoTimers.ignoreDuringCreateThread = 1;
				
				autoTimerCreateNewThreadData(&td, curThreadID);

				autoTimers.ignoreDuringCreateThread = 0;
			}
		}
		threadInstanceLeaveCS();

		autoTimerRecursionDisableDec(td);

		if(curThreadID != autoTimers.cleanupThread.threadID){
			assert(autoTimers.threadList.lockThreadID == curThreadID);
			autoTimers.threadList.lockThreadID = 0;
			writeUnlockU32(&autoTimers.threadList.lock);
		}

		threadCreateLock.threadID = 0;
		InterlockedDecrement(&threadCreateLock.lock);
	}

	return td;
}

struct TimerThreadData* timerGetCurrentThreadData(void)
{
	TimerThreadData* td = autoTimers.tlsIndex.data ?
							TlsGetValue(autoTimers.tlsIndex.data) :
							NULL;

	if(td){
		return td;
	}else{
		return timerGetCurrentThreadDataOrFail(0);
	}
}

// Version of AutoTimerData
#define AUTOTIMERDATA_VERSION 1

// Function table for child modules
// When you add a new function to this table, or change it in some other way, increment AUTOTIMERDATA_VERSION.
// Also change both autoTimerGet() and autoTimerSet().
typedef struct AutoTimerData
{
	U32 version;
	void (*fp_autoTimerRegisterPublicState)(AutoTimersPublicState *publicState);
	void (*fp_autoTimerThreadFrameBegin)(const char* threadName);
	void (*fp_autoTimerThreadFrameEnd)(void);
	void (*fp_autoTimerDisableRecursion)(S32* didDisableOut);
	void (*fp_autoTimerEnableRecursion)(S32 didDisable);
	void (__fastcall *fp_autoTimerAddFakeCPU)(U64 amount);
	void (__fastcall *fp_autoTimerBeginCPU)(const char* locName, PerfInfoStaticData** piStatic);
	void (__fastcall *fp_autoTimerBeginCPUGuard)(const char* locName, PerfInfoStaticData** piStatic, PerfInfoGuard** guard);
	void (__fastcall *fp_autoTimerBeginCPUBlocking)(const char* locName, PerfInfoStaticData** piStatic);
	void (__fastcall *fp_autoTimerBeginCPUBlockingGuard)(const char* locName, PerfInfoStaticData** piStatic, PerfInfoGuard** guard);
	void (__fastcall *fp_autoTimerBeginBits)(Packet* pak, const char* locName, PerfInfoStaticData** piStatic);
	void (__fastcall *fp_autoTimerBeginMisc)(U64 startVal, const char* locName, PerfInfoStaticData** piStatic);
	void (__fastcall *fp_autoTimerEndCPU)(void);
	void (__fastcall *fp_autoTimerEndCPUGuard)(PerfInfoGuard** guard);
	void (__fastcall *fp_autoTimerEndCPUCheckLocation)(const char* locName);
	void (__fastcall *fp_autoTimerEndBits)(Packet* pak);
	void (__fastcall *fp_autoTimerEndMisc)(U64 stopVal);
} AutoTimerData;

// If set, delegate to functions on this function table.
static const AutoTimerData *override = NULL;

void autoTimerDisableRecursion(S32* didDisableOut){
	if (override)
		override->fp_autoTimerDisableRecursion(didDisableOut);
	else if(didDisableOut){
		TimerThreadData* td = THREAD_DATA;

		if(td){
			autoTimerRecursionDisableInc(td);
			td->special.recursionDisabledExternallyCount++;

			//if(td->recursionDisabledCount == 1){
			//	printf(	"%d: rd=%d/%d\n",
			//			CURRENT_THREAD_ID,
			//			td->recursionDisabledCount,
			//			td->recursionDisabledExternallyCount);
			//}

			*didDisableOut = 1;
		}else{
			*didDisableOut = 0;
		}
	}
}

void autoTimerEnableRecursion(S32 didDisable){
	if (override)
		override->fp_autoTimerEnableRecursion(didDisable);
	if(didDisable){
		TimerThreadData* td = THREAD_DATA;

		if(td){
			assert(td->special.recursionDisabledCount);
			assert(td->special.recursionDisabledExternallyCount);
			assert(	td->special.recursionDisabledCount >=
					td->special.recursionDisabledExternallyCount);

			td->special.recursionDisabledExternallyCount--;
			autoTimerRecursionDisableDec(td);

			//if(!td->recursionDisabledCount){
			//	printf(	"%d: rd=%d/%d\n",
			//			CURRENT_THREAD_ID,
			//			td->recursionDisabledCount,
			//			td->recursionDisabledExternallyCount);
			//}
		}
	}
}

static void autoTimerGetNewReaderInstanceID(U32* instanceIDOut){
	while(1){
		S32 foundNew = 1;

		while(!++autoTimers.readers.lastInstanceID);

		EARRAY_CONST_FOREACH_BEGIN(autoTimers.readers.readers, i, isize);
			AutoTimerReader* r = autoTimers.readers.readers[i];

			EARRAY_CONST_FOREACH_BEGIN(r->streams, j, jsize);
				AutoTimerReaderStream* s = r->streams[j];

				if(s->instanceID == autoTimers.readers.lastInstanceID){
					foundNew = 0;
					break;
				}
			EARRAY_FOREACH_END;
			
			if(!foundNew){
				break;
			}
		EARRAY_FOREACH_END;

		if(foundNew){
			break;
		}
	}

	*instanceIDOut = autoTimers.readers.lastInstanceID;
}

#if !VERIFY_READER_UPDATE_LIST
	#define ruListVerify(ruList)
#else
	static void ruListVerify(const ReaderUpdateList* ruList){
		const ReaderUpdate* ru;
		U32					count = 0;
		U32					bytes = 0;
		
		for(ru = ruList->head; ru; ru = ru->next){
			U32 bytesCur;
			
			count++;
			
			if(fbGetSizeAsBytes(ru->sru->fb, &bytesCur)){
				bytes += bytesCur;
			}
			
			if(!ru->next){
				assert(ru == ruList->tail);
			}
		}

		assert(count == ruList->count);
		assert(bytes == ruList->bytes);
	}
#endif

static void ruListAdd(	ReaderUpdateList* ruList,
						SharedReaderUpdate* sru)
{
	ReaderUpdate*	ru = callocStruct(ReaderUpdate);
	U32				bytes;
	
	fbGetSizeAsBytes(sru->fb, &bytes);
	
	ru->sru = sru;
	if(ruList->tail){
		assert(ruList->count);
		assert(ruList->head);
		if(ruList->count == 1){
			assert(ruList->head == ruList->tail);
		}
		ruList->tail->next = ru;
	}else{
		assert(!ruList->head);
		assert(!ruList->count);
		ruList->head = ru;
	}

	ruList->tail = ru;
	ruList->count++;
	ruList->bytes += bytes;
	
	ruListVerify(ruList);

	sru->refCount++;
}

static void autoTimerReadersUpdateMinInstanceID(void){
	autoTimers.readers.minInstanceID = U32_MAX;
	
	EARRAY_CONST_FOREACH_BEGIN(autoTimers.readers.readers, i, isize);
		AutoTimerReader* r = autoTimers.readers.readers[i];
		
		MIN1(autoTimers.readers.minInstanceID, r->minInstanceID);
	EARRAY_FOREACH_END;
}

void autoTimerReaderCreate(	AutoTimerReader** rOut,
							AutoTimerReaderMsgHandler msgHandler,
							void* userPointer)
{
	TimerThreadData*	td = THREAD_DATA_NO_FAIL;
	AutoTimerReader*	r;

	if(	!rOut ||
		!msgHandler)
	{
		return;
	}

	r = callocStruct(AutoTimerReader);
	
	r->msgHandler = msgHandler;
	r->userPointer = userPointer;

	*rOut = r;
}

static void autoTimerReaderUpdateMinMaxInstanceID(AutoTimerReader* r){
	r->minInstanceID = U32_MAX;
	r->maxInstanceID = 0;
	
	EARRAY_CONST_FOREACH_BEGIN(r->streams, i, isize);
		MIN1(r->minInstanceID, r->streams[i]->instanceID);
		MAX1(r->maxInstanceID, r->streams[i]->instanceID);
	EARRAY_FOREACH_END;
}

static void autoTimerReaderSendNotifyIfNecessary(AutoTimerReader* r){
	// Check if the reader is waiting for more data.
	
	if(	_InterlockedOr(&r->atomicNotifyFlags, ATR_ATOMIC_FLAG_NEW_BUFFER_AVAILABLE) ==
		ATR_ATOMIC_FLAG_WAITING_FOR_NOTIFY)
	{
		// Reader was waiting for data, and I'm first, so send a msg.
		
		AutoTimerReaderMsg msg = {0};

		msg.msgType = ATR_MSG_ANY_THREAD_NEW_BUFFER_AVAILABLE;
		msg.userPointer = r->userPointer;
		
		r->msgHandler(&msg);
	}
}

#if !PLATFORM_CONSOLE
static void autoTimerGetProcessOSTimes(	HANDLE hProcess,
										OSTimes* osTimesOut)
{
	FILETIME ftCreation;
	FILETIME ftExit;
	FILETIME ftKernel;
	FILETIME ftUser;

	PERFINFO_AUTO_START_FUNC();
	
	if(	!dlls.kernel32.funcQueryProcessCycleTime ||
		!dlls.kernel32.funcQueryProcessCycleTime(hProcess, &osTimesOut->cycles))
	{
		osTimesOut->cycles = 0;
	}

	if(	!dlls.kernel32.funcGetProcessTimes ||
		!dlls.kernel32.funcGetProcessTimes(hProcess, &ftCreation, &ftExit, &ftKernel, &ftUser))
	{
		osTimesOut->ticksUser = osTimesOut->ticksKernel = 0;
	}else{
		osTimesOut->ticksUser = FT_TO_U64(ftUser);
		osTimesOut->ticksKernel = FT_TO_U64(ftKernel);
	}
	
	PERFINFO_AUTO_STOP();
}

static void trackedProcessHandleClosedCB(	void* userPointer,
											HANDLE handle,
											TrackedProcess* tp)
{
	if(handle == autoTimers.processes.eventHasReaders){
		ASSERT_TRUE_AND_RESET(autoTimers.processes.waitingForEvent);

		wtgRemoveHandle(autoTimers.processes.wtg, handle, NULL);
	}else{
		wtgRemoveHandle(autoTimers.processes.wtg, handle, tp);
		assert(tp->hProcess == handle);
		CloseHandle(handle);
		tp->hProcess = NULL;
		tp->flags.hasQueryHandle = 0;
	}
}

static S32 processScanCB(const ForEachProcessCallbackData* data){
	U32 pid = data->pid;
	
	if(	pid &&
		!stashIntFindPointer(autoTimers.processes.stPIDToProcess, pid, NULL))
	{
		HANDLE	hProcess;
		S32		hasSyncHandle = 1;
		S32		hasQueryHandle = 1;
		
		PERFINFO_AUTO_START("OpenProcess(sync, query)", 1);
		hProcess = OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION, FALSE, pid);
		PERFINFO_AUTO_STOP();
		
		if(!hProcess){
			PERFINFO_AUTO_START("OpenProcess(sync, query limited)", 1);
			hProcess = OpenProcess(SYNCHRONIZE|0x1000, FALSE, pid);
			PERFINFO_AUTO_STOP();
			
			if(!hProcess){
				hasSyncHandle = 0;

				PERFINFO_AUTO_START("OpenProcess(query)", 1);
				hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
				PERFINFO_AUTO_STOP();
				
				if(!hProcess){
					PERFINFO_AUTO_START("OpenProcess(query limited)", 1);
					hProcess = OpenProcess(0x1000, FALSE, pid);
					PERFINFO_AUTO_STOP();

					if(!hProcess){
						hasSyncHandle = 1;
						hasQueryHandle = 0;

						PERFINFO_AUTO_START("OpenProcess(sync)", 1);
						hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
						PERFINFO_AUTO_STOP();
					}
				}
			}
		}
		
		{
			TrackedProcess* tp = callocStruct(TrackedProcess);
			
			tp->pid = pid;
			tp->hProcess = hProcess;
			tp->exeFileName = allocAddCaseSensitiveString(data->exeFileName);
			tp->piPtr = callocStruct(PERFINFO_TYPE*);
			
			if(tp->hProcess){
				tp->flags.hasQueryHandle = hasQueryHandle;
			}

			eaPush(&autoTimers.processes.tps, tp);

			if(!stashIntAddPointer(autoTimers.processes.stPIDToProcess, pid, tp, false)){
				assert(0);
			}

			if(hasSyncHandle){
				wtgAddHandle(	autoTimers.processes.wtg,
								hProcess,
								trackedProcessHandleClosedCB,
								tp);
			}
		}
	}

	return 1;
}

static S32 threadScanCB(const ForEachThreadCallbackData* data){
	TimerThreadData* tdSelf = data->userPointer;

	if(data->pid == autoTimers.pidSelf){
		// Check if this thread id is already used.
		
		TimerThreadData* td;

		readLockU32(&autoTimers.threadList.lock);
		for(td = autoTimers.threadList.head;
			td && td->threadID != data->tid;
			td = td->nextThread);
		readUnlockU32(&autoTimers.threadList.lock);
		
		if(!td){
			PERFINFO_AUTO_START("new thread found", 1);
			autoTimerRecursionDisableInc(tdSelf);
			threadInstanceEnterCS();
			autoTimerCreateNewThreadData(NULL, data->tid);
			threadInstanceLeaveCS();
			autoTimerRecursionDisableDec(tdSelf);
			PERFINFO_AUTO_STOP();
		}
	}

	return 1;
}

static void autoTimerGetThreadOSTimes(	HANDLE hThread,
										OSTimes* osTimesOut)
{
	FILETIME ftCreation;
	FILETIME ftExit;
	FILETIME ftKernel;
	FILETIME ftUser;

	if(	!dlls.kernel32.funcQueryThreadCycleTime ||
		!dlls.kernel32.funcQueryThreadCycleTime(hThread,
												&osTimesOut->cycles))
	{
		osTimesOut->cycles = 0;
	}
	
	if(	!dlls.kernel32.funcGetThreadTimes ||
		!dlls.kernel32.funcGetThreadTimes(	hThread,
											&ftCreation,
											&ftExit,
											&ftKernel,
											&ftUser))
	{
		osTimesOut->ticksUser = osTimesOut->ticksKernel = 0;
	}else{
		osTimesOut->ticksUser = FT_TO_U64(ftUser);
		osTimesOut->ticksKernel = FT_TO_U64(ftKernel);
	}
}
#endif

static void autoTimerSendBufferToReaders(SharedReaderUpdate* sru){
	S32 foundReader = 0;

	if(!sru){
		return;
	}

	modifySequencingEnterCS();
	{
		EARRAY_CONST_FOREACH_BEGIN(autoTimers.readers.readers, i, isize);
			AutoTimerReader* r = autoTimers.readers.readers[i];

			if(	!r->flags.bufferOverflowed &&
				(	sru->flags.sendToAll
					||
					r->maxInstanceID > sru->oldReaderInstanceID &&
					r->minInstanceID <= sru->newReaderInstanceID)
				)
			{
				foundReader = 1;
				ruListAdd(&r->ruList, sru);
				autoTimerReaderSendNotifyIfNecessary(r);
				
				if(r->ruList.bytes >= 50 * SQR(1000)){
					r->flags.bufferOverflowed = 1;
				}
			}
		EARRAY_FOREACH_END;
	}
	modifySequencingLeaveCS();

	if(!foundReader){
		fbDestroy(&sru->fb);
		SAFE_FREE(sru);
	}
}

#if !PLATFORM_CONSOLE
static S32 __stdcall autoTimerSystemStatsThreadMain(void* unusedParam){
	TimerThreadData*	tdSelf = NULL;
	U32					msLastScan = 0;

	EXCEPTION_HANDLER_BEGIN
	
	autoTimers.processes.scannerThreadID = CURRENT_THREAD_ID;
	autoTimers.processes.stPIDToProcess = stashTableCreateInt(100);

	wtgCreate(&autoTimers.processes.wtg);

	while(1){
		autoTimerThreadFrameBegin(__FUNCTION__);
		{
			U32 msCurTime = timeGetTime();
			
			if(!tdSelf){
				tdSelf = THREAD_DATA_NO_FAIL;
			}
			
			if(!autoTimers.readers.streamCount){
				// Reset previous stuff so we don't send ancient updates when it starts again.
				
				EARRAY_CONST_FOREACH_BEGIN(autoTimers.processes.tps, i, isize);
				{
					TrackedProcess* tp = autoTimers.processes.tps[i];
					ZeroStruct(&tp->osTimesPrev);
				}
				EARRAY_FOREACH_END;

				{
					TimerThreadData* td;

					autoTimerRecursionDisableInc(tdSelf);
					readLockU32(&autoTimers.threadList.lock);
					for(td = autoTimers.threadList.head; td; td = td->nextThread){
						td->scanFrame.flags.prevIsSet = 0;
					}
					readUnlockU32(&autoTimers.threadList.lock);
					autoTimerRecursionDisableDec(tdSelf);
				}
				
				// Wait for readers.

				while(!autoTimers.readers.streamCount){
					if(FALSE_THEN_SET(autoTimers.processes.waitingForEvent)){
						wtgAddHandle(	autoTimers.processes.wtg,
										autoTimers.processes.eventHasReaders,
										trackedProcessHandleClosedCB,
										NULL);
					}

					wtgWait(autoTimers.processes.wtg, NULL, INFINITE);
				}
			}else{
				wtgWait(autoTimers.processes.wtg, NULL, 100);
			}
			
			if(	!msLastScan ||
				msCurTime - msLastScan >= 1000)
			{
				U32 oldPriority = GetThreadPriority(GetCurrentThread());
				
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
				forEachProcess(processScanCB, NULL);
				forEachThread(threadScanCB, 0, tdSelf);
				SetThreadPriority(GetCurrentThread(), oldPriority);

				msLastScan = timeGetTime();
			}
			
			if(tdSelf){
				PERFINFO_AUTO_START("sendScanFrame", 1);
				{
					TimerThreadData*	td;
					SharedReaderUpdate*	sru = NULL;
					FragmentedBuffer*	fb = NULL;
					
					autoTimerRecursionDisableInc(tdSelf);
					readLockU32(&autoTimers.threadList.lock);
					
					for(td = autoTimers.threadList.head; td; td = td->nextThread){
						if(	td->msWriteFrame &&
							subS32(msCurTime, td->msWriteFrame) < 5000)
						{
							td->scanFrame.flags.prevIsSet = 0;
						}else{
							OSTimes osTimesCur;
							U64		cyclesCur;
							
							GET_CURRENT_CYCLES(cyclesCur);
							
							autoTimerGetThreadOSTimes(td->threadHandle, &osTimesCur);

							if(!FALSE_THEN_SET(td->scanFrame.flags.prevIsSet)){
								U64 cyclesDelta = osTimesCur.cycles - td->scanFrame.osTimesPrev.cycles;
								U64 ticksUserDelta = osTimesCur.ticksUser - td->scanFrame.osTimesPrev.ticksUser;
								U64 ticksKernelDelta = osTimesCur.ticksKernel - td->scanFrame.osTimesPrev.ticksKernel;
								
								if(	cyclesDelta ||
									ticksUserDelta ||
									ticksKernelDelta)
								{
									if(!sru){
										sru = callocStruct(SharedReaderUpdate);
										fbCreate(&fb, 0);
										sru->fb = fb;
										sru->flags.sendToAll = 1;

										fbWriteString(fb, "ScanFrame");
									}

									fbWriteU32(fb, 32, td->threadID);
									fbWriteU64(fb, 64, td->scanFrame.cyclesPrev);
									fbWriteU64(fb, 64, cyclesCur - td->scanFrame.cyclesPrev);
									fbWriteU64(fb, 64, cyclesDelta);
									fbWriteU64(fb, 64, ticksUserDelta);
									fbWriteU64(fb, 64, ticksKernelDelta);
								}
							}
							
							td->scanFrame.osTimesPrev = osTimesCur;
							td->scanFrame.cyclesPrev = cyclesCur;
						}
					}
					
					readUnlockU32(&autoTimers.threadList.lock);
					autoTimerRecursionDisableDec(tdSelf);
					
					if(sru){
						fbWriteU32(fb, 32, 0);
					
						autoTimerSendBufferToReaders(sru);
					}
				}
				PERFINFO_AUTO_STOP();
			}
			
			// Write cycle time for queryable processes.
			
			{
				U64 total = 0;
				U64 thisProcessCycles = 0;
				U64 thisProcessTicks = 0;

				EARRAY_CONST_FOREACH_BEGIN(autoTimers.processes.tps, i, isize);
				{
					TrackedProcess* tp = autoTimers.processes.tps[i];
					
					if(!tp->flags.hasQueryHandle){
						continue;
					}

					autoTimerGetProcessOSTimes(tp->hProcess, &tp->osTimesCur);
				}
				EARRAY_FOREACH_END;
					
				EARRAY_CONST_FOREACH_BEGIN(autoTimers.processes.tps, i, isize);
				{
					TrackedProcess* tp = autoTimers.processes.tps[i];
					
					if(!tp->flags.hasQueryHandle){
						continue;
					}
					
					if(dlls.kernel32.funcQueryProcessCycleTime){
						if(	tp->osTimesPrev.cycles &&
							tp->osTimesCur.cycles)
						{
							U64 diff = tp->osTimesCur.cycles - tp->osTimesPrev.cycles;
							
							if(diff){
								if(tp->pid == GetCurrentProcessId()){
									thisProcessCycles = diff;
								}else{
									if(!total){
										START_MISC_COUNT(0, "queryable process cycles");
									}

									total += diff;
									START_MISC_COUNT_STATIC(0, tp->exeFileName, tp->piPtr);
									STOP_MISC_COUNT(diff);
								}
							}
						}
					}
					else if(dlls.kernel32.funcGetProcessTimes){
						U64 ticksPrev = tp->osTimesPrev.ticksUser + tp->osTimesPrev.ticksKernel;
						U64 ticksCur = tp->osTimesCur.ticksUser + tp->osTimesCur.ticksKernel;
						
						if(	ticksPrev &&
							ticksCur)
						{
							U64 diff = ticksCur - ticksPrev;
							
							if(diff){
								diff *= timeGetCPUCyclesPerSecond() / (10 * 1000 * 1000);

								if(tp->pid == GetCurrentProcessId()){
									thisProcessTicks = diff;
								}else{
									if(!total){
										START_MISC_COUNT(0, "queryable process ticks");
									}

									total += diff;
									START_MISC_COUNT_STATIC(0, tp->exeFileName, tp->piPtr);
									STOP_MISC_COUNT(diff);
								}
							}
						}
					}
					
					tp->osTimesPrev = tp->osTimesCur;
				}
				EARRAY_FOREACH_END;
				
				if(total){
					STOP_MISC_COUNT(total);
				}
				
				if(thisProcessCycles){
					ADD_MISC_COUNT(thisProcessCycles, "this process cycles");
				}
				else if(thisProcessTicks){
					ADD_MISC_COUNT(thisProcessTicks, "this process ticks");
				}
			}
			
			// Write unqueryable processes.

			{
				S32 found = 0;

				EARRAY_CONST_FOREACH_BEGIN(autoTimers.processes.tps, i, isize);
					TrackedProcess* tp = autoTimers.processes.tps[i];
					
					if(!tp->hProcess){
						if(FALSE_THEN_SET(found)){
							START_MISC_COUNT(0, "unqueryable processes");
						}

						START_MISC_COUNT_STATIC(0, tp->exeFileName, tp->piPtr);
						STOP_MISC_COUNT(0);
					}
				EARRAY_FOREACH_END;
				
				if(found){
					STOP_MISC_COUNT(0);
				}
			}

			// Write idle cycles.
			
			if(dlls.kernel32.funcQueryIdleProcessorCycleTime){
				static U32	cpuCount;
				static U64	prevBuffer[128];
				U64			buffer[ARRAY_SIZE(prevBuffer)];
				
				if(!cpuCount){
					dlls.kernel32.funcQueryIdleProcessorCycleTime(&cpuCount, buffer);
					dlls.kernel32.funcQueryIdleProcessorCycleTime(&cpuCount, prevBuffer);
				}
				else if(cpuCount <= ARRAY_SIZE(buffer)){
					U64 total = 0;
					
					dlls.kernel32.funcQueryIdleProcessorCycleTime(&cpuCount, buffer);
					
					FOR_BEGIN(i, (S32)cpuCount / (S32)sizeof(buffer[0]));
						total += buffer[i] - prevBuffer[i];
					FOR_END;
					
					ADD_MISC_COUNT(total, "idle cycles");
					
					CopyStructs(prevBuffer, buffer, cpuCount);
				}
			}

			// Write system performance info.
			
			if(dlls.funcGetPerformanceInfo){
				PERFORMANCE_INFORMATION pi = {0};
				U64						total = 0;

				pi.cb = sizeof(pi);
				
				PERFINFO_AUTO_START("GetPerformanceInfo", 1);
				dlls.funcGetPerformanceInfo(&pi, sizeof(pi));
				PERFINFO_AUTO_STOP();

				START_MISC_COUNT(0, "OS Performance Info");
					#define COUNT(count, name) total += (count);ADD_MISC_COUNT(count, name);
					COUNT(pi.KernelPaged * pi.PageSize, "Paged Pool Bytes");
					COUNT(pi.KernelNonpaged * pi.PageSize, "Non-paged Pool Bytes");
					COUNT(pi.HandleCount, "Handle Count");
					COUNT(pi.ProcessCount, "Process Count");
					COUNT(pi.ThreadCount, "Thread Count");
					COUNT(pi.PhysicalAvailable * pi.PageSize, "Physical Bytes Available");
					#undef COUNT
				STOP_MISC_COUNT(total);
			}
		}
		autoTimerThreadFrameEnd();
	}

	EXCEPTION_HANDLER_END

	return 0;
}
#endif

static void autoTimerGetFirstInfoUpdate(SharedReaderUpdate** sruOut){
	TimerThreadData*	tdCur;
	FragmentedBuffer*	fb;
	
	*sruOut = callocStruct(SharedReaderUpdate);

	fbCreate(&fb, 0);
	
	(*sruOut)->fb = fb;
	
	fbWriteString(fb, "FirstInfo");
	
	// Write thread list.
	
	readLockU32(&autoTimers.threadList.lock);
	
	for(tdCur = autoTimers.threadList.head;
		tdCur;
		tdCur = tdCur->nextThread)
	{
		fbWriteBit(fb, 1);
		fbWriteU32(fb, 32, tdCur->threadID);
		fbWriteString(fb, tdCur->name);
	}
	
	readUnlockU32(&autoTimers.threadList.lock);

	fbWriteBit(fb, 0);
	
	// Write the cpu speed.
	
	fbWriteBit(fb, 1);
	fbWriteString(fb, "CPUCyclesPerSecond");
	fbWriteU64(fb, 64, timeGetCPUCyclesPerSecond());

	fbWriteBit(fb, 1);
	fbWriteString(fb, "CPUCountReal");
	fbWriteU32(fb, 32, getNumRealCpus());

	fbWriteBit(fb, 1);
	fbWriteString(fb, "CPUCountVirtual");
	fbWriteU32(fb, 32, getNumVirtualCpus());
	
	fbWriteBit(fb, 0);
}

// List of public state objects in other modules that may need to be updated also.
static AutoTimersPublicState *autoTimersPublicStateList[8];
static unsigned autoTimersPublicStateSize = 0;

// Enable profiling.
static void autoTimerEnable()
{
	unsigned i;
	ASSERT_FALSE_AND_SET(autoTimersPublicState.enabled);
	for (i = 0; i != autoTimersPublicStateSize; ++i)
		ASSERT_FALSE_AND_SET(autoTimersPublicStateList[i]->enabled);
}

// Disable profiling.
static void autoTimerDisable()
{
	unsigned i;
	ASSERT_TRUE_AND_RESET(autoTimersPublicState.enabled);
	for (i = 0; i != autoTimersPublicStateSize; ++i)
		ASSERT_TRUE_AND_RESET(autoTimersPublicStateList[i]->enabled);
}

void autoTimerReaderStreamCreate(	AutoTimerReader* r,
									AutoTimerReaderStream** streamOut,
									void* userPointer)
{
	TimerThreadData*		td = THREAD_DATA_NO_FAIL;
	AutoTimerReaderStream*	stream;
	SharedReaderUpdate*		sruFirstInfo;

	autoTimerGetFirstInfoUpdate(&sruFirstInfo);

	stream = callocStruct(AutoTimerReaderStream);
	stream->r = r;
	stream->userPointer = userPointer;

	// Increment the global reader ID.

	writeLockU32(&autoTimers.readers.lockStreamCount, 0);
	modifySequencingEnterCS();
	{
		// Find a unique instance ID.

		autoTimerGetNewReaderInstanceID(&stream->instanceID);

		sruFirstInfo->oldReaderInstanceID = stream->instanceID - 1;
		sruFirstInfo->newReaderInstanceID = stream->instanceID;

		// Add to the reader list.

		if(!autoTimers.readers.streamCount++){
		
			
			#if !PLATFORM_CONSOLE
				if(!autoTimers.processes.scannerThreadID){
					autoTimers.processes.eventHasReaders = CreateEvent(NULL, TRUE, FALSE, NULL);

					_beginthreadex(NULL, 0, autoTimerSystemStatsThreadMain, NULL, 0, NULL);

					while(!autoTimers.processes.scannerThreadID){
						Sleep(1);
					}
				}

				autoTimerEnable();

				assert(autoTimers.processes.eventHasReaders);
				SetEvent(autoTimers.processes.eventHasReaders);
			#endif
		}
		
		autoTimerRecursionDisableInc(td);
		{
			if(!eaPush(&r->streams, stream)){
				eaPush(&autoTimers.readers.readers, r);
			}
			
			r->newBufferData.streams = realloc(	r->newBufferData.streams,
												sizeof(r->newBufferData.streams[0]) *
													eaSize(&r->streams));
			r->newBufferData.streamUserPointers = realloc(	r->newBufferData.streamUserPointers,
															sizeof(r->newBufferData.streamUserPointers[0]) *
																eaSize(&r->streams));
		
			ruListAdd(&r->ruList, sruFirstInfo);
			autoTimerReaderSendNotifyIfNecessary(r);
		}
		autoTimerRecursionDisableDec(td);
		
		autoTimerReaderUpdateMinMaxInstanceID(r);
		autoTimerReadersUpdateMinInstanceID();
	}
	modifySequencingLeaveCS();
	writeUnlockU32(&autoTimers.readers.lockStreamCount);
	
	*streamOut = stream;
}

void autoTimerReaderDestroy(AutoTimerReader** rInOut){
	AutoTimerReader*	r = SAFE_DEREF(rInOut);
	
	if(!r){
		return;
	}
	
	assert(!r->streams);
	
	SAFE_FREE(r->newBufferData.streams);
	SAFE_FREE(r->newBufferData.streamUserPointers);
	SAFE_FREE(*rInOut);
}

void autoTimerReaderStreamDestroy(AutoTimerReaderStream** streamInOut){
	AutoTimerReaderStream*	stream = SAFE_DEREF(streamInOut);
	AutoTimerReader*		r;
	S32						resetAllThreads = 0;
	
	if(!stream){
		return;
	}
	
	r = stream->r;
	
	assert(r);

	writeLockU32(&autoTimers.readers.lockStreamCount, 0);
	modifySequencingEnterCS();
	autoTimerRecursionDisableInc(THREAD_DATA_NO_FAIL);
	{
		if(eaFindAndRemove(&r->streams, stream) < 0){
			assert(0);
		}
		else if(!eaSize(&r->streams)){
			eaDestroy(&r->streams);
			
			if(eaFindAndRemove(&autoTimers.readers.readers, r) < 0){
				assert(0);
			}
			else if(!eaSize(&autoTimers.readers.readers)){
				eaDestroy(&autoTimers.readers.readers);
			}
		}

		assert(autoTimers.readers.streamCount);

		if(!--autoTimers.readers.streamCount){
			#if !PLATFORM_CONSOLE
				ResetEvent(autoTimers.processes.eventHasReaders);
			#endif

			autoTimerDisable();
			assert(!eaSize(&autoTimers.readers.readers));
			resetAllThreads = 1;
		}

		ruListVerify(&r->ruList);

		{
			ReaderUpdate* ruPrev = NULL;
			ReaderUpdate* ru;
			
			for(ru = r->ruList.head; ru;){
				ReaderUpdate*		ruNext = ru->next;
				SharedReaderUpdate*	sru = ru->sru;
				S32					keep = 0;
				
				EARRAY_CONST_FOREACH_BEGIN(r->streams, i, isize);
					AutoTimerReaderStream* s = r->streams[i];
					
					if(	sru->flags.sendToAll
						||
						s->instanceID > sru->oldReaderInstanceID &&
						s->instanceID <= sru->newReaderInstanceID)
					{
						keep = 1;
						break;
					}
				EARRAY_FOREACH_END;
				
				if(!keep){
					U32 bytes;
					
					assert(r->ruList.count);
					
					r->ruList.count--;
					
					fbGetSizeAsBytes(ru->sru->fb, &bytes);
					
					assert(r->ruList.bytes >= bytes);
					r->ruList.bytes -= bytes;

					if(!--sru->refCount){
						fbDestroy(&sru->fb);
						SAFE_FREE(sru);
					}
					
					if(ruPrev){
						ruPrev->next = ruNext;
					}else{
						r->ruList.head = ruNext;
					}
					
					if(!ruNext){
						assert(r->ruList.tail == ru);
						
						r->ruList.tail = ruPrev;

						if(!ruPrev){
							assert(!r->ruList.count);
						}
					}
					
					SAFE_FREE(ru);
				}else{
					ruPrev = ru;
				}

				ru = ruNext;
			}
		}
		
		#if VERIFY_READER_UPDATE_LIST
		{
			EARRAY_CONST_FOREACH_BEGIN(autoTimers.readers.readers, i, isize);
				AutoTimerReader* rCheck = autoTimers.readers.readers[i];
				
				ruListVerify(&rCheck->ruList);
			EARRAY_FOREACH_END;
		}
		#endif

		autoTimerReaderUpdateMinMaxInstanceID(r);
		autoTimerReadersUpdateMinInstanceID();
	}
	autoTimerRecursionDisableDec(THREAD_DATA_NO_FAIL);
	modifySequencingLeaveCS();
	writeUnlockU32(&autoTimers.readers.lockStreamCount);
	
	if(resetAllThreads){
		TimerThreadData* tdCur;
		
		readLockU32(&autoTimers.threadList.lock);
		
		// The order of these interlocked increments is important. Any changes here
		// may necessitate changes to all call sites of autoTimerResetStack.
		for(tdCur = autoTimers.threadList.head; tdCur; tdCur = tdCur->nextThread){
			InterlockedIncrement(&tdCur->special.resetOnNextCall);
			InterlockedIncrement(&tdCur->special.enabledCount);
		}
		
		readUnlockU32(&autoTimers.threadList.lock);
	}
	
	SAFE_FREE(*streamInOut);
}

void autoTimerReaderStreamGetUserPointer(	AutoTimerReaderStream* stream,
											void** userPointerOut)
{
	if(	!stream ||
		!userPointerOut)
	{
		return;
	}
	
	*userPointerOut = stream->userPointer;
}

void autoTimerReaderRead(AutoTimerReader* r){
	if(!r){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	// Reset the atomic notification flags, since we're checking right now.

	InterlockedExchange(&r->atomicNotifyFlags, 0);

	while(1){
		ReaderUpdate*			ru;
		U32						rusRemaining;
		AutoTimerReaderMsg		msg = {0};
		AutoTimerReaderMsgOut	out = {0};

		modifySequencingEnterCS();
		{
			U32 bytes;
			
			ru = r->ruList.head;
			
			if(ru){
				PERFINFO_AUTO_START("remove from list", 1);
				
				// Remove it from the list.

				fbGetSizeAsBytes(ru->sru->fb, &bytes);
				
				r->ruList.head = ru->next;
				if(!r->ruList.head){
					assert(r->ruList.tail == ru);
					assert(r->ruList.count == 1);
					r->ruList.tail = NULL;
				}

				assert(r->ruList.bytes >= bytes);
				r->ruList.bytes -= bytes;

				assert(r->ruList.count);
				r->ruList.count--;
				
				PERFINFO_AUTO_STOP();
			}

			rusRemaining = r->ruList.count;
		}
		modifySequencingLeaveCS();

		if(!ru){
			// Ran out of updates, flag as needing notification.
			
			if(	_InterlockedOr(&r->atomicNotifyFlags, ATR_ATOMIC_FLAG_WAITING_FOR_NOTIFY) &
				ATR_ATOMIC_FLAG_NEW_BUFFER_AVAILABLE)
			{
				InterlockedExchange(&r->atomicNotifyFlags, 0);

				// Something was just added, so loop and get it.
				
				ADD_MISC_COUNT(1, "found new");

				continue;
			}

			PERFINFO_AUTO_START("msg:waitForBufferAvailable", 1);
			{
				msg.msgType = ATR_MSG_WAIT_FOR_BUFFER_AVAILABLE;
				msg.userPointer = r->userPointer;
			
				r->msgHandler(&msg);
			}
			PERFINFO_AUTO_STOP();
			
			break;
		}

		PERFINFO_AUTO_START("sendMsg", 1);
		{
			msg.out = &out;
			
			msg.msgType = ATR_MSG_NEW_BUFFER;
			msg.userPointer = r->userPointer;
			
			msg.newBuffer.fb = ru->sru->fb;
			msg.newBuffer.updatesRemaining = rusRemaining;
			msg.newBuffer.streams = r->newBufferData.streams;
			msg.newBuffer.streamUserPointers = r->newBufferData.streamUserPointers;
			
			EARRAY_CONST_FOREACH_BEGIN(r->streams, i, isize);
				AutoTimerReaderStream* s = r->streams[i];
				
				if(	ru->sru->flags.sendToAll
					||
					s->instanceID > ru->sru->oldReaderInstanceID &&
					s->instanceID <= ru->sru->newReaderInstanceID)
				{
					msg.newBuffer.streams[msg.newBuffer.streamCount] = s;
					msg.newBuffer.streamUserPointers[msg.newBuffer.streamCount] = s->userPointer;
					msg.newBuffer.streamCount++;
				}
			EARRAY_FOREACH_END;

			PERFINFO_AUTO_START("msg:newBuffer", 1);
			r->msgHandler(&msg);
			PERFINFO_AUTO_STOP();
		}
		PERFINFO_AUTO_STOP();
		
		PERFINFO_AUTO_START("free update", 1);
		{
			if(!InterlockedDecrement(&ru->sru->refCount)){
				assert(!ru->sru->refCount);
				fbDestroy(&ru->sru->fb);
				SAFE_FREE(ru->sru);
			}

			SAFE_FREE(ru);
		}
		PERFINFO_AUTO_STOP();
		
		// Check if the user said to stop sending buffers.

		if(out.newBuffer.flags.stopSendingBuffers){
			break;
		}
	}

	PERFINFO_AUTO_STOP();
}

void autoTimerReaderGetBytesRemaining(	AutoTimerReader* r,
										U32* bytesOut)
{
	if(bytesOut){
		*bytesOut = SAFE_MEMBER(r, ruList.bytes);
	}
}

static S32 autoTimerFindTimerByInstanceID(	PerformanceInfo** piOut,
											PerformanceInfo* pi,
											U32 timerInstanceID)
{
	for(; pi; pi = pi->nextSibling){
		if(pi->instanceID == timerInstanceID){
			if(piOut){
				*piOut = pi;
			}
			return 1;
		}

		if(autoTimerFindTimerByInstanceID(	piOut,
											pi->children.head,
											timerInstanceID))
		{
			return 1;
		}
	}

	return 0;
}

static S32 autoTimerFindThreadAndTimer(	TimerThreadData** tdOut,
										PerformanceInfo** piOut,
										U32 threadID,
										U32 startInstanceID,
										U32 timerInstanceID)
{
	TimerThreadData* td;

	for(td = autoTimers.threadList.head;
		td;
		td = td->nextThread)
	{
		if(	td->threadID == threadID &&
			td->startInstanceID == startInstanceID)
		{
			ARRAY_FOREACH_BEGIN(td->roots, i);
				if(autoTimerFindTimerByInstanceID(	piOut,
													td->roots[i].rootList.head,
													timerInstanceID))
				{
					if(tdOut){
						*tdOut = td;
					}

					return 1;
				}
			ARRAY_FOREACH_END;

			return 0;
		}
	}

	return 0;
}

void autoTimerSetTimerBreakPoint(	U32 threadID,
									U32 startInstanceID,
									U32 timerInstanceID,
									S32 enabled)
{
	if(!autoTimers.initialized){
		return;
	}

	threadInstanceEnterCS();
	{
		TimerThreadData* td;
		PerformanceInfo* pi;

		if(autoTimerFindThreadAndTimer(	&td,
										&pi,
										threadID,
										startInstanceID,
										timerInstanceID))
		{
			td->timerFlagsChangedInstanceID++;
			pi->atomicFlags.isBreakpoint = !!enabled;
		}
	}
	threadInstanceLeaveCS();
}

void autoTimerSetTimerForcedOpen(	U32 threadID,
									U32 startInstanceID,
									U32 timerInstanceID,
									S32 enabled)
{
	if(!autoTimers.initialized){
		return;
	}

	threadInstanceEnterCS();
	{
		TimerThreadData* td;
		PerformanceInfo* pi;

		if(autoTimerFindThreadAndTimer(	&td,
										&pi,
										threadID,
										startInstanceID,
										timerInstanceID))
		{
			pi->atomicFlags.forcedOpen = !!enabled;
			pi->atomicFlags.forcedClosed = !enabled;
			td->timerFlagsChangedInstanceID++;
		}
	}
	threadInstanceLeaveCS();
}

//-- Decoder ---------------------------------------------------------------------------------------

typedef struct AutoTimerDecodedTimerInstance {
	AutoTimerDecodedThread*			dt;
	void*							userPointer;

	AutoTimerDecodedTimerInstance*	parent;
	AutoTimerDecodedTimerInstance**	children;

	char*							name;
	U64								timerID;
	U32								instanceID;
	
	struct {
		U32							infoType		: 3;
		U32							isBlocking		: 1;
	} flags;
} AutoTimerDecodedTimerInstance;

typedef struct AutoTimerDecodedTimerRoot {
	AutoTimerDecodedTimerInstance*	dtiRoot;
	AutoTimerDecodedTimerInstance*	dtiCycles;
	AutoTimerDecodedTimerInstance*	dtiUserTicks;
	AutoTimerDecodedTimerInstance*	dtiKernelTicks;
} AutoTimerDecodedTimerRoot;

typedef struct AutoTimerDecodedThread {
	AutoTimerDecoder*				d;
	void*							userPointer;
	
	U32								id;
	char*							name;
	
	U32								frame;
	U32								maxDepth;
	
	AutoTimerDecodedTimerRoot**		roots;
	
	struct {
		U64							cyclesBegin;
		U64							cyclesDelta;
	} prev;

	struct {
		U32							receivedFullUpdate	: 1;
		U32							gotFinalFrame		: 1;
	} flags;
} AutoTimerDecodedThread;

typedef struct AutoTimerDecoder {
	void*							userPointer;
	AutoTimerDecoderMsgHandler		msgHandler;

	AutoTimerDecodedThread**		dts;

	FragmentedBufferReader*			fbr;

	struct {
		U64							cyclesPerSecond;
		U32							countReal;
		U32							countVirtual;
	} cpu;
} AutoTimerDecoder;

void autoTimerDecoderCreate(AutoTimerDecoder** dOut,
							AutoTimerDecoderMsgHandler msgHandler,
							void* userPointer)
{
	AutoTimerDecoder* d;

	if(!dOut){
		return;
	}

	d = callocStruct(AutoTimerDecoder);

	d->msgHandler = msgHandler;
	d->userPointer = userPointer;
	
	fbReaderCreate(&d->fbr);

	*dOut = d;
}

static void autoTimerDecodedTimerInstanceDestroy(AutoTimerDecodedTimerInstance** dtiOut);

static void autoTimerDecodedTimerInstanceClear(AutoTimerDecodedTimerInstance* dti){
	// Clear the children.

	EARRAY_CONST_FOREACH_BEGIN(dti->children, i, isize);
		autoTimerDecodedTimerInstanceDestroy(&dti->children[i]);
	EARRAY_FOREACH_END;
	
	eaDestroy(&dti->children);
}

static void autoTimerDecodedTimerInstanceDestroy(AutoTimerDecodedTimerInstance** dtiOut){
	AutoTimerDecodedTimerInstance* dti = SAFE_DEREF(dtiOut);
	
	if(dti){
		autoTimerDecodedTimerInstanceClear(dti);

		// Notify the owner.

		if(dti->dt->d->msgHandler){
			AutoTimerDecoderMsg msg = {0};

			msg.msgType = AT_DECODER_MSG_DTI_DESTROYED;
			msg.d.d = dti->dt->d;
			msg.d.userPointer = dti->dt->d->userPointer;
			
			msg.dt.dt = dti->dt;
			msg.dt.userPointer = dti->dt->userPointer;
			
			msg.dti.dti = dti;
			msg.dti.userPointer = dti->userPointer;

			dti->dt->d->msgHandler(&msg);
		}
		
		SAFE_FREE(dti->name);
		SAFE_FREE(*dtiOut);
	}
}

void autoTimerDecoderDestroy(AutoTimerDecoder** dInOut){
	AutoTimerDecoder* d = SAFE_DEREF(dInOut);

	if(!d){
		return;
	}
	
	// Destroy all the threads.
	
	EARRAY_CONST_FOREACH_BEGIN(d->dts, i, isize);
		AutoTimerDecodedThread* dt = d->dts[i];
		
		EARRAY_CONST_FOREACH_BEGIN(dt->roots, j, jsize);
			AutoTimerDecodedTimerRoot* r = dt->roots[j];

			autoTimerDecodedTimerInstanceDestroy(&r->dtiRoot);
			SAFE_FREE(dt->roots[j]);
		EARRAY_FOREACH_END;
		
		eaDestroy(&dt->roots);
	
		if(d->msgHandler){
			AutoTimerDecoderMsg msg = {0};

			msg.msgType = AT_DECODER_MSG_DT_DESTROYED;
			msg.d.d = d;
			msg.d.userPointer = d->userPointer;
			
			msg.dt.dt = dt;
			msg.dt.userPointer = dt->userPointer;
			
			d->msgHandler(&msg);
		}
		
		SAFE_FREE(dt->name);
		SAFE_FREE(d->dts[i]);
	EARRAY_FOREACH_END;
	
	eaDestroy(&d->dts);
	
	fbReaderDestroy(&d->fbr);
	
	// Done.
	
	SAFE_FREE(*dInOut);
}

static void autoTimerDecoderStateWriteTimer(AutoTimerDecodedTimerInstance* dti,
											FragmentedBuffer* fb)
{
	fbWriteString(fb, dti->name);
	fbWriteU64(fb, 64, dti->timerID);
	fbWriteU32(fb, 32, dti->instanceID);
	fbWriteU32(fb, 3, dti->flags.infoType);

	if(dti->flags.infoType == PERFINFO_TYPE_CPU){
		fbWriteBit(fb, dti->flags.isBlocking);
	}
	
	EARRAY_CONST_FOREACH_BEGIN(dti->children, i, isize);
		AutoTimerDecodedTimerInstance* dtiChild = dti->children[i];

		fbWriteBit(fb, 1);
		autoTimerDecoderStateWriteTimer(dtiChild, fb);
	EARRAY_FOREACH_END;
	
	fbWriteBit(fb, 0);
}

static S32 autoTimerDecoderStateWrite(	AutoTimerDecoder* d,
										FragmentedBuffer* fb)
{
	// Write threads.
	
	EARRAY_CONST_FOREACH_BEGIN(d->dts, i, isize);
		AutoTimerDecodedThread* dt = d->dts[i];
		
		fbWriteBit(fb, 1);
		fbWriteU32(fb, 32, dt->id);
		fbWriteString(fb, dt->name);
		fbWriteBit(fb, dt->flags.receivedFullUpdate);
		fbWriteU32(fb, 32, dt->maxDepth);
		
		// Write timer roots.
		
		EARRAY_CONST_FOREACH_BEGIN(dt->roots, j, jsize);
			fbWriteBit(fb, 1);
			autoTimerDecoderStateWriteTimer(dt->roots[j]->dtiRoot, fb);
		EARRAY_FOREACH_END;
		
		fbWriteBit(fb, 0);
	EARRAY_FOREACH_END;

	fbWriteBit(fb, 0);
	
	return 1;
}

static void autoTimerSendMsgDecodedTimerInstanceCreated(AutoTimerDecoder* d,
														AutoTimerDecodedThread* dt,
														AutoTimerDecodedTimerInstance* dti)
{
	if(d->msgHandler){
		AutoTimerDecoderMsg msg = {0};

		msg.msgType = AT_DECODER_MSG_DTI_CREATED;
		msg.d.d = d;
		msg.d.userPointer = d->userPointer;

		msg.dt.dt = dt;
		msg.dt.userPointer = dt->userPointer;

		msg.dti.dti = dti;

		msg.dtiCreated.name = dti->name;
		msg.dtiCreated.instanceID = dti->instanceID;
		msg.dtiCreated.timerID = dti->timerID;
		msg.dtiCreated.infoType = dti->flags.infoType;
		msg.dtiCreated.flags.isBlocking = dti->flags.isBlocking;

		d->msgHandler(&msg);
	}
}

static S32 autoTimerDecoderStateReadTimer(	AutoTimerDecoder* d,
											AutoTimerDecodedThread* dt,
											FragmentedBufferReader* fbr,
											AutoTimerDecodedTimerInstance* dti)
{
	char	name[100];
	U32		infoType;
	S32		isBlocking;
	
	dti->dt = dt;
	
	if(	!fbReadString(fbr, SAFESTR(name)) ||
		!fbReadU64(fbr, 64, &dti->timerID) ||
		!fbReadU32(fbr, 32, &dti->instanceID) ||
		!fbReadU32(fbr, 3, &infoType))
	{
		return 0;
	}

	dti->flags.infoType = infoType;
	
	if(infoType == PERFINFO_TYPE_CPU){
		if(!fbReadBit(fbr, &isBlocking)){
			return 0;
		}

		dti->flags.isBlocking = isBlocking;
	}
	
	dti->name = strdup(name);
	
	autoTimerSendMsgDecodedTimerInstanceCreated(d,
												dt,
												dti);

	while(1){
		S32								hasAnotherChild;
		AutoTimerDecodedTimerInstance*	dtiChild;
		
		if(!fbReadBit(fbr, &hasAnotherChild)){
			return 0;
		}
		
		if(!hasAnotherChild){
			break;
		}
		
		dtiChild = callocStruct(AutoTimerDecodedTimerInstance);
		eaPush(&dti->children, dtiChild);
		
		dtiChild->parent = dti;
		
		if(!autoTimerDecoderStateReadTimer(d, dt, fbr, dtiChild)){
			return 0;
		}
	}
	
	return 1;
}

static S32 autoTimerDecoderFindThreadByID(	AutoTimerDecoder* d,
											U32 threadID,
											AutoTimerDecodedThread** dtOut)
{
	EARRAY_CONST_FOREACH_BEGIN(d->dts, i, isize);
	{
		AutoTimerDecodedThread* dt = d->dts[i];

		if(	dt->id == threadID &&
			!dt->flags.gotFinalFrame)
		{
			*dtOut = d->dts[i];
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

static void autoTimerDecodedThreadCreate(	AutoTimerDecoder* d,
											AutoTimerDecodedThread** dtOut,
											U32 threadID)
{
	AutoTimerDecodedThread*	dt;

	dt = callocStruct(AutoTimerDecodedThread);

	dt->d = d;
	dt->id = threadID;

	eaPush(&d->dts, dt);

	if(d->msgHandler){
		AutoTimerDecoderMsg msg = {0};

		msg.msgType = AT_DECODER_MSG_DT_CREATED;
		msg.d.d = d;
		msg.d.userPointer = d->userPointer;

		msg.dt.dt = dt;

		msg.dtCreated.id = threadID;

		d->msgHandler(&msg);
	}

	*dtOut = dt;
}

static void autoTimerDecodedThreadSendMsgNamed(	AutoTimerDecodedThread* dt,
												const char* name)
{
	AutoTimerDecoder* d = dt->d;
	
	if(	!dt->name &&
		SAFE_DEREF(name))
	{
		dt->name = strdup(name);
	
		if(d->msgHandler){
			AutoTimerDecoderMsg msg = {0};

			msg.msgType = AT_DECODER_MSG_DT_NAMED;
			msg.d.d = d;
			msg.d.userPointer = d->userPointer;

			msg.dt.dt = dt;
			msg.dt.userPointer = dt->userPointer;

			msg.dtNamed.name = dt->name;

			d->msgHandler(&msg);
		}
	}
}

static void autoTimerDecodedThreadSendMsgMaxDepth(	AutoTimerDecoder* d,
													AutoTimerDecodedThread* dt)
{
	if(d->msgHandler){
		AutoTimerDecoderMsg msg = {0};

		msg.msgType = AT_DECODER_MSG_DT_MAX_DEPTH_UPDATE;
		msg.d.d = d;
		msg.d.userPointer = d->userPointer;

		msg.dt.dt = dt;
		msg.dt.userPointer = dt->userPointer;
		
		msg.dtMaxDepthUpdate.maxDepth = dt->maxDepth;

		d->msgHandler(&msg);
	}
}

static S32 autoTimerDecoderStateRead(	AutoTimerDecoder* d,
										FragmentedBuffer* fb,
										U32 version)
{
	FragmentedBufferReader* fbr = d->fbr;
	
	fbReaderAttach(fbr, fb, 0);
	
	while(1){
		S32 hasThread;
		
		if(!fbReadBit(fbr, &hasThread)){
			return 0;
		}
		
		if(!hasThread){
			break;
		}else{
			U32						id;
			char					name[100];
			AutoTimerDecodedThread*	dt;
			S32						receivedFullUpdate;
			U32						maxDepth = U32_MAX;

			if(	!fbReadU32(fbr, 32, &id) ||
				!fbReadString(fbr, SAFESTR(name)) ||
				!fbReadBit(fbr, &receivedFullUpdate))
			{
				return 0;
			}
			
			if(version >= 1){
				if(!fbReadU32(fbr, 32, &maxDepth)){
					return 0;
				}
			}
			
			autoTimerDecodedThreadCreate(d, &dt, id);
			dt->flags.receivedFullUpdate = receivedFullUpdate;
			dt->maxDepth = maxDepth;
			
			autoTimerDecodedThreadSendMsgNamed(dt, name);
			autoTimerDecodedThreadSendMsgMaxDepth(d, dt);

			while(1){
				S32							hasRoot;
				AutoTimerDecodedTimerRoot*	r;
				
				if(!fbReadBit(fbr, &hasRoot)){
					return 0;
				}
				
				if(!hasRoot){
					break;
				}
				
				r = callocStruct(AutoTimerDecodedTimerRoot);
				eaPush(&dt->roots, r);

				r->dtiRoot = callocStruct(AutoTimerDecodedTimerInstance);
				r->dtiRoot->dt = dt;
				
				if(!autoTimerDecoderStateReadTimer(d, dt, fbr, r->dtiRoot)){
					return 0;
				}
			}
		}
	}
	
	return 1;
}

static void autoTimerDecoderSendMsgTimerInstanceFrameUpdate(AutoTimerDecoder* d,
															AutoTimerDecodedThread* dt,
															AutoTimerDecodedTimerInstance* dti,
															U32 count,
															U64 cyclesActive,
															U64 cyclesBlocking,
															U64 cyclesActiveChildren)
{
	if(d->msgHandler){
		AutoTimerDecoderMsg msg = {0};

		msg.msgType = AT_DECODER_MSG_DTI_FRAME_UPDATE;
		msg.d.d = d;
		msg.d.userPointer = d->userPointer;

		msg.dt.dt = dt;
		msg.dt.userPointer = dt->userPointer;

		msg.dti.dti = dti;
		msg.dti.userPointer = dti->userPointer;

		msg.dtiFrameUpdate.count = count;
		msg.dtiFrameUpdate.cyclesActive = cyclesActive;
		msg.dtiFrameUpdate.cyclesBlocking = cyclesBlocking;
		msg.dtiFrameUpdate.cyclesActiveChildren = cyclesActiveChildren;
		
		PERFINFO_AUTO_START("msg:DTI_FRAME_UPDATE", 1);
		{
			d->msgHandler(&msg);
		}
		PERFINFO_AUTO_STOP();
	}
}

static S32 autoTimerDecoderReadSiblingList(	AutoTimerDecoder* d,
											AutoTimerDecodedThread* dt,
											AutoTimerDecodedTimerInstance* dti,
											FragmentedBufferReader* fbr,
											const S32 hasNewChildInTree,
											const U32 depth,
											U64* cyclesOut,
											U64* cyclesBlockingOut);

static S32 autoTimerDecodedTimerInstanceReadUpdate(	AutoTimerDecoder* d,
													AutoTimerDecodedThread* dt,
													AutoTimerDecodedTimerInstance* dti,
													const S32 hasNewChildInTree,
													const U32 depth,
													FragmentedBufferReader* fbr,
													U64* cyclesActiveOut,
													U64* cyclesBlockingOut)
{
	U32 count;
	U64 cyclesActive;
	U64 cyclesBlocking;
	U64 cyclesChildActive;
	U64 cyclesChildBlocking;

	READ_CHECK_STRING(fbr, "sibling");

	if(	!autoTimerReadCount(fbr, &count) ||
		!autoTimerReadCycles(fbr, &cyclesActive))
	{
		return 0;
	}

	if(!autoTimerDecoderReadSiblingList(d,
										dt,
										dti,
										fbr,
										hasNewChildInTree,
										depth + 1,
										&cyclesChildActive,
										&cyclesChildBlocking))
	{
		return 0;
	}
	
	if(dti->flags.isBlocking){
		cyclesBlocking = cyclesActive - cyclesChildActive;
		cyclesActive = cyclesChildActive;
		*cyclesActiveOut += cyclesActive;
	}
	else if(dti->flags.infoType == PERFINFO_TYPE_CPU){
		cyclesActive -= cyclesChildBlocking;
		cyclesBlocking = cyclesChildBlocking;
		*cyclesActiveOut += cyclesActive;
	}else{
		cyclesBlocking = cyclesChildBlocking;
		*cyclesActiveOut += cyclesChildActive;
	}

	*cyclesBlockingOut += cyclesBlocking;

	if(count){
		autoTimerDecoderSendMsgTimerInstanceFrameUpdate(d,
														dt,
														dti,
														count,
														cyclesActive,
														cyclesBlocking,
														cyclesChildActive);
	}
	
	return 1;
}

static void autoTimerDecodedTimerInstanceCreate(AutoTimerDecodedThread* dt,
												AutoTimerDecodedTimerInstance* dtiParent,
												AutoTimerDecodedTimerInstance** dtiChildOut,
												const char* name,
												PerformanceInfoType infoType,
												S32 isBlocking)
{
	AutoTimerDecodedTimerInstance* dtiChild;
	
	dtiChild = *dtiChildOut = callocStruct(AutoTimerDecodedTimerInstance);

	dtiChild->dt = dt;

	dtiChild->parent = dtiParent;
	eaPush(&dtiParent->children, dtiChild);

	dtiChild->name = strdup(name);

	dtiChild->flags.infoType = infoType;
	
	if(infoType == PERFINFO_TYPE_CPU){
		dtiChild->flags.isBlocking = isBlocking;
	}
}

static S32 autoTimerDecoderReadSiblingList(	AutoTimerDecoder* d,
											AutoTimerDecodedThread* dt,
											AutoTimerDecodedTimerInstance* dti,
											FragmentedBufferReader* fbr,
											const S32 hasNewChildInTree,
											const U32 depth,
											U64* cyclesOut,
											U64* cyclesBlockingOut)
{
	U64 cyclesTotal = 0;
	U64 cyclesBlockingTotal = 0;

	// Read existing children.

	EARRAY_CONST_FOREACH_BEGIN(dti->children, i, isize);
		AutoTimerDecodedTimerInstance*	dtiChild = dti->children[i];
		U32								hasUpdate;
		
		#if 0
		{
			FOR_BEGIN(i, (S32)depth);
				printf("  ");
			FOR_END;

			printf(	"- %s (%"FORM_LL"u)\n",
					pi->locName,
					pi->currentFrame.cyclesTotal);
		}
		#endif

		if(!fbReadBit(fbr, &hasUpdate)){
			return 0;
		}
		
		if(hasUpdate){
			if(!autoTimerDecodedTimerInstanceReadUpdate(d,
														dt,
														dtiChild,
														hasNewChildInTree,
														depth,
														fbr,
														&cyclesTotal,
														&cyclesBlockingTotal))
			{
				return 0;
			}
		}
	EARRAY_FOREACH_END;

	// Read new children.

	if(hasNewChildInTree){
		S32 readNewChild;

		while(	fbReadBit(fbr, &readNewChild) &&
				readNewChild)
		{
			AutoTimerDecodedTimerInstance*	dtiChild;
			char							name[100];
			U64								timerID;
			U32								infoType;
			S32								isBlocking = 0;
			
			fbReadString(fbr, SAFESTR(name));
			fbReadU32(fbr, 3, &infoType);

			if(infoType == PERFINFO_TYPE_CPU){
				fbReadBit(fbr, &isBlocking);
			}
			
			autoTimerDecodedTimerInstanceCreate(dt,
												dti,
												&dtiChild,
												name,
												infoType,
												isBlocking);

			fbReadU64(fbr, 64, &timerID);
			dtiChild->timerID = timerID;
			
			autoTimerReadCount(fbr, &dtiChild->instanceID);

			autoTimerSendMsgDecodedTimerInstanceCreated(d, dt, dtiChild);

			if(!autoTimerDecodedTimerInstanceReadUpdate(d,
														dt,
														dtiChild,
														hasNewChildInTree,
														depth,
														fbr,
														&cyclesTotal,
														&cyclesBlockingTotal))
			{
				return 0;
			}
		}
	}
	
	if(cyclesOut){
		*cyclesOut = cyclesTotal;
	}
	
	if(cyclesBlockingOut){
		*cyclesBlockingOut = cyclesBlockingTotal;
	}

	return 1;
}

static S32 autoTimerDecoderDecodeFrameRootSiblings(	AutoTimerDecoder* d,
													AutoTimerDecodedThread* dt,
													FragmentedBufferReader* fbr,
													const U32 version,
													S32 isFullUpdate,
													S32 hasNewChildInTree)
{
	S32 hasAnotherRoot = 0;
	S32 rootIndex;
	S32 firstRoot = 1;

	READ_CHECK_STRING(fbr, "siblings");

	for(fbReadBit(fbr, &hasAnotherRoot), rootIndex = 0;
		hasAnotherRoot;
		fbReadBit(fbr, &hasAnotherRoot), rootIndex++)
	{
		AutoTimerDecodedTimerRoot*	r;
		S32 						hasFrameUpdate;
		U64							cyclesBegin = 0;
		U64							cyclesDelta = 0;
		U64							cyclesBlocking = 0;
		U64							osCycles = 0;
		U64							osTicksUser = 0;
		U64							osTicksKernel = 0;
		
		// Create the root.
		
		while(rootIndex >= eaSize(&dt->roots)){
			r = callocStruct(AutoTimerDecodedTimerRoot);
			eaPush(&dt->roots, r);
		}
		
		r = dt->roots[rootIndex];
		
		if(!r->dtiRoot){
			r->dtiRoot = callocStruct(AutoTimerDecodedTimerInstance);
			r->dtiRoot->dt = dt;
		}
		
		// Read the frame time.
		
		if(!fbReadBit(fbr, &hasFrameUpdate)){
			return 0;
		}

		if(hasFrameUpdate){
			// Read frame cycles.

			if(	!fbReadU64(fbr, 64, &cyclesBegin) ||
				!autoTimerReadCycles(fbr, &cyclesDelta))
			{
				return 0;
			}
			
			if(	dt->prev.cyclesBegin &&
				(S64)(cyclesBegin - (dt->prev.cyclesBegin + dt->prev.cyclesDelta)) < 0)
			{
				cyclesBegin = dt->prev.cyclesBegin + dt->prev.cyclesDelta;
			}
			
			if((S64)cyclesDelta < 0){
				cyclesDelta = (U64)2 * CUBE((U64)1000);
			}
			//else if(cyclesDelta > (U64)20 * CUBE((U64)1000)){
			//	cyclesDelta = (U64)20 * CUBE((U64)1000);
			//}
			
			dt->prev.cyclesBegin = cyclesBegin;
			dt->prev.cyclesDelta = cyclesDelta;
		
			// Read OS data.
		
			if(version >= 1){
				S32 hasOSValue;

				if(	!fbReadBit(fbr, &hasOSValue)
					||
					hasOSValue &&
					!autoTimerReadCycles(fbr, &osCycles))
				{
					return 0;
				}

				if(	!fbReadBit(fbr, &hasOSValue)
					||
					hasOSValue &&
					!autoTimerReadCycles(fbr, &osTicksUser))
				{
					return 0;
				}
				
				if(	hasOSValue &&
					d->cpu.cyclesPerSecond)
				{
					osTicksUser *= d->cpu.cyclesPerSecond / (10 * SQR(1000));
				}
				
				if(	!fbReadBit(fbr, &hasOSValue)
					||
					hasOSValue &&
					!autoTimerReadCycles(fbr, &osTicksKernel))
				{
					return 0;
				}

				if(	hasOSValue &&
					d->cpu.cyclesPerSecond)
				{
					osTicksKernel *= d->cpu.cyclesPerSecond / (10 * SQR(1000));
				}
			}
		}

		if(isFullUpdate){
			char name[100]; 

			fbReadString(fbr, SAFESTR(name));
			r->dtiRoot->name = strdup(name);
			
			autoTimerSendMsgDecodedTimerInstanceCreated(d, dt, r->dtiRoot);
		}
		
		if(hasNewChildInTree){
			PERFINFO_AUTO_START("readSiblingList(newChild)", 1);
		}else{
			PERFINFO_AUTO_START("readSiblingList", 1);
		}

		if(!autoTimerDecoderReadSiblingList(d,
											dt,
											r->dtiRoot,
											fbr,
											hasNewChildInTree,
											0,
											NULL,
											&cyclesBlocking))
		{
			PERFINFO_AUTO_STOP();
			return 0;
		}
		
		if(cyclesBlocking > cyclesDelta){
			cyclesBlocking = cyclesDelta;
		}
		
		PERFINFO_AUTO_STOP();

		if(hasFrameUpdate){
			autoTimerDecoderSendMsgTimerInstanceFrameUpdate(d,
															dt,
															r->dtiRoot,
															1,
															cyclesDelta - cyclesBlocking,
															cyclesBlocking,
															0);

			if(d->msgHandler){
				AutoTimerDecoderMsg msg = {0};

				msg.msgType = AT_DECODER_MSG_DT_FRAME_UPDATE;
				msg.d.d = d;
				msg.d.userPointer = d->userPointer;

				msg.dt.dt = dt;
				msg.dt.userPointer = dt->userPointer;

				msg.dtFrameUpdate.cycles.begin = cyclesBegin;
				msg.dtFrameUpdate.cycles.active = cyclesDelta - cyclesBlocking;
				msg.dtFrameUpdate.cycles.blocking = cyclesBlocking;
				
				msg.dtFrameUpdate.os.cycles = osCycles;
				msg.dtFrameUpdate.os.ticks.user = osTicksUser;
				msg.dtFrameUpdate.os.ticks.kernel = osTicksKernel;
				
				if(TRUE_THEN_RESET(firstRoot)){
					fbReaderGetSizeAsBytes(fbr, &msg.dtFrameUpdate.bytesReceived);
				}

				d->msgHandler(&msg);
			}
		}
	}
	
	return 1;
}

static S32 autoTimerDecoderDecodeFrameFlags(AutoTimerDecoder* d,
											AutoTimerDecodedThread* dt,
											FragmentedBufferReader* fbr)
{
	S32 hasUpdate;

	READ_CHECK_STRING(fbr, "flags");

	// Read flags and stuff.
	
	if(!fbReadBit(fbr, &hasUpdate)){
		return 0;
	}
	
	if(hasUpdate){
		S32 hasName;
		S32 hasFlagUpdate;
		S32 hasMaxDepthUpdate;
		S32 isFinalFrame;
	
		if(!fbReadBit(fbr, &hasName)){
			return 0;
		}
		
		if(hasName){
			char name[100];

			if(!fbReadString(fbr, SAFESTR(name))){
				return 0;
			}

			autoTimerDecodedThreadSendMsgNamed(dt, name);
		}

		if(!fbReadBit(fbr, &hasFlagUpdate)){
			return 0;
		}
		
		if(hasFlagUpdate){
			while(1){
				S32 hasOneMoreUpdate;
				U32 instanceID;
				S32 isBreakpoint;
				S32 forcedOpen;
				S32 forcedClosed;
				
				if(!fbReadBit(fbr, &hasOneMoreUpdate)){
					return 0;
				}
				
				if(!hasOneMoreUpdate){
					break;
				}

				fbReadU32(fbr, 32, &instanceID);
				fbReadBit(fbr, &isBreakpoint);
				fbReadBit(fbr, &forcedOpen);
				fbReadBit(fbr, &forcedClosed);
				
				if(d->msgHandler){
					AutoTimerDecoderMsg msg = {0};

					msg.msgType = AT_DECODER_MSG_DTI_FLAGS_UPDATE;
					msg.d.d = d;
					msg.d.userPointer = d->userPointer;

					msg.dt.dt = dt;
					msg.dt.userPointer = dt->userPointer;
					
					msg.dtiFlagsUpdate.instanceID = instanceID;
					msg.dtiFlagsUpdate.flags.isBreakpoint = isBreakpoint;
					msg.dtiFlagsUpdate.flags.forcedOpen = forcedOpen;
					msg.dtiFlagsUpdate.flags.forcedClosed = forcedClosed;

					d->msgHandler(&msg);
				}
			}
		}

		if(!fbReadBit(fbr, &hasMaxDepthUpdate)){
			return 0;
		}
		
		if(hasMaxDepthUpdate){
			if(!fbReadU32(fbr, 32, &dt->maxDepth)){
				return 0;
			}

			autoTimerDecodedThreadSendMsgMaxDepth(d, dt);
		}
		
		if(!fbReadBit(fbr, &isFinalFrame)){
			return 0;
		}
		
		if(isFinalFrame){
			dt->flags.gotFinalFrame = 1;

			if(d->msgHandler){
				AutoTimerDecoderMsg msg = {0};

				msg.msgType = AT_DECODER_MSG_DT_DESTROYED;
				msg.d.d = d;
				msg.d.userPointer = d->userPointer;

				msg.dt.dt = dt;
				msg.dt.userPointer = dt->userPointer;
				
				d->msgHandler(&msg);
			}
		}
	}
	
	return 1;
}

static S32 autoTimerDecoderDecodeFrame(	AutoTimerDecoder* d,
										FragmentedBufferReader* fbr,
										const U32 version)
{
	U32 					threadID;
	S32 					isFullUpdate = 0;
	AutoTimerDecodedThread* dt = NULL;
	S32						hasNewChildInTree;
	
	if(!d){
		return 0;
	}

	if(!fbReadU32(fbr, 32, &threadID)){
		return 0;
	}
	
	if(autoTimerDecoderFindThreadByID(d, threadID, &dt)){
		if(FALSE_THEN_SET(dt->flags.receivedFullUpdate)){
			isFullUpdate = 1;
		}
	}else{
		isFullUpdate = 1;
		
		autoTimerDecodedThreadCreate(	d,
										&dt,
										threadID);

		dt->flags.receivedFullUpdate = 1;
	}
	
	dt->frame++;
	
	// Read the new child flag.

	if(!isFullUpdate){
		READ_CHECK_STRING(fbr, "diff");
		
		if(!fbReadBit(fbr, &hasNewChildInTree)){
			return 0;
		}
	}else{
		READ_CHECK_STRING(fbr, "full");
		
		hasNewChildInTree = 1;
	}

	if(!autoTimerDecoderDecodeFrameRootSiblings(d,	
												dt,
												fbr,
												version,
												isFullUpdate,
												hasNewChildInTree))
	{
		return 0;
	}

	if(!autoTimerDecoderDecodeFrameFlags(d, dt, fbr)){
		return 0;
	}
	
	{
		char endString[100];
		
		if(!fbReadString(fbr, SAFESTR(endString))){
			return 1;
		}
		
		if(strcmp(endString, "FrameEnd")){
			if(d->msgHandler){
				AutoTimerDecoderMsg msg = {0};
				char				errorText[200];

				msg.msgType = AT_DECODER_MSG_DT_ERROR;
				msg.d.d = d;
				msg.d.userPointer = d->userPointer;

				msg.dt.dt = dt;
				msg.dt.userPointer = dt->userPointer;

				sprintf(errorText,
						"Expecting end tag \"FrameEnd\" but found \"%s\".",
						endString);
						
				msg.dtError.errorText = errorText;

				d->msgHandler(&msg);
			}
		}
	}

	return 1;
}

static S32 autoTimerDecoderDecodeThreads(	AutoTimerDecoder* d,
											FragmentedBufferReader* fbr)
{
	while(1){
		AutoTimerDecodedThread* dt;
		S32						hasAnotherThread;
		U32						threadID;
		char					threadName[100];
		
		if(!fbReadBit(fbr, &hasAnotherThread)){
			return 0;
		}

		if(!hasAnotherThread){
			break;
		}
		
		if(!fbReadU32(fbr, 32, &threadID)){
			return 0;
		}
		
		if(!fbReadString(fbr, SAFESTR(threadName))){
			return 0;
		}
		
		if(!autoTimerDecoderFindThreadByID(d, threadID, &dt)){
			autoTimerDecodedThreadCreate(d, &dt, threadID);
		}

		autoTimerDecodedThreadSendMsgNamed(dt, threadName);
	}
	
	return 1;
}

static S32 autoTimerDecoderDecodeSystemInfo(AutoTimerDecoder* d,
											FragmentedBufferReader* fbr)
{
	AutoTimerDecoderMsg msg = {0};
	S32					hasMore;
	
	msg.msgType = AT_DECODER_MSG_SYSTEM_INFO;

	msg.d.d = d;
	msg.d.userPointer = d->userPointer;
	
	while(	fbReadBit(fbr, &hasMore) &&
			hasMore)
	{
		char name[100];

		name[0] = 0;
		
		if(!fbReadString(fbr, SAFESTR(name))){
			return 0;
		}
		
		if(!stricmp(name, "CPUCyclesPerSecond")){
			if(!fbReadU64(fbr, 64, &msg.systemInfo.cpu.cyclesPerSecond)){
				return 0;
			}
		}
		else if(!stricmp(name, "CPUCountReal")){
			if(!fbReadU32(fbr, 32, &msg.systemInfo.cpu.countReal)){
				return 0;
			}
		}
		else if(!stricmp(name, "CPUCountVirtual")){
			if(!fbReadU32(fbr, 32, &msg.systemInfo.cpu.countVirtual)){
				return 0;
			}
		}
	}

	d->cpu.cyclesPerSecond = msg.systemInfo.cpu.cyclesPerSecond;
	d->cpu.countReal = msg.systemInfo.cpu.countReal;
	d->cpu.countVirtual = msg.systemInfo.cpu.countVirtual;

	d->msgHandler(&msg);
	
	return 1;
}

static S32 autoTimerDecoderDecodeScanFrame(	AutoTimerDecoder* d,
											FragmentedBufferReader* fbr)
{
	
	while(1){
		AutoTimerDecodedThread* dt;
		AutoTimerDecoderMsg		msg = {0};
		U32						threadID;

		if(!fbReadU32(fbr, 32, &threadID)){
			return 0;
		}
			
		if(!threadID){
			break;
		}

		if(!autoTimerDecoderFindThreadByID(d, threadID, &dt)){
			autoTimerDecodedThreadCreate(d, &dt, threadID);
		}

		if(	!fbReadU64(fbr, 64, &msg.dtScanFrame.cycles.begin) ||
			!fbReadU64(fbr, 64, &msg.dtScanFrame.cycles.total) ||
			!fbReadU64(fbr, 64, &msg.dtScanFrame.os.cycles) ||
			!fbReadU64(fbr, 64, &msg.dtScanFrame.os.ticks.user) ||
			!fbReadU64(fbr, 64, &msg.dtScanFrame.os.ticks.kernel))
		{
			return 0;
		}
		
		msg.msgType = AT_DECODER_MSG_DT_SCAN_FRAME;
		
		msg.d.d = d;
		msg.d.userPointer = d->userPointer;
		
		msg.dt.dt = dt;
		msg.dt.userPointer = dt->userPointer;
		
		d->msgHandler(&msg);
	}
	
	return 1;
}

S32 autoTimerDecoderDecode(	AutoTimerDecoder* d,
							FragmentedBuffer* fb)
{
	FragmentedBufferReader*	fbr = d->fbr;
	S32						retVal = 0;

	PERFINFO_AUTO_START_FUNC();
	{
		char typeName[100];
		
		fbReaderAttach(fbr, fb, 0);

		if(!fbReadString(fbr, SAFESTR(typeName))){
			retVal = 0;
		}
		else if(!stricmp(typeName, "Frame1")){
			PERFINFO_AUTO_START("autoTimerDecoderDecodeFrame", 1);
				retVal = autoTimerDecoderDecodeFrame(d, fbr, 1);
			PERFINFO_AUTO_STOP();
		}
		else if(!stricmp(typeName, "Frame")){
			PERFINFO_AUTO_START("autoTimerDecoderDecodeFrame", 1);
				retVal = autoTimerDecoderDecodeFrame(d, fbr, 0);
			PERFINFO_AUTO_STOP();
		}
		else if(!stricmp(typeName, "ScanFrame")){
			retVal = autoTimerDecoderDecodeScanFrame(d, fbr);
		}
		else if(!stricmp(typeName, "FirstInfo")){
			retVal = autoTimerDecoderDecodeThreads(d, fbr);
			retVal &= autoTimerDecoderDecodeSystemInfo(d, fbr);
		}
		else if(!stricmp(typeName, "Threads")){
			retVal = autoTimerDecoderDecodeThreads(d, fbr);
		}

		fbReaderDetach(fbr);
	}
	PERFINFO_AUTO_STOP();

	return retVal;
}

S32 autoTimerDecodedThreadSetUserPointer(	AutoTimerDecodedThread* dt,
											void* userPointer)
{
	if(!dt){
		return 0;
	}

	dt->userPointer = userPointer;

	return 1;
}

S32 autoTimerDecodedThreadGetUserPointer(	AutoTimerDecodedThread* dt,
											void** userPointerOut)
{
	if(	!dt ||
		!userPointerOut)
	{
		return 0;
	}

	*userPointerOut = dt->userPointer;

	return 1;
}

S32	autoTimerDecodedThreadGetID(AutoTimerDecodedThread* dt,
								U32* idOut)
{
	if(	!dt ||
		!idOut)
	{
		return 0;
	}

	*idOut = dt->id;

	return 1;
}

S32 autoTimerDecodedTimerInstanceSetUserPointer(AutoTimerDecodedTimerInstance* dti,
												void* userPointer)
{
	if(!dti){
		return 0;
	}

	dti->userPointer = userPointer;

	return 1;
}

S32	autoTimerDecodedTimerInstanceGetUserPointer(AutoTimerDecodedTimerInstance* dti,
												void** userPointerOut)
{
	if(	!dti ||
		!userPointerOut)
	{
		return 0;
	}

	*userPointerOut = dti->userPointer;

	return 1;
}

S32	autoTimerDecodedTimerInstanceGetParent(	AutoTimerDecodedTimerInstance* dti,
											AutoTimerDecodedTimerInstance** dtiParentOut)
{
	if(	!dti ||
		!dtiParentOut)
	{
		return 0;
	}

	*dtiParentOut = dti->parent;

	return 1;
}

S32	autoTimerDecodedTimerInstanceGetInstanceID(	AutoTimerDecodedTimerInstance* dti,
												U32* instanceIDOut)
{
	if(	!dti ||
		!instanceIDOut)
	{
		return 0;
	}

	*instanceIDOut = dti->instanceID;

	return 1;
}

//--------------------------------------------------------------------------------------------------

static S32 parseTimerFlagCmd(char* cmd){
	char* params = NULL;

	#define CMD_PARAMS(x)	(strStartsWith(cmd, x" ") ? ((params = cmd + strlen(x" ")),1): 0)

	if(CMD_PARAMS("TimerSetForcedOpen")){
		U32 threadID = atoi(params);
		
		params= strchr(params, ' ');
		if(params){
			U32 instanceID = atoi(++params);
		
			params = strchr(params, ' ');
			if(params){
				S32 enabled = atoi(++params);

				autoTimerSetTimerForcedOpen(threadID, 0, instanceID, enabled);
			}
		}
	}
	else if(CMD_PARAMS("TimerSetBreakPoint")){
		U32 threadID = atoi(params);
		
		params = strchr(params, ' ');
		if(params){
			U32 instanceID = atoi(++params);
		
			params = strchr(params, ' ');
			if(params){
				S32 enabled = atoi(++params);

				autoTimerSetTimerBreakPoint(threadID, 0, instanceID, enabled);
			}
		}
	}else{
		return 0;
	}
	
	return 1;

	#undef CMD_PARAMS
}

static void autoTimerGetThreadFrameTimeFromOS(TimerThreadData* td){
	S32		success = 0;
	S32		doDiff = !FALSE_THEN_SET(td->flags.osValuesPrevIsSet);
	OSTimes	osTimesCur = {0};

	#if !PLATFORM_CONSOLE
	autoTimerGetThreadOSTimes(td->threadHandle, &osTimesCur);
	
	if(osTimesCur.cycles){
		if(doDiff){
			td->curRoot->frame.osValues.cycles = osTimesCur.cycles - td->osValuesPrev.cycles;
		}
	}else{
		td->curRoot->frame.osValues.cycles = 0;
	}

	if(	osTimesCur.ticksUser ||
		osTimesCur.ticksKernel)
	{
		if(doDiff){
			td->curRoot->frame.osValues.ticksUser = osTimesCur.ticksUser -
													td->osValuesPrev.ticksUser;

			td->curRoot->frame.osValues.ticksKernel =	osTimesCur.ticksKernel -
														td->osValuesPrev.ticksKernel;
		}
	}else{
		td->curRoot->frame.osValues.ticksUser = 0;
		td->curRoot->frame.osValues.ticksKernel = 0;
	}

	td->osValuesPrev = osTimesCur;
	#endif
}

typedef struct CleanupThread {
	WaitThreadGroup*	wtg;

	struct {
		U32				refreshThreadList : 1;
	} flags;
} CleanupThread;

static void cleanupThreadNewThreadCB(	CleanupThread* ct,
										HANDLE handle,
										void* handleUserPointer)
{
	ct->flags.refreshThreadList = 1;

	assert(handle == autoTimers.cleanupThread.eventNewThread);
}

static void cleanupThreadDeadThreadCB(	CleanupThread* ct,
										HANDLE handle,
										TimerThreadData* td)
{
	ct->flags.refreshThreadList = 1;
	
	assert(td->cleanupThread.flags.isAdded);
	ASSERT_FALSE_AND_SET(td->cleanupThread.flags.isDead);

	wtgRemoveHandle(ct->wtg, td->threadHandle, td);
}

static S32 __stdcall autoTimerCleanupThreadMain(void* unusedParam){
	TimerThreadData*	td = THREAD_DATA_NO_FAIL;
	CleanupThread		ct = {0};

	EXCEPTION_HANDLER_BEGIN
	
	ct.flags.refreshThreadList = 1;
	
	wtgCreate(&ct.wtg);
	wtgAddHandle(	ct.wtg,
					autoTimers.cleanupThread.eventNewThread,
					cleanupThreadNewThreadCB,
					NULL);
	
	autoTimers.cleanupThread.threadID = CURRENT_THREAD_ID;

	while(1){
		autoTimerThreadFrameBegin(__FUNCTION__);

		wtgWait(ct.wtg, &ct, INFINITE);

		if(TRUE_THEN_RESET(ct.flags.refreshThreadList)){
			TimerThreadData* tdCur;
			TimerThreadData* tdPrev = NULL;
			
			PERFINFO_AUTO_START("refreshThreadList", 1);
			
			readLockU32(&autoTimers.threadList.lock);

			for(tdCur = autoTimers.threadList.head; tdCur;){
				if(tdCur->cleanupThread.flags.isDead){
					TimerThreadData* tdNext;

					PERFINFO_AUTO_START("cleanupDeadThread", 1);
					
					assert(tdCur->cleanupThread.flags.isAdded);

					// Nothing should have changed the list, this is just to prevent other readers.

					writeLockU32(&autoTimers.threadList.lock, 1);
					autoTimers.threadList.lockThreadID = CURRENT_THREAD_ID;

					// Changing the list, so get the real lock.
					
					autoTimerRecursionDisableInc(td);
					threadInstanceEnterCS();
					{
						if(tdPrev){
							tdNext = tdPrev->nextThread = tdCur->nextThread;
						}else{
							tdNext = autoTimers.threadList.head = tdCur->nextThread;
						}
						
						if(!tdNext){
							autoTimers.threadList.tail = tdPrev;
							
							if(!tdPrev){
								autoTimers.threadList.head = NULL;
							}
						}
					}
					threadInstanceLeaveCS();
					autoTimerRecursionDisableDec(td);

					tdCur->special.enabledCount = 0;
					tdCur->special.recursionDisabledCount = 0;
					autoTimerPopEntireThreadStack(tdCur, NULL);
					if(tdCur->curRoot->frame.cycles.begin){
						U64 cyclesNow;
						GET_CURRENT_CYCLES(cyclesNow);
						tdCur->curRoot->frame.cycles.end = cyclesNow;
						autoTimerGetThreadFrameTimeFromOS(tdCur);
					}
					autoTimerWriteFrame(tdCur, 1);
					assert(tdCur->threadHandle);
					CloseHandle(tdCur->threadHandle);
					tdCur->threadHandle = NULL;

					SAFE_FREE(tdCur->name);
					SAFE_FREE(tdCur);
					tdCur = tdNext;
					
					assert(autoTimers.threadList.lockThreadID == CURRENT_THREAD_ID);
					autoTimers.threadList.lockThreadID = 0;
					writeUnlockU32(&autoTimers.threadList.lock);

					PERFINFO_AUTO_STOP();
				}else{
					if(FALSE_THEN_SET(tdCur->cleanupThread.flags.isAdded)){
						PERFINFO_AUTO_START("addNewThread", 1);
						
						wtgAddHandle(	ct.wtg,
										tdCur->threadHandle,
										cleanupThreadDeadThreadCB,
										tdCur);
						
						PERFINFO_AUTO_STOP();
					}

					tdPrev = tdCur;
					tdCur = tdCur->nextThread;
				}
			}
			
			readUnlockU32(&autoTimers.threadList.lock);

			PERFINFO_AUTO_STOP();
		}
		
		autoTimerThreadFrameEnd();		
	}

	EXCEPTION_HANDLER_END

	return 0;
}

#if !PLATFORM_CONSOLE
typedef struct TimerServer TimerServer;

typedef enum TimerClientOutputType {
	TCOT_FBR,
	TCOT_COMMAND,
} TimerClientOutputType;

typedef struct TimerClientOutput TimerClientOutput;

typedef struct TimerClientOutput {
	TimerClientOutput*		next;
	TimerClientOutputType	tcoType;
	
	char*					command;
	
	struct {
		U32					isImportant;
	} flags;
} TimerClientOutput;

typedef struct TimerClient {
	TimerServer*					ts;
	
	PipeServerClient*				psc;
	
	struct {
		AutoTimerReader*			atr;
		AutoTimerReaderStream**		atrStreamsWithID;
		AutoTimerReaderStream**		atrStreamsToSend;
		U32							updatesRemaining;
		U32							bytesRemaining;
	} atr;
	
	FragmentedBufferReader*			fbr;
	U8								buffer[100];
	U32								bufferSize;

	struct {
		TimerClientOutput*			head;
		TimerClientOutput*			tail;
		U32							count;
	} tcos;

	struct {
		U32							fbrIsAttached				: 1;
		U32							sentBufferHeader			: 1;
		U32							sentTimersRunning			: 1;
		U32							waitingForWriteToComplete	: 1;
		U32							waitingForAvailableBuffer	: 1;
	} flags;
} TimerClient;

typedef struct TimerServer {
	IOCompletionPort*			iocp;
	IOCompletionAssociation*	iocaBufferAvailable;
	PipeServer*					ps;

	union {
		TimerClient**			tcsMutable;
		TimerClient*const*const	tcs;
	};
	
	OSTimes						osTimesPrev;

	U32							totalBufferSize;
	
	U32							msPrevSendProcessTime;

	struct {
		U32						updatesRemaining;
	} atr;
	
	struct {
		U32						count;
		U32						countUnimportant;
	} tco;
	
	struct {
		U32						hasOSTimesPrev	: 1;
		U32						running			: 1;
		U32						hasClients		: 1;
	} flags;
} TimerServer;

static void tcAddOutput(TimerClient* tc,
						TimerClientOutput** tcoOut,
						TimerClientOutputType tcoType,
						S32 isImportant)
{
	TimerClientOutput* tco = callocStruct(TimerClientOutput);
	
	tco->tcoType = tcoType;
	tco->flags.isImportant = !!isImportant;
	
	if(tc->tcos.tail){
		tc->tcos.tail->next = tco;
	}else{
		assert(!tc->tcos.head);
		tc->tcos.head = tco;
	}

	tc->tcos.tail = tco;
	tc->tcos.count++;
	
	tc->ts->tco.count++;
	
	if(!isImportant){
		tc->ts->tco.countUnimportant++;
	}
	
	if(tcoOut){
		*tcoOut = tco;
	}
}

static void tcAddOutputCommand(	TimerClient* tc,
								const char* command,
								S32 isImportant)
{
	TimerClientOutput* tco;
	
	tcAddOutput(tc, &tco, TCOT_COMMAND, isImportant);
	
	tco->command = strdup(command);
}

static void timerClientRemoveHeadOutput(TimerClient* tc){
	TimerClientOutput* tco = tc->tcos.head;
	
	tc->tcos.head = tco->next;
	
	if(!tc->tcos.head){
		assert(tc->tcos.tail == tco);
		assert(tc->tcos.count == 1);
		tc->tcos.tail = NULL;
	}
	
	assert(tc->ts->tco.count >= tc->tcos.count);
	
	assert(tc->tcos.count);
	tc->tcos.count--;
	
	assert(tc->ts->tco.count);
	tc->ts->tco.count--;
	
	if(!tco->flags.isImportant){
		assert(tc->ts->tco.countUnimportant);
		tc->ts->tco.countUnimportant--;
	}
	
	SAFE_FREE(tco->command);
	SAFE_FREE(tco);
}

static void timerClientAutoTimerReaderMsgHandler(const AutoTimerReaderMsg* msg){
	TimerClient* tc = msg->userPointer;
	
	switch(msg->msgType){
		xcase ATR_MSG_ANY_THREAD_NEW_BUFFER_AVAILABLE:{
			iocpAssociationTriggerCompletion(tc->ts->iocaBufferAvailable);
		}
		
		xcase ATR_MSG_WAIT_FOR_BUFFER_AVAILABLE:{
		}

		xcase ATR_MSG_NEW_BUFFER:{
			U32 bytes;
			
			assert(tc->atr.updatesRemaining <= tc->ts->atr.updatesRemaining);
			tc->ts->atr.updatesRemaining -= tc->atr.updatesRemaining;
			tc->atr.updatesRemaining = msg->newBuffer.updatesRemaining;
			tc->ts->atr.updatesRemaining += tc->atr.updatesRemaining;

			eaSetSize(&tc->atr.atrStreamsToSend, 0);
			
			FOR_BEGIN(i, (S32)msg->newBuffer.streamCount);
				eaPush(	&tc->atr.atrStreamsToSend,
						msg->newBuffer.streams[i]);
			FOR_END;

			if(!tc->fbr){
				fbReaderCreate(&tc->fbr);
			}
			
			fbReaderAttach(	tc->fbr,
							msg->newBuffer.fb,
							1);
							
			tc->flags.fbrIsAttached = 1;

			fbReaderGetBytesRemaining(tc->fbr, &bytes);
			
			tc->ts->totalBufferSize += bytes;
			
			tcAddOutput(tc, NULL, TCOT_FBR, 1);
			
			msg->out->newBuffer.flags.stopSendingBuffers = 1;
		}
	}
}

static void autoTimerServerSendProcessTimes(TimerServer* ts){
	PERFINFO_AUTO_START("sendProcessTimes", 1);
	{
		static HANDLE hProcess;
		
		OSTimes	osTimesCur;
		OSTimes	osTimesDelta;
		char	buffer[100];
		
		if(!hProcess){
			hProcess = GetCurrentProcess();
		}
		
		if(hProcess){
			autoTimerGetProcessOSTimes(hProcess, &osTimesCur);
		
			osTimesDelta.cycles = osTimesCur.cycles - ts->osTimesPrev.cycles;
			osTimesDelta.ticksUser = osTimesCur.ticksUser - ts->osTimesPrev.ticksUser;
			osTimesDelta.ticksKernel = osTimesCur.ticksKernel - ts->osTimesPrev.ticksKernel;
			
			ts->osTimesPrev = osTimesCur;
		
			if(!FALSE_THEN_SET(ts->flags.hasOSTimesPrev)){
				PERFINFO_AUTO_START("sprintf", 1);
					sprintf(buffer,
							"ProcessTimes %"FORM_LL"u %"FORM_LL"u %"FORM_LL"u",
							osTimesDelta.cycles,
							osTimesDelta.ticksUser,
							osTimesDelta.ticksKernel);
				PERFINFO_AUTO_STOP();
				
				EARRAY_CONST_FOREACH_BEGIN(ts->tcs, i, isize);
					TimerClient* tc = ts->tcs[i];
					
					tcAddOutputCommand(tc, buffer, 0);
				EARRAY_FOREACH_END;
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

static void autoTimerDebugPipeServerMsgHandler(const PipeServerMsg* msg){
	TimerServer*		ts = msg->ps.userPointer;
	PipeServerClient*	psc = msg->psc.psc;
	TimerClient*		tc = msg->psc.userPointer;

	switch(msg->msgType){
		xcase PS_MSG_CLIENT_CONNECT:{
			assert(!tc);
			tc = callocStruct(TimerClient);
			tc->ts = ts;
			tc->psc = psc;
			psClientSetUserPointer(psc, tc);
			eaPush(&ts->tcsMutable, tc);
			ts->flags.hasClients = 1;
			tcAddOutputCommand(tc, "AckConnect", 1);

			if(!ts->flags.hasOSTimesPrev){
				ts->msPrevSendProcessTime = timeGetTime();
				autoTimerServerSendProcessTimes(ts);
			}

			if (g_ConnectedCallback)
				g_ConnectedCallback(true);

			autoTimersPublicState.connected = true;
		}

		xcase PS_MSG_CLIENT_DISCONNECT:{
			autoTimersPublicState.connected = false;
			// Remove from the server.
			if (g_ConnectedCallback)
				 g_ConnectedCallback(false);
			if(eaFindAndRemove(&ts->tcsMutable, tc) < 0){
				assert(0);
			}
			
			if(tc->atr.atr){
				// Free the AutoTimerReaderStreams.
			
				eaDestroy(&tc->atr.atrStreamsToSend);

				while(eaSize(&tc->atr.atrStreamsWithID)){
					AutoTimerReaderStream* s = tc->atr.atrStreamsWithID[0];
					
					eaRemove(&tc->atr.atrStreamsWithID, 0);

					autoTimerReaderStreamDestroy(&s);
				}
				
				eaDestroy(&tc->atr.atrStreamsWithID);

				// Free the AutoTimerReader.

				autoTimerReaderDestroy(&tc->atr.atr);
			}

			if(TRUE_THEN_RESET(tc->flags.fbrIsAttached)){
				U32 bytes;
				
				if(fbReaderGetBytesRemaining(tc->fbr, &bytes)){
					assert(tc->bufferSize + bytes <= ts->totalBufferSize);
					ts->totalBufferSize -= tc->bufferSize + bytes;
				}
			}
			
			fbReaderDestroy(&tc->fbr);
			
			// Free the command list.
			
			while(tc->tcos.head){
				timerClientRemoveHeadOutput(tc);
			}
			
			if(!eaSize(&ts->tcs)){
				assert(!ts->totalBufferSize);
				assert(!ts->tco.count);

				ASSERT_TRUE_AND_RESET(ts->flags.hasClients);

				ts->msPrevSendProcessTime = 0;
 				ZeroStruct(&ts->osTimesPrev);
 				ts->flags.hasOSTimesPrev = 0;
			}
			
			// Done.
			
			SAFE_FREE(tc);
		}

		xcase PS_MSG_DATA_RECEIVED:{
			char	buffer[1000];
			char*	params = NULL;
			
			strncpy(buffer,
					msg->dataReceived.data,
					msg->dataReceived.dataBytes);
					
			#if PRINT_DEBUG_STUFF
				printf(	"Received: \"%s\".\n",
						buffer);
			#endif
					
			#define CMD(x)			(!stricmp(buffer, x))
			#define CMD_PARAMS(x)	(strStartsWith(buffer, x" ") ? ((params = buffer + strlen(x" ")),1): 0)
					
			if(CMD_PARAMS("StartTimerWithID")){
				U32						id = atoi(params);
				AutoTimerReaderStream*	stream;

				if(!tc->atr.atr){
					autoTimerReaderCreate(	&tc->atr.atr,
											timerClientAutoTimerReaderMsgHandler,
											tc);
				}
				
				autoTimerReaderStreamCreate(tc->atr.atr, &stream, U32_TO_PTR(id));
				
				eaPush(&tc->atr.atrStreamsWithID, stream);
			}
			else if(CMD_PARAMS("StopTimerWithID")){
				U32 id = atoi(params);
				S32 found = 0;
				
				if(!tc->atr.atr){
					break;
				}

				EARRAY_CONST_FOREACH_BEGIN(tc->atr.atrStreamsWithID, i, isize);
					AutoTimerReaderStream*	stream = tc->atr.atrStreamsWithID[i];
					void*					userPointer;
					
					autoTimerReaderStreamGetUserPointer(stream, &userPointer);
					
					if(PTR_TO_U32(userPointer) == id){
						char tcoText[100];

						autoTimerReaderStreamDestroy(&stream);
						eaRemove(&tc->atr.atrStreamsWithID, i);
						eaFindAndRemove(&tc->atr.atrStreamsToSend, stream);

						sprintf(tcoText, "TimerStoppedWithID %u", id);
						tcAddOutputCommand(tc, tcoText, 1);
						
						found = 1;

						break;
					}
				EARRAY_FOREACH_END;
				
				if(!found){
					#if PRINT_DEBUG_STUFF
						printf("Didn't find id %d\n", id);
					#endif
				}
			}
			else if(parseTimerFlagCmd(buffer)){
				// Do nothing.
			}

			#undef CMD
			#undef CMD_PARAMS
		}
		
		xcase PS_MSG_DATA_SENT:{
			tc->flags.waitingForWriteToComplete = 0;
		}
	}
}

static void processTimerClients(TimerServer* ts){
	U32 timersAreRunning = !!autoTimersPublicState.enabled;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(ts->tcs, i, isize);
		TimerClient* tc = ts->tcs[i];
		
		if(tc->flags.sentTimersRunning != timersAreRunning){
			tc->flags.sentTimersRunning = timersAreRunning;
			
			if(timersAreRunning){
				tcAddOutputCommand(tc, "ThreadsAreActive", 1);
			}else{
				tcAddOutputCommand(tc, "NoThreadsAreActive", 1);
			}
		}
		
		while(!tc->flags.waitingForWriteToComplete){
			TimerClientOutput*	tco;
			S32					tcoFinished = 0;
			
			if( tc->atr.atr &&
				!tc->flags.fbrIsAttached)
			{
				autoTimerReaderRead(tc->atr.atr);

				autoTimerReaderGetBytesRemaining(	tc->atr.atr,
													&tc->atr.bytesRemaining);
			}
			
			tco = tc->tcos.head;
			
			if(!tco){
				break;
			}
			
			switch(tco->tcoType){
				xcase TCOT_COMMAND:{
					PERFINFO_AUTO_START("sending command", 1);
					
					if(psClientWriteString(	tc->psc,
											tco->command,
											NULL))
					{
						tcoFinished = 1;
					}else{
						tc->flags.waitingForWriteToComplete = 1;
					}
					
					PERFINFO_AUTO_STOP();
				}
				
				xcase TCOT_FBR:{
					PERFINFO_AUTO_START("sending fb", 1);
					
					if(!tc->flags.sentBufferHeader){
						char	header[1000];
						U32		fbrBytes;
						U32		atrBytes;
						char*	pos = header;

						fbReaderGetBytesRemaining(tc->fbr, &fbrBytes);
						autoTimerReaderGetBytesRemaining(tc->atr.atr, &atrBytes);
						pos += sprintf(	header,
										"Buffer %u %u %u",
										fbrBytes,
										tc->atr.updatesRemaining,
										atrBytes);

						EARRAY_CONST_FOREACH_BEGIN(tc->atr.atrStreamsToSend, j, jsize);
							AutoTimerReaderStream*	stream = tc->atr.atrStreamsToSend[j];
							void*					userPointer;
							
							autoTimerReaderStreamGetUserPointer(stream, &userPointer);

							pos += snprintf_s(	pos,
												header + ARRAY_SIZE(header) - pos,
												" %u",
												PTR_TO_U32(userPointer));
						EARRAY_FOREACH_END;
						
						if(psClientWriteString(tc->psc, header, NULL)){
							tc->flags.sentBufferHeader = 1;
						}else{
							tc->flags.waitingForWriteToComplete = 1;
						}
					}else{
						assert(tc->flags.fbrIsAttached);

						while(1){
							if(tc->bufferSize){
								if(psClientWrite(	tc->psc,
													tc->buffer,
													tc->bufferSize,
													NULL))
								{
									assert(tc->bufferSize <= ts->totalBufferSize);
									ts->totalBufferSize -= tc->bufferSize;
									tc->bufferSize = 0;
								}else{
									tc->flags.waitingForWriteToComplete = 1;
									break;
								}
							}

							if(!tc->bufferSize){
								U32 bytesRemaining;

								PERFINFO_AUTO_START("get more buffer", 1);

								fbReaderGetBytesRemaining(tc->fbr, &bytesRemaining);

								if(!bytesRemaining){
									fbReaderDetach(tc->fbr);
									tc->flags.fbrIsAttached = 0;
									tc->flags.sentBufferHeader = 0;
									tcoFinished = 1;
									PERFINFO_AUTO_STOP();
									break;
								}
								
								tc->bufferSize = MIN(bytesRemaining, sizeof(tc->buffer));
								
								if(!fbReadBuffer(tc->fbr, tc->buffer, tc->bufferSize)){
									assert(0);
								}

								PERFINFO_AUTO_STOP();
							}
						}
					}
					PERFINFO_AUTO_STOP();
				}
			}
			
			if(tcoFinished){
				timerClientRemoveHeadOutput(tc);
			}else{
				break;
			}
		}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

static void autoTimerProcessServerBufferAvailableIOCPMsgHandler(const IOCompletionMsg* msg){
}

static void autoTimerProcessServerThreadStartup(TimerServer* ts){
	iocpCreate(&ts->iocp);

	iocpAssociationCreate(	&ts->iocaBufferAvailable,
							ts->iocp,
							NULL,
							autoTimerProcessServerBufferAvailableIOCPMsgHandler,
							ts);

	FOR_BEGIN(i, 100);
	{
		HANDLE	hMutex;
		char	name[100];
		
		sprintf(name, "CrypticProfiler%d", GetCurrentProcessId());

		if(i){
			strcatf(name, ".%d", i);
		}
		
		hMutex = CreateMutex_UTF8(NULL, FALSE, name);
		
		if(!hMutex){
			continue;
		}
		
		switch(WaitForSingleObject(hMutex, 0)){
			xcase WAIT_OBJECT_0:
			acase WAIT_ABANDONED:{
			}
			xdefault:{
				CloseHandle(hMutex);
				// Couldn't acquire the mutex.
				continue;
			}
		}

		psCreate(	&ts->ps,
					name,
					ts->iocp,
					ts,
					autoTimerDebugPipeServerMsgHandler);
		
		break;
	}
	FOR_END;
	
	assert(ts->iocp);
	assert(ts->ps);

	ts->flags.running = 1;
}

static S32 __stdcall autoTimerProcessServerThreadMain(TimerServer* ts){
	EXCEPTION_HANDLER_BEGIN
	
	autoTimerProcessServerThreadStartup(ts);
	
	while(1){
		// Main loop of server.

		autoTimerThreadFrameBegin(__FUNCTION__);
		{
			U32 msFrameStart = timeGetTime();
			
			while(1){
				U32 msCurTime = timeGetTime();
				U32 msTimeout;
				
				// Force 100ms frames so this thread doesn't spew frames.

				if(msCurTime - msFrameStart >= 100){
					break;
				}

				if(!ts->flags.hasClients){
					msTimeout = INFINITE;
				}else{
					msTimeout = msCurTime - ts->msPrevSendProcessTime;

					if(msTimeout >= 1000){
						msTimeout = 1000;

						ts->msPrevSendProcessTime = msCurTime;
						
						autoTimerServerSendProcessTimes(ts);
					}else{
						msTimeout = 1000 - msTimeout;
					}

					processTimerClients(ts);
				}

				iocpCheck(	ts->iocp,
							msTimeout,
							10,
							NULL);
			}
		}
		autoTimerThreadFrameEnd();
	}

	return 0;
	
	EXCEPTION_HANDLER_END
}

static void autoTimerConnectOnStartupClientMsgHandler(const PipeClientMsg* msg){
}

static S32 autoTimerFindConnectAtStartupProfilersCB(const ForEachProcessCallbackData* data){
	char serverPipeName[MAX_PATH];

	sprintf(serverPipeName,
			"CrypticProfilerConnectOnStartup%d",
			data->pid);

	#if PRINT_DEBUG_STUFF			
		printf("Checking if pipe \"%s\" exists.\n", serverPipeName);
	#endif

	if(pcServerIsAvailable(serverPipeName)){
		PipeClient* pc;
		
		#if PRINT_DEBUG_STUFF			
			printf("Connecting to pipe \"%s\".\n", serverPipeName);
		#endif

		if(pcCreate(&pc, NULL, autoTimerConnectOnStartupClientMsgHandler, NULL)){
			#if PRINT_DEBUG_STUFF			
				printf("Requesting connection from pid %d: ", data->pid);
			#endif
			
			if(!pcConnect(pc, serverPipeName, 2000)){
				#if PRINT_DEBUG_STUFF			
					printf("FAILED!\n");
				#endif
			}else{
				char	connectString[100];
				U32		msStartTime;

				#if PRINT_DEBUG_STUFF			
					printf("CONNECTED!\n");
				#endif

				sprintf(connectString,
						"ConnectToMe %d %s",
						GetCurrentProcessId(),
						getExecutableName());

				pcWrite(pc, connectString, (U32)strlen(connectString), NULL);

				#if PRINT_DEBUG_STUFF			
					printf("Waiting for requesting client to disconnect.\n");
				#endif

				msStartTime = timeGetTime();

				while(pcIsConnected(pc)){
					pcListen(pc, 100);
					
					if(timeGetTime() - msStartTime >= 2000){
						#if PRINT_DEBUG_STUFF			
							printf("Connect on startup server didn't respond.\n");
						#endif

						break;
					}
				}

				#if PRINT_DEBUG_STUFF			
					printf("Requesting client disconnected.\n");
				#endif
			}
			
			pcDestroy(&pc);
		}
	}
	
	return 1;
}

static void autoTimerFindConnectAtStartupProfilers(void){
	forEachProcess(autoTimerFindConnectAtStartupProfilersCB, NULL);
}
#endif

static void autoTimerStartCleanupThread(void){
	ATOMIC_INIT_BEGIN;
	{
		autoTimers.cleanupThread.eventNewThread = CreateEvent(NULL, FALSE, FALSE, NULL);
		
		_beginthreadex(NULL, 0, autoTimerCleanupThreadMain, NULL, 0, NULL);
	}
	ATOMIC_INIT_END;
}

static void autoTimerStartDebugServer(void){
#if !PLATFORM_CONSOLE
	autoTimerStartCleanupThread();

	ATOMIC_INIT_BEGIN;
	{
		FOR_BEGIN(i, TIMER_SERVER_THREAD_COUNT);
			TimerServer* ts = callocStruct(TimerServer);
			
			_beginthreadex(NULL, 0, autoTimerProcessServerThreadMain, ts, 0, NULL);
			
			while(!ts->flags.running){
				Sleep(1);
			}

			if (!autoTimers.disableConnectAtStartup)
				autoTimerFindConnectAtStartupProfilers();
		FOR_END;
	}
	ATOMIC_INIT_END;
#endif
}

typedef struct ProfilerConnect {
	char*					hostName;
	U32						hostPort;
	NetComm*				comm;
	NetLink*				link;
	AutoTimerReader*		atr;
	AutoTimerReaderStream*	atrStream;
	FragmentedBufferReader*	fbr;
	
	struct {
		U32					isInProcessMode : 1;
	} flags;
} ProfilerConnect;

static void profilerConnectHandleConnect(	NetLink* link,
											ProfilerConnect* pc)
{
	Packet* pak = pktCreate(link, 0);

	commSetMinReceiveTimeoutMS(pc->comm, 1000);

	pktSendString(pak, "MyClientType");
	pktSendString(pak, "Process");
	pktSendString(pak, getComputerName());
	pktSendString(pak, getUserName());
	pktSendString(pak, getExecutableName());
	#if PLATFORM_CONSOLE
		pktSendBitsAuto(pak, 0);
	#else
		pktSendBitsAuto(pak, GetCurrentProcessId());
	#endif
	pktSend(&pak);
}

static void profilerConnectHandleDisconnect(NetLink* link,
											ProfilerConnect* pc)
{
	pc->link = NULL;
	
	fbReaderDestroy(&pc->fbr);

	autoTimerReaderStreamDestroy(&pc->atrStream);
	autoTimerReaderDestroy(&pc->atr);
}

static void profilerConnectReaderMsgHandler(const AutoTimerReaderMsg* msg){
	ProfilerConnect* pc = msg->userPointer;

	switch(msg->msgType){
		xcase ATR_MSG_NEW_BUFFER:{
			Packet*						pak = pktCreate(pc->link, 0);
			U32							bytesAvailable = 0;
			FragmentedBufferReader*		fbr = pc->fbr;
			
			pktSendString(pak, "Buffer");
			
			if(!fbr){
				fbReaderCreate(&pc->fbr);
				fbr = pc->fbr;
			}
			
			fbReaderAttach(fbr, msg->newBuffer.fb, 1);
			fbReaderGetBytesRemaining(fbr, &bytesAvailable);
			
			while(bytesAvailable){
				char	buffer[1000];
				U32		bytesCur = MIN(sizeof(buffer), bytesAvailable);
				
				fbReadBuffer(fbr, buffer, bytesCur);
				
				pktSendBitsAuto(pak, bytesCur);
				pktSendBytes(pak, bytesCur, buffer);
				
				bytesAvailable -= bytesCur;
			}
			
			pktSendBitsAuto(pak, 0);
			
			pktSend(&pak);
			
			fbReaderDetach(fbr);
		}
	}
}

static void profilerConnectHandlePacket(Packet* pak,
										S32 cmdUnused,
										NetLink* link,
										ProfilerConnect* pc)
{
	char cmdName[100];
	
	pktGetString(pak, SAFESTR(cmdName));
	
	if(!pc->flags.isInProcessMode){
		if(!stricmp(cmdName, "YourClientType")){
			char clientTypeName[100];

			clientTypeName[0] = 0;
			
			pktGetString(pak, SAFESTR(clientTypeName));
			
			if(!stricmp(clientTypeName, "Process")){
				pc->flags.isInProcessMode = 1;
			}else{
				pc->flags.isInProcessMode = 0;
			}
		}
	}else{
		if(!stricmp(cmdName, "StartTimer")){
			U32 startID = pktGetBitsAuto(pak);

			if(!pc->atr){
				Packet* pakOut = pktCreate(link, 0);
				
				pktSendString(pakOut, "StartID");
				pktSendBitsAuto(pakOut, startID);
				pktSend(&pakOut);
				
				autoTimerReaderCreate(&pc->atr, profilerConnectReaderMsgHandler, pc);
				autoTimerReaderStreamCreate(pc->atr, &pc->atrStream, pc);
				
				commSetMinReceiveTimeoutMS(pc->comm, 10);
			}
		}
		else if(!stricmp(cmdName, "StopTimer")){
			autoTimerReaderStreamDestroy(&pc->atrStream);
			autoTimerReaderDestroy(&pc->atr);

			commSetMinReceiveTimeoutMS(pc->comm, 1000);
		}
		else if(!stricmp(cmdName, "SetTimerFlag")){
			char cmdString[1000];
			
			pktGetString(pak, SAFESTR(cmdString));

			parseTimerFlagCmd(cmdString);
		}
	}
}

static S32 __stdcall profilerConnectThreadMain(ProfilerConnect* pc){
	EXCEPTION_HANDLER_BEGIN
	
	pc->comm = commCreate(10, 0);
	
	pc->link = commConnect(	pc->comm, 
							LINKTYPE_SHARD_NONCRITICAL_500K,
							LINK_NO_COMPRESS |
								LINK_FORCE_FLUSH,
							pc->hostName,
							FIRST_IF_SET(pc->hostPort, CRYPTIC_PROFILER_PORT),
							profilerConnectHandlePacket,
							profilerConnectHandleConnect,
							profilerConnectHandleDisconnect,
							0);
	
	if(pc->link){
		linkSetUserData(pc->link, pc);
		
		while(pc->link){
			autoTimerThreadFrameBegin(__FUNCTION__);
			
			commMonitor(pc->comm);
			
			autoTimerReaderRead(pc->atr);
			
			autoTimerThreadFrameEnd();
		}
	}
	
	commDestroy(&pc->comm);
	
	SAFE_FREE(pc->hostName);
	SAFE_FREE(pc);
	
	EXCEPTION_HANDLER_END

	return 0;
}

static void autoTimerInitDLLs(void){
#if !PLATFORM_CONSOLE
	dlls.kernel32.hDLL = LoadLibrary(L"kernel32.dll");

	if(dlls.kernel32.hDLL){
		#define FIND(x) dlls.kernel32.func##x = (x##Func)GetProcAddress(dlls.kernel32.hDLL, #x)
			FIND(GetThreadTimes);
			FIND(GetProcessTimes);
			FIND(OpenThread);
			#if !DISABLE_OS_CYCLE_FUNCTIONS
				FIND(QueryThreadCycleTime);
				FIND(QueryProcessCycleTime);
				FIND(QueryIdleProcessorCycleTime);
				FIND(GetPerformanceInfo);
			#endif
		#undef FIND
	}

	dlls.psAPI.hDLL = LoadLibrary(L"psapi.dll");

	if(dlls.psAPI.hDLL){
		#define FIND(x) dlls.psAPI.func##x = (x##Func)GetProcAddress(dlls.psAPI.hDLL, #x)
			FIND(GetProcessMemoryInfo);
			FIND(GetPerformanceInfo);
		#undef FIND
	}
	
	dlls.funcGetPerformanceInfo = FIRST_IF_SET(	dlls.kernel32.funcGetPerformanceInfo,
												dlls.psAPI.funcGetPerformanceInfo);
#endif
}

void autoTimerInit(void){

	// If a function table has been passed in, use that instead of initializing a new one.
	if (override)
		return;

	ATOMIC_INIT_BEGIN;
	{
		autoTimers.tlsIndex.data = TlsAlloc();

		#if !PLATFORM_CONSOLE
			autoTimers.pidSelf = GetCurrentProcessId();
		#endif

		autoTimers.maxDepth = 30;

		//InitializeCriticalSection(&autoTimers.cs.threadInstance.cs);
		//InitializeCriticalSection(&autoTimers.cs.memPool.cs);
		//InitializeCriticalSection(&autoTimers.cs.sequencing.cs);
		
		autoTimerInitDLLs();
		autoTimerStartCleanupThread();
		autoTimerStartDebugServer();

		autoTimers.initialized = 1;
	}
	ATOMIC_INIT_END;
}

// Add a public state structure to the list.
static void autoTimerRegisterPublicState(AutoTimersPublicState *publicState)
{
	assert(autoTimersPublicStateSize < sizeof(autoTimersPublicStateList)/sizeof(autoTimersPublicStateList[0]));
	autoTimersPublicStateList[autoTimersPublicStateSize] = publicState;
	assert(!publicState->enabled);
	publicState->enabled = autoTimersPublicState.enabled;
	++autoTimersPublicStateSize;
}

// Return auto timer hooks.
const AutoTimerData *autoTimerGet()
{
	static AutoTimerData data;
	static bool initialized = false;

	if (override)
		return override;

	if (!initialized)
	{
		data.version = AUTOTIMERDATA_VERSION;
		data.fp_autoTimerRegisterPublicState = autoTimerRegisterPublicState;
		data.fp_autoTimerThreadFrameBegin = autoTimerThreadFrameBegin;
		data.fp_autoTimerThreadFrameEnd = autoTimerThreadFrameEnd;
		data.fp_autoTimerDisableRecursion = autoTimerDisableRecursion;
		data.fp_autoTimerEnableRecursion = autoTimerEnableRecursion;
		data.fp_autoTimerAddFakeCPU = autoTimerAddFakeCPU;
		data.fp_autoTimerBeginCPU = autoTimerBeginCPU;
		data.fp_autoTimerBeginCPUGuard = autoTimerBeginCPUGuard;
		data.fp_autoTimerBeginCPUBlocking = autoTimerBeginCPUBlocking;
		data.fp_autoTimerBeginCPUBlockingGuard = autoTimerBeginCPUBlockingGuard;
		data.fp_autoTimerBeginBits = autoTimerBeginBits;
		data.fp_autoTimerBeginMisc = autoTimerBeginMisc;
		data.fp_autoTimerEndCPU = autoTimerEndCPU;
		data.fp_autoTimerEndCPUGuard = autoTimerEndCPUGuard;
		data.fp_autoTimerEndCPUCheckLocation = autoTimerEndCPUCheckLocation;
		data.fp_autoTimerEndBits = autoTimerEndBits;
		data.fp_autoTimerEndMisc = autoTimerEndMisc;
		initialized = true;
	}
	return &data;
}

// Return auto timer hooks.
void autoTimerSet(const AutoTimerData *data)
{
	// Only allow overriding if the version is the same and at least one function is different.
	if (data && data->version == AUTOTIMERDATA_VERSION &&
		(data->fp_autoTimerRegisterPublicState != autoTimerRegisterPublicState
		|| data->fp_autoTimerThreadFrameBegin != autoTimerThreadFrameBegin
		|| data->fp_autoTimerThreadFrameEnd != autoTimerThreadFrameEnd
		|| data->fp_autoTimerDisableRecursion != autoTimerDisableRecursion
		|| data->fp_autoTimerEnableRecursion != autoTimerEnableRecursion
		|| data->fp_autoTimerAddFakeCPU != autoTimerAddFakeCPU
		|| data->fp_autoTimerBeginCPU != autoTimerBeginCPU
		|| data->fp_autoTimerBeginCPUGuard != autoTimerBeginCPUGuard
		|| data->fp_autoTimerBeginCPUBlocking != autoTimerBeginCPUBlocking
		|| data->fp_autoTimerBeginCPUBlockingGuard != autoTimerBeginCPUBlockingGuard
		|| data->fp_autoTimerBeginBits != autoTimerBeginBits
		|| data->fp_autoTimerBeginMisc != autoTimerBeginMisc
		|| data->fp_autoTimerEndCPU != autoTimerEndCPU
		|| data->fp_autoTimerEndCPUGuard != autoTimerEndCPUGuard
		|| data->fp_autoTimerEndCPUCheckLocation != autoTimerEndCPUCheckLocation
		|| data->fp_autoTimerEndBits != autoTimerEndBits
		|| data->fp_autoTimerEndMisc != autoTimerEndMisc))
	{
		override = data;
		data->fp_autoTimerRegisterPublicState(&autoTimersPublicState);
	}
}

bool autoTimerDisableConnectAtStartup(bool disabled)
{
	bool old = autoTimers.disableConnectAtStartup;
	autoTimers.disableConnectAtStartup = disabled;
	return old;
}

#if TRACK_CALL_STACKS
static void printTrackedCallStacks(TimerThreadData* td){
	TimerThreadStacks* stacks = td->stacks;
	
	if(stacks){
		ARRAY_FOREACH_BEGIN(stacks->stack, i);
			S32 index = (td->stacks->curStack + i) % ARRAY_SIZE(stacks->stack);
			S32 maxDepth = MIN(	stacks->stack[index].depth.hi,
								ARRAY_SIZE(stacks->stack[index].info));
			
			printf("Stack: ");
			
			FOR_BEGIN(j, maxDepth);
				printfColor((j > (S32)stacks->stack[index].depth.start ? COLOR_BRIGHT : 0) |
								(j & 1 ? COLOR_GREEN : COLOR_RED),
							"%s/",
							stacks->stack[index].info[j]->locName);
			FOR_END;
			
			printf("\n");
		ARRAY_FOREACH_END;
	}
}
#endif

static void __fastcall autoTimerEndCheckLocName(TimerThreadData* td,
												const char* locNameStop)
{
	if(	!autoTimers.assertShown &&
		//td->flags.properlyStartedFrame &&
		locNameStop &&
		THREAD_TOP)
	{
		const char* locNameStart = THREAD_TOP->locName;

		if(strcmp(locNameStop, locNameStart)){
			if(!autoTimers.disablePopupErrors){
				char message[200];
				
				autoTimerRecursionDisableInc(td);

				#if TRACK_CALL_STACKS
					printTrackedCallStacks(td);
				#endif
				
				sprintf(message, 
						"AutoTimer start/stop location mismatch:\n"
						"\n"
						"START: \"%s\"\n"
						"\n"
						"STOP:  \"%s\"\n",
						locNameStart,
						locNameStop);

				printf("%s\n", message);
				//assertmsgf(0, message);

				autoTimerRecursionDisableDec(td);
			}

			autoTimers.assertShown = 1;
		}
	}
}

static void __fastcall autoTimerStackUnderFlow(TimerThreadData* td){
	#if TRACK_CALL_STACKS
		printTrackedCallStacks(td);
	#endif

	if(!autoTimers.disablePopupErrors){
		//printf("AutoTimer stack underflow.");
		//assert(0);//Errorf("AutoTimer stack underflow.");
	}

	autoTimers.assertShown = 1;
}

typedef struct PerfInfoInitData {
	const char*				locName;
	PerformanceInfoType		infoType;
	S32						isBlocking;
} PerfInfoInitData;

static PerformanceInfo* __fastcall autoTimerCreateNewAutoTimer(	TimerThreadData* td,
																const PerfInfoInitData* piInit)
{
	PerformanceInfo* parent = THREAD_TOP;
	PerformanceInfo* pi;

	// Turn off the auto timers when allocating stuff, to prevent recursive calls from memory hooks.

	autoTimerRecursionDisableInc(td);
	{
		pi = piCreate(piInit->locName);
		pi->tdFrameWhenCreate = td->frameCount;
	}
	autoTimerRecursionDisableDec(td);

	// Turn it back on.

	assert(!pi->parent);
	assert(!pi->threadID);

	pi->parent = parent;

	pi->threadID = td->threadID;
	pi->instanceID = ++td->lastTimerInstanceID;

	pi->flags.infoType = piInit->infoType;
	
	if(pi->flags.infoType == PERFINFO_TYPE_CPU){
		pi->flags.isBlocking = !!piInit->isBlocking;
	}

	if(parent){
		if(parent->children.tail){
			parent->children.tail->nextSibling = pi;
		}else{
			parent->children.head = pi;
		}

		parent->children.tail = pi;

		assert(parent);
	}else{
		// This pi has no parent, so put it into the root list.
		
		if(td->curRoot->rootList.head){
			PerformanceInfo* tail = td->curRoot->rootList.tail;

			tail->nextSibling = pi;
			pi->nextSibling = NULL;

			td->curRoot->rootList.tail = pi;
		}else{
			td->curRoot->rootList.head = pi;
			td->curRoot->rootList.tail = pi;
		}
	}

	pi->nextSibling = NULL;
	pi->children.head = NULL;
	pi->children.tail = NULL;

	td->flags.hasNewChild = 1;

	return pi;
}

static PerformanceInfo* __fastcall autoTimerFlattenChildList(PerformanceInfo* child, PerformanceInfo** tailOut, U32* countOut){
	U32 count = 1;
	PerformanceInfo* head;
	PerformanceInfo* tail;
	U32 countIn;

	if(child->siblingTree.lo){
		head = autoTimerFlattenChildList(child->siblingTree.lo, &tail, &countIn);
		tail->siblingTree.hi = child;
		child->siblingTree.lo = tail;
		count += countIn;
	}
	else
	{
		head = child;
	}

	if(child->siblingTree.hi){
		child->siblingTree.hi = autoTimerFlattenChildList(child->siblingTree.hi, &tail, &countIn);
		child->siblingTree.hi->siblingTree.lo = child;
		count += countIn;
	}
	else
	{
		tail = child;
	}

	if(tailOut){
		*tailOut = tail;
	}

	if(countOut){
		*countOut = count;
	}

	return head;
}

static PerformanceInfo* __fastcall autoTimerBalanceFlatChildListSlow(PerformanceInfo* start, S32 dir, S32 lo, S32 hi){
	S32 offset = (hi - lo) / 2;
	S32 center = dir ? lo + offset : hi - offset;
	S32 i;

	if(hi == lo){
		start->siblingTree.lo = NULL;
		start->siblingTree.hi = NULL;
		return start;
	}
	else if(hi < lo){
		return NULL;
	}

	for(i = 0; i < offset; i++){
		start = dir ? start->siblingTree.hi : start->siblingTree.lo;
	}

	start->siblingTree.lo = autoTimerBalanceFlatChildListSlow(start->siblingTree.lo, 0, lo, center - 1);
	start->siblingTree.hi = autoTimerBalanceFlatChildListSlow(start->siblingTree.hi, 1, center + 1, hi);

	return start;
}

static PerformanceInfo* __fastcall autoTimerBalanceFlatChildListFastHelper(PerformanceInfo** timers, S32 lo, S32 hi){
	PerformanceInfo* midInfo;
	S32 mid;

	if(hi < lo){
		return NULL;
	}

	mid= (hi + lo) / 2;
	midInfo = timers[mid];

	midInfo->siblingTree.lo = autoTimerBalanceFlatChildListFastHelper(timers, lo, mid - 1);
	midInfo->siblingTree.hi = autoTimerBalanceFlatChildListFastHelper(timers, mid + 1, hi);

	return midInfo;
}

#define TIMER_BALANCE_CHILD_LIST_FAST_SIZE 1000

static PerformanceInfo* __fastcall autoTimerBalanceFlatChildListFast(PerformanceInfo* start, S32 hi){
	PerformanceInfo**	timers;
	PerformanceInfo**	cur;

	assert(hi < TIMER_BALANCE_CHILD_LIST_FAST_SIZE);

	cur = timers = _alloca((hi + 1) * sizeof(*timers));

	for(; start && cur - timers <= hi; start = start->siblingTree.hi){
		*cur++ = start;
	}

	assert(cur - timers == hi + 1);

	return autoTimerBalanceFlatChildListFastHelper(timers, 0, hi);
}

static U32 __fastcall autoTimerVerifyChildTree(PerformanceInfo* node, S32* depthOut, S32 checkDepth){
	S32 loDepth;
	S32 hiDepth;
	U32 count;

	if(!node){
		if(depthOut){
			*depthOut = 0;
		}

		return 0;
	}

	assert(!node->siblingTree.lo || node->siblingTree.lo->staticData < node->staticData);
	assert(!node->siblingTree.hi || node->siblingTree.hi->staticData > node->staticData);

	count = autoTimerVerifyChildTree(node->siblingTree.lo, &loDepth, checkDepth);
	count += autoTimerVerifyChildTree(node->siblingTree.hi, &hiDepth, checkDepth);

	if(checkDepth){
		assert(abs(loDepth - hiDepth) <= 1);
	}

	if(depthOut){
		*depthOut = 1 + max(loDepth, hiDepth);
	}

	return count + 1;
}

static void __fastcall autoTimerBalanceChildTree(PerformanceInfo** pRoot){
	//U32 initialCount = autoTimerVerifyChildTree(*pRoot, NULL, 0);
	U32 count;

	*pRoot = autoTimerFlattenChildList(*pRoot, NULL, &count);

	//assert(count == initialCount);

	if(count <= TIMER_BALANCE_CHILD_LIST_FAST_SIZE){
		*pRoot = autoTimerBalanceFlatChildListFast(*pRoot, count - 1);
	}
	else
	{
		*pRoot = autoTimerBalanceFlatChildListSlow(*pRoot, 1, 0, count - 1);
	}

	//count = autoTimerVerifyChildTree(*pRoot, NULL, 1);

	//assert(count == initialCount);
}

static void autoTimerHitBreak(	TimerThreadData* td,
								PerformanceInfo* pi)
{
	threadInstanceEnterCS();
	{
		td->timerFlagsChangedInstanceID++;
		pi->atomicFlags.isBreakpoint = 0;
	}
	threadInstanceLeaveCS();

	// Hit a break.  Go ahead and continue.

	autoTimerRecursionDisableInc(td);
	{
		char buffer[500];
		
		sprintf(buffer,
				"Hey, you hit an auto timer breakpoint (timer \"%s\").\n"
				"\n"
				"Click DEBUG to do debug it.\n"
				"\n"
				"Click IGNORE to continue running normally.\n",
				pi->locName);
		
		if(IsDebuggerPresent()){
			printf("%s", buffer);
			_DbgBreak();
		}else{
			ignorableAssertmsg(0, buffer);
		}
	}
	autoTimerRecursionDisableDec(td);
}

static S32 __fastcall autoTimerFindInChildTree(	TimerThreadData* td,
												PerfInfoStaticData** piStatic,
												PerformanceInfoType infoType)
{
	PerformanceInfo*	stackTop = THREAD_TOP;
	PerformanceInfo*	pi;

	if(stackTop){
		pi = stackTop->childTreeRoot;
	}else{
		pi = td->curRoot->childTreeRoot;
	}

	while(1){
		if(!pi){
			return 0;
		}
		else if(piStatic == pi->staticData){
			if(infoType == pi->flags.infoType){
				// Found the already-existing pi!
				break;
			}
			else if(infoType < pi->flags.infoType){
				pi = pi->siblingTree.lo;
			}else{
				pi = pi->siblingTree.hi;
			}
		}
		else if(piStatic < pi->staticData){
			pi = pi->siblingTree.lo;
		}else{
			pi = pi->siblingTree.hi;
		}
	}

	if(pi->atomicFlags.isBreakpoint){
		autoTimerHitBreak(td, pi);
	}

	// Push me onto the stack.

	THREAD_TOP = pi;
	THREAD_TOP_DEPTH = THREAD_CUR_DEPTH;

	return 1;
}

static S32 __fastcall autoTimerCreateInChildTree(	TimerThreadData* td,
													PerfInfoStaticData** piStatic,
													const PerfInfoInitData* piInit)
{
	PerformanceInfo*	stackTop = THREAD_TOP;
	PerformanceInfo*	pi;
	PerformanceInfo**	piPtr;
	PerformanceInfo**	piRoot;

	if(stackTop){
		piRoot = &stackTop->childTreeRoot;
	}else{
		piRoot = &td->curRoot->childTreeRoot;
	}

	pi = *piRoot;
	piPtr = piRoot;

	while(1){
		if(!pi){
			pi = autoTimerCreateNewAutoTimer(td, piInit);

			if(!pi){
				return 0;
			}

			pi->staticData = piStatic;
			*piPtr = pi;

			autoTimerBalanceChildTree(piRoot);

			break;
		}
		else if(piStatic == pi->staticData){
			if(piInit->infoType == pi->flags.infoType){
				// Found the already-existing pi!
				assert(0);
			}
			else if(piInit->infoType < pi->flags.infoType){
				piPtr = &pi->siblingTree.lo;
				pi = pi->siblingTree.lo;
			}else{
				piPtr = &pi->siblingTree.hi;
				pi = pi->siblingTree.hi;
			}
		}
		else if(piStatic < pi->staticData){
			piPtr = &pi->siblingTree.lo;
			pi = pi->siblingTree.lo;
		}else{
			piPtr = &pi->siblingTree.hi;
			pi = pi->siblingTree.hi;
		}
	}
	
	// Check if this pi is waiting to become a breakpoint.
	
	#if 0
		// Find waiting breakpoints here.
	#endif
	
	// Break if it was.

	if(pi->atomicFlags.isBreakpoint){
		autoTimerHitBreak(td, pi);
	}

	// Push me onto the stack.

	THREAD_TOP = pi;
	THREAD_TOP_DEPTH = THREAD_CUR_DEPTH;

	return 1;
}

#if TRACK_CALL_STACKS
static TimerThreadStacks* __fastcall timerStoreStackInit(TimerThreadData* td){
	if(!td->stacks){
		autoTimerRecursionDisableInc(td);
		td->stacks = callocStruct(TimerThreadStacks);
		autoTimerRecursionDisableDec(td);
	}

	return td->stacks;
}

static void __fastcall timerStoreStackPush(TimerThreadData* td){
	TimerThreadStacks*	stacks = timerStoreStackInit(td);
	S32					stackID = stacks->curStack;

	if(stacks->stack[stackID].depth.hi + 1 != THREAD_CUR_DEPTH){
		S32 oldStackID = stackID;
		S32 oldDepth = stacks->stack[oldStackID].depth.cur;
		S32 cappedOldDepth = min(ARRAY_SIZE(stacks->stack[0].info), oldDepth);

		stackID = stacks->curStack = (stackID + 1) % ARRAY_SIZE(stacks->stack);

		CopyStructs(stacks->stack[stackID].info, stacks->stack[oldStackID].info, cappedOldDepth);

		stacks->stack[stackID].depth.cur = oldDepth;
		stacks->stack[stackID].depth.hi = oldDepth;
		stacks->stack[stackID].depth.start = oldDepth;
	}

	if(stacks->stack[stackID].depth.cur < ARRAY_SIZE(stacks->stack[stackID].info)){
		stacks->stack[stackID].info[stacks->stack[stackID].depth.cur] = THREAD_TOP;
	}

	stacks->stack[stackID].depth.hi = ++stacks->stack[stackID].depth.cur;
}

static void __fastcall timerStoreStackPop(TimerThreadData* td){
	TimerThreadStacks*	stacks = timerStoreStackInit(td);
	S32					stackID = stacks->curStack;

	if(	stacks->stack[stackID].depth.cur <= 0 ||
		stacks->stack[stackID].depth.cur != THREAD_CUR_DEPTH)
	{
		assert(0);
	}

	stacks->stack[stackID].depth.cur--;
}
#endif // TRACK_CALL_STACKS

static void autoTimerClearSiblingList(PerformanceInfo* pi){
	for(; pi; pi = pi->nextSibling){
		ZeroStruct(&pi->currentFrame);
		ZeroStruct(&pi->currentRun);
		autoTimerClearSiblingList(pi->children.head);
	}
}

static void autoTimerClearThread(TimerThreadData* td){
	ARRAY_FOREACH_BEGIN(td->roots, i);
		TimerThreadDataRoot* r = td->roots + i;

		autoTimerClearSiblingList(r->rootList.head);
		
		ZeroStruct(&r->frame);
	ARRAY_FOREACH_END;
	
	ZeroStruct(&td->osValuesPrev);
}

static void autoTimerResetStack(TimerThreadData* td){
	td->frameWhenResetStack = td->frameCount;

	while(1){
		assert(td->special.enabledCount);
		InterlockedDecrement(&td->special.enabledCount);
		assert(td->special.resetOnNextCall);
		if(!InterlockedDecrement(&td->special.resetOnNextCall)){
			break;
		}
	}

	// Destroy entire stack.

	autoTimerPopEntireThreadStack(td, NULL);

	td->flags.properlyStartedFrame = 0;
	td->curRoot = td->roots + td->flags.properlyStartedFrame;
	td->flags.osValuesPrevIsSet = 0;
	
	autoTimerClearThread(td);
}

static __forceinline TimerThreadData* autoTimerBeginType(	TimerThreadData*	td,
															const char*					locName,
															PerfInfoStaticData**		piStatic,
															const PerformanceInfoType	infoType,
															S32							isBlocking)
{
	U32					stackPrevDepth;

	if(!td){
		return NULL;
	}
		
	if(td->special.enabledCount){
		if(td->special.recursionDisabledCount){
			return NULL;
		}

		if(td->special.resetOnNextCall){
			autoTimerResetStack(td);
		}
	}

	stackPrevDepth = THREAD_CUR_DEPTH++;

	if(	// Depth isn't past the top depth (max depth hit, timer closed, etc).
		stackPrevDepth == THREAD_TOP_DEPTH
		&&
		// Top timer isn't forced closed.
		(	!stackPrevDepth ||
			!THREAD_TOP->atomicFlags.forcedClosed) &&
		// Depth is less than max depth OR timer is forced open.
		(	stackPrevDepth < autoTimers.maxDepth ||
			THREAD_TOP->atomicFlags.forcedOpen))
	{
		// Is it time to start a new non-frame frame?
	
		if(	!stackPrevDepth &&
			!td->flags.properlyStartedFrame &&
			!td->curRoot->frame.cycles.begin)
		{
			U64 cyclesBegin;
			
			GET_CURRENT_CYCLES(cyclesBegin);
			td->curRoot->frame.cycles.begin = cyclesBegin;

			if(!td->flags.osValuesPrevIsSet){
				autoTimerGetThreadFrameTimeFromOS(td);
			}
		}
		
		// Find timer first, then create if it's not there.
		
		if(!autoTimerFindInChildTree(td, piStatic, infoType)){
			PerfInfoInitData piInit = {locName, infoType, isBlocking};

			autoTimerCreateInChildTree(td, piStatic, &piInit);
		}

		#if TRACK_CALL_STACKS
		if(autoTimers.trackCallStacks){
			timerStoreStackPush(td);
		}
		#endif
		
		return td;
	}

	return NULL;
}

#define AUTO_TIMER_BEGIN_TYPE(type, isBlocking)			\
	TimerThreadData* td = THREAD_DATA;					\
	td = autoTimerBeginType(	td,						\
								locName,				\
								piStatic,				\
								type,					\
								isBlocking)

void __fastcall autoTimerAddFakeCPU(U64 addCPU){
	TimerThreadData* td;

	if (override)
	{
		override->fp_autoTimerAddFakeCPU(addCPU);
		return;
	}

	td = THREAD_DATA;

	if(!td){
		return;
	}

	if(	THREAD_TOP &&
		THREAD_CUR_DEPTH == THREAD_TOP_DEPTH &&
		!td->special.recursionDisabledCount)
	{
		PerformanceInfo* stackTop = THREAD_TOP;

		stackTop->currentFrame.cyclesTotal += addCPU;

		//stackTop->currentFrame.count++;
	}
}

//------------[ The Main Begin Functions ]-----------------------------------------------------------------------------------------------------------

void __fastcall autoTimerBeginCPU(	const char* locName,
									PerfInfoStaticData** piStatic)
{
	if (override)
		override->fp_autoTimerBeginCPU(locName, piStatic);
	else
	{
		AUTO_TIMER_BEGIN_TYPE(PERFINFO_TYPE_CPU, 0);

		if(td){
			PerformanceInfo*	stackTop = THREAD_TOP;
			U64					cyclesBegin;

			//assert(!strcmp(locName, stackTop->locName));

			//stackTop->opCountInt += inc;

			// For maximum accuracy: start the PerformanceInfo as the very last operation.

			GET_CURRENT_CYCLES(cyclesBegin);
			stackTop->currentRun.cyclesBegin = cyclesBegin;
		}
	}
}

void __fastcall autoTimerBeginCPUWithTD(	const char* locName,
									PerfInfoStaticData** piStatic,
									TimerThreadData * td)
{
	static char aszFindStr[100] = "Playerstatus_paperdoll";
	if (stricmp(locName,aszFindStr) == 0)
	{
		int bp=3;
		bp++;
	}

	td = autoTimerBeginType(td,	locName, piStatic,
								PERFINFO_TYPE_CPU,	
								0 /*isBlocking*/);

	assert(td);
	assert(td->timerStack.top->locName == locName);

	if(td){
		PerformanceInfo*	stackTop = td->timerStack.top;
		U64					cyclesBegin;

		//assert(!strcmp(locName, stackTop->locName));

		//stackTop->opCountInt += inc;

		// For maximum accuracy: start the PerformanceInfo as the very last operation.

		GET_CURRENT_CYCLES(cyclesBegin);
		stackTop->currentRun.cyclesBegin = cyclesBegin;
	}

	assert(td->timerStack.top->locName == locName);
}

void __fastcall autoTimerBeginCPUGuard(	const char* locName,
										PerfInfoStaticData** piStatic,
										PerfInfoGuard** guard)
{
	if (override)
		override->fp_autoTimerBeginCPUGuard(locName, piStatic, guard);
	else
	{
		AUTO_TIMER_BEGIN_TYPE(PERFINFO_TYPE_CPU, 0);
	
		if(td){
			PerformanceInfo*	stackTop = THREAD_TOP;
			U64					cyclesBegin;
	
			// For maximum accuracy: start the pi as the very last operation.
	
			GET_CURRENT_CYCLES(cyclesBegin);
			stackTop->currentRun.cyclesBegin = cyclesBegin;
			stackTop->guardCurrent = guard;
		}
	}
}

void __fastcall autoTimerBeginCPUBlocking(	const char* locName,
											PerfInfoStaticData** piStatic)
{
	if (override)
		override->fp_autoTimerBeginCPUBlocking(locName, piStatic);
	else
	{
		AUTO_TIMER_BEGIN_TYPE(PERFINFO_TYPE_CPU, 1);

		if(td){
			PerformanceInfo*	stackTop = THREAD_TOP;
			U64					cyclesBegin;

			stackTop->flags.isBlocking = 1;

			GET_CURRENT_CYCLES(cyclesBegin);
			stackTop->currentRun.cyclesBegin = cyclesBegin;
		}
	}
}

void __fastcall autoTimerBeginCPUBlockingGuard(	const char* locName,
												PerfInfoStaticData** piStatic,
												PerfInfoGuard** guard)
{
	if (override)
		override->fp_autoTimerBeginCPUBlockingGuard(locName, piStatic, guard);
	else
	{
		AUTO_TIMER_BEGIN_TYPE(PERFINFO_TYPE_CPU, 1);
	
		if(td){
			PerformanceInfo*	stackTop = THREAD_TOP;
			U64					cyclesBegin;
			
			stackTop->flags.isBlocking = 1;
	
			GET_CURRENT_CYCLES(cyclesBegin);
			stackTop->currentRun.cyclesBegin = cyclesBegin;
			stackTop->guardCurrent = guard;
		}
	}
}

void __fastcall autoTimerBeginBits(	Packet* pak,
									const char* locName,
									PerfInfoStaticData** piStatic)
{
	if (override)
		override->fp_autoTimerBeginBits(pak, locName, piStatic);
	else
	{
		AUTO_TIMER_BEGIN_TYPE(PERFINFO_TYPE_BITS, 0);
	
		if(td){
			PerformanceInfo* stackTop = THREAD_TOP;
	
			stackTop->currentRun.cyclesBegin = pktGetReadOrWriteIndex(pak);
		}
	}
}

void __fastcall autoTimerBeginMisc(	U64 startVal,
									const char* locName,
									PerfInfoStaticData** piStatic)
{
	if (override)
		override->fp_autoTimerBeginMisc(startVal, locName, piStatic);
	else
	{
		AUTO_TIMER_BEGIN_TYPE(PERFINFO_TYPE_MISC, 0);

		if(td){
			PerformanceInfo* stackTop = THREAD_TOP;

			stackTop->currentRun.cyclesBegin = startVal;
		}
	}
}

//------------[ The Main End Functions ]-----------------------------------------------------------------------------------------------------------

#define POP_TIMER_STACK		((THREAD_TOP = stackTop->parent),(THREAD_TOP_DEPTH--))

static __forceinline void autoTimerEndCPUCommon(TimerThreadData* td,
												U64 cyclesEnd)
{
	U64					cyclesDelta;
	PerformanceInfo*	stackTop = THREAD_TOP;
	
	if(stackTop->flags.infoType == PERFINFO_TYPE_CPU){
		cyclesDelta = cyclesEnd - stackTop->currentRun.cyclesBegin;

		if(cyclesDelta > stackTop->currentFrame.cyclesMax){
			stackTop->currentFrame.cyclesMax = cyclesDelta;
		}

		stackTop->currentFrame.cyclesTotal += cyclesDelta;
	}

	stackTop->currentFrame.count++;

	// Pop me from the pi stack.

	POP_TIMER_STACK;
}

static void __fastcall timerCheckEndThreadID(TimerThreadData* td){
	if(	THREAD_TOP_DEPTH > 0 &&
		THREAD_TOP->threadID != td->threadID)
	{
		//S32 x = 0;
	}
}

static void autoTimerFreeTimers(TimerThreadData* td,
								PerformanceInfo* cur)
{
	while(cur){
		PerformanceInfo* next = cur->nextSibling;

		autoTimerFreeTimers(td, cur->children.head);

		autoTimerRecursionDisableInc(td);

		destroyPerformanceInfo(cur);

		autoTimerRecursionDisableDec(td);

		cur = next;
	}
}

static void autoTimerDestroyThreadTimers(TimerThreadData* td){
	assert(0);
}

#if TRACK_CALL_STACKS
	#define POP_TRACKED_STACK if(autoTimers.trackCallStacks){timerStoreStackPop(td);}
#else
	#define POP_TRACKED_STACK
#endif

#if CHECK_THREAD_ID_ON_END
	#define CHECK_FOR_CORRECT_THREAD_ID	timerCheckEndThreadID(td)
#else
	#define CHECK_FOR_CORRECT_THREAD_ID
#endif

#define DECREMENT_CUR_DEPTH								\
	if(!--THREAD_CUR_DEPTH){							\
		autoTimerEndHandleDepthZero(td, __FUNCTION__);	\
	}

#define CHECK_FOR_STACK_UNDERFLOW						\
		if(!stackCurDepth){								\
			if( td->flags.properlyStartedFrame &&		\
				!autoTimers.assertShown)				\
			{											\
				assert(!THREAD_TOP);					\
				autoTimerStackUnderFlow(td);			\
			}											\
			return;										\
		}
		
#define TIMER_FINISHED_BEGIN(guard,td)					\
	if(td){												\
		const U32 stackCurDepth = THREAD_CUR_DEPTH;		\
		if(td->special.enabledCount){					\
			if(td->special.recursionDisabledCount){		\
				return;									\
			}											\
			if(td->special.resetOnNextCall){			\
				autoTimerResetStack(td);				\
				return;									\
			}											\
		}												\
		CHECK_FOR_STACK_UNDERFLOW;						\
		CHECK_FOR_CORRECT_THREAD_ID;					\
		POP_TRACKED_STACK;								\
		if(stackCurDepth == THREAD_TOP_DEPTH){			\
			if(	THREAD_TOP->guardCurrent == guard ||	\
				autoTimerHandleGuardError(td, guard))	\
			{											\
				{										\
					void force_semicolon_aldjslfa(void)
		
#define TIMER_FINISHED_END								\
				}										\
				DECREMENT_CUR_DEPTH;					\
			}											\
		}else{											\
			DECREMENT_CUR_DEPTH;						\
		}												\
	}((void)0)

static S32 autoTimerHandleGuardError(	TimerThreadData* td,
										PerfInfoGuard** guard)
{
	if(guard){
		PerformanceInfo* pi;

		for(pi = THREAD_TOP; pi; pi = pi->parent){
			if(pi->guardCurrent != guard){
				continue;
			}

			autoTimerPopEntireThreadStack(td, guard);
		
			if(	THREAD_TOP &&
				THREAD_TOP->guardCurrent == guard)
			{
				return 1;
			}

			break;
		}
	}
	
	return 0;
}

static void autoTimerEndHandleDepthZero(TimerThreadData* td,
										const char* functionName)
{
	if(!td->flags.properlyStartedFrame){
		U64 cyclesNow;
		
		GET_CURRENT_CYCLES(cyclesNow);
		
		assert(td->curRoot->frame.cycles.begin);
		
		if(cyclesNow - td->curRoot->frame.cycles.begin >= MIN_FRAME_CYCLES){
			td->curRoot->frame.cycles.end = cyclesNow;
			autoTimerGetThreadFrameTimeFromOS(td);
			autoTimerWriteFrame(td, 0);
		}
	}
		
	if(	td->startInstanceID != autoTimers.startInstanceID &&
		!td->special.recursionDisabledCount)
	{
		autoTimerDestroyThreadTimers(td);
	}
}

#define TIMER_END_CPU_BASIC {					\
		U64 cyclesEnd;							\
		GET_CURRENT_CYCLES(cyclesEnd);			\
		autoTimerEndCPUCommon(td, cyclesEnd);	\
	}

void __fastcall autoTimerEndCPU(void){
	if (override)
		override->fp_autoTimerEndCPU();
	else
	{
		TimerThreadData* td = THREAD_DATA;
		TIMER_FINISHED_BEGIN(NULL,td);
		{
			TIMER_END_CPU_BASIC;
		}
		TIMER_FINISHED_END;
	}
}

void __fastcall autoTimerEndCPUGuard(PerfInfoGuard** guard){
	if (override)
		override->fp_autoTimerEndCPUGuard(guard);
	else
	{
		TimerThreadData* td = THREAD_DATA;
		TIMER_FINISHED_BEGIN(guard,td);
		{
			U64 cyclesEnd;
			GET_CURRENT_CYCLES(cyclesEnd);
			THREAD_TOP->guardCurrent = NULL;
			autoTimerEndCPUCommon(td, cyclesEnd);
		}
		TIMER_FINISHED_END;
	}
}

void __fastcall autoTimerEndCPUCheckLocation(const char* locName){
	if (override)
		override->fp_autoTimerEndCPUCheckLocation(locName);
	else
	{
		TimerThreadData* td = THREAD_DATA;
		TIMER_FINISHED_BEGIN(NULL,td);
		{
			U64 cyclesEnd;
			GET_CURRENT_CYCLES(cyclesEnd);
			autoTimerEndCheckLocName(td, locName);
			autoTimerEndCPUCommon(td, cyclesEnd);
		}
		TIMER_FINISHED_END;
	}
}

void __fastcall autoTimerEndCPUCheckLocationWithTD(const char* locName,TimerThreadData* td)
{			
	if (td)
	{
		const U32 stackCurDepth = THREAD_CUR_DEPTH;									
		CHECK_FOR_STACK_UNDERFLOW;					
		CHECK_FOR_CORRECT_THREAD_ID;				
		POP_TRACKED_STACK;							
		if(stackCurDepth == THREAD_TOP_DEPTH)
		{
			U64 cyclesEnd;
			GET_CURRENT_CYCLES(cyclesEnd);
			autoTimerEndCheckLocName(td, locName);
			autoTimerEndCPUCommon(td, cyclesEnd);
		}
		DECREMENT_CUR_DEPTH;
	}
}

/*void __fastcall autoTimerEndCPUCheckLocationWithTD(const char* locName,TimerThreadData* td)
{			
	TIMER_FINISHED_BEGIN(NULL,td);
	{
		U64 cyclesEnd;
		GET_CURRENT_CYCLES(cyclesEnd);
		autoTimerEndCheckLocName(td, locName);
		autoTimerEndCPUCommon(td, cyclesEnd);
	}
	TIMER_FINISHED_END;
}*/

void __fastcall autoTimerEndBits(Packet* pak){
	if (override)
		override->fp_autoTimerEndBits(pak);
	else
	{
		TimerThreadData* td = THREAD_DATA;
		TIMER_FINISHED_BEGIN(NULL,td);
		{
			PerformanceInfo* stackTop = THREAD_TOP;
	
			switch(stackTop->flags.infoType){
				xcase PERFINFO_TYPE_BITS:{
					U32 index = pktGetReadOrWriteIndex(pak);
	
					if(index > stackTop->currentRun.cyclesBegin){
						U64 addCPU =	SQR((U64)1000) *
										(U64)(index - stackTop->currentRun.cyclesBegin);
										
						stackTop->currentFrame.cyclesTotal += addCPU;
					}
	
					stackTop->currentFrame.count++;
					POP_TIMER_STACK;
				}
				xcase PERFINFO_TYPE_CPU:{
					TIMER_END_CPU_BASIC;
				}
				xdefault:{
					stackTop->currentFrame.count++;
					POP_TIMER_STACK;
				}
			}
		}
		TIMER_FINISHED_END;
	}
}

void __fastcall autoTimerEndMisc(U64 stopVal){
	if (override)
		override->fp_autoTimerEndMisc(stopVal);
	else
	{
		TimerThreadData* td = THREAD_DATA;
		TIMER_FINISHED_BEGIN(NULL,td);
		{
			PerformanceInfo* stackTop = THREAD_TOP;
	
			switch(stackTop->flags.infoType){
				xcase PERFINFO_TYPE_MISC:{
					const U64 addCPU = stopVal - stackTop->currentRun.cyclesBegin;
	
					stackTop->currentFrame.cyclesTotal += addCPU;
					stackTop->currentFrame.count++;
					POP_TIMER_STACK;
				}
				xcase PERFINFO_TYPE_CPU:{
					TIMER_END_CPU_BASIC;
				}
				xdefault:{
					stackTop->currentFrame.count++;
					POP_TIMER_STACK;
				}
			}
		}
		TIMER_FINISHED_END;
	}
}

static void autoTimerPopEntireThreadStack(	TimerThreadData* td,
											PerfInfoGuard** guard)
{
	if(!td){
		return;
	}

	for(; THREAD_CUR_DEPTH; THREAD_CUR_DEPTH--){
		if(THREAD_CUR_DEPTH == THREAD_TOP_DEPTH){
			if(	guard &&
				THREAD_TOP->guardCurrent == guard)
			{
				break;
			}
			
			THREAD_TOP->guardCurrent = NULL;

			assert(!td->special.recursionDisabledCount);

			THREAD_TOP->currentFrame.count++;

			switch(THREAD_TOP->flags.infoType){
				xcase PERFINFO_TYPE_CPU:{
					U64 cyclesNow;
					GET_CURRENT_CYCLES(cyclesNow);
					THREAD_TOP->currentFrame.cyclesTotal += cyclesNow -
															THREAD_TOP->currentRun.cyclesBegin;
				}
				xcase PERFINFO_TYPE_BITS:
					// Can't do anything.
				xcase PERFINFO_TYPE_MISC:
					// Can't do anything.
				xdefault:{
					// Can't do anything, and shouldn't happen.
				}
			}
			
			THREAD_TOP = THREAD_TOP->parent;
			THREAD_TOP_DEPTH--;
		}
	}
}

void autoTimerSetDepth(U32 depth){
	autoTimers.maxDepth = MINMAX(depth, 1, 100);
}

static void autoTimerWriteSiblingList(	TimerThreadData* td,
										PerformanceInfo* pi,
										FragmentedBuffer* fb,
										const S32 hasNewChildInTree,
										const S32 isFullUpdate,
										const S32 clearCurrentFrame,
										const U32 depth)
{
	S32 wroteNewChild = 0;

	for(; pi; pi = pi->nextSibling){
		const S32 writeChildAsNew = isFullUpdate ||
									!pi->flags.writtenPreviously;
									
		#if 0
		{
			FOR_BEGIN(i, (S32)depth);
				printf("  ");
			FOR_END;

			printf(	"- %s (%"FORM_LL"u)\n",
					pi->locName,
					pi->currentFrame.cyclesTotal);
		}
		#endif

		if(	!writeChildAsNew &&
			!pi->currentFrame.count)
		{
			fbWriteBit(fb, 0);
		}else{
			fbWriteBit(fb, 1);

			if(writeChildAsNew){
				assert(	isFullUpdate ||
						hasNewChildInTree);

				wroteNewChild = 1;

				fbWriteString(fb, pi->locName);
				fbWriteU32(fb, 3, pi->flags.infoType);
				
				if(pi->flags.infoType == PERFINFO_TYPE_CPU){
					fbWriteBit(fb, pi->flags.isBlocking);
				}
				
				fbWriteU64(fb, 64, (U64)(uintptr_t)pi->staticData);
				
				autoTimerWriteCount(fb, pi->instanceID);
			}else{
				// New children can't come before old children.

				assert(!wroteNewChild);
			}

			WRITE_CHECK_STRING(fb, "sibling");

			autoTimerWriteCount(fb,
								pi->currentFrame.count);

			autoTimerWriteCycles(	fb,
									pi->currentFrame.cyclesTotal,
									NULL);

			if(clearCurrentFrame){
				pi->currentFrame.cyclesTotal = 0;
				pi->currentFrame.count = 0;
			}

			autoTimerWriteSiblingList(	td,
										pi->children.head,
										fb,
										hasNewChildInTree,
										isFullUpdate,
										clearCurrentFrame,
										depth + 1);
		}

		pi->flags.writtenPreviously = 1;
	}

	// Terminate the new child list.

	if(	hasNewChildInTree ||
		isFullUpdate)
	{
		fbWriteBit(fb, 0);
	}
}

static void autoTimerWriteFlagChangesHelper(FragmentedBuffer* fb,
											PerformanceInfo* pi,
											S32 isFullUpdate,
											S32 clearCurrentFrame)
{
	for(; pi; pi = pi->nextSibling){
		U32 isBreakpoint = pi->atomicFlags.isBreakpoint;
		U32 forcedOpen = pi->atomicFlags.forcedOpen;
		U32 forcedClosed = pi->atomicFlags.forcedClosed;

		if(	isFullUpdate &&
			(	isBreakpoint ||
				forcedOpen ||
				forcedClosed)
			||
			!isFullUpdate &&
			(	isBreakpoint != pi->flags.writtenIsBreakPoint ||
				forcedOpen != pi->flags.writtenForcedOpen ||
				forcedClosed != pi->flags.writtenForcedClosed)
			)
		{
			fbWriteBit(fb, 1);
			fbWriteU32(fb, 32, pi->instanceID);
			fbWriteBit(fb, isBreakpoint);
			fbWriteBit(fb, forcedOpen);
			fbWriteBit(fb, forcedClosed);
			
			if(clearCurrentFrame){
				pi->flags.writtenIsBreakPoint = isBreakpoint;
				pi->flags.writtenForcedOpen = forcedOpen;
				pi->flags.writtenForcedClosed = forcedClosed;
			}
		}
		
		if(pi->children.head){
			autoTimerWriteFlagChangesHelper(fb, pi->children.head, isFullUpdate, clearCurrentFrame);
		}
	}
}

static void autoTimerWriteFlagChanges(	TimerThreadData* td,
										FragmentedBuffer* fb,
										S32 isFullUpdate,
										S32 clearCurrentFrame)
{
	ARRAY_FOREACH_BEGIN(td->roots, i);
	{
		autoTimerWriteFlagChangesHelper(fb,
										td->roots[i].rootList.head,
										isFullUpdate,
										clearCurrentFrame);
	}	
	ARRAY_FOREACH_END;

	fbWriteBit(fb, 0);
}

static void autoTimerWriteFrameToBuffer(TimerThreadData* td,
										const U32 oldReaderInstanceID,
										const U32 newReaderInstanceID,
										const U32 maxDepth,
										const S32 isFullUpdate,
										S32 doWriteFlagChanges,
										const S32 clearCurrentFrame,
										const S32 isFinalFrame)
{
	FragmentedBuffer*	fb = NULL;
	SharedReaderUpdate*	sru = NULL;
	S32					doWriteName;
	S32					readersExist;
	
	if(isFullUpdate){
		doWriteFlagChanges = 1;
	}

	readLockU32(&autoTimers.readers.lockStreamCount);
	{
		readersExist = !!autoTimers.readers.streamCount;
	}
	readUnlockU32(&autoTimers.readers.lockStreamCount);
	
	if(readersExist){
		sru = callocStruct(SharedReaderUpdate);

		sru->oldReaderInstanceID = oldReaderInstanceID;
		sru->newReaderInstanceID = newReaderInstanceID;

		fbCreate(&fb, 0);
		sru->fb = fb;
	}
		
	fbWriteString(fb, "Frame1");
	
	fbWriteU32(fb, 32, td->threadID);
	
	if(!isFullUpdate){
		WRITE_CHECK_STRING(fb, "diff");
		
		fbWriteBit(fb, !!td->flags.hasNewChild);
	}else{
		WRITE_CHECK_STRING(fb, "full");
	}

	WRITE_CHECK_STRING(fb, "siblings");

	// Write all the root trees.

	ARRAY_FOREACH_BEGIN(td->roots, i);
		const TimerThreadDataRoot* r = td->roots + i;
		
		fbWriteBit(fb, 1);
		
		if(!r->frame.cycles.begin){
			fbWriteBit(fb, 0);
		}else{
			const U64 cyclesDiff = r->frame.cycles.end - r->frame.cycles.begin;
			
			fbWriteBit(fb, 1);
			fbWriteU64(fb, 64, r->frame.cycles.begin);
			autoTimerWriteCycles(fb, cyclesDiff, NULL);
			
			if(!r->frame.osValues.cycles){
				fbWriteBit(fb, 0);
			}else{
				fbWriteBit(fb, 1);
				autoTimerWriteCycles(	fb,
										r->frame.osValues.cycles,
										NULL);
			}

			if(!r->frame.osValues.ticksUser){
				fbWriteBit(fb, 0);
			}else{
				fbWriteBit(fb, 1);
				autoTimerWriteCycles(	fb,
										r->frame.osValues.ticksUser,
										NULL);
			}

			if(!r->frame.osValues.ticksKernel){
				fbWriteBit(fb, 0);
			}else{
				fbWriteBit(fb, 1);
				autoTimerWriteCycles(	fb,
										r->frame.osValues.ticksKernel,
										NULL);
			}
		}

		if(isFullUpdate){
			switch(i){
				xcase 0:
					fbWriteString(fb, "Non-Frames");
				xcase 1:
					fbWriteString(fb, "Frames");
				xdefault:
					fbWriteString(fb, "Unknown");
			}
		}

		autoTimerWriteSiblingList(	td,
									td->roots[i].rootList.head,
									fb,
									td->flags.hasNewChild,
									isFullUpdate,
									clearCurrentFrame,
									0);
	ARRAY_FOREACH_END;
	
	fbWriteBit(fb, 0);
	
	// Write name and flags.
	
	WRITE_CHECK_STRING(fb, "flags");

	doWriteName =	td->name &&
					(	!td->flags.wroteName ||
						isFullUpdate);
	
	if(	!doWriteFlagChanges &&
		!doWriteName &&
		!isFullUpdate &&
		!isFinalFrame &&
		maxDepth == td->written.maxDepth)
	{
		fbWriteBit(fb, 0);
	}else{
		fbWriteBit(fb, 1);
		
		if(!doWriteName){
			fbWriteBit(fb, 0);
		}else{
			fbWriteBit(fb, 1);
			fbWriteString(fb, td->name);
		}

		if(!doWriteFlagChanges){
			fbWriteBit(fb, 0);
		}else{
			fbWriteBit(fb, 1);
			autoTimerWriteFlagChanges(td, fb, isFullUpdate, clearCurrentFrame);
		}
		
		if(	!isFullUpdate &&
			maxDepth == td->written.maxDepth)
		{
			fbWriteBit(fb, 0);
		}else{
			fbWriteBit(fb, 1);
			fbWriteU32(fb, 32, maxDepth);
		}
		
		fbWriteBit(fb, !!isFinalFrame);
	}
	
	fbWriteString(fb, "FrameEnd");

	// Done!
	
	#if 0
	{
		U32 size;
		fbGetSizeAsBits(fb, &size);
		printfColor(COLOR_BRIGHT | (isFullUpdate ? COLOR_GREEN : COLOR_RED),
					"%5.5d: Wrote %s frame: %d bits\n",
					td->threadID,
					isFullUpdate ? "full" : "update",
					size);
	}
	#endif
	
	// Send buffer to all relevant readers.

	autoTimerSendBufferToReaders(sru);
}

static U32 getLastInstanceID(void)
{
	U32 ret;
	
	modifySequencingEnterCS();
	{
		ret = autoTimers.readers.lastInstanceID;
	}
	modifySequencingLeaveCS();
	
	return ret;
}

static void autoTimerVerifyAllSiblingsWritten(PerformanceInfo* pi){
	for(; pi; pi = pi->nextSibling){
		assert(pi->flags.writtenPreviously);

		autoTimerVerifyAllSiblingsWritten(pi->children.head);
	}
}

static void autoTimerWriteFrame(TimerThreadData* td,
								S32 isFinalFrame)
{
	U32	readerInstanceID;
	S32	doWriteFullUpdate;
	S32	doWriteFlagChanges = td->timerFlagsChangedInstanceID != td->written.timerFlagsChangedInstanceID;
	U32	timerFlagsChangedInstanceID = 0;
	U32	maxDepth = autoTimers.maxDepth;
	S32	wroteSomething = 0;
	
	td->msWriteFrame = timeGetTime();
	
	td->frameCount++;

	ASSERT_FALSE_AND_SET(td->flags.writingFrame);
	
	autoTimerRecursionDisableInc(td);

	readerInstanceID = getLastInstanceID();
	doWriteFullUpdate = td->written.readerInstanceID != readerInstanceID;

	// Write flag changes.

	if(doWriteFlagChanges){
		threadInstanceEnterCS();
		{
			timerFlagsChangedInstanceID = td->timerFlagsChangedInstanceID;
		}
		threadInstanceLeaveCS();
	}

	// Write frame updates.

	if(td->written.readerInstanceID){
		// Write diff updated.

		if(autoTimers.readers.minInstanceID <= td->written.readerInstanceID){
			wroteSomething = 1;
			
			autoTimerWriteFrameToBuffer(td,
										0,
										td->written.readerInstanceID,
										maxDepth,
										0,
										doWriteFlagChanges,
										!doWriteFullUpdate,
										isFinalFrame);

			#if VERIFY_ALL_WRITTEN_ON_DIFF
				ARRAY_FOREACH_BEGIN(td->roots, i);
					autoTimerVerifyAllSiblingsWritten(td->roots[i].rootList.head);
				ARRAY_FOREACH_END;
			#endif
		}
	}

	if(doWriteFullUpdate){
		// Write full update.

		wroteSomething = 1;
			
		autoTimerWriteFrameToBuffer(td,
									td->written.readerInstanceID,
									readerInstanceID,
									maxDepth,
									1,
									doWriteFlagChanges,
									1,
									isFinalFrame);
	}
	
	if(!wroteSomething){
		autoTimerClearThread(td);
	}
	
	td->flags.hasNewChild = 0;
	td->written.maxDepth = maxDepth;
	td->written.readerInstanceID = readerInstanceID;
	
	if(td->name){
		td->flags.wroteName = 1;
	}
	
	if(	doWriteFlagChanges ||
		doWriteFullUpdate)
	{
		td->written.timerFlagsChangedInstanceID = timerFlagsChangedInstanceID;
	}
	
	autoTimerRecursionDisableDec(td);
	
	ARRAY_FOREACH_BEGIN(td->roots, i);
		ZeroStruct(&td->roots[i].frame);
	ARRAY_FOREACH_END;
	
	ASSERT_TRUE_AND_RESET(td->flags.writingFrame);

	td->msWriteFrame = timeGetTime();
}

void autoTimerThreadFrameBegin(const char* threadName){
	if (override)
		override->fp_autoTimerThreadFrameBegin(threadName);
	else if(autoTimers.initialized){
		TimerThreadData* td = THREAD_DATA_NO_FAIL;

		if(!td){
			return;
		}

		if(	!td->name &&
			threadName)
		{
			td->name = strdup(threadName);
		}

		if(	!td->flags.properlyStartedFrame &&
			PERFINFO_RUN_CONDITIONS)
		{
			U64 cyclesNow;
			
			GET_CURRENT_CYCLES(cyclesNow);

			if(td->curRoot->frame.cycles.begin){
				td->curRoot->frame.cycles.end = cyclesNow;
				autoTimerGetThreadFrameTimeFromOS(td);
			}

			td->flags.properlyStartedFrame = 1;
			td->curRoot = td->roots + td->flags.properlyStartedFrame;

			td->curRoot->frame.cycles.begin = cyclesNow;

			if(!td->flags.osValuesPrevIsSet){
				autoTimerGetThreadFrameTimeFromOS(td);
			}
		}
	}
}

void autoTimerThreadFrameEnd(void){
	if (override)
		override->fp_autoTimerThreadFrameEnd();
	else if(autoTimers.initialized){
		TimerThreadData* td = THREAD_DATA_NO_FAIL;
		
		if(	td &&
			td->flags.properlyStartedFrame)
		{
			U64 cyclesNow;

			// We have to check both of these variables here. autoTimerResetStack asserts
			// that both are non-zero. The other thread that sets them does not set them
			// atomically, so if we do not check both it can fail.
			if(td->special.enabledCount && td->special.resetOnNextCall){
				autoTimerResetStack(td);
			}else{
				autoTimerPopEntireThreadStack(td, NULL);

				GET_CURRENT_CYCLES(cyclesNow);
				
				if(cyclesNow - td->curRoot->frame.cycles.begin >= MIN_FRAME_CYCLES){
					td->curRoot->frame.cycles.end = cyclesNow;
					autoTimerGetThreadFrameTimeFromOS(td);

					td->flags.properlyStartedFrame = 0;
					td->curRoot = td->roots + td->flags.properlyStartedFrame;

					autoTimerWriteFrame(td, 0);
				}
			}
		}
	}
}

//---- ProfileFileWriter ---------------------------------------------------------------------------

typedef struct ProfileFileWriter {
	ProfileFileWriterMsgHandler		msgHandler;
	void*							userPointer;

	AutoTimerReader*				atr;
	AutoTimerReaderStream*			atrStream;
	
	FragmentedBufferReader*			fbr;
	
	FILE*							f;
	U32								readSomething;
	
	U8								buffer[1024];
	U32								bufferSize;
} ProfileFileWriter;

typedef struct ProfileFileReader {
	FILE*									f;
	U32										version;
	U32										curReadBytes;
	U32										prevReadBytes;
} ProfileFileReader;

S32 pfwCreate(	ProfileFileWriter** pfwOut,
				ProfileFileWriterMsgHandler msgHandler,
				void* userPointer)
{
	ProfileFileWriter* pfw;
	
	if(!pfwOut){
		return 0;
	}
	
	pfw = *pfwOut = callocStruct(ProfileFileWriter);
	
	pfw->msgHandler = msgHandler;
	pfw->userPointer = userPointer;
	
	return 1;
}

S32 pfwDestroy(ProfileFileWriter** pfwInOut){
	ProfileFileWriter* pfw = SAFE_DEREF(pfwInOut);
	
	if(!pfw){
		return 0;
	}
	
	pfwStop(pfw);
	
	fbReaderDestroy(&pfw->fbr);
	
	SAFE_FREE(*pfwInOut);
	
	return 1;
}

static void pfwFlush(ProfileFileWriter* pfw){
	if(pfw->bufferSize){
		fwrite(pfw->buffer, pfw->bufferSize, 1, pfw->f);
		pfw->bufferSize = 0;
	}
}

static void pfwWrite(	ProfileFileWriter* pfw,
						const void* bytes,
						U32 byteCount)
{
	while(byteCount){
		const U32 curByteCount = MIN(byteCount, sizeof(pfw->buffer) - pfw->bufferSize);
		
		memcpy(	pfw->buffer + pfw->bufferSize,
				bytes,
				curByteCount);
				
		bytes = (U8*)bytes + curByteCount;
		byteCount -= curByteCount;
		pfw->bufferSize += curByteCount;
		
		if(pfw->bufferSize == sizeof(pfw->buffer)){
			pfwFlush(pfw);
		}
	}
}

static void pfwWriteU32(ProfileFileWriter* pfw,
						U32 value)
{
	U8 buffer[4] = {(value >> 0) & 0xff,
					(value >> 8) & 0xff,
					(value >> 16) & 0xff,
					(value >> 24) & 0xff};
					
	pfwWrite(pfw, buffer, 4);
}

static void pfwWriteToFileMsgHandler(const AutoTimerReaderMsg* msg){
	ProfileFileWriter* pfw = msg->userPointer;
	
	switch(msg->msgType){
		xcase ATR_MSG_ANY_THREAD_NEW_BUFFER_AVAILABLE:{
			if(pfw->msgHandler){
				pfw->msgHandler(pfw,
								PFW_MSG_NEW_BUFFER_AVAILABLE,
								pfw->userPointer);
			}
		}

		xcase ATR_MSG_WAIT_FOR_BUFFER_AVAILABLE:{
			if(pfw->msgHandler){
				pfw->msgHandler(pfw,
								PFW_MSG_WAIT_FOR_NEW_BUFFER,
								pfw->userPointer);
			}
		}

		xcase ATR_MSG_NEW_BUFFER:{
			pfw->readSomething = 1;

			pfwWriteFragmentedBuffer(pfw, msg->newBuffer.fb);
			
			msg->out->newBuffer.flags.stopSendingBuffers = 1;
		}
	}
}

S32 pfwStart(	ProfileFileWriter* pfw,
				const char* fileName,
				S32 createLocalReader,
				AutoTimerDecoder* d)
{
	if(	!pfw
		||
		pfw->f
		||
		!createLocalReader &&
		!d)
	{
		return 0;
	}
	
	pfw->f = fopen(fileName, "wb");
	
	if(!pfw->f){
		return 0;
	}
	
	// Write a version.
	
	pfwWriteU32(pfw, PROFILER_FILE_VERSION);

	if(createLocalReader){
		pfwWriteU32(pfw, 0);
		autoTimerReaderCreate(&pfw->atr, pfwWriteToFileMsgHandler, pfw);
		autoTimerReaderStreamCreate(pfw->atr, &pfw->atrStream, NULL);
	}
	else if(d){
		FragmentedBuffer* fb;
		
		pfwWriteU32(pfw, 1);

		fbCreate(&fb, 0);
		autoTimerDecoderStateWrite(d, fb);
		pfwWriteFragmentedBuffer(pfw, fb);
		fbDestroy(&fb);
	}else{
		pfwWriteU32(pfw, 0);
	}
	
	return 1;
}

void pfwStop(ProfileFileWriter* pfw){
	if(!pfw->f){
		return;
	}
	
	pfwReadReader(pfw);
	pfwFlush(pfw);
	fclose(pfw->f);
	pfw->f = NULL;
	
	autoTimerReaderStreamDestroy(&pfw->atrStream);
	autoTimerReaderDestroy(&pfw->atr);
}

S32 pfwWriteFragmentedBuffer(	ProfileFileWriter* pfw,
								FragmentedBuffer* fb)
{
	U32 byteCount;
	
	if(!SAFE_MEMBER(pfw, f)){
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	fbGetSizeAsBytes(fb, &byteCount);
	
	pfwWriteU32(pfw, 0xfbfbfbfb);
	pfwWriteU32(pfw, byteCount);

	if(!pfw->fbr){
		fbReaderCreate(&pfw->fbr);
	}
	
	fbReaderAttach(pfw->fbr, fb, 1);
	{
		while(byteCount){
			U8	buffer[100];
			U32	curByteCount = MIN(byteCount, sizeof(buffer));
			
			fbReadBuffer(pfw->fbr, buffer, curByteCount);
			pfwWrite(pfw, buffer, curByteCount);
			byteCount -= curByteCount;
		}
	}
	fbReaderDetach(pfw->fbr);
	
	pfwWriteU32(pfw, ~0xfbfbfbfb);
	
	PERFINFO_AUTO_STOP();

	return 1;
}

S32 pfwReadReader(ProfileFileWriter* pfw){
	if(!pfw){
		return 0;
	}
	
	pfw->readSomething = 0;
	
	autoTimerReaderRead(pfw->atr);
						
	return pfw->readSomething;
}

S32	pfrCreate(ProfileFileReader** pfrOut){
	if(!pfrOut){
		return 0;
	}
	
	*pfrOut = callocStruct(ProfileFileReader);

	return 1;
}

S32	pfrDestroy(ProfileFileReader** pfrInOut){
	ProfileFileReader* pfr = SAFE_DEREF(pfrInOut);
	
	if(!pfr){
		return 0;
	}
	
	pfrStop(pfr);
	
	SAFE_FREE(*pfrInOut);
	
	return 1;
}

static S32 pfrRead(	ProfileFileReader* pfr,
					void* bytesOut,
					U32 byteCount)
{
	if(!pfr->f){
		return 0;
	}

	pfr->curReadBytes += byteCount;

	if(!fread(bytesOut, byteCount, 1, pfr->f)){
		pfrStop(pfr);
		return 0;
	}
	
	return 1;
}

static S32 pfrReadU32(	ProfileFileReader* pfr,
						U32* valueOut)
{
	U8 buffer[4];
	
	if(!pfrRead(pfr, buffer, 4)){
		return 0;
	}
	
	*valueOut = (U32)buffer[0] |
				((U32)buffer[1] << 8) |
				((U32)buffer[2] << 16) |
				((U32)buffer[3] << 24);
	
	return 1;
}

S32	pfrStart(	ProfileFileReader* pfr,
				const char* fileName,
				AutoTimerDecoder* d)
{
	S32 hasDecoderState;
	
	if(!pfr){
		return 0;
	}
	
	pfr->f = fopen(fileName, "rb");
	
	if(!pfr->f){
		char path[MAX_PATH];
		sprintf(path, "C:/CrypticSettings/Profiler/%s", fileName);
		pfr->f = fopen(path, "rb");
		if(!pfr->f){
			strcat(path, ".pf");
			pfr->f = fopen(path, "rb");
			if(!pfr->f){
				return 0;
			}
		}
	}
	
	if(	!pfrReadU32(pfr, &pfr->version) ||
		!pfrReadU32(pfr, &hasDecoderState))
	{
		return 0;
	}
	
	assert(pfr->version <= PROFILER_FILE_VERSION);

	assert(	hasDecoderState >= 0 &&
			hasDecoderState <= 1);
	
	if(hasDecoderState){
		if(!d){
			pfrStop(pfr);
			return 0;
		}else{
			FragmentedBuffer* fb;
			
			if(!pfrReadFragmentedBuffer(pfr, &fb)){
				pfrStop(pfr);
				return 0;
			}

			if(!autoTimerDecoderStateRead(d, fb, pfr->version)){
				pfrStop(pfr);
				return 0;
			}
		}
	}
	
	return 1;
}

S32 pfrStop(ProfileFileReader* pfr){
	if(pfr->f){
		fclose(pfr->f);
		pfr->f = NULL;
	}
	
	return 1;
}

S32	pfrReadFragmentedBuffer(ProfileFileReader* pfr,
							FragmentedBuffer** fbOut)
{
	S32					retVal = 1;
	U32					byteCount;
	FragmentedBuffer*	fb;
	
	if(	!pfr ||
		!pfr->f ||
		!fbOut)
	{
		return 0;
	}
	
	pfr->prevReadBytes = pfr->curReadBytes;
	
	fbCreate(&fb, 1);
	
	{
		U32 checkValue;
		if(!pfrReadU32(pfr, &checkValue)){
			retVal = 0;
		}else{
			assert(checkValue == 0xfbfbfbfb);
		}
	}
	
	if(!pfrReadU32(pfr, &byteCount)){
		retVal = 0;
	}else{
		while(byteCount){
			U8	buffer[100];
			U32	curByteCount = MIN(byteCount, sizeof(buffer));
			
			if(!pfrRead(pfr, buffer, curByteCount)){
				retVal = 0;
				break;
			}
			fbWriteBuffer(fb, buffer, curByteCount);
			byteCount -= curByteCount;
		}
	}
		
	{
		U32 checkValue;
		if(!pfrReadU32(pfr, &checkValue)){
			retVal = 0;
		}else{
			assert(checkValue == ~0xfbfbfbfb);
		}
	}
	
	if(!retVal){
		fbDestroy(&fb);
	}else{
		*fbOut = fb;
	}
	
	return retVal;
}

typedef struct TimerRecordThread {
	char*				fileName;
	S32					stop;
	S32					canFree;
	ProfileFileWriter*	pfw;
	HANDLE				eventWakeUp;
	
	struct {
		U32				waitForNewBuffer : 1;
	} flags;
} TimerRecordThread;

static TimerRecordThread* trtActive;

static void timerRecordThreadPFWMsgHandler(	ProfileFileWriter* pfw,
											ProfileFileWriterMsgType msgType,
											TimerRecordThread* trt)
{
	switch(msgType){
		xcase PFW_MSG_NEW_BUFFER_AVAILABLE:{
			SetEvent(trt->eventWakeUp);
		}
		
		xcase PFW_MSG_WAIT_FOR_NEW_BUFFER:{
			trt->flags.waitForNewBuffer = 1;
		}
	}
}

static S32 __stdcall timerRecordThreadMain(TimerRecordThread* trt){
	EXCEPTION_HANDLER_BEGIN
	
	pfwCreate(&trt->pfw, timerRecordThreadPFWMsgHandler, trt);
	pfwStart(trt->pfw, trt->fileName, 1, NULL);
	
	if(trt->pfw){
		while(!trt->stop){
			autoTimerThreadFrameBegin(__FUNCTION__);
			{
				U32 startTime = timeGetTime();
				
				while(!trt->stop){
					pfwReadReader(trt->pfw);
					
					if(TRUE_THEN_RESET(trt->flags.waitForNewBuffer)){
						PERFINFO_AUTO_START_BLOCKING("WaitForSingleObject:wakeUp", 1);
						WaitForSingleObject(trt->eventWakeUp, INFINITE);
						PERFINFO_AUTO_STOP();
					}

					// Force a frame every second.

					if(timeGetTime() - startTime >= 1000){
						break;
					}
				}
			}
			autoTimerThreadFrameEnd();
		}
		
		pfwDestroy(&trt->pfw);
	}

	while(!trt->stop){
		Sleep(100);
	}
	
	while(!trt->canFree){
		Sleep(1);
	}

	CloseHandle(trt->eventWakeUp);
	
	SAFE_FREE(trt->fileName);
	SAFE_FREE(trt);
	
	return 0;
	
	EXCEPTION_HANDLER_END
}

#endif // DISABLE_PERFORMANCE_COUNTERS

AUTO_COMMAND;
void profilerConnectPort(	const char* hostName,
							U32 hostPort)
{
	ProfilerConnect* pc;
	
	if(!hostName){
		return;
	}

	pc = callocStruct(ProfilerConnect);
	
	pc->hostName = strdup(hostName);
	pc->hostPort = hostPort;
	
	_beginthreadex(NULL, 0, profilerConnectThreadMain, pc, 0, NULL);
}

AUTO_COMMAND;
void profilerConnect(const char* hostName){
	profilerConnectPort(hostName, 0);
}

AUTO_COMMAND;
void timerTestStartWithoutStop(U32 count){
	MIN1(count, 100);

	while(count--){
		PERFINFO_AUTO_START_FUNC();
	}
}

AUTO_COMMAND;
void timerTrackCallStacks(S32 enabled){
	#if TRACK_CALL_STACKS
		autoTimers.trackCallStacks = !!enabled;
	#endif
}

AUTO_COMMAND;
void timerSetMaxDepth(U32 depth){
	autoTimerSetDepth(depth);
}

AUTO_COMMAND;
void timerRecordThreadStop(void){
	if(trtActive){
		trtActive->stop = 1;
		SetEvent(trtActive->eventWakeUp);
		trtActive->canFree = 1;
		trtActive = NULL;
	}
}

AUTO_COMMAND;
void timerRecordThreadStart(const char* fileName){
	char path[MAX_PATH];
	if(!SAFE_DEREF(fileName)){
		return;
	}

	timerRecordThreadStop();

	trtActive = callocStruct(TimerRecordThread);

	if (!fileIsAbsolutePath(fileName))
	{
		sprintf(path, "C:/CrypticSettings/Profiler/%s",
			fileName);
	} else {
		strcpy(path, fileName);
	}
	if (!strEndsWith(path, ".pf"))
		strcat(path, ".pf");
	mkdirtree(path);
	trtActive->fileName = strdup(path);
	
	trtActive->eventWakeUp = CreateEvent(NULL, FALSE, FALSE, NULL);

	_beginthreadex(NULL, 0, timerRecordThreadMain, trtActive, 0, NULL);
}

void timerSetAppName(const char* name){
	if(!name){
		return;
	}

	modifySequencingEnterCS();
	{
		autoTimers.appName = strdup(name);
		autoTimers.appNameCount++;
	}
	modifySequencingLeaveCS();
}

bool timerGetPIOverBudgetRecurse(PerformanceInfo* pNode,int iBudget,TimingProfilerReport paResults[10],int * piCount,int iFrames)
{
	PerformanceInfo*	pi = pNode;
	bool bFoundAny = false;
	while(pi)
	{
		// drill down
		if (!timerGetPIOverBudgetRecurse(pi->children.head,iBudget,paResults,piCount,iFrames))
		{
			int iTime = 0;

			iTime = pi->currentFrame.cyclesTotal/iFrames;

			// none of my children are over budget.  Am I?

			// I'm hard coding a pretty squirrelly assumption here.  If there are 20 or more hits for a gen (gens get multiple hits each frame), I'm going
			// to assume it has a more interesting parent, and so I'm going to consider it "not over-budget".  We'll see how this works, and perhaps adjust later.
			if (pi->currentFrame.count/iFrames < 20)
			{
				if(iTime > iBudget && *piCount < 10)
				{
					TimingProfilerReport * pReport = &paResults[*piCount];
					(*piCount)++;
					pReport->iTime = iTime;
					pReport->pchTimerName = pi->locName;
					bFoundAny = true;
				}
			}
		}
		else
		{
			bFoundAny = true;
		}

		pi->currentFrame.count = 0;
		pi->currentFrame.cyclesTotal = 0;

		pi = pi->nextSibling;
	}

	return bFoundAny;
}

int timerGetPIOverBudget(int iBudget,TimingProfilerReport paResults[10],TimerThreadData* td,int iFrames)
{
	PerformanceInfo*	stackTop = td->timerStack.top;
	PerformanceInfo*	pi;
	int iCount = 0;

	pi = td->curRoot->childTreeRoot;

	timerGetPIOverBudgetRecurse(pi,iBudget,paResults,&iCount,iFrames);

	return iCount;
}

void timerSetConnectedCallback(timerConnectedCallback cb)
{
	g_ConnectedCallback = cb;
}

TimerThreadData* timerMakeSpecialTD()
{
	TimerThreadData *td;
	static PerfInfoStaticData* piStatic;

	td = callocStruct(TimerThreadData);
	
	autoTimerRecursionDisableInc(td);
	
	td->instanceID = -9999;

	td->threadID = GetCurrentThreadId();
	
	td->curRoot = td->roots + td->flags.properlyStartedFrame;

	autoTimerRecursionDisableDec(td); // this is weird.  Why do I have to do this?

	PERFINFO_AUTO_START_STATIC_FORCE("Special", &piStatic, 1, td);

	return td;
}

///////////////////////////////////  ThreadSampler  ////////////////////////////////////////////////////

#if !PLATFORM_CONSOLE
typedef struct ThreadSampler {
	const void**	ips;
	U32				ipCount;
	U32				ipMaxCount;
	HANDLE			hEventStart;
	HANDLE			hThread;
	U32				tid;
	S32				stop;
	S32				endThread;
	S32				started;
	S32				done;
} ThreadSampler;

static S32 __stdcall threadSamplerThreadMain(ThreadSampler* ts){
	EXCEPTION_HANDLER_BEGIN
	
	while(1){
		autoTimerThreadFrameBegin(__FUNCTION__);
		
		WaitForSingleObject(ts->hEventStart, INFINITE);
		
		if(ts->endThread){
			break;
		}
		
		ts->ipCount = 0;
		
		while(	!ts->stop &&
				ts->ipCount < ts->ipMaxCount)
		{
			CONTEXT c;
			
			c.ContextFlags = ~0;
			
			SuspendThread(ts->hThread);
			GetThreadContext(ts->hThread, &c);
			ResumeThread(ts->hThread);
			
			#ifdef _WIN64
				ts->ips[ts->ipCount++] = (void*)(uintptr_t)c.Rip;
			#else
				ts->ips[ts->ipCount++] = (void*)(uintptr_t)c.Eip;
			#endif
			
			Sleep(0);
		}
		
		ts->done = 1;
		
		autoTimerThreadFrameEnd();
	}
	
	CloseHandle(ts->hEventStart);
	CloseHandle(ts->hThread);
	SAFE_FREE(ts->ips);
	SAFE_FREE(ts);
	
	EXCEPTION_HANDLER_END
	
	return 0;
}

void threadSamplerCreate(	ThreadSampler** tsOut,
							U32 maxSamples)
{
	ThreadSampler* ts;
	
	if(!tsOut){
		return;
	}
	
	ts = *tsOut = callocStruct(ThreadSampler);
	
	ts->ipMaxCount = maxSamples;
	ts->ips = callocStructs(void*, ts->ipMaxCount);
	
	ts->hEventStart = CreateEvent(NULL, FALSE, FALSE, NULL);
	
	_beginthreadex(NULL, 0, threadSamplerThreadMain, ts, 0, NULL);
}

void threadSamplerDestroy(ThreadSampler** tsInOut){
	ThreadSampler* ts = SAFE_DEREF(tsInOut);
	
	if(!ts){
		return;
	}
	
	threadSamplerStop(ts);
	
	ts->endThread = 1;
	SetEvent(ts->hEventStart);
	
	*tsInOut = NULL;
}

void threadSamplerSetThreadID(	ThreadSampler* ts,
								U32 tid)
{
	threadSamplerStop(ts);
	
	if(ts->hThread){
		CloseHandle(ts->hThread);
		ts->hThread = NULL;
		ts->tid = 0;
	}
	
	if(tid){
		ts->hThread = OpenThread(THREAD_GET_CONTEXT, FALSE, tid);
		
		if(ts->hThread){
			ts->tid = tid;
		}
	}
}

void threadSamplerStart(ThreadSampler* ts){
	if(	!ts->hThread ||
		!FALSE_THEN_SET(ts->started))
	{
		return;
	}
	
	ts->ipCount = 0;
	
	SetEvent(ts->hEventStart);
}

void threadSamplerStop(ThreadSampler* ts){
	if(TRUE_THEN_RESET(ts->started)){
		ts->stop = 1;

		while(!ts->done){
			Sleep(1);
		}
		
		ts->stop = 0;
		ts->done = 0;
	}
}

typedef struct ThreadSample {
	const void*	ip;
	U32			count;
	U32			currentRun;
	U32			longestRun;
	char*		modulePath;
	const void*	moduleBase;
	U32			moduleSize;
} ThreadSample;

static S32 compareThreadSample(	const ThreadSample** sample1,
								const ThreadSample** sample2)
{
	const void* ip1 = sample1[0]->ip;
	const void* ip2 = sample2[0]->ip;
	
	if(ip1 < ip2){
		return -1;
	}
	else if(ip1 > ip2){
		return 1;
	}else{
		return 0;
	}
}

static S32 threadSamplerForEachModuleCB(const ForEachModuleCallbackData* data){
	ThreadSample* sample = data->userPointer;
	
	if(	sample->ip >= data->baseAddress &&
		(char*)sample->ip < (char*)data->baseAddress + data->baseSize)
	{
		sample->modulePath = strdup(data->modulePath);
		sample->moduleBase = data->baseAddress;
		sample->moduleSize = data->baseSize;
	}
	
	return 1;
}

void threadSamplerReport(ThreadSampler* ts){
	StashTable		st = stashTableCreateAddress(10000);
	ThreadSample**	samples = NULL;
	ThreadSample*	samplePrev = NULL;
	
	FOR_BEGIN(i, (S32)ts->ipCount);
	{
		const void*		ip = ts->ips[i];
		ThreadSample*	sample;
		
		if(!stashFindPointer(st, ts->ips[i], &sample)){
			sample = callocStruct(ThreadSample);
			
			stashAddPointer(st, ts->ips[i], sample, false);

			sample->ip = ip;
			sample->count++;
			sample->currentRun++;
			
			forEachModule(	threadSamplerForEachModuleCB,
							GetCurrentProcessId(),
							sample);
			
			eaPush(&samples, sample);
		}else{
			sample->count++;

			if(sample == samplePrev){
				sample->currentRun++;
			}else{
				sample->currentRun = 1;
			}
		}
		
		samplePrev = sample;
		
		MAX1(sample->longestRun, sample->currentRun);
	}
	FOR_END;
	
	eaQSort(samples, compareThreadSample);
	
	EARRAY_CONST_FOREACH_BEGIN(samples, i, isize);
		ThreadSample* sample = samples[i];
		
		printf(	"0x%p: %5dx, %5d max run, (0x%8.8x in %s)\n",
				sample->ip,
				sample->count,
				sample->longestRun,
				(char*)sample->ip - (char*)sample->moduleBase,
				sample->modulePath);
		
		SAFE_FREE(sample->modulePath);
	EARRAY_FOREACH_END;
	
	printf("Total: %d\n", ts->ipCount);
	
	eaDestroyEx(&samples, NULL);
	stashTableDestroySafe(&st);
}
#endif
