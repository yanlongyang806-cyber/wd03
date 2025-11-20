#include "TestServerHttp.h"
#include "clearsilver.h"
#include "error.h"
#include "EString.h"
#include "file.h"
#include "fileutil.h"
#include "GenericHttpServing.h"
#include "GlobalTypes.h"
#include "HttpLib.h"
#include "httputil.h"
#include "net.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TestServerExpression.h"
#include "TestServerGlobal.h"
#include "TestServerIntegration.h"
#include "TestServerLua.h"
#include "TestServerMetric.h"
#include "TestServerReport.h"
#include "timing.h"
#include "windefinclude.h"

#include "TestServerHttp_h_ast.h"

static int giTestServerHttpPort = 8281; // 91^2!
static S64 siLastPrintf = 0;
static TestServerConsoleLine **sppConsoleBuffer = NULL;
static CRITICAL_SECTION cs_sConsoleBuffer;

static char *spcLastScript = NULL;
static char *spcSavedScript = NULL;

#define render(tmpl, parse) renderEx(req, tmpl, data, parse)

static void renderEx(HttpRequest *req, const char *tmpl, void *data, void *parse)
{
	char *estr = renderTemplate(tmpl, parse, data, hrFindBool(req, "hdfdump"));
	httpSendStr(req->link, estr);
	estrDestroy(&estr);
	StructDestroyVoid(parse, data);
}

static void TestServer_WebMain(HttpRequest *req)
{
	TestServerWebMain *data;

	if(req->method == HTTPMETHOD_POST)
	{
		httpRedirect(req->link, "/main");
		return;
	}

	data = StructCreate(parse_TestServerWebMain);
	estrPrintf(&data->title, "Test Server Web Interface");
	render("main.cs", parse_TestServerWebMain);
}

static void TestServer_Status(HttpRequest *req)
{
	TestServerStatus *data;

	if(req->method == HTTPMETHOD_POST)
	{
		httpRedirect(req->link, "/status");
		return;
	}

	data = StructCreate(parse_TestServerStatus);

	estrPrintf(&data->title, "TestServer Status");
	TestServerStatus_LuaPopulate(data);
	estrCopy(&data->pcLastScript, &spcLastScript);
	render("status.cs", parse_TestServerStatus);
}

static void TestServer_SubStatus(HttpRequest *req)
{
	TestServerStatus *data;
	
	if(req->method == HTTPMETHOD_POST)
	{
		httpRedirect(req->link, "/sub_status");
		return;
	}

	data = StructCreate(parse_TestServerStatus);

	estrPrintf(&data->title, "TestServer Status");
	TestServerStatus_LuaPopulate(data);
	render("substatus.cs", parse_TestServerStatus);
}

static void TestServer_ConsolePage(HttpRequest *req)
{
	TestServerConsolePage *data;

	if(req->method == HTTPMETHOD_POST)
	{
		httpRedirect(req->link, "/console");
		return;
	}

	data = StructCreate(parse_TestServerConsolePage);
	estrPrintf(&data->title, "Test Server Console");
	render("console.cs", parse_TestServerConsolePage);
}

static void TestServer_GetConsoleContents(HttpRequest *req)
{
	TestServerConsoleLines *data = NULL;
	const char *pcClearStamp;
	S64 iStamp = 0, iClearStamp = 0;
	char *pcSendStr = NULL;
	int i;

	if(req->method != HTTPMETHOD_POST)
	{
		httpRedirect(req->link, "/console");
		return;
	}

	if((pcClearStamp = hrFindCookie(req, "Clear timestamp")))
	{
		StringToInt_Multisize_AssumeGoodInput(pcClearStamp, &iClearStamp);
	}

	iStamp = hrFindInt64(req, "stamp", 0);

	data = StructCreate(parse_TestServerConsoleLines);
	data->iTimestamp = MAX(iClearStamp, iStamp);

	EnterCriticalSection(&cs_sConsoleBuffer);
	if(siLastPrintf > data->iTimestamp)
	{
		for(i = 0; i < eaSize(&sppConsoleBuffer) - 1; ++i)
		{
			TestServerConsoleLine *pLine = sppConsoleBuffer[i];

			if(pLine->iTimestamp > MAX(iClearStamp, iStamp))
			{
				eaPush(&data->ppLines, pLine);
				data->iTimestamp = pLine->iTimestamp;
			}
		}
	}
	LeaveCriticalSection(&cs_sConsoleBuffer);

	ParserWriteXML(&pcSendStr, parse_TestServerConsoleLines, data);
	eaDestroy(&data->ppLines);
	StructDestroy(parse_TestServerConsoleLines, data);

	httpSendStr(req->link, pcSendStr);
	estrDestroy(&pcSendStr);
}

char *TestServer_ViewToHtml(TestServerView *pView)
{
	return renderTemplate("view.cs", parse_TestServerView, pView, false);
}

static FileScanAction TestServer_ListSavedView(char *dir, struct _finddata32_t *data, TestServerViewList *pViewList)
{
	char *fn = strdup(data->name);
	int len = (int)strlen(fn);
	TestServerViewListItem *pItem;

	if(!strEndsWith(fn, ".view") || len <= 25)
	{
		return FSA_EXPLORE_DIRECTORY;
	}

	pItem = StructCreate(parse_TestServerViewListItem);
	estrPrintf(&pItem->fn, "%s/%s", dir, fn);
	estrReplaceOccurrences(&pItem->fn, "server/TestServer/views/", "");

	fn[len-5] = 0; // chop off .view
	fn[4] = '-'; // 2009-
	fn[7] = '-'; // 12-
	fn[10] = ' '; // 29_
	fn[13] = ':'; // 08:
	fn[16] = ':'; // 39:
	fn[19] = 0; // chop date
	pItem->date = estrDup(fn);
	pItem->name = estrDup(&fn[20]);
	estrReplaceOccurrences(&pItem->name, "_", " ");
	
	free(fn);
	eaPush(&pViewList->ppItems, pItem);
	return FSA_EXPLORE_DIRECTORY;
}

static void TestServer_SavedViewList(HttpRequest *req)
{
	TestServerViewList *data = StructCreate(parse_TestServerViewList);
	fileScanAllDataDirs("server/TestServer/views", TestServer_ListSavedView, data);
	estrPrintf(&data->title, "Saved Reports");
	render("savedlist.cs", parse_TestServerViewList);
}

static void TestServer_View(HttpRequest *req)
{
	TestServerView *data;
	const char *pcScope = urlFindValue(req->vars, "scope");
	const char *pcName = urlFindSafeValue(req->vars, "name");

	if(req->method == HTTPMETHOD_POST)
	{
		httpRedirect(req->link, "/status");
		return;
	}

	if(!pcName[0])
	{
		data = TestServer_ScopedGlobalsToView(pcScope, false);

		if(data->pcScope)
		{
			estrPrintf(&data->title, "Variables in scope: %s", data->pcScope);
		}
		else
		{
			estrPrintf(&data->title, "Variables in global scope");
		}
	}
	else
	{
		data = TestServer_GlobalToView(pcScope, pcName, false);

		if(data->pcScope)
		{
			estrPrintf(&data->title, "Global variable: (%s) %s", data->pcScope, data->pcName);
		}
		else
		{
			estrPrintf(&data->title, "Global variable: %s", data->pcName);
		}
	}

	data->bAllowModify = true;
	render("globals.cs", parse_TestServerView);
	return;
}

static void TestServer_SavedView(HttpRequest *req)
{
	TestServerView *data;
	const char *pchViewName = urlFindSafeValue(req->vars, "name");

	if(req->method == HTTPMETHOD_POST)
	{
		httpRedirect(req->link, "/status");
		return;
	}

	if(!pchViewName || !pchViewName[0])
	{
		TestServer_SavedViewList(req);
		return;
	}

	data = StructCreate(parse_TestServerView);

	if(!ParserReadTextFile(STACK_SPRINTF("server/TestServer/views/%s", pchViewName), parse_TestServerView, data, 0))
	{
		StructDestroy(parse_TestServerView, data);
		httpRedirect(req->link, "/saved");
		return;
	}

	render("view.cs", parse_TestServerView);
}

static FileScanAction TestServer_ListScript(char *dir, struct _finddata32_t *data, TestServerScriptList *pScriptList)
{
	char *fn, *short_fn, *temp;
	int depth = 0;
	TestServerScriptListItem *pItem;

	if(!stricmp(data->name, "modules"))
	{
		return FSA_NO_EXPLORE_DIRECTORY;
	}

	pItem = StructCreate(parse_TestServerScriptListItem);
	estrPrintf(&pItem->fn, "%s/%s", dir, data->name);
	estrReplaceOccurrences(&pItem->fn, "server/TestServer/scripts/", "");
	fn = strdup(pItem->fn);
	short_fn = fn;

	while((temp = strchr(short_fn, '/')))
	{
		short_fn = temp+1;
		++depth;
	}

	pItem->depth = depth;

	if(!(temp = strchr(short_fn, '.')))
	{
		pItem->dir = true;
	}
	else if(!strEndsWith(temp, ".lua"))
	{
		return FSA_NO_EXPLORE_DIRECTORY;
	}
	else
	{
		*temp = 0;
	}

	estrPrintf(&pItem->name, "%s", short_fn);
	free(fn);
	eaPush(&pScriptList->ppItems, pItem);
	return FSA_EXPLORE_DIRECTORY;
}

static void TestServer_ScriptList(HttpRequest *req)
{
	TestServerScriptList *data = StructCreate(parse_TestServerScriptList);
	fileScanAllDataDirs("server/TestServer/scripts", TestServer_ListScript, data);
	estrPrintf(&data->title, "Test Server Scripts");
	render("scriptlist.cs", parse_TestServerScriptList);
}

static void TestServer_RunOrListScripts(HttpRequest *req)
{
	const char *pcScript = urlFindSafeValue(req->vars, "name");
	int iNow = 0;

	if(req->method == HTTPMETHOD_POST)
	{
		httpRedirect(req->link, "/run");
		return;
	}

	if(!pcScript || !pcScript[0])
	{
		TestServer_ScriptList(req);
		return;
	}

	urlFindBoundedInt(req->vars, "now", &iNow, 0, 1);

	if(iNow == 1)
	{
		if(!stricmp(pcScript, "raw chunk"))
		{
			TestServer_RunScriptRawNow(spcSavedScript);
		}
		else
		{
			TestServer_RunScriptNow(pcScript);
		}

		estrCopy2(&spcLastScript, pcScript);
	}
	else
	{
		if(!stricmp(pcScript, "raw chunk"))
		{
			TestServer_RunScriptRaw(spcSavedScript);
		}
		else
		{
			TestServer_RunScript(pcScript);
		}

		estrCopy2(&spcLastScript, pcScript);
	}

	httpRedirect(req->link, "/status");
}

static void TestServer_LuaWebInterface(HttpRequest *req)
{
	TestServerLuaWebInfo *data;

	if(req->method == HTTPMETHOD_POST)
	{
		// Got a post request containing a script to run
		const char *pcScript = NULL;
		const char *pcRun = NULL;

		pcScript = urlFindSafeValue(req->vars, "lua");
		estrPrintf(&spcSavedScript, "%s", pcScript);

		pcRun = urlFindSafeValue(req->vars, "submit");

		if(!stricmp(pcRun, "Run"))
		{
			TestServer_RunScriptRaw(pcScript);
			estrCopy2(&spcLastScript, "raw chunk");
		}
		else if(!stricmp(pcRun, "Verify"))
		{
			TestServer_VerifyScriptRaw(pcScript);
			estrCopy2(&spcLastScript, "raw chunk");
		}

		httpRedirect(req->link, "/lua");
		return;
	}

	data = StructCreate(parse_TestServerLuaWebInfo);
	estrPrintf(&data->title, "TestServer Lua Interface");
	estrPrintf(&data->script, "%s", spcSavedScript ? spcSavedScript : "");

	render("luaweb.cs", parse_TestServerLuaWebInfo);
}

static void TestServer_ClearGlobalRequest(HttpRequest *req)
{
	const char *pcScope = urlFindValue(req->vars, "scope");
	const char *pcName = urlFindSafeValue(req->vars, "name");
	int iPos = -1;

	urlFindInt(req->vars, "pos", &iPos);

	if(req->method == HTTPMETHOD_POST && pcName[0])
	{
		TestServer_ClearGlobalValue(pcScope, pcName, iPos);
	}

	httpRedirect(req->link, "/view");
}

static void TestServer_SetGlobalRequest(HttpRequest *req)
{
	const char *pcScope = urlFindValue(req->vars, "scope");
	const char *pcName = urlFindSafeValue(req->vars, "name");
	const char *pcLabel = urlFindValue(req->vars, "label");
	const char *pcVal = urlFindSafeValue(req->vars, "value");
	const char *pcVal2 = urlFindSafeValue(req->vars, "value2");
	int iPos = -1, iPersist = 0, eType = 0;
	int iFoundType = urlFindBoundedInt(req->vars, "type", &eType, TSGV_Integer, TSGV_Global);
	int iFoundPersist = urlFindBoundedInt(req->vars, "persist", &iPersist, 0, 1);
	int iVal;
	float fVal;

	if(req->method != HTTPMETHOD_POST || !pcName[0] || iFoundType != 1)
	{
		httpRedirect(req->link, "/view");
		return;
	}

	if(!pcVal[0] && !pcVal2[0])
	{
		httpRedirect(req->link, "/view");
		return;
	}
	
	urlFindInt(req->vars, "pos", &iPos);

	switch(eType)
	{
	case TSGV_Integer:
		if(pcVal && StringToInt(pcVal, &iVal))
		{
			TestServer_SetGlobal_Integer(pcScope, pcName, iPos, iVal);
		}
	xcase TSGV_Boolean:
		if(pcVal && StringToInt(pcVal, &iVal))
		{
			TestServer_SetGlobal_Boolean(pcScope, pcName, iPos, iVal!=0);
		}
	xcase TSGV_Float:
		if(pcVal && StringToFloat(pcVal, &fVal))
		{
			TestServer_SetGlobal_Float(pcScope, pcName, iPos, fVal);
		}
	xcase TSGV_String:
		TestServer_SetGlobal_String(pcScope, pcName, iPos, pcVal);
	xcase TSGV_Password:
		TestServer_SetGlobal_Password(pcScope, pcName, iPos, pcVal);
	xcase TSGV_Global:
		TestServer_SetGlobal_Ref(pcScope, pcName, iPos, pcVal, pcVal2);
	xdefault:
		break;
	}

	TestServer_SetGlobalValueLabel(pcScope, pcName, iPos, pcLabel);

	if(iFoundPersist == 1)
	{
		TestServer_PersistGlobal(pcScope, pcName, iPersist);
	}

	httpRedirect(req->link, "/view");
}

static void TestServer_InsertGlobalRequest(HttpRequest *req)
{
	const char *pcScope = urlFindValue(req->vars, "scope");
	const char *pcName = urlFindSafeValue(req->vars, "name");
	const char *pcLabel = urlFindValue(req->vars, "label");
	const char *pcVal = urlFindSafeValue(req->vars, "value");
	const char *pcVal2 = urlFindSafeValue(req->vars, "value2");
	int iPos = -1, iPersist = 0, eType = 0;
	int iFoundType = urlFindBoundedInt(req->vars, "type", &eType, TSGV_Integer, TSGV_Global);
	int iFoundPersist = urlFindBoundedInt(req->vars, "persist", &iPersist, 0, 1);
	int iVal;
	float fVal;

	if(req->method != HTTPMETHOD_POST || !pcName[0] || iFoundType != 1)
	{
		httpRedirect(req->link, "/view");
		return;
	}

	if(!pcVal[0] && !pcVal2[0])
	{
		httpRedirect(req->link, "/view");
		return;
	}

	urlFindInt(req->vars, "pos", &iPos);

	switch(eType)
	{
	case TSGV_Integer:
		if(pcVal && StringToInt(pcVal, &iVal))
		{
			TestServer_InsertGlobal_Integer(pcScope, pcName, iPos, iVal);
		}
	xcase TSGV_Boolean:
		if(pcVal && StringToInt(pcVal, &iVal))
		{
			TestServer_InsertGlobal_Boolean(pcScope, pcName, iPos, iVal!=0);
		}
	xcase TSGV_Float:
		if(pcVal && StringToFloat(pcVal, &fVal))
		{
			TestServer_InsertGlobal_Float(pcScope, pcName, iPos, fVal);
		}
	xcase TSGV_String:
		TestServer_InsertGlobal_String(pcScope, pcName, iPos, pcVal);
	xcase TSGV_Password:
		TestServer_InsertGlobal_Password(pcScope, pcName, iPos, pcVal);
	xcase TSGV_Global:
		TestServer_InsertGlobal_Ref(pcScope, pcName, iPos, pcVal, pcVal2);
	xdefault:
		break;
	}

	TestServer_SetGlobalValueLabel(pcScope, pcName, iPos, pcLabel);

	if(iFoundPersist == 1)
	{
		TestServer_PersistGlobal(pcScope, pcName, iPersist);
	}

	httpRedirect(req->link, "/view");
}

static void TestServer_PersistGlobalRequest(HttpRequest *req)
{
	const char *pcScope = urlFindValue(req->vars, "scope");
	const char *pcName = urlFindSafeValue(req->vars, "name");
	int iPersist = 0;
	int iFoundPersist = urlFindBoundedInt(req->vars, "persist", &iPersist, 0, 1);

	if(req->method == HTTPMETHOD_POST && pcName[0] && iFoundPersist == 1)
	{
		TestServer_PersistGlobal(pcScope, pcName, iPersist);
	}

	httpRedirect(req->link, "/view");
}

static void TestServer_HttpHandler(HttpRequest *req, void *userdata)
{
	if(!stricmp(req->path, "/main"))
	{
		TestServer_WebMain(req);
	}
	else if(!stricmp(req->path, "/status"))
	{
		TestServer_Status(req);
	}
	else if(!stricmp(req->path, "/sub_status"))
	{
		TestServer_SubStatus(req);
	}
	else if(!stricmp(req->path, "/console"))
	{
		TestServer_ConsolePage(req);
	}
	else if(!stricmp(req->path, "/sub_console"))
	{
		TestServer_GetConsoleContents(req);
	}
	else if(!stricmp(req->path, "/verify"))
	{
		TestServer_VerifyScripts();
		httpRedirect(req->link, "/status");
	}
	else if(!stricmp(req->path, "/run"))
	{
		TestServer_RunOrListScripts(req);
	}
	else if(!stricmp(req->path, "/interrupt"))
	{
		TestServer_InterruptScript();
		httpRedirect(req->link, "/status");
	}
	else if(!stricmp(req->path, "/unqueue"))
	{
		int iIndex;

		if(urlFindInt(req->vars, "index", &iIndex) == 1)
		{
			TestServer_CancelScript(iIndex);
		}

		httpRedirect(req->link, "/status");
	}
	else if(!stricmp(req->path, "/lua"))
	{
		TestServer_LuaWebInterface(req);
	}
	else if(!stricmp(req->path, "/set"))
	{
		TestServer_SetGlobalRequest(req);
	}
	else if(!stricmp(req->path, "/insert"))
	{
		TestServer_InsertGlobalRequest(req);
	}
	else if(!stricmp(req->path, "/persist"))
	{
		TestServer_PersistGlobalRequest(req);
	}
	else if(!stricmp(req->path, "/clear"))
	{
		TestServer_ClearGlobalRequest(req);
	}
	else if(!stricmp(req->path, "/view"))
	{
		TestServer_View(req);
	}
	else if(!stricmp(req->path, "/saved"))
	{
		TestServer_SavedView(req);
	}
	else
	{
		httpRedirect(req->link, "/main");
	}
}

void TestServer_StartWebInterface(void)
{
	loadstart_printf("Opening web interface...");

	while(!hrListen(commDefault(), giTestServerHttpPort, TestServer_HttpHandler, NULL))
	{
		++giTestServerHttpPort;
	}

	hrEnableXMLRPC(true);
	httpEnableAuthentication(giTestServerHttpPort, GetProductName(), DEFAULT_HTTP_CATEGORY_FILTER);
	csSetCustomLoadPath("server/TestServer/templates");

	GenericHttpServing_Begin(giTestServerHttpPort+1, GetProductName(), DEFAULT_HTTP_CATEGORY_FILTER, 0);

	InitializeCriticalSection(&cs_sConsoleBuffer);

	loadend_printf("...open on port %d.", giTestServerHttpPort);
}

static int TestServer_EffectiveLineLength(const char *pcString, int iExistingLength)
{
	int i, count = iExistingLength;
	
	if(!pcString)
	{
		return 0;
	}

	for(i = 0; i < (int)strlen(pcString); ++i, ++count)
	{
		if(pcString[i] == '\t')
		{
			count += 7 - (count % 8);
		}
	}

	return count - iExistingLength;
}

static const char *TestServer_ConcatEffectiveLength(char **ppcDest, const char *pcSrc, int iLength, int iExistingLength)
{
	int i, count = iExistingLength;
	char *pcDestTemp = NULL;

	for(i = 0; i < (int)strlen(pcSrc) && count - iExistingLength < iLength; ++i, ++count)
	{
		if(pcSrc[i] == '\t')
		{
			count += 7 - (count % 8);
		}

		estrCopyWithHTMLUnescaping(&pcDestTemp, *ppcDest);
		estrCopy(ppcDest, &pcDestTemp);
		estrConcat(ppcDest, &pcSrc[i], 1);
		estrCopyWithHTMLEscaping(&pcDestTemp, *ppcDest, false);
		estrCopy(ppcDest, &pcDestTemp);
		estrDestroy(&pcDestTemp);
	}

	if(i < (int)strlen(pcSrc))
	{
		return &pcSrc[i];
	}
	else
	{
		return NULL;
	}
}

static void TestServer_AddConsoleLine(int color, const char *pcString, bool bAppend)
{
	TestServerConsoleLine *pLine;
	const char *pcNext;
	int newLen;
	int oldLen = 0;
	int appendLen;

	EnterCriticalSection(&cs_sConsoleBuffer);
	if(bAppend && sppConsoleBuffer)
	{
		pLine = eaGet(&sppConsoleBuffer, eaSize(&sppConsoleBuffer) - 1);
		oldLen = TestServer_EffectiveLineLength(pLine->pcLine, 0);
	}
	else
	{
		if(eaSize(&sppConsoleBuffer) >= TESTSERVER_CONSOLE_MAXLINES)
		{
			pLine = eaRemove(&sppConsoleBuffer, 0);
			estrDestroy(&pLine->pcLine);
			StructDestroy(parse_TestServerConsoleLine, pLine);
		}

		pLine = StructCreate(parse_TestServerConsoleLine);
		pLine->color = COLOR_LEAVE;
		eaPush(&sppConsoleBuffer, pLine);
	}

	if(color != COLOR_LEAVE)
	{
		pLine->color = color;
	}

	newLen = TestServer_EffectiveLineLength(pcString, oldLen);
	appendLen = MIN(newLen, TESTSERVER_CONSOLE_LINESIZE - oldLen);
	pcNext = TestServer_ConcatEffectiveLength(&pLine->pcLine, pcString, appendLen, oldLen);

	pLine->iTimestamp = timeMsecsSince2000();
	siLastPrintf = pLine->iTimestamp;

	if(pcNext)
	{
		TestServer_AddConsoleLine(color, pcNext, false);
	}
	LeaveCriticalSection(&cs_sConsoleBuffer);
}

static void TestServer_AddConsoleString(int color, char *pcString)
{
	int len = 0, start = 0, i;
	bool bFirst = true;

	printfColor(color, "%s", pcString);
	len = (int)strlen(pcString);

	for(i = 0; i < len; ++i)
	{
		if(pcString[i] == '\n')
		{
			pcString[i] = 0;
			TestServer_AddConsoleLine(color, &pcString[start], bFirst);
			pcString[i] = '\n';

			start = i + 1;
			bFirst = false;
		}
	}

	TestServer_AddConsoleLine(color, &pcString[start], bFirst);
}

void TestServer_ConsolePrintf(const char *pcFormat, ...)
{
	char *pcOutStr = NULL;

	estrGetVarArgs(&pcOutStr, pcFormat);
	TestServer_AddConsoleString(COLOR_RED | COLOR_GREEN | COLOR_BLUE, pcOutStr);
	estrDestroy(&pcOutStr);
}

void TestServer_ConsolePrintfColor(int color, const char *pcFormat, ...)
{
	char *pcOutStr = NULL;

	estrGetVarArgs(&pcOutStr, pcFormat);
	TestServer_AddConsoleString(color, pcOutStr);
	estrDestroy(&pcOutStr);
}

#include "TestServerHttp_h_ast.c"