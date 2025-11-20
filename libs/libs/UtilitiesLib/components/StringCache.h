#ifndef STRINGCACHE_H
#define STRINGCACHE_H
#pragma once
GCC_SYSTEM

#include "SharedMemory.h"
#include "stdtypes.h" // to make VS2005 not complain about stricmp

typedef const char *constCharPtr;

// Generic string table
SA_ORET_OP_STR const char* allocAddString_dbg(SA_PARAM_OP_STR const char *s, bool bFixCase, bool bFixSlashes, bool bStaticString, SA_PARAM_NN_STR const char *caller_fname, int line);
#define allocAddString(s) allocAddString_dbg(s, true, false, false, __FILE__, __LINE__)
#define allocAddFilename(s) allocAddString_dbg(s, false, true, false, __FILE__, __LINE__)

// static strings can be added ONLY during AUTO_RUNs
#define allocAddStaticString(s) allocAddString_dbg(s, false, false, true, __FILE__, __LINE__)

const char* allocFindString(SA_PARAM_OP_STR const char *s);
void stringCacheSetInitialSize(U32 numStrings);
void stringCacheDoNotWarnOnResize(void); // Must be called after stringCacheSetInitialSize

int allocAddStringCheck(void); // Checks for corruption

// For manually locking before adding a large number of entries
S32 allocAddStringManualLock(void);
const char *allocAddStringWhileLocked_dbg(SA_PARAM_OP_STR const char *s, bool bFixCase, bool bStaticString, SA_PARAM_NN_STR const char *caller_fname, int line);
#define allocAddFilenameWhileLocked(s) allocAddStringWhileLocked_dbg(s, false, false, __FILE__, __LINE__)
void allocAddStringManualUnlock(void);

// For dealing with shared string pool
void stringCacheInitializeShared(const char *sharedMemoryName, U32 totalSize, U32 numStrings, bool bWillBeIdentical);

// When all loading of shared memory is complete
void stringCacheFinalizeShared(void);

// Are cached strings in shared memory?
bool stringCacheSharingEnabled(void);

// Is the string cache currently read only? This happens during loading
bool stringCacheReadOnly(void);

// Acquire a shared memory handle, but through the string cache memory cache
SM_AcquireResult stringCacheSharedMemoryAcquire(SharedMemoryHandle **phandle, const char *name, const char *pDictName);

// Register an "extra" chunk, which is a subsidiary dictionary
void stringCacheSharedMemoryRegisterExtra(const char *pDictName);

// Returns chunk name for a given dictionary
const char *stringCacheSharedMemoryChunkForDict(const char *pDictName);

// First tries the string cache for a simple cmp, if that fails, does a stricmp...
__forceinline static bool stringCacheCompareString(const char* pcKnownStringPtr, const char* pcLookupString)
{
	const char* pcLookedUp = allocFindString(pcLookupString);
	if (!pcLookedUp)
	{
		return (stricmp(pcKnownStringPtr, pcLookupString) == 0);
	}
	return pcKnownStringPtr == pcLookedUp;
}

// Use sparingly - only shaders and perhaps filenames should ever use something like this
const char* allocAddCaseSensitiveString( const char * s );

void allocAddStringMapRecentMemory(const char *src, const char *dst_fname, int dst_line);
void allocAddStringFlushAccountingCache(void);

#if _PS3
    #define g_ccase_string_cache 0
    #define g_assert_verify_ccase_string_cache 0
#else
    extern bool g_ccase_string_cache;
    extern bool g_assert_verify_ccase_string_cache;
#endif
extern bool g_disallow_static_strings;

// to avoid using critical sections
void stringCacheEnableLocklessRead(void);
void stringCacheDisableLocklessRead(void);
bool stringCacheIsLocklessReadActive(void);

void stringCacheDisableWarnings(void);

void stringCacheSetGrowFast(void); // Grow fast instead of space efficiently

bool stringCacheIsNearlyFull();

#endif // STRINGCACHE_H