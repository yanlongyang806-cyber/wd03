#include "StashSet.h"
#include "HashFunctions.h"
#include "timing.h"
#include "wininclude.h"
#include "utils.h"
#include "TextParser.h"

#define NONP2_TABLE 1
#define CACHE_HASHES 0

#define DO_LOGIT 0
#ifdef _FULLDEBUG
#define DEF_INLINE 
#else
#define DEF_INLINE __forceinline 
#endif
#define STASH_HASH_MASK(n) (n-1)
#define STASH_TABLE_EMPTY_SLOT_KEY ((void*)(intptr_t)0)

#ifdef  _WIN64
#define STASH_TABLE_DELETED_SLOT_KEY ((void*)(uintptr_t)0xfffffffffffffffeULL)
#else
#define STASH_TABLE_DELETED_SLOT_KEY ((void*)(_W64 uintptr_t)0xfffffffeU)	// _W64 = we promise to expand to 64 bits if _WIN64 is defined
#endif

typedef struct StashSetElement
{
	const char *key;
#if CACHE_HASHES
	U32 hash;
#endif
} StashSetElement;

#ifdef _M_X64
#define FRAC_SHIFT 3
#else
#define FRAC_SHIFT 6
#endif

typedef struct StashSetImp
{
	U32							uiSize;					// Current number of elements, including deleted elements
	U32							uiValidValues;			// number of valid elements (non-deleted)
	U32							uiMaxSize;				// Current element storage array size
	U32							uiMinSize;				// Defaults to STASH_TABLE_MIN_SIZE, but can be set if you want to avoid resizings.
	U32							uiHashMask;				// 1 << log2(uiMaxSize) - 1
	StashSetElement				*pStorage;				// Actual element storage

	U16							fraction;				// fraction / (1 << FRAC_SHIFT) is the amount of the P2 size allocated

	// Bit flags
	U32							bCaseSensitive			: 1; // is sensitive to case differences, slight performance increase, but be careful
	U32							bCantResize				: 1; // if set, resize will assert
	U32							bCantDestroy			: 1; // if set, destroy will assert
	U32							bThreadSafeLookups		: 1; // if this is set, lookups must be thread safe (no caching results)
	U32							bGrowFast				: 1; // Grow fast for speed instead of space efficiency

	MEM_DBG_STRUCT_PARMS

	const char					*pLastKey;
	U32							lastIndex;

} StashSetImp;

static void stashSetResize(StashSetImp* pSet);

// Made macros for performance (in both optimized and non-optimized builds)
// static U32 stashMaskSetIndex( const StashSetImp* pSet, U32 uiValueToMask )
// {
// 	return uiValueToMask & STASH_HASH_MASK(pSet->uiMaxSize);
// }
// static bool slotEmpty(const StashSetElement* pElem)
// {
// 	return (pElem->key.pKey == STASH_TABLE_EMPTY_SLOT_KEY);
// }
//
// static bool slotDeleted(const StashSetElement* pElem)
// {
// 	return (pElem->value.pValue == STASH_TABLE_DELETED_SLOT_KEY);
// }
//

#define stashMaskSetIndex(pSet, uiValueToMask) (((uiValueToMask & pSet->uiHashMask) * pSet->fraction) >> FRAC_SHIFT)
#define slotEmpty(pElem) ((pElem)->key == STASH_TABLE_EMPTY_SLOT_KEY)
#define slotDeleted(pElem) (((pElem)->key) == STASH_TABLE_DELETED_SLOT_KEY)


// -------------------------
// Set management and info
// -------------------------
#define STASH_TABLE_MIN_SIZE 8

StashSet stashSetCreate(U32 uiInitialSize, const char *caller_fname, int line)
{
	StashSetImp* pNewSet;

	PERFINFO_AUTO_START_FUNC();

	if (!caller_fname)
		caller_fname = __FILE__, line = __LINE__;
	
	pNewSet = scalloc(1, sizeof(StashSetImp));

	MEM_DBG_STRUCT_PARMS_INIT(pNewSet);

	pNewSet->uiMinSize = STASH_TABLE_MIN_SIZE;
	if ( uiInitialSize < pNewSet->uiMinSize)
		uiInitialSize = pNewSet->uiMinSize;

#if NONP2_TABLE
	{
		U32	max_size,bits,val;

		val = MAX(uiInitialSize,pNewSet->uiMinSize);
		bits = log2(val);
		if (bits < FRAC_SHIFT)
			pNewSet->fraction = 1 << FRAC_SHIFT;
		else
		{
			pNewSet->fraction = 1 + (val >> (bits - FRAC_SHIFT));
			pNewSet->fraction = MIN(1 << FRAC_SHIFT,pNewSet->fraction);
		}
		max_size = ((1ll << bits) * pNewSet->fraction) >> FRAC_SHIFT;
		pNewSet->uiMaxSize = MAX(pNewSet->uiMinSize,max_size);
		pNewSet->uiHashMask = (1 << bits)-1;
	}
#else
	pNewSet->uiMaxSize = pow2(uiInitialSize);
	pNewSet->fraction = 1 << FRAC_SHIFT;
	pNewSet->uiHashMask = pNewSet->uiMaxSize-1;
#endif

	// allocate memory for the set
	pNewSet->pStorage = scalloc(pNewSet->uiMaxSize, sizeof(StashSetElement));

	PERFINFO_AUTO_STOP();

	return (StashSet)pNewSet;
}

static void destroyStorageValues(StashSetImp* pSet, Destructor keyDstr)
{
	StashSetIterator iter;
	char *pElem;

	assert(keyDstr); // don't call this with no destructor
	stashSetGetIterator((StashSet)pSet, &iter);
	while (stashSetGetNextElement(&iter, &pElem))
	{
		keyDstr(pElem);
	}
}

void stashSetDestroyEx(StashSetImp* pSet, Destructor keyDstr)
{
	assert(keyDstr); // don't call this with no destructor, just call stashSetDestroy()
	if(!pSet)
		return;
	destroyStorageValues(pSet, keyDstr);

	stashSetDestroy(pSet);
}

void stashSetDestroy(StashSetImp* pSet )
{
	if (!pSet)
		return;

	assert(!pSet->bCantDestroy); // can't remove an unremovable set... probably a clone with a special allocator

	// free memory being used to hold all key/value pairs
	free( pSet->pStorage );

	free( pSet );
}

void stashSetClear(StashSetImp* pSet)
{
	if (!pSet)
		return;
	// clear the storage
	memset(pSet->pStorage, 0, pSet->uiMaxSize * sizeof(StashSetElement));
	pSet->uiSize = 0;
	pSet->uiValidValues = 0;
}

void stashSetClearEx(StashSetImp* pSet, Destructor keyDstr)
{
	if (!pSet)
		return;
	destroyStorageValues(pSet, keyDstr);
	stashSetClear(pSet);
}

// set info 
U32	stashSetGetValidElementCount(const StashSetImp* pSet)
{
	return pSet?pSet->uiValidValues:0; // external functions want to know how many valid elements we have
}

U32	stashSetGetOccupiedSlots(const StashSetImp* pSet)
{
	return pSet?pSet->uiSize:0; // in case someone wants to maintain a certain size of hash set
}

U32	stashSetGetMaxSize(const StashSetImp* pSet)
{
	return pSet?pSet->uiMaxSize:0;
}

U32 stashSetGetResizeSize(const StashSetImp* pSet)
{
	U32 uiMaxSize = stashSetGetMaxSize(pSet);
	return (uiMaxSize >> 1) + (uiMaxSize >> 2);
}

U32 stashSetCountElements(const StashSetImp *pSet)
{
	U32 uiCurrentIndex;
	StashSetElement* pElement;
	U32 count = 0;

	if (!pSet)
		return 0;

	for(uiCurrentIndex = 0; uiCurrentIndex < pSet->uiMaxSize; ++uiCurrentIndex)
	{
		pElement = &pSet->pStorage[uiCurrentIndex];

		// If we have found an non-empty, non-deleted element...
		if (!slotEmpty(pElement) && !slotDeleted(pElement))
		{
			count += 1;
		}
	}

	return count;
}

bool stashSetValidateValid(const StashSetImp *pSet)
{
	bool result;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	result = !pSet || stashSetCountElements(pSet)==pSet->uiValidValues;

	PERFINFO_AUTO_STOP();
	return result;
}

void stashSetSetThreadSafeLookups(StashSetImp* pSet, bool bSet)
{
	pSet->bThreadSafeLookups = !!bSet;
}

void stashSetSetCantResize(StashSetImp* pSet, bool bSet)
{
	pSet->bCantResize = !!bSet;
}

void stashSetSetGrowFast(StashSetImp* pSet, bool bSet)
{
	pSet->bGrowFast = !!bSet;
}


// Iterators
void stashSetGetIterator(StashSetImp* pSet, StashSetIterator* pIter)
{
	pIter->pSet = pSet;
	pIter->uiIndex = 0;
}

bool stashSetGetNextElement(StashSetIterator* pIter, const char** ppElem)
{
	U32 uiCurrentIndex;
	StashSetImp* pSet = pIter->pSet;
	StashSetElement* pElement;

	if (!pSet)
		return false;

	// Look through the set starting at the index specified by the iterator.
	for(uiCurrentIndex = pIter->uiIndex; uiCurrentIndex < pSet->uiMaxSize; ++uiCurrentIndex)
	{
		pElement = &pSet->pStorage[uiCurrentIndex];

		// If we have found an non-empty, non-deleted element...
		if (!slotEmpty(pElement) && !slotDeleted(pElement))
		{
			pIter->uiIndex = uiCurrentIndex + 1;
			if (ppElem)
				*ppElem = pElement->key;
			return true;
		}
	}
	return false;
}

DEF_INLINE static bool stashKeysAreEqualInternal(const StashSetImp* pSet, const char* pKeyA, const char* pKeyB)
{
	if (pKeyA == pKeyB)
	{
		return true;
	}
	else
	{
		if ( !pSet->bCaseSensitive )
		{
			if ( inline_stricmp( pKeyA, pKeyB ) == 0 )
				return true;
		}
		else
		{
			if ( strcmp( pKeyA, pKeyB ) == 0 )
				return true;
		}
	}
	return false;
}

static DEF_INLINE bool stashSetFindIndexByHashInternal(StashSetImp* pSet, const char *key, U32 uiHashValue, U32* puiIndex, int* piFirstDeletedIndex)
{
	U32 uiNumProbes = 1; // the first probe
	U32 uiStorageIndex;

	uiStorageIndex = stashMaskSetIndex(pSet, uiHashValue);

	while ( 1 )
	{
		const StashSetElement* pElement;

		PREFETCH(&pSet->pStorage[uiStorageIndex]);

		// Check to see if the new index is the correct index or not
		pElement = &pSet->pStorage[uiStorageIndex];

		// Is it an empty slot?
		if (slotEmpty(pElement))
		{
			// We found an empty slot, here we go
			if (puiIndex)
				*puiIndex = uiStorageIndex;

			return false; // empty
		}

		// It's not empty, so is it the right key?
		if (!slotDeleted(pElement)) // if we were deleted, we can't do a compare on the keys legally
		{
#if CACHE_HASHES
			if (uiHashValue == pElement->hash)
#endif
			{
				if (stashKeysAreEqualInternal(pSet, key, pElement->key))
				{
					if ( puiIndex )
						*puiIndex = uiStorageIndex;
					if (!pSet->bThreadSafeLookups) // shared memory
					{
						pSet->lastIndex = uiStorageIndex;
						pSet->pLastKey = pElement->key;
					}
					return true; // it's already in there
				}
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
		{
#if NONP2_TABLE
#if 0
			//j = ((5*j) + 1) mod 2**i
			if (uiNumProbes == 1) // a little hackery to mix the bits unused in originally picking the slot to reduce collisions
				uiStorageIndex = (uiStorageIndex + (uiHashValue >> 29)) & pSet->uiHashMask;
			uiProbeOffset = (uiProbeOffset + uiNumProbes) & pSet->uiHashMask;
#endif
			//if (uiNumProbes == 1 && pSet->eKeyType == StashKeyTypeStrings)
			//	uiStorageIndex = (uiStorageIndex + (uiHashValue >> 29)) & pSet->uiHashMask;
			uiStorageIndex++;
			if (uiStorageIndex >= pSet->uiMaxSize)
				uiStorageIndex -= pSet->uiMaxSize;
			uiNumProbes++;
#else
			U32 uiProbeOffset = uiNumProbes * uiNumProbes;
			uiNumProbes++;
			uiStorageIndex = stashMaskSetIndex(pSet, (uiStorageIndex + uiProbeOffset) );
#endif
		}

		// Try again, now with a new index
        
        // detect lockup due to stomps etc...
        assert(uiNumProbes <= pSet->uiMaxSize);
	}
}

// ----------------------------------
// Internal hash processing functions
// ----------------------------------
// This is the real magic behind the hash set, here we translate a key into a hash set index, 
// and deal with hash set index collisions (in this case through quadratic probing)
// 
// returns true if it was already in the set, false otherwise
// will always find a value
static DEF_INLINE bool stashSetFindIndexByKeyInternal(StashSetImp* pSet, const char *key, U32* puiIndex, int* piFirstDeletedIndex, U32 *hash_p)
{
	U32 uiHashValue = 0;
	bool bRet;

	PERFINFO_AUTO_START_FUNC_L2(); // JE: Changed these to _L2 versions, they were adding around 20ms/frame when doing timer record, greatly skewing the results.  #define PERFINFO_L1 in timing.h to enable

	if ( piFirstDeletedIndex )
		*piFirstDeletedIndex = -1; // no deleted found yet

	// don't try to find an empty key
	if ( key == STASH_TABLE_EMPTY_SLOT_KEY ) // sorry, can't find an empty key
	{
		PERFINFO_AUTO_STOP_L2();
		return false;
	}

	// Check the cached last key first.

	if (!pSet->bThreadSafeLookups && pSet->pLastKey == key)
	{
		const U32 lastIndex = pSet->lastIndex;

		if (pSet->pStorage[lastIndex].key == key)
		{		
			if (puiIndex)
				*puiIndex = lastIndex;
			if (hash_p)
#if CACHE_HASHES
				*hash_p = pSet->pStorage[lastIndex].hash;
#else
				*hash_p = 0;
#endif
			PERFINFO_AUTO_STOP_L2();
			return true;
		}
	}

	if ( pSet->bCaseSensitive)
       	uiHashValue = stashDefaultHash_inline(key, (int)strlen(key), DEFAULT_HASH_SEED);
	else // this version of the hash function sets the lowercase bit, so it does not distinguish between the two possibilities
		uiHashValue = stashDefaultHashCaseInsensitive_inline(key, (int)strlen(key), DEFAULT_HASH_SEED);
	if (hash_p)
		*hash_p = uiHashValue;

	bRet = stashSetFindIndexByHashInternal(pSet, key, uiHashValue, puiIndex, piFirstDeletedIndex);
	PERFINFO_AUTO_STOP_L2();
	return bRet;
}

DEF_INLINE bool stashSetFindValueInternal(StashSetImp* pSet, const char *key, const char **pValue)
{
	U32 uiStorageIndex;

	if (!stashSetFindIndexByKeyInternal(pSet, key, &uiStorageIndex, NULL, NULL))
	{
		if ( pValue )
			*pValue = 0;
		return false;
	}

	if (pValue)
		*pValue = pSet->pStorage[uiStorageIndex].key;
	return true;
}

DEF_INLINE static bool stashSetAddValueInternal(StashSetImp* pSet, const char *key, bool bOverwriteIfFound, const char **ppKeyOut)
{
	U32 uiStorageIndex;
	int iFirstDeletedIndex;
	bool bFound;
	StashSetElement* pElement;
	U32 hash;

	PERFINFO_AUTO_START_FUNC_L2();

	assert( key != STASH_TABLE_DELETED_SLOT_KEY ); // sorry, can't add the one key that means it's a deleted slot!
	assert( key != STASH_TABLE_EMPTY_SLOT_KEY ); // sorry, can't add the one key that means it's an empty slot!
	
	bFound = stashSetFindIndexByKeyInternal(pSet, key, &uiStorageIndex, &iFirstDeletedIndex, &hash);
	if ( bFound && !bOverwriteIfFound )
	{
		if ( ppKeyOut )
			*ppKeyOut = pSet->pStorage[uiStorageIndex].key;
		PERFINFO_AUTO_STOP_L2();
		return false;
	}
	// Otherwise, copy it in and return true;

	if (!bFound && iFirstDeletedIndex > -1)
	{
		// we couldn't find the key, but found this nice deleted slot on the way to the first empty slot
		// use this instead, so we don't waste the deleted slot 
		uiStorageIndex = (U32)iFirstDeletedIndex;
		pSet->uiSize--; // Counteract increase, because we're reusing a slot
	}

	pElement = &pSet->pStorage[uiStorageIndex];

#if CACHE_HASHES
	pElement->hash = hash;
#endif
	if ( slotDeleted(pElement) || slotEmpty(pElement) )
	{
		// it was not a valid slot, now it will be
		pSet->uiValidValues++;
	}

	pElement->key = key;
	if (ppKeyOut)
		*ppKeyOut = key;

	// We just added an element to our hash set. Increment the size, and check if we need to resize
	if ( !bFound ) // don't add it if it's found in set, it means we are overwriting and there is no increase in size
	{
		pSet->uiSize++;
	}

	// If we are 75% or more full, resize so that storage index hash collision is minimized
	if ( pSet->uiSize >= ((pSet->uiMaxSize >> 1) + (pSet->uiMaxSize >> 2))  )
	{
		stashSetResize(pSet);
	}

	PERFINFO_AUTO_STOP_L2();
	return true;
}


static void stashSetResize(StashSetImp* pSet)
{
	U32 uiOldMaxSize = pSet->uiMaxSize;
	U32 uiOldValidValues = pSet->uiValidValues;
	StashSetElement* pOldStorage = pSet->pStorage;
	U32 uiOldIndex;

	PERFINFO_AUTO_START("stash set resize", 1);
	assertmsg(!pSet->bCantResize, "If this happens during objectdb loading, please increase the value passed into -faststringcache"); // can't resize!

	// The new size should be based off of the number of valid values, not the size
	// since the size includes deleted elems which will not find their way into the
	// the new set
	//
#if NONP2_TABLE
	{
		U32	max_size,bits,val,growth;

		if (pSet->bGrowFast)
			growth = 1 + uiOldValidValues * 4;
		else
			growth = 1 + (uiOldValidValues * 3) / 2;
		if (growth == uiOldValidValues)
			growth = uiOldValidValues << 1;
		val = MAX(growth,pSet->uiMinSize);
		bits = log2(val);
		if (bits < FRAC_SHIFT)
			pSet->fraction = 1 << FRAC_SHIFT;
		else
		{
			pSet->fraction = 1 + (val >> (bits - FRAC_SHIFT));
			pSet->fraction = MIN(1 << FRAC_SHIFT,pSet->fraction);
		}
		max_size = ((1ll << bits) * pSet->fraction) >> FRAC_SHIFT;
		pSet->uiMaxSize = MAX(pSet->uiMinSize,max_size);
		pSet->uiHashMask = (1 << bits)-1;
	}
#else
	pSet->uiMaxSize = MAX(pSet->uiMinSize, (U32)pow2(uiOldValidValues<<1));
	pSet->uiHashMask = pSet->uiMaxSize-1;
	pSet->fraction = 1 << FRAC_SHIFT;
#endif

	pSet->uiSize = 0; // this will increase as we add to it
	pSet->uiValidValues = 0;
	pSet->pLastKey = 0; // disable last key cache

	pSet->pStorage = stcalloc(pSet->uiMaxSize, sizeof(StashSetElement), pSet);

	// Go through every old element
	for (uiOldIndex=0; uiOldIndex < uiOldMaxSize; ++uiOldIndex)
	{
		StashSetElement* pOldElement = &pOldStorage[uiOldIndex];
		bool bSuccess = false;
		
		if ( slotEmpty(pOldElement) || slotDeleted(pOldElement))
			continue; // don't add deleted or unused slots

		// put it in the new set, since it must be valid at this point
		bSuccess  = stashSetAddValueInternal(pSet, pOldElement->key, false, NULL);
		assert(bSuccess); // otherwise, we're trying to add element twice somehow
	}

	assert(
		pSet->uiValidValues == pSet->uiSize 
		&& uiOldValidValues == pSet->uiValidValues
		); // should always be equal after a resize

	// Free old storage
	free(pOldStorage);

	PERFINFO_AUTO_STOP();
}

static bool stashSetRemoveValueInternal(StashSetImp* pSet, const char *key, const char **ppValue)
{
	U32 uiStorageIndex;
	if ( !stashSetFindIndexByKeyInternal(pSet, key, &uiStorageIndex, NULL, NULL))
	{
		if ( ppValue )
			*ppValue = NULL;
        return false;
	}
	if ( ppValue )
		*ppValue = pSet->pStorage[uiStorageIndex].key;
	pSet->pStorage[uiStorageIndex].key = (void*)STASH_TABLE_DELETED_SLOT_KEY;
	pSet->uiValidValues--;
	pSet->pLastKey = 0;
	
	return true;
}



int stashSetVerifyStringKeys(StashSet pSet) // Checks that no keys have been corrupted
{
	StashSetIterator iter;
	char *pKey;
	int ret = 1;

	stashSetGetIterator((StashSet)pSet, &iter);

	while (stashSetGetNextElement(&iter, &pKey))
	{
		U32 uiActualIndex = iter.uiIndex - 1;
		U32 uiStorageIndex;
		if (stashSetFindIndexByKeyInternal(pSet, pKey, &uiStorageIndex, NULL, NULL)) {
			// Verify that this index is the one we're looking at
			if (uiActualIndex != uiStorageIndex) {
				OutputDebugStringf("String in stash set has been changed to \"%s\", which is at both index %d and %d!", pKey, uiActualIndex, uiStorageIndex);
				ret = 0;
			}
		} else {
			// The index doesn't match!  It's gone bad!
			OutputDebugStringf("String in stash set has been changed to \"%s\", which was not found in the stash set!", pKey);
			ret = 0;
		}
	}
	return ret;
}



// -----------
// Pointer Keys
// -----------
// pointer values
bool stashSetAdd(StashSetImp* pSet, const char* pKey, bool bOverwriteIfFound, const char **ppKeyOut)
{
	assert(pSet);
	return stashSetAddValueInternal(pSet, pKey, bOverwriteIfFound, ppKeyOut);
}

bool stashSetRemove(StashSetImp* pSet, const char* pKey, const char **ppValue)
{
	assert(pSet);
	return stashSetRemoveValueInternal(pSet, pKey, ppValue);
}

bool stashSetFind(StashSetImp* pSet, const char* pKey, const char **ppValue)
{
	assert(pSet);
	return stashSetFindValueInternal(pSet, pKey, ppValue);
}
