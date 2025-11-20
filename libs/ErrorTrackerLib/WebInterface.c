#include "WebInterface.h"
#include "Backlog.h"
#include "textparser.h"
#include "file.h"
#include "earray.h"
#include "estring.h"
#include "timing.h"
#include "utils.h"
#include "sysutil.h"
#include "zutils.h"
#include "sock.h"
#include "net/net.h"
#include "HttpLib.h"
#include "HttpClient.h"
#include "net/netlink.h"
#include "htmlForms.h"
#include "objContainerIO.h"
#include "objTransactions.h"
#include "GenericHttpServing.h"

#include "ErrorTracker.h"
#include "ErrorEntry.h"
#include "ErrorTrackerLib.h"
#include "webinterface_c_ast.h"
#include "ETCommon/ETIncomingData.h"
#include "ETCommon/ETShared.h"
#include "ETCommon/ETDumps.h"
#include "ETCommon/ETWebCommon.h"

#include "Search.h"
#include "WebReport.h"
#include "wininclude.h"
#include <winsock.h>
#include <winsock2.h>
#include "blame.h"
#include "email.h"
#include "jira.h"
#include "Autogen/jira_h_ast.h"
#include "ErrorTrackerHandlerInfo.h"
#include "ErrorTrackerHandlerInfo_h_ast.h"
#include "objContainer.h"
#include "Organization.h"
#include "AutoGen/Search_h_ast.h"

#include "Autogen/HttpLib_h_ast.h"
#include "Autogen/HttpClient_h_ast.h"
#include "trivia_h_ast.h"

#include "AutoGen/ErrorTrackerLib_autotransactions_autogen_wrappers.h"

// Disabled for now, it stalls the ET way too long
//#define EXACTMODE_ENABLED

#define MAX_ASSIGNMENT_COUNT 50

// Non-NULL String
#define NNS(STR) ((STR)?STR:"")


static char *sHTMLPage = NULL; // for all the page requests

//How many asynchronous searches are currently running 
int giSearchIsRunning = 0;

extern ErrorTrackerSettings gErrorTrackerSettings;
extern char gErrorTrackerDBBackupDir[MAX_PATH];
static StashTable searchTable;

static bool httpIsJiraAuthenticated(NetLink *link, const char *url, UrlArgumentList *arglist, const char *pReferer, 
									CookieList *pCookies, bool *pbSetCookies);
static void wiAuthenticate(NetLink *link, const char *pUrl, UrlArgumentList *args, const char *pReferer, CookieList *pCookies);

// -------------------------------------------------------------------------------------

static requestHandlerFunc spHandlerFunc = NULL;

void errorTrackerLibSetRequestHandlerFunc(requestHandlerFunc pFunc)
{
	spHandlerFunc = pFunc;
}

static const char *wiGetTitle(ErrorEntry *pEntry)
{
	static char sEntryBuffer[32];
	if (!pEntry)
		return NULL;
	if (pEntry->pJiraIssue && pEntry->pJiraIssue->key)
		sprintf(sEntryBuffer, "ET #%d [%s]", pEntry->uID, pEntry->pJiraIssue->key);
	else
		sprintf(sEntryBuffer, "ET #%d", pEntry->uID);
	return sEntryBuffer;
}

// -------------------------------------------------------------------------------------

// Source Controlled
/*char gWebInterfaceRootDir[MAX_PATH] = "C:\\Core\\data\\server\\ErrorTracker\\WebRoot\\";
AUTO_CMD_STRING(gWebInterfaceRootDir, webInterfaceRootDir);

// Not Source Controlled
char gWebInterfaceAltRootDir[MAX_PATH] = "C:\\Core\\ErrorTracker\\WebRoot\\";
AUTO_CMD_STRING(gWebInterfaceAltRootDir, webInterfaceAltRootDir);*/

char gDefaultPage[MAX_PATH] = "/search";

// -------------------------------------------------------------------------------------

void formAppendAuthenticate(char **estr, int iEditSize, const char *pUserVarName, const char *pPasswordVarName)
{
	estrConcatf(estr,
		"<table>"
		"<tr><td colspan=\"2\" style=\"text-align:center\"><b>Login to Jira</b></td></tr>"
		"<tr>\n"
		"<td>Username:</td><td>\n");
	formAppendEdit(estr, iEditSize, pUserVarName, "");
	estrConcatf(estr,
		"</td>\n"
		"<tr>\n"
		"<td>Password:</td><td>\n"
		"<input class=\"formdata\" type=\"password\" size=\"%d\" name=\"%s\">\n"
		"</td></tr></table>",

		iEditSize,
		pPasswordVarName);
}

void appendParseTable(char **estr, U32 uDescriptorID, char **name, const char *pParseString)
{
	return; // does nothing
}

void wiCharacterInfo(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	httpSendClientError(link, "This feature has been removed.");
}

void appendCallStackSummary(char **estr, ErrorEntry *p, int iMaxCallstackCount)
{
	int i;
	int iCount = eaSize(&p->ppStackTraceLines);

	if(iCount == 0)
	{
		estrConcatf(estr, "<span class=\"summarycallstack\">[No Stack]</span>");
		return;
	}

	if(iMaxCallstackCount == 1)
	{
		estrConcatf(estr, "<span class=\"summarycallstack\">%s</span>", p->ppStackTraceLines[ErrorEntry_firstValidStackFrame(p)]->pFunctionName);
		return;
	}

	if(iMaxCallstackCount && (iCount > iMaxCallstackCount))
	{
		iCount = iMaxCallstackCount; 
	}

	estrConcatf(estr, "<span class=\"summarycallstack\">Stack: ");
	for(i=0; i<iCount; i++)
	{
		if(i)
			estrConcatf(estr, "<br>");

		estrConcatf(estr, "%s()", p->ppStackTraceLines[i]->pFunctionName);
	}
	estrConcatf(estr, "</span>");
}

void appendReportHeader(char **estr, SummaryTableFlags flags, char *reportID, SearchData *sd)
{
	const char *width = "width=\"100%\"";
	if(flags & STF_NO_WIDTH)
		width = "";

	estrConcatf(estr,
		"<table border=1 %s class=\"summarytable\" cellpadding=3 cellspacing=0>"
		"<tr>", width);

	estrConcatf(estr, "<td align=right class=\"summaryheadtd\"><a href=\"/report/%s?sort=%d&descend=%d\">ID</a></td>",
					reportID, SORTORDER_ID, (sd->eSortOrder == SORTORDER_ID)?!sd->bSortDescending:sd->bSortDescending);
	estrConcatf(estr, "<td align=right class=\"summaryheadtd\">Type</td>"
		"<td align=right class=\"summaryheadtd\">Executable</td>");
	estrConcatf(estr, "<td align=right class=\"summaryheadtd\"><a href=\"/report/%s?sort=%d&descend=%d\">Count</a></td>",
		reportID, SORTORDER_COUNT, (sd->eSortOrder == SORTORDER_COUNT)?!sd->bSortDescending:sd->bSortDescending);
	if(!(flags & STF_NO_SEEN))
	{
		estrConcatf(estr,
			"<td align=right class=\"summaryheadtd\">First Seen</td>"
			"<td align=right class=\"summaryheadtd\">Last Seen</td>");
	}
	if(flags & STF_VERSION || flags & STF_SHORT_VERSION)
	{
		estrConcatf(estr,
			"<td align=right class=\"summaryheadtd\">Version</td>");
	}
	estrConcatf(estr,
		"<td align=right class=\"summaryheadtd\">Product</td>"
		"<td align=right class=\"summaryheadtd\">Jira</td>");

	if(!(flags & STF_NO_INFO))
	{
		estrConcatf(estr,
			"<td align=left class=\"summaryheadtdend\">Info</td>");
	}
	estrConcatf(estr,
		"</tr>\n");
}

void appendSummaryHeader(char **estr, SummaryTableFlags flags)
{
	const char *width = "width=\"100%\"";
	if(flags & STF_NO_WIDTH)
		width = "";

	estrConcatf(estr,
		"<table border=1 %s class=\"summarytable\" cellpadding=3 cellspacing=0>"
		"<tr>"
		"<td align=right class=\"summaryheadtd\">ID</td>"
		"<td align=right class=\"summaryheadtd\">Type</td>"
		"<td align=right class=\"summaryheadtd\">Executable</td>"
		"<td align=right class=\"summaryheadtd\">Count</td>", width);
	if(!(flags & STF_NO_SEEN))
	{
		estrConcatf(estr,
			"<td align=right class=\"summaryheadtd\">First Seen</td>"
			"<td align=right class=\"summaryheadtd\">Last Seen</td>");
	}
	if(flags & STF_VERSION || flags & STF_SHORT_VERSION)
	{
		estrConcatf(estr,
			"<td align=right class=\"summaryheadtd\">Version</td>");
	}
	estrConcatf(estr,
		"<td align=right class=\"summaryheadtd\">Product</td>"
		"<td align=right class=\"summaryheadtd\">Jira</td>");

	if(!(flags & STF_NO_INFO))
	{
		estrConcatf(estr,
			"<td align=left class=\"summaryheadtdend\">Info</td>");
	}
	estrConcatf(estr,
		"</tr>\n");
}

static const char * getSummaryPrefix(ErrorEntry *p)
{
	switch(p->eType)
	{
	case ERRORDATATYPE_FATALERROR: 
	case ERRORDATATYPE_ASSERT:  return "ast";
	case ERRORDATATYPE_XPERF:
	case ERRORDATATYPE_ERROR:   return "err";
	case ERRORDATATYPE_CRASH:   return "csh";
	case ERRORDATATYPE_COMPILE: return "cpl";
	case ERRORDATATYPE_GAMEBUG: return "gmbg";
	};

	return "";
}

void wiAppendSummaryTableEntry(ErrorEntry *p, char **estr, const char * const * ppExecutableNames, bool bAssignmentMode, int assignmentID, int iMaxCallstackCount, int count, SummaryTableFlags flags)
{
	static char * pEscaped = NULL;
	char datetime[256];
	const char *prefix = getSummaryPrefix(p);
	const char *jiraSeparator = "<br>";

	if(flags & STF_SINGLELINE_JIRA)
		jiraSeparator = " / ";

	if(!ppExecutableNames)
		ppExecutableNames = p->ppExecutableNames;

	estrConcatf(estr, "<tr>\n");

	estrConcatf(estr, "<td class=\"%ssummarytd\">[<a href=\"/detail?id=%d\">%d</a>]</td>\n", prefix, p->uID, p->uID);
	estrConcatf(estr, "<td class=\"%ssummarytd\">%s</td>\n", prefix, ErrorDataTypeToString(p->eType));

	estrConcatf(estr, "<td class=\"%ssummarytd\" align=left><div class=\"executablenames\">", prefix);
	EARRAY_CONST_FOREACH_BEGIN(ppExecutableNames, i, s);
	{
		estrConcatf(estr, "%s%s", i ? "<br>" : "", ppExecutableNames[i]);
	}
	EARRAY_FOREACH_END;
	estrConcatf(estr, "&nbsp;</div></td>\n");

	estrConcatf(estr, "<td class=\"%ssummarytd\">%d</td>\n", prefix, count);

	if(!(flags & STF_NO_SEEN))
	{
		timeMakeLocalDateStringFromSecondsSince2000(datetime, p->uFirstTime);
		estrConcatf(estr, "<td class=\"%ssummarytd\">%s</td>\n", prefix, datetime);
		timeMakeLocalDateStringFromSecondsSince2000(datetime, p->uNewestTime);
		estrConcatf(estr, "<td class=\"%ssummarytd\">%s</td>\n", prefix, datetime);
	}

	if(flags & STF_SHORT_VERSION)
	{
		estrConcatf(estr, "<td class=\"%ssummarytd\" align=left><div class=\"executablenames\">", prefix);
		EARRAY_CONST_FOREACH_BEGIN(p->ppVersions, i, s);
		{
			if (p->ppVersions[i])
			{
				const char *version = strchr(p->ppVersions[i], ' ');
				int len = 0;
				if (version)
					len = version - p->ppVersions[i];
				if (len)
				{
					estrConcatf(estr, "%s", i ? "<br>" : "");
					estrConcat(estr, p->ppVersions[i], len);
				}
				else
					estrConcatf(estr, "%s%s", i ? "<br>" : "", p->ppVersions[i]);
			}
		}
		EARRAY_FOREACH_END;
		estrConcatf(estr, "&nbsp;</div></td>\n");
	}
	else if(flags & STF_VERSION)
	{
		estrConcatf(estr, "<td class=\"%ssummarytd\" align=left><div class=\"executablenames\">", prefix);
		EARRAY_CONST_FOREACH_BEGIN(p->ppVersions, i, s);
		{
			estrConcatf(estr, "%s%s", i ? "<br>" : "", p->ppVersions[i]);
		}
		EARRAY_FOREACH_END;
		estrConcatf(estr, "&nbsp;</div></td>\n");
	}

	// Product names
	estrConcatf(estr, "<td class=\"%ssummarytd\">", prefix);
	if(eaSize(&p->eaProductOccurrences) == 0)
	{
		estrConcatf(estr, "&nbsp;");
	}
	else
	{
		EARRAY_CONST_FOREACH_BEGIN(p->eaProductOccurrences, i, s);
		{
			if(i != 0)
				estrConcatf(estr, " / ");
			estrConcatf(estr, "%s (%u)", p->eaProductOccurrences[i]->key, p->eaProductOccurrences[i]->uCount);
		}
		EARRAY_FOREACH_END;
	}
	estrConcatf(estr, "</td>\n");

	// -------------------------------------------------------------------------------------
	// Assignment mode code
	{
		char tempVarName[128];

		estrConcatf(estr, "<td class=\"%ssummarytd\">", prefix);

		if(p->pJiraIssue)
		{
			estrConcatf(estr, "<span class=\"jirainfo\">[<a target=\"_new\" "
				"href=\"http://%s:%d/browse/%s\">%s</a>]%s%s%s%s</span>", 
				getJiraHost(), getJiraPort(), 
				p->pJiraIssue->key, 
				p->pJiraIssue->key, 
				jiraSeparator,
				(p->pJiraIssue->assignee) ? p->pJiraIssue->assignee : "unknown",
				jiraSeparator,
				jiraGetStatusString(p->pJiraIssue->status)
				);
		}
		else
		{
			if(bAssignmentMode)
			{
				estrConcatf(estr, "<table><tr><td>");

				sprintf_s(SAFESTR(tempVarName), "assignee%d", assignmentID);
				formAppendJiraUsers(estr, tempVarName);

				estrConcatf(estr, "</td><td>");

				sprintf_s(SAFESTR(tempVarName), "project%d", assignmentID);
				formAppendJiraProjects(estr, tempVarName);

				sprintf_s(SAFESTR(tempVarName), "id%d", assignmentID);
				formAppendHiddenInt(estr, tempVarName, p->uID);

				estrConcatf(estr, "</td><tr></table>\n");
			}
			else
			{
				estrConcatf(estr, "---");
			}
		}

		estrConcatf(estr, "</td>\n");
	}
	// -------------------------------------------------------------------------------------

	if (flags & STF_SINGLELINE_STACK)
	{
		estrConcatf(estr, "<td class=\"%ssummarytdend\">\n", prefix);
		if(p->pErrorString)
		{
			estrCopyWithHTMLEscaping(&pEscaped, p->pErrorString, false);
			pEscaped = strstr(pEscaped, "generated an error:") ? strstr(pEscaped, "generated an error:") + 19: pEscaped;
			estrConcatf(estr, "<span class=\"summaryerr\">Err: %s</span><br>", pEscaped);
		}
		else
		{
			appendCallStackSummary(estr, p, 1);
		}
		estrConcatf(estr, "&nbsp;</td>");
	}
	else if(!(flags & STF_NO_INFO))
	{
		estrConcatf(estr, "<td class=\"%ssummarytdend\">\n", prefix);

		switch(p->eType)
		{
		xcase ERRORDATATYPE_ERROR:
		case ERRORDATATYPE_XPERF:
		{
			if(p->pErrorString)
			{
				estrCopyWithHTMLEscaping(&pEscaped, p->pErrorString, false);
				estrConcatf(estr, "<span class=\"summaryerr\">Err: %s</span><br>", pEscaped);
			}
		}
		xcase ERRORDATATYPE_COMPILE:
		{
			if((p->pDataFile != NULL) && (p->pLastBlamedPerson != NULL))
			{
				estrConcatf(estr, "<span class=\"summaryfile\">%s (%s)</span><br>", p->pDataFile, p->pLastBlamedPerson);
			}
			if(p->pErrorString)
			{
				estrCopyWithHTMLEscaping(&pEscaped, p->pErrorString, false);
				estrConcatf(estr, "<span class=\"summaryerr\">Err: %s</span><br>", pEscaped);
			}
		}
		xcase ERRORDATATYPE_ASSERT:
		case ERRORDATATYPE_FATALERROR:
			{
				if(p->pExpression)
				{
					estrConcatf(estr, "<span class=\"summaryexpr\">Expr: %s</span><br>", p->pExpression);
				}
				if(p->pErrorString)
				{
					estrCopyWithHTMLEscaping(&pEscaped, p->pErrorString, false);
					estrConcatf(estr, "<span class=\"summaryerr\">Err: %s</span><br>", pEscaped);
				}
				appendCallStackSummary(estr, p, iMaxCallstackCount);
			}
			xcase ERRORDATATYPE_CRASH:
			appendCallStackSummary(estr, p, iMaxCallstackCount);
			xcase ERRORDATATYPE_GAMEBUG:
			{
				estrConcatf(estr, "<span class=\"summaryexpr\">Category: %s</span><br>", p->pCategory);
				estrCopyWithHTMLEscaping(&pEscaped, p->pErrorString, false);
				estrConcatf(estr, "<span class=\"summaryerr\">Err: %s</span><br>", pEscaped);

			}
		}

		estrConcatf(estr, "&nbsp;</td>");
	}

	estrConcatf(estr, "</tr>\n");
}

void wiList(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	static char *s = NULL; // Reuse this estring so that its growth stabilizes after a few hits
	ErrorEntry *p = NULL;
	SearchData sd = {0};

	// Just show a basic sorted-by-newest-first search
	sd.uFlags           = SEARCHFLAG_SORT;
	sd.eSortOrder       = SORTORDER_ID;
	sd.bSortDescending  = true;
	sd.bHideProgrammers = true;

	estrPrintf(&s, "Full List (filtering out programmer-only crashes):<br>\n<br>\n");
	appendSummaryHeader(&s, STF_DEFAULT);

	p = searchFirst(errorTrackerLibGetCurrentContext(), &sd);
	while(p != NULL)
	{
		wiAppendSummaryTableEntry(p, &s, NULL, false, 0, 0, p->iTotalCount, STF_DEFAULT);
		p = searchNext(errorTrackerLibGetCurrentContext(), &sd);
	}
	searchEnd(&sd);

	estrConcatf(&s, "</table>\n");

	httpSendWrappedString(link, s, NULL, pCookies);
}

void wiDetailTrivia(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	static char *s = NULL; // Reuse this estring so that its growth stabilizes after a few hits
	int i;
	int iUniqueID = 0;
	ErrorEntry *pEntry = NULL;

	estrCopy2(&s, "");

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
			break;
		}
	}

	if(iUniqueID == 0)
	{
		httpSendClientError(link, "Please supply an 'id' to get any details. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);

	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}

	if (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_WRITE_TRIVIA_DISK && pEntry->pTriviaFilename)
	{
		TriviaListMultiple trivia = {0};
		int size;
		ParserReadTextFile(pEntry->pTriviaFilename, parse_TriviaListMultiple, &trivia, 0);

		size = eaSize(&trivia.triviaLists);
		for (i=0; i<size; i++)
		{
			appendTriviaStrings(&s, trivia.triviaLists[i]->triviaDatas);
		}
		StructDeInit(parse_TriviaListMultiple, &trivia);
	}
	else if (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_LOG_TRIVIA)
	{
		// This fires up a new thread to read a bunch of rawdata files
		const char *id = startTriviaReport(iUniqueID, linkGetIp(link));
		if(id)
		{
			static char *reportURL = NULL;
			estrPrintf(&reportURL, "/triviareport/%s", id);
			httpRedirect(link, reportURL);
			return;
		}
	}
	else if (pEntry->triviaOverview.ppTriviaItems)
	{
		appendTriviaOverview(&s, (CONST_EARRAY_OF(TriviaOverviewItem)) pEntry->triviaOverview.ppTriviaItems);
	}
	httpSendWrappedString(link, s, wiGetTitle(pEntry), pCookies);
}

void wiDetail(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	static char *s = NULL; // Reuse this estring so that its growth stabilizes after a few hits
	int i;
	int iUniqueID = 0;
	ErrorEntry *pEntry = NULL;
	int iFlags;

	estrCopy2(&s, "");

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
			break;
		}
	}

	if(iUniqueID == 0)
	{
		httpSendClientError(link, "Please supply an 'id' to get any details. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);

	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}

	if (ErrorEntry_IsGenericCrash(pEntry) && !pEntry->uMergeID)
	{
		estrConcatf(&s, "<div class=\"warning\">!!! This is a Generic Crash !!!</div>");
		estrConcatf(&s, "<div class=\"bold\">Recalculate the Hash</div>"
			"This will fail if the hash matches a generic one - use the \"Un-Genericify\" option below to remove the hash from the Generics table.<br>"
			"<a href=\"/recalchash?id=%d\">Force Hash Recalculation</a><br><br>\n", iUniqueID);

		estrConcatf(&s, "<div class=\"bold\">Un-Genericify</div>Remove the hash for this entry from the Generics table "
			"so that new, incoming instances of this crash will be merged in.<br>");
		estrConcatf(&s, "<a href=\"/ungenericify?id=%d\">Remove Generic Tag</a><br><br>\n", iUniqueID);

		estrConcatf(&s, "<div class=\"bold\">Merge and Delete Entry</div> Irreversible!\n");
		formAppendStart(&s, "/mergedelete", "GET", "mergedel", NULL);
		estrConcatf(&s, "Target ID: ");
		formAppendHiddenInt(&s, "id", iUniqueID);
		formAppendEditInt(&s, 7, "target", 0);
		formAppendSubmit(&s, "Confirm");
		formAppendEnd(&s);

		estrConcatf(&s, "<div class=\"bold\">Delete</div> Also irreversible<br>\n"
			"<a href=\"/delete?id=%d\">Delete Entry</a><br>\n", iUniqueID);

		estrConcatf(&s, "<div class=\"warning\">--- End Generic Crash Options---</div>");
	}
	iFlags = wiGetFlags();
	if (hasCookie(pCookies, "hidecomments"))
		iFlags |= DUMPENTRY_FLAG_NO_COMMENTS;
	ETWeb_DumpDataToString(pEntry, &s, iFlags);
	httpSendWrappedString(link, s, wiGetTitle(pEntry), pCookies);
}

//Helper function for information shared between entries with dumps and those without dumps.
void ErrorInfoToString(char **s, ErrorEntry *pEntry)
{
	char datetime[256];
	int iDaysAgo;
	
	timeMakeLocalDateStringFromSecondsSince2000(datetime, pEntry->uFirstTime);

	estrConcatf(s, "<div class=\"entry\">");

	// ---------------------------------------------------------------
	estrConcatf(s, "<span class=\"heading\">Time of Crash: </span>");
	estrConcatf(s, "<span class=\"firstseen\">%s</span>\n", datetime);
	estrConcatf(s, "<br>\n");
	// ---------------------------------------------------------------

	// ---------------------------------------------------------------
	estrConcatf(s, "<span class=\"heading\">Client Count: </span>");
	estrConcatf(s, "<span class=\"count\">%d</span>\n", pEntry->iTotalClients);
	estrConcatf(s, "<br>\n");
	// ---------------------------------------------------------------

	// ---------------------------------------------------------------
	if(eaSize(&pEntry->ppVersions) > 0)
	{
		estrConcatf(s, "<span class=\"heading\">Version: </span>");
		estrConcatf(s, "<span class=\"versionrange\">");
		appendVersionLink(s, pEntry->ppVersions[0]);
		estrConcatf(s, "</span>\n");
		estrConcatf(s, "<br>\n");
	}
	// ---------------------------------------------------------------

	// ---------------------------------------------------------------
	estrConcatf(s, "<span class=\"heading\">Platform: </span>");
	estrConcatf(s, "<span class=\"ppPlatformCounts\">");
	EARRAY_CONST_FOREACH_BEGIN(pEntry->ppPlatformCounts, i, n);
	{
		estrConcatf(s, "%s\n", getPlatformName(pEntry->ppPlatformCounts[i]->ePlatform));
	}
	EARRAY_FOREACH_END;
	estrConcatf(s, "</span><br>\n");
	// ---------------------------------------------------------------

	// ---------------------------------------------------------------
	estrConcatf(s, "<span class=\"heading\">Product: </span>");
	estrConcatf(s, "<span class=\"ppProductNames\">\n");
	EARRAY_CONST_FOREACH_BEGIN(pEntry->eaProductOccurrences, i, n);
	{
		estrConcatf(s, "%s (%u)\n", pEntry->eaProductOccurrences[i]->key, pEntry->eaProductOccurrences[i]->uCount);
	}
	EARRAY_FOREACH_END;
	estrConcatf(s, "</span><br>\n");
	// ---------------------------------------------------------------

	// ---------------------------------------------------------------
	estrConcatf(s, "<span class=\"heading\">Executable: </span>");
	estrConcatf(s, "<span class=\"ppExecutableNames\">\n");
	if (pEntry->ppExecutableCounts)
	{
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppExecutableNames, i, n);
		{
			estrConcatf(s, "%s: %i\n", pEntry->ppExecutableNames[i], pEntry->ppExecutableCounts[i]);
		}
		EARRAY_FOREACH_END;
	}
	else
	{
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppExecutableNames, i, n);
		{
			estrConcatf(s, "%s\n", pEntry->ppExecutableNames[i]);
		}
		EARRAY_FOREACH_END;
	}
	
	estrConcatf(s, "</span><br>\n");
	// ---------------------------------------------------------------

	// ---------------------------------------------------------------
	estrConcatf(s, "<span class=\"heading\">User: </span>");
	estrConcatf(s, "<span class=\"ppUserNames\">\n");
	EARRAY_CONST_FOREACH_BEGIN(pEntry->ppUserInfo, i, n);
	{
		estrConcatf(s, "%s\n", pEntry->ppUserInfo[i]->pUserName);
	}
	EARRAY_FOREACH_END;
	estrConcatf(s, "</span><br>\n");
	// ---------------------------------------------------------------

	// ---------------------------------------------------------------
	estrConcatf(s, "<span class=\"heading\">Days Ago: </span>");
	estrConcatf(s, "<span class=\"ppDayCounts\">\n");
	iDaysAgo = calcElapsedDays(pEntry->uFirstTime, timeSecondsSince2000());
	estrConcatf(s, "%d", iDaysAgo);
	estrConcatf(s, "</span><br>\n");
	// ---------------------------------------------------------------
}

void wiDumpInfo(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	static char *s = NULL; // Reuse this estring so that its growth stabilizes after a few hits
	int i;
	int iUniqueID = 0;
	int index = -1;
	ErrorEntry *pEntry = NULL;
	DumpData *pDumpData = NULL;
	char url[512];
	bool bHasRawData = false;

	estrCopy2(&s, "");

	for(i=0;i<count;i++) 
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if(!stricmp(args[i], "index"))
		{
			index = atoi(values[i]);
		}
	}

	if(iUniqueID == 0)
	{
		httpSendClientError(link, "Please supply an 'id' to get any details. Thanks.");
		return;
	}

	if(index == -1)
	{
		httpSendClientError(link, "Please supply a dump 'index' to get any details. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);

	if(!pEntry)
	{
		httpSendClientError(link, "ID not found.");
		return;
	}

	if(index < eaSize(&pEntry->ppDumpData))
	{
		pDumpData = pEntry->ppDumpData[index];
	}

	if(!pDumpData)
	{
		httpSendClientError(link, "Dump index not found.");
		return;
	}

	if (pDumpData->uFlags & DUMPDATAFLAGS_MOVED)
	{
		if (pDumpData->uMovedID && pDumpData->uMovedIndex)
			estrConcatf(&s, "Dump reprocessed and moved to "
				"<a href=\"/dumpinfo?id=%d&index=%d\">"
				"ET ID #%d</a>.", 
				pDumpData->uMovedID, pDumpData->uMovedIndex-1, pDumpData->uMovedID);
		else if (pDumpData->uMovedID)
			estrConcatf(&s, "Dump reprocessed and moved to "
				"<a href=\"/detail?id=%d\">"
				"ET ID #%d</a>.", 
				pDumpData->uMovedID, pDumpData->uMovedID);
		else
			estrConcatf(&s, "Dump reprocessed and moved.");
		httpSendWrappedString(link, s, wiGetTitle(pEntry), pCookies);
		return;
	}

	estrConcatf(&s, "<span class=\"heading\">Dump Info: </span><br><br>");

	if (pDumpData->uFlags & DUMPDATAFLAGS_REJECTED)
	{
		estrConcatf(&s, "Dump was rejected [multiple dumps received for this simultaneously].");
	}
	else if (pDumpData->uFlags & DUMPDATAFLAGS_DELETED)
	{
		estrConcatf(&s, "Dump was deleted to free up space [too old].");
	}
	else if (pDumpData->bWritten)
	{
		char linkpath[MAX_PATH];
		calcReadDumpPath(SAFESTR(url), pEntry->uID, pDumpData->iDumpIndex, (pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP));

		calcWriteDumpPath(SAFESTR(linkpath), pEntry->uID, pDumpData->iDumpIndex, (pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP));

		if (!fileExists(linkpath)) 
		{		
			sprintf(url, "%s/%d-%d.%s", strrchr(gErrorTrackerSettings.pDumpDir, '/'),
			pEntry->uID, pDumpData->iDumpIndex, 
			(pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP) ? "dmp" : "mdmp");
		}

		estrConcatf(&s, "[<a href=\"/eth/%d-%d.eth\">Open In Debugger</a>]<br>\n", pEntry->uID, index);

		estrConcatf(&s, "[<a href=\"%s.gz\">Download GZipped File</a>]<br>\n", url);

		if(pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP)
		{
			estrConcatf(&s, "[Uncompressed download of full dump not allowed]<br>\n");
		}
		else
		{
			estrConcatf(&s, "[<a href=\"%s\">Download Uncompressed file</a>]<br>\n", url);
		}
	}
	else
	{
		estrConcatf(&s, "Dump was not received%s<br>", pDumpData->bCancelled ? " - Cancelled by user" : "");
	}

	if (pDumpData->bMiniDumpWritten)
	{
		char linkpath[MAX_PATH];
		estrConcatf(&s, "<div class=\"heading\">Mini Dump: </div>");
		calcReadDumpPath(SAFESTR(url), pEntry->uID, pDumpData->iMiniDumpIndex, false);

		calcWriteDumpPath(SAFESTR(linkpath), pEntry->uID, pDumpData->iMiniDumpIndex, false);

		if (!fileExists(linkpath)) 
		{		
			sprintf(url, "%s/%d-%d.mdmp", strrchr(gErrorTrackerSettings.pDumpDir, '/'),
			pEntry->uID, pDumpData->iMiniDumpIndex);
		}

		estrConcatf(&s, "[<a href=\"%s.gz\">Download GZipped File</a>]<br>\n", url);
		estrConcatf(&s, "[<a href=\"%s\">Download Uncompressed file</a>]<br>\n", url);
	}

	// ---------------------------------------------------------------

	if (pDumpData->uPreviousID)
	{
			estrConcatf(&s, "<br><span class=\"heading\">Dump Moved From <a href=\"/detail?id=%d\">ET ID #%d</a>. </span><br>", 
				pDumpData->uPreviousID, pDumpData->uPreviousID);
	}

	estrConcatf(&s, "<br><span class=\"heading\">User Description of Crash: </span><br>");
	estrConcatf(&s, "<pre>%s</pre>\n", pDumpData->pDumpDescription ? pDumpData->pDumpDescription : "No User Description");
	// ---------------------------------------------------------------

	ErrorInfoToString(&s, pDumpData->pEntry);

	// ---------------------------------------------------------------
	estrConcatf(&s, "<hr size=1><br><b>Raw Data From Crash:</b><br><br>\n");

	if(pDumpData->iETIndex)
	{
		NOCONST(ErrorEntry) rawEntry = {0};
		if(loadErrorEntry(pDumpData->pEntry->uID, pDumpData->iETIndex, &rawEntry))
		{
			bHasRawData = true;
			eaDestroyStructNoConst(&rawEntry.ppStackTraceLines, parse_StackTraceLine);
			eaCopyStructs(&pDumpData->pEntry->ppStackTraceLines, &((StackTraceLine**)(rawEntry.ppStackTraceLines)), parse_StackTraceLine);
			estrConcatf(&s, "[<a href=\"/xmlsingle?id=%d&index=%d\">Crash Index %d (Raw XML)</a>]<br>\n", pDumpData->pEntry->uID, pDumpData->iETIndex, pDumpData->iETIndex);
			ETWeb_DumpDataToString(CONST_ENTRY(&rawEntry), &s, DUMPENTRY_FLAG_NO_UID|DUMPENTRY_FLAG_NO_JIRA|DUMPENTRY_FLAG_NO_PROGRAMMER_REQUEST|DUMPENTRY_FLAG_FORCE_TRIVIASTRINGS|DUMPENTRY_FLAG_NO_DUMP_TOGGLES);
			StructDeInitNoConst(parse_ErrorEntry, &rawEntry);
		}
	}

	if(!bHasRawData)
	{
		estrConcatf(&s, "(No RawData known for this dump [%d] -- [<a href=\"/fixupdumpdata?id=%d&index=%d\">Fixup</a>])<br>", 
			pDumpData->iETIndex, iUniqueID, index);
	}
	// ---------------------------------------------------------------

	if (ErrorEntry_IsGenericCrash(pEntry))
	{
		estrConcatf(&s, "<div class=\"warning\">!!! This is a Generic Crash !!!</div>\n"
			"Dumps Reprocessed for this that result in new entries will also end up as \"generic\" crashes, "
			"until the hash is recalculated or the new entry is manually merged into another entry.\n");
	}
	formAppendStart(&s, "/reprocessdump", "POST", "dump", NULL);
	formAppendHiddenInt(&s, "id", pEntry->uID);
	formAppendHiddenInt(&s, "dumpidx", index);
	formAppendSubmit(&s, "Reprocess Dump Stack");
	formAppendEnd(&s);

	sprintf(url, "/detail?id=%d", pEntry->uID);
	estrConcatf(&s, "<br>[<a href=\"%s\">Back to Crash Details</a>]\n", url);

	estrConcatf(&s, "</div>\n");

	estrConcatf(&s, "\n<br><hr size=1>\n<br>\n");

	httpSendWrappedString(link, s, wiGetTitle(pEntry), pCookies);
}

void wiErrorInfo(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	static char *s = NULL; // Reuse this estring so that its growth stabilizes after a few hits
	int i;
	int iUniqueID = 0;
	int index = -1;
	ErrorEntry *pMergedEntry = NULL;
	ErrorEntry *pEntry = NULL;
	U32 uTime = timeSecondsSince2000();
	char url[512];
	bool bHasRawData = false;

	estrCopy2(&s, "");

	for(i=0;i<count;i++) 
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if(!stricmp(args[i], "index"))
		{
			index = atoi(values[i]);
		}
	}

	if(iUniqueID == 0)
	{
		httpSendClientError(link, "Please supply an 'id' to get any details. Thanks.");
		return;
	}

	if(index == -1)
	{
		httpSendClientError(link, "Please supply a dump 'index' to get any details. Thanks.");
		return;
	}

	pMergedEntry = findErrorTrackerByID(iUniqueID);

	if(!pMergedEntry)
	{
		httpSendClientError(link, "ID not found.");
		return;
	}

	if(index < eaSize(&pMergedEntry->ppRecentErrors))
	{
		pEntry = pMergedEntry->ppRecentErrors[index];
	}

	if(!pEntry)
	{
		httpSendClientError(link, "Error index not found.");
		return;
	}

	ErrorInfoToString(&s, pEntry);

	estrConcatf(&s, "<hr size=1><br><b>Raw Data From Crash:</b><br><br>\n");
	if(pEntry->iETIndex)
	{
		NOCONST(ErrorEntry) rawEntry = {0};
		if(loadErrorEntry(pEntry->uID, pEntry->iETIndex, &rawEntry))
		{
			bHasRawData = true;
			estrConcatf(&s, "[<a href=\"/xmlsingle?id=%d&index=%d\">Crash Index %d (Raw XML)</a>]<br>\n", pEntry->uID, pEntry->iETIndex, pEntry->iETIndex);
			ETWeb_DumpDataToString(CONST_ENTRY(&rawEntry), &s, DUMPENTRY_FLAG_NO_UID|DUMPENTRY_FLAG_NO_JIRA|DUMPENTRY_FLAG_NO_PROGRAMMER_REQUEST|DUMPENTRY_FLAG_FORCE_TRIVIASTRINGS|DUMPENTRY_FLAG_NO_DUMP_TOGGLES);
			StructDeInitNoConst(parse_ErrorEntry, &rawEntry);
		}
	}

	if(!bHasRawData)
	{
		estrConcatf(&s, "(No RawData known for this dump [%d] -- [<a href=\"/fixupdumpdata?id=%d&index=%d\">Fixup</a>])<br>", 
			pEntry->iETIndex, iUniqueID, index);
	}

	if (ErrorEntry_IsGenericCrash(pMergedEntry))
	{
		estrConcatf(&s, "<div class=\"warning\">!!! This is a Generic Crash !!!</div>\n"
			"Dumps Reprocessed for this that result in new entries will also end up as \"generic\" crashes, "
			"until the hash is recalculated or the new entry is manually merged into another entry.\n");
	}
	formAppendStart(&s, "/reprocessdump", "POST", "dump", NULL);
	formAppendHiddenInt(&s, "id", pMergedEntry->uID);
	formAppendHiddenInt(&s, "dumpidx", index);
	formAppendSubmit(&s, "Reprocess Dump Stack");
	formAppendEnd(&s);

	sprintf(url, "/detail?id=%d", pMergedEntry->uID);
	estrConcatf(&s, "<br>[<a href=\"%s\">Back to Crash Details</a>]\n", url);

	estrConcatf(&s, "</div>\n");

	estrConcatf(&s, "\n<br><hr size=1>\n<br>\n");

	httpSendWrappedString(link, s, wiGetTitle(pMergedEntry), pCookies);
}

void wiFixupDumpData(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	int i;
	int iUniqueID = 0;
	int index = -1;
	ErrorEntry *pEntry = NULL;
	char url[512];
	DumpData *pDumpData = NULL;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if(!stricmp(args[i], "index"))
		{
			index = atoi(values[i]);
		}
	}

	if(iUniqueID == 0)
	{
		httpSendClientError(link, "Please supply an 'id' to get any details. Thanks.");
		return;
	}

	if(index == -1)
	{
		httpSendClientError(link, "Please supply a dump 'index' to get any details. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);

	if(!pEntry)
	{
		httpSendClientError(link, "ID not found.");
		return;
	}

	if(index < eaSize(&pEntry->ppDumpData))
	{
		pDumpData = pEntry->ppDumpData[index];
	}

	if(!pDumpData)
	{
		httpSendClientError(link, "Dump index not found.");
		return;
	}

	if (gbETVerbose) printf("Performing DumpData Fixup for [%d:%d]...\n", iUniqueID, index);
	for(i=pEntry->iTotalCount; i>0; i--)
	{
		NOCONST(ErrorEntry) rawEntry = {0};
		if(loadErrorEntry(pEntry->uID, i, &rawEntry))
		{
			bool bFound = false;
			if((pDumpData->pEntry->uNewestTime == rawEntry.uNewestTime)
			&& (pDumpData->pEntry->uFirstTime  == rawEntry.uFirstTime)
			&& (eaSize(&pDumpData->pEntry->ppExecutableNames) > 0)
			&& (eaSize(&rawEntry.ppExecutableNames) > 0)
			&& (!stricmp(pDumpData->pEntry->ppExecutableNames[0], rawEntry.ppExecutableNames[0]))
			&& (eaSize(&pDumpData->pEntry->ppUserInfo) > 0)
			&& (eaSize(&rawEntry.ppUserInfo) > 0)
			&& (!stricmp(pDumpData->pEntry->ppUserInfo[0]->pUserName, rawEntry.ppUserInfo[0]->pUserName))
			&& (eaSize(&pDumpData->pEntry->ppIPCounts) > 0)
			&& (eaSize(&rawEntry.ppIPCounts) > 0)
			&& (pDumpData->pEntry->ppIPCounts[0]->uIP == rawEntry.ppIPCounts[0]->uIP)
			)
			{
				objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
					"setDumpETIndex", "set ppDumpData[%d].iETIndex = %d", pDumpData->iDumpArrayIndex, i);
				//pDumpData->iETIndex = i;
				bFound = true;
			}

			StructDeInitNoConst(parse_ErrorEntry, &rawEntry);
			if(bFound)
				break;
		}
	}

	if (gbETVerbose) printf("DumpData Fixup for [%d:%d] Complete.\n", iUniqueID, index);
	sprintf_s(SAFESTR(url), "/dumpinfo?id=%d&index=%d", iUniqueID, index);
	httpRedirect(link, url);
}

typedef struct ActiveDumpStat
{
	U32 id;
	int count;
	U32 ip;
	IncomingClientState *pClientState;
	DumpData *pDumpData;
	const char *user;
} ActiveDumpStat;

#define MAX_ACTIVE_DUMP_STATS (1000)
static ActiveDumpStat activeDumpStats[MAX_ACTIVE_DUMP_STATS];
static ActiveDumpStat **activeDumpStatsSorted = NULL;
static int statCount = 0;

ActiveDumpStat * findStat(U32 id)
{
	int i;

	for(i=0; i<statCount; i++)
	{
		if(activeDumpStats[i].id == id)
			return &activeDumpStats[i];
	}

	return NULL;
}

int SortActiveDumpsByCount(const ActiveDumpStat **pStat1, const ActiveDumpStat **pStat2, const void *ign)
{
	if      ((*pStat1)->count < (*pStat2)->count) return  1;
	else if ((*pStat1)->count > (*pStat2)->count) return -1;
	else return 0;
}

void wiActiveDumps(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	static char *s = NULL;
	DumpData *pDumpData;
	IncomingClientState *pClientState = NULL;
	int additionalCount = 0;

	estrPrintf(&s, "<h2>Active Dumps:</h2>");

	statCount = 0;
	eaClear(&activeDumpStatsSorted);

	EARRAY_FOREACH_BEGIN(gActiveDumps, i);
	{
		if(statCount >= MAX_ACTIVE_DUMP_STATS)
		{
			additionalCount++;
			continue;
		}

		pClientState = gActiveDumps[i];
		pDumpData = findDumpData(pClientState->uDumpID);

		if(pDumpData && pDumpData->pEntry)
		{
			U32 ip                    = (pDumpData->pEntry->ppIPCounts) ? pDumpData->pEntry->ppIPCounts[0]->uIP : 0;
			const char *user          = (pDumpData->pEntry->ppUserInfo && pDumpData->pEntry->ppUserInfo[0]) ? pDumpData->pEntry->ppUserInfo[0]->pUserName : "Unknown";
			ActiveDumpStat * dumpStat = findStat(pDumpData->pEntry->uID);
			if(!dumpStat)
			{
				dumpStat = &activeDumpStats[statCount++];
				dumpStat->count = 1;
				dumpStat->id    = pDumpData->pEntry->uID;
				dumpStat->pClientState = pClientState;
				dumpStat->pDumpData    = pDumpData;
				dumpStat->ip           = ip;
				dumpStat->user         = user;
				eaPush(&activeDumpStatsSorted, dumpStat);
			}
			else
			{
				dumpStat->count++;
				if(dumpStat->ip != ip)
					dumpStat->ip = 0;
				if(stricmp(dumpStat->user, user))
					dumpStat->user = "[Multiple]";
			}
		}
		else
		{
			additionalCount++;
		}
	}
	EARRAY_FOREACH_END;

	eaStableSort(activeDumpStatsSorted, NULL, SortActiveDumpsByCount);

	if(statCount > 0)
	{
		estrConcatf(&s, "<a href=\"/disconnectall\">[Disconnect All]</a>\n");

		estrConcatf(&s, "<table border=1 cellspacing=0 cellpadding=3>\n");

		estrConcatf(&s, 
			"<tr>"
			"<td><b>ET ID</b></td>"
			"<td><b>Count</b></td>"
			"<td><b>IP</b></td>"
			"<td><b>User</b></td>"
			"<td><b>FD Comp.</b></td>"
			"<td><b>MD Comp.</b></td>"
			"<td><b>Actions</b></td>"
			"</tr>\n", 
			);

		EARRAY_FOREACH_BEGIN(activeDumpStatsSorted, i);
		{
			ActiveDumpStat *dumpStat = activeDumpStatsSorted[i];
			int numFullDumps = countReceivedDumps(dumpStat->id, DUMPFLAGS_FULLDUMP);
			int numMiniDumps = countReceivedDumps(dumpStat->id, DUMPFLAGS_MINIDUMP);
			estrConcatf(&s, 
				"<tr>"
				"<td><a href=\"/detail?id=%d\">%d</a></td>"
				"<td>%d</td>"
				"<td>%s</td>"
				"<td>%s</td>"
				"<td>%d</td>"
				"<td>%d</td>"
				"<td>[<a href=\"/disconnectbyid?id=%d\">Disconnect All</a>]</td>"
				"</tr>\n", 

				dumpStat->id, dumpStat->id,
				dumpStat->count,
				dumpStat->ip ? makeIpStr(dumpStat->ip) : "[N/A]",
				dumpStat->user,
				numFullDumps,
				numMiniDumps,
				dumpStat->id
				);
		}
		EARRAY_FOREACH_END;

		estrConcatf(&s, "</table>\n");
	}
	else
	{
		estrConcatf(&s, "No active dump receives currently ...<br>");
	}

	estrConcatf(&s, "<br><br><br><br>");
	httpSendWrappedString(link, s, NULL, pCookies);
}

void wiDisconnectByID(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	int i;
	int iUniqueID = 0;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
	}

	if(iUniqueID)
	{
		disconnectAllDumpsByID(iUniqueID);
	}
	httpRedirect(link, "/activedumps");
}

void wiDisconnectAll(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	disconnectAllDumpsByID(0);
	httpRedirect(link, "/activedumps");
}

void wiToggleUnlimitedUsers(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	char redirBuf[256];
	ErrorEntry *pEntry = NULL;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
	}

	if(iUniqueID == 0)
	{
		httpSendClientError(link, "Please supply 'id' to toggle unlimited users. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);

	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}

	objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
		"setUnlimitedUsersFlag", "set bUnlimitedUsers = %d", !pEntry->bUnlimitedUsers);
	sprintf_s(SAFESTR(redirBuf), "/detail?id=%d", iUniqueID);
	httpRedirect(link, redirBuf);
}

void wiRemoveHash(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	char redirBuf[256];
	ErrorEntry *pEntry = NULL;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
			sscanf(values[i], "%d", &iUniqueID);
	}

	pEntry = findErrorTrackerByID(iUniqueID);
	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}
	AutoTrans_trErrorEntry_RemoveHash(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
	sprintf_s(SAFESTR(redirBuf), "/detail?id=%d", iUniqueID);
	httpRedirect(link, redirBuf);
}

void wiFlagReplaceCallstack(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	char redirBuf[256];
	bool bRequestFound = false;
	bool bReplaceCallstack = false;
	ErrorEntry *pEntry = NULL;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if(!stricmp(args[i], "val"))
		{
			int iFlag = atoi(values[i]);
			bReplaceCallstack = (iFlag == 1);
			bRequestFound = true;
		}
	}

	if((iUniqueID == 0) || !bRequestFound)
	{
		httpSendClientError(link, "Please supply 'id' and 'val' to set \"replace callstack\" requests. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);
	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}

	if (pEntry->bReplaceCallstack != bReplaceCallstack)
	{
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
			"setMustFindProgrammer", "set bReplaceCallstack = %d", bReplaceCallstack);
	}
	sprintf_s(SAFESTR(redirBuf), "/detail?id=%d", iUniqueID);
	httpRedirect(link, redirBuf);
}

void wiFlagImportantCrash(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	char redirBuf[256];
	bool bRequestFound = false;
	bool bMustFindProgrammer = false;
	ErrorEntry *pEntry = NULL;
	const char * pUsername = findCookieValue(pCookies, "username");

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if(!stricmp(args[i], "imp"))
		{
			int iImportantFlag = atoi(values[i]);
			bMustFindProgrammer = (iImportantFlag == 1);
			bRequestFound = true;
		}
	}

	if (bMustFindProgrammer && !pUsername)
	{
		//httpSendWrappedString(link, "You must login with your Jira username/password before setting the \
		//							programmer notification flag to attach your username to it.", pCookies);
		UrlArgumentList emptyargs = {0};
		wiAuthenticate(link, "/login", &emptyargs, pReferer, pCookies);
		return;
	}
	if((iUniqueID == 0) || !bRequestFound)
	{
		httpSendClientError(link, "Please supply 'id' and 'imp' to set full dump requests. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);

	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}

	if (pEntry->bMustFindProgrammer != bMustFindProgrammer)
	{
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
			"setMustFindProgrammer", "set bMustFindProgrammer = %d", bMustFindProgrammer);
	}
	if (bMustFindProgrammer && pUsername && (!pEntry->pProgrammerName || stricmp(pEntry->pProgrammerName, pUsername)) )
	{
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
			"setProgrammerName", "set pProgrammerName = \"%s\"", pUsername);
	}
	else if (!bMustFindProgrammer && pEntry->pProgrammerName)
	{
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
			"clearProgrammerName", "destroy pProgrammerName");
	}
	
	sprintf_s(SAFESTR(redirBuf), "/detail?id=%d", iUniqueID);
	/*if (bMustFindProgrammer && !pUsername)
	{
		httpSendWrappedString(link, "Please login with your Jira username/password and re-set the \
									programmer notification flag to attach your username to it.", pCookies);
	}
	else*/
		httpRedirect(link, redirBuf);
}

void wiMarkAsGenericCrash(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	const char *label = NULL;
	ErrorEntry *pEntry = NULL;
	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if (!stricmp(args[i], "label"))
		{
			label = values[i];
		}
	}
	if(iUniqueID == 0)
	{
		httpSendClientError(link, "Please supply the 'id' for the entry.");
		return;
	}
	pEntry = findErrorTrackerByID(iUniqueID);
	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}
	errorTrackerAddGenericHash(pEntry, label);
	httpRedirect(link, pReferer);
}

void wiUnmarkAsGenericCrash(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	ErrorEntry *pEntry = NULL;
	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
	}
	if(iUniqueID == 0)
	{
		httpSendClientError(link, "Please supply the 'id' for the entry.");
		return;
	}
	pEntry = findErrorTrackerByID(iUniqueID);
	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}
	errorTrackerUndoGenericHash(pEntry);
	httpRedirect(link, pReferer);
}

void wiReqFullDump(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	char redirBuf[256];
	bool bRequestFound = false;
	bool bFullDumpRequested = false;
	ErrorEntry *pEntry = NULL;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if(!stricmp(args[i], "full"))
		{
			int iFullDump = atoi(values[i]);
			bFullDumpRequested = (iFullDump == 1);
			bRequestFound = true;
		}
	}

	if((iUniqueID == 0) || !bRequestFound)
	{
		httpSendClientError(link, "Please supply 'id' and 'full' to set full dump requests. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);

	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}

	if (pEntry->bFullDumpRequested != bFullDumpRequested)
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
			"setDumpRequested", "set bFullDumpRequested = %d", bFullDumpRequested);
	sprintf_s(SAFESTR(redirBuf), "/detail?id=%d", iUniqueID);
	httpRedirect(link, redirBuf);
}

//Toggles the bSuppressError value that determines whether full email descriptions get sent out.
void wiSuppressError(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	char redirBuf[256];
	bool bRequestFound = false;
	bool bSuppressErrorRequested = false;
	ErrorEntry *pEntry = NULL;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if(!stricmp(args[i], "suppress"))
		{
			int iSuppress = atoi(values[i]);
			bSuppressErrorRequested = (iSuppress == 1);
			bRequestFound = true;
		}
	}

	if((iUniqueID == 0) || !bRequestFound)
	{
		httpSendClientError(link, "Please supply 'id' and 'suppress' to set error suppression requests. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID); 

	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}

	if (pEntry->bSuppressErrorInfo != bSuppressErrorRequested)
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
		"setSuppressErrorRequested", "set bSuppressErrorInfo = %d", bSuppressErrorRequested);
	sprintf_s(SAFESTR(redirBuf), "/detail?id=%d", iUniqueID);
	httpRedirect(link, redirBuf);

}

void wiBlockDumps(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	char redirBuf[256];
	bool bRequestFound = false;
	bool bBlockRequested = false;
	ErrorEntry *pEntry = NULL;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if(!stricmp(args[i], "block"))
		{
			int iFullDump = atoi(values[i]);
			bBlockRequested = (iFullDump == 1);
			bRequestFound = true;
		}
	}

	if((iUniqueID == 0) || !bRequestFound)
	{
		httpSendClientError(link, "Please supply 'id' and 'full' to set full dump requests. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);

	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}

	if (pEntry->bBlockDumpRequests != bBlockRequested)
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
			"setBlockDumpRequests", "set bBlockDumpRequests = %d", bBlockRequested);
	sprintf_s(SAFESTR(redirBuf), "/detail?id=%d", iUniqueID);
	httpRedirect(link, redirBuf);
}

void wiRemoveDumpNotify(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	int iEmailIndex = -1;
	char redirBuf[256];
	bool bRequestFound = false;
	bool bFullDumpRequested = false;
	ErrorEntry *pEntry = NULL;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if(!stricmp(args[i], "index"))
		{
			sscanf(values[i], "%d", &iEmailIndex);
		}
	}

	if((iUniqueID == 0) || (iEmailIndex == -1))
	{
		httpSendClientError(link, "Please supply 'id' and 'index' to request a dump notification. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);

	if(!pEntry)
	{
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}

	if (iEmailIndex < eaSize(&pEntry->ppDumpNotifyEmail))
	{
		AutoTrans_trErrorEntry_RemoveDumpNotify(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
			pEntry->uID, iEmailIndex);
	}
	sprintf_s(SAFESTR(redirBuf), "/detail?id=%d", iUniqueID);
	httpRedirect(link, redirBuf);
}

void wiDumpNotify(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	int iUniqueID = 0;
	char redirBuf[256];
	bool bRequestFound = false;
	bool bFullDumpRequested = false;
	ErrorEntry *pEntry = NULL;
	char *pEmail = NULL;

	estrClear(&pEmail);

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if(!stricmp(args[i], "email"))
		{
			estrPrintf(&pEmail, "%s", values[i]);
		}
	}

	if((iUniqueID == 0) || (pEmail == NULL))
	{
		estrClear(&pEmail);
		httpSendClientError(link, "Please supply 'id' and 'email' to request a dump notification. Thanks.");
		return;
	}

	pEntry = findErrorTrackerByID(iUniqueID);

	if(!pEntry)
	{
		estrClear(&pEmail);
		httpSendClientError(link, "Detail not found for that ID.");
		return;
	}

	if(!strstri(pEmail, "@"))
	{
		estrConcatf(&pEmail, "@" ORGANIZATION_DOMAIN);
	}

	if (!findUniqueString(pEntry->ppDumpNotifyEmail, pEmail))
	{
		AutoTrans_trErrorEntry_AddDumpNotify(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
			pEntry->uID, pEmail);
	}
	sprintf_s(SAFESTR(redirBuf), "/detail?id=%d", iUniqueID);
	httpRedirect(link, redirBuf);

	estrDestroy(&pEmail);
}

void appendIntToURLIfNotZero(char **estr, const char *var, int i)
{
	if (i == 0)
		return;
	appendToURL(estr, var, "%d", i);
}
void appendStringToURLIfNotEmpty(char **estr, const char *var, const char *str)
{
	if (!str || !*str)
		return;	
	appendToURL(estr, var, "%s", str);
}

// I hate this too.
#define APPEND_URL_ARGS(ESTR,OFFSET,SORTORDER,DESC) \
{ \
	appendIntToURLIfNotZero(ESTR, "offset", (OFFSET)); \
	appendIntToURLIfNotZero(ESTR, "maxcs", iMaxCallstackCount); \
	appendIntToURLIfNotZero(ESTR, "days", sd->iDaysAgo); \
	appendIntToURLIfNotZero(ESTR, "sort", (SORTORDER)); \
	appendIntToURLIfNotZero(ESTR, "asc", (DESC)?0:1); \
	appendIntToURLIfNotZero(ESTR, "hf", (bShowForm)?0:1); \
	appendIntToURLIfNotZero(ESTR, "am", (bAssignmentMode)?1:0); \
	appendIntToURLIfNotZero(ESTR, "limit", iSearchCountLimit); \
	appendIntToURLIfNotZero(ESTR, "ta", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_ASSERT)?1:0); \
	appendIntToURLIfNotZero(ESTR, "tf", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_FATALERROR)?1:0); \
	appendIntToURLIfNotZero(ESTR, "tc", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_CRASH)?1:0); \
	appendIntToURLIfNotZero(ESTR, "te", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_ERROR)?1:0); \
	appendIntToURLIfNotZero(ESTR, "to", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_COMPILE)?1:0); \
	appendIntToURLIfNotZero(ESTR, "tg", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_GAMEBUG)?1:0); \
	appendIntToURLIfNotZero(ESTR, "th", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_XPERF)?1:0); \
	appendStringToURLIfNotEmpty(ESTR, "exe", sd->pExecutable); \
	appendStringToURLIfNotEmpty(ESTR, "csf", sd->pCallStackFunc); \
	appendStringToURLIfNotEmpty(ESTR, "err", sd->pErrorText); \
	appendStringToURLIfNotEmpty(ESTR, "df", sd->pDataFile); \
	appendStringToURLIfNotEmpty(ESTR, "pn", sd->pProductName); \
	appendStringToURLIfNotEmpty(ESTR, "user", sd->pUserName); \
	appendStringToURLIfNotEmpty(ESTR, "any", sd->pAny); \
	appendStringToURLIfNotEmpty(ESTR, "expr", sd->pExpression); \
	appendStringToURLIfNotEmpty(ESTR, "mem", sd->pMemoryAlloc); \
	appendStringToURLIfNotEmpty(ESTR, "ver", sd->pVersion); \
	appendStringToURLIfNotEmpty(ESTR, "sis", sd->pShardInfoString); \
	appendStringToURLIfNotEmpty(ESTR, "catg", sd->pCategory); \
	appendIntToURLIfNotZero(ESTR, "hp", (sd->bHideProgrammers)?1:0); \
	appendIntToURLIfNotZero(ESTR, "ha", (sd->bHideJiraAssigned)?1:0); \
	appendIntToURLIfNotZero(ESTR, "hd", (sd->bShowDuplicates)?1:0); \
	appendIntToURLIfNotZero(ESTR, "jira", sd->iJira); \
	appendStringToURLIfNotEmpty(ESTR, "ip",	sd->pIP); \
	appendStringToURLIfNotEmpty(ESTR, "bran", sd->pBranch); \
	appendStringToURLIfNotEmpty(ESTR, "summ", sd->pSummary); \
	appendIntToURLIfNotZero(ESTR, "plat", sd->ePlatform); \
	appendStringToURLIfNotEmpty(ESTR, "globaltype", sd->pGlobalType); \
}

static char * InPlaceTrimStartAndEnd(char * pString)
{
	char *pEnd;
	if (pString == NULL)
		return NULL;
	while (*pString == ' ')
	{
		pString++;
	}
	pEnd = pString + strlen(pString) - 1;

	while (pEnd > pString && *pEnd == ' ')
	{
		*pEnd = '\0';
		pEnd--;
	}
	return pString;
}

// JDRAGO - This is pretty much the worst function on the planet, and exemplifies everything that is 
//          wrong with writing web applications in C. The end!
// NOTE:    If you use this function, you MUST also update your "onSubmit" function when you create your form. 
static void appendCalendarEntry(char **es, const char *baseName, const char *label, const char *defaultVal)
{
	estrConcatf(es, "<div class=\"yui-skin-sam\">"
					"<div class=\"datefield\">"
					"<label for=\"%s\">%s:</label>"
					"<div id=\"%scon\" class=\"datefieldcon\">"
					"<input type=\"hidden\" id=\"%sraw\" name=\"%sraw\" value=\"\" />"
					"<input type=\"text\" id=\"%s\" onChange=\"calendarfixup()\" name=\"%s\" value=\"%s\" />"
					"<button type=\"button\" id=\"%sbutton\" title=\"Show Calendar\">"
					"<img src=\"/calbtn.gif\">"
					"</button>"
					"</div>"
					"</div>"
					"</div>"
					"<script type=\"text/javascript\">createCal(\"%sbutton\", \"%s\", \"%sraw\");</script>",

					baseName, label,
					baseName,baseName,baseName,baseName,baseName,
					defaultVal,
					baseName,baseName,baseName,baseName
					);
}

static const char *spNotes = 
"<li>Generates a temporary, in-memory report</li>"
"<li>100% accurate, but only knows of the<br>last 100,000 <i>fatal</i> errors reported</li>"
"<li>One report stored in memory per ET-user-IP</li>";

void wiAppendSearchForm(NetLink *link, 
						char **estr, 
						SearchData *sd, 
						bool bAssignmentMode,
						int iSearchCountLimit,
						int iMaxCallstackCount,
						char *pDateStart,
						char *pDateEnd,
						const char *extraBlockText)
{
	int i;

	estrConcatf(estr, "<div class=\"formdata\">\n");
	formAppendStart(estr, "/search", "GET", "searchform", "onSubmit=\"searchfixup();\"");

	// Start outer column table
	estrConcatf(estr, "\n<table width=100%%><tr><td valign=top>\n");

	// Start inner table
	estrConcatf(estr, "\n<table>\n");

	estrConcatf(estr, "<tr><td>Sort By:  </td><td>");
	formAppendEnum(estr, "sort", sortOrderEntireEnumString(), (int)sd->eSortOrder);
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Ascending:  </td><td>");
	formAppendCheckBox(estr, "asc", !sd->bSortDescending);
	estrConcatf(estr, " ");

	estrConcatf(estr, "<tr><td>Asserts:  </td><td>");
	formAppendCheckBox(estr, "ta", !sd->uTypeFlags || SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_ASSERT));
	estrConcatf(estr, " ");

	estrConcatf(estr, "<tr><td>Crashes:  </td><td>");
	formAppendCheckBox(estr, "tc", !sd->uTypeFlags || SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_CRASH));
	estrConcatf(estr, " ");

	estrConcatf(estr, "<tr><td>Fatal Errors:  </td><td>");
	formAppendCheckBox(estr, "tf", !sd->uTypeFlags || SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_FATALERROR));
	estrConcatf(estr, " ");

	estrConcatf(estr, "<tr><td>Errors:  </td><td>");
	formAppendCheckBox(estr, "te", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_ERROR));
	estrConcatf(estr, " ");

	estrConcatf(estr, "<tr><td>Compile&nbsp;Errors:  </td><td>");
	formAppendCheckBox(estr, "to", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_COMPILE));
	estrConcatf(estr, " ");

	estrConcatf(estr, "<tr><td>Manual&nbsp;Dumps:  </td><td>");
	formAppendCheckBox(estr, "tg", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_GAMEBUG));
	estrConcatf(estr, " ");

	estrConcatf(estr, "<tr><td>Xperf Dumps:  </td><td>");
	formAppendCheckBox(estr, "th", SEARCH_TYPE_ENABLED(sd->uTypeFlags, ERRORDATATYPE_XPERF));
	estrConcatf(estr, " ");

	estrConcatf(estr, "<tr><td>Ticket Jira:  </td><td>");
	formAppendEnum(estr, "jira", "--|Has Jira|No Jira", sd->iJira);
	estrConcatf(estr, "</td></tr>\n");

	// End inner table
	estrConcatf(estr, "\n</table>\n");

	// Next outer column
	estrConcatf(estr, "\n</td><td valign=top>\n");

	// Start inner table
	estrConcatf(estr, "\n<table>\n");

	estrConcatf(estr, "<tr><td>In Function:  </td><td>");
	formAppendEdit(estr, 15, "csf", (sd->pCallStackFunc) ? sd->pCallStackFunc : "");
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Error: </td><td>");
	formAppendEdit(estr, 15, "err", (sd->pErrorText) ? sd->pErrorText : "");
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>DataFile: </td><td>");
	formAppendEdit(estr, 15, "df", (sd->pDataFile) ? sd->pDataFile : "");
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Platform:  </td><td>");
	formAppendEnum(estr, "plat", "--|Win32|Xbox 360", (int)sd->ePlatform);
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Executable: </td><td>");
	formAppendEdit(estr, 15, "exe", NNS(sd->pExecutable));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>IP: </td><td>");
	formAppendEdit(estr, 15, "ip", NNS(sd->pIP));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Shard Info String: </td><td>");
	formAppendEdit(estr, 15, "sis", NNS(sd->pShardInfoString));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Largest Memory Alloc: </td><td>");
	formAppendEdit(estr, 15, "mem", NNS(sd->pMemoryAlloc));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>GlobalType (exact): </td><td>");
	formAppendEdit(estr, 15, "globaltype", NNS(sd->pGlobalType));
	estrConcatf(estr, "</td></tr>");

	// End inner table
	estrConcatf(estr, "\n</table>\n");

	// Next outer column
	estrConcatf(estr, "\n</td><td valign=top>\n");

	// Start inner table
	estrConcatf(estr, "\n<table>\n");

	estrConcatf(estr, "<tr><td>Version Number: </td><td>");
	formAppendEdit(estr, 15, "ver", NNS(sd->pVersion));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Branch: </td><td>");
	formAppendEdit(estr, 15, "bran", NNS(sd->pBranch));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Product: </td><td>");
	formAppendEdit(estr, 15, "pn", NNS(sd->pProductName));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>User: </td><td>");
	formAppendEdit(estr, 15, "user", NNS(sd->pUserName));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Expression: </td><td>");
	formAppendEdit(estr, 15, "expr", NNS(sd->pExpression));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Category: </td><td>");
	formAppendEdit(estr, 15, "catg", NNS(sd->pCategory));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Summary: </td><td>");
	formAppendEdit(estr, 15, "summ", NNS(sd->pSummary));
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>In the last X Days: </td><td>");
	formAppendEditInt(estr, 3, "days", sd->iDaysAgo);
	estrConcatf(estr, "</td></tr>");

	// End inner table
	estrConcatf(estr, "\n</table>\n");

	// Next outer column
	estrConcatf(estr, "\n</td><td valign=top>\n");		

	// Start inner table
	estrConcatf(estr, "\n<table>\n");

	estrConcatf(estr, "<tr><td>Max Count: </td><td>");
	formAppendEditInt(estr, 3, "limit", iSearchCountLimit);
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Max Stack: </td><td>");
	formAppendEditInt(estr, 3, "maxcs", iMaxCallstackCount);
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Hide Programmer-only: </td><td>");
	formAppendCheckBox(estr, "hp", sd->bHideProgrammers);
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Assignment Mode: </td><td>");
	formAppendCheckBox(estr, "am", bAssignmentMode);
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Hide Assigned: </td><td>");
	formAppendCheckBox(estr, "ha", sd->bHideJiraAssigned);
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Hide Form: </td><td>");
	formAppendCheckBox(estr, "hf", false);
	estrConcatf(estr, "</td></tr>");

	estrConcatf(estr, "<tr><td>Show Duplicates: </td><td>");
	formAppendCheckBox(estr, "hd", sd->bShowDuplicates);
	estrConcatf(estr, "</td></tr>");

	// End inner table
	estrConcatf(estr, "\n</table>\n");

	// The bottom string
	estrConcatf(estr, "\n</td></tr><tr><td valign=top>\n");

#ifdef USE_SEARCH_ANYTHING
	// The "Any" string
	//estrConcatf(estr, "\n<br>Just search everything: ");
	//formAppendEdit(estr, 35, "any", (sd->pAny) ? sd->pAny : "");

	// Autofocus the 'any' field
	estrConcatf(estr, 
		"<script language=\"Javascript\">\n"
		"<!--\n"
		"document.searchform.any.focus();\n"
		"//-->\n"
		"</script>\n");

	// End of the "Any" string
	//estrConcatf(estr, "\n</td><td><br>");
	formAppendSubmit(estr, "Search");
#else
	estrConcatf(estr, "<br>\n");
	formAppendSubmit(estr, "Begin Search");
	estrConcatf(estr, "<br>\n");
#endif

	// End outer column table
	estrConcatf(estr, "\n</td>");
	estrConcatf(estr, "<td valign=top>");
	estrConcatf(estr, "%s", extraBlockText);
	estrConcatf(estr, "</td>\n");

	// -----------------------------------------------------------------------
	// Exact Mode
	estrConcatf(estr, "<td colspan=2 align=right><table class=\"exacttable\"><tr><td>");

	estrConcatf(estr, "<b>Exact Mode</b> - Enable: ");
	formAppendCheckBox(estr, "exact", (sd->uFlags & SEARCHFLAG_EXACT_ENABLED) ? 1 : 0);
	estrConcatf(estr, "</td></tr><tr><td align=right>");
	appendCalendarEntry(estr, "datestart", "Start Time", pDateStart);
	appendCalendarEntry(estr, "dateend", "End Time", pDateEnd);

	estrConcatf(estr, "</td></tr>");
	estrConcatf(estr, "<tr><td>");
	estrConcatf(estr, "<span class=\"smallnote\" style=\"text-align:center\">Hours Ago: [");
	for(i=0; i<7; i++)
	{
		estrConcatf(estr, "%s<a class=\"smalllink\" onClick=\"setTimeWindow(%d);\">", (i) ? " / " : "", i);
		if(i)
			estrConcatf(estr, "%d</a>", i);
		else
			estrConcatf(estr, "Now</a>");
	}
	estrConcatf(estr, "]</span>");
	estrConcatf(estr, "<ul><span class=\"smallnote\">%s</span></ul></td></tr>", spNotes);
	estrConcatf(estr, "</table></td>");

	// -----------------------------------------------------------------------

	estrConcatf(estr, "</tr></table>\n");

	formAppendEnd(estr);
	estrConcatf(estr, "</div>\n");

	formAppendStart(estr, "/detail", "GET", "gotoform", NULL);
	estrConcatf(estr, "... or go directly to ID: ");
	formAppendEdit(estr, 15, "id", "");
	formAppendSubmit(estr, "Go");
	formAppendEnd(estr);

	estrConcatf(estr, "<hr size=1>\n");
	// ----------------------------------------------------------------------------
}

void appendSearchResultsStart(char **s, 
							  int iCount, 
							  int offset, 
							  int iSearchCountLimit,  
							  SearchData *sd, 
							  int iMaxCallstackCount, 
							  bool bShowForm,
							  bool bAssignmentMode)
{
	if (iSearchCountLimit)
	{
		estrConcatf(s, "Showing Results %d-%d of %d<br>\n", 1+offset, min(offset+iSearchCountLimit, iCount), 
			iCount);
	}

	estrConcatf(s, "Search Results:<br>\n<br>\n");

	estrConcatf(s, "<a href=\"/search?");
	APPEND_URL_ARGS(s, (offset-iSearchCountLimit > 0)?(offset-iSearchCountLimit):0, sd->eSortOrder, sd->bSortDescending);
	estrConcatf(s, "\">&lt;== Prev</a>");

	estrConcatf(s, " | ");

	estrConcatf(s, "<a href=\"/search?");
	APPEND_URL_ARGS(s, offset+iSearchCountLimit, sd->eSortOrder, sd->bSortDescending);
	estrConcatf(s, "\">Next ==&gt;</a>");

	estrConcatf(s,
		"<table width=\"100%%\" class=\"summarytable\" cellpadding=3 cellspacing=0>"
		"<tr>"
		"<td align=right class=\"summaryheadtd\">");

	estrConcatf(s, "<a href=\"/search?");
	APPEND_URL_ARGS(s, offset, SORTORDER_ID, (sd->eSortOrder == SORTORDER_ID)?!sd->bSortDescending:sd->bSortDescending);
	estrConcatf(s, "\">ID</a>");

	estrConcatf(s,
		"</td>"
		"<td align=right class=\"summaryheadtd\">Type</td>"
		"<td align=right class=\"summaryheadtd\">Executable</td>"
		"<td align=right class=\"summaryheadtd\">");

	estrConcatf(s, "<a href=\"/search?");
	APPEND_URL_ARGS(s, offset, SORTORDER_COUNT, (sd->eSortOrder == SORTORDER_COUNT)?!sd->bSortDescending:sd->bSortDescending);
	estrConcatf(s, "\">Count</a>");

	estrConcatf(s, 
		"</td>"
		"<td align=right class=\"summaryheadtd\">First Seen</td>");
	estrConcatf(s, 
		"<td align=right class=\"summaryheadtd\">"
		"<a href=\"/search?");
	APPEND_URL_ARGS(s, offset, SORTORDER_NEWEST, (sd->eSortOrder == SORTORDER_NEWEST)?!sd->bSortDescending:sd->bSortDescending);
	estrConcatf(s, 
		"\">Last Seen</a>"
		"</td>"
		"<td align=right class=\"summaryheadtd\">Product</td>"
		"<td align=right class=\"summaryheadtd\">Jira</td>"
		"<td align=left class=\"summaryheadtdend\">Info</td>"
		"</tr>\n");
}

void appendSearchResultsEnd(char **s,
							int offset, 
							int iSearchCount,
							int iSearchCountLimit, 
							SearchData *sd, 
							int iMaxCallstackCount, 
							bool bShowForm,
							bool bAssignmentMode,
							bool bTruncatedList)
{
	estrConcatf(s, "<br>Displaying %d entries.\n", iSearchCount);
	if(bTruncatedList)
		estrConcatf(s, "<br>[truncated list due to assignment mode]\n");

	estrConcatf(s, "<a href=\"/search?");
	APPEND_URL_ARGS(s, (offset-iSearchCountLimit > 0)?(offset-iSearchCountLimit):0, sd->eSortOrder, sd->bSortDescending);
	estrConcatf(s, "\">&lt;== Prev</a>");

	estrConcatf(s, " | ");

	estrConcatf(s, "<a href=\"/search?");
	APPEND_URL_ARGS(s, offset+iSearchCountLimit, sd->eSortOrder, sd->bSortDescending);
	estrConcatf(s, "\">Next ==&gt;</a>");
}

static void buildSearchData(NetLink *link, SearchData *sd, char **args, char **values, int count, 
							int *piOffset, int *piMaxCallstackCount,
							bool *pbShowForm, bool *pbAssignmentMode, int *piSearchCountLimit, 
							char **ppDateStart, char **ppDateEnd, bool *pbPurgeEntries)
{
	int i;
	if (!args || !values || !sd)
		return;
	for(i=0;i<count;i++)
	{
		if (values[i])
			estrTrimLeadingAndTrailingWhitespace(&values[i]);
		if(!stricmp(args[i], "offset"))
		{
			if (piOffset)
				*piOffset = atoi(values[i]);
		}
		else if(!stricmp(args[i], "maxcs"))
		{
			if (piMaxCallstackCount)
				*piMaxCallstackCount = atoi(values[i]);
		}
		else if(!stricmp(args[i], "datestart"))
		{
			if (ppDateStart)
				*ppDateStart = strdup(values[i]); // For re-populating the search form.
			sd->uExactStartTime = timeGetSecondsSince2000FromLocalDateString(values[i]);
			if(sd->uExactStartTime)
				sd->uFlags |= SEARCHFLAG_EXACT_STARTDATE;
		}
		else if(!stricmp(args[i], "dateend"))
		{
			if (ppDateEnd)
				*ppDateEnd = strdup(values[i]); // For re-populating the search form.
			sd->uExactEndTime = timeGetSecondsSince2000FromLocalDateString(values[i]);
			if(sd->uExactEndTime)
				sd->uFlags |= SEARCHFLAG_EXACT_ENDDATE;
		}
		else if(!stricmp(args[i], "exact")) // Exact Mode
		{
			sd->uFlags |= SEARCHFLAG_EXACT_ENABLED;
		}
		else if(!stricmp(args[i], "notimeout")) // Disable the timeout, not exposed to the web interface UI! Shhhh!
		{
			char tempIP[128];
			if (gbETVerbose) printf("Performing a notimeout search for %s...\n", linkGetIpStr(link, SAFESTR(tempIP)));
			sd->uFlags |= SEARCHFLAG_DONT_TIMEOUT;
		}
		else if(!stricmp(args[i], "days"))
		{
			sd->iDaysAgo = atoi(values[i]);
			if(sd->iDaysAgo > 0)
			{
				sd->uFlags |= SEARCHFLAG_DAYSAGO;
			}
		}
		else if(!stricmp(args[i], "sort"))
		{
			sd->uFlags |= SEARCHFLAG_SORT;
			sd->eSortOrder = (SortOrder)CLAMP(atoi(values[i]), 0, SORTORDER_MAX);
		}
		else if(!stricmp(args[i], "asc"))
		{
			sd->bSortDescending = !(atoi(values[i]) == 1);
		}
		else if(!stricmp(args[i], "hf")) // Hide Form
		{
			if (pbShowForm)
				*pbShowForm = !(atoi(values[i]) == 1);
		}
		else if(!stricmp(args[i], "am")) // Assignment Mode
		{
			if (pbAssignmentMode)
				*pbAssignmentMode = (atoi(values[i]) == 1);
		}
		else if(!stricmp(args[i], "limit"))
		{
			if (piSearchCountLimit)
			{
				*piSearchCountLimit = atoi(values[i]);
				if(*piSearchCountLimit < 1)
					*piSearchCountLimit = 0;
			}
		}
		else if(!stricmp(args[i], "ta"))
		{
			int iEnabled = atoi(values[i]);
			if(iEnabled)
			{
				sd->uFlags     |= SEARCHFLAG_TYPE;
				sd->uTypeFlags |= SEARCH_TYPE_TO_FLAG(ERRORDATATYPE_ASSERT);
			}
		}
		else if(!stricmp(args[i], "tf"))
		{
			int iEnabled = atoi(values[i]);
			if(iEnabled)
			{
				sd->uFlags     |= SEARCHFLAG_TYPE;
				sd->uTypeFlags |= SEARCH_TYPE_TO_FLAG(ERRORDATATYPE_FATALERROR);
			}
		}
		else if(!stricmp(args[i], "tc"))
		{
			int iEnabled = atoi(values[i]);
			if(iEnabled)
			{
				sd->uFlags     |= SEARCHFLAG_TYPE;
				sd->uTypeFlags |= SEARCH_TYPE_TO_FLAG(ERRORDATATYPE_CRASH);
			}
		}
		else if(!stricmp(args[i], "te"))
		{
			int iEnabled = atoi(values[i]);
			if(iEnabled)
			{
				sd->uFlags     |= SEARCHFLAG_TYPE;
				sd->uTypeFlags |= SEARCH_TYPE_TO_FLAG(ERRORDATATYPE_ERROR);
			}
		}
		else if(!stricmp(args[i], "to"))
		{
			int iEnabled = atoi(values[i]);
			if(iEnabled)
			{
				sd->uFlags     |= SEARCHFLAG_TYPE;
				sd->uTypeFlags |= SEARCH_TYPE_TO_FLAG(ERRORDATATYPE_COMPILE);
			}
		}
		else if(!stricmp(args[i], "tg"))
		{
			int iEnabled = atoi(values[i]);
			if(iEnabled)
			{
				sd->uFlags     |= SEARCHFLAG_TYPE;
				sd->uTypeFlags |= SEARCH_TYPE_TO_FLAG(ERRORDATATYPE_GAMEBUG);
			}
		}
		else if(!stricmp(args[i], "th"))
		{
			int iEnabled = atoi(values[i]);
			if(iEnabled)
			{
				sd->uFlags     |= SEARCHFLAG_TYPE;
				sd->uTypeFlags |= SEARCH_TYPE_TO_FLAG(ERRORDATATYPE_XPERF);
			}
		}
		else if(!stricmp(args[i], "csf")) // CallStack Function
		{
			if(values[i][0] != 0)
			{
				sd->pCallStackFunc = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_CALLSTACK_FUNC;
			}
		}
		else if(!stricmp(args[i], "err")) // Error Text
		{
			if(values[i][0] != 0)
			{
				sd->pErrorText = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_ERROR_TEXT;
			}
		}
		else if(!stricmp(args[i], "df")) // Data File
		{
			if(values[i][0] != 0)
			{
				sd->pDataFile = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_DATA_FILE;
			}
		}
		else if(!stricmp(args[i], "pn")) // Product Name
		{
			if(values[i][0] != 0)
			{
				sd->pProductName = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_PRODUCT_NAME;
			}
		}
		else if(!stricmp(args[i], "user")) // User Name
		{
			if(values[i][0] != 0)
			{
				sd->pUserName = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_USER_NAME;
			}
		}
		else if(!stricmp(args[i], "any")) // The "Any" string
		{
			if(values[i][0] != 0)
			{
				sd->pAny = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_ANY;
			}
		}
		else if(!stricmp(args[i], "expr")) // Expression
		{
			if(values[i][0] != 0)
			{
				sd->pExpression = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_EXPRESSION;
			}
		}
		else if(!stricmp(args[i], "catg")) // Category
		{
			if(values[i][0] != 0)
			{
				sd->pCategory = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_CATEGORY;
			}
		}
		else if(!stricmp(args[i], "summ")) // Category
		{
			if(values[i][0] != 0)
			{
				sd->pSummary = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_SUMMARY;
			}
		}
		else if(!stricmp(args[i], "ver")) // Version
		{
			if(values[i][0] != 0)
			{
				sd->pVersion = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_VERSION;
			}
		}
		else if(!stricmp(args[i], "sis")) // ShardInfoString
		{
			if(values[i][0] != 0)
			{
				sd->pShardInfoString = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_SHARDINFOSTRING;
			}
		}
		else if(!stricmp(args[i], "bran")) // Branch
		{
			if(values[i][0] != 0)
			{
				sd->pBranch = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_BRANCH;
			}
		}
		else if(!stricmp(args[i], "plat")) // Platform
		{
			if(values[i][0] != 0)
			{
				sd->ePlatform = (Platform)atoi(values[i]);
				if(sd->ePlatform)
					sd->uFlags |= SEARCHFLAG_PLATFORM;
			}
		}
		else if(!stricmp(args[i], "exe")) // Executable
		{
			if(values[i][0] != 0)
			{
				sd->pExecutable = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_EXECUTABLE;
			}
		}
		else if(!stricmp(args[i], "ip")) // IP Substring
		{
			if(values[i][0] != 0)
			{
				sd->pIP = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_IP;
			}
		}
		else if (!stricmp(args[i], "mem"))
		{
			if(values[i][0] != 0)
			{
				sd->pMemoryAlloc = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_LARGESTMEM;
			}
		}
		else if(!stricmp(args[i], "hp")) // Hide Programmers
		{
			int iHide = atoi(values[i]);
			if(iHide)
			{
				sd->bHideProgrammers = true;
			}
		}
		else if(!stricmp(args[i], "ha")) // Hide Assigned
		{
			int iHide = atoi(values[i]);
			if(iHide)
			{
				sd->bHideJiraAssigned = true;
			}
		}
		else if(!stricmp(args[i], "hd")) // Show Duplicates
		{
			int iShow = atoi(values[i]);
			if(iShow)
			{
				sd->bShowDuplicates = true;
			}
		}
		else if (!stricmp(args[i], "jira"))
		{
			if(values[i][0] != 0)
			{
				sd->iJira = atoi(values[i]);
			}
		}
		else if (!stricmp(args[i], "delete"))
		{
			if (pbPurgeEntries)
				*pbPurgeEntries = true;
		}
		else if (!stricmp(args[i], "globaltype"))
		{
			if(values[i][0] != 0)
			{
				sd->pGlobalType = strdup(values[i]);
				sd->uFlags |= SEARCHFLAG_GLOBALTYPE;
			}
		}
	}

	if(sd->uFlags & SEARCHFLAG_EXACT_ENABLED)
	{
		// If they've provided a start date, that is more accurate than "days ago"
		if(sd->uFlags & (SEARCHFLAG_EXACT_STARTDATE))
			sd->uFlags &= (~SEARCHFLAG_DAYSAGO);
		// Exact mode doesn't support non-fatal errors, unflag it
		sd->uTypeFlags &= (~SEARCH_TYPE_TO_FLAG(ERRORDATATYPE_ERROR));
	}
}

void wiSearchGenericCrashes(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	int i,size;
	ErrorEntry **ppEntries;

	estrClear(&sHTMLPage);
	ppEntries = ErrorEntry_GetGenericCrashes();

	estrConcatf(&sHTMLPage,
		"<table width=\"100%%\" class=\"summarytable\" cellpadding=3 cellspacing=0>"
		"<tr>"
		"<td align=right class=\"summaryheadtd\">ID</td>"
		"<td align=right class=\"summaryheadtd\">Type</td>"
		"<td align=right class=\"summaryheadtd\">Executable</td>"
		"<td align=right class=\"summaryheadtd\">Count</td>"
		"<td align=right class=\"summaryheadtd\">First Seen</td>"
		"<td align=right class=\"summaryheadtd\">Last Seen</td>"
		"<td align=right class=\"summaryheadtd\">Product</td>"
		"<td align=right class=\"summaryheadtd\">Jira</td>"
		"<td align=left class=\"summaryheadtdend\">Info</td>"
		"</tr>\n");

	size = eaSize(&ppEntries);
	for (i=0; i<size; i++)
	{
		ErrorEntry *p = ppEntries[i];
		int tempCount = p->iTotalCount;

		wiAppendSummaryTableEntry(p, &sHTMLPage, NULL, false, 0, 0, tempCount, STF_DEFAULT);
	}
	estrConcatf(&sHTMLPage, "</table>\n");
	estrConcatf(&sHTMLPage, "<br>Displaying %d entries.\n", size);

	httpSendWrappedString(link, sHTMLPage, NULL, pCookies);
}

typedef struct AsyncSearchInfo
{
	U32 link;
	CookieList *pCookies;

	char *s;
	bool bAssignmentMode;
	bool bShowForm;
	int iSearchCountLimit;
	int iMaxCallstackCount;
	int offset;
	bool bPurgeEntries;
	
	SearchData *sd;

} AsyncSearchInfo;

typedef struct SearchInfo
{
	SearchData *sd;
	ErrorEntry **entries;
} SearchInfo;

void initSearchReports()
{
	searchTable = stashTableCreateInt(512);
}

void wiDisplay(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	int i;
	char *type = "";
	SearchInfo *pSearchInfo;

	for(i=1; i<count; i++)
	{
		if(!stricmp(args[i], "type"))
			type = values[i];
	}

	;
	if(!stashIntFindPointer(searchTable, linkGetIp(link), &pSearchInfo) || !pSearchInfo)
	{
		httpSendWrappedString(link, "No search data for this link.", NULL, NULL);
		return;
	}

	if(!stricmp(type, "csv"))
	{
		wiDisplayCSV(link, pSearchInfo->sd, pSearchInfo->entries);
	}
	else if(!stricmp(type, "copypaste"))
	{
		wiDisplayCopyPaste(link, pSearchInfo->sd, pSearchInfo->entries);
	}
}

void wiSearch_CB(U32 **ppOutContainerIDs, NonBlockingContainerIterationSummary *pSummary, AsyncSearchInfo *userData);

static const char *exportOptions =
	"<table class=\"exacttable\">"
	"<tr><td><b>Export Options</b></td></tr>"
	"<tr><td style=\"text-align:center;\">"
	"<br>"
	"<a href=\"/display?type=csv\">Save as CSV</a><br>"
	"<br>"
	"<a href=\"/display?type=copypaste\">Display as Copy/Paste Friendly</a><br>"
	"<br>"
	"<br>"
	"</td></tr>"
	"</table>"
	;

void wiSearch(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	char *s = NULL;
	SearchData *sd = StructCreate(parse_SearchData);
	bool bSearch = (count > 1);
	bool bShowForm = true;
	bool bAssignmentMode = false;
	bool bTruncatedList = false;
	int iSearchCountLimit = -1;
	int iMaxCallstackCount = 0;
	char *pDateStart = "";
	char *pDateEnd = "";
	int offset = 0;
	bool bPurgeEntries = false;

	estrCopy2(&s, ""); // Init the static estring

	// ----------------------------------------------------------------------------
	// Rebuild our SearchData from the args

	// Non-zero defaults
	sd->bSortDescending = true;
	sd->iDaysAgo = 3;

	buildSearchData(link, sd, args, values, count, 
		&offset, &iMaxCallstackCount, &bShowForm, &bAssignmentMode, &iSearchCountLimit, 
		&pDateStart, &pDateEnd, &bPurgeEntries);

	// Zero defaults
	if(iSearchCountLimit == -1)
		iSearchCountLimit = 10;

	if(bSearch)
	{
		ErrorEntry **ppToDelete = NULL;

		if(sd->uFlags & SEARCHFLAG_EXACT_ENABLED)
		{
			// This fires up a new thread to read a bunch of rawdata files
			const char *id = startReport(sd, linkGetIp(link), NULL);
			if(id)
			{
				static char *reportURL = NULL;
				estrPrintf(&reportURL, "/report/%s", id);
				httpRedirect(link, reportURL);
				StructDestroy(parse_SearchData, sd);
				return;
			}
		}
		else
		{
			AsyncSearchInfo *data = (AsyncSearchInfo *)malloc(sizeof(AsyncSearchInfo));

			// ----------------------------------------------------------------------------
			// Rebuild the form from the SearchData

			if(bShowForm)
			{
				wiAppendSearchForm(link, &s, sd, 
					bAssignmentMode, iSearchCountLimit, iMaxCallstackCount, pDateStart, pDateEnd, exportOptions);
			}

			data->s = s;
			data->bAssignmentMode = bAssignmentMode;
			data->bPurgeEntries = bPurgeEntries;
			data->bShowForm = bShowForm;
			data->iMaxCallstackCount = iMaxCallstackCount;
			data->iSearchCountLimit = iSearchCountLimit;
			data->link = linkID(link);
			data->offset = offset;
			data->pCookies = StructClone(parse_CookieList, pCookies);
			data->sd = sd;

			giSearchIsRunning++;
			
			errorSearch_Begin(sd, wiSearch_CB, data);

			return;
		}
	}

	// ----------------------------------------------------------------------------
	// Rebuild the form from the SearchData

	if(bShowForm)
	{
		wiAppendSearchForm(link, &s, sd, 
			bAssignmentMode, iSearchCountLimit, iMaxCallstackCount, pDateStart, pDateEnd, "");
	}

	httpSendWrappedString(link, s, NULL, pCookies);
	StructDestroy(parse_SearchData, sd);
}

void wiSearch_CB(U32 **ppOutContainerIDs, NonBlockingContainerIterationSummary *pSummary, AsyncSearchInfo *userData)
{
	int i;
	bool bTruncatedList = false;
	int iSearchCount = 0;
	char *s = userData->s;
	NetLink *link = linkFindByID(userData->link);
	SearchInfo *pSearchInfo = NULL;
	ErrorEntry **ppSortedEntries = NULL;

	if(!link)
	{
		giSearchIsRunning--;

		StructDestroy(parse_SearchData, userData->sd);
		StructDestroy(parse_CookieList, userData->pCookies);
		estrDestroy(&s);
		free(userData);
		return;
	}

	if(userData->bAssignmentMode)
		formAppendStart(&s, "/jiracreateissue", "POST", "jiraform", NULL);

	if(!ppOutContainerIDs || !*ppOutContainerIDs)
	{
		estrConcatf(&s, "<b>No results found. Try extending your time range.</b><br>\n");
	}
	else
	{
		U32 *pContainerIDs = *ppOutContainerIDs;
		ErrorEntry **ppToDelete = NULL;
		ErrorEntry *p;
		SearchInfo *previousSearch = NULL;

		for (i = 0; i < eaiSize(&pContainerIDs); i++)
		{
			eaPush(&ppSortedEntries, findErrorTrackerByID(pContainerIDs[i]));
		}

		sortBySortOrder(ppSortedEntries, userData->sd->eSortOrder, userData->sd->bSortDescending);

		stashIntRemovePointer(searchTable, linkGetIp(link), &previousSearch);

		if (previousSearch)
		{
			StructDestroy(parse_SearchData, previousSearch->sd);
			eaDestroy(&previousSearch->entries);
			free(previousSearch);
			previousSearch = NULL;
		}

		pSearchInfo = malloc(sizeof(SearchInfo));
		pSearchInfo->sd = userData->sd;
		pSearchInfo->entries = ppSortedEntries;

		stashIntAddPointer(searchTable, linkGetIp(link), pSearchInfo, true);

		appendSearchResultsStart(&s, eaSize(&ppSortedEntries), userData->offset, userData->iSearchCountLimit, 
			userData->sd, userData->iMaxCallstackCount, userData->bShowForm, userData->bAssignmentMode);

		iSearchCount = 0;

		for(i = 0; i < eaSize(&ppSortedEntries); i++)
		{
			p = ppSortedEntries[i];
			if (userData->bPurgeEntries)
			{
				eaPush(&ppToDelete, p);
			}
			else
			{
				const char **ppExecutableNames = NULL;
				int tempCount = p->iTotalCount;

				if(userData->offset >= i + 1)
				{
					continue;
				}

				if(userData->iSearchCountLimit != 0)
					if(iSearchCount >= userData->iSearchCountLimit)
						break;

				if(userData->bAssignmentMode && (iSearchCount >= MAX_ASSIGNMENT_COUNT))
				{
					bTruncatedList = true;
					break;
				}

				wiAppendSummaryTableEntry(p, &s, ppExecutableNames, userData->bAssignmentMode, iSearchCount, userData->iMaxCallstackCount, tempCount, STF_DEFAULT);
				iSearchCount++;
			}
		}

		estrConcatf(&s, "</table>\n");

		if(userData->bAssignmentMode)
		{
			formAppendHiddenInt(&s, "count", iSearchCount);
			formAppendSubmit(&s, "Create All Issues");
			formAppendEnd(&s);
		}

		if (userData->bPurgeEntries)
		{
			int size = eaSize(&ppToDelete);

			if (!dirExists(STACK_SPRINTF("%s%s", gErrorTrackerDBBackupDir, getComputerName())) )
				mkdirtree(STACK_SPRINTF("%s%s", gErrorTrackerDBBackupDir, getComputerName()));

			loadstart_printf("Deleting %d entries...\n", size);
			for (i=0; i<size; i++)
			{
				p = ppToDelete[i];
				errorTrackerEntryDelete(p, true);
				if (i && i % 500 == 0)
				{
					if (gbETVerbose) printf("\rDeleted %d entries", i);
					if (i % 5000 == 0)
					{
						Sleep(1000);
					}
				}
			}
			eaDestroy(&ppToDelete);
			loadend_printf("\nDone.");
		}
	}

	appendSearchResultsEnd(&s, userData->offset, iSearchCount, userData->iSearchCountLimit, userData->sd, 
		userData->iMaxCallstackCount, userData->bShowForm, userData->bAssignmentMode, bTruncatedList);
	
	httpSendWrappedString(link, s, NULL, userData->pCookies);

	giSearchIsRunning--;

	if (!pSearchInfo)
		StructDestroy(parse_SearchData, userData->sd);
	StructDestroy(parse_CookieList, userData->pCookies);
	estrDestroy(&s);
	free(userData);
}

void wiGoto(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;

	estrCopy2(&s, ""); // Init the static estring

	estrConcatf(&s, "<div class=\"formdata\">\n");
	formAppendStart(&s, "/detail", "GET", "gotoform", NULL);

	estrConcatf(&s, "Go To ID: ");
	formAppendEdit(&s, 15, "id", "");
	formAppendSubmit(&s, "Go");

	formAppendEnd(&s);

	estrConcatf(&s, "</div>\n");

	estrConcatf(&s, "<hr size=1>\n");

	httpSendWrappedString(link, s, NULL, pCookies);
}

const char *GetUsefulVersionString(void); // SuperAssert.c

void wiVersion(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	estrPrintf(&s, "Version: %s", GetUsefulVersionString());
	httpSendWrappedString(link, s, NULL, pCookies);
}

void DumpEntryToStringForJira(char **estr, ErrorEntry *pEntry)
{
	int i;
	U32 uTime = timeSecondsSince2000();
	int iNumUsers = eaSize(&pEntry->ppUserInfo);
	int iOldestDaysAgo = 0;
	int iTotalDays;

	estrConcatf(estr, "\n");
	estrConcatf(estr, "Type '%s' [%d]: http://%s/detail?id=%d\n", ErrorDataTypeToString(pEntry->eType), pEntry->uID, getMachineAddress(), pEntry->uID);

	estrConcatf(estr, "  %d occurrences - (%d users) - days ago: (", pEntry->iTotalCount, eaSize(&pEntry->ppUserInfo));
	iTotalDays = calcElapsedDays(pEntry->uFirstTime, uTime);
	for(i=0; i<=iTotalDays; i++)
	{
		// Walking this timeline, by day:
		//           |---------------|------|
		//           |               |      |
		//       uFirstTime      --->i    uTime
		//
		//      (uFirstTime+iTotalDays) = uTime

		int iDayCount =  findDayCount(pEntry->ppDayCounts, i);
		int iDaysAgo  = (iTotalDays - i);

		if(i)
			estrConcatf(estr, ", ");

		estrConcatf(estr, "%d", iDayCount);
	}
	estrConcatf(estr, ")\n");

	if(pEntry->pLastBlamedPerson && pEntry->pDataFile)
	{
		estrConcatf(estr, "  (%s) %s\n", pEntry->pLastBlamedPerson, pEntry->pDataFile);
	}

	if(pEntry->pExpression)
	{
		estrConcatf(estr, "  Expression: %s\n", pEntry->pExpression);
	}

	if(pEntry->pErrorString)
	{
		estrConcatf(estr, "  Error: %s\n", pEntry->pErrorString);
	}

	if(ERRORDATATYPE_IS_A_CRASH(pEntry->eType))
	{
		bool bHasBlameInfo = hasValidBlameInfo(pEntry);

		estrConcatf(estr, "\nCallstack:\n\n");
		for(i=0; i<eaSize(&pEntry->ppStackTraceLines); i++)
		{
			const char *pBlamedPerson;
			char daysAgoBuffer[12];
			const char *pModuleName = getPrintableModuleName(pEntry, pEntry->ppStackTraceLines[i]->pModuleName);

			if(bHasBlameInfo)
			{
				pBlamedPerson = pEntry->ppStackTraceLines[i]->pBlamedPerson;
				sprintf(daysAgoBuffer, "%d", calcElapsedDays(pEntry->ppStackTraceLines[i]->uBlamedRevisionTime, uTime));
			}
			else
			{
				pBlamedPerson = "Unknown";
				sprintf(daysAgoBuffer, "???");
			}

			estrConcatf(estr, "* %s() - %s (%d) [%s:%s]\n",
				pEntry->ppStackTraceLines[i]->pFunctionName,
				pEntry->ppStackTraceLines[i]->pFilename,
				pEntry->ppStackTraceLines[i]->iLineNum,
				pBlamedPerson,
				daysAgoBuffer);
		}
	}

	estrConcatf(estr, "\nExecutables:\n");
	if (pEntry->ppExecutableCounts)
	{
		for(i=0; i<eaSize(&pEntry->ppExecutableNames); i++)
		{
			estrConcatf(estr, "* %s: %i\n", pEntry->ppExecutableNames[i], pEntry->ppExecutableCounts[i]);
		}
	}
	else
	{
		for(i=0; i<eaSize(&pEntry->ppExecutableNames); i++)
		{
			estrConcatf(estr, "* %s\n", pEntry->ppExecutableNames[i]);
		}
	}

	estrConcatf(estr, "\nUsers: (");
	for (i=0; i<eaSize(&pEntry->ppUserInfo); i++)
	{
		if(i)
			estrConcatf(estr, ", ");

		estrConcatf(estr, "%s (%d)", pEntry->ppUserInfo[i]->pUserName, pEntry->ppUserInfo[i]->iCount);
	}
	estrConcatf(estr, ")\n");

	if(pEntry->iMiniDumpCount > 0)
	{
		estrConcatf(estr, "  %d minidumps available (at time of bug creation).\n", pEntry->iMiniDumpCount);
	}

	if(pEntry->iFullDumpCount > 0)
	{
		estrConcatf(estr, "  %d fulldumps available (at time of bug creation).\n", pEntry->iFullDumpCount);
	}

	estrConcatf(estr, "\n");
}

static void jiraConcatTruncatedString (char **estr, const char *str, size_t max_length)
{
	if (strlen(str) <= max_length)
		estrConcatf(estr, " %s", str);
	else
	{
		char *truncate = (char*) str+(max_length-3);
		char temp = *truncate;
		*truncate = 0;
		estrConcatf(estr,  " %s...", str);
		*truncate = temp;
	}
}

static void jiraIssueDefaultSummary (char **estr, ErrorEntry *pEntry)
{
	estrPrintf(estr, "[%s ET %d]", ErrorDataTypeToString(pEntry->eType), pEntry->uID);
	switch (pEntry->eType)
	{
	case ERRORDATATYPE_ERROR:
		// Use up to the first 100 characters
		if (pEntry->pErrorString)
			jiraConcatTruncatedString(estr, pEntry->pErrorString, 100);
	xcase ERRORDATATYPE_ASSERT:
	case ERRORDATATYPE_FATALERROR: // Asserts and fatal errors are the same for this
		if (pEntry->pErrorString)
			jiraConcatTruncatedString(estr, pEntry->pErrorString, 100);
	xcase ERRORDATATYPE_CRASH:
		if (pEntry->pErrorString)
		{
			const char *exceptionInfo = strstri(pEntry->pErrorString, "Exception Caught: ");
			int callstackSize = eaSize(&pEntry->ppStackTraceLines);
			if (exceptionInfo)
				exceptionInfo += strlen("Exception Caught: ");
			else
				exceptionInfo = pEntry->pErrorString;
			jiraConcatTruncatedString(estr, exceptionInfo, 100);
			if (callstackSize > 0)
			{
				estrConcatf(estr, " - %s", pEntry->ppStackTraceLines[0]->pFunctionName);
				if (callstackSize > 1)
					estrConcatf(estr, " | %s", pEntry->ppStackTraceLines[1]->pFunctionName);
			}
		}
	xdefault:
		break;
	}
}

static bool generateJiraIssue(char **estr, ErrorEntry *pEntry, const char *pProject, const char *pAssignee, 
							  int iPriority, const char* pComponent, 
							  const char *pUserSummary, const char* pUserDescription, 
							  const char *pUsername, const char *pPassword)
{
	JiraComponent *pJiraComponent = NULL;
	bool bRet = false;
	char key[128];
	char *pComponentBreak = NULL;
	bool loggedIn = false;
	key[0] = 0;

	if (pUsername && pPassword)
		loggedIn = jiraLogin(getJiraHost(), getJiraPort(), pUsername, pPassword);
	else
		loggedIn = jiraDefaultLogin();

	if (loggedIn)
	{
		char *pSummary = NULL;
		char *pDescription = NULL;
		char *pDump = NULL;
		char *pMemoryDump = NULL;

		estrClear(&pDump);
		DumpEntryToStringForJira(&pDump, pEntry);

		if (pUserSummary)
			estrPrintf(&pSummary, "%s", pUserSummary);
		else
			jiraIssueDefaultSummary(&pSummary, pEntry);
		if (pEntry->ppMemoryDumps && eaSize(&pEntry->ppMemoryDumps))
		{
			char memoryDumpPath[MAX_PATH];
			FILE *file;
			calcMemoryDumpPath(memoryDumpPath, ARRAY_SIZE_CHECKED(memoryDumpPath), pEntry->uID, 
				pEntry->ppMemoryDumps[eaSize(&pEntry->ppMemoryDumps)-1]->iDumpIndex);
			file = fopen(memoryDumpPath, "r");
			if (file)
			{
#define NUM_LAST_MEMDUMP_LINES 9 // 5 for allocations, 4 for summary
				int iFileSize = fileGetSize(file);
				char *buffer = malloc (iFileSize +1);
				char *current = buffer, *eol = NULL;
				char *ppLastLines[NUM_LAST_MEMDUMP_LINES] = {NULL};
				int iNextLineIndex = 0, i;

				fread(buffer, 1, iFileSize , file);
				buffer[iFileSize] = 0;
				eol = (char*) strchr_fast(current, '\n');
				while (*current && eol != NULL)
				{
					*eol = 0;
					ppLastLines[iNextLineIndex] = current;
					iNextLineIndex = (iNextLineIndex + 1) % NUM_LAST_MEMDUMP_LINES;
					current = eol+1;
					while (*current && (*current == '\r' || *current == '\n'))
						current++;
					eol = (char*) strchr_fast(current, '\n');
				}
				if (*current) // this last line goes until EOF
					ppLastLines[iNextLineIndex] = current;
				for (i=iNextLineIndex; i < iNextLineIndex+NUM_LAST_MEMDUMP_LINES; i++)
				{
					if (ppLastLines[i%NUM_LAST_MEMDUMP_LINES])
						estrConcatf(&pMemoryDump, "%s\n", ppLastLines[i]);
				}
				free(buffer);
				fclose(file);
			}
		}
		estrPrintf(&pDescription, 
			"[Automatically Generated Bug from ErrorTracker] by\n"
			"%s\n"
			"\n"
			"Information:\n"
			"\n"
			"%s\n",

			getComputerName(),
			pDump);

		if (pUserDescription)
		{
			estrConcatf(&pDescription,
				"User Description of Bug:\n"
				"%s\n",
				pUserDescription);
		}

		if (pMemoryDump)
		{
			estrConcatf(&pDescription,
				"\nLast Lines of Memory Dump:\n"
				"%s\n",
				pMemoryDump);
		}

		if (pComponent)
		{
			char *pComponentCopy = strdup(pComponent);
			pComponentBreak = strstr(pComponentCopy, "\t");
			if (!pComponentBreak)
			{
				return false;
			}
			*pComponentBreak++ = '\0';
			pJiraComponent = StructCreate(parse_JiraComponent);
			estrCopy2(&pJiraComponent->pID, pComponentCopy);
			estrCopy2(&pJiraComponent->pName, pComponentBreak);
			free (pComponentCopy);
		}
		jiraFixupString(&pDescription);
		jiraFixupString(&pSummary);
		bRet = jiraCreateIssue(pProject, pSummary, pDescription, pAssignee, iPriority, 1, pJiraComponent, NULL, key, 128);

		estrDestroy(&pSummary);
		estrDestroy(&pDescription);
		estrDestroy(&pDump);

		if (pJiraComponent)
			StructDestroy(parse_JiraComponent, pJiraComponent);

		jiraLogout();
	}

	if(bRet)
	{
		NOCONST(JiraIssue) *pJiraIssue = NULL;
		estrConcatf(estr, "Generated an issue for ID %d. [assigned to %s] [<a target=\"_new\" "
			"href=\"http://%s:%d/browse/%s\">%s</a>]<br>\n", 
			pEntry->uID, pAssignee, 
			getJiraHost(), getJiraPort(), 
			key, key);

		pJiraIssue = StructCreateNoConst(parse_JiraIssue);
		if (pEntry->pJiraIssue)
		{
			StructCopyDeConst(parse_JiraIssue, pEntry->pJiraIssue, pJiraIssue, 0, 0, 0);
			objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
				"tempDestroyJiraIssue", "destroy pJiraIssue");
			// Temporarily set it to NULL while we call jiraGetIssue() to dodge potential thread issues
		}

		estrCopy2(&pJiraIssue->key, key);
		if (jiraDefaultLogin())
		{
			jiraGetIssue((JiraIssue*)pJiraIssue, NULL);
			AutoTrans_trErrorEntry_SetJiraIssue(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY,
				pEntry->uID, pJiraIssue->key, pJiraIssue->assignee, pJiraIssue->status, pJiraIssue->resolution);
		}
	}
	else
	{
		estrConcatf(estr, "Failed to generate an issue for ID %d.<br>\n", pEntry->uID);
	}

	return bRet;
}

static void wiAuthenticate(NetLink *link, const char *pUrl, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	int i;

	estrCopy2(&s, "");
	formAppendStart(&s, pUrl, "POST", "authenticateform", NULL);
	for (i=0; i<eaSize(&args->ppUrlArgList); i++)
	{
		formAppendHidden(&s, args->ppUrlArgList[i]->arg, args->ppUrlArgList[i]->value);
	}
	formAppendHidden(&s, "redirect", pReferer);
	formAppendAuthenticate(&s, 20, "username", "password");
	formAppendSubmit(&s, "Login");
	formAppendEnd(&s);
	
	estrConcatf(&s, "<br><br><a href=\"%s\">Go Back...</a>", pReferer);
	httpSendWrappedString(link, s, NULL, pCookies);
}
static void wiLoginGet(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	UrlArgumentList emptyArgs;
	emptyArgs.ppUrlArgList = NULL;
	wiAuthenticate(link, args[0], &emptyArgs, pReferer, pCookies);
}

static void wiLogout(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	const char *pUsername = findCookieValue(pCookies, "username");
	const char *pPassword = findCookieValue(pCookies, "password");

	estrCopy2(&s, "");
	if (!pUsername || !pPassword)
	{
		estrConcatf(&s, "You are not logged in to Jira!");
		//estrConcatf(&s, "<br><br><a href=\"%s\">Go Back...</a>", pReferer);
		httpSendWrappedString(link, s, NULL, pCookies);
	}
	else
	{
		Cookie *cookie = removeCookie(pCookies, "username");
		CookieList toRemove  = {0};
		if (cookie)
			eaPush(&toRemove.ppCookies, cookie);
		cookie = removeCookie(pCookies, "password");
		if (cookie)
			eaPush(&toRemove.ppCookies, cookie);

		estrConcatf(&s, "Successfuly logged out of Jira");
		//estrConcatf(&s, "<br><br><a href=\"%s\">Go Back...</a>", pReferer);
		httpSendWrappedStringClearCookies(link, s, NULL, pCookies, &toRemove, pReferer);
		StructDeInit(parse_CookieList, &toRemove);
	}
}

static bool wiConfirmJiraLogin(NetLink *link, CookieList *pCookies)
{
	static char *s = NULL;
	Cookie *pUsernameCookie = findCookie(pCookies, "username");
	Cookie *pPasswordCookie = findCookie(pCookies, "password");
	const char *pUsername = pUsernameCookie ? pUsernameCookie->pValue : NULL;
	const char *pPassword = pPasswordCookie ? pPasswordCookie->pValue : NULL;
	estrCopy2(&s, "");
	if (!pUsername || !pPassword || !jiraLogin(getJiraHost(), getJiraPort(), pUsername, pPassword))
	{
		CookieList toRemove = {0};
		if (pUsernameCookie)
		{
			eaFindAndRemove(&pCookies->ppCookies, pUsernameCookie);
			eaPush(&toRemove.ppCookies, pUsernameCookie);
		}
		if (pPasswordCookie)
		{
			eaFindAndRemove(&pCookies->ppCookies, pPasswordCookie);
			eaPush(&toRemove.ppCookies, pPasswordCookie);
		}
		estrConcatf(&s, "Failed to login to Jira.");
		httpSendWrappedStringClearCookies(link, s, NULL, pCookies, &toRemove, NULL);
		StructDeInit(parse_CookieList, &toRemove);
		return false;
	}
	jiraLogout();
	return true;
}
static void wiLogin(NetLink *link, const char *url, UrlArgumentList *arglist, const char *pReferer, CookieList *pCookies)
{
	const char *pRedirect;
	if (!httpIsJiraAuthenticated(link, url, arglist, pReferer, pCookies, NULL))
		return;
	if (wiConfirmJiraLogin(link, pCookies))
	{
		pRedirect = urlFindValue(arglist, "redirect");
		httpSendWrappedStringPlusCookies(link, "Successfully logged in to Jira.", NULL, pCookies, pRedirect);
	}
}


static void wiAppendJiraDetailing(NetLink *link, const char *url, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	static char *pDetailLink = NULL;
	int iUniqueID;
	const char *pAssignee = NULL;
	const char *pProject = NULL;
	const char *pUserDescription = NULL;

	const char *tempValue;

	ErrorEntry *pEntry;
	JiraComponentList *pComponentsList;
	JiraProject *pJiraProject;
	bool setCookies = false;

	if (!httpIsJiraAuthenticated(link, url, args, pReferer, pCookies, &setCookies))
		return;

	{
		tempValue = urlFindValue(args, "id");
		if(tempValue)
			iUniqueID = atoi(tempValue);
		else
			iUniqueID = 0;

		tempValue = urlFindValue(args, "assignee");
		if(tempValue)
		{
			ANALYSIS_ASSUME(tempValue != NULL);
			if (strcmp(tempValue, "--"))
				pAssignee = tempValue;
		}
		tempValue = urlFindValue(args, "project");
		if(tempValue)
		{
			ANALYSIS_ASSUME(tempValue != NULL);
			if (strcmp(tempValue, "--"))
				pProject = tempValue;
		}
		if (!(iUniqueID && pProject && pAssignee))
		{
			httpSendClientError(link, "ErrorTracker ID, Jira Project, or Jira Assignee not specified");
			return;
		}
		pEntry = findErrorTrackerByID(iUniqueID);
		if(!pEntry)
		{
			httpSendClientError(link, "Could not find the specified ErrorTracker entry");
			return;
		}

		pComponentsList = StructCreate(parse_JiraComponentList);
		loadJiraData();
		pJiraProject = findJiraProjectByKey(pProject);
		if (!pJiraProject)
		{
			httpSendClientError(link, "Could not find the project");
			return;
		}
		jiraGetComponents(pJiraProject, pComponentsList);
	}
	
	if (!wiConfirmJiraLogin(link, pCookies))
		return;

	estrCopy2(&s, "");
	estrCopy2(&pDetailLink, "");
	formAppendStart(&s, "/jiracreateissue", "POST", "jiraform", NULL);

	formAppendHiddenInt(&s, "count", 1);
	formAppendHiddenInt(&s, "id", iUniqueID);
	formAppendHidden(&s, "assignee", pAssignee);
	formAppendHidden(&s, "project", pProject);
	estrConcatf(&pDetailLink, "/detail?id=%d", iUniqueID);
	formAppendHidden(&s, "referer", pDetailLink);

	estrConcatf(&s, "<div class=\"heading\">ID #:</div>");
	estrConcatf(&s, "<div class=\"firstseen\">%d</div>\n", iUniqueID);

	estrConcatf(&s, "<div class=\"heading\">Project Name:</div>");
	estrConcatf(&s, "<div class=\"firstseen\">%s</div>\n", pProject);

	estrConcatf(&s, "<div class=\"heading\">Assigning Issue To:</div>");
	estrConcatf(&s, "<div class=\"firstseen\">%s</div>\n", pAssignee);

	estrConcatf(&s, "<div class=\"heading\">Priority:</div>");
	formAppendEnum(&s, "priority", "--|1 - Showstopper|2 - Critical|3 - Not Critical", 0);

	estrConcatf(&s, "<div class=\"heading\">Component:</div>");
	formAppendJiraComponents(&s, "component", pComponentsList);

	estrConcatf(&s, 
		"<div class=\"heading\">Summary:</div>\n");
	{
		char *pSummary = NULL;
		char *temp = NULL;
		jiraIssueDefaultSummary(&pSummary, pEntry);
		estrCopyWithHTMLEscapingSafe(&temp, pSummary, false);
		estrConcatf(&s, "<input class=\"formdata\" type=\"text\" size=\"120\" name=\"summary\" value=\"%s\" maxlength=\"250\">\n", temp);
		estrDestroy(&temp);
		estrDestroy(&pSummary);
	}

	estrConcatf(&s, 
		"<div class=\"heading\">Description:</div>\n"
		"Please enter a description of the steps to reproduce the bug, or what you were doing at the time of the crash.<br>\n");
	estrConcatf(&s, "<div style=\"font-family: Times, Arial, serif, sans-serif; font-size: large\">\n");
	formAppendTextarea(&s, 20, 80, "description", "");
	estrConcatf(&s, "</div>\n");

	estrConcatf(&s, "<br>");
	formAppendSubmit(&s, "Submit Issue");
	formAppendEnd(&s);

	estrConcatf(&s, "<br><br><a href=\"%s\">Go Back...</a>", pDetailLink);
	StructDestroy(parse_JiraComponentList, pComponentsList);
	if (setCookies)
		httpSendWrappedStringPlusCookies(link, s, wiGetTitle(pEntry), pCookies, NULL);
	else
		httpSendWrappedString(link, s, wiGetTitle(pEntry), pCookies);
}

static void wiJiraCreateIssue(NetLink *link, const char *url, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	int iUniqueID = 0;
	ErrorEntry *pEntry = NULL;
	const char *pAssignee = NULL;
	const char *pProject = NULL;
	const char *pComponent = NULL;
	const char *pDescription = NULL;
	const char *pSummary = NULL;
	const char *pRefererPassed = NULL;
	const char *tempValue;
	int count = 0;
	bool bJiraCreated = false;
	int iPriority = 0;
	bool setCookies = false;

	if (!httpIsJiraAuthenticated(link, url, args, pReferer, pCookies, &setCookies))
		return;
	if (!wiConfirmJiraLogin(link, pCookies))
		return;

	estrCopy2(&s, "");

	tempValue = urlFindValue(args, "count");
	if(tempValue)
		count = atoi(tempValue);

	tempValue = urlFindValue(args, "referer");
	if (tempValue)
		pRefererPassed = tempValue;
	else
		pRefererPassed = pReferer;

	if(!count)
	{
		httpSendClientError(link, "Creating issues for zero search results?");
		return;
	}

		tempValue = urlFindValue(args, "id");
		if(tempValue)
			iUniqueID = atoi(tempValue);

		tempValue = urlFindValue(args, "assignee");
		if(tempValue)
			pAssignee = tempValue;

		tempValue = urlFindValue(args, "project");
		if(tempValue)
			pProject = tempValue;

		tempValue = urlFindValue(args, "priority");
		if (tempValue)
			iPriority = atoi(tempValue);

		if (!iUniqueID)
		{
			httpSendWrappedString(link, "Error Tracker Entry ID must be provided for Jira creation.", NULL, pCookies);
			return;
		}
		if (!iPriority)
		{
			httpSendWrappedString(link, "Priority field is required for Jira creation.", NULL, pCookies);
			return;
		}
		if(!pAssignee || !strcmp(pAssignee, "--"))
		{
			httpSendWrappedString(link, "Assignee must be specified for Jira creation.", NULL, pCookies);
			return;
		}
		if(!pProject || !strcmp(pProject, "--"))
		{
			httpSendWrappedString(link, "Project must be specified for Jira creation.", NULL, pCookies);
			return;
		}

		tempValue = urlFindValue(args, "component");
		if  (tempValue && tempValue[0])
			pComponent = tempValue;

		tempValue = urlFindValue(args, "description");
		if  (tempValue && tempValue[0])
			pDescription = tempValue;

		tempValue = urlFindValue(args, "summary");
		if  (tempValue && tempValue[0])
			pSummary = tempValue;

		pEntry = findErrorTrackerByID(iUniqueID);
		if(pEntry)
		{
			// Its a valid request. Create an issue.
			bJiraCreated = generateJiraIssue(&s, pEntry, pProject, pAssignee, iPriority, pComponent, pSummary, pDescription,
				findCookieValue(pCookies, "username"), findCookieValue(pCookies, "password"));
		} 
		else 
		{
			httpSendWrappedString(link, "Unable to locate Error Tracker Entry for Jira creation.", NULL, pCookies);
			return;
		}


	if (bJiraCreated)
		estrConcatf(&s, "<br>Jira issue successfully created.");
	else
		estrConcatf(&s, "<br>Jira issue not created for an unknown reason. Quite possibly due to Jira itself.");

	estrConcatf(&s, "<br><br><a href=\"%s\">Go Back...</a>", pRefererPassed);
	if (setCookies)
		httpSendWrappedStringPlusCookies(link, s, wiGetTitle(pEntry), pCookies, NULL);
	else
		httpSendWrappedString(link, s, wiGetTitle(pEntry), pCookies);
}

static void wiErrorTrackerHandler(NetLink* link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	// Break up the URL into its pieces.
	// http://localhost/eth/4213-0.eth
	const char *url = args[0];
	int value_count;
	int id, dumpindex, file_len;
	const char *p = strstr(url, "/eth/");
	static char *textBlock = NULL;
	ErrorTrackerHandlerInfo info = {0};
	U32 magic, version, len, content_length;
	ErrorEntry *pEntry = NULL;
	DumpData *pDumpData = NULL;
	char dumpFilename[MAX_PATH];
	bool bFileExists;
	FILE *pFile;

	if(!p)
	{
		httpSendClientError(link, "Invalid ETH link. (1)\n");
		return;
	}

	p += strlen("/eth/");

	value_count = sscanf(p, "%d-%d.eth", &id, &dumpindex);

	if(value_count != 2)
	{
		httpSendClientError(link, "Invalid ETH link. (2)\n");
		return;
	}

	pEntry = findErrorTrackerByID(id);
	if(!pEntry)
	{
		httpSendClientError(link, "Invalid Entry ID.\n");
		return;
	}

	if(dumpindex < eaSize(&pEntry->ppDumpData))
	{
		assert(pEntry->ppDumpData);

		pDumpData = pEntry->ppDumpData[dumpindex];
	}
	else
	{
		httpSendClientError(link, "Invalid Dump Index.\n");
		return;
	}

	info.uID = id;
	info.bFullDump = (pDumpData->uFlags & DUMPFLAGS_FULLDUMP);
	estrClear(&textBlock);
	ParserWriteText(&textBlock, parse_ErrorTrackerHandlerInfo, &info, 0, 0, 0);

	magic   = MAGIC_HANDLER_HEADER;
	version = 1;
	len     = (int)strlen(textBlock);

	// TODO(Theo) Fix this?
	errorTrackerLibGetDumpFilename(dumpFilename, MAX_PATH, pEntry, pDumpData, false);

	bFileExists = fileExists(dumpFilename);
	if(!bFileExists)
	{
		httpSendClientError(link, dumpFilename);//"Cannot find dump file on the HD!\n");
		return;
	}

	file_len = fileSize(dumpFilename);

	content_length = sizeof(magic) + sizeof(version) + sizeof(len) + len + file_len;

	httpSendBasicHeader(link, content_length, "application/octet-stream");

	httpSendBytesRaw(link, &magic, sizeof(U32));
	httpSendBytesRaw(link, &version, sizeof(U32));
	httpSendBytesRaw(link, &len, sizeof(U32));
	httpSendBytesRaw(link, textBlock, len);

	// Now send the dump part

	if(file_len < WOULD_FIT_IN_SEND_BUFFER)
	{
		// Just send it immediately
		if (gbETVerbose) printf("Immediately sending file %s to link 0x%p\n", dumpFilename, link);
		httpSendFileRaw(link, dumpFilename);
		return;
	}
	
	pFile = fopen(dumpFilename, "rb");

	if(!pFile)
	{
		httpSendFileNotFoundError(link, url);
		return;
	}

	// Add a new LinkState for this
	if (gbETVerbose) printf("Sending large ETH %s to link 0x%p\n", dumpFilename, link);
	addLinkState(link, pFile);
}

U32 findSVNRevision(const char *pStr)
{
	const char *pLine = pStr;
	const char *pCurr = NULL;

	while(pLine && *pLine)
	{
		// SVN Revision parsing ... hunting for a line like this:
		// SVN Revision: any_text_here
		{
			pCurr = pLine;
			if(strStartsWith(pLine, "SVN Revision: "))
			{
				pCurr += (int)strlen("SVN Revision: ");
				return atoi(pCurr);
			}

			// Find the beginning of the next line
			pLine = strchr(pLine, '\n');
			if (pLine)
			{
				// Advance past the newline marker
				pLine++;
			}
			else
			{
				// No more lines!
				break;
			}
		}
	};

	return 0;
}

void wiStackInfo(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	char *pStackDump = "";
	bool bPerformStackLookup = false;
	int i;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "sd"))
		{
			pStackDump = values[i];
			bPerformStackLookup = true;
		}
	}

	estrCopy2(&s, "SVN Blame Stack Dump Lookup - Paste Stack Dump Here:<br>\n");
	estrConcatf(&s, "<div class=\"formdata\">\n");
	formAppendStart(&s, "/stackinfo", "GET", "stackform", NULL);
	formAppendTextarea(&s, 15, 80, "sd", pStackDump);
	estrConcatf(&s, "<br>\n");

	formAppendSubmit(&s, "Lookup Stack Info");

	formAppendEnd(&s);
	estrConcatf(&s, "</div>\n");

	if(bPerformStackLookup)
	{
		// Temporary struct data
		// TODO this does NOT work at all
		/*int id = 0;
		NOCONST(ErrorEntry) *pEntry = StructCreate(parse_ErrorEntry);
		parseStackTraceLines(pEntry, pStackDump);
		pEntry->iRequestedBlameVersion = findSVNRevision(pStackDump);

		id = startBlameCache(CONST_ENTRY(pEntry));
		httpRedirect(link, STACK_SPRINTF("/stackcache?id=%d", id));*/
		httpSendClientError(link, "This command has been disabled.");
		return;
	}

	httpSendWrappedString(link, s, NULL, pCookies);
}

#define PERCENTAGE_BAR_WIDTH (300)

void wiStackCache(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	int i;
	int id = 0;
	int current, total;
	bool complete = false;
	bool bExists = false;
	ErrorEntry *pEntry = NULL;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			id = atoi(values[i]);
		}
	}

	if(!id)
	{
		httpSendClientError(link, "What ID?\n");
		return;
	}

	bExists = getBlameCacheProgress(id, &current, &total, &complete);
	if(!bExists)
	{
		httpSendClientError(link, "There is no stack cache entry for that ID.\n");
		return;
	}

	if(!complete)
	{
		int iPercentage;
		
		if(total == 0)
		{
			iPercentage = 1;
		}
		else
		{
			iPercentage = (current * 100) / total;
		}

		estrPrintf(&s, "\n<meta http-equiv=\"refresh\" content=\"4\">\n");
		estrConcatf(&s, "Stack Cache Query Running [%d / %d], please wait...", current, total);
		estrConcatf(&s, "<table border=0 cellpadding=0 cellspacing=0 width=%d style=\"margin-left:auto;margin-right:auto;\">"
			            "<tr>"
						"<td width=%d bgcolor=\"#ff0000\">&nbsp;</td>"
						"<td width=%d bgcolor=\"#666666\">&nbsp;</td>"
						"</tr>"
						"</table>",
						PERCENTAGE_BAR_WIDTH,
						(PERCENTAGE_BAR_WIDTH * iPercentage) / 100,
						PERCENTAGE_BAR_WIDTH - ((PERCENTAGE_BAR_WIDTH * iPercentage) / 100));


		estrConcatf(&s, "<table width=100><tr>");
		estrConcatf(&s, "<td width=50>&nbsp;</td>");
		estrConcatf(&s, "</tr></table>");
		httpSendWrappedString(link, s, NULL, pCookies);
		return;
	}

	pEntry = getBlameCache(id);

	if(!pEntry)
	{
		// Should be impossible.
		httpSendClientError(link, "There is no stack cache entry for that ID. (??)\n");
		return;
	}

	estrCopy2(&s, "");
	estrConcatf(&s, "Stack Lookup: [Revision %d]<br><br>\n", pEntry->iCurrentBlameVersion);
	appendCallStackToString(pEntry, &s);
	httpSendWrappedString(link, s, wiGetTitle(pEntry), pCookies);
}

static void wiHashLookup(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	ErrorEntry *pEntry = NULL;
	char *pHashString = NULL;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "hash"))
		{
			pHashString = values[i];
		}
	}

	if(pHashString)
	{
		pEntry = errorTrackerLibLookupHashString(errorTrackerLibGetCurrentContext(), pHashString);
		if(pEntry)
		{
			char tempBuffer[16];
			sprintf(tempBuffer, "%d", pEntry->uID);
			httpSendBytes(link, tempBuffer, (U32)strlen(tempBuffer));
		}
	}

	httpSendBytes(link, "0", 1);
}

static void wiBacklog(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	int i;
	U32 uStartTime = 0;
	U32 uEndTime = 0;
	U32 uCount = 0;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "start"))
		{
			uStartTime = atoi(values[i]);
		}
		else if(!stricmp(args[i], "end"))
		{
			uEndTime = atoi(values[i]);
		}
		else if(!stricmp(args[i], "count"))
		{
			uCount = atoi(values[i]);
		}
	}

	backlogSend(link, uCount, uStartTime, uEndTime);
}

AUTO_STRUCT;
typedef struct XMLSingleResponse
{
	JiraIssue *jira; AST(UNOWNED)
	ErrorEntry *entry; AST(UNOWNED)
} XMLSingleResponse;

static void wiXMLSingle(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	static char *tempStr = NULL;

	int i;
	U32 uID = 0;
	U32 uIndex = 0;
	XMLSingleResponse response = {0};
	NOCONST(ErrorEntry) entry = {0};
	NOCONST(JiraIssue) emptyJiraIssue = {0};
	bool bLoaded = false;
	bool bGetJiraStatus = false;
	bool bShowUser = true;
	bool bShowTrivia = true;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			uID = atoi(values[i]);
		}
		else if(!stricmp(args[i], "index"))
		{
			uIndex = atoi(values[i]);
		}
		else if(!stricmp(args[i], "jira"))
		{
			bGetJiraStatus = (atoi(values[i]) == 1);
		}
		else if(!stricmp(args[i], "user"))
		{
			bShowUser = (atoi(values[i]) == 1);
		}
		else if(!stricmp(args[i], "trivia"))
		{
			bShowTrivia = (atoi(values[i]) == 1);
		}
	}

	response.entry = (ErrorEntry*)&entry;

	if(uID && uIndex)
	{
		bLoaded = loadErrorEntry(uID, uIndex, &entry);
	}

	if(bLoaded)
	{
		if(bGetJiraStatus)
		{
			ErrorEntry *pFullEntry = findErrorTrackerByID(uID);
			if(pFullEntry)
			{
				if(pFullEntry->pJiraIssue == NULL)
				{
					// I still want to indicate that I received the "jira=1", so a processing script
					// can detect a failure by checking if the status is -1. 

					emptyJiraIssue.status = -1;
					response.jira = (JiraIssue*)&emptyJiraIssue;
				}
				else
				{
					response.jira = (JiraIssue*)pFullEntry->pJiraIssue;
				}
			}
		}

		if(!bShowUser)
		{
			eaDestroyStructNoConst(&entry.ppUserInfo, parse_UserInfo);
		}

		if(!bShowTrivia)
		{
			eaDestroyStructNoConst(&entry.ppTriviaData, parse_TriviaData);
		}
	}

	estrClear(&tempStr);
	ParserWriteXML(&tempStr, parse_XMLSingleResponse, &response);
	httpSendStr(link, tempStr);

	if(bLoaded)
		StructDeInitNoConst(parse_ErrorEntry, &entry);
}

static void wiXMLEntry(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	static char *tempStr = NULL;
	ErrorEntry *pEntry = NULL;
	ErrorEntry blankEntry = {0};
	int i;
	U32 uID = 0;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			uID = atoi(values[i]);
		}
	}

	if(uID)
	{
		pEntry = findErrorTrackerByID(uID);
	}

	if(!pEntry)
	{
		pEntry = &blankEntry;
	}

	estrClear(&tempStr);
	ParserWriteXML(&tempStr, parse_ErrorEntry, pEntry);
	httpSendStr(link, tempStr);
}

static void wiReprocessDump(NetLink *link, const char *url, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	U32 uID = atoi(urlFindValue(args, "id"));
	U32 uDumpIndex = atoi(urlFindValue(args, "dumpidx"));
	ErrorEntry *pEntry = findErrorTrackerByID(uID);

	// Does not check for Jira Login
	if (pEntry && eaSize(&pEntry->ppDumpData) > (int) uDumpIndex)
		ReprocessDumpData(pEntry, uDumpIndex, link, false);
	//httpRedirect(link, pReferer);
}

static void wiRecalculateHash(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	int i, uID = 0;
	ErrorEntry *pEntry;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			uID = atoi(values[i]);
		}
	}

	pEntry = findErrorTrackerByID(uID);
	if (!pEntry || !ErrorEntry_IsGenericCrash(pEntry))
	{
		httpSendClientError(link, "No ErrorEntry found, or entry is not a generic crash.");
		return;
	}
	if (!ErrorEntry_ForceHashRecalculate(pEntry))
		httpSendClientError(link, "Recalculated ID is still in the \"Generic Crash\" list; could not save new hash.");
	else
		httpRedirect(link, pReferer);
}

static void wiMergeAndDelete(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	int i, uID = 0, targetID = 0;
	ErrorEntry *pEntry, *pTarget;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			uID = atoi(values[i]);
		}
		else if(!stricmp(args[i], "target"))
		{
			targetID = atoi(values[i]);
		}
	}

	pEntry = findErrorTrackerByID(uID);
	pTarget = findErrorTrackerByID(targetID);
	if (!pEntry || !pTarget)
	{
		httpSendClientError(link, "No ErrorEntry found!");
		return;
	}
	ErrorEntry_MergeAndDeleteEntry(pEntry, pTarget, false);
	{
		char buffer[64];
		sprintf(buffer, "/detail?id=%d", targetID);
		httpRedirect(link, buffer);
	}
}

static void wiDeleteGeneric(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList* pCookies)
{
	int i, uID = 0;
	ErrorEntry *pEntry;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			uID = atoi(values[i]);
		}
	}

	pEntry = findErrorTrackerByID(uID);
	if (!pEntry || !ErrorEntry_IsGenericCrash(pEntry))
	{
		httpSendClientError(link, "No ErrorEntry found, or entry is not a generic crash.");
		return;
	}
	errorTrackerEntryDelete(pEntry, true);
	httpSendWrappedString(link, "Entry Deleted!", NULL, pCookies);
}

static void wiToggleComments(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies)
{
	if (!pReferer || !*pReferer || strstri(pReferer, "togglecoments"))
		pReferer = "/search";
	if (hasCookie(pCookies, "hidecomments"))
	{
		CookieList toRemove = {0};
		Cookie *cookie = removeCookie(pCookies, "hidecomments");
		if (cookie)
			eaPush(&toRemove.ppCookies, cookie);
		httpSendWrappedStringClearCookies(link, "Showing all user comments", "", pCookies, &toRemove, pReferer);
		StructDeInit(parse_CookieList, &toRemove);
	}
	else
	{
		Cookie *commentCookie = StructCreate(parse_Cookie);
		commentCookie->pName = StructAllocString("hidecomments");
		commentCookie->pValue = StructAllocString("yes");
		eaPush(&pCookies->ppCookies, commentCookie);
		httpSendWrappedStringPlusCookies(link, "Hiding all user comments", "", pCookies, pReferer);
	}
}

static bool httpIsJiraAuthenticated(NetLink *link, const char *url, UrlArgumentList *arglist, const char *pReferer, 
									CookieList *pCookies, bool *pbSetCookies)
{
	if (!(hasCookie(pCookies, "username") && hasCookie(pCookies, "password")))
	{
		const char *pUsername;
		const char *pPassword;

		pUsername = urlFindValue(arglist, "username");
		pPassword = urlFindValue(arglist, "password");

		if (pUsername && pPassword)
		{
			Cookie *pNewCookie = StructCreate(parse_Cookie);
			pNewCookie->pName = strdup("username");
			pNewCookie->pValue = strdup(pUsername);
			eaPush(&pCookies->ppCookies, pNewCookie);

			pNewCookie = StructCreate(parse_Cookie);
			pNewCookie->pName = strdup("password");
			pNewCookie->pValue = strdup(pPassword);
			eaPush(&pCookies->ppCookies, pNewCookie);
			if (pbSetCookies)
				*pbSetCookies = true;
		}
		else
		{
			wiAuthenticate(link, url, arglist, pReferer, pCookies);
			return false;
		}
	}
	return true;
}

static bool httpHandleActualFileSend(NetLink *link, const char *pRoot, const char *pPath)
{
	char buf[MAX_PATH];
	const char *ext = strrchr(pPath, '.');
	const char *mimeContentType = httpChooseSensibleContentTypeForFileName(pPath);

	sprintf_s(SAFESTR(buf),"%s%s", pRoot, pPath);
	if(fileExists(buf))
	{
		sendFileToLink(link, buf, mimeContentType, false);
		return true;
	}

	// Check for the gzipped version of this file
	sprintf_s(SAFESTR(buf),"%s%s.gz", pRoot, pPath);
	if(fileExists(buf))
	{
		// Gzipped files are always sent as raw bytes
		sendFileToLink(link, buf, "application/octet-stream", true);
		return true;
	}

	// Check for "undashed" version of this ... a slightly ugly hack.
	sprintf_s(SAFESTR(buf),"%s%s", pRoot, pPath);
	strchrReplace(buf, '-', '\\');
	if(fileExists(buf))
	{
		sendFileToLink(link, buf, mimeContentType, false);
		return true;
	}

	// Check for "undashed", gzipped version of this ... another slightly ugly hack.
	sprintf_s(SAFESTR(buf),"%s%s.gz", pRoot, pPath);
	strchrReplace(buf, '-', '\\');
	if(fileExists(buf))
	{
		// Gzipped files are always sent as raw bytes
		sendFileToLink(link, buf, "application/octet-stream", true);
		return true;
	}

	return false;
}

static bool httpHandleActualFileSends(NetLink *link, const char *pPath)
{
	if(httpHandleActualFileSend(link, ETWeb_GetSourceDir(), pPath))
		return true;
	if(httpHandleActualFileSend(link, ETWeb_GetDataDir(), pPath))
		return true;
	if(httpHandleActualFileSend(link, getGenericServingStaticDir(), pPath))
		return true;

	// Now skip past "/dumps" and check dump dirs
	if(strstri(pPath, strrchr(gErrorTrackerSettings.pDumpDir, '/')) == pPath)
	{
		pPath += 6;

		if(httpHandleActualFileSend(link, gErrorTrackerSettings.pDumpDir, pPath))
			return true;

		EARRAY_FOREACH_BEGIN(gErrorTrackerSettings.ppAlternateDumpDir, i);
		{
			if(httpHandleActualFileSend(link, gErrorTrackerSettings.ppAlternateDumpDir[i], pPath))
				return true;
		}
		EARRAY_FOREACH_END;
	}

	return false;
}

bool wiDefaultGetHandler(NetLink *link, char **args, char **values, int count)
{
	char buf[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	if (!httpHandleActualFileSends(link, args[0]))
	{
		sprintf(buf, "%s%s", ETWeb_GetDataDir(), args[0]);
		if(dirExists(buf) && (!strnicmp(args[0], strrchr(gErrorTrackerSettings.pDumpDir, '/'), 6)))
		{
			httpSendDirIndex(link, args[0], buf);
			PERFINFO_AUTO_STOP_FUNC();
			return true;
		}
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}
	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

// Register Error Tracker wi handlers
void errorTrackerWebInterfaceInit(void)
{
	ETWeb_SetDefaultPage("/search");
	ETWeb_SetDefaultGetHandler(wiDefaultGetHandler);
	// GET handlers
	ETWeb_RegisterGetHandler("/activedumps", wiActiveDumps);
	ETWeb_RegisterGetHandler("/blockdumps", wiBlockDumps);
	ETWeb_RegisterGetHandler("/suppresserror", wiSuppressError);
	ETWeb_RegisterGetHandler("/cinfo", wiCharacterInfo);
	ETWeb_RegisterGetHandler("/detail", wiDetail);
	ETWeb_RegisterGetHandler("/detail_trivia", wiDetailTrivia);
	ETWeb_RegisterGetHandler("/disconnectall", wiDisconnectAll);
	ETWeb_RegisterGetHandler("/disconnectbyid", wiDisconnectByID);
	ETWeb_RegisterGetHandler("/dumpinfo", wiDumpInfo);
	ETWeb_RegisterGetHandler("/errorinfo", wiErrorInfo);
	ETWeb_RegisterGetHandler("/dumpnotify", wiDumpNotify);
	ETWeb_RegisterGetHandler("/removedumpnotify", wiRemoveDumpNotify);
	ETWeb_RegisterGetHandler("/fixupdumpdata", wiFixupDumpData);
	ETWeb_RegisterGetHandler("/flgimpcrash", wiFlagImportantCrash);
	ETWeb_RegisterGetHandler("/flgcallstack", wiFlagReplaceCallstack);
	ETWeb_RegisterGetHandler("/goto", wiGoto);
	ETWeb_RegisterGetHandler("/list", wiList);
	ETWeb_RegisterGetHandler("/login", wiLoginGet);
	ETWeb_RegisterGetHandler("/logout", wiLogout);
	ETWeb_RegisterGetHandler("/reqfulldump", wiReqFullDump);
	//ETWeb_RegisterGetHandler("/save", wiSaveErrorTrackerDB);
	ETWeb_RegisterGetHandler("/search", wiSearch);
	ETWeb_RegisterGetHandler("/display", wiDisplay);
	ETWeb_RegisterGetHandler("/toggleunlimitedusers", wiToggleUnlimitedUsers);
	ETWeb_RegisterGetHandler("/version", wiVersion);

	ETWeb_RegisterGetHandler("/removehash", wiRemoveHash);
	ETWeb_RegisterGetHandler("/viewgeneric", wiSearchGenericCrashes);
	ETWeb_RegisterGetHandler("/makegeneric", wiMarkAsGenericCrash);
	ETWeb_RegisterGetHandler("/ungenericify", wiUnmarkAsGenericCrash);
	ETWeb_RegisterGetHandler("/recalchash", wiRecalculateHash);
	ETWeb_RegisterGetHandler("/mergedelete", wiMergeAndDelete);	
	ETWeb_RegisterGetHandler("/delete", wiDeleteGeneric);	
		

	// These commands use multiple path levels and require "/<command>/" formatting
	ETWeb_RegisterGetHandler("/report/", wiReport);
	ETWeb_RegisterGetHandler("/triviareport/", wiTriviaReport);
	// For ErrorTrackerHandler style output
	ETWeb_RegisterGetHandler("/eth/", wiErrorTrackerHandler);

	// For doing SVN blame stack queries
	ETWeb_RegisterGetHandler("/stackcache", wiStackCache);
	ETWeb_RegisterGetHandler("/stackinfo", wiStackInfo);

	// Toggle Show/Hide User comments
	ETWeb_RegisterGetHandler("/togglecomments", wiToggleComments);

	// For finding IDs in the official ET externally, by knowing the hash value
	ETWeb_RegisterGetHandler("/hashlookup", wiHashLookup);

	// For getting the list of most recent crashes
	ETWeb_RegisterGetHandler("/backlog", wiBacklog);
	ETWeb_RegisterGetHandler("/xmlsingle", wiXMLSingle);
	ETWeb_RegisterGetHandler("/xmlentry", wiXMLEntry);

	ETWeb_RegisterPostHandler("/login", wiLogin);
	ETWeb_RegisterPostHandler("/jiradetailissue", wiAppendJiraDetailing);
	ETWeb_RegisterPostHandler("/jiracreateissue", wiJiraCreateIssue);
	ETWeb_RegisterPostHandler("/reprocessdump", wiReprocessDump);
}

#include "webinterface_c_ast.c"
