// eset.c - Provide simple hash set implementation

#include "eset.h"
#include <string.h>


#include "timing.h"
#include "textparser.h"
#include "HashFunctions.h"

#define ESET_TRACKING 0
#define FRAC_SHIFT 6
#define NONP2_TABLE 0

//U32	table_stats[32];

// Struct-related inlined functions
typedef struct ESetImp
{		
	U32 uiSize;
	U32 uiMaxSize; // Total size
	U32 uiValidValues; // Number of valid values

#if NONP2_TABLE
	U32	uiHashMask: 24;				// 1 << log2(uiMaxSize) - 1
	U32	fraction: 8;				// fraction / (1 << FRAC_SHIFT) is the amount of the P2 size allocated
#endif

#if ESET_TRACKING
	U32 numAdded : 8;
	U32 numRemoved : 8;
	U32 numCleared : 8;
	U32 numResized : 8;
#endif
	void* pStorage[1];
} ESetImp;

#define ESET_HEADER_SIZE	OFFSETOF(ESetImp,pStorage)	

#define ESET_HASH_MASK(n) (n-1)
#define ESET_TABLE_EMPTY_SLOT_KEY ((void*)(intptr_t)0)

#ifdef  _WIN64
#define ESET_TABLE_DELETED_SLOT_VALUE ((void*)(uintptr_t)0xfffffffffffffffeULL)
#elif _PS3
#define ESET_TABLE_DELETED_SLOT_VALUE ((void*)(uintptr_t)0xfffffffeU)	// _W64 = we promise to expand to 64 bits if _WIN64 is defined
#else
#define ESET_TABLE_DELETED_SLOT_VALUE ((void*)(_W64 uintptr_t)0xfffffffeU)	// _W64 = we promise to expand to 64 bits if _WIN64 is defined
#endif

#define ESET_TABLE_QUADRATIC_PROBE_COEFFICIENT 1


#define ESET_TABLE_MIN_SIZE 8

#if NONP2_TABLE
#define eSetMaskTableIndex(pTable, uiValueToMask) (((uiValueToMask & pTable->uiHashMask) * pTable->fraction) >> FRAC_SHIFT)
#else
#define eSetMaskTableIndex(pTable, uiValueToMask) ((uiValueToMask) & ESET_HASH_MASK((pTable)->uiMaxSize))
#endif

#define slotEmpty(pElem) ((pElem) == ESET_TABLE_EMPTY_SLOT_KEY)
#define slotDeleted(pElem) ((pElem) == ESET_TABLE_DELETED_SLOT_VALUE)

#define eSetFromHandle(handle) ((handle)?(*handle):NULL)

#define eSetDefaultHash				MurmurHash2
#define eSetDefaultHash_inline		MurmurHash2_inline

// 0 will default it to MIN_SIZE
void eSetCreate_dbg(ESetHandle handle, int capacity MEM_DBG_PARMS)
{
	ESetImp* pSet;
	
	capacity = MAX(pow2(capacity), ESET_TABLE_MIN_SIZE);
	assertmsg(!(*handle), "Tried to create an eSet that is either already created or has a corrupt pointer.");
	pSet = scalloc(ESET_HEADER_SIZE + sizeof(void *) * capacity, 1);
	pSet->uiValidValues = 0;
	pSet->uiMaxSize = capacity;
#if NONP2_TABLE
	pSet->fraction = 1 << FRAC_SHIFT;
	pSet->uiHashMask = pSet->uiMaxSize-1;
#endif
	assertmsg(pSet->uiMaxSize != 0, "invalid");	
	*handle = pSet;
}


void eSetDestroy(ESetHandle handle)
{
	ESetImp *pSet = eSetFromHandle(handle);
	if (!pSet)
        return;

	free(pSet);

	*handle = NULL;
}

void eSetClear(ESetHandle handle)
{
	ESetImp *pSet = eSetFromHandle(handle);
	if (!pSet)
		return;
	
	// clear the storage
	memset(pSet->pStorage, 0, pSet->uiMaxSize * sizeof(void *));
	pSet->uiValidValues = 0;
#if ESET_TRACKING
	pSet->numCleared++;
#endif
	assertmsg(pSet->uiMaxSize != 0, "invalid");	
}

U32	eSetGetCount(cESetHandle handle)
{
	const ESetImp *pSet = eSetFromHandle(handle);
	if (pSet)
	{
		assertmsg(pSet->uiMaxSize != 0, "invalid");	
		return pSet->uiValidValues;
	}
	return 0;
}

U32	eSetGetMaxSize(cESetHandle handle)
{
	const ESetImp *pSet = eSetFromHandle(handle);
	if (pSet)
	{
		assertmsg(pSet->uiMaxSize != 0, "invalid");	
		return pSet->uiMaxSize;
	}
	return 0;
}

void *eSetGetValueAtIndex(cESetHandle handle, U32 index)
{
	const ESetImp *pSet = eSetFromHandle(handle);
	if (pSet)
	{
		void *pVal;
		assertmsg(pSet->uiMaxSize != 0, "invalid");	
		if (index < 0 || index >= pSet->uiMaxSize)
		{
			return NULL;
		}
		pVal = pSet->pStorage[index];
		if (!slotDeleted(pVal))
		{
			return pVal;
		}
	}
	return NULL;
}

static bool eSetFindElementIndexInternal(cESetHandle handle, const void *pSearch, U32* puiIndex, int* piFirstDeletedIndex,U32 *hash_p)
{
	U32 uiNumProbes = 1; // the first probe
	U32 uiHashValue = 0;
	U32 uiStorageIndex;
	const ESetImp *pSet = eSetFromHandle(handle);
	if (!pSet)
	{
		return false;
	}

	assertmsg(pSet->uiMaxSize != 0, "invalid");	

	PERFINFO_AUTO_START_FUNC_L2();

	if ( piFirstDeletedIndex )
		*piFirstDeletedIndex = -1; // no deleted found yet

	// don't try to find an empty key
	if ( pSearch == ESET_TABLE_EMPTY_SLOT_KEY ) // sorry, can't find an empty key
	{
		PERFINFO_AUTO_STOP_L2();
		return false;
	}
	
	uiHashValue = eSetDefaultHash((void*)&pSearch, sizeof(void *), DEFAULT_HASH_SEED);

	if (hash_p)
		*hash_p = uiHashValue;

	uiStorageIndex = eSetMaskTableIndex(pSet, uiHashValue);
	while ( 1 )
	{
		const void *pElement;

		// Check to see if the new index is the correct index or not
		pElement = pSet->pStorage[uiStorageIndex];

		// Is it an empty slot?
		if (slotEmpty(pElement))
		{
			// We found an empty slot, here we go
			if (puiIndex)
				*puiIndex = uiStorageIndex;

			PERFINFO_AUTO_STOP_L2();
			return false; // empty
		}

		// It's not empty, so is it the right key?
		if (!slotDeleted(pElement)) // if we were deleted, we can't do a compare on the keys legally
		{		
			if ( pElement == pSearch )
			{
				if ( puiIndex )
					*puiIndex = uiStorageIndex;
				PERFINFO_AUTO_STOP_L2();
				return true; // it's already in there
			}
		}
		else
		{
			// If we haven't yet recorded the first deleted, do so now
			if (piFirstDeletedIndex && *piFirstDeletedIndex == -1)
			{
				*piFirstDeletedIndex = uiStorageIndex;
			}
		}

		// Ok, it's not the right key and it's not empty, either way we need to try the next one
		// Use quadratic probing
		{
#if NONP2_TABLE
			uiNumProbes++;
			uiStorageIndex++;
			if (uiStorageIndex >= pSet->uiMaxSize)
				uiStorageIndex -= pSet->uiMaxSize;
#else
			U32 uiProbeOffset = ESET_TABLE_QUADRATIC_PROBE_COEFFICIENT * uiNumProbes * uiNumProbes;
			uiNumProbes++;
			uiStorageIndex = eSetMaskTableIndex(pSet, (uiStorageIndex + uiProbeOffset) );
#endif
		}

		// Try again, now with a new index
	}
}

static void eSetTableResize(ESetHandle handle MEM_DBG_PARMS)
{
	ESetImp *pOldSet = eSetFromHandle(handle);
	ESetImp *pSet;
	U32 uiOldIndex;
	U32 uiNewMaxSize;
	U32 fraction;
	U32 uiHashMask;
	ESet newHandle = NULL;

	assert(pOldSet);

	PERFINFO_AUTO_START("eSet table resize", 1);

	// The new size should be based off of the number of valid values, not the size
	// since the size includes deleted elems which will not find their way into the
	// the new table
	//
	// New maxsize should be the first power of 2 such that validvalues is less than half


#if NONP2_TABLE
	{
		U32	bits,val;

		val = 1 + (pOldSet->uiValidValues * 3) / 2;
		bits = log2(val);
		if (bits < FRAC_SHIFT)
			fraction = val << (FRAC_SHIFT - bits);
		else
			fraction = 1 + (val >> (bits - FRAC_SHIFT));
		fraction = MIN(1 << FRAC_SHIFT,fraction);
		uiNewMaxSize = ((1 << bits) * fraction) >> FRAC_SHIFT;
		uiHashMask = (1 << bits)-1;
	}
#else
	uiNewMaxSize = MAX(pOldSet->uiMaxSize, (U32)pow2(pOldSet->uiValidValues<<1));
	uiHashMask = uiNewMaxSize-1;
	fraction = 1 << FRAC_SHIFT;
#endif

	pSet = scalloc(ESET_HEADER_SIZE + sizeof(void *)*(uiNewMaxSize), 1);
	pSet->uiMaxSize = uiNewMaxSize;
#if NONP2_TABLE
	pSet->uiHashMask = uiHashMask;
	pSet->fraction = fraction;
#endif
	assertmsg(pSet->uiMaxSize != 0, "invalid");	

	newHandle = pSet;
	// Go through every old element
	for (uiOldIndex=0; uiOldIndex < pOldSet->uiMaxSize; ++uiOldIndex)
	{
		void* pOldElement = pOldSet->pStorage[uiOldIndex];
		bool bSuccess = false;

		if ( slotEmpty(pOldElement) || slotDeleted(pOldElement))
			continue; // don't add deleted or unused slots

		// put it in the new table, since it must be valid at this point
		bSuccess  = eSetAdd_dbg(&newHandle, pOldElement MEM_DBG_PARMS_CALL);
		assert(bSuccess); // otherwise, we're trying to add element twice somehow
		assert(pSet == eSetFromHandle(&newHandle)); // See if somehow it tried to resize recursively
	}

#if ESET_TRACKING
	pSet->numResized = pOldSet->numResized + 1;
#endif
	assertmsgf(pSet->uiSize == pSet->uiValidValues && 
		pOldSet->uiValidValues == pSet->uiValidValues, "%d != %d != %d (%p)", pSet->uiSize, pSet->uiValidValues, pOldSet->uiValidValues, pOldSet); // should always be equal after a resize

	free(pOldSet);

	assertmsg(pSet->uiMaxSize != 0, "invalid");	

	*handle = pSet;

	PERFINFO_AUTO_STOP();

//	table_stats[log2(pSet->uiMaxSize)]++;
}


bool eSetAdd_dbg(ESetHandle handle, const void *pSearch MEM_DBG_PARMS)
{
	U32 uiStorageIndex = 2;
	int iFirstDeletedIndex = 2;
	bool bFound = 2;
	U32 hash = 2; // default values so I can debug something
	void *pValue;
	ESetImp *pSet;

	assert( pSearch != ESET_TABLE_DELETED_SLOT_VALUE ); // sorry, can't add the one value that means it's a deleted slot!
	assert( pSearch != ESET_TABLE_EMPTY_SLOT_KEY ); // sorry, can't add the one key that means it's an empty slot!

	if (!(*handle)) 
	{
		eSetCreate_dbg(handle, 0 MEM_DBG_PARMS_CALL);	// Auto-create pSet
		pSet = eSetFromHandle(handle);
		assertmsg(pSet->uiMaxSize != 0, "created but failed");	
	}
	else
	{
		pSet = eSetFromHandle(handle);
		assertmsg(pSet->uiMaxSize != 0, "invalid");	
	}	

	bFound = eSetFindElementIndexInternal(handle, pSearch, &uiStorageIndex, &iFirstDeletedIndex, &hash);
	if ( bFound )
	{	
		return false;
	}	

	PERFINFO_AUTO_START_FUNC_L2();

	if (iFirstDeletedIndex > -1)
	{
		// we couldn't find the key, but found this nice deleted slot on the way to the first empty slot
		// use this instead, so we don't waste the deleted slot 
		uiStorageIndex = (U32)iFirstDeletedIndex;
	}
	else
	{
		assertmsg(uiStorageIndex >= 0 && uiStorageIndex < pSet->uiMaxSize, "invalid");	
		// Used a new slot, indicate that
		pSet->uiSize++;		
		assertmsg(pSet->uiMaxSize != 0, "invalid");	
	}

	pValue = pSet->pStorage[uiStorageIndex];

	if ( slotDeleted(pValue) || slotEmpty(pValue) )
	{
		// it was not a valid slot, now it will be
		pSet->uiValidValues++;
	}
	// copy the value
	pSet->pStorage[uiStorageIndex] = (void *)pSearch;
#if ESET_TRACKING
	pSet->numAdded++;
#endif
	// If we are 75% or more full, resize so that storage index hash collision is minimized
	if ( pSet->uiSize > ((pSet->uiMaxSize >> 1) + (pSet->uiMaxSize >> 2))  )
	{
		if (pSet->uiValidValues == 0)
		{
			eSetClear(handle);
		}
		else
		{		
			eSetTableResize(handle MEM_DBG_PARMS_CALL);
		}
	}

	pSet = eSetFromHandle(handle);
	assertmsg(pSet->uiMaxSize != 0, "invalid");	

	PERFINFO_AUTO_STOP_L2();
	return true;
}

bool eSetRemove(ESetHandle handle, const void *pSearch)
{
	U32 uiStorageIndex;

	ESetImp *pSet;
	pSet = eSetFromHandle(handle);

	if (!pSet)
	{
		return false;
	}
	assertmsg(pSet->uiMaxSize != 0, "invalid");	
	if ( !eSetFindElementIndexInternal(handle, pSearch, &uiStorageIndex, NULL, NULL))
	{
		return false;
	}
	pSet->pStorage[uiStorageIndex] = (void*)ESET_TABLE_DELETED_SLOT_VALUE;
	pSet->uiValidValues--;
#if ESET_TRACKING
	pSet->numRemoved++;
#endif
	assertmsg(pSet->uiMaxSize != 0, "invalid");	

	return true;
}

bool eSetFind(cESetHandle handle, const void *pSearch)
{
	if (eSetFindElementIndexInternal(handle, pSearch, NULL, NULL, NULL))
	{
		return true;
	}
	return false;
}
