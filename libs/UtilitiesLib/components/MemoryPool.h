/* File MemoryPool.h
 *	A memory pool manages a large array of structures of a fixed size.  
 *	The main advantage of are
 *		- Low memory management overhead
 *			It manages large pieces of memory without the need for additional memory for
 *			management purposes, unlike malloc/free.
 *		- Fast at serving mpAlloc/mpFree requests.
 *			Very few operations are needed before it can complete both operations.
 *		- Suitable for managing both statically and dynamically allocated arrays.
 *
 *
 */

#ifndef MEMORYPOOL_H
#define MEMORYPOOL_H
#pragma once
GCC_SYSTEM

C_DECLARATIONS_BEGIN

#include "tokenstore.h"
#include "MemoryBudget.h"

// MS: Some hideous macro code to make memory pools easier.
// Usage: To create and use a memory pool for a type called FunType, use this code (sorry for lame #if 0 thing):

#if 0
	// Create the global memory pool like this.  This create a variable called memPoolFunType.
	// If you ever want to destroy the memory pool, use MP_DESTROY(FunType).

	MP_DEFINE(FunType);

	FunType* createFunType(){
		FunType* funType;
		
		// Initialize the memory pool (only does anything the first time it's called).
		// 1000 is how many elements are in a MemoryPool chunk.
		
		MP_CREATE(FunType, 1000); 
		
		// Allocate a new instance from the memory pool.
		
		funType = MP_ALLOC(FunType);
		
		// Initialize funType if you want, then return it.
		
		return funType;
	}

	void destroyFunType(FunType* funType){
		// Give the memory back to the memory pool.
	
		MP_FREE(FunType, funType);
	}
#endif

// The hideous macros.

#define MP_NAME(type) memPool##type

#define MP_CREATE(type, amount)														\
	if(!MP_NAME(type)){																\
		memBudgetAddStructMappingIfNotMapped(#type, __FILE__);						\
		MP_NAME(type) = createMemoryPoolNamed(#type, #type, LINENUM_FOR_POOLED_STRUCTS); \
		initMemoryPool(MP_NAME(type), sizeof(type), amount);						\
		mpSetMode(MP_NAME(type), ZeroMemoryBit);									\
	}

#define MP_CREATE_COMPACT(type, amount, freq, limit)								\
	if(!MP_NAME(type)){																\
		memBudgetAddStructMappingIfNotMapped(#type, __FILE__);						\
		MP_NAME(type) = createMemoryPoolNamed(#type, #type, LINENUM_FOR_POOLED_STRUCTS); \
		initMemoryPool(MP_NAME(type), sizeof(type), amount);						\
		mpSetMode(MP_NAME(type), ZeroMemoryBit);									\
		mpSetCompactParams(MP_NAME(type), freq, limit);								\
	}

#define MP_CREATE_MEMBER(parent, type, amount)										\
	if(!parent->MP_NAME(type)){														\
		memBudgetAddStructMappingIfNotMapped(#type, __FILE__);						\
		parent->MP_NAME(type) = createMemoryPoolNamed(#type, #type, LINENUM_FOR_POOLED_STRUCTS); \
		initMemoryPool(parent->MP_NAME(type), sizeof(type), amount);				\
		mpSetMode(parent->MP_NAME(type), ZeroMemoryBit);							\
	}

#define MP_CREATE_MEMBER_SDBG(parent, type, amount)									\
	if(!parent->MP_NAME(type)){														\
		memBudgetAddStructMappingIfNotMapped(#type, __FILE__);						\
		parent->MP_NAME(type) = createMemoryPoolNamed(#type, #type, LINENUM_FOR_POOLED_STRUCTS); \
		initMemoryPool(parent->MP_NAME(type), sizeof(type), amount);				\
		mpSetMode(parent->MP_NAME(type), ZeroMemoryBit);							\
	}

#define MP_CREATE_DBG(type, amount, file, line)										\
	if(!MP_NAME(type)){																\
		MP_NAME(type) = createMemoryPoolNamed(#type, file, line);					\
		initMemoryPool(MP_NAME(type), sizeof(type), amount);						\
		mpSetMode(MP_NAME(type), ZeroMemoryBit);									\
	}


#define MP_CREATE_EX(type, amount, size)											\
	if(!MP_NAME(type)){																\
		memBudgetAddStructMappingIfNotMapped(#type, __FILE__);						\
		MP_NAME(type) = createMemoryPoolNamed(#type, #type, LINENUM_FOR_POOLED_STRUCTS); \
		initMemoryPool(MP_NAME(type), size, amount);								\
		mpSetMode(MP_NAME(type), ZeroMemoryBit);									\
	}

#define MP_CREATE_MEMBER_EX(parent, type, amount, size)								\
	if(!parent->MP_NAME(type)){														\
		memBudgetAddStructMappingIfNotMapped(#type, __FILE__);						\
		parent->MP_NAME(type) = createMemoryPoolNamed(#type, #type, LINENUM_FOR_POOLED_STRUCTS); \
		initMemoryPool(parent->MP_NAME(type), size, amount);						\
		mpSetMode(parent->MP_NAME(type), ZeroMemoryBit);							\
	}

#define MP_CREATE_MEMBER_EX_SDBG(parent, type, amount, size)						\
	if(!parent->MP_NAME(type)){														\
		memBudgetAddStructMappingIfNotMapped(#type, __FILE__);						\
		parent->MP_NAME(type) = createMemoryPoolNamed(#type, #type, LINENUM_FOR_POOLED_STRUCTS); \
		initMemoryPool(parent->MP_NAME(type), size, amount);						\
		mpSetMode(parent->MP_NAME(type), ZeroMemoryBit);							\
	}

#define MP_CREATE_ALIGNED(type, amount, alignment)									\
	if(!MP_NAME(type)){																\
		memBudgetAddStructMappingIfNotMapped(#type, __FILE__);						\
		MP_NAME(type) = createMemoryPoolNamed(#type, #type, LINENUM_FOR_POOLED_STRUCTS); \
		mpSetChunkAlignment(MP_NAME(type), alignment);								\
		initMemoryPool(MP_NAME(type), sizeof(type), amount);						\
		mpSetMode(MP_NAME(type), ZeroMemoryBit);									\
	}


#define MP_DESTROY(type)															\
	if(MP_NAME(type)){																\
		destroyMemoryPool(MP_NAME(type));											\
		MP_NAME(type) = 0;															\
	}

#define MP_DESTROY_MEMBER(parent, type)												\
	if(parent->MP_NAME(type)){														\
		destroyMemoryPool(parent->MP_NAME(type));									\
		parent->MP_NAME(type) = 0;													\
	}

#define MP_ALLOC(type) ((type*)(MP_NAME(type) ? mpAlloc(MP_NAME(type)) : (void*)(assertmsg(MP_NAME(type), "Memory pool is not initialized."),0)))
#define MP_ALLOC_MEMBER(parent, type) ((type*)(parent->MP_NAME(type) ? mpAlloc(parent->MP_NAME(type)) : (assertmsg(parent->MP_NAME(type), "Memory pool is not initialized."),0)))
#define MP_ALLOC_MEMBER_NOPTRCAST(parent, type) ((type)(parent->MP_NAME(type) ? mpAlloc(parent->MP_NAME(type)) : (assertmsg(parent->MP_NAME(type), "Memory pool is not initialized."),0)))
#define MP_ALLOC_DBG(type, file, line) ((type*)(MP_NAME(type) ? mpAlloc_dbg(MP_NAME(type), 1, file, line) : (assertmsg(MP_NAME(type), "Memory pool is not initialized."),0)))
#define MP_ALLOC_MEMBER_SDBG(parent, type) ((type*)(parent->MP_NAME(type) ? mpAlloc_dbg(parent->MP_NAME(type), 1 MEM_DBG_PARMS_CALL) : (assertmsg(parent->MP_NAME(type), "Memory pool is not initialized."),0)))

#define MP_FREE(type, mem) if(mem){assertmsg(MP_NAME(type), "Memory pool is not initialized.");mpFree(MP_NAME(type), mem); (mem)=0;}
#define MP_FREE_NOCLEAR(type, mem) if(mem){assertmsg(MP_NAME(type), "Memory pool is not initialized.");mpFree(MP_NAME(type), mem);}
#define MP_FREE_MEMBER(parent, type, mem) if(mem){assertmsg(parent->MP_NAME(type), "Memory pool is not initialized.");mpFree(parent->MP_NAME(type), mem); (mem)=0;}

#define MP_DEFINE(type) static MemoryPool MP_NAME(type) = 0
#define MP_DEFINE_MEMBER(type) MemoryPool MP_NAME(type)

// The functions and stuff for MemoryPool.

typedef void (*Destructor)(void*);

// MemoryPool internals are hidden.  Accidental changes are *bad* for memory management.
typedef struct MemoryPoolImp *MemoryPool;


#define ZERO_MEMORY_BIT				1

typedef enum{
	TurnOffAllFeatures =		0,
	Default =					ZERO_MEMORY_BIT,
	ZeroMemoryBit =				ZERO_MEMORY_BIT,
} MemoryPoolMode;

uintptr_t mpGetSentinelValue(void);

// MemoryPool mode query/alteration
MemoryPoolMode mpGetMode(MemoryPool pool);
int mpSetMode(MemoryPool pool, MemoryPoolMode mode);

void mpGetCompactParams(MemoryPool pool, int *frequency, float *limit);
void mpSetCompactParams(MemoryPool pool, int frequency, float limit); // set to 0 to disable automatic compaction

void mpSetChunkAlignment(MemoryPool pool, int chunkAlignment);

typedef void (*MemoryPoolForEachFunc)(MemoryPool pool, void *userData);

/************************************************************************
 * Normal Memory Pool
 */

// constructor/destructors
#define createMemoryPool() createMemoryPool_dbg(MEM_DBG_PARMS_INIT_VOID)
MemoryPool createMemoryPool_dbg(MEM_DBG_PARMS_VOID);
MemoryPool createMemoryPoolNamed(const char* name MEM_DBG_PARMS);
void initMemoryPool_dbg(MemoryPool pool, int structSize, int structCount MEM_DBG_PARMS);

void initMemoryPoolOffset_dbg(MemoryPool pool, int structSize, int structCount, int offset MEM_DBG_PARMS);

MemoryPool initMemoryPoolLazy_dbg(MemoryPool pool, int structSize, int structCount MEM_DBG_PARMS);

void destroyMemoryPool(MemoryPool pool);

// Allocate a piece of memory.
void* mpAlloc_dbg(MemoryPool pool, int forceMyCallerName MEM_DBG_PARMS);

// Retains all allocated memory.
int mpFree_dbg(MemoryPool pool, void *memory MEM_DBG_PARMS);

// Frees all allocated memory.
void mpFreeAll(MemoryPool pool);
void mpFreeAllocatedMemory(MemoryPool pool);

int mpVerifyFreelist(MemoryPool pool);

typedef void (*MemoryPoolForEachAllocationFunc)(MemoryPool pool, void *data, void *userData);
void mpForEachAllocation(MemoryPool pool, MemoryPoolForEachAllocationFunc func, void *userData);

// Get the structure size of this memory pool.
size_t mpStructSize(MemoryPool pool);

// Get the allocated struct count.
size_t mpGetAllocatedCount(MemoryPool pool);

// Get the amount of memory allocated for chunks.
size_t mpGetAllocatedChunkMemory(MemoryPool pool);

// Check if a piece of memory has been returned into a memory pool.
int mpReclaimed(void* memory);

typedef enum MemoryPoolAllocState
{
	MPAS_UNOWNED,
	MPAS_ALLOCATED,
	MPAS_FREE
} MemoryPoolAllocState;

MemoryPoolAllocState mpGetMemoryState(MemoryPool pool, const void *mem);

const char* mpGetName(MemoryPool pool);
const char* mpGetFileName(MemoryPool pool);
int	mpGetFileLine(MemoryPool pool);

void mpForEachMemoryPool(MemoryPoolForEachFunc callbackFunc, void *userData);

// Compact all pools in need of compaction since last call to this function
// If never called, pools get compacted on free, so call this once per frame
// to enable delayed compaction (faster)
void mpCompactPools(void);
void mpCompactPool(MemoryPool pool);

// enable or disable pool compaction
void mpEnablePoolCompaction(bool enabled);

void testMemoryPool(void);

void mpEnableSuperFreeDebugging(void);

int mpVerifyAllFreelists(void);

/*
 * Normal Memory Pool
 ************************************************************************/


#if defined(INCLUDE_MEMCHECK) || defined(_CRTDBG_MAP_ALLOC)
#ifndef TRACKED_MEMPOOL
#define TRACKED_MEMPOOL
#endif
/************************************************************************
 * Tracked Memory Pool
 *	Allocation request origin is tracked properly.
 */

#define initMemoryPool(pool,structSize,structCount) initMemoryPool_dbg(pool,structSize,structCount MEM_DBG_PARMS_INIT);
#define initMemoryPoolOffset(pool,structSize,structCount,offset) initMemoryPoolOffset_dbg(pool,structSize,structCount,offset MEM_DBG_PARMS_INIT);
#define initMemoryPoolLazy(pool,structSize,structCount) initMemoryPoolLazy_dbg(pool,structSize,structCount MEM_DBG_PARMS_INIT);
#define mpAlloc(pool) mpAlloc_dbg(pool, 0 MEM_DBG_PARMS_INIT)
#define mpFree(pool, mem) mpFree_dbg(pool, mem MEM_DBG_PARMS_INIT)

/*
 * Tracked Memory Pool
 ************************************************************************/
#endif

C_DECLARATIONS_END

#endif