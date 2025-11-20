#include "MemoryPool.h"
#include "MemoryPoolDebug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crtdbg.h>
#include "MemoryMonitor.h"
#include "timing.h"
#include "earray.h"
#include "wininclude.h"
#include "utils.h"
#include "memlog.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc);); // 64K from nodelist in forEachAllocation

#define CHUNK_ALIGNMENT			(16)
#define CHUNK_ALIGNMENT_MASK	(CHUNK_ALIGNMENT - 1)

int mpSuperFreeDebugging=0; // Verify freelist on every operation, very slow
int mpLightFreeDebugging=0; // Track allocations with the MemoryMonitor

typedef struct MemoryPoolNode MemoryPoolNode;
struct MemoryPoolNode{
	U32 sentinel;	// Should be MEMORYPOOL_SENTINEL_VALUE
	void *nextnode;
};

static struct MemoryPoolImp* memPoolList;

typedef struct MemoryPoolChunk {
	void *data;
	MemoryPoolNode *freelist;
	size_t freeCount;
	U32 *inUseMap; // Bitmask of which nodes are in use
} MemoryPoolChunk;

typedef struct MemoryPoolImp {
	struct {
		struct MemoryPoolImp*	next;
		struct MemoryPoolImp*	prev;
	} poolList;
	
	const char*					name;
	size_t						structSize;
	size_t						structCount;
	MemoryPoolChunk				**chunkList;
	size_t						allocationCount; // Number of structs allocated.
	size_t						chunkSize;
	size_t						alignmentOffset;
	int							inUseMapSizeInBytes;
	int							chunkAlignment;
	int							chunkAlignmentMask;
	int							compactFreq; // How often (in mpFree calls) to attempt compacting the memory pool.  A value of 0 turns it off.
	int							compactCounter;
	float						compactLimit;
	unsigned int				zeroMemory : 1;
	unsigned int				needsCompaction : 1;
	unsigned int				hasFreeChunk : 1; // At least one entry in the chunkList is completely free
	int							firstFreeNode; // Index of the first chunk known to have a free node
	int							firstFreeNonEmptyNode; // Index of the first chunk known to have a free node, but not be completely free
	MEM_DBG_STRUCT_PARMS
}MemoryPoolImp;

static bool mpDelayedCompaction=false; // Delay memory pool compaction until mpCompactPools() is called
static MemoryPoolImp **mpNeedCompaction;
static volatile int mpAnyNeedsCompaction;
static CRITICAL_SECTION mpCritSect; // For global lists

static int mpVerifyFreelistFastWhileLocked(MemoryPoolImp* pool);

uintptr_t mpGetSentinelValue(void){
	return MEMORYPOOL_SENTINEL_VALUE;
}

MemoryPoolMode mpGetMode(MemoryPoolImp* pool){
	MemoryPoolMode mode = TurnOffAllFeatures;
	if(pool->zeroMemory)
		mode |= ZERO_MEMORY_BIT;
	return mode;
}

int mpSetMode(MemoryPoolImp* pool, MemoryPoolMode mode){
	if(mode & ZERO_MEMORY_BIT){
		pool->zeroMemory = 1;
	}else
		pool->zeroMemory = 0;

	return 1;
}

void mpGetCompactParams(MemoryPoolImp *pool, int *frequency, float *limit)
{
	*frequency = pool->compactFreq;
	*limit = pool->compactLimit;
}

void mpSetCompactParams(MemoryPoolImp *pool, int frequency, float limit)
{
	if (frequency < 0)
		frequency = 0;
	pool->compactFreq = frequency;
	pool->compactLimit = limit;
}

void mpSetChunkAlignment(MemoryPoolImp *pool, int chunkAlignment)
{
	pool->chunkAlignment = chunkAlignment;
	pool->chunkAlignmentMask = chunkAlignment - 1;
}

static void enterMemPoolCS(void)
{
	ATOMIC_INIT_BEGIN;
	{
		InitializeCriticalSection(&mpCritSect);
	}
	ATOMIC_INIT_END;

	EnterCriticalSection(&mpCritSect);
}

static void leaveMemPoolCS(void)
{
	LeaveCriticalSection(&mpCritSect);
}

static void insertIntoPoolList(MemoryPool pool)
{
	MemoryPool cur;
	MemoryPool prev = NULL;

	enterMemPoolCS();	

	cur = memPoolList;
	
	while(cur){
		if(cur->name && stricmp(cur->name, pool->name) > 0){
			break;
		}
		
		prev = cur;
		cur = cur->poolList.next;
	}
	
	if(prev){
		prev->poolList.next = pool;
		pool->poolList.prev = prev;
	}else{
		pool->poolList.prev = NULL;
		memPoolList = pool;
	}
	
	if(cur){
		cur->poolList.prev = pool;
	}
	
	pool->poolList.next = cur;
	
	leaveMemPoolCS();
}

MemoryPool createMemoryPool_dbg(MEM_DBG_PARMS_VOID){
	MemoryPool pool = scalloc(1, sizeof(MemoryPoolImp));
	
	pool->name = "*** UnnamedMemoryPool";

	MEM_DBG_STRUCT_PARMS_INIT(pool);

	pool->chunkAlignment = CHUNK_ALIGNMENT;
	pool->chunkAlignmentMask = CHUNK_ALIGNMENT_MASK;
	
	insertIntoPoolList(pool);

	return pool;
}

MemoryPool createMemoryPoolNamed(const char* name MEM_DBG_PARMS){
	MemoryPool pool = scalloc(1, sizeof(MemoryPoolImp));
	
	pool->name = name ? name : "*** UnnamedMemoryPool";

	MEM_DBG_STRUCT_PARMS_INIT(pool);

	pool->chunkAlignment = CHUNK_ALIGNMENT;
	pool->chunkAlignmentMask = CHUNK_ALIGNMENT_MASK;
	
	insertIntoPoolList(pool);
	
	return pool;
}

static char* mpGetAlignedAddress(MemoryPoolImp* pool, char* address){
	intptr_t alignmentOffset = (intptr_t)address & pool->chunkAlignmentMask;

	assert(!((intptr_t)address & 3));

	if(alignmentOffset){
		address += pool->chunkAlignment - alignmentOffset;
	}

	if(pool->alignmentOffset){
		address += pool->chunkAlignment - pool->alignmentOffset;
	}

	return address;
}

/* Function mpInitMemoryChunk()
 *  Cuts up given memory chunk into the proper size for the given memory pool and
 *	adds the cut up pieces to the free-list in the memory pool.
 *
 *	Note that this function does *not* add the memory chunk to the memory pool's
 *	memory chunk table.  Remember to do this or else the memory chunk will be leaked
 *	when the pool is destroyed.
 *
 */
static void mpInitMemoryChunk(MemoryPoolImp* pool, MemoryPoolChunk *memoryChunk)
{
	size_t i;
	MemoryPoolNode *node;
	memoryChunk->inUseMap = (U32*)(((char*)memoryChunk) + sizeof(MemoryPoolChunk));
	memoryChunk->data = mpGetAlignedAddress(pool, ((char*)memoryChunk) + sizeof(MemoryPoolChunk) + pool->inUseMapSizeInBytes );

	memset(memoryChunk->inUseMap, 0, pool->inUseMapSizeInBytes);
	memoryChunk->freeCount = pool->structCount;

	node = memoryChunk->data;
	memoryChunk->freelist = node;
	for (i=0; i<pool->structCount; i++) {
		node->sentinel = MEMORYPOOL_SENTINEL_VALUE;
		if (i != pool->structCount-1) {
			node->nextnode = (MemoryPoolNode*)(((char*)node) + pool->structSize);
			node = node->nextnode;
		} else {
			node->nextnode = NULL;
		}
	}
}




/* Function mpAddMemoryChunk()
 *	Adds a new chunk of memory into the memory pool based on the existing structSize and structCount
 *	settings.  The memory is then initialized then added to the pool's memory chunk table.
 *
 *  Returns the MemoryPoolChunk*
 *
 */
static MemoryPoolChunk *mpAddMemoryChunk_dbg(MemoryPoolImp* pool MEM_DBG_PARMS)
{
	MemoryPoolChunk* memoryChunk;
	int i;
	
	PERFINFO_AUTO_START("mpAddMemoryChunk_dbg", 1);
		// Cannot add a memory chunk to a memory pool if there is no pool.
		assert(pool);
		
		// Create the memory chunk to be added.
		memoryChunk = scalloc(pool->chunkSize, 1);
		assert(memoryChunk);

		mpInitMemoryChunk(pool, memoryChunk);

		// Add the memory chunk to a list in the pool.
		for (i = 0; i < eaSize(&pool->chunkList); ++i)
		{
			if (memoryChunk < pool->chunkList[i])
				break;
		}

		// keep sorted
		seaInsert(&pool->chunkList, memoryChunk, i);

		pool->firstFreeNode = i; // This function is only called if all chunks are full
		pool->firstFreeNonEmptyNode = eaSize(&pool->chunkList);
	PERFINFO_AUTO_STOP();
	return memoryChunk;
}

/* Function initMemoryPool()
 *	Initializes the given memory pool.
 *
 *	Parameters:
 *		pool - The memory pool to be initialized.
 *		structSize - The size of an "allocation unit".  A piece of memory of this size will be returned everytime
 *					 mpAlloc() is called.
 *		structCount - The number of "allocation units" to put in the pool initially.  Together with structSize,
 *					  this number also dictates the amount of "allocation units" to grow by when the pool runs
 *					  dry.
 *
 *
 */
void initMemoryPoolOffset_dbg(MemoryPoolImp* pool, int structSize, int structCount, int offset MEM_DBG_PARMS){
	assert(structCount > 0);
	assert(!pool->structSize && !pool->structCount);
	pool->structSize = max(structSize, sizeof(MemoryPoolNode));
	pool->structCount = structCount;
	assert(offset < pool->chunkAlignment);
	pool->inUseMapSizeInBytes = (structCount+31)/32*4;
	pool->chunkSize = pool->structCount * pool->structSize + pool->chunkAlignment - 4 + (offset ? pool->chunkAlignment : 0) + sizeof(MemoryPoolChunk) + pool->inUseMapSizeInBytes;
	pool->alignmentOffset = offset;
	mpSetMode(pool, Default);
}

void initMemoryPool_dbg(MemoryPoolImp* pool, int structSize, int structCount MEM_DBG_PARMS){
	initMemoryPoolOffset_dbg(pool, structSize, structCount, 0 MEM_DBG_PARMS_CALL);
}

MemoryPool initMemoryPoolLazy_dbg(MemoryPool pool, int structSize, int structCount MEM_DBG_PARMS){
	PERFINFO_AUTO_START("initMemoryPoolLazy_dbg", 1);
		if(!pool){
			pool = createMemoryPool_dbg(MEM_DBG_PARMS_CALL_VOID);
			initMemoryPool_dbg(pool, structSize, structCount MEM_DBG_PARMS_CALL);
		}else{
			mpFreeAll(pool);
		}
	PERFINFO_AUTO_STOP();

	return pool;
}

static void removeFromPoolList(MemoryPool pool){
	if (mpDelayedCompaction) {
		//if in compact list, remove it!
		int index;
		enterMemPoolCS();
		if ((index=eaFind(&mpNeedCompaction, pool))!=-1) {
			eaRemoveFast(&mpNeedCompaction, index);
		}
		leaveMemPoolCS();
	}

	if(pool->poolList.prev){
		pool->poolList.prev->poolList.next = pool->poolList.next;
	}else{
		memPoolList = pool->poolList.next;
	}

	if(pool->poolList.next){
		pool->poolList.next->poolList.prev = pool->poolList.prev;
	}
}


/* Function destroyMemoryPool()
 *	Frees all memory allocated by memory pool, including the pool itself.
 *
 *	After this call, all memory allocated out of the given pool is invalidated.
 *
 */
void destroyMemoryPool(MemoryPoolImp* pool){
	if(!pool){
		return;
	}
	
	PERFINFO_AUTO_START("destroyMemoryPool", 1);
		removeFromPoolList(pool);
		eaDestroyEx(&pool->chunkList, NULL);
		free(pool);
	PERFINFO_AUTO_STOP();
}

__forceinline static int getChunkForAddress(MemoryPoolImp *pool, char *memory)
{
	int size = eaSize(&pool->chunkList);
	int left = 0;
	int right = size-1;
	int pos = (right+left)/2;
	while (left <= right)
	{
		if ((char*)pool->chunkList[pos] <= memory && memory < ((char*)pool->chunkList[pos] + pool->chunkSize))
			return pos;
		if (memory < (char*)pool->chunkList[pos])
			right = pos-1;
		else
			left = pos+1;
		pos = (right+left)/2;
	}
	return -1;
}

static void **mpStuffToFree=NULL; // Linked list of data to be freed.  Cannot use an EArray or anything else that allocates memory!
static void compactMemoryPoolFreeFunc(void *data)
{
	if (mpDelayedCompaction) {
		void **newhead = (void**)data;
		*newhead = mpStuffToFree;
		mpStuffToFree = newhead;
	} else {
		free(data);
	}
}
static void compactMemoryPoolDoFreeing(void)
{
	void **next = NULL;
	while (mpStuffToFree) {
		next = *mpStuffToFree;
		free(mpStuffToFree);
		mpStuffToFree = next;
	}
}

// JE: Made static, since if it is called outside of this file, it needs to check some critical sections!
static void compactMemoryPool(MemoryPoolImp *pool)
{
	int i, needcompacting = 0;

	PERFINFO_AUTO_START("compactMemoryPool", 1);

	assert(pool->firstFreeNode>=0);

	for (i = pool->firstFreeNode; i < eaSize(&pool->chunkList); i++)
	{
		ANALYSIS_ASSUME(pool->chunkList);
		if ((size_t)pool->chunkList[i]->freeCount >= pool->structCount) {
			needcompacting = 1;
			break;
		}
	}

	if (!needcompacting)
	{
		pool->hasFreeChunk = 0; // Must have been a false positive
		pool->needsCompaction = 0;
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_START("compactMemoryPool:compacting", 1);

	// free completely empty chunks
	for (i = 0; i < eaSize(&pool->chunkList); i++)
	{
		if ((size_t)pool->chunkList[i]->freeCount >= pool->structCount)
		{
			compactMemoryPoolFreeFunc(pool->chunkList[i]);
			eaRemove(&pool->chunkList, i);
			if (i == pool->firstFreeNode) {
				// Effectively increment firstFreeNode to the next chunk
			} else if (i < pool->firstFreeNode) {
				pool->firstFreeNode--;
			}
			if (i == pool->firstFreeNonEmptyNode) {
				// Effectively increment firstFreeNode to the next chunk
			} else if (i < pool->firstFreeNonEmptyNode) {
				pool->firstFreeNonEmptyNode--;
			}
			--i;
		}
	}

	pool->hasFreeChunk = 0;
	pool->needsCompaction = 0;

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();
}

bool gPoolCompactionEnabled = true;

// enable or disable pool compaction
void mpEnablePoolCompaction(bool enabled)
{
	gPoolCompactionEnabled = enabled;
}

extern volatile U32 g_inside_pool_malloc;

void mpCompactPools(void)
{
	MemoryPoolImp *pool;
	if (!gPoolCompactionEnabled)
	{
		return;
	}
	mpDelayedCompaction = true;
	if (!mpAnyNeedsCompaction)
		return;
	PERFINFO_AUTO_START("mpCompactPools", 1);
	InterlockedIncrement(&g_inside_pool_malloc); // don't do small allocs while compacting, we might be compacting the small alloc pools
	enterMemPoolCS();
	mpAnyNeedsCompaction = false;
	while (pool = eaPop(&mpNeedCompaction)) {
		compactMemoryPool(pool);
	}
	leaveMemPoolCS();
	InterlockedDecrement(&g_inside_pool_malloc);
	compactMemoryPoolDoFreeing(); // Must free *outside* of critical section
	PERFINFO_AUTO_STOP();
}

// JE: Made static, since if it is called outside of this file, it needs to check some critical sections!
static void compactMemoryPoolIfUnder(MemoryPoolImp *pool, float limit)
{
	float currUtilization;
	int chunkListSize = eaSize(&pool->chunkList);
	if (chunkListSize < 3)
	{
		return;
	}
	if (!gPoolCompactionEnabled)
	{
		return;
	}
	if (!pool->hasFreeChunk || pool->needsCompaction)
		return;

	currUtilization = ((float)pool->allocationCount) / (chunkListSize * pool->structCount);
	if (currUtilization < limit) {
		if (mpDelayedCompaction) {
			// If doing delayed compaction, simply add to another list to get compacted later
			enterMemPoolCS();
			pool->needsCompaction = 1;
			eaPush(&mpNeedCompaction, pool);
			mpAnyNeedsCompaction = true;
			leaveMemPoolCS();
		}
	}
}

void mpCompactPool(MemoryPoolImp *pool)
{
	if (mpDelayedCompaction && !pool->needsCompaction && pool->hasFreeChunk) {
		// If doing delayed compaction, simply add to another list to get compacted later
		enterMemPoolCS();
		pool->needsCompaction = 1;
		pool->compactCounter = 0;
		eaPush(&mpNeedCompaction, pool);
		mpAnyNeedsCompaction = true;
		leaveMemPoolCS();
	}
}


/* Function mpAlloc
 *	Allocates a piece of memory from the memory pool.  The size of the allocation will
 *	match the structSize given to the memory pool during initialization (initMemoryPool).
 *	
 *	Returns:
 *		NULL - cannot allocate memory because the memory pool ran dry and
 *			   it is not possible to get more memory.
 *				(note that asserts will occur in memory allocation before the NULL return)
 *		otherwise - valid memory pointer of size mpStructSize(pool)
 */
void* mpAlloc_dbg(MemoryPoolImp* pool, int forceMyCallerName, const char *caller_fname, int line)
{
	void* memory;
	bool needsCompaction;
	int chunk_idx;
	int chunkListSize;
	MemoryPoolChunk *chunk = NULL;

	PERFINFO_AUTO_START("mpAlloc_dbg", 1);

		// If there is no memory pool, no memory can be allocated.
		assert(pool);

		// If in need of compaction, enter critical section so we don't alloc while a pool is being compacted
		needsCompaction = pool->needsCompaction;
		if (needsCompaction) {
			enterMemPoolCS();
		}

		if (mpSuperFreeDebugging) {
			assert(mpVerifyFreelistFastWhileLocked(pool));
		}

		chunkListSize = eaSize(&pool->chunkList);
		if (pool->allocationCount == pool->structCount * chunkListSize) {
			// We're full, allocate a new chunk
			if (forceMyCallerName){
				chunk = mpAddMemoryChunk_dbg(pool MEM_DBG_PARMS_CALL);
			}else{
				chunk = mpAddMemoryChunk_dbg(pool MEM_DBG_STRUCT_PARMS_CALL(pool));
			}
			chunkListSize++;
		}

		assert(pool->firstFreeNode <= chunkListSize);
		assert(pool->firstFreeNonEmptyNode <= chunkListSize);

		if (pool->compactFreq)
		{
			// Find the first chunk which has free memory, and is not completely empty (and therefore needs compaction)
			for (chunk_idx=pool->firstFreeNonEmptyNode; chunk_idx<chunkListSize; chunk_idx++) {
				if (pool->chunkList[chunk_idx]->freeCount > 0 && pool->chunkList[chunk_idx]->freeCount < pool->structCount) {
					break;
				} else {
					pool->firstFreeNonEmptyNode++; // Chunk either has none free, or all free
				}
			}
			if (chunk_idx==chunkListSize) {
				// No partially free chunks, use the first fully free chunk
				for (chunk_idx=pool->firstFreeNode; chunk_idx<chunkListSize; chunk_idx++) {
					if (pool->chunkList[chunk_idx]->freeCount > 0) {
						// This is going to be the new firstFreeNonEmptyNode now
						pool->firstFreeNonEmptyNode = chunk_idx;
						break;
					} else {
						pool->firstFreeNode++; // Chunk has none free
					}
				}
			}
		} else {
			// Not doing compaction, just use the quickest to access, don't care about defragmenting
			for (chunk_idx=pool->firstFreeNode; chunk_idx<chunkListSize; chunk_idx++) {
				if (pool->chunkList[chunk_idx]->freeCount > 0) {
					// This is going to be the new firstFreeNonEmptyNode now
					pool->firstFreeNonEmptyNode = chunk_idx;
					break;
				} else {
					pool->firstFreeNode++; // Chunk has none free
				}
			}
		}

		assert(chunk_idx != chunkListSize); // We should have allocated a new chunk above if we were full!
		assert(chunkListSize);
		chunk = pool->chunkList[chunk_idx];
		if (chunk->freeCount == 1)
		{
			// About to allocate the last node from this chunk
			if (chunk_idx == pool->firstFreeNode) {
				pool->firstFreeNode++;
			}
			if (chunk_idx == pool->firstFreeNonEmptyNode) {
				pool->firstFreeNonEmptyNode++;
			}
		}

#if NO_FREELIST
		// This was too slow, so added a per-chunk freelist instead
		// Find the free memory by walking the bitfield, 32 structs at a time
		{
			U32 *walk = chunk->inUseMap;
			int memory_index=0;
			while (*walk == 0xFFFFFFFF) {
				walk++;
				memory_index+=32;
			}
			while (TSTB(chunk->inUseMap, memory_index)) {
				memory_index++;
			}
			memory = ((char*)chunk->data) + pool->structSize * memory_index;
			SETB(chunk->inUseMap, memory_index);
			assert(chunk->freeCount > 0);
			chunk->freeCount--;
		}
#else
		{
			int memory_index;
			assert(chunk->freelist);
			memory = chunk->freelist;
			chunk->freelist = chunk->freelist->nextnode;
			chunk->freeCount--;
			assert(!chunk->freeCount == !chunk->freelist);
			memory_index = (int)(((char*)memory - (char*)chunk->data) / pool->structSize);
			assertmsg(!TSTB(chunk->inUseMap, memory_index), "MemoryPool corrupted"); // Like cause is unsynchronized access from multiple threads
			SETB(chunk->inUseMap, memory_index);
		}
#endif

		// Make sure other parts of the program have been playing nice
		// with the memory allocated from the mem pool.
		assert(((MemoryPoolNode*)memory)->sentinel == MEMORYPOOL_SENTINEL_VALUE);

		// Initialize the returned memory to zero
		if(pool->zeroMemory)
			memset(memory, 0, pool->structSize);

		pool->allocationCount++;
		assert(pool->allocationCount > 0);
		
		if (mpSuperFreeDebugging) {
			assert(mpVerifyFreelistFastWhileLocked(pool));
		}

		if (mpLightFreeDebugging) {
			char temp[260];
			strcpy(temp, caller_fname);
			strcat(temp, "-MP");
			memMonitorTrackUserMemory(allocAddString(temp), 0, pool->structSize, MM_ALLOC);
		}

		if (needsCompaction) {
			leaveMemPoolCS();
		}
	
	PERFINFO_AUTO_STOP();
	
	return memory;
}

/* Function mpFree
*	Returns a piece of memory back into the pool.
*	
*	Parameters:
*		pool - a valid memory pool
*		memory - the piece of memory to return to the pool.  The size
*				 is implied by mpStructSize() of the given pool.
*
*	Returns:
*		1 - memory returned successfully
*		0 - some error encountered while returning memory
*
*/
__forceinline static int mpFreeInternal(MemoryPoolImp* pool, void* memory){

	MemoryPoolNode* node;
	int chunk_idx;
	int memory_idx;
	U32 offset;
	MemoryPoolChunk *chunk;
	
	PERFINFO_AUTO_START_L2("mpFree", 1);
		assert(pool);

		// Take the memory and overlay a memory pool node on top of it.
		node = memory;

		// Increment the chunk free count.
		chunk_idx = getChunkForAddress(pool, memory);
		assertmsg(chunk_idx >= 0, "Tried to free memory to a memory pool that doesn't own the memory!");
		chunk = pool->chunkList[chunk_idx];  
		assert((size_t)chunk->freeCount < pool->structCount);
		offset = (char*)memory - (char*)chunk->data;
		assert(offset % pool->structSize == 0);
		memory_idx = (int)((size_t)offset / pool->structSize);
		// Mark the memory as free
		chunk->freeCount++;
		assertmsg(TSTB(chunk->inUseMap, memory_idx), "Freeing a memory pool node which is already freed");
		CLRB(chunk->inUseMap, memory_idx);

		if (chunk_idx < pool->firstFreeNode)
			pool->firstFreeNode = chunk_idx;
		if (chunk->freeCount < pool->structCount && chunk_idx < pool->firstFreeNonEmptyNode)
			pool->firstFreeNonEmptyNode = chunk_idx;

		node->sentinel = MEMORYPOOL_SENTINEL_VALUE;
		node->nextnode = chunk->freelist;
		chunk->freelist = node;

		assert(pool->allocationCount > 0);
		pool->allocationCount--;

		if (chunk->freeCount == pool->structCount) {
			// New totally empty chunk
			pool->hasFreeChunk = 1;
		}

	PERFINFO_AUTO_STOP_L2();
	
	return 1;
}

int mpFree_dbg(MemoryPoolImp* pool, void* memory MEM_DBG_PARMS){

	bool needsCompaction;
	
	if(!memory){
		return 1;
	}
	
	PERFINFO_AUTO_START("mpFree_dbg", 1);

		needsCompaction = pool->needsCompaction;
		if (needsCompaction) { // If in need of compaction, enter critical section
			enterMemPoolCS();
		}
		if (mpSuperFreeDebugging) {
			assert(mpVerifyFreelistFastWhileLocked(pool));
		}

		mpFreeInternal(pool, memory);

		if (mpSuperFreeDebugging) {
			assert(mpVerifyFreelistFastWhileLocked(pool));
		}

		if (pool->zeroMemory) {
			// Reset memory to 0xFAFAFAFA on free for easier debugging
			memset(((char*)memory)+sizeof(MemoryPoolNode), 0xFA, pool->structSize-sizeof(MemoryPoolNode));
			if (mpLightFreeDebugging) {
				// Store the line number and caller of who freed this, fill the entire block with it
				char temp[256];
				char *mem = (char*)memory+sizeof(MemoryPoolNode);
				size_t mem_size = pool->structSize - sizeof(MemoryPoolNode);
				size_t bytes;
				sprintf(temp, "%d:%s", line, caller_fname);
				while (mem_size>1)
				{
					bytes = min(mem_size-1, strlen(temp)+1);
					strncpy_s(mem,
						mem_size, temp,
						bytes);
					mem += bytes;
					mem_size -= bytes;
				}
			}
		}

		if (!pool->needsCompaction && pool->compactFreq) {
			pool->compactCounter++;
			if (pool->compactCounter == pool->compactFreq) {
				pool->compactCounter = 0;
				compactMemoryPoolIfUnder(pool, pool->compactLimit);
			}
		}


		if (mpLightFreeDebugging) {
			char temp[260];
			strcpy(temp, caller_fname);
			strcat(temp, "-MP");
			memMonitorTrackUserMemory(allocAddString(temp), 0, -(S32)pool->structSize, MM_FREE);
		}
		
		if (needsCompaction) {
			leaveMemPoolCS();
		}

	PERFINFO_AUTO_STOP();

	return 1;
}

int mpIsValidPtr(MemoryPoolImp *pool, void *ptr)
{
	return getChunkForAddress(pool, ptr)!=-1;
}

int mpVerifyFreelist(MemoryPoolImp* pool)
{
	size_t freeCount=0;
	int retry_count=25; // If we find an error, retry a few times, in case it's simply a threading issue
	bool bRetry=false;

	if (!pool)
		return 1;

	// Make sure each node in the memory pool still looks valid.
	//	Each node stores a sentinel value if it is free

	do {
		int i;
		size_t j;
		size_t freelist_size=0;
		size_t total_allocs = pool->structCount*eaSize(&pool->chunkList);
		size_t expected_free = total_allocs - pool->allocationCount;
		size_t chunk_count=0;
		MemoryPoolNode *node;

#define ReportError(fmt, ...)								\
				if (retry_count == 0) {						\
					memlog_printf(NULL, fmt, __VA_ARGS__);	\
					OutputDebugStringf(fmt, __VA_ARGS__);	\
					return 0;								\
				} else {									\
					/* Might be a problem in another thread, sleep, try again! */ \
					retry_count--;							\
					Sleep(1);								\
					bRetry = true;							\
					break;									\
				}

		bRetry = false;

		// Verify the bit fields
		// Single loop so the break; in ReportError works
		for (i=0, j=0; i<eaSize(&pool->chunkList); ) {
			if (j==0) {
				if (pool->chunkList[i]->freeCount > 0 && pool->firstFreeNode > i) {
					ReportError("firstFreeNode is pointing to chunk %d, when chunk %d has %d free node(s).\n",
						pool->firstFreeNode, i, pool->chunkList[i]->freeCount);
				}
				if (pool->chunkList[i]->freeCount > 0 && pool->chunkList[i]->freeCount < pool->structCount && pool->firstFreeNonEmptyNode > i) {
					ReportError("firstFreeNonEmptyNode is pointing to chunk %d, when chunk %d has %d free node(s).\n",
						pool->firstFreeNonEmptyNode, i, pool->chunkList[i]->freeCount);
				}
				if (pool->chunkList[i]->freeCount > pool->structCount)
				{
					ReportError("chunk %d claims %d free elements when only %d elements were allocated.\n",
						i, pool->chunkList[i]->freeCount, pool->structCount);
				}
				if (!pool->chunkList[i]->data)
				{
					ReportError("chunk %d is missing its data pointer.\n", i);
				}
			}

			if (!TSTB(pool->chunkList[i]->inUseMap, j)) {
				node = (MemoryPoolNode*)((char*)pool->chunkList[i]->data + j*pool->structSize);
				if (node->sentinel != MEMORYPOOL_SENTINEL_VALUE) {
					ReportError("Freelist has a corrupt entry at 0x%08p.\n", node);
				}
				chunk_count++;
			}

			j++;
			if (j==pool->structCount) {
				// Move onto the next chunk
				if (chunk_count != pool->chunkList[i]->freeCount)
				{
					ReportError("Chunk %d (%p)'s bitfield says there are %d free elements, but freeCount is %d\n",
						i, pool->chunkList[i], chunk_count, pool->chunkList[i]->freeCount);
				}
				freelist_size+=chunk_count;

				i++;
				j=0;
				chunk_count = 0;
			}
		}
		do { // To catch the break; in ReportError()
			// Check to see if numbers match up
			if (!bRetry && expected_free != freelist_size) {
				ReportError("Pool has wrong number of elements flagged as free in its bitfields (has: %d expected: %d).\n", freelist_size, expected_free);
			}
		} while (false);

		// Verify the per-chunk freelist
		node = NULL;
		for (i=-1; !bRetry && i<eaSize(&pool->chunkList); ) {
			if (node==NULL) {
				if (i>=0) {
					// Check to see if numbers match up
					if (pool->chunkList[i]->freeCount != freelist_size) {
						ReportError("Pool has wrong number of elements in its freelist (chunk: %d has: %d expected: %d).\n", i, freelist_size, pool->chunkList[i]->freeCount);
					}
				}
				i++;
				if (i==eaSize(&pool->chunkList))
					break;
				// Advance to new chunk
				if (!pool->chunkList[i]->freeCount != !pool->chunkList[i]->freelist) {
					ReportError("Chunk %d has a freelist, but no free count, or the opposite.\n", i);
				}
				freelist_size = 0;
				if (pool->chunkList[i]->freeCount) {
					node = pool->chunkList[i]->freelist;
				}
			} else {
				// check node, then advance
				ptrdiff_t offset = (char*)node - (char*)pool->chunkList[i]->data;
				int index = (int)(offset / pool->structSize);
				if (offset % pool->structSize != 0) {
					ReportError("Chunk %d, Node at 0x%p is not the right offset from data pointer.\n", i, node);
				}
				if (index < 0 || index >= (int)pool->structCount) {
					ReportError("Chunk %d, Node at 0x%p is out of range for the chunk.\n", i, node);
				}
				if (node->sentinel != MEMORYPOOL_SENTINEL_VALUE) {
					ReportError("Chunk %d, Freelist has a corrupt entry(2) at 0x%08p.\n", i, node);
				}
				if (TSTB(pool->chunkList[i]->inUseMap, index)) {
					ReportError("Chunk %d, Node at 0x%08p is in freelist, but is flagged as inUse.\n", i, node);
				}
				freelist_size++;
				node = node->nextnode;
				if (freelist_size > pool->chunkList[i]->freeCount) {
					ReportError("Chunk %d, Infinite loop or too many elements in freelist.\n", i);
				}
			}
		}
	} while (bRetry);
	return 1;
#undef ReportError
}


static int mpVerifyFreelistFastWhileLocked(MemoryPoolImp* pool)
{
	size_t freeCount=0;
	if (!pool)
		return 1;

	// Make sure each node in the memory pool still looks valid.
	//	Each node stores a sentinel value if it is free
	{
		int i;
		size_t j;
		size_t freelist_size=0;
		size_t total_allocs = pool->structCount*eaSize(&pool->chunkList);
		size_t expected_free = total_allocs - pool->allocationCount;
		MemoryPoolNode *node;

#define ReportError(fmt, ...)							\
				memlog_printf(NULL, fmt, __VA_ARGS__);	\
				OutputDebugStringf(fmt, __VA_ARGS__);	\
				return 0;								\

		// Verify the bit fields
		for (i=0; i<eaSize(&pool->chunkList); i++) {
			MemoryPoolChunk *chunk = pool->chunkList[i];
			size_t chunk_count=0;
			if (chunk->freeCount > 0 && pool->firstFreeNode > i) {
				ReportError("firstFreeNode is pointing to chunk %d, when chunk %d has %d free node(s).\n",
					pool->firstFreeNode, i, chunk->freeCount);
			}
			if (chunk->freeCount > 0 && chunk->freeCount < pool->structCount && pool->firstFreeNonEmptyNode > i) {
				ReportError("firstFreeNonEmptyNode is pointing to chunk %d, when chunk %d has %d free node(s).\n",
					pool->firstFreeNonEmptyNode, i, chunk->freeCount);
			}
			node = (MemoryPoolNode*)chunk->data;
			for (j=0; j<pool->structCount; j++, node = (MemoryPoolNode*)((char*)node + pool->structSize)) {
				if (!TSTB(chunk->inUseMap, j)) {
					if (node->sentinel != MEMORYPOOL_SENTINEL_VALUE) {
						ReportError("Freelist has a corrupt entry at 0x%08p.\n", node);
					}
					chunk_count++;
				}
			}
			if (chunk_count != chunk->freeCount)
			{
				ReportError("Chunk %d (%p)'s bitfield says there are %d free elements, but freeCount is %d\n",
					i, chunk, chunk_count, chunk->freeCount);
			}

			// Verify the per-chunk freelist

			if (!chunk->freeCount != !chunk->freelist) {
				ReportError("Chunk %d has a freelist, but no free count, or the opposite.\n", i);
			}

			node = chunk->freelist;
			chunk_count = 0;
			while (node) {
				// check node, then advance
				ptrdiff_t offset = (char*)node - (char*)chunk->data;
				int index = (int)(offset / pool->structSize);
				if (offset % pool->structSize != 0) {
					ReportError("Chunk %d, Node at 0x%p is not the right offset from data pointer.\n", i, node);
				}
				if (index < 0 || index >= (int)pool->structCount) {
					ReportError("Chunk %d, Node at 0x%p is out of range for the chunk.\n", i, node);
				}
				if (node->sentinel != MEMORYPOOL_SENTINEL_VALUE) {
					ReportError("Chunk %d, Freelist has a corrupt entry(2) at 0x%08p.\n", i, node);
				}
				if (TSTB(chunk->inUseMap, index)) {
					ReportError("Chunk %d, Node at 0x%08p is in freelist, but is flagged as inUse.\n", i, node);
				}
				chunk_count++;
				node = node->nextnode;
				if (chunk_count > chunk->freeCount) {
					ReportError("Chunk %d, Infinite loop or too many elements in freelist.\n", i);
				}
			}

			if (chunk->freeCount != chunk_count) {
				ReportError("Chunk %d has wrong number of elements in its freelist (has: %d expected: %d).\n", i, freelist_size, pool->chunkList[i]->freeCount);
			}

			freelist_size+=chunk_count;
		}
		// Check to see if numbers match up
		if (expected_free != freelist_size) {
			ReportError("Pool has wrong number of elements flagged as free in its bitfields (has: %d expected: %d).\n", freelist_size, expected_free);
		}
	}
	return 1;
}

/* Function mpFreeAll
 *	Return all memory controlled by the memory pool back into the pool.  This effectively
 *	destroys/invalidates all structure held by the memory pool.
 *
 *	Note that this function does not actually free any of the memory that is currently
 *	held by the pool.
 *
 */
void mpFreeAll(MemoryPoolImp* pool){
	int i;

	if(!pool)
		return;

	PERFINFO_AUTO_START("mpFreeAll", 1);
		if (pool->needsCompaction) {
			int index;
			// If in need of compaction, remove it from the list, not needed anymore
			enterMemPoolCS();
			pool->needsCompaction = 0;
			if ((index=eaFind(&mpNeedCompaction, pool))!=-1) {
				eaRemoveFast(&mpNeedCompaction, index);
			}
			leaveMemPoolCS();
		}

		for (i=eaSize(&pool->chunkList)-1; i>=0; i--) {
			mpInitMemoryChunk(pool, pool->chunkList[i]);
		}
		pool->allocationCount = 0;
		pool->firstFreeNode = 0;
		pool->firstFreeNonEmptyNode = eaSize(&pool->chunkList);

	PERFINFO_AUTO_STOP();

}

void mpFreeAllocatedMemory(MemoryPoolImp* pool){
	bool needsCompaction;
	if(!pool)
		return;

	PERFINFO_AUTO_START("mpFreeAllocatedMemory", 1);
		needsCompaction = pool->needsCompaction;
		if (needsCompaction) { // If in need of compaction, enter critical section
			enterMemPoolCS();
		}

		eaClearEx(&pool->chunkList, NULL);
		pool->allocationCount = 0;
		pool->firstFreeNode = 0;
		pool->firstFreeNonEmptyNode = eaSize(&pool->chunkList);

		if (needsCompaction) {
			leaveMemPoolCS();
		}
	PERFINFO_AUTO_STOP();
}

size_t mpStructSize(MemoryPoolImp* pool){
	if(!pool)
		return 0;

	return pool->structSize;
}

size_t mpGetAllocatedCount(MemoryPoolImp* pool){
	if(!pool)
		return 0;
		
	return pool->allocationCount;
}

size_t mpGetAllocatedChunkMemory(MemoryPoolImp* pool){
	if(!pool)
		return 0;
		
	return pool->structSize * pool->structCount * eaSize(&pool->chunkList);
}

const char* mpGetName(MemoryPool pool){
	return pool->name;
}

const char* mpGetFileName(MemoryPool pool){
	return pool->caller_fname;
}

int	mpGetFileLine(MemoryPool pool){
	return pool->line;
}

void mpForEachMemoryPool(MemoryPoolForEachFunc callbackFunc, void *userData){
	MemoryPool cur;
	
	for(cur = memPoolList; cur; cur = cur->poolList.next){
		callbackFunc(cur, userData);
	}
}

/* Function mpReclaimed()
 *	Answers if a piece of memory has been reclaimed by a memory pool.  Note that this function
 *	is not fool-proof.  It does not know if the given memory is allocated out of a memory pool.
 *	This function merely checks if the piece of memory holds a valid integrity value.  It will 
 *	fail when a piece of memory that is not reclaimed just happens to be storing the expected 
 *	integrity value at the expected location.
 *
 *	Returns:
 *		0 - memory not reclaimed.
 *		1 - memory reclaimed.
 */
int mpReclaimed(void* memory){
	MemoryPoolNode* node;
	assert(memory);

	node = memory;
	if(node->sentinel == MEMORYPOOL_SENTINEL_VALUE)
		return 1;
	else
		return 0;
}


void testMemoryPool(){
	MemoryPool pool;
	void* memory;

	pool = createMemoryPool();
	initMemoryPool(pool, 16, 16);


	printf("MemoryPool created\n");
	printf("Validating memory pool\n");
	if(mpVerifyFreelist(pool)){
		printf("The memory pool is valid\n");
	}else
		printf("The memory pool has been corrupted\n");

	memory = mpAlloc_dbg(pool, 0, __FILE__, __LINE__);

	printf("\n");
	printf("Memory allocated\n");
	printf("Validating memory pool\n");
	if(mpVerifyFreelist(pool)){
		printf("The memory pool is valid\n");
	}else
		printf("The memory pool has been corrupted\n");

	mpFree_dbg(pool, memory, __FILE__, __LINE__);
	printf("\n");
	printf("Memory freed\n");
	printf("Validating memory pool\n");
	if(mpVerifyFreelist(pool)){
		printf("The memory pool is valid\n");
	}else
		printf("The memory pool has been corrupted\n");

// 	mpFree_dbg(pool, memory, __FILE__, __LINE__);
// 	printf("\n");
// 	printf("Memory freed again\n");
// 	printf("Validating memory pool\n");
// 	if(mpVerifyFreelist(pool)){
// 		printf("The memory pool is valid\n");
// 	}else
// 		printf("The memory pool has been corrupted\n");

}

void mpEnableSuperFreeDebugging()
{
	mpSuperFreeDebugging = 1;
}


static void verifyFreelistCallback(MemoryPool mempool, void *userData)
{
	int *verify_freelists_ret = userData;
	int ret = mpVerifyFreelist(mempool);

	if (!ret) {
		memlog_printf(NULL, "MemPool %s has corrupt freelist.\n", mempool->name);
		OutputDebugStringf("MemPool %s has corrupt freelist.\n", mempool->name);
	}
	(*verify_freelists_ret) &= ret;
}

int mpVerifyAllFreelists(void)
{
	int verify_freelists_ret = 1;
	mpForEachMemoryPool(verifyFreelistCallback, &verify_freelists_ret);
	return verify_freelists_ret;
}

void mpForEachAllocation(MemoryPool pool, MemoryPoolForEachAllocationFunc func, void *userData)
{
	// CANNOT do small ( < 1K ) memory allocations while in this function!
	// Walk through the chunk table
	int iChunk;
	if (!pool)
		return;
	for (iChunk=0; iChunk < eaSize(&pool->chunkList); iChunk++)
	{
		MemoryPoolChunk *chunk = pool->chunkList[iChunk];
		U32 iAlloc;
		for (iAlloc=0; iAlloc < pool->structCount; iAlloc++)
		{
			if (TSTB(chunk->inUseMap, iAlloc)) {
				void *data = (char*)chunk->data + pool->structSize * iAlloc;
				func(pool, data, userData);
			}
		}
	}
}

MemoryPoolAllocState mpGetMemoryState(MemoryPool pool, const void *mem)
{
	int iChunk;
	
	if (!pool)
		return MPAS_UNOWNED;

	for (iChunk=0; iChunk < eaSize(&pool->chunkList); iChunk++)
	{
		MemoryPoolChunk *chunk = pool->chunkList[iChunk];
		U32 iAlloc;
		for (iAlloc=0; iAlloc < pool->structCount; iAlloc++)
		{
			void *data = (char*)chunk->data + pool->structSize * iAlloc;
			if (data == mem)
				return (TSTB(chunk->inUseMap, iAlloc)) ? MPAS_ALLOCATED : MPAS_FREE;
		}
	}

	return MPAS_UNOWNED;
}