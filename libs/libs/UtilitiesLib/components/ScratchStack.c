#include "AtExit.h"
#include "earray.h"
#include "error.h"
#include "EString.h"
#include "file.h"
#include "logging.h"
#include "MemoryPool.h"
#include "ScratchStack.h"
#include "ThreadSafeMemoryPool.h"
#include "timing.h"
#include "UnitSpec.h"
#include "wininclude.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

int gScratchStackVerboseDebug = 0;
AUTO_CMD_INT(gScratchStackVerboseDebug, ScratchStackVerboseDebug) ACMD_COMMANDLINE;

int gScratchStackPrintResizes = 0;
AUTO_CMD_INT(gScratchStackPrintResizes, ScratchStackPrintResizes) ACMD_COMMANDLINE;

int gScratchStackDisableResize = 0;
AUTO_CMD_INT(gScratchStackDisableResize, ScratchStackDisableResize) ACMD_COMMANDLINE;

#define SCRATCHSTACK_DEFAULT_PERTHREAD SCRATCHSTACK_DEFAULT_SIZE_LARGE
static size_t sDefaultPerThreadScratchStackSize = SCRATCHSTACK_DEFAULT_PERTHREAD;

// Don't allow automatic increases over this size to avoid blowing out memory.  Initial size can still be larger than this if set explicitly.
#if defined(_M_X64)
#define SCRATCHSTACK_MAX_INCREASE_SIZE 1024*1024*256
#else
#define SCRATCHSTACK_MAX_INCREASE_SIZE 1024*1024*64
#endif

// Allocations this size or larger that end up on the heap will not be counted towards expansion.
#define MAX_ALLOCATION_FOR_EXPANSION 1024*1024

// struct ScratchAllocHeader flags
#define SCRATCHSTACK_FLAG_FROMHEAP (1 << 0)
#define SCRATCHSTACK_FLAG_DELETED (1 << 1)
#define SCRATCHSTACK_FLAG_LEAKED (1 << 2)		// This allocation has been reported as a leak, and should not be reported again.

typedef struct ScratchAllocHeader ScratchAllocHeader;

struct ScratchAllocHeader {
    U16 startSentinel;
    U16 flags;
	S32 line;
    const char *caller_fname;
	ScratchAllocHeader *prevDeleted; // not null if the one before this has already been deleted

	size_t allocSize;
    U32 heapArrayIndex;             // For heap allocations, the index of this allocation in the heapAllocs array.  Makes freeing much faster.
	U32 sentinel;					// should be HEADER_SENTINEL
};

#define HEADER_SIZE sizeof(ScratchAllocHeader)

// These values are used at the end of the header
#define HEADER_SENTINEL			    0xcafebabe
#define HEADER_FREED_SENTINEL	    0xdeadbabe

// These values are used at the start of the header
#define HEADER_START_SENTINAL       0xcabe
#define HEADER_START_FREED_SENTINAL 0xdebe

typedef struct ScratchStack
{
	char* mem;
	char* allocPtr;
	int fromHeapAllocPeak;          // Historical
	int fromHeapAllocCountTotal;    // Historical
	ScratchStackFlags flags;
	size_t total_size;
	size_t peak_size;               // Historical
	size_t largest_allocation;      // Historical
    size_t peakSizeSinceClear;      // The peak size used in the memory buffer since the last time it was empty.
    size_t peakOnHeapSinceClear;    // The amount allocated on the heap since the last clear.  Used to determine how big to resize the memory buffer.
    size_t startingSize;            // The original size of the memory buffer.
    size_t maxSize;                 // The maximum size that the memory buffer can be increased to.
    int numIncreases;               // The number of times that the memory buffer has been doubled in size from its starting size.
    const char *reason;             // Who created this
    void **heapAllocs;
}ScratchStack;


static CRITICAL_SECTION ScratchStackCriticalSection;

MP_DEFINE(ScratchStack);

static ThreadSafeMemoryPool tsmemPoolScratchStackMemSmall;
static ThreadSafeMemoryPool tsmemPoolScratchStackMemLarge;

void ScratchStackInitSystem(void)
{
	InitializeCriticalSection(&ScratchStackCriticalSection);
}

static void ScratchStack_Log(bool force, char* format, ...)
{
    char* logStr = NULL;
    // Can't actually log here because that can cause re-entrant calls into the scratch stack
    if ( force || gScratchStackVerboseDebug )
    {
        VA_START(ap, format);

        estrConcatfv(&logStr, format, ap);

        if ( force || gScratchStackVerboseDebug )
        {
            printf("%s\n", logStr);
        }

        VA_END();

        estrDestroy(&logStr);
    }
}

void ScratchStackInit(ScratchStack **ppstack, size_t total_size, ScratchStackFlags flags, const char *reason)
{
	assert(!*ppstack);
	EnterCriticalSection(&ScratchStackCriticalSection);
	MP_CREATE(ScratchStack, 6);
    *ppstack = MP_ALLOC(ScratchStack);
	(*ppstack)->allocPtr = (*ppstack)->mem = NULL; // allocate on demand
	(*ppstack)->total_size = total_size;
    (*ppstack)->startingSize = total_size;
    (*ppstack)->reason = reason ? strdup(reason) : NULL;
    (*ppstack)->flags = flags;
    (*ppstack)->maxSize = SCRATCHSTACK_MAX_INCREASE_SIZE;

    // If resize is not allowed, and the size is one of the defaults, then use memory pools instead of heap for the scratch stack buffer.
    if ( ( flags & SSF_DISALLOW_RESIZE ) )
    {
        if ( total_size == SCRATCHSTACK_DEFAULT_SIZE_SMALL )
        {
            (*ppstack)->flags |= SSF_USING_MEMPOOL;
            (*ppstack)->maxSize = total_size;
            threadSafeMemoryPoolInit(&tsmemPoolScratchStackMemSmall, 4, SCRATCHSTACK_DEFAULT_SIZE_SMALL, "ScratchStackSmall");
        }
        else if ( total_size == SCRATCHSTACK_DEFAULT_SIZE_LARGE )
        {
            (*ppstack)->flags |= SSF_USING_MEMPOOL;
            (*ppstack)->maxSize = total_size;
            threadSafeMemoryPoolInit(&tsmemPoolScratchStackMemLarge, 2, SCRATCHSTACK_DEFAULT_SIZE_LARGE, "ScratchStackLarge");
        }
    }


    LeaveCriticalSection(&ScratchStackCriticalSection);

    //This is way too spammy in gameservers and clients
    //ScratchStack_Log("ScratchStackInit: totalSize=%d, flags=%d, creator=%s", total_size, flags, reason);
}

typedef struct ScratchStackInfoData
{
	int count;
	bool verifyFailure;
    bool forcePrint;
} ScratchStackInfoData;

// Assert if a header is invalid.
static void ScratchStackVerifyHeader(ScratchAllocHeader *header)
{
	assertmsg(!header || ( header->sentinel == HEADER_SENTINEL && header->startSentinel == HEADER_START_SENTINAL ), "scratch stack allocation corrupt");
}

static void ScratchStackDumpAllocations(ScratchStack *stack, bool verifyFailure)
{
	bool bFoundDeleted = false;
	ScratchAllocHeader *header = (ScratchAllocHeader *)stack->mem;

	while (header &&
		(char*)header < stack->allocPtr &&
		header->allocSize)
	{
		ScratchStackVerifyHeader(header);
		if (!(header->flags & SCRATCHSTACK_FLAG_DELETED))
		{
			if (!isProductionMode())
				printf("  0x%08p : %s(%d) : %s\n", (header + 1), header->caller_fname, header->line, friendlyBytes(header->allocSize));
			if (verifyFailure && !(header->flags & SCRATCHSTACK_FLAG_LEAKED))
			{
				log_printf(LOG_BUG, "  0x%08p : %s(%d) : %s\n", (header + 1), header->caller_fname, header->line, friendlyBytes(header->allocSize));
				devassertmsguniquef(0, "Scratch stack leak: %s(%d)\n", header->caller_fname, header->line);
				header->flags |= SCRATCHSTACK_FLAG_LEAKED;
			}
			if (header->allocSize >= 1024 && memcmp((char*)(header + 1), ESTR_HEADER, 4)==0) {
				if (!verifyFailure)
				{
					log_printf(LOG_BUG, "    \"%s\"\n", (char*)(header+1) + EStrHeaderSize);
					printf("    \"%s\"\n", (char*)(header+1) + EStrHeaderSize);
				}
			}
		}
		else
		{
			bFoundDeleted = true;
		}
		header = (ScratchAllocHeader*)(((char*)header) + header->allocSize);
	}
	if (bFoundDeleted) {
		if (!isProductionMode())
			printf("  Also some allocations deleted out of order (not in this list)!\n");
		log_printf(LOG_BUG, "  Also some allocations deleted out of order (not in this list)!\n");
	}
}

static int ScratchStackDumpStatsInternal(ScratchStack *data, ScratchStackInfoData *userData)
{
	userData->count++;
    ScratchStack_Log( userData->forcePrint, "ScratchStack(%s) at 0x%08p : size %d%s (starting %d), increased %d times, allocated %d (peak %d), heap %d (peak %d, total %d), peak bytes on heap since clear %d, largest alloc %d\n",
		data->reason, data, data->total_size, data->mem?"":", no mem",
        data->startingSize, data->numIncreases,
		data->allocPtr - data->mem, data->peak_size,
		eaSize(&data->heapAllocs), data->fromHeapAllocPeak, data->fromHeapAllocCountTotal, data->peakOnHeapSinceClear,
		data->largest_allocation);

	ScratchStackDumpAllocations(data, userData->verifyFailure);
	return 1;
}

static int ScratchStackDumpStatsCallback(MemoryPool pool, ScratchStack *data, ScratchStackInfoData *userData)
{
	return ScratchStackDumpStatsInternal(data, userData);
}

AUTO_COMMAND;
void ScratchStackDumpStats(void)
{
	ScratchStackInfoData data = {0};
    data.forcePrint = true;
	EnterCriticalSection(&ScratchStackCriticalSection);
	mpForEachAllocation(MP_NAME(ScratchStack), ScratchStackDumpStatsCallback, &data);
	LeaveCriticalSection(&ScratchStackCriticalSection);
	printf("%d total ScratchStacks\n", data.count);
}

// Check an empty ScratchStack to see if it had previously overflowed into the heap, and resize it if allowed.
void ScratchStackCheckResize(ScratchStack *stack)
{
    static U32 timeSinceLastReport = 0;

    devassertmsg((stack->allocPtr == stack->mem) && (eaSize(&stack->heapAllocs) == 0), "Scratch stack is not empty when checking resize.");
    if ( gScratchStackDisableResize || ( stack->flags & SSF_DISALLOW_RESIZE ) )
    {
        return;
    }
    devassertmsg(!(stack->flags & SSF_USING_MEMPOOL), "Scratch stack is using mempools but resize is enabled");

    if ( gScratchStackVerboseDebug )
    {
        U32 curTime = timeSecondsSince2000();
        if ( curTime > (timeSinceLastReport + 10) )
        {
            timeSinceLastReport = curTime;
            ScratchStack_Log(false, "ScratchStack(%s): peak since last report: %d\n", stack->reason, stack->peakSizeSinceClear);
            stack->peakSizeSinceClear = 0;
        }
    }

    if ( stack->peakOnHeapSinceClear > 0 )
    {
        size_t targetSize = stack->total_size + stack->peakOnHeapSinceClear;
        size_t totalSize = stack->total_size;
        if ( ( ( totalSize << 1 ) <= stack->maxSize ) && ( targetSize > totalSize ) )
        {
            stack->numIncreases++;
            totalSize = totalSize << 1;
        }

        if ( totalSize > stack->total_size ) 
        {
            if ( gScratchStackPrintResizes )
            {
                ScratchStack_Log(true, "ScratchStack(%s): increasing size from %d to %d\n", stack->reason, stack->total_size, totalSize);
            }
            if ( stack->mem )
            {
                free(stack->mem);
                stack->mem = malloc(totalSize);
                stack->allocPtr = stack->mem;
            }
            stack->total_size = totalSize;
        }

        stack->peakOnHeapSinceClear = 0;
    }
}

void* ScratchStackAllocEx(ScratchStack **ppstack, size_t size, bool clear_mem, bool overflow_to_heap, const char *caller_fname, int line)
{
	ScratchStack *stack;
	ScratchAllocHeader *header;

	if(!*ppstack)
	{
		ScratchStackInit(ppstack, SCRATCHSTACK_DEFAULT_SIZE_SMALL, SCRATCHSTACK_DEFAULT_FLAGS, STACK_SPRINTF("Auto: %p", ppstack));
	}

	stack = *ppstack;

	size += HEADER_SIZE;
	size = (size + 7) & ~7; // Round up to 8 bytes

	MAX1(stack->largest_allocation, size);

	if (size <= (size_t)(stack->total_size - (stack->allocPtr - stack->mem))) {

        // Allocating from our memory buffer.
		if (!stack->mem) {
            if ( stack->flags & SSF_USING_MEMPOOL )
            {
                if (stack->total_size == SCRATCHSTACK_DEFAULT_SIZE_SMALL)
                {
                    stack->allocPtr = stack->mem = threadSafeMemoryPoolAlloc(&tsmemPoolScratchStackMemSmall);
                }
                else if (stack->total_size == SCRATCHSTACK_DEFAULT_SIZE_LARGE)
                {
                    stack->allocPtr = stack->mem = threadSafeMemoryPoolAlloc(&tsmemPoolScratchStackMemLarge);
                }
                else
                {
                    assert(0);
                }
            }
            else
            {
                stack->allocPtr = stack->mem = malloc(stack->total_size);
            }
		}
		header = (ScratchAllocHeader *)(stack->allocPtr);
		if (clear_mem)
			memset(header, 0, size);
		else
			memset(header, 0, HEADER_SIZE);

		header->sentinel = HEADER_SENTINEL;
        header->startSentinel = HEADER_START_SENTINAL;
		stack->allocPtr += size;
		MAX1(stack->peak_size, (size_t)(stack->allocPtr - stack->mem));
        MAX1(stack->peakSizeSinceClear, (size_t)(stack->allocPtr - stack->mem));
	} else {

        // Our memory buffer is full, so allocate from the heap.
		// Not tracking to the caller here because it creates inconsistent errors in memory budgetting code (if there is no budget for the caller assigned)

		// Don't count large single allocations towards the size used to determine buffer expansion
		if ( size < MAX_ALLOCATION_FOR_EXPANSION )
		{
			stack->peakOnHeapSinceClear += size;
		}
		else
		{
			ScratchStack_Log(false, "ScratchStack(%s): got large heap allocation for %d bytes", stack->reason, size);
		}

		// Only fall back to heap if the caller requests it.
		if (!overflow_to_heap)
			return NULL;

		header = calloc(size, 1);
		header->sentinel = HEADER_SENTINEL;
        header->startSentinel = HEADER_START_SENTINAL;
		header->flags |= SCRATCHSTACK_FLAG_FROMHEAP;
        header->heapArrayIndex = eaSize(&stack->heapAllocs);
		eaPush(&stack->heapAllocs, header);
		stack->fromHeapAllocCountTotal++;

		MAX1(stack->fromHeapAllocPeak, eaSize(&stack->heapAllocs));
	}
	header->allocSize = size;
	header->caller_fname = caller_fname;
	header->line = line;
	return header + 1;
}

static void ScratchStackFreeMem(ScratchStack *stack)
{
	if(stack->mem) {
        if ( stack->flags & SSF_USING_MEMPOOL )
        {
            if (stack->total_size == SCRATCHSTACK_DEFAULT_SIZE_SMALL)
            {
                threadSafeMemoryPoolFree(&tsmemPoolScratchStackMemSmall, stack->mem);
            }
            else if (stack->total_size == SCRATCHSTACK_DEFAULT_SIZE_LARGE)
            {
                threadSafeMemoryPoolFree(&tsmemPoolScratchStackMemLarge, stack->mem);
            }
            else
            {
                assert(0);
            }

        }
        else
        {
		    free(stack->mem);
        }
		stack->allocPtr = stack->mem = NULL;
	}
}

void ScratchStackFreeAll(ScratchStack** ppstack)
{
	if(*ppstack)
	{
		ScratchStackFreeMem(*ppstack);
		eaDestroyEx(&(*ppstack)->heapAllocs, NULL);
        if ( (*ppstack)->reason )
        {
            free((void *)((*ppstack)->reason));
        }
		EnterCriticalSection(&ScratchStackCriticalSection);
		MP_FREE(ScratchStack, (*ppstack));
		LeaveCriticalSection(&ScratchStackCriticalSection);
		*ppstack = NULL;
	}
}

bool ScratchStackReAllocInPlaceIfPossibleEx(ScratchStack** ppstack, void *pdata, size_t iNewSize)
{
	ScratchAllocHeader *header = ((ScratchAllocHeader *)pdata) - 1;
	ScratchStack *stack = *ppstack;

	//can't possibly realloc legally without the stack having been created in the first place
	assert(stack);

	ScratchStackVerifyHeader(header);

	//if this is a heap allocation, can't do it
	if (header->flags & SCRATCHSTACK_FLAG_FROMHEAP) 
	{
		return false;
	}

	assert(header->allocSize <= (size_t)(stack->allocPtr - stack->mem));

	//if this is not the last allocation in the scratch stack, can't do it
	if ((stack->allocPtr - header->allocSize) != (char*)header)
	{
		return false;
	}

	iNewSize += HEADER_SIZE;
	iNewSize = (iNewSize + 7) & ~7; // Round up to 8 bytes


	//won't fit in scratch stack
	if ((char*)(header) + iNewSize > stack->mem + stack->total_size)
	{
		return false;
	}

	//now we are confident we can do it
	MAX1(stack->largest_allocation, iNewSize);
	stack->allocPtr = (char*)header + iNewSize;
	header->allocSize = iNewSize;

	MAX1(stack->peak_size, (size_t)(stack->allocPtr - stack->mem));
	MAX1(stack->peakSizeSinceClear, (size_t)(stack->allocPtr - stack->mem));

	return true;
}



void ScratchStackFree(ScratchStack** ppstack, void *data)
{
	ScratchAllocHeader *header = ((ScratchAllocHeader *)data) - 1;
	ScratchStack *stack = *ppstack;
	while(header)
	{
		ScratchStackVerifyHeader(header);
		if (header->flags & SCRATCHSTACK_FLAG_FROMHEAP) {
            // Freeing a heap allocation.
            ScratchAllocHeader *swapHeader;
			int index = header->heapArrayIndex;

            // Validate that the index in the header being freed is valid.
            assertmsg(index < eaSize(&stack->heapAllocs), "When freeing from ScratchStack, found header with invalid heapArrayIndex.");
			assertmsg(stack->heapAllocs[index] == header, "When freeing from ScratchStack, heapArrayIndex does not refer to item being freed.");

            // Remove the last entry from the array.
            swapHeader = eaPop(&stack->heapAllocs);

            // If the last entry is not the one we are freeing, then write it over the one we are freeing and update its index.
            if ( index < eaSize(&stack->heapAllocs) )
            {
                stack->heapAllocs[index] = swapHeader;
                swapHeader->heapArrayIndex = index;
            }

			header->sentinel = HEADER_FREED_SENTINEL;
            header->startSentinel = HEADER_START_FREED_SENTINAL;
			free(header);
			header = NULL;
		} else {
			assert(header->allocSize <= (size_t)(stack->allocPtr - stack->mem));

			if ((stack->allocPtr - header->allocSize) != (char*)header)
			{
                // The block being freed is not the last one in the memory buffer, so just mark it as freed and link it to the next header.
				ScratchAllocHeader *nextHeader = (ScratchAllocHeader *)((char *)header + header->allocSize);

                // The block being freed is at or before the end of the memory buffer.
				assert((stack->allocPtr - header->allocSize) >= (char*)header);

                // The block being freed is at or after the start of the memory buffer.
                assert(stack->mem <= (char *)header);

				// This will get cleaned up later
				assert(!(header->flags & SCRATCHSTACK_FLAG_DELETED));
				header->flags |= SCRATCHSTACK_FLAG_DELETED;
				nextHeader->prevDeleted = header;
				header = NULL;
			}
			else
			{
                // We are freeing the last allocation in the memory buffer, so we can roll back the next allocation buffer.
                // In this case we will keep looping if there are previously freed headers immediately before this one, so that
                //  they will all be rolled back.
				ScratchAllocHeader *prevHeader = header->prevDeleted;
				stack->allocPtr -= header->allocSize;
				if (prevHeader)
				{
					assert(prevHeader->flags & SCRATCHSTACK_FLAG_DELETED);
				}
				header = prevHeader;
			}
		}
	}

    // If the ScratchStack is empty, check and see if it should be resized or freed
    if ( (stack->allocPtr == stack->mem) && (eaSize(&stack->heapAllocs) == 0) )
    {
        if ( stack->flags & SSF_USING_MEMPOOL )
        {
            // If using pooled memory, and the stack is empty, return the memory to the pool.
            ScratchStackFreeMem(*ppstack);
        }
        else
        {
            // If using heap memory, then check for resize.
            ScratchStackCheckResize(stack);
        }
    }
}

size_t ScratchSize(void *data) // Gets the size of a ScratchAlloc
{
	ScratchAllocHeader *header = ((ScratchAllocHeader *)data) - 1;
	ScratchStackVerifyHeader(header);
	return header->allocSize - HEADER_SIZE;
}

typedef struct ScratchDataPerThread {
	ScratchStack *stack;
} ScratchDataPerThread;
static bool any_scratch_stack_allocated=false;

// Get base per-thread data container, and optionally, the TLS slot index.
static ScratchDataPerThread *getThreadBaseData(int *slot) {
	ScratchDataPerThread *ret;
	STATIC_THREAD_ALLOC(ret);
	if (slot)
		*slot = tls_ret_index;
	return ret;
}

// Free scratch stack data when a thread dies.
static void ScratchStackAtThreadExitCallback(void *userdata, int thread_id, void *tls_value)
{
	ScratchDataPerThread *thread_data = tls_value;
	if (thread_data)
	{
		ScratchVerifyNoOutstanding();
		ScratchStackFreeAll(&thread_data->stack);
		free(thread_data);
	}
}

// Get the per-thread data; allocate if it if it doesn't exist.
static ScratchDataPerThread *getThreadData(size_t overrideSize) {
	int slot;
	ScratchDataPerThread *ret = getThreadBaseData(&slot);
	if (ret->stack == NULL) {
        int threadID = GetCurrentThreadId();
		ATOMIC_INIT_BEGIN;
		AtThreadExitTls(ScratchStackAtThreadExitCallback, NULL, slot);
		ATOMIC_INIT_END;
		any_scratch_stack_allocated = true;
		ScratchStackInit(&ret->stack, overrideSize? overrideSize : sDefaultPerThreadScratchStackSize, SSF_DEFAULT, STACK_SPRINTF("Thread %d", threadID));
		//printf("Allocated ScratchStack for thread %d at 0x%08X\n", GetCurrentThreadId(), ret->stack);
	}
	return ret;
}

void *ScratchStackPerThreadAllocEx(size_t size, bool clear_mem, bool overflow_to_heap, const char *caller_fname, int line)
{
	ScratchDataPerThread *thread_data = getThreadData(0);
	return ScratchStackAllocEx(&thread_data->stack, size, clear_mem, overflow_to_heap, caller_fname, line);
}

void ScratchStackSetThreadSize(size_t size)
{
	ScratchDataPerThread *thread_data = getThreadData(size);
}

void ScratchStackSetDefaultPerThreadSize(size_t size)
{
    sDefaultPerThreadScratchStackSize = size;
}

void ScratchFree(void *data)
{
	if (data)
	{
		ScratchDataPerThread *thread_data = getThreadData(0);
		ScratchStackFree(&thread_data->stack, data);
	}
}

size_t ScratchPerThreadOutstandingAllocSize(void)
{
	if (any_scratch_stack_allocated)
	{
		ScratchDataPerThread *thread_data = getThreadData(0);
		return thread_data->stack->allocPtr - thread_data->stack->mem + eaSize(&thread_data->stack->heapAllocs) * 16;
	} else
		return 0;
}

void ScratchStackOncePerFrame(void)
{
    threadSafeMemoryPoolCompact(&tsmemPoolScratchStackMemSmall);
    threadSafeMemoryPoolCompact(&tsmemPoolScratchStackMemLarge);
}

// assert() if there are outstanding scratch stack allocations.
void ScratchVerifyNoOutstanding()
{
	PERFINFO_AUTO_START_FUNC();
	if (ScratchPerThreadOutstandingAllocSize() != 0)
	{
		ScratchDataPerThread *thread_data = getThreadData(0);
		ScratchStackInfoData data = {0};
		data.verifyFailure = true;
		if(thread_data && thread_data->stack)
			ScratchStackDumpStatsInternal(thread_data->stack, &data);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

bool ScratchPerThreadReAllocInPlaceIfPossible(void *pdata, size_t iNewSize)
{
	ScratchDataPerThread *thread_data = getThreadData(0);
	return ScratchStackReAllocInPlaceIfPossibleEx(&thread_data->stack, pdata, iNewSize);
}

void ScratchFreeThisThreadsStack(void)
{
	ScratchDataPerThread *thread_data = getThreadBaseData(NULL);
	ScratchVerifyNoOutstanding();
	ScratchStackFreeAll(&thread_data->stack);
}

