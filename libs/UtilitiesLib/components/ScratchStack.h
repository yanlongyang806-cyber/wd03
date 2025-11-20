#ifndef SCRATCHSTACK_H
#define SCRATCHSTACK_H
GCC_SYSTEM

C_DECLARATIONS_BEGIN

typedef struct ScratchStack ScratchStack;

typedef enum ScratchStackFlags {
	SSF_DEFAULT=0,
    SSF_DISALLOW_RESIZE=1,
    SSF_USING_MEMPOOL=2,
} ScratchStackFlags;

// These will be used if a ScratchStack is auto-created for you
#define SCRATCHSTACK_DEFAULT_SIZE_SMALL 64 * 1024
#define SCRATCHSTACK_DEFAULT_SIZE_LARGE 256 * 1024
#define SCRATCHSTACK_DEFAULT_FLAGS SSF_DISALLOW_RESIZE

void ScratchStackInit(SA_PRE_NN_OP_VALID ScratchStack **ppstack, size_t starting_size, ScratchStackFlags flags, const char *reason);

SA_RET_OP_BYTES_VAR(size) void* ScratchStackAllocEx(SA_PRE_NN_NN_VALID ScratchStack** ppstack, size_t size, bool clear_mem, bool overflow_to_heap, SA_PARAM_NN_STR const char *caller_fname, int line);
#define ScratchStackAlloc(ppstack, size) ScratchStackAllocEx(ppstack, size, true, true, __FILE__, __LINE__)
void ScratchStackFreeAll(SA_PRE_NN_NN_VALID ScratchStack** ppstack);
void ScratchStackFree(SA_PRE_NN_NN_VALID ScratchStack** ppstack, SA_PRE_NN_VALID SA_POST_P_FREE void *data);

SA_RET_NN_BYTES_VAR(size) void *ScratchStackPerThreadAllocEx(size_t size, bool clear_mem, bool overflow_to_heap, SA_PARAM_NN_STR const char *caller_fname, int line);

//////////////////////////////////////////////////////////////////////////
// These are the functions you should actually use.
// Scratch stack allocated memory is initialized to 0.
#define ScratchAlloc(size) ScratchStackPerThreadAllocEx(size, true, true, __FILE__, __LINE__)
#define ScratchAllocUninitialized(size) ScratchStackPerThreadAllocEx(size, false, true, __FILE__, __LINE__)
#define ScratchAllocNoOverflow(size) ScratchStackPerThreadAllocEx(size, true, false, __FILE__, __LINE__)
#define ScratchAllocNoOverflowUninitialized(size) ScratchStackPerThreadAllocEx(size, false, false, __FILE__, __LINE__)
void ScratchFree(SA_PRE_OP_VALID SA_POST_P_FREE void *data);
size_t ScratchSize(SA_PARAM_NN_VALID void *data);

// Lets you set the size of the per-thread scratch stack for the current thread.  Must be called before the scratch stack is used the first time to be effective.
void ScratchStackSetThreadSize(size_t size);

// Set the default scratch stack size for any future per-thread scratch stacks that are created for this process.
void ScratchStackSetDefaultPerThreadSize(size_t size);

size_t ScratchPerThreadOutstandingAllocSize(void); // size not accurate (but deterministic) if there are heap allocations

//not done automatically because it creates a critical section and thus is not
//thread-safe
//
//Not done in AUTO_RUN because it's needed by other AUTO_RUNs.
void ScratchStackInitSystem(void);

void ScratchStackDumpStats( void );

void ScratchStackOncePerFrame(void);

// Assert if there are outstanding scratch stack allocations.
// You can see what's on the ScratchStack by executing
//   ScratchStackDumpStats()
// in the immediate window of the debugger.
// If you fix the issue and want to continue, you can clear the ScratchStack by executing
//   ScratchStackFreeAll(&getThreadData()->stack)
void ScratchVerifyNoOutstanding(void);

//try to resize an allocation, will work if it is currently the most recent allocation, will fail otherwise. Returns
//true if it worked, false otherwise.
bool ScratchPerThreadReAllocInPlaceIfPossible(void *pdata, size_t iNewSize);

//frees thie current thread's scratch stack... should be called before any thread that uses
//scratch stack exits to avoid leaking
void ScratchFreeThisThreadsStack(void);

C_DECLARATIONS_END


#endif