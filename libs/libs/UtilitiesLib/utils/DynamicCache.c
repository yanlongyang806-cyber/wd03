#include "DynamicCache.h"

#include "hoglib.h"
#include "piglib.h"
#include "rand.h"
#include "EString.h"
#include "serialize.h"
#include "structInternals.h"
#include "MemoryPool.h"
#include "wininclude.h"
#include "fileLoader.h"
#include "timing.h"
#include "fileCache.h"
#include "ScratchStack.h"
#include "StashTable.h"
#include "StringCache.h"
#include "qsortG.h"
#include "crypt.h"
#include "memlog.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

extern MemLog hogmemlog;

#define DYNAMICCACHE_VERSION 2
#define DYNAMIC_CACHE_FLUSH_CHECK_TIME 60 // In seconds, check one cache per this much time

// Special flag values in NULL headers
#define DYNAMIC_CACHE_USE_GLOBAL_DEPS 1
#define DYNAMIC_CACHE_REDIRECT 2


#define DEBUG_MULTIPLE_IDENTICAL_READS 0 // Turn this on if two reads are deleting or reading the same file

struct DynamicCache {
	char *filename;
	int version;
	int minSecondsBeforeFlush;
	size_t desiredSize;
	size_t maxSize;
	HogFile *hog_file;
	int loadsPending;
	DWORD threadId;
	DynamicCacheFlags flags;
	FileList global_deps;
	bool disallowRedirects;
	bool global_deps_up_to_date;
	U32 global_deps_last_checked_timestamp;
	DynamicCacheFilenameCallback global_deps_invalidated_callback;
#if DEBUG_MULTIPLE_IDENTICAL_READS
	StashTable htFilesLoading;
#endif
};

#define GLOBAL_DEPS_NAME "?global_deps"
#define GLOBAL_DEPS_CHECK_FREQ 2000 // ms

struct DynamicCacheElement {
	DynamicCache *parent_cache;
	void *data;
	int dataSize;
};

typedef struct DynamicCacheJob {
	DynamicCacheCallback callback;
	DynamicCacheFailureCallback callback_failed;
	void *userData;
	const char *file_name;
	DynamicCacheElement element;
} DynamicCacheJob;

MP_DEFINE(DynamicCacheJob);

typedef struct DynamicCachePerThreadData
{
	DynamicCache **dynamicCacheList; // List of all allocated dynamicCaches for this thread
	F32 timeout;
	int last_checked_index;
} DynamicCachePerThreadData;

static bool dynamicCacheInited=false;
static CRITICAL_SECTION dynamicCacheJobListCritSect;
static DynamicCacheJob **dynamicCacheJobList;
static int dynamicCacheAsserts=-1;

#define DYNAMIC_CACHE_PIGERR_MODE ((dynamicCacheAsserts==-1)?(isDevelopmentMode()?PIGERR_ASSERT:PIGERR_PRINTF):(dynamicCacheAsserts?PIGERR_ASSERT:PIGERR_PRINTF))

// Makes the dynamic cache assert on error instead of just printfing
AUTO_CMD_INT(dynamicCacheAsserts, dynamicCacheAsserts) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

static F32 dynamicCacheDebugFailureRate=0.0f;

static bool dynamicCacheIsFileUpToDateInternal(DynamicCache *cache, const char *file_name, bool displayMessages);

AUTO_RUN;
void dynamicCacheStartup(void)
{
	if (!dynamicCacheInited) {
		dynamicCacheInited = true;
		InitializeCriticalSection(&dynamicCacheJobListCritSect);
		MP_CREATE(DynamicCacheJob, 16);
	}
}

void dynamicCacheSetGlobalDepsInvalidatedCallback(DynamicCache * cache, DynamicCacheFilenameCallback callback)
{
	cache->global_deps_invalidated_callback = callback;
}

static DynamicCachePerThreadData *getPerThreadData(void)
{
	DynamicCachePerThreadData *data;
	STATIC_THREAD_ALLOC(data);
	return data;
}

static DynamicCacheJob *dynamicCacheJobCreate(void)
{
	// Could put in CritSect here if we do this from anything but the main thread
	DynamicCacheJob *ret;
	EnterCriticalSection(&dynamicCacheJobListCritSect);
	ret = MP_ALLOC(DynamicCacheJob);
	LeaveCriticalSection(&dynamicCacheJobListCritSect);
	return ret;
}

static void dynamicCacheJobDestroy(DynamicCacheJob *job)
{
	SAFE_FREE(job->element.data);
	MP_FREE(DynamicCacheJob, job);
}

static void queueJob(DynamicCacheJob *job)
{
	EnterCriticalSection(&dynamicCacheJobListCritSect);
	eaPush(&dynamicCacheJobList, job);
	LeaveCriticalSection(&dynamicCacheJobListCritSect);
}

void dynamicCacheDebugSetFailureRate(F32 rate) // 1.0 effectively disables the cache
{
	dynamicCacheDebugFailureRate = CLAMP(rate, 0.f, 1.f);
}

bool dynamicCacheDebugRandomFailure(void)
{
	return randomPositiveF32() < dynamicCacheDebugFailureRate;
}

static bool pruneOutOfDateFiles(HogFile *handle, HogFileIndex file_index, const char* elementName, void *userData)
{
	if (strEndsWith(elementName, ".dcache2"))
	{
		if (!dynamicCacheIsFileUpToDateInternal(userData, elementName, true))
			hogFileModifyDelete(handle, file_index);
	}
	return true;
}

// Try to load the global deps if it exists
static FileList dynamicCacheLoadGlobalDeps(HogFile *hog_file)
{
	SimpleBufHandle buf;
	FileList file_list=NULL;
	bool checksum_valid=false;
	void *data=NULL;
	U32 dataSize=0;

	if (!hog_file)
		return NULL;

	hogFileLock(hog_file); // keep file_index consistent
	{
		HogFileIndex file_index = hogFileFind(hog_file, GLOBAL_DEPS_NAME);
		if (file_index != HOG_INVALID_INDEX)
			data = hogFileExtract(hog_file, file_index, &dataSize, &checksum_valid);
	}
	hogFileUnlock(hog_file);

	if (!data) {
		// Bad or non-existent file
		return NULL;
	}
	if (!checksum_valid) {
		// Bad file
		free(data);
		return NULL;
	}
	buf = SimpleBufSetData(data, (unsigned int)dataSize);
	if (!FileListRead(&file_list, buf))
	{
		FileListDestroy(&file_list);
	}

	SimpleBufClose(buf); // Frees data pointer
	return file_list;
}

DynamicCache *dynamicCacheCreate(const char *cacheFileName, int version, size_t desiredSize, size_t maxSize, int minSecondsBeforeFlush, DynamicCacheFlags flags)
{
//	char buf[MAX_PATH];
	DynamicCache *cache;
	HogFileCreateFlags hog_flags = (flags&(DYNAMIC_CACHE_NO_TIMESTAMPS|DYNAMIC_CACHE_RAM_CACHED))?HOG_NO_INTERNAL_TIMESTAMPS:HOG_DEFAULT;
	assert(dynamicCacheInited);
	if (minSecondsBeforeFlush == -1 && !(flags & DYNAMIC_CACHE_RAM_CACHED)) {
//		fileLocateRead(cacheFileName, buf);
		hog_flags |= HOG_NOCREATE | HOG_READONLY;
	} else {
		hog_flags |= HOG_MUST_BE_WRITABLE|HOG_NO_REPAIR;
//		fileLocateWrite(cacheFileName, buf);
	}
	if (flags & DYNAMIC_CACHE_RAM_CACHED)
		hog_flags |= HOG_RAM_CACHED;
//	cacheFileName = buf;
	cache = calloc(sizeof(*cache), 1);
	cache->filename = strdup(cacheFileName);
	cache->version = version;
	cache->minSecondsBeforeFlush = minSecondsBeforeFlush;
	cache->desiredSize = desiredSize;
	cache->maxSize = maxSize;
	cache->hog_file = hogFileRead(cacheFileName, NULL, DYNAMIC_CACHE_PIGERR_MODE, NULL, hog_flags);
	cache->threadId = GetCurrentThreadId();
	cache->flags = flags;
	if (!cache->hog_file && minSecondsBeforeFlush != -1) {
		memlog_printf(&hogmemlog, "dynamicCacheCreate(%s): hog file failed to open the first time, deleting and retrying", cacheFileName);
		// Failed to read for some reason, try deleting it and trying again
		fileForceRemove(cacheFileName);
		cache->hog_file = hogFileRead(cacheFileName, NULL, DYNAMIC_CACHE_PIGERR_MODE, NULL, hog_flags);
	}
	if (!cache->hog_file) {
		// Terminal failure, run in non-caching mode (silently fail)
		memlog_printf(&hogmemlog, "dynamicCacheCreate(%s): hog file failed to open, failing", cacheFileName);
	} else {
		char expectedVersion[100];
		HogFileIndex index;
		// Read successfully, verify
		// Can't do this: GetVRML + Game both need it!  hogFileSetSingleAppMode(cache->hog_file, true);
		// Check version number
		sprintf(expectedVersion, "%d_%d.version", DYNAMICCACHE_VERSION, version);
		index = hogFileFind(cache->hog_file, expectedVersion);
		if (index == HOG_INVALID_INDEX) {
			// Either the version has changed, or this is a fresh file
			if (minSecondsBeforeFlush == -1) {
				// Read only, cannot delete!
				hogFileDestroy(cache->hog_file, true);
				cache->hog_file = NULL;
				memlog_printf(&hogmemlog, "dynamicCacheCreate(%s): read-only and did not find correct version file, failing", cacheFileName);
			} else {
				// Delete all existing files, and create a new index with the right version
				NewPigEntry temp_entry = {0};
				memlog_printf(&hogmemlog, "dynamicCacheCreate(%s): did not find correct version file, deleting all", cacheFileName);
				hogDeleteAllFiles(cache->hog_file);
				temp_entry.data = calloc(1, 1);
				temp_entry.size = 1;
				temp_entry.timestamp = 0;
				temp_entry.fname = expectedVersion;
				hogFileModifyUpdateNamedSync2(cache->hog_file, &temp_entry);
			}
		}
		// Because checking if something is up to date is now async/slow, doing
		//  this at open time is very costly.  For the shader cache, we prune all
		//  old files out of date or otherwise.  For other caches, we'll let the
		//  regular flushing handle it.
		//if (cache->hog_file && minSecondsBeforeFlush != -1)
		//	hogScanAllFiles(cache->hog_file, pruneOutOfDateFiles, cache);

		// Try to load the global deps if it exists
		assert(!cache->global_deps);
		cache->global_deps = dynamicCacheLoadGlobalDeps(cache->hog_file);
		cache->global_deps_last_checked_timestamp = 0;
	}
#if DEBUG_MULTIPLE_IDENTICAL_READS
	cache->htFilesLoading = stashTableCreateWithStringKeys(32, StashDefault);
#endif
	eaPush(&getPerThreadData()->dynamicCacheList, cache);
	return cache;
}

int cmpFileEntry(const void *a, const void *b)
{
	const FileEntry **fa = (const FileEntry**)a;
	const FileEntry **fb = (const FileEntry**)b;
	int ret = stricmp((*fa)->path, (*fb)->path);
	assert(ret); // Should not have duplicate names in this table
	return ret;
}

static void freeString(char* s){
	free(s);
}

void dynamicCacheConsistify(const char *cacheFileName)
{
	HogFile *src_hog;
	HogFile *dst_hog;
	char tempname[MAX_PATH];
	bool bCreated;
	U32 i;
	const char **filenames=NULL;
	StashTable stFileEntries = stashTableCreateWithStringKeys(64, StashDefault);
	StashTable stFileEntriesSource = stashTableCreateWithStringKeys(64, StashDefault);
	StashTable stAlreadyWrittenEntries = stashTableCreateInt(32*1024);
	bool bUseGlobalDeps=false;
	bool bFileHasGlobalDeps=false;
	int memlog_count=0;
	changeFileExt(cacheFileName, ".tmp", tempname);
	src_hog = hogFileRead(cacheFileName, NULL, PIGERR_ASSERT, NULL, HOG_READONLY|HOG_NOCREATE);
	assert(src_hog);
	fileForceRemove(tempname);
	dst_hog = hogFileRead(tempname, &bCreated, PIGERR_ASSERT, NULL, HOG_MUST_BE_WRITABLE|HOG_NO_INTERNAL_TIMESTAMPS);
	assert(dst_hog);
	assert(bCreated);

	hogFileLock(src_hog);
	hogFileLock(dst_hog);

	for (i=0; i<hogFileGetNumFiles(src_hog); i++)
	{
		const char *filename = hogFileGetFileName(src_hog, i);
		if (hogFileIsSpecialFile(src_hog, i))
			continue;
		if (!filename)
			continue;
		if (stricmp(filename, GLOBAL_DEPS_NAME)==0)
		{
			// If src has global_deps, it should be invalid, and everything would have recompiled
			// Though this happens sometimes when merging back and forth between caches on the builder
		} else {
			eaPush(&filenames, filename);
		}
	}

	eaQSort(filenames, strCmp);

	{
		FileList global_deps = dynamicCacheLoadGlobalDeps(src_hog);
		if (FileListLength(&global_deps))
			bFileHasGlobalDeps = true;
		FileListDestroy(&global_deps);
	}

	// First calculate deps so if our assert fires, we have not modified the file yet.

	for (i=0; i<eaUSize(&filenames); i++)
	{
		int index = hogFileFind(src_hog, filenames[i]);
		U32 data_size;
		bool bChecksumValid;
		void *data = hogFileExtract(src_hog, index, &data_size, &bChecksumValid);
		U32 headerSize2;
		SimpleBufHandle buf;
		FileList file_list=NULL;
		int loop_check=0;
		assert(data);
		assert(bChecksumValid);

		if (strEndsWith(filenames[i], ".dcache2"))
		{
			// Rip out deps and add to global list
			buf = SimpleBufSetData(data, (unsigned int)data_size);
			SimpleBufReadU32(&headerSize2, buf); // Header size
			if (headerSize2==0)
			{
				U32 flags=0;
				// Contains flags
				SimpleBufReadU32(&flags, buf);
				if (flags == DYNAMIC_CACHE_USE_GLOBAL_DEPS)
				{
					assert(bFileHasGlobalDeps);
					if (memlog_count < 5)
					{
						memlog_printf(NULL, "dynamicCacheConsistify: %susing global deps because of %s", (memlog_count==4)?"(stopping logging) ":"", filenames[i]);
						memlog_count++;
					}
					bUseGlobalDeps = true;
				} else if (flags == DYNAMIC_CACHE_REDIRECT) {
				} else {
					assert(0); // Unknown flags!
				}
			} else {
				// Extract FileList to be added to the global deps
				if (!FileListRead(&file_list, buf))
				{
					assert(0); // FileList should be good, we just built it!
				}
				FOR_EACH_IN_EARRAY(file_list, FileEntry, file_entry)
				{
					if (stashAddPointer(stFileEntries, file_entry->path, file_entry, false))
					{
						if(!stashAddPointer(stFileEntriesSource, file_entry->path, strdup(filenames[i]), false)){
							assert(0);
						}
						// New, remove so it is not freed
						eaRemoveFast(&file_list, ifile_entryIndex);
					}
				}
				FOR_EACH_END;
				FileListDestroy(&file_list); // Free unused entries
			}
			SimpleBufClose(buf); // Frees extracted data pointer
		} else {
			SAFE_FREE(data);
		}
	}

	if (bUseGlobalDeps)
	{
		// Read and merge global deps
		FileList global_deps = dynamicCacheLoadGlobalDeps(src_hog);
		assert(FileListLength(&global_deps)); // Otherwise something referenced the global deps, but it was not there?!
		FOR_EACH_IN_EARRAY(global_deps, FileEntry, file_entry)
		{
			FileEntry *existing;
			if (stashFindPointer(stFileEntries, file_entry->path, &existing))
			{
				if(	existing->path != file_entry->path ||
					existing->date != file_entry->date)
				{
					char* source = NULL;
					
					stashFindPointer(stFileEntriesSource, file_entry->path, &source);
					
					assertmsgf(	existing->path == file_entry->path,
								"Found duplicate dependency while consistifying \"%s\": \"%s\" (source: \"%s\")",
								cacheFileName,
								existing->path,
								FIRST_IF_SET(source, "not found"));

					assertmsgf(	existing->date == file_entry->date,
								"Found mismatched date while consistifying \"%s\": \"%s\" (%u vs %u) (source: \"%s\")",
								cacheFileName,
								existing->path,
								existing->date,
								file_entry->date,
								FIRST_IF_SET(source, "not found"));
				}
			} else {
				verify(stashAddPointer(stFileEntries, file_entry->path, file_entry, false));
				eaRemoveFast(&global_deps, ifile_entryIndex);
			}
		}
		FOR_EACH_END;
		FileListDestroy(&global_deps);
	}
	
	stashTableDestroyEx(stFileEntriesSource, NULL, freeString);

	for (i=0; i<eaUSize(&filenames); i++)
	{
		int index = hogFileFind(src_hog, filenames[i]);
		U32 data_size;
		bool bChecksumValid;
		void *data = hogFileExtract(src_hog, index, &data_size, &bChecksumValid);
		U32 headerSize2;
		U32 payloadSize;
		SimpleBufHandle buf;
		FileList file_list=NULL;
		int loop_check=0;
		assert(data);
		assert(bChecksumValid);

		if (strEndsWith(filenames[i], ".dcache2"))
		{
			void *newdata;
			U32 ignored;

restart_after_redirect:
			// Rip out deps and add to global list
			buf = SimpleBufSetData(data, (unsigned int)data_size);
			SimpleBufReadU32(&headerSize2, buf); // Header size
			if (headerSize2==0)
			{
				U32 flags=0;
				// Contains flags
				SimpleBufReadU32(&flags, buf);
				if (flags == DYNAMIC_CACHE_USE_GLOBAL_DEPS)
				{
					// Just copy as-is, already using global deps (presumably nothing changed)
					payloadSize = data_size - 8;
					newdata = SimpleBufGetDataAndClose(buf, &data_size);
					//hogFileModifyUpdateNamedSync(dst_hog, filenames[i], data, data_size, 0, NULL);
				} else if (flags == DYNAMIC_CACHE_REDIRECT) {
					char *redirectedFileName;
					HogFileIndex file_index;
					SimpleBufReadString(&redirectedFileName, buf);
					file_index = hogFileFind(src_hog, redirectedFileName);
					assert(file_index != HOG_INVALID_INDEX); // Redirecting to something which is not there anymore - should get pruned at an earlier step?
					data = hogFileExtract(src_hog, file_index, &data_size, &bChecksumValid);
					SimpleBufClose(buf); // Frees extracted data pointer
					// Use this newly extracted data pointer, but the original filename and continue the logic
					assert(loop_check < 1); // Shouldn't redirect twice
					loop_check++;
					goto restart_after_redirect;
				} else {
					assert(0); // Unknown flags!
				}
			} else {
				assert(headerSize2 && headerSize2 < data_size);
				// Copy payload into new, smaller file to send to the hogg
				payloadSize = data_size - headerSize2;
				newdata = calloc(payloadSize + 8, 1); // need to zero at least the first 4 bytes
				memcpy_s(OFFSET_PTR(newdata, 8), payloadSize, OFFSET_PTR(data, headerSize2), payloadSize);
				SimpleBufClose(buf); // Frees extracted data pointer
			}
			// Check if this data can be redirected to an already written file
			{
				U32 crc = cryptAdler32(OFFSET_PTR(newdata, 8), payloadSize);
				char *existing_filename;
				if (stashIntFindPointer(stAlreadyWrittenEntries, crc, &existing_filename))
				{
					HogFileIndex file_index = hogFileFind(dst_hog, existing_filename);
					U32 unpacked_size;
					hogFileGetSizes(dst_hog, file_index, &unpacked_size, NULL);
					assert(unpacked_size == payloadSize + 8); // Hash collision otherwise, need to etract or at least compare sizes?
					// Write redirect to this file
					buf = SimpleBufOpenWrite("", 0, NULL, 0, 0);
					SimpleBufWriteU32(0, buf);
					SimpleBufWriteU32(DYNAMIC_CACHE_REDIRECT, buf);
					SimpleBufWriteString(existing_filename, buf);
					data = SimpleBufGetDataAndClose(buf, &data_size);
					hogFileModifyUpdateNamedSync(dst_hog, filenames[i], data, data_size, 0, NULL);
					SAFE_FREE(newdata);
				} else {
					// Add to table
					verify(stashIntAddPointer(stAlreadyWrittenEntries, crc, (char*)filenames[i], false));
					// Write to disk
					// Write the header "size" and flags
					buf = SimpleBufSetData(newdata, payloadSize + 8);
					SimpleBufWriteU32(0, buf);
					SimpleBufWriteU32(DYNAMIC_CACHE_USE_GLOBAL_DEPS, buf);
					newdata = SimpleBufGetDataAndClose(buf, &ignored);
					hogFileModifyUpdateNamedSync(dst_hog, filenames[i], newdata, payloadSize + 8, 0, NULL);
				}
			}
		} else {
			// Just the version file?
			hogFileModifyUpdateNamedSync(dst_hog, filenames[i], data, data_size, 0, NULL);
		}
	}

	// Write global_deps
	{
		void *data;
		int data_size;
		SimpleBufHandle buf = SimpleBufOpenWrite("", 0, NULL, false, false);
		FileList global_deps=NULL;
		FOR_EACH_IN_STASHTABLE(stFileEntries, FileEntry, file_entry)
		{
			eaPush(&global_deps, file_entry);
		}
		FOR_EACH_END;
		eaQSort(global_deps, cmpFileEntry);
		FileListWrite(&global_deps, buf, NULL, "GlobalDeps");
		FileListDestroy(&global_deps);
		data = SimpleBufGetDataAndClose(buf, &data_size);
		hogFileModifyUpdateNamedSync(dst_hog, GLOBAL_DEPS_NAME, data, data_size, 0, NULL);
	}

	stashTableDestroy(stFileEntries); // Contents were destroyed as part of the FileList
	stashTableDestroy(stAlreadyWrittenEntries);

	hogFileModifyTruncate(dst_hog);

	hogFileUnlock(dst_hog);
	hogFileUnlock(src_hog);
	hogFileDestroy(src_hog, true);
	hogFileDestroy(dst_hog, true);
	eaDestroy(&filenames);

	assert(fileExists(tempname));
	fileForceRemove(cacheFileName);
	verify(rename(tempname, cacheFileName));
}


// This could be moved to hoglib.c and used in a generic fashion if needed.
// This overwrites an existing file index, unless dest_index is HOG_INVALID_INDEX, then creates a new file
void hogCopyBetweenHogs(HogFile *src_hog, HogFileIndex src_index, HogFile *dest_hog, HogFileIndex dest_index)
{
	U32 buf_size;
	U32 byte_count;
	NewPigEntry entry = {0};
	if (dest_index == HOG_INVALID_INDEX)
	{
		// Copy source name
		entry.fname = hogFileGetFileName(src_hog, src_index);
	} else {
		// Dest might be different name
		entry.fname = hogFileGetFileName(dest_hog, dest_index);
	}
	entry.timestamp = hogFileGetFileTimestamp(src_hog, src_index);
	entry.header_data = hogFileGetHeaderData(src_hog, src_index, &entry.header_data_size);
	entry.checksum[0] = hogFileGetFileChecksum(src_hog, src_index);
	hogFileGetSizes(src_hog, src_index, &entry.size, &entry.pack_size);
	buf_size = entry.pack_size?entry.pack_size:entry.size;
	entry.data = calloc(buf_size+1, 1);
	if (entry.pack_size)
	{
		byte_count = hogFileExtractRawBytes(src_hog, src_index, entry.data, 0, entry.pack_size, false, 0);
		assert(byte_count == entry.pack_size);
	} else {
		entry.dont_pack = 1;
		byte_count = hogFileExtractBytes(src_hog, src_index, entry.data, 0, entry.size);
		assert((int)byte_count == entry.size);
	}
	hogFileModifyUpdateNamedSync2(dest_hog, &entry);
}

void dynamicCacheExpandRedirects(DynamicCache *cache)
{
	U32 i;
	hogFileLock(cache->hog_file);
	for (i=0; i<hogFileGetNumFiles(cache->hog_file); i++)
	{
		const char *filename = hogFileGetFileName(cache->hog_file, i);
		U32 data_size;
		bool bChecksumValid;
		void *data;
		U32 headerSize2;
		SimpleBufHandle buf;

		if (!filename || hogFileIsSpecialFile(cache->hog_file, i) || !strEndsWith(filename, ".dcache2"))
			continue;

		if (hogFileGetFileSize(cache->hog_file, i) > 125)
			continue; // File is too large to be a redirect

		data = hogFileExtract(cache->hog_file, i, &data_size, &bChecksumValid);

		assert(data);
		assert(bChecksumValid);

		buf = SimpleBufSetData(data, (unsigned int)data_size);
		data = NULL;
		SimpleBufReadU32(&headerSize2, buf); // Header size
		if (headerSize2==0)
		{
			U32 flags=0;
			// Contains flags
			SimpleBufReadU32(&flags, buf);
			if (flags == DYNAMIC_CACHE_REDIRECT)
			{
				char *newFileName;
				HogFileIndex new_file_index;

				SimpleBufReadString(&newFileName, buf);
				// newFileName points into the buffer
				new_file_index = hogFileFind(cache->hog_file, newFileName);
				if (new_file_index == HOG_INVALID_INDEX)
				{
					assert(0);
				} else {
					hogCopyBetweenHogs(cache->hog_file, new_file_index, cache->hog_file, i);
				}
			}
		}
		SimpleBufClose(buf); // Frees extracted data pointer
	}
	hogFileUnlock(cache->hog_file);
}

static int g_merge_count;

typedef struct MergeIntoCacheData
{
	DynamicCache *destCache;
	DynamicCache *srcCache;
} MergeIntoCacheData;

static bool mergeIntoCache(HogFile *handle, HogFileIndex file_index, const char* elementName, void *userData)
{
	char filename[MAX_PATH];
	MergeIntoCacheData *data = userData;
	bool bCopy=false;
	strcpy(filename, elementName);
	if (stricmp(filename, GLOBAL_DEPS_NAME)==0)
	{
		int other_index = hogFileFind(data->destCache->hog_file, GLOBAL_DEPS_NAME);
		// Global deps file, needs to be copied verbatim, unless it's identical
		if (other_index==HOG_INVALID_INDEX || 
			hogFileGetFileChecksum(handle, file_index) != hogFileGetFileChecksum(data->destCache->hog_file, other_index) ||
			hogFileGetFileSize(handle, file_index) != hogFileGetFileSize(data->destCache->hog_file, other_index))
		{
			bCopy = true;
		}
	} else if (!strEndsWith(filename, ".dcache2")) {
		return true;
	} else {
		*strrchr(filename, '.') = '\0';
		if (!dynamicCacheIsFileUpToDateSync_WillStall(data->destCache, filename) &&
			dynamicCacheIsFileUpToDateInternal(data->srcCache, elementName, false))
		{
			bCopy = true;
		}
	}
	if (bCopy)
	{
		// Start it copying!
		int data_size;
		U32 unpacked_size, pack_size;
		NewPigEntry npe = {0};
		hogFileGetSizes(handle, file_index, &unpacked_size, &pack_size);
		npe.fname = elementName;
		npe.timestamp = (data->destCache->flags&(DYNAMIC_CACHE_NO_TIMESTAMPS|DYNAMIC_CACHE_RAM_CACHED))?0:_time32(NULL);
		npe.size = unpacked_size;
		npe.pack_size = pack_size;
		npe.checksum[0] = hogFileGetFileChecksum(handle, file_index);
		if (pack_size) {
			npe.data = hogFileExtractCompressed(handle, file_index, &data_size);
			npe.dont_pack = 0;
			npe.must_pack = 1;
		} else {
			npe.data = hogFileExtract(handle, file_index, &data_size, NULL);
			npe.dont_pack = 1;
			npe.must_pack = 0;
		}
		hogFileModifyUpdateNamedSync2(data->destCache->hog_file, &npe);
		g_merge_count++;
	}
	return true;
}

static bool pruneOrphansCallback(HogFile *handle, HogFileIndex file_index, const char* elementName, void *userData)
{
	if (strEndsWith(elementName, ".dcache2") || stricmp(elementName, GLOBAL_DEPS_NAME)==0)
	{
		DynamicCache *srcCache = userData;
		if (HOG_INVALID_INDEX == hogFileFind(srcCache->hog_file, elementName))
		{
			hogFileModifyDelete(handle, file_index);
			g_merge_count++;
		}
	}
	return true;
}


int dynamicCacheMergePrecise(DynamicCache *cache, const char *srcCacheFileName, bool pruneOrphans)
{
	g_merge_count = 0;

	if (!cache->hog_file)
		return 0; // Cache failed to create, silently fail

	if (fileExists(srcCacheFileName)) {
		U32 file_index;
		bool bUpToDate=false;

		hogFileLock(cache->hog_file); // keep file_index consistent

		if (HOG_INVALID_INDEX != (file_index = hogFileFind(cache->hog_file, "merged.version")))
		{
			__time32_t timestamp = hogFileGetFileTimestamp(cache->hog_file, file_index);
			if (timestamp == fileLastChanged(srcCacheFileName))
				bUpToDate = true;
		}
		
		if (!bUpToDate)
		{
			__time32_t timestamp;
			DynamicCache *srcCache = dynamicCacheCreate(srcCacheFileName, cache->version, DYNAMIC_CACHE_READONLY);
			if (srcCache->hog_file) // May be null if it failed to load, version mismatch, etc
			{
				MergeIntoCacheData data;
				data.srcCache = srcCache;
				data.destCache = cache;
				hogFileLock(srcCache->hog_file);
				hogFileLock(cache->hog_file);
				if (pruneOrphans)
				{
					hogScanAllFiles(cache->hog_file, pruneOrphansCallback, srcCache);
					if (cache->global_deps && HOG_INVALID_INDEX == hogFileFind(cache->hog_file, GLOBAL_DEPS_NAME))
					{
						// We pruned the global_deps file!
						FileListDestroy(&cache->global_deps);
						cache->global_deps_up_to_date = 0;
						cache->global_deps_last_checked_timestamp = 0;
					}
				}

				hogScanAllFiles(srcCache->hog_file, mergeIntoCache, &data);
				if (g_merge_count)
				{
					// Copy over any files which were previously redirects
					// Don't want to do this the first time - if nothing has actually changed
					//   this would waste a lot of time
					cache->disallowRedirects = true;
					hogScanAllFiles(srcCache->hog_file, mergeIntoCache, &data);
					cache->disallowRedirects = false;
				}
				hogFileUnlock(cache->hog_file);
				hogFileUnlock(srcCache->hog_file);
			}

			dynamicCacheDestroy(srcCache);

			//if (!pruneOrphans)
			//{
				// If pruning orphans, don't update/create the merged.version file,
				//  so that build machines do not generated a bunch of differences
				//  in the checked in hogg files when nothing has changed.
				timestamp = fileLastChanged(srcCacheFileName);
				hogFileModifyUpdateNamedSync(cache->hog_file, "merged.version", malloc(1), 1, timestamp, NULL);
			//}
			verbose_printf("Merged %d files between caches.\n", g_merge_count);
		}
		hogFileUnlock(cache->hog_file);
	}
	return g_merge_count;
}

typedef struct UpToDateData
{
	int up_to_date;
	int total;
	DynamicCache *cache;
} UpToDateData;

static bool checkUpToDate(HogFile *handle, HogFileIndex file_index, const char* elementName, void *userData)
{
	UpToDateData *data = userData;
	if (data->total > 20) // Only check a few of them
		return false;
	if (!strEndsWith(elementName, ".dcache2"))
		return true;
	if (dynamicCacheIsFileUpToDateInternal(data->cache, elementName, false))
	{
		data->up_to_date++;
	}
	data->total++;
	return true;
}

// Returns true on success
static bool copyFile(const char *src, const char *dest)
{
	FILE *fin;
	FILE *fout;
	char *buf;
	size_t count;
	bool ret=true;
	fin = fileOpen(src, "rb");
	if (!fin)
		return false;
	fout = fileOpen(dest, "wb");
	if (!fout)
	{
		fclose(fin);
		return false;
	}
#define BUF_SIZE (64*1024)
	buf = ScratchAlloc(64*1024);
	while (count = fread(buf, 1, BUF_SIZE, fin))
	{
		if (count!=fwrite(buf, 1, count, fout))
			ret = false;
	}
#undef BUF_SIZE
	fclose(fin);
	fclose(fout);
	ScratchFree(buf);
	if (!ret)
		DeleteFile_UTF8(dest);
	return ret;
}

bool dynamicCacheFreeRAMCache(DynamicCache * cache)
{
	bool ret = false;
	if (cache->flags & DYNAMIC_CACHE_RAM_CACHED)
	{
		EnterCriticalSection(&dynamicCacheJobListCritSect);
		if (cache->flags & DYNAMIC_CACHE_RAM_CACHED)
		{
			if (cache->hog_file)
				hogFileFreeRAMCache(cache->hog_file);

			cache->flags &=~ DYNAMIC_CACHE_RAM_CACHED;
			ret = true;
		}
		LeaveCriticalSection(&dynamicCacheJobListCritSect);
	}
	return ret;
}

DynamicCache *dynamicCacheMergeQuick(const char *destCacheFileName, const char *srcCacheFileName, int *numMerged,
									 int version, size_t desiredSize, size_t maxSize, int minSecondsBeforeFlush, DynamicCacheFlags flags)
{
	DynamicCache *cache;
	// Check if it needs to be updated, if so, copy it, otherwise, just open it
	cache = dynamicCacheCreate(destCacheFileName, version, desiredSize, maxSize, minSecondsBeforeFlush, flags);
	if (!cache->hog_file)
	{
		memlog_printf(&hogmemlog, "dynamicCacheMergeQuick(%s): dynamicCacheCreate returned NULL", destCacheFileName);
	}
	if (fileExists(srcCacheFileName))
	{
		U32 file_index;
		U32 timestamp;
		bool bUpToDate=false;
		bool bNeedsCopy=false;

		if (cache->hog_file)
		{
			hogFileLock(cache->hog_file); // keep file_index consistent
			if (HOG_INVALID_INDEX != (file_index = hogFileFind(cache->hog_file, "merged.version")))
			{
				__time32_t timestamp2 = hogFileGetFileTimestamp(cache->hog_file, file_index);
				if (timestamp2 == fileLastChanged(srcCacheFileName))
					bUpToDate = true;
			}
			hogFileUnlock(cache->hog_file);
		}

		if (!bUpToDate)
		{
			// Dest has not been merged from this source, check that the source
			//  is up to date (will not be on programmer computers with local
			//  shader changes
			bNeedsCopy = true;
			if (isDevelopmentMode() && cache->hog_file)
			{
				DynamicCache *srcCache = dynamicCacheCreate(srcCacheFileName, cache->version, DYNAMIC_CACHE_READONLY);
				bool bSrcUpToDate=false;
				if (srcCache->hog_file) // May be null if it failed to load, version mismatch, etc
				{
					UpToDateData data = {0};
					data.cache = srcCache;
					hogFileLock(srcCache->hog_file);
					hogScanAllFiles(srcCache->hog_file, checkUpToDate, &data);
					hogFileUnlock(srcCache->hog_file);
					if (data.up_to_date > 0.8 * data.total)
					{
						// At least 80% up to date, good enough
						bSrcUpToDate = true;
					}
				}
				dynamicCacheDestroy(srcCache);
				if (!bSrcUpToDate)
					bNeedsCopy = false;
			}
		}
		if (bNeedsCopy)
		{
			// close cache
			dynamicCacheDestroy(cache);
			// overwrite with source cache
			loadstart_printf("Copying prebuilt shader cache...");
			if (!copyFile(srcCacheFileName, destCacheFileName))
			{
				printf("Warning: Error copying cache file %s to %s\n", srcCacheFileName, destCacheFileName);
			}
			loadend_printf(" done.");
			// re-open cache
			// But without RAMCached, because we're going to destroy it almost immediately
			// Can't have minSecondsBeforeFlush == -1 here, it'll be read-only
			cache = dynamicCacheCreate(destCacheFileName, version, desiredSize, maxSize, 1000000, (flags&~DYNAMIC_CACHE_RAM_CACHED));
			memlog_printf(&hogmemlog, "dynamicCacheMergeQuick(%s):did copy: dynamicCacheCreate returned hog of %08p", destCacheFileName, cache->hog_file);

			if (numMerged)
				*numMerged += dynamicCacheNumEntries(cache);
			assert(!bUpToDate);
		}

		if (!bUpToDate)
		{
			bool bWantRAMCache = !!(flags & DYNAMIC_CACHE_RAM_CACHED);
			// regardless of whether we copied or not, need to change the merged.version so we don't run these checks again
			timestamp = fileLastChanged(srcCacheFileName);
			dynamicCacheFreeRAMCache(cache);
			if (cache->hog_file)
				hogFileModifyUpdateNamedSync(cache->hog_file, "merged.version", malloc(1), 1, timestamp, NULL);
			if (bWantRAMCache)
			{
				// Close and re-open the hogg, now RAM-cached again
				loadstart_printf("Closing cache due to invalided RAM cache...");
				dynamicCacheDestroy(cache);
				loadend_printf(" done.");
				loadstart_printf("Reopening cache with populated RAM cache...");
				cache = dynamicCacheCreate(destCacheFileName, version, desiredSize, maxSize, minSecondsBeforeFlush, flags);
				memlog_printf(&hogmemlog, "dynamicCacheMergeQuick(%s):re-opened RAMCached: dynamicCacheCreate returned hog of %08p", destCacheFileName, cache->hog_file);
				loadend_printf(" done.");
			}
		}
	} else {
		if (!cache->hog_file)
		{
			// Might have failed to create because it didn't exist yet and we were asking for ram-cached or something
			// Can't have minSecondsBeforeFlush == -1 here, it'll be read-only
			cache = dynamicCacheCreate(destCacheFileName, version, desiredSize, maxSize, 1000000, (flags&~DYNAMIC_CACHE_RAM_CACHED));
			memlog_printf(&hogmemlog, "dynamicCacheMergeQuick(%s):failed to open first time, nothing to copy: dynamicCacheCreate returned hog of %08p", destCacheFileName, cache->hog_file);
		}
	}

	return cache;
}

static bool pruneOldFiles(HogFile *handle, HogFileIndex file_index, const char* elementName, void *userData)
{
	if (strEndsWith(elementName, ".dcache2"))
	{
		U32 prune_timestamp = *(U32*)userData;
		U32 timestamp = hogFileGetFileTimestamp(handle, file_index);
		if (timestamp <= prune_timestamp)
			hogFileModifyDelete(handle, file_index);
	}
	return true;
}

int dynamicCachePruneOldFiles(DynamicCache *cache, U32 timestamp)
{
	hogFileLock(cache->hog_file);
	hogScanAllFiles(cache->hog_file, pruneOldFiles, &timestamp);
	hogFileUnlock(cache->hog_file);
	return 0;
}


int dynamicCacheGetVersion(const DynamicCache *cache)
{
	return cache->version;
}

void dynamicCacheDestroy(DynamicCache *cache)
{
	assert(cache->threadId == GetCurrentThreadId());
	eaFindAndRemove(&getPerThreadData()->dynamicCacheList, cache);
	if (cache->hog_file) {
		hogFileDestroy(cache->hog_file, true);
		cache->hog_file = NULL;
	}
	FileListDestroy(&cache->global_deps);
	SAFE_FREE(cache->filename);
	SAFE_FREE(cache);
}

void dynamicCacheSafeDestroy(DynamicCache **cache)
{
	if (*cache) {
		// guaranteeing that the hog file is flushed regardless of the number of references being made to it.
		if ((*cache)->hog_file)
			hogFileModifyFlush((*cache)->hog_file);
		dynamicCacheDestroy(*cache);
		*cache = NULL;
	}
}

void dynamicCacheUpdateFile(DynamicCache *cache, const char *filename, const void *data, int dataSize, FileList *dependencyList)
{
	char elementName[CRYPTIC_MAX_PATH];
	SimpleBufHandle buf;
	int headerSize;
	void *headerData;
	void *finalData;
	int finalSize;
	if (!cache->hog_file) {
		// No cache available
		return;
	}

	dynamicCacheFreeRAMCache(cache); // Go into write-mode if needed

	// Build .dcache2 file to be serialized
	sprintf(elementName, "%s.dcache2", filename);

	if (data == NULL)
	{
		hogFileModifyUpdateNamedAsync(cache->hog_file, elementName, NULL, 0, 0, NULL);
	} else {
		buf = SimpleBufOpenWrite("", 0, NULL, false, false);
		SimpleBufWriteU32(0, buf); // Header size
		FileListWrite(dependencyList, buf, NULL, "DependencyList");
		headerSize = SimpleBufTell(buf);
		SimpleBufSeek(buf, 0, SEEK_SET);
		SimpleBufWriteU32(headerSize, buf); // Header size
		headerData = SimpleBufGetData(buf, &headerSize);

		finalSize = headerSize + dataSize;
		finalData = malloc(finalSize);
		memcpy(finalData, headerData, headerSize);
		memcpy(OFFSET_PTR(finalData, headerSize), data, dataSize);
		SimpleBufClose(buf);

		// Send this data to .hogg to serialize
		hogFileModifyUpdateNamedAsync(cache->hog_file, elementName, finalData, finalSize, (cache->flags&(DYNAMIC_CACHE_NO_TIMESTAMPS|DYNAMIC_CACHE_RAM_CACHED))?0:_time32(NULL), NULL);
	}
}

bool dynamicCacheFileExists(DynamicCache *cache, const char *filename)
{
	char elementName[CRYPTIC_MAX_PATH];
	HogFileIndex file_index;

	assert(cache);
	if (!cache->hog_file) // No cache available
		return false;

	sprintf(elementName, "%s.dcache2", filename);
	file_index = hogFileFind(cache->hog_file, elementName);
	return (file_index != HOG_INVALID_INDEX);
}

bool dynamicCacheIsFileUpToDateSync_WillStall(DynamicCache *cache, const char *filename)
{
	char elementName[CRYPTIC_MAX_PATH];

	assert(cache);
	if (!cache->hog_file) // No cache available
		return false;

	sprintf(elementName, "%s.dcache2", filename);
	return dynamicCacheIsFileUpToDateInternal(cache, elementName, true);
}

void dynamicCacheTouchFile(DynamicCache *cache, const char *filename)
{
	char elementName[CRYPTIC_MAX_PATH];
	HogFileIndex file_index;

	assert(cache);
	if (!cache->hog_file) // No cache available
		return;

	sprintf(elementName, "%s.dcache2", filename);
	hogFileLock(cache->hog_file); // keep file_index consistent
	file_index = hogFileFind(cache->hog_file, elementName);
	if (file_index != HOG_INVALID_INDEX)
	{
		// Update last used timestamp for MRU caching
		hogFileModifyUpdateTimestamp(cache->hog_file, file_index, (cache->flags&(DYNAMIC_CACHE_NO_TIMESTAMPS|DYNAMIC_CACHE_RAM_CACHED))?0:_time32(NULL));
	}
	hogFileUnlock(cache->hog_file);
}


static bool dynamicCacheIsFileUpToDateInternalGetPayload(DynamicCache *cache, const char *file_name, bool displayMessages, void **dataOut, int *dataSizeOut)
{
	void *data;
	int dataSize;
	int headerSize2;
	SimpleBufHandle buf;
	int i;
	FileList file_list=NULL;
	bool checksum_valid;
	bool bUseGlobalDeps;
	bool bAlreadyHaveData=false;

	if (dataSizeOut)
		*dataSizeOut = 0;
	if (dataOut)
		*dataOut = NULL;

	if (dynamicCacheDebugRandomFailure()) {
		// Fake something went wrong
		return false;
	}

	hogFileLock(cache->hog_file); // keep file_index consistent
	{
		HogFileIndex file_index = hogFileFind(cache->hog_file, file_name);
		data = hogFileExtract(cache->hog_file, file_index, &dataSize, &checksum_valid);
	}
	hogFileUnlock(cache->hog_file);
	if (!data) {
		// Bad file
		return false;
	}
	if (!checksum_valid || dataSize < sizeof(U32)) {
		// Bad file
		free(data);
		return false;
	}
	buf = SimpleBufSetData(data, (unsigned int)dataSize);
	SimpleBufReadU32(&headerSize2, buf); // Header size
	if (headerSize2 > dataSize) {
		// Bad file
		SimpleBufClose(buf); // Frees data pointer
		return false;
	}

	if (headerSize2 == 0)
	{
		// contains special flags
		U32 flags=0;
		SimpleBufReadU32(&flags, buf);
		if (flags == DYNAMIC_CACHE_USE_GLOBAL_DEPS) {
			// Uses global deps
			file_list = cache->global_deps;
			bUseGlobalDeps = true;
			// fix up sizes
			headerSize2 = 8;
			dataSize -= 8;
		} else if (flags == DYNAMIC_CACHE_REDIRECT) {
			char *newFileName;
			bool bRet;
			
			if (cache->disallowRedirects)
			{
				SimpleBufClose(buf); // Frees data pointer
				return false;
			}

			SimpleBufReadString(&newFileName, buf);
			// newFileName points into the buffer
			// call on redirected file recursively
			bRet = dynamicCacheIsFileUpToDateInternalGetPayload(cache, newFileName, displayMessages, dataOut, dataSizeOut);

			if (!bRet)
			{
				SimpleBufClose(buf); // Frees data pointer
				return false;
			}

			// Redirects are only guaranteed valid if the hogg uses global_deps and global_deps is valid, check that too
			file_list = cache->global_deps;
			bUseGlobalDeps = true;
			bAlreadyHaveData = true;
		} else {
			// Unknown flags!
			SimpleBufClose(buf); // Frees data pointer
			return false;
		}
	} else {
		if (!FileListRead(&file_list, buf))
		{
			FileListDestroy(&file_list);
			SimpleBufClose(buf); // Frees data pointer
			return false;
		}
		bUseGlobalDeps = false;

		dataSize = dataSize - headerSize2; // Calc payload size
	}

	// Check to see if any of the dependent files are newer
	if (bUseGlobalDeps && !FileListLength(&cache->global_deps))
	{
		if (dataOut)
			SAFE_FREE(*dataOut); // if a redirect, possibly already filled in
		SimpleBufClose(buf); // Frees data pointer
		return false;
	}
	else if (bUseGlobalDeps && cache->global_deps_up_to_date && (timerCpuMs() - cache->global_deps_last_checked_timestamp) < GLOBAL_DEPS_CHECK_FREQ)
	{
		// It was recently checked and found to be good
	} else {
		for (i=eaSize(&file_list)-1; i>=0; i--) 
		{
			FileEntry* file_entry = file_list[i];
			bool bUpToDate = true;
			static const char *lastPath=NULL;
			if (file_entry->date & FILELIST_CHECKSUM_BIT) {
				U32 diskchecksum = fileCachedChecksum(file_entry->path);
				if ((diskchecksum | FILELIST_CHECKSUM_BIT) != (U32)file_entry->date) {
					if (file_entry->path != lastPath)
					{
						if (bUseGlobalDeps && cache->global_deps_invalidated_callback)
							cache->global_deps_invalidated_callback(file_entry->path);
						if (displayMessages)
						{
							verbose_printf("Source file \"%s\" failed checksum check.\n", file_entry->path);
							lastPath = file_entry->path;
						}
					}
					bUpToDate = false;
				}
			} else {
				__time32_t diskdate = fileLastChanged(file_entry->path);
				if (diskdate == 0 && isProductionMode())
				{
					// Most source files don't exist in production mode, assume up to date
				} else {
					if (diskdate != file_entry->date && ABS_UNS_DIFF(diskdate, file_entry->date) != 3600) {
						if (file_entry->path != lastPath)
						{
							if (bUseGlobalDeps && cache->global_deps_invalidated_callback)
								cache->global_deps_invalidated_callback(file_entry->path);
							if (displayMessages)
							{
								verbose_printf("Source file \"%s\" failed timestamp check.\n", file_entry->path);
								lastPath = file_entry->path;
							}
						}
						bUpToDate = false;
					}
				}
			}
			if (!bUpToDate) {
				if (!bUseGlobalDeps)
					FileListDestroy(&file_list);
				if (dataOut)
					SAFE_FREE(*dataOut); // if a redirect, possibly already filled in
				SimpleBufClose(buf); // Frees data pointer
				return false;
			}
		}
		if (bUseGlobalDeps)
		{
			cache->global_deps_up_to_date = true;
			cache->global_deps_last_checked_timestamp = timerCpuMs();
		}
	}

	if (!bUseGlobalDeps)
		FileListDestroy(&file_list);

	// Good, copy payload
	if (!bAlreadyHaveData) // unless already filled in
	{
		if (dataSizeOut)
			*dataSizeOut = dataSize;
		if (dataOut)
		{
			*dataOut = malloc(dataSize);
			memcpy(*dataOut, OFFSET_PTR(data, headerSize2), dataSize);
		}
	}

	SimpleBufClose(buf); // Frees data pointer
	return true;
}

static bool dynamicCacheIsFileUpToDateInternal(DynamicCache *cache, const char *file_name, bool displayMessages)
{
	return dynamicCacheIsFileUpToDateInternalGetPayload(cache, file_name, true, NULL, NULL);
}

// This is the only function which executes anywhere other than the thread that owns the dynamic cache
static void dynamicCacheLoadDataCallback(const char *filename, void *data)
{
	DynamicCacheJob *job = (DynamicCacheJob *)data;

	PERFINFO_AUTO_START_FUNC();

#if DEBUG_MULTIPLE_IDENTICAL_READS
	EnterCriticalSection(&dynamicCacheJobListCritSect);
	verify(stashRemoveInt(job->element.parent_cache->htFilesLoading, job->file_name, NULL));
	LeaveCriticalSection(&dynamicCacheJobListCritSect);
#endif

	if (dynamicCacheIsFileUpToDateInternalGetPayload(job->element.parent_cache, job->file_name, true, &job->element.data, &job->element.dataSize) && job->element.data)
	{
		// It's good!
	} else {
		// Data extraction failed or invalid checksum or not up to date
		// Delete so next query won't try to load this again
		dynamicCacheFreeRAMCache(job->element.parent_cache);
		// If you get a assert in here (deleting an already removed file), enable DEBUG_MULTIPLE_IDENTICAL_READS to track it to the caller (will assert on the second read call for the duplicate)
		hogFileModifyDeleteNamed(job->element.parent_cache->hog_file, job->file_name);
	}

	queueJob(job);

	PERFINFO_AUTO_STOP();
}

void dynamicCacheGetAsync(DynamicCache *cache, const char *filename, DynamicCacheCallback callback, DynamicCacheFailureCallback callback_failed, void *userData)
{
	char elementName[CRYPTIC_MAX_PATH];
	DynamicCacheJob *job;
	HogFileIndex file_index;

	assert(cache);
	//assert(cache->hog_file);

	assert(cache->threadId == GetCurrentThreadId());

	// Create job
	job = dynamicCacheJobCreate();
	cache->loadsPending++;
	job->callback = callback;
	job->callback_failed = callback_failed;
	job->userData = userData;
	job->element.parent_cache = cache;

	sprintf(elementName, "%s.dcache2", filename);
	job->file_name = allocAddString(elementName);
	hogFileLock(cache->hog_file); // keep file_index consistent
	file_index = hogFileFind(cache->hog_file, job->file_name);
	if (file_index == HOG_INVALID_INDEX)
	{
		// Something went wrong
		// job->element.data is NULL, so callback_failed will be called
		queueJob(job);
		hogFileUnlock(cache->hog_file);
		return;
	}

	// Update last used timestamp for MRU caching
	if (!(cache->flags&(DYNAMIC_CACHE_NO_TIMESTAMPS|DYNAMIC_CACHE_RAM_CACHED)))
		hogFileModifyUpdateTimestamp(cache->hog_file, file_index, _time32(NULL));

	hogFileUnlock(cache->hog_file);

#if DEBUG_MULTIPLE_IDENTICAL_READS
	EnterCriticalSection(&dynamicCacheJobListCritSect);
	verify(stashAddInt(cache->htFilesLoading, job->file_name, 1, false));
	LeaveCriticalSection(&dynamicCacheJobListCritSect);
#endif

	fileLoaderRequestAsyncExec(hogFileGetArchiveFileName(cache->hog_file), FILE_MEDIUM_HIGH_PRIORITY, false, dynamicCacheLoadDataCallback, job);

}

void *dynamicCacheGetSync(DynamicCache *cache, const char *filename, int *data_size)
{
	char elementName[CRYPTIC_MAX_PATH];
	void *data;
	HogFileIndex file_index;

	assert(cache);
	if (!cache->hog_file)
		return NULL;

	sprintf(elementName, "%s.dcache2", filename);
	hogFileLock(cache->hog_file); // keep file_index consistent
	file_index = hogFileFind(cache->hog_file, elementName);
	if (file_index == HOG_INVALID_INDEX)
	{
		// Something went wrong
		hogFileUnlock(cache->hog_file);
		return NULL;
	}

	// Update last used timestamp for MRU caching
	if (!(cache->flags&(DYNAMIC_CACHE_NO_TIMESTAMPS|DYNAMIC_CACHE_RAM_CACHED)))
		hogFileModifyUpdateTimestamp(cache->hog_file, file_index, _time32(NULL));
	hogFileUnlock(cache->hog_file);

	if (dynamicCacheIsFileUpToDateInternalGetPayload(cache, elementName, true, &data, data_size))
	{
		return data;
	}
	*data_size = 0;
	return NULL;
}

void dynamicCacheGetSync2(DynamicCache *cache, const char *filename, DynamicCacheCallback callback, DynamicCacheFailureCallback callback_failed, void *userData)
{
	dynamicCacheGetAsync(cache, filename, callback, callback_failed, userData);
	dynamicCacheForceLoadingToFinish(cache);
}

static void dynamicCacheCheckSpecificForLoadedData(DynamicCache *cache)
{
	int i;
	DWORD threadID = GetCurrentThreadId();
	// Iterate through hasDataReady list
	EnterCriticalSection(&dynamicCacheJobListCritSect);
	for (i=eaSize(&dynamicCacheJobList)-1; i>=0 && i<eaSize(&dynamicCacheJobList); i--) {
		DynamicCacheJob *job = dynamicCacheJobList[i];
		if ((!cache && job->element.parent_cache->threadId == threadID) || job->element.parent_cache == cache) {
			bool bFailed = !job->element.data;
			assert(job->element.parent_cache->threadId == threadID);
			eaRemoveFast(&dynamicCacheJobList, i); // Remove *before* calling callback
			if (job->element.data) {
				// Must have been read successfully
				bool success = job->callback(&job->element, job->userData);
				if (!success) {
					SAFE_FREE(job->element.data);
					bFailed = true;
					// Remove it from the hog file
					hogFileModifyDeleteNamed(job->element.parent_cache->hog_file, job->file_name);
				}
			}
			if (bFailed) {
				// Failed!
				job->callback_failed(NULL, job->userData);
			}
			job->element.parent_cache->loadsPending--;
			dynamicCacheJobDestroy(job);
		}
	}
	LeaveCriticalSection(&dynamicCacheJobListCritSect);
}

typedef struct DCSortable {
	int index;
	__time32_t timestamp;
} DCSortable;

static int dynamicCacheSortFlush(const DCSortable *a, const DCSortable *b)
{
	return a->timestamp - b->timestamp;
}

int dynamic_cache_debug_num_pruned;

void dynamicCacheCheckSpecificForFlush(DynamicCache *cache)
{
	size_t total_size=0;
	size_t total_young_size=0;
	U32 oldTimeCutoff;
	U32 j, i;
	U32 num_files_in_hog;
	U32 num_files=0;
	DCSortable *file_list;

	dynamic_cache_debug_num_pruned = 0;

	assert(cache);
	if (!cache->hog_file) {
		// Failed to be created, silently fail
		return;
	}
	if (cache->minSecondsBeforeFlush == -1)
		return;
	assert(cache->threadId == GetCurrentThreadId());
	assert(!(cache->flags&(DYNAMIC_CACHE_NO_TIMESTAMPS|DYNAMIC_CACHE_RAM_CACHED)));

	hogFileLock(cache->hog_file);
	hogFileLockDataCS(cache->hog_file);
	if (hogFileGetQueuedModSize(cache->hog_file))
	{
		// There are queued updates, which means some of the queries below might need to cause
		//   a flush, which is not valid if we're manually locking the data CS
		hogFileUnlockDataCS(cache->hog_file);
		hogFileUnlock(cache->hog_file);
		return; // Nothing to do
	}

	// Cache will hold any files (regardless of age) up to desiredSize
	// Cache will flush oldest files older than minSecondsBeforeFlush if size is > desiredSize
	// Cache will flush oldest files (even younger than minSecondsBeforeFlush) if size is > maxSize
	oldTimeCutoff = (U32)(_time32(NULL) - cache->minSecondsBeforeFlush);
	num_files_in_hog = hogFileGetNumFiles(cache->hog_file);
	for (j=0; j<num_files_in_hog; j++) {
		const char *filename = hogFileGetFileName(cache->hog_file, j);
		int fresh;
		U32 packed_size, unpacked_size;
		if (!filename || hogFileIsSpecialFile(cache->hog_file, j) || strEndsWith(filename, ".version"))
			continue;
		fresh = hogFileGetFileTimestampInternal(cache->hog_file, j) > oldTimeCutoff;
		hogFileGetSizesInternal(cache->hog_file, j, &unpacked_size, &packed_size);
		if (!packed_size)
			packed_size = unpacked_size;
		total_size += packed_size;
		if (fresh)
			total_young_size += packed_size;
		num_files++;
	}
	if (total_size < cache->desiredSize)
	{
		hogFileUnlockDataCS(cache->hog_file);
		hogFileUnlock(cache->hog_file);
		return; // Nothing to do
	}
	// We have more than desired
	if (total_young_size == total_size &&
		total_size < cache->maxSize)
	{
		hogFileUnlockDataCS(cache->hog_file);
		hogFileUnlock(cache->hog_file);
		return; // Only young files, and not over max
	}
	// Sort, and then flush files, from oldest to youngest, until either we're below desired size, or
	//  we're at a young file and below maxSize
	if (num_files * sizeof(file_list[0]) > MAX_STACK_ESTR) {
		file_list = malloc(num_files * sizeof(file_list[0]));
	} else {
		file_list = _alloca(num_files * sizeof(file_list[0]));
	}
	for (j=0, i=0; j<num_files_in_hog; j++) {
		const char *filename = hogFileGetFileName(cache->hog_file, j);
		if (!filename || hogFileIsSpecialFile(cache->hog_file, j) || strEndsWith(filename, ".version"))
			continue;
		assert(i < num_files); // Control logic here must match control logic in for loop above
		file_list[i].index = j;
		file_list[i].timestamp = hogFileGetFileTimestampInternal(cache->hog_file, j);
		i++;
	}
	qsort(file_list, num_files, sizeof(file_list[0]), dynamicCacheSortFlush);
	hogFileUnlockDataCS(cache->hog_file);
	for (i=0; i<num_files; i++)
	{
		U32 packed_size, unpacked_size;
		if (total_size < cache->desiredSize)
			break;
		if (total_size < cache->maxSize &&
			total_size <= total_young_size) // <= since we're not updating total_young_size below
			break;
		// Else, need to free one!
		hogFileGetSizes(cache->hog_file, file_list[i].index, &unpacked_size, &packed_size);
		if (!packed_size)
			packed_size = unpacked_size;
		total_size -= packed_size;
		hogFileModifyDelete(cache->hog_file, file_list[i].index);
		dynamic_cache_debug_num_pruned++;
	}

	if (num_files * sizeof(file_list[0]) > MAX_STACK_ESTR) {
		free(file_list);
	}
	hogFileUnlock(cache->hog_file);
}

void dynamicCacheCheckAll(F32 elapsed) // Call this once per frame
{
	PERFINFO_AUTO_START_FUNC();
	if (dynamicCacheInited) {
		DynamicCachePerThreadData *perThreadData = getPerThreadData();
		dynamicCacheCheckSpecificForLoadedData(NULL);

		// Once every so often (minutes?) iterate through a cache and flush old entries
		if (perThreadData->dynamicCacheList)
		{
			if (perThreadData->timeout == 0) {
				perThreadData->timeout = DYNAMIC_CACHE_FLUSH_CHECK_TIME;
				perThreadData->last_checked_index = -1;
			}
			perThreadData->timeout -= elapsed;
			if (perThreadData->timeout <= 0) {
				int next_index = perThreadData->last_checked_index;
				next_index++;
				if (next_index < 0) {
					next_index = 0;
				} else if (next_index >= eaSize(&perThreadData->dynamicCacheList)) {
					next_index = 0;
				}
				if (!perThreadData->dynamicCacheList[next_index]->loadsPending) {
					// Do the check
					perThreadData->timeout = DYNAMIC_CACHE_FLUSH_CHECK_TIME;
					dynamicCacheCheckSpecificForFlush(perThreadData->dynamicCacheList[next_index]);
					perThreadData->last_checked_index = next_index;
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

void dynamicCacheForceLoadingToFinish(DynamicCache *cache)
{
	assert(cache);
	if (!cache->hog_file) // No cache available
		return;
	assert(cache->threadId == GetCurrentThreadId());

	while (cache->loadsPending) {
		dynamicCacheCheckSpecificForLoadedData(cache);
		if (cache->loadsPending) {
			Sleep(1);
		}
	}
}

const void *dceGetData(const DynamicCacheElement *elem)
{
	return elem->data;
}

void *dceGetDataAndAcquireOwnership(DynamicCacheElement *elem)
{
	void *ret = elem->data;
	elem->data = NULL;
	return ret;
}

int dceGetDataSize(const DynamicCacheElement *elem)
{
	return elem->dataSize;
}

const DynamicCache *dceGetParentCache(const DynamicCacheElement *elem)
{
	return elem->parent_cache;
}

void dynamicCacheTest(void)
{
	DynamicCache *cache;
	char filename[CRYPTIC_MAX_PATH];
	char *test_data = malloc(800);
	int i, saved;
	U32 seed=0x12345678;
	for (i=0; i<800/4; i++)
		((U32*)test_data)[i] = randomU32Seeded(&seed, RandType_LCG);

#if _PS3
    sprintf(filename, "%s/testCache_ps3.hogg", fileCacheDir());
#else
	sprintf(filename, "%s/testCache.hogg", fileCacheDir());
#endif
	cache = dynamicCacheCreate(filename, randomInt(), 1024, 2048, 1, DYNAMIC_CACHE_DEFAULT); // Random version forces a new file

	// size < desiredSize test
	dynamicCacheUpdateFile(cache, "file1", test_data, 800, NULL);
	hogFileModifyFlush(cache->hog_file); // Writes are queued async
	dynamicCacheCheckSpecificForFlush(cache);
	assert(dynamic_cache_debug_num_pruned==0);
	Sleep(4000);
	dynamicCacheCheckSpecificForFlush(cache);
	assert(dynamic_cache_debug_num_pruned==0);

	// size > desiredSize, prune just the single old one
	dynamicCacheUpdateFile(cache, "file2", test_data, 800, NULL);
	hogFileModifyFlush(cache->hog_file); // Writes are queued async
	dynamicCacheCheckSpecificForFlush(cache);
	assert(dynamic_cache_debug_num_pruned==1); // Should prune file1
	Sleep(4000);
	dynamicCacheCheckSpecificForFlush(cache);
	assert(dynamic_cache_debug_num_pruned==0);

	// size > desired but all young, prune nothing
	dynamicCacheUpdateFile(cache, "file1", test_data, 800, NULL);
	dynamicCacheUpdateFile(cache, "file2", test_data, 800, NULL);
	hogFileModifyFlush(cache->hog_file); // Writes are queued async
	dynamicCacheCheckSpecificForFlush(cache);
	assert(dynamic_cache_debug_num_pruned==0);
	// After time, prune one (arbitrarily)
	Sleep(4000);
	dynamicCacheCheckSpecificForFlush(cache);
	assert(dynamic_cache_debug_num_pruned==1);

	// size > max, but all young, prune one (arbitrarily)
	dynamicCacheUpdateFile(cache, "file1", test_data, 800, NULL);
	dynamicCacheUpdateFile(cache, "file2", test_data, 800, NULL);
	dynamicCacheUpdateFile(cache, "file3", test_data, 800, NULL);
	hogFileModifyFlush(cache->hog_file); // Writes are queued async
	dynamicCacheCheckSpecificForFlush(cache);
	assert((saved=dynamic_cache_debug_num_pruned)>=1);
	// After time, prune another one (arbitrarily)
	Sleep(4000);
	dynamicCacheCheckSpecificForFlush(cache);
	assert((saved+dynamic_cache_debug_num_pruned)==2);

	free(test_data);
}

void dynamicCacheLoadTest(void)
{
	DynamicCache *cache;
	char hogfilename[CRYPTIC_MAX_PATH];
	int data_size = 10000;
	char *test_data = malloc(data_size);
	int i=0;

#if _PS3
    sprintf(hogfilename, "%s/testCache_ps3.hogg", fileCacheDir());
#else
	sprintf(hogfilename, "%s/testCache.hogg", fileCacheDir());
#endif
	cache = dynamicCacheCreate(hogfilename, 1, 1024, 2048, 100, DYNAMIC_CACHE_DEFAULT); // Random version forces a new file

	for (i=0; i<1000; i++) 
	{
		char filename[100];
		sprintf(filename, "file_%d", i);		
		dynamicCacheUpdateFile(cache, filename, test_data, data_size, NULL);
		dynamicCacheUpdateFile(cache, filename, test_data, data_size, NULL);
		dynamicCacheUpdateFile(cache, filename, test_data, data_size, NULL);
		dynamicCacheUpdateFile(cache, filename, NULL, 0, NULL);
		dynamicCacheUpdateFile(cache, filename, NULL, 0, NULL);
		dynamicCacheUpdateFile(cache, filename, test_data, data_size, NULL);
		dynamicCacheUpdateFile(cache, filename, NULL, 0, NULL);
		dynamicCacheUpdateFile(cache, filename, test_data, data_size, NULL);
		dynamicCacheUpdateFile(cache, filename, test_data, data_size, NULL);
		dynamicCacheUpdateFile(cache, filename, NULL, 0, NULL);
	}
	dynamicCacheDestroy(cache);
	free(test_data);
}

int dynamicCacheLoadsPending(void)
{
	int ret = 0;
	DynamicCachePerThreadData *perThreadData = getPerThreadData();

	FOR_EACH_IN_EARRAY(perThreadData->dynamicCacheList, DynamicCache, cache)
	{
		assert(cache->threadId == GetCurrentThreadId());
		ret += cache->loadsPending;
	}
	FOR_EACH_END;
	EnterCriticalSection(&dynamicCacheJobListCritSect);
	ret += eaSize(&dynamicCacheJobList);
	LeaveCriticalSection(&dynamicCacheJobListCritSect);
	return ret;
}

int dynamicCacheNumEntries(DynamicCache *cache)
{
	if (!cache || !cache->hog_file)
		return 0;
	return hogFileGetNumUserFiles(cache->hog_file) - 1;
}

void dynamicCacheVerifyHogg(DynamicCache *cache)
{
	char *estr=NULL;
	if (!cache || !cache->hog_file)
		return;
	hogFileVerifyToEstr(cache->hog_file, &estr, false);
	estrDestroy(&estr);
}

const char* dynamicCacheGetFilename(const DynamicCache *cache)
{
	return cache->filename;
}

bool dynamicCacheHasGlobalDeps(const DynamicCache *cache)
{
	return !!cache->global_deps;
}
