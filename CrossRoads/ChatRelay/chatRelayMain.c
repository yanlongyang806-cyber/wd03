#include "AutoStartupSupport.h"
#include "AutoTransDefs.h"
#include "BitStream.h"
#include "chatdb.h"
#include "chatRelayComm.h"
#include "chatRelayCommandParse.h"
#include "cmdparse.h"
#include "ControllerLink.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GlobalTypeEnum.h"
#include "GlobalTypes.h"
#include "objTransactions.h"
#include "ServerLib.h"
#include "SuperAssert.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"
#include "ResourceManager.h"

#include "sysutil.h"
#include "winutil.h"

int giServerMonitorPort = 8085;
AUTO_CMD_INT(giServerMonitorPort, ServerMonitorPort);

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
			printf("Exiting Chat Relay...\n");
			return TRUE; 
			// Pass other signals to the next handler.
		default: 
			return FALSE; 
	} 
}

AUTO_STARTUP(ChatRelay, 1) ASTRT_DEPS(Chat, AS_Messages);
void chatRelayAutoStartup(void)
{
}

// Search for (FIND_DATA_DIRS_TO_CACHE) in to find a place 
// you can uncomment a line to verify if this is the correct list of folders to cache
const char* chatRelayPrecachePaths[] = {
	"ai",
	"defs",
	"genesis",
	"maps",
	"messages",
	"object_library",
	"powerart",
	"server/objectdb/schemas",
	"ui"
};

// Search for (FIND_HOGGS_TO_IGNORE) to find related code
// Only "basic.hogg", "data.hogg", "defs.hogg", "extended.hogg", "maps.hogg", "object_library.hogg"
const char* chatRelayHoggIgnores[] = {
	"bin.hogg",
	"character_library.hogg",
	"client.hogg",
	"ns.hogg",
	"server.hogg",
	"sound.hogg",
	"texts.hogg",
	"texture_library.hogg"
};

int main(int argc, char** argv)
{
	static char title[64] = "";
	U32 frame_timer;
	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER

	SetAppGlobalType(GLOBALTYPE_CHATRELAY);

	DO_AUTO_RUNS;
	
	sprintf(title, "%s [id %d] [pid %d]", 
		GlobalTypeToName(GetAppGlobalType()),
		GetAppGlobalID(), 
		GetCurrentProcessId());
	setConsoleTitle(title);
	printf("Running in Local mode.\n");

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), (GlobalTypeToName(GetAppGlobalType()))[0], 0x8080ff);
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);

	// First, call the universal setup stuff (copied from AppServerLib.c aslPreMain)
	memCheckInit();
	newConsoleWindow();
	showConsoleWindow();
	preloadDLLs(0);

	if (isDevelopmentMode()) {
		// Search for (FIND_HOGGS_TO_IGNORE) to find related code
		FolderCacheAddIgnores(chatRelayHoggIgnores, ARRAY_SIZE(chatRelayHoggIgnores));
	}

	FolderCacheChooseMode();
	FolderCacheSetManualCallbackMode(1);

	bsAssertOnErrors(true);
	setDefaultAssertMode();
	serverLibStartup(argc, argv);

	loadstart_printf("Caching folders...");
	fileCacheDirectories(chatRelayPrecachePaths, ARRAY_SIZE(chatRelayPrecachePaths));
	loadend_printf(" done.");

	if (!GetControllerLink())
		AttemptToConnectToController(true, NULL, true);

	frame_timer = timerAlloc();
	timerStart(frame_timer);
	FolderCacheEnableCallbacks(1);

	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	chatRelayStart();
	ATR_DoLateInitialization();
	DirectlyInformControllerOfState("ready");

	while (!sbExitCalled)
	{
		static F32 fTotalElapsed = 0;
		F32 elapsed;

		autoTimerThreadFrameBegin("main");
		elapsed = timerElapsedAndStart(frame_timer);

		utilitiesLibOncePerFrame(elapsed, 1);
		FolderCacheDoCallbacks();
		serverLibOncePerFrame();

		UpdateControllerConnection();
		UpdateObjectTransactionManager();

		chatRelayCommsMonitor();

		autoTimerThreadFrameEnd();
	}

	svrExit(0);
	EXCEPTION_HANDLER_END
	return 0;
}

AUTO_RUN;
void initChatRelay(void)
{
	cmdSetGenericServerToClientCB(chatRelaySendCommandToClient);
}