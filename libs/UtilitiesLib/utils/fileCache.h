#pragma once

// These functions keep copies of the file's data/checksum around in memory so
// you can reliably access them without hitting the disk (unless they've changed)
U32 fileCachedChecksum(const char *filename);
const void *fileCachedData(const char *filename, int *data_size);
const void *fileCachUpdateAndGetData(const char *filename, int *data_size, U32 known_timestamp);

// periodically frees data from the fileCache
void fileCachePrune(void);

// "freezes" the file cache during startup, it will assume no files have changed
void fileCacheFreeze(bool freeze);

//////////////////////////////////////////////////////////////////////////
// Creates a DirMonitor monitoring the specific folder that this file is in
// in order to deterministically get timestamps without hitting the disk.
// (Regular fileLastChanged() calls are not efficient outside of the GameDataDirs
//  and in some race conditions may return a timestamp which is slightly out of
//  date)
// This is independent of the above fileCache functions
U32 fileCacheGetTimestamp(const char *fullpath);
void fileCacheTimestampStopMonitoring(const char *fullpath);

