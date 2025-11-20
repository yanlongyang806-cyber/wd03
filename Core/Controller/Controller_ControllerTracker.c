#include "Controller.h"
#include "..\..\core\newcontrollertracker\pub\NewControllerTracker_pub.h"
#include "serverlib.h"
#include "structnet.h"
#include "autogen\NewControllerTracker_pub_h_ast.h"
#include "stringcache.h"
#include "sysutil.h"
#include "sock.h"
#include "utilitieslib.h"
#include "fileutil2.h"
#include "estring.h"
#include "Logging.h"
#include "timing.h"
#include "Alerts.h"
#include "Controller_ControllerTracker_c_ast.h"
#include "GlobalTypes.h"
#include "accountNet.h"
#include "ticketNet.h"
#include "controller_utils.h"
#include "Controller_ClusterController.h"
#include "ShardCommon.h"

#define CONTROLLER_TRACKER_LINK_FAILURE_TIME 10

#define LAST_MINUTE_DIR_NAME "c:\\LastMinuteFiles"

AllLastMinuteFilesInfo gLastMinuteFileInfo = {NULL};

ShardInfo_Perf gShardPerf = {0};




ShardInfo_Full *GetShardInfo(void)
{
	char *pTempString1 = NULL;
	char *pTempString2 = NULL;
	ShardInfo_Full *pShardInfo = StructCreate(parse_ShardInfo_Full);

	TrackedMachineState *pMachineForLoginServer = FindDefaultMachineForType(GLOBALTYPE_LOGINSERVER, NULL, NULL);

	pShardInfo->basicInfo.pShardCategoryName = allocAddString(gShardCategoryName);
	pShardInfo->basicInfo.pProductName = allocAddString(GetProductName());
	pShardInfo->basicInfo.pShardName = strdup(gNameToGiveToControllerTracker[0] ? gNameToGiveToControllerTracker : getComputerName());
	if (ShardCommon_GetClusterName())
	{
		pShardInfo->basicInfo.pClusterName = strdup(ShardCommon_GetClusterName());
	}

	pShardInfo->basicInfo.pShardControllerAddress = strdup(makeIpStr(spLocalMachine->iPublicIP));
	pShardInfo->basicInfo.pShardLoginServerAddress = pMachineForLoginServer ? strdup(makeIpStr(pMachineForLoginServer->iPublicIP))
		: strdup(makeIpStr(spLocalMachine->iPublicIP));
	pShardInfo->basicInfo.pVersionString = strdup(GetUsefulVersionString());
	pShardInfo->basicInfo.pPatchCommandLine = gPatchingCommandLine ? strdup( gPatchingCommandLine ) : NULL;
	pShardInfo->basicInfo.pAccountServer = strdup(getAccountServer());

	estrSuperEscapeString(&pTempString1, GetShardInfoString());
	estrPrintf(&pTempString2, "%s -SuperEsc SetShardInfoString %s", gpAutoPatchedClientCommandLine ? gpAutoPatchedClientCommandLine : "", pTempString1);
	
	if (accountServerWasSet())
	{
		if (stricmp(getAccountServer(), "localhost") == 0)
		{
			estrConcatf(&pTempString2, " -SetAccountServer %s", makeIpStr(getHostPublicIp()));
		}
		else
		{	
			estrConcatf(&pTempString2, " -SetAccountServer %s", makeIpStr(ipFromString(getAccountServer())));
		}
	}

	if (ticketTrackerWasSet())
	{
		if (stricmp(getTicketTracker(), "localhost") == 0)
		{
			estrConcatf(&pTempString2, " -SetTicketTracker %s", makeIpStr(getHostPublicIp()));
		}
		else
		{	
			estrConcatf(&pTempString2, " -SetTicketTracker %s", makeIpStr(ipFromString(getTicketTracker())));
		}
	}


	pShardInfo->basicInfo.pAutoClientCommandLine =  strdup(pTempString2);
	estrDestroy(&pTempString1);
	estrDestroy(&pTempString2);

	pShardInfo->basicInfo.bHasLocalMontiringMCP = gbLaunchMonitoringMCP;

	if (gbLaunchMonitoringMCP)
	{
		estrPrintf(&pShardInfo->basicInfo.pMonitoringLink, "<a href=\"http://%s/viewxpath\">Monitor</a>",pShardInfo->basicInfo.pShardControllerAddress);
	}

	return pShardInfo;
}

AUTO_ENUM;
typedef enum
{
	CONNECTION_STATE_NONE,
	CONNECTION_STATE_WAITING_FOR_CONNECTION,
	CONNECTION_STATE_CONNECTED,
} enumControllerToControllerTrackerConnectionState;

typedef struct
{
	enumControllerToControllerTrackerConnectionState eState;
	U32 iTimeEnteredState;
	char *pIPToConnectTo;
	NetLink *pLink;
} ControllerToControllerTrackerConnection;


static ControllerToControllerTrackerConnection **sppControllerTrackerConnections = NULL;

void SetControllerTrackerConnectionState(ControllerToControllerTrackerConnection *pConnection, enumControllerToControllerTrackerConnectionState eNewState)
{
	pConnection->eState = eNewState;
	pConnection->iTimeEnteredState = timeSecondsSince2000();

	printf("Now in Controllertracker connection state %s\n",
		StaticDefineIntRevLookup(enumControllerToControllerTrackerConnectionStateEnum, eNewState));

	log_printf(LOG_MISC, "For CT %s, now in Controllertracker connection state %s\n",
		pConnection->pIPToConnectTo, StaticDefineIntRevLookup(enumControllerToControllerTrackerConnectionStateEnum, eNewState));

}

extern void GlobalChatServerOnline(void);
void HandleControllerTrackerMessage(Packet *pak,int cmd, NetLink *link, void *data)
{
	switch(cmd)
	{
	case FROM_NEWCONTROLLERTRACKER_TO_SHARD_GLOBALCHAT_ONLINE:
		GlobalChatServerOnline();
		break;
	}
}


void HandleControllerTrackerDisconnect(NetLink* link,void *user_data)
{
	char *pDisconnectReason = NULL;
	estrStackCreate(&pDisconnectReason);
	linkGetDisconnectReason(link, &pDisconnectReason);
	printf("Controllertracker %s disconnected. Reason: %s\n", makeIpStr(linkGetIp(link)), pDisconnectReason);
	log_printf(LOG_MISC, "Controllertracker %s disconnected. Reason: %s\n", makeIpStr(linkGetIp(link)), pDisconnectReason);
	estrDestroy(&pDisconnectReason);

}

//for compatibility reasons, we send two separate packets... TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_ARE_LOGINSERVER_IPS
//is the old packet, containing IPs of all loginservers that are monitoring on DEFAULT_LOGINSERVER_PORT
//
//TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_ARE_LOGINSERVER_IPS_AND_PORTS is the new-style, with 
//IP-port pairs
void SendLoginServerIPsToControllerTracker(ControllerToControllerTrackerConnection *pConnection)
{
	Packet *pPak = pktCreate(pConnection->pLink, TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_ARE_LOGINSERVER_IPS);
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_LOGINSERVER];

	PortIPPairList *pList = StructCreate(parse_PortIPPairList);

	while (pServer)
	{
		int iPort = SAFE_MEMBER2(pServer, pLoginServerSpecificInfo, iPort);

		if (iPort)
		{
			PortIPPair *pPair = StructCreate(parse_PortIPPair);
			pPair->iIP = pServer->pMachine->iPublicIP;
			pPair->iPort = iPort;

			eaPush(&pList->ppPortIPPairs, pPair);

			if (iPort == DEFAULT_LOGINSERVER_PORT)
			{
				pktSendBits(pPak, 32, pServer->pMachine->iPublicIP);
			
			}
		}

		pServer = pServer->pNext;
	}

	pktSendBits(pPak, 32, 0);
	pktSend(&pPak);

	pPak = pktCreate(pConnection->pLink, TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_ARE_LOGINSERVER_IPS_AND_PORTS);
	ParserSendStructSafe(parse_PortIPPairList, pPak, pList);
	pktSend(&pPak);
	StructDestroy(parse_PortIPPairList, pList);
}



void UpdateControllerTrackerConnection(ControllerToControllerTrackerConnection *pConnection)
{
	switch (pConnection->eState)
	{
	case CONNECTION_STATE_NONE:
		linkRemove(&pConnection->pLink);
		printf("Attempting to contact controllertracker at %s port %d\n", pConnection->pIPToConnectTo, CONTROLLERTRACKER_SHARD_INFO_PORT);
		pConnection->pLink = commConnect(comm_controller,LINKTYPE_SHARD_NONCRITICAL_5MEG, LINK_FORCE_FLUSH,pConnection->pIPToConnectTo, CONTROLLERTRACKER_SHARD_INFO_PORT, HandleControllerTrackerMessage,0,HandleControllerTrackerDisconnect,0);
		if (pConnection->pLink)
		{
			SetControllerTrackerConnectionState(pConnection, CONNECTION_STATE_WAITING_FOR_CONNECTION);
		}
		break;

	case CONNECTION_STATE_WAITING_FOR_CONNECTION:
		if (timeSecondsSince2000() - pConnection->iTimeEnteredState > CONTROLLER_TRACKER_LINK_FAILURE_TIME
			|| linkDisconnected(pConnection->pLink))
		{
			CRITICAL_NETOPS_ALERT("CT_CONNECT_FAIL", "After %d seconds, uanble to connect to CT %s",
				CONTROLLER_TRACKER_LINK_FAILURE_TIME, pConnection->pIPToConnectTo);

			SetControllerTrackerConnectionState(pConnection, CONNECTION_STATE_NONE);
			break;
		}

		if (linkConnected(pConnection->pLink))
		{
			Packet *pak = pktCreate(pConnection->pLink, TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_IS_SHARD_INFO);

			ShardInfo_Full *pShardInfo = GetShardInfo();
			ParserSendStructSafe(parse_ShardInfo_Full, pak, pShardInfo);
			pktSend(&pak);

			StructDestroy(parse_ShardInfo_Full, pShardInfo);


			pak = pktCreate(pConnection->pLink, TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_ARE_LAST_MINUTE_FILES);
			
			ParserSendStructSafe(parse_AllLastMinuteFilesInfo, pak, &gLastMinuteFileInfo);
			
			pktSend(&pak);

			SetControllerTrackerConnectionState(pConnection, CONNECTION_STATE_CONNECTED);

			SendLoginServerIPsToControllerTracker(pConnection);
		}
		break;

	case CONNECTION_STATE_CONNECTED:
		if (linkDisconnected(pConnection->pLink))
		{
			SetControllerTrackerConnectionState(pConnection, CONNECTION_STATE_NONE);
			break;
		}
	}
}




void UpdateControllerTrackerConnections(void)
{
	int i;

	static bool bFirst = true;

	if (!gbConnectToControllerTracker)
	{
		return;
	}

	if (bFirst)
	{	
		char **sppControllerTrackerIPs = NULL;
		char *pControllerTrackerHost = GetControllerTrackerHost();
		bFirst = false;

		if (strstri(pControllerTrackerHost, "qa"))
		{

			printf("%s appears to be a qa controllertracker, not trying to find unique IPs from it...\n",
				pControllerTrackerHost);

			if (!ipFromString(pControllerTrackerHost))
			{
				AssertOrAlert("BAD_CT_HOST", "Couldn't find or parse assigned controller tracker host %s", pControllerTrackerHost);
			}
			else
			{
				ControllerToControllerTrackerConnection *pConnection = calloc(sizeof(ControllerToControllerTrackerConnection), 1);
				pConnection->pIPToConnectTo = pControllerTrackerHost;

				eaPush(&sppControllerTrackerConnections, pConnection);
			}
		}
		else
		{

			GetAllUniqueIPs(pControllerTrackerHost, &sppControllerTrackerIPs);

			if (!eaSize(&sppControllerTrackerIPs))
			{
				AssertOrAlert("BAD_CT_HOST", "Couldn't find or parse assigned controller tracker host %s", pControllerTrackerHost);
			}

			for (i=0; i < eaSize(&sppControllerTrackerIPs); i++)
			{
				ControllerToControllerTrackerConnection *pConnection = calloc(sizeof(ControllerToControllerTrackerConnection), 1);

				pConnection->pIPToConnectTo = sppControllerTrackerIPs[i];

				eaPush(&sppControllerTrackerConnections, pConnection);
			}
		}
	}

	for (i=0; i < eaSize(&sppControllerTrackerConnections); i++)
	{
		UpdateControllerTrackerConnection(sppControllerTrackerConnections[i]);
	}

}
	

AUTO_COMMAND;
char *SendLastMinuteFilesToNewControllerTracker(void)
{
	char **ppFileList;
	int iCount;
	int i;
	Packet *pOutPack;
	static char retString[32];


	if (!gbConnectToControllerTracker)
	{
		return "ControllerTracker connection not requested";
	}

	StructDeInit(parse_AllLastMinuteFilesInfo, &gLastMinuteFileInfo);

	ppFileList = fileScanDirFolders(LAST_MINUTE_DIR_NAME, FSF_FILES);
    iCount = eaSize( &ppFileList );

	for (i=0;i < iCount; i++)
	{
		LastMinuteFileInfo *pInfo = StructCreate(parse_LastMinuteFileInfo);
		pInfo->pFileName = strdup(ppFileList[i] + strlen(LAST_MINUTE_DIR_NAME) + 1);
		pInfo->pFileData = TextParserBinaryBlock_CreateFromFile(ppFileList[i], true);

		eaPush(&gLastMinuteFileInfo.ppFiles, pInfo);
	}

	fileScanDirFreeNames(ppFileList);

	for (i=0; i < eaSize(&sppControllerTrackerConnections); i++)
	{
		if (sppControllerTrackerConnections[i]->eState == CONNECTION_STATE_CONNECTED)
		{
			pOutPack = pktCreate(sppControllerTrackerConnections[i]->pLink, TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_ARE_LAST_MINUTE_FILES);
			
			ParserSendStructSafe(parse_AllLastMinuteFilesInfo, pOutPack, &gLastMinuteFileInfo);
			
			pktSend(&pOutPack);
		}
	}
	
	sprintf(retString, "Sent %d files", iCount);
	return retString;
}

void SendLoginServerIPsToControllerTrackers(void)
{
	int i;

	for (i=0 ; i < eaSize(&sppControllerTrackerConnections); i++)
	{
		if (sppControllerTrackerConnections[i]->eState == CONNECTION_STATE_CONNECTED)
		{
			SendLoginServerIPsToControllerTracker(sppControllerTrackerConnections[i]);
		}
	}
}


void SendPerfInfoToControllerTracker(bool bAlsoLog)
{

	Packet *pak;

	TrackedServerState *pServer;

	int iTotalCPU60 = 0;
	int i;
	U64 iTotalPhysAvail = 0;

	StructReset(parse_ShardInfo_Perf, &gShardPerf);

	pServer = gpServersByType[GLOBALTYPE_GAMESERVER];

	while (pServer)
	{
		gShardPerf.iGameServers++;
		if (pServer->pGameServerSpecificInfo)
		{
			gShardPerf.iPlayers += pServer->pGameServerSpecificInfo->iNumPlayers;
			gShardPerf.iEntities += pServer->pGameServerSpecificInfo->iNumEntities;
		}
		pServer = pServer->pNext;
	}

	ReportNumPlayersForForSuddenDrops(gShardPerf.iPlayers);
	Controller_HereIsTotalNumPlayersForQueue(gShardPerf.iPlayers);
	Controller_ClusterControllerHereIsNumPlayers(gShardPerf.iPlayers);

	gShardPerf.iMachines = giNumMachines;
	gShardPerf.iAlerts = Alerts_GetAllLevelCount();

	gShardPerf.iNumNotResponding = Alerts_GetCountByKey(ALERTKEY_GAMESERVERNOTRESPONDING);
	gShardPerf.iNumDiedAtStartup = Alerts_GetCountByKey(ALERTKEY_GAMESERVERNEVERSTARTED);
	if (gServerTypeInfo[GLOBALTYPE_GAMESERVER].pCrashOrAssertAlertKey)
	{
		gShardPerf.iNumCrashed = Alerts_GetCountByKey(gServerTypeInfo[GLOBALTYPE_GAMESERVER].pCrashOrAssertAlertKey);
	}
	gShardPerf.iRunningSlow = Alerts_GetCountByKey(ALERTKEY_GAMESERVERRUNNINGSLOW);

	for (i=0; i < giNumMachines; i++)
	{
		TrackedMachineState *pMachine = &gTrackedMachines[i];

		if (pMachine->performance.cpuLast60 > gShardPerf.iMaxCPU60)
		{
			gShardPerf.iMaxCPU60 = pMachine->performance.cpuLast60;
		}

		iTotalCPU60 += pMachine->performance.cpuLast60;
		iTotalPhysAvail += pMachine->performance.iFreeRAM;

		if ( i == 0 || pMachine->performance.iAvailVirtual < gShardPerf.iMinVirtAvail)
		{
			gShardPerf.iMinVirtAvail = pMachine->performance.iAvailVirtual;
		}

		if ( i == 0 || pMachine->performance.iFreeRAM < gShardPerf.iMinPhysAvail)
		{
			gShardPerf.iMinPhysAvail = pMachine->performance.iFreeRAM;
		}

		if (pMachine->iCrashedServers > gShardPerf.iMaxCrashes)
		{
			gShardPerf.iMaxCrashes = pMachine->iCrashedServers;
		}

	}

	gShardPerf.iAvgCPU60 = iTotalCPU60 / giNumMachines;
	gShardPerf.iAvgPhysAvail = iTotalPhysAvail / giNumMachines;




	pServer = gpServersByType[GLOBALTYPE_LOGINSERVER];
	while (pServer)
	{
		if (pServer->pLoginServerSpecificInfo)
		{
			gShardPerf.iLoggingIn = pServer->pLoginServerSpecificInfo->iNumLoggingIn;
		}

		pServer = pServer->pNext;
	}

	pServer = gpServersByType[GLOBALTYPE_OBJECTDB];
	if (pServer && pServer->pDataBaseSpecificInfo)
	{
		gShardPerf.dbUpdPerSec = pServer->pDataBaseSpecificInfo->iUpdatesPerSec;
	}

	gShardPerf.iCreationTime = gControllerStartupTime;

	gShardPerf.iLongestStall = giLongestDelayedGameServerTime;


	for (i=0 ; i < eaSize(&sppControllerTrackerConnections); i++)
	{
		if (sppControllerTrackerConnections[i]->eState == CONNECTION_STATE_CONNECTED)
		{
			pak = pktCreate(sppControllerTrackerConnections[i]->pLink, TO_NEWCONTROLLERTRACKER_FROM_SHARD_HERE_IS_SHARD_PERF_INFO);

			ParserSendStructSafe(parse_ShardInfo_Perf, pak, &gShardPerf);

			pktSend(&pak);
		}
	}

	if (bAlsoLog)
	{
		servLogWithStruct(LOG_CONTROLLER, "ControllerPerfLog", &gShardPerf, parse_ShardInfo_Perf);
	}
}

static char *spOverrideControllerTracker = NULL;

AUTO_COMMAND ACMD_COMMANDLINE;
void SetControllerTracker(char *pControllerTracker)
{
	//due to the way shardLauncher works, this may get called with "other", just ignore that
	if (pControllerTracker && pControllerTracker[0] && stricmp(pControllerTracker, "unspecified") != 0)
	{
		estrCopy2(&spOverrideControllerTracker, pControllerTracker);
	}
}

char *OVERRIDE_LATELINK_GetControllerTrackerHost(void)
{
	if (estrLength(&spOverrideControllerTracker))
	{
		return spOverrideControllerTracker;
	}

	return gServerLibLoadedConfig.newControllerTrackerHost_internal;
}

char *OVERRIDE_LATELINK_GetQAControllerTrackerHost(void)
{
	if (estrLength(&spOverrideControllerTracker))
	{
		return spOverrideControllerTracker;
	}
	return gServerLibLoadedConfig.qaControllerTrackerHost_internal;
}

void SendVersionInfoToControllerTracker(void)
{
	char *pCommandLine = NULL;
	char *pPatchingCommandLineSuperEsc = NULL;
	char *pVersionSuperEsc = NULL;

	if (!gPatchingCommandLine[0])
	{
		return;
	}

	estrSuperEscapeString(&pPatchingCommandLineSuperEsc, gPatchingCommandLine);
	estrSuperEscapeString(&pVersionSuperEsc, GetUsefulVersionString());

	estrPrintf(&pCommandLine, "SetShardVersionOnCT.exe -ShardName %s -CTName %s -SuperEsc VersionString %s -SuperEsc PatchCommandLine %s",
		GetShardNameFromShardInfoString(), GetControllerTrackerHost(), pVersionSuperEsc, pPatchingCommandLineSuperEsc);

	system_detach(pCommandLine, true, true);

	estrDestroy(&pCommandLine);
	estrDestroy(&pPatchingCommandLineSuperEsc);
	estrDestroy(&pVersionSuperEsc);
}



#include "autogen\NewControllerTracker_pub_h_ast.c"
#include "Controller_ControllerTracker_c_ast.c"
