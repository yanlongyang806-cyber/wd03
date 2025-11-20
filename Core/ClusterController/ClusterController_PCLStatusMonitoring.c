#include "ClusterController_PCLStatusMonitoring.h"
#include "ClusterController_PCLStatusMonitoring_h_ast.h"
#include "PatchClientLibStatusMonitoring_h_ast.h"
#include "CrypticPorts.h"
#include "net/net.h"
#include "textparser.h"
#include "earray.h"
#include "utils.h"

static ClusterControllerPCLState *spPCLState = NULL;


static void ClusterController_PCLStatusMonitoringCB(PCLStatusMonitoringUpdate *pStatus)
{
	char **ppStrings = NULL;
	char *pShardName;
	char machineAndTaskName[512];
	ClusterControllerShardPCLState *pShardState;
	ClusterControllerMachineAndTaskPCLState *pMachineAndTask;

	DivideString(pStatus->internalStatus.pMyIDString, ":", &ppStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
	if (eaSize(&ppStrings) != 3)
	{
		eaDestroyEx(&ppStrings, NULL);
		return;
	}

	pShardName = ppStrings[0];
	pShardState = eaIndexedGetUsingString(&spPCLState->ppShards, pShardName);

	if (!pShardState)
	{
		pShardState = StructCreate(parse_ClusterControllerShardPCLState);
		pShardState->pShardName = strdup(pShardName);
		eaPush(&spPCLState->ppShards, pShardState);
	}

	sprintf(machineAndTaskName, "%s:%s", ppStrings[1], ppStrings[2]);
	pMachineAndTask = eaIndexedGetUsingString(&pShardState->ppTasks, machineAndTaskName);

	if (!pMachineAndTask)
	{
		pMachineAndTask = StructCreate(parse_ClusterControllerMachineAndTaskPCLState);
		pMachineAndTask->pMachineAndTask = strdup(machineAndTaskName);
		eaPush(&pShardState->ppTasks, pMachineAndTask);
	}

	if (pStatus->internalStatus.eState == PCLSMS_DELETING)
	{
		eaIndexedRemoveUsingString(&pShardState->ppTasks, machineAndTaskName);
		StructDestroy(parse_ClusterControllerMachineAndTaskPCLState, pMachineAndTask);

		if (eaSize(&pShardState->ppTasks) == 0)
		{
			eaIndexedRemoveUsingString(&spPCLState->ppShards, pShardName);
			StructDestroy(parse_ClusterControllerShardPCLState, pShardState);
		}
	}
	else
	{
		pMachineAndTask->eState = pStatus->internalStatus.eState;
		SAFE_FREE(pMachineAndTask->pStatusString);
		pMachineAndTask->pStatusString = strdup(pStatus->internalStatus.pUpdateString);
	}



	eaDestroyEx(&ppStrings, NULL);
}

void ClusterController_PCLStatusMonitoring_Begin(void)
{
	U32 *piTimeouts = NULL;
	ea32Push(&piTimeouts, 120);
	
	PCLStatusMonitoring_Begin(commDefault(), ClusterController_PCLStatusMonitoringCB, CLUSTERCONTROLLER_PATCHSTATUS_PORT, piTimeouts, 
		120, 120);
	ea32Destroy(&piTimeouts);

	spPCLState = StructCreate(parse_ClusterControllerPCLState);
}

void ClusterController_PCLStatusMonitoring_Tick(void)
{	
	PCLStatusMonitoring_Tick();
}



ClusterControllerPCLState *ClusterController_GetPCLStatus(void)
{
	return spPCLState;

}

#include "ClusterController_PCLStatusMonitoring_h_ast.c"
