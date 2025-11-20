#include "TransactionServer_ShardCluster.h"
#include "ShardCluster.h"
#include "earray.h"
#include "textparser.h"
#include "net.h"
#include "ShardCluster_h_ast.h"
#include "TransactionServer_ShardCLuster_c_ast.h"
#include "alerts.h"
#include "ResourceInfo.h"
#include "sock.h"
#include "crypticPorts.h"
#include "ServerLib.h"
#include "TransactionServer.h"
#include "error.h"

static bool sbInitted = false;

AUTO_STRUCT;
typedef struct InterShardReceipt
{
	U32 iReceiptID; AST(KEY)
	S64 iErrorCB;
	S64 iErrorUserData1;
	S64 iErrorUserData2;
	int iReturnConnectionIndex;
	int iReturnConnectionID;
} InterShardReceipt;

//we only care about shards that have transaction servers
AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "ShardName, TransServerIP, Connected");
typedef struct TransactionServer_ShardClusterShard
{
	char *pShardName; AST(KEY)
	char *pLinkDebugName;
	U32 iTransServerIP; AST(FORMAT_IP)
	char *pTransServerIPStr;
	bool bMarked;
	NetLink *pLink; NO_AST
	CommConnectFSM *pFSM; NO_AST
	bool bConnected;

	U32 iNextReceiptID;
	InterShardReceipt **ppReceipts;
} TransactionServer_ShardClusterShard;

static TransactionServer_ShardClusterShard **sppShards = NULL;

static void CreateLocalShardInfo(char *pShardName, U32 iTransServerIP)
{
	TransactionServer_ShardClusterShard *pShard = StructCreate(parse_TransactionServer_ShardClusterShard);
	pShard->pShardName = strdup(pShardName);
	pShard->pTransServerIPStr = strdup(makeIpStr(iTransServerIP));
	pShard->iTransServerIP = iTransServerIP;
	pShard->bMarked = true;
	pShard->pLinkDebugName = strdupf("Trans server ShardCluster link to %s", pShardName);
	eaPush(&sppShards, pShard);
}

static void DestroyLocalShardInfo(char *pShardName)
{
	TransactionServer_ShardClusterShard *pShard = eaIndexedRemoveUsingString(&sppShards, pShardName);

	if (pShard)
	{
		commConnectFSMDestroy(&pShard->pFSM);
		linkRemove_wReason(&pShard->pLink, "DestroyLocalShardInfo");
	
		StructDestroy(parse_TransactionServer_ShardClusterShard, pShard);
	}
}

void TransServer_HandleRemoteCommandReceipt(NetLink *link, Packet *pak)
{
	U32 iReceiptIndex = pktGetBits(pak, 32);
	bool bSucceeded = pktGetBits(pak, 1);
	TransactionServer_ShardClusterShard *pShard = linkGetUserData(link);
	InterShardReceipt *pReceipt;

	if (!pShard)
	{
		AssertOrAlert("INTERSHARD_LINK_CORRUPTION", "Getting a remote command receipt on a link with no userData");
		return;
	}

	pReceipt = eaIndexedRemoveUsingInt(&pShard->ppReceipts, iReceiptIndex);

	if (!pReceipt)
	{
		AssertOrAlert("INTERSHARD_LINK_CORRUPTION", "Got an unknown remote command receipt from %s", pShard->pShardName);
		return;
	}

	if (!bSucceeded)
	{
		HandleGotFailureFromOtherShard(pReceipt->iReturnConnectionIndex, pReceipt->iReturnConnectionID,
			pReceipt->iErrorCB, pReceipt->iErrorUserData1, pReceipt->iErrorUserData2);
	}

	StructDestroy(parse_InterShardReceipt, pReceipt);
}


static void TransactionServerShardClusterHandleMsg(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	switch (cmd)
	{
	xcase SHARDCLUSTER_TRANSACTIONSERVER_HERE_IS_COMMAND:
		TransServer_HandleRemoteCommandFromOtherShard(link, pak);
	xcase SHARDCLUSTER_TRANSACTIONSERVER_COMMAND_RECEIPT:
		TransServer_HandleRemoteCommandReceipt(link, pak);
	}
}

void OVERRIDE_LATELINK_ShardClusterOverviewChanged(void)
{
	Cluster_Overview *pOverview = GetShardClusterOverview_IfInCluster();

	if (!pOverview)
	{
		return;
	}

	if (!sbInitted)
	{
		sbInitted = true;
		eaIndexedEnable(&sppShards, parse_TransactionServer_ShardClusterShard);
		resRegisterDictionaryForEArray("transServerClusterShards", RESCATEGORY_SYSTEM, 0, &sppShards, parse_TransactionServer_ShardClusterShard);
		commListen(commDefault(),
			LINKTYPE_SHARD_CRITICAL_1MEG, LINK_FORCE_FLUSH,DEFAULT_SHARDCLUSTER_PORT_TRANSSERVER,
			TransactionServerShardClusterHandleMsg, NULL, NULL, 0);
	}


	//check the overview compared to the shards we already know about... for any shard we didn't already know about,
	//add it and start connection attempt. Also alert and delete shards that have vanished
	FOR_EACH_IN_EARRAY(sppShards, TransactionServer_ShardClusterShard, pShard)
	{
		pShard->bMarked = false;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pOverview->ppShards, ClusterShardSummary, pClusterShard)
	{
		ClusterShardStatus *pStatus = pClusterShard->pMostRecentStatus;
		ClusterServerTypeStatus *pTransServers;
		ClusterServerStatus *pTransServer;
		TransactionServer_ShardClusterShard *pLocalInfoAboutShard;

		if (!pStatus)
		{
			continue;
		}

		pTransServers = eaIndexedGetUsingInt(&pStatus->ppServersByType, GLOBALTYPE_TRANSACTIONSERVER);
		if (!pTransServers)
		{
			continue;
		}

		pTransServer = pTransServers->ppServers[0];

		if (!pTransServer || !pTransServer->iIP)
		{
			continue;
		}

		pLocalInfoAboutShard = eaIndexedGetUsingString(&sppShards, pClusterShard->pShardName);
		if (pLocalInfoAboutShard)
		{
			if (pLocalInfoAboutShard->iTransServerIP != pTransServer->iIP)
			{
				CRITICAL_NETOPS_ALERT("CLUSTER_IP_CHANGED", "The IP of the trans server on cluster shard %s seems to have changed",
					pLocalInfoAboutShard->pShardName);
				DestroyLocalShardInfo(pLocalInfoAboutShard->pShardName);
				pLocalInfoAboutShard = NULL;
			}
			else
			{
				pLocalInfoAboutShard->bMarked = true;
			}
		}
		if (!pLocalInfoAboutShard)
		{
			CreateLocalShardInfo(pClusterShard->pShardName, pTransServer->iIP);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(sppShards, TransactionServer_ShardClusterShard, pShard)
	{
		if (!pShard->bMarked)
		{
			//no point in alerting here, controller already will have
			DestroyLocalShardInfo(pShard->pShardName);
		}
	}
	FOR_EACH_END;

}

void TransactionServerShardClusterConnect(NetLink *pLink, void *pUserData)
{
	linkSetKeepAliveSeconds(pLink, 30);
}

void TransactionServer_ShardCluster_Tick(void)
{
	FOR_EACH_IN_EARRAY(sppShards, TransactionServer_ShardClusterShard, pShard)
	{
		if (commConnectFSMForTickFunctionWithRetrying(&pShard->pFSM, &pShard->pLink,
				pShard->pLinkDebugName, 20.0f, commDefault(), LINKTYPE_SHARD_NONCRITICAL_1MEG, LINK_FORCE_FLUSH,
				pShard->pTransServerIPStr, DEFAULT_SHARDCLUSTER_PORT_TRANSSERVER,
				TransactionServerShardClusterHandleMsg, TransactionServerShardClusterConnect, NULL, 0, NULL, NULL, NULL, NULL))
		{
			pShard->bConnected = true;
			linkSetUserData(pShard->pLink, pShard);
		}
		else
		{
			pShard->bConnected = false;
		}
	}
	FOR_EACH_END;
}

U32 GetNewReceiptID(TransactionServer_ShardClusterShard *pShard, S64 iErrorCB, S64 iErrorUserData1, S64 iErrorUserData2,
	int iReturnConnectionIndex, int iReturnConnectionID)
{
	U32 iRetVal;
	InterShardReceipt *pReceipt;
	
	if (!iErrorCB)
	{
		return 0;
	}

	do
	{
		iRetVal = pShard->iNextReceiptID++;
	} 
	while (!iRetVal || eaIndexedGetUsingInt(&pShard->ppReceipts, iRetVal));

	pReceipt = StructCreate(parse_InterShardReceipt);
	pReceipt->iReceiptID = iRetVal;
	pReceipt->iErrorCB = iErrorCB;
	pReceipt->iErrorUserData1 = iErrorUserData1;
	pReceipt->iErrorUserData2 = iErrorUserData2;
	pReceipt->iReturnConnectionIndex = iReturnConnectionIndex;
	pReceipt->iReturnConnectionID = iReturnConnectionID;

	eaPush(&pShard->ppReceipts, pReceipt);

	return iRetVal;

}

bool TransactionServer_ShardCluster_SendRemoteCommandPacket(Packet *pPacket, char *pCommandName, char *pShardName, GlobalType eContainerType, ContainerID iID,
	S64 iErrorCB, S64 iErrorUserData1, S64 iErrorUserData2, int iReturnConnectionIndex, int iReturnConnectionID)
{
	TransactionServer_ShardClusterShard *pShard = eaIndexedGetUsingString(&sppShards, pShardName);
	Packet *pOutPack;
	U32 iReceiptID;

	if (!pShard || !pShard->bConnected)
	{
		return false;
	}


	iReceiptID = GetNewReceiptID(pShard, iErrorCB, iErrorUserData1, iErrorUserData2, iReturnConnectionIndex, iReturnConnectionID);
	

	pOutPack = pktCreate(pShard->pLink, SHARDCLUSTER_TRANSACTIONSERVER_HERE_IS_COMMAND);
	pktSendBits(pOutPack, 32, iReceiptID);
	pktSendString(pOutPack, pCommandName);
	PutContainerTypeIntoPacket(pOutPack, eContainerType);
	PutContainerTypeIntoPacket(pOutPack, iID);
	pktCopyRemainingRawBytesToOtherPacket(pOutPack, pPacket);
	pktSend(&pOutPack);

	return true;

}

void TransServerShardCluster_SendReceipt(NetLink *pLink, U32 iReceiptIndex, bool bSucceeded)
{
	Packet *pPak;

	if (!iReceiptIndex)
	{
		return;
	}

	pPak = pktCreate(pLink, SHARDCLUSTER_TRANSACTIONSERVER_COMMAND_RECEIPT);
	pktSendBits(pPak, 32, iReceiptIndex);
	pktSendBits(pPak, 1, bSucceeded);
	pktSend(&pPak);


}


#include "TransactionServer_ShardCLuster_c_ast.c"
