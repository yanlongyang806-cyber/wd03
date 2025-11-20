#pragma once
GCC_SYSTEM



typedef enum ACacheKeyType
{
	ACacheKeyType_Strings,
	ACacheKeyType_FixedLength,
} ACacheKeyType;


typedef struct ACacheElem
{
	void* pKey;
	void* pValue;
} ACacheElem;

typedef void (*ACacheFreeElemFunc)(ACacheElem* pElem);

typedef struct AssociativeCache
{
	U32 uiNumSets;
	U32 uiSetSize;
	U32	uiFixedKeyLength;

	ACacheKeyType keyType;

	ACacheFreeElemFunc freeElemFunc;

	ACacheElem* pStorage;
} AssociativeCache;

// Stores uiNumSets * uiSetSize total slots. Requires a freeElemFunc so when an element gets evicted, the value and/or key can be freed.
// Only requires the uiFixedKeyLength if the keytype is ACacheKeyType_FixedSize
AssociativeCache* associativeCacheCreateEx( U32 uiNumSets, U32 uiSetSize, ACacheKeyType keyType, U32 uiFixedKeyLength, SA_PARAM_NN_VALID ACacheFreeElemFunc freeElemFunc MEM_DBG_PARMS );

#define associativeCacheCreateWithStringPooledKeys(uiNumSets, uiSetSize, freeElemFunc) associativeCacheCreateEx(uiNumSets, uiSetSize, ACacheKeyType_Strings, 0, freeElemFunc MEM_DBG_PARMS_INIT)
#define associativeCacheCreateWithFixedLengthKeys(uiNumSets, uiSetSize, uiFixedKeyLength, freeElemFunc) associativeCacheCreateEx(uiNumSets, uiSetSize, ACacheKeyType_FixedLength, uiFixedKeyLength, freeElemFunc MEM_DBG_PARMS_INIT)

void associativeCacheDestroy(AssociativeCache* pCache);

void associativeCacheClear(AssociativeCache* pCache);

// This returns a slot either way, and true if the key matched, false if it didn't
bool associativeCacheFindElem(AssociativeCache* pCache, void* pKey, ACacheElem** ppElem);