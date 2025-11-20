#pragma once
#include "GlobalTypes.h"

typedef struct Packet Packet;
typedef struct NetLink NetLink;

void TransactionServer_ShardCluster_Tick(void);

//creates a new packet, copies the packet contents, ends up being treated the same as a simple packet on the other end
//
//returns true if the shard was found, false otherwise
bool TransactionServer_ShardCluster_SendRemoteCommandPacket(Packet *pPacket, char *pCommandName, char *pShardName, 
	GlobalType eContainerType, ContainerID iID, S64 iErrorCB, S64 iErrorUserData1, S64 iErrorUserData2,
	int iReturnConnectionIndex, int iReturnConnectionID);

void TransServerShardCluster_SendReceipt(NetLink *pLink, U32 iReceiptID, bool bSucceeded);