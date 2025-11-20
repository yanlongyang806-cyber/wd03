#include "utilitiesLib.h"
#include "rand.h"
#include "hoglib.h"
#include "referencesystem.h"
#include "TimedCallback.h"
#include "DebugState.h"
#include "DynamicCache.h"
#include "fileLoader.h"
#include "MemoryPool.h"
#include "timing_profiler_interface.h"
#include "MemoryMonitor.h"
#include "ScratchStack.h"
#include "ConsoleDebug.h"
#include "EventTimingLog.h"
#include "ThreadManager.h"
#include "sysutil.h"
#include "StringCache.h"
#include "fileutil.h"
#include "fileCache.h"
#include "ssemath.h"
#include "ContinuousBuilderSupport.h"
#include "HttpClient.h"
#include "Prefs.h"
#include "gimmeDLLWrapper.h"
#include "FolderCache.h"
#include "logging.h"
#include "file.h"
#include "ResourceSearch.h"
#include "ResourceInfo.h"
#include "Organization.h"
#include "StatusReporting.h"
#include "CrypticPorts.h"
#include "zutils.h"
#include "objContainer.h"
#include "process_util.h"
#include "headshotUtils.h"
#include "UTF8.h"

#include "AutoGen/UtilitiesLibEnums_h_ast.c"
#include "headshotUtils_h_ast.h"

int giCurAutoRunStep = 0;
U32 giLongestFrameMsecs = 0;

TextureNameFixup g_texture_name_fixup = NULL;

bool gbCavemanMode = false;

void setCavemanMode(void)
{
	gbCavemanMode = true;
	gbSurpressStartupMessages = true;
	fileAllPathsAbsolute(true);
	FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
	fileDisableAutoDataDir();
	gimmeDLLDisable(true);
}


bool gbMemCorruptionFishing = false;
AUTO_CMD_INT(gbMemCorruptionFishing, MemCorruptionFishing) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

// If true, log frame performance information in LOG_FRAMEPERF.
static int sbLogFramePerf = -1;
AUTO_CMD_INT(sbLogFramePerf, LogFramePerf);

// Default value for sbLogFramePerf if not overridden.
static bool sbLogFramePerfDefault = false;

// Threshold for logging long frames.
static U32 suLongFrameThresholdMs = 1000;
AUTO_CMD_INT(suLongFrameThresholdMs, LongFrameThresholdMs);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

static S32 noSetErrorMode;
AUTO_CMD_INT(noSetErrorMode, noSetErrorMode) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

#define NUM_MEM_CORRUPTION_FISHING_BLOCKS 3

typedef struct
{
	U8 *pBuffer;
	int iSize;
} MemCorruptionFishingBlock;


static DWORD WINAPI MemCorruptionFishingThread( LPVOID lpParam )
{
	int i, j;

	MemCorruptionFishingBlock sFishingBlocks[NUM_MEM_CORRUPTION_FISHING_BLOCKS] = {0};


	while (1)
	{
		for (i=0; i < NUM_MEM_CORRUPTION_FISHING_BLOCKS; i++)
		{
			if (sFishingBlocks[i].pBuffer)
			{
				char *pWhatDid = NULL;

				for (j=0; j < sFishingBlocks[i].iSize; j++)
				{
					if(sFishingBlocks[i].pBuffer[j] != j % 256)
					{
						printf("POST-SLEEP Corruption at %p (%d/%d). Expected %d. Found %d.\n",
							&sFishingBlocks[i].pBuffer[j], j, sFishingBlocks[i].iSize, j % 256, sFishingBlocks[i].pBuffer[j]);
						sFishingBlocks[i].pBuffer[j] = j % 256;
					}
				}

				switch (randomIntRange(0,2))
				{
				case 0:
					memmove(sFishingBlocks[i].pBuffer, sFishingBlocks[i].pBuffer, sFishingBlocks[i].iSize);
					pWhatDid = "MEMMOVE";
					break;
				case 1:
					for (j=0; j < sFishingBlocks[i].iSize; j++)
					{
						sFishingBlocks[i].pBuffer[j]++;
					}
					for (j=0; j < sFishingBlocks[i].iSize; j++)
					{
						sFishingBlocks[i].pBuffer[j]--;
					}
					pWhatDid = "INCDEC";
					break;
				case 2:
					Sleep(0.1f);
					pWhatDid = "SLEEPAGAIN";

					break;
				}

				for (j=0; j < sFishingBlocks[i].iSize; j++)
				{
					if(sFishingBlocks[i].pBuffer[j] != j % 256)
					{
						printf("POST-%s Corruption at %p (%d/%d). Expected %d. Found %d.\n",
							pWhatDid,

							&sFishingBlocks[i].pBuffer[j], j, sFishingBlocks[i].iSize, j % 256, sFishingBlocks[i].pBuffer[j]);
						sFishingBlocks[i].pBuffer[j] = j % 256;
					}				}

//				free(sFishingBlocks[i].pBuffer);
			}

			else
			{

			
				sFishingBlocks[i].iSize = 8000000; //randomIntRange(100, 10000000);
				sFishingBlocks[i].pBuffer = malloc(sFishingBlocks[i].iSize);

				for (j=0; j < sFishingBlocks[i].iSize; j++)
				{
					sFishingBlocks[i].pBuffer[j] = j % 256;
				}
			}
		}

		Sleep(1);
	}
}









static bool bInit = false;
static bool g_bShouldQuit = false;
S64 gUtilitiesLibTicks = 1;

bool gbMakeBinsAndExit = false;
char *gpcMakeBinsAndExitNamespace = NULL;

char *gpcCalcDepsAndExit = NULL;
bool gbSurpressStartupMessages = false;

bool utilitiesLibShouldQuit(void)
{
	return g_bShouldQuit;
}

void utilitiesLibSetShouldQuit(bool bShouldQuit)
{
	g_bShouldQuit = bShouldQuit;
}

// Enable or disable FRAMEPERF FPS and long frame logging.
void utilitiesLibEnableFramePerfLogging(bool bEnable)
{
	sbLogFramePerfDefault = bEnable;
}

void fileTimedCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	PERFINFO_AUTO_START_FUNC();
	fileFreeOldZippedBuffers();
	PERFINFO_AUTO_STOP();
}

void initErrorAccessCriticalSection(void);

int g_mem_system_initted = 0;
void utilitiesLibPreAutoRunStuff(void)
{
	Earray_InitSystem();

	TextParserPreAutoRunInit();

	if (!g_mem_system_initted)
	{
        memCheckInit();
		ScratchStackInitSystem();
		initErrorAccessCriticalSection();
		g_mem_system_initted = 1;
		TimedCallback_Startup();
	}
}

bool utilitiesLibStartupEx(bool bLightWeight)
{
	if ( bInit )
		return false;
	bInit = true;

#if !PLATFORM_CONSOLE
	if(!noSetErrorMode){
		SetErrorMode(SEM_NOOPENFILEERRORBOX|SEM_FAILCRITICALERRORS);
	}
	timeBeginPeriod(1);
#endif
	isSSEavailable();
	timeSecondsSince2000EnableCache(1);
	initRand();
	initQuickTrig();
	RefSystem_Init();
#ifdef _FULLDEBUG
	//dynamicCacheDebugSetFailureRate(0.05);
#endif
	TimedCallback_Add(fileTimedCallback, NULL, 1.f);

	frameLockedTimerCreate(&ulFLT, 3000, 3000 / 60);
	
	if (!bLightWeight)
	{
		RefSystem_LoadStashTableSizesFromDataFiles();
	}

	mpCompactPools(); // To tell it that we *will* be compacting pools
	autoTimerInit();

	if (gbMemCorruptionFishing)
	{
		assert(tmCreateThread(MemCorruptionFishingThread, NULL));
		assert(tmCreateThread(MemCorruptionFishingThread, NULL));
	}

	return true;
}

bool utilitiesLibStartup(void)
{
	return utilitiesLibStartupEx(false);
}
bool utilitiesLibStartup_Lightweight(void)
{
	return utilitiesLibStartupEx(true);
}

// Track frame performance.
static void CalculateFrameRate(U32 iTimeLastTime, U32 iTimeThisTime)
{
	U32 frameTime = iTimeThisTime - iTimeLastTime;

	// Track longest frame.
	if (frameTime > giLongestFrameMsecs)
		giLongestFrameMsecs = frameTime;

	if (sbLogFramePerf > 0 || sbLogFramePerf == -1 && sbLogFramePerfDefault)
	{
		static U32 lastLogTime = 0;			// Last time FPS was logged
		static S64 lastTicks = 0;			// Ticks the last time FPS was logged

		// Log FPS periodically.
		if (iTimeThisTime > lastLogTime + 1000)
		{
			if (lastTicks)
				servLog(LOG_FRAMEPERF, "Fps", "fps %"FORM_LL"u", gUtilitiesLibTicks - lastTicks);
			lastTicks = gUtilitiesLibTicks;
			lastLogTime = iTimeThisTime;
		}

		// Log long frames.
		if (suLongFrameThresholdMs && frameTime > suLongFrameThresholdMs)
			servLog(LOG_FRAMEPERF, "LongFrame", "duration %lu", frameTime);
	}
}

void utilitiesLibUpdateAbsTime(F32 timeStepScale)
{
	U32 frameCpuTicks;
	frameLockedTimerStartNewFrame(ulFLT, timeStepScale);
	frameLockedTimerGetFrameTicks(ulFLT, &frameCpuTicks);
	ulAbsTime += MAX(frameCpuTicks, 1);
}

static ExtraTickFunction **sppExtraTickCBs = NULL;

void UtilitiesLib_AddExtraTickFunction(ExtraTickFunction *pCB)
{
	eaPushUnique((void***)&sppExtraTickCBs, pCB);
}

static void extraTicks(void)
{
	int i;
	for (i=0; i < eaSizeUnsafe(&sppExtraTickCBs); i++)
	{
		(sppExtraTickCBs[i])();
	}
}

void utilitiesLibOncePerFrame(F32 elapsed, F32 timeStepScale)
{
	static U32 iTimeLastTime = 0;
	U32 iTimeThisTime = 0;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("early", 1);

	iTimeThisTime = timeGetTime();

	//special case so that calling utilitiesLibOncePerFrame(REAL_TIME) will work 
	if (elapsed == -1.0f && timeStepScale == -1.0f)
	{
		if (iTimeLastTime)
		{
			elapsed = (iTimeThisTime - iTimeLastTime) / 1000.0f;
			timeStepScale = 1.0f;
		}
		else
		{
			elapsed = 0;
			timeStepScale = 0;
		}
	}

	DoConsoleDebugMenu(GetDefaultConsoleDebugMenu());
	memMonitorResetFrameCounters();
	ErrorOncePerFrame();
	TimedCallback_Tick(elapsed, timeStepScale);
	dbgOncePerFrame();
	dynamicCacheCheckAll(elapsed);
	PrefSetOncePerFrame();
	fileLoaderCheck();
	fileCachePrune();
	tagCacheClear();
	ParserValidateQueuedDicts();

	PERFINFO_AUTO_STOP_START("late", 1);

	mpCompactPools();
	ScratchStackOncePerFrame();
	utilitiesLibUpdateAbsTime(timeStepScale);
	etlSetFrameMarker();
	tmSetThreadNames();
	httpClientProcessTimeouts();
	zipTick();
	NonBlockingContainerIterators_Tick();

	timeSecondsSince2000Update();
	
	extraTicks();

	gUtilitiesLibTicks++;

	//once we've cycled through 60 times, start tracking frame performance
	if (gUtilitiesLibTicks > 60)
		CalculateFrameRate(iTimeLastTime, iTimeThisTime);
	iTimeLastTime = iTimeThisTime;

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();
}

int utilitiesLibLoadsPending(void)  // Counts loads form various generic systems
{
	return dynamicCacheLoadsPending() + fileLoaderLoadsPending() + hogTotalPendingOperationCount();
}


int gBuildVersion = 0;
char gBuildBranch[128] = "";

void SetBuildBranch(char *pBranchString)
{
	if (!pBranchString || !pBranchString[0])
		return;
	if (gBuildBranch[0] == 0)
	{
		strcpy(gBuildBranch, pBranchString);
	}
	else
	{
		if (strcmp(pBranchString, gBuildBranch) != 0)
		{
			int i = 0;

			while (pBranchString[i] == gBuildBranch[i])
			{
				i++;
			}

			assertmsgf(i > 0, "Incompatible build branches %s and %s", gBuildBranch, pBranchString);

			gBuildBranch[i] = 0;
		}
	}
}

AUTO_COMMAND ACMD_CATEGORY(debug);
char *SVNVersion(void)
{
	static char retBuf[128];

	sprintf(retBuf, "SVN Rev: %d (%s)", gBuildVersion, gBuildBranch);

	return retBuf;
}

extern char prodVersion[];

AUTO_COMMAND ACMD_CATEGORY(debug);
const char *ProdVersion(void)
{
	return prodVersion;
}


char *spShardInfoString = NULL;

AUTO_COMMAND ACMD_CATEGORY(debug);
char *GetShardInfoString(void)
{
	if (spShardInfoString && spShardInfoString[0])
	{
		return spShardInfoString;
	}

	return "none";
}

bool ShardInfoStringWasSet(void)
{
	return (spShardInfoString != NULL);
}

AUTO_COMMAND ACMD_CMDLINE;
void SetShardInfoString(ACMD_SENTENCE pShardInfoString)
{
	if (pShardInfoString && pShardInfoString[0])
	{
		estrCopy2(&spShardInfoString, pShardInfoString);
	}
	else
	{
		estrClear(&spShardInfoString);
	}
}



//on the controller itself, this function doesn't work, which is just plain confusing, so making it work
//via latelink
char *DEFAULT_LATELINK_GetShardNameFromShardInfoString(void)
{
	static char retBuffer[64] = "";

	sprintf(retBuffer, "%s", getComputerName());

	if (spShardInfoString && spShardInfoString[0])
	{
		char *pName = strstr(spShardInfoString, "name (");

		if (pName)
		{
			char *pCloseParens = strchr(pName, ')');
			
			if (pCloseParens)
			{
				strncpy(retBuffer, pName + 6, pCloseParens - pName - 6);
			}
		}
	}

	return retBuffer;
}

static char spShardCategory[64] = "";

//usually not used, because it gets set in the shard info string
AUTO_COMMAND ACMD_COMMANDLINE;
void SetShardCategoryManually(char *pCategory)
{
	strcpy(spShardCategory, pCategory);
}

char *GetShardCategoryFromShardInfoString(void)
{
	if (spShardCategory[0])
	{
		return spShardCategory;
	}

	sprintf(spShardCategory, "Dev");

	if (spShardInfoString && spShardInfoString[0])
	{
		char *pName = strstr(spShardInfoString, "category (");

		if (pName)
		{
			char *pCloseParens = strchr(pName, ')');
			
			if (pCloseParens)
			{
				strncpy(spShardCategory, pName + 10, pCloseParens - pName - 10);
			}
		}
	}
	return spShardCategory;
}

char *GetShardControllerTrackerFromShardInfoString(void)
{
	static char retBuffer[64] = "";

	if (retBuffer[0])
	{
		return retBuffer;
	}

	sprintf(retBuffer, "%s", StatusReporting_GetControllerTrackerName());

	if (spShardInfoString && spShardInfoString[0])
	{
		char *pName = strstr(spShardInfoString, "controllertracker (");

		if (pName)
		{
			char *pCloseParens = strchr(pName, ')');

			if (pCloseParens)
			{
				strncpy(retBuffer, pName + 19, pCloseParens - pName - 19);
			}
		}
	}
	return retBuffer;
}

const char *GetShardValueFromInfoStringByKey(SA_PARAM_NN_STR const char *shardInfo, SA_PARAM_NN_STR const char *key)
{
	static char retBuffer[64];
	char *pName;
	char *pCloseParens;
	int iParenCount = 0;

	sprintf(retBuffer, "%s (", key);
	pName = strstr(shardInfo, retBuffer);
	if (!pName)
		return NULL;
	pName += strlen(retBuffer);
	pCloseParens = pName;
	while (*pCloseParens)
	{
		if (*pCloseParens == '(')
			iParenCount++;
		else if (*pCloseParens == ')')
		{
			if (iParenCount == 0)
				break;
			else
				iParenCount--;
		}
		pCloseParens++;
	}
	if (!*pCloseParens)
		return NULL;
	strncpy(retBuffer, pName, pCloseParens - pName);
	return retBuffer;
}

const char *GetProductNameFromShardInfoString(SA_PARAM_NN_STR const char *shardInfo)
{
	static char retBuffer[64];
	const char *end = strstr(shardInfo, " shard");
	if (!end)
		return NULL;
	strncpy(retBuffer, shardInfo, end - shardInfo);
	return retBuffer;
}

//////////////////////////////////////////////////////////////////////////

static StashTable touched_bin_files;
static U32 touched_bin_timestamp;
static const char **untouched_files;

static CRITICAL_SECTION sTouchedOutputFileCS = {0};


void binNotifyTouchedOutputFile_Inner(const char *filename)
{
	if (!gbMakeBinsAndExit || !filename || !filename[0])
		return;

	ONCE(InitializeCriticalSection(&sTouchedOutputFileCS));

	EnterCriticalSection(&sTouchedOutputFileCS);

	if (!touched_bin_files)
	{
		touched_bin_timestamp = timeSecondsSince2000();
		touched_bin_files = stashTableCreateWithStringKeys(2048, StashDefault);
	}

	while (filename[0] == '/' || filename[0] == '\\')
		++filename;

	filename = allocAddFilename(filename);
	stashAddPointer(touched_bin_files, filename, filename, false);

	LeaveCriticalSection(&sTouchedOutputFileCS);

}

bool DEFAULT_LATELINK_binNotifyTouchedOutputFile_Multiplexed(const char *filename)
{
	return false;
}

void binNotifyTouchedOutputFile(const char *filename)
{
	if (binNotifyTouchedOutputFile_Multiplexed(filename))
	{
		return;
	}

	binNotifyTouchedOutputFile_Inner(filename);
}


AUTO_STRUCT;
typedef struct BinTouchedFileList
{
	U32 timestamp;
	const char **filenames;		AST( POOL_STRING FILENAME )
} BinTouchedFileList;

#include "utilitiesLib_c_ast.c"

#define GSL_TOUCHED_FILENAME "touched_bin_files.txt"

void binReadTouchedFileList(void)
{
	BinTouchedFileList file_list = {0};
	char filename[MAX_PATH];
	int i;

	if (!gbMakeBinsAndExit)
		return;

	sprintf(filename, "%s/%s", fileTempDir(), GSL_TOUCHED_FILENAME);
	if (ParserReadTextFile(filename, parse_BinTouchedFileList, &file_list, 0))
	{
		for (i = 0; i < eaSize(&file_list.filenames); ++i)
			binNotifyTouchedOutputFile_Inner(file_list.filenames[i]);
		touched_bin_timestamp = file_list.timestamp;

		StructReset(parse_BinTouchedFileList, &file_list);
	}
}

static void binWriteTouchedFileListEx(const char* txtFilename)
{
	char filename[MAX_PATH];

	sprintf(filename, "%s/%s", fileTempDir(), txtFilename);

	if (touched_bin_files)
	{
		BinTouchedFileList file_list = {0};
		StashTableIterator iter;
		StashElement elem;

		stashGetIterator(touched_bin_files, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			const char *binfile = stashElementGetStringKey(elem);
			if (binfile)
				eaPush(&file_list.filenames, binfile);
		}

		file_list.timestamp = touched_bin_timestamp;

		ParserWriteTextFile(filename, parse_BinTouchedFileList, &file_list, 0, 0);
		StructReset(parse_BinTouchedFileList, &file_list);
	}
	else
	{
		fileForceRemove(filename);
	}
}

void binWriteTouchedFileList(void)
{
	binWriteTouchedFileListEx(GSL_TOUCHED_FILENAME);
}

static FileScanAction findUntouchedFiles(char* dir, struct _finddata32_t* data, void *pUserData)
{
	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;

// 	if (!strstr(dir, "/_") && !(data->name[0] == '_'))
	{
		char fname[MAX_PATH];
		const char *filename;
		sprintf(fname,"%s/%s",dir,data->name);
		filename = allocAddFilename(fname);
		if (!touched_bin_files || !stashFindPointer(touched_bin_files, filename, NULL))
		{
			
			if(gpcMakeBinsAndExitNamespace)
			{
				char nameSpace[MAX_PATH], baseName[MAX_PATH];
				if(!resExtractNameSpace(fname, nameSpace, baseName) || stricmp(nameSpace, gpcMakeBinsAndExitNamespace))
					return FSA_EXPLORE_DIRECTORY;
			}
			eaPush(&untouched_files, filename);
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}

int binLeaveUntouchedFiles = 0;
AUTO_CMD_INT(binLeaveUntouchedFiles,binLeaveUntouchedFiles) ACMD_CMDLINE ACMD_CATEGORY(Debug);

void binDeleteUntouchedFiles(void)
{
	static const char *bin_dirs[3] = {"bin", "server/bin", "tempbin"};
	int i;

	if (binLeaveUntouchedFiles)
	{
		printf("Warning: Leaving untouched bin files behind because -binLeaveUntouchedFiles is set\n");
		assert(!g_isContinuousBuilder);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(bin_dirs); ++i)
		fileScanAllDataDirs(bin_dirs[i], findUntouchedFiles, NULL);

	for (i = 0; i < eaSize(&untouched_files); ++i)
	{
		char buf[MAX_PATH];
		U32 file_timestamp = fileLastChangedSS2000(untouched_files[i]);
		if (!strEndsWith(untouched_files[i], ".old"))
			assertmsgf(file_timestamp < touched_bin_timestamp, "Bin file %s did not get marked as touched during makeBinsAndExit but its timestamp is newer than when makeBinsAndExit started.  All bin files need to be written by either the game server, game client, or queue server.", untouched_files[i]);

		printf("DELETING %s\n", untouched_files[i]);

		// delete the file
		fileLocateWrite(untouched_files[i], buf);
		fileForceRemove(buf);

		// delete again to get the core version of the file
		fileLocateWrite(untouched_files[i], buf);
		fileForceRemove(buf);
	}

	stashTableDestroy(touched_bin_files);
	touched_bin_files = NULL;
	eaDestroy(&untouched_files);
}

ShellExecuteFunc g_shellExecuteFunc;
ShellExecuteWithCallbackFunc g_shellExecuteWithCallbackFunc;
void utilitiesLibSetShellExecuteFunc(ShellExecuteFunc shellExecuteFunc, ShellExecuteWithCallbackFunc shellExecuteWithCallback)
{
	g_shellExecuteFunc = shellExecuteFunc;
	g_shellExecuteWithCallbackFunc = shellExecuteWithCallback;
}

void ulShellExecute(HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd )
{
#if !PLATFORM_CONSOLE
	if (g_shellExecuteFunc && !isCrashed())
		g_shellExecuteFunc(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd);
	else
		ShellExecute_UTF8(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd);
#endif
}

void ulShellExecuteWithCallback(HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd, ShellExecuteCallback callback)
{
#if !PLATFORM_CONSOLE
	if (g_shellExecuteWithCallbackFunc)
		g_shellExecuteWithCallbackFunc(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd, callback);
	else
	{
		if (callback)
			callback((int)(intptr_t)ShellExecute_UTF8(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd), hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd);
		else
			ShellExecute_UTF8(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd);
	}
#endif
}


char *GetDirForBaseConfigFiles(void)
{
	static char *spDirName = NULL;

	if (!spDirName)
	{
		if (isProductionMode())
		{
			estrPrintf(&spDirName, "server/config");
		}
		else
		{
			char tempName[CRYPTIC_MAX_PATH];
			sprintf(tempName, "%s/server/config", fileDataDir());
			if (dirExists(tempName))
			{
				estrPrintf(&spDirName, "server/config");
			}
			else
			{
				estrPrintf(&spDirName, "server");
			}
		}
	}
		
	return spDirName;
}	

// Print a random string to the console.
static void spamPrintfs(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	char *output = NULL;
	unsigned i;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&output);
	estrForceSize(&output, randInt(100));
	for (i = 0; i != estrLength(&output); ++i)
		output[i] = 32 + randInt(126-32);
	printf("%s\n", output);
	estrDestroy(&output);
}

// Periodically print random strings to the console.
AUTO_COMMAND;
void BeginSpammingPrintfs()
{
	TimedCallback_Add(spamPrintfs, NULL, 0.1f);
}

typedef struct SimpleEventCounter
{
	int iNumToCount;
	int iWithinHowManySeconds;
	U32 iThrottleSeconds;
	U32 iLastTimeReturnedTrue;
	int iNextEventIndex;
	U32 *eventTimes; //really size iNumToCount
	char **eventInfo;
} SimpleEventCounter;

SimpleEventCounter *SimpleEventCounter_Create(int iNumEventsICareAbout, int iWithinHowManySeconds, U32 iThrottleSeconds)
{
	SimpleEventCounter *pCounter;
	assert(iNumEventsICareAbout);

	pCounter = callocStruct(SimpleEventCounter);
	pCounter->eventTimes = callocStructs(U32, iNumEventsICareAbout);
	pCounter->eventInfo = callocStructs(char*, iNumEventsICareAbout);

	pCounter->iNumToCount = iNumEventsICareAbout;
	pCounter->iThrottleSeconds = iThrottleSeconds;
	pCounter->iWithinHowManySeconds = iWithinHowManySeconds;

	return pCounter;
}


//returns true if the n-within-m condition was met (automatically does throttling)
bool SimpleEventCounter_ItHappenedWithInfo(SimpleEventCounter *pCounter, U32 iCurTime, const char* info, char** outStr)
{
	pCounter->eventTimes[pCounter->iNextEventIndex] = iCurTime;

	if (pCounter->eventInfo[pCounter->iNextEventIndex])
		SAFE_FREE(pCounter->eventInfo[pCounter->iNextEventIndex]);
	if (info)
		pCounter->eventInfo[pCounter->iNextEventIndex] = strdup(info);

	pCounter->iNextEventIndex = (pCounter->iNextEventIndex + 1) % pCounter->iNumToCount;
	if (pCounter->eventTimes[pCounter->iNextEventIndex] > iCurTime - pCounter->iWithinHowManySeconds)
	{
		if (pCounter->iThrottleSeconds && (iCurTime < pCounter->iLastTimeReturnedTrue + pCounter->iThrottleSeconds))
		{
			return false;
		}

		pCounter->iLastTimeReturnedTrue = iCurTime;
		if (outStr)
		{
			int i;

			for(i = 0; i < pCounter->iNumToCount; i++)
			{
				int index = (pCounter->iNextEventIndex + i) % pCounter->iNumToCount;
				estrConcatf(outStr, "%s\n", pCounter->eventInfo[index]);
			}
		}
		return true;
	}
	return false;
}

void SimpleEventCounter_Destroy(SimpleEventCounter **ppCounter)
{
	if (*ppCounter)
	{
		SimpleEventCounter *pCounter = *ppCounter;
		int i;

		SAFE_FREE(pCounter->eventTimes);
		for(i = 0; i < pCounter->iNumToCount; i++)
			SAFE_FREE(pCounter->eventInfo[i]);
		SAFE_FREE(pCounter->eventInfo);

		free(*ppCounter);
		*ppCounter = NULL;
	}
}

typedef struct SimpleEventThrottler
{
	int iNumEventsICareAbout;
	int iWithinHowManySeconds;
	int iTimeToKeepFailingAfterFirstFail;
	U32 iMostRecentFirstFailTime;
	int iNextIndex;
	U32 times[1];
} SimpleEventThrottler;
	


SimpleEventThrottler *SimpleEventThrottler_Create(int iNumEventsICareAbout, int iWithinHowManySeconds, int iTimeToKeepFailingAfterFirstFail)
{
	SimpleEventThrottler *pThrottler = calloc(sizeof(SimpleEventThrottler) + (iNumEventsICareAbout - 1) * sizeof(U32), 1);
	pThrottler->iNumEventsICareAbout = iNumEventsICareAbout;
	pThrottler->iWithinHowManySeconds = iWithinHowManySeconds;
	pThrottler->iTimeToKeepFailingAfterFirstFail = iTimeToKeepFailingAfterFirstFail;

	return pThrottler;
}

SimpleEventThrottlerResult SimpleEventThrottler_ItHappened(SimpleEventThrottler *pThrottler, U32 iCurTime)
{
	if (pThrottler->iMostRecentFirstFailTime && (pThrottler->iMostRecentFirstFailTime + pThrottler->iTimeToKeepFailingAfterFirstFail > iCurTime))
	{
		return SETR_FAIL;
	}

	if (pThrottler->iMostRecentFirstFailTime)
	{
		pThrottler->iMostRecentFirstFailTime = 0;
		memset(&pThrottler->times[0], 0, sizeof(U32) * pThrottler->iNumEventsICareAbout);
	}

	if (pThrottler->times[pThrottler->iNextIndex] > iCurTime - pThrottler->iWithinHowManySeconds)
	{
		pThrottler->iMostRecentFirstFailTime = iCurTime;
		return SETR_FIRSTFAIL;
	}

	pThrottler->times[pThrottler->iNextIndex] = iCurTime;
	pThrottler->iNextIndex++;
	pThrottler->iNextIndex %= pThrottler->iNumEventsICareAbout;

	return SETR_OK;
}





void SimpleEventThrottler_Destroy(SimpleEventThrottler **ppCounter)
{
	if (ppCounter && *ppCounter)
	{
		free(*ppCounter);
		*ppCounter = NULL;
	}
}



static int siSharedMachineIndex = -1;
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;
void SetSharedMachineIndex(int iVal)
{
}

int UtilitiesLib_GetSharedMachineIndex(void)
{
	if (siSharedMachineIndex == -1)
	{
		char temp[32];
		if (ParseCommandOutOfCommandLine("SetSharedMachineIndex", temp))
		{	
			siSharedMachineIndex = atoi(temp);
		}
		else
		{
			siSharedMachineIndex = 0;
		}
	}

	return siSharedMachineIndex;
}


void DEFAULT_LATELINK_XMLRPC_WriteSimpleStructResponse(char **ppOutString, void *pStruct, ParseTable *pTPI)
{
	assertmsgf(0, "Someone calling WriteSimpleStructResponse without httplib, that won't work");
}

static char *spOtherProcessExeName = NULL;
static int siOtherProcessPID = 0;

static DWORD WINAPI UtilitiesLib_KillMeWhenAnotherProcessDiesThread(LPVOID lpParam)
{
	while (1)
	{
		if (!processExists(spOtherProcessExeName, siOtherProcessPID))
		{
			exit(0);
		}

		Sleep(1000);
	}
}

	AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:UtilitiesLib_KillMeWhenAnotherProcessDiesThread", BUDGET_EngineMisc););


AUTO_COMMAND ACMD_NAME(KillMeWhenAnotherProcessDies);
void UtilitiesLib_KillMeWhenAnotherProcessDies(char *pExeName, int iOtherProcessPID)
{
	spOtherProcessExeName = strdup(pExeName);
	siOtherProcessPID = iOtherProcessPID;

	tmCreateThread(UtilitiesLib_KillMeWhenAnotherProcessDiesThread, NULL);
}

char *DEFAULT_LATELINK_ShardCommon_GetClusterName(void)
{
	return NULL;
}

void DEFAULT_LATELINK_OverlordWasJustSet(void)
{

}

static char *spOverlordName = NULL;

AUTO_COMMAND;
void SetOverlord(char *pOverlord)
{
	estrCopy2(&spOverlordName, pOverlord);

	if (spOverlordName  && !spOverlordName[0])
	{
		estrDestroy(&spOverlordName);
	}


	OverlordWasJustSet();
}

//a shard, or other server, can optionally have a single "overlord" machine that various parts of it will report to in 
//various ways
char *UtilitiesLib_GetOverlordName(void)
{
	return spOverlordName;
}


void DEFAULT_LATELINK_PCLStatusMonitoring_GetAllStatuses(PCLStatusMonitoringUpdate ***pppUpdates)
{
	ONCE(AssertOrAlert("NO_PCL_LINKED", "Someone trying to call PCLStatusMonitoring_GetAllStatuses without PatchClientLib linked in"));
}


//sticking a few random ast_h.c includes from .h files with no corresponding .c file here
#include "TransactionOutcomes.h"
#include "TransactionOutcomes_h_ast.c"
#include "headshotUtils_h_ast.c"