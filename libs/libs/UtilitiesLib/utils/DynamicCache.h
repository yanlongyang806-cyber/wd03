#pragma once
GCC_SYSTEM

typedef struct DynamicCache DynamicCache;
typedef struct DynamicCacheElement DynamicCacheElement;
typedef struct FileEntry FileEntry;
typedef FileEntry** FileList;

// All dynamicCache functions may only be called from the thread they were created in

void dynamicCacheDebugSetFailureRate(F32 rate); // 1.0 effectively disables the cache
bool dynamicCacheDebugRandomFailure(void); // For callbacks that want to debug failure in later validation


#define DYNAMIC_CACHE_READONLY -1, -1, -1, DYNAMIC_CACHE_DEFAULT // Pass as last 4 parameters to dynamicCacheCreate

typedef enum DynamicCacheFlags
{
	DYNAMIC_CACHE_DEFAULT = 0,
	DYNAMIC_CACHE_NO_TIMESTAMPS = 1<<0,
	DYNAMIC_CACHE_RAM_CACHED = 1<<1,
} DynamicCacheFlags;

// Cache is flushed on version change
// Cache will hold any files (regardless of age) up to desiredSize
// Cache will flush oldest files older than minSecondsBeforeFlush if size is > desiredSize
// Cache will flush oldest files (even younger than minSecondsBeforeFlush) if size is > maxSize
DynamicCache *dynamicCacheCreate(const char *cacheFileName,
								 int version, size_t desiredSize, size_t maxSize, int minSecondsBeforeFlush, DynamicCacheFlags flags);
void dynamicCacheDestroy(DynamicCache *cache);
void dynamicCacheSafeDestroy(DynamicCache **cache);

// Merge one cache with another (used for shader binning).  Returns number of merged files.
// Merges only the files which are up to date and leaves up to date files in the
//  destination alone
int dynamicCacheMergePrecise(DynamicCache *cache, const char *srcCacheFileName, bool pruneOrphans);
// "Merges" by simply doing a fast copy of one cache over the other, if the
//  destination cache is in need of merging and a quick random sample shows the
//  source to be up to date.
// *increments* merged, must be inited if non-NULL
// Also opens/creates the cache and returns it, see dynamicCacheCreate for parameter info
DynamicCache *dynamicCacheMergeQuick(const char *destCacheFileName, const char *srcCacheFileName, SA_PARAM_OP_VALID int *numMerged,
									 int version, size_t desiredSize, size_t maxSize, int minSecondsBeforeFlush, DynamicCacheFlags flags);

// Makes a new cache file, in-place, with no timestamps, and in sorted order
void dynamicCacheConsistify(const char *cacheFileName);

// Expands any redirects so that pruning will not leave orphaned files
void dynamicCacheExpandRedirects(DynamicCache *cache);

// Removes all old, untouched files, used for shader binning when it has just touched all relevant files
int dynamicCachePruneOldFiles(DynamicCache *cache, U32 timestamp);

int dynamicCacheGetVersion(const DynamicCache *cache);

// Data ownership is not taken (it makes a new copy with some header data), free data yourself
// FileList ownership is *not* taken, you must free it or reuse it after calling this
void dynamicCacheUpdateFile(DynamicCache *cache, const char *filename, const void *data, int dataSize, FileList *dependencyList);

void dynamicCacheTouchFile(DynamicCache *cache, const char *filename);

bool dynamicCacheIsFileUpToDateSync_WillStall(DynamicCache *cache, const char *filename);

// May not be up to date (need to call dynamicCacheGetAsync to try and find out)
bool dynamicCacheFileExists(DynamicCache *cache, const char *filename);

// Callback returns false if there was a failure (and the failure callback will be called appropriately)
typedef bool (*DynamicCacheCallback)(DynamicCacheElement* elem, void *userData);
typedef void (*DynamicCacheFailureCallback)(DynamicCacheElement* elem, void *userData);
// Gets data asynchronously.  Note: if data retrieval fails (bad HD, CRC failure,
//  etc), the failed callback is called instead.  This MUST be handled, as it's
//  rather likely!  If the failed callback is called, future calls to
//  dynamicCacheFileExists/dynamicCacheIsFileUpToDate will return false.
// Note that the CRC checking *should* find bad data, you should still do some
//  validation on your data, and call your own failure callback if the data
//  is not good.
void dynamicCacheGetAsync(DynamicCache *cache, const char *filename, DynamicCacheCallback callback, DynamicCacheFailureCallback callback_failed, void *userData);

// Synchronous version which can be called in threads which are doing data loading
void *dynamicCacheGetSync(DynamicCache *cache, const char *filename, int *data_size);
// Synchronous version with the same parameters as the async for easy debugging
void dynamicCacheGetSync2(DynamicCache *cache, const char *filename, DynamicCacheCallback callback, DynamicCacheFailureCallback callback_failed, void *userData);

// Forces all current loads to finish
void dynamicCacheForceLoadingToFinish(DynamicCache *cache);

void dynamicCacheCheckAll(F32 elapsed); // Call this once per frame per thread

int dynamicCacheLoadsPending(void);

int dynamicCacheNumEntries(DynamicCache *cache);

void dynamicCacheVerifyHogg(DynamicCache *cache);
bool dynamicCacheHasGlobalDeps(const DynamicCache *cache);

// Functions to call in your callback functions

const void *dceGetData(const DynamicCacheElement *elem);
void *dceGetDataAndAcquireOwnership(DynamicCacheElement *elem); // Remember to free it later!
int dceGetDataSize(const DynamicCacheElement *elem);
const DynamicCache *dceGetParentCache(const DynamicCacheElement *elem);
const char* dynamicCacheGetFilename(const DynamicCache *cache);

bool dynamicCacheFreeRAMCache(DynamicCache * cache); // Returns true if it was actually RAMCached

typedef void (*DynamicCacheFilenameCallback)(const char *filename);
void dynamicCacheSetGlobalDepsInvalidatedCallback(DynamicCache * cache, DynamicCacheFilenameCallback callback);
