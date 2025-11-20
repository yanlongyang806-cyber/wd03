#include "hoglib.h"
#include "hogutil.h"

#if !_PS3
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include <limits.h>
#include "piglib.h"
#include "ScratchStack.h"
#include "EString.h"
#include "fileCache.h"
#include "datalist.h"
#include "network/crypt.h"
#include "logging.h"
#include "piglib_internal.h"
#include "StashTable.h"
#include "memlog.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "mutex.h"
#include "threadedFileCopy.h"
#include "endian.h"
#include "textparser.h"
#include "XboxThreads.h"
#include "ThreadManager.h"
#include "UnitSpec.h"
#include "StringCache.h"
#include "GlobalTypes.h"
#include "MemoryMonitor.h"
#include "zutils.h"
#include "sysutil.h"
#include "MemAlloc.h"
#include "ThreadSafeMemoryPool.h"
#include "fileutil2.h"
#include "ContinuousBuilderSupport.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:hogThreadingThread", BUDGET_FileSystem););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:hogAsyncThread", BUDGET_FileSystem););

MemLog hogmemlog={0};

#ifdef _FULLDEBUG
#define HOGG_WATCH_TIMESTAMPS 0 // Technically shouldn't be needed, but might let a separate computer watching a hogg over a network reload correctly... though will be as likely to crash in that case
#else
// Leaving this on in general for now, certainly doesn't make things *less* safe
#define HOGG_WATCH_TIMESTAMPS 1 // Technically shouldn't be needed, but might let a separate computer watching a hogg over a network reload correctly... though will be as likely to crash in that case
#endif
// create: pig chzyf ./pigtest.hogg ./source_files/*
// list: pig tv2p ./pigtest.hogg
// list: pig tv2p ./trouble.hogg
// extract: pig xvf ../pigtest.hogg -Cextract
// update: pig uvhzyf ./pigtest.hogg ./test_files/* --hogdebug -Rtest1.bat
// patchtest: pig cv a --patchtest

static int debug_crash_counter=0;
#define DEBUG_CHECK() saveDebugHog(handle->file, handle->filename, ++debug_crash_counter)
#define NESTED_ERROR(err_code, old_error) ((old_error << 4) | err_code)
#define ENTER_CRITICAL(which) { PERFINFO_AUTO_START(__FUNCTION__ " (outside CS)",1); EnterCriticalSection(&handle->which); handle->debug_in_##which++; PERFINFO_AUTO_START(__FUNCTION__ " (inside CS)",1); } // debug_in_data_access and debug_in_file_access
#define LEAVE_CRITICAL(which) { assertmsg(handle->debug_in_##which > 0, "Leaving a CRITICAL_SECTION we're not in"); handle->debug_in_##which--; PERFINFO_AUTO_STOP(); LeaveCriticalSection(&handle->which); PERFINFO_AUTO_STOP(); }
#define IN_CRITICAL(which) { assert(handle->debug_in_##which); }
int hog_debug_check=0;
int hog_verbose=0;
int hog_mode_no_data=0; // Does not actually move data around, just pretends and updates headers
int delay_hog_loading=0;
int hog_verify_crcs=0;

U64 hog_write_bytes_estimate;
U64 hog_read_bytes_estimate;
U64 hog_last_write_bytes_estimate;
U64 hog_last_read_bytes_estimate;
F32 hog_last_write_sec;
F32 hog_last_read_sec;
F32 hog_last_total_time;
int hog_timing_timer;
int hog_timing_timer_total;

#if _PS3 && _DEBUG
HogOpenMode g_hogOpenMode=HogSafeAgainstOSCrash;
#else
HogOpenMode g_hogOpenMode=HogSafeAgainstAppCrash;
#endif


// Artificially slows down hog operations, in ms
AUTO_CMD_INT(delay_hog_loading, delay_hog_loading) ACMD_CMDLINE ACMD_CATEGORY(DEBUG);

// Verify all CRCs passed in to hog update functions
AUTO_CMD_INT(hog_verify_crcs, hog_verify_crcs) ACMD_CMDLINE ACMD_CATEGORY(DEBUG);

// Display extra hogg diagnostic information
AUTO_CMD_INT(hog_verbose, hog_verbose) ACMD_CMDLINE ACMD_CATEGORY(DEBUG);

#define HOG_OP_JOURNAL_SIZE 1024
#define HOG_MAX_DL_JOURNAL_SIZE (128*1024*1024)
#define HOG_OLD_MAX_DL_JOURNAL_SIZE (65536 - HOG_OP_JOURNAL_SIZE)
#define HOG_DEFAULT_DL_JOURNAL_SIZE (65536 - HOG_OP_JOURNAL_SIZE)
#define HOG_MIN_DL_JOURNAL_SIZE 1024
#define HOG_MAX_SLACK_SIZE 2048
#define FILELIST_PAD 0.20 // In percent of filelist size
#define FILELIST_PAD_THRESHOLD 0.05 // If we're less than this when we expand the EAList, expand the FileList as well
#define EALIST_PAD_THRESHOLD 0.15 // If we're less than this when we expand the FileList, expand the EAList as well
#define HOG_NO_VALUE ((U32)-1)
#define HOG_JOURNAL_TERMINATOR 0xdeabac05 // arbitrary
#define HOG_POINTER_SENTINEL 0xcab1fade // arbitrary

typedef enum HogEAFlags {
	HOGEA_NOT_IN_USE = 1 << 0,
} HogEAFlags;

typedef enum HogOpJournalAction {
	HOJ_INVALID,
	HOJ_DELETE,
	HOJ_ADD_OR_UPDATE,
	HOJ_UPDATE_DEPRECATED,
	HOJ_MOVE,
	HOJ_FILELIST_RESIZE,
	HOJ_DATALISTFLUSH,
} HogOpJournalAction;
STATIC_ASSERT(sizeof(HogOpJournalAction)==sizeof(U32)); // endianSwapping down below relies on this

// Begin Format on disk:

// Header to archive file
typedef struct HogHeader {
	U32 hog_header_flag;
	U16 version;
	U16 op_journal_size;
	U32 file_list_size;		// TODO: Make this a U64
	U32 ea_list_size;		// TODO: Make this a U64
	U32 datalist_fileno;	// Index of the special file "?DataList"
	U32 dl_journal_size;
} HogHeader;
ParseTable parseHogHeader[] = {
	{"hog_header_flag",		TOK_INT(HogHeader, hog_header_flag, 0)},
	{"version",				TOK_INT16(HogHeader, version, 0)},
	{"op_journal_size",		TOK_INT16(HogHeader, op_journal_size, 0)},
	{"file_list_size",		TOK_INT(HogHeader, file_list_size, 0)},
	{"ea_list_size",		TOK_INT(HogHeader, ea_list_size, 0)},
	{"datalist_fileno",		TOK_INT(HogHeader, datalist_fileno, 0)},
	{"dl_journal_size",		TOK_INT(HogHeader, dl_journal_size, 0)},
	{0, 0}
};

// Then comes the Operation Journal
//  (structures below, HogOpJournalHeader, et al)

// Then comes the DataList Journal
typedef struct DLJournalHeader {
	U32 inuse_flag; // Flag this,
	U32 size;		// Then write new size, then unflag,
	U32 oldsize;	// Then overwrite oldsize
} DLJournalHeader;
ParseTable parseDLJournalHeader[] = {
	{"inuse_flag",	TOK_INT(DLJournalHeader, inuse_flag, 0)},
	{"size",		TOK_INT(DLJournalHeader, size, 0)},
	{"oldsize",		TOK_INT(DLJournalHeader, oldsize, 0)},
	{0, 0}
};

// Basic file information.  For lightmaps which don't have filenames, etc, this needs
// to be kept to a bare minimum
typedef union _HogFileHeaderData {
	U64 raw;
	struct {
#if PLATFORM_CONSOLE // Big endian
		S32 ea_id;
		U16 reserved;
		U16 flagFFFE; // If this is FFFE, the ea_index is valid
#else
		U16 flagFFFE; // If this is FFFE, the ea_index is valid
		U16 reserved;
		S32 ea_id;
#endif
	};
} HogFileHeaderData;
typedef struct HogFileHeader {
	U64 offset;
	U32 size;
	__time32_t timestamp;
	U32	checksum; // first 32 bits of a checksum (MD5)
	HogFileHeaderData headerdata;
} HogFileHeader;
STATIC_ASSERT(sizeof(((HogFileHeader*)0)->headerdata) == sizeof(U64)); // Make sure the union isn't confused
// Inlined for performance instead of using a parse table
#define endianSwapHogFileHeaderIfBig(hfh)					\
	if (isBigEndian()) {									\
		(hfh)->offset = endianSwapU64((hfh)->offset);			\
		(hfh)->size = endianSwapU32((hfh)->size);				\
		(hfh)->timestamp = endianSwapU32((hfh)->timestamp);		\
		(hfh)->checksum = endianSwapU32((hfh)->checksum);		\
		*(U64*)&(hfh)->headerdata = endianSwapU64(*(U64*)&(hfh)->headerdata);	\
	}

// Extended Attributes (optional) contains more information about a file
typedef struct HogEAHeader {
	U32 name_id; // ID of name (names stored in the DataList file)
	U32 header_data_id; // ID of header data (if any)
	U32 unpacked_size; // Size of file after unzipping if compressed (0 if not compressed)
	U32 flags; // Reserved for special flags (HogEAFlags)
} HogEAHeader;
#define endianSwapHogEAHeaderIfBig(hfh)								\
	if (isBigEndian()) {											\
		(hfh)->name_id = endianSwapU32((hfh)->name_id);					\
		(hfh)->header_data_id = endianSwapU32((hfh)->header_data_id);	\
		(hfh)->unpacked_size = endianSwapU32((hfh)->unpacked_size);		\
		(hfh)->flags = endianSwapU32((hfh)->flags);						\
	}

// datalist is a DataList file on disk

// End Format on disk

// Begin structures for modifications
typedef enum HogFileModType {
	HFM_NONE,
	HFM_DELETE,
	HFM_ADD,
	HFM_UPDATE,
	HFM_MOVE,
	HFM_DATALISTDIFF,
	HFM_TRUNCATE,
	HFM_FILELIST_RESIZE,
	HFM_DATALISTFLUSH,
	HFM_RELEASE_MUTEX,
	HFM_GROW,
} HogFileModType;

// These need to contain all of the information needed to do the command without
// access to the old elements of the hog_file structure (serialized copy of
// DataList needed?)
typedef struct HFMDelete {
	HogFileIndex file_index;
	S32 ea_id;
} HFMDelete;
ParseTable parseHFMDelete[] = {
	{"file_index",	TOK_INT(HFMDelete, file_index, 0)},
	{"ea_id",		TOK_INT(HFMDelete, ea_id, 0)},
	{0, 0}
};

typedef struct HFMAddOrUpdate {
	HogFileIndex file_index;
	U32 size;
	U32 timestamp;
	HogFileHeaderData headerdata; // ea_id or other header data
	U32 checksum;
	U64 offset;
	struct { // Only if ea_id present
		U32 unpacksize;
		S32 name_id;
		S32 header_data_id;
	} ea_data;

	U8 *data; // Pointer *must* be last for 64-bit compatibility
} HFMAddOrUpdate;
STATIC_ASSERT(sizeof(((HFMAddOrUpdate*)0)->headerdata) == sizeof(U64));
ParseTable parseHFMAddOrUpdate[] = {
	{"file_index",		TOK_INT(HFMAddOrUpdate, file_index, 0)},
	{"size",			TOK_INT(HFMAddOrUpdate, size, 0)},
	{"timestamp",		TOK_INT(HFMAddOrUpdate, timestamp, 0)},
	{"headerdata",		TOK_INT64(HFMAddOrUpdate, headerdata, 0)},
	{"checksum",		TOK_INT(HFMAddOrUpdate, checksum, 0)},
	{"offset",			TOK_INT64(HFMAddOrUpdate, offset, 0)},
	{"unpacksize",		TOK_INT(HFMAddOrUpdate, ea_data.unpacksize, 0)},
	{"name_id",			TOK_INT(HFMAddOrUpdate, ea_data.name_id, 0)},
	{"header_data_id",	TOK_INT(HFMAddOrUpdate, ea_data.header_data_id, 0)},
	{0, 0}
};

typedef struct HFMMove {
	HogFileIndex file_index;
	U32 size;
	U64 old_offset;
	U64 new_offset;
} HFMMove;
ParseTable parseHFMMove[] = {
	{"file_index",	TOK_INT(HFMMove, file_index, 0)},
	{"size",		TOK_INT(HFMMove, size, 0)},
	{"old_offset",	TOK_INT64(HFMMove, old_offset, 0)},
	{"new_offset",	TOK_INT64(HFMMove, new_offset, 0)},
	{0, 0}
};

typedef struct HFMDataListDiff {
	U64 size_offset; // Where to write the size
	U32 size; // Size of new data
	U64 offset; // where to write the data
	U32 newsize; // New size to write

	U8 *data; // Pointer at end!
} HFMDataListDiff;
ParseTable parseHFMDataListDiff[] = {
	{"size_offset",		TOK_INT64(HFMDataListDiff, size_offset, 0)},
	{"size",			TOK_INT(HFMDataListDiff, size, 0)},
	{"offset",			TOK_INT64(HFMDataListDiff, offset, 0)},
	{"newsize",			TOK_INT(HFMDataListDiff, newsize, 0)},
	{0, 0}
};

typedef struct HFMDataListFlush {
	U64 size_offset; // Where to write the size
} HFMDataListFlush;
ParseTable parseHFMDataListFlush[] = {
	{"size_offset",		TOK_INT64(HFMDataListFlush, size_offset, 0)},
	{0, 0}
};

typedef struct HFMTruncate {
	U64 newsize;
} HFMTruncate;
ParseTable parseHFMTruncate[] = {
	{"newsize",			TOK_INT64(HFMTruncate, newsize, 0)},
	{0, 0}
};

typedef struct HFMGrow {
	U64 newsize;
} HFMGrow;
ParseTable parseHFMGrow[] = {
	{"newsize",			TOK_INT64(HFMGrow, newsize, 0)},
	{0, 0}
};

typedef struct HFMReleaseMutex {
	bool needsFlushAndSignalAndAsyncOpDecrement;
} HFMReleaseMutex;

typedef struct HFMFileListResize {
	U32 new_filelist_size;
	U32 old_ealist_pos; // Not needed?
	U32 old_ealist_size; // Size of old data (used if EAList didn't move)
	U32 new_ealist_pos; // Should be implicit from new_filelist_size and filelist_offset
	U32 new_ealist_size;
	void *new_ealist_data; // Size is new_ealist_size
} HFMFileListResize;
ParseTable parseHFMFileListResize[] = {
	{"new_filelist_size",	TOK_INT(HFMFileListResize, new_filelist_size, 0)},
	{"old_ealist_pos",		TOK_INT(HFMFileListResize, old_ealist_pos, 0)},
	{"old_ealist_size",		TOK_INT(HFMFileListResize, old_ealist_size, 0)},
	{"new_ealist_pos",		TOK_INT(HFMFileListResize, new_ealist_pos, 0)},
	{"new_ealist_size",		TOK_INT(HFMFileListResize, new_ealist_size, 0)},
	{0, 0}
};

typedef struct HogFileMod HogFileMod;
struct HogFileMod {
	HogFileMod *next;
	HogFileModType type;
	U32 byte_size;
	U64 filelist_offset;
	U64 ealist_offset;
	DataFreeCallback free_callback;
	union {
		HFMDelete del;
		HFMAddOrUpdate addOrUpdate;
		HFMMove move;
		HFMDataListDiff datalistdiff;
		HFMTruncate truncate;
		HFMFileListResize filelist_resize;
		HFMDataListFlush datalistflush;
		HFMReleaseMutex release_mutex;
		HFMGrow grow;
	};
};
// End structures for modifications

// Begin Op Journal structures (uses above Mod structures for simplicity)
typedef struct HogOpJournalHeader {
	U32 size;
	// Then begins one of the following structures (with Type as the first element)
	HogOpJournalAction type;
} HogOpJournalHeader;
ParseTable parseHogOpJournalHeader[] = {
	{"size", TOK_INT(HogOpJournalHeader, size, 0)},
	{"type", TOK_INT(HogOpJournalHeader, type, 0)},
	{0, 0}
};
typedef struct HFJDelete {
	HogOpJournalAction type;
	HFMDelete del;
} HFJDelete;
typedef struct HFJAddOrUpdate {
	HogOpJournalAction type;
	HFMAddOrUpdate addOrUpdate; // data entry is ignored
} HFJAddOrUpdate;
typedef struct HFJMove {
	HogOpJournalAction type;
	HFMMove move; // old_offset and size entries are ignored
} HFJMove;
typedef struct HFJFileListResize {
	HogOpJournalAction type;
	HFMFileListResize filelist_resize; // old_offset and size entries are ignored
} HFJFileListResize;
typedef struct HFJDataListFlush {
	HogOpJournalAction type;
	HFMDataListFlush datalistflush; // old_offset and size entries are ignored
} HFJDataListFlush;

// End Op Journal structures

// Begin In memory format
typedef struct HogFileListEntry {
	HogFileHeader header;
	U8 *data; // Data to be written
	short dirty; // Has an operation queued to modify this file, on disk data is not valid, header might point to wrong offset, etc
	short dirty_async; // Has an async operation queued to start a modification, header will have wrong size/crc/etc
	U8 in_use:1;
	U8 queued_for_delete:1; // Has a delete operation queued on it, all further operations on this handle are illegal
} HogFileListEntry;

typedef struct HogEAListEntry {
	HogEAHeader header;
//	bool in_use;
} HogEAListEntry;

#define EA_IN_USE(ealistentry) (!((ealistentry)->header.flags & HOGEA_NOT_IN_USE))
#define EA_IN_USE_STRUCT(ealistentry) (!((ealistentry).header.flags & HOGEA_NOT_IN_USE))

typedef struct HogFile {
	U32 hog_sentinel;
	HogHeader header;
	HogFileListEntry *filelist;
	PigErrLevel err_level;
	U32 num_files;
	size_t filelist_min; // minimum size of filelist, increase the real one to match
	size_t filelist_count; // Count of FileList entries (matches what's on disk)
	size_t filelist_max; // max (for use by dynArray)
	HogEAListEntry *ealist;
	size_t ealist_min; // minimum size of filelist, increase the real one to match
	size_t ealist_count; // Count of EAList entries (matches what's on disk)
	size_t ealist_max; // max (for use by dynArray)
	DataList datalist; // Contains names and HeaderData chunks
	char *filename;
	int resource_id; // Windows resource ID of contents, if this is a resource-backed hog
#if HOGG_WATCH_TIMESTAMPS
	char *filename_for_timestamp;
#endif
	struct {
		char *name;
		volatile int reference_count;
		ThreadAgnosticMutex handle;
	} mutex;
	U64 file_size;
	FILE *file;
	FILE **multipleFiles; // FILE *s for multiple reads mode
	const char *open_mode_string;
	HogFileCreateFlags create_flags;
	bool read_only;
	bool file_needs_flush;
	HogFileMod *mod_ops_head; // First one queued
	HogFileMod *mod_ops_tail; // Last one queued
	U32 mod_list_size; // current size of mod list
	U64 mod_list_byte_size; // Approximate amount of memory consumed by entires in the mod_list
	U32 datalist_diff_size; // Current size of DataList Journal
	U32 last_journal_size;
	CRITICAL_SECTION file_access; // For multi-threaded read/write
	int debug_in_file_access;
	CRITICAL_SECTION data_access; // For multi-threaded query of in-memory data
	int debug_in_data_access;
	int auto_release_data_access; // Set this before calling specific functions to get them to release and regrab data_access for you to avoid deadlocks
	CRITICAL_SECTION doing_flush; // For multiple threads doing a flush at the same time
	int debug_in_doing_flush;
	bool crit_sect_inited;
	HANDLE done_doing_operation_event;
	HANDLE starting_flush_event;
	int last_threaded_error;
	volatile U32 async_operation_count;
#if _XBOX
	int num_skipped_flushes; // How many times we skipped calling FlushUtilityDrive for performance
#endif
	StashTable fn_to_index_lookup;
	StashTable async_new_files; // List of new files which are being created asynchronously
	int *file_free_list; // EArray of free FileList indices
	int *ea_free_list; // EArray of free EAList indicies
	HogFreeSpaceTracker2 *free_space2;
	bool doing_delete_during_load_or_repair; // does not update free space, does not assert on hashtable entry missing
	bool has_been_reloaded;
	bool policy_flush;
	bool policy_journal;
	bool policy_journal_datalist;
	bool header_was_invalidated; // We wrote over the hog header in the file
	bool single_app_mode;
	bool need_upgrade; // Set if we think this file should be upgraded to a new hogg version
	bool guaranteed_no_ops; // Set to remove some checking when we the caller can guarantee there could be no operations queued
	bool assert_if_semaphore_changed;
#if HOGG_WATCH_TIMESTAMPS
	bool file_cache_is_monitoring; // We've querying the fileCache at least once
	__time32_t file_last_changed;
#endif
	volatile int shared_reference_count;
	U32 implementation_limit_warning; // Set if we've warned about hitting an implementation limit check
	struct {
		char *name;
		LONG value;
		LONG value_on_last_get;
		LONG soloValue; // Value created just by me (if != value, someone else is modifying this hogg too)
		HANDLE semaphore;
	} version;
	struct {
		HogCallbackUpdated fileUpdated;
		void *userData;
	} callbacks;
} HogFile;
// End In memory format

TSMP_DEFINE(HogFileMod);

HogFileMod *callocHogFileMod(void)
{
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(HogFileMod, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	return TSMP_CALLOC(HogFileMod);
}

void safefreeHogFileMod(HogFileMod **h)
{
	if(!h || !*h)
		return;

	TSMP_FREE(HogFileMod, *h);
	*h = NULL;
}

volatile U32 total_async_operation_count;
volatile U32 total_async_mutex_release_count;

U32 hogTotalPendingOperationCount(void)
{
	return total_async_operation_count - total_async_mutex_release_count;
}

void hogGetGlobalStats(F32 *bytesReadSec, F32 *bytesWriteSec, F32 *bytesReadSecAvg, F32 *bytesWriteSecAvg)
{
	if (!hog_timing_timer)
	{
		hog_timing_timer = timerAlloc();
		hog_timing_timer_total = timerAlloc();
		hog_last_read_bytes_estimate = hog_read_bytes_estimate = 0;
		hog_last_write_bytes_estimate = hog_write_bytes_estimate = 0;
		hog_last_total_time = 1;
	} else {
		F32 elapsed = timerElapsed(hog_timing_timer);
		if (elapsed >= 1)
		{
			timerStart(hog_timing_timer);
			hog_last_total_time = timerElapsed(hog_timing_timer_total);
			hog_last_read_sec = (hog_read_bytes_estimate - hog_last_read_bytes_estimate) / elapsed;
			hog_last_read_bytes_estimate = hog_read_bytes_estimate;
			hog_last_write_sec = (hog_write_bytes_estimate - hog_last_write_bytes_estimate) / elapsed;
			hog_last_write_bytes_estimate = hog_write_bytes_estimate;
		}
	}
	if (bytesReadSec)
		*bytesReadSec = hog_last_read_sec;
	if (bytesWriteSec)
		*bytesWriteSec = hog_last_write_sec;
	if (bytesReadSecAvg)
		*bytesReadSecAvg = hog_last_read_bytes_estimate / hog_last_total_time;
	if (bytesWriteSecAvg)
		*bytesWriteSecAvg = hog_last_write_bytes_estimate / hog_last_total_time;;
}


static int hogFileModifyDoDeleteInternal(HogFile *handle, HogFileMod *mod);
static int hogFileModifyDoAddOrUpdateInternal(HogFile *handle, HogFileMod *mod);
static int hogFileModifyDoUpdateInternal(HogFile *handle, HogFileMod *mod);
static int hogFileModifyDoMoveInternal(HogFile *handle, HogFileMod *mod);
static int hogFileModifyDoFileListResizeInternal(HogFile *handle, HogFileMod *mod);
static int hogFileModifyDoDataListFlushInternal(HogFile *handle, HogFileMod *mod);
static int hogFileAddDataListMod(HogFile *handle, DataListJournal *dlj);
static void hogThreadHasWork(HogFile *handle);
static void hogFileCreate(HogFile *handle, PigErrLevel err_level, HogOpenMode mode, HogFileCreateFlags flags, U32 datalist_journal_size);
static char *makeMutexName(const char *fname, const char *prefix);
static int hogFileReadInternal(HogFile *handle, const char *filename); // Load a .HOGG file
static void hogReleaseMutexAsync(HogFile *handle, bool needsFlushAndSignalAndAsyncOpDecrement);
static HogFileListEntry *hogFileWaitForFile(HogFile *handle, HogFileIndex file, bool bNeedFileAccess, bool bNeedMutex);
static HogFileListEntry *hogFileWaitForFileData(HogFile *handle, HogFileIndex file, bool should_assert, bool flag_for_delete);
static int hogFileFlushDataListDiff(HogFile *handle);
static void hogAcquireMutex(HogFile *handle);
static void hogReleaseMutex(HogFile *handle, bool needsFlush, bool needsSignalAndAsyncOpDecrement);
static void hogFileModifyGrowToAtLeast(HogFile *handle, U64 newFileSize);
static int hogFileModifyUpdate(HogFile *handle, HogFileIndex file, NewPigEntry *entry);
static int hogJournalReset(HogFile *handle);


int stash_add_count,stash_save_count;

int logHogTransactions = 0;

AUTO_CMD_INT(logHogTransactions, logHogTransactions) ACMD_COMMANDLINE;

__inline void hoggMemLog(char const *frmt, ...)
{
	va_list _va_list;

	va_start(_va_list, frmt);
	memlog_vprintf(&hogmemlog, frmt, _va_list);
	va_end(_va_list);

	if (logHogTransactions)
	{
		char buffer[255];
		va_start(_va_list, frmt);
		vsprintf(buffer, frmt, _va_list);
		va_end(_va_list);
		log_printf(LOG_HOGG,"PID %d : %s",_getpid(), buffer);
	}
}

static bool storeNameToId(const HogFile *handle,const char *stored_name,int file_index)
{
	int		*id=0;
	bool bRet=true;

	assert(!(handle->create_flags & HOG_NO_ACCESS_BY_NAME));

	if (file_index == -1)
		bRet = stashRemoveInt(handle->fn_to_index_lookup, stored_name, NULL);
	else
		bRet = stashAddInt(handle->fn_to_index_lookup, stored_name, file_index, false); // Shallow copied name from DataList
	stash_add_count++;
	return bRet;
}

int idfromname_count;

static int idFromName(const HogFile *handle,const char *relpath,int *idp)
{
	assert(!(handle->create_flags & HOG_NO_ACCESS_BY_NAME)); // NOTE: Did you rename the dynpatch hogg to something other than dynamic.hogg? Look at PigSetInit in piglib.c <NPK 2010-04-05>
	idfromname_count++;
	if (stashFindInt(handle->fn_to_index_lookup, relpath, idp))
		return true;
	return false;
}

// code is important for bucketing (e.g. return code), err_int_value is important for debugging (e.g. file index)
static int hogShowErrorEx(PigErrLevel err_level, const char *hogfname, const char *internalfname, int err_code,const char *err_msg,int err_int_value)
{
	char	buf[1000];
	char	details_buf[1000];

	// buf gets "err_msg: [err_code]"
	// details gets "err_msg: [hogfname:] [internalfname: ] [err_code] [err_int_value]
	strcpy(buf, err_msg);
	strcat(buf, ": ");
	strcpy(details_buf, buf);

	if (hogfname)
	{
		strcat(details_buf, hogfname);
		strcat(details_buf, ": ");
	}
	if (internalfname)
	{
		strcat(details_buf, internalfname);
		strcat(details_buf, ": ");
	}
	if (err_code > 1)
	{
		strcatf(buf, "%d ", err_code);
		strcatf(details_buf, "%d ", err_code);
	}
	if (err_int_value != 0 && err_int_value != -1)
		strcatf(details_buf, "%d ", err_int_value);
	
	filelog_printf("hog_errors.log", "%s", details_buf);

	if (!errorIsDuringDataLoadingGet()) // Don't re-set this if a caller set a better/more specific filename already
		errorIsDuringDataLoadingInc(hogfname); // There was some error, set this so any crash after this causes a re-verify
	if (err_level != PIGERR_QUIET)
		printf("%s\n", details_buf);
	if (err_level == PIGERR_ASSERT)
	{
		ErrorDetailsf("%s", details_buf);
		if (g_isContinuousBuilder)
		{
			FatalErrorf("%s(%s): %s", hogfname,internalfname, buf);
		}
		else
		{
			FatalErrorf("%s", buf);
		}
	}
	return err_code;
}

int hogShowError(HogFile *handle,int err_code,const char *err_msg,int err_int_value)
{
	return hogShowErrorEx(handle->err_level, handle->filename, NULL, err_code, err_msg, err_int_value);
}

int hogShowErrorWithFile(HogFile *handle, HogFileIndex file_index, int err_code, const char *err_msg, int err_int_value)
{
	const char *fname = hogFileGetFileName(handle, file_index);
	return hogShowErrorEx(handle->err_level, handle->filename, fname, err_code, err_msg, err_int_value);
}



void hoglogEcho(const char* s, ...)
{
	va_list ap;
	char buf[MEMLOG_LINE_WIDTH+10];

	va_start(ap, s);
	if (vsprintf(buf,s,ap) < 10) return;
	va_end(ap);

	printf("%s\n", buf);
}

void hogSetGlobalOpenMode(HogOpenMode mode)
{
	g_hogOpenMode = mode;
}

// Sets hoggs to unsafe mode for testing purposes
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void hogUnsafeMode(int unsafe)
{
	if (unsafe==2)
		g_hogOpenMode = HogSuperUnsafe;
	else if (unsafe)
		g_hogOpenMode = HogUnsafe;
	else
		g_hogOpenMode = HogSafeAgainstAppCrash;
}

static HogFile *hogFileAlloc(PigErrLevel err_level, HogFileCreateFlags flags)
{
	HogFile *ret;

	PERFINFO_AUTO_START_FUNC();

	ret = calloc(1, sizeof(HogFile));
	//memlog_setCallback(&hogmemlog, hoglogEcho);
	memlog_enableThreadId(&hogmemlog);
	hogFileCreate(ret, err_level, g_hogOpenMode, flags, HOG_DEFAULT_DL_JOURNAL_SIZE);

	PERFINFO_AUTO_STOP();

	return ret;
}

static bool isValidHogHandle(HogFile *handle)
{
	return handle && handle->hog_sentinel == HOG_POINTER_SENTINEL;
}

static void hogFileCreate(HogFile *handle, PigErrLevel err_level, HogOpenMode mode, HogFileCreateFlags flags, U32 datalist_journal_size)
{
	assert(!(flags & HOG_MULTIPLE_READS) || (flags & HOG_READONLY)); // multiple reads only supported with read-only for now
	if (flags & HOG_NO_JOURNAL)
		mode = HogSuperUnsafe;

	hogThreadingInit();
	hoggMemLog( "0x%08p: Create", handle);
	assert(!isValidHogHandle(handle));
	assert(!handle->crit_sect_inited);
	ZeroStruct(handle);
	handle->err_level = err_level;
	handle->header.hog_header_flag = HOG_HEADER_FLAG;
	handle->header.dl_journal_size = CLAMP(datalist_journal_size, HOG_MIN_DL_JOURNAL_SIZE, HOG_MAX_DL_JOURNAL_SIZE);
	if (handle->header.dl_journal_size <= HOG_OLD_MAX_DL_JOURNAL_SIZE)
	{
		handle->header.version = HOG_OLD_VERSION;
	} else {
		handle->header.version = HOG_VERSION;
	}
	handle->header.op_journal_size = HOG_OP_JOURNAL_SIZE;
	DataListCreate(&handle->datalist);
	handle->header.datalist_fileno = HOG_NO_VALUE;
	InitializeCriticalSection(&handle->file_access);
	InitializeCriticalSection(&handle->data_access);
	InitializeCriticalSection(&handle->doing_flush);
	handle->crit_sect_inited = true;
	handle->done_doing_operation_event = CreateEvent(NULL, FALSE, FALSE, NULL); // Auto-reset, not initially signaled
	assert(handle->done_doing_operation_event != NULL);
	handle->starting_flush_event = CreateEvent(NULL, FALSE, FALSE, NULL); // Auto-reset, not initially signaled
	assert(handle->starting_flush_event);
	handle->policy_flush = (mode == HogSafeAgainstOSCrash); // Because we're opening with 'c', this might not even be needed, but does not change performance
#if !_PS3 // JE: This is almost certainly wrong for PS3, no journalling = bad
	handle->policy_journal = (mode != HogUnsafe && mode != HogSuperUnsafe);
	handle->policy_journal_datalist = (mode != HogSuperUnsafe);
#endif
	handle->create_flags = flags;
	handle->hog_sentinel = HOG_POINTER_SENTINEL;
	assert(isValidHogHandle(handle));
}


static struct {
	CRITICAL_SECTION critical_section;
	StashTable stHandles;
} g_hogSharedHandles;
AUTO_RUN_LATE; // No one should do file access in auto_runs anyway
void initHogFileShared(void)
{
	InitializeCriticalSection(&g_hogSharedHandles.critical_section);
	g_hogSharedHandles.stHandles = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
}

static int hogFileDecrementSharedReference(HogFile *handle)
{
	int ret=0;
	if (handle->shared_reference_count)
	{
		// Must not do this if reference_count is 0, because that happens during reloading,
		//  and reloading must not lock the global shared handles critical section
		// Deadlock check might fire here between this and a specific hogg's data_access
		//  but this is safe because we only lock it in the other order while creating
		//  a hogg, which is guaranteed single-threaded.
		EnterCriticalSection(&g_hogSharedHandles.critical_section);
		handle->shared_reference_count--;
		ret = handle->shared_reference_count; // 0 means it's OK to free it
		if (0 == ret) {
			// Remove from stashtable
			bool bRemoved;
			bRemoved = stashRemovePointer(g_hogSharedHandles.stHandles, handle->mutex.name, NULL);
			assert(bRemoved);
		}
		LeaveCriticalSection(&g_hogSharedHandles.critical_section);
	}
	return ret;
}

static void hogFilenameForMutexName(const char *filename, char *mutex_name, size_t mutex_name_size)
{
	strcpy_s(SAFESTR2(mutex_name), filename);
	if (!fileIsAbsolutePath(mutex_name))
		fileLocateWrite_s(mutex_name, SAFESTR2(mutex_name));
}

static HogFile *hogFileGetSharedHandleOrLock(const char *filename) {
	char *hashName;
	StashElement element;
	HogFile *handle;
	char fullname[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	hogFilenameForMutexName(filename, SAFESTR(fullname));
	// Must be after fileLocateWrite, because that might trigger a hog reload of some other hogg
	EnterCriticalSection(&g_hogSharedHandles.critical_section);
	hashName = makeMutexName(fullname, "");
	if (stashFindElement(g_hogSharedHandles.stHandles, hashName, &element)) {
		handle = stashElementGetPointer(element);
		handle->shared_reference_count++;
		LeaveCriticalSection(&g_hogSharedHandles.critical_section);
	} else {
		// Not in there, leave the CS locked
		handle = NULL;
	}
	free(hashName);

	PERFINFO_AUTO_STOP();

	return handle;
}

static void hogFileAddSharedHandleAndUnlock(HogFile *handle, const char *filename) {
	assert(!handle == !filename);
	if (filename) {
		char *hashName;
		bool b;
		char fullname[MAX_PATH];
		// Add it
		handle->shared_reference_count++;
		hogFilenameForMutexName(filename, SAFESTR(fullname));
		hashName = makeMutexName(fullname, "");
		b = stashAddPointer(g_hogSharedHandles.stHandles, hashName, handle, false);
		assert(b);
		free(hashName);
	} else {
		// Just unlock
	}
	LeaveCriticalSection(&g_hogSharedHandles.critical_section);
}

bool hogFileIsOpenInMyProcess(SA_PARAM_NN_STR const char *filename)
{
	bool ret=false;
	char *hashName;
	StashElement element;
	char fullname[MAX_PATH];
	EnterCriticalSection(&g_hogSharedHandles.critical_section);
	hogFilenameForMutexName(filename, SAFESTR(fullname));
	hashName = makeMutexName(fullname, "");
	if (stashFindElement(g_hogSharedHandles.stHandles, hashName, &element)) {
		ret = true;
	}
	LeaveCriticalSection(&g_hogSharedHandles.critical_section);
	free(hashName);
	return ret;
}

// Return true if an entry with this name exists within this hog.
bool hogFileExistsNoChangeCheck(HogFile *handle, SA_PARAM_NN_STR const char *filename)
{
	bool exists;
	HogFileIndex file_index;
	int count;

	assert(isValidHogHandle(handle));

	ENTER_CRITICAL(data_access);
	exists = idFromName(handle, filename, &file_index)											// Regular files
		|| handle->async_new_files && stashFindInt(handle->async_new_files, filename, &count);	// Pending asynch files
	LEAVE_CRITICAL(data_access);

	return exists;
}

HogFileCreateFlags hogGetCreateFlags(HogFile *handle)
{
	return handle->create_flags;
}


static void assertNotSharedHandleName(HogFile *handle, const char *filename)
{
	StashElement element;
	char *hashName;
	bool b;
	hashName = makeMutexName(filename, "");
	b = stashFindElement(g_hogSharedHandles.stHandles, hashName, &element);
	if (b) {
		assert(handle == stashElementGetPointer(element));
	}
	free(hashName);
}

bool hogFileAddFile(HogFile *hogFile, const char *file_name, const char *file_hogg_name)
{
	int file_size;
	U8* file_data = fileAlloc(file_name, &file_size);

	assert(file_data);

	hogFileModifyUpdateNamed(hogFile, file_hogg_name, file_data, file_size, 0, NULL);	// do not free file_data since this function frees it for us when done processing.
	return true;
}

// if clearDirectory is set to true, then this would be more akin to a move.
void hogFileCopyFolder(HogFile *hogFile, const char *dirName, bool clearDirectory)
{
	char **files = fileScanDir(dirName);
	int i;

	for (i = 0; i < eaSize(&files); i++) {
		int j;
		char *file_full_name = files[i];

		loadstart_printf("Adding %s\n", file_full_name);

		for (j = ((int)strlen(file_full_name)) - 1; j >= 0; j--) {	// Casting to an int should be fine here for dealing with a file name
			if (file_full_name[j] == '/' || file_full_name[j] == '\\')
				break;
		}
		j ++;
		hogFileAddFile(hogFile,file_full_name,&file_full_name[j]);	// The file should still have the same name as the image, just cropping the directory portion.

		loadend_printf(" done.\n");
	}
	fileScanDirFreeNames(files);

	if (clearDirectory)
		rmdir(dirName);
	return;
}

void hogFileDestroy(HogFile *handle, bool bFreeHandle)
{
	if (!handle)
		return;
	PERFINFO_AUTO_START_FUNC();
	if (hogFileDecrementSharedReference(handle)>0)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	if (handle->crit_sect_inited) {
		assert(!(handle->create_flags & HOG_SKIP_MUTEX));
		// Flush changes
		if (!handle->policy_journal_datalist && handle->datalist_diff_size && !handle->read_only)
		{
			// Changes are only in memory, flush it to disk
			hogAcquireMutex(handle);
			ENTER_CRITICAL(data_access);
			hogFileFlushDataListDiff(handle);
			LEAVE_CRITICAL(data_access);
			hogFileModifyTruncate(handle); // This mode is probably from a defrag, truncate the file when we're done
			hogReleaseMutex(handle, false, false);
		}
		hoggMemLog( "0x%08p: Destroy:start flush", handle);
		hogFileModifyFlush(handle);
		hoggMemLog( "0x%08p: Destroy:end flush", handle);

		if (handle->header_was_invalidated)
		{
			U32 flag = HOG_HEADER_FLAG;
			// Invalidate the hogg file until closed
			fseek(handle->file, 0, SEEK_SET);
			fwrite(&flag, 1, 4, handle->file);
			handle->header_was_invalidated = false;
		}

		// If we were in single app mode, we need to release the mutex
		hogFileSetSingleAppMode(handle, false);

	}

	hoggMemLog( "0x%08p: Destroy:Final", handle);

#if HOGG_WATCH_TIMESTAMPS
	// Don't need to keep watching for filesystem changes
	if (handle->file_cache_is_monitoring)
	{
		handle->file_cache_is_monitoring = 0;
		fileCacheTimestampStopMonitoring(handle->filename_for_timestamp);
	}
#endif

	//assert(isValidHogHandle(handle)); // Allowing destroying twice for now.
	handle->hog_sentinel = 0;

	assert(!handle->debug_in_data_access);
	assert(!handle->debug_in_doing_flush);
	assert(!handle->mutex.reference_count);
	assert(!handle->mod_ops_head);
	// Clean up
	if (handle->file)
		fclose(handle->file);
	if (handle->create_flags & HOG_MULTIPLE_READS)
	{
		FILE *f;
		while ( (f = eaPop(&handle->multipleFiles)) )
		{
			fclose(f);
		}
	}
#if _XBOX
	if (handle->filename && strStartsWith(handle->filename, "cache:"))
		XFlushUtilityDrive();
#endif
	if (handle->fn_to_index_lookup)
		stashTableDestroy(handle->fn_to_index_lookup);
	if (handle->async_new_files)
		stashTableDestroy(handle->async_new_files);
	if (handle->crit_sect_inited) {
		DeleteCriticalSection(&handle->doing_flush);
		DeleteCriticalSection(&handle->data_access);
		DeleteCriticalSection(&handle->file_access);
		handle->crit_sect_inited = true;
	}
	SAFE_FREE(handle->filelist);
	SAFE_FREE(handle->ealist);
	DataListDestroy(&handle->datalist);
	SAFE_FREE(handle->filename);
#if HOGG_WATCH_TIMESTAMPS
	SAFE_FREE(handle->filename_for_timestamp);
#endif
	SAFE_FREE(handle->mutex.name);
	if (handle->version.semaphore)
#if _PS3
        DestroySemaphore(handle->version.semaphore);
#else
		CloseHandle(handle->version.semaphore);
#endif
#if _PS3
	DestroyEvent(handle->done_doing_operation_event);
	DestroyEvent(handle->starting_flush_event);
#else
	CloseHandle(handle->done_doing_operation_event);
	CloseHandle(handle->starting_flush_event);
#endif
	SAFE_FREE(handle->version.name);
	eaiDestroy(&handle->file_free_list);
	eaiDestroy(&handle->ea_free_list);
	hfst2Destroy(handle->free_space2);
	if (bFreeHandle)
		free(handle);
	else
		ZeroStruct(handle);
	PERFINFO_AUTO_STOP();
}

static void hogFileCallCallback(HogFile *handle, const char* path, U32 filesize, U32 timestamp, HogFileIndex file_index)
{
	if (handle->callbacks.fileUpdated)
		handle->callbacks.fileUpdated(handle->callbacks.userData, path, filesize, timestamp, file_index);
}

void hogFileSetCallbacks(HogFile *handle, void *userData, HogCallbackUpdated fileUpdated)
{
	assert(isValidHogHandle(handle));
	ENTER_CRITICAL(data_access);
	handle->callbacks.fileUpdated = fileUpdated;
	handle->callbacks.userData = userData;
	LEAVE_CRITICAL(data_access);
}

static void hogFileReload(HogFile *handle)
{
	int ret;
	HogFile *temp;

	IN_CRITICAL(data_access);

	if (hog_verbose)
		loadstart_printf("Reloading changes to %s...", handle->filename);
	if (!fileExists(handle->filename)) {
		// File was removed
		if (hog_verbose)
			loadend_printf("deleted!");
#if HOGG_WATCH_TIMESTAMPS
		handle->file_last_changed = -1;
#endif
		handle->version.value = -1;
		handle->version.soloValue = -1;
		return;
	}
	temp=hogFileAlloc(handle->err_level, handle->create_flags);
	if (ret=hogFileReadInternal(temp, handle->filename)) {
		assertmsg(0, "Failed to read the hogg we're reloading");
	}
	// Swap everything
	swap(temp, handle, sizeof(*handle));
	// Then swap back what we need to keep around (mutexes, critical sections)
#define SWAPFIELD(field) swap(&temp->field, &handle->field, sizeof(handle->field))
	SWAPFIELD(single_app_mode); // Tied to the mutex reference count
	SWAPFIELD(mutex);
	SWAPFIELD(file_access);
	SWAPFIELD(debug_in_file_access);
	SWAPFIELD(data_access);
	SWAPFIELD(debug_in_data_access);
	SWAPFIELD(callbacks);
	SWAPFIELD(doing_flush);
	SWAPFIELD(debug_in_doing_flush);
	{
		volatile int tempi = handle->shared_reference_count;
		handle->shared_reference_count = temp->shared_reference_count;
		temp->shared_reference_count = tempi;
	}
	handle->has_been_reloaded = true;
	handle->guaranteed_no_ops = true;
	// Do diff and send callbacks (to FolderCache, etc)
	if (handle->callbacks.fileUpdated)
	{
		U32 numfiles;
		U32 i;
		// Look for changes
		numfiles = MAX(hogFileGetNumFiles(handle), hogFileGetNumFiles(temp));
		for (i=0; i<numfiles; i++) {
			const char *newfilename = hogFileGetFileName(handle, i);
			const char *oldfilename = hogFileGetFileName(temp, i);
			if (newfilename && hogFileIsSpecialFile(handle, i))
				newfilename = NULL;
			if (oldfilename && hogFileIsSpecialFile(temp, i))
				oldfilename = NULL;
			if (!newfilename && !oldfilename)
				continue;
			if (!newfilename)
			{
				// File was deleted
				hogFileCallCallback(handle, oldfilename, 0, 0, HOG_INVALID_INDEX);
			} else if (!oldfilename) {
				// File is new
				hogFileCallCallback(handle, newfilename, hogFileGetFileSizeInternal(handle, i), hogFileGetFileTimestampInternal(handle, i), i);
			} else {
				// Both existed, check for rename/reused filename
				U32 newtimestamp = hogFileGetFileTimestampInternal(handle, i);
				U32 oldtimestamp = hogFileGetFileTimestamp(temp, i);
				U32 newsize = hogFileGetFileSizeInternal(handle, i);
				U32 oldsize = hogFileGetFileSize(temp, i);
				if (stricmp(oldfilename, newfilename)!=0) {
					// renamed!
					hogFileCallCallback(handle, oldfilename, 0, 0, HOG_INVALID_INDEX);
					hogFileCallCallback(handle, newfilename, newsize, newtimestamp, i);
				} else {
					// Check for modification
					if (oldsize != newsize || oldtimestamp != newtimestamp) {
						hogFileCallCallback(handle, newfilename, newsize, newtimestamp, i);
					}
				}
			}
		}
	}
	handle->guaranteed_no_ops = false;
	// Clean up
	hogFileDestroy(temp, true);
	if (hog_verbose)
		loadend_printf("done.");
}

static bool hogFileAnyOperationsQueued(HogFile *handle)
{
	IN_CRITICAL(data_access);
	return handle->mod_ops_head || // Something queued
		handle->async_operation_count; // In the middle of anything (probably async mutex release, possibly hogFileModify*Async)
}

void hogFileReloadAsWritable(HogFile *handle)
{
	bool bNeedFlush;

	PERFINFO_AUTO_START_FUNC();

	hogAcquireMutex(handle);

	// When reloading, we are going to call hogFileFlush on the temporary hog that is reloaded,
	// so we need to make sure there are no blocking operations waiting on the critical section of
	// the actual hogg before starting to reload.
	do 
	{
		bNeedFlush = false;
		hogFileModifyFlush(handle);

		ENTER_CRITICAL(data_access);

		if (hogFileAnyOperationsQueued(handle))
		{
			LEAVE_CRITICAL(data_access);
			bNeedFlush = true;
		}
	} while (bNeedFlush);

	handle->create_flags &= ~HOG_READONLY;
	hogFileReload(handle);

	LEAVE_CRITICAL(data_access);
	hogReleaseMutex(handle, false, false);

	PERFINFO_AUTO_STOP();
}


static LONG hogGetVersion(HogFile *handle)
{
	if (!(handle->create_flags & HOG_NO_MUTEX))
	{
		DWORD result;
		LONG old_version;
		assert(handle->mutex.reference_count);
		assert(handle->version.semaphore);
#if _PS3
		result = WaitForSemaphore(handle->version.semaphore, 0);
#else
		WaitForSingleObjectWithReturn(handle->version.semaphore, 0, result);
#endif
		if (result == WAIT_OBJECT_0) {
			// Got it
		} else {
			// Somehow the semaphore object's count got set to 0!
			//  This should only be able to happen with crashing at exactly the right time.
			Sleep(100);
#if _PS3
			result = WaitForSemaphore(handle->version.semaphore, 0);
#else
			WaitForSingleObjectWithReturn(handle->version.semaphore, 0, result);
#endif
			assert(result != WAIT_OBJECT_0); // If it changed, then we have a software bug, someone else modified it while we've got the mutex!
			// This assert will also go off if you create two handles to the same hogg file
			// in the same process.
			ReleaseSemaphore(handle->version.semaphore, 1, NULL); // Artificially increment it to 1
		}
		if (!ReleaseSemaphore(handle->version.semaphore, 1, &old_version)) {
			// Failed?
			assert(0);
			return -1;
		} else {
			// old_version is the decremented version number, starting at 0
			handle->version.value_on_last_get = (old_version + 1);
			return old_version + 1;
		}
	} else {
		return -1;
	}
}

static LONG hogIncVersion(HogFile *handle)
{
	if (!(handle->create_flags & HOG_NO_MUTEX))
	{
		LONG old_version;
		assert(handle->mutex.handle);
		assert(handle->version.semaphore);
		if (!ReleaseSemaphore(handle->version.semaphore, 1, &old_version)) {
			// Failed?
			// Could theoretically happen after 2 billion hogg operations on the same hogg file, I think
			assert(0);
			return -1;
		} else {
			// old_version is the decremented version number, starting at 0
			assert(old_version < LONG_MAX - 10); // About to loop, who knows what would happen
			assert(old_version == handle->version.value_on_last_get);
			handle->version.value_on_last_get = old_version + 1;
			return old_version + 1;
		}
	} else {
		return -1;
	}
}

static void hogAcquireMutex(HogFile *handle)
{
	assert(isValidHogHandle(handle));

	ENTER_CRITICAL(data_access);
	if (handle->mutex.reference_count) {
		handle->mutex.reference_count++;
		//hoggMemLog( "0x%08p: hogAcquireMutex: Increment to %d", handle, handle->mutex.reference_count);
	} else {
		LONG new_semaphore_value;
		__time32_t timestamp=0;
		assert(!handle->async_operation_count);
		assert(!handle->mutex.handle);
		assert(!handle->mod_ops_head); // If actions are queued up, we're already doomed!

		handle->mutex.reference_count++;
 // Mutexes and file timestamps don't update on the Xbox currently
#if !PLATFORM_CONSOLE
		if (!(handle->create_flags & HOG_SKIP_MUTEX))
		{
			if (!(handle->create_flags & HOG_NO_MUTEX))
			{
				hoggMemLog( "0x%08p: hogAcquireMutex:acquireThreadAgnosticMutex()", handle);
				handle->mutex.handle = acquireThreadAgnosticMutex(handle->mutex.name);
			}
			new_semaphore_value = hogGetVersion(handle); // Make sure we get the version
			// Now need to check the file to see if there were any modifications!
			if (handle->file_size &&
				(
#if HOGG_WATCH_TIMESTAMPS
				((timestamp = fileCacheGetTimestamp(handle->filename_for_timestamp))!=handle->file_last_changed) ||
#endif
				new_semaphore_value != handle->version.value))
			{
				assertmsg(!handle->assert_if_semaphore_changed, "Semaphore modified while HOG_SKIP_MUTEX was set");
				assert(!(handle->create_flags & HOG_NO_MUTEX)); // If the file changed, and we're not using a mutex, things have gone horribly wrong

#if HOGG_WATCH_TIMESTAMPS
				// But, still need to flush the changes in the FolderCache without doing a reload?
				//  Unless we don't call FolderCacheQuery in hogReload?  Don't really care about perf if we're doing a reload though.
				fileLastChanged_NoHogReload(handle->filename);
#endif

				hoggMemLog( "0x%08p: hogAcquireMutex:Got Mutex, need reload", handle);
				// Re-load hogg file!
				hogFileReload(handle);
			}
		}
#endif
	}
	LEAVE_CRITICAL(data_access);
}

static void hogReleaseMutex(HogFile *handle, bool needsFlush, bool needsSignalAndAsyncOpDecrement)
{
	bool bDidFlush=false;
	ENTER_CRITICAL(data_access);
	assert(handle->mutex.reference_count);
	if (needsFlush) {
		handle->file_needs_flush = true;
	}

	// Do flush *outside* of the critical section, to prevent
	//   stalling the main thread.  May not catch all cases, so check again below
	if (handle->mutex.reference_count == 1 && handle->file_needs_flush) {
		assert(!(handle->create_flags & HOG_SKIP_MUTEX)); // Shouldn't write to a hogg while this mode is on
		handle->file_needs_flush = false;
		bDidFlush = true;
		LEAVE_CRITICAL(data_access);
		ENTER_CRITICAL(file_access);
		hoggMemLog( "0x%08p: hogReleaseMutex:fflush() (preemptive)", handle);
		fflush(handle->file); // Needs to be regular flush (regardless of policy_flush)
		LEAVE_CRITICAL(file_access);
		ENTER_CRITICAL(data_access);
#if !PLATFORM_CONSOLE
		handle->version.value = hogIncVersion(handle);
		handle->version.soloValue++;
#endif
	}

	if (handle->mutex.reference_count==1) { // Going to decrement to 0
		if (handle->file_needs_flush) {
			assert(!(handle->create_flags & HOG_SKIP_MUTEX)); // Shouldn't write to a hogg while this mode is on
			// This should only happen if someone in another thread *also*
			//  released the mutex while asking for a flush... which shouldn't happen
			//  since only the background thread asks for flushes, and if an operation
			//  is queued in the background thread, then the reference_count won't be == 1
			hoggMemLog( "0x%08p: hogReleaseMutex:fflush() INSIDE CRITICAL!", handle);
			fflush(handle->file); // Needs to be regular flush (regardless of policy_flush)
			handle->file_needs_flush = false;
#if !PLATFORM_CONSOLE
			handle->version.value = hogIncVersion(handle);
			handle->version.soloValue++;
#endif
		}
// Mutexes and file timestamps don't update on the Xbox currently
#if !PLATFORM_CONSOLE

#if HOGG_WATCH_TIMESTAMPS
		// another problem: Need to call fileLastChanged before releasing mutex
		//			fileLastChanged will call this function if it gets a message that the hogg file was modified
		// solution: decrement reference_count at the end - doesn't work when more than one hogg is involved!
		// final solution: version of fileLastChanged which does not try to reload hog files.
		// final final solution: using external system to get timestamps that does not use the FolderCache,
		//  and also works outside of the GameDataDirs
		handle->file_last_changed = fileCacheGetTimestamp(handle->filename_for_timestamp);
#endif
		if (!(handle->create_flags & HOG_SKIP_MUTEX))
		{
			if (!(handle->create_flags & HOG_NO_MUTEX))
			{
				hoggMemLog( "0x%08p: hogReleaseMutex:releaseThreadAgnosticMutex()", handle);
				releaseThreadAgnosticMutex(handle->mutex.handle);
				handle->mutex.handle = NULL;
			}
		}
#endif
	} else {
		//hoggMemLog( "0x%08p: hogReleaseMutex:Decrement to %d", handle, handle->mutex.reference_count);
	}
	handle->mutex.reference_count--;

	if (needsSignalAndAsyncOpDecrement) {
		// Last thing before setting the event, so that anyone calling flush does not get released before this decrements
		InterlockedDecrement(&handle->async_operation_count);
		InterlockedDecrement(&total_async_operation_count);
		SetEvent(handle->done_doing_operation_event); // Signal after updating mutex.reference_count
	}
	LEAVE_CRITICAL(data_access);
}

void hogFileLock(HogFile *handle)
{
	hogAcquireMutex(handle);
}

void hogFileLockDataCS(HogFile *handle)
{
	assert(handle->mutex.reference_count);
	ENTER_CRITICAL(data_access);
}

void hogFileUnlockDataCS(HogFile *handle)
{
	assert(handle->mutex.reference_count);
	LEAVE_CRITICAL(data_access);
}

void *hogFileDupHandle(HogFile *handle, HogFileIndex file_index)
{
	HANDLE ret=INVALID_HANDLE_VALUE;
	HogFileListEntry *file_entry;
	U32 packed_size, unpacked_size;
	LARGE_INTEGER offs;
				
	file_entry = hogFileWaitForFile(handle, file_index, true, true); // Enters data_access and file_access critical and mutex
	if (!file_entry)
		goto fail;
	hogFileGetSizesInternal(handle, file_index, &unpacked_size, &packed_size);
	assertmsg(unpacked_size, "Attempting to lock a file which does not exist"); // File must exist
	assertmsg(packed_size == 0, "Can only lock uncompressed files"); // Can't directly lock a packed file
	ret = fileDupHandle(handle->file);
	offs.QuadPart = file_entry->header.offset;
	assert(ret != INVALID_HANDLE_VALUE);
	assert(SetFilePointerEx(ret, offs, NULL, FILE_BEGIN));
fail:
	LEAVE_CRITICAL(data_access);
	LEAVE_CRITICAL(file_access);
	hogReleaseMutexAsync(handle, false);
	return ret;
}


void hogFileSetSingleAppMode(HogFile *handle, bool singleAppMode)
{
	assert(isValidHogHandle(handle));

	ENTER_CRITICAL(data_access);
	if (handle->single_app_mode!=singleAppMode) {
		hoggMemLog( "0x%08p: SetSingleAppMode(%d)", handle, (int)singleAppMode);
		handle->single_app_mode=singleAppMode;
		LEAVE_CRITICAL(data_access);
		if (singleAppMode)
			hogAcquireMutex(handle);
		else
			hogReleaseMutex(handle, false, false);
	} else {
		LEAVE_CRITICAL(data_access);
	}
}

void hogFileFreeRAMCache(HogFile *handle)
{
	ENTER_CRITICAL(file_access);
	ENTER_CRITICAL(data_access);
	if (handle->create_flags & HOG_RAM_CACHED)
	{
		handle->create_flags &=~ HOG_RAM_CACHED;
		fileRAMCachedFreeCache(handle->file);
	}
	LEAVE_CRITICAL(data_access);
	LEAVE_CRITICAL(file_access);
}



void hogFileUnlock(HogFile *handle)
{
	assert(isValidHogHandle(handle));
	hogReleaseMutex(handle, false, false);
}

void hogFileCheckForModifications(HogFile *handle)
{
	hogAcquireMutex(handle);
	hogReleaseMutex(handle, false, false);
}

void hogFileSetSkipMutex(HogFile *handle, bool skip_mutex)
{
	assert(!handle->single_app_mode);
	if (skip_mutex)
		hogFileCheckForModifications(handle);
	hogFileModifyFlush(handle);
	do{
		ENTER_CRITICAL(data_access);
		if (handle->mutex.reference_count == 0)
		{
			if (skip_mutex)
			{
				assert(!(handle->create_flags & HOG_SKIP_MUTEX));
				handle->create_flags |= HOG_SKIP_MUTEX;
			} else {
				assert((handle->create_flags & HOG_SKIP_MUTEX));
				handle->create_flags &= ~HOG_SKIP_MUTEX;
			}
			break;
		}
		Sleep(1);
		LEAVE_CRITICAL(data_access);
	} while (true);
	if (!skip_mutex)
	{
		handle->assert_if_semaphore_changed = 1;
		hogFileCheckForModifications(handle); // asserts if the semaphore no longer matches up
		handle->assert_if_semaphore_changed = 0;
	}
	LEAVE_CRITICAL(data_access);

}


static void saveDebugHog(FILE *file, const char *filename, int number)
{
	char fmt[CRYPTIC_MAX_PATH];
	char fn[CRYPTIC_MAX_PATH];
	char src[CRYPTIC_MAX_PATH], dst[CRYPTIC_MAX_PATH];
	fflush(file);
	strcpy(fmt, filename);
	changeFileExt(fmt, "/%03d.hogg", fmt);
	sprintf(fn, FORMAT_OK(fmt), number);
	fileLocateWrite(filename, src);
	fileLocateWrite(fn, dst);
	mkdirtree(dst);
	fileCopy(src, dst);
}

static void hogFileAllocFileEntry(HogFile *handle)
{
	U32 i;
	bigDynArrayAddStruct(handle->filelist, handle->filelist_count, handle->filelist_max);
	i = (U32)handle->filelist_count-1;
	eaiPush(&handle->file_free_list, i+1);
}

static void hogFileAllocEAEntry(HogFile *handle)
{
	U32 i;
	bigDynArrayAddStruct(handle->ealist, handle->ealist_count, handle->ealist_max);
	i = (U32)handle->ealist_count-1;
	handle->ealist[i].header.flags |= HOGEA_NOT_IN_USE;
	eaiPush(&handle->ea_free_list, i+1);
}

static int hogFileCreateFileEntry(HogFile *handle) // Returns file number
{
	U32 i;
	IN_CRITICAL(data_access);
	if (eaiSize(&handle->file_free_list)==0) {
		assert(!handle->file); // Not during run-time modifications
		hogFileAllocFileEntry(handle);
	}
	i = eaiPop(&handle->file_free_list) - 1;
	//ZeroStruct(&handle->filelist[i]); // should be zeroed already? // Can't zero dirty flag/count!
	assert(!handle->filelist[i].dirty && !handle->filelist[i].dirty_async);
	handle->num_files++;
	handle->filelist[i].in_use = true;
	handle->filelist[i].header.offset = 0;
	handle->filelist[i].header.size = HOG_NO_VALUE;
	handle->filelist[i].header.timestamp = HOG_NO_VALUE;
	return i;
}

static HogFileListEntry *hogGetFileListEntrySafe(HogFile *handle, HogFileIndex file, bool should_assert, bool allow_deleted)
{
	HogFileListEntry *file_entry;
	IN_CRITICAL(data_access);
	if (file < 0 || file >= handle->filelist_count || file == HOG_NO_VALUE) {
		if (should_assert) {
			hogShowError(handle, 1, "Invalid file index ", file);
		}
		return NULL;
	}
	file_entry = &handle->filelist[file];
	if (!file_entry->in_use || !allow_deleted && file_entry->queued_for_delete) {
		if (should_assert) {
			hogShowError(handle, 1, "Invalid file index (file removed) ", file);
		}
		return NULL;
	}
	return file_entry;
}

__forceinline static HogFileListEntry *hogGetFileListEntry(HogFile *handle, HogFileIndex file)
{
	return hogGetFileListEntrySafe(handle, file, true, false);
}


static void hogDirtyFile(HogFile *handle, HogFileIndex file)
{
	HogFileListEntry *file_entry;
	ENTER_CRITICAL(data_access);
	assert(file >= 0 && file < handle->filelist_count && file != HOG_NO_VALUE);
	file_entry = &handle->filelist[file];
	assert(file_entry->in_use);
	file_entry->dirty++;
	LEAVE_CRITICAL(data_access);
}

static void hogUnDirtyFile(HogFile *handle, HogFileIndex file)
{
	HogFileListEntry *file_entry;
	ENTER_CRITICAL(data_access);
	assert(file >= 0 && file < handle->filelist_count && file != HOG_NO_VALUE);
	file_entry = &handle->filelist[file];
	assert(file_entry->in_use);
	assert(file_entry->dirty>0);
	file_entry->dirty--;
	LEAVE_CRITICAL(data_access);
}

static HogEAListEntry *hogGetEAListEntry(HogFile *handle, S32 ea_id)
{
	HogEAListEntry *ea_entry;
	IN_CRITICAL(data_access);
	if (ea_id==HOG_NO_VALUE)
		return NULL;
	assert(ea_id >= 0 && ea_id < (S32)handle->ealist_count);
	ea_entry = &handle->ealist[ea_id];
	assert(EA_IN_USE(ea_entry));
	return ea_entry;
}

static int hogFileCreateEAEntry(HogFile *handle, U32 file)
{
	U32 i;
	HogFileListEntry *file_entry = hogGetFileListEntry(handle, file);
	IN_CRITICAL(data_access);
	assert(file_entry->header.headerdata.ea_id == 0);
	if (eaiSize(&handle->ea_free_list)==0) {
		assert(!handle->file); // Not during run-time modifications
		hogFileAllocEAEntry(handle);
	}
	i = eaiPop(&handle->ea_free_list) - 1;
	assert(!EA_IN_USE_STRUCT(handle->ealist[i]));
	handle->ealist[i].header.flags &= ~HOGEA_NOT_IN_USE;
	//handle->ealist[i].in_use = true;
	handle->ealist[i].header.header_data_id = HOG_NO_VALUE;
	handle->ealist[i].header.name_id = HOG_NO_VALUE;
	file_entry->header.headerdata.flagFFFE = 0xFFFE;
	file_entry->header.headerdata.ea_id = i;
	return i;
}



static int hogFileNameFile(HogFile *handle, HogEAListEntry *ea_entry, const char *name, HogFileIndex file_index)
{
	const char *stored_name;
	IN_CRITICAL(data_access);
	assert(ea_entry->header.name_id == HOG_NO_VALUE);
	ea_entry->header.name_id = DataListAdd(&handle->datalist, name, (int)strlen(name)+1, true, NULL);
	stored_name = DataListGetString(&handle->datalist, ea_entry->header.name_id, true);
	assert(storeNameToId(handle,stored_name,file_index));
	return 0;
}

static int hogFileAddHeaderData(HogFile *handle, HogEAListEntry *ea_entry, const U8 *header_data, U32 header_data_size)
{
	IN_CRITICAL(data_access);
	assert(ea_entry->header.header_data_id == HOG_NO_VALUE);
	ea_entry->header.header_data_id = DataListAdd(&handle->datalist, header_data, header_data_size, false, NULL);
	return 0;
}

static int hogFileUpdateHeaderData(HogFile *handle, HogEAListEntry *ea_entry, const U8 *header_data, U32 header_data_size)
{
	IN_CRITICAL(data_access);
	if (ea_entry->header.header_data_id == HOG_NO_VALUE)
		return hogFileAddHeaderData(handle, ea_entry, header_data, header_data_size);

	ea_entry->header.header_data_id = DataListUpdate(&handle->datalist, ea_entry->header.header_data_id, header_data, header_data_size, false, NULL);
	return 0;
}


// Note: takes ownership of data pointer
// Just used internally during initial Hog creation
static int hogFileAddFileDuringCreate(HogFile *handle, const char *name, U8 *data, U32 size, U32 timestamp, const U8 *header_data, U32 header_data_size, U32 unpacksize, U32 *crc)
{
	int file;
	int ea_index;
	HogFileListEntry *file_entry;
	HogEAListEntry *ea_entry;
	ENTER_CRITICAL(data_access);
	file = hogFileCreateFileEntry(handle);
	file_entry = hogGetFileListEntry(handle, file);
	file_entry->header.size = size;
	file_entry->header.timestamp = timestamp;
	file_entry->data = data;
	ea_index = hogFileCreateEAEntry(handle, file);
	ea_entry = &handle->ealist[ea_index];
	ea_entry->header.unpacked_size = unpacksize;
	hogFileNameFile(handle, ea_entry, name, file);
	if (!crc) {
		U32 autocrc[4];
		assert(!unpacksize);
		cryptMD5Update(data,size);
		cryptMD5Final(autocrc);
		file_entry->header.checksum = autocrc[0];
	} else {
		file_entry->header.checksum = crc[0];
	}
	if (header_data) {
		hogFileAddHeaderData(handle, ea_entry, header_data, header_data_size);
	}
	LEAVE_CRITICAL(data_access);
	return file;
}

// Note: takes ownership of data pointer
// Just used internally during initial Hog creation
static int hogFileUpdateFile(HogFile *handle, const char *name, U8 *data, U32 size, U32 timestamp, U8 *header_data, U32 header_data_size, U32 unpacksize, U32 *crc)
{
	HogFileIndex file;
	int ea_index;
	HogEAListEntry *ea_entry;
	HogFileListEntry *file_entry;

	ENTER_CRITICAL(data_access);

	file = hogFileFind(handle, name);
	assert(file != HOG_NO_VALUE);

	file_entry = hogGetFileListEntry(handle, file);
	file_entry->header.size = size;
	file_entry->header.timestamp = timestamp;
	SAFE_FREE(file_entry->data);
	file_entry->data = data;
	assert(file_entry->header.headerdata.ea_id != HOG_NO_VALUE);
	ea_index = (int)file_entry->header.headerdata.ea_id;
	ea_entry = &handle->ealist[ea_index];
	ea_entry->header.unpacked_size = unpacksize;
	assert(ea_entry->header.name_id != HOG_NO_VALUE); // Must already have a name
	if (!crc) {
		U32 autocrc[4];
		assert(!unpacksize);
		cryptMD5Update(data,size);
		cryptMD5Final(autocrc);
		file_entry->header.checksum = autocrc[0];
	} else {
		file_entry->header.checksum = crc[0];
	}
	if (header_data) {
		hogFileUpdateHeaderData(handle, ea_entry, header_data, header_data_size);
	}
	LEAVE_CRITICAL(data_access);
	return file;
}


void hogChecksumAndPackEntry(NewPigEntry *entry)
{
	PERFINFO_AUTO_START_FUNC();
	if (!entry->header_data) {
		entry->header_data = pigGetHeaderData(entry, &entry->header_data_size);
	}
	pigChecksumAndPackEntry(entry);
	PERFINFO_AUTO_STOP();
}

static U64 hogFileGetSlackByName(const char *filename, U32 current_size)
{
	static char *needSlackExt[] = {
		".ms", ".bin", ".def"
	};
	int i;
	F32 ratio = 0;
	const char *ext = filename?strrchr(filename, '.'):NULL;
	if (ext) {
		for (i=0; i<ARRAY_SIZE(needSlackExt); i++) {
			if (stricmp(ext, needSlackExt[i])==0) {
				ratio = 0.05;
				break;
			}
		}
	}
	if (filename && filename[0]=='?') // Special file
		ratio = 0.05;
	return MIN((U64)(current_size * ratio), HOG_MAX_SLACK_SIZE);
}

// Calculates how much slack a file needs
static U64 hogFileGetSlack(HogFile *handle, U32 file)
{
	return 0; // Not doing slack seems to be fine.  Also, HFST2 does not deal with slack
// 	HogFileListEntry *file_entry = hogGetFileListEntry(handle, file);
// 	IN_CRITICAL(data_access);
// 	if (file_entry->header.headerdata.flagFFFE == 0xFFFE && 
// 		file_entry->header.headerdata.ea_id != HOG_NO_VALUE)
// 	{
// 		HogEAListEntry *ea_entry = hogGetEAListEntry(handle, (S32)file_entry->header.headerdata.ea_id);
// 		if (ea_entry->header.name_id != HOG_NO_VALUE) {
// 			const char *filename = DataListGetString(&handle->datalist, ea_entry->header.name_id);
// 			return hogFileGetSlackByName(filename, file_entry->header.size);
// 		}
// 	}
// 	// No filename, assume lightmap, and pack tightly
// 	return 0;
}

static int hogFileMakeSpecialFiles(HogFile *handle)
{
	U32 size;
	U8 *data;
	U32 timestamp = (handle->create_flags&HOG_NO_INTERNAL_TIMESTAMPS)?0:_time32(NULL);
	IN_CRITICAL(data_access);
	// Name and HeaderData list
	data = DataListWrite(&handle->datalist, &size);
	hogFileAddFileDuringCreate(handle, HOG_DATALIST_FILENAME, data, size, timestamp, NULL, 0, 0, NULL);
	// Now that we just added ourself, re-write out the data
	data = DataListWrite(&handle->datalist, &size);
	hogFileUpdateFile(handle, HOG_DATALIST_FILENAME, data, size, timestamp, NULL, 0, 0, NULL);
	return 1;
}

static U32 hogFileOffsetOfOpJournal(HogFile *handle)
{
	return sizeof(HogHeader);
}

static U32 hogFileOffsetOfDLJournal(HogFile *handle)
{
	IN_CRITICAL(data_access);
	return sizeof(HogHeader) +
		handle->header.op_journal_size;
}

static U32 hogFileOffsetOfFileList(HogFile *handle)
{
	IN_CRITICAL(data_access);
	return sizeof(HogHeader) +
		handle->header.op_journal_size +
		handle->header.dl_journal_size;
}

static U32 hogFileOffsetOfEAList(HogFile *handle)
{
	IN_CRITICAL(data_access);
	return sizeof(HogHeader) +
		handle->header.op_journal_size +
		handle->header.dl_journal_size +
		handle->header.file_list_size;
}

static U32 hogFileOffsetOfData(HogFile *handle)
{
	IN_CRITICAL(data_access);
	return sizeof(HogHeader) +
		handle->header.op_journal_size +
		handle->header.dl_journal_size +
		handle->header.file_list_size +
		handle->header.ea_list_size;
}

static HogFileHeader *hogSerializeFileHeaders(HogFile *handle)
{
	U32 i;
	HogFileHeader *file_headers = NULL;
	IN_CRITICAL(data_access);
	file_headers = calloc(sizeof(HogFileHeader), handle->filelist_count);
	for (i=0; i<handle->filelist_count; i++) {
		if (handle->filelist[i].in_use) {
			if (!handle->filelist[i].header.size) {
				handle->filelist[i].header.offset = 0;
			}
			file_headers[i] = handle->filelist[i].header;
		} else {
			file_headers[i].size = HOG_NO_VALUE;
		}
		endianSwapHogFileHeaderIfBig(&file_headers[i]);
	}
	return file_headers;
}

static HogEAHeader *hogSerializeEAHeaders(HogFile *handle)
{
	U32 i;
	HogEAHeader *ea_headers;
	IN_CRITICAL(data_access);
	ea_headers = calloc(handle->header.ea_list_size, 1);
	for (i=0; i<handle->ealist_count; i++) {
		if (EA_IN_USE_STRUCT(handle->ealist[i])) {
			ea_headers[i] = handle->ealist[i].header;
		} else {
			ea_headers[i].flags = HOGEA_NOT_IN_USE;
		}
		endianSwapHogEAHeaderIfBig(&ea_headers[i]);
	}
	return ea_headers;
}

static int intRevCompare(const int *i1, const int *i2){
	if(*i1 < *i2)
		return 1;

	if(*i1 == *i2)
		return 0;

	return -1;
}

U32 DEFAULT_LATELINK_hogWarnImplementationLimitExceeded(const char *hog, U32 implementation_limit_warning, U32 file_count)
{
	ErrorFilenameDeferredf(hog, "Hog file has %lu files, limit is 100,000,000", file_count);
	return UINT_MAX;
}

// Warn if this hog has exceeded safe operating limits.
static void hogCheckImplementationLimits(HogFile *handle, U32 file_count)
{
	if (file_count > 50000000 && file_count > handle->implementation_limit_warning)
		handle->implementation_limit_warning = hogWarnImplementationLimitExceeded(handle->filename, handle->implementation_limit_warning, file_count);
}

static void hogGrowFileList(HogFile *handle, U32 new_count)
{
	size_t size;

	hogCheckImplementationLimits(handle, new_count);

	IN_CRITICAL(data_access);
	//	__deref_out(handle)

	while (handle->filelist_count != new_count) {
		hogFileAllocFileEntry(handle);
	}
	//	qsort(handle->file_free_list, eaiSize(&handle->file_free_list), sizeof(handle->file_free_list[0]), intRevCompare);
	ea32QSort(handle->file_free_list, intRevCompare);
	size = sizeof(HogFileHeader) * handle->filelist_count;
	assert(size <= UINT_MAX);
	handle->header.file_list_size = (U32)size;
}

static void hogGrowEAList(HogFile *handle, U32 new_count)
{
	size_t size;

	IN_CRITICAL(data_access);
	while (handle->ealist_count != new_count) {
		hogFileAllocEAEntry(handle);
	}

	ea32QSort(handle->ea_free_list, intRevCompare);
	size = sizeof(HogEAHeader) * handle->ealist_count;
	assert(size <= UINT_MAX);
	handle->header.ea_list_size = (U32)size;
}

PigErrLevel hogGetErrLevel(const HogFile *handle)
{
	return handle->err_level;
}


static char **g_hog_path_redirects;
void hogAddPathRedirect(const char *srcpath, const char *destpath) // e.g. "devkit:/FightClub/ -> game:/
{
	char *s = strdup(srcpath);
	forwardSlashes(s);
	assert(strEndsWith(s, "/"));
	eaPush(&g_hog_path_redirects, s);
	s = strdup(destpath);
	forwardSlashes(s);
	assert(strEndsWith(s, "/"));
	eaPush(&g_hog_path_redirects, s);
}

static char *makeMutexName(const char *fname, const char *prefix)
{
	int i, len = 1 + (int)strlen(prefix); 
	char *buf, full[1024];
	int path_len;

	// Make full path
	if(fname[0] == '.' && (fname[1] == '/' || fname[1] == '\\')) {
		fname += 2;
		makefullpath_s(fname, SAFESTR(full));
	} else {
		if (fileIsAbsolutePath(fname))
			strcpy(full, fname);
		else
			fileLocateWrite(fname, full);
		if (!strchr(full, ':')) {
			makefullpath_s(fname, SAFESTR(full));
		}
	}

	path_len = (int)strlen(full);
	if (len + path_len > MAX_PATH - 10)
	{
		int excess = path_len - (MAX_PATH - 10 - len);
		assert(excess < path_len);
		strcpy(full, full + excess); // Just get the last characters if we're not going to fit in MAX_PATH
	}
	len += (int)strlen(full);
	buf = malloc(len);
	strcpy_s(buf, len, full);
	for(i = 0; i < len; i++)
	{
		if(buf[i] == '/' || buf[i] == '\\' || buf[i] == ':')
			buf[i] = '_';
		else
			buf[i] = toupper(buf[i]);
	}
	strcat_s(buf, len, prefix);

	return buf;
}

static int hogFileWriteFresh(HogFile *handle, const char *fname)
{
	FILE *fout;
	U8 *scratch=NULL;
	int success=0;
	HogFileHeader *file_headers = NULL;
	HogEAHeader *ea_headers = NULL;
	U32 i;
	U64 offset=0;
	U32 total_files;
	U32 total_eas;
	U64 loc;
	int temp_buffer_size;
	char fn[MAX_PATH];

	makeDirectoriesForFile(fname); // Added to make hogs just the slightest bit easier to use.  Remove if it breaks anything.

#define CHECKED_FWRITE(ptr, size, required) if (hog_mode_no_data && !(required)) { fseek(fout, size, SEEK_CUR); } else { if ((U32)fwrite(ptr, 1, size, fout) != size) goto fail; }

	if (g_hogOpenMode == HogSafeAgainstOSCrash) {
		fout = fileOpen(fname,"wcb");
	} else {
		fout = fileOpen(fname,"wb");
	}
	if (!fout)
		return 0;

	assert(fout->iomode == IO_WINIO); // Otherwise HogSafeAgainstAppCrash is a lie (we need to do flushes)

	temp_buffer_size = MAX(MAX(HOG_OP_JOURNAL_SIZE, handle->header.dl_journal_size), HOG_MAX_SLACK_SIZE);
	scratch = ScratchAlloc(temp_buffer_size);
	ZeroStruct(scratch);

	ENTER_CRITICAL(file_access);
	ENTER_CRITICAL(data_access);

	hogFilenameForMutexName(fname, SAFESTR(fn));

	handle->filename = strdup(fn);
#if HOGG_WATCH_TIMESTAMPS
	handle->filename_for_timestamp = strdup(fn);
#endif
	if (!(handle->create_flags & HOG_NO_MUTEX))
	{
		handle->mutex.name = makeMutexName(fn, "");
		handle->version.name = makeMutexName(fn, "Ver");
		handle->version.semaphore = CreateSemaphore_UTF8(NULL, 1, LONG_MAX, handle->version.name);
		devassertmsg(handle->version.semaphore, "Failed to open semaphore object");
	}
	assert(!handle->fn_to_index_lookup);
	handle->fn_to_index_lookup = stashTableCreateWithStringKeys(1, StashDefault);
	assert(!handle->async_new_files);
	handle->async_new_files = stashTableCreateWithStringKeys(1, StashDefault);

	// Turn special data into files
	hogFileMakeSpecialFiles(handle);

	// Write a zeroed header first so that if this file is read, it is
	//  known as a bad file
	{
		HogHeader header;
		ZeroStruct(&header);
		CHECKED_FWRITE(&header, sizeof(header),true);
	}

	assert(handle->filelist_count <= UINT_MAX);
	total_files = (U32)handle->filelist_count;
	total_files += (U32)(total_files * FILELIST_PAD); // Add padding for growth
	if (total_files < handle->filelist_min)
	{
		total_files = (U32)handle->filelist_min;
	}
	hogGrowFileList(handle, total_files);
	assert(handle->ealist_count <= UINT_MAX);
	total_eas = (U32)handle->ealist_count;
	total_eas += (U32)(total_eas * FILELIST_PAD); // Add padding for growth
	if (total_eas < handle->ealist_min)
	{
		total_eas = (U32)handle->ealist_min;
	}
	hogGrowEAList(handle, total_eas);

	// Fill in header (but write later)
	assert(sizeof(HogFileHeader) * handle->filelist_count < UINT_MAX);
	assert(sizeof(HogEAHeader) * handle->ealist_count < UINT_MAX);
	handle->header.file_list_size = (U32)(sizeof(HogFileHeader) * handle->filelist_count);
	handle->header.ea_list_size = (U32)(sizeof(HogEAHeader) * handle->ealist_count);
	handle->header.datalist_fileno = hogFileFind(handle, HOG_DATALIST_FILENAME);

	// Write operations journal block
	// TODO: Put journal entry of DeletePigFile?
	CHECKED_FWRITE(scratch, handle->header.op_journal_size,true);
	// Write DataList journal block
	CHECKED_FWRITE(scratch, handle->header.dl_journal_size,true);

	// Fill in offsets
	offset = hogFileOffsetOfData(handle);
	for (i=0; i<handle->filelist_count; i++) {
		if (handle->filelist[i].in_use) {
			if (handle->filelist[i].header.size) {
				handle->filelist[i].header.offset = offset;
				offset += handle->filelist[i].header.size + hogFileGetSlack(handle, i);
			}
		}
	}

	// Assemble and write HogFileHeaders and slack
	file_headers = hogSerializeFileHeaders(handle);
	CHECKED_FWRITE(file_headers, handle->header.file_list_size,true);
	SAFE_FREE(file_headers);

	// Assemble and write EAList and slack
	ea_headers = hogSerializeEAHeaders(handle);
	CHECKED_FWRITE(ea_headers, handle->header.ea_list_size,true);
	SAFE_FREE(ea_headers);

	// Write files
	offset = hogFileOffsetOfData(handle);
	loc = ftell(fout);
	assert(loc == offset);
	for (i=0; i<handle->filelist_count; i++)
	{
		if (handle->filelist[i].in_use && handle->filelist[i].header.size)
		{
			assert(offset <= handle->filelist[i].header.offset);
			if (offset < handle->filelist[i].header.offset) {
				U32 amt = (U32)(handle->filelist[i].header.offset - offset);
				assert(amt <= sizeof(scratch));
				// Write slack
				CHECKED_FWRITE(scratch, amt, false);
				offset += amt;
			}
			loc = ftell(fout);
			assert(loc == handle->filelist[i].header.offset);
			assert(loc == offset);
			CHECKED_FWRITE(handle->filelist[i].data, handle->filelist[i].header.size, i==handle->header.datalist_fileno);
			offset += handle->filelist[i].header.size;
		}
	}

	// Go back and write header, now that we're done writing
	fseek(fout, 0, SEEK_SET);
	{
		HogHeader header = handle->header;
		endianSwapStructIfBig(parseHogHeader, &header);
		CHECKED_FWRITE(&header, sizeof(header), true);
	}
	// Clear journal
	CHECKED_FWRITE(scratch, handle->header.op_journal_size, true);

	success = 1;
fail:
	// Cleanup
	fclose(fout);
	ScratchFree(scratch);
	SAFE_FREE(file_headers);
	SAFE_FREE(ea_headers);
	// Free data we allocated in here
	SAFE_FREE(handle->filelist[handle->header.datalist_fileno].data);
	LEAVE_CRITICAL(data_access);
	LEAVE_CRITICAL(file_access);
	return success;
#undef CHECKED_FWRITE
}

// Does *not* take ownership of data pointers
static int hogCreateFromMem(const char *fname, PigErrLevel err_level, HogFileCreateFlags flags, U32 datalist_journal_size)
{
	HogFile hog_file={0};
	int success=0;

	hogFileCreate(&hog_file, err_level, g_hogOpenMode, flags, datalist_journal_size);

	success = hogFileWriteFresh(&hog_file, fname);

	//hogFileDumpInfo(&hog_file, 2, 1);

	// Cleanup
	hogFileDestroy(&hog_file, false);
	return success;
}

int g_debug_hogg_last_error;
int g_debug_hogg_last_error2;


static int checkedWriteData(HogFile *handle, const void *data, U32 size, U64 offs)
{
	int ret=0;
#define FAIL(code) { ret = code; goto fail; }
	ENTER_CRITICAL(file_access);
	assert(!handle->read_only);
	if (hog_debug_check)
	{
		U32 size1 = size / 2;
		U32 size2 = size - size1;
		U64 offs2 = offs + size1;
		if (size1) {
			if (0 != fseek(handle->file, offs, SEEK_SET))
				FAIL(3);
			if (size1 != (U32)fwrite(data, 1, size1, handle->file))
				FAIL(4);
			DEBUG_CHECK();
		}
		if (0 != fseek(handle->file, offs2, SEEK_SET))
			FAIL(5);
		if (size2 != (U32)fwrite((U8*)data + size1, 1, size2, handle->file))
			FAIL(6);
		DEBUG_CHECK();
	} else {
		int retry_count=10;
retry_write:
		if (0 != fseek(handle->file, offs, SEEK_SET))
			FAIL(1);

		// Write the file data in small chunks, because it's faster on some disks.
		{
			U32 remainingSize = size;
			const U8* curData = data;
			while(remainingSize){
#if !PLATFORM_CONSOLE
				const U32 curSize = remainingSize;
#else
				const U32 curSize = MIN(remainingSize, 64 * 1024);
#endif
				if (curSize != (U32)fwrite(curData, 1, curSize, handle->file))
				{
					char shortname[MAX_PATH];
					g_debug_hogg_last_error = GetLastError();
					g_debug_hogg_last_error2 = errno;
					filelog_printf("hog_errors.log", "Failed writing to file, retrying...  last error = %d\n", g_debug_hogg_last_error);
					
					RunHandleExeAndAlert("HOGG_WRITE_FAIL", shortname, "hog_errors", "Failed writing to file, retrying...  last error = %dn", g_debug_hogg_last_error);
					
					Sleep(15);

					retry_count--;
					if (retry_count>0)
						goto retry_write;
					FAIL(2);
				}
				remainingSize -= curSize;
				curData += curSize;
			}
		}
	}
	hog_write_bytes_estimate += size;
fail:
	LEAVE_CRITICAL(file_access);
	return ret;
#undef FAIL
}

static int checkedReadData(HogFile *handle, U8 *data, U32 size, U64 offs)
{
	int ret=0;
	ENTER_CRITICAL(file_access);
	if (0 != fseek(handle->file, offs, SEEK_SET))
		ret = 1;
	else if (size != (U32)fread(data, 1, size, handle->file))
		ret = 2;
	hog_read_bytes_estimate += size;
	LEAVE_CRITICAL(file_access);
	return ret;
}

static bool hogJournalHasWork(HogFile *handle, U8 * journal)
{
	U32 possible_terminator;
	U32 journal_entry_size = *(U32*)journal;
	if (journal_entry_size==0) {// No journal data
		hoggMemLog( "0x%08p: Journal:None", handle);
		return false;
	}
	hoggMemLog( "0x%08p: Journal:Size: %d", handle, journal_entry_size);
	if (journal_entry_size+sizeof(U32) > HOG_OP_JOURNAL_SIZE) {// Invalid
		hoggMemLog( "0x%08p: Journal:Too large", handle);
		return false;
	}
	// Look for terminator
	possible_terminator = *(U32*)&journal[journal_entry_size+sizeof(U32)];
	if (isBigEndian())
		possible_terminator = endianSwapU32(possible_terminator);
	if (possible_terminator == HOG_JOURNAL_TERMINATOR) {
		hoggMemLog( "0x%08p: Journal:Found and terminated", handle);
		return true;
	}
	hoggMemLog( "0x%08p: Journal:Not terminated", handle);
	// Was in the middle of writing the journal
	return false;
}

static void hogFillInFileModBasics(HogFile *handle, HogFileMod *mod, U32 byte_size)
{
	mod->filelist_offset = hogFileOffsetOfFileList(handle);
	mod->ealist_offset = hogFileOffsetOfEAList(handle);
	mod->byte_size = (byte_size==0)?0:(sizeof(*mod) + byte_size);
}

static int hogJournalDoWork(HogFile *handle, U8 * journal)
{
	HogFileMod mod;
	int ret;
	HogOpJournalAction op;

	// List of errors that this function can return.  Add new errors to the end of this list.
	// Warning: Do not add values in the middle of this list.
	enum hogJournalDoWorkErrors {
		hogJournalDoWorkErrors_DELETE = 1,
		hogJournalDoWorkErrors_ADD_OR_UPDATE,
		hogJournalDoWorkErrors_DEPRECATED,
		hogJournalDoWorkErrors_MOVE,
		hogJournalDoWorkErrors_RESIZE,
		hogJournalDoWorkErrors_DATALISTFLUSH,
		hogJournalDoWorkErrors_READONLY,
	};

	IN_CRITICAL(data_access);

	// If the hogg has a journaled operation, we *must* apply it before trying to read the header, even if in read-only mode :(
	if (handle->read_only || handle->create_flags & HOG_NO_REPAIR)
	{
		hoggMemLog( "0x%08p: Journal:Hogg journal operation needs to be applied, but file opened in read-only or no-repair mode.", handle);
		return hogJournalDoWorkErrors_READONLY;
	}

	hogFileFreeRAMCache(handle);

	handle->last_journal_size = *(U32*)journal;
	journal += sizeof(U32);
	op = *(HogOpJournalAction*)journal; // endianSwapped in hogJournalHaswork
	hoggMemLog( "0x%08p: Journal:Op: %d", handle, op);

	switch(op) {
	xcase HOJ_DELETE:
	{
		HFJDelete *journal_delete = (HFJDelete *)journal;
		endianSwapStructIfBig(parseHFMDelete, &journal_delete->del);
		hoggMemLog( "0x%08p: Journal:Del: file_id:%d ea_id:%d", handle, journal_delete->del.file_index, journal_delete->del.ea_id);
		// Re-do delete operation (before we read in the data)
		hogFillInFileModBasics(handle, &mod, -1);
		mod.type = HFM_DELETE;
		mod.del = journal_delete->del;
		if (ret=hogFileModifyDoDeleteInternal(handle, &mod))
			return NESTED_ERROR(hogJournalDoWorkErrors_DELETE, ret);
	}
	xcase HOJ_ADD_OR_UPDATE:
	{
		HFJAddOrUpdate *journal_addOrUpdate = (HFJAddOrUpdate *)journal;
		endianSwapStructIfBig(parseHFMAddOrUpdate, &journal_addOrUpdate->addOrUpdate);
		hoggMemLog( "0x%08p: Journal:AddOrUpdate: file_id:%d size:%d timestamp:%d offset:%"FORM_LL"d", handle,
			journal_addOrUpdate->addOrUpdate.file_index, journal_addOrUpdate->addOrUpdate.size, journal_addOrUpdate->addOrUpdate.timestamp, journal_addOrUpdate->addOrUpdate.offset);
		if (journal_addOrUpdate->addOrUpdate.headerdata.flagFFFE == 0xFFFE) {
			hoggMemLog( "0x%08p: Journal:AddOrUpdate: ea_id:%d  unpacksize:%d name_id:%d header_data_id:%d crc:%08X", handle,
				journal_addOrUpdate->addOrUpdate.headerdata.ea_id, journal_addOrUpdate->addOrUpdate.ea_data.unpacksize, journal_addOrUpdate->addOrUpdate.ea_data.name_id, journal_addOrUpdate->addOrUpdate.ea_data.header_data_id,
				journal_addOrUpdate->addOrUpdate.checksum);
		} else {
			hoggMemLog( "0x%08p: Journal:AddOrUpdate: NO ea_id; headerdata: 0x%0"FORM_LL"x", handle,
				journal_addOrUpdate->addOrUpdate.headerdata.raw);
		}
		hogFillInFileModBasics(handle, &mod, -1);
		mod.type = HFM_ADD;
		mod.addOrUpdate = journal_addOrUpdate->addOrUpdate;
		if (ret=hogFileModifyDoAddOrUpdateInternal(handle, &mod))
			return NESTED_ERROR(hogJournalDoWorkErrors_ADD_OR_UPDATE, ret);
	}
	xcase HOJ_UPDATE_DEPRECATED:
	{
		assertmsg(0, "Corrupt hog file of older version, journal format not supported");
	}
	xcase HOJ_MOVE:
	{
		HFJMove *journal_move = (HFJMove *)journal;
		endianSwapStructIfBig(parseHFMMove, &journal_move->move);
		hoggMemLog( "0x%08p: Journal:Move: file_id:%d offset:%"FORM_LL"d", handle,
			journal_move->move.file_index, journal_move->move.new_offset);
		// Move operation finished (copying data), just update the header
		hogFillInFileModBasics(handle, &mod, -1);
		mod.type = HFM_MOVE;
		mod.move = journal_move->move;
		if (ret=hogFileModifyDoMoveInternal(handle, &mod))
			return NESTED_ERROR(hogJournalDoWorkErrors_MOVE, ret);
	}
	xcase HOJ_FILELIST_RESIZE:
	{
		HFJFileListResize *journal_filelist_resize = (HFJFileListResize *)journal;
		endianSwapStructIfBig(parseHFMFileListResize, &journal_filelist_resize->filelist_resize);
		hoggMemLog( "0x%08p: Journal:FileListResize: old ea pos:0x%x  new ea pos:0x%x  new ea size:%d", handle,
			journal_filelist_resize->filelist_resize.old_ealist_pos,
			journal_filelist_resize->filelist_resize.new_ealist_pos,
			journal_filelist_resize->filelist_resize.new_ealist_size);

		hogFillInFileModBasics(handle, &mod, -1);
		mod.type = HFM_FILELIST_RESIZE;
		mod.filelist_resize = journal_filelist_resize->filelist_resize;
		if (ret=hogFileModifyDoFileListResizeInternal(handle, &mod))
			return NESTED_ERROR(hogJournalDoWorkErrors_RESIZE, ret);
		handle->header.file_list_size = journal_filelist_resize->filelist_resize.new_filelist_size;
		handle->header.ea_list_size = journal_filelist_resize->filelist_resize.new_ealist_size;
	}
	xcase HOJ_DATALISTFLUSH:
	{
		HFJDataListFlush *journal_datalistflush = (HFJDataListFlush *)journal;
		endianSwapStructIfBig(parseHFMDataListFlush, &journal_datalistflush->datalistflush);
		hoggMemLog( "0x%08p: Journal:DataListFlush", handle);
		hogFillInFileModBasics(handle, &mod, -1);
		mod.type = HFM_DATALISTFLUSH;
		mod.datalistflush = journal_datalistflush->datalistflush;
		if (ret=hogFileModifyDoDataListFlushInternal(handle, &mod))
			return NESTED_ERROR(hogJournalDoWorkErrors_DATALISTFLUSH, ret);
	}
	xdefault:
		hogShowError(handle, 1, "Invalid item in journal", op);
		return 1;
	}

	// Now that it's been applied, need to remove it - if the terminator was still there, we could
	//  end up trying to apply a half-written journal for the next operation if it died mid-write.
	hogJournalReset(handle); 
	return 0;
}

static void hogFflush(HogFile *handle)
{
	if (handle->policy_flush)
		fflush(handle->file);
}

// Expects pre-endian swapped data
static int hogJournalJournal(HogFile *handle, void *data, U32 size)
{
	assert(!(handle->create_flags & HOG_SKIP_MUTEX)); // Shouldn't write to a hogg while this mode is on
	// Increment semaphore here so that if this process terminates, others will reload, process the
	//  journal, etc
	ENTER_CRITICAL(data_access);
	handle->version.value = hogIncVersion(handle);
	handle->version.soloValue++;
	LEAVE_CRITICAL(data_access);

	if (handle->policy_journal)
	{
		U64 offs = hogFileOffsetOfOpJournal(handle);
		int ret;
		U32 data_to_write;
		PERFINFO_AUTO_START("hogJournalJournal",1);

		hogFflush(handle);

		handle->last_journal_size = size;
		data_to_write = endianSwapIfBig(U32, size);
		if (ret=checkedWriteData(handle, &data_to_write, sizeof(U32), offs))
		{
			PERFINFO_AUTO_STOP();
			return NESTED_ERROR(1, ret);
		}
		offs += sizeof(U32);
		if (ret=checkedWriteData(handle, data, size, offs))
		{
			PERFINFO_AUTO_STOP();
			return NESTED_ERROR(2, ret);
		}
		offs += size;
		data_to_write = endianSwapIfBig(U32, HOG_JOURNAL_TERMINATOR);
		if (ret=checkedWriteData(handle, &data_to_write, sizeof(U32), offs))
		{
			PERFINFO_AUTO_STOP();
			return NESTED_ERROR(3, ret);
		}

		hogFflush(handle);
		
		PERFINFO_AUTO_STOP();
	}
	return 0;
}

static int hogJournalReset(HogFile *handle)
{
	if (handle->policy_journal)
	{
		U32 zeroes[HOG_OP_JOURNAL_SIZE]={0};
		U64 offs;
		int ret;
		if (!handle->last_journal_size)
			return 1; // Invalid call

		PERFINFO_AUTO_START("hogJournalReset",1);
		// Zero terminator
		offs = hogFileOffsetOfOpJournal(handle) + sizeof(U32) + handle->last_journal_size;
		if (ret=checkedWriteData(handle, zeroes, sizeof(U32), offs))
		{
			PERFINFO_AUTO_STOP();
			return NESTED_ERROR(1, ret);
		}
		offs = hogFileOffsetOfOpJournal(handle);
		if (ret=checkedWriteData(handle, zeroes, sizeof(U32) + handle->last_journal_size, offs))
		{
			PERFINFO_AUTO_STOP();
			return NESTED_ERROR(2, ret);
		}
		handle->last_journal_size = 0;

#if _XBOX
		if (strStartsWith(handle->filename, "cache:"))
		{
			// If we have a number of operations queued up, don't bother flushing!
			if (handle->async_operation_count < 5 ||
				handle->num_skipped_flushes > 100)
			{
				XFlushUtilityDrive();
				handle->num_skipped_flushes = 0;
			} else {
				handle->num_skipped_flushes++;
			}
		}
#endif

		PERFINFO_AUTO_STOP();
	}
	return 0;
}

static int hogDLRead(HogFile *handle, U8 *filedata, U32 filesize, U32 *datalist_bitfield, U32 datalist_bitfield_size, U8 *journal)
{
	DLJournalHeader *header = (DLJournalHeader *)journal;
	DataListJournal dljournal;
	int ret;
	IN_CRITICAL(data_access);
	endianSwapStructIfBig(parseDLJournalHeader, header);
	hoggMemLog( "0x%08p: DLJournal: inuse: %d old: %d new: %d", handle,
		header->inuse_flag, header->oldsize, header->size);
	if (header->inuse_flag) {
		// Didn't finish writing, use oldsize
		dljournal.size = header->oldsize;
	} else {
		// Did finish writing, use size (oldsize might be incorrect)
		dljournal.size = header->size;
	}
	dljournal.data = journal + sizeof(DLJournalHeader);

	if (1) // More memory efficient scheme, less efficient if tons of modifications are occurring
	{
		ret = DataListReadWithJournal(&handle->datalist, filedata, filesize, datalist_bitfield, datalist_bitfield_size, &dljournal, handle->err_level);
		if (ret == -1)
			return 2;
	} else {
		ret = DataListRead(&handle->datalist, filedata, filesize, datalist_bitfield, datalist_bitfield_size);
		if (ret==-1)
			return 3;
		// Apply journaled changes to DataList
		if (ret=DataListApplyJournal(&handle->datalist, &dljournal)) { // Endianness handled internally
			hoggMemLog( "0x%08p: DLJournal: failed to apply", handle);
			return NESTED_ERROR(1, ret);
		}
	}
	handle->datalist_diff_size = dljournal.size; // Don't overwrite the journal until it's flushed!
	return 0;
}

static int hogDLJournalSave(HogFile *handle, HogFileMod *mod)
{
	DLJournalHeader header;
	int ret;
	assert(mod->type == HFM_DATALISTDIFF);
	// First Append data
	if (ret=checkedWriteData(handle, mod->datalistdiff.data, mod->datalistdiff.size, mod->datalistdiff.offset))
		return NESTED_ERROR(1, ret);

	// Then safely update the size
#define SAVE_FIELD(fieldname) if (ret=checkedWriteData(handle, &header.fieldname, sizeof(header.fieldname), mod->datalistdiff.size_offset + offsetof(DLJournalHeader, fieldname))) return NESTED_ERROR(2, ret);
	header.inuse_flag = endianSwapIfBig(U32,1);
	SAVE_FIELD(inuse_flag);
	header.size = endianSwapIfBig(U32, mod->datalistdiff.newsize);
	SAVE_FIELD(size);
	header.inuse_flag = endianSwapIfBig(U32, 0);
	SAVE_FIELD(inuse_flag);
	header.oldsize = endianSwapIfBig(U32, mod->datalistdiff.newsize);
	SAVE_FIELD(oldsize);
#undef SAVE_FIELD
	return 0;
}

static HogFileIndex hogFileFindAndLockDA(HogFile *handle, const char *relpath) // Find an entry in a Hog, lock data_access
{
	HogFileIndex file_index=HOG_NO_VALUE;
	int count;

	ENTER_CRITICAL(data_access);
	if (handle->async_new_files) {
		while (stashFindInt(handle->async_new_files, relpath, &count)) {
			// This is a new file which is currently asynchronously being created,
			//   and does not yet have an index, stall until it's ready
			assert(handle->debug_in_data_access == 1); // Can't be called while the parent is in a critical, it will never complete!
			LEAVE_CRITICAL(data_access);
// #ifdef _FULLDEBUG
// 			// can't be in file-access critical either
// 			ENTER_CRITICAL(file_access);
// 			assert(handle->debug_in_file_access == 1);
// 			LEAVE_CRITICAL(file_access);
// #endif
			Sleep(0);
			ENTER_CRITICAL(data_access);
		}
	}
	if (!idFromName(handle,relpath,&file_index))
		file_index = HOG_NO_VALUE;
	return file_index;
}


HogFileIndex hogFileFind(HogFile *handle, const char *relpath) // Find an entry in a Hog
{
	HogFileIndex file_index;

	assert(isValidHogHandle(handle));

	hogFileCheckForModifications(handle);

	file_index=hogFileFindAndLockDA(handle, relpath);
	LEAVE_CRITICAL(data_access);
	return file_index;
}

void hogFileGetSizesInternal(HogFile *handle, HogFileIndex file_index, U32 *unpacked, U32 *packed)
{
	U32 pack_size=0, unpacked_size;
	HogFileListEntry *file_entry;

	IN_CRITICAL(data_access);

	assert(isValidHogHandle(handle));

	file_entry = hogFileWaitForFileData(handle, file_index, true, false);
	assert(file_entry);

	unpacked_size = file_entry->header.size;
	if (file_entry->header.headerdata.flagFFFE == 0xFFFE && 
		file_entry->header.headerdata.ea_id != HOG_NO_VALUE)
	{
		HogEAListEntry *ea_entry = hogGetEAListEntry(handle, (S32)file_entry->header.headerdata.ea_id);
		if (ea_entry->header.name_id != HOG_NO_VALUE) {
			if (ea_entry->header.unpacked_size) {
				pack_size = unpacked_size;
				unpacked_size = ea_entry->header.unpacked_size;
			}
		}
	}
	if (unpacked)
		*unpacked = unpacked_size;
	if (packed)
		*packed = pack_size;
}

void hogFileGetSizes(HogFile *handle, HogFileIndex file_index, U32 *unpacked, U32 *packed)
{
	ENTER_CRITICAL(data_access);
	hogFileGetSizesInternal(handle, file_index, unpacked, packed);
	LEAVE_CRITICAL(data_access);
}


U64 hogFileGetOffset(HogFile *handle, HogFileIndex file_index)
{
	HogFileListEntry *file_entry;
	U64 ret=0;
	assert(isValidHogHandle(handle));
	ENTER_CRITICAL(data_access);
	file_entry = hogFileWaitForFileData(handle, file_index, true, false);
	assert(file_entry);
	ret = file_entry->header.offset;
	LEAVE_CRITICAL(data_access);
	return ret;
}

S32 hogFileGetEAIDInternal(HogFile *handle, HogFileIndex file_index)
{
	HogFileListEntry *file_entry;
	S32 ret = HOG_NO_VALUE;;

	assert(isValidHogHandle(handle));
	ENTER_CRITICAL(data_access);
	file_entry = hogFileWaitForFileData(handle, file_index, true, false);
	assert(file_entry);

	if (file_entry->header.headerdata.flagFFFE == 0xFFFE)
		ret = file_entry->header.headerdata.ea_id;

	LEAVE_CRITICAL(data_access);
	return ret;
}

// Return the number of open handles for this file.
int hogFileGetSharedRefCount(HogFile *handle)
{
	int result;

	// Make sure this is a valid handle.
	assert(isValidHogHandle(handle));

	// Get the reference count.
	// Note that it is not possible for it to be zero, as that would mean it should have been freed.
	EnterCriticalSection(&g_hogSharedHandles.critical_section);
	result = handle->shared_reference_count;
	assert(result > 0);
	LeaveCriticalSection(&g_hogSharedHandles.critical_section);

	return result;
}

// Waits for it to be safe to read from a file (all writes are finished)
static HogFileListEntry *hogFileWaitForFile(HogFile *handle, HogFileIndex file, bool bNeedFileAccess, bool bNeedMutex)
{
	HogFileListEntry *file_entry;
	int ret;
	bool file_entry_dirty;
	if (bNeedMutex)
		hogAcquireMutex(handle); // No other process can be writing to this file!
	if (bNeedFileAccess)
		ENTER_CRITICAL(file_access);
	ENTER_CRITICAL(data_access);
	if (!handle->guaranteed_no_ops)
		assert(handle->debug_in_data_access == 1); // Have to be in exactly one deep otherwise we cannot release it
	do {
		file_entry = hogGetFileListEntrySafe(handle, file, false, false);
		if (!file_entry) {
			return NULL;
		}
		file_entry_dirty = file_entry->dirty;
		if (file_entry_dirty)
		{
			assert(!handle->guaranteed_no_ops);
			// This file is being worked on, flush changes before trying to read them!
			file_entry = NULL; // Not valid after leaving data_access
			LEAVE_CRITICAL(data_access);
			if (bNeedFileAccess)
				LEAVE_CRITICAL(file_access);
			if (ret=hogFileModifyFlush(handle)) {
				char msg[1024];
				sprintf(msg, "Failure flushing (error code: 0x%x)", ret);
				hogShowError(handle, ret, msg, 0);
			}
			if (bNeedFileAccess)
				ENTER_CRITICAL(file_access);
			ENTER_CRITICAL(data_access);
		}
	} while (file_entry_dirty);
	return file_entry;
}

// Waits for it to be safe to read the header data about a file (all async updates are finished)
static HogFileListEntry *hogFileWaitForFileData(HogFile *handle, HogFileIndex file, bool should_assert, bool flag_for_delete)
{
	HogFileListEntry *file_entry;
	U32 file_entry_dirty;
	bool bDidFlag=false;

	IN_CRITICAL(data_access);

	if (!handle->guaranteed_no_ops)
		assert(handle->debug_in_data_access == 1); // Have to be in exactly one deep otherwise we cannot release it

	do {
		file_entry = hogGetFileListEntrySafe(handle, file, should_assert, bDidFlag);
		if (!file_entry) {
			break;
		}

		if (flag_for_delete)
		{
			file_entry->queued_for_delete = 1; // Will cause any other callers to asserts if they try to access this file
			bDidFlag = true;
		}

		file_entry_dirty = file_entry->dirty_async;
		if (file_entry_dirty)
		{
			assert(!handle->guaranteed_no_ops);
			// This file has an updated queued for it
			file_entry = NULL; // Invalid after leaving critical, array could be resized
			LEAVE_CRITICAL(data_access);
			PERFINFO_AUTO_START("WaitForAsyncOp", 1);
			Sleep(1);
			PERFINFO_AUTO_STOP();
			ENTER_CRITICAL(data_access);
		}
	} while (file_entry_dirty);
	return file_entry;
}


// This is a helper function for hogFileExtractBytesCompressed.  It replaces a call to PigExtractBytesInternal,
// from which it's interior is cut-and-pasted.
static U32 extractRawInternal(FILE *file, void *buf, U32 pos, U32 size, U64 fileoffset)
{
	size_t numread;

	if (size==0) {
		return 0;
	}

	if (0!=fseek(file, fileoffset + pos, SEEK_SET)) {
		return 0;
	}

	hog_read_bytes_estimate += size;
	numread = fread(buf, 1, size, file);
	if (numread!=size)
		return 0;

	return (U32)numread;
}

// This is for the patcher.  The piglib version asserts that it won't work on hog files, so instead of
// attempting to fix that function, a new function shall be made that shamelessly copies hogFileExtractBytes.
// The function returns the file segment as it's stored in the hogg, compressed or uncompressed.
U32 hogFileExtractRawBytes(HogFile *handle, HogFileIndex file, void *buf, U32 pos, U32 size, bool haveOffset, U64 offset)
{
	HogFileListEntry * file_entry;
	U32 ret;
	bool bNeedFileAccess = !(handle->create_flags & HOG_MULTIPLE_READS);
	FILE *file_to_use;

	if(!haveOffset)
	{
		file_entry = hogFileWaitForFile(handle, file, bNeedFileAccess, true); // Enters data_access and file_access critical and mutex
		if (!file_entry)
		{
			LEAVE_CRITICAL(data_access);
			ret = 0;
			goto fail;
		}
		offset = file_entry->header.offset;
		file_entry = NULL;
	}
	else
	{
		hogAcquireMutex(handle); // No other process can be writing to this file!
		if (bNeedFileAccess)
			ENTER_CRITICAL(file_access);
		ENTER_CRITICAL(data_access);
	}

	if (handle->create_flags & HOG_MULTIPLE_READS)
	{
		if ( !(file_to_use = eaPop(&handle->multipleFiles)) )
		{
			file_to_use = fopen(handle->filename, handle->open_mode_string);
			assert(file_to_use);
		}
	} else {
		file_to_use = handle->file;
	}

	LEAVE_CRITICAL(data_access);

	ret = extractRawInternal(file_to_use, buf, pos, size, offset);

	if (handle->create_flags & HOG_MULTIPLE_READS)
	{
		ENTER_CRITICAL(data_access);
		assert(file_to_use != handle->file);
		eaPush(&handle->multipleFiles, file_to_use);
		LEAVE_CRITICAL(data_access);
	}
fail:
	if (bNeedFileAccess)
		LEAVE_CRITICAL(file_access);
	hogReleaseMutexAsync(handle, false);
	return ret;
}

// Continuing in the line of cut-and-paste functions for the patcher, hogFileExtractCompressed
// copies from hogFileExtract, except that it uses hogFileExtractRawBytes.
void *hogFileExtractCompressedEx(HogFile *handle, HogFileIndex file, U32 *count, U64 offset, U32 total, U32 pack_size, int special_heap)
{
	char *data=NULL;
	U32 numread;
	HogFileListEntry *file_entry;
	bool bLeftCrit=false;
	bool bNeedFileAccess = !(handle->create_flags & HOG_MULTIPLE_READS);
	bool haveInfo = !!total; // got offset/total/packed passed in

	if (!handle || !handle->file)
		return NULL;
	if (file==HOG_NO_VALUE)
		return NULL;

	assert(isValidHogHandle(handle));
	if(!haveInfo)
	{
		// Call this just to make the read atomic, no changes while we're reading
		file_entry = hogFileWaitForFile(handle, file, bNeedFileAccess, true); // Enters data_access and file_access critical and mutex
		if (!file_entry) {
			hogShowError(handle,14,"File disappeared while reading", file);
			*count = 0;
			goto fail;
		}

		hogFileGetSizesInternal(handle, file, &total, &pack_size);
	}
	else
	{
		hogAcquireMutex(handle); // No other process can be writing to this file!
		if (bNeedFileAccess)
			ENTER_CRITICAL(file_access);
		ENTER_CRITICAL(data_access);
	}

	if(!pack_size)
	{
		data = NULL;
		*count = 0;
		goto fail;
	}

	data = malloc_special_heap(pack_size + 1, special_heap); //Why the + 1?
	if (!data) {
		hogShowError(handle,15,"Failed to allocate memory",total+1);
		*count = 0;
		goto fail;
	}

	LEAVE_CRITICAL(data_access);
	bLeftCrit = true;

	numread = hogFileExtractRawBytes(handle, file, data, 0, pack_size, haveInfo, offset);

	if (bNeedFileAccess)
		LEAVE_CRITICAL(file_access);

	if (numread != pack_size)
	{
		SAFE_FREE(data);
		hogShowErrorWithFile(handle, file, 13,"Read wrong number of bytes",numread);
		*count = 0;
		goto fail;
	}
	*count = numread;

fail:
	if (!bLeftCrit) {
		LEAVE_CRITICAL(data_access);
		if (bNeedFileAccess)
			LEAVE_CRITICAL(file_access);
	}
	hogReleaseMutexAsync(handle, false);
	return data;
}

U32 hogFileExtractBytesEx(HogFile *handle, HogFileIndex file, void *buf, U32 pos, U32 bufsize, U64 file_offset, U32 unpacked_size, U32 pack_size, bool haveOffset)
{
	bool zipped=false;
	HogFileListEntry *file_entry;
	U32 ret;
	bool bNeedFileAccess = !(handle->create_flags & HOG_MULTIPLE_READS);
	FILE *file_to_use;

	assert(isValidHogHandle(handle));
	if(!haveOffset)
	{
		file_entry = hogFileWaitForFile(handle, file, bNeedFileAccess, true); // Enters data_access and file_access critical and mutex
		if (!file_entry) {
			hoggMemLog( "0x%08p: hogFileWaitForFile failed with no file_entry for index %d", handle, file);
			ret = 0;
			LEAVE_CRITICAL(data_access);
			if (bNeedFileAccess)
				LEAVE_CRITICAL(file_access);
			goto fail;
		}

		hogFileGetSizesInternal(handle, file, &unpacked_size, &pack_size);

		file_offset = file_entry->header.offset;
	}
	else
	{
		hogAcquireMutex(handle); // No other process can be writing to this file!
		if (bNeedFileAccess)
			ENTER_CRITICAL(file_access);
		ENTER_CRITICAL(data_access);
	}

	zipped = pack_size > 0;

	// Let got of the data critical section, but hang onto the file one, that should guarantee we have the correct offset
	//   without stalling queries that need the data critical section
	file_entry = NULL; // Invalid after this
	errorIsDuringDataLoadingInc(hogFileGetFileName(handle, file));
	file_to_use = handle->file;
	if (handle->create_flags & HOG_MULTIPLE_READS)
	{
		if ( !(file_to_use = eaPop(&handle->multipleFiles)) )
		{
			file_to_use = fopen(handle->filename, handle->open_mode_string);
			assert(file_to_use);
		}
	}

	LEAVE_CRITICAL(data_access);

	hog_read_bytes_estimate += pack_size?pack_size:unpacked_size;
	ret = PigExtractBytesInternal(handle, file_to_use, file, buf, pos, bufsize, file_offset, unpacked_size, pack_size);
	errorIsDuringDataLoadingDec();
	if (bNeedFileAccess)
		LEAVE_CRITICAL(file_access);

	if (handle->create_flags & HOG_MULTIPLE_READS)
	{
		ENTER_CRITICAL(data_access);
		assert(file_to_use != handle->file);
		eaPush(&handle->multipleFiles, file_to_use);
		LEAVE_CRITICAL(data_access);
	}


fail:
	hogReleaseMutexAsync(handle, false);
	return ret;
}

bool hogFileChecksumIsGood(HogFile *handle, int index)
{
	U32 count;
	bool ret;
	char * data;

	data = hogFileExtract(handle, index, &count, &ret);
	SAFE_FREE(data);

	return ret;
}

void *hogFileExtractEx(HogFile *handle, HogFileIndex file, U32 *count, bool * checksum_valid, U64 offset, U32 total, U32 packed, int special_heap)
{
	char *data=NULL;
	U32 numread;
	HogFileListEntry *file_entry;
	bool bLeftCrit = false;
	bool bNeedFileAccess = !(handle->create_flags & HOG_MULTIPLE_READS);
	bool haveInfo = !!total; // got offset/total passed in
	U32 expected_checksum=0;

	if (!handle || !handle->file)
		return NULL;
	if (file==HOG_NO_VALUE)
		return NULL;

	assert(isValidHogHandle(handle));

	if(checksum_valid)
		*checksum_valid = false;

	if(!haveInfo)
	{
		// Call this just to make the read atomic, no changes while we're reading
		file_entry = hogFileWaitForFile(handle, file, bNeedFileAccess, true); // Enters data_access and file_access critical and mutex
		if (!file_entry) {
			// Not critical?  File removed in another process probably
			// hogShowError(handle,14,"File disappeared while reading", file);
			hoggMemLog( "0x%08p: File disappeared while reading (index=%d)", handle,	file);
			*count = 0;
			goto fail;
		}

		hogFileGetSizesInternal(handle, file, &total, &packed);
		offset = file_entry->header.offset;
		if (checksum_valid)
			expected_checksum = file_entry->header.checksum;
		haveInfo = true;
	}
	else
	{
		hogAcquireMutex(handle); // No other process can be writing to this file!
		if (checksum_valid)
			expected_checksum = hogFileGetFileChecksum(handle, file);
		if (bNeedFileAccess)
			ENTER_CRITICAL(file_access);
		ENTER_CRITICAL(data_access);
	}

	data = malloc_special_heap(total + 1, special_heap);
	if (!data) {
		hogShowErrorWithFile(handle, file, 15,"Failed to allocate memory",total+1);
		*count = 0;
		goto fail;
	}

	bLeftCrit = true;
	LEAVE_CRITICAL(data_access);
	numread = hogFileExtractBytesEx(handle, file, data, 0, total, offset, total, packed, haveInfo);
	data[total] = 0; // For fileAlloc and others who might want to load a text file

	// Leave file_access so any waitforfile functions called below can wait without blocking the other threads
	if (bNeedFileAccess)
	{
		bNeedFileAccess = false;
		LEAVE_CRITICAL(file_access);
	}

	if (numread != total)
	{
		SAFE_FREE(data);
		hogShowErrorWithFile(handle, file, 13,"Read wrong number of bytes",numread);
		*count = 0;
		goto fail;
	}
	*count = numread;

	if(checksum_valid)
	{
		if(total > 0)
		{
			bool valid;
			U32 autocrc[4];
			U32 checksum;

			cryptMD5Update(data,total);
			cryptMD5Final(autocrc);
			checksum = autocrc[0];
			valid = *checksum_valid = (expected_checksum == checksum);

			if (!valid)
			{
				U32 new_checksum;
				// Checksum is bad, was this a disk error or what?  If it was zipped, it was already unzipped successfully!
				int ret;
				// First do a memory test on the buffer we read into before the page can change
				ret = memTestRange(data, *count);
				if (ret)
				{
					// g_genericlog will have output of prints which contain more details on what failed the memory test
					FatalErrorf("Memory inconsistency #1 detected.  Most likely cause is bad RAM.");
					// If we wanted to continue, leak data, it is bad, allocate a new buffer and read it in
					data = malloc_special_heap(total + 1, special_heap);
				}
				// If memory appears good, try a read again into the same buffer
				numread = hogFileExtractBytes(handle, file, data, 0, total);
				assert(numread == total); // It read it successfully above, shouldn't be going wrong now!
				cryptMD5Update(data,total);
				cryptMD5Final(autocrc);
				new_checksum = autocrc[0];
				if (new_checksum == expected_checksum || new_checksum != checksum)
				{
					// First read got data which did not match the expected CRC
					// Second read succeeded or failed in a different way
					// We got a bad disk read the first time?
					hogShowErrorWithFile(handle, file, 17, "Disk read error - two repeated reads returned different results.", 0);
					// The data is technically "good" now, but something is definitely wrong!
				} else {
					U8 *data2;
					// Both reads failed identically, probably bad on disk or bad destination
					// Try read again into different buffer
					data2 = malloc_special_heap(total + 1, special_heap);
					numread = hogFileExtractBytes(handle, file, data2, 0, total);
					assert(numread == total); // It read it successfully above, shouldn't be going wrong now!
					cryptMD5Update(data,total);
					cryptMD5Final(autocrc);
					new_checksum = autocrc[0];
					if (new_checksum == expected_checksum)
					{
						// Reading into new memory worked, probably bad memory at old destination,
						//  but not caught by memTestRange (this error rarely if ever shows up on ET)
						hogShowErrorWithFile(handle, file, 18, "Memory inconsistency #2 detected.  Most likely cause is bad RAM.", 0);
						// If we wanted to continue, leak data, it's bad, use data2
						data = data2;
						data2 = NULL;
					} else if (new_checksum != checksum) {
						// Reading into new memory failed in a new way, yet the last two didn't, WTF?
						hogShowErrorWithFile(handle, file, 19, "Memory inconsistency #3 detected.  Most likely cause is bad RAM.", 0);
						// both bad, leave them be, return the first bad one
					} else {
						// Reading into new memory failed in the same way, most likely bad on disk
						hogShowErrorWithFile(handle, file, 20, "File read from disk has bad CRC.  Verifying Files in the launcher may fix this issue.", 0);
					}
					SAFE_FREE(data2);
				}
			}
		}
		else
		{
			*checksum_valid = true;
		}
	}
fail:
	if (bNeedFileAccess)
		LEAVE_CRITICAL(file_access);
	if (!bLeftCrit) {
		LEAVE_CRITICAL(data_access);
	}
	hogReleaseMutexAsync(handle, false);
	return data;
}

static int cmpFileOffsets(const HogFileListEntry **a, const HogFileListEntry **b)
{
	S64 diff = (S64)(*a)->header.offset - (S64)(*b)->header.offset;
	return diff>0?1:(diff<0?-1:0);
}


static void hogFileBuildFreeSpaceList(HogFile *handle, int **eaiFilesToPrune) // Additionally sets min/max size
{
	U64 data_offset=hogFileOffsetOfData(handle);
	U32 i;

	IN_CRITICAL(data_access);
	hfst2Destroy(handle->free_space2);
	handle->free_space2 = hfst2Create();
	hfst2SetStartLocation(handle->free_space2, data_offset);
	hfst2SetAppendOnly(handle->free_space2, !!(handle->create_flags & HOG_APPEND_ONLY));

	for (i=0; i<handle->filelist_count; i++)
	{
		HogFileListEntry *file_entry = &handle->filelist[i];
		if (file_entry->in_use)
		{
			if (file_entry->header.size)
			{
				if (!hfst2AllocSpace(handle->free_space2, file_entry->header.offset, file_entry->header.size))
				{
					// Need to delete both this file and the file(s) it overlaps with!
					U32 otherFileIndex;
					bool bFoundOne=false;
					eaiPushUnique(eaiFilesToPrune, i);
					hoggMemLog( "0x%08p: Found file overlapping other file(s) #%d (offs: %"FORM_LL"d size: %d)", handle,
						i, file_entry->header.offset, file_entry->header.size);
					for (otherFileIndex=0; otherFileIndex<handle->filelist_count; otherFileIndex++)
					{
						HogFileListEntry *other_file_entry;
						if (otherFileIndex == i)
							continue;
						other_file_entry = &handle->filelist[otherFileIndex];
						if (other_file_entry->in_use && other_file_entry->header.size)
						{
							U64 other_end = other_file_entry->header.offset  + other_file_entry->header.size;
							U64 my_end = file_entry->header.offset + file_entry->header.size;
							if (other_file_entry->header.offset <= file_entry->header.offset &&
									other_end > file_entry->header.offset ||
								file_entry->header.offset <= other_file_entry->header.offset &&
									my_end > other_file_entry->header.offset)
							{
								bFoundOne = true;
								eaiPushUnique(eaiFilesToPrune, otherFileIndex);
								hoggMemLog( "0x%08p:   Overlapped file #%d (offs: %"FORM_LL"d size: %d)", handle,
									otherFileIndex, other_file_entry->header.offset, other_file_entry->header.size);
							}
						}
					}
					assert(bFoundOne);
				}
			}
		}
	}
	hfst2DoneAllocingSpace(handle->free_space2);
}

int num_fileheaders,num_eaheaders,num_stringnames,num_precache;
int num_unused_eaheaders,num_unused_fileheaders;

enum
{
	// Only add to the end of this
	hogFileReadError_EOF = 1,
	hogFileReadError_DoesNotExist,
	hogFileReadError_NotWritable,
	hogFileReadError_Opening,
	hogFileReadError_Statting,
	hogFileReadError_TooSmall,
	hogFileReadError_BadVersion,
	hogFileReadError_TooLargeOpJournal,
	hogFileReadError_InvalidHeader,
	hogFileReadError_InvalidEAID,
	hogFileReadError_InvalidEAID2,
	hogFileReadError_FilePastEOF,
	hogFileReadError_OpJournal, // 13, nests sub-error
	hogFileReadError_Pruning, // 14, nests sub-error
	hogFileReadError_RESERVED, // 15 anything else that needs nesting
	hogFileReadError_FilePastBOF,
	hogFileReadError_NoDataList,
	hogFileReadError_MissingDataList,
	hogFileReadError_CorruptDataList,
	hogFileReadError_DupEAID,
	hogFileReadError_CorruptFileEntry,
	hogFileReadError_MissingName,
	hogFileReadError_CorruptDataList2,
	hogFileReadError_InvalidFilenameIndex,
	hogFileReadError_InvalidHeaderDataIndex,
};
STATIC_ASSERT(hogFileReadError_OpJournal == 13);

// If file_data and file_data_size are provided, they are assumed to be the contents of filename, and the file is not actually
// opened.
static int hogFileReadInternal(HogFile *handle, const char *filename) // Load a .HOGG file
{
	U32 *used_list=NULL; // [eaid]s
	U32 *used_name_list=NULL; // [nameid]s
	int numfiles, numeas;
	int freecount=0;
	int i;
	char fn[CRYPTIC_MAX_PATH];
	char fn_orig[CRYPTIC_MAX_PATH];
	struct _stat64 status = {0};
	int err_code=0;
	int err_int_value=-1;
	int ret;
	U8 op_journal[HOG_OP_JOURNAL_SIZE];
	U8 *dl_journal = NULL;
	U8 *buffer=NULL;
	int *eaiFilesToPrune=NULL;
	int num_named_files;
	U32 *datalist_bitfield=NULL;
	U32 datalist_bitfield_size=0;
	U64 data_offset;
#define FAIL_SILENT(num, str) { hoggMemLog( "0x%08p: FAIL_SILENT:%s", handle, str); err_code = num; goto fail; }
#define FAIL(num,str) {  hoggMemLog( "0x%08p: FAIL:%s", handle, str); err_code = hogShowError(handle,num,str,err_int_value); goto fail; }
#define FAIL_SAFE(num, str) {  hoggMemLog( "0x%08p: FAIL_SAFE:%s", handle, str); err_code = hogShowError(handle,num,str,err_int_value); if (handle->create_flags & HOG_NO_REPAIR) goto fail; err_code = 0; } // Asserts or errorfs, but we handle it
#define CHECKED_FREAD(buf, count) if (count != (U32)fread(buf, 1, count, handle->file)) FAIL(hogFileReadError_EOF, "Unexpected end of file");

	//assertNotSharedHandleName(handle, filename); hogFileReload would trigger this, even though it's valid
	assert(!handle->file && "Cannot reuse a handle without calling hogFileDestroy() first!");
	assert(!handle->num_files);

	ENTER_CRITICAL(file_access);
	ENTER_CRITICAL(data_access);

	if (!handle->resource_id && !fileExistsEx(filename, false)) {
		FAIL_SILENT(hogFileReadError_DoesNotExist, "File does not exist");
	}
	strcpy(fn_orig, filename);
	hogFilenameForMutexName(filename, SAFESTR(fn));
	SAFE_FREE(handle->filename);
	handle->filename = strdup(fn);
#if HOGG_WATCH_TIMESTAMPS
	handle->filename_for_timestamp = strdup(fn);
#endif
	if (!(handle->create_flags & HOG_NO_MUTEX))
	{
		handle->mutex.name = makeMutexName(fn, "");
		handle->version.name = makeMutexName(fn, "Ver");
		handle->version.semaphore = CreateSemaphore_UTF8(NULL, 1, LONG_MAX, handle->version.name);
		devassertmsg(handle->version.semaphore, "Failed to open semaphore object");
	}
	hoggMemLog( "0x%08p: Loading %s", handle,	fn);
	hogAcquireMutex(handle);

	// Open file read/write
#if _XBOX
	if (!strStartsWith(handle->filename, "net:/smb")) // All Xbox<->Samba hoggs read-only
#endif
	{
		if (!(handle->create_flags & HOG_READONLY))
		{
			if (g_hogOpenMode == HogSafeAgainstOSCrash) {
				handle->open_mode_string = "r+cb~";
			} else {
				handle->open_mode_string = "r+b~";
			}
			if (handle->create_flags & HOG_RAM_CACHED)
			{
				handle->file = fileOpenRAMCached(handle->filename, handle->open_mode_string);
			} else {
				handle->file = fileOpen(handle->filename, handle->open_mode_string);
			}
		}
	}
	if (!handle->file) {
		if (handle->create_flags & HOG_MUST_BE_WRITABLE)
		{
			FAIL(hogFileReadError_NotWritable, "Failed to open file as writable");
		}

		// Get read filename.
		if (!handle->resource_id)
		{
			SAFE_FREE(handle->filename);
			fileLocateRead(fn_orig, fn);
			handle->filename = strdup(fn_orig);
		}

		// Open file read-only
		// *Not* updating filename_for_timestamp
		handle->read_only = true;
		handle->open_mode_string = "rb";
		if (handle->resource_id)
		{
			void *resource_buffer;
			size_t resource_buffer_size;
			resource_buffer = allocResourceById("HOGG", handle->resource_id, &resource_buffer_size);
			if (resource_buffer)
			{
				handle->file = fileOpenRAMCachedPreallocated(resource_buffer, resource_buffer_size);
				if (handle->file)
					handle->file_size = resource_buffer_size;
			}
		}
		else if (handle->create_flags & HOG_RAM_CACHED)
		{
			handle->file = fileOpenRAMCached(fn, handle->open_mode_string);
		} else {
			handle->file = fopen(fn, handle->open_mode_string);
		}
	}
	if (!handle->file) {
		FAIL(hogFileReadError_Opening,"Error opening hog file");
	}
    assert(handle->file->iomode == IO_WINIO||
		handle->file->iomode==IO_NETFILE||
		handle->read_only||
		handle->file->iomode==IO_RAMCACHED); // Otherwise HogSafeAgainstAppCrash is a lie (we need to do flushes)
	// RAMCACHED is only actually safe it the underlying one is also IO_WINIO

#if _XBOX
	backSlashes(fn);
#endif

	if (handle->resource_id)
		{}	// We already have the size for resource-backed hogs.
	else if(!_stat64(fn, &status)){
		if(!(status.st_mode & _S_IFREG)) {
			FAIL_SILENT(hogFileReadError_Statting,"Error statting hog file");
		}
	} else {
		if (handle->read_only) {
			status.st_size = fileSize(handle->filename);
			if (status.st_size <= 0)
			{
				S64 s64 = fileSize64(fn);
				if (s64 > 0)
					status.st_size = s64; // net:/cryptic/ files
				else
					FAIL_SILENT(hogFileReadError_Statting,"Error statting hog file");
			}
		} else {
			S64 s64 = fileSize64(fn);
			if (s64 > 0)
				status.st_size = s64; // net:/cryptic/ files
			else
				FAIL_SILENT(hogFileReadError_Statting,"Error statting hog file");
		}
	}
#if HOGG_WATCH_TIMESTAMPS
	handle->file_last_changed = fileCacheGetTimestamp(handle->filename_for_timestamp);
	handle->file_cache_is_monitoring = 1;
#endif
	handle->version.value = hogGetVersion(handle);
	handle->version.soloValue = 1; // Starts at 1 if we're a single process, if it's more, someone else already touched this hogg

	if (!handle->resource_id)
		handle->file_size = status.st_size;

	hoggMemLog( "0x%08p: File size: %"FORM_LL"d", handle, handle->file_size);

	if (handle->file_size < 24) {
		// Too small to be valid (probably 0-byte file)
		fileForceRemove(fn);
		FAIL_SILENT(hogFileReadError_TooSmall, "File too small");
	}

	// Read header
	memset(&handle->header, 0, sizeof(handle->header));
	STATIC_INFUNC_ASSERT(sizeof(handle->header)==24); // Changed size of HogHeader but didn't update reading code to read the extra values
	CHECKED_FREAD(&handle->header, 24);
	endianSwapStructIfBig(parseHogHeader, &handle->header);
	if (handle->header.version < 10 || handle->header.version > HOG_VERSION) {
		err_int_value = handle->header.version;
		FAIL(hogFileReadError_BadVersion,"Hog file incorrect/unreadable version");
	}
	if (handle->header.op_journal_size > HOG_OP_JOURNAL_SIZE) {
		err_int_value = handle->header.op_journal_size;
		FAIL(hogFileReadError_TooLargeOpJournal,"Hog file has too large of op journal to read");
	}
// 	if (handle->header.dl_journal_size == 3096) { // Old default
// 		handle->need_upgrade = true;
// 	}
	if (handle->header.version == 10)
		devassert(handle->header.dl_journal_size < (1<<16)); // Was only 16 bits in this version
	if (handle->header.hog_header_flag != HOG_HEADER_FLAG) {
		err_int_value = handle->header.hog_header_flag;
		FAIL(hogFileReadError_InvalidHeader,"Hog file has invalid header flag");
	}

	// read operation journal
	CHECKED_FREAD(op_journal, handle->header.op_journal_size);
	endianSwapStructIfBig(parseHogOpJournalHeader, op_journal);

	if (hogJournalHasWork(handle, op_journal)) {
		hoggMemLog( "0x%08p: Has OP Journal", handle);
		err_code = hogJournalDoWork(handle, op_journal); // Do work that needs to be done before continuing
		if (err_code)
			FAIL(NESTED_ERROR(hogFileReadError_OpJournal, err_code), "Failed to recover from op journal");
	}

	fseek(handle->file, hogFileOffsetOfDLJournal(handle), SEEK_SET);
	// read DL journal
	dl_journal = ScratchAlloc(handle->header.dl_journal_size);
	CHECKED_FREAD(dl_journal, handle->header.dl_journal_size);

	bigDynArrayFitStructs(&handle->filelist, &handle->filelist_max, handle->header.file_list_size / sizeof(HogFileHeader));
	bigDynArrayFitStructs(&handle->ealist, &handle->ealist_max, handle->header.ea_list_size / sizeof(HogEAHeader));
	num_fileheaders += handle->header.file_list_size / sizeof(HogFileHeader);
	num_eaheaders += handle->header.ea_list_size / sizeof(HogEAHeader);
	numfiles = handle->header.file_list_size / sizeof(HogFileHeader);
	handle->filelist_count = numfiles;
	numeas = handle->header.ea_list_size / sizeof(HogEAHeader);
	handle->ealist_count = numeas;
	data_offset=hogFileOffsetOfData(handle);

	// Read FileHeaders
	buffer = malloc(MAX(handle->header.file_list_size, handle->header.ea_list_size));
	CHECKED_FREAD(buffer, handle->header.file_list_size);

	// Fill in HogFileHeader list
	// Filling freelist in 2 passes for slight performance improvement (saves lots of function calls on mostly empty databases)
	freecount = 0;
	for (i=0; i<numfiles; i++)
	{
		HogFileHeader *hfh = &((HogFileHeader*)buffer)[i];
		HogFileListEntry *file_entry = &handle->filelist[i];
		endianSwapHogFileHeaderIfBig(hfh);
		if (hfh->size == HOG_NO_VALUE) {
			file_entry->in_use = 0;
			freecount++;
			num_unused_fileheaders++;
		} else {
			file_entry->in_use = 1;
			handle->num_files++;
			file_entry->header = *hfh;
		}
	}

	// Leave extra capacity so the first delete doesn't stall and fragment memory
	eaiSetCapacity(&handle->file_free_list, freecount + 256);
	eaiSetSize(&handle->file_free_list, freecount);
	for (i=0; i<numfiles && freecount; i++)
	{
		if (!handle->filelist[i].in_use) {
			freecount--;
			handle->file_free_list[freecount] = i+1;
		}
	}
	assert(freecount == 0);

	// Read EA Headers
	CHECKED_FREAD(buffer, handle->header.ea_list_size);

	// Fill in the HogEAHeader list
	// Filling freelist in 2 passes for slight performance improvement (saves lots of function calls on mostly empty databases)
	freecount = 0;
	for (i=0; i<numeas; i++)
	{
		HogEAHeader *hfh = &((HogEAHeader*)buffer)[i];
		HogEAListEntry *ea_entry = &handle->ealist[i];
		endianSwapHogEAHeaderIfBig(hfh);
		if (hfh->flags & HOGEA_NOT_IN_USE) {
			ea_entry->header.flags = HOGEA_NOT_IN_USE;
			freecount++;
			num_unused_eaheaders++;
		} else {
			//ea_entry->in_use = 1;
			ea_entry->header = *hfh;
			if (ea_entry->header.name_id != HOG_NO_VALUE)
				num_stringnames++;
			if (ea_entry->header.header_data_id != HOG_NO_VALUE)
				num_precache++;
		}
	}

	// Leave extra capacity so the first delete doesn't stall and fragment memory
	eaiSetCapacity(&handle->ea_free_list, freecount + 256);
	eaiSetSize(&handle->ea_free_list, freecount);
	for (i=0; i<numeas && freecount; i++)
	{
		if (!EA_IN_USE_STRUCT(handle->ealist[i])) {
			freecount--;
			handle->ea_free_list[freecount] = i+1;
		}
	}
	assert(freecount == 0);

	// Sort lists to use lower file numbers first (put them at the end of the list)
	//ea32QSort(handle->file_free_list, intRevCompare);
	//ea32QSort(handle->ea_free_list, intRevCompare);
	// Should be already sorted!
	assert(eaiSize(&handle->file_free_list)<2 || handle->file_free_list[0] > handle->file_free_list[1]);
	assert(eaiSize(&handle->ea_free_list)<2 || handle->ea_free_list[0] > handle->ea_free_list[1]);

	// Verify FileHeader integrity
	//  Also count number of names, and build bit field
	//  describing what data list entries should go directly to the
	//  StringCache
	num_named_files=0;
	if (!(handle->create_flags & HOG_NO_STRING_CACHE))
	{
		datalist_bitfield_size = numfiles * 2;
		datalist_bitfield = calloc((datalist_bitfield_size + 31) >> 5, 4);
	}
	for (i=0; i<numfiles; i++) {
		HogFileListEntry *file_entry = &handle->filelist[i];
		HogEAListEntry *ea_entry=NULL;
		U64 entry_size;
		if (!file_entry->in_use)
			continue;
		if (file_entry->header.headerdata.flagFFFE == 0xFFFE) {
			S32 ea_id = (S32)file_entry->header.headerdata.ea_id;
			if (ea_id != HOG_NO_VALUE) {
				if (ea_id < 0 || ea_id >= (S32)handle->ealist_count) {
					hoggMemLog( "0x%08p: Pruning corrupt file (invalid ea_id) #%d", handle, i);
					err_int_value = ea_id;
					FAIL_SAFE(hogFileReadError_InvalidEAID, "Invalid ea_id ");
					printf("%s: Pruning corrupt file (invalid ea_id) #%d\n", handle->filename, i);

					file_entry->header.headerdata.ea_id = HOG_NO_VALUE;
					eaiPushUnique(&eaiFilesToPrune, i);
				} else {
					ea_entry = &handle->ealist[ea_id];
					if (!EA_IN_USE(ea_entry)) {
						hoggMemLog( "0x%08p: Pruning corrupt file (invalid ea_id) #%d", handle, i);
						err_int_value = ea_id;
						FAIL_SAFE(hogFileReadError_InvalidEAID2, "Invalid ea_id ");
						printf("%s: Pruning corrupt file (invalid ea_id) #%d\n", handle->filename, i);

						file_entry->header.headerdata.ea_id = HOG_NO_VALUE;
						eaiPushUnique(&eaiFilesToPrune, i);

					} else if (ea_entry->header.name_id != HOG_NO_VALUE) {
						if (ea_entry->header.name_id < datalist_bitfield_size)
							SETB(datalist_bitfield, ea_entry->header.name_id);
						num_named_files++;
					}
				}
			}
		}
		entry_size = file_entry->header.size;
		if (entry_size && (file_entry->header.offset + file_entry->header.size - 1 > handle->file_size) && !hog_mode_no_data)
		{
			err_int_value = i;
			if ((U32)i == handle->header.datalist_fileno)
			{
				FAIL(hogFileReadError_FilePastEOF,"Fileheader extends past end of .hogg, probably corrupt .hogg file!  #");
			} else {
				FAIL_SAFE(hogFileReadError_FilePastEOF,"Fileheader extends past end of .hogg, probably corrupt .hogg file!  #");
			}
			// Set values so it doesn't get into free space list, and then remove it later
			if (!(handle->create_flags & HOG_READONLY))
			{
				file_entry->header.offset = 0;
				entry_size = file_entry->header.size = 0;
				eaiPushUnique(&eaiFilesToPrune, i);
			}
		}
		if (entry_size && (file_entry->header.offset < data_offset)) {
			err_int_value = i;
			FAIL(hogFileReadError_FilePastBOF,"Fileheader extends past beginning of data portion of .hogg, probably corrupt .hogg file!  #");
			// TODO: Fix (delete file (add to eaiFilesToPrune?)?), not error
			// Probably just want to delete this whole .hogg file, something is messed up more than just an OS crash could account for.
		}
	}

	// Read ?DataList
	if (handle->header.datalist_fileno==HOG_NO_VALUE) {
		FAIL(hogFileReadError_NoDataList, "No DataList file specified in Hogg file");
	}
	{
		U32 filesize;
		U8 *filedata;
		LEAVE_CRITICAL(data_access);
		filedata = hogFileExtract(handle, handle->header.datalist_fileno, &filesize, NULL);
		ENTER_CRITICAL(data_access);
		if (!filedata) {
			// Corrupt datalist!  Total failure?
			if (handle->create_flags & (HOG_READONLY))
			{
				FAIL_SAFE(hogFileReadError_MissingDataList, "Missing DataList in Hogg file");
			} else {
				FAIL(hogFileReadError_MissingDataList, "Missing DataList in Hogg file");
			}
		} else {
			ret = hogDLRead(handle, filedata, filesize, datalist_bitfield, datalist_bitfield_size, dl_journal);
			free(filedata);
			if (ret) {
				// Corrupt datalist!  Total failure?
				FAIL(hogFileReadError_CorruptDataList, "Corrupt DataList in Hogg file");
			}
		}
	}

	SAFE_FREE(datalist_bitfield);

	// Prune orphaned EAList entries, build lookup table
	{
		int debug_throttle=0;
		assert(!handle->fn_to_index_lookup);
		if (!(handle->create_flags & HOG_NO_ACCESS_BY_NAME))
		{
			handle->fn_to_index_lookup = stashTableCreateWithStringKeys(num_named_files*14/10, StashDefault);
		}
		assert(!handle->async_new_files);
		if (!handle->read_only)
			handle->async_new_files = stashTableCreateWithStringKeys(64, StashDefault);

		used_list = calloc(sizeof(used_list[0]), handle->ealist_count);
		used_name_list = calloc(sizeof(used_name_list[0]), DataListGetNumTotalEntries(&handle->datalist));
		for (i=0; i<numfiles; i++)
		{
			HogFileListEntry *file_entry = &handle->filelist[i];
			if (!file_entry->in_use)
				continue;
			if (file_entry->header.headerdata.flagFFFE == 0xFFFE) {
				S32 ea_id = (S32)file_entry->header.headerdata.ea_id;
				if (ea_id != HOG_NO_VALUE) {
					if (ea_id < 0 || ea_id >= (S32)handle->ealist_count) {
						// Errored above already
					} else {
						HogEAListEntry *ea_entry = &handle->ealist[ea_id];
						if (!EA_IN_USE(ea_entry)) {
							// Errored above already
						} else {
							U32 name_id = ea_entry->header.name_id;
							U32 header_data_id = ea_entry->header.header_data_id;
							bool bCorrupt = false;
							if (header_data_id != HOG_NO_VALUE) {
								if (!DataListGetData(&handle->datalist, header_data_id, NULL)) {
									// Corrupt!
									hoggMemLog( "0x%08p: File with bad header data: file %d header_data_id %d", handle, i, header_data_id);
									bCorrupt = true;
								}
							}
							if (name_id != HOG_NO_VALUE) {
								const char *stored_name = DataListGetString(&handle->datalist, name_id, !(handle->create_flags & HOG_NO_STRING_CACHE));
								if (!stored_name) {
									// This ea_entry and file_entry are effectively corrupt!
									// delete the file!
									hoggMemLog( "0x%08p: File with bad stored name: file %d name_id %d", handle, i, name_id);
									bCorrupt = true;
								} else {
									if (used_name_list[name_id])
									{
										// Two files sharing the same name, remove this one
										hoggMemLog( "0x%08p: File referencing the same name_id: file %d name_id %d conflicted_file_id %d name %s", handle, i, name_id, used_name_list[name_id]-1, stored_name);
										bCorrupt = true;
									} else {
										used_name_list[name_id] = i+1;
										if (!(handle->create_flags & HOG_NO_ACCESS_BY_NAME))
										{
											if (!storeNameToId(handle,stored_name,i))
											{
												hoggMemLog( "0x%08p: File with duplicate name: file %d name %s", handle, i, stored_name);
												bCorrupt = true;
												// This data list entry should get pruned automatically below when no file references it
											}
										}
									}
								}
							}
							if (used_list[ea_id])
							{
								// Two files sharing the same ea_id, delete them both!
								hoggMemLog( "0x%08p: Pruning corrupt files (two files referencing the same ea_id #%d) #%d and #%d", handle, ea_id, i, used_list[ea_id]-1);
								err_int_value = ea_id;
								FAIL_SAFE(hogFileReadError_DupEAID, "Two files referencing the same ea_id");
								printf("Pruning corrupt files (two files referencing the same ea_id #%d) #%d and #%d\n", ea_id, i, used_list[ea_id]-1);

								eaiPushUnique(&eaiFilesToPrune, i);
								eaiPushUnique(&eaiFilesToPrune, used_list[ea_id]-1);
								if (!(handle->create_flags & HOG_READONLY))
								{
									// Because we're going to be deleting both files, and we only want to delete the ea_entry once, clear out this reference to it.
									file_entry->header.headerdata.ea_id = HOG_NO_VALUE;
								}
							}
							if (bCorrupt) {
								if (1) {
									hoggMemLog( "0x%08p: Pruning corrupt file (missing name or header_data or duplicate files) file %d  ea_id %d", handle, i, ea_id);
									err_int_value = i;
									FAIL_SAFE(hogFileReadError_CorruptFileEntry, "Corrupt file missing name or header_data or duplicate files");
									printf("Pruning corrupt file (missing name or header_data or duplicate files) #%d\n", i);

									eaiPushUnique(&eaiFilesToPrune, i);
									if (!used_list[ea_id])
										used_list[ea_id] = i+1; // Don't let it get pruned yet!
								} else {
									FAIL(hogFileReadError_MissingName, "Corrupt file - missing name");
								}
							} else {
								if (!used_list[ea_id])
									used_list[ea_id] = i+1;
							}
						}
					}
				}
			}
		}
		for (i=numeas-1; i>0; i--) // Back to front to keep free_list roughly sorted 
		{
			HogEAListEntry *ea_entry = &handle->ealist[i];
			if (!used_list[i]) {
				if (!EA_IN_USE(ea_entry))
					continue;
				hoggMemLog( "0x%08p: Pruning unused ea list item #%d", handle, i);
				// Extra ea_entry, but not referenced by anything!
				// I think this can happen during "normal" usage (e.g. a crash while doing a write)
				//ea_entry->in_use = 0;
				ea_entry->header.flags = HOGEA_NOT_IN_USE;
				if (debug_throttle < 1000000) // In the worst case, don't spend years doing this assert
				{
					assert(-1==eaiFind(&handle->ea_free_list, i+1)); // Logic error - shouldn't be in the list here!
					debug_throttle += eaiSize(&handle->ea_free_list);
				}
				eaiPush(&handle->ea_free_list, i+1);
			}
		}
	}

	// Verify ?DataList is named appropriately
	if (handle->header.datalist_fileno != HOG_NO_VALUE)
	{
		const char *name = hogFileGetFileName(handle, handle->header.datalist_fileno);
		if (!name || stricmp(name, HOG_DATALIST_FILENAME)!=0)
		{
			if (!name) {
				// DataList must be corrupt, it is not there at all
				FAIL(hogFileReadError_CorruptDataList2, "Corrupt " HOG_DATALIST_FILENAME " entry in Hogg file");
			} else {
				// Header must be corrupt, the file has no name or it points to the wrong file
				if (handle->create_flags & HOG_READONLY) {
					FAIL_SAFE(hogFileReadError_CorruptDataList2, "Corrupt " HOG_DATALIST_FILENAME " entry in Hogg file");
				} else {
					FAIL(hogFileReadError_CorruptDataList2, "Corrupt " HOG_DATALIST_FILENAME " entry in Hogg file");
				}
			}
		}
	}

	// Build freelist before anything that may need to make modifications
	if (!handle->read_only)
		hogFileBuildFreeSpaceList(handle, &eaiFilesToPrune); // Additionally sets min/max size

	// Now, do any verification that may need to make modifications

	if (!handle->read_only && eaiSize(&eaiFilesToPrune))
	{
		bool no_ops_save = handle->guaranteed_no_ops;

		if (handle->create_flags & HOG_RAM_CACHED)
			hogFileFreeRAMCache(handle); // If we're RAM-cached, we've gotta undo that to apply this change

		handle->guaranteed_no_ops = true; // Can't have any async ops queued up here
		// Remove files previously marked as corrupt
		for (i=eaiSize(&eaiFilesToPrune)-1; i>=0; i--)
		{
			int j;
			int file_index = eaiFilesToPrune[i];
			HogFileListEntry *file_entry = &handle->filelist[file_index];
			assert(file_entry->in_use); // Internal logic error?
			// We're deleting a corrupt file
			// If any other files reference this file's datalist_ids,
			//  we should at least not remove those datalist entries (although
			//  we should probably additionally remove those files as they may
			//  be corrupt).
			if (file_entry->header.headerdata.flagFFFE == 0xFFFE) {
				S32 ea_id = (S32)file_entry->header.headerdata.ea_id;
				if (ea_id != HOG_NO_VALUE) {
					HogEAListEntry *ea_entry = &handle->ealist[ea_id];
					U32 header_data_id = ea_entry->header.header_data_id;
					U32 name_id = ea_entry->header.name_id;
					bool clear_name_id = false, clear_header_data_id = false;
					assert(EA_IN_USE(ea_entry)); // Set/verified above
					for (j=0; j<numeas; j++) {
						if (j!=ea_id) {
							if (EA_IN_USE_STRUCT(handle->ealist[j])) {
								if (header_data_id != HOG_NO_VALUE) {
									if (handle->ealist[j].header.header_data_id == header_data_id)
										clear_header_data_id = true;
									if (handle->ealist[j].header.name_id == header_data_id)
										clear_header_data_id = true;
								}
								if (name_id != HOG_NO_VALUE) {
									if (handle->ealist[j].header.header_data_id == name_id)
										clear_name_id = true;
									if (handle->ealist[j].header.name_id == name_id)
										clear_name_id = true;
								}
							}
						}
					}
					if (clear_name_id) {
						// TODO: Also delete conflicted file(s) after all checks?
						ea_entry->header.name_id = HOG_NO_VALUE;
					}
					if (clear_header_data_id) {
						// TODO: Also delete conflicted file(s) after all checks?
						ea_entry->header.header_data_id = HOG_NO_VALUE;
					}
				}
			}

			handle->doing_delete_during_load_or_repair = 1; // We're going to rebuild it shortly, and it might currently be corrupt
			hogFileModifyDelete(handle, file_index);
			handle->doing_delete_during_load_or_repair = 0;
		}

		// Re-build free space, because it may have been corrupt to start with
		eaiDestroy(&eaiFilesToPrune);
		// Leave criticals so flush can work
		LEAVE_CRITICAL(file_access);
		LEAVE_CRITICAL(data_access);
		hogFileModifyFlush(handle); // Deletes don't update the header until they're through the background, gotta flush the delets first!
		ENTER_CRITICAL(file_access);
		ENTER_CRITICAL(data_access);
		hogFileBuildFreeSpaceList(handle, &eaiFilesToPrune); // Additionally sets min/max size
		assert(eaiSize(&eaiFilesToPrune)==0);

		handle->guaranteed_no_ops = no_ops_save;
	}

	// Verify ea_list integrity (name IDs and HeaderIDs)
	// Prune orphaned DataList entries (adds to journal, may cause a DataListFlush) (for recovery from some ops (Delete, Add))
	{
		int num_datalist_entries = DataListGetNumTotalEntries(&handle->datalist);
		used_list = realloc(used_list, sizeof(used_list[0])*num_datalist_entries);
		memset(used_list, 0, sizeof(used_list[0])*num_datalist_entries);
		for (i=0; i<numeas; i++)
		{
			HogEAListEntry *ea_entry = &handle->ealist[i];
			const U8 *data;
			U32 size;
			if (!EA_IN_USE(ea_entry))
				continue;
			if (ea_entry->header.name_id != HOG_NO_VALUE) {
				data = DataListGetData(&handle->datalist, ea_entry->header.name_id, &size);
				if (!data) {
					err_int_value = ea_entry->header.name_id;
					FAIL_SAFE(hogFileReadError_InvalidFilenameIndex, "File with invalid filename index");
					ea_entry->header.name_id = HOG_NO_VALUE;
				} else {
					used_list[ea_entry->header.name_id] = 1;
				}
			}
			if (ea_entry->header.header_data_id != HOG_NO_VALUE) {
				data = DataListGetData(&handle->datalist, ea_entry->header.header_data_id, &size);
				if (!data) {
					err_int_value = ea_entry->header.header_data_id;
					FAIL_SAFE(hogFileReadError_InvalidHeaderDataIndex, "File with invalid header data index");
					ea_entry->header.header_data_id = HOG_NO_VALUE;
				} else {
					used_list[ea_entry->header.header_data_id] = 1;
				}
			}
		}
		if (!handle->read_only && !(handle->create_flags & HOG_NO_REPAIR))
		{
			DataListJournal dlj = {0};
			for (i=0; i<num_datalist_entries; i++) 
			{
				if (!used_list[i]) {
					U32 size;
					const U8 *data;
					data = DataListGetData(&handle->datalist, i, &size);
					assert(!data == !size);
					if (data) {
						// Has data, but is not in use
						hoggMemLog( "0x%08p: Pruning unused data list item #%d: (0x%x) \"%c%c%c%c%c%c...\"", handle,
							i, *(U32*)data, data[0], data[1], data[2], data[3], data[4], data[5]);
						DataListFree(&handle->datalist, i, &dlj);
					}
				}
			}
			if (dlj.size) {
				if (handle->create_flags & HOG_RAM_CACHED)
					hogFileFreeRAMCache(handle); // If we're RAM-cached, we've gotta undo that to apply this change

				if (ret=hogFileAddDataListMod(handle, &dlj)) {
					FAIL(NESTED_ERROR(hogFileReadError_Pruning, ret), "Error while pruning invalid DataList entries");
				}
			}
		}
	}

	if (!handle->policy_journal_datalist && !handle->read_only)
	{
		U32 flag = HOG_INVALID_HEADER_FLAG;
		// Invalidate the hogg file until closed
		fseek(handle->file, 0, SEEK_SET);
		fwrite(&flag, 1, 4, handle->file);
		handle->header_was_invalidated = true;
	}

#if PLATFORM_CONSOLE
	// Only one app on the consoles, and we can't detect PC-changes on shares anyway, just go for better performance!
	hogFileSetSingleAppMode(handle, true);
#endif

fail:
	eaiDestroy(&eaiFilesToPrune);
	SAFE_FREE(used_list);
	SAFE_FREE(used_name_list);
	SAFE_FREE(datalist_bitfield);
	SAFE_FREE(buffer);
	ScratchFree(dl_journal);
	dl_journal = NULL;
	LEAVE_CRITICAL(file_access);
	LEAVE_CRITICAL(data_access);
	if (!err_code)
	{
		// If there were any startup-time changes, flush them now!
		// Must do this after releasing criticals
		if (ret=hogFileModifyFlush(handle)) {
			err_code = ret;
		}
	}
	if(handle->mutex.name)
		hogReleaseMutex(handle, false, false);
	if (err_code)
		hogFileDestroy(handle, false);
	return err_code;
#undef FAIL
}

// Will destroy handle* on success
int hogFileUpgrade(HogFile *handle)
{
	char temp_filename[MAX_PATH];
	char temp_filename2[MAX_PATH];
	char orig_filename[MAX_PATH];
	U32 i;
	HogFile *dest;
	int ret;
	int chars=0;
	int j;
	U32 num_files;

	strcpy(temp_filename, handle->filename);
	strcat(temp_filename, ".upgrade_temp");
	strcpy(temp_filename2, handle->filename);
	strcat(temp_filename2, ".bak");
	strcpy(orig_filename, handle->filename);
	if (fileExists(temp_filename))
		fileForceRemove(temp_filename);
	if (fileExists(temp_filename2))
		fileForceRemove(temp_filename2);

	if (fileExists(temp_filename2) ||
		fileExists(temp_filename))
	{
		// Failed to remove one of the temp files
		return 2;
	}

	dest = hogFileRead(temp_filename, NULL, handle->err_level, &ret, HOG_DEFAULT);
	if (!dest) {
		// Error opening temporary hog file
		return 1;
	}

	loadstart_printf("Upgrading %s to new hogg version... ", orig_filename);

	hogFileLock(handle);
	hogFileLock(dest);

	num_files = hogFileGetNumFiles(handle);
	for (i=0; i<num_files; i++) {
		const char *entry_name;
		char *data;
		U32 data_size;
		bool checksum_valid;
		if (hogFileIsSpecialFile(handle, i))
			continue;
		if (!(entry_name = hogFileGetFileName(handle, i)))
			continue;
		if (strStartsWith(entry_name, "autofiles/")) {
			devassertmsg(0, "Hog file upgrading does not support nameless files");
			continue;
		}
		data = hogFileExtractCompressed(handle, i, &data_size);
		if (data) {
			NewPigEntry new_entry = {0};
			new_entry.checksum[0] = hogFileGetFileChecksum(handle, i);
			new_entry.data = data;
			new_entry.fname = entry_name;
			new_entry.header_data = hogFileGetHeaderData(handle, i, &new_entry.header_data_size);
			new_entry.pack_size = data_size;
			new_entry.size = hogFileGetFileSize(handle, i);
			new_entry.timestamp = hogFileGetFileTimestamp(handle, i);
			hogFileModifyUpdateNamedSync2(dest, &new_entry);
		} else {
			data = hogFileExtract(handle, i, &data_size, &checksum_valid);
			if (data && checksum_valid) {
				ret = hogFileModifyUpdateNamedSync(dest, entry_name, data, data_size, hogFileGetFileTimestamp(handle, i), NULL);
			} else {
				Errorf("Error extracting file %s for hog file version upgrade", entry_name);
			}
		}
		if ((i%10) == 9 || i == num_files - 1)
		{
			for (j=0; j<chars; j++) 
				printf("%c", 8);
			chars = printf("%d/%d", i+1, num_files);
		}
	}

	for (j=0; j<chars; j++) 
		printf("%c", 8);
	for (j=0; j<chars; j++) 
		printf(" ");
	for (j=0; j<chars; j++) 
		printf("%c", 8);

	hogFileUnlock(handle);
	hogFileUnlock(dest);

	hogFileDestroy(dest, true);

	hogFileDestroy(handle, true);

	// Backslashes for Xbox
	backSlashes(orig_filename);
	backSlashes(temp_filename);
	backSlashes(temp_filename2);
	assert(!fileExists(temp_filename2));
	rename(orig_filename, temp_filename2);
	assert(!fileExists(orig_filename));
	rename(temp_filename, orig_filename);
	fileForceRemove(temp_filename2);
	loadend_printf("done.");
	return 0;
}

static bool allow_hogg_upgrade=false;
// Enable automatic hogg upgrading process
AUTO_CMD_INT(allow_hogg_upgrade, allow_hogg_upgrade) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

void hogSetAllowUpgrade(bool allow_upgrade)
{
	allow_hogg_upgrade = !!allow_upgrade;
}

// When opening read only, don't error if the journal needs to be flushed. 
// Open for write, apply the journal, then reopen read only.
HogFile *hogFileReadReadOnlySafeEx(const char *filename, bool *bCreated, PigErrLevel err_level, int *err_return, HogFileCreateFlags flags, U32 datalist_journal_size)
{
	HogFile *hogg = NULL;
	int local_err_return = 0;

	if(flags & HOG_READONLY)
	{
		hogg = hogFileReadEx(filename, bCreated, PIGERR_QUIET, &local_err_return, flags, datalist_journal_size);

		if(hogg)
		{
			hogg->err_level = err_level;
		}
		else
		{
			hogg = hogFileReadEx(filename, bCreated, err_level, err_return, flags & ~HOG_READONLY, datalist_journal_size);
			if(hogg)
			{
				hogFileDestroy(hogg, true);
				hogg = hogFileReadEx(filename, bCreated, err_level, err_return, flags, datalist_journal_size);
			}
		}
	}
	else
	{
		hogg = hogFileReadEx(filename, bCreated, err_level, err_return, flags, datalist_journal_size);
	}

	return hogg;
}

HogFile *hogFileReadReadOnlySafe(const char *filename, bool *bCreated, PigErrLevel err_level, int *err_return, HogFileCreateFlags flags)
{
	return hogFileReadReadOnlySafeEx(filename, bCreated, err_level, err_return, flags, HOG_DEFAULT_DL_JOURNAL_SIZE);
}

// Create a hog file from a special file handle; note that this can not be used with regular files as that would bypass hog sharing rules.
HogFile *hogFileReadFromResource(int resource_id, PigErrLevel err_level, SA_PRE_OP_FREE SA_POST_OP_VALID int *err_return, HogFileCreateFlags flags)
{
	HogFile *hogg;
	char fullpath[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	if (err_return)
		*err_return = 0;

	assert(!(flags & HOG_SKIP_MUTEX)); // Call hogFileSetSkipMutex after opening instead

	// Get pseudo filename.
	sprintf(fullpath, HOG_FILENAME_RESOURCE_PREFIX "%s:%d", getExecutableName(), resource_id);

	// Check if we already have it open; otherwise create it.
	if (hogg = hogFileGetSharedHandleOrLock(fullpath)) {
		hoggMemLog("0x%08p: hogFileReadFromFilePointer(%s):Returning cached handle", hogg, fullpath);
		PERFINFO_AUTO_STOP();
		return hogg;
	} else {

		int ret;

		hogg = hogFileAlloc(err_level, flags | HOG_RAM_CACHED | HOG_READONLY | HOG_NO_REPAIR | HOG_NO_MUTEX);
		hogg->resource_id = resource_id;

		// Open the resource: hogFileReadInternal has special handling if resource_id is set.
		ret = hogFileReadInternal(hogg, fullpath);
		if (ret != 0) // failure
		{
			if (err_return)
				*err_return = ret;
			hogShowErrorEx(err_level, fullpath, NULL, ret, "Unable to read from hogg file! ", 0);
			hogFileDestroy(hogg, true);
			hogFileAddSharedHandleAndUnlock(NULL, NULL);
			PERFINFO_AUTO_STOP();
			return NULL;
		}

		hogFileAddSharedHandleAndUnlock(hogg, fullpath);
		PERFINFO_AUTO_STOP();
		return hogg;
	}
}

// add a reference to a hog that is already open
void hogFileAddRef(HogFile *handle)
{
	int err_return;
	HogFile *result;

	assert(isValidHogHandle(handle));
	assert(hogFileGetSharedRefCount(handle) > 0);

	result = hogFileRead(handle->filename, NULL, handle->err_level, &err_return, handle->create_flags | HOG_NOCREATE);
	assert(result == handle);
	assert(err_return == 0);
}

HogFile *hogFileRead(const char *filename, bool *bCreated, PigErrLevel err_level, int *err_return, HogFileCreateFlags flags)
{
#if !PLATFORM_CONSOLE
	if((IsClient() || GetAppGlobalType() == GLOBALTYPE_TESTCLIENT || GetAppGlobalType() == GLOBALTYPE_GAMESERVER) && stringCacheSharingEnabled() && (flags & HOG_SHARED_MEMORY))
	{
		return hogFileReadShared(filename, bCreated, err_level, err_return, flags, HOG_DEFAULT_DL_JOURNAL_SIZE);
	}
	else
#endif
	{
		return hogFileReadEx(filename, bCreated, err_level, err_return, flags, HOG_DEFAULT_DL_JOURNAL_SIZE);
	}
}

uintptr_t hogFileGetMemoryUsage(HogFile *handle)
{
	uintptr_t size = sizeof(HogFile);

	size += sizeof(HogFileListEntry) * handle->filelist_max;
	size += sizeof(HogEAListEntry) * handle->ealist_max;
	size += DataListGetMemoryUsage(&handle->datalist);
	size += handle->filename ? strlen(handle->filename)+1 : 0;
#if HOGG_WATCH_TIMESTAMPS
	size += handle->filename_for_timestamp ? strlen(handle->filename_for_timestamp)+1 : 0;
#endif
	size += handle->mutex.name ? strlen(handle->mutex.name)+1 : 0;
	size += handle->file_free_list ? eaiMemUsage(&handle->file_free_list, true) : 0;
	size += handle->ea_free_list ? eaiMemUsage(&handle->ea_free_list, true) : 0;
	size += handle->version.name ? strlen(handle->version.name)+1 : 0;

	return size;
}

HogFile *hogFileReadShared(const char *filename, bool *bCreated, PigErrLevel err_level, int *err_return, HogFileCreateFlags flags, U32 datalist_journal_size)
{
	HogFile *hogg;
	SharedMemoryHandle *sm_handle = NULL;
	SM_AcquireResult sm_result;
	HogFile *sm_hogg;
	const char *local_fn = STACK_SPRINTF("%s%s", GlobalTypeToName(GetAppGlobalType()), filename);

	sm_result = sharedMemoryAcquire(&sm_handle, local_fn);

	if(sm_result == SMAR_FirstCaller)
	{
		// Our responsibility to load the hogg fully and then copy to shared memory as appropriate
		hogg = hogFileReadEx(filename, bCreated, err_level, err_return, flags, datalist_journal_size);

		assert(hogg);
		assert(!(*err_return));
		assert(!hogg->free_space2);

		sm_hogg = sharedMemorySetSize(sm_handle, hogFileGetMemoryUsage(hogg));

		// Copy the thing into memory wholesale.
		memcpy(sm_hogg, hogg, sizeof(HogFile));
		sharedMemorySetBytesAlloced(sm_handle, sizeof(HogFile));

		// Clear out pointers to things that would be local.
		ZeroStruct(&sm_hogg->mutex.handle);
		ZeroStruct(&sm_hogg->file);
		ZeroStruct(&sm_hogg->mod_ops_head);
		ZeroStruct(&sm_hogg->mod_ops_tail);
		ZeroStruct(&sm_hogg->file_access);
		ZeroStruct(&sm_hogg->data_access);
		ZeroStruct(&sm_hogg->doing_flush);
		ZeroStruct(&sm_hogg->done_doing_operation_event);
		ZeroStruct(&sm_hogg->starting_flush_event);
		ZeroStruct(&sm_hogg->fn_to_index_lookup);
		ZeroStruct(&sm_hogg->async_new_files);
		ZeroStruct(&sm_hogg->free_space2);
		ZeroStruct(&sm_hogg->version.semaphore);
		ZeroStruct(&sm_hogg->callbacks.fileUpdated);
		ZeroStruct(&sm_hogg->callbacks.userData);

		// Move EArrays and other dynamic structures
		{
			sm_hogg->filelist = sharedMemoryAlloc(sm_handle, sizeof(HogFileListEntry) * hogg->filelist_max);
			memcpy(sm_hogg->filelist, hogg->filelist, sizeof(HogFileListEntry) * hogg->filelist_max);
			free(hogg->filelist);
			hogg->filelist = sm_hogg->filelist;
		}

		{
			sm_hogg->ealist = sharedMemoryAlloc(sm_handle, sizeof(HogEAListEntry) * hogg->ealist_max);
			memcpy(sm_hogg->ealist, hogg->ealist, sizeof(HogEAListEntry) * hogg->ealist_max);
			free(hogg->ealist);
			hogg->ealist = sm_hogg->ealist;
		}

		{
			int i;

			eaCompress(&sm_hogg->datalist.data_list_not_const, &hogg->datalist.data_list_not_const, sharedMemoryAlloc, sm_handle);
			eaiCompress(&sm_hogg->datalist.size_list, &hogg->datalist.size_list, sharedMemoryAlloc, sm_handle);
			eaiCompress(&sm_hogg->datalist.flags, &hogg->datalist.flags, sharedMemoryAlloc, sm_handle);
			eaiCompress(&sm_hogg->datalist.free_list, &hogg->datalist.free_list, sharedMemoryAlloc, sm_handle);
				
			for(i=0; i<eaSize(&hogg->datalist.data_list); ++i)
			{
				if(!(hogg->datalist.flags[i] & DLF_DO_NOT_FREEME_STRINGPOOL))
				{
					U8* new_data = sharedMemoryAlloc(sm_handle, hogg->datalist.size_list[i]);
					memcpy(new_data, hogg->datalist.data_list[i], hogg->datalist.size_list[i]);
					sm_hogg->datalist.data_list_not_const[i] = new_data;
				}
			}

			DataListDestroy(&hogg->datalist);

			hogg->datalist.data_ptr = sm_hogg->datalist.data_ptr;
			hogg->datalist.data_list_not_const = sm_hogg->datalist.data_list_not_const;
			hogg->datalist.size_list = sm_hogg->datalist.size_list;
			hogg->datalist.flags = sm_hogg->datalist.flags;
			hogg->datalist.free_list = sm_hogg->datalist.free_list;
		}

		if(hogg->filename)
		{
			sm_hogg->filename = sharedMemoryAlloc(sm_handle, strlen(hogg->filename)+1);
			memcpy(sm_hogg->filename, hogg->filename, strlen(hogg->filename)+1);
			free(hogg->filename);
			hogg->filename = sm_hogg->filename;
		}

#if HOGG_WATCH_TIMESTAMPS
		if(hogg->filename_for_timestamp)
		{
			sm_hogg->filename_for_timestamp = sharedMemoryAlloc(sm_handle, strlen(hogg->filename_for_timestamp)+1);
			memcpy(sm_hogg->filename_for_timestamp, hogg->filename_for_timestamp, strlen(hogg->filename_for_timestamp)+1);
			free(hogg->filename_for_timestamp);
			hogg->filename_for_timestamp = sm_hogg->filename_for_timestamp;
		}
#endif
		
		if(hogg->mutex.name)
		{
			sm_hogg->mutex.name = sharedMemoryAlloc(sm_handle, strlen(hogg->mutex.name)+1);
			memcpy(sm_hogg->mutex.name, hogg->mutex.name, strlen(hogg->mutex.name)+1);
			free(hogg->mutex.name);
			hogg->mutex.name = sm_hogg->mutex.name;
		}

		{
			eaiCompress(&sm_hogg->file_free_list, &hogg->file_free_list, sharedMemoryAlloc, sm_handle);
			eaiDestroy(&hogg->file_free_list);
			hogg->file_free_list = sm_hogg->file_free_list;
		}

		{
			eaiCompress(&sm_hogg->ea_free_list, &hogg->ea_free_list, sharedMemoryAlloc, sm_handle);
			eaiDestroy(&hogg->ea_free_list);
			hogg->ea_free_list = sm_hogg->ea_free_list;
		}

		if(hogg->version.name)
		{
			sm_hogg->version.name = sharedMemoryAlloc(sm_handle, strlen(hogg->version.name)+1);
			memcpy(sm_hogg->version.name, hogg->version.name, strlen(hogg->version.name)+1);
			free(hogg->version.name);
			hogg->version.name = sm_hogg->version.name;
		}

		// Should be good to unlock now -- god help us all!
		sharedMemoryUnlock(sm_handle);

		return hogg;
	}
	else if(sm_result == SMAR_DataAcquired)
	{
		// We can copy the whole structure out of shared memory
		hogg = calloc(1, sizeof(HogFile));
		sm_hogg = sharedMemoryGetDataPtr(sm_handle);
		memcpy(hogg, sm_hogg, sizeof(HogFile));
		assert(!hogg->free_space2);

		// Initialize hog threading
		hogThreadingInit();

		// Now we need to set up critical sections and stuff.
		InitializeCriticalSection(&hogg->file_access);
		InitializeCriticalSection(&hogg->data_access);
		InitializeCriticalSection(&hogg->doing_flush);
		hogg->crit_sect_inited = true;

		hogg->done_doing_operation_event = CreateEvent(NULL, FALSE, FALSE, NULL); // Auto-reset, not initially signaled
		assert(hogg->done_doing_operation_event);
		hogg->starting_flush_event = CreateEvent(NULL, FALSE, FALSE, NULL); // Auto-reset, not initially signaled
		assert(hogg->starting_flush_event);

		if(!(flags & HOG_NO_MUTEX))
		{
			hogg->version.semaphore = CreateSemaphore_UTF8(NULL, 1, LONG_MAX, hogg->version.name);
			devassertmsg(hogg->version.semaphore, "Failed to open semaphore object");
		}

		hogg->read_only = true;
		hogg->open_mode_string = "rb";
		hogg->file = fopen(hogg->filename, hogg->open_mode_string);

		return hogg;
	}
	else
	{
		return hogFileReadEx(filename, bCreated, err_level, err_return, flags, datalist_journal_size);
	}
}

HogFile *hogFileReadEx(const char *filename, bool *bCreated, PigErrLevel err_level, int *err_return, HogFileCreateFlags flags, U32 datalist_journal_size) // Load a .HOGG file or creates a new empty one if it does not exist
{
	HogFile *hogg;
	int i;
	char fullpath[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	if (err_return)
		*err_return = 0;

	if (!datalist_journal_size)
		datalist_journal_size = HOG_DEFAULT_DL_JOURNAL_SIZE;

	assert(!(flags & HOG_SKIP_MUTEX)); // Call hogFileSetSkipMutex after opening instead

	// Check for redirects
	strcpy(fullpath, filename); 
	forwardSlashes(fullpath);
	for (i=0; i<eaSize(&g_hog_path_redirects); i+=2) {
		if (strStartsWith(fullpath, g_hog_path_redirects[i])) {
			size_t temp_size = strlen(fullpath) + strlen(g_hog_path_redirects[i+1]+1);
			char *temp = ScratchAlloc(temp_size);
			strcpy_s(temp, temp_size, g_hog_path_redirects[i+1]);
			strcat_s(temp, temp_size, fullpath + strlen(g_hog_path_redirects[i]));
			strcpy(fullpath, temp);
			ScratchFree(temp);
		}
	}
	filename = fullpath;

	if (hogg = hogFileGetSharedHandleOrLock(filename)) {
		hoggMemLog( "0x%08p: hogFileRead(%s):Returning cached handle", hogg, filename);
		if(bCreated)
			*bCreated = false;
		if (hogg->read_only &&
			(flags & HOG_MUST_BE_WRITABLE))
		{
			// Previously opened handle is read-only, but the caller is requesting a writable version
			// Reload as writable.
			hogFileReloadAsWritable(hogg);
		}
		if (hogg->read_only &&
			(flags & HOG_MUST_BE_WRITABLE))
		{
			// Won't ever get here anymore, will have been re-opened
			// Previously opened handle is read-only, but the caller is requesting a writable version
			hogShowError(hogg, 1, "Requesting a writable handle to a hogg file which was previously opened read-only.  Caller must first close the read-only handle.", 0);
			if (err_return)
				*err_return = 1;
			hogFileDestroy(hogg, false);
			PERFINFO_AUTO_STOP();
			return NULL;
		}
		if (!hogg->read_only &&
			(flags & HOG_MUST_BE_READONLY))
		{
			// Previously opened handle is NOT read-only, but the caller needs a read-only version
			hogShowError(hogg, 1, "Requesting a read-only handle to a hogg file which was previously opened writable.  Caller must first close the writable handle.", 0);
			if (err_return)
				*err_return = 1;
			hogFileDestroy(hogg, false);
			PERFINFO_AUTO_STOP();
			return NULL;
		}
		PERFINFO_AUTO_STOP();
		return hogg;
	} else {
		ThreadAgnosticMutex hOpenerMutex = NULL;
		char *mutex_name_alloced = NULL;
		int ret;
		if (!(flags & HOG_NO_MUTEX))
		{
			char mutex_name[MAX_PATH];
			// Use the mutex name that will be generated for the hogg
			hogFilenameForMutexName(filename, SAFESTR(mutex_name));
			mutex_name_alloced = makeMutexName(mutex_name, "");

			hOpenerMutex = acquireThreadAgnosticMutex(mutex_name_alloced);
		}
		hogg = hogFileAlloc(err_level, flags);
		if ((fileSizeEx(filename, false) > 0 || fileSize64(filename)>0)) // fileSize64 will fail for in-hogg hoggs, fileSize will fail for > 2GB
		{
			if (bCreated)
				*bCreated = false;
			ret = hogFileReadInternal(hogg, filename);
			if (ret != 0) // failure
			{
				if (!(flags & HOG_NO_MUTEX))
					releaseThreadAgnosticMutex(hOpenerMutex);
				if (err_return)
					*err_return = ret;
				hogShowErrorEx(err_level, filename, NULL, ret, "Unable to read from hogg file! ", 0);
				hogFileDestroy(hogg, true);
				hogFileAddSharedHandleAndUnlock(NULL, NULL);
				SAFE_FREE(mutex_name_alloced);
				PERFINFO_AUTO_STOP();
				return NULL;
			}
		}
		else if (!(flags & HOG_NOCREATE))
		{
			if (bCreated)
				*bCreated = true;
			fileForceRemove(filename);
			if (!hogCreateFromMem(filename, err_level, flags, datalist_journal_size)) {
				if (!(flags & HOG_NO_MUTEX))
					releaseThreadAgnosticMutex(hOpenerMutex);
				hogShowErrorEx(err_level, filename, NULL, 1, "Unable to create hogg file! ", 0);
				hogFileDestroy(hogg, true);
				hogFileAddSharedHandleAndUnlock(NULL, NULL);
				SAFE_FREE(mutex_name_alloced);
				PERFINFO_AUTO_STOP();
				return NULL;
			}
			ret = hogFileReadInternal(hogg, filename);
			if (ret != 0) {
				if (!(flags & HOG_NO_MUTEX))
					releaseThreadAgnosticMutex(hOpenerMutex);
				if (err_return)
					*err_return = ret;
				hogShowErrorEx(err_level, filename, NULL, ret, "Unable to read from hogg file! ", 0);
				hogFileDestroy(hogg, true);
				hogFileAddSharedHandleAndUnlock(NULL, NULL);
				SAFE_FREE(mutex_name_alloced);
				PERFINFO_AUTO_STOP();
				return NULL;
			}
		}
		else
		{
			hogFileDestroy(hogg, true);
			if (!(flags & HOG_NO_MUTEX))
				releaseThreadAgnosticMutex(hOpenerMutex);
			hogFileAddSharedHandleAndUnlock(NULL, NULL);
			SAFE_FREE(mutex_name_alloced);
			PERFINFO_AUTO_STOP();
			return NULL;
		}

		SAFE_FREE(mutex_name_alloced);
		if (!(flags & HOG_NO_MUTEX))
			releaseThreadAgnosticMutex(hOpenerMutex);

		// Read successfully
		if (hogg->need_upgrade && allow_hogg_upgrade) {
			// Upgrade to latest hogg version via streaming from one to the other, renaming
			if (0==hogFileUpgrade(hogg)) {
				HogFile *h;
				// Succeeded, clean up and call ourselves recursively to actually open the new file.
				hogFileAddSharedHandleAndUnlock(NULL, NULL);
				h = hogFileRead(filename, bCreated, err_level, err_return, flags);
				PERFINFO_AUTO_STOP();
				return h;
			} else {
				// Failed, just return the handle to the old hogg file
			}
		}

		hogFileAddSharedHandleAndUnlock(hogg, filename);
		PERFINFO_AUTO_STOP();
		return hogg;
	}
}


U32 hogFileGetNumFiles(HogFile *handle)
{
	U32 ret;
	assert(isValidHogHandle(handle));
	ENTER_CRITICAL(data_access);
	ret = (U32)handle->filelist_count;
	LEAVE_CRITICAL(data_access);
	return ret;
}

U32 hogFileGetNumUserFiles(HogFile *handle)
{
	U32 ret = 0;
	int i;
	assert(isValidHogHandle(handle));
	ENTER_CRITICAL(data_access);
	for (i=hogFileGetNumFiles(handle)-1; i>=0; i--) {
		if (hogFileGetFileName(handle, i) &&
			!hogFileIsSpecialFile(handle, i))
			ret++;
	}
	LEAVE_CRITICAL(data_access);
	return ret;
}


void hogFileReserveFiles(HogFile *handle, U32 numFiles)
{
	assert(isValidHogHandle(handle));
	ENTER_CRITICAL(data_access);
	handle->filelist_min = numFiles;
	handle->ealist_min = numFiles;
	LEAVE_CRITICAL(data_access);
}




U32 hogFileGetNumUsedFiles(HogFile *handle)
{
	U32 ret;
	ENTER_CRITICAL(data_access);
	ret = handle->num_files;
	LEAVE_CRITICAL(data_access);
	return ret;
}

const char *hogFileGetArchiveFileName(HogFile *handle)
{
    return handle ? handle->filename : 0;
}

const char *hogFileGetFileName(HogFile *handle, int index)
{
	HogFileListEntry *file_entry;
	HogEAListEntry *ea_entry = NULL;
	const char *ret=NULL;

	assert(isValidHogHandle(handle));

	ENTER_CRITICAL(data_access);
	if (index < 0 || index >= (int)handle->filelist_count)
		goto cleanup;
	if (!handle->filelist[index].in_use || handle->filelist[index].queued_for_delete)
		goto cleanup;
	file_entry = hogGetFileListEntry(handle, index);
	if (!file_entry)
		goto cleanup;
	if (file_entry->header.headerdata.flagFFFE == 0xFFFE) {
		ea_entry = hogGetEAListEntry(handle, (S32)file_entry->header.headerdata.ea_id);
	}
	if (!ea_entry || ea_entry->header.name_id==HOG_NO_VALUE) {
		static char buf[32];
		sprintf(buf, "autofiles/%08d.dat", index);
		ret = allocAddFilename(buf);
	} else {
		ret = DataListGetString(&handle->datalist, ea_entry->header.name_id, !(handle->create_flags & HOG_NO_STRING_CACHE));
	}
cleanup:
	LEAVE_CRITICAL(data_access);
	return ret;
}

bool hogFileIsSpecialFile(HogFile *handle, int index)
{
	assert(isValidHogHandle(handle));
	if ((U32)index == handle->header.datalist_fileno)
	{
		return true;
	}
	return false;
}

U32 hogFileGetFileTimestampInternal(HogFile *handle, int index)
{
	U32 ret=0;
	HogFileListEntry *file_entry;
	assert(isValidHogHandle(handle));
	IN_CRITICAL(data_access);
	file_entry = hogFileWaitForFileData(handle, index, true, false);
	if (file_entry)
		ret = file_entry->header.timestamp;
	return ret;
}

U32 hogFileGetFileTimestamp(HogFile *handle, int index)
{
	U32 ret=0;
	ENTER_CRITICAL(data_access);
	ret = hogFileGetFileTimestampInternal(handle, index);
	LEAVE_CRITICAL(data_access);
	return ret;
}

U32 hogFileGetFileSizeInternal(HogFile *handle, int index)
{
	U32 pack_size, unpacked_size;
	hogFileGetSizesInternal(handle, index, &unpacked_size, &pack_size);
	return unpacked_size;
}


U32 hogFileGetFileSize(HogFile *handle, int index)
{
	U32 pack_size, unpacked_size;
	hogFileGetSizes(handle, index, &unpacked_size, &pack_size);
	return unpacked_size;
}

U32 hogFileGetFileChecksum(HogFile *handle, int index)
{
	U32 ret=0;
	HogFileListEntry *file_entry;
	assert(isValidHogHandle(handle));
	ENTER_CRITICAL(data_access);
	file_entry = hogFileWaitForFileData(handle, index, true, false);
	if (file_entry)
		ret = file_entry->header.checksum;
	LEAVE_CRITICAL(data_access);
	return ret;
}

bool hogFileIsZipped(HogFile *handle, int index)
{
	U32 pack_size, unpacked_size;
	hogFileGetSizes(handle, index, &unpacked_size, &pack_size);
	return !!pack_size;
}

// Warning: pointer returned is volatile, and may be invalid after any hog file changes
const U8 *hogFileGetHeaderData(HogFile *handle, int index, U32 *header_size)
{
	HogFileListEntry *file_entry;
	const U8 *ret=NULL;
	assert(isValidHogHandle(handle));

	ENTER_CRITICAL(data_access);
	file_entry = hogFileWaitForFileData(handle, index, true, false);
	assert(file_entry);

	if (file_entry->header.headerdata.flagFFFE == 0xFFFE) {
		HogEAListEntry *ea_entry = hogGetEAListEntry(handle, (S32)file_entry->header.headerdata.ea_id);
		if (ea_entry && ea_entry->header.header_data_id!=HOG_NO_VALUE)
		{
			ret = DataListGetData(&handle->datalist, ea_entry->header.header_data_id, header_size);
		}
	}
	LEAVE_CRITICAL(data_access);
	if (!ret)
		*header_size = 0;
	return ret;
}

void hogScanAllFiles(HogFile *handle, HogFileScanProcessor processor, void * userData)
{
	U32 j;
	U32 num_files;
	hogAcquireMutex(handle);

	num_files = hogFileGetNumFiles(handle);

	for (j=0; j<num_files; j++) {
		const char *filename;
		ENTER_CRITICAL(data_access);
		hogFileWaitForFileData(handle, j, false, false); // The file may be getting updated or deleted in another thread
		LEAVE_CRITICAL(data_access);

		filename = hogFileGetFileName(handle, j);

		if (!filename || hogFileIsSpecialFile(handle, j))
			continue;
		if (!processor(handle, j, filename, userData))
			break;
	}

	hogReleaseMutex(handle, false, false);
}

static bool deleteAllFiles(HogFile *handle, HogFileIndex index, const char* filename, void *userData)
{
	hogFileModifyDelete(handle, index);
	return true;
}

void hogDeleteAllFiles(HogFile *handle)
{
	hogScanAllFiles(handle, deleteAllFiles, NULL);
}

static int hogFileGetNumEAs(HogFile *handle)
{
	int ret;
	ENTER_CRITICAL(data_access);
	ret = (U32)handle->ealist_count;
	LEAVE_CRITICAL(data_access);
	return ret;
}

static int hogFileGetNumUsedEAs(HogFile *handle) // Slow
{
	int ret=0;
	ENTER_CRITICAL(data_access);
	ret = (U32)handle->ealist_count - eaiSize(&handle->ea_free_list);
	LEAVE_CRITICAL(data_access);
	return ret;
}

// Size that the .hogg file is required to be (for truncating and fragmentation)
static U64 hogFileGetRequiredFileSize(HogFile *handle)
{
	IN_CRITICAL(data_access);
	return hfst2GetTotalSize(handle->free_space2);
}

U64 hogFileGetArchiveSize(HogFile *handle)
{
	U64 ret;
	assert(isValidHogHandle(handle));
	ENTER_CRITICAL(data_access);
	ret = hogFileGetRequiredFileSize(handle);
	LEAVE_CRITICAL(data_access);
	return ret;
}

U64 hogFileGetLargestFreeSpace(HogFile *handle)
{
	U64 ret;
	assert(isValidHogHandle(handle));
	ENTER_CRITICAL(data_access);
	ret = hfst2GetLargestFreeSpace(handle->free_space2);
	LEAVE_CRITICAL(data_access);
	return ret;
}

static U64 hogFileCalcFragmentationSize(HogFile *handle)
{
	U64 total_size;
	U64 used_size=0;
	U64 archive_size;
	U32 i;
	ENTER_CRITICAL(data_access);
	archive_size=hogFileOffsetOfData(handle);
	for (i=0; i<handle->filelist_count; i++) {
		HogFileListEntry *file_entry = &handle->filelist[i];
		if (!file_entry->in_use || file_entry->queued_for_delete)
			continue;
		used_size += (U64)file_entry->header.size;
		MAX1(archive_size, file_entry->header.offset + file_entry->header.size);
	}
	total_size=archive_size - hogFileOffsetOfData(handle);
	LEAVE_CRITICAL(data_access);
	return total_size - used_size;
}

F32 hogFileCalcFragmentation(HogFile *handle)
{
	U64 total_size;
	U64 used_size=0;
	U64 archive_size;
	U32 i;
	F32 total_size_float;
	F32 delta_size_float;
	ENTER_CRITICAL(data_access);
	archive_size=hogFileOffsetOfData(handle);
	for (i=0; i<handle->filelist_count; i++) {
		HogFileListEntry *file_entry = &handle->filelist[i];
		if (!file_entry->in_use || file_entry->queued_for_delete)
			continue;
		used_size += (U64)file_entry->header.size;
		MAX1(archive_size, file_entry->header.offset + file_entry->header.size);
	}
	total_size=archive_size - hogFileOffsetOfData(handle);
	total_size_float=(F32)(total_size);
	delta_size_float=(F32)(total_size - used_size);
	LEAVE_CRITICAL(data_access);
	return delta_size_float / total_size_float;
}

bool hogFileShouldDefragEx(HogFile *handle, U64 threshold_bytes, U64 *wasted_bytes)
{
	bool bRet;
	U64 total_size;
	U64 used_size=0;
	U64 archive_size;
	U64 internal_fragmentation;
	U64 slack_size;
	U64 filelist_wasted=0;
	U32 i;
	bool bAssumeTight=true; // Could make this a parameter if needed
	if (!threshold_bytes)
		threshold_bytes = 100*1024;
	ENTER_CRITICAL(data_access);
	archive_size=hogFileOffsetOfData(handle);
	for (i=0; i<handle->filelist_count; i++) {
		HogFileListEntry *file_entry = &handle->filelist[i];
		if (!file_entry->in_use || file_entry->queued_for_delete)
			continue;
		used_size += (U64)file_entry->header.size;
		MAX1(archive_size, file_entry->header.offset + file_entry->header.size);
	}
	total_size=archive_size - hogFileOffsetOfData(handle);
	internal_fragmentation = total_size - used_size;
	slack_size = handle->file_size - archive_size;
	if (!bAssumeTight)
	{
		U64 expected_slack = 0.1f * archive_size;
		if (slack_size < expected_slack)
			slack_size = 0;
		else
			slack_size -= expected_slack;
	}
	if (bAssumeTight)
		filelist_wasted = (hogFileGetNumFiles(handle) - hogFileGetNumUsedFiles(handle)) * sizeof(HogFileHeader) +
			(hogFileGetNumEAs(handle) - hogFileGetNumUsedEAs(handle)) * sizeof(HogEAHeader);

	if (wasted_bytes)
		*wasted_bytes = slack_size + internal_fragmentation + filelist_wasted;
	bRet = (slack_size + internal_fragmentation + filelist_wasted >= threshold_bytes);
	LEAVE_CRITICAL(data_access);
	return bRet;
}

U64 hogFileGetQueuedModSize(HogFile *handle)
{
	assert(isValidHogHandle(handle));
	return handle->mod_list_byte_size;
}

typedef struct FileAndIndex
{
	const char *filename;
	U32 size;
	U32 index;
} FileAndIndex;

static int faiCompare(const FileAndIndex *f1, const FileAndIndex *f2)
{
	return stricmp((f1)->filename, (f2)->filename);
}

void hogFileDumpInfoToEstr(HogFile *handle, char **estr)
{
	U64 uncompressed_size=0;
	U64 ondisk_size=0;
	U64 archive_size;
	U64 slack_size;
	U32 i;
	char buf[64];
	bool bShouldDefrag;
	U64 wasted_bytes;

	assert(isValidHogHandle(handle));

	ENTER_CRITICAL(data_access);
	archive_size=hogFileOffsetOfData(handle);
	for (i=0; i<handle->filelist_count; i++) {
		HogFileListEntry *file_entry = &handle->filelist[i];
		HogEAListEntry *ea_entry = NULL;
		if (!file_entry->in_use || file_entry->queued_for_delete)
			continue;
		if (file_entry->header.headerdata.flagFFFE == 0xFFFE && file_entry->header.headerdata.ea_id != HOG_NO_VALUE)
			ea_entry = hogGetEAListEntry(handle, (S32)file_entry->header.headerdata.ea_id);
		ondisk_size += (U64)file_entry->header.size;
		uncompressed_size += (ea_entry && ea_entry->header.unpacked_size)?ea_entry->header.unpacked_size:file_entry->header.size;
		MAX1(archive_size, file_entry->header.offset + file_entry->header.size);
	}
	slack_size = handle->file_size - archive_size;

	estrConcatf(estr, "HogFile Info:\n");
	estrConcatf(estr, "  HogFile version: %d\n", handle->header.version);
	estrConcatf(estr, "  DataList Journal size: %s/%s\n", friendlyBytesBuf(handle->datalist_diff_size, buf), friendlyBytes(handle->header.dl_journal_size));
	estrConcatf(estr, "  Number of Files: %ld (%ld)\n", hogFileGetNumUsedFiles(handle), hogFileGetNumFiles(handle));
	estrConcatf(estr, "  Number of EAs: %ld (%ld)\n", hogFileGetNumUsedEAs(handle), hogFileGetNumEAs(handle));
	estrConcatf(estr, "  Number of Names and HeaderData blocks: %ld (%ld)\n", DataListGetNumEntries(&handle->datalist), DataListGetNumTotalEntries(&handle->datalist));
	estrConcatf(estr, "  Internal fragmentation: %1.1f%% (%s)\n", hogFileCalcFragmentation(handle)*100, friendlyBytes(hogFileCalcFragmentationSize(handle)));
	estrConcatf(estr, "  Slack: %1.1f%% (%s)\n", slack_size * 100.f / handle->file_size, friendlyBytes(slack_size));
	estrConcatf(estr, "  Archive header+journal size: %s (%"FORM_LL"d)\n", friendlyBytes(hogFileOffsetOfData(handle)), (U64)hogFileOffsetOfData(handle));
	estrConcatf(estr, "  All files total uncompressed Size: %s (%"FORM_LL"d)\n", friendlyBytes(uncompressed_size), uncompressed_size);
	estrConcatf(estr, "  All files total on-disk Size: %s (%"FORM_LL"d)\n", friendlyBytes(ondisk_size), ondisk_size);
	estrConcatf(estr, "  Required file size: %s (%"FORM_LL"d)\n", friendlyBytes(archive_size), archive_size);
	estrConcatf(estr, "  Actual file size: %s (%"FORM_LL"d)\n", friendlyBytes(handle->file_size), handle->file_size);
	bShouldDefrag = hogFileShouldDefragEx(handle, 0, &wasted_bytes);
	estrConcatf(estr, "  Should defrag: %s (%s wasted)\n", bShouldDefrag?"YES":"No", friendlyBytes(wasted_bytes));

	LEAVE_CRITICAL(data_access);
}

void hogFileDumpInfo(HogFile *handle, int verbose, int debug_verbose)
{
	U32 i;
	FileAndIndex *aFiles = NULL;
	char *str=NULL;
	U32 num_files=0;

	assert(isValidHogHandle(handle));

	ENTER_CRITICAL(data_access);
	if (debug_verbose) { // header info
		estrStackCreate(&str);
		hogFileDumpInfoToEstr(handle, &str);
		printf("%s", str);
		estrDestroy(&str);
		if (debug_verbose==2) {
			LEAVE_CRITICAL(data_access);
			return;
		}
	}

	aFiles = calloc(handle->filelist_count, sizeof(aFiles[0]));
	for(i=0; i<handle->filelist_count; i++)
	{
		FileAndIndex *fai = &aFiles[num_files];
		const char *filename = "NONAME";
		HogFileListEntry *file_entry = &handle->filelist[i];
		HogEAListEntry *ea_entry = NULL;
		U32 crc = 0;
		U32 unpacked_size = 0;

		if (!file_entry->in_use || file_entry->queued_for_delete)
		{
			
		}
		else
		{
			if (file_entry->header.headerdata.flagFFFE == 0xFFFE) {
				assert(file_entry->header.headerdata.ea_id != HOG_NO_VALUE);
				ea_entry = hogGetEAListEntry(handle, file_entry->header.headerdata.ea_id);
			} else {
				ea_entry = NULL; // May have a 64-bit header value
			}
			if (ea_entry) {
				if (ea_entry->header.name_id!=HOG_NO_VALUE) {
					filename = DataListGetString(&handle->datalist, ea_entry->header.name_id, false);
				}
			}
			fai->filename = filename;
			fai->size = file_entry->header.size;
			fai->index = i;
			num_files++;
		}
	}

	qsort(aFiles, num_files, sizeof(aFiles[0]), faiCompare);

	if(num_files)
	{
		for (i=0; i<num_files; i++) {
			char date[32];
			const char *filename="NONAME";
			HogFileListEntry *file_entry = &handle->filelist[aFiles[i].index];
			HogEAListEntry *ea_entry = NULL;
			U32 crc = 0;
			U32 unpacked_size = 0;
			if (!file_entry->in_use || file_entry->queued_for_delete)
				continue;
			if (file_entry->header.headerdata.flagFFFE == 0xFFFE) {
				assert(file_entry->header.headerdata.ea_id != HOG_NO_VALUE);
				ea_entry = hogGetEAListEntry(handle, file_entry->header.headerdata.ea_id);
			} else {
				ea_entry = NULL; // May have a 64-bit header value
			}
			crc = file_entry->header.checksum;
			if (ea_entry) {
				filename = hogFileGetFileName(handle, aFiles[i].index);
				unpacked_size = ea_entry->header.unpacked_size;
			}
			if (verbose)
				printf("%06d: ", i);
			if (debug_verbose) {
				printf("0x%010"FORM_LL"X %c ", file_entry->header.offset, (!ea_entry || ea_entry->header.header_data_id==HOG_NO_VALUE)?' ':'H');
			}
			if (!verbose) {
				printf("%s\n", filename);
			} else {
				if (0!=_ctime32_s(SAFESTR(date), &file_entry->header.timestamp)) {
					sprintf(date, "Invalid date (%d)", file_entry->header.timestamp);
				} else {
					date[strlen(date)-1]=0; // knock off the carriage return
				}
				if (verbose > 1)
					printf("%08x ",crc);
				printf("%10ld", file_entry->header.size);
				if (verbose > 1)
					printf("/%10ld",unpacked_size);
				printf(" %16s %s\n", date, filename);
			}
		}
	}
	
	LEAVE_CRITICAL(data_access);

	SAFE_FREE(aFiles);
}


typedef struct HogVerifyData
{
	char **estr;
	bool bRet;
	bool bDeleteCorruptFiles;
} HogVerifyData;

static bool hogVerifyProcessor(HogFile *handle, HogFileIndex index, const char* filename, void * userData)
{
	HogVerifyData *data = (HogVerifyData*)userData;
	U32 count;
	bool checksum_valid=true;
	bool bad=false;
	void *filedata = hogFileExtract(handle, index, &count, &checksum_valid);
	if (!checksum_valid)
	{
		estrConcatf(data->estr, "%d:%s: Checksum on file data is invalid\n", index, filename);
		bad = true;
		data->bRet = false;
	} else if (!filedata) {
		estrConcatf(data->estr, "%d:%s: Unable to extract file data\n", index, filename);
		bad = true;
		data->bRet = false;
	}
	SAFE_FREE(filedata);
	if (bad && data->bDeleteCorruptFiles)
	{
		hogFileModifyDelete(handle, index);
	}
	return true;
}

bool hogFileVerifyToEstr(HogFile *handle, char **estr, bool bDeleteCorruptFiles)
{
	HogVerifyData data;

	assert(isValidHogHandle(handle));

	data.estr = estr;
	data.bRet = true;
	data.bDeleteCorruptFiles = bDeleteCorruptFiles;
	estrCopy2(data.estr, "");
	hogScanAllFiles(handle, hogVerifyProcessor, &data);

	if (data.bRet)
	{
		estrConcatf(data.estr, "All files verified OK!");
	}

	return data.bRet;
}





//////////////////////////////////////////////////////////////////////////
// HogFileModify functions
//////////////////////////////////////////////////////////////////////////
// All of these should generate commands that a background thread commits to disk

static U32 gHogMaxBufferCount = 32700; //Max of 32K since that's how many we can count in FileEntry::dirty
static U64 gHogMaxBufferSize = 256*1024*1024;
void hogSetMaxBufferSize(U64 bufferSize)
{
	gHogMaxBufferSize = bufferSize;
}

void hogSetMaxBufferCount(U32 bufferCount)
{
	gHogMaxBufferCount = bufferCount;
}

static void hogFileModifyCheckForFlush(HogFile *handle)
{
	bool needFlush=false;
	PERFINFO_AUTO_START("hogFileModifyCheckForFlush",1);
	do {
		needFlush=false;
		ENTER_CRITICAL(data_access);
		if (handle->debug_in_data_access != 1) {
			// Recursively in the critical!
			LEAVE_CRITICAL(data_access);
			break;
		}
		if (gHogMaxBufferCount && handle->mod_list_size > gHogMaxBufferCount ||
			gHogMaxBufferSize && handle->mod_list_byte_size > gHogMaxBufferSize)
			needFlush = true; // Too much stuff in the background!
		LEAVE_CRITICAL(data_access);
		//if (needFlush)
		//	hogFileModifyFlush(handle); // Only if single threaded
		if (needFlush) {
			//printf("Buffer full, sleeping...\n");
			Sleep(10);
		}
	} while (needFlush);
	PERFINFO_AUTO_STOP();
}

bool hogFileNeedsToFlush(HogFile *handle)
{
	bool needFlush=false;
	assert(isValidHogHandle(handle));
	PERFINFO_AUTO_START("hogFileNeedsToFlush",1);
		ENTER_CRITICAL(data_access);
		if (handle->debug_in_data_access == 1) {
			if (gHogMaxBufferCount && handle->mod_list_size > gHogMaxBufferCount ||
				gHogMaxBufferSize && handle->mod_list_byte_size > gHogMaxBufferSize)
				needFlush = true; // Too much stuff in the background!
		}
		LEAVE_CRITICAL(data_access);
	PERFINFO_AUTO_STOP();
	
	return needFlush;
}

void hogReleaseHeaderData(HogFile *handle)
{
	if (handle->read_only)
	{
		DataListDumpNonStringData(&handle->datalist);
	}
}

void hogReleaseFileData(HogFile *handle)
{
	SAFE_FREE(handle->filelist);
	SAFE_FREE(handle->ealist);
	DataListDestroy(&handle->datalist);
	if (handle->fn_to_index_lookup)
	{
		stashTableDestroy(handle->fn_to_index_lookup);
		handle->fn_to_index_lookup = NULL;
	}
}

#define COMMAND_BYTE_SIZE 4
static void hogFileAddMod(HogFile *handle, HogFileMod *mod, U32 byte_size)
{
	ENTER_CRITICAL(data_access);
	assert(handle->mutex.reference_count);
	hogFillInFileModBasics(handle, mod, byte_size);
	mod->next = NULL;
	if (handle->mod_ops_tail) {
		assert(handle->mod_ops_head);
		handle->mod_ops_tail->next = mod;
	} else {
		// No tail or head
		handle->mod_ops_head = mod;
	}
	handle->mod_ops_tail = mod;
	handle->mod_list_size++;
	assert(mod->byte_size > 0);
	handle->mod_list_byte_size += mod->byte_size;
	hogThreadHasWork(handle);
	LEAVE_CRITICAL(data_access);
}

static HogFileMod *hogFileGetMod(HogFile *handle)
{
	HogFileMod *ret;

    ENTER_CRITICAL(data_access);
    ret = handle->mod_ops_head;
    if (ret) {
        handle->mod_ops_head = ret->next;
        if (handle->mod_ops_tail == ret) {
	        handle->mod_ops_tail = NULL;
	        assert(!handle->mod_ops_head);
        }
        assert(ret->byte_size > 0);
        assert(ret->byte_size <= handle->mod_list_byte_size);
        handle->mod_list_byte_size-=ret->byte_size;
        handle->mod_list_size--;
    }
	LEAVE_CRITICAL(data_access);

    return ret;
}

// Caller must have called hogAquireMutex()
static void hogReleaseMutexAsync(HogFile *handle, bool needsFlushAndSignalAndAsyncOpDecrement)
{
	HogFileMod *mod;
	hoggMemLog( "0x%08p: Mod:ReleaseMutex:%d", handle, (int)needsFlushAndSignalAndAsyncOpDecrement);
	mod = callocHogFileMod();
	mod->type = HFM_RELEASE_MUTEX;
	mod->release_mutex.needsFlushAndSignalAndAsyncOpDecrement = needsFlushAndSignalAndAsyncOpDecrement;
	hogFileAddMod(handle, mod, COMMAND_BYTE_SIZE);
	InterlockedIncrement(&total_async_mutex_release_count);
}

static int hogFileFlushDataListDiff(HogFile *handle)
{
	HogFileMod *mod;
	U8 *data;
	U32 datasize;
	int ret;

	PERFINFO_AUTO_START_FUNC();

	assert(handle->mutex.reference_count); // Should already be in one of these
	hogAcquireMutex(handle); // Just because we free it when processing the mod

	IN_CRITICAL(data_access);

	hoggMemLog( "0x%08p: hogFileFlushDataListDiff:Modifying DataList file", handle);
	// Serialize and write new data (recoverably)
	data = DataListWrite(&handle->datalist, &datasize); // Does Endianness internally
	assert(((U32*)data)[1] > 0); // We're writing no data, did it get blown away in memory?
	{
		NewPigEntry entry={0};
		entry.data = data;
		entry.size = datasize;
		entry.timestamp = (handle->create_flags&HOG_NO_INTERNAL_TIMESTAMPS)?0:_time32(NULL);
		entry.fname = HOG_DATALIST_FILENAME;
		if (ret = hogFileModifyUpdate(handle, handle->header.datalist_fileno, &entry))
		{
			hogReleaseMutex(handle, false, false);
			PERFINFO_AUTO_STOP();
			return NESTED_ERROR(1, ret);
		}
	}

	hoggMemLog( "0x%08p: Mod:FlushDataListDiff", handle);
	mod = callocHogFileMod();
	mod->type = HFM_DATALISTFLUSH;
	mod->datalistflush.size_offset = hogFileOffsetOfDLJournal(handle);
	hogFileAddMod(handle, mod, COMMAND_BYTE_SIZE);

	// Zero out the DL journal
	handle->datalist_diff_size = 0;
	PERFINFO_AUTO_STOP();
	return 0;
}

static int just_flushed_datalist=0;

// Send modification of DataList change (may need to do some moves/writes/etc first)
static int hogFileAddDataListMod(HogFile *handle, DataListJournal *dlj)
{
	int ret=0;
	HogFileMod *mod;
	assert(dlj->size && dlj->data);

	assert(handle->mutex.reference_count);
	IN_CRITICAL(data_access);

	if (!handle->policy_journal_datalist)
	{
		// Do nothing
		handle->datalist_diff_size = handle->datalist_diff_size + dlj->size;
		SAFE_FREE(dlj->data);
	}
	else if (handle->datalist_diff_size + dlj->size + sizeof(DLJournalHeader) >= handle->header.dl_journal_size)
	{
		just_flushed_datalist = 1;
		// Includes this change!
		if (ret=hogFileFlushDataListDiff(handle)) {
			SAFE_FREE(dlj->data);
			return NESTED_ERROR(1, ret);
		}
		SAFE_FREE(dlj->data);
	} else {
		hogAcquireMutex(handle); // Just because we free it when processing the mod

		just_flushed_datalist = 0;
		hoggMemLog( "0x%08p: Mod:  DataListDiff size %d", handle, dlj->size);
		mod = callocHogFileMod();
		mod->type = HFM_DATALISTDIFF;
		mod->datalistdiff.data = dlj->data;
		mod->datalistdiff.newsize = handle->datalist_diff_size + dlj->size;
		assert(mod->datalistdiff.newsize + sizeof(DLJournalHeader) < handle->header.dl_journal_size);
		mod->datalistdiff.offset = hogFileOffsetOfDLJournal(handle) + handle->datalist_diff_size + sizeof(DLJournalHeader);
		mod->datalistdiff.size = dlj->size;
		mod->datalistdiff.size_offset = hogFileOffsetOfDLJournal(handle);
		handle->datalist_diff_size = mod->datalistdiff.newsize;
		// Send packet
		hogFileAddMod(handle, mod, dlj->size);
	}
	return ret;
}

void hogFileFreeSpace(HogFile *handle, U64 offset, U32 size)
{
	PERFINFO_AUTO_START_FUNC();
	hoggMemLog( "0x%08p: Freeing offs: %"FORM_LL"d size: %d", handle, offset, size);
	if (size)
	{
		IN_CRITICAL(data_access);
		hfst2FreeSpace(handle->free_space2, offset, size);
	}
	PERFINFO_AUTO_STOP();
}

int disable_truncate_assert;
// Disables some hogg error checking
AUTO_CMD_INT(disable_truncate_assert, disable_truncate_assert) ACMD_CMDLINE;

int hogFileModifyDelete_dbg(HogFile *handle, HogFileIndex file, const char *caller_fname, int linenum)
{
	int ret;
	HogFileMod *mod;
	HogFileListEntry *file_entry;
	HogEAListEntry *ea_entry = NULL;
	DataListJournal dlj = {0};
	S32 ea_id=HOG_NO_VALUE;
	U64 newsize;
	bool should_truncate=false;
	const char *name=NULL;
	bool bDidAutoRelease=false;

	assert(isValidHogHandle(handle));
	assert(!handle->read_only);

	devassert(handle->mutex.reference_count > 0); // Only guaranteed multi-process safe to call this on a locked hogg (e.g. in a scan callback), use hogFileModifyDeleteNamed() instead otherwise

	hogAcquireMutex(handle);
	ENTER_CRITICAL(data_access);
	if (handle->auto_release_data_access)
	{
		assert(handle->debug_in_data_access == 2);
		handle->auto_release_data_access = 0; // Handle it once, clear it to not mess up other things
		bDidAutoRelease = true;
		LEAVE_CRITICAL(data_access);
	}

	if (file == HOG_INVALID_INDEX)
	{
		if (!bDidAutoRelease)
			LEAVE_CRITICAL(data_access);
		hogReleaseMutex(handle, false, false);
		return -1;
	}

	// We release data_access, but if anyone else tries to delete
	//  this file at the same time, it will get flagged as queued_for_delete
	//  and they will assert trying to access it.
	file_entry = hogFileWaitForFileData(handle, file, true, true);

	if (file_entry == NULL) {
		// File index invalid (deleted in another process perhaps)
		if (!bDidAutoRelease)
			LEAVE_CRITICAL(data_access);
		hogReleaseMutex(handle, false, false);
		return -1;
	}

	assert(file_entry->queued_for_delete); // Should have happened in hogFileWaitForFileData

	assert(!(handle->create_flags & HOG_RAM_CACHED));

	// Make modification packet
	hoggMemLog( "0x%08p: Mod:Delete %d Offs:%"FORM_LL"d, size:%d from %s:%d", handle, file, file_entry->header.offset, file_entry->header.size, caller_fname, linenum);
	mod = callocHogFileMod();
	mod->type = HFM_DELETE;
	mod->del.file_index = file;
	if (file_entry->header.headerdata.flagFFFE == 0xFFFE) {
		ea_id = (S32)file_entry->header.headerdata.ea_id;
		mod->del.ea_id = ea_id;
		ea_entry = hogGetEAListEntry(handle, ea_id);
	} else {
		mod->del.ea_id = HOG_NO_VALUE;
	}

	if (!handle->doing_delete_during_load_or_repair)
		hogFileFreeSpace(handle, file_entry->header.offset, file_entry->header.size);

	// Modify in-memory structures and make DataList Journal
	// Can't zero dirty flag/count!
	// Moved to thread: ZeroStruct(&file_entry->header);
	// Moved to thread: file_entry->in_use = 0;
	// Moved to thread: eaiPush(&handle->file_free_list, file+1);
	// Moved to thread: handle->num_files--;
	if (ea_entry) {
		PERFINFO_AUTO_START("DataList modification", 1);
		// Remove Name and Header if they exist
		if (ea_entry->header.name_id != HOG_NO_VALUE) {
			const char *fn = DataListGetString(&handle->datalist, ea_entry->header.name_id, false);
			if (fn) {
				if (handle->callbacks.fileUpdated)
					name = allocAddString(fn);
				if (!(handle->create_flags & HOG_NO_ACCESS_BY_NAME))
					assert(storeNameToId(handle,fn,-1) || handle->doing_delete_during_load_or_repair);
				// stashRemoveInt(handle->fn_to_index_lookup, fn, NULL);
				DataListFree(&handle->datalist, ea_entry->header.name_id, &dlj);
			}
		}
		if (ea_entry->header.header_data_id != HOG_NO_VALUE) {
			DataListFree(&handle->datalist, ea_entry->header.header_data_id, &dlj);
		}

		ZeroStruct(ea_entry);
		ea_entry->header.flags |= HOGEA_NOT_IN_USE;
		//if (isDevelopmentMode()) // This assert can take a lot of time, let's not do it in production mode - also slows down pig.exe way too much on big hoggs
		//	assert(-1==eaiFind(&handle->ea_free_list, ea_id+1));
		//Moved to thread: eaiPush(&handle->ea_free_list, ea_id+1);
		PERFINFO_AUTO_STOP();
	}

	// Check for file truncation (must be *before* sending packet which may release the mutex)
	newsize = hogFileGetRequiredFileSize(handle);
	if (newsize < 0.9 * handle->file_size) {
		should_truncate = true;
		hogAcquireMutex(handle);
	}

	// Send modification packet
	hogFileAddMod(handle, mod, COMMAND_BYTE_SIZE);

	// If datalist was changed, send modification of that (may need to do some moves/writes/etc first)
	if (dlj.size) {
		if (ret=hogFileAddDataListMod(handle, &dlj)) {
			if (!bDidAutoRelease)
				LEAVE_CRITICAL(data_access);
			if (should_truncate)
				hogReleaseMutex(handle, false, false);
			return NESTED_ERROR(2, ret);
		}
	}

	if (should_truncate) {
		// truncate!
		hoggMemLog( "0x%08p: Mod:Truncate:%"FORM_LL"d", handle, newsize);
		mod = callocHogFileMod();
		mod->type = HFM_TRUNCATE;
		mod->truncate.newsize = hogFileGetRequiredFileSize(handle); // Recalculate here because the datalistdiff flush might have grown it!
		if (!disable_truncate_assert)
		{
			U32 j;
			// Verify that newsize is accurate and we're not actually truncating an existing file
			for (j=0; j<handle->filelist_count; j++)
			{
				if (handle->filelist[j].in_use && !handle->filelist[j].queued_for_delete)
				{
					assertmsg(handle->filelist[j].header.offset + handle->filelist[j].header.size <= mod->truncate.newsize,
						"Trying to truncate when the data is actually in use");
					// If this goes off, there was corruption in handle->free_space2 (the RBT that tracks free/used space),
					//   either from memory corruption or a bug in the RBT code.
				}
			}

		}
		handle->file_size = mod->truncate.newsize;
		hogFileAddMod(handle, mod, COMMAND_BYTE_SIZE);
	}

	if (handle->callbacks.fileUpdated && name)
		hogFileCallCallback(handle, name, 0, 0, HOG_INVALID_INDEX);

	if (!bDidAutoRelease)
		LEAVE_CRITICAL(data_access);
	hogFileModifyCheckForFlush(handle);
	return 0;
}

int hogFileModifyTruncate(HogFile *handle) // If the archive can be made smaller, truncate it
{
	U64 newsize;
	ENTER_CRITICAL(data_access);
	hogAcquireMutex(handle);
	// Check for file truncation
	newsize = hogFileGetRequiredFileSize(handle);
	if (newsize < handle->file_size) {
		HogFileMod *mod;
		// truncate!
		hoggMemLog( "0x%08p: Mod:Truncate:%"FORM_LL"d", handle, newsize);
		mod = callocHogFileMod();
		mod->type = HFM_TRUNCATE;
		mod->truncate.newsize = hogFileGetRequiredFileSize(handle); // Recalculate here because the datalistdiff flush might have grown it!
		hogFileAddMod(handle, mod, COMMAND_BYTE_SIZE);
		handle->file_size = mod->truncate.newsize;
		// Mutex released in other thread
	} else {
		hogReleaseMutex(handle, false, false);
	}

	LEAVE_CRITICAL(data_access);
	return 0;
}

static U64 hogFileModifyFitFile(HogFile *handle, U32 size, U64 data_offset, HogFileIndex file_index)
{
	U64 ret=0;
	IN_CRITICAL(data_access);
	hfst2SetStartLocation(handle->free_space2, data_offset);
	ret = hfst2GetSpace(handle->free_space2, size, size+hogFileGetSlack(handle, file_index));
	//hoggMemLog( "0x%08p:   Allocing %d bytes past %"FORM_LL"d: Return %"FORM_LL"d", handle, size, data_offset, ret);
	return ret;
}

static int hogFileModifyMoveFilePast(HogFile *handle, HogFileIndex file, U64 offset)
{
	HogFileMod *mod;
	HogFileListEntry *file_entry = hogGetFileListEntry(handle, file);
	U64 new_offset;
	assert(file_entry->header.size);

	assert(handle->mutex.reference_count);
	hogAcquireMutex(handle); // Just because we free it when processing the mod

	IN_CRITICAL(data_access);

	// Determine new location
	new_offset = hogFileModifyFitFile(handle, file_entry->header.size, offset, file);

	if (new_offset + file_entry->header.size > handle->file_size)
	{
		hogFileModifyGrowToAtLeast(handle, new_offset + file_entry->header.size);
	}

	// Send Mod packet
	hoggMemLog( "0x%08p: Mod:Move %d From:%"FORM_LL"d, To: %"FORM_LL"d, size:%d", handle, file, file_entry->header.offset, new_offset, file_entry->header.size);
	mod = callocHogFileMod();
	mod->type = HFM_MOVE;
	mod->move.file_index = file;
	mod->move.old_offset = file_entry->header.offset;
	mod->move.new_offset = new_offset;
	mod->move.size = file_entry->header.size;
	hogDirtyFile(handle, file);
	hogFileAddMod(handle, mod, file_entry->header.size+1);

	// Modify in-memory structures
	hogFileFreeSpace(handle, file_entry->header.offset, file_entry->header.size);
	file_entry->header.offset = new_offset;

	return 0;
}

static int hogFileModifyGrowFileAndEAList(HogFile *handle)
{
	size_t filelist_old_used_count;
	size_t filelist_new_count;
	size_t ealist_new_count;
	size_t ealist_old_used_count;
	size_t filelist_new_size;
	size_t ealist_new_size;
	size_t ealist_new_offset;
	size_t data_new_offset;
	U32 i;
	int ret;
	HogFileMod *mod=NULL;

	assert(handle->mutex.reference_count);
	hogAcquireMutex(handle); // Just because we free it when processing the mod

	IN_CRITICAL(data_access);
	hoggMemLog( "0x%08p: GrowFileAndEAList", handle);

	// Find new FileList size
	filelist_new_count = handle->filelist_count;
	filelist_old_used_count = hogFileGetNumUsedFiles(handle);
	if ((handle->filelist_count - filelist_old_used_count) / (F32)filelist_old_used_count < FILELIST_PAD_THRESHOLD )
	{
		filelist_new_count = (U32)(handle->filelist_count * (1 + FILELIST_PAD));
		if (filelist_new_count < handle->filelist_count + 16) {
			// Up it by at least 16 if we're going through the effort!
			filelist_new_count = handle->filelist_count + 16;
		}
		// Must expand FileList by at least the size of the old EAList
		if ((filelist_new_count - handle->filelist_count) * sizeof(HogFileHeader) < handle->header.ea_list_size) {
			// We need the FileList to expand by at least the size of the EAList, otherwise
			//   we can't safely and atomicly resize
			filelist_new_count = handle->filelist_count + 
				(handle->header.ea_list_size + sizeof(HogFileHeader) - 1) / sizeof(HogFileHeader);
		}
	}
	assert((filelist_new_count - handle->filelist_count) * sizeof(HogFileHeader) >= handle->header.ea_list_size ||
		filelist_new_count == handle->filelist_count);

	// Find new EAList size (if it needs to be resized, or is close to needing it)
	ealist_new_count = handle->ealist_count; // Default to not resizing
	ealist_old_used_count = hogFileGetNumUsedEAs(handle);
	if ((handle->ealist_count - ealist_old_used_count) / (F32)ealist_old_used_count < EALIST_PAD_THRESHOLD )
	{
		// EAList could use expanding too!
		ealist_new_count = (U32)(handle->ealist_count * (1 + FILELIST_PAD));
		if (ealist_new_count < handle->ealist_count + 16) {
			// Up it by at least 16 if we're going through the effort!
			ealist_new_count = handle->ealist_count + 16;
		}
	}

	// Try to keep them the same if they're similar
	if (ealist_new_count < filelist_new_count && ABS_UNS_DIFF(handle->ealist_count, handle->filelist_count) < 100)
		ealist_new_count = filelist_new_count;

	if (filelist_new_count < handle->filelist_min)
	{
		filelist_new_count = handle->filelist_min;
	}
	if (ealist_new_count < handle->ealist_min)
	{
		ealist_new_count = handle->ealist_min;
	}

	// Calculate new offset of file data
	filelist_new_size = filelist_new_count * sizeof(HogFileHeader);
	ealist_new_size = ealist_new_count * sizeof(HogEAHeader);
	// Doesn't change: filelist_new_offset = hogFileOffsetOfFileList(handle);
	ealist_new_offset = hogFileOffsetOfFileList(handle) + filelist_new_size;
	data_new_offset = ealist_new_offset + ealist_new_size;

	hoggMemLog( "0x%08p: GrowFileAndEAList: FileList:%d->%d EAList:%d->%d", handle,
		handle->header.file_list_size, filelist_new_size,
		handle->header.ea_list_size, ealist_new_size);

	// Move any files in the way
	for (i=0; i<handle->filelist_count; i++) {
		HogFileListEntry *file_entry = &handle->filelist[i];
		if (file_entry->in_use && !file_entry->queued_for_delete) {
			if (file_entry->header.size) {
				if (file_entry->header.offset < data_new_offset) {
					if (ret = hogFileModifyMoveFilePast(handle, i, data_new_offset))
					{
						hogReleaseMutex(handle, false, false);
						return NESTED_ERROR(1, ret);
					}
				}
			}
		}
	}

	mod = callocHogFileMod();
	mod->type = HFM_FILELIST_RESIZE;
	mod->filelist_resize.old_ealist_pos = hogFileOffsetOfEAList(handle);
	mod->filelist_resize.new_ealist_pos = (U32)ealist_new_offset;
	assert(ealist_new_size <= UINT_MAX);
	mod->filelist_resize.new_ealist_size = (U32)ealist_new_size;
	assert(filelist_new_size <= UINT_MAX);
	mod->filelist_resize.new_filelist_size = (U32)filelist_new_size;
	mod->filelist_resize.old_ealist_size = handle->header.ea_list_size;

	// Modify in-memory structures with new data
	// Add empty slots	
	assert(filelist_new_count <= UINT_MAX);
	hogGrowFileList(handle, (U32)filelist_new_count); // Modifies handle->header.file_list_size
	assert(ealist_new_count <= UINT_MAX);
	hogGrowEAList(handle, (U32)ealist_new_count); // Modifies handle->header.ea_list_size
	mod->filelist_resize.new_ealist_data = hogSerializeEAHeaders(handle); // Freed in thread!

	assert(hogFileOffsetOfEAList(handle) == mod->filelist_resize.new_ealist_pos);

	// Send packet
	hogFileAddMod(handle, mod, (U32)ealist_new_size);

	return 0;
}

static int just_grew_filelist=0;
static int hogFileModifyFitFileList(HogFile *handle)
{
	IN_CRITICAL(data_access);
	if (eaiSize(&handle->file_free_list)) {
		just_grew_filelist = 0;
		return 0; // Has an empty slot
	}
	hoggMemLog( "0x%08p: FitFileList:Growing", handle);
	assert(eaiSize(&handle->file_free_list)==0);
	just_grew_filelist = 1;
	return hogFileModifyGrowFileAndEAList(handle);
}

static int hogFileModifyFitEAList(HogFile *handle)
{
	IN_CRITICAL(data_access);
	if (eaiSize(&handle->ea_free_list)) {
		return 0; // Has an empty slot
	}
	hoggMemLog( "0x%08p: FitEAList:Growing", handle);
	assert(eaiSize(&handle->ea_free_list)==0);
	return hogFileModifyGrowFileAndEAList(handle);
}

static void hogFileModifyGrowToAtLeast(HogFile *handle, U64 newFileSize)
{
	HogFileMod *mod;
	hogAcquireMutex(handle);

	ENTER_CRITICAL(data_access);

	if (newFileSize > handle->file_size)
	{
		// Grow file size in at least 10% chunk!
		if ((newFileSize - handle->file_size) / (F32)handle->file_size > 0.1)
		{
			// Already more than 10%, just do this
			handle->file_size = newFileSize;
		} else {
			// Less than 10%, do 10% instead
			handle->file_size *= 1.10;
			if (handle->file_size < newFileSize) // In case of floating point error
				handle->file_size = newFileSize;
		}

		// Send mod packet
		hoggMemLog( "0x%08p: Mod:GrowTo:%"FORM_LL"d", handle,
			handle->file_size);
		mod = callocHogFileMod();
		mod->type = HFM_GROW;
		mod->grow.newsize = handle->file_size;
		hogFileAddMod(handle, mod, COMMAND_BYTE_SIZE);
	} else {
		hogReleaseMutex(handle, false, false);
	}

	LEAVE_CRITICAL(data_access);
}

void hogVerifyCRCs(NewPigEntry *entry)
{
	if (entry->size)
	{
		U32 autocrc[4];
		if (entry->pack_size)
		{
			U8 *buf = ScratchAlloc(entry->size);
			U32 outsize = entry->size;
			int ret = unzipDataEx(buf, &outsize, entry->data, entry->pack_size, true);
			assert(ret==0);
			cryptMD5Update(buf,entry->size);
			cryptMD5Final(autocrc);
			assert(autocrc[0] == entry->checksum[0]);
			ScratchFree(buf);
		} else {
			cryptMD5Update(entry->data,entry->size);
			cryptMD5Final(autocrc);
			assert(autocrc[0] == entry->checksum[0]);
		}
	}
}

// Note: takes ownership of data
static int hogFileModifyAdd(HogFile *handle, NewPigEntry *entry)
{
	const char *name = entry->fname;
	U32 size = entry->pack_size?entry->pack_size:entry->size;
	U32 timestamp = entry->timestamp;
	const U8 *header_data = entry->header_data;
	U32 header_data_size = entry->header_data_size;
	U32 unpacksize = entry->pack_size?entry->size:0;
	U32 *crc = entry->checksum;
	HogFileMod *mod;
	int file_index;
	int ea_index;
	HogFileListEntry *file_entry;
	HogEAListEntry *ea_entry;
	int ret=0;
	U64 offset=0;
	S32 name_id=HOG_NO_VALUE;
	S32 header_data_id=HOG_NO_VALUE;
	DataListJournal dlj = {0};
#define FAIL(code) { ret = code; goto fail; }

	hoggMemLog( "0x%08p: hogFileModifyAdd: %s data:%08p size:%d", handle, name, entry->data, size);

	hogAcquireMutex(handle);

	ENTER_CRITICAL(data_access);

	PERFINFO_AUTO_START("ResizeLists",1);

	// Verify FileList/EAList can fit one more, resize if necessary
	if (ret=hogFileModifyFitFileList(handle))
	{
		hogReleaseMutex(handle, false, false);
		FAIL(NESTED_ERROR(1, ret));
	}
	if (ret=hogFileModifyFitEAList(handle))
	{
		hogReleaseMutex(handle, false, false);
		FAIL(NESTED_ERROR(2, ret));
	}

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("DataList",1);

	// Modify DataList, send mod packet
	name_id = DataListAdd(&handle->datalist, name, (int)strlen(name)+1, true, &dlj);
	name = DataListGetString(&handle->datalist, name_id, true);
	if (header_data) {
		header_data_id = DataListAdd(&handle->datalist, header_data, header_data_size, false, &dlj);
	}
	if (ret=hogFileAddDataListMod(handle, &dlj))
	{
		hogReleaseMutex(handle, false, false);
		FAIL(NESTED_ERROR(3, ret));
	}

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("SetupFile",1);

	// Modify in-memory structures to point to new file
	file_index = hogFileCreateFileEntry(handle);
	file_entry = hogGetFileListEntry(handle, file_index);
	file_entry->header.size = size;
	file_entry->header.timestamp = timestamp;
	file_entry->header.checksum = crc[0];

	if (hog_verify_crcs)
		hogVerifyCRCs(entry);

	if (!(handle->create_flags & HOG_NO_ACCESS_BY_NAME))
		assert(storeNameToId(handle,name,file_index)); // Shallow copied name from DataList, asserts if duplicate

	ea_index = hogFileCreateEAEntry(handle, file_index);
	ea_entry = hogGetEAListEntry(handle, ea_index);
	ea_entry->header.unpacked_size = unpacksize;
	ea_entry->header.name_id = name_id;
	ea_entry->header.header_data_id = header_data_id;

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("FindSpace",1);

	// Find empty space (need file index/name to be set first)
	if (size) {
		offset = hogFileModifyFitFile(handle, size, hogFileOffsetOfData(handle), file_index);
		if (offset==0)
		{
			hogReleaseMutex(handle, false, false);
			FAIL(1);
		}
	}
	file_entry->header.offset = offset;
	if (offset + size > handle->file_size)
	{
		hogFileModifyGrowToAtLeast(handle, offset+size);
	}

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("SendMod",1);

	// Send mod packet (must be after datalist mod in order to not leave nameless files)
	hoggMemLog( "0x%08p: Mod:Add file %d Offs:%"FORM_LL"d, size:%d", handle,
		file_index, offset, size);
	mod = callocHogFileMod();
	mod->type = HFM_ADD;
	tfcRecordMemoryUsed(size);
	mod->addOrUpdate.file_index = file_index;
	mod->addOrUpdate.size = size;
	mod->addOrUpdate.timestamp = timestamp;
	mod->addOrUpdate.checksum = file_entry->header.checksum;
	mod->addOrUpdate.headerdata.flagFFFE = 0xFFFE;
	mod->addOrUpdate.headerdata.ea_id = ea_index;
	mod->addOrUpdate.ea_data.unpacksize = unpacksize;
	mod->addOrUpdate.ea_data.name_id = name_id;
	mod->addOrUpdate.ea_data.header_data_id = header_data_id;
	mod->addOrUpdate.offset = offset;
	mod->addOrUpdate.data = entry->data; // Freed in thread
	entry->data = NULL;
	mod->free_callback = entry->free_callback;
	hogDirtyFile(handle, file_index);
	hogFileAddMod(handle, mod, size+1);

	hogFileCallCallback(handle, name, unpacksize ? unpacksize : size, timestamp, file_index);

	PERFINFO_AUTO_STOP();

fail:
	LEAVE_CRITICAL(data_access);
	hogFileModifyCheckForFlush(handle);
	return ret;
#undef FAIL
}

// Note: takes ownership of data
static int hogFileModifyAdd2(HogFile *handle, NewPigEntry *entry)
{
	int ret;
	if (!hog_mode_no_data)
		hogChecksumAndPackEntry(entry);
	if (hogFileFindAndLockDA(handle, entry->fname)!=HOG_INVALID_INDEX)
	{
		LEAVE_CRITICAL(data_access);
		return -1;
	}
	ret = hogFileModifyAdd(handle, entry);
	LEAVE_CRITICAL(data_access);
	hogFileModifyCheckForFlush(handle);
	return ret;
}

// Note: takes ownership of entry->data
static int hogFileModifyUpdate(HogFile *handle, HogFileIndex file, NewPigEntry *entry)
{
	int ret=0;
	U64 offset=0;
	HogFileMod *mod;
	HogFileListEntry *file_entry;
	HogEAListEntry *ea_entry = NULL;
	S32 name_id=HOG_NO_VALUE;
	S32 new_header_data_id=HOG_NO_VALUE;
	S32 old_header_data_id=HOG_NO_VALUE;
	U32 on_disk_size;
	U32 unpack_size;
#define FAIL(code) { ret = code; goto fail; }

	assert(isValidHogHandle(handle));

	hogChecksumAndPackEntry(entry);
	on_disk_size = entry->pack_size?entry->pack_size:entry->size;
	unpack_size = entry->pack_size?entry->size:0;

	hogAcquireMutex(handle);

	ENTER_CRITICAL(data_access);

	file_entry = hogGetFileListEntry(handle, file);
	if (file_entry->header.headerdata.flagFFFE == 0xFFFE && file_entry->header.headerdata.ea_id != HOG_NO_VALUE) {
		ea_entry = hogGetEAListEntry(handle, (S32)file_entry->header.headerdata.ea_id);
		old_header_data_id = ea_entry->header.header_data_id;
	} else {
		assert(!entry->pack_size); // Cannot be compressed if no EA for it
	}

	// Update header data
	if (entry->header_data) {
		DataListJournal dlj = {0};
		assert(ea_entry);
		// Add new data list entry (must be before modifying ea_entry on disk or in memory)
		new_header_data_id = DataListAdd(&handle->datalist, entry->header_data, entry->header_data_size, false, &dlj);
		if (ret=hogFileAddDataListMod(handle, &dlj))
		{
			hogReleaseMutex(handle, false, false);
			FAIL(NESTED_ERROR(3, ret));
		}
	}

	// Find location for file
	if (on_disk_size)
		offset = hogFileModifyFitFile(handle, on_disk_size, hogFileOffsetOfData(handle), file);

	// Free up previous space
	hogFileFreeSpace(handle, file_entry->header.offset, file_entry->header.size);

	if (offset + on_disk_size > handle->file_size)
	{
		hogFileModifyGrowToAtLeast(handle, offset+on_disk_size);
	}

	hoggMemLog( "0x%08p: Mod:Update file %d, Offs:%"FORM_LL"d->%"FORM_LL"d, size:%d->%d", handle,
		file, file_entry->header.offset, offset,
		file_entry->header.size, on_disk_size);

	if (hog_verify_crcs)
		hogVerifyCRCs(entry);

	// Modify in-memory header
	file_entry->header.checksum = entry->checksum[0];
	file_entry->header.offset = offset;
	file_entry->header.size = on_disk_size;
	file_entry->header.timestamp = entry->timestamp;
	if (ea_entry) {
		ea_entry->header.unpacked_size = unpack_size;
		name_id = ea_entry->header.name_id;
		ea_entry->header.header_data_id = new_header_data_id;
	}

	// Send modification packet
	mod = callocHogFileMod();
	mod->type = HFM_UPDATE;
	mod->addOrUpdate.file_index = file;
	mod->addOrUpdate.size = on_disk_size;
	mod->addOrUpdate.timestamp = entry->timestamp;
	mod->addOrUpdate.headerdata = file_entry->header.headerdata;
	mod->addOrUpdate.checksum = file_entry->header.checksum;
	mod->addOrUpdate.offset = offset;
	mod->addOrUpdate.ea_data.unpacksize = unpack_size;
	mod->addOrUpdate.ea_data.name_id = name_id;
	mod->addOrUpdate.ea_data.header_data_id = new_header_data_id;
	mod->addOrUpdate.data = entry->data; // Freed in thread
	mod->free_callback = entry->free_callback;
	hogDirtyFile(handle, file);
	hogFileAddMod(handle, mod, on_disk_size+1);

	// Remove old header data if any (must be after modifying the ea_entry on disk)
	if (old_header_data_id != HOG_NO_VALUE) {
		DataListJournal dlj = {0};
		DataListFree(&handle->datalist, old_header_data_id, &dlj);
		if (ret=hogFileAddDataListMod(handle, &dlj))
			FAIL(NESTED_ERROR(4, ret));
	}

	if (!hogFileIsSpecialFile(handle, file))
		hogFileCallCallback(handle, hogFileGetFileName(handle, file), unpack_size?unpack_size:on_disk_size, entry->timestamp, file);

fail:
	LEAVE_CRITICAL(data_access);
	hogFileModifyCheckForFlush(handle);
	return ret;
#undef FAIL
}

// Note: takes ownership of entry->data
// Note: entry->fname need not be valid after exiting (can be a stack variable)
// Pass in NULL data for a delete
int hogFileModifyUpdateNamedSync2(HogFile *handle, NewPigEntry *entry)
{
	int ret;
	HogFileIndex index;

	assert(isValidHogHandle(handle));
	assert(!handle->read_only);
	assert(!(handle->create_flags & HOG_RAM_CACHED));

	if (!hog_mode_no_data)
		hogChecksumAndPackEntry(entry);

	// Have to hold the mutex while making any decisions regarding an index and filename already existing
	hogAcquireMutex(handle);

	index = hogFileFindAndLockDA(handle, entry->fname);

	if (index == HOG_INVALID_INDEX) {
		// New!
		if (entry->data) {
			ret = hogFileModifyAdd2(handle, entry);
		} else {
			// Deleting something already deleted?!
			// Can happen on funny things like v_pwrbox, a file that is deleted, but a folder named that exists
			ret = 0;
		}
	} else {
		if (entry->data) {
			// Update
			hoggMemLog( "0x%08p: hogFileModifyUpdateNamedSync2:hogFileModifyUpdate: %s data:%08p size:%d  index = %d", handle, entry->fname, entry->data, entry->size, index);
			ret = hogFileModifyUpdate(handle, index, entry);
		} else {
			// Delete
			hoggMemLog( "0x%08p: hogFileModifyUpdateNamedSync2:hogFileModifyDelete: %s data:%08p size:%d  index = %d", handle, entry->fname, entry->data, entry->size, index);
			handle->auto_release_data_access = 1;
			ret = hogFileModifyDelete(handle, index);
		}
	}
	LEAVE_CRITICAL(data_access);
	hogReleaseMutex(handle, false, false);
	hogFileModifyCheckForFlush(handle);
	return ret;
}

// Note: takes ownership of data
// Pass in NULL data for a delete
int hogFileModifyUpdateNamedSync(HogFile *handle, const char *relpath, U8 *data, U32 size, U32 timestamp, DataFreeCallback free_callback)
{
	NewPigEntry entry = {0};
	entry.data = data;
	entry.fname = (char*)relpath;
	entry.size = size;
	entry.timestamp = timestamp;
	entry.free_callback = free_callback;
	return hogFileModifyUpdateNamedSync2(handle, &entry);
}

int hogFileModifyUpdateTimestamp(HogFile *handle, HogFileIndex file, U32 timestamp)
{
	HogFileMod *mod;
	HogFileListEntry *file_entry;
	HogEAListEntry *ea_entry = NULL;
	U32 size;
	U32 unpacksize=0;

	hogAcquireMutex(handle);

	ENTER_CRITICAL(data_access);

	file_entry = hogGetFileListEntry(handle, file);
	if (!file_entry)
	{
		hoggMemLog( "0x%08p: Mod:UpdateTimestamp file %d DOES NOT EXIST", handle,
			file);
		LEAVE_CRITICAL(data_access);
		hogReleaseMutexAsync(handle, false);
		return 1;
	}

	if ((U32)file_entry->header.timestamp == timestamp)
	{
		// do nothing if the timestamp is identical (happens with dynamic patching multiple clients at once)
		hoggMemLog( "0x%08p: Mod:UpdateTimestamp file %d timestamp identical (%d)", handle,
			file, timestamp);
		LEAVE_CRITICAL(data_access);
		hogReleaseMutexAsync(handle, false);
		return 0;
	}

	hoggMemLog( "0x%08p: Mod:UpdateTimestamp file %d, timestamp:%d->%d", handle,
		file, file_entry->header.timestamp, timestamp);

	if (file_entry->header.headerdata.flagFFFE == 0xFFFE && file_entry->header.headerdata.ea_id != HOG_NO_VALUE)
	{
		ea_entry = hogGetEAListEntry(handle, (S32)file_entry->header.headerdata.ea_id);
		unpacksize = ea_entry->header.unpacked_size;
	}
	size = file_entry->header.size;

	// Modify in-memory offset
	file_entry->header.timestamp = timestamp;

	// Send modification packet
	mod = callocHogFileMod();
	mod->type = HFM_UPDATE;
	mod->addOrUpdate.file_index = file;
	mod->addOrUpdate.size = file_entry->header.size;
	mod->addOrUpdate.timestamp = file_entry->header.timestamp;
	mod->addOrUpdate.headerdata = file_entry->header.headerdata;
	mod->addOrUpdate.checksum = file_entry->header.checksum;
	mod->addOrUpdate.offset = file_entry->header.offset;
	mod->addOrUpdate.ea_data.unpacksize = ea_entry?ea_entry->header.unpacked_size:0;
	mod->addOrUpdate.ea_data.name_id = ea_entry?ea_entry->header.name_id:HOG_NO_VALUE;
	mod->addOrUpdate.ea_data.header_data_id = ea_entry?ea_entry->header.header_data_id:HOG_NO_VALUE;
	mod->addOrUpdate.data = NULL;
	hogFileAddMod(handle, mod, COMMAND_BYTE_SIZE);

	// Do a callback, might be called by the patcher
	hogFileCallCallback(handle, hogFileGetFileName(handle, file), unpacksize?unpacksize:size, timestamp, file);
	LEAVE_CRITICAL(data_access);

	hogFileModifyCheckForFlush(handle);
	return 0;
}


static ManagedThread *hog_thread_ptr;

static VOID CALLBACK hogThreadingHasWorkFunc( ULONG_PTR dwParam);

bool g_doHogTiming=false;

static DWORD WINAPI hogThreadingThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
		if (g_doHogTiming)
			timerRecordStart("C:/FightClub/profiler/hogg.profiler");
		PERFINFO_AUTO_START("hogThreadingThread", 1);
			for(;;)
			{
				// Because we need a "frame" to be a single call of hogThreadingHasWorkFunc, we can't have timers around Sleep
#pragma push_macro("SleepEx")
#undef SleepEx
				SleepEx(INFINITE, TRUE);
#pragma pop_macro("SleepEx")
			}
		PERFINFO_AUTO_STOP();
		return 0; 
	EXCEPTION_HANDLER_END
}

void hogThreadingInit(void)
{
	// Creates a thread to handle hog modifications
	if (!hog_thread_ptr)
	{
		hog_thread_ptr = tmCreateThread(hogThreadingThread, NULL);
		assert(hog_thread_ptr);
		tmSetThreadAllowSync(hog_thread_ptr, false);
		tmSetThreadProcessorIdx(hog_thread_ptr, THREADINDEX_DATASTREAMING);
	}
	hogSetAsyncThread(NULL);
}

static VOID CALLBACK stopTimingFunc( ULONG_PTR dwParam)
{
	if (g_doHogTiming)
		timerRecordEnd();
	g_doHogTiming = false;
}

void hogThreadingStopTiming(void)
{
	if (g_doHogTiming && hog_thread_ptr) {
		tmQueueUserAPC(stopTimingFunc, hog_thread_ptr, (ULONG_PTR)0 );
		while(g_doHogTiming)
			Sleep(100);
	}
}

static void hogThreadHasWork(HogFile *handle)
{
	InterlockedIncrement(&handle->async_operation_count);
	InterlockedIncrement(&total_async_operation_count);
	tmQueueUserAPC(hogThreadingHasWorkFunc, hog_thread_ptr, (ULONG_PTR)handle );
	if (!handle->single_app_mode)
		SetEvent(handle->starting_flush_event); // Wake up the thread if it's sleeping
}

int hogFileModifyFlush(HogFile *handle)
{
	int ret=0;
	assert(isValidHogHandle(handle));
	ENTER_CRITICAL(doing_flush); // Multiple people may lock of both waiting on the same event
	SetEvent(handle->starting_flush_event);
	do {
		ENTER_CRITICAL(data_access);
		assert(handle->debug_in_data_access==1); // Can't be in the critical when entering this function
		if (!hogFileAnyOperationsQueued(handle))
		{
			// Nothing left to do!
			ret = handle->last_threaded_error;
			handle->last_threaded_error = 0;
			LEAVE_CRITICAL(data_access);
			break;
		}
		LEAVE_CRITICAL(data_access);
		// Wait on event
        WaitForEvent(handle->done_doing_operation_event, INFINITE);
	} while (true);
	LEAVE_CRITICAL(doing_flush);
	return ret;
}

static DWORD WINAPI hogAsyncThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
			for(;;)
			{
				autoTimerThreadFrameBegin("hogAsyncThread");
				SleepEx(INFINITE, TRUE);
				autoTimerThreadFrameEnd();
			}
		return 0; 
	EXCEPTION_HANDLER_END
}

static ManagedThread *hog_update_thread_ptr;

void hogSetAsyncThread(ManagedThread *thread)
{
	if (hog_update_thread_ptr)
	{
		// Already have a thread
		assert(!thread);
		return;
	}
	if (thread)
	{
		hog_update_thread_ptr = thread;
	} else {
		hog_update_thread_ptr = tmCreateThread(hogAsyncThread, NULL);
		assert(hog_update_thread_ptr);
		tmSetThreadProcessorIdx(hog_update_thread_ptr, THREADINDEX_DATASTREAMING);
	}
}

typedef struct QueuedNewPigEntry
{
	HogFile *handle;
	HogFileIndex index;
	NewPigEntry entry;
} QueuedNewPigEntry;

TSMP_DEFINE(QueuedNewPigEntry);

QueuedNewPigEntry *callocQueuedNewPigEntry()
{
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(QueuedNewPigEntry, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	return TSMP_CALLOC(QueuedNewPigEntry);
}

void safefreeQueuedNewPigEntry(QueuedNewPigEntry **p)
{
	if(!p || !*p)
		return;

	TSMP_FREE(QueuedNewPigEntry, *p);
	*p = NULL;
}

void APIENTRY hogFileModifyUpdateAsyncCallback(ULONG_PTR dwParam)
{
	QueuedNewPigEntry *queued = (QueuedNewPigEntry *)dwParam;
	HogFile *handle = queued->handle;
	U32 size = queued->entry.size;
	bool bQueuedForDelete=false;

	PERFINFO_AUTO_START_FUNC();

	hogChecksumAndPackEntry(&queued->entry);

	// Deletes shouldn't be queued up asynchronously
	assert(queued->entry.data);

	ENTER_CRITICAL(file_access);
	ENTER_CRITICAL(data_access);

	handle->mod_list_size--;
	handle->mod_list_byte_size-=size;
	hogReleaseMutex(handle, true, true); // JE: I think this does not need needsFlush = true, it has not written anything, this will cause extra flushes

	if (queued->index == HOG_INVALID_INDEX) {
		int oldvalue=0;
		ENTER_CRITICAL(data_access);
		assert(handle->async_new_files);
		assert(stashFindInt(handle->async_new_files, queued->entry.fname, &oldvalue));
		assert(oldvalue > 0);
		if (oldvalue > 1) {
			stashAddInt(handle->async_new_files, queued->entry.fname, oldvalue-1, true);
		} else {
			stashRemoveInt(handle->async_new_files, queued->entry.fname, NULL);
		}
		LEAVE_CRITICAL(data_access);
	} else {
		hogUnDirtyFile(queued->handle, queued->index);
		assert(queued->handle->filelist[queued->index].dirty_async > 0);
		queued->handle->filelist[queued->index].dirty_async--;
		if (queued->handle->filelist[queued->index].queued_for_delete)
		{
			bQueuedForDelete = true;
			// Unflag this so the interior functions do not assert, it's okay
			//  that we're modifying this despite being queued for deletion
			//  because it's probably being queued for delete in another thread
			//  right now that's waiting on us to finish this update
			queued->handle->filelist[queued->index].queued_for_delete = 0;
		}

	}

	if (0!=hogFileModifyUpdateNamedSync2(queued->handle, &queued->entry)) {
		// Error, just free the memory?
		SAFE_FREE(queued->entry.data);
	}

	if (bQueuedForDelete)
	{
		// restore bit
		queued->handle->filelist[queued->index].queued_for_delete = 1;
	}

	LEAVE_CRITICAL(data_access);
	LEAVE_CRITICAL(file_access);

	safefreeQueuedNewPigEntry(&queued);

	PERFINFO_AUTO_STOP_FUNC();
}

int hogFileModifyUpdateNamedAsync2(HogFile *handle, NewPigEntry *entry)
{
	HogFileIndex index;
	QueuedNewPigEntry *queued;
	const char *fname;

	assert(isValidHogHandle(handle));
	assert(!handle->read_only);
	assert(!(handle->create_flags & HOG_RAM_CACHED));
	if(!entry->no_devassert) 
		devassert(!entry->pack_size || entry->checksum[0]); // pre-packed data must have a checksum! But, perhaps it's possible for an actual checksum to be 0?

	if (!entry->data) {
		// Deletes are fast, and do not have the logic to have hogFileFind return a consistent
		//  result while the delete is going on, so just delete in the foregrounds
		return hogFileModifyUpdateNamedSync2(handle, entry);
	}

	fname = allocAddFilename(entry->fname);
	queued = callocQueuedNewPigEntry();

	hogAcquireMutex(handle);

	index = hogFileFindAndLockDA(handle, fname);

	InterlockedIncrement(&handle->async_operation_count);
	InterlockedIncrement(&total_async_operation_count);

	if (index == HOG_INVALID_INDEX) {
		// New file
		int oldvalue=0;
		assert(handle->async_new_files);
		stashFindInt(handle->async_new_files, fname, &oldvalue);
		stashAddInt(handle->async_new_files, fname, oldvalue+1, true);
	} else {
		// Existing file, flag it as dirty
		hogDirtyFile(handle, index);
		handle->filelist[index].dirty_async++;
	}

	handle->mod_list_size++;
	handle->mod_list_byte_size+=entry->size;
	LEAVE_CRITICAL(data_access);

	queued->handle = handle;
	queued->index = index;
	queued->entry = *entry;
	queued->entry.fname = fname;
	tmQueueUserAPC(hogFileModifyUpdateAsyncCallback, hog_update_thread_ptr, (ULONG_PTR)queued);

	hogFileModifyCheckForFlush(handle);
	return 0;
}

int hogFileModifyUpdateNamedAsync(HogFile *handle, const char *relpath, U8 *data, U32 size, U32 timestamp, DataFreeCallback free_callback)
{
	NewPigEntry entry = {0};
	entry.fname = relpath;
	entry.timestamp = timestamp;
	entry.data = data;
	entry.size = size;
	entry.free_callback = free_callback;
	return hogFileModifyUpdateNamedAsync2(handle, &entry);
}



//////////////////////////////////////////////////////////////////////////
// In-thread: Functions to actually apply the modifications queued up from above
//    Must handle Endianness while writing to disk
//////////////////////////////////////////////////////////////////////////

static int hogFileModifyDoDeleteInternal(HogFile *handle, HogFileMod *mod)
{
	U64 offs;
	HogFileHeader file_header = {0};
	int ret;
	PERFINFO_AUTO_START("hogFileModifyDoDeleteInternal",1);

	// zero FileList entry
	file_header.size = HOG_NO_VALUE;
	offs = mod->filelist_offset + sizeof(HogFileHeader)*mod->del.file_index;
	endianSwapHogFileHeaderIfBig(&file_header);
	if (ret=checkedWriteData(handle, &file_header, sizeof(file_header), offs)) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(1, ret);
	}

	if (mod->del.ea_id != HOG_NO_VALUE) {
		// zero EAList entry
		HogEAHeader ea_header = {0};
		ea_header.flags = HOGEA_NOT_IN_USE;
		offs = mod->ealist_offset + sizeof(HogEAHeader)*mod->del.ea_id;
		endianSwapHogEAHeaderIfBig(&ea_header);
		if (ret=checkedWriteData(handle, &ea_header, sizeof(ea_header), offs)) {
			PERFINFO_AUTO_STOP();
			return NESTED_ERROR(2, ret);
		}

	}
	PERFINFO_AUTO_STOP();
	return 0;
}

static int hogFileModifyDoDelete(HogFile *handle, HogFileMod *mod)
{
	HFJDelete journal_entry = {0};
	int ret;
	HogFileListEntry *file_entry;
	PERFINFO_AUTO_START("hogFileModifyDoDelete",1);

	ENTER_CRITICAL(data_access);
	// Push unused handles into the free list (could not do in calling thread because there
	//   may have still been updates en route that need to touch the dirty bits, etc).
	assert(mod->del.file_index>=0 && mod->del.file_index<handle->filelist_count);
	file_entry = &handle->filelist[mod->del.file_index];
	assert(file_entry->in_use);
	assert(file_entry->queued_for_delete);
	assert(!file_entry->dirty);
	assert(!file_entry->dirty_async);
	ZeroStruct(file_entry); // Zeroes queued_for_delete
	eaiPush(&handle->file_free_list, mod->del.file_index+1);
	handle->num_files--; // Must be kept with pushing into free list
	if (mod->del.ea_id != HOG_NO_VALUE) {
		eaiPush(&handle->ea_free_list, mod->del.ea_id+1);
	}
	LEAVE_CRITICAL(data_access);

	// Journal Delete operation - recovery must delete file and modify the ?DataList
	journal_entry.del = mod->del;
	endianSwapStructIfBig(parseHFMDelete, &journal_entry.del);
	journal_entry.type = endianSwapIfBig(U32, HOJ_DELETE);
	if (ret=hogJournalJournal(handle, &journal_entry, sizeof(journal_entry))) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(1, ret);
	}

	if (ret=hogFileModifyDoDeleteInternal(handle, mod)) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(2, ret);
	}

	// clear ops journal
	if (ret=hogJournalReset(handle)) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(3, ret);
	}

	PERFINFO_AUTO_STOP();
	return 0;
}


static int hogFileModifyDoAddOrUpdateInternal(HogFile *handle, HogFileMod *mod)
{
	U64 offs;
	HogFileHeader file_header = {0};
	int ret;

	PERFINFO_AUTO_START("hogFileModifyDoAddInternal",1);

	// Fill in FileList entry
	file_header.offset = mod->addOrUpdate.offset;
	file_header.size = mod->addOrUpdate.size;
	file_header.timestamp = mod->addOrUpdate.timestamp;
	file_header.headerdata = mod->addOrUpdate.headerdata;
	file_header.checksum = mod->addOrUpdate.checksum;
	// Write it
	offs = mod->filelist_offset + sizeof(HogFileHeader)*mod->addOrUpdate.file_index;
	endianSwapHogFileHeaderIfBig(&file_header);
	if (ret=checkedWriteData(handle, &file_header, sizeof(file_header), offs)) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(1, ret);
	}

	if (mod->addOrUpdate.headerdata.flagFFFE == 0xFFFE && mod->addOrUpdate.headerdata.ea_id != HOG_NO_VALUE) {
		// Fill in EAList entry
		HogEAHeader ea_header = {0};
		ea_header.unpacked_size = mod->addOrUpdate.ea_data.unpacksize;
		ea_header.name_id = mod->addOrUpdate.ea_data.name_id;
		ea_header.header_data_id = mod->addOrUpdate.ea_data.header_data_id;
		offs = mod->ealist_offset + sizeof(HogEAHeader)*mod->addOrUpdate.headerdata.ea_id;
		endianSwapHogEAHeaderIfBig(&ea_header);
		if (ret=checkedWriteData(handle, &ea_header, sizeof(ea_header), offs)) {
			PERFINFO_AUTO_STOP();
			return NESTED_ERROR(2, ret);
		}
	}
	PERFINFO_AUTO_STOP();
	return 0;
}

static int hogFileModifyDoAdd(HogFile *handle, HogFileMod *mod)
{
	HFJAddOrUpdate journal_entry = {0};
	int ret;

	PERFINFO_AUTO_START("hogFileModifyDoAdd",1);

	// Write data
	if (!hog_mode_no_data) {
		if (mod->addOrUpdate.size) {
			if (hog_verify_crcs)
			{
				NewPigEntry entry = {0};
				entry.checksum[0] = mod->addOrUpdate.checksum;
				entry.pack_size = mod->addOrUpdate.ea_data.unpacksize?mod->addOrUpdate.size:0;
				entry.size = mod->addOrUpdate.ea_data.unpacksize?mod->addOrUpdate.ea_data.unpacksize:mod->addOrUpdate.size;
				entry.data = mod->addOrUpdate.data;
				hogVerifyCRCs(&entry);
			}

			enterFileWriteCriticalSection();
			assert(mod->addOrUpdate.offset);
			if (ret=checkedWriteData(handle, mod->addOrUpdate.data, mod->addOrUpdate.size, mod->addOrUpdate.offset)) {
				leaveFileWriteCriticalSection();
				PERFINFO_AUTO_STOP();
				return NESTED_ERROR(1, ret);
			}
			hogFflush(handle); // Need to make sure data is written before updating the header to point to it!
			leaveFileWriteCriticalSection();
		}
	}

	// Journal Add operation - recovery can fill in file_list, [ea_list] (since data is there and good)
	journal_entry.type = endianSwapIfBig(U32,HOJ_ADD_OR_UPDATE);
	journal_entry.addOrUpdate = mod->addOrUpdate;
	journal_entry.addOrUpdate.data = NULL;
	endianSwapStructIfBig(parseHFMAddOrUpdate, &journal_entry.addOrUpdate);
	if (ret=hogJournalJournal(handle, &journal_entry, sizeof(journal_entry))) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(2, ret);
	}

	// Edit file_list, [ea_list]
	if (ret=hogFileModifyDoAddOrUpdateInternal(handle, mod)) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(3, ret);
	}

	// Clear journal
	if (ret=hogJournalReset(handle)) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(4, ret);
	}
	hogUnDirtyFile(handle, mod->addOrUpdate.file_index);

	PERFINFO_AUTO_STOP();
	return 0;
}

static int hogFileModifyDoUpdate(HogFile *handle, HogFileMod *mod)
{
	int ret;
	HFJAddOrUpdate journal_entry = {0};

	PERFINFO_AUTO_START("hogFileModifyDoUpdate",1);

	// Write data
	if (mod->addOrUpdate.data)
	{
		if (hog_verify_crcs)
		{
			NewPigEntry entry = {0};
			entry.checksum[0] = mod->addOrUpdate.checksum;
			entry.pack_size = mod->addOrUpdate.ea_data.unpacksize?mod->addOrUpdate.size:0;
			entry.size = mod->addOrUpdate.ea_data.unpacksize?mod->addOrUpdate.ea_data.unpacksize:mod->addOrUpdate.size;
			entry.data = mod->addOrUpdate.data;
			hogVerifyCRCs(&entry);
		}

		enterFileWriteCriticalSection();
		if (ret=checkedWriteData(handle, mod->addOrUpdate.data, mod->addOrUpdate.size, mod->addOrUpdate.offset)) {
			leaveFileWriteCriticalSection();
			return NESTED_ERROR(1, ret);
		}
		hogFflush(handle); // Need to make sure data is written before updating the header to point to it!
		leaveFileWriteCriticalSection();
	}

	// Journal Update operation - recovery can fill in file_list, [ea_list] (since data is there and good)
	journal_entry.type = endianSwapIfBig(U32,HOJ_ADD_OR_UPDATE);
	journal_entry.addOrUpdate = mod->addOrUpdate;
	journal_entry.addOrUpdate.data = NULL;
	endianSwapStructIfBig(parseHFMAddOrUpdate, &journal_entry.addOrUpdate);
	if (ret=hogJournalJournal(handle, &journal_entry, sizeof(journal_entry))) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(2, ret);
	}

	// Edit file_list [and ea_list]
	if (ret=hogFileModifyDoAddOrUpdateInternal(handle, mod)) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(3, ret);
	}

	// Clear journal
	if (ret=hogJournalReset(handle)) {
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(4, ret);
	}

	if (mod->addOrUpdate.data)
		hogUnDirtyFile(handle, mod->addOrUpdate.file_index);
	
	PERFINFO_AUTO_STOP();
	return 0;
}

static int hogFileModifyDoMoveInternal(HogFile *handle, HogFileMod *mod)
{
	int ret;
	U64 offset = mod->filelist_offset + sizeof(HogFileHeader)*mod->move.file_index + offsetof(HogFileHeader, offset);
	U64 data_to_write = endianSwapIfBig(U64,mod->move.new_offset);
	STATIC_ASSERT(sizeof(data_to_write) == SIZEOF2(HogFileHeader, offset));
	PERFINFO_AUTO_START("hogFileModifyDoMoveInternal",1);
	if (ret=checkedWriteData(handle, &data_to_write, sizeof(data_to_write), offset))
	{
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(1, ret);
	}
	PERFINFO_AUTO_STOP();
	return 0;
}

static int hogFileModifyDoMove(HogFile *handle, HogFileMod *mod)
{
	HFJMove journal_entry = {0};
	U8 *data=NULL;
	int ret=0;
#define FAIL(code) { ret = NESTED_ERROR(code, ret); goto cleanup; }
	PERFINFO_AUTO_START("hogFileModifyDoMove",1);
	if (!hog_mode_no_data) {
		// read data
		data = malloc(mod->move.size);
		if (ret=checkedReadData(handle, data, mod->move.size, mod->move.old_offset))
			FAIL(1);

		// write data
		if (ret=checkedWriteData(handle, data, mod->move.size, mod->move.new_offset))
			FAIL(2);
		hogFflush(handle); // Need to make sure data is written before updating the header to point to it!
	}

	// journal header change
	journal_entry.type = endianSwapIfBig(U32,HOJ_MOVE);
	journal_entry.move = mod->move;
	endianSwapStructIfBig(parseHFMMove, &journal_entry.move);
	if (ret=hogJournalJournal(handle, &journal_entry, sizeof(journal_entry)))
		FAIL(3);

	// change header
	if (ret=hogFileModifyDoMoveInternal(handle, mod))
		FAIL(4)

	// end journal
	if (ret=hogJournalReset(handle))
		FAIL(5);

	hogUnDirtyFile(handle, mod->move.file_index);

cleanup:
	SAFE_FREE(data);
	PERFINFO_AUTO_STOP();
	return ret;
#undef FAIL
}


static int hogFileModifyDoTruncate(HogFile *handle, HogFileMod *mod)
{
	int ret;
	ENTER_CRITICAL(file_access);
	ret = fileTruncate(handle->file, mod->truncate.newsize);
	LEAVE_CRITICAL(file_access);
	return ret;
}

static int hogFileModifyDoGrow(HogFile *handle, HogFileMod *mod)
{
	U64 oldsize;
	int ret;
	ENTER_CRITICAL(file_access);
	fseek(handle->file, 0, SEEK_END);
	oldsize = ftell(handle->file);
	assert(mod->grow.newsize >= oldsize);
	if (mod->grow.newsize < oldsize)
		ret = 0; // Just ignore!
	else
		ret = fileTruncate(handle->file, mod->grow.newsize);
	LEAVE_CRITICAL(file_access);
	return ret;
}


static int hogFileModifyDoFileListResizeInternal(HogFile *handle, HogFileMod *mod)
{
	HogFileHeader *file_headers=NULL;
	int num_file_headers;
	int ret=0;
	int i;
	HogHeader header = {0};
	PERFINFO_AUTO_START("hogFileModifyDoFileListResizeInternal",1);
#define SAVE_FIELD(fieldname) if (ret=checkedWriteData(handle, &header.fieldname, sizeof(header.fieldname), offsetof(HogHeader, fieldname))) { ret = NESTED_ERROR(2, ret); goto fail; }

	// Grow FileList
	// zero new FileList data (no need to re-write existing FileList data)
	if (!(mod->filelist_resize.new_ealist_pos == mod->filelist_offset + mod->filelist_resize.new_filelist_size)) {
		PERFINFO_AUTO_STOP();
		return hogShowError(handle, 1, "Inconsistent operation data ", 0);
	}

	num_file_headers = (mod->filelist_resize.new_ealist_pos - mod->filelist_resize.old_ealist_pos) / sizeof(HogFileHeader);
	if (num_file_headers) {
		file_headers = calloc(sizeof(HogFileHeader), num_file_headers);
		for (i=0; i<num_file_headers; i++) 
			file_headers[i].size = endianSwapIfBig(U32,HOG_NO_VALUE); // Flag as not in use
		if (ret=checkedWriteData(handle, file_headers, num_file_headers*sizeof(HogFileHeader), mod->filelist_resize.old_ealist_pos)) {
			ret = NESTED_ERROR(1, ret);
			goto fail;
		}
		hogFflush(handle); // Need to make sure data is written before updating the header to point to it!
	}

	// modify header with new data (ealist_size, filelist_size)
	header.file_list_size = endianSwapIfBig(U32,mod->filelist_resize.new_filelist_size);
	header.ea_list_size = endianSwapIfBig(U32,mod->filelist_resize.new_ealist_size);
	SAVE_FIELD(file_list_size);
	SAVE_FIELD(ea_list_size);

fail:
	SAFE_FREE(file_headers);
	PERFINFO_AUTO_STOP();
	return ret;
#undef SAVE_FIELD
}

int global_hack_hogg_exit_after_filelist_resize=0; // For PigLibTest

static int hogFileModifyDoFileListResize(HogFile *handle, HogFileMod *mod)
{
	HFJFileListResize journal_entry = {0};
	int ret;
	PERFINFO_AUTO_START("hogFileModifyDoFileListResize",1);

	// write new EAList data
	if (mod->filelist_resize.old_ealist_pos != mod->filelist_resize.new_ealist_pos)
	{
		// Write it all!
		// Data endian swapped in Serialize functions
		if (ret=checkedWriteData(handle, mod->filelist_resize.new_ealist_data, mod->filelist_resize.new_ealist_size, mod->filelist_resize.new_ealist_pos))
		{
			PERFINFO_AUTO_STOP();
			return NESTED_ERROR(1, ret);
		}
		hogFflush(handle); // Need to make sure data is written before updating the header to point to it!
	} else {
		// Didn't move: Write only new data
		U32 offs = mod->filelist_resize.old_ealist_size;
		// Data endian swapped in Serialize functions
		if (ret=checkedWriteData(handle,
				((U8*)mod->filelist_resize.new_ealist_data) + offs,
				mod->filelist_resize.new_ealist_size - offs,
				mod->filelist_resize.new_ealist_pos + offs))
		{
			PERFINFO_AUTO_STOP();
			return NESTED_ERROR(1, ret);
		}
		hogFflush(handle); // Need to make sure data is written before updating the header to point to it!
	}

	journal_entry.type = endianSwapIfBig(U32,HOJ_FILELIST_RESIZE);
	journal_entry.filelist_resize = mod->filelist_resize;
	endianSwapStructIfBig(parseHFMFileListResize, &journal_entry.filelist_resize);
    if (ret=hogJournalJournal(handle, &journal_entry, sizeof(journal_entry)))
	{
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(2, ret);
	}

	if (ret=hogFileModifyDoFileListResizeInternal(handle, mod))
	{
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(3, ret);
	}

	if (ret=hogJournalReset(handle))
	{
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(4, ret);
	}

	if (global_hack_hogg_exit_after_filelist_resize) // for PigLibTest
		exit(1);

	PERFINFO_AUTO_STOP();

	return 0;
}

int hogFileModifyDoDataListFlushInternal(HogFile *handle, HogFileMod *mod)
{
	DLJournalHeader header;
	int ret;

	// Safely update the size
#define SAVE_FIELD(fieldname) if (ret=checkedWriteData(handle, &header.fieldname, sizeof(header.fieldname), mod->datalistflush.size_offset + offsetof(DLJournalHeader, fieldname))) return NESTED_ERROR(2, ret);
	header.inuse_flag = endianSwapIfBig(U32,1);
	SAVE_FIELD(inuse_flag);
	header.size = endianSwapIfBig(U32,0);
	SAVE_FIELD(size);
	header.inuse_flag = endianSwapIfBig(U32,0);
	SAVE_FIELD(inuse_flag);
	header.oldsize = endianSwapIfBig(U32,0);
	SAVE_FIELD(oldsize);
#undef SAVE_FIELD
	return 0;
}

int hogFileModifyDoDataListFlush(HogFile *handle, HogFileMod *mod)
{
	HFJDataListFlush journal_entry = {0};
	int ret;

	PERFINFO_AUTO_START("hogFileModifyDoDataListFlush",1);

	journal_entry.type = endianSwapIfBig(U32,HOJ_DATALISTFLUSH);
	journal_entry.datalistflush = mod->datalistflush;
	endianSwapStructIfBig(parseHFMDataListFlush, &journal_entry.datalistflush);
    if (ret=hogJournalJournal(handle, &journal_entry, sizeof(journal_entry)))
	{
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(2, ret);
	}

	if (ret=hogFileModifyDoDataListFlushInternal(handle, mod))
	{
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(3, ret);
	}

	if (ret=hogJournalReset(handle))
	{
		PERFINFO_AUTO_STOP();
		return NESTED_ERROR(4, ret);
	}

	return 0;
}

static bool no_assert_on_hogg_write=false; // Probably not useful, since any write errors will cause horrible things to happen
// Disables asserting on hogg write errors
AUTO_CMD_INT(no_assert_on_hogg_write, no_assert_on_hogg_write);

//volatile int startProfile;
//volatile int stopProfile;

static VOID CALLBACK hogThreadingHasWorkFunc( ULONG_PTR dwParam)
{
	static HogFileMod last_mod;
	HogFile *handle = (HogFile*)dwParam;
	HogFileMod *mod;
	int ret=0;
	static DWORD threadid=0;
	bool needsFlush = true;
	bool wasReleaseMutex = false;
	autoTimerThreadFrameBegin("hogThreadingThread");

	PERFINFO_AUTO_START_FUNC();

	if (!threadid) {
		threadid = GetCurrentThreadId();
	}
	assert(threadid == GetCurrentThreadId()); // This must be ran from only one thread!

	if (delay_hog_loading)
		Sleep(delay_hog_loading);

	mod = hogFileGetMod(handle);
	assert(mod);
	last_mod = *mod;
	switch(mod->type) {
	xcase HFM_DELETE:
		if (ret = hogFileModifyDoDelete(handle, mod))
			ret = NESTED_ERROR(1, ret);
	xcase HFM_ADD:
		if (ret = hogFileModifyDoAdd(handle, mod))
		{
			ret = NESTED_ERROR(2, ret);
		}
		if (!hog_mode_no_data)
		{
			if (mod->free_callback && mod->addOrUpdate.data)
			{
				mod->free_callback(mod->addOrUpdate.data);
				mod->addOrUpdate.data = NULL;
				mod->free_callback = NULL;
			}
			else
			{
				SAFE_FREE(mod->addOrUpdate.data);
			}
		}
		tfcRecordMemoryFreed(mod->addOrUpdate.size);
	xcase HFM_UPDATE:
		if (ret = hogFileModifyDoUpdate(handle, mod))
		{
			ret = NESTED_ERROR(3, ret);
		}
		if (mod->free_callback && mod->addOrUpdate.data)
		{
			mod->free_callback(mod->addOrUpdate.data);
			mod->addOrUpdate.data = NULL;
			mod->free_callback = NULL;
		}
		else
		{
			SAFE_FREE(mod->addOrUpdate.data);
		}
		SAFE_FREE(mod->addOrUpdate.data);
	xcase HFM_MOVE:
		if (ret = hogFileModifyDoMove(handle, mod))
			ret = NESTED_ERROR(4, ret);
	xcase HFM_DATALISTDIFF:
		if (ret = hogDLJournalSave(handle, mod))
			ret = NESTED_ERROR(5, ret);
		SAFE_FREE(mod->datalistdiff.data);
	xcase HFM_TRUNCATE:
		if (ret = hogFileModifyDoTruncate(handle, mod))
			ret = NESTED_ERROR(6, ret);
	xcase HFM_FILELIST_RESIZE:
		if (ret = hogFileModifyDoFileListResize(handle, mod))
			ret = NESTED_ERROR(7, ret);
		SAFE_FREE(mod->filelist_resize.new_ealist_data);
	xcase HFM_DATALISTFLUSH:
		if (ret = hogFileModifyDoDataListFlush(handle, mod))
			ret = NESTED_ERROR(8, ret);
	xcase HFM_RELEASE_MUTEX:
		hoggMemLog( "0x%08p: AsyncReleaseMutex start", handle);
		if (mod->release_mutex.needsFlushAndSignalAndAsyncOpDecrement) {
			InterlockedDecrement(&handle->async_operation_count);
			InterlockedDecrement(&total_async_operation_count);
		}
		needsFlush = mod->release_mutex.needsFlushAndSignalAndAsyncOpDecrement;
		wasReleaseMutex = true;
	xcase HFM_GROW:
		if (ret = hogFileModifyDoGrow(handle, mod))
			ret = NESTED_ERROR(9, ret);
	}
	if (ret) {
		if (no_assert_on_hogg_write)
			hogShowErrorEx(PIGERR_PRINTF, handle->filename, NULL, ret, "Error modifying .hogg file Err#", 0);
		else {
			printf("An error occurred while attempting to modify a hogg file (Err# 0x%x).\n", ret);
			printf("Any operations queued up to be written after this point are probably lost,\n"
				   "but the file should be in a recoverable state.  If no programmer is around\n"
				   "to debug the problem it should be find to restart the application.\n");
			hogShowErrorEx(PIGERR_ASSERT, handle->filename, NULL, ret, "Error modifying .hogg file Err#", 0);
		}
	}
	safefreeHogFileMod(&mod);
	if (ret) {
		ENTER_CRITICAL(data_access);
		handle->last_threaded_error = ret;
		LEAVE_CRITICAL(data_access);
	}

	if (!handle->single_app_mode) // (handle->version.soloValue != handle->version.value)
	{
		// Sleep for a bit of time if this was the first
		//  item in the queue, and nothing else has been queued up,
		//  and another process has shown evidence of accessing our
		//  hogg file, otherwise the hog file ownership will bounce
		//  back and forth causing lots of loads/reloads.
		// Do this even if we're the only process, in case small
		//  operations are finishing quicker than they can be queued up
		// Break out of this if it appears we're doing a flush or have more work.
		// But don't worry if we're in single app mode, we don't release the mutex anyway
		int left = 10;
		while (left && !handle->mod_ops_head && !handle->debug_in_doing_flush) {
			// Wait on this instead of sleeping so we can be notified sooner
            WaitForEvent(handle->starting_flush_event, 1);
			left--;
		}
	}

	hogReleaseMutex(handle, needsFlush, true); // Must be the final action
	if (wasReleaseMutex)
		InterlockedDecrement(&total_async_mutex_release_count);
	autoTimerThreadFrameEnd();

	PERFINFO_AUTO_STOP();

	return;
}

U32 GetDatatlistJournalSize(HogFile *hog_file)
{
	return hog_file->header.dl_journal_size;
}

//////////////////////////////////////////////////////////////////////////
// End in-thread: Functions to actually apply the modifications queued up
//////////////////////////////////////////////////////////////////////////

