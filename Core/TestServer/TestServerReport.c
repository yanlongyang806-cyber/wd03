#include "TestServerReport.h"
#include "EString.h"
#include "file.h"
#include "net.h"
#include "netsmtp.h"
#include "StringCache.h"
#include "TestServerExpression.h"
#include "TestServerGlobal.h"
#include "TestServerHttp.h"
#include "TestServerIntegration.h"
#include "TestServerMetric.h"
#include "timing.h"
#include "windefinclude.h"

#include "TestServerHttp_h_ast.h"
#include "AutoGen/TestServerIntegration_h_ast.h"

static CRITICAL_SECTION cs_eaReports;
static TestServerGlobalReference **eaReports;

bool gbEmailEnabled = true;
AUTO_CMD_INT(gbEmailEnabled, EnableEmail);

AUTO_COMMAND ACMD_NAME(QueueReport);
void TestServer_QueueReport(const char *pcScope, const char *pcName)
{
	TestServerGlobalReference *pRef;

	EnterCriticalSection(&cs_eaReports);
	pRef = StructCreate(parse_TestServerGlobalReference);
	pRef->pcScope = allocAddString(pcScope);
	pRef->pcName = allocAddString(pcName);

	eaPush(&eaReports, pRef);
	LeaveCriticalSection(&cs_eaReports);
}

AUTO_COMMAND ACMD_NAME(CancelAllReports);
void TestServer_CancelAllReports(void)
{
	EnterCriticalSection(&cs_eaReports);
	eaDestroyStruct(&eaReports, parse_TestServerGlobalReference);
	LeaveCriticalSection(&cs_eaReports);
}

static void TestServer_EmailReport_internal(TestServerView *pView)
{
	SMTPMessageRequest *pReq;
	char *estr = NULL;
	int i, count;

	pReq = StructCreate(parse_SMTPMessageRequest);
	estrPrintf(&pReq->from, "TestServer@" ORGANIZATION_DOMAIN);

	TestServer_GlobalAtomicBegin();
	count = TestServer_GetGlobalValueCount("Config", "ReportRecipients");

	if(!count)
	{
		StructDestroy(parse_SMTPMessageRequest, pReq);
		TestServer_GlobalAtomicEnd();
		return;
	}

	for(i = 0; i < count; ++i)
	{
		if(TestServer_GetGlobalValueType("Config", "ReportRecipients", i) == TSGV_String)
		{
			eaPush(&pReq->to, estrDup(TestServer_GetGlobal_String("Config", "ReportRecipients", i)));
		}
	}
	TestServer_GlobalAtomicEnd();

	estrPrintf(&pReq->subject, "[%s] %s", getHostName(), pView->pcName);
	pReq->html = true;
	pReq->comm = commDefault();
	estrPrintf(&pReq->server, "moon");
	pReq->timeout = 5;
	pReq->body = TestServer_ViewToHtml(pView);

	smtpMsgRequestSend(pReq, &estr);
	StructDestroy(parse_SMTPMessageRequest, pReq);

	if(estr)
	{
		TestServer_ConsolePrintfColor(COLOR_RED | COLOR_BRIGHT, "An e-mail error occurred! %s\n", estr);
		estrDestroy(&estr);
	}
}

static void TestServer_WriteReport_internal(TestServerView *pView)
{
	char *path = NULL;
	char *fn = NULL;
	char date[CRYPTIC_MAX_PATH];

	timeMakeFilenameDateStringFromSecondsSince2000(date, timeSecondsSince2000());
	estrPrintf(&fn, "%s %s", date, pView->pcName);
	estrMakeAllAlphaNumAndUnderscores(&fn);
	estrPrintf(&path, "%s/server/TestServer/views/%s.view", fileLocalDataDir(), fn);
	estrDestroy(&fn);
	ParserWriteTextFile(path, parse_TestServerView, pView, 0, 0);
	estrDestroy(&path);
}

static void TestServer_IssueReport_internal(const char *pcScope, const char *pcName)
{
	TestServerView *pView = NULL;

	// Get the view
	pView = TestServer_GlobalToView(pcScope, pcName, true);

	// Email it to interested recipients
	if(gbEmailEnabled)
	{
		TestServer_EmailReport_internal(pView);
	}

	// Write it to a file
	TestServer_WriteReport_internal(pView);

	// Free the view
	StructDestroy(parse_TestServerView, pView);
}

AUTO_COMMAND ACMD_NAME(IssueReport);
void TestServer_IssueReport(const char *pcScope, const char *pcName)
{
	TestServer_IssueReport_internal(pcScope, pcName);
}

static bool TestServer_CheckReport_internal(const char *pcScope, const char *pcName)
{
	int i, count;
	TestServerGlobalType eType;

	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Unset)
	{
		TestServer_GlobalAtomicEnd();
		return false;
	}

	count = TestServer_GetGlobalValueCount(pcScope, pcName);
	for(i = 0; i < count; ++i)
	{
		TestServerGlobalValueType eValType = TestServer_GetGlobalValueType(pcScope, pcName, i);

		if(eValType == TSGV_Global)
		{
			if(!TestServer_CheckReport_internal(TestServer_GetGlobal_RefScope(pcScope, pcName, i), TestServer_GetGlobal_RefName(pcScope, pcName, i)))
			{
				TestServer_GlobalAtomicEnd();
				return false;
			}
		}
		else if(eValType == TSGV_Unset)
		{
			TestServer_GlobalAtomicEnd();
			return false;
		}
	}
	TestServer_GlobalAtomicEnd();

	return true;
}

void TestServer_InitReports(void)
{
	InitializeCriticalSection(&cs_eaReports);
}

void TestServer_CheckReports(void)
{
	static lastCheck = 0;
	int curTime = timeSecondsSince2000();
	int i;

	if(curTime == lastCheck)
	{
		return;
	}

	lastCheck = curTime;

	EnterCriticalSection(&cs_eaReports);
	for(i = eaSize(&eaReports) - 1; i >= 0; --i)
	{
		TestServerGlobalReference *pRef = eaReports[i];

		TestServer_GlobalAtomicBegin();
		if(TestServer_GetGlobalType(pRef->pcScope, pRef->pcName) == TSG_Unset)
		{
			StructDestroy(parse_TestServerGlobalReference, eaRemove(&eaReports, i));
		}
		else if(TestServer_CheckReport_internal(pRef->pcScope, pRef->pcName))
		{
			// send out notifications
			TestServer_IssueReport_internal(pRef->pcScope, pRef->pcName);
			StructDestroy(parse_TestServerGlobalReference, eaRemove(&eaReports, i));
		}
		TestServer_GlobalAtomicEnd();
	}
	LeaveCriticalSection(&cs_eaReports);
}