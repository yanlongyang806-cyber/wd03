/*
 * Launcher - starts, tracks, and stops processes (mapservers) for dbserver
 * 
 * Supported commands:
 * 
 * 	start process
 * 	get processes

connection plan:

	launcher starts up, looks for dbserver
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

#include "proclist.h"
#include "performance.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "logging.h"
#include "sock.h"
#include <stdio.h>
#include <conio.h>
#include <sys/types.h>
#include "MemoryMonitor.h"

#include "FolderCache.h"
#include "Launcher_enum.h"
#include "sysutil.h"
#include "version/AppRegCache.h"
#include "RegistryReader.h"
#include "winutil.h"
#include "serverlib.h"
#include "stashtable.h"
#include "utilitieslib.h"
#include "MemAlloc.h"
#include "fileutil.h"
#include "controllerLink.h"
#include "process_util.h"
#include "stringcache.h"
#include "GlobalTypes.h"
#include "fileutil2.h"
#include "structnet.h"
#include "osdependent.h"
#include "..\..\core\controller\pub\controllerPub.h"
#include "ControllerPub_h_ast.h"
#include "crypt.h"
#include "stringUtil.h"
#include "ThreadManager.h"
#include "logging.h"
#include "Launcher_c_ast.h"
#include "utils.h"
#include "Alerts.h"
#include "serverLib_h_ast.h"
#include "..\..\Utilities\MachineStatus\MachineStatusPub.h"
#include "MachineStatusPub_h_ast.h"
#include "Launcher_MachineStatus.h"
#include "PerfLogger.h"
#include "LogComm.h"
#include "Launcher.h"
#include "Launcher_Utils.h"
#include "systemspecs.h"
#include "SystemSpecs_h_ast.h"

#define COMMANDLINE_FILE_DIR "c:\\temp\\CmdLineFiles"

//command lines longer than this are put into a file instead
//
//Note that as of November 2012, Alex W tested this at 20000, and it seemed to work fine
//in XP, Win7 and Server 2003, but Stephen D and Aaron LaF were both uncomfortable changing it,
//so I'm putting it back to 1700
//
//And then Stephen clarified his position, so I'm putting it up to 7000
#define CMDLINE_CUTOFF_FOR_FILE 7000


QueryableProcessHandle *pDynHogFilePruningProc = NULL;

//if true, log every time you send a process packet to the controller
bool gbLogAllProcessSends = false;
AUTO_CMD_INT(gbLogAllProcessSends, LogAllProcessSends);

bool gbHide = false;
AUTO_CMD_INT(gbHide, hide);

bool gbSkipDeletingDynamicHogg = false;
AUTO_CMD_INT(gbSkipDeletingDynamicHogg, SkipDeletingDynamicHogg) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_LAUNCHER);
}

bool bMachineStatusExistsOrExisted = false;

bool gbPutShardNameInBginfo = false;
AUTO_CMD_INT(gbPutShardNameInBginfo, PutShardNameInBginfo);


//#define LAUNCHER_TRACKS_SHARED_HEAP

U32 giLastControllerTime = 0;

#define MAX_CLIENTS 50
NetListen *pLauncherListen;
static int local_launcher;
static int version_match=0;
static char dbserver_version[1024];
static int launcher_no_version_check=0;

static void launcherHandleMsg(Packet *pak,int cmd, NetLink *link,void *userdata);

const char **gppExesWhichMustShutThemselvesDown = NULL;

//if true, then delete all files before mirroring data dirs
bool gbDataMirroringDeletion = false;
AUTO_CMD_INT(gbDataMirroringDeletion, DataMirroringDeletion);

//if true, and if this is a 32 bit launcher, try to kill yourself and respawn 64-bit launcher.exe instead
bool gbRun64BitIfPossible = false;
AUTO_CMD_INT(gbRun64BitIfPossible, Run64BitIfPossible);

//if the launcher gets a report of controller FPS below this, do an xperf dump
static float sfControllerFPSWhichTriggersXperf = 100.0f;
AUTO_CMD_FLOAT(sfControllerFPSWhichTriggersXperf, ControllerFPSWhichTriggerXperf) ACMD_AUTO_SETTING(Misc, LAUNCHER);

//(seconds)if the round trip comm lag to the controller is slower than this, do an xperf dump
static int siControllerCommLagWhichTriggersXperf = 5;
AUTO_CMD_INT(siControllerCommLagWhichTriggersXperf, ControllerCommLagWhichTriggersXperf) ACMD_AUTO_SETTING(Misc, LAUNCHER);

//(seconds)if the clock on the controller machine and launcher machine are off by this many seconds, alert
static int siControllerTimeDifWhichTriggersAlert = 5;
AUTO_CMD_INT(siControllerTimeDifWhichTriggersAlert, ControllerTimeDifWhichTriggersAlert) ACMD_AUTO_SETTING(Misc, LAUNCHER);


bool gbXperfOnControllerStallsOrSlowdowns = false;
//set to true by the controller once startup is done
AUTO_CMD_INT(gbXperfOnControllerStallsOrSlowdowns, XperfOnControllerStallsOrSlowdowns);

// If set, use PerfLogger.
static bool sbUsePerfLogger = false;
AUTO_CMD_INT(sbUsePerfLogger, UsePerfLogger);


bool AttachDebugger(long iProcessID);

void smf_test();

StashTable htWindowsToHide;
StashTable htWindowsToShow;
StashTable htProcessesToAttach;

//for debug purposes, have a link that the local xbox client attached to me
NetLink *gpXboxClientLink = NULL;

//exe names to kill all of at shutdown
const char **ppExesToKillAllOfAtShutdown = NULL;

U32 *geaiCrypticErrorPIDs = NULL;

//every time we send the proc list to the controller, we push the time onto this list, every time we hear
//back we remove element zero. If element zero is every sufficiently long ago, something is wrong
U32 *spProcListSendTimes = NULL;

AUTO_COMMAND ACMD_CMDLINE;
void killAllExesOfThisTypeAtShutdown(char *pExeName)
{
	eaPushUnique(&ppExesToKillAllOfAtShutdown, allocAddString(pExeName));
}

AUTO_COMMAND ACMD_CMDLINE;
void exeMustShutItselfDown(char *pExeName)
{
	if (strEndsWith(pExeName, ".exe"))
	{
		pExeName[strlen(pExeName)-4] = 0;
	}

	eaPushUnique(&gppExesWhichMustShutThemselvesDown, allocAddString(pExeName));
	
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void SetDirToLaunchFrom(char *pDir)
{
	assert(chdir(pDir) == 0);
}

void OVERRIDE_LATELINK_ControllerLink_ProcessTimeDifference(int iTimeDiff)
{
	if (siControllerTimeDifWhichTriggersAlert && iTimeDiff > siControllerTimeDifWhichTriggersAlert)
	{
		WARNING_NETOPS_ALERT("CLOCK_DIFF", "The clock on the controller machine and machine %s appear to differ by %d seconds, more than %d seconds (this alert could be a false alarm if there was a bunch of lagging or debugging going on)",
			getHostName(),  iTimeDiff, siControllerTimeDifWhichTriggersAlert);
	}
}

static void handleGameVersion(Packet *pak)
{
	version_match = pktGetBits(pak, 1);
	pktGetString(pak,dbserver_version,sizeof(dbserver_version));
}

int connectDb()
{
	int		ret = 1;
/*	static int delay_init = 1000;
	static int delay = 1000;
	static int delay_add = 1000;
	loadstart_printf("Connecting to %s...",server_cfg.db_server);
#undef fflush
	fflush(stdout);
	ret = netConnect(&db_link,server_cfg.db_server,DEFAULT_DBLAUNCHER_PORT,NLT_TCP,NO_TIMEOUT,NULL);
	if (ret && local_launcher)
	{
		Packet	*pak;

		pak = pktCreate(&db_link, LAUNCHERANSWER_LOCALONLY);
		pktSend(&pak,&db_link);
	}
	if (ret) {
		Packet	*pak;

		version_match = 0;
		pak = pktCreate(&db_link, LAUNCHERANSWER_GAMEVERSION);
		pktSendString(pak, getCompatibleGameVersion());
		pktSendBitsPack(pak, 1, getNumRealCpus());
		pktSend(&pak,&db_link);

		ret = netLinkMonitorBlock(&db_link, LAUNCHERQUERY_GAMEVERSION, launcherHandleMsg, 1000.0f);
		if (ret) {
			if (version_match || launcher_no_version_check) {
				loadend_printf("connected");
				delay = delay_init;
			} else {
				loadend_printf("wrong version");
				printf("  Local version: %s\n  DbServer version: %s\n", getCompatibleGameVersion(), dbserver_version);
				clearNetLink(&db_link);
				loadstart_printf("Waiting and then trying again...");
				Sleep(delay);
				delay += delay_add; // Make sure we don't spam the system with connections when we already know we have the wrong version
				loadend_printf("");
			}
		} else {
			loadend_printf("failed to receive version info from DbServer");
			clearNetLink(&db_link);
		}
	} else {
		loadend_printf("failed");
	}
	perfSendReset();
*/
	return ret;
}

static void sendProcesses(NetLink *link)
{
	Packet	*pak;
	static U32 sUID = 0;

	PERFINFO_AUTO_START_FUNC();
	
	pktCreateWithCachedTracker(pak, link,LAUNCHERANSWER_PROCESSES);
	procSendTrackedInfo(pak);
	perfSendTrackedInfo(pak);
	
	pktSendBits(pak, 32, ++sUID);

	if (gbLogAllProcessSends)
	{
		objLog(LOG_LAUNCHER, 0, 0, 0, NULL, 0, NULL, "SentProcInfo", NULL, "UID %u", sUID);
	}

	pktSend(&pak);

	if (gbXperfOnControllerStallsOrSlowdowns && ea32Size(&spProcListSendTimes)
		&& spProcListSendTimes[0] < timeSecondsSince2000() - siControllerCommLagWhichTriggersXperf)
	{
		THROTTLE(120, 
			CRITICAL_NETOPS_ALERT("LAUNCHER_CONTROLLER_COMM_LAG", 
				"Round trip comm time launcher to controller was slower than %d seconds, did xperf", siControllerCommLagWhichTriggersXperf); 
		xperfDump("ControllerCommLag"););
	}
	ea32Push(&spProcListSendTimes, timeSecondsSince2000());
	PERFINFO_AUTO_STOP();
}

char *ExtractShortExeName(char *pExeName_In)
{
	static char *spRetVal = NULL;
	char *pFoundExe;
	estrCopy2(&spRetVal, pExeName_In);

	pFoundExe = strstri(spRetVal, ".exe");
	if (pFoundExe)
	{
		estrSetSize(&spRetVal, pFoundExe - spRetVal);
	}
	else
	{
		//should be impossible... but try to truncate the extension in any case
		estrTruncateAtFirstOccurrence(&spRetVal, '.');
	}
	estrRemoveUpToLastOccurrence(&spRetVal, '/');
	estrRemoveUpToLastOccurrence(&spRetVal, '\\');
	estrRemoveUpToLastOccurrence(&spRetVal, '"');
	return spRetVal;
}

void StartProcessFromRequest(ServerLaunchRequest *pRequest)
{
	U32		pid=0;
	char *pFullCommandLine = NULL;
	Packet *pOutPack;
	char transCommandLineSnippet[128];
	char logServerCommandLineSnippet[128];
	char *pTemp;
	static char *pShortExeName = NULL;


	transCommandLineSnippet[0] = 0;
	logServerCommandLineSnippet[0] = 0;

	if (pRequest->iTransactionServerIPToSpecify)
	{
		sprintf(transCommandLineSnippet, " -TransactionServer %s", makeIpStr(pRequest->iTransactionServerIPToSpecify));
	}
	
	if (pRequest->iLogServerIPToSpecify)
	{
		sprintf(logServerCommandLineSnippet, " -LogServer %s", makeIpStr(pRequest->iLogServerIPToSpecify));
	}
	

	log_printf(LOG_LOGIN,"launcher starting: %s\n", pRequest->pExeName);

	//make sure that the command name itself is all backslashes (bear in mind that commandName can actually include
	//more than just the .exe name... appservers have the -globalType in there
	pTemp = pRequest->pExeName;
	while (IS_WHITESPACE(*pTemp))
	{
		pTemp++;
	}
	while (*pTemp && !IS_WHITESPACE(*pTemp))
	{
		if (*pTemp == '/')
		{
			*pTemp = '\\';
		}
		pTemp++;
	}

	estrCopy2(&pShortExeName, ExtractShortExeName(pRequest->pExeName));

	if (pRequest->eContainerType == GLOBALTYPE_CLIENT)
	{
		// special case client stuff
		estrPrintf(&pFullCommandLine, "-DebugContainerID %d %s %s ",
			pRequest->iContainerID, pRequest->bStartInDebugger ? "-WaitForDebugger":"", pRequest->pCommandLine);


			
	}
	else
	{
		estrPrintf(&pFullCommandLine, "-ContainerID %d -Cookie %u %s -controllerHost %s %s %s -SetProductName %s %s %s",
			pRequest->iContainerID, pRequest->iAntiZombificationCookie,
			pRequest->bStartInDebugger ? "-WaitForDebugger":"",
			gServerLibState.controllerHost, transCommandLineSnippet, logServerCommandLineSnippet, GetProductName(), GetShortProductName(), pRequest->pCommandLine);

		
	}

	if (UtilitiesLib_GetSharedMachineIndex())
	{
		estrConcatf(&pFullCommandLine, " -SetSharedMachineIndex %d ", UtilitiesLib_GetSharedMachineIndex());
	}

	if (estrLength(&pFullCommandLine) > CMDLINE_CUTOFF_FOR_FILE)
	{
		char fileName[CRYPTIC_MAX_PATH];
		FILE *pFile;
		
		COARSE_AUTO_START("MakeCmdLineFile");
			
		sprintf(fileName, "%s\\%s_%u_%u.txt", COMMANDLINE_FILE_DIR, GlobalTypeToName(pRequest->eContainerType), pRequest->iContainerID, timeSecondsSince2000());

		mkdirtree_const(fileName);
		pFile = fopen(fileName, "wt");
		if (!pFile)
		{
			AssertOrAlert("CANT_MAKE_CMDLINE_FILE", "Launcher was trying to open file %s to write extra-long command line into it, couldn't do so",
				fileName);
			estrInsertf(&pFullCommandLine, 0, "%s ", pRequest->pExeName);
		}
		else
		{
			fprintf(pFile, "%s", pFullCommandLine);
			fclose(pFile);

			estrPrintf(&pFullCommandLine, "%s -CommandLineFile \"%s THE REST OF THIS COMMAND LINE WAS ACTUALLY LOADED FROM THIS FILE\"", pRequest->pExeName, fileName);
		}

		COARSE_AUTO_STOP("MakeCmdLineFile");

	}
	else
	{
		estrInsertf(&pFullCommandLine, 0, "%s ", pRequest->pExeName);
	}



	if (pRequest->pWorkingDirectory && pRequest->pWorkingDirectory[0])
	{
		char cwd[MAX_PATH];
		fileGetcwd(cwd, ARRAY_SIZE_CHECKED(cwd)-1);
		assert(chdir(pRequest->pWorkingDirectory) == 0);
		COARSE_WRAP(pid = system_detach_with_fulldebug_fixup(pFullCommandLine, 1, pRequest->bStartHidden));
		assert(chdir(cwd) == 0);
	}
	else
	{
		COARSE_WRAP(pid = system_detach_with_fulldebug_fixup(pFullCommandLine, 1, pRequest->bStartHidden));
	}
	
	printf("Executing: %s\n", pFullCommandLine);

	if (!pid)
	{
		char errorBuf[1000];
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, errorBuf, 1000, NULL);

		AssertOrAlert("LAUNCH_FAILURE", "Couldn't launch <<%s>> : error <<%s>>", pFullCommandLine, errorBuf);
		return;
	}

	if (!trackProcessByKnownProcessID(pRequest->eContainerType, pRequest->iContainerID, pid, pRequest->iLowLevelControllerIndex, pShortExeName))
	{
		AssertOrAlert("TRACK_FAILURE", "Just launched %s(%d), couldn't find it while tracking processes\n", GlobalTypeToName(pRequest->eContainerType),
			pRequest->iContainerID);
		return;
	}

	if (pRequest->bStartInDebugger)
	{
		stashIntAddInt(htProcessesToAttach, pid, 0, false);
	}

	if (pRequest->bStartHidden)
	{
		stashIntAddInt(htWindowsToHide, pid, 0, false);
	}


	pktCreateWithCachedTracker(pOutPack, gpControllerLink, TO_CONTROLLER_PID_OF_NEW_SERVER);
	PutContainerTypeIntoPacket(pOutPack, pRequest->eContainerType);
	PutContainerIDIntoPacket(pOutPack, pRequest->iContainerID);
	pktSendBits(pOutPack, 32, pid);
	pktSend(&pOutPack);
	

	estrDestroy(&pFullCommandLine);

}

static void handleStartProcess(Packet *pak)
{
	ServerLaunchRequest *pRequest = StructCreate(parse_ServerLaunchRequest);


	ParserRecv(parse_ServerLaunchRequest, pak, pRequest, 0);
	
	assertmsgf(pRequest->iAntiZombificationCookie == gServerLibState.antiZombificationCookie, "Controller wants launcher to start a server with wrong anti-zombification cookie");
	

	if (FileExistsAndMatchesCRC(pRequest->pExeName, pRequest->iExecutableCRC))
	{
		StartProcessFromRequest(pRequest);
		StructDestroy(parse_ServerLaunchRequest, pRequest);
	}
	else
	{
		CachedControllerFileRequest *pControllerFileRequest = GetControllerFileRequest(pRequest->pExeName, pRequest->iExecutableCRC);

		if (pControllerFileRequest)
		{
			eaPush(&pControllerFileRequest->ppPendingLaunchRequests, pRequest);
		}
		else
		{
			StructDestroy(parse_ServerLaunchRequest, pRequest);
		}
	}
}




static void handleKillProcess(Packet *pak)
{
	int		iContainerID;
	GlobalType		iContainerType;


	iContainerID = GetContainerIDFromPacket(pak);
	iContainerType = GetContainerTypeFromPacket(pak);

	printf("Killing %s %u\n", GlobalTypeToName(iContainerType), iContainerID);


	if (iContainerID)
	{
		KillProcessFromTypeAndID(iContainerID, iContainerType);
	}
}

static void handleIgnoreProcess(Packet *pak)
{
	int		iContainerID;
	GlobalType		iContainerType;
	int i;
	int iSecsToRemainIgnored;

	iContainerID = GetContainerIDFromPacket(pak);
	iContainerType = GetContainerTypeFromPacket(pak);
	iSecsToRemainIgnored = pktGetBits(pak, 32);

	if (PidOfProcessBeingIgnored())
	{
		printf("Asked to ignore %s %u, already ignoring something, killing it instead",
			GlobalTypeToName(iContainerType), iContainerID);
		KillProcessFromTypeAndID(iContainerID, iContainerType);
		return;
	}
	else
	{

		printf("Ignoring %s %u\n", GlobalTypeToName(iContainerType), iContainerID);


		for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
		{
			if (process_list.ppProcessInfos[i]->container_id == iContainerID && process_list.ppProcessInfos[i]->container_type == iContainerType)
			{
				BeginIgnoringProcess(process_list.ppProcessInfos[i], iSecsToRemainIgnored);
//				process_list.ppProcessInfos[i]->container_id = -1;
//				process_list.ppProcessInfos[i]->container_type = 0;
				return;
			}
		}
	}
}


static void handleHideProcess(Packet *pak)
{
	int		iContainerID;
	int		iContainerType;
	bool bHide;

	iContainerID = GetContainerIDFromPacket(pak);
	iContainerType = GetContainerTypeFromPacket(pak);
	bHide = pktGetBits(pak, 1);

	if (iContainerType == GLOBALTYPE_LAUNCHER)
	{
		if (bHide)
		{
			hideConsoleWindow();
		}
		else
		{
			showConsoleWindow();
		}

		return;
	}

	HideProcessFromTypeAndID(iContainerID, iContainerType, bHide);
}



static void handleProcessRegister(Packet *pak)
{
	int iContainerID, iContainerType, iProcessID, iLowLevelControllerIndex;
	char *pRawExeName;
	char *pShortExeName = NULL;

	iContainerID = GetContainerIDFromPacket(pak);
	iContainerType = GetContainerTypeFromPacket(pak);
	iLowLevelControllerIndex = pktGetBits(pak, 32);

	iProcessID = pktGetBitsPack(pak,1);
	pRawExeName = pktGetStringTemp(pak);
	pShortExeName = strdup(ExtractShortExeName(pRawExeName));

	trackProcessByKnownProcessID(iContainerType, iContainerID, iProcessID, iLowLevelControllerIndex, pShortExeName);

	free(pShortExeName);

	printf("trying to register process type %d id %d pid %d\n",
		iContainerType, iContainerID, iProcessID);
}


static void handleMCPDying(void)
{
	int i;
	U32 iStartTime = timeSecondsSince2000_ForceRecalc();
	static bool bIn = false;

	if (bIn)
	{
		return;
	}

	bIn = true;

	printf("Controller has told us to die... waiting 5 seconds, plus for all crypticerrors to go away\n");

	while (timeSecondsSince2000_ForceRecalc() < iStartTime + 5)
	{
		Sleep(10);
		utilitiesLibOncePerFrame(REAL_TIME);
		commMonitor(commDefault());
		
		serverLibOncePerFrame();
	}

	printf("Killing still-running exes that are not being handled by CrypticError\n");
	KillAllProcessesWithTypeAndID(gppExesWhichMustShutThemselvesDown, geaiCrypticErrorPIDs);

	//also kill all exes that the controller told us to kill on shutdown
	for (i=0; i < eaSize(&ppExesToKillAllOfAtShutdown); i++)
	{
		KillAllEx(ppExesToKillAllOfAtShutdown[i], false, geaiCrypticErrorPIDs, true, true, NULL);
	}

	if (ea32Size(&geaiCrypticErrorPIDs))
	{
		"killed everything we could... but still waiting for cryptic error(s)\n";
		while (ea32Size(&geaiCrypticErrorPIDs))
		{
			Sleep(10);
			utilitiesLibOncePerFrame(REAL_TIME);
			commMonitor(commDefault());
			
			serverLibOncePerFrame();
		}
	}
	exit(0);
}

static void handleRelayCommandToXboxClient(Packet *pPak)
{
	if (gpXboxClientLink)
	{
		Packet *pOutPack = pktCreate(gpXboxClientLink, FROM_LAUNCHERDEBUG_COMMAND);
		pktSendString(pOutPack, pktGetStringTemp(pPak));
		pktSend(&pOutPack);
	}
}

static bool DontDeleteCallBack(const char *pFileName)
{
	if (strStartsWith(pFileName, "ns/"))
	{
		return true;
	}

	return false;
}

static void handleSetDirectoryContents(Packet *pPak)
{
	char *pDirName;
	TPFileList *pNewFileList;
	TPFileList *pCurrentFileList;
	char *pReport = NULL;
	Packet *pOutPak;

	pDirName = pktGetStringTemp(pPak);
	pNewFileList = StructCreate(parse_TPFileList);

	ParserRecv(parse_TPFileList, pPak, pNewFileList, 0);

	pCurrentFileList = TPFileList_ReadDirectory(pDirName);

	TPFileList_CompareAndGenerateReport(pCurrentFileList, pNewFileList, &pReport, DontDeleteCallBack);

	pOutPak = pktCreate(GetControllerLink(), TO_CONTROLLER_HERE_IS_DATA_MIRRORING_REPORT);
	pktSendString(pOutPak, pReport);
	pktSend(&pOutPak);

	estrDestroy(&pReport);
	StructDestroy(parse_TPFileList, pCurrentFileList);


	if (gbDataMirroringDeletion)
	{
		char fullPath[CRYPTIC_MAX_PATH];
		char systemString[1024];

		makefullpath(pDirName, fullPath);

		backSlashes(fullPath);

		if (dirExists(fullPath))
		{
			sprintf(systemString, "erase /f /s /q %s\\*.*", fullPath);

			
			if (system_w_timeout(systemString, NULL, 600) == -1)
			{
				assertmsgf(0, "failed to remove all files in %s during data mirroring", fullPath);
			}
		}
	}
	else
	{
		rmdirtree(pDirName);
	}

	TPFileList_WriteDirectory(pDirName, pNewFileList);
	StructDestroy(parse_TPFileList, pNewFileList);
}

static void handleRequestManualDump(Packet *pak)
{
	U32 iPid = pktGetBits(pak, 32);
	char *pString = pktGetStringTemp(pak);
	bool bForce32Bit = pktGetBits(pak,1);

	char *pSystemString = NULL;

	estrPrintf(&pSystemString, "c:\\cryptic\\tools\\bin\\CrypticError%s.exe -ForceAutoClose -pid %u -manualDump \"%s\"",
		(IsUsingX64() && !bForce32Bit) ? "X64" : "", iPid, pString);

	printf("Requesting manual dump: %s\n", pSystemString);

	system_detach(pSystemString, false, false);
			
	estrDestroy(&pSystemString);

}		

static void handleLocalEXECrcs(NetLink *link, Packet *pPak)
{
	LocalExecutableWithCRCList *pList = StructCreate(parse_LocalExecutableWithCRCList);
	int i;
	U32 iCRC;
	Packet *pOutPack;

	ParserRecv(parse_LocalExecutableWithCRCList, pPak, pList, 0);
	pOutPack = pktCreate(link, TO_CONTROLLER_LAUNCHER_REQUESTS_LOCAL_EXES_FOR_MIRRORING);
	PutContainerIDIntoPacket(pOutPack, GetAppGlobalID());

	for (i=0; i < eaSize(&pList->ppList); i++)
	{
		iCRC = cryptAdlerFile(pList->ppList[i]->name);
		if (iCRC && pList->ppList[i]->iCRC && iCRC != pList->ppList[i]->iCRC)
		{
			pktSendString(pOutPack, pList->ppList[i]->name);
		}
	}

	pktSendString(pOutPack, "");
	pktSend(&pOutPack);
}

static void handleHereAreEXES(NetLink *pLink, Packet *pPak)
{
	char *pFileName;
	int iSize;
	void *pData;
	Packet *pOutPack;

	while (1)
	{
		pFileName = pktGetStringTemp(pPak);
		if (!pFileName[0])
		{
			pOutPack = pktCreate(pLink, TO_CONTROLLER_LAUNCHER_GOT_LOCAL_EXES_FOR_MIRRORING);
			pktSend(&pOutPack);
			return;
		}

		printf("Receiving file %s for mirroring from controller\n", pFileName);

		iSize = pktGetBits(pPak, 32);
		if (iSize)
		{
			FILE *pFile;
			pData = malloc(iSize);
			pktGetBytes(pPak, iSize, pData);

			pFile = fopen(pFileName, "wb");
			if (!pFile)
			{
				ErrorOrAlert("FILE_MIRROR_FAIL", "Couldn't open file %s for mirroring from controller", pFileName);
			}
			else
			{
				fwrite(pData, iSize, 1, pFile);
				fclose(pFile);
			}

			free(pData);
		}
	}
}

void handleControllerTime(Packet *pPak)
{
	giLastControllerTime = pktGetBits(pPak, 32);
	if (ea32Size(&spProcListSendTimes))
	{
		ea32Remove(&spProcListSendTimes, 0);
	}
}

void handleSetVerboseProcListLogging(Packet *pPak)
{
	gbLogAllProcessSends = pktGetBits(pPak, 1);
}

AUTO_STRUCT;
typedef struct GettingFilesStatus
{
	char **ppFileNames;
	int *piFileSizes;
	int iBytesOfCurFileReceived;
	int iBytesOfCurFileRequested;
	FILE *pCurFile; NO_AST
	char *pDoneHandshakeCmd;
} GettingFilesStatus;

GettingFilesStatus **ppActiveGettingFilesStatuses = NULL;

void handleBeginGettingFiles(Packet *pPak)
{
	char *pFileName;

	GettingFilesStatus *pStatus = StructCreate(parse_GettingFilesStatus);

	while (1)
	{
		pFileName = pktGetStringTemp(pPak);
		if (!pFileName[0])
		{
			break;
		}

		eaPush(&pStatus->ppFileNames, strdup(pFileName));
		ea32Push(&pStatus->piFileSizes, pktGetBits(pPak, 32));
	}

	pStatus->pDoneHandshakeCmd = strdup(pktGetStringTemp(pPak));

	eaPush(&ppActiveGettingFilesStatuses, pStatus);

}

#define GETTING_FILE_CHUNK_SIZE (512 * 1024)

void launcherUpdateGettingFiles(void)
{
	Packet *pPak;
	GettingFilesStatus *pStatus;
	int iBytesToRequest;

	if (!eaSize(&ppActiveGettingFilesStatuses))
	{
		return;
	}

	if (!gpControllerLink)
	{
		return;
	}

	pStatus = ppActiveGettingFilesStatuses[0];
	if (pStatus->iBytesOfCurFileRequested == pStatus->iBytesOfCurFileReceived)
	{
		if (pStatus->iBytesOfCurFileReceived == pStatus->piFileSizes[0])
		{
			//finished receiving this file... close it down
			printf("Finished receiving %s\n", pStatus->ppFileNames[0]);
			fclose(pStatus->pCurFile);
			pStatus->pCurFile = NULL;
			ea32Remove(&pStatus->piFileSizes, 0);
			free(pStatus->ppFileNames[0]);
			eaRemove(&pStatus->ppFileNames, 0);
			pStatus->iBytesOfCurFileReceived = 0;
			pStatus->iBytesOfCurFileRequested = 0;

			if (!eaSize(&pStatus->ppFileNames))
			{
				if (pStatus->pDoneHandshakeCmd && pStatus->pDoneHandshakeCmd[0])
				{
					pPak = pktCreate(gpControllerLink, TO_CONTROLLER_EXECUTE_COMMAND_STRING);
					pktSendString(pPak, pStatus->pDoneHandshakeCmd);
					pktSend(&pPak);
				}

				StructDestroy(parse_GettingFilesStatus, pStatus);
				eaRemove(&ppActiveGettingFilesStatuses, 0);
			}
				return;
		}

		iBytesToRequest = GETTING_FILE_CHUNK_SIZE;

		if (pStatus->iBytesOfCurFileReceived + iBytesToRequest > pStatus->piFileSizes[0])
		{
			iBytesToRequest = pStatus->piFileSizes[0] - pStatus->iBytesOfCurFileReceived;
		}

		printf("About to request bytes %d through %d from %s\n",
			pStatus->iBytesOfCurFileReceived, pStatus->iBytesOfCurFileReceived + iBytesToRequest, pStatus->ppFileNames[0]);

		pPak = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_WANTS_BYTES_FROM_FILE);

		pktSendString(pPak, pStatus->ppFileNames[0]);
		pktSendBits(pPak, 32, pStatus->iBytesOfCurFileReceived);
		pktSendBits(pPak, 32, iBytesToRequest);
		pktSend(&pPak);

		pStatus->iBytesOfCurFileRequested += iBytesToRequest;
	}
}

void handleHereAreRequestedBytesFromFile(Packet *pak)
{
	GettingFilesStatus *pStatus;

	if (!eaSize(&ppActiveGettingFilesStatuses) || ppActiveGettingFilesStatuses[0]->iBytesOfCurFileReceived == ppActiveGettingFilesStatuses[0]->iBytesOfCurFileRequested)
	{
		AssertOrAlert("LAUNCHER_GOT_BAD_BYTES", "Launcher received bytes for a file it didn't request");
		return;
	}

	pStatus = ppActiveGettingFilesStatuses[0];

	if (!pStatus->pCurFile)
	{

		printf("Opening file %s for getting bytes\n", pStatus->ppFileNames[0]);

		pStatus->pCurFile = fopen(pStatus->ppFileNames[0], "wb");
		if (!pStatus->pCurFile)
		{
			AssertOrAlert("LAUNCHER_FILE_CREATE_FAILED", "Launcher couldn't create file %s to get bytes into", pStatus->ppFileNames[0]);
			return;
		}
	}
	fwrite(pktGetBytesTemp(pak, pStatus->iBytesOfCurFileRequested - pStatus->iBytesOfCurFileReceived), pStatus->iBytesOfCurFileRequested - pStatus->iBytesOfCurFileReceived, 1, pStatus->pCurFile);
	pStatus->iBytesOfCurFileReceived = pStatus->iBytesOfCurFileRequested;
}		

void handleKillAllByName(Packet *pPak)
{
	killall(pktGetStringTemp(pPak));
}

void FailDynHogPruning(char *pErrorString_in, ...)
{
	char *pErrorString = NULL;
	Packet *pPak;

	estrGetVarArgs(&pErrorString, pErrorString_in);

	pPak = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_REPORTING_DYN_HOG_PRUNING_FAILURE);
	pktSendString(pPak, pErrorString);
	pktSend(&pPak);


	estrDestroy(&pErrorString);
}

static char dynHogPruningOuptFileName[CRYPTIC_MAX_PATH];
static U32 iPruningStartTime = 0;

void handleRequestDynHogPruning(Packet *pPak)
{
	char *pTemp;
	char **ppNameSpacesNotToPrune = NULL;
	char tempFileName[CRYPTIC_MAX_PATH];
	FILE *pFile;
	char *pCmdString = NULL;
	int i;
	char cwd[CRYPTIC_MAX_PATH];

	fileGetcwd(SAFESTR(cwd));

	if (pDynHogFilePruningProc)
	{
		FailDynHogPruning("Dynamic hogg pruning already ongoing");
		return;
	}

	while (1)
	{
		pTemp = pktGetStringTemp(pPak);
		if (!pTemp[0])
		{
			break;
		}

		eaPush(&ppNameSpacesNotToPrune, pTemp);
	}

	sprintf(tempFileName, "%s\\temp\\hogFilePruningCmdLine.txt", cwd);
	sprintf(dynHogPruningOuptFileName, "%s\\temp\\hogFilePruning_Output.txt", cwd);

	mkdirtree_const(tempFileName);
	pFile = fopen(tempFileName, "wt");
	if (!pFile)
	{
		FailDynHogPruning("Couldn't open %s", tempFileName);
		eaDestroy(&ppNameSpacesNotToPrune);
		return;
	}
	
	for (i=0 ; i < eaSize(&ppNameSpacesNotToPrune); i++)
	{
		fprintf(pFile, "ns/%s/*\n", ppNameSpacesNotToPrune[i]);
	}

	fclose(pFile);

	estrPrintf(&pCmdString, "pig.exe dif %s\\piggs\\dynamic.hogg -T%s", cwd, tempFileName);

	pDynHogFilePruningProc = StartQueryableProcess(pCmdString, NULL,false, false, false, dynHogPruningOuptFileName);
	iPruningStartTime = timeSecondsSince2000();

	if (!pDynHogFilePruningProc)
	{
		FailDynHogPruning("Couldn't launch hog file pruning. Command line: %s", pCmdString);
	}
	else
	{
		Packet *pOutPak = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_REPORTING_DYN_HOG_PRUNING_COMMENT);
		pktSendStringf(pOutPak, "Dynamic hog file pruning started with command line \"%s\". Results will go into %s", pCmdString, dynHogPruningOuptFileName);
		pktSend(&pOutPak);
	}

	estrDestroy(&pCmdString);
	eaDestroy(&ppNameSpacesNotToPrune);
}

void UpdateDynHogFilePruning(void)
{
	int iRetVal = 0;
	if (QueryableProcessComplete(&pDynHogFilePruningProc, &iRetVal))
	{
		if (iRetVal == 0)
		{
			Packet *pOutPak = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_REPORTING_DYN_HOG_PRUNING_SUCCESS);
			pktSend(&pOutPak);
		}
		else
		{
			char *pErrorString = fileAlloc(dynHogPruningOuptFileName, NULL);
			char *pDurationString = NULL;
			if (!pErrorString)
			{
				pErrorString = strdupf("(Unable to load output file %s", dynHogPruningOuptFileName);
			}

			timeSecondsDurationToPrettyEString(timeSecondsSince2000() - iPruningStartTime, &pDurationString);
			FailDynHogPruning("pig.exe failed with return code %d in %s. Console output: %s",
				iRetVal, pDurationString, pErrorString);
			
			estrDestroy(&pDurationString);
		}
	}
}

static char sQueryableProcOutputFile[CRYPTIC_MAX_PATH] = "";

static char *spCurProcessQueryString = NULL;
static U32 iCurQueryableProcessStartTime = 0;
static QueryableProcessHandle *pQueryableProcessHandle = NULL;

void handleRequestLaunchAndQueryProcess(Packet *pPak)
{
	char *pNewCmdString = pktGetStringTemp(pPak);

	if (!sQueryableProcOutputFile[0])
	{
		getExecutableDir(sQueryableProcOutputFile);
		backSlashes(sQueryableProcOutputFile);
		strcat(sQueryableProcOutputFile, "\\launcher_query_output.txt");
	}

	if (pQueryableProcessHandle)
	{
		static char *pTimeAgoString = NULL;
		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - iCurQueryableProcessStartTime, &pTimeAgoString);
		CRITICAL_NETOPS_ALERT("LAUNCHER_PROCESS_INTERRUPTION", "%s ago, the launcher launched and started querying command \"%s\". That never completed, and it is now killing that and starting \"%s\"",
			pTimeAgoString, spCurProcessQueryString, pNewCmdString);
		KillQueryableProcess(&pQueryableProcessHandle);
	}

	estrCopy2(&spCurProcessQueryString, pNewCmdString);
	iCurQueryableProcessStartTime = timeSecondsSince2000();

	mkdirtree_const(sQueryableProcOutputFile);

	pQueryableProcessHandle = StartQueryableProcess(spCurProcessQueryString, NULL, false, false, false, sQueryableProcOutputFile);

	if (!pQueryableProcessHandle)
	{
		CRITICAL_NETOPS_ALERT("LAUNCHER_PROCESS_FAIL", "The launcher was asked to start command \"%s\", but couldn't for some reason",
			pNewCmdString);
	}
}


void UpdateQueryableProcess(void)
{
	if (pQueryableProcessHandle)
	{
		int iRetVal;
		if (QueryableProcessComplete(&pQueryableProcessHandle, &iRetVal))
		{
			if (iRetVal != 0)
			{
				char *pBuf = fileAlloc(sQueryableProcOutputFile, NULL);

				if (!pBuf)
				{
					CRITICAL_NETOPS_ALERT("LAUNCHER_PROCESS_FAIL", "The launcher ran command \"%s\", and it failed with return code %d. Console output unavailable for some reason",
						spCurProcessQueryString, iRetVal);
					return;
				}
				else
				{
					char **ppLines = NULL;
					DivideString(pBuf, "\r\n", &ppLines, DIVIDESTRING_POSTPROCESS_WINDOWSNEWLINES);

					if (eaSize(&ppLines) <= 40)
					{
						CRITICAL_NETOPS_ALERT("LAUNCHER_PROCESS_FAIL", "The launcher ran command \"%s\", and it failed with return code %d. Console output:\n%s",
							spCurProcessQueryString, iRetVal, pBuf);
						SAFE_FREE(pBuf);
						eaDestroyEx(&ppLines, NULL);
					}
					else
					{
						int i;
						char *pTempString = NULL;

						log_printf(LOG_ERRORS, "The launcher ran command \"%s\", and it failed with return code %d. Console output:\n%s",
							spCurProcessQueryString, iRetVal, pBuf);
						SAFE_FREE(pBuf);

						
						for (i=0; i < 20; i++)
						{
							estrConcatf(&pTempString, "%s\n", ppLines[i]);
						}

						estrConcatf(&pTempString, "<<<<<<OUTPUT WAS TOO LONG, TRUNCATED HERE, FULL OUTPUT LOGGED ON LAUNCHER MACHINE>>>>>>\n");

						for (i=eaSize(&ppLines) - 20; i < eaSize(&ppLines); i++)
						{
							estrConcatf(&pTempString, "%s\n", ppLines[i]);
						}

						CRITICAL_NETOPS_ALERT("LAUNCHER_PROCESS_FAIL", "The launcher ran command \"%s\", and it failed with return code %d. Console output (TRUNCATED):\n%s",
							spCurProcessQueryString, iRetVal, pTempString);
						eaDestroyEx(&ppLines, NULL);
						estrDestroy(&pTempString);
					}
				}
			}
		}
	}
}


void handleSetLogServer(Packet *pPak)
{
	ONCE(strcpy(gServerLibState.logServerHost, pktGetStringTemp(pPak));svrLogInit();)
}


// defaults to map
static LauncherEnabledServer s_enabledServers = kLauncherEnabledServer_Map;
static int last_cmd=-1, last_last_cmd=-1;
static void launcherHandleMsg(Packet *pak,int cmd, NetLink *link,void *userdata)
{
	const char *pTempChar;

	COARSE_AUTO_START_STATIC_DEFINE(pTempChar, cmd, LauncherQueryCommandsEnum);

	last_last_cmd = last_cmd;
	last_cmd = cmd;
	switch(cmd)
	{
		xcase LAUNCHERQUERY_REGISTEREXISTINGPROCESS:
			handleProcessRegister(pak);
		xcase LAUNCHERQUERY_STARTPROCESS:
			handleStartProcess(pak);
		xcase LAUNCHERQUERY_GAMEVERSION:
			handleGameVersion(pak);
		xcase LAUNCHERQUERY_KILLPROCESS:
			handleKillProcess(pak);
		xcase LAUNCHERQUERY_HIDEPROCESS:
			handleHideProcess(pak);
		xcase LAUNCHERQUERY_IGNOREPROCESS:
			handleIgnoreProcess(pak);
		xcase FROM_CONTROLLER_IAMDYING:
			handleMCPDying();
		xcase FROM_CONTROLLER_RELAY_COMMAND_TO_XBOX_CLIENT:
			handleRelayCommandToXboxClient(pak);
		xcase LAUNCHERQUERY_SETDIRECTORYCONTENTS:
			coarseTimerFrameCheck_DontCountThisFrame();
			handleSetDirectoryContents(pak);
		xcase LAUNCHERQUERY_GETAMANUALDUMP:
			handleRequestManualDump(pak);
		xcase LAUNCHERQUERY_HEREARELOCALEXECRCS:
			handleLocalEXECrcs(link, pak);
		xcase LAUNCHERQUERY_HEREAREREQUESTEDEXES:
			handleHereAreEXES(link, pak);
		xcase LAUNCHERQUERY_CONTROLLERTIME:
			handleControllerTime(pak);
		xcase LAUNCHERQUERY_VERBOSEPROCLISTLOGGING:
			handleSetVerboseProcListLogging(pak);
		xcase LAUNCHERQUERY_BEGINGETTINGFILES:
			handleBeginGettingFiles(pak);
		xcase LAUNCHERQUERY_HEREAREREQUESTEDBYTESFROMFILE:
			handleHereAreRequestedBytesFromFile(pak);
		xcase LAUNCHERQUERY_KILLALLBYNAME:
			handleKillAllByName(pak);
		xcase LAUNCHERQUERY_REQUEST_DYN_HOG_PRUNING:
			handleRequestDynHogPruning(pak);
		xcase LAUNCHERQUERY_REQUEST_LAUNCH_AND_QUERY_PROCESS:
			handleRequestLaunchAndQueryProcess(pak);
		xcase LAUNCHERQUERY_SETLOGSERVER:
			handleSetLogServer(pak);
		xcase LAUNCHERQUERY_FILEREQUESTFULFILLED:
			HandleControllerFileRequestFulfilled(link, pak);

		xdefault:
			printf("Unknown command %d\n",cmd);			
	}

	COARSE_AUTO_STOP_STATIC_DEFINE(pTempChar);
}


static void autoRegisterCrypticStuff(void)
{
	const char *install_directory = regGetInstallationDir();
	RegReader reader;
	int last_ran_timestamp;
	int current_timestamp = fileLastChanged("./src/util/NCAutoSetup.exe");
	reader = createRegReader();
	initRegReader(reader, regGetAppKey());
	if (!rrReadInt(reader, "NCAutoSetup", &last_ran_timestamp))
		last_ran_timestamp = 0;
	if (current_timestamp>0	// file exists
		&& current_timestamp != last_ran_timestamp // hasn't been ran yet
		&& last_ran_timestamp != -2) // not disabled (-2 in registry -> disabled)
	{
		char cmdline[1024];
		sprintf(cmdline, "./src/util/NCAutoSetup.exe %s", install_directory);
		system_detach(cmdline, 0, false);
		rrWriteInt(reader, "NCAutoSetup", current_timestamp);
	}
	destroyRegReader(reader);
}

static void AttachPendingProcesses(void)
{
	StashTableIterator iterator;
	StashElement element;

	if(!stashGetCount(htProcessesToAttach)){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	stashGetIterator(htProcessesToAttach, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{

		int pID = stashElementGetIntKey(element);

		if (AttachDebugger(pID))
		{
			int dummy;
			stashIntRemoveInt(htProcessesToAttach, pID, &dummy);
		}
		
	}
	
	PERFINFO_AUTO_STOP();
}

int GetUniqueID()
{
	SYSTEMTIME sysTime;

	GetSystemTime(&sysTime);

	
	return (sysTime.wHour * 12 * 60 * 60
		+ sysTime.wMinute * 60 * 60
		+ sysTime.wSecond * 60
		+ sysTime.wMilliseconds) ^ getHostLocalIp();
}

AUTO_COMMAND;
void SetUniqueContainerID(int dummy)
{
	gServerLibState.containerID = GetUniqueID();
}


void sendIPsToController(void)
{
	U32 ipList[2];
	Packet *pPak;
	
	assert(GetControllerLink());

	setHostIpList(ipList);

	pPak = pktCreate(gpControllerLink, TO_CONTROLLER_HERE_ARE_LAUNCHERS_IPS);

	pktSendBits(pPak, 32, ipList[0]);
	pktSendBits(pPak, 32, ipList[1]);

	pktSendString(pPak, getHostName());

	pktSendBits(pPak, 1, IsUsingX64() ? 1 : 0);

	pktSend(&pPak);
}

void sendSystemSpecsToController(void)
{
	Packet *pPak;
	
	systemSpecsInit();
	assert(GetControllerLink());

	pPak = pktCreate(gpControllerLink, TO_CONTROLLER_HERE_ARE_SYSTEM_SPECS);

	ParserSendStruct(parse_SystemSpecs, pPak, &system_specs);
	pktSend(&pPak);
}
	

static int LauncherListenDisconnect(NetLink* link,void *user_data)
{
	if (link == gpXboxClientLink)
	{
		gpXboxClientLink = NULL;
	}

	return 1;
}

static void LauncherListenHandleMsg(Packet *pak,int cmd, NetLink *link,void *pUserData)
{
	COARSE_AUTO_START_FUNC();

	switch (cmd)
	{
	case TO_LAUNCHER_PROCESS_REPORTING_FPS:
		{
			U32 iPid = pktGetBits(pak, 32);
			GlobalType eType = GetContainerTypeFromPacket(pak);
			ContainerID iID = GetContainerIDFromPacket(pak);
			U32 iCookie = pktGetBits(pak, 32);


			if (iCookie != gServerLibState.antiZombificationCookie)
			{
				if (!iCookie)
				{
					break;
				}

				kill(iPid);
			}
			else
			{
				ProcessInfo *pProcess = FindProcessByPID(iPid);

				if (pProcess)
				{
					pProcess->fFPS = pktGetF32(pak);;
					pProcess->container_type = eType;
					pProcess->container_id = iID;
					pProcess->iLastContactTime = timeSecondsSince2000();
					pProcess->iLongestFrameMsecs = pktGetBits(pak, 32);

					if (pProcess->container_type == GLOBALTYPE_OBJECTDB)
					{
						printf("Just got fps (%f) from ObjectDB %d\n", pProcess->fFPS, pProcess->container_id);
					}

					if (pProcess->container_type == GLOBALTYPE_CONTROLLER && gbXperfOnControllerStallsOrSlowdowns)
					{
						if (pProcess->fFPS < sfControllerFPSWhichTriggersXperf)
						{
							THROTTLE(120, WARNING_NETOPS_ALERT("SLOW_CONTROLLER_FPS", "Launcher got fps %f for controller, lower than %f. Doing xperf dump",
								pProcess->fFPS, sfControllerFPSWhichTriggersXperf);
							xperfDump("SlowControllerFPS"););
						}
					}
				}
			}
		}
		break;


	case TO_LAUNCHER_ERROR:
		{


			int iIsXBOX = pktGetBits(pak, 1);
			int iPid = pktGetBits(pak, 32);
			char *pErrorString = pktGetStringTemp(pak);
			char *pTitleString = pktGetStringTemp(pak);
			char *pFaultString = pktGetStringTemp(pak);
			int iHighLight = pktGetBits(pak, 32);
	
			if (!gpControllerLink)
			{
				break;
			}

			if (iIsXBOX)
			{
				Packet *pOutPack;
				pOutPack = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_RELAYING_ERRORS);

				PutContainerIDIntoPacket(pOutPack, SPECIAL_CONTAINERID_XBOX_CLIENT);
				PutContainerTypeIntoPacket(pOutPack, GLOBALTYPE_CLIENT);
				pktSendString(pOutPack, pErrorString);
				pktSendString(pOutPack, pTitleString);
				pktSendString(pOutPack, pFaultString);
				pktSendBitsPack(pOutPack, 32, iHighLight);

				pktSend(&pOutPack);
			}
			else
			{
				ProcessInfo *pProcInfo = FindProcessByPID(iPid);

				if (pProcInfo && pProcInfo->container_type && pProcInfo->container_id)
				{
					Packet *pOutPack;
					pOutPack = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_RELAYING_ERRORS);

					PutContainerIDIntoPacket(pOutPack, pProcInfo->container_id);
					PutContainerTypeIntoPacket(pOutPack, pProcInfo->container_type);
					pktSendString(pOutPack, pErrorString);
					pktSendString(pOutPack, pTitleString);
					pktSendString(pOutPack, pFaultString);
					pktSendBitsPack(pOutPack, 32, iHighLight);

					pktSend(&pOutPack);
				}
			}
			
		}
		break;

	case TO_LAUNCHER_SETCONTROLLERSCRIPTINGRESULT:
		{
			Packet *pOutPack;
			int iIsXBOX = pktGetBits(pak, 1);
			int iPid = pktGetBits(pak, 32);
			int iResult = pktGetBits(pak, 32);
			char *pResultString = pktGetStringTemp(pak);
	
			assert(gpControllerLink);

			if (iIsXBOX)
			{
				pOutPack = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_RELAYING_SCRIPT_RESULT);
			
				PutContainerIDIntoPacket(pOutPack, SPECIAL_CONTAINERID_XBOX_CLIENT);
				PutContainerTypeIntoPacket(pOutPack, GLOBALTYPE_CLIENT);
		

				pktSendBits(pOutPack, 32, iResult);
				pktSendString(pOutPack, pResultString);
				pktSend(&pOutPack);
			}
			else
			{
				ProcessInfo *pProcInfo = FindProcessByPID(iPid);

				if (pProcInfo && pProcInfo->container_type && pProcInfo->container_id)
				{
					assert(gpControllerLink);
					pOutPack = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_RELAYING_SCRIPT_RESULT);

					PutContainerIDIntoPacket(pOutPack, pProcInfo->container_id);
					PutContainerTypeIntoPacket(pOutPack, pProcInfo->container_type);
			
					pktSendBits(pOutPack, 32, iResult);
					pktSendString(pOutPack, pResultString);
					pktSend(&pOutPack);
				}
			}
		}
		break;

	case TO_LAUNCHER_CONTROLLERSCRIPTINGPAUSE:
		{
			Packet *pOutPack;
			int iIsXBOX = pktGetBits(pak, 1);
			int iPid = pktGetBits(pak, 32);
			int iNumSeconds = pktGetBits(pak, 32);
			char *pReason = pktGetStringTemp(pak);
	
			assert(gpControllerLink);

			if (iIsXBOX)
			{
				assert(gpControllerLink);
				pOutPack = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_RELAYING_SCRIPT_PAUSE_REQUEST);

				PutContainerIDIntoPacket(pOutPack, SPECIAL_CONTAINERID_XBOX_CLIENT);
				PutContainerTypeIntoPacket(pOutPack, GLOBALTYPE_CLIENT);
			
				pktSendBits(pOutPack, 32, iNumSeconds);
				pktSendString(pOutPack, pReason);
				pktSend(&pOutPack);		
			}
			else
			{
				ProcessInfo *pProcInfo = FindProcessByPID(iPid);

				if (pProcInfo && pProcInfo->container_type && pProcInfo->container_id)
				{
					assert(gpControllerLink);
					pOutPack = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_RELAYING_SCRIPT_PAUSE_REQUEST);

					PutContainerIDIntoPacket(pOutPack, pProcInfo->container_id);
					PutContainerTypeIntoPacket(pOutPack, pProcInfo->container_type);
			
					pktSendBits(pOutPack, 32, iNumSeconds);
					pktSendString(pOutPack, pReason);
					pktSend(&pOutPack);
				}
			}
		}
		break;

	case TO_LAUNCHER_REPORTCLIENTSTATE:
		{
			int iIsXBOX = pktGetBits(pak, 1);
			int iPid = pktGetBits(pak, 32);
			char *pStateString = pktGetStringTemp(pak);

			if (!gpControllerLink)
			{
				break;
			}

			if (iIsXBOX)
			{
				Packet *pOutPack;

				pOutPack = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_RELAYING_CLIENT_STATE);
				PutContainerIDIntoPacket(pOutPack, SPECIAL_CONTAINERID_XBOX_CLIENT);
				PutContainerTypeIntoPacket(pOutPack, GLOBALTYPE_CLIENT);

				pktSendString(pOutPack, pStateString);
				pktSend(&pOutPack);
			}
			else
			{
				ProcessInfo *pProcInfo = FindProcessByPID(iPid);

				if (pProcInfo && pProcInfo->container_type && pProcInfo->container_id)
				{
					Packet *pOutPack;
					pOutPack = pktCreate(gpControllerLink, TO_CONTROLLER_LAUNCHER_RELAYING_CLIENT_STATE);

					PutContainerIDIntoPacket(pOutPack, pProcInfo->container_id);
					PutContainerTypeIntoPacket(pOutPack, pProcInfo->container_type);
					pktSendString(pOutPack, pStateString);

					pktSend(&pOutPack);
				}
			}
		}
		break;
	case TO_LAUNCHER_I_AM_XBOX_CLIENT:
		gpXboxClientLink = link;
		break;

	case TO_LAUNCHER_PROCESS_CRASHED_OR_ASSERTED__DEPRECATED:
	case TO_LAUNCHER_PROCESS_BEGAN_CRASH_OR_ASSERT:
		{
			U32 iPid = pktGetBits(pak, 32);
			int iErrorTrackerID = pktGetBits(pak, 32);
			//maybe send full error info here in the future

			MarkCrashedServer(iPid, iErrorTrackerID);

			ea32Push(&geaiCrypticErrorPIDs, iPid);

			printf("Cryptic error reports process %u has crashed\n", iPid);
		}
		break;

	case TO_LAUNCHER_PROCESS_COMPLETED_CRASH_OR_ASSERT:
		{
			U32 iPid = pktGetBits(pak, 32);
			ProcessInfo *pProcInfo;
			ea32FindAndRemoveFast(&geaiCrypticErrorPIDs, iPid);
			printf("Cryptic error is done with process %u\n", iPid);

			pProcInfo = FindProcessByPID(iPid);

			if (pProcInfo && pProcInfo->container_type && pProcInfo->container_id && gpControllerLink)
			{
				Packet *pOutPack;

				pOutPack = pktCreate(gpControllerLink, LAUNCHER_ANSWER_CRYPTIC_ERROR_IS_FINISHED_WITH_SERVER);

				//NOTE NOTE NOTE this must be identical to the bits sent for LAUNCHERANSWER_PROCESS_CRASHED
				PutContainerTypeIntoPacket(pOutPack, pProcInfo->container_type);
				PutContainerIDIntoPacket(pOutPack, pProcInfo->container_id);
				pktSendBits(pOutPack, 32, iPid);
				pktSendBits(pOutPack, 32, 0);
				pktSend(&pOutPack);
				//NOTE NOTE NOTE this must be identical to the bits sent for LAUNCHERANSWER_PROCESS_CRASHED


			}

			
		}

		break;

	}


	COARSE_AUTO_STOP_FUNC();
}

S32 gatherPerfInfo = 1;
AUTO_CMD_INT(gatherPerfInfo, gatherPerfInfo);

void OVERRIDE_LATELINK_ControllerLink_ExtraDisconnectLogging(void)
{
	log_printf(LOG_CRASH, "Launcher will now dump proc and perf info");
	LogProcInfo();
	perfLog();
	log_printf(LOG_CRASH, "Done");

}

// If you change this, be sure to also change the version in Controller.c
static void deleteDynamicHogg(void)
{
	char pcDynHoggPath[MAX_PATH];
	fileGetcwd(SAFESTR(pcDynHoggPath));
	forwardSlashes(pcDynHoggPath);
	strcat(pcDynHoggPath, "/piggs/dynamic.hogg");
	if(fileExists(pcDynHoggPath))
	{
		int ret;
		printf("Deleting dynamic hogg from %s\n", pcDynHoggPath);
		ret = unlink(pcDynHoggPath);
		assertmsg(ret==0, lastWinErr());
	}
}

#define SHARD_BGI_MACRO_STRING "(Shard                         )"

void PutShardNameInBgInfo(void)
{
	char *pShardName = GetShardNameFromShardInfoString();
	int iBufSize;
	char *pBuf = fileAlloc("\\\\jawl\\files\\bginfo\\bgdefault.bgi", &iBufSize);
	if (pBuf)
	{
		char *pNewBuf = malloc(iBufSize + strlen(pShardName));
		int i;
		int iStartOffset = -1;
		for (i=0; i < iBufSize; i++)
		{
			if (strStartsWith(pBuf + i, SHARD_BGI_MACRO_STRING))
			{
				iStartOffset = i;
				break;
			}
		}

		if (iStartOffset != -1)
		{
			FILE *pOutFile;
			memcpy(pNewBuf, pBuf, iBufSize);
			memset(pNewBuf + iStartOffset, ' ', strlen(SHARD_BGI_MACRO_STRING));
			memcpy(pNewBuf + iStartOffset, pShardName, strlen(pShardName) > strlen(SHARD_BGI_MACRO_STRING) ? strlen(SHARD_BGI_MACRO_STRING) : strlen(pShardName));

			pOutFile = fopen("c:\\withShardName.bgi", "wb");
			if (pOutFile)
			{
				fwrite(pNewBuf, iBufSize, 1, pOutFile);
				fclose(pOutFile);
				system("\\\\jawl\\files\\bginfo\\bginfo.exe /accepteula c:\\withShardName.bgi /timer:0");
			}
		}


		free(pBuf);
		free(pNewBuf);
	}
}


int main(int argc,char **argv)
{
	int		i;
	int pointerSize;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	gbTimeStampAllPrintfs = true;

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'L', 0x8080ff);

	FolderCacheChooseModeNoPigsInDevelopment();

	if (isProductionMode() && !gbSkipDeletingDynamicHogg)
	{
		deleteDynamicHogg();
		autoRegisterCrypticStuff();
	}

	preloadDLLs(0);
	//trickGoogleDesktopDll(0); // we also have shared memory, so trick the google desktop dll into mapping somewhere reasonable

	printf( "SVN Revision: %d\n", gBuildVersion);

	setConsoleTitle("Launcher");

	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	serverLibStartup(argc, argv);

	//if we're on a 64 bit machine running a 32-bit executable and the controller is going
	//to be launching 64 bit apps, then try to run launcherx64 instead
	pointerSize = sizeof(void*);
	if (gbRun64BitIfPossible && pointerSize != 8 && IsUsingX64())
	{
		char *p64BitCommandLine = NULL;
		estrPrintf(&p64BitCommandLine, "launcherX64.exe %s", GetCommandLineWithoutExecutable());
		
		if (system_detach_with_fulldebug_fixup(p64BitCommandLine, false, false))
		{
			exit(0);
		}
	}

	// In production, try to start xperf.
	if (isProductionMode())
		xperfEnsureRunning();

	if (0)
	{
		TestTextParser();
	}

	if (gbHide)
	{
		hideConsoleWindow();
	}

	
/*	for(i=1;i<argc;i++)
	{
		if (stricmp(argv[i],"-db")==0)
			strcpy(server_cfg.db_server,argv[++i]);
		if (stricmp(argv[i],"-localmapserver")==0)
			local_launcher = 1;
		if (stricmp(argv[i],"-monitor")==0) {
			setConsoleTitle("Launcher - Performance Monitor Only mode");
			local_launcher = 1;
		}
		if (stricmp(argv[i],"-noversioncheck")==0)
			launcher_no_version_check = 1;
		if(0 == stricmp(argv[i],"-servers"))
		{
			s_enabledServers = kLauncherEnabledServer_None;
			
			for(;i<argc;++i)
			{
				static struct IdStrPairs
				{
					int id;
					char const *str;
				} idStrPairs[] = {
					{kLauncherEnabledServer_Map,"mapserver"},
					{kLauncherEnabledServer_Stat,"statserver"},
				};

				for(i = 0; i < ARRAY_SIZE( idStrPairs ); ++i)
				{
					if( 0 == stricmp(argv[i], idStrPairs[i].str ))
					{
						s_enabledServers |= idStrPairs[i].id;
						break;
					}
				}
			}
		}
	}*/

	loadstart_printf("Starting performance counters...");
	perfGetList(); // Doing this early seems to open up access for querying more processes?  Weird.
	perfBeginBackgroundPerfGetListThread(1.0f);
	loadend_printf("");

#ifdef LAUNCHER_TRACKS_SHARED_HEAP
	loadstart_printf("initializing shared heap memory manager...");
	{
		initSharedHeapMemoryManager();

		/*
		U32 uiReservedSize = reserveSharedHeapSpace(getSharedHeapReservationSize());
		if ( uiReservedSize )
		{
			loadend_printf(" reserved %d MB", uiReservedSize / (1024 * 1024));
		}
		else
		{
			loadend_printf(" failed: no shared heap");
		}
		*/

		loadend_printf("done");
	}
#endif

	loadstart_printf("Networking startup...");
	//packetStartup(0,0);


/*
	netLinkListAlloc(&net_links,MAX_CLIENTS,0,0);
	while (!netInit(&net_links,0,DEFAULT_LAUNCHER_PORT))
		Sleep(1);
	NMAddLinkList(&net_links, launcherHandleMsg);
	*/
	loadend_printf("Done");

	htWindowsToHide = stashTableCreateInt(16);
	htWindowsToShow = stashTableCreateInt(16);
	htProcessesToAttach = stashTableCreateInt(16);

	//start listening ports... retry for a while in case another launcher is still dying
	i = 0;
	do
	{
		if ((pLauncherListen = commListen(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,GetLauncherListenPort(),
				LauncherListenHandleMsg,NULL,LauncherListenDisconnect,0)))
		{
			break;
		}


		Sleep(1000);

		i++;

		assertmsg(i < 10, "Couldn't create listening port on launcher... is another launcher running?");
	} while (1);



	AttemptToConnectToController(false, launcherHandleMsg, true);
	RequestTimeDifferenceWithController();
	sendIPsToController();
	sendSystemSpecsToController();


	if (isProductionMode())
	{
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
		tmSetGlobalThreadPriority(THREAD_PRIORITY_ABOVE_NORMAL);
	}

	if (gbPutShardNameInBginfo)
	{
		PutShardNameInBgInfo();
	}


	if (dirExists(COMMANDLINE_FILE_DIR))
	{
		PurgeDirectoryOfOldFiles(COMMANDLINE_FILE_DIR, 3, NULL);
	}

	// Start PerfLogger.
	if (sbUsePerfLogger)
		perfloggerInit();

	if (isProductionMode())
	{
		BeginPeriodicFreeDiskSpaceCheck(0, 60, 3, 600);
	}

	for(;;)
	{
		static U32 lastTime;
		static U32 lastOncePerSecond;
		U32 curTime = timeGetTime();
		U32 timeDiff = lastTime ? 0 : (curTime - lastTime);
		F32 frametime = (F32)timeDiff / 1000.f;
		
		lastTime = curTime;
		
		if(!lastOncePerSecond){
			lastOncePerSecond = curTime;
		}

	
		coarseTimerFrameCheck(1.0f, 120, 60, COARSE_ALERT | COARSE_XPERF, NULL);
		
		autoTimerThreadFrameBegin(__FUNCTION__);
		COARSE_WRAP(utilitiesLibOncePerFrame(REAL_TIME));
	
		COARSE_WRAP(commMonitor(commDefault()));

		COARSE_WRAP(launcherUpdateGettingFiles()); 

		COARSE_WRAP(UpdateQueryableProcess());

		if (bMachineStatusExistsOrExisted)
		{
			COARSE_WRAP(LauncherMachineStatus_Update());
		}
		
		if (pDynHogFilePruningProc)
		{
			COARSE_WRAP(UpdateDynHogFilePruning());
		}
		

		if (curTime - lastOncePerSecond >= 1000)
		{
			U32 oldPriority;
			PERFINFO_AUTO_START("oncePerSecond", 1);
			if(isDevelopmentMode()){
				oldPriority = GetThreadPriority(GetCurrentThread());
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
			}
			
			COARSE_WRAP(HideAndShowWindows(); AttachPendingProcesses());
			COARSE_WRAP(procGetList());
			
			COARSE_WRAP(sendProcesses(GetControllerLink()));

			lastOncePerSecond = timeGetTime();
			if(isDevelopmentMode()){
				SetThreadPriority(GetCurrentThread(), oldPriority);
			}

			if (!bMachineStatusExistsOrExisted && processExistsByNameSubString("MachineStatus"))
			{
				printf("Found MachineStatus.exe... starting reporting\n");
				bMachineStatusExistsOrExisted = true;
			}

			PERFINFO_AUTO_STOP();
		}

		COARSE_WRAP(serverLibOncePerFrame());


#ifdef LAUNCHER_TRACKS_SHARED_HEAP
		sharedHeapLazySyncHandlesOnly();
#endif
		autoTimerThreadFrameEnd();
	}
	EXCEPTION_HANDLER_END
}

#include "Launcher_c_ast.c"
#include "MachineStatusPub_h_ast.c"
