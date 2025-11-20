/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#define JOB_GROUP_VARIABLES 1

#include "ClientBinner.h"
#include "ClientControllerLib.h"
#include "file.h"
#include "stringCache.h"
#include "sysUtil.h"
#include "gimmeDLLWrapper.h"
#include "memReport.h"
#include "dirMonitor.h"
#include "folderCache.h"
#include "winUtil.h"
#include "serverLib.h"
#include "error.h"
#include "objTransactions.h"
#include "autoStartupSupport.h"
#include "resourceManager.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "Alerts.h"
#include "jobManagerSupport.h"

#define PROJ_SPECIFIC_COMMANDS_ONLY 1
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.c"
#undef PROJ_SPECIFIC_COMMANDS_ONLY

char nameSpaceForClient[CRYPTIC_MAX_PATH] = "";
char *pStatusFromClient = NULL;

//if non-zero, fork all printfs from the client to a file for debugging
static int giForkPrintfsForClientBinnerClients = 1;
AUTO_CMD_INT(giForkPrintfsForClientBinnerClients, ForkPrintfsForClientBinnerClients) ACMD_AUTO_SETTING(Ugc, CLIENTBINNER);

bool gbClientMakeBinsSucceeded = false;
AUTO_CMD_INT(gbClientMakeBinsSucceeded, ClientMakeBinsSucceeded);

int giClientStartTimeout = 300;
AUTO_CMD_INT(giClientStartTimeout, ClientStartTimeout);

AUTO_CMD_STRING(nameSpaceForClient, nameSpaceForClient);

/* REDUNDANT use ClientController_AppendClientCommandLine in ClientControllerLib instead
char *pExtraClientCommandLine = NULL;

AUTO_COMMAND;
void ExtraClientCommandLine(ACMD_SENTENCE pString)
{
	estrCopy2(&pExtraClientCommandLine, pString);
}
*/

static char *GetClientForkPrintfFileName(void)
{
	static char *spRetVal = NULL;

	if (!spRetVal)
	{
		estrPrintf(&spRetVal, "c:\\temp\\shortterm\\1_week\\ClientBinnerPrintfs\\%s\\%u_%u.txt",
			GetShardNameFromShardInfoString(), GetAppGlobalID(), timeSecondsSince2000());
	}
	return spRetVal;
}

char *GetForkPrintfFileComment(void)
{
	static char *spRetVal = NULL;

	if (giForkPrintfsForClientBinnerClients)
	{
		estrPrintf(&spRetVal, "Client printfs should be in %s on %s", GetClientForkPrintfFileName(), getHostName());
	}
	else
	{
		estrPrintf(&spRetVal, "No client printfs available. AUTO_SETTING ForkPrintfsForClientBinnerClients enables this");
	}

	return spRetVal;
}

AUTO_RUN_SECOND;
void ClientBinner_InitStringCache(void)
{
	if(isProductionMode())
	{
		stringCacheSetInitialSize(128*1024);
	}
	else
	{
		stringCacheSetInitialSize(800*1024);
	}
}

static bool bDoNotDeleteNamespaceFiles = false;
AUTO_CMD_INT(bDoNotDeleteNamespaceFiles, DoNotDeleteNamespaceFiles);

static void ClientBinner_DeleteNamespaceFiles(void)
{
	// Delete namespace files
	char dir[MAX_PATH], out_dir[MAX_PATH];
	char cmd[MAX_PATH];
	int rv;

	if (!isProductionMode() || bDoNotDeleteNamespaceFiles)
		return;

	sprintf(dir, "ns/%s", nameSpaceForClient);
	fileLocateWrite(dir, out_dir);
	if (!dirExists(out_dir))
	{
		return;
	}

	backSlashes(out_dir);
	sprintf(cmd, "rd /s /q %s", out_dir);
	rv = system(cmd);
	if (rv != 0)
	{
		AssertOrAlert("NAMESPACE_FILE_DELETE_FAILED", "Failed to remove namespace directory: %s", out_dir);
	}
}

int main(int argc, char **argv)
{
	int maintimer = 0, frametimer = 0;
	F32 fClientStartTime = 0.0f;

	EXCEPTION_HANDLER_BEGIN;
	WAIT_FOR_DEBUGGER;

	gimmeDLLDisable(1);
	RegisterGenericGlobalTypes();
	parseGlobalTypeArgc(argc, argv, GLOBALTYPE_CLIENTBINNER);

	DO_AUTO_RUNS;

	dirMonSetBufferSize(NULL, 2*1024);
	ServerLibPatch();
	FolderCacheChooseMode();
	FolderCacheEnableCallbacks(0);
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'C', 0x808080);
	serverLibStartup(argc, argv);
	ClientController_InitLib();

	loadstart_printf("Connecting ClientBinner to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
	while (!InitObjectTransactionManager(GetAppGlobalType(), gServerLibState.containerID, gServerLibState.transactionServerHost, gServerLibState.transactionServerPort, gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}
	if (!objLocalManager())
	{
		loadend_printf("failed.");
		return 0;
	}
	loadend_printf("connected.");

	loadstart_printf("Running auto startup...");
	DoAutoStartup();
	loadend_printf("...done.");

	resFinishLoading();
	stringCacheFinalizeShared();

	maintimer = timerAlloc();
	timerStart(maintimer);
	frametimer = timerAlloc();
	timerStart(frametimer);

	assertmsgf(nameSpaceForClient[0], "No namespace specified for clientBinner");
	


	if(!ClientController_MonitorCrashBeganEvents(CLIENTBINNER_CRASHBEGAN_EVENT) || !ClientController_MonitorCrashCompletedEvents(CLIENTBINNER_CRASHCOMPLETED_EVENT))
	{
		ErrorOrAlert("CLIENTBINNER_EVENTMONITORING_FAILED", "Failed to establish Windows events to monitor Headshot Client. Might not get proper reporting of Headshot Client crashes.");
	}

	{
		char buffer[256];
		sprintf(buffer, "%s %d", GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID());
		setConsoleTitle(buffer);
	}

	while(1)
	{
		static bool bWasConnected = false;
		static bool bWasCrashed = false;
		static bool bDone = false;
		static bool sbAlreadyStartedOne = false;
		ClientControllerState eState;

		autoTimerThreadFrameBegin("main");
		utilitiesLibOncePerFrame(timerElapsedAndStart(frametimer), 1.0f);
		serverLibOncePerFrame();
		commMonitor(commDefault());

		if (!bDone)
		{
			eState = ClientController_MonitorState();

			switch(eState)
			{
			case CC_NOT_RUNNING:
				if (!sbAlreadyStartedOne)
				{
					char *pExtraCommandLine = NULL;

					estrPrintf(&pExtraCommandLine, "-SetTestingMode -ProductionEdit -notimeout -noaudio  -windowed -loadUserNameSpaces %s -makeBinsAndExitForNamespace %s -ReportMakebinsToTestClient",
						nameSpaceForClient, nameSpaceForClient);

					if (giForkPrintfsForClientBinnerClients)
					{
						estrConcatf(&pExtraCommandLine, " -ForkPrintfsToFile %s", GetClientForkPrintfFileName());
					}

					sbAlreadyStartedOne = true;
					ClientController_StartClient(true, true, pExtraCommandLine);
					fClientStartTime = timerElapsed(maintimer);
					RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "ClientStarting");
					estrDestroy(&pExtraCommandLine);
				}
				else
				{
					if (gbClientMakeBinsSucceeded)
					{
						char **files = NULL;
						bool res = false;
						char *pcError = NULL;
						eaPush(&files, strdupf("%s:/bin", nameSpaceForClient));
						res = ServerLibPatchUpload(files, "ClientBinner", &pcError, "(CC_NOT_RUNNING) Updated client bins for job %s", GetCurJobNameForJobManager());
						eaDestroyEx(&files, NULL);
						ClientBinner_DeleteNamespaceFiles();
						if (res)
							JobManagerUpdate_Complete(true, true, "Client reported Success");
						else
							JobManagerUpdate_Complete(false, true, "Client reported failure: Error uploading results to ugcmaster: %s. %s", pcError, GetForkPrintfFileComment());
						bDone = true;

					}
					else
					{
						if (pStatusFromClient)
						{
							JobManagerUpdate_Complete(false, true, "Client reported failure: %s. %s", pStatusFromClient, GetForkPrintfFileComment());
						}
						else
						{
							JobManagerUpdate_Complete(false, true, "Client closed unexpectedly. %s", GetForkPrintfFileComment());
						}
						bDone = true;
					}
				}
				xcase CC_RUNNING:
				if(bWasConnected)
				{
					// Client was connected but isn't anymore, it may have gone away in a very bad way
					// This would be superseded by the "crash began" event if the client had crashed, since CE wouldn't kill the exe's links and there's no link timeout
					ClientController_KillClient();
					if (gbClientMakeBinsSucceeded)
					{
						char **files = NULL;
						bool res = false;
						char *pcError = NULL;
						eaPush(&files, strdupf("%s:/bin", nameSpaceForClient));
						res = ServerLibPatchUpload(files, "ClientBinner", &pcError, "(CC_RUNNING)Updated client bins for job %s", GetCurJobNameForJobManager());
						eaDestroyEx(&files, NULL);
						ClientBinner_DeleteNamespaceFiles();
						if (res)
							JobManagerUpdate_Complete(true, true, "Client reported Success");
						else
							JobManagerUpdate_Complete(false, true, "Client reported failure: Error uploading results to ugcmaster: %s. %s", pcError, GetForkPrintfFileComment());
						bDone = true;

					}
					else
					{
						JobManagerUpdate_Complete(false, true, "Client reported failure: %s. %s",
							pStatusFromClient ? pStatusFromClient : "(unspecified)",
							GetForkPrintfFileComment());
						bDone = true;

					}

				}
				else if(timerElapsed(maintimer) - fClientStartTime > giClientStartTimeout)
				{
					ClientController_KillClient();
					JobManagerUpdate_Complete(false, true, "Client never launched. %s",
						GetForkPrintfFileComment());
					bDone = true;
				}
				xcase CC_CONNECTED:
				// Client is connected, everything is set, we don't need to do anything magical here
				if(!bWasConnected)
				{
					bWasConnected = true;
					printf("ClientBinner Client now connected.\n");
					RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "ClientIsReady");
					JobManagerUpdate_Status(0, "ClientBinner connected to client");
				}
				xcase CC_CRASHED:
				if(!bWasCrashed)
				{
					bWasCrashed = true;
					bWasConnected = false;
					RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "ClientCrashed");
					TriggerAlertf("CBINNER_CRASH", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, "Client binner client has crashed");
					JobManagerUpdate_Complete(false, false, "Client crashed. %s", GetForkPrintfFileComment());
					bDone = true;
				}
				xcase CC_CRASH_COMPLETE:
				bWasCrashed = false;
				ClientController_KillClient();
				exit(-1);
			}
		}
		autoTimerThreadFrameEnd();
	}

	EXCEPTION_HANDLER_END
	return 0;
}

