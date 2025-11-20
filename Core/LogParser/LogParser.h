#pragma once
#include "StashTable.h"
#include "LogParsing.h"
#include "MapDescription.h"

typedef struct ParsedLog ParsedLog;
typedef struct MultiVal MultiVal;
typedef struct LPCOStringGroupInput LPCOStringGroupInput;
typedef struct LPCODataGroupInput LPCODataGroupInput;
typedef struct LPCOFSMDef LPCOFSMDef;
typedef struct LPCOSimpleDataInput LPCOSimpleDataInput;
typedef struct LPCOSimpleListInput LPCOSimpleListInput;
typedef struct LPCOTrackedValueDef LPCOTrackedValueDef;
typedef struct ResourceDictionaryInfo ResourceDictionaryInfo;
typedef struct LPCOSimpleDataWildCardInput LPCOSimpleDataWildCardInput;
typedef struct LPCOProceduralSimpleDataInput LPCOProceduralSimpleDataInput;
typedef struct LPCOTypeTimedBroadcastInput LPCOTypeTimedBroadcastInput;
typedef struct LPTimedAggregator LPTimedAggregator;

extern ResourceDictionaryInfo *gpPowerDictResInfo;

//names for basic filter categories. Filled in with pooled strings at autorun time
extern const char *BASECATEGORY_UNKNOWN;
extern const char *BASECATEGORY_OTHER;

extern bool gbStandAloneForceExit;
extern bool gbStandAloneLogParserNeverTimeOut;

const char *LogParserLocalDataDir();

void RunWebFilter(U32 playerid, U32 accountid, int startTime, int endTime, const char *categories, bool bAllowDownload, bool bCompressDownload);

AUTO_STRUCT;
typedef struct LogParserPlayerLogFilterPowerToRequire
{
	const char *pParentName; AST(POOL_STRING FORMATSTRING(HTML_SKIP=1))
	char *pPowerName;
	AST_COMMAND("Remove", "PlayerLog_RemovePowerRestriction \"$FIELD(ParentName)\" $FIELD(PowerName) $CONFIRM(Remove this power restriction?) $NORETURN")
} LogParserPlayerLogFilterPowerToRequire;

AUTO_STRUCT;
typedef struct LogParserPlayerLogFilterPlayerToRequire
{
	const char *pParentName; AST(POOL_STRING FORMATSTRING(HTML_SKIP=1))
	char *pPlayerName;
	AST_COMMAND("Remove", "PlayerLog_RemovePlayerRestriction \"$FIELD(ParentName)\" $FIELD(PlayerName) $CONFIRM(Remove this player restriction?) $NORETURN")
} LogParserPlayerLogFilterPlayerToRequire;

AUTO_STRUCT;
typedef struct LogParserPlayerLogFilterMapNameToRequire
{
	const char *pParentName; AST(POOL_STRING FORMATSTRING(HTML_SKIP=1))
	char *pMapName;
	AST_COMMAND("Remove", "PlayerLog_RemoveMapNameRestriction \"$FIELD(ParentName)\" $FIELD(MapName) $CONFIRM(Remove this MapName restriction?) $NORETURN")
} LogParserPlayerLogFilterMapNameToRequire;


//a filter that can be applied to logs about players
AUTO_STRUCT;
typedef struct LogParserPlayerLogFilter
{
	const char *pFilterName; AST(KEY POOL_STRING)
	int iMinTeamSize;
	int iMaxTeamSize;
	int iMinLevel;
	int iMaxLevel;
	int iMinAccessLevel;
	int iMaxAccessLevel;
	char **ppRoleNames; AST(POOL_STRING) 
		//if empty, accept all players, otherwise only accept players whose 
		//role matches one of these

	LogParserPlayerLogFilterPlayerToRequire **ppPlayersToRequire;//match any
	LogParserPlayerLogFilterMapNameToRequire **ppMapNamesToRequire;//match any
	LogParserPlayerLogFilterPowerToRequire **ppPowersToRequire;//match all

	AST_COMMAND("Remove", "RemovePlayerFilter \"$FIELD(FilterName)\" $CONFIRM(Remove this player filter?) $NORETURN")
	AST_COMMAND("Add Power Restriction", "PlayerLog_AddPowerRestriction  \"$FIELD(FilterName)\" $SELECT(Which power|NAMELIST_PowersNameList) $CONFIRM(Limit this category to players with a particular power) $NORETURN")
	AST_COMMAND("Add Player Name Restriction", "PlayerLog_AddPlayerRestriction \"$FIELD(FilterName)\" $STRING(Enter player name. Ie, superDude@johnSmith) $NORETURN")
	AST_COMMAND("Add Map Name Restriction", "PlayerLog_AddMapNameRestriction \"$FIELD(FilterName)\" $SELECT(Search only on certain maps|NAMELIST_MapNameList) $NORETURN")
} LogParserPlayerLogFilter;

AUTO_STRUCT;
typedef struct LogParserGameServerFilterMapName
{
	const char *pParentName; AST(POOL_STRING FORMATSTRING(HTML_SKIP=1))
	char *pMapName;
	AST_COMMAND("Remove", "GameServerLog_RemoveMapNameRestriction \"$FIELD(ParentName)\" $FIELD(MapName) $CONFIRM(Remove this MapName restriction?) $NORETURN")
} LogParserGameServerFilterMapName;

AUTO_STRUCT;
typedef struct LogParserGameServerFilterMachineName
{
	const char *pParentName; AST(POOL_STRING FORMATSTRING(HTML_SKIP=1))
	char *pMachineName;
	AST_COMMAND("Remove", "GameServerLog_RemoveMachineNameRestriction \"$FIELD(ParentName)\" $FIELD(MachineName) $CONFIRM(Remove this MachineName restriction?) $NORETURN")
} LogParserGameServerFilterMachineName;


//a filter that can be applied to gameserver LPCOs
AUTO_STRUCT;
typedef struct LogParserGameServerFilter
{
	const char *pFilterName; AST(KEY POOL_STRING)
	LogParserGameServerFilterMapName **ppMapNames; //match any
	LogParserGameServerFilterMachineName **ppMachineNames; //match any

	ZoneMapType eMapType; //if unspecified, match any

	int iMinPlayers;
	int iMaxPlayers;
	int iMinEntities;
	int iMaxEntities;

	AST_COMMAND("Remove", "RemoveGameServerFilter \"$FIELD(FilterName)\" $CONFIRM(Remove this GameServer filter?) $NORETURN")
	AST_COMMAND("Add Map Name Restriction", "GameServerLog_AddMapNameRestriction \"$FIELD(FilterName)\" $SELECT(Search only on certain maps|NAMELIST_MapNameList) $NORETURN")
	AST_COMMAND("Add Machine Name Restriction", "GameServerLog_AddMachineNameRestriction \"$FIELD(FilterName)\" $STRING(Machine Name To Match) $NORETURN")
	AST_COMMAND("Set Map Type Restriction", "GameServerLog_SetMapTypeRestriction \"$FIELD(FilterName)\" $SELECT(Match only one map type|ENUM_ZoneMapType) $NORETURN")
	AST_COMMAND("Set Num Players Restriction", "GameServerLog_SetPlayerNumRestriction \"$FIELD(FilterName)\" $INT(Min Players) $INT(Max Players) $NORETURN")
	AST_COMMAND("Set Num Entities Restriction", "GameServerLog_SetEntityNumRestriction \"$FIELD(FilterName)\" $INT(Min Entities) $INT(Max Entities) $NORETURN")

} LogParserGameServerFilter;


//
//the options that are set on a per-user basis when someone is in standalone mode. Saved when changed, loaded
//at standalone startup.
AUTO_STRUCT;
typedef struct LogParserStandAloneOptions
{
	LogParsingRestrictions parsingRestrictions; AST(ADDNAMES(timeRestrictions, timeAndMapRestrictions))

	bool bTimeWasSetInEasyMode; //time was set to something like "last week", so it should be
		//fixed up to still be "last week" when loaded/saved

	char *pFilenameRestrictions; AST(ESTRING) //comma-separated list
	char **ppDirectoriesToScan;
	char **ppActiveGraphNames; AST(FORMATSTRING(HTML_SKIP=1))
	LogParserPlayerLogFilter **ppPlayerFilterCategories; AST(ADDNAMES(FilterCategories))
	LogParserPlayerLogFilter **ppExclusionPlayerFilters; AST(ADDNAMES(ExclusionFilters))

	LogParserGameServerFilter **ppGameServerFilterCategories;
	LogParserGameServerFilter **ppExclusionGameServerFilters;

	bool bIncludeOtherLogs_bool; AST(FORMATSTRING(HTML_SKIP=1))
	char *pIncludeOtherLogs; AST(ESTRING, FORMATSTRING(HTML=1))

	bool bCreateFilteredLogFile; AST(FORMATSTRING(EDITABLE=1))
	bool bCreateBinnedLogFiles; AST(FORMATSTRING(EDITABLE=1))
	bool bCompressFilteredLogFile; AST(FORMATSTRING(EDITABLE=1))

	bool bFilteredFileIncludesProceduralLogs; AST(FORMATSTRING(EDITABLE=1))

	char *pBinnedLogFileDirectory; AST(ESTRING)
	bool bWebFilteredScan; AST(FORMATSTRING(HTML_SKIP=1))	// True if this is a special scan started by RunFilteredScan().

	char *pDownloadLink; AST(ESTRING, FORMATSTRING(HTML=1))

	char *pSetDatesToSearch_Easy; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetDatesToSearch_LocalGimmeTime; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetDatesToSearch_SecsSince2000; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetDatesToSearch_UtcLogDate; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetFilenamesToMatch; AST(ESTRING, FORMATSTRING(command=1))

	char *pAddMapRestriction; AST(ESTRING, FORMATSTRING(command=1))
	char *pClearMapRestrictions; AST(ESTRING, FORMATSTRING(command=1))
	char *pAddActionRestriction; AST(ESTRING, FORMATSTRING(command=1))
	char *pClearActionRestrictions; AST(ESTRING, FORMATSTRING(command=1))
	char *pAddObjectRestriction; AST(ESTRING, FORMATSTRING(command=1))
	char *pClearObjectRestrictions; AST(ESTRING, FORMATSTRING(command=1))
	char *pAddOwnerRestriction; AST(ESTRING, FORMATSTRING(command=1))
	char *pClearOwnerRestrictions; AST(ESTRING, FORMATSTRING(command=1))
	char *pAddPlayerRestriction; AST(ESTRING, FORMATSTRING(command=1))
	char *pClearPlayerRestrictions; AST(ESTRING, FORMATSTRING(command=1))

	char *pSetExpressionRestriction; AST(ESTRING, FORMATSTRING(command=1))

	char *pSetSubstringRestriction; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetSubstringInverseRestriction; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetSubstringCaseSensitiveRestriction; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetSubstringCaseSensitiveInverseRestriction; AST(ESTRING, FORMATSTRING(command=1))

	char *pSetRegexRestriction; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetRegexInverseRestriction; AST(ESTRING, FORMATSTRING(command=1))

	char *pSetDirectoriesToScan_Easy; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetDirectoriesToScan_Precise; AST(ESTRING, FORMATSTRING(command=1))
	char *pAddNewPlayerFilterCategory; AST(ESTRING, FORMATSTRING(command=1))
	char *pAddPlayerExclusionFilter; AST(ESTRING, FORMATSTRING(command=1))
	char *pAddNewGameServerFilterCategory; AST(ESTRING, FORMATSTRING(command=1))
	char *pAddNewGameServerExclusionFilter; AST(ESTRING, FORMATSTRING(command=1))
	char *pBeginCreatingFilteredFile; AST(ESTRING, FORMATSTRING(command=1))
	char *pStopCreatingFilteredFile; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetFilteredFileIncludesProcedural; AST(ESTRING, FORMATSTRING(command=1))
	char *pBeginCreatingBinnedFile; AST(ESTRING, FORMATSTRING(command=1))
	char *pStopCreatingBinnedFile; AST(ESTRING, FORMATSTRING(command=1))
	char *pCompressFilteredFile; AST(ESTRING, FORMATSTRING(command=1))
	char *pDontCompressFilteredFile; AST(ESTRING, FORMATSTRING(command=1))
	char *pSetBinnedLogFileDirectory; AST(ESTRING, FORMATSTRING(command=1))




} LogParserStandAloneOptions;

//the struct that is used by the FrontEnd to request the running of a standalone logparser in frontend mode
AUTO_STRUCT;
typedef struct FrontEndFilterRequest
{
	int iUID;
	char *pDirectory;
	int iSecondsDelay;

	LogParserStandAloneOptions *pOptions;

} FrontEndFilterRequest;



//defines a common place to find log files
AUTO_STRUCT;
typedef struct LogCommonSearchLocation
{
	char *pName;
	char *pDir;
} LogCommonSearchLocation;

//the stringified versions of these are passed in to SetSearchTimes
AUTO_ENUM;
typedef enum enumSearchTimeType
{
	SEARCHTIME_ALWAYS,
	SEARCHTIME_LASTMINUTE,
	SEARCHTIME_LAST15MINUTES,
	SEARCHTIME_LASTHOUR,
	SEARCHTIME_LAST6HOURS,
	SEARCHTIME_LAST12HOURS,
	SEARCHTIME_LASTDAY,
	SEARCHTIME_LAST2DAYS,
	SEARCHTIME_LAST3DAYS,
	SEARCHTIME_LASTWEEK,
	SEARCHTIME_LAST2WEEKS,
	SEARCHTIME_LASTMONTH,
	SEARCHTIME_LAST3MONTHS,
	SEARCHTIME_LAST6MONTHS,	
	SEARCHTIME_LASTYEAR,

	SEARCHTIME_LAST, EIGNORE
} enumSearchTimeType;


AUTO_ENUM;
typedef enum 
{
	GRAPHDISPLAYTYPE_BARGRAPH,
	GRAPHDISPLAYTYPE_LINES,
	GRAPHDISPLAYTYPE_POINTS,
	GRAPHDISPLAYTYPE_LINESWITHPOINTS,
} enumGraphDisplayType;

AUTO_ENUM;
typedef enum
{
	GRAPHDATATYPE_SUM,
	GRAPHDATATYPE_COUNT,
	GRAPHDATATYPE_AVERAGE,
	GRAPHDATATYPE_MIN_MAX_AVG,
	GRAPHDATATYPE_MEDIANS, //displays the 10th, 50th, and 90th percentiles
} enumGraphDataType;


AUTO_STRUCT;
typedef struct UseThisLogCheckEquality
{
	char *pXPath; AST(STRUCTPARAM)
	char *pComparee; AST(STRUCTPARAM)
} UseThisLogCheckEquality;

//a generic struct which defines expressions which can be used to check whether to use
//a given log for some kind of graph or data collection
AUTO_STRUCT;
typedef struct UseThisLogCheckingStruct
{
	char *pUseThisLogCheck_Expression; AST(POOL_STRING)//if this string exists, it defines an expression which must be 
		//fulfilled before the log will be added to this bar graph
		//use "me" as the name of the log in the expression
		//(for instance, "me.objInfo.GameEvent.type = 0"

	//instead of using an expression, you can do a somewhat kludgy thing where you attempt to take an
	//xpath into the log, evaluate it as a string, and compare it to another string. This does two things the
	//expression system can't currently do: (1) it just fails instead of crashing if you try to xpath into
	//a non-existent substruct, and (2) it allows comparison of enums
	//
	//These are ANDed together.
	UseThisLogCheckEquality **ppUseThisLog_EqualityChecks; 


	char *pEArrayContainingDataPoints; //if this is non-empty, then the struct we want to parse
		//contains, somewhere in its xpath, an earray of objects, and we actually want to log
		//each of those objects separately. If this is true, then all other xpaths beginning with @ are relative
		//to that earray, others are relative to the entire object


} UseThisLogCheckingStruct;

// This field may be changed without causing the graph to be marked as invalid.
#define TOK_LOGPARSER_MUTABLE TOK_USEROPTIONBIT_1

// Defines the parameters for creating a graph
// Important: When adding new fields, make sure to add TOK_LOGPARSER_MUTABLE if appropriate.  Be aware that if you
// ever change a field that is not TOK_LOGPARSER_MUTABLE, it will destroy all history for existing graphs that use
// that field.
AUTO_STRUCT;
typedef struct GraphDefinition
{
	enumGraphDisplayType eDisplayType;
	enumGraphDataType eDataType;
	char *pGraphName;
	char *pGraphTitle;						AST(USERFLAG(TOK_LOGPARSER_MUTABLE))

	char *pFileName;						AST(CURRENTFILE)

	// Extra HTML to include at the bottom of the graph
	char *pGraphExtra;						AST(USERFLAG(TOK_LOGPARSER_MUTABLE))

	// Log scale X and Y axes respectively
	bool bLogScaleX;						AST(USERFLAG(TOK_LOGPARSER_MUTABLE))
	bool bLogScaleY;						AST(USERFLAG(TOK_LOGPARSER_MUTABLE))

	//can be a comma-separated list of action names, which is useful for things like
	//"Event,EventSource"
	const char *pActionName;				AST(POOL_STRING) 

	//can be a comma-separated list of sub-action names, which is useful for things like
	//"Kills,Damage"
	const char *pSubActionName;				AST(POOL_STRING) 

	UseThisLogCheckingStruct useThisLog;	AST(EMBEDDED_FLAT)

	char *pCountField; //what field this graph is actually counting, 

	char *pBarField; //what field divides the graph into separate bars (or determines the horizontal 
		//spacing of a line graph)
		//there are several "magic" names that can go in here, such as "HOUR_OF_DAY", which 
		//will use .time, but convert into hms, then fields name "midnight - 1 a.m.", "1 a.m. - 2 a.m.", etc.
		//and TIME_ROUNDED(x which rounds off to the nearest x seconds

	int iBarRounding; //if true, then the value gotten out of barField is presumed to be an integer. Round it off
		//to the nearest multiple of this before using it

	char *pCategoryField; //what field divides the graph into separate categories
	char *pCategoryNames; //a comma separated list of names for the (presumably) ints in the category field
	
	char *pCategoryCommand; //if this exists, then after the category field is read in, this cmdparse command
		//will be called with the read-in category field as its only argument, then the return value from 
		//that will be used as the new category. For instance, the client latency graph looks like this:
		//	CategoryField .objInfo.ClientPerf.ip
		//	CategoryCommand IPToCountryName

	//if non-zero, then the graph will collect data for this many seconds, then clear and start over
	//(keeping a copy of the last "Full" graph around)	
	int iGraphLifespan;						AST(USERFLAG(TOK_LOGPARSER_MUTABLE))

	//you can set these instead just for clarity, they are converted into seconds and put into 
	//iGraphLifespan at load time
	int iGraphLifespan_Minutes;				AST(USERFLAG(TOK_LOGPARSER_MUTABLE))
	int iGraphLifespan_Hours;				AST(USERFLAG(TOK_LOGPARSER_MUTABLE))
	int iGraphLifespan_Days;				AST(USERFLAG(TOK_LOGPARSER_MUTABLE))

	//if true, and if graphLifeSpan is set, then three additional graphs will be created. Each
	//will be a line graph. Each will get one data point each lifespan of the current graph. One will get the max
	//value, one the min value, and one the average value.
	bool bLongTermGraph;					AST(USERFLAG(TOK_LOGPARSER_MUTABLE))

	//if true, the graph will ignore all inputs past the first for a single data point.
	//(This is useful for the critical systems FPS graphs where we get one input when it goes to zero, one when it
	//comes out of zero, and then one every 5 seconds in between. This keeps the graph sides nice and vertical.)
	bool bOneValuePerDataPoint;				AST(USERFLAG(TOK_LOGPARSER_MUTABLE))

	//if set, and using magic bar fields, fill in any previous missed
	//data points.
	bool bFillMissedMagicPoints;			AST(USERFLAG(TOK_LOGPARSER_MUTABLE))
	// when using FillMissedMagicPoints, use this as the sum fill value
	float fFillMissedMagicPointsSum;		AST(USERFLAG(TOK_LOGPARSER_MUTABLE))
	// when using FillMissedMagicPoints, use this as the count fill value
	int iFillMissedMagicPointsCount;		AST(USERFLAG(TOK_LOGPARSER_MUTABLE))

	bool bIAmALongTermGraph;				NO_AST //if true, I am the long term graph for another graph
	bool bThisGraphAlreadyExists;			NO_AST //used during graph loading to not recreate graphs that were already loaded in

	//log filesnames that should be loaded in order for this graph to be created. Comma separated
	//list. (This is for convenience so that people can choose only a subset of file to load and still see what
	//they're interested in).
	char *pFilenamesNeeded;					AST(USERFLAG(TOK_LOGPARSER_MUTABLE))

	//loaded float template file
	char *pTemplateString;					NO_AST

	//if true, and if this is a cluster-level log parser, prepend the shard name to the category for every data point,
	//including adding categories to a graph that otherwise wouldn't have categories
	bool bUseShardNamesForCategoriesAtClusterLevel;

	//this is the "Simple" way to auto-create a total from all categories... every log that goes into any category is duplicated
	//and a copy is put in a category called "total". This works perfectly for graphs which are basically counting events over time
	//periods... as in, there were 5 events in category A in this time period, and 3 in category B, so there will be 8 in the "total"
	//category.
	bool bAutoCreateTotal;


/*this is a more complicated total... so if there's a category where you get a data point every once in a while, and draw lines
between them... ie, at time 10 the total is 100, and time 20 it's 150, etc. Then if you have two different categories
and just tried to use a simple total, it wouldn't work.... so imagine this input:

Time	Category     Value
1		A			 100
2		B			 70
10		A			 110
11		B			 65

WIth a simple sum, you'd end up with a line that went from 100 to 70 to 110 to 65. So with a PeriodicCategoryTotal, each time
it gets a data point, it adds a total data point which is the sum of all the most recent category data.... so in our example, it would
look like this:

Time	Category     Value		Total
1		A			 100		100
2		B			 70			170
10		A			 110		180
11		B			 65			175

Note that since this is usuall used for periodically updating reporting, you might want to use it with iSecondsOfInactivityBeforeSetToZero
*/
	bool bPeriodicCategoryTotal;


	//only applies to graphs which get regular data points and have time as their barfield. Any time you go
	//this many seconds without getting a data point, add a "0" data point, per category
	int iSecondsOfInactivityBeforeSetToZero;

	//if true, then disable (ignore) this graph on the cluster-level 
	//log-parser
	bool bDisableAtClusterLevel;

} GraphDefinition;


/*A UniqueCounter is something that is specifically designed for the type of counting that you need
to do in order to calculate unique logins over a time period. So for instance if it sees these logs:
thingHappened ID 23
thingHappened ID 23
thingHappened ID 24
thingHappened ID 23
thingHappened ID 24
thingHappened ID 25

and was told to count IDs, then it would count up to 3, then after n seconds it would emit a procedural
log with 3 in it

note that the count isn't 100% guaranteed to be precisely dead-on accurate because it CRCs the string of the field
it's looking at, so hypothetically can very slightly undercount
*/

AUTO_STRUCT;
typedef struct LogParserUniqueCounterDefinition
{
	//name so it can be servermonitored
	char *pName;

	//how many seconds to count for (only one of these should be set)
	int iCountingPeriod;
	int iCountingPeriod_Hours;
	
	//what action name to listen for
	const char *pActionName;				AST(POOL_STRING) 

	UseThisLogCheckingStruct useThisLog;	AST(EMBEDDED_FLAT)

	//an xpath into the parsed log which gives us the field that we're counting
	char *pFieldToCount;
	bool bFieldToCountIsInteger;

	//action name of the procedural log to generate every iCountingPeriod seconds (the count will be in a name-value-pair named count)
	char *pOutActionName; AST(POOL_STRING)
} LogParserUniqueCounterDefinition;


AUTO_STRUCT;
typedef struct LogParserLongTermData 
{
	char *pGraphName;
	float fData;
	char *pDataType; //"Min" "Max" or "Average"
} LogParserLongTermData;

AUTO_STRUCT;
typedef struct LogParserConfig
{
	// Graphs
	GraphDefinition **ppGraphs;

	//Unique Counters
	LogParserUniqueCounterDefinition **ppUniqueCounters;

	// LPCOs
	LPCOStringGroupInput **ppLPCOStringGroupInputs;
	LPCODataGroupInput **ppLPCODataGroupInputs;
	LPCOFSMDef **ppLPCOFSMs;
	LPCOSimpleDataInput **ppLPCOSimpleDataInputs;
	LPCOSimpleListInput **ppLPCOSimpleListInputs;
	LPCOTrackedValueDef **ppLPCOTrackedValueInputs;
	LPCOSimpleDataWildCardInput **ppLPCOSimpleDataWildCardInputs;
	LPCOProceduralSimpleDataInput **ppLPCOProceduralSimpleDatas;
	LPCOTypeTimedBroadcastInput **ppLPCOTypeTimedBroadcastInputs;


	// Aggregators
	LPTimedAggregator **ppLPTimedAggregator; AST(ADDNAMES(LPTimedAggregators))

	// Misc
	LogCommonSearchLocation **ppCommonSearchLocations;



} LogParserConfig;

//only one of these two may be true
extern bool gbNoShardMode; //this logparser is talking to a logserver, but nothing else
extern bool gbStandAlone; //this logparser is not talking to anything, but instead is loading files

extern bool gbLiveLikeStandAlone; // Implies gbStandAlone
extern bool gbResendLogs; // Implies gbStandAlone

extern bool gbStaticAnalysis; // Implies gbStandAlone
extern U32 giLogLinesUsed;

extern bool gbLoadingBins;

extern LogParserConfig gLogParserConfig;
extern LogParserStandAloneOptions gStandAloneOptions;
extern LogParsingRestrictions gImplicitRestrictions;
extern char **gppExtraLogParserConfigFiles;
			
extern bool gbCurrentlyScanningDirectories;
extern int giDirScanningPercent;
extern char *gpDirScanningStatus;


//we have an enumerated list of callback types purely for performance tracking purposes
AUTO_ENUM;
typedef enum ActionSpecificCallbackType
{
	ASC_AGGREGATOR,
	ASC_LPCO_STRINGGROUP_ACTION,
	ASC_LPCO_DATAGROUP_ACTION,
	ASC_LPCO_FSMDEF_ACTION,
	ASC_LPCO_SIMPLEDATA_ACTION,
	ASC_LPCO_SIMPLEDATAWILDCARD_ACTION,
	ASC_LPCO_SIMPLELIST_ACTION,
	ASC_LPCO_TRACKEDVALUE_ACTION,
	ASC_GRAPH,
	ASC_UNIQUECOUNTER,

	ASC_COUNT, AEN_IGNORE
} ActionSpecificCallbackType;

#define ASC_LPCO_FIRST ASC_LPCO_STRINGGROUP_ACTION
#define ASC_LPCO_LAST ASC_LPCO_TRACKEDVALUE_ACTION

//if bNeedToCopy is set, this log is in static memory somewhere and needs to be
//StructCopied before being shipped around to various places
void ProcessSingleLog(char *pFileName, ParsedLog *pLog, bool bNeedToCopy);

void LogParser_DoCrossroadsStartupStuff(void);


typedef U32 LogParserTimedCallbackFunction(void *pUserData);

typedef void LogParserProcessingCallback(void *pUserData, ParsedLog *pLog, ParseTable *pSubTable, void *pSubObject);

void FindLogsThatMatchExpressionAndProcess(LogParserProcessingCallback *pCB, void *pUserData, UseThisLogCheckingStruct *pCheckingStruct,
	ParsedLog *pLog, ActionSpecificCallbackType eCallbackType);

//graph-specific version of objPathGetEString which looks in a substruct if the obj path begins with @
bool Graph_objPathGetEString(const char* path, ParseTable table[], void* structptr, ParseTable subTable[], void *substructptr,
					  char** estr, char **ppResultString);
bool Graph_objPathGetMultiVal(const char* path, ParseTable table[], void* structptr, ParseTable subTable[], void *substructptr,
						MultiVal *result, char **ppResultString);

// Adds a filename to the list of files to check
void RegisterFileNameToMatch(const char *pFileName);

// Add a timed callback that sets its own timing
void RegisterLogParserTimedCallback(const char *pCallbackName, LogParserTimedCallbackFunction *pCB, void *pUserData, U32 iNextTimeToCall);





//things that want to subscribe to log parsing register a callback which will be called on each log
//with a given action name
void RegisterActionSpecificCallback(const char *pActionName, const char *pSubActionName, const char *pGraphName, 
	LogParserProcessingCallback *pCB, void *pUserData, UseThisLogCheckingStruct *pCheckingStruct, 
	ActionSpecificCallbackType eCallbackType);

void DoAllActionSpecificCallbacks(ParsedLog *pLog, const char *pActionName);

void ResetActionSpecificCallbacks(void);


void LogParser_AddProceduralLog(ParsedLog *pLog, const char *pFixedUpLogString);

void LoadStandAloneOptions(char *pFileName);
void SaveStandAloneOptions(char *pFileName);

bool LogParserPostProcessCB(ParsedLog *pLog);

void ProcessAllProceduralLogs();

void LogParser_FullyResetEverything();

enumLogParsingFlags GetCurrentParsingFlags(void);

//text which is basically appended to the end of logparserconfig.txt, but which can 
//be set at run-time through the web interface
extern char *gpRunTimeConfig;

#define EXCFILTER_NAME_PREFIX "Exclusion_"

void FakeSendPacket(char *pFileName, char *pLogString);

void BeginDirectoryScanning(void);
void AbortDirectoryScanning(void);

U32 LogParserRoundTime(U32 iTime, U32 iRoundAmount, U32 iOffset);

AUTO_STRUCT;
typedef struct LogParserPerfStats
{
	U64 iNumLogsProcessed;
	U64 iLogBytesProcessed; AST(FORMATSTRING(HTML_BYTES=1))

	U64 iActionCallbacks[ASC_COUNT];
	U64 iTotalLPCOCallbacks;

	int iLogsPerSecond;
	U64 iBytesPerSecond; AST(FORMATSTRING(HTML_BYTES=1))
	int iGraphCallbacksPerSecond;
	int iLPCOCallbacksPerSecond;

} LogParserPerfStats;

extern LogParserPerfStats gPerfStats;