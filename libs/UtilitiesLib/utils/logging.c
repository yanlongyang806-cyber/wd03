/***************************************************************************



*/

#include "stringcache.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>

#include "Alerts.h"
#include "EString.h"
#include "file.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "RegistryReader.h"
#include "ScratchStack.h"
#include "sock.h"
#include "StashTable.h"
#include "sysutil.h"
#include "ThreadManager.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "XboxThreads.h"

#include "Logging_h_ast.h"
#include "loggingEnums_h_ast.h"
#include "loggingEnums_h_ast.c"
#include "TimedCallback.h"
#include "rand.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:logBackgroundWriter", BUDGET_EngineMisc););

bool gbAlertLogErrors = true;
AUTO_CMD_INT(gbAlertLogErrors, AlertLogErrors) ACMD_COMMANDLINE;

int gMaxAllowedLogSize = 0;  // default off
AUTO_CMD_INT(gMaxAllowedLogSize, MaxAllowedLogSize) ACMD_COMMANDLINE;

#define ALERT_TRUNCATE_SIZE 4096

bool gbZipAllLogs = false;

AUTO_CMD_INT(gbZipAllLogs, ZipAllLogs) ACMD_COMMANDLINE;

//if set, then whenever we close a log file "for good", either because we rotated past it or because we're shutting down,
//write a tag file into this directory. So if we close "gameserver/damageevents_1234.gz", write "gameserver/damagevents/1234.gz.complete"
char *gpDirectoryForLogCompletionFiles = NULL;
AUTO_CMD_ESTRING(gpDirectoryForLogCompletionFiles, DirectoryForCompletionFiles);

//if true, then rotate every file every hour, rather than only files that get above a certain size
static bool sbAllFilesHeavy = false;
AUTO_CMD_INT(sbAllFilesHeavy, AllFilesHeavy);

//if true, then whenever we rotate a file, we also check all other files to see if they need to be rotated... this means
//that if we have log files A and B, both in the 1:00 filename, and then at 1:01 we write something into B, it will close 1:00 
//B and open 2:00 B, but will also close 1:00 A
static bool sbTryToRotateAllFilesAtOnce = false;
AUTO_CMD_INT(sbTryToRotateAllFilesAtOnce, TryToRotateAllFilesAtOnce);

//if true, then the time used to do file rotation and so forth is the time of the log, not the current clock time.
//(This is generally only useful on the logserver itself)
static bool sbUseUseLogTimeForFileRotation = false;

//counts how many times a writing thread has had to do Sleep(1) while waiting for the background writing thread... hopefully
//a useful metric of whether we're writing logs too fast
static volatile U32 siForegroundLoggingMSStalls = 0;

U32 logGetNumStalls(void)
{
	return siForegroundLoggingMSStalls;
}

void logSetUseLogTimeForFileRotation(bool bSet)
{
	sbUseUseLogTimeForFileRotation = bSet;
}

static char log_default_fname[CRYPTIC_MAX_PATH];
static char log_default_dir[CRYPTIC_MAX_PATH];
static ManagedThread *log_background_thread_ptr;

typedef struct
{
	LogForkingCB *pCB;
	enumLogCategory eCategory;
	bool bUseRawMessage;
} LogForkingCBReq;

static LogForkingCBReq **sppLogForkingReqs = NULL;


void logAddForkingCB(LogForkingCB *pCB, enumLogCategory eCategory, bool bUseRawMessage)
{
	LogForkingCBReq *pReq = calloc(sizeof(LogForkingCBReq), 1);
	pReq->pCB = pCB;
	pReq->eCategory = eCategory;
	pReq->bUseRawMessage = bUseRawMessage;
	eaPush(&sppLogForkingReqs, pReq);
}


typedef enum LogMsgFlags
{
	// Work on one file name
	kLog_WriteMsg = 1 << 0, // Primary operation, means to write out the string starting at str
	kLog_FlushLog = 1 << 1, // Flushes a file, can be added to write or by self
	kLog_CloseLog = 1 << 2, // Closes a file, can be added to write or by self
	
	kLog_SetOptions = 1 << 3, // Set options for a file, like zip and rotation (saved in value)
	kLog_Zipped = 1 << 4, // If sent with SetOptions, zip this file
	kLog_FlushOnWrite = 1 << 5, // If sent with SetOptions, flush after every write, in performance mode
	kLog_ManualRotation = 1 << 6, // If sent with SetOptions, enable manual rotation time

	// Works on all open files
	kLog_CloseAll = 1 << 7, // Closes all logs, when closing
	kLog_RenameOnClose = 1 << 8, // If set, rename the log after closing
} LogMsgFlags;

typedef struct
{
	char	*filename;			// 4/8 bytes
	LogMsgFlags	flags;			// 4 bytes
	U32     value;				// 4 bytes
	U32		num_blocks;			// 4 bytes
#ifdef _WIN64
	char	str[4+8];	// one pointer above are actually +8 bytes
#else
	char	str[4+12];
#endif
	// don't put any members after this. 
	// this acts as a start for the contiguous memory in the msg_queue
	// and any structs put after this will get overwritten by log msgs
	// longer than the array size.
} MsgEntry;
STATIC_ASSERT(POW_OF_2(sizeof(MsgEntry)));

static int msg_queue_size=4096,max_realloc_msgs = 65536; // about 1 meg of buffer

static MsgEntry			*msg_queue;
// used when the reader is accessing queue memory. prevents
// problems in realloc.

static volatile U32		msg_queue_start,msg_queue_end,reader_is_processing;
static int				background_writer_running;
static int				s_disable_logging=0;
static int				s_high_performance=0;
static int				s_auto_rotate_logs=0;

// UID for tracking relative order of separate log entries in separate logs
static volatile U32 s_log_uid = 0;

int logGetQueueSize(void)
{
	return msg_queue_size * sizeof(MsgEntry);
}


void logDisableLogging(int disable) {
	s_disable_logging = disable;
}

void logEnableHighPerformance(void)
{
	s_high_performance = 1;
}

void logAutoRotateLogFiles(int enable)
{
	s_auto_rotate_logs = enable;
}

void logSetMsgQueueSize(int max_bytes)
{
	assertmsg(!msg_queue, "logSetMsgQueueSize() called after log message queue has been inited.");
	msg_queue_size = (1 << log2(max_bytes/4)) / (int)sizeof(MsgEntry);
	max_realloc_msgs = (1 << log2(max_bytes)) / (int)sizeof(MsgEntry);
}

void logSetMaxLogSize(int size)
{
	if(!gMaxAllowedLogSize)
		gMaxAllowedLogSize = size;
}

void logSetMaxLogSizeOverride(int size)
{
	gMaxAllowedLogSize = size;
}

static MsgEntry *peekMsg(void)
{
	MsgEntry	*msg;
	if (msg_queue_start == msg_queue_end)
		return 0;
	msg = &msg_queue[msg_queue_start];
	// 	printf("writer %i: 0x%x: ", msg_queue_start, msg);
	// 	printf(" filename (0x%x)%s zipped:%i, blocks:%i delim:%s\n", msg->filename, msg->filename, msg->zipped,msg->num_blocks, msg->delim);

	return msg;
}

static MsgEntry *getMsg(void)
{
	MsgEntry	*msg;

	// 	EnterCriticalSection(&multiple_writers);
	if (msg_queue_start == msg_queue_end)
		return 0;
	msg = &msg_queue[msg_queue_start];
	msg_queue_start = (msg_queue_start + msg->num_blocks) & (msg_queue_size-1);
	//printf("read %d (start %d   end %d) TID:%d\n",msg->num_blocks,msg_queue_start,msg_queue_end, GetCurrentThreadId());
	// 	LeaveCriticalSection(&multiple_writers);
	return msg;
}

static StashTable sCompleteFiles = NULL;

static void WriteCompletionFile(const char *pFileName)
{
	static char *spCompletionFileName = NULL;
	static int siLogDirNameLen = 0;
	FILE *pFile;

	if (!sCompleteFiles)
	{
		sCompleteFiles = stashTableCreateAddress(16);
	}

	stashAddPointer(sCompleteFiles, pFileName, NULL, false);
	printf("COMPLETE: %s\n", pFileName);

	if (!siLogDirNameLen)
	{
		siLogDirNameLen = (int)strlen(log_default_dir);
	}

	estrCopy2(&spCompletionFileName, pFileName);
	estrRemove(&spCompletionFileName, 0, siLogDirNameLen);
	estrInsertf(&spCompletionFileName, 0, "%s/", gpDirectoryForLogCompletionFiles);
	estrConcatf(&spCompletionFileName, ".complete");

	mkdirtree_const(spCompletionFileName);
	
	pFile = fopen(spCompletionFileName, "wt");
	assertmsgf(pFile, "Can't open %s for writing", spCompletionFileName);

	fprintf(pFile, "Complete");
	fclose(pFile);

}

void CheckForCompletion(const char *pFileName)
{
	if (stashFindPointer(sCompleteFiles, pFileName, NULL))
	{
		assertmsgf(0, "We already claimed that %s was complete, now opening it again\n", 
			pFileName);
	}
}


typedef struct LogFileData
{
	const char *logicalName; // Original name of file, before adding any timestamps. In string pool
	const char *fileName; // Final name, including any timestamps
	FILE *logFile; // Active file pointer
	U32 rotateMinutes; // How often to rotate the log, in minutes
	U32 nextRotateTime; // When to rotate to next log, in SS2000
	U32 bytesSinceRotate; // How many bytes written since last rotation

	U32 lastLogTime; //the most recent time we were logged to... in sbUseUseLogTimeForFileRotation
		//mode we will use this as the current time

	bool bZipped; // Should this be zipped?
	bool bFlushOnWrite; // Flush on write?
	bool bHeavy; // Log is in heavy rotation
	bool bManualRotation; // Don't determine rotation time automatically
	bool bProblemsOpening; // If true, last time we tried to open it, we were not able to.
} LogFileData;

static StashTable s_logFiles;


static int handleWriteMsg(LogFileData *fileData, char *msg)
{
	unsigned int msgLen;
	int		i;
	static bool firstmsg = true;
	bool prepend_startup = false;
	bool ignoreRestrictions = false;

	char *fname_orig_nodir=NULL;

	// Try to open the log file.
	if (!fileData->logFile)
	{
		for(i=0;i<7;i++)
		{
			unsigned backoff = 0;
			makeDirectoriesForFile(fileData->fileName);
			
			//assert that we're not writing a file that we've already claimed was complete
			if (gpDirectoryForLogCompletionFiles)
			{
				CheckForCompletion(fileData->fileName);
			}

			// Open the log file to append the new log entry.
			// fileName is already in the string pool
			if (fileData->bZipped)
			{
				fileData->logFile = fopen(fileData->fileName,"abz");
			}
			else
				fileData->logFile = fopen(fileData->fileName,"ab");
			if (!fileData->logFile)
			{
				// If we've had trouble opening this file in the past, just fail.
				if (fileData->bProblemsOpening)
					break;
				// Otherwise, wait for a while then try again: use random exponential backoff.
				Sleep(backoff);
				if (backoff == 0)
					backoff = 1;
				else if (backoff == 1)
					backoff = rand() % 4 + 1;
				else
					backoff = backoff * 2 + rand() % 3;
			}
			else
			{
				break;
			}
		}
	}

	// Handle failure to open log file.
	if (!fileData->logFile)
	{
		char *errorString = NULL;
		int err = GetLastError();
		static U32 iLastTime = 0;
		U32 iCurTime = timeSecondsSince2000_ForceRecalc();

		fileData->bProblemsOpening = true;
		estrStackCreate(&errorString);
		printf("Failed to write to log file (%s). Error might be: (%s): %s\n",fileData->fileName,getWinErrString(&errorString, err),msg);

		if (gbAlertLogErrors && iCurTime > iLastTime + 600)
		{
			iLastTime = iCurTime;

			if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED)
			{
				char shortname[MAX_PATH];
			
				getFileNameNoExt(shortname, fileData->fileName);
				backSlashes(shortname);

				RunHandleExeAndAlert("LOGGING_FAILED", shortname, "log_sharing", "Failed to write log file. Error might be: (%s): %s", getWinErrString(&errorString, err),msg);
			}
			else
			{
	
				ErrorOrAlertDeferred(false, "LOGGING_FAILED",
					"At least one log write failed. Filename: (%s). Possible windows error: (%s)",
					fileData->fileName, errorString);
			}
		}
		estrDestroy(&errorString);

		return 0;
	}
	msgLen = (unsigned int)strlen(msg);

	fwrite(msg, 1, msgLen, fileData->logFile);

	if (!s_high_performance)
	{	
		fclose(fileData->logFile);
		fileData->logFile = NULL;
	}
	else if (fileData->bFlushOnWrite)
	{
		fflush(fileData->logFile);
	}

	fileData->bytesSinceRotate += msgLen;
	if (!fileData->bManualRotation && fileData->bytesSinceRotate > HEAVY_LOG_THRESHOLD)
	{
		//forces into heavy mode, and forces a rotation immediately.
		fileData->bHeavy = true;
		fileData->nextRotateTime = 1;
	}

	return 1;
}

static int handleFlushLog(LogFileData *fileData)
{
	if (!s_high_performance || !fileData->logFile)
	{
		return 0;

	}
	fflush(fileData->logFile);
	return 1;
}

static int handleCloseLog(LogFileData *fileData)
{
	if (!s_high_performance || !fileData->logFile)
	{
		return 0;
	}

	if (gpDirectoryForLogCompletionFiles && fileData->logFile)
	{
		WriteCompletionFile(fileData->fileName);
	}

	fclose(fileData->logFile);
	fileData->logFile = NULL;
	return 1;
}

static LogFileData *logGetFileData(const char *filename, U32 iCurTimeToUse, bool bNoRecurse);

static void TryToRotateAllOtherFilesWithSameRotateTime(LogFileData *fileData, U32 iRotateTime, U32 iCurTime)
{
	FOR_EACH_IN_STASHTABLE(s_logFiles, LogFileData, pOtherData)
	{
		if (pOtherData == fileData)
		{
			continue;
		}

		if (pOtherData->nextRotateTime != iRotateTime)
		{
			continue;
		}

		if (!fileData->logFile)
		{
			continue;
		}

		logGetFileData(pOtherData->logicalName, iCurTime, true);
	}
	FOR_EACH_END;
}



static LogFileData *logGetFileData(const char *filename, U32 iCurTimeToUse, bool bNoRecurse)
{
	LogFileData *fileData;
	char fname_orig[MAX_PATH];
	char fname_final[MAX_PATH];
	int rotateMinutes;

	if (!filename || !filename[0])
	{
		return NULL;
	}



	if (fileIsAbsolutePathInternal(filename)) { // Calling fileIsAbsolutePathInternal because we want to ignore fileAllPathsAbsolute in utilities for this check
		strcpy(fname_orig, filename);
	} else {
		sprintf(fname_orig,"%s%s",log_default_dir,filename);

	}

	if (!FindExtensionFromFilename(fname_orig))
	{
		strcat(fname_orig,".log");
	}

	if (gbZipAllLogs)
	{
		if (!strEndsWith(fname_orig, ".gz"))
		{
			strcat(fname_orig, ".gz");
		}
	}

	if (!s_logFiles)
	{
		s_logFiles = stashTableCreateWithStringKeys(10,StashDefault);
	}
	if (!stashFindPointer(s_logFiles,fname_orig,&fileData))
	{
		fileData = calloc(sizeof(LogFileData),1);
		fileData->logicalName = allocAddFilename(fname_orig);
		if (strstri(fileData->logicalName,".gz"))
		{
			fileData->bZipped = true;
		}
		stashAddPointer(s_logFiles,fileData->logicalName,fileData,false);
	}

	if (!iCurTimeToUse)
	{
		if (sbUseUseLogTimeForFileRotation  && fileData->lastLogTime)
		{
			iCurTimeToUse = fileData->lastLogTime;
		}
		else 
		{
			iCurTimeToUse = timeSecondsSince2000();
		}
	}

	if (fileData->bManualRotation || !s_auto_rotate_logs)
	{
		rotateMinutes = fileData->rotateMinutes;
	}
	else if (fileData->bHeavy || sbAllFilesHeavy)
	{
		rotateMinutes = HEAVY_LOG_ROTATION;
	}
	else 
	{
		rotateMinutes = DEFAULT_LOG_ROTATION;
	}

	// Log rotation
	if (rotateMinutes && iCurTimeToUse >= fileData->nextRotateTime)
	{
		char extension[MAX_PATH];
		char timestring[MAX_PATH];

		char *dotStart = FindExtensionFromFilename(fname_orig);

		if (gpDirectoryForLogCompletionFiles && fileData->logFile)
		{
			WriteCompletionFile(fileData->fileName);
		}

		if (fileData->fileName && fileData->nextRotateTime && sbTryToRotateAllFilesAtOnce && !bNoRecurse)
		{	
			TryToRotateAllOtherFilesWithSameRotateTime(fileData, fileData->nextRotateTime, iCurTimeToUse);
		}

		assert(dotStart);

		strncpy(fname_final,fname_orig,dotStart - fname_orig);
		strcpy(extension,dotStart);

		iCurTimeToUse = timeClampSecondsSince2000ToMinutes(iCurTimeToUse, rotateMinutes);
		timeMakeFilenameDateStringFromSecondsSince2000(timestring, iCurTimeToUse);

		strcat(fname_final,"_");
		strcat(fname_final,timestring);
		strcat(fname_final,extension);
		if (fileData->bZipped && !strstr(fname_final,".gz"))
		{
			strcat(fname_final,".gz");
		}
		fileData->fileName = allocAddFilename(fname_final);

		fileData->nextRotateTime = iCurTimeToUse + rotateMinutes * 60;
		fileData->bytesSinceRotate = 0;
		fileData->bProblemsOpening = false;

		if (fileData->logFile)
		{
			
			fclose(fileData->logFile);
			fileData->logFile = NULL;
		}
	}
	else if (!fileData->fileName)
	{
		if (fileData->bZipped && !strstr(fileData->logicalName,".gz"))
		{
			sprintf(fname_final,"%s.gz",fileData->logicalName);
			fileData->fileName = allocAddFilename(fname_final);			
		}
		else
		{
			fileData->fileName = fileData->logicalName;
		}
		fileData->bProblemsOpening = false;
	}

	return fileData;
}

void logGetFilename(char *filename, char *buffer, size_t buffer_size)
{
	LogFileData *data = logGetFileData(filename, 0, false);
	if (data)
		strcpy_s(buffer, buffer_size, data->fileName);
	else
		strcpy_s(buffer, buffer_size, "");
}

static DWORD WINAPI logBackgroundWriter( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
		MsgEntry	*msg;
	static char	*str = 0;
	static int	str_max = 0;
	U32			fastFrameCount = 0;

	for(;;)
	{
		autoTimerThreadFrameBegin(__FUNCTION__);
		PERFINFO_AUTO_START("logBackgroundWriter", 1);
		reader_is_processing = 1;
	
		while(msg = peekMsg())
		{
			LogFileData *fileData = NULL;
			
			fastFrameCount = 100;
			
			if (msg->flags != kLog_CloseAll) 
			{
				PERFINFO_AUTO_START("logGetFileData", 1);
				// If all we're doing is closing, we don't need this, and it'll crash when superassert() calls logWaitForQueueToEmpty()
				if ((msg->flags & kLog_WriteMsg) && sbUseUseLogTimeForFileRotation)
				{
					fileData = logGetFileData(msg->filename, msg->value, false);
					fileData->lastLogTime = msg->value;
				}
				else
				{
					fileData = logGetFileData(msg->filename, 0, false);
				}

				PERFINFO_AUTO_STOP();
			}

			if (msg->flags & kLog_SetOptions)
			{
				PERFINFO_AUTO_START("handleSetOptions", 1);
				fileData->bZipped = !!(msg->flags & kLog_Zipped);
				fileData->bFlushOnWrite = !!(msg->flags & kLog_FlushOnWrite);
				if (fileData->rotateMinutes != msg->value || fileData->bManualRotation != !!(msg->flags & kLog_ManualRotation))
				{				
					fileData->bManualRotation = !!(msg->flags & kLog_ManualRotation);
					fileData->rotateMinutes = msg->value;
					fileData->nextRotateTime = 0;
					fileData->fileName = NULL; // This gets overwritten by the next GetFileData
				}
				fileData = logGetFileData(msg->filename, 0, false);
				PERFINFO_AUTO_STOP();
			}
			if (msg->flags & kLog_WriteMsg)
			{			
				PERFINFO_AUTO_START("handleWriteMsg", 1);
				handleWriteMsg(fileData,(char *)msg->str);
				PERFINFO_AUTO_STOP();
			}
			// Intentional fall-through, so you can do a write-flush
			if (msg->flags & kLog_FlushLog)
			{			
				PERFINFO_AUTO_START("handleFlushLog", 1);
				handleFlushLog(fileData);
				PERFINFO_AUTO_STOP();
			}
			if (msg->flags & kLog_CloseLog)
			{			
				PERFINFO_AUTO_START("handleCloseLog", 1);
				// in non-high performance mode, don't need to close
				if ((!s_high_performance || handleCloseLog(fileData)) && msg->flags & kLog_RenameOnClose)
				{
					if (fileMove(fileData->fileName, msg->str) != 0) 
					{
						printf("Failed to rename log from %s to %s!\n", fileData->fileName, msg->str);
					}
				}
				PERFINFO_AUTO_STOP();
			}

			if (msg->flags & kLog_CloseAll)
			{
				StashTableIterator stashIterator;
				StashElement element;
				PERFINFO_AUTO_START("handleCloseAll", 1);
				stashGetIterator(s_logFiles, &stashIterator);

				while (stashGetNextElement(&stashIterator, &element))
				{
					fileData = stashElementGetPointer(element);
					handleCloseLog(fileData);
				}
				
				PERFINFO_AUTO_STOP();
			}
			getMsg();
		}
		reader_is_processing = 0;
		PERFINFO_AUTO_STOP();

		if(fastFrameCount){
			fastFrameCount--;
			SleepEx(1, TRUE);
		}else{
			SleepEx(100, TRUE);
		}
		autoTimerThreadFrameEnd();
	}
	EXCEPTION_HANDLER_END
}

static CRITICAL_SECTION multiple_writers;

// Don't call this, unless you're gimme
int logToFileName(char *fname_ptr,char *msg,int compress)
{
	LogFileData *fileData;

	fileData = logGetFileData(fname_ptr, 0, false);
	return handleWriteMsg(fileData,msg);
}

void logWaitForQueueToEmpty(void)
{
#define TIMEOUT 30 // wait 30 seconds, otherwise, give up, maybe the log thread is crashed
	U32 timerStart;
	BOOL enteredCritical=FALSE;
	if (!background_writer_running)
		return;
	logCloseAllLogs();
	timerStart = timeSecondsSince2000();
	while(!(enteredCritical=TryEnterCriticalSection(&multiple_writers)) && (timeSecondsSince2000() - timerStart) < TIMEOUT)
		Sleep(1);
	if (enteredCritical) {
		while((msg_queue_end != msg_queue_start || reader_is_processing) && (timeSecondsSince2000() - timerStart) < TIMEOUT)
			Sleep(1);
		LeaveCriticalSection(&multiple_writers);
	}
#undef TIMEOUT
}

AUTO_RUN_LATE;
void logInitThread(void)
{
	msg_queue = malloc(msg_queue_size * sizeof(MsgEntry));
	log_background_thread_ptr = tmCreateThread(logBackgroundWriter, NULL);
	assert(log_background_thread_ptr);
	tmSetThreadProcessorIdx(log_background_thread_ptr, THREADINDEX_LOGWRITER);
	background_writer_running = 1;
	InitializeCriticalSection(&multiple_writers);
}

bool loggingActive(void)
{
	return !!background_writer_running;
}

static void logWriteLocal(const char *msg_str,int len, const char *filename, U32 flags, int extraValue)
{
	U32			t,free_blocks,num_blocks,contiguous_blocks,filler_blocks=0;
	MsgEntry	*msg;
	const char	*allocFileName = allocAddFilename(filename);

	if(gMaxAllowedLogSize && len > gMaxAllowedLogSize)
	{
		char tempString[ALERT_TRUNCATE_SIZE + 1];
		strncpy(tempString, msg_str, ALERT_TRUNCATE_SIZE);
		ErrorOrAlertDeferred(false, "LOGSERVER_LOG_TOO_LARGE", "Attempting to write log of size %d to file %s. (%s...)", len, filename, tempString);
	}

	PERFINFO_AUTO_START_FUNC();
	
	assert(!(flags & kLog_WriteMsg && flags & kLog_RenameOnClose));
	assert(background_writer_running);

	num_blocks = 1 + (len - 1 - sizeof(msg->str) + sizeof(MsgEntry)) / sizeof(MsgEntry);
	EnterCriticalSection(&multiple_writers);
	for(;;)
	{
		contiguous_blocks = msg_queue_size - msg_queue_end;
		if (num_blocks > contiguous_blocks)
			filler_blocks = contiguous_blocks;
		else
			filler_blocks = 0;
		free_blocks = (msg_queue_start - (msg_queue_end+1)) & (msg_queue_size-1);
		//printf("end %d  start %d = %d free (need %d) TID:%d\n",msg_queue_end,msg_queue_start,free_blocks,num_blocks+filler_blocks, GetCurrentThreadId());
		if (free_blocks >= num_blocks+filler_blocks)
		{
			t = (msg_queue_end + num_blocks + filler_blocks) & (msg_queue_size-1);
			break;
		}
		if (msg_queue_size < max_realloc_msgs || num_blocks+filler_blocks >= (U32)msg_queue_size)
		{
			// wait for queue to empty, and other thread processing to complete
			while(msg_queue_end != msg_queue_start || reader_is_processing)
			{
				InterlockedIncrement(&siForegroundLoggingMSStalls);
				Sleep(1);
			}
			msg_queue_size *= 2;
			//free(msg_queue);
			//msg_queue = malloc(sizeof(msg_queue[0]) * msg_queue_size);
			msg_queue = realloc(msg_queue,sizeof(msg_queue[0]) * msg_queue_size);

			LoggingResizeHappened(sizeof(msg_queue[0]), msg_queue_size/2, msg_queue_size, max_realloc_msgs);
		}
		
		InterlockedIncrement(&siForegroundLoggingMSStalls);
		Sleep(1);
	}
	msg = &msg_queue[msg_queue_end];
	if (filler_blocks)
	{
		memset(msg,0,sizeof(*msg));
		msg->num_blocks = filler_blocks;
		msg = &msg_queue[(msg_queue_end + filler_blocks) & (msg_queue_size-1)];
	}
	msg->filename = (char*)allocFileName;
	msg->num_blocks = num_blocks;
	msg->flags = flags;
	msg->value = extraValue;

//	#pragma warning(suppress:6386) // temporarily hide warning about size of msg->str
	ANALYSIS_ASSUME(len < sizeof(msg->str));
	memcpy(msg->str,msg_str,len);
	msg_queue_end = t;

	// 	printf("msg %i-%i: 0x%x: filename (0x%x)%s zipped:%i, blocks:%i delim:%s\n", msg_queue_end, msg_queue_end+num_blocks, msg, msg->filename, msg->filename, msg->zipped,msg->num_blocks, msg->delim);

	//printf("wrote %d/%d (start %d   end %d) TID:%d\n",filler_blocks,num_blocks,msg_queue_start,msg_queue_end,GetCurrentThreadId());
	LeaveCriticalSection(&multiple_writers);
	
	PERFINFO_AUTO_STOP();
}

int logDirectWriteWithTime(const char *fname, const char *msg_str, int flags, U32 iTime)
{
	char buf[MAX_PATH];

	// Make sure there is some default directory to output the log to.
	if (!log_default_dir[0] && !fileIsAbsolutePath(fname))
		logSetDir("");

	if (!fname)
	{
		fname = "default";
	}

	// Dump the entry to a default log file if one hasn't been specified.
	if (!strchr(fname, '.')) {
		strcpy(buf, fname);
		strcat(buf, ".log");
		fname = buf;
	}

	if (errorGetVerboseLevel() == 2)
		printf("%s",msg_str);

	logWriteLocal(msg_str,(int)strlen(msg_str)+1,fname,kLog_WriteMsg | flags,iTime);
	return 1;
}


int logSetFileOptions_Filename(char *fname, bool bManualRotation, U32 rotateMinutes, bool bZipFile, bool bFlushOnWrite)
{
	U32 flags = kLog_SetOptions;

	if (bZipFile) flags |= kLog_Zipped;
	if (bFlushOnWrite) flags |= kLog_FlushOnWrite;
	if (bManualRotation) flags |= kLog_ManualRotation;

	assertmsg(!rotateMinutes || (24*60) % rotateMinutes == 0, "rotateMinutes must be an even divider of 24 hours");

	logWriteLocal("",0,fname,flags,rotateMinutes);
	return 1;
}

int logCloseAllLogs(void)
{
	logWriteLocal("",0,"",kLog_CloseAll,0);
	return 1;
}

int logFlushFile(const char *fname)
{
	logWriteLocal("",0,fname,kLog_CloseLog,0);
	return 1;
}

int logFlushAndRenameFile(const char *fname, const char *pNewFileName)
{
	logWriteLocal(pNewFileName,(int)strlen(pNewFileName)+1,fname,kLog_CloseLog | kLog_RenameOnClose,0);
	return 1;
}

int logFlushFile_ByCategory(enumLogCategory eCategory)
{
	logWriteLocal("",0,StaticDefineIntRevLookup(enumLogCategoryEnum, eCategory),kLog_CloseLog,0);
	return 1;
}



char *logGetDir(void)
{
	if (!log_default_dir[0])
		logSetDir("");

	return log_default_dir;
}

AUTO_COMMAND ACMD_CMDLINE;
void logSetDir(const char *dir)
{
	if (!log_default_fname[0]) {
		if (!dir[0]) {
			getFileNameNoExt(log_default_fname, getExecutableName());
			strcat(log_default_fname, ".log");
		} else {
			sprintf(log_default_fname,"%s.log",dir);
		}
	}
	if (strncmp(dir,"\\\\",2)==0 || strchr(dir,':'))
	{
		strcpy(log_default_dir,dir);
		if (!strEndsWith(dir,"/") || !strEndsWith(dir,"\\"))
			strcat(log_default_dir,"/");
	}
	else
	{
		strcpy(log_default_dir, fileLogDir());
		strcatf(log_default_dir, "/%s/", dir);
	}
	if (!s_disable_logging)
		mkdirtree(log_default_dir);
}

int logDefaultVprintf(char **estrOut, char const *fmt, va_list ap)
{
	char	*date;
	int		uid_local;
	char *pTempString = NULL;
	bool bNeedToEscape = false;
	int iLen;
	
	PERFINFO_AUTO_START_FUNC();

	// Increment UID.
	uid_local = InterlockedIncrement(&s_log_uid);

	estrStackCreate(&pTempString);

	estrConcatfv(&pTempString, (char*)fmt, ap);

	//if the final character is a newline, remove it as we will add it back in later and it confuses and bewilders our
	//whether-to-escape code
	iLen = estrLength(&pTempString);

	if (iLen)
	{
		if (pTempString[iLen-1] == '\n')
		{
			estrSetSize(&pTempString, iLen - 1);
		}
	}

	if (strchr(pTempString, '\n'))
	{
		bNeedToEscape = true;
	}

	PERFINFO_AUTO_START("Make ip string", 1);
	date = timeGetLogDateString();

	estrConcatf(estrOut,"%s %6d %s[%s]", date, uid_local, GlobalTypeToShortName(GetAppGlobalType()), (char*)GetAppIDStr());

	PERFINFO_AUTO_STOP();

	estrConcatf(estrOut, "%s: ", bNeedToEscape? " ESC " : "");

	if (bNeedToEscape)
	{
		estrAppendEscaped(estrOut, pTempString);
	}
	else
	{
		estrAppend(estrOut, &pTempString);
	}

	estrConcatChar(estrOut, '\r');
	estrConcatChar(estrOut, '\n');

	estrDestroy(&pTempString);

	PERFINFO_AUTO_STOP();

	return 1;
}

static LogFormatCB s_log_vprintf_fptr = NULL;

LogFormatCB logSetFormatCallback(LogFormatCB log_vprintf_fptr)
{
	LogFormatCB orig = s_log_vprintf_fptr;
	s_log_vprintf_fptr = log_vprintf_fptr;
	return orig;
}

static LogWriteCB s_log_write_fptr = NULL;

LogWriteCB logSetWriteCallback(LogWriteCB log_send_fptr)
{
	LogWriteCB orig = s_log_write_fptr;
	s_log_write_fptr = log_send_fptr;
	return orig;
}


int log_vprintf(enumLogCategory eCategory,char const *fmt, va_list ap)
{
	int success;
	int i;
	char *msg = NULL;
	char *pRawMessage = NULL;

	if (s_disable_logging)
		return 0;
	
	PERFINFO_AUTO_START_FUNC();
	
	estrStackCreate(&msg);
	if(s_log_vprintf_fptr)
	{
		success = s_log_vprintf_fptr(&msg, eCategory, fmt, ap);
	}
	else
	{
		success = logDefaultVprintf(&msg, fmt, ap);
	}

	if (!success)
	{
		estrDestroy(&msg);
		PERFINFO_AUTO_STOP();
		return 0;
	}
	if (s_log_write_fptr)
	{
		success = s_log_write_fptr(eCategory,msg);
	}
	else
	{
		success = logDirectWrite(StaticDefineIntRevLookup(enumLogCategoryEnum, eCategory),msg);
	}

	for (i=0; i < eaSize(&sppLogForkingReqs); i++)
	{
		if (sppLogForkingReqs[i]->eCategory != LOG_LAST && sppLogForkingReqs[i]->eCategory != eCategory)
		{
			continue;
		}

		if (sppLogForkingReqs[i]->bUseRawMessage)
		{
			if (!pRawMessage)
			{
				estrStackCreate(&pRawMessage); 
				estrConcatfv(&pRawMessage, fmt, ap);
			}

			sppLogForkingReqs[i]->pCB(pRawMessage);
		}
		else
		{
			sppLogForkingReqs[i]->pCB(msg);
		}
	}


	estrDestroy(&pRawMessage);
	estrDestroy(&msg);
	PERFINFO_AUTO_STOP();
	return success;
}

#undef log_printf
int log_printf(enumLogCategory eCategory,char const *fmt, ...)
{
	int result;
	va_list ap;

	if (s_disable_logging)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	va_start(ap, fmt);
	result = log_vprintf(eCategory, fmt, ap);
	va_end(ap);

	PERFINFO_AUTO_STOP();
	return result;
}

bool StringIsBadlyFormed(const char *pString)
{
	while (*pString)
	{
		if ((*pString) < 0)
		{
			return true;
		}
		pString++;
	}

	return false;
}

LogClosure *logPair_dbg(const char *name, FORMAT_STR const char *format, ...)
{
	LogClosure *closure = ScratchAllocUninitialized(sizeof(*closure));
	closure->name = name;
	closure->value = NULL;
	estrStackCreate(&closure->value);
	estrGetVarArgs(&closure->value, format);
	return closure;
}

// Append pairs generated by logPair() to an EString.
void logAppendPairs(char **estrPairs, ...)
{
	va_list args;

	va_start(args, estrPairs);
	logvAppendPairs(estrPairs, args);
	va_end(args);
}

// Append pairs generated by logPair() to an EString, from a va_list.
void logvAppendPairs(char **estrPairs, va_list args)
{
	LogClosure *pair;

	for (pair = va_arg(args, LogClosure *); pair; pair = va_arg(args, LogClosure *))
	{
		if (estrLength(estrPairs))
			estrConcatChar(estrPairs, ' ');
		estrConcatf(estrPairs, "%s ", pair->name);
		if (!*pair->value)
			estrAppend2(estrPairs, "\"\"");
		else if (strchr(pair->value, '"'))
		{
			estrConcatChar(estrPairs, '"');
			estrAppendEscaped(estrPairs, pair->value);
			estrConcatChar(estrPairs, '"');
		}
		else if (strchr(pair->value, ' '))
			estrConcatf(estrPairs, "\"%s\"", pair->value);
		else
			estrAppend2(estrPairs, pair->value);
		estrDestroy(&pair->value);
		ScratchFree(pair);
	}
}

int servLog_vprintf(enumLogCategory eCategory, const char *action, FORMAT_STR char const *oldFmt, va_list ap)
{
	char *pFixedUpFmt = NULL;

	int result;
	if (s_disable_logging)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	if (!oldFmt)
	{
		oldFmt = "";
	}

	estrStackCreate(&pFixedUpFmt);

	estrConcatfv(&pFixedUpFmt, oldFmt, ap);

	result = log_printf(eCategory,": %s(%s)", action, pFixedUpFmt);

	estrDestroy(&pFixedUpFmt);

	PERFINFO_AUTO_STOP();

	return result;
}

#undef servLog
int servLog(enumLogCategory eCategory, const char *action, FORMAT_STR char const *fmt, ...)
{
	int result;
	va_list ap;

	if (s_disable_logging)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	va_start(ap, fmt);
	result = servLog_vprintf(eCategory, action, fmt, ap);
	va_end(ap);

	PERFINFO_AUTO_STOP();

	return result;
}

int servLogWithStruct(enumLogCategory eCategory, const char *action, void *pStruct, ParseTable *pTPI)
{
	char *pTemp = NULL;
	char *pTempEscaped = NULL;
	int result;

	estrStackCreate(&pTemp);
	estrStackCreate(&pTempEscaped);

	ParserWriteText(&pTemp, pTPI, pStruct, 0, 0, TOK_NO_LOG);
	estrAppendEscaped(&pTempEscaped, pTemp);

	result = servLog(eCategory, action, "%s", pTempEscaped);

	estrDestroy(&pTemp);
	estrDestroy(&pTempEscaped);

	return result;
}

int file_servLogWithStruct(const char *pFileName, const char *action, void *pStruct, ParseTable *pTPI)
{
	char *pTemp = NULL;
	char *pTempEscaped = NULL;
	int result;

	estrStackCreate(&pTemp);
	estrStackCreate(&pTempEscaped);

	ParserWriteText(&pTemp, pTPI, pStruct, 0, 0, TOK_NO_LOG);
	estrAppendEscaped(&pTempEscaped, pTemp);

	result = filelog_printf(pFileName,": %s(%s)", action, pTempEscaped);

	estrDestroy(&pTemp);
	estrDestroy(&pTempEscaped);

	return result;
}


int servLogWithPairs(enumLogCategory eCategory, const char *action, ...)
{
	va_list args;
	const char *name;
	char *string = NULL;
	bool first = true;
	int result;

	// Add each pair.
	estrStackCreate(&string);
	va_start(args, action);
	for (name = va_arg(args, const char *); name; name = va_arg(args, const char *))
	{
		const char *value = va_arg(args, const char *);
		devassert(value);

		// Write name.
		if (!first)
			estrConcatChar(&string, ' ');
		first = false;
		estrAppend2(&string, name);
		estrConcatChar(&string, ' ');

		// Write value.
		if (!*value)
			estrAppend2(&string, "\"\"");
		if (strchr(value, '"'))
		{
			char *escapedString = NULL;
			estrConcatChar(&string, '"');
			estrStackCreate(&escapedString);
			estrAppendEscaped(&escapedString, string);
			estrAppend2(&escapedString, value);
			estrDestroy(&escapedString);
			estrConcatChar(&string, '"');
		}
		else if (strchr(value, ' '))
		{
			estrConcatChar(&string, '"');
			estrAppend2(&string, value);
			estrConcatChar(&string, '"');
		}
		else
			estrAppend2(&string, value);
	}
	va_end(args);

	// Write log string.
	result = servLog(eCategory, action, "%s", string);
	estrDestroy(&string);

	return result;
}

// servLog() using logPair()s
int servLogPairs(enumLogCategory eCategory, const char *action, ...)
{
	va_list args;
	char *estrPairs = NULL;
	int result;

	// Create name-value pairs list.
	estrStackCreate(&estrPairs);
	va_start(args, action);
	logvAppendPairs(&estrPairs, args);
	va_end(args);

	// Send servLog().
	result = servLog(eCategory, action, "%s", estrPairs);
	estrDestroy(&estrPairs);
	return result;
}

int objLog_vprintf(enumLogCategory eCategory, GlobalType type, ContainerID id, ContainerID owner_id, const char *objName, Vec3 *pLocation, const char *objOwner, const char *action, const char *pProjSpecificObjInfoString, char const *oldFmt, va_list ap)
{
	char *pFixedUpObjInfoString = NULL;
	char *pFixedUpObjName = NULL;
	char *pFixedUpFmt = NULL;
	int result;

	if (s_disable_logging)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	if(!objName || strrchr(objName, ']')==NULL)
	{
		estrStackCreate(&pFixedUpObjName);
		estrPrintf(&pFixedUpObjName, "%s[%d", GlobalTypeToShortName(type), id);
		if(owner_id) estrConcatf(&pFixedUpObjName, "@%d", owner_id);
		if(objName || objOwner)
		{
			estrConcatStatic(&pFixedUpObjName, " ");
			if(objName) estrConcatf(&pFixedUpObjName, "%s", objName);
			if(objOwner) estrConcatf(&pFixedUpObjName, "@%s", objOwner);
		}
		estrConcatStatic(&pFixedUpObjName, "]");
	}

	if(pProjSpecificObjInfoString && pProjSpecificObjInfoString[0])
	{
		estrStackCreate(&pFixedUpObjInfoString);
		estrPrintf(&pFixedUpObjInfoString, "[%s]", pProjSpecificObjInfoString);
	}

	if(oldFmt)
	{
		estrStackCreate(&pFixedUpFmt);
		estrConcatfv(&pFixedUpFmt, oldFmt, ap);
	}

	result = log_printf(eCategory,"%s%s: %s(%s)",
		pFixedUpObjName ? pFixedUpObjName : objName,
		pFixedUpObjInfoString ? pFixedUpObjInfoString : "",
		action,
		pFixedUpFmt ? pFixedUpFmt : "");

	estrDestroy(&pFixedUpFmt);
	estrDestroy(&pFixedUpObjName);
	estrDestroy(&pFixedUpObjInfoString);

	PERFINFO_AUTO_STOP();

	return result;
}

#undef objLog
int objLog(enumLogCategory eCategory, GlobalType type, ContainerID id, ContainerID owner_id, const char *objName, Vec3 *pLocation, const char *objOwner, const char *action, const char *pProjSpecificObjInfoString, char const *fmt, ...)
{
	int result;
	va_list ap;

	if (s_disable_logging)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	va_start(ap, fmt);
	result = objLog_vprintf(eCategory, type, id, owner_id, objName, pLocation, objOwner, action, pProjSpecificObjInfoString, fmt, ap);
	va_end(ap);

	PERFINFO_AUTO_STOP();

	return result;
}

int objLogWithStruct(enumLogCategory eCategory, GlobalType type, ContainerID id, ContainerID owner_id, const char *objName, Vec3 *pLocation, const char *objOwner, const char *action, const char *pProjSpecificObjInfoString, void *pStruct, ParseTable *pTPI)
{
	char *pTemp = NULL;
	char *pTempEscaped = NULL;
	int result;

	estrStackCreate(&pTemp);
	estrStackCreate(&pTempEscaped);

	ParserWriteText(&pTemp, pTPI, pStruct, 0, 0, TOK_NO_LOG);
	estrAppendEscaped(&pTempEscaped, pTemp);

	result = objLog(eCategory, type, id, owner_id, objName, pLocation, objOwner, action, pProjSpecificObjInfoString, "%s", pTempEscaped);

	estrDestroy(&pTemp);
	estrDestroy(&pTempEscaped);

	return result;
}

// objLog() using logPair()s
int objLogPairs(enumLogCategory eCategory, GlobalType type, ContainerID id, ContainerID owner_id, const char *objName, Vec3 *pLocation, const char *objOwner, const char *action, const char *pProjSpecificObjInfoString, ...)
{
	va_list args;
	char *estrPairs = NULL;
	int result;

	// Create name-value pairs list.
	estrStackCreate(&estrPairs);
	va_start(args, pProjSpecificObjInfoString);
	logvAppendPairs(&estrPairs, args);
	va_end(args);

	// Send objLog().
	result = objLog(eCategory, type, id, owner_id, objName, pLocation, objOwner, action, pProjSpecificObjInfoString, "%s", estrPairs);
	estrDestroy(&estrPairs);
	return result;
}

objPrintCB gObjPrintCB;

#undef objPrintf
void objPrintf(GlobalType type, ContainerID id, FORMAT_STR char const *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	objvPrintf(type, id, fmt, ap);
	va_end(ap);
}
void objvPrintf(GlobalType type, ContainerID id, FORMAT_STR char const *fmt, va_list ap)
{
	char *pTemp = NULL;
	if (gObjPrintCB)
	{	
		estrStackCreate(&pTemp);

		estrConcatfv(&pTemp, fmt, ap);
		gObjPrintCB(type, id, pTemp);
		estrDestroy(&pTemp);
	}
}

void setObjPrintCB(objPrintCB cb)
{
	// Overwrite it if it's there already
	gObjPrintCB = cb;
}

objBroadcastMessageCB gObjBroadcastMessageCB;
objBroadcastMessageExCB gObjBroadcastMessageExCB;

void objBroadcastMessage(GlobalType type, ContainerID id, const char *pTitle, const char *pMessage)
{
	if (gObjBroadcastMessageCB)
	{
		gObjBroadcastMessageCB(type, id, pTitle, pMessage);
	}
}

void setObjBroadcastMessageCB(objBroadcastMessageCB cb)
{
	gObjBroadcastMessageCB = cb;
}

void objBroadcastMessageEx(GlobalType type, ContainerID id, const char *pTitle, MessageStruct *pFmt)
{
	if (gObjBroadcastMessageExCB)
	{
		gObjBroadcastMessageExCB(type, id, pTitle, pFmt);
	}
}

void setObjBroadcastMessageExCB(objBroadcastMessageExCB cb)
{
	gObjBroadcastMessageExCB = cb;
}


int filelog_vprintf(const char *fname,char const *fmt, va_list ap)
{
	int success = 1;
	char *msg = NULL;
	estrStackCreate(&msg);

	success = logDefaultVprintf(&msg, fmt, ap);

	if (success)
	{
		success = logDirectWrite(fname,msg);
	}
	estrDestroy(&msg);

	return success;
}

int filelog_vprintf_echo(bool bEcho, const char *fname,char const *fmt, va_list ap)
{
	int success = 1;
	char *msg = NULL;
	estrStackCreate(&msg);

	success = logDefaultVprintf(&msg, fmt, ap);

	if (success)
	{
		success = logDirectWrite(fname,msg);
		if (success && bEcho)
			printf("%s", msg);
	}
	estrDestroy(&msg);

	return success;
}

#undef filelog_printf
int filelog_printf(const char *fname,char const *fmt, ...)
{
	int result;
	va_list ap;

	if (s_disable_logging)
		return 0;

	va_start(ap, fmt);
	result = filelog_vprintf(fname, fmt, ap);
	va_end(ap);
	return result;
}

int filelog_printf_zipped(const char *fname,char const *fmt, ...)
{
	va_list ap;
	int success;
	char *msg = NULL;

	if (s_disable_logging)
		return 0;

	va_start(ap, fmt);
	estrStackCreate(&msg);
	success = logDefaultVprintf(&msg, fmt, ap);
	if (success)
		success = logDirectWriteWithTime(fname,msg, kLog_SetOptions | kLog_Zipped, 0);
	estrDestroy(&msg);
	va_end(ap);
	return success;
}

void DEFAULT_LATELINK_logFlush(void)
{
}


AUTO_RUN_LATE;
void LogSystemVerify(void)
{
	int i, j;

	for (i=0; i < LOG_LAST - 1; i++)
	{
		for (j = i+1; j < LOG_LAST; j++)
		{
			const char *pName1 = StaticDefineIntRevLookup(enumLogCategoryEnum, i);
			const char *pName2 = StaticDefineIntRevLookup(enumLogCategoryEnum, j);
			if (strStartsWith(pName1, pName2) || strStartsWith(pName2, pName1))
			{
				assertmsgf(0, "Log category names %s and %s can't both exist, one is a prefix of the other", pName1, pName2);
			}
		}
	}
}

void PrintfLogCB(char *pInString)
{
	printf("%s\n", pInString);
}

//returns per-server extra info log string. For GameServers, this is the name of the current map
const char *DEFAULT_LATELINK_GetExtraInfoForLogPrintf(void)
{
	return NULL;
}


U64 logGetNumBytesInMessageQueue(void)
{
	int iNumBlocks = msg_queue_end - msg_queue_start;
	if (iNumBlocks < 0)
	{
		iNumBlocks += msg_queue_size;
	}

	return (U64)iNumBlocks * sizeof(MsgEntry);
}

void logReset()
{
	EnterCriticalSection(&multiple_writers);
	while(msg_queue_end != msg_queue_start || reader_is_processing)
		Sleep(1);

	msg_queue_end = msg_queue_start = 0;
	msg_queue_size = 1;
	msg_queue = realloc(msg_queue,sizeof(msg_queue[0]) * msg_queue_size);
	LeaveCriticalSection(&multiple_writers);
}

AUTO_TRANS_HELPER_SIMPLE;
void objSetDebugName(char *pchName, U32 uiLen, GlobalType type, ContainerID id, ContainerID owner_id, const char *objName, const char *objOwner)
{
	char *estr = NULL;

	estrStackCreate(&estr);

	estrConcatf(&estr, "%s[%d", GlobalTypeToShortName(type), id);

	if(owner_id)
		estrConcatf(&estr, "@%d", owner_id);

	if(objName || objOwner)
	{
		estrConcatChar(&estr, ' ');

		if(objName)
			estrConcatf(&estr, "%s", objName);

		if(objOwner)
			estrConcatf(&estr, "@%s", objOwner);

		if(strlen(estr) > uiLen-2) // -1 for ], -1 for EOS
		{
			estrSetSize(&estr, uiLen-5); // -1 for ], -1 for EOS, -3 for ...
			estrConcatString(&estr, "...", 3);
		}
	}

	estrConcatChar(&estr, ']');

	strcpy_s(pchName, uiLen, estr);

	estrDestroy(&estr);
}

enumLogCategory FindLogCategoryByFilename(const char *pFileName)
{
	return StaticDefineIntGetInt(enumLogCategoryEnum, pFileName);
}

AUTO_COMMAND;
void GenerateLog(int n)
{
	char *str = NULL;

	estrConcatCharCount(&str, 'a', n);

	logDirectWrite("C:/testing.log", str);
}

AUTO_COMMAND;
void LogNow(ACMD_SENTENCE pLog)
{
	log_printf(LOG_TEST, "%s", pLog);
}


/*
void logDisableLogging(int disable) {
	s_disable_logging = disable;
}

void logEnableHighPerformance(void)
{
	s_high_performance = 1;
}

void logAutoRotateLogFiles(int enable)
{
	s_auto_rotate_logs = enable;

*/

char *DEFAULT_LATELINK_GetLoggingStatusString(void)
{
	static char *spRetString = NULL;

	if (s_disable_logging)
	{
		return "LOGGING DISABLED";
	}

	estrPrintf(&spRetString, "Logging locally to %s.%s%s%s", 
		logGetDir(), s_high_performance ? "(High Performance)" : "", 
		gbZipAllLogs ? "(ZipAll)" : "", s_auto_rotate_logs ? "(AutoRotate)" : "");
	return spRetString;
}

void StartSpammingLogsCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int iNumLogs = (int)((intptr_t)userData);
	int i, j;
	char *pLogString = NULL;
		
	estrStackCreate(&pLogString);


	for (i = 0; i < iNumLogs; i++)
	{
		int iCategory = randomIntRange(0, LOG_LAST - 1);
		int iLength = randomIntRange(10, 100) * randomIntRange(10, 100);

		estrPrintf(&pLogString, "This is a spam log from StartSpammingLogs for testing purposes: ");
		for (j = 0; j < iLength; j++)
		{
			estrConcatChar(&pLogString, 'a' + j % 26);
		}

		log_printf(iCategory, "%s", pLogString);
	}

	estrDestroy(&pLogString);
}


AUTO_COMMAND;
void StartSpammingLogs(int iNumPerSecond)
{
	TimedCallback_Add(StartSpammingLogsCB, (void*)((intptr_t)iNumPerSecond), 1.0f);

}

void DEFAULT_LATELINK_LoggingResizeHappened(int iBlockSize, int iOldSize, int iNewSize, int iMaxSize)
{

}


#include "Logging_h_ast.c"