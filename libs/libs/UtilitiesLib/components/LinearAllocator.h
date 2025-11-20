#ifndef _LINEARALLOCATOR_H_
#define _LINEARALLOCATOR_H_
#pragma once
GCC_SYSTEM

/////////////////////////////////////////////////////////
// A linear allocator allocates arbitrary sized chunks 
// of memory out of fixed sized blocks.
// Frees of individual allocations is not allowed, 
// all of the memory must be freed at the same time.
/////////////////////////////////////////////////////////

#ifdef _FULLDEBUG
// #define LINALLOC_EXTRA_CHECKING
#endif

typedef struct LinearAllocator LinearAllocator;


LinearAllocator *linAllocCreateDbg(int block_size, int max_size, bool zeromem MEM_DBG_PARMS);
#define linAllocCreateEx(block_size, max_size, zeromem) linAllocCreateDbg(block_size, max_size, zeromem MEM_DBG_PARMS_INIT)
#define linAllocCreate(block_size, zeromem) linAllocCreateEx(block_size, 0, zeromem)

// reset allocator to the beginning, keeping the blocks allocated
void linAllocClear(SA_PARAM_OP_VALID LinearAllocator *la);

// set max memory size, freeing blocks if over the limit
void linAllocSetMemoryLimit(SA_PARAM_OP_VALID LinearAllocator *la, int max_size);

// free all blocks and the allocator struct
void linAllocDestroy(SA_PRE_OP_VALID SA_POST_P_FREE LinearAllocator *la);

// allocate a chunk of memory from the allocator
void *linAllocExInternal(SA_PARAM_NN_VALID LinearAllocator *la, int bytes, bool allow_failure, bool bAlign16 FD_MEM_DBG_PARMS);
#define linAllocEx(la, bytes, allow_failure) linAllocExInternal(la, bytes, allow_failure, false FD_MEM_DBG_PARMS_INIT)
#define linAlloc(la, bytes) linAllocExInternal(la, bytes, false, false FD_MEM_DBG_PARMS_INIT)
#define linAlloc16(la, bytes) linAllocExInternal(la, bytes, false, true FD_MEM_DBG_PARMS_INIT)

void linAllocCheck(LinearAllocator *la);

#ifdef LINALLOC_EXTRA_CHECKING
void linAllocDoneDebug(SA_PARAM_NN_VALID LinearAllocator *la);
// Call this when you're done using the memory allocated (for tracking buffer overruns)
#define linAllocDone(la) linAllocDoneDebug(la)
#else
#define linAllocDone(la)
#endif

#endif //_LINEARALLOCATOR_H_


