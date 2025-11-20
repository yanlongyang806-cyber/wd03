#include "wininclude.h"
#include "WebReport.h"
#include "ErrorEntry.h"
#include "ErrorTracker.h"
#include "etTrivia.h"
#include "HttpLib.h"
#include "HttpClient.h"
#include "jira.h"
#include "Autogen/jira_h_ast.h"
#include "WebReport_c_ast.h"
#include "Search_h_ast.h"
#include "WebInterface.h"
#include "estring.h"
#include "Backlog.h"
#include "Search.h"
#include "StashTable.h"
#include "sock.h"
#include "timing.h"
#include "file.h"
#include "ETCommon/ETShared.h"
#include "ETCommon/ETWebCommon.h"
#include "timing_profiler_interface.h"
#include "logging.h"

#define UNSPECIFIED_TIME "UnspecifiedTime"

extern ParseTable parse_JiraIssue[];
#define TYPE_parse_JiraIssue JiraIssue

#define REPORT_PREFIX "/report/"
#define REPORT_PREFIX_LEN 8

#define TRIVIA_REPORT_PREFIX "/triviareport/"
#define TRIVIA_REPORT_PREFIX_LEN 14

AUTO_STRUCT;
typedef struct TriviaReportInfo
{
	int uID;
	TriviaOverview overview;
} TriviaReportInfo;

AUTO_STRUCT;
typedef struct ReportInfo
{
	char *id; AST(ESTRING)

	int percentComplete;
	int complete;
	int cancel;
	U32 age;
	U32 uIP;

	U32 uCurr;
	U32 uCount;

	SearchData *search;
	ErrorEntry **foundEntries;
	TriviaReportInfo *pTriviaReportInfo;
	void *userData; NO_AST
	Backlog *backlog; NO_AST // Reminder to delete this manually!
} ReportInfo;

static StashTable reportTable;
static StashTable ipToReportTable;

static ReportInfo **ppReportsToDelete = NULL;

#define MAX_REPORT_AGE 86400 

static void wiProgress(NetLink *link, ReportInfo *pReportInfo, const char *location);
static void wiDisplayFullReport(NetLink *link, ReportInfo *pReportInfo);
static void wiDisplayMatches(NetLink *link, ReportInfo *pReportInfo);
static void wiDisplayMatchDetail(NetLink *link, ReportInfo *pReportInfo, int iIndexID);

void initWebReports()
{
	reportTable     = stashTableCreateWithStringKeys(512, StashDeepCopyKeys_NeverRelease);
	ipToReportTable = stashTableCreateInt(512);
}

void expireOldReports()
{
	StashTableIterator iter;
	StashElement element;
	int i;
	ReportInfo *pReport;
	stashGetIterator(reportTable, &iter);

	while (stashGetNextElement(&iter, &element))
	{
		pReport = stashElementGetPointer(element);
		if (pReport->complete && pReport->age + MAX_REPORT_AGE < timeSecondsSince2000())
		{
			stashRemovePointer(reportTable, pReport->id, NULL);
			stashIntRemovePointer(ipToReportTable, pReport->uIP, NULL);
			eaPush(&ppReportsToDelete, pReport);
		}
	}

	// Reports are always removed from Stash Table before being added to earray
	for (i = eaSize(&ppReportsToDelete) - 1; i >= 0; i--)
	{
		pReport = ppReportsToDelete[i];
		if (!pReport->complete) 
			continue;
		eaRemove(&ppReportsToDelete, i);

		if(pReport->backlog)
			free(pReport->backlog);
		StructDestroy(parse_ReportInfo, pReport);
	}
}

static void stealJiraState(NOCONST(ErrorEntry) *pEntry)
{
	// Note: This is not threadsafe! I'm assuming that since we only really alter already-created Jira substructs
	//       in the middle of the night, this is safe-ish. If this ever explodes, I can audit the Jira manipulation
	//       code in here and try to wrap it up properly.

	ErrorEntry *pFound = findErrorTrackerByID(pEntry->uID);
	if(pFound && pFound->pJiraIssue)
	{
		pEntry->pJiraIssue = StructCloneDeConst(parse_JiraIssue, pFound->pJiraIssue);
	}
}

static ErrorEntry *findETID(ReportInfo *pReportInfo, U32 id)
{
	EARRAY_FOREACH_BEGIN(pReportInfo->foundEntries, i);
	{
		if(pReportInfo->foundEntries[i]->uID == id)
			return pReportInfo->foundEntries[i];
	}
	EARRAY_FOREACH_END;
	return NULL;
}

static DWORD WINAPI TriviaReportThread(ReportInfo *pReportInfo)
{
	U32 uPassed = 0;
	U32 uRewindIndex = 0;
	U32 uCount = 0;
	U32 uStartTime = 0;
	U32 uEndTime   = 0;
	char filename[MAX_PATH];
	FILE *file;
	char *line = NULL;
	EXCEPTION_HANDLER_BEGIN
	{
		TriviaReportInfo *info = pReportInfo->pTriviaReportInfo;

		GetTriviaDataZipFileName(info->uID, SAFESTR(filename));


		file = fopen(filename, "rbz");
		if(!file) // Check unzipped file
		{
			GetTriviaDataFileName(info->uID, SAFESTR(filename));
			file = fopen(filename, "rt");
		}
		if (!file) // check old data file location
		{
			GetOldTriviaDataFileName(info->uID, SAFESTR(filename));
			file = fopen(filename, "rt");
		}		
		if (!file)
		{
			printf("Finished Report [%s] [%s].\n", pReportInfo->id, pReportInfo->cancel ? "Cancelled" : "Complete");
			pReportInfo->percentComplete = 100;
			pReportInfo->complete = true;
			pReportInfo->age = timeSecondsSince2000();
		}
		else
		{
			uCount = fileSize(filename);
			pReportInfo->uCount = uCount;
			pReportInfo->uCurr = 0;
			
			// Do work	
			estrStackCreate(&line);
			while(fgetEString(&line, file))
			{
				pReportInfo->uCurr += estrLength(&line);
				pReportInfo->percentComplete = ((pReportInfo->uCurr)*100) / uCount;
				ParseTriviaLogLine(line, &info->overview);

				if(pReportInfo->cancel)
					break;
			}
			estrDestroy(&line);

			fclose(file);
			printf("Finished Report [%s] [%s].\n", pReportInfo->id, pReportInfo->cancel ? "Cancelled" : "Complete");
			pReportInfo->percentComplete = 100;
			pReportInfo->age = timeSecondsSince2000();
		}
		pReportInfo->complete = true;
	}
	EXCEPTION_HANDLER_END

	return 0;
}

static void searchExactAddMatch(SA_PARAM_NN_VALID SearchData *pData, U32 uIndex, U32 uFileIndex, SA_PARAM_NN_VALID ErrorEntry *pEntryMatch)
{
	SearchExactMatch *pMatch = StructCreate(parse_SearchExactMatch);
	pMatch->uIndexID = ++pData->uLastID; // pre-increment

	estrPrintf(&pMatch->filePath, "%d\\%d.ee", uIndex, uFileIndex);
	pMatch->uID = uIndex;
	pMatch->uFileIndex = uFileIndex;
	pMatch->uTime = pEntryMatch->uNewestTime;
	if (!pData->ppExactModeMatches)
		eaIndexedEnable(&pData->ppExactModeMatches, parse_SearchExactMatch);
	eaIndexedAdd(&pData->ppExactModeMatches, pMatch);
}
static DWORD WINAPI ReportThread(ReportInfo *pReportInfo)
{
	EXCEPTION_HANDLER_BEGIN
	{
		U32 i;
		U32 uPassed = 0;
		SearchData *search = pReportInfo->search;
		Backlog *backlog   = pReportInfo->backlog;
		U32 uCurrIndex = backlog->uFront;
		U32 uRewindIndex = 0;
		U32 uCount = 0;
		U32 uStartTime = 0;
		U32 uEndTime   = 0;

		if(search->uFlags & SEARCHFLAG_EXACT_STARTDATE)
			uStartTime = search->uExactStartTime;
		if(search->uFlags & SEARCHFLAG_EXACT_ENDDATE)
			uEndTime = search->uExactEndTime;

		// Skip past old entries
		if(uStartTime != 0)
		{
			while(uPassed < backlog->uCount && backlog->aRecentErrors[uCurrIndex].uTime < uStartTime)
			{
				uPassed++;
				RING_INCREMENT(uCurrIndex);
			}
		}

		uRewindIndex = uCurrIndex;
		for(i=0; uPassed < backlog->uCount; i++)
		{
			if(uEndTime && backlog->aRecentErrors[uCurrIndex].uTime > uEndTime)
				break;

			uCount++;
			RING_INCREMENT(uCurrIndex);
			uPassed++;
		}
		uCurrIndex = uRewindIndex;

		pReportInfo->uCount = uCount;

		for(i=0; i < uCount; i++)
		{
			RecentError *pRecentError;
			NOCONST(ErrorEntry) *pErrorEntry;

			autoTimerThreadFrameBegin("ReportThread");

			pRecentError = &backlog->aRecentErrors[uCurrIndex];
			pErrorEntry = StructCreateNoConst(parse_ErrorEntry);


			pReportInfo->uCurr = i+1;
			pReportInfo->percentComplete = ((i+1)*100) / uCount;

			if(loadErrorEntry(pRecentError->uID, pRecentError->uIndex, pErrorEntry))
			{
				if(searchMatches(pReportInfo->search, CONST_ENTRY(pErrorEntry), 0))
				{
					ErrorEntry *pExisting;

					PERFINFO_AUTO_START("AddExact", 1);

					pExisting = findETID(pReportInfo, pErrorEntry->uID);
					if (pErrorEntry)
						searchExactAddMatch(pReportInfo->search, pRecentError->uID, pRecentError->uIndex, CONST_ENTRY(pErrorEntry));
					if(pExisting)
					{
						mergeErrorEntry_Part1(UNCONST_ENTRY(pExisting), CONST_ENTRY(pErrorEntry), 0);
						mergeErrorEntry_Part2(UNCONST_ENTRY(pExisting), CONST_ENTRY(pErrorEntry), 0);
						pErrorEntry = NULL; // Don't let it be destroyed
					}
					else
					{
						stealJiraState(pErrorEntry);
						eaPush(&pReportInfo->foundEntries, (ErrorEntry*)pErrorEntry);
						pErrorEntry = NULL;
					}

					PERFINFO_AUTO_STOP_CHECKED("AddExact");
				}

				if(pErrorEntry)
					StructDestroyNoConst(parse_ErrorEntry, pErrorEntry);
			}
			else if(pErrorEntry)
				StructDestroyNoConst(parse_ErrorEntry, pErrorEntry);

			if(pReportInfo->cancel) //If we canceled it, we don't care about its information anymore. Just get out as quickly as possible.
			{
				pReportInfo->age = timeSecondsSince2000();
				pReportInfo->complete = true;
				autoTimerThreadFrameEnd();
				return 0;
			}
			RING_INCREMENT(uCurrIndex);

			autoTimerThreadFrameEnd();
		}

		sortBySortOrder(pReportInfo->foundEntries, pReportInfo->search->eSortOrder, pReportInfo->search->bSortDescending);

		printf("Finished Report [%s] [%s].\n", pReportInfo->id, pReportInfo->cancel ? "Cancelled" : "Complete");
		if(pReportInfo->backlog)
		{
			free(pReportInfo->backlog);
			pReportInfo->backlog = NULL;
		}
		pReportInfo->percentComplete = 100;
		pReportInfo->age = timeSecondsSince2000();
		pReportInfo->complete = true;

		if(search->uFlags & SEARCHFLAG_EXACT_INTERNAL)
		{
			assert(search->exactCompleteCallback);
			search->exactCompleteCallback(search, pReportInfo->foundEntries, pReportInfo->userData);
			StructDestroy(parse_ReportInfo, pReportInfo);
		}
	}
	EXCEPTION_HANDLER_END
	return 0;
}

// --------------------------------------------------------------------------------
// Pilfered from AccountServer

static void estrConcatCompressedNumber(SA_PRE_NN_NN_STR char **estr, U64 uValue, SA_PARAM_NN_STR const char *pTable)
{
	while (uValue > 0)
	{
		estrConcatChar(estr, pTable[uValue % strlen(pTable)]);
		uValue /= strlen(pTable);
	}
}

static void createShortUniqueString(char **output, U32 uIP, unsigned int uMaxLen, SA_PARAM_NN_STR const char *pTable)
{
	static const U64 uSubtractMicroseconds = 12887145790099688; // No use worrying about time in the past
	U64 uTime = microsecondsSince1601() - uSubtractMicroseconds;
	static U8 uEverIncrementingID = 0;
	unsigned int tableLen = (unsigned int)strlen(pTable);
	unsigned int uRandom = rand() % tableLen;
	unsigned int uRandom2 = rand() % tableLen;
	unsigned int uMachine = uIP % tableLen;

	estrPrintf(output, "%c%c%c%c", pTable[uRandom], pTable[uRandom2], pTable[uMachine], pTable[uEverIncrementingID]);
	estrConcatCompressedNumber(output, uTime, pTable);
	if (uMaxLen)
		estrSetSize(output, uMaxLen);

	uEverIncrementingID++;
	if (uEverIncrementingID == strlen(pTable)) uEverIncrementingID = 0;
}

// --------------------------------------------------------------------------------

const char * startReport(SearchData *pSearchData, U32 uIP, void *userData)
{
	static char *id = NULL;
	DWORD ignored = 0;
	ReportInfo *pReportInfo = StructCreate(parse_ReportInfo);
	ReportInfo *pPreviousReport = NULL;

	if(!(pSearchData->uFlags & SEARCHFLAG_EXACT_INTERNAL) && 
		stashIntFindPointer(ipToReportTable, uIP, &pPreviousReport) )
	{
		stashIntRemovePointer(ipToReportTable, uIP, NULL);
		if(pPreviousReport)
		{
			stashRemovePointer(reportTable, pPreviousReport->id, NULL);
			
			if(!pPreviousReport->complete)
			{
				pPreviousReport->cancel = 1;
				printf("Waiting for %s [%s] to complete...\n", makeIpStr(uIP), pPreviousReport->id);
				eaPush(&ppReportsToDelete, pPreviousReport);
				pPreviousReport = NULL;
			}
			else
			{
				printf("Throwing out old report for %s [%s]\n", makeIpStr(uIP), pPreviousReport->id);
				if(pPreviousReport->backlog)
					free(pPreviousReport->backlog);
				StructDestroy(parse_ReportInfo, pPreviousReport);
			}
		}
	}

	pReportInfo->search   = StructClone(parse_SearchData, pSearchData);
	pReportInfo->backlog  = backlogClone();
	pReportInfo->userData = userData;
	pReportInfo->uIP      = uIP;
	
	if(pSearchData->uFlags & SEARCHFLAG_EXACT_INTERNAL)
	{
		assertmsg(pSearchData->exactCompleteCallback, "Internal Exact Search requested with no callback! Someone wrote bad ET code. Probably Joe.");
		createShortUniqueString(&id, uIP, 20, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
		estrCopy(&pReportInfo->id, &id);
	}
	else
	{
		do
		{
			createShortUniqueString(&id, uIP, 20, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
			estrCopy(&pReportInfo->id, &id);		
		} while (!stashAddPointer(reportTable, pReportInfo->id, pReportInfo, false));
		assert(stashIntAddPointer(ipToReportTable, uIP, pReportInfo, false));
	}

	printf("Starting Report for %s [%s]...\n", makeIpStr(uIP), id);
	CloseHandle((HANDLE) _beginthreadex(NULL, 0, ReportThread, pReportInfo, 0, &ignored));
	return id;
}

void wiReport(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	char *id = NULL;
	bool bPercentOnly = false;
	ReportInfo *pReportInfo = NULL;
	char *type = "";
	int iIndexID = 0;
	SortOrder sortOrder = -1;
	bool descending = 0;

	for(i=1; i<count; i++)
	{
		if(!stricmp(args[i], "type"))
			type = values[i];
		else if(!stricmp(args[i], "id"))
			iIndexID = atoi(values[i]);
		else if(!stricmp(args[i], "sort"))
			sortOrder = atoi(values[i]);
		else if(!stricmp(args[i], "descend"))
			descending = atoi(values[i]);
	}

	id = strstri(args[0], REPORT_PREFIX);
	if(!id)
	{
		httpSendFileNotFoundError(link, "Bad report URL!");
		return;
	}
	id += REPORT_PREFIX_LEN;

	if(id[0] == 'P' && id[1] == '_')
	{
		bPercentOnly = true;
		id+=2;
	}

	if(!stashFindPointer(reportTable, id, &pReportInfo) || !pReportInfo)
	{
		httpSendWrappedString(link, "No report by that ID.", NULL, NULL);
		return;
	}

	if (sortOrder != -1)
	{
		sortBySortOrder(pReportInfo->foundEntries, sortOrder, descending);
		pReportInfo->search->eSortOrder = sortOrder;
		pReportInfo->search->bSortDescending = descending;
	}

	if(bPercentOnly)
	{
		static char *temp = NULL;
		estrPrintf(&temp, "{ \"percentage\":%d, \"complete\":%d, \"description\":\"Processing, please wait... [%u/%u]\" }",
			pReportInfo->percentComplete, 
			pReportInfo->complete, 
			pReportInfo->uCurr, pReportInfo->uCount
			);
		httpSendBytes(link, temp, (int)strlen(temp));
	}
	else if(pReportInfo->complete)
	{
		if(!stricmp(type, "csv"))
		{
			wiDisplayCSV(link, pReportInfo->search, pReportInfo->foundEntries);
		}
		else if(!stricmp(type, "copypaste"))
		{
			wiDisplayCopyPaste(link, pReportInfo->search, pReportInfo->foundEntries);
		}
		else if (!stricmp(type, "viewmatches"))
		{
			wiDisplayMatches(link, pReportInfo);
		}
		else if (!stricmp(type, "detail"))
		{
			wiDisplayMatchDetail(link, pReportInfo, iIndexID);
		}
		else
		{
			wiDisplayFullReport(link, pReportInfo);
		}
	}
	else
	{
		wiProgress(link, pReportInfo, "/report/");
	}
	
}

const char * startTriviaReport(int iUniqueID, U32 uIP)
{
	static char *id = NULL;
	DWORD ignored = 0;
	ReportInfo *pReportInfo = StructCreate(parse_ReportInfo);
	ReportInfo *pPreviousReport = NULL;

	if(stashIntFindPointer(ipToReportTable, uIP, &pPreviousReport))
	{
		stashIntRemovePointer(ipToReportTable, uIP, NULL);
		if(pPreviousReport)
		{
			stashRemovePointer(reportTable, pPreviousReport->id, NULL);			
			if(!pPreviousReport->complete)
			{
				pPreviousReport->cancel = 1;
				verbose_printf("Waiting for %s [%s] to complete...\n", makeIpStr(uIP), pPreviousReport->id);

				eaPush(&ppReportsToDelete, pPreviousReport);
				pPreviousReport = NULL;
			}
			else
			{
				printf("Throwing out old report for %s [%s]\n", makeIpStr(uIP), pPreviousReport->id);
				if(pPreviousReport->backlog)
					free(pPreviousReport->backlog);
				StructDestroy(parse_ReportInfo, pPreviousReport);
			}
		}
	}
	
	pReportInfo->pTriviaReportInfo = StructCreate(parse_TriviaReportInfo);
	pReportInfo->pTriviaReportInfo->uID = iUniqueID;
	pReportInfo->uIP = uIP;

	do
	{
		createShortUniqueString(&id, uIP, 20, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
		estrCopy(&pReportInfo->id, &id);		
	} while (!stashAddPointer(reportTable, pReportInfo->id, pReportInfo, false));
	assert(stashIntAddPointer(ipToReportTable, uIP, pReportInfo, false));

	verbose_printf("Starting Report for %s [%s]...\n", makeIpStr(uIP), id);
	CloseHandle((HANDLE) _beginthreadex(NULL, 0, TriviaReportThread, pReportInfo, 0, &ignored));

	return id;
}

void wiTriviaReport(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL; // Reuse this estring so that its growth stabilizes after a few hits
	int i;
	char *id = NULL;
	bool bPercentOnly = false;
	ReportInfo *pReportInfo = NULL;
	char *type = "";

	estrCopy2(&s, "");

	for(i=1; i<count; i++)
	{
		if(!stricmp(args[i], "type"))
			type = values[i];
	}

	id = strstri(args[0], TRIVIA_REPORT_PREFIX);
	if(!id)
	{
		httpSendFileNotFoundError(link, "Bad report URL!");
		return;
	}
	id += TRIVIA_REPORT_PREFIX_LEN;

	if(id[0] == 'P' && id[1] == '_')
	{
		bPercentOnly = true;
		id+=2;
	}

	if(!stashFindPointer(reportTable, id, &pReportInfo) || !pReportInfo)
	{
		httpSendWrappedString(link, "No report by that ID.", NULL, NULL);
		return;
	}

	if(bPercentOnly)
	{
		static char *temp = NULL;
		estrPrintf(&temp, "{ \"percentage\":%d, \"complete\":%d, \"description\":\"Processing, please wait... [%u/%u]\" }",
			pReportInfo->percentComplete, 
			pReportInfo->complete, 
			pReportInfo->uCurr, pReportInfo->uCount
			);
		httpSendBytes(link, temp, (int)strlen(temp));
	}
	else if(pReportInfo->complete)
	{
		appendTriviaOverview(&s, (CONST_EARRAY_OF(TriviaOverviewItem)) pReportInfo->pTriviaReportInfo->overview.ppTriviaItems);
		httpSendWrappedString(link, s, NULL, NULL);
	}
	else
	{
		wiProgress(link, pReportInfo, "/triviareport/");
	}
	
}

const char *progressFormat = // contains two %s's
	"<script>\n"
	"<!--\n"
	"\n"
	"var id = '%s';\n"
	"\n"
	"function updateProgress() {\n"
	"	$.getJSON('/report/P_'+id, {})"
	"	.done(function (data) {\n"
	"		if(data.complete)\n"
	"       {\n"
	"			window.location = '%s'+id;\n"
	"       }\n"
	"	    $('#progdesc').text(data.description);\n"
	"	    $('#progbar').progressbar({value: data.percentage});\n"
	"	    window.setTimeout(updateProgress, 1000);\n"
	"    })\n"
	"    .fail(function(data) {\n"
	"	    $('#progdesc').text('Failing to talk to processing script ...');\n"
	"	    window.setTimeout(updateProgress, 1000);\n"
	"    });\n"
	"}\n"
	"\n"
	"$(document).ready(function () {\n"
	"	$('#progbar').progressbar({max: 100});\n"
	"	updateProgress();\n"
	"} );\n"
	"\n"
	"-->\n"
	"</script>\n"
	"\n"
	"<br><div id=\"progdesc\">Please Wait ...</div>\n"
	"<div id=\"progbar\"></div><br><br><br><br><br><br>\n";

static void wiProgress(NetLink *link, ReportInfo *pReportInfo, const char *location)
{
	static char *s = NULL;
	estrPrintf(&s, FORMAT_OK(progressFormat), pReportInfo->id, location);

	httpSendWrappedString(link, s, NULL, NULL);
}

static const char *exactOptionsFormat = // %s %s %s %s %s %s
"<table class=\"exacttable\">"
"<tr><td><b>Exact Mode Export Options</b></a> - [%s]</td></tr>"
"<tr><td style=\"text-align:center\">"
"<br>"
"<a href=\"/report/%s?type=csv\">Save as CSV</a><br>"
"<br>"
"<a href=\"/report/%s?type=copypaste\">Display as Copy/Paste Friendly</a><br>"
"<br>"
"<a href=\"/report/%s?type=viewmatches\">View All Exact Matches</a><br>"
"<br>"
"Time Range: [%s - %s]<br>"
"<br>"
"<br>"
"</td></tr>"
"</table>"
;

static void wiDisplayFullReport(NetLink *link, ReportInfo *pReportInfo)
{
	static char *s = NULL;
	static char *exactOptions = NULL;
	char dateStart[1024] = UNSPECIFIED_TIME;
	char dateEnd[1024] = UNSPECIFIED_TIME;
	estrCopy2(&s, "");

	if(pReportInfo->search->uFlags & SEARCHFLAG_EXACT_STARTDATE)
		timeMakeLocalDateStringFromSecondsSince2000(dateStart, pReportInfo->search->uExactStartTime);
	if(pReportInfo->search->uFlags & SEARCHFLAG_EXACT_ENDDATE)
		timeMakeLocalDateStringFromSecondsSince2000(dateEnd,   pReportInfo->search->uExactEndTime);

	estrPrintf(&exactOptions, FORMAT_OK(exactOptionsFormat), pReportInfo->id, pReportInfo->id, pReportInfo->id, pReportInfo->id, dateStart, dateEnd);

	if(!(pReportInfo->search->uFlags & SEARCHFLAG_EXACT_STARTDATE))
		dateStart[0] = 0;
	if(!(pReportInfo->search->uFlags & SEARCHFLAG_EXACT_ENDDATE))
		dateEnd[0] = 0;

	wiAppendSearchForm(link, &s, pReportInfo->search, false, 0, 0, dateStart, dateEnd, exactOptions);

	appendReportHeader(&s, STF_NO_SEEN|STF_VERSION, pReportInfo->id, pReportInfo->search);
	EARRAY_FOREACH_BEGIN(pReportInfo->foundEntries, i);
	{
		ErrorEntry *p = pReportInfo->foundEntries[i];
		wiAppendSummaryTableEntry(p, &s, NULL, false, 0, 0, p->iTotalCount, STF_NO_SEEN|STF_VERSION);
	}
	EARRAY_FOREACH_END;
	estrConcatf(&s, "</table>\n");

	httpSendWrappedString(link, s, NULL, NULL);
}

static const char *csvHeader =
"\"ID\","
"\"Type\","
"\"Executable\","
"\"Count\","
"\"Product\","
"\"Jira\","
"\"Version\","
"\"Info\","
"\n";

#define NL_ON_NONEMPTY_TEMP (temp[0] ? "\n" : "")

void wiDisplayCSV(NetLink *link, SearchData *search, ErrorEntry **entries)
{
	static char *s = NULL;
	static char *temp = NULL;
	char dateStart[1024] = UNSPECIFIED_TIME;
	char dateEnd[1024] = UNSPECIFIED_TIME;
	estrCopy2(&s, csvHeader);

	if(search->uFlags & SEARCHFLAG_EXACT_STARTDATE)
		timeMakeLocalDateStringFromSecondsSince2000(dateStart, search->uExactStartTime);
	if(search->uFlags & SEARCHFLAG_EXACT_ENDDATE)
		timeMakeLocalDateStringFromSecondsSince2000(dateEnd,   search->uExactEndTime);

	EARRAY_FOREACH_BEGIN(entries, i);
	{
		ErrorEntry *p = entries[i];
		estrConcatf(&s, "\"%u\",", p->uID);
		estrConcatf(&s, "\"%s\",", ErrorDataTypeToString(p->eType));

		estrCopy2(&temp, "");
		EARRAY_FOREACH_BEGIN(p->ppExecutableNames, j);
		{
			if(p->ppExecutableNames[j])
				estrConcatf(&temp, "%s%s", NL_ON_NONEMPTY_TEMP, p->ppExecutableNames[j]);
		}
		EARRAY_FOREACH_END;
		estrConcatf(&s, "\"%s\",", temp);

		estrConcatf(&s, "\"%d\",", p->iTotalCount);

		estrCopy2(&temp, "");
		EARRAY_FOREACH_BEGIN(p->eaProductOccurrences, j);
		{
			estrConcatf(&temp, "%s%s", NL_ON_NONEMPTY_TEMP, p->eaProductOccurrences[j]->key);
		}
		EARRAY_FOREACH_END;
		estrConcatf(&s, "\"%s\",", temp);

		estrCopy2(&temp, "");
		if(p->pJiraIssue)
		{
			estrConcatf(&temp, "%s / %s / %s", 
				p->pJiraIssue->key, 
				(p->pJiraIssue->assignee) ? p->pJiraIssue->assignee : "unknown",
				jiraGetStatusString(p->pJiraIssue->status));
		}
		estrConcatf(&s, "\"%s\",", temp);

		estrCopy2(&temp, "");
		EARRAY_FOREACH_BEGIN(p->ppVersions, j);
		{
			if(p->ppVersions[j])
			{
				const char *version = strchr(p->ppVersions[j], ' ');
				int len = 0;
				if (version)
					len = version - p->ppVersions[j];
				if (len)
				{
					estrConcatf(&temp, "%s", NL_ON_NONEMPTY_TEMP);
					estrConcat(&temp, p->ppVersions[j], len);
				}
				else
					estrConcatf(&temp, "%s%s", NL_ON_NONEMPTY_TEMP, p->ppVersions[j]);
			}
		}
		EARRAY_FOREACH_END;
		estrConcatf(&s, "\"%s\",", temp);

		estrCopy2(&temp, "");
		if(p->pErrorString)
		{
			estrConcatf(&temp, "Err: %s", strstr(p->pErrorString, "generated an error:") ? strstr(p->pErrorString, "generated an error:") + 19: p->pErrorString);
		}
		else if(p->ppStackTraceLines) 
		{
			estrConcatf(&temp, "%s", p->ppStackTraceLines[ErrorEntry_firstValidStackFrame(p)]->pFunctionName);
		}
		else
		{
			estrConcatf(&temp, "%s", "[No Stack]");	
		}
		estrConcatf(&s, "\"%s\",", temp);

		//estrConcatf(&s, "\"");
		//switch(p->eType)
		//{
		//	xcase ERRORDATATYPE_ERROR:
		//{
		//	if(p->pErrorString)
		//	{
		//		estrCopy2(&temp, p->pErrorString);
		//		estrReplaceOccurrences(&temp, "\"", "\"\"");
		//		estrConcatf(&s, "Err: %s", temp);
		//	}
		//}
		//xcase ERRORDATATYPE_ASSERT:
		//case ERRORDATATYPE_FATALERROR:
		//	{
		//		if(p->pExpression)
		//		{
		//			estrCopy2(&temp, p->pExpression);
		//			estrReplaceOccurrences(&temp, "\"", "\"\"");
		//			estrConcatf(&s, "Expr: %s", temp);
		//		}
		//		if(p->pErrorString)
		//		{
		//			estrCopy2(&temp, p->pErrorString);
		//			estrReplaceOccurrences(&temp, "\"", "\"\"");
		//			estrConcatf(&s, "%sErr: %s", NL_ON_NONEMPTY_TEMP, temp);
		//		}


		//		if(eaSize(&p->ppStackTraceLines) > 0)
		//			estrConcatf(&s, "\nStack:\n");
		//		// Fall through
		//	}
		//case ERRORDATATYPE_CRASH:
		//	{
		//		estrCopy2(&temp, "");
		//		EARRAY_FOREACH_BEGIN(p->ppStackTraceLines, j);
		//		{
		//			if(j==3)
		//				break;

		//			estrConcatf(&temp, "%s%s()", NL_ON_NONEMPTY_TEMP, p->ppStackTraceLines[j]->pFunctionName);
		//		}
		//		EARRAY_FOREACH_END;
		//		estrConcatf(&s, "%s", temp);
		//	}
		//};
		//estrConcatf(&s, "\"");

		estrConcatf(&s, "\n");
	}
	EARRAY_FOREACH_END;

	if(stricmp(dateStart, UNSPECIFIED_TIME) && stricmp(dateEnd, UNSPECIFIED_TIME)) 
		estrPrintf(&temp, "ETReport-%s-%s.csv", dateStart, dateEnd);
	else
		estrPrintf(&temp, "ETReport.csv");
	estrReplaceOccurrences(&temp, ":", "");
	estrReplaceOccurrences(&temp, "/", "");
	estrReplaceOccurrences(&temp, " ", "_");
	httpSendAttachment(link, temp, s, (U32)estrLength(&s));
}

static const char *copyPasteHeader = // %s %s
"<style>\n"
"<!--\n"
".astsummarytd  { vertical-align: top; }\n"
".errsummarytd  { vertical-align: top; }\n"
".cshsummarytd  { vertical-align: top; }\n"
".cplsummarytd  { vertical-align: top; }\n"
".gmbgsummarytd { vertical-align: top; }\n"
".summaryheadtd { font-weight: 900; }\n"
"-->\n"
"</style>\n"
"<b>ET Report: [%s - %s]</b>"
"\n";

void wiDisplayCopyPaste(NetLink *link, SearchData *search, ErrorEntry **entries)
{
	static char *s = NULL;
	char dateStart[1024] = UNSPECIFIED_TIME;
	char dateEnd[1024] = UNSPECIFIED_TIME;

	if(search->uFlags & SEARCHFLAG_EXACT_STARTDATE)
		timeMakeLocalDateStringFromSecondsSince2000(dateStart, search->uExactStartTime);
	if(search->uFlags & SEARCHFLAG_EXACT_ENDDATE)
		timeMakeLocalDateStringFromSecondsSince2000(dateEnd,   search->uExactEndTime);

	estrPrintf(&s, FORMAT_OK(copyPasteHeader), dateStart, dateEnd);

	appendSummaryHeader(&s, STF_NO_SEEN|STF_SHORT_VERSION|STF_SINGLELINE_STACK|STF_NO_WIDTH);
	EARRAY_FOREACH_BEGIN(entries, i);
	{
		ErrorEntry *p = entries[i];
		wiAppendSummaryTableEntry(p, &s, NULL, false, 0, 0, p->iTotalCount, STF_NO_SEEN|STF_SHORT_VERSION|STF_SINGLELINE_STACK|STF_SINGLELINE_JIRA);
	}
	EARRAY_FOREACH_END;
	estrConcatf(&s, "</table>\n");

	httpSendBytes(link, s, estrLength(&s));
}

static void appendExactMatchSummaryHeader(char **estr, SummaryTableFlags flags)
{
	const char *width = "width=\"100%\"";
	if(flags & STF_NO_WIDTH)
		width = "";

	estrConcatf(estr,
		"<table border=1 %s class=\"summarytable\" cellpadding=3 cellspacing=0>"
		"<tr>"
		"<td align=right class=\"summaryheadtd\">Index ID</td>"
		"<td align=right class=\"summaryheadtd\">File Path</td>"
		"<td align=right class=\"summaryheadtd\">ErrorEntry ID</td>"
		"<td align=right class=\"summaryheadtd\">Time Seen</td>", width);
	estrConcatf(estr,
		"</tr>\n");
}
static void appendExactMatchEntry(char **estr, SearchExactMatch *pMatch, const char *reportID)
{
	// getSummaryPrefix();
	// TODO shows them all as errors right now
	char datetime[256];
	timeMakeLocalDateStringFromSecondsSince2000(datetime, pMatch->uTime);
	estrConcatf(estr,
		"<tr>"
		"<td align=right class=\"errsummarytd\">"
		"<a href=\"/report/%s?type=detail&id=%d\">%d</a>"
		"</td>"
		"<td align=right class=\"errsummarytd\">%s</td>"
		"<td align=right class=\"errsummarytd\">%d</td>"
		"<td align=right class=\"errsummarytd\">%s</td>"
		"</tr>\n", 
		reportID, pMatch->uIndexID, pMatch->uIndexID, 
		pMatch->filePath, pMatch->uID, datetime);
}
static void appendWebReportHeader(char **estr, ReportInfo *pReportInfo)
{
	char dateStart[1024] = UNSPECIFIED_TIME;
	char dateEnd[1024] = UNSPECIFIED_TIME;

	if(pReportInfo->search->uFlags & SEARCHFLAG_EXACT_STARTDATE)
		timeMakeLocalDateStringFromSecondsSince2000(dateStart, pReportInfo->search->uExactStartTime);
	if(pReportInfo->search->uFlags & SEARCHFLAG_EXACT_ENDDATE)
		timeMakeLocalDateStringFromSecondsSince2000(dateEnd,   pReportInfo->search->uExactEndTime);

	estrConcatf(estr, 
		"<table class=\"exacttable\">"
		"<tr><td><b>Exact Mode Export Options</b></a> - [%s]</td></tr>"
		"<tr><td style=\"text-align:center\">"
		"<br>"
		"<a href=\"/report/%s\">Back to main Exact Mode Web Report</a><br>"
		"<br>"
		"Time Range: [%s - %s]<br>"
		"<br>"
		"<br>"
		"</td></tr>"
		"</table>", pReportInfo->id, pReportInfo->id, dateStart, dateEnd);
}
static void wiDisplayMatches(NetLink *link, ReportInfo *pReportInfo)
{
	static char *s = NULL;
	int i, size;

	estrClear(&s);
	appendWebReportHeader(&s, pReportInfo);
	appendExactMatchSummaryHeader(&s, 0);
	size = eaSize(&pReportInfo->search->ppExactModeMatches);
	for (i=0; i<size; i++)
	{
		appendExactMatchEntry(&s, pReportInfo->search->ppExactModeMatches[i], pReportInfo->id);
	}
	httpSendWrappedString(link, s, NULL, NULL);
}

static void wiDisplayMatchDetail(NetLink *link, ReportInfo *pReportInfo, int iIndexID)
{
	static char *s = NULL;
	SearchExactMatch *match = eaIndexedGetUsingInt(&pReportInfo->search->ppExactModeMatches, iIndexID);

	estrClear(&s);
	if (match)
	{
		NOCONST(ErrorEntry) *pErrorEntry = StructCreateNoConst(parse_ErrorEntry);
		if(loadErrorEntry(match->uID, match->uFileIndex, pErrorEntry))
		{
			ETWeb_DumpDataToString(CONST_ENTRY(pErrorEntry), &s, wiGetFlags() | DUMPENTRY_FLAG_NO_DUMP_TOGGLES | DUMPENTRY_FLAG_NO_PROGRAMMER_REQUEST | DUMPENTRY_FLAG_NO_JIRA | DUMPENTRY_FLAG_FORCE_TRIVIASTRINGS);
		}
		StructDestroyNoConst(parse_ErrorEntry, pErrorEntry);
	}
	else
		estrPrintf(&s, "Could not find match.");
	// TODO(Theo) send ID
	httpSendWrappedString(link, s, NULL, NULL);
}

#include "WebReport_c_ast.c"
