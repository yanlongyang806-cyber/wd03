#include "BlockEarray.h"
#include "SharedMemory.h"
#include "textParser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define BEA_COOKIE 0xabcd

//size of newly created blockEarrays
#define BEA_STARTING_CAPACITY 2

//when something is pushed into a zero-size blockearray, it becomes this size
#define BEA_MIN_CAPACITY 2

typedef struct BlockEarrayHeader
{
	U32 iCount;
	U32 iCapacity;
	U32 iBlockSize;
	U16 iCookie;
	U16 iFlags;
} BlockEarrayHeader;

static __forceinline void *beaGetNthBlock(BlockEarrayHeader *pHeader, U32 n)
{
	return ((char*)pHeader) + sizeof(BlockEarrayHeader) + n * pHeader->iBlockSize;
}

static __forceinline void beaPointHandleToHeader(void **ppHandle, BlockEarrayHeader *pHeader)
{
	*ppHandle = ((char*)pHeader) + sizeof(BlockEarrayHeader);
}

static __forceinline void *beaAllocInternal(int iSize, enumBeaFlags iFlags, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS)
{
	if (memAllocator)
	{
		void *pRetVal = memAllocator(customData, iSize);
		if (!(iFlags & BEAFLAG_NO_ZERO))
			memset(pRetVal, 0, iSize);
		return pRetVal;
	}

	return scalloc(iSize, 1);
}

#define ALLOC(size) beaAllocInternal(size, iFlags, memAllocator, customData MEM_DBG_PARMS_CALL)

#define GET_HEADER_TPI() beaGetAndCheckHeaderForHandle(ppHandle, iBlockSize, iFlags, pTPI, __FUNCTION__, BEA_STARTING_CAPACITY, memAllocator, customData MEM_DBG_PARMS_CALL)
#define GET_HEADER_TPI_READONLY() beaGetAndCheckHeaderForHandle(ppHandle, iBlockSize, 0, pTPI, __FUNCTION__, BEA_STARTING_CAPACITY, NULL, NULL MEM_DBG_PARMS_INIT)
#define GET_HEADER_SIMPLE() beaGetHeaderForHandle(ppHandle, __FUNCTION__)
#define GET_HEADER_TPI_WITH_CAPACITY(iStartingCapacity) beaGetAndCheckHeaderForHandle(ppHandle, iBlockSize, iFlags, pTPI, __FUNCTION__, iStartingCapacity, memAllocator, customData MEM_DBG_PARMS_CALL)

static __forceinline BlockEarrayHeader *beaGetAndCheckHeaderForHandle(void **ppHandle, U32 iBlockSize, enumBeaFlags iFlags, ParseTable *pTPI, char *pFunction, int iStartingCapacity, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS)
{
	BlockEarrayHeader *pHeader;
	assertmsgf(ppHandle, "NULL pointer passed in as handle pointer to %s", pFunction );
	assertmsgf(iBlockSize, "zero blocksize passed in to %s", pFunction );

	if (pTPI)
	{
		assertmsgf(ParserGetTableSize(pTPI) == (int)iBlockSize, "In %s, mismatch between TPI size (%d) and passed in size (%d)",
			pFunction, ParserGetTableSize(pTPI), iBlockSize);
	}

	if (!(*ppHandle))
	{
		pHeader = ALLOC(sizeof(BlockEarrayHeader) + iBlockSize * iStartingCapacity);
		pHeader->iCapacity = iStartingCapacity;
		pHeader->iCookie = BEA_COOKIE;
		pHeader->iBlockSize = iBlockSize;
		pHeader->iFlags = iFlags;
		pHeader->iCount = 0;

		beaPointHandleToHeader(ppHandle, pHeader);


		return pHeader;
	}

	pHeader = (BlockEarrayHeader*)(((char*)(*ppHandle)) - sizeof(BlockEarrayHeader));
	assertmsgf(pHeader->iCookie == BEA_COOKIE, "Bad BlockEarray cookie in %s", pFunction);
	assertmsgf(pHeader->iBlockSize == iBlockSize, "Mismatched BlockEarray blocksize in %s... %d passed in, %d saved",
		pFunction, iBlockSize, pHeader->iBlockSize);

	return pHeader;
}


static __forceinline BlockEarrayHeader *beaGetHeaderForHandle(const void * const *ppHandle, char *pFunction)
{
	assertmsgf(ppHandle, "NULL pointer passed in as handle pointer to %s", pFunction );

	if (!(*ppHandle))
	{
		return NULL;
	}

	return (BlockEarrayHeader*)(((char*)(*ppHandle)) - sizeof(BlockEarrayHeader));
}




//returns new header pointer, also resets ppHandle
static BlockEarrayHeader *beaChangeCapacity(void **ppHandle, BlockEarrayHeader *pOldHeader, U32 iNewCapacity, ParseTable *pTPI, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS)
{
	enumBeaFlags iFlags = pOldHeader->iFlags;
	BlockEarrayHeader *pNewHeader = ALLOC(sizeof(BlockEarrayHeader) + pOldHeader->iBlockSize * iNewCapacity);
	U32 i;

	pNewHeader->iBlockSize = pOldHeader->iBlockSize;
	pNewHeader->iCapacity = iNewCapacity;
	pNewHeader->iCookie = BEA_COOKIE;
	pNewHeader->iFlags = iFlags;

	pNewHeader->iCount = MIN(pOldHeader->iCount, iNewCapacity);

	for (i=0; i < pNewHeader->iCount; i++)
	{
		void *pOld = beaGetNthBlock(pOldHeader, i);
		void *pNew = beaGetNthBlock(pNewHeader, i);

		if (pTPI)
		{
			StructInitVoid(pTPI, pNew);
			StructCopyAllVoid(pTPI, pOld, pNew);
			StructDeInitVoid(pTPI, pOld);
		}
		else
		{
			memcpy(pNew, pOld, pNewHeader->iBlockSize);
		}		
	}

	if (pTPI)
	{
		for (i=pNewHeader->iCount; i < pOldHeader->iCount; i++)
		{
			void *pOld = beaGetNthBlock(pOldHeader, i);

			if (pTPI)
			{
				StructDeInitVoid(pTPI, pOld);
			}
		}
	}

	if (!isSharedMemory(pOldHeader))
		free(pOldHeader);
	beaPointHandleToHeader(ppHandle, pNewHeader);

	return pNewHeader;
}


int beaSize(const void * const *ppHandle)
{
	BlockEarrayHeader *pHeader = GET_HEADER_SIMPLE();

	if (!pHeader)
	{
		return 0;
	}
	
	return pHeader->iCount;
}

int beaCapacity(void **ppHandle)
{
	BlockEarrayHeader *pHeader = GET_HEADER_SIMPLE();

	if (!pHeader)
	{
		return 0;
	}
	
	return pHeader->iCapacity;
}


int beaMemUsage(void **ppHandle, bool bAbsoluteUsage)
{
	BlockEarrayHeader *pHeader = GET_HEADER_SIMPLE();

	if (!pHeader)
	{
		return 0;
	}
	
	if (bAbsoluteUsage)
	{
		return sizeof(BlockEarrayHeader) + pHeader->iCapacity * pHeader->iBlockSize;
	}
	else
	{
		return sizeof(BlockEarrayHeader) + pHeader->iCount * pHeader->iBlockSize;
	}

}



int beaBlockSize(void **ppHandle)
{
	BlockEarrayHeader *pHeader = GET_HEADER_SIMPLE();

	if (!pHeader)
	{
		return 0;
	}
	
	return pHeader->iBlockSize;
}

void *beaPushEmptyEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, enumBeaFlags eFlags, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS)
{
	enumBeaFlags iFlags = eFlags & 0xffff;
	BlockEarrayHeader *pHeader = GET_HEADER_TPI();
	void *pNewBlock;

	if (pHeader->iCount == pHeader->iCapacity)
	{
		U32 iNewCapacity;
		if (pHeader->iCapacity == 0)
		{
			iNewCapacity = BEA_MIN_CAPACITY;
		}
		else
		{
			iNewCapacity = pHeader->iCapacity * 2;
		}

		pHeader = beaChangeCapacity(ppHandle, pHeader, iNewCapacity, pTPI, memAllocator, customData MEM_DBG_PARMS_CALL);
	}

	pNewBlock = beaGetNthBlock(pHeader, pHeader->iCount++);
	if (pTPI && !(eFlags & BEAPUSHFLAG_NO_STRUCT_INIT))
	{
		StructInitVoid(pTPI, pNewBlock);
	}
	else
	{
		memset(pNewBlock, 0, pHeader->iBlockSize);
	}

	return pNewBlock;
}

void beaSetSizeEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, U32 iNewSize, enumBeaFlags eFlags, CustomMemoryAllocator memAllocator, void *customData, bool force MEM_DBG_PARMS)
{
	enumBeaFlags iFlags = eFlags & 0xffff;

	//special case... if handle doesn't exist and requested size is 0, do nothing
	if (!(*ppHandle) && iNewSize == 0)
	{
		return;
	}
	else
	{
		BlockEarrayHeader *pHeader = GET_HEADER_TPI_WITH_CAPACITY(iNewSize);
		U32 i;

		if (pHeader->iCount == iNewSize)
		{
			return;
		}



		if (iNewSize < pHeader->iCount)
		{	
			//don't change capacity... in TPI case, go in and deinit a bunch of things
			if (pTPI)
			{
				for (i = iNewSize; i < pHeader->iCount; i++)
				{
					void *pBlock = beaGetNthBlock(pHeader, i);
					StructDeInitVoid(pTPI, pBlock);
				}
			}

			pHeader->iCount = iNewSize;
			return;
		}

		if (iNewSize > pHeader->iCapacity)
		{
			pHeader = beaChangeCapacity(ppHandle, pHeader, iNewSize, pTPI, memAllocator, customData MEM_DBG_PARMS_CALL);
		}

		if (pTPI && !force)
		{

			for (i=pHeader->iCount; i < iNewSize; i++)
			{
				void *pBlock = beaGetNthBlock(pHeader, i);
				StructInitVoid(pTPI, pBlock);
			}
		}
		else if (!force)
		{
			void *pBlock = beaGetNthBlock(pHeader, pHeader->iCount);
			memset(pBlock, 0, pHeader->iBlockSize * (iNewSize - pHeader->iCount));
		}

		pHeader->iCount = iNewSize;
	}

}

void beaSetCapacityEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, U32 iNewCapacity, enumBeaFlags eFlags, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS)
{
	enumBeaFlags iFlags = eFlags & 0xffff;

	//special case... if handle doesn't exist and requested size is 0, do nothing
	if (!(*ppHandle) && iNewCapacity == 0)
	{
		return;
	}
	else
	{
		BlockEarrayHeader *pHeader = GET_HEADER_TPI_WITH_CAPACITY(iNewCapacity);

		if (pHeader->iCapacity == iNewCapacity)
		{
			return;
		}

		beaChangeCapacity(ppHandle, pHeader, iNewCapacity, pTPI, memAllocator, customData MEM_DBG_PARMS_CALL);
	}
}

void beaDestroyEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI)
{
	BlockEarrayHeader *pHeader;
	if (!(*ppHandle))
	{
		return;
	}

	pHeader = GET_HEADER_TPI_READONLY();

	if (pTPI)
	{
		U32 i;

		for (i=0; i < pHeader->iCount; i++)
		{
			void *pBlock = beaGetNthBlock(pHeader, i);

			StructDeInitVoid(pTPI, pBlock);
		}
	}

	free(pHeader);

	*ppHandle = NULL;
}


void beaRemoveEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, U32 iIndex)
{
	BlockEarrayHeader *pHeader = GET_HEADER_TPI_READONLY();
	U32 i;

	assertmsgf(iIndex < pHeader->iCount, "Trying to remove the %dth element from a %d-entry blockEarray",
		iIndex, pHeader->iCount);

	if (pTPI)
	{
		for (i=iIndex; i < pHeader->iCount - 1; i++)
		{
			StructCopyAllVoid(pTPI, beaGetNthBlock(pHeader, i + 1), beaGetNthBlock(pHeader, i));
		}

		StructDeInitVoid(pTPI, beaGetNthBlock(pHeader, pHeader->iCount - 1));
		pHeader->iCount--;
		return;
	}

	if (iIndex == pHeader->iCount - 1)
	{
		pHeader->iCount--;
		return;
	}

	memmove(beaGetNthBlock(pHeader, iIndex), beaGetNthBlock(pHeader, iIndex + 1), 
		(pHeader->iCount - iIndex - 1) * pHeader->iBlockSize);
	pHeader->iCount--;
}


void beaRemoveFastEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, U32 iIndex)
{
	BlockEarrayHeader *pHeader = GET_HEADER_TPI_READONLY();

	assertmsgf(iIndex < pHeader->iCount, "Trying to remove the %dth element from a %d-entry blockEarray",
		iIndex, pHeader->iCount);

	if (pTPI)
	{
		if (iIndex == pHeader->iCount - 1)
		{	
			StructDeInitVoid(pTPI, beaGetNthBlock(pHeader, iIndex));
			pHeader->iCount--;
			return;
		}

		StructCopyAllVoid(pTPI, beaGetNthBlock(pHeader, pHeader->iCount - 1), beaGetNthBlock(pHeader, iIndex));
		StructDeInitVoid(pTPI, beaGetNthBlock(pHeader, pHeader->iCount - 1));

		pHeader->iCount--;
		return;
	}

	if (iIndex == pHeader->iCount - 1)
	{
		pHeader->iCount--;
		return;
	}

	memcpy(beaGetNthBlock(pHeader, iIndex), beaGetNthBlock(pHeader, pHeader->iCount - 1), 
		pHeader->iBlockSize);
	pHeader->iCount--;
}

void* beaInsertEmptyEx(void **ppHandle, U32 iBlockSize, ParseTable *pTPI, U32 iIndex, enumBeaFlags eFlags, CustomMemoryAllocator memAllocator, void *customData MEM_DBG_PARMS)
{
	enumBeaFlags iFlags = eFlags & 0xffff;
	U32 i;
	BlockEarrayHeader *pHeader = GET_HEADER_TPI();

	assertmsgf(iIndex <= pHeader->iCount, "Trying to insert a %dth element in a %d element blockEarray",
		iIndex, pHeader->iCount);

	if (pHeader->iCount == pHeader->iCapacity)
	{
		if (pHeader->iCapacity == 0)
		{
			pHeader = beaChangeCapacity(ppHandle, pHeader, BEA_MIN_CAPACITY, pTPI, memAllocator, customData MEM_DBG_PARMS_CALL);
		}
		else
		{
			pHeader = beaChangeCapacity(ppHandle, pHeader, pHeader->iCapacity * 2, pTPI, memAllocator, customData MEM_DBG_PARMS_CALL);
		}
	}

	if (iIndex == pHeader->iCount)
	{
		void *pNewBlock = beaGetNthBlock(pHeader, iIndex);
		if (pTPI)
		{
			StructInitVoid(pTPI, pNewBlock);
		}
		else
		{
			memset(pNewBlock, 0, pHeader->iBlockSize);
		}
		pHeader->iCount++;
		return pNewBlock;
	}

	if (pTPI)
	{
		for (i=pHeader->iCount; i > iIndex; i--)
		{
			StructCopyAllVoid(pTPI, beaGetNthBlock(pHeader, i - 1), beaGetNthBlock(pHeader, i));
		}

		StructResetVoid(pTPI, beaGetNthBlock(pHeader, iIndex));
		pHeader->iCount++;
		return beaGetNthBlock(pHeader, iIndex);
	}

	memmove(beaGetNthBlock(pHeader, iIndex + 1), beaGetNthBlock(pHeader, iIndex), (pHeader->iCount - iIndex) * pHeader->iBlockSize);
	memset(beaGetNthBlock(pHeader, iIndex), 0, pHeader->iBlockSize);
	pHeader->iCount++;
	return beaGetNthBlock(pHeader, iIndex);
}

	