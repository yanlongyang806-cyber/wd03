#if _MSC_VER < 1600
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/x64/debug/AttachToDebuggerLibX64.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/debug/AttachToDebuggerLib.lib")
#endif
#else
#if _WIN64
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLibX64_vs10.lib")
#else
#pragma comment(lib,"../../libs/AttachToDebuggerLib/lib/AttachToDebuggerLib_vs10.lib")
#endif
#endif

#include "AutoStartupSupport.h"
#include "Category.h"
#include "file.h"
#include "FolderCache.h"
#include "GenericFileServing.h"
#include "GenericHttpServing.h"
#include "gimmeDLLWrapper.h"
#include "GlobalTypes.h"
#include "MemoryMonitor.h"
#include "ResourceManager.h"
#include "ServerLib.h"
#include "StayUp.h"
#include "sysutil.h"
#include "textparser.h"
#include "timing.h"
#include "UtilitiesLib.h"
#include "utils.h"
#include "winutil.h"

#include "TicketTracker.h"
#include "TicketTrackerDB.h"

TimingHistory *gSearchHistory;
TimingHistory *gSendHistory;

extern char gTicketTrackerAltDataDir[MAX_PATH];

extern bool gbCreateSnapshotMode;
extern bool gbMakeBinsAndExit;
AUTO_COMMAND ACMD_CMDLINE;
void makeBinsAndExit(bool bSet)
{
	ParserForceBinCreation(bSet);
	gbMakeBinsAndExit = bSet;
}

int giServerMonitorPort = 8082;
AUTO_CMD_INT(giServerMonitorPort, ServerMonitorPort);

int giHttpPort = 81;
AUTO_CMD_INT(giHttpPort, httpPort);

AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_TICKETTRACKER);
}

void updateConsoleTitle(const char *pCurrentState)
{
	char buf[256];
		int pid = GetCurrentProcessId ();

	if(pCurrentState == NULL)
	{
		pCurrentState = "Idle";
	}
	if (gbCreateSnapshotMode)
		sprintf_s(SAFESTR(buf), "[%d]TicketMerger - %s", pid, pCurrentState);
	else
		sprintf_s(SAFESTR(buf), "[%d]TicketTracker - %s", pid, pCurrentState);
	setConsoleTitle(buf);
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
			printf("Ticket Tracker gracefully shutting down ...");
			sbExitCalled = true;
			// why is vista so f-ing stupid
			while(1)
			{
				Sleep(DEFAULT_SERVER_SLEEP_TIME);
			}
			return FALSE; 

		// Pass other signals to the next handler.

		default: 
			return FALSE; 
	} 
}

static void OncePerFrame(F32 fElapsed)
{
	static bool bOnce = false;
	U32 currentTime = timeSecondsSince2000();
		
	utilitiesLibOncePerFrame(fElapsed, 1);
	ticketTrackerOncePerFrame();
	GenericHttpServing_Tick();
	GenericFileServing_Tick();
	FolderCacheDoCallbacks();
	commMonitor(commDefault());
	
	TicketTrackerCreateSnapshot();
}

static bool TicketTracker_StayUpTick(void *pUserData)
{
	// Does nothing
	return true;
}
static bool TicketTracker_StayUpStartupSafe(void *pUserData)
{	
	int iLastMergerPID = TicketTrackerGetMergerPID();
	if (!iLastMergerPID || !system_poke(iLastMergerPID))
		return true;
	return false;
}

int main(int argc,char **argv)
{
	int i;

	int frameTimer;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS

	if (StayUp(argc, argv, TicketTracker_StayUpStartupSafe, NULL, TicketTracker_StayUpTick, NULL)) return 0;

	categorySetFile("internal.category");
	categorySetPublicFile("public.category");
	setDefaultAssertMode();
	memMonitorInit();

	for(i = 0; i < argc; i++)
	{
		printf("%s ", argv[i]);
	}
	printf("\n");

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'E', 0x8080ff);
	updateConsoleTitle("Initializing");
	
	FolderCacheChooseModeNoPigsInDevelopment();
	FolderCacheEnableCallbacks(1);
	FolderCacheSetManualCallbackMode(1);
	FolderCacheStartMonitorThread();

	utilitiesLibStartup();
	preloadDLLs(0);
	srand((unsigned int)time(NULL));
	consoleUpSize(110,128);

	DoAutoStartup();
	resFinishLoading();

	if (!fileIsUsingDevData())
	{
		gimmeDLLDisable(1);
	}

	serverLibStartup(argc, argv);
	ServerLibSetControllerHost("NONE");

	if (gbMakeBinsAndExit)
		return;
	updateConsoleTitle("Loading DB");
	ticketTrackerInit();
	if (gbCreateSnapshotMode)
		return;

	// ---------------------------------------------------------------------
	// Fire these up as quickly as possible, to be capable of receiving 
	// errors again, and restore the web interface (after a restart/crash).

	// options loading goes here
	//ParserReadTextFile(STACK_SPRINTF("%s%s", errorTrackerLibGetDataDir(), "settings.txt"), parse_ErrorTrackerSettings, &tempErrorTrackerSettings);

	// This ensures that we don't lose data when we close the app.
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);

	printf("Load complete.\n\n");

	updateConsoleTitle("Running");

	GenericHttpServing_Begin(giServerMonitorPort, "TicketTracker", DEFAULT_HTTP_CATEGORY_FILTER, 0);

	GenericFileServing_Begin(0, 0);
	{
		char screenshotsFullPath[MAX_PATH];
		sprintf(screenshotsFullPath, "%s\\%s", fileLocalDataDir(), gTicketTrackerAltDataDir);
		GenericFileServing_ExposeDirectory(screenshotsFullPath);
	}

	frameTimer = timerAlloc();
	gSearchHistory = timingHistoryCreate(10000);
	gSendHistory = timingHistoryCreate(10000);

	// This thread currently handles the Ticket Tracker Database work
	ErrorfPushCallback(NULL, NULL);
	while (!sbExitCalled)
	{
		F32 frametime = timerElapsedAndStart(frameTimer);
		OncePerFrame(frametime);
	}
	ErrorfPopCallback();

	ticketTrackerShutdown();

	EXCEPTION_HANDLER_END
}
