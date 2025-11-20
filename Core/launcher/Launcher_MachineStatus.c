#include "..\..\Utilities\MachineStatus\MachineStatusPub.h"
#include "MachineStatusPub_h_ast.h"
#include "proclist.h"
#include "performance.h"
#include "net.h"
#include "structNet.h"
#include "CrypticPorts.h"
#include "GlobalComm.h"
#include "timing.h"
#include "utilitiesLib.h"
#include "Launcher_MachineStatus.h"
#include "sysutil.h"


static CommConnectFSM *pMachineStatusConnectFSM = NULL;
static NetLink *pMachineStatusNetLink = NULL;

static U32 siLastTimeSentStatus = 0;

//__CATEGORY Stuff for launchers
//How often to send local machine status to MachineStatus.exe (seconds) (0 = never)
static int siMachineStatusSendDelay = 1;
AUTO_CMD_INT(siMachineStatusSendDelay, MachineStatusSendDelay) ACMD_AUTO_SETTING(Launcher, LAUNCHER);


MachineStatusUpdate *GetStatusStructForMachineStatus(void)
{
	static MachineStatusUpdate *pRetVal = NULL;
	int i;

	COARSE_AUTO_START_FUNC();

	if (!pRetVal)
	{
		pRetVal = StructCreate(parse_MachineStatusUpdate);
	}
	else
	{
		StructReset(parse_MachineStatusUpdate, pRetVal);
	}

	pRetVal->pShardName = strdup(GetShardNameFromShardInfoString());
	pRetVal->iTime = timeSecondsSince2000();

	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
	{
		ProcessInfo *pProcess = process_list.ppProcessInfos[i];

		if (pProcess->container_id >= 0)
		{
			if (pProcess->container_type == GLOBALTYPE_GAMESERVER)
			{
				ea32Push(&pRetVal->pGameServerPIDs, pProcess->process_id);
				pRetVal->iGSRam += pProcess->mem_used_phys * 1024;
				pRetVal->fGSCpu += pProcess->fCPUUsage;
				pRetVal->fGSCpuLast60 += pProcess->fCPUUsageLastMinute;

			}
			else
			{
				ea32Push(&pRetVal->pOtherPIDs, pProcess->process_id);
			}
		}
	}

	pRetVal->iPIDOfIgnoredServer = PidOfProcessBeingIgnored();


	COARSE_AUTO_STOP_FUNC();
	return pRetVal;
}

static void HandleFromMachineStatusMessage(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	switch (cmd)
	{
	xcase FROM_MACHINESTATUS_HIDE_ALL:
		HideAllProcesses(true);
		hideConsoleWindow();

	xcase FROM_MACHINESTATUS_SHOW_ALL:
		HideAllProcesses(false);
		showConsoleWindow();
	}
}

void LauncherMachineStatus_Update(void)
{
	Packet *pPkt;
	MachineStatusUpdate *pStatus;

	if (!siMachineStatusSendDelay)
	{
		return;
	}

	if (!commConnectFSMForTickFunctionWithRetrying(&pMachineStatusConnectFSM, &pMachineStatusNetLink, "link to MachineStatus.exe",
		2.0f, commDefault(), LINKTYPE_SHARD_NONCRITICAL_1MEG, LINK_FORCE_FLUSH,"127.00.0.1",
		DEFAULT_MACHINESTATUS_PORT,HandleFromMachineStatusMessage,0,0,0, NULL, 0, NULL, 0))
	{
		return;
	}

	

	pStatus = GetStatusStructForMachineStatus();
	if (!pStatus)
	{

		return;
	}

	if (siLastTimeSentStatus <= timeSecondsSince2000() - siMachineStatusSendDelay)
	{
		siLastTimeSentStatus = timeSecondsSince2000();
		pPkt = pktCreate(pMachineStatusNetLink, TO_MACHINESTATUS_HERE_IS_UPDATE);
		ParserSendStructSafe(parse_MachineStatusUpdate, pPkt, pStatus);
		pktSend(&pPkt);
	}		

}