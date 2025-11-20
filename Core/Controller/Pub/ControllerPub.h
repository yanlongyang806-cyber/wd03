#pragma once
#include "GlobalTypeEnum.h"
#include "svrGlobalInfo.h"
#include "ControllerStartupSupport.h"

typedef struct RemoteCommandGroup RemoteCommandGroup;
typedef struct NetLink NetLink;
typedef struct JobManagerGlobalInfo JobManagerGlobalInfo;
typedef struct DynHoggPruningState DynHoggPruningState;
typedef struct ConditionChecker ConditionChecker;
typedef struct SystemSpecs SystemSpecs;
typedef struct AutoSetting_SingleSetting AutoSetting_SingleSetting;
typedef struct ControllerInterestingStuff ControllerInterestingStuff;

extern StaticDefineInt enumMachineCanLaunchServerSettingEnum[]; 

//whether various commands specify the visibility state of console windows
AUTO_ENUM;
typedef enum
{
	VIS_UNSPEC,
	VIS_VISIBLE,
	VIS_HIDDEN,
} enumVisibilitySetting;


AUTO_ENUM;
typedef enum ControllerMapCreationFailureType
{
	FAILURE_UNSPECIFIED,
	FAILURE_NO_MACHINE_FOR_GAME_SERVER,
} ControllerMapCreationFailureType;


AUTO_STRUCT;
typedef struct Controller_SingleServerInfo
{
	GlobalType eServerType;
	U32 iGlobalID;
	U32 iIP;
	U32 iPublicIP;
	char stateString[64];
	int pid;

	//not necessarily filled in all the time. Check the particular usage carefully.
	char machineName[128];

	//this struct is used as the return value of the StartServer remote command, so it sometimes returns
	//failure (if iIP == 0, then it failed). If it does, then this field sometimes (although not always)
	//indicates what type of failure
	ControllerMapCreationFailureType eFailureType;
} Controller_SingleServerInfo;

AUTO_STRUCT;
typedef struct Controller_ServerList
{
	Controller_SingleServerInfo **ppServers;
} Controller_ServerList;

AUTO_STRUCT;
typedef struct Controller_KillAllButSomeServersOfTypeInfo
{
	char *pReason;
	GlobalType eServerType;
	ContainerID *pIDsNotToKill;
} Controller_KillAllButSomeServersOfTypeInfo;

AUTO_STRUCT;
typedef struct GenericServerInfoForMonitoring
{
	char *pTPIString; //a string that describes the TPI... comes from ParseTableWriteText
	char *pStructString; //a string that describes the struct
} GenericServerInfoForMonitoring;

AUTO_ENUM;
typedef enum
{
	CONTROLLERSCRIPTING_NOTRUNNING,
	CONTROLLERSCRIPTING_LOADING,
	CONTROLLERSCRIPTING_RUNNING,
	CONTROLLERSCRIPTING_SUCCEEDED,
	CONTROLLERSCRIPTING_FAILED,
	CONTROLLERSCRIPTING_COMPLETE_W_ERRORS,

	//should be used sparingly, as it just halts nonelegantly
	CONTROLLERSCRIPTING_CANCELLED,
} enumControllerScriptingState;



AUTO_STRUCT;
typedef struct Controller_GenericServerOverview
{
	AST_COMMAND("Kill_NoRestart", "KillServerCmd_NoRestart $FIELD(Type) $FIELD(ID) $STRING(Type yes to kill AND NOT RESTART $FIELD(Type) $FIELD(ID))")
	AST_COMMAND("Kill_Normal", "KillServerCmd_Normal $FIELD(Type) $FIELD(ID) $CONFIRM(Really Kill $FIELD(Type) $FIELD(ID)) $NORETURN")
	AST_COMMAND("GetDump", "GetDumpCmd $FIELD(Type) $FIELD(ID) $CONFIRM(Really get a dump for $FIELD(Type) $FIELD(ID)) $NORETURN")

	char *pMuteAlerts; AST(ESTRING, FORMATSTRING(command=1))
	char *pUnMuteAlerts;  AST(ESTRING, FORMATSTRING(command=1))

	char *pVNC; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1))
	char *pType; AST(ESTRING)
	U32 iID; AST(FORMATSTRING(accessLevel=9))
	float fFPS;
	char *pMonitorKey; AST(NO_LOG, KEY, ESTRING, FORMATSTRING(HTML_SKIP=1)) //this is a stupid way of doing a multi-column key
	char *pMachine; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1))
	char *pMachine_IP; AST(ESTRING, FORMATSTRING(HTML_SKIP=1), ADDNAMES(MachineName))
	char *pMachine_StringName; AST(ESTRING, FORMATSTRING(HTML_SKIP=1))
	char *pLink; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1))
	char *pState; AST(ESTRING)
	U32 iCreationTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))
	char *pError; AST(ESTRING, FORMATSTRING(HTML=1))
	U32 iRAMMegs; AST(FORMATSTRING(HTML_SKIP=1))
	float fCPUPercent; AST(FORMATSTRING(HTML_SKIP=1))
} Controller_GenericServerOverview;

AUTO_STRUCT;
typedef struct Controller_GameServerOverview
{
	Controller_GenericServerOverview gGenericInfo; AST(EMBEDDED_FLAT)	
	GameServerGlobalInfo gGlobalInfo; AST(EMBEDDED_FLAT)
	bool bLocked; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 0 ; divWarning2"))
	AST_COMMAND("Broadcast", "BroadcastMessageToGameServer $FIELD(ID) $STRING(Message to Broadcast)$NORETURN")
	AST_COMMAND("Profile", "TimerRecordStartAutoRemotely $FIELD(ID)")
	AST_COMMAND("Lock", "LockGameServer $FIELD(ID) $INT(1 to lock, 0 to unlock) $NORETURN")
	AST_COMMAND("GracefulShutdown", "ShutDownGameServer $FIELD(ID) $CONFIRM(Really lock this server, then boot all players, then kill it?)")
} Controller_GameServerOverview;

AUTO_STRUCT;
typedef struct Controller_GatewayServerOverview
{
	Controller_GenericServerOverview gGenericInfo; AST(EMBEDDED_FLAT)	
	GatewayServerGlobalInfo gGlobalInfo; AST(EMBEDDED_FLAT)
} Controller_GatewayServerOverview;

AUTO_STRUCT;
typedef struct Controller_ClientControllerOverview
{
	Controller_GenericServerOverview gGenericInfo; AST(EMBEDDED_FLAT)	
} Controller_ClientControllerOverview;

AUTO_STRUCT;
typedef struct Controller_TransactionServerOverview
{
	Controller_GenericServerOverview gGenericInfo; AST(EMBEDDED_FLAT)	
	TransactionServerGlobalInfo gGlobalInfo; AST(EMBEDDED_FLAT)
} Controller_TransactionServerOverview;

AUTO_STRUCT;
typedef struct Controller_DatabaseOverview
{
	Controller_GenericServerOverview gGenericInfo; AST(EMBEDDED_FLAT)	
	DatabaseGlobalInfo gGlobalInfo; AST(EMBEDDED_FLAT)
} Controller_DatabaseOverview;



AUTO_STRUCT;
typedef struct Controller_MachineOverview
{
	char *pVNC; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1))
	char *pMachineName_Internal; AST(ESTRING, FORMATSTRING(HTML_SKIP=1))//actually contains the machine name, because pMachineName is a link
	char *pMachineName; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1))
	U32 iPublicIP; AST(FORMAT_IP)
	U32 iPrivateIP; AST(FORMAT_IP)
	int iNumServers;
	TrackedPerformance performance; AST(EMBEDDED_FLAT)
	U32 iConnectionTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))
	AST_COMMAND("Broadcast a message", "BroadcastMessageToMachine $FIELD(MachineName_Internal) $STRING(Message to Broadcast)$NORETURN")
	AST_COMMAND("Lock", "LockMachine $FIELD(MachineName_Internal) $INT(1 to lock, 0 to unlock) $NORETURN")
	AST_COMMAND("LockAllGameservers", "LockAllGameservers $FIELD(MachineName_Internal) $INT(1 to lock, 0 to unlock) $NORETURN")
} Controller_MachineOverview;

AUTO_STRUCT;
typedef struct Controller_DeadMachineOverview
{
	char *pVNC; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1))
	char *pMachineName_Internal; AST(ESTRING, FORMATSTRING(HTML_SKIP=1))//actually contains the machine name, because pMachineName is a link
	char *pMachineName; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1))
	U32 iPublicIP; AST(FORMAT_IP)
	U32 iPrivateIP; AST(FORMAT_IP)
	AST_COMMAND("Attempt Reconnection", "GrabMachineForShardAndMaybePatch $FIELD(MachineName_Internal) $INT(Kill everything) $INT(Patch) Attempt_Reconnection")
} Controller_DeadMachineOverview;

AUTO_STRUCT;
typedef struct Controller_OpenMachineOverview
{
	char *pGrab; AST(NO_LOG, ESTRING, FORMATSTRING(command=1))
	char *pGrabAndPatch; AST(NO_LOG, ESTRING, FORMATSTRING(command=1))
	char *pMachineName;
} Controller_OpenMachineOverview;

AUTO_STRUCT;
typedef struct VersionHistoryEntry
{
	U32 iStartTime; AST(FORMATSTRING(HTML_SECS=1))
	char *pPatchName;
} VersionHistoryEntry;

AUTO_STRUCT;
typedef struct VersionHistory
{
	VersionHistoryEntry **ppEntries; //most recent == 0
} VersionHistory; 


AUTO_STRUCT;
typedef struct Controller_ProductionGameServerSummary
{
	char *pCommentString; AST(ESTRING FORMATSTRING(HTML=1))
	Controller_GameServerOverview **ppServersWithAlerts; //display up to 20 game servers with state-based alerts on the front page
} Controller_ProductionGameServerSummary;

AUTO_STRUCT;
typedef struct Controller_ProductionMachineSummary
{
	char *pCommentString; AST(ESTRING FORMATSTRING(HTML=1))
	Controller_MachineOverview **ppMachinesWithAlerts;
} Controller_ProductionMachineSummary;

AUTO_STRUCT;
typedef struct ControllerUGCOverview
{
	int iEditingGameservers;
	int iOtherUGCShardGameServers;
	int iPlayingUGCGameServers;
	int iPlayingUGCPlayers;

	JobManagerGlobalInfo *pJobManagerInfo;

} ControllerUGCOverview;


AUTO_STRUCT;
typedef struct ControllerAlertListOverview
{
	int iTotalCount;
	int iLastHour; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 10 ; divAlertLevelFAILURE ; $ > 3 ; divAlertLevelCRITICAL ; $ > 0 ; divAlertLevelWARNING")) 
	char *pLink; AST(NO_LOG, ESTRING, FORMATSTRING(html=1))
} ControllerAlertListOverview;

AUTO_STRUCT;
typedef struct ControllerAlertCategoryOverview
{
	char *pLink; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
	ControllerAlertListOverview **ppAlerts; AST(FORMATSTRING(HTML_NO_HEADER=1))
} ControllerAlertCategoryOverview;

AUTO_STRUCT;
typedef struct ControllerAlertsOverview
{
	ControllerAlertCategoryOverview Critical; AST(FORMATSTRING(HTML_NO_HEADER=1))
	ControllerAlertCategoryOverview Warning; AST(FORMATSTRING(HTML_NO_HEADER=1))
} ControllerAlertsOverview;
	

AUTO_STRUCT;
typedef struct ControllerOverview
{
	char *pBannerString; AST(ESTRING, FORMATSTRING(HTML_CLASS="Alerts", HTML_NO_HEADER=1))
	char *pTheShardIsLocked; AST(ESTRING, FORMATSTRING(HTML_CLASS="Alerts", HTML_NO_HEADER = 1))
	char *pSharedMemoryIsDisabled; AST(ESTRING, FORMATSTRING(HTML_CLASS="Alerts"))
	char *pFrankenBuilds; AST(ESTRING, FORMATSTRING(HTML=1, HTML_CLASS="Alerts", HTML_NO_HEADER = 1))
	U32 iShardCreationTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1, HTML_CLASS="SideBySide"))
	float fControllerFPS;  AST(FORMATSTRING(HTML_CLASS="SideBySide"))
	char *pVersion; AST(ESTRING FORMATSTRING(HTML_NOTES="The Version on the serverMon front page"))

	char *pStartupStatus; 

	ControllerAlertsOverview alertsOverview; AST(FORMATSTRING(HTML_CLASS="AlertsOverview",HTML_PRE_DIV_CLOSE="<div style=\\qclear: both\\q></div>"))

	char *pClientControllers; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NOTES="The ClientControllers link on the serverMon front page"))
	Controller_TransactionServerOverview **ppTransactionServer;   AST(FORMATSTRING(HTML_NOTES="The Transaction Server table on the serverMon front page"))
	Controller_DatabaseOverview **ppDatabases; AST(FORMATSTRING(HTML_NOTES="The Databases table on the serverMon front page"))

	//used if there are < 10 gameservers
	Controller_GameServerOverview **ppGameServers; AST(FORMATSTRING(HTML_NOTES="The GameServers table on the serverMon front page"))

	//used if there are 10+ gameservers
	Controller_ProductionGameServerSummary *pProductionGameServerSummary; AST(FORMATSTRING(HTML_NOTES="the GameServers summary on the servermon front page"))

	Controller_GenericServerOverview **ppOtherServers; AST(FORMATSTRING(HTML_NOTES="The servers table on the serverMon front page"))

	//used if there are are < 10 machines
	Controller_MachineOverview **ppMachines; AST(FORMATSTRING(HTML_NOTES="The machines table on the serverMon front page"))
	
	//used if there are 10+ machines
	Controller_ProductionMachineSummary *pProductionMachineSummary; AST(FORMATSTRING(HTML_NOTES="The machine summary on the serverMon front page"))

	Controller_DeadMachineOverview **ppDeadMachines; AST(NAME(MachinesWithDeadOrStalledLaunchers) FORMATSTRING(HTML_NOTES="The deadMachines table on the serverMon front page"))
//	Controller_ClientControllerOverview **ppClientControllers;
	Controller_OpenMachineOverview **ppOpenMachines; AST(FORMATSTRING(HTML_NOTES="The openMachines table on the serverMon front page"))

	LogServerGlobalInfo *pLogServerGlobalInfo; //only present in the full version logged, never seen in serverMon


	char *pGenericInfo; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1, HTML_NOTES="The Generic Info link on the serverMon front page"))
	char *pServerConfiguration; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1, HTML_NOTES="The Server configuration link on the serverMon front page"))
	char *pControllerAutoSettings; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1, HTML_NOTES="The AUTO_SETTINGS link on the serverMon front page"))
	char *pGameServerLaunchingConfig; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1, HTML_NOTES="The GS Launch config link on the serverMon front page"))
	char *pShardVariables; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1, HTML_NOTES="The ShardVariables link on the serverMon front page"))
	char *pUGCOverview; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1, HTML_NOTES="The UGC Overview link on the serverMon front page"))
	

	VersionHistory *pVersionHistory; AST(FORMATSTRING(HTML_NOTES="The version history table on the serverMon front page"))


	AST_COMMAND("Broadcast a message", "BroadcastMessage $STRING(Message to Broadcast)$NORETURN")
//	AST_COMMAND("Reload Alerts", "ReloadAlerts $CONFIRM(Really reload alerts?)")
	AST_COMMAND("Dump Shard Setup", "DumpShardSetupFile $CONFIRM(Really dump shard setup file) $STRING(Filename w/o extension) $NORETURN")
	AST_COMMAND("Update Last Minute Files", "SendLastMinuteFilesToControllerTracker")
	AST_COMMAND("Boot everyone", "BootEveryone $STRING(type yes to boot everyone) 1")
	AST_COMMAND("Lock the Shard (also locks Gateway)", "LockTheShard $INT(1 to lock, 0 to unlock) $NORETURN")
	AST_COMMAND("Lock only Gateway", "GatewayLockTheShard $INT(1 to lock, 0 to unlock) $NORETURN")
	AST_COMMAND("Microtransaction kill switch", "MTKillSwitch $INT(1 to disallow microtransactions, 0 to allow) $NORETURN")
	AST_COMMAND("Billing kill switch", "BillingKillSwitch $INT(1 to disallow billing, 0 to allow) $NORETURN")
	AST_COMMAND("Set Delay Before Killing Stalled Gameserver", "DelayBeforeKillingStalledGameserver $STRING(Seconds to delay) $NORETURN")
//	AST_COMMAND("Test checkbox command", "TestCheckboxCommand $CHECKBOX(Here is a checkbox) $CHECKBOX(and another one) $CHECKBOX(and a third) $NORETURN")
	char *pToggleMonitoringLaunchersAndMultiplexers; AST(NO_LOG, ESTRING, FORMATSTRING(command=1))

	char *pQuerySentryServerForOpenMachines; AST(NO_LOG, ESTRING, FORMATSTRING(command=1))

	char *pConfirm;AST(NO_LOG, ESTRING, FORMATSTRING(command=1))

	char *pSetStressTestLogsPerServerPerSecond; AST(NO_LOG, ESTRING, FORMATSTRING(command=1))

//	AST_COMMAND("testCommand", "httpTestCommand $INT(x) $FLOAT(f) $SELECT(string|happy,sad,crazy,wacky)")
	AST_COMMAND("Boot a player by account id", "BootPlayerByAccountID $INT(Account id to boot) $NORETURN")
	AST_COMMAND("Boot a player by account name", "BootPlayerByAccountName $STRING(Account name to boot) $NORETURN")
	AST_COMMAND("Boot a player by display name", "BootPlayerByDisplayName $STRING(Display name to boot) $NORETURN")
	AST_COMMAND("Set max players threshold", "SetMaxPlayers $INT(Number of players)")
	AST_COMMAND("Set soft max players threshold", "SetSoftMaxPlayers $INT(Number of players)")
	AST_COMMAND("Set max logins threshold", "SetMaxLogins $INT(Number of players) $NORETURN")
	AST_COMMAND("Set max queue threshold", "SetMaxQueue $INT(Number of players) $NORETURN")
	AST_COMMAND("Set login rate", "SetLoginRate $INT(Number of players per minute) $NORETURN")
	AST_COMMAND("Set all GS inactive timeout", "SetAllGameServerInactivityTimeoutMinutes $INT(Minutes) $NORETURN")
	AST_COMMAND("Reload GSL Banned Commands", "ReloadGSLBannedCommands")
	AST_COMMAND("Acknowledge all duplicate alerts", "AcknowledgeAllDuplicateAlerts")
	AST_COMMAND("Prime the shard", "DoPrePatching $STRING(Patch version) $INT(How many machine groups to prime) $STRING(Are you really sure? Type yes here if you are. Double check your patch version!) $STRING(Extra patching command line)")
	AST_COMMAND("Shutdown", "ShutDownShard $STRING(Enter the shard name to shut down)")
}ControllerOverview;



AUTO_STRUCT;
typedef struct MachineCanLaunchServerSetting
{
	enumMachineCanLaunchServerSetting eCanLaunch;
	int iPriority; //higher = more likely to launch, does NOT apply to gameservers which have their own entire universe of loadbalancing stuff
	const char **ppCategories; //pool_strings
} MachineCanLaunchServerSetting;

//debug-only information that is passed along with a server launch command so that 
//we can send a little window on the client telling that client what machine/pid their 
//new server was launched on
AUTO_STRUCT;
typedef struct ServerLaunchDebugNotificationInfo
{
	GlobalType eServerType;
	ContainerID eServerID;
	U32 iCookie;
} ServerLaunchDebugNotificationInfo;

//
#define MAX_UPDATE_TICK_TIMES_TO_SAVE_FOR_SIMPLE_SERVER_LOG 20
#define MAX_UPDATES_TO_SAVE_FOR_SIMPLE_SERVER_LOG 20

AUTO_STRUCT;
typedef struct SimpleServerUpdate
{
	U32 iTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))
	char *pString;
} SimpleServerUpdate;

AUTO_STRUCT;
typedef struct SimpleServerLog
{
	int iNextUpdateTickTime;
	U32 iUpdateTickTimes[MAX_UPDATE_TICK_TIMES_TO_SAVE_FOR_SIMPLE_SERVER_LOG]; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))

	int iNextUpdate;
	SimpleServerUpdate **ppUpdates; //will always have size MAX_UPDATES_TO_SAVE_FOR_SIMPLE_SERVER_LOG
} SimpleServerLog;


//using USERFLAG(TOK_USEROPTIONBIT_1) to mask out things that shouldn't be logged with alerts
AUTO_STRUCT;
typedef struct TrackedServerState
{
	char VNC[256]; AST(FORMATSTRING(HTML=1) USERFLAG(TOK_USEROPTIONBIT_1))
	char monitor[512]; AST(FORMATSTRING(HTML=1) USERFLAG(TOK_USEROPTIONBIT_1))
	GlobalType eContainerType; AST(SUBTABLE(GlobalTypeEnum))
	U32 iContainerID;
	char uniqueName[64]; AST(KEY) //directly derived from the above... for instance, "GAMESERVER 15"

	int iScriptingIndex; //used by controller scripting to refer to servers. Starts at 0 and increments per server type

	int iLowLevelIndex; //index in ppLowLevelServerList, used for very-fast-lookup (pServerFromTypeAndIDAndIndex)

	float fCreationTimeFloat;
	U32 iCreationTime;  AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))

	float fLauncherReconnectTime;

	struct TrackedMachineState *pMachine; NO_AST
	char machineLink[512]; AST(FORMATSTRING(HTML=1) USERFLAG(TOK_USEROPTIONBIT_1))

	NetLink *pLink; NO_AST

	bool bWaitingForUpdate; 

	//track whether we deliberately killed this server. If we didn't, then if it crashes
	//or quits, we need to check whether we want to do something to resurrect it, or what
	//have you
	bool bKilledIntentionally; 

	//tracks whether this server gracefully shut down
	bool bKilledGracefully;

	//set in concord with KilledIntentionally
	bool bRecreateDespiteIntentionalKill;

	//whether the launcher has reported that this thing is crashed
	bool bHasCrashed;

	//the current state string for this server, if provided
	char stateString[1024];

	//this server hasn't actually launched yet, because it is waiting for another server to reach a specified state
	bool bWaitingToLaunch;

	//has already reached the state where it wants to be told about shard cluster... so send it any further difs that arrive
	bool bBeganInformingAboutShardCluster;

	//servers that are waiting to launch need to store their launching parameters
	bool bStartInDebugger; 
	enumVisibilitySetting eVisibility;

	bool bAlertsMuted;

	bool bAlreadyDumpedForStalling; 
	bool bAlreadyAlertedSlowGracefulDeath;

	bool bHasGottenAutoSettingsViaRemoteCommand;

	char *pFullPath; AST(ESTRING)
	char *pCommandLine; AST(ESTRING)
	char *pWorkingDirectory; AST(ESTRING)
	U32 iExecutableCRC; NO_AST


	struct TrackedServerState *pNext; NO_AST
	struct TrackedServerState *pPrev; NO_AST

	bool bRemoving;

	//special stuff only for GameClients that are launched by controller scripting
	int iTestClientLinkNum; // if 0, this GameClient was not launched by controller scripting
	NetLink *pTestClientLink; NO_AST //the controller links to the game client, pretending to be a test client, so it can
								//send commands



	//a variable that can be set by remote command, so that other servers can inform the 
	//controller of controller scripting commands finishing. 1 = success, -1 = failure, 0 = not done
	int iControllerScriptingCommandStepResult;

	//string describing what happened, only used on failure
	char controllerScriptingCommandStepResultString[256];

	//if you add something here, make sure to modify DoTypeSpecificServerCleanup() and DoTypeSpecificServerInit()
	TransactionServerGlobalInfo *pTransServerSpecificInfo;
	GameServerGlobalInfo *pGameServerSpecificInfo;
	GatewayServerGlobalInfo *pGatewayServerSpecificInfo;
	DatabaseGlobalInfo *pDataBaseSpecificInfo;
	LoginServerGlobalInfo *pLoginServerSpecificInfo;
	LogServerGlobalInfo *pLogServerSpecificInfo;
	JobManagerGlobalInfo *pJobManagerSpecificInfo;

	//performance info for all processes
	ProcessPerformanceInfo perfInfo;

	SimpleServerLog simpleLog;

	//what error ID this server has crashed/asserted with
	int iErrorID;

	int PID; NO_AST

	char launchReason[2048];

	U32 iNextExpectedKeepAliveTime; //if 0, there is no keep-alive going on, or it hasn't begun
	U32 iKeepAliveStartTime; //if keepalive hasn't started by this time, fail
	
	U32 iNoLagOrInactivityAlertsUntilTime; //if set, and if the set time is still in the future, don't
		//generate any lag or inactivity timeouts

	int iNumActiveStateBasedAlerts; //the number of state-based alerts currently going on

	AdditionalServerLaunchInfo additionalServerLaunchInfo; //copied from the launch request

	AST_COMMAND("Kill_NoRestart", "KillServerCmd_NoRestart $FIELD(ContainerType) $FIELD(ContainerID)  $STRING(Type yes to kill AND NOT RESTART $FIELD(Type) $FIELD(ID))")
	AST_COMMAND("Kill_Normal", "KillServerCmd_Normal $FIELD(ContainerType) $FIELD(ContainerID) $CONFIRM(Really Kill $FIELD(ContainerType) $FIELD(ContainerID)) $NORETURN")

	AST_COMMAND("Lock", "LockGameServer $FIELD(containerID) $INT(1 to lock, 0 to unlock) $NORETURN", "\q$FIELD(ContainerType)\q = \qGameServer\q")

	AST_COMMAND("Forget", "ForgetServer $FIELD(ContainerType) $FIELD(ContainerID) $STRING(Type yes here) $CONFIRM(Really forget $FIELD(ContainerType) $FIELD(ContainerID)? You should only do this in corner cases where the controller is convinced a server exists that doesn't exist, or something else unfortunate like that) $NORETURN")

	bool bLocked; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 0 ; divWarning2"))

	char *pMuteAlerts; AST(ESTRING, FORMATSTRING(command=1) USERFLAG(TOK_USEROPTIONBIT_1))
	char *pUnMuteAlerts;  AST(ESTRING, FORMATSTRING(command=1) USERFLAG(TOK_USEROPTIONBIT_1))

	ServerLaunchDebugNotificationInfo *pLaunchDebugNotification;

	RemoteCommandGroup **ppThingsToDoOnAnyClose; AST(LATEBIND NO_TEXT_SAVE USERFLAG(TOK_USEROPTIONBIT_1))
	RemoteCommandGroup **ppThingsToDoOnCrash; AST(LATEBIND NO_TEXT_SAVE USERFLAG(TOK_USEROPTIONBIT_1))

	ConditionChecker *pAlertsConditionChecker; NO_AST

	AST_COMMAND("Broadcast", "BroadcastMessageToGameServer $FIELD(ContainerID) $STRING(Message to Broadcast)$NORETURN", "\q$FIELD(ContainerType)\q = \qGameServer\q" )
	AST_COMMAND("Profile", "TimerRecordStartAutoRemotely $FIELD(ContainerID)", "\q$FIELD(ContainerType)\q = \qGameServer\q")
	AST_COMMAND("GetSimpleLog", "GetSimpleLogString $FIELD(ContainerType) $FIELD(ContainerID)")
} TrackedServerState;



AUTO_STRUCT  AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "mainLink, VNC, performance.cpuLast60, performance.FreeRAM, NumGameServers, performance.NumPlayers, performance.Weighted_GS_FPS, SharedMachineGroupIndex, GSLaunchWeight");
typedef struct TrackedMachineState
{
	U32 bIsLocalHost:1;
	U32 bIsX64;
	U32 IP; AST(FORMAT_IP) //the machine's private IP, as reported by the launcher
	U32 iPublicIP; AST(FORMAT_IP) //the machine's public IP, as reported by the launcher
	U32 iLastLauncherContactTime; AST(FORMATSTRING(HTML_SECS_AGO = 1))
	char machineName[128]; AST(KEY)
	char *VNCString; NO_AST //for now just points to machine name
	char mainLink[128]; AST(FORMATSTRING(HTML=1))
	TrackedServerState *pServersByType[GLOBALTYPE_MAXTYPES]; NO_AST
	MachineCanLaunchServerSetting canLaunchServerTypes[GLOBALTYPE_MAXTYPES]; NO_AST
	TrackedPerformance performance;
	int iCrashedServers;
	int iWaitingServers;
	int iTotalServers;
	int iServersByType[GLOBALTYPE_MAXTYPES]; AST(INDEX(GLOBALTYPE_GAMESERVER, NumGameServers), INDEX(GLOBALTYPE_TESTCLIENT, NumTestClients))

	int iNumActiveStateBasedAlerts;

	int iGameServerLaunchWeight;

	bool bIsLocked; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 0 ; divWarning2"))

	bool bLauncherIsCurrentlyStalled;

	bool bVerboseProcListLogging;

	int iPIDOfIgnoredServer;

	int iLastLauncherLag_Actual; //the acutal lag
	int iLastLauncherLag_ToUse; //lag, capped at a reaosnable value (60 seconds or so)

	int iSharedMachineGroupIndex; //0 = "main" machines, 1 = first added group, etc.

	U32 iLastTimeAlertedLauncherLag; AST(FORMATSTRING(HTML_SECS_AGO = 1))
	U32 iLastTimeAlertedCriticalLauncherLag; AST(FORMATSTRING(HTML_SECS_AGO = 1))

	U32 iLastTimeStalledGSDump; AST(FORMATSTRING(HTML_SECS_AGO = 1))

	char *pCurGSLaunchWeight_ForServerMonOnly; AST(ESTRING, NAME(GSLaunchWeight))

	char **ppDynNameSpacesLoadedByGameServers;

	char *pVNC; AST(ESTRING, FORMATSTRING(HTML=1))

	DynHoggPruningState *pDynHoggPruningState; AST(LATEBIND)

	SystemSpecs *pSystemSpecs; AST(LATEBIND)

	ConditionChecker *pLauncherKeepAliveConditionChecker; NO_AST

	AST_COMMAND("Broadcast a message", "BroadcastMessageToMachine $FIELD(machineName) $STRING(Message to Broadcast)$NORETURN")
	AST_COMMAND("Lock", "LockMachine $FIELD(machineName) $INT(1 to lock, 0 to unlock) $NORETURN")
	AST_COMMAND("LockAllGameServers", "LockAllGameServers $FIELD(machineName) $INT(1 to lock, 0 to unlock) $NORETURN")

} TrackedMachineState;

AUTO_STRUCT;
typedef struct ControllerMirrorInfo
{
	TrackedServerState **ppServers;
	TrackedMachineState **ppMachines;
} ControllerMirrorInfo;









//used during local executable mirroring
AUTO_STRUCT;
typedef struct LocalExecutableWithCRC
{
	char name[CRYPTIC_MAX_PATH];
	U32 iCRC;
} LocalExecutableWithCRC;

AUTO_STRUCT;
typedef struct LocalExecutableWithCRCList
{
	LocalExecutableWithCRC **ppList;
} LocalExecutableWithCRCList;

AUTO_STRUCT;
typedef struct ServerLaunchRequest
{
	char *pExeName;
	ContainerID iContainerID;
	GlobalType eContainerType;
	int iLowLevelControllerIndex;
	int iAntiZombificationCookie;
	bool bStartInDebugger;
	bool bStartHidden;
	U32 iTransactionServerIPToSpecify;
	U32 iLogServerIPToSpecify;
	char *pCommandLine; AST(ESTRING)
	char *pWorkingDirectory;
	U32 iExecutableCRC; 

	//fields that are not actually sent to the launcher, just set for logging purposes
	char *pLaunchReason; AST(POOL_STRING)
	char *pMachineName; AST(POOL_STRING) 


} ServerLaunchRequest;



//the status update that a shard sends to a clusterController
//
//a few really important things at the top level, then the entire ControllerOverview
AUTO_STRUCT;
typedef struct ControllerSummaryForClusterController
{
	const char *pShardNameForSummary; AST(POOL_STRING FORMATSTRING(HTML_SKIP_IN_TABLE=1))

//these two are not theoretically needed, they're just there for verification
	char *pClusterName; AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))
	char *pProductName; AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))
	
	char *pStartupStatus; AST(FORMATSTRING(HTML_PREFORMATTED=1))

	char *pLocked;  AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ ; divWarning2"))
	char *pPatchDir; AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))
	int iNumPlayers;
	int iNumGameServers;
	ControllerOverview *pOverview; AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))
	ControllerInterestingStuff *pInterestingStuff; AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))

} ControllerSummaryForClusterController;


AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "NumSettings, Comment");
typedef struct ControllerAutoSetting_Category
{
	const char *pName; AST(KEY POOL_STRING)
	int iNumSettings;
	char *pComment; AST(ESTRING)
	char *pAddSetting; AST(ESTRING FORMATSTRING(command=1))
	AutoSetting_SingleSetting **ppSettings; AST(LATEBIND)
} ControllerAutoSetting_Category;

//when the cluster controller requests a frankenbuild, it uses IDs above a certain range,
//which should not overlap with the ID range that the normal internally generated ones use
#define MIN_FRANKENBUILD_ID_FROM_CLUSTER_CONTROLLER 1000000000

//the controller sends these simplified representations of ShardVariables to the ClusterController
AUTO_STRUCT;
typedef struct ShardVariableForClusterController
{
	const char *pName; AST(KEY, POOL_STRING)
	char *pValueString; AST(ESTRING)
	char *pDefaultValueString; AST(ESTRING)
} ShardVariableForClusterController;

//this string will be stuck in the defaultValueString if something went wrong
#define SHARDVARIABLEFORCLUSTERCONTROLLER_BAD_DEFAULT_VALUE "(BAD SHARD VARAIABLE)"

AUTO_STRUCT;
typedef struct ShardVariableForClusterController_List
{
	ShardVariableForClusterController **ppVariables;
} ShardVariableForClusterController_List;


//interestingStuff
//"interesting stuff" is a custom page set up to answer as many of Nelson's queries as possible as fast as possible
AUTO_STRUCT;
typedef struct ControllerInterestingStuff_GameServer
{
	U32 iID;
	char *pMachineName; AST(POOL_STRING)
	float fFPS;
	float fCPUUsage; AST(FORMATSTRING(HTML_FLOAT_PREC = 2))
	U64 physicalMemUsed; AST(FORMATSTRING(HTML_BYTES = 1)) 
	U32 iCreationTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))
	int iNumPlayers;
	int iNumEntities;
	int iNumActiveEnts;
	char mapNameShort[64];
	bool bLocked;
	int iNumPartitions;
} ControllerInterestingStuff_GameServer;

AUTO_STRUCT;
typedef struct ControllerInterestingStuff_GatewayServer
{
	U32 iID;
	char *pMachineName; AST(POOL_STRING)
	float fFPS;
	int iCPUUsage;
	U64 physicalMemUsed; AST(FORMATSTRING(HTML_BYTES = 1)) 
	U32 iCreationTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))

	int iNumSessions;
	U64 iHeapTotal; AST(FORMATSTRING(HTML_BYTES = 1)) 
	U64 iHeapUsed; AST(FORMATSTRING(HTML_BYTES = 1)) 
	U64 iWorkingSet; AST(FORMATSTRING(HTML_BYTES = 1)) 

} ControllerInterestingStuff_GatewayServer;

AUTO_STRUCT;
typedef struct ControllerInterestingStuff_MachineSet
{
	int iTotalMachineCPU; NO_AST
	int iAvgMachineCPU;
	int iLowestCPU;
	char lowestCPUMachineName[64];
	int iHighestCPU;
	char highestCPUMachineName[64];

	U64 iTotalRAM; AST(FORMATSTRING(HTML_BYTES = 1))
	U64 iTotalFreeRAM; AST(FORMATSTRING(HTML_BYTES = 1))
	U64 iLowestFreeRAM; AST(FORMATSTRING(HTML_BYTES = 1))
	char lowestFreeRAMMachineName[64];
	U64 iHighestFreeRAM; AST(FORMATSTRING(HTML_BYTES = 1))
	char highestFreeRAMMachineName[64];

	int iNumMachines;

	int iNumMachines_CPU0to10;
	int iNumMachines_CPU11to80;
	int iNumMachines_CPU81to95;
	int iNumMachines_CPU96to100;
} ControllerInterestingStuff_MachineSet;

AUTO_STRUCT;
typedef struct ControllerInterestingStuff
{
	//interesting gameservers are the top and bottom few game servers on each of various categories;
	ControllerInterestingStuff_GameServer **ppGameServers;
	ControllerInterestingStuff_GatewayServer **ppGatewayServers;
	
	int iPlayersLoggingIn;
	int iNumGatewaySessions;
	
/*	int iPlayersInMainQueue;
	int iPlayersInVIPQueue;
	int iMeanTimeInQueue;
	int iMaxTimeInQueue;*/

	int iNumGameServersBelow10fps;
	int iNumGameServers10to25fps;
	int iNumGameServersAbove25fps;

	ControllerInterestingStuff_MachineSet allMachines; AST(EMBEDDED_FLAT)

	ControllerInterestingStuff_MachineSet gameServerMachines;
	ControllerInterestingStuff_MachineSet gatewayServerMachines;
	ControllerInterestingStuff_MachineSet nonGameServerMachines;

	int iLogsPerSecond;

	float fControllerFps;

	int iObjDBLastContactSecsAgo;
	int iTransServerLastContactSecsAgo;

	int iMainQueueSize;
	int iVIPQueueSize;

	int iMaxMainQueueSize;
	int iMaxMainQueueSize_LastDay;
	int iMaxMainQueueSize_LastHour;

	int iMaxVIPQueueSize;
	int iMaxVIPQueueSize_LastDay;
	int iMaxVIPQueueSize_LastHour;

	int iHowLongFirstGuyInMainQueueHasBeenWaiting;
	int iHowLongFirstGuyInVIPQueueHasBeenWaiting;
} 
ControllerInterestingStuff;
