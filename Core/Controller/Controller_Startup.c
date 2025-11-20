#include "controller.h"
#include "sock.h"
#include "estring.h"
#include "timing.h"
#include "../../utilities/SentryServer/Sentry_comm.h"
#include "serverlib.h"
#include "autogen/sentryPub_h_ast.h"
#include "autogen/controller_h_ast.h"
#include "stringcache.h"
#include "GlobalTypes.h"
#include "fileUtil2.h"
#include "StructNet.h"
#include "autogen/controllerpub_h_ast.h"
#include "fileutil.h"
#include "crypt.h"
#include "Controller_Startup_c_ast.h"
#include "alerts.h"
#include "UtilitiesLib.h"
#include "Controller_MachineGroups.h"
#include "Controller_PCLStatusMonitoring.h"
#include "SentryServerComm.h"
#include "logging.h"
#include "StringUtil.h"
#include "AutoGen/ControllerStartupSupport_h_ast.h"
#include "Controller_Utils.h"
#include "Controller_ClusterController.h"


#define WAIT_FOR_SENTRY_SERVER_DELAY 10
#define LAUNCHER_CONNECTION_MAX_TIME (gPatchingCommandLine[0] ? 7200.0f : 15.0f)

#define MAX_LAUNCHERS_FOR_MIRRORING_SIMUL 2

//#define SENTRY_CONNECTION_MAX_TIME 150.0f
//#define LAUNCHER_CONNECTION_MAX_TIME 150.0f

#define DATADIRCLONING_SIZE_ALERT_CUTOFF (2 * 1024 * 1024)

//if true, reject connection from any machine without distinct public and private IPs (in case machines
//in live shards have bad public NICs)
bool gbMachinesMustHave2IPs = false;
AUTO_CMD_INT(gbMachinesMustHave2IPs, MachinesMustHave2IPs);


bool gbMirrorLocalExecutables = false;
AUTO_CMD_INT(gbMirrorLocalExecutables, MirrorLocalExecutables) ACMD_COMMANDLINE;

//if true, then delete all files before mirroring data dirs (passed to launcher)
bool gbDataMirroringDeletion = false;
AUTO_CMD_INT(gbDataMirroringDeletion, DataMirroringDeletion) ACMD_COMMANDLINE;

typedef enum
{
	WAITING_FOR_LAUNCHER,
	SUCCEEDED,
	FAILED,
} enumControllerMachineStartupState;

typedef struct
{
	//the "machine identifier" might be an IP string (ie, "1.2.3.4") or a machine name. It is guaranteed
	//to match either the machine name, local IP or public IP that the sentry server returns
	char machineIdentifier[256];
	char machineName[256];
	U32 iLocalIP;
	U32 iIP;
	enumControllerMachineStartupState eState;
	float fTimeStarted;
	bool bRequired;
	char whyRequired[512];
	bool bIsLocalHost;
	bool bSentryServerSaysItExists;
	bool bAlreadyAddedErrorMessage;
} ControllerMachineStartupState;

ControllerMachineStartupState **ppStartupMachineStates = NULL;

static char sShardSetupFileName[MAX_PATH] = "";
AUTO_CMD_STRING(sShardSetupFileName, ShardSetupFile);

void ControllerStartup_AddMachine(char *pMachineIdentifier, bool bMachineIsRequired, char *pWhyRequired)
{
	ControllerMachineStartupState *pState;
	int i;
	char *pIdentifierToUse = (strcmp(pMachineIdentifier, "localhost") == 0 ? makeIpStr(getHostLocalIp()) : pMachineIdentifier);

	for (i=0; i< eaSize(&ppStartupMachineStates); i++)
	{
		if (stricmp(ppStartupMachineStates[i]->machineIdentifier, pIdentifierToUse) == 0)
		{
			if (bMachineIsRequired)
			{
				ppStartupMachineStates[i]->bRequired = true;
				strcpy(ppStartupMachineStates[i]->whyRequired, pWhyRequired);
			}

			return;
		}
	}
	

	pState = calloc(sizeof(ControllerMachineStartupState), 1);

	if (strcmp(pMachineIdentifier, "localhost") == 0)
	{
		pState->bIsLocalHost = true;
	}

	strcpy(pState->machineIdentifier, pIdentifierToUse);
	
	pState->eState = WAITING_FOR_LAUNCHER;
	pState->fTimeStarted = timerElapsed(gControllerTimer);
	if (bMachineIsRequired)
	{
		pState->bRequired = true;
		strcpy(pState->whyRequired, pWhyRequired);
	}

	eaPush(&ppStartupMachineStates, pState);
}

void ControllerStartup_MachineExists(char *pMachineName, char *pLocalIP, char *pPublicIP)
{
	int i;
	bool bFoundItAlready = false;

	printf("Found machine named %s with local IP %s and public IP %s\n",
		pMachineName, pLocalIP, pPublicIP);

	for (i=0; i < eaSize(&ppStartupMachineStates); i++)
	{
		if (stricmp(ppStartupMachineStates[i]->machineIdentifier, pMachineName) == 0
			||strcmp(ppStartupMachineStates[i]->machineIdentifier, pLocalIP) == 0
			||strcmp(ppStartupMachineStates[i]->machineIdentifier, pPublicIP) == 0)
		{
			assertmsgf(!bFoundItAlready, "The machine named %s seems to be referred to in controller scripting with two different names, likely because it is in an IP range and also referred to specifically. This is not allowed", pMachineName);
			bFoundItAlready = true;
			strcpy(ppStartupMachineStates[i]->machineName, pMachineName);
			ppStartupMachineStates[i]->iLocalIP = ipFromString(pLocalIP);
			ppStartupMachineStates[i]->iIP = ipPublicFromString(pMachineName);
			ppStartupMachineStates[i]->bSentryServerSaysItExists = true;

			return;
		}
	}
}


int giLocalExecutableMirroringQueriesOutstanding = 0;

AUTO_STRUCT;
typedef struct ShortNameWithTypes
{
	const char *pShortName; AST(POOL_STRING)
	U32 *pGlobalTypes;
} ShortNameWithTypes;

AUTO_STRUCT;
typedef struct LocalExecutableWithCRCAndTypes
{
	LocalExecutableWithCRC exeWithCRC;
	U32 *pGlobalTypes;
} LocalExecutableWithCRCAndTypes;


void AddShortNameWithType(ShortNameWithTypes ***pppList, char *pName, GlobalType eType)
{
	int i;
	const char *pPoolName = allocAddString(pName);
	ShortNameWithTypes *pNameWithTypes;
	for (i = 0; i < eaSize(pppList); i++)
	{
		pNameWithTypes = (*pppList)[i];

		if (pNameWithTypes->pShortName == pPoolName)
		{
			ea32PushUnique(&pNameWithTypes->pGlobalTypes, eType);
			return;
		}
	}

	pNameWithTypes = StructCreate(parse_ShortNameWithTypes);
	pNameWithTypes->pShortName = pPoolName;
	ea32PushUnique(&pNameWithTypes->pGlobalTypes, eType);

	eaPush(pppList, pNameWithTypes);
}

void AddCRCsForType(LocalExecutableWithCRCList *pCRCList, LocalExecutableWithCRCAndTypes **ppExesWithCRCsAndTypes, GlobalType eType)
{
	int i;

	for (i=0; i < eaSize(&ppExesWithCRCsAndTypes); i++)
	{
		if (ea32Find(&ppExesWithCRCsAndTypes[i]->pGlobalTypes, eType) != -1)
		{
			eaPushUnique(&pCRCList->ppList, &ppExesWithCRCsAndTypes[i]->exeWithCRC);
		}
	}
}


//scan the local executable folders for shard executables, CRC them, and tell all launchers about them. Then wait for launchers
//to ask for ones that are different, then send those. This is used to easily launch multi-machine shards with locally modified
//executables. This does NOT work on controller.exe or launcher.exe
void DoLocalExecutableMirroring(void)
{

}

void HandleLauncherRequestsLocalExesForMirroring(NetLink *link, Packet *pak)
{
	char *pName;
	Packet *pOutPack;
	TrackedServerState *pLauncher;


	ContainerID iLauncherID = GetContainerIDFromPacket(pak);
	assert(giLocalExecutableMirroringQueriesOutstanding);
	giLocalExecutableMirroringQueriesOutstanding--;

	pLauncher = FindServerFromID(GLOBALTYPE_LAUNCHER, iLauncherID);

	if (!pLauncher)
	{
		printf("Got a request for local EXEs from a launcher that seems not to exist. It may have died during startup?\n");
		return;
	}
	else
	{
		printf("Launcher on %s has requested the following local files for mirroring:\n", 
			pLauncher->pMachine->machineName);
	}

	linkPushType(link);
	linkSetType(link, LINKTYPE_USES_FULL_SENDBUFFER);


	pOutPack = pktCreate(link, LAUNCHERQUERY_HEREAREREQUESTEDEXES);
	
	while (1)
	{
		void *pBuf;
		int iBufSize;
		static char *pPdbName = NULL;

		pName = pktGetStringTemp(pak);
		if (!pName[0])
		{
			break;
		}

		pktSendString(pOutPack, pName);

		printf("%s\n", pName);

		pBuf = fileAlloc(pName, &iBufSize);

		if (pBuf && iBufSize)
		{
			pktSendBits(pOutPack, 32, iBufSize);
			pktSendBytes(pOutPack, iBufSize, pBuf);
		}
		else
		{
			pktSendBits(pOutPack, 32, 0);
		}

		if (pBuf)
		{
			free(pBuf);
		}

		estrCopy2(&pPdbName, pName);
		estrReplaceOccurrences_CaseInsensitive(&pPdbName, ".exe", ".pdb");
		if (fileExists(pPdbName))
		{
			printf("Also sending %s\n", pPdbName);
			pktSendString(pOutPack, pPdbName);

			pBuf = fileAlloc(pPdbName, &iBufSize);

			if (pBuf && iBufSize)
			{
				pktSendBits(pOutPack, 32, iBufSize);
				pktSendBytes(pOutPack, iBufSize, pBuf);
			}
			else
			{
				pktSendBits(pOutPack, 32, 0);
			}

			if (pBuf)
			{
				free(pBuf);
			}
		}
	}

	pktSendString(pOutPack, "");
	pktSend(&pOutPack);


	giLocalExecutableMirroringQueriesOutstanding++;
}

void HandleLauncherGotLocalExesForMirroring(NetLink *link, Packet *pak)
{
	linkPopType(link);
	giLocalExecutableMirroringQueriesOutstanding--;
}

AUTO_STRUCT;
typedef struct DMReport
{
	char *pReport; AST(KEY)
	char **ppMachines;
} DMReport;

DMReport **ppReports = NULL;
	

int giNumMachinesSettingContents = 0;

void GenerateDataMirroringReport(void)
{
	char *pFullReport = NULL;
	int i, j;

	estrPrintf(&pFullReport, "%d unique data mirroring results:\n", eaSize(&ppReports));

	for (i=0; i < eaSize(&ppReports); i++)
	{
		estrConcatf(&pFullReport, "%s\nReported by: ", ppReports[i]->pReport);

		for (j=0; j < eaSize(&ppReports[i]->ppMachines); j++)
		{
			estrConcatf(&pFullReport, "%s%s", ppReports[i]->ppMachines[j], j == eaSize(&ppReports[i]->ppMachines) - 1 ? "\n" : ", ");
		}
	}

	log_printf(LOG_CONTROLLER, "Data Mirroring Report:\n%s", pFullReport);

	estrDestroy(&pFullReport);

}

void HandleDataMirroringReport(Packet *pak, NetLink *link)
{
	TrackedMachineState *pMachine = GetMachineFromNetLink(link);
	char *pReportString = pktGetStringTemp(pak);

	DMReport *pReport = eaIndexedGetUsingString(&ppReports, pReportString);

	giNumMachinesSettingContents--;

	if (pReport)
	{
		eaPush(&pReport->ppMachines, pMachine->machineName);
	}
	else
	{
		pReport = StructCreate(parse_DMReport);
		pReport->pReport = strdup(pReportString);
		eaIndexedEnable(&ppReports, parse_DMReport);
		eaPush(&pReport->ppMachines, pMachine->machineName);
		eaPush(&ppReports, pReport);
	}
}


	
bool ControllerStartup_ConnectionStuffWithSentryServer(char **ppErrorString)
{
	int i;
	int iSucceededCount;
	int iFailedCount;
	bool bRequiredMachinesFailed = false;
	Packet *pPak;
	char *query = NULL;
	ControllerMachineStartupState *pState;
	S64 iStartTime;
	bool bRequestSent = false;
	MachineInfoForShardSetupList shardSetupList = {0};
	char *pFullCommandLine = NULL, *pPatchingDir=NULL, *pLastSlash;

	int counter = 0;

	if (!gbUseSentryServer)
	{
		CreateLocalLauncher();
		return true;
	}

	Controller_SetStartupStatusString("main", "Beginning SentryServer comm");

	// Execute patchclient to update C:\Cryptic\tools
	estrCopy2(&pPatchingDir, gCoreExecutableDirectory);
	//can't use backslashes() on an EString
	estrReplaceOccurrences(&pPatchingDir, "/", "\\");

	pLastSlash = strrchr(pPatchingDir, '\\');

	if (pLastSlash)
	{
		estrSetSize(&pPatchingDir, pLastSlash - pPatchingDir);
	}
	estrPrintf(&pFullCommandLine, "%s\\patchclient -sync -project CrypticTools -root C:/Cryptic/tools -cleanup", pPatchingDir);
	loadstart_printf("Updating C:/Cryptic/tools (%s)", pFullCommandLine);
	system_detach(pFullCommandLine, 1, 0);
	estrDestroy(&pPatchingDir);
	loadend_printf(" done");

	ControllerStartup_AddMachine("localhost", true, "Localhost is always required");

	//find all machines listed in the command list and add them to our list of startup machines
	for (i=0; i < eaSize(&gCommandList.ppCommands); i++)
	{
		ControllerScriptingCommand *pCommand = gCommandList.ppCommands[i];

		if (pCommand->iFirstIP)
		{
			U32 j;

			for (j = pCommand->iFirstIP; j <= pCommand->iLastIP; j++)
			{
				ControllerStartup_AddMachine(makeIpStr(j), false, NULL);
			}
		}
		else if (strcmp(pCommand->pMachineName, "*") != 0)
		{
			ControllerStartup_AddMachine(pCommand->pMachineName, 
				pCommand->eCommand == CONTROLLERCOMMAND_LAUNCH_NORMALLY || 
				(pCommand->eCommand == CONTROLLERCOMMAND_SPECIFY_MACHINE && gServerTypeInfo[pCommand->eServerType].bIsUnique),
				"Machine specifically requested by controller script");
		}
	}

	if (sShardSetupFileName[0])
	{
		char *pFileToUse = NULL;

		estrStackCreate(&pFileToUse);
		if (fileIsAbsolutePath(sShardSetupFileName))
		{
			estrCopy2(&pFileToUse, sShardSetupFileName);
		}
		else
		{
			estrPrintf(&pFileToUse, "c:\\ShardSetupFiles\\%s.txt", sShardSetupFileName);
		}

		{
			char *pTempString = NULL;
			int iRet;
			ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
			iRet = ParserReadTextFile(pFileToUse, parse_MachineInfoForShardSetupList, &shardSetupList, 0);
			ErrorfPopCallback();

			if (!iRet)
			{
				estrPrintf(ppErrorString, "Encountered errors while reading %s: %s",
					pFileToUse, pTempString);
				estrDestroy(&pTempString);
				return false;
			}

			estrDestroy(&pTempString);
		}

		if (shardSetupList.pBaseGameServerGroupName)
		{
			ControllerMachineGroups_FixupShardSetupListWithBaseGameServerGroup(&shardSetupList);
		}

		FOR_EACH_IN_EARRAY_FORWARDS(shardSetupList.ppMachineGroups, GSMachineGroupDef, pGroup)
		{
			ControllerMachineGroups_AddMachineGroup(pGroup, "Read GSMachineGroup from %s", pFileToUse);
		}
		FOR_EACH_END

		estrDestroy(&pFileToUse);
	}

	for (i=0; i < eaSize(&shardSetupList.ppMachines); i++)
	{
		int j;

		bool bIsRequired = false;
		char whyRequired[1024] = "";

		for (j=0; j < eaSize(&shardSetupList.ppMachines[i]->ppSettings); j++)
		{
			if (shardSetupList.ppMachines[i]->ppSettings[j]->eSetting == CAN_LAUNCH_SPECIFIED)
			{
				gServerTypeInfo[shardSetupList.ppMachines[i]->ppSettings[j]->eServerType].bMachineSpecifiedInShardSetupFile = true;
			}

			if (gServerTypeInfo[shardSetupList.ppMachines[i]->ppSettings[j]->eServerType].bIsUnique)
			{
				sprintf(whyRequired, "Machine specified for launch of %s, which is a unique server type", GlobalTypeToName(shardSetupList.ppMachines[i]->ppSettings[j]->eServerType));
				bIsRequired = true;
			}
		}

		ControllerStartup_AddMachine(shardSetupList.ppMachines[i]->pMachineName, bIsRequired, whyRequired);
	}

	loadstart_printf("Waiting for machine list from sentry server");

	iStartTime = timeMsecsSince2000();

	while (1)
	{
		if (!bRequestSent)
		{
			bRequestSent = Controller_RequestAllMachineListFromSentryServer();
		}

		commMonitor(comm_controller);
		commMonitor(commDefault());	
		Controller_ClusterControllerTick();

		Controller_PCLStatusMonitoringTick();
		SentryServerComm_Tick();

		Sleep(1.0f);
		utilitiesLibOncePerFrame(REAL_TIME);

		if (eaSize(&gSentryClientList.ppClients))
		{
			break;
		}

		if ((timeMsecsSince2000() - iStartTime) / 1000 > WAIT_FOR_SENTRY_SERVER_DELAY)
		{
			estrPrintf(ppErrorString, "During controller startup, couldn't contact sentry server, or received empty machine list");
			return false;
		}
	}




	for (i=0; i < eaSize(&gSentryClientList.ppClients); i++)
	{
		char localIPString[32];
		char publicIPString[32];

		strcpy(localIPString, makeIpStr(gSentryClientList.ppClients[i]->local_ip));
		strcpy(publicIPString, makeIpStr(gSentryClientList.ppClients[i]->public_ip));
		ControllerStartup_MachineExists(gSentryClientList.ppClients[i]->name, localIPString, 
			publicIPString);

		ControllerMachineGroups_AddAvailableMachineName(gSentryClientList.ppClients[i]->name, gSentryClientList.ppClients[i]->public_ip, gSentryClientList.ppClients[i]->local_ip);
	}

	ControllerMachineGroups_AllAvailableMachineNamesAdded();

	StructDeInit(parse_SentryClientList, &gSentryClientList);

	Controller_BeginPCLStatusMonitoring();

	loadend_printf("succeeded\n");

	loadstart_printf("Preparing non-local machines for launching\n");
	Controller_SetStartupStatusString("main", "Beginning patching via SentryServer");



	for (i=0; i < eaSize(&ppStartupMachineStates); i++)
	{

		pState = ppStartupMachineStates[i];

		if (pState->bSentryServerSaysItExists)
		{

			if (gPatchingCommandLine[0] && !pState->bIsLocalHost)
			{
				GrabMachineForShardAndMaybePatch_Internal(pState->machineName, 0, gbKillAllOtherShardExesAtStartup, !gbDontPatchOtherMachines, true, CONTROLLER_PCL_STARTUP);
			}
		
		}
		else
		{
			Controller_MessageBoxError("SENTRY_SERVER_ERROR", "Sentry server does not know about machine %s specified in shard setup file... typo? (Controller will attempt to continue startup for now)", pState->machineIdentifier);
			pState->eState = FAILED;
		}
	}

	loadend_printf("done");

	loadstart_printf("Waiting for contact from launchers\n");

	for (i=0; i < eaSize(&ppStartupMachineStates); i++)
	{
		char executableName[256];
		bool bHide = false;
		bool bStartInDebugger = false;					

		pState = ppStartupMachineStates[i];

		if (pState->bSentryServerSaysItExists)
		{
			/*		loadupdate_printf("Killing previous launcher on host %s\n",makeIpStr(pState->iLocalIP));
			pPak = pktCreate(gpLinkToSentryServer, MONITORCLIENT_KILL);
			pktSendString(pPak, pState->machineName);
			pktSendString(pPak, "launcher.exe");
			pktSend(&pPak);*/

			if (gPatchingCommandLine[0] && !pState->bIsLocalHost)
			{
				GrabMachineForShardAndMaybePatch_Internal(pState->machineName, 1, gbKillAllOtherShardExesAtStartup, !gbDontPatchOtherMachines, true, CONTROLLER_PCL_STARTUP);
				if (!gbDontPatchOtherMachines)
				{
					Controller_AddPCLStatusMonitoringTask(pState->machineName, CONTROLLER_PCL_STARTUP);
				}
			}
			else
			{
				if (pState->bIsLocalHost)
				{
					bHide = gbHideLocalLauncher;
					bStartInDebugger = gbStartLocalLauncherInDebugger;
				}

				sprintf(executableName, "%s\\Launcher.exe", gCoreExecutableDirectory);


				estrPrintf(&pFullCommandLine, "WORKINGDIR(%s) %s -SetDirToLaunchFrom %s -ContainerID %u -ControllerHost %s -Cookie %u -SetErrorTracker %s %s %s -SetProductName %s %s %s",
					gCoreExecutableDirectory, 
					executableName,
					gExecutableDirectory,
					GetFreeContainerID(GLOBALTYPE_LAUNCHER),
					makeIpStr(GetHostIpToUse()),
					gServerLibState.antiZombificationCookie,
					getErrorTracker(), 
					bHide ? "-hide" : "", bStartInDebugger ? "-WaitForDebugger" : "", GetProductName(), GetShortProductName(),
					pLauncherCommandLine ? pLauncherCommandLine : "");

				PutExtraStuffInLauncherCommandLine(&pFullCommandLine, pState->iLocalIP);

				loadupdate_printf("Starting launcher on host %s using: %s\n",makeIpStr(pState->iLocalIP),pFullCommandLine);
				pPak = SentryServerComm_CreatePacket(MONITORCLIENT_LAUNCH, "Starting launcher on %s",
					makeIpStr(pState->iLocalIP));
				pktSendString(pPak, pState->machineName);
				pktSendString(pPak, pFullCommandLine);
				SentryServerComm_SendPacket(&pPak);
			}

			// Start the wait timer from when we try to execute the launcher
			pState->fTimeStarted = timerElapsed(gControllerTimer);
		}
		else
		{
			loadupdate_printf("Skipping host %s because it is not registered with the sentry server.\n",pState->machineIdentifier);
			pState->eState = FAILED;
		}
	}


	estrDestroy(&pFullCommandLine);
			
	counter = 0;

	do
	{
		TrackedMachineState *pMachine;
		iSucceededCount = 0;
		iFailedCount = 0;
		counter++;

		if (counter % 10000 == 0)
		{
			printf("%s: Current status:\n", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc()));
			for (i=0; i < eaSize(&ppStartupMachineStates); i++)
			{
				pState = ppStartupMachineStates[i];

				switch (pState->eState)
				{
				case WAITING_FOR_LAUNCHER:
					printf("STILL WAITING: %s (%s)\n", pState->machineName, Controller_GetPCLStatusMonitoringDescriptiveStatusString(pState->machineName, CONTROLLER_PCL_STARTUP));
					break;

				case FAILED:
					printf("MACHINE FAILED: %s (%s) (%s)\n", pState->machineName[0] ? pState->machineName : pState->machineIdentifier, makeIpStr(pState->iIP), Controller_GetPCLStatusMonitoringDescriptiveStatusString(pState->machineName, CONTROLLER_PCL_STARTUP));
					break;
				}
			}
		}


		Sleep(1);
		commMonitor(comm_controller);
		commMonitor(commDefault());
		Controller_ClusterControllerTick();

		Controller_PCLStatusMonitoringTick();
		SentryServerComm_Tick();

		utilitiesLibOncePerFrame(REAL_TIME);


		for (i=0; i < eaSize(&ppStartupMachineStates); i++)
		{
			pState = ppStartupMachineStates[i];

			switch (pState->eState)
			{
			case WAITING_FOR_LAUNCHER:
				pMachine = FindMachineByIP(pState->iIP, pState->iLocalIP);

				if (pMachine && pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
				{
					pState->eState = SUCCEEDED;
				}
				else if (timerElapsed(gControllerTimer) - pState->fTimeStarted > LAUNCHER_CONNECTION_MAX_TIME)
				{
					loadupdate_printf("Failed to start launcher for host %s\n",pState->machineName);
					pState->eState = FAILED;

				}
				else if (Controller_CheckTaskFailedByName(pState->machineName, CONTROLLER_PCL_STARTUP))
				{
					//already generated an alert whenever patching fails, so no random printf
					pState->eState = FAILED;
				}
				break;

			case SUCCEEDED:
				iSucceededCount++;
				break;

			case FAILED:
				iFailedCount++;
				if (pState->bRequired)
				{
					if (!pState->bAlreadyAddedErrorMessage)
					{
						pState->bAlreadyAddedErrorMessage = true;
						bRequiredMachinesFailed = true;

						estrConcatf(ppErrorString, "Couldn't start a launcher on machine %s(%s), presumably because the sentry server doesn't know about it, or sentry.exe is not running on it. This is fatal because: %s",
							pState->machineName, pState->machineIdentifier, pState->whyRequired);
				
					}
				}
				break;
			}
		}

		if (counter % 1000 == 0)
		{
			int iStillPatchingCount = eaSize(&ppStartupMachineStates) - iSucceededCount - iFailedCount;
			Controller_SetStartupStatusString("main", "%d machines patched, %d failed, %d still patching", 
				iSucceededCount, iFailedCount, iStillPatchingCount);
		}
	}
	while (iSucceededCount + iFailedCount < eaSize(&ppStartupMachineStates));

	loadend_printf("%d connected, %d failed\n", iSucceededCount, iFailedCount);

	eaDestroyEx(&ppStartupMachineStates, NULL);

	Controller_SetStartupStatusString("main", "Patching done, waiting for launchers");

	loadstart_printf("Waiting for all launchers to submit machine names and IPs");
	//wait for a while so that the most recent launcher to make contact can send its name and IPs
	while (FindMachineByName("UNKNOWN"))
	{
		Sleep(1);
		commMonitor(comm_controller);
		commMonitor(commDefault());
		Controller_ClusterControllerTick();

		Controller_PCLStatusMonitoringTick();
		SentryServerComm_Tick();

		utilitiesLibOncePerFrame(REAL_TIME);
	}
	loadend_printf("done");

	

	for (i=0; i < eaSize(&shardSetupList.ppMachines); i++)
	{
		int j;

		TrackedMachineState *pMachine = FindMachineByName(shardSetupList.ppMachines[i]->pMachineName);

		if (!pMachine)
		{
			printf("Couldn't find machine %s for shard setup stuff\n", shardSetupList.ppMachines[i]->pMachineName);
		}
		else
		{
			printf("About to apply shard setup file to machine %s\n", shardSetupList.ppMachines[i]->pMachineName);

			for (j=0; j < eaSize(&shardSetupList.ppMachines[i]->ppSettings); j++)
			{
				switch (shardSetupList.ppMachines[i]->ppSettings[j]->eSetting)
				{
				case CAN_LAUNCH_SPECIFIED:
					{
						char **ppTempList = NULL;

						printf("Setting LAUNCH_SPECIFIED for type %s\n", GlobalTypeToName(shardSetupList.ppMachines[i]->ppSettings[j]->eServerType));

						pMachine->canLaunchServerTypes[shardSetupList.ppMachines[i]->ppSettings[j]->eServerType].eCanLaunch = CAN_LAUNCH_SPECIFIED;
						pMachine->canLaunchServerTypes[shardSetupList.ppMachines[i]->ppSettings[j]->eServerType].iPriority = shardSetupList.ppMachines[i]->ppSettings[j]->iPriority;
						if (!pMachine->bIsLocalHost)
						{
							if (spLocalMachine->canLaunchServerTypes[shardSetupList.ppMachines[i]->ppSettings[j]->eServerType].eCanLaunch == CAN_LAUNCH_DEFAULT)
							{
								spLocalMachine->canLaunchServerTypes[shardSetupList.ppMachines[i]->ppSettings[j]->eServerType].eCanLaunch = CAN_NOT_LAUNCH;
							}
						}

						if (shardSetupList.ppMachines[i]->ppSettings[j]->pCategories && shardSetupList.ppMachines[i]->ppSettings[j]->pCategories[0])
						{
							int k;

							ExtractAlphaNumTokensFromString(shardSetupList.ppMachines[i]->ppSettings[j]->pCategories, &ppTempList);

							for (k=0; k < eaSize(&ppTempList); k++)
							{
								eaPushUnique(&pMachine->canLaunchServerTypes[shardSetupList.ppMachines[i]->ppSettings[j]->eServerType].ppCategories, allocAddString(ppTempList[k]));
							}

							eaDestroyEx(&ppTempList, NULL);
						}
					}

					break;

				default:
					break;
				}	
			}
		}
	}



	if (gbMirrorLocalExecutables)
	{
		DoLocalExecutableMirroring();
	}

	Controller_SetStartupStatusString("main", "Got contact from launchers, cloning local data dir");


	if (gbDoDataDirectoryCloning && isProductionMode())
	{
		TrackedServerState *pServer;

		if (!gpDataDirFileList)
		{
			gpDataDirFileList = TPFileList_ReadDirectory(fileDataDir());
		}


		pServer = gpServersByType[GLOBALTYPE_LAUNCHER];

		while (pServer)
		{
			if (!pServer->pMachine->bIsLocalHost)
			{
				pPak = pktCreate(pServer->pLink, LAUNCHERQUERY_SETDIRECTORYCONTENTS);
				pktSendString(pPak, fileDataDir());
				ParserSendStruct(parse_TPFileList, pPak, gpDataDirFileList);
				ONCE(
					if (pktGetSize(pPak) > DATADIRCLONING_SIZE_ALERT_CUTOFF)
					{
						CRITICAL_NETOPS_ALERT("DIRCLONING_SIZE", "While doing DataDirectoryCloning, the packet containing file contents is > %d bytes, this is alarming, might possibly cause link overflows when being sent to launchers",
							pktGetSize(pPak));
					}
					)
				pktSend (&pPak);
				giNumMachinesSettingContents++;

			}
			
			pServer = pServer->pNext;
		}
	


		loadstart_printf("Waiting for all launchers to complete setting directory contents");
		while (giNumMachinesSettingContents)
		{
			Sleep(1);
			commMonitor(comm_controller);
			commMonitor(commDefault());			
			Controller_ClusterControllerTick();

			Controller_PCLStatusMonitoringTick();
			SentryServerComm_Tick();

			utilitiesLibOncePerFrame(REAL_TIME);
		}
		loadend_printf("done");

		GenerateDataMirroringReport();
	}


	Controller_SetStartupStatusString("main", "Done with machine startup stuff");

	StructDeInit(parse_MachineInfoForShardSetupList, &shardSetupList);



	//for public shards, make sure the machine has distinct public/private IPs
	if (gbMachinesMustHave2IPs)
	{
		for (i=0; i < giNumMachines; i++)
		{
			TrackedMachineState *pMachine = &gTrackedMachines[i];

			if (pMachine->iPublicIP == pMachine->IP)
			{
				TriggerAlertf("NO_PUBLIC_IP", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, pMachine->machineName, 0, "Machine %s has identical public and private IPs... removing it from the shard",
					pMachine->machineName);
				pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->bKilledIntentionally = true;
				linkRemove(&pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink);
			}
		}
	}


	//at the end of startup, reset all the launcher keepalives because they may have gotten out of whack while doing
	//slow things like mirror files and stuff
	for (i = 0; i < giNumMachines; i++)
	{
		gTrackedMachines[i].iLastLauncherContactTime = 0;
	}


	if (bRequiredMachinesFailed)
	{
		

		return false;
	}

	MachineGroups_InitSystem();

	return true;
}

#include "Controller_Startup_c_ast.c"
