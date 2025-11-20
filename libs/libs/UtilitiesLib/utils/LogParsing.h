#pragma once
GCC_SYSTEM

#include "file.h"
#include "GlobalTypeEnum.h"
#include "referencesystem.h"
#include "statusReporting.h"
#include "textparser.h"
#include "autogen/LogParsing_h_ast.h"


typedef struct GameEvent GameEvent;
typedef struct ShardInfo_Perf ShardInfo_Perf;
typedef struct LogParserLongTermData LogParserLongTermData;
typedef struct InventoryBag InventoryBag;
typedef struct InventoryBagSummary InventoryBagSummary;
typedef struct IndexedPairs IndexedPairs;
typedef struct NameValuePair NameValuePair;
typedef struct FrameCountsHistReported FrameCountsHistReported;
typedef struct ControllerOverview ControllerOverview;
typedef struct PatchMsgPerfInfos PatchMsgPerfInfos;
typedef struct LogFileParser LogFileParser;
typedef struct MemTrackOpsInfo MemTrackOpsInfo;
typedef enum enumLogCategory enumLogCategory;

// This rank list is ORDERED
AUTO_ENUM;
typedef enum LogParserCritterRank
{
	kLogParserCritterRank_Henchman = 0,
	kLogParserCritterRank_Villain,
	kLogParserCritterRank_MasterVillain,
	kLogParserCritterRank_SuperVillain,
	kLogParserCritterRank_Legendary,
	kLogParserCritterRank_Cosmic,

	kLogParserCritterRank_NumRanks, EIGNORE
} LogParserCritterRank;

AUTO_ENUM;
typedef enum LogParserWorldRegionType
{
	LPWRT_None = -1,		//< only -1, because I don't know if it is
						//< safe to make this 0. -- MJF
	LPWRT_Ground,
	LPWRT_Space,
	LPWRT_SectorSpace,
	LPWRT_SystemMap,
	LPWRT_CharacterCreator,

	LPWRT_COUNT, EIGNORE
} LogParserWorldRegionType;

AUTO_STRUCT;
typedef struct KillEventParticipant
{
	const char *pRank; AST(POOL_STRING)
	bool bIsPlayer;
	int iLevel;
	char *pName; AST(ESTRING)
	char *pGroupName; AST(ESTRING)
} KillEventParticipant;

AUTO_STRUCT;
typedef struct KillEvent 
{
	Vec3 pPos;
	LogParserWorldRegionType eRegionType;
	KillEventParticipant **ppSources;
	KillEventParticipant **ppTargets;
}KillEvent;

AUTO_STRUCT;
typedef struct ItemGainedEvent 
{
	const char *pItemName;
	int iCount;
}ItemGainedEvent;

//For heatmaps
AUTO_STRUCT;
typedef struct TemplateMapSingleZoneInfo
{
	int iZoneMinX;
	int iZoneMaxX;
	int iZoneMinZ;
	int iZoneMaxZ;
	int iMapMinX;
	int iMapMaxX;
	int iMapMinY;
	int iMapMaxY;
	char *pZoneName;
} TemplateMapSingleZoneInfo;
extern ParseTable parse_TemplateMapSingleZoneInfo[];
#define TYPE_parse_TemplateMapSingleZoneInfo TemplateMapSingleZoneInfo

AUTO_STRUCT;
typedef struct TemplateMapInfo
{
	TemplateMapSingleZoneInfo **ppZones;
} TemplateMapInfo;
extern ParseTable parse_TemplateMapInfo[];
#define TYPE_parse_TemplateMapInfo TemplateMapInfo

typedef struct SubActionList
{
	char **pList;
} SubActionList;

//You can pass in a LogParsingRestrictions when you are parsing log files. This will allow you to filter things
//out as fast as possible for quick rejection
AUTO_STRUCT;
typedef struct LogParsingRestrictions
{
	U32 iStartingTime; AST(FORMATSTRING(HTML_SECS=1))
	U32 iEndingTime; AST(FORMATSTRING(HTML_SECS=1))
	char **ppMapNames;							//zone map filenames, ie "maps/Adventure_Zones/CAN/CAN.zone". If this list exists, then logs will be rejected
												//if they come from a map not in this list. This is a fairly fast rejection.
	
	//a stash table of lists. Talk to Jonathan P.
	StashTable sAction;				NO_AST
	StashTable sFileGroups;			NO_AST

	//contains the same as the stash table, not used except to be seen in the server monitor
	char **ppActions;

	char **ppObjNames;							// similar to mapnames
	char **ppOwnerNames;

	// Text-based restrictions
	char **ppSubstring;	AST(NAME(substring))							// if non-empty, then checks if the log has any substrings, using a case-insensitive scan, and discards it if it doesn't
	char **ppSubstringInverse;					// if non-empty, then checks if the log has any substrings, using a case-insensitive scan, and discards it if it does
	char **ppSubstringCaseSensitive;				// if non-empty, then checks if the log has any substrings, using a case-sensitive scan, and discards it if it doesn't
	char **ppSubstringCaseSensitiveInverse;		// if non-empty, then checks if the log has any substrings, using a case-sensitive scan, and discards it if it does
	char *pRegex;								// if non-empty, then checks if the log matches this PCRE expression, and discards it if it doesn't.
	char *pRegexInverse;						// if non-empty, then checks if the log matches this PCRE expression, and discards it if it does.

	char *pExpression;							// if non-empty, then checks if the parsed log matches this expression

	// If non-zero, the message must be related in some way to this container ID.
	U32 uObjID;			AST(FORMATSTRING(HTML_SKIP=1))
	U32 uPlayerID;		AST(FORMATSTRING(HTML_SKIP=1))
	U32 uAccountID;		AST(FORMATSTRING(HTML_SKIP=1))
} LogParsingRestrictions;

AUTO_STRUCT;
typedef struct ProjSpecificParsedLogObjInfo
{
	char *pKey; AST(ESTRING, KEY)
	char *pVal; AST(ESTRING, NAME(Val, Value))
} ProjSpecificParsedLogObjInfo;

#define PARSEDLOG_FLAG_HASLOCATION				  (1 << 0)
#define PARSEDLOG_FLAG_MESSAGEWASPARSEDINTOSTRUCT (1 << 1) //if true, then one of the structs hanging off ParsedLogObjInfo was
														   //parsed out of the message string. This means the message
														   //string can be thrown out without losing any information.
				

//special case structure for logging the non-auto-parsable lines that the controller spits out when choosing what 
//machine to launch a server on.
//These lines look like:
//100131 19:59:48 298003 Controller[1] ESC : (Tick 1047798) About to try to pick a machine to launch a server of type GameServer, category (unspecified). Machine BOS1R5MS09: CPU:91,78. Megs: 10988. LaunchWeight: 0.000000. TOTAL: 9.000000\n (many more machines) Picked machine BOS1R9MS08
AUTO_STRUCT;
typedef struct LoadBalancingSingleMachineInfo
{
	char *pMachineName; AST(KEY)
	int iCPU;
	int iCPU60;
	int iFreeMegs;
} LoadBalancingSingleMachineInfo;

AUTO_STRUCT;
typedef struct LoadBalancingInfo
{
	GlobalType eLaunchServerType;
	char *pLaunchServerCategory;
	char *pPickedMachine;
	LoadBalancingSingleMachineInfo **ppMachineInfo;
} LoadBalancingInfo;


//struct you get from parsing a logged AccessLevelCommand
AUTO_STRUCT;
typedef struct AccessLevelCommandInfo
{
	int iAccessLevel;
	char *pHowCalled; AST(ESTRING)
	char *pCmdString; AST(ESTRING)
	char *pResultString; AST(ESTRING)
} AccessLevelCommandInfo;

// Generic EArray
// The main use of this is to be able to construct categories from LPCOs, since categories only work with earrays.
typedef struct GenericArray GenericArray;
AUTO_STRUCT;
typedef struct GenericArray
{
	EARRAY_OF(GenericArray) array;
	int integer;
	float floating;
	char *string;
} GenericArray;



//NOTE NOTE NOTE if you modify any fields here such that they do not
//default to zero memory, then LogFileParser_GetEmptyObjInfo and its accompanying stuff
//need to be changed. Note that this includes adding new indexed earrays, ie, earrays of things
//with TOK_KEY
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct ParsedLogObjInfo
{
	GlobalType eObjType;
	ContainerID iObjID;
	ContainerID iownerID;
	char *pObjName; AST(ESTRING)
	Vec3 vLocation;
	char *pOwnerName; AST(ESTRING)
	const char *pAction; AST(POOL_STRING)
	// Currently only used to mark that we have forced ServerData logs to be parsed even though they would have been restricted.
	bool bForceKept;

	//the parsed name-value pairs that come from entity_CreateProjSpecificLogString
	ProjSpecificParsedLogObjInfo **ppProjSpecific;

	//based on the action, we might parse a struct out of the message
	//action == "Event"
	GameEvent *pGameEvent; AST(LATEBIND, INDEX_DEFINE)

	//action == "Event"; type == "Kills"
	KillEvent *pKillEvent; AST(LATEBIND, INDEX_DEFINE)

	//action == "Event"; type == "ItemGained"
	ItemGainedEvent *pItemGainedEvent; AST(LATEBIND, INDEX_DEFINE)

	//action == ControllerPerfLog
	ShardInfo_Perf *pShardPerf; AST(LATEBIND, INDEX_DEFINE)

	//action == LongTermData
	LogParserLongTermData *pLongTermData; AST(LATEBIND, INDEX_DEFINE)

	//action == DropSummary
	InventoryBagSummary *pInventoryBagSummary; AST(LATEBIND, INDEX_DEFINE)

	//action == EntKill
	InventoryBag *pInventoryBag; AST(LATEBIND, INDEX_DEFINE)

	//action == SurveyMission
	IndexedPairs *pSurveyMission; AST(LATEBIND, INDEX_DEFINE)

	//action == PerfLog
	FrameCountsHistReported *pClientPerf; AST(LATEBIND, INDEX_DEFINE)

	//action == ControllerOverview
	ControllerOverview *pControllerOverview; AST(LATEBIND, INDEX_DEFINE)
	
	//action == StatusReport
	StatusReporting_Wrapper *pStatusReport; AST(LATEBIND, INDEX_DEFINE)

	//action == LoadBalancing. This objInfo and struct are custom-parsed by hardwired code
	LoadBalancingInfo *pLoadBalancing; AST(INDEX_DEFINE)

	//action == PatchCmdPerf
	PatchMsgPerfInfos *pPatchMsgPerf; AST(LATEBIND, INDEX_DEFINE)

	//action == MemTrackOps
	MemTrackOpsInfo *pMemTrackOps; AST(LATEBIND, INDEX_DEFINE)

	//action == AccessLevelCommand
	AccessLevelCommandInfo *pAccessLevelCommand; 

	// Generic array
	GenericArray pGenericArray; AST(INDEX_DEFINE)
} ParsedLogObjInfo;

typedef struct PreparsedLog
{
	ParseTable *table;
	void *log;
	U32 time_stamp;
	U32 refCount;
} PreparsedLog;

typedef struct ParsedLog ParsedLog;

AUTO_STRUCT;
typedef struct LogFileGroup
{
	const char *name;
	ParsedLog **ppParsedLogs;
} LogFileGroup;


//NOTE NOTE NOTE: if you're going to do something that makes
//any field in this not default to zero'd memory, then you
//will have to change the memset in CreateLogFromString(), so
//you probably shouldn't
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct ParsedLog
{
	U32 iTime; AST(FORMATSTRING(HTML_SECS=1))
	int iLogID; //local ID assigned to this log by its originating server
	GlobalType eServerType;
	ContainerID iServerID;
	U32 iServerIP; AST(FORMATSTRING(HTML_IP=1))
	int iServerPID;
	char *pMapName; AST(ESTRING) //gameservers only
	char *pExtraMapInfo; AST(ESTRING) 
	U8 iParsedLogFlags;

	LogFileGroup* pFileGroup; AST(UNOWNED)

	ParsedLogObjInfo *pObjInfo;

	//if no struct can be found, we try to treat the message as name/value pairs
	NameValuePair **ppPairs;

	char *pMessage; AST(ESTRING)

	//this field is never read from the log, but can be filled in via a callback. For instance, when someone sets
	//restrictions on types of players to process for logs, the category is filled in with the name
	//of the restriction that the player meets
	const char *pParsingCategory; AST(POOL_STRING)

	char serverIdString[64];

	char *pRawLogLine; //the original unparsed log line from the file. Set only if
		//LOGPARSINGFLAG_COPYRAWLOGLINE is set during parsing

	// this is the pointer to the preparsed log that's pointing at the same pObjInfo internals
	// this code assumes that preparsedlogs are never actually freed, just recycled, please update
	// if that behavior changes
	PreparsedLog* pPreParsed; NO_AST

	bool bProcedural; NO_AST
} ParsedLog;


//true means process this log, false means throw it out
typedef bool LogPostProcessingCB(ParsedLog *pLog);

AUTO_ENUM;
typedef enum enumLogParsingFlags
{
	LOGPARSINGFLAG_COPYRAWLOGLINE = 1, //copy the entire unparsed line into pLog->pRawLogLine
} enumLogParsingFlags;

//returns true on success
//pLine is NULL-terminated.
//
//if returns false, pLog may be partially filled in.
//
//Note that LogFileParser is optional
bool ReadLineIntoParsedLog(ParsedLog *pLog, char *pLine, int iLineLength, LogParsingRestrictions *pRestrictions, LogPostProcessingCB *pCB, 
	enumLogParsingFlags eFlags, LogFileParser *pParser);

void ParseLogLines(ParsedLog ***pppOutLogs, char *pInBuf, LogParsingRestrictions *pRestrictions, LogPostProcessingCB *pCB, enumLogParsingFlags eFlags);

void ParseLogFile(ParsedLog ***pppOutLogs, char *pFileName, LogParsingRestrictions *pRestrictions, LogPostProcessingCB *pCB, enumLogParsingFlags eFlags);






//given "BUG.Log_2008-07-14_23-00-00.Gz", returns 2008-07-14_23-00-00 turned into secs since 2000, returns 0 
//if badly formatted
U32 GetTimeFromLogFilename(const char *pFileName);

//returns a pooled string
const char *GetKeyFromLogFilename(const char *pFileName);

//takes a string like "gameserver/OTHER_PLAYER_EVENTS", sees if it can be divided up and turned
//into a global type and logging enum type, returns true if it can, false otherwise
bool SubdivideLoggingKey(const char *pKey, GlobalType *pOutServerType, enumLogCategory *pOutCategory);

#define LOGFILEPARSER_STARTING_BUFFER_SIZE (1024 * 1024)
typedef struct LogFileParser
{
	char fileName[CRYPTIC_MAX_PATH];
	FILE *pFile;
	char *pReadHead;
	char *pCurEndOfBuffer;
	
	char *pBuffer;
	int iBufferSizeNotCountingTerminator; //starts at LOGFILEPARSER_STARTING_BUFFER_SIZE, doubles each time, but is always alloced +1

	char *pBufferRemainder;
	int iBufferRemainderSize;

	//log strings are returned in a double buffered fashion so that the estrings don't have to be
	//constantly freed/allocated
	char *pLogStringsToReturn[2];
	int iNextLogStringToReturnToggler;

	U32 iNextLogTime;
	U32 iNextLogID;
	LogParsingRestrictions *pRestrictions;
	LogPostProcessingCB *pCB;
	enumLogParsingFlags eFlags;
	U32 iTotalBytesRemaining;

	//we created a lot and tried to read into it. Then it failed to fit our criteria or something. So we stick it
	//here to avoid extra StructCreate/StructDestroy
	ParsedLog *pDeInittedLog;

	ParsedLogObjInfo *pDoInittedObjInfo;
} LogFileParser;



//---------------------stuff relating to LogFileParsers
//a log file parser is a struct that manages reading logs out of a single file. This is useful when you want to 
//read logs from multiple files interleaved properly with respect to date.
typedef struct LogFileParser LogFileParser;
LogFileParser *LogFileParser_Create(LogParsingRestrictions *pRestrictions, LogPostProcessingCB *pCB, enumLogParsingFlags eFlags);
bool LogFileParser_OpenFile(LogFileParser *pParser, const char *pFileName);

//the log string returned is owned by the log parser and SHOULD NOT BE DESTROYED!!!! (this is a change)
char *LogFileParser_GetNextLogString(LogFileParser *pParser);


ParsedLog *CreateLogFromString(char *pLogString, LogFileParser *pParser);

//returns 0 if there are no more logs
//U32 LogFileParser_GetTimeOfNextLog(LogFileParser *pParser);
#define LogFileParser_GetTimeOfNextLog(pParser) pParser->iNextLogTime;
#define LogFileParser_GetIDOfNextLog(pParser) pParser->iNextLogID;
void LogFileParser_Destroy(LogFileParser *pParser);
U32 LogFileParser_GetBytesRemaining(LogFileParser *pParser);

static __forceinline char *LogFileParser_GetFileName(LogFileParser *pParser)
{
	return pParser->fileName;
}

//because we are constantly creating and destroying ParsedLogObjInfos, we instead just keep one around
//on the logFileParser to reuse whenever possible
ParsedLogObjInfo *LogFileParser_GetEmptyObjInfoEx(LogFileParser *pParser);
void LogFileParser_RecycleObjInfoEx(LogFileParser *pParser, ParsedLogObjInfo **ppObjInfoHandle);


static __forceinline ParsedLogObjInfo *LogFileParser_GetEmptyObjInfo(LogFileParser *pParser)
{
	if (pParser) return LogFileParser_GetEmptyObjInfoEx(pParser);
	
	return StructCreate(parse_ParsedLogObjInfo);
}

static __forceinline void LogFileParser_RecycleObjInfo(LogFileParser *pParser, ParsedLogObjInfo **ppObjInfoHandle)
{
	if (pParser)
	{
		LogFileParser_RecycleObjInfoEx(pParser, ppObjInfoHandle);
	}
	else
	{
		StructDestroySafe(parse_ParsedLogObjInfo, ppObjInfoHandle);
	}

}



//approx average compression from zipping a log file. Used for progress reporting
#define ZIP_RATIO_APPROX 5

extern bool gbLogCountingMode; // Implies gbStandAlone

typedef struct LogCountingData
{
	U64 header;
	U64 data;
	U64 count;
} LogCountingData;

// File (category and server) performance profiling.
typedef struct LogFilePerf {
	char*				name;			// Filename
	PERFINFO_TYPE*		pi;				// Performance information for this file
} LogFilePerf;

StashTable sEventTypeLogCount;
StashTable sActionLogCount;

void InitServerDataStashTable();

// functions to manage a stash table with preparsed data.
#define USE_PREPARSED_LOGS 1

#if USE_PREPARSED_LOGS
void InitPreparsedStashTable();
void UpdatePreparsedStashTable();
#else

#define InitPreparsedStashTable(...)
#define UpdatePreparsedStashTable(...)
#endif

#define PERFINFO_AUTO_START_LOGPARSE(file) PERFINFO_RUN(LogParserStartFileTimer(file);)
void LogParserStartFileTimer(const char *pName);

//if this is true, then do extra time-consuming parsing of certain types of logs. Set true by standalone
//logparser.
extern bool gbLogParsing_DoExtraStandaloneParsing;

void PreParsedLogRemoveRef(ParsedLog* pLog);

U32 LogParsing_GetMostRecentLogTimeRead(void);

//note that this value does not reset over multiple runs, so is only meaningful for purposes of making deltas
U64 LogParsing_GetTotalLogsRead(void);

extern bool gbVerboseLogParsing;
extern S64 giTotalLogsScanned;
extern S64 giLogsThatPassedFiltering;