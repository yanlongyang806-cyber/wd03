#pragma once
#include "../../libs/PatchClientLib/PatchClientLibStatusMonitoring.h"



AUTO_STRUCT;
typedef struct ClusterControllerMachineAndTaskPCLState
{
	char *pMachineAndTask; AST(KEY)
	PCLStatusMonitoringState eState;
	char *pStatusString;
} ClusterControllerMachineAndTaskPCLState;

AUTO_STRUCT;
typedef struct ClusterControllerShardPCLState
{
	char *pShardName; AST(KEY)
	ClusterControllerMachineAndTaskPCLState **ppTasks;
} ClusterControllerShardPCLState;

AUTO_STRUCT;
typedef struct ClusterControllerPCLState
{
	ClusterControllerShardPCLState **ppShards;
} ClusterControllerPCLState;

void ClusterController_PCLStatusMonitoring_Tick(void);
ClusterControllerPCLState *ClusterController_GetPCLStatus(void);
void ClusterController_PCLStatusMonitoring_Begin(void);
