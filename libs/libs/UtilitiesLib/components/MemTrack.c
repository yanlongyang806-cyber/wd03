#define MEMALLOC_C

GCC_SYSTEM

// This code is heavily hand-optimized for speed.  Various parts of the Cryptic engine depend crucially on good heap performance.
// This is the reason for most of the various strangenesses in this file.
//
// Warning: It is likely that any changes made in the main code paths of the allocator will cause a large performance slowdown
// unless they are carefully constructed, with performance as a primary goal.  Any such changes must be carefully
// tested in all relevant use cases (threaded and non-threaded) and iterated on until their performance is acceptable.
// If this high standard is difficult to meet, the preferred approach is to design a custom allocator, possibly wrapping
// MemTrack, that implements the needed new features, so that the entire Cryptic engine will not be slowed down by something that
// only a specific component needs.

#include "ContinuousBuilderSupport.h"
#include "wininclude.h"
#include "stdtypes.h"
#include "error.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "memtrack.h"
#include "timing_profiler_interface.h"
#include "memalloc.h"
#include "MemoryBudget.h"
#include "MemReport.h"
#include "sysutil.h"
#include "GlobalTypes.h"
#include "MemoryMonitor.h"
#include "file.h"
#include "osdependent.h"
#include "utils.h"

int allow_free_callback = 1;
AUTO_CMD_INT(allow_free_callback, allow_free_callback) ACMD_CMDLINE ACMD_ACCESSLEVEL(9);

int fill_on_alloc = 0;
AUTO_CMD_INT(fill_on_alloc, fill_on_alloc) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
int fill_on_free = 2;
AUTO_CMD_INT(fill_on_free, fill_on_free)  ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
int clear_guardbands = 1;
AUTO_CMD_INT(clear_guardbands, clear_guardbands)  ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

// If 1, apply workaround for [COR-15550].  If 0, don't.  Otherwise, determine automatically.
// See below for an explanation of what this option does.
static int workaround_cor15550 = -1;
AUTO_CMD_INT(workaround_cor15550, workaround_cor15550)  ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

AUTO_RUN_LATE;
void disableFillOnFreeInProduction(void)
{
	// Only do this if it wasn't force by the cmdline option.

	if(fill_on_free == 2){
		fill_on_free = 1;

		if (g_isContinuousBuilder) {
			fill_on_free = 0;
		}

		if (GetAppGlobalType() == GLOBALTYPE_GIMMEDLL)
		{	
			fill_on_free = 0;
			return;
		}

		if (isProductionMode())
			fill_on_free = 0;
	}
}

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef _malloc_dbg
#undef _calloc_dbg
#undef _realloc_dbg
#undef _free_dbg
#undef HeapAlloc
#undef HeapReAlloc
#undef HeapFree
#undef HeapCreate
#undef HeapDestroy

#if !_PS3
#define ENABLE_OVERRIDES 1
#endif

#if _XBOX
#define HeapAlloc RtlAllocateHeap
#define HeapReAlloc RtlReAllocateHeap
#define HeapFree RtlFreeHeap
#endif

// Uncomment the following to make memtrack easier to profile in a sample-based profiler, for a performance penalty
//#define MEMTRACK_MAKE_PROFILING_EASIER
#ifdef MEMTRACK_MAKE_PROFILING_EASIER
#define MEMTRACK_FORCEINLINE
#else
#define MEMTRACK_FORCEINLINE __forceinline
#endif

// If defined, save the MemTrack header on free.
// This can be commented out for a slight performance boost.
#define SAVE_MEMTRACK_HEADER

#define MM_ALLOC 1
#define MM_REALLOC 0
#define MM_FREE -1

#define SLOTS_MAX MEMTRACK_SLOTS_MAX
#define SLOT_MASK (SLOTS_MAX-1)

#if _M_X64
#define LOG_MEM_OPS (256*256)
#else
#define LOG_MEM_OPS 256
#endif

AllocTracker	*memtrack_counts[MEMTRACK_THREAD_MAX];
#if MEMTRACK_THREAD_MAX != 1
int				memtrack_thread_count;
#endif

typedef enum LogMemOp
{
	LogMemOp_FREE=-1,
	LogMemOp_REALLOC=0,
	LogMemOp_ALLOC=1,
} LogMemOp;

#ifdef LOG_MEM_OPS

typedef struct MemCmd
{
	const char	*fname;
	int			line;
	MemTrackType size;
	LogMemOp	op;
	void		*data;
} MemCmd;

// Small allocations, and all allocations, are logged to the per-thread detailed counter structure.

// Medium allocation log.
#define MEDIUM_ALLOC_SIZE 1024
unsigned medium_memcmds_idx;			// Current index into medium_memcmds
MemCmd medium_memcmds[LOG_MEM_OPS];		// Last LOG_MEM_OPS medium-sized memory operations

// Large allocation log.
#define LARGE_ALLOC_SIZE 512*1024
unsigned large_memcmds_idx;				// Current index into large_memcmds
MemCmd large_memcmds[LOG_MEM_OPS];		// Last LOG_MEM_OPS large-sized memory operations

#endif  // LOG_MEM_OPS

AllocName		memtrack_names[MEMTRACK_MAXENTRIES];
int				memtrack_total;

AllocNamePooled	memtrack_names_pooled[MEMTRACK_MAX_UNIQUE_FILENAMES];
int				memtrack_total_pooled;


static U16		alloc_name_idxs[SLOTS_MAX];
static int		total_thread_count;
static HANDLE	thread_handles[MEMTRACK_THREAD_MAX];

// Special-purpose heaps.
static HANDLE *special_heaps[CRYPTIC_END_HEAP] = {0};

// The following track the high and low water mark of allocation activity.  Atomic operations are used to
// update the global totals.  The water marks are then updated if necessary, also using atomic operations.
// The update check relies on the architecture's memory ordering providing a guarantee that loads are not
// reordered with these atomic operations.  Intel x86 processors provide this guarantee, in
// IASDM December 2011 III 8.2.2 (5).
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS
static MemTrackType	memtrack_global_bytes;				// Total outstanding allocation sizes, in bytes
static MemTrackType	memtrack_global_bytes_high;			// High water mark of memtrack_global_bytes
static MemTrackType	memtrack_global_bytes_low;			// Low water mark of memtrack_global_bytes
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_COUNTS
static MemTrackType	memtrack_global_num_allocs;			// Outstanding allocation count
static MemTrackType	memtrack_global_num_allocs_high;	// High water mark of memtrack_global_num_allocs
static MemTrackType	memtrack_global_num_allocs_low;		// Low water mark of memtrack_global_num_allocs_low
#endif // MEMTRACK_UPDATE_MEMOP_COUNTERS_COUNTS
#endif // MEMTRACK_UPDATE_MEMOP_COUNTERS

// Region reserved by memTrackReserveMemoryChunk().
static void *reserved_region = NULL;

// Committed size of reserved_region.
static volatile long reserved_region_commit_size = 0;

#if ENABLE_OVERRIDES
CRTMallocFunc g_malloc_func;
static CRTCallocFunc g_calloc_func;
static CRTReallocFunc g_realloc_func;
static CRTFreeFunc g_free_func;

void setMemoryAllocators(CRTMallocFunc m, CRTCallocFunc c, CRTReallocFunc r, CRTFreeFunc f)
{
	g_malloc_func = m;
	g_calloc_func = c;
	g_realloc_func = r;
	g_free_func = f;
}
#endif

#ifdef _XBOX
#define USE_TLSF 0 // If we use this, we can't also use TLSF as custom app-level heap code.  Need to also set TLSF_USE_LOCKS 1 in tlsf.c to be threadsafe
#define USE_NED 0 // Might be better for multiple threads, but has per-thread caches using more memory?
#define USE_DL 1 // Best on Xbox
#else
#define USE_TLSF 0 // Possibly the fastest, but never releases memory back to the system.  Need to also set TLSF_USE_LOCKS 1 in tlsf.c to be threadsafe
#define USE_NED 0
#define USE_DL 0 // A bit faster for the PC client startup times, but probably not as good for multithreaded/servers?
#endif

#if _PS3
#define defmalloc(heap,amount) malloc(amount)
#define defcalloc(heap,count,amount) calloc(count,amount)
#define defrealloc(heap,base,amount) realloc(base,amount)
#define deffree(heap,base) free(base)
#elif USE_TLSF
#include "heaps/tlsf.h"
#define defmalloc(heap,amount) tlsf_malloc(amount)
#define defcalloc(heap,count,amount) tlsf_calloc(count, amount)
#define defrealloc(heap,base,amount) tlsf_realloc(base, amount)
#define deffree(heap,base) tlsf_free(base)
#elif USE_NED
#include "heaps/nedmalloc.h"
#define defmalloc(heap,amount) nedmalloc(amount)
#define defcalloc(heap,count,amount) nedcalloc(count, amount)
#define defrealloc(heap,base,amount) nedrealloc(base, amount)
#define deffree(heap,base) nedfree(base)
#elif USE_DL
#include "heaps/dlmalloc.h"
#define defmalloc(heap,amount) dlmalloc(amount)
#define defcalloc(heap,count,amount) dlcalloc(count, amount)
#define defrealloc(heap,base,amount) dlrealloc(base, amount)
#define deffree(heap,base) dlfree(base)
#else
#define defmalloc(heap,amount) HeapAlloc(heap, s_oom_exceptions_enabled ? HEAP_GENERATE_EXCEPTIONS : 0, amount)
#define defcalloc(heap,count,amount) HeapAlloc(heap, (s_oom_exceptions_enabled ? HEAP_GENERATE_EXCEPTIONS : 0)|HEAP_ZERO_MEMORY, (amount) * (count))
#define defrealloc(heap,base,amount) HeapReAlloc(heap, s_oom_exceptions_enabled ? HEAP_GENERATE_EXCEPTIONS : 0, base, amount)
#define deffree(heap,base) HeapFree(heap, 0, base)
#define defmsize(heap,base) HeapSize(heap, 0, base)
#endif

char memMonitorBreakOnAlloc[MAX_PATH];
int memMonitorBreakOnAllocLine;
bool memMonitorBreakOnAllocDisableReset;

// Instructs the memory tracking code to signal a breakpoint if an allocation occurs in the specified file
AUTO_CMD_SENTENCE(memMonitorBreakOnAlloc, memMonitorBreakOnAlloc) ACMD_CMDLINE;
AUTO_CMD_INT(memMonitorBreakOnAllocLine, memMonitorBreakOnAllocLine) ACMD_CMDLINE;
AUTO_CMD_INT(memMonitorBreakOnAllocDisableReset, memMonitorBreakOnAllocDisableReset) ACMD_CMDLINE;


static PERFINFO_TYPE* memory_group;

#if 0
#define PERFINFO_ALLOC_START(func, size)
#define PERFINFO_ALLOC_END()
#else
#define PERFINFO_ALLOC_START_SIZE(func, size, x, xName)					\
		else if(size < x){												\
			PERFINFO_AUTO_START(func" "xName, 1);						\
		}
		
#define PERFINFO_ALLOC_START_MAIN(func, size)							\
	PERFINFO_AUTO_START_STATIC("heap", &memory_group, 1);				\
		if(size < 0){													\
			PERFINFO_AUTO_START(func, 1);								\
		}																\
		PERFINFO_ALLOC_START_SIZE(func, size, 4, "[1,4)")				\
		PERFINFO_ALLOC_START_SIZE(func, size, 8, "[4,8)")				\
		PERFINFO_ALLOC_START_SIZE(func, size, 16, "[8,16)")				\
		PERFINFO_ALLOC_START_SIZE(func, size, 32, "[16,32)")			\
		PERFINFO_ALLOC_START_SIZE(func, size, 64, "[32,64)")			\
		PERFINFO_ALLOC_START_SIZE(func, size, 128, "[64,128)")			\
		PERFINFO_ALLOC_START_SIZE(func, size, 256, "[128,256)")			\
		PERFINFO_ALLOC_START_SIZE(func, size, 512, "[256,512)")			\
		PERFINFO_ALLOC_START_SIZE(func, size, 1024, "[512,1K)")			\
		PERFINFO_ALLOC_START_SIZE(func, size, 2048, "[1K,2K)")			\
		PERFINFO_ALLOC_START_SIZE(func, size, 4096, "[2K,4K)")			\
		PERFINFO_ALLOC_START_SIZE(func, size, 8192, "[4K,8K)")			\
		PERFINFO_ALLOC_START_SIZE(func, size, 16384, "[8K,16K)")		\
		PERFINFO_ALLOC_START_SIZE(func, size, 20440, "[16K,20440)")		\
		PERFINFO_ALLOC_START_SIZE(func, size, 32768, "[20440,32K)")		\
		PERFINFO_ALLOC_START_SIZE(func, size, 65536, "[32K,64K)")		\
		PERFINFO_ALLOC_START_SIZE(func, size, 131072, "[64K,128K)")		\
		PERFINFO_ALLOC_START_SIZE(func, size, 262144, "[128K,256K)")	\
		PERFINFO_ALLOC_START_SIZE(func, size, 524288, "[256K,512K)")	\
		PERFINFO_ALLOC_START_SIZE(func, size, 1044480, "[512K,1020K)")	\
		PERFINFO_ALLOC_START_SIZE(func, size, 2097152, "[1020K,2M)")	\
		PERFINFO_ALLOC_START_SIZE(func, size, 4194304, "[2M,4M)")		\
		PERFINFO_ALLOC_START_SIZE(func, size, 8388608, "[4M,8M)")		\
		PERFINFO_ALLOC_START_SIZE(func, size, 16777216, "[8M,16M)")		\
		PERFINFO_ALLOC_START_SIZE(func, size, 33554432, "[16M,32M)")	\
		PERFINFO_ALLOC_START_SIZE(func, size, 67108864, "[32M,64M)")	\
		else{															\
			PERFINFO_AUTO_START(func ">64M", 1);						\
		}

#define PERFINFO_ALLOC_START(func, size) {								\
	S32 didDisable = 0;													\
	PERFINFO_RUN(														\
		PERFINFO_ALLOC_START_MAIN(func, size);							\
		autoTimerDisableRecursion(&didDisable);							\
	);

#define PERFINFO_ALLOC_END_MAIN()										\
	PERFINFO_AUTO_STOP();												\
	PERFINFO_AUTO_STOP_CHECKED("heap")

#define PERFINFO_ALLOC_END_MAIN_CHECKED(x)								\
	PERFINFO_AUTO_STOP_CHECKED(x);										\
	PERFINFO_AUTO_STOP_CHECKED("heap")

#define PERFINFO_ALLOC_END()											\
	if(didDisable){														\
		autoTimerEnableRecursion(didDisable);							\
		PERFINFO_ALLOC_END_MAIN();										\
	}																	\
}

#define PERFINFO_ALLOC_END_MIDDLE()										\
	if(didDisable){														\
		autoTimerEnableRecursion(didDisable);							\
		PERFINFO_ALLOC_END_MAIN();										\
	}

#define PERFINFO_ALLOC_END_CHECKED(x)									\
	if(didDisable){														\
		autoTimerEnableRecursion(didDisable);							\
		PERFINFO_ALLOC_END_MAIN_CHECKED(x);								\
	}																	\
}

#endif

static bool s_oom_exceptions_enabled = false;  // True if SEH exceptions should be thrown on out-of-memory conditions

// If non-zero, do not allow heap operations to proceed.
volatile static DWORD freeze_memory = 0;

// This is used to freeze threads calling into MemTrack after freeze_memory is set.
static CRITICAL_SECTION freeze_mutex;

OutOfMemoryCallback g_OutOfMemoryCallback;


// This is meant to allow a program to attempt to recover from a failed malloc as it
// happens.  If an OutOfMemoryCallback is set, the memory allocation function will do
// one retry after running the callback.
OutOfMemoryCallback setOutOfMemoryCallback(OutOfMemoryCallback callback)
{
	OutOfMemoryCallback ret = g_OutOfMemoryCallback;
	g_OutOfMemoryCallback = callback;
	return ret;
}

// This function is called when another thread has experienced a heap failure, so that
// other threads do not damage the heap state further and complicate crash analysis.
// To analyze, search other threads for an assertOnAllocError() that is not in
// memtrackFailureFreezeThread().
static void memtrackFailureFreezeThread()
{
	EnterCriticalSection(&freeze_mutex);
	LeaveCriticalSection(&freeze_mutex);
}

// Freeze MemTrack: hold all threads trying to enter or leave.
static void memtrackFreeze()
{
	// Initialize and acquire the freeze mutex, which frozen threads will wait on.
	ATOMIC_INIT_BEGIN;
	InitializeCriticalSection(&freeze_mutex);
	ATOMIC_INIT_END;
	EnterCriticalSection(&freeze_mutex);

	// Ask other threads to wait on the mutex.
	InterlockedIncrement(&freeze_memory);
}

// Unfreeze MemTrack and release waiting threads.
static void memtrackThaw()
{
	// Stop asking threads to wait.
	InterlockedDecrement(&freeze_memory);

	// Release the mutex.
	LeaveCriticalSection(&freeze_mutex);
}

#pragma optimize("", off)
void assertOnAllocError(size_t size,const char *filename, int linenumber){
	char buffer[300];
	static int recursive_count=0;

	// Once a thread has called assertOnAllocError(), shut down MemTrack, and capture all subsequent threads
	// that try to get in or out.
	memtrackFreeze();

	// Check for excessive recursion.
	InterlockedIncrement(&recursive_count);
	if (recursive_count>4) {
		// We're not recovering calling assert, just break into the debugger if we can!
		_DbgBreak();
	}

	// Call the out-of-memory callback.
	if(g_OutOfMemoryCallback)
		g_OutOfMemoryCallback();

	// Assert.
	sprintf(buffer, "(%s:%d) failed to allocate %"FORM_LL"u bytes", filename, linenumber, (U64)size);
	assertmsg(0, buffer);
	InterlockedDecrement(&recursive_count);

	// Assert wants us to try to continue, so let's give it a shot.
	memtrackThaw();
}
#pragma optimize("", on)

// Allocate a memory chunk from the reserved memory, if possible.
static void *allocate_reserved(size_t size)
{
	unsigned long old;
	void *result;

	// Fail if we don't have an available reserved region.
	if (!size || size > LONG_MAX || !reserved_region || reserved_region_commit_size)
		return NULL;

	// Atomically acquire reserved region..
	old = InterlockedExchange(&reserved_region_commit_size, (long)size);
	if (old)
	{
		while (old != size)
			old = InterlockedExchange(&reserved_region_commit_size, old);
		return NULL;
	}

	// Try to allocate from reserved_region.
	PERFINFO_AUTO_START("VirtualAlloc", 1);
	result = VirtualAlloc(reserved_region, size, MEM_COMMIT, PAGE_READWRITE);
	PERFINFO_AUTO_STOP();
	if (!result)
	{
		reserved_region_commit_size = 0;
		return NULL;
	}
	assert(result == reserved_region);

	return reserved_region;
}

// Try to change the size of an allocation from the reserved memory chunk.
static void *reallocate_reserved(size_t size)
{
	void *result;

	// Fail if we don't have a reserved region.
	if (!size || size > LONG_MAX || !reserved_region)
		return NULL;

	// If the allocation is smaller, for performance, don't actually do anything.
	if ((long)size <= reserved_region_commit_size)
		return reserved_region;

	// Try to resize the reserved region.
	PERFINFO_AUTO_START("VirtualAlloc", 1);
	result = VirtualAlloc(reserved_region, size, MEM_COMMIT, PAGE_READWRITE);
	PERFINFO_AUTO_STOP();
	if (!result)
		return NULL;
	assert(result == reserved_region);

	// Save reserved region size.
	reserved_region_commit_size = (long)size;

	return reserved_region;
}

// Free memory from the reserved chunk.
static void free_reserved(void *p)
{
	bool success;

	PERFINFO_AUTO_START_FUNC();

	assert(reserved_region_commit_size);

#pragma warning(disable:6250)
	success = VirtualFree(reserved_region, reserved_region_commit_size, MEM_DECOMMIT);
#pragma warning(default:6250)
	assertmsgf(success, "VirtualFree: %d", GetLastError());

	reserved_region_commit_size = 0;

	PERFINFO_AUTO_STOP();
}

// Wrapper for defmalloc() that calls allocate_reserved() on failure.
__forceinline static void *try_malloc(void *heap, size_t amount)
{
	void *result = defmalloc(heap, amount);
	return result ? result : allocate_reserved(amount);
}

// Wrapper for defcalloc() that calls allocate_reserved() on failure.
__forceinline static void *try_calloc(void *heap, size_t count, size_t amount)
{
	void *result = defcalloc(heap, count, amount);
	return result ? result : allocate_reserved(count*amount);
}

// Wrapper for deffree() that calls free_reserved() when appropriate.
__forceinline static void try_free(void *heap, void *base)
{
	if (base == reserved_region)
		free_reserved(base);
	else
		deffree(heap, base);
}

// Wrapper for defrealloc() and associated operations that calls reallocate_reserved() when appropriate.
static void *try_realloc(void *heap, void *base, size_t amount)
{
	void *result;

	// If this is a free request, free it.
	if (!amount)
	{
		try_free(heap, base);
		return NULL;
	}

	// This is a reserved allocation: it must be processed specially.
	if (base == reserved_region)
	{

		// If it's a regular resize, try to resize.
		result = reallocate_reserved(amount);
		if (result)
			return result;

		// It couldn't be resized in place, but maybe we can get a separate allocation in the regular heap that will work.
		result = defmalloc(heap, amount);
		if (result)
		{
			memcpy(base, result, amount);
			free_reserved(base);
			return result;
		}

		// It couldn't be resized.
		return NULL;
	}

	// Try regular realloc.
	result = defrealloc(heap, base, amount);

	// If it doesn't work, try a reserved allocation.
	if (!result)
	{
		result = allocate_reserved(amount);
		if (result)
		{
			size_t old_size = defmsize(heap, base);
			memcpy(result, base, old_size);
			deffree(heap, base);
		}
	}

	return result;
}

#ifdef LOG_MEM_OPS

// See memTrackLogOpsInit().
const static int g_log_mem_ops_enabled = 1;

void memTrackLogOpsInit(void)
{
	// Currently, g_log_mem_ops_enabled is always on.  Previously, it was only enabled for specific things, so it wouldn't be enabled on the ObjectDB,
	// because there was a claim it hurt multithreaded performance. However, changes were made that make this code much better in multithreaded
	// mode, using the same per-thread memtrack mechanism.
	//if (GetAppGlobalType() == GLOBALTYPE_CLIENT || GetAppGlobalType() == GLOBALTYPE_GAMESERVER || GetAppGlobalType() == GLOBALTYPE_NONE)
	//	g_log_mem_ops_enabled = true;
}

#define logMem(tracker,size,op,fname,line,data) if (g_log_mem_ops_enabled && data) logMemInternal(tracker,size, op, fname, line, data)
#define logMemInterlocked(tracker,size,op,fname,line,data) if (g_log_mem_ops_enabled && data) logMemInternalInterlocked(tracker,size, op, fname, line, data)
#else
#define logMem(tracker,size,op,fname,line,data)
#define logMemInterlocked(tracker,size,op,fname,line,data)
#endif


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping("Unknown C++", BUDGET_Unknown););
AUTO_RUN_ANON(memBudgetAddMapping("Unknown C++[]", BUDGET_Unknown););

//////////////////////////////////////////////////////////////////////////
// CRT memory fill values

#define MALLOC_UNINITIALIZED_VALUE		0xCDCDCDCD
#define MALLOC_NOMANSLAND_VALUE			0x5D379DF5
#define FREE_VALUE						0xDDDDDDDD
#define MALLOC_GUARDBANDFREED_VALUE		0xCACACACA
#define FREE_MARKER						0xDEBA7AB1E5EAF00DULL

#define HEAPALLOC_UNINITIALIZED_VALUE	0xBAADF00D
#define HEAPFREE_VALUE					0xFEEEFEEE

#if _PS3
static bool my_heap;
static __thread DWORD tls_thread_id;
#elif _XBOX
static HANDLE *my_heap,*default_heap;
static __declspec(thread) DWORD tls_thread_id;
#else
static HANDLE *my_heap,*default_heap;
static DWORD tls_thread_id_slot;
#endif
static CRITICAL_SECTION memtracker_critsec,validate_critsec;

typedef struct MemTrackHeader MemTrackHeader;
static void addMemOpNonInlined(size_t amount,LogMemOp op,const char *filename, int line,MemTrackHeader *mem);
// For internal allocations to get them tracked
#define calloc_return_then_track(num, size)		\
	defcalloc(default_heap, (num), (size));		\
	addMemOpNonInlined((num)*(size), LogMemOp_ALLOC, __FILE__, __LINE__, NULL);

static bool enableLFHdummy; // Not used, just so the command parses correctly
AUTO_CMD_INT(enableLFHdummy, enableLFH) ACMD_COMMANDLINE;
static bool usingLFH=false;

// Like AllocTracker, but with more information
// Note: The first fields of this structure must match AllocTracker exactly.
typedef struct AllocTrackerDetailed
{
	// Basic information
	MemTrackType	bytes;				// Total outstanding allocation size, in bytes
	MemTrackType	num_allocs;			// Number of outstanding allocations
	MemTrackType	total_allocs;		// Cumulative count of number of allocations

	// Delta bytes
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
	// See total_allocs above
	MemTrackType	total_frees;		// Cumulative count of number of frees
	MemTrackType	total_alloc_bytes;	// Cumulative count of bytes allocated
	MemTrackType	total_free_bytes;	// Cumulative count of bytes freed
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS

#ifdef LOG_MEM_OPS
	unsigned memcmds_idx;				// Current index into memcmds
	MemCmd memcmds[LOG_MEM_OPS];		// Last LOG_MEM_OPS memory operations
#endif
} AllocTrackerDetailed;

#ifdef LOG_MEM_OPS

// Log medium- and large-sized allocations.
static MEMTRACK_FORCEINLINE void logMemLargeAllocs(MemTrackType size,LogMemOp op,const char *fname,int line, void *data)
{
	int t;
	if (size > MEDIUM_ALLOC_SIZE)
	{
		t = medium_memcmds_idx++ % LOG_MEM_OPS;
		medium_memcmds[t].fname = fname;
		medium_memcmds[t].line = line;
		medium_memcmds[t].size = size;
		medium_memcmds[t].op = op;
		medium_memcmds[t].data = data;

		if (size > LARGE_ALLOC_SIZE)
		{
			t = large_memcmds_idx++ % LOG_MEM_OPS;
			large_memcmds[t].fname = fname;
			large_memcmds[t].line = line;
			large_memcmds[t].size = size;
			large_memcmds[t].op = op;
			large_memcmds[t].data = data;
		}
	}
}

// Record an allocation in the per-thread tracker.
// Note: If you change this function, please change logMemInternalInterlocked() below.
static MEMTRACK_FORCEINLINE void logMemInternal(AllocTrackerDetailed *tracker,MemTrackType size,LogMemOp op,const char *fname,int line, void *data)
{
	// All allocations go to the per-thread place.
	int t = tracker->memcmds_idx++ % LOG_MEM_OPS;
	tracker->memcmds[t].fname = fname;
	tracker->memcmds[t].line = line;
	tracker->memcmds[t].size = size;
	tracker->memcmds[t].op = op;
	tracker->memcmds[t].data = data;

	logMemLargeAllocs(size,op,fname,line,data);
}

// Like logMemInternal(), but interlocked for the default thread.
static MEMTRACK_FORCEINLINE void logMemInternalInterlocked(AllocTrackerDetailed *tracker,MemTrackType size,LogMemOp op,const char *fname,int line, void *data)
{
	int t = (InterlockedIncrement(&tracker->memcmds_idx) - 1) % LOG_MEM_OPS;
	tracker->memcmds[t].fname = fname;
	tracker->memcmds[t].line = line;
	tracker->memcmds[t].size = size;
	tracker->memcmds[t].op = op;
	tracker->memcmds[t].data = data;

	logMemLargeAllocs(size,op,fname,line,data);
}
#endif LOG_MEM_OPS

// Initialize LFH and any other appropriate options on a heap.
static void setHeapOptions(HANDLE *heap)
{
	ULONG ulEnableLFH = 2;
	bool enableLFH=true;

	// Disable the special heap checking that is turned on when a debugger is attached.
	disableRtlHeapChecking(heap);

#if WIN32
	if (!IsUsingVista())
	{
		//NOTE NOTE NOTE NOTE this is querying the command line directly, which is generally a no-no due to the
		//new command line automatic file glomming code. So if you're running on XP and definitely need this to
		//be on, and have very long command lines... talk to Alex
		if (strstri((char*)GetCommandLineA(), "-enableLFH") && !strstri((char*)GetCommandLineA(), "-enableLFH 0"))
		{
			// This redundant assignment just makes sure that specifically enabling the LFH overrides the next condition
			enableLFH = true;
		}
		else if (strEndsWith(getExecutableName(), "GameClient.exe") ||
			strEndsWith(getExecutableName(), "GameServer.exe"))
		{
			// If it's a Game Server or Game Client and it's not specifically enabled,
			// turn it off for heap validation purposes
			enableLFH = false;
		}
	}
	if(enableLFH)
	{ 
		usingLFH = true;
		if (!HeapSetInformation(heap, HeapCompatibilityInformation, &ulEnableLFH, sizeof(ulEnableLFH)))
		{
			//printf("Failed to enable LFH\n");
		}
	}
#endif
}

// Create and initialize a new heap.
static HANDLE *createNewHeap()
{
	HANDLE *heap;

	// Create the heap.
	heap = HeapCreate(s_oom_exceptions_enabled ? HEAP_GENERATE_EXCEPTIONS : 0,0,0);

	// Configure the heap.
	setHeapOptions(heap);

	return heap;
}

AUTO_RUN_FIRST;
void initMemTrack(void)
{
#if _PS3
	if(my_heap)
		return;
	my_heap = 1;
#else  // !_PS3
	U32 i=0,numHeap=0;

	// Enter one-time initialization mutex.
	ATOMIC_INIT_BEGIN;

	// Disable all MS CRT heap debugging flags.
	_CrtSetDbgFlag(0);

	// Save a pointer to the default heap, for special use.
	default_heap = GetProcessHeap();
	
	// Initialize TLSF, if enabled.
#if USE_TLSF
	tlsf_init_memory_pool(400*1024*1024, HeapAlloc(GetProcessHeap(), 0, 400*1024*1024));
#endif

	// Determine if we're going to throw exceptions on out-of-memory.
#ifdef MEMTRACK_DEFAULT_ENABLE_OOM_EXCEPTIONS
	s_oom_exceptions_enabled = true;
#endif

	// Create the main Cryptic heap.
	my_heap = createNewHeap();

	// Initialize MemTrack TLS slot.
#if !_XBOX
	if (!tls_thread_id_slot)
		tls_thread_id_slot = TlsAlloc();
#endif
#endif  // !_PS3

#ifdef LOG_MEM_OPS
	memTrackLogOpsInit();
#endif

	InitializeCriticalSection(&memtracker_critsec);
	InitializeCriticalSection(&validate_critsec);

#if MEMTRACK_THREAD_MAX == 1
	memtrack_counts[0] = calloc_return_then_track(1, MEMTRACK_MAXENTRIES * sizeof(AllocTracker) + sizeof(AllocTrackerDetailed));
#endif

	// track the other big arrays
	addMemOpNonInlined(sizeof(memtrack_names), LogMemOp_ALLOC, __FILE__, __LINE__, NULL);
	addMemOpNonInlined(sizeof(memtrack_names_pooled), LogMemOp_ALLOC, __FILE__, __LINE__, NULL);
	addMemOpNonInlined(sizeof(alloc_name_idxs), LogMemOp_ALLOC, __FILE__, __LINE__, NULL);

	ATOMIC_INIT_END;
}

// Initialize a special-purpose heap.
static void initSpecialHeap(int index)
{
	static CRITICAL_SECTION special_heap_create_mutex;

	// Enter mutex.
	ATOMIC_INIT_BEGIN;
	InitializeCriticalSection(&special_heap_create_mutex);
	ATOMIC_INIT_END;
	EnterCriticalSection(&special_heap_create_mutex);

	// If already initialized (due to race), abort.
	if (special_heaps[index])
	{
		LeaveCriticalSection(&special_heap_create_mutex);
		return;
	}

	// Create the special heap.
	special_heaps[index] = createNewHeap();

	LeaveCriticalSection(&special_heap_create_mutex);
}


static int do_validate_critsec;

#define ENTER_VALIDATE_CRITSEC 	if (do_validate_critsec) { EnterCriticalSection(&validate_critsec); in_validate_critsec = true; }
#define LEAVE_VALIDATE_CRITSEC 	if (in_validate_critsec) { LeaveCriticalSection(&validate_critsec); in_validate_critsec = false; }
#define VALIDATE_RETRY 3



typedef struct MemTrackHeader
{
	U32		large_size;		// this is only valid if small_size > MAX_SMALL_SIZE or if the process is 64-bit, 
							// otherwise it's invalid data hanging off the front of the allocation
	U8		small_size;
	U8		name_idx_high;
	U8		name_idx_low;
	U8		guardband;
	U8		data[1]; // actually size bytes
} MemTrackHeader;

#define PREFIX_MIN_BYTES 4
#define PREFIX_EXTEND_BYTES 4
#define PREFIX_BYTES (PREFIX_MIN_BYTES + PREFIX_EXTEND_BYTES)
STATIC_ASSERT(offsetof(MemTrackHeader,data)==PREFIX_BYTES);

#define POSTFIX_BYTES 2
#define WRAPPER_BYTES (PREFIX_BYTES + POSTFIX_BYTES)

#if _M_X64 || _PS3
#define MAX_SMALL_SIZE	0
#else
#define MAX_SMALL_SIZE	255
#endif

__forceinline static size_t GET_ALLOC_SIZE(MemTrackHeader *mem)
{
	#if _M_X64
	return (((size_t)mem->large_size) << 8) | mem->small_size;
	#else
	if (mem->small_size < MAX_SMALL_SIZE)
		return mem->small_size;
	return mem->large_size;
	#endif
}

__forceinline static void SET_ALLOC_SIZE(MemTrackHeader *mem,size_t size)
{
	#if _M_X64
	mem->large_size = (U32)(size >> 8);
	mem->small_size = (U32) size;
	#else
	if (size < MAX_SMALL_SIZE)
		mem->small_size = (U8)size;
	else
	{
		mem->small_size = MAX_SMALL_SIZE;
		mem->large_size = (U32)size;
	}
	#endif
}

size_t GetAllocSize(void *p)
{
	return GET_ALLOC_SIZE((MemTrackHeader *)((U8*)p - PREFIX_BYTES));
}

#define GET_NAME_IDX(mem)		(((U32)(mem)->name_idx_high << 8) + (mem)->name_idx_low)
#define SET_NAME_IDX(mem,idx)	((mem)->name_idx_high = idx >> 8, (mem)->name_idx_low = idx)

#define SET_BASE_GUARDBAND(mem,val)		((mem)->guardband = val & 255)
#define GET_BASE_GUARDBAND(mem)			((mem)->guardband)
#define BASE_GUARDBAND_WHEN_ALLOCATED	(MALLOC_NOMANSLAND_VALUE & 255)
#define BASE_GUARDBAND_WHEN_UNFREEABLE	((MALLOC_NOMANSLAND_VALUE + 2) & 255)
#define CHECK_BASE_GUARDBAND(mem)		(GET_BASE_GUARDBAND(mem) == BASE_GUARDBAND_WHEN_ALLOCATED)

#define SET_END_GUARDBAND(mem,size,val) ((mem)->data[size] = val & 255, (mem)->data[size + 1] = (val >> 8) & 255)
#define GET_END_GUARDBAND(mem,size) ((U32)(mem)->data[size] | ((U32)(mem)->data[size + 1] << 8))
#define CHECK_END_GUARDBAND(mem,size) (GET_END_GUARDBAND(mem,size) == (MALLOC_NOMANSLAND_VALUE & 65535))

// If this is a special heap allocation, return the heap it's from, and also check its guardbands.
__forceinline static int GET_SPECIAL_HEAP_INDEX(MemTrackHeader *mem)
{
	if (GET_BASE_GUARDBAND(mem) == BASE_GUARDBAND_WHEN_ALLOCATED + 1)
	{
		size_t size = GET_ALLOC_SIZE(mem);
		int index;
		assert(CHECK_END_GUARDBAND(mem, size));
		assert(mem->data[size + 3] == (MALLOC_NOMANSLAND_VALUE & 0xff) + 1);
		index = mem->data[size + 2];
		assert(index > CRYPTIC_FIRST_HEAP && index <= CRYPTIC_LAST_HEAP && special_heaps[index]);
		return mem->data[size + 2];
	}
	return 0;
}

__forceinline static void initMemWrapper(MemTrackHeader *mem,size_t size,int heap_index)
{
	SET_ALLOC_SIZE(mem,size);
	if (heap_index > CRYPTIC_FIRST_HEAP)
		SET_BASE_GUARDBAND(mem,MALLOC_NOMANSLAND_VALUE+1);
	else
		SET_BASE_GUARDBAND(mem,MALLOC_NOMANSLAND_VALUE);
	SET_END_GUARDBAND(mem,size,MALLOC_NOMANSLAND_VALUE);
	if (heap_index > CRYPTIC_FIRST_HEAP)
	{
		mem->data[size + 2] = heap_index;
		mem->data[size + 3] = MALLOC_NOMANSLAND_VALUE + 1;
	}
}

char * GetAllocInfo(void *p)
{
	static char info[1024] = {0};
	MemTrackHeader *mem = (MemTrackHeader *)((U8*)p - PREFIX_BYTES);
	AllocName *slot = &memtrack_names[GET_NAME_IDX(mem)];
	AllocNamePooled *slotpooled = &memtrack_names_pooled[slot->pooled_index];
	bool allocated = CHECK_BASE_GUARDBAND(mem) && CHECK_END_GUARDBAND(mem, GET_ALLOC_SIZE(mem));

	sprintf(info, "%s:%u (%u bytes, %s)",
		slotpooled->filename, slot->line, GET_ALLOC_SIZE(mem),
		allocated ? "allocated" : "free or corrupted");
	return info;
}

int memtrack_pooled_miss;

MEMTRACK_FORCEINLINE static int findEntry(const char *filename, int line)
{
	size_t		hash;
	int			i;
	AllocName	*slot;
	AllocNamePooled	*slotpooled;

	hash = ((size_t)filename) * line;

	for(i=0;i<SLOTS_MAX;i++)
	{

		U32		idx = (U32)((i + hash) & SLOT_MASK),name_idx;

		name_idx = alloc_name_idxs[idx];
		if (name_idx)
		{
			name_idx--;
			slot = &memtrack_names[name_idx];
			slotpooled = &memtrack_names_pooled[slot->pooled_index];
			if (slotpooled->filename == filename && slot->line == (U32)line)
				return name_idx;
		}
		else
		{
			static int last_miss;
			memBudgetGetByFilename(filename); // to cause the assert to have the right stack
			EnterCriticalSection(&memtracker_critsec);
			if (alloc_name_idxs[idx]) // make sure no one already took the slot
			{
				LeaveCriticalSection(&memtracker_critsec);
				// Check to see if it was the same name we're going for
				name_idx = alloc_name_idxs[idx];
				name_idx--;
				slot = &memtrack_names[name_idx];
				slotpooled = &memtrack_names_pooled[slot->pooled_index];
				if (slotpooled->filename == filename && slot->line == (U32)line)
					return name_idx;
				continue;
			}
			name_idx = memtrack_total++;
			alloc_name_idxs[idx] = name_idx + 1;
			slot = &memtrack_names[name_idx];
			// Find existing pooled info, this could be slow, perhaps change it to a hash table
			if (memtrack_names_pooled[last_miss].filename == filename)
			{
				slot->pooled_index = last_miss;
			}
			else
			{
				int j;
				for (j=memtrack_total_pooled-1; j>=0; j--)
				{
					if (memtrack_names_pooled[j].filename == filename)
						break;
					memtrack_pooled_miss++;
				}
				if (j==-1)
				{
					memtrack_names_pooled[memtrack_total_pooled].filename = filename;
					slot->pooled_index = memtrack_total_pooled;
					memtrack_total_pooled++;
				} else {
					if (j != memtrack_total_pooled-1)
						last_miss = j;
					slot->pooled_index = j;
				}
			}
			slot->line = line;
			slot->video_mem = filterVideoMemory(filename);
			LeaveCriticalSection(&memtracker_critsec);
			assert(slot->line == (U32)line); // Checking that we fit in the limited number of bits
			if (memtrack_total*2 > SLOTS_MAX)
			{
				static int warned;

				if (!warned)
				{
					warned = 1;
					Errorf("MemTrack: memtracker hashtable getting full, increase MEMTRACK_SLOTS_MAX in MemTrack.h\n");
				}
			}
			if (memtrack_total > MEMTRACK_MAXENTRIES - 1000)
			{
				static int warned;

				if (memtrack_total > MEMTRACK_MAXENTRIES)
					FatalErrorf("MemTrack: memtracker table is full. increase MEMTRACK_MAXENTRIES in MemTrack.h\n");

				if (!warned)
				{
					warned = 1;
					Errorf("MemTrack: memtracker table getting full, increase MEMTRACK_MAXENTRIES in MemTrack.h\n");
				}
			}
			return name_idx;
		}
	}
	assert(0);
	return 0;
}

#if MEMTRACK_64BIT
#	define safeIncrement(val) InterlockedIncrement64(&val)
#	define safeDecrement(val) InterlockedDecrement64(&val)
#	define safeAdd(val,amount) InterlockedExchangeAdd64(&val,amount)
#	define safeSwap(lhs,rhs) InterlockedExchange64(&lhs,rhs)
#else
#	define safeIncrement(val) InterlockedIncrement((LONG *)&val)
#	define safeDecrement(val) InterlockedDecrement((LONG *)&val)
#	define safeAdd(val,amount) InterlockedExchangeAdd((LONG *)&val,amount)
#	define safeSwap(lhs,rhs) InterlockedExchange((LONG *)&lhs,rhs)
#endif

#if MEMTRACK_THREAD_MAX == 1

#	define threadId() 0
#else // MEMTRACK_THREADMAX != 1

MEMTRACK_FORCEINLINE static int threadId(void)
{
	size_t		t;
	int			i;
    int			thread_id;

#if PLATFORM_CONSOLE
    thread_id = tls_thread_id;
#else
	thread_id = (int)(intptr_t)TlsGetValue(tls_thread_id_slot);
#endif
    if (!thread_id)
	{
        int		inc_thread_count=0;
		EnterCriticalSection(&memtracker_critsec);
        ++total_thread_count;
	    for(i=memtrack_thread_count-1;i>=0;i--)
	    {
#if _PS3
            if(!IsThreadLivePS3(thread_handles[i]))
                break;
#else
		    int		ret;
		    DWORD	code;

		    ret = GetExitCodeThread(thread_handles[i],&code);
		    if (ret && !code)
		    {
			    CloseHandle(thread_handles[i]);
			    break;
		    }
#endif
	    }
	    if (i < 0)
	    {
		    if (memtrack_thread_count >= MEMTRACK_THREAD_MAX)
		    {
#if MEMTRACK_THREAD_MAX > 2 // 2-thread mode, it's expected to hit this all the time
			    ErrorFilenameDeferredf(NULL, "MemAlloc: Active thread count exceeded, max is %d\n",MEMTRACK_THREAD_MAX);
#endif
			    i = MEMTRACK_THREAD_MAX-1;
		    }
		    else
		    {
			    i = memtrack_thread_count;
			    inc_thread_count = 1;
		    }
	    }
	    t = i;

#if _PS3
		thread_handles[i] = GetCurrentThread();
#else
		DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
			GetCurrentProcess(), &thread_handles[i], 0, false, DUPLICATE_SAME_ACCESS);
#endif

#if PLATFORM_CONSOLE
        tls_thread_id = thread_id = t+1;
#else
	    TlsSetValue(tls_thread_id_slot,(LPVOID)(t+1));
	    thread_id = (int)(intptr_t)TlsGetValue(tls_thread_id_slot);
#endif
	    if (!memtrack_counts[i])
		{
		    memtrack_counts[i] = calloc_return_then_track(1, MEMTRACK_MAXENTRIES * sizeof(AllocTracker) + sizeof(AllocTrackerDetailed));
		}
	    if (inc_thread_count)
		    memtrack_thread_count++;
		LeaveCriticalSection(&memtracker_critsec);
	    //printf("thread id %d\n",thread_id-1);
	}

    return (int)thread_id-1;
}
#endif

// Update high water mark in a thread-safe way.
__forceinline static void updateMemHighInterlocked(volatile MemTrackType *accumulator, MemTrackType value)
{
	MemTrackType initial = *accumulator;
	while (value > initial)
	{
		MemTrackType old;
		old = safeSwap(*accumulator, value);
		initial = value;
		value = old;
	}
}

// Update low water mark in a thread-safe way.
__forceinline static void updateMemLowInterlocked(volatile MemTrackType *accumulator, MemTrackType value)
{
	MemTrackType initial = *accumulator;
	while (value < initial)
	{
		MemTrackType old;
		old = safeSwap(*accumulator, value);
		initial = value;
		value = old;
	}
}

// Get the summary totals slot for a thread.
__forceinline static AllocTrackerDetailed *findMemTrackTotalCount(int thread_id)
{
	// This works because MemTrack allocates extra space for the last array entry.
	return (AllocTrackerDetailed *)&memtrack_counts[thread_id][MEMTRACK_MAXENTRIES];
}

MEMTRACK_FORCEINLINE static void addMemOp(size_t amount,LogMemOp op,const char *filename, int line,MemTrackHeader *mem)
{
	AllocTracker	*slot;
	AllocTrackerDetailed *total;
	int				slot_idx,thread_id;

	if (memMonitorBreakOnAlloc[0] && strEndsWith(filename, memMonitorBreakOnAlloc) &&
		(!memMonitorBreakOnAllocLine || memMonitorBreakOnAllocLine == line))
	{
		_DbgBreak();
		if (!memMonitorBreakOnAllocDisableReset)
			memMonitorBreakOnAlloc[0]='\0';
	}

	// The following code attempts to record the memory statistics in a per-thread place.  The aggregate of each
	// thread's statistics will be used for the overall statistics values.  The reason for this is that it is
	// much faster to update data that is not shared between threads.  However, if we've run out of thread slots,
	// we update the catch-all thread instead, using slow interlocked operations.

	thread_id = threadId();
	slot_idx = findEntry(filename,line);
	slot = &memtrack_counts[thread_id][slot_idx];
	if (mem)
		SET_NAME_IDX(mem,slot_idx);

	total = findMemTrackTotalCount(thread_id);

	// Last thread statistics, shared by multiple threads
	if (thread_id == MEMTRACK_THREAD_MAX-1)
	{

		// Per-slot totals
		safeIncrement(slot->num_allocs);
		safeIncrement(slot->total_allocs);
		safeAdd(slot->bytes,(MemTrackType)amount);

		// Totals across all slots
		safeIncrement(total->num_allocs);
		safeIncrement(total->total_allocs);
		safeAdd(total->bytes,(MemTrackType)amount);

#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
		// Accumulated allocations, reset periodically.
		safeAdd(total->total_alloc_bytes, (MemTrackType)amount);
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS

		logMemInterlocked(total, amount,op,filename,line,mem);
	}

	// Per-thread statistics
	else {

		// Per-slot totals
		slot->num_allocs++;
		slot->total_allocs++;
		slot->bytes += amount;

		// Totals across all slots
		total->num_allocs++;
		total->total_allocs++;
		total->bytes+=amount;

#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
		// Accumulated allocations, reset periodically.
		total->total_alloc_bytes += amount;
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS

		logMem(total, amount,op,filename,line,mem);
	}

#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS
	// Update high water marks.
	safeAdd(memtrack_global_bytes,(MemTrackType)amount);
	updateMemHighInterlocked(&memtrack_global_bytes_high, memtrack_global_bytes);
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_COUNTS
	safeIncrement(memtrack_global_num_allocs);
	updateMemHighInterlocked(&memtrack_global_num_allocs_high, memtrack_global_num_allocs);
#endif
#endif
}

static void addMemOpNonInlined(size_t amount,LogMemOp op,const char *filename, int line,MemTrackHeader *mem)
{
	addMemOp(amount, op, filename, line, mem);
}



MEMTRACK_FORCEINLINE static void freeMemOp(size_t amount,int slot_idx, void *data)
{
	AllocTracker	*slot;
	AllocTrackerDetailed *total;
	int				thread_id;

	// See addMemOp() for a description of what's going on in this function.

	thread_id = threadId();
	slot = &memtrack_counts[thread_id][slot_idx];
	total = findMemTrackTotalCount(thread_id);

	if (thread_id == MEMTRACK_THREAD_MAX-1)
	{
		
		safeDecrement(slot->num_allocs);		
		safeAdd(slot->bytes,-(MemTrackType)amount);

	
		safeDecrement(total->num_allocs);	
		safeAdd(total->bytes,-(MemTrackType)amount);

#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
		// Accumulated allocations, reset periodically.
		safeAdd(total->total_free_bytes, (MemTrackType)amount);
		safeIncrement(total->total_frees);
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS

		logMemInterlocked(total, amount, LogMemOp_FREE, memtrack_names_pooled[memtrack_names[slot_idx].pooled_index].filename, memtrack_names[slot_idx].line, data);
	}
	else {


		slot->num_allocs--;
		slot->bytes-=amount;

		total->num_allocs--;
		total->bytes-=amount;

#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
		// Accumulated allocations, reset periodically.
		total->total_free_bytes += amount;
		++total->total_frees;
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS

		logMem(total, amount, LogMemOp_FREE, memtrack_names_pooled[memtrack_names[slot_idx].pooled_index].filename, memtrack_names[slot_idx].line, data);
	}

#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS
	// Update low water marks.
	safeAdd(memtrack_global_bytes,-(MemTrackType)amount);
	updateMemLowInterlocked(&memtrack_global_bytes_low, memtrack_global_bytes);
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_COUNTS
	safeDecrement(memtrack_global_num_allocs);
	updateMemLowInterlocked(&memtrack_global_num_allocs_low, memtrack_global_num_allocs);
#endif
#endif
}

void memTrackFreeBySlotIdx(int iSlotIdx, MemTrackType iAmount, void *pData)
{
	freeMemOp(iAmount, iSlotIdx, pData);
}

// Read and reset the MemOp counters.
void readMemOpCounters(MemOpCounters *counters)
{
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS
	static MemTrackType last_delta_alloc_bytes = 0;
	static MemTrackType last_delta_alloc_count = 0;
	static MemTrackType last_delta_free_bytes = 0;
	static MemTrackType last_delta_free_count = 0;
	MemTrackType delta_alloc_bytes = 0;
	MemTrackType delta_alloc_count = 0;
	MemTrackType delta_free_bytes = 0;
	MemTrackType delta_free_count = 0;
	MemTrackType outstanding_bytes = memtrack_global_bytes;
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_COUNTS
	MemTrackType outstanding_count = memtrack_global_num_allocs;
#endif
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
	int i;
#endif

	// Add up current deltas.
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
	for(i=0;i<memtrack_thread_count;i++)
	{
		AllocTrackerDetailed *per_thread = findMemTrackTotalCount(i);
		delta_alloc_bytes += per_thread->total_alloc_bytes;
		delta_alloc_count += per_thread->total_allocs;
		delta_free_bytes += per_thread->total_free_bytes;
		delta_free_count += per_thread->total_frees;
	}
#endif  // MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS

	// Subtract to get actual deltas.
	counters->delta_alloc_bytes = delta_alloc_bytes - last_delta_alloc_bytes;
	counters->delta_alloc_count = delta_alloc_count - last_delta_alloc_count;
	counters->delta_free_bytes = delta_free_bytes - last_delta_free_bytes;
	counters->delta_free_count = delta_free_count - last_delta_free_count;

	// Save current counts to be subtracted next time.
	last_delta_alloc_bytes = delta_alloc_bytes;
	last_delta_alloc_count = delta_alloc_count;
	last_delta_free_bytes = delta_free_bytes;
	last_delta_free_count = delta_free_count;

	// Save and reset watermarks.
	counters->outstanding_bytes_high = safeSwap(memtrack_global_bytes_high, outstanding_bytes);
	counters->outstanding_bytes_low = safeSwap(memtrack_global_bytes_low, outstanding_bytes);
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS_COUNTS
	counters->outstanding_count_high = safeSwap(memtrack_global_num_allocs_high, outstanding_count);
	counters->outstanding_count_low = safeSwap(memtrack_global_num_allocs_low, outstanding_count);
#else
#endif

#endif // MEMTRACK_UPDATE_MEMOP_COUNTERS
}

__forceinline static void checkGuardBand(MemTrackHeader *curr, size_t size)
{
	assert(CHECK_BASE_GUARDBAND(curr) && CHECK_END_GUARDBAND(curr,size));
}

// Stall if the heap has been frozen due to an allocation failure.
__forceinline static void checkHeapState()
{
	if (freeze_memory)
		memtrackFailureFreezeThread();
}

// Number of times workaround_cor15550 has been applied.
volatile U64 memtrackCor15550WorkaroundCount = 0;

// Last amount that failed.
volatile size_t memtrackCor15550WorkaroundLast = 0;

// Saved exception state, from heapException().
static volatile U32 memtrack_exception_count = 0;
static volatile int memtrack_exception_saved_code;
static volatile EXCEPTION_RECORD memtrack_exception_saved_record;
static volatile CONTEXT memtrack_exception_saved_context;

// The heap has thrown an exception: decide what to do.
#pragma optimize("", off)
static int heapException(bool can_fail, bool *retry_alloc,unsigned int code,PEXCEPTION_POINTERS info,size_t *size,const char *filename, int linenumber)
{
	// If we're not actually enabling heap exceptions, don't catch anything.
	if (!s_oom_exceptions_enabled)
		return EXCEPTION_CONTINUE_SEARCH;

	// If we have an exception for some reason other than being out of memory, don't handle it here.
	if (code != STATUS_NO_MEMORY)
		return EXCEPTION_CONTINUE_SEARCH;

	// If we're allowed to fail, continue execution.
	if (can_fail)
		return EXCEPTION_EXECUTE_HANDLER;

	// Save the exception context, so we can get to it easily from a debugger.
	InterlockedIncrement(&memtrack_exception_count);
	memtrack_exception_saved_code = code;
	memtrack_exception_saved_record = *info->ExceptionRecord;
	memtrack_exception_saved_context = *info->ContextRecord;

	// Workaround for COR-15550
	// On Windows Server 2003 R2 x64 Enterprise, we observe an apparent limitation where the total size of the heap
	// segments cannot exceed 106 GB.  In our observations on this system, the virtual allocation threshold is
	// 1020 KiB, so any allocations over this amount are exempt from the problem.  So, if we get allocations failing
	// under circumstances that seem to be similar
#ifdef _M_X64
	if (workaround_cor15550 && code == STATUS_NO_MEMORY)
	{
		bool use_workaround = false;

		// Decide if we should try the workaround, if it hasn't been forced on.
		if (workaround_cor15550 == 1)
			use_workaround = true;
		else
		{
			if (IsThisObjectDB() && !IsUsingVista())
			{
				bool enough_bytes = true;
#ifdef MEMTRACK_UPDATE_MEMOP_COUNTERS
				if (memtrack_global_bytes < 17179869184ULL)
					enough_bytes = false;
#endif
				if (enough_bytes)
					use_workaround = true;
			}
		}

#define COR15550_RETRY_SIZE (1020*1024-8)		// Retry allocation with this size

		// Try to apply the workaround, if appropriate.
		if (use_workaround && *size > 20*1024 && *size < COR15550_RETRY_SIZE)
		{
			EXCEPTION_RECORD *ExceptionRecord = info->ExceptionRecord;
			if    (ExceptionRecord->ExceptionFlags == 0
				&& ExceptionRecord->NumberParameters == 1)
			{
				size_t fault_size = ExceptionRecord->ExceptionInformation[0];
				if (*size && fault_size >= *size && fault_size < *size * 2)
				{
					memtrackCor15550WorkaroundLast = *size;
					InterlockedIncrement64(&memtrackCor15550WorkaroundCount);
					*retry_alloc = 1;
					*size = COR15550_RETRY_SIZE;
					return EXCEPTION_CONTINUE_EXECUTION;
				}
			}
		}
	}
#endif  // _M_X64

#if 0
	// Alternative one: Handle the exception by setting the result to null, to make the behavior as close as possible to the
	// non-exception behavior.  The downside of this is that we lose the actual exception context.  Some of it is saved
	// above.
	return EXCEPTION_EXECUTE_HANDLER;
#endif

	// Currently, we're using this alternative as a good balance between exception context information for the crash dump
	// and ease of initial triage, debugability, and development suitability.
#if 1
	// Alternative two: Assert immediately.  This is the closest to the non-exception behavior that still allows us to
	// preserve the exception context on the stack.  If the assert is ignored, we attempt to continue exception, but it's not
	// clear what effect this will have for exceptions thrown from inside the heap.
	assertOnAllocError(*size,filename,linenumber);
	return EXCEPTION_CONTINUE_EXECUTION;
#endif

#if 0
	// Alternative three: Call the out-of-memory callback, then allow the exception to continue unwinding.  This should
	// eventually reach our per-thread top-level exception handler, which will superassert().  This will make initial triage
	// more difficult, so it probably isn't worth it.
	// FIXME: Note this isn't quite right, because it should InterlockedIncrement() calling_OutOfMemory in assertOnAllocError().
	if(g_OutOfMemoryCallback)
		g_OutOfMemoryCallback();
	return EXCEPTION_CONTINUE_SEARCH;
#endif

#if 0
	// Alternative four: Just allow the exception to propagate, without doing any special handling.  The crash context may be
	// more pristine this way, but we won't get mmpl, which may be difficult to reconstruct after the fact.
	return EXCEPTION_CONTINUE_SEARCH;
#endif
}
#pragma optimize("", on)

bool IsHeapInitialized(int blockType)
{
	if (blockType > CRYPTIC_FIRST_HEAP && blockType <= CRYPTIC_LAST_HEAP)
	{
		return !!special_heaps[blockType];
	}
	else
	{
		return !!my_heap;
	}
}

// Choose the appropriate heap.
static MEMTRACK_FORCEINLINE void *memtrack_select_heap(int blockType, size_t *extra_postfix_bytes)
{
	HANDLE *heap;

	assert(blockType <= CRYPTIC_LAST_HEAP);

	if (blockType > CRYPTIC_FIRST_HEAP)
	{
		heap = special_heaps[blockType];
		if (!heap)
			initSpecialHeap(blockType);
		heap = special_heaps[blockType];
		*extra_postfix_bytes = 2;
	}
	else
	{
		heap = my_heap;
		*extra_postfix_bytes = 0;
	}
	return heap;
}

void *malloc_timed_canfail(size_t size,int blockType,bool can_fail, const char *filename, int linenumber)
{
	HANDLE *heap;
	MemTrackHeader	*mem;
    void *p;
    int offset;
	bool in_validate_critsec=false;
	bool retry_alloc = false;
	size_t extra_postfix_bytes;

#if ENABLE_OVERRIDES
	if (g_malloc_func)
		return g_malloc_func(size,blockType,filename,linenumber);
#endif

	PERFINFO_ALLOC_START("malloc", size);

	checkHeapState();

	if (!my_heap)
		initMemTrack();

	heap = memtrack_select_heap(blockType, &extra_postfix_bytes);

	ENTER_VALIDATE_CRITSEC;
	if (size < MAX_SMALL_SIZE)
		offset = PREFIX_EXTEND_BYTES;
	else
        offset = 0;
	__try
	{
		mem = (MemTrackHeader*)((U8*)(p = try_malloc(heap, size + WRAPPER_BYTES + extra_postfix_bytes - offset)) - offset);
	}
	__except(heapException(can_fail, &retry_alloc, GetExceptionCode(), GetExceptionInformation(), &size, filename, linenumber))
	{
		mem = p = NULL;
	}
	if (!p) {
		LEAVE_VALIDATE_CRITSEC;
		if (retry_alloc)
		{
			PERFINFO_ALLOC_END_MIDDLE();
			return malloc_timed_canfail(size,blockType,can_fail,filename,linenumber);
		}
		if (can_fail)
			checkHeapState();
		PERFINFO_ALLOC_END_MIDDLE();
		return 0;
	}
	if (fill_on_alloc)
		memset(mem->data,MALLOC_UNINITIALIZED_VALUE,size);
	initMemWrapper(mem,size,blockType);
	LEAVE_VALIDATE_CRITSEC;
	checkHeapState();
	addMemOp(size,LogMemOp_ALLOC,filename,linenumber,mem);
	PERFINFO_ALLOC_END();

	return mem->data;
}

void* malloc_timed(size_t size,int blockType, const char *filename, int linenumber)
{
	U8 *mem;

malloc_retry:
	mem = malloc_timed_canfail(size,blockType,false,filename,linenumber);
	if (!mem)
	{
		assertOnAllocError(size,filename,linenumber);
		goto malloc_retry;
	}
	return mem;
}

void *calloc_timed_canfail(size_t size,size_t count,int blockType,bool can_fail, const char *filename, int linenumber)
{
	HANDLE *heap;
	MemTrackHeader	*mem;
    void *p;
    int offset;
	bool in_validate_critsec=false;
	bool retry_alloc = false;
	size_t extra_postfix_bytes;

#if ENABLE_OVERRIDES
	if (g_calloc_func)
		return g_calloc_func(size,count,blockType,filename,linenumber);
#endif
	size *= count;
	PERFINFO_ALLOC_START("calloc", size);

	checkHeapState();

	if (!my_heap)
		initMemTrack();

	heap = memtrack_select_heap(blockType, &extra_postfix_bytes);

    ENTER_VALIDATE_CRITSEC;
	if (size < MAX_SMALL_SIZE)
		offset = PREFIX_EXTEND_BYTES;
	else
        offset = 0;
	__try
	{
		mem = (MemTrackHeader*)((U8*)(p = try_calloc(heap, size + WRAPPER_BYTES + extra_postfix_bytes - offset, 1)) - offset);
	}
	__except(heapException(can_fail, &retry_alloc, GetExceptionCode(), GetExceptionInformation(), &size, filename, linenumber))
	{
		mem = p = NULL;
	}
    if (!p) {
        LEAVE_VALIDATE_CRITSEC;
		if (retry_alloc)
		{
			PERFINFO_ALLOC_END_MIDDLE();
			return calloc_timed_canfail(size,1,blockType,can_fail,filename,linenumber);
		}
		if (can_fail)
			checkHeapState();
		PERFINFO_ALLOC_END_MIDDLE();
		return 0;
	}
	initMemWrapper(mem,size,blockType);
	LEAVE_VALIDATE_CRITSEC;
	checkHeapState();
	addMemOp(size,LogMemOp_ALLOC,filename,linenumber,mem);
	PERFINFO_ALLOC_END();
	return mem->data;
}

void* calloc_timed(size_t size,size_t count,int blockType, const char *filename, int linenumber)
{
	U8 *mem;

calloc_retry:
	mem = calloc_timed_canfail(size,count,blockType,false,filename,linenumber);
	if (!mem)
	{
		assertOnAllocError(size*count,filename,linenumber);
		goto calloc_retry;
	}
	return mem;
}

#ifdef SAVE_MEMTRACK_HEADER

// Pad before saved MemTrackHeader: see copy_freed_headers().
#if _M_X64
#define COPY_FREED_HEADERS_PAD 8
#else
#define COPY_FREED_HEADERS_PAD 0
#endif

// Data saved by copy_freed_headers() because it's going to be overwritten.
typedef struct copy_headers_saved
{
	U32 offset;												// Offset of saved data from beginning of allocation
	char data[COPY_FREED_HEADERS_PAD + PREFIX_BYTES + 8];	// Saved data
} copy_headers_saved;

// Copy the MemTrackHeader into the memory itself so we know who used to own the memory.
// Layout of saved information:
//   [Original Windows heap header] [Freed MemTrackHeader] [[on x64 only: sizeof(void*) pad]] [copy of MemTrackHeader *] [FREE_MARKER] ... [end guardband]
// * Note: The size of copied MemTrackHeader includes the extended prefix, even for small allocations, but it will not be copied for small allocations, so
// the value of these bytes will be whatever was there before the MemTrackHeader save.
// For allocations that are not in an LFH bucket, we expect the Windows heap will cover the first two pointer sizes of the
// allocation with the free list data.  We move the MemTrack header past this, if the allocation is big enough, and put
// a special marker to make it easier to find.
static MEMTRACK_FORCEINLINE void copy_freed_headers(MemTrackHeader *curr, size_t old_size, copy_headers_saved *saved)
{
	// Only copy if we're not filling on free, since that would overwrite the copy.
	// This is not true for realloc(), but we still don't do it, for consistency.
	if (!fill_on_free && old_size > sizeof(saved->data))
	{
		U8 *save_area = curr->data + COPY_FREED_HEADERS_PAD;		// Pointer to area to write MemTrackHeader into
		U8 *header_start = curr->small_size < MAX_SMALL_SIZE		// Start of part of header to write
			? (U8 *)&curr->small_size : (U8 *)curr;
		U32 header_size = curr->small_size < MAX_SMALL_SIZE			// Size of header to write
			? PREFIX_MIN_BYTES : PREFIX_BYTES;

		// If necessary, save contents that we will overwrite.
		if (saved)
		{
			saved->offset = curr->data - header_start;
			memcpy(saved->data, curr->data, sizeof(saved->data));
		}

		// Copy header itself.
		save_area += (header_start - (U8 *)curr);
		memcpy(save_area, header_start, header_size);
		save_area += header_size;

		// Copy the special marker.
		*(U64 *)save_area = FREE_MARKER;
	}
}

// Restore original contents overwritten by the guardband save.
static MEMTRACK_FORCEINLINE void restore_from_guardband_overwrite(MemTrackHeader *curr, size_t new_size, copy_headers_saved *saved)
{
	if (clear_guardbands && !fill_on_free && saved->offset)
	{
		U8 *alloc_start = new_size < MAX_SMALL_SIZE ? (U8 *)&curr->small_size : (U8 *)curr;
		U32 header_size = curr->small_size < MAX_SMALL_SIZE ? PREFIX_MIN_BYTES : PREFIX_BYTES;
		U32 alloc_size = header_size + (U32)new_size + POSTFIX_BYTES;
		if (alloc_size > saved->offset)
			memcpy((U8 *)alloc_start + saved->offset, saved->data, MIN(sizeof(saved->data), alloc_size - saved->offset));
	}
}

#else  // SAVE_MEMTRACK_HEADER

// Dummy versions that do nothing.
typedef struct copy_headers_saved
{
	U32 offset;  // Not used
} copy_headers_saved;
static __forceinline void copy_freed_headers(MemTrackHeader *curr, size_t old_size, copy_headers_saved *saved)
{
	// No-op.
}
static __forceinline void restore_from_guardband_overwrite(MemTrackHeader *curr, size_t new_size, copy_headers_saved *saved)
{
	// No-op.
}

#endif  // SAVE_MEMTRACK_HEADER

static void *realloc_core(void *p, size_t size,int blockType,bool can_fail, const char *filename, int linenumber)
{
	MemTrackHeader	*mem,*curr = (MemTrackHeader*) (((U8 *)p) - PREFIX_BYTES);
	size_t			old_size;
	int				old_offset=0,new_offset=0,end_pad=0;
	char			end_pad_bytes[PREFIX_EXTEND_BYTES-POSTFIX_BYTES];
	void			*ret;
	bool			in_validate_critsec = false;
	bool			retry_alloc = false;

#if ENABLE_OVERRIDES
	if (g_realloc_func)
		return g_realloc_func(p,size,blockType,filename,linenumber);
#endif

	PERFINFO_ALLOC_START("realloc", size);

	checkHeapState();

	if (!my_heap)
		initMemTrack();

	// Convert this realloc() into a different allocator operation, if appropriate.
	if (!p)
	{
		ret = malloc_timed_canfail(size,blockType,can_fail,filename,linenumber);
	} else if (!size)
	{
		free_timed(p,blockType);
		ret = NULL;
	}
	// See the similar comment in free_timed_size() for an explanation of why this code has been disabled.
	//	else if (!CHECK_BASE_GUARDBAND(curr)) // maybe it's from the CRT allocator, if not, it will crash anyhow.
	//	{
	//#pragma warning(disable:6280)
	//#undef realloc
	//		ret = realloc(p, size);
	//	}
	else
	{
		int heap_index;
		HANDLE *heap;
		copy_headers_saved saved;
		size_t extra_postfix_bytes = 0;

		// Determine which heap we're using.
		heap_index = GET_SPECIAL_HEAP_INDEX(curr);
		if (heap_index)
		{
			heap = special_heaps[heap_index];
			extra_postfix_bytes = 2;
		}
		else
			heap = my_heap;

		// Check and update accounting information.
		old_size = GET_ALLOC_SIZE(curr);
		if (!heap_index)
			checkGuardBand(curr,old_size);
		freeMemOp(old_size,GET_NAME_IDX(curr),curr);
		saved.offset = 0;
		if(clear_guardbands)
		{
			// Change the guardbands to the free value.
			SET_BASE_GUARDBAND(curr, MALLOC_GUARDBANDFREED_VALUE);
			SET_END_GUARDBAND(curr, old_size, MALLOC_GUARDBANDFREED_VALUE);
		}

	realloc_retry:

		// Determine how the header offset is going to change based on crossing the MAX_SMALL_SIZE boundary.
		if (old_size < MAX_SMALL_SIZE)
			old_offset = PREFIX_EXTEND_BYTES;
		if (size < MAX_SMALL_SIZE)
		{
			new_offset = PREFIX_EXTEND_BYTES;
			if (old_size >= MAX_SMALL_SIZE)
			{
				// Save trailing bytes, which will be obliterated when we resize with a larger offset and consequently smaller total allocation size.
				end_pad = sizeof(end_pad_bytes);
				memcpy(end_pad_bytes, curr->data + size - end_pad, end_pad);
			}
		}

		// Copy the MemTrackHeader into the memory itself so we know who used to own the memory.
		if(clear_guardbands)
			copy_freed_headers(curr, old_size, &saved);

		ENTER_VALIDATE_CRITSEC;
		__try
		{
			mem = (MemTrackHeader *) (((U8*)(p = try_realloc(heap, ((U8*)curr) + old_offset,size + WRAPPER_BYTES + extra_postfix_bytes - new_offset + end_pad))) - new_offset);
		}
		__except(heapException(can_fail, &retry_alloc, GetExceptionCode(), GetExceptionInformation(), &size, filename, linenumber))
		{
			mem = p = NULL;
		}
		if (!p) {
			LEAVE_VALIDATE_CRITSEC;
			if (retry_alloc)
			{
				PERFINFO_ALLOC_END_MIDDLE();
				return realloc_core(p,size,blockType,can_fail,filename,linenumber);
			}
			if (can_fail)
			{
				checkHeapState();
				restore_from_guardband_overwrite(curr, old_size, &saved);
				initMemWrapper(curr, old_size, heap_index);
				addMemOp(old_size,LogMemOp_REALLOC,filename,linenumber,mem);
			}
			else
			{
				assertOnAllocError(size,filename,linenumber);
				goto realloc_retry;
			}
			PERFINFO_ALLOC_END_MIDDLE();
			return 0;
		}

		// Restore original contents overwritten by the guardband save.
		restore_from_guardband_overwrite(mem, size, &saved);

		// Move all memory over if we moved over a small size boundary.
		if (old_offset != new_offset)
		{
			PERFINFO_AUTO_START_L2("Offset Mismatch", 1);
			if (!new_offset)
				memmove(((U8*)mem)+PREFIX_EXTEND_BYTES,mem,size + WRAPPER_BYTES - PREFIX_EXTEND_BYTES);
			else
			{
				memmove(((U8*)mem)+PREFIX_EXTEND_BYTES,((U8*)mem)+PREFIX_EXTEND_BYTES*2,size + PREFIX_MIN_BYTES - end_pad);
				memcpy(((U8*)mem)+PREFIX_EXTEND_BYTES + size + PREFIX_MIN_BYTES - end_pad, end_pad_bytes, end_pad);
			}
			PERFINFO_AUTO_STOP_L2();
		}
		if (fill_on_alloc && old_size < size)
			memset(mem->data + old_size,MALLOC_UNINITIALIZED_VALUE,size - old_size);
		initMemWrapper(mem,size, heap_index);
		LEAVE_VALIDATE_CRITSEC;
		addMemOp(size,LogMemOp_REALLOC,filename,linenumber,mem);
		ret = mem->data;
	}
	PERFINFO_ALLOC_END();
	return ret;
}

#define IS_MEMALIGN(curr) (curr->name_idx_high == 255)
#define UNDO_MEMALIGN(curr) if IS_MEMALIGN(curr) curr = (MemTrackHeader*)(((U8*)curr) - curr->small_size);

MemTrackHeader freeCheckShadow;
MemFreeCallback freeCheckCB = NULL;

void memTrackRegisterFreeCallback(const void *ptrCheck, MemFreeCallback cb)
{
	MemTrackHeader	*header = (MemTrackHeader *) (((U8 *)ptrCheck) - PREFIX_BYTES);

	memcpy(&freeCheckShadow, header, sizeof(MemTrackHeader));
	freeCheckCB = cb;
}

static void reportMismatch(	MemTrackHeader* h,
							const void* p,
							const char* reason)
{
	U32 nameIndex = GET_NAME_IDX(h);
	U32 pooledIndex = U32_MAX;

	if(nameIndex < (U32)memtrack_total){
		ANALYSIS_ASSUME(nameIndex < ARRAY_SIZE(memtrack_names));
		pooledIndex = memtrack_names[nameIndex].pooled_index;
	}

	assertmsgf(	0,
				"%s: 0x%p (allocated to %s:%u (memtrack_names[%u]))",
				reason,
				p,
				pooledIndex < (U32)memtrack_total_pooled ?
					memtrack_names_pooled[pooledIndex].filename :
					"out of bounds pooled name index",
				nameIndex < (U32)memtrack_total ?
					memtrack_names[nameIndex].line :
					0,
				nameIndex);
}

void memFreeDisable(void* p, U32* keyInOut){
	MemTrackHeader* h = (MemTrackHeader *)(((U8*)p) - PREFIX_BYTES);
	MemTrackHeader* hReal = h;
	U32				key = *keyInOut;

	UNDO_MEMALIGN(hReal);
	assert(CHECK_BASE_GUARDBAND(h));

	if(GET_NAME_IDX(hReal) != key){
		if(!key){
			*keyInOut = GET_NAME_IDX(hReal);
		}else{
			reportMismatch(hReal, p, "Wrong key used to free-disable");
		}
	}
	
	h->guardband = BASE_GUARDBAND_WHEN_UNFREEABLE;
}

void memFreeEnable(void* p, U32 key){
	MemTrackHeader* h = (MemTrackHeader *)(((U8*)p) - PREFIX_BYTES);
	MemTrackHeader* hReal = h;
	
	UNDO_MEMALIGN(hReal);
	assert(GET_BASE_GUARDBAND(h) == BASE_GUARDBAND_WHEN_UNFREEABLE);
	if(GET_NAME_IDX(hReal) != key){
		reportMismatch(hReal, p, "Wrong key used to free-enable");
	}
	
	h->guardband = BASE_GUARDBAND_WHEN_ALLOCATED;
}

void free_timed_size(void *p,int blockType,size_t size)
{
	bool in_validate_critsec = false;
	if (!p)
		return;
#if ENABLE_OVERRIDES
    if (g_free_func)
	{
		g_free_func(p,blockType);
		return;
	}
#endif
	checkHeapState();
	{
		MemTrackHeader	*curr = (MemTrackHeader *) (((U8 *)p) - PREFIX_BYTES);

		PERFINFO_ALLOC_START("free", GET_ALLOC_SIZE(curr));

		if(allow_free_callback && freeCheckCB)
		{
			if(curr->name_idx_high==freeCheckShadow.name_idx_high &&
				curr->name_idx_low==freeCheckShadow.name_idx_low &&
				curr->small_size==freeCheckShadow.small_size &&
				(curr->small_size!=0xff || curr->large_size==freeCheckShadow.large_size))
			{
				freeCheckCB(p);
			}
		}

		if(GET_BASE_GUARDBAND(curr) != BASE_GUARDBAND_WHEN_ALLOCATED){
			if(GET_BASE_GUARDBAND(curr) == BASE_GUARDBAND_WHEN_UNFREEABLE){
				UNDO_MEMALIGN(curr);
				reportMismatch(curr, p, "Trying to free unfreeable allocation");
			}
			// The following code used to allow free() on a raw CRT pointer to succeed without any error, on the logic
			// that this would still catch totally bad pointers.  This might be true, but it still seriously
			// undermines the guardband mechanism, meaning that free() won't actually catch all bad pointers.  It 
			// isn't really the best idea to trust the CRT heap to detect invalid frees anyway.  Also, when this
			// condition occurred, it could put MemTrack into an inconsistent state, because it won't correctly
			// register the free(), and again, there would be no record or reporting of this condition.  If there are
			// any locations allocating from the CRT alloc but trying to free with free(), they are wrong, and we
			// should correct them.  The main place this comes up is CRT functions that internally call malloc()
			// and return a pointer to that, where we haven't overridden the function with our own version.  In this
			// case, crt_free() should be used, or we should write a wrapper.
			//else{
			//	// maybe it's from the CRT allocator, if not, it will crash anyhow.
			//	free(p);
			//}
		}	//else
		{
			size_t	old_size;
			int		heap_index;
			HANDLE *heap;

			UNDO_MEMALIGN(curr);
			old_size = GET_ALLOC_SIZE(curr);
			if(size){
				assert(size == old_size);
			}

			heap_index = GET_SPECIAL_HEAP_INDEX(curr);
			if (heap_index)
				heap = special_heaps[heap_index];
			else
				heap = my_heap;
			if (!heap_index)
				checkGuardBand(curr,old_size);
			if(clear_guardbands)
			{
				// Change the guardbands to the free value.
				SET_BASE_GUARDBAND(curr, MALLOC_GUARDBANDFREED_VALUE);
				SET_END_GUARDBAND(curr, old_size, MALLOC_GUARDBANDFREED_VALUE);

				// Copy the MemTrackHeader into the memory itself so we know who used to own the memory.
				copy_freed_headers(curr, old_size, NULL);
			}
			freeMemOp(old_size,GET_NAME_IDX(curr), curr);
			if (fill_on_free)
			{
				PERFINFO_AUTO_START("fill", 1);
				memset(curr->data,FREE_VALUE,old_size);
				PERFINFO_AUTO_STOP();
			}

			ENTER_VALIDATE_CRITSEC;
			if (old_size < MAX_SMALL_SIZE)
				try_free(heap, (((U8*)curr) + PREFIX_EXTEND_BYTES));
			else
				try_free(heap, curr);
			LEAVE_VALIDATE_CRITSEC;
		}
		PERFINFO_ALLOC_END();
	}
	checkHeapState();
}

void free_timed(void *p,int blockType)
{
	free_timed_size(p, blockType, 0);
}

void *aligned_malloc_dbg(size_t size, int align_bytes, const char *filename, int linenumber)
{
	size_t			mem,align_mem;
	U8				*align_ptr;
	MemTrackHeader	*track;

	mem = (size_t) malloc_timed(size + align_bytes + PREFIX_MIN_BYTES,_NORMAL_BLOCK,filename,linenumber);
	align_mem = (mem + PREFIX_MIN_BYTES + align_bytes-1) & ~(align_bytes-1);
	align_ptr = (void *)align_mem;
	track = (MemTrackHeader*) (align_mem - PREFIX_BYTES);
	track->name_idx_high = 255;
	track->name_idx_low = align_bytes;
	SET_BASE_GUARDBAND(track,MALLOC_NOMANSLAND_VALUE);
	track->small_size = (U8)(align_mem - mem);
	return align_ptr;
}

void *aligned_calloc_dbg(size_t size, int align_bytes, const char *filename, int linenumber)
{
	void *r = aligned_malloc_dbg(size, align_bytes, filename, linenumber);
	ZeroMemory(r, size);
	return r;
}

#define PAGE_SIZE 4096

void * guarded_aligned_malloc(GuardedAlignedMalloc * gam_header, size_t allocation_size, size_t pre_guard_pages, size_t post_guard_pages)
{
	size_t total_allocation_size = ALIGNUP(allocation_size, PAGE_SIZE) + (pre_guard_pages + post_guard_pages) * PAGE_SIZE + PAGE_SIZE;
	uintptr_t base_page_address;
	DWORD prior_protection_unused = 0;

	assert(!gam_header->malloc_result);

	gam_header->malloc_result = (U8*)malloc(total_allocation_size);
	memset(gam_header->malloc_result, 0, total_allocation_size);
	base_page_address = ALIGNUP((uintptr_t)gam_header->malloc_result, PAGE_SIZE);

	gam_header->pre_guard = (U8*)base_page_address;
	gam_header->pre_guard_size = pre_guard_pages * PAGE_SIZE;

	gam_header->aligned_protected_memory = gam_header->pre_guard + PAGE_SIZE * pre_guard_pages;

	gam_header->post_guard = gam_header->aligned_protected_memory + ALIGNUP(allocation_size, PAGE_SIZE);
	gam_header->post_guard_size = post_guard_pages * PAGE_SIZE;

	ANALYSIS_ASSUME(gam_header->pre_guard != NULL);
	VirtualProtect(gam_header->pre_guard, gam_header->pre_guard_size, PAGE_NOACCESS, &prior_protection_unused);
	VirtualProtect(gam_header->post_guard, gam_header->post_guard_size, PAGE_NOACCESS, &prior_protection_unused);

	return gam_header->aligned_protected_memory;
}

void guarded_aligned_free(GuardedAlignedMalloc * gam_header)
{
	void * malloc_block = gam_header->malloc_result;
	DWORD prior_protection_unused = 0;

	VirtualProtect(gam_header->pre_guard, gam_header->pre_guard_size, PAGE_READWRITE, &prior_protection_unused);
	VirtualProtect(gam_header->post_guard, gam_header->post_guard_size, PAGE_READWRITE, &prior_protection_unused);

	ZeroMemory(gam_header, sizeof(*gam_header));

	free(malloc_block);
}

void *realloc_memalign(void *p, size_t size,int blockType, const char *filename, int linenumber)
{
	MemTrackHeader	*curr = (MemTrackHeader*) (((U8 *)p) - PREFIX_BYTES);
	U8	*dst,align_bytes;

	align_bytes = curr->name_idx_low;
	UNDO_MEMALIGN(curr);
	dst = aligned_malloc_dbg(size,align_bytes,filename,linenumber);
	memcpy(dst,p,MIN(GET_ALLOC_SIZE(curr),size));
	free_timed(p,blockType);
	checkHeapState();
	return dst;
}

void *realloc_timed(void *p, size_t size,int blockType, const char *filename, int linenumber)
{
	void *result;

	checkHeapState();

	if (p)
	{
		MemTrackHeader	*curr = (MemTrackHeader*) (((U8 *)p) - PREFIX_BYTES);

		if (IS_MEMALIGN(curr))
			return realloc_memalign(p,size,blockType, filename,linenumber);
	}

	result = realloc_core(p,size,blockType,false,filename,linenumber);

	checkHeapState();

	return result;
}

void *realloc_timed_canfail(void *p, size_t size,int blockType,bool can_fail, const char *filename, int linenumber)
{
	void *result;

	checkHeapState();

	if (p)
	{
		MemTrackHeader	*curr = (MemTrackHeader*) (((U8 *)p) - PREFIX_BYTES);

		if (IS_MEMALIGN(curr))
			return realloc_memalign(p,size,blockType, filename,linenumber);
	}

	result = realloc_core(p,size,blockType,can_fail,filename,linenumber);

	checkHeapState();

	return result;
}


MEMTRACK_FORCEINLINE static int trackMemOp(const char *moduleName, int linenum, bool bSlotNumIsKnown, int iSlotNum, MemTrackType sizeDelta, MemTrackType countDelta)
{
	AllocTracker	*slot;
	AllocTracker	*total;
	int				slot_idx,thread_id;

	if (moduleName && sizeDelta > 0 && memMonitorBreakOnAlloc[0] && strEndsWith(moduleName, memMonitorBreakOnAlloc) &&
		(!memMonitorBreakOnAllocLine || memMonitorBreakOnAllocLine == linenum))
	{
		_DbgBreak();
		if (!memMonitorBreakOnAllocDisableReset)
			memMonitorBreakOnAlloc[0]='\0';
	}

	// check to make sure string probably isn't on the stack
#if _FULLDEBUG
	{
		char	curr_stack;
		ptrdiff_t stack_distance;

		if (moduleName)
		{
			stack_distance = moduleName - &curr_stack;
			devassert(stack_distance < 0 || stack_distance > 16384);
		}
	}
#endif

	// See addMemOp() for a description of what's going on in this function.

	thread_id = threadId();
	if (bSlotNumIsKnown)
	{
		slot_idx = iSlotNum;
	}
	else
	{
		slot_idx = findEntry(moduleName,linenum);
	}
	if (!memtrack_names[slot_idx].user_alloc)
		memtrack_names[slot_idx].user_alloc = 1;
	slot = &memtrack_counts[thread_id][slot_idx];

	total = &memtrack_counts[thread_id][MEMTRACK_MAXENTRIES];

	if (thread_id == MEMTRACK_THREAD_MAX-1)
	{
		safeAdd(slot->num_allocs, countDelta);
		safeAdd(slot->total_allocs, ABS(countDelta));
		safeAdd(slot->bytes, sizeDelta);

		safeAdd(total->bytes, sizeDelta);
		safeAdd(total->num_allocs, countDelta);
		safeAdd(total->total_allocs, ABS(countDelta));
	} else {
		slot->num_allocs+=countDelta;
		slot->total_allocs+=ABS(countDelta);
		slot->bytes+=sizeDelta;

		total->num_allocs+=countDelta;
		total->total_allocs+=ABS(countDelta);
		total->bytes+=sizeDelta;
	}

	return slot_idx;
}

void memMonitorTrackUserMemory(const char *moduleName, int linenum, MemTrackType sizeDelta, MemTrackType countDelta)
{
	// JE: Most things are passing "staticModuleName" as "linenum", which will crash on stack-allocated temp strings!
	trackMemOp(moduleName,linenum, false, 0, sizeDelta,countDelta);
}

int memTrackUpdateStatsByName(const char *moduleName, int linenum, MemTrackType sizeDelta, MemTrackType countDelta)
{
	return trackMemOp(moduleName,linenum, false, 0, sizeDelta,countDelta);
}

void memTrackUpdateStatsBySlotIdx(int iSlotIdx, MemTrackType sizeDelta, MemTrackType countDelta)
{
	trackMemOp(NULL, 0, true, iSlotIdx, sizeDelta, countDelta);
}


void memMonitorUpdateStatsShared(const char *moduleName, MemTrackType size)
{
	int		slot_idx;

	slot_idx = trackMemOp(moduleName,1,false, 0,size,1);
	memtrack_names[slot_idx].shared_mem = 1;
}

#if _PS3 || _XBOX && defined(PROFILE)

bool memTrackForEachAlloc(void *heap, ForEachAllocCallback callback, void *userData, int retry_count, bool check_cryptic, int special)
{
	return true;
}

#else

static size_t maybe_bad_found = 0;
static void *bad_address_found = 0;
static bool bad_heapwalk_crashed;
static bool bad_heapwalk_looped;
static PROCESS_HEAP_ENTRY	last_good_entry;


static int heapWalkExcept(unsigned int code, PEXCEPTION_POINTERS info)
{
	return EXCEPTION_EXECUTE_HANDLER;
}

// Ensures heap is valid and walks it
// Returns false if it failed for some reason
bool memTrackForEachAlloc(void *heap_ptr, ForEachAllocCallback callback, void *userData, int retry_count, bool check_cryptic, int special)
{
	HANDLE *heap = heap_ptr;
	size_t	bad,good,total,little,big;
	bool in_validate_critsec = false;
	size_t total_mem = getProcessPageFileUsage();

	if (!heap)
		heap = my_heap;

	assert(retry_count >= 0);
	assert(!callback || retry_count == 0); // If you're passing a callback, and retrying, you'll get the same data multiple times - do your own retry in the calling function

	do_validate_critsec = 1;
	if (!HeapValidate(heap, 0, NULL))
	{
		return false;
	}
	ENTER_VALIDATE_CRITSEC;
	do
	{
		PROCESS_HEAP_ENTRY	entry = {0};
		size_t num_walks = 0;

		bad = good = total = little = big = maybe_bad_found = 0;
		bad_address_found = NULL;
		bad_heapwalk_crashed = false;
		bad_heapwalk_looped = false;
		for(;;)
		{
			MemTrackHeader	*curr;
			size_t			size;
			ptrdiff_t chunk_size;
			bool bBreak=false;

			__try {
				if (!HeapWalk(heap,&entry))
					bBreak = true;
			}
			__except (heapWalkExcept(GetExceptionCode(), GetExceptionInformation()))
			{
				OutputDebugStringf("memTrackValidateHeap: HeapWalk crashed\n");
				bad++;
				bad_address_found = NULL;
				bad_heapwalk_crashed = true;
				bBreak = true;
			}
			if (bBreak)
				break;

			num_walks++;

			if (num_walks > (total_mem >> 3))
			{
				OutputDebugStringf("memTrackValidateHeap: Infinite loop walking heap, last walked was %p\n", entry.lpData);
				bad++;
				bad_heapwalk_looped = true;
				break;
			}

			last_good_entry = entry;
			chunk_size = entry.cbData;
			if (!(entry.wFlags == PROCESS_HEAP_ENTRY_BUSY || // use == instead of & because i don't want any of the other options
				entry.wFlags == (PROCESS_HEAP_ENTRY_DDESHARE|PROCESS_HEAP_ENTRY_BUSY))) // All giant allocs have this flag set, not sure what it is
				continue;
			if (!total++)
				continue;

			// Everything that follows is Cryptic heap-specific checking of MemTrackHeaders.
			if (!check_cryptic)
				continue;

			#if _M_X64
			curr = entry.lpData;
			size = GET_ALLOC_SIZE(curr);

			#else
			if (entry.cbData < MAX_SMALL_SIZE + WRAPPER_BYTES - PREFIX_EXTEND_BYTES)
			{
				curr = (MemTrackHeader *) (((char *)entry.lpData) - PREFIX_EXTEND_BYTES);
				size = curr->small_size;
			}
			else
			{
				curr = entry.lpData;
				size = curr->large_size;
				if (size && (size >= entry.cbData || size < MAX_SMALL_SIZE))
				{
					curr = (MemTrackHeader *) (((char *)entry.lpData) - PREFIX_EXTEND_BYTES);
					size = curr->small_size;
				}
			}
			#endif
			if (size + WRAPPER_BYTES - PREFIX_EXTEND_BYTES > entry.cbData || !CHECK_BASE_GUARDBAND(curr))
			{
				// when using the LFH, it reports some chunks we neved alloced (probably from pooling) so we don't know for sure they are corrupt
				maybe_bad_found++;
			}
			else
			{
				if (!CHECK_END_GUARDBAND(curr,size))
				{
					OutputDebugStringf("memTrackValidateHeap: Bad end guardband on allocation at %p, size %Iu\n", curr, size);
					bad++;
					bad_address_found = curr;
					break;
				}
				else
				{
					good++;
					if (callback)
					{
						int slot_idx = GET_NAME_IDX(curr);
						callback(userData, &curr->data[0], size, memtrack_names_pooled[memtrack_names[slot_idx].pooled_index].filename, memtrack_names[slot_idx].line, slot_idx, memtrack_names_pooled[memtrack_names[slot_idx].pooled_index].budget_stats);
					}
				}
				if (size < 256)
					little++;
				else
					big++;
			}
		}
		if (!bad)
			break;
	} while (--retry_count >= 0);
	LEAVE_VALIDATE_CRITSEC;
	do_validate_critsec = 0;

	if (bad && bad_address_found == NULL && usingLFH && bad_heapwalk_crashed && !IsUsingVista())
	{
		OutputDebugStringf("memTrackValidateHeap: HeapWalk crashed, but we're using the LFH on XP, ignoring this and pretending the heap is fine.\n");
		return true;
	}
	return (bool)!bad;
}
#endif

int memTrackValidateHeap()
{
	bool result;
	int i;
	
	// Verify Cryptic heap.
	result = memTrackForEachAlloc(my_heap, NULL, NULL, VALIDATE_RETRY, true, 0);

	// Verify default process heap.
	result = memTrackForEachAlloc(default_heap, NULL, NULL, VALIDATE_RETRY, false, 0) && result;

	// Verify each special heap.
	for (i = CRYPTIC_FIRST_HEAP + 1; i != CRYPTIC_END_HEAP; ++i)
	{
		if (0 && special_heaps[i]) // TODO: The verifier needs to be made to understand our headers.
			result = memTrackForEachAlloc(special_heaps[i], NULL, NULL, VALIDATE_RETRY, true, i) && result;
	}

	return result;
}

///////////////////// STUBS ///////////////////////////////
int g_inside_pool_malloc;

#if !_PS3

#undef timed_HeapAlloc
#undef timed_HeapReAlloc
#undef timed_HeapFree
#undef timed_HeapCreate
#undef timed_HeapDestroy

//////////////////// Not 100% sure why we need this
LPVOID timed_HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes)
{
	LPVOID ret;
	PERFINFO_AUTO_START("HeapAlloc", 1);
	ret = (LPVOID)HeapAlloc(hHeap, dwFlags, dwBytes);
	PERFINFO_AUTO_STOP();
	return ret;
}

HANDLE timed_HeapCreate(DWORD flOptions, SIZE_T dwInitialSize, SIZE_T dwMaximumSize)
{
	HANDLE hHeap;
	PERFINFO_AUTO_START("HeapCreate", 1);
	hHeap = HeapCreate(flOptions, dwInitialSize, dwMaximumSize);
	PERFINFO_AUTO_STOP();
	return hHeap;
}

BOOL timed_HeapDestroy(HANDLE hHeap)
{
	BOOL ret;
	PERFINFO_AUTO_START("HeapDestroy", 1);
	ret = HeapDestroy(hHeap);
	PERFINFO_AUTO_STOP();
	return ret;
}

LPVOID timed_HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes)
{
	LPVOID ret;
	PERFINFO_AUTO_START("HeapReAlloc", 1);
	ret = (LPVOID)HeapReAlloc(hHeap, dwFlags, lpMem, dwBytes);
	PERFINFO_AUTO_STOP();
	return ret;
}

BOOL timed_HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem)
{
	BOOL ret;
	PERFINFO_AUTO_START("HeapFree", 1);
	ret = HeapFree(hHeap, dwFlags, lpMem);
	PERFINFO_AUTO_STOP();
	return ret;
}
#endif

#include "mathutil.h"
//AUTO_RUN;
void memSpeedTest(void)
{
	int timer = timerAlloc();
	void *data[100];
	int sizes[ARRAY_SIZE(data)];
	int i, j;
	U64 count=0;
	for (i=0; i<ARRAY_SIZE(sizes); i++)
	{
		sizes[i] = randInt(50);
	}
	timerStart(timer);
	count=0;
	for (i=0; i<10000; i++)
	{
		for (j=0; j<ARRAY_SIZE(data); j++)
		{
			data[j] = malloc_timed(sizes[j], _NORMAL_BLOCK, __FILE__, __LINE__);
		}
		for (j=0; j<ARRAY_SIZE(data); j++)
		{
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*malloc_timed(sizes[j]..."
			free_timed(data[j], _NORMAL_BLOCK);
		}
		count+=ARRAY_SIZE(data)*2;
	}
	printf("%1.3fM memory ops/second (%1.3fs spent timing)\n", count / (F32)timerElapsed(timer) / 1000000.f, timerElapsed(timer));
	timerFree(timer);
}

int cmpAllocName(const void *_a, const void *_b)
{
	const AllocName *a = (const AllocName*)_a;
	const AllocName *b = (const AllocName*)_b;
	const AllocNamePooled *ap = &memtrack_names_pooled[a->pooled_index];
	const AllocNamePooled *bp = &memtrack_names_pooled[b->pooled_index];
	int r;

	r = stricmp(ap->filename, bp->filename);
	if (r)
		return r;
	r = a->line - b->line;
	assert(r);
	return r;
}

typedef struct NameCountPair
{
	const char *name;
	int count;
} NameCountPair;

int cmpNameCountPair(const void *_a, const void *_b)
{
	const NameCountPair *a = (const NameCountPair*)_a;
	const NameCountPair *b = (const NameCountPair*)_b;
	int r;

	r = a->count - b->count;
	return r;
}

AUTO_COMMAND;
void memTrackEntriesAnalyze(void)
{
	AllocName		*names_dup;
	NameCountPair	*pairs = calloc_timed(sizeof(NameCountPair), memtrack_total, _NORMAL_BLOCK, __FILE__, __LINE__);

	int i;
	int lasti;
	int numpairs;
	names_dup = memdup(memtrack_names, sizeof(memtrack_names));
	qsort(names_dup, memtrack_total, sizeof(names_dup[0]), cmpAllocName);

	lasti=0;
	numpairs=0;
	for (i=0; i<memtrack_total; i++)
	{
		if (memtrack_names_pooled[names_dup[i].pooled_index].filename != memtrack_names_pooled[names_dup[lasti].pooled_index].filename)
		{
			numpairs++;
			pairs[numpairs].count = 0;
			lasti = i;
		}
		pairs[numpairs].name = memtrack_names_pooled[names_dup[i].pooled_index].filename;
		pairs[numpairs].count++;
	}

	qsort(pairs, numpairs, sizeof(pairs[0]), cmpNameCountPair);
	
	free_timed(names_dup, _NORMAL_BLOCK);
	free_timed(pairs, _NORMAL_BLOCK);
}

// Reserve a large memory chunk to be sure that it is available later.
void memTrackReserveMemoryChunk(size_t chunk_size)
{
	size_t original_size = chunk_size;

	// Validate.
	if (reserved_region_commit_size || !chunk_size)
		return;

	// Resize request.
	if (reserved_region)
	{
		bool success;
		PERFINFO_AUTO_START("VirtualFree", 1);
		success = VirtualFree(reserved_region, 0, MEM_RELEASE);
		PERFINFO_AUTO_STOP();
		assert(success);
	}

	// Try to allocate the chunk.
	do
	{
		// Try to allocate.
		PERFINFO_AUTO_START("VirtualAlloc", 1);
		reserved_region = VirtualAlloc(NULL, chunk_size, MEM_RESERVE, PAGE_READWRITE);
		PERFINFO_AUTO_STOP();

		// Make the chunk smaller and try again.
		if (!reserved_region && chunk_size - 11 <= original_size/10)
			break;										// Failure.
		if (!reserved_region)
			chunk_size -= original_size/10;
	} while(!reserved_region);
}

// Return true if the reserve memory chunk is in use.
bool memTrackIsReserveMemoryChunkBusy()
{
	return !!reserved_region_commit_size;
}

static int memTrackCountInternalHeapSegments_PreVista(int blockType)
{
	HANDLE *curHeap;
	if (blockType > CRYPTIC_FIRST_HEAP && blockType <= CRYPTIC_LAST_HEAP)
	{
		curHeap = special_heaps[blockType];
	}
	else
	{
		curHeap = my_heap;
	}

	if(!curHeap)
		return -6;

	// Lock the heap.
	if (!HeapLock(curHeap))
		return -2;

	__try
	{
		char *heap = (char *)curHeap;
		U32 *signature = (U32 *)(heap + 0x010);
		U32 *flags = (U32 *)(heap + 0x014);
		void **segments = (void **)(heap + 0x0a0);
		int i;

		// Make sure the heap looks like we expect.
		if (*signature != 0xeeffeeff)
		{
			HeapUnlock(curHeap);
			return -3;
		}
		
		// Count number of segments in use.
		for (i = 0; i != 64; ++i)
		{
			if (!segments[i])
				break;
		}
		HeapUnlock(curHeap);
		return i;
	}
#pragma warning(suppress:6320)		//Exception-filter is the constant...
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		HeapUnlock(curHeap);
		return -4;  // Failure when trying to analyze heap.
	}
}

static int memTrackCountInternalHeapSegments_Win7(int blockType)
{
	HANDLE *curHeap;
	if (blockType > CRYPTIC_FIRST_HEAP && blockType <= CRYPTIC_LAST_HEAP)
	{
		curHeap = special_heaps[blockType];
	}
	else
	{
		curHeap = my_heap;
	}

	if(!curHeap)
		return -6;

	// Lock the heap.
	if (!HeapLock(curHeap))
		return -2;

	__try
	{
		char *heap = (char *)curHeap;
		U32 *signature = (U32 *)(heap + 0x010);
		U32 *flags = (U32 *)(heap + 0x014);
		char *counters = (heap + 0x188);
		int count;

		// Make sure the heap looks like we expect.
		if (*signature != 0xffeeffee)
		{
			HeapUnlock(curHeap);
			return -3;
		}
		
		count = *(U32*)(counters + 0x20);
		HeapUnlock(curHeap);
		return count;
	}
#pragma warning(suppress:6320)		//Exception-filter is the constant...
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		HeapUnlock(curHeap);
		return -4;  // Failure when trying to analyze heap.
	}
}

// Count the number of heap segments that are in active use.
int memTrackCountInternalHeapSegments(int blockType)
{
#if _M_X64

	// Only implemented on pre-Vista x64 heaps
	// TODO: Implement for Vista and higher heaps.
	if (IsUsingWin7())
		return memTrackCountInternalHeapSegments_Win7(blockType);

	if (IsUsingVista())
		return -1;

	return memTrackCountInternalHeapSegments_PreVista(blockType);

#else
	return -5;  // Not implemented
#endif
}

// Get the size of the largest free block.
size_t memTrackGetInternalLargestFreeBlockSize(int blockType)
{
	size_t size = 0;
	HANDLE *heap;
	PERFINFO_AUTO_START_FUNC();

	if (blockType > CRYPTIC_FIRST_HEAP && blockType <= CRYPTIC_LAST_HEAP)
	{
		heap = special_heaps[blockType];
	}
	else
	{
		heap = my_heap;
	}

	if(heap)
		size = HeapCompact(heap, 0);

	PERFINFO_AUTO_STOP();
	return size;
}

bool memTrackGetWin7InternalHeapData(int blockType, Win7x64HeapCounters *countersOut)
{
	size_t size = 0;
	HANDLE *curHeap;
	assert(countersOut);

	PERFINFO_AUTO_START_FUNC();

	if (blockType > CRYPTIC_FIRST_HEAP && blockType <= CRYPTIC_LAST_HEAP)
	{
		curHeap = special_heaps[blockType];
	}
	else
	{
		curHeap = my_heap;
	}

	// Lock the heap.
	if (!HeapLock(curHeap))
		return false;

	__try
	{
		char *heap = (char *)curHeap;
		U32 *signature = (U32 *)(heap + 0x010);
		U32 *flags = (U32 *)(heap + 0x014);
		char *counters = (heap + 0x188);
		char *blocksIndex;

		// Make sure the heap looks like we expect.
		if (*signature != 0xffeeffee)
		{
			HeapUnlock(curHeap);
			return false;
		}
		
		countersOut->TotalMemoryReserved =		*(U64*)(counters);
		countersOut->TotalMemoryCommitted =		*(U64*)(counters + 0x8);
		countersOut->TotalMemoryLargeUCR =		*(U64*)(counters + 0x10);
		countersOut->TotalSizeInVirtualBlocks = *(U64*)(counters + 0x18);
		countersOut->TotalSegments =			*(U32*)(counters + 0x20);
		countersOut->TotalUCRs =				*(U32*)(counters + 0x24);
		countersOut->CommittOps =				*(U32*)(counters + 0x28);
		countersOut->DeCommitOps =				*(U32*)(counters + 0x2c);
		countersOut->LockAcquires =				*(U32*)(counters + 0x30);
		countersOut->LockCollisions =			*(U32*)(counters + 0x34);
		countersOut->CommitRate =				*(U32*)(counters + 0x38);
		countersOut->DecommittRate =			*(U32*)(counters + 0x3c);
		countersOut->CommitFailures =			*(U32*)(counters + 0x40);
		countersOut->InBlockCommitFailures =	*(U32*)(counters + 0x44);
		countersOut->CompactHeapCalls =			*(U32*)(counters + 0x48);
		countersOut->CompactedUCRs =			*(U32*)(counters + 0x4c);
		countersOut->AllocAndFreeOps =			*(U32*)(counters + 0x50);
		countersOut->InBlockDeccommits =		*(U32*)(counters + 0x54);
		countersOut->InBlockDeccomitSize =		*(U64*)(counters + 0x58);
		countersOut->HighWatermarkSize =		*(U64*)(counters + 0x60);
		countersOut->LastPolledSize =			*(U64*)(counters + 0x68);

		blocksIndex = *(char**)(heap + 0x140);

		if(blocksIndex)
		{
			char *extended = *(char**)(blocksIndex);
			if(extended)
			{
				countersOut->LargeFreeListSize = *(U32*)(extended+0x10);
			}
		}

		HeapUnlock(curHeap);
		return true;
	}
#pragma warning(suppress:6320)		//Exception-filter is the constant...
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		HeapUnlock(curHeap);
		return false;  // Failure when trying to analyze heap.
	}


	PERFINFO_AUTO_STOP();
}

#undef malloc
#undef calloc
#undef realloc
#undef free

void *crt_malloc(size_t size)
{
	return malloc(size);
}

void *crt_calloc(size_t num, size_t size)
{
	return calloc(num, size);
}

void *crt_realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

void  crt_free(void *ptr)
{
	free(ptr);
}
