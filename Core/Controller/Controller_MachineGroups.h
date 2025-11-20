#pragma once

#include "Controller.h"

AUTO_ENUM;
typedef enum
{
	GROUPSTATE_STARTUP, //the machines in this group are not patched or anything

	GROUPSTATE_PATCHING, //the machines in this group have been instructed to patch

	GROUPSTATE_PATCHED, //the machines in this group have been patched (some may have failed). They
						//are presumably now in gTrackedMachines, but do not have any legal server types, so
						//should not be doing anything

	GROUPSTATE_RUNNING, //the machines in this group are now fully part of the shard


	GROUPSTATE_LAST,
} enumGroupState;

AUTO_ENUM;
typedef enum
{
	GROUPMACHINESTATE_STARTUP,
	GROUPMACHINESTATE_PATCHING,
	GROUPMACHINESTATE_PATCHED,
	GROUPMACHINESTATE_FAILED,
	GROUPMACHINESTATE_RUNNING,

	GROUPMACHINESTATE_LAST,
} enumGroupMachineState;


typedef struct ControllerMachineGroup ControllerMachineGroup;
typedef struct GSMachineGroupDef GSMachineGroupDef;
typedef struct TrackedMachineState TrackedMachineState;
typedef struct AdditionalServerLaunchInfo AdditionalServerLaunchInfo;

AUTO_STRUCT;
typedef struct SimpleLogEntry
{
	U32 iTime; AST(FORMATSTRING(HTML_SECS_AGO = 1))
	char *pComment; AST(ESTRING)
} SimpleLogEntry;

AUTO_STRUCT;
typedef struct ControllerMachineGroup_SingleMachine
{
	const enumGroupMachineState eState;
	char *pMachineName;
	U32 iPublicIP; AST(FORMAT_IP)
	U32 iPrivateIP; AST(FORMAT_IP)
	TrackedMachineState *pActualMachine; NO_AST
	ControllerMachineGroup *pGroup; NO_AST
	SimpleLogEntry **ppLogs;
	char *pActualMachineLink; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pVNC; AST(ESTRING, FORMATSTRING(HTML=1))

	char *pPatchingStatus; AST(ESTRING)

	AST_COMMAND("RestartPatching", "RestartPatching $FIELD(MachineName) $CONFIRM(Attempt to restart patching on this machine?)", "\q$FIELD(State)\q = \qPATCHING\q")

} ControllerMachineGroup_SingleMachine;

AUTO_ENUM;
typedef enum MachineGroupFlags
{
	MACHINEGROUP_ACTIVATE_WHEN_PATCHED = 1 << 0,
} MachineGroupFlags;

AUTO_STRUCT  AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Name, State, Patch, Activate");
typedef struct ControllerMachineGroup
{
	int iID; AST(KEY)
	char *pName; //will be set if this was specified via a shared name, unset otherwise. Not a key.
	const enumGroupState eState; //const to make sure it's set via SetGroupState
	ControllerMachineGroup_SingleMachine **ppMachines;
	SimpleLogEntry **ppLogs;
	MachineGroupFlags eFlags;

	int iMachinesPerState[GROUPMACHINESTATE_LAST];

	bool bStartedPostFirstPatchCB; NO_AST

	AST_COMMAND("Patch", "PatchMachineGroup $FIELD(ID) $CONFIRM(start this group patching?)", "\q$FIELD(State)\q = \qSTARTUP\q")
	AST_COMMAND("Activate", "ActivateMachineGroup $FIELD(ID) $CONFIRM(Start launching gameservers on machines in this group?)", "\q$FIELD(State)\q = \qPATCHING\q OR \q$FIELD(State)\q = \qPATCHED\q")
} ControllerMachineGroup;

void ControllerMachineGroups_AddAvailableMachineName(char *pName, U32 iPublicIP, U32 iPrivateIP);
void ControllerMachineGroups_AddMachineGroup(GSMachineGroupDef *pGroup, char *pComment, ...);
void ControllerMachineGroups_AllAvailableMachineNamesAdded(void);
void ControllerMachineGroups_GotContactFromMachine(char *pName, TrackedMachineState *pActualMachine);

//tells the machine group system that the controller now needs gameserver machines, start something going. 
void ControllerMachineGroups_RequestGroupActivation(FORMAT_STR const char *pComment,  ...);

void MachineGroups_InitSystem(void);

extern bool gbControllerMachineGroups_SystemIsActive;
extern float gfLaunchWeightWhichTriggersMachineGroupRequest;

TrackedMachineState *ControlleMachineGroups_FindMachineForGameServerLaunch(const char *pCategory, AdditionalServerLaunchInfo *pInfo, char **ppLogString, float *pfLaunchWeight);

void ControllerMachineGroups_AddMachinesToShutDownWatcherCommandLine(char **ppCmdLine);

void ControllerMachineGroups_FixupShardSetupListWithBaseGameServerGroup(MachineInfoForShardSetupList *pSetupList);

void ControllerMachineGroups_ExecuteCommandOnNonPatchedMachines(char *pFullCommand, char *pComment, int iNumGroups, char *pPatchMonitoringTaskName);