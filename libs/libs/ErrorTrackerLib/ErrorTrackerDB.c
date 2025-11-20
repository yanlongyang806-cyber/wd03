#include "ServerLib.h"
#include "file.h"
#include "logging.h"
#include "objTransactions.h"
#include "objContainerIO.h"
#include "objTransactionCommands.h"
#include "objBackupCache.h"
#include "hoglib.h"
#include "AutoStartupSupport.h"
#include "foldercache.h"

#include "ErrorTracker.h"
#include "ErrorEntry.h"
#include "ErrorTrackerDB_c_ast.h"
#include "ETCommon/ETShared.h"

void ErrorTracker_RegisterFastLocalCopy(void);
extern ParseTable parse_ErrorEntry[];
#define TYPE_parse_ErrorEntry ErrorEntry
char gErrorTrackerDBLogFilename[MAX_PATH];

bool gbIgnoreContainerSource = false;
bool gbCreateSnapshotMode = false;
AUTO_CMD_INT(gbIgnoreContainerSource, StaticHogg) ACMD_CMDLINE;
AUTO_CMD_INT(gbCreateSnapshotMode, CreateSnapshot) ACMD_CMDLINE;

#define ET_HOG_BUFFER_SIZE 1024*1024*1024

AUTO_STRUCT;
typedef struct DatabaseConfig
{
	char	*pDatabaseDir;						//"" Defaults to C:/<Product Name>/?
	bool	bNoHogs;							//force discrete container file storage.

	bool	bShowSnapshots;						//Whether to show/hide the snapshot console window.
	bool	bFastSnapshot;						//If we're making a snapshot do it the fast hog->hog way
	bool	bBackupSnapshot;					//After making a snapshot back it up ourselves, only works with fast snapshot
	bool    bForceSnapshot; // Try to force the Chat Server to save a snapshot

	U32		iIncrementalInterval;				//The number of minutes between incremental hog rotation (defaults to 5)
	U32		iSnapshotInterval;					//The number of minutes between snapshot creation		 (defaults to 120)
	U32		iIncrementalHoursToKeep;			//The number of hours to keep incremental files around.	 (default to 4)
} DatabaseConfig;

AUTO_STRUCT;
typedef struct ETDBCacheSettings
{
	bool bUseLRUCache; AST(DEFAULT(1))
	
	// Miss log file name (path is the local data dir) - NULL or "" means no logging
	char *pLRUMissLog;
	// Whether or not detailed info about misses (the IDs) should be logged for each miss
	bool bLRULogMissDetails;

	int iStashInitialSize; AST(DEFAULT(50000))
	int iLRUCacheSize; AST(DEFAULT(10000))
} ETDBCacheSettings;

DatabaseConfig gDatabaseConfig = {0};
static bool sbDBConfigLoaded = false;
char gDBConfigFileName[CRYPTIC_MAX_PATH] = "DBConfig.txt";
U32 * gChatServerHttpIps = NULL;

const DatabaseConfig *LoadDatabaseConfig(void)
{
	if (sbDBConfigLoaded)
	{
		return &gDatabaseConfig;
	}
	StructInit(parse_DatabaseConfig, &gDatabaseConfig);

	if (fileExists(STACK_SPRINTF("%s/%s", errorTrackerGetDatabaseDir(), gDBConfigFileName)))
	{
		printf("\nLoading Config File... \n\n");
		ParserReadTextFile(STACK_SPRINTF("%s/%s", errorTrackerGetDatabaseDir(), gDBConfigFileName), parse_DatabaseConfig, &gDatabaseConfig, 0);
	}

	//clean up config defaults
	if (!gDatabaseConfig.iSnapshotInterval) 
		gDatabaseConfig.iSnapshotInterval = 120; //Default snapshots to two hours.
	if (!gDatabaseConfig.iIncrementalInterval && !gDatabaseConfig.bNoHogs) 
		gDatabaseConfig.iIncrementalInterval = 5; //Default incrementals to 5min.
	if (!gDatabaseConfig.iIncrementalHoursToKeep && !gDatabaseConfig.bNoHogs) 
		gDatabaseConfig.iIncrementalHoursToKeep = 4;

	sbDBConfigLoaded = true;
	return &gDatabaseConfig;
}

AUTO_CMD_INT(gDatabaseConfig.bForceSnapshot, ForceHoggSnapshot) ACMD_CMDLINE;

int getSnapshotInterval(void)
{
	if (!sbDBConfigLoaded)
		LoadDatabaseConfig();
	return gDatabaseConfig.iSnapshotInterval;
}

static void dbLogRotateCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog)
{
	char *fname_orig = newIncrementalHog;
	char fname_final[MAX_PATH];
	char *dotStart = FindExtensionFromFilename(fname_orig);

	if (gErrorTrackerDBLogFilename[0])
	{
		logFlushFile(gErrorTrackerDBLogFilename);
	}

	strncpy(fname_final,fname_orig,dotStart - fname_orig);
	strcat(fname_final,".log");

	strcpy(gErrorTrackerDBLogFilename, fname_final);
	logSetFileOptions_Filename(gErrorTrackerDBLogFilename,true,0,0,1);
}
static void dbLogCloseCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog)
{
	logCloseAllLogs();
}

static void dbLogTransaction(const char *commitString, U64 trSeq, U32 timestamp)
{
	static char *logString;
	static char timeString[128];
	size_t size = strlen(commitString);

	PERFINFO_AUTO_START("etLogTransaction",1);

	if (!gErrorTrackerDBLogFilename[0])
	{
		sprintf(gErrorTrackerDBLogFilename, "%s/DB.log", errorTrackerGetDatabaseDir());			
		logSetFileOptions_Filename(gErrorTrackerDBLogFilename,true,0,0,1);
	}

	estrClear(&logString);

	timeMakeDateStringFromSecondsSince2000(timeString,timestamp);
	estrPrintf(&logString,"%"FORM_LL"u %s: %s\n",trSeq, timeString,commitString);
	logDirectWrite(gErrorTrackerDBLogFilename,logString);

	PERFINFO_AUTO_STOP();
}
CmdList gErrorTrackerUpdateCmdList = {1,0,0};
static void errorTrackerHandleDatabaseUpdateStringEx(const char *cmd_orig, U64 sequence, U32 timestamp, bool replay)
{
	int result = 0;
	static CmdContext context = {0};
	static char *message;
	static char *cmd_start;
	char *lineBuffer, *cmd;
	PERFINFO_AUTO_START("etHandleDatabaseUpdateString",1);

	context.output_msg = &message;
	context.access_level = 9;
	context.multi_line = true;

	estrCopy2(&cmd_start, cmd_orig);
	cmd = cmd_start;

	// This may cause a log rotation as well
	objRotateIncrementalHog();

	if (!timestamp)
	{
		timestamp = timeSecondsSince2000();
	}
	if (!sequence)
	{
		sequence = objContainerGetSequence() + 1;
	}
	objContainerSetSequence(sequence);
	objContainerSetTimestamp(timestamp);

	if (!replay) dbLogTransaction(cmd, sequence, timestamp);
	while (cmd)
	{
		lineBuffer = cmdReadNextLine(&cmd);
		result = cmdParseAndExecute(&gErrorTrackerUpdateCmdList,lineBuffer,&context);
		if (result)
		{
			S64 val;
			bool valid = false;
			val = MultiValGetInt(&context.return_val,&valid);
			if (val && valid)
			{
				result = 1;
			}
			else
			{
				result = 0;
			}
		}
		if (!result)
		{
			Errorf("Error \"%s\" while executing DBUpdateCommand: %s",message,lineBuffer);
		}
	}
	PERFINFO_AUTO_STOP();
}
static void errorTrackerUpdateCB(TransDataBlock ***pppBlocks, void *pUserData)
{
	int i;
	for (i=0; i < eaSize(pppBlocks); i++)
	{
		char *pString = (*pppBlocks)[i]->pString1;
		if (pString)
		{
			errorTrackerHandleDatabaseUpdateStringEx(pString, 0, 0, false);
		}
		pString = (*pppBlocks)[i]->pString2;
		if (pString)
		{
			errorTrackerHandleDatabaseUpdateStringEx(pString, 0, 0, false);
		}
	}
}

AUTO_COMMAND ACMD_LIST(gErrorTrackerUpdateCmdList);
int dbUpdateContainer(U32 containerType, U32 containerID, ACMD_SENTENCE diffString)
{
	Container *pObject;
	if (!containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbUpdateContainer",1);
	verbose_printf("Updating data of container %s[%d] using diff %s\n",GlobalTypeToName(containerType),containerID,diffString);

	pObject = objGetContainer(containerType,containerID);

	if (pObject)
	{
		objModifyContainer(pObject,diffString);
	}
	else
	{
		if (!objAddToRepositoryFromString(containerType,containerID,diffString))
		{
			Errorf("Couldn't add to repository");
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}
	PERFINFO_AUTO_STOP();
	return 1;
}
// Permanently destroys a container
AUTO_COMMAND ACMD_LIST(gErrorTrackerUpdateCmdList);
int dbDestroyContainer(char *containerTypeName, U32 containerID)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	if (!containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbDestroyContainer",1);

	verbose_printf("Permanently destroying container %s[%d]\n",containerTypeName,containerID);
	objRemoveContainerFromRepository(containerType,containerID);

	PERFINFO_AUTO_STOP();
	return 1;
}

void errorTrackerHandleDatabaseReplayString(const char *cmd_orig, U64 sequence, U32 timestamp)
{
	errorTrackerHandleDatabaseUpdateStringEx(cmd_orig, sequence, timestamp, true);
}

void ErrorTrackerDBInit(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ERRORTRACKERENTRY, parse_ErrorEntry, 
		NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_ERRORTRACKERENTRY_LAST, parse_ErrorEntry, 
		NULL, NULL, NULL, NULL, NULL);
	InitObjectLocalTransactionManager(GLOBALTYPE_ERRORTRACKER, NULL);
	LoadDatabaseConfig();

	// Loading database
	assertmsg(GetAppGlobalType() == GLOBALTYPE_ERRORTRACKER, "Error Tracker app type not set");
	objSetContainerSourceToHogFile(STACK_SPRINTF("%serrortracker.hogg", errorTrackerGetDatabaseDir()),
		gDatabaseConfig.iIncrementalInterval,dbLogRotateCB,dbLogCloseCB);

	ErrorTracker_RegisterFastLocalCopy();

	objSetContainerIgnoreSource(gbIgnoreContainerSource);
	objSetSnapshotMode(gbCreateSnapshotMode);

	if (gbIgnoreContainerSource)
	{
		objSetContainerForceSnapshot(true);
		objSetSnapshotMode(true);
	}
	else if (gDatabaseConfig.bForceSnapshot)
		objSetContainerForceSnapshot(true);

	hogSetGlobalOpenMode(HogSafeAgainstAppCrash);
	hogSetMaxBufferSize(ET_HOG_BUFFER_SIZE);

	objSetIncrementalHoursToKeep(gDatabaseConfig.iIncrementalHoursToKeep);
	objSetCommandReplayCallback(errorTrackerHandleDatabaseReplayString);
	RegisterDBUpdateDataCallback(objLocalManager(), &errorTrackerUpdateCB);
	if (gbCreateSnapshotMode)
	{
		objMergeIncrementalHogs(gDatabaseConfig.bBackupSnapshot, false, false);
	}
	else
	{
		loadstart_printf("Loading containers... ");
		objSetPurgeHogOnFailedContainer(true);
		objLoadAllContainers();
		LocalTransactionsTakeOwnership();
		objContainerLoadingFinished();
		loadend_printf("Done.");

		if (gbIgnoreContainerSource)
			gbCreateSnapshotMode = true;
	}
	if (!gbCreateSnapshotMode)
	{
		ContainerIterator iter = {0};
		ErrorEntry *pEntry;
		objInitContainerIteratorFromType(GLOBALTYPE_ERRORTRACKERENTRY, &iter);
		while (pEntry = objGetNextObjectFromIterator(&iter))
			ErrorEntry_AddHashStash(pEntry);
		objClearContainerIterator(&iter);
	}

	/*loadstart_printf("Clearing modify marks... ");
	objClearModifiedContainers(); // Loading marks them as modified, so clear that flag
	loadend_printf("Done.");*/
}

void* ErrorTracker_GetObject(GlobalType globalType, U32 uID)
{
	Container *con = objGetContainer(globalType, uID);
	if (con)
		return con->containerData;
	return NULL;
}

void *ErrorTracker_GetBackup(GlobalType globalType, ContainerID uID)
{
	Container *con = objGetContainer(globalType, uID);
	if (!con)
		return NULL;
	return BackupCacheGet(con);
}

#define ETCACHE_CONFIG_FILE "cache_config.txt"
static ETDBCacheSettings sETCacheSettings = {0};
static bool sbDisableFastTransactions = 0;
AUTO_CMD_INT(sbDisableFastTransactions, DisableFastTransact);
void ErrorTracker_LoadCacheSettings(void);

static void ErrorTracker_ReloadCacheSettings(const char *relpath, int when)
{
	loadstart_printf("Reloading Cache Settings...");
	ErrorTracker_LoadCacheSettings();
	loadend_printf("done");
}

void ErrorTracker_LoadCacheSettings(void)
{
	static bool sbLoadedOnce = false;
	char filepath[MAX_PATH];
	sprintf(filepath, "%s%s", errorTrackerGetSourceDataDir(), ETCACHE_CONFIG_FILE);
	if (fileExists(filepath))
	{
		printf("\nLoading Cache Config File... \n\n");
		StructDeInit(parse_ETDBCacheSettings, &sETCacheSettings); // Clear old settings from this
		ParserReadTextFile(filepath, parse_ETDBCacheSettings, &sETCacheSettings, 0);
	}
	if (!sbLoadedOnce)
	{
		char relfilepath[MAX_PATH];
		sprintf(relfilepath, "server/ErrorTracker/Data/%s", ETCACHE_CONFIG_FILE);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, relfilepath, ErrorTracker_ReloadCacheSettings);

		sbLoadedOnce = true;
	}
	if (sETCacheSettings.pLRUMissLog && sETCacheSettings.pLRUMissLog[0])
	{
		char lruLogPath[MAX_PATH];
		sprintf(lruLogPath, "%s/%s", errorTrackerGetDatabaseDir(), sETCacheSettings.pLRUMissLog);
		LRUCacheSetMissLogFile(lruLogPath);
	}
	else
		LRUCacheSetMissLogFile("");
	LRUCache_EnableMissDetails(sETCacheSettings.bLRULogMissDetails);
	BackupCache_RegisterType(GLOBALTYPE_ERRORTRACKERENTRY, 
		sETCacheSettings.bUseLRUCache ? BACKUPCACHE_LRU : BACKUPCACHE_STASH, 
		sETCacheSettings.bUseLRUCache ? sETCacheSettings.iLRUCacheSize : sETCacheSettings.iStashInitialSize);
}

void ErrorTracker_RegisterFastLocalCopy(void)
{
	if (!sbDisableFastTransactions)
	{
		ErrorTracker_LoadCacheSettings();
		RegisterFastLocalCopyCB(GLOBALTYPE_ERRORTRACKERENTRY, ErrorTracker_GetObject, ErrorTracker_GetBackup);
	}
}

#include "ErrorTrackerDB_c_ast.c"