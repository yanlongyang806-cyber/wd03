#pragma once

#include "GlobalTypeEnum.h"
#include "net/net.h"
#include "stashtable.h"
#include "controllerpub.h"
#include "controller_alerts.h"
#include "svrGlobalInfo.h"
#include "../../utilities/sentryServer/SentryPub.h"
#include "Controller_scripting.h"

typedef struct StructInfoForHttpXpath StructInfoForHttpXpath;
typedef struct ShardVariableContainer ShardVariableContainer;

//information describing a depency that a server type can have
AUTO_STRUCT;
typedef struct ServerDependency
{
	GlobalType eTypeToWaitOn; AST(SUBTABLE(GlobalTypeEnum)) //what type of server I'm dependent on
	char stateToWaitFor[64]; //what state that server must be in (this string must be a substate of the server string)
	
	U32 bPerMachineDependency:1; //a server of that type must exist on the same machine that I am on (ie, a multiplexer)
							   //(as opposed to a server of that type must exist globally)

	U32 bAutoLaunchDependency:1; //if someone wants to launch me, and I depend on something else with an autoLaunchDependency,
								 //then launch that thing automatically (NOTE that the controller scripting usually does
								 //this automatically, so this is only needed for things like Multiplexers, where the
								 //script launches a client, and the client tells the mapmanager to launch a GameServer,
								 //and the GameServer depends on a Multiplexer, which the client itself did not depend on)

	U32 bProductionModeOnlyDependency:1; //this dependency only exists in production mode

	U32 bTurnDependencyOff:1; //if you are using a text file to override the settings compiled into controller.exe, set this
							  //to completely override the dependency

	U32 bBuilderOnlyDependency:1; //this dependency only exists in continuous builders

} ServerDependency;

AUTO_ENUM;
typedef enum enumFrankenState
{
	FRANKENSTATE_NONE,
	FRANKENSTATE_32BIT = 1 << 1,
	FRANKENSTATE_64BIT = 1 << 2,
	FRANKENSTATE_BOTH = FRANKENSTATE_32BIT | FRANKENSTATE_64BIT,
} enumFrankenState;

//information describing how a particular type of server gets created
AUTO_STRUCT;
typedef struct ServerTypeInfo
{
	GlobalType gServerType;  AST(SUBTABLE(GlobalTypeEnum)) //what type of server this struct describes

	U32 bIsUnique : 1; //if there's one of these, then don't create any more
	U32 bReCreateOnCrash : 1; //if one of these crashes, then start a new one immediately
			//only used when ProductionShardMode is set on the controller

	U32 bReCreateOnSameMachineOnCrash : 1; //if one of these crashes, start a new one immediately on the same machine
			//only used when ProductionShardMode is set on the controller
		

	U32 bCanNotCrash : 1; //this server should not be able to crash. If it crashes, we can not recover, so 
			//kill the entire shard (only used when ProductionShardMode is set on the controller
	
	U32 bIsUniquePerMachine : 1; //no more than one can exist per machine

	U32 bDontKillDuringKillAll : 1; //this type of server shouldn't be purged when resetting everything (ie, launchers)

	U32 bLaunchersDontKnowAboutMe : 1; //this type of server is not reported on by launchers (ie, MCPs)

	U32 bKillExistingOnBigGreenButton : 1; //when the BigGreenButton is pressed, instead of letting the server
		//keep running, kill it and start a new one (ie, gameserver, gameclient)

	U32 bAllowMultiplesWhenSingleMCPButtonPressed : 1; //When individual "launch this" MCP button is pressed, always create one
	    //of these, no matter how many are currently running. (ie, gameserver, gameclient)

	U32 bLaunchFromCoreDirectory : 1; // This project launches from the core directory, instead of the project one

	U32 bIgnoreCookies : 1; //don't worry about anti-zombification cookies for this type (MCPs)

	U32 bLaunchersCanLaunchMeFromMonitorWindow : 1; //when you go into the MCP monitor window and double click on a launcher,
												 //this is one of the choices of what type of server to launch on it
												//OBSOLETE

	U32 bRequiredForProductionShardMode : 1; //in production shard mode, automatically create a server of this type, even
											//if the controller setup commands don't specify it

	U32 bIgnoredByControllerScripting : 1; //controller scripting commands like KILLALL don't apply to this type
										   //(presumably things that are part of the controller infrastructure,
										   //launchers, multiplexers, MCPs, and the controller itself)

	U32 bCanNotBeQueriedByControllerScripting : 1; //a weaker version of the above, for servers that can not respond to
													//remote commands, ie, TRANSACTION_SERVER, LOG_SERVER, MULTIPLEXER

	U32 bCanNotBeHttpMonitored : 1; //this server can not be monitored through the MCP HTTP stuff, but can be listed
									//as part of the shard

	U32 bCanBeManagedInPerMachinePermissions : 1; //in the server monitor, you can manually choose whether or not
			//any given machine can run servers of this type

	U32 bCanBeManuallyLaunchedThroughServerMonitor : 1; //you can monitor a machine in your shard and launch a server
		//of this type through a simple command
	
	U32 bScriptingLaunchesAsManyOfTheseAsAllowed : 1; //this type may be allowed for multiple (but not all) machines via shardSetupFile.
		//When controller scripting launches one, launch one for each machine. For instance, LoginServer.

	U32 bMustShutItselfDown : 1; //servers of this type may take a while to shutdown. Don't kill them at shard
		//shutdown time. (This is what ShutdownWatcher is for).

	U32 bLeaveCrashesUpForever : 1; //if true, then when servers of this type crash, the crashes stay open forever

	U32 bWaitForCrypticErrorBeforeRecreating : 1; //only applies if RecreateOnCrash or bReCreateOnSameMachineOnCrash is set. Don't
		//kill and restart until crypticError is finished

	U32 bMCPRequestsStartAllInDebugger : 1;

	U32 bInformAboutShardLockStatus : 1; //keep me informed about whether the shard is locked or not

	U32 bInformAboutGatewayLockStatus : 1; //keep me informed about whether the gateway is locked or not

	U32 bInformAboutMTStatus : 1; // keep me informed about whether the shard microtransaction kill switch is on or not

	U32 bInformAboutNumGameServerMachines : 1; //at startup, inform this server of how many game server machines there are in the shard

	U32 bInformAboutAllowShardVersionMismatch : 1; //inform this server if gbAllowShardVersionMismatch is true or not, keep it updated

	U32 bIsScarce : 1; //even though it is not unique, this server type is scarce, add it to ScarceServers global resource
		//dict on the controller

	U32 bSupportsAutoSettings : 1; //can have AUTO_SETTINGS on this server, means that one of these will be run during
		//DumpAutoSettingsAndExit

	U32 bInformObjectDBWhenThisDies : 1; //if true, then call InformObjectDBOfServerDeath whenever one of these dies

	U32 bMachineSpecifiedInShardSetupFile : 1; NO_AST //if true, then one or more machines were specified for this server type
		//in the shard setup file

	U32 bNonRelocatable : 1; //if true, then if a machine is specified for this server, the server can not be launched on any other machine
		//(triggers an alert if it wants to launch one)



	U32 bPutInfoAboutMeInShardClusterSummary : 1; //if true, then include information about me
		//in the summary of the shard reported to other shards in the cluster

	char executableName32_original[256]; AST(NAME(executableName))
	U32 iExecutable32OriginalCRC;
	
	char executableName64_original[256]; AST(NAME(executableName64))//if nonempty, then a 64-bit version of this executable exists, and should be used
		//on x64 machines
	U32 iExecutable64OriginalCRC;

	char *pExecutableName32_FrankenBuilt; AST(ESTRING)
	U32 iExecutable32FrankenBuiltCRC;

	char *pExecutableName64_FrankenBuilt; AST(ESTRING)
	U32 iExecutable64FrankenBuiltCRC;

	enumFrankenState eFrankenState;

	ServerDependency **ppServerDependencies; //EArray of ServerDependencies

	char monitoringWaitState[256]; // if nonempty, then the server can not be monitored until this string
								   // is a substring of the server's state string

	char *pSharedCommandLine_FromMCP; NO_AST //EString
	char *pSharedCommandLine_FromScript; NO_AST //EString
	char *pSharedCommandLine_Combined; NO_AST //EString

	bool bInUse; NO_AST

	enumVisibilitySetting eVisibility;
	enumVisibilitySetting eVisibility_ProdMode;
	enumVisibilitySetting eVisibility_DevMode;
	enumVisibilitySetting eVisibility_FromScript; //first loaded as part of server type info, then potentially overridden by general
		//script stuff, then by specific script launching command
	enumVisibilitySetting eVisibility_FromMCP;


	//if non-zero, then, in production mode, the controller will ask this server to send a keep-alive ever N/2
	//seconds, and will consider the server to have crashed/failed/whatever if it doesn't hear back for N seconds
	//(note that gameservers are handled separately)
	//(so if you set this to 10, controller will request a packet every 5 seconds, and then if 10 seconds 
	//pass without a packet, it's considered a failure)
	int iKeepAliveDelay;
	char keepAliveBeginState[256]; //don't start the keep-aliving until this state is reached
	
	int iStartupKeepAliveDelay; //after starting the server, if it doesn't get into its keep-alive state 
								//in this many seconds, treat it as if it has failed keepalive

	U32 bDumpAndKillOnKeepAliveStartupFail : 1;
	U32 bDumpAndKillOnKeepAliveFail : 1;


	U32 iUsedBits[3]; AST(USEDFIELD)

	//if set, then launch all servers of this type from this directory. Can be set from command line
	//via -ServerTypeOverrideLaunchDir TYPE dir, or set from MCP
	char *pOverrideLaunchDir; AST(ESTRING)

	//like above, uses -ServerTypeOverrideExeName
	char *pOverrideExeName; AST(ESTRING)
	U32 iOverrideExeCRC;


	char beginMemLeakTrackingState[256];
	int iMemLeakTracking_BeginDelay;
	int iMemLeakTracking_Frequency;
	int iMemLeakTracking_IncreaseAmountThatIsALeak_Megs;
	int iMemLeakTracking_FirstIncreaseAllowance_Megs;

	char *pInformMeAboutShardCluster_State; //once I'm in this state, start udpating me with the shard cluster overview

	const char *pCrashOrAssertAlertKey; NO_AST

} ServerTypeInfo;

AUTO_STRUCT;
typedef struct ServerTypeInfoList
{
	ServerTypeInfo **ppServerInfo;
} ServerTypeInfoList;



//sets up various options about how launching of gameservers work (defaults and comments moved into contorller.c
//as these are all controllerAutoSettings now.
AUTO_STRUCT;
typedef struct ControllerGameServerLaunchingConfig
{
    float fFreeMegsToCPUScaleFactor; 
    float fMinFreeRAMPercentCutoff; 
    float fMinFreeRAMMegsCutoff;
    float fMinFreeCPUCutoff;
} ControllerGameServerLaunchingConfig;

extern ControllerGameServerLaunchingConfig gGameServerLaunchingConfig;

//returns true if at least one machine can legally launch gameservers based on the above limitations, false otherwise
bool CheckForMachinesForLegalGameServerLaunch(void);







AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct ControllerScriptingCommandList
{
	ControllerScriptingCommand **ppCommands; AST(NAME(Command) FORMATSTRING(DEFAULT_FIELD=1))
} ControllerScriptingCommandList;


#define MAX_MACHINES 300

#define MAX_CONNECTED_SERVERS 800

//special prefix that is used to indicate that a command requested from the MPC is actually
//supposed to be executed on the controller itself (used for faked-up launcher menus)
#define COMMAND_ON_CONTROLLER "__COMMAND_ON_CONTROLLER"


extern VersionHistory *gpVersionHistory;
extern ServerTypeInfo gServerTypeInfo[GLOBALTYPE_MAXTYPES];
extern int giNumMachines;
extern int giTotalNumServers;
extern int giTotalNumServersByType[GLOBALTYPE_MAXTYPES];
extern TrackedMachineState gTrackedMachines[MAX_MACHINES];
extern TrackedServerState *gpServersByType[GLOBALTYPE_MAXTYPES];
extern TrackedMachineState *spLocalMachine;
extern bool sbNeedToKillAllServers;
extern StashTable sServerTables[GLOBALTYPE_MAXTYPES];
extern int gControllerTimer;
extern bool gbStartGameServersInDebugger;
extern char *pLauncherCommandLine;
extern NetComm *comm_controller;
extern bool gbStartLocalLauncherInDebugger;
extern bool gbHideLocalLauncher;
extern ControllerScriptingCommandList gCommandList;
extern char gExecutableDirectory[MAX_PATH];
extern char gCoreExecutableDirectory[MAX_PATH];
extern bool gbEverythingUsesPublicIPs;
extern bool gbProductionShardMode;
extern bool gbShardIsLocked;
extern bool gbGatewayIsLocked;
extern bool gbMTKillSwitch;
extern bool gbBillingKillSwitch;

extern bool gbUseSentryServer;
extern bool gbDoDataDirectoryCloning;

extern bool gbRequestMachineList;

//estring
extern ContainerID sUIDOfMCPThatStartedMe;
extern bool gbMCPThatStartedMeIsReady;
extern bool gbLaunchMonitoringMCP;

extern bool gbKillAllOtherShardExesAtStartup;

extern char gXBOXClientState[256];

extern U32 gControllerStartupTime; //secs since 2000 when the controller started running

extern StashTable gMachinesByName;

extern int iXBOXClientControllerScriptingCommandStepResult;
extern char XBOXClientControllerScriptingCommandStepResultString[256];

extern bool gbLaunch64BitWhenPossible;

extern bool gbCheckForFrankenbuildExes;

//there's an earray of current Alerts. Might convert to stashTable later
//extern ControllerAlert **ppAlerts; 
//extern AllAlertExpressions gAllAlertExpressions;
//extern AlertEmailRecipientList gAlertEmailRecipients;

//if empty, then this is not a patched controller
extern char gPatchingCommandLine[1024];


//if true, then fill in the above list periodically
extern bool gbGetMachineListFromSentryServer;

extern bool gbLeaveCrashesUpForever;

bool AllowShardVersionMismatch(void);
void SetAllowShardVersionMismatch(bool bSet);

extern int gbConnectToControllerTracker;

extern char gShardCategoryName[64];

extern char gNameToGiveToControllerTracker[128];

extern char *gpAutoPatchedClientCommandLine;

extern int giLongestDelayedGameServerTime;


typedef struct TPFileList TPFileList;

extern TPFileList *gpDataDirFileList;

extern U64 giControllerTick;
extern U64 giServerMonitoringBytes;

typedef struct ShardInfo_Perf ShardInfo_Perf;
extern ShardInfo_Perf gShardPerf;

extern bool gbShutdownWatcher;

extern bool gbDontPatchOtherMachines;

//as an earray
extern char **gppGSLBannedCommands;
//same as above, as a comma/newline separated list
extern char *gpGSLBannedCommandsString;


extern bool gbGameServerInactivityTimeoutMinutesSet;
extern int giGameServerInactivityTimeoutMinutes;

extern bool gbDataMirroringDeletion;


TrackedServerState *FindServerFromID(GlobalType eGlobalType, U32 iContainerID);

//respect the convention that ID 0 means "return the first one you find"
TrackedServerState *FindServerFromIDRespectingID0(GlobalType eGlobalType, U32 iContainerID);

TrackedMachineState *GetMachineFromNetLink(NetLink *pLink);
TrackedMachineState *FindMachineByName(const char *pName);
TrackedMachineState *FindMachineByIP(U32 iIP, U32 iLocalIP);

//returns whichever IP of that machine we are generally using (based on gbEverythingUsesPublicIPs)
U32 GetIPToUse(TrackedMachineState *pMachine);
U32 GetHostIpToUse(void);

void LinkAndInitServer(TrackedServerState *pServer, TrackedMachineState *pMachine, char *pReason, AdditionalServerLaunchInfo *pAdditionalInfo);
void TellLauncherAboutExistingServer(TrackedMachineState *pMachine, GlobalType eGlobalType, U32 iContainerID,
	U32 pid, int iLowLevelControllerIndex);
void KillServerByIDs(TrackedMachineState *pMachine, GlobalType eGlobalType, U32 iContainerID, char *pReason);
void KillServer(TrackedServerState *pServer, char *pReason);
void StopTrackingServer(TrackedServerState *pServer, char *pReason, bool bUnnaturalDeath, bool bAlreadyAlerted);

//NULL or "" working directory means launch in the current directory
typedef enum enumServerLaunchFlag
{
LAUNCHFLAG_FAILURE_IS_FATAL = 1,
LAUNCHFLAG_FAILURE_IS_ALERTABLE = 1 << 1,
} enumServerLaunchFlag;

TrackedServerState *RegisterNewServerAndMaybeLaunch(enumServerLaunchFlag eFlags, enumVisibilitySetting eVisibility, TrackedMachineState *pMachine, GlobalType eGlobalType, U32 iContainerID, bool bStartInDebugger,
			char *pCommandLine, char *pWorkingDirectory, bool bSkipWaiting, int iTestClientLinkNum, AdditionalServerLaunchInfo *pAdditionalInfo, char *pReason, ...);
TrackedMachineState *FindDefaultMachineForTypeInIPRange(GlobalType eGlobalType, char *pCategory, U32 iMinIP, U32 iMaxIP, AdditionalServerLaunchInfo *pAdditionalInfo);
static __forceinline TrackedMachineState *FindDefaultMachineForType(GlobalType eGlobalType, char *pCategory, AdditionalServerLaunchInfo *pAdditionalInfo)
{
	return FindDefaultMachineForTypeInIPRange(eGlobalType, pCategory, 0, 0, pAdditionalInfo);
}

void FindAllMachinesForServerType(GlobalType eContainerType, TrackedMachineState ***pppOutMachines);

void SendLauncherMonitoringInfoToMCP(U32 iLauncherID, TrackedServerState *pMCP);
void InformControllerOfServerState(int eGlobalType, U32 iContainerID, char *pStateString);
void SendErrorDialogToMCPThroughController(char *str, char* title, char* fault, int highlight);
U32 GetFreeContainerID(GlobalType eGlobalType);

void ApplyCommandConfigStuffToMachine(TrackedMachineState *pMachine);

void ControllerHandleMsg(Packet *pak,int cmd, NetLink *link, TrackedServerState **ppServer);
int ControllerClientConnect(NetLink* link,TrackedServerState **ppServer);

bool ControllerStartup_ConnectionStuffWithSentryServer(char **ppErrorEString);

void CreateLocalLauncher(void);
bool ServerTypeIsReadyToLaunchOnMachine(GlobalType eType, TrackedMachineState *pMachine, GlobalType *pTypeBeingWaitedOn);
bool ServerTypeIsReadyToLaunchOnMachines(GlobalType eType, TrackedMachineState **ppMachines);
bool ServerDependencyIsFulfilledForMachine(GlobalType eType, TrackedMachineState *pMachine, ServerDependency *pDependency);
void DoAnyDependencyAutoLaunchingForServerTypeOnMachine(GlobalType eType, TrackedMachineState *pMachine);


void ControllerScripting_Update();
void ControllerScripting_Load();
void ControllerScripting_LogString(char *pString, bool bIsSubHead, bool bIsError);
bool ControllerScripting_IsRunning();
void ControllerScripting_ServerDied(TrackedServerState *pServer);
void ControllerScripting_ServerCrashed(TrackedServerState *pServer);
char *ControllerScripting_GetCurStepName();
void ControllerScripting_Fail(char *pFailureString);
void ControllerScripting_LoadWhileRunning(char *pScriptName);
void ControllerScripting_Cancel();

void ReturnXPathForHttp(GlobalType eTypeOfProvidingServer, ContainerID eIDOfProvidingServer, int iRequestID, ContainerID iMCPContainerID, StructInfoForHttpXpath *pStructInfo);

void SendMonitoringCommandResultToMCP(ContainerID iMCPID, int iRequestID, int iClientID, char *pMessageString, void *pUserData);

void LogControllerOverview(void);

bool Controller_RequestOpenMachineListFromSentryServer(void);
bool Controller_RequestAllMachineListFromSentryServer(void);
void SentryServerMessageCB(Packet *pak,int cmd, NetLink *link, void *pUserData);


char *GrabMachineForShardAndMaybePatch(char *pMachineName, bool bKillEverything, bool bPatch, char *pPatchJobName);

//two steps, 0 and 1, divided so that we can do all the slow stuff before
//we start launching launchers
void GrabMachineForShardAndMaybePatch_Internal(char *pMachineName, int iStepNum, bool bKillEverything, bool bPatch, bool bFirstTimeConnectingToThisMachine, char *pPatchJobName);


void UpdateControllerTrackerConnections(void);
void SendVersionInfoToControllerTracker(void);

void SendPerfInfoToControllerTracker(bool bAlsoLog);
void SendLoginServerIPsToControllerTrackers(void);


void ReturnJpegToMCP(ContainerID iMCPID, int iRequestID, char *pData, int iDataSize, int iLifeSpan, char *pErrorMessage);

void PutExtraStuffInLauncherCommandLine(char **ppFullCommandLine, U32 iContainerID);


//if an important-but-not-critical server like a mapmanager crashes or asserts in a production shard, we initially do nothing,
//trusting CrypticError to do its magical thing and then kill the process. The process going away will make a new one launch
//automatically. However, just in case that fails, we kill it after a while
#define DELAY_BEFORE_KILLING_SERVERS_THAT_WILL_BE_RESTARTED (120.0f)

//tells a launcher to forget that a given server is part of the shard. This allows an infinite looping gameserver to be left alive
//but ignored
void TellLauncherToIgnoreServer(TrackedServerState *pServer, int iSecsToRemainIgnored);


void InformOtherServersOfServerDeath(GlobalType eContainerType, ContainerID iContainerID, char *pReason, bool bUnnaturalDeath);

void Controller_FailWithAlert(char *pErrorString);
void RequestOpenMachineListFromSentryServer_DoIt(void);

void VersionHistory_UpdateFSM(void);



void BeginKeepAlive(TrackedServerState *pServer);

bool ServerReadyForMonitoring(TrackedServerState *pServer);

char *GetGlobalSharedCommandLine(void);
void SetGlobalSharedCommandLine(char *pStr);
void AppendGlobalSharedCommandLine(char *pStr);
void SetGlobalSharedCommandLine_FromMCP(char *pStr);


TrackedServerState *FindRandomServerOfType(GlobalType eType);


void KillServer_Delayed(GlobalType eContainerType, ContainerID iID, float fDelay);

bool ControllerMirroringIsActive(void);

void InformControllerOfServerState_Internal(TrackedServerState *pServer, char *pStateString);

char **GetListOfMustShutItselfDownExes(void);

//all servers are in an earray, and new servers are added at the end. BUT, when a server is removed, it
//is just nulled out and left in the earray, and added to a linked list of empty servers. Servers
//also have a lowLevelIndex. This is used in performance critical cases where we want to very quickly
//look go from type/ID to server pointer. Instead of doing a stash table lookup, we go from
//type/ID/index directly to the pointer, then verify that the type and ID match.
//
//note that index of 0 is invalid, so we always push a single empty server as the first 
//element of the low level list
extern TrackedServerState **ppLowLevelServerList;
extern TrackedServerState *pFirstLowLevelEmptyServer;

static __forceinline TrackedServerState *ServerFromTypeAndIDAndIndex(GlobalType eType, ContainerID ID, int iIndex)
{
	if (iIndex && ppLowLevelServerList[iIndex]->eContainerType == eType && ppLowLevelServerList[iIndex]->iContainerID == ID)
	{
		return ppLowLevelServerList[iIndex];
	}

	return FindServerFromID(eType, ID);
}

void SendCommandToAllServersOfType(GlobalType eType, ContainerID iExceptThisID, ACMD_SENTENCE pCommandStringToSend);

//briefly keep track of servers after they have died, so we don't think they're zombies
void SetServerJustDied(GlobalType eType, ContainerID iID);
bool ServerRecentlyDied(GlobalType eType, ContainerID iID);
void UpdateRecentlyDiedServers(void);

//stuff to do when scripting is done, or immediately if there is no scripting. OK to call it multiple times,
//it only does the stuff once
void Contoller_StartupStuffToDoWhenScriptingNotRunning(void);

//returns the filename (with dir) into which a given server should fork its printfs, along with the "-ForkPrintfsToFile" command,
//or ""
char *GetPrintfForkingCommand(GlobalType eType, ContainerID iID);

void HandleDataMirroringReport(Packet *pak, NetLink *link);

//returns the value in eVisilibility, if set. Otherwise evaluates all the set values in gServerTypeInfo, in the correct
//order
bool ShouldWindowActuallyBeHidden(enumVisibilitySetting eVisibility, GlobalType eContainerType);

//returns how many machines in the shard are "supposed" to launch servers of a given type
int CountMachinesForServerType(GlobalType eType);


void SendCommandDirectlyToServerThroughNetLink(TrackedServerState *pServer, char *pCmdString);

extern float gfLastReportedControllerFPS;

extern int giCheckForEmptyGameServersInterval;


void Controller_SetServerMonitorBannerString(const char *pString);

U32 GetPIDOfMCPThatStartedMe(void);

bool Controller_MachineIsLegalForGameServerLaunch(TrackedMachineState *pMachine);
float Controller_FindServerLaunchingWeight(TrackedMachineState *pMachine, GlobalType eContainerType, AdditionalServerLaunchInfo *pAdditionalInfo, char **ppDescString);

bool DependencyIsActive(ServerDependency *pDependency);

//whenever we're checking if things are stalling on a machine, we generally use the last contact time for that machine's launcher
//as the "current time" for comparisons, as we assume the launcher will always keep running, and if the whole machine is stalled,
//we don't want to report that tons of individual servers are stalled, but we also want to put a cap on how far behind current
//time we will let that lag
U32 GetCurTimeToUseForStallChecking(TrackedMachineState *pMachine);

//in controller_http.c
ControllerOverview *GetControllerOverview(bool bForceFullList, const char *pAuthNameAndIP);
void GetShardLockageString(char **ppOutString);


//the first time you call this it will return nothing, as it has to subscribe with the object DB before the
//container will be populated
//
//in controller_http.c
ShardVariableContainer *Controller_GetShardVariableContainer(bool *pbAlreadyWaiting);