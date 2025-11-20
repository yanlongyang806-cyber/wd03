
#include "StashTable.h"
#include "StringTable.h"
#include "HashFunctions.h"

#include "timing.h"
#include "SharedHeap.h"
#include "wininclude.h"
#include "utils.h"
#include "TextParser.h"

#define NONP2_TABLE 1
#define CACHE_HASHES 0

// See hitme() for instructions.
#define STASH_TIMING 0

//#define STASH_TABLE_TRACK
#ifdef STASH_TABLE_TRACK
#include "file.h"
#include "SharedMemory.h"
#endif

#define DO_LOGIT 0
#ifdef _FULLDEBUG
#define DEF_INLINE 
#else
#define DEF_INLINE __forceinline 
#endif
#define STASH_HASH_MASK(n) (n-1)

// must be the size of a pointer
typedef union _StashKey
{
	//int iKey;
	//const char* pcKey;
	//const void* pKey;
	//void* pNonConstKey;
	//U32 uiKey;
	void* pKey;
} StashKey;

// must be the size of a pointer
typedef union _StashValue
{
	//int iValue;
	void* pValue;
	//U32 uiValue;
} StashValue;


typedef struct StashElementImp
{
	StashKey key;
	StashValue value;
#if CACHE_HASHES
	U32 hash;
#endif
} StashElementImp;

#ifdef _M_X64
#define FRAC_SHIFT 3
#else
#define FRAC_SHIFT 6
#endif

typedef struct StashTableImp
{
	U32							uiSize;					// Current number of elements, including deleted elements
	U32							uiValidValues;			// number of valid elements (non-deleted)
	U32							uiMaxSize;				// Current element storage array size
	U32							uiMinSize;				// Defaults to STASH_TABLE_MIN_SIZE, but can be set if you want to avoid resizings.
	U32							uiHashMask;				// 1 << log2(uiMaxSize) - 1
	StashElementImp*			pStorage;				// Actual element storage
	StringTable					pStringTable;			// string storage space when deep copy is requested.

	U16							uiFixedKeyLength;		// If we are not strings, we have a fixed keylength specified here (4 for ints)
	U16							fraction;				// fraction / (1 << FRAC_SHIFT) is the amount of the P2 size allocated

	StashKeyType				eKeyType;

	ExternalHashFunction		hashFunc;				// Only used if keytype is StashKeyTypeExternalFunctions
	ExternalCompareFunction		compFunc;				// Only used if keytype is StashKeyTypeExternalFunctions

	// Bit flags
	U32							bDeepCopyKeys_NeverRelease				: 1; // only makes sense for string keys, copy the keys into the string table above
																			 // note that you should NEVER remove elements from tables of this type, it leaks

	U32							bDeepCopyKeys_ReleaseOnRemove			: 1; //safer but slower than _NeverRelease

	U32							bCaseSensitive			: 1; // is sensitive to case differences, slight performance increase, but be careful
	U32							bInSharedHeap			: 1; // means this hash table resides in shared heap
	U32							bReadOnly				: 1; // do not allow writing or access to nonconst accessors
	U32							bCantResize				: 1; // if set, resize will assert
	U32							bCantDestroy			: 1; // if set, destroy will assert
	U32							bThreadSafeLookups		: 1; // if this is set, lookups must be thread safe (no caching results)
	U32							bAllPointerValues		: 1; // if set, all values are pointers, and can be verified
	U32							bCaseInsensitive		: 1; // force case-sensitive comparison on fixed-size keys

	U32							bThreadLocked			: 1; // a sentinel to make sure no one is abusing the stashtable in thread-unsafe code
	

	MEM_DBG_STRUCT_PARMS

	void*						pLastKey;
	U32							lastIndex;

#ifdef STASH_TABLE_TRACK
	char cName[128];
	struct StashTableImp* pNext;
#endif

} StashTableImp;

#ifdef STASH_TABLE_TRACK
StashTableImp* pStashTableList;
#endif

static char *stashTableStrDup(const char *pInKey)
{
	return strdup(pInKey);
}

static void stashTableFreeStringKey(char *pKeyToFree)
{
	free(pKeyToFree);
}

static void stashTableResize(StashTableImp* pTable);

// -------------------------
// Static utility functions
// -------------------------
static void stashSetMode(StashTableImp* pTable, StashTableMode eMode)
{
	assert(!pTable->bReadOnly);

	pTable->bCaseSensitive = !!(eMode & CASE_SENSITIVE_BIT);
	pTable->bInSharedHeap = !!(eMode & STASH_IN_SHARED_HEAP);
	pTable->bCaseInsensitive = !!(eMode & CASE_INSENSITIVE_BIT);

	if(eMode & STASH_DEEP_COPY_KEYS_NEVER_RELEASE_BIT)
	{
		assertmsg(!pTable->bDeepCopyKeys_ReleaseOnRemove, "StashTable can't have both DeepCopyKey settings");

		pTable->bDeepCopyKeys_NeverRelease	 = 1;
		// Init the string table
		pTable->pStringTable = strTableCreateEx(0, 128 MEM_DBG_STRUCT_PARMS_CALL(pTable));
	}
	
	if(eMode & STASH_DEEP_COPY_KEYS_RELEASE_ON_REMOVE_BIT)
	{
		assertmsg(!pTable->bDeepCopyKeys_NeverRelease, "StashTable can't have both DeepCopyKey settings");
		pTable->bDeepCopyKeys_ReleaseOnRemove	 = 1;
	}
	
}

// Made macros for performance (in both optimized and non-optimized builds)
// static U32 stashMaskTableIndex( const StashTableImp* pTable, U32 uiValueToMask )
// {
// 	return uiValueToMask & STASH_HASH_MASK(pTable->uiMaxSize);
// }
// static bool slotEmpty(const StashElementImp* pElem)
// {
// 	return (pElem->key.pKey == STASH_TABLE_EMPTY_SLOT_KEY);
// }
//
// static bool slotDeleted(const StashElementImp* pElem)
// {
// 	return (pElem->value.pValue == STASH_TABLE_DELETED_SLOT_VALUE);
// }
//
// static bool usesStringKeys(const StashTableImp* pTable)
// {
// 	return (pTable->eKeyType == StashKeyTypeStrings);
// }
#define stashMaskTableIndex(pTable, uiValueToMask) (((uiValueToMask & pTable->uiHashMask) * pTable->fraction) >> FRAC_SHIFT)
#define slotEmpty(pElem) ((pElem)->key.pKey == STASH_TABLE_EMPTY_SLOT_KEY)
#define slotDeleted(pElem) (((pElem)->value.pValue) == STASH_TABLE_DELETED_SLOT_VALUE)
#define usesStringKeys(pTable) ((pTable)->eKeyType == StashKeyTypeStrings)


U32 stashGetKeyLength(const StashTableImp* pTable, const void* pKey)
{
	if (!pTable)
		return 0;
	if (usesStringKeys(pTable))
	{
		assert(pKey);
		return (U32)strlen(pKey);
	}
	return pTable->uiFixedKeyLength;
}

static void setExternalFunctions(StashTableImp* pTable, ExternalHashFunction hashFunc, ExternalCompareFunction compFunc)
{
	pTable->hashFunc = hashFunc;
	pTable->compFunc = compFunc;
}

#ifdef STASH_TABLE_TRACK
static void startTrackingStashTable(StashTableImp* pNewTable MEM_DBG_PARMS)
{
	StashTableImp* pList = pStashTableList;
	sprintf(pNewTable->cName, "HT: %s - %d" MEM_DBG_PARMS_CALL);
	if ( pStashTableList )
	{
		while ( pList->pNext )
			pList = pList->pNext;

		pList->pNext = pNewTable;
	}
	else
	{
		pStashTableList = pNewTable;
	}
}

static void stopTrackingStashTable(StashTableImp* pTable)
{
	// remove from list
	StashTableImp* pList = pStashTableList;
	// we don't track (or destroy, for that matter) shared stash tables
	if ( pTable->bInSharedHeap )
		return;
	assert( pList );

	if ( pList == pTable )
	{
		pStashTableList = pTable->pNext;
	}
	else
	{
		StashTableImp* pPrev = pStashTableList;
		pList = pStashTableList->pNext;
		while ( pList != pTable )
		{
			pPrev = pList;
			pList = pList->pNext;
			if ( !pList )
			{
				// we ran out?
				return;
			}
		}
		assert( pList == pTable );
		pPrev->pNext = pList->pNext;
	}
}
#endif

// -------------------------
// Table management and info
// -------------------------
#define STASH_TABLE_MIN_SIZE 8

StashTable stashTableCreateEx(U32 uiInitialSize, StashTableMode eMode, StashKeyType eKeyType, U32 uiKeyLength MEM_DBG_PARMS )
{
	StashTableImp* pNewTable;
	bool bInSharedHeap = eMode & STASH_IN_SHARED_HEAP;

	PERFINFO_AUTO_START_FUNC();

	if (!caller_fname)
		caller_fname = __FILE__, line = __LINE__;
	
	if ( bInSharedHeap )
	{
#if !PLATFORM_CONSOLE
		pNewTable = sharedCalloc(1, sizeof(StashTableImp));
#else
		assert(0);
		pNewTable = NULL;
#endif

		assertmsg(!(eMode & STASH_DEEP_COPY_KEYS_RELEASE_ON_REMOVE_BIT), "Can't have DeepCopyKeysReleaseOnRemove stashtable in shared memory");
	}
	else
		pNewTable = scalloc(1, sizeof(StashTableImp));

	MEM_DBG_STRUCT_PARMS_INIT(pNewTable);

	pNewTable->uiMinSize = STASH_TABLE_MIN_SIZE;
	if ( uiInitialSize < pNewTable->uiMinSize)
		uiInitialSize = pNewTable->uiMinSize;


#if NONP2_TABLE
	{
		U32	max_size,bits,val;

		val = MAX(uiInitialSize,pNewTable->uiMinSize);
		bits = log2(val);
		if (bits < FRAC_SHIFT)
			pNewTable->fraction = 1 << FRAC_SHIFT;
		else
		{
			pNewTable->fraction = 1 + (val >> (bits - FRAC_SHIFT));
			pNewTable->fraction = MIN(1 << FRAC_SHIFT,pNewTable->fraction);
		}
		max_size = ((1ll << bits) * pNewTable->fraction) >> FRAC_SHIFT;
		pNewTable->uiMaxSize = MAX(pNewTable->uiMinSize,max_size);
		pNewTable->uiHashMask = (1 << bits)-1;
	}
#else
	pNewTable->uiMaxSize = pow2(uiInitialSize);
	pNewTable->fraction = 1 << FRAC_SHIFT;
	pNewTable->uiHashMask = pNewTable->uiMaxSize-1;
#endif

	// allocate memory for the table
	if (bInSharedHeap)
#if !PLATFORM_CONSOLE
		pNewTable->pStorage = sharedCalloc(pNewTable->uiMaxSize, sizeof(StashElementImp));
#else
		assert(0);
#endif
	else
		pNewTable->pStorage = scalloc(pNewTable->uiMaxSize, sizeof(StashElementImp));

#ifdef STASH_TABLE_TRACK
	if ( !bInSharedHeap && !isSharedMemory(pNewTable) )
	{
		startTrackingStashTable(pNewTable MEM_DBG_PARMS_CALL);
	}
#endif

	// Set flags
	stashSetMode(pNewTable, eMode);

	pNewTable->eKeyType = eKeyType;

	switch (eKeyType)
	{
	case StashKeyTypeStrings:
		pNewTable->uiFixedKeyLength = 0;
		break;
	case StashKeyTypeInts:
		pNewTable->uiFixedKeyLength = sizeof(int);
		assert( uiKeyLength == sizeof(int));// we don't support different sizes if we are using int keys
		break;
	case StashKeyTypeFixedSize:
		pNewTable->uiFixedKeyLength = uiKeyLength;
		break;
	case StashKeyTypeExternalFunctions:
		pNewTable->uiFixedKeyLength = 0;
		break;
	case StashKeyTypeAddress:
		pNewTable->uiFixedKeyLength = sizeof(void*);
		assert(uiKeyLength == sizeof(void*));
		break;
	default:
		assert(0); // need to cover everything
		break;
	}

	pNewTable->bAllPointerValues = 1;

	PERFINFO_AUTO_STOP();

	return (StashTable)pNewTable;
}

StashTable stashTableCreateWithStringKeysEx(U32 uiInitialSize, StashTableMode eMode MEM_DBG_PARMS)
{
	return stashTableCreateEx(uiInitialSize, eMode, StashKeyTypeStrings, 0 MEM_DBG_PARMS_CALL);
}

// Assumes the int key is sizeof(int), not smaller (which is allowed)
StashTable stashTableCreateIntEx(U32 uiInitialSize MEM_DBG_PARMS)
{
	return stashTableCreateEx(uiInitialSize, StashDefault, StashKeyTypeInts, sizeof(int) MEM_DBG_PARMS_CALL);
}

// Assumes the int key is sizeof(int), not smaller (which is allowed)
StashTable stashTableCreateFixedSizeEx(U32 uiInitialSize, U32 sizeInBytes MEM_DBG_PARMS)
{
	return stashTableCreateEx(uiInitialSize, StashDefault, StashKeyTypeFixedSize, sizeInBytes MEM_DBG_PARMS_CALL);
}

// Assumes the int key is sizeof(int), not smaller (which is allowed)
StashTable stashTableCreateAddressEx(U32 uiInitialSize MEM_DBG_PARMS)
{
	return stashTableCreateEx(uiInitialSize, StashDefault, StashKeyTypeAddress, sizeof(void*) MEM_DBG_PARMS_CALL);
}

StashTable stashTableCreateExternalFunctionsEx(U32 uiInitialSize, StashTableMode eMode, ExternalHashFunction hashFunc, ExternalCompareFunction compFunc MEM_DBG_PARMS)
{
	StashTable pNewTable = stashTableCreateEx(uiInitialSize, eMode, StashKeyTypeExternalFunctions, 0 MEM_DBG_PARMS_CALL);
	assert(hashFunc && compFunc);
	setExternalFunctions((StashTableImp*)pNewTable, hashFunc, compFunc);
	return pNewTable;
}



static void destroyStringKeys(StashTableImp *pTable)
{
	StashTableIterator iter;
	StashElementImp *elem;

	assert(pTable->bDeepCopyKeys_ReleaseOnRemove);

	stashGetIterator((StashTable)pTable, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		void* pKey = elem->key.pKey;

		if (pKey)
		{
			stashTableFreeStringKey(pKey);
			elem->key.pKey = NULL;
		}
	}
}




static void destroyStorageValues(StashTableImp* pTable, Destructor keyDstr, Destructor valDstr)
{
	StashTableIterator iter;
	StashElement pElem;


	assert( keyDstr || valDstr ); // don't call this with no destructor
	assert(!pTable->bReadOnly);

	if (pTable->bDeepCopyKeys_ReleaseOnRemove)
	{
		assertmsg(!keyDstr, "Can't have a keyDstr for a DeepCopyKeys_ReleaseOnRemove table");
		destroyStringKeys(pTable);
	}

	stashGetIterator((StashTable)pTable, &iter);
	while (stashGetNextElement(&iter, &pElem))
	{
		void* pValue = pElem->value.pValue;
		void* pKey = pElem->key.pKey;

		if ( keyDstr )
			keyDstr(pKey);

		if ( valDstr )
			valDstr(pValue);
	}
}

static void destroyStorageValues_Struct(StashTableImp* pTable, Destructor keyDstr, ParseTable valueTable[])
{
	StashTableIterator iter;
	StashElement pElem;


	assert( valueTable ); // don't call this with no table
	assert(!pTable->bReadOnly);

	if (pTable->bDeepCopyKeys_ReleaseOnRemove)
	{
		assertmsg(!keyDstr, "Can't have a keyDstr for a DeepCopyKeys_ReleaseOnRemove table");
		destroyStringKeys(pTable);
	}

	stashGetIterator((StashTable)pTable, &iter);
	while (stashGetNextElement(&iter, &pElem))
	{
		void* pValue = pElem->value.pValue;
		void* pKey = pElem->key.pKey;

		if ( keyDstr )
			keyDstr(pKey);

		if (pValue && valueTable)
		{
			StructDestroyVoid(valueTable, pValue);
		}
	}
}


void stashTableDestroyEx(StashTableImp* pTable, Destructor keyDstr, Destructor valDstr )
{
	assert( keyDstr || valDstr ); // don't call this with no destructor, just call stashTableDestroy()
	if(!pTable)
		return;
	destroyStorageValues(pTable, keyDstr, valDstr);

	stashTableDestroy((StashTable)pTable);
}

void stashTableDestroyStruct(StashTable pTable, Destructor keyDstr, ParseTable valTable[])
{
	assert( valTable ); // don't call this with no destructor, just call stashTableDestroy()
	if(!pTable)
		return;
	destroyStorageValues_Struct(pTable, keyDstr, valTable);

	stashTableDestroy((StashTable)pTable);


}



void stashTableDestroy(StashTableImp* pTable )
{
	if (!pTable)
		return;
		
	PERFINFO_AUTO_START_FUNC();
	
#ifdef STASH_TABLE_TRACK
	stopTrackingStashTable(pTable);
#endif
	assert(!pTable->bReadOnly);

	assert(!pTable->bInSharedHeap); // can't remove a shared hash table...

	assert(!pTable->bCantDestroy); // can't remove an unremovable table... probably a clone with a special allocator


	// free all stored values if neccessary
	//stashTableClearImp(table, func);

	// Do not destroy the string table if the hash table is only being partially destroyed.
	if( pTable->pStringTable )
	{
		destroyStringTable( pTable->pStringTable );
	}
	else if (pTable->bDeepCopyKeys_ReleaseOnRemove)
	{
		destroyStringKeys(pTable);
	}

	// free memory being used to hold all key/value pairs
	if ( pTable->bInSharedHeap )
#if !PLATFORM_CONSOLE
		sharedFree( pTable->pStorage );
#else
		assert(0);
#endif
	else
		free( pTable->pStorage );



	if ( pTable->bInSharedHeap )
#if !PLATFORM_CONSOLE
		sharedFree( pTable );
#else
		assert(0);
#endif
	else
		free( pTable );
		
	PERFINFO_AUTO_STOP();
}

void stashTableMerge(StashTableImp* pDestinationTable, const StashTableImp* pSourceTable )
{
	StashElementImp* pElement;
	StashTableIterator iter;
	StashKeyType keyType;
	
	if (!pSourceTable)
		return;

	keyType = pSourceTable->eKeyType;
	assert( keyType == pDestinationTable->eKeyType );

	stashGetIterator((StashTable)pSourceTable, &iter);
	while (stashGetNextElement(&iter, (StashElement*)&pElement))
	{
		bool bNoCollision = false;

		switch (pSourceTable->eKeyType)
		{
		case StashKeyTypeStrings:
		case StashKeyTypeExternalFunctions:
		case StashKeyTypeFixedSize:
			{
				bNoCollision = stashAddPointer((StashTable)pDestinationTable, pElement->key.pKey, pElement->value.pValue, false);
				break;
			}
		case StashKeyTypeInts:
			{
				bNoCollision = stashIntAddPointer((StashTable)pDestinationTable, PTR_TO_S32(pElement->key.pKey), pElement->value.pValue, false);
				break;
			}
		case StashKeyTypeAddress:
			{
				bNoCollision = stashAddressAddPointer((StashTable)pDestinationTable, pElement->key.pKey, pElement->value.pValue, false);
				break;
			}
		default:
			assert(0); // need to cover everything
			break;
		}

		assert( bNoCollision );
	}
}

void stashTableClear(StashTableImp* pTable)
{
	if (!pTable)
		return;
	assert( !pTable->bReadOnly);

	if (pTable->bDeepCopyKeys_ReleaseOnRemove)
	{
		destroyStringKeys(pTable);
	}

	// clear the storage
	memset(pTable->pStorage, 0, pTable->uiMaxSize * sizeof(StashElementImp));
	pTable->uiSize = 0;
	pTable->uiValidValues = 0;

	if ( pTable->pStringTable )
		strTableClear(pTable->pStringTable);
}

void stashTableClearEx(StashTableImp* pTable, Destructor keyDstr, Destructor valDstr )
{
	if (!pTable)
		return;

	if (pTable->bDeepCopyKeys_ReleaseOnRemove)
	{
		assertmsg(!keyDstr, "Can't pass in a keyDstr to a bDeepCopyKeys_ReleaseOnRemove table");
		destroyStringKeys(pTable);
	}

	destroyStorageValues(pTable, keyDstr, valDstr);
	stashTableClear((StashTable)pTable);
}

void stashTableClearStruct(StashTableImp* pTable, Destructor keyDstr, ParseTable valTable[] )
{
	if (!pTable)
		return;

	if (pTable->bDeepCopyKeys_ReleaseOnRemove)
	{
		assertmsg(!keyDstr, "Can't pass in a keyDstr to a bDeepCopyKeys_ReleaseOnRemove table");
		destroyStringKeys(pTable);
	}

	destroyStorageValues_Struct(pTable, keyDstr, valTable);
	stashTableClear((StashTable)pTable);
}

void stashTableLock(StashTableImp* pTable)
{
	pTable->bReadOnly = 1;
}

void stashTableUnlock(StashTableImp* pTable)
{
	pTable->bReadOnly = 0;
}

// table info 
U32	stashGetCount(const StashTableImp* pTable)
{
	return pTable?pTable->uiValidValues:0; // external functions want to know how many valid elements we have
}

U32	stashGetOccupiedSlots(const StashTableImp* pTable)
{
	return pTable?pTable->uiSize:0; // in case someone wants to maintain a certain size of hash table
}

U32	stashGetMaxSize(const StashTableImp* pTable)
{
	return pTable?pTable->uiMaxSize:0;
}

static U32 stashVerifyCountInternal(const StashTableImp *pTable)
{
	U32 uiCurrentIndex;
	StashElementImp* pElement;
	U32 count = 0;

	if (!pTable)
		return 0;

	for(uiCurrentIndex = 0; uiCurrentIndex < pTable->uiMaxSize; ++uiCurrentIndex)
	{
		pElement = &pTable->pStorage[uiCurrentIndex];

		// If we have found an non-empty, non-deleted element...
		if (!slotEmpty(pElement) && !slotDeleted(pElement))
		{
			count += 1;
		}
	}

	return count;
}

bool stashTableValidateValid(const StashTableImp *pTable)
{
	bool result;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	result = !pTable || stashVerifyCountInternal(pTable)==pTable->uiValidValues;

	PERFINFO_AUTO_STOP();
	return result;
}

void stashTableSetThreadSafeLookups(StashTableImp* pTable, bool bSet)
{
	pTable->bThreadSafeLookups = !!bSet;
}

void stashTableSetCantResize(StashTableImp* pTable, bool bSet)
{
	pTable->bCantResize = !!bSet;
}

//FIXME make this function do something
void stashTableSetMinSize(StashTableImp* pTable, U32 uiMinSize)
{
	if (uiMinSize < STASH_TABLE_MIN_SIZE)
		uiMinSize = STASH_TABLE_MIN_SIZE;
	pTable->uiMinSize = uiMinSize;
	stashTableResize(pTable);
}

void stashTableFindCurrentBestSize(StashTableImp* pTable)
{
	pTable->uiMinSize = STASH_TABLE_MIN_SIZE;
	stashTableResize(pTable);
}


size_t stashGetMemoryUsage(const StashTableImp* pTable)
{
	size_t uiMemUsage = sizeof(StashTableImp) + pTable->uiMaxSize * sizeof(StashElementImp);

	assertmsg(!(pTable->bDeepCopyKeys_ReleaseOnRemove), "stashGetMemoryUsage unsupported for DeepCopyKeys_ReleaseOnRemove tables");

	if(pTable->pStringTable)
		uiMemUsage += strTableMemUsage(pTable->pStringTable);

	return uiMemUsage;
}

StashTableMode stashTableGetMode(const StashTableImp* pTable)
{
	StashTableMode eMode = StashDefault;

	if ( pTable->bDeepCopyKeys_NeverRelease	 )
		eMode |= StashDeepCopyKeys_NeverRelease;

	if ( pTable->bDeepCopyKeys_ReleaseOnRemove	 )
		eMode |= StashDeepCopyKeys_ReleaseOnRemove;

	if ( pTable->bCaseSensitive )
		eMode |= StashCaseSensitive;

	if ( pTable->bCaseInsensitive )
		eMode |= StashCaseInsensitive;


	return eMode;
}

StashKeyType stashTableGetKeyType(const StashTableImp* pTable)
{
	return pTable->eKeyType;
}

bool stashTableAreAllValuesPointers(const StashTableImp* pTable)
{
	return pTable?pTable->bAllPointerValues:false;
}

// ------------------------
// Shared memory management
// ------------------------
size_t stashGetTableImpSize(void)
{
	return sizeof(StashTableImp);
}

size_t stashGetCopyTargetAllocSize(const StashTableImp* pTable)
{
	size_t uiMemUsage = sizeof(StashTableImp) + pTable->uiMaxSize * sizeof(StashElementImp);
	if ( pTable->pStringTable )
		uiMemUsage += strTableGetCopyTargetAllocSize(pTable->pStringTable);
	return uiMemUsage;
}

StashTable stashCopyToAllocatedSpace(const StashTableImp* pTable, void* pAllocatedSpace, size_t uiTotalSize )
{
	StashTableImp* pNewTable = memset(pAllocatedSpace, 0, uiTotalSize);
	size_t uiStorageMemUsage = pTable->uiMaxSize * sizeof(StashElementImp);
	size_t uiStringTableMemUsage = (pTable->pStringTable) ? strTableGetCopyTargetAllocSize(pTable->pStringTable) : 0;

	assertmsg(!pTable->bDeepCopyKeys_ReleaseOnRemove, "StashTables with ReleaseOnRemove can not be cloned or copied to allocated space. Use DeepCopy_NeverRelease instead");

	// first, memcpy the table imp
	memcpy(pNewTable, pTable, sizeof(StashTableImp));
#ifdef STASH_TABLE_TRACK
	pNewTable->pNext = NULL;
#endif

	// the actual storage
	pNewTable->pStorage = (StashElementImp*)((char*)pNewTable + sizeof(StashTableImp));
	memcpy(pNewTable->pStorage, pTable->pStorage, uiStorageMemUsage);

	// unlock the new table, if the old was locked
	stashTableUnlock((StashTable)pNewTable);

	// The string table

	// Now, we need to make sure every hash table deep copy into the string table is adjusted for the new table
	// we can't just use pointer arithmetic, because the copied string table is compact now
	// so we use build a fresh new one
	if ( pTable->pStringTable ) 
	{
		StashTableIterator iter;
		StashElement elem;

		pNewTable->pStringTable = strTableCopyEmptyToAllocatedSpace(pTable->pStringTable, (char*)pNewTable->pStorage + uiStorageMemUsage, uiStringTableMemUsage);

		stashGetIterator((StashTable)pNewTable, &iter);

		// Go through every new element and lookup the new string pointer for it, using the stashtable we made
		while (stashGetNextElement(&iter, &elem))
		{
			StashElementImp* pNewElement = (StashElementImp*)elem;
			char *newString;
			newString = strTableAddString(pNewTable->pStringTable, pNewElement->key.pKey);
			assert(newString >= (char*)pAllocatedSpace && (newString < ((char*)pAllocatedSpace + uiTotalSize)));
			pNewElement->key.pKey = newString;
		}
	}

	return (StashTable)pNewTable;
}

StashTable stashTableClone(const StashTableImp* pTable, CustomMemoryAllocator memAllocator, void *customData)
{
	StashTableImp* pNewTable;

	if (memAllocator)
	{
		size_t uiCopySize = stashGetCopyTargetAllocSize((cStashTable)pTable);
		pNewTable = memAllocator(customData, uiCopySize);
		pNewTable = (StashTableImp*)stashCopyToAllocatedSpace((cStashTable)pTable, pNewTable, uiCopySize);
		pNewTable->bCantResize = 1;
		pNewTable->bCantDestroy = 1;
#ifdef STASH_TABLE_TRACK
		if ( !pNewTable->bInSharedHeap && !isSharedMemory(pNewTable) )
		{
			startTrackingStashTable(pNewTable, pTable->cName, 0);
		}
#endif
	}
	else
	{
		pNewTable = (StashTableImp*)stashTableCreateEx( stashGetCount((cStashTable)pTable), stashTableGetMode((cStashTable)pTable), pTable->eKeyType, pTable->uiFixedKeyLength MEM_DBG_STRUCT_PARMS_CALL(pTable));
		if ( pTable->eKeyType == StashKeyTypeExternalFunctions )
		{
			setExternalFunctions(pNewTable, pTable->hashFunc, pTable->compFunc);
		}

		// now use merge to add all elements from old table to new one
		stashTableMerge((StashTable)pNewTable, (cStashTable)pTable);
	}



	return (StashTable)pNewTable;
}

StashTable stashTableIntersectEx(const StashTableImp *tableOne, const StashTableImp *tableTwo MEM_DBG_PARMS)
{
	StashTableIterator iter;
	StashElementImp *elem;
	StashTable pNewTable;
	int max_count = MIN(stashGetCount((cStashTable)tableOne), stashGetCount((cStashTable)tableTwo));

	pNewTable = stashTableCreateEx(max_count, stashTableGetMode((cStashTable)tableOne), tableOne->eKeyType, tableOne->uiFixedKeyLength MEM_DBG_PARMS_CALL );
	if (tableOne->eKeyType == StashKeyTypeExternalFunctions)
		setExternalFunctions(pNewTable, tableOne->hashFunc, tableOne->compFunc);

	assert(stashTableGetMode((cStashTable)tableOne) == stashTableGetMode((cStashTable)tableTwo));
	assert(tableOne->eKeyType == tableTwo->eKeyType);

	stashGetIterator((StashTable)tableOne, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		switch (tableOne->eKeyType)
		{
		case StashKeyTypeStrings:
		case StashKeyTypeExternalFunctions:
		case StashKeyTypeFixedSize:
			{
				if (stashFindPointer((StashTable)tableTwo, elem->key.pKey, NULL))
					stashAddPointer((StashTable)pNewTable, elem->key.pKey, elem->value.pValue, false);
				break;
			}
		case StashKeyTypeInts:
			{
				if (stashIntFindPointer((StashTable)tableTwo, PTR_TO_S32(elem->key.pKey), NULL))
					stashIntAddPointer((StashTable)pNewTable, PTR_TO_S32(elem->key.pKey), elem->value.pValue, false);
				break;
			}
		case StashKeyTypeAddress:
			{
				if (stashAddressFindPointer((StashTable)tableTwo, elem->key.pKey, NULL))
					stashAddressAddPointer((StashTable)pNewTable, elem->key.pKey, elem->value.pValue, false);
				break;
			}
		default:
			assert(0); // need to cover everything
			break;
		}
	}

	if (stashGetCount((cStashTable)pNewTable) > 0)
		return pNewTable;

	stashTableDestroy(pNewTable);
	return NULL;
}




// ---------------------------
// Element management and info
// ---------------------------
// pointer values
void* stashElementGetPointer(StashElementImp* pElement)
{
	return pElement->value.pValue;
}

void stashElementSetPointer(StashElementImp* pElement, void* pValue)
{
	pElement->value.pValue = pValue;
}

// int values
int	stashElementGetInt(const StashElementImp* pElement)
{
	return PTR_TO_S32(pElement->value.pValue);
}

void stashElementSetInt(StashElementImp* pElement, int iValue)
{
	pElement->value.pValue = S32_TO_PTR(iValue);
}

// float values
float stashElementGetFloat(const StashElementImp* pElement)
{
	return PTR_TO_F32(pElement->value.pValue);
}

void stashElementSetFloat(StashElementImp* pElement, float fValue)
{
	pElement->value.pValue = F32_TO_PTR(fValue);
}

// keys
char*	stashElementGetStringKey(const StashElementImp* element)
{
	// this should be const, but i can't deal with the const avalanche right now..
	return (char*)element->key.pKey;
}

int	stashElementGetIntKey(const StashElementImp* element)
{
	return PTR_TO_S32(element->key.pKey);
}

U32	stashElementGetU32Key(const StashElementImp* element)
{
	return PTR_TO_U32(element->key.pKey);
}

void* stashElementGetKey(const StashElementImp* element)
{
	return element->key.pKey;
}

// Iterators
void stashGetIterator(StashTableImp* pTable, StashTableIterator* pIter)
{
	if (pTable)
		assert(!pTable->bReadOnly);
	pIter->pTable = pTable;
	pIter->uiIndex = 0;
}

bool stashGetNextElement(StashTableIterator* pIter, StashElementImp** ppElem)
{
	U32 uiCurrentIndex;
	StashTableImp* pTable = pIter->pTable;
	StashElementImp* pElement;

	if (!pTable)
		return false;

	assert(!pTable->bReadOnly);

	// Look through the table starting at the index specified by the iterator.
	for(uiCurrentIndex = pIter->uiIndex; uiCurrentIndex < pTable->uiMaxSize; ++uiCurrentIndex)
	{
		pElement = &pTable->pStorage[uiCurrentIndex];

		// If we have found an non-empty, non-deleted element...
		if (!slotEmpty(pElement) && !slotDeleted(pElement))
		{
			pIter->uiIndex = uiCurrentIndex + 1;
			if (ppElem)
				*ppElem = pElement;
			return true;
		}
	}
	return false;
}

void stashForEachElement(StashTableImp* pTable, StashElementProcessor proc)
{
	StashTableIterator iter;
	StashElement elem;

	stashGetIterator((StashTable)pTable, &iter);

	while (stashGetNextElement(&iter, &elem))
	{
		if ( !proc(elem) )
			return;
	}
}

void stashForEachElementEx(StashTableImp* pTable, StashElementProcessorEx proc, void* userdata)
{
	StashTableIterator iter;
	StashElement elem;

	stashGetIterator((StashTable)pTable, &iter);

	while (stashGetNextElement(&iter, &elem))
	{
		if ( !proc(userdata, elem) )
			return;
	}
}

static void stashRemoveElementByIndexInternal(StashTable pTable,U32 uiStorageIndex)
{
	if (pTable->bDeepCopyKeys_ReleaseOnRemove)
	{
		stashTableFreeStringKey((char*)pTable->pStorage[uiStorageIndex].key.pKey);
	}

	pTable->pStorage[uiStorageIndex].value.pValue = (void*)STASH_TABLE_DELETED_SLOT_VALUE;
	pTable->uiValidValues--;
	pTable->pLastKey = 0;
	if ( pTable->bDeepCopyKeys_NeverRelease	 )
	{
	//	Errorf("You can't remove from a deep copy stash table, or it will cause a leak in the string table\n");

		// Instead, just log that you'd like to delete this string
		if ( pTable->pStringTable )
		{
			strTableLogRemovalRequest(pTable->pStringTable, (char*)pTable->pStorage[uiStorageIndex].key.pKey );
		}
	}
}

bool stashRemoveElementByIndex(StashTable pTable,U32 uiStorageIndex)
{
	devassert(!pTable->bThreadLocked);		// You are using this stash table in multiple threads without any safety checks.  Stop it!
	pTable->bThreadLocked = true;

	// magical decrement by 1
	stashRemoveElementByIndexInternal(pTable,uiStorageIndex-1);

	pTable->bThreadLocked = false;
	
	return true;
}

#if DO_LOGIT
extern int g_mem_system_initted;

static	U32		hist_found[1000];
static	U32		hist_added[1000];
static	int		total_count;


void logit(int count,char *added,char *str)
{
	char	buf[2000];
	static FILE	*file;
	static FILE	*hist_file;

	if (!g_mem_system_initted)
		return;

	total_count++;
	if (!file)
		file = fopen("c:/temp/hash.log","wb");
	if (!hist_file)
		hist_file = fopen("c:/temp/hist.log","wb");

	if (added[0] == 'A')
		hist_added[strlen(str)/10]++;
	else
		hist_found[strlen(str)/10]++;

	if (!(total_count % 100000))
	{
		int		i;

		fwrite("\n\nADDED\n",strlen("\n\nADDED\n"),1,hist_file);
		for(i=0;i<30;i++)
		{
			sprintf(buf,"%d-%d\t%d\n",i*10,(i+1)*10-1,hist_added[i]);
			fwrite(buf,strlen(buf),1,hist_file);
		}

		fwrite("\n",strlen("\n"),1,hist_file);
		for(i=0;i<30;i++)
		{
			sprintf(buf,"%d-%d\t%d\n",i*10,(i+1)*10-1,hist_found[i]);
			fwrite(buf,strlen(buf),1,hist_file);
		}
	}

	sprintf(buf,"%d %s %s\n",count,added,str);
	fwrite(buf,strlen(buf),1,file);
}
#endif

DEF_INLINE static bool stashKeysAreEqualInternal(const StashTableImp* pTable, const void* pKeyA, const void* pKeyB)
{
	switch (pTable->eKeyType)
	{
	case StashKeyTypeStrings:
		// It's a string, do a comparison
		if (pKeyA == pKeyB)
		{
			return true;
		}
		else
		{
			if ( !pTable->bCaseSensitive )
			{
				if ( inline_stricmp( (char*)pKeyA, (char*)pKeyB ) == 0 )
					return true;
			}
			else
			{
				if ( strcmp( (char*)pKeyA, (char*)pKeyB ) == 0 )
					return true;
			}
		}
		break;
	case StashKeyTypeInts:
		// It's an int, so just compare the two
		if ( pKeyA == pKeyB )
			return true;
		break;
	case StashKeyTypeFixedSize:
		if (pTable->bCaseInsensitive)
		{
			if ( _memicmp(pKeyA, pKeyB, pTable->uiFixedKeyLength) == 0)
				return true;	
		}
		else
		{
			if ( memcmp(pKeyA, pKeyB, pTable->uiFixedKeyLength) == 0)
				return true;	
		}
		break;
	case StashKeyTypeExternalFunctions:
		if ( pTable->compFunc(pKeyA, pKeyB) == 0)
			return true;
		break;
	case StashKeyTypeAddress:
		// It's an address, so just compare the two
		if ( pKeyA == pKeyB )
			return true;
		break;
	default:
		assert(0); // need to cover everything
		break;
	}

	return false;
}

bool stashKeysAreEqual(const StashTableImp* pTable, const void* pKeyA, const void* pKeyB)
{
	return stashKeysAreEqualInternal(pTable, pKeyA, pKeyB);
}

bool stashIntValueIsValid(int iValue)
{
	return (void*)(uintptr_t)iValue != STASH_TABLE_DELETED_SLOT_VALUE;
}

#if STASH_TIMING
U64 total_probes;
U64	total_searches;
U64 search_types[StashKeyTypeMax];
U64 search_types_probes[StashKeyTypeMax];

static int stash_timer;
F32 stash_elapsed;

// Instructions:
// 1. Recompile with STASH_TIMING.
// 2. Place a call to hitme() at the point where you want to stop timing.  See example in gslQueueGeneralUpdateForLink().
// 3. Start the process to be timed, and wait for hitme() to be hit.
void hitme()
{
	int i;
	stash_elapsed = timerElapsed(stash_timer);
	printf("\n\n"
		"Ratio:             %f\n"
		"total_probes:      %"FORM_LL"u\n"
		"total_searches:    %"FORM_LL"u\n"
		"stash_elapsed (s): %f\n",
		1 + (double) total_probes / total_searches, total_probes, total_searches, stash_elapsed);
	printf("  %20s: %10s - %10s - Ratio\n", "Type", "Probes", "Searches");
	for (i = 0; i != StashKeyTypeMax; ++i)
	{
		const char *key = "???";
		switch (i)
		{
			case StashKeyTypeStrings:
				key = "Strings";
				break;
			case StashKeyTypeInts:
				key = "Ints";
				break;
			case StashKeyTypeFixedSize:
				key = "FixedSize";
				break;
			case StashKeyTypeExternalFunctions:
				key = "ExternalFunctions";
				break;
			case StashKeyTypeAddress:
				key = "Address";
				break;
		}
		printf("  %20s: %10"FORM_LL"u - %10"FORM_LL"u - %f\n", key, search_types_probes[i], search_types[i], search_types[i] ? 1 + (double) search_types_probes[i] / search_types[i] : 0);
	}
	for(;;)
		printf("");
}
#endif

/*
		grow 1.5 + linear probe + string rehash
		1 + (double) total_probes / total_searches	2.2282027501307606	double
		total_probes	61318166	unsigned __int64
		total_searches	49925117	unsigned __int64
		stash_elapsed	61.815651	float

		grow 1.5 + linear probe
		1 + (double) total_probes / total_searches	2.3487856819190718	double
		total_probes	67321264	unsigned __int64
		total_searches	49912499	unsigned __int64
		stash_elapsed	61.514030	float

		grow 1.5 + linear probe + rehash
		1 + (double) total_probes / total_searches	2.1095106091386802	double
		total_probes	55374251	unsigned __int64
		total_searches	49908717	unsigned __int64
		stash_elapsed	63.783020	float

		grow 1.5 + triangular probe
		1 + (double) total_probes / total_searches	1.9588113888810514	double
		total_probes	47847419	unsigned __int64
		total_searches	49902848	unsigned __int64
		stash_elapsed	63.931911	float

		30M
		grow 1.5 + linear probe
		1 + (double) total_probes / total_searches	1.9587893013736899	double
		total_probes	28763680	unsigned __int64
		total_searches	30000001	unsigned __int64
		stash_elapsed	47.546013	float

		1 + (double) total_probes / total_searches	1.6573449114218364	double
		total_probes	19720348	unsigned __int64
		total_searches	30000001	unsigned __int64
		stash_elapsed	49.377922	float

		grow 1.5 + double hash hackery
		1 + (double) total_probes / total_searches	1.6130588462313717	double
		total_probes	18391766	unsigned __int64
		total_searches	30000001	unsigned __int64
		stash_elapsed	51.506626	float

		grow 1.5 + hackery
		1 + (double) total_probes / total_searches	1.6367012121099596	double
		total_probes	19101037	unsigned __int64
		total_searches	30000001	unsigned __int64
		stash_elapsed	46.542343	float

		grow 1.5
		1 + (double) total_probes / total_searches	1.6995866766804442	double
		total_probes	20987601	unsigned __int64
		total_searches	30000001	unsigned __int64
		stash_elapsed	48.691872	float

		old style + triangular
		1 + (double) total_probes / total_searches	1.5426966485767784	double
		total_probes	16280900	unsigned __int64
		total_searches	30000001	unsigned __int64
		stash_elapsed	49.230511	float

		old style + quadratic
		1 + (double) total_probes / total_searches	1.5383615487212818	double
		total_probes	16150847	unsigned __int64
		total_searches	30000001	unsigned __int64
		stash_elapsed	51.406136	float


*/

static DEF_INLINE bool stashFindIndexByHashInternal(const StashTableImp* pTable, StashKey key, U32 uiHashValue, U32* puiIndex, int* piFirstDeletedIndex)
{
	U32 uiNumProbes = 1; // the first probe
	U32 uiStorageIndex;

	uiStorageIndex = stashMaskTableIndex(pTable, uiHashValue);

#if STASH_TIMING
	if (!total_searches)
		stash_timer = timerAlloc();
	total_searches++;
	search_types[pTable->eKeyType]++;
#endif
	while ( 1 )
	{
		const StashElementImp* pElement;

		PREFETCH(&pTable->pStorage[uiStorageIndex]);

		// Check to see if the new index is the correct index or not
		pElement = &pTable->pStorage[uiStorageIndex];


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
				if (stashKeysAreEqualInternal(pTable, key.pKey, pElement->key.pKey))
				{
					if ( puiIndex )
						*puiIndex = uiStorageIndex;
					if (!pTable->bCantDestroy && !pTable->bThreadSafeLookups) // shared memory
					{
						((StashTableImp*)pTable)->lastIndex = uiStorageIndex;
						((StashTableImp*)pTable)->pLastKey = pElement->key.pKey;
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
		// Use quadratic probing
		{
#if NONP2_TABLE
#if 0
			//j = ((5*j) + 1) mod 2**i
			if (uiNumProbes == 1) // a little hackery to mix the bits unused in originally picking the slot to reduce collisions
				uiStorageIndex = (uiStorageIndex + (uiHashValue >> 29)) & pTable->uiHashMask;
			uiProbeOffset = (uiProbeOffset + uiNumProbes) & pTable->uiHashMask;
#endif
			//if (uiNumProbes == 1 && pTable->eKeyType == StashKeyTypeStrings)
			//	uiStorageIndex = (uiStorageIndex + (uiHashValue >> 29)) & pTable->uiHashMask;
			uiStorageIndex++;
			if (uiStorageIndex >= pTable->uiMaxSize)
				uiStorageIndex -= pTable->uiMaxSize;
			uiNumProbes++;
#else
			U32 uiProbeOffset = uiNumProbes * uiNumProbes;
			uiNumProbes++;
			uiStorageIndex = stashMaskTableIndex(pTable, (uiStorageIndex + uiProbeOffset) );
#endif
#if STASH_TIMING
			total_probes++;
			search_types_probes[pTable->eKeyType]++;
#endif
		}

		// Try again, now with a new index
        
        // detect lockup due to stomps etc...
        assert(uiNumProbes <= pTable->uiMaxSize);
	}
}

/* stash perf results from starting up an STO mission map
1	1
3	3
6	
10
linear probing, 66-75% load: 2.3
linear probing, 37-75% load: 1.77
quadratic probing, 37-75% load: 1.61

triangular probing, 37-75% load: 1.62
triangular probing, 66-75% load: 1.93
triangular probing, 57-75% load: 1.85
triangular probing + hackery, 66-75% load: 1.89
triangular probing + hackery, 57-75% load: 1.78


37-75% load
		total_probes	27704792	unsigned __int64
		toal_searches	44326320	unsigned __int64

66-75% load
		total_probes	45685828	unsigned __int64
		toal_searches	48894113	unsigned __int64


*/

// ----------------------------------
// Internal hash processing functions
// ----------------------------------
// This is the real magic behind the hash table, here we translate a key into a hash table index, 
// and deal with hash table index collisions (in this case through quadratic probing)
// 
// returns true if it was already in the table, false otherwise
// will always find a value
static DEF_INLINE bool stashFindIndexByKeyInternal(const StashTableImp* pTable, StashKey key, U32* puiIndex, int* piFirstDeletedIndex,U32 *hash_p)
{
	U32 uiHashValue = 0;
	bool bRet;

	PERFINFO_AUTO_START_FUNC_L2(); // JE: Changed these to _L2 versions, they were adding around 20ms/frame when doing timer record, greatly skewing the results.  #define PERFINFO_L1 in timing.h to enable

	if ( piFirstDeletedIndex )
		*piFirstDeletedIndex = -1; // no deleted found yet

	// don't try to find an empty key
	if ( key.pKey == STASH_TABLE_EMPTY_SLOT_KEY ) // sorry, can't find an empty key
	{
		PERFINFO_AUTO_STOP_L2();
		return false;
	}

	// Check the cached last key first.

	if(pTable->pLastKey == key.pKey)
	{
		// Make a copy of lastIndex so another thread can't change it.

		const U32 lastIndex = pTable->lastIndex;

		if(	pTable->pStorage[lastIndex].key.pKey == key.pKey &&
			!slotDeleted(&pTable->pStorage[lastIndex]))
		{		
			if (puiIndex)
				*puiIndex = lastIndex;
			if (hash_p)
#if CACHE_HASHES
				*hash_p = pTable->pStorage[lastIndex].hash;
#else
				*hash_p = 0;
#endif
			PERFINFO_AUTO_STOP_L2();
			return true;
		}
	}

	switch (pTable->eKeyType)
	{
	xcase StashKeyTypeStrings:
		if ( pTable->bCaseSensitive)
	       	uiHashValue = stashDefaultHash_inline(key.pKey, (int)strlen(key.pKey), DEFAULT_HASH_SEED);
		else // this version of the hash function sets the lowercase bit, so it does not distinguish between the two possibilities
			uiHashValue = stashDefaultHashCaseInsensitive_inline(key.pKey, (int)strlen(key.pKey), DEFAULT_HASH_SEED);
	xcase StashKeyTypeInts:
		{
			U32 i = PTR_TO_U32(key.pKey);
			uiHashValue = MurmurHash2Int(i);
		}
	xcase StashKeyTypeFixedSize:
		if ( pTable->bCaseInsensitive)
			uiHashValue = stashDefaultHashCaseInsensitive_inline(key.pKey, pTable->uiFixedKeyLength, DEFAULT_HASH_SEED);
		else
			uiHashValue = stashDefaultHash_inline(key.pKey, pTable->uiFixedKeyLength, DEFAULT_HASH_SEED);
	xcase StashKeyTypeExternalFunctions:
		uiHashValue = pTable->hashFunc(key.pKey, DEFAULT_HASH_SEED);
	xcase StashKeyTypeAddress:
		uiHashValue = MurmurHash2Pointer_inline((void*)&key.pKey, DEFAULT_HASH_SEED);
	xdefault:
		assert(0); // need to cover everything
	}
	if (hash_p)
		*hash_p = uiHashValue;

	bRet = stashFindIndexByHashInternal(pTable, key, uiHashValue, puiIndex, piFirstDeletedIndex);
	PERFINFO_AUTO_STOP_L2();
	return bRet;
}

DEF_INLINE bool stashFindValueInternal(const StashTableImp* pTable, StashKey key, StashValue* pValue)
{
	U32 uiStorageIndex;

	if (!stashFindIndexByKeyInternal(pTable, key, &uiStorageIndex, NULL, NULL))
	{
		if ( pValue )
			pValue->pValue = 0;
		return false;
	}

	if (pValue)
		pValue->pValue = pTable->pStorage[uiStorageIndex].value.pValue;
	return true;
}

DEF_INLINE bool stashFindElementInternal(const StashTableImp* pTable, StashKey key, StashElementImp** ppElement)
{
	U32 uiStorageIndex;

	if (!stashFindIndexByKeyInternal(pTable, key, &uiStorageIndex, NULL, NULL))
	{
		if ( ppElement ) *ppElement = NULL;
		return false;
	}

	if ( ppElement ) *ppElement = &pTable->pStorage[uiStorageIndex];

	return true;
}

DEF_INLINE static bool stashFindElementInternalConst(const StashTableImp* pTable, StashKey key, const StashElementImp** ppElement)
{
	U32 uiStorageIndex;

	if (!stashFindIndexByKeyInternal(pTable, key, &uiStorageIndex, NULL, NULL))
	{
		*ppElement = NULL;
		return false;
	}

	*ppElement = &pTable->pStorage[uiStorageIndex];

	return true;
}

static bool stashFindKeyByIndexInternal(const StashTableImp* pTable, U32 uiIndex, StashKey* pKey)
{
	if ( uiIndex >= pTable->uiMaxSize ||slotEmpty(&pTable->pStorage[uiIndex]) || slotDeleted(&pTable->pStorage[uiIndex]) )
	{
		pKey->pKey = 0;
		return false;
	}

	pKey->pKey = pTable->pStorage[uiIndex].key.pKey;
	return true;
}

DEF_INLINE static bool stashAddValueInternalNoLock(StashTableImp* pTable, StashKey key, StashValue value, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	U32 uiStorageIndex;
	int iFirstDeletedIndex;
	bool bFound;
	StashElementImp* pElement;
	U32 hash;

	PERFINFO_AUTO_START_FUNC_L2();

	assert( value.pValue != STASH_TABLE_DELETED_SLOT_VALUE ); // sorry, can't add the one value that means it's a deleted slot!
	assert( key.pKey != STASH_TABLE_EMPTY_SLOT_KEY ); // sorry, can't add the one key that means it's an empty slot!
	
	bFound = stashFindIndexByKeyInternal(pTable, key, &uiStorageIndex, &iFirstDeletedIndex, &hash);
	if ( bFound && !bOverwriteIfFound )
	{
		if ( ppElement )
			*ppElement = &pTable->pStorage[uiStorageIndex];
		PERFINFO_AUTO_STOP_L2();
		return false;
	}
	else if ( bFound && pTable->bDeepCopyKeys_NeverRelease	 )
	{
		//optimization 7/2013 - just leave the key in place
	}

	// Otherwise, copy it in and return true;

	if (!bFound && iFirstDeletedIndex > -1)
	{
		// we couldn't find the key, but found this nice deleted slot on the way to the first empty slot
		// use this instead, so we don't waste the deleted slot 
		uiStorageIndex = (U32)iFirstDeletedIndex;
		pTable->uiSize--; // Counteract increase, because we're reusing a slot
	}

	pElement = &pTable->pStorage[uiStorageIndex];

#if CACHE_HASHES
	pElement->hash = hash;
#endif
	if ( slotDeleted(pElement) || slotEmpty(pElement) )
	{
		// it was not a valid slot, now it will be
		pTable->uiValidValues++;
	}

	// copy the value
	pElement->value.pValue = value.pValue;

	if (usesStringKeys(pTable))
	{
		if (pTable->bDeepCopyKeys_NeverRelease)
		{
			//if bFound was true, then we already have the key in this slot pointing at the right thing, so adding 
			//again would just leak memory
			if (!bFound)
			{
				// deep copy the string
				assert( pTable->pStringTable );
				pElement->key.pKey = strTableAddString(pTable->pStringTable, key.pKey);
			}
		}
		else if (pTable->bDeepCopyKeys_ReleaseOnRemove)
		{
			//if it was bFound, then do nothing, otherwise strdup the passed-in key
			if (!bFound)
			{
				pElement->key.pKey = stashTableStrDup(key.pKey);
			}
		}
		else
		{
			pElement->key.pKey = key.pKey;
		}
	}
	else
	{
		// just copy whatever is in the key field, be it a pointer or int
		pElement->key.pKey = key.pKey;
	}

	// We just added an element to our hash table. Increment the size, and check if we need to resize
	if ( !bFound ) // don't add it if it's found in table, it means we are overwriting and there is no increase in size
	{
		pTable->uiSize++;
	}

	// If we are 75% or more full, resize so that storage index hash collision is minimized
	if ( pTable->uiSize >= ((pTable->uiMaxSize >> 1) + (pTable->uiMaxSize >> 2))  )
	{
		stashTableResize(pTable);
		if ( ppElement )
		{
			bFound = stashFindIndexByHashInternal(pTable, key, hash, &uiStorageIndex, NULL);
			devassert(bFound);
			*ppElement = &pTable->pStorage[uiStorageIndex];
		}
	}
	else if ( ppElement )
		*ppElement = pElement;

	PERFINFO_AUTO_STOP_L2();
	return true;
}

DEF_INLINE static bool stashAddValueInternal(StashTableImp* pTable, StashKey key, StashValue value, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	bool bResult;
	devassert(!pTable->bThreadLocked);		// You are using this stash table in multiple threads without any safety checks.  Stop it!
	pTable->bThreadLocked = true;
	bResult = stashAddValueInternalNoLock(pTable,key,value,bOverwriteIfFound,ppElement);
	pTable->bThreadLocked = false;

	return bResult;
}


static void stashTableResize(StashTableImp* pTable)
{
	U32 uiOldMaxSize = pTable->uiMaxSize;
	U32 uiOldValidValues = pTable->uiValidValues;
	StashElementImp* pOldStorage = pTable->pStorage;
	U32 uiOldIndex;
	bool bWasDeepCopy_NeverRelease = pTable->bDeepCopyKeys_NeverRelease	;
	bool bWasDeepCopy_ReleaseOnRemove = pTable->bDeepCopyKeys_ReleaseOnRemove;

	if(PERFINFO_RUN_CONDITIONS){
		PERFINFO_AUTO_START("stash table resize", 1);
		if(!pTable->uiValidValues){
			PERFINFO_AUTO_START("stash table resize 0", 1);
		}
		#define START_TIMER(x) else if(pTable->uiValidValues <= x){PERFINFO_AUTO_START("stash table resize <= "#x, 1);}
		#define START_TIMER_NAME(x, y) else if(pTable->uiValidValues <= x){PERFINFO_AUTO_START("stash table resize <= "y, 1);}
		START_TIMER(10)
		START_TIMER(20)
		START_TIMER(30)
		START_TIMER(40)
		START_TIMER(50)
		START_TIMER(60)
		START_TIMER(70)
		START_TIMER(80)
		START_TIMER(90)
		START_TIMER(100)
		START_TIMER(150)
		START_TIMER(200)
		START_TIMER(250)
		START_TIMER(300)
		START_TIMER(350)
		START_TIMER(400)
		START_TIMER(450)
		START_TIMER(500)
		START_TIMER(550)
		START_TIMER(600)
		START_TIMER(650)
		START_TIMER(700)
		START_TIMER(750)
		START_TIMER(800)
		START_TIMER(850)
		START_TIMER(900)
		START_TIMER(950)
		START_TIMER_NAME(1000, "1,000")
		START_TIMER_NAME(2000, "2,000")
		START_TIMER_NAME(3000, "3,000")
		START_TIMER_NAME(4000, "4,000")
		START_TIMER_NAME(5000, "5,000")
		START_TIMER_NAME(6000, "6,000")
		START_TIMER_NAME(7000, "7,000")
		START_TIMER_NAME(8000, "8,000")
		START_TIMER_NAME(9000, "9,000")
		START_TIMER_NAME(10000, "10,000")
		START_TIMER_NAME(20000, "20,000")
		START_TIMER_NAME(30000, "30,000")
		START_TIMER_NAME(40000, "40,000")
		START_TIMER_NAME(50000, "50,000")
		START_TIMER_NAME(60000, "60,000")
		START_TIMER_NAME(70000, "70,000")
		START_TIMER_NAME(80000, "80,000")
		START_TIMER_NAME(90000, "90,000")
		START_TIMER_NAME(100000, "100,000")
		START_TIMER_NAME(200000, "200,000")
		START_TIMER_NAME(300000, "300,000")
		START_TIMER_NAME(400000, "400,000")
		START_TIMER_NAME(500000, "500,000")
		START_TIMER_NAME(600000, "600,000")
		START_TIMER_NAME(700000, "700,000")
		START_TIMER_NAME(800000, "800,000")
		START_TIMER_NAME(900000, "900,000")
		START_TIMER_NAME(1000000, "1,000,000")
		START_TIMER_NAME(2000000, "2,000,000")
		START_TIMER_NAME(3000000, "3,000,000")
		START_TIMER_NAME(4000000, "4,000,000")
		START_TIMER_NAME(5000000, "5,000,000")
		START_TIMER_NAME(6000000, "6,000,000")
		START_TIMER_NAME(7000000, "7,000,000")
		START_TIMER_NAME(8000000, "8,000,000")
		START_TIMER_NAME(9000000, "9,000,000")
		START_TIMER_NAME(10000000, "10,000,000")
		else{
			PERFINFO_AUTO_START("stash table resize > 10M", 1);
		}
		#undef START_TIMER
		#undef START_TIMER_NAME
	}

	assert(!pTable->bCantResize); // can't resize!
	
	// The new size should be based off of the number of valid values, not the size
	// since the size includes deleted elems which will not find their way into the
	// the new table
	//
#if NONP2_TABLE
	{
		U32	max_size,bits,val,growth;

		growth = 1 + (uiOldValidValues * 3) / 2;
		if (growth == uiOldValidValues)
			growth = uiOldValidValues << 1;
		val = MAX(growth,pTable->uiMinSize);
		bits = log2(val);
		if (bits < FRAC_SHIFT)
			pTable->fraction = 1 << FRAC_SHIFT;
		else
		{
			pTable->fraction = 1 + (val >> (bits - FRAC_SHIFT));
			pTable->fraction = MIN(1 << FRAC_SHIFT,pTable->fraction);
		}
		max_size = ((1ll << bits) * pTable->fraction) >> FRAC_SHIFT;
		pTable->uiMaxSize = MAX(pTable->uiMinSize,max_size);
		pTable->uiHashMask = (1 << bits)-1;
	}
#else
	pTable->uiMaxSize = MAX(pTable->uiMinSize, (U32)pow2(uiOldValidValues<<1));
	pTable->uiHashMask = pTable->uiMaxSize-1;
	pTable->fraction = 1 << FRAC_SHIFT;

#endif
#ifdef STASH_TABLE_TRACK
	if (uiOldMaxSize > pTable->uiMaxSize)
		printf("");
	if (pTable->uiMaxSize > 16384)
		printf("");
	printf("stashresize %s %d -> %d\n",pTable->cName,uiOldMaxSize,pTable->uiMaxSize);
#endif

	pTable->uiSize = 0; // this will increase as we add to it
	pTable->uiValidValues = 0;
	pTable->bDeepCopyKeys_NeverRelease	 = 0; // we need to make shallow copies for this stage, since we already made the deep copies
	pTable->bDeepCopyKeys_ReleaseOnRemove	 = 0; // we need to make shallow copies for this stage, since we already made the deep copies
	pTable->pLastKey = 0; // disable last key cache

	if ( pTable->bInSharedHeap )
#if !PLATFORM_CONSOLE
		pTable->pStorage = sharedCalloc(pTable->uiMaxSize, sizeof(StashElementImp));
#else
		assert(0);
#endif
	else
		pTable->pStorage = stcalloc(pTable->uiMaxSize, sizeof(StashElementImp), pTable);

	// Go through every old element
	for (uiOldIndex=0; uiOldIndex < uiOldMaxSize; ++uiOldIndex)
	{
		StashElementImp* pOldElement = &pOldStorage[uiOldIndex];
		bool bSuccess = false;
		
		if ( slotEmpty(pOldElement) || slotDeleted(pOldElement))
			continue; // don't add deleted or unused slots

		// put it in the new table, since it must be valid at this point
		bSuccess  = stashAddValueInternalNoLock(pTable, pOldElement->key, pOldElement->value, false, NULL);
		assert(bSuccess); // otherwise, we're trying to add element twice somehow
	}

	assert(
		pTable->uiValidValues == pTable->uiSize 
		&& uiOldValidValues == pTable->uiValidValues
		); // should always be equal after a resize

	pTable->bDeepCopyKeys_NeverRelease	 = bWasDeepCopy_NeverRelease; // be sure to restore the deep copy status
	pTable->bDeepCopyKeys_ReleaseOnRemove	 = bWasDeepCopy_ReleaseOnRemove; // be sure to restore the deep copy status

	// Free old storage
	if ( pTable->bInSharedHeap)
#if !PLATFORM_CONSOLE
		sharedFree(pOldStorage);
#else
		assert(0);
#endif
	else
		free(pOldStorage);

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

static bool stashRemoveValueInternal(StashTableImp* pTable, StashKey key, StashValue* pValue)
{
	U32 uiStorageIndex;

	devassert(!pTable->bThreadLocked);		// You are using this stash table in multiple threads without any safety checks.  Stop it!
	pTable->bThreadLocked = true;
	if ( !stashFindIndexByKeyInternal(pTable, key, &uiStorageIndex, NULL, NULL))
	{
		if ( pValue ) pValue->pValue = 0;
		pTable->bThreadLocked = false;
        return false;
	}
	if ( pValue )
		pValue->pValue = pTable->pStorage[uiStorageIndex].value.pValue;

	stashRemoveElementByIndexInternal(pTable,uiStorageIndex);

	pTable->bThreadLocked = false;
	
	return true;
}



int stashTableVerifyStringKeys(StashTable pTable) // Checks that no keys have been corrupted
{
	StashTableIterator iter;
	StashElement elem;
	int ret = 1;

	assert(pTable->eKeyType == StashKeyTypeStrings);

	stashGetIterator((StashTable)pTable, &iter);

	while (stashGetNextElement(&iter, &elem))
	{
		char *pKey = stashElementGetStringKey(elem);
		U32 uiActualIndex = iter.uiIndex - 1;
		U32 uiStorageIndex;
		if (stashFindIndexByKeyInternal(pTable, elem->key, &uiStorageIndex, NULL, NULL)) {
			// Verify that this index is the one we're looking at
			if (uiActualIndex != uiStorageIndex) {
				OutputDebugStringf("String in stash table has been changed to \"%s\", which is at both index %d and %d!", pKey, uiActualIndex, uiStorageIndex);
				ret = 0;
			}
		} else {
			// The index doesn't match!  It's gone bad!
			OutputDebugStringf("String in stash table has been changed to \"%s\", which was not found in the stash table!", pKey);
			ret = 0;
		}
	}
	return ret;
}



// -----------
// Pointer Keys
// -----------
bool stashFindElement(StashTableImp* pTable, const void* pKey, StashElementImp** pElement)
{
	if (!pTable)
	{
		if (pElement)
			*pElement = NULL;
		return false;
	}
	assert(pTable->eKeyType != StashKeyTypeInts);
	assert(!pTable->bReadOnly);
	return stashFindElementInternal(pTable, *((StashKey*)&pKey), pElement);
}

bool stashFindElementConst(const StashTableImp* pTable, const void* pKey, const StashElementImp** pElement)
{
	if (!pTable)
	{
		if (pElement)
			*pElement = NULL;
		return false;
	}
	assert(pTable->eKeyType != StashKeyTypeInts);
	return stashFindElementInternalConst(pTable, *((StashKey*)&pKey), pElement);
}

bool stashFindIndexByKey(const StashTableImp* pTable, const void* pKey, U32* puiIndex)
{
	assert(pTable);
	assert(pTable->eKeyType != StashKeyTypeInts);
	return stashFindIndexByKeyInternal(pTable, *((StashKey*)&pKey), puiIndex, NULL, NULL);
}

bool stashGetKey(const StashTableImp* pTable, const void* pKeyIn, const void** pKeyOut)
{
	StashElementImp* pElement;
	if (!pTable)
		return false;
	assert(pTable->eKeyType != StashKeyTypeInts);
	if (!stashFindElementInternal(pTable, *((StashKey*)&pKeyIn), &pElement))
		return false;
	if (pKeyOut)
		*pKeyOut = pElement->key.pKey;
	return true;
}

bool stashFindKeyByIndex(const StashTableImp* pTable, U32 uiIndex, const void** pKeyOut)
{
	if (!pTable)
		return false;
	assert(pTable->eKeyType != StashKeyTypeInts);
	return stashFindKeyByIndexInternal(pTable, uiIndex, (StashKey*)pKeyOut);
}


// pointer values
bool stashAddPointer(StashTableImp* pTable, const void* pKey, const void* pValue, bool bOverwriteIfFound)
{
	assert(pTable && !pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), *((StashValue*)&pValue), bOverwriteIfFound, NULL);
}

bool stashAddPointerAndGetElement(StashTableImp* pTable, const void* pKey, void* pValue, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	assert(pTable && !pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), *((StashValue*)&pValue), bOverwriteIfFound, ppElement);
}

bool stashRemovePointer(StashTableImp* pTable, const void* pKey, void** ppValue)
{
	assert(pTable && !pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	return stashRemoveValueInternal(pTable, *((StashKey*)&pKey), (StashValue*)ppValue);
}

bool stashFindPointer(const StashTableImp* pTable, const void* pKey, void** ppValue)
{
	if (!pTable)
	{
		if (ppValue)
			*ppValue = NULL;
		return false;
	}
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	return stashFindValueInternal(pTable, *((StashKey*)&pKey),(StashValue*)ppValue);
}

bool stashFindPointerConst(const StashTableImp* pTable, const void* pKey, const void** ppValue)
{
	if (!pTable)
	{
		if (ppValue)
			*ppValue = NULL;
		return false;
	}
	assert(pTable->eKeyType != StashKeyTypeInts);
	return stashFindValueInternal(pTable, *((StashKey*)&pKey), (StashValue*)ppValue);
}

// please don't use, for backwards-compatibility only	
void* stashFindPointerReturnPointer(const StashTableImp* table, const void* pKey) 
{
	void* pResult;
	if ( table && stashFindPointer((cStashTable)table, pKey, &pResult) )
		return pResult;
	return NULL;
}


// int values
bool stashAddInt(StashTableImp* pTable, const void* pKey, int iValue, bool bOverwriteIfFound)
{
	StashValue value;
	value.pValue = S32_TO_PTR(iValue);
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), value, bOverwriteIfFound, NULL);
}

bool stashAddIntAndGetElement(StashTableImp* pTable, const void* pKey, int iValue, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	StashValue value;
	value.pValue = S32_TO_PTR(iValue);
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), value, bOverwriteIfFound, ppElement);
}

bool stashRemoveInt(StashTableImp* pTable, const void* pKey, int* piResult)
{
	StashValue value;
	bool ret;
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	pTable->bAllPointerValues = 0;
	ret = stashRemoveValueInternal(pTable, *((StashKey*)&pKey), &value);
	if (piResult)
		*piResult = PTR_TO_S32(value.pValue);
	return ret;
}

bool stashFindInt(const StashTableImp* pTable, const void* pKey, int* piResult)
{
	StashValue value;
	bool ret;
	if (!pTable)
	{
		if (piResult)
			*piResult = 0;
		return false;
	}
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	ret = stashFindValueInternal(pTable, *((StashKey*)&pKey), &value);
	if (piResult)
		*piResult = PTR_TO_S32(value.pValue);
	return ret;
}

// float values
bool stashAddFloat(StashTableImp* pTable, const void* pKey, float fValue, bool bOverwriteIfFound)
{
	StashValue value;
	value.pValue = F32_TO_PTR(fValue);
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), value, bOverwriteIfFound, NULL);
}

bool stashAddFloatAndGetElement(StashTableImp* pTable, const void* pKey, float fValue, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	StashValue value;
	value.pValue = F32_TO_PTR(fValue);
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), value, bOverwriteIfFound, ppElement);
}

bool stashRemoveFloat(StashTableImp* pTable, const void* pKey, float* pfResult)
{
	StashValue value;
	bool ret;
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	pTable->bAllPointerValues = 0;
	ret = stashRemoveValueInternal(pTable, *((StashKey*)&pKey), &value);
	if (pfResult)
		*pfResult = PTR_TO_F32(value.pValue);
	return ret;
}

bool stashFindFloat(const StashTableImp* pTable, const void* pKey, float* pfResult)
{
	StashValue value;
	bool ret;
	if (!pTable)
	{
		if (pfResult)
			*pfResult = 0;
		return false;
	}
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType != StashKeyTypeInts);
	ret = stashFindValueInternal(pTable, *((StashKey*)&pKey), &value);
	if (pfResult)
		*pfResult = PTR_TO_F32(value.pValue);
	return ret;
}

bool stashFindPointerDirect(cStashTable table, const void* pKey, U32 uiHashValue, void** ppValue, U32 * piEmptySlot)
{
	if (!stashFindIndexByHashInternal(table, *((StashKey*)&pKey), uiHashValue, piEmptySlot, NULL))
	{
		if ( ppValue )
			*ppValue = 0;
		return false;
	}

	if (ppValue)
		*ppValue = ((StashTableImp *)table)->pStorage[*piEmptySlot].value.pValue;

	return true;
}

void stashAddPointerDirect(StashTable table, const void* pKey, U32 iEmptySlot, const void* pValue)
{
	((StashTableImp *)table)->pStorage[iEmptySlot].key.pKey = (void *)pKey;
	((StashTableImp *)table)->pStorage[iEmptySlot].value.pValue = (void *)pValue;

	((StashTableImp *)table)->uiValidValues++;
	((StashTableImp *)table)->uiSize++;
}

// -----------
// Int Keys
// -----------
bool stashIntFindElement(StashTableImp* pTable, int iKey, StashElementImp** pElement)
{
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	if (!pTable)
	{
		if (pElement)
			*pElement = NULL;
		return false;
	}
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType == StashKeyTypeInts);
	return stashFindElementInternal(pTable, key, pElement);
}

bool stashIntFindElementConst(const StashTableImp* pTable, int iKey, const StashElementImp** pElement)
{
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	if (!pTable)
	{
		if (pElement)
			*pElement = NULL;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeInts);
	return stashFindElementInternalConst(pTable, key, pElement);
}

bool stashIntFindIndexByKey(const StashTableImp* pTable, int iKey, U32* puiIndex)
{
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	assert(pTable);
	assert(pTable->eKeyType == StashKeyTypeInts);
	return stashFindIndexByKeyInternal(pTable, key, puiIndex, NULL, NULL);
}

bool stashIntGetKey(const StashTableImp* pTable, int iKeyIn, int* piKeyOut)
{
	StashKey key;
	StashElementImp* pElement;
	key.pKey = S32_TO_PTR(iKeyIn);
	if (!pTable)
	{
		if (piKeyOut)
			*piKeyOut = 0;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeInts);
	if (!stashFindElementInternal(pTable, key, &pElement))
		return false;
	if (piKeyOut)
		*piKeyOut = PTR_TO_S32(pElement->key.pKey);
	return true;
}

bool stashIntFindKeyByIndex(const StashTableImp* pTable, U32 uiIndex, int* piKeyOut)
{
	StashKey key;
	bool ret;
	if (!pTable)
	{
		if (piKeyOut)
		{
			*piKeyOut = 0;
			return false;
		}
	}
	assert(pTable && (pTable->eKeyType == StashKeyTypeInts));
	ret = stashFindKeyByIndexInternal(pTable, uiIndex, &key);
	if (piKeyOut)
		*piKeyOut = PTR_TO_S32(key.pKey);
	return ret;
}


// pointer values
bool stashIntAddPointer(StashTableImp* pTable, int iKey, void* pValue, bool bOverwriteIfFound)
{
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType == StashKeyTypeInts);
	return stashAddValueInternal(pTable, key, *((StashValue*)&pValue), bOverwriteIfFound, NULL);
}

bool stashIntAddPointerAndGetElement(StashTableImp* pTable, int iKey, void* pValue, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType == StashKeyTypeInts);
	return stashAddValueInternal(pTable, key, *((StashValue*)&pValue), bOverwriteIfFound, ppElement);
}

bool stashIntRemovePointer(StashTableImp* pTable, int iKey, void** ppValue)
{
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	if (!pTable)
	{
		if (ppValue)
			*ppValue = NULL;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeInts);
	assert(!pTable->bReadOnly);
	return stashRemoveValueInternal(pTable, key, (StashValue*)ppValue);
}

bool stashIntFindPointer(const StashTableImp* pTable, int iKey, void** ppValue)
{
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	if (!pTable)
	{
		if (ppValue)
			*ppValue = NULL;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeInts);
	assert(!pTable->bReadOnly);
	return stashFindValueInternal(pTable, key, (StashValue*)ppValue);
}

bool stashIntFindPointerConst(const StashTableImp* pTable, int iKey, const void** ppValue)
{
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	if (!pTable)
	{
		if (ppValue)
			*ppValue = NULL;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeInts);
	return stashFindValueInternal(pTable, key, (StashValue*)ppValue);
}

// int values
bool stashIntAddInt(StashTableImp* pTable, int iKey, int iValue, bool bOverwriteIfFound)
{
	StashValue value;
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	value.pValue = S32_TO_PTR(iValue);
	assert(pTable);
	assert(pTable->eKeyType == StashKeyTypeInts);
	assert(!pTable->bReadOnly);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, key, value, bOverwriteIfFound, NULL);
}

bool stashIntAddIntAndGetElement(StashTableImp* pTable, int iKey, int iValue, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	StashValue value;
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	value.pValue = S32_TO_PTR(iValue);
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType == StashKeyTypeInts);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, key, value, bOverwriteIfFound, ppElement);
}

bool stashIntRemoveInt(StashTableImp* pTable, int iKey, int* piResult)
{
	StashValue value;
	StashKey key;
	bool ret;
	key.pKey = S32_TO_PTR(iKey);
	assert(pTable);
	assert(pTable->eKeyType == StashKeyTypeInts);
	assert(!pTable->bReadOnly);
	pTable->bAllPointerValues = 0;
	ret = stashRemoveValueInternal(pTable, key, &value);
	if (piResult)
		*piResult = PTR_TO_S32(value.pValue);
	return ret;
}

bool stashIntFindInt(const StashTableImp* pTable, int iKey, int* piResult)
{
	bool ret;
	StashValue value;
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	if (!pTable)
	{
		if (piResult)
			*piResult = 0;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeInts);
	assert(!pTable->bReadOnly);
	ret = stashFindValueInternal(pTable, key, &value);
	if (piResult)
		*piResult = PTR_TO_S32(value.pValue);
	return ret;
}

// float values
bool stashIntAddFloat(StashTableImp* pTable, int iKey, float fValue, bool bOverwriteIfFound)
{
	StashValue value;
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	value.pValue = F32_TO_PTR(fValue);
	assert(pTable);
	assert(pTable->eKeyType == StashKeyTypeInts);
	assert(!pTable->bReadOnly);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, key, value, bOverwriteIfFound, NULL);
}

bool stashIntAddFloatAndGetElement(StashTableImp* pTable, int iKey, float fValue, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	StashValue value;
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	value.pValue = F32_TO_PTR(fValue);
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType == StashKeyTypeInts);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, key, value, bOverwriteIfFound, ppElement);
}

bool stashIntRemoveFloat(StashTableImp* pTable, int iKey, float* pfResult)
{
	StashValue value;
	StashKey key;
	bool ret;
	key.pKey = S32_TO_PTR(iKey);
	assert(pTable);
	assert(pTable->eKeyType == StashKeyTypeInts);
	assert(!pTable->bReadOnly);
	pTable->bAllPointerValues = 0;
	ret = stashRemoveValueInternal(pTable, key, &value);
	if (pfResult)
		*pfResult = PTR_TO_F32(value.pValue);
	return ret;
}

bool stashIntFindFloat(const StashTableImp* pTable, int iKey, float* pfResult)
{
	bool ret;
	StashValue value;
	StashKey key;
	key.pKey = S32_TO_PTR(iKey);
	if (!pTable)
	{
		if (pfResult)
			*pfResult = 0;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeInts);
	assert(!pTable->bReadOnly);
	ret = stashFindValueInternal(pTable, key, &value);
	if (pfResult)
		*pfResult = PTR_TO_F32(value.pValue);
	return ret;
}

// -----------
// Address Keys
// -----------
bool stashAddressFindElement(StashTableImp* pTable, const void* pKey, StashElementImp** pElement)
{
	if (!pTable)
	{
		if (pElement)
			*pElement = NULL;
		return false;
	}
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType == StashKeyTypeAddress);
	return stashFindElementInternal(pTable, *((StashKey*)&pKey), pElement);
}

bool stashAddressFindElementConst(const StashTableImp* pTable, const void* pKey, const StashElementImp** pElement)
{
	if (!pTable)
	{
		if (pElement)
			*pElement = NULL;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeAddress);
	return stashFindElementInternalConst(pTable, *((StashKey*)&pKey), pElement);
}

bool stashAddressFindIndexByKey(const StashTableImp* pTable, const void* pKey, U32* puiIndex)
{
	assert(pTable);
	assert(pTable->eKeyType == StashKeyTypeAddress);
	return stashFindIndexByKeyInternal(pTable, *((StashKey*)&pKey), puiIndex, NULL, NULL);
}

bool stashAddressGetKey(const StashTableImp* pTable, const void* pKeyIn, void** ppKeyOut)
{
	StashElementImp* pElement;
	if (!pTable)
	{
		if (ppKeyOut)
			*ppKeyOut = 0;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeAddress);
	if (!stashFindElementInternal(pTable, *((StashKey*)&pKeyIn), &pElement))
		return false;
	*ppKeyOut = pElement->key.pKey;
	return true;
}

bool stashAddressFindKeyByIndex(const StashTableImp* pTable, U32 uiIndex, void** ppKeyOut)
{
	if (!pTable)
	{
		if (ppKeyOut)
		{
			*ppKeyOut = 0;
			return false;
		}
	}
	assert(pTable && (pTable->eKeyType == StashKeyTypeAddress));
	return stashFindKeyByIndexInternal(pTable, uiIndex, (StashKey*)ppKeyOut);
}


// pointer values
bool stashAddressAddPointer(StashTableImp* pTable, const void* pKey, void* pValue, bool bOverwriteIfFound)
{
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType == StashKeyTypeAddress);
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), *((StashValue*)&pValue), bOverwriteIfFound, NULL);
}

bool stashAddressAddPointerAndGetElement(StashTableImp* pTable, const void* pKey, void* pValue, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType == StashKeyTypeAddress);
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), *((StashValue*)&pValue), bOverwriteIfFound, ppElement);
}

bool stashAddressRemovePointer(StashTableImp* pTable, const void* pKey, void** ppValue)
{
	if (!pTable)
	{
		if (ppValue)
			*ppValue = NULL;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeAddress);
	assert(!pTable->bReadOnly);
	return stashRemoveValueInternal(pTable, *((StashKey*)&pKey), (StashValue*)ppValue);
}

bool stashAddressFindPointer(const StashTableImp* pTable, const void* pKey, void** ppValue)
{
	if (!pTable)
	{
		if (ppValue)
			*ppValue = NULL;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeAddress);
	assert(!pTable->bReadOnly);
	return stashFindValueInternal(pTable, *((StashKey*)&pKey), (StashValue*)ppValue);
}

bool stashAddressFindPointerConst(const StashTableImp* pTable, const void* pKey, const void** ppValue)
{
	if (!pTable)
	{
		if (ppValue)
			*ppValue = NULL;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeAddress);
	return stashFindValueInternal(pTable, *((StashKey*)&pKey), (StashValue*)ppValue);
}

// int values
bool stashAddressAddInt(StashTableImp* pTable, const void* pKey, int iValue, bool bOverwriteIfFound)
{
	StashValue value;
	value.pValue = S32_TO_PTR(iValue);
	assert(pTable);
	assert(pTable->eKeyType == StashKeyTypeAddress);
	assert(!pTable->bReadOnly);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), value, bOverwriteIfFound, NULL);
}

bool stashAddressAddIntAndGetElement(StashTableImp* pTable, const void* pKey, int iValue, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	StashValue value;
	value.pValue = S32_TO_PTR(iValue);
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType == StashKeyTypeAddress);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), value, bOverwriteIfFound, ppElement);
}

bool stashAddressRemoveInt(StashTableImp* pTable, const void* pKey, int* piResult)
{
	StashValue value;
	bool ret;
	assert(pTable);
	assert(pTable->eKeyType == StashKeyTypeAddress);
	assert(!pTable->bReadOnly);
	pTable->bAllPointerValues = 0;
	ret = stashRemoveValueInternal(pTable, *((StashKey*)&pKey), &value);
	if (piResult)
		*piResult = PTR_TO_S32(value.pValue);
	return ret;
}

bool stashAddressFindInt(const StashTableImp* pTable, const void* pKey, int* piResult)
{
	StashValue value;
	bool ret;
	if (!pTable)
	{
		if (piResult)
			*piResult = 0;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeAddress);
	assert(!pTable->bReadOnly);
	ret = stashFindValueInternal(pTable, *((StashKey*)&pKey), &value);
	if (piResult)
		*piResult = PTR_TO_S32(value.pValue);
	return ret;
}

// float values
bool stashAddressAddFloat(StashTableImp* pTable, const void* pKey, float fValue, bool bOverwriteIfFound)
{
	StashValue value;
	value.pValue = F32_TO_PTR(fValue);
	assert(pTable);
	assert(pTable->eKeyType == StashKeyTypeAddress);
	assert(!pTable->bReadOnly);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), value, bOverwriteIfFound, NULL);
}

bool stashAddressAddFloatAndGetElement(StashTableImp* pTable, const void* pKey, float fValue, bool bOverwriteIfFound, StashElementImp** ppElement)
{
	StashValue value;
	value.pValue = F32_TO_PTR(fValue);
	assert(pTable);
	assert(!pTable->bReadOnly);
	assert(pTable->eKeyType == StashKeyTypeAddress);
	pTable->bAllPointerValues = 0;
	return stashAddValueInternal(pTable, *((StashKey*)&pKey), value, bOverwriteIfFound, ppElement);
}

bool stashAddressRemoveFloat(StashTableImp* pTable, const void* pKey, float* pfResult)
{
	StashValue value;
	bool ret;
	assert(pTable);
	assert(pTable->eKeyType == StashKeyTypeAddress);
	assert(!pTable->bReadOnly);
	pTable->bAllPointerValues = 0;
	ret = stashRemoveValueInternal(pTable, *((StashKey*)&pKey), &value);
	if (pfResult)
		*pfResult = PTR_TO_F32(value.pValue);
	return ret;
}

bool stashAddressFindFloat(const StashTableImp* pTable, const void* pKey, float* pfResult)
{
	StashValue value;
	bool ret;
	if (!pTable)
	{
		if (pfResult)
			*pfResult = 0;
		return false;
	}
	assert(pTable->eKeyType == StashKeyTypeAddress);
	assert(!pTable->bReadOnly);
	ret = stashFindValueInternal(pTable, *((StashKey*)&pKey), &value);
	if (pfResult)
		*pfResult = PTR_TO_F32(value.pValue);
	return ret;
}

// -----------
// Stash Tracker
// -----------

#ifdef STASH_TABLE_TRACK
typedef struct HTTrackerInfo
{
	char cName[128];
	int iNumStashTables;
	unsigned int uiTotalSize;
	unsigned int uiStringTableSize;
} HTTrackerInfo;

FILE* stashDumpFile;

static int stashTrackerProcessor(StashElement a)
{
	HTTrackerInfo* pInfo = stashElementGetPointer(a);
	printf("%.1f KB\t%.1f KB\t%d tables\t%s\n", ((float)pInfo->uiTotalSize) / 1024.0f, ((float)pInfo->uiStringTableSize) / 1024.0f, pInfo->iNumStashTables, pInfo->cName );
	fprintf(stashDumpFile, "%d,%d,%d,%s\n", pInfo->uiTotalSize, pInfo->uiStringTableSize, pInfo->iNumStashTables, pInfo->cName );
	return 1;
}

void printStashTableMemDump()
{
	StashTableImp* pList = pStashTableList;
	StashTable pStashTableStashTable = stashTableCreateWithStringKeys(64, StashDeepCopyKeys_NeverRelease);
	int iTotalOver64k = 0;
	int iTotalUnder64k = 0;

	stashDumpFile = NULL;

	while (!stashDumpFile)
	{
		stashDumpFile = fopen( "c:/hashdump.csv", "wt" );
		if (!stashDumpFile)
		{
			printf("Let go of c:/hashdump.csv, please!\n");
			Sleep(1000);
		}
	}	
	fprintf(stashDumpFile, "Size,StringTableSize,NumInstances,Name\n");

	printf("Hash Table Mem Usage\n");
	printf("----------------------\n");

	while (pList)
	{
		HTTrackerInfo* pInfo;
		int iMemUsage = (int)stashGetMemoryUsage((cStashTable)pList);
		int iStringTableUsage = 0;

		if ( pList->pStringTable )
			iStringTableUsage = (int)strTableMemUsage(pList->pStringTable);


		if ( !stashFindPointer(pStashTableStashTable, pList->cName, &pInfo))
		{
			pInfo = calloc(1, sizeof(HTTrackerInfo));
			strcpy(pInfo->cName, pList->cName);
			stashAddPointer(pStashTableStashTable, pList->cName, pInfo, false);
		}

		assert(pInfo);
		pInfo->iNumStashTables++;
		pInfo->uiTotalSize += iMemUsage;
		pInfo->uiStringTableSize += iStringTableUsage;

		//printf("%5d - %s\n", iMemUsage, pList->cName);
		
		if ( iMemUsage > 1024 * 64)
		{
			iTotalOver64k += iMemUsage;
		}
		else
		{
			iTotalUnder64k += iMemUsage;
		}
		pList = pList->pNext;
	}
	stashForEachElement(pStashTableStashTable, stashTrackerProcessor);

	printf("----------------------\n");
	printf("Under64k = %d		Over64k = %d		Total = %d\n", iTotalUnder64k, iTotalOver64k, iTotalOver64k + iTotalUnder64k);
	printf("----------------------\n");

	stashTableDestroy(pStashTableStashTable);
	fclose(stashDumpFile);
}
#else
void printStashTableMemDump()
{
	printf("ERROR: Please #define STASH_TABLE_TRACK in stashtable.c for stash table tracking information!\n");
}
#endif

