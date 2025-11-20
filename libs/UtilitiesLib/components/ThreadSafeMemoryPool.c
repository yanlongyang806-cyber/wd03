#include "ThreadSafeMemoryPool.h"
#include "MemoryMonitor.h"
#include "earray.h"
#include "memtrack.h"
#include "ThreadSafeMemoryPool_h_Ast.h"
#include "StashTable.h"
#include "ResourceInfo.h"
#include "UtilitiesLib.h"
#include "tokenstore.h"
#include "qsortG.h"
#include "MemAlloc.h"
#include "cmdParse.h"
#include "mathutil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#if !_XBOX

#define InterlockedPushEntrySListRelease(ListHead, ListEntry) InterlockedPushEntrySList((ListHead), (ListEntry))
#define InterlockedPopEntrySListAcquire(ListHead) InterlockedPopEntrySList(ListHead)

#endif

#define THREADSAFE_MEMORYPOOL_SENTINEL_VALUE 0xFCFCFCFC

typedef struct TSMPHeader
{
	SLIST_ENTRY slist;
	U32 sentinel;
} TSMPHeader;

static bool sbRegisteredResourceDictionary = false;

AUTO_FIXUPFUNC;
TextParserResult ThreadSafeMemoryPoolFixupFunc(ThreadSafeMemoryPool *pool, enumTextParserFixupType eFixupType, void *pExtraData)
{
	int iAllocationCount;
	switch (eFixupType)
	{
		case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
			iAllocationCount = pool->allocationCount;
			pool->iTotalBytes_ForServerMonitoring = ((size_t)pool->totalStructCount) * pool->structSize;
			pool->iUsedBytes_ForServerMonitoring = ((size_t)pool->structSize) * iAllocationCount;
			pool->iFreeBytes_ForServerMonitoring = pool->iTotalBytes_ForServerMonitoring - pool->iUsedBytes_ForServerMonitoring;
			break;
	}

	return true;
}

AUTO_COMMAND;
void SpecialHeapForTSMPs(int iSet)
{
}

static StashTable sPoolsByName;
static CRITICAL_SECTION csThreadSafeMemoryPool;
static int siTSMPHeapToUse = CRYPTIC_FIRST_HEAP;

//we don't have a "real" way to do an aligned malloc from an arbitrary heap, so do a super simple thing where we
//use the word immediately before the amount returned to indicate the size of the prefix needed to get back to the
//actual malloc that needs to be freed
static void *tsmp_malloc_internal_aligned(size_t iAmount, int iAlign, int iHeap, const char *pFileName, int iLineNum)
{
	size_t iSizeToMalloc = iAmount + iAlign;
	char *pMallocedBuffer = malloc_timed_canfail(iSizeToMalloc, iHeap, false, pFileName, iLineNum);
	char *pBufferToReturn = (char*)((((intptr_t)pMallocedBuffer) + iAlign) & ~(iAlign - 1));
	U32 iPrefixSize = pBufferToReturn - pMallocedBuffer;

	assert(iAlign > 4);
	assert(iPrefixSize >= 4);
	assert(isPower2(iAlign));

	*(((int*)pBufferToReturn) - 1) = iPrefixSize;

	return pBufferToReturn;
}

static void tsmp_free_internal(void *pBuf)
{
	int iPrefixSize = *(((int*)pBuf)-1);
	free((char*)pBuf - iPrefixSize);
}

static bool UseSpecialHeapForTSMPs_default(void)
{
	if (IsThisObjectDB() && sizeof(void*) == 8)
	{
		return true;
	}

	return false;

}

// This function makes the VITAL ASSUMPTION that structSize is a multiple of MEMORY_ALLOCATION_ALIGNMENT
// Call threadSafeMemoryPoolAdjustStructSize_internal to fix it up before calling this, or you will BREAK THE WORLD
static void threadSafeMemoryPoolInit_internal(ThreadSafeMemoryPool *pool, size_t structsPerBlock, size_t structSize, int iAlignment, const char *pName MEM_DBG_PARMS)
{
	const char *pNameToUse;
	char tempName[1024];

	ATOMIC_INIT_BEGIN;
	{
		char temp[16] = "0";
		InitializeCriticalSection(&csThreadSafeMemoryPool);
		sPoolsByName = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
		
		if (ParseCommandOutOfCommandLine("SpecialHeapForTSMPs", temp))
		{
			if (atoi(temp))
			{
				siTSMPHeapToUse = CRYPTIC_TSMP_HEAP;
			}
		}
		else
		{
			if (UseSpecialHeapForTSMPs_default())
			{
				siTSMPHeapToUse = CRYPTIC_TSMP_HEAP;
			}
		}
		
	}
	ATOMIC_INIT_END;
	
	EnterCriticalSection(&csThreadSafeMemoryPool);
	if (!sbRegisteredResourceDictionary && giCurAutoRunStep > 2)
	{
		sbRegisteredResourceDictionary = true;
		resRegisterDictionaryForStashTable("Threadsafe mem pools", RESCATEGORY_SYSTEM, 0, sPoolsByName, parse_ThreadSafeMemoryPool);
	}

	if (!pool->structSize) {
		pool->structSize = structSize;
		pool->structsPerBlock = structsPerBlock;
		pool->data_pool = NULL;
		pool->pName = strdup(pName);
		pool->alignment = iAlignment;

		MEM_DBG_STRUCT_PARMS_INIT(pool);
		InitializeSListHead(&pool->ListHead);

		if (!stashFindPointer(sPoolsByName, pName, NULL))
		{
			pNameToUse = pName;
		}
		else
		{
			int i = 0;

			do
			{
				sprintf(tempName, "%s_%d", pName, i);
				i++;
			}
			while (stashFindPointer(sPoolsByName, tempName, NULL));

			pNameToUse = tempName;
		}

		stashAddPointer(sPoolsByName, pNameToUse, pool, false);
	}
	LeaveCriticalSection(&csThreadSafeMemoryPool);
}

static size_t threadSafeMemoryPoolAdjustStructSize_internal(size_t structSize, int iAlignment)
{
	if (iAlignment)
	{
		assertmsgf(isPower2(iAlignment) && iAlignment >= MEMORY_ALLOCATION_ALIGNMENT, "Alignment for TSMP must be power of 2 >= %d", MEMORY_ALLOCATION_ALIGNMENT);
	}

	// Adjust size to be at least big enough for TSMPHeader struct
	// VITAL NOTE: InterlockedSList feature requires SList entries to be aligned on MEMORY_ALLOCATION_ALIGNMENT boundaries
	// To abide by this restriction, we force structSize to be a multiple of MEMORY_ALLOCATION_ALIGNMENT
	MAX1(structSize, sizeof(TSMPHeader));

	if (!iAlignment)
	{
		iAlignment = MEMORY_ALLOCATION_ALIGNMENT;
	}

	return (structSize + (iAlignment-1)) & ~(iAlignment-1);
}

void threadSafeMemoryPoolInit_dbg(ThreadSafeMemoryPool *pool, size_t structsPerBlock, size_t structSize, int iAlignment, const char *pName MEM_DBG_PARMS)
{
	structSize = threadSafeMemoryPoolAdjustStructSize_internal(structSize, iAlignment);
	threadSafeMemoryPoolInit_internal(pool, structsPerBlock, structSize, iAlignment, pName MEM_DBG_PARMS_CALL);
}

void threadSafeMemoryPoolInitTrack_dbg(ThreadSafeMemoryPool *pool, size_t structsPerBlock, size_t structSize, int iAlignment, const char *pName, const char *pObj)
{
	threadSafeMemoryPoolInit_dbg(pool, structsPerBlock, structSize,	iAlignment, pName, pObj, LINENUM_FOR_TS_POOLED_STRUCTS);
}

void threadSafeMemoryPoolInitSize_dbg(ThreadSafeMemoryPool *pool, size_t structChunkSize, size_t structSize, int iAlignment, const char *pName MEM_DBG_PARMS)
{
	size_t structsPerBlock = 0;
	structSize = threadSafeMemoryPoolAdjustStructSize_internal(structSize, iAlignment);
	structsPerBlock = (structChunkSize + structSize - 1) / structSize;
	threadSafeMemoryPoolInit_internal(pool, structsPerBlock, structSize, iAlignment, pName MEM_DBG_PARMS_CALL);
}

void threadSafeMemoryPoolInitSizeTrack_dbg(ThreadSafeMemoryPool *pool, size_t structChunkSize, size_t structSize, int iAlignment, const char *pName, const char *pObj)
{
	threadSafeMemoryPoolInitSize_dbg(pool, structChunkSize, structSize, iAlignment, pName, pObj, LINENUM_FOR_TS_POOLED_STRUCTS);
}

void threadSafeMemoryPoolSetNoEarrays(ThreadSafeMemoryPool *pool, bool bSet)
{
	if (bSet)
	{
		pool->eFlags |= TSMPFLAG_NO_INTERNAL_EARRAYS;
	}
	else
	{
		pool->eFlags &= ~TSMPFLAG_NO_INTERNAL_EARRAYS;
	}
}


static void threadSafeMemoryPoolAddMoreInternal(ThreadSafeMemoryPool *pool, void *data)
{
	U32 i;
	if (!(pool->eFlags & TSMPFLAG_NO_INTERNAL_EARRAYS))
	{
		steaPush(&pool->data_pool, data, pool);
	}

	for (i=0; i<pool->structsPerBlock; i++) {
		void *newChunk = (char*)data + pool->structSize * i;
		((TSMPHeader*)newChunk)->sentinel = THREADSAFE_MEMORYPOOL_SENTINEL_VALUE;
		InterlockedPushEntrySListRelease(&pool->ListHead, newChunk);
	}
}

static void threadSafeMemoryPoolAddMore(ThreadSafeMemoryPool *pool)
{
	void *data;
	EnterCriticalSection(&csThreadSafeMemoryPool);
	if (data = InterlockedPopEntrySListAcquire(&pool->ListHead))
	{
		// It's no longer empty, another thread must have added something, or compaction was going on
		InterlockedPushEntrySListRelease(&pool->ListHead, data);
	} else {
		// Add another set from the memory pool to this list

		data = tsmp_malloc_internal_aligned(pool->structsPerBlock * pool->structSize,MAX(16, pool->alignment),siTSMPHeapToUse,pool->caller_fname,pool->line);

		threadSafeMemoryPoolAddMoreInternal(pool, data);
		pool->totalStructCount += (int)(pool->structsPerBlock);
	}
	LeaveCriticalSection(&csThreadSafeMemoryPool);
}

void *threadSafeMemoryPoolAlloc(ThreadSafeMemoryPool *pool)
{
	void *data;

	while (!(data = InterlockedPopEntrySListAcquire(&pool->ListHead))) {
		threadSafeMemoryPoolAddMore(pool);
	}
	assertmsg(((TSMPHeader*)data)->sentinel == THREADSAFE_MEMORYPOOL_SENTINEL_VALUE,
		"Memory was modified after it was freed");
	((TSMPHeader*)data)->sentinel = 0;
	InterlockedIncrement(&pool->allocationCount);
	return data;
}

void *threadSafeMemoryPoolCalloc(ThreadSafeMemoryPool *pool)
{
	void *pData = threadSafeMemoryPoolAlloc(pool);
	memset(pData, 0, pool->structSize);
	return pData;
}


void threadSafeMemoryPoolCompact(ThreadSafeMemoryPool *pool)
{
	U32 iMaxExpectedStructs = 0;
	U32 iStructsFound = 0;
	void **ppAllStructsEarray = NULL;
	SLIST_ENTRY *pEntry;
	U32 iStructIndex = 0;
	U32 iBlockIndex = 0;
	char *pCurBlock;
	U32 iStartingNumBlocks;
	U32 iNumBlocksFreed = 0;
	size_t i;
	void **ppDataPoolCopy;
	void ***pppListsOfStructsPerBlock;

	// Releases elements back to the memory pool, compacts it
	if (!pool->data_pool)
		return;

	EnterCriticalSection(&csThreadSafeMemoryPool);

	//if there's only one (or zero) blocks, don't bother compacting
	if (eaSize(&pool->data_pool) <= 1)
	{
		LeaveCriticalSection(&csThreadSafeMemoryPool);
		return;
	}

	if (pool->eFlags & TSMPFLAG_COMPACTING)
	{
		LeaveCriticalSection(&csThreadSafeMemoryPool);
		return;
	}

	pool->eFlags |= TSMPFLAG_COMPACTING;
	ppDataPoolCopy = pool->data_pool;
	pool->data_pool = NULL;

	LeaveCriticalSection(&csThreadSafeMemoryPool);


	iStartingNumBlocks = eaSize(&ppDataPoolCopy);


	while ((pEntry = InterlockedPopEntrySListAcquire(&pool->ListHead)))
	{
		eaPush(&ppAllStructsEarray, pEntry);
	}

	iStructsFound = eaSize(&ppAllStructsEarray);

	if (!iStructsFound)
	{

		EnterCriticalSection(&csThreadSafeMemoryPool);
		eaPushEArray(&pool->data_pool, &ppDataPoolCopy);
		pool->eFlags &= ~TSMPFLAG_COMPACTING;
		LeaveCriticalSection(&csThreadSafeMemoryPool);

		eaDestroy(&ppAllStructsEarray);
		eaDestroy(&ppDataPoolCopy);
		return;
	}

//	printf("Beginning compaction... found %d unallocated structs, mempool thinks there are %d unallocated\n",
//		iStructsFound, pool->totalStructCount - pool->allocationCount);

	//we assemble an earray of every struct in every block as we go
	pppListsOfStructsPerBlock = calloc(sizeof(void**) * iStartingNumBlocks, 1);

	qsort(ppDataPoolCopy, iStartingNumBlocks, sizeof(void*), ptrCmp);
	qsort(ppAllStructsEarray, iStructsFound, sizeof(void*), ptrCmp);

	//list of blocks is qsorted, list of structs is qsorted, so walk through list of structs, for each one increment
	//the count for the block it's in

	pCurBlock = ppDataPoolCopy[iBlockIndex];

	iStructIndex = 0;

	while(iStructIndex < iStructsFound)
	{
		char *pCurStruct = ppAllStructsEarray[iStructIndex];
		if (pCurStruct < pCurBlock)
		{
			//this struct was not in any of our blocks, but someone else may have added
			//a block while we were already compacting, so we just push this back into the list
			InterlockedPushEntrySListRelease(&pool->ListHead, (void*)pCurStruct);
			iStructIndex++;
			continue;
		}

		if (!pCurBlock)
		{
			ANALYSIS_ASSUME(pCurStruct != NULL); // this is iffy
			//the other case where the struct was not in any of our blocks, because it was off the far end
			InterlockedPushEntrySListRelease(&pool->ListHead, (void*)pCurStruct);
			iStructIndex++;
			continue;
		}

		//if we are in the current block, add ourself to its list
		if (pCurStruct < pCurBlock + pool->structSize * pool->structsPerBlock)
		{
			eaPush(&(pppListsOfStructsPerBlock[iBlockIndex]), pCurStruct);
			iStructIndex++;
			continue;
		}

		//our struct is past the end of our current block... so increment our block counter
		iBlockIndex++;
		if (iBlockIndex < iStartingNumBlocks)
		{
			pCurBlock = ppDataPoolCopy[iBlockIndex];
		}
		else
		{
			pCurBlock = NULL;
		}
	}

	//now every struct has ended up one of two places... either pushed back into pool->ListHead, or 
	//pushed into one of pppListsOfStructsPerBlock
	for (iBlockIndex = 0; iBlockIndex < iStartingNumBlocks; iBlockIndex++)
	{
		size_t iStructCountThisBlock = eaSize(&(pppListsOfStructsPerBlock[iBlockIndex]));
		if (iStructCountThisBlock < pool->structsPerBlock || iNumBlocksFreed == iStartingNumBlocks - 1)
		{
			for (i = 0; i < iStructCountThisBlock; i++)
			{
				//return all the structs to the pool
				InterlockedPushEntrySListRelease(&pool->ListHead, (pppListsOfStructsPerBlock[iBlockIndex])[i]);
			}
		}
		else
		{
			//free the block
			tsmp_free_internal(ppDataPoolCopy[iBlockIndex]);
			iNumBlocksFreed++;
			ppDataPoolCopy[iBlockIndex] = 0;
		}

		eaDestroy(&(pppListsOfStructsPerBlock[iBlockIndex]));

	}


	//now we have finished processing all structs and blocks, need to go back into our critical section
	//and rebuild pool->data_pool
	EnterCriticalSection(&csThreadSafeMemoryPool);

	//now fix up pool->data_pool
	for (i = 0; i < iStartingNumBlocks; i++)
	{
		if (ppDataPoolCopy[i])
		{
			eaPush(&pool->data_pool, ppDataPoolCopy[i]);
		}
	}

	pool->totalStructCount -= iNumBlocksFreed * (int)pool->structsPerBlock;

	pool->eFlags &= ~TSMPFLAG_COMPACTING;
	LeaveCriticalSection(&csThreadSafeMemoryPool);

	free(pppListsOfStructsPerBlock);
	eaDestroy(&ppAllStructsEarray);
	eaDestroy(&ppDataPoolCopy);
}


void threadSafeMemoryPoolFree(ThreadSafeMemoryPool *pool, void *data)
{
	assertmsg(((TSMPHeader*)data)->sentinel != THREADSAFE_MEMORYPOOL_SENTINEL_VALUE,
		"Freeing memory which has already been freed");
	((TSMPHeader*)data)->sentinel = THREADSAFE_MEMORYPOOL_SENTINEL_VALUE;
	InterlockedPushEntrySListRelease(&pool->ListHead, data);
	InterlockedDecrement(&pool->allocationCount);

}

void threadSafeMemoryPoolDeinit(ThreadSafeMemoryPool *pool)
{
	//if anyone starts using this, need to fix up the stashtable stuff, the strduped pname, etc
	assert(0);


	EnterCriticalSection(&csThreadSafeMemoryPool);
	eaDestroyEx(&pool->data_pool, 0);
	pool->structSize = 0;
	LeaveCriticalSection(&csThreadSafeMemoryPool);
}

void threadSafeMemoryPoolForEachAllocationUNSAFE(ThreadSafeMemoryPool *pool, ThreadSafeMemoryPoolForEachAllocationFunc func, void *userData)
{
	int i;
	unsigned int j;
	for (i=eaSize(&pool->data_pool)-1; i>=0; i--)
	{
		TSMPHeader *data = pool->data_pool[i];
		for (j=0; j<pool->structsPerBlock; j++)
		{
			if (data->sentinel == THREADSAFE_MEMORYPOOL_SENTINEL_VALUE)
			{
				// Free
			} else {
				func(pool, data, userData);
			}
			data = (TSMPHeader*)(((U8*)data) + pool->structSize);
		}
	}
}


/*
// Test function
AUTO_COMMAND ACMD_COMMANDLINE;
void threadSafeMemoryPoolTest(void)
{
	ThreadSafeMemoryPool p = {0};
	int *d[4];
	threadSafeMemoryPoolInit(&p, 2, 8);

	d[0] = threadSafeMemoryPoolAlloc(&p);
	d[1] = threadSafeMemoryPoolAlloc(&p);
	d[2] = threadSafeMemoryPoolAlloc(&p); // Should grow
	threadSafeMemoryPoolFree(&p, d[0]);
	d[3] = threadSafeMemoryPoolAlloc(&p);
	assert(d[3] == d[0]);
	threadSafeMemoryPoolFree(&p, d[3]);
	threadSafeMemoryPoolFree(&p, d[3]); // Should assert freeing twice

	threadSafeMemoryPoolFree(&p, d[2]);
	(d[2])[1] = 0;
	d[2] = threadSafeMemoryPoolAlloc(&p); // Should assert modified after freed

	threadSafeMemoryPoolDeinit(&p);
}
*/
/*
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
AUTO_COMMAND ACMD_COMMANDLINE;
void memPoolSpeedTest(int dummy)
{
	int i, k;
	int t = timerAlloc();
	typedef struct TestStruct {
		int a, b, c, d, e, f, g, h;
	} TestStruct;
	int doit[256];
	TestStruct *buf[512];
	int c=0;
	MP_DEFINE(TestStruct);
	TSMP_DEFINE(TestStruct);
	MP_CREATE(TestStruct, 256);
	TSMP_CREATE(TestStruct, 256);
	for (i=0; i<255; i++) {
		int v = randInt(256);
		if (v < c) {
			doit[i] = -v;
			c -= v;
		} else {
			doit[i] = v;
			c += v;
		}
	}
	doit[255] = -c;
	c = 0;
	for (k=0; k<4; k++) 
	{
		timerStart(t);
		for (i=0; i<2560000; i++) {
			int j;
			for (j=0; j<ABS(doit[i&0xff]); j++) {
				if (doit[i&0xff]<0) {
					c--;
					assert(c >= 0);
					TSMP_FREE(TestStruct, buf[c]);
				} else {
					assert(c < ARRAY_SIZE(buf));
					buf[c++] = TSMP_ALLOC(TestStruct);
				}
			}
		}
		assert(c==0);
		printf("TSMP: %1.3f\n", timerElapsed(t));
		timerStart(t);
		for (i=0; i<2560000; i++) {
			int j;
			for (j=0; j<ABS(doit[i&0xff]); j++) {
				if (doit[i&0xff]<0) {
					assert(c >= 0);
					c--;
					MP_FREE(TestStruct, buf[c]);
				} else {
					assert(c < ARRAY_SIZE(buf));
					buf[c++] = MP_ALLOC(TestStruct);
				}
			}
		}
		assert(c==0);
		printf("MP: %1.3f\n", timerElapsed(t));
	}
	printf("");
}
*/


#include "ThreadSafeMemoryPool_h_Ast.c"
