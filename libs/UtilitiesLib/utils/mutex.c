#include "mutex.h"

#include "timing.h"
#include "timing_profiler_interface.h"
#include "strings_opt.h"
#include "sysutil.h"
#include "memlog.h"
#include "logging.h"
#include "wininclude.h"
#if !PLATFORM_CONSOLE
	#include <Tlhelp32.h>
#endif
#include "earray.h"
#include "XboxThreads.h"
#include "ThreadManager.h"
#include "process_util.h"
#include "utils.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:threadAgnosticMutexThread", BUDGET_EngineMisc););

#if _PS3
#else

static ManagedThread *threadAgnosticMutex_ptr;

typedef struct MutexEventPair {
	HANDLE hMutex;
	HANDLE hEvent;
	const char *debug_name; // Can not reference, pointer may go bad
} MutexEventPair;
static MutexEventPair **threadAgnosticMutex_actions=NULL; // Only modified/accessed in-thread

static DWORD WINAPI threadAgnosticMutexThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
	static volatile int count_of_threads;
	int result;
	result = InterlockedIncrement(&count_of_threads);
	assert(result==1); // Two of these threads started!
	for(;;)
	{
		int count;
		autoTimerThreadFrameBegin(__FUNCTION__);
		if (count=eaSize(&threadAgnosticMutex_actions)) {
			// Wait for these objects or a request
			static HANDLE *handles=NULL;
			static int handles_max=0;
			int i;
			DWORD ret;
			bool bAbandoned=false;
			dynArrayFit((void**)&handles, sizeof(HANDLE), &handles_max, count);
			for (i=0; i<count; i++) 
				handles[i] = threadAgnosticMutex_actions[i]->hMutex;
			ret = WaitForMultipleObjectsEx(count, handles, FALSE, INFINITE, TRUE);
			assert(ret != WAIT_FAILED);
			if (ret != WAIT_IO_COMPLETION) {
				assert(eaSize(&threadAgnosticMutex_actions)==count); // Otherwise we processed an APC while waiting!
			}
			if (ret >= WAIT_ABANDONED_0 && ret < WAIT_ABANDONED_0 + count) {
				memlog_printf(NULL, "threadAgnosticMutexThread():Mutex abandoned!");
				filelog_printf("mutex.log", "Mutex abandoned, noticed by pid:%d", getpid());
				logWaitForQueueToEmpty();
				bAbandoned = true;
				ret = ret - WAIT_ABANDONED_0 + WAIT_OBJECT_0;
			}
			if (ret >= WAIT_OBJECT_0 && ret < WAIT_OBJECT_0 + count) {
				MutexEventPair *pair;
				i = ret - WAIT_OBJECT_0;
				// Finished waiting on index i
				pair = eaRemove(&threadAgnosticMutex_actions, i);
				// Signal waiting thread
				SetEvent(pair->hEvent); // Cannot reference pair after this point
			}
		} else {
			// Wait for a request
			SleepEx(INFINITE, TRUE);
		}
		//memlog_printf(NULL, "threadAgnosticMutexThread():Returned from waiting");
		autoTimerThreadFrameEnd();
	}
	return 0; 
	EXCEPTION_HANDLER_END
} 

static VOID CALLBACK threadAgnosticMutexAcquireFunc( ULONG_PTR dwParam)
{
	MutexEventPair *pair = (MutexEventPair*)dwParam;
	PERFINFO_AUTO_START("mutex acquire", 1);
	//memlog_printf(NULL, "threadAgnosticMutexAcquireFunc(%08x):WaitForSingleObject", pair->hMutex);
	eaPush(&threadAgnosticMutex_actions, pair);
	PERFINFO_AUTO_STOP();
}

static VOID CALLBACK threadAgnosticMutexReleaseFunc( ULONG_PTR dwParam)
{
	MutexEventPair *pair = (MutexEventPair*)dwParam;
	BOOL result;
	DWORD ret;
	PERFINFO_AUTO_START("mutex release", 1);
	//memlog_printf(NULL, "threadAgnosticMutexReleaseFunc(%08x):Release", pair->hMutex);
	result = ReleaseMutex(pair->hMutex);
	assert(result);
	//memlog_printf(NULL, "threadAgnosticMutexReleaseFunc(%08x):SetEvent", pair->hMutex);
	ret = SetEvent(pair->hEvent);
	//memlog_printf(NULL, "threadAgnosticMutexReleaseFunc(%08x):Done, returned %d", pair->hMutex, ret);
	PERFINFO_AUTO_STOP();
}

DWORD WaitForEventInfiniteSafe(HANDLE hEvent)
{
	DWORD result;
	int retries=5;
	do {
		WaitForSingleObjectWithReturn(hEvent, INFINITE, result);
		if (result == WAIT_FAILED) {
			DWORD err = GetLastError();
			UNUSED(err);
			devassertmsg(0, "WaitForSingleObject failed");
			Sleep(100);
			retries--;
			if (retries<=0) {
				assertmsg(0, "WaitForSingleObject failed 5 times");
			}
		} else {
			assert(result==WAIT_OBJECT_0);
		}
	} while (result!=WAIT_OBJECT_0);
	return result;
}

// Generally only have one of these in flight at a time, so use a static one
static volatile int static_mutex_pool_inuse=0;
static MutexEventPair static_mutex_pool;

ThreadAgnosticMutex acquireThreadAgnosticMutex(const char *name)
{
	static bool inited=false;
	MutexEventPair *pair;
	char name_fixed[MAX_PATH];

	int static_mutex_try = InterlockedIncrement(&static_mutex_pool_inuse);
	if (static_mutex_try == 1) {
		pair = &static_mutex_pool;
	} else {
		InterlockedDecrement(&static_mutex_pool_inuse);
		pair = malloc(sizeof(MutexEventPair));
	}

	assert(name);

	if (!inited) {
		static volatile int num_people_initing=0;
		int result = InterlockedIncrement(&num_people_initing);
		if (result != 1) {
			while (!inited)
				Sleep(1);
		} else {
			// First person trying to init this
			threadAgnosticMutex_ptr = tmCreateThread(threadAgnosticMutexThread, NULL);
			assert(threadAgnosticMutex_ptr);
			tmSetThreadProcessorIdx(threadAgnosticMutex_ptr, THREADINDEX_MUTEX);
			tmSetThreadRelativePriority(threadAgnosticMutex_ptr, 1);
			inited = true;
		}
	}
	strcpy(name_fixed, name);
	{
		char *s = strchr(name_fixed, '\\'); // Skip past Global\ 
		if (!s)
			s = name_fixed;
		strupr(s);
	}
	pair->hMutex = CreateMutex_UTF8(NULL, FALSE, name_fixed); // initially not owned
	assert(pair->hMutex);
	pair->hEvent = CreateEvent( NULL, FALSE, FALSE, NULL); // Auto-reset, not signaled
	assert(pair->hEvent != NULL);
	pair->debug_name = name;
	//memlog_printf(NULL, "acquireThreadAgnosticMutex(%08x,%08x)", pair->hMutex, pair->hEvent);

	tmQueueUserAPC(threadAgnosticMutexAcquireFunc, threadAgnosticMutex_ptr, (ULONG_PTR)pair );
	//memlog_printf(NULL, "acquireThreadAgnosticMutex(%08x):Waiting", pair->hMutex);
	WaitForEventInfiniteSafe(pair->hEvent);
	//memlog_printf(NULL, "acquireThreadAgnosticMutex(%08x):Done", pair->hMutex);

	return (ThreadAgnosticMutex)pair;
}

void releaseThreadAgnosticMutex(ThreadAgnosticMutex hPair)
{
	MutexEventPair *pair = (MutexEventPair*)hPair;
	DWORD ret;
	//memlog_printf(NULL, "releaseThreadAgnosticMutex(%08x)", pair->hMutex);
	ret = tmQueueUserAPC(threadAgnosticMutexReleaseFunc, threadAgnosticMutex_ptr, (ULONG_PTR)pair );
	//memlog_printf(NULL, "releaseThreadAgnosticMutex(%08x):Waiting (Queue returned %d)", pair->hMutex, ret);
	WaitForEventInfiniteSafe(pair->hEvent);
	//memlog_printf(NULL, "releaseThreadAgnosticMutex(%08x):Done", pair->hMutex);
	CloseHandle(pair->hMutex);
	CloseHandle(pair->hEvent);
	if (pair == &static_mutex_pool) {
		InterlockedDecrement(&static_mutex_pool_inuse);
	} else {
		free(pair);
	}
}

#define TTAM_NUM_TO_LAUNCH 3
#define TTAM_NUM_MUTEXES 2
#define TTAM_NUM_THREADS 3
static int ttam_timer;
static F32 ttam_last_result[TTAM_NUM_THREADS];

static void testThreadAgnosticMutexThread(void *dwParam)
{
	int threadnum = PTR_TO_S32(dwParam);
	do {
		char name[10];
		int mutex = rand() * TTAM_NUM_MUTEXES / (RAND_MAX + 1);
		ThreadAgnosticMutex h;
		sprintf(name, "TTAM_%d", mutex);
		h = acquireThreadAgnosticMutex(name);
		Sleep(rand() * 100 / (RAND_MAX + 1));
		releaseThreadAgnosticMutex(h);
		ttam_last_result[threadnum] = timerElapsed(ttam_timer);
	} while (true);
}

#endif

//make a string into a string that is a legal mutex name 
void makeLegalMutexName(char mutexName[CRYPTIC_MAX_PATH], const char *pInName)
{
	char *pChar = mutexName;
	strcpy_s(mutexName, CRYPTIC_MAX_PATH, pInName);

	while (*pChar)
	{
		if (!(isalnum(*pChar) || *pChar == '_'))
		{
			*pChar = '_';
		}

		pChar++;
	}
}
	
// Read/Write locks
// Properties
// If any writer is waiting, it supersedes all waiting readers.
// Locks are not necessarily fair. When it becomes available for writing, any waiting writer could get the lock.

#define LOCK_U32_WRITE_OWNED_BIT				BIT(31)
#define LOCK_U32_WRITE_WAITING_BIT				BIT(16)
#define LOCK_U32_READER_COUNT					(LOCK_U32_WRITE_WAITING_BIT - 1)
#define LOCK_U32_READER_COUNT_HIGH_BIT			(LOCK_U32_WRITE_WAITING_BIT >> 1)
#define LOCK_U32_WRITERS_OWN_OR_ARE_WAITING		(~LOCK_U32_READER_COUNT)
#define LOCK_U32_CHECK_ALIGNMENT(lock)			(assert(!((intptr_t)lock & 0x3)))

void readLockU32(U32* lock){
	S32 firstTry = 1;
	
	LOCK_U32_CHECK_ALIGNMENT(lock);
	
	while(1){
		U32 newValue;
		U32 sleepCount = 0;
		
		// Sleep while any writers own or are waiting for lock.
		
		while(*lock & LOCK_U32_WRITERS_OWN_OR_ARE_WAITING){
			if(firstTry){
				PERFINFO_AUTO_START("readLockU32:SleepForWriter", 1);
				if(sleepCount < 1000){
					sleepCount++;
					Sleep(0);
				}else{
					Sleep(1);
				}
				PERFINFO_AUTO_STOP();
			}else{
				PERFINFO_AUTO_START("readLockU32:SleepForWriter(secondary)", 1);
				Sleep(1);
				PERFINFO_AUTO_STOP();
			}
		}

		newValue = InterlockedIncrement(lock);
		
		if(!(newValue & LOCK_U32_WRITERS_OWN_OR_ARE_WAITING)){
			// No writers own or are waiting for lock.
			
			break;
		}
		
		firstTry = 0;
		newValue = InterlockedDecrement(lock);
		
		assert(!(newValue & LOCK_U32_READER_COUNT_HIGH_BIT));
	}
}

void readUnlockU32(U32* lock){
	LOCK_U32_CHECK_ALIGNMENT(lock);

	InterlockedDecrement(lock);
}

// Different from InterlockedExchangeAdd because it returns the new value, not the old value
static U32 customInterlockedAdd(volatile U32* lock,
								S32 addThis)
{
	while(1){
		U32 valueToReplace = *lock;
		U32 valueAttemptToSet = valueToReplace + addThis;
		U32 valueReplaced;

		valueReplaced = InterlockedCompareExchange(	lock,
													valueAttemptToSet,
													valueToReplace);

		if(valueReplaced == valueToReplace){
			return valueAttemptToSet;
		}
	}
}

void writeLockU32(	U32* lock,
					U32 hasReadLock)
{
	U32 curHasReadLock;
	U32 sleepCount = 0;

	LOCK_U32_CHECK_ALIGNMENT(lock);

	curHasReadLock = hasReadLock = !!hasReadLock;

	while(1){
		U32 prevValue;
		
		prevValue = _InterlockedOr(lock, LOCK_U32_WRITE_OWNED_BIT);
		
		if(!(prevValue & LOCK_U32_WRITE_OWNED_BIT)){
			// Got write lock, wait for all other readers to go away.

			while(1){
				if((*lock & LOCK_U32_READER_COUNT) == curHasReadLock){
					// Reader count is correct.

					break;
				}
				
				// Wait for non-me readers to finish.

				PERFINFO_AUTO_START("writeLockU32:SleepForReaders", 1);
				if(sleepCount < 1000){
					sleepCount++;
					Sleep(0);
				}else{
					Sleep(1);
				}
				PERFINFO_AUTO_STOP();
			}
			
			// Add back in my reader count, if I previously removed it, and remove writer wait.
			
			if(curHasReadLock != hasReadLock){
				U32 newValue = customInterlockedAdd(lock, hasReadLock - LOCK_U32_WRITE_WAITING_BIT);

				// Check the reader count is at least hasReadLock.
				
				assert((newValue & LOCK_U32_READER_COUNT) >= hasReadLock);
			}
			
			break;
		}else{
			// Another writer has the lock, so remove my readers and mark me as waiting for write.
			
			if(TRUE_THEN_RESET(curHasReadLock)){
				U32 newValue;

				// Check that the caller didn't lie about having a read lock.

				assert((*lock & LOCK_U32_READER_COUNT) >= hasReadLock);

				newValue = customInterlockedAdd(lock, LOCK_U32_WRITE_WAITING_BIT - hasReadLock);

				// Check again that the caller didn't lie about having a read lock.

				assert(!(newValue & LOCK_U32_READER_COUNT_HIGH_BIT));
			}

			// Wait for writer to finish.

			PERFINFO_AUTO_START("writeLockU32:SleepForWriter", 1);
			if(sleepCount < 1000){
				sleepCount++;
				Sleep(0);
			}else{
				Sleep(1);
			}
			PERFINFO_AUTO_STOP();
		}
	}
}

void writeUnlockU32(U32* lock){
	LOCK_U32_CHECK_ALIGNMENT(lock);

	_InterlockedAnd(lock, ~LOCK_U32_WRITE_OWNED_BIT);
}

typedef struct ReadWriteLock
{
	U32 lock;
	HANDLE TriggerReaders;
	HANDLE TriggerNextWriter;
} ReadWriteLock;

void InitializeReadWriteLock(ReadWriteLock *rwl)
{
	assert(rwl);
	rwl->lock = 0;
	rwl->TriggerReaders = CreateEvent(0, 1, 1, 0);
	rwl->TriggerNextWriter = CreateEvent(0, 1, 1, 0);
}

// If anyone ends up using this in a situation that requires creating and destroying many of these, we should make this use TSMP
ReadWriteLock *CreateReadWriteLock(void)
{
	ReadWriteLock *rwl;
	rwl = callocStruct(ReadWriteLock);
	InitializeReadWriteLock(rwl);
	return rwl;
}

void DestroyReadWriteLock(ReadWriteLock *rwl)
{
	free(rwl);
}

#define LOCK_RWL_WRITE_OWNED_BIT				BIT(31)
#define LOCK_RWL_WRITE_WAITING_BIT				BIT(16)
#define LOCK_RWL_READER_COUNT					(LOCK_RWL_WRITE_WAITING_BIT - 1)
#define LOCK_RWL_READER_COUNT_HIGH_BIT			(LOCK_RWL_WRITE_WAITING_BIT >> 1)
#define LOCK_RWL_WRITERS_OWN_OR_ARE_WAITING		(~LOCK_RWL_READER_COUNT)
#define LOCK_RWL_CHECK_ALIGNMENT(lock)			(assert(!((intptr_t)lock & 0x3)))
#define LOCK_RWL_WAIT_TIMEOUT					INFINITE // These read/write locks should always wait for a signal

#if DEBUG_RWL_TIMEOUTS
static U32 lockRequests = 0;
static U32 waitTimeouts = 0;

void printTimeouts(void)
{
	printf("timeouts: %u/%u\n", waitTimeouts, lockRequests);
}
#endif

void rwlReadLock(ReadWriteLock* rwl)
{
	S32 firstTry = 1;
	
	LOCK_RWL_CHECK_ALIGNMENT(&rwl->lock);
	
	while(1)
	{
		U32 newValue;
		U32 sleepCount = 0;
		
		// Sleep while any writers own or are waiting for lock.
		
		while(rwl->lock & LOCK_U32_WRITERS_OWN_OR_ARE_WAITING)
		{
			U32 ret;
#if DEBUG_RWL_TIMEOUTS
			++lockRequests;
#endif
			WaitForSingleObjectWithReturn(rwl->TriggerReaders, LOCK_RWL_WAIT_TIMEOUT, ret);
#if DEBUG_RWL_TIMEOUTS
			if(ret == WAIT_TIMEOUT)
				++waitTimeouts;
#endif
		}

		newValue = InterlockedIncrement(&rwl->lock);
		
		if(!(newValue & LOCK_U32_WRITERS_OWN_OR_ARE_WAITING))
		{
			// No writers own or are waiting for lock.
			
			break;
		}
		
		firstTry = 0;
		newValue = InterlockedDecrement(&rwl->lock);
		
		assert(!(newValue & LOCK_U32_READER_COUNT_HIGH_BIT));
	}
	// Readers have the lock, writers should sleep
	ResetEvent(rwl->TriggerNextWriter);
}

void rwlReadUnlock(ReadWriteLock* rwl)
{
	LOCK_RWL_CHECK_ALIGNMENT(&rwl->lock);

	InterlockedDecrement(&rwl->lock);
	SetEvent(rwl->TriggerNextWriter); // Wake up writer in case there is one waiting
	SetEvent(rwl->TriggerReaders);
}

void rwlWriteLock(ReadWriteLock* rwl, U32 hasReadLock)
{
	U32 curHasReadLock;
	U32 sleepCount = 0;

	LOCK_RWL_CHECK_ALIGNMENT(&rwl->lock);

	curHasReadLock = hasReadLock = !!hasReadLock;

	while(1)
	{
		U32 prevValue;
		
		prevValue = _InterlockedOr(&rwl->lock, LOCK_U32_WRITE_OWNED_BIT);
		
		if(!(prevValue & LOCK_U32_WRITE_OWNED_BIT)){
			// Got write lock, wait for all other readers to go away.

			// New readers should sleep until the writer is done.
			ResetEvent(rwl->TriggerReaders);
			while(1)
			{
				U32 ret;
				if((rwl->lock & LOCK_U32_READER_COUNT) == curHasReadLock)
				{
					// Reader count is correct.
					break;
				}
				
				// Wait for non-me readers to finish.
#if DEBUG_RWL_TIMEOUTS
				++lockRequests;
#endif
				WaitForSingleObjectWithReturn(rwl->TriggerNextWriter, LOCK_RWL_WAIT_TIMEOUT, ret);
#if DEBUG_RWL_TIMEOUTS
				if(ret == WAIT_TIMEOUT)
					++waitTimeouts;
#endif
			}
			
			// Add back in my reader count, if I previously removed it, and remove writer wait.
			
			if(curHasReadLock != hasReadLock){
				U32 newValue = customInterlockedAdd(&rwl->lock, hasReadLock - LOCK_U32_WRITE_WAITING_BIT);

				// Check the reader count is at least hasReadLock.
				
				assert((newValue & LOCK_U32_READER_COUNT) >= hasReadLock);
			}
			
			break;
		}else{
			// Another writer has the lock, so remove my readers and mark me as waiting for write.
			U32 ret;
			if(TRUE_THEN_RESET(curHasReadLock)){
				U32 newValue;

				// Check that the caller didn't lie about having a read lock.

				assert((rwl->lock & LOCK_U32_READER_COUNT) >= hasReadLock);

				newValue = customInterlockedAdd(&rwl->lock, LOCK_U32_WRITE_WAITING_BIT - hasReadLock);

				// Check again that the caller didn't lie about having a read lock.

				assert(!(newValue & LOCK_U32_READER_COUNT_HIGH_BIT));
			}

			// Wait for writer to finish.

#if DEBUG_RWL_TIMEOUTS
			++lockRequests;
#endif
			WaitForSingleObjectWithReturn(rwl->TriggerNextWriter, LOCK_RWL_WAIT_TIMEOUT, ret);
#if DEBUG_RWL_TIMEOUTS
			if(ret == WAIT_TIMEOUT)
				++waitTimeouts;
#endif
		}
	}
	// A writer has the lock. Everyone else should sleep
	ResetEvent(rwl->TriggerNextWriter);
	ResetEvent(rwl->TriggerReaders);
}

void rwlWriteUnlock(ReadWriteLock* rwl)
{
	LOCK_RWL_CHECK_ALIGNMENT(&rwl->lock);

	_InterlockedAnd(&rwl->lock, ~LOCK_U32_WRITE_OWNED_BIT);
	SetEvent(rwl->TriggerNextWriter); // writers first so they have higher priority.
	SetEvent(rwl->TriggerReaders);
}

#if !PLATFORM_CONSOLE
// usage: put return testThreadAgnosticMutex(argc, argv); in main()
int testThreadAgnosticMutex(int argc, char **argv)
{
	int i;
	if (argc==1) {
		// launcher
		for (i=0; i<TTAM_NUM_TO_LAUNCH; i++) {
			char buf[CRYPTIC_MAX_PATH];
			sprintf(buf, "%s %d", getExecutableName(), i);
			system_detach(buf, 0, 0);
			Sleep(10);
		}
		return 0;
	} else {
		srand(_time32(NULL));
		ttam_timer = timerAlloc();
		for (i=0; i<TTAM_NUM_THREADS; i++) {
			_beginthread(testThreadAgnosticMutexThread, 0, S32_TO_PTR(i));
		}
		do {
			for (i=0; i<TTAM_NUM_THREADS; i++) {
				printf("%4.1f ", ttam_last_result[i]);
			}
			printf("\n");
			Sleep(500);
		} while (true);
	}
	return 0;
}

HANDLE acquireMutexHandleByPID(const char* prefix, S32 pid, const char* suffix)
{
	char	buffer[1000];
	HANDLE	hMutex;
	
	if(pid < 0){
		pid = _getpid();
	}
	
	STR_COMBINE_SSDS(buffer, "Global\\", prefix ? prefix : "", pid, suffix ? suffix : "");

	hMutex = CreateMutex_UTF8(NULL, TRUE, buffer);

	if(hMutex){
		if(GetLastError() == ERROR_ALREADY_EXISTS){
			// Another process created it first.
			
			CloseHandle(hMutex);
		}else{
			// I got it!
			
			return hMutex;
		}
	}else{
		// Another process has it, and I don't have access rights.  Do nothing.
	}

	return NULL;
}

typedef struct CountMutexesData {
	U32			count;
	const char* prefix;
	const char* suffix;
} CountMutexesData;

static S32 countMutexesByPIDCallback(const ForEachProcessCallbackData* data){
	CountMutexesData*	cmd = data->userPointer;
	HANDLE				hMutex = acquireMutexHandleByPID(	cmd->prefix,
															data->pid,
															cmd->suffix);

	if(hMutex){
		ReleaseMutex(hMutex);
		CloseHandle(hMutex);
	}else{
		cmd->count++;
	}
	
	return 1;
}

S32 countMutexesByPID(const char* prefix, const char* suffix)
{
	CountMutexesData cmd = {0};
	
	cmd.prefix = prefix;
	cmd.suffix = suffix;

	forEachProcess(countMutexesByPIDCallback, &cmd);
	
	return cmd.count;
}
#endif

#if !_PS3

typedef struct WaitThread WaitThread;

typedef struct WaitThreadUserData {
	void*						userPointer;
	WaitThreadGroupCallback		callback;
} WaitThreadUserData;

typedef struct WaitThread {
	WaitThread*					next;
	HANDLE						hThread;
	U32							threadID;
	
	struct {
		HANDLE					eventSharedStart;
		HANDLE					eventFrameFinished;
	} waitFor;
	
	struct {
		HANDLE					eventStopped;
		HANDLE					eventSharedWaitFinished;
	} set;
	
	HANDLE						handles[MAXIMUM_WAIT_OBJECTS - 1];
	WaitThreadUserData			handleUserDatas[MAXIMUM_WAIT_OBJECTS - 1];
	U32							handleCount;
	U32							handleResultIndex;
} WaitThread;

typedef struct WaitThreadGroup {
	WaitThread					wtHead;
	HANDLE						eventSharedStop;
	
	struct {
		U32						isWaiting			: 1;
		U32						threadsAreWaiting	: 1;
		U32						needsBeginThread	: 1;
	} flags;
} WaitThreadGroup;

static S32 __stdcall wtgThreadMain(WaitThread* wt){
	EXCEPTION_HANDLER_BEGIN

	wt->threadID = GetCurrentThreadId();
	
	autoTimerThreadFrameBegin(__FUNCTION__);

	while(1){
		if(wt->handleCount){
			assert(wt->handleCount <= ARRAY_SIZE(wt->handles));

			PERFINFO_AUTO_START_BLOCKING("waitForHandles", 1);
			wt->handleResultIndex = WaitForMultipleObjects(	wt->handleCount,
															wt->handles,
															0,
															INFINITE);
			assert(wt->handleResultIndex != WAIT_FAILED);
			PERFINFO_AUTO_STOP();
			
			autoTimerThreadFrameEnd();
			autoTimerThreadFrameBegin(__FUNCTION__);
			
			PERFINFO_AUTO_START_BLOCKING("SetEvent:waitFinished", 1);
			SetEvent(wt->set.eventSharedWaitFinished);
			PERFINFO_AUTO_STOP();
		}
		
		PERFINFO_AUTO_START_BLOCKING("SetEvent:stopped", 1);
		SetEvent(wt->set.eventStopped);
		PERFINFO_AUTO_STOP();
		
		PERFINFO_AUTO_START_BLOCKING("WFSO:frameFinished", 1);
		WaitForSingleObject(wt->waitFor.eventFrameFinished, INFINITE);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START_BLOCKING("WFSO:start", 1);
		WaitForSingleObject(wt->waitFor.eventSharedStart, INFINITE);
		PERFINFO_AUTO_STOP();
	}
	
	EXCEPTION_HANDLER_END
	
	return 0;
}

S32 wtgCreate(WaitThreadGroup** wtgOut){
	WaitThreadGroup*	wtg;
	WaitThread*			wt;

	if(!wtgOut){
		return 0;
	}
	
	wtg = *wtgOut = callocStruct(WaitThreadGroup);
	wt = &wtg->wtHead;
	
	wtg->eventSharedStop = CreateEvent(NULL, TRUE, FALSE, NULL);
	wt->waitFor.eventSharedStart = CreateEvent(NULL, TRUE, FALSE, NULL);
	wt->set.eventSharedWaitFinished = CreateEvent(NULL, TRUE, FALSE, NULL);
	
	// The first handle has to be the shared wait-finished event.

	wt->handles[wt->handleCount++] = wt->set.eventSharedWaitFinished;

	return 1;
}

static void wtgStopThreads(WaitThreadGroup* wtg){
	WaitThread* wtHead = &wtg->wtHead;
	WaitThread* wtCur;

	assert(!wtg->flags.isWaiting);

	if(!TRUE_THEN_RESET(wtg->flags.threadsAreWaiting)){
		return;
	}

	// Tell all the threads that haven't stopped yet to stop.

	PERFINFO_AUTO_START("SetEvent:stop", 1);
	SetEvent(wtg->eventSharedStop);
	PERFINFO_AUTO_STOP();
		
	// Wait for all the threads to stop.

	PERFINFO_AUTO_START("waitForWaitThreads", 1);
	for(wtCur = wtHead->next; wtCur; wtCur = wtCur->next){
		WaitForSingleObject(wtCur->set.eventStopped, INFINITE);
	}
	PERFINFO_AUTO_STOP();
		
	// Reset the shared events.
		
	ResetEvent(wtg->eventSharedStop);
	ResetEvent(wtHead->set.eventSharedWaitFinished);
	ResetEvent(wtHead->waitFor.eventSharedStart);

	// Advance to waiting for start.

	PERFINFO_AUTO_START("queuedThreadsToStart", 1);
	for(wtCur = wtHead->next; wtCur; wtCur = wtCur->next){
		SetEvent(wtCur->waitFor.eventFrameFinished);
	}
	PERFINFO_AUTO_STOP();
}

S32 wtgAddHandle(	WaitThreadGroup* wtg,
					HANDLE handle,
					WaitThreadGroupCallback callback,
					void* userPointer)
{
	WaitThread* wt;
	WaitThread*	wtPrev;
	
	if(	!wtg ||
		!handle ||
		!callback)
	{
		return 0;
	}

	wtgStopThreads(wtg);

	for(wt = &wtg->wtHead;
		wt;
		wt = wt->next)
	{
		FOR_BEGIN(i, (S32)wt->handleCount);
		{
			assert(wt->handles[i] != handle);
		}
		FOR_END;
	}

	for(wt = &wtg->wtHead, wtPrev = NULL;
		wt;
		wtPrev = wt, wt = wt->next)
	{
		if(wt->handleCount < ARRAY_SIZE(wt->handles)){
			break;
		}
	}
	
	if(!wt){
		// Start a new wait thread and wait for it to block.

		wt = wtPrev->next = callocStruct(WaitThread);
		
		wt->set.eventSharedWaitFinished = wtg->wtHead.set.eventSharedWaitFinished;
		wt->set.eventStopped = CreateEvent(NULL, FALSE, FALSE, NULL);
		wt->waitFor.eventFrameFinished = CreateEvent(NULL, FALSE, FALSE, NULL);
		wt->waitFor.eventSharedStart = wtg->wtHead.waitFor.eventSharedStart;
		
		wtg->flags.needsBeginThread = 1;

		// The first handle has to be the shared stop event.

		wt->handles[wt->handleCount++] = wtg->eventSharedStop;
	}
	
	wt->handles[wt->handleCount] = handle;
	wt->handleUserDatas[wt->handleCount].userPointer = userPointer;
	wt->handleUserDatas[wt->handleCount].callback = callback;
	wt->handleCount++;

	return 1;
}

void wtgRemoveHandle(	WaitThreadGroup* wtg,
						HANDLE handle,
						void* userPointer)
{
	WaitThread* wt;
	
	if(	!wtg ||
		!handle)
	{
		return;
	}

	wtgStopThreads(wtg);

	for(wt = &wtg->wtHead;
		wt;
		wt = SAFE_MEMBER(wt, next))
	{
		FOR_BEGIN_FROM(i, 1, (S32)wt->handleCount);
		{
			if(wt->handles[i] == handle){
				assert(i);
				assert(wt->handleUserDatas[i].userPointer == userPointer);
				
				wt->handleCount--;
				wt->handles[i] = wt->handles[wt->handleCount];
				wt->handleUserDatas[i] = wt->handleUserDatas[wt->handleCount];
				
				if(wt->handleResultIndex == (U32)i){
					wt->handleResultIndex = 0;
				}
				else if(wt->handleResultIndex == wt->handleCount){
					wt->handleResultIndex = i;
				}
				
				wt = NULL;

				break;
			}
		}
		FOR_END;
	}
}

void wtgCheckThreadResult(	WaitThread* wt,
							void* userPointer)
{
	if(!wt->handleResultIndex){
		// Thread was told to stop, so ignore it.
	}else{
		// One of the handles finished waiting, so send a callback.
				
		U32 i;
		
		PERFINFO_AUTO_START_FUNC();

		i = wt->handleResultIndex;
		wt->handleResultIndex = 0;
				
		assert(i < wt->handleCount);

		wt->handleUserDatas[i].callback(	userPointer,
											wt->handles[i],
											wt->handleUserDatas[i].userPointer);

		PERFINFO_AUTO_STOP();
	}
}

void wtgWait(	WaitThreadGroup* wtg,
				void* userPointer,
				U32 msTimeout)
{
	WaitThread* wtCur;
	WaitThread* wtHead;

	if(!wtg){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	wtHead = &wtg->wtHead;

	if(TRUE_THEN_RESET(wtg->flags.needsBeginThread)){
		for(wtCur = wtHead->next; wtCur; wtCur = wtCur->next){
			if(!wtCur->hThread){
				U32 handleCount = wtCur->handleCount;
				
				wtCur->handleCount = 0;
				
				wtCur->hThread = (HANDLE)_beginthreadex(	NULL,
															0,
															wtgThreadMain,
															wtCur,
															0,
															NULL);
				
				WaitForSingleObject(wtCur->set.eventStopped, INFINITE);
				wtCur->handleCount = handleCount;
				SetEvent(wtCur->waitFor.eventFrameFinished);
			}
		}
	}

	while(1){
		// Tell all the threads to start.
	
		if(	wtHead->next &&
			!wtg->flags.threadsAreWaiting)
		{
			// Check if any of the threads signaled.
		
			for(wtCur = wtHead->next; wtCur; wtCur = wtCur->next){
				wtgCheckThreadResult(wtCur, userPointer);
			}

			ASSERT_FALSE_AND_SET(wtg->flags.threadsAreWaiting);

			PERFINFO_AUTO_START("SetEvent:start", 1);
			SetEvent(wtHead->waitFor.eventSharedStart);
			PERFINFO_AUTO_STOP();
		}
	
		// Wait for something to happen.

		PERFINFO_AUTO_START_BLOCKING("waitForHandles", 1);
		{
			ASSERT_FALSE_AND_SET(wtg->flags.isWaiting);
			wtHead->handleResultIndex = WaitForMultipleObjects(	wtHead->handleCount,
																wtHead->handles,
																0,
																msTimeout);
			assert(wtHead->handleResultIndex != WAIT_FAILED);
			ASSERT_TRUE_AND_RESET(wtg->flags.isWaiting);
		}
		PERFINFO_AUTO_STOP();
	
		if(wtHead->handleResultIndex == WAIT_TIMEOUT){
			break;
		}

		if(!wtHead->handleResultIndex){
			// One of the threads has a result, so stop all of them.

			wtgStopThreads(wtg);
		}else{
			wtgCheckThreadResult(wtHead, userPointer);
		}
	
		msTimeout = 0;
	}
	
	PERFINFO_AUTO_STOP();
}

#endif

static void csCreateEvent(CrypticalSection* cs){
	PERFINFO_AUTO_START_FUNC();

	if(!InterlockedExchange(&cs->createEventLock, 1)){
		// Was first so create the event.

		if(!cs->eventWait){
			cs->eventWait = CreateEvent(NULL, FALSE, FALSE, NULL);
		}
	}else{
		// Another thread was first, so wait for the event to exist.

		while(!cs->eventWait){
			Sleep(0);
		}
	}
	
	PERFINFO_AUTO_STOP();
}

void csEnter(CrypticalSection* cs){
	const U32 threadID = GetCurrentThreadId();
	
	if(cs->ownerThreadID == threadID){
		// This thread is already inside the lock, so increment the count.

		assert(cs->sharedEnterCount);
		assert(cs->ownerEnterCount);
		cs->ownerEnterCount++;
	}
	else if(InterlockedIncrement(&cs->sharedEnterCount) == 1){
		// Got the lock, so we're done.

		cs->ownerThreadID = threadID;
		cs->ownerEnterCount = 1;

		if(PERFINFO_RUN_CONDITIONS){
			GET_CPU_TICKS_64(cs->cyclesStart);
		}
	}else{
		// Couldn't get the lock and this thread doesn't own it, so wait for it.
		
		if(!cs->eventWait){
			csCreateEvent(cs);
		}
		
		WaitForSingleObject(cs->eventWait, INFINITE);
		
		// This thread now owns the CS.
		
		assert(!cs->ownerThreadID);
		assert(!cs->ownerEnterCount);
		cs->ownerThreadID = threadID;
		cs->ownerEnterCount = 1;

		if(PERFINFO_RUN_CONDITIONS){
			GET_CPU_TICKS_64(cs->cyclesStart);
		}
	}
}

void csLeave(CrypticalSection* cs){
	if( verify(cs->ownerThreadID == GetCurrentThreadId()) &&
		!--cs->ownerEnterCount)
	{
		cs->ownerThreadID = 0;

		if(cs->cyclesStart){
			if(PERFINFO_RUN_CONDITIONS){
				U64 cyclesEnd;

				GET_CPU_TICKS_64(cyclesEnd);
				cyclesEnd -= cs->cyclesStart;

				if(cyclesEnd >= 1000){
					ADD_MISC_COUNT(cyclesEnd, "csLeave:TotalCycles");
				}
			}

			cs->cyclesStart = 0;
		}

 		if(InterlockedDecrement(&cs->sharedEnterCount)){
			// Another thread is waiting for it, so the event needs to be set.

			if(!cs->eventWait){
				csCreateEvent(cs);
			}

			PERFINFO_AUTO_START_FUNC();
			SetEvent(cs->eventWait);
			PERFINFO_AUTO_STOP();
		}
	}
}

// Get the internal critical section pointer.
CRITICAL_SECTION *InternalCs(CrypticLock *lock)
{
	return (CRITICAL_SECTION *)lock->storage;
}

// Initialize a CrypticLock, if necessary.
static void initCrypticLock(CrypticLock *lock)
{
	static CRITICAL_SECTION global_CrypticLock;		// This is used to initialize individual initCrypticLock()s.

	// Initialize global mutex.
	ATOMIC_INIT_BEGIN;
	InitializeCriticalSection(&global_CrypticLock);
	ATOMIC_INIT_END;

	// Initialize the per-lock mutex.
	EnterCriticalSection(&global_CrypticLock);
	if (!lock->criticalSection)
	{
		InitializeCriticalSection(InternalCs(lock));
		lock->criticalSection = (CRITICAL_SECTION *)lock->storage;
	}

	LeaveCriticalSection(&global_CrypticLock);
}

// Acquire lock.  If necessary, initialize it.
void Lock(CrypticLock *lock)
{
	BOOL success;

	// Initialize on demand.
	if (!lock->criticalSection)
		initCrypticLock(lock);

	// Attempt to get critical section.
	success = TryEnterCriticalSection(InternalCs(lock));

	// If we didn't get it, block on it.
	// This seems silly, but it allows us to bypass the Cryptic timer on EnterCriticalSection() in the uncontended case.
	if (!success)
		EnterCriticalSection(InternalCs(lock));
}

// Acquire lock.  If necessary, initialize it.
bool TryLock(CrypticLock *lock)
{
	// Initialize on demand.
	if (!lock->criticalSection)
		initCrypticLock(lock);

	// Attempt to get critical section.
	return TryEnterCriticalSection(InternalCs(lock));
}

// Release lock.
void Unlock(CrypticLock *lock)
{
	LeaveCriticalSection(InternalCs(lock));
}

// Destroy a lock to free resources associated with it.
void DestroyCrypticLock(CrypticLock *lock)
{
	if (lock->criticalSection)
		DeleteCriticalSection(InternalCs(lock));
}

static CrypticLock dummy_crypticlock;

STATIC_ASSERT(sizeof(dummy_crypticlock.storage) == sizeof(CRITICAL_SECTION));
