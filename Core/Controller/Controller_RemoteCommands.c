#include "Controller.h"
#include "controllerpub_h_ast.h"
#include "serverlib.h"
#include "Autogen/svrGlobalInfo_h_ast.h"
#include "../../CrossRoads/common/autogen/GameServerLib_autogen_RemoteFuncs.h"
#include "../../CrossRoads/common/autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "../common/autogen/ServerLib_autogen_RemoteFuncs.h"
#include "../Common/autogen/ChatServer_autogen_RemoteFuncs.h"
#include "Sock.h"
#include "LogComm.h"
#include "alerts.h"
#include "httpXpathSupport.h"
#include "file.h"
#include "RemoteCommandGroup.h"
#include "RemoteCommandGroup_h_ast.h"
#include "Controller_RemoteCommands_c_ast.h"
#include "TimedCallback.h"
#include "resourceInfo.h"
#include "Controller_DynHoggPruning.h"
#include "controller_utils.h"
#include "MemLeakTracking.h"
#include "NotifyEnum.h"
#include "Controller_AutoSettings.h"
#include "stringUtil.h"
#include "Controller_ShardCluster.h"


AUTO_COMMAND_REMOTE;
Controller_ServerList *GetServerListEx(int iServerTypeToMatch, char *pStateToMatch)
{
	Controller_ServerList *pList;
	Controller_SingleServerInfo *pSingleServerInfo;
	TrackedServerState *pServer = gpServersByType[iServerTypeToMatch];

	pList = StructCreate(parse_Controller_ServerList);

	while (pServer)
	{
	
		if (!pStateToMatch || !pStateToMatch[0] || strstri(pServer->stateString, pStateToMatch))
		{
			pSingleServerInfo = StructAlloc(parse_Controller_SingleServerInfo);

			pSingleServerInfo->eServerType = pServer->eContainerType;
			pSingleServerInfo->iGlobalID = pServer->iContainerID;
			pSingleServerInfo->iIP = pServer->pMachine->IP;
			pSingleServerInfo->iPublicIP = pServer->pMachine->iPublicIP;
			pSingleServerInfo->pid = pServer->PID;
			strcpy_trunc(pSingleServerInfo->machineName, pServer->pMachine->machineName);
			strcpy_trunc(pSingleServerInfo->stateString, pServer->stateString);

			eaPush(&pList->ppServers, pSingleServerInfo);
		}

		pServer = pServer->pNext;
	}
	
	
	return pList;
	
}


AUTO_COMMAND_REMOTE;
Controller_ServerList *GetServerList(int iServerTypeToMatch)
{
	return GetServerListEx(iServerTypeToMatch, NULL);
}





//starts a requested server, returns information about it. If it couldn't be started, the returned IP is 0
//
//pCategory is a string describing this server's "category", meaningful currently only for gameservers
AUTO_COMMAND_REMOTE;
Controller_SingleServerInfo *StartServer(GlobalType eGlobalType, U32 iContainerID, char *pCategory, char *commandLine, char *pReason, AdditionalServerLaunchInfo *pAdditionalInfo,
	ServerLaunchDebugNotificationInfo *pDebugInfo, RemoteCommandGroup *pWhatToDoOnCrash, RemoteCommandGroup *pWhatToDoOnAnyClose)
{
	Controller_SingleServerInfo *pRetVal = StructCreate(parse_Controller_SingleServerInfo);
	TrackedMachineState *pMachine;
	TrackedServerState *pLauncherToUse;
	char *pFixedUpCommandLine = NULL;
	TrackedServerState *pServer;

	pMachine = FindDefaultMachineForType(eGlobalType, pCategory, pAdditionalInfo);
	pLauncherToUse = pMachine ? pMachine->pServersByType[GLOBALTYPE_LAUNCHER] : NULL;
		
	if (!pLauncherToUse)
	{
		pRetVal->iIP = 0;

		if (eGlobalType == GLOBALTYPE_GAMESERVER)
		{
			AssertOrAlert("CANT_CREATE_GAMESERVER", "All GS machines have free RAM and free cpu below limits specified in GSLaunchConfig in ControllerAutoSettings. Refusing GS launch request.");
			pRetVal->eFailureType = FAILURE_NO_MACHINE_FOR_GAME_SERVER;
		}

		return pRetVal;
	}

	if (iContainerID && FindServerFromID(eGlobalType, iContainerID))
	{
		pRetVal->iIP = 0;
		return pRetVal;
	}

	estrStackCreate(&pFixedUpCommandLine);
	estrCopy2(&pFixedUpCommandLine, commandLine);

	if (gpServersByType[GLOBALTYPE_LOGINSERVER])
	{
		estrReplaceOccurrences(&pFixedUpCommandLine, "$LOGINSERVERIP", makeIpStr(FindRandomServerOfType(GLOBALTYPE_LOGINSERVER)->pMachine->iPublicIP));
	}

	if (!iContainerID)
	{
		iContainerID = GetFreeContainerID(eGlobalType);
	}



	pServer = RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, VIS_UNSPEC, pLauncherToUse->pMachine, eGlobalType, 
		iContainerID, (pLauncherToUse->pMachine->bIsLocalHost && eGlobalType == GLOBALTYPE_GAMESERVER) ? gbStartGameServersInDebugger : false, pFixedUpCommandLine, NULL,
		false, 0, pAdditionalInfo, "%s", pReason);

	pRetVal->eServerType = eGlobalType;
	pRetVal->iGlobalID = iContainerID;
	pRetVal->iIP = pLauncherToUse->pMachine->IP;
	pRetVal->iPublicIP = pLauncherToUse->pMachine->iPublicIP;
	strcpy(pRetVal->machineName, pLauncherToUse->pMachine->machineName);
	estrDestroy(&pFixedUpCommandLine);

	if (pDebugInfo)
	{
		pServer->pLaunchDebugNotification = StructClone(parse_ServerLaunchDebugNotificationInfo, pDebugInfo);
	}

	if (pWhatToDoOnCrash)
	{
		eaPush(&pServer->ppThingsToDoOnCrash, StructClone(parse_RemoteCommandGroup, pWhatToDoOnCrash));
	}

	if (pWhatToDoOnAnyClose)
	{
		eaPush(&pServer->ppThingsToDoOnAnyClose, StructClone(parse_RemoteCommandGroup, pWhatToDoOnAnyClose));
	}
	return pRetVal;
}

AUTO_COMMAND_REMOTE;
void StartServer_NoReturn(int eGlobalType, U32 iContainerID, char *commandLine, char *pReason, AdditionalServerLaunchInfo *pAdditionalInfo)
{
	TrackedMachineState *pMachine = FindDefaultMachineForType(eGlobalType, NULL, pAdditionalInfo);
	TrackedServerState *pLauncherToUse;
	char *pFixedUpCommandLine = NULL;

	if (!pMachine)
	{
		return;
	}

	pLauncherToUse = pMachine->pServersByType[GLOBALTYPE_LAUNCHER];
	
	if (!pLauncherToUse)
	{
		return;
	}

	if (iContainerID && FindServerFromID(eGlobalType, iContainerID))
	{

		return;
	}

	estrStackCreate(&pFixedUpCommandLine);
	estrCopy2(&pFixedUpCommandLine, commandLine);

	if (gpServersByType[GLOBALTYPE_LOGINSERVER])
	{
		estrReplaceOccurrences(&pFixedUpCommandLine, "$LOGINSERVERIP", makeIpStr(FindRandomServerOfType(GLOBALTYPE_LOGINSERVER)->pMachine->iPublicIP));
	}

	RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, VIS_UNSPEC, pLauncherToUse->pMachine, eGlobalType, 
		iContainerID ? iContainerID : GetFreeContainerID(eGlobalType), (pLauncherToUse->pMachine->bIsLocalHost && eGlobalType == GLOBALTYPE_GAMESERVER) ? gbStartGameServersInDebugger : false, pFixedUpCommandLine, NULL, false, 0, pAdditionalInfo, "%s", pReason);

	estrDestroy(&pFixedUpCommandLine);

}

//servers use this to inform the controller of their state
//
//this is also called internally on receipt of TO_CONTROLLER_SETTING_SERVER_STATE for 
//servers that connect directly to the controller, and thus can not use AUTO_COMMANDs
AUTO_COMMAND_REMOTE;
void InformControllerOfServerState(int eGlobalType, U32 iContainerID, char *pStateString)
{
	TrackedServerState *pServer = FindServerFromID(eGlobalType, iContainerID);

	if (!pServer)
	{
		printf("WARNING: Nonexistent server %s %d changing to state %s\n",
			GlobalTypeToName(eGlobalType), iContainerID, pStateString);
		return;
	}

	InformControllerOfServerState_Internal(pServer, pStateString);
}

void InformControllerOfServerState_Internal(TrackedServerState *pServer, char *pStateString)
{

	if (stricmp(pServer->stateString, pStateString) == 0)
	{
		return;
	}

	SimpleServerLog_AddUpdate(pServer, "New state: %s", pStateString);

	//if something is setting its state, it must be alive
	pServer->perfInfo.iLastContactTime = timeSecondsSince2000();


	//just in case the gameserver ever switches OUT of monitoring wait state for some reason, re-add the launch weight. This
	//should only happen during weird corner cases or something, and is so that the code that subtracts it again when
	//the server is removed won't double-subtract
	if (pServer->eContainerType == GLOBALTYPE_GAMESERVER && strstr(pServer->stateString, gServerTypeInfo[GLOBALTYPE_GAMESERVER].monitoringWaitState))
	{
		pServer->pMachine->iGameServerLaunchWeight += pServer->additionalServerLaunchInfo.iGameServerLaunchWeight;
	}

	if ((isProductionMode() || gbTrackMemLeaksInDevMode) && gServerTypeInfo[pServer->eContainerType].beginMemLeakTrackingState[0] &&
		strstr(pStateString, gServerTypeInfo[pServer->eContainerType].beginMemLeakTrackingState))
	{
		char *pCmdString = NULL;

		if (!(gServerTypeInfo[pServer->eContainerType].iMemLeakTracking_BeginDelay
			&& gServerTypeInfo[pServer->eContainerType].iMemLeakTracking_Frequency
			&& gServerTypeInfo[pServer->eContainerType].iMemLeakTracking_IncreaseAmountThatIsALeak_Megs))
		{
			AssertOrAlert("BAD_MEMLEAK_CONFIG", "For server type %s, want to do mem leak tracking on state %s, but one of BeginDelay (%d), TrackingFrequence(%d) or IncreaseAmount (%d) is not set",
				GlobalTypeToName(pServer->eContainerType),  gServerTypeInfo[pServer->eContainerType].beginMemLeakTrackingState,
				gServerTypeInfo[pServer->eContainerType].iMemLeakTracking_BeginDelay,
				gServerTypeInfo[pServer->eContainerType].iMemLeakTracking_Frequency,
				gServerTypeInfo[pServer->eContainerType].iMemLeakTracking_IncreaseAmountThatIsALeak_Megs);

		}
		else
		{
			estrPrintf(&pCmdString, "BeginMemLeakTracking %d %d %d %d",
				gServerTypeInfo[pServer->eContainerType].iMemLeakTracking_BeginDelay,
				gServerTypeInfo[pServer->eContainerType].iMemLeakTracking_Frequency,
				gServerTypeInfo[pServer->eContainerType].iMemLeakTracking_IncreaseAmountThatIsALeak_Megs,
				gServerTypeInfo[pServer->eContainerType].iMemLeakTracking_FirstIncreaseAllowance_Megs);

			if (pServer->pLink)
			{
				SendCommandDirectlyToServerThroughNetLink(pServer, pCmdString);
			}
			else
			{
				RemoteCommand_CallLocalCommandRemotely(pServer->eContainerType, pServer->iContainerID, pCmdString);
			}

			estrDestroy(&pCmdString);
		}
	}

	strcpy_trunc(pServer->stateString, pStateString);

	if (pServer->eContainerType != GLOBALTYPE_GAMESERVER && pServer->eContainerType != GLOBALTYPE_TESTCLIENT)
	{
		printf("Server %s %d now in state %s\n",
			GlobalTypeToName(pServer->eContainerType), pServer->iContainerID, pStateString);
	}

	
	//special case for log server
	if (pServer->eContainerType == GLOBALTYPE_LOGSERVER && strstr(pStateString, "ready"))
	{
		static bool sbFirstLogServer = true;
		if (sbFirstLogServer)
		{
			sbFirstLogServer = false;
			printf("First Log server is now alive... will try to connect\n");
			//on single-machine shards, we sometimes get this message before we actually have the machine name... don't send "UNKNOWN" in that case
			strcpy(gServerLibState.logServerHost, pServer->pMachine->bIsLocalHost ? "localhost" : pServer->pMachine->machineName);
			svrLogInit();

			Controller_LogServerNowSetAndActive(pServer->pMachine->machineName);
		}
		else
		//if this isn't the first log server that has ever existed, inform all muliplexers, so that they can reconnect
		{
			TrackedServerState *pMultiplexer = gpServersByType[GLOBALTYPE_MULTIPLEXER];
			while (pMultiplexer)
			{
				if (pMultiplexer->pLink)
				{
					Packet *pPkt = pktCreate(pMultiplexer->pLink, FROM_CONTROLLER__SERVERSPECIFIC__SERVER_RESTARTED);
					PutContainerTypeIntoPacket(pPkt, GLOBALTYPE_LOGSERVER);
					pktSend(&pPkt);
				}

				pMultiplexer = pMultiplexer->pNext;
			}
		}
		
	}
	else if (pServer->eContainerType == GLOBALTYPE_GAMESERVER)
	{
		if (strstr(pStateString, gServerTypeInfo[GLOBALTYPE_GAMESERVER].monitoringWaitState))
		{
			if (Controller_DoingLogServerStressTest())
			{
				RemoteCommand_BeginLogServerStressTestMode(GLOBALTYPE_GAMESERVER, pServer->iContainerID, Controller_GetLogServerStressTestLogsPerServerPerSecond());
			}

			pServer->pMachine->iGameServerLaunchWeight -= pServer->additionalServerLaunchInfo.iGameServerLaunchWeight;

			RemoteCommand_SetGSLBannedCommands(GLOBALTYPE_GAMESERVER, pServer->iContainerID, gpGSLBannedCommandsString ? gpGSLBannedCommandsString : "");
		}
	}


	if (isProductionMode() && pServer->iNextExpectedKeepAliveTime == 0 && gServerTypeInfo[pServer->eContainerType].iKeepAliveDelay
		&& (!gServerTypeInfo[pServer->eContainerType].keepAliveBeginState || strstri(pStateString, gServerTypeInfo[pServer->eContainerType].keepAliveBeginState)))
	{
		BeginKeepAlive(pServer);
	}

	//if this server is one that cares about shard cluster updates, and is now in the state where it can receive them,
	//send it one
	if (pStateString && gServerTypeInfo[pServer->eContainerType].pInformMeAboutShardCluster_State &&
		strstri(pStateString, gServerTypeInfo[pServer->eContainerType].pInformMeAboutShardCluster_State))
	{
		pServer->bBeganInformingAboutShardCluster = true;
		ControllerShardCluster_InformServer(pServer);
	}

	//if this server is one that other shards want to know info about, update other shards
	if (gServerTypeInfo[pServer->eContainerType].bPutInfoAboutMeInShardClusterSummary)
	{
		ControllerShardCluster_SomethingLocalChanged();
	}

	if (pServer->eContainerType == GLOBALTYPE_OBJECTDB && ControllerScripting_IsRunning())
	{
		if (strstri(pStateString, "HandleRequests"))
		{
			Controller_SetStartupStatusString("ObjectDB", NULL);
		}
		else
		{
			Controller_SetStartupStatusString("ObjectDB", "Now in state %s", pStateString);
		}
	}
}

// Command that is useful during development, that kills all servers and the controller itself
AUTO_COMMAND_REMOTE;
void KillAllServersAndSelf(void)
{
	// This will cause everything else to die
	svrExit(0);
}

AUTO_COMMAND_REMOTE;
void SendErrorDialogToMCPThroughController(char *str, char* title, char* fault, int highlight)
{
	if (ControllerScripting_IsRunning())
	{
		char logString[4096];

		sprintf(logString, "Error in step %s:\n%s\n", ControllerScripting_GetCurStepName(), str);

		ControllerScripting_LogString(logString, false, true);
	}
	else
	{
		TrackedServerState *pServer = gpServersByType[GLOBALTYPE_MASTERCONTROLPROGRAM];

		while (pServer)
		{
		
			Packet *pPak;
			
			pktCreateWithCachedTracker(pPak, pServer->pLink, FROM_CONTROLLER_HERE_IS_ERROR_DIALOG_FOR_MCP);

			pktSendString(pPak, str);
			pktSendString(pPak, title);
			pktSendString(pPak, fault);
			pktSendBitsPack(pPak, 32, highlight);

			pktSend(&pPak);

			pServer = pServer->pNext;
		}
	}

}

//used for launching things from the MCP monitoring window, not for serious use
AUTO_COMMAND;
void LaunchServerOnLauncher(U32 iServerType, U32 iLauncherID)
{	
	TrackedServerState *pLauncher = FindServerFromID(GLOBALTYPE_LAUNCHER, iLauncherID);

	if (pLauncher)
	{
		RegisterNewServerAndMaybeLaunch(0, VIS_UNSPEC, pLauncher->pMachine, iServerType, 
			GetFreeContainerID(iServerType), false, "", NULL, false, 0, NULL, "Launched through server monitor (LaunchServerOnLauncher)");
	}
}

AUTO_COMMAND;
void SetLauncherCommandLine(ACMD_SENTENCE pString)
{
	if (pString && pString[0])
	{
		if (pLauncherCommandLine)
		{
			free(pLauncherCommandLine);
		}

		pLauncherCommandLine = strdup(pString);
	}
}



AUTO_COMMAND_REMOTE;
void SetControllerScriptingOutcome(int eGlobalTypeDoingSetting, U32 iContainerIDDoingSetting, int iOutCome, char *pResultString)
{
	TrackedServerState *pServerDoingSetting;

	if (eGlobalTypeDoingSetting == GLOBALTYPE_CONTROLLER)
	{
		Errorf("Controller is setting scripting outcome... something weird is going on...");
		return;
	}

	pServerDoingSetting = FindServerFromID(eGlobalTypeDoingSetting, iContainerIDDoingSetting);

	assertmsgf(pServerDoingSetting, "Unknown server %s(%d) setting scripting outcome", 
		GlobalTypeToName(eGlobalTypeDoingSetting), iContainerIDDoingSetting);
	assertmsgf(iOutCome == 1 || iOutCome == -1, "Only legal values for SetControllerScriptingOutcome are 1 or -1");

	pServerDoingSetting->iControllerScriptingCommandStepResult = iOutCome;

	strcpy(pServerDoingSetting->controllerScriptingCommandStepResultString, pResultString);

	printf("Scripting result %s(%d) from server %s(%d)\n", 
		pResultString, iOutCome, GlobalTypeToName(eGlobalTypeDoingSetting), iContainerIDDoingSetting);


}

static bool sbLogAllXpathsSentToMCP = false;
AUTO_CMD_INT(sbLogAllXpathsSentToMCP, LogAllXpathsSentToMCP);

AUTO_COMMAND_REMOTE;
void ReturnXPathForHttp(GlobalType eTypeOfProvidingServer, ContainerID eIDOfProvidingServer, int iRequestID, ContainerID iMCPContainerID, StructInfoForHttpXpath *pStructInfo)
{
	TrackedServerState *pMCP = FindServerFromID(GLOBALTYPE_MASTERCONTROLPROGRAM, iMCPContainerID);
	if (pMCP)
	{
		Packet *pPak;
		
		if (sbLogAllXpathsSentToMCP)
		{
			objLogWithStruct(LOG_SERVERMONITOR, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, pStructInfo, parse_StructInfoForHttpXpath);
		}

		pktCreateWithCachedTracker(pPak, pMCP->pLink, FROM_CONTROLLER_HERE_IS_XPATH_HTTP_FOR_MCP);

		PutContainerTypeIntoPacket(pPak, eTypeOfProvidingServer);
		PutContainerIDIntoPacket(pPak, eIDOfProvidingServer);



		pktSendBits(pPak, 32, iRequestID);
		pktSendStruct(pPak, pStructInfo, parse_StructInfoForHttpXpath);

		giServerMonitoringBytes += pktGetSize(pPak);
		pktSend(&pPak);
	}
}



AUTO_COMMAND_REMOTE;
void ReturnHtmlStringFromRemoteCommand(int iClientID, int iCommandRequestID, U32 iMCPID, char *pRetString)
{
	SendMonitoringCommandResultToMCP(iMCPID, iCommandRequestID, iClientID, pRetString, NULL);
}

AUTO_COMMAND_REMOTE;
void HereIsGSLGlobalInfo(ContainerID iServerID, int iLowLevelIndex, GameServerGlobalInfo *pInfo)
{
	TrackedServerState *pServer;

	if (iLowLevelIndex)
	{
		pServer = ServerFromTypeAndIDAndIndex(GLOBALTYPE_GAMESERVER, iServerID, iLowLevelIndex);
	}
	else
	{
		pServer = FindServerFromID(GLOBALTYPE_GAMESERVER, iServerID);
		if (pServer)
		{
			RemoteCommand_HereIsYourLowLevelControllerIndex(GLOBALTYPE_GAMESERVER, iServerID, pServer->iLowLevelIndex);
		}
	}

	if (pServer)
	{
		SimpleServerLog_AddTick(pServer);

		if (gbDoDynHoggPruning && !pServer->pGameServerSpecificInfo->mapName[0] && pInfo->mapName[0])
		{
			char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE];

			if (resExtractNameSpace(pInfo->mapName, ns, base))
			{
				AddNamespaceForDynHoggPruning(pServer->pMachine, ns);
			}
		}

		StructCopyAll(parse_GameServerGlobalInfo, pInfo, pServer->pGameServerSpecificInfo);

		pServer->pGameServerSpecificInfo->iLastContact = timeSecondsSince2000();
	}

}

AUTO_COMMAND_REMOTE;
void HereIsGatewayGlobalInfo(ContainerID iServerID, GatewayServerGlobalInfo *pInfo)
{
	TrackedServerState *pServer;

	pServer = FindServerFromID(GLOBALTYPE_GATEWAYSERVER, iServerID);

	if (pServer)
	{
		StructCopyAll(parse_GatewayServerGlobalInfo, pInfo, pServer->pGatewayServerSpecificInfo);
	}

}

AUTO_COMMAND_REMOTE;
void HereIsDBGlobalInfo(GlobalType iServerType, ContainerID iServerID, DatabaseGlobalInfo *pInfo)
{
	TrackedServerState *pServer;
	if (!IsTypeObjectDB(iServerType))
	{
		return;
	}

	pServer = FindServerFromID(iServerType, iServerID);

	if (pServer)
	{
		StructCopyAll(parse_DatabaseGlobalInfo, pInfo, pServer->pDataBaseSpecificInfo);		
	}

}

AUTO_COMMAND_REMOTE;
void HereIsLoginServerGlobalInfo(GlobalType iServerType, ContainerID iServerID, LoginServerGlobalInfo *pInfo)
{
	TrackedServerState *pServer;
	if (iServerType != GLOBALTYPE_LOGINSERVER)
	{
		return;
	}

	pServer = FindServerFromID(iServerType, iServerID);

	if (pServer)
	{

		StructCopyFields(parse_LoginServerGlobalInfo, pInfo, pServer->pLoginServerSpecificInfo, 0, 0);		
	}
}

AUTO_COMMAND_REMOTE;
void HereIsJobManagerGlobalInfo(GlobalType iServerType, ContainerID iServerID, JobManagerGlobalInfo *pInfo)
{
	TrackedServerState *pServer;
	if (iServerType != GLOBALTYPE_JOBMANAGER)
	{
		return;
	}

	pServer = FindServerFromID(iServerType, iServerID);

	if (pServer)
	{

		StructCopyAll(parse_JobManagerGlobalInfo, pInfo, pServer->pJobManagerSpecificInfo);		
	}
}


AUTO_COMMAND_REMOTE;
void ServerIsGoingToDie(GlobalType eType, ContainerID iID, char *pExplanation, bool bForceRelaunch, bool bWantGoAheadAndDieCall)
{
	TrackedServerState *pServer = FindServerFromID(eType, iID);

	log_printf(LOG_SHARD, "Server %u of type %s killing itself... explanation: %s",
		iID, GlobalTypeToName(eType), pExplanation);

	if (pServer)
	{
		pServer->bKilledGracefully = true;
		pServer->bKilledIntentionally = true;
		pServer->bRecreateDespiteIntentionalKill = bForceRelaunch;

		if (bWantGoAheadAndDieCall)
		{
			RemoteCommand_GoAheadAndDie(eType, iID);
		}
	}
}

AUTO_COMMAND_REMOTE;
void ReturnJpegForServerMonitoring(int iRequestID, ContainerID iMCPID, TextParserBinaryBlock *pBlock, int iLifeSpan, char *pErrorMessage)
{
	int iSize;
	char *pBuf = TextParserBinaryBlock_PutIntoMallocedBuffer(pBlock, &iSize);

	ReturnJpegToMCP(iMCPID, iRequestID, pBuf, iSize, iLifeSpan, pErrorMessage);

	if (iSize)
	{
		free(pBuf);
	}
}

AUTO_COMMAND_REMOTE;
void BroadcastChatMessageToGameServers(ACMD_SENTENCE msg, S32 eNotifyType)
{
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_GAMESERVER];

	while (pServer)
	{
		RemoteCommand_cmdServerChat_BroadcastMessage(GLOBALTYPE_GAMESERVER, pServer->iContainerID, msg, eNotifyType);
		pServer = pServer->pNext;
	}
}

// This is a message from the Shard ChatServer to other shard servers (GameServers, GuildServer)
AUTO_COMMAND_REMOTE;
void BroadcastGlobalChatServerOnline(bool bOnline)
{	
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_GAMESERVER];
	while (pServer)
	{
		RemoteCommand_cmdServerChat_ReconnectToChatServer(GLOBALTYPE_GAMESERVER, pServer->iContainerID, bOnline);
		pServer = pServer->pNext;
	}
	if (bOnline) // This stuff only happens if Global Chat (or Local-only) is Online
	{
		pServer = gpServersByType[GLOBALTYPE_GUILDSERVER];
		while (pServer)
		{
			RemoteCommand_aslGuild_ChatUpdate(GLOBALTYPE_GUILDSERVER, 0); // Request an update of guild information
			pServer = pServer->pNext;
		}
	}
}

// This is a message from the ControllerTracker about a GCS and tells the shard ChatServer to reconnect
AUTO_COMMAND_REMOTE;
void GlobalChatServerOnline(void)
{
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_CHATSERVER];
	while (pServer)
	{
		RemoteCommand_ReconnectToGlobalChat(GLOBALTYPE_CHATSERVER, pServer->iContainerID);
		pServer = pServer->pNext;
	}
}

AUTO_COMMAND_REMOTE;
void ControllerAlertNotify(const char *pKey, const char *pString, enumAlertLevel eLevel, enumAlertCategory eCategory, int iLifespan,
	  GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer,
	  char *pMachineName, int iErrorID )
{
	TriggerAlert(pKey, pString, eLevel, eCategory, iLifespan, eContainerTypeOfObject, iIDOfObject, eContainerTypeOfServer, iIDOfServer, pMachineName, iErrorID);

}

//dev mode only... checks if a server exists and then sends a message to clients if it doesn't. Useful to make sure
//that people don't keep trying to do team commands when no team server is there and then they don't know why it's failing
AUTO_COMMAND_REMOTE;
void CheckIfServerTypeExists(ContainerID iGameServerIDToNotify, GlobalType eTypeToCheck)
{
	if (!gpServersByType[eTypeToCheck])
	{
		char errorString[1024];
		sprintf(errorString, "NOTE: No server of type %s exists, you silly person", GlobalTypeToName(eTypeToCheck));
		RemoteCommand_cmdServerChat_BroadcastMessage(GLOBALTYPE_GAMESERVER, iGameServerIDToNotify, errorString, kNotifyType_ServerAnnounce);
	}
}

AUTO_COMMAND_REMOTE;
void KeepAliveReturn(GlobalType eServerType, ContainerID iID)
{
	TrackedServerState *pServer = FindServerFromID(eServerType, iID);

	if (pServer)
	{
		pServer->iNextExpectedKeepAliveTime = timeSecondsSince2000() + gServerTypeInfo[eServerType].iKeepAliveDelay;
	}

}

AUTO_COMMAND ACMD_CATEGORY(test);
void floodLogServer(int iVal)
{
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_GAMESERVER];

	while (pServer)
	{
		RemoteCommand_FloodLogServer_Remote(GLOBALTYPE_GAMESERVER, pServer->iContainerID, iVal);
		pServer = pServer->pNext;
	}
}

//remember, if you want to do this through server monitor, it's in the Server Type Configuration screen
AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void SendCommandToAllServersOfType(GlobalType eType, ContainerID iExceptThisID, ACMD_SENTENCE pCommandStringToSend)
{
	TrackedServerState *pServer = gpServersByType[eType];

	while (pServer)
	{
		if (pServer->iContainerID != iExceptThisID)
		{
			if (pServer->pLink)
			{
				SendCommandDirectlyToServerThroughNetLink(pServer, pCommandStringToSend);
			}
			else
			{
				RemoteCommand_CallLocalCommandRemotely(eType, pServer->iContainerID, pCommandStringToSend);
			}
		}

		pServer = pServer->pNext;
	}
}

AUTO_COMMAND_REMOTE;
void SendCommandToAllServersOfTypeOnMachine(GlobalType eType, char *pMachineName, ContainerID iExceptThisID, ACMD_SENTENCE pCommandStringToSend)
{
	TrackedServerState *pServer = gpServersByType[eType];

	while (pServer)
	{
		if (pServer->iContainerID != iExceptThisID && stricmp_safe(pServer->pMachine->machineName, pMachineName) == 0)
		{
			RemoteCommand_CallLocalCommandRemotely(eType, pServer->iContainerID, pCommandStringToSend);
		}

		pServer = pServer->pNext;
	}
}

AUTO_COMMAND ACMD_NAME(SendCommandToAllServersOfTypeOnMachine);
void SendCommandToAllServersOfTypeOnMachineCmd(GlobalType eType, char *pMachineName, ContainerID iExceptThisID, ACMD_SENTENCE pCommandStringToSend)
{
	SendCommandToAllServersOfTypeOnMachine(eType, pMachineName, iExceptThisID, pCommandStringToSend);
}


AUTO_STRUCT;
typedef struct SendRepeatedlyCache
{
	GlobalType eType;
	ContainerID iExceptThisID;
	int iNumTimes;
	int iDelay;
	char *pCommandToSend;
} SendRepeatedlyCache;

void SendCommandRepeatedlyToAllServersOfTypeCB(TimedCallback *pCB, float fTime, SendRepeatedlyCache *pCache)
{
	SendCommandToAllServersOfType(pCache->eType, pCache->iExceptThisID, pCache->pCommandToSend);

	pCache->iNumTimes--;

	if (pCache->iNumTimes)
	{
		TimedCallback_Run(SendCommandRepeatedlyToAllServersOfTypeCB, pCache, (float)(pCache->iDelay));
	}
	else
	{
		StructDestroy(parse_SendRepeatedlyCache, pCache);
	}
}
AUTO_COMMAND_REMOTE;
void SendCommandRepeatedlyToAllServersOfType(GlobalType eType, ContainerID iExceptThisID, int iNumTimes, int iBetweenTimesDelay, int iInitialDelay, ACMD_SENTENCE pCommandStringToSend)
{
	SendRepeatedlyCache *pCache;
	if (iNumTimes == 0)
	{
		AssertOrAlert("BAD_SENDCOMMANDREPEATEDLY", "Someone is trying to repeatedly send command <<%s>> to all %ss, but zero times. That seems wrong",
			pCommandStringToSend, GlobalTypeToName(eType));
		return;
	}

	pCache = StructCreate(parse_SendRepeatedlyCache);
	pCache->eType = eType;
	pCache->iExceptThisID = iExceptThisID;
	pCache->iNumTimes = iNumTimes;
	pCache->iDelay = iBetweenTimesDelay;
	pCache->pCommandToSend = strdup(pCommandStringToSend);

	TimedCallback_Run(SendCommandRepeatedlyToAllServersOfTypeCB, pCache, (float)iInitialDelay);
}





AUTO_COMMAND;
void SetAllGameServerInactivityTimeoutMinutes(int iMinutes)
{
	char str[256];

	gbGameServerInactivityTimeoutMinutesSet = true;
	giGameServerInactivityTimeoutMinutes = iMinutes;

	sprintf(str, "InactivityTimeoutMinutes %d", iMinutes);
	SendCommandToAllServersOfType(GLOBALTYPE_GAMESERVER, 0, str);
}

AUTO_COMMAND_REMOTE;
void GameServerIsHandshaking(GlobalType eType, ContainerID iID)
{
	TrackedServerState *pServer = FindServerFromID(eType, iID);

	if (pServer)
	{
		if (!pServer->pGameServerSpecificInfo)
		{
			pServer->pGameServerSpecificInfo = StructCreate(parse_GameServerGlobalInfo);
		}

		pServer->pGameServerSpecificInfo->iLastHandhakingWithMapManagerTime = timeSecondsSince2000();
	}
}

AUTO_COMMAND_REMOTE;
void AddThingToDoOnServerCrash(GlobalType eType, ContainerID iID, RemoteCommandGroup *pGroup)
{
	TrackedServerState *pServer = FindServerFromIDRespectingID0(eType, iID);

	if (pServer)
	{
		eaPush(&pServer->ppThingsToDoOnCrash, StructClone(parse_RemoteCommandGroup, pGroup));
	}
}

AUTO_COMMAND_REMOTE;
void AddThingToDoOnServerClose(GlobalType eType, ContainerID iID, RemoteCommandGroup *pGroup)
{
	TrackedServerState *pServer = FindServerFromIDRespectingID0(eType, iID);

	if (pServer)
	{
		eaPush(&pServer->ppThingsToDoOnAnyClose, StructClone(parse_RemoteCommandGroup, pGroup));
	}
}

AUTO_COMMAND_REMOTE;
void RemoveThingToDoOnServerCrashOrClose(GlobalType eServerType, ContainerID iServerID, bool bOnAnyClose, GlobalType eRequestServerType, ContainerID iRequestServerID, U32 iRequestUID)
{
	TrackedServerState *pServer = FindServerFromID(eServerType, iServerID);

	if (pServer)
	{
		RemoteCommandGroup ***pppList = bOnAnyClose ? &pServer->ppThingsToDoOnAnyClose : &pServer->ppThingsToDoOnCrash;
		char keyString[32];
		int iIndex;

		GetRemoteCommandGroupKeyString(keyString, eRequestServerType, iRequestServerID, iRequestUID);

		iIndex = eaIndexedFindUsingString(pppList, keyString);
		if (iIndex != -1)
		{
			eaRemove(pppList, iIndex);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_NAME(KillServer);
void KillServerRemotely(GlobalType eServerType, ContainerID iServerID, char *pReason)
{
	TrackedServerState *pServer = FindServerFromID(eServerType, iServerID);
	if (pServer)
	{
		pServer->bKilledIntentionally = true;
		KillServer(pServer, pReason);
	}
}



AUTO_COMMAND_REMOTE;
void ServerMayBeStallyOverNextNSeconds(GlobalType eServerType, ContainerID iServerID, int iPotentialStallyTime, char *pReason)
{
	TrackedServerState *pServer = FindServerFromID(eServerType, iServerID);
	if (pServer)
	{
		SimpleServerLog_AddUpdate(pServer, "May be laggy for %d seconds because %s", iPotentialStallyTime, pReason);
		if (iPotentialStallyTime)
		{
			pServer->iNoLagOrInactivityAlertsUntilTime = timeSecondsSince2000() + iPotentialStallyTime;
		}
		else
		{
			pServer->iNoLagOrInactivityAlertsUntilTime = 0;
		}
	}
}

AUTO_COMMAND_REMOTE;
void ServerLogOnController(GlobalType eServerType, ContainerID iServerID, char* strLog)
{
	TrackedServerState *pServer = FindServerFromID(eServerType, iServerID);
	if (pServer)
	{
		SimpleServerLog_AddUpdate(pServer, "Additional log: %s", strLog);
	}
}

AUTO_COMMAND_REMOTE;
void KillAllButSomeServersOfType(Controller_KillAllButSomeServersOfTypeInfo *pInfo)
{
	TrackedServerState *pServer = gpServersByType[pInfo->eServerType];

	while (pServer)
	{
		TrackedServerState *pNext = pServer->pNext;

		if (ea32Find(&pInfo->pIDsNotToKill, pServer->iContainerID) == -1)
		{
			pServer->bKilledIntentionally = true;
			KillServer(pServer, pInfo->pReason);
		}

		pServer = pNext;
	}

}

AUTO_COMMAND_REMOTE;
void RequestAutoSettings(GlobalType eType, ContainerID iID)
{
	char **ppCommands = NULL;
	int i;

	TrackedServerState *pServer;

	if (!gServerTypeInfo[eType].bSupportsAutoSettings)
	{
		return;
	}

	pServer = FindServerFromID(eType, iID);

	if (!pServer)
	{
		return;
	}

	pServer->bHasGottenAutoSettingsViaRemoteCommand = true;


	ppCommands = ControllerAutoSettings_GetCommandStringsForServerType(eType);

	
	for (i=0; i < eaSize(&ppCommands); i++)
	{
		RemoteCommand_SendAutoSettingFromController(eType, iID, ppCommands[i]);
	}
}

AUTO_COMMAND_REMOTE;
int DoesServerExist(GlobalType eType, ContainerID iID)
{
	if (FindServerFromID(eType, iID))
	{
		return 1;
	}

	return 0;
}

//command for XMLRPC which gets a super-simple super-high-level overview of shard status
AUTO_STRUCT;
typedef struct ShardStatusOverview
{
	bool bStartingUp;
	bool bLocked;
	bool bGatewayLocked;
	bool bBillingKillSwitch;
	bool bMicroTransKillSwitch;
} ShardStatusOverview;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
ShardStatusOverview *GetShardStatusOverview(void)
{
	ShardStatusOverview *pOverview = StructCreate(parse_ShardStatusOverview);
	pOverview->bStartingUp = ControllerScripting_IsRunning();
	pOverview->bLocked = gbShardIsLocked;
	pOverview->bGatewayLocked = gbGatewayIsLocked;
	pOverview->bBillingKillSwitch = gbBillingKillSwitch;
	pOverview->bMicroTransKillSwitch = gbMTKillSwitch;

	return pOverview;
}

static U32 siNextDoesntExistAlertTime[GLOBALTYPE_MAXTYPES] = {0};

//used for situations like "hey, a team server command failed... I want to make sure that someone didn't accidentally set
//up this shard with no team server
AUTO_COMMAND_REMOTE;
void AlertIfServerTypeDoesntExist(GlobalType eRequiredType, char *pMessage, int iThrottleSeconds)
{
	U32 iCurTime;

	if (eRequiredType < 0 || eRequiredType >= GLOBALTYPE_MAXTYPES)
	{
		return;
	}

	if (gpServersByType[eRequiredType])
	{
		return;
	}

	iCurTime = timeSecondsSince2000();

	if (siNextDoesntExistAlertTime[eRequiredType] > iCurTime)
	{
		return;
	}

	siNextDoesntExistAlertTime[eRequiredType] = iCurTime + iThrottleSeconds;
	
	CRITICAL_NETOPS_ALERT("SERVER_DOESNT_EXIST", "There is no %s in the shard right now, but one is required because: %s. Did you misconfigure ShardLauncher or something?",
		GlobalTypeToName(eRequiredType), pMessage);

}





#include "Controller_RemoteCommands_c_ast.c"
