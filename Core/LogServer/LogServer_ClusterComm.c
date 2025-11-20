#include "LogServer_ClusterComm.h"
#include "net/net.h"
#include "GlobalTypes.h"
#include "EString.h"
#include "CrypticPorts.h"
#include "Alerts.h"
#include "ServerLib.h"
#include "file.h"
#include "LogServer_ClusterComm_h_ast.h"
#include "LogParsing.h"
#include "logserver.h"
#include "svrGlobalInfo.h"
#include "svrGlobalInfo_h_Ast.h"


//__CATEGORY Settings related to comm between in-shard and cluster-level log servers
//if a packet of messages has been gathering up for this long, always send it
static U32 siMaxPacketAgeBeforeSendingMessages = 15;
AUTO_CMD_INT(siMaxPacketAgeBeforeSendingMessages, MaxPacketAgeBeforeSendingMessages) ACMD_AUTO_SETTING(LogServerClusterComm, LOGSERVER);

//if a packet of messages gets to be this many bytes large, send it
static U32 siMaxPacketSizeBeforeSendingMessages = 500000;
AUTO_CMD_INT(siMaxPacketSizeBeforeSendingMessages, MaxPacketSizeBeforeSendingMessages) ACMD_AUTO_SETTING(LogServerClusterComm, LOGSERVER);

//if a packet of messages gets to be this many bytes large, and can't be sent because
//we aren't connected, generate a warning alert
static U32 siMaxUnsentPacketSizeBeforeWarning = 2000000;
AUTO_CMD_INT(siMaxUnsentPacketSizeBeforeWarning, MaxUnsentPacketSizeBeforeWarning) ACMD_AUTO_SETTING(LogServerClusterComm, LOGSERVER);

//if a packet of messages gets to be this many bytes large, and can't be sent because
//we aren't connected, throw it out and generate an alert
static U32 siMaxUnsentPacketSizeBeforeDiscard = 4000000;
AUTO_CMD_INT(siMaxUnsentPacketSizeBeforeDiscard, MaxUnsentPacketSizeBeforeDiscard) ACMD_AUTO_SETTING(LogServerClusterComm, LOGSERVER);


bool gbYouAreClusterLevelLogServer = false;
AUTO_CMD_INT(gbYouAreClusterLevelLogServer, YouAreClusterLevelLogServer) ACMD_COMMANDLINE;

bool gbSendLogsToClusterLevelLogServer = false;
static char *spClusterLevelLogServerName = NULL;

typedef struct ClusterCommFilterList
{
	bool bCategories[LOG_LAST];
	bool bServerTypes[GLOBALTYPE_MAXTYPES];
	bool bServerCategoryPairs[GLOBALTYPE_MAXTYPES][LOG_LAST];
} ClusterCommFilterList;

typedef struct ClusterCommFilter
{
	ClusterCommFilterList blackList;
	ClusterCommFilterList whiteList;
	bool bIncludeOthers;
} ClusterCommFilter;

static ClusterCommFilter *spFilter = NULL;

bool ClusterLevelLogServerIsInterested(GlobalType eSourceServerType, enumLogCategory eCategory)
{
	assert(spFilter);
	if (spFilter->whiteList.bServerCategoryPairs[eSourceServerType][eCategory])
	{
		return true;
	}

	if (spFilter->blackList.bServerCategoryPairs[eSourceServerType][eCategory])
	{
		return false;
	}

	if (spFilter->bIncludeOthers)
	{
		if (spFilter->whiteList.bServerTypes[eSourceServerType])
		{
			return true;
		}

		if (spFilter->whiteList.bCategories[eCategory])
		{
			return true;
		}

		if (spFilter->blackList.bServerTypes[eSourceServerType])
		{
			return false;
		}

		if (spFilter->blackList.bCategories[eCategory])
		{
			return false;
		}

		return true;
	}
	else
	{
		if (spFilter->blackList.bServerTypes[eSourceServerType])
		{
			return false;
		}

		if (spFilter->blackList.bCategories[eCategory])
		{
			return false;
		}

		if (spFilter->whiteList.bServerTypes[eSourceServerType])
		{
			return true;
		}

		if (spFilter->whiteList.bCategories[eCategory])
		{
			return true;
		}



		return false;
	}


}

void LoadConfigIntoList(char *pMainFileName, LogServerClusterCommFilterConfigList *pConfig, ClusterCommFilterList *pList)
{
	int i;

	if (!pConfig)
	{
		return;
	}

	FOR_EACH_IN_EARRAY(pConfig->ppServerCategoryPairs, char, pPair)
	{
		GlobalType eServerType;
		enumLogCategory eCategory;

		if (!SubdivideLoggingKey(pPair, &eServerType, &eCategory))
		{
			assertmsgf(0, "While loading config file %s, couldn't parse server type/category pair %s",
				pMainFileName, pPair);
		}

		pList->bServerCategoryPairs[eServerType][eCategory] = true;
	}
	FOR_EACH_END;

	for (i = 0; i < ea32Size(&pConfig->pLogCategoryTypes); i++)
	{
		pList->bCategories[pConfig->pLogCategoryTypes[i]] = true;
	}

	for (i = 0; i < ea32Size(&pConfig->pServerTypes); i++)
	{
		pList->bServerTypes[pConfig->pServerTypes[i]] = true;
	}
}


void AttemptToLoadConfigFileForClusterSending(char *pConfigFileName)
{
	LogServerClusterCommFilterConfig *pConfig = StructCreate(parse_LogServerClusterCommFilterConfig);

	if (!ParserReadTextFile(pConfigFileName, parse_LogServerClusterCommFilterConfig, pConfig, 0))
	{

		char *pTempString = NULL;
		ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
		ParserReadTextFile(pConfigFileName, parse_LogServerClusterCommFilterConfig, pConfig, 0);
		ErrorfPopCallback();
		
		assertmsgf(0, "Fatal error while reading %s: %s", pConfigFileName, pTempString);

	}

	assertmsgf(pConfig->bIncludeAllOthers ^ pConfig->bExcludeAllOthers, "Tried to load config from %s, must have exactly one of ExcludeAllOthers or IncludeAllOthers set",
		pConfigFileName);

	spFilter = calloc(sizeof(ClusterCommFilter), 1);
	spFilter->bIncludeOthers = pConfig->bIncludeAllOthers;

	LoadConfigIntoList(pConfigFileName, pConfig->pWhiteList, &spFilter->whiteList);
	LoadConfigIntoList(pConfigFileName, pConfig->pBlackList, &spFilter->blackList);
}


AUTO_COMMAND;
void SetClusterLevelLogServerName(char *pName, char *pConfigFileName)
{
	AttemptToLoadConfigFileForClusterSending(pConfigFileName);

	estrCopy2(&spClusterLevelLogServerName, pName);
	gbSendLogsToClusterLevelLogServer = true;

	gGlobalInfo.pClusterServerStatus = StructCreate(parse_LogClusterServerStatus);
	gGlobalInfo.pClusterServerStatus->pClusterLogServerName = strdup(pName);
}

typedef struct MessagesForCluster_OneServerType
{
	GlobalType eServerType;

	U32 iPacketCreationTime; //when the current packet was created, ie, when we got the first messages currently in that
		//packet

	U32 iSizeWarningTime; //when we sent a warning that the packet was getting too big without having a connection

	Packet *pPack;
} MessagesForCluster_OneServerType;

static MessagesForCluster_OneServerType sMessagesForCluster[GLOBALTYPE_MAXTYPES] = {0};

static CommConnectFSM *spSendLogsToClusterLevelLogServerConnectFSM = NULL;
static NetLink *spSendLogsToClusterLevelLogServerNetLink = NULL;

static void MaybeSendPacket(MessagesForCluster_OneServerType *pMessages, NetLink *pLink, bool bForceSend)
{
	U32 iCurSize = pktGetSize(pMessages->pPack);
	U32 iCurAge = timeSecondsSince2000() - pMessages->iPacketCreationTime;

	if (iCurSize > siMaxPacketSizeBeforeSendingMessages || iCurAge > siMaxPacketAgeBeforeSendingMessages || bForceSend)
	{
		pktSendBits(pMessages->pPack, 32, 0);
		pktSendThroughLink(&pMessages->pPack, pLink);
		pMessages->iPacketCreationTime = 0;
		pMessages->iSizeWarningTime = 0;
	}

}


static void MabyeThrowOutPacket(MessagesForCluster_OneServerType *pMessages)
{
	U32 iCurSize = pktGetSize(pMessages->pPack);

	if (!siMaxUnsentPacketSizeBeforeDiscard)
	{
		return;
	}

	if (iCurSize > siMaxUnsentPacketSizeBeforeDiscard)
	{
		static char *spSize1 = NULL;
		static char *spSize2 = NULL;

		estrMakePrettyBytesString(&spSize1, (U64)iCurSize);
		estrMakePrettyBytesString(&spSize2, siMaxUnsentPacketSizeBeforeDiscard);

		CRITICAL_NETOPS_ALERT("DISCARDED_CLUSTERLOG_PKT", "We can not connect to the cluster-level log server at %s, and have built up %s of logs from server type %s. This exceeds the max (%s), so we are discarding them",
			spClusterLevelLogServerName, spSize1, GlobalTypeToName(pMessages->eServerType), spSize2);
		pktFree(&pMessages->pPack);
		pMessages->iPacketCreationTime = 0;
		pMessages->iSizeWarningTime = 0;
		return;
	}

	if (!siMaxUnsentPacketSizeBeforeWarning)
	{
		return;
	}

	if (iCurSize > siMaxUnsentPacketSizeBeforeWarning && !pMessages->iSizeWarningTime)
	{
		static char *spSize1 = NULL;
		static char *spSize2 = NULL;
		static char *spSize3 = NULL;
		static char *spTime1 = NULL;
		static char *spTime2 = NULL;
		float fEstimatedTimeUntilFull;

		U32 iTimeSinceCreation = timeSecondsSince2000() - pMessages->iPacketCreationTime;
		if (!iTimeSinceCreation)
		{
			iTimeSinceCreation = 1;
		}


		estrMakePrettyBytesString(&spSize1, iCurSize);
		estrMakePrettyBytesString(&spSize2, siMaxUnsentPacketSizeBeforeDiscard);

		timeSecondsDurationToPrettyEString(iTimeSinceCreation, &spTime1);

		fEstimatedTimeUntilFull = iTimeSinceCreation;
		fEstimatedTimeUntilFull *= (siMaxUnsentPacketSizeBeforeDiscard - siMaxUnsentPacketSizeBeforeWarning) / siMaxUnsentPacketSizeBeforeWarning;
	

		timeSecondsDurationToPrettyEString((U32)fEstimatedTimeUntilFull, &spTime2);

		WARNING_NETOPS_ALERT("TOO_LARGE_CLUSTERLOG_PKT", "We can-not connect to cluster-level log server %s. We have built up %s of logs from server type %s in the past %s. If this grows to %s, they will be discarded. This will happen in approximately %s",
			spClusterLevelLogServerName, spSize1, GlobalTypeToName(pMessages->eServerType), spTime1, spSize2, spTime2);

		pMessages->iSizeWarningTime = timeSecondsSince2000();
	}

}


void SendLogToClusterLevelLogServer(GlobalType eSourceServerType, U32 iTime, enumLogCategory eCategory, const char *pMessageString)
{
	MessagesForCluster_OneServerType *pMessages = &sMessagesForCluster[eSourceServerType];

	if (!pMessages->pPack)
	{
		pMessages->pPack = pktCreate(NULL, LOGCLIENT_LOGPRINTF);
		pktSetHasVerify(pMessages->pPack, isDevelopmentMode());
		PutContainerTypeIntoPacket(pMessages->pPack, eSourceServerType);
	}

	pktSendBits(pMessages->pPack, 32, iTime);
	pktSendBits(pMessages->pPack, 32, eCategory);
	pktSendString(pMessages->pPack, pMessageString);
}



void SendLogsToClusterLevelLogServer_Tick(void)
{
	static int siNextGlobalTypeToCheck = 0;

	siNextGlobalTypeToCheck++;
	if (siNextGlobalTypeToCheck == GLOBALTYPE_MAXTYPES)
	{
		siNextGlobalTypeToCheck = 0;
	}


	if (commConnectFSMForTickFunctionWithRetrying(&spSendLogsToClusterLevelLogServerConnectFSM, &spSendLogsToClusterLevelLogServerNetLink, 
		"link to cluster-level log server",
			2.0f, commDefault(), LINKTYPE_SHARD_CRITICAL_20MEG, LINK_FORCE_FLUSH,
			spClusterLevelLogServerName, DEFAULT_LOGSERVER_PORT, NULL,NULL,0,0, NULL, 0, NULL, 0))
	{
		if (sMessagesForCluster[siNextGlobalTypeToCheck].pPack)
		{
			MaybeSendPacket(&sMessagesForCluster[siNextGlobalTypeToCheck], spSendLogsToClusterLevelLogServerNetLink, false);
		}

		gGlobalInfo.pClusterServerStatus->bCurrentlyConnected = true;
		gGlobalInfo.pClusterServerStatus->iLastConnectionTime = timeSecondsSince2000();;

	}
	else
	{
		gGlobalInfo.pClusterServerStatus->bCurrentlyConnected = false;

		if (sMessagesForCluster[siNextGlobalTypeToCheck].pPack)
		{
			MabyeThrowOutPacket(&sMessagesForCluster[siNextGlobalTypeToCheck]);
		}
	}
}


void SendLogsToClusterLevelLogServer_TickFlush(void)
{

	if (commConnectFSMForTickFunctionWithRetrying(&spSendLogsToClusterLevelLogServerConnectFSM, &spSendLogsToClusterLevelLogServerNetLink, 
		"link to cluster-level log server",
			2.0f, commDefault(), LINKTYPE_SHARD_CRITICAL_20MEG, LINK_FORCE_FLUSH,
			spClusterLevelLogServerName, DEFAULT_LOGSERVER_PORT, NULL,NULL,0,0, NULL, 0, NULL, 0))
	{
		GlobalType eType;

		for (eType = 0; eType < GLOBALTYPE_MAXTYPES; eType++)
		{
			if (sMessagesForCluster[eType].pPack)
			{
				MaybeSendPacket(&sMessagesForCluster[eType], spSendLogsToClusterLevelLogServerNetLink, true);
			}
		}
	}
	else
	{
		//make an alert of some sort happen here
	}
}

void LogServer_ClusterTick(void)
{
	if (gbSendLogsToClusterLevelLogServer)
	{
		if (gTimeToDie)
		{
			SendLogsToClusterLevelLogServer_TickFlush();
		}
		else
		{
			SendLogsToClusterLevelLogServer_Tick();
		}
	}
	
}


#include "LogServer_ClusterComm_h_ast.c"
