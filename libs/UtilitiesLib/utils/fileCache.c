#include "fileCache.h"
#include "file.h"
#include "hoglib.h"
#include "StashTable.h"
#include "MemoryPool.h"
#include "FolderCache.h"
#include "crypt.h"
#include "utils.h"
#include "StringCache.h"
#include "earray.h"
#include "fileWatch.h"
#include "DirMonitor.h"
#include "memlog.h"
#include "timing.h"
#include "UnitSpec.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

typedef struct FileCacheData {
	void *data;
	int data_size;
	U32 checksum;
	U32 timestamp;
	U32 lastuse_timestamp;
} FileCacheData;

CRITICAL_SECTION csFileCache;
StashTable stFileCache;
MP_DEFINE(FileCacheData);
static bool bFileCacheFrozen=false;

void fileCacheFreeze(bool freeze)
{
	bFileCacheFrozen = freeze;
}


AUTO_RUN;
void fileCacheInit(void)
{
	stFileCache = stashTableCreateWithStringKeys(16, StashDefault);
	MP_CREATE(FileCacheData, 16);
	InitializeCriticalSection(&csFileCache);
}

typedef enum FileCacheUpdateFlag {
	FileCacheUpdate_Checksum = 1<<0,
	FileCacheUpdate_Data = 1<<1,
} FileCacheUpdateFlag;

static FileCacheData *fileCacheUpdate(const char *filename, FileCacheUpdateFlag update_what, U32 known_timestamp)
{
	FileCacheData *data;
	bool bRet;
	U32 timestamp;
	char temp[CRYPTIC_MAX_PATH];
	strcpy(temp, filename);
	forwardSlashes(temp);
	filename = temp;
	filename = fileRelativePath(filename, temp);
	assert(!fileIsAbsolutePath(filename));

	EnterCriticalSection(&csFileCache);
	if (!stashFindPointer(stFileCache, filename, &data)) {
		data = MP_ALLOC(FileCacheData);
		bRet = stashAddPointer(stFileCache, allocAddFilename(filename), data, false);
		assert(bRet);
	}
	assert(data);
	data->lastuse_timestamp = timerCpuTicks();
	if (data->data)
		update_what |= FileCacheUpdate_Data;
	if (data->checksum)
		update_what |= FileCacheUpdate_Checksum;
	if (known_timestamp) {
		timestamp = known_timestamp;
	} else if (bFileCacheFrozen && data->timestamp) {
		// Assume up to date, make no filesystem calls
		timestamp = data->timestamp;
	} else {
		timestamp = fileLastChanged(filename);
	}
	if (data->timestamp != timestamp ||
		((update_what & FileCacheUpdate_Checksum) && !data->checksum) ||
		((update_what & FileCacheUpdate_Data) && !data->data))
	{
		if ((update_what & FileCacheUpdate_Data) && (!data->data || (data->timestamp != timestamp)))
		{
			SAFE_FREE(data->data);
			data->data = fileAlloc(filename, &data->data_size);
		}
		if ((update_what & FileCacheUpdate_Checksum) && (!data->checksum || (data->timestamp != timestamp)))
		{
			// Get checksum from hog system or file system
			FolderNode *node;
			node = FolderCacheQueryEx(folder_cache, filename, true, true);
			if (!node) {
				// File doesn't exist, I guess
				data->checksum = 0;
				FolderNodeLeaveCriticalSection();
			} else {
				if (node->virtual_location < 0 && !node->needs_patching) {
					HogFile *hog_file;
					// This file is in a pig, we can freely get the checksum
					assert(node->file_index>=0);
					hog_file = PigSetGetHogFile(VIRTUAL_LOCATION_TO_PIG_INDEX(node->virtual_location));
					data->checksum = hogFileGetFileChecksum(hog_file, node->file_index);
					FolderNodeLeaveCriticalSection();
				} else {
					U32 hash[4];
					FolderNodeLeaveCriticalSection();
					// This file is on disk, need to load it to get the checksum
					if (data->data) {
						cryptMD5(data->data, data->data_size, hash);
						data->checksum = hash[0];
					} else {
						void *file_data;
						int file_data_size;
						file_data = fileAlloc(filename, &file_data_size);
						assert(file_data);
						cryptMD5(file_data, file_data_size, hash);
						data->checksum = hash[0];
						free(file_data);
					}
				}
			}
		}
		data->timestamp = timestamp;
	}
	LeaveCriticalSection(&csFileCache);
	return data;
}

U32 fileCachedChecksum(const char *filename)
{
	FileCacheData *data = fileCacheUpdate(filename, FileCacheUpdate_Checksum, 0);
	return data->checksum;
}

const void *fileCachedData(const char *filename, int *data_size)
{
	FileCacheData *data = fileCacheUpdate(filename, FileCacheUpdate_Data, 0);
	if (data_size)
		*data_size = data->data_size;
	return data->data;
}

const void *fileCachUpdateAndGetData(const char *filename, int *data_size, U32 known_timestamp)
{
	FileCacheData *data = fileCacheUpdate(filename, FileCacheUpdate_Data, known_timestamp);
	if (data_size)
		*data_size = data->data_size;
	return data->data;
}

void fileCachePrune(void)
{
#define FILECACHE_TIMEOUT 5 // actual between this and 2x this
	static U32 last_check;
	U32 cputime;
	U32 expiry;
	EnterCriticalSection(&csFileCache);

	cputime = timerCpuTicks(); 
	expiry = cputime - timerCpuSpeed() * FILECACHE_TIMEOUT;
	if (cputime < expiry)
	{
		LeaveCriticalSection(&csFileCache);
		return; // wrapped, wait for it to unwrap
	}
	if (expiry < last_check)
	{
		LeaveCriticalSection(&csFileCache);
		return; // it's been less than FILECACHE_TIMEOUT seconds since last check
	}
	last_check = timerCpuTicks();

	FOR_EACH_IN_STASHTABLE2(stFileCache, elem)
	{
		FileCacheData *fcd = stashElementGetPointer(elem);
		if (fcd->lastuse_timestamp < expiry || fcd->lastuse_timestamp > cputime)
		{
			// Old
			SAFE_FREE(fcd->data);
			fcd->data_size = 0;
			stashRemovePointer(stFileCache, stashElementGetStringKey(elem), NULL);
			MP_FREE(FileCacheData, fcd);
		}
	}
	FOR_EACH_END;

	LeaveCriticalSection(&csFileCache);
}

AUTO_COMMAND;
void fileCacheDump(void)
{
	U32 totalSize=0;
	EnterCriticalSection(&csFileCache);
	printf("File Cache (%d elements):\n", stashGetCount(stFileCache));
	FOR_EACH_IN_STASHTABLE2(stFileCache, elem)
	{
		const char *filename = stashElementGetStringKey(elem);
		FileCacheData *fcd = stashElementGetPointer(elem);
		totalSize += fcd->data_size;
		printf("  %-40s : %s\n", filename, friendlyBytes(fcd->data_size));
	}
	FOR_EACH_END;
	printf("Total size: %s\n", friendlyBytes(totalSize));

	LeaveCriticalSection(&csFileCache);
}


//////////////////////////////////////////////////////////////////////////
// Begin fileCacheGetTimestamp() and related

typedef struct FileCacheDirFileData
{
	const char *filename;
	U32 timestamp;
} FileCacheDirFileData;

typedef struct FileCacheDirData
{
	const char *dir;
	DirMonitor *dirMonitor;
	FileCacheDirFileData **files;
} FileCacheDirData;

static FileCacheDirData **fileCacheDirs;
static CRITICAL_SECTION fileCacheDirCritSec;

AUTO_RUN;
void initFileCacheDir(void)
{
	InitializeCriticalSection(&fileCacheDirCritSec);
}

static FileCacheDirFileData *last_fileData2;
static const char *last_fileData_key2;
static FileCacheDirFileData *fileCacheGetFileData(FileCacheDirData *dirData, const char *fullpath_in, bool bAlloc)
{
	int i;
	const char *fullpath;
	assert(dirData);

	if (bAlloc) {
		fullpath = allocAddFilename(fullpath_in);
	} else {
		fullpath = allocFindString(fullpath_in);
		if (!fullpath) // Not monitored
			return NULL;
	}

	if (fullpath == last_fileData_key2) {
		return last_fileData2;
	}

	for (i=eaSize(&dirData->files)-1; i>=0; i--) {
		if (dirData->files[i]->filename == fullpath) {
			last_fileData2 = dirData->files[i];
			last_fileData_key2 = fullpath;
			return dirData->files[i];
		}
	}
	if (bAlloc) {
		FileCacheDirFileData *fileData = calloc(sizeof(*fileData), 1);
		FWStatType sbuf;
		if (-1==fwStat(fullpath, &sbuf))
		{
			fileData->timestamp = -1;
		} else {
			fileData->timestamp = sbuf.st_mtime;
		}
		fileData->filename = fullpath;
		eaPush(&dirData->files, fileData);
		last_fileData2 = fileData;
		last_fileData_key2 = fullpath;
		return fileData;
	} else {
		return NULL;
	}
}

static FileCacheDirFileData *last_fileData;
static const char *last_fileData_key;
static FileCacheDirData *last_dirData;
static const char *last_dirData_key;
static bool fileCacheGetDirDatas(const char *fullpath_in, FileCacheDirData **pdirData, FileCacheDirFileData **pfileData, bool bAlloc)
{
	char fullpath_buf[MAX_PATH];
	const char *fullpath;
	int i;
	FileCacheDirData *dirData=NULL;

	FileCacheDirFileData *fileData=NULL;

	if (pdirData)
		*pdirData = NULL;
	if (pfileData)
		*pfileData = NULL;

	strcpy(fullpath_buf, fullpath_in);
	forwardSlashes(fullpath_buf);
	if (bAlloc) {
		fullpath = allocAddFilename(fullpath_buf);
	} else {
		fullpath = allocFindString(fullpath_buf);
		if (!fullpath) // Not monitored
			return false;
	}

	if (fullpath == last_fileData_key) {
		fileData = last_fileData;
		dirData = last_dirData;
	} else {
		char dir_buf[MAX_PATH];
		const char *dir;

		strcpy(dir_buf, fullpath_buf);
		getDirectoryName(dir_buf);
		dir = allocAddString(dir_buf);

		if (dir == last_dirData_key) {
			dirData = last_dirData;
		} else {
			for (i=eaSize(&fileCacheDirs)-1; i>=0 && !dirData; i--) {
				if (fileCacheDirs[i]->dir == dir) {
					dirData = fileCacheDirs[i];
				}
			}
			if (!dirData) {
				if (bAlloc) {
					dirData = calloc(sizeof(*dirData), 1);
					dirData->dir = dir;
					// Create a DirMonitor for the directory
					dirData->dirMonitor = dirMonCreate();
					dirMonSetBufferSize(dirData->dirMonitor, 8*1024);
					dirMonSetFlags(dirData->dirMonitor, DIR_MON_NO_TIMESTAMPS);
					dirMonAddDirectory(dirData->dirMonitor, dir, 0, NULL);
					eaPush(&fileCacheDirs, dirData);
				} else {
					return false;
				}
			}

			last_dirData = dirData;
			last_dirData_key = dir;
		}

		fileData = fileCacheGetFileData(dirData, fullpath_buf, bAlloc);
		if (!fileData)
			return false;

		last_fileData = fileData;
		last_fileData_key = fullpath;
	}

	if (pfileData)
		*pfileData = fileData;
	if (pdirData)
		*pdirData = dirData;

	return true;
}

U32 fileCacheGetTimestamp(const char *fullpath_in)
{
	U32 ret;
	FileCacheDirData *dirData=NULL;
	FileCacheDirFileData *fileData=NULL;
	bool bGotData;
	char fullpath[1024];

	makefullpath(fullpath_in, fullpath);
	if (strlen(fullpath)+1 > MAX_PATH)
		return -1; // Long file path inside a hogg which is also a long file path

	EnterCriticalSection(&fileCacheDirCritSec);

	bGotData = fileCacheGetDirDatas(fullpath, &dirData, &fileData, true);
	assert(bGotData);

	// Found our data, query the DirMonitor
	{
		DirChangeInfo* bufferOverrun=NULL;
		DirChangeInfo *dci;
		while(dci = dirMonCheckDirs(dirData->dirMonitor, 0, &bufferOverrun)) {
			char update_fullpath[MAX_PATH];
			FileCacheDirFileData *update_fileData;

			sprintf(update_fullpath, "%s/%s", dci->dirname, dci->filename);
			update_fileData = fileCacheGetFileData(dirData, update_fullpath, false);
			if (!update_fileData) {
				//printf("Got callback on unwatched file: %s\n", update_fullpath); // Don't care
			} else {
				FWStatType sbuf;
				fwStat(update_fullpath, &sbuf);
				update_fileData->timestamp = sbuf.st_mtime;
			}
		}
		if (bufferOverrun)
		{
			int i;
			memlog_printf(NULL, "Buffer overrun in fileCache for %s: rescanning.\n", dirData->dir);
			// Buffer overrun, rescan the affected files
			for (i=eaSize(&dirData->files)-1; i>=0; i--) {
				FWStatType sbuf;
				fwStat(dirData->files[i]->filename, &sbuf);
				dirData->files[i]->timestamp = sbuf.st_mtime;
			}
		}
	}
	// Return the time
	ret = fileData->timestamp;
	LeaveCriticalSection(&fileCacheDirCritSec);
	return ret;
}

void fileCacheTimestampStopMonitoring(const char *fullpath_in)
{
	FileCacheDirData *dirData=NULL;
	FileCacheDirFileData *fileData=NULL;
	bool bGotData;
	char fullpath[1024];
	int idx;

	makefullpath(fullpath_in, fullpath);
	if (strlen(fullpath)+1 > MAX_PATH)
		return; // Long file path inside a hogg which is also a long file path

	EnterCriticalSection(&fileCacheDirCritSec);

	bGotData = fileCacheGetDirDatas(fullpath, &dirData, &fileData, false);
	assert(bGotData); // Otherwise, freeing something that wasn't monitored?

	// Found our file data, remove it from the folder and destroy it
	idx = eaFindAndRemove(&dirData->files, fileData);
	assert(idx != -1); // Wasn't in the list?
	free(fileData);

	// If the dir is empty, shut it down
	if (eaSize(&dirData->files)==0)
	{
		dirMonDestroy(dirData->dirMonitor);
		eaDestroy(&dirData->files);
		idx = eaFindAndRemove(&fileCacheDirs, dirData);
		assert(idx != -1); // Wasn't in the list?
		free(dirData);
	}

	// Clear caches
	last_fileData2 = NULL;
	last_fileData_key2 = NULL;
	last_fileData = NULL;
	last_fileData_key = NULL;
	last_dirData = NULL;
	last_dirData_key = NULL;

	LeaveCriticalSection(&fileCacheDirCritSec);
}

AUTO_COMMAND;
void fileCacheTimestampDump(void)
{
	int total_files=0;
	EnterCriticalSection(&fileCacheDirCritSec);
	printf("File Timestamp Cache (%d elements):\n", eaSize(&fileCacheDirs));
	FOR_EACH_IN_EARRAY(fileCacheDirs, FileCacheDirData, dirData)
	{
		printf("  %-40s : %d files\n", dirData->dir, eaSize(&dirData->files));
		total_files += eaSize(&dirData->files);
	}
	FOR_EACH_END;
	printf("%d total files\n", total_files);

	LeaveCriticalSection(&fileCacheDirCritSec);
}
