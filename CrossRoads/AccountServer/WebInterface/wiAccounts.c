#include "wiAccounts.h"
#include "AccountServer.h"
#include "AccountManagement.h"
#include "AutoGen/wiAccounts_c_ast.h"
#include "wiCommon.h"
#include "timing.h"
#include "StringUtil.h"
#include "AccountLog.h"
#include "WikiToHTML.h"

extern int giServerMonitorPort;

/************************************************************************/
/* Index                                                                */
/************************************************************************/

static void wiHandleAccountsIndex(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* View                                                                 */
/************************************************************************/

AUTO_STRUCT;
typedef struct ASWIActivityLog
{
	char *pMessage; AST(ESTRING)
	U32 uTime;
} ASWIActivityLog;

AUTO_STRUCT;
typedef struct ASWIAccount
{
	const char *pSelf; AST(UNOWNED)
	const AccountInfo *pAccount; AST(UNOWNED)
	char *pServerMonitorLink; AST(ESTRING)
	EARRAY_OF(ASWIActivityLog) eaActivities;
} ASWIAccount;

static void wiHandleAccountsView(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	const char *pSelf = "view" WI_EXTENSION;
	ASWIAccount aswiAccount = {0};
	U32 uAccountID = 0;

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	StructInit(parse_ASWIAccount, &aswiAccount);

	aswiAccount.pSelf = pSelf;

	uAccountID = wiGetInt(pWebRequest, "id", 0);
	if (uAccountID)
	{
		aswiAccount.pAccount = findAccountByID(uAccountID);
	}

	if (aswiAccount.pAccount)
	{
		EARRAY_OF(const AccountLogEntry) eaEntries = NULL;

		accountGetLogEntries(aswiAccount.pAccount, &eaEntries, 0, 0);
		EARRAY_CONST_FOREACH_BEGIN(eaEntries, i, s);
		{
			ASWIActivityLog * pActivityLog = StructCreate(parse_ASWIActivityLog);
			pActivityLog->pMessage = wikiToHTML(eaEntries[i]->pMessage);
			pActivityLog->uTime = eaEntries[i]->uSecondsSince2000;
			eaPush(&aswiAccount.eaActivities, pActivityLog);
		}
		EARRAY_FOREACH_END;
		eaDestroy(&eaEntries);

		estrPrintf(&aswiAccount.pServerMonitorLink, "http://%s:%u/viewxpath?xpath=AccountServer[1].globObj.Account[%u]",
			getHostName(), giServerMonitorPort, aswiAccount.pAccount->uID);

	}

	wiAppendStruct(pWebRequest, "AccountsView.cs", parse_ASWIAccount, &aswiAccount);

	StructDeInit(parse_ASWIAccount, &aswiAccount);

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Search                                                               */
/************************************************************************/

static void wiHandleAccountsSearch(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Create                                                               */
/************************************************************************/

static void wiHandleAccountsCreate(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Handler                                                              */
/************************************************************************/

bool wiHandleAccounts(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	bool bHandled = false;

	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

#define WI_ACCOUNT_PAGE(page) \
	if (!stricmp_safe(wiGetPath(pWebRequest), WI_ACCOUNTS_DIR #page WI_EXTENSION)) \
	{ \
		wiHandleAccounts##page(pWebRequest); \
		bHandled = true; \
	}

	WI_ACCOUNT_PAGE(Index);
	WI_ACCOUNT_PAGE(View);
	WI_ACCOUNT_PAGE(Search);
	WI_ACCOUNT_PAGE(Create);

#undef WI_ACCOUNT_PAGE

	PERFINFO_AUTO_STOP_FUNC();

	return bHandled;
}

#include "AutoGen/wiAccounts_c_ast.c"