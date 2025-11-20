#pragma once

typedef struct Packet Packet;
typedef struct Shard Shard;
void ClusterController_HandleHereAreShardVariables(Packet *pPack, Shard *pShard);