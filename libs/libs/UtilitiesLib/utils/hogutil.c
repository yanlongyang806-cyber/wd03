#include "hogutil.h"
#include "ThreadSafeMemoryPool.h"
#include "RedBlackTree.h"
#include "piglib.h"
#include "hoglib.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "rand.h"
#include "sysutil.h"
#include "MemAlloc.h"
#include "crypt.h"
#include "earray.h"
#include "StashTable.h"
#include "ThreadManager.h"
#include "UnitSpec.h"
#include "fileutil.h"
#include "ScratchStack.h"
#include "zutils.h"
#include "cpu_count.h"
#include "utils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:syncHogWorker", BUDGET_FileSystem););

struct HogFreeSpaceTracker2 {
	U64 total_size;
	U64 start_location;
	RedBlackTree *rbt_spatial; // Tree by spatial location, for managing/compacting the freelist
	RedBlackTree *rbt_size; // Tree by size, for finding empty spots to allocate from
	int allocingSpace; // 1 at startup, 0 after calling DoneAllocingSpace
	bool append_only;
};

// Less than max U64 so we can do math with it, actual value stored will be -size of hog file
#define SIZE_INFINITE ((U64)0x0FffFFffFFffFFffLL)
#define OFFS_SEARCH ((U64)0xFFffFFffFFffFFffLL)

typedef struct HFST2Element {
	U64 offs;
	U64 size;
	// min and max?
} HFST2Element;

TSMP_DEFINE(HFST2Element);

AUTO_RUN;
void HFST2ElementMPInit(void)
{
	TSMP_CREATE(HFST2Element, 1024);
}

static int cmpHFST2ElementSpatial(HFST2Element *a /* value */, HFST2Element *b /* range */)
{
	assert(b->size);
	if (b->offs <= a->offs) {
		if (a->offs < b->offs + b->size) {
			// a starts within b, we found a match
			return 0;
		} else {
			// a starts after b ends
			return 1;
		}
	} else {
		// a starts before b begins
		return -1;
	}
}

static int cmpHFST2ElementSize(HFST2Element *a /* value */, HFST2Element *b /* range */)
{
	assert(a->size && b->size);
	if (a->size == b->size) {
		if (a->offs == OFFS_SEARCH)
			return 0;
		if (a->offs == b->offs)
			return 0;
		if (a->offs > b->offs)
			return 1;
		return -1;
	} else {
		if (a->size < b->size)
			return -1;
		return 1;
	}
}

static void destroyHFST2element(HFST2Element *elem)
{
	TSMP_FREE(HFST2Element, elem);
}

static HFST2Element *createHFST2Element(void)
{
	HFST2Element *ret;
	ret = TSMP_ALLOC(HFST2Element);
	return ret;
}

static void hfst2GrowElem(HogFreeSpaceTracker2 *hfst, HFST2Element *elem, U64 newoffs, U64 newsize, RbtIterator iter_size)
{
	RbtStatus status;
	int needResize = !hfst->allocingSpace && (newsize != elem->size);

	assert(newsize);
	assert(elem->size);
	if (needResize) {
		if (!iter_size)
			iter_size = rbtFind(hfst->rbt_size, elem);
		assert(iter_size);
		status = rbtErase(hfst->rbt_size, iter_size);
		assert(status == RBT_STATUS_OK);
	}
	if (elem->offs == hfst->total_size)
		hfst->total_size = newoffs;
	elem->offs = newoffs;
	elem->size = newsize;
	if (needResize) {
		status = rbtInsert(hfst->rbt_size, elem, NULL);
		assert(status == RBT_STATUS_OK);
	}
}

static void hfst2RemoveElem(HogFreeSpaceTracker2 *hfst, HFST2Element *elem, RbtIterator iter_spatial, RbtIterator iter_size)
{
	RbtStatus status;
	assert(elem->offs != hfst->total_size); // Can't delete this one!  It has a size ~= SIZE_INFINITY as well.
	if (!iter_spatial)
		iter_spatial = rbtFind(hfst->rbt_spatial, elem);
	status = rbtErase(hfst->rbt_spatial, iter_spatial);
	assert(status == RBT_STATUS_OK);
	if (!hfst->allocingSpace) {
		if (!iter_size)
			iter_size = rbtFind(hfst->rbt_size, elem);
		status = rbtErase(hfst->rbt_size, iter_size);
		assert(status == RBT_STATUS_OK);
	}
	destroyHFST2element(elem);
}

static void hfst2AddElem(HogFreeSpaceTracker2 *hfst, HFST2Element *elem)
{
	RbtStatus status;
	assert(elem->size);
	status = rbtInsert(hfst->rbt_spatial, elem, NULL);
	assert(status == RBT_STATUS_OK);
	if (!hfst->allocingSpace) {
		status = rbtInsert(hfst->rbt_size, elem, NULL);
		assert(status == RBT_STATUS_OK);
	}
}


void hfst2SetAppendOnly(HogFreeSpaceTracker2 *hfst, bool bAppendOnly)
{
	hfst->append_only = bAppendOnly;
}

HogFreeSpaceTracker2 *hfst2Create()
{
	HFST2Element *elem;
	HogFreeSpaceTracker2 *ret = calloc(sizeof(*ret),1);
	ret->append_only = false;
	ret->rbt_spatial = rbtNew(cmpHFST2ElementSpatial);
	ret->rbt_size = rbtNew(cmpHFST2ElementSize);
	ret->allocingSpace = 1;
	elem = createHFST2Element();
	ret->total_size = 0;
	elem->offs = 0;
	elem->size = SIZE_INFINITE;
	hfst2AddElem(ret, elem);
	return ret;
}

// Set automatically
// void hfst2SetTotalSize(HogFreeSpaceTracker2 *hfst, U64 total_size)
// {
// 	hfst->total_size = total_size;
// }

U64 hfst2GetTotalSize(HogFreeSpaceTracker2 *hfst)
{
	return MAX(hfst->total_size, hfst->start_location);
}

void hfst2SetStartLocation(HogFreeSpaceTracker2 *hfst, U64 start_location)
{
	hfst->start_location = start_location;
	do {
		RbtIterator elem = rbtBegin(hfst->rbt_spatial);
		HFST2Element *key;
		if (elem) {
			rbtKeyValue(hfst->rbt_spatial, elem, &key, NULL);
			if (key->offs < start_location) {
				if (key->offs + key->size <= start_location) {
					// Starts and ends before the new start location
					hfst2RemoveElem(hfst, key, elem, NULL);
					continue; // Continue checking
				} else {
					hfst2GrowElem(hfst, key, start_location, key->offs + key->size - start_location, NULL);
				}
			}
		}
		break;
	} while (true);
}

void hfst2Destroy(HogFreeSpaceTracker2 *hfst)
{
	if (!hfst)
		return;
	rbtDelete(hfst->rbt_size, NULL, NULL);
	rbtDelete(hfst->rbt_spatial, destroyHFST2element, NULL);
}

// Called only at startup
// Returns false if an error occurred
bool hfst2AllocSpace(HogFreeSpaceTracker2 *hfst, U64 offs, U64 size)
{
	RbtIterator iter;
	HFST2Element elem;
	HFST2Element *key=NULL;

	assert(hfst->allocingSpace);

	// Finds the entry which contains this block
	elem.offs = offs;
	elem.size = 0; // Not used
	iter = rbtFind(hfst->rbt_spatial, &elem);
	if (!iter) {
		// This file overlaps with another file!  Kill them all!
		return false;
	}
	rbtKeyValue(hfst->rbt_spatial, iter, &key, NULL);

	// If the block is off an end, modifies the existing entry with new values
	// Otherwise: Modifies the entry to be the low half, adds a new entry which is the high half
	if (key->offs == offs) {
		if (size == key->size) {
			// Remove it!
			hfst2RemoveElem(hfst, key, iter, NULL);
		} else {
			// Shrink it
			if (size > key->size) {
				// This file overlaps with another file!  Kill them all!
				return false;
			}
			hfst2GrowElem(hfst, key, key->offs + size, key->size - size, NULL);
		}
	} else if (key->offs + key->size == offs+size) {
		assert(size < key->size);
		hfst2GrowElem(hfst, key, key->offs, key->size - size, NULL);
	} else if (key->offs + key->size < offs+size) {
		// This file overlaps with another file!  Kill them all!
		return false;
	} else {
		HFST2Element *newelem;
		// Make new low half (before modifying high half)
		newelem = createHFST2Element();
		newelem->offs = key->offs;
		newelem->size = offs - key->offs;
		// Modify high half
		assert(offs > key->offs);
		hfst2GrowElem(hfst, key, offs + size, key->offs + key->size - (offs + size), NULL);
		// Insert new low half (after modifying high half)
		hfst2AddElem(hfst, newelem);
	}
	return true;
}

void hfst2DoneAllocingSpace(HogFreeSpaceTracker2 *hfst)
{
	RbtIterator iter;
	assert(hfst->allocingSpace);
	hfst->allocingSpace = 0;
	// Build rbt_size
	iter = rbtBegin(hfst->rbt_spatial);
	while (iter) {
		HFST2Element *key;
		rbtKeyValue(hfst->rbt_spatial, iter, &key, NULL);
		rbtInsert(hfst->rbt_size, key, NULL);
		iter = rbtNext(hfst->rbt_spatial, iter);
	}
}


void hfst2FreeSpace(HogFreeSpaceTracker2 *hfst, U64 offs, U64 size)
{
	RbtIterator left_iter;
	RbtIterator right_iter;
	HFST2Element elem;
	HFST2Element *left=NULL;
	HFST2Element *right=NULL;

	assert(!hfst->allocingSpace);

	// Find any node adjacent to this one on the left
	// If found, grow it
	// Find any node adjacent on the right
	// If found, if left found, delete it, else grow it
	// If none found, insert it

	assert(offs>0);
	// Look for neighbor on left
	elem.offs = offs-1;
	elem.size = 0;
	left_iter = rbtFind(hfst->rbt_spatial, &elem);
	if (left_iter) {
		rbtKeyValue(hfst->rbt_spatial, left_iter, &left, NULL);
		assert(left->offs + left->size == offs); // Otherwise there's some overlap!
		hfst2GrowElem(hfst, left, left->offs, left->size + size, NULL);
	}
	// Check right
	elem.offs = offs + size;
	right_iter = rbtFind(hfst->rbt_spatial, &elem);
	if (right_iter) {
		rbtKeyValue(hfst->rbt_spatial, right_iter, &right, NULL);
		assert(right->offs == offs + size); // Otherwise there's some overlap!
		if (left) {
			U64 newoffs = left->offs;
			U64 newsize = left->size + right->size;
			hfst2RemoveElem(hfst, left, left_iter, NULL);
			hfst2GrowElem(hfst, right, newoffs, newsize, NULL);
		} else {
			// Grow it
			hfst2GrowElem(hfst, right, offs, size + right->size, NULL);
		}
	}
	if (!left && !right) {
		// Insert new
		HFST2Element *newelem;
		newelem = createHFST2Element();
		newelem->offs = offs;;
		newelem->size = size;
		hfst2AddElem(hfst, newelem);
	}
}

U64 hfst2GetSpace(HogFreeSpaceTracker2 *hfst, U64 minsize, U64 desiredsize)
{
	HFST2Element search;
	RbtIterator iter;
	HFST2Element *key;
	U64 ret;

	assert(!hfst->allocingSpace);
	assert(minsize);

	search.offs = OFFS_SEARCH;
	if (hfst->append_only)
		search.size = 10000000000000LL;
	else
		search.size = minsize;
	iter = rbtFindGTE(hfst->rbt_size, &search);
	assert(iter); // Should always be a node with "infinite" space GTE the query
	rbtKeyValue(hfst->rbt_size, iter, &key, NULL);
	assert(key->offs >= hfst->start_location);
	assert(key->size >= minsize);
	// Should we walk a couple nodes and see if there's one much closer to the beginning of the file here?

	ret = key->offs;
	if (key->size == minsize) {
		hfst2RemoveElem(hfst, key, NULL, iter);
	} else {
		hfst2GrowElem(hfst, key, key->offs + minsize, key->size - minsize, iter);
	}

	return ret;
}

U64 hfst2GetLargestFreeSpace(HogFreeSpaceTracker2 *hfst)
{
	RbtIterator iter;
	HFST2Element *key;

	assert(!hfst->allocingSpace);
	iter = rbtEnd(hfst->rbt_size);
	assert(!iter);
	iter = rbtPrev(hfst->rbt_size, iter);
	assert(iter); // Should always be a node with "infinite" space
	rbtKeyValue(hfst->rbt_size, iter, &key, NULL);
	assert(key->size > SIZE_INFINITE/2); // Should be the infinite node
	iter = rbtPrev(hfst->rbt_size, iter);
	if (!iter)
		return 0; // No free space
	rbtKeyValue(hfst->rbt_size, iter, &key, NULL);
	return key->size;
}


volatile U64 data_size;
#if PLATFORM_CONSOLE
static int num_reader_threads=30; // To hide ridiculous filesystem latency in our fake network filesystem
#else
static int num_reader_threads=0; // Defaults to number of CPUs
#endif
static unsigned long memory_cap=1024*1024*1024;
static volatile U32 oustanding_mem_needed;
static volatile U32 did_something;
static bool sync_inited;

void hogSyncSetNumReaderThreads(int num_threads)
{
	assert(!sync_inited);
	num_reader_threads = num_threads;
}

void hogSyncSetMemoryCap(unsigned long max)
{
	memory_cap = max;
}


static void syncHogDoRead(NewPigEntry *entry, HogFileIndex file_index, HogFile *src_hog, bool dont_pack)
{
	bool checksum_valid;
	U32 byte_count;
	U32 buf_size;
	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_START("get stats", 1);
	assert(stricmp(entry->fname, hogFileGetFileName(src_hog, file_index))==0);
	entry->timestamp = hogFileGetFileTimestamp(src_hog, file_index);
	entry->header_data = hogFileGetHeaderData(src_hog, file_index, &entry->header_data_size);
	entry->checksum[0] = hogFileGetFileChecksum(src_hog, file_index);
	hogFileGetSizes(src_hog, file_index, &entry->size, &entry->pack_size);
	buf_size = entry->pack_size?entry->pack_size:entry->size;
	// Keep packed state
	if (dont_pack)
		entry->dont_pack = 1;
	else if (entry->pack_size)
		entry->must_pack = 1;
	else
		entry->dont_pack = 1;
	InterlockedIncrement(&did_something);
	PERFINFO_AUTO_STOP_START("memory test", 1);
	{
		// Test for enough free memory, wait for up to 5 minutes after nothing has happened
		U32 timeout = 60000*5;
		U32 last_did_something = did_something;
		U32 wait_time=0;
		do
		{
			U32 max, avail;
			bool bWait=false;
			timeout -= wait_time;
			wait_time = randInt(1000);

			getPhysicalMemory(&max, &avail);
			avail = MIN(avail, memory_cap);

			// If anything else is happening, reset timer
			if (did_something != last_did_something)
			{
				timeout = 60000*5;
				last_did_something = did_something;
			}

			if (timeout > wait_time && num_reader_threads > 1)
			{
				// If we're not about to time out
				if (avail < buf_size + 8*1024*1024 + oustanding_mem_needed)
				{
					// Not enough memory for the back containing this to get read
					bWait = true;
				}
			}

			if (!bWait)
			{
				entry->data = calloc_canfail(buf_size+1, 1);
				if (entry->data)
				{
					break;
				}
			}

			if (num_reader_threads > 1)
			{
				Sleep(wait_time);
			} else {
				break;
			}
		} while (timeout > wait_time);
		if (!entry->data)
		{
			assertmsg(entry->data, "Out of memory, did not recover in 60 seconds");
		}
	}
	PERFINFO_AUTO_STOP_START("interlocked", 1);
	InterlockedIncrement(&did_something);
	InterlockedExchangeAdd(&oustanding_mem_needed, buf_size); // We don't actually end up allocating that much, just a few MB for a packet buffer
	PERFINFO_AUTO_STOP_START("extract", 1);
	if (entry->pack_size)
	{
		byte_count = hogFileExtractRawBytes(src_hog, file_index, entry->data, 0, entry->pack_size, false, 0);
		assert(byte_count == entry->pack_size);
		data_size += byte_count; // Not threadsafe, but only for debugging/output
		// Unzip and check checksum
		{
			U32 unpack_size = entry->size;
			char *unzip_buf;
			int ret;
			if (dont_pack)
				unzip_buf = malloc(unpack_size);
			else
				unzip_buf = ScratchAlloc(unpack_size);
			g_file_stats.pig_unzips++;
			ret = unzipDataEx(unzip_buf,&unpack_size,entry->data,entry->pack_size,true);
			if (ret < 0)
			{
				assertmsg(0, "Data corruption in source hogg file, cannot unzip data.");
			} else {
				U32 expected_checksum = hogFileGetFileChecksum(src_hog, file_index);
				cryptMD5Update(unzip_buf,entry->size);
				cryptMD5Final(entry->checksum);
				checksum_valid = (expected_checksum == entry->checksum[0]);
				if (!checksum_valid)
				{
					if (expected_checksum != 0) // ObjectDB known to write 0 CRCs
						printf("Checksum failure on %s  (header:%08x data:%08x)               \n", entry->fname,
							expected_checksum, entry->checksum[0]);
					// Get new checksum and headerdata
					entry->header_data = NULL;
					entry->header_data_size = 0;
					pigChecksumAndPackEntry(entry);
				}
				// assert(checksum_valid); - repairing instead.
			}
			if (dont_pack)
			{
				if (entry->free_callback)
				{
					entry->free_callback(entry->data);
					entry->free_callback = NULL;
				}
				else
					free(entry->data);
				entry->data = unzip_buf;
				entry->pack_size = 0;
			}
			else
				ScratchFree(unzip_buf);
		}

	} else {
		entry->dont_pack = 1;
		byte_count = hogFileExtractBytes(src_hog, file_index, entry->data, 0, entry->size);
		assert((int)byte_count == entry->size);
		if (entry->size) {
			U32 expected_checksum = hogFileGetFileChecksum(src_hog, file_index);
			cryptMD5Update(entry->data,entry->size);
			cryptMD5Final(entry->checksum);
			checksum_valid = (expected_checksum == entry->checksum[0]);
			if (!checksum_valid)
			{
				printf("Checksum failure on %s  (header:%08x data:%08x)               \n", entry->fname,
					expected_checksum, entry->checksum[0]);
				// Get new checksum and headerdata
				entry->header_data = NULL;
				entry->header_data_size = 0;
				pigChecksumAndPackEntry(entry); // re-get header data, will use checksum calc'd above
			}
			// assert(checksum_valid); - repairing instead.
		}
		data_size += byte_count; // Not threadsafe, but only for debugging/output
	}
	PERFINFO_AUTO_STOP_START("finish", 1);
	InterlockedExchangeAdd(&oustanding_mem_needed, -(int)buf_size);
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

typedef struct SyncHogAction
{
	NewPigEntry entry;
	HogFile *src_hog;
	HogFileIndex file_index;
	U64 file_offset;
} SyncHogAction;

static SyncHogAction **sync_actions_todo;
static SyncHogAction **sync_actions_done;
static bool sync_dont_pack;
static CRITICAL_SECTION sync_cs;

static DWORD WINAPI syncHogWorker(LPVOID lpParam)
{
	EXCEPTION_HANDLER_BEGIN;
	do
	{
		int num_actions_done;
		SyncHogAction *action = NULL;
		autoTimerThreadFrameBegin("syncHogWorker");
		PERFINFO_AUTO_START("top", 1);
		EnterCriticalSection(&sync_cs);
		num_actions_done = eaSize(&sync_actions_done);
#if PLATFORM_CONSOLE
		if (num_actions_done + (int)hogTotalPendingOperationCount() < num_reader_threads * 2) // Don't start another read, too many writes pending
#else
		// Not limiting based on hog operations pending, main thread will stall trying to queue if internal
		//  (memory-size-dependent) queue is hit
		if (num_actions_done < num_reader_threads * 2 + 100) // Don't start another read, too many writes pending
#endif
		{
			action = eaPop(&sync_actions_todo);
		}
		LeaveCriticalSection(&sync_cs);
		PERFINFO_AUTO_STOP();
		if (!action) {
			Sleep(1);
		} else {
			syncHogDoRead(&action->entry, action->file_index, action->src_hog, sync_dont_pack);
			PERFINFO_AUTO_START("queue", 1);
			EnterCriticalSection(&sync_cs);
			eaPush(&sync_actions_done, action);
			LeaveCriticalSection(&sync_cs);
			PERFINFO_AUTO_STOP();
		}
		autoTimerThreadFrameEnd();
	} while (true);
	EXCEPTION_HANDLER_END;
}

ManagedThread **sync_threads;

static void syncHogInit(void)
{
	if (!sync_inited)
	{
		int i;
		InitializeCriticalSection(&sync_cs);

		if (!num_reader_threads)
			num_reader_threads = getNumRealCpus();

		// Create worker threads
		for (i=0; i<num_reader_threads; i++)
		{
			ManagedThread *thread = tmCreateThreadEx(syncHogWorker, NULL, 0, i % 6);
			eaPush(&sync_threads, thread);
		}

		sync_inited = true;
	}
}

static void syncFinalizeAction(SyncHogAction *action, HogFile ***dest_hogs, const char *outprefix, U64 roughMaxSize, HogFileCreateFlags open_flags, U32 datalist_journal_size, HogDefragFlags defrag_flags)
{
	int i;
	HogFile *hog_file=NULL;
	PERFINFO_AUTO_START_FUNC();
	InterlockedIncrement(&did_something);
	// Determine the best hogg for this file
	for (i=0; i<eaSize(dest_hogs); i++)
	{
		U32 byte_count = action->entry.pack_size?action->entry.pack_size:action->entry.size;
		hog_file = (*dest_hogs)[i];
		if (!roughMaxSize || hogFileGetArchiveSize(hog_file) + byte_count < roughMaxSize)
			break; // Enough space for sure
		if (byte_count < hogFileGetLargestFreeSpace(hog_file))
			break; // Fits in a free chunk
		hog_file = NULL;
	}
	if (!hog_file)
	{
		char newhogfilename[MAX_PATH];
		bool bCreated=false;;
		// Need a new one!
		sprintf(newhogfilename, "%s%d.hogg", outprefix, i);
		hog_file = hogFileReadEx(newhogfilename, &bCreated, PIGERR_ASSERT, NULL, HOG_MUST_BE_WRITABLE|open_flags, datalist_journal_size);
		assert(bCreated);
		eaPush(dest_hogs, hog_file);
	}

	// Make sure not to pack if this is what was requested.
	if (defrag_flags & HogDefrag_DontPack)
	{
		action->entry.must_pack = false;
		action->entry.dont_pack = true;
	}

	verify(0==hogFileModifyUpdateNamedSync2(hog_file, &action->entry));
	InterlockedIncrement(&did_something);
	PERFINFO_AUTO_STOP();
}

int cmpSyncAction(const SyncHogAction **a1, const SyncHogAction **a2)
{
	S64 diff;
	if ((*a1)->src_hog != (*a2)->src_hog)
		return ((*a1)->src_hog > (*a2)->src_hog) ? 1 : -1;
	diff = (*a2)->file_offset - (*a1)->file_offset;
	return (diff>0)?1:(diff<0)?-1:0;
}

int hogSync(const char *outname, const char *srcname, U64 roughMaxSize, HogFileCreateFlags open_flags, U32 datalist_journal_size, HogDefragFlags defrag_flags)
{
	char outprefix[MAX_PATH];
	char *s;
	int i, j;
	HogFile *src_hog;
	HogFile **dest_hogs=NULL;
	int num_files;
	int count;
	int num_hogs;
	bool bDidCR;
	int lastval;
	int timer = timerAlloc();
	int timerShort = timerAlloc();
	int timerPrintf;
	F32 speedShort=0;
	U64 data_size_last_short=0;
	StashTable prune_table=0;

	SyncHogAction **temp_actions_list=NULL;

	strcpy(outprefix, outname);
	s = strrchr(outprefix, '.');
	assert(s && stricmp(s, ".hogg")==0);
	*s = '\0';

	syncHogInit();

	loadstart_printf("Opening source (%s)...", srcname);
	src_hog = hogFileRead(srcname, NULL, PIGERR_ASSERT, NULL, HOG_READONLY|HOG_NOCREATE|HOG_MULTIPLE_READS);
	if (!src_hog)
	{
		loadend_printf(" failed.");
		assertmsgf(src_hog, "Error opening source file for syncing : \"%s\"\n", srcname);
		timerFree(timer);
		timerFree(timerShort);
		return -1;
	}
	loadend_printf(" done.");

	loadstart_printf("Opening output...");
	// Open output hoggs
	for (i=0; ; i++)
	{
		char filename[MAX_PATH];
		HogFile *hog_file;
		if (i==0)
			sprintf(filename, "%s.hogg", outprefix);
		else
			sprintf(filename, "%s%d.hogg", outprefix, i);
		hog_file = hogFileReadEx(filename, NULL, PIGERR_ASSERT, NULL, open_flags|((i==0)?HOG_MUST_BE_WRITABLE:(HOG_MUST_BE_WRITABLE|HOG_NOCREATE)), datalist_journal_size ? datalist_journal_size : GetDatatlistJournalSize(src_hog));
		if (hog_file)
		{
			hogFileSetSingleAppMode(hog_file, true); // Lock it
			if (defrag_flags & HogDefrag_Tight) // Use exact size
				hogFileReserveFiles(hog_file, hogFileGetNumUsedFiles(src_hog));
			else // allow to grow by 15% without resizing, and do no resize during syncing
				hogFileReserveFiles(hog_file, (int)(hogFileGetNumUsedFiles(src_hog) * 1.15f));
			eaPush(&dest_hogs, hog_file);
		}
		else
			break;
	}
	loadend_printf(" done.");
	assert(eaSize(&dest_hogs)>=1);

	if(defrag_flags & HogDefrag_SkipMutex)
	{
		hogFileSetSkipMutex(src_hog, true);
	}
	else
	{
		hogFileSetSingleAppMode(src_hog, true); // Lock it
	}

	// Prune all output hoggs
	loadstart_printf("Pruning orphaned and outdated files...");
	count = 0;
	num_files = hogFileGetNumFiles(src_hog);
	prune_table = stashTableCreateWithStringKeys(num_files*2, StashDefault);
	for (i=0; i<num_files; i++) {
		const char *filename;
		filename = hogFileGetFileName(src_hog, i);
		if (!filename || hogFileIsSpecialFile(src_hog, i))
			continue;

		verify(stashAddInt(prune_table, filename, i+1, false));
	}
	num_hogs = eaSize(&dest_hogs);
	lastval=0;
	for (j=0; j<num_hogs; j++)
	{
		HogFile *hog_file = dest_hogs[j];
		num_files = hogFileGetNumFiles(hog_file);
		for (i=0; i<num_files; i++)
		{
			const char *filename;
			HogFileIndex file_index;
			filename = hogFileGetFileName(hog_file, i);
			if (!filename || hogFileIsSpecialFile(hog_file, i))
				continue;

			if (!stashFindInt(prune_table, filename, &file_index))
			{
				count++;
				hogFileModifyDelete(hog_file, i);
			} else {
				// Exists in one output file, 
				// If it's up to date, remove it from the list
				// Otherwise, remove it from output file and let it be copied anew
				if (hogFileGetFileTimestamp(src_hog, file_index-1) != hogFileGetFileTimestamp(hog_file, i) ||
					hogFileGetFileSize(src_hog, file_index-1) != hogFileGetFileSize(hog_file, i) ||
					hogFileGetFileChecksum(src_hog, file_index-1) != hogFileGetFileChecksum(hog_file, i))
				{
					// Different!
					count++;
					hogFileModifyDelete(hog_file, i);
				} else {
					// The same, forget about it
					stashRemoveInt(prune_table, filename, NULL);
				}
			}

			if (((i % 500)==499 && lastval != count) || (lastval!=0 && i+1 == num_files))
			{
				if (lastval==0)
					printf("\n");
				lastval = count;
				printf("%d/%d (%d of %d hoggs, %d pruned)", i+1, num_files, j+1, num_hogs, count);
#ifdef _XBOX
				printf("\n");
#else
				printf("\r");
#endif
			}
		}
	}
	for (j=0; j<eaSize(&dest_hogs); j++)
		hogFileModifyFlush(dest_hogs[j]);
	loadend_printf("done (%d pruned).", count);

	// TODO: Prune each down to requisite size?

	loadstart_printf("Determining diff...");
	count=0;
	num_files = stashGetCount(prune_table);
	bDidCR = false;
	data_size=0;

	// Sync all source files
	FOR_EACH_IN_STASHTABLE2(prune_table, elem)
	{
		autoTimerThreadFrameBegin("pig");
		{
			const char *filename = stashElementGetKey(elem);
			HogFileIndex file_index = stashElementGetInt(elem)-1;

			// Merge from one hogg into another
			HogFile *hog_file=NULL;
			SyncHogAction *action = calloc(sizeof(*action), 1);
			action->file_index = file_index;
			action->src_hog = src_hog;
			action->entry.fname = filename;
			action->file_offset = hogFileGetOffset(src_hog, file_index);
			eaPush(&temp_actions_list, action);
		}
		autoTimerThreadFrameEnd();
	}
	FOR_EACH_END;

	loadend_printf(" done (%d to sync).", eaSize(&temp_actions_list));
	loadstart_printf("Sorting...");

	eaQSort(temp_actions_list, cmpSyncAction);
	// Want them sorted backwards here since we pop from the list
	EnterCriticalSection(&sync_cs);
	assert(eaSize(&sync_actions_todo)==0);
	eaDestroy(&sync_actions_todo);
	sync_actions_todo = temp_actions_list;
	temp_actions_list = NULL;
	sync_dont_pack = defrag_flags & HogDefrag_DontPack;
	LeaveCriticalSection(&sync_cs);
	loadend_printf(" done.");
	loadstart_printf("Copying files...");
	timerStart(timer);
	timerStart(timerShort);
	timerPrintf = timerAlloc();

	while (true)
	{
		SyncHogAction *action;
		int num_actions_done;
		autoTimerThreadFrameBegin("pig");
		EnterCriticalSection(&sync_cs);
		num_actions_done = eaSize(&sync_actions_done);
		action = eaRemove(&sync_actions_done, 0);
		LeaveCriticalSection(&sync_cs);
		if (!action)
		{
			if (count == num_files)
				break;
			Sleep(1);
		}
		else
		{
			syncFinalizeAction(action, &dest_hogs, outprefix, roughMaxSize, open_flags, datalist_journal_size, defrag_flags);
			count++;
			if (((count % 100)==0 && timerElapsed(timerPrintf)>0.25) || count == num_files)
			{
				F32 elapsed = timerElapsed(timer);
				F32 elapsedShort = timerElapsed(timerShort);
				char buf[64];
				if (!bDidCR) {
					printf("\n");
					bDidCR = true;
				}
				timerStart(timerPrintf);

				if (elapsedShort > 1.0)
				{
					U64 ds = data_size;
					speedShort = (ds - data_size_last_short) / elapsedShort;
					data_size_last_short = ds;
					timerStart(timerShort);
				}

				if (!elapsed)
					elapsed = 0.0001;

#ifdef _XBOX
				printf("%d/%d  (%1.2fs total, %s/s, %s/s average)  \n", count, num_files, elapsed, friendlyBytesBuf(speedShort, buf), friendlyBytes((U64)(data_size / elapsed)));
#else
				printf("%d/%d  (%1.2fs total, %s/s, %s/s average)  \r", count, num_files, elapsed, friendlyBytesBuf(speedShort, buf), friendlyBytes((U64)(data_size / elapsed)));
#endif
			}
		}
		autoTimerThreadFrameEnd();
	}
	printf("\n");
	loadend_printf("done (%d copied, %s).", count, friendlyBytes(data_size));

	loadstart_printf("Closing...");
	if(defrag_flags & HogDefrag_SkipMutex)
	{
		hogFileSetSkipMutex(src_hog, false);
	}
	hogFileDestroy(src_hog, true);
	for (i=0; i<eaSize(&dest_hogs); i++)
	{
		hogFileModifyTruncate(dest_hogs[i]);
		hogFileDestroy(dest_hogs[i], true); // May grow the file and then truncate again if running with HOG_NO_JOURNAL mode
	}
	eaDestroy(&dest_hogs);
	stashTableDestroy(prune_table);
	prune_table = NULL;
	{
		F32 elapsed = timerElapsed(timer);
		if (!elapsed)
			elapsed = 0.0001;
		loadend_printf("done (%1.2fs total, %s/s).", elapsed, friendlyBytes((U64)(data_size / elapsed)));
	}
	timerFree(timerShort);
	timerFree(timer);
	timerFree(timerPrintf);
	return 0;
}


int hogDefragEx(const char *filename, U32 datalist_journal_size, HogDefragFlags defrag_flags, char *tempnameout, size_t tempnameout_size)
{
	char tempname[MAX_PATH];
	int ret;
	changeFileExt(filename, ".temp.hogg", tempname);
	if (fileExists(tempname))
	{
		if (fileForceRemove(tempname))
		{
			assertmsg(0, "hogDefrag - failed to remove existing temporary file");
			return -1;
		}
	}
	assert(!hogFileIsOpenInMyProcess(filename));
	assert(!hogFileIsOpenInMyProcess(tempname));
	ret = hogSync(tempname, filename, 0, HOG_APPEND_ONLY|HOG_NO_JOURNAL, datalist_journal_size, defrag_flags);
	assert(!hogFileIsOpenInMyProcess(filename));
	assert(!hogFileIsOpenInMyProcess(tempname));
	if (ret)
	{
		fileForceRemove(tempname);
		return ret;
	}
	assert(fileExists(tempname));

	if (defrag_flags & HogDefrag_RunDiff)
	{
		HogDiffFlags diffFlags = defrag_flags & HogDefrag_SkipMutex ? HogDiff_SkipMutex : HogDiff_Default;
		int diff = hogDiff(filename, tempname, 1, diffFlags);
		assert(diff==0);
	}

	if(tempnameout)
	{
		strcpy_s(SAFESTR2(tempnameout), tempname);
	}

	if(!(defrag_flags & HogDefrag_NoRename))
	{
		if (fileForceRemove(filename))
		{
			assertmsg(0, "hogDefrag - failed to remove file being defragmented");
			return -1;
		}
		if (fileMove(tempname, filename))
		{
			assertmsg(0, "hogDefrag - failed to rename temporary file to original name");
			return -1;
		}
	}
	return 0;
}

typedef struct DiffAction
{
	const char *name;
	HogFileIndex hog1_index;
	HogFileIndex hog2_index;
	U64 hog1_offset;
	U64 hog2_offset;
} DiffAction;

int cmpDiffAction(const void *a, const void *b)
{
	const DiffAction *d1 = (const DiffAction *)a;
	const DiffAction *d2 = (const DiffAction *)b;
	U64 m1 = MIN(d1->hog1_offset, d1->hog2_offset);
	U64 m2 = MIN(d2->hog1_offset, d2->hog2_offset);
	if (m1 < m2)
		return -1;
	if (m2 < m1)
		return 1;
	return 0;
}

int hogDiff(const char *filename1, const char *filename2, int verbose, HogDiffFlags flags)
{
	HogFile *hog1;
	HogFile *hog2;
	int ret=-1;
	DiffAction *actions=NULL;
	U32 num_actions=0;
	U32 num_actions_allocated;
	HogFileIndex i;
	int timer=timerAlloc();

	if (verbose)
		loadstart_printf("Loading hogs to diff...");
	hog1 = hogFileRead(filename1, NULL, PIGERR_PRINTF, NULL, HOG_READONLY|HOG_NOCREATE);
	hog2 = hogFileRead(filename2, NULL, PIGERR_PRINTF, NULL, HOG_READONLY|HOG_NOCREATE);
	if (verbose)
		loadend_printf(" done.");
	if (!hog1)
	{
		printf("Failed to open %s\n", filename1);
		goto fail;
	}
	if (!hog2)
	{
		printf("Failed to open %s\n", filename2);
		goto fail;
	}

	if (verbose)
		loadstart_printf("Generating diff...");
	if(flags & HogDiff_SkipMutex)
	{
		hogFileSetSkipMutex(hog1, true);
		hogFileSetSkipMutex(hog2, true);
	}
	else
	{
		hogFileSetSingleAppMode(hog1, true);
		hogFileSetSingleAppMode(hog2, true);
	}
	num_actions_allocated = hogFileGetNumFiles(hog1) + hogFileGetNumFiles(hog2);
	actions = calloc(sizeof(actions[0]), num_actions_allocated);
	for (i=0; i<hogFileGetNumFiles(hog1); i++)
	{
		const char *filename = hogFileGetFileName(hog1, i);
		if (!filename || hogFileIsSpecialFile(hog1, i))
			continue;
		actions[num_actions].name = filename;
		actions[num_actions].hog1_index = i;
		actions[num_actions].hog1_offset = hogFileGetOffset(hog1, i);
		actions[num_actions].hog2_index = hogFileFind(hog2, filename);
		if (actions[num_actions].hog2_index != HOG_INVALID_INDEX)
		{
			actions[num_actions].hog2_offset = hogFileGetOffset(hog2, actions[num_actions].hog2_index);
		}
		num_actions++;
	}
	// Find those only in hog2
	for (i=0; i<hogFileGetNumFiles(hog2); i++)
	{
		const char *filename = hogFileGetFileName(hog2, i);
		if (!filename || hogFileIsSpecialFile(hog2, i))
			continue;
		if (hogFileFind(hog1, filename) != HOG_INVALID_INDEX)
			continue;
		actions[num_actions].name = filename;
		actions[num_actions].hog1_index = HOG_INVALID_INDEX;
		actions[num_actions].hog1_offset = 0;
		actions[num_actions].hog2_index = i;
		actions[num_actions].hog2_offset = hogFileGetOffset(hog2, i);
		num_actions++;
	}
	assert(num_actions <= num_actions_allocated);
	if (verbose)
		loadend_printf(" done (%d unique files).", num_actions);

	if (verbose)
		loadstart_printf("Sorting...");
	qsort(actions, num_actions, sizeof(actions[0]), cmpDiffAction);
	if (verbose)
		loadend_printf(" done.");

	ret = 0;

	if (verbose)
		loadstart_printf("Comparing...\n");
	timerStart(timer);
	// Do diff
	for (i=0; i<num_actions; i++)
	{
		if (actions[i].hog1_index == HOG_INVALID_INDEX)
		{
			printf("%s: Exists in second hog only\n", actions[i].name);
			ret = -1;
		} else if (actions[i].hog2_index == HOG_INVALID_INDEX) {
			printf("%s: Exists in first hog only\n", actions[i].name);
			ret = -1;
		} else {
			// Exist in both
			U32 unpacked1, unpacked2, packed1, packed2;
			U32 timestamp1, timestamp2;
			U32 crc1, crc2;
			hogFileGetSizes(hog1, actions[i].hog1_index, &unpacked1, &packed1);
			hogFileGetSizes(hog2, actions[i].hog2_index, &unpacked2, &packed2);
			timestamp1 = hogFileGetFileTimestamp(hog1, actions[i].hog1_index);
			timestamp2 = hogFileGetFileTimestamp(hog2, actions[i].hog2_index);
			crc1 = hogFileGetFileChecksum(hog1, actions[i].hog1_index);
			crc2 = hogFileGetFileChecksum(hog2, actions[i].hog2_index);
			if (unpacked1 != unpacked2 || packed1 != packed2)
			{
				printf("%s: Different sizes - first:%d/%d  second:%d/%d\n",
					actions[i].name, unpacked1, packed1, unpacked2, packed2);
				ret = -1; // Is actually fine if it only got compressed and wasn't before
			}
			if (!(flags & HogDiff_IgnoreTimestamps) && timestamp1 != timestamp2)
			{
				printf("%s: Different timestamps - first:%d  second:%d\n",
					actions[i].name, timestamp1, timestamp2);
				ret = -1;
			}
			if (crc1 != crc2 && crc1 && crc2) // But ignore known 0 CRCs in ObjectDB code
			{
				// Not an error because if the data is the same, that's fine, it was probably repaired
				if (verbose >= 2)
					printf("Warning:%s: Different checksums - first:0x%08x  second:0x%08x\n",
						actions[i].name, crc1, crc2);
			}

			if (unpacked1 == unpacked2)
			{
				// Compare data too
				void *data1 = hogFileExtract(hog1, actions[i].hog1_index, &unpacked1, NULL);
				void *data2 = hogFileExtract(hog2, actions[i].hog2_index, &unpacked2, NULL);
				assert(unpacked1 == unpacked2);
				if (memcmp(data1, data2, unpacked1))
				{
					printf("%s: data differs between two hogs\n", actions[i].name);
					ret = -1;
				}
				SAFE_FREE(data1);
				SAFE_FREE(data2);
			}
		}
		if (verbose && (timerElapsed(timer) > 1.f || i == num_actions-1))
		{
			printf("%d/%d (%1.1f%%)    \r", i+1, num_actions, 100.f*(i+1)/(float)num_actions);
			timerStart(timer);
		}
	}
	if (verbose)
		loadend_printf(" done.");

	if(flags & HogDiff_SkipMutex)
	{
		hogFileSetSkipMutex(hog1, false);
		hogFileSetSkipMutex(hog2, false);
	}

fail:
	SAFE_FREE(actions);
	if (hog1)
		hogFileDestroy(hog1, true);
	if (hog2)
		hogFileDestroy(hog2, true);
	timerFree(timer);
	return ret;
}
