/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslChatServer.h"
#include "aslChatServerInit.h"
#include "chatGlobal.h"
#include "chatLocal.h"
#include "chatVoice.h"
#include "ChatServer/chatShard_Shared.h"

#include "objContainerIO.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "AutoTransDefs.h"
#include "serverlib.h"
#include "ControllerLink.h"
#include "sysutil.h"
#include "winutil.h"
#include "FolderCache.h"
#include "fileutil.h"
#include "utilitiesLib.h"
#include "logging.h"
#include "BitStream.h"
#include "GenericHttpServing.h"
#include "HttpLib.h"
#include "accountnet.h"

//#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "timing.h"
#include "timing_profiler_interface.h"

TimingHistory *gChatHistory;

//if this is true, then the ChatServer is running outside a shard (the Global Chat Server).
bool gbNoShardMode = false; 
bool gbLocalShardOnlyMode = false;

//bool gbCreateSnapshotMode = false;
//AUTO_CMD_INT(gbCreateSnapshotMode, CreateSnapshot) ACMD_CMDLINE;

bool gbChatVerbose = false;
AUTO_CMD_INT(gbChatVerbose, ChatVerbose) ACMD_CMDLINE;

bool gbConnectGlobalChatToAccount = false;
AUTO_CMD_INT(gbConnectGlobalChatToAccount, ConnectToAccount) ACMD_CMDLINE;

//bool gbImportUserAccounts = false;
//AUTO_CMD_INT(gbImportUserAccounts, ImportUsers) ACMD_CMDLINE;

int giServerMonitorPort = 8083;
AUTO_CMD_INT(giServerMonitorPort, ServerMonitorPort);

char gsLogServerAddress[128] = "NONE";

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_NAME(ChatLogServer);
void setChatLogServer(const char *pChatLogServer)
{
	strcpy(gsLogServerAddress, pChatLogServer);
}

AUTO_COMMAND_REMOTE;
void ChatServerReceiveChatMsg(ContainerID playerID, const char *msg)
{
	if (gbChatVerbose)
		printf("got chat: (%d) \"%s\"\n", playerID, msg);
}

bool gbTestingMetrics = false;
AUTO_CMD_INT(gbTestingMetrics, TestingMetrics) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

int ChatServerLibOncePerFrame(F32 fTotalElapsed, F32 fElapsed)
{
	static bool bOnce = false;
	if(!bOnce)
	{
		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
		bOnce = true;
	}
	UpdateControllerConnection();
	UpdateObjectTransactionManager();

	cvOncePerFrame();
	return 1;
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
			printf("Exiting Chat Server...\n");
			return TRUE; 

		// Pass other signals to the next handler.

		default: 
			return FALSE; 
	} 
}

// Search for (FIND_DATA_DIRS_TO_CACHE) in to find a place 
// you can uncomment a line to verify if this is the correct list of folders to cache
const char* chatServerPrecachePaths[] = {
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
const char* chatServerHoggIgnores[] = {
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
	U32 frame_timer;
	
	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	
	SetAppGlobalType(GLOBALTYPE_CHATSERVER);

	DO_AUTO_RUNS;

	if(gbTestingMetrics)
	{
		gChatHistory = timingHistoryCreate(200000);
	}


	setConsoleTitle(GlobalTypeToName(GetAppGlobalType()));
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
		FolderCacheAddIgnores(chatServerHoggIgnores, ARRAY_SIZE(chatServerHoggIgnores));
	}

	FolderCacheChooseMode();
	FolderCacheSetManualCallbackMode(1);

	bsAssertOnErrors(true);
	setDefaultAssertMode();
	serverLibStartup(argc, argv);

	loadstart_printf("Caching folders...");
	fileCacheDirectories(chatServerPrecachePaths, ARRAY_SIZE(chatServerPrecachePaths));
	loadend_printf(" done.");

	//calls app-specific app-init
	ChatServerLibInit();

	ATR_DoLateInitialization();
	if (!GetControllerLink())
		AttemptToConnectToController(true, NULL, true);
	DirectlyInformControllerOfState("ready");

	frame_timer = timerAlloc();
	timerStart(frame_timer);
	FolderCacheEnableCallbacks(1);

	while (!sbExitCalled)
	{
		static F32 fTotalElapsed = 0;
		F32 elapsed;

		autoTimerThreadFrameBegin("main");
		elapsed = timerElapsedAndStart(frame_timer);

		fTotalElapsed += elapsed;
		utilitiesLibOncePerFrame(elapsed, 1);

		FolderCacheDoCallbacks();

		commMonitor(commDefault());
		commMonitor(getCommToGlobalChat());
		//Sleep(DEFAULT_SERVER_SLEEP_TIME);

		serverLibOncePerFrame();
		ChatServerLibOncePerFrame(fTotalElapsed, elapsed);

		ChatServerLocalTick();
		ChatLocal_QueuedMessageTick();
		ChatLocal_LoginQueueTicket();
		ChatLocal_AdSpamTick();

		autoTimerThreadFrameEnd();
	}
	logWaitForQueueToEmpty();

	svrExit(0);
	EXCEPTION_HANDLER_END
	return 0;
}

Entity *entExternGetCommandEntity(CmdContext *context)
{
	assertmsg(0,"You can only call this from game or server!");
	return NULL;
}

int OVERRIDE_LATELINK_comm_commandqueue_size(void)
{
	return (1<<15);
}