#include "etTrivia.h"

#include "Alerts.h"
#include "earray.h"
#include "error.h"
#include "ErrorTracker.h"
#include "EString.h"
#include "ETCommon\ETCommonStructs.h"
#include "ETCommon\ETShared.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GlobalTypes.h"
#include "objContainer.h"
#include "qsortG.h"
#include "logging.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "textparser.h"
#include "timing.h"
#include "timing_profiler.h"
#include "timing_profiler_interface.h"
#include "trivia.h"
#include "zutils.h"

#include "AutoGen/trivia_h_ast.h"
#include "AutoGen/etTrivia_c_ast.h"

extern ErrorTrackerSettings gErrorTrackerSettings;

// -------------------------------------------------------------------------------------
// Filters

typedef void (*TriviaProcessCB) (NOCONST(TriviaData) *data);
AUTO_STRUCT;
typedef struct TriviaSpecialProcessing
{
	char *key; AST(KEY ESTRING)
	TriviaProcessCB cb; NO_AST
} TriviaSpecialProcessing;

AUTO_STRUCT;
typedef struct TriviaAggregateValue
{
	int min;
	int max;
} TriviaAggregateValue;

AUTO_ENUM;
typedef enum TriviaAggregateType
{
	AGGREGATE_INTEGER = 0,
	AGGREGATE_FLOAT,
} TriviaAggregateType;

AUTO_STRUCT;
typedef struct TriviaAggregate
{
	char *key; AST(KEY ESTRING)

	EARRAY_OF(TriviaAggregateValue) eaAggregates;
	TriviaAggregateType eType;
	char *readFormat;  // Input format: should use "%f" for reading in floats, "%d" for ints. Only one float value should be saved.
	char *writeFormat; // Output format: should use "%f" if floats, "%d" if ints

	// Global min/max
	int min;
	char *globalMinFormat;
	int max;
	char *globalMaxFormat;
} TriviaAggregate;

AUTO_STRUCT;
typedef struct TriviaFilterList
{
	// Keys to exclude (case-insensitive, whitespace sensitive)
	STRING_EARRAY eaFilteredKeys; AST(ESTRING)

	// Keys to exclude for Trivia Overviews
	STRING_EARRAY eaOverviewFilteredKeys; AST(ESTRING)

	// Keys to aggregate for Trivia Overviews (numerics only)
	EARRAY_OF(TriviaAggregate) eaOverviewAggregates;
} TriviaFilterList;

static EARRAY_OF(TriviaSpecialProcessing) seaTriviaSpecial = NULL;

static void TriviaCleanupCommandLine(NOCONST(TriviaData) *data)
{
	const char *exeName;
	char truncatedName[256];
	
	if (nullStr(data->pVal))
		return;
	exeName = errorExecutableGetName(data->pVal);
	if (exeName != data->pVal)
	{
		if (strlen(exeName) < ARRAY_SIZE(truncatedName))
			strcpy(truncatedName, exeName);
		else
		{
			strncpy(truncatedName, exeName, ARRAY_SIZE(truncatedName)-1);
			truncatedName[255] = 0;
		}
		estrCopy2(&data->pVal, truncatedName);
	}
}

static void TriviaProcessOtherTrivia(NOCONST(TriviaData) *data)
{
	// TODO process stupid proxy shit
}

static TriviaFilterList sTriviaFilters = {0};

#define TRIVIA_FILTER_FILEPATH "server/ErrorTracker/TriviaFilters.txt"
static void triviaFiltersReloadCallback(const char *relpath, int when)
{
	loadstart_printf("Reloading Trivia Filters...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	if (!fileExists(relpath))
	{
		// File was deleted, do we care here?
	}

	StructDeInit(parse_TriviaFilterList, &sTriviaFilters);
	ParserReadTextFile(TRIVIA_FILTER_FILEPATH, parse_TriviaFilterList, &sTriviaFilters, 0);
	eaQSort(sTriviaFilters.eaFilteredKeys, strCmp);
	eaQSort(sTriviaFilters.eaOverviewFilteredKeys, strCmp);
	loadend_printf("done");
}

void InitializeTriviaFilter(void)
{
	TriviaSpecialProcessing *process;
	if (fileExists(TRIVIA_FILTER_FILEPATH))
	{
		ParserReadTextFile(TRIVIA_FILTER_FILEPATH, parse_TriviaFilterList, &sTriviaFilters, 0);
		eaQSort(sTriviaFilters.eaFilteredKeys, strCmp);
		eaQSort(sTriviaFilters.eaOverviewFilteredKeys, strCmp);
	}
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, TRIVIA_FILTER_FILEPATH, triviaFiltersReloadCallback);

	eaIndexedEnable(&seaTriviaSpecial, parse_TriviaSpecialProcessing);
	process = StructCreate(parse_TriviaSpecialProcessing);
	estrCopy2(&process->key, "CommandLine");
	process->cb = TriviaCleanupCommandLine;
	eaIndexedAdd(&seaTriviaSpecial, process);

	process = StructCreate(parse_TriviaSpecialProcessing);
	estrCopy2(&process->key, "SystemSpecs:Trivia");
	process->cb = TriviaProcessOtherTrivia;
	eaIndexedAdd(&seaTriviaSpecial, process);
}

// Filters incoming Trivia Data for keys that should be tossed
void FilterIncomingTriviaData(TriviaData ***eaTriviaData)
{
	int i;
	TriviaSpecialProcessing *special;
	PERFINFO_AUTO_START_FUNC();
	if (eaSize(&sTriviaFilters.eaFilteredKeys) > 0)
	{
		for (i=eaSize(eaTriviaData)-1; i>=0; i--)
		{
			if ((*eaTriviaData)[i]->pKey && eaBSearch(sTriviaFilters.eaFilteredKeys, strCmp, (*eaTriviaData)[i]->pKey))
			{
				StructDestroy(parse_TriviaData, (*eaTriviaData)[i]);
				eaRemove(eaTriviaData, i);
			}
			else if (special = eaIndexedGetUsingString(&seaTriviaSpecial, (*eaTriviaData)[i]->pKey))
			{
				special->cb(CONTAINER_NOCONST(TriviaData, (*eaTriviaData)[i]));
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

static void getAggregateValue(char **estr, TriviaAggregate *aggregate, const char *value)
{
	if (aggregate->eType == AGGREGATE_FLOAT)
	{
		float val;
		sscanf(value, aggregate->readFormat, &val);
		if (val < (float) aggregate->min)
			estrPrintf(estr, FORMAT_OK(aggregate->globalMinFormat), aggregate->min);
		else if (val >= (float) aggregate->max)
			estrPrintf(estr, FORMAT_OK(aggregate->globalMaxFormat), aggregate->max);
		else
		{
			EARRAY_CONST_FOREACH_BEGIN(aggregate->eaAggregates, i, s);
			{
				TriviaAggregateValue *agval = aggregate->eaAggregates[i];
				if ((float) agval->min <= val && val < (float) agval->max)
				{
					estrPrintf(estr, FORMAT_OK(aggregate->writeFormat), agval->min, agval->max);
					break;
				}
			}
			EARRAY_FOREACH_END;
		}
	}
	else
	{
		int val;
		sscanf(value, aggregate->readFormat, &val);
		if (val < aggregate->min)
			estrPrintf(estr, FORMAT_OK(aggregate->globalMinFormat), aggregate->min);
		else if (val >= aggregate->max)
			estrPrintf(estr, FORMAT_OK(aggregate->globalMaxFormat), aggregate->max);
		else
		{
			EARRAY_CONST_FOREACH_BEGIN(aggregate->eaAggregates, i, s);
			{
				TriviaAggregateValue *agval = aggregate->eaAggregates[i];
				if (agval->min <= val && val < agval->max)
				{
					estrPrintf(estr, FORMAT_OK(aggregate->writeFormat), agval->min, agval->max);
					break;
				}
			}
			EARRAY_FOREACH_END;
		}
	}
}

void FilterForTriviaOverview(TriviaOverview *overview, CONST_EARRAY_OF(TriviaData) eaTrivia)
{
	NOCONST(TriviaOverview) *overviewNoConst = CONTAINER_NOCONST(TriviaOverview, overview);
	PERFINFO_AUTO_START_FUNC();
	if (eaSize(&sTriviaFilters.eaOverviewFilteredKeys) > 0 || eaSize(&sTriviaFilters.eaOverviewAggregates) > 0)
	{
		EARRAY_CONST_FOREACH_BEGIN(eaTrivia, i, s);
		{
			TriviaData *trivia = eaTrivia[i];
			if (trivia->pKey)
			{
				TriviaAggregate *aggregate = NULL;
				if (eaBSearch(sTriviaFilters.eaOverviewFilteredKeys, strCmp, trivia->pKey))
					continue;
				if (aggregate = eaIndexedGetUsingString(&sTriviaFilters.eaOverviewAggregates, trivia->pKey))
				{
					char *value = NULL;
					getAggregateValue(&value, aggregate, trivia->pVal);
					if (value)
						triviaOverviewAddValue(overviewNoConst, trivia->pKey, value);
				}
				else
					triviaOverviewAddValue(overviewNoConst, trivia->pKey, trivia->pVal);
			}
		}
		EARRAY_FOREACH_END;
	}
	else
	{
		triviaMergeOverview(overviewNoConst, eaTrivia, true);
	}
	PERFINFO_AUTO_STOP();
}

// TODO This is incomplete and intended for use in updating existing triviadata.logs for the filters
/*void FilterTriviaLog(ErrorEntry *pEntry)
{
	TriviaOverview overview = {0};
	char triviaFilename[MAX_PATH];

	GetTriviaDataFileName(pEntry->uID, triviaFilename, ARRAY_SIZE_CHECKED(triviaFilename));
	ReadTriviaData(pEntry->uID, &overview);
}*/

// -------------------------------------------------------------------------------------
// File I/O

static U32 suTriviaRequestsOutstanding = 0;
AUTO_COMMAND;
void TriviaDataPrintOutstanding(void)
{
	printf("Trivia Outstanding Writes: %u\n", suTriviaRequestsOutstanding);
}

static bool PopulateTriviaString(CONST_EARRAY_OF(TriviaData) ppTriviaData, char **triviastring)
{
	int size = eaSize(&ppTriviaData);
	if (size == 0)
		return false;
	PERFINFO_AUTO_START_FUNC();
	if (!*triviastring)
		estrStackCreate(triviastring);
	EARRAY_FOREACH_BEGIN(ppTriviaData, i);
	{
		estrAppend2(triviastring, "\"");
		estrAppendEscapedf(triviastring, "%s", ppTriviaData[i]->pKey);
		estrAppend2(triviastring, "\" \"");
		estrAppendEscapedf(triviastring, "%s", ppTriviaData[i]->pVal);
		estrAppend2(triviastring, "\"");
		if(i < size - 1)
			estrAppend2(triviastring, ", ");
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
	return true;
}

static bool PopulateTriviaOverviewString(ErrorEntry *pEntry, char **triviastring)
{
	int itemListSize;
	if(!pEntry->triviaOverview.ppTriviaItems || !eaSize(&pEntry->triviaOverview.ppTriviaItems))
		return false;

	PERFINFO_AUTO_START_FUNC();

	itemListSize = eaSize(&pEntry->triviaOverview.ppTriviaItems);
	EARRAY_FOREACH_BEGIN(pEntry->triviaOverview.ppTriviaItems, i);
	{
		TriviaOverviewItem *item = pEntry->triviaOverview.ppTriviaItems[i];
		int itemValueSize = eaSize(&item->ppValues);
		estrConcatf(triviastring, "\"%s\"{", item->pKey);
		EARRAY_FOREACH_BEGIN(item->ppValues, j);
		{
			estrConcatf(triviastring, "\"%s\" %u%s", item->ppValues[j]->pVal, item->ppValues[j]->uCount, j < itemValueSize - 1 ? ", " : "");
		}
		EARRAY_FOREACH_END;
		estrConcatf(triviastring, "}%s", i < itemListSize - 1 ? ", " : "");
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
	return true;
}

AUTO_STRUCT;
typedef struct TriviaDataQueue
{
	char *filename;
	char *oldFilename;
	char *triviaString; AST(ESTRING)
} TriviaDataQueue;
static CRITICAL_SECTION sTriviaQueueCS;
static EARRAY_OF(TriviaDataQueue) seaTriviaQueue = NULL;

#define TRIVIA_QUEUE_PER_FRAME 5
DWORD WINAPI TriviaDataThreadedWrite(LPVOID lpParam)
{
	TriviaDataQueue *processing[TRIVIA_QUEUE_PER_FRAME];
	int processingIndex[TRIVIA_QUEUE_PER_FRAME];
	int i, idx, count, size, lasti;
	
	EXCEPTION_HANDLER_BEGIN

	while (!ErrorTrackerExitTriggered())
	{
		autoTimerThreadFrameBegin("TriviaDataThreadedWrite");
		lasti = idx = 0;
		count = 0;
		EnterCriticalSection(&sTriviaQueueCS);
		size = eaSize(&seaTriviaQueue);
		LeaveCriticalSection(&sTriviaQueueCS);

		while (idx<size && count < TRIVIA_QUEUE_PER_FRAME)
		{
			EnterCriticalSection(&sTriviaQueueCS);
			for (; idx < size && count < TRIVIA_QUEUE_PER_FRAME; count++)
			{
				processingIndex[count] = idx;
				processing[count] = seaTriviaQueue[idx];
				idx++;
			}
			LeaveCriticalSection(&sTriviaQueueCS);
			for (i=lasti; i<count; )
			{
				if (fileExists(processing[i]->filename) && 
					fileExists(processing[i]->oldFilename))
				{
					memmove(processingIndex+i, processingIndex+i+1, (count-i-1)* sizeof(int));
					memmove(processing+i, processing+i+1, (count-i-1)* sizeof(TriviaDataQueue*));
					count--;
				}
				else
					i++;
			}
			lasti = count;
		}
		EnterCriticalSection(&sTriviaQueueCS);
		for (i=count-1; i>=0; i--)
			eaRemove(&seaTriviaQueue, processingIndex[i]);
		suTriviaRequestsOutstanding -= count;
		LeaveCriticalSection(&sTriviaQueueCS);
				
		for (i=0; i<count; i++)
		{
			// process
			PERFINFO_AUTO_START("DataWrite", 1);
			mkdirtree(processing[i]->filename);
			if (fileExists(processing[i]->oldFilename))
				filelog_printf(processing[i]->oldFilename, "%s", processing[i]->triviaString);
			else
				filelog_printf_zipped(processing[i]->filename, "%s", processing[i]->triviaString);
			PERFINFO_AUTO_STOP();
			StructDestroy(parse_TriviaDataQueue, processing[i]);
		}
		autoTimerThreadFrameEnd();
		Sleep(1);
	}
	EXCEPTION_HANDLER_END
	return 0;
}

#define TRIVIA_ALERT_PERIOD (SECONDS_PER_MINUTE * 30)
static void TriviaDataThreadedQueue(U32 etid, char *format, ...)
{
	TriviaDataQueue *queue = StructCreate(parse_TriviaDataQueue);
	va_list ap;
	char filename[MAX_PATH];

	va_start(ap, format);
	estrConcatfv(&queue->triviaString, format, ap);
	va_end(ap);
	GetTriviaDataFileName(etid, SAFESTR(filename));
	queue->oldFilename = strdup(filename);
	GetTriviaDataZipFileName(etid, SAFESTR(filename));
	queue->filename = strdup(filename);
	
	EnterCriticalSection(&sTriviaQueueCS);
	if (gErrorTrackerSettings.uTriviaAlertLimit && (U32) eaSize(&seaTriviaQueue) > gErrorTrackerSettings.uTriviaAlertLimit)
	{
		static U32 suLastTriviaAlert = 0;
		U32 uTime = timeSecondsSince2000();

		if (uTime - suLastTriviaAlert > TRIVIA_ALERT_PERIOD)
		{
			suLastTriviaAlert = uTime;
			/*TriggerAlertfEx(allocAddString("ERRORTRACKER_TRIVIABACKEDUP"), ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
				0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), 
				getHostName(), 0, __FILE__, __LINE__, "Trivia thread writing backed up to %d entries", eaSize(&seaTriviaQueue));*/
		}
	}
	eaPush(&seaTriviaQueue, queue);
	suTriviaRequestsOutstanding++;
	LeaveCriticalSection(&sTriviaQueueCS);
}

void LogTriviaData(U32 uID, CONST_EARRAY_OF(TriviaData) ppTriviaData)
{
	char *triviastring = NULL;
	PERFINFO_AUTO_START_FUNC();
	if(PopulateTriviaString(ppTriviaData, &triviastring))
	{
		TriviaDataThreadedQueue(uID, "TriviaData(%s)", triviastring);
	}
	estrDestroy(&triviastring);
	PERFINFO_AUTO_STOP();
}

void LogTriviaOverview(U32 uID, ErrorEntry *pEntry)
{
	/*char filename[MAX_PATH];
	char *triviastring = NULL;
	PERFINFO_AUTO_START_FUNC();
	if(PopulateTriviaOverviewString(pEntry, &triviastring))
	{
		GetTriviaDataFileName(uID, SAFESTR(filename));
		TriviaDataThreadedQueue(uID, "TriviaOverview(%s)", triviastring);
	}
	estrDestroy(&triviastring);
	PERFINFO_AUTO_STOP();*/
	// non-functional for now
	// TODO
}

char *ParseTriviaData(char *line, EARRAY_OF(NOCONST(TriviaData)) *pppTriviaDatas)
{
	NOCONST(TriviaData) *data;
	char *pCurStart, *pReadHead, *pReadHead2;
	char *pKey = NULL;
	char *pValue = NULL;

	// Find the first quote
	pCurStart = line;
	if(*pCurStart != '\"')
		goto parse_fail;

	++pCurStart;
	// Find the second quote. If pCurStart is the end of the string, this will return NULL
	pReadHead = strchr_fast_nonconst(pCurStart, '\"');
	if(!pReadHead)
		goto parse_fail;

	// Copy the key
	*pReadHead = '\0';
	estrAppendUnescaped(&pKey, pCurStart);
	*pReadHead = '\"';

	pCurStart = pReadHead + 1;
	// Find the third quote. If pCurStart is the end of the string, this will return NULL
	pReadHead = strchr_fast_nonconst(pCurStart, '\"');
	if(!pReadHead)
		goto parse_fail;

	pCurStart = pReadHead + 1;

	pReadHead2 = pCurStart;

	if(stricmp(pKey, "playerPos") == 0)
	{
		// skip two quotes if it's a playerPos
		char *next_quote = strchr_fast_nonconst(pReadHead2, '\"');
		char *escaped_quote = strstri_safe(pReadHead2, "\\q");
		if(!(escaped_quote && next_quote && escaped_quote < next_quote))
		{
			if(next_quote)
			{
				next_quote = strchr_fast_nonconst(next_quote + 1, '\"');
				if(next_quote)
					pReadHead2 = next_quote + 1;
			}
		}
	}

	// Find the fourth quote. If pCurStart is the end of the string, this will return NULL
	pReadHead = strchr_fast_nonconst(pReadHead2, '\"');
	if(!pReadHead)
		goto parse_fail;

	// Copy the value
	*pReadHead = '\0';
	estrAppendUnescaped(&pValue, pCurStart);
	*pReadHead = '\"';

	data = StructCreateNoConst(parse_TriviaData);
	if(!data)
		goto parse_fail;

	data->pKey = pKey;
	data->pVal = pValue;

	eaPush(pppTriviaDatas, data);

	pCurStart = pReadHead + 1;
	if(*pCurStart != ',')
		return NULL;

	++pCurStart;

	if(*pCurStart != ' ')
		return NULL;

	++pCurStart;

	return pCurStart;

parse_fail:
	estrDestroy(&pKey);
	estrDestroy(&pValue);
	return NULL;
}

char *ParseTriviaOverviewValue(char *line, EARRAY_OF(NOCONST(TriviaOverviewValue)) *pppTriviaValues)
{
	NOCONST(TriviaOverviewValue) *data;
	char *pCurStart, *pReadHead;
	char *pValue = NULL;
	U32 uCount = 0;

	// Find the first quote
	pCurStart = line;
	if(*pCurStart != '\"')
		goto parse_fail;

	++pCurStart;
	// Find the second quote. If pCurStart is the end of the string, this will return NULL
	pReadHead = strchr_fast_nonconst(pCurStart, '\"');
	if(!pReadHead)
		goto parse_fail;

	// Copy the value
	*pReadHead = '\0';
	estrCopy2(&pValue, pCurStart);
	*pReadHead = '\"';


	pCurStart = pReadHead + 1;
	if(!pCurStart)
		goto parse_fail;

	pCurStart = pReadHead + 1;
	if(!pCurStart)
		goto parse_fail;

	pReadHead = strchr_fast_nonconst(pCurStart, ',');
	if(pReadHead)
	{
		*pReadHead = '\0';
		uCount = atoi(pCurStart);
		*pReadHead = ',';
	}
	else
	{
		uCount = atoi(pCurStart);
	}

	data = StructCreateNoConst(parse_TriviaOverviewValue);
	if(!data)
		goto parse_fail;

	data->uCount = uCount;
	data->pVal = pValue;

	eaPush(pppTriviaValues, data);

	if(!pReadHead)
		return NULL;

	pCurStart = pReadHead + 1;

	if(*pCurStart != ' ')
		return NULL;

	++pCurStart;

	return pCurStart;

parse_fail:
	estrDestroy(&pValue);
	return NULL;
}

char *ParseTriviaOverviewItem(char *line, EARRAY_OF(NOCONST(TriviaOverviewItem)) *pppTriviaItems)
{
	NOCONST(TriviaOverviewItem) *data;
	char *pCurStart, *pReadHead;
	char *pKey = NULL;

	// Find the first quote
	pCurStart = line;
	if(*pCurStart != '\"')
		goto parse_fail;

	++pCurStart;
	// Find the second quote. If pCurStart is the end of the string, this will return NULL
	pReadHead = strchr_fast_nonconst(pCurStart, '\"');
	if(!pReadHead)
		goto parse_fail;

	// Copy the key
	*pReadHead = '\0';
	estrCopy2(&pKey, pCurStart);
	*pReadHead = '\"';


	pCurStart = pReadHead + 1;
	if(!pCurStart)
		goto parse_fail;

	++pCurStart;
	if(!pCurStart)
		goto parse_fail;

	pReadHead = strchr_fast_nonconst(pCurStart, '}');
	if(!pReadHead)
		goto parse_fail;

	data = StructCreateNoConst(parse_TriviaOverviewItem);
	if(!data)
		goto parse_fail;

	data->pKey = pKey;

	*pReadHead = '\0';
	while(pCurStart)
		pCurStart = ParseTriviaOverviewValue(pCurStart, &data->ppValues);
	*pReadHead = '}';

	eaPush(pppTriviaItems, data);

	pCurStart = pReadHead + 1;

	if(*pCurStart != ',')
		return NULL;

	pCurStart = pReadHead + 1;

	if(*pCurStart != ' ')
		return NULL;

	++pCurStart;

	return pCurStart;

parse_fail:
	estrDestroy(&pKey);
	return NULL;
}

void ParseTriviaDataLine(char *line, TriviaOverview *pOverview)
{
	char *pCurStart;
	EARRAY_OF(NOCONST(TriviaData)) ppTriviaDatas = NULL;
	pCurStart = strchr_fast_nonconst(line, '(');
	if(!pCurStart)
		return;

	++pCurStart;

	while(pCurStart)
	{
		pCurStart = ParseTriviaData(pCurStart, &ppTriviaDatas);
	}

	FilterForTriviaOverview(pOverview, (CONST_EARRAY_OF(TriviaData)) ppTriviaDatas);
	eaDestroyStructNoConst(&ppTriviaDatas, parse_TriviaData);
}

void ParseTriviaOverviewLine(char *line, TriviaOverview *pOverview)
{
	char *pCurStart;
	pCurStart = strchr_fast_nonconst(line, '(');
	if(!pCurStart)
		return;

	++pCurStart;

	while(pCurStart)
	{
		pCurStart = ParseTriviaOverviewItem(pCurStart, &(CONTAINER_NOCONST(TriviaOverview, pOverview))->ppTriviaItems);
	}
}

void ParseTriviaLogLine(char *line, TriviaOverview *pOverview)
{
	char *templine = NULL;
	char *start;
	bool bEscaping = false;

	if(strstr(line, " ESC :"))
		bEscaping = true;

	if(start = strstr(line, "TriviaData("))
	{
		estrStackCreate(&templine);

		if(bEscaping)
			estrAppendUnescaped(&templine, start);
		else
			estrCopy2(&templine, start);

		ParseTriviaDataLine(templine, pOverview);
	}
	else if(start = strstr(line, "TriviaOverview("))
	{
		estrStackCreate(&templine);

		if(bEscaping)
			estrAppendUnescaped(&templine, start);
		else
			estrCopy2(&templine, start);

		ParseTriviaOverviewLine(templine, pOverview);
	}
	estrDestroy(&templine);
}

void GetOldTriviaDataFileName(U32 uID, char *destfilename, size_t destfilename_size)
{
	sprintf_s(SAFESTR2(destfilename), "%s\\%d\\triviadata.log", getRawDataDir(), uID);
}

void GetTriviaDataFileName(U32 uID, char *destfilename, size_t destfilename_size)
{
	GetErrorEntryDir(getRawDataDir(), uID, SAFESTR2(destfilename));
	strcat_s(SAFESTR2(destfilename), "\\triviadata.log");
}

void GetTriviaDataZipFileName(U32 uID, char *destfilename, size_t destfilename_size)
{
	GetErrorEntryDir(getRawDataDir(), uID, SAFESTR2(destfilename));
	strcat_s(SAFESTR2(destfilename), "\\triviadata.log.zip");
}

void ReadTriviaData(U32 uID, TriviaOverview *overview)
{
	char filename[MAX_PATH];
	FILE *file;
	char *line = NULL;

	GetTriviaDataFileName(uID, SAFESTR(filename));

	file = fopen(filename, "rt");
	if(!file) 
	{
		GetOldTriviaDataFileName(uID, SAFESTR(filename));
		file = fopen(filename, "rt");
		if(!file)
			return;
	}
	estrStackCreate(&line);

	while(fgetEString(&line, file))
	{
		ParseTriviaLogLine(line, overview);
	}
	estrDestroy(&line);
}

// -------------------------------------------------------------------------------------
// Migration Steps

static void writeZippedFile(U32 uID)
{
	char logFilePath[MAX_PATH];
	char zipFilePath[MAX_PATH];
	FILE *logFile;
	FILE *zipFile;
	char *line = NULL;
	int count = 0;

	GetTriviaDataFileName(uID, SAFESTR(logFilePath));
	if (!fileExists(logFilePath))
		return;

	GetTriviaDataZipFileName(uID, SAFESTR(zipFilePath));
	if (fileExists(zipFilePath))
		fileForceRemove(zipFilePath);

	logFile = fopen(logFilePath, "rt");
	if (!devassert(logFile))
		return;
	zipFile = fopen(zipFilePath, "wbz");
	if (!devassert(zipFile))
		return;
	estrStackCreate(&line);
	while(fgetEString(&line, logFile))
	{
		unsigned int len = estrLength(&line);
		fwrite(line, 1, len, zipFile);
		count++;
		if (count % 10 == 0)
			Sleep(1);
	}
	fclose(logFile);
	fclose(zipFile);
	estrDestroy(&line);
	fileForceRemove(logFilePath);
}

static INT_EARRAY seaiTriviaMigrateETIDs = NULL;
static INT_EARRAY seaiTriviaMigrateComplete = NULL;
static int siTriviaToMigrateCount = 0;
static int siTriviaMigratedCount = 0;

AUTO_COMMAND;
void updateTriviaMigrateFile(void)
{
	static char fname[CRYPTIC_MAX_PATH] = "";
	FILE *file;
	char buffer[32];
	int i, size;

	PERFINFO_AUTO_START_FUNC();
	if (fname[0] == 0)
		sprintf(fname, "%s/%s", fileLocalDataDir(), "triviaToMigrate.txt");
	file = fopen(fname, "wt");
	if (!devassert(file))
		return;
	size = eaiSize(&seaiTriviaMigrateETIDs);
	for (i=siTriviaMigratedCount; i<size; i++)
	{
		sprintf(buffer, "%d\n", seaiTriviaMigrateETIDs[i]);
		fwrite(buffer, 1, strlen(buffer), file);
	}
	fclose(file);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND;
void TriviaMigratePrintStatus(void)
{
	printf("Migrated %d out of %d.\n", siTriviaMigratedCount, siTriviaToMigrateCount);
}

static DWORD WINAPI TriviaDataMigrateThread(LPVOID lpParam)
{
	EXCEPTION_HANDLER_BEGIN
	siTriviaMigratedCount = 0;
	EARRAY_INT_CONST_FOREACH_BEGIN(seaiTriviaMigrateETIDs, i, n);
	{
		writeZippedFile(seaiTriviaMigrateETIDs[i]);
		siTriviaMigratedCount++;
		// Update 
		if (siTriviaMigratedCount % 1000 == 0)
			updateTriviaMigrateFile();
		Sleep(1);
	}
	EARRAY_FOREACH_END;
	eaiDestroy(&seaiTriviaMigrateETIDs);
	TriggerAlertfEx(allocAddString("ERRORTRACKER_TRIVIABACKEDUP"), ALERTLEVEL_WARNING, ALERTCATEGORY_PROGRAMMER,
		0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), 
		getHostName(), 0, __FILE__, __LINE__, "Trivia zip conversion done. This is not really an alert and there is no action to be taken.");
	EXCEPTION_HANDLER_END
	return 0;
}

void TriviaDataInit(void)
{
	DWORD dummy;
	char fname[CRYPTIC_MAX_PATH];
	char buffer[32];
	FILE *f;

	InitializeCriticalSection(&sTriviaQueueCS);
	CloseHandle((HANDLE) _beginthreadex(0, 0, TriviaDataThreadedWrite, 0, 0, &dummy));
	
	sprintf(fname, "%s/%s", fileLocalDataDir(), "triviaToMigrate.txt");
	f = fopen(fname, "rt");
	if (f)
	{
		while (!feof(f->fptr))
		{
			if (fgets(buffer, ARRAY_SIZE(buffer), f))
			{
				U32 id = atoi(buffer);
				if (id)
					eaiPush(&seaiTriviaMigrateETIDs, id);
			}
		}
		fclose(f);
	}
	siTriviaToMigrateCount = eaiSize(&seaiTriviaMigrateETIDs);
	if (siTriviaToMigrateCount > 0)
		CloseHandle((HANDLE) _beginthreadex(0, 0, TriviaDataMigrateThread, 0, 0, &dummy));
}

#include "AutoGen/etTrivia_c_ast.c"