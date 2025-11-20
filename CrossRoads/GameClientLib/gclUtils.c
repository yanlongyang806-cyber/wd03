/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "GameClientLib.h"
#include "dynFxInterface.h"
#include "worldgrid.h"
#include "EntityLib.h"
#include "graphicslib.h"
#include "inputlib.h"
#include "trivia.h"
#include "renderlib.h"
#include "uimodaldialog.h"
#include "rdrstandarddevice.h"
#include "inputkeybind.h"
#include "gclextern.h"
#include "gclhandlemsg.h"
#include "inputGamepad.h"
#include "gclEntity.h"
#include "gclMapState.h"
#include "XboxThreads.h"
#include "Globalstatemachine.h"
#include "GameAccountDataCommon.h"
#include "gclutils.h"
#include "gcldemo.h"
#include "gclOptions.h"
#include "gclCamera.h"
#include "gclCommandParse.h"
#include "gclControlScheme.h"
#include "gclPlayerControl.h"
#include "gclSendToServer.h"
#include "Character.h"
#include "Character_target.h"
#include "ControllerLink.h"
#include "gfxHeadShot.h"
#include "CostumeCommon.h"
#include "PowerModes.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "TimedCallback.h"
#include "GenericDialog.h"
#include "utilitiesLib.h"
#include "Prefs.h"
#include "AppRegCache.h"
#include "../../libs/graphicsLib/GfxTextures.h"
#include "../../libs/graphicsLib/GfxDXT.h"
#include "WritePNG.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "GfxDebug.h"
#include "GameClientLibResource.h"
#include "../../libs/WorldLib/pub/dynSequencer.h"
#include "../../libs/WorldLib/pub/dynAnimInterface.h"
#include "../../libs/WorldLib/pub/dynSkeleton.h"
#include "winutil.h"
#include "ContinuousBuilderSupport.h"
#include "Player.h"
#include "PowersMovement.h"
#include "mission_common.h"
#include "mapstate_common.h"
#include "SavedPetCommon.h"
#include "WorldGrid.h"
#include "RoomConn.h"
#include "../RoomConnPrivate.h"
#include "../StaticWorld/WorldGridPrivate.h"
#include "structInternals.h"
#include "alerts.h"
#include "contactui_eval.h"
#include "contact_common.h"
#include "WorldLib.h"
#include "headshotUtils.h"
#include "autogen/headshotUtils_h_ast.h"
#include "NotifyEnum.h"
#include "NotifyCommon.h"
#include "Message.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_CMD_INT(gbConnectToController, ConnectToController) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
bool gbConnectToController = false;

U32 iDebugContainerID = 0;
AUTO_CMD_INT(iDebugContainerID, DebugContainerID) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

bool gbQueueErrorsEvenInProductionMode = false;

//global used to allow/disallow various testing features... set by AUTO_COMMAND SetTestingMode
//in gclCommandParse.c
bool gbGclTestingMode = false;


char gLocalHost[32] = "localhost";
AUTO_CMD_STRING(gLocalHost, localHost) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;

void gclAuxAssertFunc_SendAssertsToController(const char* expr, const char* errormsg, const char* filename, unsigned lineno);

NetComm *pDebugComm = NULL;


int gbSendAllErrorsToController = 0;

//tells the client to send all errors to the controller (for controller scripting)
//
//this used to always set DontDoAlerts. But the new mcp error screen doesn't want that to 
//happen. So I'm making the API a bit ugly just to keep things simple. So iSet=2 means 
//the preferred behavior for MCP error screen mode
AUTO_COMMAND ACMD_NOTESTCLIENT ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void SendAllErrorsToController(int iSet)
{
	gbSendAllErrorsToController = !!iSet;
	if (iSet)
	{
		if (iSet == 1)
		{
			gbDontDoAlerts = true;
			SetAuxAssertCB(gclAuxAssertFunc_SendAssertsToController);
		}
		else if (iSet == 2)
		{
			ErrorSetAlwaysReportDespiteDuplication(true);
		}

	}
}

// Duplicating this function without ACMD_EARLYCOMMANDLINE so it can be put into cmdline.txt
AUTO_COMMAND ACMD_NAME(SendAllErrorsToController) ACMD_NOTESTCLIENT ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void SendAllErrorsToControllerNotEarly(int iSet)
{
	SendAllErrorsToController(iSet);
}


void gclDebugLinkToLauncherHandleInput(Packet* pak, int cmd, NetLink* link,void *user_data)
{
	switch (cmd)
	{
	xcase FROM_LAUNCHERDEBUG_COMMAND:
		globCmdParse(pktGetStringTemp(pak));
		
	}
}

#define DEBUG_COMM_TIMEOUT_MS 0

NetLink *gclGetDebugLinkToLauncher(bool bFailureIsFatal)
{
	static NetLink *spDebugLauncherLink = NULL;
	static bool sbAlreadyFailed = false;

	if (sbAlreadyFailed)
	{
		return NULL;
	}

#if !_PS3
	if (isProductionMode() && !gbGclTestingMode)
	{
		sbAlreadyFailed = true;
		return NULL;
	}

//the xbox can't do DNSing, so if we don't have a numeric IP, fail
#if _XBOX
	if (strcmp(gLocalHost, "localhost") == 0)
	{
		sbAlreadyFailed = true;
		return NULL;
	}
#endif

	if (!pDebugComm)
	{
		pDebugComm = commCreate(DEBUG_COMM_TIMEOUT_MS, 1);
	}

	if (!spDebugLauncherLink)
	{
		spDebugLauncherLink = commConnect(pDebugComm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,gLocalHost,GetLauncherListenPort(),gclDebugLinkToLauncherHandleInput,0,0,0);
		linkConnectWait(&spDebugLauncherLink, g_isContinuousBuilder ? 200 : 2);

		if (bFailureIsFatal)
		{
			assertmsgf(spDebugLauncherLink, "Couldn't connect to launcher to report errors to controller, this is fatal in CBs");
		}
		if (!spDebugLauncherLink)
		{
			sbAlreadyFailed = true;
			return NULL;
		}
	}
#endif
	return spDebugLauncherLink;
}

static bool development_priority_boost;
// 0 - idle always, 1 - normal in FG, below in BG (like prod), 2 - below always (old behavior)
AUTO_CMD_INT(development_priority_boost, development_priority_boost) ACMD_COMMANDLINE;

static int process_priority;
// 0 - default, normal always; 1 - normal in foreground, below normal in background/alt-tabbed; 2 - high always
AUTO_CMD_INT(process_priority, process_priority) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC;

static bool force_hardware_warning = false;
// forcibly show the bad hardware warning dialog
AUTO_CMD_INT(force_hardware_warning, forcehardwarewarning) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_HIDE;

void gclUpdateThreadPriority(void)
{
#if !PLATFORM_CONSOLE
	int curActive = 1;
	static int current_priority = NORMAL_PRIORITY_CLASS;
	int desired_priority;

	PERFINFO_AUTO_START_FUNC();

	if (!gbNoGraphics)
	{
		curActive = (isProductionMode() || (development_priority_boost==1)) &&
			(!gGCLState.pPrimaryDevice || (gGCLState.pPrimaryDevice && !gfxIsInactiveApp()));
	}

	if (isProductionMode() && process_priority==0)
	{
		// Production mode default always
		desired_priority = NORMAL_PRIORITY_CLASS;
	} else if (process_priority==2)
	{
		desired_priority = HIGH_PRIORITY_CLASS;
	} else if(curActive)
	{
		// Old production mode, in foreground
		desired_priority = NORMAL_PRIORITY_CLASS; // Effective priority - 7 or 9
	}
	else
	{
		if (isProductionMode() || development_priority_boost)
			// Old production mode, in background
			desired_priority = BELOW_NORMAL_PRIORITY_CLASS; // Effective priority - 6
		else
			// Development always (don't want to be above the server)
			desired_priority = IDLE_PRIORITY_CLASS; // Effective priority - 4
	}

	if (desired_priority != current_priority)
	{
		SetPriorityClass(GetCurrentProcess(), desired_priority);
		current_priority = desired_priority;
	}

	PERFINFO_AUTO_STOP();
#endif
}

void gclAboutToExit(void)
{
	winCheckAccessibilityShortcuts(CheckWhen_Shutdown);
}


void gclUpdateRoamingCell(void){
	Vec3 center;
	
	if(gbNoGraphics)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC_PIX();
	
	gfxGetActiveCameraPos(center);
	
	dynPhysicsSetCenter(center);
	
	PERFINFO_AUTO_STOP_FUNC_PIX();
}

static void GimmeErrorDialog(ErrorMessage* errMsg, void *userdata)
{
    static bool break_into_debugger = 0;
	char *errString = errorFormatErrorMessage(errMsg);
	char author[200] = {0};
	if (errMsg->author)
	{
		if (strlen(errMsg->author) < 15)
			sprintf(author, "%s is Responsible", errMsg->author);
		else
			strcpy(author, errMsg->author);
	}
    if(break_into_debugger && IsDebuggerPresent())
        _DbgBreak();
	rdrSafeErrorMsg(errString, 0, author[0]?author:NULL, errMsg->bForceShow);
}

void gclHideWindowOnCrash(void)
{
	static bool doneOnce;
	if (doneOnce)
		return;
	doneOnce = true;

	if (SAFE_MEMBER(gGCLState.pPrimaryDevice, device))
		rdrAppCrashed(gGCLState.pPrimaryDevice->device);
}

void gclFatalErrorfCallback(ErrorMessage* errMsg, void *userdata)
{
	char *errString = errorFormatErrorMessage(errMsg);
#if !PLATFORM_CONSOLE
	char buffer[1024];

	if (errMsg->estrDetails)
		sprintf(buffer, "Fatal Error: %s\r\n\r\nTechnical Details: %s", errString, errMsg->estrDetails);
	else
		sprintf(buffer, "Fatal Error: %s", errString);
	
	if (!g_isContinuousBuilder && !gfxIsHeadshotServer())
	{
		gclHideWindowOnCrash();
		MessageBox(NULL, buffer, "Fatal Error", MB_ICONEXCLAMATION | MB_OK | MB_SYSTEMMODAL);
	}
#endif
	fatalerrorAssertmsg(0, errString);
}

int noPopUps;
// Disables pop-up errors, called internally, do not use this command, probably use -productionMode instead?
AUTO_CMD_INT(noPopUps, noPopUps) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

// Do not ever use this command
AUTO_CMD_INT(noPopUps, noPopUps2) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE;

#define MAX_ERRORS_OUT 200

void gclErrorfCallback(ErrorMessage* errMsg, void *userdata)
{
	char *errString = errorFormatErrorMessage(errMsg);
	if (!consoleIsCursorOnLeft())
		printf("\n");
	printfColor(COLOR_RED|COLOR_GREEN, "ERROR: %s\n", errString);
	if (!noPopUps && errMsg->errorCount < MAX_ERRORS_OUT && errMsg->bRelevant)
	{	
		if (errMsg->errorType == ERROR_ALERT && !g_isContinuousBuilder)
		{
			gclQueueError(NULL, errString, "Alert", (errMsg->author && errMsg->author[0]) ? errMsg->author : NULL, false, (void*)0x1);
		}
		else
		{
			GimmeErrorDialog(errMsg, userdata);
		}
	}
}

typedef struct QueuedError
{
	char *str, *title, *fault;
	int highlight;
} QueuedError;

void gclAuxAssertFunc_SendAssertsToController(const char* expr, const char* errormsg, const char* filename, unsigned lineno)
{
	NetLink *pDebugLink;
	char assertString[4096];


	sprintf(assertString, "ASSERTION FAILED: %s -- %s -- %s(%d)",
		expr, errormsg, filename, lineno);

	if (pDebugLink = gclGetDebugLinkToLauncher(g_isContinuousBuilder))
	{
		Packet *pPacket = pktCreate(pDebugLink, TO_LAUNCHER_ERROR);
#if !PLATFORM_CONSOLE
		pktSendBits(pPacket, 1, 0);
		pktSendBits(pPacket, 32, getpid());
#else
		pktSendBits(pPacket, 1, 1);
		pktSendBits(pPacket, 32, 0);
#endif
		pktSendString(pPacket, assertString);
		pktSendString(pPacket, "");
		pktSendString(pPacket, "");
		pktSendBits(pPacket, 32, 0);
		pktSend(&pPacket);	

		linkFlush(pDebugLink);
		Sleep(1000);
	}
}

void gclDebugFunc_SendControllerScriptingResult(int iResult, char *pString)
{
	NetLink *pDebugLink;
	
	if (pDebugLink = gclGetDebugLinkToLauncher(g_isContinuousBuilder))
	{
		Packet *pPacket = pktCreate(pDebugLink, TO_LAUNCHER_SETCONTROLLERSCRIPTINGRESULT);
#if !PLATFORM_CONSOLE
		pktSendBits(pPacket, 1, 0);
		pktSendBits(pPacket, 32, getpid());
#else
		pktSendBits(pPacket, 1, 1);
		pktSendBits(pPacket, 32, 0);
#endif
		pktSendBits(pPacket, 32, iResult);
		pktSendString(pPacket, pString);
		pktSend(&pPacket);	
	}
}

void gclDebugFunc_SendControllerScriptingTemporaryPause(int iNumSeconds, char *pReason)
{
	NetLink *pDebugLink;
	
	if (pDebugLink = gclGetDebugLinkToLauncher(false))
	{
		Packet *pPacket = pktCreate(pDebugLink, TO_LAUNCHER_CONTROLLERSCRIPTINGPAUSE);
#if !PLATFORM_CONSOLE
		pktSendBits(pPacket, 1, 0);
		pktSendBits(pPacket, 32, getpid());
#else
		pktSendBits(pPacket, 1, 1);
		pktSendBits(pPacket, 32, 0);
#endif

		pktSendBits(pPacket, 32, iNumSeconds);
		pktSendString(pPacket, pReason);
		pktSend(&pPacket);	
	}
}


static QueuedError **queued_errors;
// userdata is ignored and should be NULL
void gclQueueError(HWND hwnd, char *str, char* title, char* fault, int highlight, void *userdata)
{
	static bool bWarningShown = false;
	QueuedError *error = malloc(sizeof(*error));

	if (!gbSendAllErrorsToController && isProductionMode() && GameClientAccessLevel(NULL, IGNORE_UGC_MODIFICATIONS) && !bWarningShown)
	{
		bWarningShown = true;
		gclQueueError(hwnd, "You are seeing these pop-up errors in production mode because you are logged in with a character which has access level > 0.  Normal end-users will not see these messages.", "Pop-ups", NULL, 0, NULL);
	}
	error->str = str?strdup(str):NULL;
	error->title = title?strdup(title):NULL;
	error->fault = fault?strdup(fault):NULL;
	error->highlight = highlight;
	eaPush(&queued_errors, error);
}

void gclProcessQueuedErrors(bool bDisplayErrors)
{
	int i;
	bool bShowDialog = isDevelopmentMode() || GameClientAccessLevel(NULL, IGNORE_UGC_MODIFICATIONS) || gbQueueErrorsEvenInProductionMode;
	NetLink *pDebugLink = NULL;

	if (gbSendAllErrorsToController)
		pDebugLink = gclGetDebugLinkToLauncher(g_isContinuousBuilder);
	if (!pDebugLink && !bDisplayErrors)
		return;

	if (!pDebugLink && gGCLState.bInitialPopUp)
	{
		ui_ModalDialog("Title", "Message", ColorBlack, UIOk | UICopyToClipboard | UIEditFile | UIOpenFolder);
		gGCLState.bInitialPopUp = false;
	}

	for (i = 0; i < eaSize(&queued_errors); ++i)
	{
		QueuedError *error = queued_errors[i];
		if (error)
		{
			if (pDebugLink)
			{
				Packet *pPacket;
				
				pktCreateWithCachedTracker(pPacket, pDebugLink, TO_LAUNCHER_ERROR);
#if !PLATFORM_CONSOLE
				pktSendBits(pPacket, 1, 0);
				pktSendBits(pPacket, 32, getpid());
#else
				pktSendBits(pPacket, 1, 1);
				pktSendBits(pPacket, 32, 0);
#endif		
				pktSendString(pPacket, error->str);
				pktSendString(pPacket, error->title);
				pktSendString(pPacket, error->fault);
				pktSendBits(pPacket, 32, error->highlight);
				pktSend(&pPacket);	


			}
			else if (bShowDialog)
			{
				if (!(inpLevelPeek(INP_SHIFT) && inpLevelPeek(INP_CONTROL)) && errorGetVerboseLevel()!=2)
				{
					Color text_color = CreateColorRGB(0, 0, 0);
					if (queued_errors[i]->highlight)
						text_color = CreateColorRGB(255, 0, 0);
					ui_ModalDialog(queued_errors[i]->title, queued_errors[i]->str, text_color, UIOk | UICopyToClipboard | UIEditFile | UIOpenFolder);
				}
			}

			SAFE_FREE(error->str);
			SAFE_FREE(error->title);
			SAFE_FREE(error->fault);
			SAFE_FREE(queued_errors[i]);
		}
	}
	eaSetSize(&queued_errors, 0);
}

bool gclCreatePrimaryDevice(HINSTANCE hInstance)
{
	WindowCreateParams params={0};

	loadstart_printf("Creating primary rendering device");

	SAFE_FREE(gGCLState.pPrimaryDevice);
	gGCLState.pPrimaryDevice = calloc(sizeof(*gGCLState.pPrimaryDevice),1);

	gfxGetWindowSettings(&params);
	loadupdate_printf(" (%s)...", params.device_type);

	if (gGCLState.bForceThread)
		params.threaded = 1;
	else if (gGCLState.bNoThread)
		params.threaded = 0;
	params.display.allow_windowed_fullscreen = 1;
	params.icon = gGCLState.iconResource;

	params.window_title = regGetAppName(); // Haven't loaded any data yet, use the registry app name

	gGCLState.pPrimaryDevice->device = rdrCreateDevice(&params, hInstance, THREADINDEX_RENDER);

	if (!gGCLState.pPrimaryDevice->device)
	{
		SAFE_FREE(gGCLState.pPrimaryDevice);
		loadend_printf(" FAILED.");
		return false;
	}
	gGCLState.pPrimaryDevice->windowHandle = rdrGetWindowHandle(gGCLState.pPrimaryDevice->device);

	loadend_printf(" done.");

	return true;
}

#if !PLATFORM_CONSOLE

typedef struct SystemSpecsDialogData
{
	// input
	const char *message;

	// output
	bool doNotAskAgain;
	int action;
} SystemSpecsDialogData;

static SystemSpecsDialogData *g_systemspecswarning_data;

static void UTF8ToACP(const char *str, char *out, int len)
{
	wchar_t *wstr;
	int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	wstr = malloc(size * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, size);
	WideCharToMultiByte(CP_ACP, 0, wstr, -1, out, len, NULL, NULL);
	free(wstr);
}

LRESULT CALLBACK DlgSystemSpecsWarning (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	char buf[4096];
	switch (iMsg)
	{
	case WM_INITDIALOG:
		UTF8ToACP(TranslateMessageKeySafe("GameClientLib.FailedSystemSpecsTitle"), SAFESTR(buf));
		SetWindowText(hDlg, buf);
		UTF8ToACP(g_systemspecswarning_data->message, SAFESTR(buf));
		SetWindowText(GetDlgItem(hDlg, IDC_MESSAGE), buf);
		UTF8ToACP(TranslateMessageKeySafe("GameClientLib.ContinueAnyway"), SAFESTR(buf));
		SetWindowText(GetDlgItem(hDlg, IDC_PROMPT), buf);
		UTF8ToACP(TranslateMessageKeySafe("GameClientLib.YesLowered"), SAFESTR(buf));
		SetWindowText(GetDlgItem(hDlg, ID_YES_LOWERED), buf);
		UTF8ToACP(TranslateMessageKeySafe("GameClientLib.YesDefault"), SAFESTR(buf));
		SetWindowText(GetDlgItem(hDlg, ID_YES_DEFAULT), buf);
		UTF8ToACP(TranslateMessageKeySafe("GameClientLib.LoweredHelp"), SAFESTR(buf));
		SetWindowText(GetDlgItem(hDlg, IDC_LOWERED_HELP), buf);
		UTF8ToACP(TranslateMessageKeySafe("GameClientLib.DefaultHelp"), SAFESTR(buf));
		SetWindowText(GetDlgItem(hDlg, IDC_DEFAULT_HELP), buf);
		UTF8ToACP(TranslateMessageKeySafe("GameClientLib.QuitBuggingMe"), SAFESTR(buf));
		SetWindowText(GetDlgItem(hDlg, IDC_QUIT_BUGGING_ME), buf);
		UTF8ToACP(TranslateMessageKeySafe("GameClientLib.Cancel"), SAFESTR(buf));
		SetWindowText(GetDlgItem(hDlg, IDCANCEL), buf);
		// flash the window if it is not the focus
		flashWindow(hDlg);
		break;
	case WM_COMMAND:
		g_systemspecswarning_data->doNotAskAgain =  IsDlgButtonChecked(hDlg, IDC_QUIT_BUGGING_ME);
		g_systemspecswarning_data->action = LOWORD(wParam);
		if ( LOWORD(wParam) == IDCANCEL )
		{
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}
		if ( LOWORD(wParam) == ID_YES_LOWERED )
		{
			EndDialog(hDlg, IDOK);
			return TRUE;
		}
		if ( LOWORD(wParam) == ID_YES_DEFAULT )
		{
			EndDialog(hDlg, IDOK);
			return TRUE;
		}
		break;
	}

	return FALSE;
}

static void doSystemSpecsWarning(const char *message)
{
	if (gfxSkipSystemSpecsCheck())
		return;
	if (gbSendAllErrorsToController)
	{
		// Non-interactive mode, just errorf it
		Errorf("%s", message);
	} else {
		SystemSpecsDialogData data = {0};
		char specs_id[1024];
		bool bSkipDialog=false;
		if (isDevelopmentMode())
		{
			// Exclude the build number, just check for hardware change
			sprintf(specs_id, "%s_%d", system_specs.videoDriverVersion, system_specs.videoCardDeviceID);
		} else {
			U32 seconds = timeSecondsSince2000();
			seconds /= 60*60*24*31; // Round to month
			sprintf(specs_id, "%d_%s_%d", seconds, system_specs.videoDriverVersion, system_specs.videoCardDeviceID);
		}

		data.message = message;
		g_systemspecswarning_data = &data;

		if (!gfxGetSafeMode() && (data.action = GamePrefGetInt("SkipSystemSpecsValue", 0)))
		{
			if (stricmp(GamePrefGetString("SkipSystemSpecsID", "NONE"), specs_id)==0)
			{
				// They don't want to be bothered, and it's still the same version of everything
				bSkipDialog = true;
				data.doNotAskAgain = true;
			}
		}
		if (!bSkipDialog)
		{
			DialogBox(winGetHInstance(), (LPCTSTR)(IDD_SYSTEMSPECS), NULL, (DLGPROC)DlgSystemSpecsWarning);
		}

		if (IDCANCEL==data.action)
		{
			gclAboutToExit();
			exit(0);
		} else {
			// Don't report crashes if they fail system specs
			// JE: Leaving this on for a while so we collect some good data on unsupported cards for now
			// gbDontReportErrorsToErrorTracker = true;

			if (data.doNotAskAgain) {
				GamePrefStoreString("SkipSystemSpecsID", specs_id);
				GamePrefStoreInt("SkipSystemSpecsValue", data.action);
			} else {
				GamePrefStoreInt("SkipSystemSpecsValue", 0);
			}

			if (data.action == ID_YES_DEFAULT)
			{
				// Let things go as usual
			} else if (data.action == ID_YES_LOWERED)
			{
				// Disable postprocessing, outlining, etc - anything that creates
				//  a depth buffer or non-standard surface
				gfxSettingsSetMinimalOptions();
			}
		}
	}
	// Log that they are running with unsupported specs and are running anyway
	system_specs.isUnsupportedSpecs = 1;
	triviaPrintf("UnsupportedSpecs", "%d", 1);
}

#endif
#if !PLATFORM_CONSOLE
static bool g_shownDriverNag = false;
#endif

void gclCheckSystemSpecsEarly(void)
{
#if !PLATFORM_CONSOLE
	char *badmessage=NULL;

	if (!FindResource(winGetHInstance(), (LPCTSTR)(IDD_SYSTEMSPECS), RT_DIALOG))
	{
		// Could not find GameClientLib.rc, add this directly to the project
		assertmsg(0, "Missing required resource");
	}

	systemSpecsInit();
	if (system_specs.videoDriverState != VIDEODRIVERSTATE_OK)
	{
		const char * videoDriverMessageKey;

		msgLoadAllMessages();
		if (!badmessage)
			estrConcatf(&badmessage, "%s\n\n", TranslateMessageKeySafe("GameClientLib.FailedSystemSpecs"));
		if (isDevelopmentMode())
		{
			videoDriverMessageKey = system_specs.videoDriverState == VIDEODRIVERSTATE_OLD ? 
				"GameClientLib.OldDriverInternal" : "GameClientLib.DriverKnownBugs_NVIDIA";
		}
		else if (system_specs.videoCardVendorID == VENDOR_NV)
		{
			videoDriverMessageKey = system_specs.videoDriverState == VIDEODRIVERSTATE_OLD ? 
				"GameClientLib.OldDriver_NVIDIA" : "GameClientLib.DriverKnownBugs_NVIDIA";
		}
		else if (system_specs.videoCardVendorID == VENDOR_ATI)
		{
			videoDriverMessageKey = system_specs.videoDriverState == VIDEODRIVERSTATE_OLD ? 
				"GameClientLib.OldDriver_ATI" : "GameClientLib.DriverKnownBugs_ATI";
		}
		else if (system_specs.videoCardVendorID == VENDOR_INTEL)
		{
			videoDriverMessageKey = system_specs.videoDriverState == VIDEODRIVERSTATE_OLD ? 
				"GameClientLib.OldDriver_Intel" : "GameClientLib.DriverKnownBugs_Intel";
		}
		else
		{
			videoDriverMessageKey = "GameClientLib.OldDriver";
		}

		estrConcatf(&badmessage, "%s\n", TranslateMessageKeySafe(videoDriverMessageKey));
	}
	if (!gfxSettingsIsSupportedHardwareEarly() || force_hardware_warning)
	{
		msgLoadAllMessages();
		if (!badmessage)
			estrConcatf(&badmessage, "%s\n\n", TranslateMessageKeySafe("GameClientLib.FailedSystemSpecs"));
		estrConcatf(&badmessage, "%s\n", TranslateMessageKeySafe("GameClientLib.UnsupportedHardware"));
	}

	{
		// Check for 16-bit desktop color depth
		GfxResolution *desktop_res=NULL;
		rdrGetSupportedResolutions(&desktop_res, multimonGetPrimaryMonitor(), NULL);
		if (desktop_res->depth != 32)
		{
			msgLoadAllMessages();
			if (!badmessage)
				estrConcatf(&badmessage, "%s\n\n", TranslateMessageKeySafe("GameClientLib.FailedSystemSpecs"));
			estrConcatf(&badmessage, "%s\n", TranslateMessageKeySafe("GameClientLib.Not32bitDesktop"));
		}
	}


	if (badmessage) {
		doSystemSpecsWarning(badmessage);
		estrDestroy(&badmessage);
		g_shownDriverNag = true;
	}
#endif
}

void gclCheckSystemSpecsLate(void)
{
#if !PLATFORM_CONSOLE
	RdrDevice *device = gGCLState.pPrimaryDevice->device;
	char *badmessage=NULL;
	
	if (!gfxSettingsIsSupportedHardware(device))
	{
		msgLoadAllMessages();
		if (!badmessage)
			estrConcatf(&badmessage, "%s\n\n", TranslateMessageKeySafe("GameClientLib.FailedSystemSpecs"));
		estrConcatf(&badmessage, "%s\n", TranslateMessageKeySafe("GameClientLib.UnsupportedHardware"));
	}

	//only set the old driver if we are a DX10 level card
	if (system_specs.videoCardVendorID == VENDOR_ATI && rdrSupportsFeature(device, FEATURE_DX10_LEVEL_CARD))
	{
		if (system_specs.videoDriverState == VIDEODRIVERSTATE_OK && is_old_ati_catalyst)
			system_specs.videoDriverState = VIDEODRIVERSTATE_OLD;
	}

	if (system_specs.isUsingD3DDebug)
	{
		msgLoadAllMessages();
		estrConcatf(&badmessage, "%s\n", TranslateMessageKeySafe("GameClientLib.D3DDebugWarning"));
	}
	//maybe it got set by now (eg ati needs to be checked after the device is created
	if (system_specs.videoDriverState && !g_shownDriverNag)
	{
		msgLoadAllMessages();
		if (!badmessage)
			estrConcatf(&badmessage, "%s\n\n", TranslateMessageKeySafe("GameClientLib.FailedSystemSpecs"));
		if (isDevelopmentMode())
		{
			estrConcatf(&badmessage, "%s\n", TranslateMessageKeySafe("GameClientLib.OldDriverInternal"));
		} else if (system_specs.videoCardVendorID == VENDOR_NV) {
			estrConcatf(&badmessage, "%s\n", TranslateMessageKeySafe("GameClientLib.OldDriver_NVIDIA"));
		} else if (system_specs.videoCardVendorID == VENDOR_ATI) {
			estrConcatf(&badmessage, "%s\n", TranslateMessageKeySafe("GameClientLib.OldDriver_ATI"));
		} else if (system_specs.videoCardVendorID == VENDOR_INTEL) {
			estrConcatf(&badmessage, "%s\n", TranslateMessageKeySafe("GameClientLib.OldDriver_Intel"));
		} else {
			estrConcatf(&badmessage, "%s\n", TranslateMessageKeySafe("GameClientLib.OldDriver"));
		}
	}

	if (badmessage) {
		// ensure the window is hidden so the dialog is visible
		rdrShowWindow(device, SW_HIDE);
		doSystemSpecsWarning(badmessage);
		// if the user doesn't kill the app, show the window
		rdrShowWindow(device, SW_SHOWDEFAULT);
		estrDestroy(&badmessage);
	}
#endif
}

bool gclCreateDummyPrimaryDevice(HINSTANCE hInstance)
{
	loadstart_printf("Creating dummy primary rendering device...");

	SAFE_FREE(gGCLState.pPrimaryDevice);
	gGCLState.pPrimaryDevice = calloc(sizeof(*gGCLState.pPrimaryDevice),1);

	loadend_printf(" done.");

	return true;
}

void gclRegisterPrimaryInputDevice(HINSTANCE hInstance)
{
	if (!gGCLState.pPrimaryDevice)
		return;

	loadstart_printf("Registering primary Input device...");

    gGCLState.pPrimaryDevice->inp_device = inpCreateInputDevice(gGCLState.pPrimaryDevice->device,hInstance,
		keybind_ExecuteKey, rdr_state.unicodeRendererWindow);
	// Ignore input until the application has completely initialized (exiting gclInit)
	gclBeginIgnoringInput();

    loadend_printf(" done.");
}

void gclBeginIgnoringInput()
{
	inpBeginIgnoringInput();
}

void gclStopIgnoringInput()
{
	inpStopIgnoringInput();
}

void gclRegisterPrimaryRenderingDevice(HINSTANCE hInstance)
{
	if (!gGCLState.pPrimaryDevice)
		return;

	loadstart_printf("Registering primary rendering device...");
	if (gGCLState.pPrimaryDevice->device)
	{
//		gGCLState.pPrimaryDevice->inp_device = inpCreateInputDevice(gGCLState.pPrimaryDevice->device,hInstance,keybind_ExecuteKey);
		rdrSetTitle(gGCLState.pPrimaryDevice->device, GetProductDisplayName(getCurrentLocale())?GetProductDisplayName(getCurrentLocale()):"GameClient");
		gfxRegisterDevice(gGCLState.pPrimaryDevice->device, gGCLState.pPrimaryDevice->inp_device, true);
	}
	// Set default camera in case client doesn't want to initialize it
	gfxInitCameraController(&gGCLState.pPrimaryDevice->gamecamera, gfxFreeCamFunc, NULL);
	gclExternInitCamera(&gGCLState.pPrimaryDevice->gamecamera);
	gfxInitCameraController(&gGCLState.pPrimaryDevice->freecamera, gfxFreeCamFunc, NULL);
	gfxInitCameraController(&gGCLState.pPrimaryDevice->democamera, gfxDemoCamFunc, NULL);
	gfxInitCameraController(&gGCLState.pPrimaryDevice->cutscenecamera, gclCutsceneCamFunc, NULL);
	gfxInitCameraController(&gGCLState.pPrimaryDevice->contactcamera, contactCutSceneCameraFunc, NULL);
	gGCLState.pPrimaryDevice->activecamera = &gGCLState.pPrimaryDevice->gamecamera;
	loadend_printf(" done.");
}

bool gclIsPrimaryRenderingDeviceInactive()
{
	if (!gGCLState.pPrimaryDevice->device)
		return false;
	return rdrIsDeviceInactive(gGCLState.pPrimaryDevice->device);
}

static F32 stickDeadAngle = 15.f;
static S32 stickControlledEntityIndex;
static S32 stickDebug;

AUTO_CMD_FLOAT(stickDeadAngle,stickDeadAngle) ACMD_ACCESSLEVEL(9);
AUTO_CMD_INT(stickControlledEntityIndex,stickControlledEntityIndex) ACMD_ACCESSLEVEL(9);
AUTO_CMD_INT(stickDebug,stickDebug) ACMD_ACCESSLEVEL(9);

// gets the stick direction (in radians) and how far it's pushed [0-1]
void gclUtil_GetStick(SA_PARAM_OP_VALID Entity *e, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *pfScale, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *pfPitch, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *pfYaw)
{
	F32 x = 0.0f;
	F32 y = 0.0f;
	F32 stickDeadAngleRad = RAD(stickDeadAngle);
	F32 fNewThrottle = 0.0f;
	int j;

	if(gGCLState.pPrimaryDevice->activecamera == &gGCLState.pPrimaryDevice->gamecamera
		&& !gGCLState.bLockPlayerAndCamera)
	{
		gamepadGetLeftStick(&x, &y);
	}

	// Dist
	*pfScale = sqrt(SQR(x) + SQR(y));

	if(stickDebug)
	{
		gfxDebugPrintfQueue("stick xy = %1.3f, %1.3f, len = %1.3f",
			x, y, *pfScale);
	}

	if(*pfScale > 0.f)
	{
		x /= *pfScale;
		y /= *pfScale;

		MINMAX1(*pfScale, 0.f, 1.f);

		x *= *pfScale;
		y *= *pfScale;
	}

	// Pitch
	*pfPitch = -y;

	// Yaw
	*pfYaw = atan2f(x, y);
	for(j=0; j<4; j++)
	{
		F32 center = j * RAD(90.f);
		F32 diff = subAngle(*pfYaw, center);
		F32 diffAbs = fabs(diff);

		if(diffAbs < stickDeadAngleRad)
		{
			*pfYaw = center;
			break;
		}

		if(diffAbs <= RAD(45.f))
		{
			diff += diff < 0 ? stickDeadAngleRad : -stickDeadAngleRad;
			diff *= RAD(45.f) / (RAD(45.f) - stickDeadAngleRad);
			*pfYaw = addAngle(center, diff);
			break;
		}
	}

	

	if(stickDebug)
	{
		gfxDebugPrintfQueue("stick xy = %1.3f, %1.3f, len = %1.3f, pitch = %1.3f, yaw = %1.3f, throttle = %1.3f",
			x, y, *pfScale, *pfPitch, *pfYaw, fNewThrottle);
	}
}



void gclUtil_UpdatePlayerMovement(const FrameLockedTimer* flt)
{
	Entity *			e = entActivePlayerPtr();
	U32					milliseconds;
	F32					fFrameTime;
	MovementManager*	mm;

	if (!e)
		return;

	PERFINFO_AUTO_START_FUNC_PIX();

	if(!mmGetLocalManagerByIndex(&mm, 0))
	{
		PERFINFO_AUTO_STOP_FUNC_PIX();
		return;
	}

	if(	gclServerIsDisconnected() ||
		gclServerTimeSinceRecv() > 5.f)
	{
		mmDisabledHandleCreate(&e->mm.mdhDisconnected, mm, __FILE__, __LINE__);

		PERFINFO_AUTO_STOP_FUNC_PIX();
		return; 
	}

	mmDisabledHandleDestroy(&e->mm.mdhDisconnected);

	// logging 
	if(e)
	{
		Vec3 pos;
		entGetPos(e, pos);
		mmLog(	e->mm.movement,
				NULL,
				"[gcl] %s pos (%1.2f, %1.2f, %1.2f).",
				__FUNCTION__,
				vecParamsXYZ(pos));
	}

	frameLockedTimerGetCurTimes(flt, &fFrameTime, &milliseconds, NULL);

	
	gclPlayerControl_Update(e, mm, fFrameTime, milliseconds);

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

// Name: gclSndVelCB
// Desc: Callback for setting a Vec3 with the player's current velocity. 
// Args: vel: Vec3 to receive the velocity
void gclSndVelCB(Vec3 vel)
{
	//EntityPhysics *physics;
	Entity*	pTempPlayerEnt = entActivePlayerPtr();
	// TODO: This correctly.
	//mmGetPhysics(pTempPlayerEnt->movement, &physics);
	//epGetTotalVel(physics, vel);
	zeroVec3(vel);
}


static F32 gclSndEntTimeSinceLastDamaged(SA_PARAM_NN_VALID Entity *pEnt, F32* pfTime)
{
	if(pEnt && pEnt->pEntUI && pEnt->pEntUI->uiLastDamaged)
	{
		F32 fTime = (gGCLState.totalElapsedTimeMs - pEnt->pEntUI->uiLastDamaged) / 1000.0;
		if ( pfTime && fTime < (*pfTime) )
		{
			(*pfTime) = fTime;
		}
		return fTime;
	}
	return FLT_MAX;
}

static void gclSndEntTimeSinceLastDamagedNoReturn(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, F32* pfTime)
{
	gclSndEntTimeSinceLastDamaged(pEnt, pfTime);
}


bool gclSndPlayerInCombat(void)
{
	bool result = false;
	Entity*	pEntity = entActivePlayerPtr();
	static int lastState = 0;

	if(pEntity && pEntity->pChar)
	{
		F32 fTime = FLT_MAX;
		gclSndEntTimeSinceLastDamaged( pEntity, &fTime );
		Entity_ForEveryPet( PARTITION_CLIENT, pEntity, gclSndEntTimeSinceLastDamagedNoReturn, &fTime, true, true );

		if(character_HasMode(pEntity->pChar, kPowerMode_Combat))
		{
			if(fTime < 0.75)
			{
				//if(gGCLState.totalElapsedTimeMs / 1000.0 < )
				result = true;
				lastState = 1;
			}

			if(lastState == 1) 
			{
				result = true;
			}
		}
		else
		{
			result = false;
			lastState = 0;
		}
	
	}

	return result;
}

OpenMission *gclSndGetActiveOpenMission()
{
	OpenMission *openMission = NULL;

	MapState *pState = mapStateClient_Get();
	Entity*	pEntity = entActivePlayerPtr();
	if(pEntity && pState)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
		if (pInfo)
		{
			openMission = mapState_OpenMissionFromName(pState, pInfo->pchCurrentOpenMission);
		}
	}

	return openMission;
}

void gclSndSetTalkingBit(U32 entRef, bool bEnabled)
{
	Entity *ent = entFromEntityRefAnyPartition(entRef);
	if(ent)
	{
		DynSkeleton *pSkeleton = dynSkeletonFromGuid(ent->dyn.guidSkeleton);
		if(pSkeleton)
		{
			if (!gConf.bNewAnimationSystem)
				dynSkeletonSetTalkBit(pSkeleton, bEnabled);
			else if (bEnabled)
				dynSkeletonSetAudioStanceWord(pSkeleton, allocAddString("Talking"));
			else
				dynSkeletonClearAudioStanceWord(pSkeleton, allocAddString("Talking"));
		}
	}
}

U32 gclSndAcctToEntref(ContainerID acctid)
{
	Entity *e = entFromAccountID(acctid);

	if(e)
		return entGetRef(e);

	return 0;
}

const char *gclSndGetEntNameByRef(U32 entRef)
{
	const char *pchResult = NULL;
	Entity *ent = entFromEntityRefAnyPartition(entRef);
	if(ent)
	{
		pchResult = entGetLocalName(ent);
	}
	return pchResult;
}

// return true if ent was found
bool gclSndGetEntPositionByRef(U32 entRef, Vec3 entityPos)
{
	bool result = false;
	Entity *ent = entFromEntityRefAnyPartition(entRef);
	if(ent)
	{
		entGetPos(ent, entityPos);
		result = true;
	}

	return result;
}

int gclSndPlayerExistsCB(void)
{
	Entity*	pTempPlayerEnt = entActivePlayerPtr();

	return pTempPlayerEnt!=NULL;
}

void gclSndPlayerVoiceCB(const char** unOut, const char **pwOut, int *idOut)
{
	Entity *e = entActivePlayerPtr();
	GameAccountData* gad = entity_GetGameAccount(e);

	if(gad)
	{
		*unOut = gad->pchVoiceUsername;
		*pwOut = gad->pchVoicePassword;
		*idOut = gad->iVoiceAccountID;
	}
}

void gclSndGetPlayerMatCB(Mat4 mat)
{
	Entity*	pTempPlayerEnt = entActivePlayerPtr();
	if (pTempPlayerEnt)
	{
		entGetBodyMat(pTempPlayerEnt, mat);
	}
}

void gclSndVerifyVoice(void)
{
	ServerCmd_ServerChat_VerifyVoice(); 
}

// Name: gclSndCameraMatrixCB
// Desc: Callback for setting a Vec4 with the camera's position.
// Args: mat: Mat4 to receive the position/orientation
void gclSndCameraMatCB(Mat4 mat)
{
	gfxGetActiveCameraMatrix(mat);
}

void gclSvNotifyJoin(void)
{
	notify_NotifySend(NULL, kNotifyType_VoiceChannelJoin, TranslateMessageKey("VoiceChannel.Join"), NULL, NULL);
}

void gclSvNotifyLeave(void)
{
	notify_NotifySend(NULL, kNotifyType_VoiceChannelLeave, TranslateMessageKey("VoiceChannel.Leave"), NULL, NULL);
}

void gclSvNotifyFailure(void)
{
	notify_NotifySend(NULL, kNotifyType_VoiceChannelFailure, TranslateMessageKey("VoiceChannel.Failure"), NULL, NULL);
}

int gclCutsceneActiveCB(void)
{
	return gGCLState.bCutsceneActive;
}

//controller scripting support
void OVERRIDE_LATELINK_ControllerScript_Succeeded(void)
{
	gclDebugFunc_SendControllerScriptingResult(1, "");
}
void OVERRIDE_LATELINK_ControllerScript_Failed(char *pFailureString)
{
	gclDebugFunc_SendControllerScriptingResult(-1, pFailureString);
}

void OVERRIDE_LATELINK_ControllerScript_TemporaryPauseInternal(int iNumSeconds, char *pReason)
{
	//in makebins mode, talk directly to CB.exe... in non-makebins mode, go through launcher and controller
	if(gbMakeBinsAndExit)
	{
		CBSupport_PauseTimeout(iNumSeconds);
	}
	else
	{
		gclDebugFunc_SendControllerScriptingTemporaryPause(iNumSeconds, pReason);
	}
}

AUTO_COMMAND;
void RunCommandAndReturnStringToControllerScripting(ACMD_SENTENCE pLocalCommand)
{
	char *pRetString = NULL;


	if (cmdParseAndReturn(pLocalCommand, &pRetString, CMD_CONTEXT_HOWCALLED_CONTROLLER_SCRIPTING))
	{
		gclDebugFunc_SendControllerScriptingResult(1, pRetString);
	}
	else
	{
		gclDebugFunc_SendControllerScriptingResult(-1, "");
	}
	
	estrDestroy(&pRetString);
}


void gclReportStateToController(char *pStateString)
{
	
	NetLink *pDebugLink;
	
	if (pDebugLink = gclGetDebugLinkToLauncher(g_isContinuousBuilder))
	{
		Packet *pPacket = pktCreate(pDebugLink, TO_LAUNCHER_REPORTCLIENTSTATE);
#if !PLATFORM_CONSOLE
		pktSendBits(pPacket, 1, 0);
		pktSendBits(pPacket, 32, getpid());
#else
		pktSendBits(pPacket, 1, 1);
		pktSendBits(pPacket, 32, 0);
#endif	
		pktSendString(pPacket, pStateString);
		pktSend(&pPacket);	
	}	
}

void gclUtilsOncePerFrame(void)
{
	if (pDebugComm)
	{
		commMonitor(pDebugComm);
	}
}



void DEFAULT_LATELINK_gclMakeAllOtherBins(void)
{
	
}

//tells the client to send its state to the controller (for scripting)
AUTO_COMMAND ACMD_CMDLINE ACMD_HIDE;
void SendStateToController(int iSend)
{
	if (iSend)
	{
		GSM_SetStatesChangedCB(gclReportStateToController);
	}
}


//given a short name to avoid xbox command line overflow
AUTO_COMMAND ACMD_NAME(RCOLDL) ACMD_CMDLINE ACMD_HIDE;
void ReceiveCommandsOnLauncherDebugLink(int iReceive)
{
#if !PLATFORM_CONSOLE
	assertmsg(0, "ReceiveCommandsOnLauncherDebugLink called on non-XBOX... use testClient interface instead");
#endif

	if (iReceive)
	{
		NetLink *pDebugLink;
		
		SetTestingMode(1);

		if (pDebugLink = gclGetDebugLinkToLauncher(g_isContinuousBuilder))
		{
			Packet *pPak = pktCreate(pDebugLink, TO_LAUNCHER_I_AM_XBOX_CLIENT);
			pktSend(&pPak);
		}
	}
}


AUTO_RUN;
void SetClientChecksCommandLineCommands(void)
{
	gbLimitCommandLineCommands = true;
}

void gclUpdateControllerConnection(void)
{
	if(!gbConnectToController)
	{
		return;
	}
	
	if (isProductionMode())
	{
		gbConnectToController = false;
		return;
	}

	if (GetControllerLink())
	{
		UpdateControllerConnection();
	}
	else
	{
		AttemptToConnectToController(false, NULL, false);
	}
}

U32 OVERRIDE_LATELINK_GetAppGlobalID(void)
{
	return iDebugContainerID;
}

void* OVERRIDE_LATELINK_GetAppIDStr(void)
{
	static char buf[MAX_PATH] = {0};
	Entity *e = entActivePlayerPtr();
	
	if(e)
		sprintf(buf, "%d %s", iDebugContainerID, ENTDEBUGNAME(e));
	else
		sprintf(buf, "%d [NOENT]", iDebugContainerID);

	return buf;
}

typedef struct
{
	int iRequestID;
	PlayerCostume *pPlayerCostume;
	BasicTexture *pTexture;
} HeadshotForHeadshotServerHandle;

void HeadshotForHeadshotServerDone(HeadshotForHeadshotServerHandle *pUserData)
{
	PERFINFO_AUTO_START_FUNC();
	StructDestroy(parse_PlayerCostume, pUserData->pPlayerCostume);
	SendCommandStringToTestClientf("HeadshotComplete %d", pUserData->iRequestID);
	if (pUserData->pTexture)
	{	
		texUnloadRawData(pUserData->pTexture);
	}
			
	free(pUserData);
	PERFINFO_AUTO_STOP();
}

static bool WriteHeadshotForHeadshotServer_GetTexture(const char* pBGTextureName, int iRequestID, BasicTexture **ppTextureOut)
{
	if (pBGTextureName && pBGTextureName[0])
	{
		if (!texFind(pBGTextureName, true))
		{
			SendCommandStringToTestClientf("HeadshotFailed %d Failed. unknown texture %s", iRequestID, pBGTextureName);
			return false;
		}

		*ppTextureOut = texLoadRawData(pBGTextureName, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);

		if (!*ppTextureOut)
		{
			SendCommandStringToTestClientf("HeadshotFailed %d Failed. Couldn't load texture %s", iRequestID, pBGTextureName);
			return false;
		}
	}

	return true;
}

static void WriteHeadshotForHeadshotServer_SetBitFieldGroup(const char * pPoseString, bool bHeadshot, DynBitFieldGroup * bfg) 
{
	char *pPoseInfo = NULL;
	estrStackCreate(&pPoseInfo);
	estrCopy2(&pPoseInfo, pPoseString);
	if(strcmp(pPoseInfo, "") == 0)
	{
		estrAppend2(&pPoseInfo, "IDLE");
	}
	if( bHeadshot )
	{
		estrAppend2(&pPoseInfo, " HEADSHOT");
	}
	estrAppend2(&pPoseInfo, " NOLOD");
	estrTrimLeadingAndTrailingWhitespace(&pPoseInfo);
	dynBitFieldGroupAddBits(bfg, pPoseInfo, true);
	estrDestroy(&pPoseInfo);
}

static void WriteHeadshotForHeadshotServer_ReadPlayerCostume(const char *pCostumeString, HeadshotForHeadshotServerHandle *pHandle, bool bCostumeV0) 
{
	char *pUnescapedString = NULL;
	estrStackCreate(&pUnescapedString);
	estrAppendUnescaped(&pUnescapedString, pCostumeString);
	if(bCostumeV0)
	{
		PlayerCostumeV0 *pTempCostume = StructCreate(parse_PlayerCostumeV0);
		ParserReadText(pUnescapedString, parse_PlayerCostumeV0, pTempCostume, 0);
		pHandle->pPlayerCostume = CONTAINER_RECONST(PlayerCostume,costumeLoad_UpgradeCostumeV0toV5(CONTAINER_NOCONST(PlayerCostumeV0, pTempCostume)));
	}
	else
	{
		pHandle->pPlayerCostume = StructCreate(parse_PlayerCostume);
		ParserReadText(pUnescapedString, parse_PlayerCostume, pHandle->pPlayerCostume, 0);
	}
	estrDestroy(&pUnescapedString);
}

static void WriteHeadshotForHeadshotServer_Error(BasicTexture *pTexture, WLCostume *pWLCostume, WLCostume ***peaSubCostumes, HeadshotForHeadshotServerHandle *pHandle) 
{
	if (pTexture)
	{
		texUnloadRawData(pTexture);
	}
	if (pWLCostume)
	{
		wlCostumeFree(pWLCostume);
	}
	if (peaSubCostumes)
	{
		FOR_EACH_IN_EARRAY(*peaSubCostumes, WLCostume, pSubCostume)
		{
			wlCostumeFree(pSubCostume);
		}
		FOR_EACH_END;
	}

	StructDestroy(parse_PlayerCostume, pHandle->pPlayerCostume);
	//SendCommandStringToTestClientf("HeadshotFailed %d Failed. Incomplete costume", pHandle->iRequestID);
	free(pHandle);
}

static WLCostume *WriteHeadshotForHeadshotServer_CheckDictForCostume(WLCostume *pWLCostume, WLCostume ***peaSubCostumes) 
{
	if (RefSystem_ReferentFromString("wlCostume", pWLCostume->pcName))
	{
		const char* name = pWLCostume->pcName;
		wlCostumeFree(pWLCostume);
		FOR_EACH_IN_EARRAY(*peaSubCostumes, WLCostume, pSubCostume)
		{
			wlCostumeFree(pSubCostume);
		}
		FOR_EACH_END;
		return (WLCostume*)RefSystem_ReferentFromString("wlCostume", name);
	}
	else
	{
		RefSystem_AddReferent("wlCostume", pWLCostume->pcName, pWLCostume);
		FOR_EACH_IN_EARRAY(*peaSubCostumes, WLCostume, pSubCostume)
		{
			wlCostumePushSubCostume(pSubCostume, pWLCostume);
		}
		FOR_EACH_END;
		return pWLCostume;
	}
}

// The workhorse for saving out headshots for the headshot server.
static void WriteHeadshotForHeadshotServerEx(
		const char *pFileName,
		int iRequestID,
		U32 eContainerType, 
		int iContainerID,
		const char *pCostumeString,
		Vec3 size,
		const char *pBGTextureName,
		Vec3 camPos, 
		Vec3 camDir,
		const char *pPoseString, 
		float animDelta,
		F32 fFOVy,
		const char* pchSky,
		const char* pchFrame,
		Color BackgroundColor,
		bool bForceBodyshot,
		bool bCostumeV0)
{
	HeadshotForHeadshotServerHandle *pHandle;
	WLCostume *pWLCostume;
	BasicTexture *pTexture = NULL;
	DynBitFieldGroup *bfg;
	WLCostume **eaSubCostumes = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!WriteHeadshotForHeadshotServer_GetTexture(pBGTextureName, iRequestID, &pTexture))
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	
	pHandle = calloc(sizeof(HeadshotForHeadshotServerHandle), 1);

	pHandle->iRequestID = iRequestID;
	pHandle->pTexture = pTexture;
	
	WriteHeadshotForHeadshotServer_ReadPlayerCostume(pCostumeString, pHandle, bCostumeV0);
	bfg = calloc(sizeof(DynBitFieldGroup), 1);
	WriteHeadshotForHeadshotServer_SetBitFieldGroup(pPoseString, !bForceBodyshot, bfg);
	pWLCostume = costumeGenerate_CreateWLCostume(pHandle->pPlayerCostume, NULL, NULL, NULL, NULL, NULL, NULL, "Entity.", eContainerType, iContainerID, true, &eaSubCostumes);
	if (pWLCostume)
	{
		if (!pWLCostume->bComplete)
		{
			SendCommandStringToTestClientf("HeadshotFailed %d Failed. Incomplete costume", pHandle->iRequestID);
			WriteHeadshotForHeadshotServer_Error(pTexture, pWLCostume, &eaSubCostumes, pHandle);
			PERFINFO_AUTO_STOP();
			return;
		}

		pWLCostume = WriteHeadshotForHeadshotServer_CheckDictForCostume(pWLCostume, &eaSubCostumes);

		if(!pchFrame && !camPos && !camDir && stricmp(GetShortProductName(), "FC") != 0)
		{
			pchFrame = "Default";
		}

		if (pchFrame && *pchFrame)
		{
			// Uses a "frame" to specify what to show in the headshot
			gfxHeadshotCaptureCostumeAndSave(pFileName, 
											 size[0], 
											 size[1], 
											 pWLCostume, 
											 pTexture,
											 pchFrame, 
											 BackgroundColor,
											 false, 
											 bfg, 
											 NULL, 
											 NULL,
											 fFOVy,
											 pchSky,
											 HeadshotForHeadshotServerDone, 
											 pHandle, 
											 NULL);
		}
		else
		{
			// Uses camPos and camDir to specify what to show in the headshot
			gfxHeadshotCaptureCostumeSceneAndSave(pFileName, 
												  size[0], 
												  size[1], 
												  pWLCostume, 
												  pTexture,
												  BackgroundColor,
												  bfg, 
												  NULL,
												  NULL,
												  camPos, 
												  camDir, 
												  fFOVy, 
												  pchSky, 
												  HeadshotForHeadshotServerDone, 
												  pHandle,
												  animDelta, 
												  bForceBodyshot,
												  NULL);
		}
	}
	else
	{
		SendCommandStringToTestClientf("HeadshotFailed %d Failed to create WLCostume", pHandle->iRequestID);
		WriteHeadshotForHeadshotServer_Error(pTexture, NULL, NULL, pHandle);
	}

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND;
void WriteHeadshotForHeadshotServer(const char* pchParams, const char* pchCostume)
{
	HeadshotRequestParams *pParams = StructCreate(parse_HeadshotRequestParams);
	if (ParserReadTextEscaped(&pchParams, parse_HeadshotRequestParams, pParams, 0))
	{
		HeadshotStyleDef *pStyle = NULL;
		const char *pchBackground = NULL;
		const char *pchAnimBits = NULL;
		const char *pchSky = NULL;
		const char *pchFrame = NULL;
		float fFoV = -1;
		Color color;
		F32* pfCamPos = NULL;
		F32* pfCamDir = NULL;
		float fAnimDelta = 0;

		if (pParams->pchStyle)
		{
			pStyle = RefSystem_ReferentFromString("HeadshotStyleDef", pParams->pchStyle);
			if(!pStyle)
			{
				SendCommandStringToTestClientf("HeadshotFailed request: %d. Undefined style '%s'", pParams->iRequestID, pParams->pchStyle);
				StructDestroy(parse_HeadshotRequestParams, pParams);
				return;
			}
		}

		if (pStyle)
		{
			pchBackground = pStyle->pchBackground;
			pchAnimBits = pStyle->pchAnimBits;
			pchSky = pStyle->pchSky;
			pchFrame = pStyle->pchFrame;
			fFoV = contact_HeadshotStyleDefGetFOV(pStyle, -1);
			color = colorFromRGBA(pStyle->uiBackgroundColor);
		}
		else
		{
			pchBackground = pParams->pBGTextureName;
			pchAnimBits = pParams->pPoseString;
			pchFrame = pParams->pchFrame;
			fAnimDelta = pParams->animDelta;
			color = ColorBlack;
			pfCamPos = pParams->camPos;
			pfCamDir = pParams->camDir;
		}

		WriteHeadshotForHeadshotServerEx(
			pParams->pFileName,
			pParams->iRequestID,
			pParams->eContainerType,
			pParams->iContainerID,
			pchCostume,
			pParams->size,
			pchBackground, 
			pfCamDir, 
			pfCamPos, 
			pchAnimBits,
			fAnimDelta,
			fFoV, 
			pchSky,
			pchFrame,
			pParams->bTransparent ? ColorTransparent : color,
			pParams->bForceBodyshot,			
			pParams->bCostumeV0);
	}
	StructDestroy(parse_HeadshotRequestParams, pParams);
}

typedef struct
{
	char *pFileName; //strduped
	int iRequestID;
	BasicTexture *pHeader;
} TextureForHeadshotServerHeaderRequestHandle;

static TextureForHeadshotServerHeaderRequestHandle **sppTextureRequestHandles = NULL;

void CheckForCompleteTextureRequests(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for (i=eaSize(&sppTextureRequestHandles) - 1; i >= 0; i--)
	{
		TextureForHeadshotServerHeaderRequestHandle *pHandle = sppTextureRequestHandles[i];

		if (pHandle->pHeader->tex_is_loaded & RAW_DATA_BITMASK)
		{
			TexReadInfo *rawInfo = texGetRareData(pHandle->pHeader->actualTexture)->bt_rawInfo;
			bool bResult;
			char errorString[1024] = "Unknown Error";

			assert(rawInfo);

			verify(uncompressRawTexInfo(rawInfo,textureMipsReversed(pHandle->pHeader->actualTexture)));

			if (rawInfo->tex_format == RTEX_BGRA_U8)
			{
				//swap R and B for DirectX
				int pixelNum;

				for (pixelNum = 0; pixelNum < rawInfo->width * rawInfo->height; pixelNum++)
				{
					PngPixel4 *pPixel = (PngPixel4*) (rawInfo->data_refcounted + pixelNum * sizeof(PngPixel4));
					U8 temp = pPixel->r;
					pPixel->r = pPixel->b;
					pPixel->b = temp;		
				}

				bResult = WritePNG_File(rawInfo->data_refcounted, pHandle->pHeader->width, pHandle->pHeader->height, rawInfo->width, 4, pHandle->pFileName);
			}
			else if (rawInfo->tex_format == RTEX_BGR_U8)
			{
				//swap R and B for DirectX
				int pixelNum;

				for (pixelNum = 0; pixelNum < rawInfo->width * rawInfo->height; pixelNum++)
				{
					PngPixel3 *pPixel = (PngPixel3*) (rawInfo->data_refcounted + pixelNum * sizeof(PngPixel3));
					U8 temp = pPixel->r;
					pPixel->r = pPixel->b;
					pPixel->b = temp;		
				}

				bResult = WritePNG_File(rawInfo->data_refcounted, pHandle->pHeader->width, pHandle->pHeader->height, rawInfo->width, 3, pHandle->pFileName);
			}
			else
			{
				bResult = false;
				sprintf(errorString, "Unknown texture format");
			}

			texUnloadRawData(pHandle->pHeader);

			SendCommandStringToTestClientf("TextureComplete %d %d %s", pHandle->iRequestID, bResult, errorString);

			free(pHandle->pFileName);
			free(pHandle);
			eaRemove(&sppTextureRequestHandles, i);
		}
	}
	
	PERFINFO_AUTO_STOP();
}

//this is the headshot-getting command that is called via the testclient interface by the headshot server, which
//is a special instance of a ClientController, which communicates through TestClientLib
AUTO_COMMAND;
void WriteTextureForHeadshotServer(char *pTextureName, int iRequestID, char *pFileName)
{
	BasicTexture *pHeader;
	TextureForHeadshotServerHeaderRequestHandle *pHandle;
	static bool sbStartedCallbacks = false;

	if (!texFind(pTextureName, true))
	{
		SendCommandStringToTestClientf("TextureComplete %d 0 unknown texture %s", iRequestID, pTextureName);
		return;
	}

	pHeader = texLoadRawData(pTextureName, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);

	if (!pHeader)
	{
		SendCommandStringToTestClientf("TextureComplete %d 0 texLoadRawData failure", iRequestID);
		return;
	}

	pHandle = calloc(sizeof(TextureForHeadshotServerHeaderRequestHandle), 1);
	pHandle->pFileName = strdup(pFileName);
	pHandle->iRequestID = iRequestID;
	pHandle->pHeader = pHeader;

	eaPush(&sppTextureRequestHandles, pHandle);

	if (!sbStartedCallbacks)
	{
		sbStartedCallbacks = true;
		TimedCallback_Add(CheckForCompleteTextureRequests, NULL, 0.2f);
	}


}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void OpenUrlCmd(ACMD_SENTENCE pUrl)
{
	openURL(pUrl);
}



/// External map snap generation
void gclMapWriteMapSnapInfo(ZoneMap *zmap, const char *info_filename)
{
	int i;

	ZoneMapExternalMapSnap *pExternalMapSnap = StructCreate(parse_ZoneMapExternalMapSnap);

	pExternalMapSnap->version = ZENISNAP_CURRENT_VERSION;
	pExternalMapSnap->map_name = zmapGetName(zmap);
	pExternalMapSnap->filename = allocAddString(info_filename);
	{
		char buffer[ MAX_PATH ];
		worldGetTempBaseDir( zmapGetFilename( zmap ), SAFESTR( buffer ));
		strcat( buffer, "/client_world_cells_deps.bin" );
		FileListInsert( &pExternalMapSnap->deps, buffer, 0 );
	}

	{
		ZoneMapInfo* zmapInfo = zmapGetInfo( zmap );
		WorldRegion** regions = zmapInfo ? zmapInfoGetWorldRegions( zmapInfo ) : NULL;

		// save out mapsnap info
		for (i = 0; i < eaSize(&regions); i++) {
			WorldRegion* region = regions[i];
			const RoomConnGraph* regionGraph = worldRegionGetRoomConnGraph( region );
			Vec3 regionMin;
			Vec3 regionMax;
			if(!worldRegionGetBounds(region, regionMin, regionMax))
			{
				// implicit empty region -- ignore it;
				continue;
			}

			if( regionGraph ) {
				int roomIt;
				int partitionIt;
				for (roomIt = 0; roomIt != eaSize( &regionGraph->rooms ); ++roomIt ) {
					const Room* room = regionGraph->rooms[ roomIt ];
					for (partitionIt = 0; partitionIt != eaSize( &room->partitions ); ++partitionIt ) {
						const RoomPartition* partition = room->partitions[ partitionIt ];
						RoomPartitionParsed* partitionInfo;

						if( partition->partition_data && partition->partition_data->no_photo ) {
							continue;
						}

						// copy over the mapsnap data
						partitionInfo = StructCreate( parse_RoomPartitionParsed );
						copyVec3( partition->bounds_min, partitionInfo->bounds_min );
						copyVec3( partition->bounds_max, partitionInfo->bounds_max );
						copyVec3( partition->bounds_mid, partitionInfo->bounds_mid );
						eaCopy( &partitionInfo->mapSnapData.image_name_list, &partition->mapSnapData.image_name_list );
						partitionInfo->mapSnapData.overview_image_name = partition->mapSnapData.overview_image_name;
						partitionInfo->mapSnapData.image_width = partition->mapSnapData.image_width;
						partitionInfo->mapSnapData.image_height = partition->mapSnapData.image_height;
						copyVec2(partition->mapSnapData.vMin,partitionInfo->mapSnapData.vMin);
						copyVec2(partition->mapSnapData.vMax,partitionInfo->mapSnapData.vMax);

						eaPush( &pExternalMapSnap->mapRooms, partitionInfo );
					}
				}
			}
		}

		// save out golden path info
		FOR_EACH_IN_EARRAY_FORWARDS( regions, WorldRegion, region ) {
			WorldPathNode** eaPathNodes = worldRegionGetPathNodes( region );
			
			FOR_EACH_IN_EARRAY_FORWARDS( eaPathNodes, WorldPathNode, pathNode ) {
				ZoneMapMetadataPathNode* zeniPathNode = StructCreate( parse_ZoneMapMetadataPathNode );
				eaPush( &pExternalMapSnap->eaPathNodes, zeniPathNode );
			
				zeniPathNode->defUID = pathNode->uID;
				copyVec3( pathNode->position, zeniPathNode->pos );
				FOR_EACH_IN_EARRAY_FORWARDS( pathNode->properties.eaConnections, WorldPathEdge, pathEdge ) {
					ZoneMapMetadataPathEdge* zeniPathEdge = StructCreate( parse_ZoneMapMetadataPathEdge );
					eaPush( &zeniPathNode->eaConnections, zeniPathEdge );
					zeniPathEdge->uOther = pathEdge->uOther;
				} FOR_EACH_END;
			} FOR_EACH_END;
		} FOR_EACH_END;
	}

	resSetDictionaryEditMode( g_ZoneMapExternalMapSnapDictionary, true );

	ParserWriteTextFileFromSingleDictionaryStruct( info_filename, "ZoneMapExternalMapSnap", pExternalMapSnap, 0, 0 );

	StructDestroy(parse_ZoneMapExternalMapSnap, pExternalMapSnap);
}

AUTO_RUN;
void gclZeniSnapInit(void)
{
	worldlibSetCreateEncounterInfoCallback(gclMapWriteMapSnapInfo);
}

void OVERRIDE_LATELINK_UseRealAccessLevelInUGC_ExtraStuff(int iSet)
{

	cmdSendCmdClientToServer(STACK_SPRINTF("UseRealAccessLevelInUGC %d", iSet), false, 0, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
}

AUTO_RUN;
void gclAlertsSetup(void)
{
	if (isProductionMode())
	{
		SetAlwaysErrorfOnAlert(true);
	}
}
