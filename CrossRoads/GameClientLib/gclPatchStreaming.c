#include "ContinuousBuilderSupport.h"
#include "earray.h"
#include "EditorManager.h"
#include "error.h"
#include "file.h"
#include "fileLoader.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameClientLib.h"
#include "gclBaseStates.h"
#include "gclPatchStreaming.h"
#include "GfxDebug.h"
#include "GfxSprite.h"
#include "GlobalStateMachine.h"
#include "GlobalTypes.h"
#include "GraphicsLib.h"
#include "hoglib.h"
#include "inputMouse.h"
#include "inputLib.h"
#include "memlog.h"
#include "net/net.h"
#include "patchcommonutils.h"
#include "patchfilespec.h"
#include "patchtrivia.h"
#include "pcl_client.h"
#include "pcl_client_struct.h"
#include "ScratchStack.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "ThreadManager.h"
#include "timing_profiler_interface.h"
#include "UnitSpec.h"
#include "utilitiesLib.h"
#include "utils.h"
#include "sysutil.h"
#include "logging.h"
#include "WorldGrid.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:Patchstreamingprocessthread", BUDGET_FileSystem););

extern bool g_PatchingStreamingOn;

static MemLog patch_streaming_memlog;
static char *patch_streaming_xfers = NULL;

static NetComm *g_patch_comm = NULL;
static PCL_Client *g_patchstreaming_client = NULL;
static char g_PatchName[128];
static char g_PatchSandbox[128];
static int g_PatchBranch = INT_MAX;
static int g_PatchTime = INT_MAX;
static char g_PatchServer[128];
static char g_PatchProject[128];
static int g_PatchPort = DEFAULT_PATCHSERVER_PORT;
static char g_ExtraFilespec[CRYPTIC_MAX_PATH];
static int g_last_display = 0;
static int g_last_files = 0;
static bool patch_streaming_verbose;
static bool patch_streaming_log;
extern bool g_PatchStreamingSimulate;
static int g_PatchStreamingSimulateForce = -1;
static U64 patch_streaming_simulate_latency_ms=1000; // 1 second minimum patch time
static U64 patch_streaming_simulate_kbps=96; // bytes per ms or ~kb per second, default 768kbps broadband connection

static int patch_streaming_connect_timer;
static F32 patch_streaming_connect_timeout=120; // timeout when connecting initially (no user-facing information displayed, since it's before loading fonts/textures/etc)
static bool patch_streaming_inactive = false;	// true if we're inactive 
static bool patch_streaming_inactivity_check = false; // true if inactivity check is enabled
static F32 patch_streaming_inactivity_timeout=300; // if we ever go longer than this without patching activity, shut down the PCL link

static bool patch_streaming_initial_get = false; // true if we're in the initial get

// True if we're waiting for some background operation to complete, on startup.
static bool patch_streaming_operation_running = false;

// This is set to fail PCL in dev mode, as a special case.
static bool patch_streaming_dev_mode_pcl_fail = false;

static CRITICAL_SECTION csPatchStreaming;
static CRITICAL_SECTION csPatchStreamingStats;

// Needs to be small enough that there's always room for a high-priority transfer
static int patchstreaming_max_xfers = 20;
// Sets the maximum number of files being patched simultaneously.
AUTO_CMD_INT(patchstreaming_max_xfers, patchstreaming_max_xfers) ACMD_COMMANDLINE;

typedef struct PatchStreamingSimulatedPatch
{
	const char *relpath;
	U64 totalBytes;
	U64 bytesRemaining;
	U64 msRemaining;
} PatchStreamingSimulatedPatch;

static PatchStreamingSimulatedPatch **streaming_simulated_patches;

static void gclPatchStreamingHandleDisconnect(void);

#define GCL_PATCHSTREAMING_CONNECT "gclPatchStreamingConnect"
#define GCL_PATCHSTREAMING_SET_VIEW "gclPatchStreamingSetView"
#define GCL_PATCHSTREAMING_GET_REQUIRED "gclPatchStreamingGetRequired"

#define STREAMING_TIMEOUT_SECONDS 65

#define PATCHSTREAMING_LOG_NAME "PatchStreaming.log"

// Sets the timeout time for connecting to the PatchServer for PatchStreaming
AUTO_CMD_FLOAT(patch_streaming_connect_timeout, PatchStreamingConnectTimeout) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CommandLine) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_HIDE;

// Sets the inactivity timeout after which the Patch Server link is disconnected
AUTO_CMD_FLOAT(patch_streaming_inactivity_timeout, PatchStreamingInactivityTimeout) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CommandLine) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_HIDE;

// Displays verbose messages about what files are patch streamed.  Requires -verbose as well.
AUTO_CMD_INT(patch_streaming_verbose, PatchStreamingVerbose) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CommandLine) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_HIDE;

// Create patch streaming log.
AUTO_CMD_INT(patch_streaming_log, PatchStreamingLog) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CommandLine) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_HIDE;

// Assumes all files are up to date on disk, and simulates a delay in loading/patching all files
AUTO_CMD_INT(g_PatchStreamingSimulateForce, PatchStreamingSimulate) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CommandLine) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_HIDE;

// Sets the per-file latency for PatchStreamingSimulate
AUTO_CMD_INT(patch_streaming_simulate_latency_ms, PatchStreamingSimulateMS) ACMD_CMDLINE;

// Sets the total bandwidth for PatchStreamingSimulate
AUTO_CMD_INT(patch_streaming_simulate_kbps, PatchStreamingSimulateKBPS) ACMD_CMDLINE;

// Turn off most of the simulation delay.
AUTO_COMMAND;
void gclPatchStreamingFastMode()
{
	patch_streaming_simulate_latency_ms = 100;
	patch_streaming_simulate_kbps = 1250000;
	patch_streaming_connect_timeout = 10;
}

// Enables patch streaming from the specified patch server
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CommandLine) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE;
void PatchServerAndProjectForStreaming(char * server, char * project)
{
	strcpy(g_PatchServer, server);
	strcpy(g_PatchProject, project);
	g_PatchingStreamingOn = true;
}

AUTO_RUN_EARLY;
void PatchStreamingEnable(void)
{
	if (isDevelopmentMode()) {
		g_PatchingStreamingOn = false;
	} else {
		g_PatchingStreamingOn = true;
	}
}

// Log something to the patch streaming log file.
#define patchlog_printf(format, ...)											\
	do  {																		\
		if (patch_streaming_log)												\
			filelog_printf(PATCHSTREAMING_LOG_NAME, "\t" format, __VA_ARGS__);	\
	} while (0)

static void patchStreamingGetVersionFromTrivia(void)
{
	char sPatchProject[128];
	char sPatchServer[128];
	char sPatchPort[128];
	char sPatchName[128];
	char sPatchBranch[128];
	char sPatchSandbox[128];
	const char *file_to_search = fileDataDir();
	if (triviaGetPatchTriviaForFile(SAFESTR(sPatchProject), file_to_search, "PatchProject"))
		strcpy(g_PatchProject, sPatchProject);
	if (triviaGetPatchTriviaForFile(SAFESTR(sPatchServer), file_to_search, "PatchServer"))
		strcpy(g_PatchServer, sPatchServer);
	if (triviaGetPatchTriviaForFile(SAFESTR(sPatchPort), file_to_search, "PatchPort"))
		if (atoi(sPatchPort))
			g_PatchPort = atoi(sPatchPort);
	if (triviaGetPatchTriviaForFile(SAFESTR(sPatchName), file_to_search, "PatchName"))
		strcpy(g_PatchName, sPatchName);
	if (triviaGetPatchTriviaForFile(SAFESTR(sPatchBranch), file_to_search, "PatchBranch"))
		if (atoi(sPatchBranch))
			g_PatchBranch = atoi(sPatchBranch);
	if (triviaGetPatchTriviaForFile(SAFESTR(sPatchSandbox), file_to_search, "PatchSandbox"))
		strcpy(g_PatchSandbox, sPatchSandbox);
}

// Enables patch streaming automatically
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CommandLine) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE;
void PatchStreaming(int on)
{
	if (on)
		g_PatchingStreamingOn = true;
	else
		g_PatchingStreamingOn = false;
}

static bool fileContainsString(const char *filename, const char *str)
{
	bool bRet=false;
	int flen;
	char *data = fileAlloc(filename, &flen);
	if (data)
	{
		int i;
		int slen = (int)strlen(str);
		for (i=0; i<flen-slen && !bRet; i++)
		{
			if (strStartsWith(&data[i], str))
				bRet = true;
		}
		fileFree(data);
	}
	return bRet;
}

// Make sure all of the filespecs are the same.
static FileScanAction PatchStreamingCheckFilespecConsistency(const char *fullpath, FolderNode *node, void *pUserData_Proc, void *pUserData_Data)
{
	char *firstfile = pUserData_Data;
	if (node->is_dir)
		return FSA_EXPLORE_DIRECTORY;
	if (*firstfile)
	{
		if (fileCompare(fullpath, firstfile))
			ErrorFilenameTwof(fullpath, firstfile, "Filespecs differ.");
	}
	else
		strcpy_s(firstfile, CRYPTIC_MAX_PATH, fullpath);
	return FSA_EXPLORE_DIRECTORY;
}

// Depending on whether or not the current filespec indicates it is required,
//  enable or disable patch streaming (unless it was explicitly disableD)
AUTO_RUN_FILE;
void PatchStreamingAutoEnable(void)
{
	if (g_PatchingStreamingOn)
	{
		bool leaveStreamingOn = false;
		char trivia_path[MAX_PATH];
		char root_path[MAX_PATH];
		char filespec_path[MAX_PATH];
		loadstart_printf("Auto-enable PatchStreaming...");
		if (!g_PatchServer[0])
		{
			patchStreamingGetVersionFromTrivia();
		}
		if (g_PatchProject[0])
		{
			if (isProductionMode())
			{
				// Find the filespec and check if it has any NotRequired files
				if (getPatchTriviaList(SAFESTR(trivia_path), SAFESTR(root_path), fileDataDir()))
				{
					getDirectoryName(trivia_path);
					sprintf(filespec_path, "%s/%s.filespec", trivia_path, g_PatchProject);
					if (fileContainsString(filespec_path, "NotRequired"))
						leaveStreamingOn = true;
					else
					{
						sprintf(filespec_path, "%s/%s.filespecoverride", trivia_path, g_PatchProject);
						if (fileContainsString(filespec_path, "NotRequired"))
							leaveStreamingOn = true;
					}
				}
			}
			else
			{
				fileScanAllDataDirs2("patchfilespec", PatchStreamingCheckFilespecConsistency, g_ExtraFilespec);
				if (*g_ExtraFilespec && fileContainsString(g_ExtraFilespec, "NotRequired"))
					leaveStreamingOn = true;
			}
		}

		// Remove this after PCL memory usage is <= 80MB or so
		if (isDevelopmentMode() && getPhysicalMemory64(NULL, NULL) < 3900*1024*1024LL)
		{
			// disable this on "low memory" development machines, it takes 250MB!
			loadend_printf(" disabled (low memory)");
			g_PatchingStreamingOn = false;
		}
		if (!leaveStreamingOn)
		{
			loadend_printf(" disabled (no NotRequired in filespec)");
			g_PatchingStreamingOn = false;
		}
		else
		{
			bool make_fast = false;
			if (g_PatchStreamingSimulateForce != -1)
				g_PatchStreamingSimulate = g_PatchStreamingSimulateForce;
			else
			{
				g_PatchStreamingSimulate = !isProductionMode();
				make_fast = g_PatchStreamingSimulate; // Always fast mode if not in production, unless ForceSimulate
			}
			if (make_fast)
				gclPatchStreamingFastMode();
			loadend_printf(" enabled (simulation %s)", g_PatchStreamingSimulate ? (make_fast ? "\"fast mode\"" : "on") : "off");
		}
	}
}

static void patchStreamingGetRequiredFinished(PCL_Client *client, PCL_ErrorCode error, const char *error_details, void * userData)
{
	char buf[MAX_PATH];

	patch_streaming_initial_get = false;

	if(!error)
		patch_streaming_operation_running = false;
	else
	{
		error = pclGetErrorString(error, SAFESTR(buf));
		FatalErrorf("Patching Error Getting Required Files: %s%s%s",
			buf,
			error_details ? ": " : error_details,
			NULL_TO_EMPTY(error_details));
	}
}


// Statistics are recorded in two ways:
// Reporting period, which is periodic, frequent, and fixed-length
// Interval, which is a "session" of patching, during which one or more files are all patched, and at the end of which all files are patched
static struct 
{
	U32 lastTime;
	S64 lastReceivedBase;

	U32 lastElapsed;
	S64 lastReceivedDelta;

	S64 lastReceived;
	S64 totalReceived;

	U64 receivedRealBytes;	// Total number of bytes of uncompressed files received so far
	U64 totalRealBytes;		// Total number of bytes of uncompressed files requested

	U64 simulReceived;		// Total number of simulated bytes received
	U64 simulTotal;			// Total number of simulated bytes requested

	// An interval is one progress bar lifetime, which lasts until there are no more files waiting to download
	U64 intervalStartBytes;	// Number of received bytes, at the start of this interval
	U64 intervalStartTotal;	// Number of total bytes, at the start of this interval

	U32 ss2000LastReceipt;	// Last time we received anything from the Patch Server
} patch_streaming_stats;

void gclPatchStreamingUpdateStats(void)
{
	U32 currentTime = timerCpuMs();
	U32 elapsedTime;
	if (!patch_streaming_stats.lastTime)
		patch_streaming_stats.lastTime = currentTime;
	elapsedTime = currentTime - patch_streaming_stats.lastTime;
	if (elapsedTime > 1000)
	{
		S64 receivedDelta = patch_streaming_stats.lastReceived - patch_streaming_stats.lastReceivedBase;
		patch_streaming_stats.lastReceivedDelta = receivedDelta;
		patch_streaming_stats.lastElapsed = elapsedTime;
		patch_streaming_stats.totalReceived += receivedDelta;

		patch_streaming_stats.lastReceivedBase = patch_streaming_stats.lastReceived;
		patch_streaming_stats.lastTime = currentTime;
	}

}

void gclPatchStreamingGetStats(S64 *total_bytes, S64 *bytes, U32 *ms)
{
	EnterCriticalSection(&csPatchStreamingStats);
	gclPatchStreamingUpdateStats();
	*total_bytes = patch_streaming_stats.totalReceived;
	*bytes = patch_streaming_stats.lastReceivedDelta;
	*ms = patch_streaming_stats.lastElapsed;
	LeaveCriticalSection(&csPatchStreamingStats);
}

static bool patchStreamingProcessRunTimeCallback(PatchProcessStats *stats, void *userData)
{
	EnterCriticalSection(&csPatchStreamingStats);
	if (patch_streaming_stats.lastReceived != stats->actual_transferred)
		patch_streaming_stats.ss2000LastReceipt = timeSecondsSince2000();
	if (stats->actual_transferred < patch_streaming_stats.lastReceivedBase)
		patch_streaming_stats.lastReceivedBase = 0; // Must have reset?
	patch_streaming_stats.lastReceived = stats->actual_transferred;
	patch_streaming_stats.receivedRealBytes = stats->received;
	patch_streaming_stats.totalRealBytes = stats->total;
	gclPatchStreamingUpdateStats();
	LeaveCriticalSection(&csPatchStreamingStats);
	return false;
}

static bool patchStreamingProcessCallback(PatchProcessStats *stats, void *userData)
{
	float fProgress = (stats->total > 0 && stats->total_files > 0)
		? ((float)stats->received/stats->total + (float)stats->received_files/stats->total_files)/2.f // average of file and byte progress
		: -1; // don't show the bar
	static bool did_error = false;
	int i;

	ASSERT_CALLED_IN_SINGLE_THREAD;

	patchStreamingProcessRunTimeCallback(stats, userData);

	if (!gbNoGraphics)
	{
		//float spin = sin(elapsed)*0.25f + 0.5f;
		//gfxDisplayLogoProgress(gGCLState.pPrimaryDevice->device, NULL, 0, NULL, fProgress, spin, false);
		gclLoadEndCallbackInternal(true, fProgress * 100);
	}

	if (stats->xfers)
	{
		estrPrintf(&patch_streaming_xfers, "PCL xfers:\n");
		//2 + 12 + 1 + 7 + 1 + 15 + 1
		estrConcatf(&patch_streaming_xfers, "  %12s %7s %15s %s\n", "State", "Block", "Progress", "Filename");
		for(i = 0; i < stats->xfers; i++)
		{
			char filename[40], * found = strrchr(stats->state_info[i]->filename, '/');
			const char * state = stats->state_info[i]->state;
			char progress[40], progress_total[40];

			filenameInFixedSizeBuffer(stats->state_info[i]->filename, 39, SAFESTR(filename), false);
			if( stricmp("XFER_REQ_FILEINFO", state) == 0 ||
				stricmp("XFER_WAIT_FILEINFO", state) == 0 )
				state = "Starting";
			else if(stricmp("XFER_REQ_FINGERPRINTS", state) == 0 ||
				stricmp("XFER_WAIT_FINGERPRINTS", state) == 0)
				state = "Fingerprints";
			else if(stricmp("XFER_REQ_DATA", state) == 0 ||
				stricmp("XFER_WAIT_DATA", state) == 0)
				state = "Data Blocks";
			else if(stricmp("XFER_COMPLETE", state) == 0)
				state = "Complete";
			else
				state = "Unknown";

			if(stats->state_info[i]->blocks_total == 0)
				strcpy(progress_total, "?");
			else
				itoa(stats->state_info[i]->blocks_total, progress_total, 10);
			sprintf(progress, "%u/%s", stats->state_info[i]->blocks_so_far, progress_total);
			estrConcatf(&patch_streaming_xfers, "  %12s %7u %15s %s\n", state, stats->state_info[i]->block_size, progress, filename);
		}
		estrConcatf(&patch_streaming_xfers, "\n");
	}

	if (stats->seconds - g_last_display >= 5)
	{
		g_last_display = stats->seconds;

		verbose_printf("Received: %.3f MB/%.3f MB (%u/%u Files) Rate: %.3f MB/s\n",
			stats->received / (1024.0 * 1024), stats->total / (1024.0 * 1024), stats->received_files,
			stats->total_files, stats->seconds ? (stats->received / (stats->seconds * 1024.0 * 1024)) : 0);
	}

	if (patch_streaming_initial_get && stats->written_bytes && !did_error && isProductionMode())
	{
		did_error = true;
		Errorf("Some required files or headers needed to be patched on startup.");
	}

	return false;
}

static void patchingConnect(PCL_Client *client, bool updated_UNUSED, PCL_ErrorCode error, const char *error_details, void * userData_UNUSED)
{
	char buf[MAX_PATH];

	if(!error)
	{
		pclAddFileFlags(client, PCL_USE_SAVED_METADATA|PCL_RECONNECT|PCL_TRIM_PATCHDB_FOR_STREAMING);
		if (*g_ExtraFilespec)
			pclAddExtraFilespec(client, g_ExtraFilespec);
		patch_streaming_operation_running = false;
	}
	else
	{
		error = pclGetErrorString(error, SAFESTR(buf));
		FatalErrorf("Unable to initialize on-demand patching and connect to Patch Server: %s%s%s",
			buf,
			error_details ? ": " : "",
			NULL_TO_EMPTY(error_details));
	}

	assert(patch_streaming_connect_timer);
	timerFree(patch_streaming_connect_timer);
	patch_streaming_connect_timer = 0;
}

static void patchingSetView(PCL_Client *client, PCL_ErrorCode error, const char *error_details, void * userData)
{
	if(!error)
		patch_streaming_operation_running = false;
	else
	{
		char buf[MAX_PATH];
		error = pclGetErrorString(error, SAFESTR(buf));
		FatalErrorf("Patching Error Setting View: %s%s%s",
			buf,
			error_details ? ": " : "",
			NULL_TO_EMPTY(error_details));
	}
}

// Make the patch streaming system inactive if we've been idle for a while.
static void gclPatchStreamingInactivity(void)
{
	U32 ss2000LastReceipt;
	int xfers;

	EnterCriticalSection(&csPatchStreamingStats);
	ss2000LastReceipt = patch_streaming_stats.ss2000LastReceipt;
	LeaveCriticalSection(&csPatchStreamingStats);

	pclXfers(g_patchstreaming_client, &xfers);

	if (patch_streaming_inactivity_check
		&& !xfers
		&& !fileLoaderPatchesPending()
		&& timeSecondsSince2000() - ss2000LastReceipt > patch_streaming_inactivity_timeout)
	{
		pclDisconnect(g_patchstreaming_client);
		printf("Patch Streaming: Entering inactivity\n");
		memlog_printf(&patch_streaming_memlog, "Entering inactivity");
		patch_streaming_inactive = true;
		while (patch_streaming_inactivity_check && patch_streaming_inactive)
		{
			LeaveCriticalSection(&csPatchStreaming);
			Sleep(100);
			EnterCriticalSection(&csPatchStreaming);
		}
		memlog_printf(&patch_streaming_memlog, "Waking back up");
		printf("Patch Streaming: Waking up\n");
	}
}

// Entering PCL network packet processing.
static void gclPatchStreamingNetEnter(PCL_Client *client, void * userdata)
{
	EnterCriticalSection(&csPatchStreaming);
}

// Leaving PCL network packet processing.
static void gclPatchStreamingNetLeave(PCL_Client *client, void * userdata)
{
	LeaveCriticalSection(&csPatchStreaming);
}

static void gclPatchStreamingProcessSimulated(void);

// Background patch streaming processing thread
// This uses a background thread, rather than processing in the foreground, for two reasons:
// 1) This allows PCL to use FileLoader without causing deadlocks in FolderCache
// 2) PCL currently does various things that cause slight stalls, which could cause graphical glitches
DWORD WINAPI patchStreamingProcessThread(LPVOID lpParam)
{
	char buf[MAX_PATH];
	PCL_ErrorCode error;
	const char *rootdir;

	EXCEPTION_HANDLER_BEGIN;

	// Get connected.
	EnterCriticalSection(&csPatchStreaming);
	rootdir = fileBaseDir();
	error = pclConnectAndCreate(&g_patchstreaming_client, g_PatchServer, g_PatchPort, 30.0, g_patch_comm,
		rootdir, "PatchStreaming", NULL, patchingConnect, NULL);
	patch_streaming_connect_timer = timerAlloc();
	if(error)
	{
		error = pclGetErrorString(error, SAFESTR(buf));
		FatalErrorf("Patch Server Connection Error: %s", buf);
	}
	else
	{
		//pclSetKeepAliveAndTimeout(g_patchstreaming_client, STREAMING_TIMEOUT_SECONDS);  Doesn't work because *we* don't send keepalives that often during loading/etc
		linkSetKeepAliveSeconds(g_patchstreaming_client->link,5);
		pclAssertOnError(g_patchstreaming_client, true);
		pclSetNetCallbacks(g_patchstreaming_client, gclPatchStreamingNetEnter, gclPatchStreamingNetLeave, NULL);
	}
	LeaveCriticalSection(&csPatchStreaming);

	// PCL processing loop.
	for (;;)
	{
		autoTimerThreadFrameBegin(__FUNCTION__);

		// Receive patch data from the server.
		// PCL will call back into us to enter and leave a critical section if it actually wants to do interesting things.
		commMonitor(g_patch_comm);

		EnterCriticalSection(&csPatchStreaming);

		// Process.
		error = pclProcessTracked(g_patchstreaming_client);
		if(error && !(error == PCL_WAITING || error == PCL_LOST_CONNECTION))
		{
			error = pclGetErrorString(error, buf, MAX_PATH);
			FatalErrorf("Patching Error: %s\n", buf);
		}

		// If the initial connect takes too long, we should fail, rather than stalling on the loading screen, potentially forever.
		if (patch_streaming_connect_timer && timerElapsed(patch_streaming_connect_timer) > patch_streaming_connect_timeout)
		{
			// If we're in dev mode, this means AssetMaster is down.  Just turn off patch streaming.
			if (isDevelopmentMode())
			{
				pclDisconnectAndDestroy(g_patchstreaming_client);
				g_patchstreaming_client = NULL;
				patch_streaming_dev_mode_pcl_fail = true;
				LeaveCriticalSection(&csPatchStreaming);
				return 0;
			}

			patchingConnect(g_patchstreaming_client, true, PCL_TIMED_OUT, NULL, NULL);
		}

		// Simulate.
		gclPatchStreamingProcessSimulated();

		// Check if we should pause processing due to inactivity.
		gclPatchStreamingInactivity();

		LeaveCriticalSection(&csPatchStreaming);

		autoTimerThreadFrameEnd();
	}

	EXCEPTION_HANDLER_END;
}

static void patchStreamingConnectEnter(void)
{
	ManagedThread *thread;

	// Figure out what we're supposed to patch to.
	EnterCriticalSection(&csPatchStreaming);
	if (!g_PatchServer[0])
		patchStreamingGetVersionFromTrivia();

	if(!g_patch_comm)
		g_patch_comm = commCreate(1, 1);

	patch_streaming_operation_running = true;
	LeaveCriticalSection(&csPatchStreaming);

	// Start a background patching thread.
	thread = tmCreateThread(patchStreamingProcessThread, NULL);
}

static void patchStreamingConnectCheck(void)
{
	EnterCriticalSection(&csPatchStreaming);
	if (patch_streaming_dev_mode_pcl_fail)
	{
		devassert(isDevelopmentMode());
		Errorf("Unable to connect to server for patch streaming simulation");
		g_PatchingStreamingOn = false;
		GSM_SwitchToState_Complex("/");
	}
	if (!patch_streaming_operation_running)
		GSM_SwitchToState_Complex(GCL_PATCHSTREAMING "/" GCL_PATCHSTREAMING_SET_VIEW);
	LeaveCriticalSection(&csPatchStreaming);
}

static void patchStreamingSetViewEnter(void)
{
	EnterCriticalSection(&csPatchStreaming);
	patch_streaming_operation_running = true;
	if (g_PatchName[0])
		pclSetNamedView(g_patchstreaming_client, g_PatchProject, g_PatchName, true, false, patchingSetView, NULL);
	else if (g_PatchTime != INT_MAX)
		pclSetViewByTime(g_patchstreaming_client, g_PatchProject, g_PatchBranch, g_PatchSandbox, g_PatchTime, true, false, patchingSetView, NULL);
	else
	{
		assert(isDevelopmentMode()); // Production mode should know where it's coming from
		pclSetViewLatest(g_patchstreaming_client, g_PatchProject, g_PatchBranch, g_PatchSandbox, true, false, patchingSetView, NULL);
	}
	LeaveCriticalSection(&csPatchStreaming);
}

static void patchStreamingSetViewCheck(void)
{
	EnterCriticalSection(&csPatchStreaming);
	if (!patch_streaming_operation_running)
		GSM_SwitchToState_Complex(GCL_PATCHSTREAMING "/" GCL_PATCHSTREAMING_GET_REQUIRED);
	LeaveCriticalSection(&csPatchStreaming);
}

static void patchStreamingGetRequiredEnter(void)
{
	loadstart_printf("Patching...");
	EnterCriticalSection(&csPatchStreaming);
	patch_streaming_initial_get = true;
	patch_streaming_operation_running = true;
	pclSetProcessCallback(g_patchstreaming_client, patchStreamingProcessCallback, NULL);

	// Confirm that we have headers for all required files.
	// If this actually does something, CrypticLauncher has failed, and everything might be completely screwed up.
	// Because of the PCL_TRIM_PATCHDB_FOR_STREAMING flag, the PatchDB will be incomplete, so required files will not
	// be patched up if they are missing.
	pclAddFileFlags(g_patchstreaming_client, PCL_NO_DELETE);
	pclGetRequiredFiles(g_patchstreaming_client, true, true, patchStreamingGetRequiredFinished, NULL, NULL);
	pclRemoveFileFlags(g_patchstreaming_client, PCL_NO_DELETE);

	LeaveCriticalSection(&csPatchStreaming);
}

// Check if the 'get required' operation is complete.
static void patchStreamingGetRequiredCheck(void)
{
	EnterCriticalSection(&csPatchStreaming);
	if (!patch_streaming_operation_running)
		GSM_SwitchToState_Complex("/");
	LeaveCriticalSection(&csPatchStreaming);
}

void gcl_PatchStreamingFrame(void)
{
}

void gcl_PatchStreamingLeave(void)
{
	EnterCriticalSection(&csPatchStreaming);
	EnterCriticalSection(&csPatchStreamingStats);
	patch_streaming_stats.ss2000LastReceipt = timeSecondsSince2000();
	LeaveCriticalSection(&csPatchStreamingStats);
	patch_streaming_inactivity_check = true;
	if(g_patchstreaming_client)
	{
		pclSetHoggsSingleAppMode(g_patchstreaming_client, false);
		pclSetProcessCallback(g_patchstreaming_client, patchStreamingProcessRunTimeCallback, NULL);
	}
	LeaveCriticalSection(&csPatchStreaming);
}

// Mark the micropatch log
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Debug);
void PatchStreamingMark(ACMD_SENTENCE pMarkName)
{
	static U32 sMark = 0;
	patchlog_printf("mark\t%s\t%d", pMarkName, sMark++);
}

// Legacy name
AUTO_COMMAND ACMD_ACCESSLEVEL(1);
void MicroPatchMarkLog(char *pMarkName)
{
	PatchStreamingMark(pMarkName);
}

static void gclPatchStreamingFileCallback(PCL_Client *client, const char *filename, XferStateInfo * info, PCL_ErrorCode error, const char *error_details, void *userData)
{
	FolderNode *node;
	HogFileIndex file_index;
	HogFile *hogfile = NULL;
	char relpath[MAX_PATH];
	bool bUpToDateAlready=false;
	char error_string[256];

	// Make sure it worked.
	if (error != PCL_SUCCESS)
	{
		pclGetErrorString(error, error_string, sizeof(error_string));
		ErrorFilenamef(filename, "Patch streaming failed: %s%s%s", error_string,
			error_details ? ": " : "",
			NULL_TO_EMPTY(error_details));
		return;
	}

	// Log success.
	memlog_printf(&patch_streaming_memlog, "Patching finished: %s\n", filename);
	if (patch_streaming_verbose)
		verbose_printf("Patching finished: %s\n", filename);
	patchlog_printf("finished\t%s\t%f\t%u\t%u\t%u\t%u", filename, timerSeconds64(timerCpuTicks64() - info->start_ticks),
		info->cum_bytes_requested, info->total_bytes_received, info->total_bytes_received, info->filedata_size);

	// Find node, find hogg, set it all up
	strcpy(relpath, filename);
	stripPath(relpath, "data");

	node = FolderCacheQuery(folder_cache, relpath);
	assert(node);
	if (node->virtual_location < 0)
	{
		if (!node->needs_patching && !node->started_patching && node->file_index>0)
		{
			// Another process probably patched this at the same time!
			bUpToDateAlready = true;
		} else {
			assert(node->needs_patching);
			assert(node->started_patching);
		}
		hogfile = PigSetGetHogFile(VIRTUAL_LOCATION_TO_PIG_INDEX(node->virtual_location));
	} else {
		// FolderCache updated this node!  Find the appropriate hogg and virtual_location for it
		int pcl_hogg_index = fileSpecGetHoggIndexForFile(client->filespec, filename);
		HogFile *pcl_hogg_file = fileSpecGetHoggHandle(client->filespec, pcl_hogg_index);
		int i;
		int pig_index = -1;
		assert(pcl_hogg_index != -1);
		assert(pcl_hogg_file);
		for (i=0; i<PigSetGetNumPigs(); i++)
		{
			if (pcl_hogg_file == PigSetGetHogFile(i))
			{
				hogfile = PigSetGetHogFile(i);
				pig_index = i;
				break;
			}
		}
		assert(pig_index != -1);
		node->virtual_location = PIG_INDEX_TO_VIRTUAL_LOCATION(pig_index);
	}
	file_index = hogFileFind(hogfile, relpath);
	assert(file_index != HOG_INVALID_INDEX);
	if (bUpToDateAlready)
	{
		assert((HogFileIndex)node->file_index == file_index);
	}
	node->file_index = file_index;
	node->needs_patching = 0;
	node->started_patching = 0;

	FolderNodeLeaveCriticalSection();
}

static int gclPatchStreamingSimulatedPatchSizeCompare(const void *lhs_ptr, const void *rhs_ptr)
{
	const PatchStreamingSimulatedPatch **lhs = lhs_ptr;
	const PatchStreamingSimulatedPatch **rhs = rhs_ptr;
	return (*rhs)->totalBytes - (*lhs)->totalBytes;
}

static void gclPatchStreamingSimulateValidate()
{
	EARRAY_CONST_FOREACH_BEGIN(streaming_simulated_patches, i, n);
	{
		PatchStreamingSimulatedPatch *simul = streaming_simulated_patches[i];
		assert(simul->bytesRemaining <= simul->totalBytes);
		assert(strlen(simul->relpath));
		if (i)
			assert(simul->totalBytes <= streaming_simulated_patches[i-1]->totalBytes);
	}
	EARRAY_FOREACH_END;
}

static PatchStreamingStartResult gclPatchStreamingRequestPatchInternal(const char *relpath, bool highPriority)
{
	PCL_ErrorCode error;
	char patch_path[MAX_PATH];
	FolderNode *node;
	PatchStreamingStartResult ret;
	bool bLikelyFull;
	int xfers;
	int max_xfers;

	PERFINFO_AUTO_START_FUNC();

	EnterCriticalSection(&csPatchStreaming);
	pclXfers(g_patchstreaming_client, &xfers);
	//pclMaxXfers(g_patchstreaming_client, &max_xfers);
	// It seems that with the default number of transfers, performance (including patching performance) tanks horribly, but 2 works great
	if (highPriority)
		max_xfers = patchstreaming_max_xfers*2;
	else
		max_xfers = patchstreaming_max_xfers;
	bLikelyFull = false; // xfers >= max_xfers;

	node = FolderCacheQuery(folder_cache, relpath);
	if (node && node->needs_patching)
	{
		if (!node->started_patching)
		{
			if (bLikelyFull)
			{
				ret = PSSR_Full;
				node = NULL;
			} else {
				ret = PSSR_Started;
				node->started_patching = 1;
				FolderCacheGetRealPath(folder_cache, node, SAFESTR(patch_path));
			}
		} else {
			// Already started
			ret = PSSR_AlreadyStarted;
			node = NULL;
		}
	} else {
		ret = PSSR_NotNeeded;
		// Doesn't need patching
		node = NULL;
	}
	if (!node)
	{
		FolderNodeLeaveCriticalSection();
		LeaveCriticalSection(&csPatchStreaming);
		PERFINFO_AUTO_STOP_FUNC();
		return ret;
	}

	pclFixPath(g_patchstreaming_client, SAFESTR(patch_path));

	// Decide if we should create a new stats interval.
	g_last_files = fileLoaderPatchesPending() + 1;
	if (g_last_files == 1 && patch_streaming_stats.ss2000LastReceipt + 3 < timeSecondsSince2000())
	{
		EnterCriticalSection(&csPatchStreamingStats);
		patch_streaming_stats.intervalStartBytes = patch_streaming_stats.receivedRealBytes + patch_streaming_stats.simulReceived;
		patch_streaming_stats.intervalStartTotal = patch_streaming_stats.totalRealBytes + patch_streaming_stats.simulTotal;
		LeaveCriticalSection(&csPatchStreamingStats);
	}

	patch_streaming_inactive = false;
	if (g_PatchStreamingSimulate)
	{
		PatchStreamingSimulatedPatch *simul;
		int index;
		simul = callocStruct(PatchStreamingSimulatedPatch);
		simul->relpath = allocAddFilename(patch_path);
		simul->msRemaining = patch_streaming_simulate_latency_ms;
		simul->totalBytes = (U32)node->size;
		simul->bytesRemaining = (U32)simul->totalBytes;
		gclPatchStreamingSimulateValidate();
		index = (int)eaBFind(streaming_simulated_patches, gclPatchStreamingSimulatedPatchSizeCompare, simul);
		eaInsert(&streaming_simulated_patches, simul, index);
		gclPatchStreamingSimulateValidate();
		EnterCriticalSection(&csPatchStreamingStats);
		patch_streaming_stats.simulTotal += simul->totalBytes;
		patch_streaming_stats.ss2000LastReceipt = timeSecondsSince2000();
		LeaveCriticalSection(&csPatchStreamingStats);
		error = PCL_SUCCESS;
	} else {
		error = pclGetFile(g_patchstreaming_client, patch_path, 1, gclPatchStreamingFileCallback, NULL);
		EnterCriticalSection(&csPatchStreamingStats);
		patch_streaming_stats.ss2000LastReceipt = timeSecondsSince2000();
		LeaveCriticalSection(&csPatchStreamingStats);
	}
	if (error != PCL_SUCCESS && error != PCL_XFERS_FULL && error != PCL_WAITING)
	{
		gclPatchStreamingHandleDisconnect();
	}
	assert(error == PCL_SUCCESS || error == PCL_XFERS_FULL || error == PCL_WAITING);
	if (error == PCL_SUCCESS)
	{
		memlog_printf(&patch_streaming_memlog, "Patching(async) %s\n", patch_path);
		if (patch_streaming_verbose)
			verbose_printf("Patching(async) %s\n", patch_path);
		patchlog_printf("starting\t%s\t%d\n", node->name, node->size);
		ret = PSSR_Started;
	} else if (error == PCL_XFERS_FULL || error == PCL_WAITING) {
		ret = PSSR_Full;
		node->started_patching = 0;
	} else {
		assert(0);
	}

	FolderNodeLeaveCriticalSection();
	LeaveCriticalSection(&csPatchStreaming);

	PERFINFO_AUTO_STOP_FUNC();

	return ret;
}

static PatchStreamingStartResult gclPatchStreamingRequestPatch(const char *relpath)
{
	return gclPatchStreamingRequestPatchInternal(relpath, false);
}

static void gclPatchStreamingHandleDisconnect(void)
{
	// If this reconnects, we need to eventually fatally error if we reconnect multiple times while patching
	// the same file in gcl_PatchStreamingCallback
	FatalErrorf("Patch Server disconnected");
}

static void gclPatchStreamingProcessSimulated(void)
{
	static U32 last_time;
	static U64 bytes_available;
	U32 delta_time;
	U32 current_time;
	U64 bytes_per_transfer;
	int num_transferring=0;

	PERFINFO_AUTO_START_FUNC();

	if (!eaSize(&streaming_simulated_patches))
		return;

	if (!TryEnterCriticalSection(&csPatchStreaming))
		return;

	current_time = timerCpuMs();
	if (!last_time)
		last_time = current_time;
	delta_time = current_time - last_time;
	MIN1(delta_time, 1000); // Max of 1s simulated per call

	bytes_available += delta_time * patch_streaming_simulate_kbps;
	gclPatchStreamingSimulateValidate();
	FOR_EACH_IN_EARRAY(streaming_simulated_patches, PatchStreamingSimulatedPatch, simul)
	{
		if (delta_time >= simul->msRemaining)
		{
			num_transferring++;
			simul->msRemaining = 0;
		} else
			simul->msRemaining -= delta_time;
	}
	FOR_EACH_END;
	if (num_transferring)
	{
		bytes_per_transfer = bytes_available / num_transferring;
		bytes_available -= bytes_per_transfer * num_transferring;
		EnterCriticalSection(&csPatchStreamingStats);
		patch_streaming_stats.lastReceived += bytes_per_transfer * num_transferring;
		patch_streaming_stats.ss2000LastReceipt = timeSecondsSince2000();
		LeaveCriticalSection(&csPatchStreamingStats);
		FOR_EACH_IN_EARRAY(streaming_simulated_patches, PatchStreamingSimulatedPatch, simul)
		{
			if (!simul->msRemaining)
			{
				if (bytes_per_transfer >= simul->bytesRemaining)
				{
					XferStateInfo simul_info;

					// Done!
					// Make up some fake state info.
					simul_info.filename = simul->relpath;
					simul_info.state = "Simulated";
					simul_info.filedata_size = simul->totalBytes;

					gclPatchStreamingFileCallback(g_patchstreaming_client, simul->relpath, &simul_info, PCL_SUCCESS, NULL, NULL);
					eaRemove(&streaming_simulated_patches, isimulIndex);
					EnterCriticalSection(&csPatchStreamingStats);
					patch_streaming_stats.simulReceived += simul->bytesRemaining;
					LeaveCriticalSection(&csPatchStreamingStats);
					free(simul);
				} else {
					simul->bytesRemaining -= bytes_per_transfer;
					EnterCriticalSection(&csPatchStreamingStats);
					patch_streaming_stats.simulReceived += bytes_per_transfer;
					LeaveCriticalSection(&csPatchStreamingStats);
				}
			}
		}
		FOR_EACH_END;
	} else
		bytes_available = 0;
	gclPatchStreamingSimulateValidate();

	last_time = current_time;
	LeaveCriticalSection(&csPatchStreaming);
	PERFINFO_AUTO_STOP();
}

static void gclPatchStreamingProcessFileLoaderCallback(void)
{
}

// The number of files that need to be patched
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingPatchesPending");
int gclPatchStreamingPatchesPending(SA_PARAM_OP_VALID Entity *pEntity)
{
	return fileLoaderPatchesPending();
}

// The beginning of the previous reporting period, SS2000
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsLastTime");
U32 gclPatchStreamingStatsLastTime(SA_PARAM_OP_VALID Entity *pEntity)
{
	U32 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.lastTime;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Patched data (real and simul combined) at the beginning of previous reporting period, bytes uncompressed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsLastReceivedBase");
S64 gclPatchStreamingStatsLastReceivedBase(SA_PARAM_OP_VALID Entity *pEntity)
{
	S64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.lastReceivedBase;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Duration of the previous reporting period, seconds
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsLastElapsed");
U32 gclPatchStreamingStatsLastElapsed(SA_PARAM_OP_VALID Entity *pEntity)
{
	U32 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.lastElapsed;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Data patched in the last reporting period, bytes uncompressed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsLastReceivedDelta");
S64 gclPatchStreamingStatsLastReceivedDelta(SA_PARAM_OP_VALID Entity *pEntity)
{
	S64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.lastReceivedDelta;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Data patched, at the beginning of the last reporting period, bytes uncompressed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsLastReceived");
S64 gclPatchStreamingStatsLastReceived(SA_PARAM_OP_VALID Entity *pEntity)
{
	S64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.lastReceived;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Data patched so so far in this period, bytes uncompressed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsTotalReceived");
S64 gclPatchStreamingStatsTotalReceived(SA_PARAM_OP_VALID Entity *pEntity)
{
	S64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.totalReceived;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// "Real" data patched so far, bytes uncompressed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsReceivedRealBytes");
U64 gclPatchStreamingStatsReceivedRealBytes(SA_PARAM_OP_VALID Entity *pEntity)
{
	U64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.receivedRealBytes;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// "Real" data requested so far, bytes uncompressed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsTotalRealBytes");
U64 gclPatchStreamingStatsTotalRealBytes(SA_PARAM_OP_VALID Entity *pEntity)
{
	U64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.totalRealBytes;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Simulated data patched so far, bytes uncompressed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsSimulReceived");
U64 gclPatchStreamingStatsSimulReceived(SA_PARAM_OP_VALID Entity *pEntity)
{
	U64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.simulReceived;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Simulated data requested so far, bytes uncompressed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsSimulTotal");
U64 gclPatchStreamingStatsSimulTotal(SA_PARAM_OP_VALID Entity *pEntity)
{
	U64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.simulTotal;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Data received so far, at the beginning of the interval, bytes uncompressed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsIntervalStartBytes");
U64 gclPatchStreamingStatsIntervalStartBytes(SA_PARAM_OP_VALID Entity *pEntity)
{
	U64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.intervalStartBytes;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Data requested so far, at the beginning of the interval, bytes uncompressed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsIntervalStartTotal");
U64 gclPatchStreamingStatsIntervalStartTotal(SA_PARAM_OP_VALID Entity *pEntity)
{

	U64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.intervalStartTotal;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Last time we received anything from the Patch Server, SS2000
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingStatsLastReceipt");
U32 gclPatchStreamingStatsLastReceipt(SA_PARAM_OP_VALID Entity *pEntity)
{

	U32 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.ss2000LastReceipt;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Simple number of bytes patched so far, in this interval
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingGetCompletedBytes");
U64 gclPatchStreamingGetCompletedBytes(SA_PARAM_OP_VALID Entity *pEntity)
{
	U64 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.receivedRealBytes + patch_streaming_stats.simulReceived - patch_streaming_stats.intervalStartBytes;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

// Simple number of bytes requested to be patched, in this interval
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PatchStreamingGetTotalBytes");
S32 gclPatchStreamingGetTotalBytes(SA_PARAM_OP_VALID Entity *pEntity)
{
	S32 ret;
	EnterCriticalSection(&csPatchStreamingStats);
	ret = patch_streaming_stats.totalRealBytes + patch_streaming_stats.simulTotal - patch_streaming_stats.intervalStartTotal;
	LeaveCriticalSection(&csPatchStreamingStats);
	return ret;
}

void gclPatchStreamingProcess(void)
{
	// Called by game only in main thread
	S64 total_bytes;
	S64 bytes;
	U32 ms;
	int files;

	PERFINFO_AUTO_START_FUNC();

	if (!isPatchStreamingOn())
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	files = fileLoaderPatchesPending();
	gclPatchStreamingGetStats(&total_bytes, &bytes, &ms);
	if ((bytes || files) && !gbNoGraphics && !gConf.bDisablePatchStreamingBuiltinProgress)
	{
		static AtlasTex *indicator;
		static float rotation;
		U64 completed, total;
		float completed_bar = 0, total_bar;
		const float max_bar_length = 50;
		const float tiles_per_kb = max_bar_length/(50*1024);
		char buf1[64];
		char *buf2 = NULL;
		int x, y;
		unsigned color = 0xFFFFFFff;

		estrStackCreate(&buf2);
		estrPrintf(&buf2, "Patch%s %d file%s, %s/s\n\n",
			files ? "ing" : "ed",
			files ? files : g_last_files,
			files > 1 || g_last_files != 1 ? "s" : "",
			friendlyBytesBuf(bytes*1000/AVOID_DIV_0(ms), buf1));
		
		if (patch_streaming_verbose)
		{
			EnterCriticalSection(&csPatchStreaming);
			gclPatchStreamingSimulateValidate();
			EnterCriticalSection(&csPatchStreamingStats);
			EARRAY_CONST_FOREACH_BEGIN(streaming_simulated_patches, i, n);
			{
				F32 tot_num;
				char *tot_units;
				U32 tot_prec;
				humanBytes(streaming_simulated_patches[i]->totalBytes, &tot_num, &tot_units, &tot_prec);
				estrConcatf(&buf2, "%s: %.*f%s\n", streaming_simulated_patches[i]->relpath, tot_prec, tot_num, tot_units);
			}
			EARRAY_FOREACH_END;
			estrAppend(&buf2, &patch_streaming_xfers);
			LeaveCriticalSection(&csPatchStreamingStats);
			LeaveCriticalSection(&csPatchStreaming);
		}

		// Calculate the progress bar parameters.
		EnterCriticalSection(&csPatchStreamingStats);
		completed = patch_streaming_stats.receivedRealBytes + patch_streaming_stats.simulReceived - patch_streaming_stats.intervalStartBytes;
		completed = completed >= 0 ? completed : 0;
		total = patch_streaming_stats.totalRealBytes + patch_streaming_stats.simulTotal - patch_streaming_stats.intervalStartTotal;
		total = total >= 0 ? total : 0;
		total_bar = total * (tiles_per_kb/1024);
		total_bar = total_bar <= max_bar_length ? total_bar : max_bar_length;
		if (total)
			completed_bar = total_bar * ((float)completed/total);
		LeaveCriticalSection(&csPatchStreamingStats);

		// Allow fast mode to be turned on by clicking on the spinner.
		mousePos(&x, &y);
		if (eaSize(&streaming_simulated_patches) && (x - 8) * (x - 8) + (y - 8) * (y - 8) < 10*10)
		{
			color = 0x800000ff;
			if (inpLevel(INP_LBUTTON))
			{
				color = 0xFF0000ff;
				gclPatchStreamingFastMode();
			}
		}

		// Draw spinner
		if (!indicator)
			indicator = atlasLoadTexture("PatchingIndicator");
		gfxXYprintfColor(2, 1, 255, 255, 255, 180, "%-32s", buf2);
		estrDestroy(&buf2);
		if (patch_streaming_stats.ss2000LastReceipt + 5 >= timeSecondsSince2000())
		{
			rotation += gfxGetFrameTime()*4;
			if (rotation > TWOPI)
				rotation -= TWOPI;
		}
		display_sprite_rotated(indicator, gfxXYgetX(0)+8, gfxXYgetY(0)+8, rotation, GRAPHICSLIB_Z, 0.5, color);

		// Draw the patching progress bar.
		if (total_bar)
		{
			display_sprite_rotated_ex(white_tex_atlas, NULL, gfxXYgetX(0)+300, gfxXYgetY(0)+4, GRAPHICSLIB_Z, total_bar, 1, 0xCDCDCDdc, 0, 0);
			display_sprite_rotated_ex(white_tex_atlas, NULL, gfxXYgetX(0)+300, gfxXYgetY(0)+4, GRAPHICSLIB_Z+1, completed_bar, 1, 0xE0E0FFdc, 0, 0);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}


static bool gcl_PatchStreamingCallback(FolderCache * cache, FolderNode * node, const char *relpath, PigFileDescriptor *pfd, bool header_only)
{
	PCL_ErrorCode error;
	char real_path[MAX_PATH], patch_path[MAX_PATH], *location;
	HogFile *hogfile;
	HogFileIndex file_index;
	int virtual_location;

	assert(g_patchstreaming_client);

	//assert(heapValidateAll());
	virtual_location = node->virtual_location;
	assert(virtual_location < 0);
	location = FolderCache_GetFromVirtualLocation(folder_cache, virtual_location);
	hogfile = PigSetGetHogFile(VIRTUAL_LOCATION_TO_PIG_INDEX(virtual_location));
	assert(hogfile);
	FolderCacheGetRealPath(cache, node, SAFESTR(real_path));
	node = NULL;
	FolderNodeLeaveCriticalSection();

	strcpy(patch_path, real_path);
	pclFixPath(g_patchstreaming_client, SAFESTR(patch_path));

	EnterCriticalSection(&csPatchStreaming);

	if (header_only && pfd)
	{
		// fill in PFD
		ZeroStruct(pfd);
		if (PCL_SUCCESS == pclGetFileHeader(g_patchstreaming_client, patch_path, &pfd->parent_hog, &pfd->file_index))
		{
			assert(pfd->parent_hog); // Should only ask for a header on something that'll succeed?
			if (pfd->parent_hog)
			{
				pfd->debug_name = hogFileGetFileName(pfd->parent_hog, pfd->file_index);
				if (pfd->debug_name && !hogFileIsSpecialFile(pfd->parent_hog, pfd->file_index))
				{
					U32 header_data_size;
					hogFileGetHeaderData(pfd->parent_hog, pfd->file_index, &header_data_size);
					pfd->header_data_size = header_data_size;
					pfd->size = hogFileGetFileSize(pfd->parent_hog, pfd->file_index);
					LeaveCriticalSection(&csPatchStreaming);
					return true;
				} else {
					assert(0);
				}
			}
		}
	}

	if (1)
	{
		LeaveCriticalSection(&csPatchStreaming);
		if (fileNeedsPatching(relpath))
		{
			PatchStreamingStartResult res = gclPatchStreamingRequestPatchInternal(relpath, true);
			U32 timeout_ms = STREAMING_TIMEOUT_SECONDS*1000;
			U32 last_time;

			// This is bad because it can possibly stall the main thread (or whichever thread calls this)
			//  for an indeterminate amount of time, with no feedback to the user.  The filespec for
			//  this project should be adjusted to include this file as "required", or the code
			//  should be adjusted to preload this file asynchronously through fileLoader.
			verbose_printf("Synchronously patch streaming file: %s\n", relpath);
			// Note there is a specific exclusion when the editor is active, because editors may need
			// to synchronously access files in various circumstances.  Making the editors use
			// asynchronous loading just to avoid this error really wouldn't serve any purpose.
			if (!emIsEditorActive())
			{
				ErrorDetailsf("%s", relpath);
				Errorf("Synchronously patch streaming file");
			}
			
			last_time = timerCpuMs();
			while (fileNeedsPatching(relpath))
			{
				if (res == PSSR_Full)
					res = gclPatchStreamingRequestPatchInternal(relpath, true);
				//else if (fileNeedsPatching(relpath)) // perhaps disconnect, re-request it
				//	res = gclPatchStreamingRequestPatchInternal(relpath, true);
				{
					U32 new_time = timerCpuMs();
					U32 time_delta = new_time - last_time;
					MIN1(time_delta, 1000); // Decrement no more than 1s (debugger, stalls, etc)
					if (time_delta >= timeout_ms)
					{
						// Timeout!
						gclPatchStreamingHandleDisconnect();
						// If we reconnected, reset timeout?
						timeout_ms = STREAMING_TIMEOUT_SECONDS*1000;
						last_time = timerCpuMs();
					} else {
						timeout_ms -= time_delta;
					}
					last_time = new_time;
				}
			}
		}
	} else {
		memlog_printf(&patch_streaming_memlog, "Patching %s\n", real_path);
		if (patch_streaming_verbose)
			verbose_printf("Patching %s\n", real_path);

		if (0)
		{
			pclGetFile(g_patchstreaming_client, patch_path, 1, NULL, NULL);
		} else {
			char full_path[MAX_PATH];
			char *pathptr = full_path;
			int recurse = 0;
			pclRootDir(g_patchstreaming_client, SAFESTR(full_path));
			strcatf(full_path, "/%s", patch_path);
			error = pclGetFileList(g_patchstreaming_client,
				&pathptr,
				&recurse,
				false,
				1,
				NULL,
				NULL,
				NULL);
		}
		error = pclWait(g_patchstreaming_client);

		assert(error == PCL_SUCCESS);
		LeaveCriticalSection(&csPatchStreaming);

		file_index = hogFileFind(hogfile, relpath);
		assert(file_index != HOG_INVALID_INDEX);
		node = FolderCacheQuery(folder_cache, relpath);
		assert(node);
		node->virtual_location = virtual_location;
		node->file_index = file_index;
		node->needs_patching = 0;
		FolderNodeLeaveCriticalSection();
	}
	return false;
}

void gcl_PatchStreamingFinish(void)
{
	PCL_ErrorCode error = PCL_SUCCESS;

	if(!g_patchstreaming_client) // patching was skipped
		return;

	EnterCriticalSection(&csPatchStreaming);

	loadend_printf(" done.");
	loadstart_printf("Updating FolderCache with dynamic patching info...");
	error = pclAddFilesToFolderCache(g_patchstreaming_client, folder_cache);
	if(error != PCL_SUCCESS)
	{
		char msg[MAX_PATH];
		error = pclGetErrorString(error, SAFESTR(msg));
		FatalErrorf("Error Finishing Patching: %s", msg);
	}

	folder_cache->patchCallback = gcl_PatchStreamingCallback;
	loadend_printf(" done.");

// 	if(!g_PatchDynamic)
// 	{
// 		pclDisconnectAndDestroy(g_patchstreaming_client);
// 		g_patchstreaming_client = NULL;
// //		commDestroy(&g_patch_comm); // currently this just leaks anyway.  might as well keep it.
// 	}

	LeaveCriticalSection(&csPatchStreaming);
}

void gcl_PatchStreamingEnter(void)
{
	static bool inited = false;

	if(!inited)
	{
		GSM_AddGlobalState(GCL_PATCHSTREAMING_CONNECT);
		GSM_AddGlobalStateCallbacks(GCL_PATCHSTREAMING_CONNECT, patchStreamingConnectEnter, NULL, patchStreamingConnectCheck, NULL);

		GSM_AddGlobalState(GCL_PATCHSTREAMING_SET_VIEW);
		GSM_AddGlobalStateCallbacks(GCL_PATCHSTREAMING_SET_VIEW, patchStreamingSetViewEnter, NULL, patchStreamingSetViewCheck, NULL);

		GSM_AddGlobalState(GCL_PATCHSTREAMING_GET_REQUIRED);
		GSM_AddGlobalStateCallbacks(GCL_PATCHSTREAMING_GET_REQUIRED, patchStreamingGetRequiredEnter, NULL, patchStreamingGetRequiredCheck, NULL);

		InitializeCriticalSection(&csPatchStreaming);
		InitializeCriticalSection(&csPatchStreamingStats);

		fileLoaderSetPatchStreamingCallbacks(gclPatchStreamingRequestPatch, fileNeedsPatching, gclPatchStreamingProcessFileLoaderCallback);

		inited = true;
	}

	GSM_AddChildState(GCL_PATCHSTREAMING_CONNECT, true);
}

// Make debug marker for map loading begin.
void gclPatchStreamingDebugLoadingStarted()
{
	patchlog_printf("loadstart\t%s", NULL_TO_EMPTY(zmapInfoGetPublicName(NULL)));
}

// Make debug marker for map loading end.
void gclPatchStreamingDebugLoadingFinished()
{
	patchlog_printf("loadend\t%s", NULL_TO_EMPTY(zmapInfoGetPublicName(NULL)));
}

// Debug command
AUTO_COMMAND ACMD_CATEGORY(Debug);
char *PatchStreamingIsRequired(char *filename)
{
	bool not_required;
	char *debug = NULL;
	static char *result = NULL;
	PCL_ErrorCode error;
	if (!g_patchstreaming_client)
		return "unavailable";
	estrStackCreate(&debug);
	error = pclIsNotRequired(g_patchstreaming_client, filename, &not_required, &debug);
	if (error)
		return "error";
	estrPrintf(&result, "%s: %s, %s", filename, not_required ? "OPTIONAL" : "REQUIRED", debug);
	estrDestroy(&debug);
	return result;
}
