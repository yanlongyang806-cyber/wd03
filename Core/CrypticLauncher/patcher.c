#include "patcher.h"
#include "CrypticLauncher.h"
#include "registry.h"
#include "embedbrowser.h"
#include "launcherUtils.h"
#include "GameDetails.h"
#include "systemtray.h"
#include "resource_CrypticLauncher.h"

#include "pcl_typedefs.h"
#include "pcl_client.h"
#include "SimpleWindowManager.h"
#include "net.h"
#include "accountnet.h"
#include "CrypticPorts.h"
#include "GlobalComm.h"
#include "ScratchStack.h"
#include "cmdparse.h"
#include "patchtrivia.h"
#include "patchcommonutils.h"
#include "pcl_client_struct.h"
#include "utils.h"
#include "sysutil.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "error.h"
#include "EString.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "wininclude.h"
#include "ThreadSafeQueue.h"
#include "hoglib.h"
#include "error.h"
#include "earray.h"
#include "timing.h"
#include "StringUtil.h"
#include "MemTrack.h"

#include "..\..\core\NewControllerTracker\pub\NewControllerTracker_pub.h"

#define HUGE_ERROR_STRING 2048
#define HANDLE_ERROR(error, connecting)\
	if(error)\
	{\
	char * patch_error = ScratchAlloc(HUGE_ERROR_STRING);\
	int error_error = pclGetErrorString(error, patch_error, ScratchSize(patch_error));\
	Errorf("Launcher PCL Error %s", patch_error);\
	if (connecting && error == PCL_TIMED_OUT)\
		MessageBox(NULL, "Unable to establish connection to Patch Server.  Please check your Internet connection.  If problems persist, please contact Technical Support.", _("Connection problem"), MB_OK|MB_ICONERROR);\
	else\
		MessageBox(NULL, patch_error, _("Patcher error"), MB_OK|MB_ICONERROR);\
	exit(error + 100);\
	ScratchFree(patch_error);\
	}

#define DisplayMsg(window, msg) postCommandString(((CrypticLauncherWindow*)(window)->pUserData)->queueFromPCL, CLCMD_DISPLAY_MESSAGE, (msg))
#define SetProgress(window, msg) postCommandString(((CrypticLauncherWindow*)(window)->pUserData)->queueFromPCL, CLCMD_SET_PROGRESS, (msg))
#define setButtonState(window, state) postCommandInt(((CrypticLauncherWindow*)(window)->pUserData)->queueFromPCL, CLCMD_SET_BUTTON_STATE, (U32)(state))

#define RETRY_TIMEOUT 30
#define RETRY_BACKOFF 5

int g_dontAutopatch = 0;
AUTO_CMD_INT(g_dontAutopatch, dontautopatch) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

int g_crashme = 0;
AUTO_CMD_INT(g_crashme, crash) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

int g_autoPatchTimeout = 60;
AUTO_CMD_INT(g_autoPatchTimeout, autopatchtimeout) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

int g_patchAll = 0;
AUTO_CMD_INT(g_patchAll, patchall) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

// this overrides ONLY the auto patch/cryptic launcher patch server.  NOT the game patch.
char *g_patchserver = CRYPTICLAUNCHER_PATCHSERVER;
AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0); void server(char *str) { g_patchserver = strdup(str); }

// Force the CrypticLauncher to autoupdate, even if it ordinarily wouldn't.
static int s_forceAutoPatch = 0;
AUTO_CMD_INT(s_forceAutoPatch, forceAutoPatch) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

extern ShardInfo_Basic *g_prepatch;
extern bool g_launcher_debug;
extern bool g_pwrd_mode;

static bool isDefaultCrypticLauncherAutoPatchServer(void)
{
	return stricmp(g_patchserver, CRYPTICLAUNCHER_PATCHSERVER) == 0;
}

static char *autoPatchToken(U32 gameID)
{
	static char token[64];
	strcpy(token, "CrypticLauncher");
	if(gameID)
		strcat(token, gdGetCode(gameID));
	return token;
}


static S64 *autoPatchTimer = NULL;
static bool autoPatchShutdown = false;
static bool autoPatchDidShutdown = false;

static void autoPatchConnectCallback(PCL_Client *client, bool updated, PCL_ErrorCode error, const char *error_details, const char *exe_name)
{
	if(updated)
	{
		char *cmd=NULL;
		SimpleWindow *main_window = SimpleWindowManager_FindWindow(CL_WINDOW_MAIN, 0);
		if(main_window && main_window->pUserData)
		{
			CrypticLauncherWindow *launcher = main_window->pUserData;
			if(launcher->username && launcher->username[0])
			{
				char *input=NULL;

				// This feature is deprecated; if this devassert is hit, you'll need to change
				// the password being passed to be the hashed variety. Note that the only hash
				// currently accepted is the PWE one.
				devassertmsg(launcher->passwordHashed, "Cannot pass unhashed password to Cryptic Launcher");

				estrPrintf(&cmd, "\"%s\" -readpassword 1 %s", exe_name, GetCommandLineWithoutExecutable());
				estrPrintf(&input, "%s\n%s\n", launcher->username, launcher->password);
				printf("Restarting after auto-patch (w/ login): %s\n", cmd);
				system_w_input(cmd, input, estrLength(&input), true, false);
				exit(0);
			}
		}

		assert(estrPrintf(&cmd, "\"%s\" %s", exe_name, GetCommandLineWithoutExecutable()) >= 0);
		printf("Restarting after auto-patch: %s\n", cmd);
		system_detach(cmd, 0, 0);
		exit(0);
	}
}

static BOOL autoPatchDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		SetTimer(hDlg, 1, 100, NULL);
		break;
	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDCANCEL:
			// Handler for the red X in the corner
			exit(0);
			break;
		}
		break;
	case WM_TIMER:
		{
			PCL_Client **ppClient = pWindow->pUserData, *client;
			if(!autoPatchShutdown && *ppClient)
			{
				char buf[1024];
				client = *ppClient;
				if(client->retry_count)
					sprintf(buf, "Tried connecting %d times.", client->retry_count+1);
				else
					strcpy(buf, "Tried connecting 1 time.");
				SetWindowText(GetDlgItem(hDlg, IDC_RETRIES), buf);
			}
		}
		break;
	}

	return FALSE;
}

static bool autoPatchTickProc(SimpleWindow *ppClient)
{
	static bool shown = false;
	if(autoPatchTimer && !shown && timerSeconds64(timerCpuTicks64() - *autoPatchTimer) > 2)
	{
		shown = true;
		SimpleWindowManager_AddOrActivateWindow(CL_WINDOW_AUTOPATCH, 0, IDD_AUTOPATCH, false, autoPatchDialogProc, NULL, ppClient);
	}
	if(autoPatchShutdown)
		return false;
	return true;
}

static void autoPatchDialogThread(PCL_Client **ppClient)
{
	SimpleWindowManager_Run(autoPatchTickProc, (void*)ppClient);
	printf("AutopatchDialog thread shutting down\n");
	autoPatchDidShutdown = true;
}

void autoPatch(U32 gameID)
{
	PCL_Client *client = NULL;
	PCL_ErrorCode error;
	char autoupdate_path[MAX_PATH] = "";
	int attempts = 0;
	SimpleWindow *autopatch_window = NULL;
	uintptr_t autopatch_thread;
	NetComm *comm = NULL;
	char *patchServer;

	if(g_dontAutopatch)
	{
		printf("Skipping self-patch because of command-line argument.\n");
		return;
	}

	if (!isDefaultCrypticLauncherAutoPatchServer() || !g_pwrd_mode)
	{
		patchServer = g_patchserver;
	}
	else
	{
		patchServer = PWRD_PATCHSERVER_HOST;
	}

	strcpy(autoupdate_path, getExecutableName());
	forwardSlashes(autoupdate_path);

	if( !s_forceAutoPatch && (
			strstri(autoupdate_path, "/Core/tools/bin/") ||
			strstri(autoupdate_path, "/FightClub/tools/bin/") ||
			strstri(autoupdate_path, "/StarTrek/tools/bin/") ||
			strstri(autoupdate_path, "/Night/tools/bin/") ||
			strstri(autoupdate_path, "/Creatures/tools/bin/") ||
			strstri(autoupdate_path, "/Bronze/tools/bin/") ||
			strstri(autoupdate_path, "/src/")))
	{
		printf("Skipping self-patch because the client is in Utilities/bin or Core/tools/bin or src/.\n");
		return;
	}

	autopatch_thread = _beginthread(autoPatchDialogThread, 0, &client);

	do {
		if(client == NULL)
		{
			printf("Connecting to %s:%i\n", patchServer, CRYPTICLAUNCHER_PATCHSERVER_PORT);
		}
		else
		{
			printf("The client was disconnected during a connect attempt\n");
			autoPatchTimer = NULL;
			pclDisconnectAndDestroy(client);
			client = NULL;
			attempts++;
		}

		if(!comm)
			comm = commCreate(0, 1);
		error = pclConnectAndCreate(&client,
			patchServer,
			CRYPTICLAUNCHER_PATCHSERVER_PORT,
			g_autoPatchTimeout,
			comm,
			"",
			autoPatchToken(gameID),
			autoupdate_path,
			autoPatchConnectCallback,
			autoupdate_path);
		if(error || !client || attempts >= 4)
		{
			MessageBox(NULL, _("The patch server could not be found"), _("Server error"), MB_OK|MB_ICONERROR);
			exit(attempts ? 2 : 1);
		}
		//pclSetKeepAliveAndTimeout(client, 30);
		pclSetRetryTimes(client, RETRY_TIMEOUT, RETRY_BACKOFF);
		autoPatchTimer = &client->progress_timer;
		error = pclWait(client);

	} while(error == PCL_LOST_CONNECTION);
	HANDLE_ERROR(error, true);

	//autopatch_window = SimpleWindowManager_FindWindow(CL_WINDOW_AUTOPATCH, 0);
	//if(autopatch_window)
	//{
	//	autopatch_window->bCloseRequested = true;
	//	PostMessage(autopatch_window->hWnd, WM_APP, 0, 0); // Send a message to trigger the dialog proc and therefore the cleanup.
	//}
	//else
	//{
	//	PostThreadMessage(autopatch_thread, WM_QUIT, 0, 0);
	//}
	autoPatchShutdown = true;
	autoPatchTimer = NULL;
	pclDisconnectAndDestroy(client);
	commDestroy(&comm);
	while(!autoPatchDidShutdown)
		WaitForSingleObject((HANDLE)autopatch_thread, 1000);
}

static S64 g_last_recieved = 0;
static bool displayDownloadProgress(PatchProcessStats *stats, SimpleWindow *userdata)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)userdata->pUserData;
	
	FOR_EACH_IN_EARRAY(stats->state_info, XferStateInfo, state)
		stats->received += state->blocks_so_far * state->block_size;
	FOR_EACH_END

	launcher->speed_received.cur = stats->received;
	launcher->speed_actual.cur = stats->actual_transferred;
	launcher->http_percent = stats->actual_transferred ? 100*stats->http_actual_transferred/stats->actual_transferred : 0;
	
	if(stats->received && stats->elapsed - launcher->elapsedLast > 0.1)
	{
		F32 rec_num, tot_num, act_num;
		char *rec_units, *tot_units, *act_units;
		U32 rec_prec, tot_prec, act_prec;
		double percent;
		U32 compression;
		F32 total_time;
		static char *buf = NULL;
		//static S64 last_recieved = 0;

		if(stats->received < g_last_recieved)
			stats->received = g_last_recieved;
		else
			g_last_recieved = stats->received;

		percent = stats->received * 100.0 / stats->total;
		compression = stats->received ? stats->actual_transferred * 100 / stats->received : 0;
		total_time = stats->received ? stats->elapsed * stats->total / stats->received : 0;

		SetWindowText(userdata->hWnd, STACK_SPRINTF("%.1f%% %s", percent, gdGetDisplayName(launcher->gameID)));

		humanBytes(stats->received, &rec_num, &rec_units, &rec_prec);
		humanBytes(stats->total, &tot_num, &tot_units, &tot_prec);
		humanBytes(stats->actual_transferred, &act_num, &act_units, &act_prec);

		//estrCreate(&buf);
		estrPrintf(&buf, "%.4f,%d:%.2d,%d:%.2d,%d,%d,%.*f%s,%.0f%s,%.*f%s",
			percent, (U32)stats->elapsed / 60, (U32)stats->elapsed % 60, (U32)total_time / 60, (U32)total_time % 60,
			stats->received_files, stats->total_files, rec_prec, rec_num, rec_units, tot_num, tot_units, act_prec, act_num, act_units);

		SetProgress(userdata, buf);
		//InvokeScript(userdata, "do_set_progress", SCRIPT_ARG_STRING, buf, SCRIPT_ARG_NULL);
		//estrDestroy(&buf);
		//printf("%3d%%  Time: %d:%.2d/%d:%.2d  Files: %d/%d  Data: %4d%s/%d%s  Compression: %3d%%    \r",
		//	percent, (U32)elapsed / 60, (U32)elapsed % 60, (U32)total_time / 60, (U32)total_time % 60,
		//	recieved_files, total_files, rec_num, rec_units, tot_num, tot_units, compression);
		launcher->elapsedLast = stats->elapsed;
	}
	return false;
}

static bool mirrorCallback(	PCL_Client* client,
								  SimpleWindow* window,
								  F32 elapsed,
								  const char* curFileName,
								  ProgressMeter* progress)
{
	static U32 lastTime = 0;

	if(	timeGetTime() - lastTime > 250 ||
		progress->files_so_far == progress->files_total)
	{
		char title[500];

		lastTime = timeGetTime();

		sprintf(title,
			"Mirroring %d: %s",
			progress->files_so_far,
			curFileName);

		SetProgress(window, "100,,,0,0,,,");
		DisplayMsg(window, title);
	}

	return true;
}

static bool verifyCallback(	PCL_Client* client,
	SimpleWindow *window,
	F32 elapsed,
	const char* curFileName,
	ProgressMeter* progress)
{
	static U32 lastTime = 0;
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)window->pUserData;
	launcher->isVerifying = true;

	if(	timeGetTime() - lastTime > 250 ||
		progress->files_so_far == progress->files_total)
	{
		char title[500];
		const char *trimname;
		int percent = 100;
		
		if(progress->files_total)
			percent = (100 * progress->files_so_far) / progress->files_total;

		lastTime = timeGetTime();

		trimname = strrchr(curFileName, '/');
		if(!trimname)
			trimname = curFileName;
		else
			trimname++;

		sprintf(title,
			"Verifying %d/%d: %s",
			progress->files_so_far,
			progress->files_total,
			trimname);

		DisplayMsg(window, title);
		postCommandInt(launcher->queueFromPCL, CLCMD_SET_PROGRESS_BAR, percent);
	}

	return launcher->askVerify;
}

typedef struct VerifyCompleteUserdata {
	SimpleWindow *window;
	char productName[1024];
} VerifyCompleteUserdata;

static bool verifyCompleteCallback(PCL_Client* client,
	VerifyCompleteUserdata *userdata,
	F32 elapsed,
	const char* curFileName,
	ProgressMeter* progress)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)userdata->window->pUserData;
	if(launcher->askVerify)
		writeRegInt(userdata->productName, "LastVerifyComplete", timeSecondsSince2000());
	SAFE_FREE(userdata);
	return true;
}

void connectToServer(SimpleWindow *window, ShardInfo_Basic *shard)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)window->pUserData;
	PCL_ErrorCode error;
	int attempts = 0;
	char autoupdate_path[MAX_PATH] = {0};
	char *autoupdate_token = NULL;
	char prepatch_path[MAX_PATH];

	if(launcher->client)
	{
		if(!strcmpi(launcher->client->root_folder, launcher->config->root))
			return; // Early bailout
		else
		{
			// Connect for a new product or category
			pclDisconnectAndDestroy(launcher->client);
			launcher->client = NULL;
		}
	}

	do {
		if(launcher->client == NULL)
		{
			//printf("Connecting to %s:%i\n", CRYPTICLAUNCHER_PATCHSERVER, CRYPTICLAUNCHER_PATCHSERVER_PORT);
		}
		else
		{
			//printf("The client was disconnected during a connect attempt\n");
			pclDisconnectAndDestroy(launcher->client);
			launcher->client = NULL;
			attempts++;
		}

		// Should we autopatch?
		strcpy(autoupdate_path, getExecutableName());
		forwardSlashes(autoupdate_path);
		if( g_dontAutopatch || 
			strstri(autoupdate_path, "/Core/tools/bin/") ||
			strstri(autoupdate_path, "/FightClub/tools/bin/") ||
			strstri(autoupdate_path, "/StarTrek/tools/bin/") ||
			strstri(autoupdate_path, "/Night/tools/bin/") ||
			strstri(autoupdate_path, "/Creatures/tools/bin/") ||
			strstri(autoupdate_path, "/Bronze/tools/bin/") ||
			strstri(autoupdate_path, "/src/"))
		{
			autoupdate_path[0] = '\0';
		}

		if (!isDefaultCrypticLauncherAutoPatchServer())
		{
			autoupdate_path[0] = '\0';
		}
			
		autoupdate_token = autoPatchToken(launcher->gameID);
		makeDirectories(launcher->config->root);
		if(attempts > 0)
			ShellExecute( NULL, "open", "ipconfig.exe", "/flushdns", "", SW_HIDE );
		printf("Connecting with root %s\n", launcher->config->root);
		error = pclConnectAndCreate(&launcher->client,
			launcher->config->server,
			launcher->config->port,
			CRYPTICLAUNCHER_PATCHSERVER_TIMEOUT,
			launcher->pclComm,
			launcher->config->root,
			autoupdate_token,
			*autoupdate_path ? autoupdate_path : NULL,
			autoPatchConnectCallback,
			autoupdate_path);
		if(error || !launcher->client)
		{
			MessageBox(NULL, "Error connecting to patch server", "Error", MB_OK|MB_ICONERROR);
			systemTrayRemove(launcher->window->hWnd);
			exit(attempts ? 2 : 1);
		}

		// Boost the amount the PCL can request at once to 500k
		pclSetMaxNetBytes(launcher->client, 1024 * 500);

		pclSetRetryTimes(launcher->client, RETRY_TIMEOUT, RETRY_BACKOFF);
		error = pclWait(launcher->client);

	} while(error == PCL_LOST_CONNECTION);
	HANDLE_ERROR(error, false);

	if(g_launcher_debug)
		pclVerboseLogging(launcher->client, 1);
	//pclVerifyAllFiles(launcher->client, g_config.verifyAllFiles);

	// Setup the progress callback
	error = pclSetProcessCallback(launcher->client, displayDownloadProgress, window);
	HANDLE_ERROR(error, false);
	error = pclSetMirrorCallback(launcher->client, mirrorCallback, window);
	HANDLE_ERROR(error, false);

	// Enable clean-hoggs mode
	pclAddFileFlags(launcher->client, PCL_CLEAN_HOGGS);

	// Setup the prepatch folder as an extra
	strcpy(prepatch_path, launcher->client->root_folder);
	strcat(prepatch_path, "/prepatch");
	pclAddExtraFolder(launcher->client, prepatch_path, g_prepatch?HOG_DEFAULT:HOG_READONLY);
	if(g_prepatch)
	{
		pclSetWriteFolder(launcher->client, prepatch_path);
		pclAddFileFlags(launcher->client, PCL_NO_DELETE|PCL_NO_MIRROR);
	}
	else if(!shard->pPrePatchCommandLine || !shard->pPrePatchCommandLine[0])
	{
		// No prepatch, erase all the old data.
		char **files = fileScanDirFolders(prepatch_path, FSF_FILES);
		if(g_launcher_debug)
		{
			char msg[1024];
			sprintf(msg, "About to delete all prepatch hoggs in %s", prepatch_path);
			MessageBox(NULL, msg, "Deleting prepatch", MB_OK|MB_ICONINFORMATION);
		}
		FOR_EACH_IN_EARRAY(files, char, file)
			if(strEndsWith(file, ".hogg"))
			{
				int ignored = remove(file);
			}
		FOR_EACH_END
		fileScanDirFreeNames(files);
	}

	// Load all previously launched games as extra folders
	FOR_EACH_IN_EARRAY(launcher->history, char, hist)
		char *game = strdup(hist), *category, *extra;
		ShardInfo_Basic shardtmp;
		category = strchr(game, ':');
		assert(category);
		*category = '\0';
		category = strrchr(hist, ':');
		assert(category);
		category += 1;
		shardtmp.pProductName = game;
		shardtmp.pShardCategoryName = category;
		extra = shardRootFolder(&shardtmp);
		pclAddExtraFolder(launcher->client, extra, HOG_NOCREATE|HOG_READONLY);
		estrDestroy(&extra);
		free(game);
	FOR_EACH_END
}

// Return true if this is a shard used for testing by customers
static bool isExternalTestCategory(const char *category)
{
	return !stricmp_safe(category, "Playtest")
		|| !stricmp_safe(category, "Beta");
}

extern int gCOR15050workaround;

bool startPatch(SimpleWindow *window, ShardInfo_Basic *shard)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)window->pUserData;
	int argc;
	char *argv[50];
	char *commandline, *project, *test_root;
	int i;
	
	// For testing launcher<->error tracker comms.
	if(g_crashme)
		assertmsg(0, "Testing dumps from launcher");

	printf("startPatch, current root %s new %s:%s\n", launcher->config?launcher->config->root:"(no config)", shard->pProductName, shard->pShardName);

	test_root = shardRootFolder(shard);
	if(launcher->state == CL_STATE_GETTINGFILES && launcher->config && stricmp(NULL_TO_EMPTY(launcher->config->root), test_root)==0)
	{
		printf("Rejecting\n");
		return true;
	}
	estrDestroy(&test_root);

	// Skip patching if connecting to localhost
	if(strcmpi(shard->pProductName, "Local") == 0)
	{
		DisplayMsg(window, _("Ready (local shard mode)"));
		SetWindowText(window->hWnd, gdGetDisplayName(launcher->gameID));
		//InvokeScript(window, "do_set_progress", SCRIPT_ARG_STRING, "100,,,0,0,,,", SCRIPT_ARG_NULL);
		SetProgress(window, "100,,,0,0,,,");
		setButtonState(window, CL_BUTTONSTATE_PLAY);
		launcher->state = CL_STATE_READY;
		writeRegStr(NULL, "LastShard", STACK_SPRINTF("%s:%s", shard->pProductName, shard->pShardName));
		return false;
	}

	SetWindowText(window->hWnd, gdGetDisplayName(launcher->gameID));
	//InvokeScript(window, "do_set_progress", SCRIPT_ARG_STRING, "0,,,0,0,,,", SCRIPT_ARG_NULL);
	SetProgress(window, "0,,,0,0,,,");
	launcher->patchTicks = 0;
	g_last_recieved = 0;
	DisplayMsg(window, _("Connecting ..."));

	if(launcher->config)
	{
		if(launcher->config->root)
			estrDestroy(&launcher->config->root);
		free(launcher->config);
	}
	launcher->config = callocStruct(PatchClientConfig);

	commandline = estrCreateFromStr(g_prepatch ? shard->pPrePatchCommandLine : shard->pPatchCommandLine);

	if(strcmpi(shard->pShardCategoryName, "Avatar") == 0)
		project = "%sClientAvatar";
	else if(isExternalTestCategory(shard->pShardCategoryName))
		project = "%sClientBeta";
	else
		project = "%sClient";
	estrReplaceOccurrences_CaseInsensitive(&commandline, STACK_SPRINTF("%sServer", shard->pProductName), STACK_SPRINTF(FORMAT_OK(project), shard->pProductName));
	argc = tokenize_line(commandline, argv, 0);
	for(i = 0; i < argc; ++i)
	{
		char *arg = argv[i];
		if(!stricmp(arg, "-project"))
		{
			assert(i+1 < argc);
			launcher->config->project = strdup(argv[i+1]);
		}
		else if(!stricmp(arg, "-name"))
		{
			assert(i+1 < argc);
			launcher->config->view_name = strdup(argv[i+1]);
		}
	}
	estrDestroy(&commandline);

	// Zero means 'latest'
	if(launcher->config->view_time == 0)
		launcher->config->view_time = INT_MAX;
	// branch == 0 will be adjusted to the latest branch on the server // TODO: time and branch should be handled the same
	if(launcher->config->sandbox == NULL)
		launcher->config->sandbox = "";

	launcher->config->root = shardRootFolder(shard);

	// note: it is intended that g_patchserver has no bearing on game patching patch server - only on (cryptic launcher) auto patch
	if (g_pwrd_mode)
	{
		launcher->config->server = PWRD_PATCHSERVER_HOST;
	}
	else
	{
		launcher->config->server = CRYPTICLAUNCHER_PATCHSERVER;
	}

	launcher->config->port = CRYPTICLAUNCHER_PATCHSERVER_PORT;

	// Error checking for -name HOLD setting
	if(stricmp(launcher->config->view_name, "HOLD")==0)
	{
		Errorf("-name HOLD got to patching, something is wrong. Command line is \"%s\"", g_prepatch ? shard->pPrePatchCommandLine : shard->pPatchCommandLine);
	}

	connectToServer(window, shard);

	// TODO: do more sanity checking
	if(!SAFE_DEREF(launcher->config->project))
	{
		launcher->state = CL_STATE_LAUNCHERPAGELOADED;
		setButtonState(window, CL_BUTTONSTATE_DISABLED);
		DisplayMsg(window, _("Cannot patch, no project given."));
		return true;
	}

	//machinePath(trivia_path, launcher->client->root_folder);
	//if(launcher->config->force_patch || triviaCheckPatchCompletion(trivia_path, launcher->config->view_name, launcher->config->branch, launcher->config->view_time, launcher->config->sandbox, false))//!launcher->config->patch_all))
	//{
	//	launcher->state = CL_STATE_READY;
	//	launcher->fastLaunch = NULL;
	//	setButtonState(window, CL_BUTTONSTATE_PLAY);
	//	DisplayMsg(window, "Patch already applied");
	//	return true;
	//}

	if(launcher->askVerify)
	{
		int ret;
		char msg[1024], title[1024];
		writeRegInt(shard->pProductName, "LastVerifyStart",  timeSecondsSince2000());
		UTF8ToACP(_("An error was detected by the game, would you like to verify all files? NOTE: This may take 10-20 minutes."), SAFESTR(msg));
		UTF8ToACP(_("Verify?"), SAFESTR(title));
		if(launcher->forceVerify)
			ret = IDYES;
		else
			ret = MessageBox(window->hWnd, msg, title, MB_YESNO|MB_ICONWARNING);
		if(ret == IDYES)
		{
			VerifyCompleteUserdata *userdata = calloc(1, sizeof(VerifyCompleteUserdata));
			userdata->window = window;
			strcpy(userdata->productName, shard->pProductName);
			pclVerifyAllFiles(launcher->client, 1);
			pclSetExamineCallback(launcher->client, verifyCallback, window);
			pclSetExamineCompleteCallback(launcher->client, verifyCompleteCallback, userdata);
		}
		else
		{
			writeRegInt(shard->pProductName, "LastVerifySkip",  timeSecondsSince2000());
		}
		// Clear the verify flag
		writeRegInt(shard->pProductName, "VerifyOnNextUpdate", 0);
	}

	launcher->state = CL_STATE_SETTINGVIEW;
	setButtonState(window, CL_BUTTONSTATE_DISABLED);
	pclSendLog(launcher->client, "patch started", "project %s view %s", launcher->config->project, launcher->config->view_name);

	// Apply filespec workaround for startrek shards specifically.
	// This will be removed once Star Trek shards are fixed.
	if (launcher->config->project && strstri(launcher->config->project, "startrek"))
		gCOR15050workaround = true;
	else
		gCOR15050workaround = false;

	if(SAFE_DEREF(launcher->config->view_name))
	{
		char *msg = NULL;
		estrStackCreate(&msg);
		estrPrintf(&msg, FORMAT_OK(_("Version %s")), launcher->config->view_name);
		DisplayMsg(window, msg);
		pclSetNamedView(launcher->client, launcher->config->project, launcher->config->view_name, true, true, NULL, NULL);
	}
	else
		pclSetViewByTime(launcher->client, launcher->config->project, launcher->config->branch, launcher->config->sandbox, launcher->config->view_time, true, true, NULL, NULL);

	return false;
}

void patchTick(SimpleWindow *window)
{
	static char *spinner[] = { "", ".", "..", "..." };
	PCL_ErrorCode err;
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)window->pUserData;

	launcher->patchTicks = (launcher->patchTicks + 1) % (4<<9);

	switch(launcher->state)
	{
	case CL_STATE_SETTINGVIEW:
		{
			err = pclProcessTracked(launcher->client);
			if(err == PCL_WAITING)
			{
				DisplayMsg(window, STACK_SPRINTF(FORMAT_OK(_("Version %s %s")), launcher->config->view_name, spinner[launcher->patchTicks>>9]));
				break;
			}
			else if(err == PCL_SUCCESS)
			{
				// Finished setting up our view
				// If we are in fast-launch mode, just start downloading.
				launcher->state = CL_STATE_WAITINGFORPATCH;
				if(launcher->fastLaunch)
				{
					doButtonAction(window, launcher->fastLaunch);
					launcher->fastLaunch = NULL;
				}
				else
					setButtonState(window, CL_BUTTONSTATE_PATCH);
			}
			else
			{
				// Some kind of error
				char errormsg[256];
				SetWindowText(window->hWnd, gdGetDisplayName(launcher->gameID));
				pclGetErrorString(err, SAFESTR(errormsg));
				DisplayMsg(window, STACK_SPRINTF(FORMAT_OK(_("Error %d: %s")), err, cgettext(errormsg)));
				setButtonState(window, CL_BUTTONSTATE_PATCH);
				launcher->state = CL_STATE_ERROR;
				// Make sure to clear the, now useless, patch client.
				pclDisconnectAndDestroy(launcher->client);
				launcher->client = NULL;
				launcher->restartTime = time(NULL) + RESTART_DELAY;
				launcher->restartError = err;
			}
		}
		break;
	case CL_STATE_GETTINGFILES:
		{
			err = pclProcessTracked(launcher->client);
			if(err == PCL_WAITING)
			{
				DisplayMsg(window, STACK_SPRINTF(FORMAT_OK(_("Patching %s")), spinner[launcher->patchTicks>>9]));
				break;
			}
			else if(err == PCL_SUCCESS)
			{
				U64 actual_transferred;

				// Got all the files, good to go.
				launcher->askVerify = false;
				launcher->forceVerify = false;
				launcher->isVerifying = false;
				pclSendLog(launcher->client, "patch completed", "project %s view %s", launcher->config->project, launcher->config->view_name);
				pclSendLogHttpStats(launcher->client);
				pclActualTransferred(launcher->client, &actual_transferred);
				printf("Total transfer: %"FORM_LL"u\n", actual_transferred);
				// Disconnect from the server so we don't hog resources.
				pclDisconnectAndDestroy(launcher->client);
				launcher->client = NULL;
				if(g_prepatch)
					// Prepatching, exit
					exit(0);
				SetWindowText(window->hWnd, gdGetDisplayName(launcher->gameID));
				postCommandString(launcher->queueFromPCL, CLCMD_DISPLAY_MESSAGE, _("Ready"));
				//InvokeScript(window, "do_set_progress", SCRIPT_ARG_STRING, "100,,,0,0,,,", SCRIPT_ARG_NULL);
				SetProgress(window, "100,,,0,0,,,");
				setButtonState(window, CL_BUTTONSTATE_PLAY);
				launcher->state = CL_STATE_READY;
				if(launcher->autoLaunch && !(GetKeyState(VK_SHIFT)&0x8000))
					postCommandPtr(launcher->queueFromPCL, CLCMD_PUSH_BUTTON, NULL);
			}
			else
			{
				// Some kind of error
				char errormsg[256];
				launcher->askVerify = false;
				launcher->forceVerify = false;
				launcher->isVerifying = false;
				SetWindowText(window->hWnd, gdGetDisplayName(launcher->gameID));
				pclGetErrorString(err, SAFESTR(errormsg));
				DisplayMsg(window, STACK_SPRINTF(FORMAT_OK(_("Error %d: %s.")), err, cgettext(errormsg)));
				setButtonState(window, CL_BUTTONSTATE_PATCH);
				launcher->state = CL_STATE_ERROR;
				// Make sure to clear the, now useless, patch client.
				pclDisconnectAndDestroy(launcher->client);
				launcher->client = NULL;
				launcher->restartTime = time(NULL) + RESTART_DELAY;
				launcher->restartError = err;
			}
		}
		break;
	case CL_STATE_ERROR:
		{
			time_t now = time(NULL);
			int timeleft;
			char errormsg[256], msg[1024];
			
			// No restart time setup, bail
			if(!launcher->restartTime)
				break;

			timeleft = launcher->restartTime - now;
			if(timeleft == launcher->restartLast)
				break;

			launcher->restartLast = timeleft;

			if(timeleft <= 0)
			{
				launcher->restartError = 0;
				launcher->restartTime = 0;
				postCommandPtr(launcher->queueFromPCL, CLCMD_PUSH_BUTTON, 0);
			}
			else
			{
				pclGetErrorString(launcher->restartError, SAFESTR(errormsg));
				sprintf(msg, FORMAT_OK(_("Error %d: %s. Restart in %u.")), launcher->restartError, cgettext(errormsg), timeleft);
				DisplayMsg(window, msg);
				printfColor(COLOR_RED|COLOR_BRIGHT, "%s\n", msg);
			}
		}
		break;
	}
}

void doButtonAction(SimpleWindow *window, ShardInfo_Basic *shard)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)window->pUserData;
	U32 size;

	switch(launcher->state)
	{
	case CL_STATE_ERROR:
		launcher->fastLaunch = shard;
		startPatch(window, shard);
		break;
	case CL_STATE_WAITINGFORPATCH:
		DisplayMsg(window, _("Patching"));
		if(!isExternalTestCategory(shard->pShardCategoryName))
			writeRegStr(NULL, "LastShard", STACK_SPRINTF("%s:%s", shard->pProductName, shard->pShardName));

		// Resized the reserved chunk to just over the size of the largest file.
		pclGetLargestFileSize(launcher->client, &size);
		memTrackReserveMemoryChunk(size + 10*1024*1024);

		setButtonState(window, CL_BUTTONSTATE_CANCEL);
		if(g_patchAll || launcher->disable_micropatching)
			pclGetAllFiles(launcher->client, NULL, NULL, NULL);
		else
			pclGetRequiredFiles(launcher->client, true, false, NULL, NULL, NULL);
		launcher->state = CL_STATE_GETTINGFILES;
		launcher->elapsedLast = 0;
		break;
	case CL_STATE_GETTINGFILES:
		// Cancel file transfer
		pclDisconnectAndDestroy(launcher->client);
		launcher->client = NULL;
		launcher->fastLaunch = NULL;
		launcher->state = CL_STATE_LAUNCHERPAGELOADED;
		startPatch(window, shard);
		break;
	case CL_STATE_READY:
		// Get a ticket for passing to the client
		postCommandPtr(launcher->queueFromPCL, CLCMD_START_LOGIN_FOR_GAME, shard);
		break;
	case CL_STATE_GETTINGGAMETICKET:
		break; // Already starting the game, just ignore it.
	default:
		//assertmsg(0, "The button should be disabled");
		break;
	}
}

unsigned int __stdcall thread_Patch(SimpleWindow *window)
{
	EXCEPTION_HANDLER_BEGIN
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)window->pUserData;
	HRESULT ret;
	CrypticLauncherCommand *cmd;
	bool run = true;

	while(run)
	{
		autoTimerThreadFrameBegin(__FUNCTION__);
		
		if(!launcher)
		{
			Sleep(100);
			autoTimerThreadFrameEnd();
			continue;
		}

		while((ret = XLFQueueRemove(launcher->queueToPCL, &cmd)) == S_OK)
		{
			switch(cmd->type)
			{
			xcase CLCMD_START_PATCH:
				printf("Patch: Got command START_PATCH %s:%s\n", ((ShardInfo_Basic*)cmd->ptr_value)->pProductName, ((ShardInfo_Basic*)cmd->ptr_value)->pShardName);
				startPatch(window, cmd->ptr_value);
			xcase CLCMD_RESTART_PATCH:
				printf("Patch: Got command RESTART_PATCH %s:%s\n", ((ShardInfo_Basic*)cmd->ptr_value)->pProductName, ((ShardInfo_Basic*)cmd->ptr_value)->pShardName);
				launcher->state = CL_STATE_LAUNCHERPAGELOADED;
				if(launcher->client)
				{
					pclDisconnectAndDestroy(launcher->client);
					launcher->client = NULL;
				}
				launcher->fastLaunch = cmd->ptr_value;
				startPatch(window, cmd->ptr_value);
			xcase CLCMD_DO_BUTTON_ACTION:
				printf("Patch: Got command DO_BUTTON_ACTION %s:%s\n", ((ShardInfo_Basic*)cmd->ptr_value)->pProductName, ((ShardInfo_Basic*)cmd->ptr_value)->pShardName);
				doButtonAction(window, cmd->ptr_value);
			xcase CLCMD_STOP_THREAD:
				printf("Patch: Got command STOP_THREAD\n");
				run = false;
			xcase CLCMD_FIX_STATE:
				printf("Patch: Got command FIX_STATE\n");
				if(launcher->client && eaSize(&launcher->client->state))
				{
					switch(launcher->client->state[0]->call_state)
					{
					xcase STATE_SET_VIEW:
						launcher->state = CL_STATE_SETTINGVIEW;
					xcase STATE_GET_FILE_LIST:
					case STATE_GET_REQUIRED_FILES:
						launcher->state = CL_STATE_GETTINGFILES;
					}
				}
			}
			free(cmd);
		}
		assert(ret == XLOCKFREE_STRUCTURE_EMPTY);
		commMonitor(launcher->pclComm);
		if(launcher->client || launcher->restartTime)
			patchTick(window);

		autoTimerThreadFrameEnd();
	}
	return 0;
	EXCEPTION_HANDLER_END
}
