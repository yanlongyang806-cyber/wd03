#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef U32 ContainerID;

typedef void (*BootPlayerCB)(bool success, void *userData);

// Boot a logged in player from a remote shard.
void aslLogin2_BootPlayer(ContainerID playerID, const char *shardName, const char *message, BootPlayerCB cbFunc, void *userData);

// Boot the player from all login servers in the cluster.
void aslLogin2_BootAccountFromAllLoginServers(ContainerID accountID, ContainerID excludeServerID);