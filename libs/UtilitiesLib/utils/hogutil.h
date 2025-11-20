#pragma once
GCC_SYSTEM
#include "stdtypes.h"
#include "hoglib.h"

typedef enum HogDefragFlags {
	HogDefrag_Default = 0,
	HogDefrag_RunDiff = 1 << 0,		// runs a diff between the original and defrag'd file and asserts they are correct
	HogDefrag_NoRename = 1 << 1,	// does not rename over original input file
	HogDefrag_Tight = 1 << 2,		// creates a close to optimally tightly packed output file (will be slow to add to).  Set datalist_journal_size to 1024 to be as small as possible
	HogDefrag_SkipMutex = 1 << 3,	// Skip the mutex when reading from the source file.
	HogDefrag_DontPack = 1 << 4,	// Don't compress destination files
} HogDefragFlags;

// Returns 0 on success
// All handles to this hog must be currently closed
int hogDefragEx(const char *filename, U32 datalist_journal_size, HogDefragFlags defrag_flags, char *tempnameout, size_t tempnameout_size);
#define hogDefrag(filename, datalist_journal_size, defrag_flags) hogDefragEx(filename, datalist_journal_size, defrag_flags, NULL, 0)
// roughMaxSize == 0 implies all into a single file, no max size
// only HogDefrag_Tight is listened to out of defrag_flags
int hogSync(const char *outname, const char *srcname, U64 roughMaxSize, HogFileCreateFlags open_flags, U32 datalist_journal_size, HogDefragFlags defrag_flags);
void hogSyncSetNumReaderThreads(int num_threads); // Defaults to number of CPUs on PC
void hogSyncSetMemoryCap(unsigned long max);

typedef enum HogDiffFlags {
	HogDiff_Default = 0,
	HogDiff_IgnoreTimestamps = 1 << 0,
	HogDiff_SkipMutex = 1 << 1, // Skip the mutex when reading from the source file.
} HogDiffFlags;

int hogDiff(const char *hog1, const char *hog2, int verbose, HogDiffFlags flags);

// Used internally by Hogg
typedef struct HogFreeSpaceTracker2 HogFreeSpaceTracker2;
HogFreeSpaceTracker2 *hfst2Create(void);
void hfst2SetStartLocation(HogFreeSpaceTracker2 *hfst, U64 start_location);

bool hfst2AllocSpace(HogFreeSpaceTracker2 *hfst, U64 offs, U64 size);
void hfst2DoneAllocingSpace(HogFreeSpaceTracker2 *hfst);

void hfst2FreeSpace(HogFreeSpaceTracker2 *hfst, U64 offs, U64 size);
U64 hfst2GetSpace(HogFreeSpaceTracker2 *hfst, U64 minsize, U64 desiredsize);
U64 hfst2GetTotalSize(HogFreeSpaceTracker2 *hfst);
U64 hfst2GetLargestFreeSpace(HogFreeSpaceTracker2 *hfst);

void hfst2SetAppendOnly(HogFreeSpaceTracker2 *hfst, bool bAppendOnly); // Can be enabled/disabled as required

void hfst2Destroy(HogFreeSpaceTracker2 *hfst);


