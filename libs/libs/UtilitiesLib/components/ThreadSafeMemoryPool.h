#pragma once
GCC_SYSTEM

// Thread-Safe MemoryPool
// Does *not* zero memory
// Lockless other than while growing (which is required, since memory allocations
//   are not lockless)

#include "windefinclude.h"
#include "TextParserEnums.h"
#include "MemoryBudget.h"

AUTO_ENUM;
typedef enum TSMPFlags
{
	TSMPFLAG_NO_INTERNAL_EARRAYS = 1 << 0, //if true, then don't use the internal data_pool earray. This
		//will make compaction fail, but is necessary so that earrays themselves can use TSMPs

	TSMPFLAG_COMPACTING = 1 << 1, //set and cleared during critical sections at beginning/end of each 
		//compaction
} TSMPFlags;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "allocationCount, structSize");
typedef struct ThreadSafeMemoryPool {
	SLIST_HEADER ListHead; NO_AST // thread-safe freelist
	void **data_pool; NO_AST
	size_t structSize; AST(FORMATSTRING(HTML_BYTES = 1))
	size_t structsPerBlock;

	int alignment; AST(NAME(Alignment)) //0 means default, must be power of 2 greater than or equal to the default alignment, which is 8 or 16

	int allocationCount; //cur num actually allocated

	int totalStructCount; //structsPerBlock * numBlocks

	size_t iTotalBytes_ForServerMonitoring; AST(NAME(TotalBytes) FORMATSTRING(HTML_BYTES = 1))
	size_t iUsedBytes_ForServerMonitoring; AST(NAME(UsedBytes) FORMATSTRING(HTML_BYTES = 1))
	size_t iFreeBytes_ForServerMonitoring; AST(NAME(FreeBytes) FORMATSTRING(HTML_BYTES = 1))
	char *pName; AST(KEY)
	TSMPFlags eFlags;

AST_STOP
	MEM_DBG_STRUCT_PARMS
} ThreadSafeMemoryPool;

//alignment of 0 means default, which is 8 or 16 byte depending on architecture
void threadSafeMemoryPoolInit_dbg(ThreadSafeMemoryPool *pool, size_t structsPerBlock, size_t structSize, int iAlignment, const char *pName MEM_DBG_PARMS);
void threadSafeMemoryPoolInitTrack_dbg(ThreadSafeMemoryPool *pool, size_t structsPerBlock, size_t structSize, int iAlignment, const char *pName, const char *pObj);
#define threadSafeMemoryPoolInit(pool, structsPerBlock, structSize, name) threadSafeMemoryPoolInit_dbg(pool, structsPerBlock, structSize, 0, name MEM_DBG_PARMS_INIT)
void threadSafeMemoryPoolInitSize_dbg(ThreadSafeMemoryPool *pool, size_t structChunkSize, size_t structSize, int iAlignment, const char *pName MEM_DBG_PARMS);
void threadSafeMemoryPoolInitSizeTrack_dbg(ThreadSafeMemoryPool *pool, size_t structChunkSize, size_t structSize, int iAlignment, const char *pName, const char *pObj);
#define threadSafeMemoryPoolInitSize(pool, structChunkSize, structSize, name) threadSafeMemoryPoolInitSize_dbg(pool, structChunkSize, structSize, 0, name MEM_DBG_PARMS_INIT)
void threadSafeMemoryPoolDeinit(ThreadSafeMemoryPool *pool);
SA_RET_NN_VALID void *threadSafeMemoryPoolAlloc(ThreadSafeMemoryPool *pool);
void *threadSafeMemoryPoolCalloc(ThreadSafeMemoryPool *pool);
void threadSafeMemoryPoolFree(ThreadSafeMemoryPool *pool, void *data);

void threadSafeMemoryPoolSetNoEarrays(ThreadSafeMemoryPool *pool, bool bSet);

// Cleanup: If empty, frees all but structsPerBlock worth of memory
void threadSafeMemoryPoolCompact(ThreadSafeMemoryPool *pool);

// *NOT* thread-safe iterator
typedef void (*ThreadSafeMemoryPoolForEachAllocationFunc)(ThreadSafeMemoryPool *pool, void *data, void *userData);
void threadSafeMemoryPoolForEachAllocationUNSAFE(ThreadSafeMemoryPool *pool, ThreadSafeMemoryPoolForEachAllocationFunc func, void *userData);

static __forceinline int threadSafeMemoryPoolAllocationCount(ThreadSafeMemoryPool *pool) { return pool->allocationCount; }

// Convenience macros
// Note: TSMP_CREATE can *not* be called lazily - it's not threadsafe.
//   Initialize in an auto-run or something.
#define TSMP_NAME(type) tsmemPool##type
#define TSMP_CREATE(type, amount)											\
	assertmsg(!TSMP_NAME(type).structSize, "TSMP_CREATE called twice");		\
	memBudgetAddStructMappingIfNotMapped(#type, __FILE__);					\
	threadSafeMemoryPoolInitTrack_dbg(&TSMP_NAME(type), amount, sizeof(type), 0, #type, #type)
#define TSMP_CREATE_SIZE(type, size)										\
	assertmsg(!TSMP_NAME(type).structSize, "TSMP_CREATE called twice");		\
	memBudgetAddStructMappingIfNotMapped(#type, __FILE__);					\
	threadSafeMemoryPoolInitSizeTrack_dbg(&TSMP_NAME(type), size, sizeof(type), 0, #type, #type)
#define TSMP_CREATE_ALIGNED(type, amount, alignment)											\
	assertmsg(!TSMP_NAME(type).structSize, "TSMP_CREATE called twice");		\
	memBudgetAddStructMappingIfNotMapped(#type, __FILE__);					\
	threadSafeMemoryPoolInitTrack_dbg(&TSMP_NAME(type), amount, sizeof(type), alignment, #type, #type)
// Special magic macro
// If running in X64, this will create a TSMP with chunks of (size) bytes
// If running in 32-bit, this will create a TSMP with chunks of (amount) structs
#ifdef _M_X64
#define TSMP_SMART_CREATE(type, amount, size) TSMP_CREATE_SIZE(type, size)
#else
#define TSMP_SMART_CREATE(type, amount, size) TSMP_CREATE(type, amount)
#endif

#define TSMP_X64_RECOMMENDED_CHUNK_SIZE 1024*1024

#define TSMP_DEFINE(type) static ThreadSafeMemoryPool TSMP_NAME(type) = {0}
#define TSMP_ALLOC(type) ((type*)threadSafeMemoryPoolAlloc(&TSMP_NAME(type)))
#define TSMP_CALLOC(type) ((type*)threadSafeMemoryPoolCalloc(&TSMP_NAME(type)))
#define TSMP_FREE(type, mem) if(mem) {threadSafeMemoryPoolFree(&TSMP_NAME(type), mem); (mem)=0;}
#define TSMP_ALLOCATION_COUNT(type) (threadSafeMemoryPoolAllocationCount(&TSMP_NAME(type)))

//don't use this unless you know what you're doing
TextParserResult ThreadSafeMemoryPoolFixupFunc(ThreadSafeMemoryPool *pool, enumTextParserFixupType eFixupType, void *pExtraData);
