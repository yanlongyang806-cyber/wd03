#include "Controller_MachineGroups.h"
#include "Controller_MachineGroups_h_ast.h"
#include "Controller_MachineGroups_c_ast.h"
#include "StashTable.h"
#include "earray.h"
#include "EString.h"
#include "error.h"
#include "ResourceInfo.h"
#include "Alerts.h"
#include "timing.h"
#include "cmdparse.h"
#include "logging.h"
#include "TimedCallback.h"
#include "ControllerPub_h_ast.h"
#include "ControllerPub.h"
#include "Controller.h"
#include "file.h"
#include "Controller_DynHoggPruning.h"
#include "svrGlobalInfo.h"
#include "ContinuousBuilderSupport.h"
#include "structDefines.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "Controller_PCLStatusMonitoring.h"
#include "SentryServerComm.h"
#include "TimedCallback.h"
#include "AutoGen/ControllerStartupSupport_h_ast.h"

void FirstMachineDonePatchingCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);
static void StartPatchingGroup(ControllerMachineGroup *pGroup);

StashTable sGroupsFromSharedFileByName = NULL;


//__CATEGORY Stuff relating to shared machine groups
//(Percent) Once this percent of the machines in a group are patched (rounded down), the group can be considered patched
static int siPercentOfMachinesPatchedBeforeGroupPatched = 80;
AUTO_CMD_INT(siPercentOfMachinesPatchedBeforeGroupPatched, PercentOfMachinesPatchedBeforeGroupPatched) ACMD_CONTROLLER_AUTO_SETTING(MachineGroups);


//how often the controller processes a group activation request (leave this alone most of the time)
static int siMinTimeBetweenActivationRequests = 120;
AUTO_CMD_INT(siMinTimeBetweenActivationRequests, MinTimeBetweenActivationRequests) ACMD_CONTROLLER_AUTO_SETTING(MachineGroups);

//if true, use machine groups
bool gbControllerMachineGroups_SystemIsActive = false;
AUTO_CMD_INT(gbControllerMachineGroups_SystemIsActive, ControllerMachineGroups_ActivateSystem);

//when launching a gameserver, if the best launch weight is lower (worse) than this, then request a new group of machines
//This must be less than or equal to GoodEnoughLaunchWeightCutoff
float gfLaunchWeightWhichTriggersMachineGroupRequest = 20.0f;
AUTO_CMD_FLOAT(gfLaunchWeightWhichTriggersMachineGroupRequest, LaunchWeightWhichTriggersMachineGroupRequest) ACMD_CONTROLLER_AUTO_SETTING(MachineGroups) ACMD_CALLBACK(CheckMachineGroupSettingsCB);

//always keep this many groups patched ahead of time when possible
static int siNumGroupsToAlwaysHavePatched = 0;
AUTO_CMD_INT(siNumGroupsToAlwaysHavePatched, NumGroupsToAlwaysHavePatched) ACMD_CONTROLLER_AUTO_SETTING(MachineGroups);

//what filename to get machine groups from, presumably on a shared drive somewhere
static char *spPredefinedGroupsFileName = NULL;
AUTO_CMD_ESTRING(spPredefinedGroupsFileName, PredefinedGroupsFileName) ACMD_COMMANDLINE;

//If a machine in our "base group" has launch weight greater than this, always use it in preference to all machines in other groups,
//then apply that to each group sequentially, to weight launches towards the lower groups.
//This must be greater than or equal to LaunchWeightWhichTriggersMachineGroupRequest
static float sfGoodEnoughLaunchWeightCutoff = 20.0f;
AUTO_CMD_FLOAT(sfGoodEnoughLaunchWeightCutoff, GoodEnoughLaunchWeightCutoff) ACMD_CONTROLLER_AUTO_SETTING(MachineGroups) ACMD_CALLBACK(CheckMachineGroupSettingsCB);

//we want as many static maps as possible to be in our "base group". So when launching static maps, we bias its launch weight
//up this amount, and for all other maps bias it down the same amount
static float sfStaticBaseGroupLaunchBias = 10.0f;
AUTO_CMD_FLOAT(sfStaticBaseGroupLaunchBias, StaticBaseGroupLaunchBias) ACMD_CONTROLLER_AUTO_SETTING(MachineGroups);

//whenever we start patching a group of machines, wait this many seconds, then check if all have completed patching,
//and trigger an alert if not all have
static int siSecondsBeforeGroupPatchingAlert = 600;
AUTO_CMD_INT(siSecondsBeforeGroupPatchingAlert, SecondsBeforeGroupPatchingAlert) ACMD_CONTROLLER_AUTO_SETTING(MachineGroups);

//whenever the first machine in a group finishes patching, wait this many seconds, then check if all have completed patching,
//and trigger an alert if not all have
static int siSecondsAfterFirstPatchedInGroupBeforeAlert = 120;
AUTO_CMD_INT(siSecondsAfterFirstPatchedInGroupBeforeAlert, SecondsAfterFirstPatchedInGroupBeforeAlert) ACMD_CONTROLLER_AUTO_SETTING(MachineGroups);


static StashTable sMachineGroupsByID = NULL;
static StashTable sAvailableMachinesByName = NULL;

static StashTable sMachineGroupMachinesByName = NULL;

static ControllerMachineGroup **sppGroupsInOrder = NULL;

static int siNextID = 1;

static ControllerMachineGroup **sppGroupsByState[GROUPSTATE_LAST] = {0};

static void ActivateGroup(ControllerMachineGroup *pGroup);

AUTO_STRUCT;
typedef struct MachineIPs
{
	U32 iPublic;
	U32 iPrivate;
} MachineIPs;

void CheckMachineGroupSettingsCBCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (gfLaunchWeightWhichTriggersMachineGroupRequest >  sfGoodEnoughLaunchWeightCutoff)
	{
		CRITICAL_NETOPS_ALERT("BAD_MACHINEGROUP_SETTINGS", "Controller Auto Seting LaunchWeightWhichTriggersMachineGroupRequest has been set to %f, which is greater than GoodEnoughLaunchWeightCutoff, which has been set to %f. This is bad and will cause all machine groups to be patched and grabbed immediately",
			gfLaunchWeightWhichTriggersMachineGroupRequest, sfGoodEnoughLaunchWeightCutoff);
	}
}

void CheckMachineGroupSettingsCB(CMDARGS)
{
	//wait a second before triggering in case they're both set during controller startup
	TimedCallback_Run(CheckMachineGroupSettingsCBCB, NULL, 1.0f);
}

AUTO_FIXUPFUNC;
TextParserResult ControllerMachineGroupFixupFunc(ControllerMachineGroup *pGroup, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		case FIXUPTYPE_CONSTRUCTOR:
			eaPush(&sppGroupsByState[GROUPSTATE_STARTUP], pGroup);
			break;
	}

	return true;
}


AUTO_FIXUPFUNC;
TextParserResult ControllerMachineGroup_SingleMachineFixupFunc(ControllerMachineGroup_SingleMachine *pMachine, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
			if (pMachine->pGroup->eState == GROUPMACHINESTATE_PATCHING)
			{
				estrCopy2(&pMachine->pPatchingStatus, Controller_GetPCLStatusMonitoringDescriptiveStatusString(pMachine->pMachineName, CONTROLLER_GROUP_PATCHING));
			}
			else
			{
				estrDestroy(&pMachine->pPatchingStatus);
			}
			break;
	}

	return true;
}


int CompareControllerMachineGroups(const ControllerMachineGroup **ppGroup1, const ControllerMachineGroup **ppGroup2)
{
	return ((*ppGroup1)->iID) - ((*ppGroup2)->iID);
}

void SortGroupList(ControllerMachineGroup ***pppGroups)
{
	eaQSort((*pppGroups), CompareControllerMachineGroups);


}

int PatchedMachinesNeededForGroupToBePatched(ControllerMachineGroup *pGroup)
{
	int iSize = eaSize(&pGroup->ppMachines);
	int iRetVal = (iSize * siPercentOfMachinesPatchedBeforeGroupPatched) / 100;
	return iRetVal;
}

static void MachineLog(ControllerMachineGroup_SingleMachine *pMachine, char *pComment, ...)
{
	SimpleLogEntry *pLogEntry = StructCreate(parse_SimpleLogEntry);
	estrGetVarArgs(&pLogEntry->pComment, pComment);
	pLogEntry->iTime = timeSecondsSince2000();
	eaPush(&pMachine->ppLogs, pLogEntry);
}

static void SetMachineState(ControllerMachineGroup_SingleMachine *pMachine, enumGroupMachineState eState, char *pComment, ...)
{
	char *pFullComment = NULL;
	estrGetVarArgs(&pFullComment, pComment);


	pMachine->pGroup->iMachinesPerState[pMachine->eState]--;
	DECONST(enumGroupMachineState, pMachine->eState) = eState;
	pMachine->pGroup->iMachinesPerState[pMachine->eState]++;

	MachineLog(pMachine, "Setting to %s because: %s", StaticDefineIntRevLookup(enumGroupMachineStateEnum, eState), pFullComment);
	estrDestroy(&pFullComment);

}


static void GroupLog(ControllerMachineGroup *pGroup, char *pComment, ...)
{
	SimpleLogEntry *pLogEntry = StructCreate(parse_SimpleLogEntry);
	estrGetVarArgs(&pLogEntry->pComment, pComment);
	pLogEntry->iTime = timeSecondsSince2000();
	eaPush(&pGroup->ppLogs, pLogEntry);
	


}

static void SetGroupState(ControllerMachineGroup *pGroup, enumGroupState eState, char *pComment, ...)
{
	char *pFullComment = NULL;
	estrGetVarArgs(&pFullComment, pComment);

	eaFindAndRemoveFast(&sppGroupsByState[pGroup->eState], pGroup);
	DECONST(enumGroupState, pGroup->eState) = eState;
	eaPush(&sppGroupsByState[pGroup->eState], pGroup);

	GroupLog(pGroup, "Setting to %s because: %s", StaticDefineIntRevLookup(enumGroupStateEnum, eState), pFullComment);
	estrDestroy(&pFullComment);
}

ControllerMachineGroup_SingleMachine *FindMachineInMachineGroupByName(ControllerMachineGroup *pGroup, char *pName)
{
	FOR_EACH_IN_EARRAY(pGroup->ppMachines, ControllerMachineGroup_SingleMachine, pMachine)
	{
		if (stricmp(pMachine->pMachineName, pName) == 0)
		{
			return pMachine;
		}
	}
	FOR_EACH_END;

	return NULL;
}

ControllerMachineGroup_SingleMachine *FindMachineInAnyMachineGroupByName(char *pName)
{
	ControllerMachineGroup_SingleMachine *pRetVal;

	if (!sMachineGroupsByID)
	{
		return NULL;
	}

	FOR_EACH_IN_STASHTABLE(sMachineGroupsByID, ControllerMachineGroup, pGroup)
	{

		if ((pRetVal = FindMachineInMachineGroupByName(pGroup, pName)))
		{
			return pRetVal;
		}
	}
	FOR_EACH_END

	return NULL;
}

static void CreateAndAddMachine(ControllerMachineGroup *pGroup, char *pMachineName, char *pComment, ...)
{
	ControllerMachineGroup_SingleMachine *pMachine = StructCreate(parse_ControllerMachineGroup_SingleMachine);
	char *pActualComment = NULL;
	estrGetVarArgs(&pActualComment, pComment);

	pMachine->pMachineName = strdup(pMachineName);
	pMachine->pGroup = pGroup;
	estrPrintf(&pMachine->pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pMachineName);

	eaPush(&pGroup->ppMachines, pMachine);

	MachineLog(pMachine, "Created: %s", pActualComment);
	estrDestroy(&pActualComment);

	pGroup->iMachinesPerState[GROUPMACHINESTATE_STARTUP]++;

	if (!sMachineGroupMachinesByName)
	{
		sMachineGroupMachinesByName = stashTableCreateWithStringKeys(16, StashDefault);
	}

	stashAddPointer(sMachineGroupMachinesByName, pMachine->pMachineName, pMachine, false);
}

void LazyLoadSharedGroupsFile(void)
{
	int i;

	if (!sGroupsFromSharedFileByName)
	{
		MachineInfoForShardSetupList list = {0};

		if (!ParserReadTextFile(spPredefinedGroupsFileName, parse_MachineInfoForShardSetupList, &list, 0))
		{
			assertmsgf(0, "Unable to load shared machine group defs from %s", spPredefinedGroupsFileName);
		}
		sGroupsFromSharedFileByName = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);

		for (i = 0; i < eaSize(&list.ppMachineGroups); i++)
		{
			assertmsgf(list.ppMachineGroups[i]->pPredefinedGroupName && list.ppMachineGroups[i]->pPredefinedGroupName[0], 
				"Invalid machine group missing predifined group name in %s", spPredefinedGroupsFileName);

			assertmsgf(!stashFindPointer(sGroupsFromSharedFileByName, list.ppMachineGroups[i]->pPredefinedGroupName, NULL),
				"Duplicate machine groups named %s while reading %s", list.ppMachineGroups[i]->pPredefinedGroupName,
				spPredefinedGroupsFileName);

			stashAddPointer(sGroupsFromSharedFileByName, list.ppMachineGroups[i]->pPredefinedGroupName, 
				list.ppMachineGroups[i], true);
		}

		eaDestroy(&list.ppMachineGroups);

		StructDeInit(parse_MachineInfoForShardSetupList, &list);
	}
}

void ControllerMachineGroups_AddMachineGroup(GSMachineGroupDef *pGroupDef, char *pComment, ...)
{
	ControllerMachineGroup *pGroup;
	GSMachineGroupDef *pDefFromSharedFile = NULL;
	int i;
	char *pFullComment = NULL;

	if (!pGroupDef)
	{
		return;
	}

	if (pGroupDef->pPredefinedGroupName)
	{
		

		assertmsgf(estrLength(&spPredefinedGroupsFileName), "A groupMachineDef wants to use predefined group %s, but no PredefinedGroupsFileName is specified, this makes shard startup impossible",
			pGroupDef->pPredefinedGroupName);

		LazyLoadSharedGroupsFile();

		

		assertmsgf(stashFindPointer(sGroupsFromSharedFileByName, pGroupDef->pPredefinedGroupName, &pDefFromSharedFile),
			"A GroupMachineDef wants to use predefined group %s, we loaded groups from shared file %s, that group doesn't exist.",
				pGroupDef->pPredefinedGroupName, spPredefinedGroupsFileName);
			
		for (i=0; i < eaSize(&pDefFromSharedFile->ppMachines); i++)
		{
			eaPush(&pGroupDef->ppMachines, strdup(pDefFromSharedFile->ppMachines[i]));
		}
		

	}
		

	if (!eaSize(&pGroupDef->ppMachines))
	{
		return;
	}

	estrGetVarArgs(&pFullComment, pComment);

	if (pGroupDef->pPredefinedGroupName)
	{
		estrConcatf(&pFullComment, " -- got shared group def from %s", spPredefinedGroupsFileName);
	}

	pGroup = StructCreate(parse_ControllerMachineGroup);
	pGroup->iID = siNextID++;
	if (pGroupDef->pPredefinedGroupName)
	{
		pGroup->pName = strdup(pGroupDef->pPredefinedGroupName);
	}

	for (i = 0; i < eaSize(&pGroupDef->ppMachines); i++)
	{
		if (FindMachineInAnyMachineGroupByName(pGroupDef->ppMachines[i]))
		{
			AssertOrAlert("DUP_MACHINE_IN_GROUPS", "Machine %s being registered in two different machine groups, this is not allowed",
				pGroupDef->ppMachines[i]);
		}
		else if (FindMachineInMachineGroupByName(pGroup, pGroupDef->ppMachines[i]))
		{
			AssertOrAlert("DUP_MACHINE_IN_GROUP", "Machine %s listed twice in the same machine group, this is meaningless and confusing",
				pGroupDef->ppMachines[i]);
		}
		else
		{
			CreateAndAddMachine(pGroup, pGroupDef->ppMachines[i], "AddMachinesForNewMachineGroup");
			
		}
	}
	
	if (!sMachineGroupsByID)
	{
		sMachineGroupsByID = stashTableCreateInt(16);
		resRegisterDictionaryForEArray("MachineGroups", RESCATEGORY_OTHER, 0, &sppGroupsInOrder, parse_ControllerMachineGroup);
	}

	stashIntAddPointer(sMachineGroupsByID, pGroup->iID, pGroup, false);
	eaPush(&sppGroupsInOrder, pGroup);

	GroupLog(pGroup, "Created via AddMachinesForNewMachineGroup: %s", pFullComment);
	estrDestroy(&pFullComment);
}


void ControllerMachineGroups_AddAvailableMachineName(char *pName, U32 iPublicIP, U32 iPrivateIP)
{
	MachineIPs *pIPs = StructCreate(parse_MachineIPs);

	if (!sAvailableMachinesByName)
	{
		sAvailableMachinesByName = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);
	}

	pIPs->iPrivate = iPrivateIP;
	pIPs->iPublic = iPrivateIP;

	stashAddPointer(sAvailableMachinesByName, pName, pIPs, true);
}

void ControllerMachineGroups_AllAvailableMachineNamesAdded(void)
{
	if (!sMachineGroupsByID)
	{
		return;
	}

	FOR_EACH_IN_STASHTABLE(sMachineGroupsByID, ControllerMachineGroup, pGroup)
	{
		FOR_EACH_IN_EARRAY(pGroup->ppMachines, ControllerMachineGroup_SingleMachine, pMachine)
		{
			MachineIPs *pIPs;
			if (!stashFindPointer(sAvailableMachinesByName, pMachine->pMachineName, &pIPs))
			{
				WARNING_NETOPS_ALERT("UNKNOWN_MACHINE_IN_GROUP", "Machine %s is listed in a machine group, but sentry server doesn't know about it. This may or may not be a problem",
					pMachine->pMachineName);
			}
			else
			{
				if (!pMachine->iPublicIP)
				{
					pMachine->iPublicIP = pIPs->iPublic;
					pMachine->iPrivateIP = pIPs->iPrivate;
				}
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END
}

void ControllerMachineGroups_MachineIsPatched(ControllerMachineGroup *pGroup, ControllerMachineGroup_SingleMachine *pMachine)
{
	switch (pGroup->eState)
	{
	case GROUPSTATE_PATCHING:
		if (pGroup->iMachinesPerState[GROUPMACHINESTATE_PATCHED] >= PatchedMachinesNeededForGroupToBePatched(pGroup))
		{
			if (pGroup->eFlags & MACHINEGROUP_ACTIVATE_WHEN_PATCHED)
			{
				int iNumPatched = pGroup->iMachinesPerState[GROUPMACHINESTATE_PATCHED];
				ActivateGroup(pGroup);
	
				SetGroupState(pGroup, GROUPSTATE_RUNNING, "%d/%d machines patched, flag tells us to activate immediately", iNumPatched, eaSize(&pGroup->ppMachines));
			}
			else
			{
				SetGroupState(pGroup, GROUPSTATE_PATCHED, "%d/%d machines patched", pGroup->iMachinesPerState[GROUPMACHINESTATE_PATCHED], eaSize(&pGroup->ppMachines));
			}
		}
		break;

	case GROUPSTATE_RUNNING:
		pMachine->pActualMachine->canLaunchServerTypes[GLOBALTYPE_GAMESERVER].eCanLaunch = CAN_LAUNCH_SPECIFIED;
		SetMachineState(pMachine, GROUPMACHINESTATE_RUNNING, "Parent was already running when patching completed");
		break;
	}

	if (siSecondsAfterFirstPatchedInGroupBeforeAlert && !pGroup->bStartedPostFirstPatchCB)
	{	
		pGroup->bStartedPostFirstPatchCB = true;
		TimedCallback_Run(FirstMachineDonePatchingCB, (void*)((intptr_t)pGroup->iID), siSecondsAfterFirstPatchedInGroupBeforeAlert);
	}

}

void ControllerMachineGroups_GotContactFromMachine(char *pMachineName, TrackedMachineState *pActualMachine)
{
	ControllerMachineGroup_SingleMachine *pMachine = FindMachineInAnyMachineGroupByName(pMachineName);

	if (pMachine)
	{
		pActualMachine->iSharedMachineGroupIndex = pMachine->pGroup->iID;

		GroupLog(pMachine->pGroup, "Got launcher contact for %s", pMachineName);

		if (!pMachine->iPrivateIP)
		{
			pMachine->iPrivateIP = pActualMachine->IP;
			pMachine->iPublicIP = pActualMachine->iPublicIP;
		}

		switch(pMachine->eState)
		{
		case GROUPMACHINESTATE_RUNNING:
		case GROUPMACHINESTATE_PATCHED:
			//some weird reconnect case
			return;

		case GROUPMACHINESTATE_STARTUP:
		case GROUPMACHINESTATE_PATCHING:
		case GROUPMACHINESTATE_FAILED:
			SetMachineState(pMachine, GROUPMACHINESTATE_PATCHED, "Got launcher contact");
			pMachine->pActualMachine = pActualMachine;
			estrCopy2(&pMachine->pActualMachineLink, pActualMachine->mainLink);
		}

		ControllerMachineGroups_MachineIsPatched(pMachine->pGroup, pMachine);
	}
}

void PatchAnotherGroupIfPossible(FORMAT_STR const char *pReason_in, ...)
{
	if (eaSize(&sppGroupsByState[GROUPSTATE_STARTUP]))
	{
		static char *pReason = NULL;
		ControllerMachineGroup *pNextGroup;

		estrClear(&pReason);
		estrGetVarArgs(&pReason, pReason_in);

		SortGroupList(&sppGroupsByState[GROUPSTATE_STARTUP]);

		pNextGroup = sppGroupsByState[GROUPSTATE_STARTUP][0];
			
		StartPatchingGroup(pNextGroup);
		SetGroupState(pNextGroup, GROUPSTATE_PATCHING, "%s", pReason);
	}
}

void GroupStartedPatchingCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	ControllerMachineGroup *pGroup;

	if (!stashIntFindPointer(sMachineGroupsByID, (int)((intptr_t)userData), &pGroup))
	{
		return;
	}

	if (pGroup->iMachinesPerState[GROUPMACHINESTATE_PATCHING])
	{
		char *pDuration = NULL;
		char *pFullAlertString = NULL;
		timeSecondsDurationToPrettyEString(siSecondsBeforeGroupPatchingAlert, &pDuration);
		estrPrintf(&pFullAlertString, "%s after it began patching, machine group %d (%s) still has %d machines patching:",
			pDuration, pGroup->iID, pGroup->pName ? pGroup->pName : "unnamed", pGroup->iMachinesPerState[GROUPMACHINESTATE_PATCHING]);

		FOR_EACH_IN_EARRAY(pGroup->ppMachines, ControllerMachineGroup_SingleMachine, pMachine)
		{
			if (pMachine->eState == GROUPMACHINESTATE_PATCHING)
			{
				estrConcatf(&pFullAlertString, " %s", pMachine->pMachineName);
			}
		}
		FOR_EACH_END;

		CRITICAL_NETOPS_ALERT("GROUP_PATCHING_FAIL", "%s", pFullAlertString);

		PatchAnotherGroupIfPossible("Patching of group %s may have failed, starting another group patching just to be safe",
			pGroup->pName ? pGroup->pName : "unnamed");

		estrDestroy(&pDuration);
		estrDestroy(&pFullAlertString);
	}

}

void FirstMachineDonePatchingCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	ControllerMachineGroup *pGroup;

	if (!stashIntFindPointer(sMachineGroupsByID, (int)((intptr_t)userData), &pGroup))
	{
		return;
	}

	if (pGroup->iMachinesPerState[GROUPMACHINESTATE_PATCHING])
	{
		char *pDuration = NULL;
		char *pFullAlertString = NULL;
		timeSecondsDurationToPrettyEString(siSecondsAfterFirstPatchedInGroupBeforeAlert, &pDuration);
		estrPrintf(&pFullAlertString, "%s after its first machine completed patching, machine group %d (%s) still has %d machines patching:",
			pDuration, pGroup->iID, pGroup->pName ? pGroup->pName : "unnamed", pGroup->iMachinesPerState[GROUPMACHINESTATE_PATCHING]);

		FOR_EACH_IN_EARRAY(pGroup->ppMachines, ControllerMachineGroup_SingleMachine, pMachine)
		{
			if (pMachine->eState == GROUPMACHINESTATE_PATCHING)
			{
				estrConcatf(&pFullAlertString, " %s", pMachine->pMachineName);
			}
		}
		FOR_EACH_END;

		CRITICAL_NETOPS_ALERT("GROUP_PATCHING_FAIL", "%s", pFullAlertString);

		PatchAnotherGroupIfPossible("Patching of group %s may have failed, starting another group patching just to be safe",
			pGroup->pName ? pGroup->pName : "unnamed");

		estrDestroy(&pDuration);
		estrDestroy(&pFullAlertString);
	}
}

static void StartPatchingGroup(ControllerMachineGroup *pGroup)
{
	if (siSecondsBeforeGroupPatchingAlert);
	{
		TimedCallback_Run(GroupStartedPatchingCB, (void*)((intptr_t)pGroup->iID), siSecondsBeforeGroupPatchingAlert);
	}

	FOR_EACH_IN_EARRAY(pGroup->ppMachines, ControllerMachineGroup_SingleMachine, pMachine)
	{
		if (pMachine->eState == GROUPMACHINESTATE_STARTUP)
		{
			SetMachineState(pMachine, GROUPMACHINESTATE_PATCHING, "Parent patching");
			GrabMachineForShardAndMaybePatch(pMachine->pMachineName, true, true, CONTROLLER_GROUP_PATCHING);
			Controller_AddPCLStatusMonitoringTask(pMachine->pMachineName, CONTROLLER_GROUP_PATCHING);
		}
		else if (pMachine->eState == GROUPMACHINESTATE_PATCHED)
		{
			if (siSecondsAfterFirstPatchedInGroupBeforeAlert && !pGroup->bStartedPostFirstPatchCB)
			{	
				pGroup->bStartedPostFirstPatchCB = true;
				TimedCallback_Run(FirstMachineDonePatchingCB, (void*)((intptr_t)pGroup->iID), siSecondsAfterFirstPatchedInGroupBeforeAlert);
			}
		}
	}
	FOR_EACH_END;
}

AUTO_COMMAND;
char *PatchMachineGroup(int iID, CmdContext *pContext)
{
	ControllerMachineGroup *pGroup;
	if (!stashIntFindPointer(sMachineGroupsByID, iID, &pGroup))
	{
		return "Unknown machine group";
	}

	if (pGroup->eState != GROUPSTATE_STARTUP)
	{
		return "Can't patch this group, it's not in startup state";
	}

	StartPatchingGroup(pGroup);

	SetGroupState(pGroup, GROUPSTATE_PATCHING, "PatchMachineGroup command called by %s", StaticDefineIntRevLookup(enumCmdContextHowCalledEnum, pContext->eHowCalled));
	return "Done";
}

static void ActivateGroup(ControllerMachineGroup *pGroup)
{
	FOR_EACH_IN_EARRAY(pGroup->ppMachines, ControllerMachineGroup_SingleMachine, pMachine)
	{
		if (pMachine->pActualMachine)
		{
			pMachine->pActualMachine->canLaunchServerTypes[GLOBALTYPE_GAMESERVER].eCanLaunch = CAN_LAUNCH_SPECIFIED;
			SetMachineState(pMachine, GROUPMACHINESTATE_RUNNING, "Parent being used");
		}
	}
	FOR_EACH_END;
}


AUTO_COMMAND;
char *ActivateMachineGroup(int iID, CmdContext *pContext)
{
	ControllerMachineGroup *pGroup;
	if (!stashIntFindPointer(sMachineGroupsByID, iID, &pGroup))
	{
		return "Unknown machine group";
	}

	if (!(pGroup->eState == GROUPSTATE_PATCHING || pGroup->eState == GROUPSTATE_PATCHED))
	{
		return "Can't activate this group, it's not in patching or patched state";
	}

	ActivateGroup(pGroup);
	SetGroupState(pGroup, GROUPSTATE_RUNNING, "ActivateMachineGroup command called by %s", StaticDefineIntRevLookup(enumCmdContextHowCalledEnum, pContext->eHowCalled));

	

	return "done";
}


void ControllerMachineGroups_RequestGroupActivation(const char *pInComment, ...)
{
	static U32 siLastTime = 0;
	static char *pFullInComment = NULL;

	if (timeSecondsSince2000() < siLastTime + siMinTimeBetweenActivationRequests)
	{
		return;
	}

	estrGetVarArgs(&pFullInComment, pInComment);
	log_printf(LOG_MACHINEGROUPS, "Group activation requested because %s", pFullInComment);

	if (eaSize(&sppGroupsByState[GROUPSTATE_PATCHED]))
	{
		SortGroupList(&sppGroupsByState[GROUPSTATE_PATCHED]);
	
		log_printf(LOG_MACHINEGROUPS, "Group activation requested, we have a patched group (ID %d), activating it", sppGroupsByState[GROUPSTATE_PATCHED][0]->iID);
		ActivateGroup(sppGroupsByState[GROUPSTATE_PATCHED][0]);
		SetGroupState(sppGroupsByState[GROUPSTATE_PATCHED][0], GROUPSTATE_RUNNING, 
			"Group patching/activation requested (ie, gameservers are all sufficiently full)");
		return;
	}

	if (eaSize(&sppGroupsByState[GROUPSTATE_PATCHING]))
	{
		log_printf(LOG_MACHINEGROUPS, "Group activation requested, we already have a group patching (ID %d), doing nothing", 
			sppGroupsByState[GROUPSTATE_PATCHING][0]->iID);
		return;
	}

	if (!eaSize(&sppGroupsByState[GROUPSTATE_STARTUP]))
	{
		static bool sbFirst = true;

		if (sbFirst)
		{
			log_printf(LOG_MACHINEGROUPS, "Group activation requested, no machine groups left");
			WARNING_NETOPS_ALERT("INSUF_MACHINE_GROUPS", "Controller wants more machine groups, but they are all full. This alert will only happen once");
			sbFirst = false;
		}

		return;
	}

	SortGroupList(&sppGroupsByState[GROUPSTATE_STARTUP]);

	log_printf(LOG_MACHINEGROUPS, "Group activation requested, no patched or patching groups, starting group (ID %d) patching", 
		sppGroupsByState[GROUPSTATE_STARTUP][0]->iID);
	sppGroupsByState[GROUPSTATE_STARTUP][0]->eFlags |= MACHINEGROUP_ACTIVATE_WHEN_PATCHED;
	StartPatchingGroup(sppGroupsByState[GROUPSTATE_STARTUP][0]);
	SetGroupState(sppGroupsByState[GROUPSTATE_STARTUP][0], GROUPSTATE_PATCHING, 
		"Group patching/activation requested (ie, gameservers are all sufficiently full)");


}

AUTO_COMMAND;
void TestRequestMachineGroupActivation(CmdContext *pContext)
{
	ControllerMachineGroups_RequestGroupActivation("TestRequestMachineGroupActivation called by %s", GetContextHowString(pContext));
}

static void MachineGroups_PeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (siNumGroupsToAlwaysHavePatched)
	{

	
		while ((siNumGroupsToAlwaysHavePatched > eaSize(&sppGroupsByState[GROUPSTATE_PATCHING]) + eaSize(&sppGroupsByState[GROUPSTATE_PATCHED])) 
			&& eaSize(&sppGroupsByState[GROUPSTATE_STARTUP]))
		{
			ControllerMachineGroup *pNextGroup;
			
			SortGroupList(&sppGroupsByState[GROUPSTATE_STARTUP]);

			pNextGroup = sppGroupsByState[GROUPSTATE_STARTUP][0];
			
			StartPatchingGroup(pNextGroup);
			SetGroupState(pNextGroup, GROUPSTATE_PATCHING, "Config tells us to have %d groups patching or patched, we had %d patching and %d patched", 
				siNumGroupsToAlwaysHavePatched,
				eaSize(&sppGroupsByState[GROUPSTATE_PATCHING]),
				eaSize(&sppGroupsByState[GROUPSTATE_PATCHED]));
		}
	}
}

void MachineGroups_InitSystem(void)
{
	TimedCallback_Add(MachineGroups_PeriodicUpdate, NULL, 15.0f);
}


typedef struct BestMachinePerGroupTracker
{
	TrackedMachineState *pBestMachine;
	float fBestWeight;
} BestMachinePerGroupTracker;

TrackedMachineState *ControlleMachineGroups_FindMachineForGameServerLaunch(const char *pCategory, AdditionalServerLaunchInfo *pAdditionalInfo, char **ppLogString, float *pfLaunchWeight)
{
	BestMachinePerGroupTracker *spBestTrackers = NULL;
	int siBestTrackersSize = 0;
	int i;
	static char *pCommentString = NULL;

	TrackedMachineState *pOverallBestMachine = NULL;
	float fOverallBestWeight = -100000000000000.0f;

	//one tracker of best machine per machine group
	siBestTrackersSize = siNextID;
	spBestTrackers = alloca(sizeof(BestMachinePerGroupTracker) * siBestTrackersSize);

	for (i = 0; i < siBestTrackersSize; i++)
	{
		spBestTrackers[i].fBestWeight = -1000000000000000000.0f;
		spBestTrackers[i].pBestMachine = NULL;
	}

	for (i = 0; i < giNumMachines; i++)
	{
		if (gTrackedMachines[i].iSharedMachineGroupIndex >= siBestTrackersSize)
		{
			AssertOrAlert("MACHINE_GROUP_CORRUPTION", "Machine %s thinks it has shared group index %d which is out of range, can not launch GSs on it",
				gTrackedMachines[i].machineName, gTrackedMachines[i].iSharedMachineGroupIndex);
			continue;
		}

		//-----------------------IDENTICAL CHECK OCCURS IN ControlleMachineGroups_FindMachineForGameServerLaunch
		if (gTrackedMachines[i].pServersByType[GLOBALTYPE_LAUNCHER] && gTrackedMachines[i].canLaunchServerTypes[GLOBALTYPE_GAMESERVER].eCanLaunch != CAN_NOT_LAUNCH
		&& (!pCategory || eaFind(&gTrackedMachines[i].canLaunchServerTypes[GLOBALTYPE_GAMESERVER].ppCategories, pCategory) != -1)
		&& !gTrackedMachines[i].bIsLocked && !DynHoggPruningActiveForMachine(&gTrackedMachines[i]))
		//-----------------------IDENTICAL CHECK OCCURS IN ControlleMachineGroups_FindMachineForGameServerLaunch
		{
			estrClear(&pCommentString);

			if (isProductionMode() && !g_isContinuousBuilder && !Controller_MachineIsLegalForGameServerLaunch(&gTrackedMachines[i]))
			{
				estrConcatf(ppLogString, "Machine %s(%d): CPU: %d,%d. Megs: %d. BELOW CUTOFF\n", gTrackedMachines[i].machineName,
					gTrackedMachines[i].iSharedMachineGroupIndex,
					(int)(gTrackedMachines[i].performance.cpuUsage), (int)(gTrackedMachines[i].performance.cpuLast60), (int)(gTrackedMachines[i].performance.iFreeRAM / (1024 * 1024)));
			}
			else
			{
				float fCurWeight = Controller_FindServerLaunchingWeight(&gTrackedMachines[i], GLOBALTYPE_GAMESERVER, pAdditionalInfo, &pCommentString);
				BestMachinePerGroupTracker *pBestTracker = &spBestTrackers[gTrackedMachines[i].iSharedMachineGroupIndex];

				if (pCommentString)
				{
					estrConcatf(ppLogString, "Machine %s(%d): %s\n",
						gTrackedMachines[i].machineName,  gTrackedMachines[i].iSharedMachineGroupIndex, pCommentString);
				}
				else
				{
					estrConcatf(ppLogString, "Machine %s(%d): %f\n",
						gTrackedMachines[i].machineName,  gTrackedMachines[i].iSharedMachineGroupIndex, fCurWeight);
				}

				if (fCurWeight > pBestTracker->fBestWeight)
				{
					pBestTracker->fBestWeight = fCurWeight;
					pBestTracker->pBestMachine = &gTrackedMachines[i];
				}
			
			}
		}

	}

	if (spBestTrackers[0].pBestMachine)
	{
		if (SAFE_MEMBER(pAdditionalInfo, zMapType) == ZMTYPE_STATIC)
		{
			spBestTrackers[0].fBestWeight += sfStaticBaseGroupLaunchBias;
			estrConcatf(ppLogString, " Static map. Biased base group up %f. ", sfStaticBaseGroupLaunchBias);
		}
		else
		{
			spBestTrackers[0].fBestWeight -= sfStaticBaseGroupLaunchBias;
			estrConcatf(ppLogString, " Non-Static map. Biased base group down %f. ", sfStaticBaseGroupLaunchBias);
		}
	}


	estrConcatf(ppLogString, "Best machines by group: ");

	for (i = 0; i < siBestTrackersSize; i++)
	{
		estrConcatf(ppLogString, "%d: ", i);
		if (spBestTrackers[i].pBestMachine)
		{
			estrConcatf(ppLogString, "%s (%f) ", spBestTrackers[i].pBestMachine->machineName, spBestTrackers[i].fBestWeight);
		}
	}

	//first check... for each group, see if we have a machine that is "good enough". If so, use it. This weights
	//launches towards the first groups.
	for (i = 0 ; i < siBestTrackersSize; i++)
	{
		if (spBestTrackers[i].pBestMachine && spBestTrackers[i].fBestWeight > sfGoodEnoughLaunchWeightCutoff)
		{
			estrConcatf(ppLogString, " %s in group %d has launch weight %f above cutoff %f, so we use it",
				spBestTrackers[i].pBestMachine->machineName, i, spBestTrackers[i].fBestWeight, sfGoodEnoughLaunchWeightCutoff);
			*pfLaunchWeight = spBestTrackers[i].fBestWeight;
			return spBestTrackers[i].pBestMachine;
		}
	}

	//otherwise, we just use the best machine... 
	for (i = 0; i < siBestTrackersSize; i++)
	{
		if (spBestTrackers[i].pBestMachine)
		{
			if (spBestTrackers[i].fBestWeight > fOverallBestWeight)
			{
				fOverallBestWeight = spBestTrackers[i].fBestWeight;
				pOverallBestMachine = spBestTrackers[i].pBestMachine;
			}
		}
	}

	if (!pOverallBestMachine)
	{
		estrConcatf(ppLogString, " No machines found at all ");
	}
	else
	{
		estrConcatf(ppLogString, " Best machine: %s ", pOverallBestMachine->machineName);
	}

	*pfLaunchWeight = fOverallBestWeight;
	return pOverallBestMachine;
}


void ControllerMachineGroups_AddMachinesToShutDownWatcherCommandLine(char **ppCmdLine)
{
	FOR_EACH_IN_STASHTABLE(sMachineGroupsByID, ControllerMachineGroup, pGroup)
	{
		FOR_EACH_IN_EARRAY(pGroup->ppMachines, ControllerMachineGroup_SingleMachine, pMachine)
		{
			estrConcatf(ppCmdLine, " -machine %s ", pMachine->pMachineName);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}


void ControllerMachineGroups_FixupShardSetupListWithBaseGameServerGroup(MachineInfoForShardSetupList *pSetupList)
{
	GSMachineGroupDef *pDefFromSharedFile = NULL;
	int i;

	LazyLoadSharedGroupsFile();

	assertmsgf(stashFindPointer(sGroupsFromSharedFileByName, pSetupList->pBaseGameServerGroupName, &pDefFromSharedFile),
			"ShardSetup file wants to use base GS machine group %s, we loaded groups from shared file %s, that group doesn't exist.",
				pSetupList->pBaseGameServerGroupName, spPredefinedGroupsFileName);

	for (i = 0; i < eaSize(&pDefFromSharedFile->ppMachines); i++)
	{
		MachineInfoForShardSetup *pMachineSetup = StructCreate(parse_MachineInfoForShardSetup);
	
		estrCopy2(&pMachineSetup->pMachineName, pDefFromSharedFile->ppMachines[i]);
		eaPush(&pMachineSetup->ppSettings, StructCreate(parse_MachineInfoServerLaunchSettings));
		pMachineSetup->ppSettings[0]->eServerType = GLOBALTYPE_GAMESERVER;
		pMachineSetup->ppSettings[0]->eSetting = CAN_LAUNCH_SPECIFIED;
		
		eaPush(&pSetupList->ppMachines, pMachineSetup);

		printf("Added %s as a GS machine\n", pMachineSetup->pMachineName);
	}

}

void ControllerMachineGroups_ExecuteCommandOnNonPatchedMachines(char *pFullCommand, char *pComment, int iNumGroups, char *pPatchMonitoringTaskName)
{
	FOR_EACH_IN_STASHTABLE(sMachineGroupsByID, ControllerMachineGroup, pGroup)
	{
		//less than or equal because if we want to do 1 group, it shoudl be group ID 1, etc.
		if (pGroup->iID <= iNumGroups)
		{
			FOR_EACH_IN_EARRAY(pGroup->ppMachines, ControllerMachineGroup_SingleMachine, pMachine)
			{
				if (!pMachine->pActualMachine)
				{
					char *pCommandCopy = NULL;
					Packet *pPak;
					estrStackCreate(&pCommandCopy);
					estrCopy2(&pCommandCopy, pFullCommand);
					estrReplaceOccurrences(&pCommandCopy, "{MachineName}", pMachine->pMachineName);

					if (pPatchMonitoringTaskName)
					{
						Controller_AddPCLStatusMonitoringTask(pMachine->pMachineName, pPatchMonitoringTaskName);
						estrConcatf(&pCommandCopy, " - %s", Controller_GetPCLStatusMonitoringCmdLine(pMachine->pMachineName, pPatchMonitoringTaskName));
					}

					pPak = SentryServerComm_CreatePacket(MONITORCLIENT_LAUNCH, "Executing command with comment %s as part of %s on machine groups",
						pComment, pPatchMonitoringTaskName);
					pktSendString(pPak, pMachine->pMachineName);
					pktSendString(pPak, pCommandCopy);
					SentryServerComm_SendPacket(&pPak);

					estrDestroy(&pCommandCopy);


				}
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

AUTO_COMMAND;
void RestartPatching(char *pMachineName, CmdContext *pContext)
{
	ControllerMachineGroup_SingleMachine *pMachine;

	if (stashFindPointer(sMachineGroupMachinesByName, pMachineName, &pMachine))
	{
		if (pMachine->eState == GROUPMACHINESTATE_PATCHING || pMachine->eState == GROUPMACHINESTATE_FAILED)
		{
			SetMachineState(pMachine, GROUPMACHINESTATE_PATCHING, "RestartPatching called via %s", GetContextHowString(pContext));
			GrabMachineForShardAndMaybePatch(pMachine->pMachineName, true, true, "Group_Patching_Restart");
		}
	}
}



#include "Controller_MachineGroups_h_ast.c"
#include "Controller_MachineGroups_c_ast.c"