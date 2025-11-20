#pragma once
GCC_SYSTEM

#include "alerts.h"

typedef struct NameValuePairList NameValuePairList;
typedef struct PCLStatusMonitoringUpdate PCLStatusMonitoringUpdate;

//when we do "extra" status reporting, which is how shards talk to the cluster controller, and how the cluster controller talks
//to the overlord, etc., we can have optional flags which affect what information we send. They are set by a latelink,
//so for instance the cluster controller knows that if it's reporting to an overlord, it should send the overlord all of its
//patch statuses
AUTO_ENUM;
typedef enum StatusReporting_ExtraReportingBehaviorFlags
{
	EXTRAREPORTING_PATCHING_STATUSES = 1 << 1,
} StatusReporting_ExtraReportingBehaviorFlags;


//we run our update tick very frequently until we've connected at least once to at least something, then we slow down to our normal speed,
//so that we don't have a lenghty pause before showing up to begin with
#define STATUS_REPORTING_INTERVAL_BEFORE_CONNECTION (0.1f)

#define STATUS_REPORTING_INTERVAL_NORMAL (5.0f)

//Some, but not all, servers report a StatusReportingState, which is a high-level enumerated state. Make sure to
//only add to the END of this list (I've started out grouping together the states that apply to 
//server types, but that's purely cosmetic)

AUTO_ENUM;
typedef enum StatusReporting_SelfReportedState
{
	STATUS_UNSPECIFIED,

	STATUS_CONTROLLER_STARTUP, //controller is doing startup-time communication with sentryserver, etc.
	STATUS_CONTROLLER_PATCHING, //controller is waiting for other machines to patch
	STATUS_CONTROLLER_POST_PATCH_STARTUP, //controller is doing data dir cloning and other things that happen
		//after PATCHING 
	STATUS_CONTROLLER_SERVERS_STARTING, //controller is waiting while 
	STATUS_CONTROLLER_LOCKED, //shard is locked
	STATUS_CONTROLLER_RUNNING, //shard is unlocked and running

	STATUS_SHARDLAUNCHER_STARTINGUP,
	STATUS_SHARDLAUNCHER_PATCHING,
	STATUS_SHARDLAUNCHER_LAUNCHING,
} StatusReporting_SelfReportedState;

AUTO_STRUCT;
typedef struct StatusReporting_GenericServerStatus
{
	float fFPS;
	char *pRestartingCommand; AST(ESTRING, FORMATSTRING(HTML_SKIP=1)) //a batch file that can be run which will kill and restart this
		//server if it crashes (ie, "c:\fightclub\tools\internal\run_accountserver.bat")
	int iNumAlerts[ALERTLEVEL_COUNT]; AST(FORMATSTRING(HTML_SKIP=1))

	StatusReporting_SelfReportedState eState;
	NameValuePairList *pNameValuePairs;

	//only sent if specifically required for this sender and receiver type, via latelink
	PCLStatusMonitoringUpdate **ppPCLStatuses; AST(LATEBIND)
} StatusReporting_GenericServerStatus;

AUTO_STRUCT;
typedef struct StatusReporting_Wrapper
{
	char *pMyName;
	GlobalType eMyType;
	ContainerID iMyID;
	char *pMyProduct; AST(POOL_STRING)
	char *pMyShortProduct; AST(POOL_STRING)
	int iMyMainMonitoringPort; //the main port used to monitor this server... for instance, 82 for an account server
	int iMyGenericMonitoringPort; //the port used for generic server monitoring, ie, 8081 for an account server
	const char *pMyMachineName; AST(POOL_STRING)
	char *pMyShardName;
	char *pMyClusterName;
	char *pMyOverlord; AST(POOL_STRING)
	U32 iMyPid;
	char *pMyExeName;

	const char *pVersion;

	StatusReporting_GenericServerStatus status;

} StatusReporting_Wrapper;

AUTO_STRUCT;
typedef struct StatusReporting_LogWrapper
{
	char *pMyName; 
	char *pLog; AST(ESTRING)
	U32 iTime;
} StatusReporting_LogWrapper;

void StatusReporting_SetGenericPortNum(int iPortNum);

typedef enum
{
	STATUSREPORTING_OFF,
	STATUSREPORTING_NOT_CONNECTED,
	STATUSREPORTING_CONNECTED,
} enumStatusReportingState;

enumStatusReportingState StatusReporting_GetState(void);

//call this if you need to force status reporting to update, because for instance
//you are about to shut down
void StatusReporting_ForceUpdate(void);
char *StatusReporting_GetMyName(void);
char *StatusReporting_GetControllerTrackerName(void);

//gets a comma-separated string of the all main and extra systems being reported to
char *StatusReporting_GetAllControllerTrackerNames(void);


void BeginStatusReporting(const char *pMyName, const char *pControllerTrackerName, int iMyMainPortNumber);

//set a port with a colon in the name, ie "newfcdev:8123"
void DoExtraStatusReporting(char *pToWho);

//feel free to set one or both of these bools directly, both will default to false
extern bool sbStatusReportingUsesSendAlertExeWhenNotConnected;
extern bool sbStatusReportingGeneratesMessagesBoxesForAlertsWhenNotConnected;

//whoever is our critical system (or extra monitoring server) has asked us to shut down. Most
//systems will ignore that, but if you put smething in this function, then you can do a clean
//shutdown on that request
LATELINK;
void StatusReporting_ShutdownSafely(void);

//sends a log line to anyone we're reporting status to. This does NOT buffer them up to report during a disconnection,
//so should not be used for anything that anyone depends on for anything to proceed. Do not use this for anything heavy duty.
void StatusReporting_Log(FORMAT_STR const char* format, ...);

//like the above, but can be called from any thread
void StatusReporting_LogFromBGThread(FORMAT_STR const char* format, ...);

//returns one of StatusReporting_SelfReportedState
LATELINK;
int StatusReporting_GetSelfReportedState(void);

//can be used to provide additional generically useful information
LATELINK;
NameValuePairList *StatusReporting_GetSelfReportedNamedValuePairs(void);

//when extra reporting starts up, call this to see what behaviors are needed
LATELINK;
int StatusReporting_GetBehaviorFlagsForExtraStatusReporting(char *pName, int iPort);