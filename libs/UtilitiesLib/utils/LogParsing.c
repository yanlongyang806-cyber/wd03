#include "LogParsing.h"
#include "LogParsing_h_ast.h"
#include "GlobalTypes.h"
#include "estring.h"
#include "Sock.h"
#include "timing.h"
#include "file.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "tokenstore.h"
#include "NameValuePair.h"
#include "expression.h"
#include "HashFunctions.h"
#include "Regex.h"
#include "MemoryPool.h"
#include "MemTrackLog.h"
#include "loggingEnums.h"

bool gbVerboseLogParsing = false;
AUTO_CMD_INT(gbVerboseLogParsing, VerboseLogParsing);

//these are used only in verboseLogParsing, which is currently nonelegantly
//scattered between logparsing.c and logparser.c
S64 giTotalLogsScanned;
S64 giLogsThatPassedFiltering;


bool gbLogParsing_DoExtraStandaloneParsing = false;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define DATE_WIDTH 15
#define LOGID_WIDTH 8

// Option to allow disabling of activity parsing in case of problems.
int gpEnableActivityParsing = 1;
AUTO_CMD_INT(gpEnableActivityParsing, setEnableActivityParsing) ACMD_CMDLINE;

static const char *spEventPoolString = NULL;
static const char *spDeltaPoolString = NULL;
static const char *spDamagePoolString = NULL;

//if true, then try to parse all serverLogs regardless of whether the action name seems to be one that we want
static bool sbAlwaysParseAllActions = false;
AUTO_CMD_INT(sbAlwaysParseAllActions, AlwaysParseAllActions) ACMD_CMDLINE;

extern ParseTable parse_NullStruct[];
#define TYPE_parse_NullStruct NullStruct

#define PREPARSED_STASH_PURGE_DELAY 60

static StashTable gServerDataStash = NULL;
static StashTable gPreparsedStash = NULL;
static U32 gLastPurged;

//these values 
static U32 siMostRecentLogTimeRead = 0;
static U64 siTotalLogCount = 0;


U32 LogParsing_GetMostRecentLogTimeRead(void)
{
	return siMostRecentLogTimeRead;
}

//note that this value does not reset over multiple runs, so is only meaningful for purposes of making deltas
U64 LogParsing_GetTotalLogsRead(void)
{
	return siTotalLogCount;
}

typedef struct ServerData
{
	U32 iServerIP;
	int iServerPID;
	char *pExtraInfo;
} ServerData;


U32 fastAtoi_Unsigned(char *pStr)
{
	U32 iRetVal = 0;
	char c;

	while (*pStr == ' ')
	{
		pStr++;
	}

	c = pStr[0];

	if (c >= '0' && c <= '9')
	{
		iRetVal = c - '0';
		c = pStr[1];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[2];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[3];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[4];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[5];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[6];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[7];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[8];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[9];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
	}}}}}}}}}}

	return iRetVal;
}


S32 fastAtoi_Signed(char *pStr)
{
	U32 iRetVal = 0;
	char c;
	bool bNeg = false;

	while (*pStr == ' ')
	{
		pStr++;
	}

	while (*pStr == '-')
	{
		bNeg = !bNeg;
		pStr++;
	}

	c = pStr[0];

	if (c >= '0' && c <= '9')
	{
		iRetVal = c - '0';
		c = pStr[1];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[2];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[3];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[4];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[5];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[6];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[7];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[8];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[9];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
	}}}}}}}}}}

	if (bNeg)
	{
		return -((S32)iRetVal);
	}
	else
	{
		return iRetVal;
	}
}

bool StringToInt_Fast(char *pStr, int *pOutInt)
{
	U32 iRetVal = 0;
	char c;
	bool bNeg = false;
	int iLastDigit = 0;

	while (*pStr == ' ')
	{
		pStr++;
	}

	while (*pStr == '-')
	{
		bNeg = !bNeg;
		pStr++;
	}

	c = pStr[0];

	if (!(c >= '0' && c <= '9'))
	{
		return false;
	}
	else
	{
		iRetVal = c - '0';
		c = pStr[1];
		iLastDigit = 0;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[2];
		iLastDigit = 1;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[3];
		iLastDigit = 2;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[4];
		iLastDigit = 3;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[5];
		iLastDigit = 4;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[6];
		iLastDigit = 5;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[7];
		iLastDigit = 6;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[8];
		iLastDigit = 7;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[9];
		iLastDigit = 8;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		iLastDigit = 9;
	}}}}}}}}}}

	pStr += iLastDigit + 1;

	while (*pStr == ' ')
	{
		pStr++;
	}

	if (*pStr == 0)
	{
		if (bNeg)
		{
			*pOutInt = -((S32)iRetVal);
		}
		else
		{
			*pOutInt = iRetVal;
		}
		return true;
	}

	return false;
}




static bool sbAlwaysFailToParseLogs = false;
AUTO_CMD_INT(sbAlwaysFailToParseLogs, AlwaysFailToParseLogs);

void DestructPreparsedValue(void *val)
{
	PreparsedLog *log = (PreparsedLog *)val;
	if(log)
	{
		StructDestroyVoid(log->table, log->log);
		free(log);
	}
}

void PreParsedLogRemoveRef(ParsedLog* pLog)
{
	if(pLog->pPreParsed)
	{
		devassert(pLog->pPreParsed->log == pLog->pObjInfo->pKillEvent ||
			pLog->pPreParsed->log == pLog->pObjInfo->pGameEvent ||
			pLog->pPreParsed->log == pLog->pObjInfo->pItemGainedEvent);

		pLog->pPreParsed->refCount--;
		//assert(pLog->pPreParsed->refCount >= 0);
		if(pLog->pPreParsed->refCount) // if refCount is 0, just destroy with the following structdestroy
		{
			if(pLog->pObjInfo->pGameEvent == pLog->pPreParsed->log)
				pLog->pObjInfo->pGameEvent = NULL;
			else if(pLog->pObjInfo->pKillEvent == pLog->pPreParsed->log)
				pLog->pObjInfo->pKillEvent = NULL;
			else if(pLog->pObjInfo->pItemGainedEvent == pLog->pPreParsed->log)
				pLog->pObjInfo->pItemGainedEvent = NULL;
		}
		else
		{
			free(pLog->pPreParsed);
			pLog->pPreParsed = NULL;
		}
	}
}

typedef enum PreParsedLogRetrievalType
{
	PreParsedLogIgnore = 0,
	PreParsedLogCache,
	PreParsedLogRetrieve
}PreParsedLogRetrievalType;

void InitServerDataStashTable()
{
	gServerDataStash = stashTableCreate(10, StashDeepCopyKeys_NeverRelease, StashKeyTypeStrings, 0);
}

void CacheServerData(ParsedLog *pLog, ServerData *pServerData, const char *sServerIdString)
{
	static const char *spPoolString = NULL;																							\
	if (!spPoolString)																												\
	{																																\
		spPoolString = allocAddString("ServerData");																					\
	}																																\

	if(pLog->pObjInfo && pLog->pObjInfo->pAction == spPoolString)
	{
		U32 iServerIP = 0;
		int iServerPID = 0;
		char *pExtraInfo = NULL;
		char *pReadHead;
		char *pCurStart;
		char *pStringEnd;

		if(!pLog->pMessage)
			return;

		pStringEnd = pLog->pMessage;

		while(*pStringEnd)
			pStringEnd++;

		pCurStart = pLog->pMessage;

		// Read Ip
		pCurStart += 3;
		if(pCurStart >= pStringEnd)
		{
			return;
		}

		pReadHead = strchr_fast_nonconst(pCurStart, ' ');

		if(!pReadHead)
		{
			return;
		}

		*pReadHead = 0;
		iServerIP = ipFromString(pCurStart);
		*pReadHead = ' ';

		// Read Port
		pCurStart = pReadHead + 3;
		if(pCurStart >= pStringEnd)
		{
			return;
		}
		pReadHead = strchr_fast_nonconst(pCurStart, ' ');

		if(!pReadHead)
		{
			iServerPID = fastAtoi_Unsigned(pCurStart);
		}
		else
		{
			*pReadHead = 0;
			iServerPID = fastAtoi_Unsigned(pCurStart);
			*pReadHead = ' ';

			pCurStart = pReadHead + 3;

			if(pCurStart >= pStringEnd)
			{
				return;
			}

			estrCopy2(&pExtraInfo, pCurStart);
		}

		if(pServerData)
		{
			pServerData->iServerIP = iServerIP;
			pServerData->iServerPID = iServerPID;
			estrDestroy(&pServerData->pExtraInfo);
			pServerData->pExtraInfo = pExtraInfo;
		}
		else
		{
			ServerData *pNewData = (ServerData*)malloc(sizeof(ServerData));
			if(pNewData)
			{
				pNewData->iServerIP = iServerIP;
				pNewData->iServerPID = iServerPID;
				pNewData->pExtraInfo = pExtraInfo;
				stashAddPointer(gServerDataStash, sServerIdString, pNewData, true);
			}
		}
	}
}
#if USE_PREPARSED_LOGS

void InitPreparsedStashTable()
{
	gLastPurged = timeSecondsSince2000();
	gPreparsedStash = stashTableCreate(10, StashDeepCopyKeys_NeverRelease, StashKeyTypeStrings, 0);
}

void UpdatePreparsedStashTable()
{
	PERFINFO_AUTO_START_FUNC();
	if(gPreparsedStash)
	{
		if(timeSecondsSince2000() >= gLastPurged + PREPARSED_STASH_PURGE_DELAY)
		{
			StashTableIterator iter;
			StashElement ppElem;
			stashGetIterator(gPreparsedStash, &iter);
			while(stashGetNextElement(&iter, &ppElem))
			{
				PreparsedLog *pLog = (PreparsedLog*)stashElementGetPointer(ppElem);
				if(timeSecondsSince2000() >= pLog->time_stamp + PREPARSED_STASH_PURGE_DELAY)
				{
					pLog->refCount--;
					//assert(pLog->refCount >= 0);
					if(!pLog->refCount)
					{
						StructDestroyVoid(pLog->table, pLog->log);
						free(pLog);
					}
					stashRemovePointer(gPreparsedStash, stashElementGetStringKey(ppElem), &pLog);
				}
			}
			gLastPurged = timeSecondsSince2000();
		}
	}
	PERFINFO_AUTO_STOP();
}
#endif

// Profile by log filename.
void LogParserStartFileTimer(const char *pName)
{
	static LogFilePerf **perf = NULL;
	static StashTable sFilePerf = NULL;
	bool success;
	int index;
	char* nopath;
	char* dot;
	char newpath[MAX_PATH];

	nopath = strrchr(pName, '/');
	if(nopath && nopath[1])
	{
		strcpy(newpath, nopath + 1);
		dot = strchr(newpath, '.');
		if(dot)
			*dot = '\0';
	}
	else
		strcpy(newpath, pName);

	// Create performance entry if necessary.
	if (!sFilePerf)
		sFilePerf = stashTableCreateWithStringKeys(1009, StashDeepCopyKeys_NeverRelease);
	success = stashFindInt(sFilePerf, newpath, &index);
	if (!success)
	{
		LogFilePerf *ptr = malloc(sizeof(LogFilePerf));
		ptr->name = strdup(newpath);
		ptr->pi = NULL;
		index = eaPush(&perf, ptr);
		stashAddInt(sFilePerf, newpath, index, true);
	}
	devassert(perf);

	// Start profiling.
	PERFINFO_AUTO_START_STATIC(perf[index]->name, &perf[index]->pi, 1);
}

bool gbLogCountingMode = false;
AUTO_CMD_INT(gbLogCountingMode, LogCountingMode) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

//#define INIT_SERVER_ID_STRING(string_to_set, log) estrPrintf(&string_to_set, "%s[%u]", GlobalTypeToName(log->eServerType), log->iServerID)
#define INIT_SERVER_ID_STRING(log) if(!log->serverIdString[0]) sprintf(log->serverIdString, "%s[%u]", GlobalTypeToName(log->eServerType), log->iServerID)

static PreparsedLog *GetPreparsedLog(const char* sServerIdString, ParseTable *pParseTable)
{
	PreparsedLog *pPreParsed = NULL;
	if(stashFindPointer(gPreparsedStash, sServerIdString, &pPreParsed))
	{
		if(pPreParsed->refCount)
		{
			pPreParsed->refCount--;
			//assert(pPreParsed->refCount >= 0);
			if(!pPreParsed->refCount)
				StructDestroyVoid(pPreParsed->table, pPreParsed->log);
			else
			{
				pPreParsed = malloc(sizeof(*pPreParsed));
				stashAddPointer(gPreparsedStash, sServerIdString, pPreParsed, true);
			}
		}
	}
	else
	{
		pPreParsed = malloc(sizeof(*pPreParsed));
		stashAddPointer(gPreparsedStash, sServerIdString, pPreParsed, false);
	}

	return pPreParsed;
}

static void CacheStruct(ParsedLog* pLog, const char *sServerIdString, ParseTable *pParseTable, void *pNewStruct)
{
	PreparsedLog *pRetrievedStruct;

	PERFINFO_AUTO_START_FUNC();

	pRetrievedStruct = GetPreparsedLog(sServerIdString, pParseTable);

	// one ref for the stashtable and one for the ParsedLog
	pRetrievedStruct->refCount = 2;
	pLog->pPreParsed = pRetrievedStruct;

	pRetrievedStruct->log = pNewStruct;
	pRetrievedStruct->table = pParseTable;

	pRetrievedStruct->time_stamp = timeSecondsSince2000();

	PERFINFO_AUTO_STOP();
}

static bool ParseAndCacheStruct(ParsedLog* pLog, const char *sServerIdString, char *pTempString, ParseTable *pParseTable, void **pNewStruct)
{
	PERFINFO_AUTO_START_FUNC();

	*pNewStruct = StructCreateVoid(pParseTable);											

	if(ParserReadText(pTempString, pParseTable, *pNewStruct, PARSER_NOERRORFSONPARSE))
	{
		CacheStruct(pLog, sServerIdString, pParseTable, *pNewStruct);
		PERFINFO_AUTO_STOP();
		return true;
	}

	// failed
	StructDestroyVoid(pParseTable, *pNewStruct);
	
	PERFINFO_AUTO_STOP();
	return false;
}

static bool GetParsedStruct(ParsedLog* pLog, const char *sServerIdString, ParseTable *pParseTable, void **pNewStruct)
{
	PreparsedLog *pRetrievedStruct = NULL;
	PERFINFO_AUTO_START_FUNC();
	
	if(stashFindPointer(gPreparsedStash, sServerIdString, &pRetrievedStruct) && pRetrievedStruct && (pRetrievedStruct->table == pParseTable))
	{
		pRetrievedStruct->refCount++;
		pLog->pPreParsed = pRetrievedStruct;
		*pNewStruct = pRetrievedStruct->log;
		pRetrievedStruct->time_stamp = timeSecondsSince2000();
		PERFINFO_AUTO_STOP();
		return true;
	}

	PERFINFO_AUTO_STOP();
	return false;
}


bool CheckForSpecificAction(ParsedLog *pLog, const char *pPooledActionName, int iColumnInObjLog, const char *pMessage, PreParsedLogRetrievalType eRetrievalType)
{
	if (pLog->pObjInfo && pLog->pObjInfo->pAction == pPooledActionName &&																
		parse_ParsedLogObjInfo[iColumnInObjLog].subtable != parse_NullStruct)							
	{																												
		char *pTempString;																							
		bool bSuccess;																								
		ParseTable *pParseTable = parse_ParsedLogObjInfo[iColumnInObjLog].subtable;	
		void *pNewStruct = NULL;

		PERFINFO_AUTO_START_FUNC();
		estrStackCreate(&pTempString);																				
		estrAppendUnescaped(&pTempString, pMessage);																

#if USE_PREPARSED_LOGS
		INIT_SERVER_ID_STRING(pLog);
		switch(eRetrievalType)
		{
		case PreParsedLogCache:
			bSuccess = ParseAndCacheStruct(pLog, pLog->serverIdString, pTempString, pParseTable, &pNewStruct);
			break;
		case PreParsedLogRetrieve:
			bSuccess = GetParsedStruct(pLog, pLog->serverIdString, pParseTable, &pNewStruct);
			break;
		case PreParsedLogIgnore:
			pNewStruct = StructCreateVoid(pParseTable);											

			bSuccess = ParserReadText(pTempString, pParseTable, pNewStruct, PARSER_NOERRORFSONPARSE);

			if(!bSuccess)
				StructDestroyVoid(pParseTable, pNewStruct);
			break;
		default:
			// Must pass in a valid value
			assert(0);
		}
#else
		pNewStruct = StructCreate(pParseTable);											

		bSuccess = ParserReadText(pTempString, pParseTable, pNewStruct, PARSER_NOERRORFSONPARSE);
#endif

		estrDestroy(&pTempString);

		if (!bSuccess)																								
		{																											
			StructDestroy(parse_ParsedLogObjInfo, pLog->pObjInfo);
			pLog->pObjInfo = NULL;
			PERFINFO_AUTO_STOP();
		}																											
		else																										
		{	
			pLog->iParsedLogFlags |= PARSEDLOG_FLAG_MESSAGEWASPARSEDINTOSTRUCT;
			TokenStoreSetPointer(parse_ParsedLogObjInfo, iColumnInObjLog, pLog->pObjInfo, 0, pNewStruct, NULL);
			PERFINFO_AUTO_STOP();
			return true;
		}																											
	}				

	return false;
}

#define CHECK_FOR_SPECIFIC_ACTION(ActionName, FieldName, CapsFieldName, PreParsedLogBehavior)										\
{																																	\
	static const char *spPoolString = NULL;																							\
	PERFINFO_AUTO_START_L2(#ActionName, 1);																							\
	if (!spPoolString)																												\
	{																																\
		spPoolString = allocAddString(#ActionName);																					\
	}																																\
	if (CheckForSpecificAction(pLog, spPoolString, PARSE_PARSEDLOGOBJINFO_##CapsFieldName##_INDEX, pMessage, PreParsedLogBehavior))	\
	{																																\
		PERFINFO_AUTO_STOP_L2();																									\
		PERFINFO_AUTO_STOP();																										\
		return;																														\
	}																																\
	PERFINFO_AUTO_STOP_L2();																										\
}																																	\

//Access Level 9 command run (SERVERWRAPPER). Cmd string: "RequestDebugMenu " Result String: ""
//Access Level 9 command run (SERVER_MONITORING). Cmd string: \qContainerIDsFromAccountName  rehpic\q Result String: \q30621\n1\n\q
bool ParseAccessLevelCommandLog(ParsedLog *pLog, char *pMessage)
{
	int iAccessLevel;
	static char *spHow = NULL;
	char *pFound1;
	char *pFound2;
	AccessLevelCommandInfo *pInfo;
	static char *spUnescaped = NULL;

	if (!strStartsWith(pMessage, "Access Level "))
	{
		return false;
	}

	iAccessLevel = atoi(pMessage + 13);

	pFound1 = strchr(pMessage, '(');
	if (!pFound1)
	{
		return false;
	}

	pFound2 = strchr(pFound1, ')');
	if (!pFound2)
	{
		return false;
	}

	estrClear(&spHow);
	estrConcat(&spHow, pFound1 + 1, pFound2 - pFound1 - 1);

	pFound1 = strstri(pFound2, "Cmd string: \"");

	if (!pFound1)
	{
		estrClear(&spUnescaped);
		estrAppendUnescaped(&spUnescaped, pFound2);
		pFound1 = strstri(spUnescaped, "Cmd string: \"");

		if (!pFound1)
		{
			return false;
		}
	}

	pFound2 = strstri(pFound1, "\" Result String: \"");
	if (!pFound2)
	{
		return false;
	}

	if (!pLog->pObjInfo)
	{
		return false;
	}

	pInfo = pLog->pObjInfo->pAccessLevelCommand = StructCreate(parse_AccessLevelCommandInfo);

	pInfo->iAccessLevel = iAccessLevel;

	estrCopy2(&pInfo->pHowCalled, spHow);

	estrConcat(&pInfo->pCmdString, pFound1 + 13, pFound2 - pFound1 - 14);
	estrCopy2(&pInfo->pResultString, pFound2 + 18);
	estrSetSize(&pInfo->pResultString, estrLength(&pInfo->pResultString) - 1);

	return true;
}

bool ParseTradeLog(ParsedLog *pLog, char *pMessage)
{
	NameValuePair *pPair;
	char IntString[32];
	char *pCurStart = pMessage;
	char *pReadHead = pMessage;
	char *pEndParen;
	int iResourcesTraded = 0;
	PERFINFO_AUTO_START_FUNC();

	pReadHead = strchr_fast_nonconst(pCurStart, ':');
	if (!pReadHead)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	*pReadHead = 0;
	if(!strstr(pCurStart, "Success"))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	*pReadHead = ':';
	
	pCurStart = pReadHead + 2;

	pReadHead = strchr_fast_nonconst(pCurStart, '(');
	if (!pReadHead)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	pEndParen = strchr_fast_nonconst(pReadHead, ')');
	if (!pEndParen)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	++pReadHead;
	while(pReadHead < pEndParen && pReadHead)
	{
		int iCount;
		bool bResult;
		pCurStart = pReadHead;
		pReadHead = strchr_fast_nonconst(pCurStart, ' ');
		if(!pReadHead)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

		*pReadHead = 0;
		bResult = StringToInt_Fast(pCurStart, &iCount);
		*pReadHead = ' ';

		if(!bResult)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

		pCurStart = pReadHead + 1;

		if(*pCurStart == '#')
		{
			iResourcesTraded += iCount;
		}

		pReadHead = strchr_fast_nonconst(pCurStart, ',');
		if(!pReadHead)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

		pReadHead += 2;
	}

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup("ResourcesTraded");
	sprintf(IntString, "%d", iResourcesTraded);
	pPair->pValue = strdup(IntString);

	eaPush(&pLog->ppPairs, pPair);	
	
	PERFINFO_AUTO_STOP();
	return true;
}

static bool ParseItemGainedEvent(char *pMessage, ItemGainedEvent *pItemGainedEvent)
{
	char *pCurStart;
	char *pReadHead;

	PERFINFO_AUTO_START_FUNC();

	pCurStart = pMessage;

	pReadHead = strstri_safe(pCurStart, "Count ");
	if(!pReadHead)
		goto fail;

	pCurStart = strchr(pReadHead, ' ');
	pCurStart = pCurStart + 1;
	pReadHead = strchr(pCurStart, '\r');
	if(!pReadHead)
		goto fail;

	*pReadHead = 0;
	pItemGainedEvent->iCount = fastAtoi_Signed(pCurStart);
	*pReadHead = '\r';

	pReadHead = strstri_safe(pCurStart, "ItemName ");
	if(!pReadHead)
		goto fail;

	pCurStart = strchr(pReadHead, ' ');
	pCurStart = pCurStart + 1;
	pReadHead = strchr(pCurStart, '\r');
	if(!pReadHead)
		goto fail;

	*pReadHead = 0;
	pItemGainedEvent->pItemName = strdup(pCurStart);
	*pReadHead = '\r';

	PERFINFO_AUTO_STOP();
	return true;

fail:
	PERFINFO_AUTO_STOP();
	return false;
}

static bool ParseKillEventParticipant(KillEventParticipant *pParticipant, char *pStart, char *pEnd)
{
	char *pReadHead;
	char *pCurStart;

	PERFINFO_AUTO_START_FUNC();

	pReadHead = strstri_safe(pStart, "isplayer ");
	if(!pReadHead || pReadHead > pEnd)
		pParticipant->bIsPlayer = false;
	else
	{
		pCurStart = strchr(pReadHead, 'r');
		pCurStart = pCurStart + 2;
		pReadHead = strchr(pCurStart, '\r');
		if(!pReadHead || pReadHead > pEnd)
			goto fail;

		*pReadHead = 0;
		pParticipant->bIsPlayer = fastAtoi_Unsigned(pCurStart);
		*pReadHead = '\r';
	}

	if(!pParticipant->bIsPlayer)
	{
		pReadHead = strstri_safe(pStart, "rank ");
		if(!pReadHead || pReadHead > pEnd)
			pParticipant->pRank = allocAddString("");
		else
		{
			pCurStart = strchr(pReadHead, ' ');
			++pCurStart;
			pReadHead = strchr(pCurStart, '\r');
			if(!pReadHead || pReadHead > pEnd)
				goto fail;

			*pReadHead = 0;
			pParticipant->pRank = allocAddString(pCurStart);
			*pReadHead = '\r';
		}

		pReadHead = strstri_safe(pStart, "CritterName ");
		if(!pReadHead || pReadHead > pEnd)
			estrCopy2(&pParticipant->pName, "");
		else
		{
			pCurStart = strchr(pReadHead, ' ');
			++pCurStart;
			pReadHead = strchr(pCurStart, '\r');
			if(!pReadHead || pReadHead > pEnd)
				goto fail;

			*pReadHead = 0;
			estrCopy2(&pParticipant->pName, pCurStart);
			*pReadHead = '\r';
		}

		pReadHead = strstri_safe(pStart, "CritterGroupName ");
		if(!pReadHead || pReadHead > pEnd)
			estrCopy2(&pParticipant->pGroupName, "");
		else
		{
			pCurStart = strchr(pReadHead, ' ');
			++pCurStart;
			pReadHead = strchr(pCurStart, '\r');
			if(!pReadHead || pReadHead > pEnd)
				goto fail;

			*pReadHead = 0;
			estrCopy2(&pParticipant->pGroupName, pCurStart);
			*pReadHead = '\r';
		}
	}

	pReadHead = strstri_safe(pStart, "levelcombat ");
	if(!pReadHead || pReadHead > pEnd)
		pParticipant->bIsPlayer = false;
	else
	{
		pCurStart = strchr(pReadHead, ' ');
		++pCurStart;
		pReadHead = strchr(pCurStart, '\r');
		if(!pReadHead || pReadHead > pEnd)
			goto fail;

		*pReadHead = 0;
		pParticipant->iLevel = fastAtoi_Unsigned(pCurStart);
		*pReadHead = '\r';
	}

	PERFINFO_AUTO_STOP();
	return true;

fail:
	PERFINFO_AUTO_STOP();
	return false;
}

static bool ParseKillEvent(char *pMessage, KillEvent *pKillEvent)
{
	char *pCurStart = NULL;
	char *pReadHead = NULL;
	char *pParticipantStart = NULL;
	char *pParticipantEnd = NULL;
	int tempInt = 0;
	KillEventParticipant *pParticipant = NULL;

	PERFINFO_AUTO_START_FUNC();

	pCurStart = pMessage;
	pReadHead = strstri_safe(pCurStart, "Targets\r\n");
	pParticipantStart = pReadHead;
	if(pParticipantStart)
		pParticipantEnd = strchr(pParticipantStart, '}');

	while(pParticipantStart && pParticipantEnd)
	{
		pParticipant = StructCreate(parse_KillEventParticipant);
		if(ParseKillEventParticipant(pParticipant, pParticipantStart, pParticipantEnd))
		{
			eaPush(&pKillEvent->ppTargets, pParticipant);
		}
		else
		{
			StructDestroy(parse_KillEventParticipant, pParticipant);
			goto fail;
		}

		pCurStart = pParticipantEnd;
		pReadHead = strstri_safe(pCurStart, "Targets\r\n");
		pParticipantStart = pReadHead;
		if(pParticipantStart)
			pParticipantEnd = strchr(pParticipantStart, '}');
	}

	pCurStart = pMessage;
	pReadHead = strstri_safe(pCurStart, "Sources\r\n");
	pParticipantStart = pReadHead;
	if(pParticipantStart)
		pParticipantEnd = strchr(pParticipantStart, '}');

	while(pParticipantStart && pParticipantEnd)
	{
		pParticipant = StructCreate(parse_KillEventParticipant);
		if(ParseKillEventParticipant(pParticipant, pParticipantStart, pParticipantEnd))
		{
			eaPush(&pKillEvent->ppSources, pParticipant);
		}
		else
		{
			StructDestroy(parse_KillEventParticipant, pParticipant);
			goto fail;
		}

		pCurStart = pParticipantEnd;
		pReadHead = strstri_safe(pCurStart, "Sources\r\n");
		pParticipantStart = pReadHead;
		if(pParticipantStart)
			pParticipantEnd = strchr(pParticipantStart, '}');
	}

	pCurStart = strstri_safe(pParticipantEnd, "pos ");
	if(!pCurStart)
		goto fail;
	pCurStart += 4;
	pReadHead = strchr(pCurStart, ',');
	if(!pReadHead)
		goto fail;

	*pReadHead = 0;
	tempInt = fastAtoi_Signed(pCurStart);
	 pKillEvent->pPos[0] = (float)tempInt;
	*pReadHead = ',';

	pCurStart = pReadHead + 2;
	pReadHead = strchr(pCurStart, ',');
	if(!pReadHead)
		goto fail;

	*pReadHead = 0;
	tempInt = fastAtoi_Signed(pCurStart);
	pKillEvent->pPos[1] = (float)tempInt;
	*pReadHead = ',';

	pCurStart = pReadHead + 2;
	pReadHead = strchr(pCurStart, '}');
	if(!pReadHead)
		goto fail;

	*pReadHead = 0;
	tempInt = fastAtoi_Signed(pCurStart);
	pKillEvent->pPos[2] = (float)tempInt;
	*pReadHead = '}';

	pReadHead = strstri_safe(pMessage, "RegionType ");
	if(!pReadHead)
		pKillEvent->eRegionType = LPWRT_None;
	else
	{
		pCurStart = strchr(pReadHead, ' ');
		++pCurStart;

		if(strStartsWith(pCurStart, "Ground"))
			pKillEvent->eRegionType = LPWRT_Ground;
		else if(strStartsWith(pCurStart, "SectorSpace"))
			pKillEvent->eRegionType = LPWRT_SectorSpace;
		else if(strStartsWith(pCurStart, "Space"))
			pKillEvent->eRegionType = LPWRT_Space;
		else if(strStartsWith(pCurStart, "SystemMap"))
			pKillEvent->eRegionType = LPWRT_SystemMap;
		else if(strStartsWith(pCurStart, "CharacterCreator"))
			pKillEvent->eRegionType = LPWRT_CharacterCreator;
	}

	PERFINFO_AUTO_STOP();
	return true;

fail:
	PERFINFO_AUTO_STOP();
	return false;
}

bool CheckForKillsEvent(ParsedLog *pLog, char *pMessage, bool bDup)
{
	static const char *spEventSourceString = NULL;
	static const char *spEventTargetString = NULL;
	static const char *spKillEventSourceString = NULL;
	static const char *spKillEventTargetString = NULL;
	static const char *spKillsString = NULL;

	PERFINFO_AUTO_START_FUNC_L2();

	if (!spEventSourceString)
	{
		spEventSourceString = allocAddString("EventSource");
	}
	if (!spEventTargetString)
	{
		spEventTargetString = allocAddString("EventTarget");
	}
	if (!spKillEventSourceString)
	{
		spKillEventSourceString = allocAddString("KillEventSource");
	}
	if (!spKillEventTargetString)
	{
		spKillEventTargetString = allocAddString("KillEventTarget");
	}
	if (!spKillsString)
	{
		spKillsString = allocAddString(" Kills");
	}

	if(pLog->pObjInfo 
		&& (pLog->pObjInfo->pAction == spEventSourceString || pLog->pObjInfo->pAction == spEventTargetString)
		&& strStartsWith(pMessage, spKillsString))
	{
		char *pTempString;
		bool bSuccess;
		KillEvent *pNewStruct = NULL;

		PERFINFO_AUTO_START_FUNC();

		estrStackCreate(&pTempString);																				
		estrAppendUnescaped(&pTempString, pMessage);																

		if(bDup)
		{
			//Get cached value
			INIT_SERVER_ID_STRING(pLog);
			bSuccess = GetParsedStruct(pLog, pLog->serverIdString, parse_KillEvent, &pNewStruct);
			//assert(!bSuccess || pNewStruct);
		}
		else
		{
			// parse and cache
			INIT_SERVER_ID_STRING(pLog);

			pNewStruct = StructCreate(parse_KillEvent);											

			// parse
			bSuccess = ParseKillEvent(pTempString, pNewStruct);
			// cache
			if(bSuccess)
				CacheStruct(pLog, pLog->serverIdString, parse_KillEvent, pNewStruct);
			else
			{
				StructDestroy(parse_KillEvent, pNewStruct);
				pNewStruct = NULL;
			}
		}

		devassert(bSuccess || !pNewStruct);

		estrDestroy(&pTempString);

		if (bSuccess)																								
		{	
			pLog->iParsedLogFlags |= PARSEDLOG_FLAG_MESSAGEWASPARSEDINTOSTRUCT;
			pLog->pObjInfo->pKillEvent = pNewStruct;
			if(pLog->pObjInfo->pAction == spEventSourceString)
				pLog->pObjInfo->pAction = spKillEventSourceString;
			else if(pLog->pObjInfo->pAction == spEventTargetString)
				pLog->pObjInfo->pAction = spKillEventTargetString;
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP_L2();
			return true;
		}

		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP_L2();
	return false;
}

bool CheckForItemGainedEvent(ParsedLog *pLog, char *pMessage, bool bDup)
{
	static const char *spEventSourceString = NULL;
	static const char *spItemGainedEventSourceString = NULL;
	static const char *spItemGainedString = NULL;

	PERFINFO_AUTO_START_FUNC_L2();

	if (!spEventSourceString)
	{
		spEventSourceString = allocAddString("EventSource");
	}
	if (!spItemGainedEventSourceString)
	{
		spItemGainedEventSourceString = allocAddString("ItemGainedEventSource");
	}
	if (!spItemGainedString)
	{
		spItemGainedString = allocAddString(" ItemGained");
	}

	if(pLog->pObjInfo 
		&& pLog->pObjInfo->pAction == spEventSourceString
		&& strStartsWith(pMessage, spItemGainedString))
	{
		char *pTempString;
		bool bSuccess;
		ItemGainedEvent *pNewStruct;

		PERFINFO_AUTO_START_FUNC();

		estrStackCreate(&pTempString);																				
		estrAppendUnescaped(&pTempString, pMessage);																

		pNewStruct = StructCreate(parse_ItemGainedEvent);											

		if(bDup)
		{
			//Get cached value
			INIT_SERVER_ID_STRING(pLog);
			bSuccess = GetParsedStruct(pLog, pLog->serverIdString, parse_ItemGainedEvent, &pNewStruct);
		}
		else
		{
			// parse and cache
			INIT_SERVER_ID_STRING(pLog);
			// parse
			bSuccess = ParseItemGainedEvent(pTempString, pNewStruct);
			// cache
			if(bSuccess)
				CacheStruct(pLog, pLog->serverIdString, parse_ItemGainedEvent, pNewStruct);
		}

		estrDestroy(&pTempString);

		if (!bSuccess)																								
		{																											
			StructDestroy(parse_ItemGainedEvent, pNewStruct);													
		}																											
		else																										
		{	
			pLog->iParsedLogFlags |= PARSEDLOG_FLAG_MESSAGEWASPARSEDINTOSTRUCT;
			pLog->pObjInfo->pItemGainedEvent = pNewStruct;
			pLog->pObjInfo->pAction = spItemGainedEventSourceString;
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP_L2();
			return true;
		}
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP_L2();

	return false;
}


//here is where you add structs that you want to have parsed out of the log message. Just copy exactly how it is done
//for events
void CheckForStructsInMessage(ParsedLog *pLog, char *pMessage, bool bDup)
{
	//static const char GenericArray_prefix[] = "\\r\\n{\\r\\n\\r\\n\\t";

	PERFINFO_AUTO_START_FUNC();

	if(CheckForItemGainedEvent(pLog, pMessage, bDup))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if(CheckForKillsEvent(pLog, pMessage, bDup))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if(bDup)
	{
		CHECK_FOR_SPECIFIC_ACTION(EventSource, pGameEvent, GAMEEVENT, PreParsedLogRetrieve);
		CHECK_FOR_SPECIFIC_ACTION(EventTarget, pGameEvent, GAMEEVENT, PreParsedLogRetrieve);
	}
	CHECK_FOR_SPECIFIC_ACTION(Event, pGameEvent, GAMEEVENT, PreParsedLogIgnore);
	CHECK_FOR_SPECIFIC_ACTION(EventSource, pGameEvent, GAMEEVENT, PreParsedLogCache);
	CHECK_FOR_SPECIFIC_ACTION(EventTarget, pGameEvent, GAMEEVENT, PreParsedLogCache);
	CHECK_FOR_SPECIFIC_ACTION(ControllerPerfLog, pShardPerf, SHARDPERF, PreParsedLogIgnore);
	CHECK_FOR_SPECIFIC_ACTION(LongTermData, pLongTermData, LONGTERMDATA, PreParsedLogIgnore);
	CHECK_FOR_SPECIFIC_ACTION(DropSummary, pInventoryBagSummary, INVENTORYBAGSUMMARY, PreParsedLogIgnore);
	CHECK_FOR_SPECIFIC_ACTION(EntKill, pInventoryBag, INVENTORYBAG, PreParsedLogIgnore);
	CHECK_FOR_SPECIFIC_ACTION(SurveyMission, pSurveyMission, SURVEYMISSION, PreParsedLogIgnore);
	CHECK_FOR_SPECIFIC_ACTION(Survey, pSurveyMission, SURVEYMISSION, PreParsedLogIgnore);
	CHECK_FOR_SPECIFIC_ACTION(PerfLog, pClientPerf, CLIENTPERF, PreParsedLogIgnore);
	CHECK_FOR_SPECIFIC_ACTION(ControllerOverview, ControllerOverview, CONTROLLEROVERVIEW, PreParsedLogIgnore);
	CHECK_FOR_SPECIFIC_ACTION(StatusReport, StatusReport, STATUSREPORT, PreParsedLogIgnore);	
	CHECK_FOR_SPECIFIC_ACTION(PatchCmdPerf, PatchMsgPerf, PATCHMSGPERF, PreParsedLogIgnore);	
	CHECK_FOR_SPECIFIC_ACTION(MemTrackOps, MemTrackOps, MEMTRACKOPS, PreParsedLogIgnore);

	{
		static const char *spTradeString = NULL;
		if (!spTradeString)
		{
			spTradeString = allocAddString("Trade");
		}

		if(pLog->pObjInfo && pLog->pObjInfo->pAction == spTradeString && ParseTradeLog(pLog, pMessage))
		{
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	{
		static const char *spServerDataString = NULL;
		if (!spServerDataString)
		{
			spServerDataString = allocAddString("ServerData");
		}

		if(pLog->pObjInfo && pLog->pObjInfo->pAction == spServerDataString)
		{
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	{
		static const char *spAccessLevelCommandString = NULL;
		if (!spAccessLevelCommandString)
		{
			spAccessLevelCommandString = allocAddString("AccessLevelCommand");
		}

		if(pLog->pObjInfo && pLog->pObjInfo->pAction == spAccessLevelCommandString)
		{
			if (ParseAccessLevelCommandLog(pLog, pMessage))
			{
				PERFINFO_AUTO_STOP();
				return;
			}
			else
			{
				int iBrk = 0;
			}
		}



	}

	// After all other structs have been parsed, try to parse it as a GenericArray.

	// Note: I wrote some code to speed up the non-GenericArray case.  I think we don't need this, but I'd like to keep it here for now
	// in case we do.
	// We use some heuristics to try to only parse it if it really looks like a GenericArray, to avoid trying to parse
	// other struct-like stuff that probably isn't actually a GenericArray.

	//if (!memcmp(pMessage, GenericArray_prefix, sizeof(GenericArray_prefix) - 1))
	//{
	//	static const char *GenericArray_Fields[] = {"Array", "Integer", "String", "Floating"};
	//	const char *field = pMessage + sizeof(GenericArray_prefix) - 1;
	//	if (strStartsWithAnyStatic(field, GenericArray_Fields))
		{
			char *pTempString = NULL;
			bool bSuccess;
			PERFINFO_AUTO_START("GenericArray", 1);
			estrStackCreate(&pTempString);
			estrAppendUnescaped(&pTempString, pMessage);
			bSuccess = ParserReadText(pTempString, parse_GenericArray, &pLog->pObjInfo->pGenericArray, PARSER_NOERRORFSONPARSE);
			estrDestroy(&pTempString);
			if (bSuccess)
			{
				pLog->iParsedLogFlags |= PARSEDLOG_FLAG_MESSAGEWASPARSEDINTOSTRUCT;
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return;
			}
			PERFINFO_AUTO_STOP();
		}
	//}

	PERFINFO_AUTO_START("GetNameValuePairsFromString", 1);
	GetNameValuePairsFromString(pMessage, &pLog->ppPairs, ",");
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

#define LB_FAIL() { StructDestroy(parse_LoadBalancingInfo, pLoadBalancingInfo); return; }

#define FIND(c) { pFound = strchr(pReadHead, c); if (!pFound) LB_FAIL(); }

#define SAFE_SET_READHEAD(p) { pReadHead = (p); if (pReadHead > pEndOfString) LB_FAIL(); }

void CheckForStructsInMessage_NoObjLog(ParsedLog *pLog, char *pMessage, LogFileParser *pParser)
{
	char *pFound;
	char *pReadHead;

	char *pEndOfString = pMessage + strlen(pMessage);

	GetNameValuePairsFromString(pMessage, &pLog->ppPairs, NULL);

	if (gbLogParsing_DoExtraStandaloneParsing && pLog->eServerType == GLOBALTYPE_CONTROLLER && ((pFound = strstri(pMessage, "About to try to pick a machine to launch a server of type"))))
	{
		static const char *pPooledAction = NULL;
		LoadBalancingInfo *pLoadBalancingInfo = StructCreate(parse_LoadBalancingInfo);
		if (!pPooledAction)
		{
			pPooledAction = allocAddString("LoadBalancing");
		}
		SAFE_SET_READHEAD(pFound + 58);
		pFound = strchr(pFound, ',');
		if (!pFound)
		{
			LB_FAIL();
		}
		*pFound = 0;
		pLoadBalancingInfo->eLaunchServerType = NameToGlobalType(pReadHead);
		*pFound = ',';

		if (!pLoadBalancingInfo->eLaunchServerType)
		{
			LB_FAIL();
		}
		pReadHead = strchr(pReadHead, '(');
		if (!pReadHead)
		{
			LB_FAIL();
		}
		pReadHead++;
		pFound = strchr(pReadHead, ')');
		if (!pFound)
		{
			LB_FAIL();
		}
		*pFound = 0;
		pLoadBalancingInfo->pLaunchServerCategory = strdup(pReadHead);
		*pFound = ')';

		SAFE_SET_READHEAD(pFound + 3);

		while (pReadHead[0] == 'M')
		{
			char machineName[256];
			int iMegs;
			int iCpu;
			int iCpu60;
			LoadBalancingSingleMachineInfo *pMachineInfo;

			FIND(' ');
			pReadHead = pFound+1;
			FIND(':');
			*pFound = 0;
			if (pFound - pReadHead + 1 > sizeof(machineName))
			{
				*pFound = ':';
				LB_FAIL();
			}
			strcpy(machineName, pReadHead);
			*pFound = ':';
			pReadHead = pFound + 1;
			FIND(':');
			pReadHead = pFound + 1;
			iCpu = fastAtoi_Unsigned(pReadHead);
			FIND(',');
			pReadHead = pFound+1;
			iCpu60 = fastAtoi_Unsigned(pReadHead);
			FIND(':');
			pReadHead = pFound + 1;
			iMegs = fastAtoi_Unsigned(pReadHead);
			FIND('\n');
			pReadHead = pFound + 1;

			pMachineInfo = StructCreate(parse_LoadBalancingSingleMachineInfo);
			pMachineInfo->iCPU = iCpu;
			pMachineInfo->iCPU60 = iCpu60;
			pMachineInfo->iFreeMegs = iMegs;
			pMachineInfo->pMachineName = strdup(machineName);

			eaPush(&pLoadBalancingInfo->ppMachineInfo, pMachineInfo);
		}

		if (!strStartsWith(pReadHead, "Picked machine "))
		{
			LB_FAIL();
		}

		SAFE_SET_READHEAD(pReadHead + 15);
		pLoadBalancingInfo->pPickedMachine = strdup(pReadHead);

		pLog->pObjInfo = LogFileParser_GetEmptyObjInfo(pParser);
		pLog->pObjInfo->pAction = pPooledAction;
		pLog->pObjInfo->pLoadBalancing = pLoadBalancingInfo;
	}


}






//takes in a string of the form "LEV 13, POW Mutant" or something, and parses it out into name value pairs, and 
//inserts them into pObjInfo->ppProjSpecific
bool GetProjSpecificObjLogInfo(char *pString, ParsedLogObjInfo *pObjInfo)
{
	char *pFirstComma;

	do
	{
		char *pStart;
		ProjSpecificParsedLogObjInfo *pPair;

		pFirstComma = strchr_fast_nonconst(pString, ',');
		if (pFirstComma)
		{
			*pFirstComma = 0;
		}

		while (IS_WHITESPACE(*pString))
		{
			pString++;
		}

		if (!(*pString))
		{
			//might as well allow [] and [lev 5,]
			return true;
		}

		pStart = pString;
		pString++;

		while (!IS_WHITESPACE(*pString))
		{
			pString++;
		}

		pPair = StructCreate(parse_ProjSpecificParsedLogObjInfo);
		estrConcat(&pPair->pKey, pStart, pString - pStart);
		estrCopy2(&pPair->pVal, pString+1);
		estrTrimLeadingAndTrailingWhitespace(&pPair->pVal);
		if (estrLength(&pPair->pVal) == 0)
		{
			StructDestroy(parse_ProjSpecificParsedLogObjInfo, pPair);
			return false;
		}

		eaPush(&pObjInfo->ppProjSpecific, pPair);

		if (pFirstComma)
		{
			*pFirstComma = ',';
		}
		pString = pFirstComma + 1;
	} 
	while (pFirstComma);

	return true;

}

static void CountNoObjLogData(int iLineLength, const char *pLine)
{
	LogCountingData *pData = NULL;

	if(stashFindPointer(sActionLogCount, "NoObjLog", &pData))
	{
		pData->data += (U64)(iLineLength);
		pData->count += 1;
	}
	else
	{
		pData = calloc(1, sizeof(LogCountingData));
		pData->data = (U64)(iLineLength);
		pData->header = 0;
		pData->count = 1;
		stashAddPointer(sActionLogCount, "NoObjLog", pData, true);
	}
}

static void CountFailedLogData(int iLineLength, const char *pLine)
{
	LogCountingData *pData = NULL;

	if(stashFindPointer(sActionLogCount, "FailedLogParse", &pData))
	{
		pData->data += (U64)(iLineLength);
		pData->count += 1;
	}
	else
	{
		pData = calloc(1, sizeof(LogCountingData));
		pData->data = (U64)(iLineLength);
		pData->header = 0;
		pData->count = 1;
		stashAddPointer(sActionLogCount, "FailedLogParse", pData, true);
	}
}

static void CountLogData(char *pCurStart, char *pReadHead, int iLineLength, const char *pLine)
{
	static const char *pEventSourceString = NULL;
	static const char *pEventTargetString = NULL;
	static const char *pEventString = NULL;
	const char *pActionName = NULL;
	LogCountingData *pData = NULL;

	if(!pEventSourceString)
		pEventSourceString = allocAddString("EventSource");
	if(!pEventTargetString)
		pEventTargetString = allocAddString("EventTarget");
	if(!pEventString)
		pEventString = allocAddString("Event");

	pActionName = allocAddString(pCurStart);

	if(stashFindPointer(sActionLogCount, pActionName, &pData))
	{
		pData->data += (U64)(iLineLength - (U64)strlen(pLine));
		pData->header += (U64)strlen(pLine);
		pData->count += 1;
	}
	else
	{
		pData = calloc(1, sizeof(LogCountingData));
		pData->data = (U64)(iLineLength - (U64)strlen(pLine));
		pData->header = (U64)strlen(pLine);
		pData->count = 1;
		stashAddPointer(sActionLogCount, pActionName, pData, true);
	}

	if((pActionName == pEventSourceString) || (pActionName == pEventTargetString) || (pActionName == pEventString))
	{
		char *pStart;
		char *pEnd;
		// build string
		
		pStart = pReadHead + 2;
		pEnd = strchr_fast_nonconst(pStart, '\\');
		if(pEnd)
		{
			*pEnd = 0;
			if(stashFindPointer(sEventTypeLogCount, pStart, &pData))
			{
				pData->data += (U64)(iLineLength - (U64)strlen(pLine));
				pData->header += (U64)strlen(pLine);
				pData->count += 1;
			}
			else
			{
				pData = calloc(1, sizeof(LogCountingData));
				pData->data = (U64)(iLineLength - (U64)strlen(pLine));
				pData->header = (U64)strlen(pLine);
				pData->count = 1;
				stashAddPointer(sEventTypeLogCount, pStart, pData, true);
			}
			*pEnd = '\\';
		}

	}
}

#define FINDCHAR(c)							\
do											\
{											\
	if (!(*pReadHead))						\
		goto count_failed;						\
	if (*pReadHead == (c))					\
		break;								\
	pReadHead++;							\
}											\
while (1);									\


// Try to read a log as an activity.
// Return true if it worked.
// NOTE: Presently, this is only enabled when filtering by object ID.
static bool ReadLineIntoParsedLog_activity(ParsedLog *pLog, char *pLine, int iLineLength, 
									LogParsingRestrictions *pRestrictions, LogPostProcessingCB *pCB, enumLogParsingFlags eFlags,
									char *pReadHead, LogFileParser *pParser)
{
	const char beginning[] = "{\"privateaccountname\": \"";
	const char entity[] = "[{\"character_ent\": \"";
	const char *ptr;
	ContainerID id = 0;
	int result = 0;

	PERFINFO_AUTO_START_FUNC();

	// Return if activity parsing has been disabled.
	if (!gpEnableActivityParsing || !(pRestrictions->uObjID || pRestrictions->uPlayerID || pRestrictions->uAccountID))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	// If this does not seem to be an activity log, return.
	if (strncmp(beginning, pReadHead, sizeof(beginning) - 1))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Try to read the entity ID.
	ptr = pLine + sizeof(beginning);
	ptr = strstr(ptr, entity);
	if (ptr)
	{
		ptr += sizeof(entity) - 1;
		result = sscanf_s(ptr, "%lu", &id);
	}
	if (result != 1)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Build a mock ObjInfo structure for this.
	pLog->pObjInfo = LogFileParser_GetEmptyObjInfo(pParser);
	pLog->pObjInfo->eObjType = GLOBALTYPE_ENTITYPLAYER;
	pLog->pObjInfo->iObjID = id;
	pLog->pObjInfo->pAction = allocAddString("activity");

	PERFINFO_AUTO_STOP();
	return true;
}

#define LogParserReadString_Success 1
#define LogParserReadString_NoTerminator 0
#define LogParserReadString_Restricted -1

// Read a string from ppCurStart to the next occurrence of terminator and store it in dest. If pppEarrayToCheck is 
// not empty, abort if the string is not in the array.
// returns 1 on success.
// returns 0 if there is no terminator.
// returns -1 if pppEarrayToCheck is not empty and the string was not present.
static int LogParserReadString(char **ppCurStart, char **ppReadHead, char terminator, char ***pppEarrayToCheck, char **dest) 
{
	char oldReadHeadChar;
	*ppReadHead = strchr_fast_nonconst(*ppCurStart, terminator);

	if (!*ppReadHead)
		return LogParserReadString_NoTerminator;

	oldReadHeadChar = **ppReadHead;
	**ppReadHead=0;

	if (eaSize(pppEarrayToCheck))
	{
		if (eaFindString(pppEarrayToCheck, *ppCurStart) == -1)
			return LogParserReadString_Restricted;
	}

	estrCopy2(dest, *ppCurStart);
	**ppReadHead=oldReadHeadChar;

	*ppCurStart = *ppReadHead + 1;
	return LogParserReadString_Success;
}

// Read an int from ppCurStart to the next occurrence of terminator and store it in dest.
static bool LogParserReadInt(char **ppCurStart, char **ppReadHead, char terminator, int *dest) 
{
	bool bResult;
	char oldReadHeadChar;

	*ppReadHead = strchr_fast_nonconst(*ppCurStart, terminator);

	if (!*ppReadHead)
		return false;

	oldReadHeadChar = **ppReadHead;

	**ppReadHead = 0;
	bResult = StringToInt_Fast(*ppCurStart, dest);
	**ppReadHead = oldReadHeadChar;

	if (!bResult)
		return false;

	*ppCurStart = *ppReadHead + 1;
	return true;
}

static __forceinline U32 GetIDFromLogString(char *pLogString, int iLogLength)
{
	U32 iLogID;
	char *pReadHead;

	if (pLogString[0] == '/')
		return 0;

	if (iLogLength < DATE_WIDTH + LOGID_WIDTH)
		return 0;

	if (pLogString[DATE_WIDTH] != ' ')
		return 0;

	pReadHead = pLogString + DATE_WIDTH + LOGID_WIDTH - 1;

	while (isdigit(*pReadHead))
	{
		pReadHead++;
	}

	if ( *pReadHead != ' ' )
		return 0;

	*pReadHead = 0;
	iLogID = fastAtoi_Unsigned(pLogString + DATE_WIDTH);
	*pReadHead = ' ';

	return iLogID;
}

#define NO_OBJLOG_RETURN {																						\
if (bObjInfoRequired)																							\
{ estrDestroy(&pTempMessage); LogFileParser_RecycleObjInfo(pParser, &pObjInfo);  return false; }	\
estrCopy2(&pLog->pMessage, pTempMessage);																		\
estrDestroy(&pTempMessage);																						\
LogFileParser_RecycleObjInfo(pParser, &pObjInfo);																\
if (pLog->pMessage) CheckForStructsInMessage_NoObjLog(pLog, pLog->pMessage, pParser);									\
if (eFlags & LOGPARSINGFLAG_COPYRAWLOGLINE) {																	\
		pLog->pRawLogLine = malloc(iLineLength + 1);															\
		memcpy(pLog->pRawLogLine, pLine, iLineLength + 1); }													\
if (pCB) return pCB(pLog); return true;																			\
}																		

#define FAIL_RETURN { estrDestroy(&pTempMessage); LogFileParser_RecycleObjInfo(pParser, &pObjInfo); return false; }
//returns true on success. pLine is NULL-terminated
bool ReadLineIntoParsedLog_internal(ParsedLog *pLog, char *pLine, int iLineLength, 
	LogParsingRestrictions *pRestrictions, LogPostProcessingCB *pCB, enumLogParsingFlags eFlags, LogFileParser *pParser)
{
	bool bRestrictedActionName = false;
	char *pDupTest;
	bool bDup = false;
	bool bSuccess = false;
	char *pReadHead;
	char *pCurStart;
	bool bEscaping = false;
	char *pTempMessage;
	GlobalType eTempType;
	ParsedLogObjInfo *pObjInfo = NULL;
	bool bResult;
	ServerData *pServerData = NULL;

	bool bObjInfoRequired = stashGetCount(pRestrictions->sAction) 
		||eaSize(&pRestrictions->ppObjNames) 
		||eaSize(&pRestrictions->ppOwnerNames);

	PERFINFO_AUTO_START_FUNC();

	if (sbAlwaysFailToParseLogs)
	{
		goto count_failed;
	}

	if (pLine[0] == '/')
		goto count_failed;

	if (iLineLength < DATE_WIDTH + LOGID_WIDTH)
		goto count_failed;

	if (pLine[DATE_WIDTH] != ' ')
		goto count_failed;

	pLog->iTime = timeGetSecondsSince2000FromLogDateString(pLine);
	
	siMostRecentLogTimeRead = pLog->iTime;
	siTotalLogCount++;

	if (pRestrictions)
	{
		if (pRestrictions->iEndingTime && pLog->iTime > pRestrictions->iEndingTime)
			goto count_failed;

		if (pRestrictions->iStartingTime && pLog->iTime < pRestrictions->iStartingTime)
			goto count_failed;
	}


	pReadHead = pLine + DATE_WIDTH + LOGID_WIDTH - 1;

	while (isdigit(*pReadHead))
	{
		pReadHead++;
	}


	if ( *pReadHead != ' ' )
		goto count_failed;

	*pReadHead = 0;
	pLog->iLogID = fastAtoi_Unsigned(pLine + DATE_WIDTH);
	*pReadHead = ' ';

	if (!pLog->iLogID)
		goto count_failed;

	while (*pReadHead == ' ')
	{
		pReadHead++;
	}

	pCurStart = pReadHead;

	FINDCHAR('[');

	*pReadHead = 0;
	pLog->eServerType = ShortNameToGlobalType(pCurStart);
	*pReadHead = '[';

	if (pLog->eServerType == GLOBALTYPE_NONE)
		goto count_failed;

	pReadHead++;
	pCurStart = pReadHead;
	FINDCHAR(']');


	*pReadHead = 0;
	bSuccess = StringToInt_Fast(pCurStart, &pLog->iServerID);
	*pReadHead = ']';

	if (!bSuccess)
		goto count_failed;

	pReadHead++;

	// Get server ip, port, and map from stash table

	INIT_SERVER_ID_STRING(pLog);
	if(stashFindPointer(gServerDataStash, pLog->serverIdString, &pServerData))
	{
		pLog->iServerIP = pServerData->iServerIP;
		pLog->iServerPID = pServerData->iServerPID;
		if(pLog->eServerType == GLOBALTYPE_GAMESERVER)
		{
			estrCopy2(&pLog->pMapName, pServerData->pExtraInfo);
		}
		else
		{
			estrCopy2(&pLog->pExtraMapInfo, pServerData->pExtraInfo);
		}
	}

	if (eaSize(&pRestrictions->ppMapNames) && pLog->pMapName && pLog->pMapName[0])
	{
		if (eaFindString(&pRestrictions->ppMapNames, pLog->pMapName) == -1)
			goto count_failed;
	}

	pCurStart = pReadHead;
	pReadHead = strchr_fast_nonconst(pCurStart, ':');

	if(!pReadHead)
		goto count_failed;

	*pReadHead = 0;
	if (strstr(pCurStart, "ESC"))
	{
		bEscaping = true;
	}
	*pReadHead = ':';

	pReadHead+=2;

	// Check if this is an activity log, and if so, parse it separately.
	if (ReadLineIntoParsedLog_activity(pLog, pLine, iLineLength, pRestrictions, pCB, eFlags, pReadHead, pParser))
	{
		bool result = !pRestrictions->uObjID
			|| pLog->pObjInfo->iObjID == pRestrictions->uObjID || pLog->pObjInfo->iownerID == pRestrictions->uObjID;
		result &= !pRestrictions->uPlayerID || pLog->pObjInfo->iObjID == pRestrictions->uPlayerID;
		result &= !pRestrictions->uAccountID || pLog->pObjInfo->iownerID == pRestrictions->uAccountID;
		PERFINFO_AUTO_STOP();
		return result;
	}

	estrStackCreate(&pTempMessage);

	if (bEscaping)
	{
		estrAppendUnescaped(&pTempMessage, pReadHead);
	}
	else
	{
		estrCopy2(&pTempMessage, pReadHead);
	}

	// Check if the first character of TempMessage is ':'. If so, this was generated by servLog, and we should use the 
	// server type and id as the objInfo type and id.
	if(*pReadHead == ':')
	{
		eTempType = pLog->eServerType;		
	}
	else
	{
		//check if TempMessage begins with globalTypeName[. If it does, we assume it came from objLog and attempt to do further 
		//parsing.
		pCurStart = pReadHead;
		pReadHead = strchr_fast_nonconst(pCurStart, '[');
		if (!pReadHead)
			goto no_objlog_return;

		*pReadHead = 0;
		eTempType = ShortNameToGlobalType(pCurStart);
		*pReadHead = '[';
	}

	if (eTempType == GLOBALTYPE_NONE && strncmp(pCurStart, "InvalidType", 11) != 0)
		goto no_objlog_return;

	pObjInfo = LogFileParser_GetEmptyObjInfo(pParser);
	pObjInfo->eObjType = eTempType;

	pCurStart = pReadHead + 1;

	if(pObjInfo->eObjType == GLOBALTYPE_ENTITYCRITTER)
	{
		int retval;
		if(!LogParserReadInt(&pCurStart, &pReadHead, ' ', &pObjInfo->iObjID))
			goto no_objlog_return;

		retval = LogParserReadString(&pCurStart, &pReadHead, ']', &pRestrictions->ppObjNames, &pObjInfo->pObjName);
		if(retval == LogParserReadString_NoTerminator)
			goto no_objlog_return;
		else if(retval == LogParserReadString_Restricted)
			goto fail_return;

		if (eaSize(&pRestrictions->ppOwnerNames))
		{
			if (eaFindString(&pRestrictions->ppOwnerNames, "Critter") == -1)
				goto fail_return;
		}

		estrCopy2(&pObjInfo->pOwnerName, "Critter");

	}
	else
	{
		if(*pReadHead == ':')
		{
			if (eaSize(&pRestrictions->ppObjNames) || eaSize(&pRestrictions->ppOwnerNames))
				goto fail_return;

			pObjInfo->iObjID = pLog->iServerID;
			--pReadHead;
		}
		else
		{
			char *at = strchr_fast_nonconst(pCurStart, '@');
			char *space = strchr_fast_nonconst(pCurStart, ' ');
			char *rightbracket = strchr_fast_nonconst(pCurStart, ']');

			if(!rightbracket)
				goto no_objlog_return;

			if(at && space && at < space && space < rightbracket)
			{
				// [objid@ownerid objname@ownername]
				int retval;
				if (!LogParserReadInt(&pCurStart, &pReadHead, '@', &pObjInfo->iObjID))
					goto no_objlog_return;

				if (!LogParserReadInt(&pCurStart, &pReadHead, ' ', &pObjInfo->iownerID))
					goto no_objlog_return;

				retval = LogParserReadString(&pCurStart, &pReadHead, '@', &pRestrictions->ppObjNames, &pObjInfo->pObjName);
				if(retval == LogParserReadString_NoTerminator)
					goto no_objlog_return;
				else if(retval == LogParserReadString_Restricted)
					goto fail_return;

				retval = LogParserReadString(&pCurStart, &pReadHead, ']', &pRestrictions->ppOwnerNames, &pObjInfo->pOwnerName);
				if(retval == LogParserReadString_NoTerminator)
					goto no_objlog_return;
				else if(retval == LogParserReadString_Restricted)
					goto fail_return;
			}
			else if(at && space && space < at && at < rightbracket)
			{
				// [objid objname@ownername]
				int retval;
				if (!LogParserReadInt(&pCurStart, &pReadHead, ' ', &pObjInfo->iObjID))
					goto no_objlog_return;

				retval = LogParserReadString(&pCurStart, &pReadHead, '@', &pRestrictions->ppObjNames, &pObjInfo->pObjName);
				if(retval == LogParserReadString_NoTerminator)
					goto no_objlog_return;
				else if(retval == LogParserReadString_Restricted)
					goto fail_return;

				retval = LogParserReadString(&pCurStart, &pReadHead, ']', &pRestrictions->ppOwnerNames, &pObjInfo->pOwnerName);
				if(retval == LogParserReadString_NoTerminator)
					goto no_objlog_return;
				else if(retval == LogParserReadString_Restricted)
					goto fail_return;
			}
			else if(at && at < rightbracket)
			{
				// [objid@ownerid]
				if (eaSize(&pRestrictions->ppObjNames) || eaSize(&pRestrictions->ppOwnerNames))
					goto fail_return;

				if (!LogParserReadInt(&pCurStart, &pReadHead, '@', &pObjInfo->iObjID))
					goto no_objlog_return;

				if (!LogParserReadInt(&pCurStart, &pReadHead, ']', &pObjInfo->iownerID))
					goto no_objlog_return;
			}
			else if(space && space < rightbracket)
			{
				// [objid objname]
				int retval;
				if (eaSize(&pRestrictions->ppOwnerNames))
					goto fail_return;

				if (!LogParserReadInt(&pCurStart, &pReadHead, ' ', &pObjInfo->iObjID))
					goto no_objlog_return;

				retval = LogParserReadString(&pCurStart, &pReadHead, ']', &pRestrictions->ppObjNames, &pObjInfo->pObjName);
				if(retval == LogParserReadString_NoTerminator)
					goto no_objlog_return;
				else if(retval == LogParserReadString_Restricted)
					goto fail_return;
			}
			else
			{
				// [objid]
				if (eaSize(&pRestrictions->ppObjNames) || eaSize(&pRestrictions->ppOwnerNames))
					goto fail_return;
				if (!LogParserReadInt(&pCurStart, &pReadHead, ']', &pObjInfo->iObjID))
					goto no_objlog_return;

			}
		}
	}

	// Player and account filtering for web filter
	if(pRestrictions->uPlayerID || pRestrictions->uAccountID)
	{
		if(pObjInfo->eObjType == GLOBALTYPE_ENTITYPLAYER)
		{
			if((pRestrictions->uPlayerID && (pObjInfo->iObjID != pRestrictions->uPlayerID))
				|| (pRestrictions->uAccountID && (pObjInfo->iownerID != pRestrictions->uAccountID)))
			{
				PERFINFO_AUTO_STOP();
				if(gbLogCountingMode)
					CountNoObjLogData(iLineLength, pLine);
				NO_OBJLOG_RETURN;
			}
		}
		else if(pObjInfo->eObjType == GLOBALTYPE_ACCOUNT || pObjInfo->eObjType == GLOBALTYPE_CHATUSER)
		{
			if(pRestrictions->uAccountID && (pObjInfo->iObjID != pRestrictions->uAccountID))
			{
				PERFINFO_AUTO_STOP();
				if(gbLogCountingMode)
					CountNoObjLogData(iLineLength, pLine);
				NO_OBJLOG_RETURN;
			}
		}
		else
		{
			PERFINFO_AUTO_STOP();
			if(gbLogCountingMode)
				CountNoObjLogData(iLineLength, pLine);
			NO_OBJLOG_RETURN;
		}
	}
	else if (pRestrictions->uObjID && pObjInfo->iObjID != pRestrictions->uObjID && pObjInfo->iownerID != pRestrictions->uObjID)
	{
		// Filter on container ID if requested.
		PERFINFO_AUTO_STOP();
		if(gbLogCountingMode)
			CountNoObjLogData(iLineLength, pLine);
		NO_OBJLOG_RETURN;
	}

	++pReadHead;

	//proj-specific obj log info
	if (*pReadHead == '[')
	{
		pCurStart = pReadHead + 1;
		pReadHead = strchr_fast_nonconst(pCurStart, ']');

		if (!pReadHead)
			goto no_objlog_return;

		*pReadHead = 0;
		bResult = GetProjSpecificObjLogInfo(pCurStart, pObjInfo);
		*pReadHead = ']';

		if (!bResult)
			goto no_objlog_return;

		pReadHead++;
	}

	if (*pReadHead != ':')
		goto no_objlog_return;

	pCurStart = pReadHead + 2;
	pReadHead = strchr_fast_nonconst(pCurStart, '(');
	if (!pReadHead)
		goto no_objlog_return;

	*pReadHead = 0;

	if(gbLogCountingMode)
	{
		// Track total number of logs as well as total size.
		CountLogData(pCurStart, pReadHead, iLineLength, pLine);
		FAIL_RETURN;
	}

	pDupTest = pReadHead - 3;
	if(stricmp(pDupTest, "Dup") == 0)
	{
		*pDupTest = 0;
		bDup = true;
	}

	if (stashGetCount(pRestrictions->sAction))
	{
		SubActionList *ppSubList = NULL;
		if(stashFindPointer(pRestrictions->sAction, pCurStart, &ppSubList))
		{
			if(eaSize(&ppSubList->pList) > 0)
			{
				char *pStart;
				char *pEnd;
				// build string
					
				pStart = pReadHead + 2;
				pEnd = strchr_fast_nonconst(pStart, '\\');
				if(pEnd)
				{
					*pEnd = 0;
					if(eaFindString(&ppSubList->pList, pStart) == -1)
						bRestrictedActionName = true;
					*pEnd = '\\';
				}
			}
		}
		else
			bRestrictedActionName = true;
	}

	if(bRestrictedActionName)
	{
		if(stricmp(pCurStart, "ServerData") == 0 || sbAlwaysParseAllActions)
		{
			//force keep
			pObjInfo->bForceKept = true;
		}
		else
		{
			goto fail_return;
		}
	}

	pObjInfo->pAction = allocAddString(pCurStart);
	if(bDup)
		*pDupTest = 'D';

	*pReadHead = '(';

	pCurStart = pReadHead + 1;
	pReadHead = strrchr(pCurStart, ')');
	if (!pReadHead)
		goto no_objlog_return;

	*pReadHead = 0;
	estrCopy2(&pLog->pMessage, pCurStart);
	*pReadHead = ')';

	pLog->pObjInfo = pObjInfo;

	CheckForStructsInMessage(pLog, pLog->pMessage, bDup);

	INIT_SERVER_ID_STRING(pLog);
	CacheServerData(pLog, pServerData, pLog->serverIdString);

	estrDestroy(&pTempMessage);

	if (eFlags & LOGPARSINGFLAG_COPYRAWLOGLINE)
	{
		pLog->pRawLogLine = malloc(iLineLength + 1);
		memcpy(pLog->pRawLogLine, pLine, iLineLength + 1);
	}

	if (pCB)
	{
		PERFINFO_AUTO_STOP();
		return pCB(pLog);
	}

	PERFINFO_AUTO_STOP();
	return true;

count_failed:
	PERFINFO_AUTO_STOP();
	if(gbLogCountingMode)
		CountFailedLogData(iLineLength, pLine);
	return false;

no_objlog_return:
	PERFINFO_AUTO_STOP();
	if(gbLogCountingMode)
		CountNoObjLogData(iLineLength, pLine);
	NO_OBJLOG_RETURN;
fail_return:
	PERFINFO_AUTO_STOP();
	FAIL_RETURN;
}

// Check the text restrictions (substring and regex) on a log line.
static bool ReadLineIntoParsedLog_CheckTextRestrictions(const LogParsingRestrictions *pRestrictions, const char *pLine)
{
	bool any_matched;

	// Check restrictions in order of speed.

	// First check substring restrictions.
	any_matched = false;
	if (pRestrictions->ppSubstring)
	{
		EARRAY_CONST_FOREACH_BEGIN(pRestrictions->ppSubstring, i, n);
		{
			if (pRestrictions->ppSubstring[i] && strstri(pLine, pRestrictions->ppSubstring[i]))
			{
				any_matched = true;
				break;
			}
		}
		EARRAY_FOREACH_END;
		if (!any_matched)
			return false;
	}
	if (pRestrictions->ppSubstringInverse)
	{
		EARRAY_CONST_FOREACH_BEGIN(pRestrictions->ppSubstringInverse, i, n);
		{
			if (pRestrictions->ppSubstringInverse[i] && strstri(pLine, pRestrictions->ppSubstringInverse[i]))
				return false;
		}
		EARRAY_FOREACH_END;
	}
	any_matched = false;
	if (pRestrictions->ppSubstringCaseSensitive)
	{
		EARRAY_CONST_FOREACH_BEGIN(pRestrictions->ppSubstringCaseSensitive, i, n);
		{
			if (pRestrictions->ppSubstringCaseSensitive[i] && strstr(pLine, pRestrictions->ppSubstringCaseSensitive[i]))
			{
				any_matched = true;
				break;
			}
		}
		EARRAY_FOREACH_END;
		if (!any_matched)
			return false;
	}
	if (pRestrictions->ppSubstringCaseSensitiveInverse)
	{
		EARRAY_CONST_FOREACH_BEGIN(pRestrictions->ppSubstringCaseSensitiveInverse, i, n);
		{
			if (pRestrictions->ppSubstringCaseSensitiveInverse[i] && strstr(pLine, pRestrictions->ppSubstringCaseSensitiveInverse[i]))
				return false;
		}
		EARRAY_FOREACH_END;
	}
	// Then check regex restrictions.
	if (pRestrictions->pRegex && regexMatch_s(pRestrictions->pRegex, pLine, NULL, 0, NULL) < 0)
		return false;
	if (pRestrictions->pRegexInverse && regexMatch_s(pRestrictions->pRegexInverse, pLine, NULL, 0, NULL) >= 0)
		return false;

	return true;
}

bool ReadLineIntoParsedLog(ParsedLog *pLog, char *pLine, int iLineLength, 
	LogParsingRestrictions *pRestrictions, LogPostProcessingCB *pCB, enumLogParsingFlags eFlags, LogFileParser *pParser)
{
	PERFINFO_AUTO_START_FUNC();

	giTotalLogsScanned++;

	// Skip this log line if it fails to match text restrictions.
	if (!ReadLineIntoParsedLog_CheckTextRestrictions(pRestrictions, pLine))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (!ReadLineIntoParsedLog_internal(pLog, pLine, iLineLength, pRestrictions, pCB, eFlags, pParser))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	// If we're filtering on object ID, require the log to have one.
	if ((pRestrictions->uObjID || pRestrictions->uPlayerID || pRestrictions->uAccountID) && !pLog->pObjInfo)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (pRestrictions->pExpression && pRestrictions->pExpression[0])
	{
		static ExprContext *pContext = NULL;
		static Expression *pExpression = NULL;
		static char *pLastExprString = NULL;
		static ExprFuncTable* pFuncTable = NULL;
		static MultiVal answer = {0};
		int iAnswer;

		if (!pContext)
		{
			pContext = exprContextCreate();
			pFuncTable = exprContextCreateFunctionTable("LogParsing");
			exprContextAddFuncsToTableByTag(pFuncTable, "util");
			exprContextSetFuncTable(pContext, pFuncTable);
			exprContextSetSilentErrors(pContext, true);
		}

		exprContextSetPointerVar(pContext, "me", pLog, parse_ParsedLog, false, true);

		if (!pExpression || stricmp(pLastExprString, pRestrictions->pExpression) != 0)
		{
			if (pExpression)
			{
				exprDestroy(pExpression);
			}
			pExpression = exprCreate();
			
			if (!exprGenerateFromString(pExpression, pContext, pRestrictions->pExpression, NULL))
			{
				Errorf("Couldn't generate expression %s", pRestrictions->pExpression);
				exprDestroy(pExpression);
				pExpression = NULL;
				PERFINFO_AUTO_STOP();
				return false;

			}
			else
			{


				if (pLastExprString)
				{
					free(pLastExprString);
				}
				pLastExprString = strdup(pRestrictions->pExpression);
			}
		}

		exprEvaluate(pExpression, pContext, &answer);

		iAnswer = QuickGetIntSafe(&answer);

		if (!iAnswer)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	PERFINFO_AUTO_STOP();
	return true;
}




void ParseLogLines(ParsedLog ***pppOutLogs, char *pInBuf, LogParsingRestrictions *pRestrictions, 
	LogPostProcessingCB *pCB, enumLogParsingFlags eFlags)
{
	char *pReadHead = pInBuf;
	ParsedLog *pCurLog = StructCreate(parse_ParsedLog);
	char *pEOL;
	int iLen;

	do
	{
		pEOL = strchr_fast_nonconst(pReadHead, '\n');

		if (!pEOL)
		{
			break;
		}

		*pEOL = 0;
		if (ReadLineIntoParsedLog(pCurLog, pReadHead, pEOL - pReadHead, pRestrictions, pCB, eFlags, NULL))
		{
			eaPush(pppOutLogs, pCurLog);
			pCurLog = StructCreate(parse_ParsedLog);
		}
		else
		{
			StructReset(parse_ParsedLog, pCurLog);
		}

		*pEOL = ' ';
		pReadHead = pEOL + 1;
	}
	while (1);

	iLen = (int)strlen(pReadHead);
	if (ReadLineIntoParsedLog(pCurLog, pReadHead, iLen, pRestrictions, pCB, eFlags, NULL))
	{
		eaPush(pppOutLogs, pCurLog);
	}
	else
	{
		PreParsedLogRemoveRef(pCurLog);
		StructDestroy(parse_ParsedLog, pCurLog);
	}
}

	
void ParseLogFile(ParsedLog ***pppOutLogs, char *pFileName, LogParsingRestrictions *pRestrictions, 
	LogPostProcessingCB *pCB, enumLogParsingFlags eFlags)
{
	FILE *pFile = fopen(pFileName, "rt");
	int iSize;
	char *pBuffer;
	size_t iBytesRead;

	if (!pFile)
	{
		return;
	}

	iSize = fileGetSize(pFile);

	if (!iSize)
	{
		fclose(pFile);
		return;
	}

	pBuffer = malloc(iSize + 1);
	iBytesRead = fread(pBuffer, 1, iSize, pFile);
	fclose(pFile);
	pBuffer[iBytesRead] = 0;

	ParseLogLines(pppOutLogs, pBuffer, pRestrictions, pCB, eFlags);

	free(pBuffer);
}





	
//log filenames currently come in two varieties:
//CONTROLLER.Log_2008-07-17_22-00-00.Gz
//CONTROLLER_2008-06-26_16-00-00.Log

// The expected directory structures are
//<ServerType>/<log>
//<ServerType>/<year>/<month>/<day>/<log>

#define LOGPARSING_DATE_SIZE 64
#define LOGPARSING_KEY_SIZE 64

__forceinline static const char *FindPreviousSlash(const char *searchStart, const char *stringStart)
{
	const char *pTemp = searchStart;
	if (*pTemp == '/' || *pTemp == '\\')
	{
		pTemp--;

		while (pTemp > stringStart && (*pTemp != '/' && *pTemp != '\\'))
		{
			pTemp--;
		}
	}

	return pTemp;
}

static bool ParseKeyString(const char *pFileName, const char *pFileTypeEnd, char key[LOGPARSING_KEY_SIZE])
{
	const char *pFileTypeStart = pFileTypeEnd - 1;
	const char *pServerTypeStart, *pServerTypeEnd;
	const char *pTemp, *pOld;

	pTemp = pFileTypeStart;

	if(pTemp <= pFileName)
		return false;

	while (pTemp > pFileName && (*pTemp != '/' && *pTemp != '\\'))
	{
		pTemp--;
	}

	if(*pTemp == '/' || *pTemp == '\\')
		pFileTypeStart = pTemp + 1;

	pOld = pTemp;
	//we want to include the servertype, which might be the next directory, or it might be 4 up.
	pTemp = FindPreviousSlash(pTemp, pFileName);

	if(pTemp == pOld)
	{
		// We've hit the beginning of pFileName
		pServerTypeStart = "UNKNOWN";
		pServerTypeEnd = pServerTypeStart + 7;
	}
	else
	{
		pServerTypeStart = pTemp + 1;
		pServerTypeEnd = pOld;
	}

	if(pOld - pTemp <= 3) // 2 digit number + /
	{
		pOld = pTemp;
		pTemp = FindPreviousSlash(pTemp, pFileName);

		if(pTemp > pFileName && pOld - pTemp == 4) // 3 character string + /
		{
			pOld = pTemp;
			pTemp = FindPreviousSlash(pTemp, pFileName);

			if(pTemp > pFileName && pOld - pTemp == 5) // 4 digit year + /
			{
				pOld = pTemp;
				pTemp = FindPreviousSlash(pTemp, pFileName);

				if(pTemp > pFileName)
				{
					pServerTypeStart = pTemp + 1;
					pServerTypeEnd = pOld;
				}
			}
		}
	}

	strncpy_s(key, LOGPARSING_KEY_SIZE, pServerTypeStart, pServerTypeEnd - pServerTypeStart + 1);
	strncpy_s(key + (pServerTypeEnd - pServerTypeStart + 1), LOGPARSING_KEY_SIZE - (pServerTypeEnd - pServerTypeStart + 1), pFileTypeStart, pFileTypeEnd - pFileTypeStart);
	return true;
}

static void GetDateAndKeyStringsFromLogFilename(const char *pFileName, char date[LOGPARSING_DATE_SIZE], char key[LOGPARSING_KEY_SIZE])
{
	char *pTemp = strstri(pFileName, ".Log_20");

	if (pTemp)
	{
		strncpy_s(date, LOGPARSING_DATE_SIZE, pTemp + 5, 19);

		if(!ParseKeyString(pFileName, pTemp, key))
			snprintf_s(key, LOGPARSING_KEY_SIZE, "UNKNOWN");
	}
	else
	{
		int iLen = (int)strlen(pFileName);

		if (iLen < 24 || pFileName[iLen - 24] != '_')
		{
			date[0] = 0;
			snprintf_s(key, LOGPARSING_KEY_SIZE, "UNKNOWN");
			return;
		}

		strncpy_s(date, LOGPARSING_DATE_SIZE, pFileName + iLen - 23, 19);

		if(!ParseKeyString(pFileName, pFileName + iLen - 24, key))
			snprintf_s(key, LOGPARSING_KEY_SIZE, "UNKNOWN");
	}
}

U32 GetTimeFromLogFilename(const char *pFileName)
{
	char date[64];
	char key[CRYPTIC_MAX_PATH];


	GetDateAndKeyStringsFromLogFilename(pFileName, date, key);

	return timeGetSecondsSince2000FromDateString(date);
}

//returns a pooled string
const char *GetKeyFromLogFilename(const char *pFileName)
{
	char date[64];
	char key[CRYPTIC_MAX_PATH];


	GetDateAndKeyStringsFromLogFilename(pFileName, date, key);

	return allocAddString(key);
}











LogFileParser *LogFileParser_Create(LogParsingRestrictions *pRestrictions, LogPostProcessingCB *pCB, enumLogParsingFlags eFlags)
{	
	LogFileParser *pParser = (LogFileParser*)calloc(sizeof(LogFileParser), 1);
	pParser->pRestrictions = pRestrictions;
	pParser->pCB = pCB;
	pParser->eFlags = eFlags;
	pParser->iBufferSizeNotCountingTerminator = LOGFILEPARSER_STARTING_BUFFER_SIZE;
	pParser->pBuffer = malloc(pParser->iBufferSizeNotCountingTerminator + 1);

	return pParser;
}


void LogFileParser_Reset(LogFileParser *pParser)
{
	if (pParser->pFile)
	{
		fclose(pParser->pFile);
		pParser->pFile = NULL;
	}

	if (pParser->pBufferRemainder)
	{
		free(pParser->pBufferRemainder);
		pParser->pBufferRemainder = NULL;
	}

	estrClear(&pParser->pLogStringsToReturn[0]);
	estrClear(&pParser->pLogStringsToReturn[1]);

	pParser->iNextLogStringToReturnToggler = 0;

	pParser->pReadHead = pParser->pCurEndOfBuffer = NULL;
	pParser->iBufferRemainderSize = 0;
	pParser->iTotalBytesRemaining = 0;
	pParser->iNextLogTime = 0;
	pParser->iNextLogID = 0;
}

bool LogFileParser_GetNextLogStringInternal(LogFileParser *pParser)
{
	PERFINFO_AUTO_START_FUNC();

	while (1)
	{
		int iLineLength;
		char *pNextNewLine;
		char *pOldReadHead = pParser->pReadHead;
		U32 iBytesBeingRead;

		char **ppNextLogString = &pParser->pLogStringsToReturn[pParser->iNextLogStringToReturnToggler];

		estrClear(ppNextLogString);
		pParser->iNextLogTime = 0;
		pParser->iNextLogID = 0;
	

		if (pParser->pCurEndOfBuffer - pParser->pReadHead < 10)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

		pNextNewLine = strchr_fast_nonconst(pParser->pReadHead, '\n');

		if (pNextNewLine)
		{
			iLineLength = pNextNewLine - pParser->pReadHead;
			*pNextNewLine = 0;
			pParser->pReadHead = pNextNewLine + 1;
		}
		else
		{
			iLineLength = pParser->pCurEndOfBuffer - pParser->pReadHead;
			pParser->pReadHead = pParser->pCurEndOfBuffer;
		}


		iBytesBeingRead = pParser->pReadHead - pOldReadHead;
		if (pParser->iTotalBytesRemaining > iBytesBeingRead)
		{
			pParser->iTotalBytesRemaining -= iBytesBeingRead;
		}
		else
		{
			pParser->iTotalBytesRemaining = 0;
		}

		PERFINFO_AUTO_START("estrCopy2", 1);
		estrCopy2(ppNextLogString, pOldReadHead);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Get log time", 1);
		pParser->iNextLogTime = timeGetSecondsSince2000FromLogDateString((*ppNextLogString));
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Get log id", 1);
		pParser->iNextLogID = GetIDFromLogString((*ppNextLogString), iLineLength);
		PERFINFO_AUTO_STOP();

		//if we were previously parsing a super-huge log, our line may start in the middle of it and thus not
		//have a parsable date. If so, don't treat this line as an actual log line
		if (pParser->iNextLogTime != 0)
		{
			PERFINFO_AUTO_STOP();
			return true;
		}
	}
}



void LogFileParser_ReadFromFileAndGetNextLogString(LogFileParser *pParser)
{
	size_t iBytesToRead, iBytesActuallyRead;

	PERFINFO_AUTO_START_FUNC();
	
	//loop on the off chance that we read in a megabyte of log file data and none of it is parsable
	while (pParser->pFile)
	{
		int iRemainderSize = pParser->iBufferRemainderSize;

		estrClear(&pParser->pLogStringsToReturn[pParser->iNextLogStringToReturnToggler]);
		pParser->iNextLogTime = 0;
		pParser->iNextLogID = 0;
		

		if (iRemainderSize)
		{
			memcpy(pParser->pBuffer, pParser->pBufferRemainder, iRemainderSize);
			free(pParser->pBufferRemainder);
			pParser->pBufferRemainder = NULL;
			pParser->iBufferRemainderSize = 0;
		}

		iBytesToRead = pParser->iBufferSizeNotCountingTerminator - iRemainderSize;
		
		PERFINFO_AUTO_START("fread", 1);
		iBytesActuallyRead = fread(pParser->pBuffer + iRemainderSize, 1, iBytesToRead, pParser->pFile);
		PERFINFO_AUTO_STOP();

		pParser->pReadHead = pParser->pBuffer;

		if (iBytesActuallyRead < iBytesToRead)
		{
			pParser->pCurEndOfBuffer = pParser->pBuffer + iRemainderSize + iBytesActuallyRead;
			*(pParser->pCurEndOfBuffer) = 0;
			fclose(pParser->pFile);
			pParser->pFile = NULL;
		}
		else
		{
			char *pTemp;
			pParser->pCurEndOfBuffer = pParser->pBuffer + pParser->iBufferSizeNotCountingTerminator;
			*(pParser->pCurEndOfBuffer) = 0;

			pTemp = pParser->pCurEndOfBuffer - 1;
			while (*pTemp != '\n' && pTemp > pParser->pBuffer)
			{
				pTemp--;
			}

			//our entire loaded buffer has no newlines... ignore an oversized log.
			if (pTemp <= pParser->pBuffer)
			{
				//need to resize our buffer... so we pull a neat trick and and shove our current buffer into the remainder buffer
				pParser->pBufferRemainder = pParser->pBuffer;
				pParser->iBufferRemainderSize = pParser->iBufferSizeNotCountingTerminator;

				pParser->iBufferSizeNotCountingTerminator *= 2;
				pParser->pBuffer = malloc(pParser->iBufferSizeNotCountingTerminator + 1);
				*pParser->pBuffer = 0;
				pParser->pCurEndOfBuffer = pParser->pBuffer;

				continue;
			}
			else
			{
				*pTemp = 0;
				pParser->pCurEndOfBuffer = pTemp;
				pParser->iBufferRemainderSize = pParser->iBufferSizeNotCountingTerminator - (pParser->pCurEndOfBuffer - pParser->pBuffer) - 1;
				pParser->pBufferRemainder = malloc(pParser->iBufferRemainderSize);
				memcpy(pParser->pBufferRemainder, pTemp + 1, pParser->iBufferRemainderSize);
			}
		}

		if (LogFileParser_GetNextLogStringInternal(pParser))
		{
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	PERFINFO_AUTO_STOP();
}

bool LogFileParser_OpenFile(LogFileParser *pParser, const char *pFileName)
{
	if (gbVerboseLogParsing)
	{
		printf("Attempting to open %s\n", pFileName);
	}

	strcpy(pParser->fileName, pFileName);
	LogFileParser_Reset(pParser);
	pParser->pFile = fopen(pFileName, strEndsWith(pFileName, ".gz") ? "rbz" : "rb");

	if (!pParser->pFile)
	{
		return false;
	}

	pParser->iTotalBytesRemaining = fileSize(pFileName);
	if (strEndsWith(pFileName, ".gz"))
	{
		pParser->iTotalBytesRemaining *= ZIP_RATIO_APPROX;
	}


	LogFileParser_ReadFromFileAndGetNextLogString(pParser);

	if (!estrLength(&pParser->pLogStringsToReturn[pParser->iNextLogStringToReturnToggler]))
	{
		LogFileParser_Reset(pParser);
		return false;
	}

	return true;
}

/*
U32 LogFileParser_GetTimeOfNextLog(LogFileParser *pParser)
{
	return pParser->iNextLogTime;
}
*/

char *LogFileParser_GetNextLogString(LogFileParser *pParser)
{
	char *pRetVal;
	pRetVal = pParser->pLogStringsToReturn[pParser->iNextLogStringToReturnToggler];

	if (!estrLength(&pRetVal))
	{
		return NULL;
	}

	pParser->iNextLogStringToReturnToggler = !pParser->iNextLogStringToReturnToggler;

	if (!LogFileParser_GetNextLogStringInternal(pParser))
	{
		LogFileParser_ReadFromFileAndGetNextLogString(pParser);
	}

	return pRetVal;
}

void LogFileParser_Destroy(LogFileParser *pParser)
{
	LogFileParser_Reset(pParser);
	estrDestroy(&pParser->pLogStringsToReturn[0]);
	estrDestroy(&pParser->pLogStringsToReturn[1]);

	StructDestroySafe(parse_ParsedLog, &pParser->pDeInittedLog);
	
	free(pParser->pBuffer);
	free(pParser);
	
}

U32 LogFileParser_GetBytesRemaining(LogFileParser *pParser)
{
	return pParser->iTotalBytesRemaining;
}

ParsedLog *CreateLogFromString(char *pLogString, LogFileParser *pParser)
{
	ParsedLog *pLog;

	if (pParser->pDeInittedLog)
	{
		pLog = pParser->pDeInittedLog;
		pParser->pDeInittedLog = NULL;
	}
	else
	{
		pLog = StructCreate(parse_ParsedLog);
	}

	if(ReadLineIntoParsedLog(pLog, pLogString, estrLength(&pLogString), pParser->pRestrictions, pParser->pCB, pParser->eFlags, pParser))
		return pLog;
	else
	{
		PreParsedLogRemoveRef(pLog);

		if (pLog->pObjInfo)
		{
			LogFileParser_RecycleObjInfoEx(pParser, &pLog->pObjInfo);
		}

		StructDeInit(parse_ParsedLog, pLog);

		//not generally safe for all struct types
		memset(pLog, 0, sizeof(ParsedLog));
		eaIndexedEnable(&pLog->ppPairs, parse_NameValuePair);


		pParser->pDeInittedLog = pLog;
		return NULL;
	}
	
}



MP_DEFINE(ParsedLog);
MP_DEFINE(ParsedLogObjInfo);

AUTO_RUN;
void CreateLogParsingMemPools(void)
{
	MP_CREATE(ParsedLog, 2048);
	MP_CREATE(ParsedLogObjInfo, 2048);
}



ParsedLogObjInfo *LogFileParser_GetEmptyObjInfoEx(LogFileParser *pParser)
{
	ParsedLogObjInfo *pRetVal;

	if (pParser->pDoInittedObjInfo)
	{
		pRetVal = pParser->pDoInittedObjInfo;
		pParser->pDoInittedObjInfo = NULL;
		return pRetVal;
	}
	else
	{
		return StructCreate(parse_ParsedLogObjInfo);
	}

}
void LogFileParser_RecycleObjInfoEx(LogFileParser *pParser, ParsedLogObjInfo **ppObjInfoHandle)
{
	ParsedLogObjInfo *pObjInfo;

	if (!ppObjInfoHandle || !(*ppObjInfoHandle))
	{
		return;
	}

	pObjInfo = *ppObjInfoHandle;
	*ppObjInfoHandle = NULL;

	if (pParser->pDoInittedObjInfo)
	{
		StructDestroy(parse_ParsedLogObjInfo, pObjInfo);
	}
	else
	{
		StructDeInit(parse_ParsedLogObjInfo, pObjInfo);
		
		//NOT GENERALLY SAFE TO DO THIS
		memset(pObjInfo, 0, sizeof(ParsedLogObjInfo));
		eaIndexedEnable(&pObjInfo->ppProjSpecific, parse_ProjSpecificParsedLogObjInfo);

		pParser->pDoInittedObjInfo = pObjInfo;
	}
}

bool SubdivideLoggingKey(const char *pKey, GlobalType *pOutServerType, enumLogCategory *pOutCategory)
{
	char temp[64];
	char *pFirstSlash = strchr(pKey, '/');
	GlobalType outServerType;
	enumLogCategory outCategory;
	
	if (!pFirstSlash)
	{
		pFirstSlash = strchr(pKey, '\\');
	}

	if (!pFirstSlash)
	{
		return false;
	}

	outCategory = StaticDefineInt_FastStringToInt(enumLogCategoryEnum, pFirstSlash + 1, -1);
	if (outCategory == -1)
	{
		return false;
	}

	if (pFirstSlash - pKey > sizeof(temp) - 1)
	{
		return false;
	}

	memcpy(temp, pKey, pFirstSlash - pKey);
	temp[pFirstSlash - pKey] = 0;

	outServerType = NameToGlobalType(temp);

	if (!outServerType)
	{
		return false;
	}

	if (pOutServerType)
	{
		*pOutServerType = outServerType;
	}

	if (pOutCategory)
	{
		*pOutCategory = outCategory;
	}

	return true;
}





#include "LogParsing_h_ast.c"
