
#include "PoolQueue.h"

#include "MemoryPool.h"
#include "error.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););



MP_DEFINE(PoolQueue);

void poolQueueInit(PoolQueue* pQueue, int iElementSize, int iMaxElements, int iAlignmentBytes)
{
	if (iElementSize <= 0 || iMaxElements <= 0)
	{
		FatalErrorf("Both elementsize and max elements must be positive!");
	}

	assert(!pQueue->pStorage);

	pQueue->iElementSize = iElementSize;
	pQueue->iMaxElements = iMaxElements;
	pQueue->iAlignmentBytes = iAlignmentBytes;
	pQueue->pStorage = iAlignmentBytes?aligned_calloc(iElementSize, iMaxElements, iAlignmentBytes):calloc(iElementSize, iMaxElements);
	pQueue->iHead = pQueue->iTail = 0;
}


PoolQueue* poolQueueCreate(void)
{
	PoolQueue* pNewQueue;

	MP_CREATE(PoolQueue, 128);
	pNewQueue = MP_ALLOC(PoolQueue);

	return pNewQueue;
}

void poolQueueGrow(PoolQueue* pQueue, int iMaxElements)
{
	assert(iMaxElements > pQueue->iMaxElements);
	{
		char* pNewStorage = pQueue->iAlignmentBytes?aligned_calloc(pQueue->iElementSize, iMaxElements, pQueue->iAlignmentBytes):calloc(pQueue->iElementSize, iMaxElements);
		// copy over storage 
		if (pQueue->iHead < pQueue->iTail) // nothing needs to be done except a straight memcopy
		{
			memcpy(pNewStorage, pQueue->pStorage + pQueue->iHead * pQueue->iElementSize, pQueue->iNumElements * pQueue->iElementSize);
		}
		else
		{
			memcpy(pNewStorage, pQueue->pStorage + pQueue->iHead * pQueue->iElementSize, (pQueue->iMaxElements - pQueue->iHead) * pQueue->iElementSize);
			memcpy(pNewStorage + ((pQueue->iMaxElements - pQueue->iHead) * pQueue->iElementSize), pQueue->pStorage, pQueue->iTail * pQueue->iElementSize);
		}

		// change it up
		pQueue->iTail = pQueue->iNumElements;
		pQueue->iHead = 0;
		pQueue->iMaxElements = iMaxElements;
		pQueue->iAlignmentBytes?aligned_free(pQueue->pStorage):free(pQueue->pStorage);
		pQueue->pStorage = pNewStorage;
	}
}

void poolQueueDeinit(PoolQueue* pQueue)
{
	assert(pQueue->pStorage);
	pQueue->iAlignmentBytes?aligned_free(pQueue->pStorage):free(pQueue->pStorage);
	pQueue->pStorage = NULL;
	poolQueueClear(pQueue);
}

void poolQueueDestroy(PoolQueue* pQueue)
{
	poolQueueDeinit(pQueue);
	MP_FREE(PoolQueue, pQueue);
}

void poolQueueClear(PoolQueue* pQueue)
{
	pQueue->iNumElements = pQueue->iHead = pQueue->iTail = 0;
}

bool poolQueueIsFull(PoolQueue* pQueue)
{
	return (pQueue->iNumElements == pQueue->iMaxElements);
}

bool poolQueueEnqueue(PoolQueue* pQueue, void* pElement)
{
	if (poolQueueIsFull(pQueue))
		return false;
	memcpy(pQueue->pStorage + pQueue->iTail * pQueue->iElementSize, pElement, pQueue->iElementSize);

	++pQueue->iTail;
	if (pQueue->iTail == pQueue->iMaxElements)
		pQueue->iTail = 0;

	++pQueue->iNumElements;
	return true;
}

// This function, rather than passing in a pointer and having it memcpy the results, will just return to you the pointer to the newly allocated element
void* poolQueuePreEnqueue(PoolQueue* pQueue)
{
	void* pNew;
	if (poolQueueIsFull(pQueue))
		return NULL;
	pNew = pQueue->pStorage + pQueue->iTail * pQueue->iElementSize;

	++pQueue->iTail;
	if (pQueue->iTail == pQueue->iMaxElements)
		pQueue->iTail = 0;

	++pQueue->iNumElements;
	return pNew;
}

bool poolQueueDequeue(PoolQueue* pQueue, void** ppElementTarget)
{
	bool bCopied = false;
	if (ppElementTarget && pQueue->iNumElements > 0)
	{
		memcpy(*ppElementTarget, pQueue->pStorage + pQueue->iHead * pQueue->iElementSize, pQueue->iElementSize);
		bCopied = true;
	}

	++pQueue->iHead;
	if (pQueue->iHead == pQueue->iMaxElements)
		pQueue->iHead = 0;

	--pQueue->iNumElements;
	return bCopied;
}

bool poolQueuePeek(PoolQueue* pQueue, const void** ppElementTarget)
{
	if (ppElementTarget && pQueue->iNumElements > 0)
	{
		*ppElementTarget = pQueue->pStorage + pQueue->iHead * pQueue->iElementSize;
		return true;
	}
	return false;
}

bool poolQueuePeekTail(PoolQueue* pQueue, const void** ppElementTarget)
{
	if (ppElementTarget && pQueue->iNumElements > 0)
	{
		int iIndex = (pQueue->iTail == 0)?pQueue->iMaxElements-1:pQueue->iTail -1;
		*ppElementTarget = pQueue->pStorage + iIndex * pQueue->iElementSize;
		return true;
	}
	return false;
}

int poolQueueGetNumElements( SA_PARAM_NN_VALID PoolQueue* pQueue )
{
	return pQueue->iNumElements;
}

int poolQueueGetElementSize( SA_PARAM_NN_VALID PoolQueue* pQueue )
{
	return pQueue->iElementSize;
}

void poolQueueGetIterator( PoolQueue* pQueue, PoolQueueIterator* pIter )
{
	pIter->pQueue = pQueue;
	if (pQueue->iNumElements == 0)
		pIter->iIndex = -1;
	else
		pIter->iIndex = pQueue->iHead;
	pIter->bBackwards = false;
}

void poolQueueGetBackwardsIterator( PoolQueue* pQueue, PoolQueueIterator* pIter )
{
	pIter->pQueue = pQueue;
	if (pQueue->iNumElements == 0)
		pIter->iIndex = -1;
	else if (pQueue->iTail == 0)
		pIter->iIndex = pQueue->iMaxElements-1;
	else
		pIter->iIndex = pQueue->iTail-1;
	pIter->bBackwards = true;
}

bool poolQueueGetNextElement( PoolQueueIterator* pIter, void** ppElem )
{
	PoolQueue* pQueue = pIter->pQueue;
	int iIndex = pIter->iIndex;
	// Negative iIndex means we've finished
	if (iIndex < 0)
		return false; 
	if (ppElem)
		*ppElem = pQueue->pStorage + iIndex * pQueue->iElementSize;

	if ( pIter->bBackwards )
	{
		int iBeforeHead = (pQueue->iHead-1 >= 0)?pQueue->iHead-1:pQueue->iMaxElements-1;

		// increment index mod queue size
		if ( --iIndex < 0 )
		{
			iIndex = pQueue->iMaxElements-1;
		}

		// Once we reach the tail, we're done (next time we return false)
		if ( iIndex == iBeforeHead )
		{
			iIndex = -1;
		}
	}
	else
	{
		// increment index mod queue size
		if ( ++iIndex == pQueue->iMaxElements )
			iIndex = 0;

		// Once we reach the tail, we're done (next time we return false)
		if ( iIndex == pQueue->iTail )
		{
			iIndex = -1;
		}
	}


	// To avoid LHS, do this at the end
	pIter->iIndex = iIndex;
	return true;
}