
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

#include "UtilitiesLib.h"
#include "logging.h"
#include "sock.h"
#include <stdio.h>
#include <conio.h>
#include <sys/types.h>
#include "MemoryMonitor.h"

#include "FolderCache.h"

#include "sysutil.h"
#include <math.h>
#include "gimmeDLLWrapper.h"

#include "winutil.h"
#include "ServerLib.h"
#include "Controller.h"
#include "autogen/appserverlib_autogen_remotefuncs.h"
#include "autogen/serverlib_autogen_remotefuncs.h"
#include "autogen/objectdb_autogen_remotefuncs.h"
#include "autogen/gameserverlib_autogen_remotefuncs.h"
#include "AutoGen/controller_h_ast.h"
//#include "AutoGen/controller_alerts_h_ast.h"
#include "MemAlloc.h"
#include "TimedCallback.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "hashFunctions.h"
#include "patchtrivia.h"
#include "fileutil.h"
#include "zutils.h"
#include "Process_util.h"
#include "stringcache.h"
#include "HttpXpathSupport.h"
#include "svrGlobalInfo_h_Ast.h"
#include "statusReporting.h"
#include "net.h"
#include "error.h"
#include "timing_profiler_interface.h"
#include "accountnet.h"
#include "rand.h"
#include "osDependent.h"
#include "controllerpub_h_ast.h"
#include "ContinuousBuilderSupport.h"
#include "Controller_Utils.h"
#include "ticketNet.h"
#include "RemoteCommandGroup.h"
#include "RemoteCommandGroup_h_Ast.h"
#include "resource_Controller.h"
#include "Controller_InterShardComm.h"
#include "ShardVariableCommon.h"
#include "StringUtil.h"
#include "Controller_Startup.h"
#include "controller_DynHoggPruning.h"
#include "patchclient.h"
#include "Controller_AutoSettings.h"
#include "Controller_MachineGroups.h"
#include "Controller_PCLStatusMonitoring.h"
#include "SentryServerComm.h"
#include "NotesServerComm.h"
#include "structNet.h"
#include "autogen/ControllerStartupSupport_h_ast.h"
#include "Controller_Utils.h"
#include "Expression.h"
#include "Controller_ShardCluster.h"
#include "FileUtil2.h"
#include "Controller_ClusterController.h"
#include "ShardCluster.h"
#include "ShardCommon.h"
#include "GlobalTypes_h_ast.h"

bool gbCheckForFrankenbuildExes = false;

//any of the GS stalls alerts, or other server keepalives, won't trigger unless they've been continuously true for this many seconds
int siSecondsBeforeStallAlerts = 2;
AUTO_CMD_INT(siSecondsBeforeStallAlerts, SecondsBeforeStallAlerts) ACMD_CONTROLLER_AUTO_SETTING(MiscAlerts);

//any of the GS stalls alerts, or other server keepalives won't trigger unless they've been continuously true for this many controller frames
int siFramesForStallAlerts = 100;
AUTO_CMD_INT(siFramesForStallAlerts, FramesForStallAlerts) ACMD_CONTROLLER_AUTO_SETTING(MiscAlerts);


//if true, then use sentry server to send launcher to each machine we grab. This will only work
//if DontPatchOtherMachines is set
bool gbSpecialLauncherMirroring = false;
AUTO_CMD_INT(gbSpecialLauncherMirroring, SpecialLauncherMirroring);

void SetKeepAliveStartTime(TrackedServerState *pServer);

//if this string is non-empty then this controller is running from a "patched" version, and can pass that patching information
//along to other machines to ensure shard compatibility
char gPatchingCommandLine[1024] = "";

//similar to the above, this string contains the patch name for this build
char gPatchName[1024] = "";

char *pSpawnPatchCmdLine = NULL;
AUTO_COMMAND;
void SpawnPatchCmdLine(char *pStr)
{
	estrCopy2(&pSpawnPatchCmdLine, pStr);
}


bool gbGameServerInactivityTimeoutMinutesSet = false;
int giGameServerInactivityTimeoutMinutes = 0;

U64 giControllerTick = 0;
U64 giServerMonitoringBytes = 0;

U32 gControllerStartupTime = 0;

NetListen *net_links;

bool AttachDebugger(long iProcessID);

bool gbDontPatchOtherMachines = false;
AUTO_CMD_INT(gbDontPatchOtherMachines, DontPatchOtherMachines);

NetComm *comm_controller;

ControllerScriptingCommandList gCommandList = { 0 };

int gControllerTimer;

bool gbShuttingDown = false;

ContainerID sUIDOfMCPThatStartedMe = 0;
static U32 sPIDOfMCPThatStartedMe = 0;
bool gbMCPThatStartedMeIsReady = false;

char *pLauncherCommandLine = NULL;

//config on how to start up the local launcher, as specified in MCP
bool gbStartLocalLauncherInDebugger = false;
bool gbHideLocalLauncher = false;
int gbConnectToControllerTracker = false;

// Paths to use for launching things
char gExecutableDirectory[MAX_PATH];
char gCoreExecutableDirectory[MAX_PATH];

//how many servers, if any, are currently waiting for other servers
int giNumWaitingServers = 0;

//special case for gameservers, so that if the "start in debugger" game server checkbox on the MCP is set, all
//gameservers will start in the debugger
bool gbStartGameServersInDebugger = false;

//if this is true, then use the sentry server to start launchers and stuff, otherwise do it the old way
bool gbUseSentryServer = false;
AUTO_CMD_INT(gbUseSentryServer, UseSentryServer) ACMD_CMDLINE;

//if this is true, then we are a "production shard" meaning we die when a CanNotCrash server dies,
//and recreate ReCreateOnCrash servers
bool gbProductionShardMode = false;
AUTO_CMD_INT(gbProductionShardMode, ProductionShardMode) ACMD_CMDLINE;

//every N seconds, log the overall state of the controller
int giLogControllerOverviewInterval = 600;
AUTO_CMD_INT(giLogControllerOverviewInterval, LogControllerOverviewInterval) ACMD_CMDLINE;

//if this is true, then the controller informs other serves of IPs of itself and trans/log/etc servers via their public
//IPs instead of private
bool gbEverythingUsesPublicIPs = false;
AUTO_CMD_INT(gbEverythingUsesPublicIPs, EverythingUsesPublicIPs) ACMD_CMDLINE;


//if not empty, the name for this shard that shows up in the list of shards in the MCP
char gNameToGiveToControllerTracker[128] = "";
AUTO_CMD_STRING(gNameToGiveToControllerTracker, ShardName) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

char *OVERRIDE_LATELINK_GetShardNameFromShardInfoString(void)
{
	if (gNameToGiveToControllerTracker[0])
	{
		return gNameToGiveToControllerTracker;
	}

	if (ParseCommandOutOfCommandLine("ShardName", gNameToGiveToControllerTracker))
	{
		return gNameToGiveToControllerTracker;
	}

	return DEFAULT_LATELINK_GetShardNameFromShardInfoString();
}

//if true, then this controller starts up an MCP that is monitoring it.
bool gbLaunchMonitoringMCP = false;
AUTO_CMD_INT(gbLaunchMonitoringMCP, LaunchMonitoringMCP) ACMD_CMDLINE;

//if not empty, extra command line options that anyone auto-patching-via-mcp will get 
//on their client

char *gpAutoPatchedClientCommandLine = NULL;


//the "Category" of shard, which the controller tracker sorts shards by
char gShardCategoryName[64] = "dev";
AUTO_CMD_STRING(gShardCategoryName, ShardCategory) ACMD_CMDLINE;

// the "MT category" of shard, which determines how microtransactions are handled by the game
// also the "MT kill switch," which disables all microtransactions on the shard if set
char gpMTCategory[64] = "Off";
bool gbMTKillSwitch = false;
bool gbBillingKillSwitch = false;
AUTO_CMD_STRING(gpMTCategory, MTCategory) ACMD_CMDLINE;

//if true, when the controller starts up and starts launchers, it will kill all other .exes that are still running.
//Useful for clearing out crashed apps and so forth from previous runnings of the shard
//Only works when starting things via sentry server
bool gbKillAllOtherShardExesAtStartup = false;
AUTO_CMD_INT(gbKillAllOtherShardExesAtStartup, KillAllOtherShardExesAtStartup) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);


//tell all launchers to kill all shard exes when they shut down
bool gbKillAllOtherShardExesAtShutdown = false;
AUTO_CMD_INT(gbKillAllOtherShardExesAtShutdown, KillAllOtherShardExesAtShutdown) ACMD_CMDLINE;

//if true, then in a production shard, copy all files from the local data directory onto all patched
//machines
bool gbDoDataDirectoryCloning = false;
AUTO_CMD_INT(gbDoDataDirectoryCloning, DoDataDirectoryCloning) ACMD_CMDLINE;

//if true, then do NOT restart the shard when do-not-crash server crashes
bool gbIgnoreCanNotCrashFlag = false;
AUTO_CMD_INT(gbIgnoreCanNotCrashFlag, IgnoreCanNotCrashFlag) ACMD_CATEGORY(debug) ACMD_CMDLINE;

//if true, then do NOT start account server and kill existing ones
//normally, the controller tries to launch an account server whenever getAccountServer() is "localhost"
bool gbDoNotLaunchAccountServer = false;
AUTO_CMD_INT(gbDoNotLaunchAccountServer, DoNotLaunchAccountServer) ACMD_CMDLINE;

//delay before assuming inactive launchers are dead
int gLauncherKeepAliveTime = 10;
AUTO_CMD_INT(gLauncherKeepAliveTime, LauncherKeepAliveTime);

//wait this long before starting to worry about a newly created launcher
int gLauncherCreationGracePeriod = 60;
AUTO_CMD_INT(gLauncherCreationGracePeriod, LauncherCreationGracePeriod);

//Set the command line that will be automatically added to all pc and xbox clients that are autopatched via MCP button
AUTO_COMMAND ACMD_CMDLINE;
void AutoPatchedClientCommandLine(char *pString)
{
	estrCopy2(&gpAutoPatchedClientCommandLine, pString);
}

//if true, launch 64 bit versions of apps when they exist and when the machine they
//are launching on is 64 bit
bool gbLaunch64BitWhenPossible = false;
AUTO_CMD_INT(gbLaunch64BitWhenPossible, Launch64BitWhenPossible);




bool gbShardIsLocked = false;
bool gbRequestMachineList = false;
bool gbGatewayIsLocked = false;




TrackedServerState **ppLowLevelServerList = NULL;
TrackedServerState *pFirstLowLevelEmptyServer = NULL;

//used for all apps launched by launcher, also locally-created launcher
static char *spGlobalSharedCommandLine = NULL;
static char *spGlobalSharedCommandLine_FromMCP = NULL;
static bool bSharedCommandLineChanged = true;


//in case the 64 bit launcher code doesn't work
bool gbDontLaunch64BitLaunchers = false;
AUTO_CMD_INT(gbDontLaunch64BitLaunchers, DontLaunch64BitLaunchers);

bool gbDoControllerMirroring = false;
AUTO_CMD_INT(gbDoControllerMirroring, DoControllerMirroring);

bool gbUseNewWeightingSystem = true;
AUTO_CMD_INT(gbUseNewWeightingSystem, UseNewWeightingSystem);

float gCPULaunchWeight = 0.5f;
AUTO_CMD_FLOAT(gCPULaunchWeight, CPULaunchWeight);

float gFreeMegsPhysRamLaunchWeight = -0.05f;
AUTO_CMD_FLOAT(gFreeMegsPhysRamLaunchWeight, FreeMegsPhysRamLaunchWeight);

float gFreeMegsVirtRamLaunchWeight = -0.01f;
AUTO_CMD_FLOAT(gFreeMegsVirtRamLaunchWeight, FreeMegsVirtRamLaunchWeight);


//commands currently banned on game servers
char **gppGSLBannedCommands = NULL;
char *gpGSLBannedCommandsString = NULL;

//when you set the priority for a machine to launch a particular server type in a ShardSetup file, doing so actually adds that priority times
//this number to the launch weight during load balancing calculations. Presumably this should just be some very high number
float sfAdditionalLaunchWeightPerCanLaunchPriority = 1000.0f;
AUTO_CMD_FLOAT(sfAdditionalLaunchWeightPerCanLaunchPriority, AdditionalLaunchWeightPerCanLaunchPriority) ACMD_CONTROLLER_AUTO_SETTING(Misc);


//all servers spawned by this controller should redirect their printfs into a unique file in this dir
char gPrintfForkingDir[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(gPrintfForkingDir, PrintfForkingDir);

int OVERRIDE_LATELINK_comm_commandqueue_size(void)
{
	return (1<<20);
}


ControllerGameServerLaunchingConfig gGameServerLaunchingConfig = 
{
	0.004f,
	10.0f,
	500.0f,
	8.0f,
};

//__CATEGORY Stuff relating to deciding which machine (if any) to launch a GS on
//(Percentage) Percent of CPU that is equivalent in value to 1MB of RAM
AUTO_CMD_FLOAT(gGameServerLaunchingConfig.fFreeMegsToCPUScaleFactor, FreeMegsToCPUScaleFactor) ACMD_CONTROLLER_AUTO_SETTING(GsLaunchConfig); 

//(Percentage) Percent minimum available RAM required to launch a new Game Server
AUTO_CMD_FLOAT(gGameServerLaunchingConfig.fMinFreeRAMPercentCutoff, MinFreeRAMPercentCutoff) ACMD_CONTROLLER_AUTO_SETTING(GsLaunchConfig);

//(MB) Minimum available RAM required to launch a new Game Server
AUTO_CMD_FLOAT(gGameServerLaunchingConfig.fMinFreeRAMMegsCutoff, MinFreeRAMMegsCutoff) ACMD_CONTROLLER_AUTO_SETTING(GsLaunchConfig);

//(Percentage) Percent available CPU required to launch a new Game Server
AUTO_CMD_FLOAT(gGameServerLaunchingConfig.fMinFreeCPUCutoff, MinFreeCPUCutoff) ACMD_CONTROLLER_AUTO_SETTING(GsLaunchConfig);


//if true, then invoke an instance of every other server type, tell them all to dump their auto settings, then quit
static bool sbDumpAllAutoSettingsAndQuit = false;
AUTO_CMD_INT(sbDumpAllAutoSettingsAndQuit, DumpAllAutoSettingsAndQuit) ACMD_COMMANDLINE;


static U32 siContainerIDRangeMin = 0, siContainerIDRangeMax = 0;

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void SetContainerIDRange(U32 iMin, U32 iMax)
{
	assertmsgf(iMin == gServerLibState.containerID, "On controller, siContainerIDRangeMin must equal ContainerID");
	siContainerIDRangeMin = iMin;
	siContainerIDRangeMax = iMax;
}

AUTO_RUN_FIRST;
void SetContainerID(void)
{
	char containerIDString[32];
	if (ParseCommandOutOfCommandLine("ContainerID", containerIDString))
	{
		gServerLibState.containerID = atoi(containerIDString);
	}
}

char *GetPrintfForkingCommand(GlobalType eType, ContainerID iID)
{
	static char *spRetVal = NULL;

	if (gPrintfForkingDir[0] == 0)
	{
		estrCopy2(&spRetVal, "");
		return spRetVal;
	}
	else
	{
		estrPrintf(&spRetVal, "-forkPrintfsToFile %s\\%s_%u_%u.txt", gPrintfForkingDir, GlobalTypeToName(eType), iID, timeSecondsSince2000());
		return spRetVal;
	}
}

AUTO_COMMAND;
void NegativeFreeMegsPhysRamLaunchWeight(float f)
{
	gFreeMegsPhysRamLaunchWeight = -f;
}

AUTO_COMMAND;
void NegativeFreeMegsVirtRamLaunchWeight(float f)
{
	gFreeMegsVirtRamLaunchWeight = -f;
}




AUTO_COMMAND ACMD_CMDLINE ACMD_NAME(GlobalSharedCommandLine);
void SetGlobalSharedCommandLine(char *pCmdLine)
{
	estrCopy2(&spGlobalSharedCommandLine, pCmdLine);
	bSharedCommandLineChanged = true;
}

void AppendGlobalSharedCommandLine(char *pToAppend)
{
	estrConcatf(&spGlobalSharedCommandLine, " %s", pToAppend);
	bSharedCommandLineChanged = true;
}

void SetGlobalSharedCommandLine_FromMCP(char *pCmdLine)
{
	if (isProductionMode())
	{
		if (DoCommandLineFragmentsDifferMeaningfully(pCmdLine, spGlobalSharedCommandLine_FromMCP))
		{
			CRITICAL_NETOPS_ALERT("MCP_SETTING_GLOB_CMD_LINE", "The global shared command line is being set through the MCP in production mode from %s to %s. This is dangerous and nonstandard",
				spGlobalSharedCommandLine_FromMCP, pCmdLine);
		}
	}

	estrCopy2(&spGlobalSharedCommandLine_FromMCP, pCmdLine);
	bSharedCommandLineChanged = true;
}


char *GetGlobalSharedCommandLine(void)
{
	static char *pRetVal = NULL;
	const char *shardPatchClient = patchclientParameterEx(false, gExecutableDirectory, gCoreExecutableDirectory);  //aaronwl

	char *pTempString = NULL;

	if (pRetVal && !bSharedCommandLineChanged)
	{
		return pRetVal ? pRetVal : "";
	}

	estrPrintf(&pRetVal, "%s - %s - %s ", 
		CBSupport_GetSpawningCommandLine(), spGlobalSharedCommandLine ? spGlobalSharedCommandLine : "",
		spGlobalSharedCommandLine_FromMCP ? spGlobalSharedCommandLine_FromMCP : "");

	estrSuperEscapeString(&pTempString, GetShardInfoString());
	estrConcatf(&pRetVal, " -EarlySuperEsc SetShardInfoString %s ", pTempString);
	estrDestroy(&pTempString);

	if (shardPatchClient)
		estrAppend2(&pRetVal, shardPatchClient);

	bSharedCommandLineChanged = false;

	return pRetVal ? pRetVal : "";
}


TrackedServerState *GetNewEmptyServer(void)
{
	TrackedServerState *pRetVal;

	if (pFirstLowLevelEmptyServer)
	{
		int iLowLevelIndex;
		pRetVal = pFirstLowLevelEmptyServer;
		pFirstLowLevelEmptyServer = pFirstLowLevelEmptyServer->pNext;

		iLowLevelIndex = pRetVal->iLowLevelIndex;
		memset(pRetVal, 0, sizeof(TrackedServerState));
		pRetVal->iLowLevelIndex = iLowLevelIndex;

		return pRetVal;
	}

	//push an empty server as index 0 in the low level list, because index 0 is invalid
	if (eaSize(&ppLowLevelServerList) == 0)
	{
		eaPush(&ppLowLevelServerList, calloc(sizeof(TrackedServerState), 1));
	}


	pRetVal = calloc(sizeof(TrackedServerState), 1);
	pRetVal->iLowLevelIndex = eaSize(&ppLowLevelServerList);

	eaPush(&ppLowLevelServerList, pRetVal);

	return pRetVal;
}

void ReleaseServer(TrackedServerState *pServer)
{
	pServer->eContainerType = 0;
	pServer->iContainerID = 0;

	pServer->pNext = pFirstLowLevelEmptyServer;
	pFirstLowLevelEmptyServer = pServer;
}



bool ControllerMirroringIsActive(void)
{
	return gbDoControllerMirroring && gpServersByType[GLOBALTYPE_WEBREQUESTSERVER]
		&& strstri(gpServersByType[GLOBALTYPE_WEBREQUESTSERVER]->stateString, gServerTypeInfo[GLOBALTYPE_WEBREQUESTSERVER].monitoringWaitState );
}

// Startup commands

//hide the controller's console window
AUTO_COMMAND ACMD_NAME(hide) ACMD_CMDLINE;
void HideConsoleWindow(int hide)
{
	if (hide)
	{
		hideConsoleWindow();
	}
	else
	{
		showConsoleWindow();
	}
}

//name and UID of MCP that started me
AUTO_COMMAND ACMD_CMDLINE;
void StartedByMCP(char *mcp)
{
	sscanf(mcp, "%d:%d", &sUIDOfMCPThatStartedMe, &sPIDOfMCPThatStartedMe);
	printf("sUIDOfMCPThatStartedMe = %d pid = %d\n", sUIDOfMCPThatStartedMe, sPIDOfMCPThatStartedMe);
}

U32 GetPIDOfMCPThatStartedMe(void)
{
	return sPIDOfMCPThatStartedMe;
}

// Rather to launch launchers in the debugger
AUTO_CMD_INT(gbStartLocalLauncherInDebugger, StartLocalLauncherInDebugger) ACMD_CMDLINE;

// Rather to hide the local launchers you create
AUTO_CMD_INT(gbHideLocalLauncher, HideLocalLauncher) ACMD_CMDLINE;

// sets whether controller connects to ControllerTracker
AUTO_CMD_INT(gbConnectToControllerTracker, ConnectToControllerTracker) ACMD_CMDLINE;

// Sets what directory to run executables from
AUTO_CMD_STRING(gExecutableDirectory, ExecDir) ACMD_CMDLINE;

// Sets what directory to run core tools from (if empty, use gExecutableDirectory instead)
AUTO_CMD_STRING(gCoreExecutableDirectory, CoreExecDir) ACMD_CMDLINE;

//special case so that game servers launched not by MCP can still
//start in debugger
AUTO_CMD_INT(gbStartGameServersInDebugger, StartGameServersInDebugger) ACMD_CMDLINE;

ServerTypeInfo gServerTypeInfo[GLOBALTYPE_MAXTYPES] = {{0}};


int giNumMachines = 0;
TrackedMachineState gTrackedMachines[MAX_MACHINES];
TrackedServerState *gpServersByType[GLOBALTYPE_MAXTYPES];
TrackedMachineState *spLocalMachine = NULL;
bool sbNeedToKillAllServers = false;
StashTable sServerTables[GLOBALTYPE_MAXTYPES];
int giLongestDelayedGameServerTime = 0;

int giTotalNumServers = 0;
int giTotalNumServersByType[GLOBALTYPE_MAXTYPES] = {0};

StashTable gMachinesByName = {0};

//copy of the data  directories, for directory cloning in production shards
TPFileList *gpDataDirFileList = NULL;






AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_CONTROLLER);
}


	



void Controller_InitLists()
{
	memset(gTrackedMachines, 0, sizeof(gTrackedMachines));
	memset(gpServersByType, 0, sizeof(gpServersByType));

	memset(sServerTables, 0, sizeof(sServerTables));

	sServerTables[GLOBALTYPE_GAMESERVER] = stashTableCreateInt(300);
	sServerTables[GLOBALTYPE_MULTIPLEXER] = stashTableCreateInt(300);
	sServerTables[GLOBALTYPE_LAUNCHER] = stashTableCreateInt(300);
	sServerTables[GLOBALTYPE_TESTCLIENT] = stashTableCreateInt(300);

	giNumMachines = 0;
}


void UpdateControllerTitle(void)
{
	char buf[200];
	sprintf_s(SAFESTR(buf), "Controller - %s", gNameToGiveToControllerTracker[0] ? gNameToGiveToControllerTracker : "(unnamed shard)");
	setConsoleTitle(buf);
}



void PutExtraStuffInLauncherCommandLine(char **ppFullCommandLine, U32 iContainerID)
{
	int i;

	if (UtilitiesLib_GetSharedMachineIndex())
	{
		estrConcatf(ppFullCommandLine, " -SetSharedMachineIndex %d ", UtilitiesLib_GetSharedMachineIndex());
	}

	if (GetGlobalSharedCommandLine())
	{
		estrConcatf(ppFullCommandLine, " - %s - ", GetGlobalSharedCommandLine());
	}



	if (gbShutdownWatcher)
	{
		char **ppListOfMustShutItselfDownExes = GetListOfMustShutItselfDownExes();
		for (i=0; i < eaSize(&ppListOfMustShutItselfDownExes); i++)
		{
			estrConcatf(ppFullCommandLine, " -ExeMustShutItselfDown %s ", ppListOfMustShutItselfDownExes[i]);
		}
	}


	if (gbKillAllOtherShardExesAtShutdown)
	{
		char *pTempExeName = NULL;

		estrStackCreate(&pTempExeName);

		for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
		{
			if (i != GLOBALTYPE_CONTROLLER && i != GLOBALTYPE_MASTERCONTROLPROGRAM && i != GLOBALTYPE_LAUNCHER)
			{
				//FRANKENBUILD TODO
				if (gServerTypeInfo[i].executableName32_original[0])
				{
					estrCopy2(&pTempExeName, gServerTypeInfo[i].executableName32_original);
					estrTruncateAtFirstOccurrence(&pTempExeName, ' ');
					estrConcatf(ppFullCommandLine, " -killAllExesOfThisTypeAtShutdown %s ", pTempExeName);
				}
				if (gServerTypeInfo[i].executableName64_original[0])
				{
					estrCopy2(&pTempExeName, gServerTypeInfo[i].executableName64_original);
					estrTruncateAtFirstOccurrence(&pTempExeName, ' ');
					estrConcatf(ppFullCommandLine, " -killAllExesOfThisTypeAtShutdown %s ", pTempExeName);
				}
			}
		}

		estrDestroy(&pTempExeName);
	}

	if (gbLaunch64BitWhenPossible && !gbDontLaunch64BitLaunchers)
	{
		estrConcatf(ppFullCommandLine, " -Run64BitIfPossible ");
	}

	estrConcatf(ppFullCommandLine, " %s ", GetPrintfForkingCommand(GLOBALTYPE_LAUNCHER, iContainerID));

	if (gbDataMirroringDeletion)
	{
		estrConcatf(ppFullCommandLine, " -DataMirroringDeletion 1 ");
	}
}

char **GetListOfMustShutItselfDownExes(void)
{
	static char **sppList = NULL;
	char *pTemp = NULL;
	int i;

	if (sppList)
	{
		return sppList;
	}

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (gServerTypeInfo[i].bMustShutItselfDown)
		{
			estrCopy2(&pTemp, gServerTypeInfo[i].executableName32_original);
			estrTruncateAtFirstOccurrence(&pTemp, ' ');
			eaPushUnique(&sppList, (char*)allocAddString(pTemp));

			if (gServerTypeInfo[i].executableName64_original[0])
			{
				estrCopy2(&pTemp, gServerTypeInfo[i].executableName64_original);
				estrTruncateAtFirstOccurrence(&pTemp, ' ');
				eaPushUnique(&sppList, (char*)allocAddString(pTemp));
			}

		}
	}

	estrDestroy(&pTemp);

	return sppList;
}




TrackedMachineState *FindMachineByName(const char *pName)
{
	U32 iIP;
	U32 iPublicIP;
	int i;
	TrackedMachineState *pMachine;

	if (strcmp(pName, "*") == 0)
	{
		return NULL;
	}

	if (stashFindPointer(gMachinesByName, pName, &pMachine))
	{
		return pMachine;
	}



	if (strcmp(pName, "UNKNOWN") == 0)
	{
		iIP = 0xffffffff;
		iPublicIP = 0xffffffff;
	}
	else
	{
		iIP = ipFromString(pName);
		iPublicIP = ipPublicFromString(pName);
	}


	for (i=0; i < giNumMachines; i++)
	{
		if (stricmp(gTrackedMachines[i].machineName, pName) == 0 || (gTrackedMachines[i].IP == iIP) || (gTrackedMachines[i].iPublicIP == iIP) ||
		    (gTrackedMachines[i].IP == iPublicIP) || (gTrackedMachines[i].iPublicIP == iPublicIP))
		{
			return &gTrackedMachines[i];
		}
	}

	return NULL;
}

TrackedMachineState *FindMachineByIP(U32 iIP, U32 iPublicIP)
{
	int i;

	for (i=0; i < giNumMachines; i++)
	{
		if ((gTrackedMachines[i].IP == iIP) || (gTrackedMachines[i].iPublicIP == iIP) ||
		    (gTrackedMachines[i].IP == iPublicIP) || (gTrackedMachines[i].iPublicIP == iPublicIP))
		{
			return &gTrackedMachines[i];
		}
	}

	return NULL;
}

//NULL means local machine
TrackedMachineState *GetMachineFromNetLink(NetLink *pLink)
{
	U32 IP = pLink ? linkGetSAddr(pLink) : getHostLocalIp();
	U32 iPublicIP = pLink ? IP : getHostPublicIp();

	bool bIsLocal = (IP == getHostLocalIp());

	int i;
	TrackedMachineState *pMachine;

	for (i=0; i < giNumMachines; i++)
	{
		if (bIsLocal && gTrackedMachines[i].bIsLocalHost || IP == gTrackedMachines[i].IP || iPublicIP == gTrackedMachines[i].iPublicIP)
		{
			return &gTrackedMachines[i];
		}
	}

	if (giNumMachines == MAX_MACHINES)
	{
		return NULL;
	}

	pMachine = &gTrackedMachines[giNumMachines++];

	pMachine->bIsLocalHost = bIsLocal;
	pMachine->IP = IP;
	pMachine->iPublicIP = iPublicIP;
	sprintf(pMachine->machineName, "UNKNOWN");
	sprintf(pMachine->mainLink, "<a href=\"%s.machine[%d]\">Link</a>", LinkToThisServer(), giNumMachines - 1);
	pMachine->VNCString = pMachine->machineName;

	if (bIsLocal)
	{
		spLocalMachine = pMachine;
	}

	//the local machine starts out being able to launch everything, everything else can launch nothing
	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		pMachine->canLaunchServerTypes[i].eCanLaunch = bIsLocal ? CAN_LAUNCH_DEFAULT : CAN_NOT_LAUNCH;
	}

	ApplyCommandConfigStuffToMachine(pMachine);


	return pMachine;
}

PERFINFO_TYPE *sFindServerFromIDPerfInfos[GLOBALTYPE_LAST];

TrackedServerState *FindServerFromID(GlobalType eContainerType, U32 iContainerID)
{
	TrackedServerState *pServer = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (eContainerType < 0 || eContainerType >= GLOBALTYPE_MAXTYPES)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_START_STATIC(globalTypeMapping[eContainerType].name, &sFindServerFromIDPerfInfos[eContainerType], 1);

	if (sServerTables[eContainerType])
	{
		PERFINFO_AUTO_START("Stash", 1);
		if(!stashIntFindPointer(sServerTables[eContainerType], iContainerID, &pServer))
		{
			pServer = NULL;
		}
		PERFINFO_AUTO_STOP();
	}
	else
	{
		PERFINFO_AUTO_START("Linked List", 1);
		pServer = gpServersByType[eContainerType];

		while (pServer)
		{
			if (pServer->iContainerID == iContainerID)
			{
				break;
			}

			pServer = pServer->pNext;
		}
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
	return pServer;
}

TrackedServerState *FindServerFromIDRespectingID0(GlobalType eContainerType, U32 iContainerID)
{
	if (iContainerID != 0)
	{
		return FindServerFromID(eContainerType, iContainerID);
	}

	if (eContainerType < 0 || eContainerType >= GLOBALTYPE_MAXTYPES)
	{
		return NULL;
	}

	if (gpServersByType[eContainerType])
	{
		return gpServersByType[eContainerType];
	}

	return NULL;
}

TrackedServerState *FindRandomServerOfType(GlobalType eType)
{
	int iCount = giTotalNumServersByType[eType];
	int iIndex;
	TrackedServerState *pServer;

	if (!iCount)
	{
		return NULL;
	}

	if (iCount == 1)
	{
		return gpServersByType[eType];
	}

	iIndex = randomIntRange(0, iCount - 1);

	pServer = gpServersByType[eType];
	while (iIndex)
	{
		pServer = pServer->pNext;
		iIndex--;
	}

	return pServer;
}


//this is initialized to zero, but we want to start IDs from 1, so we always increment before allocating
static U32 sFreeContainerIDs[GLOBALTYPE_MAXTYPES] = { 0 };

//each server has a creation index, which is used by scripting to refer to specifically created servers,
//and starts at 0 and increments with each creation. Whenever there are no servers of the type, it is
//reset to zero6
static int sNextServerScriptingIndex[GLOBALTYPE_MAXTYPES] = {0};

//FIXME at some point get rid of this. It's making sure that people are no longer using the old hardwired ID 1 business
AUTO_RUN;
void initFreeCountainerIDs(void)
{
	int i;

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		sFreeContainerIDs[i] = siContainerIDRangeMin ? siContainerIDRangeMin : 1;
	}

	sFreeContainerIDs[GLOBALTYPE_GAMESERVER] = siContainerIDRangeMin ? siContainerIDRangeMin : 100000;
}

U32 GetFreeContainerID(GlobalType eContainerType)
{


	U32 iRetVal = sFreeContainerIDs[eContainerType];

	sFreeContainerIDs[eContainerType]++;

	if (siContainerIDRangeMin)
	{
		if (sFreeContainerIDs[eContainerType] > siContainerIDRangeMax)
		{
			sFreeContainerIDs[eContainerType] = siContainerIDRangeMin;
		}
	}
	else
	{
		if (sFreeContainerIDs[eContainerType] == 0)
		{
			sFreeContainerIDs[eContainerType]++;
		}
	}
	
	return iRetVal;
	
}

void DoTypeSpecificServerInit(TrackedServerState *pServer)
{
	switch (pServer->eContainerType)
	{
	case GLOBALTYPE_GAMESERVER:
		pServer->pGameServerSpecificInfo = StructCreate(parse_GameServerGlobalInfo);
		pServer->pMachine->iGameServerLaunchWeight += pServer->additionalServerLaunchInfo.iGameServerLaunchWeight;
		break;
	case GLOBALTYPE_GATEWAYSERVER:
		pServer->pGatewayServerSpecificInfo = StructCreate(parse_GatewayServerGlobalInfo);
		break;
	case GLOBALTYPE_TRANSACTIONSERVER:
		pServer->pTransServerSpecificInfo = StructCreate(parse_TransactionServerGlobalInfo);
		break;
	case GLOBALTYPE_OBJECTDB:
	case GLOBALTYPE_CLONEOBJECTDB:
	case GLOBALTYPE_CLONEOFCLONE:
		pServer->pDataBaseSpecificInfo = StructCreate(parse_DatabaseGlobalInfo);
		break;
	case GLOBALTYPE_LOGINSERVER:
		pServer->pLoginServerSpecificInfo = StructCreate(parse_LoginServerGlobalInfo);
		break;	
	case GLOBALTYPE_JOBMANAGER:
		pServer->pJobManagerSpecificInfo = StructCreate(parse_JobManagerGlobalInfo);
		break;
	case GLOBALTYPE_LOGSERVER:
		pServer->pLogServerSpecificInfo = StructCreate(parse_LogServerGlobalInfo);

	}
}



void LinkAndInitServer(TrackedServerState *pServer, TrackedMachineState *pMachine, char *pReason, AdditionalServerLaunchInfo *pAdditionalInfo)
{
	//common init stuff for all new servers
	pServer->fCreationTimeFloat = timerElapsed(gControllerTimer);
	pServer->iCreationTime = timeSecondsSince2000();

	sprintf(pServer->VNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pMachine->VNCString);
	sprintf(pServer->machineLink, "<a href=\"%s.machine[%d]\">%s</a>", 
		LinkToThisServer(), pServer->pMachine - gTrackedMachines, pServer->pMachine->machineName);
	sprintf(pServer->monitor, "<a href=\"/viewxpath?xpath=%s[%u].custom\">Monitor</a>", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID);

	sprintf(pServer->uniqueName, "%s_%u", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID); 

	estrPrintf(&pServer->pMuteAlerts, "MuteAlerts %s %u $NOCONFIRM $NORETURN", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID);

	strcpy_trunc(pServer->launchReason, pReason);

	assert(pServer->eContainerType >= 0 && pServer->eContainerType < GLOBALTYPE_MAXTYPES); 

	//if the machine has at least one of this type, then we link after that to avoid having to mess with gpServersByType
	if (pMachine->pServersByType[pServer->eContainerType])
	{
		pServer->pPrev = pMachine->pServersByType[pServer->eContainerType];
		pServer->pNext = pServer->pPrev->pNext;
		pServer->pPrev->pNext = pServer;
		if (pServer->pNext)
		{
			pServer->pNext->pPrev = pServer;
		}
	}
	else 
	{
		pServer->pNext = gpServersByType[pServer->eContainerType];
		pServer->pPrev = NULL;
		gpServersByType[pServer->eContainerType] = pServer;
		pMachine->pServersByType[pServer->eContainerType] = pServer;
		if (pServer->pNext)
		{
			pServer->pNext->pPrev = pServer;
		}
	}

	if (sServerTables[pServer->eContainerType])
	{
		TrackedServerState *pTempServer;

		assert(!stashIntFindPointer(sServerTables[pServer->eContainerType], pServer->iContainerID, &pTempServer));
		stashIntAddPointer(sServerTables[pServer->eContainerType], pServer->iContainerID, pServer, false);
	}

	giTotalNumServers++;
	giTotalNumServersByType[pServer->eContainerType]++;
	pMachine->iTotalServers++;
	pMachine->iServersByType[pServer->eContainerType]++;

	if (pAdditionalInfo)
	{
		StructCopyAll(parse_AdditionalServerLaunchInfo, pAdditionalInfo, &pServer->additionalServerLaunchInfo);
	}

	DoTypeSpecificServerInit(pServer);

	objLog(LOG_CONTROLLER, pServer->eContainerType, pServer->iContainerID, 0, NULL, NULL, NULL, "serverCreation", NULL,
		"%s", pReason);

	SendStringToCB(CBSTRING_COMMENT, "New server %s on controller due to: %s", 
		GlobalTypeAndIDToString(pServer->eContainerType, pServer->iContainerID), pReason);

	eaIndexedEnable(&pServer->ppThingsToDoOnAnyClose, parse_RemoteCommandGroup);
	eaIndexedEnable(&pServer->ppThingsToDoOnCrash, parse_RemoteCommandGroup);

	pServer->pAlertsConditionChecker = ConditionChecker_Create(siSecondsBeforeStallAlerts, siFramesForStallAlerts);

	if (gServerTypeInfo[pServer->eContainerType].bPutInfoAboutMeInShardClusterSummary)
	{
		ControllerShardCluster_SomethingLocalChanged();
	}
}

void DoTypeSpecificServerCleanup(TrackedServerState *pServer)
{
	switch (pServer->eContainerType)
	{
	case GLOBALTYPE_GAMESERVER:
		//if we are already in monitoring wait state, then we've already done our subtracting, don't double-subtract
		if (!strstr(pServer->stateString, gServerTypeInfo[GLOBALTYPE_GAMESERVER].monitoringWaitState))
		{
			pServer->pMachine->iGameServerLaunchWeight -= pServer->additionalServerLaunchInfo.iGameServerLaunchWeight;
		}		

		StructDestroy(parse_GameServerGlobalInfo, pServer->pGameServerSpecificInfo);
		break;
	case GLOBALTYPE_GATEWAYSERVER:
		StructDestroy(parse_GatewayServerGlobalInfo, pServer->pGatewayServerSpecificInfo);
		break;
	case GLOBALTYPE_TRANSACTIONSERVER:
		StructDestroy(parse_TransactionServerGlobalInfo, pServer->pTransServerSpecificInfo);
		break;
	case GLOBALTYPE_OBJECTDB:
	case GLOBALTYPE_CLONEOBJECTDB:
	case GLOBALTYPE_CLONEOFCLONE:
		StructDestroy(parse_DatabaseGlobalInfo, pServer->pDataBaseSpecificInfo);
		break;
	case GLOBALTYPE_LOGINSERVER:
		StructDestroy(parse_LoginServerGlobalInfo, pServer->pLoginServerSpecificInfo);
		break;
	case GLOBALTYPE_JOBMANAGER:
		StructDestroy(parse_JobManagerGlobalInfo, pServer->pJobManagerSpecificInfo);
		break;
	case GLOBALTYPE_LOGSERVER:
		StructDestroy(parse_LogServerGlobalInfo, pServer->pLogServerSpecificInfo);
		break;
	}
}

static void UnlinkServer(TrackedServerState *pServer, TrackedMachineState *pMachine)
{
	assert(pMachine->pServersByType[pServer->eContainerType] && gpServersByType[pServer->eContainerType]);

	DoTypeSpecificServerCleanup(pServer);

	giTotalNumServers--;
	giTotalNumServersByType[pServer->eContainerType]--;
	pMachine->iTotalServers--;
	pMachine->iServersByType[pServer->eContainerType]--;

	if (pMachine->pServersByType[pServer->eContainerType] == pServer)
	{
		pMachine->pServersByType[pServer->eContainerType] = ( (pServer->pNext && (pServer->pNext->pMachine == pServer->pMachine) ) ? pServer->pNext : NULL); 
	}

	if (gpServersByType[pServer->eContainerType] == pServer)
	{
		gpServersByType[pServer->eContainerType] = pServer->pNext;
	}

	if (pServer->pNext)
	{
		pServer->pNext->pPrev = pServer->pPrev;
	}

	if (pServer->pPrev)
	{
		pServer->pPrev->pNext = pServer->pNext;
	}

	if (sServerTables[pServer->eContainerType])
	{
		TrackedServerState *pTempServer;

		bool bRemoved = stashIntRemovePointer(sServerTables[pServer->eContainerType], pServer->iContainerID, &pTempServer);

		assert(bRemoved);
	}

	ConditionChecker_Destroy(&pServer->pAlertsConditionChecker);
}

void AttemptToConnectToTransactionServer()
{
	if (!objLocalManager() && gpServersByType[GLOBALTYPE_TRANSACTIONSERVER]  
		&& !gpServersByType[GLOBALTYPE_TRANSACTIONSERVER]->bWaitingToLaunch
		&& !gpServersByType[GLOBALTYPE_TRANSACTIONSERVER]->bKilledIntentionally
		&& strcmp(gpServersByType[GLOBALTYPE_TRANSACTIONSERVER]->stateString, "ready") == 0)
	{
		SetLocalTransactionManagerConnectionDelayTime(3.0f);

		if (InitObjectTransactionManager(
			GetAppGlobalType(),
			gServerLibState.containerID,
			gpServersByType[GLOBALTYPE_TRANSACTIONSERVER]->pMachine->bIsLocalHost ? "localhost" : makeIpStr(GetIPToUse(gpServersByType[GLOBALTYPE_TRANSACTIONSERVER]->pMachine)),
			gServerLibState.transactionServerPort,
			false, NULL))
		{
			printf("Connected to transaction server\n");
			InformControllerOfServerState_Internal(gpServersByType[GLOBALTYPE_TRANSACTIONSERVER], "connected");
		}
	}
}





void InformOtherServersOfServerDeath(GlobalType eContainerType, ContainerID iContainerID, char *pReason, bool bUnnaturalDeath)
{
	//always inform transaction server when a server crashes or asserts
	if (SAFE_MEMBER(gpServersByType[GLOBALTYPE_TRANSACTIONSERVER], pLink))
	{
		Packet *pPack;

		pPack = pktCreate(gpServersByType[GLOBALTYPE_TRANSACTIONSERVER]->pLink, FROM_CONTROLLER__SERVERSPECIFIC__SERVER_CRASHED);
		PutContainerTypeIntoPacket(pPack, eContainerType);
		PutContainerIDIntoPacket(pPack, iContainerID);
		pktSendString(pPack, pReason);
		pktSend(&pPack);
	}

	//do any type-specific stuff
	switch(eContainerType)
	{
	case GLOBALTYPE_GAMESERVER:
		if (objLocalManager())
		{
			RemoteCommand_InformMapManagerOfGameServerDeath(GLOBALTYPE_MAPMANAGER, 0, iContainerID, bUnnaturalDeath);
			RemoteCommand_InformQueueServerOfGameServerDeath(GLOBALTYPE_QUEUESERVER,0,iContainerID, bUnnaturalDeath);
		}
		break;
	}

	if (objLocalManager())
	{
		if (gServerTypeInfo[eContainerType].bInformObjectDBWhenThisDies)
		{
			RemoteCommand_InformObjectDBOfServerDeath(GLOBALTYPE_OBJECTDB, 0, eContainerType, iContainerID);
		}
	}
}

void DelayedLauncherReconnect(TimedCallback *callback, F32 timeSinceLastCallback, char *pMachineName)
{
	TrackedMachineState *pMachine = FindMachineByName(pMachineName);

	if (pMachine && !pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
	{
		GrabMachineForShardAndMaybePatch(pMachineName, false, false, NULL);
	}
}


//returns true if it did something, false otherwise
bool DealWithDisconnectingLauncher(TrackedServerState *pServer, char *pReason)
{
	if (isProductionMode() && !pServer->bKilledIntentionally)
	{
		TriggerAlertf("LAUNCHER_DISCONNECTED", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0.0f, pServer->eContainerType, 
			pServer->iContainerID, GetAppGlobalType(), GetAppGlobalID(), pServer->pMachine->machineName, 0,
			"Launcher %u on %s has disconnected because \"%s\". Will try to automatically reconnect in 5, 60 and 300 seconds", pServer->iContainerID, pServer->pMachine->machineName, pReason);
		TimedCallback_Run(DelayedLauncherReconnect, pServer->pMachine->machineName, 5.0f);
		TimedCallback_Run(DelayedLauncherReconnect, pServer->pMachine->machineName, 60.0f);
		TimedCallback_Run(DelayedLauncherReconnect, pServer->pMachine->machineName, 300.0f);

		return true;
	}


	return false;
}


void StopTrackingServer(TrackedServerState *pServer, char *pMessage, bool bUnnaturalDeath, bool bAlreadyAlerted)
{
	int i;

	if (pServer->bRemoving)
	{
		return;
	}

	SetServerJustDied(pServer->eContainerType, pServer->iContainerID);


	pServer->bRemoving = true;

	if (gServerTypeInfo[pServer->eContainerType].bPutInfoAboutMeInShardClusterSummary)
	{
		ControllerShardCluster_SomethingLocalChanged();
	}

	for (i=0; i < eaSize(&pServer->ppThingsToDoOnAnyClose); i++)
	{
		ExecuteAndFreeRemoteCommandGroup(pServer->ppThingsToDoOnAnyClose[i], NULL, NULL);
	}
	eaDestroy(&pServer->ppThingsToDoOnAnyClose);


	SendStringToCB(CBSTRING_COMMENT, "Server %s on controller went away due to: %s", 
		GlobalTypeAndIDToString(pServer->eContainerType, pServer->iContainerID), pMessage);

	if(pMessage)
	{
		log_printf(LOG_SHARD, "Controller stopping tracking of server %d of type %s on %s because: %s", pServer->iContainerID, GlobalTypeAndIDToString(pServer->eContainerType, pServer->iContainerID), pServer->pMachine->machineName, pMessage);
	}
	else
	{
		log_printf(LOG_SHARD, "Controller stopping tracking of server %d of type %s on %s", pServer->iContainerID, GlobalTypeAndIDToString(pServer->eContainerType, pServer->iContainerID), pServer->pMachine->machineName);
	}

	if (pServer->eContainerType == GLOBALTYPE_MASTERCONTROLPROGRAM && pServer->iContainerID == sUIDOfMCPThatStartedMe)
	{
			svrExit(1);
	}


	//do alert before unlinking so the server can still be found
	if (pServer->eContainerType != GLOBALTYPE_MASTERCONTROLPROGRAM 
		&& !(pServer->eContainerType == GLOBALTYPE_LAUNCHER && DealWithDisconnectingLauncher(pServer, pMessage)))
	{
		if (!pServer->bKilledIntentionally && !pServer->bHasCrashed && !bAlreadyAlerted)
		{
			char *pOutMessage = NULL;

			if (pMessage)
			{
				estrPrintf(&pOutMessage, "Controller stopping tracking of %s on %s because: %s",
					GlobalTypeAndIDToString(pServer->eContainerType, pServer->iContainerID), pServer->pMachine->machineName, pMessage);
			}
			else
			{
				estrPrintf(&pOutMessage, "%s(%d) has died unexpectedly on %s", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID, pServer->pMachine->machineName);
			}

			TriggerAlert(GetCrashedOrAssertedKey(pServer->eContainerType),
				pOutMessage,
				ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, pServer->eContainerType, pServer->iContainerID,
				pServer->eContainerType, pServer->iContainerID, pServer->pMachine->machineName, 0);

			estrDestroy(&pOutMessage);

		}
	}


	UnlinkServer(pServer, pServer->pMachine);


	linkRemove(&pServer->pLink);

	if (pServer->bWaitingToLaunch)
	{
		giNumWaitingServers--;
		pServer->pMachine->iWaitingServers--;
	}

	


	//never react to MCPs dying
	if (pServer->eContainerType != GLOBALTYPE_MASTERCONTROLPROGRAM)
	{

		if (ControllerScripting_IsRunning())
		{
			ControllerScripting_ServerDied(pServer);
		}


		InformOtherServersOfServerDeath(pServer->eContainerType, pServer->iContainerID, pMessage, bUnnaturalDeath);



		if (gServerTypeInfo[pServer->eContainerType].bCanNotCrash && gbProductionShardMode && !gbIgnoreCanNotCrashFlag)
		{
			char errorString[1024];
			sprintf_s(SAFESTR(errorString), "Server %d of type %s crashed... we can not recover, dying\n", pServer->iContainerID, GlobalTypeToName(pServer->eContainerType));

			printf("%s", errorString);
			log_printf(LOG_CRASH, "%s", errorString);
			svrExit(1);
		}

		if ((!pServer->bKilledIntentionally || pServer->bRecreateDespiteIntentionalKill) && gServerTypeInfo[pServer->eContainerType].bReCreateOnCrash && gbProductionShardMode)
		{
			printf("A %s died. Recreating\n", GlobalTypeToName(pServer->eContainerType));
			RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, VIS_UNSPEC, FindDefaultMachineForType(pServer->eContainerType, NULL, NULL), pServer->eContainerType, GetFreeContainerID(pServer->eContainerType), false, "", pServer->pWorkingDirectory, false, 0,
				NULL, "Server %s[%u] died, and is a ReCreateOnCrash server", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID);
		}

		if ((!pServer->bKilledIntentionally || pServer->bRecreateDespiteIntentionalKill) && gServerTypeInfo[pServer->eContainerType].bReCreateOnSameMachineOnCrash && gbProductionShardMode)
		{
			printf("A %s died. Recreating on same machine\n", GlobalTypeToName(pServer->eContainerType));

			if (pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
			{
				RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, VIS_UNSPEC, pServer->pMachine, pServer->eContainerType, GetFreeContainerID(pServer->eContainerType), false, "", pServer->pWorkingDirectory, false, 0,
					NULL, "Server %s[%u] died, and is a ReCreateOnSameMachineOnCrash server", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID);
			}
			else
			{
				TriggerAlertf(allocAddString("COULDNT_RECREATE_SERVER"), ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 
					pServer->eContainerType, pServer->iContainerID, GetAppGlobalType(), GetAppGlobalID(), pServer->pMachine->machineName, 0,
					"Server %s[%u] died, and is RecreateOnSameMachineOnCrash, but we couldn't recreate it because the machine appears to be dead",
					GlobalTypeToName(pServer->eContainerType), pServer->iContainerID);
			}
		}

		if (!gpServersByType[pServer->eContainerType])
		{
			sNextServerScriptingIndex[pServer->eContainerType] = 0;
		}
	}

	if (pServer->bHasCrashed)
	{
		pServer->pMachine->iCrashedServers--;
	}

	if (pServer->pLaunchDebugNotification)
	{
		StructDestroy(parse_ServerLaunchDebugNotificationInfo, pServer->pLaunchDebugNotification);
		pServer->pLaunchDebugNotification = NULL;
	}

	estrDestroy(&pServer->pMuteAlerts);
	estrDestroy(&pServer->pUnMuteAlerts);

	eaDestroyStruct(&pServer->ppThingsToDoOnCrash, parse_RemoteCommandGroup);
	StructReset(parse_SimpleServerLog, &pServer->simpleLog);
	ReleaseServer(pServer);

}



int ControllerClientDisconnect(NetLink* link, TrackedServerState **ppServer)
{
	if (*ppServer)
	{
		//printf("A client disconnected\n");
		StopTrackingServer(*ppServer, "Link disconnected", false, false);
	}

	return 1;
}

// void AddTrustedIPsToCommandLine(char **ppCommandLine)
// {
// 	int i;
// 
// 	for (i=0; i < giNumMachines; i++)
// 	{
// 		TrackedMachineState *pMachine = &gTrackedMachines[i];
// 
// 		estrConcatf(ppCommandLine, " -AddTrustedIP %u -AddTrustedIP %u ", 
// 			pMachine->IP, pMachine->iPublicIP);
// 	}
// }









		




void KillAllServers()
{
	int i;

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (!gServerTypeInfo[i].bDontKillDuringKillAll)
		{

			TrackedServerState *pServer = gpServersByType[i];

			while (pServer)
			{
				pServer->bKilledIntentionally = true;

				if (pServer->pLink)
				{
					Packet *pak = pktCreate(pServer->pLink, FROM_CONTROLLER_KILLYOURSELF);
					pktSend(&pak);

					pServer = pServer->pNext;
				}
				else
				{
					TrackedServerState *pNext = pServer->pNext;
	
					if (pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
					{
						KillServer(pServer, "KillAll");
					}
					StopTrackingServer(pServer, "KillAll", false, false);
					pServer = pNext;
				}

			}
		}
		else
		{
			TrackedServerState *pServer = gpServersByType[i];

			while (pServer)
			{
				TrackedServerState *pNext = pServer->pNext;

				if (pServer->pLink)
				{
					Packet *pak = pktCreate(pServer->pLink, FROM_CONTROLLER_INCCOOKIE);
					pktSend(&pak);
				}
				else
				{
					//a server was started but does not yet really exist, kill it
					if (pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
					{
						KillServer(pServer, "KillAll");
					}
					StopTrackingServer(pServer, "KillAll", false, false);
				}

				pServer = pNext;
			}
		}

	}

	gServerLibState.antiZombificationCookie++;
}

bool CheckOKToCreateServer(TrackedMachineState *pMachine, GlobalType eContainerType, U32 iContainerID, char **ppErrorString)
{
	TrackedServerState *pServer = FindServerFromID(eContainerType, iContainerID);

	if (!pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
	{
		estrPrintf(ppErrorString, "No launcher on machine %s", pMachine->machineName);
		return false;
	}

	//servers can not have container IDs greater than LOWEST_SPECIAL_CONTAINERID
	if (iContainerID >= LOWEST_SPECIAL_CONTAINERID)
	{
		estrPrintf(ppErrorString, "Container ID >= LOWEST_SPECIAL_CONTAINERID requested");
		return false;
	}

	if (pServer)
	{
		estrPrintf(ppErrorString, "server witht his type/ID already exists");
		return false;
	}

	if (gServerTypeInfo[eContainerType].bIsUnique)
	{
		if (gpServersByType[eContainerType])
		{
			estrPrintf(ppErrorString, "This server type is unique, but one already exists");
			return false;
		}
	}

	if (gServerTypeInfo[eContainerType].bIsUniquePerMachine)
	{
		if(pMachine->pServersByType[eContainerType])
		{
			estrPrintf(ppErrorString, "This server type is unique-per-machine, but one already exists");
			return false;
		}

	}

	return true;
}


bool ShouldWindowActuallyBeHidden(enumVisibilitySetting eVisibility, GlobalType eContainerType)
{
	if (eVisibility)
	{
		return eVisibility == VIS_HIDDEN;
	}

	if (gServerTypeInfo[eContainerType].eVisibility_FromMCP)
	{
		return gServerTypeInfo[eContainerType].eVisibility_FromMCP == VIS_HIDDEN;
	}

	if (gServerTypeInfo[eContainerType].eVisibility_FromScript)
	{
		return gServerTypeInfo[eContainerType].eVisibility_FromScript == VIS_HIDDEN;
	}

	if (isProductionMode())
	{
		if (gServerTypeInfo[eContainerType].eVisibility_ProdMode)
		{
			return gServerTypeInfo[eContainerType].eVisibility_ProdMode == VIS_HIDDEN;
		}
	}
	else
	{
		if (gServerTypeInfo[eContainerType].eVisibility_DevMode)
		{
			return gServerTypeInfo[eContainerType].eVisibility_DevMode == VIS_HIDDEN;
		}
	}

	if (gServerTypeInfo[eContainerType].eVisibility)
	{
		return gServerTypeInfo[eContainerType].eVisibility == VIS_HIDDEN;
	}

	return false;
}


void DoTypeSpecificPreLaunchThingsOnMachine(TrackedMachineState *pMachine, GlobalType eContainerType)
{
	switch (eContainerType)
	{
	case GLOBALTYPE_BCNCLIENTSENTRY:
		{
			Packet *pPak = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_KILLALLBYNAME);
			pktSendString(pPak, "beaconizer.exe");
			pktSend(&pPak);
		}
	}
}


void DoActualLaunchingWithLauncher(TrackedServerState *pServer, TrackedMachineState *pMachine, char *pFullPath, U32 iExecutableCRC, U32 iContainerID, GlobalType eContainerType,  int iLowLevelIndex, bool bStartInDebugger,
								 char *pCommandLine, char *pWorkingDirectory, enumVisibilitySetting eVisibility, char *pSharedCommandLine)
{
	Packet *pak;

	static char *pAccountServerIPString = NULL;
	static char *pTicketTrackerIPString = NULL;
	static char *pTestServerIPCmd = NULL;
	ServerLaunchRequest *pRequest = StructCreate(parse_ServerLaunchRequest);

	DoTypeSpecificPreLaunchThingsOnMachine(pMachine, eContainerType);

	if (!pAccountServerIPString)
	{
		estrPrintf(&pAccountServerIPString, "%s", makeIpStr(ipFromString(getAccountServer())));
		estrPrintf(&pTicketTrackerIPString, "%s", makeIpStr(ipFromString(getTicketTracker())));
	}

	if (gpServersByType[GLOBALTYPE_TESTSERVER])
	{
		estrPrintf(&pTestServerIPCmd, "-SetTestServer %s", makeIpStr(GetIPToUse(gpServersByType[GLOBALTYPE_TESTSERVER]->pMachine)));
	}

	pktCreateWithCachedTracker(pak, pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_STARTPROCESS);

	pRequest->pExeName = strdup(pFullPath);
	pRequest->iContainerID = iContainerID;
	pRequest->eContainerType = eContainerType;
	pRequest->iLowLevelControllerIndex = iLowLevelIndex;
	pRequest->iAntiZombificationCookie = gServerLibState.antiZombificationCookie;
	pRequest->bStartInDebugger = bStartInDebugger | gServerTypeInfo[eContainerType].bMCPRequestsStartAllInDebugger;
	pRequest->bStartHidden = ShouldWindowActuallyBeHidden(eVisibility, eContainerType);
	pRequest->iExecutableCRC = iExecutableCRC;

	if (gpServersByType[GLOBALTYPE_TRANSACTIONSERVER])
	{
		pRequest->iTransactionServerIPToSpecify = GetIPToUse(gpServersByType[GLOBALTYPE_TRANSACTIONSERVER]->pMachine);
	}

	if (gpServersByType[GLOBALTYPE_LOGSERVER])
	{
		pRequest->iLogServerIPToSpecify = GetIPToUse(gpServersByType[GLOBALTYPE_LOGSERVER]->pMachine);
	}

	estrPrintf(&pRequest->pCommandLine, " -SetAccountServer %s -SetTicketTracker %s %s -SetErrorTracker %s %s - %s - %s - %s - %s - ", 
		pAccountServerIPString, 
		pTicketTrackerIPString,
		pTestServerIPCmd ? pTestServerIPCmd : "",
		getErrorTracker(), 
		gbLeaveCrashesUpForever || gServerTypeInfo[eContainerType].bLeaveCrashesUpForever ? " -LeaveCrashesUpForever 1 " : "",
		GetGlobalSharedCommandLine(), pSharedCommandLine ? pSharedCommandLine : "", pCommandLine ? pCommandLine : "",
		GetPrintfForkingCommand(eContainerType, iContainerID)
		); 	

	pRequest->pWorkingDirectory = strdup(pWorkingDirectory ? pWorkingDirectory : "");

	ParserSendStruct(parse_ServerLaunchRequest, pak, pRequest);
	pktSend(&pak);

	pRequest->pMachineName = pMachine->machineName;
	pRequest->pLaunchReason = pServer->launchReason;
	servLogWithStruct(LOG_SERVERLAUNCH, "ServerLaunch", pRequest, parse_ServerLaunchRequest);

	StructDestroy(parse_ServerLaunchRequest, pRequest);


	SimpleServerLog_AddUpdate(pServer, "Server launched on machine %s", pMachine->machineName);

	if (ControllerScripting_IsRunning() && pServer->eContainerType == GLOBALTYPE_OBJECTDB)
	{
		Controller_SetStartupStatusString("ObjectDB", "Launched");
	}
}

char *GetExecutableNameForMachine(GlobalType eType, TrackedMachineState *pMachine, U32 *piOutCRC, char *pDirName)
{
	if (estrLength(&gServerTypeInfo[eType].pOverrideExeName))
	{
		if (!gServerTypeInfo[eType].iOverrideExeCRC)
		{
			gServerTypeInfo[eType].iOverrideExeCRC = Controller_GetCRCFromExeName(pDirName, gServerTypeInfo[eType].pOverrideExeName);
		}

		*piOutCRC = gServerTypeInfo[eType].iOverrideExeCRC;
		return gServerTypeInfo[eType].pOverrideExeName;
	}

	assert(eType >= 0 && eType < GLOBALTYPE_MAXTYPES);


	if (pMachine->bIsX64 && gbLaunch64BitWhenPossible)
	{

		if (gServerTypeInfo[eType].pExecutableName64_FrankenBuilt)
		{
			*piOutCRC = gServerTypeInfo[eType].iExecutable64FrankenBuiltCRC;
			return gServerTypeInfo[eType].pExecutableName64_FrankenBuilt;
		}

		if (gServerTypeInfo[eType].executableName64_original[0])
		{
			if (gServerTypeInfo[eType].eFrankenState == FRANKENSTATE_32BIT)
			{
				CRITICAL_NETOPS_ALERT("BAD_FRANKEN_LAUNCH", "Launching a %s on machine %s. This is a 64-bit launch, but a 32-bit-only frankenbuild of this exe type has occurred. This may or may not be fatal, but is potentially alarming",
					GlobalTypeToName(eType), pMachine->machineName);
			}

			if (!gServerTypeInfo[eType].iExecutable64OriginalCRC)
			{
				gServerTypeInfo[eType].iExecutable64OriginalCRC = Controller_GetCRCFromExeName(pDirName, gServerTypeInfo[eType].executableName64_original);
			}

			*piOutCRC = gServerTypeInfo[eType].iExecutable64OriginalCRC;
			return gServerTypeInfo[eType].executableName64_original;
		}
	}

	if (gServerTypeInfo[eType].eFrankenState == FRANKENSTATE_64BIT)
	{
		CRITICAL_NETOPS_ALERT("BAD_FRANKEN_LAUNCH", "Launching a %s on machine %s. This is a 32-bit launch, but a 64-bit-only frankenbuild of this exe type has occurred. This may or may not be fatal, but is potentially alarming",
			GlobalTypeToName(eType), pMachine->machineName);
	}

	if (gServerTypeInfo[eType].pExecutableName32_FrankenBuilt)
	{
		*piOutCRC = gServerTypeInfo[eType].iExecutable32FrankenBuiltCRC;
		return gServerTypeInfo[eType].pExecutableName32_FrankenBuilt;
	}

	if (!gServerTypeInfo[eType].executableName32_original[0])
	{
		AssertOrAlert("NO_EXE_NAME", "Trying to launch a %s on %s, but we don't what exe name to use. Presumably you need to edit src/data/server/ControllerServerSetup.txt and add executableName32",
			GlobalTypeToName(eType), pMachine->machineName);
	}

	if (!gServerTypeInfo[eType].iExecutable32OriginalCRC)
	{
		gServerTypeInfo[eType].iExecutable32OriginalCRC = Controller_GetCRCFromExeName(pDirName, gServerTypeInfo[eType].executableName32_original);
	}

	*piOutCRC = gServerTypeInfo[eType].iExecutable32OriginalCRC;

	return gServerTypeInfo[eType].executableName32_original;
}

void CheckForOutOfDate64BitVersion(GlobalType eContainerType, char *pFullPath)
{
	static char *p32BitName = NULL;
	static char *p64BitName = NULL;
	static char *p32BitNameFD = NULL;
	static char *p64BitNameFD = NULL;

	U32 modTime32;
	U32 modTime64;

	U32 modTime32FD;
	U32 modTime64FD;

	estrCopy2(&p32BitName, pFullPath);
	estrCopy2(&p64BitName, pFullPath);

	//the full path will be 64-bit or 32-bit already depending on what mode we're in. Whichever it is,
	//replace the other one
	if (gbLaunch64BitWhenPossible)
	{
		estrReplaceOccurrences(&p32BitName, gServerTypeInfo[eContainerType].executableName64_original, gServerTypeInfo[eContainerType].executableName32_original);
	}
	else
	{
		estrReplaceOccurrences(&p64BitName, gServerTypeInfo[eContainerType].executableName32_original, gServerTypeInfo[eContainerType].executableName64_original);
	}

	estrTrimLeadingAndTrailingWhitespace(&p32BitName);
	estrTruncateAtFirstOccurrence(&p32BitName, ' ');

	estrTrimLeadingAndTrailingWhitespace(&p64BitName);
	estrTruncateAtFirstOccurrence(&p64BitName, ' ');

	estrCopy(&p32BitNameFD, &p32BitName);
	estrCopy(&p64BitNameFD, &p64BitName);
	estrReplaceOccurrences(&p32BitNameFD, ".exe", "FD.exe");
	estrReplaceOccurrences(&p64BitNameFD, ".exe", "FD.exe");


	if (!((fileExists(p32BitName) || fileExists(p32BitNameFD) && (fileExists(p64BitName) || fileExists(p64BitNameFD)))))
	{
		return;
	}

	modTime32 = fileLastChangedSS2000(p32BitName);
	modTime64 = fileLastChangedSS2000(p64BitName);
	modTime32FD = fileLastChangedSS2000(p32BitNameFD);
	modTime64FD = fileLastChangedSS2000(p64BitNameFD);

	if (modTime32FD > modTime32)
	{
		modTime32 = modTime32FD;
		estrCopy(&p32BitName, &p32BitNameFD);
	}

	if (modTime64FD > modTime64)
	{
		modTime64 = modTime64FD;
		estrCopy(&p64BitName, &p64BitNameFD);
	}

	if (gbLaunch64BitWhenPossible)
	{
		if (modTime32 > modTime64 + 1200)
		{
			Errorf("You are about to launch %s, which is significantly older than %s. Are you sure you want to do this?",
				p64BitName, p32BitName);
		}
	}
	else
	{
		if (modTime64 > modTime32 + 1200)
		{
			Errorf("You are about to launch %s, which is significantly older than %s. Are you sure you want to do this?",
				p32BitName, p64BitName);
		}
	}
}

static void ModifyAppTypeCommandLine(TrackedMachineState *pMachine, char **estrFullCommandLine, GlobalType eContainerType)
{
	//type-specific command line stuff
	switch (eContainerType)
	{
	case GLOBALTYPE_CLIENT:
		//locally-launched gameclients (debug/dev only) get -ConnectToController
		if (pMachine->bIsLocalHost)
			estrConcatf(estrFullCommandLine, " -ConnectToController");

	xcase GLOBALTYPE_TESTCLIENT:
		{
			//make sure that all the types which run off ClientControllerLib get an exec directory
			//Test Clients also need all Login Server IPs
			TrackedServerState *pLoginServer = gpServersByType[GLOBALTYPE_LOGINSERVER];
			estrConcatf(estrFullCommandLine, " -ExecDirectoryForClient %s", 
				estrLength(&gServerTypeInfo[eContainerType].pOverrideLaunchDir) ? gServerTypeInfo[eContainerType].pOverrideLaunchDir : gExecutableDirectory);

			while(pLoginServer)
			{
				estrConcatf(estrFullCommandLine, " -server %s", pLoginServer->pMachine->machineName);
				pLoginServer = pLoginServer->pNext;
			}
		}

	xcase GLOBALTYPE_HEADSHOTSERVER:
	case GLOBALTYPE_CLIENTBINNER: // fall-through intentional
		estrConcatf(estrFullCommandLine, " -ExecDirectoryForClient %s ", estrLength(&gServerTypeInfo[eContainerType].pOverrideLaunchDir) ? gServerTypeInfo[eContainerType].pOverrideLaunchDir : gExecutableDirectory);

	xcase GLOBALTYPE_MAPMANAGER:
		if (siContainerIDRangeMin)
		{
			estrConcatf(estrFullCommandLine, " -SetGameServerIDRange %d %d ", siContainerIDRangeMin, siContainerIDRangeMax);
		}

	// FALL THROUGH!!!!!
	case GLOBALTYPE_RESOURCEDB: 
		if (gPatchName[0])
			estrConcatf(estrFullCommandLine, " -DynamicPatchName %s ", gPatchName);

	xcase GLOBALTYPE_OBJECTDB:
	case GLOBALTYPE_CLONEOBJECTDB:
	case GLOBALTYPE_CLONEOFCLONE:
		if (siContainerIDRangeMin)
		{
			estrConcatf(estrFullCommandLine, " -SetBaseContainerIDs %d %d ", siContainerIDRangeMin - 1, siContainerIDRangeMax + 1);
		}


	xcase GLOBALTYPE_GAMESERVER:
		{
			int i;

			for (i=0;i < eaSize(&gppGSLBannedCommands); i++)
			{
				estrConcatf(estrFullCommandLine, " -BanCommand %s ", gppGSLBannedCommands[i]);
			}

			if (gbGameServerInactivityTimeoutMinutesSet)
			{
				estrConcatf(estrFullCommandLine, " -InactivityTimeoutMinutes %d ", giGameServerInactivityTimeoutMinutes);
			}
		}

	xcase GLOBALTYPE_GLOBALCHATSERVER:
		if (isDevelopmentMode() && accountServerWasSet())
		{
			const char *pASAddress = getAccountServer();
			if (stricmp(pASAddress, "localhost") == 0 ||
				stricmp(pASAddress, makeIpStr(getHostPublicIp())) == 0)
			{
				estrConcatf(estrFullCommandLine, " -ConnectToAccountServer ");
			}
		}

	}
}

TrackedServerState *RegisterNewServerAndMaybeLaunch(enumServerLaunchFlag eFlags, enumVisibilitySetting eVisibility, TrackedMachineState *pMachine, GlobalType eContainerType, U32 iContainerID, bool bStartInDebugger,
								 char *pInCommandLine, char *pWorkingDirectory, bool bSkipWaiting, int iTestClientLinkNum, AdditionalServerLaunchInfo *pAdditionalInfo, char *pReason, ...)
{
	TrackedServerState *pServerState;
	char fullPath[MAX_PATH];
	char *pFullPath = fullPath;


	char *pFullCommandLine = NULL; //estring

	char *pErrorString = NULL;

	char *pReasonSprintfed = NULL;
	char **ppExtraCommandsFromAutoSettings = NULL;

	int i;
	U32 iExecutableCRC = 0;

	if (!pInCommandLine)
	{
		pInCommandLine = "";
	}

	if (!pMachine)
	{
		if (eFlags & LAUNCHFLAG_FAILURE_IS_FATAL)
		{
			assertmsgf(0, "No machine found when trying to launch %s", GlobalTypeToName(eContainerType));
		}

		if (eFlags & LAUNCHFLAG_FAILURE_IS_ALERTABLE)
		{
			AssertOrAlert("SVR_LAUNCH_FAILURE", "No machine found when trying to launch %s", GlobalTypeToName(eContainerType));
		}
		return NULL;
	}


	VA_START(args, pReason);


	if (!CheckOKToCreateServer(pMachine, eContainerType, iContainerID, &pErrorString))
	{
		if (eFlags & LAUNCHFLAG_FAILURE_IS_FATAL)
		{
			assertmsg(0, pErrorString);
		}

		if (eFlags & LAUNCHFLAG_FAILURE_IS_ALERTABLE)
		{
			AssertOrAlert("SVR_LAUNCH_FAILURE", "Failed to launch critical server %s: %s", GlobalTypeToName(eContainerType), pErrorString);
		}

		estrDestroy(&pErrorString);
		return NULL;
	}

	/*if (bFailureIsNonFatal)
	{
		if (!CheckOKToCreateServer(pMachine, eContainerType, iContainerID, &pErrorString))
		{
			estrDestroy(&pErrorString);
			return NULL;
		}
	}
	else
	{
		if (!CheckOKToCreateServer(pMachine, eContainerType, iContainerID, &pErrorString))
		{
			assertmsg(0, pErrorString);
		}
	}*/

	//type-specific command line stuff
	estrCopy2(&pFullCommandLine, pInCommandLine ? pInCommandLine : "");
	ModifyAppTypeCommandLine(pMachine, &pFullCommandLine, eContainerType);

	if (gServerTypeInfo[eContainerType].bInformAboutShardLockStatus && gbShardIsLocked)
	{
		estrConcatf(&pFullCommandLine, " -ControllerReportsShardLocked ");
	}

	if (gServerTypeInfo[eContainerType].pInformMeAboutShardCluster_State)
	{
		ControllerShardCluster_AddToCommandLine(&pFullCommandLine);
	}

	if (gServerTypeInfo[eContainerType].bInformAboutGatewayLockStatus  && gbGatewayIsLocked)
	{
		estrConcatf(&pFullCommandLine, " -ControllerReportsGatewayLocked ");
	}

	if (gServerTypeInfo[eContainerType].bInformAboutAllowShardVersionMismatch && AllowShardVersionMismatch())
	{
		estrConcatf(&pFullCommandLine, " -ControllerReportsAllowShardVersionMismatch ");
	}

	if (gServerTypeInfo[eContainerType].bInformAboutNumGameServerMachines)
	{
		estrConcatf(&pFullCommandLine, " -SetNumGSMachines %d ", CountMachinesForServerType(GLOBALTYPE_GAMESERVER));
	}

	if (gServerTypeInfo[eContainerType].bInformAboutMTStatus)
	{
		estrConcatf(&pFullCommandLine, " -MicroTrans_ShardCategory %s", gpMTCategory);
	}

	if (ShardCommon_GetClusterName())
	{
		estrConcatf(&pFullCommandLine, " -ShardClusterName %s -ShardTypeInCluster %s ", ShardCommon_GetClusterName(), StaticDefineInt_FastIntToString(ClusterShardTypeEnum, ShardCluster_GetShardType()));
	}

	ppExtraCommandsFromAutoSettings = ControllerAutoSettings_GetCommandStringsForServerType(eContainerType);
	for (i=0; i < eaSize(&ppExtraCommandsFromAutoSettings); i++)
	{
		estrConcatf(&pFullCommandLine, " -%s%s", AUTOSETTING_CMDLINECOMMAND_PREFIX, ppExtraCommandsFromAutoSettings[i]);
	}

	if (!(pWorkingDirectory && pWorkingDirectory[0]) && estrLength(&gServerTypeInfo[eContainerType].pOverrideLaunchDir))
	{
		pWorkingDirectory = gServerTypeInfo[eContainerType].pOverrideLaunchDir;
	}

	if (eContainerType == GLOBALTYPE_ARBITRARYPROCESS)
	{
		pFullPath = pInCommandLine;
		estrClear(&pFullCommandLine);
	}
	else if (pWorkingDirectory && pWorkingDirectory[0])
	{
		sprintf(fullPath, "%s/%s", pWorkingDirectory, GetExecutableNameForMachine(eContainerType, pMachine, &iExecutableCRC, pWorkingDirectory));
	}
	else if (pMachine->bIsLocalHost && gServerTypeInfo[eContainerType].bLaunchFromCoreDirectory && gCoreExecutableDirectory[0])
	{
		pWorkingDirectory = gExecutableDirectory;
		sprintf(fullPath,"%s/%s",gCoreExecutableDirectory,GetExecutableNameForMachine(eContainerType, pMachine, &iExecutableCRC, gCoreExecutableDirectory));
	}
	else if (pMachine->bIsLocalHost && gExecutableDirectory[0])
	{
		pWorkingDirectory = gExecutableDirectory;
		sprintf(fullPath,"%s/%s",gExecutableDirectory,GetExecutableNameForMachine(eContainerType, pMachine, &iExecutableCRC, gExecutableDirectory));
	}
	else
	{
		strcpy(fullPath,GetExecutableNameForMachine(eContainerType, pMachine, &iExecutableCRC, "."));
	}

	//dev-mode only check to catch the easy-to-fall-into trap of forgetting to recompile 64 bit vs 32 bit executables
	if (isDevelopmentMode() && pMachine->bIsLocalHost && gServerTypeInfo[eContainerType].executableName64_original[0] && !g_isContinuousBuilder)
	{
		CheckForOutOfDate64BitVersion(eContainerType, fullPath);
	}

	pServerState = GetNewEmptyServer();

	pServerState->eContainerType = eContainerType;
	pServerState->iContainerID = iContainerID;
	pServerState->pMachine = pMachine;

	pServerState->iTestClientLinkNum = iTestClientLinkNum;

	pServerState->iScriptingIndex = sNextServerScriptingIndex[eContainerType]++;

	estrConcatfv(&pReasonSprintfed, pReason, args);

	VA_END();


	LinkAndInitServer(pServerState, pMachine, pReasonSprintfed, pAdditionalInfo);


	if (!ServerTypeIsReadyToLaunchOnMachine(eContainerType, pMachine, NULL) && !bSkipWaiting)
	{

		log_printf(LOG_SHARD, "Controller wants to start %s[%u] because \"%s\". Launch will be delayed due to dependencies",
			GlobalTypeToName(eContainerType), iContainerID, pReasonSprintfed);


		assert(eContainerType != GLOBALTYPE_ARBITRARYPROCESS);

		pServerState->bWaitingToLaunch = true;
		giNumWaitingServers++;
		pMachine->iWaitingServers++;

		pServerState->eVisibility = eVisibility;
		pServerState->bStartInDebugger = bStartInDebugger;
		estrCopy2(&pServerState->pFullPath, fullPath);
		estrCopy2(&pServerState->pCommandLine, pFullCommandLine?pFullCommandLine:"");
		estrCopy2(&pServerState->pWorkingDirectory, pWorkingDirectory ? pWorkingDirectory : "");
		pServerState->iExecutableCRC = iExecutableCRC;

		SimpleServerLog_AddUpdate(pServerState, "Server created because %s, launch waiting due to dependencies", pReasonSprintfed);

		estrDestroy(&pFullCommandLine);
		estrDestroy(&pReasonSprintfed);


		return pServerState;
		
	}


	log_printf(LOG_SHARD, "Controller about to start %s[%u] because \"%s\"",
		GlobalTypeToName(eContainerType), iContainerID, pReasonSprintfed);


	SimpleServerLog_AddUpdate(pServerState, "Server created because %s", pReasonSprintfed);

	estrDestroy(&pReasonSprintfed);


	estrPrintf(&gServerTypeInfo[eContainerType].pSharedCommandLine_Combined, " %s %s ",
		gServerTypeInfo[eContainerType].pSharedCommandLine_FromScript ? gServerTypeInfo[eContainerType].pSharedCommandLine_FromScript : "",
		gServerTypeInfo[eContainerType].pSharedCommandLine_FromMCP ? gServerTypeInfo[eContainerType].pSharedCommandLine_FromMCP : "");


	DoActualLaunchingWithLauncher(pServerState, pMachine, pFullPath, iExecutableCRC, iContainerID, eContainerType, pServerState->iLowLevelIndex, bStartInDebugger, pFullCommandLine, pWorkingDirectory, eVisibility, gServerTypeInfo[eContainerType].pSharedCommandLine_Combined);

	SetKeepAliveStartTime(pServerState);

	estrDestroy(&pFullCommandLine);

	return pServerState;
}



void TellLauncherAboutExistingServer(TrackedMachineState *pMachine, GlobalType eContainerType, U32 iContainerID,
	U32 pid, int iLowLevelControllerIndex)
{
	Packet *pak;
	TrackedServerState *pServerState;
	pak = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_REGISTEREXISTINGPROCESS);

	PutContainerIDIntoPacket(pak, iContainerID);
	PutContainerTypeIntoPacket(pak, eContainerType);
	pktSendBits(pak, 32, iLowLevelControllerIndex);

	pktSendBitsPack(pak, 1, pid);

	//the exe comparison that the launcher does is just a substring, so the raw original 32 bit exe name should be fine here, 
	//it will match 64 bit, FD, or frankenbuilt EXEs just fine
	pktSendString(pak, gServerTypeInfo[eContainerType].executableName32_original);

	pktSend(&pak);

	if (!FindServerFromID(eContainerType, iContainerID))
	{

		pServerState = GetNewEmptyServer();


		pServerState->eContainerType = eContainerType;
		pServerState->iContainerID = iContainerID;

		pServerState->pMachine = pMachine;


		pServerState->iScriptingIndex = sNextServerScriptingIndex[eContainerType]++;

	

		LinkAndInitServer(pServerState, pMachine, "Unknown", NULL);
	}

}


void TellLauncherToIgnoreServer(TrackedServerState *pServer, int iSecsToRemainIgnored)
{
	Packet *pak;

	if (pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
	{
		pak = pktCreate(pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_IGNOREPROCESS);

		PutContainerIDIntoPacket(pak, pServer->iContainerID);
		PutContainerTypeIntoPacket(pak, pServer->eContainerType);
		pktSendBits(pak, 32, iSecsToRemainIgnored);

		pktSend(&pak);
	}
}


void KillServerByIDs(TrackedMachineState *pMachine, GlobalType eContainerType, U32 iContainerID, char *pReason)
{
	Packet *pak;

	if (pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
	{
		pak = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_KILLPROCESS);

		PutContainerIDIntoPacket(pak, iContainerID);
		PutContainerTypeIntoPacket(pak, eContainerType);

		pktSend(&pak);	
	}
}
void KillServer(TrackedServerState *pServer, char *pReason)
{
	if (pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
	{
		log_printf(LOG_SHARD, "Killing %s[%u] because: \"%s\"", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID, pReason);
		KillServerByIDs(pServer->pMachine, pServer->eContainerType, pServer->iContainerID, pReason);
	}
	else
	{
		StopTrackingServer(pServer, pReason, false, false);
	}
}


#define SECS_TO_WAIT_FOR_LAUNCHER (30 * (gbLaunch64BitWhenPossible ? 2 : 1))
void CreateLocalLauncher(void)
{
	char *pFullCommandLine = NULL;
	U32 iStartTime = timeSecondsSince2000();
	U32 pid;
	ContainerID iContainerID = GetFreeContainerID(GLOBALTYPE_LAUNCHER);

	loadstart_printf("Creating local launcher");

	estrStackCreate(&pFullCommandLine);
	
	estrPrintf(&pFullCommandLine, "\"%s/Launcher\" -SetDirToLaunchFrom \"%s\" -ContainerID %d -Cookie %u -SetErrorTracker %s %s %s -SetProductName %s %s - %s - ",
			fileCoreExecutableDir(),
			fileExecutableDir(),
			iContainerID, gServerLibState.antiZombificationCookie,
			getErrorTracker(),
			gbHideLocalLauncher ? "-hide" : "", gbStartLocalLauncherInDebugger ? "-WaitForDebugger" : "", GetProductName(), GetShortProductName(),
			pLauncherCommandLine ? pLauncherCommandLine : "");
	
	PutExtraStuffInLauncherCommandLine(&pFullCommandLine, iContainerID);

	pid = system_detach_with_fulldebug_fixup(pFullCommandLine, 1, gbHideLocalLauncher);

	estrDestroy(&pFullCommandLine);

	if (gbStartLocalLauncherInDebugger)
	{
		AttachDebugger(pid);
	}


	do
	{
		assertmsgf(timeSecondsSince2000() <= iStartTime + SECS_TO_WAIT_FOR_LAUNCHER, "DO NOT BE ALARMED BY THIS ASSERT! IT PROBABLY JUST MEANS THAT YOU HAVE TOO MUCH STUFF RUNNING RIGHT NOW OR SOMETHING!!!!!!! No response from local launcher after %d seconds... it either couldn't be created, or crashed, or is hitting some gimme slowdown or something", SECS_TO_WAIT_FOR_LAUNCHER);

		commMonitor(comm_controller);
		commMonitor(commDefault());
		
		Controller_PCLStatusMonitoringTick();
		SentryServerComm_Tick();

		if (sbNeedToKillAllServers)
		{
			KillAllServers();
			sbNeedToKillAllServers = false;
		}

		UpdateControllerTitle();
		serverLibOncePerFrame();
		Sleep(DEFAULT_SERVER_SLEEP_TIME);
	} 
	while (!spLocalMachine || !spLocalMachine->pServersByType[GLOBALTYPE_LAUNCHER]);



	if (sUIDOfMCPThatStartedMe)
	{
		TellLauncherAboutExistingServer(spLocalMachine,
			GLOBALTYPE_MASTERCONTROLPROGRAM, sUIDOfMCPThatStartedMe, sPIDOfMCPThatStartedMe, 0);
	}

	loadend_printf("done");
}



static BOOL CALLBACK bigGreenButtonDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	return FALSE;
}







void errorDialogController(HWND hwnd, char *str, char* title, char* fault, int highlight, void *userdata)
{
	if (ControllerScripting_IsRunning())
	{
		char logString[4096];

		sprintf(logString, "Error in step %s:\n%s\n", ControllerScripting_GetCurStepName(), str);

		ControllerScripting_LogString(logString, false, true);
	}
	else if (g_isContinuousBuilder)
	{
		//do nothing, error should already have been reported to CB
	}
	else if (IsConsoleWindowVisible() || !gpServersByType[GLOBALTYPE_MASTERCONTROLPROGRAM])
	{
		errorDialogInternal(hwnd, str, title, fault, highlight);
	}
	else
	{	
		char *pFullString = NULL;
		estrPrintf(&pFullString, "%s (reported by CONTROLLER)", str);
		SendErrorDialogToMCPThroughController(pFullString, title, fault, highlight);
		estrDestroy(&pFullString);
	}
}



bool ServerDependencyIsFulfilledForMachine(GlobalType eType, TrackedMachineState *pMachine, ServerDependency *pDependency)
{
	TrackedServerState *pWaitedOnServer;

	if (pDependency->bPerMachineDependency)
	{
		pWaitedOnServer = pMachine->pServersByType[pDependency->eTypeToWaitOn];

		if (pWaitedOnServer && pWaitedOnServer->bHasCrashed)
		{
			//multiplexer has crashed, kill it with launcher as fast as possible so new one can start up
			KillServerByIDs(pMachine, pWaitedOnServer->eContainerType, pWaitedOnServer->iContainerID, "Per machine dependency needs a new one of these, killing the old one");
			StopTrackingServer(pWaitedOnServer, "Per machine dependency needs a new one of these, killing the old one", true, true);
			return false;
		}
	}
	else
	{
		pWaitedOnServer = gpServersByType[pDependency->eTypeToWaitOn];
	}

	if (!pWaitedOnServer)
	{
		return false;
	}

	if (pDependency->stateToWaitFor[0] == 0)
	{
		return true;
	}

	if (strstr(pWaitedOnServer->stateString, pDependency->stateToWaitFor))
	{
		return true;
	}

	return false;
}
	
bool DependencyIsActive(ServerDependency *pDependency)
{
	if (pDependency->bProductionModeOnlyDependency && !isProductionMode())
	{
		return false;
	}

	if (pDependency->bBuilderOnlyDependency)
	{
		if (!(g_isContinuousBuilder || isDevelopmentMode() && ControllerScripting_IsRunning()))
		{
			return false;
		}
	}

	return true;
}

bool ServerTypeIsReadyToLaunchOnMachine(GlobalType eType, TrackedMachineState *pMachine, GlobalType *pTypeBeingWaitedOn)
{
	int iNumDependencies = eaSize(&gServerTypeInfo[eType].ppServerDependencies);
	int i;

	for (i=0; i < iNumDependencies; i++)
	{
		if (DependencyIsActive(gServerTypeInfo[eType].ppServerDependencies[i]))
		{
			if (!ServerDependencyIsFulfilledForMachine(eType, pMachine, gServerTypeInfo[eType].ppServerDependencies[i]))
			{
				if (pTypeBeingWaitedOn)
				{
					*pTypeBeingWaitedOn = gServerTypeInfo[eType].ppServerDependencies[i]->eTypeToWaitOn;
				}
				return false;
			}
		}
	}

	return true;
}

bool ServerTypeIsReadyToLaunchOnMachines(GlobalType eType, TrackedMachineState **ppMachines)
{
	int iNumMachines = eaSize(&ppMachines);
	int i;

	for (i=0; i < iNumMachines; i++)
	{
		if (!ServerTypeIsReadyToLaunchOnMachine(eType, ppMachines[i], NULL))
		{
			return false;
		}
	}

	return true;
}

/*
int CalculateServerLaunchingWeight(TrackedMachineState *pMachine, ServerLaunchingWeights *pWeights)
{
	int iNumOtherGameServers = 0;
	int i;

	for (i=1; i < ZMTYPE_COUNT; i++)
	{
		if (i != ZMTYPE_STATIC)
		{
			iNumOtherGameServers += pMachine->iGameServersByType[i];
		}
	}

	return pMachine->iGameServersByType[ZMTYPE_STATIC] * pWeights->iStaticMapWeight 
		+ iNumOtherGameServers * pWeights->iOtherMapWeight
		+ (pMachine->iTotalServers - pMachine->iGameServersByType[ZMTYPE_STATIC] - iNumOtherGameServers) * pWeights->iOtherServerWeight
		+ pMachine->performance.cpuLast60 * pWeights->iCPUWeight
		+ pMachine->performance.iFreeRAM * pWeights->iPhysicalGigsFreeWeight / (1024 * 1024 * 1024)
	+ pMachine->performance.iAvailVirtual * pWeights->iVirtualGigsFreeWeight / (1024 * 1024 * 1024);
}
*/

//under new weighting system, bigger is better. Under old, bigger is smaller

float Controller_FindServerLaunchingWeight(TrackedMachineState *pMachine, GlobalType eContainerType, AdditionalServerLaunchInfo *pAdditionalInfo, char **ppDescString)
{
	if (gbUseNewWeightingSystem)
	{ 
		float fFPSComponent = 100.0f - MAX(pMachine->performance.cpuUsage, pMachine->performance.cpuLast60);
        // the use of 3000.0f in this expression forces the subtraction to be done as a float, rather than as a U64.  This avoids an unsigned underflow, which was
        //  causing fRamComponent to be very large.
		float fRAMComponent = (pMachine->performance.iFreeRAM / (1024 * 1024) - 3000.0f) * gGameServerLaunchingConfig.fFreeMegsToCPUScaleFactor;
	
		float fLaunchWeightComponent = ((float)pMachine->iGameServerLaunchWeight) / (pMachine->performance.iNumProcessors ? pMachine->performance.iNumProcessors : 1);

		float fRetVal;

		fRetVal = MIN(fFPSComponent, fRAMComponent) - fLaunchWeightComponent - pMachine->iServersByType[GLOBALTYPE_TESTCLIENT];

		estrPrintf(ppDescString, "CPU:%d,%d. Megs: %d. LaunchWeight: %f. TOTAL: %f", (int)(pMachine->performance.cpuUsage), (int)(pMachine->performance.cpuLast60), (int)(pMachine->performance.iFreeRAM / (1024 * 1024)), 
			fLaunchWeightComponent, fRetVal);

		return fRetVal;
	}
	else
	{
		float fFPSComponent = pMachine->performance.cpuUsage * gCPULaunchWeight;
		float fPhysComponent = pMachine->performance.iFreeRAM / (1024 * 1024) * gFreeMegsPhysRamLaunchWeight;
		float fVirtComponent = pMachine->performance.iAvailVirtual / (1024 * 1024) * gFreeMegsVirtRamLaunchWeight;
		return pMachine->iGameServerLaunchWeight + pMachine->iServersByType[GLOBALTYPE_TESTCLIENT] + 
			fFPSComponent + fPhysComponent + fVirtComponent;
	}
}

static int siRAMUsagePercentBeforeGSLaunchAlert = 80;
//if non-zero, then alert whenever launching a GS on a machine with more than this percent of RAM used
AUTO_CMD_INT(siRAMUsagePercentBeforeGSLaunchAlert, RAMUsagePercentBeforeGSLaunchAlert) ACMD_CONTROLLER_AUTO_SETTING(GsLaunchConfig); 

static int siCPUUsagePercentBeforeGSLaunchAlert = 90;
//if non-zero, then alert whenever launching a GS on a machine with more than this percent of CPU used
AUTO_CMD_INT(siCPUUsagePercentBeforeGSLaunchAlert, CPUUsagePercentBeforeGSLaunchAlert) ACMD_CONTROLLER_AUTO_SETTING(GsLaunchConfig); 

static int siGSFPSBeforeGSLaunchAlert = 15;
//if non-zero, then alert whenever launching a GS on a machine with GS FPS lower than this (CURRENTLY DISABLED IN CODE)
AUTO_CMD_INT(siGSFPSBeforeGSLaunchAlert, GSFPSBeforeGSLaunchAlert) ACMD_CONTROLLER_AUTO_SETTING(GsLaunchConfig); 

static char *spMachineExpressionForGSLaunchAlert = NULL;
//if set, then this is an expression that is applied to the machine whenever a GS is launched, and alerts if it is true
AUTO_CMD_ESTRING(spMachineExpressionForGSLaunchAlert, MachineExpressionForGSLaunchAlert) ACMD_CONTROLLER_AUTO_SETTING(GsLaunchConfig); 


void CheckForGameServerLaunchingAlerts(TrackedMachineState *pMachine)
{
	if (siRAMUsagePercentBeforeGSLaunchAlert)
	{
		S64 iFreeRAM = pMachine->performance.iFreeRAM;
		S64 iTotalRAM = pMachine->performance.iTotalRAM;

		//be a bit careful in case for some reason we're launching on a machine before we've gotten any perf info from it
		S64 iUsedRamPercent = iTotalRAM ? (100 - iFreeRAM * 100 / iTotalRAM) : 0;


		if (iUsedRamPercent >= siRAMUsagePercentBeforeGSLaunchAlert)
		{
			CRITICAL_NETOPS_ALERT("GS_LAUNCH_EXCEEDS_RAM", "A gameserver is being launched on machine %s, which has %"FORM_LL"d percent RAM usage, exceeding %d as set by RAMUsagePercentBeforeGSLaunchAlert AUTO_SETTING (which you can set to 0 to disable this)",
				pMachine->machineName, iUsedRamPercent, siRAMUsagePercentBeforeGSLaunchAlert);
		}
	}

	if (siCPUUsagePercentBeforeGSLaunchAlert)
	{
		int iCPU = MAX(pMachine->performance.cpuUsage, pMachine->performance.cpuLast60);
		if (iCPU > siCPUUsagePercentBeforeGSLaunchAlert)
		{
			CRITICAL_NETOPS_ALERT("GS_LAUNCH_EXCEEDS_CPU", "A gameserver is being launched on machine %s, which has %d percent CPU usage, exceeding %d as set by CPUUsagePercentBeforeGSLaunchAlert AUTO_SETTING (which you can set to 0 to disable this)",
				pMachine->machineName, iCPU, siCPUUsagePercentBeforeGSLaunchAlert);
		}
	}

/*	if (siGSFPSBeforeGSLaunchAlert)
	{
		if (pMachine->performance.iWeighted_GS_FPS && pMachine->performance.iWeighted_GS_FPS < siGSFPSBeforeGSLaunchAlert)
		{
			CRITICAL_NETOPS_ALERT("GS_LAUNCH_EXCEEDS_FPS", "A gameserver is being launched on machine %s, which has %d GS FPS, lower than %d as set by GSFPSBeforeGSLaunchAlert AUTO_SETTING (which you can set to 0 to disable this)",
				pMachine->machineName, pMachine->performance.iWeighted_GS_FPS, siGSFPSBeforeGSLaunchAlert);

		}
	}*/

	if (estrLength(&spMachineExpressionForGSLaunchAlert))
	{
		int iAnswer;

		exprSimpleEvaluateWithVariable(spMachineExpressionForGSLaunchAlert, parse_TrackedMachineState, pMachine, iAnswer);

		if (iAnswer)
		{
			CRITICAL_NETOPS_ALERT("GS_LAUNCH_MACHINE_EXPR", "A gameserver is being launched on machine %s, which matches expression \"%s\", as specified in spMachineExpressionForGSLaunchAlert",
				pMachine->machineName, spMachineExpressionForGSLaunchAlert);
		}
	}
	
}


void FindAllMachinesForServerType(GlobalType eContainerType, TrackedMachineState ***pppOutMachines)
{
	int i;

	for (i = 0; i < giNumMachines; i++)
	{
		//-----------------------IDENTICAL CHECK OCCURS IN ControlleMachineGroups_FindMachineForGameServerLaunch
		if (gTrackedMachines[i].pServersByType[GLOBALTYPE_LAUNCHER] && gTrackedMachines[i].canLaunchServerTypes[eContainerType].eCanLaunch != CAN_NOT_LAUNCH
		&& !gTrackedMachines[i].bIsLocked && !DynHoggPruningActiveForMachine(&gTrackedMachines[i]))
		{
			eaPush(pppOutMachines, &gTrackedMachines[i]);
		}
	}
}


TrackedMachineState *FindDefaultMachineForTypeInIPRange_Internal(GlobalType eContainerType, const char *pCategory, U32 iMinIP, U32 iMaxIP, AdditionalServerLaunchInfo *pAdditionalInfo)
{
	int i;
	float fCurBestWeight;
	TrackedMachineState *pCurBestMachine = NULL;

	char *pLogString = NULL;
	char *pCommentString = NULL;

	if (gbUseNewWeightingSystem)
	{
		fCurBestWeight = -1000000000000.0f;
	}
	else
	{
		fCurBestWeight = 10000000000000.0f;
	}

	assert(eContainerType >= 0 && eContainerType < GLOBALTYPE_MAXTYPES);

	estrPrintf(&pLogString, "(Tick %"FORM_LL"d) About to try to pick a machine to launch a server of type %s, category %s. ",
		giControllerTick, GlobalTypeToName(eContainerType), pCategory ? pCategory : "(unspecified)");

	if (eContainerType == GLOBALTYPE_GAMESERVER && gbControllerMachineGroups_SystemIsActive)
	{
		estrConcatf(&pLogString, "Sending choice off to MachineGroup code. ");

		pCurBestMachine = ControlleMachineGroups_FindMachineForGameServerLaunch(pCategory, pAdditionalInfo, &pLogString, &fCurBestWeight);
	}
	else
	{
		for (i = 0; i < giNumMachines; i++)
		{
			//-----------------------IDENTICAL CHECK OCCURS IN ControlleMachineGroups_FindMachineForGameServerLaunch
			if (gTrackedMachines[i].pServersByType[GLOBALTYPE_LAUNCHER] && gTrackedMachines[i].canLaunchServerTypes[eContainerType].eCanLaunch != CAN_NOT_LAUNCH
			&& (iMinIP == 0 || (iMinIP <= gTrackedMachines[i].IP && iMaxIP >= gTrackedMachines[i].IP))
			&& (!pCategory || eaFind(&gTrackedMachines[i].canLaunchServerTypes[eContainerType].ppCategories, pCategory) != -1)
			&& !gTrackedMachines[i].bIsLocked && !DynHoggPruningActiveForMachine(&gTrackedMachines[i]))
			//-----------------------IDENTICAL CHECK OCCURS IN ControlleMachineGroups_FindMachineForGameServerLaunch
			{
				if (isProductionMode() && !g_isContinuousBuilder && eContainerType == GLOBALTYPE_GAMESERVER && !Controller_MachineIsLegalForGameServerLaunch(&gTrackedMachines[i]))
				{
					estrConcatf(&pLogString, "Machine %s: CPU: %d,%d. Megs: %d. BELOW CUTOFF\n", gTrackedMachines[i].machineName,
						(int)(gTrackedMachines[i].performance.cpuUsage), (int)(gTrackedMachines[i].performance.cpuLast60), (int)(gTrackedMachines[i].performance.iFreeRAM / (1024 * 1024)));
				}
				else
				{
					float fCurWeight = Controller_FindServerLaunchingWeight(&gTrackedMachines[i], eContainerType, pAdditionalInfo, &pCommentString);
		
					fCurWeight += gTrackedMachines[i].canLaunchServerTypes[eContainerType].iPriority * sfAdditionalLaunchWeightPerCanLaunchPriority;

					if (pCommentString)
					{
						estrConcatf(&pLogString, "Machine %s: %s\n",
							gTrackedMachines[i].machineName,  pCommentString);
					}
					else
					{
						estrConcatf(&pLogString, "Machine %s: %f\n",
							gTrackedMachines[i].machineName,  fCurWeight);
					}

					if (gbUseNewWeightingSystem)
					{
						if (fCurWeight > fCurBestWeight)
						{
							fCurBestWeight = fCurWeight;
							pCurBestMachine = &gTrackedMachines[i];
						}
					}
					else
					{
						if (fCurWeight < fCurBestWeight)
						{
							fCurBestWeight = fCurWeight;
							pCurBestMachine = &gTrackedMachines[i];
						}
					}
				}
			}
	/*		else
			{
				estrConcatf(&pLogString, "Machine %s doesn't support this type/category  ", gTrackedMachines[i].machineName);
			}*/
		}
	}

	if (pCurBestMachine)
	{
		estrConcatf(&pLogString, "Picked machine %s", pCurBestMachine->machineName);

		if (eContainerType == GLOBALTYPE_GAMESERVER && gbControllerMachineGroups_SystemIsActive
			&& fCurBestWeight < gfLaunchWeightWhichTriggersMachineGroupRequest)
		{
		
			ControllerMachineGroups_RequestGroupActivation("We wanted to launch a GS, settled on %s (CPU: %d,%d. Megs: %d), but its launch weight of %f is below our acceptable cutoff of %f, so we need more machines if possible",
				pCurBestMachine->machineName,
				(int)(pCurBestMachine->performance.cpuUsage), (int)(pCurBestMachine->performance.cpuLast60), 
				(int)(pCurBestMachine->performance.iFreeRAM / (1024 * 1024)),
				fCurBestWeight, gfLaunchWeightWhichTriggersMachineGroupRequest);
		}

		if (eContainerType == GLOBALTYPE_GAMESERVER)
		{
			CheckForGameServerLaunchingAlerts(pCurBestMachine);
		}
	}
	else
	{
		estrConcatf(&pLogString, "No machine found");
	}



	log_printf(LOG_CONTROLLER, "%s", pLogString);

	estrDestroy(&pLogString);
	estrDestroy(&pCommentString);

	return pCurBestMachine;

}

bool Controller_MachineIsLegalForGameServerLaunch(TrackedMachineState *pMachine)
{
	static int siNumGSMachines = 0;

	if (!siNumGSMachines)
	{
		siNumGSMachines = CountMachinesForServerType(GLOBALTYPE_GAMESERVER);
	}

	//so that local production shards don't default to not being able to launch gameservers.
	if (siNumGSMachines <= 1)
	{
		return true;
	}

	if (gGameServerLaunchingConfig.fMinFreeRAMPercentCutoff)
	{
		if (((float)pMachine->performance.iFreeRAM / (float)pMachine->performance.iTotalRAM) * 100 < gGameServerLaunchingConfig.fMinFreeRAMPercentCutoff)
		{
			return false;
		}
	}

	if (gGameServerLaunchingConfig.fMinFreeRAMMegsCutoff)
	{
		if (pMachine->performance.iFreeRAM / (1024 * 1024) < gGameServerLaunchingConfig.fMinFreeRAMMegsCutoff)
		{
			return false;
		}
	}

	if (gGameServerLaunchingConfig.fMinFreeCPUCutoff)
	{
		if ((100 - pMachine->performance.cpuLast60) < gGameServerLaunchingConfig.fMinFreeCPUCutoff)
		{
			return false;
		}
	}

	//this machine passes
	return true;
}





//we always first try to find a machine that fits our category. If we can't, then we try to find one irrespective of
//category. If we can't, then we assert.
TrackedMachineState *FindDefaultMachineForTypeInIPRange(GlobalType eContainerType, char *pCategory, U32 iMinIP, U32 iMaxIP, AdditionalServerLaunchInfo *pAdditionalInfo)
{
	TrackedMachineState *pRetVal;

	if (pCategory && pCategory[0])
	{
		pRetVal = FindDefaultMachineForTypeInIPRange_Internal(eContainerType, allocAddString(pCategory), iMinIP, iMaxIP, pAdditionalInfo);

		if (pRetVal)
		{
			return pRetVal;
		}
	}

	pRetVal = FindDefaultMachineForTypeInIPRange_Internal(eContainerType, NULL, iMinIP, iMaxIP, pAdditionalInfo);

	return pRetVal;
}




void DoAnyDependencyAutoLaunchingForServerTypeOnMachine(GlobalType eType, TrackedMachineState *pMachine)
{
	int iDependencyNum;

	for (iDependencyNum = 0; iDependencyNum < eaSize(&gServerTypeInfo[eType].ppServerDependencies); iDependencyNum++)
	{
		ServerDependency *pDependency = gServerTypeInfo[eType].ppServerDependencies[iDependencyNum];

		if (DependencyIsActive(pDependency))
		{

			if (pDependency->bAutoLaunchDependency)
			{
				//if the type we want to launch is unique per shard and one already exists, ignore the auto launch
				if (gServerTypeInfo[pDependency->eTypeToWaitOn].bIsUnique && gpServersByType[pDependency->eTypeToWaitOn])
				{
					continue;
				}
				
				if (pDependency->bPerMachineDependency)
				{
					if (!pMachine->pServersByType[pDependency->eTypeToWaitOn])
					{
						RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, VIS_UNSPEC,
							pMachine, pDependency->eTypeToWaitOn, GetFreeContainerID(pDependency->eTypeToWaitOn), false, NULL, NULL, false, 0, NULL, "Want to launch a %s, which depends (per-machine) on %s", GlobalTypeToName(eType), GlobalTypeToName(pDependency->eTypeToWaitOn));
					}
				}
				else
				{
					if (!gpServersByType[pDependency->eTypeToWaitOn])
					{
						RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, VIS_UNSPEC, FindDefaultMachineForType(pDependency->eTypeToWaitOn, NULL, NULL), pDependency->eTypeToWaitOn, GetFreeContainerID(pDependency->eTypeToWaitOn), false, NULL, NULL, false, 0,
							NULL, "Want to launch a %s, which depends on %s", GlobalTypeToName(eType), GlobalTypeToName(pDependency->eTypeToWaitOn));
					}
				}
			}
		}
	}
}

AUTO_COMMAND;
void RequestOpenMachineListFromSentryServer(void)
{
	gbRequestMachineList = true;
}


void RequestOpenMachineListFromSentryServer_DoIt(void)
{
	Packet *pPack = SentryServerComm_CreatePacket(MONITORCLIENT_EXPRESSIONQUERY, "Requesting open machine list");
	pktSendBits(pPack, 32, EXPRESSIONQUERY_FLAG_SEND_BACK_SAFE_RESULT); //flags of type EXPRESSIONQUERY_FLAG_
	pktSendString(pPack, "secsSince2000() - me.last_heard < 10 AND StatCount(\"Status_Type\", {stat.str = \"Open\"}) > 0 AND ( StatCount(\"Process_Name\", {stat.str = \"launcher\"}) + StatCount(\"Process_Name\", {stat.str = \"PatchClient\"}) + StatCount(\"Process_Name\", {stat.str = \"PatchClientX64\"}) < 1 )");
	SentryServerComm_SendPacket(&pPack);
}

bool Controller_RequestAllMachineListFromSentryServer(void)
{
	Packet *pPack = SentryServerComm_CreatePacket(MONITORCLIENT_EXPRESSIONQUERY, "RequestAllMachineList");
	pktSendBits(pPack, 32, EXPRESSIONQUERY_FLAG_SEND_BACK_SAFE_RESULT); //flags of type EXPRESSIONQUERY_FLAG_
	pktSendString(pPack, "1");
	SentryServerComm_SendPacket(&pPack);

	return true;
}

extern int gbTimeStampAllPrintfs;

void GetDumpForStalledServer(TrackedServerState *pServer)
{

	if ( isProductionMode() && !pServer->bAlreadyDumpedForStalling && pServer->PID && pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER] && pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink)
	{
		Packet *pPak;
		char *pDumpString = NULL;
		estrPrintf(&pDumpString, "%s_Responding_%s_%u_%u",GlobalTypeToName(pServer->eContainerType), pServer->pMachine->machineName, pServer->iContainerID, timeSecondsSince2000());
		pPak = pktCreate(pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_GETAMANUALDUMP);
		pktSendBits(pPak, 32, pServer->PID);
		pktSendString(pPak, pDumpString);

		if (gServerTypeInfo[pServer->eContainerType].executableName64_original[0])
		{
			pktSendBits(pPak, 1, 0);
		}
		else
		{
			pktSendBits(pPak, 1, 1);
		}

		pktSend(&pPak);
		pServer->bAlreadyDumpedForStalling = true;
		estrDestroy(&pDumpString);
	}

}

void ControllerOnceASecond(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int i;
	static int iCounter = 1;
	TrackedServerState *pServer;
	U32 iCurTime;


	if (gbShuttingDown)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();


	iCurTime = timeSecondsSince2000();

//	UpdateAlerts();
	UpdateRecentlyDiedServers();


	PERFINFO_AUTO_START("UpdateControllerTrackerConnections", 1);
	if (!ControllerScripting_IsRunning())
	{
		UpdateControllerTrackerConnections();
	}
	PERFINFO_AUTO_STOP();

	iCounter++;

	PERFINFO_AUTO_START("Every20SecondsIterateServers", 1);
	if (iCounter % 20 == 0 )
	{
		static U64 iLastControllerTick = 0;
		static U64 iLastServerMonitoringBytes = 0;
		static char *spBytesString = NULL;

		estrMakePrettyBytesString(&spBytesString, (U32)(giServerMonitoringBytes - iLastServerMonitoringBytes));

		if (!gbTimeStampAllPrintfs)
		{
			printf("%s: ", timeGetLocalDateStringFromSecondsSince2000(iCurTime));
		}

		printf("%d servers. %d gameservers. %d fps, %s of server monitoring over last 20 seconds\n", 
			giTotalNumServers, giTotalNumServersByType[GLOBALTYPE_GAMESERVER], 
			(U32)((giControllerTick - iLastControllerTick) / 20), spBytesString);
		iLastControllerTick = giControllerTick;
		iLastServerMonitoringBytes = giServerMonitoringBytes;




		for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
		{
			pServer = gpServersByType[i];

			while (pServer)
			{
				TrackedServerState *pNextServer = pServer->pNext;
				if (pServer->iKeepAliveStartTime)
				{
					if (pServer->iNextExpectedKeepAliveTime)
					{
						pServer->iKeepAliveStartTime = 0;
					}
					else if (pServer->iKeepAliveStartTime < iCurTime)
					{
						if (gServerTypeInfo[pServer->eContainerType].bDumpAndKillOnKeepAliveStartupFail)
						{
							GetDumpForStalledServer(pServer);
							StopTrackingServer(pServer, "KeepAlive Startup time overflow (dump requested)", true, false);
						}
						else
						{
							TriggerAlertf("KEEPALIVE_STARTUP_FAIL", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 40.0f, pServer->eContainerType, pServer->iContainerID, 0, 0, pServer->pMachine->machineName, 0, "%s took more than %d seconds to reach state %s during startup, something is likely wrong",
								GlobalTypeAndIDToString(pServer->eContainerType, pServer->iContainerID), gServerTypeInfo[pServer->eContainerType].iStartupKeepAliveDelay, gServerTypeInfo[pServer->eContainerType].keepAliveBeginState);
						}
							
						pServer = pNextServer;
						continue;
					}
				}
					

				if (!ControllerScripting_IsRunning() && gServerTypeInfo[pServer->eContainerType].iKeepAliveDelay)
				{
					ConditionChecker_BetweenConditionUpdates(pServer->pAlertsConditionChecker);

					if (pServer->iNextExpectedKeepAliveTime && pServer->iNextExpectedKeepAliveTime < GetCurTimeToUseForStallChecking(pServer->pMachine))
					{
						if (ConditionChecker_CheckCondition(pServer->pAlertsConditionChecker, "KeepAliveFail"))
						{
							if (gServerTypeInfo[pServer->eContainerType].bDumpAndKillOnKeepAliveFail)
							{
								Controller_DoXperfDumpOnMachine(pServer->pMachine, "KeepAliveFail");
								GetDumpForStalledServer(pServer);
								StopTrackingServer(pServer, "KeepAlive time overflow (dump requested)", true, false);
							}
							else
							{
								Controller_DoXperfDumpOnMachine(pServer->pMachine, "KeepAliveFail_%s", GlobalTypeToName(pServer->eContainerType));
								TriggerAlertf("KEEPALIVE_FAIL", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 40.0f, pServer->eContainerType, 
									pServer->iContainerID, 0, 0, pServer->pMachine->machineName, 0, 
									"%s has not responded to a keepalive packed for  %d seconds, something is likely wrong",
									GlobalTypeAndIDToString(pServer->eContainerType, pServer->iContainerID), gServerTypeInfo[pServer->eContainerType].iKeepAliveDelay);
							}
						}

					}
				}

				pServer = pNextServer;
			}
		}
	}
	PERFINFO_AUTO_STOP();



	if (iCounter % 5 == 0)
	{
		if (iCounter % 300 == 0)
		{
			PERFINFO_AUTO_START("Every300SecondsLogPerfInfo", 1);
			SendPerfInfoToControllerTracker(1);
			PERFINFO_AUTO_STOP();
		}
		else
		{
			PERFINFO_AUTO_START("Every5SecondsSendPerfInfo", 1);
			SendPerfInfoToControllerTracker(0);
			PERFINFO_AUTO_STOP();

		}

		PERFINFO_AUTO_START("Every5SecondsSendIPs", 1);
		SendLoginServerIPsToControllerTrackers();
		PERFINFO_AUTO_STOP();
	}

	pServer = gpServersByType[GLOBALTYPE_GAMESERVER];

	giLongestDelayedGameServerTime = 0;

	PERFINFO_AUTO_START("EverySecondCheckGSAlerts", 1);
	while (pServer)
	{
		TrackedServerState *pNext = pServer->pNext;

		if (!pServer->bHasCrashed)
		{
			CheckGameServerInfoForAlerts(pServer);
		}

		pServer = pNext;
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("LogControllerOverview", 1);
	if (giLogControllerOverviewInterval && (iCounter % giLogControllerOverviewInterval == 0))
	{
		LogControllerOverview();
	}
	PERFINFO_AUTO_STOP();


	if (isProductionMode() && iCounter % 5 == 0)
	{
		PERFINFO_AUTO_START("Every5SecondsCheckForDeadLaunchers", 1);

		for (i = 0; i < giNumMachines; i++)
		{
			gTrackedMachines[i].bLauncherIsCurrentlyStalled = false;

			if (gTrackedMachines[i].pServersByType[GLOBALTYPE_LAUNCHER])
			{
				if (!gTrackedMachines[i].pLauncherKeepAliveConditionChecker)
				{
					gTrackedMachines[i].pLauncherKeepAliveConditionChecker = ConditionChecker_Create(2, 100);
				}

				ConditionChecker_BetweenConditionUpdates(gTrackedMachines[i].pLauncherKeepAliveConditionChecker);

				if (gTrackedMachines[i].iLastLauncherContactTime == 0)
				{
					gTrackedMachines[i].iLastLauncherContactTime = iCurTime + gLauncherCreationGracePeriod;
				}
				else if (gTrackedMachines[i].iLastLauncherContactTime < iCurTime - gLauncherKeepAliveTime)
				{
					if (ConditionChecker_CheckCondition(gTrackedMachines[i].pLauncherKeepAliveConditionChecker, "LAUNCHER_KEEPALIVE"))
					{
						TriggerAlertf("LAUNCHER_KEEPALIVE", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, gLauncherKeepAliveTime * 2, 
							GLOBALTYPE_LAUNCHER, gTrackedMachines[i].pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID,
							GLOBALTYPE_LAUNCHER, gTrackedMachines[i].pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID,
							gTrackedMachines[i].machineName, 0, 
							"Haven't heard from launcher on %s for %d seconds... NOT KILLING IT. Suggest reconnection.",
							gTrackedMachines[i].machineName, gLauncherKeepAliveTime);	
						Controller_DoXperfDumpOnMachine(&gTrackedMachines[i], "LauncherKeepAliveFail");
						gTrackedMachines[i].bLauncherIsCurrentlyStalled = true;

						if (gTrackedMachines[i].iLastLauncherContactTime < iCurTime - gLauncherKeepAliveTime * 10)
						{
							TriggerAlertf("LAUNCHER_CRITICAL_KEEPALIVE", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, gLauncherKeepAliveTime * 2, 
								GLOBALTYPE_LAUNCHER, gTrackedMachines[i].pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID,
								GLOBALTYPE_LAUNCHER, gTrackedMachines[i].pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID,
								gTrackedMachines[i].machineName, 0, 
								"Haven't heard from launcher on %s for %d seconds... THIS IS EXTREMELY FATAL AND WILL PROBABLY SCREW UP ALL SORTS OF OTHER THINGS ON THAT MACHINE.",
								gTrackedMachines[i].machineName, gLauncherKeepAliveTime * 10);	
						}
					}
				}
			}
		}
		PERFINFO_AUTO_STOP();
	}

	if ((iCounter % 5 == 0) && ControllerMirroringIsActive())
	{
		ControllerMirrorInfo *pMirrorInfo;
		PERFINFO_AUTO_START("Every5SecondsControllerMirroring", 1);
	
		pMirrorInfo = StructCreate(parse_ControllerMirrorInfo);

		for (i=0; i < giNumMachines; i++)
		{
			eaPush(&pMirrorInfo->ppMachines, &gTrackedMachines[i]);
		}

		for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
		{
			pServer = gpServersByType[i];

			while (pServer)
			{
				eaPush(&pMirrorInfo->ppServers, pServer);
				pServer = pServer->pNext;
			}
		}

		RemoteCommand_HereIsControllerMirrorInfo(GLOBALTYPE_WEBREQUESTSERVER, 0, pMirrorInfo);

		eaDestroy(&pMirrorInfo->ppMachines);
		eaDestroy(&pMirrorInfo->ppServers);

		PERFINFO_AUTO_STOP();
	}

	if (giCheckForEmptyGameServersInterval && (iCounter % giCheckForEmptyGameServersInterval == 0))
	{
		CheckForEmptyGameServers();
	}


	PERFINFO_AUTO_STOP_FUNC();

}
AUTO_COMMAND;
void SetMachineServerTypePriority(int iMachineNum, char *pServerTypeName, int iNewPriority)
{

	GlobalType eServerType = NameToGlobalType(pServerTypeName);
	TrackedMachineState *pMachine;
	
	if (eServerType == GLOBALTYPE_NONE)
	{
		return;
	}
	
	if (iMachineNum < 0 || iMachineNum >= giNumMachines)
	{
		return;
	}

	pMachine = &gTrackedMachines[iMachineNum];

	pMachine->canLaunchServerTypes[eServerType].iPriority = iNewPriority;
}

AUTO_COMMAND;
void SetMachineServerTypePermissions(int iMachineNum, char *pServerTypeName, int bSet)
{
	GlobalType eServerType = NameToGlobalType(pServerTypeName);
	TrackedMachineState *pMachine;
	
	if (eServerType == GLOBALTYPE_NONE)
	{
		return;
	}
	
	if (iMachineNum < 0 || iMachineNum >= giNumMachines)
	{
		return;
	}

	pMachine = &gTrackedMachines[iMachineNum];

	pMachine->canLaunchServerTypes[eServerType].eCanLaunch = bSet ? CAN_LAUNCH_SPECIFIED : CAN_NOT_LAUNCH;

	if (!pMachine->bIsLocalHost && spLocalMachine->canLaunchServerTypes[eServerType].eCanLaunch == CAN_LAUNCH_DEFAULT
		&& bSet)
	{
		spLocalMachine->canLaunchServerTypes[eServerType].eCanLaunch = CAN_NOT_LAUNCH;
	}
}

int CountMachinesForServerType(GlobalType eContainerType)
{
	int iRetVal = 0;
	int i;
	for (i = 0; i < giNumMachines; i++)
	{
		if (gTrackedMachines[i].canLaunchServerTypes[eContainerType].eCanLaunch != CAN_NOT_LAUNCH)
		{
			iRetVal++;
		}
	}

	return iRetVal;
}



AUTO_COMMAND;
char *GrabMachineForShard(char *pMachineName)
{
	char *pFullCommandLine = NULL;
	char executableName[256];
	Packet *pPak;
	ContainerID iContainerID;

	estrCreate(&pFullCommandLine);

	sprintf(executableName, "%s\\Launcher.exe", gCoreExecutableDirectory);


	iContainerID = GetFreeContainerID(GLOBALTYPE_LAUNCHER);
	estrPrintf(&pFullCommandLine, "%s -SetDirToLaunchFrom %s -ContainerID %u -ControllerHost %s -Cookie %u -SetErrorTracker %s -SetProductName %s %s %s",
			executableName,
			gExecutableDirectory,
			iContainerID,
			makeIpStr(GetHostIpToUse()),
			gServerLibState.antiZombificationCookie,
			getErrorTracker(), 
			GetProductName(), GetShortProductName(),
			pLauncherCommandLine ? pLauncherCommandLine : "");

	PutExtraStuffInLauncherCommandLine(&pFullCommandLine, iContainerID);


	pPak = SentryServerComm_CreatePacket(MONITORCLIENT_LAUNCH, "Grabbing machine %s", pMachineName);
	pktSendString(pPak, pMachineName);
	pktSendString(pPak, pFullCommandLine);
	SentryServerComm_SendPacket(&pPak);

	estrDestroy(&pFullCommandLine);

	return "command sent through sentry server";


}



void SendFileThroughSentryServer(char *pMachineName, char *pLocalFile, char *pRemoteFile, bool bDontCacheFileData)
{
	int iInFileSize;

	if (bDontCacheFileData)
	{
		char *pCompressedBuffer = NULL;
		int iCompressedSize;
		char *pInBuffer = fileAlloc(pLocalFile, &iInFileSize);
		Packet *pPak;
		if (!pInBuffer)
		{
			AssertOrAlert("CANT_READ_FILE_TO_SEND", "Couldn't read file %s in order to send it via sentry server", pLocalFile);
			return;
		}
		pCompressedBuffer = zipData(pInBuffer, iInFileSize, &iCompressedSize);
	
		pPak = SentryServerComm_CreatePacket(MONITORCLIENT_CREATEFILE, "Sending file %s to %s", pLocalFile, pMachineName);	pktSendString(pPak, pMachineName);
		pktSendString(pPak, pRemoteFile);
		pktSendBits(pPak, 32, iCompressedSize);
		pktSendBits(pPak, 32, iInFileSize);
		pktSendBytes(pPak, iCompressedSize, pCompressedBuffer);

		SentryServerComm_SendPacket(&pPak);

		SAFE_FREE(pInBuffer);
		SAFE_FREE(pCompressedBuffer);
	}
	else
	{
		CompressedFileCache *pCache;
		Packet *pPak;
		pCache = Controller_GetCompressedFileCache(pLocalFile);
		if (!pCache)
		{
			AssertOrAlert("CANT_READ_FILE_TO_SEND", "Couldn't read file %s in order to send it via sentry server", pLocalFile);
			return;
		}


	/*	if (!stashFindPointer(sCompressedFileDataByLocalFileName, pLocalFile, &pCache))
		{
			pInBuffer = fileAlloc(pLocalFile, &iInFileSize);
			if (!pInBuffer)
			{
				AssertOrAlert("CANT_READ_FILE_TO_SEND", "Couldn't read file %s in order to send it via sentry server", pLocalFile);
				return;
			}

			pCache = calloc(sizeof(CompressedFileCache), 1);
			pCache->pFileName = strdup(pLocalFile);
			pCache->pCompressedBuffer  = zipData(pInBuffer, iInFileSize, &pCache->iCompressedSize);
			pCache->iNormalSize = iInFileSize;
			stashAddPointer(sCompressedFileDataByLocalFileName, pCache->pFileName, pCache, false);

			SAFE_FREE(pInBuffer);

		}*/


		pPak = SentryServerComm_CreatePacket(MONITORCLIENT_CREATEFILE, "Sending file %s to %s", pLocalFile, pMachineName);	
		pktSendString(pPak, pMachineName);
		pktSendString(pPak, pRemoteFile);
		pktSendBits(pPak, 32, pCache->iCompressedSize);
		pktSendBits(pPak, 32, pCache->iNormalSize);
		pktSendBytes(pPak, pCache->iCompressedSize, pCache->pCompressedBuffer);

		SentryServerComm_SendPacket(&pPak);
	}
}

	


//when grabbing other machines for the shard, send them patchclient.exe and patchclientx64.exe
bool gbSendPatchClientThroughSentryServer = false;
AUTO_CMD_INT(gbSendPatchClientThroughSentryServer, SendPatchClientThroughSentryServer);


//step 0 = preparation
//step 1 = start patching
void GrabMachineForShardAndMaybePatch_Internal(char *pMachineName, int iStepNum, bool bKillEverything, bool bPatch, bool bFirstTimeConnectingToThisMachine, char *pPatchJobName)
{
	char *pPatchingDir = 0, *pFullCommandLine = 0, *pGrabbingLauncherCommandLine = 0;
	char *pLastSlash;
	Packet *pPak;
	int i;

	char *pFailExecuteCmdLine = NULL;
	char *pSuperEscFailExecuteCmdLine = NULL;
	char *pMessageBoxString = NULL;
	char *pSuperEscMessageBoxString = NULL;

	char *pMyCriticalSystemName = NULL;

	char patchExecutableFullName[CRYPTIC_MAX_PATH];
	ContainerID iContainerID = 1; //never actually used as 1

	// Get the name of the patchclient to use.
	strcpy(patchExecutableFullName, patchclientCmdLineEx(false, gExecutableDirectory, gCoreExecutableDirectory));

	//always kill launcher and launcherx64 no matter what
	if (iStepNum == 0)
	{
		ControllerKillAllDeferred(pMachineName, "launcher.exe");
		ControllerKillAllDeferred(pMachineName, "launcherX64.exe");
	}

	if (iStepNum == 0 && bPatch && gbSendPatchClientThroughSentryServer)
	{
		char tempFileName[CRYPTIC_MAX_PATH];
	
		sprintf(tempFileName, "%s/patchclient.exe", gExecutableDirectory);
		if (fileExists(tempFileName))
		{
			SendFileThroughSentryServer(pMachineName, tempFileName, tempFileName, false);
		}

		sprintf(tempFileName, "%s/patchclientX64.exe", gExecutableDirectory);
		if (fileExists(tempFileName))
		{
			SendFileThroughSentryServer(pMachineName, tempFileName, tempFileName, false);
		}
	}

	if (bKillEverything && iStepNum == 0)
	{
		char *pTempExeName = NULL;

		estrStackCreate(&pTempExeName);

		ControllerKillAllDeferred(pMachineName, "patchclient.exe");
		ControllerKillAllDeferred(pMachineName, "patchclientX64.exe");
		ControllerKillAllDeferred(pMachineName, "crypticError.exe");

		for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
		{
			if (i != GLOBALTYPE_CONTROLLER && i != GLOBALTYPE_LAUNCHER)
			{
				//FRANKENBUILD TODO
				if (gServerTypeInfo[i].executableName32_original[0])
				{
					estrCopy2(&pTempExeName, gServerTypeInfo[i].executableName32_original);
					estrTruncateAtFirstOccurrence(&pTempExeName, ' ');
					ControllerKillAllDeferred(pMachineName, pTempExeName);
				}
				if (gServerTypeInfo[i].executableName64_original[0])
				{
					estrCopy2(&pTempExeName, gServerTypeInfo[i].executableName64_original);
					estrTruncateAtFirstOccurrence(&pTempExeName, ' ');
					ControllerKillAllDeferred(pMachineName, pTempExeName);
				}
			}
		}

		estrDestroy(&pTempExeName);
	}

	if (iStepNum == 0)
	{
		ControllerKillAll_DoDeferredKills(pMachineName);
	}


	estrStackCreate(&pPatchingDir);
	estrStackCreate(&pFullCommandLine);

	estrCopy2(&pPatchingDir, gCoreExecutableDirectory);
	//can't use backslashes() on an EString
	estrReplaceOccurrences(&pPatchingDir, "/", "\\");

	pLastSlash = strrchr(pPatchingDir, '\\');

	if (pLastSlash)
	{
		estrSetSize(&pPatchingDir, pLastSlash - pPatchingDir);
	}

	estrPrintf(&pFullCommandLine, "WORKINGDIR(%s) %s %s ", pPatchingDir,
		patchExecutableFullName, gPatchingCommandLine);

	estrConcatf(&pFullCommandLine, " %s ", Controller_GetPCLStatusMonitoringCmdLine(pMachineName, pPatchJobName));

	estrConcatf(&pFullCommandLine, "-forkPrintfsToFile c:\\temp\\patch_output_main_%u.txt - %s -executableDir %s -executable ", timeSecondsSince2000(), pSpawnPatchCmdLine ? pSpawnPatchCmdLine : "", gCoreExecutableDirectory);

	if (!bPatch)
	{
		estrPrintf(&pGrabbingLauncherCommandLine, "WORKINGDIR(%s) ", gExecutableDirectory);
	}

	if (iStepNum == 1)
	{
		iContainerID = GetFreeContainerID(GLOBALTYPE_LAUNCHER);
	}

	estrConcatf(&pGrabbingLauncherCommandLine, "Launcher.exe -SetDirToLaunchFrom %s -ContainerID %u -ControllerHost %s -Cookie %u -SetErrorTracker %s -SetProductName %s %s %s", 
		gExecutableDirectory,
		iContainerID,
		makeIpStr(GetHostIpToUse()),
		gServerLibState.antiZombificationCookie,
		getErrorTracker(), 
		GetProductName(), GetShortProductName(),
		pLauncherCommandLine ? pLauncherCommandLine : "");
	PutExtraStuffInLauncherCommandLine(&pGrabbingLauncherCommandLine, iContainerID);

	//all but the first time, delete dynamic.hogg
	if (!bFirstTimeConnectingToThisMachine)
	{
		estrConcatf(&pGrabbingLauncherCommandLine, " -SkipDeletingDynamicHogg ");
	}

	estrConcatf(&pFullCommandLine, "\"%s\"", pGrabbingLauncherCommandLine);

	pMyCriticalSystemName = StatusReporting_GetMyName();

	//if we are doing status reporting, tell patch client to send an alert for us if it fails
	if (pMyCriticalSystemName && pMyCriticalSystemName[0])
	{
		estrPrintf(&pFailExecuteCmdLine, "SendAlert -controllerTrackerName %s -criticalSystemName \"%s\" -alertKey PCL_FAIL -alertString \"PCL failure while grabbing machine %s: {ErrorString}\"",
			StatusReporting_GetControllerTrackerName(), pMyCriticalSystemName, pMachineName);
		estrClear(&pSuperEscFailExecuteCmdLine);
		estrSuperEscapeString(&pSuperEscFailExecuteCmdLine, pFailExecuteCmdLine);
		estrConcatf(&pFullCommandLine, " -superEsc FailExecute %s ", pSuperEscFailExecuteCmdLine);
	}

	estrPrintf(&pMessageBoxString, "Patching has failed on %s. Alerts should be being sent to give you more info. You can also look in c:\\temp on that machine to get the patch client console output.",
		pMachineName);
	estrSuperEscapeString(&pSuperEscMessageBoxString, pMessageBoxString);

	estrPrintf(&pFailExecuteCmdLine, "SentryHelper -launch %s \"MessageBox -title PATCH_FAIL -superesc message %s\"",
		getHostName(), pSuperEscMessageBoxString);
	estrClear(&pSuperEscFailExecuteCmdLine);
	estrSuperEscapeString(&pSuperEscFailExecuteCmdLine, pFailExecuteCmdLine);
	estrConcatf(&pFullCommandLine, " -superEsc FailExecute %s ", pSuperEscFailExecuteCmdLine);


//	char *pFailExecuteCmdLine = NULL;
//	char *pSuperEscFailExecuteCmdLine = NULL;


	if (iStepNum == 1)
	{

		if (gbSpecialLauncherMirroring)
		{
			char tempFileName[CRYPTIC_MAX_PATH];
	
			sprintf(tempFileName, "%s/Launcher.exe", gExecutableDirectory);
			if (fileExists(tempFileName))
			{
				SendFileThroughSentryServer(pMachineName, tempFileName, tempFileName, false);
			}
			sprintf(tempFileName, "%s/Launcher.pdb", gExecutableDirectory);
			if (fileExists(tempFileName))
			{
				SendFileThroughSentryServer(pMachineName, tempFileName, tempFileName, false);
			}
			sprintf(tempFileName, "%s/LauncherX64.exe", gExecutableDirectory);
			if (fileExists(tempFileName))
			{
				SendFileThroughSentryServer(pMachineName, tempFileName, tempFileName, false);
			}
			sprintf(tempFileName, "%s/LauncherX64.pdb", gExecutableDirectory);
			if (fileExists(tempFileName))
			{
				SendFileThroughSentryServer(pMachineName, tempFileName, tempFileName, false);
			}
		}


		pPak = SentryServerComm_CreatePacket(MONITORCLIENT_LAUNCH, "Grabbing %s as part of %s", pMachineName, pPatchJobName);
		pktSendString(pPak, pMachineName);
		pktSendString(pPak, bPatch ? pFullCommandLine : pGrabbingLauncherCommandLine );
		SentryServerComm_SendPacket(&pPak);
	}	

	if (iStepNum == 0 && bPatch)
	{
		char patchServer[128];

		// Execute patchclient to update C:\Cryptic\tools
		estrPrintf(&pFullCommandLine, "WORKINGDIR(%s) %s -forkPrintfsToFile c:\\temp\\patch_output_cryptictools_%u.txt -sync -project CrypticTools -root C:/Cryptic/tools -cleanup",
			pPatchingDir,
			patchExecutableFullName,
			timeSecondsSince2000());

		estrConcatf(&pFullCommandLine, " -beginPatchStatusReporting \"%s: CrypticToolsBin\" localhost %d ",
			GetShardNameFromShardInfoString(), DEFAULT_MACHINESTATUS_PATCHSTATUS_PORT);

		if (triviaGetPatchTriviaForFile(SAFESTR(patchServer), gCoreExecutableDirectory, "PatchServer"))
		{
			estrConcatf(&pFullCommandLine, " -server %s", patchServer);
		}

		pPak = SentryServerComm_CreatePacket(MONITORCLIENT_LAUNCH, "Patching cryptic/tools/bin as part of %s", pPatchJobName);
		pktSendString(pPak, pMachineName);
		pktSendString(pPak, pFullCommandLine);
		SentryServerComm_SendPacket(&pPak);
	}

	estrDestroy(&pFullCommandLine);
	estrDestroy(&pPatchingDir);
	estrDestroy(&pGrabbingLauncherCommandLine);

	estrDestroy(&pFailExecuteCmdLine);
	estrDestroy(&pSuperEscFailExecuteCmdLine);
	estrDestroy(&pMessageBoxString);
	estrDestroy(&pSuperEscMessageBoxString);


}


AUTO_COMMAND;
char *GrabMachineForShardAndMaybePatch(char *pMachineName, bool bKillEverything, bool bPatch, char *pPatchJobName)
{
	TrackedMachineState *pMachine;
	bool bFirstTime = true;

	pMachine = FindMachineByName(pMachineName);
	if (pMachine)
	{
		bFirstTime = false;
		if (pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
		{
			pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->bKilledIntentionally = true;
			StopTrackingServer(pMachine->pServersByType[GLOBALTYPE_LAUNCHER], "Stopping tracking of (presumably already dead) launcher before machine reconnection", false, false);
		}
	}


	GrabMachineForShardAndMaybePatch_Internal(pMachineName, 0, bKillEverything, bPatch, bFirstTime, pPatchJobName);
	GrabMachineForShardAndMaybePatch_Internal(pMachineName, 1, bKillEverything, bPatch, bFirstTime, pPatchJobName);

	return "Launch commands sent through sentry server";
}

AUTO_COMMAND;
void DumpShardSetupFile(char *pFileName)
{
	char *pFileNameToUse = NULL;
	MachineInfoForShardSetupList list = {0};

	int i, j;

	estrPrintf(&pFileNameToUse, "c:\\shardSetupFiles\\%s.txt", pFileName);
	mkdirtree(pFileNameToUse);

	for (i=0; i < giNumMachines; i++)
	{
		if (stricmp(gTrackedMachines[i].machineName, "unknown") != 0)
		{
			MachineInfoForShardSetup *pMachine = StructCreate(parse_MachineInfoForShardSetup);

			if (gTrackedMachines[i].bIsLocalHost)
			{
				estrCopy2(&pMachine->pMachineName, "localhost");
			}
			else
			{
				estrCopy2(&pMachine->pMachineName, gTrackedMachines[i].machineName);
			}

			for (j=0; j < GLOBALTYPE_MAXTYPES; j++)
			{
				if (gServerTypeInfo[j].bInUse)
				{
					if (gTrackedMachines[i].canLaunchServerTypes[j].eCanLaunch == CAN_LAUNCH_SPECIFIED)
					{
						MachineInfoServerLaunchSettings *pSettings = StructCreate(parse_MachineInfoServerLaunchSettings);
						pSettings->eServerType = j;
						pSettings->eSetting = gTrackedMachines[i].canLaunchServerTypes[j].eCanLaunch;
						pSettings->iPriority = gTrackedMachines[i].canLaunchServerTypes[j].iPriority;

						eaPush(&pMachine->ppSettings, pSettings);
					}
				}
			}

			eaPush(&list.ppMachines, pMachine);
		}
	}

	ParserWriteTextFile(pFileNameToUse, parse_MachineInfoForShardSetupList, &list, 0, 0);

	StructDeInit(parse_MachineInfoForShardSetupList, &list);
}

//if you change this, also change GetShardNameFromShardInfoString() in utilitiesLib.c
void ConstructShardInfoString(void)
{
	char *pTempString = NULL;

	estrStackCreate(&pTempString);

	estrPrintf(&pTempString, "%s shard, name (%s), category (%s), version (%s), machine (%s), start time (%s)",
		GetProductName(),gNameToGiveToControllerTracker[0] ? gNameToGiveToControllerTracker : getComputerName(),
		gShardCategoryName, GetUsefulVersionString(), getComputerName(), timeGetLocalDateString());
	if(StatusReporting_GetControllerTrackerName())
		estrConcatf(&pTempString, ", controllertracker (%s)", StatusReporting_GetControllerTrackerName());
	SetShardInfoString(pTempString);

	estrDestroy(&pTempString);
}


U32 GetIPToUse(TrackedMachineState *pMachine)
{
	if (gbEverythingUsesPublicIPs)
	{
		return pMachine->iPublicIP;
	}

	return pMachine->IP;
}

U32 GetHostIpToUse(void)
{
	if (gbEverythingUsesPublicIPs && getHostPublicIp())
	{
		return getHostPublicIp();
	}

	return getHostLocalIp();
}


void Controller_FailWithAlert(char *pErrorString)
{
	int frameTimer = timerAlloc();
	U32 iStartTime = timeSecondsSince2000();


	enumStatusReportingState eState = StatusReporting_GetState();
		
	Controller_MessageBoxError("FAIL_WITH_ALERT", "%s", pErrorString);

	gbShuttingDown = true;
	if (eState == STATUSREPORTING_OFF)
	{
		exit(-1);
	}

	while (eState == STATUSREPORTING_NOT_CONNECTED)
	{
		F32 frametime = timerElapsedAndStart(frameTimer);

		if (timeSecondsSince2000() > iStartTime + 20)
		{
			Sleep(5000);
			exit(-1);
		}

		Sleep(1);

		utilitiesLibOncePerFrame(frametime, frametime);
		commMonitor(comm_controller);
		commMonitor(commDefault());
		
		Controller_PCLStatusMonitoringTick();
		SentryServerComm_Tick();

		serverLibOncePerFrame();
		commMonitor(commDefault());

		eState = StatusReporting_GetState();

	}

	TriggerAlert(allocAddString("CONTROLLER_STARTUP_FAILURE"), pErrorString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0,
	  GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(),  0);

	StatusReporting_ForceUpdate();

	exit(-1);
}

void SetKeepAliveStartTime(TrackedServerState *pServer)
{
	ServerTypeInfo *pTypeInfo;

	if (!isProductionMode())
	{
		return;
	}

	pTypeInfo = &gServerTypeInfo[pServer->eContainerType];

	if (pTypeInfo->iStartupKeepAliveDelay)
	{
		if (!pTypeInfo->iKeepAliveDelay)
		{
			Errorf("Server type %s has a startupKeepAliveDelay but no keepAliveDelay. This will not do anything",
				GlobalTypeToName(pServer->eContainerType));
			return;
		}

		pServer->iKeepAliveStartTime = timeSecondsSince2000() + pTypeInfo->iStartupKeepAliveDelay;
	}
}

void BeginKeepAlive(TrackedServerState *pServer)
{
	if (pServer->pLink)
	{
		Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_REQUEST_BEGIN_KEEPALIVE);
		pktSendBits(pPak, 32, gServerTypeInfo[pServer->eContainerType].iKeepAliveDelay / 2);
		pktSend(&pPak);

		pServer->iNextExpectedKeepAliveTime = timeSecondsSince2000() + gServerTypeInfo[pServer->eContainerType].iKeepAliveDelay;
	}
	else if (pServer && objLocalManager())
	{
		RemoteCommand_BeginSendingKeepAliveToController(pServer->eContainerType, pServer->iContainerID, gServerTypeInfo[pServer->eContainerType].iKeepAliveDelay / 2);

		pServer->iNextExpectedKeepAliveTime = timeSecondsSince2000() + gServerTypeInfo[pServer->eContainerType].iKeepAliveDelay;
	}
}

bool ServerReadyForMonitoring(TrackedServerState *pServer)
{
	char *pWaitState = gServerTypeInfo[pServer->eContainerType].monitoringWaitState;
	if (pWaitState[0] == 0)
	{
		return true;
	}

	if (strstri(pServer->stateString, pWaitState))
	{
		return true;
	}

	return false;
}

void CheckForIncorrectLocalStatedBasedAlerts(void)
{
	char temp[CRYPTIC_MAX_PATH];
	char dataDir[CRYPTIC_MAX_PATH];
	char localDataDir[CRYPTIC_MAX_PATH];
	char **ppFileNames = NULL;
	int i;

	if (!isProductionMode())
	{
		return;
	}

	fileLocateWrite(fileDataDir(), temp);
	makefullpath(temp, dataDir);
	fileLocateWrite(fileLocalDataDir(), temp);
	makefullpath(temp, localDataDir);	

	strcat(dataDir, "/server/config");
	strcat(localDataDir, "/server/config");

	ppFileNames = fileScanDir(dataDir);

	for (i = 0; i < eaSize(&ppFileNames); i++)
	{
		if (strstri(ppFileNames[i], "StateBasedAlerts") && !strstri(ppFileNames[i], "StateBasedAlerts_Local"))
		{
			WARNING_NETOPS_ALERT("LOCAL_STATEBASEDALERTS_FILE", "File %s is presumably a local file of stateBasedAlerts. But it is not named _local. This means it may override an entire checked in file of alerts, rather than just one or two, which is probably not the intended behavior",
				ppFileNames[i]);
		}
	}

	fileScanDirFreeNames(ppFileNames);

	ppFileNames = fileScanDir(localDataDir);

	for (i = 0; i < eaSize(&ppFileNames); i++)
	{
		if (strstri(ppFileNames[i], "StateBasedAlerts") && !strstri(ppFileNames[i], "StateBasedAlerts_Local"))
		{
			WARNING_NETOPS_ALERT("LOCAL_STATEBASEDALERTS_FILE", "File %s is presumably a local file of stateBasedAlerts. But it is not named _local. This means it may override an entire checked in file of alerts, rather than just one or two, which is probably not the intended behavior",
				ppFileNames[i]);
		}
	}

	fileScanDirFreeNames(ppFileNames);
	
	

}

AUTO_COMMAND;
char *ReloadStateBasedAlerts(void)
{
	char **ppStateBasedAlertFileNames = NULL;
	char *pRetVal;

	eaPush(&ppStateBasedAlertFileNames, strdup("StateBasedAlerts.txt"));
	eaPush(&ppStateBasedAlertFileNames, strdupf("StateBasedAlerts_%s.txt", GetShardNameFromShardInfoString()));
	eaPush(&ppStateBasedAlertFileNames, strdup("StateBasedAlerts_Local.txt"));

	pRetVal = BeginStateBasedAlerts(GetDirForBaseConfigFiles(), ppStateBasedAlertFileNames, true);
	eaDestroyEx(&ppStateBasedAlertFileNames, NULL);

	if (pRetVal)
	{
		return pRetVal;
	}

	return "Reloading was successful";
}


void Contoller_StartupStuffToDoWhenScriptingNotRunning(void)
{
	static bool bOnce = false;

	if (!bOnce)
	{
		char **ppStateBasedAlertFileNames = NULL;

		bOnce = true;



		loadstart_printf("Beginning state-based alerts\n");

		

		eaPush(&ppStateBasedAlertFileNames, strdup("StateBasedAlerts.txt"));
		eaPush(&ppStateBasedAlertFileNames, strdupf("StateBasedAlerts_%s.txt", GetShardNameFromShardInfoString()));
		eaPush(&ppStateBasedAlertFileNames, strdup("StateBasedAlerts_Local.txt"));

		//special case code... if data or localdata on disk contain any stateBasedAlerts files that are not _local,
		//then the overriding system is likely screwed up, so generate an alert
		CheckForIncorrectLocalStatedBasedAlerts();

		BeginStateBasedAlerts(GetDirForBaseConfigFiles(), ppStateBasedAlertFileNames, false);
		eaDestroyEx(&ppStateBasedAlertFileNames, NULL);

		loadend_printf("done");

		//launcher shouldn't get alarmed about controller stalliness until all startup stuff is done

		if (isProductionMode() && spLocalMachine)
		{
			TrackedServerState *pLocalLauncher = spLocalMachine->pServersByType[GLOBALTYPE_LAUNCHER];
			if (pLocalLauncher)
			{
				SendCommandDirectlyToServerThroughNetLink(pLocalLauncher, "XperfOnControllerStallsOrSlowdowns 1");
			}		
		}
	}
}



void CleanupLoadedServerTypeDependencies(ServerTypeInfo *pInfo)
{
	//two steps... first go through and set bTurnDependencyOff on all but the last copy of any dependencies with the same type to 
	//wait on. Then remove all bTurnDependencyOff dependencies
	int i, j;

	for (i=0; i < eaSize(&pInfo->ppServerDependencies) - 1; i++)
	{
		for (j=i + 1; j < eaSize(&pInfo->ppServerDependencies); j++)
		{
			if (pInfo->ppServerDependencies[i]->eTypeToWaitOn == pInfo->ppServerDependencies[j]->eTypeToWaitOn)
			{
				pInfo->ppServerDependencies[i]->bTurnDependencyOff = true;
			}
		}
	}

	for (i = eaSize(&pInfo->ppServerDependencies) - 1; i >= 0; i--)
	{
		if (pInfo->ppServerDependencies[i]->bTurnDependencyOff)
		{
			StructDestroy(parse_ServerDependency, pInfo->ppServerDependencies[i]);
			eaRemove(&pInfo->ppServerDependencies, i);
		}
	}
}

// If you change this, be sure to also change the version in launcher.c
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

static bool sbDontRestartIn64Bit = false;
AUTO_CMD_INT(sbDontRestartIn64Bit, DontRestartIn64Bit) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

void MaybeRestartIn64BitMode(void)
{
	int iPointerSize = sizeof(void*);
	char *pMyName;
	char *pMyName64 = NULL;
	U32 iMyTime, iMyTime64;
	char *p64BitCommandLine = NULL;


	if (isDevelopmentMode())
	{
		return;
	}
	

	if (iPointerSize == 8)
	{
		return;
	}

	if (!IsUsingX64())
	{
		return;
	}

	if (!gbLaunch64BitWhenPossible)
	{
		return;
	}

	if (sbDontRestartIn64Bit)
	{
		return;
	}

	pMyName = getExecutableName();
	estrCopy2(&pMyName64, pMyName);

	if (strstri(pMyName64, "FD.exe"))
	{
		estrReplaceOccurrences(&pMyName64, "FD.exe", "X64FD.exe");
	}
	else
	{
		estrReplaceOccurrences(&pMyName64, ".exe", "X64.exe");
	}

	if (!fileExists(pMyName64))
	{
		estrDestroy(&pMyName64);
		return;
	}

	iMyTime = fileLastChangedSS2000(pMyName);
	iMyTime64 = fileLastChangedSS2000(pMyName64);

	if (!iMyTime64 || !iMyTime)
	{
		TriggerAlertDeferred("CONTROLLER_X64_STARTUP_CONFUSION", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			"While starting up and trying to figure out whether to spawn the X64 executable, couldn't get modification times for %s and/or %s.",
			pMyName, pMyName64);
		estrDestroy(&pMyName64);
		return;
	}

	if (iMyTime64 < iMyTime - 2 * 60 * 60)
	{
		char *pDuration = NULL;
		timeSecondsDurationToPrettyEString(iMyTime - iMyTime64, &pDuration);
		TriggerAlertDeferred("CONTROLLER_X64_STARTUP_OBSOLETE", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			"While starting up, controller wanted to switch to X64 mode, but %s appears to be %s old, which probably indicates an incomplete patch or frankenbuild or something",
			pMyName64, pDuration);
		estrDestroy(&pDuration);
		estrDestroy(&pMyName64);
		return;
	}

	estrPrintf(&p64BitCommandLine, "%s %s", pMyName64, GetCommandLineWithoutExecutable());
		
	if (system_detach_with_fulldebug_fixup(p64BitCommandLine, false, false))
	{
		exit(0);
	}	
	else
	{
		TriggerAlertDeferred("CONTROLLER_X64_STARTUP_FAILURE", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			"While starting up, controller wanted to switch to X64 mode, but could not launch the following command line: %s",
			p64BitCommandLine);
	}

	estrDestroy(&pMyName64);


}


int main(int argc,char **argv)
{
	int		i, frameTimer;
	char cwd[MAX_PATH];

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	comm_controller = commCreate(0,1);

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'T', 0x8080ff);

	FolderCacheChooseModeNoPigsInDevelopment();

	MaybeRestartIn64BitMode();

	SentryServerComm_SetNetComm(comm_controller);

	if(isProductionMode())
		deleteDynamicHogg();

	fileAutoDataDir();

	preloadDLLs(0);

	filePrintDataDirs();

	if (fileIsUsingDevData()) 
	{
	} 
	else 
	{
		gimmeDLLDisable(1);
	}

	gMachinesByName = stashTableCreateWithStringKeys(16, StashDefault);

	srand((unsigned int)time(NULL));
	consoleUpSize(110,128);

	logSetDir(GlobalTypeToName(GetAppGlobalType()));

	//controller starts out not attached to log server, because the log server doesn't yet exist
	sprintf(gServerLibState.logServerHost, "NONE");

	fileGetcwd(SAFESTR(cwd));


	loadstart_printf("ServerLibStartup...");
	serverLibStartup(argc, argv);
	loadend_printf("done");

	loadstart_printf("checking patch trivia...");
	//to check whether we are a "patched" server, we check whether our patched project is "FightClubServer" or not
	{
		char curPatchProjectName[256];
		char desiredPatchProjectName[256];
		char temp[128] = "";

		if (triviaGetPatchTriviaForFile(SAFESTR(curPatchProjectName), gCoreExecutableDirectory, "PatchProject"))
		{
			sprintf(desiredPatchProjectName, "%sServer", GetProductName());
			if (stricmp(curPatchProjectName, desiredPatchProjectName) == 0)
			{
				printf("Found Patch project %s... this appears to be a patched server\n", curPatchProjectName);
				if (isProductionMode())
				{
					triviaGetPatchTriviaForFile(SAFESTR(temp), gCoreExecutableDirectory, "PatchComplete");
					assertmsgf(stricmp(temp, "all") == 0, "This appears to be a patched server, BUT, patching did not complete");
				}
				triviaGetPatchTriviaForFile(SAFESTR(gPatchingCommandLine), gCoreExecutableDirectory, "PatchCmdLine");

				assert(gPatchingCommandLine[0]);

				printf("Patch command line: %s\n", gPatchingCommandLine);

				triviaGetPatchTriviaForFile(SAFESTR(gPatchName), gCoreExecutableDirectory, "PatchName");

				assert(gPatchName[0]);

				printf("Patch name: %s\n", gPatchName);
			}
			else
			{
				//THIS IS NOT AN ERROR. It just means that gimme was run.
				printf("We have patch trivia data, but the patch project is %s instead of %s, so this is not a patched server\n", 
					curPatchProjectName, desiredPatchProjectName);
			}
		}
		else
		{
			printf("Found no patch trivia... this is not a patched server\n");
		}
	}
	loadend_printf("done");

	setErrorDialogCallback(errorDialogController, NULL);

	UpdateControllerTitle();
	Controller_InitLists();

	gControllerTimer = timerAlloc();
	timerStart(gControllerTimer);

	SendVersionInfoToControllerTracker();


	{
		ServerTypeInfoList *pList = StructCreate(parse_ServerTypeInfoList);
		HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(CONTROLLER_SERVER_SETUP_TXT), "TXT");
		if (rsrc)
		{
			HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
			if (gptr)
			{
				void *pTxtFile = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
				size_t iExeSize = SizeofResource(GetModuleHandle(NULL), rsrc);

				if (!ParserReadTextForFile(pTxtFile, "src\\data\\controllerServerSetup.txt", parse_ServerTypeInfoList, pList, 0))
				{
					char *pTempString = NULL;
					ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
					ParserReadTextForFile(pTxtFile, "src\\data\\controllerServerSetup.txt", parse_ServerTypeInfoList, pList, 0);
					ErrorfPopCallback();
					
					assertmsgf(0, "Got an error while reading the internal resource version of ControllerServerSetup.txt. That is fatal. Error: %s",
						pTempString);
				}
			}
		}

		ParserLoadFiles(GetDirForBaseConfigFiles(), "_ControllerServerSetup.txt", NULL, 0, parse_ServerTypeInfoList, pList);

//		ParserReadTextFile("server/ControllerServerSetup.txt", parse_ServerTypeInfoList, pList);


		for (i=0; i < eaSize(&pList->ppServerInfo); i++)
		{
			if (gServerTypeInfo[pList->ppServerInfo[i]->gServerType].bInUse)
			{
				StructOverride(parse_ServerTypeInfo, &gServerTypeInfo[pList->ppServerInfo[i]->gServerType], pList->ppServerInfo[i], 1, false, false);
			}
			else
			{
				StructCopyAll(parse_ServerTypeInfo, pList->ppServerInfo[i], &gServerTypeInfo[pList->ppServerInfo[i]->gServerType]);
				gServerTypeInfo[pList->ppServerInfo[i]->gServerType].bInUse = true;
			}

		}

		StructDestroy(parse_ServerTypeInfoList, pList);

		//sanity check to make sure data loaded
		assertmsg(gServerTypeInfo[GLOBALTYPE_LAUNCHER].executableName32_original[0], "Controller couldn't load basic server type info... something must be horribly wrong");
	
	
	}

	//in case our config-inheriting ended up setting the same startup dependency multiple times in cotnrollerServerSetup.txt,
	//go in and clean them up
	for (i=0 ; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (gServerTypeInfo[i].bInUse)
		{
			CleanupLoadedServerTypeDependencies(&gServerTypeInfo[i]);
		}
	}



	if (gbCheckForFrankenbuildExes)
	{
		gbMirrorLocalExecutables = true;
		CheckForPotentialFrankenbuildExes();
	}

	ApplyOverrideLaunchDirsAndExeNames();

//	ParserLoadFiles("server", "_ControllerAlertExpressions.txt", NULL, 0, parse_AllAlertExpressions, &gAllAlertExpressions);
//	ParserLoadFiles("server", "_ControllerAlertEmailRecipients.txt", NULL, 0, parse_AlertEmailRecipientList, &gAlertEmailRecipients);

	//special case for hide-launcher setting
	if (gbHideLocalLauncher)
	{
		gServerTypeInfo[GLOBALTYPE_LAUNCHER].eVisibility_FromMCP = VIS_HIDDEN;
	}

	if (gbKillAllOtherShardExesAtStartup)
	{
		char *pTempExeName = NULL;

		ControllerKillAll(NULL, "patchclient.exe");
		ControllerKillAll(NULL, "crypticError.exe");
		ControllerKillAll(NULL, "loginhammer.exe");

		estrStackCreate(&pTempExeName);

		for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
		{
			if (gServerTypeInfo[i].executableName32_original[0])
			{
				estrCopy2(&pTempExeName, gServerTypeInfo[i].executableName32_original);
				estrTruncateAtFirstOccurrence(&pTempExeName, ' ');
				ControllerKillAll(NULL, pTempExeName);
			}
			if (gServerTypeInfo[i].executableName64_original[0])
			{
				estrCopy2(&pTempExeName, gServerTypeInfo[i].executableName64_original);
				estrTruncateAtFirstOccurrence(&pTempExeName, ' ');
				ControllerKillAll(NULL, pTempExeName);
			}

			if (estrLength(&gServerTypeInfo[i].pOverrideExeName))
			{
				ControllerKillAll(NULL, gServerTypeInfo[i].pOverrideExeName);
			}
		}

	
		estrDestroy(&pTempExeName);
	}	

	InitAlertSystem();

	loadstart_printf("Opening listening port..");
	net_links = commListen(comm_controller,LINKTYPE_SHARD_CRITICAL_1MEG, LINK_FORCE_FLUSH,DEFAULT_CONTROLLER_PORT,ControllerHandleMsg,ControllerClientConnect,ControllerClientDisconnect,sizeof(TrackedServerState *));
	if (!net_links)
	{
		Alertf("ERROR: Coudldn't open listening port... another controller.exe may be running?");
		exit(-1);
	}

	loadend_printf("Done");

	ReloadGSLBannedCommands();

	//create the local machine
	GetMachineFromNetLink(NULL);

	frameTimer = timerAlloc();

	printf("Ready.\n");

	TimedCallback_Add(ControllerOnceASecond, NULL, 1.0f);

	gControllerStartupTime = timeSecondsSince2000();

	ConstructShardInfoString();

	AddStaticDefineIntForStateBasedAlerts(GlobalTypeEnum);

	InitInterShardComm();

	shardvariable_Load();

	ControllerAutoSetting_NormalOperationStarted();

	ControllerStartLocalMemLeakTracking();

	NotesServer_InitSystemAndConnect("Shard", GetShardNameFromShardInfoString());

	if (sbDumpAllAutoSettingsAndQuit)
	{
		Controller_DumpAllAutoSettingsAndQuit();
	}

	ControllerShardCluster_Startup();

	for(;;)
	{
		F32 frametime;
		
		autoTimerThreadFrameBegin(__FUNCTION__);
		
		frametime = timerElapsedAndStart(frameTimer);

		giControllerTick++;

		VersionHistory_UpdateFSM();

		if (gbLaunchMonitoringMCP)
		{
			if (spLocalMachine->pServersByType[GLOBALTYPE_LAUNCHER] && gpServersByType[GLOBALTYPE_LOGSERVER] && !spLocalMachine->pServersByType[GLOBALTYPE_MASTERCONTROLPROGRAM])
			{
				char *pMCPCommandLine = NULL;
				estrPrintf(&pMCPCommandLine, "-complex -local -dontcreatecontroller -controllerhost localhost -HttpMonitor -YouAreMonitoringMCP -logserver %s -LogServerMonitor", makeIpStr(gpServersByType[GLOBALTYPE_LOGSERVER]->pMachine->IP));
				RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, VIS_UNSPEC, 
					spLocalMachine, GLOBALTYPE_MASTERCONTROLPROGRAM, GetFreeContainerID(GLOBALTYPE_MASTERCONTROLPROGRAM), false,
					pMCPCommandLine, 
					NULL, true, 0, NULL, "Launching monitoring MCP");
				estrDestroy(&pMCPCommandLine);
			}
		}

		if (gbRequestMachineList)
		{
			RequestOpenMachineListFromSentryServer_DoIt();
			gbRequestMachineList = false;
		}
		

		//assertmsg(heapValidateAll(), "heapValidateAll() failed");

		utilitiesLibOncePerFrame(frametime, 1);

		AttemptToConnectToTransactionServer();


		//calls ControllerStartup_ConnectionStuffWithSentryServer
		ControllerScripting_Update();
		
		UpdateInterShardComm();

		UdpateThrottledFileSends();

		commMonitor(comm_controller);
		commMonitor(commDefault());

		Controller_PCLStatusMonitoringTick();
		SentryServerComm_Tick();

		NotesServer_Tick();

		Controller_ClusterControllerTick();

		//Sleep(1);

		if (sbNeedToKillAllServers)
		{
			KillAllServers();
			sbNeedToKillAllServers = false;
		}

		UpdateControllerTitle();
		serverLibOncePerFrame();

		DynHoggPruning_Update();

		if (giNumWaitingServers)
		{
			PERFINFO_AUTO_START("checkWaitingServers", 1);
			
			for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
			{
				if (gServerTypeInfo[i].ppServerDependencies)
				{
					TrackedServerState *pServer = gpServersByType[i];

					while (pServer)
					{
						if (pServer->bWaitingToLaunch)
						{
							if (ServerTypeIsReadyToLaunchOnMachine(i, pServer->pMachine, NULL))
							{
								pServer->bWaitingToLaunch = false;
								giNumWaitingServers--;
								pServer->pMachine->iWaitingServers--;


								estrPrintf(&gServerTypeInfo[pServer->eContainerType].pSharedCommandLine_Combined, " %s %s ",
									gServerTypeInfo[pServer->eContainerType].pSharedCommandLine_FromScript ? gServerTypeInfo[pServer->eContainerType].pSharedCommandLine_FromScript : "",
									gServerTypeInfo[pServer->eContainerType].pSharedCommandLine_FromMCP ? gServerTypeInfo[pServer->eContainerType].pSharedCommandLine_FromMCP : "");


								DoActualLaunchingWithLauncher(pServer, pServer->pMachine, pServer->pFullPath, pServer->iExecutableCRC, pServer->iContainerID, pServer->eContainerType, pServer->iLowLevelIndex, pServer->bStartInDebugger,
									pServer->pCommandLine, pServer->pWorkingDirectory, pServer->eVisibility, gServerTypeInfo[pServer->eContainerType].pSharedCommandLine_Combined);
								pServer->fCreationTimeFloat = timerElapsed(gControllerTimer);
								pServer->iCreationTime = timeSecondsSince2000();
								SetKeepAliveStartTime(pServer);
							}
							else
							{
								DoAnyDependencyAutoLaunchingForServerTypeOnMachine(i, pServer->pMachine);
							}
						}

						pServer = pServer->pNext;
					}
				}
			}
			
			PERFINFO_AUTO_STOP();
		}

		autoTimerThreadFrameEnd();
	}


	EXCEPTION_HANDLER_END



}

//(seconds) generally, we check servers for lags by comparing their update times to the most recent update time from the
//launcher on that machine, so that a whole machine hiccuping doesn't result in tons of individual server alerts... but
//we only extend that buffer a certain amount of time into the past (as we don't want to just never get alerts at all),
//this is that maximum window
int giMaxDiffForTimeToUseForStallChecking = 60;
AUTO_CMD_INT(giMaxDiffForTimeToUseForStallChecking, MaxDiffForTimeToUseForStallChecking) ACMD_CONTROLLER_AUTO_SETTING(MiscAlerts);

U32 GetCurTimeToUseForStallChecking(TrackedMachineState *pMachine)
{
	U32 iRetVal = pMachine->iLastLauncherContactTime;
	if (iRetVal < timeSecondsSince2000() - giMaxDiffForTimeToUseForStallChecking)
	{
		iRetVal = timeSecondsSince2000() - giMaxDiffForTimeToUseForStallChecking;
	}

	return iRetVal;
}




#include "autogen/controller_h_ast.c"
