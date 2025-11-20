#include "AssociativeCache.h"
#include "HashFunctions.h"
#include "rand.h"


AssociativeCache* associativeCacheCreateEx( U32 uiNumSets, U32 uiSetSize, ACacheKeyType keyType, U32 uiFixedKeyLength, SA_PARAM_NN_VALID ACacheFreeElemFunc freeElemFunc MEM_DBG_PARMS)
{
	AssociativeCache* pCache = scalloc(sizeof(AssociativeCache) + uiNumSets*uiSetSize*sizeof(ACacheElem),1);

	assert(isPower2(uiNumSets) && isPower2(uiSetSize));

	pCache->pStorage = (ACacheElem*) ( ((char*)pCache) + sizeof(AssociativeCache) );

	pCache->uiNumSets = uiNumSets;
	pCache->uiSetSize = uiSetSize;
	pCache->keyType = keyType;
	pCache->freeElemFunc = freeElemFunc;
	pCache->uiFixedKeyLength = (keyType==ACacheKeyType_Strings)?0:uiFixedKeyLength;

	return pCache;
}

// Calls the dump
void associativeCacheClear(AssociativeCache* pCache)
{
	U32 uiIndex;
	U32 uiNumElems = pCache->uiNumSets * pCache->uiSetSize;
	for (uiIndex=0; uiIndex<uiNumElems; ++uiIndex)
	{
		ACacheElem* pElem = &pCache->pStorage[uiIndex];
		if (pElem->pKey)
		{
			pCache->freeElemFunc(pElem);
			pElem->pKey = pElem->pValue = NULL;
		}
	}
}

void associativeCacheDestroy(AssociativeCache* pCache)
{
	associativeCacheClear(pCache);
	free(pCache);
}

bool associativeCacheFindElem(AssociativeCache* pCache, void* pKey, ACacheElem** ppElem)
{
	U32 uiHashValue = (pCache->keyType==ACacheKeyType_Strings)
		? stashDefaultHashCaseInsensitive_inline(pKey, (int)strlen(pKey), DEFAULT_HASH_SEED)
		: stashDefaultHash_inline(pKey, pCache->uiFixedKeyLength, DEFAULT_HASH_SEED);
	U32 uiSetIndex = uiHashValue & (pCache->uiNumSets - 1);
	U32 uiFirstSlotIndex = uiSetIndex * pCache->uiSetSize;
	U32 uiSlotIndex;

	*ppElem = NULL;

	for (uiSlotIndex=0; uiSlotIndex<pCache->uiSetSize; ++uiSlotIndex)
	{
		ACacheElem* pElem = &pCache->pStorage[uiFirstSlotIndex + uiSlotIndex];
		if (!pElem->pKey) // at least we found a free slot, use this one
		{
			*ppElem = pElem;
			continue;
		}

		if (pCache->keyType == ACacheKeyType_Strings)
		{
			// Assumes they are string pooled!!!
			if (pElem->pKey == pKey)
			{
				*ppElem = pElem;
				return true;
			}
		}
		else if (memcmp(pElem->pKey, pKey, pCache->uiFixedKeyLength)==0)
		{
				*ppElem = pElem;
				return true;
		}
	}

	if (!(*ppElem)) // Found no free slot, so pick one at random
	{
		ACacheElem* pElem = &pCache->pStorage[ uiFirstSlotIndex + (randomU32() & (pCache->uiSetSize - 1)) ];
		pCache->freeElemFunc(pElem); // free whatever was there before
		*ppElem = pElem;
	}
	(*ppElem)->pKey = pKey;
	return false;
}