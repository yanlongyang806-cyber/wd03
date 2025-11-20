/*
 * LogParser
 */
      
#if _MSC_VER < 1600
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/x64/debug/AttachToDebuggerLibX64.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")
#endif
#else
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLibX64_vs10.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLib_vs10.lib")
#endif
#endif

#include "timing.h"
#include <stdio.h>
#include <conio.h>
#include "MemoryMonitor.h"

#include "FolderCache.h"
#include "sysutil.h"
#include "winutil.h"
#include "../../libs/serverlib/pub/serverlib.h"
#include "estring.h"
#include "stringcache.h"
#include "utilitieslib.h"
#include "LogParsing_h_ast.h"
#include "LogParser.h"
#include "LogParserConstructedObjects.h"
#include "LogParser_h_ast.h"
#include "stringUtil.h"
#include "cmdParse.h"
#include "StashTable.h"
#include "ScratchStack.h"
#include "timing_profiler_interface.h"
#include "qsortG.h"
#include "error.h"
#include "guild.h"

#include "../../libs/Serverlib/Pub/GenericHttpServing.h"


#include "TimedCallback.h"

#include "fileUtil2.h"

#include "StringUtil.h"

#include "../../core/NewControllerTracker/newcontrollertracker.h"
#include "../../libs/serverlib/pub/LogComm.h"
#include "LogParserGraphs.h"
#include "ControllerLink.h"
#include "EventCountingHeatMap.h"
#include "objPath.h"
#include "expression.h"
#include "httpServing.h"
#include "NameList.h"
#include "LogParserBinning.h"
#include "MapDescription_h_Ast.h"
#include "GlobalEnums_h_Ast.h"

#include "../../libs/worldlib/pub/WorldGrid.h"
#include "LogParserLaunching.h"
#include "LogParsingFileBuckets.h"
#include "LogParserAggregator.h"
#include "LogParserAggregator_h_ast.h"

#include "LogParserFilteredLogFile.h"
#include "LogParserFrontEndFiltering.h"

#include "GenericFileServing.h"

#include "HttpLib.h"
#include "HttpClient.h"
#include "Regex.h"

#include "LogParsing.h"

#include "LogParserUniqueCounter.h"


LogParserPerfStats gPerfStats = {0};

bool gbStandAloneForceExit = false;
static bool gbCloseLogParser = false;
static __int64 gTimeToDie = 0;

LogParsingRestrictions gImplicitRestrictions = {0};
U32 iTimeOfLatestLog = 0;

int siStartingPortNum = 80;
AUTO_CMD_INT(siStartingPortNum, StartingPortNum);

//turning this on stores the most recent n parsed logs in each category so they can be servermonitored. Note that
//you should generally also turn on AlwaysParseAllActions, otherwise most logs won't be parsed, for optimization purposes.
int giNumParsedLogsPerCategoryForServerMonitor = false;
AUTO_CMD_INT(giNumParsedLogsPerCategoryForServerMonitor, NumParsedLogsPerCategoryForServerMonitor) ACMD_COMMANDLINE;



const char *BASECATEGORY_UNKNOWN;
const char *BASECATEGORY_OTHER;

U32 gStartTime = 0;
U32 gElapsedTime = 0;

bool gbCurrentlyScanningDirectories = false;
int giDirScanningPercent = 0;
char *gpDirScanningStatus = NULL;

char *gpRunTimeConfig = NULL;

int giInactivityTimeOut = 0;
AUTO_CMD_INT(giInactivityTimeOut, InactivityTimeOut);

bool gbStaticAnalysis = false;
AUTO_CMD_INT(gbStaticAnalysis, StaticAnalysis) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

bool gbStandAlone = false;
AUTO_CMD_INT(gbStandAlone, StandAlone) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

bool gbNoShardMode = false;
AUTO_CMD_INT(gbNoShardMode, NoShardMode) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

bool gbLiveLikeStandAlone = false;
AUTO_CMD_INT(gbLiveLikeStandAlone, LiveLikeStandAlone) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

bool gbResendLogs = false;
AUTO_CMD_INT(gbResendLogs, ResendLogs) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

bool gbStandAloneLogParserNeverTimeOut = false;

void NeverTimeOutReset(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	gbStandAloneLogParserNeverTimeOut = false;
}

AUTO_COMMAND;
void NeverTimeOut(bool bSet)
{
	static TimedCallback *spNeverTimeOutResetCB = NULL;

	gbStandAloneLogParserNeverTimeOut = bSet;

	if (spNeverTimeOutResetCB)
	{
		TimedCallback_Remove(spNeverTimeOutResetCB);
		spNeverTimeOutResetCB = NULL;
	}


	if (bSet)
	{
		spNeverTimeOutResetCB = TimedCallback_Run(NeverTimeOutReset, 0, 60 * 60 * 24 * 3);
	}
}

static ParsedLog **sppProceduralLogs = NULL;

GuildEmblemList g_GuildEmblems;

// Activity message management.
static char **ppActivityQueue = NULL;					// Queued activity messages.
static char **ppActivityQueuePending = NULL;			// Activities that have already been sent to the server.
static HttpClient *pWebServer = NULL;					// Web server connection, if any
static unsigned long long ullCountActivities;			// Count of activities received by the LogParser
static unsigned long long ullCountActivitySends;		// Count of activity connections made to the server
static unsigned long long ullCountActivityFails;		// Count of failed activity connection attempts
static unsigned long long ullCountActivityDiscards;		// Count of activities discarded due to link problems
#define ACTIVITY_REQUEST_PATH "/activity_stream/service"

char gLogParserConfigFile[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(gLogParserConfigFile, LogParserConfigFile);

char **gppExtraLogParserConfigFiles = NULL;
AUTO_COMMAND ACMD_COMMANDLINE;
void ExtraLogParserConfigFile(char *pName)
{
	eaPush(&gppExtraLogParserConfigFiles, strdup(pName));
}

AUTO_COMMAND ACMD_COMMANDLINE;
void RunWebFilter(U32 playerid, U32 accountid, int startTime, int endTime, const char *categories, bool bAllowDownload, bool bCompressDownload)
{
	// Set up run.
	if (!bAllowDownload)
	{
		gStandAloneOptions.bWebFilteredScan = true;
		gStandAloneOptions.bCompressFilteredLogFile = false;
	}
	else
	{
		gStandAloneOptions.bWebFilteredScan = false;

		if (bCompressDownload)
		{
			gStandAloneOptions.bCompressFilteredLogFile = true;
		}
	}

	gStandAloneOptions.bCreateFilteredLogFile = true;
	gStandAloneOptions.parsingRestrictions.uPlayerID = playerid;
	gStandAloneOptions.parsingRestrictions.uAccountID = accountid;
	gStandAloneOptions.parsingRestrictions.iStartingTime = startTime;
	gStandAloneOptions.parsingRestrictions.iEndingTime = endTime;
	estrClear(&gStandAloneOptions.pFilenameRestrictions);
	estrCopy2(&gStandAloneOptions.pFilenameRestrictions, categories);
}

AUTO_COMMAND ACMD_COMMANDLINE;
void RunWebFilterNoCategories(U32 playerid, U32 accountid, int startTime, int endTime, bool bAllowDownload, bool bCompressDownload)
{
	// Set up run.
	if (!bAllowDownload)
	{
		gStandAloneOptions.bWebFilteredScan = true;
		gStandAloneOptions.bCompressFilteredLogFile = false;
	}
	else
	{
		gStandAloneOptions.bWebFilteredScan = false;

		if (bCompressDownload)
		{
			gStandAloneOptions.bCompressFilteredLogFile = true;
		}
	}

	gStandAloneOptions.bCreateFilteredLogFile = true;
	gStandAloneOptions.parsingRestrictions.uPlayerID = playerid;
	gStandAloneOptions.parsingRestrictions.uAccountID = accountid;
	gStandAloneOptions.parsingRestrictions.iStartingTime = startTime;
	gStandAloneOptions.parsingRestrictions.iEndingTime = endTime;
	estrClear(&gStandAloneOptions.pFilenameRestrictions);
}

char gLogParserLocalDataDir[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(gLogParserLocalDataDir, DataDir);

const char *LogParserLocalDataDir()
{
	if(gLogParserLocalDataDir[0])
		return gLogParserLocalDataDir;

	return fileLocalDataDir();
}

// Activity mode command-line option.
char *gpActivityServer = NULL;
AUTO_COMMAND ACMD_COMMANDLINE;
void setActivityServer(char *pName)
{
	assert(*pName);
	estrCopy2(&gpActivityServer, pName);
}

// Activity mode command-line option, server port.
int gpActivityServerPort = 80;
AUTO_CMD_INT(gpActivityServerPort, setActivityServerPort) ACMD_CMDLINE;

// Print some activity stream statistics to the console.
AUTO_COMMAND;
void activityStats()
{
	printf("Activity stream status: %s\n", gpActivityServer ? "Enabled" : "Disabled");
	if (gpActivityServer)
		printf(
			"Activity server: %s:%u\n"
			"Number of activities received: %"FORM_LL"u\n"
			"Number of connections attempted to activity server: %"FORM_LL"u\n"
			"Number of failures in communicating with activity server: %"FORM_LL"u\n"
			"Number of activities discarded due to communication problems: %"FORM_LL"u\n"
			"Number of pending activities waiting to be sent: %d\n"
			"Number of pending activities sent for which we are waiting for a confirmation: %d\n",
			gpActivityServer, gpActivityServerPort,
			ullCountActivities, ullCountActivitySends, ullCountActivityFails, ullCountActivityDiscards,
			eaSize(&ppActivityQueue), eaSize(&ppActivityQueuePending));
}

// Save new search time and report this to the user.
static char *SetSearchTimes_Precise(U32 uStartTime, U32 uEndTime, char *pStartTime, char *pEndTime, const char *pFormatString)
{
	static char *pRetString = NULL;
	char offset[7];

	// Validate parameters.
	if (pStartTime && !uStartTime)
	{
		estrPrintf(&pRetString, "Can't process start time \"%s\".", pStartTime);
		if (pFormatString)
			estrConcatf(&pRetString, " Format should be %s", pFormatString);
		return pRetString;
	}
	if (pEndTime && !uEndTime)
	{
		estrPrintf(&pRetString, "Can't process end time \"%s\".", pEndTime);
		if (pFormatString)
			estrConcatf(&pRetString, " Format should be %s", pFormatString);
		return pRetString;
	}

	// Set new times.
	gStandAloneOptions.bTimeWasSetInEasyMode = false;
	gStandAloneOptions.parsingRestrictions.iStartingTime = uStartTime;
	gStandAloneOptions.parsingRestrictions.iEndingTime = uEndTime;

	// Save options.
	SaveStandAloneOptions(NULL);

	// Report result.
	timeLocalOffsetStringFromUTC(offset, sizeof(offset));
	estrPrintf(&pRetString, "Scanning will now occur from %s %s to ", timeGetLocalDateStringFromSecondsSince2000(gStandAloneOptions.parsingRestrictions.iStartingTime), offset); 
	estrConcatf(&pRetString, "%s %s", timeGetLocalDateStringFromSecondsSince2000(gStandAloneOptions.parsingRestrictions.iEndingTime), offset); 
	return pRetString;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *SetSearchTimes_LocalGimmeTime(char *pStartTime, char *pEndTime)
{
	return SetSearchTimes_Precise(timeGetSecondsSince2000FromLocalGimmeString(pStartTime), timeGetSecondsSince2000FromLocalGimmeString(pEndTime),
		pStartTime, pEndTime, "MMDDYYHH{:MM{:SS}}");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *SetSearchTimes_SecsSince2000(U32 uStartTime, U32 uEndTime)
{
	return SetSearchTimes_Precise(uStartTime, uEndTime, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *SetSearchTimes_UtcLogDate(char *pStartTime, char *pEndTime)
{
	return SetSearchTimes_Precise(timeGetSecondsSince2000FromLogDateString(pStartTime), timeGetSecondsSince2000FromLogDateString(pEndTime),
		pStartTime, pEndTime, "YYMMDD HH:MM:SS");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void KillStandAlone()
{
	if(!gbStandAlone)
		return;

	AbortDirectoryScanning();

	gbStandAloneForceExit = true;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void LongLogParserTimeout()
{
	if(!gbStandAlone)
		return;

	giInactivityTimeOut = 36*60*60;
}



#define HOUR (60 * 60)
#define DAY (HOUR * 24)
#define YEAR (DAY * 365)



AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void SetSearchTimes_Easy(char *pTypeName)
{
	enumSearchTimeType eType= StaticDefineIntGetInt(enumSearchTimeTypeEnum, pTypeName);

	gStandAloneOptions.bTimeWasSetInEasyMode = true;
	gStandAloneOptions.parsingRestrictions.iEndingTime = timeSecondsSince2000();


	switch (eType)
	{
	xcase SEARCHTIME_ALWAYS:
		gStandAloneOptions.parsingRestrictions.iEndingTime = gStandAloneOptions.parsingRestrictions.iStartingTime = 0;
	xcase SEARCHTIME_LASTMINUTE:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - 60;
	xcase SEARCHTIME_LAST15MINUTES:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - 15 * 60;
	xcase SEARCHTIME_LASTHOUR:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - HOUR;
	xcase SEARCHTIME_LAST6HOURS:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - 6 * HOUR;
	xcase SEARCHTIME_LAST12HOURS:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - 12 * HOUR;
	xcase SEARCHTIME_LASTDAY:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - DAY;
	xcase SEARCHTIME_LAST2DAYS:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - 2 * DAY;
	xcase SEARCHTIME_LAST3DAYS:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - 3 * DAY;
	xcase SEARCHTIME_LASTWEEK:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - 7 * DAY;
	xcase SEARCHTIME_LAST2WEEKS:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - 14 * DAY;
	xcase SEARCHTIME_LASTMONTH:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - YEAR / 12;
	xcase SEARCHTIME_LAST3MONTHS:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - YEAR / 4;
	xcase SEARCHTIME_LAST6MONTHS:	
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - YEAR / 2;
	xcase SEARCHTIME_LASTYEAR:
		gStandAloneOptions.parsingRestrictions.iStartingTime = timeSecondsSince2000() - YEAR;
	}
	
	SaveStandAloneOptions(NULL);

}

void ClearSubActionRestrictions(SubActionList *pList)
{
	eaClear(&pList->pList);
	free(pList);
}
	
U32 giLogLinesUsed = 0;
LogParserConfig gLogParserConfig = {0};

NetLink *pLogServerLink = NULL;


StashTable sLogFileGroupsByName = NULL;
StashTable sFilesProcessedTable = NULL;


LogFileGroup *GetFileGroupForServerMonitoring(char *pFileName)
{
	LogFileGroup *pFileGroup;

	PERFINFO_AUTO_START_FUNC();


	if (stashFindPointer(sLogFileGroupsByName, pFileName, &pFileGroup))
	{
		PERFINFO_AUTO_STOP();
		return pFileGroup;
	}

	pFileGroup = StructCreate(parse_LogFileGroup);
	pFileGroup->name = allocAddString(pFileName);
	stashAddPointer(sLogFileGroupsByName, pFileGroup->name, pFileGroup, false);

	PERFINFO_AUTO_STOP();

	return pFileGroup;


	return NULL;
}


//lists of registered action-specific-callbacks
typedef struct ActionSpecificCallback
{
	const char *pCallbackName;
	LogParserProcessingCallback *pCB; 
	void *pUserData;
	UseThisLogCheckingStruct *pCheckingStruct;
	ActionSpecificCallbackType eType;
} ActionSpecificCallback;

typedef struct ActionSpecificCallbackList
{
	ActionSpecificCallback **ppCallbacks;
	U32 iLogLinesUsed;
	U32 iBytesProcessed;
} ActionSpecificCallbackList;

StashTable sActionSpecificCallbackLists = NULL; //ActionSpecificCallbackList by pointer to pooled string name

void ActionSpecificCallbackListDestructor(ActionSpecificCallbackList *pList)
{
	eaDestroyEx(&pList->ppCallbacks, NULL);
	free(pList);
}



void ResetActionSpecificCallbacks(void)
{
	stashTableClearEx(sActionSpecificCallbackLists, NULL, ActionSpecificCallbackListDestructor);
	stashTableClearEx(gImplicitRestrictions.sAction, NULL, ClearSubActionRestrictions);
}

void RegisterActionSpecificCallback(const char *pActionName, const char *pSubActionName, const char *pCallbackName, 
	LogParserProcessingCallback *pCB, void *pUserData, UseThisLogCheckingStruct *pCheckingStruct, ActionSpecificCallbackType eCallbackType)
{
	ActionSpecificCallbackList *pList;
	SubActionList *pSubList;
	const char *pPooledActionName = allocAddString(pActionName);
	const char *pPooledSubActionName = allocAddString(pSubActionName);
	const char *pActionNameForRestriction;
	const char *pItemGainedEventSourceString = NULL;
	const char *pKillEventSourceString = NULL;
	const char *pKillEventTargetString = NULL;
	const char *pEventSourceString = NULL;
	const char *pEventTargetString = NULL;

	ActionSpecificCallback *pCallbackStruct = calloc(sizeof(ActionSpecificCallback), 1);

	pCallbackStruct->eType = eCallbackType;

	if(!pItemGainedEventSourceString)
		pItemGainedEventSourceString = allocAddString("ItemGainedEventSource");
	if(!pKillEventSourceString)
		pKillEventSourceString = allocAddString("KillEventSource");
	if(!pKillEventTargetString)
		pKillEventTargetString = allocAddString("KillEventTarget");
	if(!pEventSourceString)
		pEventSourceString = allocAddString("EventSource");
	if(!pEventTargetString)
		pEventTargetString = allocAddString("EventTarget");

	if (!stashFindPointer(sActionSpecificCallbackLists, pPooledActionName, &pList))
	{
		pList = calloc(sizeof(ActionSpecificCallbackList), 1);
		stashAddPointer(sActionSpecificCallbackLists, pPooledActionName, pList, false);
	}

	if(pPooledActionName == pItemGainedEventSourceString)
		pActionNameForRestriction = pEventSourceString;
	else if(pPooledActionName == pKillEventTargetString)
		pActionNameForRestriction = pEventTargetString;
	else if(pPooledActionName == pKillEventTargetString)
		pActionNameForRestriction = pEventTargetString;
	else
		pActionNameForRestriction = pPooledActionName;

	if(!stashFindPointer(gImplicitRestrictions.sAction, pActionNameForRestriction, &pSubList))
	{
		pSubList = calloc(sizeof(SubActionList), 1);
		stashAddPointer(gImplicitRestrictions.sAction, pActionNameForRestriction, pSubList, false);
		if(pSubActionName)
		{
			int i;
			char **ppSubActionNameList = NULL;
			DivideString(pSubActionName, ",", &ppSubActionNameList, 
				DIVIDESTRING_POSTPROCESS_ALLOCADD | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
			for (i=0; i < eaSize(&ppSubActionNameList); i++)
				eaPush(&pSubList->pList, ppSubActionNameList[i]);
			eaDestroy(&ppSubActionNameList);
		}
	}
	else if(eaSize(&pSubList->pList) != 0)
	{
		if(pSubActionName)
		{
			int i;
			char **ppSubActionNameList = NULL;
			DivideString(pSubActionName, ",", &ppSubActionNameList, 
				DIVIDESTRING_POSTPROCESS_ALLOCADD | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
			for (i=0; i < eaSize(&ppSubActionNameList); i++)
			{
				if(!eaFindString(&pSubList->pList, ppSubActionNameList[i]))
				{
					eaPush(&pSubList->pList, ppSubActionNameList[i]);
				}
			}
			eaDestroy(&ppSubActionNameList);
		}
		else
		{
			eaClear(&pSubList->pList);
		}
	}

	pCallbackStruct->pCallbackName = pCallbackName;
	pCallbackStruct->pCB = pCB;
	pCallbackStruct->pUserData = pUserData;
	pCallbackStruct->pCheckingStruct = pCheckingStruct;

	eaPush(&pList->ppCallbacks, pCallbackStruct);
}

typedef struct LogParserTimedCallback
{
	const char *pCallbackName;
	LogParserTimedCallbackFunction *pCB;
	void *pUserData;
	U32 iNextTimeToCall;
} LogParserTimedCallback;

LogParserTimedCallback **sLogParserTimedCallbackList = NULL;

void ResetLogParserTimedCallbacks(void)
{
	eaDestroyEx(&sLogParserTimedCallbackList, NULL);
}

void RegisterLogParserTimedCallback(const char *pCallbackName, LogParserTimedCallbackFunction *pCB, void *pUserData, U32 iNextTimeToCall)
{
	LogParserTimedCallback *pCallbackStruct = calloc(sizeof(LogParserTimedCallback), 1);

	pCallbackStruct->iNextTimeToCall = iNextTimeToCall;
	pCallbackStruct->pCallbackName = pCallbackName;
	pCallbackStruct->pCB = pCB;
	pCallbackStruct->pUserData = pUserData;

	eaPush(&sLogParserTimedCallbackList, pCallbackStruct);
}

void UpdateLogParserTimedCalbacks()
{
	static U32 iLastTimeCalled = 0;
	U32 iTime = timeSecondsSince2000();

	if(iTime > iLastTimeCalled)
	{
		EARRAY_FOREACH_BEGIN(sLogParserTimedCallbackList, i);
		{
			if(sLogParserTimedCallbackList[i]->iNextTimeToCall <= iTime)
			{
				sLogParserTimedCallbackList[i]->iNextTimeToCall = sLogParserTimedCallbackList[i]->pCB(sLogParserTimedCallbackList[i]->pUserData);
			}
		}
		EARRAY_FOREACH_END;
		iLastTimeCalled = iTime;
	}
}

typedef struct CallbackReportingData
{
	U32 iLogLinesUsed;
	U32 iBytesProcessed;
} CallbackReportingData;

static void InitSet(int **set, int count)
{
	int i;
	*set = (int*)calloc(count, sizeof(int));
	
	(*set)[0] = 0;
	for(i = 1; i < count; ++i)
		(*set)[i] = (*set)[i-1] + 1;
}

static bool IncrementSet(int *set, int count, int base)
{
	int i = count - 1;
	while(i >= 0)
	{
		if(++set[i] < base - (count - 1 - i)) // Making sure we don't duplicate sets
			break;

		--i;
	}

	if(i < 0)
		return false;

	++i;

	while(i < count)
	{
		set[i] = set[i-1] + 1;
		++i;
	}

	return true;
}

static const char *GetCallbackReportName(ActionSpecificCallbackList *pCallbackList, int *set, int count)
{
	int i;
	char *pName = NULL;
	for(i = 0; i < count; ++i)
	{
		estrConcatf(&pName, "%s", pCallbackList->ppCallbacks[set[i]]->pCallbackName);
	}
	return pName;
}

static int ReportCallbackData(void *userData, StashElement element)
{
	const char *pActionName = stashElementGetStringKey(element);
	ActionSpecificCallbackList *pCallbackList = stashElementGetPointer(element);
	StashTable sReportingData = userData;
	int n;

	for(n = 1; n <= min(eaSize(&pCallbackList->ppCallbacks), 5) ; ++n)
	{
		int *set = NULL;
		InitSet(&set, n);

		do 
		{
			const char *pName = GetCallbackReportName(pCallbackList, set, n);
			CallbackReportingData *pData = NULL;

			if(!stashFindPointer(sReportingData, pName, &pData))
			{
				pData = (CallbackReportingData*)calloc(1, sizeof(CallbackReportingData));
				stashAddPointer(sReportingData, pName, pData, false);
			}

			pData->iBytesProcessed += pCallbackList->iBytesProcessed;
			pData->iLogLinesUsed += pCallbackList->iLogLinesUsed;
			
		} while (IncrementSet(set, n, eaSize(&pCallbackList->ppCallbacks)));

		free(set);
	}

	return 1;
}

static int PrintSingleCallbackReport(StashElement element)
{
	const char *pName = stashElementGetStringKey(element);
	CallbackReportingData *pData = stashElementGetPointer(element);
	printf("%s: %lu lines/%lu bytes.\n", pName, pData->iLogLinesUsed, pData->iBytesProcessed);
	return 1;
}

static void PrintCallbackReport(StashTable sReportingData)
{
	printf("Callback Report.\n");
	printf("Total log lines in files: %lu.\n", giLogLinesUsed);
	stashForEachElement(sReportingData, &PrintSingleCallbackReport);
	printf("Callback Report.\n");
}

static int PrintSingleActionData(StashElement element)
{
	const char *pActionName = stashElementGetStringKey(element);
	ActionSpecificCallbackList *pList = stashElementGetPointer(element);
	char *pCallbackNames = NULL;
	int i;
	for(i = 0; i < eaSize(&pList->ppCallbacks); ++i)
	{
		estrConcatf(&pCallbackNames, "%s", pList->ppCallbacks[i]->pCallbackName);
	}
	printf("%s : %lu lines, %lu bytes, %lu bytes/line, %d callbacks : %s\n", pActionName, pList->iLogLinesUsed, pList->iBytesProcessed, pList->iLogLinesUsed ? pList->iBytesProcessed / pList->iLogLinesUsed : 0, eaSize(&pList->ppCallbacks), pCallbackNames);
	return 1;
}

static void PrintActionCallbackData()
{
	stashForEachElement(sActionSpecificCallbackLists, &PrintSingleActionData);
}

static void CalculateLoadByCallbackName()
{
	StashTable sReportingData = NULL;
	sReportingData = stashTableCreate(10, StashDeepCopyKeys, StashKeyTypeStrings, 0);

	stashForEachElementEx(sActionSpecificCallbackLists, &ReportCallbackData, sReportingData);
	PrintActionCallbackData();
	PrintCallbackReport(sReportingData);
	stashTableClear(sReportingData);
}

static U64 iTotalLength;
static U64 iHeaderLength;
static U64 iTotalCount;

static int WriteLogCountData(StashElement element)
{
	const char *pName = stashElementGetStringKey(element);
	LogCountingData *pData = (LogCountingData*)stashElementGetPointer(element);
	iTotalLength += pData->data;
	iHeaderLength += pData->header;
	iTotalCount += pData->count;
	printf("%s, %llu, %llu, %llu\n", pName, pData->data, pData->header, pData->count);
	return 1;
}

static void WriteLogCountingData()
{
	printf("\nLog counts\n");
	iTotalLength = 0;
	iHeaderLength = 0;
	printf("Data by Action Name.\n");
	stashForEachElement(sActionLogCount, &WriteLogCountData);
	printf("Total, %llu, %llu, %llu\n\n", iTotalLength, iHeaderLength, iTotalCount);
	iTotalLength = 0;
	iHeaderLength = 0;
	iTotalCount = 0;
	printf("Data by Event Type.\n");
	stashForEachElement(sEventTypeLogCount, &WriteLogCountData);
	printf("Total, %llu, %llu, %llu\n\n", iTotalLength, iHeaderLength, iTotalCount);
}

void RegisterFileNameToMatch(const char *pFileName)
{
	StashElement elem;
	const char* pAllocFileName = allocAddString(pFileName);

	if(!gImplicitRestrictions.sFileGroups)
		gImplicitRestrictions.sFileGroups = stashTableCreateWithStringKeys(8, StashDefault);

	if(stashAddPointerAndGetElement(gImplicitRestrictions.sFileGroups, pAllocFileName, NULL, false, &elem))
	{
		LogFileGroup *pFileGroup = malloc(sizeof(*pFileGroup));
		pFileGroup->name = pAllocFileName;
		pFileGroup->ppParsedLogs = NULL;
		stashElementSetPointer(elem, pFileGroup);
	}
}


bool ProcessPerfIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	return ProcessStructIntoStructInfoForHttp("", pArgList,
		&gPerfStats, parse_LogParserPerfStats, iAccessLevel, 0, pStructInfo, eFlags);
}

#define LOGPARSERPERFCB_INTERVAL 10

void LogParserPerfCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	static U64 iLogsProcessedLastTime = 0;
	static U64 iLogBytesProcessedLastTime = 0;
	static U64 iGraphCBsLastTime = 0;
	static U64 iLPCOCBsLastTime = 0;

	gPerfStats.iLogsPerSecond = (gPerfStats.iNumLogsProcessed - iLogsProcessedLastTime) / LOGPARSERPERFCB_INTERVAL;
	gPerfStats.iBytesPerSecond = (gPerfStats.iLogBytesProcessed - iLogBytesProcessedLastTime) / LOGPARSERPERFCB_INTERVAL;
	gPerfStats.iGraphCallbacksPerSecond = (gPerfStats.iActionCallbacks[ASC_GRAPH] - iGraphCBsLastTime) / LOGPARSERPERFCB_INTERVAL;
	gPerfStats.iLPCOCallbacksPerSecond = (gPerfStats.iTotalLPCOCallbacks - iLPCOCBsLastTime) / LOGPARSERPERFCB_INTERVAL;

	iLogsProcessedLastTime = gPerfStats.iNumLogsProcessed;
	iLogBytesProcessedLastTime = gPerfStats.iLogBytesProcessed;
	iGraphCBsLastTime = gPerfStats.iActionCallbacks[ASC_GRAPH];
	iLPCOCBsLastTime = gPerfStats.iTotalLPCOCallbacks;
}

AUTO_RUN;
void InitLogParserLib(void)
{
	sFilesProcessedTable = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);
	sLogFileGroupsByName = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);
	sActionSpecificCallbackLists = stashTableCreateAddress(16);

	BASECATEGORY_UNKNOWN = allocAddString("_UNKNOWN");
	BASECATEGORY_OTHER = allocAddString("other");

	RegisterCustomXPathDomain(".perf", ProcessPerfIntoStructInfoForHttp, NULL);
	TimedCallback_Add(LogParserPerfCB, NULL, LOGPARSERPERFCB_INTERVAL);
}

static void consoleCloseHandler(DWORD fdwCtrlType)
{
	printf("Shutting down...\n");
	gbCloseLogParser = true;
}

void OVERRIDE_LATELINK_StatusReporting_ShutdownSafely(void)
{
	printf("Shutdown requested via status monitoring down...\n");
	gbCloseLogParser = true;
}


static int lpHandleControllerMsg(Packet *pak,int cmd, NetLink *link,void *userdata)
{
	switch(cmd)
	{

		// If the controller has asked us to die, close immediately.
		case FROM_CONTROLLER_KILLYOURSELF:
			printf("Controller is telling us to die...\n");
			gbCloseLogParser = true;
			break;

		// If the controller is dying, wait for 30 seconds before dying.
		// If there is a Log Server running, it should kill us sooner.
		case FROM_CONTROLLER_IAMDYING:
			printf("Controller died... Cleaning up\n");
			gTimeToDie = timeMsecsSince2000() + 30000;

			// Save graph data in case we're killed before we're ready.
			Graph_DumpStatusFile();
			LogParserUniqueCounter_WriteOut();
			break;
	}
	return 1;
}

void AddLogToHeatMapsAndGraphs(ParsedLog *pLog, char *pFileName)
{
	if (pLog->pObjInfo)
	{
		//this call adds the log to all graphs, and also creates the lists of which heat maps to add
		//a log to
		DoAllActionSpecificCallbacks(pLog, pLog->pObjInfo->pAction);
	}
	else if (pFileName)
	{
		static char lastName[CRYPTIC_MAX_PATH] = "";
		char temp1[CRYPTIC_MAX_PATH] = "";
		char temp2[CRYPTIC_MAX_PATH] = "";
		static const char *pPooled = NULL;

		if (stricmp_safe(pFileName, lastName) != 0)
		{
			char *pFirstPeriod;
			strcpy(lastName, pFileName);
			getFileNameNoExtNoDirs(temp1, pFileName);
			
			pFirstPeriod = strchr(temp1, '.');
			if (pFirstPeriod)
			{
				*pFirstPeriod = 0;
			}

			sprintf(temp2, "__%s", temp1);
			pPooled = allocAddString(temp2);
		}

		DoAllActionSpecificCallbacks(pLog, pPooled);
	}
		
	
}

// Open a connection to the activity web server.
static void ConnectToActivityServer(void)
{
	// Open connection to web server.
	assert(gpActivityServer && gpActivityServerPort > 0 && gpActivityServerPort <= 65535);
	assert(!pWebServer);
	pWebServer = httpClientConnect(gpActivityServer, gpActivityServerPort, NULL, NULL, NULL, NULL, commDefault(), false, 0);
	++ullCountActivitySends;
}

// A connection has been opened successfully.  Send the activities.
static void SendActivitiesToServer(void) {

	char *body = NULL;
	char *request = NULL;
	bool first = true;

	// Format JSON body.
	estrCreate(&body);
	estrAppend2(&body, "[");
	EARRAY_FOREACH_BEGIN(ppActivityQueue, i);
	{
		if (!first)
			estrAppend2(&body, ", ");
		first = false;
		estrAppend2(&body, ppActivityQueue[i]);
	}
	EARRAY_FOREACH_END;
	estrAppend2(&body, "]\r\n");

	// Send request to web server.
	estrPrintf(&request,
		"POST " ACTIVITY_REQUEST_PATH " HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Content-Length: %d\r\n"
		"Content-Type: application/json\r\n"
		"User-Agent: Cryptic Activity Streams %s\r\n\r\n%s",
		gpActivityServer, gpActivityServerPort, estrLength(&body), GetUsefulVersionString(), body);
	httpClientSendBytesRaw(pWebServer, request, estrLength(&request));

	// Save sent activities in case the request fails so that they can be resent.
	ppActivityQueuePending = ppActivityQueue;
	ppActivityQueue = NULL;
	estrDestroy(&body);
	estrDestroy(&request);
}

// The connection to the activities server has died.  Put the sent requests back into the outgoing queue.
static void RestoreLostActivities(void)
{
	// Transfer ownership of pending messages from ppActivityQueuePending to ppActivityQueue.
	eaPushEArray(&ppActivityQueue, &ppActivityQueuePending);
	eaDestroy(&ppActivityQueuePending);
}

// Connect to server, send activity stream data to server, and close the server connection, as appropriate.
static void CheckActivityServerStatus(void)
{
	DWORD now;
	static DWORD LastActivitySendTick = 0;
	static enum {
		ACTIVITY_DISCONNECTED,								// No connection to server
		ACTIVITY_CONNECTING,								// Waiting for connection with server to complete
		ACTIVITY_WAITING									// Waiting for response to HTTP request
	} status = ACTIVITY_DISCONNECTED;
	const unsigned uMaxActivities = 3000;					// Maximum number of activities to queue.
	const unsigned uActivityPeriod = 1000;					// Duration of buffering between each web server update (ms)
	const unsigned uActivityTimeout = 15000;				// Maximum duration to wait for a response from the server (ms)
	NetLink *link;

	PERFINFO_AUTO_START_FUNC();

	// If no server is specified, activity updates are not active.  Do nothing.
	if (!gpActivityServer)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	// State machine over the above states.
	now = GetTickCount();
	switch (status)
	{
		xcase ACTIVITY_DISCONNECTED:

			// Open a connection if there are waiting activities and its been uActivityPeriod since the last connection.
			if (eaSize(&ppActivityQueue) && now >= LastActivitySendTick + uActivityPeriod)
			{
				ConnectToActivityServer();
				LastActivitySendTick = now;
				status = ACTIVITY_CONNECTING;
			}

		xcase ACTIVITY_CONNECTING:
			link = httpClientGetLink(pWebServer);

			// Connection failure
			if (linkDisconnected(link))
			{
				ErrorOrAlert("LOGPARSER_ACTIVITY_CONNECT", "Unable to connect to activity stream web server");
				++ullCountActivityFails;
				httpClientDestroy(&pWebServer);
				LastActivitySendTick = now;
				status = ACTIVITY_DISCONNECTED;
			}

			// Successful connection: send activities
			else if (linkConnected(link))
			{
				SendActivitiesToServer();
				status = ACTIVITY_WAITING;
			}

			// Timed out waiting for connection to succeed
			else if (now >= LastActivitySendTick + uActivityTimeout)
			{
				ErrorOrAlert("LOGPARSER_ACTIVITY_CTIMEOUT", "Timed out waiting to connect to server");
				++ullCountActivityFails;
				httpClientDestroy(&pWebServer);
				LastActivitySendTick = now;
				status = ACTIVITY_DISCONNECTED;
			}

		xcase ACTIVITY_WAITING:
			link = httpClientGetLink(pWebServer);

			// Response received from server; check for success.
			if (httpClientResponded(pWebServer))
			{
				int response = httpClientGetResponseCode(pWebServer);
				if (response != 200)
				{
					ErrorOrAlert("LOGPARSER_ACTIVITY_BADRESPONSE", "Error response from activity stream server.");
					++ullCountActivityFails;
					RestoreLostActivities();
				}
				eaDestroyEString(&ppActivityQueuePending);
				httpClientDestroy(&pWebServer);
				LastActivitySendTick = now;
				status = ACTIVITY_DISCONNECTED;
			}

			// Server disconnected without sending a response
			else if (linkDisconnected(link))
			{
				ErrorOrAlert("LOGPARSER_ACTIVITY_DISCONNECTED", "Disconnected while waiting for response from activity server");
				++ullCountActivityFails;
				RestoreLostActivities();
				httpClientDestroy(&pWebServer);
				LastActivitySendTick = now;
				status = ACTIVITY_DISCONNECTED;
			}

			// Timed out waiting for response
			else if (now >= LastActivitySendTick + uActivityTimeout)
			{
				ErrorOrAlert("LOGPARSER_ACTIVITY_RTIMEOUT", "Timed out waiting for response from server");
				++ullCountActivityFails;
				RestoreLostActivities();
				httpClientDestroy(&pWebServer);
				LastActivitySendTick = now;
				status = ACTIVITY_DISCONNECTED;
			}
	}
	PERFINFO_AUTO_STOP();
}

// Queue a single activity to be sent to the web server.
static void QueueActivity(const char *pMessage)
{
	// Queue the activity.
	const unsigned uMaximumActivities = 1500000;
	if ((unsigned)eaSize(&ppActivityQueue) > uMaximumActivities)
	{
		ErrorOrAlert("LOGPARSER_ACTIVITY_TOOSLOW", "Too many buffered activities; activity web server is not keeping up!  All pending activities have been discarded.");
		ullCountActivityDiscards += eaSize(&ppActivityQueue);
		eaDestroyEString(&ppActivityQueue);
	}
	else
	{
		char *msg = 0;
		estrCopy2(&msg, pMessage);
		eaPush(&ppActivityQueue, msg);
	}

	// See if it's time to send the activities to the server.
	CheckActivityServerStatus();
}

// Return true if this an activity stream message.
static bool IsActivityLog(const char *pFileName)
{
	return gpActivityServer && !strcmp(pFileName, "GameServer/ACTIVITY");
}

// Parse activity log message.
static void HandleActivityLog(const char *pMessage)
{

	// Find JSON string in log message.
	const char *ptr;
	ptr = strchr(pMessage, '{');
	if (!ptr)
	{
		ErrorOrAlert("LOGPARSER_ACTIVITY_INVALID", "Invalid LOG_ACTIVITY format.");
		return;
	}

	// Send message to web server.
	++ullCountActivities;
	QueueActivity(ptr);
}

void ProcessSingleLog(char *pFileName, ParsedLog *pLog, bool bNeedToCopy)
{
	PERFINFO_AUTO_START_FUNC();

	if (bNeedToCopy)
	{
		ParsedLog *pOldLog = pLog;
		PERFINFO_AUTO_START("bNeedToCopy", 1);
		pLog = StructCreate(parse_ParsedLog);
		StructCopyAll(parse_ParsedLog, pOldLog, pLog);
		PERFINFO_AUTO_STOP();
	}
		

	if (pLog->pFileGroup && eaSize(&pLog->pFileGroup->ppParsedLogs) > (giNumParsedLogsPerCategoryForServerMonitor ? giNumParsedLogsPerCategoryForServerMonitor : 30))
	{
		ParsedLog* pDestroy = pLog->pFileGroup->ppParsedLogs[0];
		PreParsedLogRemoveRef(pDestroy);
		StructDestroy(parse_ParsedLog, pLog->pFileGroup->ppParsedLogs[0]);
		eaRemove(&pLog->pFileGroup->ppParsedLogs, 0);
	}

	if (!gbDoFrontEndFiltering)
	{
		AddLogToHeatMapsAndGraphs(pLog, pFileName);
	}

	if (pLog->iTime > iTimeOfLatestLog)
	{
		iTimeOfLatestLog = pLog->iTime;
	}

	//special filtering is used for standalone logparsers run via the pretty web frontend,
	//it involves writing to a sequence of files in a specified directory with particular timing specifications
	if (gbDoFrontEndFiltering)
	{
		FrontEndFiltering_AddLog(pLog);
	}
	else
	{
		LPCOSystem_PostLogProcessUpdate(pLog->iTime);

		if (gStandAloneOptions.bCreateFilteredLogFile && !(pLog->pObjInfo && pLog->pObjInfo->bForceKept) && (!pLog->bProcedural || gStandAloneOptions.bFilteredFileIncludesProceduralLogs))
		{
			AddLogToFilteredLogFile(pLog);
		}

		if (gStandAloneOptions.bCreateBinnedLogFiles)
		{
			LogParserBinning_AddLog(pLog);
		}
	}

	if (pLog->pFileGroup)
	{
		eaPush(&pLog->pFileGroup->ppParsedLogs, pLog);
	}
	else
	{
		PreParsedLogRemoveRef(pLog);
		StructDestroy(parse_ParsedLog, pLog);
	}

	PERFINFO_AUTO_STOP();
}

static LogFileGroup *IsNeededFileName(char *pFileName)
{
	LogFileGroup *pFileGroup;
	char shortName[MAX_PATH];
	char *dot;

	PERFINFO_AUTO_START_FUNC();

	getFileNameNoExtNoDirs(shortName, pFileName);

	dot = strchr(shortName, '.');
	if(dot)
		*dot = '\0';

	stashFindPointer(gImplicitRestrictions.sFileGroups, shortName, &pFileGroup);
	
	PERFINFO_AUTO_STOP();

	return pFileGroup;
}

static void HandleSingleLog(char *pFileName, char *pMessage)
{
	ParsedLog *pLog = NULL;
	LogFileGroup *pFileGroup;
	// Process activity logs specially.
	// Note: This mechanism is obsolete, and not in use at the moment.  However, activity logs are still being used
	// to capture notification information.
	if (IsActivityLog(pFileName))
		HandleActivityLog(pMessage);

	if (giNumParsedLogsPerCategoryForServerMonitor)
	{
		pFileGroup = GetFileGroupForServerMonitoring(pFileName);
	}
	else
	{
		if(!(pFileGroup = IsNeededFileName(pFileName)))
			return;
	}

	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_START_LOGPARSE(pFileName);

	pLog = StructCreate(parse_ParsedLog);
	pLog->pFileGroup = pFileGroup;

	if (ReadLineIntoParsedLog(pLog, pMessage, (int)strlen(pMessage), &gImplicitRestrictions, LogParserPostProcessCB, 0, NULL))
	{
		if (LogParserPostProcessCB(pLog))
		{
			ProcessSingleLog(pFileName, pLog, false);
		}
		else
		{
			PreParsedLogRemoveRef(pLog);
			StructDestroy(parse_ParsedLog, pLog);
		}
	}
	else
	{
		PreParsedLogRemoveRef(pLog);
		StructDestroy(parse_ParsedLog, pLog);
	}
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

void HandleHereAreLogs(Packet *pPak)
{
	do
	{
		char *pFileName = pktGetStringTemp(pPak);
		char *pMessage;
		int iLen;

		if (!pFileName[0])
		{
			return;
		}

		pMessage = pktGetStringTempAndGetLen(pPak, &iLen);

		gPerfStats.iNumLogsProcessed++;
		gPerfStats.iLogBytesProcessed += iLen;

		HandleSingleLog(pFileName, pMessage);
	}
	while (1);
}

void FakeSendPacket(char *pFileName, char *pLogString)
{
	HandleSingleLog(pFileName, pLogString);
}

void HandleAboutToDie(Packet *pPak)
{
	gbCloseLogParser = true;
}



void LogParserLinkCallback(Packet *pak, int cmd, NetLink *link, void *userdata)
{
	switch (cmd)
	{
	xcase LOGSERVER_TO_LOGPARSER_HERE_ARE_LOGS:
		HandleHereAreLogs(pak);

	xcase LOGSERVER_TO_LOGPARSER_ABOUT_TO_DIE:
		HandleAboutToDie(pak);
	}
}

AUTO_FIXUPFUNC;
TextParserResult GraphDefinition_ParserFixup(GraphDefinition *pDefinition, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{

	case FIXUPTYPE_POST_TEXT_READ:
		if (!pDefinition->iGraphLifespan)
		{
			if (pDefinition->iGraphLifespan_Minutes)
			{
				pDefinition->iGraphLifespan = pDefinition->iGraphLifespan_Minutes * 60;
				pDefinition->iGraphLifespan_Minutes = 0;
			}
			else if (pDefinition->iGraphLifespan_Hours)
			{
				pDefinition->iGraphLifespan = pDefinition->iGraphLifespan_Hours * 60 * 60;
				pDefinition->iGraphLifespan_Hours = 0;
			}
			else if (pDefinition->iGraphLifespan_Days)
			{
				pDefinition->iGraphLifespan = pDefinition->iGraphLifespan_Days * 60 * 60 * 24;
				pDefinition->iGraphLifespan_Days = 0;
			}
		}	

		if (pDefinition->bUseShardNamesForCategoriesAtClusterLevel && 
			(pDefinition->eDataType == GRAPHDATATYPE_MIN_MAX_AVG
				|| pDefinition->eDataType == GRAPHDATATYPE_MEDIANS))
		{
			//definitions get loaded from LongTermLogs, but those aren't the "real" definitions, so don't alert
			if (!strstri(pDefinition->pFileName, "LongTermLogs"))
			{
				CRITICAL_PROGRAMMER_ALERT("BAD_CLUSTER_GRAPH_DEFINITION", "Graph %s has Useshardnamesforcategoriesatclusterlevel flag and is MinMaxAvg or Medians. This is not legal, disabling the flag",
					pDefinition->pGraphName);
			}
			pDefinition->bUseShardNamesForCategoriesAtClusterLevel = false;
		}
		break;


	}
	return PARSERESULT_SUCCESS;
}

#define LOGFILE_SINGLE_CHUNK_CUTOFF 5000000

void LoadLogsFromBuffer(char *pBuffer, char *pFileName)
{
	ParsedLog *pLog;

	char *pBeginningOfLine = pBuffer;
	char *pEndOfLine;

	pLog = StructCreate(parse_ParsedLog);

	do
	{
		pEndOfLine = strchr(pBeginningOfLine, '\n');
		if (pEndOfLine)
		{
			*pEndOfLine = 0;
		}

		if (ReadLineIntoParsedLog(pLog, pBeginningOfLine, (int)strlen(pBeginningOfLine), &gStandAloneOptions.parsingRestrictions, LogParserPostProcessCB, 0, NULL))
		{
			ProcessSingleLog(pFileName, pLog, false);

		
			pLog = StructCreate(parse_ParsedLog);
		}
		else
		{
			PreParsedLogRemoveRef(pLog);
			StructReset(parse_ParsedLog, pLog);
		}

		pBeginningOfLine = pEndOfLine + 1;
	}
	while (pEndOfLine);

	PreParsedLogRemoveRef(pLog);
	StructDestroy(parse_ParsedLog, pLog);
}

//this function can't do any fseeks or filesize stuff because it needs to work on zip files
void LoadLogsFromFileInChunks(FILE *pInFile, char *pFileName)
{
	char *pWorkBuffer = malloc(LOGFILE_SINGLE_CHUNK_CUTOFF);
	size_t iCurReadHead = 0;
	size_t iTruncatedSize = 0;

	size_t iBytesToTryToRead;
	size_t iBytesActuallyRead;

	do
	{
		iBytesToTryToRead = LOGFILE_SINGLE_CHUNK_CUTOFF - iTruncatedSize;
		iBytesActuallyRead;

		if (iTruncatedSize)
		{
			memmove(pWorkBuffer, pWorkBuffer + LOGFILE_SINGLE_CHUNK_CUTOFF - iTruncatedSize, iTruncatedSize);
		}

		iBytesActuallyRead = fread(pWorkBuffer + iTruncatedSize, 1, iBytesToTryToRead, pInFile);

		if (iBytesActuallyRead == 0)
		{
			break;
		}

		if (iBytesActuallyRead == iBytesToTryToRead)
		{
			char *pTemp = pWorkBuffer + LOGFILE_SINGLE_CHUNK_CUTOFF - 1;
			while (*pTemp != '\n')
			{
				pTemp--;
			}

			*pTemp = 0;
			iTruncatedSize = pWorkBuffer + LOGFILE_SINGLE_CHUNK_CUTOFF - 1 - pTemp;
		}
		else
		{
			iTruncatedSize = 0;
		}

		LoadLogsFromBuffer(pWorkBuffer, pFileName);
	} while (iBytesActuallyRead == iBytesToTryToRead);

	free(pWorkBuffer);
}





			

	




void  LoadLogsFromFile(char *pFileName)
{
	LogFileParser *pParser = LogFileParser_Create(&gStandAloneOptions.parsingRestrictions, LogParserPostProcessCB,
		GetCurrentParsingFlags());
	ParsedLog *pLog;
	char *pLogString;
	int iCounter = 0;

	if (!pParser)
	{
		return;
	}

	if (!LogFileParser_OpenFile(pParser, pFileName))
	{
		LogFileParser_Destroy(pParser);
		return;
	}

	loadstart_printf("Now reading logs from %s... ", pFileName);

	while(pLogString = LogFileParser_GetNextLogString(pParser))
	{
		pLog = CreateLogFromString(pLogString, pParser);
		if(pLog)
		{
			ProcessSingleLog(LogFileParser_GetFileName(pParser), pLog, false);
		}
		iCounter++;
	}

	loadend_printf("Read %d logs\n", iCounter);


	LogFileParser_Destroy(pParser);

}






void logParserLibPeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	LogParserLaunching_PeriodicUpdate();

	if (iTimeOfLatestLog && !gbStandAlone)
	{
		Graph_CheckLifetimes(iTimeOfLatestLog);
	}
}

void logParserLib15MinuteUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (!gbStandAlone)
	{
		Graph_DumpStatusFile();
	}
}

void logParserLib60MinuteUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (!gbStandAlone)
	{
		Graph_CopyStatusFile();
	}
}



//graph-specific version of objPathGetEString which looks in a substruct if the obj path begins with @
bool Graph_objPathGetEString_internal(const char* path, ParseTable table[], void* structptr, ParseTable subTable[], void *substructptr,
					  char** estr, char **ppResultString)
{
	if (path[0] == '@' && subTable && substructptr)
	{
		return objPathGetEStringWithResult(path + 1, subTable, substructptr, estr, ppResultString);
	}

	return objPathGetEStringWithResult(path, table, structptr, estr, ppResultString);
}

//wrapper around the previous function which deals with {}'s, so that "{.objInfo.objName}@{.objInfo.owernName}" will
//work the way you expect
bool Graph_objPathGetEString_Middle(const char* path, ParseTable table[], void* structptr, ParseTable subTable[], void *substructptr,
					  char** estr, char **ppResultString)
{
	char *pFirstBrace = strchr(path, '{');
	char *pTemp = NULL;
	char *pTempOutput = NULL;
	char *pWorkString = NULL;

	if (!pFirstBrace)
	{
		return Graph_objPathGetEString_internal(path, table, structptr, subTable, substructptr, estr, ppResultString);
	}

	estrStackCreate(&pTemp);
	estrStackCreate(&pTempOutput);
	estrStackCreate(&pWorkString);

	estrCopy2(&pWorkString, path);

	while (1)
	{
		char *pFirstCloseBrace;
		bool bRetVal;
		int iFirstBraceOffset;
		int iCloseBraceOffset;

		pFirstBrace = strchr(pWorkString, '{');


		if (!pFirstBrace)
		{
			break;
		}

		pFirstCloseBrace = strchr(pWorkString, '}');

		if (!pFirstCloseBrace)
		{
			if (ppResultString)
			{
				estrPrintf(ppResultString, "non-matching curly braces in xpath %s", path);
				estrDestroy(&pTemp);
				estrDestroy(&pTempOutput);
				estrDestroy(&pWorkString);
				return false;
			}
		}

		iFirstBraceOffset = pFirstBrace - pWorkString;
		iCloseBraceOffset = pFirstCloseBrace - pWorkString;

		estrClear(&pTempOutput);
		estrClear(&pTemp);
		estrConcat(&pTemp, pFirstBrace + 1, pFirstCloseBrace - pFirstBrace - 1);

		bRetVal = Graph_objPathGetEString_internal(pTemp, table, structptr, subTable, substructptr, &pTempOutput, ppResultString);
		if (!bRetVal)
		{
			estrDestroy(&pTemp);
			estrDestroy(&pTempOutput);
			estrDestroy(&pWorkString);
			return false;
		}
	
		estrRemove(&pWorkString, iFirstBraceOffset, iCloseBraceOffset - iFirstBraceOffset + 1);
		estrInsert(&pWorkString, iFirstBraceOffset, pTempOutput, estrLength(&pTempOutput));
	}

	if (estr)
	{
		estrCopy(estr, &pWorkString);
	}

	estrDestroy(&pTemp);
	estrDestroy(&pTempOutput);
	estrDestroy(&pWorkString);
	return true;
}


//wrapper around above function which looks for ! as the first character. If ! is the first character, then it is followed by
//a token which is the name of a string-returning command. Take ther est of the string, process it as normal, then apply
//the command to it
bool Graph_objPathGetEString(const char* path, ParseTable table[], void* structptr, ParseTable subTable[], void *substructptr,
					  char** estr, char **ppResultString)
{
	if (path[0] == '!')
	{
		char *pFirstSpace;
		char *pAfterSpace;
		char *pInnerString = NULL;
		int iRetVal;

		if (IS_WHITESPACE(path[1]))
		{
			estrPrintf(ppResultString, "Badly formed Graph_objPathGetEString command \"%s\"", path);
			return false;
		}

		pFirstSpace = strchr(path + 1, ' ');

		if (!pFirstSpace)
		{
			estrPrintf(ppResultString, "Badly formed Graph_objPathGetEString command \"%s\"", path);
			return false;
		}

		pAfterSpace = pFirstSpace+1;

		while (IS_WHITESPACE(*pAfterSpace))
		{
			pAfterSpace++;
		}

		if (!*pAfterSpace)
		{
			estrPrintf(ppResultString, "Badly formed Graph_objPathGetEString command \"%s\"", path);
				return false;
		}

		if (!Graph_objPathGetEString_Middle(pAfterSpace, table, structptr, subTable, substructptr, &pInnerString, ppResultString))
		{
			estrDestroy(&pInnerString);
			return false;
		}

		estrInsert(&pInnerString, 0, path + 1, pFirstSpace - path);


		iRetVal = cmdParseAndReturn(pInnerString, estr, CMD_CONTEXT_HOWCALLED_LOGPARSER);

		if (!iRetVal)
		{
			estrPrintf(ppResultString, "CmdParse error in LogParser while processing \"%s\"", pInnerString);
		}

		estrDestroy(&pInnerString);

		return iRetVal;
	}
	else
	{
		return Graph_objPathGetEString_Middle(path, table, structptr, subTable, substructptr, estr, ppResultString);
	}
}



bool Graph_objPathGetMultiVal(const char* path, ParseTable table[], void* structptr, ParseTable subTable[], void *substructptr,
						MultiVal *result, char **ppResultString)
{
	if (path[0] == '@' && subTable && substructptr)
	{
		return objPathGetMultiValWithResult(path + 1, subTable, substructptr, result, ppResultString);
	}
	
	return objPathGetMultiValWithResult(path, table, structptr, result, ppResultString);
}


void FindLogsThatMatchExpressionAndProcess(LogParserProcessingCallback *pCB, void *pUserData, UseThisLogCheckingStruct *pCheckingStruct,
	ParsedLog *pLog, ActionSpecificCallbackType eCallbackType)
{
	if (pCheckingStruct->pEArrayContainingDataPoints && pCheckingStruct->pEArrayContainingDataPoints[0])
	{
		void **ppEArray;
		ParseTable *pSubTable;
		int j, k;

		PERFINFO_AUTO_START("DataPoints", 1);

		if (!objPathGetEArray(pCheckingStruct->pEArrayContainingDataPoints, parse_ParsedLog, pLog, 
				  &ppEArray, &pSubTable))
		{
			PERFINFO_AUTO_STOP();
			return;
		}

		if (!pSubTable || eaSize(&ppEArray) == 0)
		{
			PERFINFO_AUTO_STOP();
			return;
		}

		if (pCheckingStruct->pUseThisLogCheck_Expression)
		{
			if (!exprEvaluateRawStringWithObject(pCheckingStruct->pUseThisLogCheck_Expression,
				"log", pLog, parse_ParsedLog, true))
			{
				PERFINFO_AUTO_STOP();
				return;
			}
		}

		for (j=0; j < eaSize(&ppEArray); j++)
		{
			bool bAllMatch = true;

			PERFINFO_AUTO_START("per point", 1);
			for (k=0; k < eaSize(&pCheckingStruct->ppUseThisLog_EqualityChecks); k++)
			{
				bool bMatch = false;
				UseThisLogCheckEquality *pEqualityCheck = pCheckingStruct->ppUseThisLog_EqualityChecks[k];

				char *pXPath = NULL;
				estrStackCreate(&pXPath);

				if (Graph_objPathGetEString(pEqualityCheck->pXPath, parse_ParsedLog, pLog, pSubTable, ppEArray[j], &pXPath, NULL)
					&& (pEqualityCheck->pComparee == NULL || pEqualityCheck->pComparee[0] == 0 || stricmp(pXPath, pEqualityCheck->pComparee) == 0))
				{
					bMatch = true;
				}

				estrDestroy(&pXPath);

				if (!bMatch)
				{
					bAllMatch = false;
					break;
				}
			}

			if (!bAllMatch)
			{
				PERFINFO_AUTO_STOP();
				continue;
			}


			gPerfStats.iActionCallbacks[eCallbackType]++;
			if (eCallbackType >= ASC_LPCO_FIRST && eCallbackType <= ASC_LPCO_LAST)
			{
				gPerfStats.iTotalLPCOCallbacks++;
			}
			pCB(pUserData, pLog, pSubTable, ppEArray[j]);
			PERFINFO_AUTO_STOP();
		}
		PERFINFO_AUTO_STOP();
	}
	else
	{
		int k;
		PERFINFO_AUTO_START("non-Datapoints",1);

		if (pCheckingStruct->pUseThisLogCheck_Expression)
		{
			if (!exprEvaluateRawStringWithObject(pCheckingStruct->pUseThisLogCheck_Expression,
				"log", pLog, parse_ParsedLog, true))
			{
				PERFINFO_AUTO_STOP();
				return;
			}
		}


		for (k=0; k < eaSize(&pCheckingStruct->ppUseThisLog_EqualityChecks); k++)
		{
			bool bMatch = false;
			UseThisLogCheckEquality *pEqualityCheck = pCheckingStruct->ppUseThisLog_EqualityChecks[k];

			char *pXPath = NULL;
			estrStackCreate(&pXPath);

			if (objPathGetEString(pEqualityCheck->pXPath, parse_ParsedLog, pLog, &pXPath)
				&& (pEqualityCheck->pComparee == NULL || pEqualityCheck->pComparee[0] == 0 || stricmp(pXPath, pEqualityCheck->pComparee) == 0))
			{
				bMatch = true;
			}

			estrDestroy(&pXPath);

			if (!bMatch)
			{
				PERFINFO_AUTO_STOP();
				return;
			}
		}
	


		gPerfStats.iActionCallbacks[eCallbackType]++;
		if (eCallbackType >= ASC_LPCO_FIRST && eCallbackType <= ASC_LPCO_LAST)
		{
			gPerfStats.iTotalLPCOCallbacks++;
		}
		pCB(pUserData, pLog, NULL, NULL);
		PERFINFO_AUTO_STOP();
	}
}



void DoAllActionSpecificCallbacks(ParsedLog *pLog, const char *pActionName)
{
	ActionSpecificCallbackList *pList;
	int i;

	if (!sActionSpecificCallbackLists)
	{
		return;
	}


	if (!(pActionName && pActionName[0]))
	{
		return;
	}

	if (!stashFindPointer(sActionSpecificCallbackLists, pActionName, &pList))
	{
		return;
	}

	if(gbStaticAnalysis && pLog->pMessage)
	{
		size_t length = strlen(pLog->pMessage);
		++pList->iLogLinesUsed;
		pList->iBytesProcessed += (U32)(length > 0 ? length : 0);
	}

	for (i=0; i < eaSize(&pList->ppCallbacks); i++)
	{
		ActionSpecificCallback *pCallbackStruct = pList->ppCallbacks[i];

		FindLogsThatMatchExpressionAndProcess(pCallbackStruct->pCB, pCallbackStruct->pUserData, pCallbackStruct->pCheckingStruct, pLog, 
			pCallbackStruct->eType);
	}
}


FileNameBucketList *spBucketList = NULL;


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void AbortDirectoryScanning(void)
{
	if (!gbCurrentlyScanningDirectories)
	{
		return;
	}

	

	gbCurrentlyScanningDirectories = false;
	gElapsedTime = timeSecondsSince2000() - gStartTime;

	AbortFilteredLogFile();

	Graph_ResetSystem();
	LPCO_ResetSystem();
	LPAggregator_ResetSystem();

	if(gStandAloneOptions.bCreateBinnedLogFiles)
	{
		LogParserBinning_Close();
	}

	AbortChunkedLogProcessing(spBucketList);

	DestroyFileNameBucketList(spBucketList);
	spBucketList = NULL;
}

static void LogParserBucketProcessCB(char *pFileName, ParsedLog *pLog)
{
	ProcessSingleLog(pFileName, pLog, false);
	ProcessAllProceduralLogs();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void BeginDirectoryScanning(void)
{
	int i, j;

	char **ppFileNamesToMatch = NULL;
	StashTableIterator iterator;
	StashElement element;

	if (gbCurrentlyScanningDirectories)
	{
		return;
	}


	if (gStandAloneOptions.bWebFilteredScan)
	{
		InitFilteredLogMemory();
	}
	else if (gStandAloneOptions.bCreateFilteredLogFile)
	{
		InitFilteredLogFile(gStandAloneOptions.bCompressFilteredLogFile);
	}

	gStartTime = timeSecondsSince2000();
	if(gbStaticAnalysis)
		giLogLinesUsed = 0;
	gbCurrentlyScanningDirectories = true;


	if(gbLiveLikeStandAlone)
	{
		LogParser_FullyResetEverything();
	}
	else
	{
		Graph_ResetSystem();
		LPCO_ResetSystem();
		LPAggregator_ResetSystem();
	}

	if(gStandAloneOptions.bCreateBinnedLogFiles)
	{
		LogParserBinning_Reset("Temp");
	}

	if(!gbLogCountingMode && !gbLiveLikeStandAlone)
	{
		stashGetIterator(sGraphsByGraphName, &iterator);
		
		while (stashGetNextElement(&iterator, &element))
		{
			Graph *pGraph = stashElementGetPointer(element);
			if (!pGraph->pDefinition->bIAmALongTermGraph && pGraph->bActiveInStandaloneMode)
			{
				DivideString(pGraph->pDefinition->pFilenamesNeeded, ",", &ppFileNamesToMatch, 
					DIVIDESTRING_POSTPROCESS_ALLOCADD | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
			}
		}

		if (gStandAloneOptions.pFilenameRestrictions)
		{
			DivideString(gStandAloneOptions.pFilenameRestrictions, ",", &ppFileNamesToMatch,
				DIVIDESTRING_POSTPROCESS_ALLOCADD | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
		}
	}

	if (eaSize(&ppFileNamesToMatch))
	{
		for (i=0; i < eaSize(&ppFileNamesToMatch); i++)
		{
			printf("%s%s", i == 0 ? " ":", ", ppFileNamesToMatch[i]);
		}
		printf("\n");
	}
	else
	{
		printf("(No restrictions)\n");
	}

	spBucketList = CreateFileNameBucketList(LogParserBucketProcessCB, LogParserPostProcessCB, GetCurrentParsingFlags());

	for (i=0; i < eaSize(&gStandAloneOptions.ppDirectoriesToScan); i++)
	{
		char **ppOriginalFileList;


		ppOriginalFileList = fileScanDirFolders(gStandAloneOptions.ppDirectoriesToScan[i], FSF_FILES);
	
		printf("Found %d files on first pass\n", eaSize(&ppOriginalFileList));

		for (j = eaSize(&ppOriginalFileList) - 1; j>=0; j--)
		{
			if (stashFindPointer(sFilesProcessedTable, ppOriginalFileList[j], NULL))
			{
				eaRemoveFast(&ppOriginalFileList, j);
			}
			else if (eaSize(&ppFileNamesToMatch))
			{
				int k;
				bool bFound = false;

				for (k=0; k < eaSize(&ppFileNamesToMatch); k++)
				{
					char shortName[MAX_PATH];
					getFileNameNoExtNoDirs(shortName, ppOriginalFileList[j]);
					if (strStartsWith(shortName, ppFileNamesToMatch[k]))
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					eaRemoveFast(&ppOriginalFileList, j);
				}
			}
		}


		printf("Found %d files after initial filter\n", eaSize(&ppOriginalFileList));


		DivideFileListIntoBuckets(spBucketList, &ppOriginalFileList);
	
		fileScanDirFreeNames(ppOriginalFileList);
	}


	eaDestroy(&ppFileNamesToMatch);

	ApplyRestrictionsToBucketList(spBucketList, gbLiveLikeStandAlone ? &gImplicitRestrictions : &gStandAloneOptions.parsingRestrictions);
	
	BeginChunkedLogProcessing(spBucketList, gbLiveLikeStandAlone ? &gImplicitRestrictions : &gStandAloneOptions.parsingRestrictions);

	giDirScanningPercent = 0;
	estrPrintf(&gpDirScanningStatus, "Directory scanning begun");
}

	
void UpdateDirectoryScanning(void)
{
	PERFINFO_AUTO_START_FUNC();
	if (ContinueChunkedLogProcessing(spBucketList, 1.0f, &giDirScanningPercent, &gpDirScanningStatus))
	{
		LogParserBinning_Close();
		DestroyFileNameBucketList(spBucketList);
		spBucketList = NULL;
		gbCurrentlyScanningDirectories = false;
		CloseFilteredLogFile();
		printf("100%% complete. Done.\n");
		gElapsedTime = timeSecondsSince2000() - gStartTime;
		if(gbStaticAnalysis)
		{
			CalculateLoadByCallbackName();
		}
		if(gbLogCountingMode)
		{
			WriteLogCountingData();
		}
		printf("Elapsed Time: %lu.\n", gElapsedTime);

		if (gbDoFrontEndFiltering)
		{
			FrontEndFiltering_Done();
		}
	}
	else
	{
		printf("%d%% complete. %s\n", giDirScanningPercent, gpDirScanningStatus);
	}
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void SetFileNameRestrictions(ACMD_SENTENCE pRestrictions)
{
	estrCopy2(&gStandAloneOptions.pFilenameRestrictions, pRestrictions);
	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *SetDirectoriesToScan_Precise(ACMD_SENTENCE pDirectoriesToScan)
{
	static char retString[CRYPTIC_MAX_PATH];
	int i;
	char **directoriesToScan = NULL;
	DivideString(pDirectoriesToScan, ",", &directoriesToScan, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
	for(i = 0; i < eaSize(&directoriesToScan); ++i)
	{
		if(!fileIsAbsolutePath(directoriesToScan[i]))
		{
			sprintf(retString, "%s is not an absolute path, aborting.", directoriesToScan[i]);
			eaDestroyEx(&directoriesToScan, NULL);
			return retString;
		}
		if(!dirExists(directoriesToScan[i]))
		{
			sprintf(retString, "%s doesn't exist, aborting.", directoriesToScan[i]);
			eaDestroyEx(&directoriesToScan, NULL);
			return retString;
		}
	}

	eaDestroyEx(&gStandAloneOptions.ppDirectoriesToScan, NULL);

	for(i = 0; i < eaSize(&directoriesToScan); ++i)
	{
		eaPush(&gStandAloneOptions.ppDirectoriesToScan, directoriesToScan[i]);
	}

	// Don't destroy the strings.
	eaDestroy(&directoriesToScan);
	SaveStandAloneOptions(NULL);
	sprintf(retString, "Success");
	return retString;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char * SetDirectoriesToScan_Easy(ACMD_SENTENCE pDirName)
{
	static char retString[CRYPTIC_MAX_PATH];
	int i;
	char *pTempString = NULL;
	estrStackCreate(&pTempString);
	estrCopy2(&pTempString, pDirName);
	estrTrimLeadingAndTrailingWhitespace(&pTempString);

	for (i=0; i < eaSize(&gLogParserConfig.ppCommonSearchLocations); i++)
	{
		if (stricmp(pTempString, gLogParserConfig.ppCommonSearchLocations[i]->pName) == 0)
		{
			eaDestroyEx(&gStandAloneOptions.ppDirectoriesToScan, NULL);
			eaPush(&gStandAloneOptions.ppDirectoriesToScan, strdup(gLogParserConfig.ppCommonSearchLocations[i]->pDir));
			sprintf(retString, "Now scanning for logs in %s", gLogParserConfig.ppCommonSearchLocations[i]->pDir);
			estrDestroy(&pTempString);
			SaveStandAloneOptions(NULL);
			return retString;
		}
	}

	sprintf(retString, "Directory setting failed");
	estrDestroy(&pTempString);
	return retString;
}

/*
AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void SetMapToScan(char *pPublicName)
{
	eaDestroyEx(&gStandAloneOptions.parsingRestrictions.ppMapNames, NULL);
	
	if (stricmp(pPublicName, "ALL") == 0)
	{
	}
	else
	{
		const char *pLogWorldName = worldGetZoneMapFilenameByPublicName(pPublicName);
		if (pLogWorldName)
		{	
			eaPush(&gStandAloneOptions.parsingRestrictions.ppMapNames, strdup(pLogWorldName));
		}
	}
	SaveStandAloneOptions(NULL);
}
*/
AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void AddMapRestriction(char *pPublicName)
{
	const char *pLogWorldName = allocAddString(worldGetZoneMapFilenameByPublicName(pPublicName));
	if (eaFindString(&gStandAloneOptions.parsingRestrictions.ppMapNames, pLogWorldName) == -1)
	{
		eaPush(&gStandAloneOptions.parsingRestrictions.ppMapNames, strdup(pLogWorldName));
	}
	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void ClearMapRestrictions(void)
{
	eaDestroyEx(&gStandAloneOptions.parsingRestrictions.ppMapNames, NULL);
	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void AddObjectRestriction(char *pName)
{
	if (eaFindString(&gStandAloneOptions.parsingRestrictions.ppObjNames, pName) == -1)
	{
		eaPush(&gStandAloneOptions.parsingRestrictions.ppObjNames, strdup(pName));
	}
	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void ClearObjectRestrictions(void)
{
	eaDestroyEx(&gStandAloneOptions.parsingRestrictions.ppObjNames, NULL);
	SaveStandAloneOptions(NULL);
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void AddActionRestriction(char *pName)
{
	SubActionList *pSubList;

	if (!gStandAloneOptions.parsingRestrictions.sAction)
	{
		gStandAloneOptions.parsingRestrictions.sAction = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);
	}
	

			
	if(!stashFindPointer(gStandAloneOptions.parsingRestrictions.sAction, pName, &pSubList))
	{
		pSubList = calloc(sizeof(SubActionList), 1);
		stashAddPointer(gStandAloneOptions.parsingRestrictions.sAction, pName, pSubList, false);
	
		eaPush(&gStandAloneOptions.parsingRestrictions.ppActions, strdup(pName));
	}
	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void ClearActionRestrictions(void)
{
	if (gStandAloneOptions.parsingRestrictions.sAction)
	{
		stashTableClearEx(gStandAloneOptions.parsingRestrictions.sAction, NULL, ClearSubActionRestrictions);
	}

	eaDestroyEx(&gStandAloneOptions.parsingRestrictions.ppActions, NULL);
	SaveStandAloneOptions(NULL);
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void AddOwnerRestriction(char *pName)
{
	if (eaFindString(&gStandAloneOptions.parsingRestrictions.ppOwnerNames, pName) == -1)
	{
		eaPush(&gStandAloneOptions.parsingRestrictions.ppOwnerNames, strdup(pName));
	}
	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void SetExpressionRestriction(ACMD_SENTENCE pRestriction)
{
	SAFE_FREE(gStandAloneOptions.parsingRestrictions.pExpression);

	if (pRestriction && !StringIsAllWhiteSpace(pRestriction) && stricmp(pRestriction, "\"\"") != 0)
	{
		gStandAloneOptions.parsingRestrictions.pExpression = strdup(pRestriction);
	}
	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *SetSubstringRestriction(ACMD_SENTENCE pRestriction)
{
	char *error = NULL;

	eaDestroyEx(&gStandAloneOptions.parsingRestrictions.ppSubstring, NULL);

	if (pRestriction && !StringIsAllWhiteSpace(pRestriction) && stricmp(pRestriction, "\"\"") != 0)
	{
		DivideString(pRestriction, ",", &gStandAloneOptions.parsingRestrictions.ppSubstring,
			DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
	}

	return pRestriction;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *SetSubstringInverseRestriction(ACMD_SENTENCE pRestriction)
{
	char *error = NULL;

	eaDestroyEx(&gStandAloneOptions.parsingRestrictions.ppSubstringInverse, NULL);

	if (pRestriction && !StringIsAllWhiteSpace(pRestriction) && stricmp(pRestriction, "\"\"") != 0)
	{
		DivideString(pRestriction, ",", &gStandAloneOptions.parsingRestrictions.ppSubstringInverse,
			DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
	}

	return pRestriction;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *SetSubstringCaseSensitiveRestriction(ACMD_SENTENCE pRestriction)
{
	char *error = NULL;

	eaDestroyEx(&gStandAloneOptions.parsingRestrictions.ppSubstringCaseSensitive, NULL);

	if (pRestriction && !StringIsAllWhiteSpace(pRestriction) && stricmp(pRestriction, "\"\"") != 0)
	{
		DivideString(pRestriction, ",", &gStandAloneOptions.parsingRestrictions.ppSubstringCaseSensitive,
			DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
	}

	return pRestriction;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *SetSubstringCaseSensitiveInverseRestriction(ACMD_SENTENCE pRestriction)
{
	char *error = NULL;

	eaDestroyEx(&gStandAloneOptions.parsingRestrictions.ppSubstringCaseSensitiveInverse, NULL);

	if (pRestriction && !StringIsAllWhiteSpace(pRestriction) && stricmp(pRestriction, "\"\"") != 0)
	{
		DivideString(pRestriction, ",", &gStandAloneOptions.parsingRestrictions.ppSubstringCaseSensitiveInverse,
			DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
	}

	return pRestriction;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *SetRegexRestriction(ACMD_SENTENCE pRestriction)
{
	char *error = NULL;
	int result;

	SAFE_FREE(gStandAloneOptions.parsingRestrictions.pRegex);

	if (!pRestriction || !stricmp(pRestriction, "\"\""))
		return "No expression specified";

	// Verify that expression is OK.
	result = regexMatch_s(pRestriction, "", NULL, 0, &error);
	if (error && result < -1)
		return error;
	else if (result < -1)
		return "Unknown regex compilation error";

	// Save regex.
	gStandAloneOptions.parsingRestrictions.pRegex = strdup(pRestriction);
	SaveStandAloneOptions(NULL);

	return pRestriction;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *SetRegexInverseRestriction(ACMD_SENTENCE pRestriction)
{
	char *error = NULL;
	int result;

	SAFE_FREE(gStandAloneOptions.parsingRestrictions.pRegexInverse);

	if (!pRestriction || !stricmp(pRestriction, "\"\""))
		return "No expression specified";

	// Verify that expression is OK.
	result = regexMatch_s(pRestriction, "", NULL, 0, &error);
	if (error && result < -1)
		return error;
	else if (result < -1)
		return "Unknown regex compilation error";

	// Save regex.
	gStandAloneOptions.parsingRestrictions.pRegexInverse = strdup(pRestriction);
	SaveStandAloneOptions(NULL);

	return pRestriction;
}
	


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void ClearOwnerRestrictions(void)
{
	eaDestroyEx(&gStandAloneOptions.parsingRestrictions.ppOwnerNames, NULL);
	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *AddPlayerRestriction(char *pName)
{
	static char retString[CRYPTIC_MAX_PATH];
	char *at = strchr_fast_nonconst(pName, '@');

	if(!at || !*(at + 1)) // Verify that there is an @ and that there is at least one non-zero character after it.
	{
		sprintf(retString, "%s is not a valid Player identifier. Must be <playername>@<accountname>", pName);
		return retString;
	}

	*at = 0;

	AddObjectRestriction(pName);
	AddOwnerRestriction(at+1);
	*at = '@';
	sprintf(retString, "Success");
	return retString;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void ClearPlayerRestrictions(void)
{
	ClearObjectRestrictions();
	ClearOwnerRestrictions();
}




void LogParser_AddProceduralLog(ParsedLog *pLog, const char *pFixedUpLogString)
{
	// If we're filtering or creating bins, create raw log line.
	if ((gStandAloneOptions.bCreateFilteredLogFile || gStandAloneOptions.bCreateBinnedLogFiles) && !pLog->pRawLogLine)
	{
		char *pRawLog = NULL;

		PERFINFO_AUTO_START("CreateRawFromProcedural", 1);

		estrStackCreate(&pRawLog);
		estrPrintf(&pRawLog, "%s %6d %s[%d]: : %s(%s)", timeGetLogDateStringFromSecondsSince2000(pLog->iTime), 0, 
			"procedural", 1, pLog->pObjInfo->pAction, pFixedUpLogString);
		pLog->pRawLogLine = strdup(pRawLog);
		estrDestroy(&pRawLog);

		PERFINFO_AUTO_STOP();
	}

	pLog->bProcedural = true;

	eaPush(&sppProceduralLogs, pLog);
}

void UpdateIncludeOtherLogsCommand(void)
{
	if (gStandAloneOptions.bIncludeOtherLogs_bool)
	{
		estrPrintf(&gStandAloneOptions.pIncludeOtherLogs, "<a href=\"/directcommand?command=SetIncludeOtherLogs 0\" class=\"js-button\">Currently INCLUDING other logs... click to toggle</a>");
	}
	else
	{
		estrPrintf(&gStandAloneOptions.pIncludeOtherLogs, "<a href=\"/directcommand?command=SetIncludeOtherLogs 1\" class=\"js-button\">Currently NOT including other logs... click to toggle</a>");
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void SetIncludeOtherLogs(int bSet)
{
	gStandAloneOptions.bIncludeOtherLogs_bool = bSet;
	SaveStandAloneOptions(NULL);
}

AUTO_FIXUPFUNC;
TextParserResult standAloneOptionsFixup(LogParserStandAloneOptions *pOptions, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			if (eaSize(&pOptions->parsingRestrictions.ppActions))
			{
				int i;

				if (!pOptions->parsingRestrictions.sAction)
				{
					pOptions->parsingRestrictions.sAction = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);
				}
				else
				{
					stashTableClearEx(pOptions->parsingRestrictions.sAction, NULL, ClearSubActionRestrictions);
				}

				for (i=0; i < eaSize(&pOptions->parsingRestrictions.ppActions); i++)
				{
					SubActionList *pSubList;
					pSubList = calloc(sizeof(SubActionList), 1);
					stashAddPointer(pOptions->parsingRestrictions.sAction, pOptions->parsingRestrictions.ppActions[i], pSubList, false);
				}
			}		
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void LoadStandAloneOptions(char *pFileName)
{
	char fileName[CRYPTIC_MAX_PATH];
	StashTableIterator iterator;
	StashElement element;

	if (!pFileName)
	{
		pFileName = "_CURRENT";
	}

	StructDeInit(parse_LogParserStandAloneOptions, &gStandAloneOptions);
	StructInit(parse_LogParserStandAloneOptions, &gStandAloneOptions);

	sprintf(fileName, "%s/LogParserOptions/%s.txt", LogParserLocalDataDir(), pFileName);
	if (fileExists(fileName))
	{
		ParserReadTextFile(fileName, parse_LogParserStandAloneOptions, &gStandAloneOptions, 0);
	}


	stashGetIterator(sGraphsByGraphName, &iterator);
	
	while (stashGetNextElement(&iterator, &element))
	{
		Graph *pGraph = stashElementGetPointer(element);
		if (!pGraph->pDefinition->bIAmALongTermGraph)
		{
			if (eaFindString(&gStandAloneOptions.ppActiveGraphNames, pGraph->pGraphName) != -1)
			{
				pGraph->bActiveInStandaloneMode = true;
			}
			else
			{
				pGraph->bActiveInStandaloneMode = false;
			}
		}
	}

	UpdateIncludeOtherLogsCommand();

	if (gStandAloneOptions.bTimeWasSetInEasyMode)
	{
		if (gStandAloneOptions.parsingRestrictions.iEndingTime)
		{
			U32 iDelta = timeSecondsSince2000() - gStandAloneOptions.parsingRestrictions.iEndingTime;
			gStandAloneOptions.parsingRestrictions.iEndingTime += iDelta;
			gStandAloneOptions.parsingRestrictions.iStartingTime += iDelta;
		}
	}




}


void SaveStandAloneOptions(char *pFileName)
{
	char fileName[CRYPTIC_MAX_PATH];

	StashTableIterator iterator;
	StashElement element;

	if(gStandAloneOptions.bWebFilteredScan)
		return;

	stashGetIterator(sGraphsByGraphName, &iterator);

	if (!pFileName)
	{
		pFileName = "_CURRENT";
	}

	eaDestroyEx(&gStandAloneOptions.ppActiveGraphNames, NULL);
	while (stashGetNextElement(&iterator, &element))
	{
		Graph *pGraph = stashElementGetPointer(element);
		if (!pGraph->pDefinition->bIAmALongTermGraph)
		{
			if (pGraph->bActiveInStandaloneMode)
			{
				eaPush(&gStandAloneOptions.ppActiveGraphNames, strdup(pGraph->pGraphName));
			}
		}
	}	

	sprintf(fileName, "%s/LogParserOptions/%s.txt", LogParserLocalDataDir(), pFileName);
	
	makeDirectoriesForFile(fileName);
	ParserWriteTextFile(fileName, parse_LogParserStandAloneOptions, &gStandAloneOptions, 0, 0);
	
	UpdateIncludeOtherLogsCommand();

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void SaveAndNameStandaloneOptions(ACMD_SENTENCE pName)
{
	char *pTempString = NULL;
	estrStackCreate(&pTempString);
	estrCopy2(&pTempString, pName);
	estrTrimLeadingAndTrailingWhitespace(&pTempString);
	estrMakeAllAlphaNumAndUnderscores(&pTempString);
	SaveStandAloneOptions(pTempString);
	estrDestroy(&pTempString);


}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void StartCreatingFilteredFile(int iStart)
{
	gStandAloneOptions.bCreateFilteredLogFile = (iStart != 0);
	SaveStandAloneOptions(NULL);

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void StartCreatingBinnedFile(int iStart)
{
	gStandAloneOptions.bCreateBinnedLogFiles = (iStart != 0);
	SaveStandAloneOptions(NULL);

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void SetFilteredFileIncludesProcedural(bool bSet)
{
	gStandAloneOptions.bFilteredFileIncludesProceduralLogs = bSet;
	SaveStandAloneOptions(NULL);

}	

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void CompressFilteredFile(int iTurnOn)
{
	gStandAloneOptions.bCompressFilteredLogFile = (iTurnOn != 0);
	SaveStandAloneOptions(NULL);

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void SetBinnedLogFileDirectory(ACMD_SENTENCE pDirectory)
{
	estrCopy2(&gStandAloneOptions.pBinnedLogFileDirectory, pDirectory);
	SaveStandAloneOptions(NULL);
}

bool LogParserPostProcessCB(ParsedLog *pLog)
{
	LogParserConstructedObject *pObj;
	char tempName[512];

	static const char *spPlayerPooled = NULL;

	int i;

	if (gbLoadingBins)
	{
		return true;
	}

	if (!spPlayerPooled)
	{
		spPlayerPooled = allocAddCaseSensitiveString("Player");
	}

	pLog->pParsingCategory = BASECATEGORY_OTHER;


	if (!pLog->pObjInfo)
	{
		return true;
	}

	if (pLog->pObjInfo->eObjType != GLOBALTYPE_ENTITYPLAYER)
	{
		return true;
	}

	if (!eaSize(&gStandAloneOptions.ppPlayerFilterCategories) && !eaSize(&gStandAloneOptions.ppExclusionPlayerFilters))
	{
		return true;
	}

	sprintf(tempName, "%s@%s@%u@%u", pLog->pObjInfo->pObjName, pLog->pObjInfo->pOwnerName, pLog->pObjInfo->iObjID, pLog->pObjInfo->iownerID);

	pObj = FindExistingLPCOFromNameAndTypeName(tempName, spPlayerPooled);

	if (!pObj)
	{
		pLog->pParsingCategory = BASECATEGORY_UNKNOWN;
		return true;
	}

	for (i=0; i < eaSize(&gStandAloneOptions.ppExclusionPlayerFilters); i++)
	{
		switch(PlayerLPCOMatchesFilter(pObj, gStandAloneOptions.ppExclusionPlayerFilters[i]))
		{
			case -1: 
				pLog->pParsingCategory = BASECATEGORY_UNKNOWN;
				return true;
			case 1:
				return false;
		}
	}

	for (i=0; i < eaSize(&gStandAloneOptions.ppPlayerFilterCategories); i++)
	{
		switch(PlayerLPCOMatchesFilter(pObj, gStandAloneOptions.ppPlayerFilterCategories[i]))
		{
			case -1: 
				pLog->pParsingCategory = BASECATEGORY_UNKNOWN;
				return true;
			case 1:
				pLog->pParsingCategory = gStandAloneOptions.ppPlayerFilterCategories[i]->pFilterName;
				return true;
		}
	}

	return true;
}


void ProcessAllProceduralLogs(void)
{
	int i;
	unsigned count = 0;
	const unsigned limit = 1000;
	PERFINFO_AUTO_START_FUNC();

	while (eaSize(&sppProceduralLogs))
	{
		ParsedLog **ppCopied = NULL;

		// Avoid infinite looping.
		if (count++ > limit)
		{
			Errorf("Detected potentially-infinite procedural log loop (%u iterations), discarding %u procedural logs",
				limit, eaSize(&sppProceduralLogs));
			eaDestroyStruct(&sppProceduralLogs, parse_ParsedLog);
			PERFINFO_AUTO_STOP();
			return;
		}

		eaCopy(&ppCopied, &sppProceduralLogs);
		eaDestroy(&sppProceduralLogs);

		for (i=0; i < eaSize(&ppCopied); i++)
		{
			ProcessSingleLog("procedural", ppCopied[i], false);
		}

		eaDestroy(&ppCopied);
	}
	PERFINFO_AUTO_STOP();
}

enumLogParsingFlags GetCurrentParsingFlags(void)
{
	return gStandAloneOptions.bCreateFilteredLogFile ? LOGPARSINGFLAG_COPYRAWLOGLINE : 0;
}

void LogParser_LoadConfigFiles(void)
{
	int i;

	if (gLogParserConfigFile[0])
	{
		ParserReadTextFile(gLogParserConfigFile, parse_LogParserConfig, &gLogParserConfig, 0);
	}
	else
	{

		ParserLoadFiles("server", "LogParserConfig.txt", NULL, 0, parse_LogParserConfig, &gLogParserConfig);
		ParserLoadFiles("server", "LogParserConfig.preproc.txt", NULL, 0, parse_LogParserConfig, &gLogParserConfig);
		
		if (gbStandAlone && !gbLiveLikeStandAlone)
		{
			ParserLoadFiles("server", "LogParserStandAloneConfig.txt", NULL, 0, parse_LogParserConfig, &gLogParserConfig);
			ParserLoadFiles("server", "LogParserStandAloneConfig.preproc.txt", NULL, 0, parse_LogParserConfig, &gLogParserConfig);
		}
	}

	for (i=0; i < eaSize(&gppExtraLogParserConfigFiles); i++)
	{
		int result = ParserReadTextFile(gppExtraLogParserConfigFiles[i], parse_LogParserConfig, &gLogParserConfig, 0);
		if (!result)
			AssertOrAlert("LOGPARSER_CONFIG_ERROR", "Unable to load extra config file: %s", gppExtraLogParserConfigFiles[i]);
	}

	if (gpRunTimeConfig)
	{
		ParserReadText(gpRunTimeConfig, parse_LogParserConfig, &gLogParserConfig, 0);
	}
}

void LogParser_FullyResetEverything(void)
{
	ResetLogParserTimedCallbacks();
	ResetActionSpecificCallbacks();
	StructDeInit(parse_LogParserConfig, &gLogParserConfig);
	LogParser_LoadConfigFiles();

	Graph_FullyResetSystem();
	LPCO_FullyResetSystem();
	LPAggregator_FullyResetSystem();
}

static void UpdateLinkToLogServer()
{
	static U32 iLastConnectTime;
	static U32 iNextSendFPSTime;

	U32 iCurTime = timeSecondsSince2000();

	if (!pLogServerLink)
	{
		pLogServerLink = commConnect(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,gServerLibState.logServerHost,LOGSERVER_LOGPARSER_PORT,LogParserLinkCallback,0,0,0);
		iLastConnectTime = iCurTime;

		return;
	}

	//after 5 seconds, if we tried to connect but have not succeeded, start over
	if ((!linkConnected(pLogServerLink) || linkDisconnected(pLogServerLink)) && iCurTime > iLastConnectTime + 5)
	{
		linkRemove_wReason(&pLogServerLink, "Timed out or disconnected in UpdateLinkToLogServer");
	}
}

int main(int argc,char **argv)
{
	int		i;
	int frameTimer;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	SetAppGlobalType(GLOBALTYPE_LOGPARSER);
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	gbStandAlone |= gbStaticAnalysis | gbLiveLikeStandAlone | gbLogCountingMode | gbResendLogs;

	InitPreparsedStashTable();
	InitServerDataStashTable();

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'P', 0x8080ff);

	//Loads a bunch of stuff, can't do FolderCacheChooseModeNoPigsInDevelopment();
	FolderCacheChooseMode();

	preloadDLLs(0);

	printf( "SVN Revision: %d\n", gBuildVersion);

	serverLibStartup(argc, argv);

	setSafeCloseAction(consoleCloseHandler);
	useSafeCloseHandler();
	disableConsoleCloseButton();

	if(gbResendLogs)
	{
		svrLogInit();
	}

	if (!ShardInfoStringWasSet() && !gbNoShardMode)
	{
		printf("No shard info string set... assuming standalone\n");
		gbStandAlone = true;
	}
	else if (gbNoShardMode)
	{
		ServerLibSetControllerHost("NONE");
		printf("Running in no shard mode\n");
	}

	if(gbStandAlone)
		setConsoleTitle("Standalone LogParser");
	else if(gbNoShardMode)
		setConsoleTitle("Global LogParser");
	else
		setConsoleTitle("LogParser");

	GenericFileServing_Begin(0, 0);
	GenericFileServing_ExposeDirectory("c:\\LogParser\\Downloads");

	gImplicitRestrictions.sAction = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);

	if(gbLogCountingMode)
	{
		sEventTypeLogCount = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);
		sActionLogCount = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);
	}

	if (gbStandAlone || gbNoShardMode)
	{
		gbHttpServingAllowCommandsInURL = true;
		GenericHttpServing_Begin(gbNoShardMode ? DEFAULT_WEBMONITOR_GLOBAL_LOG_PARSER : siStartingPortNum, GetProductName(),
			DEFAULT_HTTP_CATEGORY_FILTER, GHSFLAG_AUTOUPDATE_ALWAYS_ON);
		gHttpXpathListCutoffSize = 2000;
	}

	if (!gbStandAlone)
	{
		if (!gbNoShardMode)
		{
			//attempt to connect to controller
			AttemptToConnectToController(false, lpHandleControllerMsg, true);
		}
	}

	LogParser_DoCrossroadsStartupStuff();

	SetGloballyIgnoreNonNullRefs(true);

	frameTimer = timerAlloc();
	resRegisterDictionaryForStashTable("LogFileGroups", RESCATEGORY_OTHER, 0, sLogFileGroupsByName, parse_LogFileGroup);

	TimedCallback_Add(logParserLibPeriodicUpdate, NULL, 3.0f);
	TimedCallback_Add(logParserLib15MinuteUpdate, NULL, 15.0f * 60.0f);
	TimedCallback_Add(logParserLib60MinuteUpdate, NULL, 60.0f * 60.0f);


	LogParser_LoadConfigFiles();

	Graph_CheckForLongTermGraphs();

	Graph_InitSystem();
	LPCO_InitSystem();
	LPAggregator_InitSystem();

	LogParserUniqueCounter_InitAllFromConfig();

	worldLoadZoneMaps();

	if (!gbStandAlone)
	{
		char longTermLogsFilename[CRYPTIC_MAX_PATH];
		ParsedLog **ppLogs = NULL;
		sprintf(longTermLogsFilename, "%s/LongTermLogs.log", fileLogDir());

		LoadLogsFromFile(longTermLogsFilename);
	
	
		DirectlyInformControllerOfState("ready");

	}

	//in standalone mode, set controller host to NONE so that errors don't get sent to a
	//nonexistant controller
	if (gbStandAlone)
	{
		char configFileDirName[CRYPTIC_MAX_PATH];
		ResourceDictionaryInfo *pPowerDictionaryInfo;
		NameList *pPowerNameList;
		NameList *pConfigFilesNameList;
		NameList *pMapNameList;
		const char **ppMapNames = NULL;
		RefDictIterator iter;
		ZoneMapInfo *zminfo;

		sprintf(gServerLibState.controllerHost, "NONE");

		if (siStartingPortNum == 80)
		{
			openURL("http://localhost");
		}

		if(!gStandAloneOptions.bWebFilteredScan && !gbDoFrontEndFiltering)
			LoadStandAloneOptions(NULL);


		//load in names of all powers so that we can have a command which chooses between them

		pPowerDictionaryInfo = StructCreate(parse_ResourceDictionaryInfo);
		ParserOpenReadBinaryFile(NULL, "server/bin/metadata/powers.metabin", parse_ResourceDictionaryInfo, pPowerDictionaryInfo, NULL, NULL, NULL, NULL, 0, 0, 0);
		pPowerNameList = CreateNameList_Bucket();
		NameList_AssignName(pPowerNameList, "PowersNameList");

		for (i=0; i < eaSize(&pPowerDictionaryInfo->ppInfos); i++)
		{
			NameList_Bucket_AddName(pPowerNameList, pPowerDictionaryInfo->ppInfos[i]->resourceName);
		}

		StructDestroy(parse_ResourceDictionaryInfo, pPowerDictionaryInfo);


		sprintf(configFileDirName, "%s/LogParserOptions", LogParserLocalDataDir());
		pConfigFilesNameList = CreateNameList_FilesInDirectory(configFileDirName, NULL);
		NameList_AssignName(pConfigFilesNameList, "ConfigFilesNameList");

		pMapNameList = CreateNameList_Bucket();
		NameList_AssignName(pMapNameList, "MapNameList");

		worldGetZoneMapIterator(&iter);
		eaClear(&ppMapNames);
		while (zminfo = worldGetNextZoneMap(&iter))
		{
			eaPush(&ppMapNames, zmapInfoGetPublicName(zminfo));
		}
		eaQSort(ppMapNames, strCmp);

		for (i=0; i < eaSize(&ppMapNames); i++)
		{
			NameList_Bucket_AddName(pMapNameList, ppMapNames[i]);
		}

		eaDestroy(&ppMapNames);

		FilteredLogFile_InitSystem();

		gbLogParsing_DoExtraStandaloneParsing = true;
	
	}

	if(gStandAloneOptions.bWebFilteredScan)
		BeginDirectoryScanning();

	while(!gbCloseLogParser)
	{
		F32 frametime;

		autoTimerThreadFrameBegin("main");

		ScratchVerifyNoOutstanding();

		frametime = timerElapsedAndStart(frameTimer);
		utilitiesLibOncePerFrame(frametime, 1);

		//Sleep(DEFAULT_SERVER_SLEEP_TIME);

		commMonitor(commDefault());

		UpdatePreparsedStashTable();

		serverLibOncePerFrame();

		if (gbStandAlone || gbNoShardMode)
		{
			GenericHttpServing_Tick();
		}

		if(gbStandAlone)
		{
			UpdateLinkToLiveLogParser();
		}
		else
		{
			UpdateLinkToLogServer();
		}

		LogParserUniqueCounter_Tick();

		ProcessAllProceduralLogs();



		CheckActivityServerStatus();
		UpdateLogParserTimedCalbacks();

		if (gbCurrentlyScanningDirectories)
		{
			if (gbDoFrontEndFiltering)
			{
				FrontEndFiltering_Tick();
			}
			UpdateDirectoryScanning();
		}
		else if (gbDoFrontEndFiltering)
		{
			FrontEndFiltering_Tick();
			BeginDirectoryScanning();
		}

		if (!gbDoFrontEndFiltering && (gbStandAloneForceExit || (giInactivityTimeOut && !gbStandAloneLogParserNeverTimeOut && (int)(timeSecondsSince2000() - GenericHttpServer_GetLastActivityTime()) > giInactivityTimeOut)))
		{
			exit(0);
		}

		// Exit if the we've been asked to or the controller is dying
		if (gTimeToDie && timeMsecsSince2000() > gTimeToDie)
		{
			printf("time to die\n");
			gbCloseLogParser = true;
		}

		autoTimerThreadFrameEnd();
	}

	Graph_DumpStatusFile();
	LogParserUniqueCounter_WriteOut();
	LogParserShutdownStandAlones();

	EXCEPTION_HANDLER_END
}


U32 LogParserRoundTime(U32 iTime, U32 iRoundAmount, U32 iOffset)
{
	U32 iTimeAtBeginningOfYear;
	int iDelta;
	SYSTEMTIME	t;

	timerLocalSystemTimeFromSecondsSince2000(&t,iTime);
	t.wHour = t.wMinute = t.wSecond =  t.wMilliseconds = 0;
	t.wDay = t.wMonth = 1;

	iTimeAtBeginningOfYear = timerSecondsSince2000FromLocalSystemTime(&t);
	iDelta = iTime - iTimeAtBeginningOfYear;
	iDelta /= iRoundAmount;
	iDelta *= iRoundAmount;
	return iTimeAtBeginningOfYear + iDelta + iOffset;
}

bool OVERRIDE_LATELINK_isValidRegionTypeForGame(U32 world_region_type)
{
	return true;
}

AUTO_COMMAND;
void FrontEndTest(int iUID)
{
	char *pCmdLine = NULL;

	estrPrintf(&pCmdLine, " -FrontEndFiltering_Start c:\\temp\\foo_%d 5 QlbraceQ013Q010Q013Q010ParsingrestrictionsQ013Q010QlbraceQ013Q010Q009Objnames_JamesQ013Q010Q009Ownernames_Wolfhound3060Q013Q010QrbraceQ013Q010Directoriestoscan_cQcolonQbslashtempQbslashgslogsQ013Q010Qrbrace", iUID);
	LogParserLaunching_LaunchOne(120, pCmdLine, 0, iUID);
}

AUTO_COMMAND;
void FrontEndTestKill(int iUID)
{
	LogParserLaunching_KillByUID(iUID);
}

//returns a positive integer (actually the port that the logparser is monitoring on) on success, 0 on failure
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
int BeginFrontEndFiltering(FrontEndFilterRequest *pRequest)
{
	char *pCmdLine = NULL;
	char *pTemp = NULL;
	char *pSuperEsc = NULL;
	int iRetVal;

	if (!pRequest->iUID || !pRequest->pDirectory || !pRequest->iSecondsDelay || !pRequest->pOptions)
	{
		return 0;
	}

	ParserWriteText(&pTemp, parse_LogParserStandAloneOptions, pRequest->pOptions, 0, 0, 0);
	estrSuperEscapeString(&pSuperEsc, pTemp);
	estrPrintf(&pCmdLine, " -FrontEndFiltering_Start %s %d %s ", pRequest->pDirectory, pRequest->iSecondsDelay,
		pSuperEsc);
	iRetVal = LogParserLaunching_LaunchOne(120, pCmdLine, 0, pRequest->iUID);

	estrDestroy(&pCmdLine);
	estrDestroy(&pTemp);
	estrDestroy(&pSuperEsc);

	return iRetVal;
}

#include "LogParser_h_ast.c"
#include "newcontrollertracker_pub_h_ast.c"
