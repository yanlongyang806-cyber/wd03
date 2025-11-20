#ifndef STASHTABLE_H
#define STASHTABLE_H
#pragma once
GCC_SYSTEM

#include "stdtypes.h"

C_DECLARATIONS_BEGIN

//----------------------------------
// A StashTable is just a HashTable
//----------------------------------
typedef struct StashTableImp*			StashTable;
typedef struct StashElementImp*		StashElement;
typedef const struct StashTableImp*	cStashTable;
typedef const struct StashElementImp*	cStashElement;
typedef void (*Destructor)(void* value);
typedef unsigned int (*ExternalHashFunction)(const void* key, int hashSeed);
typedef int (*ExternalCompareFunction)(const void* key1, const void* key2);

typedef struct
{
	StashTable		pTable;
	U32				uiIndex;
} StashTableIterator;

#define STASH_TABLE_EMPTY_SLOT_KEY ((void*)(intptr_t)0)
#ifdef  _WIN64
#define STASH_TABLE_DELETED_SLOT_VALUE ((void*)(uintptr_t)0xfffffffffffffffeULL)
#else
#define STASH_TABLE_DELETED_SLOT_VALUE ((void*)(_W64 uintptr_t)0xfffffffeU)	// _W64 = we promise to expand to 64 bits if _WIN64 is defined
#endif

#define STASH_DEEP_COPY_KEYS_NEVER_RELEASE_BIT	(1 << 0) // only makes sense on string keys, int keys are always deep copied
#define STASH_TABLE_READ_ONLY			(1 << 1) // lock the table from writing
#define CASE_SENSITIVE_BIT				(1 << 2) // only makes sense on string keys, case sensitive for hashing
#define STASH_IN_SHARED_HEAP			(1 << 3) // this stash table is alloc'd in the shared heap
#define CASE_INSENSITIVE_BIT			(1 << 4) // forces StashKeyTypeFixedSize keys to be treated as case-insensitive
#define STASH_DEEP_COPY_KEYS_RELEASE_ON_REMOVE_BIT (1 << 5) //copies the key internally, frees it when the object is removed

typedef enum
{
	StashDefault			=			0, // doesn't do anything, a place holder
	
	//VERY IMPORTANT NOTE: every key that you ever add with StashDeepCopyKeys_NeverRelease gets allocated and never 
	//released, EVEN IF THAT KEY HAS ALREADY BEEN USED IN THIS TABLE. The one exception to that is that as long
	//as the element is still in the table, you can add a new element with the same key, overriding the previous element,
	//and no leaking will occur. In general, use this very carefully, mainly for tables that get populated once and then
	//left alone

	// Note: DeepCopyKeys only works with StashKeyTypeStrings.  It currently does not work with any of the other modes.

	StashDeepCopyKeys_NeverRelease		=			STASH_DEEP_COPY_KEYS_NEVER_RELEASE_BIT,
	
	StashCaseSensitive		=			CASE_SENSITIVE_BIT,
	StashSharedHeap			=			STASH_IN_SHARED_HEAP,
	StashCaseInsensitive	=			CASE_INSENSITIVE_BIT,
	StashDeepCopyKeys_ReleaseOnRemove   = STASH_DEEP_COPY_KEYS_RELEASE_ON_REMOVE_BIT,
} StashTableMode;

typedef enum 
{
	StashKeyTypeStrings,
	StashKeyTypeInts,
	StashKeyTypeFixedSize,
	StashKeyTypeExternalFunctions,
	StashKeyTypeAddress,
	StashKeyTypeMax
} StashKeyType;

// -------------------------
// Table management and info
// -------------------------


StashTable			stashTableCreateEx(U32 uiInitialSize, StashTableMode eMode, StashKeyType eKeyType, U32 uiKeyLength MEM_DBG_PARMS);
StashTable			stashTableCreateWithStringKeysEx(U32 uInitialSize, StashTableMode eMode MEM_DBG_PARMS);
StashTable			stashTableCreateIntEx(U32 uiInitialSize MEM_DBG_PARMS);
StashTable			stashTableCreateExternalFunctionsEx(U32 uiInitialSize, StashTableMode eMode, ExternalHashFunction hashFunc, ExternalCompareFunction compFunc MEM_DBG_PARMS);
StashTable			stashTableCreateAddressEx(U32 uiInitialSize MEM_DBG_PARMS);
StashTable			stashTableCreateFixedSizeEx(U32 uiInitialSize, U32 sizeInBytes MEM_DBG_PARMS);

#define stashTableCreate(uiInitialSize, eMode, eKeyType, uiKeyLength) stashTableCreateEx(uiInitialSize, eMode, eKeyType, uiKeyLength MEM_DBG_PARMS_INIT)
#define stashTableCreateWithStringKeys(uiInitialSize, eMode) stashTableCreateWithStringKeysEx(uiInitialSize, eMode MEM_DBG_PARMS_INIT)
#define stashTableCreateInt(uiInitialSize) stashTableCreateIntEx(uiInitialSize MEM_DBG_PARMS_INIT)
#define stashTableCreateExternalFunctions(uiInitialSize, eMode, hashFunc, compFunc) stashTableCreateExternalFunctionsEx(uiInitialSize, eMode, hashFunc, compFunc MEM_DBG_PARMS_INIT)
#define stashTableCreateAddress(uiInitialSize) stashTableCreateAddressEx(uiInitialSize MEM_DBG_PARMS_INIT)
#define stashTableCreateFixedSize(uiInitialSize, sizeInBytes) stashTableCreateFixedSizeEx(uiInitialSize, sizeInBytes MEM_DBG_PARMS_INIT)

void				stashTableDestroy(StashTable pTable);
__forceinline static void stashTableDestroySafe(StashTable* ppTable){if(*ppTable){stashTableDestroy(*ppTable);*ppTable=NULL;}}
void				stashTableDestroyEx(StashTable pTable, Destructor keyDstr, Destructor valDstr );
void				stashTableDestroyStruct(StashTable pTable, Destructor keyDstr, ParseTable valTable[]);
__forceinline static void stashTableDestroyStructSafe(StashTable* ppTable, Destructor keyDstr, ParseTable valTable[]){if(*ppTable){stashTableDestroyStruct(*ppTable, keyDstr, valTable);*ppTable=NULL;}}
void				stashTableMerge(StashTable pDestinationTable, cStashTable pSourceTable );
StashTable			stashTableIntersectEx(cStashTable tableOne, cStashTable tableTwo MEM_DBG_PARMS);
#define				stashTableIntersect(tableOne, tableTwo) stashTableIntersectEx(tableOne, tableTwo, MEM_DBG_PARMS_INIT)
void				stashTableClear(StashTable pTable);
void				stashTableClearEx(StashTable pTable, Destructor keyDstr, Destructor valDstr );
void				stashTableClearStruct(StashTable pTable, Destructor keyDstr, ParseTable valTable[] );

int					stashTableVerifyStringKeys(StashTable pTable); // Checks that no keys have been corrupted (returns 0 on failure)

void				stashTableLock(StashTable table);
void				stashTableUnlock(StashTable table);
// table info 
U32					stashGetCount(cStashTable table);
U32					stashGetOccupiedSlots(cStashTable table);
U32					stashGetMaxSize(cStashTable table);
size_t				stashGetMemoryUsage(cStashTable table);
StashTableMode		stashTableGetMode(cStashTable table);
StashKeyType		stashTableGetKeyType(cStashTable table);
bool				stashTableAreAllValuesPointers(cStashTable table);
bool				stashTableValidateValid(cStashTable table);	// Checks that number of elements present is correct
void				stashTableSetThreadSafeLookups(StashTable table, bool bSet);
void				stashTableSetCantResize(StashTable table, bool bSet);

//Alex is adding this function prototype which seems easy to write... use it to resize a stash table
//which you know is going to get big later. Should do nothing if iSize equals or is less than its
//current size
void				stashTableSetMinSize(StashTable table, U32 uiMinSize);

//another prototype added by Alex that needs to be made to work
void				stashTableFindCurrentBestSize(StashTable table);


// ------------------------
// Shared memory management
// ------------------------
typedef void* (*CustomMemoryAllocator)(void* data, size_t size);
size_t stashGetCopyTargetAllocSize(cStashTable pTable);
StashTable stashCopyToAllocatedSpace(cStashTable pTable, void* pAllocatedSpace, size_t uiTotalSize );
StashTable stashTableClone(cStashTable pTable, CustomMemoryAllocator memAllocator, void *customData);
size_t stashGetTableImpSize(void);


// Odd utility functions
bool stashKeysAreEqual(cStashTable pTable, const void* pKeyA, const void* pKeyB); // Just tells you if two keys are equal according to this stash table
bool stashIntValueIsValid(int iValue);


// -------------------------
// Element management and info
// -------------------------
// pointer values
SA_RET_OP_VALID void* stashElementGetPointer(StashElement element);
void				stashElementSetPointer(StashElement element, void* pValue);
// int values
int					stashElementGetInt(cStashElement element);
void				stashElementSetInt(StashElement element, int iValue);
// float values
float               stashElementGetFloat(cStashElement element);
void                stashElementSetFloat(StashElement element, float fValue);
// keys
char*				stashElementGetStringKey(cStashElement element);
int					stashElementGetIntKey(cStashElement element);
U32					stashElementGetU32Key(cStashElement element);
void*				stashElementGetKey(cStashElement element);


// Iterators
void				stashGetIterator(StashTable pTable, StashTableIterator* pIter);
bool				stashGetNextElement(StashTableIterator* pIter, StashElement* ppElem);
typedef int			(*StashElementProcessor)(StashElement element);
typedef int			(*StashElementProcessorEx)(void* userData, StashElement element);
void				stashForEachElement(StashTable pTable, StashElementProcessor proc);
void				stashForEachElementEx(StashTable pTable, StashElementProcessorEx proc, void* userdata);

bool				stashRemoveElementByIndex(StashTable pTable,U32 uiIndex);

// -----------
// Pointer Keys
// -----------
bool				stashFindElement(StashTable table, const void* pKey, StashElement* pElement);
bool				stashFindElementConst(cStashTable table, const void* pKey, const struct StashElementImp** pElement);
bool				stashFindKeyByIndex(cStashTable table, U32 uiIndex, const void** pKey);
bool				stashFindIndexByKey(cStashTable table, const void* pKey, U32* piIndex);
bool				stashGetKey(cStashTable table, const void* pKeyIn, const void** pKeyOut);
U32					stashGetKeyLength(cStashTable pTable, const void* pKey);
// pointer values
bool				stashAddPointer(StashTable table, const void* pKey, const void* pValue, bool bOverwriteIfFound);
bool				stashAddPointerAndGetElement(StashTable table, const void* pKey, void* pValue, bool bOverwriteIfFound, StashElement* pElement);
bool				stashRemovePointer(StashTable table, const void* pKey, void** ppValue);
bool				stashFindPointer(cStashTable table, const void* pKey, void** ppValue);
bool				stashFindPointerConst(cStashTable table, const void* pKey, const void** ppValue);
void*				stashFindPointerReturnPointer(cStashTable table, const void* pKey); // please don't use, for backwards-compatibility only	
// int values
bool				stashAddInt(StashTable table, const void* pKey, int iValue, bool bOverwriteIfFound);
bool				stashAddIntAndGetElement(StashTable table, const void* pKey, int iValue, bool bOverwriteIfFound, StashElement* pElement);
bool				stashRemoveInt(StashTable table, const void* pKey, int* piResult);
bool				stashFindInt(cStashTable table, const void* pKey, int* piResult);
// bool				stashFindIntConst(cStashTable table, const void* pKey, const int* piResult);
// float values
bool				stashAddFloat(StashTable table, const void* pKey, float fValue, bool bOverwriteIfFound);
bool				stashAddFloatAndGetElement(StashTable table, const void* pKey, float fValue, bool bOverwriteIfFound, StashElement* pElement);
bool				stashRemoveFloat(StashTable table, const void* pKey, float* pfResult);
bool				stashFindFloat(cStashTable table, const void* pKey, float* pfResult);
//bool				stashFindFloatConst(cStashTable table, const void* pKey, const float* pfResult);

// These functions are designed for a very specific access pattern.  They are unsafe.  Don't call them if you don't know what you're doing. [RMARR - 5/16/12]
bool				stashFindPointerDirect(cStashTable table, const void* pKey, U32 uiHashValue, void** ppValue, U32 * piEmptySlot);
// iEmptySlot was returned by stashFindPointerDirect
void				stashAddPointerDirect(StashTable table, const void* pKey, U32 iEmptySlot, const void* pValue);

// -----------
// Int Keys
// -----------
bool				stashIntFindElement(StashTable table, int iKey, StashElement* pElement);
bool				stashIntElementConst(StashTable table, int iKey, const StashElement* pElement);
bool				stashIntFindKeyByIndex(cStashTable table, U32 uiIndex, int* piKeyOut);
bool				stashIntFindIndexByKey(cStashTable table, int iKey, U32* piIndex);
bool				stashIntGetKey(cStashTable table, int iKeyIn, int* piKeyOut);
// pointer values
bool				stashIntAddPointer(StashTable table, int iKey, void* pValue, bool bOverwriteIfFound);
bool				stashIntAddPointerAndGetElement(StashTable table, int iKey, void* pValue, bool bOverwriteIfFound, StashElement* pElement);
bool				stashIntRemovePointer(StashTable table, int iKey, void** ppValue);
bool				stashIntFindPointer(cStashTable table, int iKey, SA_PRE_VALID SA_POST_OP_VALID void** ppValue);
bool				stashIntFindPointerConst(cStashTable table, int iKey, const void** ppValue);
// int values
bool				stashIntAddInt(StashTable table, int iKey, int iValue, bool bOverwriteIfFound);
bool				stashIntAddIntAndGetElement(StashTable table, int iKey, int iValue, bool bOverwriteIfFound, StashElement* pElement);
bool				stashIntRemoveInt(StashTable table, int iKey, int* piResult);
bool				stashIntFindInt(cStashTable table, int iKey, int* piResult);
//bool				stashIntFindIntConst(cStashTable table, int iKey, const int* piResult);
// float values
bool				stashIntAddFloat(StashTable table, int iKey, float fValue, bool bOverwriteIfFound);
bool				stashIntAddFloatAndGetElement(StashTable table, int iKey, float fValue, bool bOverwriteIfFound, StashElement* pElement);
bool				stashIntRemoveFloat(StashTable table, int iKey, float* pfResult);
bool				stashIntFindFloat(cStashTable table, int iKey, float* pfResult);
//bool				stashIntFindFloatConst(cStashTable table, int iKey, const float* pfResult);


// -----------
// Address Keys - for pointer keys that aren't dereferenced
// -----------
bool				stashAddressFindElement(StashTable table, const void* pKey, StashElement* pElement);
bool				stashAddressElementConst(StashTable table, const void* pKey, const StashElement* pElement);
bool				stashAddressFindKeyByIndex(cStashTable table, U32 uiIndex, void** ppKeyOut);
bool				stashAddressFindIndexByKey(cStashTable table, const void* pKey, U32* piIndex);
bool				stashAddressGetKey(cStashTable table, const void* pKeyIn, void** ppKeyOut);
// pointer values
bool				stashAddressAddPointer(StashTable table, const void* pKey, void* pValue, bool bOverwriteIfFound);
bool				stashAddressAddPointerAndGetElement(StashTable table, const void* pKey, void* pValue, bool bOverwriteIfFound, StashElement* pElement);
bool				stashAddressRemovePointer(StashTable table, const void* pKey, void** ppValue);
bool				stashAddressFindPointer(cStashTable table, const void* pKey, void** ppValue);
bool				stashAddressFindPointerConst(cStashTable table, const void* pKey, const void** ppValue);
// int values
bool				stashAddressAddInt(StashTable table, const void* pKey, int iValue, bool bOverwriteIfFound);
bool				stashAddressAddIntAndGetElement(StashTable table, const void* pKey, int iValue, bool bOverwriteIfFound, StashElement* pElement);
bool				stashAddressRemoveInt(StashTable table, const void* pKey, int* piResult);
bool				stashAddressFindInt(cStashTable table, const void* pKey, int* piResult);
//bool				stashAddressFindIntConst(cStashTable table, const void* pKey, const int* piResult);
// float values
bool				stashAddressAddFloat(StashTable table, const void* pKey, float fValue, bool bOverwriteIfFound);
bool				stashAddressAddFloatAndGetElement(StashTable table, const void* pKey, float fValue, bool bOverwriteIfFound, StashElement* pElement);
bool				stashAddressRemoveFloat(StashTable table, const void* pKey, float* pfResult);
bool				stashAddressFindFloat(cStashTable table, const void* pKey, float* pfResult);
//bool				stashAddressFindFloatConst(cStashTable table, const void* pKey, const float* pfResult);

// ----------
// Tracking
// ----------
void				printStashTableMemDump(void);

#define FOR_EACH_IN_STASHTABLE(st, typ, p) { StashTableIterator i##p##Iter; StashElement e##p##Elem; stashGetIterator(st, &i##p##Iter); while (stashGetNextElement(&i##p##Iter, &e##p##Elem)) { typ *p = stashElementGetPointer(e##p##Elem);
#define FOR_EACH_IN_STASHTABLE2(st, elem) { StashTableIterator i##elem##Iter; StashElement elem; stashGetIterator(st, &i##elem##Iter); while (stashGetNextElement(&i##elem##Iter, &elem)) {  
#define FOR_EACH_END } } 

C_DECLARATIONS_END

#endif
