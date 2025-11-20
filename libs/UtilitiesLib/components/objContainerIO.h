#pragma once
GCC_SYSTEM

// Defines the interface for loading and saving containers to the file system
typedef U32 HogFileIndex;

#include "objContainer.h"
#include "objPath.h"
#include "windefinclude.h"

#define DB_HOG_DATALIST_JOURNAL_SIZE (isProductionMode()?(32*1024*1024):(70*1024)) // Defaulting to 70k in development just so it's more than the old 64k to exercise the new code
#define CONTAINER_INFO_FILE "ContainerInfo.info"

typedef struct MultiWorkerThreadManager MultiWorkerThreadManager;
typedef struct ContainerLoadRequest ContainerLoadRequest;
// Public functions. Use these to kick off container io functionality

// Called when various container operations are complete
typedef void (*ContainerIOCB)(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog);

// Called during replay at each new log file. Processing is not expected to be done in this callback. This is primarily used during synthetic log creation.
// Return false to stop the replay. Return true to continue the replay.
typedef bool (*CommandLogFileReplayCB)(const char *log_file);

// Called to replay a transaction from a log.
typedef void (*CommandReplayCB)(const char *command, U64 sequence, U32 timestamp);

// Called per container if objSetDumpLoadedContainerCallback() is used, just prior to the container being cleaned up
typedef void (*DumpLoadedContainerCallback)(Container *con, U32 uContainerModifiedTime);

// Called immediately prior to a modified container save operation
typedef void (*PreSaveCallback)(void);

// Set a hog file to load containers from
// incrementalMinutes, if not 0, is how often to generate a new incremental hog. 0 means write full hog instead
// rolloverCB gets called when the incremental hog rolls over
// shutdownCB gets called when logs should be closed
bool objSetContainerSourceToHogFile(const char *hogfile, U32 incrementalMinutes, 
									ContainerIOCB rolloverCB, ContainerIOCB shutdownCB);

U64 objGetConupEstrCount(void);
U64 objGetConupEstrTotalSize(void);
void objSetConupEstrSize(size_t maxSize, size_t removeSize);

typedef struct WorkerThread WorkerThread;
typedef struct IncrementalLoadState IncrementalLoadState;

AUTO_STRUCT;
typedef struct ContainerRepositoryTypeInfo 
{
	GlobalType eGlobalType; AST(KEY)
	ContainerID iMaxContainerID;
} ContainerRepositoryTypeInfo;

AUTO_STRUCT AST_IGNORE_STRUCT(DeletedContainers);
typedef struct ContainerRepositoryInfo {
	U64 iLastSequenceNumber;
	U32 iLastTimeStamp;
	U64 iCurrentSequenceNumber;
	U32 iCurrentTimeStamp;
	U32 iBaseContainerID;
	U32 iMaxContainerID;
	ContainerRepositoryTypeInfo **ppRepositoryTypeInfo;
} ContainerRepositoryInfo;

AUTO_STRUCT;
typedef struct ContainerIOConfig {
	char MergerScript[1024];
	char OfflineBackupScript[1024];
} ContainerIOConfig;

AUTO_STRUCT;
typedef struct ContainerSource {
	ContainerRepositoryInfo *sourceInfo;

	char sourcePath[MAX_PATH];
	//This should never be unset from the main thread.
	HogFile *fullHog;						NO_AST

	char incrementalPath[MAX_PATH];
	//This should never be unset from the main thread.
	HogFile *incrementalHog;				NO_AST

	char offlinePath[MAX_PATH];
	HogFile *offlineHog;					NO_AST

	bool thread_initialized;

	CRITICAL_SECTION hog_access;			NO_AST
	//Thread for update/delete hog operation
	WorkerThread *hog_update_thread;		NO_AST

	ContainerIOCB incrementalRolloverCB;	NO_AST
	ContainerIOCB incrementalShutdownCB;	NO_AST

	CommandLogFileReplayCB logFileReplayCB;	NO_AST
	CommandReplayCB replayCB;				NO_AST

	//incremental rotate time
	U32 incrementalMinutes;

	//incremental and log keep time for main dir
	U32 incrementalHoursToKeep;

	//snapshot keep times for old dir
	U32 snapshotMinutesToKeep;
	U32 snapshotHoursToKeep;
	U32 snapshotDaysToKeep;

	//offline keep times for old dir
	U32 offlineDaysToKeep;

	//incremental hog and log keep times for old dir
	U32 pendingHoursToKeep;
	U32 journalHoursToKeep;

	U32 skipperiodiccontainersaves; //Add extra container saves between incremental rotates.
	U32 savecontainerflushperiod; //seconds to wait before flushing containers to the incremental.
	U32 savecontainernextflush;	//last time containers were flushed

	U32 nextRotateTime;
	U32 lastRotateTime;
	U32 scanSnapshotTime;	//if we scanned a snapshot, keep the time here.
	U64 scanSnapshotSeq;	//if we scanned a snapshot, keep the seq here.

	//bool writeFull;
	bool validSource;
	bool useHogs;
	bool ignoreContainerSource;
	bool forceSnapshot;
	//bool hogMerge;

	//This startup flag tells the process to merge, create a snapshot, and exit.
	bool createSnapshot;

	//this startup flag tells the merger to generate a log report.
	bool generateLogReport;

	//Uses more stringent log parsing for replay.
	bool strictMerge;

	//Unloads the uncompressed container when 
	bool unloadOnHeaderCreate;					AST(DEFAULT(1))

	//This startup flag tells the process to dump web data as it reads the latests snapshot.
	char * dumpWebData;

	//Force hog reading to disk order
	// 1 = in-order requests; 2 = in-order requests AND main thread file extract
	int hogUseDiskOrder;

	U32	containerPreloadThresholdHours;			AST(DEFAULT(4))//The window of which characters to unpack on load during lazy loading in hours
	U32	containerPlayedUnloadThresholdHours;	AST(DEFAULT(5))//The window on played time of which characters to unpack during operation in hours
	U32	containerAccessUnloadThresholdHours;	AST(DEFAULT(1))//The window on accessed time of which characters to unpack during operation in hours
		
	bool enableStaleContainerCleanup;			AST(DEFAULT(1))// Turns on the cleanup of stale containers using containerPreloadThresholdHours as the measure of when.
	
	U32 forceUnpackThreshold;

	// Queue of containers to save out slowly (for header updates)
	ContainerRef **throttledSaveQueue;			NO_AST

	//Use multithreading load
	int loadThreads;

	int dumpload;				//true if we're loading just to dump.
	GlobalType dumpContainerType;
	DumpLoadedContainerCallback dumpLoadedContainerCallback; NO_AST

	int purgeHogOnFailedContainer;

	bool needsRotate;
	bool rotateQueued;

	U32 lastSaveTick;							NO_AST
	PreSaveCallback preSaveCallback;			NO_AST
} ContainerSource;

extern ContainerSource gContainerSource;

ContainerIOConfig *objGetContainerIOConfig(ContainerIOConfig *copyfromandfree);

void objSetSkipPeriodicContainerSaves(bool enabled);

// Gets the path of the source directory of hog file
const char * objGetContainerSourcePath(void);

// Set to true to load a static hogg with no ContainerSource sequence or timestamp info to load as an incremental
void objSetContainerIgnoreSource(bool ignore);
void objSetContainerForceSnapshot(bool forceSnapshot);

// Set a directory to load containers from
bool objSetContainerSourceToDirectory(const char *directory);

//Set to true to make process dump web data during snapshot loading.
void objSetDumpWebDataMode(char * dumpfile);

//Set to true to make process rename files, create snapshot, and exit after load.
void objSetSnapshotMode(bool snapshotMode);

//When dumping only, set this mode so that we can suppress errors and do other special case things.
void objSetDumpMode(int mode);
void objSetDumpType(GlobalType type);
void objSetDumpLoadedContainerCallback(DumpLoadedContainerCallback cb);

//Set a callback to occur immediately before a modified container save occurs
void objSetPreSaveCallback(PreSaveCallback cb);

//Set the number of threads to use while loading if enabled.
void objSetMultiThreadedLoadThreads(int count);
// Closes a container source and flushes anything
bool objCloseContainerSource(void);
// When a loaded container is thrown out, purge the failed container from the hog
void objSetPurgeHogOnFailedContainer(bool purgehog);

//Closes incrmental hogs and calls shutdown callback for logging.
void objCloseAllWorkingFiles(void);

// Sets the callback function to for log file replay.
void objSetCommandLogFileReplayCallback(CommandLogFileReplayCB logFileReplayCB);

// Sets the callback function to allow containerIO functions to replay commands.
void objSetCommandReplayCallback(CommandReplayCB replayCB);

//Sets the time in hours before the last snapshot to keep. (or rather the threshold to delete)
void objSetIncrementalHoursToKeep(U32 hours);

bool objLoadFileData(ContainerLoadRequest* request);

// Load all containers from disk
void objLoadAllContainers(void);

// Load schema and containers of a specific type from a specific hogg.
#define objLoadContainersFromHoggForDump(filename, type) objLoadContainersFromHoggForDumpEx(filename, type)
bool objLoadContainersFromHoggForDumpEx(const char *filename, GlobalType type);
bool objLoadContainersFromOfflineHoggForDumpEx(const char *filename, GlobalType type);

typedef struct ContainerHogLoadState ContainerHogLoadState;

typedef struct ContainerLoadIterator
{
	ContainerHogLoadState *state;
	IncrementalLoadState *loadstate;
	char *offlineFilename;
	bool loadingOfflineHog;
	GlobalType type;
} ContainerLoadIterator;

void objContainerLoadIteratorInit(ContainerLoadIterator *iter, const char *filename, const char *offlineFilename, GlobalType type, int numThreads);

Container * objGetNextContainerFromLoadIterator(ContainerLoadIterator *iter, U32 * uLastModifiedTimeOut);

void objClearContainerLoadIterator(ContainerLoadIterator *iter);

void objFindLogsToReplay();
void objReplayLogs();
int objReplayLog(char *filename);
int objReplayLogThrottled(char *filename, FileWrapper **file, int iMaxTransactions, bool bReplaySyntheticLogDataInRealtime, U32 iStopSyntheticReplayAtTimestamp);

// Run once per frame, will correctly save out any modified containers, according to the policies
void objContainerSaveTick(void);

// Flush container saving, before a shutdown
void objFlushContainers(void);

// We're done loading containers, possibly close the hog
void objContainerLoadingFinished(void);

// Sets the next rotate time so we're not rotating on the first call to objRotateIncrementalHog().
void objInitIncrementalHogRotation(void);

// Rotates hogs if necessary. You should call this at journal time if you have a journal
void objRotateIncrementalHog(void);

//Forces hog rotation.
void objForceRotateIncrementalHog(void);

//Scans logs for queries to be replayed during transaction tests.
void objScanLogsForQueries(StringQueryList *list);

//Scans logs and writes a statistical report.
void objGenerateLogReport(void);

//Get the time interval in seconds since the last rotation.
U32 objGetLastIncrementalRotation(void);

//Get the last snapshot scanned; only really useful immediately after load.
U32 objGetLastSnapshot(void);

//Rotates the incremental hog then saves the complete container store out to a dated Snapshot hog.
void objSaveSnapshotHog(bool waitForFlush);

//#define TRACK_MEAN_SERVICE_TIME

//Returns the number of containers of a type unloaded for free (i.e. repacked) since DB startup
int objCountFreeRepacksOfType(GlobalType eType);

//Returns the number of containers of a type unloaded (i.e. repacked) since DB startup
int objCountUnloadedContainersOfType(GlobalType eType);

//Returns the number of containers of a type preloaded (i.e. unpacked) at DB startup
int objCountPreloadedContainersOfType(GlobalType eType);

//Returns the number of destroyed containers of a type since DB startup
int objCountDestroyedContainersOfType(GlobalType eType);

//Returns the average disk service over the last minute (in seconds).
F32 MeanServiceTime(void);

//Returns the number of pending persists to disk.
U32 PendingWrites(void);

AUTO_STRUCT;
typedef struct ContainerRestoreState
{
	char *hog_file; AST(ESTRING)
	char *filename; AST(ESTRING)
	bool isSnap;
} ContainerRestoreState;

AUTO_STRUCT;
typedef struct DatabaseFileList
{
	char **files;		AST(FORMATSTRING(XML_UNWRAP_ARRAY = 1))
} DatabaseFileList;

/*
AUTO_STRUCT;
typedef struct ContainerRSInfo
{
	ContainerRef *ref;
	char * filename; AST(ESTRING)
	char * linkname; AST(ESTRING)
	ContainerRestoreState **ppStates;
} ContainerRSInfo;
*/

//Quickly return a list of all the filenames we can (possibly) extract a viable container from.
DatabaseFileList *objFindAllDatabaseHogs(void);
//Given a file from the above function, get the restore state struct (if it is possible), or NULL if not. This is Slow!
ContainerRestoreState *objCheckRestoreState(const char *file, GlobalType containerType, ContainerID containerID);
//Get the restore state struct for the container in currently open offline.hogg
ContainerRestoreState *objCheckOfflineRestoreState(GlobalType containerType, ContainerID containerID);

//ContainerRSInfo * objContainerFindRestoreStates(GlobalType containerType, ContainerID containerID);
//void objContainerReleaseRestoreStates(ContainerRSInfo *ptr);

LATELINK;
void objGetDependentContainers(GlobalType containerType, Container **con, ContainerRef ***pppRefs, bool recursive);
bool objContainerGetRestoreStringFromState(GlobalType containerType, ContainerID containerID, ContainerRestoreState *rs, char **diffstr, ContainerRef ***pppRefs, bool recursive, bool alertOnExistingContainer);
bool objContainerGetDependentContainersFromState(GlobalType containerType, ContainerID containerID, ContainerRestoreState *rs, ContainerRef ***pppRefs, bool recursive);
void* objOpenRestoreStateHog(ContainerRestoreState *rs);
void objCloseRestoreStateHog(void *the_hog);

bool objContainerGetRestoreString(GlobalType containerType, ContainerID containerID, char *filename, char **outString, char **resultString);

// Internal use Container IO. Don't call these directly

AUTO_STRUCT;
typedef struct ContainerRefMapping
{
	GlobalType type;
	ContainerID oldID;
	ContainerID newID;
} ContainerRefMapping;

// Unpacks a container into a passed in pointer from (possibly) zipped data
int objUnpackContainerEx(ContainerSchema *schema, Container *con, void **containerData, int keepFileData, int onlyGetFileData, int hasLock);

#define objUnpackContainer(schema, con, keepFileData, onlyGetFileData, hasLock) objUnpackContainerEx(schema, con, &con->containerData, keepFileData, onlyGetFileData, hasLock)

// Load a single container from disk and return it (does not modify global stores or hogg)
Container *objLoadTemporaryContainer(GlobalType containerType, ContainerID containerID, HogFile* the_hog, U32 hogIndex, bool autoLoadArchivedSchema);

//Imports a container from disk, overriding the containerID in the process. Error messages estrPrintf'd to resultString on failure.
// containerID and type are passed back by ref.
bool objImportContainer(const char *fileName, ContainerID *containerID, GlobalType *containerType, char **resultString);

// Save a single container to disk
bool objSaveContainer(Container *container);

// Loads a schema from a hogg if a schema for that GlobalType isn't already represented
bool objLoadSchemaSource(GlobalType type, HogFile *the_hog);

// Updates a hogg with all currently registered schemas
bool objUpdateAllSchemas(const char *hoggFilename);

// Permanently delete from disk all containers that have been removed. return the number removed.
int objPermanentlyDeleteRemovedContainers(void);

// Delete an invalid container from disk
bool objDeleteInvalidContainer(GlobalType containerType, ContainerID containerID);

// Save all containers to disk
void objSaveAllContainers(void);

//Save charname:containerid:displayname for a characters to a file. No expressions here, so should be fast.
void objSaveWebDataToFile(char *file);

// Clear the list of modified (but not deleted) containers without saving them
void objClearModifiedContainers(void);


// Closes an open hog file backup *Deprecated
bool objCloseIncrementalHogFile(void);

//Returns true if the string represents an incremental snapshot filename.
bool objStringIsSnapshotFilename(const char *filename);

bool objStringIsOfflineFilename(char *filename);
bool objStringIsTempOfflineFilename(char *filename);

//Returns true if the file has a valid ContainerInfo.info file inside it.
bool objHogHasValidContainerSourceInfo(char *filename);

static void objInternalRotateIncrementalHog(void);

void objHandleUpdateDuringSave(Container *con);
void objHandleDestroyDuringSave(Container *con);

//Convert an incremental filename
static void objConvertToMergedFilename(char *pendingFilename);
static void objConvertToPendingFilename(char *incrementalFilename);

static void objUpgradeNonHoggedDatabase();

// Merges incrementals into a snapshot
void objMergeIncrementalHogs(bool bWriteBackup, bool bAppend, bool bMoveBackupToOld);

// Defrags the most recent snapshot
void objDefragLatestSnapshot();

//************* These functions should not be used; alas they are...

// Gets most recent timestamp in db
U32 objContainerGetTimestamp(void);

U32 objContainerGetSystemTimestamp();

// Updates most recent timestamp, if it is newer
void objContainerSetTimestamp(U32 timeStamp);

// Sets the maximum id for the given type in container info
void objContainerSetMaxID(GlobalType eGlobalType, ContainerID iMaxID);

// Gets last sequence number for repository
U64 objContainerGetSequence(void);

// Updates most recent sequence, if it is newer
void objContainerSetSequence(U64 sequence);

bool dbDumpSchemaToSQL(const char *namedtype, const char *to_dir);
char * objDumpDatabaseToSQL(const char *namedtype, char *path);

// return -1 to use the standard callback. Every other value indexes into the extra cutoff array, which has max size MAX_STALE_CUTOFFS
typedef int (*CutoffIndexCallback)(Container *con);
typedef void (*MakeExtraCutoffsCallback)(U32 now, U32 multiplier, U32 cutoff, U32* extracutoffs);
typedef bool (*NotStaleCallback)(Container *con, U32 cutoff);

void activateStaleContainerCleanup(void);
void deactivateStaleContainerCleanup(void);

void objStaleContainerTypeAdd(GlobalType containerType, CutoffIndexCallback cutoffIndexCB, MakeExtraCutoffsCallback extraCutoffsCB, NotStaleCallback notStaleCB);

void objWriteContainerToOfflineHog(GlobalType containerType, ContainerID containerID);
void objRemoveContainerFromOfflineHog(GlobalType containerType, ContainerID containerID);

void objUnloadContainer(Container *con);

// bAppendMode only matters if bAllowWrites is true
HogFile *OpenOfflineHogg(bool bAllowWrites, bool bAppendMode, bool *bCreated);
char *GetCurrentOfflineHogFilename(void);
void CleanupTempOfflineHogFiles(void);
void objMakeOfflineFilename(char **estr, U32 atTime);

void FlushOfflineHogg();
void CloseOfflineHogg();
void LaunchOfflineBackupScript();
void ReOpenOfflineHoggForRestores();

#define CONTAINER_LOGPRINT(str, ...) \
	fprintf(fileGetStderr(), str, ##__VA_ARGS__); \
	log_printf(LOG_CONTAINER, str, ##__VA_ARGS__);

void FreeZippedContainerData(Container *con);

char *malloc_FileData(size_t size);

U32 GetDefragDay();
bool TimeToDefrag(U32 iDaysBetweenDefrags, U32 lastDefragDay, U32 iTargetDefragWindowStart, U32 iTargetDefragWindowDuration);

bool QueueContainerHogLoad(HogFile *handle, HogFileIndex hogIndex, const char* filename, void *userData);

typedef struct ContainerLoadRequest
{
	MultiWorkerThreadManager * threadManager;
	GlobalType containerType;
	ContainerID containerID;
	U32 deletedTime;
	U64 sequenceNumber;
	HogFile * the_hog;
	U32 hog_index;
	U64 hog_file_offset;

	const char *fileName;
	char *fileData;
	U32 fileSize;
	U32 bytesCompressed;
	U32 overwrite : 1;
	U32 fromSnapshot : 1;
	U32 deleted : 1;
	U32 useHeader : 1;
	U32 lazyLoad : 1;
	U32 hasIndex : 1;
	U32 skipFileData : 1;
	U32 snapshotMarkDeleted : 1; // During a merger, we found a deleted marker so recreate it on the destination
	U32 removeLatestMarker : 1; // During a merger, we are removing all references to a container, so remove the .lcon

	//result
	U32 removeContainer : 1;
	U32 deletedContainer : 1;
	U32 invalid : 1;
	U32 headerAdded : 1;
	Container* newContainer;
	U32 *completeCount;
} ContainerLoadRequest;

typedef struct ContainerLoadThreadState
{
	MultiWorkerThreadManager *threadManager;
	U32 requestCount;
	U32 lastUpdate;
	char cacheline[1024];
	U32 fileCount;
	U32 completeCount;
	U64 maxSequence;
	GlobalType requiredType;
	bool bReadDeletes;
	bool addToDiskStorageHog;

	// Container load times by type.
	U64 *ppContainerLoadCpuTicks;
	U64 uLastCpuTick; 

	ContainerLoadRequest **ppRequests;
} ContainerLoadThreadState;

typedef struct FileRenameRequest {
	char *filename;
	char *newname;
	bool moveToOld;
} FileRenameRequest;

AUTO_STRUCT;
typedef struct IncrementalLogRef {
	U64 sequenceNum;
	__time32_t  time_write;
	char *filename; AST(ESTRING)
} IncrementalLogRef;

typedef struct IncrementalLoadState {
	int fileCount;
	S64 containersRequested;
	S64 containersLoaded;
	IncrementalLogRef **ppLogs;
	FileRenameRequest **renames;

	//Only for dumps
	GlobalType dumptype;			//specify a type to only load containers of that type; also use the archived schema.

	// For fast merging
	ContainerLoadThreadState *loadThreadState;
	HogFile **openedIncrementals;

} IncrementalLoadState;

void EStringFreeCallback(char *string);
int sortContainerLoadRequestsByFile(const ContainerLoadRequest **pRequest1, const ContainerLoadRequest **pRequest2);
void AddRequestsToThreadState(const char *filename, ContainerLoadThreadState *threadState);
void AddOfflineRequestsToThreadState(const char *filename, ContainerLoadThreadState *threadState);
void ContainerHogMarkLatest(HogFile *snapshotHog, GlobalType conType, ContainerID conID, U64 seq);
void ContainerHogCopyToSnapshot(HogFile *snapshotHog, ContainerLoadThreadState *threadState, ContainerLoadRequest *request, bool writeSequenceNumber);
void ContainerHogMarkDeleted(HogFile *snapshotHog, GlobalType conType, ContainerID conID, U64 seq);

ContainerUpdate *PopulateContainerUpdate(Container *object);
void objWriteContainerToSnapshot(HogFile *outputSnapshot, Container *container, U64 sequenceNumber);

#define MAX_STALE_CUTOFFS 10