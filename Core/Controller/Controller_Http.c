#include "controller.h"
#include "GlobalTypes.h"
#include "httpXPathSupport.h"
#include "autogen/controller_http_c_ast.h"
#include "estring.h"
#include "serverlib.h"
#include "timing.h"
#include "Autogen/svrGlobalInfo_h_ast.h"
#include "sock.h"
#include "UtilitiesLib.h"
#include "Alerts.h"
#include "Alerts_h_ast.h"
#include "autogen/controllerpub_h_ast.h"
#include "logging.h"
#include "autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "autogen/GameServerLib_autogen_RemoteFuncs.h"
#include "TimedCallback.h"
#include "Controller_h_ast.h"
#include "httpLib.h"
#include "file.h"
#include "sharedMemory.h"
#include "shardVariableCommon.h"
#include "resourceManager.h"
#include "ugcProjectUtils.h"
#include "Controller_AutoSettings.h"
#include "EventCountingHeatMap.h"
#include "Controller_Utils.h"
#include "systemspecs.h"
#include "SystemSpecs_h_ast.h"
#include "SentryServerComm.h"

#define MAX_MACHINES_TO_SHOW 10
#define MAX_MACHINES_WITH_ALERTS_TO_SHOW 20

#define MAX_GAMESERVERS_TO_SHOW 10
#define MAX_GAMESERVERS_WITH_ALERTS_TO_SHOW 20

static char *spBannerString = NULL;


static StashTable sMonitorLaunchersAndMultiplexers = NULL;

bool MonitorLaunchersAndMultiplexers(const char *pAuthNameAndIP)
{
	int iMonitor;

	if (!pAuthNameAndIP)
	{
		pAuthNameAndIP = "";
	}
	
	if (!sMonitorLaunchersAndMultiplexers)
	{
		return false;
	}

	if (stashFindInt(sMonitorLaunchersAndMultiplexers, pAuthNameAndIP, &iMonitor))
	{
		return iMonitor;
	}

	return false;
}

AUTO_COMMAND;
void ToggleMonitorLaunchersAndMultiplexers(CmdContext *pContext)
{
	const char *pAuthNameAndIP = pContext->pAuthNameAndIP;
	int iCur;
	
	if (!pAuthNameAndIP)
	{
		pAuthNameAndIP = "";
	}

	if (!sMonitorLaunchersAndMultiplexers)
	{
		sMonitorLaunchersAndMultiplexers = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);
	}

	iCur = MonitorLaunchersAndMultiplexers(pAuthNameAndIP);

	stashAddInt(sMonitorLaunchersAndMultiplexers, pAuthNameAndIP, !iCur, true);
}





ControllerAlertListOverview *GetControllerAlertListOverview(KeyedAlertList *pList, U32 iCurTime)
{
	ControllerAlertListOverview *pRetVal = StructCreate(parse_ControllerAlertListOverview);

	pRetVal->iTotalCount = pList->iTotalCount;
	pRetVal->iLastHour = EventCounter_GetCount(pList->pEventCounter, EVENTCOUNT_LASTFULLHOUR, iCurTime) 
		+ EventCounter_GetCount(pList->pEventCounter, EVENTCOUNT_CURRENTMINUTE, iCurTime);
	estrPrintf(&pRetVal->pLink, "<a href=\"/viewxpath?xpath=%s[0].globObj.AlertLists[%s]\">%s</a>", ControllerMirroringIsActive() ? "WebRequestServer" : "Controller", pList->pKey, pList->pKey);
	return pRetVal;
}

int CompareAlertListOverviews(const ControllerAlertListOverview **ppList1, const ControllerAlertListOverview **ppList2)
{
	return ((*ppList2)->iLastHour) - ((*ppList1)->iLastHour);
}

void FillInControllerAlertsOverview(ControllerAlertsOverview *pOverview)
{
	U32 iCurTime = timeSecondsSince2000();
	FOR_EACH_IN_STASHTABLE(gKeyedAlertListsByKey, KeyedAlertList, pList)
	{
		if (pList->iTotalBySeverity[ALERTLEVEL_CRITICAL])
		{
			eaPush(&pOverview->Critical.ppAlerts, GetControllerAlertListOverview(pList, iCurTime));
		}
		else if (pList->iTotalBySeverity[ALERTLEVEL_WARNING])
		{
			eaPush(&pOverview->Warning.ppAlerts, GetControllerAlertListOverview(pList, iCurTime));
		}
	}
	FOR_EACH_END

	eaQSort(pOverview->Critical.ppAlerts, CompareAlertListOverviews);
	eaQSort(pOverview->Warning.ppAlerts, CompareAlertListOverviews);
}


void FillInGenericServerInfo(Controller_GenericServerOverview *pGenericInfo, TrackedServerState *pServer)
{
    assert(pServer->eContainerType >= 0 && pServer->eContainerType < GLOBALTYPE_MAXTYPES);
    estrPrintf(&pGenericInfo->pType, "%s", GlobalTypeToName(pServer->eContainerType));
    pGenericInfo->iID = pServer->iContainerID;

    estrPrintf(&pGenericInfo->pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pServer->pMachine->VNCString);

    estrPrintf(&pGenericInfo->pMonitorKey, "%s%u", pGenericInfo->pType, pGenericInfo->iID);
    if (gServerTypeInfo[pServer->eContainerType].bCanNotBeHttpMonitored)
    {
		estrCopy2(&pGenericInfo->pLink, "&nbsp;");
    }
    else if (!ServerReadyForMonitoring(pServer))
    {
		estrPrintf(&pGenericInfo->pLink, "...");
    }
    else
    {
		estrPrintf(&pGenericInfo->pLink, "<a href=\"/viewxpath?xpath=%s[%u]%s\">Monitor</a>",
			GlobalTypeToName(pServer->eContainerType), pServer->iContainerID, CUSTOM_DOMAIN_NAME);
    }


    estrCopy2(&pGenericInfo->pState, pServer->stateString[0] ? pServer->stateString : " ");
    estrCopy2(&pGenericInfo->pMachine_IP, makeIpStr(pServer->pMachine->iPublicIP));
    estrCopy2(&pGenericInfo->pMachine_StringName, pServer->pMachine->machineName);
    estrPrintf(&pGenericInfo->pMachine, "<a href=\"%s.machine[%d]\">%s</a>", 
                    LinkToThisServer(), pServer->pMachine - gTrackedMachines, pServer->pMachine->machineName);
    pGenericInfo->iCreationTime = pServer->iCreationTime;
	

    //exclude a few small negative values that the launcher uses that have special meaning
    if (pServer->iErrorID > 0 || pServer->iErrorID < -1000)
    {
		estrPrintf(&pGenericInfo->pError, "<a href=\"http://%s/detail?id=%d\">%d</a>",
			getErrorTracker(), pServer->iErrorID, pServer->iErrorID);
    }

    if (pServer->bAlertsMuted)
    {
        estrPrintf(&pGenericInfo->pUnMuteAlerts, "UnMuteAlerts %s %u $NOCONFIRM $NORETURN", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID);
    }
    else
    {
        estrPrintf(&pGenericInfo->pMuteAlerts, "MuteAlerts %s %u $NOCONFIRM $NORETURN", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID);
    }

    pGenericInfo->fFPS = pServer->perfInfo.fFPS;

	pGenericInfo->fCPUPercent = pServer->perfInfo.fCPUUsageLastMinute;
	pGenericInfo->iRAMMegs = (pServer->perfInfo.physicalMemUsed / (1024 * 1024));
 
}

char *GetLaunchAServerCommand(void)
{
    static char *pOutString = NULL;
    int i;

    if (!pOutString)
    {
        bool bFirst = true;
        estrPrintf(&pOutString, "LaunchAServerThroughServerMonitoring $FIELD(MachineName_Internal) $INT(How many to launch) $SELECT(What type of server?|");
        for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
        {
            if (gServerTypeInfo[i].bCanBeManuallyLaunchedThroughServerMonitor)
            {
                estrConcatf(&pOutString, "%s%s", bFirst ? "" : ",", GlobalTypeToName(i));
                bFirst = false;
            }
        }

        estrConcatf(&pOutString, ") $STRING(Extra command line options)");
    }

    return pOutString;
}


static ControllerUGCOverview *spUGCOverview = NULL;

void GetShardLockageString(char **ppOutString)
{
	estrClear(ppOutString);

	if (Controller_DoingLogServerStressTest())
	{
		estrPrintf(ppOutString, "Doing LogServer stress tests... %d gameservers, %d logs/gs/second", 
			giTotalNumServersByType[GLOBALTYPE_GAMESERVER], Controller_GetLogServerStressTestLogsPerServerPerSecond());
	}

	if (gbShardIsLocked)
	{
		estrPrintf(ppOutString, "The Shard is Locked! The Shard is Locked! The Shard is Locked!");
	}

	if (gbGatewayIsLocked)
	{
		estrConcatf(ppOutString, "Gateway is Locked!");
	}

	if (gbBillingKillSwitch)
	{
		estrConcatf(ppOutString, "Billing Kill Switch! Billing Kill Switch!");
	}

	if (gbMTKillSwitch)
	{
		estrConcatf(ppOutString, "MicroTrans Kill Switch! MicroTrans Kill Switch!");
	}



}


ControllerOverview *GetControllerOverview_Internal(bool bForceFullList, bool bMonitorLaunchersAndMultiplexers)
{
	int i;
	TrackedServerState *pServer;
	int iNumClientControllers = 0;
	static bool bUGCEnabled = false;
	static U32 iUGCVirtualShardID = 1;
	ControllerOverview *pControllerOverview;

	PERFINFO_AUTO_START_FUNC();


	if (spUGCOverview)
	{
		StructReset(parse_ControllerUGCOverview, spUGCOverview);
	}
	else
	{
		spUGCOverview = StructCreate(parse_ControllerUGCOverview);
	}

	pControllerOverview = StructCreate(parse_ControllerOverview);

	GetShardLockageString(&pControllerOverview->pTheShardIsLocked);

	if (isProductionMode() && sharedMemoryGetMode() == SMM_DISABLED)
	{
		estrPrintf(&pControllerOverview->pSharedMemoryIsDisabled, "SHARED MEMORY DISABLED!!! (If you are not a programmer who did this on purpose, something is wrong");
	}

	//            pControllerOverview->ppAlerts = ppAlerts;

	pControllerOverview->iShardCreationTime = gControllerStartupTime;
	pControllerOverview->fControllerFPS = gfLastReportedControllerFPS;

	estrPrintf(&pControllerOverview->pVersion, "%s", GetUsefulVersionString());

	pControllerOverview->pStartupStatus = strdup(Controller_GetStartupStatusString());

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		pServer = gpServersByType[i];

		if (i == GLOBALTYPE_CONTROLLER)
		{
			continue;
		}

		if (i == GLOBALTYPE_GAMESERVER)
		{
		

			if (!bForceFullList && giTotalNumServersByType[GLOBALTYPE_GAMESERVER] > MAX_GAMESERVERS_TO_SHOW)
			{
				pControllerOverview->pProductionGameServerSummary = StructCreate(parse_Controller_ProductionGameServerSummary);

				estrPrintf(&pControllerOverview->pProductionGameServerSummary->pCommentString, "<a href=\"/viewxpath?xpath=%s[0].globObj.Gameservers\">%d total gameservers</a>", ControllerMirroringIsActive() ? "WebRequestServer" : "Controller", giTotalNumServersByType[GLOBALTYPE_GAMESERVER]);

				while (pServer)
				{
					GameServerGlobalInfo *pGSLInfo = pServer->pGameServerSpecificInfo;
					if (pGSLInfo->bIsEditingServer)
					{
						spUGCOverview->iEditingGameservers++;
					}
					else if (pGSLInfo->iVirtualShardID == iUGCVirtualShardID)
					{
						spUGCOverview->iOtherUGCShardGameServers++;
					}
					else if (resNamespaceIsUGC(pGSLInfo->mapName))
					{
						spUGCOverview->iPlayingUGCGameServers++;
						spUGCOverview->iPlayingUGCPlayers += pGSLInfo->iNumPlayers;
					}


					if (pServer->iNumActiveStateBasedAlerts)
					{
						Controller_GameServerOverview *pServerOverview = StructCreate(parse_Controller_GameServerOverview);

						FillInGenericServerInfo(&pServerOverview->gGenericInfo, pServer);
						pServerOverview->bLocked = pServer->bLocked;

						if (pServer->pGameServerSpecificInfo)
						{
							StructCopyAll(parse_GameServerGlobalInfo, pServer->pGameServerSpecificInfo, &pServerOverview->gGlobalInfo);
						}

						if (pServerOverview->gGlobalInfo.iLastContact == 0)
						{
							pServerOverview->gGlobalInfo.iLastContact = timeSecondsSince2000();
						}
						eaPush(&pControllerOverview->pProductionGameServerSummary->ppServersWithAlerts, pServerOverview);
						if (eaSize(&pControllerOverview->pProductionGameServerSummary->ppServersWithAlerts) >= MAX_GAMESERVERS_WITH_ALERTS_TO_SHOW)
						{
							break;
						}
					}


			

					pServer = pServer->pNext;
				}
			}
			else
			{

				while (pServer)
				{
					Controller_GameServerOverview *pServerOverview = StructCreate(parse_Controller_GameServerOverview);
					GameServerGlobalInfo *pGSLInfo = pServer->pGameServerSpecificInfo;

					FillInGenericServerInfo(&pServerOverview->gGenericInfo, pServer);

					if (pGSLInfo->bIsEditingServer)
					{
						spUGCOverview->iEditingGameservers++;
					}
					else if (pGSLInfo->iVirtualShardID == 1)
					{
						spUGCOverview->iOtherUGCShardGameServers++;
					}
					else if (resNamespaceIsUGC(pGSLInfo->mapName))
					{
						spUGCOverview->iPlayingUGCGameServers++;
						spUGCOverview->iPlayingUGCPlayers += pGSLInfo->iNumPlayers;
					}


					pServerOverview->bLocked = pServer->bLocked;

					if (pServer->pGameServerSpecificInfo)
					{
						StructCopyAll(parse_GameServerGlobalInfo, pServer->pGameServerSpecificInfo, &pServerOverview->gGlobalInfo);
					}

					if (pServerOverview->gGlobalInfo.iLastContact == 0)
					{
						pServerOverview->gGlobalInfo.iLastContact = timeSecondsSince2000();
					}
					eaPush(&pControllerOverview->ppGameServers, pServerOverview);

					pServer = pServer->pNext;
				}
			}
		}
		else if (i == GLOBALTYPE_TESTCLIENT)
		{
			//remove all client controllers from the front page for now, adding ClientControllerOverview
            
			while (pServer)
			{/*
				Controller_ClientControllerOverview *pServerOverview = StructCreate(parse_Controller_ClientControllerOverview);

				FillInGenericServerInfo(&pServerOverview->gGenericInfo, pServer);


				eaPush(&pControllerOverview->ppClientControllers, pServerOverview);*/
				iNumClientControllers++;

				pServer = pServer->pNext;
			}
		}
		else if (i == GLOBALTYPE_TRANSACTIONSERVER)
		{
			if (pServer)
			{
				Controller_TransactionServerOverview *pTransServerOverview = StructCreate(parse_Controller_TransactionServerOverview);

				FillInGenericServerInfo(&pTransServerOverview->gGenericInfo, pServer);

				if (pServer->pTransServerSpecificInfo)
				{
					StructCopyAll(parse_TransactionServerGlobalInfo, pServer->pTransServerSpecificInfo, &pTransServerOverview->gGlobalInfo);
				}

				eaPush(&pControllerOverview->ppTransactionServer, pTransServerOverview);
			}
		}
		else if (IsTypeObjectDB(i))
		{
			while (pServer)
			{
				Controller_DatabaseOverview *pServerOverview = StructCreate(parse_Controller_DatabaseOverview);

				FillInGenericServerInfo(&pServerOverview->gGenericInfo, pServer);

				if (pServer->pDataBaseSpecificInfo)
				{
					StructCopyAll(parse_DatabaseGlobalInfo, pServer->pDataBaseSpecificInfo, &pServerOverview->gGlobalInfo);
				}

				eaPush(&pControllerOverview->ppDatabases, pServerOverview);

				pServer = pServer->pNext;
			}
		}
		else if (i == GLOBALTYPE_LOGINSERVER)
		{
			while (pServer)
			{
				Controller_GenericServerOverview *pServerOverview = StructCreate(parse_Controller_GenericServerOverview);
				FillInGenericServerInfo(pServerOverview, pServer);
				eaPush(&pControllerOverview->ppOtherServers, pServerOverview);

				if (pServer->pLoginServerSpecificInfo->bUGCEnabled)
				{
					bUGCEnabled = true;
				}

				if (pServer->pLoginServerSpecificInfo->iUGCVirtualShardID)
				{
					iUGCVirtualShardID = pServer->pLoginServerSpecificInfo->iUGCVirtualShardID;
				}



				pServer = pServer->pNext;
			}

		}
		else if (i == GLOBALTYPE_JOBMANAGER)
		{
			while (pServer)
			{
				Controller_GenericServerOverview *pServerOverview = StructCreate(parse_Controller_GenericServerOverview);

				FillInGenericServerInfo(pServerOverview, pServer);

				eaPush(&pControllerOverview->ppOtherServers, pServerOverview);

			
				StructDestroySafe(parse_JobManagerGlobalInfo, &spUGCOverview->pJobManagerInfo);
				spUGCOverview->pJobManagerInfo = StructClone(parse_JobManagerGlobalInfo, pServer->pJobManagerSpecificInfo);


				pServer = pServer->pNext;
			}


		}
		else if (!(gServerTypeInfo[i].bCanNotBeHttpMonitored) || ((i == GLOBALTYPE_LAUNCHER || i == GLOBALTYPE_MULTIPLEXER ) && bMonitorLaunchersAndMultiplexers))
		{
			while (pServer)
			{
				Controller_GenericServerOverview *pServerOverview = StructCreate(parse_Controller_GenericServerOverview);

				FillInGenericServerInfo(pServerOverview, pServer);

				eaPush(&pControllerOverview->ppOtherServers, pServerOverview);

				pServer = pServer->pNext;
			}
		}
	}


	for (i=0; i < giNumMachines; i++)
	{
		TrackedMachineState *pMachine = &gTrackedMachines[i];
        
		if (!pMachine->pServersByType[GLOBALTYPE_LAUNCHER] || pMachine->bLauncherIsCurrentlyStalled)
		{
			Controller_DeadMachineOverview *pMachineOverview = StructCreate(parse_Controller_DeadMachineOverview);

			estrPrintf(&pMachineOverview->pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pMachine->VNCString);
			estrPrintf(&pMachineOverview->pMachineName, "<a href=\"%s.machine[%d]\">%s%s</a>", 
							LinkToThisServer(), i, pMachine->bIsLocked ? "(LOCKED)" : "", pMachine->machineName);
			estrPrintf(&pMachineOverview->pMachineName_Internal, "%s", pMachine->machineName);
			pMachineOverview->iPublicIP = pMachine->iPublicIP;
			pMachineOverview->iPrivateIP = pMachine->IP;

			eaPush(&pControllerOverview->ppDeadMachines, pMachineOverview);

		}
	}

	if (! bForceFullList && giNumMachines >= MAX_MACHINES_TO_SHOW)
	{
		pControllerOverview->pProductionMachineSummary = StructCreate(parse_Controller_ProductionMachineSummary);
		estrPrintf(&pControllerOverview->pProductionMachineSummary->pCommentString, "<a href=\"/viewxpath?xpath=%s[0].globObj.Machines\">%d total machines</a>", 
			ControllerMirroringIsActive() ? "WebRequestServer" : "Controller", giNumMachines);
	}
	else
	{
		for (i=0; i < giNumMachines; i++)
		{
			TrackedMachineState *pMachine = &gTrackedMachines[i];
            
			if (pMachine->pServersByType[GLOBALTYPE_LAUNCHER] && !pMachine->bLauncherIsCurrentlyStalled)
			{
				if (!pControllerOverview->pProductionMachineSummary
								|| (pMachine->iNumActiveStateBasedAlerts && eaSize(&pControllerOverview->pProductionMachineSummary->ppMachinesWithAlerts) < MAX_MACHINES_WITH_ALERTS_TO_SHOW))
				{
					Controller_MachineOverview *pMachineOverview = StructCreate(parse_Controller_MachineOverview);
					int iServerCount = 0;
					int j;

					estrPrintf(&pMachineOverview->pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pMachine->VNCString);
					estrPrintf(&pMachineOverview->pMachineName, "<a href=\"%s.machine[%d]\">%s%s</a>", 
									LinkToThisServer(), i, pMachine->bIsLocked ? "(LOCKED)" : "", pMachine->machineName);
					estrPrintf(&pMachineOverview->pMachineName_Internal, "%s", pMachine->machineName);
					pMachineOverview->iPublicIP = pMachine->iPublicIP;
					pMachineOverview->iPrivateIP = pMachine->IP;

					for (j=0; j < GLOBALTYPE_MAXTYPES; j++)
					{
						pServer = pMachine->pServersByType[j];

						while (pServer && pServer->pMachine == pMachine)
						{
							iServerCount++;
							pServer = pServer->pNext;
						}
					}
    
					pMachineOverview->iNumServers = iServerCount;
					pMachineOverview->iConnectionTime = pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->iCreationTime;
					StructCopyAll(parse_TrackedPerformance, &pMachine->performance, &pMachineOverview->performance);
    

					if (pControllerOverview->pProductionMachineSummary)
					{
						eaPush(&pControllerOverview->pProductionMachineSummary->ppMachinesWithAlerts, pMachineOverview);
					}
					else
					{
						eaPush(&pControllerOverview->ppMachines, pMachineOverview);
					}
				}
			}
		}
	}


	for (i=0; i < eaSize(&gSentryClientList.ppClients); i++)
	{
		Controller_OpenMachineOverview *pOpenMachineOverview = StructCreate(parse_Controller_OpenMachineOverview);
		pOpenMachineOverview->pMachineName = strdup(gSentryClientList.ppClients[i]->name);

		if (gPatchingCommandLine[0] == 0)
		{
			estrPrintf(&pOpenMachineOverview->pGrab, "GrabMachineForShard %s $CONFIRM(Really attempt to make machine %s part of your shard? (This will only work if it has the same server code installed in the same directory... launcher.exe will be run from %s)) $NORETURN",
				pOpenMachineOverview->pMachineName, pOpenMachineOverview->pMachineName, gCoreExecutableDirectory);
		}
		else
		{
			estrPrintf(&pOpenMachineOverview->pGrabAndPatch, "GrabMachineForShardAndMaybePatch %s 1 1 GrabAndPatch $CONFIRM(Really attempt to patch machine %s to your current version and make it part of your shard?) $NORETURN",
				pOpenMachineOverview->pMachineName, pOpenMachineOverview->pMachineName);
		}

		eaPush(&pControllerOverview->ppOpenMachines, pOpenMachineOverview);
	}

	estrPrintf(&pControllerOverview->pServerConfiguration, "<a href=\"/viewxpath?xpath=%s[%u].ServerTypeInfo\">Server Type Configuration</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID);
	estrPrintf(&pControllerOverview->pShardVariables, "<a href=\"/viewxpath?xpath=%s[%u].ShardVariables\">Shard Variables</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID);
	estrPrintf(&pControllerOverview->pGameServerLaunchingConfig, "<a href=\"/viewxpath?xpath=%s[%u].GameServerLaunching\">Game Server Launching Config</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID);
	estrPrintf(&pControllerOverview->pUGCOverview, "<a href=\"/viewxpath?xpath=%s[%u].UgcOverview\">UGC Overview</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID);
	estrPrintf(&pControllerOverview->pGenericInfo, "<input type=\"hidden\" id=\"defaultautorefresh\" value=\"1\"><a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));

	if (ControllerAutoSetting_SystemIsActive())
	{
		estrPrintf(&pControllerOverview->pControllerAutoSettings, "<a href=\"/viewxpath?xpath=%s[%u].globObj.Controllerautosetting_Categories\">Controller Auto Settings</a>",
			GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID);
	}

	if (gbUseSentryServer)
	{
		estrPrintf(&pControllerOverview->pQuerySentryServerForOpenMachines, "RequestOpenMachineListFromSentryServer $CONFIRM(Really request a list of available machines from SentryServer?) $NORETURN");
	}

	if (Controller_DoingLogServerStressTest())
	{
		estrPrintf(&pControllerOverview->pSetStressTestLogsPerServerPerSecond, "SetLogServerStressTestLogsPerServerPerSecond $INT(How many) $NORETURN");
	}

	estrPrintf(&pControllerOverview->pToggleMonitoringLaunchersAndMultiplexers, "ToggleMonitorLaunchersAndMultiplexers $NOCONFIRM $NORETURN");

	for (i=0; i < ALERTLEVEL_COUNT; i++)
	{
		int iCurCount = Alerts_GetCountByLevel(i);
		int iTotalCount = Alerts_GetTotalCountByLevel(i);
		char **ppLinkString = NULL;
		
		if (iCurCount || iTotalCount)
		{
			switch (i)
			{
			xcase ALERTLEVEL_WARNING:
				ppLinkString = &pControllerOverview->alertsOverview.Warning.pLink;
			xcase ALERTLEVEL_CRITICAL:
				ppLinkString = &pControllerOverview->alertsOverview.Critical.pLink;
			}

			if (ppLinkString)
			{
				estrPrintf(ppLinkString, "<div class=\"divAlertLevel%s\"><a href=\"%s.globobj.alerts&svrfilter=me.level%%3D%d\">%d %s Alert%s (%d total) </a></div>",
					StaticDefineIntRevLookup(enumAlertLevelEnum, i), LinkToThisServer(), i, iCurCount, StaticDefineIntRevLookup(enumAlertLevelEnum, i), iCurCount == 1 ? "" : "s", iTotalCount);
			}
		}
	}

	FillInControllerAlertsOverview(&pControllerOverview->alertsOverview);

	if (iNumClientControllers)
	{
		estrPrintf(&pControllerOverview->pClientControllers, "<a href=\"/viewxpath?xpath=%s[0].globobj.ClientControllers\">%d ClientControllers</a>",
			ControllerMirroringIsActive() ? "WebRequestServer" : "Controller", iNumClientControllers);
	}

	if (gpVersionHistory)
	{
		pControllerOverview->pVersionHistory = StructClone(parse_VersionHistory, gpVersionHistory);
	}
	
	if (spBannerString)
	{
		estrCopy2(&pControllerOverview->pBannerString, spBannerString);
		estrPrintf(&pControllerOverview->pConfirm, "ConfirmControllerScriptingStep $NOCONFIRM $NORETURN");
	}

	if (Controller_AreThereFrankenBuilds())
	{
		estrPrintf(&pControllerOverview->pFrankenBuilds, "<a href=\"/viewxpath?xpath=Controller[0].globobj.Frankenbuilds\">There are FrankenBuilds</a>");
	}

	if (bForceFullList)
	{
		TrackedServerState *pLogServer = gpServersByType[GLOBALTYPE_LOGSERVER];
		if (pLogServer)
		{
			if (pLogServer->pLogServerSpecificInfo)
			{
				pControllerOverview->pLogServerGlobalInfo = StructClone(parse_LogServerGlobalInfo, pLogServer->pLogServerSpecificInfo);
			}
		}
	}


	PERFINFO_AUTO_STOP();

	return pControllerOverview;
}

ControllerOverview *GetControllerOverview(bool bForceFullList, const char *pAuthNameAndIP)
{
	static ControllerOverview *spControllerOverview = NULL;
	static U32 iTimeStamp = 0;

	static ControllerOverview *spControllerOverview_LaM = NULL;
	static U32 iTimeStamp_LaM = 0;

	bool bMonitorLaunchersAndMultiplexers = MonitorLaunchersAndMultiplexers(pAuthNameAndIP);

	U32 iCurTime = timeSecondsSince2000();

	if (bForceFullList)
	{
		return GetControllerOverview_Internal(true, false);
	}

	if (bMonitorLaunchersAndMultiplexers)
	{
		if (spControllerOverview_LaM && iTimeStamp_LaM >= iCurTime - 3)
		{
			return spControllerOverview_LaM;
		}

		StructDestroySafe(parse_ControllerOverview, &spControllerOverview_LaM);
		spControllerOverview_LaM = GetControllerOverview_Internal(false, true);
		iTimeStamp_LaM = iCurTime;

		return spControllerOverview_LaM;
	}

	if (spControllerOverview && iTimeStamp >= iCurTime - 3)
	{
		return spControllerOverview;
	}

	StructDestroySafe(parse_ControllerOverview, &spControllerOverview);
	spControllerOverview = GetControllerOverview_Internal(false, false);
	iTimeStamp = iCurTime;

	return spControllerOverview;

}



ControllerUGCOverview *GetControllerUGCOverview(const char *pAuthNameAndIP)
{
	GetControllerOverview(false, pAuthNameAndIP);
	return spUGCOverview;
}



void OVERRIDE_LATELINK_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{

    *ppTPI = parse_ControllerOverview;
    *ppStruct = GetControllerOverview(false, urlFindSafeValue(pUrl, "__AUTH"));
}





AUTO_COMMAND;
char *KillServerCmd_NoRestart(char *pServerType, ContainerID iID, char *pConfirm, CmdContext *pContext)
{
    GlobalType eType = NameToGlobalType(pServerType);

	if (stricmp(pConfirm, "yes") != 0)
	{
		return "You didn't type yes!";
	}

    if (eType)
    {
        TrackedServerState *pServer = FindServerFromID(eType, iID);

        if (pServer)
        {
			char *pCommentString = NULL;
			estrStackCreate(&pCommentString);
			estrPrintf(&pCommentString, "KillServerCmd_NoRestart called via %s", GetContextHowString(pContext));
            pServer->bKilledIntentionally = true;
            KillServer(pServer, pCommentString);                                         
			estrDestroy(&pCommentString);
			return "killed!";
        }
    }

	return "couldn't find server";
}

AUTO_COMMAND;
void KillServerCmd_Normal(char *pServerType, ContainerID iID, CmdContext *pContext)
{
    GlobalType eType = NameToGlobalType(pServerType);

    if (eType)
    {
        TrackedServerState *pServer = FindServerFromID(eType, iID);

        if (pServer)
        {
			char *pCommentString = NULL;

			pServer->bKilledIntentionally = true;
			pServer->bRecreateDespiteIntentionalKill = true;

			estrStackCreate(&pCommentString);
			estrPrintf(&pCommentString, "KillServerCmd_Normal called via %s", GetContextHowString(pContext));
            KillServer(pServer, pCommentString);                                         
			estrDestroy(&pCommentString);
        }
    }
}

AUTO_COMMAND;
void KillAllServersOnMachine(bool bExcludeLauncher, bool bNoRestart, char *pMachineName, CmdContext *pContext)
{
	
	TrackedMachineState *pMachine = FindMachineByName(pMachineName);
	char *pCommentString = NULL;
	estrStackCreate(&pCommentString);
	estrPrintf(&pCommentString, "KillAllServersOnMachine called via %s, noRestart %d", GetContextHowString(pContext), bNoRestart);


	if (pMachine)
	{
		int i;
		for (i = 0 ; i < GLOBALTYPE_MAXTYPES; i++)
		{
			//always have to kill launcher last, as it does the killing of other things
			if (i != GLOBALTYPE_LAUNCHER)
			{
				TrackedServerState *pServer = pMachine->pServersByType[i];

				while (pServer && pServer->pMachine == pMachine)
				{
					TrackedServerState *pNext = pServer->pNext;

					if (bNoRestart)
					{
						pServer->bKilledIntentionally = true;
					}

					KillServer(pServer, pCommentString);

					pServer = pNext;
				}
			}
		}

		if (!bExcludeLauncher)
		{
			TrackedServerState *pServer = pMachine->pServersByType[GLOBALTYPE_LAUNCHER];

			if (pServer)
			{
				KillServer(pServer, pCommentString);
			}
		}
	}

	estrDestroy(&pCommentString);
}


AUTO_COMMAND;
void GetDumpCmd(char *pServerType, ContainerID iID)
{
    GlobalType eType = NameToGlobalType(pServerType);

    if (eType)
    {
        TrackedServerState *pServer = FindServerFromID(eType, iID);

        if (pServer && pServer->PID && pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER] && pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink)
		{
			Packet *pPak;
			char *pDumpString = NULL;
			estrPrintf(&pDumpString, "%s_ServerMonRequest_%s_%u_%u",GlobalTypeToName(pServer->eContainerType), pServer->pMachine->machineName, pServer->iContainerID, timeSecondsSince2000());
			pPak = pktCreate(pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_GETAMANUALDUMP);
			pktSendBits(pPak, 32, pServer->PID);
			pktSendString(pPak, pDumpString);
			if (gServerTypeInfo[pServer->eContainerType].executableName64_original[0])
			{
				pktSendBits(pPak, 1, 0);
			}
			else
			{
				pktSendBits(pPak, 1, 1);
			}
			pktSend(&pPak);
			estrDestroy(&pDumpString);
		}                                           
    }
}


AUTO_COMMAND;
void ForgetServer(char *pServerType, ContainerID iID, char *pConfirm)
{
    GlobalType eType = NameToGlobalType(pServerType);

	if (stricmp(pConfirm, "yes") != 0)
	{
		return;
	}

	if (eType)
	{
		TrackedServerState *pServer = FindServerFromID(eType, iID);

		if (pServer)
		{
			StopTrackingServer(pServer, "ForgetServer", true, false);
		}
	}
}

AUTO_COMMAND;
void MuteAlerts(char *pServerType, ContainerID iID)
{
    GlobalType eType = NameToGlobalType(pServerType);

    if (eType)
    {
        TrackedServerState *pServer = FindServerFromID(eType, iID);

        if (pServer)
        {
            pServer->bAlertsMuted = true;                                
            estrClear(&pServer->pMuteAlerts);
            estrPrintf(&pServer->pUnMuteAlerts, "UnMuteAlerts %s %u $NOCONFIRM $NORETURN", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID);

        }
    }
}

AUTO_COMMAND;
void UnMuteAlerts(char *pServerType, ContainerID iID)
{
    GlobalType eType = NameToGlobalType(pServerType);

    if (eType)
    {
        TrackedServerState *pServer = FindServerFromID(eType, iID);

        if (pServer)
        {
            pServer->bAlertsMuted = false;
            estrClear(&pServer->pUnMuteAlerts);
            estrPrintf(&pServer->pMuteAlerts, "MuteAlerts %s %u $NOCONFIRM $NORETURN", GlobalTypeToName(pServer->eContainerType), pServer->iContainerID);

        }
    }
}

AUTO_STRUCT;
typedef struct ProcessOverview
{
    char pType[256];
    ContainerID iID;
    char stateString[256];
    U32 PID;
    ProcessPerformanceInfo performanceInfo; AST(EMBEDDED_FLAT)
	char mapName[64];
	int iNumPlayers;
} ProcessOverview;

AUTO_STRUCT;
typedef struct MachineServerTypePermissions
{
//            AST_COMMAND("Allow", "foo 0")
//            AST_COMMAND("Disallow", "foo 1")
    char *pAllow; AST(ESTRING FORMATSTRING(command = 1))
    char *pDisallow; AST(ESTRING FORMATSTRING(command = 1))
	char *pSetPriority; AST(ESTRING FORMATSTRING(command = 1))
    char *pStateString; AST(ESTRING)
} MachineServerTypePermissions;

AUTO_STRUCT;
typedef struct SystemSpecs_Subset
{
	char	cpuIdentifier[256];
	F32		CPUSpeed;
} SystemSpecs_Subset;


AUTO_STRUCT;
typedef struct MachineDescription
{
    AST_COMMAND("Broadcast a message", "BroadcastMessageToMachine $FIELD(MachineName_Internal) $STRING(Message to Broadcast)$NORETURN")
    AST_COMMAND("LockAllGameservers", "LockAllGameservers $FIELD(MachineName_Internal) $INT(1 to lock, 0 to unlock) $NORETURN")
    AST_COMMAND("Lock", "LockMachine $FIELD(MachineName_Internal) $INT(1 to lock, 0 to unlock) $NORETURN")
	AST_COMMAND("Turn on/off verbsoe proclist logging", "VerboseProcListLogging $FIELD(MachineName_Internal) $INT(1 to turn on, 0 off)")
	AST_COMMAND("Kill all servers", "KillAllServersOnMachine $INT(Exclude launcher) $INT(NoRestart) $FIELD(MachineName_Internal) $CONFIRM(Are you sure?)")
	
    char *pLaunchAServer; AST(ESTRING, FORMATSTRING(command=1))
	char *pVNC; AST(ESTRING, FORMATSTRING(HTML=1))

    char mainIP[16];
    char publicIP[16];
    char machineName[256]; AST(FORMATSTRING(HTML=1))
    char machineName_Internal[256]; AST(FORMATSTRING(HTML_SKIP=1))

	SystemSpecs_Subset specs; AST(FORMATSTRING(HTML_TABLE=1))
    TrackedPerformance performance; AST(FORMATSTRING(HTML_TABLE=1))

    ProcessOverview **ppProcesses;

    MachineServerTypePermissions **ppPermissions;
} MachineDescription;




bool ProcessMachineIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
    char *pFirstRightBracket;
    int iMachineNum;
    TrackedMachineState *pMachine;
    MachineDescription machineDescription = {0};
    bool bRetVal;
    int i;

    if (sscanf(pLocalXPath, "[%d]", &iMachineNum) != 1)
    {
		return false;
    }

    pFirstRightBracket = strchr(pLocalXPath, ']');

    pMachine = &gTrackedMachines[iMachineNum];

    strcpy(machineDescription.mainIP, makeIpStr(pMachine->IP));
    strcpy(machineDescription.publicIP, makeIpStr(pMachine->iPublicIP));
    sprintf(machineDescription.machineName, "<input type=\"hidden\" id=\"defaultautorefresh\" value=\"1\">%s", pMachine->machineName);
    sprintf(machineDescription.machineName_Internal, "%s", pMachine->machineName);
    StructCopyAll(parse_TrackedPerformance, &pMachine->performance, &machineDescription.performance);

	estrPrintf(&machineDescription.pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pMachine->VNCString);


    for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
    {
        TrackedServerState *pServer = pMachine->pServersByType[i];

        while (pServer && pServer->pMachine == pMachine)
        {
            ProcessOverview *pProcessOverview = StructCreate(parse_ProcessOverview);
            strcpy(pProcessOverview->pType, GlobalTypeToName(pServer->eContainerType));
            pProcessOverview->iID = pServer->iContainerID;
            pProcessOverview->PID = pServer->PID;
            strcpy_trunc(pProcessOverview->stateString, pServer->stateString);

            StructCopyAll(parse_ProcessPerformanceInfo, &pServer->perfInfo, &pProcessOverview->performanceInfo);

			if (pServer->pGameServerSpecificInfo)
			{
				pProcessOverview->iNumPlayers = pServer->pGameServerSpecificInfo->iNumPlayers;
				strcpy_trunc(pProcessOverview->mapName, pServer->pGameServerSpecificInfo->mapNameShort);
			}

            eaPush(&machineDescription.ppProcesses, pProcessOverview);

            pServer = pServer->pNext;
        }
    }

    for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
    {
        if (gServerTypeInfo[i].bCanBeManagedInPerMachinePermissions)
        {
            MachineServerTypePermissions *pPermissions = StructCreate(parse_MachineServerTypePermissions);

            switch (pMachine->canLaunchServerTypes[i].eCanLaunch)
            {
            case CAN_NOT_LAUNCH:
                estrPrintf(&pPermissions->pStateString, "%s: NOT allowed", GlobalTypeToName(i));
                estrPrintf(&pPermissions->pAllow, "SetMachineServerTypePermissions %d %s 1 $CONFIRM(Really allow servers of type %s on machine %s?) $NORETURN", iMachineNum, GlobalTypeToName(i), GlobalTypeToName(i), pMachine->machineName);
                break;
            case CAN_LAUNCH_DEFAULT:
                estrPrintf(&pPermissions->pStateString, "%s: allowed by default only because this is LocalHost", GlobalTypeToName(i));
                estrPrintf(&pPermissions->pAllow, "SetMachineServerTypePermissions %d %s 1 $CONFIRM(Really allow servers of type %s on machine %s?) $NORETURN", iMachineNum, GlobalTypeToName(i), GlobalTypeToName(i), pMachine->machineName);
                estrPrintf(&pPermissions->pDisallow, "SetMachineServerTypePermissions %d %s 0 $CONFIRM(Really disallow servers of type %s on machine %s?) $NORETURN", iMachineNum, GlobalTypeToName(i), GlobalTypeToName(i), pMachine->machineName);
                break;

            case CAN_LAUNCH_SPECIFIED:
				estrPrintf(&pPermissions->pStateString, "%s: ALLOWED", GlobalTypeToName(i));
				
				if (i != GLOBALTYPE_GAMESERVER)
				{
					estrPrintf(&pPermissions->pStateString, "%s: ALLOWED (pri: %d)", GlobalTypeToName(i), pMachine->canLaunchServerTypes[i].iPriority);
				}

                if (eaSize(&pMachine->canLaunchServerTypes[i].ppCategories))
                {
                    int j;

                    estrConcatf(&pPermissions->pStateString, "  Categories: ");
                    for (j=0; j < eaSize(&pMachine->canLaunchServerTypes[i].ppCategories); j++)
                    {
                        estrConcatf(&pPermissions->pStateString, "%s%s", j == 0 ? "" : ", ", pMachine->canLaunchServerTypes[i].ppCategories[j]);
                    }
                }
                estrPrintf(&pPermissions->pDisallow, "SetMachineServerTypePermissions %d %s 0 $CONFIRM(Really disallow servers of type %s on machine %s?) $NORETURN", iMachineNum, GlobalTypeToName(i), GlobalTypeToName(i), pMachine->machineName);
			
				if (i != GLOBALTYPE_GAMESERVER)
				{
					estrPrintf(&pPermissions->pSetPriority, "SetMachineServerTypePriority %d %s $INT(Set launch priority for server type %s on machine %s... higher means launch here first)", iMachineNum, GlobalTypeToName(i), GlobalTypeToName(i), pMachine->machineName);
				}
                break;
            }

            eaPush(&machineDescription.ppPermissions, pPermissions);
        }
    }

    estrCopy2(&machineDescription.pLaunchAServer,GetLaunchAServerCommand());

	if (pMachine->pSystemSpecs)
	{
		strcpy(machineDescription.specs.cpuIdentifier, pMachine->pSystemSpecs->cpuIdentifier);
		machineDescription.specs.CPUSpeed = pMachine->pSystemSpecs->CPUSpeed;
	}

    bRetVal =  ProcessStructIntoStructInfoForHttp(pFirstRightBracket ? pFirstRightBracket + 1 : "", pArgList,
		&machineDescription, parse_MachineDescription, iAccessLevel, 0, pStructInfo, eFlags);

    StructDeInit(parse_MachineDescription, &machineDescription);

    return bRetVal;
}

void LogControllerOverview(void)
{
    servLogWithStruct(LOG_CONTROLLER, "ControllerOverview", GetControllerOverview(true, ""), parse_ControllerOverview);
}

AUTO_COMMAND;
char *LaunchAServerThroughServerMonitoring(char *pMachineName, int iCount, char *pServerType, ACMD_SENTENCE pExtraCommandLine)
{
    TrackedMachineState *pMachine = FindMachineByName(pMachineName);
    GlobalType eGlobalType = NameToGlobalType(pServerType);
	int i;

	ANALYSIS_ASSUME(eGlobalType>=0 && eGlobalType < GLOBALTYPE_MAX);
	
	if (iCount == 0)
	{
		iCount = 1;
	}

    if (pMachine && eGlobalType)
    {
        if (pMachine->canLaunchServerTypes[eGlobalType].eCanLaunch == CAN_NOT_LAUNCH 
			|| pMachine->canLaunchServerTypes[eGlobalType].eCanLaunch == CAN_LAUNCH_DEFAULT)
        {
			pMachine->canLaunchServerTypes[eGlobalType].eCanLaunch = CAN_LAUNCH_SPECIFIED;
        }

		for (i = 0; i < iCount; i++)
		{
			if (!RegisterNewServerAndMaybeLaunch(0, VIS_UNSPEC, pMachine, eGlobalType, 
				GetFreeContainerID(eGlobalType), false, pExtraCommandLine, NULL, false, 0, NULL, "Requested via server monitoring"))
			{
				return "Couldn't create server";
			}
		}

        return "created";
    }

    return "Unknown machine or type";

}

AUTO_COMMAND;
void LockGameServer(ContainerID iID, int iLock)
{
    TrackedServerState *pServer = FindServerFromID(GLOBALTYPE_GAMESERVER, iID);
    if (!pServer)
    {
        return;
    }

    pServer->bLocked = iLock;
    RemoteCommand_LockGameServer(GLOBALTYPE_MAPMANAGER, 0, iID, iLock);
    RemoteCommand_LockGameServerRemotely(GLOBALTYPE_GAMESERVER, iID, iLock);
}

static void LockMachine_CB(TransactionReturnVal *returnVal, TrackedMachineState *pMachine)
{
	char *pRetString = NULL;
	

	switch(RemoteCommandCheck_LockAllGameserversOnOneMachine(returnVal, &pRetString))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		CRITICAL_NETOPS_ALERT("LOCKMACHINE_FAILED", "Tried to lock or unlock machine %s, but the command on the MapManager failed, with failure string %s",
			pMachine->machineName, GetTransactionFailureString(returnVal));
		return;

	case TRANSACTION_OUTCOME_SUCCESS:
		if (!strStartsWith(pRetString, "UNLOCK:"))
		{
			CRITICAL_NETOPS_ALERT("MACHINE_LOCKED", "%s",
				pRetString);
		}
		estrDestroy(&pRetString);
		return;
	}
}

AUTO_COMMAND;
void LockMachine(char *pMachineName, int iLock)
{
    TrackedMachineState *pMachine = FindMachineByName(pMachineName);
    if (pMachine)
    {
        pMachine->bIsLocked = iLock;
      
		RemoteCommand_LockAllGameserversOnOneMachine(objCreateManagedReturnVal(LockMachine_CB, pMachine),
			GLOBALTYPE_MAPMANAGER, 0, pMachineName, iLock);
    }
}

AUTO_COMMAND;
void LockAllGameServers(char *pMachineName, int iLock)
{
    TrackedMachineState *pMachine = FindMachineByName(pMachineName);
    if (pMachine)
    {
        TrackedServerState *pServer;

        pServer = pMachine->pServersByType[GLOBALTYPE_GAMESERVER];

        while (pServer && pServer->pMachine == pMachine)
        {
            LockGameServer(pServer->iContainerID, iLock);
            pServer = pServer->pNext;
        }
    }
}

void BootAllFromGameServerTimedCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{

    RemoteCommand_gslBootEveryone(GLOBALTYPE_GAMESERVER, (ContainerID)((intptr_t)userData), 0);


}


AUTO_COMMAND;
void ShutDownGameServer(ContainerID iID)
{
    TrackedServerState *pServer = FindServerFromID(GLOBALTYPE_GAMESERVER, iID);
    if (!pServer)
    {
        return;
    }
    //int objLog(enumLogCategory eCategory, GlobalType type, ContainerID id, const char *objName, Vec3 *pLocation, const char *objOwner, const char *action, const char *pProjSpecificObjInfoString, char const *fmt, ...)

    objLog(LOG_CONTROLLER, GLOBALTYPE_GAMESERVER, iID, 0, NULL, NULL, NULL, "GracefulShutDown", NULL, "Graceful shut down requested from server monitoring");

    LockGameServer(iID, 1);

    TimedCallback_Run(BootAllFromGameServerTimedCB, (void*)((intptr_t)iID), 1.0f);
    KillServer_Delayed(GLOBALTYPE_GAMESERVER, iID, 10.0f);

}

AUTO_STRUCT;
typedef struct ControllerServerTypeInfoForServerMon
{
    char *pTypeName; AST(ESTRING)
    char *pCurCommandLine; AST(ESTRING)
    bool bLeaveCrashesUpForever;
    AST_COMMAND("Set Leave Crashes Up Forever", "SetLeaveCrashesUpForeverFromServerMon $FIELD(TypeName) $INT(1 or 0)")
    AST_COMMAND("Send command to all", "SendCommandToAllFromServerMon $FIELD(TypeName) $STRING(Send what command to all servers of type $FIELD(TypeName))")
    AST_COMMAND("Set command line", "SetCommandLineFromServerMon $FIELD(TypeName) $STRING(Set shared command line for all servers of type $FIELD(TypeName))")
    AST_COMMAND("Kill all servers of type", "KillAllServersOfType $FIELD(TypeName) $STRING(Type yes if you reall want to kill all servers of type $FIELD(TypeName)) $STRING(Explain your actions here for the alert that will be sent out. You might want to tidy up your desk, also.)")
}
ControllerServerTypeInfoForServerMon;

AUTO_STRUCT;
typedef struct ControllerAllServerTypeInfoForServerMon
{
	char *pControllerCommandLine; AST(ESTRING)
	ControllerServerTypeInfoForServerMon **ppServerTypes;
} ControllerAllServerTypeInfoForServerMon;

ControllerAllServerTypeInfoForServerMon *GetAllServerTypeInfoForServerMon(void)
{
    static ControllerAllServerTypeInfoForServerMon info = {0};
    int i;

    StructDeInit(parse_ControllerAllServerTypeInfoForServerMon, &info);

	estrCopy2(&info.pControllerCommandLine, GetCommandLine());

    for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
    {
        if (gServerTypeInfo[i].bInUse && i != GLOBALTYPE_CONTROLLER)
        {
			ControllerServerTypeInfoForServerMon *pTypeInfo = StructCreate(parse_ControllerServerTypeInfoForServerMon);
			estrCopy2(&pTypeInfo->pTypeName, GlobalTypeToName(i));
			estrCopy2(&pTypeInfo->pCurCommandLine, gServerTypeInfo[i].pSharedCommandLine_FromScript ? gServerTypeInfo[i].pSharedCommandLine_FromScript : "");
			pTypeInfo->bLeaveCrashesUpForever = gServerTypeInfo[i].bLeaveCrashesUpForever;
			eaPush(&info.ppServerTypes, pTypeInfo);
		}
    }
    return &info;
}


bool GetAllServerTypeInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
    bool bRetVal;
    ControllerAllServerTypeInfoForServerMon *pInfo = GetAllServerTypeInfoForServerMon();

    bRetVal =  ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList,
        pInfo, parse_ControllerAllServerTypeInfoForServerMon, iAccessLevel, 0, pStructInfo, eFlags);

    return bRetVal;
}

AUTO_COMMAND;
void SetLeaveCrashesUpForeverFromServerMon(char *pTypeName, int iSet)
{
    GlobalType eType = NameToGlobalType(pTypeName);

    if (!eType)
    {
        return;
    }
    assert(eType > 0 && eType < GLOBALTYPE_MAXTYPES);

    gServerTypeInfo[eType].bLeaveCrashesUpForever = iSet;
    SendCommandToAllServersOfType(eType, 0, iSet ? "LeaveCrashesUpForever 1" : "LeaveCrashesUpForever 0");

}

AUTO_COMMAND;
void SendCommandToAllFromServerMon(char *pTypeName, ACMD_SENTENCE cmd)
{
    GlobalType eType = NameToGlobalType(pTypeName);

    if (!eType)
    {
        return;
    }
    assert(eType > 0 && eType < GLOBALTYPE_MAXTYPES);
    
    SendCommandToAllServersOfType(eType, 0, cmd);
}

AUTO_COMMAND;
void SetCommandLineFromServerMon(char *pTypeName, ACMD_SENTENCE cmdLine)
{
    GlobalType eType = NameToGlobalType(pTypeName);

    if (!eType)
    {
        return;
    }
    assert(eType > 0 && eType < GLOBALTYPE_MAXTYPES);

    estrCopy2(&gServerTypeInfo[eType].pSharedCommandLine_FromScript, cmdLine ? cmdLine : "");
}


AUTO_STRUCT;
typedef struct ControllerGameServerLaunchingOverview
{
    ControllerGameServerLaunchingConfig *pConfig;
    AST_COMMAND("Reload", "ReloadGameServerLaunchingOverview")
} ControllerGameServerLaunchingOverview;
           


bool GetGameServerLaunchingConfigForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
    bool bRetVal;
    ControllerGameServerLaunchingOverview info = {0};
    info.pConfig = &gGameServerLaunchingConfig;

    bRetVal =  ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList,
        &info, parse_ControllerGameServerLaunchingOverview, iAccessLevel, 0, pStructInfo, eFlags);

    return bRetVal;
}

bool GetUGCOverviewForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
    bool bRetVal;
    ControllerUGCOverview *pOverview = GetControllerUGCOverview(urlFindSafeValue(pArgList, "__AUTH"));

    bRetVal =  ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList,
        pOverview, parse_ControllerUGCOverview, iAccessLevel, 0, pStructInfo, eFlags);

    return bRetVal;
}

typedef struct ShardVariableContainerHolder
{
	REF_TO(ShardVariableContainer) hShardVariableContainer;
}  ShardVariableContainerHolder;

static ShardVariableContainerHolder sSVCHolder = {0};

AUTO_STRUCT;
typedef struct ShardVariableOverview
{
	char *pName;
	char *pValue; AST(ESTRING)
	bool bIsDefault;
	const char *pType; AST(POOL_STRING)
	char *pResetToDefault; AST(ESTRING, FORMATSTRING(command=1))
	char *pSet; AST(ESTRING, FORMATSTRING(command=1))
} ShardVariableOverview;

AUTO_STRUCT;
typedef struct ShardVariablesListOverview
{
	ShardVariableOverview **ppShardVariables;
} ShardVariablesListOverview;

ShardVariablesListOverview *GetShardVariableListOverviewFromContainer(ShardVariableContainer *pContainer)
{
	static ShardVariablesListOverview *pListOverview = NULL;
	const char ***pppShardVariableNames;
	int i;

	if (pListOverview)
	{
		StructReset(parse_ShardVariablesListOverview, pListOverview);
	}
	else
	{
		pListOverview = StructCreate(parse_ShardVariablesListOverview);
	}

	pppShardVariableNames = shardvariable_GetShardVariableNames();

	for (i=0; i < eaSize(pppShardVariableNames); i++)
	{
		ShardVariableOverview *pOverview = StructCreate(parse_ShardVariableOverview);
		WorldVariableContainer *pWVContainer;
		
		pOverview->pName = strdup((*pppShardVariableNames)[i]);
		
		pWVContainer = eaIndexedGetUsingString(&pContainer->eaWorldVars, pOverview->pName);
		if (pWVContainer)
		{
			WorldVariable *pWV;
			pWV = StructCreate(parse_WorldVariable);
			worldVariableCopyFromContainer(pWV, pWVContainer);
			worldVariableToEString(pWV, &pOverview->pValue);
			pOverview->pType = worldVariableTypeToString(pWV->eType);
			StructDestroy(parse_WorldVariable, pWV);
			estrPrintf(&pOverview->pResetToDefault, "ResetShardVarToDefault %s $CONFIRM(This will reset variable %s to its default value. This is a slow command involving remote commands, transactions and subscriptions, so may take a while to execute)",
				pOverview->pName, pOverview->pName);
		}
		else
		{
			const WorldVariable *pWV;
			pWV = shardvariable_GetDefaultValue(pOverview->pName);
			if (pWV)
			{
				worldVariableToEString(pWV, &pOverview->pValue);
				pOverview->pType = worldVariableTypeToString(pWV->eType);
				pOverview->bIsDefault = true;
			}
			else
			{
				estrPrintf(&pOverview->pValue, "(BAD SHARD VARIABLE)");
			}
		}

		estrPrintf(&pOverview->pSet, "SetShardVar %s $STRING(New value) $CONFIRM(This will set variable %s the specified value. This is a slow command involving remote commands, transactions and subscriptions, so may take a while to execute)",
			pOverview->pName, pOverview->pName);



		eaPush(&pListOverview->ppShardVariables, pOverview);
	}



	return pListOverview;
}

AUTO_COMMAND;
char *ResetShardVarToDefault(char *pVarName)
{
	if (!gpServersByType[GLOBALTYPE_WEBREQUESTSERVER])
	{
		return "There is no WebRequestServer running, can't set Shard Variables";
	}

	RemoteCommand_ResetShardVariable(GLOBALTYPE_WEBREQUESTSERVER, 0, pVarName);

	return "Request submitted to WebRequestServer";

}

AUTO_COMMAND;
char *SetShardVar(char *pVarName, ACMD_SENTENCE pNewValue)
{
	if (!gpServersByType[GLOBALTYPE_WEBREQUESTSERVER])
	{
		return "There is no WebRequestServer running, can't set Shard Variables";
	}

	RemoteCommand_SetShardVariable(GLOBALTYPE_WEBREQUESTSERVER, 0, pVarName, pNewValue);

	return "Request submitted to WebRequestServer";


}

ShardVariableContainer *Controller_GetShardVariableContainer(bool *pbAlreadyWaiting)
{
	ShardVariableContainer *pContainer;
	pContainer = GET_REF(sSVCHolder.hShardVariableContainer);

	if (pContainer)
	{
		return pContainer;
	}

	if (REF_IS_SET_BUT_ABSENT(sSVCHolder.hShardVariableContainer))
	{
		*pbAlreadyWaiting = true;
		return NULL;
	}


	objRegisterNativeSchema(GLOBALTYPE_SHARDVARIABLE, parse_ShardVariableContainer, NULL, NULL, NULL, NULL, NULL);
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_SHARDVARIABLE), false, parse_ShardVariableContainer, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_SHARDVARIABLE), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_SHARDVARIABLE));
	SET_HANDLE_FROM_REFDATA(GlobalTypeToCopyDictionaryName(GLOBALTYPE_SHARDVARIABLE), "1", sSVCHolder.hShardVariableContainer);

	*pbAlreadyWaiting = false;
	return NULL;
}

bool ProcessShardVariablesIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	ShardVariableContainer *pContainer;
	bool bAlreadyRequested;
	if (!objLocalManager())
	{
		GetMessageForHttpXpath("Controller not connected to trans server yet, can't get shard variables", pStructInfo, 1);
		return true;
	}

	pContainer = Controller_GetShardVariableContainer(&bAlreadyRequested);

	if (pContainer)
	{
		ShardVariablesListOverview *pListOverview = GetShardVariableListOverviewFromContainer(pContainer);
		return ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList,
			pListOverview, parse_ShardVariablesListOverview, iAccessLevel, 0, pStructInfo, eFlags);		
	}
	else if (bAlreadyRequested)
	{
		GetMessageForHttpXpath("Waiting for shard variables", pStructInfo, 1);
	}
	else
	{
		GetMessageForHttpXpath("Requested shard variables, this page should populate shortly", pStructInfo, 1);
	}



	return true;
/*
                char *pFirstRightBracket;
                int iMachineNum;
                TrackedMachineState *pMachine;
                MachineDescription machineDescription = {0};
                bool bRetVal;
                int i;

                if (sscanf(pLocalXPath, "[%d]", &iMachineNum) != 1)
                {
                                return false;
                }

                pFirstRightBracket = strchr(pLocalXPath, ']');

                pMachine = &gTrackedMachines[iMachineNum];

                strcpy(machineDescription.mainIP, makeIpStr(pMachine->IP));
                strcpy(machineDescription.publicIP, makeIpStr(pMachine->iPublicIP));
                sprintf(machineDescription.machineName, "<input type=\"hidden\" id=\"defaultautorefresh\" value=\"1\">%s", pMachine->machineName);
                sprintf(machineDescription.machineName_Internal, "%s", pMachine->machineName);
                StructCopyAll(parse_TrackedPerformance, &pMachine->performance, &machineDescription.performance);

                for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
                {
                                TrackedServerState *pServer = pMachine->pServersByType[i];

                                while (pServer && pServer->pMachine == pMachine)
                                {
                                                ProcessOverview *pProcessOverview = StructCreate(parse_ProcessOverview);
                                                strcpy(pProcessOverview->pType, GlobalTypeToName(pServer->eContainerType));
                                                pProcessOverview->iID = pServer->iContainerID;
                                                pProcessOverview->PID = pServer->PID;
                                                strcpy(pProcessOverview->stateString, pServer->stateString);

                                                StructCopyAll(parse_ProcessPerformanceInfo, &pServer->perfInfo, &pProcessOverview->performanceInfo);

                                                eaPush(&machineDescription.ppProcesses, pProcessOverview);

                                                pServer = pServer->pNext;
                                }
                }

                for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
                {
                                if (gServerTypeInfo[i].bCanBeManagedInPerMachinePermissions)
                                {
                                                MachineServerTypePermissions *pPermissions = StructCreate(parse_MachineServerTypePermissions);

                                                switch (pMachine->canLaunchServerTypes[i].eCanLaunch)
                                                {
                                                case CAN_NOT_LAUNCH:
                                                                estrPrintf(&pPermissions->pStateString, "%s: NOT allowed", GlobalTypeToName(i));
                                                                estrPrintf(&pPermissions->pAllow, "SetMachineServerTypePermissions %d %s 1 $CONFIRM(Really allow servers of type %s on machine %s?) $NORETURN", iMachineNum, GlobalTypeToName(i), GlobalTypeToName(i), pMachine->machineName);
                                                                break;
                                                case CAN_LAUNCH_DEFAULT:
                                                                estrPrintf(&pPermissions->pStateString, "%s: allowed by default only because this is LocalHost", GlobalTypeToName(i));
                                                                estrPrintf(&pPermissions->pAllow, "SetMachineServerTypePermissions %d %s 1 $CONFIRM(Really allow servers of type %s on machine %s?) $NORETURN", iMachineNum, GlobalTypeToName(i), GlobalTypeToName(i), pMachine->machineName);
                                                                estrPrintf(&pPermissions->pDisallow, "SetMachineServerTypePermissions %d %s 0 $CONFIRM(Really disallow servers of type %s on machine %s?) $NORETURN", iMachineNum, GlobalTypeToName(i), GlobalTypeToName(i), pMachine->machineName);
                                                                break;

                                                case CAN_LAUNCH_SPECIFIED:
                                                                estrPrintf(&pPermissions->pStateString, "%s: ALLOWED", GlobalTypeToName(i));

                                                                if (eaSize(&pMachine->canLaunchServerTypes[i].ppCategories))
                                                                {
                                                                                int j;

                                                                                estrConcatf(&pPermissions->pStateString, "  Categories: ");
                                                                                for (j=0; j < eaSize(&pMachine->canLaunchServerTypes[i].ppCategories); j++)
                                                                                {
                                                                                                estrConcatf(&pPermissions->pStateString, "%s%s", j == 0 ? "" : ", ", pMachine->canLaunchServerTypes[i].ppCategories[j]);
                                                                                }
                                                                }
                                                                estrPrintf(&pPermissions->pDisallow, "SetMachineServerTypePermissions %d %s 0 $CONFIRM(Really disallow servers of type %s on machine %s?) $NORETURN", iMachineNum, GlobalTypeToName(i), GlobalTypeToName(i), pMachine->machineName);
                                                                break;
                                                }

                                                eaPush(&machineDescription.ppPermissions, pPermissions);
                                }
                }

                estrCopy2(&machineDescription.pLaunchAServer,GetLaunchAServerCommand());

                bRetVal =  ProcessStructIntoStructInfoForHttp(pFirstRightBracket ? pFirstRightBracket + 1 : "", pArgList,
                                &machineDescription, parse_MachineDescription, iAccessLevel, pStructInfo);

                StructDeInit(parse_MachineDescription, &machineDescription);

                return bRetVal;*/
}



AUTO_RUN;
void intDomains(void)
{
    RegisterCustomXPathDomain(".machine", ProcessMachineIntoStructInfoForHttp, NULL);
    RegisterCustomXPathDomain(".serverTypeInfo", GetAllServerTypeInfoForHttp, NULL);
    RegisterCustomXPathDomain(".gameServerLaunching", GetGameServerLaunchingConfigForHttp, NULL);
	RegisterCustomXPathDomain(".ShardVariables", ProcessShardVariablesIntoStructInfoForHttp, NULL);
	RegisterCustomXPathDomain(".UGCOverview", GetUGCOverviewForHttp, NULL);
}

AUTO_COMMAND;
char *KillAllServersOfType(char *pTypeName, char *pYes, ACMD_SENTENCE pReason)
{
    GlobalType eType = NameToGlobalType(pTypeName);
    static char *pRetVal = NULL;
    int iCount = 0;
    TrackedServerState *pServer;

    if (!eType)
    {
        estrPrintf(&pRetVal, "didn't recognize type name %s", pTypeName);
        return pRetVal;
    }

    assert(eType > 0 && eType < GLOBALTYPE_MAXTYPES);

    if (stricmp(pYes, "yes") != 0)
    {
        estrPrintf(&pRetVal, "You didn't type \"yes\"");
        return pRetVal;
    }

    estrPrintf(&pRetVal, "Someone has requested the killing of all servers of type %s through servermonitoring because: \"%s\"",
        pTypeName, pReason);
    TriggerAlert("KILLING_SVRS_OF_TYPE", pRetVal, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(),
        eType, 0, getHostName(), 0);

    pServer = gpServersByType[eType];

    while (pServer)
    {
    }

    estrPrintf(&pRetVal, "Killed %d servers", iCount);

    return pRetVal;

                

}

void OVERRIDE_LATELINK_FlushAuthCache_DoExtraStuff(CmdContext *pContext)
{
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_MASTERCONTROLPROGRAM];

	while (pServer)
	{
		if (pServer->pLink)
		{
			Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_REQUESTING_MONITORING_COMMAND);
			pktSendBits(pPak, 32, 0);
			pktSendBits(pPak, 32, 0);
			PutContainerIDIntoPacket(pPak, 0);
			pktSendString(pPak, "FlushHttpAuthenticationCache");
			pktSendString(pPak, pContext->pAuthNameAndIP ? pContext->pAuthNameAndIP : "");
			pktSendBits(pPak, 32, pContext->access_level);
			pktSendBits(pPak, 1, true);
			pktSend(&pPak);
		}

		pServer = pServer->pNext;
	}
}

void Controller_SetServerMonitorBannerString(const char *pString)
{
	if (pString)
	{
		estrCopy2(&spBannerString, pString);
	}
	else
	{
		estrDestroy(&spBannerString);
	}
}



#include "autogen/controller_http_c_ast.c"
