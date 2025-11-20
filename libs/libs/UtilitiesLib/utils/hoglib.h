/***************************************************************************



*
*
*  Important interfaces used by apps:
*	hogFileRead() - creates a new or loads an existing .hogg file
*	hogFileDestroy() - flushes, then closes a handle (does not delete the .hogg)
*	hogFileFind() - find a file by name in a hogg file
*	hogFileExtract() - read a file
*	hogFileModifyUpdateNamed() - updates a file (pass in NULL data to delete a file)
*/

#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "piglib.h"

#define HOG_OLD_VERSION 10
#define HOG_VERSION 11
#define HOG_INVALID_INDEX ((U32)-1)
#define HOG_HEADER_FLAG 0xDEADF00D // arbitrary
#define HOG_INVALID_HEADER_FLAG 0xBADBEEF // written when hog was opened in SuperUnsafe mode but never closed

#define HOG_DATALIST_FILENAME "?DataList"

// This is the filename that resource hogs will display as in hogFileGetArchiveFileName().
#define HOG_FILENAME_RESOURCE_PREFIX "?RESOURCE:"

#define HOG_ALL_UPDATES_ASYNC

#ifdef HOG_ALL_UPDATES_ASYNC
	#define hogFileModifyUpdateNamed(handle, relpath, data, size, timestamp, free_callback) hogFileModifyUpdateNamedAsync(handle, relpath, data, size, timestamp, free_callback)
	#define hogFileModifyUpdateNamed2(handle, entry) hogFileModifyUpdateNamedAsync2(handle, entry)
#else
	#define hogFileModifyUpdateNamed(handle, relpath, data, size, timestamp, free_callback) hogFileModifyUpdateNamedSync(handle, relpath, data, size, timestamp, free_callback)
	#define hogFileModifyUpdateNamed2(handle, entry) hogFileModifyUpdateNamedSync2(handle, entry)
#endif

#define hogFileModifyDeleteNamed(handle, relpath) hogFileModifyUpdateNamed(handle, relpath, NULL, 0, 0, NULL)

typedef struct ManagedThread ManagedThread;
typedef struct NewPigEntry NewPigEntry;
typedef struct HogFile HogFile;
typedef U32 HogFileIndex;

typedef enum PigErrLevel
{
	PIGERR_QUIET,
	PIGERR_PRINTF,
	PIGERR_ASSERT,
} PigErrLevel;

typedef enum HogFileCreateFlags {
	HOG_DEFAULT = 0,		// Creates if it doesn't exist
	HOG_NOCREATE = 1<<0,	// Only loads existing files
	HOG_READONLY = 1<<1,	// Opens file in read-only mode (will try to load hoggs within hoggs first (fileLocateRead))
	HOG_MUST_BE_WRITABLE = 1<<2, // Refuses to open the file in read-only mode, instead errors
	HOG_NO_INTERNAL_TIMESTAMPS = 1<<3, // Does not timestamp internal files
	HOG_NO_REPAIR = 1<<4, // Opening a hogg file fails instead of repairing the file, use with PIGERR_PRINTF
	HOG_NO_MUTEX = 1<<5, // Do not do any mutex locking, must (in any sane situation) be used with HOG_READONLY (otherwise use hogSetSingleAppMode for the perf boost without safety loss)
	HOG_SHARED_MEMORY = 1<<6, // Forces the hogg to load into shared memory, if available
	HOG_NO_ACCESS_BY_NAME = 1<<7, // Files are only accessed by index, do not need lookup information
	HOG_MULTIPLE_READS = 1<<8, // Allows multiple simultaneous reads (opens multiple file handles to the same file)
	HOG_MUST_BE_READONLY = 1<<9, // Refuses to open the file in writable mode (if a previous handle exists), instead errors
	HOG_RAM_CACHED = 1<<10, // Reads the entire hogg into memory, not compatible with HOG_MULTIPLE_READS
	HOG_APPEND_ONLY = 1<<11, // Only writes to end of the file, makes for very fast updates of small files, but grows space - use if defragmenting afterwards
	HOG_NO_JOURNAL = 1<<12, // "SuperUnsafe" mode - no journaling while writing - guaranteed unopenable (asserts) if not completely closed, but safe if completely closed.
	HOG_NO_STRING_CACHE = 1<<13, // Does not put filenames into the string cache
	HOG_SKIP_MUTEX = 1<<14, // Enabled at run-time, skips grabbing the mutex - set only by hogFileSetSkipMutex
} HogFileCreateFlags;

void hogFileDumpInfo(HogFile *handle, int verbose, int debug_verbose);
void hogFileDumpInfoToEstr(HogFile *handle, char **estr);
bool hogFileVerifyToEstr(HogFile *handle, char **estr, bool bDeleteCorruptFiles); // Checks all files to make sure they can be extracted and have the right CRC
bool hogFileAddFile(HogFile *hogFile, const char *file_name, const char *file_hogg_name);
void hogFileCopyFolder(HogFile *hogFile, const char *dirName, bool clearDirectory);
void hogFileDestroy(HogFile *handle, bool bFreeHandle);
SA_RET_OP_VALID HogFile *hogFileReadShared(SA_PARAM_NN_STR const char *filename, SA_PRE_OP_FREE SA_POST_OP_VALID bool *bCreated, PigErrLevel err_level, SA_PRE_OP_FREE SA_POST_OP_VALID int *err_return, HogFileCreateFlags flags, U32 datalist_journal_size);
SA_RET_OP_VALID HogFile *hogFileReadEx(SA_PARAM_NN_STR const char *filename, SA_PRE_OP_FREE SA_POST_OP_VALID bool *bCreated, PigErrLevel err_level, SA_PRE_OP_FREE SA_POST_OP_VALID int *err_return, HogFileCreateFlags flags, U32 datalist_journal_size); // Load a .HOGG file or creates a new empty one if it does not exist
SA_RET_OP_VALID HogFile *hogFileReadReadOnlySafe(SA_PARAM_NN_STR const char *filename, SA_PRE_OP_FREE SA_POST_OP_VALID bool *bCreated, PigErrLevel err_level, SA_PRE_OP_FREE SA_POST_OP_VALID int *err_return, HogFileCreateFlags flags); // Load a .HOGG file or creates a new empty one if it does not exist
SA_RET_OP_VALID HogFile *hogFileReadReadOnlySafeEx(SA_PARAM_NN_STR const char *filename, SA_PRE_OP_FREE SA_POST_OP_VALID bool *bCreated, PigErrLevel err_level, SA_PRE_OP_FREE SA_POST_OP_VALID int *err_return, HogFileCreateFlags flags, U32 datalist_journal_size); // Load a .HOGG file or creates a new empty one if it does not exist
SA_RET_OP_VALID HogFile *hogFileReadFromResource(int resource_id, PigErrLevel err_level, SA_PRE_OP_FREE SA_POST_OP_VALID int *err_return, HogFileCreateFlags flags); // Create a hog file from a special file handle; note that this can not be used with regular files as that would bypass hog sharing rules.
void hogFileAddRef(HogFile *handle); // add a reference to a hog that is already open
SA_RET_OP_VALID HogFile *hogFileRead(SA_PARAM_NN_STR const char *filename, SA_PRE_OP_FREE SA_POST_OP_VALID bool *bCreated, PigErrLevel err_level, SA_PRE_OP_FREE SA_POST_OP_VALID int *err_return, HogFileCreateFlags flags); // Load a .HOGG file or creates a new empty one if it does not exist
HogFileIndex hogFileFind(HogFile *handle, const char *relpath); // Find an entry in a Hog
bool hogFileIsOpenInMyProcess(SA_PARAM_NN_STR const char *filename);

// Return true if an entry with this name exists within this hog.
// WARNING: This function does not check for changes on the disk, so it may return stale data.  This is meant to be used for checking
// which files in a file list exist with in a hog, in performance-sensitive situations.  For regular usage, please try hogFileFind().
bool hogFileExistsNoChangeCheck(HogFile *handle, SA_PARAM_NN_STR const char *filename);

HogFileCreateFlags hogGetCreateFlags(HogFile *handle);

void hogFileSetSingleAppMode(HogFile *handle, bool singleAppMode);

void hogFileFreeRAMCache(HogFile *handle);

// Return false to stop scanning
typedef bool (*HogFileScanProcessor)(HogFile *handle, HogFileIndex index, const char* filename, void * userData);
void hogScanAllFiles(HogFile *handle, HogFileScanProcessor processor, void * userData);
void hogDeleteAllFiles(HogFile *handle);

// file_index == HOG_INVALID_INDEX indicates a delete
typedef void (*HogCallbackUpdated)(void *userData, const char* path, U32 filesize, U32 timestamp, HogFileIndex file_index);
void hogFileSetCallbacks(HogFile *handle, void *userData, HogCallbackUpdated fileUpdated);

void hogFileLock(HogFile *handle); // Acquires the multi-process mutex
void hogFileUnlock(HogFile *handle);
void hogFileCheckForModifications(HogFile *handle);
void hogFileSetSkipMutex(HogFile *handle, bool skip_mutex);
bool hogFileNeedsToFlush(HogFile *handle);
void *hogFileDupHandle(HogFile *handle, HogFileIndex file_index);

void hogReleaseHeaderData(HogFile *handle); // Must be on read-only hogg
void hogReleaseFileData(HogFile *handle);

void hogFileUnlockDataCS(HogFile *handle); // Internal use only
void hogFileLockDataCS(HogFile *handle); // Internal use only
U32 hogFileGetFileTimestampInternal(HogFile *handle, int index); // Internal use only, assumes data cs is locked
U32 hogFileGetFileSizeInternal(HogFile *handle, int index); // Internal use only, assumes data cs is locked
void hogFileGetSizesInternal(HogFile *handle, HogFileIndex file_index, U32 *unpacked, U32 *packed); // Internal use only, assumes data cs is locked


const char *hogFileGetArchiveFileName(HogFile *handle);
U32 hogFileGetNumFiles(HogFile *handle);
U32 hogFileGetNumUserFiles(HogFile *handle); // Slow!
U32 hogFileGetNumUsedFiles(HogFile *handle); // fast, probably equal to hogFileGetNumUserFiles() + 1
void hogFileReserveFiles(HogFile *handle, U32 numFiles); // reserve space for files
const char *hogFileGetFileName(HogFile *handle, int index); // Returns NULL if an empty slot
U32 hogFileGetFileTimestamp(HogFile *handle, int index);
U32 hogFileGetFileSize(HogFile *handle, int index);
void hogFileGetSizes(HogFile *handle, HogFileIndex file_index, U32 *unpacked, U32 *packed);
U32 hogFileGetFileChecksum(HogFile *handle, int index);
U64 hogFileGetOffset(HogFile *handle, HogFileIndex file_index);
S32 hogFileGetEAIDInternal(HogFile *handle, HogFileIndex file_index); // Just for debugging tools
int hogFileGetSharedRefCount(HogFile *handle); // Return the number of open handles for this file.
bool hogFileChecksumIsGood(HogFile *handle, int index);
bool hogFileIsZipped(HogFile *handle, int index);
bool hogFileIsSpecialFile(HogFile *handle, int index);
const U8 *hogFileGetHeaderData(HogFile *handle, int index, U32 *header_size); // Warning: pointer returned is volatile, and may be invalid after any hog file changes
F32 hogFileCalcFragmentation(HogFile *handle);
int hogShowError(HogFile *handle,int err_code,const char *err_msg,int err_int_value);
int hogShowErrorWithFile(HogFile *handle, HogFileIndex file_index, int err_code, const char *err_msg, int err_int_value);

// Determines if a hogg should be defragged, assuming it will be defragged with the "tight" option.
// If not a "tight" defrag, then regular hogg growth grows the file by 10%, so this would
//  aggressively say it needs a defrag when there is simply that 10% slack at the end.
bool hogFileShouldDefragEx(HogFile *handle, U64 threshold_bytes, U64 *wasted_bytes);
#define hogFileShouldDefrag(handle) hogFileShouldDefragEx(handle, 0, NULL)

U64 hogFileGetQueuedModSize(HogFile *handle);

U64 hogFileGetLargestFreeSpace(HogFile *handle);
U64 hogFileGetArchiveSize(HogFile *handle);

#define hogFileExtract(handle, file, count, checksum_valid) hogFileExtractEx(handle, file, count, checksum_valid, 0, 0, 0, 0)
SA_RET_OP_VALID void *hogFileExtractEx(SA_PARAM_NN_VALID HogFile *handle, HogFileIndex file, SA_PRE_NN_FREE SA_POST_NN_VALID U32 *count, SA_PRE_OP_FREE SA_POST_OP_VALID bool * checksum_valid, U64 offset, U32 total, U32 packed, int special_heap);
U32 hogFileExtractBytesEx(SA_PARAM_NN_VALID HogFile *handle, HogFileIndex file, SA_PRE_NN_BYTES_VAR(bufsize) SA_POST_NN_VALID void *buf, U32 pos, U32 bufsize, U64 file_offset, U32 unpacked_size, U32 pack_size, bool haveOffset);
#define hogFileExtractBytes(handle, file, buf, pos, bufsize) hogFileExtractBytesEx(handle, file, buf, pos, bufsize, 0, 0, 0, false)
U32 hogFileExtractRawBytes(SA_PARAM_NN_VALID HogFile *handle, HogFileIndex file, SA_PRE_NN_BYTES_VAR(size) SA_POST_NN_VALID void *buf, U32 pos, U32 size, bool haveOffset, U64 offset);

// Doesn't uncompress - for the patcher
#define hogFileExtractCompressed(handle, file, count) hogFileExtractCompressedEx(handle, file, count, 0, 0, 0, 0)
void *hogFileExtractCompressedEx(HogFile *handle, HogFileIndex file, U32 *count, U64 offset, U32 total, U32 pack_size, int special_heap);


typedef void (*DataFreeCallback)(void *data);

// HogFileModify functions for modifying a hog_file (in memory and on disk)
// All Update functions either add, update, or delete based on whether or not the
//   file is new, and whether or not data/entry->data is NULL.
// They will compress and checksum the data in the main thread, unless it is an Async variety
int hogFileModifyDelete_dbg(HogFile *handle, HogFileIndex file, const char *caller_fname, int linenum);
#define hogFileModifyDelete(handle, file) hogFileModifyDelete_dbg(handle, file, __FILE__, __LINE__)

int hogFileModifyUpdateTimestamp(HogFile *handle, HogFileIndex file, U32 timestamp);
int hogFileModifyUpdateNamedSync(HogFile *handle, const char *relpath, U8 *data, U32 size, U32 timestamp, DataFreeCallback free_callback);
int hogFileModifyUpdateNamedSync2(HogFile *handle, NewPigEntry *entry);
int hogFileModifyUpdateNamedAsync(HogFile *handle, const char *relpath, U8 *data, U32 size, U32 timestamp, DataFreeCallback free_callback);
int hogFileModifyUpdateNamedAsync2(HogFile *handle, NewPigEntry *entry);
int hogFileModifyFlush(HogFile *handle);
int hogFileModifyTruncate(HogFile *handle); // If the archive can be made smaller, truncate it - normal truncations happen only after deletes

void hogThreadingInit(void);

typedef enum HogOpenMode {
	// Hog files are written and flushed such that a graceful recovery is
	//  possible in the event of an OS crash
	HogSafeAgainstOSCrash,

	// Hog files send their data to the OS layer in a fashion such that graceful
	//  recovery is possible regardless of when the application crashes, but
	//  but because we are not enforcing the OS to flush to disk in sequential
	//  order, an OS or system failure may render the hog file corrupt.
	// On the Xbox, an app crash may behave the same as an OS crash, therefore
	//  this should not be used?  Or can we get away with using MS's transactional
	//  stuff?
	HogSafeAgainstAppCrash, 

	// No operation journaling done, in the event of an application crash during modification
	//  the hog file will likely be corrupt.
	// On a contrived test (sync of lots of very small files) this provided a 40% speedup
	//  over HogSafeAgainstAppCrash
	HogUnsafe,

	// No journaling of any kind done, in the event of an application exiting without
	//  completely closing the hogg handle (flush is *not* enough) the hog file will
	//  be guaranteed to be corrupt.
	// On a contrived test (sync of lots of very small files) this provided a 25% speedup
	//  over HogUnsafe
	HogSuperUnsafe,
} HogOpenMode;

// This applies to the next hog file opened, but does not affect already open
//  handles.  Perhaps this should be a parameter to hogFileRead()
void hogSetGlobalOpenMode(HogOpenMode mode);
void hogSetMaxBufferSize(U64 bufferSize); // If more than this much memory is used, the foreground thread will stall
//void hogSetMaxBufferCount(U32 bufferCount); // 0 for no max, which is the default, probably irrelevant now

// Instructs the async compression/crcing code to use an existing thread instead of creating a new one
// Must be called before any call to hogFileCreate() or hogThreadingInit()
void hogSetAsyncThread(ManagedThread *thread);

U32 hogTotalPendingOperationCount(void);
void hogGetGlobalStats(F32 *bytesReadSec, F32 *bytesWriteSec, F32 *bytesReadSecAvg, F32 *bytesWriteSecAvg);

void hogAddPathRedirect(const char *srcpath, const char *destpath); // e.g. game:/ -> devkit:/FightClub/

PigErrLevel hogGetErrLevel(const HogFile *handle);

void hogSetAllowUpgrade(bool allow_upgrade);

LATELINK;
U32 hogWarnImplementationLimitExceeded(const char *hog, U32 implementation_limit_warning, U32 file_count);

U32 GetDatatlistJournalSize(HogFile *hog_file);
