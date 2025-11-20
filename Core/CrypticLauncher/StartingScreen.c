#include "CrypticLauncher.h"
#include "patcher.h"
#include "registry.h"
#include "GameDetails.h"
#include "options.h"
#include "xfers_dialog.h"
#include "version.h"
#include "launcherUtils.h"
#include "systemtray.h"

#include "sock.h"
#include "HttpLib.h"
#include "structNet.h"
#include "accountnet.h"
#include "mastercontrolprogram.h"
#include "utils.h"
#include "error.h"
#include "file.h"
#include "netlinkprintf.h"
#include "fileutil.h"
#include "estring.h"
#include "timing.h"
#include "mcpUtilities.h"
#include "earray.h"
#include "GlobalComm.h"
#include "sysutil.h"
#include "stringutil.h"
#include "embedbrowser.h"
#include "textparser.h"
#include "pcl_client.h"
#include "pcl_typedefs.h"
#include "pcl_client_struct.h"
#include "systemspecs.h"
#include "ThreadSafeQueue.h"
#include "Prefs.h"
#include "hoglib.h"
#include "autogen/NewControllerTracker_pub_h_ast.h"
#include "Organization.h"

#include <exdisp.h>		/* Defines of stuff like IWebBrowser2. This is an include file with Visual C 6 and above */
#include <mshtml.h>		/* Defines of stuff like IHTMLDocument2. This is an include file with Visual C 6 and above */
#include <mshtmhst.h>	/* Defines of stuff like IDocHostUIHandler. This is an include file with Visual C 6 and above */

#define AUTO_REFRESH_SERVER_LIST_DELAY 600

ShardInfo_Basic *g_prepatch = NULL;
AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_NAME(prepatch);
void cmd_prepatch(char *product, char *name, char *category, char *prepatch, char *cwd)
{
	char *tmp = NULL;
	int ignored;
	g_prepatch = StructCreate(parse_ShardInfo_Basic);
	g_prepatch->pProductName = StructAllocString(product);
	estrSuperUnescapeString(&tmp, name);
	g_prepatch->pShardName = StructAllocString(tmp);
	g_prepatch->pShardCategoryName = StructAllocString(category);
	estrSuperUnescapeString(&tmp, prepatch);
	g_prepatch->pPrePatchCommandLine = StructAllocString(tmp);

	// Set the working dir to something more sane
	// We don't mind if it fails though.
	estrSuperUnescapeString(&tmp, cwd);
	ignored = _chdir(tmp);

	estrDestroy(&tmp);
}

static bool g_no_portrait = false;
AUTO_CMD_INT(g_no_portrait, no_portrait);

static bool g_no_localshard = false;
AUTO_CMD_INT(g_no_localshard, no_localshard);

static char *g_fake_shards = NULL;
AUTO_COMMAND ACMD_NAME(FakeShards) ACMD_CMDLINE;
void cmd_FakeShards(const char *path)
{
	SAFE_FREE(g_fake_shards);
	g_fake_shards = strdup(path);
}

// Write out the list of shards to a file.
static char *g_write_shards = NULL;
AUTO_COMMAND ACMD_NAME(WriteShards) ACMD_CMDLINE;
void cmd_WriteShards(const char *path)
{
	SAFE_FREE(g_write_shards);
	g_write_shards = strdup(path);
}

static char *g_override_ct = NULL;
AUTO_COMMAND ACMD_NAME(ct) ACMD_CMDLINE;
void cmd_SetOverrideCT(const char *ct)
{
	SAFE_FREE(g_override_ct);
	g_override_ct = strdup(ct);
}

static char **ctIPs = NULL;

#define startPatch(window, shard) postCommandPtr(((CrypticLauncherWindow*)(window)->pUserData)->queueToPCL, CLCMD_START_PATCH, (shard))
#define doButtonAction(window, shard) postCommandPtr(((CrypticLauncherWindow*)(window)->pUserData)->queueToPCL, CLCMD_DO_BUTTON_ACTION, (shard))

extern bool g_read_password;

extern bool g_qa_mode;
extern bool g_dev_mode;
extern bool g_pwrd_mode;

static U32 g_taskbar_create_message = 0;

static char* g_window_exclusions[] = {
	"button",
	"tooltip",
	"sysshadow",
	"shell_traywnd",
	"gdkwindowtemp"
};

// If set, check this file next frame against the current patcher's filespec.
static const char *s_debug_check_filespec = NULL;

static char *sAccountFailureMessage = NULL;

static bool excludeWindow(HWND h)
{
	char buf[100];
	U32 i;

	if(!IsWindowVisible(h))
		return true;

	GetClassName(h, SAFESTR(buf));
	for(i=0; i<ARRAY_SIZE_CHECKED(g_window_exclusions); i++)
	{
		if(strstri(buf, g_window_exclusions[i])!=NULL)
			return true;
	}

	return false;
}

bool setButtonState(SimpleWindow *window, enumCrypticLauncherButtonState state) 
{
	char *state_str = "";
	switch(state)
	{
	case CL_BUTTONSTATE_DISABLED:
		state_str = "disabled";
		break;
	case CL_BUTTONSTATE_PATCH:
		state_str = "patch";
		break;
	case CL_BUTTONSTATE_PLAY:
		state_str = "play";
		break;
	case CL_BUTTONSTATE_CANCEL:
		state_str = "cancel";
		break;
	}
	return InvokeScript(window, "do_set_button_state", SCRIPT_ARG_STRING, state_str, SCRIPT_ARG_NULL);
}


bool DisplayMsg(SimpleWindow *window, const char *msg)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow *)window->pUserData;
	IHTMLElement *elm;
	BSTR bstr;

	if(!launcher->htmlDoc)
		return false;

	// Update the message text
	bstr = TStr2BStr(window, msg);
	elm = GetWebElement(window, launcher->htmlDoc, "msg", 0, NULL);
	if(!elm)
		return false;
	elm->lpVtbl->put_innerHTML(elm, bstr);
	SysFreeString(bstr);
	estrPrintf(&launcher->statusText, "%s", msg);
	return true;
}

static void hideElm(SimpleWindow *window, const char *id)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow *)window->pUserData;
	IHTMLElement *elm = GetWebElement(window, launcher->htmlDoc, id, 0, NULL);
	if(elm)
	{
		// Hide the element
		IHTMLStyle *style;
		BSTR style_text;
		assert(SUCCEEDED(elm->lpVtbl->get_style(elm, &style)));
		style_text = TStr2BStr(window, "visibility: hidden");
		assert(SUCCEEDED(style->lpVtbl->put_cssText(style, style_text)));
		SysFreeString(style_text);
		style->lpVtbl->Release(style);
		elm->lpVtbl->Release(elm);
	}
}

static const char *getLangCode(void)
{
	switch(locGetLanguage(getCurrentLocale()))
	{
	case LANGUAGE_FRENCH:
		return "fr";
	case LANGUAGE_GERMAN:
		return "de";
	default:
		return"en-US";
	}
}

static int cmpShardInfo(const ShardInfo_Basic **a, const ShardInfo_Basic **b)
{
	int i = stricmp((*a)->pProductName, (*b)->pProductName);
	if(i == 0)
		i = stricmp((*a)->pShardName, (*b)->pShardName);
	return i;
}

static void restartAtLoginPage(SimpleWindow *window, const char *errorMessageToDisplayAfterLoginPageLoaded)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow *)window->pUserData;
	char *url=NULL;

	// Restart from the beginning.
	estrDestroy(&launcher->accountError);

	launcher->state = CL_STATE_START;

	estrPrintf(&url, "%slauncher_login", launcher->baseUrl);
	DisplayHTMLPage(window, url, NULL, getLangCode());
	estrDestroy(&url);

	sAccountFailureMessage = strdup(errorMessageToDisplayAfterLoginPageLoaded);
}

static bool UpdateShardList(SimpleWindow *window, ShardInfo_Basic_List *availableShardList)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow *)window->pUserData;
	long i;
	IHTMLDocument2 *htmlDoc;
	IHTMLElement *elm;
	IHTMLSelectElement *selectElm;
	IHTMLOptionElement *optionElm;
	VARIANT var;
	char cstr[MAX_PATH];
	BSTR bstr;
	char *err=NULL;
	bool show_product = false;

	if(!launcher->htmlDoc || !launcher->shardList->ppShards)
		return false;
	htmlDoc = launcher->htmlDoc;

	VariantInit(&var);

	if(availableShardList->pMessage && availableShardList->pMessage[0])
	{
		// Update the message text
		char *msg = availableShardList->pMessage;
		if(availableShardList->pUserMessage && availableShardList->pUserMessage[0])
			msg = availableShardList->pUserMessage;
		bstr = TStr2BStr(window, cgettext(msg));
		elm = GetWebElement(window, htmlDoc, "msg", 0, &err);
		assertmsgf(elm, "Cannot find msg element in %s", err);
		estrDestroy(&err);
		elm->lpVtbl->put_innerHTML(elm, bstr);
		SysFreeString(bstr);
	}

	// Update the shard select box
	elm = GetWebElement(window, htmlDoc, "shards", 0, &err);
	assertmsgf(elm, "Cannot find shard element in %s", err);
	estrDestroy(&err);
	elm->lpVtbl->QueryInterface(elm, &IID_IHTMLSelectElement, (void **)&selectElm);
	assertmsg(selectElm, "Can't cast to a SelectElement");
	if(FAILED(selectElm->lpVtbl->get_length(selectElm, &i)))
		return false;
	for(--i; i>=0; --i)
	{
		selectElm->lpVtbl->remove(selectElm, i);
	}
	eaQSort(availableShardList->ppShards, cmpShardInfo);
	for(i=eaSize(&availableShardList->ppShards)-1; i >= 0; i--)
	{
		if(stricmp(availableShardList->ppShards[i]->pShardCategoryName, "Xbox")==0)
		{
			StructDestroy(parse_ShardInfo_Basic, availableShardList->ppShards[i]);
			eaRemove(&availableShardList->ppShards, i);
			continue;
		}

		if(stricmp(availableShardList->ppShards[i]->pProductName, gdGetName(launcher->gameID))!=0 &&
		   stricmp(availableShardList->ppShards[i]->pProductName, "Local")!=0)
			show_product = true;
	}
	for(i=0; i < eaSize(&availableShardList->ppShards); i++)
	{
		ShardInfo_Basic *shard = availableShardList->ppShards[i];

		if((!shard->pPatchCommandLine || !shard->pPatchCommandLine[0]) && stricmp(shard->pProductName, "Local")!=0)
			continue;

		bstr = TStr2BStr(window, "option");
		htmlDoc->lpVtbl->createElement(htmlDoc, bstr, &elm);
		SysFreeString(bstr);

		elm->lpVtbl->QueryInterface(elm, &IID_IHTMLOptionElement, (void **)&optionElm);
		if(show_product)
		{	
			const char *displayName = gdGetDisplayName(gdGetIDByName(shard->pProductName));
			//nameDetails(shard->pProductName, NULL, &displayName);
			sprintf(cstr, "%s - %s", displayName, shard->pShardName);
		}
		else
			sprintf(cstr, "%s", shard->pShardName);
		bstr = TStr2BStr(window, cstr);
		optionElm->lpVtbl->put_text(optionElm, bstr);
		SysFreeString(bstr);

		sprintf(cstr, "%s:%s", shard->pProductName, shard->pShardName);
		bstr = TStr2BStr(window, cstr);
		optionElm->lpVtbl->put_value(optionElm, bstr);
		SysFreeString(bstr);

		selectElm->lpVtbl->add(selectElm, elm, var);
	}
	return true;
}

static ShardInfo_Basic * getSelectedShard(SimpleWindow *window, char **err)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow *)window->pUserData;
	IHTMLElement *elm;
	IHTMLSelectElement *selectElm;
	BSTR bstr;
	char *cstr, *temp;
	int i;
	//unsigned int verify;

	if(!launcher->htmlDoc)
		return false;

	if(g_prepatch)
		return g_prepatch;
	
	elm = GetWebElement(window, launcher->htmlDoc, "shards", 0, err);
	if(!elm || FAILED(elm->lpVtbl->QueryInterface(elm, &IID_IHTMLSelectElement, (void **)&selectElm)))
	{
		if(err && elm)
			estrPrintf(err, "Unable to get HTMLSelectElement interface from element");
		return NULL;
	}
	selectElm->lpVtbl->get_value(selectElm, &bstr);
	cstr = BStr2TStr(window, bstr);
	temp = strchr(cstr, ':');
	if(!temp)
	{
		if(err)
			estrPrintf(err, "Invalid shard line selected: %s", cstr);
		GlobalFree(cstr);
		return NULL;
	}
	*temp = '\0';
	//*product = estrCreateFromStr(cstr);
	//*shard = estrCreateFromStr(temp+1);

	for(i=0; i < eaSize(&launcher->shardList->ppShards); i++) 
	{
		ShardInfo_Basic *shard = launcher->shardList->ppShards[i];
		if(!stricmp(cstr, shard->pProductName) && !stricmp(temp+1, shard->pShardName)) {
			GlobalFree(cstr);
			//if(readRegInt(shard->pProductName, "VerifyOnNextUpdate", &verify, NULL))
			//	launcher->forceVerify = verify;
			loadRegistrySettings(launcher, shard->pProductName);
			return shard;
		}
	}

	if(err)
		estrPrintf(err, "Can't find a shard to match %s:%s", cstr, temp+1);
	GlobalFree(cstr);
	return NULL;
}

static void showElement(CrypticLauncherWindow *launcher, const char *elm_id, bool visible)
{
	IHTMLElement *elm;
	IHTMLStyle *style;
	BSTR style_text;

	elm = GetWebElement(launcher->window, launcher->htmlDoc, elm_id, 0, NULL);
	assert(elm);
	assert(SUCCEEDED(elm->lpVtbl->get_style(elm, &style)));
	style_text = TStr2BStr(launcher->window, visible?"display:":"display:none");
	assert(SUCCEEDED(style->lpVtbl->put_cssText(style, style_text)));
	SysFreeString(style_text);
	style->lpVtbl->Release(style);
}

#define CONTROLLERTRACKER_ATTEMPT_TIMEOUT 1

void CrypticLauncherControllerTrackerCallback(Packet *pak,int cmd, NetLink *link, void *user_data)
{
	SimpleWindow *window = (SimpleWindow *)user_data;
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow *)window->pUserData;
	

	switch(cmd)
	{
	case FROM_NEWCONTROLLERTRACKER_TO_MCP_HERE_IS_SHARD_LIST:
		{	
			// Decode the shard list from the controller tracker
			StructDeInit(parse_ShardInfo_Basic_List, launcher->shardList);
			ParserRecvStructSafe(parse_ShardInfo_Basic_List, pak, launcher->shardList);

			// Check if we have only one shard, and if it is an avatar shard
			if(eaSize(&launcher->shardList->ppShards)==1 && stricmp(launcher->shardList->ppShards[0]->pShardCategoryName, "Avatar")==0)
				launcher->avatarMode = true;

			// If we have a debugger, show the local shard
			if(IsDebuggerPresent() && !launcher->avatarMode && !g_no_localshard)
			{
				ShardInfo_Basic *local_shard = StructCreate(parse_ShardInfo_Basic);
				local_shard->pProductName = StructAllocString("Local");
				local_shard->pShardName = StructAllocString("Local");
				local_shard->pShardCategoryName = StructAllocString("Local");
				local_shard->pShardLoginServerAddress = StructAllocString("localhost");
				local_shard->pShardControllerAddress = StructAllocString("localhost");
				//local_shard->pPrePatchCommandLine = StructAllocString("-sync -project FightClubServer -name FC_9_20090713_0952");
				eaPush(&launcher->shardList->ppShards, local_shard);
			}

			// Write out the shard list, if requested.
			if(g_write_shards)
				ParserWriteTextFile(g_write_shards, parse_ShardInfo_Basic_List, launcher->shardList, 0, 0);

			// Allow making a fake list of shards for testing
			if(g_fake_shards)
			{
				StructDeInit(parse_ShardInfo_Basic_List, launcher->shardList);
				assert(ParserReadTextFile(g_fake_shards, parse_ShardInfo_Basic_List, launcher->shardList, 0));
			}

			// Get a new ticket for the web server.
			if (!accountValidateStartLoginProcess(launcher->username))
			{
				// already displaying the launcher_login page - so set the state back to the beginning, display an error, set the error timeout, and move on.
				launcher->state = CL_STATE_LOGINPAGELOADED;
				DisplayMsg(launcher->window, "Unable to contact account server");
				launcher->accountLastFailureTime = time(NULL);
			}
			else
			{
				launcher->state = CL_STATE_GETTINGPAGETICKET;
			}
			linkRemove(&launcher->linkToCT);
			launcher->linkToCT = NULL;
		}
		break;
	}
}

//returns true if connected
bool CrypticLauncherUpdateControllerTrackerConnection(SimpleWindow *window, char **ppResultEString)
{
	U32 iCurTime;
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)window->pUserData;
	ControllerTrackerConnectionStatusStruct *pStatus = launcher->ctConnStatus;
	static int ctIndex = 0;
	int i;

	if (launcher->linkToCT && linkConnected(launcher->linkToCT) && !linkDisconnected(launcher->linkToCT))
	{
		if (pStatus->iOverallBeginTime != 0)
		{
			Packet *pak;
			char *prodName = GetProductName();

			if(launcher->allMode)
				prodName = "all";

			pak = pktCreate(launcher->linkToCT, FROM_MCP_TO_NEWCONTROLLERTRACKER_REQUEST_SHARD_LIST);
			pktSendString(pak, prodName);
			if(launcher->accountHasTicket)
				// XXX: Remove this when compat with old tickets is no longer needed <NPK 2008-12-10>
				pktSendString(pak, launcher->accountTicket);
			else
			{
				pktSendString(pak, ACCOUNT_FASTLOGIN_LABEL);
				pktSendU32(pak, launcher->accountID);
				pktSendU32(pak, launcher->accountTicketID);
			}
			pktSend(&pak);
		}

		pStatus->iOverallBeginTime = 0;
		return true;
	}

	if (!eaSize(&ctIPs))
	{

		if(g_override_ct)
			eaPush(&ctIPs, strdup(g_override_ct));
		else if(g_qa_mode)
			eaPush(&ctIPs, strdup(QA_CONTROLLERTRACKER_HOST));
		else if(g_dev_mode)
			eaPush(&ctIPs, strdup(DEV_CONTROLLERTRACKER_HOST));
		else if(g_pwrd_mode)
			eaPush(&ctIPs, strdup(PWRD_CONTROLLERTRACKER_HOST));
		else
			GetAllUniqueIPs(CONTROLLERTRACKER_HOST, &ctIPs);
		if (!eaSize(&ctIPs))
		{
			estrPrintf(ppResultEString, "DNS can't resolve controller tracker");
			return false;
		}

		ctIndex = timeSecondsSince2000() % eaSize(&ctIPs);
	}

	iCurTime = timeSecondsSince2000();

	if (pStatus->iOverallBeginTime == 0)
		pStatus->iCurBeginTime = pStatus->iOverallBeginTime = iCurTime;

	// Timeout, move on to the next possible server
	if (iCurTime - pStatus->iCurBeginTime > CONTROLLERTRACKER_ATTEMPT_TIMEOUT)
	{
		linkRemove(&launcher->linkToCT);
		ctIndex += 1;
		ctIndex %= eaSize(&ctIPs);
	}

	if(!launcher->linkToCT)
	{
		launcher->linkToCT = commConnect(launcher->comm, LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, ctIPs[ctIndex], NEWCONTROLLERTRACKER_GENERAL_MCP_PORT, CrypticLauncherControllerTrackerCallback, 0, 0, 0);
		if(launcher->linkToCT)
		{
			linkSetUserData(launcher->linkToCT, (void*)window);
		}
		else
		{
			ctIndex += 1;
			ctIndex %= eaSize(&ctIPs);
		}
		pStatus->iCurBeginTime = iCurTime;
		launcher->lastControllerTracker = ctIPs[ctIndex];
	}

	estrPrintf(ppResultEString, "Attempting to connect to controller tracker");
	for(i=0; i<eaSize(&ctIPs); i++)
		estrConcatf(ppResultEString, ".");
	return false;
}

char *DEFAULT_LATELINK_getAccountServer(void);
char *OVERRIDE_LATELINK_getAccountServer(void)
{
	if(accountServerWasSet())
		return DEFAULT_LATELINK_getAccountServer();

	if(g_qa_mode)
		return QA_ACCOUNTSERVER_HOST;

	if(g_dev_mode)
		return DEV_ACCOUNTSERVER_HOST;

	if(g_pwrd_mode)
		return PWRD_ACCOUNTSERVER_HOST;

	return DEFAULT_LATELINK_getAccountServer();
}

// Clear a field on the login page, with extra option to allow silently ignoring a missing field.
static void setFieldText(CrypticLauncherWindow *launcher, const char *field, const char *text, bool allow_missing)
{
	char *errmsg=NULL;
	IHTMLElement *elm;
	IHTMLInputElement *inputElm;
	IHTMLElement2 *elm2;
	char acpText[1024];
	BSTR bstrText;

	// Clear the field.
	elm = GetWebElement(launcher->window, launcher->htmlDoc, field, 0, &errmsg);
	if (!elm && allow_missing)
		return;
	assertmsgf(elm, "Cannot find %s element: %s", field, errmsg);
	estrDestroy(&errmsg);
	elm->lpVtbl->QueryInterface(elm, &IID_IHTMLInputElement, (void **)&inputElm);
	assertmsgf(inputElm, "Unable to cast %s element to input", field);
	UTF8ToACP(text, SAFESTR(acpText));
	bstrText = TStr2BStr(launcher->window, acpText);
	inputElm->lpVtbl->put_value(inputElm, bstrText);
	SysFreeString(bstrText);
	inputElm->lpVtbl->Release(inputElm);
	elm->lpVtbl->QueryInterface(elm, &IID_IHTMLElement2, (void **)&elm2);
	assertmsgf(elm2, "Unable to cast %s element to element2", field);
	elm2->lpVtbl->focus(elm2);
	elm2->lpVtbl->Release(elm2);
	elm->lpVtbl->Release(elm);
}

// Clear a field on the login page.
static void clearField(CrypticLauncherWindow *launcher, const char *field)
{
	setFieldText(launcher, field, "", false);
}

static void accountLauncherLinkInit(CrypticLauncherWindow *launcher, bool bUseConnectCB);

// Handle an error on the Account Server link.
static void accountHandleError(const char *reason, CrypticLauncherWindow *launcher)
{
	estrPrintf(&launcher->accountError, "%s", reason);
}

// Create a conflict URL.
static void createConflictUrl(char **url, CrypticLauncherWindow *launcher, U32 ticket, const char *prefix)
{
	// Convert a partial conflict path into a complete URL.
	if (launcher->baseUrl && *launcher->baseUrl
		&& !(strStartsWith(launcher->conflictUrl, "http://") || strStartsWith(launcher->conflictUrl, "https://")))
	{
		estrAppend2(url, launcher->baseUrl);
		if (launcher->conflictUrl[0] == '/')
			estrRemove(url, estrLength(url) - 1, 1);
	}

	// Add actual path or URL.
	estrAppend2(url, launcher->conflictUrl);

	// Append flags.
	estrConcatf(url, "?pwaccountname=%s&ticket=%s%u", launcher->username, NULL_TO_EMPTY(prefix), ticket);

}

// Specifically for when we're in CL_STATE_LOGGINGIN *or* CL_STATE_LOGGINGINAFTERLINK, but only when
// forceMigrate on in launcher, but off in account server.  This situation at the moment (1/9/2013) is never hit
// TODO: remove this code path and levels above when really sure this is never called (look for callers to accountLauncherLinkInit() with bUseConnectCB=true)
static int accountHandleConnect(NetLink* link,void *user_data)
{
	CrypticLauncherWindow *launcher = user_data;
	if (!accountValidateStartLoginProcess(launcher->username))
	{
		restartAtLoginPage(launcher->window, STACK_SPRINTF("Unable to authenticate: %s", NULL_TO_EMPTY(launcher->accountError)));
	}
	else
	{
		// successfully started login process
		// launcher->state remains CL_STATE_LOGGINGIN or CL_STATE_LOGGINGINAFTERLINK for now, and will advance to next state later in the call chain
		// (would like to understand more of the state transition here)
	}
	return 0;
}

// Handle a packet coming from the Account Server.
static int accountHandleInput(Packet* pak, int cmd, NetLink* link, CrypticLauncherWindow *launcher)
{
	static bool sent_specs = false;
	AccountLoginType eAccountType = ACCOUNTLOGINTYPE_Default;

	switch (cmd)
	{
	case FROM_ACCOUNTSERVER_LOGIN_NEW:
		launcher->accountID = pktGetU32(pak);
		launcher->accountTicketID = pktGetU32(pak);
		if (launcher->forceMigrate)
		{
			// Check for data on LoginType
			if (pktCheckRemaining(pak, sizeof(U32)))
				eAccountType = pktGetU32(pak);
		}
		launcher->accountHasTicket = false;

		if(!sent_specs)
		{
			char tempbuf[2048];
			Packet *pak_out = pktCreate(link, TO_ACCOUNTSERVER_LOGSPECS);
			sent_specs = true;
			systemSpecsGetNameValuePairString(SAFESTR(tempbuf));
			pktSendU32(pak_out, launcher->accountID);
			pktSendString(pak_out, tempbuf);
			pktSend(&pak_out);
		}

		switch(launcher->state)
		{
		xcase CL_STATE_LOGGINGIN:
		acase CL_STATE_LOGGINGINAFTERLINK:
			// If we're forcing migrate...
			if (launcher->forceMigrate)
			{
				if (eAccountType == ACCOUNTLOGINTYPE_Default && launcher->state == CL_STATE_LOGGINGIN && launcher->loginType == ACCOUNTLOGINTYPE_CrypticAndPW)
				{
					// Retry login with just PW
					AccountValidateData validateData = {0};
					launcher->loginType = ACCOUNTLOGINTYPE_PerfectWorld;
					// Clear error state.
					estrDestroy(&launcher->accountError);
					accountLauncherLinkInit(launcher, true);
				}
				else if (launcher->loginType == ACCOUNTLOGINTYPE_Cryptic || eAccountType == ACCOUNTLOGINTYPE_Cryptic)
				{
					// We succeeded after we retried as a Cryptic login, start force migrate flow.
					char *url = NULL;

					// Clear the password field, if it is present.
					setFieldText(launcher, "pass", "", launcher->state == CL_STATE_LOGGINGINAFTERLINK);

					// Create conflict URL.
					estrStackCreate(&url);
					createConflictUrl(&url, launcher, launcher->accountTicketID, "N%3A");

					// Navigate to conflict page.
					DisplayHTMLPage(launcher->window, url, NULL, getLangCode());
					estrDestroy(&url);
					launcher->state = CL_STATE_LINKING;
				}
				else
					launcher->state = CL_STATE_LOGGEDIN;
			}
			else
				launcher->state = CL_STATE_LOGGEDIN;
			// The next step is to get the shard list from the Controller Tracker.

		xcase CL_STATE_GETTINGPAGETICKET:
			{
				char *post=NULL, *url=NULL;

				DisplayMsg(launcher->window, "Loading launcher");

				// Release the existing page pointer
				if(launcher->htmlDoc)
				{
					launcher->htmlDoc->lpVtbl->Release(launcher->htmlDoc);
					launcher->htmlDoc = NULL;
				}
				//else
				//	Errorf("Page pointer already released when page ticket received");

				// Request the real launcher page.
				estrPrintf(&post, "accountid=%u&ticketid=%u", launcher->accountID, launcher->accountTicketID);
				estrPrintf(&url, "%slauncher", launcher->baseUrl);
				if(launcher->avatarMode)
					estrConcatf(&url, "_scion");
				DisplayHTMLPage(launcher->window, url, post, getLangCode());
				estrDestroy(&post);
				estrDestroy(&url);
				launcher->state = CL_STATE_GOTPAGETICKET;
				SAFE_FREE(launcher->currentUrl);
				accountValidateCloseAccountServerLink();
				launcher->accountLink = NULL;
			}
		xcase CL_STATE_GETTINGGAMETICKET:
			// Run the game.
			{
				char *commandline = NULL, cwd[MAX_PATH], *regbuf=NULL, regname[512], *errmsg=NULL;
				bool local;
				ShardInfo_Basic *shard = launcher->fastLaunch;
				int i, pid;

				sprintf(regname, "%s:%s:%s", shard->pProductName, shard->pShardName, shard->pShardCategoryName);
				i = eaFindString(&launcher->history, regname);
				if(i != -1)
					eaRemove(&launcher->history, i);
				eaInsert(&launcher->history, regname, 0);
				if(eaSize(&launcher->history) > 10)
					for(i=10; i<eaSize(&launcher->history); i++)
						eaRemove(&launcher->history, i);

				if(eaSize(&launcher->history))
					estrPrintf(&regbuf, "%s", launcher->history[0]);
				for(i=1; i<eaSize(&launcher->history); i++)
					estrConcatf(&regbuf, ",%s", launcher->history[i]);
				writeRegStr("CrypticLauncher", "GameHistory", regbuf);
				estrDestroy(&regbuf);

				local = strcmpi(shard->pProductName, "Local") == 0;

				estrCreate(&commandline);
				estrConcatf(&commandline, "\"%s/GameClient.exe\" %s -AuthTicketNew %u %u -Locale %s %s",
				//estrConcatf(&commandline, "%s/GameClient.exe %s %s",
					local ? STACK_SPRINTF("C:/src/%s/bin", gdGetName(launcher->gameID)) : launcher->config->root,
					launcher->useSafeMode ? "-safemode" : "",
					launcher->accountID, launcher->accountTicketID,
					locGetName(getCurrentLocale()),
					shard->pAutoClientCommandLine ? shard->pAutoClientCommandLine : "");
				if(!local)
				{
					if(eaiSize(&shard->allLoginServerIPs))
					{
						for(i=eaiSize(&shard->allLoginServerIPs)-1; i>=0; i--)
						{
							U32 ip = shard->allLoginServerIPs[i];
							estrConcatf(&commandline, " -server %d.%d.%d.%d", ip&255, (ip>>8)&255, (ip>>16)&255, (ip>>24)&255);
						}
					}
					else
						estrConcatf(&commandline, " -server %s", shard->pShardLoginServerAddress);

					if (shard->pLoginServerPortsAndIPs && eaSize(&shard->pLoginServerPortsAndIPs->ppPortIPPairs))
					{
						FOR_EACH_IN_EARRAY(shard->pLoginServerPortsAndIPs->ppPortIPPairs,
							PortIPPair, pPair)
						{
							estrConcatf(&commandline, " -?LoginServerShardIPAndPort %s %s %d  ", shard->pShardName, makeIpStr(pPair->iIP), pPair->iPort);
						}
						FOR_EACH_END;
					}
				}
				else
					estrConcatf(&commandline, " -ConnectToController");
				// If we have prepatch data, send it on to the client
				if(shard->pPrePatchCommandLine && shard->pPrePatchCommandLine[0] && !strstri(shard->pPrePatchCommandLine, "-name HOLD"))
				{
					char *prepatch = NULL, *prepatch_esc = NULL, *prepatch_str = NULL, *prepatch_name = NULL, *prepatch_cwd = NULL;
					// Generate the full command-line to execute after the gameclient finishes
					estrSuperEscapeString(&prepatch_name, shard->pShardName);
					estrSuperEscapeString(&prepatch_str, shard->pPrePatchCommandLine);
					assert(_getcwd(cwd, ARRAY_SIZE_CHECKED(cwd)));
					estrSuperEscapeString(&prepatch_cwd,cwd);
					estrPrintf(&prepatch, "%s %s -prepatch %s %s %s %s %s", getExecutableName(), NULL_TO_EMPTY(launcher->commandline), shard->pProductName, prepatch_name, shard->pShardCategoryName, prepatch_str, prepatch_cwd);
					// Append the now double-superescaped data to the gameclient command line
					estrSuperEscapeString(&prepatch_esc, prepatch);
					estrConcatf(&commandline, " -superesc prepatch %s", prepatch_esc);
					estrDestroy(&prepatch);
					estrDestroy(&prepatch_esc);
					estrDestroy(&prepatch_str);
					estrDestroy(&prepatch_name);
					estrDestroy(&prepatch_cwd);
				}
				// Special case for the standalone creator client to make it not die on a huge number of missing textures
				if(stricmp(shard->pShardCategoryName, "Avatar")==0)
					estrConcatf(&commandline, " -CostumesOnly -AuthTicket %u %u %s", launcher->accountID, launcher->accountTicketID, launcher->username);
				if(launcher->proxy)
				{
					if(stricmp(launcher->proxy, "US")==0)
						estrConcatf(&commandline, " -SetProxy us1.proxy." ORGANIZATION_DOMAIN " 80");
					else if(stricmp(launcher->proxy, "EU")==0)
					{
						int server = time(NULL) % 2;
						if(server)
							estrConcatf(&commandline, " -SetProxy eu1.proxy." ORGANIZATION_DOMAIN " 80");
						else
							estrConcatf(&commandline, " -SetProxy eu2.proxy." ORGANIZATION_DOMAIN " 80");
					}
				}
				if(launcher->commandLine)
					estrConcatf(&commandline, " %s", NULL_TO_EMPTY(launcher->commandLine));

				PrefStoreInt(shardPrefSet(shard), "Locale", getCurrentLocale());

				{
					// Verify the root folder is correct
					char *test_root = shardRootFolder(shard);
					assertmsgf(stricmp(launcher->config->root, test_root)==0, "Root folder should be \"%s\", is \"%s\"", test_root, launcher->config->root);
					estrDestroy(&test_root);
				}

				// Touch dynamic.hogg so we know it exists
				{
					HogFile *dynamic_hogg;
					char dynamic_path[MAX_PATH];
					sprintf(dynamic_path, "%s/piggs/dynamic.hogg", launcher->config->root);
					dynamic_hogg = hogFileRead(dynamic_path, NULL, PIGERR_PRINTF, NULL, HOG_DEFAULT);
					hogFileDestroy(dynamic_hogg, true);
				}

				fileGetcwd(cwd, ARRAY_SIZE_CHECKED(cwd)-1);
				assert(chdir(local ? STACK_SPRINTF("C:/src/%s/bin", gdGetName(launcher->gameID)) : launcher->config->root) == 0);
				pid = system_detach(commandline, false, false);
				if(!pid)
					errmsg = lastWinErr();
				assert(chdir(cwd) == 0);
				estrDestroy(&commandline);
				accountValidateCloseAccountServerLink();
				launcher->accountLink = NULL;
				if(pid)
				{
					systemTrayRemove(launcher->window->hWnd);
					exit(0);
				}
				else
				{
					DisplayMsg(launcher->window, STACK_SPRINTF(FORMAT_OK(_("Unable to start game client: %s")), errmsg));
					launcher->state = CL_STATE_READY;
					setButtonState(launcher->window, CL_BUTTONSTATE_PLAY);
				}
			}
		}
		break;
	case FROM_ACCOUNTSERVER_FAILED:
		if (launcher->accountServerSupportsLoginFailedPacket)
			break;
	case FROM_ACCOUNTSERVER_LOGIN_FAILED:
		if(launcher->state == CL_STATE_LOGGINGIN || launcher->state == CL_STATE_LOGGINGINAFTERLINK)
		{
			const char *msg = NULL;
			LoginFailureCode code = LoginFailureCode_Unknown;

			if (cmd == FROM_ACCOUNTSERVER_LOGIN_FAILED)
			{
				code = pktGetU32(pak);
				msg = accountValidatorGetFailureReasonByCode(NULL, code);
				launcher->accountServerSupportsLoginFailedPacket = true;
			}
			else
				msg = pktGetStringTemp(pak);

			// If this login failed due to a PW account conflict, redirect the user to the conflict page.
			if (cmd == FROM_ACCOUNTSERVER_LOGIN_FAILED && code == LoginFailureCode_UnlinkedPWCommonAccount && launcher->conflictUrl && launcher->conflictUrl[0])
			{
				char *url = NULL;
				U32 ticket;

				// Clear the password field
				clearField(launcher, "pass");

				// Get the conflict ticket ID from the packet.
				ticket = pktGetU32(pak);

				// Create conflict URL.
				estrStackCreate(&url);
				createConflictUrl(&url, launcher, ticket, NULL);

				// Navigate to conflict page.
				DisplayHTMLPage(launcher->window, url, NULL, getLangCode());
				estrDestroy(&url);

				launcher->state = CL_STATE_LINKING;
			}
			// Forcing migrate and user authenticated with Cryptic credentials against an AS that is rejecting Cryptic logins
			else if (launcher->state == CL_STATE_LOGGINGIN && launcher->forceMigrate && code == LoginFailureCode_CrypticDisabled)
			{
				char *url = NULL;
				U32 ticket;

				// Clear the password field
				clearField(launcher, "pass");
					
				// Get the conflict ticket ID from the packet.
				ticket = pktGetU32(pak);

				// Create conflict URL.
				estrStackCreate(&url);
				createConflictUrl(&url, launcher, ticket, "C%3A");

				// Navigate to conflict page.
				DisplayHTMLPage(launcher->window, url, NULL, getLangCode());
				estrDestroy(&url);

				launcher->state = CL_STATE_LINKING;
			}
			// If we're forcing migrate, and we failed the initial PerfectWorld login, try again with Cryptic.
			else if (launcher->state == CL_STATE_LOGGINGIN && launcher->forceMigrate && launcher->loginType == ACCOUNTLOGINTYPE_PerfectWorld && code == LoginFailureCode_NotFound)
			{
				AccountValidateData validateData = {0};

				// Try different login type.
				launcher->loginType = ACCOUNTLOGINTYPE_Cryptic;

				// Clear error state.
				estrDestroy(&launcher->accountError);

				// Initiate new validate link.
				accountLauncherLinkInit(launcher, true);
			}
			// Just a normal login failure.
			else
			{
				// Display failure.
				DisplayMsg(launcher->window, msg);
	
				// Clear the password field
				if (launcher->state == CL_STATE_LOGGINGIN)
					clearField(launcher, "pass");

				launcher->state = CL_STATE_LOGINPAGELOADED;
				launcher->accountLastFailureTime = time(NULL);
			}
		}
		break;
	default:
		break;
	}
	return 1;
}

static void accountLauncherLinkInit(CrypticLauncherWindow *launcher, bool bUseConnectCB)
{
	AccountValidateData validateData = {0};
	validateData.eLoginType = launcher->loginType;
	validateData.login_cb = accountHandleInput;
	validateData.failed_cb = accountHandleError;
	if (bUseConnectCB)
		validateData.connect_cb = accountHandleConnect;
	validateData.userData = launcher;
	validateData.pLoginField = launcher->username;
	validateData.pPassword = launcher->password;
	validateData.bPasswordHashed = launcher->passwordHashed;

	launcher->accountLink = accountValidateInitializeLinkEx(&validateData);
}


static void updateSpeedData(LauncherSpeedData *data, F32 time)
{
	S64 delta = data->cur - data->last;
	if(delta < 0)
		delta = 0;
	data->last = data->cur;
	data->deltas[data->head] = delta;
	data->times[data->head] = time;
	data->head = (data->head + 1) % LAUNCHER_SPEED_SAMPLES;
}

void OVERRIDE_LATELINK_netreceive_socksRecieveError(NetLink *link, U8 code)
{
	MessageBox(NULL, "An error has occurred while connecting to the proxy server, if this happens again it may be helpful to try disabling the proxy and restarting the game client.", "Proxy server error", MB_OK|MB_ICONERROR);
}

bool MCPStartTickFunc(SimpleWindow *pWindow)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)pWindow->pUserData;
	HRESULT ret;
	CrypticLauncherCommand *cmd;
	
	if(!launcher)
		return true;

	commMonitor(launcher->comm);
	//patchTick(pWindow);
	while((ret = XLFQueueRemove(launcher->queueFromPCL, &cmd)) == S_OK)
	{
		switch(cmd->type)
		{
		xcase CLCMD_DISPLAY_MESSAGE:
			DisplayMsg(pWindow, cmd->str_value);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**cmd[4]'"
			free(cmd->str_value);
		xcase CLCMD_SET_PROGRESS:
			InvokeScript(pWindow, "do_set_progress", SCRIPT_ARG_STRING, cmd->str_value, SCRIPT_ARG_NULL);
			free(cmd->str_value);
		xcase CLCMD_SET_PROGRESS_BAR:
			InvokeScript(pWindow, "set_progress_bar", SCRIPT_ARG_INT, cmd->int_value, SCRIPT_ARG_NULL);
		xcase CLCMD_SET_BUTTON_STATE:
			setButtonState(pWindow, cmd->int_value);
		xcase CLCMD_PUSH_BUTTON:
			doButtonAction(pWindow, getSelectedShard(pWindow, NULL));
		xcase CLCMD_START_LOGIN_FOR_GAME:
			accountLauncherLinkInit(launcher, false);
			accountValidateWaitForLink(&launcher->accountLink, 5);
			if(!accountValidateStartLoginProcess(launcher->username))
			{
				// launcher->state remains CL_STATE_READY - let the user keep trying - this is a valid state to stay in.
				DisplayMsg(pWindow, _("Unable to connect to account server (3)"));
			}
			else
			{
				launcher->fastLaunch = cmd->ptr_value;
				launcher->state = CL_STATE_GETTINGGAMETICKET;
			}
		}
		free(cmd);
	}
	assert(ret == XLOCKFREE_STRUCTURE_EMPTY);
	if(launcher->state == CL_STATE_LOGINPAGELOADED && launcher->accountLastFailureTime && time(NULL) - launcher->accountLastFailureTime >= 5)
	{
		DisplayMsg(pWindow, "");
		launcher->accountLastFailureTime = 0;
	}
	if(launcher->state == CL_STATE_LOGGEDIN)
	{
		char *pControllerTrackerStatusString = NULL;
		if (!CrypticLauncherUpdateControllerTrackerConnection(pWindow, &pControllerTrackerStatusString))
		{
			DisplayMsg(pWindow, pControllerTrackerStatusString);
			estrDestroy(&pControllerTrackerStatusString);
		}
	}

	// Speed tracking
	if(timerElapsed(launcher->delta_timer) > 0.5)
	{	
		F32 time = timerElapsedAndStart(launcher->delta_timer);
		if(launcher->client && launcher->client->link)
			launcher->speed_link.cur = linkStats(launcher->client->link)->recv.real_bytes;
		updateSpeedData(&launcher->speed_received, time);
		updateSpeedData(&launcher->speed_actual, time);
		updateSpeedData(&launcher->speed_link, time);
	}

	// Perform debugging filespec check, if requested.
	if (s_debug_check_filespec)
	{
		if (launcher->client)
		{
			bool not_required;
			char *debug = NULL;
			PCL_ErrorCode error;

			estrStackCreate(&debug);
			error = pclIsNotRequired(launcher->client, s_debug_check_filespec, &not_required, &debug);
			if (error)
				printf("error checking filespec\n");
			else
				printf("%s: %s, %s\n", s_debug_check_filespec, not_required ? "OPTIONAL" : "REQUIRED", debug);
			estrDestroy(&debug);
		}
		else
		{
			printf("No PCL client.\n");
		}
		SAFE_FREE(s_debug_check_filespec);
	}

	return true;
}

HWND g_hWndInternetExplorerServer = NULL;

BOOL MCPPreDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	if(g_hWndInternetExplorerServer && 
		hDlg == g_hWndInternetExplorerServer && 
		pWindow->eWindowType == CL_WINDOW_MAIN && 
		iMsg >= WM_KEYDOWN && iMsg <= WM_KEYLAST)
	{
		CrypticLauncherWindow *launcher = pWindow->pUserData;
		if(wParam == VK_TAB || wParam == VK_RETURN)
		{
			pWindow->pDialogCB(hDlg, iMsg, wParam, lParam, pWindow);
			return TRUE;
		}
		else if(GetKeyState(VK_CONTROL) & 0x8000 && (wParam == 'V'))
		{
			pWindow->pDialogCB(hDlg, iMsg, wParam, lParam, pWindow);
			return TRUE;
		}
		else if(launcher->state >= CL_STATE_LOGGEDIN && GetKeyState(VK_CONTROL) & 0x8000 && wParam == 'X')
		{
			PostMessage(pWindow->hWnd, WM_APP, CLMSG_OPEN_XFER_DEBUG, 0);
			return TRUE;
		}
		else if(launcher->state >= CL_STATE_LOGGEDIN && GetKeyState(VK_CONTROL) & 0x8000 && wParam == 'C')
		{
			newConsoleWindow();
			showConsoleWindow();
		}
	}
	return FALSE;
}

// Clear the environment.
static void clearEnvironment()
{
	g_dev_mode = false;
	g_qa_mode = false;
	g_pwrd_mode = false;
}

// Set a new environment.
static void setEnvironment(CrypticLauncherWindow *launcher, const char *env, const char *note)
{
	char *message = NULL;
	estrStackCreate(&message);
	estrPrintf(&message, "You are now in the <b>%s</b> environment.  %s", env, note);
	DisplayMsg(launcher->window, message);
	estrDestroy(&message);
	clearField(launcher, "pass");
	clearField(launcher, "name");
	eaDestroyEx(&ctIPs, NULL);
	SetLauncherBaseUrl(launcher);
	launcher->state = CL_STATE_LOGINPAGELOADED;
}

// Return true if an IP should be considered "internal" for purposes of access to internal shards.
static bool isInternalIp(U32 ip) {
	return (ip & 0xffff) == 8108;
}

// Return true if the local host has an IP that passes isInternalIp().
static bool isInternalHost()
{
	return isInternalIp(getHostPublicIp()) || isInternalIp(getHostLocalIp());
}

// Get a form field.
// Note: You must call GlobalFree() on the result when you're done with it.
static char *getFormField(IHTMLDocument2 *htmlDoc, SimpleWindow *pWindow, const char *name, bool optional)
{
	IHTMLElement *elm;
	IHTMLInputElement *inputElm;
	BSTR value;
	char *errmsg=NULL;
	char *result = NULL;

	// Grab the username
	elm = GetWebElement(pWindow, htmlDoc, name, 0, &errmsg);
	if (!elm && optional)
		return NULL;
	assertmsgf(elm, "Cannot find %s element: %s", name, errmsg);
	estrDestroy(&errmsg);
	elm->lpVtbl->QueryInterface(elm, &IID_IHTMLInputElement, (void **)&inputElm);
	inputElm->lpVtbl->get_value(inputElm, &value);
	if(value)
		result = BStr2TStr(pWindow, value);
	inputElm->lpVtbl->Release(inputElm);
	elm->lpVtbl->Release(elm);

	return result;
}

BOOL MCPStartDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)pWindow->pUserData;
	assert(launcher);
	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			HICON hIcon;
			char *url=NULL;
			DWORD threadid;

			// Spawn a PCL thread
			_beginthreadex(NULL, 0, thread_Patch, pWindow, 0, &threadid);
			SetThreadName(threadid, "PCLThread");

			// Get a copy of the message used to indicate a new taskbar has been created
			ATOMIC_INIT_BEGIN;
			{
				g_taskbar_create_message = RegisterWindowMessage("TaskbarCreated");
			}
			ATOMIC_INIT_END;

			SetWindowText(hDlg, gdGetDisplayName(launcher->gameID));
			SetWindowPos(hDlg, NULL, 0, 0, 800 + (GetSystemMetrics(SM_CXBORDER) * 2), 600 + GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYBORDER), SWP_NOMOVE|SWP_NOZORDER);
			
			// Load the small and large icons
			hIcon = LoadImage(GetModuleHandle(NULL),
				MAKEINTRESOURCE(IDI_LAUNCHER),
				IMAGE_ICON,
				GetSystemMetrics(SM_CXSMICON),
				GetSystemMetrics(SM_CYSMICON),
				0);
			assertmsg(hIcon, "Can't load small icon");
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
			hIcon = LoadImage(GetModuleHandle(NULL),
				MAKEINTRESOURCE(IDI_LAUNCHER),
				IMAGE_ICON,
				GetSystemMetrics(SM_CXICON),
				GetSystemMetrics(SM_CYICON),
				0);
			assertmsg(hIcon, "Can't load big icon");
			SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

			// Initialize the embedded IE window and load a 
			launcher->com_initialization_status = OleInitialize(NULL);
			if (launcher->com_initialization_status != S_OK)
				Errorf("OLE failed to initialize: %lX", launcher->com_initialization_status);
			EmbedBrowserObject(pWindow);
			assert(launcher->browserPtr);
			DisplayHTMLStr(pWindow, "<em>Loading. Please wait.</em>");

			// Request the login page
			if(g_prepatch)
				estrPrintf(&url, "%slauncher_prepatch", launcher->baseUrl);
			else
				estrPrintf(&url, "%slauncher_login", launcher->baseUrl);
			DisplayHTMLPage(pWindow, url, NULL, getLangCode());
			estrDestroy(&url);

			return TRUE;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDCANCEL:
			// Handler for the red X in the corner
			pWindow->bCloseRequested = true;
			systemTrayRemove(launcher->window->hWnd);
			postCommandInt(launcher->queueToPCL, CLCMD_STOP_THREAD, 0);
			break;
		}
		break;
	case WM_SYSCOMMAND:
		switch(wParam)
		{
		case SC_MINIMIZE:
			if(launcher->minimizeTrayIcon)
			{
				systemTrayAdd(hDlg);
				ShowWindow(hDlg, SW_HIDE);
				return TRUE;
			}
		}
		break;
	case WM_NOTIFY:
		{
			register NMHDR	*nmhdr;

			// Is it a message that was sent from one of our _IDispatchEx's (informing us
			// of an event that happened which we want to be informed of)? If so, then the
			// NMHDR->hwndFrom is the handle to our window, and NMHDR->idFrom is 0.
			nmhdr = (NMHDR *)lParam;
			if (((NMHDR *)lParam)->hwndFrom == hDlg && !((NMHDR *)lParam)->idFrom)
			{
				// This message is sent from one of our _IDispatchEx's. The NMHDR is really
				// a WEBPARAMS struct, so we can recast it as so. Also, the wParam is the
				// __IDispatchEx object we used to attach to the event that just happened.
				WEBPARAMS		*webParams;
				_IDispatchEx	*lpDispatch;

				webParams =		(WEBPARAMS *)lParam;
				lpDispatch = 	(_IDispatchEx *)wParam;
				if (((NMHDR *)lParam)->code)
				{
					LPCTSTR		eventType;

					// It is some other event type, such as "onmouseover".
					eventType = webParams->eventStr;

					switch (lpDispatch->id)
					{
					case 1: // <select id="shards">
						{
							char *err=NULL;
							ShardInfo_Basic *shard = getSelectedShard(pWindow, &err);
							assertmsgf(shard, "%s", err);
							estrDestroy(&err);
							loadRegistrySettings(launcher, shard->pProductName);
							launcher->fastLaunch = shard;
							launcher->state = CL_STATE_LAUNCHERPAGELOADED;
							startPatch(pWindow, shard);
						}
						break;
					}
				}
				else
				{
					// This _IDispatch is about to be freed, so we need to detach all
					// events that were attached to it.
					VARIANT			varNull;
					//VariantInit(&varNull);
					varNull.vt = VT_NULL;
					switch (lpDispatch->id)
					{
					case 1:
						{
							IHTMLSelectElement	*selectElm;

							selectElm = (IHTMLSelectElement *)lpDispatch->object;

							// Detach from the event
							//selectElm->lpVtbl->put_onchange(selectElm, varNull);
							//selectElm->lpVtbl->Release(selectElm);
						}
						break;
					}
				}
			}

		}
		break;
	case WM_APP:
		switch(wParam)
		{
		case CLMSG_PAGE_LOADED: // Page loaded
			{
				HRESULT hr;
				VARIANT disp;
				IHTMLElement *elm;
				IHTMLSelectElement *selectElm;
				ShardInfo_Basic *shard = NULL;
				char lastshard[100];
				IOleInPlaceObject* pInPlace;
				IHTMLLocation *loc;
				BSTR bpathname;
				char *pathname;
				
				// XXX: What should I do here? <NPK 2008-06-05>
				hr = GetWebPtrs(pWindow, NULL, &launcher->htmlDoc);
				assertmsgf(SUCCEEDED(hr), "Unable to load web pointers: %s", getWinErrString(NULL, hr));

				RecordIETriviaData(launcher->htmlDoc);

				if(SUCCEEDED(launcher->htmlDoc->lpVtbl->QueryInterface( launcher->htmlDoc, &IID_IOleInPlaceObject, (void**)&pInPlace)))
					pInPlace->lpVtbl->GetWindow( pInPlace, &g_hWndInternetExplorerServer);

				// Find the URL of the current page
				hr = launcher->htmlDoc->lpVtbl->get_location(launcher->htmlDoc, &loc);
				assertmsgf(SUCCEEDED(hr), "Can't find location object: %s", getWinErrString(NULL, hr));
				loc->lpVtbl->get_pathname(loc, &bpathname);
				pathname = BStr2TStr(pWindow, bpathname);
				SysFreeString(bpathname);
				loc->lpVtbl->Release(loc);

				// Check for double notifications
				if(launcher->currentUrl && stricmp(launcher->currentUrl, pathname)==0 && launcher->currentLocale == (U32)getCurrentLocale())
				{
					GlobalFree(pathname);
					break;
				}
				SAFE_FREE(launcher->currentUrl);
				launcher->currentUrl = strdup(pathname);
				launcher->currentLocale = getCurrentLocale();

				// If we're in the linking phase, ignore page loads.
				if (launcher->state == CL_STATE_LOGGINGINAFTERLINK)
					break;

				// Check if the website is signaling that that the linking state is done.
				// Note: This could have been done by checking the pathname, as below, but we decided to do it differently for flexibility.
				if (launcher->state == CL_STATE_LINKING)
				{
					// link_success - Unlinked PWE account was logged in with in the first place, and successfully linked
					// PWE account linking is done.
					elm = GetWebElement(pWindow, launcher->htmlDoc, "link_success", 0, NULL);
					if (elm)
					{
						// Initiate authentication via accountnet
						estrDestroy(&launcher->accountError);
						accountLauncherLinkInit(launcher, false);
						accountValidateWaitForLink(&launcher->accountLink, 5);
						if(!accountValidateStartLoginProcess(launcher->username))
						{
							restartAtLoginPage(pWindow, STACK_SPRINTF("Unable to authenticate: %s", NULL_TO_EMPTY(launcher->accountError)));
							// this will result in launcher->state going to CL_STATE_START, and then CL_STATE_LOGINPAGELOADED
						}
						else
						{
							launcher->state = CL_STATE_LOGGINGINAFTERLINK;
						}
					}

					// migrate_success - Unlinked Cryptic account has gone through force migrate (linking) flow
					// Cryptic account force migrate is done.
					elm = GetWebElement(pWindow, launcher->htmlDoc, "migrate_success", 0, NULL);
					if (elm)
					{
						char *url=NULL;

						// Restart from the beginning.
						estrDestroy(&launcher->accountError);

						launcher->state = CL_STATE_START;

						estrPrintf(&url, "%slauncher_login", launcher->baseUrl);
						DisplayHTMLPage(pWindow, url, NULL, getLangCode());
						estrDestroy(&url);
					}

					break;
				}

				// Display the build timestamp
				elm = GetWebElement(pWindow, launcher->htmlDoc, "launcher_version", 0, NULL);
				if(elm)
				{
					char *old_version, *new_version;
					BSTR version;
					elm->lpVtbl->get_innerHTML(elm, &version);
					old_version = BStr2TStr(pWindow, version);
					SysFreeString(version);
					new_version = STACK_SPRINTF("%s%s", old_version, BUILD_CRYPTICSTAMP);
					GlobalFree(old_version);
					version = TStr2BStr(pWindow, new_version);
					elm->lpVtbl->put_innerHTML(elm, version);
					SysFreeString(version);
					elm->lpVtbl->Release(elm);
				}

				if(stricmp(pathname, "/launcher_login")==0)
				{
					IHTMLElement2 *elm2;
					launcher->state = CL_STATE_LOGINPAGELOADED;
					
					// Set username to saved value if present
					if(g_read_password || readRegStr("CrypticLauncher", "UserName", SAFESTR(lastshard), launcher->history))
					{
						IHTMLInputElement *inputElm;
						BSTR value;

						// If using login bypass, display the user name being logged-in-to during the login process,
						// even if it isn't the last login username in the history.
						if (g_read_password)
						{
							strcpy(lastshard, launcher->username);

							// Also fill in the password field
							setFieldText(launcher, "pass", "******", false);
						}

						// Set the username
						elm = GetWebElement(pWindow, launcher->htmlDoc, "name", 0, NULL);
						if(elm)
						{
							elm->lpVtbl->QueryInterface(elm, &IID_IHTMLInputElement, (void **)&inputElm);
							if(!inputElm)
							{
								char *tagName, *asString;
								elm->lpVtbl->get_tagName(elm, &value);
								tagName = BStr2TStr(pWindow, value);
								elm->lpVtbl->get_outerHTML(elm, &value);
								asString = BStr2TStr(pWindow, value);
								assertmsgf(inputElm, "Unable to cast <%s> to IHTMLInputElement: %s", tagName, asString);
								GlobalFree(tagName);
								GlobalFree(asString);
							}
							value = TStr2BStr(pWindow, lastshard);
							inputElm->lpVtbl->put_value(inputElm, value);
							inputElm->lpVtbl->Release(inputElm);
							elm->lpVtbl->Release(elm);
							SysFreeString(value);
						}

						// Focus the password field
						elm = GetWebElement(pWindow, launcher->htmlDoc, "pass", 0, NULL);
						if(elm)
						{
							elm->lpVtbl->QueryInterface(elm, &IID_IHTMLElement2, (void **)&elm2);
							assertmsg(elm2, "Unable to cast pass element to element2");
							elm2->lpVtbl->focus(elm2);
							elm2->lpVtbl->Release(elm2);
							elm->lpVtbl->Release(elm);
						}
					}
					else
					{
						// Focus the username field
						elm = GetWebElement(pWindow, launcher->htmlDoc, "name", 0, NULL);
						if(elm)
						{
							elm->lpVtbl->QueryInterface(elm, &IID_IHTMLElement2, (void **)&elm2);
							assertmsg(elm2, "Unable to cast name element to element2");
							elm2->lpVtbl->focus(elm2);
							elm2->lpVtbl->Release(elm2);
							elm->lpVtbl->Release(elm);
						}
					}

					// Automatically log in if we got a username/password on stdin.
					if(g_read_password && launcher->username[0])
					{
						char *conflictUrl;

						// Grab the conflict URL, since we may need it if this is a PW account that is unlinked.
						conflictUrl = getFormField(launcher->htmlDoc, pWindow, "link_page", true);
						if (conflictUrl)
						{
							launcher->conflictUrl = strdup(conflictUrl);
							GlobalFree(conflictUrl);
						}

						estrDestroy(&launcher->accountError);
						accountLauncherLinkInit(launcher, false);
						accountValidateWaitForLink(&launcher->accountLink, 5);
						if(!accountValidateStartLoginProcess(launcher->username))
						{
							// launcher->state remains CL_STATE_LOGINPAGELOADED
							DisplayMsg(pWindow, STACK_SPRINTF("Unable to authenticate: %s", NULL_TO_EMPTY(launcher->accountError)));
						}
						else
						{
							launcher->state = CL_STATE_LOGGINGIN;
							g_read_password = false;
						}
					}

					// handle displaying sAccountFailureMessage, if non-null
					if (sAccountFailureMessage)
					{
						DisplayMsg(pWindow, sAccountFailureMessage);
						launcher->accountLastFailureTime = time(NULL); // mark start of display time
						SAFE_FREE(sAccountFailureMessage);
					}

				}
				else if(stricmp(pathname, "/launcher")==0 || stricmp(pathname, "/launcher_scion")==0)
				{
					bool shard_down = true;
					bool use_buttons = false;
					launcher->state = CL_STATE_LAUNCHERPAGELOADED;

					UpdateShardList(pWindow, launcher->shardList);

					// Get the shard select box
					VariantInit(&disp);
					disp.vt = VT_DISPATCH;
					elm = GetWebElement(pWindow, launcher->htmlDoc, "shards", 0, NULL);
					assert(elm);
					disp.pdispVal = CreateWebEvtHandler(pWindow, launcher->htmlDoc, 0, 1, (IUnknown *)elm, 0);
					assert(disp.pdispVal);
					elm->lpVtbl->QueryInterface(elm, &IID_IHTMLSelectElement, (void **)&selectElm);

					// Check if there are any shards for the correct product
					if(launcher->gameID == 0)
						shard_down = eaSize(&launcher->shardList->ppShards) == 0;
					else
					{
						FOR_EACH_IN_EARRAY(launcher->shardList->ppShards, ShardInfo_Basic, s)
							if(stricmp(gdGetName(launcher->gameID), s->pProductName)==0 && !s->bNotReallyThere)
							{
								shard_down = false;
								break;
							}
						FOR_EACH_END
					}
					if(shard_down)
						InvokeScript(pWindow, "set_server_status", SCRIPT_ARG_INT, 0, SCRIPT_ARG_NULL);

					if(eaSize(&launcher->shardList->ppShards) == 0)
					{
						char buf[1024];
						sprintf(buf, FORMAT_OK(_("Account error, please click <a href=\"%ssupport/account_error\">here</a> for more information")), gdGetURL(launcher->gameID));
						DisplayMsg(pWindow, buf);
					}
					else if(eaSize(&launcher->shardList->ppShards) == 1)
					{
						// Hide the shard list and start patching if only one shard.
						/*IHTMLStyle *style;
						BSTR style_text;
						assert(SUCCEEDED(elm->lpVtbl->get_style(elm, &style)));
						style_text = TStr2BStr(pWindow, "visibility: hidden");
						assert(SUCCEEDED(style->lpVtbl->put_cssText(style, style_text)));
						SysFreeString(style_text);
						style->lpVtbl->Release(style);*/
						
						launcher->fastLaunch = launcher->shardList->ppShards[0];
						if(stricmp(launcher->fastLaunch->pProductName, gdGetName(launcher->gameID))!=0
						   || 
						   stricmp(launcher->fastLaunch->pShardCategoryName, "Live")!=0)
						{
							showElement(launcher, "shards_label", true);
							showElement(launcher, "shards", true);
						}
					}
					else
					{
						use_buttons = true;
						showElement(launcher, "shards_label", true);

						FOR_EACH_IN_EARRAY(launcher->shardList->ppShards, ShardInfo_Basic, s)
							bool matches_button = false;
							if(stricmp(s->pProductName, gdGetName(launcher->gameID))!=0)
							{
								use_buttons = false;
								break;
							}

							if(gdGetLiveShard(launcher->gameID) && stricmp(s->pShardName, gdGetLiveShard(launcher->gameID))==0)
								matches_button = true;
							else if(gdGetPtsShard1(launcher->gameID) && stricmp(s->pShardName, gdGetPtsShard1(launcher->gameID))==0)
								matches_button = true;
							else if(gdGetPtsShard2(launcher->gameID) && stricmp(s->pShardName, gdGetPtsShard2(launcher->gameID))==0)
								matches_button = true;
							if(!matches_button)
							{
								use_buttons = false;
								break;
							}
						FOR_EACH_END

						if(use_buttons)
						{
							FOR_EACH_IN_EARRAY(launcher->shardList->ppShards, ShardInfo_Basic, s)
								if(gdGetLiveShard(launcher->gameID) && stricmp(s->pShardName, gdGetLiveShard(launcher->gameID))==0)
									showElement(launcher, "shard_button_1", true);
								else if(gdGetPtsShard1(launcher->gameID) && stricmp(s->pShardName, gdGetPtsShard1(launcher->gameID))==0)
									showElement(launcher, "shard_button_2", true);
								else if(gdGetPtsShard2(launcher->gameID) && stricmp(s->pShardName, gdGetPtsShard2(launcher->gameID))==0)
									showElement(launcher, "shard_button_3", true);
							FOR_EACH_END
						}
						else
						{
							showElement(launcher, "shards", true);
						}
						

						if(!launcher->fastLaunch && readRegStr(NULL, "LastShard", lastshard, 100, NULL))
						{
							char buf[100];

							// Set the box back to the last selected shard if possible
							FOR_EACH_IN_EARRAY(launcher->shardList->ppShards, ShardInfo_Basic, shardIter)
								sprintf(buf, "%s:%s", shardIter->pProductName, shardIter->pShardName);
								if(stricmp(lastshard, buf) == 0)
								{						
									launcher->fastLaunch = shardIter;
									break;
								}
							FOR_EACH_END
						}
					}

					// Check the PCL state in case this is a reload
					postCommandPtr(launcher->queueToPCL, CLCMD_FIX_STATE, NULL);

					// Figure out the default shard
					if(launcher->fastLaunch)
						shard = launcher->fastLaunch;
					else
					{
						shard = NULL;
						 /* Formerly, there was logic to handle having two live shards, and a single test shard.
						  * Now, there is a single live shard, and two test shards.  If we alter this arrangement in the future,
						  * we might being back this code.
						 FOR_EACH_IN_EARRAY(launcher->shardList->ppShards, ShardInfo_Basic, s)
							if(gdGetPtsShard1(launcher->gameID) && stricmp(s->pShardName, gdGetPtsShard1(launcher->gameID))==0)
							{
								shard = s;
								break;
							}
						FOR_EACH_END */

						if(!shard)
						{
							FOR_EACH_IN_EARRAY(launcher->shardList->ppShards, ShardInfo_Basic, s)
								if(gdGetLiveShard(launcher->gameID) && s &&stricmp(s->pShardName, gdGetLiveShard(launcher->gameID))==0)
								{
									shard = s;
									break;
								}
							FOR_EACH_END
						}

						if(!shard)
							shard = eaHead(&launcher->shardList->ppShards);
					}


					// Do a patch check against the initially selected shard.
					if(shard)
					{
						BSTR bstr = TStr2BStr(pWindow, STACK_SPRINTF("%s:%s", shard->pProductName, shard->pShardName));
						selectElm->lpVtbl->put_value(selectElm, bstr);
						SysFreeString(bstr);

						if(use_buttons)
						{
							char *button_to_enable = NULL;
							if(gdGetLiveShard(launcher->gameID) && stricmp(shard->pShardName, gdGetLiveShard(launcher->gameID))==0)
								button_to_enable = "shard_button_1";
							else if(gdGetPtsShard1(launcher->gameID) && stricmp(shard->pShardName, gdGetPtsShard1(launcher->gameID))==0)
								button_to_enable = "shard_button_2";
							else if(gdGetPtsShard2(launcher->gameID) && stricmp(shard->pShardName, gdGetPtsShard2(launcher->gameID))==0)
								button_to_enable = "shard_button_3";

							if(button_to_enable)
							{
								IHTMLElement *button_elm = GetWebElement(pWindow, launcher->htmlDoc, button_to_enable, 0, NULL);
								if(button_elm)
								{
									bstr = TStr2BStr(pWindow, "shard-button-selected");
									button_elm->lpVtbl->put_className(button_elm, bstr);
									SysFreeString(bstr);
									button_elm->lpVtbl->Release(button_elm);
								}
							}
						}

						loadRegistrySettings(launcher, shard->pProductName);
						startPatch(pWindow, shard);
					}

					// Install the change callback
					selectElm->lpVtbl->put_onchange(selectElm, disp);
					
					// Release the select element
					selectElm->lpVtbl->Release(selectElm);
					elm->lpVtbl->Release(elm);

					// Hide the portrait if needed
					if(g_no_portrait)
					{
						hideElm(pWindow, "charaspace");
					}
				}
				else if(stricmp(pathname, "/launcher_prepatch")==0)
				{
					elm = GetWebElement(pWindow, launcher->htmlDoc, "shards", 0, NULL);
					if(elm)
					{
						// Hide the shard list
						IHTMLStyle *style;
						BSTR style_text;
						assert(SUCCEEDED(elm->lpVtbl->get_style(elm, &style)));
						style_text = TStr2BStr(pWindow, "visibility: hidden");
						assert(SUCCEEDED(style->lpVtbl->put_cssText(style, style_text)));
						SysFreeString(style_text);
						style->lpVtbl->Release(style);
						elm->lpVtbl->Release(elm);
					}
					elm = GetWebElement(pWindow, launcher->htmlDoc, "login_block", 0, NULL);
					if(elm)
					{
						// Hide the login form
						IHTMLStyle *style;
						BSTR style_text;
						assert(SUCCEEDED(elm->lpVtbl->get_style(elm, &style)));
						style_text = TStr2BStr(pWindow, "visibility: hidden");
						assert(SUCCEEDED(style->lpVtbl->put_cssText(style, style_text)));
						SysFreeString(style_text);
						style->lpVtbl->Release(style);
						elm->lpVtbl->Release(elm);
					}
					assertmsg(g_prepatch, "Loading prepatch page, but no prepatch data");
					launcher->fastLaunch = g_prepatch;
					startPatch(pWindow, g_prepatch);
				}
				else
				{
					// We have an unrecognized page that has a "/launcher*" suffix (the only way we'd get a CLMSG_PAGE_LOADED msg)
					// realistically, I don't ever think we'll get this?
					accountValidateStartLoginProcess(launcher->username);
					Errorf("unrecognized web page with '%s' suffix detected", pathname);
				}
				GlobalFree(pathname);
			}
			break;
		case CLMSG_ACTION_BUTTON_CLICKED: // Action button clicked
			{
				ShardInfo_Basic *shard = getSelectedShard(pWindow, NULL);
				if(launcher->isVerifying)
				{
					if(!launcher->askVerify)
						writeRegInt(shard->pProductName, "LastVerifyCancel",  timeSecondsSince2000());
					launcher->askVerify = false;
				}
				else if(shard)
					doButtonAction(pWindow, shard);
			}
			break;
		case CLMSG_OPTIONS_CLICKED: // Options link
			{
				SimpleWindow *window;
				ShardInfo_Basic *shard = getSelectedShard(pWindow, NULL);
				if(!shard && stricmp(launcher->currentUrl, "/launcher_login")!=0 && eaSize(&launcher->shardList->ppShards)!=0) break;
				SimpleWindowManager_AddOrActivateWindow(CL_WINDOW_OPTIONS, 0, IDD_OPTIONS, false, OptionsDialogFunc, OptionsTickFunc, (void*)shard);
				window = SimpleWindowManager_FindWindow(CL_WINDOW_OPTIONS, 0);
				if(window)
					window->pPreDialogCB = OptionsPreDialogFunc;
			}
			break;
		case CLMSG_LOGIN_SUBMIT: // Login form
			{
				char *username;
				char *password;
				char *loginType;
				char *conflictUrl;
				char *errmsg=NULL;
				bool env_user = false;

				launcher->accountLastFailureTime = 0;
				launcher->username[0] = 0;
				launcher->password[0] = 0;
				launcher->loginType = ACCOUNTLOGINTYPE_Default;
				SAFE_FREE(launcher->conflictUrl);

				// Grab the username
				username = getFormField(launcher->htmlDoc, pWindow, "name", false);
				if (username)
				{
					if (!strcmp(username, ENV_USER))
						env_user = true;
					strcpy(launcher->username, username);
					GlobalFree(username);
				}

				// Grab the conflict page.
				conflictUrl = getFormField(launcher->htmlDoc, pWindow, "link_page", true);
				if (conflictUrl)
				{
					launcher->conflictUrl = strdup(conflictUrl);
					GlobalFree(conflictUrl);
				}

				// Grab the login type.
				loginType = getFormField(launcher->htmlDoc, pWindow, "login_type", true);
				if (loginType)
				{
					if (!stricmp_safe(loginType, "default"))
						launcher->loginType = ACCOUNTLOGINTYPE_Default;
					else if (!stricmp_safe(loginType, "cryptic"))
						launcher->loginType = ACCOUNTLOGINTYPE_Cryptic;
					else if (!stricmp_safe(loginType, "pwe"))
						launcher->loginType = ACCOUNTLOGINTYPE_PerfectWorld;
					else if (!stricmp_safe(loginType, "either"))
						launcher->loginType = ACCOUNTLOGINTYPE_CrypticAndPW;
					else if (!stricmp_safe(loginType, "force_migrate"))
					{
						launcher->loginType = ACCOUNTLOGINTYPE_CrypticAndPW;
						launcher->forceMigrate = true;
						if (!launcher->conflictUrl || !*launcher->conflictUrl)
						{
							Errorf("force_migrate is not valid without link_page");
							launcher->loginType = ACCOUNTLOGINTYPE_PerfectWorld;
							launcher->forceMigrate = false;
						}
					}
					else
						assertmsgf(0, "Invalid login_type %s", loginType);
					GlobalFree(loginType);
				}

				// Grab the password
				password = getFormField(launcher->htmlDoc, pWindow, "pass", false);
				if (password)
				{
					// needs upgrade to handle g_pwrd_mode possibly.
					if (env_user && !strcmp(password, "live") && isInternalHost()
						&& (g_dev_mode || g_qa_mode))
					{
						clearEnvironment();
						setEnvironment(launcher, "Live", "");
						GlobalFree(password);
						break;
					}
					else if (env_user && !strcmp(password, "dev") && isInternalHost()
						&& ipFromString(DEV_ACCOUNTSERVER_HOST))
					{
						clearEnvironment();
						g_dev_mode = true;
						setEnvironment(launcher, "Dev", "Absque sudore et labore nullum opus perfectum est.");
						GlobalFree(password);
						break;
					}
					else if (env_user && !strcmp(password, "qa") && isInternalHost()
						&& ipFromString(QA_ACCOUNTSERVER_HOST))
					{
						clearEnvironment();
						g_qa_mode = true;
						setEnvironment(launcher, "QA", "Urbem latericium invenit, marmoream reliquit.");
						GlobalFree(password);
						break;
					}
					else if (env_user && !strcmp(password, "pwrd") && isInternalHost()
						&& ipFromString(PWRD_ACCOUNTSERVER_HOST))
					{
						clearEnvironment();
						g_pwrd_mode = true;
						setEnvironment(launcher, "PWRD", "Adde parum parvo, magnus acervus erit.");
						GlobalFree(password);
						break;
					}
					else
					{
						strcpy(launcher->password, password);
						memset(password, 0, strlen(password));
					}
					GlobalFree(password);
				}

				// Initiate authentication via accountnet
				estrDestroy(&launcher->accountError);
				if (launcher->username[0] && launcher->password)
				{
					accountLauncherLinkInit(launcher, false);
					accountValidateWaitForLink(&launcher->accountLink, 5);
					if(!accountValidateStartLoginProcess(launcher->username))
					{
						// launcher->state remains CL_STATE_LOGINPAGELOADED
						DisplayMsg(pWindow, STACK_SPRINTF("Unable to authenticate: %s", NULL_TO_EMPTY(launcher->accountError)));
					}
					else
					{
						launcher->state = CL_STATE_LOGGINGIN;
					}
				}
				else
				{
					// launcher->state remains CL_STATE_LOGINPAGELOADED
				}

				// Save the username to the registry for next time.
				writeRegStr("CrypticLauncher", "UserName", launcher->username);
			}
			break;
		case CLMSG_OPEN_XFER_DEBUG: // X keypress
			{
				SimpleWindow *xfers_dialog = SimpleWindowManager_FindWindow(CL_WINDOW_XFERS, 0);
				if(xfers_dialog)
					PostMessage(xfers_dialog->hWnd, WM_COMMAND, IDCANCEL, 0);
				else
				{
					SimpleWindowManager_AddOrActivateWindow(CL_WINDOW_XFERS, 0, IDD_XFERS, false, XfersDialogFunc, XfersTickFunc, NULL);
					xfers_dialog = SimpleWindowManager_FindWindow(CL_WINDOW_XFERS, 0);
					if(xfers_dialog)
						xfers_dialog->pPreDialogCB = XfersPreDialogFunc;
					SetActiveWindow(pWindow->hWnd);
				}
			}
			break;
		case CLMSG_RELOAD_PAGE: // Reload launcher page
			{
				char *url = NULL;

				switch(launcher->state)
				{
				case CL_STATE_LOGINPAGELOADED:
					if(g_prepatch)
						estrPrintf(&url, "%slauncher_prepatch", launcher->baseUrl);
					else
						estrPrintf(&url, "%slauncher_login", launcher->baseUrl);
					DisplayHTMLPage(pWindow, url, NULL, getLangCode());
					estrDestroy(&url);
					break;

				case CL_STATE_LAUNCHERPAGELOADED:
				case CL_STATE_SETTINGVIEW:
				case CL_STATE_WAITINGFORPATCH:
				case CL_STATE_GETTINGFILES:
				case CL_STATE_READY:
					if(!launcher->accountLink)
					{
						accountLauncherLinkInit(launcher, false);
						accountValidateWaitForLink(&launcher->accountLink, 5);
					}
					
					if(!accountValidateStartLoginProcess(launcher->username))
					{
						// launcher->state remains in one of the states above
						DisplayMsg(pWindow, "Unable to contact account server");
					}
					else
					{
						launcher->state = CL_STATE_GETTINGPAGETICKET;
					}
				}
			}
			break;
		case CLMSG_RESTART_PATCH: // Restart the active patch, if any
			{
				ShardInfo_Basic *shard = (ShardInfo_Basic*)lParam;
				if(shard)
					postCommandPtr(launcher->queueToPCL, CLCMD_RESTART_PATCH, shard);
			}
			break;
		}
		break;
	case WM_APP_TRAYICON:
		{
			if(LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_RBUTTONUP)
			{
				WINDOWINFO wi = {0};
				wi.cbSize = sizeof(WINDOWINFO);
				GetWindowInfo(hDlg, &wi);
				if(!(wi.dwStyle & WS_VISIBLE) || IsIconic(hDlg))
				{
					ShowWindow(hDlg, SW_NORMAL);
					SetForegroundWindow(hDlg);
					if(!launcher->showTrayIcon)
						systemTrayRemove(hDlg);
				}
				else
				{
					HWND h = GetTopWindow(NULL);
					while(h != NULL)
					{
						if(!excludeWindow(h))
							break;
						h = GetNextWindow(h, GW_HWNDNEXT);
					}
					if(h == hDlg)
						// We are on top
						SendMessage(hDlg, WM_SYSCOMMAND, SC_MINIMIZE, (LPARAM)NULL);
					else
						SetForegroundWindow(hDlg);
				}
			}
			//else if(LOWORD(lParam) == WM_LBUTTONDOWN || LOWORD(lParam) == WM_RBUTTONDOWN)
			//{
			//	HWND h = GetTopWindow(NULL);
			//	U32 i = 0;
			//	TCHAR buf[100], buf2[100];
			//	foreground = GetForegroundWindow();
			//	printf("%p %p %p\n", hDlg, foreground, h);
			//	while(h != NULL)
			//	{
			//		i += 1;
			//		if(IsWindowVisible(h))
			//		{
			//			GetClassName(h, buf, ARRAY_SIZE_CHECKED(buf));
			//			GetWindowText(h, buf2, ARRAY_SIZE_CHECKED(buf2));
			//			printf("%u %p %s %s %s\n", i, h, buf, buf2, (IsIconic(h)?"icon":""));
			//		}
			//		h = GetNextWindow(h, GW_HWNDNEXT);
			//	}
			//	printf("\n");
			//}
		}
		break;
	case WM_GETDLGCODE:
		{
			LPMSG lpMsg = (LPMSG)lParam;
			if (lpMsg && lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_RETURN)
			{
				Errorf("Got a GETDLGCODE with a return");
				ProcessKeystrokes(pWindow, lpMsg[0]);
				return DLGC_WANTALLKEYS;
			}
			return DLGC_WANTALLKEYS;

		}
		break;
	case WM_KEYDOWN:
	case WM_CHAR:
	case WM_DEADCHAR:
	case WM_SYSKEYDOWN:
	case WM_SYSCHAR:
	case WM_SYSDEADCHAR:
	case WM_KEYLAST:
		{
			MSG dummy;

			// Do not process repeated keys
			// From http://msdn.microsoft.com/en-us/library/ms646280(VS.85).aspx
			// 30 - Specifies the previous key state. The value is 1 if the key is down before the message is sent, or it is zero if the key is up.
			if(lParam & 0x40000000)
				break;

			if(wParam == VK_RETURN)
			{
				switch(launcher->state)
				{
				case CL_STATE_LOGINPAGELOADED:
					PostMessage(pWindow->hWnd, WM_APP, 4, 0);
					break;
				case CL_STATE_WAITINGFORPATCH:
				case CL_STATE_READY:
					PostMessage(pWindow->hWnd, WM_APP, 2, 0);
					break;
				}
			}
			else
			{
				if(launcher->state == CL_STATE_LOGINPAGELOADED)
				{
					dummy.hwnd = hDlg;
					dummy.message = iMsg;
					dummy.wParam = wParam;
					dummy.lParam = lParam;
					ProcessKeystrokes(pWindow, dummy);
				}
			}
		}
		break;
	default:
		if(g_taskbar_create_message && g_taskbar_create_message == iMsg)
			systemTrayAdd(hDlg);
		break;
	}

	return FALSE;
}

// Debug command
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void PatchStreamingIsRequired(char *filename)
{
	s_debug_check_filespec = strdup(filename);
}

#include "autogen/NewControllerTracker_pub_h_ast.c"
