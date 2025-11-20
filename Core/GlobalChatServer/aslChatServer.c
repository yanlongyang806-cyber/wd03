/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "accountnet.h"
#include "aslChatServer.h"
#include "aslChatServerInit.h"
#include "BitStream.h"
#include "chatGlobal.h"
#include "chatLocal.h"
#include "ControllerLink.h"
#include "file.h"
#include "FolderCache.h"
#include "HttpLib.h"
#include "GenericHttpServing.h"
#include "logging.h"
#include "objContainerIO.h"
#include "objMerger.h"
#include "objTransactions.h"
#include "serverlib.h"
#include "StayUp.h"
#include "sysutil.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "winutil.h"
#include "utilitiesLib.h"
#include "xmpp/XMPP_Chat.h"
#include "ScratchStack.h"

TimingHistory *gChatHistory;

bool gbConnectToController = false;
AUTO_CMD_INT(gbConnectToController, ConnectToController) ACMD_CMDLINE;

bool gbCreateSnapshotMode = false;
AUTO_CMD_INT(gbCreateSnapshotMode, CreateSnapshot) ACMD_CMDLINE;

bool gbChatVerbose = false;
AUTO_CMD_INT(gbChatVerbose, ChatVerbose) ACMD_CMDLINE;

bool gbConnectGlobalChatToAccount = false;
AUTO_CMD_INT(gbConnectGlobalChatToAccount, ConnectToAccount) ACMD_CMDLINE;

bool gbImportUserAccounts = false;
AUTO_CMD_INT(gbImportUserAccounts, ImportUsers) ACMD_CMDLINE;

int giServerMonitorPort = 8083;
AUTO_CMD_INT(giServerMonitorPort, ServerMonitorPort);

bool gbDisableMergers = false;
AUTO_CMD_INT(gbDisableMergers, DisableMerger);

extern bool gbMakeBinsAndExit;
AUTO_COMMAND ACMD_CMDLINE;
void makeBinsAndExit(bool bSet)
{
	ParserForceBinCreation(bSet);
	gbMakeBinsAndExit = bSet;
}

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

static U32 suLastSnapshotTime = 0;

// If set, don't run the merger until after the end of the first period.
bool gbDelayMerger = false;
AUTO_CMD_INT(gbDelayMerger, DelayMerger) ACMD_CMDLINE;

static int siLastMergerPID = 0;
extern int getSnapshotInterval(void);

void ChatServerCreateSnapshotInternal(bool bDelayOnStartup)
{
	static int poke_count = 0;
	char *estr = NULL;
	U32 currentTime = timeSecondsSince2000();

	// If delayed merging has been requested, don't run the merger on startup.
	if (!suLastSnapshotTime && bDelayOnStartup)
	{
		suLastSnapshotTime = currentTime;
		return;
	}

	suLastSnapshotTime = timeSecondsSince2000();
	if (siLastMergerPID)
	{
		if (IsMergerRunning(GLOBALCHATSERVER_MERGER_NAME))
		{
			poke_count++;
			if (poke_count > 1)
				ErrorOrAlert("CHATSERVER.MERGER_STILL_RUNNING",
					"ChatServer merger [pid:%d] is still running! Please wait a few minutes before snapshotting again. If this message repeats, find Theo.\n", siLastMergerPID);
			return;
		}
	}
	poke_count = 0;

	estrStackCreate(&estr);
	estrPrintf(&estr, "%s -CreateSnapshot -NoShardMode -quickLoadMessages -NeverConnectToController", getExecutableName());
	estrConcatf(&estr, " -SetProductName %s %s", GetProductName(), GetShortProductName());
	estrConcatf(&estr, " -SetErrorTracker %s", getErrorTracker());
	estrConcatf(&estr, " -ChatLogServer %s", gsLogServerAddress);

	siLastMergerPID = system_detach(estr,1,false);
	estrDestroy(&estr);
}
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ChatServer);
void ChatServerCreateSnapshot(void)
{
	ChatServerCreateSnapshotInternal(false);
}

int ChatServerLibOncePerFrame(F32 fTotalElapsed, F32 fElapsed)
{
	static bool bOnce = false;
	U32 currentTime = timeSecondsSince2000();

	UpdateObjectTransactionManager();
	objContainerSaveTick();

	if (!gbDisableMergers && currentTime >= suLastSnapshotTime + getSnapshotInterval()*60)
	{
		// Create the Chat Server snapshot.
		ChatServerCreateSnapshotInternal(gbDelayMerger);
	}
	return 1;
}

static bool GlobalChat_StayUpTick(void *pUserData)
{
	// Does nothing
	return true;
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
			log_printf(LOG_CHATSERVER, "Exiting Chat Server...");
			printf("Exiting Chat Server...\n");
			return TRUE; 

			// Pass other signals to the next handler.

		default: 
			return FALSE; 
	} 
}

static bool GlobalChat_StayUpStartupSafe(void *pUserData)
{
	if (!siLastMergerPID || !system_poke(siLastMergerPID))
		return true;
	return false;
}

int main(int argc, char** argv)
{
	U32 frame_timer;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS;

	if (StayUp(argc, argv, GlobalChat_StayUpStartupSafe, NULL, GlobalChat_StayUpTick, NULL)) return 0;

	loadstart_report_unaccounted(true);
	loadstart_printf("Global ChatServer early initialization...\n");

	if(gbTestingMetrics)
	{
		gChatHistory = timingHistoryCreate(200000);
	}

	SetAppGlobalType(GLOBALTYPE_GLOBALCHATSERVER);

	{
		int pid = GetCurrentProcessId ();
		if (gbCreateSnapshotMode)
			setConsoleTitle(STACK_SPRINTF("Global Chat Merger [PID %d]", pid));
		else
			setConsoleTitle(STACK_SPRINTF("Global Chat Server [PID %d]", pid));
	}

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), (GlobalTypeToName(GetAppGlobalType()))[0], 0x8080ff);
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);

	disableConsoleCloseButton(); // So you can't inadvertently close ChatServer with 'X' and lose data
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
	serverLibEnableErrorDialog(false);

	// Will send alerts instead of a stalling pop-up
	if (isProductionMode())
		ErrorfPushCallback(serverErrorAlertCallbackProgrammer, NULL);

	//calls app-specific app-init
	loadstart_printf("Global ChatServer initialization...\n");
	ChatServerLibInit();
	loadend_printf("GCS initialization done.");

	loadstart_printf("Global ChatServer late initialization...\n");

	if (gbCreateSnapshotMode || gbMakeBinsAndExit)
		return;
	if (gbImportUserAccounts)
	{
		objContainerSaveTick();
		objForceRotateIncrementalHog();
		objFlushContainers();
		objCloseAllWorkingFiles();
		objCloseContainerSource();
		return;
	}

	frame_timer = timerAlloc();
	timerStart(frame_timer);
	FolderCacheEnableCallbacks(1);
	
	if (gbConnectToController)
		AttemptToConnectToController(false, NULL, false);
	else
		ServerLibSetControllerHost("NONE");

	GenericHttpServing_Begin(giServerMonitorPort, "ChatServer", DEFAULT_HTTP_CATEGORY_FILTER, 0);

	loadend_printf("GCS late initialization done.");

	printf("Running in NoShardMode\n");
	DirectlyInformControllerOfState("ready");
	loadstart_report_unaccounted(false);

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
		commMonitor(getChatServerComm());
		//Sleep(DEFAULT_SERVER_SLEEP_TIME);

		serverLibOncePerFrame();
		ChatServerLibOncePerFrame(fTotalElapsed, elapsed);

		GenericHttpServing_Tick();
		ChatServerGlobalTick(elapsed);

		autoTimerThreadFrameEnd();
	}

	objContainerSaveTick();
	objForceRotateIncrementalHog();
	objCloseContainerSource();
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