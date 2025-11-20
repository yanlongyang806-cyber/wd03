#include <string.h>

#include "sysutil.h"
#include "UtilitiesLib.h"
#include "FolderCache.h"
#include "ServerLib.h"
#include "BitStream.h"
#include "crypt.h"
#include "HttpLib.h"
#include "GenericHttpServing.h"
#include "AccountServer.h"
#include "ProductKey.h"
#include "objContainerIO.h"
#include "objTransactions.h"
#include "ControllerLink.h"
#include "File.h"
#include "StringCache.h"
#include "Alerts.h"
#include "StayUp.h"
#include "timing_profiler_interface.h"
#include "logging.h"
#include "ScratchStack.h"
#include "wininclude.h"
#include "winutil.h"
#include "objMerger.h"

char gUpdateSchemas[MAX_PATH] = "";
AUTO_CMD_STRING(gUpdateSchemas, UpdateSchemas) ACMD_CMDLINE;

int giServerMonitorPort = DEFAULT_WEBMONITOR_ACCOUNT_SERVER;
AUTO_CMD_INT(giServerMonitorPort, ServerMonitorPort);

// Number of ms a stall must last before issuing an ACCOUNTSERVER_SLOW_FRAME alert
static int giSlowFrameThreshold = 30000; // in ms
AUTO_CMD_INT(giSlowFrameThreshold, SlowFrameThreshold) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

extern bool gbConnectToController;

static bool gbFullyLoaded = false;

static bool sbExitCalled = false;

static int siHeartbeatInterval = 0;
AUTO_CMD_INT(siHeartbeatInterval, HeartbeatInterval) ACMD_COMMANDLINE;

// Cleanup handling
static BOOL consoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType){ 
		case CTRL_CLOSE_EVENT: 
		case CTRL_LOGOFF_EVENT: 
		case CTRL_SHUTDOWN_EVENT: 
		case CTRL_BREAK_EVENT: 
		case CTRL_C_EVENT: 
			if (!gbFullyLoaded)
			{
				printf("Please wait for the Account Server to finish loading before killing it.\n");
				return TRUE;
			}
			printf("Account Server is shutting down...\n");
			log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Account Server is shutting down...");
			sbExitCalled = true;
			while (1)
			{
				Sleep(DEFAULT_SERVER_SLEEP_TIME);
			}
			return FALSE; 

		// Pass other signals to the next handler.

		default: 
			return FALSE; 
	} 
}

static bool StayUpTick(void *pUserData)
{
	return true;
}

// Return true if it is safe to start up.
static bool StayUpStartupSafe(void *pUserData)
{
	return !IsMergerRunning(ACCOUNT_SERVER_MERGER_NAME);
}

// Return static function string to be used as a console title
SA_RET_NN_STR static const char * GetAccountServerConsoleTitle(void)
{
	static char *pTitle = NULL;

	if (!pTitle)
	{
		if (!isAccountServerMode(ASM_Normal))
			estrPrintf(&pTitle, "%s AS %s [pid:%d]", StaticDefineIntRevLookupNonNull(AccountServerModeEnum, getAccountServerMode()), ACCOUNT_SERVER_VERSION, GetCurrentProcessId());
		else
			estrPrintf(&pTitle, "Account Server %s [pid:%d]", ACCOUNT_SERVER_VERSION, GetCurrentProcessId());

		if (isProductionMode())
			estrConcatf(&pTitle, " (production mode)");
		else
			estrConcatf(&pTitle, " (non-production mode)");
	}

	return pTitle;
}

// Get elapsed time since the program has been running or 
// since the last time this function was called
static F32 GetElapsedTime(void)
{
	static U32 elapsedTimerID = 0;
	static bool ranOnce = false;

	if (!ranOnce)
	{
		elapsedTimerID = timerAlloc();
		timerStart(elapsedTimerID);
		ranOnce = true;
	}

	return timerElapsedAndStart(elapsedTimerID);
}

// Get elapsed time since the program has been running
static F32 GetTotalElapsedTime(void)
{
	static U32 elapsedTimerID = 0;
	static bool ranOnce = false;

	if (!ranOnce)
	{
		elapsedTimerID = timerAlloc();
		timerStart(elapsedTimerID);
		ranOnce = true;
	}

	return timerElapsed(elapsedTimerID);
}

static void updateCoarseTimer(CoarseTimerManager * pFrameTimerManager, F32 fElapsed)
{
	PERFINFO_AUTO_START_FUNC();
	if (fElapsed > (giSlowFrameThreshold / 1000.0f))
	{
		char * pCounterData = NULL;
		coarseTimerPrint(pFrameTimerManager, &pCounterData, NULL);

		TriggerAlertf("ACCOUNTSERVER_SLOW_FRAME", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0,
			"Account server frame was %f.\nProfile data:\n%s", fElapsed, pCounterData);

		estrDestroy(&pCounterData);
	}
	coarseTimerClear(pFrameTimerManager);
	PERFINFO_AUTO_STOP();
}

static int giLogMsgQueueSize = 10 * 1024 * 1024;
AUTO_CMD_INT(giLogMsgQueueSize, LogMsgQueueSize) ACMD_COMMANDLINE;

AUTO_RUN_FIRST;
void setLogQueueSize(void)
{
	char	buf[100] = {0};

	if (ParseCommandOutOfCommandLine("LogMsgQueueSize", buf))
		giLogMsgQueueSize = atoi(buf);

	// Increase the log queue size
	if (isProductionMode())
		logSetMsgQueueSize(giLogMsgQueueSize);
}

int main(int argc, char** argv)
{
	CoarseTimerManager * pFrameTimerManager = NULL;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER

	// Make sure our random numbers are random
	srand(time(NULL));

	SetAppGlobalType(GLOBALTYPE_ACCOUNTSERVER);
	DO_AUTO_RUNS;

	// Run stay up code instead of normal account server code if -StayUp was given
	if (StayUp(argc, argv, StayUpStartupSafe, NULL, StayUpTick, NULL)) return 0;

	// Print loading information.
	loadstart_printf("Starting Account Server %s...\n\n", ACCOUNT_SERVER_VERSION);
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'A', 0xff0000);
	SetConsoleTitle("Account Server - Starting...");
	loadstart_report_unaccounted(true);

	// First, call the universal setup stuff
	memCheckInit();
	newConsoleWindow();
	showConsoleWindow();
	preloadDLLs(0);

	// This ensures that we don't lose data when we close the app.
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);
	if (isProductionMode()) disableConsoleCloseButton();

	FolderCacheChooseModeNoPigsInDevelopment();
	FolderCacheEnableCallbacks(1);
	FolderCacheSetManualCallbackMode(1);
	FolderCacheStartMonitorThread();

	bsAssertOnErrors(true);
	disableRtlHeapChecking(NULL);
	setDefaultAssertMode();
	cryptAdler32Init();
	
	serverLibStartup(argc, argv);

	// Set the console title
	SetConsoleTitle(GetAccountServerConsoleTitle());

	//want to do this before AccountServerInit() so we don't incorrectly attempt to send errors
	if (!gbConnectToController)
	{
		ServerLibSetControllerHost("NONE");
	}

	//calls app-specific app-init
	if(!AccountServerInit())
		svrExit(-1);

	// Connect to a controller if appropriate
	if (gbConnectToController)
		AttemptToConnectToController(false, NULL, false);


	if (isAccountServerMode(ASM_Normal))
	{
		// Start the server monitor
		bool bStarted = GenericHttpServing_Begin(giServerMonitorPort, ACCOUNT_SERVER_INTERNAL_NAME, DEFAULT_HTTP_CATEGORY_FILTER, 0);
		if (!devassertmsgf(bStarted,
			"Could not start server monitor HTTP page! This probably means you need to provide -serverMonitorPort x to the command line, "
			"where x is an unused port.  It is currently set to %d.", giServerMonitorPort))
		{
			svrExit(-1);
		}
	}

	if (isProductionMode())
	{
		// Will send alerts instead of a stalling pop-up
		ErrorfPushCallback(serverErrorAlertCallbackProgrammer, NULL);
	}

	pFrameTimerManager = coarseTimerCreateManager(true);

	gbFullyLoaded = true;
	printf("\nEntering ready state...\n");

	if (!isAccountServerMode(ASM_Merger) && !isAccountServerMode(ASM_ExportUserList) && !isAccountServerMode(ASM_UpdateSchemas))
	{
		bool finishedInitialFrames = false;
		U32 loopCount = 0;
		while (!sbExitCalled)
		{
			F32 elapsed = GetElapsedTime();

			if (siHeartbeatInterval)
			{
				static U32 siLastTime = 0;
				static U32 iFrameCount = 0;
				static U32 iLastFrameCount = 0;
				U32 iCurTime = timeSecondsSince2000();

				iFrameCount++;
				if (!siLastTime)
				{
					siLastTime = iCurTime;
				}
				else
				{
					if (siLastTime < iCurTime - siHeartbeatInterval)
					{
						siLastTime = iCurTime;
						printf("%d frames in last %d seconds... %f fps\n", iFrameCount - iLastFrameCount, siHeartbeatInterval, (float)(iFrameCount - iLastFrameCount) / (float)siHeartbeatInterval);
						iLastFrameCount = iFrameCount;
					}
				}
			}



			// Report successful startup after a few frames; outside of the frame timers because of the loadend_printf().
			if (!finishedInitialFrames && loopCount++ > 10)
			{
				finishedInitialFrames = true;
				DirectlyInformControllerOfState("ready");
				log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Server ready. (%s, %s)", ACCOUNT_SERVER_VERSION, GetUsefulVersionString());
				loadend_printf("startup complete.");
			}

			autoTimerThreadFrameBegin(__FUNCTION__);
			updateCoarseTimer(pFrameTimerManager, elapsed);
			coarseTimerAddInstance(NULL, "frame");

			ScratchVerifyNoOutstanding();

			utilitiesLibOncePerFrame(elapsed, 1);

			FolderCacheDoCallbacks();

			commMonitor(commDefault());

			if (isAccountServerMode(ASM_Normal))
			{
				F32 totalElapsed = GetTotalElapsedTime();

				PERFINFO_AUTO_START("ASM_Normal Activities", 1);
				UpdateObjectTransactionManager();
				AccountServerOncePerFrame(totalElapsed, elapsed);
				GenericHttpServing_Tick();

				if (!StartedByStayUp())
					StayUpTick(NULL); // Go ahead and take over StayUp's tick if we got here and weren't started by StayUp.
				PERFINFO_AUTO_STOP();
			}
			else if (isAccountServerMode(ASM_KeyGenerating))
			{
				PERFINFO_AUTO_START("ASM_KeyGenerating Activities", 1);
				httpProcessAuthentications();
				PERFINFO_AUTO_STOP();
			}
			
			UpdateControllerConnection();

			coarseTimerStopInstance(NULL, "frame");

			autoTimerThreadFrameEnd();
		}

		coarseTimerEnable(false);
	}
	ErrorfPopCallback();

	AccountServerShutdown();

	if (!isAccountServerMode(ASM_KeyGenerating))
	{
		if (!isAccountServerMode(ASM_Merger) && !isAccountServerMode(ASM_UpdateSchemas))
		{
			objContainerSaveTick();
			objForceRotateIncrementalHog();
			objFlushContainers();
		}
		objCloseAllWorkingFiles();
		objCloseContainerSource();
	}

	svrExit(0);
	EXCEPTION_HANDLER_END
	return 0;
}
