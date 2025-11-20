/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "accountnet.h"
#include "AutoStartupSupport.h"
#include "BitStream.h"
#include "ControllerLink.h"
#include "file.h"
#include "FolderCache.h"
#include "HttpLib.h"
#include "GenericHttpServing.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "serverlib.h"
#include "StayUp.h"
#include "sysutil.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "winutil.h"
#include "utilitiesLib.h"
#include "ScratchStack.h"
#include "ResourceManager.h"

#include "XMPP_Connect.h"
#include "XMPP_Gateway.h"

bool gbConnectToController = false;
AUTO_CMD_INT(gbConnectToController, ConnectToController) ACMD_CMDLINE;

bool gbCreateSnapshotMode = false;
AUTO_CMD_INT(gbCreateSnapshotMode, CreateSnapshot) ACMD_CMDLINE;

bool gbXMPPVerbose = false;
AUTO_CMD_INT(gbXMPPVerbose, XMPPVerbose) ACMD_CMDLINE;

int giServerMonitorPort = DEFAULT_WEBMONITOR_XMPPSERVER;
AUTO_CMD_INT(giServerMonitorPort, ServerMonitorPort);

char gsLogServerAddress[128] = "NONE";

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_NAME(XMPPLogServer);
void setXMPPLogServer(const char *pLogServer)
{
	strcpy(gsLogServerAddress, pLogServer);
}


static bool sbExitCalled = false;
// Cleanup handling
static BOOL consoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType){ 
		case CTRL_CLOSE_EVENT: 
		case CTRL_LOGOFF_EVENT: 
		case CTRL_SHUTDOWN_EVENT: 
		case CTRL_BREAK_EVENT: 
		case CTRL_C_EVENT: 
			sbExitCalled = true;
			return TRUE; 

			// Pass other signals to the next handler.

		default: 
			return FALSE; 
	} 
}

AUTO_STARTUP(Xmpp, 1) ASTRT_DEPS(AS_TextFilter);
void xmppStartup(void)
{
}

int main(int argc, char** argv)
{
	U32 frame_timer;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER

	SetAppGlobalType(GLOBALTYPE_GLOBALCHATSERVER);
	DO_AUTO_RUNS;

	//if (StayUp(argc, argv, GlobalChat_StayUpStartupSafe, NULL, GlobalChat_StayUpTick, NULL)) return 0;

	loadstart_report_unaccounted(true);
	loadstart_printf("XMPP Server early initialization...\n");


	setConsoleTitle("XMPP Server");

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), (GlobalTypeToName(GetAppGlobalType()))[0], 0x8080ff);
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);

	loadend_printf(" done.");

	// First, call the universal setup stuff (copied from AppServerLib.c aslPreMain)
	memCheckInit();
	newConsoleWindow();
	showConsoleWindow();
	preloadDLLs(0);

	FolderCacheChooseModeNoPigsInDevelopment();
	FolderCacheSetManualCallbackMode(1);

	bsAssertOnErrors(true);
	setDefaultAssertMode();

	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	strcpy(gServerLibState.logServerHost, gsLogServerAddress);
	serverLibStartup(argc, argv);

	// Will send alerts instead of a stalling pop-up
	if (isProductionMode())
		ErrorfPushCallback(serverErrorAlertCallbackProgrammer, NULL);

	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	loadstart_printf("XMPP Server late initialization...\n");
	XMPP_Begin();

	frame_timer = timerAlloc();
	timerStart(frame_timer);
	FolderCacheEnableCallbacks(1);

	if (gbConnectToController)
		AttemptToConnectToController(false, NULL, false);
	else
		ServerLibSetControllerHost("NONE");

	GenericHttpServing_Begin(giServerMonitorPort, "XmppServer", DEFAULT_HTTP_CATEGORY_FILTER, 0);

	loadend_printf(" done.");

	//DirectlyInformControllerOfState("ready");
	//loadstart_report_unaccounted(false);

	while (!sbExitCalled)
	{
		static F32 fTotalElapsed = 0;
		F32 elapsed;

		autoTimerThreadFrameBegin("main");
		elapsed = timerElapsedAndStart(frame_timer);

		fTotalElapsed += elapsed;

		ScratchVerifyNoOutstanding();

		utilitiesLibOncePerFrame(elapsed, 1);

		FolderCacheDoCallbacks();

		commMonitor(commDefault());
		serverLibOncePerFrame();
		GenericHttpServing_Tick();
		XMPPServer_Tick(elapsed);

		autoTimerThreadFrameEnd();
	}

	svrExit(0);
	EXCEPTION_HANDLER_END
	return 0;
}
