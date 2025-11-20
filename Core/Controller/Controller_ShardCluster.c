#include "Controller_ShardCluster.h"
#include "textparser.h"
#include "earray.h"
#include "EString.h"
#include "ShardCluster_h_ast.h"
#include "Controller_ShardCluster_c_ast.h"
#include "Controller_ShardCluster.h"
#include "ShardCluster.h"
#include "utilitiesLib.h"
#include "net.h"
#include "Controller.h"
#include "CrypticPorts.h"
#include "TimedCallback.h"
#include "Alerts.h"
#include "ServerLib.h"
#include "structNet.h"
#include "sock.h"
#include "autogen/ServerLib_autogen_remotefuncs.h"
#include "ResourceInfo.h"
#include "ContinuousBuilderSupport.h"
#include "Controller_Utils.h"


#define SECS_BEFORE_NO_CONNECTION_ALERT 300
#define SECS_BEFORE_NO_RESPONSE_ALERT 60
#define SECS_BETWEEN_STATUS_REQUESTS 120

#define SECS_BETWEEN_ALERTS 3600

//this means that some other shard has sent us info that was different than what we previously knew, or has
//been created/destroyed
static bool sbSomethingChanged = true;

//this means that something about oru local shard that other shards care about has changed
static bool sbSomethingLocalChanged = false;

static ClusterShardSummary *FindThatsMeShard(void);

AUTO_STRUCT;
typedef struct OtherShardInfo
{
	char *pShardName;
	char *pMachineName;
} OtherShardInfo; 

AUTO_STRUCT;
typedef struct ClusterShardSummary_ControllerInternalStuff
{
	char *pLinkDebugName; AST(ESTRING)
	NetLink *pLink; NO_AST
	CommConnectFSM *pFSM; NO_AST
	U32 iLastStatusRequestedTime;
	U32 iLastStatusReceivedTime;
	U32 iConnectionAttemptedBeganTime;
	U32 iLastAlertTime;
} ClusterShardSummary_ControllerInternalStuff;

static OtherShardInfo **sppOtherShards = NULL;

static Cluster_Overview *spOverview = NULL;

static NetListen *spControllerShardClusterListen = NULL;

AUTO_COMMAND ACMD_COMMANDLINE;
void AddShardToCluster(char *pOtherShardName, char *pMachineName)
{

	OtherShardInfo *pInfo = StructCreate(parse_OtherShardInfo);
	pInfo->pShardName = strdup(pOtherShardName);
	pInfo->pMachineName = strdup(pMachineName);

	eaPush(&sppOtherShards, pInfo);
}

void CalculatePeriodicStatus(ClusterShardPeriodicStatus *pPeriodicStatus)
{
	TrackedServerState *pServer;

	pPeriodicStatus->iNumPlayers = 0;
	pPeriodicStatus->iNumGameServers = 0;
	pPeriodicStatus->iNumLoggingIn = 0;

	pServer = gpServersByType[GLOBALTYPE_GAMESERVER];

	while (pServer)
	{
		pPeriodicStatus->iNumGameServers++;
		if (pServer->pGameServerSpecificInfo)
		{
			pPeriodicStatus->iNumPlayers += pServer->pGameServerSpecificInfo->iNumPlayers;
		}
		pServer = pServer->pNext;
	}

	pServer = gpServersByType[GLOBALTYPE_LOGINSERVER];
	while (pServer)
	{
		if (pServer->pLoginServerSpecificInfo)
		{
			pPeriodicStatus->iNumLoggingIn += pServer->pLoginServerSpecificInfo->iNumLoggingIn;
		}

		pServer = pServer->pNext;
	}

	pPeriodicStatus->iNumInMainQueue = Controller_GetNumPlayersInMainLoginQueue();
	pPeriodicStatus->iNumInVIPQueue = Controller_GetNumPlayersInVIPLoginQueue();
}

static ClusterShardStatus *FindLocalShardStatus(bool bRecalcPeriodicData)
{
	static ClusterShardStatus *spStatus = NULL;
	GlobalType eType;
	
	if (!spStatus)
	{
		spStatus = StructCreate(parse_ClusterShardStatus);
		spStatus->pShardName = strdup(GetShardNameFromShardInfoString());
		spStatus->pVersion = strdup(GetUsefulVersionString());
		spStatus->eShardType = ShardCluster_GetShardType();
		CalculatePeriodicStatus(&spStatus->periodicStatus);
	}
	else if (bRecalcPeriodicData)
	{
		CalculatePeriodicStatus(&spStatus->periodicStatus);
	}

	eaClearStruct(&spStatus->ppServersByType, parse_ClusterServerTypeStatus);


	for (eType = 0; eType < GLOBALTYPE_MAX; eType++)
	{
		if (gServerTypeInfo[eType].bPutInfoAboutMeInShardClusterSummary)
		{
			if (gpServersByType[eType])
			{
				ClusterServerTypeStatus *pTypeStatus = StructCreate(parse_ClusterServerTypeStatus);
				TrackedServerState *pServer = gpServersByType[eType];
				pTypeStatus->eType = eType;

				while (pServer)
				{
					ClusterServerStatus *pServerStatus = StructCreate(parse_ClusterServerStatus);
					pServerStatus->iContainerID = pServer->iContainerID;
					pServerStatus->iIP = pServer->pMachine->IP;
					pServerStatus->pStateString= strdup(pServer->stateString);
					eaPush(&pTypeStatus->ppServers, pServerStatus);

					pServer = pServer->pNext;
				}

				eaPush(&spStatus->ppServersByType, pTypeStatus);
			}
		}
	}

	spStatus->bShardIsLocked = gbShardIsLocked;


	return spStatus;


}

static void SendOverviewToServer(TrackedServerState *pServer)
{
	if (!spOverview)
	{
		return;
	}

	if (pServer->pLink)
	{
		Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_HERE_IS_SHARD_CLUSTER_OVERVIEW);
		ParserSendStruct(parse_Cluster_Overview, pPak, spOverview);
		pktSend(&pPak);		
	}
	else
	{
		if (objLocalManager())
		{
			RemoteCommand_HereIsShardClusterOverview(pServer->eContainerType, pServer->iContainerID, spOverview);
		}
		else
		{
			//this happens from time to time in a CB during shard shutdown for unclear but probably irrelevant reasons
			if (!g_isContinuousBuilder)
			{
				AssertOrAlert("CANT_SEND_SHARD_OVERVIEW", "Can't send shard overview to %s[%u]. Talk to Alex W",
					GlobalTypeToName(pServer->eContainerType), pServer->iContainerID);
			}
		}
	}
}

void HandleRequestStatus(Packet *pak, NetLink *link)
{
	ClusterShardStatus *pStatus = FindLocalShardStatus(false);

	Packet *pReturnPak = pktCreate(link, SHARDCLUSTER_CONTROLLER_HERE_IS_STATUS);
	ParserSendStructSafe(parse_ClusterShardStatus, pReturnPak, pStatus);
	pktSend(&pReturnPak);
}

void HandleHereIsStatus(Packet *pak, NetLink *link)
{
	ClusterShardStatus *pStatus = ParserRecvStructSafe_Create(parse_ClusterShardStatus, pak);

	if (!(pStatus->pShardName && pStatus->pShardName[0]))
	{
		CRITICAL_NETOPS_ALERT("CORRUPT_SHARDCLUSTER_STATUS", "Received a corrupted shard status from %s",
			makeIpStr(linkGetIp(link)));
	}
	else
	{
		ClusterShardSummary *pSummary = eaIndexedGetUsingString(&spOverview->ppShards, pStatus->pShardName);
		if (!pSummary)
		{
			CRITICAL_NETOPS_ALERT("UNKNOWN_SHARDCLUSTER_STATUS", "Received a shard status from %s, never heard of it",
				pStatus->pShardName);
		}
		else
		{
			ClusterShardSummary_ControllerInternalStuff *pInternalStuff = (ClusterShardSummary_ControllerInternalStuff*)pSummary->pUserData;
			pInternalStuff->iLastStatusReceivedTime = timeSecondsSince2000();


			if (pSummary->pMostRecentStatus)
			{
				if (StructCompare(parse_ClusterShardStatus, pStatus, pSummary->pMostRecentStatus, 0, 0, 0) == 0)
				{
					//do nothing, status is the same as it was previously
				}
				else
				{
					StructDestroy(parse_ClusterShardStatus, pSummary->pMostRecentStatus);
					pSummary->pMostRecentStatus = pStatus;
					sbSomethingChanged = true;
					pStatus = NULL;
				}
			}
			else
			{
				pSummary->eShardType = pStatus->eShardType;
				pSummary->pMostRecentStatus = pStatus;
				sbSomethingChanged = true;
				pStatus = NULL;

			}
		}
	}

	StructDestroySafe(parse_ClusterShardStatus, &pStatus);

}

static void ControllerShardClusterHandleMsg(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	switch (cmd)
	{

	case SHARDCLUSTER_CONTROLLER_REQUEST_STATUS:
		HandleRequestStatus(pak, link);
		return;

	case SHARDCLUSTER_CONTROLLER_HERE_IS_STATUS:
		HandleHereIsStatus(pak, link);
		return;
	}

}

static void RequestStatus(ClusterShardSummary *pShard)
{
	ClusterShardSummary_ControllerInternalStuff *pInternalStuff = (ClusterShardSummary_ControllerInternalStuff*)pShard->pUserData;
	Packet *pOutPack = pktCreate(pInternalStuff->pLink, SHARDCLUSTER_CONTROLLER_REQUEST_STATUS);
	pktSend(&pOutPack);
	pInternalStuff->iLastStatusRequestedTime = timeSecondsSince2000();
	pInternalStuff->iLastStatusReceivedTime = 0;

}

void ControllerShardCluster_Tick(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	U32 iCurTime = timeSecondsSince2000();
	static U32 siLastPeriodicDataTime = 0;
	ClusterShardStatus *pLocalStatusToSend = NULL;
	bool bSomethingLocalHadChanged = false;
	bool bRecalcPeriodicData = false;

	if (iCurTime - siLastPeriodicDataTime >= SHARDCLUSTER_PERIODICSTATUS_UPDATE_FREQ)
	{
		siLastPeriodicDataTime = iCurTime;
		bRecalcPeriodicData = true;
		sbSomethingLocalChanged = true;
	}



	if (sbSomethingLocalChanged)
	{
		bSomethingLocalHadChanged = true;
		sbSomethingLocalChanged = false;
		pLocalStatusToSend = FindLocalShardStatus(bRecalcPeriodicData);
	}
		

	FOR_EACH_IN_EARRAY(spOverview->ppShards, ClusterShardSummary, pShard)
	{
		if (pShard->eState != CLUSTERSHARDSTATE_THATS_ME)
		{
			ClusterShardSummary_ControllerInternalStuff *pInternalStuff = (ClusterShardSummary_ControllerInternalStuff*)pShard->pUserData;

			if (!pInternalStuff->pLinkDebugName)
			{
				estrPrintf(&pInternalStuff->pLinkDebugName, "ShardCluster link to %s", pShard->pShardName);
			}

			if (!pInternalStuff->iConnectionAttemptedBeganTime)
			{
				pInternalStuff->iConnectionAttemptedBeganTime = iCurTime;
			}

			if (commConnectFSMForTickFunctionWithRetrying(&pInternalStuff->pFSM, &pInternalStuff->pLink,
				pInternalStuff->pLinkDebugName, 20.0f, comm_controller, LINKTYPE_SHARD_NONCRITICAL_1MEG, LINK_FORCE_FLUSH,
				pShard->pControllerHostName, DEFAULT_SHARDCLUSTER_PORT_CONTROLLERS,
				ControllerShardClusterHandleMsg, NULL, NULL, 0, NULL, NULL, NULL, NULL))
			{
				if (pLocalStatusToSend)
				{
					Packet *pReturnPak = pktCreate(pInternalStuff->pLink, SHARDCLUSTER_CONTROLLER_HERE_IS_STATUS);
					ParserSendStructSafe(parse_ClusterShardStatus, pReturnPak, pLocalStatusToSend);
					pktSend(&pReturnPak);
				}

				//if we have only just now connected, reset all the internal timers, request status
				if (pShard->eState != CLUSTERSHARDSTATE_CONNECTED)
				{
					pShard->eState = CLUSTERSHARDSTATE_CONNECTED;
					sbSomethingChanged = true;

					pInternalStuff->iConnectionAttemptedBeganTime = 0;
					pInternalStuff->iLastStatusRequestedTime = 0;
					pInternalStuff->iLastStatusReceivedTime = 0;
					pInternalStuff->iLastAlertTime = 0;	
					RequestStatus(pShard);
					break;
				}

				//if we've requested status and not heard back for a long time, alert, then re-request status
				if (pInternalStuff->iLastStatusReceivedTime < pInternalStuff->iLastStatusRequestedTime && 
					pInternalStuff->iLastStatusRequestedTime &&
					pInternalStuff->iLastStatusRequestedTime < iCurTime - SECS_BEFORE_NO_RESPONSE_ALERT)
				{
					if (pInternalStuff->iLastAlertTime < iCurTime - SECS_BETWEEN_ALERTS)
					{
						pInternalStuff->iLastAlertTime = iCurTime;
						CRITICAL_NETOPS_ALERT("SHARD_CLUSTER_NO_RESPONSE", 
							"Requested shard status from %s %d seconds ago, haven't heard back",
							pShard->pShardName, SECS_BEFORE_NO_RESPONSE_ALERT);
					}

					RequestStatus(pShard);
				}
				//if we HAVE received status, wait a while, then request status
				else if (pInternalStuff->iLastStatusReceivedTime && pInternalStuff->iLastStatusReceivedTime < iCurTime - SECS_BETWEEN_STATUS_REQUESTS)
				{
					RequestStatus(pShard);
				}
			}
			else
			{
				if (pShard->eState == CLUSTERSHARDSTATE_CONNECTED)
				{
					CRITICAL_NETOPS_ALERT("SHARD_CLUSTER_DISCONNECT",
						"Lost intra-shard connection to cluster shard %s", pShard->pShardName);
					pShard->eState = CLUSTERSHARDSTATE_DISCONNECTED;

					StructDestroySafe(parse_ClusterShardStatus, &pShard->pMostRecentStatus);

					pInternalStuff->iConnectionAttemptedBeganTime = 0;
					pInternalStuff->iLastStatusRequestedTime = 0;
					pInternalStuff->iLastStatusReceivedTime = 0;
					pInternalStuff->iLastAlertTime = 0;
					sbSomethingChanged = true;
				}

				if (pInternalStuff->iLastAlertTime < iCurTime - SECS_BETWEEN_ALERTS)
				{
					if (pInternalStuff->iConnectionAttemptedBeganTime 
						&& pInternalStuff->iConnectionAttemptedBeganTime < iCurTime - SECS_BEFORE_NO_CONNECTION_ALERT)
					{
						pInternalStuff->iLastAlertTime = iCurTime;
						CRITICAL_NETOPS_ALERT("SHARD_CLUSTER_CANT_CONNECT", "Can't make intra-shard connection to %s despite trying for %u seconds",
							pShard->pShardName, SECS_BEFORE_NO_CONNECTION_ALERT);
					}
				}
			}
		}
	}
	FOR_EACH_END;

	if (sbSomethingChanged || bSomethingLocalHadChanged)
	{
		GlobalType eType;

		if (bSomethingLocalHadChanged)
		{
			ClusterShardSummary *pShard = FindThatsMeShard();
			if (pShard)
			{
				if (pShard->pMostRecentStatus)
				{
					StructCopy(parse_ClusterShardStatus, FindLocalShardStatus(false), pShard->pMostRecentStatus, 0, 0, 0);
				}
				else
				{
					pShard->pMostRecentStatus = StructClone(parse_ClusterShardStatus, FindLocalShardStatus(false));
				}
			}
		}


		for (eType = 0; eType < GLOBALTYPE_MAXTYPES; eType++)
		{
			if (gServerTypeInfo[eType].pInformMeAboutShardCluster_State)
			{
				TrackedServerState *pServer = gpServersByType[eType];

				while (pServer)
				{
					if (pServer->bBeganInformingAboutShardCluster)
					{
						SendOverviewToServer(pServer);
					}

					pServer = pServer->pNext;
				}
			}
		}

		sbSomethingChanged = false;
	}


}

static ClusterShardSummary *FindThatsMeShard(void)
{
	if (!spOverview)
	{
		return NULL;
	}

	FOR_EACH_IN_EARRAY(spOverview->ppShards, ClusterShardSummary, pShard)
	{
		if (pShard->eState == CLUSTERSHARDSTATE_THATS_ME)
		{
			return pShard;
		}
	}
	FOR_EACH_END;

	return NULL;
}

void ControllerShardCluster_Startup(void)
{
	ClusterShardSummary *pShard;

	spOverview = StructCreate(parse_Cluster_Overview);

	pShard = StructCreate(parse_ClusterShardSummary);
	pShard->pShardName = strdup(GetShardNameFromShardInfoString());

	pShard->eState = CLUSTERSHARDSTATE_THATS_ME;
	pShard->eShardType = ShardCluster_GetShardType();
	pShard->pMostRecentStatus = StructClone(parse_ClusterShardStatus, FindLocalShardStatus(true));

	eaPush(&spOverview->ppShards, pShard);

	FOR_EACH_IN_EARRAY(sppOtherShards, OtherShardInfo, pOtherShard)
	{
		pShard = StructCreate(parse_ClusterShardSummary);
		pShard->pShardName = strdup(pOtherShard->pShardName);
		pShard->pControllerHostName = strdup(pOtherShard->pMachineName);
		pShard->eState = CLUSTERSHARDSTATE_NEVER_CONNECTED;
		pShard->pUserData = StructCreate(parse_ClusterShardSummary_ControllerInternalStuff);

		eaPush(&spOverview->ppShards, pShard);
	}
	FOR_EACH_END;

	spControllerShardClusterListen = commListen(comm_controller,
		LINKTYPE_SHARD_CRITICAL_1MEG, LINK_FORCE_FLUSH,DEFAULT_SHARDCLUSTER_PORT_CONTROLLERS,
			ControllerShardClusterHandleMsg, NULL, NULL, 0);

	TimedCallback_Add(ControllerShardCluster_Tick, NULL, 1.0f);

	resRegisterDictionaryForEArray("ClusterShards", RESCATEGORY_SYSTEM, 0, &spOverview->ppShards, parse_ClusterShardSummary);
	

}

void ControllerShardCluster_AddToCommandLine(char **ppCommandLine)
{
	if (spOverview)
	{
		char *pStructString = NULL;
		char *pEscaped = NULL;

		ParserWriteText(&pStructString, parse_Cluster_Overview, spOverview, 0, 0, 0);
		estrSuperEscapeString(&pEscaped, pStructString);

		estrConcatf(ppCommandLine, " -SetShardClusterOverview_SuperEsc %s ", pEscaped);

		estrDestroy(&pStructString);
		estrDestroy(&pEscaped);
	}

}

void ControllerShardCluster_InformServer(TrackedServerState *pServer)
{
	if (spOverview)
	{
		SendOverviewToServer(pServer);
	}
}


void ControllerShardCluster_SomethingLocalChanged(void)
{
	sbSomethingLocalChanged = true;
}



#include "Controller_ShardCluster_c_ast.c"
