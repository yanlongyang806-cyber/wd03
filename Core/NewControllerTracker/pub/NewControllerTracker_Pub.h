#pragma once


#include "TextParserUtils.h"

AUTO_STRUCT;
typedef struct LastMinuteFileInfo
{
	char *pFileName;
	TextParserBinaryBlock *pFileData;
} LastMinuteFileInfo;

AUTO_STRUCT;
typedef struct AllLastMinuteFilesInfo
{
	LastMinuteFileInfo **ppFiles;
} AllLastMinuteFilesInfo;

//"basic" shard info is what the controller tracker sends to the MCP. It should be small and public.
//"full" shard info includes that, plus whatever else the controller has told the controller tracker about

AUTO_STRUCT;
typedef struct PortIPPair
{
	int iPort;
	U32 iIP; AST(FORMAT_IP)
} PortIPPair;

AUTO_STRUCT;
typedef struct PortIPPairList
{
	PortIPPair **ppPortIPPairs;
} PortIPPairList;

AUTO_STRUCT;
typedef struct ShardInfo_Basic
{
	char *pMonitoringLink; AST(ESTRING,FORMATSTRING(HTML=1))
	const char *pShardCategoryName; AST(POOL_STRING)
	const char *pProductName; AST(POOL_STRING)

	char *pShardName;
	char *pClusterName;

	//This is somewhat obsolete as there are now multiple login servers, but they are continuously updated seperately,
	//so it's worth leaving this for backward compatibility
	char *pShardLoginServerAddress; //for clients to log in


	char *pShardControllerAddress; //for servermonitoring

	char *pVersionString; //GetUsefulVersionString() called on the controller

	char *pPatchCommandLine;
	char *pAutoClientCommandLine;

	//if the shard is set up for prepatching. This is set on the controller tracker via ther server
	//monitor interface, should never be set on the shard itself
	char *pPrePatchCommandLine; 

	int iUniqueID; 

	bool bHasLocalMontiringMCP; AST(FORMATSTRING(HTML_SKIP=1))

	//obsolete, but still supported forward/backward compatible
	U32 *allLoginServerIPs; //earray
	//the new hotness
	PortIPPairList *pLoginServerPortsAndIPs;

	//set to true for permanent shards. So if the permanent shard gets sent to a MCP/crypticLauncher, this will be true, and
	//the MCP will know that the shard is probably not currently active
	bool bNotReallyThere;

	char *pAccountServer;
} ShardInfo_Basic;

AUTO_STRUCT;
typedef struct ShardInfo_Perf
{
	int iPlayers;
	int iEntities;
	int iGameServers;
	int iAlerts; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 10 ; divWarning2 ; $ > 0 ; divWarning1"))
	int iMachines;
	int iLoggingIn;
	int iNumNotResponding; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 5 ; divWarning2 ; $ > 0 ; divWarning1"))
	int iNumCrashed; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 5 ; divWarning2 ; $ > 0 ; divWarning1"))
	int iAvgCPU60; //average cpu usage over past 60 seconds on all machines in shard 
		AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 95 ; divWarning2 ; $ > 80 ; divWarning1"))

	int iMaxCPU60; //highest average cpu usage over past 60 seconds on any machine in shard
		AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 95 ; divWarning2 ; $ > 80 ; divWarning1"))

		
	U64 iMinVirtAvail; AST(FORMATSTRING(HTML_BYTES = 1, HTML_CLASS_IFEXPR = "$ < 5000000 ; divWarning2 ; $ < 50000000 ; divWarning1"))

	U64 iMinPhysAvail; AST(FORMATSTRING(HTML_BYTES = 1, HTML_CLASS_IFEXPR = "$ < 5000000 ; divWarning2 ; $ < 50000000 ; divWarning1"))

	U64 iAvgPhysAvail; AST(FORMATSTRING(HTML_BYTES = 1, HTML_CLASS_IFEXPR = "$ < 5000000 ; divWarning2 ; $ < 50000000 ; divWarning1"))
	
	int dbUpdPerSec; 
	
	U32 iCreationTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))

	//filled out by controller tracker, not controller
	U32 iLastUpdateTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))

	int iNumDiedAtStartup; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 5 ; divWarning2 ; $ > 0 ; divWarning1"))
	int iMaxCrashes; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 10 ; divWarning2 ; $ > 5 ; divWarning1"))
	int iRunningSlow; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "$ > 5 ; divWarning2 ; $ > 0 ; divWarning1"))
	int iLongestStall; AST(FORMATSTRING(HTML_SECS_DURATION_SHORT = 1 , HTML_CLASS_IFEXPR = "$ > 240 ; divWarning2 ; $ > 30 ; divWarning1"))

} ShardInfo_Perf;

typedef struct ShardConnectionUserData ShardConnectionUserData;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "basicInfo.MonitoringLink, basicInfo.productName, basicInfo.ShardName, basicInfo.ClusterName, basicInfo.ShardCategoryName, basicInfo.VersionString, Players, Entities, GameServers, Alerts, Machines, LoggingIn, NumNotResponding, NumCrashed, NumDiedAtStartup, AvgCPU60, MaxCPU60, MinVirtAvail, MinPhysAvail, AvgPhysAvail, dbUpdPerSec, CreationTime, LastUpdateTime, MaxCrashes, RunningSlow, LongestStall, SetPrepatchVersion, MakePermanent");
typedef struct ShardInfo_Full
{
	ShardInfo_Basic basicInfo; 
	ShardInfo_Perf perfInfo; AST(EMBEDDED_FLAT)

	AllLastMinuteFilesInfo *pAllLastMinuteFiles;

	//used for internal linking
	ShardConnectionUserData *pParent; NO_AST

	AST_COMMAND("SetPrepatchVersion", "SetPrePatchVersion $FIELD(basicInfo.ShardName) $STRING(Version name) $CONFIRM(Set version for prepatching? Current: $FIELD(basicInfo.PrePatchCommandLine))")
	AST_COMMAND("MakePermanent", "MakeShardPermanent $FIELD(basicInfo.ShardName) $NORETURN")
} ShardInfo_Full;


AUTO_STRUCT;
typedef struct ShardInfo_Basic_List
{
	ShardInfo_Basic **ppShards;
	char *pMessage;//an optional message like "Permissions invalid, no shard list for you!"
	char *pUserMessage;//an optional message similar to pMessage but to be shown to end users
} ShardInfo_Basic_List;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "basicInfo.MonitoringLink, basicInfo.productName, basicInfo.ShardName, basicInfo.ClusterName, basicInfo.ShardCategoryName, basicInfo.VersionString, basicInfo.PatchCommandLine");
typedef struct ShardInfo_Perm
{
	char *pName; AST(KEY)
	ShardInfo_Basic basicInfo;
	AST_COMMAND("Remove", "RemovePermanentShard $FIELD(Name) $NORETURN")

} ShardInfo_Perm;


//struct sent by SetShardVersionOnCT.exe to CT
AUTO_STRUCT;
typedef struct ShardVersionInfoFromStandaloneUtil
{
	char *pShardName;
	char *pVersionString;
	char *pPatchCommandLine;
} ShardVersionInfoFromStandaloneUtil;