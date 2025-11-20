#include "controllerMirror.h"
#include "StashTable.h"
#include "ResourceInfo.h"
#include "ControllerPub_h_ast.h"
#include "autogen/GameServerLib_autogen_RemoteFuncs.h"
#include "autogen/ServerLib_autogen_RemoteFuncs.h"

static bool sbDoingControllerMirroring = false;

static StashTable gMachinesByName = NULL;
static StashTable sServersByName = NULL;
static StashTable sGameServersByName = NULL;
static StashTable sClientControllersByName = NULL;


AUTO_COMMAND_REMOTE;
void HereIsControllerMirrorInfo(ControllerMirrorInfo *pInfo)
{
	int i;

	//the ownership here is a little tricky. Everything comes into this function owned by the remote
	//command, but we don't want to waste time StructCopying everything, so we eaDestroy the earrays
	//in pInfo which leaks the servers and machines. Then we add them to the stash tables, but only
	//the machine and server tables "own" them, because the gameserver and clientcontroller tables
	//are subsets of the server table


	if (!sbDoingControllerMirroring)
	{
		sbDoingControllerMirroring = true;

		gMachinesByName = stashTableCreateWithStringKeys(128, StashDefault);
		sServersByName = stashTableCreateWithStringKeys(4096, StashDefault);
		sGameServersByName = stashTableCreateWithStringKeys(4096, StashDefault);
		sClientControllersByName = stashTableCreateWithStringKeys(16, StashDefault);

		resRegisterDictionaryForStashTable("Machines", RESCATEGORY_SYSTEM, 0, gMachinesByName, parse_TrackedMachineState);
		resRegisterDictionaryForStashTable("Servers", RESCATEGORY_SYSTEM, 0, sServersByName, parse_TrackedServerState);
		resRegisterDictionaryForStashTable("GameServers", RESCATEGORY_SYSTEM, 0, sGameServersByName, parse_TrackedServerState);
		resRegisterDictionaryForStashTable("ClientControllers", RESCATEGORY_SYSTEM, 0, sClientControllersByName, parse_TrackedServerState);
	}
	else
	{
		stashTableClear(sGameServersByName);
		stashTableClear(sClientControllersByName);
		stashTableClearStruct(gMachinesByName, NULL, parse_TrackedMachineState);
		stashTableClearStruct(sServersByName, NULL, parse_TrackedServerState);
			
		for (i=0; i < eaSize(&pInfo->ppMachines); i++)
		{
			stashAddPointer(gMachinesByName, pInfo->ppMachines[i]->machineName, pInfo->ppMachines[i], false);
		}

		for (i=0; i < eaSize(&pInfo->ppServers); i++)
		{
			stashAddPointer(sServersByName, pInfo->ppServers[i]->uniqueName, pInfo->ppServers[i], false);

			if (pInfo->ppServers[i]->eContainerType == GLOBALTYPE_GAMESERVER)
			{
				stashAddPointer(sGameServersByName, pInfo->ppServers[i]->uniqueName, pInfo->ppServers[i], false);
			}
			else if (pInfo->ppServers[i]->eContainerType == GLOBALTYPE_TESTCLIENT)
			{
				stashAddPointer(sClientControllersByName, pInfo->ppServers[i]->uniqueName, pInfo->ppServers[i], false);
			}
		}
	}

	eaDestroy(&pInfo->ppServers);
	eaDestroy(&pInfo->ppMachines);
}




AUTO_COMMAND;
void KillServerCmd(char *pServerType, ContainerID iID)
{
	RemoteCommand_CallLocalCommandRemotely(GLOBALTYPE_CONTROLLER, 0, STACK_SPRINTF("KillServerCmd %s %u", pServerType, iID));
}

AUTO_COMMAND;
void GetDumpCmd(char *pServerType, ContainerID iID)
{
	RemoteCommand_CallLocalCommandRemotely(GLOBALTYPE_CONTROLLER, 0, STACK_SPRINTF("GetDumpCmd %s %u", pServerType, iID));
}

AUTO_COMMAND;
void BroadcastMessageToGameServer(ContainerID iContainerID, ACMD_SENTENCE pMessage)
{
	RemoteCommand_RemoteObjBroadcastMessage(GLOBALTYPE_GAMESERVER, iContainerID, "Server-global Message", pMessage);
}


AUTO_COMMAND;
void TimerRecordStartAutoRemotely(ContainerID iContainerID)
{
	RemoteCommand_timerRecordStartAutoRemote(GLOBALTYPE_GAMESERVER, iContainerID);
}

AUTO_COMMAND;
void LockGameServer(ContainerID iID, int iLock)
{
	RemoteCommand_CallLocalCommandRemotely(GLOBALTYPE_CONTROLLER, 0, STACK_SPRINTF("LockGameServer %u %d", iID, iLock));
}

AUTO_COMMAND;
void MuteAlerts(char *pServerType, ContainerID iID)
{
	RemoteCommand_CallLocalCommandRemotely(GLOBALTYPE_CONTROLLER, 0, STACK_SPRINTF("MuteAlerts %s %u", pServerType, iID));

}

AUTO_COMMAND;
void UnMuteAlerts(char *pServerType, ContainerID iID)
{
	RemoteCommand_CallLocalCommandRemotely(GLOBALTYPE_CONTROLLER, 0, STACK_SPRINTF("UnMuteAlerts %s %u", pServerType, iID));

}


AUTO_COMMAND;
void BroadcastMessageToMachine(char *pMachineName, ACMD_SENTENCE pMessage)
{
	char *pFullString = NULL;
	estrStackCreate(&pFullString);
	estrPrintf(&pFullString, "BroadcastMessageToMachine %s %s", pMachineName, pMessage);
	RemoteCommand_CallLocalCommandRemotely(GLOBALTYPE_CONTROLLER, 0, pFullString);
	estrDestroy(&pFullString);
}

AUTO_COMMAND;
void LockMachine(char *pMachineName, int iLock)
{
	RemoteCommand_CallLocalCommandRemotely(GLOBALTYPE_CONTROLLER, 0, STACK_SPRINTF("LockMachine %s %d", pMachineName, iLock));

}

AUTO_COMMAND;
void LockAllGameServers(char *pMachineName, int iLock)
{
	RemoteCommand_CallLocalCommandRemotely(GLOBALTYPE_CONTROLLER, 0, STACK_SPRINTF("LockAllGameServers %s %d", pMachineName, iLock));

}