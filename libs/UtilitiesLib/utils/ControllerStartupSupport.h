#pragma once

#include "GlobalTypeEnum.h"


//stuff that is used for ShardSetupFiles, which both the controller and ShardLauncher need to be able to read


AUTO_ENUM;
typedef enum
{
	CAN_NOT_LAUNCH,
	CAN_LAUNCH_DEFAULT,
	CAN_LAUNCH_SPECIFIED,
} enumMachineCanLaunchServerSetting;


//a simplified set of information about a machine, used to write out/read in the "shard setup" file

AUTO_STRUCT;
typedef struct MachineInfoServerLaunchSettings
{
	GlobalType eServerType;  AST(SUBTABLE(GlobalTypeEnum))
	enumMachineCanLaunchServerSetting eSetting;
	int iPriority; //higher means launcher here first, ignored for gameservers, as they use their own complicated load-balancing system
	char *pCategories; //comma or space or semicolon-separated list of categories
} MachineInfoServerLaunchSettings;


AUTO_STRUCT;
typedef struct MachineInfoForShardSetup
{
	char *pMachineName; AST(ESTRING)
	MachineInfoServerLaunchSettings **ppSettings;
} MachineInfoForShardSetup;

AUTO_STRUCT;
typedef struct GSMachineGroupDef
{
	char *pPredefinedGroupName; AST(STRUCTPARAM)
	char **ppMachines; AST(NAME(Machine, MachineName))
} GSMachineGroupDef;

//flexible, generic, not-really-textparsery argument that basically reads in the entire string, and then later will
//process it as either "machine, machine, machine: servertype" or "machine: servertype, servertype, servertype". These will
//then get stuck into ppMachines as if they were explicit LAUNCH_SPECIFIED pairs
AUTO_STRUCT;
typedef struct ShardSetupGenericArg
{
	const char *pCurrentFile; AST(CURRENTFILE)
	int iLineNum; AST(LINENUM)
	char **ppInternalStrs; AST(STRUCTPARAM)
} ShardSetupGenericArg;

AUTO_STRUCT;
typedef struct MachineInfoForShardSetupList
{
	char *pComment; //used by the ShardLauncher tool when providing nice friendly information about
					//a shard setup file
	MachineInfoForShardSetup **ppMachines; 

	GSMachineGroupDef **ppMachineGroups; AST(NAME(MachineGroup))

	char *pBaseGameServerGroupName; //a group name, as defined in PredefinedGroupsFileName. All machines in it
		//will be added as part of the base configuration as game server machines

	ShardSetupGenericArg **ppGenericArgs; AST(FORMATSTRING(DEFAULT_FIELD=1))
} MachineInfoForShardSetupList;


