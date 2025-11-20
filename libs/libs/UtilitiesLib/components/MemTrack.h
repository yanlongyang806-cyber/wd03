#pragma once
GCC_SYSTEM

typedef struct ModuleMemOperationStats ModuleMemOperationStats;

#if !_M_X64
#	define MEMTRACK_64BIT 0
	typedef ptrdiff_t MemTrackType;
#else
	typedef S64 MemTrackType;
#	define MEMTRACK_64BIT 1
#endif

#if PLATFORM_CONSOLE
	// Console clients
// 	// 1-thread mode uses interlocked operations (relatively slow)
// #	define MEMTRACK_THREAD_MAX 1
// #	define memtrack_thread_count 1
//	// More threads lets tracking operations in all threads be fast
//#	define MEMTRACK_THREAD_MAX 64
// 	extern int	memtrack_thread_count;

	// 2-thread mode uses the fast mode for the main thread, interlocked for other threads
#	define MEMTRACK_THREAD_MAX 2
	extern int	memtrack_thread_count;

#	define MEMTRACK_MAXENTRIES 7000
#	define MEMTRACK_MAX_UNIQUE_FILENAMES 2000
#	define MEMTRACK_SLOTS_MAX 8192      // MEMTRACK_SLOTS_MAX must be power of 2!
#elif !_M_X64
	// PC Client, 32-bit Servers (GameServer), all misc apps
#	define MEMTRACK_THREAD_MAX 2
#	define MEMTRACK_MAXENTRIES 12000
#	define MEMTRACK_MAX_UNIQUE_FILENAMES 4096 // Cannot be increased without increasing the size of AllocName
#	define MEMTRACK_SLOTS_MAX 32768     // MEMTRACK_SLOTS_MAX must be power of 2!
	extern int	memtrack_thread_count;
#else
	// Object DB, large servers
#	define MEMTRACK_THREAD_MAX 64
#	define MEMTRACK_MAXENTRIES 10000
#	define MEMTRACK_MAX_UNIQUE_FILENAMES 4096 // Cannot be increased without increasing the size of AllocName
#	define MEMTRACK_SLOTS_MAX 65536     // MEMTRACK_SLOTS_MAX must be power of 2!
	extern int	memtrack_thread_count;
#endif

// Currently, only enable OOM exceptions in 64-bit mode.
#if _M_X64
#define MEMTRACK_DEFAULT_ENABLE_OOM_EXCEPTIONS
#endif

// Currently, this tracking is only needed for large memory servers, so only enable it there to avoid slowdowns elsewhere.
#ifdef _M_X64
#define MEMTRACK_UPDATE_MEMOP_COUNTERS
#define MEMTRACK_UPDATE_MEMOP_COUNTERS_DELTAS
//#define MEMTRACK_UPDATE_MEMOP_COUNTERS_COUNTS  // Currently, the performance overhead of this is not worth it, given that deltas work
#endif

typedef struct AllocNamePooled // Shared between multiple AllocNames
{
	const char		*filename;
	ModuleMemOperationStats	*budget_stats;
} AllocNamePooled;

typedef struct AllocName // Exactly 32 bits
{
	U32				pooled_index : 12;
	U32				line : 17;
	U32				user_alloc : 1; // These 3 bits could be combined into a 2-bit enum if we need 1 more bit
	U32				shared_mem : 1;
	U32				video_mem : 1;
} AllocName;
STATIC_ASSERT(sizeof(AllocName) == 4);

// Track some summary information about each allocation site.
// Note: If you change this, you must change AllocTrackerDetailed also.
typedef struct AllocTracker
{
	MemTrackType	bytes;				// Total outstanding allocation size, in bytes
	MemTrackType	num_allocs;			// Number of outstanding allocations
	MemTrackType	total_allocs;		// Cumulative count of number of allocations
} AllocTracker;

typedef struct GuardedAlignedMalloc
{
	U8 * malloc_result;
	U8 * aligned_protected_memory;

	U8 * pre_guard;
	size_t pre_guard_size;

	U8 * post_guard;
	size_t post_guard_size;
} GuardedAlignedMalloc;


extern AllocNamePooled	memtrack_names_pooled[MEMTRACK_MAX_UNIQUE_FILENAMES];
extern AllocName		memtrack_names[MEMTRACK_MAXENTRIES];
extern AllocTracker		*memtrack_counts[MEMTRACK_THREAD_MAX];
extern int				memtrack_total;

void initMemTrack(void);

int memTrackUpdateStatsByName(const char *moduleName, int linenum, MemTrackType sizeDelta, MemTrackType countDelta);
void memTrackUpdateStatsBySlotIdx(int iSlotIdx, MemTrackType sizeDelta, MemTrackType countDelta);
void memTrackFreeBySlotIdx(int iSlotIdx, MemTrackType iAmount, void *pData);

int memTrackValidateHeap(void);
void *aligned_malloc_dbg(size_t size, int align_bytes, const char *filename, int linenumber);
void *aligned_calloc_dbg(size_t size, int align_bytes, const char *filename, int linenumber);
void * guarded_aligned_malloc(GuardedAlignedMalloc * gam_header, size_t allocation_size, size_t pre_guard_pages, size_t post_guard_pages);
void guarded_aligned_free(GuardedAlignedMalloc * gam_header);

size_t GetAllocSize(void *p);

typedef void (*MemFreeCallback)(const void *mem);
// Pass in an object allocated from the location/type/size for which you want to check
// Does NOT take ownership, so free it afterwards
void memTrackRegisterFreeCallback(const void *ptrCheck, MemFreeCallback cb);

extern OutOfMemoryCallback g_OutOfMemoryCallback;

typedef struct ModuleMemOperationStats ModuleMemOperationStats;
// name_idx is a unique index for this filename+linenum pair which can be useful for the callback to hash on/etc
typedef void (*ForEachAllocCallback)(void *userData, void *mem, size_t size, const char *filename, int linenum, int name_idx, ModuleMemOperationStats *stats);
// Ensures heap is valid and walks it, returns false if it failed for some reason
bool memTrackForEachAlloc(void *heap, ForEachAllocCallback callback, void *userData, int retry_count, bool check_cryptic, int special);

// Reserve a large memory chunk to be sure that it is available later.
void memTrackReserveMemoryChunk(size_t chunk_size);

// Return true if the reserve memory chunk is in use.
bool memTrackIsReserveMemoryChunkBusy(void);

// Return true iff the requested heap has been initialized
bool IsHeapInitialized(int blockType);

// Count the number of heap segments that are in active use.
int memTrackCountInternalHeapSegments(int blockType);

// Get the size of the largest free block.
size_t memTrackGetInternalLargestFreeBlockSize(int blockType);

// Call the CRT allocator, in case you have some specific reason to need a CRT heap pointer, instead of a MemTrack pointer.
void *crt_malloc(size_t size);
void *crt_calloc(size_t num, size_t size);
void *crt_realloc(void *ptr, size_t size);
void  crt_free(void *ptr);

typedef struct Win7x64HeapCounters
{
	U64 TotalMemoryReserved;
	U64 TotalMemoryCommitted;
	U64 TotalMemoryLargeUCR;
	U64 TotalSizeInVirtualBlocks;
	U32 TotalSegments;
	U32 TotalUCRs;
	U32 CommittOps;
	U32 DeCommitOps;
	U32 LockAcquires;
	U32 LockCollisions;
	U32 CommitRate;
	U32 DecommittRate;
	U32 CommitFailures;
	U32 InBlockCommitFailures;
	U32 CompactHeapCalls;
	U32 CompactedUCRs;
	U32 AllocAndFreeOps;
	U32 InBlockDeccommits;
	U64 InBlockDeccomitSize;
	U64 HighWatermarkSize;
	U64 LastPolledSize;
	U32 LargeFreeListSize;
} Win7x64HeapCounters;

bool memTrackGetWin7InternalHeapData(int blockType, Win7x64HeapCounters *countersOut);
