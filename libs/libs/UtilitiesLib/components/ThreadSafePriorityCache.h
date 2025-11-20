#pragma once
GCC_SYSTEM

#include "StashTable.h"

typedef struct ThreadSafePriorityCache ThreadSafePriorityCache;

typedef struct ThreadSafePriorityCacheIterator
{
	StashTableIterator iter;
} ThreadSafePriorityCacheIterator;

// Should return > 0 if A is higher priority than B and should be kept over B in the cache
typedef int (*ThreadSafePriorityCompareFunc)(const void* a, const void* b);
typedef void (*Destructor)(void* value);


/*

The ThreadSafePriorityCache is most useful when you want a thread-safe Most-Recently-Used cache of objects, 
and the vast majority of time those objects are going to be found in the cache and not need to be added.

The ThreadSafePriorityCache is basically just a non-thread-safe hashtable combined with a thread safe list of objects to add to the cache. 
To add to the cache, an object is added to the thread safe list, and then at a point where no one will be looking at the hash table, everything in the list is added.
During lookups, the hash table is checked first (lookups are thread safe), and if nothing is found, the list is linearly searched. If still not found, it will return NULL
and you are expected to add the object yourself. 

At some point you need to run tspCacheUpdate() which is not thread safe, and moves the list cache into the stashtable for faster lookups next time.
Also during this function the least-recently-used objects in the cache over the iMaxObjects limit are purged, using the ThreadSafePriorityCompareFunc to sort.

*/



ThreadSafePriorityCache* tspCacheCreateEx(int iMaxObjects, int iMaxQueueSize, StashTableMode eMode, StashKeyType eKeyType, U32 uiKeyLength, ThreadSafePriorityCompareFunc compFunc, Destructor keyDestructor, Destructor valueDestructor, ParseTable* pDebugParseTable MEM_DBG_PARMS);
#define tspCacheCreate(iMaxObjects, iMaxQueueSize, eMode, eKeyType, uiKeyLength, compFunc, keyDestructor, valueDestructor, pDebugParseTable) tspCacheCreateEx(iMaxObjects, iMaxQueueSize, eMode, eKeyType, uiKeyLength, compFunc, keyDestructor, valueDestructor, pDebugParseTable MEM_DBG_PARMS_INIT)

void tspCacheClear(ThreadSafePriorityCache* pCache);
void tspCacheDestroy(ThreadSafePriorityCache* pCache);


// Thread safe functions:
const void* tspCacheFind(ThreadSafePriorityCache* pCache, const void* pKey);
const void* tspCacheFindQueue(ThreadSafePriorityCache* pCache, const void* pKey);
bool tspCacheAdd(ThreadSafePriorityCache* pCache, const void* pKey, const void* pValue);

// need to be strictly in pairs
void tspCacheLock(ThreadSafePriorityCache* pCache);
void tspCacheUnlock(ThreadSafePriorityCache* pCache);

// Not thread safe functions:


// NOT THREAD SAFE
// You should call this once per frame while worker threads are not working.
void tspCacheUpdate(ThreadSafePriorityCache* pCache);


void tspCacheGetIterator(ThreadSafePriorityCache* pCache, ThreadSafePriorityCacheIterator* pIter);
void* tspCacheGetNextElement(ThreadSafePriorityCacheIterator* pIter);
