#include "ThreadManager.h"
#include "MemoryMonitor.h"
#include "mathutil.h"
#include "timing.h"
#include "ThreadManager_c_ast.h"
#include "Resourceinfo.h"
#include "stringcache.h"
#include "cpu_count.h"
#include "XboxThreads.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

// #define DISABLE_ASYNC_APC

AUTO_STRUCT;
typedef struct ManagedThread
{
	AST_COMMAND("Restrict Core","SetCoresForThread $FIELD(thread_id) $INT(What core num should this thread run on?)")
	AST_COMMAND("Update Performance", "UpdatePerformanceCmd $FIELD(thread_id) $NORETURN $NOCONFIRM")
	HANDLE thread_handle; NO_AST
	DWORD thread_id; 
	int relative_priority;
	char thread_name[128];
	const char *filename_creator;
	int linenumber_creator;
	bool bExternal;
	bool bDisallowDisableAsync;
	U32 stack_size;
	ManagedThreadPerformance perf; 
} ManagedThread;


static int tm_global_thread_priority = THREAD_PRIORITY_NORMAL;
static ManagedThread **tm_threads;
static CRITICAL_SECTION tm_critical_section;
static bool tm_inited = false;
static bool tm_set_proc_idx_disabled = false;  // Set by tmDisableSetThreadProcessorIdx()

#if !_PS3
// Thread name setting code from FMOD
#define MS_VC_EXCEPTION 0x406D1388

typedef struct tagTHREADNAME_INFO
{
	DWORD dwType;       // Must be 0x1000.
	LPCSTR szName;      // Pointer to name (in user addr space).
	DWORD dwThreadID;   // Thread ID (-1=caller thread).
	DWORD dwFlags;      // Reserved for future use, must be zero.
} THREADNAME_INFO;

static void SetThreadName(DWORD dwThreadID, LPCSTR szThreadName)
{
	THREADNAME_INFO info;

	info.dwType     = 0x1000;
	info.szName     = szThreadName;
	info.dwThreadID = dwThreadID;
	info.dwFlags    = 0;
	__try
	{
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR*)&info);
	}
#pragma warning(suppress:6312)
	__except(EXCEPTION_CONTINUE_EXECUTION)
#pragma warning(suppress:6322)		//Empty _except block...
	{
	}
}
#endif


AUTO_RUN_EARLY;
void tmStartup(void)
{
	ManagedThread *main_thread;
	if (tm_inited)
		return;
	tm_inited = true;
	InitializeCriticalSection(&tm_critical_section);
	main_thread = tmAddExternalThreadDebug(GetCurrentThread(), GetCurrentThreadId(), "Main Thread", 0, __FILE__, __LINE__);
	tmSetThreadProcessorIdx(main_thread, THREADINDEX_MAIN);
}

ManagedThread *tmGetMainThread(void)
{
	ManagedThread *main_thread;
	assertmsg(tm_inited, "Auto-runs appear to not be working!");
	EnterCriticalSection(&tm_critical_section);
		assert(tm_threads && eaSize(&tm_threads));
		main_thread = tm_threads[0];
	LeaveCriticalSection(&tm_critical_section);
	return main_thread;
}

bool tmIsMainThread(void)
{
	bool is_main = false;
	assertmsg(tm_inited, "Auto-runs appear to not be working!");
	EnterCriticalSection(&tm_critical_section);
	is_main = GetCurrentThreadId() == tm_threads[0]->thread_id;
	LeaveCriticalSection(&tm_critical_section);
	return is_main;
}

void tmForEachThread(SA_PARAM_NN_VALID ForEachThreadFunc callback, void *userData)
{
	EnterCriticalSection(&tm_critical_section);
		FOR_EACH_IN_EARRAY(tm_threads, ManagedThread, thread_ptr)
			callback(thread_ptr, userData);
		FOR_EACH_END;
	LeaveCriticalSection(&tm_critical_section);
}

ManagedThread *tmAddExternalThreadDebug(HANDLE thread_handle, DWORD thread_id, const char *thread_name, U32 stack_size, const char *filename, int linenumber)
{
	char thread_name_str[128];
	ManagedThread *thread_ptr = callocStruct(ManagedThread);

	assertmsg(tm_inited, "Auto-runs appear to not be working!");
	EnterCriticalSection(&tm_critical_section);
		eaPush(&tm_threads, thread_ptr);
		thread_ptr->thread_handle = thread_handle;
	LeaveCriticalSection(&tm_critical_section);

	SetThreadPriority(thread_ptr->thread_handle, tm_global_thread_priority + thread_ptr->relative_priority);

	thread_ptr->filename_creator = filename;
	thread_ptr->linenumber_creator = linenumber;
	thread_ptr->bExternal = true;
	thread_ptr->thread_id = thread_id;
	thread_ptr->stack_size = stack_size;

	if (!thread_name)
	{
		sprintf(thread_name_str, "%s(%d)", filename, linenumber);
		thread_name = thread_name_str;
	}
	tmSetThreadName(thread_ptr, thread_name);
	
	if (thread_ptr->stack_size && !stringCacheReadOnly())
		memMonitorTrackUserMemory(allocAddString(STACK_SPRINTF("ThreadStack:%s", thread_name)), 0, thread_ptr->stack_size, MM_ALLOC);

	return thread_ptr;
}

ManagedThread *tmCreateThreadDebug(ThreadFunc thread_function, void *thread_param, U32 stack_size, int processor_idx, const char *thread_name, const char *filename, int linenumber)
{
	ManagedThread *thread_ptr;
	HANDLE thread_handle;
	DWORD thread_id;
	
	PERFINFO_AUTO_START_FUNC();
	
#if _PS3
    {
        char thread_name_str[256];
        if (!thread_name)
	    {
		    sprintf(thread_name_str, "%s(%d)", filename, linenumber);
		    thread_name = thread_name_str;
	    }

        thread_handle = CreateThreadPS3(
            stack_size, 
            thread_function, 
            thread_param, 
            thread_name
        );
	    if (!thread_handle)
		{
			PERFINFO_AUTO_STOP();
		    return NULL;
		}

        thread_id = (DWORD)thread_handle;
    }
#else
	if (!stack_size)
		stack_size = 64*1024;
	stack_size = NEXTMULTIPLE(stack_size, 64*1024); // At least on Xbox it rounds up to 64K, keep it consistent across platforms

    thread_handle = (HANDLE)_beginthreadex( NULL,				// no security attributes
											stack_size,			// stack size
											thread_function,	// thread function
											thread_param,		// argument to thread function
											0,					// use default creation flags
											&thread_id);		// returns the thread identifier

	if (!thread_handle)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}
#endif

    thread_ptr = tmAddExternalThreadDebug(thread_handle, thread_id, thread_name, stack_size, filename, linenumber);
	tmSetThreadProcessorIdx(thread_ptr, processor_idx);
	
	PERFINFO_AUTO_STOP();

    return thread_ptr;
}

static void __stdcall exitThread(ULONG_PTR data)
{
#if _PS3
    ExitThread(0);
#else
	_endthreadex(0);
#endif
}

void tmDestroyThread(ManagedThread *thread_ptr, bool wait)
{
	PERFINFO_AUTO_START_FUNC();
	
	assertmsg(tm_inited, "Auto-runs appear to not be working!");

    EnterCriticalSection(&tm_critical_section);
		if (tmIsStillActive(thread_ptr))
			tmQueueUserAPC(exitThread, thread_ptr, 0);
		eaFindAndRemove(&tm_threads, thread_ptr);
	LeaveCriticalSection(&tm_critical_section);

    if (wait) {
#if _PS3
        uint64_t code;
        sys_ppu_thread_join((sys_ppu_thread_t)(uintptr_t)thread_ptr->thread_handle, &code);
#else
		while (tmIsStillActive(thread_ptr))
			Sleep(50);
#endif
    } else {
#if _PS3
        sys_ppu_thread_detach((sys_ppu_thread_t)(uintptr_t)thread_ptr->thread_handle);
#endif
    }

    if (thread_ptr->stack_size)
		memMonitorTrackUserMemory(allocAddString(STACK_SPRINTF("ThreadStack:%s", thread_ptr->thread_name)), 0, -((ptrdiff_t)thread_ptr->stack_size), MM_FREE);

#if !_PS3
    CloseHandle(thread_ptr->thread_handle);
#endif

	ZeroStruct(thread_ptr);
	assert(thread_ptr);
	free(thread_ptr);
	
	PERFINFO_AUTO_STOP();
}

bool tmIsStillActive(ManagedThread *thread_ptr)
{
	assertmsg(tm_inited, "Auto-runs appear to not be working!");
#if _PS3
    return IsThreadLivePS3(thread_ptr->thread_handle);
#else
    {
        DWORD exit_code;
	    return !GetExitCodeThread(thread_ptr->thread_handle, &exit_code) || exit_code == STILL_ACTIVE;
    }
#endif
}

void tmSetGlobalThreadPriority(int global_priority)
{
	int i;
	assertmsg(tm_inited, "Auto-runs appear to not be working!");
	EnterCriticalSection(&tm_critical_section);
		tm_global_thread_priority = global_priority;
		for (i = 0; i < eaSize(&tm_threads); ++i)
			verify(SetThreadPriority(tm_threads[i]->thread_handle, tm_global_thread_priority + tm_threads[i]->relative_priority));
			// Note: SetThreadPriority only accepts values of -15, -2, -1, 0, 1, 2, 15
	LeaveCriticalSection(&tm_critical_section);
}

void tmSetThreadRelativePriority(ManagedThread *thread_ptr, int relative_priority)
{
	thread_ptr->relative_priority = relative_priority;
	verify(SetThreadPriority(thread_ptr->thread_handle, tm_global_thread_priority + thread_ptr->relative_priority));
}

void tmSetThreadProcessorIdx(ManagedThread *thread_ptr, int processor_idx)
{
	if (tm_set_proc_idx_disabled || processor_idx < 0)
		return;
		
	PERFINFO_AUTO_START_FUNC();
	
#if _PS3
#elif _XBOX
	XSetThreadProcessor(thread_ptr->thread_handle, processor_idx);
#else
	if (!HyperThreadingEnabled())
		processor_idx = processor_idx / 2;
	processor_idx = processor_idx % getNumVirtualCpus();
	SetThreadIdealProcessor(thread_ptr->thread_handle, processor_idx);
#endif

	PERFINFO_AUTO_STOP();
}

bool tmDisableSetThreadProcessorIdx(bool disable)
{
	bool old = tm_set_proc_idx_disabled;
	tm_set_proc_idx_disabled = disable;
	return old;
}

#if _XBOX
static void __stdcall setThreadName(ULONG_PTR data)
{
	const char *name = (const char *)data;
	PIXNameThread(name);
}
#endif

void tmSetThreadName(ManagedThread *thread_ptr, const char *thread_name)
{
	strcpy(thread_ptr->thread_name, thread_name);
#if _PS3
#elif _XBOX
	// PIX name
	if (GetCurrentThreadId() == thread_ptr->thread_id)
		setThreadName((ULONG_PTR)thread_ptr->thread_name);
	else
		tmQueueUserAPC(setThreadName, thread_ptr, (ULONG_PTR)thread_ptr->thread_name);
	// VS name
	SetThreadName(thread_ptr->thread_id, thread_ptr->thread_name);
#else
	SetThreadName(thread_ptr->thread_id, thread_ptr->thread_name);
#endif
}

typedef struct SyncAPCData
{
	PAPCFUNC pfnAPC;
	ULONG_PTR dwData;
	HANDLE hEvent;
} SyncAPCData;

static void __stdcall syncAPC(ULONG_PTR dwData)
{
	SyncAPCData *data = (SyncAPCData *)dwData;
	
	if (data->pfnAPC == exitThread) // Horrible hack
		SetEvent(data->hEvent);

	data->pfnAPC(data->dwData);
	SetEvent(data->hEvent);
}

void tmSetThreadAllowSync(SA_PARAM_NN_VALID ManagedThread *thread_ptr, bool bAllowSync) // For debugging
{
	thread_ptr->bDisallowDisableAsync = !bAllowSync;
}


DWORD tmQueueUserAPC(PAPCFUNC pfnAPC, ManagedThread *thread_ptr, ULONG_PTR dwData)
{
#ifdef DISABLE_ASYNC_APC
	if (thread_ptr->bDisallowDisableAsync) {
		return QueueUserAPC(pfnAPC, thread_ptr->thread_handle, dwData);
	} else {
		int ret;
		SyncAPCData data = {0};
		data.pfnAPC = pfnAPC;
		data.dwData = dwData;
		data.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL); // Auto-reset, not signaled
		ret = QueueUserAPC(syncAPC, thread_ptr->thread_handle, (ULONG_PTR)&data);
		if (ret)
			WaitForEventInfiniteSafe(data.hEvent);
		CloseHandle(data.hEvent);
		return ret;
	}
#else
	return QueueUserAPC(pfnAPC, thread_ptr->thread_handle, dwData);
#endif
}

DWORD tmGetThreadId(const ManagedThread *thread_ptr)
{
	return thread_ptr->thread_id;
}

HANDLE tmGetThreadHandle(const ManagedThread *thread_ptr)
{
	return thread_ptr->thread_handle;
}

#if PROFILE_PERF
void tmProfileSuspendThreads(ManagedThread *exclude_thread)
{
	int i;
	assertmsg(tm_inited, "Auto-runs appear to not be working!");
	EnterCriticalSection(&tm_critical_section);
		for (i = 0; i < eaSize(&tm_threads); ++i)
		{
			if ( tm_threads[i] != exclude_thread )
				SuspendThread(tm_threads[i]->thread_handle);
		}
	LeaveCriticalSection(&tm_critical_section);
}

void tmProfileResumeThreads(ManagedThread *exclude_thread)
{
	int i;
	assertmsg(tm_inited, "Auto-runs appear to not be working!");
	EnterCriticalSection(&tm_critical_section);
		for (i = 0; i < eaSize(&tm_threads); ++i)
		{
			if ( tm_threads[i] != exclude_thread )
				ResumeThread(tm_threads[i]->thread_handle);
		}
	LeaveCriticalSection(&tm_critical_section);
}

#endif

ManagedThreadPerformance *tmGetThreadPerformance(ManagedThread *thread_ptr)
{
	return &thread_ptr->perf;
}

#if _PS3
void tmAddUnknownThreads(void)
{
    // add others to the list?
    assert(0);
}
#elif _XBOX
void tmAddUnknownThreads(void)
{
	DWORD threadIDs[256];
	DWORD count = ARRAY_SIZE(threadIDs);
	DWORD i;
	DmGetThreadList(threadIDs, &count);
	for (i=0; i<count; i++) {
		bool bFound=false;
		// Look for it in the list
		FOR_EACH_IN_EARRAY(tm_threads, ManagedThread, thread_ptr)
		{
			if (thread_ptr->thread_id == threadIDs[i]) {
				bFound = true;
				break;
			}
		}
		FOR_EACH_END;
		if (!bFound) {
			// Unknown thread!
			HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, threadIDs[i]);
			DM_THREADINFOEX ti = {0};
			char *name;
			ti.Size = sizeof(ti);
			DmGetThreadInfoEx(threadIDs[i], &ti);
			name = ti.ThreadNameAddress?ti.ThreadNameAddress:"Unknown Thread";
			if (hThread) {
				printf("Adding external thread (%s), stack size %d, processor %d\n", name, (char*)ti.StackBase - (char*)ti.StackLimit, ti.CurrentProcessor);
				tmAddExternalThreadDebug(hThread, threadIDs[i], name, (char*)ti.StackBase - (char*)ti.StackLimit, __FILE__, __LINE__);
			} else
				printf("Error opening thread (%s) with id %x\n", name, threadIDs[i]);
		}
	}
}
#else
#include <TlHelp32.h>
void tmAddUnknownThreads(void)
{
	DWORD currentProcessID = GetCurrentProcessId();
	// Actually grabs all threads in the system :(
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, currentProcessID);
	THREADENTRY32 te32;
	te32.dwSize = sizeof(te32);
	if (Thread32First(hSnapshot, &te32)) {
		do {
			bool bFound=false;
			if (te32.th32OwnerProcessID != currentProcessID)
				continue;
			// Look for it in the list
			FOR_EACH_IN_EARRAY(tm_threads, ManagedThread, thread_ptr)
			{
				if (thread_ptr->thread_id == te32.th32ThreadID) {
					bFound = true;
					break;
				}
			}
			FOR_EACH_END;
			if (!bFound) {
				// Unknown thread!
				HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te32.th32ThreadID);
				if (hThread)
					tmAddExternalThreadDebug(hThread, te32.th32ThreadID, "Unknown Thread", 0, __FILE__, __LINE__);
				else
					printf("Error opening thread with id %d\n", te32.th32ThreadID);
			}
		} while (Thread32Next(hSnapshot, &te32));
	}
	CloseHandle(hSnapshot);
}
#endif

static void tmUpdatePerformance(ManagedThread *thread_ptr, void *userData_UNUSED)
{
	FILETIME ftCreation, ftExit, ftKernel, ftUser;
	F64 oldKernel = thread_ptr->perf.kernelTime;
	F64 oldUser = thread_ptr->perf.userTime;
	GetThreadTimes(thread_ptr->thread_handle, &ftCreation, &ftExit, &ftKernel, &ftUser);
	thread_ptr->perf.kernelTime = *(S64*)&ftKernel / (F64)WINTICKSPERSEC;
	thread_ptr->perf.userTime = *(S64*)&ftUser / (F64)WINTICKSPERSEC;
	thread_ptr->perf.kernelTimeDelta = thread_ptr->perf.kernelTime - oldKernel;
	thread_ptr->perf.userTimeDelta = thread_ptr->perf.userTime - oldUser;

	thread_ptr->perf.kernelTime32 = thread_ptr->perf.kernelTime;
	thread_ptr->perf.userTime32 = thread_ptr->perf.userTime;
	thread_ptr->perf.kernelTimeDelta32 = thread_ptr->perf.kernelTimeDelta;
	thread_ptr->perf.userTimeDelta32 = thread_ptr->perf.userTimeDelta;
}

void tmUpdateAllPerformance(bool requeryUnknownThreads)
{
	static bool doneOnce=false;
	if (!doneOnce || requeryUnknownThreads) {
		tmAddUnknownThreads();
		doneOnce = true;
	}
	tmForEachThread(tmUpdatePerformance, NULL);
}

ManagedThread *tmGetThreadFromId(DWORD thread_id)
{
	EnterCriticalSection(&tm_critical_section);
		FOR_EACH_IN_EARRAY(tm_threads, ManagedThread, thread_ptr)
		{
			if (thread_ptr->thread_id == thread_id)
			{
				LeaveCriticalSection(&tm_critical_section);
				return thread_ptr;
			}
		}
		FOR_EACH_END;
	LeaveCriticalSection(&tm_critical_section);

	return NULL;
}

const char *tmGetThreadName(const ManagedThread *thread_ptr)
{
	if (!thread_ptr)
		return NULL;

	return thread_ptr->thread_name;
}

const char *tmGetThreadNameFromId(DWORD thread_id)
{
	return tmGetThreadName(tmGetThreadFromId(thread_id));
}

static S32 debuggerWasAttached;
AUTO_RUN_EARLY;
void tmInitDebuggerWasAttached(void)
{
	debuggerWasAttached = !!IsDebuggerPresent();
}

void tmSetThreadNames(void){
	S32 debuggerAttached = !!IsDebuggerPresent();

	if(debuggerAttached != debuggerWasAttached)
	{
		debuggerWasAttached = debuggerAttached;
		
		if(debuggerAttached)
		{
			EnterCriticalSection(&tm_critical_section);
				EARRAY_CONST_FOREACH_BEGIN(tm_threads, i, isize);
					tmSetThreadName(tm_threads[i], tm_threads[i]->thread_name);
				EARRAY_FOREACH_END;
			LeaveCriticalSection(&tm_critical_section);
		}
	}
}




void *tm_GetObject(const char *dictName, const char *itemName, void *pUserData)
{
	DWORD thread_id = 0;

	//the thread name always starts with digits followed by a space
	while (itemName[0] != ' ')
	{
		thread_id *= 10;
		thread_id += itemName[0] - '0';
		itemName++;
	}
	
	return tmGetThreadFromId(thread_id);
}


int tm_GetNumberOfObjects(const char *dictName, void *pUserData, enumResDictFlags eFlags)
{
	int iCount;
	EnterCriticalSection(&tm_critical_section);
		iCount = eaSize(&tm_threads);
	LeaveCriticalSection(&tm_critical_section);
	return iCount;
}

bool tm_InitIterator(const char *dictName, ResourceIterator *pIterator, void *pUserData)
{
	pIterator->index = 0;
	return true;
}


bool tm_GetNext(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	int iCount;

	EnterCriticalSection(&tm_critical_section);

	if (!tm_threads)
	{
		LeaveCriticalSection(&tm_critical_section);
		return false;
	}

	iCount = eaSize(&tm_threads);

	if (pIterator->index < iCount)
	{
		char tempString[256];
		*ppOutObj = tm_threads[pIterator->index++];

		sprintf(tempString, "%u - %s", ((ManagedThread*)(*ppOutObj))->thread_id, ((ManagedThread*)(*ppOutObj))->thread_name);
		*ppOutName = allocAddString(tempString);

		LeaveCriticalSection(&tm_critical_section);
		return true;
	}

	LeaveCriticalSection(&tm_critical_section);
	return false;
}
		

AUTO_RUN;
void addThreadManagerAsGlobalResource(void)
{
	resRegisterDictionary("Threads", RESCATEGORY_SYSTEM, 0, parse_ManagedThread,
							tm_GetObject,
							tm_GetNumberOfObjects,
							NULL,
							NULL,
							tm_InitIterator,
							tm_GetNext,
							NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}


AUTO_COMMAND;
void SetCoresForThread(int iThreadID, int iCoreNum)
{
#if !PLATFORM_CONSOLE
	ManagedThread *pThread = tmGetThreadFromId(iThreadID);
	if (pThread)
	{
		SetThreadAffinityMask(pThread->thread_handle, ((DWORD_PTR)1 << iCoreNum));
		Errorf("WARNING: Setting thread affinity for thread %d to only core %d\n", iThreadID, iCoreNum);
	}
#endif
}

AUTO_COMMAND;
void UpdatePerformanceCmd(int iThreadID)
{
	ManagedThread *pThread = tmGetThreadFromId(iThreadID);
	if (pThread)
	{
		tmUpdatePerformance(pThread, NULL);
	}
}


#include "ThreadManager_c_ast.c"
#include "ThreadManager_h_ast.c"
