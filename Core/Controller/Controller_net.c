#include "controller.h"
#include "serverlib.h"
#include "sysutil.h"
#include "autogen/serverlib_autogen_remotefuncs.h"
#include "gimmedllwrapper.h"
#include "fileutil2.h"
#include "StructNet.h"
#include "httpxpathsupport.h"
#include "Autogen/svrGlobalInfo_h_ast.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "autogen/sentrypub_h_ast.h"
#include "url.h"
#include "Autogen/url_h_ast.h"
#include "httpJpegLibrary.h"
#include "StringUtil.h"
#include "TimedCallback.h"
#include "alerts.h"
#include "utilitieslib.h"
#include "logging.h"
#include "GenericFileServing.h"
#include "crypt.h"
#include "sock.h"
#include "ControllerPub_h_ast.h"
#include "Controller_Startup.h"
#include "Controller_AutoSettings.h"
#include "Controller_MachineGroups.h"
#include "sock.h"

#include "XMLRPC.h"
#include "HttpLib.h"
#include "ContinuousBuilderSupport.h"
#include "RemoteCommandGroup.h"
#include "Controller_Utils.h"
#include "Controller_DynHoggPruning.h"
#include "systemspecs.h"
#include "SystemSpecs_h_ast.h"
#include "Controller_ClusterController.h"
#include "HttpServing.h"

#define MAX_MONITORING_STR_LEN (50 * 1024 * 1024)

//we use the "slowness" of hte machine to factor into whether to trigger various (mostly GS-related) alerts... cap it at this value
#define MAX_LAUNCHER_LAG_FOR_ALERT_CALCULATION 60

//after a certain number of seconds, if the controller attempted to create a server of any type
//but sees no evidence that server exists, it will decide the server ain't never gonna exist,
//and will kill it and start a new one. (Relates to messages like "server foo not on launcher after n seconds")

//__CATEGORY Stuff that doesn't fit anywhere else
//(Seconds) ALERT & KILL - Application Server startup timer
float gfServerCreationGracePeriod = 120;
AUTO_CMD_FLOAT(gfServerCreationGracePeriod, ServerCreationGracePeriod) ACMD_CONTROLLER_AUTO_SETTING(Misc);



float gfLastReportedControllerFPS = 0.0f;
static bool sbDontKillZombies = false;
AUTO_CMD_INT(sbDontKillZombies, DontKillZombies);



//__CATEGORY Various things that might cause alerts
//(FPS) ALERT - Required Controller FPS
float gfControllerFpsAlertCutoff = 120;
AUTO_CMD_FLOAT(gfControllerFpsAlertCutoff, ControllerFpsAlertCutoff) ACMD_CONTROLLER_AUTO_SETTING(MiscAlerts);

//(Seconds) ALERT - Round-trip timer for Launcher and Controller
int gAlertableLauncherLag = 10;
AUTO_CMD_INT(gAlertableLauncherLag, AlertableLauncherLag) ACMD_CONTROLLER_AUTO_SETTING(MiscAlerts);

//(Seconds) Don't repeat the Alertablelauncherlag alert for this amount of time
int giLauncherLagAlertTime = (60 * 15);
AUTO_CMD_INT(giLauncherLagAlertTime, LauncherLagAlertTime)ACMD_CONTROLLER_AUTO_SETTING(MiscAlerts);


int ControllerClientConnect(NetLink* link,TrackedServerState **ppServer)
{
	return 1;
}


void RefuseConnection(GlobalType eType, ContainerID iID, NetLink *link, char *pErrorString, bool bFatalError)
{

	Packet *pak = pktCreate(link, FROM_CONTROLLER_CONNECTIONRESULT);
	pktSendBits(pak, 1, 0);
	pktSendBits(pak, 1, bFatalError);
	pktSendString(pak, pErrorString);
	pktSend(&pak);


	printf("Refusing connection from %s because: %s\n",
		GlobalTypeAndIDToString(eType, iID), pErrorString);

}

void AcceptConnection(NetLink *link)
{
	Packet *pak;
	
	pktCreateWithCachedTracker(pak, link, FROM_CONTROLLER_CONNECTIONRESULT);
	pktSendBits(pak, 1, 1);
	pktSend(&pak);
}

void SendAutoSettingCommands(TrackedServerState *pServerState)
{
	char **ppCommands = ControllerAutoSettings_GetCommandStringsForServerType(pServerState->eContainerType);

	if (eaSize(&ppCommands))
	{
		Packet *pak = pktCreate(pServerState->pLink, FROM_CONTROLLER_AUTO_SETTING_COMMANDS);
		int i;

		for (i=0; i < eaSize(&ppCommands); i++)
		{
			pktSendString(pak, ppCommands[i]);
		}

		pktSendString(pak, "");
		pktSend(&pak);
	}
}



int HandleServerConnectMessage(Packet *pak, NetLink *link, TrackedServerState **ppServerState)
{
	GlobalType eContainerType;
	U32 iContainerID;
	int iCookie;
	TrackedServerState *pServerState;
	char *pProductName;
	U32 pid;
	char *pVersionString = "__UNSENT__";
	bool bAllowVersionMismatchJustForMe = false;

	TrackedMachineState *pMachine;

	if (*ppServerState)
	{
		//already have a tracked process on this link
		RefuseConnection(0, 0, link, "NetLink already has a server", true);

		assert(0);

		return 1;
	}

	eContainerType = pktGetBitsPack(pak, 1);
	iContainerID = pktGetBitsPack(pak, 1);
	iCookie = pktGetBitsPack(pak, 1);
	pProductName = pktGetStringTemp(pak);
	pid = pktGetBits(pak, 32);

	if (eContainerType == GLOBALTYPE_LAUNCHER)
	{
		printf("Got contact from LAUNCHER %u\n", iContainerID);
	}


	linkSetDebugName(link, STACK_SPRINTF("Controller link to %s", GlobalTypeAndIDToString(eContainerType, iContainerID)));
	
	//we know we're going to send tons of stuff to MCP
	if (eContainerType == GLOBALTYPE_MASTERCONTROLPROGRAM)
	{
		linkSetType(link, LINKTYPE_SHARD_NONCRITICAL_10MEG);

		if (iContainerID == sUIDOfMCPThatStartedMe && pid == GetPIDOfMCPThatStartedMe())
		{
			bAllowVersionMismatchJustForMe = true;
		}
	}
	else
	{
		linkSetType(link, GlobalTypeIsCriticallyImportant(eContainerType) ? LINKTYPE_SHARD_CRITICAL_1MEG : LINKTYPE_SHARD_NONCRITICAL_1MEG);
	}

	if (!pktEnd(pak))
	{
		pVersionString = pktGetStringTemp(pak);
	}

	if (isProductionMode()&& strcmp(pVersionString, GetUsefulVersionString()) != 0)
	{
		char *pAlertString = NULL;

		if (AllowShardVersionMismatch() || bAllowVersionMismatchJustForMe)
		{
			if (!VersionMismatchAlreadyReported(pVersionString, eContainerType))
			{
				VersionMismatchReported(pVersionString, eContainerType);
				estrPrintf(&pAlertString, "Server %s(%u)(IP %s) trying to connect to controller with version \"%s\", which doesn't match controller version \"%s\", ALLOWING. This is expected behavior if this server type was frankenbuilt.", 
					GlobalTypeToName(eContainerType), iContainerID, makeIpStr(linkGetIp(link)), pVersionString, GetUsefulVersionString());
				TriggerAlert(ALERTKEY_VERSIONMISMATCH, pAlertString, ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, eContainerType, iContainerID,
					eContainerType, iContainerID, getHostName(), 0);
				estrDestroy(&pAlertString);
			}
		}
		else
		{
			estrPrintf(&pAlertString, "Server %s(%u)(IP %s) trying to connect to controller with version \"%s\", which doesn't match controller version \"%s\", REJECTING", 
				GlobalTypeToName(eContainerType), iContainerID, makeIpStr(linkGetIp(link)), pVersionString, GetUsefulVersionString());
			TriggerAlert(ALERTKEY_VERSIONMISMATCH_REJECT, pAlertString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, eContainerType, iContainerID,
				eContainerType, iContainerID, getHostName(), 0);
			estrDestroy(&pAlertString);

			RefuseConnection(eContainerType, iContainerID, link, STACK_SPRINTF("Nonmatching versions %s and %s", pVersionString, GetUsefulVersionString()), true);
			return 1;
		}
	}

	if (strcmp(pProductName, GetProductName()) != 0)
	{
		RefuseConnection(eContainerType, iContainerID, link, STACK_SPRINTF("Nonmatching product names %s and %s", pProductName, GetProductName()), true);
		return 1;
	}



	//check if this is a server type we can handle
	if (eContainerType >= GLOBALTYPE_MAXTYPES || eContainerType <= 0 || gServerTypeInfo[eContainerType].executableName32_original[0] == 0)
	{
		char errorString[1024];
		sprintf(errorString, "Unhandled server type %d (%s)", eContainerType, GlobalTypeToName(eContainerType));
		RefuseConnection(eContainerType, iContainerID, link, errorString, true);
		return 1;
	}


	if ((U32)iCookie != gServerLibState.antiZombificationCookie)
	{
		if (!gServerTypeInfo[eContainerType].bIgnoreCookies)
		{
			RefuseConnection(eContainerType, iContainerID, link, "Nonmatching antizombification cookie", true);
			return 1;
		}
	}


	if ((pServerState = FindServerFromID(eContainerType, iContainerID)))
	{
		if (pServerState->pLink)
		{
			//someone is already connected with this container type and ID
			RefuseConnection(eContainerType, iContainerID, link, "non-unique container type/ID", true);
		}
		else
		{
			pServerState->pLink = link;
			AcceptConnection(link);
			*ppServerState = pServerState;
			SendAutoSettingCommands(pServerState);


		}

		return 1;
	}



	//check if this is a unique server type that there's already one of
	if (gServerTypeInfo[eContainerType].bIsUnique && gpServersByType[eContainerType] && gpServersByType[eContainerType]->iContainerID != iContainerID)
	{
		RefuseConnection(eContainerType, iContainerID, link, STACK_SPRINTF("duplication of unique server type %s", GlobalTypeToName(eContainerType)), true);
		return 1;
	}

	pMachine = GetMachineFromNetLink(link);	
	if (!pMachine)
	{
		RefuseConnection(eContainerType, iContainerID, link, "Too many machines connected", true);
		return 1;
	}

	if (gServerTypeInfo[eContainerType].bIsUniquePerMachine && pMachine->pServersByType[eContainerType] && pMachine->pServersByType[eContainerType]->iContainerID != iContainerID)
	{
		RefuseConnection(eContainerType, iContainerID, link, "Server of that type already exists on that machine", true);
		return 1;
	}

	//special case... a second MCP on localHost is almost certainly due to someone trying to launch two shards at once on the same PC,
	//version mismatch here might crash the controller
	if (pMachine->bIsLocalHost && eContainerType == GLOBALTYPE_MASTERCONTROLPROGRAM && gpServersByType[GLOBALTYPE_MASTERCONTROLPROGRAM]
		&& strcmp(pVersionString, GetUsefulVersionString()) != 0)
	{
		RefuseConnection(eContainerType, iContainerID, link, STACK_SPRINTF("Nonmatching versions %s and %s", pVersionString, GetUsefulVersionString()), true);
		return 1;
	}


	pServerState = (TrackedServerState*)calloc(sizeof(TrackedServerState), 1);

	pServerState->eContainerType = eContainerType;
	pServerState->iContainerID = iContainerID;

	pServerState->pMachine = pMachine;
	pServerState->pLink = link;
	*ppServerState = pServerState;

	LinkAndInitServer(pServerState, pMachine, "Unknown", NULL);

	AcceptConnection(link);

	if (pServerState->pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
	{
		TellLauncherAboutExistingServer(pServerState->pMachine, eContainerType, iContainerID, pid, pServerState->iLowLevelIndex);
	}
	

	if (eContainerType == GLOBALTYPE_LAUNCHER)
	{
		Packet *pOutPack;
		char *pLogServerName;
		
		printf("Connection complete\n");

		if ((pLogServerName = Controller_GetMachineNameOfActiveLogServer()))
		{
			pOutPack = pktCreate(link, LAUNCHERQUERY_SETLOGSERVER);
			pktSendString(pOutPack, pLogServerName);
			pktSend(&pOutPack);
		}

		//this might not work for launchers created at startup time, because the script hasn't been loaded yet.
		pOutPack = pktCreate(link, LAUNCHERQUERY_HIDEPROCESS);
		PutContainerIDIntoPacket(pOutPack, pServerState->iContainerID);
		PutContainerTypeIntoPacket(pOutPack, pServerState->eContainerType);
		pktSendBits(pOutPack, 1, ShouldWindowActuallyBeHidden(VIS_UNSPEC, GLOBALTYPE_LAUNCHER));

		pktSend(&pOutPack);


		//if this is a reconnection from a machine that was previously connected, need to do some magic reconnection juju...
		if (pServerState->pMachine->iTotalServers > 1)
		{
			GlobalType eType;

			//for all servers on this machine, tell the launcher about it, and also reset its heard-about-it-from-launcher timeout
			for (eType=0; eType < GLOBALTYPE_MAXTYPES; eType++)
			{
				if (eType != GLOBALTYPE_LAUNCHER)
				{
					TrackedServerState *pOtherServer = pServerState->pMachine->pServersByType[eType];

					while (pOtherServer && pOtherServer->pMachine == pServerState->pMachine)
					{
						TellLauncherAboutExistingServer(pServerState->pMachine, pOtherServer->eContainerType, pOtherServer->iContainerID,
							pOtherServer->PID, pOtherServer->iLowLevelIndex);
						pOtherServer->fLauncherReconnectTime = timerElapsed(gControllerTimer);

						pOtherServer = pOtherServer->pNext;
					}
				}
			}
		}
		
		if (pServerState->pMachine->bIsLocalHost)
		{
			TellLauncherAboutExistingServer(pServerState->pMachine, GLOBALTYPE_CONTROLLER, GetAppGlobalID(), getpid(), 0);
		}
	}

	SendAutoSettingCommands(pServerState);

	if (eContainerType == GLOBALTYPE_MASTERCONTROLPROGRAM)
	{
		SendAllSingleNotes(pServerState);
	}

	return 1;
}

void CalculatePlayerCountAndGSFPS(TrackedMachineState *pMachine)
{
	float fFPSSum = 0;
	TrackedServerState *pServer = pMachine->pServersByType[GLOBALTYPE_GAMESERVER];
	pMachine->performance.iNumPlayers = pMachine->performance.iWeighted_GS_FPS = 0;

	while (pServer && pServer->pMachine == pMachine)
	{
		if (pServer->pGameServerSpecificInfo)
		{
			pMachine->performance.iNumPlayers += pServer->pGameServerSpecificInfo->iNumPlayers;
			fFPSSum += pServer->perfInfo.fFPS * pServer->pGameServerSpecificInfo->iNumPlayers;
		}

		pServer = pServer->pNext;
	}

	if (pMachine->performance.iNumPlayers)
	{
		pMachine->performance.iWeighted_GS_FPS = fFPSSum / pMachine->performance.iNumPlayers;
	}
}
	
	


void HandleProcessListUpdate(Packet *pak, NetLink *pLink)
{
	TrackedMachineState *pMachine = GetMachineFromNetLink(pLink);
	int i;
	Packet *pOutPack;
	
	

	TrackedServerState *pServer;


	LauncherGlobalInfo globalInfo = {0};

	if (!pMachine || !pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
	{
		AssertOrAlert("UNKOWN_PROCESS_LIST", "Got a process list from an unknown/nonexistent launcher");
		return;
	}

	pMachine->iLastLauncherContactTime = timeSecondsSince2000_ForceRecalc();

	ParserRecv(parse_LauncherGlobalInfo, pak, &globalInfo, 0);

	pMachine->iPIDOfIgnoredServer = globalInfo.iPIDOfIgnoredServer;

	if (globalInfo.iLastTimeReceivedFromController)
	{
		pMachine->iLastLauncherLag_Actual = pMachine->iLastLauncherContactTime - globalInfo.iLastTimeReceivedFromController;
		pMachine->iLastLauncherLag_ToUse = MIN(pMachine->iLastLauncherLag_Actual, MAX_LAUNCHER_LAG_FOR_ALERT_CALCULATION);
	}

	pktCreateWithCachedTracker(pOutPack, pLink, LAUNCHERQUERY_CONTROLLERTIME);
	pktSendBits(pOutPack, 32, pMachine->iLastLauncherContactTime);
	pktSend(&pOutPack);


	//convert "last contact" times back to secsSince20000
	for (i=0; i < eaSize(&globalInfo.ppProcesses); i++)
	{
		if (globalInfo.ppProcesses[i]->perfInfo.iLastContactTime)
		{
			globalInfo.ppProcesses[i]->perfInfo.iLastContactTime = pMachine->iLastLauncherContactTime - globalInfo.ppProcesses[i]->perfInfo.iLastContactTime;
		}
	}



	PERFINFO_AUTO_START("SetWaitingBit", 1);

	//first go through all processes we know about on that machine and set the Waiting bit to true
	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (i != GLOBALTYPE_LAUNCHER)
		{
			pServer = pMachine->pServersByType[i];

			while (pServer && pServer->pMachine == pMachine)
			{
				pServer->bWaitingForUpdate = true;
				pServer = pServer->pNext;
			}
		}
	}
		
	PERFINFO_AUTO_STOP();


	//now get all processes from launcher... for ones we recognize, set waiting bit to false. For ones we don't
	//recognize, tell the launcher to kill them

	PERFINFO_AUTO_START("CheckExistingServers", 1);


	for (i=0; i < eaSize(&globalInfo.ppProcesses); i++)
	{
		LauncherProcessInfo *pCurProcess = globalInfo.ppProcesses[i];		

		//put reported controller FPSs in a special place
		if (pCurProcess->eType == GLOBALTYPE_CONTROLLER)
		{
			gfLastReportedControllerFPS = pCurProcess->perfInfo.fFPS;
			if (gfLastReportedControllerFPS < gfControllerFpsAlertCutoff)
			{
				if (!ControllerScripting_IsRunning())
				{
					TriggerAlertf("SLOW_CONTROLLER", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0, "The controller's fps is %f, lower than alerting threshhold of %f",
						gfLastReportedControllerFPS, gfControllerFpsAlertCutoff);
				}
			}
		}

		if ( pCurProcess->eType < 0 || pCurProcess->eType >= GLOBALTYPE_MAXTYPES)
		{
			ErrorOrAlert("CORRUPT_LAUNCHER_PROCLIST", "Launcher has told us about process %d with type %d, which is not legal",
				pCurProcess->PID, pCurProcess->eType);
			continue;
		}

		pServer = ServerFromTypeAndIDAndIndex(pCurProcess->eType, pCurProcess->ID, pCurProcess->iLowLevelControllerIndex);

		if (!pServer)
		{
			if (!ServerRecentlyDied(pCurProcess->eType, pCurProcess->ID))
			{
				if (pCurProcess->eType != GLOBALTYPE_CONTROLLER && !sbDontKillZombies && !g_isContinuousBuilder)
				{
					ErrorOrAlert("KILLING_ZOMBIE", "Controller was told by launcher on %s about %s with pid %d which is actively running but which the controller doesn't know about. This is presumably a zombie process. Killing it. If this is happening a lot and having bad side effects, tell Alex and add -DontKillZombies to your controller command line (or execute it on your controller through servermonitoring).",
						pMachine->machineName, GlobalTypeAndIDToString(pCurProcess->eType, pCurProcess->ID), pCurProcess->PID);
					KillServerByIDs(pMachine, pCurProcess->eType, pCurProcess->ID, "Unknown process reported by launcher... zombie?");
					SetServerJustDied(pCurProcess->eType, pCurProcess->ID);
				}
			}
		}
		else if (pServer->pMachine != pMachine)
		{
			KillServer(pServer, "Server on wrong machine somehow");
		}
		else
		{
			pServer->bWaitingForUpdate = false;


			pServer->PID = pCurProcess->PID;

			StructCopyAll(parse_ProcessPerformanceInfo, &pCurProcess->perfInfo, &pServer->perfInfo);

//			CheckProcessPerformanceForAlerts(pServer);
			

			if (pCurProcess->stateString[0])
			{
				InformControllerOfServerState_Internal(pServer, pCurProcess->stateString);
			}	
		}
	}

	PERFINFO_AUTO_STOP();


	PERFINFO_AUTO_START("CheckForNonexistingServers", 1);


	//now go through the known servers again and see if we can find any that don't seem to exist on the launcher
	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (!gServerTypeInfo[i].bLaunchersDontKnowAboutMe)
		{
			pServer = pMachine->pServersByType[i];

			while (pServer && pServer->pMachine == pMachine)
			{
				TrackedServerState *pNext = pServer->pNext;

				if (pServer->bWaitingForUpdate && !pServer->bWaitingToLaunch)
				{
					float fCreationTimeToUse = pServer->fLauncherReconnectTime ? pServer->fLauncherReconnectTime : pServer->fCreationTimeFloat;

					if (timerElapsed(gControllerTimer) - fCreationTimeToUse - pMachine->iLastLauncherLag_ToUse  > gfServerCreationGracePeriod)
					{
						StopTrackingServer(pServer, STACK_SPRINTF("Server type %s not on launcher after %f seconds",
							GlobalTypeToName(pServer->eContainerType), gfServerCreationGracePeriod), true, false);
					}
				}
				pServer = pNext;
			}
		}
	}

	PERFINFO_AUTO_STOP();

	ParserRecv(parse_TrackedPerformance, pak, &pMachine->performance, 0);

	CalculatePlayerCountAndGSFPS(pMachine);

//	CheckMachinePerformanceForAlerts(pMachine);

	StructDeInit(parse_LauncherGlobalInfo, &globalInfo);
	

	//now alert if the lag to this launcher is particularly bad
	if (!ControllerScripting_IsRunning())
	{
		if (pMachine->iLastLauncherLag_Actual > gAlertableLauncherLag * 10)
		{
			if (pMachine->iLastTimeAlertedCriticalLauncherLag < pMachine->iLastLauncherContactTime - giLauncherLagAlertTime)
			{
				pMachine->iLastTimeAlertedCriticalLauncherLag = pMachine->iLastLauncherContactTime;
				TriggerAlertf("CRITICAL_LAUNCHER_LAG", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_LAUNCHER, pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID, GLOBALTYPE_LAUNCHER, pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID,
					pMachine->machineName, 0, 
					"Launcher on machine %s has round trip communication of %d seconds, WAY THE HECK more than allowable max of %d. This WILL lead to cascading failures. INVESTIGATE AND FIX THIS! Controller fps: %f. Launcher fps: %f",
					pMachine->machineName, pMachine->iLastLauncherLag_Actual, gAlertableLauncherLag, gfLastReportedControllerFPS, pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->perfInfo.fFPS);
			}
		}
		else if (pMachine->iLastLauncherLag_Actual > gAlertableLauncherLag)
		{
			if (pMachine->iLastTimeAlertedLauncherLag < pMachine->iLastLauncherContactTime - giLauncherLagAlertTime)
			{
				pMachine->iLastTimeAlertedLauncherLag = pMachine->iLastLauncherContactTime;
				TriggerAlertf("LAUNCHER_LAG", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_LAUNCHER, pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID, GLOBALTYPE_LAUNCHER, pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID,
					pMachine->machineName, 0, 
					"Launcher on machine %s has round trip communication of %d seconds, more than allowable max of %d. This may lead to cascading failures. Controller fps: %f. Launcher fps: %f",
					pMachine->machineName, pMachine->iLastLauncherLag_Actual, gAlertableLauncherLag, gfLastReportedControllerFPS, pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->perfInfo.fFPS);
			}
		}
	}

	if (pMachine->bVerboseProcListLogging && !pktEnd(pak))
	{
		U32 iID = pktGetBits(pak, 32);

		objLog(LOG_LAUNCHER, GLOBALTYPE_LAUNCHER, pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->iContainerID, 0, 0, NULL, NULL, "ProcListLog", NULL, "ID %d cpu60 %d FreeMegs %d LauncherFPS %f  ControllerFPS %f",
			iID, pMachine->performance.cpuLast60, 
			(int)(pMachine->performance.iFreeRAM / 1024 / 1024),
			pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->perfInfo.fFPS, 
			gfLastReportedControllerFPS);
	}
		
}



int HandleServerDisconnectMessage(Packet *pak, NetLink *link,TrackedServerState **ppServer)
{
	if (*ppServer)
	{
		//printf("A server disconnected\n");
		StopTrackingServer(*ppServer, "Disconnected", false, false);
	}

	return 1;
}

void HandleProcessDied(Packet *pak, NetLink *link)
{
	TrackedMachineState *pMachine = GetMachineFromNetLink(link);
	TrackedServerState *pServer;
	GlobalType eContainerType = GetContainerTypeFromPacket(pak);
	U32 iContainerID = GetContainerIDFromPacket(pak);

	pServer = FindServerFromID(eContainerType, iContainerID);

	//printf("Got ProcessDied message type %s id %d\n", 
	//	GlobalTypeToName(eContainerType), iContainerID);

	if (pServer)
	{
		if (pServer->pMachine == pMachine)
		{
			StopTrackingServer(pServer, "Launcher reported process died", pServer->bKilledIntentionally ? false : true, false);
		}
		else
		{
			AssertOrAlert("ZOMBIE_DIED", "The launcher on machine %s just told us that %s[%u] died. But it's on %s. So it was probably a zombie process",
				pMachine->machineName, GlobalTypeToName(eContainerType), iContainerID, pServer->pMachine->machineName);
		}

	}
}

typedef struct DelayedKillStruct 
{
	GlobalType eContainerType;
	ContainerID iContainerID;
} DelayedKillStruct;

void KillAServer_DelayedCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	DelayedKillStruct *pKill = (DelayedKillStruct*)userData;
	TrackedServerState *pServer = FindServerFromID(pKill->eContainerType, pKill->iContainerID);
	free(pKill);

	if (pServer)
	{
		StopTrackingServer(pServer, "Delayed kill", false, false);
	}
}

void KillServer_Delayed(GlobalType eContainerType, ContainerID iID, float fDelay)
{
	DelayedKillStruct *pDelayedKill = calloc(sizeof(DelayedKillStruct), 1);
	pDelayedKill->eContainerType = eContainerType;
	pDelayedKill->iContainerID = iID;

	TimedCallback_Run(KillAServer_DelayedCB, pDelayedKill, fDelay);

}



void HandleProcessCrashed(Packet *pak, NetLink *link, bool bCrypticErrorIsFinished)
{
	TrackedMachineState *pMachine = GetMachineFromNetLink(link);
	TrackedServerState *pServer;
	GlobalType eContainerType = GetContainerTypeFromPacket(pak);
	U32 iContainerID = GetContainerIDFromPacket(pak);
	int iProcessID = pktGetBits(pak, 32);
	int iErrorID = pktGetBits(pak, 32);

	if (gbLeaveCrashesUpForever)
	{
		ControllerScripting_Cancel();
		return;
	}

	pServer = FindServerFromID(eContainerType, iContainerID);

	printf("Got ProcessCrashed message type %s id %d, crypticError %s finished\n", 
		GlobalTypeToName(eContainerType), iContainerID, bCrypticErrorIsFinished ? "IS" : "IS NOT");

	if (pServer)
	{
		assert (eContainerType < GLOBALTYPE_MAXTYPES && eContainerType >= 0);

		if (gServerTypeInfo[eContainerType].bLeaveCrashesUpForever)
		{
			ControllerScripting_Cancel();
			return;
		}

		assert(pServer->pMachine == pMachine);


		if (!pServer->bHasCrashed)
		{
			int i;
			pServer->iErrorID = iErrorID;
			

			if (ControllerScripting_IsRunning())
			{
				ControllerScripting_ServerCrashed(pServer);
			}
		
			pServer->bHasCrashed = true;
			pServer->pMachine->iCrashedServers++;

			TriggerAlert(GetCrashedOrAssertedKey(pServer->eContainerType),
					STACK_SPRINTF("%s(%d) has crashed/asserted on %s", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID, pServer->pMachine->machineName),
					ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, pServer->eContainerType, pServer->iContainerID,
					pServer->eContainerType, pServer->iContainerID, pServer->pMachine->machineName, iErrorID);


			for (i=0; i < eaSize(&pServer->ppThingsToDoOnCrash); i++)
			{
				ExecuteAndFreeRemoteCommandGroup(pServer->ppThingsToDoOnCrash[i], NULL, NULL);
			}
			eaDestroy(&pServer->ppThingsToDoOnCrash);


			InformOtherServersOfServerDeath(pServer->eContainerType, pServer->iContainerID, "Got process crashed/asserted message from launcher", true);
		}

/*		TriggerAlert(STACK_SPRINTF("%s(%d) (machine %s) has crashed/assert", GlobalTypeToName(pServer->eContainerType), 
			pServer->iContainerID, pServer->pMachine->machineName), pServer->eContainerType, 
			pServer->iContainerID, pServer->pMachine, ALERT_TRIGGERED, 0, true, iErrorID, NAMEDALERT_SERVERCRASHEDORASSERTED);
*/
	

	}

	assert(eContainerType >= 0 && eContainerType < GLOBALTYPE_MAXTYPES);

	//in production shard mode, kill any restart-on-crash server
	if (!bCrypticErrorIsFinished)
	{
		if (gbProductionShardMode && !gServerTypeInfo[eContainerType].bWaitForCrypticErrorBeforeRecreating && (gServerTypeInfo[eContainerType].bReCreateOnCrash || gServerTypeInfo[eContainerType].bReCreateOnSameMachineOnCrash))
		{
			KillServer_Delayed(eContainerType, iContainerID, DELAY_BEFORE_KILLING_SERVERS_THAT_WILL_BE_RESTARTED);
		}
	}
	else
	{
		if (gbProductionShardMode && gServerTypeInfo[eContainerType].bWaitForCrypticErrorBeforeRecreating && (gServerTypeInfo[eContainerType].bReCreateOnCrash || gServerTypeInfo[eContainerType].bReCreateOnSameMachineOnCrash))
		{
			KillServer_Delayed(eContainerType, iContainerID, 1.0f);
		}
	}


	if (gServerTypeInfo[eContainerType].bCanNotCrash && gbProductionShardMode)
	{
		char errorString[1024];
		sprintf_s(SAFESTR(errorString), "Server %d of type %s crashed... we can not recover, dying\n", iContainerID, GlobalTypeToName(eContainerType));

		printf("%s", errorString);
		log_printf(LOG_CRASH, "%s", errorString);
		svrExit(1);
	}


}

void HandleOverrideExeNameAndDir(Packet *pak, NetLink *link)
{
	GlobalType eType = GetContainerTypeFromPacket(pak);
	char *pExeName = pktGetStringTemp(pak);
	char *pLaunchDir = pktGetStringTemp(pak);

	estrCopy2(&gServerTypeInfo[eType].pOverrideExeName, pExeName);
	estrCopy2(&gServerTypeInfo[eType].pOverrideLaunchDir, pLaunchDir);

	estrTrimLeadingAndTrailingWhitespace(&gServerTypeInfo[eType].pOverrideExeName);
	estrTrimLeadingAndTrailingWhitespace(&gServerTypeInfo[eType].pOverrideLaunchDir);
}



void HandleHideRequest(Packet *pak, NetLink *link)
{
	GlobalType eType;
	TrackedMachineState *pMachine;
	bool bHide;

	pMachine = GetMachineFromNetLink(link);	
	eType = GetContainerTypeFromPacket(pak);

	bHide = pktGetBits(pak, 1);

	if (eType == GLOBALTYPE_CONTROLLER)
	{
		if (bHide)
		{
			hideConsoleWindow();
		}
		else
		{
			showConsoleWindow();
		}

		return;
	}

	gServerTypeInfo[eType].eVisibility_FromMCP = bHide ? VIS_HIDDEN : VIS_VISIBLE;;

	if (isProductionMode())
	{
		TrackedServerState *pServer = gpServersByType[eType];
		while (pServer)
		{
			if (pServer->bWaitingToLaunch)
			{
				pServer->eVisibility = bHide ? VIS_HIDDEN : VIS_VISIBLE;
			}
			else if (pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
			{
				Packet *newPak = pktCreate(pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_HIDEPROCESS);
				PutContainerIDIntoPacket(newPak, pServer->iContainerID);
				PutContainerTypeIntoPacket(newPak, eType);
				pktSendBits(newPak, 1, bHide);

				pktSend(&newPak);
			}

			pServer = pServer->pNext;
		}

	}
	else
	{
		if (pMachine->pServersByType[eType] && pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
		{
			Packet *newPak;

			if (pMachine->pServersByType[eType]->bWaitingToLaunch)
			{
				pMachine->pServersByType[eType]->eVisibility = bHide ? VIS_HIDDEN : VIS_VISIBLE;
			}
			else
			{

				newPak = pktCreate(pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_HIDEPROCESS);
				PutContainerIDIntoPacket(newPak, pMachine->pServersByType[eType]->iContainerID);
				PutContainerTypeIntoPacket(newPak, eType);
				pktSendBits(newPak, 1, bHide);

				pktSend(&newPak);
			}
		}
	}
}

void HandleLocalStatusRequest(Packet *pak, NetLink *link)
{
	GlobalType eServerType;
	Packet *pOutPacket;

	pktCreateWithCachedTracker(pOutPacket, link, FROM_CONTROLLER_HERE_IS_LOCAL_STATUS);

	while ((eServerType = GetContainerTypeFromPacket(pak)) != GLOBALTYPE_NONE)
	{
		PutContainerTypeIntoPacket(pOutPacket, eServerType);

		if (gpServersByType[eServerType])
		{
			pktSendBits(pOutPacket, 1, 1);

			pktSendBits(pOutPacket, 1, gpServersByType[eServerType]->bHasCrashed ? 1 : 0);
			pktSendBits(pOutPacket, 1, gpServersByType[eServerType]->bWaitingToLaunch ? 1 : 0);

			if (gpServersByType[eServerType]->bWaitingToLaunch)
			{
				char tempString[256];
				GlobalType eTypeBeingWaitedOn = GLOBALTYPE_NONE;
				if (ServerTypeIsReadyToLaunchOnMachine(eServerType, gpServersByType[eServerType]->pMachine, &eTypeBeingWaitedOn))
				{
					sprintf(tempString, "Ready to launch...");
				}
				else
				{
					sprintf(tempString, "Waiting for %s...", GlobalTypeToName(eTypeBeingWaitedOn));
				}
				pktSendString(pOutPacket, tempString);

			}
			else
			{
				pktSendString(pOutPacket, gpServersByType[eServerType]->stateString);
			}
		}
		else
		{
			pktSendBits(pOutPacket, 1, 0);
		}
	}

	PutContainerTypeIntoPacket(pOutPacket, GLOBALTYPE_NONE);

	pktSend(&pOutPacket);
}



void HandleMCPServerList(Packet *pak, NetLink *link)
{
	int i;

	Packet *pOutPack;
	
	pktCreateWithCachedTracker(pOutPack, link, FROM_CONTROLLER_HERE_IS_SERVER_LIST_FOR_MCP);

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		TrackedServerState *pServer = gpServersByType[i];

		while (pServer)
		{
			PutContainerTypeIntoPacket(pOutPack, pServer->eContainerType);
			PutContainerIDIntoPacket(pOutPack, pServer->iContainerID);
			pktSendBits(pOutPack, 32, GetIPToUse(pServer->pMachine));
			pktSendBits(pOutPack, 1, pServer->bHasCrashed);
			pktSendBits(pOutPack, 1, pServer->bWaitingToLaunch);
			pktSendString(pOutPack, pServer->stateString);

			pServer = pServer->pNext;
		}
	}

	PutContainerTypeIntoPacket(pOutPack, GLOBALTYPE_NONE);
	pktSend(&pOutPack);
}

void SendMonitoringCommandResultToMCP(ContainerID iMCPID, int iRequestID, int iClientID, char *pMessageString, void *pUserData)
{
	//special case... MCP ID of 0 means that this command actually came from the clusterController code
	if (iMCPID == 0)
	{
		Controller_ClusterControllerHandleCommandReturnFromServer(iRequestID, pMessageString);
	}
	else
	{

		TrackedServerState *pServer = FindServerFromID(GLOBALTYPE_MASTERCONTROLPROGRAM, iMCPID);

		if (pServer && pServer->pLink)
		{
			Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_MONITORING_COMMAND_RETURN);
			pktSendBits(pPak, 32, iRequestID);
			pktSendBits(pPak, 32, iClientID);
	
			if (pMessageString && strlen(pMessageString) > MAX_MONITORING_STR_LEN)
			{
				pktSendStringf(pPak, "ERROR: return string was more than %d bytes long", MAX_MONITORING_STR_LEN);
			}
			else
			{
				pktSendString(pPak, pMessageString);
			}

			giServerMonitoringBytes += pktGetSize(pPak);

			pktSend(&pPak);
		}
	}
}

void ReturnJpegToMCP(ContainerID iMCPID, int iRequestID, char *pData, int iDataSize, int iLifeSpan, char *pErrorMessage)
{
	TrackedServerState *pServer = FindServerFromID(GLOBALTYPE_MASTERCONTROLPROGRAM, iMCPID);

	if (pServer && pServer->pLink)
	{
		Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_HERE_IS_JPEG_FOR_MCP);
		pktSendBits(pPak, 32, iRequestID);
		pktSendBits(pPak, 32, iDataSize);

		if (iDataSize)
		{
			pktSendBytes(pPak, iDataSize, pData);
			pktSendBits(pPak, 32, iLifeSpan);
		}
		else
		{
			if (pErrorMessage && pErrorMessage[0])
			{
				pktSendString(pPak, pErrorMessage);
			}
			else
			{
				pktSendString(pPak, "Unknown JPEG error");
			}
		}

		giServerMonitoringBytes += pktGetSize(pPak);


		pktSend(&pPak);
	}
}



void HandleHereIsRequestedMonitoringJpeg(Packet *pak)
{
	int iRequestID;
	ContainerID iMCPID;
	int iDataSize;

	iRequestID = pktGetBits(pak, 32);
	iMCPID = GetContainerIDFromPacket(pak);
	iDataSize = pktGetBits(pak, 32);

	if (iDataSize)
	{
		char *pBuf = malloc(iDataSize);
		int iLifeSpan;

		pktGetBytes(pak, iDataSize, pBuf);
		iLifeSpan = pktGetBits(pak, 32);

		ReturnJpegToMCP(iMCPID, iRequestID, pBuf, iDataSize, iLifeSpan, NULL);

		free(pBuf);
	}
	else
	{
		ReturnJpegToMCP(iMCPID, iRequestID, NULL, 0, 0, pktGetStringTemp(pak));
	}

}


typedef struct ControllerJpegCache
{
	int iRequestID;
	ContainerID iMCPID;
} ControllerJpegCache;


void ReturnJpegToMCPCB(char *pData, int iDataSize, int iLifeSpan, char *pMessage, ControllerJpegCache *pUserData)
{
	ReturnJpegToMCP(pUserData->iMCPID, pUserData->iRequestID, pData, iDataSize, iLifeSpan, pMessage);
	free(pUserData);
}


void HandleMCPMonitoringJpeg(Packet *pak, NetLink *link)
{
	int iRequestID = pktGetBits(pak, 32);
	ContainerID iMCPID = GetContainerIDFromPacket(pak);
	char *pRequestName = pktGetStringTemp(pak);

	GlobalType eServerType;
	ContainerID iServerID;
	
	char *pFirstUnderscore, *pSecondUnderscore;
	UrlArgumentList argList = {0};


	if (!pktEnd(pak))
	{
		ParserRecv(parse_UrlArgumentList, pak, &argList, 0);
	}

	pFirstUnderscore = strchr(pRequestName, '_');
	if (!pFirstUnderscore)
	{
		ReturnJpegToMCP(iMCPID, iRequestID, NULL, 0, 0, STACK_SPRINTF("Bad syntax in JPEG name %s", pRequestName));
		StructDeInit(parse_UrlArgumentList, &argList);
		return;
	}

	*pFirstUnderscore = 0;

	eServerType = NameToGlobalType(pRequestName);
	if (eServerType == GLOBALTYPE_NONE)
	{
		ReturnJpegToMCP(iMCPID, iRequestID, NULL, 0, 0, STACK_SPRINTF("Unknown server type in JPEG name %s", pRequestName));
		StructDeInit(parse_UrlArgumentList, &argList);
		return;
	}

	pSecondUnderscore = strchr(pFirstUnderscore + 1, '_');
	if (!pSecondUnderscore)
	{
		ReturnJpegToMCP(iMCPID, iRequestID, NULL, 0, 0, STACK_SPRINTF("Bad syntax in JPEG name %s", pRequestName));
		StructDeInit(parse_UrlArgumentList, &argList);
		return;
	}

	if (!StringToInt(pFirstUnderscore + 1, &iServerID))
	{
		ReturnJpegToMCP(iMCPID, iRequestID, NULL, 0, 0, STACK_SPRINTF("Bad syntax in JPEG name %s", pRequestName));
		StructDeInit(parse_UrlArgumentList, &argList);
		return;
	}

	//at this point, we have extracted a container type and ID for the destination server

	if (eServerType == GLOBALTYPE_CONTROLLER)
	{
		ControllerJpegCache *pCache = malloc(sizeof(ControllerJpegCache));
		pCache->iMCPID = iMCPID;
		pCache->iRequestID = iRequestID;

		JpegLibrary_GetJpeg(pSecondUnderscore + 1, &argList, ReturnJpegToMCPCB, pCache);
		StructDeInit(parse_UrlArgumentList, &argList);
		return;
	}
	else
	{
		TrackedServerState *pServer;



		if (iServerID == 0 && gpServersByType[eServerType] && !gpServersByType[eServerType]->pNext)
		{
			pServer = gpServersByType[eServerType];
		}
		else
		{
			pServer = FindServerFromID(eServerType, iServerID);
		}

		if (!pServer)
		{
			ReturnJpegToMCP(iMCPID, iRequestID, NULL, 0, 0, STACK_SPRINTF("Can't find server %s(%u) which requested JPEG", 
				GlobalTypeToName(eServerType), iServerID));
			StructDeInit(parse_UrlArgumentList, &argList);
			return;
		}
		else if (!ServerReadyForMonitoring(pServer))
		{
			ReturnJpegToMCP(iMCPID, iRequestID, NULL, 0, 0, "server not yet ready");
			StructDeInit(parse_UrlArgumentList, &argList);
			return;
		}
		else if (pServer->pLink)
		{
			Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_REQUESTING_MONITORING_JPEG);
			pktSendBits(pPak, 32, iRequestID);
			PutContainerIDIntoPacket(pPak, iMCPID);
			pktSendString(pPak, pSecondUnderscore + 1);
			ParserSendStruct(parse_UrlArgumentList, pPak, &argList);
			pktSend(&pPak);
		}
		else if (pServer && objLocalManager())
		{
			RemoteCommand_GetJpegForServerMonitoring(eServerType, iServerID, iRequestID, &argList, iMCPID, pSecondUnderscore + 1);
		}
		else
		{
			ReturnJpegToMCP(iMCPID, iRequestID, NULL, 0, 0, STACK_SPRINTF("No way to request JPEG %s from server", pRequestName));
			StructDeInit(parse_UrlArgumentList, &argList);
			return;
		}
	}

	StructDeInit(parse_UrlArgumentList, &argList);
}



void HandleMCPMonitoringCommand(Packet *pak, NetLink *link)
{
	int iAccessLevel = 0;
	bool bNoReturn = false;

	GlobalType eType = GetContainerTypeFromPacket(pak);
	ContainerID iContainerID = GetContainerIDFromPacket(pak);

	int iClientID = pktGetBits(pak, 32);
	int iCommandRequestID = pktGetBits(pak, 32);
	ContainerID iMCPID = GetContainerIDFromPacket(pak);

	char *pCommandString = pktGetStringTemp(pak);
	char *pAuthNameAndIP = pktGetStringTemp(pak);
	
	if (!pktEnd(pak))
	{
		bNoReturn = pktGetBits(pak, 1);
	}

	if (!pktEnd(pak))
	{
		iAccessLevel = pktGetBits(pak, 32);
	}

	if (eType == GLOBALTYPE_CONTROLLER)
	{
		if (bNoReturn)
		{
			if (!cmdParseForServerMonitor(pCommandString, iAccessLevel, NULL, NULL, 0, 0, 0, 0, 0, pAuthNameAndIP))
			{
				SendMonitoringCommandResultToMCP(iMCPID, iCommandRequestID, iClientID, "Execution failed... talk to a programmer. Most likely the AST_COMMAND was set up wrong", NULL);			
			}
			else
			{
				SendMonitoringCommandResultToMCP(iMCPID, iCommandRequestID, iClientID, SERVERMON_CMD_RESULT_HIDDEN, NULL);
			}
		}
		else
		{
			char *pRetString = NULL;
			bool bSlowReturn = false;

			estrStackCreate(&pRetString);

#ifndef _XBOX
			if (strStartsWith(pCommandString, "<?xml"))
			{
				//This code is duplicated in ServerLib.c and RemoteXMLRPC.c
				//any updates must update all 3 places.

				char clientname[16];
				XMLParseInfo *info = XMLRPC_Parse(pCommandString,linkGetIpStr(link, clientname, 16));
				XMLMethodResponse *response = NULL;
				CmdSlowReturnForServerMonitorInfo slowReturnInfo = {0};
				estrCreate(&pRetString);
				if (info)
				{
					XMLMethodCall *method = NULL;
					if (method = XMLRPC_GetMethodCall(info))
					{
						slowReturnInfo.iClientID = iClientID;
						slowReturnInfo.iCommandRequestID = iCommandRequestID;
						slowReturnInfo.iMCPID = iMCPID;
						slowReturnInfo.pSlowReturnCB = SendMonitoringCommandResultToMCP;
						response = XMLRPC_ConvertAndExecuteCommand(method, iAccessLevel, 
							//This is probably the wrong link! This'll give bad categories.
							httpGetCommandCategories(link), 
							&slowReturnInfo);

						if (slowReturnInfo.bDoingSlowReturn)
						{	//We're going to pass the method call to the destination server so don't destroy it.
							info->state->methodCall = NULL;
						}
					}
					else
					{
						estrPrintf(&pRetString, "XMLRPC Request contained an error: %s", info->error);
					}
					StructDestroy(parse_XMLParseInfo, info);
				}
				else
				{
					estrPrintf(&pRetString, "Error generating XMLRPC request.");
				}
				if (!response)
				{
					response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_BADPARAMS, pRetString);
					estrClear(&pRetString);
				}
				if (!(bSlowReturn = slowReturnInfo.bDoingSlowReturn))
				{
					XMLRPC_WriteOutMethodResponse(response, &pRetString);
					SendMonitoringCommandResultToMCP(iMCPID, iCommandRequestID, iClientID, pRetString, NULL);
				}
				//else SendMonitoringCommandResultToMCP will be called by a finishing callback 
				StructDestroy(parse_XMLMethodResponse, response);			
			}
			else
#endif
			{
				cmdParseForServerMonitor(pCommandString, iAccessLevel, &pRetString, &bSlowReturn, iClientID, iCommandRequestID, iMCPID, SendMonitoringCommandResultToMCP, NULL, pAuthNameAndIP);
				if (!bSlowReturn)
				{
					SendMonitoringCommandResultToMCP(iMCPID, iCommandRequestID, iClientID, pRetString, NULL);
				}
			}

			estrDestroy(&pRetString);
		}
	}
	else
	{
		TrackedServerState *pServer = NULL;
		if (iContainerID)
		{
			pServer = FindServerFromID(eType, iContainerID);
		}
		else
		{
			pServer = gpServersByType[eType];
		}
		if (!pServer || !ServerReadyForMonitoring(pServer))
		{
#ifndef _XBOX
			if (strStartsWith(pCommandString, "<?xml"))
			{
				XMLMethodResponse *response = NULL;
				char *out = NULL;
				estrStackCreate(&out);

				if (!pServer)
				{
					estrPrintf(&out, "%s[%d] does not exist.", GlobalTypeToName(eType), iContainerID);
					response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_SERVERDOESNOTEXIST, out);
				}
				else
				{
					estrPrintf(&out, "%s[%d] is not ready for requests.", GlobalTypeToName(eType), iContainerID);
					response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_SERVERNOTREADY, out);
				}
				estrClear(&out);
				XMLRPC_WriteOutMethodResponse(response, &out);
				StructDestroy(parse_XMLMethodResponse, response);
				SendMonitoringCommandResultToMCP(iMCPID, iCommandRequestID, iClientID, out, NULL);
				estrDestroy(&out);
			}
			else
#endif
			{
				SendMonitoringCommandResultToMCP(iMCPID, iCommandRequestID, iClientID, "Server not yet ready", NULL);
			}
			return;
		}



		if (pServer && pServer->pLink)
		{
			Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_REQUESTING_MONITORING_COMMAND);
			pktSendBits(pPak, 32, iClientID);
			pktSendBits(pPak, 32, iCommandRequestID);
			PutContainerIDIntoPacket(pPak, iMCPID);
			pktSendString(pPak, pCommandString);
			pktSendString(pPak, pAuthNameAndIP);
			pktSendBits(pPak, 32, iAccessLevel);
			pktSendBits(pPak, 1, bNoReturn);
			pktSend(&pPak);
		}
		else if (objLocalManager())
		{
			RemoteCommand_CallLocalCommandRemotelyAndReturnVerboseHtmlString(eType, iContainerID, 
				iClientID, iCommandRequestID, iMCPID, pCommandString, iAccessLevel, bNoReturn, pAuthNameAndIP);
		}
	}
}

void SendCommandDirectlyToServerThroughNetLink(TrackedServerState *pServer, char *pCmdString)
{
	if (pServer->pLink)
	{
		Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_REQUESTING_MONITORING_COMMAND);
		pktSendBits(pPak, 32, 0);
		pktSendBits(pPak, 32, 0);
		PutContainerIDIntoPacket(pPak, 0);
		pktSendString(pPak, pCmdString);
		pktSendString(pPak, "");
		pktSendBits(pPak, 32, ACCESS_DEBUG);
		pktSendBits(pPak, 1, true);
		pktSend(&pPak);
	}
}

void HandleSystemSpecs(Packet *pak, NetLink *link)
{
	TrackedMachineState *pMachine = GetMachineFromNetLink(link);
	StructDestroySafe(parse_SystemSpecs, &pMachine->pSystemSpecs);
	pMachine->pSystemSpecs = StructCreate(parse_SystemSpecs);
	ParserRecv(parse_SystemSpecs, pak, pMachine->pSystemSpecs, 0);
}



void HandleLauncherIPs(Packet *pak, NetLink *link)
{
	TrackedMachineState *pMachine = GetMachineFromNetLink(link);
	U32 iPrivateIP, iPublicIP;

	iPrivateIP = pktGetBits(pak, 32);
	iPublicIP = pktGetBits(pak, 32);
	pktGetString(pak,pMachine->machineName,sizeof(pMachine->machineName));

	if (!(iPrivateIP == pMachine->IP || iPublicIP == pMachine->IP))
	{
		CRITICAL_NETOPS_ALERT("IP_CONFIG_ERROR", "Machine %s is reporting public and private IPs %s and %s. We previously thought it had %s. Unknown what affect this will have, but it's certainly puzzling",
			pMachine->machineName, makeIpStr(iPublicIP), makeIpStr(iPrivateIP), makeIpStr(pMachine->IP));
	}

	pMachine->IP = iPrivateIP;
	pMachine->iPublicIP = iPublicIP;
	
	//not sure if this is necessary for machines with only one IP
	if (pMachine->iPublicIP == 0)
	{
		pMachine->iPublicIP = pMachine->IP;
	}
	else if (pMachine->IP == 0)
	{
		pMachine->IP = pMachine->iPublicIP;
	}


	stashAddPointer(gMachinesByName, pMachine->machineName, pMachine, false);

	ControllerMachineGroups_GotContactFromMachine(pMachine->machineName, pMachine);

	if (!pktEnd(pak))
	{
		pMachine->bIsX64 = pktGetBits(pak, 1);
	}
}

void HandleSettingServerState(Packet *pak, NetLink *link)
{
	GlobalType eType = GetContainerTypeFromPacket(pak);
	U32 iContainerID = GetContainerIDFromPacket(pak);
	char *pStateString = pktGetStringTemp(pak);

	InformControllerOfServerState(eType, iContainerID, pStateString);
}
void HandleErrorDialogForMCP(Packet *pPak)
{
	char *pStr = pktMallocString(pPak);
	char *pTitle = pktMallocString(pPak);
	char *pFault = pktMallocString(pPak);
	U32 iHighLight = pktGetBitsPack(pPak, 32);

	SendErrorDialogToMCPThroughController(pStr, pTitle, pFault, iHighLight);

	free(pStr);
	free(pTitle);
	free(pFault);
}

/*
File: materials/test/Averytest_Tint.Material
Last Author/Status:Source Control not available
Parser error in materials/test/Averytest_Tint.Material, line 6: Unrecognized token SpecificValufffffe
Unique Error ID: 6398
*/

void HandleLauncherRelayingErrors(Packet *pPak)
{
	U32 iContainerID = GetContainerIDFromPacket(pPak);
	GlobalType eType = GetContainerTypeFromPacket(pPak);
	char *pStr = pktMallocString(pPak);
	char *pTitle = pktMallocString(pPak);
	char *pFault = pktMallocString(pPak);
	U32 iHighLight = pktGetBitsPack(pPak, 32);

	bool bAlreadySent = false;

	if (iContainerID == SPECIAL_CONTAINERID_XBOX_CLIENT)
	{
		if (strstri(pStr, "Source Control not available"))
		{
			if (strStartsWith(pStr, "File: "))
			{
				char *pNewLine;
				char fileName[CRYPTIC_MAX_PATH];
				char fixedErrorString[4096];

				strcpy_trunc(fileName, pStr + 6);

				pNewLine = strchr(fileName, '\n');

				if (pNewLine)
				{
					const char *pUserName;

					*pNewLine = 0;

					pUserName = gimmeDLLQueryLastAuthor(fileName);

					strcpy_trunc(fixedErrorString, pStr);

					if (strstri(pUserName, "Source Control not available") == 0)
					{
						ReplaceStrings(fixedErrorString, sizeof(fixedErrorString) - 1, 
							"Source Control not available", pUserName, false);
					}

					bAlreadySent = true;

					SendErrorDialogToMCPThroughController(STACK_SPRINTF("%s (reported by XBOX Client)", fixedErrorString),
						pTitle, pFault, iHighLight);
				}
			}
		}

		if (strstr(pStr, "ASSERTION FAILED") && ControllerScripting_IsRunning())
		{
			ControllerScripting_Fail(STACK_SPRINTF("XBOX Client Asserted or crashed : %s", pStr));
		}
		else if (!bAlreadySent)
		{
			SendErrorDialogToMCPThroughController(STACK_SPRINTF("%s (reported by XBOX Client)", pStr),
				pTitle, pFault, iHighLight);
		}



	}
	else
	{
		SendErrorDialogToMCPThroughController(STACK_SPRINTF("%s (reported by %s %d)", pStr, GlobalTypeToName(eType), iContainerID),
			pTitle, pFault, iHighLight);
	}

	free(pStr);
	free(pTitle);
	free(pFault);
}



void HandleLauncherRelayingScriptResult(Packet *pPak)
{

	U32 iContainerID = GetContainerIDFromPacket(pPak);
	GlobalType eType = GetContainerTypeFromPacket(pPak);
	int iResult = pktGetBits(pPak, 32);
	char *pStr = pktGetStringTemp(pPak);
	TrackedServerState *pServer;

	assertmsgf(iResult == 1 || iResult == -1, "Only legal values for SetControllerScriptingOutcome are 1 or -1");

	if (iContainerID == SPECIAL_CONTAINERID_XBOX_CLIENT)
	{
		iXBOXClientControllerScriptingCommandStepResult = iResult;
		strcpy(XBOXClientControllerScriptingCommandStepResultString, pStr);
		return;
	}


	pServer = FindServerFromID(eType, iContainerID);

	assertmsgf(pServer, "Unknown server %s(%d) relaying script result to controller", GlobalTypeToName(eType), iContainerID);

	printf("Scripting result %s(%d) from server %s(%d)\n", 
		pStr, iResult, GlobalTypeToName(eType), iContainerID);

	pServer->iControllerScriptingCommandStepResult = iResult;
	strcpy(pServer->controllerScriptingCommandStepResultString, pStr);

}


void HandleLauncherRelayingScriptPauseRequest(Packet *pPak)
{
	U32 iContainerID = GetContainerIDFromPacket(pPak);
	GlobalType eType = GetContainerTypeFromPacket(pPak);
	int iNumSeconds = pktGetBits(pPak, 32);
	char *pReason = pktGetStringTemp(pPak);

	ControllerScripting_TemporaryPause(iNumSeconds, "%s[%u] requesting pause because: %s",
		GlobalTypeToName(eType), iContainerID, pReason);
}


void HandleLauncherRelayingClientState(Packet *pPak)
{
	U32 iContainerID = GetContainerIDFromPacket(pPak);
	GlobalType eType = GetContainerTypeFromPacket(pPak);
	char *pStateString = pktGetStringTemp(pPak);

	if (iContainerID == SPECIAL_CONTAINERID_XBOX_CLIENT)
	{
		sprintf(gXBOXClientState, "%s", pStateString);
	}
	else
	{
		InformControllerOfServerState(eType, iContainerID, pStateString);
	}

}

void HandleRunScript(Packet *pPak)
{
	char *pScriptName = pktGetStringTemp(pPak);
	ControllerScripting_LoadWhileRunning(pScriptName);
}


void HandleSetGameserversStartInDebugger(Packet *pak)
{
	gbStartGameServersInDebugger = pktGetBits(pak, 1);
}

void HandleHereAreServerTypeCommandLines(Packet *pak)
{
	GlobalType eType;

	while ((eType = GetContainerTypeFromPacket(pak)) != GLOBALTYPE_NONE)
	{
		if (eType == GLOBALTYPE_ALL)
		{
			SetGlobalSharedCommandLine_FromMCP(pktGetStringTemp(pak));
		}
		else
		{
			char *pNewCmdLine = pktGetStringTemp(pak);
			bool bStartInDebugger = pktGetBits(pak, 1);

			if (isProductionMode())
			{
				if (DoCommandLineFragmentsDifferMeaningfully(pNewCmdLine, gServerTypeInfo[eType].pSharedCommandLine_FromMCP))
				{
					CRITICAL_NETOPS_ALERT("MCP_SETTING_CMDLINE", "The MCP is changing the command line for servers of type %s from %s to %s in production mode. THis is nonstandard and dangerous",
						GlobalTypeToName(eType), gServerTypeInfo[eType].pSharedCommandLine_FromMCP, pNewCmdLine);
				}

				if (bStartInDebugger)
				{
					CRITICAL_NETOPS_ALERT("MCP_SETTING_START_IN_DEBUGGER", "The MCP start-in-debugger box is checked for servers of type %s in production mode. This is dangerous and nonstandard",
						GlobalTypeToName(eType));
				}

			}

			estrCopy2(&gServerTypeInfo[eType].pSharedCommandLine_FromMCP, pNewCmdLine ? pNewCmdLine : "");
			gServerTypeInfo[eType].bMCPRequestsStartAllInDebugger = bStartInDebugger;
		}
	}
}


void HandleBigGreenButton(Packet *pak)
{
	int iNumServerTypes = pktGetBitsPack(pak, 1);
	int i;

	if (!(spLocalMachine && spLocalMachine->pServersByType[GLOBALTYPE_LAUNCHER]))
	{
		return;
	}

	for (i = 0; i < iNumServerTypes; i++)
	{
		GlobalType eType = GetContainerTypeFromPacket(pak);
		bool bHidden = pktGetBits(pak, 1);
		bool bDebug = pktGetBits(pak, 1);
		char *pCommandLine = pktGetStringTemp(pak);
	
		if (gServerTypeInfo[eType].bAllowMultiplesWhenSingleMCPButtonPressed && iNumServerTypes == 1)
		{
			RegisterNewServerAndMaybeLaunch(0, bHidden ? VIS_HIDDEN : VIS_VISIBLE, FindDefaultMachineForType(eType, NULL, NULL), eType, 
				GetFreeContainerID(eType), bDebug, pCommandLine, NULL, (iNumServerTypes == 1), 0, NULL, "Big Green Button Press from MCP");
		}
		else if (gServerTypeInfo[eType].bKillExistingOnBigGreenButton)
		{
			if (spLocalMachine->pServersByType[eType])
			{
				KillServer(spLocalMachine->pServersByType[eType], "KillExistingOnBigGreenButton");
			}

			RegisterNewServerAndMaybeLaunch(0, bHidden ? VIS_HIDDEN : VIS_VISIBLE, FindDefaultMachineForType(eType, NULL, NULL), eType, 
				GetFreeContainerID(eType), bDebug, pCommandLine, NULL, (iNumServerTypes == 1), 0, NULL, "Big Green Button Press from MCP");
		}
		else
		{
			if (!gpServersByType[eType])
			{
				RegisterNewServerAndMaybeLaunch(0, bHidden ? VIS_HIDDEN : VIS_VISIBLE, FindDefaultMachineForType(eType, NULL, NULL), eType, 
					GetFreeContainerID(eType), bDebug, pCommandLine, NULL, (iNumServerTypes == 1), 0, NULL, "Big Green Button Press from MCP");
			}
		}

	}
}

void XPathFromMCPOnController_CB( U32 iReqID1, U32 iReqID2, StructInfoForHttpXpath *pStructInfo )
{
		ReturnXPathForHttp(GLOBALTYPE_CONTROLLER, 0, iReqID1, iReqID2, pStructInfo);
}

void HandleMCPRequestsXpath(Packet *pPak)
{
	GlobalType eXPathContainerType;
	ContainerID iXPathContainerID;

	ContainerID iMCPContainerID;
	GetHttpFlags eFlags;
	int iRequestID;
	int iAccessLevel = 0;

	UrlArgumentList url = {0};

	eXPathContainerType = GetContainerTypeFromPacket(pPak);
	iXPathContainerID = GetContainerIDFromPacket(pPak);

	iMCPContainerID = GetContainerIDFromPacket(pPak);
	iRequestID = pktGetBits(pPak, 32);
	eFlags = pktGetBits(pPak, 32);


	estrCopy2(&url.pBaseURL, pktGetStringTemp(pPak));

	//check if the packet is done before getting URL args for backward compatibility)
	if (!pktEnd(pPak))
	{
		while (1)
		{
			char *pArgName = pktGetStringTemp(pPak);
			UrlArgument *pArg;

			if (!pArgName[0])
			{
				break;
			}

			pArg = StructCreate(parse_UrlArgument);
			pArg->arg = strdup(pArgName);
			pArg->value = pktMallocString(pPak);

			eaPush(&url.ppUrlArgList, pArg);
		}
	}

	if (!pktEnd(pPak))
	{
		iAccessLevel = pktGetBits(pPak, 32);
	}

	if (!pktEnd(pPak))
	{
		char *pAuthNameAndIP = pktGetStringTemp(pPak);

		//add Auth name and IP as "fake" URL arg just so that they get spawned out to everywhere that might
		//use them...

		urlRemoveValue(&url, "__AUTH");
		urlAddValue(&url, "__AUTH", pAuthNameAndIP, HTTPMETHOD_GET);
	}

	//if the requested xpath is on the controller, we short-circuit the normal remote command process and
	//just do all the work ourself (this is so that the controller can be queried before the transaction
	//server exists)
	if (eXPathContainerType == GLOBALTYPE_CONTROLLER)
	{

		GetStructForHttpXpath(&url, iAccessLevel, iRequestID, iMCPContainerID, XPathFromMCPOnController_CB, eFlags);

	}
	else
	{
		TrackedServerState *pServer;

		if (iXPathContainerID == 0)
		{
			pServer = gpServersByType[eXPathContainerType];
		}
		else
		{
			pServer = FindServerFromID(eXPathContainerType, iXPathContainerID);
		}


		if (!pServer)
		{
			StructInfoForHttpXpath structInfo = {0};
			GetMessageForHttpXpath(STACK_SPRINTF("Controller reports that server %s(%u) doesn't seem to exist",
				GlobalTypeToName(eXPathContainerType), iXPathContainerID), &structInfo, true);

			ReturnXPathForHttp(GLOBALTYPE_CONTROLLER, 0, iRequestID, iMCPContainerID, &structInfo);

			StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
			StructDeInit(parse_UrlArgumentList, &url);
			return;
		}

		if (!ServerReadyForMonitoring(pServer))
		{
			StructInfoForHttpXpath structInfo = {0};
			GetMessageForHttpXpath(STACK_SPRINTF("Controller reports that server %s(%u) isn't ready for monitoring",
				GlobalTypeToName(eXPathContainerType), iXPathContainerID), &structInfo, true);

			ReturnXPathForHttp(GLOBALTYPE_CONTROLLER, 0, iRequestID, iMCPContainerID, &structInfo);

			StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
			StructDeInit(parse_UrlArgumentList, &url);
			return;
		}


		if (pServer->pLink)
		{
			Packet *pReturnPak = pktCreate(pServer->pLink, FROM_CONTROLLER_REQUESTING_MONITORING_INFO);
			pktSendBits(pReturnPak, 32, iRequestID);
			pktSendBits(pReturnPak, 32, iMCPContainerID);
			pktSendBits(pReturnPak, 32, iAccessLevel);
			pktSendBits(pReturnPak, 32, eFlags);
			ParserSendStruct(parse_UrlArgumentList, pReturnPak, &url);

			pktSend(&pReturnPak);
		}
		else if (objLocalManager())
		{
			RemoteCommand_ServerLib_GetXpathForHttp(eXPathContainerType, iXPathContainerID,
				iRequestID, iMCPContainerID, &url, iAccessLevel, eFlags);
		}
		else
		{
			StructInfoForHttpXpath structInfo = {0};
			GetMessageForHttpXpath(STACK_SPRINTF("Server %s(%u) exists, but has no direct link, and there's no transaction server",
				GlobalTypeToName(eXPathContainerType), iXPathContainerID), &structInfo, true);

			ReturnXPathForHttp(GLOBALTYPE_CONTROLLER, 0, iRequestID, iMCPContainerID, &structInfo);

			StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
		}
	}

	StructDeInit(parse_UrlArgumentList, &url);

}

void HandleHereIsMonitoringInfo(Packet *pPacket)
{
	StructInfoForHttpXpath structInfo = {0};

	GlobalType eServingType = GetContainerTypeFromPacket(pPacket);
	ContainerID iServingID = GetContainerIDFromPacket(pPacket);

	int iRequestID = pktGetBits(pPacket, 32);
	ContainerID iMCPID = pktGetBits(pPacket, 32);

	ParserRecv(parse_StructInfoForHttpXpath, pPacket, &structInfo, 0);


	ReturnXPathForHttp(eServingType, iServingID, iRequestID, iMCPID, &structInfo);

	StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
}

void HandleHereIsMonitoringCommandResult(Packet *pak)
{
	int iClientID = pktGetBits(pak, 32);
	int iCommandRequestID = pktGetBits(pak, 32);
	ContainerID iMCPID = GetContainerIDFromPacket(pak);
	char *pRetString = pktGetStringTemp(pak);

	SendMonitoringCommandResultToMCP(iMCPID, iCommandRequestID, iClientID, pRetString, NULL);
}

void HandleHereIsGlobalServerMonitoringSummary(Packet *pak)
{
	GlobalType eType = GetContainerTypeFromPacket(pak);
	ContainerID iID = GetContainerIDFromPacket(pak);

	TrackedServerState *pServer = FindServerFromID(eType, iID);

	if (pServer)
	{
		switch (eType)
		{
		case GLOBALTYPE_TRANSACTIONSERVER:
			{
				StructDeInit(parse_TransactionServerGlobalInfo, pServer->pTransServerSpecificInfo);
				ParserRecv(parse_TransactionServerGlobalInfo, pak, pServer->pTransServerSpecificInfo, 0);
			}
			break;

		default:
			Errorf("HandleHereIsGlobalServerMonitoringSummary got data for unhandled type %s\n", GlobalTypeToName(eType));
		}
	}
}


void HandleAlert(Packet *pak)
{
	char *pKey = pktGetStringTemp(pak);
	char *pString = pktGetStringTemp(pak);
	enumAlertLevel eLevel = pktGetBits(pak, 32);
	enumAlertCategory eCategory = pktGetBits(pak, 32);
	int iLifespan = pktGetBits(pak, 32);
	GlobalType eContainerTypeOfObject = GetContainerTypeFromPacket(pak);
	ContainerID iIDOfObject = GetContainerIDFromPacket(pak);
	GlobalType eContainerTypeOfServer = GetContainerTypeFromPacket(pak);
	ContainerID iIDOfServer = GetContainerIDFromPacket(pak);
	char *pMachineName = pktGetStringTemp(pak);
	int iErrorID = pktGetBits(pak, 32);

	TriggerAlert(pKey, pString, eLevel, eCategory, iLifespan, eContainerTypeOfObject, iIDOfObject, eContainerTypeOfServer,
		iIDOfServer, pMachineName, iErrorID);
}

typedef struct
{
	ContainerID iMCPID;
	int iRequestID_MCP;
	int iRequestID_Controller;
} FileServingIDHandle;

static FileServingIDHandle **sppIDHandles = NULL;
static int siNextFileServingReqID = 1;

static int GetNewControllerReqID(ContainerID MCPID, int iRequestID)
{
	FileServingIDHandle *pHandle = malloc(sizeof(FileServingIDHandle));

	pHandle->iMCPID = MCPID;
	pHandle->iRequestID_MCP = iRequestID;
	pHandle->iRequestID_Controller = siNextFileServingReqID++;
	if (siNextFileServingReqID == 0)
	{
		siNextFileServingReqID++;
	}
	eaPush(&sppIDHandles, pHandle);
	return pHandle->iRequestID_Controller;
}

static bool FindMCPIDAndReqIDFromLocalReqID(int iLocalReqID, ContainerID *pOutMCPID, int *pOutMCPReqID, bool bDone)
{
	int i;

	for (i=0; i < eaSize(&sppIDHandles); i++)
	{
		if (sppIDHandles[i]->iRequestID_Controller == iLocalReqID)
		{
			*pOutMCPID = sppIDHandles[i]->iMCPID;
			*pOutMCPReqID = sppIDHandles[i]->iRequestID_MCP;

			if (bDone)
			{
				free(sppIDHandles[i]);
				eaRemoveFast(&sppIDHandles, i);
			}

			return true;
		}
	}

	return false;
}

static int GetControllerReqIDForCommand(ContainerID MCPID, int iMCPRequestID, enumFileServingCommand eCommand)
{
	int iRetVal;
	int i;

	if (eCommand == FILESERVING_BEGIN)
	{
		return GetNewControllerReqID(MCPID, iMCPRequestID);
	}



	for (i=0; i < eaSize(&sppIDHandles); i++)
	{
		if (sppIDHandles[i]->iMCPID == MCPID && sppIDHandles[i]->iRequestID_MCP == iMCPRequestID)
		{
			iRetVal = sppIDHandles[i]->iRequestID_Controller;

			if (eCommand == FILESERVING_CANCEL)
			{
				free(sppIDHandles[i]);
				eaRemoveFast(&sppIDHandles, i);
			}

			return iRetVal;
		}
	}

	return 0;
}



void Controller_FileServingReturn(ContainerID MCPID, int iRequestID, char *pErrorString, 
	U64 iTotalSize, U64 iCurBeginByteOffset, U64 iCurNumBytes, U8 *pCurData)
{
	TrackedServerState *pServer = FindServerFromID(GLOBALTYPE_MASTERCONTROLPROGRAM, MCPID);

	if (pServer && pServer->pLink)
	{
		Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_FILE_SERVING_RETURN);
		pktSendBits(pPak, 32, iRequestID);

		if (pErrorString)
		{
			pktSendBits(pPak, 1, 1);
			pktSendString(pPak, pErrorString);
		}
		else
		{
			pktSendBits(pPak, 1, 0);
		}

		pktSendBits64(pPak, 64, iTotalSize);
		pktSendBits64(pPak, 64, iCurBeginByteOffset);
		pktSendBits64(pPak, 64, iCurNumBytes);

		if (iCurNumBytes)
		{
			pktSendBytes(pPak, iCurNumBytes, pCurData);
		}
		
		giServerMonitoringBytes += pktGetSize(pPak);


		pktSend(&pPak);
	}

	free(pCurData);

}

void Controller_FileServingFulfillCB(int iRequestID, char *pErrorString,
	 U64 iTotalSize, U64 iCurBeginByteOffset, U64 iCurNumBytes, U8 *pCurData)
{
	ContainerID MCPID;
	int iRequestID_MCP;

	bool bDone = pErrorString || (iCurBeginByteOffset + iCurNumBytes == iTotalSize);

	if (!FindMCPIDAndReqIDFromLocalReqID(iRequestID, &MCPID, &iRequestID_MCP, bDone))
	{
		free(pCurData);
		return;
	}

	Controller_FileServingReturn(MCPID, iRequestID_MCP, pErrorString, iTotalSize, iCurBeginByteOffset, iCurNumBytes, pCurData);
}

void HandleFileServingFulfilled(Packet *pak)
{
	int iReqID = pktGetBits(pak, 32);
	char *pErrorString = pktGetBits(pak, 1) ? pktGetStringTemp(pak) : NULL;
	U64 iTotalSize = pktGetBits64(pak, 64);
	U64 iCurBeginByteOffset = pktGetBits64(pak, 64);
	U64 iCurNumBytes = pktGetBits64(pak, 64);
	char *pData = NULL;

	if (iCurNumBytes)
	{
		pData = malloc(iCurNumBytes);
		pktGetBytes(pak, iCurNumBytes, pData);
	}

	Controller_FileServingFulfillCB(iReqID, pErrorString, iTotalSize, iCurBeginByteOffset, iCurNumBytes, pData);
}

void HandleFileServingRequest(Packet *pak, NetLink *link)
{
	ContainerID MCPID = GetContainerIDFromPacket(pak);
	char *pFileName = pktGetStringTemp(pak);
	int iMCPRequestID = pktGetBits(pak, 32);
	enumFileServingCommand eCommand = pktGetBits(pak, 32);
	U64 iBytesRequested = pktGetBits64(pak, 64);

	ContainerID iServerID;
	GlobalType eServerType;
	static char *pTypeString = NULL;
	static char *pInnerName = NULL;

	if (!DeconstructFileServingName(pFileName, &eServerType, &iServerID, &pTypeString, &pInnerName)
		|| eServerType == GLOBALTYPE_NONE)
	{
		Controller_FileServingReturn(MCPID, iMCPRequestID, STACK_SPRINTF("Couldn't parse filename %s", pFileName), 0, 0, 0, NULL);
		return;
	}

	if (eServerType == GLOBALTYPE_CONTROLLER)
	{
		int iControllerReqID = GetControllerReqIDForCommand(MCPID, iMCPRequestID, eCommand);

		if (iControllerReqID)
		{

			GenericFileServing_CommandCallBack(pFileName, iControllerReqID, eCommand, iBytesRequested, 
				Controller_FileServingFulfillCB);
		}
	}
	else
	{
		TrackedServerState *pServer;

		if (iServerID == 0)
		{
			pServer = gpServersByType[eServerType];
		}
		else
		{
			pServer = FindServerFromID(eServerType, iServerID);
		}

		if (!pServer)
		{
			Controller_FileServingReturn(MCPID, iMCPRequestID, STACK_SPRINTF("Unknown server %s", GlobalTypeAndIDToString(eServerType, iServerID)), 0, 0, 0, NULL);
			return;
		}

		if (!ServerReadyForMonitoring(pServer))
		{
			Controller_FileServingReturn(MCPID, iMCPRequestID, STACK_SPRINTF("Server %s not ready for monitoring", GlobalTypeAndIDToString(eServerType, iServerID)), 0, 0, 0, NULL);
			return;
		}

		if (pServer->pLink)
		{

			int iControllerReqID = GetControllerReqIDForCommand(MCPID, iMCPRequestID, eCommand);

			if (iControllerReqID)
			{
				Packet *pPak = pktCreate(pServer->pLink, FROM_CONTRLLER_REQUESTING_FILE_SERVING);
				pktSendString(pPak, pFileName);
				pktSendBits(pPak, 32, iControllerReqID);
				pktSendBits(pPak, 32, eCommand);
				pktSendBits64(pPak, 64, iBytesRequested);
				pktSend(&pPak);
				return;
			}
		}
		else if (objLocalManager())
		{
			int iControllerReqID = GetControllerReqIDForCommand(MCPID, iMCPRequestID, eCommand);

			if (iControllerReqID)
			{
				assert(iBytesRequested <= INT_MAX);
				RemoteCommand_FileServingRequestForServerMonitoring(pServer->eContainerType, pServer->iContainerID,
					pFileName, iControllerReqID, eCommand, iBytesRequested);
				return;
			}
		}
	
		Controller_FileServingReturn(MCPID, iMCPRequestID, STACK_SPRINTF("Couldn't issue request to server %s", GlobalTypeAndIDToString(eServerType, iServerID)), 0, 0, 0, NULL);
	}
}

AUTO_COMMAND_REMOTE;
void FileServingReturn(int iRequestID, char *pErrorString,
	 U64 iTotalSize, U64 iCurBeginByteOffset, U64 iCurNumBytes, char *pEncodedData, int iEncodedLen)
{
	char *pData = NULL;

	//remote commands lose string-NULL-ness
	if (pErrorString && !pErrorString[0])
	{
		pErrorString = NULL;
	}

	if (iCurNumBytes)
	{
		pData = malloc(iCurNumBytes);
		decodeBase64String(pEncodedData, iEncodedLen, pData, iCurNumBytes);
	}

	Controller_FileServingFulfillCB(iRequestID, pErrorString, iTotalSize, iCurBeginByteOffset, iCurNumBytes, 
		pData);
}

void HandleKeepAlive(Packet *pak)
{
	GlobalType eType = GetContainerTypeFromPacket(pak);
	ContainerID iID = GetContainerIDFromPacket(pak);

	TrackedServerState *pServer = FindServerFromID(eType, iID);

	if (pServer)
	{
		pServer->iNextExpectedKeepAliveTime = timeSecondsSince2000() + gServerTypeInfo[eType].iKeepAliveDelay;		
	}
}

void HandleNewServerPID(Packet *pak)
{
	GlobalType eType = GetContainerTypeFromPacket(pak);
	ContainerID iID = GetContainerIDFromPacket(pak);

	TrackedServerState *pServer = FindServerFromID(eType, iID);

	if (pServer)
	{
		pServer->PID = pktGetBits(pak, 32);

		if (pServer->pLaunchDebugNotification)
		{
			char *pTempString = NULL;
			estrStackCreate(&pTempString);

			estrPrintfUnsafe(&pTempString, "%s you requested has been started on machine <a href=\"cmd:OpenUrlCmd cryptic://vnc/%s\">%s</a>, pid %u cid %u.",
				GlobalTypeToName(eType),
				pServer->pMachine->VNCString, pServer->pMachine->VNCString,
				pServer->PID, iID);

			RemoteCommand_SendDebugTransferMessage(pServer->pLaunchDebugNotification->eServerType, pServer->pLaunchDebugNotification->eServerID, pServer->pLaunchDebugNotification->iCookie, pTempString);

			estrDestroy(&pTempString);
			StructDestroy(parse_ServerLaunchDebugNotificationInfo, pServer->pLaunchDebugNotification);
			pServer->pLaunchDebugNotification = NULL;
		}
	}
}

void HandleHereIsLogServerGlobalInfo(Packet *pak)
{
	ContainerID iID = GetContainerIDFromPacket(pak);

	TrackedServerState *pServer = FindServerFromID(GLOBALTYPE_LOGSERVER, iID);

	if (pServer)
	{
		ParserRecv(parse_LogServerGlobalInfo, pak, pServer->pLogServerSpecificInfo, 0);
	}

}

typedef struct
{
	char *pFileName;
	int iSize;
	U8 *pData; 
} LoadedFileForLaunchers;

LoadedFileForLaunchers **ppLoadedFiles = NULL;

void FreeLoadedFile(LoadedFileForLaunchers *pLoadedFile)
{
	free(pLoadedFile->pFileName);
	free(pLoadedFile->pData);
	free(pLoadedFile);
}
#define MAX_LOADED_FILES 8


LoadedFileForLaunchers *GetLoadedFile(char *pFileName)
{
	LoadedFileForLaunchers *pRetVal;
	void *pBuf;
	int iBufSize;
	int i;

	for (i=0; i < eaSize(&ppLoadedFiles); i++)
	{
		if (stricmp(ppLoadedFiles[i]->pFileName, pFileName) == 0)
		{
			pRetVal = ppLoadedFiles[i];
			if (i != 0)
			{
				eaRemoveFast(&ppLoadedFiles, i);
				eaInsert(&ppLoadedFiles, pRetVal, 0);
			}

			return pRetVal;
		}
	}

	if (eaSize(&ppLoadedFiles) == MAX_LOADED_FILES)
	{
		FreeLoadedFile(eaPop(&ppLoadedFiles));
	}

	pBuf = fileAlloc(pFileName, &iBufSize);
	if (!pBuf)
	{
		AssertOrAlert("NO_FILE_FOR_LAUNCHER", "Couldn't load %s which the launcher wants bytes from", pFileName);
		return NULL;
	}

	pRetVal = calloc(sizeof(LoadedFileForLaunchers), 1);
	pRetVal->pFileName = strdup(pFileName);
	pRetVal->pData = pBuf;
	pRetVal->iSize = iBufSize;

	eaInsert(&ppLoadedFiles, pRetVal, 0);

	return pRetVal;
}

	



void HandleLauncherWantsBytesFromFile(Packet *pak, NetLink *link)
{
	char *pFileName = pktGetStringTemp(pak);
	int iStartingOffset = pktGetBits(pak, 32);
	int iNumBytes = pktGetBits(pak, 32);

	LoadedFileForLaunchers *pLoadedFile = GetLoadedFile(pFileName);

	if (pLoadedFile)
	{
		Packet *pOutPack = pktCreate(link, LAUNCHERQUERY_HEREAREREQUESTEDBYTESFROMFILE);
		pktSendBytes(pOutPack, iNumBytes, pLoadedFile->pData + iStartingOffset);
		pktSend(&pOutPack);
	}
}

void HandleTimeRequest(Packet *pak, NetLink *link)
{
	Packet *pOutPack = pktCreate(link, FROM_CONTROLLER_HERE_IS_TIMESS2000);
	pktSendBits(pOutPack, 32, timeSecondsSince2000());
	pktSend(&pOutPack);
}

#define PERF_CASE(foo) xcase foo: PERFINFO_AUTO_START(#foo, 1);

void ControllerHandleMsg(Packet *pak,int cmd, NetLink *link, TrackedServerState **ppServer)
{
	switch(cmd)
	{
	PERF_CASE( TO_CONTROLLER_CONNECT)
		HandleServerConnectMessage(pak, link, ppServer);
	PERF_CASE( TO_CONTROLLER_DISCONNECT)
		HandleServerDisconnectMessage(pak, link, ppServer);
	PERF_CASE( LAUNCHERANSWER_PROCESSES)
		HandleProcessListUpdate(pak, link);
	PERF_CASE( LAUNCHERANSWER_PROCESS_CLOSED)
		HandleProcessDied(pak, link);
	PERF_CASE( LAUNCHERANSWER_PROCESS_CRASHED)
		HandleProcessCrashed(pak, link, false);
	PERF_CASE( TO_CONTROLLER_BIGGREENBUTTON)
		HandleBigGreenButton(pak);
	PERF_CASE( TO_CONTROLLER_MCP_REQUESTS_RESET)
		sbNeedToKillAllServers = true;
	PERF_CASE( TO_CONTROLLER_MCP_REQUESTS_HIDE_SERVER)
		HandleHideRequest(pak, link);
	PERF_CASE( TO_CONTROLLER_MCP_REQUESTS_LOCAL_STATUS)
		HandleLocalStatusRequest(pak, link);
	PERF_CASE( TO_CONTROLLER_MCP_REQUESTS_SERVER_LIST)
		HandleMCPServerList(pak, link);
	PERF_CASE( TO_CONTROLLER_MCP_REQUESTS_MONITORING_COMMAND)
		HandleMCPMonitoringCommand(pak, link);
	PERF_CASE( TO_CONTROLLER_HERE_ARE_LAUNCHERS_IPS)
		HandleLauncherIPs(pak, link);
	PERF_CASE( TO_CONTROLLER_SETTING_SERVER_STATE)
		HandleSettingServerState(pak, link);
	PERF_CASE( TO_CONTROLLER_ERROR_DIALOG_FOR_MCP)
		HandleErrorDialogForMCP(pak);
	PERF_CASE( TO_CONTROLLER_SET_GAMESERVERS_START_IN_DEBUGGER)
		HandleSetGameserversStartInDebugger(pak);
	PERF_CASE( TO_CONTROLLER_HERE_ARE_SERVERTYPE_COMMANDLINES)
		HandleHereAreServerTypeCommandLines(pak);

//when an MCP connects, immediately send it a server list
	PERF_CASE( TO_CONTROLLER_MCP_IS_READY)
		gbMCPThatStartedMeIsReady = true;
		HandleMCPServerList(pak, link);

	PERF_CASE( TO_CONTROLLER_LAUNCHER_RELAYING_ERRORS)
		HandleLauncherRelayingErrors(pak);
	PERF_CASE( TO_CONTROLLER_LAUNCHER_RELAYING_SCRIPT_RESULT)
		HandleLauncherRelayingScriptResult(pak);
	PERF_CASE( TO_CONTROLLER_LAUNCHER_RELAYING_CLIENT_STATE)
		HandleLauncherRelayingClientState(pak);
	PERF_CASE( TO_CONTROLLER_RUN_SCRIPT)
		HandleRunScript(pak);
	PERF_CASE( TO_CONTROLLER_MCP_REQUESTS_XPATH_FOR_HTTP)
		HandleMCPRequestsXpath(pak);
	PERF_CASE( TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_INFO)
		HandleHereIsMonitoringInfo(pak);
	PERF_CASE( TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_COMMAND_RESULT)
		HandleHereIsMonitoringCommandResult(pak);
	PERF_CASE( TO_CONTROLLER_HERE_IS_GLOBAL_SERVER_MONITORING_SUMMARY)
		HandleHereIsGlobalServerMonitoringSummary(pak);
	PERF_CASE( TO_CONTROLLER_MCP_REQUESTS_MONITORING_JPEG)
		HandleMCPMonitoringJpeg(pak, link);
	PERF_CASE( TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_JPEG)
		HandleHereIsRequestedMonitoringJpeg(pak);
	PERF_CASE( TO_CONTROLLER_HERE_IS_ALERT)
		HandleAlert(pak);
	PERF_CASE( TO_CONTROLLER_MCP_REQUESTS_FILE_SERVING)
		HandleFileServingRequest(pak, link);

	PERF_CASE( TO_CONTROLLER_FILE_SERVING_FULFILLED)
		HandleFileServingFulfilled(pak);
	PERF_CASE( TO_CONTROLLER_KEEP_ALIVE)
		HandleKeepAlive(pak);
	PERF_CASE( TO_CONTROLLER_PID_OF_NEW_SERVER)
		HandleNewServerPID(pak);
	PERF_CASE( TO_CONTROLLER_LAUNCHER_REQUESTS_LOCAL_EXES_FOR_MIRRORING)
		HandleLauncherRequestsLocalExesForMirroring(link, pak);
	PERF_CASE( TO_CONTROLLER_LAUNCHER_GOT_LOCAL_EXES_FOR_MIRRORING)
		HandleLauncherGotLocalExesForMirroring(link, pak);
	PERF_CASE( TO_CONTROLLER_HERE_IS_GLOBAL_LOGSERVER_INFO )
		HandleHereIsLogServerGlobalInfo(pak);
	PERF_CASE( LAUNCHER_ANSWER_CRYPTIC_ERROR_IS_FINISHED_WITH_SERVER )
		HandleProcessCrashed(pak, link, true);

	PERF_CASE( TO_CONTROLLER_HERE_IS_DATA_MIRRORING_REPORT )
		HandleDataMirroringReport(pak, link);

	PERF_CASE( TO_CONTROLLER_EXECUTE_COMMAND_STRING )
		globCmdParse(pktGetStringTemp(pak));

	PERF_CASE( TO_CONTROLLER_LAUNCHER_WANTS_BYTES_FROM_FILE )
		HandleLauncherWantsBytesFromFile(pak, link);

	PERF_CASE( TO_CONTROLLER_LAUNCHER_REPORTING_DYN_HOG_PRUNING_SUCCESS )
		HandleDynHogPruningSuccess(pak, link);
	PERF_CASE( TO_CONTROLLER_LAUNCHER_REPORTING_DYN_HOG_PRUNING_FAILURE )
		HandleDynHogPruningFailure(pak, link);
	PERF_CASE( TO_CONTROLLER_LAUNCHER_REPORTING_DYN_HOG_PRUNING_COMMENT )
		HandleDynHogPruningComment(pak, link);
	PERF_CASE( TO_CONTROLLER_MCP_SETS_EXE_NAME_AND_DIR )
		HandleOverrideExeNameAndDir(pak, link);
	PERF_CASE( TO_CONTROLLER_LAUNCHER_RELAYING_SCRIPT_PAUSE_REQUEST)
		HandleLauncherRelayingScriptPauseRequest(pak);
	PERF_CASE( TO_CONTROLLER_LAUNCHER_REQUESTING_EXE_FROM_NAME_AND_CRC )
		HandleLauncherRequestingExeFromNameAndCRC(pak, link);
	PERF_CASE( TO_CONTROLLER_HERE_ARE_SYSTEM_SPECS)
		HandleSystemSpecs(pak, link);
	PERF_CASE( TO_CONTROLLER_REQUESTING_TIMESS2000 )
		HandleTimeRequest(pak, link);

	PERF_CASE( TO_CONTROLLER_FILE_SERVING_HANDSHAKE )
		HandleThrottledFileSendingHandshake(pak);

	xdefault:
		PERFINFO_AUTO_START("Default_case", 1);
		printf("Unknown command %d\n",cmd);
	}

	PERFINFO_AUTO_STOP();
}

