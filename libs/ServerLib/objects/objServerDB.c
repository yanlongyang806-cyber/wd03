#include "objServerDB.h"

#include "cmdparse.h"
#include "error.h"
#include "estring.h"
#include "file.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "objBackupCache.h"
#include "objContainer.h"
#include "objContainerIO.h"
#include "timing.h"
#include "timing_profiler.h"
#include "TransactionSystem.h"
#include "utils.h"

#include "AutoGen/objServerDB_h_ast.h"

static char gDBLogFilename[MAX_PATH] = "";
static bool gbCacheDBLog = false;
static char * gpCachedDBLog = NULL;

void serverdbSetLogFileName(const char *filename)
{
	sprintf(gDBLogFilename,"%s", filename);
	logSetFileOptions_Filename(gDBLogFilename,true,0,0,1);
}

static void serverdbWriteDBLogCache(void)
{
	assert(gDBLogFilename[0]);
	logDirectWrite(gDBLogFilename, gpCachedDBLog);
	logFlushFile(gDBLogFilename);
	estrClear(&gpCachedDBLog);
}

void serverdbLogRotateCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog)
{
	char *fname_orig = newIncrementalHog;
	char fname_final[MAX_PATH];
	char *dotStart = FindExtensionFromFilename(fname_orig);

	if(gDBLogFilename[0])
	{
		char buf[MAX_PATH];
		char *ext = NULL;

		if(gbCacheDBLog)
		{
			serverdbWriteDBLogCache();
		}
		else
		{
			logFlushFile(gDBLogFilename);
		}

		//rename the file to .log
		strcpy(buf, gDBLogFilename);
		ext = FindExtensionFromFilename(buf);
		ext[0] = '\0';
		strcat(buf, ".log");

		logFlushAndRenameFile(gDBLogFilename, buf);
	}

	strncpy(fname_final,fname_orig,dotStart - fname_orig);
	strcat(fname_final,".lcg");

	strcpy(gDBLogFilename, fname_final);
	logSetFileOptions_Filename(gDBLogFilename,true,0,0,1);
}

void serverdbLogCloseCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog)
{
	logCloseAllLogs();
}

static bool gbAllowDBLogCache = true;
AUTO_CMD_INT(gbAllowDBLogCache, AllowServerDBLogCache) ACMD_CMDLINE;

static void serverdbEnableDBLogCacheHint(void)
{
	PERFINFO_AUTO_START_FUNC();

	gbCacheDBLog = gbAllowDBLogCache;
	if(gpCachedDBLog)
	{
		estrClear(&gpCachedDBLog);
	}
	else if(gbCacheDBLog)
	{
		estrCreate(&gpCachedDBLog);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void serverdbDisableDBLogCacheHint(void)
{
	PERFINFO_AUTO_START_FUNC();

	gbCacheDBLog = false;
	if (estrLength(&gpCachedDBLog) > 0)
	{
		serverdbWriteDBLogCache();
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void serverdbLogTransaction(const char *commitString, U64 trSeq, U32 timestamp)
{
	static char *logString;
	static char timeString[128];
	size_t size = strlen(commitString);

	PERFINFO_AUTO_START("dbLogTransaction",1);

	estrClear(&logString);

	timeMakeDateStringFromSecondsSince2000(timeString,timestamp);
	estrPrintf(&logString,"%"FORM_LL"u %s: %s\n",trSeq, timeString,commitString);

	if (gbCacheDBLog)
	{
		estrAppend(&gpCachedDBLog, &logString);
		if (estrLength(&gpCachedDBLog) > 1024 * 1024) {
			serverdbWriteDBLogCache();
		}
	}
	else
	{
		logDirectWrite(gDBLogFilename,logString);
	}

	PERFINFO_AUTO_STOP();
}
CmdList gdbUpdateCmdList = {1,0,0};
static void serverdbHandleDatabaseUpdateStringEx(const char *cmd_orig, U64 sequence, U32 timestamp, bool replay)
{
	int result = 0;
	static CmdContext context = {0};
	static char *message;
	static char *cmd_start;
	char *lineBuffer, *cmd;
	PERFINFO_AUTO_START("dbHandleDatabaseUpdateString",1);

	context.output_msg = &message;
	context.access_level = 9;
	context.multi_line = true;

	estrCopy2(&cmd_start, cmd_orig);
	cmd = cmd_start;

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

	if (!replay) serverdbLogTransaction(cmd, sequence, timestamp);
	while (cmd)
	{
		lineBuffer = cmdReadNextLine(&cmd);
		result = cmdParseAndExecute(&gdbUpdateCmdList,lineBuffer,&context);
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
			AssertOrAlert("DB_APPLY", "Error \"%s\" while executing DBUpdateCommand: %s",message,lineBuffer);
		}
	}
	PERFINFO_AUTO_STOP();
}

void serverdbUpdateCB(TransDataBlock ***pppBlocks, void *pUserData)
{
	int i;
	for (i=0; i < eaSize(pppBlocks); i++)
	{
		char *pString = (*pppBlocks)[i]->pString1;
		if (pString)
		{
			serverdbHandleDatabaseUpdateStringEx(pString, 0, 0, false);
		}

		pString =  (*pppBlocks)[i]->pString2;
		if (pString)
		{
			serverdbHandleDatabaseUpdateStringEx(pString, 0, 0, false);
		}

	}
}

AUTO_COMMAND ACMD_NAME(dbUpdateContainer) ACMD_LIST(gdbUpdateCmdList);
int serverdbUpdateContainer(U32 containerType, U32 containerID, ACMD_SENTENCE diffString)
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
AUTO_COMMAND ACMD_NAME(dbDestroyContainer) ACMD_LIST(gdbUpdateCmdList);
int serverdbDestroyContainer(char *containerTypeName, U32 containerID)
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

void serverdbHandleDatabaseReplayString(const char *cmd_orig, U64 sequence, U32 timestamp)
{
	serverdbHandleDatabaseUpdateStringEx(cmd_orig, sequence, timestamp, true);
}

#define DBSERVER_HOG_BUFFER_SIZE 1024*1024*1024

const ServerDatabaseConfig *GetServerDatabaseConfig(const char *configFile)
{
	static ServerDatabaseConfig *pDatabaseConfig = NULL;

	if (pDatabaseConfig)
	{
		return pDatabaseConfig;
	}
	pDatabaseConfig = StructCreate(parse_ServerDatabaseConfig);

	//load the config
	if (fileExists(configFile))
	{
		ParserReadTextFile(configFile, parse_ServerDatabaseConfig, pDatabaseConfig, 0);

		pDatabaseConfig->IOConfig = objGetContainerIOConfig(pDatabaseConfig->IOConfig);
	}

	//clean up config defaults
	if (!pDatabaseConfig->iSnapshotInterval) pDatabaseConfig->iSnapshotInterval = 60; //Default snapshots to an hour.
	if (!pDatabaseConfig->iIncrementalInterval && !pDatabaseConfig->bNoHogs) pDatabaseConfig->iIncrementalInterval = 5; //Default incrementals to 5min.
	if (!pDatabaseConfig->iIncrementalHoursToKeep && !pDatabaseConfig->bNoHogs) pDatabaseConfig->iIncrementalHoursToKeep = 4;

	return pDatabaseConfig;
}

// Fast Local Copy stuff
void *serverdbGetObject(GlobalType globalType, U32 uID)
{
	Container *con = objGetContainer(globalType, uID);
	if (con)
		return con->containerData;
	return NULL;
}

void *serverdbGetBackup(GlobalType type, ContainerID id)
{
	Container *con = objGetContainer(type, id);
	if (!con)
		return NULL;
	return BackupCacheGet(con);
}

#include "AutoGen/objServerDB_h_ast.c"