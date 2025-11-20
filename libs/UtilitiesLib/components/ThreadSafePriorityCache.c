#include "ThreadSafePriorityCache.h"

#include "ScratchStack.h"
#include "wininclude.h"
#include "error.h"
#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

typedef struct QElement
{
	const void* pKey;
	const void* pValue;
} QElement;

typedef struct ThreadSafePriorityCache
{
	StashTable stCache;
	QElement* pTempQueue;
	int iMaxObjects;
	int iMaxQueueSize;
	volatile int iCurrentQueueSize;
	ThreadSafePriorityCompareFunc compFunc;
	Destructor keyDestructor;
	Destructor valueDestructor;
	ParseTable* pDebugParseTable;
	CRITICAL_SECTION criticalSection;
} ThreadSafePriorityCache;

ThreadSafePriorityCache* tspCacheCreateEx(int iMaxObjects, int iMaxQueueSize, StashTableMode eMode, StashKeyType eKeyType, U32 uiKeyLength, ThreadSafePriorityCompareFunc compFunc, Destructor keyDestructor, Destructor valueDestructor, ParseTable* pDebugParseTable MEM_DBG_PARMS)
{
	ThreadSafePriorityCache* pCache = scalloc(sizeof(*pCache) + sizeof(QElement) * iMaxQueueSize, 1);
	assert(compFunc);
	pCache->compFunc = compFunc;
	pCache->keyDestructor = keyDestructor;
	pCache->valueDestructor = valueDestructor;
	pCache->iMaxQueueSize = iMaxQueueSize;
	pCache->iMaxObjects = iMaxObjects;
	pCache->pDebugParseTable = pDebugParseTable;
	assert(pCache->iMaxQueueSize <= pCache->iMaxObjects);
	assert(pCache->iMaxObjects > 0 && pCache->iMaxQueueSize > 0);

	InitializeCriticalSection(&pCache->criticalSection);

	pCache->stCache = stashTableCreateEx(iMaxObjects, eMode, eKeyType, uiKeyLength MEM_DBG_PARMS_CALL);
	stashTableSetThreadSafeLookups(pCache->stCache, true);
	pCache->pTempQueue = (QElement*)(((char*)pCache) + sizeof(*pCache));
	return pCache;
}

const void* tspCacheFind(ThreadSafePriorityCache* pCache, const void* pKey)
{
	// Try the stash table cache
	{
		void* pResult = NULL;
		if (stashFindPointer(pCache->stCache, pKey, &pResult))
			return pResult;
	}

	// Try the current queue list
	{
		int i;
		for (i=0; i<pCache->iCurrentQueueSize; ++i)
		{
			if (stashKeysAreEqual(pCache->stCache, pKey, pCache->pTempQueue[i].pKey))
				return pCache->pTempQueue[i].pValue;
		}
	}

	// No luck
	return NULL;
}

const void* tspCacheFindQueue(ThreadSafePriorityCache* pCache, const void* pKey)
{
	// Try the current queue list
	{
		int i;
		for (i=0; i<pCache->iCurrentQueueSize; ++i)
		{
			if (stashKeysAreEqual(pCache->stCache, pKey, pCache->pTempQueue[i].pKey))
				return pCache->pTempQueue[i].pValue;
		}
	}

	// No luck
	return NULL;
}

void tspCacheLock(ThreadSafePriorityCache* pCache)
{
    EnterCriticalSection(&pCache->criticalSection);
}

void tspCacheUnlock(ThreadSafePriorityCache* pCache)
{
    LeaveCriticalSection(&pCache->criticalSection);
}

bool tspCacheAdd(ThreadSafePriorityCache* pCache, const void* pKey, const void* pValue)
{
	if (pCache->iCurrentQueueSize == pCache->iMaxQueueSize)
		return false;
	EnterCriticalSection(&pCache->criticalSection);
	// Make sure it's still not in the temp queue
	if (pCache->iCurrentQueueSize == pCache->iMaxQueueSize)
	{
		LeaveCriticalSection(&pCache->criticalSection);
		return false;
	}

	{
		int i;
		for (i=0; i<pCache->iCurrentQueueSize; ++i)
		{
			if (stashKeysAreEqual(pCache->stCache, pKey, pCache->pTempQueue[i].pKey))
			{
				// Already found!
				LeaveCriticalSection(&pCache->criticalSection);
				return false;
			}
		}
	}

	// Ok, there is room and we need to add it
	{
		QElement* pElem = &pCache->pTempQueue[pCache->iCurrentQueueSize];
		pElem->pKey = pKey;
		pElem->pValue = pValue;
	}

	// Must be done last so that nothing tries to use the new element before it's done being written
	// Interlocked release is to create a memory barrier so that the new array element is fully written before the number is incremented
	InterlockedIncrementRelease(&pCache->iCurrentQueueSize);
	LeaveCriticalSection(&pCache->criticalSection);
	return true;
}

ThreadSafePriorityCompareFunc currentCompFunc = NULL;
int ToCutElementCompare(const QElement* pElemA, const QElement* pElemB)
{
	return currentCompFunc(pElemA->pValue, pElemB->pValue);
}

#include "qsortG.h"
void tspCacheUpdate(ThreadSafePriorityCache* pCache)
{
	// First, figure out how many we have to cut in order to stay under our max size
	int iNumToCut = stashGetCount(pCache->stCache) + pCache->iCurrentQueueSize - pCache->iMaxObjects;
	if (iNumToCut > 0)
	{
		// We need to make a list of stash elements to cut
		int iNumElems = 0;
		QElement* pElementsToCut = ScratchAlloc(sizeof(StashElement) * (iNumToCut+1));
		StashTableIterator iter;
		StashElement elem;
		stashGetIterator(pCache->stCache, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			if (iNumElems < iNumToCut)
			{
				pElementsToCut[iNumElems].pKey = stashElementGetKey(elem);
				pElementsToCut[iNumElems].pValue = stashElementGetPointer(elem);
				++iNumElems;
			}
			else
			{
				// Copy the current element into the last slot and then sort
				pElementsToCut[iNumToCut].pKey = stashElementGetKey(elem);
				pElementsToCut[iNumToCut].pValue = stashElementGetPointer(elem);


				// Sort are list of elements to cut, so we can compare the worst element
				// Sort them from lowest priority to highest priority
				currentCompFunc = pCache->compFunc;
				qsortG(pElementsToCut, iNumToCut+1, sizeof(QElement), ToCutElementCompare);
			}
		}

		// Now cut all on our list from the stash table
		{
			int i;
			for (i=0; i<iNumElems; ++i)
			{
				stashRemovePointer(pCache->stCache, pElementsToCut[i].pKey, NULL);
				if (pCache->keyDestructor)
					pCache->keyDestructor((void*)pElementsToCut[i].pKey);
				if (pCache->valueDestructor)
					pCache->valueDestructor((void*)pElementsToCut[i].pValue);
			}
		}
		ScratchFree(pElementsToCut);
	}


	// Add all in queue
	{
		int i;
		for (i=0; i<pCache->iCurrentQueueSize; ++i)
		{
			if (!stashAddPointer(pCache->stCache, pCache->pTempQueue[i].pKey, pCache->pTempQueue[i].pValue, false))
			{
				// This key is already in the stash table. Log some debugging information to track down how this happened.
				// First, find out if it was previously in the queue
				bool bFound = false;
				int j;
				void* pOther = NULL;
				char* esDetails = NULL;
				estrCreate(&esDetails);
				for (j=0; j<i; ++j)
				{
					if (stashKeysAreEqual(pCache->stCache, pCache->pTempQueue[j].pKey, pCache->pTempQueue[i].pKey))
					{
						bFound = true;
						estrConcatf(&esDetails, "Queue indices %d and %d have the same key", i, j);
						break;
					}
				}
				if (pCache->pDebugParseTable)
				{
					if (stashFindPointer(pCache->stCache, pCache->pTempQueue[i].pKey, &pOther))
					{
						// Print details about the other match and this one
						char* esStruct = NULL;
						ParserWriteText(&esStruct, pCache->pDebugParseTable, pOther, WRITETEXTFLAG_PRETTYPRINT, 0, 0);
						estrConcatf(&esDetails, "In table:\n%s", esStruct);
						estrDestroy(&esStruct);
					}
					{
						// Print details about the found one
						char* esStruct = NULL;
						ParserWriteText(&esStruct, pCache->pDebugParseTable, (void*)pCache->pTempQueue[i].pValue, WRITETEXTFLAG_PRETTYPRINT, 0, 0);
						estrConcatf(&esDetails, "To Add:\n%s", esStruct);
						estrDestroy(&esStruct);
					}
				}
				ErrorDetailsf("%s", esDetails);
				Errorf("Tried to add element to ThreadSafePriorityCache that was already there!");
				estrDestroy(&esDetails);
				/*
				if (pCache->keyDestructor)
					pCache->keyDestructor((void*)pCache->pTempQueue[i].pKey);
				if (pCache->valueDestructor)
					pCache->valueDestructor((void*)pCache->pTempQueue[i].pValue);
					*/
			}
		}
	}

	// Clear the queue
	memset(pCache->pTempQueue, 0, sizeof(QElement) * pCache->iMaxQueueSize);
	pCache->iCurrentQueueSize = 0;
}

// NOT THREAD SAFE
void tspCacheClear(ThreadSafePriorityCache* pCache)
{
	// Clear the queue
	memset(pCache->pTempQueue, 0, sizeof(QElement) * pCache->iMaxQueueSize);
	pCache->iCurrentQueueSize = 0;

	stashTableClearEx(pCache->stCache, pCache->keyDestructor, pCache->valueDestructor);
}

// NOT THREAD SAFE
void tspCacheDestroy(ThreadSafePriorityCache* pCache)
{
	tspCacheClear(pCache);
    DeleteCriticalSection(&pCache->criticalSection);
	free(pCache);
}

void tspCacheGetIterator(ThreadSafePriorityCache* pCache, ThreadSafePriorityCacheIterator* pIter)
{
	stashGetIterator(pCache->stCache, &pIter->iter);
}

void* tspCacheGetNextElement(ThreadSafePriorityCacheIterator* pIter)
{
	StashElement elem;
	bool bFound = stashGetNextElement(&pIter->iter, &elem);
	if (!bFound)
		return NULL;
	return stashElementGetPointer(elem);
}
