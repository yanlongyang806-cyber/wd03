#pragma once

typedef struct TrackedServerState TrackedServerState;

void ControllerShardCluster_InformServer(TrackedServerState *pServer);
void ControllerShardCluster_AddToCommandLine(char **ppCommandLine);

void ControllerShardCluster_Startup(void);

//tells the system that something local to this shard has changed with other shards will want to know about
void ControllerShardCluster_SomethingLocalChanged(void);

#include "Controller_ShardCluster.h"
