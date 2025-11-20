#include "WebInterface.h"

#include "ManageTickets.h"
#include "TicketEntry.h"
#include "TicketEntry_h_ast.h"
#include "TicketTracker.h"
#include "tickettracker_h_ast.h"

#include "TicketAssignment.h"
#include "TicketAssignment_h_ast.h"

#include "Authentication.h"
#include "Authentication_h_ast.h"

#include "Category.h"
#include "EntityDescriptor.h"
#include "trivia.h"
#include "htmlForms.h"
#include "textparser.h"
#include "file.h"
#include "earray.h"
#include "estring.h"
#include "timing.h"
#include "utils.h"
#include "sysutil.h"
#include "sock.h"
#include "net/net.h"
#include "HttpLib.h"
#include "HttpClient.h"
#include "Autogen/HttpLib_h_ast.h"
#include "Autogen/HttpClient_h_ast.h"
#include "net/netlink.h"
#include "Search.h"
#include "wininclude.h"
#include <winsock.h>
#include <winsock2.h>
#include "objContainer.h"
#include "ProductNames.h"
#include "jira.h"
#include "Message.h"
#include "csv.h"
#include "logging.h"
#include "Organization.h"
#include "UTF8.h"

#include "ticketnet.h" // for TicketData struct for creating tickets
#include "autogen/trivia_h_ast.h"
#include "autogen/jira_h_ast.h"

#define MAX_ASSIGNMENT_COUNT 50

// Non-NULL String
#define NNS(STR) ((STR)?STR:"")

#define TICKETCOOKIE_USERNAME ("ticketusername")
#define TICKETCOOKIE_PASSWORD ("ticketpassword")

extern ParseTable parse_TicketData[];
#define TYPE_parse_TicketData TicketData
//extern TicketTrackerUserList csReps;
extern char gTicketTrackerAltDataDir[MAX_PATH];
extern char gLogFileName[MAX_PATH];
extern int giHttpPort;
extern int giServerMonitorPort;

#define TICKET_FIRST_VERSION_STRING(p) (p->ppUserInfo && p->ppUserInfo[0]->pVersionString ? p->ppUserInfo[0]->pVersionString : p->pVersionString)

#define APPEND_URL_ARGS(ESTR,OFFSET,SORTORDER,DESC) \
{ \
	appendToURL(ESTR, "offset", "%d", (OFFSET)); \
	appendToURL(ESTR, "sort",   "%d", (SORTORDER)); \
	appendToURL(ESTR, "asc",    "%d", (DESC)?0:1); \
	appendToURL(ESTR, "limit",  "%d", iSearchCountLimit); \
	appendToURL(ESTR, "pn",     "%d", sd.iProductIndex); \
	appendToURLIfNotNull(ESTR, "any",    "%s", sd.pAny); \
	appendToURLIfNotNull(ESTR, "ver",    "%s", sd.pVersion); \
	appendToURLIfNotNull(ESTR, "trv",    "%s", sd.pTrivia); \
	appendToURLIfNotNull(ESTR, "trvcat", "%s", STACK_SPRINTF("%d", sd.iSearchTriviaKeyValue)); \
	appendToURLIfNotNull(ESTR, "sumdcp", "%s", sd.pSummaryDescription); \
	appendToURLIfNotNull(ESTR, "an",     "%s", sd.pAccountName); \
	appendToURLIfNotNull(ESTR, "char",   "%s", sd.pCharacterName); \
	appendToURLIfNotNull(ESTR, "ex",     "%s", sd.pExtraSubstring); \
	appendToURL(ESTR, "sumdcpflag", "%d", sd.iSearchSummaryDescription); \
    appendToURL(ESTR, "catg", "%d", categoryConvertToCombinedIndex(sd.iMainCategory, sd.iCategory) + 1); \
    appendToURL(ESTR, "vis", "%d", sd.iVisible); \
	appendToURL(ESTR, "plat", "%d", sd.ePlatform); \
	appendToURL(ESTR, "stat", "%d", sd.eStatus); \
	appendToURL(ESTR, "jira", "%d", sd.iJira); \
	appendToURLIfNotNull(ESTR, "repid", "%d", sd.pRepAccountName); \
	appendToURL(ESTR, "occur", "%d", sd.iOccurrences); \
	appendToURLIfNotNull(ESTR, "datestart", "%s", sd.pDateStartString); \
	appendToURLIfNotNull(ESTR, "dateend", "%s", sd.pDateEndString); \
	appendToURLIfNotNull(ESTR, "shard", "%s", sd.pShardInfoString); \
	appendToURLIfNotNull(ESTR, "shardname", "%s", sd.pShardExactName); \
	appendToURL(ESTR, "locale", "%d", sd.iLocaleID); \
}

#define APPEND_URL_ARGS_PSD(ESTR,OFFSET,SORTORDER,DESC) \
{ \
	appendToURL(ESTR, "offset", "%d", (OFFSET)); \
	appendToURL(ESTR, "sort",   "%d", (SORTORDER)); \
	appendToURL(ESTR, "asc",    "%d", (DESC)?0:1); \
	appendToURL(ESTR, "limit",  "%d", iSearchCountLimit); \
	appendToURL(ESTR, "pn",     "%d", psd->iProductIndex); \
	appendToURLIfNotNull(ESTR, "any",    "%s", psd->pAny); \
	appendToURLIfNotNull(ESTR, "ver",    "%s", psd->pVersion); \
	appendToURLIfNotNull(ESTR, "trv",    "%s", psd->pTrivia); \
	appendToURLIfNotNull(ESTR, "trvcat", "%s", STACK_SPRINTF("%d", psd->iSearchTriviaKeyValue)); \
	appendToURLIfNotNull(ESTR, "sumdcp", "%s", psd->pSummaryDescription); \
	appendToURLIfNotNull(ESTR, "an",     "%s", psd->pAccountName); \
	appendToURLIfNotNull(ESTR, "char",   "%s", psd->pCharacterName); \
	appendToURLIfNotNull(ESTR, "ex",     "%s", psd->pExtraSubstring); \
	appendToURL(ESTR, "sumdcpflag", "%d", psd->iSearchSummaryDescription); \
    appendToURL(ESTR, "catg", "%d", categoryConvertToCombinedIndex(psd->iMainCategory, psd->iCategory) + 1); \
    appendToURL(ESTR, "vis", "%d", psd->iVisible); \
	appendToURL(ESTR, "plat", "%d", psd->ePlatform); \
	appendToURL(ESTR, "stat", "%d", psd->eStatus); \
	appendToURL(ESTR, "jira", "%d", psd->iJira); \
	appendToURLIfNotNull(ESTR, "repid", "%d", psd->pRepAccountName); \
	appendToURL(ESTR, "occur", "%d", psd->iOccurrences); \
	appendToURLIfNotNull(ESTR, "datestart", "%s", psd->pDateStartString); \
	appendToURLIfNotNull(ESTR, "dateend", "%s", psd->pDateEndString); \
	appendToURLIfNotNull(ESTR, "shard", "%s", psd->pShardInfoString); \
	appendToURLIfNotNull(ESTR, "shardname", "%s", psd->pShardExactName); \
	appendToURL(ESTR, "locale", "%d", psd->iLocaleID); \
}
/*appendToURLIfNotNull(ESTR, "dcp",    "%s", psd->pDescription); \*/

// -------------------------------------------------------------------------------------

typedef bool (*requestHandlerFunc)(NetLink *link, char **args, char **values, int count);
static requestHandlerFunc spHandlerFunc = NULL;

void ticketTrackerLibSetRequestHandlerFunc(requestHandlerFunc pFunc)
{
	spHandlerFunc = pFunc;
}

// -------------------------------------------------------------------------------------

// Source Controlled
char gWebInterfaceRootDir[MAX_PATH] = "server\\TicketTracker\\WebRoot\\";
AUTO_CMD_STRING(gWebInterfaceRootDir, webInterfaceRootDir);

// Not Source Controlled
char gWebInterfaceAltRootDir[MAX_PATH] = "TicketTracker\\WebRoot\\";
AUTO_CMD_STRING(gWebInterfaceAltRootDir, webInterfaceAltRootDir);

char gDefaultPage[MAX_PATH] = "/search";

// -------------------------------------------------------------------------------------

static void httpHandlePost(NetLink *link, HttpClientStateDefault *pClientState);
static void httpHandleGet(char *data, NetLink *link, HttpClientStateDefault *pClientState);
static int httpDisconnect(NetLink* link, void *pIgnored);

bool initWebInterface(void)
{
	NetListen *listen_socket;
	for(;;)
	{
		setHttpDefaultPostHandler(httpHandlePost);
		setHttpDefaultGetHandler(httpHandleGet);

		listen_socket = commListen(ticketTrackerCommDefault(),LINKTYPE_HTTP_SERVER, LINK_HTTP, giHttpPort,
			httpDefaultMsgHandler, httpDefaultConnectHandler, httpDisconnect,sizeof(HttpClientStateDefault));
		if (listen_socket)
			break;
		Sleep(1);
	}

	initializeEntries();
	return true;
}

void shutdownWebInterface(void)
{
	cleanupLinkStates();
}

void constructHtmlHeaderEnd(char **estr, bool isLoggedIn, const char *pRedirect)
{
	if (pRedirect)
	{
		estrConcatf(estr, 
			"<META HTTP-EQUIV=\"Refresh\" CONTENT=\"1; URL=%s\">\n",
			pRedirect
		);
	}
	{
		char headerEnd[MAX_PATH];
		FILE *file = NULL;
		char *text = NULL;
		int size = 0;

		snprintf_s (headerEnd, MAX_PATH, "%s%s", gWebInterfaceRootDir, "headerEnd.html.format.txt" );
		file = fileOpen(headerEnd, "rb");
		size = fileGetSize(file);
		assert (file && size > 0);
		
		text = (char *) malloc(size + 1);
		fread(text, sizeof(char), size, file);
		text[size] = 0;

		estrConcatf(estr, FORMAT_OK(text),
			isLoggedIn ? "[<a href=\"/logout\">Logout</a>]" : "[<a href=\"/login\">Login</a>]");

		free(text);
		fclose(file);
	}
}

static TicketTrackerUser * wiConfirmLogin(CookieList *pCookies);
void httpSendWrappedString(NetLink *link, const char *pString, CookieList *pList)
{
	static char *pHeaderEnd = NULL;
	int iTotalLength;
	char headerBuf[MAX_PATH];
	char footerBuf[MAX_PATH];

	int iStrLen = (int)strlen(pString);
	int iHeaderEndLen = 0;

	estrCopy2(&pHeaderEnd, "");
	constructHtmlHeaderEnd(&pHeaderEnd, wiConfirmLogin(pList) != NULL, NULL);
	iHeaderEndLen = (int) strlen(pHeaderEnd);

	snprintf_s(headerBuf, MAX_PATH, "%s%s", gWebInterfaceRootDir, "style.html");
	snprintf_s(footerBuf, MAX_PATH, "%s%s", gWebInterfaceRootDir, "footer.html");

	iTotalLength  = iStrLen + iHeaderEndLen;
	iTotalLength += fileSize(headerBuf);
	iTotalLength += fileSize(footerBuf);

	httpSendBasicHeader(link, iTotalLength, NULL);
	httpSendFileRaw(link, headerBuf);
	httpSendBytesRaw(link, (void*) pHeaderEnd, iHeaderEndLen);
	httpSendBytesRaw(link, (void*)pString, iStrLen);
	httpSendFileRaw(link, footerBuf);
}

void httpSendWrappedStringPlusCookies(NetLink *link, const char *pString, CookieList *pList, const char *pRedirect)
{
	static char *pHeaderEnd = NULL;
	int iTotalLength;
	char headerBuf[MAX_PATH];
	char footerBuf[MAX_PATH];

	int iStrLen = (int)strlen(pString);
	int iHeaderEndLen = 0;
	
	estrCopy2(&pHeaderEnd, "");
	constructHtmlHeaderEnd(&pHeaderEnd, true, pRedirect);
	iHeaderEndLen = (int) strlen(pHeaderEnd);

	snprintf_s(headerBuf, MAX_PATH, "%s%s", gWebInterfaceRootDir, "style.html");
	snprintf_s(footerBuf, MAX_PATH, "%s%s", gWebInterfaceRootDir, "footer.html");

	iTotalLength  = iStrLen + iHeaderEndLen;
	iTotalLength += fileSize(headerBuf);
	iTotalLength += fileSize(footerBuf);

	httpSendBasicHeaderPlusCookies(link, iTotalLength, NULL, pList);
	httpSendFileRaw(link, headerBuf);
	httpSendBytesRaw(link, (void*) pHeaderEnd, iHeaderEndLen);
	httpSendBytesRaw(link, (void*)pString, iStrLen);
	httpSendFileRaw(link, footerBuf);
}

void httpSendWrappedStringClearCookies(NetLink *link, const char *pString, CookieList *pList, const char *pRedirect)
{
	static char *pHeaderEnd = NULL;
	int iTotalLength;
	char headerBuf[MAX_PATH];
	char footerBuf[MAX_PATH];
	int iStrLen = (int)strlen(pString);
	int iHeaderEndLen = 0;

	estrCopy2(&pHeaderEnd, "");
	constructHtmlHeaderEnd(&pHeaderEnd, false, pRedirect);
	iHeaderEndLen = (int) strlen(pHeaderEnd);

	snprintf_s(headerBuf, MAX_PATH, "%s%s", gWebInterfaceRootDir, "style.html");
	snprintf_s(footerBuf, MAX_PATH, "%s%s", gWebInterfaceRootDir, "footer.html");

	iTotalLength  = iStrLen + iHeaderEndLen;
	iTotalLength += fileSize(headerBuf);
	iTotalLength += fileSize(footerBuf);

	httpSendBasicHeaderClearCookies(link, iTotalLength, NULL, pList);
	httpSendFileRaw(link, headerBuf);
	httpSendBytesRaw(link, (void*) pHeaderEnd, iHeaderEndLen);
	httpSendBytesRaw(link, (void*)pString, iStrLen);
	httpSendFileRaw(link, footerBuf);
}


void SendWrappedError(NetLink *link, const char *pErrorMsg, CookieList *pCookies)
{
	httpSendWrappedString(link, pErrorMsg, pCookies);
}

// ------------------------------------------------
// Authentication

void formAppendAuthenticate(char **estr, int iEditSize, const char *pUserVarName, const char *pPasswordVarName)
{
	estrConcatf(estr,
		"<table>"
		"<tr><td colspan=\"2\" align=center><b>Login to Ticket Tracker</b></td></tr>"
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

static TicketTrackerUser * wiConfirmLogin(CookieList *pCookies)
{
	static TicketTrackerUser user = {0};
	const char *pUsername = findCookieValue(pCookies, TICKETCOOKIE_USERNAME);
	const char *pPassword = findCookieValue(pCookies, TICKETCOOKIE_PASSWORD);

	if (pUsername && pPassword)
	{
		strcpy(user.username, pUsername);
		strcpy(user.password, pPassword);
		return &user;
	}
	return NULL;
}

static bool wiConfirmJiraLogin(TicketTrackerUser *user)
{
	if (!user || !jiraLogin(jiraGetAddress(), jiraGetPort(), user->username, user->password))
	{
		return false;
	}
	jiraLogout();
	return true;
}


static bool wiConfirmPermission(NetLink *link, CookieList *pCookies, const char *pActionName, U32 uSubActionFlag, bool sendError)
{
	static char *s = NULL;
	const char *pUsername = findCookieValue(pCookies, TICKETCOOKIE_USERNAME);
	const char *pPassword = findCookieValue(pCookies, TICKETCOOKIE_PASSWORD);

	estrCopy2(&s, "");
	if (pUsername && pPassword)
	{
		/*TicketTrackerUser *pRep = verifyLogin(pUsername, pPassword);
		if (pRep)
		{
			if (hasAccessLevel(pRep, pActionName, uSubActionFlag))
			{
				return true;
			}
			else
			{
				if (sendError)
				{
					estrConcatf(&s, "User <b>%s</b> does not have the required permissions for %s %s.",
						pUsername, getSubActionString(uSubActionFlag), pActionName);
					SendWrappedError(link, s, pCookies);
				}
				return false;
			}
		}*/
		return true;
	}

	if (sendError)
	{
		estrConcatf(&s, "Login failed: invalid username and password pair.");
		httpSendWrappedStringClearCookies(link, s, pCookies, NULL);
	}
	return false;
}

U32 getLoginAccessLevel(CookieList *pCookies)
{
	/*TicketTrackerUser *pUser = getLoginUser(pCookies);
	return pUser ? pUser->uAccessLevel : 0;*/
	return ALVL_ADMIN;
}


static void wiAuthenticate(NetLink *link, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;

	estrCopy2(&s, "");
	formAppendStart(&s, "/login", "POST", "authenticateform", NULL);
	formAppendHidden(&s, "redirect", pReferer);
	formAppendAuthenticate(&s, 20, "username", "password");
	formAppendSubmit(&s, "Login");
	formAppendEnd(&s);
	
	estrConcatf(&s, "<br><br><a href=\"%s\">Go Back...</a>", pReferer);
	httpSendWrappedString(link, s, pCookies);
}

// ------------------------------------------------
// Web Pages
void appendParseTable(char **estr, U32 uDescriptorID, const char *pParseString)
{
	ParseTable **ppParseTables = NULL;
	void *pEntity = NULL;
	char *ptiName;
	
	bool loaded = loadParseTableAndStruct (&ppParseTables, &pEntity, &ptiName, uDescriptorID, pParseString);

	if (loaded)
	{
		char *tempPTIString = NULL;
		WriteHTMLContext htmlContext = {0};

		estrConcatf(estr, "\n<div class=\"heading\">%s</div>\n", ptiName ? ptiName : "Struct Information");
		ParserWriteHTML(&tempPTIString, ppParseTables[0], pEntity, &htmlContext);
		estrAppend2(estr, tempPTIString);
		estrDestroy(&tempPTIString);

		destroyParseTableAndStruct(&ppParseTables, &pEntity);
	}
}

void wiAppendSummaryTableEntry(TicketEntry *p, char **estr, int assignmentID, bool oddRow, bool addCheck, SearchData *pSearchData /* can be NULL */)
{
	static char * pEscaped = NULL;
	char datetime[256];
	char *prefix = oddRow ? "odd" : "even";
	TicketUserGroup *pGroup = p->uGroupID ? findTicketGroupByID(p->uGroupID) : NULL;
	U32 uOccurrences = FilterOccurenceCount(pSearchData, p);

	estrConcatf(estr, "<tr>\n");

	estrConcatf(estr, "<td class=\"summary%std\">[<a href=\"/detail?id=%d\">%d</a>]</td>\n", prefix, p->uID, p->uID);
	timeMakeLocalDateStringFromSecondsSince2000(datetime, p->uFiledTime);
	estrConcatf(estr, "<td class=\"summary%std\" nowrap>%s</td>\n", prefix, datetime);

	timeMakeLocalDateStringFromSecondsSince2000(datetime, p->uLastTime);
	estrConcatf(estr, "<td class=\"summary%std\" nowrap>%s</td>\n", prefix, datetime);
	// Ticket Status
	estrConcatf(estr, "<td class=\"summary%std\" nowrap>%s</td>\n", prefix, TranslateMessageKey(getStatusString(p->eStatus)));
	// Visibility
	estrConcatf(estr, "<td class=\"summary%std\" nowrap>%s</td>\n", prefix, TranslateMessageKey(getVisibilityString(p->eVisibility)));

	// Occurrences
	estrConcatf(estr, "<td class=\"summary%std\">%d</td>\n", prefix, uOccurrences);

	// Assigned To
	estrConcatf(estr, "<td class=\"summary%std\">%s</td>\n", prefix, p->pRepAccountName ? p->pRepAccountName : "--");

	// Product names
	estrConcatf(estr, "<td class=\"summary%std\">%s</td>\n", prefix, p->pProductName);
	// Version
	estrConcatf(estr, "<td class=\"summary%std\">%s</td>\n", prefix, TICKET_FIRST_VERSION_STRING(p));

	// Main Category
	estrConcatf(estr, "<td class=\"summary%std\">%s</td>\n", prefix, categoryGetTranslation(p->pMainCategory));
	// Category
	estrConcatf(estr, "<td class=\"summary%std\">%s</td>\n", prefix, categoryGetTranslation(p->pCategory));

	// Category Label
	estrConcatf(estr, "<td class=\"summary%std\">%s</td>\n", prefix, p->pLabel);

	// Group
	if (pGroup)
	{
		estrConcatf(estr, "<td class=\"summary%std\">"
			"<a href=\"/usergroup?id=%d\">%s</a></td>\n",
			prefix, pGroup->uID, pGroup->pName);
	}
	else
	{
		estrConcatf(estr, "<td class=\"summary%std\">%s</td>\n", prefix, "--");
	}

	if (p->pJiraIssue)
	{
		estrConcatf(estr, "<td class=\"summary%std\">"
			"<a href=\"%sbrowse/%s\">%s</a>"
			"</td>\n", 
			prefix, jiraGetDefaultURL(), p->pJiraIssue->key, p->pJiraIssue->key);
	}
	else
	{
		estrConcatf(estr, "<td class=\"summary%std\">--</td>\n", prefix);
	}

	estrCopyWithHTMLEscaping(&pEscaped, p->pSummary, false);
	estrConcatf(estr, "<td class=\"summary%stdend\">\n"
		"<span class=\"summary\">Summary: %s</span><br>\n", 
		prefix, pEscaped);
	estrCopyWithHTMLEscaping(&pEscaped, p->pUserDescription, false);
	estrConcatf(estr, "<span class=\"description\">%s</span><br>\n"
		"</td>\n", pEscaped);

	if (addCheck)
	{
		estrConcatf(estr, "<td class=\"summary%std\">\n", prefix);
		formAppendCheckBoxScripted(estr, STACK_SPRINTF("check%d", p->uID), "onClick=\"checkUncheckAll(this)\"", false);
		estrConcatf(estr, "</td>\n");
	}

	estrConcatf(estr, "</tr>\n");
}

void appendTriviaStrings(char **estr, TicketEntry *p)
{
	static char * pEscaped = NULL;
	int i;
	if (!p->ppTriviaData)
		return;

	estrConcatf(estr, "<table border=1 cellspacing=0 cellpadding=3>\n");

	estrConcatf(estr, 
		"<tr>"
		"<td class=\"summaryheadtd\">Key</td>\n"
		"<td class=\"summaryheadtd\">Value</td>\n"
		"</tr>");

	for (i=0; i<eaSize(&p->ppTriviaData); i++)
	{
		estrCopyWithHTMLEscaping(&pEscaped, p->ppTriviaData[i]->pKey, false);
		estrConcatf(estr,
			"<tr>"
			"<td cass=\"summarytd\">%s</td>\n", pEscaped);
		estrCopyWithHTMLEscaping(&pEscaped, p->ppTriviaData[i]->pVal, false);
		estrConcatf(estr, 
			"<td cass=\"summarytd\"><pre>%s</pre></td>\n"
			"</tr>", pEscaped);
	}
	estrConcatf(estr, "</table>\n");
}

void appendComments(char **estr, TicketEntry *p, bool canPost)
{
	static char * pEscaped = NULL;
	int i;
	estrConcatf(estr, "<div class=\"heading\">Comments</div>");

	estrConcatf(estr, "<table border=0 cellspacing=0 cellpadding=3>\n");

	for (i=0; i<eaSize(&p->ppComments); i++)
	{
		TicketComment *pc = p->ppComments[i];
		
		estrCopyWithHTMLEscaping(&pEscaped, pc->pComment, false);

		estrConcatf(estr,
			"<tr>"
			"<td cass=\"summarytd\"><b>%s:</b> </td>\n"
			"<td cass=\"summarytd\">%s</td>\n"
			"</tr>",
			pc->pUser, pEscaped);
	}
	estrConcatf(estr, "</table>\n");
	if (canPost)
	{
		formAppendStart(estr, "/comment", "POST", "formComment", NULL);
		formAppendHiddenInt(estr, "id", p->uID);
		estrConcatf(estr, "<div style=\"font-family: Times, Arial, serif, sans-serif; font-size: large\">\n");
		formAppendTextarea(estr, 5, 50, "comment", "");
		estrConcatf(estr, "</div>");
		formAppendSubmit(estr, "Add Comment");
		formAppendEnd(estr);
	}
}

void appendResponseToUser(char **estr, TicketEntry *p, bool canEdit)
{
	static char * pEscaped = NULL;

	estrConcatf(estr, "<div class=\"heading\">Response to User</div>");
	estrCopyWithHTMLEscaping(&pEscaped, p->pResponseToUser ? p->pResponseToUser : "", false);
	estrConcatf(estr, "<pre>%s</pre>", pEscaped ? pEscaped : "");

	if (canEdit)
	{
		formAppendStart(estr, "/respond", "POST", "formResponse", NULL);
		formAppendHiddenInt(estr, "id", p->uID);
		estrConcatf(estr, "<div style=\"font-family: Times, Arial, serif, sans-serif; font-size: large\">\n");
		formAppendTextarea(estr, 5, 50, "response", "");
		estrConcatf(estr, "</div>");
		formAppendSubmit(estr, "Edit Response to User");
		formAppendEnd(estr);
	}
}

void appendActionLog(char **estr, TicketEntry *p)
{
	int i;
	
	estrConcatf(estr, "<div class=\"heading\">Ticket Action Log</div>\n"
		"<table border=1 cellspacing=0 cellpadding=3>\n"
		"<td>Username</td>\n"
		"<td>Time</td>\n"
		"<td>Action String</td></tr>\n");
	for (i=0; i<eaSize(&p->ppLog); i++)
	{
		estrConcatf(estr, "<tr><td>%s</td>\n"
			"<td>%s</td>\n"
			"<td>%s</td></tr>\n", 
			p->ppLog[i]->actorName ? p->ppLog[i]->actorName : "Unknown", 
			timeGetLocalDateStringFromSecondsSince2000(p->ppLog[i]->uTime), 
			p->ppLog[i]->pLogString);
	}
	estrConcatf(estr, "</table>\n");
}

void IFunc_appendUserGroupToList(TicketUserGroup *pGroup, void **userData)
{
	int* pi = (int*) userData[0];
	const char **ppNames = (char**) (userData[1]);
	int *pValues = (int*) (userData[2]);

	ppNames[*pi] = pGroup->pName;
	pValues[*pi] = pGroup->uID;
	*pi = (*pi)+1;
}

void appendUserGroupList(char **estr, const char *pVarName, U32 uSelectedID)
{
	int i = 1;
	int iCount = getTicketGroupCount() + 1;
	const char **ppNames = calloc(iCount, sizeof(char*));
	int *pValues = calloc(iCount, sizeof(int));
	void *pGroupData[3];

	ppNames[0] = "--";
	pValues[0] = 0;

	pGroupData[0] = (void*) &i;
	pGroupData[1] = (void*) ppNames;
	pGroupData[2] = (void*) pValues;
	iterateOverTicketGroups(IFunc_appendUserGroupToList, pGroupData);

	formAppendSelection(estr, pVarName, ppNames, pValues, uSelectedID, iCount);
	free((void*)ppNames);
	free(pValues);
}

/*void appendLimitedUserGroupList(char **estr, const char *pVarName, U32 uSelectedID, TicketTrackerUser *pUser)
{
	int i;
	int iCount = eaiSize(&pUser->iUserGroups) + 1;
	const char **ppNames = calloc(iCount, sizeof(char*));
	int *pValues = calloc(iCount, sizeof(int));

	ppNames[0] = "--";
	pValues[0] = 0;
	for (i=1; i<iCount; i++)
	{
		pValues[i] = pUser->iUserGroups[i-1];
		ppNames[i] = findUserGroupByID(pValues[i])->pName;
	}
	formAppendSelection(estr, pVarName, ppNames, pValues, uSelectedID, iCount);
	free((char**) ppNames);
	free(pValues);
}

void appendAllUsersAndGroupsList(char **estr, const char *pVarName, U32 uSelectedID)
{
	int i;
	int iUserCount = getUserCount() + 1;
	int iGroupCount = getTicketGroupCount() + 1;
	const char **ppNames = calloc(iUserCount + iGroupCount, sizeof(char*));
	int *pValues = calloc(iUserCount + iGroupCount, sizeof(int));
	void *pUserData[3];

	ppNames[0] = "--";
	pValues[0] = 0;
	i = 1;
	pUserData[0] = (void*) &i;
	pUserData[1] = (void*) ppNames;
	pUserData[2] = (void*) pValues;
	iterateOverUserGroups(IFunc_appendUserGroupToList, pUserData);

	ppNames[iGroupCount] = "--"; // Group / User separator
	pValues[iGroupCount] = 0;
	
	i = 1+iGroupCount;
	iterateOverUsers(IFunc_appendUserToList, pUserData);

	formAppendSelection(estr, pVarName, ppNames, pValues, uSelectedID, iUserCount + iGroupCount);
	free((void*)ppNames);
	free(pValues);
}*/

/*void appendUsersInGroupList(char **estr, U32 uGroupID, const char *pVarName, U32 uSelectedID)
{
	int i;
	TicketUserGroup *pGroup = findTicketGroupByID(uGroupID);
	int iCount, iActualCount;
	const char ** ppNames;
	int *pValues;

	if (!pGroup || !pGroup->iMemberIDs)
		return;
	iCount = eaiSize(&pGroup->iMemberIDs) + 1;
	ppNames = calloc(iCount, sizeof(char*));
	pValues = calloc(iCount, sizeof(int));


	ppNames[0] = "--";
	pValues[0] = 0;
	iActualCount = 1;
	for (i=1; i<iCount; i++)
	{
		TicketTrackerUser *pRep = findUserByID(pGroup->iMemberIDs[i-1]);
		if (pRep)
		{
			ppNames[iActualCount] = pRep->pUsername;
			pValues[iActualCount] = pRep->uID;
			iActualCount++;
		}
	}
	formAppendSelection(estr, pVarName, ppNames, pValues, uSelectedID, iActualCount);
	free((void*)ppNames);
	free(pValues);
}*/

/*void appendGroupList(char **estr, const char *pVarName, U32 uSelectedID)
{
	int i;
	TicketEntryGroup **ppGroups = ticketTrackerGetCurrentContext()->entryList.groupList.ppList;
	int iCount = eaSize(&ppGroups) + 1;
	char **ppNames = calloc(iCount, sizeof(char*));
	int *pValues = calloc(iCount, sizeof(int));

	ppNames[0] = "--";
	pValues[0] = 0;
	for (i=1; i<iCount; i++)
	{
		ppNames[i] = ppGroups[i-1]->pNameShort;
		pValues[i] = ppGroups[i-1]->uID;
	}
	formAppendSelection(estr, pVarName, ppNames, pValues, uSelectedID, iCount);
	free(ppNames);
	free(pValues);
}*/

void wiAddComment(NetLink *link, UrlArgumentList * arglist, const char *pReferer, CookieList *pCookies)
{
	const char *pUsername = findCookieValue(pCookies, TICKETCOOKIE_USERNAME);
	const char *pIdString = urlFindValue(arglist, "id");
	const char *pCommentString = urlFindValue(arglist, "comment");
	int iUniqueID = 0;

	if (pIdString)
		iUniqueID = atoi(pIdString);
	if (!pIdString || !iUniqueID)
	{
		SendWrappedError(link, "Error: No ID or an invalid ID was provided.", pCookies);
		return;
	}
	if (!(ticketTrackerGetOptions() & TICKETOPTION_NO_ANON_COMMENTS) || pUsername)
	{
		if (pCommentString)
		{
			TicketEntry *p = findTicketEntryByID(iUniqueID);
			if (p)
			{
				ticketAddComment(p, pUsername ? pUsername : "Anonymous", pCommentString);
			}
		}
		httpRedirect(link, STACK_SPRINTF("/detail?id=%d", iUniqueID));
	}
	else
	{
		SendWrappedError(link, "Must be logged in to post comments!", pCookies);
	}
}

void wiEditResponse(NetLink *link, UrlArgumentList * arglist, const char *pReferer, CookieList *pCookies)
{
	const char *pIdString = urlFindValue(arglist, "id");
	const char *pResponseString = urlFindValue(arglist, "response");
	int iUniqueID = 0;

	if (pIdString)
		iUniqueID = atoi(pIdString);
	if (!pIdString || !iUniqueID)
	{
		SendWrappedError(link, "Error: No ID or an invalid ID was provided.", pCookies);
		return;
	}
	if (pResponseString)
	{
		TicketEntry *p = findTicketEntryByID(iUniqueID);
		if (p)
		{
			ticketChangeCSRResponse(p, "WebInterface", pResponseString);
		}
	}
	httpRedirect(link, STACK_SPRINTF("/detail?id=%d", iUniqueID));
}

static void wiChangeCategory(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	TicketEntry *pEntry = NULL;
	int iUniqueID = atoi(urlFindSafeValue(args, "id"));
	const char *pCategoryIndex = urlFindValue(args, "category");
	int index = atoi(pCategoryIndex);

	estrCopy2(&s, "");

	if(iUniqueID == 0 || pCategoryIndex == NULL)
	{
		httpSendWrappedString(link, "Ticket Tracker Entry ID and category key must be provided to change the category.", pCookies);
		return;
	}

	pEntry = findTicketEntryByID(iUniqueID);
	if(pEntry)
	{
		const char *pMainCategoryKey = NULL;
		int main, sub;

		categoryConvertFromCombinedIndex(index, &main, &sub);

		if (main > -1)
			pMainCategoryKey = categoryGetKey(main);
		if (pMainCategoryKey)
		{
			const char *pCategoryKey = NULL;
			if (sub > -1)
				pCategoryKey = subcategoryGetKey(main, sub);
			ticketChangeCategory(pEntry, NULL, pMainCategoryKey, pCategoryKey);
		}
		httpRedirect(link, STACK_SPRINTF("/detail?id=%d", iUniqueID));
	} 
	else 
	{
		httpSendWrappedString(link, "Unable to locate Ticket Tracker Entry for Category change.", pCookies);
		return;
	}
}

static void wiConcatChangeCategoryDropDown(char **estr, U32 uTicketID, const char *pCurrentCategory, const char *pCurrentSubcategory)
{
	const char **ppCategories = *(getCategoryStringsCombined());
	char combinedCategory[256] = "";

	if (pCurrentCategory)
	{
		if (pCurrentSubcategory)
			sprintf(combinedCategory, "%s - %s", categoryGetTranslation(pCurrentCategory), categoryGetTranslation(pCurrentSubcategory));
		else
			sprintf(combinedCategory, "%s (all)", categoryGetTranslation(pCurrentCategory));
	}

	estrConcatf(estr, "<form action=\"/changecategory\" method=\"POST\">\n");
	estrConcatf(estr, "<input type=\"hidden\" name=\"id\" value=\"%d\">\n", uTicketID);
	estrConcatf(estr, "<select class=\"formdata\" name=\"category\">\n");

	EARRAY_FOREACH_BEGIN(ppCategories, i);
	{
		const char *pSelected = "";
		if(combinedCategory[0] && !stricmp(ppCategories[i], combinedCategory))
		{
			pSelected = "SELECTED ";
		}

		estrConcatf(estr, "<option %svalue=\"%d\">%s</option>\n", pSelected, i, ppCategories[i]);
	}
	EARRAY_FOREACH_END;

	estrConcatf(estr, "</select>\n");
	estrConcatf(estr, "<input type=\"submit\" value=\"Change\">\n");
	estrConcatf(estr, "</form>\n");

	//estrConcatf(estr, "%s\n", categoryGetTranslation(pCurrentCategory));
}

void wiChangeTicketVisible(NetLink *link, char **args, char **values, int count, CookieList *pCookies)
{
	TicketEntry *pEntry = NULL;
	int i;
	int iUniqueID = 0;
	int iValue = -1;

	for (i=0; i<count; i++)
	{
		if(!stricmp(args[i], "id"))
		{
			iUniqueID = atoi(values[i]);
		}
		else if (!stricmp(args[i], "visible"))
		{
			iValue = atoi(values[i]);
		}
	}

	if(iUniqueID == 0 || iValue == -1)
	{
		httpSendWrappedString(link, "Ticket Tracker Entry ID and Visibility must be provided to modify the visibility.", pCookies);
		return;
	}

	pEntry = findTicketEntryByID(iUniqueID);

	if(pEntry)
	{
		changeTicketTrackerEntryVisible(NULL, pEntry, iValue);
		httpRedirect(link, STACK_SPRINTF("/detail?id=%d", iUniqueID));
	} 
	else 
	{
		httpSendWrappedString(link, "Unable to locate Ticket Tracker Entry for Visibility change.", pCookies);
	}
}

void wiDeleteTicketConfirm(NetLink *link, char **args, char **values, int count, CookieList *pCookies)
{
	static char *s = NULL;
	TicketEntry *pEntry = NULL;
	int i;
	int iUniqueID = 0;

	for (i=0; i<count; i++)
	{
		if(!stricmp(args[i], "id"))
		{
			iUniqueID = atoi(values[i]);
		}
	}

	if(iUniqueID == 0)
	{
		httpSendWrappedString(link, "No Ticket ID provided", pCookies);
		return;
	}

	pEntry = findTicketEntryByID(iUniqueID);

	if(pEntry)
	{
		estrClear(&s);

		estrConcatf(&s, "Are you absolutely sure you want to permanently delete Ticket ID #%d?\n This is an irreversible action!", pEntry->uID);
		formAppendStart(&s, "/deleteticket", "GET", "delete", NULL);
		formAppendHiddenInt(&s, "id", pEntry->uID);
		formAppendSubmit(&s, "Yes");
		formAppendEnd(&s);
		formAppendStart(&s, "/detail", "GET", "detail", NULL);
		formAppendHiddenInt(&s, "id", pEntry->uID);
		formAppendSubmit(&s, "No");
		formAppendEnd(&s);
		httpSendWrappedString(link, s, pCookies);
	} 
	else 
	{
		httpSendWrappedString(link, "Could not find Ticket to delete", pCookies);
	}
}

void wiDeleteTicket(NetLink *link, char **args, char **values, int count, CookieList *pCookies)
{
	static char *s = NULL;
	TicketEntry *pEntry = NULL;
	int i;
	int iUniqueID = 0;

	for (i=0; i<count; i++)
	{
		if(!stricmp(args[i], "id"))
		{
			iUniqueID = atoi(values[i]);
		}
	}

	if(iUniqueID == 0)
	{
		httpSendWrappedString(link, "No Ticket ID provided", pCookies);
		return;
	}

	pEntry = findTicketEntryByID(iUniqueID);

	if(pEntry)
	{
		filelog_printf(gLogFileName, "Ticket Delete - ID #%d", pEntry->uID);
		removeTicketTrackerEntry (pEntry);
		httpRedirect(link, "/");
	} 
	else 
	{
		httpSendWrappedString(link, "Could not find Ticket to delete", pCookies);
	}
}

void wiAppendEntryDataToString(TicketEntry *p, char **estr, U32 uFlags)
{
	static char * pEscaped = NULL;
	char datetime[256];
	U32 uTime = timeSecondsSince2000();
	int i;


	formAppendStart(estr, "/deleteconfirm", "GET", "delete", NULL);
	formAppendHiddenInt(estr, "id", p->uID);
	formAppendSubmit(estr, "Delete Ticket");
	formAppendEnd(estr);

	estrConcatf(estr, "<div class=\"heading\">Visible:</div>");
	
	formAppendStart(estr, "/changeVisibility", "GET", "changeVisibility", NULL);
	formAppendHiddenInt(estr, "id", p->uID);
	formAppendEnum(estr, "visible", "Private|Public|Hidden", p->eVisibility);
	estrConcatf(estr, "&nbsp;");
	formAppendSubmit(estr, "Change Visibility");
	formAppendEnd(estr);

	estrConcatf(estr, "<div class=\"entry\">");
	{
		estrConcatf(estr, "<div class=\"heading\">Assigned To:</div>");
		estrConcatf(estr, "%s", p->pRepAccountName ? p->pRepAccountName : "No one");
	}
	{
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">ID #:</div>");
		estrConcatf(estr, "<div class=\"firstseen\">%d</div>\n", p->uID);
		
		estrConcatf(estr, "<div class=\"heading\">Jira Issue:</div>\n");
		if (p->pJiraIssue)
		{
			estrConcatf(estr, "[<a href=\"%sbrowse/%s\">%s</a>] (%s)<br>\n"
				"Jira Status: %s<br>"
				"Jira Resolution: %s<br>", 
				jiraGetDefaultURL(), p->pJiraIssue->key, p->pJiraIssue->key, 
				p->pJiraIssue->assignee, jiraGetStatusString(p->pJiraIssue->status), jiraGetResolutionString(p->pJiraIssue->resolution));
		}
		else
		{
			formAppendStart(estr, "/jiradetailissue", "GET", "jira", NULL);
			formAppendHiddenInt(estr, "id", p->uID);
			formAppendJiraProjects(estr, "project");
			formAppendSubmit(estr, "Create Jira Issue");
			formAppendEnd(estr);

			formAppendStart(estr, "/jiraassocissue", "GET", "jira", NULL);
			formAppendHiddenInt(estr, "id", p->uID);
			formAppendEdit(estr, 25, "key", "");
			formAppendSubmit(estr, "Associate Jira Issue");
			formAppendEnd(estr);
		}
		// ---------------------------------------------------------------
		timeMakeLocalDateStringFromSecondsSince2000(datetime, p->uFiledTime);
		estrConcatf(estr, "<div class=\"heading\">Ticket Filed:</div>");
		estrConcatf(estr, "<div class=\"firstseen\">%s</div>\n", datetime);
		
		if (p->uStartTime)
		{
			timeMakeLocalDateStringFromSecondsSince2000(datetime, p->uStartTime);
			estrConcatf(estr, "<div class=\"heading\">Ticket Resolution Started:</div>");
			estrConcatf(estr, "<div class=\"firstseen\">%s</div>\n", datetime);
		}

		if (p->uEndTime && p->uEndTime >= p->uStartTime)
		{
			timeMakeLocalDateStringFromSecondsSince2000(datetime, p->uEndTime);
			estrConcatf(estr, "<div class=\"heading\">Ticket Resolution Completed:</div>");
			estrConcatf(estr, "<div class=\"firstseen\">%s</div>\n", datetime);
		}

		estrConcatf(estr, "<div class=\"heading\">Ticket Status:</div>");
		if (p->eResolution && p->eStatus == TICKETSTATUS_RESOLVED)
		{
			estrConcatf(estr, "%s (%s)\n", TranslateMessageKey(getStatusString(p->eStatus)), TranslateMessageKey(getResolutionString(p->eResolution)) );
		}
		else
			estrConcatf(estr, "%s\n", TranslateMessageKey(getStatusString(p->eStatus)));

		{
			estrConcatf(estr, "<div>\n");
			formAppendStart(estr, "/status", "GET", "changestatus", NULL);
			formAppendHiddenInt(estr, "id", p->uID);
			formAppendEnum(estr, "status", "Open|Closed By Player|Resolved|In Progress|Pending|Merged|Processed", p->eStatus-1);
			estrConcatf(estr, "&nbsp;");
			formAppendSubmit(estr, "Change Status");
			formAppendEnd(estr);
			estrConcatf(estr, "</div>\n");
		}
		{
			estrConcatf(estr, "<div>\n");
			switch (p->eStatus)
			{
			case TICKETSTATUS_OPEN: // start progress, close
				formAppendStart(estr, "/status", "GET", "repaction", NULL);
				formAppendHiddenInt(estr, "id", p->uID);
				formAppendHiddenInt(estr, "status", TICKETSTATUS_IN_PROGRESS-1);
				formAppendSubmit(estr, "Start Progress");
				formAppendEnd(estr);
				
				formAppendStart(estr, "/status", "GET", "repaction1", NULL);
				formAppendHiddenInt(estr, "id", p->uID);
				formAppendHiddenInt(estr, "status", TICKETSTATUS_CLOSED-1);
				formAppendSubmit(estr, "Close Ticket");
				formAppendEnd(estr);
				break;
			case TICKETSTATUS_CLOSED: // reopen
				formAppendStart(estr, "/status", "GET", "repaction", NULL);
				formAppendHiddenInt(estr, "id", p->uID);
				formAppendHiddenInt(estr, "status", TICKETSTATUS_OPEN-1);
				formAppendSubmit(estr, "Reopen Ticket");
				formAppendEnd(estr);
				break;
			case TICKETSTATUS_RESOLVED: // reopen
				formAppendStart(estr, "/status", "GET", "repaction", NULL);
				formAppendHiddenInt(estr, "id", p->uID);
				formAppendHiddenInt(estr, "status", TICKETSTATUS_OPEN-1);
				formAppendSubmit(estr, "Reopen Ticket");
				formAppendEnd(estr);
				break;
			case TICKETSTATUS_IN_PROGRESS: // resolve
				formAppendStart(estr, "/status", "GET", "repaction", NULL);
				formAppendHiddenInt(estr, "id", p->uID);
				formAppendHiddenInt(estr, "status", TICKETSTATUS_RESOLVED-1);
				formAppendSubmit(estr, "Resolve Ticket");
				formAppendEnd(estr);
				break;
			default:
				break;
			}
			estrConcatf(estr, "</div>\n");
		}

		// ---------------------------------------------------------------
	}

	{
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Platform:</div>");
		estrConcatf(estr, "%s\n", getPlatformName(p->ePlatform));
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Product:</div>");
		estrConcatf(estr, "%s\n", p->pProductName);
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Version:</div>");
		estrConcatf(estr, "%s", TICKET_FIRST_VERSION_STRING(p));
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Langauge:</div>");
		estrConcatf(estr, "%s", locGetName(locGetIDByLanguage(p->eLanguage)));
	}

	{
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Group:</div>");
		if (p->uGroupID)
		{
			TicketUserGroup *pGroup = findTicketGroupByID(p->uGroupID);
			if (pGroup)
				estrConcatf(estr, "<a href=\"/usergroup?id=%d\">%s</a>\n", pGroup->uID, pGroup->pName);
			else
				estrConcatf(estr, "No Group");
		}
		else
		{
			estrConcatf(estr, "No Group");
		}

		formAppendStart(estr, "/changegroup", "GET", "changegroup", NULL);
		formAppendHiddenInt(estr, "id", p->uID);
		appendUserGroupList(estr, "groupid", p->uGroupID);
		estrConcatf(estr, "&nbsp;");
		formAppendSubmit(estr, "Change Group");
		formAppendEnd(estr);
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Label:</div>");
		estrConcatf(estr, "%s\n", p->pLabel);
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Category:</div>");
		wiConcatChangeCategoryDropDown(estr, p->uID, p->pMainCategory, p->pCategory);
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Summary:</div>");
		estrCopyWithHTMLEscaping(&pEscaped, p->pSummary, false);
		estrConcatf(estr, "%s\n", pEscaped);
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">User Description:</div>");
		estrConcatf(estr, "<pre>");
		estrCopyWithHTMLEscaping(&pEscaped, p->pUserDescription, false);
		estrConcatf(estr, "%s", pEscaped);
		estrConcatf(estr, "</pre>");
		// ---------------------------------------------------------------
		if (p->gameLocation.zoneName || p->pDebugPosString)
		{

			char *debugPosString = NULL;
			estrStackCreate(&debugPosString);
			estrConcatf(estr, "<div class=\"heading\">User Position:</div>");
			ticketConstructDebugPosString(&debugPosString, p);
			estrConcatf(estr, "%s\n<br>\n", debugPosString);
			estrDestroy(&debugPosString);
		}
		// ---------------------------------------------------------------
	}

	{
		/*estrConcatf(estr, "<div class=\"heading\">Account Name:</div>\n%s\n", p->pAccountName);
		estrConcatf(estr, "<div class=\"heading\">Character Name:</div>\n%s\n", p->pCharacterName ? p->pCharacterName : "");*/
		estrConcatf(estr, "<table class=\"summarytable\" cellpadding=3 cellspacing=0>\n");
		estrConcatf(estr, "<tr>\n"
			"<td class=\"summaryheadtd\">Filed Time</td>\n"
			"<td class=\"summaryheadtd\">Account Name</td>\n"
			"<td class=\"summaryheadtd\">Character Name</td>\n"
			"<td class=\"summaryheadtd\">Shard Info</td>\n"
			"</tr>");
		for (i=0; i<(int) p->uOccurrences; i++)
		{
			char *pRowClass = (i % 2) ? "summaryeventd" : "summaryoddtd";
			timeMakeLocalDateStringFromSecondsSince2000(datetime, p->ppUserInfo[i]->uFiledTime);
			estrConcatf(estr, "<tr>\n");
			estrConcatf(estr, "<td class=\"%s\">%s</td>\n"
				"<td class=\"%s\">%s</td>\n"
				"<td class=\"%s\">%s</td>\n"
				"<td class=\"%s\">%s</td>\n</tr>\n",
				pRowClass, datetime, 
				pRowClass, p->ppUserInfo[i]->pAccountName,
				pRowClass, p->ppUserInfo[i]->pCharacterName,
				pRowClass, p->ppUserInfo[i]->pShardInfoString);
		}
		estrConcatf(estr, "</table>\n");

		if (p->uEntityDescriptorID && p->pEntityFileName)
		{
			estrConcatf(estr, "<div class=\"heading\"><a href=\"/cinfo?id=%d\">Character Info</a></div>\n",
				p->uID);
		}
	}
	// Server Info
	if (p->pShardInfoString)
	{
		estrConcatf(estr, "<div class=\"heading\">Shard Info:</div>\n%s\n", p->pShardInfoString);
		if (p->iGameServerID)
			estrConcatf(estr, "<div class=\"heading\">Game Server %d</div>\n", p->iGameServerID);
	}
	{
		if (p->eaiUserDataDescriptorIDs)
		{
			int size = eaiSize(&p->eaiUserDataDescriptorIDs);
			estrConcatf(estr, "\n<div class=\"heading\">Extra Data</div>\n");
			for (i=0; i<size; i++)
			{
				char *pStructName = NULL;
				getEntityDescriptorName(&pStructName, p->eaiUserDataDescriptorIDs[i]);
				estrConcatf(estr, "<div class=\"heading\"><a href=\"/cinfo?id=%d&userdata=%d\">%s</a></div>\n",
					p->uID, i, pStructName ? pStructName : STACK_SPRINTF("User Data #%d", i));
			}
		}
	}
	{	
		estrConcatf(estr, "\n<div class=\"heading\">Trivia Strings</div>\n");
		appendTriviaStrings(estr, p);
	}
	{
		if (p->pScreenshotFilename)
			estrConcatf(estr, "<img src=\"http://%s:%d/file/tickettracker/0/fileSystem/%s\"/>", getMachineAddress(), giServerMonitorPort, p->pScreenshotFilename);
	}
	{
		appendComments(estr, p, true);
		appendResponseToUser(estr, p, true);
	}
	{
		appendActionLog(estr, p);
	}

	estrConcatf(estr, "</div>\n");

	estrConcatf(estr, "\n<br><hr size=1>\n<br>\n");
}

void wiDetail(NetLink *link, char **args, char **values, int count, CookieList* pCookies)
{
	static char *s = NULL; // Reuse this estring so that its growth stabilizes after a few hits
	int i;
	int iUniqueID = 0;
	TicketEntry *pEntry = NULL;

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
		SendWrappedError(link, "Please supply an 'id' to get any details. Thanks.", pCookies);
		return;
	}

	pEntry = findTicketEntryByID(iUniqueID);

	if(!pEntry)
	{
		SendWrappedError(link, "Detail not found for that ID.", pCookies);
		return;
	}

	wiAppendEntryDataToString(pEntry, &s, 0);
	httpSendWrappedString(link, s, pCookies);
}

void wiCharacterInfo(NetLink *link, char **args, char **values, int count, CookieList* pCookies)
{
	static char *s = NULL; // Reuse this estring so that its growth stabilizes after a few hits
	int i;
	int iUniqueID = 0;
	int iUserDataIndex = -1;
	TicketEntry *pEntry = NULL;

	estrCopy2(&s, "");

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			iUniqueID = atoi(values[i]);
		}
		else if (!stricmp(args[i], "userdata"))
		{
			iUserDataIndex = atoi(values[i]);
		}
	}

	if(iUniqueID == 0)
	{
		SendWrappedError(link, "Please supply an 'id' to get any details. Thanks.", pCookies);
		return;
	}

	pEntry = findTicketEntryByID(iUniqueID);

	if(!pEntry)
	{
		SendWrappedError(link, "Detail not found for that ID.", pCookies);
		return;
	}
	if (iUserDataIndex < 0)
	{
		if (!pEntry->pEntityFileName)
		{
			SendWrappedError(link, "No attached character info found for that ID.", pCookies);
			return;
		}

		estrConcatf(&s, "<div><a href=\"/detail?id=%d\">Return to Ticket Entry</a></div>\n", pEntry->uID);
		appendEntityParseTable(&s, pEntry->uID, pEntry->uEntityDescriptorID, pEntry->pEntityFileName, false);
	}
	else
	{
		if (iUserDataIndex >= eaiSize(&pEntry->eaiUserDataDescriptorIDs))
		{
			SendWrappedError(link, "Invalid custom data index for that ID.", pCookies);
			return;
		}

		estrConcatf(&s, "<div><a href=\"/detail?id=%d\">Return to Ticket Entry</a></div>\n", pEntry->uID);
		appendParseTable(&s, pEntry->eaiUserDataDescriptorIDs[iUserDataIndex], pEntry->eaUserDataStrings[iUserDataIndex]);
	}
	httpSendWrappedString(link, s, pCookies);
}

void appendSearchTableHeader(char **estr, int offset, int iSearchCountLimit, SearchData *psd, const char *pPage, bool addCheckBoxes)
{
	estrConcatf(estr, "<tr><td align=right class=\"summaryheadtd\">");

	if (psd)
	{
		estrConcatf(estr, "<a href=\"/%s", pPage);
		APPEND_URL_ARGS_PSD(estr, offset, SORTORDER_ID, (psd->eSortOrder == SORTORDER_ID)?!psd->bSortDescending:psd->bSortDescending);
		estrConcatf(estr, "\">ID</a></td>\n");
	}
	else 
		estrConcatf(estr, "ID</td>\n");

	estrConcatf(estr, "<td align=right class=\"summaryheadtd\">");
	if (psd)
	{
		estrConcatf(estr, "<a href=\"/%s", pPage);
		APPEND_URL_ARGS_PSD(estr, offset, SORTORDER_NEWEST, (psd->eSortOrder == SORTORDER_NEWEST)?!psd->bSortDescending:psd->bSortDescending);
		estrConcatf(estr, "\">Ticket Filed</a></td>\n");
	}
	else
		estrConcatf(estr, "Ticket Filed</td>\n");

	estrConcatf(estr, 
		"<td align=right class=\"summaryheadtd\">Last Seen</td>\n"
		"<td align=right class=\"summaryheadtd\">Ticket Status</td>\n"
		"<td align=right class=\"summaryheadtd\">Visible</td>\n");

	estrConcatf(estr, "<td align=right class=\"summaryheadtd\">");
	if (psd)
	{
		estrConcatf(estr, "<a href=\"/%s", pPage);
		APPEND_URL_ARGS_PSD(estr, offset, SORTORDER_COUNT, (psd->eSortOrder == SORTORDER_COUNT)?!psd->bSortDescending:psd->bSortDescending);
		estrConcatf(estr, "\">Occurrences</a></td>\n");
	}
	else 
		estrConcatf(estr, "Occurrences</td>\n");

	estrConcatf(estr,
		"<td align=right class=\"summaryheadtd\">Assigned To</td>\n"
		"<td align=right class=\"summaryheadtd\">Product Name</td>\n"
		"<td align=right class=\"summaryheadtd\">Version</td>\n"
		"<td align=right class=\"summaryheadtd\">Main Category</td>\n"
		"<td align=right class=\"summaryheadtd\">Subcategory</td>\n"
		"<td align=right class=\"summaryheadtd\">Label</td>\n");
	
	estrConcatf(estr, "<td align=right class=\"summaryheadtd\">\n");
	if (psd)
	{
		estrConcatf(estr, "<a href=\"/%s", pPage);
		APPEND_URL_ARGS_PSD(estr, offset, SORTORDER_GROUPID, (psd->eSortOrder == SORTORDER_GROUPID)?!psd->bSortDescending:psd->bSortDescending);
		estrConcatf(estr, "\">Group</a></td>\n");
	}
	else
		estrConcatf(estr, "Group</td>\n");
		
	estrConcatf(estr, "<td align=left class=\"summaryheadtd\">Jira</td>\n");

	estrConcatf(estr, "<td align=left class=\"summaryheadtd\">Summary and Description</td>\n");

	if (addCheckBoxes)
		estrConcatf(estr, "<td align=left class=\"summaryheadtdend\"></td>\n");
	estrConcatf(estr,"</tr>\n");
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

static void buildSearchData (SearchData *psd, char **args, char **values, int count, int *pOffset, int *pSearchCountLimit)
{
	int i;
	
	// Non-zero defaults
	searchDataInitialize(psd);
	psd->eAdminSearch = SEARCH_ADMIN;

	for(i=0;i<count;i++)
	{
		if (values[i])
			estrTrimLeadingAndTrailingWhitespace(&values[i]);
		else
			continue;
		if(!stricmp(args[i], "offset"))
		{
			if (pOffset)
				*pOffset = atoi(values[i]);
		}
		else if(!stricmp(args[i], "an")) // Account Name
		{
			if(values[i][0] != 0)
			{
				psd->pAccountName = values[i];
				psd->uFlags |= SEARCHFLAG_ACCOUNT_NAME;
			}
		}
		else if(!stricmp(args[i], "char")) // Character Name
		{
			if(values[i][0] != 0)
			{
				psd->pCharacterName = values[i];
				psd->uFlags |= SEARCHFLAG_CHARACTER_NAME;
			}
		}
		else if(!stricmp(args[i], "catg")) // Category
		{
			if(values[i][0] != 0)
			{
				int iCombinedIndex = atoi(values[i]) - 1;

				categoryConvertFromCombinedIndex(iCombinedIndex, &psd->iMainCategory, &psd->iCategory);

				if (psd->iMainCategory > -1)
					psd->uFlags |= SEARCHFLAG_CATEGORY;
				else
				{
					psd->iMainCategory = -1;
					psd->iCategory = -1; // make sure it stays at -1
				}
			}
		}
		else if(!stricmp(args[i], "locale")) // Locale / Language
		{
			if(values[i][0] != 0)
			{
				psd->iLocaleID = atoi(values[i]);
				if (psd->iLocaleID > 0)
					psd->uFlags |= SEARCHFLAG_LANGUAGE;
				else if (psd->iLocaleID < 0)
					psd->iLocaleID = 0; // make sure it stays at -1
			}
		}
		else if(!stricmp(args[i], "sumdcp")) // Summary and/or Description
		{
			if(values[i][0] != 0)
			{
				psd->pSummaryDescription = values[i];
				psd->uFlags |= SEARCHFLAG_SUMMARY_DESCRIPTION;
			}
		}
		else if(!stricmp(args[i], "sumdcpflag")) // Summary/Description Flag for which to search
		{
			psd->iSearchSummaryDescription = atoi(values[i]);
		}
		else if(!stricmp(args[i], "trv"))
		{
			if(values[i][0] != 0)
			{
				psd->pTrivia = values[i];
				psd->uFlags |= SEARCHFLAG_TRIVIA;
			}
		}
		else if(!stricmp(args[i], "trvcat"))
		{
			psd->iSearchTriviaKeyValue = atoi(values[i]);
		}
		else if(!stricmp(args[i], "sort"))
		{
			psd->uFlags |= SEARCHFLAG_SORT;
			psd->eSortOrder = (SortOrder)CLAMP(atoi(values[i]), 0, SORTORDER_MAX);
		}
		else if(!stricmp(args[i], "asc"))
		{
			psd->bSortDescending = !(atoi(values[i]) == 1);
		}
		else if(!stricmp(args[i], "pn")) // Product Name
		{
			if(values[i][0] != 0)
			{
				int idx = atoi(values[i]);
				psd->iProductIndex = idx;
				if (idx > 0)
					psd->pProductName = (char*) productNameGetString(idx-1);
				if (psd->pProductName)
					psd->uFlags |= SEARCHFLAG_PRODUCT_NAME;
			}
		}
		else if (!stricmp(args[i], "ver")) // Version
		{
			if(values[i][0] != 0)
			{
				psd->uFlags |= SEARCHFLAG_VERSION;
				psd->pVersion = values[i];
			}
		}
		else if(!stricmp(args[i], "plat")) // Platform
		{
			if(values[i][0] != 0)
			{
				psd->ePlatform = (Platform)atoi(values[i]);
				if(psd->ePlatform)
					psd->uFlags |= SEARCHFLAG_PLATFORM;
			}
		}
		else if(!stricmp(args[i], "ex")) // Extra Data
		{
			if(values[i][0] != 0)
			{
				psd->pExtraSubstring = values[i];
				psd->uFlags |= SEARCHFLAG_EXTRA;
			}
		}
		else if(!stricmp(args[i], "any")) // The "Any" string
		{
			if(values[i][0] != 0)
			{
				psd->pAny = values[i];
				psd->uFlags |= SEARCHFLAG_ANY;
			}
		}
		else if(!stricmp(args[i], "limit"))
		{
			if (pSearchCountLimit)
			{
				*pSearchCountLimit = atoi(values[i]);
				if(*pSearchCountLimit < 1)
				{
					*pSearchCountLimit = 0;
				}
			}
		}
		else if (!stricmp(args[i], "stat"))
		{
			if(values[i][0] != 0)
			{
				psd->eStatus = atoi(values[i]);
				if (psd->eStatus)
					psd->uFlags |= SEARCHFLAG_STATUS;
			}
		}
		else if (!stricmp(args[i], "jira"))
		{
			if(values[i][0] != 0)
			{
				psd->iJira = atoi(values[i]);
			}
		}
		else if (!stricmp(args[i], "repid"))
		{
			if (values[i][0] != 0)
			{
				psd->pRepAccountName = values[i];
				if (psd->pRepAccountName)
					psd->uFlags |= SEARCHFLAG_ASSIGNED;
			}
		}
		else if (!stricmp(args[i], "occur"))
		{
			psd->iOccurrences = atoi(values[i]);
			if (psd->iOccurrences > 0)
				psd->uFlags |= SEARCHFLAG_OCCURRENCES;
		}
		else if (!stricmp(args[i], "datestart"))
		{
			psd->uDateStart = timeGetSecondsSince2000FromLocalDateString(values[i]);
			psd->pDateStartString = values[i];
			if (psd->uDateStart)
				psd->uFlags |= SEARCHFLAG_DATESTART;
		}
		else if (!stricmp(args[i], "dateend"))
		{
			psd->uDateEnd = timeGetSecondsSince2000FromLocalDateString(values[i]);
			psd->pDateEndString = values[i];
			if (psd->uDateEnd)
				psd->uFlags |= SEARCHFLAG_DATEEND;
		}
		else if (!stricmp(args[i], "shard"))
		{
			if (values[i][0] != 0)
			{
				psd->pShardInfoString = values[i];
				psd->uFlags |= SEARCHFLAG_SHARD;
			}
		}
		else if (!stricmp(args[i], "shardname"))
		{
			if (values[i][0] != 0)
			{
				psd->pShardExactName = values[i];
				psd->uFlags |= SEARCHFLAG_SHARD;
			}
		}
		else if (!stricmp(args[i], "vis"))
		{
			if (values[i][0] != 0)
			{
				psd->iVisible = atoi(values[i]);
				if (psd->iVisible)
					psd->uFlags |= SEARCHFLAG_VISIBILITY;
			}
		}
	}
}

typedef void (*FilteredCsvHeaderFileWrite_CB) (FileWrapper *file, ParseTable *pti, void *filter);
typedef void (*FilteredCsvFileWrite_CB)       (FileWrapper*file, void *data, void *filter);

static void filteredCsvWriteToFile(SA_PARAM_NN_STR const char *fileName, void ** ppData, void *filter, 
                                   ParseTable *pti, FilteredCsvHeaderFileWrite_CB header_cb, 
								   FilteredCsvFileWrite_CB element_cb)
{
	FileWrapper *file = fopen(fileName, "w");
	int i, size;

	if (!file)
		return;

	if (header_cb)
		header_cb(file, pti, filter);

	if (!element_cb)
		return;

	size = eaSize(&ppData);
	for (i=0; i<size; i++)
	{
		element_cb(file, ppData[i], filter);
	}
	fclose(file);
}

void wiSaveCSV(DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;
	bool bSearch = count > 1;
	char *pFileName = NULL;
	char filedir[MAX_PATH];

	sprintf(filedir, "%s\\%s", fileLocalDataDir(), gTicketTrackerAltDataDir);
	GetTempFileName_UTF8(filedir, "CSV", 0, &pFileName);
	estrCopy2(&s, "");
	if (bSearch)
	{
		SearchData sd = {0};
		TicketEntry *p;
		int iSearchCount = 0;

		buildSearchData(&sd, args, values, count, NULL, NULL);

		p = searchFirst(&sd);
		filteredCsvWriteToFile(pFileName, sd.ppSortedEntries, &sd, parse_TicketEntryConst, TicketEntry_CsvHeaderFileCB, TicketEntry_CsvFileCB);
		searchEnd(&sd);
	}
	sendTextFileToLink(link, pFileName, "TicketTracker.csv", "text/plain", true);

	estrDestroy(&pFileName);
}

static const char ** wiLocaleNameList(void)
{
	static const char **sList = NULL;
	static char *sUnsupportedLanguage = "[Unsupported]";
	
	if (!sList)
	{
		int i;
		//for (i=0; i<LANGUAGE_MAX; i++)
		for (i=0; i<LOCALE_ID_COUNT; i++)
		{	
			//int localeID = locGetIDByLanguage(i);
			if (locIDIsValid(i))
				eaPush(&sList, locGetName(i));
			else
				eaPush(&sList, sUnsupportedLanguage);
		}
	}
	return sList;
}
 
void wiSearch(NetLink *link, char **args, char **values, int count, CookieList* pCookies)
{
	static char *s = NULL;
	SearchData sd = {0};
	TicketEntry *p;
	bool bShowForm = true;
	bool bSearch = count > 1;
	int iSearchCount = 0;
	int iSearchCountLimit = -1;
	int offset = 0;
	int i;

	estrCopy2(&s, ""); // Init the static estring

	// ----------------------------------------------------------------------------
	// Rebuild our SearchData from the args

	buildSearchData(&sd, args, values, count, &offset, &iSearchCountLimit);

	// Zero defaults
	if(iSearchCountLimit == -1)
	{
		iSearchCountLimit = 20;
	}

	if(bShowForm)
	{
		char ibuffer[10];
		char datetime[64];

		estrConcatf(&s, "<div class=\"formdata\">\n");
		formAppendStart(&s, "/search", "GET", "searchform", NULL);

		// Start outer column table
		estrConcatf(&s, "\n<table width=100%%><tr><td valign=top>\n");
		// Start inner table
		estrConcatf(&s, "\n<table>\n");

		estrConcatf(&s, "<tr><td>Sort By:  </td><td>");
		formAppendEnum(&s, "sort", sortOrderEntireEnumString(), (int)sd.eSortOrder);
		estrConcatf(&s, "</td></tr>\n");

		estrConcatf(&s, "<tr><td>Ascending:  </td><td>");
		formAppendCheckBox(&s, "asc", !sd.bSortDescending);
		estrConcatf(&s, " ");
		
		estrConcatf(&s, "<tr><td>Ticket Status:  </td><td>");
		formAppendEnum(&s, "stat", "--|Open|Closed By Player|Resolved|In Progress|Pending|Merged|Processed|Player Edited", sd.eStatus);
		estrConcatf(&s, "</td></tr>\n");

		estrConcatf(&s, "<tr><td>Ticket Jira:  </td><td>");
		formAppendEnum(&s, "jira", "--|Has Jira|No Jira", sd.iJira);
		estrConcatf(&s, "</td></tr>\n");

		estrConcatf(&s, "<tr><td>Assigned To: </td><td>");
		formAppendEdit(&s, 20, "repid", sd.pRepAccountName ? sd.pRepAccountName : "");
		estrConcatf(&s, "</td></tr>\n");
		
		sprintf(ibuffer, "%d", sd.iOccurrences);
		estrConcatf(&s, "<tr><td>Minimum Occurrences: </td><td>");
		formAppendEdit(&s, 10, "occur", sd.iOccurrences ? ibuffer : "");
		estrConcatf(&s, "</td></tr>\n");

		estrConcatf(&s, "<tr><td>Visibility: </td><td>");
		formAppendEnum(&s, "vis", "--|Private|Public|Hidden", sd.iVisible);
		estrConcatf(&s, "</td></tr>\n");

		estrConcatf(&s, "<tr><td>Locale: </td><td>");
		formAppendEnumList(&s, "locale", wiLocaleNameList(), sd.iLocaleID-1, true);
		estrConcatf(&s, "</td></tr>\n");

		// End inner table
		estrConcatf(&s, "\n</table>\n");

		// Next outer column
		estrConcatf(&s, "\n</td><td valign=top>\n");
		// Start inner table
		estrConcatf(&s, "\n<table>\n"); // Column 1

		estrConcatf(&s, "<tr><td>Account Name:  </td><td>");
		formAppendEdit(&s, 15, "an", (sd.pAccountName) ? sd.pAccountName : "");
		estrConcatf(&s, "</td></tr>\n");
		
		estrConcatf(&s, "<tr><td>Character Name:  </td><td>");
		formAppendEdit(&s, 15, "char", (sd.pCharacterName) ? sd.pCharacterName : "");
		estrConcatf(&s, "</td></tr>\n");

		estrConcatf(&s, "<tr><td>Category:  </td><td>");
		formAppendEnumList(&s, "catg", *getCategoryStringsCombined(), 
			categoryConvertToCombinedIndex(sd.iMainCategory, sd.iCategory), true);
		estrConcatf(&s, "</td></tr>\n");

		estrConcatf(&s, "<tr><td>Group:  </td><td>");
		appendUserGroupList(&s, "grp", 0);
		estrConcatf(&s, "</td></tr>\n");

		estrConcatf(&s, "<tr><td>Summary and Description:  </td><td>");
		formAppendEdit(&s, 30, "sumdcp", (sd.pSummaryDescription) ? sd.pSummaryDescription : "");
		estrConcatf(&s, "</td></tr>\n");

		estrConcatf(&s, "<tr><td align=right>Search: </td><td>");
		formAppendEnum(&s, "sumdcpflag", "Summary and Description|Summary only|Description only", sd.iSearchSummaryDescription);
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td>Trivia Strings:  </td><td>");
		formAppendEdit(&s, 30, "trv", (sd.pTrivia) ? sd.pTrivia : "");
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td align=right>Search: </td><td>");
		formAppendEnum(&s, "trvcat", "Trivia Keys and Values|Trivia Keys|Trivia Values", sd.iSearchTriviaKeyValue);
		estrConcatf(&s, "</td></tr>");

		//estrConcatf(&s, "<tr><td>Extra Data Values: </td><td>");
		//formAppendEdit(&s, 15, "ex", (sd.pExtraSubstring) ? sd.pExtraSubstring : "");
		//estrConcatf(&s, "</td></tr>");/**/
		
		estrConcatf(&s, "<tr><td>Shard Info String: </td><td>");
		formAppendEdit(&s, 15, "shard", (sd.pShardInfoString) ? sd.pShardInfoString : "");
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td>Exact Shard Name: </td><td>");
		formAppendEdit(&s, 15, "shardname", (sd.pShardExactName) ? sd.pShardExactName : "");
		estrConcatf(&s, "</td></tr>");

		// End inner table
		estrConcatf(&s, "\n</table>\n");

		// Next outer column
		estrConcatf(&s, "\n</td><td valign=top>\n");
		// Start inner table
		estrConcatf(&s, "\n<table>\n"); // Column 2
		
		estrConcatf(&s, "<tr><td>Platform:  </td><td>");
		formAppendEnum(&s, "plat", "--|Win32|Xbox 360", (int)sd.ePlatform);
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td>Product Name: </td><td>");
		formAppendEnumList(&s, "pn", *productNamesGetEArray(), sd.iProductIndex - 1, true);
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td>Version: </td><td>");
		formAppendEdit(&s, 15, "ver", (sd.pVersion) ? sd.pVersion: "");
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td>IP: </td><td>");
		formAppendEdit(&s, 15, "ip", (sd.pIP) ? sd.pIP : "");
		estrConcatf(&s, "</td></tr>");
		
		estrConcatf(&s, "<tr><td>Max Count: </td><td>");
		formAppendEditInt(&s, 3, "limit", iSearchCountLimit);
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td></td></tr>\n"
			"<tr><td>Date Ranges (\"Year-Month-Day Hour:Minutes:Seconds\")</td><td>");
		estrConcatf(&s, "</td></tr>");

		timeMakeLocalDateStringFromSecondsSince2000(datetime, sd.uDateStart);
		estrConcatf(&s, "<tr><td>Start Date: </td><td>");
		formAppendEdit(&s, 15, "datestart", sd.uDateStart ? datetime : "");
		estrConcatf(&s, "</td></tr>");

		timeMakeLocalDateStringFromSecondsSince2000(datetime, sd.uDateEnd);
		estrConcatf(&s, "<tr><td>End Date: </td><td>");
		formAppendEdit(&s, 15, "dateend", sd.uDateEnd ? datetime : "");
		estrConcatf(&s, "</td></tr>");

		// End inner table
		estrConcatf(&s, "\n</table>\n");

		// The bottom string
		estrConcatf(&s, "\n</td></tr><tr><td colspan=4>\n");

		// The "Any" string
		estrConcatf(&s, "\n<br>Just search everything: ");
		formAppendEdit(&s, 45, "any", (sd.pAny) ? sd.pAny : "");

		// Autofocus the 'any' field
		estrConcatf(&s, 
			"<script language=\"Javascript\">\n"
			"<!--\n"
			"document.searchform.any.focus();\n"
			"//-->\n"
			"</script>\n");

		// End of the "Any" string
		//estrConcatf(&s, "\n</td><td><br>");
		formAppendSubmit(&s, "Search");

		// End outer column table
		estrConcatf(&s, "\n</td></tr></table>\n");

		formAppendEnd(&s);
		estrConcatf(&s, "</div>\n");

		formAppendStart(&s, "/detail", "GET", "gotoform", NULL);
		estrConcatf(&s, "... or go directly to ID: ");
		formAppendEdit(&s, 15, "id", "");
		formAppendSubmit(&s, "Go");
		formAppendEnd(&s);

		estrConcatf(&s, "<hr size=1>\n");
	}

	if(bSearch)
	{
		iSearchCount = 0;
		i = 0;
		p = searchFirst(&sd);

		if (iSearchCountLimit)
		{
			int iCount = eaSize(&sd.ppSortedEntries);
			estrConcatf(&s, "Showing Results %d-%d of %d<br>\n", 1+offset, min(offset+iSearchCountLimit, iCount), 
				iCount);
		}
		formAppendStart(&s, "/batchAction", "GET", "batch", NULL);
		estrConcatf(&s, "Search Results:<br>\n");

		// ------------------------------------------------------------------------------------------
		// TODO: Move into a function?
		estrConcatf(&s, "<table border=0 cellpadding=3 cellspacing=0 width=100%%><tr><td>");

		//estrConcatf(&s, "%d results returned</td></tr>\n<tr><td>\n", eaSize(&sd.ppSortedEntries));
		if (iSearchCountLimit > 0)
		{
			estrConcatf(&s, "<a href=\"/search?");
			APPEND_URL_ARGS(&s, (offset-iSearchCountLimit > 0)?(offset-iSearchCountLimit):0, sd.eSortOrder, sd.bSortDescending);
			estrConcatf(&s, "\">&lt;== Prev</a>");

			estrConcatf(&s, " | ");

			estrConcatf(&s, "<a href=\"/search?");
			APPEND_URL_ARGS(&s, offset+iSearchCountLimit, sd.eSortOrder, sd.bSortDescending);
			estrConcatf(&s, "\">Next ==&gt;</a>\n");

			estrConcatf(&s, " | ");
			
			estrConcatf(&s, "<a href=\"/csv?");
			APPEND_URL_ARGS(&s, 0, sd.eSortOrder, sd.bSortDescending);
			estrConcatf(&s, "\">Save Search Results to CSV</a>\n");
		}

		estrConcatf(&s, "</td><td style=\"text-align: right\">");
		formAppendSubmit(&s, "Batch Action on Selected");
		formAppendCheckBoxScripted(&s, "selectall", "onClick=\"checkUncheckAll(this)\"", false);
		estrConcatf(&s, "Select All&nbsp;</td></tr></table>\n");
		// ------------------------------------------------------------------------------------------

		estrConcatf(&s, "<table class=\"fullsummarytable\">");
		appendSearchTableHeader(&s, offset, iSearchCountLimit, &sd, "search?", true);

		while(p != NULL)
		{
			i++;
			if(offset >= i)
			{
				p = searchNext(&sd);
				continue;
			}

			if(iSearchCountLimit > 0)
				if(iSearchCount >= iSearchCountLimit)
					break;

			wiAppendSummaryTableEntry(p, &s, iSearchCount, i%2, true, &sd);

			iSearchCount++;

			p = searchNext(&sd);
		}
		searchEnd(&sd);

		estrConcatf(&s, "</table>\n");

		// ------------------------------------------------------------------------------------------
		// TODO: Move into a function?
		estrConcatf(&s, "<table border=0 cellpadding=3 cellspacing=0 width=100%%><tr><td>");

		//estrConcatf(&s, "%d results returned</td></tr>\n<tr><td>\n", eaSize(&sd.ppSortedEntries));
		if (iSearchCountLimit > 0)
		{
			estrConcatf(&s, "<a href=\"/search?");
			APPEND_URL_ARGS(&s, (offset-iSearchCountLimit > 0)?(offset-iSearchCountLimit):0, sd.eSortOrder, sd.bSortDescending);
			estrConcatf(&s, "\">&lt;== Prev</a>");

			estrConcatf(&s, " | ");

			estrConcatf(&s, "<a href=\"/search?");
			APPEND_URL_ARGS(&s, offset+iSearchCountLimit, sd.eSortOrder, sd.bSortDescending);
			estrConcatf(&s, "\">Next ==&gt;</a>\n");

			estrConcatf(&s, " | ");
			
			estrConcatf(&s, "<a href=\"/csv?");
			APPEND_URL_ARGS(&s, 0, sd.eSortOrder, sd.bSortDescending);
			estrConcatf(&s, "\">Save Search Results to CSV</a>\n");
		}

		estrConcatf(&s, "</td><td style=\"text-align: right\">");
		formAppendSubmit(&s, "Batch Action on Selected");
		formAppendCheckBoxScripted(&s, "selectall", "onClick=\"checkUncheckAll(this)\"", false);
		estrConcatf(&s, "Select All&nbsp;</td></tr></table>\n");
		// ------------------------------------------------------------------------------------------

		formAppendEnd(&s);
	}

	httpSendWrappedString(link, s, pCookies);
}

// ------------------------------------------------
// User Group Actions
void appendTicketsBelongingToUserGroup(char **estr, char **args, char **values, int count, TicketUserGroup *pGroup)
{
	SearchData sd = {0};
	TicketEntry *p;
	int i=0, iSearchCount = 0;
	TicketEntry **ppAssignedEntries = NULL;

	sd.uFlags = SEARCHFLAG_GROUP | SEARCHFLAG_SORT;
	sd.bSortDescending = false;
	sd.eSortOrder = SORTORDER_REPNAME;
	sd.uGroupID = pGroup->uID;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "sort"))
		{
			sd.uFlags |= SEARCHFLAG_SORT;
			sd.eSortOrder = (SortOrder)CLAMP(atoi(values[i]), 0, SORTORDER_MAX);
		}
		else if(!stricmp(args[i], "asc"))
		{
			sd.bSortDescending = !(atoi(values[i]) == 1);
		}
	}

	estrConcatf(estr, "<div class=\"heading\">Unassigned Tickets in this Group</div>\n");
	estrConcatf(estr, "<table class=\"fullsummarytable\">\n");
	appendSearchTableHeader(estr, 0, 0, &sd, STACK_SPRINTF("group?id=%d", pGroup->uID), false);
	p = searchFirst(&sd);
	while(p != NULL)
	{
		if (!p->pRepAccountName)
		{
			i++;
			wiAppendSummaryTableEntry(p, estr, iSearchCount, i%2, false, &sd);
		}
		else
			eaPush(&ppAssignedEntries, p);
		p = searchNext(&sd);
		iSearchCount++;
	}
	estrConcatf(estr, "</table>\n");

	
	estrConcatf(estr, "<div class=\"heading\">Assigned Tickets in this Group</div>\n");
	estrConcatf(estr, "<table class=\"fullsummarytable\">\n");
	appendSearchTableHeader(estr, 0, 0, &sd, STACK_SPRINTF("group?id=%d", pGroup->uID), false);
	for (i=0; i<eaSize(&ppAssignedEntries); i++)
	{
		wiAppendSummaryTableEntry(ppAssignedEntries[i], estr, i+1, (i+1)%2, false, &sd);
	}
	estrConcatf(estr, "</table>\n");
}

void wiChangeGroup(NetLink *link, char **args, char **values, int count, CookieList* pCookies)
{
	static char *s = NULL;
	int i;
	int iTicketID = 0, iGroupID = 0;
	TicketEntry *pEntry;
	TicketUserGroup *pGroup;

	/*if (!wiConfirmPermission(link, pCookies, "Tickets", ACTION_EDIT, true))
		return;*/
	estrCopy2(&s, ""); // Init the static estring

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iTicketID);
		}
		else if (!stricmp(args[i], "groupid"))
		{
			sscanf(values[i], "%d", &iGroupID);
		}
	}

	if(iTicketID == 0)
	{
		SendWrappedError(link, "Please supply an 'id' to change the group of. Thanks.", pCookies);
		return;
	}

	pEntry = findTicketEntryByID(iTicketID);
	pGroup = findTicketGroupByID(iGroupID);
	if(!pEntry)
	{
		SendWrappedError(link, "Detail not found for that ID.", pCookies);
		return;
	}
	if(!pGroup && iGroupID)
	{
		// iGroupID == 0 means group is being cleared, so ignore that
		SendWrappedError(link, "User Group not found for that ID.", pCookies);
		return;
	}

	pEntry->uGroupID = (U32) iGroupID;
	httpRedirect(link, STACK_SPRINTF("/detail?id=%d", iTicketID));
}

void appendUserGroupData(TicketUserGroup *pGroup, char **estr)
{
	estrConcatf(estr,
		"<tr>\n"
		"<td align=left class=\"summarytd\">");
	estrConcatf(estr, "<a href=\"/usergroup?id=%d\">%s</a>", pGroup->uID, pGroup->pName);
	estrConcatf(estr, "</td>\n");
}

void appendAllUserGroups(char **estr)
{
	formAppendStart(estr, "/createusergroup", "POST", "create", NULL);
	estrConcatf(estr, "<div class=\"heading\">Create a New Ticket Group</div>\n"
		"Group Name:&nbsp;\n");
	formAppendEdit(estr, 20, "name", "");
	formAppendSubmit(estr, "Create New Group");
	formAppendEnd(estr);

	estrConcatf(estr,
		"<table class=\"summarytable\" border=1 cellpadding=3 cellspacing=0>\n"
		"<tr>\n"
		"<td align=left class=\"summaryheadtd\">Group Name</td>\n"
		"</tr>\n");

	iterateOverTicketGroups(appendUserGroupData, estr);
	estrConcatf(estr, "</table>\n");
}

void appendTicketsBelongingToRep(char **estr, char **args, char **values, int count, char *pAccountName, bool bShowAllTickets)
{
	SearchData sd = {0};
	TicketEntry *p;
	int i=0, iSearchCount = 0;

	sd.uFlags = SEARCHFLAG_ASSIGNED | SEARCHFLAG_SORT;
	sd.bSortDescending = false;
	sd.eSortOrder = SORTORDER_NEWEST;
	sd.pRepAccountName = pAccountName;

	if (!bShowAllTickets)
	{
		sd.eStatus = TICKETSTATUS_OPEN; // TODO
		sd.uFlags |= SEARCHFLAG_STATUS;
	}

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "sort"))
		{
			sd.uFlags |= SEARCHFLAG_SORT;
			sd.eSortOrder = (SortOrder)CLAMP(atoi(values[i]), 0, SORTORDER_MAX);
		}
		else if(!stricmp(args[i], "asc"))
		{
			sd.bSortDescending = !(atoi(values[i]) == 1);
		}
	}

	estrConcatf(estr, "<div class=\"heading\">Name</div>\n%s", pAccountName);
	estrConcatf(estr, "<div class=\"heading\">Tickets Assigned to this User</div>\n");

	estrConcatf(estr, "<table cellpadding=3 border=0 cellspacing=0 width=100%%><tr><td>\n");
	formAppendStart(estr, "/user", "GET", "user", NULL);
	formAppendHidden(estr, "id", pAccountName);
	formAppendHiddenInt(estr, "showall", bShowAllTickets ? 0 : 1);
	formAppendSubmit(estr, bShowAllTickets ? "Hide Closed Tickets" : "Show All Tickets");
	formAppendEnd(estr);
	estrConcatf(estr, "&nbsp;%s</td>\n", bShowAllTickets ? "Showing all tickets" : "Showing Open or In Progress tickets only");

	estrConcatf(estr, "<td>\n");
	formAppendStart(estr, "/batchAction", "GET", "batch", NULL);
	estrConcatf(estr, "<div style=\"text-align: right\" width=100%%>");
	formAppendSubmit(estr, "Batch Action on Selected");
	formAppendCheckBoxScripted(estr, "selectall", "onClick=\"checkUncheckAll(this)\"", false);
	estrConcatf(estr, "Select All&nbsp;</div>\n");
	estrConcatf(estr, "</td>\n");

	estrConcatf(estr, "</tr></table>\n");

	estrConcatf(estr, "<table class=\"fullsummarytable\">\n");
	appendSearchTableHeader(estr, 0, 0, &sd, STACK_SPRINTF("user?id=%s", pAccountName), false);
	p = searchFirst(&sd);
	while(p != NULL)
	{
		i++;
		wiAppendSummaryTableEntry(p, estr, iSearchCount, i%2, true, &sd);
		iSearchCount++;
		p = searchNext(&sd);
	}
	estrConcatf(estr, "</table>\n");
	formAppendEnd(estr);
}

void wiViewUserGroup(NetLink *link, char **args, char **values, int count, CookieList* pCookies)
{
	static char *s = NULL;
	int i;
	int iUniqueID = 0;

	estrCopy2(&s, ""); // Init the static estring

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
			break;
		}
	}

	if (!iUniqueID)
	{
		appendAllUserGroups(&s);
	}
	else
	{
		TicketUserGroup *pGroup = findTicketGroupByID(iUniqueID);
				
		if (pGroup)
		{
			estrConcatf(&s, "<div class=\"heading\">Group Name</div>\n"
				"%s", pGroup->pName);

			appendTicketsBelongingToUserGroup(&s, args, values, count, pGroup);
		}
		else
		{
			SendWrappedError(link, STACK_SPRINTF("No user group found under ID #%d.", iUniqueID), pCookies);
			return;
		}
	}

	httpSendWrappedString(link, s, pCookies);
}

void wiCreateUserGroup(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	const char *pTempName = urlFindValue(args, "name");
	if (getLoginAccessLevel(pCookies) != ALVL_ADMIN)
	{
		SendWrappedError(link, "You do not have the required permissions for the requested action.", pCookies);
		return;
	}

	if (!pTempName || strlen(pTempName) == 0)
	{
		SendWrappedError(link, "A name for the group is required.", pCookies);
		return;
	}
	else
	{
		char *pName = NULL; 
		char *pErrors = NULL;

		estrCopy2(&pName, pTempName); 
		estrTrimLeadingAndTrailingWhitespace(&pName);
		if (findTicketGroupByName(pName))
		{
			SendWrappedError(link, STACK_SPRINTF("A group already exists with the name '%s'.", pName), pCookies);
			return;
		}
		if (!createTicketGroup(pName))
		{
			SendWrappedError(link, pErrors, pCookies);
			return;
		}
	}
	httpRedirect(link, "/usergroup");
}

// ---------------------------------
// Users

void wiViewUser(NetLink *link, char **args, char **values, int count, CookieList* pCookies)
{
	static char *s = NULL;
	int i;
	char *pAccountName = NULL;
	bool bShowAllTickets = false;

	estrCopy2(&s, ""); // Init the static estring

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			pAccountName = values[i];
		}
		else if (!stricmp(args[i], "showall"))
		{
			if (atoi(values[i]) == 1)
			{
				bShowAllTickets = true;
			}
		}
	}

	formAppendStart(&s, "/user", "GET", "user", NULL);
	estrConcatf(&s, "Account Name: ");
	formAppendEdit(&s, 20, "id", "");
	formAppendSubmit(&s, "View User");
	formAppendEnd(&s);
	if (pAccountName)
	{
		estrConcatf(&s, "<div class=\"heading\">Account Name</div>\n%s\n", pAccountName);
		appendTicketsBelongingToRep(&s, args, values, count, pAccountName, bShowAllTickets);
	}

	httpSendWrappedString(link, s, pCookies);
}

// ------------------------------------------------
// Ticketing Actions

void wiChangeStatus(NetLink *link, char **args, char **values, int count, CookieList* pCookies)
{
	static char *s = NULL;
	int i;
	int iUniqueID = 0;
	TicketStatus eStatus = TICKETSTATUS_UNKNOWN;
	TicketEntry *pEntry;
		
	estrCopy2(&s, ""); // Init the static estring

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iUniqueID);
		}
		else if (!stricmp(args[i], "status"))
		{
			sscanf(values[i], "%d", (int*) &eStatus);
			eStatus++; // Since form dropdown starts with 0
		}
	}

	if(iUniqueID == 0)
	{
		SendWrappedError(link, "Please supply an 'id' to change the status of. Thanks.", pCookies);
		return;
	}
	if(eStatus == TICKETSTATUS_UNKNOWN || eStatus >= TICKETSTATUS_COUNT)
	{
		SendWrappedError(link, "Please supply a status to change the Ticket to. Thanks.", pCookies);
		return;
	}

	pEntry = findTicketEntryByID(iUniqueID);

	if(!pEntry)
	{
		SendWrappedError(link, "Detail not found for that ID.", pCookies);
		return;
	}
	if (ticketEntryChangeStatus(NULL, pEntry, eStatus)) 
	{
		SendWrappedError(link, "Invalid status specified.", pCookies);
		return;
	}
	httpRedirect(link, STACK_SPRINTF("/detail?id=%d", iUniqueID));
}

void wiCreateTicket(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	TicketData *pTicketData = StructCreate(parse_TicketData);
	TicketEntry *pEntry = NULL;
	TicketUserGroup *pGroup = NULL;

	const char *pCategoryIndex = urlFindValue(args, "cat");
	const char *pSummary = urlFindValue(args, "sum");
	const char *pDescription = urlFindValue(args, "dcp");
	const char *pComment = urlFindValue(args, "cmt");
	const char *pIntString = urlFindValue(args, "plat");
	const char *pCharacterName = urlFindValue(args, "cname");
	const char *pAccountName = urlFindValue(args, "aname");

	int iCategoryCombinedIndex = atoi(pCategoryIndex) - 1;
	int iMainCategoryIdx, iSubCategoryIdx;

	categoryConvertFromCombinedIndex(iCategoryCombinedIndex, &iMainCategoryIdx, &iSubCategoryIdx);
	if (iMainCategoryIdx < 0 || iSubCategoryIdx < 0)
		return;
	estrCopy2(&s, "");

	pTicketData->pMainCategory = estrDup(categoryGetKey(iMainCategoryIdx));
	pTicketData->pCategory = estrDup(subcategoryGetKey(iMainCategoryIdx, iSubCategoryIdx));
	pTicketData->pSummary = strdup(pSummary);
	pTicketData->pUserDescription = strdup(pDescription);
	pTicketData->pAccountName = strdup(pAccountName);
	pTicketData->pCharacterName = pCharacterName ? strdup(pCharacterName) : strdup("");

	if (pIntString)
	{
		Platform ePlatform = atoi(pIntString);
		pTicketData->pPlatformName = strdup(getPlatformName(ePlatform));
	}
	pIntString = urlFindValue(args, "prod");
	if (pIntString)
	{
		int val = atoi(pIntString);
		switch (val)
		{
		case 1:
			pTicketData->pProductName = strdup("FightClub");
			break;
		case 2:
			pTicketData->pProductName = strdup("PrimalAge");
			break;
		default:
			pTicketData->pProductName = strdup("");
		}
	}

	createTicket(pTicketData, 0);
	estrConcatf(&s, "New Ticket created.");
	httpSendWrappedString(link, s, pCookies);
}

void wiNewTicket(NetLink *link, char **args, char **values, int count, CookieList* pCookies)
{
	static char *s = NULL;

	estrCopy2(&s, "");
	estrConcatf(&s, "<div class=\"title\">Create New Ticket</div>\n"
		"Automatic ticket assignment rules <i>will</i> be applied to tickets created through this form!<br>\n");
	formAppendStart(&s, "/cticket", "POST", "ticket", NULL);

	estrConcatf(&s, "<div class=\"heading\">Platform:</div>\n");
	formAppendEnum(&s, "plat", "--|Win32|Xbox 360", 0);

	estrConcatf(&s, "<div class=\"heading\">Product Name:</div>\n");
	formAppendEnum(&s, "prod", "--|FightClub|PrimalAge", 0);

	estrConcatf(&s, "<div class=\"heading\">Group:</div>\n");
	appendUserGroupList(&s, "grp", 0);

	estrConcatf(&s, "<div class=\"heading\">Account Name:</div>\n");
	formAppendEdit(&s, 20, "aname", "");

	estrConcatf(&s, "<div class=\"heading\">Character Name:</div>\n");
	formAppendEdit(&s, 20, "cname", "");

	estrConcatf(&s, "<div class=\"heading\">Category:</div>\n");
	formAppendEnumList(&s, "cat", *getCategoryStringsCombined(), 0, true);
	//formAppendEdit(&s, 50, "cat", "");

	estrConcatf(&s, "<div class=\"heading\">Summary:</div>\n");
	formAppendEdit(&s, 50, "sum", "");

	estrConcatf(&s, "<div class=\"heading\">Description:</div>\n");
	estrConcatf(&s, "<div style=\"font-family: Times, Arial, serif, sans-serif; font-size: large\">\n");
	formAppendTextarea(&s, 8, 70, "dcp", "");
	estrConcatf(&s, "</div>\n");
	
	estrConcatf(&s, "<div class=\"heading\">Comment:</div>\n");
	estrConcatf(&s, "<div style=\"font-family: Times, Arial, serif, sans-serif; font-size: large\">\n");
	formAppendTextarea(&s, 8, 70, "cmt", "");
	estrConcatf(&s, "</div>\n");

	estrConcatf(&s, "<br>");
	formAppendSubmit(&s, "Create Ticket");
	formAppendEnd(&s);

	httpSendWrappedString(link, s, pCookies);
}

void wiAssignTicket(NetLink *link, char **args, char ** values, int count, CookieList *pCookies)
{
	int iTicketID = 0;
	char *pRepAccountName = NULL;
	TicketEntry *pEntry = NULL;
	int i;

	for(i=0;i<count;i++)
	{
		if(!stricmp(args[i], "id"))
		{
			sscanf(values[i], "%d", &iTicketID);
		}
		else if (!stricmp(args[i], "repid"))
		{
			pRepAccountName = values[i];
		}
	}

	pEntry = findTicketEntryByID(iTicketID);

	if (!pEntry)
	{
		SendWrappedError(link, "Please supply a Ticket 'id' to assign.", pCookies);
		return;
	}
	if (!pRepAccountName) // non-zero ID and no rep found
	{
		SendWrappedError(link, "Please supply a Customer Service Rep name to assign to.", pCookies);
		return;
	}
	ticketAssignToAccountName(pEntry, pRepAccountName);
	httpRedirect(link, STACK_SPRINTF("/detail?id=%d", pEntry->uID));
}

static void appendUserGroupOptionString(TicketUserGroup *pGroup, char **estr)
{
	estrConcatf(estr, "<option value=%d>%s</option>\n", pGroup->uID, pGroup->pName);
}

static void wiBatchActionForm(NetLink *link, char **args, char **values, int count, CookieList *pCookies)
{
	static char *s = NULL;
	int i, iTicketCount = 0;
	bool bAdminAccess = true;
	bool bGroupAccess = true; // User has group admin access to all tickets?
	U32 uGroupID = 0;
	bool bUserAccess = true;
	bool bUnowned = true;
	SearchData sd = {0};
	
	estrCopy2(&s, "");
	formAppendStart(&s, "/batchActionRun", "POST", "ladeda", NULL);

	estrConcatf(&s, "<div class=\"heading\">Batch Action on:</div>\n"
		"<table class=\"fullsummarytable\">\n");
	appendSearchTableHeader(&s, 0, 0, NULL, "", false);
	for (i=0; i<count; i++)
	{
		int id = 0;
		sscanf(args[i], "check%d", &id);
		if (id)
		{
			TicketEntry *pEntry = findTicketEntryByID(id);
			if (pEntry)
			{
				//eaiPush(&iIDs, id);
				formAppendHiddenInt(&s, STACK_SPRINTF("ticket%d", iTicketCount), id);
				if (!bAdminAccess)
				{
					// save time, skip this if user is a full admin
					if (!uGroupID)
					{
						uGroupID = pEntry->uGroupID;
					}
				}
				wiAppendSummaryTableEntry(pEntry, &s, 0, ++iTicketCount % 2, false, NULL);
			}
		}
	}
	formAppendHiddenInt(&s, "count", iTicketCount);
	estrConcatf(&s, "</table>\n");

	/*{
		estrConcatf(&s, "<div class=\"heading\">Change Group Assignments</div>\n"
			"<select class=\"formdata\" name=\"group\">\n"
			"<option SELECTED value=\"-1\">Do not change</option>\n"
			"<option value=0>Remove assignments</option>\n");
		iterateOverTicketGroups(appendUserGroupOptionString, &s);
		estrConcatf(&s, "</select>\n");
	}*/
	{
		estrConcatf(&s, "<div class=\"heading\">Change Public Visibility</div>\n"
			"<select class=\"formdata\" name=\"visible\">\n"
			"<option SELECTED value=\"-1\">Do not change</option>\n"
			"<option value=0>Private</option>\n"
			"<option value=1>Public</option>\n"
			"<option value=2>Hidden</option>\n"
			"</select>\n");

		estrConcatf(&s, "<div class=\"heading\">Change Statuses</div>\n"
			"<select class=\"formdata\" name=\"status\">\n"
			"<option SELECTED value=\"-1\">Do not change</option>\n"
			"<option value=%d>%s</option>\n<option value=%d>%s</option>\n"
			"<option value=%d>%s</option>\n<option value=%d>%s</option>\n"
			"<option value=%d>%s</option>\n<option value=%d>%s</option>\n"
			"</select>\n", 
			TICKETSTATUS_OPEN, "Open", TICKETSTATUS_CLOSED, "Closed", 
			TICKETSTATUS_RESOLVED, "Resolved", TICKETSTATUS_IN_PROGRESS, "In Progress", 
			TICKETSTATUS_PENDING, "Pending", TICKETSTATUS_PROCESSED, "Processed");

		estrConcatf(&s, "<div class=\"heading\">Batch Reply to Tickets</div>\n");
		estrConcatf(&s, "<div style=\"font-family: Times, Arial, serif, sans-serif; font-size: large\">\n");
		formAppendTextarea(&s, 5, 50, "response", "");
		estrConcatf(&s, "</div>");
	}
	formAppendNamedSubmit(&s, "batchaction", "Submit Changes");

	{
		estrConcatf(&s, "<div><div class=\"heading\">Batch Create Jiras</div>\n");
		formAppendJiraProjects(&s, "project");
		formAppendNamedSubmit(&s, "batchjira", "Create Jira Issues");
		estrConcatf(&s, "</div>\n");
	}

	// TODO actions = comment, reply to client, change status, other stuff - reassign/take possesion of?
	// Restrictions: Full Admin, Group Admin + all tickets in group, User + all tickets owned
	formAppendEnd(&s);

	httpSendWrappedString(link, s, pCookies);
}

static void ticketsBatchAction(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	const char *pResponse = urlFindValue(args, "response");
	const char *pStatus = urlFindValue(args, "status");
	const char *pCount = urlFindValue(args, "count");
	const char *pVisible = urlFindValue(args, "visible");
	int *aiTicketIDs = NULL;

	int iTicketCount = atoi(pCount);
	int iStatus = pStatus ? atoi(pStatus) : -1;
	int iVisible = pVisible ? atoi(pVisible) : -1;
	int i;
	
	estrCopy2(&s, "");
	estrConcatf(&s, "<div class=\"heading\">Batch Action Run on:</div>\n"
		"Tickets showing updated values\n"
		"<table class=\"fullsummarytable\">\n");
	appendSearchTableHeader(&s, 0, 0, NULL, "", false);
	for (i=0; i<iTicketCount; i++)
	{
		const char *pTicketID = urlFindSafeValue(args, STACK_SPRINTF("ticket%d", i));
		int id = atoi(pTicketID);
		TicketEntry *pEntry = findTicketEntryByID(id);
		if (pEntry)
		{
			eaiPush(&aiTicketIDs, id);
			if (iVisible != -1)
				pEntry->eVisibility = iVisible;
			if (pResponse && pResponse[0])
			{
				ticketChangeCSRResponse(pEntry, NULL, pResponse); // TODO
			}
			if (iStatus != -1)
			{
				ticketEntryChangeStatus(NULL, pEntry, iStatus);
			}
			wiAppendSummaryTableEntry(pEntry, &s, 0, i+1 % 2, false, NULL);
		}
	}
	estrConcatf(&s, "</table>\n");

	{
		estrConcatf(&s, "<div class=\"heading\">Values Changed</div>\n");
		estrConcatf(&s, "Visibility: %s<br>\n"
			"Status: %s<br>\n"
			"Response: %s<br>\n",
			iVisible == -1 ? "Not changed" : TranslateMessageKey (getVisibilityString(iVisible)), 
			iStatus != -1 ? TranslateMessageKey(getStatusString(iStatus)) : "Not changed",
			pResponse && pResponse[0] ? pResponse : "No response sent");

		httpSendWrappedString(link, s, pCookies);
	}
}

static void ticketsBatchJira(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	const char *pCount = urlFindValue(args, "count");
	const char *pProject = urlFindValue(args, "project");
	JiraComponentList *pComponentsList;
	JiraProject *pJiraProject;
	int *aiTicketIDs = NULL;

	int iTicketCount = 0;
	int count = atoi(pCount);
	int i;
	TicketTrackerUser *pUser = wiConfirmLogin(pCookies);
	
	loadJiraData();
	pJiraProject = findJiraProjectByKey(pProject);
	if (!pJiraProject)
	{
		httpSendClientError(link, "Could not find the project");
		return;
	}

	if (!pUser)
	{
		httpRedirect(link, "/login");
		return;
	}

	pComponentsList = StructCreate(parse_JiraComponentList);
	jiraGetComponents(pJiraProject, pComponentsList);

	estrCopy2(&s, "");
	
	formAppendStart(&s, "/batchJiraCreate", "POST", "batchjiraissues", NULL);
	estrConcatf(&s, "<div class=\"heading\">Batch Create Jiras Run on:</div>\n"
		"Tickets with existing Jiras were removed\n"
		"<table class=\"fullsummarytable\">\n");
	appendSearchTableHeader(&s, 0, 0, NULL, "", false);
	for (i=0; i<count; i++)
	{
		const char *pTicketID = urlFindSafeValue(args, STACK_SPRINTF("ticket%d", i));
		int id = atoi(pTicketID);
		TicketEntry *pEntry = findTicketEntryByID(id);
		if (pEntry && !pEntry->pJiraIssue)
		{
			formAppendHiddenInt(&s, STACK_SPRINTF("ticket%d", iTicketCount), id);
			wiAppendSummaryTableEntry(pEntry, &s, 0, ++iTicketCount % 2, false, NULL);
		}
	}
	estrConcatf(&s, "</table>\n");
	formAppendHiddenInt(&s, "count", iTicketCount);

	formAppendHidden(&s, "project", pProject);
	estrConcatf(&s, "<div class=\"heading\">Project Name:</div>\n%s", pProject);

	estrConcatf(&s, "<div class=\"heading\">Assign Issue To:</div>");
	formAppendJiraUsers(&s, "assignee");
	
	estrConcatf(&s, "<div class=\"heading\">Component:</div>");
	formAppendJiraComponents(&s, "component", pComponentsList);

	estrConcatf(&s, "<div class=\"heading\">Label:</div>");
	formAppendEdit(&s, 40, "label", "");

	estrConcatf(&s, 
		"<div class=\"heading\">Global Appended Description:</div>\n"
		"This information will be concatenated to description for all of the Jiras.<br>\n");
	estrConcatf(&s, "<div style=\"font-family: Times, Arial, serif, sans-serif; font-size: large\">\n");
	formAppendTextarea(&s, 20, 80, "description", "");
	estrConcatf(&s, "</div>\n");

	estrConcatf(&s, "<br>");
	formAppendSubmit(&s, "Create Jira Issues");
	formAppendEnd(&s);

	httpSendWrappedString(link, s, pCookies);
}

static void wiBatchActionRun(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	if (urlFindValue(args, "batchaction"))
	{
		ticketsBatchAction(link, args, pReferer, pCookies);
	}
	else if (urlFindValue(args, "batchjira"))
	{
		ticketsBatchJira(link, args, pReferer, pCookies);
	}
	else
	{
		httpSendWrappedString(link, "Error: invalid batch action", pCookies);
	}
}

static bool generateJiraIssue(TicketEntry *pEntry, const char *pProject, const char *pAssignee, 
							  const char* pComponent, const char *pLabel, 
							  const char *pUserSummary, const char* pUserDescription);
static void wiBatchJiraCreate(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	const char *pAssignee = urlFindValue(args, "assignee");
	const char *pProject = urlFindValue(args, "project");
	const char *pComponent = urlFindValue(args, "component");
	const char *pGlobalDescription = urlFindValue(args, "description");
	const char *pLabel = urlFindValue(args, "label");
	const char *tempValue = NULL;
	int iTicketCount = 0, iSuccessCount = 0;
	int i = 0, count = 0;
	bool loggedIn = false;
	TicketTrackerUser *loginUser = wiConfirmLogin(pCookies);

	// TODO use errortracker/errortracker for now
	/*if (!wiConfirmJiraLogin(link, pCookies))
		return;*/
	estrCopy2(&s, "");

	tempValue = urlFindValue(args, "count");
	if (tempValue)
		count = atoi(tempValue);	

	if (!loginUser)
	{
		httpRedirect(link, "/login");
		return;
	}

	loggedIn = jiraLogin(jiraGetAddress(), jiraGetPort(), loginUser->username, loginUser->password);
	if (loggedIn)
	{
		char *summary = NULL;
		jiraLogout();
		estrConcatf(&s, "<div class=\"heading\">Batch Jiras Created:</div>\n"
			"<table class=\"fullsummarytable\">\n");
		appendSearchTableHeader(&s, 0, 0, NULL, "", false);
		for (i=0; i<count; i++)
		{
			const char *pTicketID = urlFindSafeValue(args, STACK_SPRINTF("ticket%d", i));
			int id = atoi(pTicketID);
			TicketEntry *pEntry = findTicketEntryByID(id);
			if (pEntry && !pEntry->pJiraIssue)
			{
				bool bSuccess;
				jiraLogin(jiraGetAddress(), jiraGetPort(), loginUser->username, loginUser->password);
				
				estrCopy2(&summary, "[Ticket] ");
				if (pEntry->gameLocation.zoneName)
					estrConcatf(&summary, "%s - ", pEntry->gameLocation.zoneName);
				estrConcatf(&summary, "%s", pEntry->pSummary);

				bSuccess = generateJiraIssue(pEntry, pProject, pAssignee, pComponent, pLabel, summary, pGlobalDescription);
				wiAppendSummaryTableEntry(pEntry, &s, 0, ++iTicketCount % 2, false, NULL);
				jiraLogout();
				if (bSuccess)
					iSuccessCount++;
			}
		}
		estrDestroy(&summary);
		estrConcatf(&s, "</table>\n"
			"%d issues successfully created.\n"
			"%d issues failed.\n", iSuccessCount, iTicketCount - iSuccessCount);
		jiraLogout();
	}
	else
	{
		estrPrintf(&s, "Could not log in to Jira.");
	}

	httpSendWrappedString(link, s, pCookies);
}

// ------------------------------------------------
// Ticket Assignment
static void wiViewAllRules(NetLink *link, char **args, char ** values, int count, CookieList *pCookies)
{
	static char *s = NULL;
	int i=0;
	AssignmentRulesList *pRulesList;

	estrCopy2(&s, "");
	estrConcatf(&s, "<div class=\"heading\">Global Ticket Assignment Rules</div>\n");
	estrConcatf(&s, "<div>Lower rules take precedence</div>\n");

	formAppendStart(&s, "/deleteRules", "POST", "deleterules", NULL);
	estrConcatf(&s, "<table class=\"fullsummarytable\">");
	estrConcatf(&s, "<tr>\n"
		"<td class=\"summaryheadtd\">Name</td>\n"
		"<td class=\"summaryheadtd\">Product Filter</td>\n"
		"<td class=\"summaryheadtd\">Main Category Filter</td>\n"
		"<td class=\"summaryheadtd\">Category Filter</td>\n"
		"<td class=\"summaryheadtd\">Keyword Filter</td>\n"
		"<td class=\"summaryheadtd\">Actions</td>\n"
		"<td class=\"summaryheadtd\">Reorder</td>\n"
		"<td class=\"summaryheadtd\">Delete</td>\n"
		"</tr>\n");

	pRulesList = getGlobalRules();
	if (pRulesList->ppRules)
	{
		for (i=0; i<eaSize(&pRulesList->ppRules); i++)
		{
			static char *pTempString = NULL;
			AssignmentRule *pRule = pRulesList->ppRules[i];
			char *prefix = (i % 2) ? "even": "odd";
			
			estrConcatf(&s, "<tr>\n"
				"<td class=\"summary%std\"><a href=\"/editRule?id=%d\">%s</a></td>\n"
				"<td class=\"summary%std\">%s</td>\n",
				prefix, pRule->uID, pRule->pName, 
				prefix, pRule->pProductName && pRule->pProductName[0] ? pRule->pProductName : "--");

			estrConcatf(&s, "<td class=\"summary%std\">%s</td>\n", prefix, 
				pRule->pMainCategoryFilter ? categoryGetTranslation(pRule->pMainCategoryFilter) : "--");

			estrConcatf(&s, "<td class=\"summary%std\">%s</td>\n", prefix, 
				pRule->pCategoryFilter ? categoryGetTranslation(pRule->pCategoryFilter) : "--");

			estrConcatf(&s, "<td class=\"summary%std\">%s</td>\n", prefix, pRule->pKeywordFilter ? pRule->pKeywordFilter : "--");

			estrCopy2(&pTempString, "");
			writeActionsToString(&pTempString, pRule->ppActions);
			estrConcatf(&s, "<td class=\"summary%std\">%s</td>\n", prefix, pTempString[0] ? pTempString : "--");

			estrConcatf(&s, "<td class=\"summary%std\">\n", prefix);
			estrConcatf(&s, "<a href=\"/moveRule?id=%d&dir=up\">up</a>&nbsp;<a href=\"moveRule?id=%d&dir=down\">dn</a>\n",
				pRule->uID, pRule->uID);
			estrConcatf(&s, "</td>\n");
			estrConcatf(&s, "<td class=\"summary%std\">\n", prefix);
			formAppendHiddenInt(&s, STACK_SPRINTF("id%d", i), pRule->uID);
			formAppendCheckBox(&s, STACK_SPRINTF("delete%d", i), false); 
			estrConcatf(&s, "</td>\n</tr>\n");
		}
	}
	formAppendHiddenInt(&s, "count", i);
	estrConcatf(&s, "</table>\n");
	formAppendSubmit(&s, "Delete Selected Rules");
	formAppendEnd(&s);
	formAppendStart(&s, "/editRule", "GET", "editrule", NULL);
	formAppendSubmit(&s, "Add New Rule");
	formAppendEnd(&s);

	httpSendWrappedString(link, s, pCookies);
}

static void wiDeleteRules(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	const char *pParseIntString = urlFindValue(args, "count");

	if (pParseIntString)
	{
		int count = atoi(pParseIntString);
		if (count)
		{
			int i;
			for (i=0; i<count; i++)
			{
				const char *pIdString = urlFindValue(args, STACK_SPRINTF("id%d", i));
				const char *pDelete = urlFindValue(args, STACK_SPRINTF("delete%d", i));

				if (pDelete)
				{
					int id = atoi(pIdString);
					if (id)
						deleteAssignmentRule(id);
				}
			}
		}
	}
	httpRedirect(link, "/rules");
}

static void wiMoveAssignmentRule(NetLink *link, char **args, char ** values, int count, CookieList *pCookies)
{
	static char *s = NULL;
	int i;
	U32 uID = 0;
	char * dir = NULL;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "id") == 0)
		{
			uID = atoi(values[i]);
		}
		else if (stricmp(args[i], "dir") == 0)
		{
			dir = values[i];
		}
	}

	if (uID && dir)
	{
		AssignmentRule *pRule = findAssignmentRuleByID(uID);
		if (pRule)
		{
			int index = eaFind(&(getGlobalRules()->ppRules), pRule);
			if (index)
			{
				if (index > 0 && stricmp(dir, "up") == 0)
				{
					eaSwap(&(getGlobalRules()->ppRules), index, index-1);
				}
				else if (index < eaSize(&(getGlobalRules()->ppRules)) - 1 && stricmp(dir, "down") == 0)
				{
					eaSwap(&(getGlobalRules()->ppRules), index, index+1);
				}
				saveAssignmentRules();
			}
		}
	}
	httpRedirect(link, "/rules");
}

static void wiApplyEditRule(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	const char *pIntParseString = urlFindValue(args, "id");
	const char *pName = urlFindValue(args, "name");
	const char *pProductName = urlFindValue(args, "product");
	const char *pCategoryFilters = urlFindValue(args, "cat");
	const char *pGroupActionString = urlFindValue(args, "group");
	const char *pKeywordFilter = urlFindValue(args, "keyword");
	char *pActionsString = NULL;

	int retval;
	U32 uID = pIntParseString ? atoi(pIntParseString) : 0;
	int iCombinedCategory = atoi(pCategoryFilters) - 1;
	int iMainCategory, iCategory;
	int iProductIndex = pProductName ? atoi(pProductName) : 0;

	categoryConvertFromCombinedIndex(iCombinedCategory, &iMainCategory, &iCategory);
	if (pGroupActionString && pGroupActionString[0])
	{
		U32 uGroupID;
		TicketUserGroup *pGroup;
		ANALYSIS_ASSUME(pGroupActionString != NULL);
		uGroupID = atoi(pGroupActionString);
		pGroup = findTicketGroupByID(uGroupID);
		if (pGroup)
		{
			estrConcatf(&pActionsString, ";group:%s", pGroup->pName);
		}
	}

	retval = editAssignmentRule(uID, pName, iMainCategory > -1 ? categoryGetKey(iMainCategory) : NULL, 
		iMainCategory > -1 && iCategory > -1 ? subcategoryGetKey(iMainCategory, iCategory) : NULL, 
		iProductIndex ? productNameGetString(iProductIndex-1) : "", pKeywordFilter, pActionsString);

	httpRedirect(link, "/rules");
}

static void wiEditAssignmentRule(NetLink *link, char **args, char ** values, int count, CookieList *pCookies)
{
	static char *s = NULL;
	static char *pActionsString = NULL;
	U32 uRuleID = 0;
	int i;
	AssignmentRule *pRule = NULL;

	estrCopy2(&s, "");
	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "id") == 0)
			uRuleID = atoi(values[i]);
	}

	if (uRuleID)
		pRule = findAssignmentRuleByID(uRuleID);
	estrCopy2(&pActionsString, "");
	if (pRule)
	{
		writeActionsToString(&pActionsString, pRule->ppActions);
	}

	if (pRule)
		estrConcatf(&s, "<div class=\"heading\">Edit Rule #%d</div>", pRule->uID);
	else
		estrConcatf(&s, "<div class=\"heading\">Add New Rule</div>");

	formAppendStart(&s, "/editRule", "POST", "editrule", NULL);
	formAppendHiddenInt(&s, "id", pRule ? pRule->uID : 0);
	estrConcatf(&s, "<table border=0>\n");
	
	estrConcatf(&s, "<tr><td>Rule Name: </td>\n<td>\n");
	formAppendEdit(&s, 20, "name", pRule ? pRule->pName : "");
	estrConcatf(&s, "</td></tr>\n");

	estrConcatf(&s, "<tr><td>Product Name: </td>\n<td>\n");
	formAppendEnumList(&s, "product", *productNamesGetEArray(), 
		pRule && pRule->pProductName ? productNameGetIndex(pRule->pProductName) : -1, true);
	estrConcatf(&s, "</td></tr>\n");

	{
		int iMainIndex = pRule ? categoryGetIndex(pRule->pMainCategoryFilter) : -1;
		int iSubIndex = pRule ? categoryGetIndex(pRule->pCategoryFilter) : -1;

		estrConcatf(&s, "<tr><td>Category Filter: </td>\n<td>\n");
		formAppendEnumList(&s, "cat", *getCategoryStringsCombined(), 
			categoryConvertToCombinedIndex(iMainIndex, iSubIndex), true);
		estrConcatf(&s, "</td></tr>\n");
	}

	estrConcatf(&s, "<tr><td>Summary and Description Filter: </td>\n<td>\n");
	formAppendEdit(&s, 30, "keyword", pRule && pRule->pKeywordFilter ? pRule->pKeywordFilter : "");
	estrConcatf(&s, "</td></tr>\n");

	estrConcatf(&s, "<tr><td colspan=2>Actions: </td>\n</tr>\n");
	estrConcatf(&s, "</td></tr>\n<tr><td>Assign to Group:</td><td>");
	appendUserGroupList(&s, "group", pRule ? getActionTarget(pRule, ASSIGN_GROUP) : 0);
	estrConcatf(&s, "</td></tr>\n");

	estrConcatf(&s, "</table>\n");
	
	formAppendSubmit(&s, pRule ? "Edit Rule" : "Add Rule");
	formAppendEnd(&s);

	httpSendWrappedString(link, s, pCookies);
}

// ------------------------------------------------
// Jira
static void wiAppendJiraDetailing(NetLink *link, char **args, char ** values, int count, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	static char *pDetailLink = NULL;
	int i, iUniqueID = 0;
	const char *pProject = NULL;
	const char *pUserDescription = NULL;
	TicketTrackerUser *pUser = wiConfirmLogin(pCookies); 
	char *summary = NULL;

	TicketEntry *pEntry;
	JiraComponentList *pComponentsList;
	JiraProject *pJiraProject;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "id") == 0)
		{
			iUniqueID = atoi(values[i]);
		}
		else if (stricmp(args[i], "project") == 0)
		{
			pProject = values[i];
		}
	}

	if (!iUniqueID || !pProject)
	{
		httpSendClientError(link, "Ticket Tracker entry ID or Jira project not specified");
		return;
	}
	pEntry = findTicketEntryByID(iUniqueID);
	if(!pEntry)
	{
		httpSendClientError(link, "Could not find the specified Ticket Tracker entry");
		return;
	}

	if (!pUser)
	{
		httpRedirect(link, "/login");
		return;
	}

	loadJiraData();
	pJiraProject = findJiraProjectByKey(pProject);
	if (!pJiraProject)
	{
		httpSendClientError(link, "Could not find the project");
		return;
	}
	pComponentsList = StructCreate(parse_JiraComponentList);
	jiraGetComponents(pJiraProject, pComponentsList);
	
	estrCopy2(&s, "");
	estrCopy2(&pDetailLink, "");
	formAppendStart(&s, "/jiracreateissue", "POST", "jiraform", NULL);

	formAppendHiddenInt(&s, "id", iUniqueID);
	formAppendHidden(&s, "project", pProject);
	estrConcatf(&pDetailLink, "/detail?id=%d", iUniqueID);
	formAppendHidden(&s, "referer", pDetailLink);

	estrConcatf(&s, "<div class=\"heading\">ID #:</div>");
	estrConcatf(&s, "<div class=\"firstseen\">%d</div>\n", iUniqueID);

	estrConcatf(&s, "<div class=\"heading\">Project Name:</div>\n%s", pProject);

	estrConcatf(&s, "<div class=\"heading\">Assign Issue To:</div>");
	formAppendJiraUsers(&s, "assignee");
	
	estrConcatf(&s, "<div class=\"heading\">Component:</div>");
	formAppendJiraComponents(&s, "component", pComponentsList);

	estrConcatf(&s, "<div class=\"heading\">Label:</div>");
	formAppendEdit(&s, 40, "label", "");

	estrCopy2(&summary, "[Ticket] ");
	if (pEntry->gameLocation.zoneName)
		estrConcatf(&summary, "%s - ", pEntry->gameLocation.zoneName);
	estrConcatf(&summary, "%s", pEntry->pSummary);
	estrConcatf(&s, 
		"<div class=\"heading\">Summary:</div>\n");
	formAppendEdit(&s, 80, "summary", summary);
	estrDestroy(&summary);

	estrConcatf(&s, 
		"<div class=\"heading\">Additional Information:</div>\n"
		"This information will be concatenated to the Ticket's description.<br>\n");
	estrConcatf(&s, "<div style=\"font-family: Times, Arial, serif, sans-serif; font-size: large\">\n");
	formAppendTextarea(&s, 20, 80, "description", "");
	estrConcatf(&s, "</div>\n");

	estrConcatf(&s, "<br>");
	formAppendSubmit(&s, "Submit Issue");
	formAppendEnd(&s);

	estrConcatf(&s, "<br><br><a href=\"%s\">Go Back...</a>", pDetailLink);
	StructDestroy(parse_JiraComponentList, pComponentsList);

	httpSendWrappedString(link, s, pCookies);
}

static void DumpEntryToStringForJira (char **estr, TicketEntry *pTicket)
{
	estrConcatf(estr, "Summary:\n%s\n\n"
		"Description:\n%s\n\n"
		"From (account):\n%s\n\n",
		pTicket->pSummary,
		pTicket->pUserDescription, 
		pTicket->ppUserInfo ? pTicket->ppUserInfo[0]->pAccountName : "Unknown");

	estrConcatf(estr, "http://%s:%d/detail?id=%d\n", getMachineAddress(), giHttpPort, pTicket->uID);
	if (pTicket->pScreenshotFilename)
		estrConcatf(estr, "http://%s:%d/file/tickettracker/0/fileSystem/%s\n", getMachineAddress(), giServerMonitorPort, pTicket->pScreenshotFilename);
}

static bool generateJiraIssue(TicketEntry *pEntry, const char *pProject, const char *pAssignee, 
							  const char* pComponent, const char *pLabel, 
							  const char *pUserSummary, const char* pUserDescription)
{
	JiraComponent *pJiraComponent = NULL;
	bool bRet = false;
	char key[128] = "";
	char *pComponentBreak = NULL;

	if (jiraIsLoggedIn())
	{
		char *pSummary = NULL;
		char *pDescription = NULL;
		char *pTicketDump = NULL;

		DumpEntryToStringForJira(&pTicketDump, pEntry);

		if (pUserSummary && *pUserSummary)
			estrPrintf(&pSummary, "%s", pUserSummary);
		else
			estrPrintf(&pSummary, "Ticket Tracker ID #%d", pEntry->uID);
		estrPrintf(&pDescription, 
			"[Automatically Generated from Ticket Tracker] by\n"
			"%s\n"
			"\n"
			"Information:\n"
			"%s\n",
			getComputerName(),
			pTicketDump);

		if (pUserDescription && *pUserDescription)
		{
			estrConcatf(&pDescription,
				"User Description of Issue:\n"
				"%s\n",
				pUserDescription);
		}

		if (pComponent && *pComponent)
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
		bRet = jiraCreateIssue(pProject, pSummary, pDescription, pAssignee, 1, 10, pJiraComponent, pLabel, key, 128);

		estrDestroy(&pSummary);
		estrDestroy(&pDescription);
		estrDestroy(&pTicketDump);

		if (pJiraComponent)
			StructDestroy(parse_JiraComponent, pJiraComponent);
	}
	if (bRet)
	{
		setJiraKey(pEntry, NULL, key);
	}

	return bRet;
}

static void wiAssociateJiraIssue(NetLink *link, char **args, char ** values, int count, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	static char *pDetailLink = NULL;
	int i, iUniqueID = 0;
	const char *pKey = NULL;
	TicketTrackerUser *pUser = wiConfirmLogin(pCookies);
	char *summary = NULL;

	TicketEntry *pEntry;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "id") == 0)
		{
			iUniqueID = atoi(values[i]);
		}
		else if (stricmp(args[i], "key") == 0)
		{
			pKey = values[i];
		}
	}

	if (!iUniqueID || !pKey)
	{
		httpSendClientError(link, "Ticket Tracker entry ID or Jira key not specified");
		return;
	}
	pEntry = findTicketEntryByID(iUniqueID);
	if(!pEntry)
	{
		httpSendClientError(link, "Could not find the specified Ticket Tracker entry");
		return;
	}

	if (!pUser)
	{
		httpRedirect(link, "/login");
		return;
	}
	setJiraKey(pEntry, NULL, pKey);
	httpRedirect(link, STACK_SPRINTF("/detail?id=%d", iUniqueID));
}

static void wiJiraCreateIssue(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	int iUniqueID = 0;
	TicketEntry *pEntry = NULL;
	const char *pAssignee = urlFindValue(args, "assignee");
	const char *pProject = urlFindValue(args, "project");
	const char *pComponent = urlFindValue(args, "component");
	const char *pDescription = urlFindValue(args, "description");
	const char *pSummary = urlFindValue(args, "summary");
	const char *pLabel = urlFindValue(args, "label");
	const char *pRefererPassed = NULL;
	const char *tempValue = NULL;
	int count = 0;
	bool bJiraCreated = false;

	estrCopy2(&s, "");

	tempValue = urlFindValue(args, "referer");
	if (tempValue)
		pRefererPassed = tempValue;
	else
		pRefererPassed = pReferer;

	tempValue = urlFindValue(args, "id");
	if(tempValue)
		iUniqueID = atoi(tempValue);

	if (!iUniqueID)
	{
		httpSendWrappedString(link, "Ticket Tracker Entry ID must be provided for Jira creation.", pCookies);
		return;
	}
	if(!pAssignee || !strcmp(pAssignee, "--"))
	{
		httpSendWrappedString(link, "Assignee must be specified for Jira creation.", pCookies);
		return;
	}
	if(!pProject || !strcmp(pProject, "--"))
	{
		httpSendWrappedString(link, "Project must be specified for Jira creation.", pCookies);
		return;
	}

	pEntry = findTicketEntryByID(iUniqueID);
	if(pEntry)
	{
		TicketTrackerUser *pLoginUser = wiConfirmLogin(pCookies);
		bool loggedIn = false;
		
		if (!pLoginUser)
		{
			httpRedirect(link, "/login");
			return;
		}

		loggedIn = jiraLogin(jiraGetAddress(), jiraGetPort(), pLoginUser->username, pLoginUser->password);
		// It's a valid request. Create an issue.
		if (loggedIn)
		{
			bJiraCreated = generateJiraIssue(pEntry, pProject, pAssignee, pComponent, pLabel, pSummary, pDescription);
			jiraLogout();
		}
		else
		{
			estrPrintf(&s, "Failed to log in to Jira.");
			httpSendWrappedStringClearCookies(link, s, pCookies, NULL);
			return;
		}
		if (bJiraCreated)
		{
			estrConcatf(&s, "Generated an issue for ID %d. [assigned to %s] [<a target=\"_new\" href=\"%sbrowse/%s\">%s</a>]<br>\n", 
				pEntry->uID, pAssignee, jiraGetDefaultURL(), pEntry->pJiraIssue->key, pEntry->pJiraIssue->key);
		}
		else
			estrConcatf(&s, "Failed to generate an issue for ID %d.<br>\n", pEntry->uID);
	} 
	else 
	{
		httpSendWrappedString(link, "Unable to locate Ticket Tracker Entry for Jira creation.", pCookies);
		return;
	}

	if (bJiraCreated)
		estrConcatf(&s, "<br>Jira issue successfully created.");
	else
		estrConcatf(&s, "<br>Jira issue not created for an unknown reason. Quite possibly due to Jira itself.");

	estrConcatf(&s, "<br><br><a href=\"%s\">Go Back...</a>", pRefererPassed);

	httpSendWrappedString(link, s, pCookies);
}

// ------------------------------------------------
// HTTP / HTML Handling

static void httpHandlePost(NetLink *link, HttpClientStateDefault *pClientState)
{
	char *pErr  = NULL;
	char *arg   = NULL;
	char *value = NULL;
	char *pReferer = NULL;
	char *url = NULL;
	bool setCookies = false;
	CookieList *pCookies = StructCreate(parse_CookieList);
	UrlArgumentList *arglist;
	httpParseHeader(pClientState->pPostHeaderString, "POST", &url, &pReferer, pCookies);

	if(!url)
	{
		estrDestroy(&pReferer);
		StructDestroy(parse_CookieList, pCookies);
		return;
	}

	arglist = urlToUrlArgumentList(pClientState->pPostDataString);

	if (!(hasCookie(pCookies, TICKETCOOKIE_USERNAME) && hasCookie(pCookies, TICKETCOOKIE_PASSWORD)))
	{
		const char *pUsername;
		const char *pPassword;

		pUsername = urlFindValue(arglist, "username");
		pPassword = urlFindValue(arglist, "password");

		if (pUsername && pPassword)
		{
			Cookie *pNewCookie = StructCreate(parse_Cookie);
			pNewCookie->pName = strdup(TICKETCOOKIE_USERNAME);
			pNewCookie->pValue = strdup(pUsername);
			eaPush(&pCookies->ppCookies, pNewCookie);

			pNewCookie = StructCreate(parse_Cookie);
			pNewCookie->pName = strdup(TICKETCOOKIE_PASSWORD);
			pNewCookie->pValue = strdup(pPassword);
			eaPush(&pCookies->ppCookies, pNewCookie);
			setCookies = true;
		}
	}

	if (stricmp(url, "/login")==0)
	{
		if (setCookies)
		{
			TicketTrackerUser * user;
			if (user = wiConfirmLogin(pCookies))
			{
				if (wiConfirmJiraLogin(user))
				{
					const char * pRedirect = urlFindValue(arglist, "redirect");
					if (pRedirect)
						httpSendWrappedStringPlusCookies(link, "Successfully logged in to Ticket Tracker.", pCookies, pRedirect);
					else
					{
						httpSendWrappedStringPlusCookies(link, "Successfully logged in to Ticket Tracker.",
							pCookies, STACK_SPRINTF("/user?id=%s", user->username));
					}
				}
				else
				{
					httpSendWrappedStringClearCookies(link, "Failed to login to Jira", pCookies, NULL);
				}
			}
			else
			{
				SendWrappedError(link, "No username or password specified.", pCookies);
			}
		}
		else
		{
			wiAuthenticate(link, pReferer, pCookies);
		}
	}
	else if (stricmp(url, "/createusergroup") == 0)
	{
		wiCreateUserGroup(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/comment") == 0)
	{
		wiAddComment(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/respond") == 0)
	{
		wiEditResponse(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/cticket") == 0)
	{
		wiCreateTicket(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/editRule") == 0)
	{
		wiApplyEditRule(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/deleteRules") == 0)
	{
		wiDeleteRules(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/batchActionRun") == 0)
	{
		wiBatchActionRun(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/changecategory") == 0)
	{
		wiChangeCategory(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/jiracreateissue") == 0)
	{
		wiJiraCreateIssue(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/batchJiraCreate") == 0)
	{
		wiBatchJiraCreate(link, arglist, pReferer, pCookies);
	}

	StructDestroy(parse_CookieList, pCookies);
	StructDestroy(parse_UrlArgumentList, arglist);
	estrDestroy(&pReferer);
	estrDestroy(&url);
}

static void httpHandleGet(char *data, NetLink *link, HttpClientStateDefault *pClientState)
{
	char	*url_esc = NULL,*args[100] = {0},*values[100] = {0};
	char	buf[MAX_PATH];
	int		count, i;
	int		rawDataRemaining = linkGetRawDataRemaining(link);
	bool	bIsPostData = false;
	char *pReferer = NULL;
	CookieList *pCookies = StructCreate(parse_CookieList);

	httpParseHeader(data, "GET", &url_esc, &pReferer, pCookies);

	if (!url_esc)
	{
		StructDestroy(parse_CookieList, pCookies);
		estrDestroy(&pReferer);
		return;
	}
	count = urlToArgs(url_esc,args,values,ARRAY_SIZE(args));
	if (!count)
	{
		StructDestroy(parse_CookieList, pCookies);
		estrDestroy(&url_esc);
		estrDestroy(&pReferer);
		return;
	}

	ANALYSIS_ASSUME(args[0] != NULL);
	if (strlen(args[0]) >= MAX_PATH)
		return;
	strcpy_s(buf, MAX_PATH, args[0]);

	if (stricmp(args[0],"/")==0)
	{
		TicketTrackerUser *pUser = wiConfirmLogin(pCookies);
		
		if (pUser)
		{
			httpRedirect(link, STACK_SPRINTF("/user?id=%s", pUser->username));
		}
		else
			httpRedirect(link, gDefaultPage);
	}
	else
	{
		if(stricmp(args[0], "/search") == 0)
		{
			wiSearch(link, args, values, count, pCookies);
		}
		else if (stricmp(args[0], "/csv") == 0)
		{
			wiSaveCSV(link, pClientState, count, args, values, pReferer, pCookies);
		}
		else if(stricmp(args[0], "/detail") == 0)
		{
			wiDetail(link, args, values, count, pCookies);
		}
		else if(stricmp(args[0], "/cinfo") == 0)
		{
			wiCharacterInfo(link, args, values, count, pCookies);
		}
		else if(stricmp(args[0], "/status") == 0)
		{
			wiChangeStatus(link, args, values, count, pCookies);
		}
		else if(stricmp(args[0], "/changegroup") == 0)
		{
			wiChangeGroup(link, args, values, count, pCookies);
		}
		else if (stricmp(args[0], "/changeVisibility") == 0)
		{
			wiChangeTicketVisible(link, args, values, count, pCookies);
		}
		else if (stricmp(args[0], "/deleteconfirm") == 0)
		{
			wiDeleteTicketConfirm(link, args, values, count, pCookies);
		}
		else if (stricmp(args[0], "/deleteticket") == 0)
		{
			wiDeleteTicket(link, args, values, count, pCookies);
		}
		/*else if(stricmp(args[0], "/group") == 0)
		{
			wiViewGroup(link, args, values, count, pCookies);
		}*/
		else if(stricmp(args[0], "/usergroup") == 0)
		{
			wiViewUserGroup(link, args, values, count, pCookies);
		}
		else if(stricmp(args[0], "/user") == 0)
		{
			wiViewUser(link, args, values, count, pCookies);
		}
		else if(stricmp(args[0], "/newticket") == 0)
		{
			wiNewTicket(link, args, values, count, pCookies);
		}
		else if (stricmp(args[0], "/assignticket") == 0)
		{
			wiAssignTicket(link, args, values, count, pCookies);
		}
		else if (stricmp(args[0], "/rules") == 0)
		{
			wiViewAllRules(link, args, values, count, pCookies);
		}
		else if (stricmp(args[0], "/editRule") == 0)
		{
			wiEditAssignmentRule(link, args, values, count, pCookies);
		}
		else if (stricmp(args[0], "/moveRule") == 0)
		{
			wiMoveAssignmentRule(link, args, values, count, pCookies);
		}
		else if (stricmp(args[0], "/batchAction") == 0)
		{
			wiBatchActionForm(link, args, values, count, pCookies);
		}
		else if (stricmp(args[0], "/jiradetailissue")==0)
		{
			wiAppendJiraDetailing(link, args, values, count, pReferer, pCookies);
		} 
		else if (stricmp(args[0], "/jiraassocissue")==0)
		{
			wiAssociateJiraIssue(link, args, values, count, pReferer, pCookies);
		} 
		else if (stricmp(args[0], "/login") == 0)
		{
			if (wiConfirmLogin(pCookies))
			{
				SendWrappedError(link, "Already logged in to Ticket Tracker!\nPlease log out first if you wish to change logins.", pCookies);
			}
			else
			{
				wiAuthenticate(link, pReferer, pCookies);
			}
		}
		else if (stricmp(args[0], "/logout") == 0)
		{
			if (wiConfirmLogin(pCookies))
			{
				httpSendWrappedStringClearCookies(link, "Logged out of Ticket Tracker.", pCookies, "/search");
			}
			else
			{
				httpSendWrappedStringClearCookies(link, "You are not logged in!", pCookies, NULL);
			}
		}
		else if (args[0] && strStartsWith(args[0], "/screenshots"))
		{	//This is deprecated by generic file serving.
			static char *pFileName = NULL;
			estrCopy2(&pFileName, gTicketTrackerAltDataDir);
			estrAppendUnescaped(&pFileName, args[0]);

			if (fileExists(pFileName))
				httpSendImageFile(link, pFileName, "jpeg");
			else
				httpSendFileNotFoundError(link, "Image not found.");
		}
		else
		{
			FILE * file = fopen(STACK_SPRINTF("%s\\%saccess_log.txt", fileLocalDataDir(), gWebInterfaceAltRootDir), "a");
			struct in_addr ina = {0};
			char *ipString;
			ina.S_un.S_addr = linkGetIp(link);
			ipString = inet_ntoa(ina);
			if (file)
			{
				fprintf(file, "HTTP Get Error: URL[%s], IP[%s]\n", url_esc, ipString);
				fclose(file);
			}
			httpSendWrappedString(link, "Unknown Command or filename.", pCookies);
		}
	}
	estrDestroy(&url_esc);
	estrDestroy(&pReferer);
	StructDestroy(parse_CookieList, pCookies);
	for (i=0; i<ARRAY_SIZE(args); i++)
	{
		estrDestroy(&args[i]);
		estrDestroy(&values[i]);
	}
}

static int httpDisconnect(NetLink* link, HttpClientStateDefault *pClientState)
{
	LinkState *pLinkState = findLinkState(link);

	httpDefaultDisconnectHandler(link, pClientState);

	if(!pLinkState)
	{
		return 0;
	}

	printf("Cancelling File Transfer for Link: 0x%x\n", (INT_PTR)link);
	removeLinkState(pLinkState);
	return 0;
}

void updateWebInterface(void)
{
	updateLinkStates();
}