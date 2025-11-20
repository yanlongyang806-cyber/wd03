#include "Controller_PCLStatusMonitoring.h"
#include "../../libs/PatchClientLib/PatchClientLibStatusMonitoring.h"
#include "PatchClientLibStatusMonitoring_h_ast.h"
#include "Controller.h"
#include "CrypticPorts.h"
#include "structDefines.h"
#include "net.h"
#include "sock.h"
#include "utilitiesLib.h"
#include "Alerts.h"
#include "Controller_ClusterController.h"

//__CATEGORY Stuff relating to the controller monitoring patching
//Seconds before issuing an alert when patching stalls or does not start
static int siPCLFailTime = 120;
AUTO_CMD_INT(siPCLFailTime, PCLFailTime) ACMD_CONTROLLER_AUTO_SETTING(PCLMonitoring);


static int sbDumpAllPCLStatusUpdates = false;
AUTO_CMD_INT(sbDumpAllPCLStatusUpdates, DumpAllPCLStatusUpdates);

void Controller_PCStatusCB(PCLStatusMonitoringUpdate *pStatus)
{
	if (sbDumpAllPCLStatusUpdates)
	{
		printf("Got %s from %s: %s\n", StaticDefineIntRevLookup(PCLStatusMonitoringStateEnum, pStatus->internalStatus.eState), pStatus->internalStatus.pMyIDString,
			pStatus->internalStatus.pUpdateString);
	}

	switch (pStatus->internalStatus.eState)
	{

	xcase PCLSMS_FAILED:
		CRITICAL_NETOPS_ALERT("STARTUP_PATCHING_FAILED", "PCL monitoring reports that patching task %s has failed because: %s",
			pStatus->internalStatus.pMyIDString, pStatus->internalStatus.pUpdateString);
		
	xcase PCLSMS_FAILED_TIMEOUT:
		if (pStatus->iNumUpdatesReceived)
		{
			CRITICAL_NETOPS_ALERT("STARTUP_PATCHING_FAILED", "Monitoring reports that patching task %s has not updated for %d seconds, presumed failed",
				pStatus->internalStatus.pMyIDString, pStatus->iTimeoutLength);
		}
		else
		{
			CRITICAL_NETOPS_ALERT("STARTUP_PATCHING_FAILED", "Monitoring reports that patching task %s never started (waited %d seconds)",
				pStatus->internalStatus.pMyIDString, pStatus->iTimeoutLength);
		}
	}
}


char *Controller_GetPCLStatusMonitoringCmdLine(char *pMachineName, char *pTaskName)
{
	static char *spRetVal = NULL;
	static char localIP[32] = "";

	if (!localIP[0])
	{
		strcpy(localIP, makeIpStr(getHostLocalIp()));
	}

	estrPrintf(&spRetVal, " -beginPatchStatusReporting \"%s: %s\" localhost %d -beginPatchStatusReporting \"%s: %s\" %s %d ",
		GetShardNameFromShardInfoString(), pTaskName, DEFAULT_MACHINESTATUS_PATCHSTATUS_PORT,
		pTaskName, pMachineName, localIP, DEFAULT_CONTROLLER_PATCHSTATUS_PORT);

	if (Controller_GetClusterControllerName())
	{
		estrConcatf(&spRetVal, " -beginPatchStatusReporting \"%s:%s:%s\" %s %d ",
			GetShardNameFromShardInfoString(), pMachineName, pTaskName, Controller_GetClusterControllerName(),
			CLUSTERCONTROLLER_PATCHSTATUS_PORT);
	}

	return spRetVal;
}

void Controller_BeginPCLStatusMonitoring(void)
{
	int *piTimeOuts = NULL;
	ATOMIC_INIT_BEGIN;

	if (siPCLFailTime)
	{
		ea32Push(&piTimeOuts, siPCLFailTime);
	}

	PCLStatusMonitoring_Begin(comm_controller, Controller_PCStatusCB, DEFAULT_CONTROLLER_PATCHSTATUS_PORT,
		piTimeOuts, 120, 120);

	ea32Destroy(&piTimeOuts);
	ATOMIC_INIT_END;
}

void Controller_AddPCLStatusMonitoringTask(char *pMachineName, char *pTaskName)
{
	Controller_BeginPCLStatusMonitoring();

	PCLStatusMonitoring_Add(STACK_SPRINTF("%s: %s", pTaskName, pMachineName));
}

void Controller_PCLStatusMonitoringTick(void)
{
	static U32 siLastTime = 0;

	if (siLastTime < timeSecondsSince2000())
	{
		PCLStatusMonitoring_Tick();
		siLastTime = timeSecondsSince2000();
	}
}

bool Controller_CheckTaskFailedByName(char *pMachineName, char *pTaskName)
{
	PCLStatusMonitoringUpdate *pUpdate;
	pUpdate = PCLStatusMonitoring_FindStatusByName(STACK_SPRINTF("%s: %s", pTaskName, pMachineName));

	if (!pUpdate)
	{
		return false;
	}

	switch (pUpdate->internalStatus.eState)
	{
	case PCLSMS_FAILED:
	case PCLSMS_FAILED_TIMEOUT:
		return true;
	}

	return false;
}


char *Controller_GetPCLStatusMonitoringDescriptiveStatusString(char *pMachineName, char *pTaskName)
{
	static char *spRetVal = NULL;
	PCLStatusMonitoringUpdate *pUpdate;
	U32 iCurTime;

	pUpdate = PCLStatusMonitoring_FindStatusByName(STACK_SPRINTF("%s: %s", pTaskName, pMachineName));

	if (!pUpdate)
	{
		return "Unknown patching task";
	}

	iCurTime = timeSecondsSince2000();

	switch (pUpdate->internalStatus.eState)
	{
	case PCLSMS_UPDATE:
		estrPrintf(&spRetVal, "Patching update %s ago: %s", GetPrettyDurationString(iCurTime - pUpdate->iMostRecentUpdateTime), pUpdate->internalStatus.pUpdateString);
		return spRetVal;

	case PCLSMS_FAILED:
		estrPrintf(&spRetVal, "Patching FAILED %s ago because: %s", GetPrettyDurationString(iCurTime - pUpdate->iSucceededOrFailedTime), pUpdate->internalStatus.pUpdateString);
		return spRetVal;

	case PCLSMS_SUCCEEDED:
		estrPrintf(&spRetVal, "Patching SUCCEEDED %s ago", GetPrettyDurationString(iCurTime - pUpdate->iSucceededOrFailedTime));
		return spRetVal;

	case PCLSMS_TIMEOUT:
		if (pUpdate->iNumUpdatesReceived)
		{
			estrPrintf(&spRetVal, "Patching hit predefined timeout value of %s... most recent update was %s", GetPrettyDurationString(pUpdate->iTimeoutLength),
				pUpdate->internalStatus.pUpdateString);
		}
		else
		{
			estrPrintf(&spRetVal, "Patching hit predefined timeout value of %s, never provided an update at all", GetPrettyDurationString(pUpdate->iTimeoutLength));
		}
		return spRetVal;

	case PCLSMS_FAILED_TIMEOUT:
		if (pUpdate->iNumUpdatesReceived)
		{
			estrPrintf(&spRetVal, "Patching FAILED due to timeout value of %s... most recent update was %s", GetPrettyDurationString(pUpdate->iTimeoutLength),
				pUpdate->internalStatus.pUpdateString);
		}
		else
		{
			estrPrintf(&spRetVal, "Patching FAILED due to timeout value of %s, never provided an update at all", GetPrettyDurationString(pUpdate->iTimeoutLength));
		}
		return spRetVal;

	case PCLSMS_INTERNAL_CREATE:
		estrPrintf(&spRetVal, "Patching began %s ago, no update yet received", GetPrettyDurationString(iCurTime - pUpdate->iTimeBegan));
		return spRetVal;
	}

	return "Corrupt patching state";
}


void Controller_ResetPCLStatusMonitoringForTask(char *pTaskName)
{
	PCLStatusMonitoringIterator *pIter = NULL;
	PCLStatusMonitoringUpdate *pUpdate = NULL;

	while ((pUpdate = PCLStatusMonitoring_GetNextUpdateFromIterator(&pIter)))
	{
		if (strStartsWith(pUpdate->internalStatus.pMyIDString, pTaskName))
		{
			pUpdate->bDestroyRequested = true;
		}
	}
}