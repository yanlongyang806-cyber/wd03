#include "ETCommon/ETWebCommon.h"
#include "ETCommon/ETCommonStructs.h"
#include "ETCommon/ETIncomingData.h"
#include "ETCommon/ETShared.h"

#include "StashTable.h"
#include "net.h"
#include "netlink.h"
#include "HttpClient.h"
#include "HttpLib.h"
#include "estring.h"
#include "file.h"
#include "jira.h"
#include "timing.h"
#include "htmlForms.h"
#include "logging.h"

extern ParseTable parse_CookieList[];
#define TYPE_parse_CookieList CookieList
extern ErrorTrackerSettings gErrorTrackerSettings;

#define MAX_USERS_TO_SHOW (10)

static StashTable stHttpGetHandlers = NULL;
static StashTable stHttpPostHandlers = NULL;
static ETWebDefaultGetHandlerFunc sDefaultGetHandler = NULL;
static ETWebDefaultPostHandlerFunc sDefaultPostHandler = NULL;

// For source-controller files (eg. HTML / CSS files)
static char sWebRootSourcePath[MAX_PATH] = "";
// For data files (eg. dumps)
static char sWebRootDataPath[MAX_PATH] = "";

const char *getJiraHost(void)
{
	return gErrorTrackerSettings.pJiraHost;
}
int getJiraPort(void)
{
	return gErrorTrackerSettings.iJiraPort;
}

void ETWeb_SetSourceDir(const char *dirpath)
{
	strcpy(sWebRootSourcePath, dirpath);
	forwardSlashes(sWebRootSourcePath);
}
void ETWeb_SetDataDir(const char *dirpath)
{
	strcpy(sWebRootDataPath, dirpath);
	forwardSlashes(sWebRootDataPath);
}
const char *ETWeb_GetSourceDir(void)
{
	return sWebRootSourcePath;
}
const char *ETWeb_GetDataDir(void)
{
	return sWebRootDataPath;
}

void ETWeb_InitWebRootDirs(void)
{
	char *dirPath = NULL;
	estrStackCreate(&dirPath);
	estrPrintf(&dirPath, "server/ErrorTracker/WebRoot/");
	ETWeb_SetSourceDir(dirPath);
	estrPrintf(&dirPath, "%s/ErrorTracker/WebRoot/", fileLocalDataDir());
	ETWeb_SetDataDir(dirPath);
	estrDestroy(&dirPath);
}


void ETWebInit(void)
{
	stHttpGetHandlers = stashTableCreateWithStringKeys(20, StashDeepCopyKeys_NeverRelease);
	stHttpPostHandlers = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
}

void ETWeb_RegisterGetHandler(const char *url, ETWebGetHandler func)
{
	if (!stHttpGetHandlers)
		ETWebInit();
	stashAddPointer(stHttpGetHandlers, url, func, true);
}

void ETWeb_RegisterPostHandler(const char *url, ETWebPostHandler func)
{
	if (!stHttpPostHandlers)
		ETWebInit();
	stashAddPointer(stHttpPostHandlers, url, func, true);
}

// Find and run a handler for an HTTP GET request.
bool ETWeb_RunGetHandler(const char *url, NetLink *link, char **args, char **values, int count, 
						 const char *pReferer, CookieList *pCookies)
{
	StashElement element = NULL;

	// Try to find a handler.
	stashFindElement(stHttpGetHandlers, url, &element);

	// If not found, try to filter the URL to '/<url>/'
	if (!element)
	{
		const char *urlEnd = url;
		if (*urlEnd == '/')
		{
			urlEnd = strchr(++urlEnd, '/');
			if (urlEnd)
			{
				char baseURL[64] = "";
				++urlEnd;
				if (url-urlEnd < 64)
				{
					strncpy(baseURL, url, urlEnd-url);
					stashFindElement(stHttpGetHandlers, baseURL, &element);
				}
			}
		}
	}

	// If we found a handler, run it.
	if (element)
	{
		const char *timer_name = stashElementGetStringKey(element);
		ETWebGetHandler func = stashElementGetPointer(element);
#pragma warning(suppress:4054) // 'type cast' : from function pointer 'ETWebGetHandler' to data pointer 'PerfInfoStaticData **'
		PERFINFO_AUTO_START_STATIC(timer_name, (PerfInfoStaticData **)func, 1);
		func(link, args, values, count, pReferer, pCookies);
		PERFINFO_AUTO_STOP();
		return true;
	}

	// No handler found.
	ADD_MISC_COUNT(1000000, "Unknown URL");
	return false;
}

// Find and run a handler for an HTTP POST request.
bool ETWeb_RunPostHandler(const char *url, NetLink *link, UrlArgumentList *args, const char *pReferer, 
						  CookieList *pCookies)
{
	StashElement element = NULL;

	// Try to find a handler.
	stashFindElement(stHttpPostHandlers, url, &element);
	if (element)
	{
		const char *timer_name = stashElementGetStringKey(element);
		ETWebPostHandler func = stashElementGetPointer(element);
#pragma warning(suppress:4054) // 'type cast' : from function pointer 'ETWebGetHandler' to data pointer 'PerfInfoStaticData **'
		PERFINFO_AUTO_START_STATIC(timer_name, (PerfInfoStaticData **)func, 1);
		func(link, url, args, pReferer, pCookies);
		PERFINFO_AUTO_STOP();
		return true;
	}

	ADD_MISC_COUNT(1000000, "Unknown URL");
	return false;

}

// -----------------------------------------------------------
// General Web Handlers

static char sDefaultPage[MAX_PATH] = ""; // not set

void ETWeb_SetDefaultPage (const char *page)
{
	if (!page)
		return;
	if (page[0] != '/')
		sprintf(sDefaultPage, "/%s", page);
	else
		strcpy(sDefaultPage, page);
}
void ETWeb_SetDefaultGetHandler(ETWebDefaultGetHandlerFunc func)
{
	sDefaultGetHandler = func;
}
void ETWeb_SetDefaultPostHandler(ETWebDefaultPostHandlerFunc func)
{
	sDefaultPostHandler = func;
}

void ETWeb_httpHandlePost(NetLink *link, HttpClientStateDefault *pClientState)
{
	PERFINFO_AUTO_START("ET_PostRequest", 1);{
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
			PERFINFO_AUTO_STOP();
			return;
		}
		arglist = urlToUrlArgumentList(pClientState->pPostDataString);

		if (ETWeb_RunPostHandler(url, link, arglist, pReferer, pCookies))
		{
		}
		else if (sDefaultPostHandler && sDefaultPostHandler(link, arglist))
		{
		}
		else // Bad stuff here
		{
			httpSendWrappedString(link, "Unknown POST operation.", NULL, pCookies);
		}

		StructDestroy(parse_CookieList, pCookies);
		StructDestroy(parse_UrlArgumentList, arglist);
		estrDestroy(&pReferer);
		estrDestroy(&url);
	}PERFINFO_AUTO_STOP();
}

void ETWeb_httpHandleGet(char *data, NetLink *link, HttpClientStateDefault *pClientState)
{
	PERFINFO_AUTO_START("ET_GetRequest", 1);
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
			PERFINFO_AUTO_STOP();
			return;
		}
		count = urlToArgs(url_esc,args,values,ARRAY_SIZE(args));
		if (!count)
		{
			StructDestroy(parse_CookieList, pCookies);
			estrDestroy(&url_esc);
			estrDestroy(&pReferer);
			PERFINFO_AUTO_STOP();
			return;
		}
		ANALYSIS_ASSUME(args[0] != NULL);
		if (strlen(args[0]) >= MAX_PATH) return; // bad URL - it's too long
		strcpy(buf, args[0]);

		if (stricmp(args[0],"/")==0)
		{
			if (sDefaultPage[0])
				httpRedirect(link, sDefaultPage);
			else
				httpSendFileNotFoundError(link, "No homepage specified");
		}
		else if (ETWeb_RunGetHandler(args[0], link, args, values, count, pReferer, pCookies))
		{
		}
		else if (sDefaultGetHandler && sDefaultGetHandler(link, args, values, count))
		{
		}
		else // Bad stuff here
		{
			httpSendWrappedString(link, "Unknown Command or filename.", NULL, pCookies);
		}
		estrDestroy(&url_esc);
		estrDestroy(&pReferer);
		StructDestroy(parse_CookieList, pCookies);
		for (i=0; i<ARRAY_SIZE(args); i++)
		{
			estrDestroy(&args[i]);
			estrDestroy(&values[i]);
		}
	}PERFINFO_AUTO_STOP();
}

// -----------------------------------------------------------
// General HTTP String Senders

static int iWebFlags = 0;
void wiSetFlags(int iFlags)
{
	iWebFlags = iFlags;
}
int wiGetFlags(void)
{
	return iWebFlags;
}

void constructHtmlHeaderEnd(char **estr, bool isLoggedIn, CookieList *pList, const char *pRedirect)
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
		bool bHidingComments = hasCookie(pList, "hidecomments");

		sprintf_s (SAFESTR(headerEnd), "%s/%s", ETWeb_GetSourceDir(), "headerEnd.html.format.txt" );
		file = fileOpen(headerEnd, "rb");
		assert (file);
		size = fileGetSize(file);
		assert (size > 0);

		text = (char *) malloc(size + 1);
		fread(text, sizeof(char), size, file);
		text[size] = 0;

		if (!(iWebFlags & DUMPENTRY_FLAG_NO_JIRA))
		{
			estrConcatf(estr, FORMAT_OK(text),
				isLoggedIn ? "[<a href=\"/logout\">Logout</a>]" : "[<a href=\"/login\">Login</a>]", 
				bHidingComments ? "Show Comments" : "Hide Comments");
		}
		else
		{
			estrConcatf(estr, FORMAT_OK(text), "", bHidingComments ? "Show Comments" : "Hide Comments");
		}

		free(text);
		fclose(file);
	}
}

char *DEFAULT_LATELINK_httpGetAppCustomHeaderString(void)
{
	return "";
}

static bool sbHTMLTemplateLoaded = false;
static U32 siStyleLen = 0;
static char *spStyle = NULL;
static U32 siFooterLen = 0;
static char *spFooter = NULL;

static char spHTMLDefaultTitle[64] = "Error Tracker";
void ETWeb_SetDefaultTitle(const char *title)
{
	sprintf(spHTMLDefaultTitle, "%s", title);
}
static bool sbEnableDynamicTitles = false;
void ETWeb_EnableDynamicHTMLTitles(bool bEnable)
{
	sbEnableDynamicTitles = bEnable;
}

static const char *htmlFormatTitle (const char *title)
{
	static char sBuffer[128] = "";
	if (!sbEnableDynamicTitles)
		return "";
	sprintf(sBuffer, "<!DOCTYPE HTML>\n<html>\n<head>\n<title>%s</title>\n\n", title ? title : spHTMLDefaultTitle);
	return sBuffer;
}

static void httpLoadHTMLTemplate(void)
{
	char styleFile[MAX_PATH];
	char footerFile[MAX_PATH];
	FILE *pFile;

	if (sbHTMLTemplateLoaded)
		return;
	sprintf(styleFile, "%s/%s", ETWeb_GetSourceDir(), "style.html");
	sprintf(footerFile, "%s/%s", ETWeb_GetSourceDir(), "footer.html");

	if (spStyle)
	{
		free(spStyle);
		spStyle = NULL;
	}
	if (spFooter)
	{
		free(spFooter);
		spFooter = NULL;
	}

	pFile = fileOpen(styleFile, "rb");
	if (pFile)
	{
		siStyleLen = (U32) fileGetSize(pFile);
		spStyle = malloc(siStyleLen+1);
		fread(spStyle, 1, siStyleLen, pFile);
		spStyle[siStyleLen] = '\0';
		fclose(pFile);
	}
	pFile = fileOpen(footerFile, "rb");
	if (pFile)
	{
		siFooterLen = (U32) fileGetSize(pFile);
		spFooter = malloc(siFooterLen+1);
		fread(spFooter, 1, siFooterLen, pFile);
		spFooter[siFooterLen] = '\0';
		fclose(pFile);
	}
	sbHTMLTemplateLoaded = true;
}

void httpSendWrappedString(NetLink *link, const char *pBody, const char *pTitle, CookieList *pList)
{
	static char *pHeaderEnd = NULL;
	char *pAppCustomHeaderString = NULL;
	int iAppCustomStringLen = 0;

	int iTotalLength;
	int iStrLen = (int) strlen(pBody);
	int iHeaderEndLen = 0;
	const char *pTitleHTML = htmlFormatTitle(pTitle);
	int iTitleLen = (int) strlen(pTitleHTML);
	
	httpLoadHTMLTemplate();
	pAppCustomHeaderString = httpGetAppCustomHeaderString();	

	estrCopy2(&pHeaderEnd, "");
	constructHtmlHeaderEnd(&pHeaderEnd, hasCookie(pList, "username") && hasCookie(pList, "password"), pList, NULL);
	iHeaderEndLen = (int) strlen(pHeaderEnd);
	
	iTotalLength  = iStrLen + iHeaderEndLen + iTitleLen;
	iTotalLength += siStyleLen;
	iTotalLength += siFooterLen;
	if (pAppCustomHeaderString)
	{
		iTotalLength += (iAppCustomStringLen = (int) strlen(pAppCustomHeaderString));
	}

	httpSendBasicHeader(link, iTotalLength, NULL);
	httpSendBytesRaw(link, (void*) pTitleHTML, iTitleLen);
	httpSendBytesRaw(link, (void*) spStyle, siStyleLen);

	if (iAppCustomStringLen)
	{
		httpSendBytesRaw(link, (void*)pAppCustomHeaderString, iAppCustomStringLen);
	}

	httpSendBytesRaw(link, (void*) pHeaderEnd, iHeaderEndLen);
	httpSendBytesRaw(link, (void*) pBody, iStrLen);
	httpSendBytesRaw(link, (void*) spFooter, siFooterLen);

	httpSendComplete(link);
}

void httpSendWrappedStringPlusCookies(NetLink *link, const char *pBody, const char *pTitle, CookieList *pList, const char *pRedirect)
{
	static char *pHeaderEnd = NULL;
	int iTotalLength;

	int iStrLen = (int) strlen(pBody);
	int iHeaderEndLen = 0;
	const char *pTitleHTML = htmlFormatTitle(pTitle);
	int iTitleLen = (int) strlen(pTitleHTML);

	httpLoadHTMLTemplate();
	estrCopy2(&pHeaderEnd, "");
	constructHtmlHeaderEnd(&pHeaderEnd, hasCookie(pList, "username") && hasCookie(pList, "password"), pList, pRedirect);
	iHeaderEndLen = (int) strlen(pHeaderEnd);

	iTotalLength  = iStrLen + iHeaderEndLen + iTitleLen;
	iTotalLength += siStyleLen;
	iTotalLength += siFooterLen;

	httpSendBasicHeaderPlusCookies(link, iTotalLength, NULL, pList);
	httpSendBytesRaw(link, (void*) pTitleHTML, iTitleLen);
	httpSendBytesRaw(link, (void*) spStyle, siStyleLen);
	httpSendBytesRaw(link, (void*) pHeaderEnd, iHeaderEndLen);
	httpSendBytesRaw(link, (void*) pBody, iStrLen);
	httpSendBytesRaw(link, (void*) spFooter, siFooterLen);

	httpSendComplete(link);
}

void httpSendWrappedStringClearCookies(NetLink *link, const char *pBody, const char *pTitle, CookieList *pList, CookieList *pToRemove, const char *pRedirect)
{
	static char *pHeaderEnd = NULL;
	int iTotalLength;
	int iStrLen = (int) strlen(pBody);
	int iHeaderEndLen = 0;
	const char *pTitleHTML = htmlFormatTitle(pTitle);
	int iTitleLen = (int) strlen(pTitleHTML);
	
	httpLoadHTMLTemplate();
	estrCopy2(&pHeaderEnd, "");
	constructHtmlHeaderEnd(&pHeaderEnd, hasCookie(pList, "username") && hasCookie(pList, "password"), pList, pRedirect);
	iHeaderEndLen = (int) strlen(pHeaderEnd);

	iTotalLength  = iStrLen + iHeaderEndLen + iTitleLen;
	iTotalLength += siStyleLen;
	iTotalLength += siFooterLen;

	httpSendBasicHeaderClearCookies(link, iTotalLength, NULL, pToRemove);
	httpSendBytesRaw(link, (void*) pTitleHTML, iTitleLen);
	httpSendBytesRaw(link, (void*) spStyle, siStyleLen);
	httpSendBytesRaw(link, (void*) pHeaderEnd, iHeaderEndLen);
	httpSendBytesRaw(link, (void*) pBody, iStrLen);
	httpSendBytesRaw(link, (void*) spFooter, siFooterLen);

	httpSendComplete(link);
}

// ----------------------------------------------------------------------------------
// Initialization code / Handlers

static int httpDisconnect(NetLink* link, HttpClientStateDefault *pClientState)
{
	LinkState *pLinkState = findLinkState(link);
	httpDefaultDisconnectHandler(link, pClientState);

	if(!pLinkState)
		return 0;

	printf("Cancelling File Transfer for Link: 0x%x\n", (INT_PTR)link);
	servLog(LOG_DUMPDATA, "LinkStateRemoved", "Cancelling file %s to link %d", pLinkState->pFile->nameptr, linkID(pLinkState->pLink));
	removeLinkState(pLinkState);
	RemoveClientLink(link);
	return 0;
}

static int sHttpPort = 0;
AUTO_CMD_INT(sHttpPort, httpPort);
void ETWeb_Init(void)
{
	NetListen *listen_socket;
	for(;;)
	{
		setHttpDefaultPostHandler(ETWeb_httpHandlePost);
		setHttpDefaultGetHandler(ETWeb_httpHandleGet);
		listen_socket = commListen(errorTrackerCommDefault(), LINKTYPE_HTTP_SERVER, LINK_HTTP, 
			sHttpPort > 0 ? sHttpPort : gErrorTrackerSettings.iWebInterfacePort,
			httpDefaultMsgHandler, httpDefaultConnectHandler, httpDisconnect,
			sizeof(HttpClientStateDefault));
		if (listen_socket)
			break;
		Sleep(1);
	}
}

void shutdownWebInterface(void)
{
	printf("Cleaning up web links...\n");
	cleanupLinkStates();
}

void updateWebInterface(void)
{
	updateLinkStates();
}

// ------------------------------------------------------------
// Dump Error Entry Functions

void appendVersionLink(char **estr, const char *version)
{
	bool isNumeric = true;
	int iSVNRevision;

	if (!version)
		return;
	iSVNRevision = atoi(version);

	isNumeric = iSVNRevision > 3000; // Not a year and non-zero

	if(isNumeric)
	{
		// All numeric versions are probably SVN revision numbers.
		estrConcatf(estr, "<a href=\"http://code:8083/browser?rev=%d\">%s</a>", iSVNRevision, version);
	}
	else if(strStartsWith(version, "200"))
	{
		// It is probably something like "2009-06-01 ..." No interesting link for that.
		estrConcatf(estr, "%s", version);
	}
	else
	{
		// patchinternal link. We'll probably get some false positives here.
		const char *productName = NULL;
		char *patchName = strstr(version, "Patch: ");
		
		if (patchName)
		{
			// Ex: 59871 Patch: PM4_2008_10_01_12_11_06			
			patchName += 7; // advance past "Patch: "
			productName = getVersionPatchProject(patchName);
			if (productName)
				estrConcatf(estr, "<a href=\"http://patchinternal/%s/view/%s/\">%s</a>", productName, patchName, patchName);
		}
		else
		{
			// Ex: FC.9.20090601.5 (SVN 71037(svn://code/FightClub/Baselines/FC.9.20090601.0) Gimme 05310920:04:13)(+incrs)
			char *truncate = NULL;
			estrStackCreate(&patchName);
			estrCopy2(&patchName, version);
			truncate = strchr(patchName, ' ');
			if (truncate && *truncate)
				*truncate = 0;
			productName = getVersionPatchProject(patchName);
			if (productName)
				estrConcatf(estr, "<a href=\"http://patchinternal/%s/view/%s/\">%s</a>", productName, patchName, patchName);
			estrDestroy(&patchName);
		}


		if (!productName)
		{
			estrConcatf(estr, "%s", version);
		}
	}
}

void appendCallStackToString(ErrorEntry *p, char **estr)
{
	U32 uTime = timeSecondsSince2000();
	int i;
	bool bValidBlameInfo = /*hasValidBlameInfo(p) && */!ERRORDATATYPE_IS_A_ERROR(p->eType);

	estrConcatf(estr, "<div class=\"heading\">Call Stack:</div>"
		"<table class=\"callstack\" width=\"100%%\"><tr>\n"
		"<td class=\"tableheading\">Blamed</td>\n"
		"<td class=\"tableheading\">Days Ago</td>\n"
		"<td class=\"tableheading\">Module</td>\n"
		"<td class=\"tableheading\">Function</td>\n"
		"<td class=\"tableheading\" align=right>File / Line</td></tr>\n"
		);

	for(i=0; i<eaSize(&p->ppStackTraceLines); i++)
	{
		const char *pModuleName = getPrintableModuleName(p, p->ppStackTraceLines[i]->pModuleName);

		estrConcatf(estr, "<tr>");
		if (bValidBlameInfo && p->ppStackTraceLines[i]->pBlamedPerson)
		{
			int iDaysAgo = calcElapsedDays(p->ppStackTraceLines[i]->uBlamedRevisionTime, uTime);
			estrConcatf(estr, "<td><span class=\"callstackblamename\">%s</span></td>",
				p->ppStackTraceLines[i]->pBlamedPerson);
			estrConcatf(estr, "<td><span class=\"callstackblametime\">%d</span></td>",
				iDaysAgo);
		}
		else
		{
			estrConcatf(estr, "<td><span class=\"callstackblamename\">?</span></td>");
			estrConcatf(estr, "<td><span class=\"callstackblametime\">?</span></td>");
		}

		estrConcatf(estr, "<td><span class=\"callstackmodule\">%s</span></td>", pModuleName);			

		if (strnicmp(p->ppStackTraceLines[i]->pFunctionName, "-nosymbols-", 11))
			estrConcatf(estr, "<td><span class=\"callstackfunc\">%s()</span></td>", 
				p->ppStackTraceLines[i]->pFunctionName);
		else
			estrConcatf(estr, "<td><span class=\"callstackfunc\">-No PC for this frame-</span></td>");

		estrConcatf(estr, "<td align=right><a href=\"file://%s\"><span class=\"callstackfile\">%s</span></a> <span class=\"callstackline\">(%d)</span></td>",
			p->ppStackTraceLines[i]->pFilename,
			p->ppStackTraceLines[i]->pFilename,
			p->ppStackTraceLines[i]->iLineNum);
	}
	estrConcatf(estr, "</table>\n\n");
}

void appendTriviaStrings(char **estr, CONST_EARRAY_OF(TriviaData) ppTriviaData)
{
	static char * pEscaped = NULL;
	int i;
	if (!ppTriviaData)
		return;

	estrConcatf(estr, "<table border=1 cellspacing=0 cellpadding=3>\n");

	estrConcatf(estr, 
		"<tr>"
		"<td class=\"summaryheadtd\">Key</td>\n"
		"<td class=\"summaryheadtd\">Value</td>\n"
		"</tr>");

	for (i=0; i<eaSize(&ppTriviaData); i++)
	{
		estrCopyWithHTMLEscaping(&pEscaped, ppTriviaData[i]->pKey, false);
		estrConcatf(estr,
			"<tr>"
			"<td class=\"summarytdend\">%s</td>\n", pEscaped);
		estrCopyWithHTMLEscaping(&pEscaped, ppTriviaData[i]->pVal, false);
		/*if (stricmp(ppTriviaData[i]->pKey, "CommandLine") == 0) // wrap this, since command-lines are bloody long
			estrConcatf(estr, "<td>%s</td>\n</tr>\n", pEscaped);
		else*/
		estrConcatf(estr, "<td><pre style=\"word-wrap:break-word;max-width:1000px;\">%s</pre></td>\n</tr>\n", pEscaped);
	}
	estrConcatf(estr, "</table>\n");
}

void appendCommentValues (char **estr, CONST_EARRAY_OF(CommentEntry) pTriviaItem)
{
	static char * pEscaped = NULL;
	int i, valCount;

	estrConcatf(estr, "<table border=1 cellspacing=0 cellpadding=3>\n");

	estrConcatf(estr, 
		"<tr>"
		"<td class=\"summaryheadtd\">IP Address</td>\n"
		"<td class=\"summaryheadtd\">Comment</td>\n"
		"</tr>");

	valCount = eaSize(&pTriviaItem);
	for (i=0; i<valCount; i++)
	{
		CommentEntry *value = pTriviaItem[i];
		estrCopyWithHTMLEscaping(&pEscaped, value->pIP, false);

		estrConcatf(estr, "<tr><td><div>%s</div></td>\n", pEscaped);

		estrCopyWithHTMLEscaping(&pEscaped, value->pDesc, false);

		estrConcatf(estr, "<td><div style='overflow: auto; max-height: 120px; white-space: pre-wrap; width: 900px; overflow: auto; height: expression( this.scrollHeight > 119 ? \"120px\" : \"auto\" )'>%s</div></td>\n"
			"</tr>\n", pEscaped);
	}
	estrConcatf(estr, "</table>\n");
}

void appendTriviaOverview (char **estr, CONST_EARRAY_OF(TriviaOverviewItem) ppTriviaItems)
{
	static char * pEscaped = NULL;
	int i, size;
	if (!ppTriviaItems)
	{
		estrConcatf(estr, "No trivia data retrieved.\n");
		return;
	}

	estrConcatf(estr, "<table border=1 cellspacing=0 cellpadding=3>\n");

	estrConcatf(estr, 
		"<tr>"
		"<td class=\"summaryheadtd\">Key</td>\n"
		"<td class=\"summaryheadtd\">Value(s)</td>\n"
		"<td class=\"summaryheadtd\">Count(s)</td>\n"
		"</tr>");

	size = eaSize(&ppTriviaItems);
	for (i=0; i<size; i++)
	{
		int j, valCount;
		estrCopyWithHTMLEscaping(&pEscaped, ppTriviaItems[i]->pKey, false);
		estrConcatf(estr,
			"<tr>"
			"<td class=\"summarytd\">%s</td>\n", pEscaped);

		valCount = eaSize(&ppTriviaItems[i]->ppValues);
		for (j=0; j<valCount; j++)
		{
			TriviaOverviewValue *value = ppTriviaItems[i]->ppValues[j];
			estrCopyWithHTMLEscaping(&pEscaped, value->pVal, false);
			if (j)
			{
				estrConcatf(estr, "<tr><td class=\"summarytd\"></td>\n"
					"<td><pre style=\"word-wrap:break-word;max-width:1000px;\">%s</pre></td>\n"
					"<td class=\"summarytd\">%d</td>\n"
					"</tr>\n", pEscaped, value->uCount);
			}
			else
			{
				estrConcatf(estr, "<td><pre style=\"word-wrap:break-word;max-width:1000px;\">%s</pre></td>\n"
					"<td class=\"summarytd\">%d</td>\n"
					"</tr>\n", pEscaped, value->uCount);
			}
		}
	}
	estrConcatf(estr, "</table>\n");
}

void ETWeb_DumpDataToString(ErrorEntry *p, char **estr, U32 iFlags)
{
	static char * pEscaped = NULL;
	char datetime[256];
	U32 uTime = timeSecondsSince2000();
	int iTotalDays;
	int i;

	estrConcatf(estr, "<div class=\"entry\">");

	if (p->uMergeID)
	{
		estrConcatf(estr, "<div class=\"warning\">This entry is a duplicate!</div>");
		estrConcatf(estr, "This data has been added to "
			"<a href=\"/detail?id=%d\">"
			"ET ID #%d</a>.<br>", 
			p->uMergeID, p->uMergeID);
		estrConcatf(estr, "Use "
			"<a href=\"/detail?id=%d\">"
			"ET ID #%d</a>, rather than this page.", 
			p->uMergeID, p->uMergeID);
		estrConcatf(estr, "<div class=\"heading\">Delete Entry</div> Warning! Once you delete this it is gone forever!<br>\n"
			"<a href=\"/delete?id=%d\">Delete Entry</a><br>\n", p->uID);
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_UID))
	{
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">ID #:</div>");
		estrConcatf(estr, "<div class=\"firstseen\">%d</div>\n", p->uID);
		switch (p->eType)
		{
		case ERRORDATATYPE_ERROR:
			estrConcatf(estr, "<div class=\"heading\">Type: Error</div>\n");
		xcase ERRORDATATYPE_ASSERT:
			estrConcatf(estr, "<div class=\"heading\">Type: Assert</div>\n");
		xcase ERRORDATATYPE_CRASH:
			estrConcatf(estr, "<div class=\"heading\">Type: Crash</div>\n");
		xcase ERRORDATATYPE_COMPILE:
			estrConcatf(estr, "<div class=\"heading\">Type: Compile</div>\n");
		xcase ERRORDATATYPE_FATALERROR:
			estrConcatf(estr, "<div class=\"heading\">Type: Fatal Error</div>\n");
		xcase ERRORDATATYPE_XPERF:
			estrConcatf(estr, "<div class=\"heading\">Type: Xperf Dump</div>\n");
		}
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_JIRA))
	{
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Jira:</div>");

		if (p->pJiraIssue)
		{
			estrConcatf(estr, "[<a href=\"http://%s:%d/browse/%s\">%s</a>] (%s)<br>\n"
				"Jira Status: %s<br>", 
				getJiraHost(), getJiraPort(), 
				p->pJiraIssue->key, p->pJiraIssue->key, 
				p->pJiraIssue->assignee, jiraGetStatusString(p->pJiraIssue->status));
		}
		else
		{
			// Show assignment form
			formAppendStart(estr, "/jiradetailissue", "POST", "jiraform", NULL);
			formAppendJiraUsers(estr, "assignee");
			formAppendJiraProjects(estr, "project");
			formAppendHiddenInt(estr, "id", p->uID);
			formAppendHiddenInt(estr, "count", 1);
			formAppendSubmit(estr, "Create New Issue");
			formAppendEnd(estr);
		}
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_FIRSTSEEN))
	{
		timeMakeLocalDateStringFromSecondsSince2000(datetime, p->uFirstTime);
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">First Seen:</div>");
		estrConcatf(estr, "<div class=\"firstseen\">%s</div>\n", datetime);
		// ---------------------------------------------------------------
		timeMakeLocalDateStringFromSecondsSince2000(datetime, p->uNewestTime);
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Most Recently Seen:</div>");
		estrConcatf(estr, "<div class=\"firstseen\">%s</div><br>\n", datetime);
		// ---------------------------------------------------------------
		if (p->branchTimeLogs)
		{
			estrConcatf(estr, "<table>\n");
			estrConcatf(estr, "<tr><td class=\"summaryheadtd\">Branch</td><td class=\"summaryheadtd\">Newest Time</td></tr>\n");
			for (i=0; i<eaSize(&p->branchTimeLogs); i++)
			{
				timeMakeLocalDateStringFromSecondsSince2000(datetime, p->branchTimeLogs[i]->uNewestTime);
				estrConcatf(estr, "<tr><td class=\"summarytd\">%s</td><td class=\"summarytd\">%s</td></tr>\n", p->branchTimeLogs[i]->branch, datetime);
			}
			estrConcatf(estr, "<table>\n");
		}
	}

	if (p->eType != ERRORDATATYPE_GAMEBUG && !(iFlags & DUMPENTRY_FLAG_NO_TOTALCOUNT))
	{
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Total Crash Count:</div>");
		estrConcatf(estr, "<div class=\"count\">%d</div>\n", p->iTotalCount);
		// ---------------------------------------------------------------
	}

	if (p->eType != ERRORDATATYPE_GAMEBUG && !(iFlags & DUMPENTRY_FLAG_NO_USERSCOUNT))
	{

		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Client Counts (total / max for a single crash / average):</div>");
		estrConcatf(estr, "<div class=\"count\">%d / %d / %2.2f</div>\n", 
			p->iTotalClients, 
			p->iMaxClients, 
			(p->iTotalCount) ? ((float)p->iTotalClients / p->iTotalCount) : 0.00);
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_VERSIONS))
	{

		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Branches:</div>");
		estrConcatf(estr, "<div class=\"versionrange\">\n");
		for(i=0; i<eaSize(&p->ppBranches); i++)
		{
			if(i)
				estrConcatf(estr, ", ");

			estrConcatf(estr, "%s", p->ppBranches[i]);
		}
		estrConcatf(estr, "</div>\n");

		estrConcatf(estr, "<div class=\"heading\">Versions:</div>");
		estrConcatf(estr, "<div class=\"versionrange\">\n");
		for(i=0; i<eaSize(&p->ppVersions); i++)
		{
			if(i)
				estrConcatf(estr, ", ");

			appendVersionLink(estr, p->ppVersions[i]);
		}
		estrConcatf(estr, "</div>\n");

		estrConcatf(estr, "<div class=\"heading\">Shard Info:</div>");
		estrConcatf(estr, "<div class=\"versionrange\">\n");
		for(i=0; i<eaSize(&p->ppShardInfoStrings); i++)
		{
			if(i)
				estrConcatf(estr, "<br>\n");

			estrConcatf(estr, "%s", p->ppShardInfoStrings[i]);
		}
		estrConcatf(estr, "</div>\n");

		if (p->pShardStartString)
		{
			estrConcatf(estr, "<div class=\"heading\">Shard Start Time:</div>");
			estrConcatf(estr, "<div class=\"versionrange\">%s</div>\n", p->pShardStartString);
		}
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_EXPRESSION))
	{

		// ---------------------------------------------------------------
		if(p->pExpression)
		{
			estrCopyWithHTMLEscaping(&pEscaped, p->pExpression, false);
			estrConcatf(estr, "<div class=\"heading\">Expression:</div>");
			estrConcatf(estr, "<div class=\"pExpression\">%s</div>\n", pEscaped);
		}

		if(p->pCategory)
		{
			estrConcatf(estr, "<div class=\"heading\">Category:</div>");
			estrConcatf(estr, "<div class=\"pExpression\">%s</div>\n", p->pCategory);
		}
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_ERRORSTRING))
	{
		if (p->pErrorSummary)
		{
			estrCopyWithHTMLEscaping(&pEscaped, p->pErrorSummary, false);
			estrConcatf(estr, "<div class=\"heading\">Summary:</div>\n"
				"<div class=\"pError\">%s</div>\n", pEscaped);
		}
		if(p->pErrorString)
		{
			if (p->eType == ERRORDATATYPE_GAMEBUG)
				estrConcatf(estr, "<div class=\"heading\">Description:</div>");
			else
				estrConcatf(estr, "<div class=\"heading\">Error Text:</div>");

			estrCopyWithHTMLEscaping(&pEscaped, p->pErrorString, false);
			estrConcatf(estr, "<div class=\"pError\">%s</div>\n", pEscaped);
		}

		if(p->pLargestMemory)
		{
			estrConcatf(estr, "<div class=\"heading\">Largest Memory Allocation:</div>");
			estrCopyWithHTMLEscaping(&pEscaped, p->pLargestMemory, false);
			estrConcatf(estr, "<div class=\"pLargestMemory\">%s</div>\n", pEscaped);
		}

		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_ERRORDETAILS))
	{
		TriviaOverviewItem *pItem = eaIndexedGetUsingString(&p->triviaOverview.ppTriviaItems, "details");
		if (p->eaErrorDetails || pItem)
		{
			int size = eaSize(&p->eaErrorDetails);
			estrConcatf(estr, "<div class=\"heading\">Details:</div><span class=\"smallnote\"><ul>");

			for (i=0; i<size; i++)
			{
				estrConcatf(estr, "<li>%d - %s</li>\n",
					i, p->eaErrorDetails[i]);
			}

			if (pItem)
			{
				size = eaSize(&pItem->ppValues);
				for (i=0; i<size; i++)
				{
					if(pItem->ppValues[i]->pVal)
					{
						estrConcatf(estr, "<li>%d - %s</li>\n",
							pItem->ppValues[i]->uCount,
							pItem->ppValues[i]->pVal);
					}
				}
			}
			estrConcatf(estr, "</ul></span>");
		}
	}


	if (!(iFlags & DUMPENTRY_FLAG_NO_CALLSTACK))
	{
		// ---------------------------------------------------------------
		if(ERRORDATATYPE_IS_A_CRASH(p->eType) || (ERRORDATATYPE_IS_A_ERROR(p->eType) && p->ppStackTraceLines))
		{
			// Show the whole callstack
			appendCallStackToString(p, estr);
		}
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_SOURCEFILE))
	{
		// ---------------------------------------------------------------
		if(p->pSourceFile)
		{
			estrConcatf(estr, "<div class=\"heading\">Source File / Line:</div>");
			estrConcatf(estr, "<div class=\"fileline\">%s (%d)</div>\n", p->pSourceFile, p->iSourceFileLine);
		}
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_DATAFILE))
	{
		if (p->pDataFile)
		{
			estrConcatf(estr, "<div class=\"heading\">Data File:</div>");
			estrConcatf(estr, "<div class=\"fileline\">%s</div>\n", p->pDataFile);
		}
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_PLATFORMS))
	{

		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Platforms:</div>");
		estrConcatf(estr, "<div class=\"ppPlatformCounts\">\n");
		for(i=0; i<eaSize(&p->ppPlatformCounts); i++)
		{
			estrConcatf(estr, "%s: %d<br>\n", getPlatformName(p->ppPlatformCounts[i]->ePlatform), p->ppPlatformCounts[i]->iCount);
		}
		estrConcatf(estr, "</div>\n");
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_PRODUCTS))
	{
		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Products:</div>");
		estrConcatf(estr, "<div class=\"ppProductNames\">\n");
		for(i=0; i<eaSize(&p->eaProductOccurrences); i++)
		{
			estrConcatf(estr, "%s (%u)<br>\n", p->eaProductOccurrences[i]->key, p->eaProductOccurrences[i]->uCount);
		}
		estrConcatf(estr, "</div>\n");
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_EXECUTABLES))
	{

		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Executables:</div>");
		estrConcatf(estr, "<div class=\"ppExecutableNames\">\n");
		if (p->ppExecutableCounts)
		{
			for(i=0; i<eaSize(&p->ppExecutableNames); i++)
			{
				estrConcatf(estr, "%s: %i<br>\n", p->ppExecutableNames[i], p->ppExecutableCounts[i]);
			}
		}
		else
		{
			for(i=0; i<eaSize(&p->ppExecutableNames); i++)
			{
				estrConcatf(estr, "%s<br>\n", p->ppExecutableNames[i]);
			}
		}
		estrConcatf(estr, "</div>\n");
		estrConcatf(estr, "<div class=\"heading\">Global Types:</div>");
		estrConcatf(estr, "<div class=\"ppExecutableNames\">\n");
		for(i=0; i<eaSize(&p->ppAppGlobalTypeNames); i++)
		{
			estrConcatf(estr, "%s<br>\n", p->ppAppGlobalTypeNames[i]);
		}
		estrConcatf(estr, "</div>\n");
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_USERS))
	{

		// ---------------------------------------------------------------
		if(p->bUnlimitedUsers)
			estrConcatf(estr, "<div class=\"heading\">Users: (unlimited cap)</div>");
		else
			estrConcatf(estr, "<div class=\"heading\">Users: (capped at %d entries)</div>", MAX_USERS_AND_IPS);
		estrConcatf(estr, "<div class=\"ppUserNames\">\n");
		for(i=0; i<eaSize(&p->ppUserInfo); i++)
		{
			estrConcatf(estr, "%s: %d<br>\n", p->ppUserInfo[i]->pUserName, p->ppUserInfo[i]->iCount);
		}
		estrConcatf(estr, "</div>\n");
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_IPS))
	{

		// ---------------------------------------------------------------
		if(p->bUnlimitedUsers)
			estrConcatf(estr, "<div class=\"heading\">IPs: (unlimited cap)</div>");
		else
			estrConcatf(estr, "<div class=\"heading\">IPs: (capped at %d entries)</div>", MAX_USERS_AND_IPS);
		estrConcatf(estr, "<div class=\"ppIPCounts\">\n");
		for(i=0; i<eaSize(&p->ppIPCounts); i++)
		{
			struct in_addr ina = {0};
			ina.S_un.S_addr = p->ppIPCounts[i]->uIP;
			estrConcatf(estr, "%s: %d<br>\n", inet_ntoa(ina), p->ppIPCounts[i]->iCount);
		}
		estrConcatf(estr, "</div>\n");
		// ---------------------------------------------------------------
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_DAYS_AGO))
	{
		bool bFirst = true;

		// ---------------------------------------------------------------
		estrConcatf(estr, "<div class=\"heading\">Days (days ago : count):</div>");
		estrConcatf(estr, "<div class=\"ppDayCounts\">\n(");
		iTotalDays = calcElapsedDays(p->uFirstTime, uTime);
		for(i=iTotalDays; i>=0; i--)
		{
			// Walking this timeline, by day:
			//           |--------|-------------|
			//           |        |             |
			//       uFirstTime   i<----      uTime
			//
			//      (uFirstTime+iTotalDays) = uTime

			int iDayCount =  findDayCount(p->ppDayCounts, i);
			int iDaysAgo  = (iTotalDays - i);

			if(iDayCount > 0)
			{
				if(!bFirst)
					estrConcatf(estr, ", ");

				estrConcatf(estr, "%d:%d", iDaysAgo, iDayCount);
				bFirst = false;
			}
		}
		estrConcatf(estr, ")</div>\n");
		// ---------------------------------------------------------------
	}

	if(!(iFlags & DUMPENTRY_FLAG_NO_DUMP_TOGGLES))
	{
		estrConcatf(estr, "<div class=\"heading\">Unlimited User Count</div>");
		estrConcatf(estr, "<div class=\"dumpreq\"><a href=\"/toggleunlimitedusers?id=%d\">%s</a><span class=\"hint\">(click to toggle)</span></div>\n", 
			p->uID,
			p->bUnlimitedUsers ? "yes" : "no");
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_PROGRAMMER_REQUEST) && ERRORDATATYPE_IS_A_CRASH(p->eType))
	{
		estrConcatf(estr, "<div class=\"heading\">Request Immediate Programmer Notification on Crash</div>");
		estrConcatf(estr, "<div class=\"dumpreq\"><a href=\"/flgimpcrash?id=%d&imp=%d\">%s</a> %s<span class=\"hint\">(click to toggle)</span></div>\n", 
			p->uID,
			p->bMustFindProgrammer ? 0 : 1,
			p->bMustFindProgrammer ? "yes" : "no",
			p->pProgrammerName ? STACK_SPRINTF("by %s ", p->pProgrammerName) : "");
	}
	if (!(iFlags & DUMPENTRY_FLAG_NO_PROGRAMMER_REQUEST) && ERRORDATATYPE_IS_A_ERROR(p->eType))
	{
		estrConcatf(estr, "<div class=\"heading\">Replace Callstack on Next Occurrence</div>");
		estrConcatf(estr, "<div class=\"dumpreq\"><a href=\"/flgcallstack?id=%d&val=%d\">%s</a><span class=\"hint\">(click to toggle)</span></div>\n", 
			p->uID,
			p->bReplaceCallstack ? 0 : 1,
			p->bReplaceCallstack ? "yes" : "no");
	}

	// ---------------------------------------------------------------
	estrConcatf(estr, "<div class=\"heading\">Suppress Error Reports:</div>");
	estrConcatf(estr, "<div class=\"dumpreq\"><a href=\"/suppresserror?id=%d&suppress=%d\">%s</a> <span class=\"hint\">(click to toggle)</span></div>\n", 
		p->uID,
		p->bSuppressErrorInfo ? 0 : 1,
		p->bSuppressErrorInfo ? "yes" : "no");
	// ---------------------------------------------------------------

	if (!(iFlags & DUMPENTRY_FLAG_NO_DUMPINFO))
	{
		// Dump information makes no sense for errors
		if(ERRORDATATYPE_IS_A_CRASH(p->eType) && (!(iFlags & DUMPENTRY_FLAG_NO_DUMP_TOGGLES)))
		{
			// ---------------------------------------------------------------
			estrConcatf(estr, "<div class=\"heading\">Full Dump Requested:</div>");
			estrConcatf(estr, "<div class=\"dumpreq\"><a href=\"/reqfulldump?id=%d&full=%d\">%s</a> <span class=\"hint\">(click to toggle)</span></div>\n", 
				p->uID,
				p->bFullDumpRequested ? 0 : 1,
				p->bFullDumpRequested ? "yes" : "no");
			// ---------------------------------------------------------------

			// ---------------------------------------------------------------
			estrConcatf(estr, "<div class=\"heading\">Block Future Dump Requests:</div>");
			estrConcatf(estr, "<div class=\"dumpreq\"><a href=\"/blockdumps?id=%d&block=%d\">%s</a> <span class=\"hint\">(click to toggle)</span></div>\n", 
				p->uID,
				p->bBlockDumpRequests ? 0 : 1,
				p->bBlockDumpRequests ? "yes" : "no");
			// ---------------------------------------------------------------

			// ---------------------------------------------------------------
			estrConcatf(estr, "<div class=\"heading\">Email notification (on new dump):</div>");
			formAppendStart(estr, "/dumpnotify", "GET", "dumpnotifyform", NULL);
			formAppendEdit(estr, 20, "email", "");
			formAppendHiddenInt(estr, "id", p->uID);
			formAppendSubmit(estr, "Add Email");
			formAppendEnd(estr);

			estrConcatf(estr, "<div class=\"heading\">Registered emails for this crash:</div>");

			if(eaSize(&p->ppDumpNotifyEmail) > 0)
			{
				for(i=0; i<eaSize(&p->ppDumpNotifyEmail); i++)
				{
					formAppendStart(estr, "/removedumpnotify", "GET", "removedumpnotifyform", NULL);
					estrConcatf(estr, "%s", p->ppDumpNotifyEmail[i]);
					formAppendHiddenInt(estr, "id", p->uID);
					formAppendHiddenInt(estr, "index", i);
					formAppendSubmit(estr, "Remove Email");
					formAppendEnd(estr);
				}
			}
			else
			{
				estrConcatf(estr, "-- None --<br>");
			}

			// ---------------------------------------------------------------
		}

		if (eaSize(&p->ppXperfDumps) > 0)
		{
			char dumpPath[MAX_PATH];
			estrConcatf(estr, "<div class=\"heading\">Xperf Dumps:</div>");
			estrConcatf(estr, "<div class=\"dumps\">\n");
			estrConcatf(estr, "<table border=1 cellspacing=0 cellpadding=3>\n");
			estrConcatf(estr, 
				"<tr>"
				"<td class=\"summaryheadtd\">Received</td>"
				"<td class=\"summaryheadtd\">Xperf Dump</td>"
				"</tr>"
				"\n");

			for(i=0; i<eaSize(&p->ppXperfDumps); i++)
			{
				GetErrorEntryDirDashes(strrchr(gErrorTrackerSettings.pDumpDir, '/'), p->uID, SAFESTR(dumpPath));

				timeMakeLocalDateStringFromSecondsSince2000(datetime, p->ppXperfDumps[i]->uTimeReceived);
				estrConcatf(estr, 
					"<tr>"
					"<td class=\"summarytd\">%s</td>\n"
					"<td class=\"summarytd\"><a href=\"%s-%s\">Download</a></td>\n"
					"</tr>\n",
					datetime, dumpPath, p->ppXperfDumps[i]->filename);
			}
			estrConcatf(estr, "</table>\n");
			estrConcatf(estr, "</div>\n");
		}

		// ---------------------------------------------------------------
		if (eaSize(&p->ppMemoryDumps) > 0)
		{
			char dumpPath[MAX_PATH];
			estrConcatf(estr, "<div class=\"heading\">Memory Dumps:</div>");
			estrConcatf(estr, "<div class=\"dumps\">\n");

			estrConcatf(estr, "<table border=1 cellspacing=0 cellpadding=3>\n");

			estrConcatf(estr, 
				"<tr>"
				"<td class=\"summaryheadtd\">ID</td>\n"
				"<td class=\"summaryheadtd\">Received</td>"
				"<td class=\"summaryheadtd\">Memory Dump</td>"
				"</tr>"
				"\n");

			for(i=0; i<eaSize(&p->ppMemoryDumps); i++)
			{
				GetErrorEntryDirDashes(strrchr(gErrorTrackerSettings.pDumpDir, '/'), p->uID, SAFESTR(dumpPath));

				timeMakeLocalDateStringFromSecondsSince2000(datetime, p->ppMemoryDumps[i]->uTimeReceived);

				estrConcatf(estr, 
					"<tr>"
					"<td class=\"summarytd\">%d</td>\n"
					"<td class=\"summarytd\">%s</td>\n"
					"<td class=\"summarytd\"><a href=\"%s-memdmp_%d.txt\">Download</a></td>\n"
					"</tr>\n",
					p->ppMemoryDumps[i]->iDumpIndex,
					datetime, dumpPath, p->ppMemoryDumps[i]->iDumpIndex);
			}
			estrConcatf(estr, "</table>\n");
			estrConcatf(estr, "</div>\n");
		}

		if(eaSize(&p->ppDumpData) > 0)
		{
			estrConcatf(estr, "<div class=\"heading\">Dumps:</div>");
			estrConcatf(estr, "<div class=\"dumps\">\n");

			estrConcatf(estr, "<table border=1 cellspacing=0 cellpadding=3>\n");

			estrConcatf(estr, 
				"<tr>"
				"<td class=\"summaryheadtd\">Dump</td>\n"
				"<td class=\"summaryheadtd\">Version</td>"
				"<td class=\"summaryheadtd\">Date / Time</td>\n"
				"<td class=\"summaryheadtd\">User</td>\n"
				"<td class=\"summaryheadtd\">IP</td>\n"
				"<td class=\"summaryheadtd\">Platform</td>"
				"<td class=\"summaryheadtd\">Executable</td>"
				"<td class=\"summaryheadtd\">User Comment</td>"
				"<td class=\"summaryheadtd\">Received</td>"
				"</tr>"
				"\n");

			for(i=0; i<eaSize(&p->ppDumpData); i++)
			{
				const char *pDumpPrefix = (p->ppDumpData[i]->uFlags & DUMPDATAFLAGS_FULLDUMP) ? "Full" : "Mini";
				if (p->ppDumpData[i]->uFlags & DUMPDATAFLAGS_MOVED)
				{
					estrConcatf(estr, "<tr class=\"tr_dumpmoved\">"
						"<td class=\"summarytd\"><a href=\"dumpinfo?id=%d&index=%d&\">%s Dump</a></td>\n"
						"<td class=\"summarytd\" colspan=7>%s</td>\n"
						"</tr>",
						p->uID, i, pDumpPrefix,
						"Dump Moved");
				}
				else
				{
					const char *pUserName = eaSize(&p->ppDumpData[i]->pEntry->ppUserInfo) > 0 
						? p->ppDumpData[i]->pEntry->ppUserInfo[0]->pUserName 
						: "???";

					const char *pPlatform = eaSize(&p->ppDumpData[i]->pEntry->ppPlatformCounts) > 0
						? getPlatformName(p->ppDumpData[i]->pEntry->ppPlatformCounts[0]->ePlatform)
						: "??";

					const char *pEXE      = eaSize(&p->ppDumpData[i]->pEntry->ppExecutableNames) > 0
						? p->ppDumpData[i]->pEntry->ppExecutableNames[0]
					: "??";
					const char *pDumpCSSClass = "tr_dumpnone";
					struct in_addr ina = {0};
					if (p->ppDumpData[i]->pEntry->ppIPCounts && p->ppDumpData[i]->pEntry->ppIPCounts[0])
						ina.S_un.S_addr = p->ppDumpData[i]->pEntry->ppIPCounts[0]->uIP;
					else
						ErrorOrAlert("NoIPInformation", "An ErrorEntry was accessed with no IP data. ETID #%d, dump #%d", p->uID, i);

					if ((p->ppDumpData[i]->uFlags & DUMPDATAFLAGS_FULLDUMP) && p->ppDumpData[i]->bWritten)
						pDumpCSSClass = "tr_dumpfull";
					else if (p->ppDumpData[i]->bWritten || p->ppDumpData[i]->bMiniDumpWritten)
						pDumpCSSClass = "tr_dumpmini";

					timeMakeLocalDateStringFromSecondsSince2000(datetime, p->ppDumpData[i]->pEntry->uFirstTime);

					estrConcatf(estr, "<tr class=\"%s\">"
						"<td class=\"summarytd\"><a href=\"dumpinfo?id=%d&index=%d&\">%s Dump</a></td>\n"
						"<td class=\"summarytd\">%s</td>\n"
						"<td class=\"summarytd\">%s</td>\n"
						"<td class=\"summarytd\">%s</td>\n"
						"<td class=\"summarytd\">%s</td>\n"
						"<td class=\"summarytd\">%s</td>\n"
						"<td class=\"summarytd\">%s</td>\n"
						"<td class=\"summarytd\">%s</td>\n", 
						pDumpCSSClass,
						p->uID, i, pDumpPrefix,
						(eaSize(&p->ppDumpData[i]->pEntry->ppVersions) > 0) ? p->ppDumpData[i]->pEntry->ppVersions[0] : "---",
						datetime,
						pUserName, inet_ntoa(ina), pPlatform, pEXE, 
						(p->ppDumpData[i]->pDumpDescription) ? p->ppDumpData[i]->pDumpDescription : "--");
					if (p->ppDumpData[i]->uFlags & DUMPDATAFLAGS_DELETED)
						estrConcatf(estr, "<td class=\"summarytd\">Deleted</td>\n"
						"</tr>");
					else if (p->ppDumpData[i]->uFlags & DUMPDATAFLAGS_REJECTED)
						estrConcatf(estr, "<td class=\"summarytd\">Rejected</td>\n"
						"</tr>");
					else
						estrConcatf(estr, "<td class=\"summarytd\">%s%s</td>\n"
						"</tr>",
						p->ppDumpData[i]->bWritten ? "Yes" : (p->ppDumpData[i]->bCancelled ? "Cancelled" : "No"), 
						p->ppDumpData[i]->bMiniDumpWritten ? " - Minidump Available" : "");
				}
			}

			estrConcatf(estr, "</table>\n");

			estrConcatf(estr, "</div>\n");
		}

		// ---------------------------------------------------------------

		if(eaSize(&p->ppRecentErrors) > 0)
		{
			estrConcatf(estr, "<div class=\"heading\">Recent Errors:</div>");
			estrConcatf(estr, "<div class=\"dumps\">\n");

			estrConcatf(estr, "<table border=1 cellspacing=0 cellpadding=3>\n");

			estrConcatf(estr, 
				"<tr>"
				"<td class=\"summaryheadtd\">Error</td>\n"
				"<td class=\"summaryheadtd\">Version</td>"
				"<td class=\"summaryheadtd\">Date / Time</td>\n"
				"<td class=\"summaryheadtd\">User</td>\n"
				"<td class=\"summaryheadtd\">IP</td>\n"
				"<td class=\"summaryheadtd\">Platform</td>"
				"<td class=\"summaryheadtd\">Executable</td>"
				"</tr>"
				"\n");

			for(i=0; i<eaSize(&p->ppRecentErrors); i++)
			{

				const char *pUserName = eaSize(&p->ppRecentErrors[i]->ppUserInfo) > 0
					? p->ppRecentErrors[i]->ppUserInfo[0]->pUserName 
					: "???";

				const char *pPlatform = eaSize(&p->ppRecentErrors[i]->ppPlatformCounts) > 0
					? getPlatformName(p->ppRecentErrors[i]->ppPlatformCounts[0]->ePlatform)
					: "??";

				const char *pEXE      = eaSize(&p->ppRecentErrors[i]->ppExecutableNames) > 0
					? p->ppRecentErrors[i]->ppExecutableNames[0]
				: "??";
				const char *pDumpCSSClass = "tr_dumpinfo";
				struct in_addr ina = {0};
				ina.S_un.S_addr = p->ppRecentErrors[i]->ppIPCounts[0]->uIP;

				timeMakeLocalDateStringFromSecondsSince2000(datetime, p->ppRecentErrors[i]->uFirstTime);

				estrConcatf(estr, "<tr class=\"%s\">"
					"<td class=\"summarytd\"><a href=\"errorinfo?id=%d&index=%d&\">Error Info</a></td>\n"
					"<td class=\"summarytd\">%s</td>\n"
					"<td class=\"summarytd\">%s</td>\n"
					"<td class=\"summarytd\">%s</td>\n"
					"<td class=\"summarytd\">%s</td>\n"
					"<td class=\"summarytd\">%s</td>\n"
					"<td class=\"summarytd\">%s</td>\n", 
					pDumpCSSClass,
					p->uID, i,
					(eaSize(&p->ppRecentErrors[i]->ppVersions) > 0) ? p->ppRecentErrors[i]->ppVersions[0] : "---",
					datetime,
					pUserName, inet_ntoa(ina), pPlatform, pEXE);
			}

			estrConcatf(estr, "</table>\n");

			estrConcatf(estr, "</div>\n");
		}
	}

	estrConcatf(estr, "<div class=\"heading\">All user comments:</div>");

	if (eaSize(&p->ppCommentList) > 0) {
		if (iFlags & DUMPENTRY_FLAG_NO_COMMENTS)
			estrConcatf(estr, "-- Comments Hidden --<br>");
		else
			appendCommentValues(estr, p->ppCommentList);
	} else {
		estrConcatf(estr, "-- None --<br>");
	}

	if (p->eType == ERRORDATATYPE_GAMEBUG)
	{
		// Does nothing
	}
	else if (iFlags & DUMPENTRY_FLAG_FORCE_TRIVIASTRINGS )
	{
		appendTriviaStrings(estr, (TriviaData**) p->ppTriviaData);
	}
	else if ((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_WRITE_TRIVIA_DISK && p->pTriviaFilename) 
		|| (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_LOG_TRIVIA)
		|| p->triviaOverview.ppTriviaItems)
	{
		estrConcatf(estr, "<div class=\"heading\"><a href=\"/detail_trivia?id=%d\">Trivia Data</a></div>\n",
			p->uID);
	}

	if(!(iFlags & DUMPENTRY_FLAG_NO_DUMP_TOGGLES))
	{
		estrConcatf(estr, "<div class=\"heading\">Remove Entry (removes hash so no more instances will be merged into this)</div>");
		estrConcatf(estr, "<div class=\"dumpreq\"><a href=\"/removehash?id=%d\">click to delete</a>(not reversible)</div>\n", 
			p->uID);
	}
	estrConcatf(estr, "</div>\n");

	estrConcatf(estr, "\n<br><hr size=1>\n<br>\n");
}

// -----------------------------------------------------------------------------------------------

// No HTML!
void appendTriviaStringTextOnly(char **estr, CONST_EARRAY_OF(TriviaData) ppTriviaData)
{
	int i, size = eaSize(&ppTriviaData);
	if (size <= 0)
		return;

	estrConcatf(estr, "\n  Trivia Values (key,value):\n");
	for (i=0; i<size; i++)
	{
		estrConcatf(estr, "      %s, <&%s&>\n", ppTriviaData[i]->pKey, ppTriviaData[i]->pVal);
	}
}

void ET_DumpEntryToString(char **estr, ErrorEntry *pEntry, U32 iFlags, bool returnFullEntry)
{
	int i;
	U32 uTime = timeSecondsSince2000();
	int iNumUsers = eaSize(&pEntry->ppUserInfo);
	int iTotalDays;

	estrConcatf(estr, "\n");
	if (!(iFlags & DUMPENTRY_FLAG_NO_TYPE))
	{
		estrConcatf(estr, "Type '%s'", ErrorDataTypeToString(pEntry->eType));
	}
	if (!(iFlags & DUMPENTRY_FLAG_NO_UID))
	{
		estrConcatf(estr, "[%d]", pEntry->uID);
	}
	if (!(iFlags & DUMPENTRY_FLAG_NO_HTTP))
	{
		estrConcatf(estr, " http://%s/detail?id=%d", getMachineAddress(), pEntry->uID);
	}
	estrConcatf(estr, "\n");

	if (!(iFlags & DUMPENTRY_FLAG_NO_JIRA))
	{
		estrConcatf(estr, "Jira Issue: ");
		if (pEntry->pJiraIssue)
		{
			estrConcatf(estr, "http://%s:%d/browse/%s (%s)", getJiraHost(), getJiraPort(), 
				pEntry->pJiraIssue->key, (pEntry->pJiraIssue->assignee) ? pEntry->pJiraIssue->assignee : "unknown");
		}
		else
		{
			estrConcatf(estr, "No Jira Entry");
		}
	}
	estrConcatf(estr, "\n");

	if (!returnFullEntry) 
		return;

	if (!(iFlags & DUMPENTRY_FLAG_NO_TOTALCOUNT))
	{
		estrConcatf(estr, "  %d occurrences - ", pEntry->iTotalCount);
	}
	if (!(iFlags & DUMPENTRY_FLAG_NO_USERSCOUNT))
	{
		estrConcatf(estr, "(%d users) - ", eaSize(&pEntry->ppUserInfo));
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_DAYS_AGO))
	{
		estrConcatf(estr, "days ago: (");
		iTotalDays = calcElapsedDays(pEntry->uFirstTime, uTime);
		for(i=iTotalDays; i>=0; i--)
		{
			// Walking this timeline, by day:
			//           |---------------|------|
			//           |               |      |
			//       uFirstTime      --->i    uTime
			//
			//      (uFirstTime+iTotalDays) = uTime

			int iDayCount =  findDayCount(pEntry->ppDayCounts, i);
			int iDaysAgo  = (iTotalDays - i);

			estrConcatf(estr, "%d:%d", iDaysAgo, iDayCount);

			if (iDaysAgo >= 20) {
				estrConcatf(estr, "...");
				break;
			}

			if(i)
				estrConcatf(estr, ", ");
		}

		estrConcatf(estr, ")");
	}
	estrConcatf(estr, "\n");

	if(pEntry->pLastBlamedPerson && pEntry->pDataFile)
	{
		if (!(iFlags & DUMPENTRY_FLAG_NO_BLAMEE))
		{
			estrConcatf(estr, "  (%s) ", pEntry->pLastBlamedPerson);
		}
		if (!(iFlags & DUMPENTRY_FLAG_NO_DATAFILE))
		{
			estrConcatf(estr, "%s", pEntry->pDataFile);
		}
		estrConcatf(estr, "\n");
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_EXPRESSION))
	{
		if(pEntry->pExpression)
		{
			estrConcatf(estr, "  Expression: %s\n", pEntry->pExpression);
		}
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_ERRORSTRING))
	{
		if(pEntry->pErrorString)
		{
			char *tmp = NULL;
			estrCreate(&tmp);
			estrCopy2(&tmp, pEntry->pErrorString);
			if (!(iFlags & DUMPENTRY_FLAG_ALLOW_NEWLINES_IN_ERRORSTRING))
			{
				estrTruncateAtFirstOccurrence(&tmp, '\n');
			}
			estrConcatf(estr, "  Error: %s\n", tmp);
			estrDestroy(&tmp);
		}
	}

	if(ERRORDATATYPE_IS_A_CRASH(pEntry->eType))
	{
		ErrorEntry *pMergedEntry = findErrorTrackerByID(pEntry->uID);

		if (pMergedEntry)
		{

			bool bHasBlameInfo = hasValidBlameInfo(pMergedEntry);

			if (!(iFlags & DUMPENTRY_FLAG_NO_CALLSTACK))
			{
				estrConcatf(estr, "  Callstack:\n");
				for(i=0; i<eaSize(&pMergedEntry->ppStackTraceLines); i++)
				{
					const char *pBlamedPerson;
					char daysAgoBuffer[12];
					const char *pModuleName = getPrintableModuleName(pMergedEntry, pMergedEntry->ppStackTraceLines[i]->pModuleName);

					if(bHasBlameInfo && pMergedEntry->ppStackTraceLines[i]->pBlamedPerson)
					{
						pBlamedPerson = pMergedEntry->ppStackTraceLines[i]->pBlamedPerson;
						sprintf(daysAgoBuffer, "%d", calcElapsedDays(pMergedEntry->ppStackTraceLines[i]->uBlamedRevisionTime, uTime));
					}
					else
					{
						pBlamedPerson = "Unknown";
						sprintf(daysAgoBuffer, "???");
					}


					//pEntry->ppStackTraceLines[i]->pBlamedPerson

					estrConcatf(estr, "  %13s %4s %s!%s() - %s (%d)\n",
						pBlamedPerson,
						daysAgoBuffer,
						pModuleName,
						pMergedEntry->ppStackTraceLines[i]->pFunctionName,
						pMergedEntry->ppStackTraceLines[i]->pFilename,
						pMergedEntry->ppStackTraceLines[i]->iLineNum);
				}
			}
		}
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_EXECUTABLES))
	{
		estrConcatf(estr, "  Executables:\n");

		for (i=0; i<eaSize(&pEntry->ppExecutableNames); i++)
		{
			estrConcatf(estr, "    * %s\n", pEntry->ppExecutableNames[i]);
		}
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_USERS))
	{
		int size = eaSize(&pEntry->ppUserInfo);
		estrConcatf(estr, "  Users: (");
		for (i=size-1; i>=0 && i >= size-MAX_USERS_TO_SHOW; i--)
		{
			if(i<size-1)
				estrConcatf(estr, ", ");

			estrConcatf(estr, "%s (%d)", pEntry->ppUserInfo[i]->pUserName, pEntry->ppUserInfo[i]->iCount);
		}
		if (size > MAX_USERS_TO_SHOW)
			estrConcatf(estr, ") and %d more\n", size-MAX_USERS_TO_SHOW);
		else
			estrConcatf(estr, ")\n");
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_DUMPINFO))
	{
		if(pEntry->iMiniDumpCount > 0)
		{
			estrConcatf(estr, "  %d minidumps available.\n", pEntry->iMiniDumpCount);
		}

		if(pEntry->iFullDumpCount > 0)
		{
			estrConcatf(estr, "  %d fulldumps available.\n", pEntry->iFullDumpCount);
		}
	}
	if (iFlags & DUMPENTRY_FLAG_FORCE_TRIVIASTRINGS)
	{
		appendTriviaStringTextOnly(estr, (TriviaData**) pEntry->ppTriviaData);
	}

	if (!(iFlags & DUMPENTRY_FLAG_NO_ERRORDETAILS))
	{
		if (pEntry->eaErrorDetails) // look for earray
		{
			int size = eaSize(&pEntry->eaErrorDetails);
			estrConcatf(estr, "\n  Error Details:\n");
			for (i=0; i<size; i++)
				estrConcatf(estr, "      %s\n", pEntry->eaErrorDetails[i]);
		}
		else // legacy: check trivia data
		{
			TriviaData *data = eaIndexedGetUsingString(&((TriviaData**) pEntry->ppTriviaData), "details");
			if (data) // should be a redundant NULL check
			{
				estrConcatf(estr, "\n  Trivia Details (key,value):\n");
				estrConcatf(estr, "      %s, <&%s&>\n", data->pKey, data->pVal);
			}
		}
	}

	estrConcatf(estr, "\n");
}
