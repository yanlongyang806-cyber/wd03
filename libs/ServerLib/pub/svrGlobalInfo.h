#pragma once

// This file contains the various server-specific global info files.
// These structures are for communicating data about a server to the controller/server monitor

// Contains status info for a running ObjectDB, in addition to standard information

#include "GlobalEnums.h"
#include "GlobalTypeEnum.h"

typedef struct LogClusterServerStatus LogClusterServerStatus;

typedef struct MapPartitionSummary MapPartitionSummary;

// System performance structure
AUTO_STRUCT;
typedef struct TrackedPerformance 
{
	int iNumProcessors;
	bool bHyperThreading;

	long bytesSent; AST(FORMATSTRING(HTML_BYTES = 1))
	long bytesRead; AST(FORMATSTRING(HTML_BYTES = 1))
	long cpuUsage; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 75 ; divWarning2 ; $ > 10 ; divWarning1"))
	int cpuLast60; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 75 ; divWarning2 ; $ > 10 ; divWarning1"))

	long cpuUsage_Raw;
	int cpuLast60_Raw;

	U64 iTotalRAM; AST(FORMATSTRING(HTML_BYTES = 1))
	U64 iFreeRAM; AST(FORMATSTRING(HTML_BYTES = 1))
	U64 TotalPageFile; AST(FORMATSTRING(HTML_BYTES = 1))
	U64 iAvailPageFile; AST(FORMATSTRING(HTML_BYTES = 1))
	U64 iAvailVirtual; AST(FORMATSTRING(HTML_BYTES = 1))

	//calculated on the Controller every time this is received from the launcher
	int iNumPlayers;
	int iWeighted_GS_FPS;
} TrackedPerformance;

AUTO_STRUCT;
typedef struct DatabaseGlobalInfo
{
	int iTotalPlayers;
	int iActivePlayers;
	int iDeletedPlayers;
	int iOfflinePlayers;


	//F32 fMeanServiceTime;
	//U32 iPendingWrites;

	int iDBThreadQueue;
	F32 iDBThreadQueueLatencyMSecs; // This reflects time between being added to the GWTQueue and finishing processing
	U64 iDBThreadQueueOperationsPerSecond;

	int iUpdateBytesMax;
	int iBytesPerUpdate;
	int iUpdatesPerSec;
	int iTransfersPerSec;

	bool bHogsEnabled; AST(FORMATSTRING(HTML=1))

	U32 iLastRotateTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))
	U32 iSnapshotTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))

	U64 iLastReplicatedSequenceNumber;
	U32 iLastReplicatedTimeStamp; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))

	U32 iTotalSubscriptions;
	F32 fSubscriptionsPerPlayer;
	U32 iUnpackedPlayers;
	U32 iOnlinedPlayers;
	
	AST_COMMAND("Download dbUpdateReport", "Redirect $REDIRECT(/file/objectdb/$FIELD(ID)/filesystem/dbUpdateReport.txt)")
	AST_COMMAND("Create Snapshot", "DbCreateSnapshot $FIELD(ID) 0 0 0 $CONFIRM(Really create snapshot?) $NORETURN")
	AST_COMMAND("Snapshot+Webdump", "DbCreateSnapshot $FIELD(ID) 0 0 $STRING(File to save to) $CONFIRM(Run snapshot merger with webdata dump?) $NORETURN")
	//AST_COMMAND("View All Loggedin Players", "Redirect $REDIRECT(/viewxpath?xpath=ObjectDB[$FIELD(ID)].query&svrQuery=SELECT+.Mycontainerid,.pplayer.privateaccountname,.Debugname,.ContainerState+WHERE+not+(.ContainerState+%3D+OWNED))")
} DatabaseGlobalInfo;

//a structure containing generic "status info" about a running gameserver. Gameservers assemble this
//every few seconds and send it up to the MapManager and Controller
AUTO_STRUCT;
typedef struct GameServerGlobalInfo
{
	float fFPS_gsl; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ < 15 ; divWarning2 ; $ < 20 ; divWarning1"))
	ContainerID iVirtualShardID;

	int iNumPartitions;
	U32 iNumPartitionsSinceServerStart;
	U32 iMaxPartitionsSinceServerStart;
	char *pIndices; AST(ESTRING) //string containing the instance indices of all partitions

	U32 iLastTimeWithPlayers; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1 , HTML_CLASS_IFEXPR = "$ > 30 ; divWarning2 ; $ > 10 ; divWarning1"))

	int iNumPlayers;
	int iNumEntities;
	int iNumActiveEnts;
	int iNumCombatEnts;
	int iNumProjectileEnts;
	int iDifficulty; //this is only valid if open instancing is enabled
	char mapName[256];
	char mapNameShort[64];
	bool bMapNameHasChanged; AST(FORMATSTRING(HTML_SKIP=1))
	bool bEnableOpenInstancing;
	ZoneMapType eMapType;
	int iSlowTransCount;
	int iLaggyFrameCount;

	bool bSentNeverStartWarning;

	//meaningful only on cont
	U32 iLastContact; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1 , HTML_CLASS_IFEXPR = "$ > 30 ; divWarning2 ; $ > 10 ; divWarning1"))
	U32 iLastHandhakingWithMapManagerTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))
	bool bReceivedGSLGlobalInfo; //set to true in HereIsGSLGlobalInfo only, so if this is true, then iLastContact will
		//always be non-zero

	bool bIsEditingServer;

	MapPartitionSummary **ppPartitions; //note that while gsglobalinfo is being assembled on the gameserver, this array is created unowned
		//to avoid meaningless duplication

} GameServerGlobalInfo;


//a structure containing generic "status info" about a running GatewayServer. GatewayServers assemble this
//every few seconds and send it up to the Controller
AUTO_STRUCT;
typedef struct GatewayServerGlobalInfo
{
	int iNumSessions;
	U64 iHeapTotal;
	U64 iHeapUsed;
	U64 iWorkingSet;
} GatewayServerGlobalInfo;

// Data about the transaction server

AUTO_STRUCT;
typedef struct TransactionServerGlobalInfo
{
	int iNumActive;
	U64 iNumSucceeded;
	U64 iNumOtherCompleted;
	int iTransPerSec;
	int iTransPerSecHrAvg;
	int iBytesPerTrans;
//	float fBytesPerTransHrAvg;
	int iMsecsLatency;
//	float fMsecsLatencyHrAvg;
//	float fBlocksPerSec;
//	float fBlocksPerSecHrAvg;
} TransactionServerGlobalInfo;


//Data about the LoginServer
AUTO_STRUCT;
typedef struct LoginServerGlobalInfo
{
	int iNumLoggingIn;
	bool bUGCEnabled;
	U32 iUGCVirtualShardID;
	int iPort;
	int iNumQueued_Main;
	int iNumQueued_VIP;

	int iNumWhoWentThroughQueueWhoAreStillOnLoginServer; NO_AST
} LoginServerGlobalInfo;

AUTO_STRUCT;
typedef struct JobManagerGlobalInfo
{
	int iActiveAndQueuedPublishes;
	int iActiveAndQueuedRepublishes;

	int iAveragePublishTime; AST(FORMATSTRING(HTML_SECS_DURATION=1))
	int iAverageQueuedTime; AST(FORMATSTRING(HTML_SECS_DURATION=1))

	int iNumSuccededPublishes;
	int iNumFailedPublishes;

	int iNumSuccededPublishesLastHour;
	int iNumFailedPublishesLastHour;
} JobManagerGlobalInfo;


AUTO_STRUCT;
typedef struct LogServerCategoryStatus
{
	char *pName;
	U64 iNumLogsProcessed;
	U64 iBytesProcessed; AST(FORMATSTRING(HTML_BYTES=1))
	int iCurLogs;
	U64 iCurBytes;
	bool bActive;
	AST_COMMAND("Toggle", "ToggleCategory $FIELD(Name)")
} LogServerCategoryStatus;

AUTO_STRUCT;
typedef struct LogClusterServerStatus
{
	char *pClusterLogServerName;
	bool bCurrentlyConnected;
	U32 iLastConnectionTime; AST(FORMATSTRING(HTML_SECS_AGO=1))
}  LogClusterServerStatus;



AUTO_STRUCT;
typedef struct LogServerGlobalInfo
{
	LogClusterServerStatus *pClusterServerStatus; 

	U64 iNumLogsProcessed;
	U64 iBytesProcessed; AST(FORMATSTRING(HTML_BYTES=1))

	U64 iLogsPerSecond;
	U64 iBytesPerSecond; AST(FORMATSTRING(HTML_BYTES=1))

	char *pBackgroundQueue; AST(ESTRING)
	
	char *pStallsPer10SecondPeriod; AST(ESTRING)
	
	LogServerCategoryStatus **ppCategories;

	char *pGenericInfo; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1))
} LogServerGlobalInfo;
// Information about launchers

//if you add new floats to this, make sure to do an IS_FINITE check before sending them
AUTO_STRUCT;
typedef struct ProcessPerformanceInfo
{
	U64 physicalMemUsed; AST(FORMATSTRING(HTML_BYTES = 1))
	U64 physicalMemUsedMax;  AST(FORMATSTRING(HTML_BYTES = 1))
	float fCPUUsage; AST(FORMATSTRING(HTML_FLOAT_PREC = 2))
	float fCPUUsageLastMinute; AST(FORMATSTRING(HTML_FLOAT_PREC = 2))

	float fCPUUsage_raw; AST(FORMATSTRING(HTML_FLOAT_PREC = 2))
	float fCPUUsageLastMinute_raw; AST(FORMATSTRING(HTML_FLOAT_PREC = 2))

	float fFPS;
	U32 iLongestFrameMsecs;
	U32 iLastContactTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
} ProcessPerformanceInfo;
//if you add new floats to this, make sure to do an IS_FINITE check before sending them

AUTO_STRUCT;
typedef struct LauncherProcessInfo
{
	GlobalType eType;
	ContainerID ID;
	int iLowLevelControllerIndex;
	char stateString[256];
	U32 PID;
	ProcessPerformanceInfo perfInfo;
} LauncherProcessInfo;

AUTO_STRUCT;
typedef struct LauncherGlobalInfo
{
	//the last timeSecondsSince2000() that we received from the controller, so the 
	//controller can generate alerts if it sucks, and also adjust server start timing accordingly
	U32 iLastTimeReceivedFromController;

	//on each machine there can be up to one "ignored process", which is left there so it can be debugged,
	//currently only used for stalled-but-not-crashed gameservers
	int iPIDOfIgnoredServer;	
	
	LauncherProcessInfo **ppProcesses;
} LauncherGlobalInfo;

//this is information that is optionally presented to the controller when
//it is launching a server, providing various extra pieces of random
//information
AUTO_STRUCT;
typedef struct AdditionalServerLaunchInfo
{
	int iGameServerLaunchWeight;
	U8 zMapType; //really ZoneMapType
} AdditionalServerLaunchInfo;
