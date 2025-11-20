#pragma once
#include "LogParser.h"

typedef struct NameValuePair NameValuePair;
typedef struct Expression Expression;

AUTO_STRUCT;
typedef struct LPCOSimpleProceduralLogDef
{
	char **ppStateName; AST(POOL_STRING) //used by FSMs to check whether to generate this log, ignored by everyone else
	char **ppLastStateName; AST(POOL_STRING) //used by FSMs to check whether to generate this log, ignored by everyone else

	char *pActionName; AST(POOL_STRING)

		//name value pairs, with xpaths into the LPCO in curly braces.
		//for example: "FPS {.DataGroups[fps].MostRecentVal} MapName {.SimpleData[MapName].value}" 
		//(the procedural log will only be generated if all the xpaths resolve)
	char *pLogString; 
}  LPCOSimpleProceduralLogDef;

AUTO_STRUCT;
typedef struct LPCOFSMState
{
	const char *pName; AST(KEY, POOL_STRING)
	int iTimesEntered; 
	int iTimesLeft;
	U32 iTotalTime; AST(FORMATSTRING(HTML_SECS_DURATION_SHORT=1))
	U32 iLastVisitLength; AST(FORMATSTRING(HTML_SECS_DURATION_SHORT=1))
	U32 iFirstVisitLength; AST(FORMATSTRING(HTML_SECS_DURATION_SHORT=1))
	int iAverageTime; AST(FORMATSTRING(HTML_SECS_DURATION_SHORT=1))
} LPCOFSMState;

//LPCOFSM = Log Parser Constructed Object Finite State Machine, for anyone who wants to read this
//source file then make fun of me.
AUTO_STRUCT;
typedef struct LPCOFSM
{
	const char *pName; AST(KEY, POOL_STRING)
	const char *pCurStateName; AST(POOL_STRING) //NULL if none
	LPCOFSMState **ppStates;
	U32 iTimeEnteredCurState; AST(FORMATSTRING(HTML_SECS=1))
	char **ppComments; AST(ESTRING)//will save a record of the most recent 50 state transactions
} LPCOFSM;





AUTO_STRUCT;
typedef struct LPCODataGroup
{
	const char *pName; AST(KEY, POOL_STRING)

		//UNDESCRIPTIVENAME means that when choosing which fieldnames to display,
		//this name all by itself is meaningless, so the hybrid obj code should always
		//grab another name from the xpath to make something human-readable
		//
		//count and average default to -1, so that it's easy to see when data doesn't exist
	int iCount; AST(DEF(-1) FORMATSTRING(HTML_UNDESCRIPTIVENAME=1))
	float fSum; AST(DEF(-1) FORMATSTRING(HTML_UNDESCRIPTIVENAME=1))
	float fAverage; AST(DEF(-1) FORMATSTRING(HTML_UNDESCRIPTIVENAME=1))
	float fMostRecentVal; AST(DEF(-1) FORMATSTRING(HTML_UNDESCRIPTIVENAME=1))
} LPCODataGroup;

AUTO_STRUCT;
typedef struct LPCOStringGroupMember
{
	const char *pString; AST(KEY)
	int iCount; 
} LPCOStringGroupMember;

AUTO_STRUCT;
typedef struct LPCOStringGroup
{
	const char *pName; AST(KEY, POOL_STRING)
	int iCount;
	LPCOStringGroupMember **ppMembers;
} LPCOStringGroup;

AUTO_STRUCT;
typedef struct LPCOTrackedValueInstance
{
	char *pValue;
	NameValuePair **ppPairs;
} LPCOTrackedValueInstance;

AUTO_STRUCT;
typedef struct LPCOTrackedValue
{
	const char *pName; AST(KEY, POOL_STRING)
	LPCOTrackedValueInstance *pFirstValue; //the first value ever received
	LPCOTrackedValueInstance **ppValues; AST(FORMATSTRING(HTML_NO_DEFAULT_SORT = 1)) //the most recent values received, with [0] being the newest
	char *pCurVal; AST(ESTRING, FORMATSTRING(HTML_DONTUSENAME=1)) //if set, always points to ppValues[0]->pValue
} LPCOTrackedValue;

//an LPCO can have a simple list of items, which can be set, added to, or removed
AUTO_STRUCT;
typedef struct LPCOSimpleListElement
{
	char *pName; AST(KEY)
} LPCOSimpleListElement;

AUTO_STRUCT;
typedef struct LPCOSimpleList
{
	const char *pName; AST(KEY, POOL_STRING)

	LPCOSimpleListElement **ppElements;
} LPCOSimpleList;

AUTO_STRUCT;
typedef struct LPCOProceduralDataList
{
	LPCOProceduralSimpleDataInput **ppList;
} LPCOProceduralDataList;


AUTO_STRUCT;
typedef struct LogParserConstructedObject
{
	const char *pTypeName; AST(POOL_STRING)
	const char *pName; AST(KEY, POOL_STRING)
	LPCODataGroup **ppDataGroups;
	LPCOStringGroup **ppStringGroups;
	LPCOFSM **ppFSMs;
	NameValuePair **ppSimpleData;
	LPCOTrackedValue **ppTrackedValues;
	LPCOSimpleList **ppSimpleLists;

	//some LPCOs get their ParsingCategory from the logs, where it is set by callback. Others 
	//have it directly set via callback
	bool bParsingCategorySetDirectly; AST(FORMATSTRING(HTML_SKIP=1))

	LPCOProceduralDataList *pProceduralData; NO_AST

	int iDirtyInt; AST(FORMATSTRING(HTML_SKIP=1)) //like a dirty bit, but set to a global value so that it doesn't need to be cleared
	bool bDirtyBit; NO_AST
	char *pPeriodicWriteFileName; AST(NO_WRITE, FORMATSTRING(HTML_SKIP=1))
} LogParserConstructedObject;


//defines a type of log that will be added to an LPCOStringGroup
AUTO_STRUCT;
typedef struct LPCOStringGroupInput
{
	const char *pActionName; AST(POOL_STRING) //the action name to look for in a log
	char *pLPCOTypeName; AST(POOL_STRING) //what type of LPCO to apply this log to
	
	UseThisLogCheckingStruct useThisLog; AST(EMBEDDED_FLAT)
	
	char *pLPCOName; //an xpath inside the log which gives the name of the LPCO to add this string to
	char *pStringGroupName; //a constant string which says which string group inside the LPCO to add this string to
	char *pStringToAdd; //an xpath inside the log which is what string to add to that string group
} LPCOStringGroupInput;

AUTO_STRUCT;
typedef struct FloatAlias
{
	char *pName; AST(STRUCTPARAM)
	float fVal; AST(STRUCTPARAM)
} FloatAlias;

//defines a type of log that will be added to an LPCODataGroup
AUTO_STRUCT;
typedef struct LPCODataGroupInput
{
	const char *pActionName; AST(POOL_STRING) //the action name to look for in a log
	char *pLPCOTypeName; AST(POOL_STRING) //what type of LPCO to apply this log to
	
	UseThisLogCheckingStruct useThisLog; AST(EMBEDDED_FLAT)
	
	char *pLPCOName; //an xpath inside the log which gives the name of the LPCO to add this data to
	char *pDataGroupName; //a constant string which says which data group inside the LPCO to add this data to

	//only one of the following should be set
	char *pDataField; //an xpath inside the log which is the data to add
	char *pDataExpressionString; AST(ADDNAMES(DataExpression))//an expression about the log which is the data to add

	Expression *pDataExpression; NO_AST

	FloatAlias **ppAliases; //aliases for converting strings to floats (kind of like static defines)

	bool bErrorOnEmptyInput; //normally, if the float input is empty (ie, all whitespace), the input is just
		//ignored. Set this to true to cause that to generate an error
	

	bool bAverageIsDefaultForMonitoring;
	bool bSumIsDefaultForMonitoring; AST(ADDNAMES(SumIsDefaultInsteadOfAverage))
	bool bCountIsDefaultForMonitoring; AST(ADDNAMES(CountIsDefaultInsteadOfAverage))

	LPCOSimpleProceduralLogDef **ppProceduralLogs;
} LPCODataGroupInput;

AUTO_STRUCT;
typedef struct LPCOSimpleDataInput
{
	const char *pActionName; AST(POOL_STRING) //the action name to look for in a log
	char *pLPCOTypeName; AST(POOL_STRING) //what type of LPCO to apply this log to
	
	UseThisLogCheckingStruct useThisLog; AST(EMBEDDED_FLAT)

	char *pLPCOName;

	char *pDataNameInLPCO; //constant string, the name the data will have once it's put into the LPCO
	char *pSourceDataName; //xpath into parsed log, the name the data has in the source

	char *pTranslationCommand;

	bool bIsDefaultForMonitoring; //show this field by default while servermonitoring 
} LPCOSimpleDataInput;

AUTO_STRUCT;
typedef struct LPCOSimpleDataWildCardInput
{
	const char *pActionName; AST(POOL_STRING) //the action name to look for in a log
	char *pLPCOTypeName; AST(POOL_STRING) //what type of LPCO to apply this log to
	
	UseThisLogCheckingStruct useThisLog; AST(EMBEDDED_FLAT)

	char *pLPCOName;

	char *pPrefix; //if set, this will be prepended to the key name gotten out of the log before
		//adding simple data to the LPCO

	char *pSourceWildCardString;
};

AUTO_STRUCT;
typedef struct LPCOFSMStateDef
{
	LPCOFSMDef *pParent; NO_AST
	const char *pActionName; AST(POOL_STRING)
	const char *pLiteralStateName; AST(POOL_STRING, ADDNAMES(StateName))
	char *pXPathStateName;

	UseThisLogCheckingStruct useThisLog; AST(EMBEDDED_FLAT)

	LPCOSimpleProceduralLogDef **ppProceduralLogs;
} LPCOFSMStateDef;

AUTO_STRUCT;
typedef struct LPCOFSMDef
{
	char *pLPCOTypeName; AST(POOL_STRING) //what type of LPCO to apply this log to
	char *pLPCOName; //an xpath inside the log which gives the name of the LPCO to add this data to
	char *pFSMName; AST(POOL_STRING)

	LPCOFSMStateDef **ppStates;
} LPCOFSMDef;


//this sets up a procedural log that is associated with a pair associated with a 
//tracked val, every time the tracked val changes. For example, if you want to generate "kills per hour"
//data points, then "time played" is your tracked value and "number of kills" is the pair (name/value pair).
//
//So, for instance, you'd know that when time played was 1000, num kills was 5, then when time played was
//1100, num kills was 8. Then you want to generate a kills-per-second of (8-5)/(1100-1000). Multiplying
//by 3600 will give kills per hour
//
//These logs will only be generated when the tracked value CHANGES (ie, not when it's initially set,
//nor when it's set again to the same value), and will only work if the old and new values of the tracked value,
//and the old and new values of the pair, are all parsable as floats.
//
//ExtraFields are extra name/value pairs that go along with this log. For instance, you might 
//want to log the level along with every kph log, so this would take "Level %d" into the procedural log string
AUTO_STRUCT;
typedef struct LPCOTrackedValuePairDeltaProceduralLogDefExtraFields
{
	char *pFieldName;
	char *pFieldValue; //if it starts with a ., it's an xpath in lpco, otherwise a constant string
} LPCOTrackedValuePairDeltaProceduralLogDefExtraFields;



AUTO_STRUCT;
typedef struct LPCOTrackedValuePairDeltaProceduralLogDef
{
	char *pActionName; AST(POOL_STRING)
	char *pDeltaDataNameInLog; //ie, "kph". The name for the float value generated
	bool bDivideByTrackedValDelta; //in the above example, if true, the float value is (8-5)/(1100-1000). Otherwise
		//it's just (8-5)
	float fScale; //in the above example, put 3600.0 here to convert per-second to per-hour.

	LPCOTrackedValuePairDeltaProceduralLogDefExtraFields **ppExtraFields;
} LPCOTrackedValuePairDeltaProceduralLogDef;


AUTO_STRUCT;
typedef struct LPCOTrackedValuePairDef
{
	char *pPairName;

	//one but not both of these must exist
	char *pPairValue; //xpath in LPCO 
	char *pLogPairValue; //xpath in log

	char *pPairDefault; //if the xpath doesn't exist, use this string instead

	LPCOTrackedValuePairDeltaProceduralLogDef **ppProceduralLogDefs;
} LPCOTrackedValuePairDef;

AUTO_STRUCT;
typedef struct LPCOTrackedValueDef
{
	const char *pActionName; AST(POOL_STRING) //the action name to look for in a log
	char *pLPCOTypeName; AST(POOL_STRING) //what type of LPCO to apply this log to
	
	UseThisLogCheckingStruct useThisLog; AST(EMBEDDED_FLAT)

	char *pLPCOName; //xpath in log

	char *pDataNameInLPCO; AST(POOL_STRING)//constant string, the name the data will have once it's put into the LPCO
	char *pSourceDataName; //xpath into parsed log, the name the data has in the source

	bool bUseThisValueEvenIfNotNew; 
	int iNumValuesToSave; AST(DEF(5))

	float fMinInterval; //if true, then the values must be floats. New  values are ignored until 
						//they differ from the previous one by at least this amount. (Useful for
						//querying things every hour to get more accurate kph, for example.)

	LPCOTrackedValuePairDef **ppPairDefs;
	char **ppExpressionStrings; //each of these expressions are evaluated when this value is updated. They can include
						  //expression function calls, which can cause data points to be added to other graphs, etc.

	Expression **ppExpressions; NO_AST //synced up with ppExpressions

	bool bIsDefaultForMonitoring;
} LPCOTrackedValueDef;


AUTO_ENUM;
typedef enum LPCOSimpleListOp
{
	LIST_CLEAR,
	LIST_SET,
	LIST_ADD,
	LIST_REMOVE,
} LPCOSimpleListOp;

AUTO_STRUCT;
typedef struct LPCOSimpleListInput
{
	const char *pActionName; AST(POOL_STRING) //the action name to look for in a log
	char *pLPCOTypeName; AST(POOL_STRING) //what type of LPCO to apply this log to
	
	UseThisLogCheckingStruct useThisLog; AST(EMBEDDED_FLAT)

	

	char *pLPCOName;

	char *pListName; AST(POOL_STRING) //constant string, the list name in the LPCO
	char *pSourceDataName; //xpath into parsed log, the name the data has in the source (not used for CLEAR)

	LPCOSimpleListOp eListOp;

	char *pSeparators; //separators to use for SET. 


} LPCOSimpleListInput;

AUTO_STRUCT;
typedef struct LPCOProceduralSimpleDataInput
{
	char *pLPCOTypeName; AST(POOL_STRING)

	char *pDataNameInLPCO;

	char *pExpressionString; AST(NAME(Expression))
	Expression *pExpression; NO_AST
} LPCOProceduralSimpleDataInput;

//defines a timed callback that can generate procedural logs an possibly clear the LPCO.
AUTO_STRUCT;
typedef struct LPCOTypeTimedBroadcastInput
{
	char *pLPCOTypeName; AST(POOL_STRING) //what type of LPCO to apply this log to
		
	char *pCallbackName; 

	U32 iFrequencyInSeconds;

	U32 iOffset; 

	bool bClear; // Whether to clear the LPCO when this callback fires.
} LPCOTypeTimedBroadcastInput;

void LPCO_InitSystem(void);
void LPCO_DumpStatusFile(void);
void LPCO_LoadFromStatusFile(void);

//the difference between these two is that FullyReset assumes
//gLogParserConfig has changed and reloads from it
void LPCO_ResetSystem(void);
void LPCO_FullyResetSystem(void);

LogParserConstructedObject *FindExistingLPCOFromNameAndTypeName(const char *pLPCOName /*not necessarily pooled*/, const char *pLPCOTypeName /*pooled*/);
StashTable GetLPCOTypeTableFromTypeName(char *pLPCOTypeName /*pooled*/);

//returns 1 on match, 0 on no-match, -1 on insufficient data/missing fields
int PlayerLPCOMatchesFilter(LogParserConstructedObject *pObj, LogParserPlayerLogFilter *pFilter);

//should be called after every log is finished being processed
void LPCOSystem_PostLogProcessUpdate(U32 iTime);