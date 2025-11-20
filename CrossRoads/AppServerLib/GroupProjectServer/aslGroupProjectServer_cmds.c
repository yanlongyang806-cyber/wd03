/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "aslGroupProjectServer.h"
#include "GroupProjectCommon.h"
#include "error.h"
#include "objTransactions.h"
#include "itemEnums.h"
#include "StringCache.h"
#include "LoggedTransactions.h"

#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/aslGroupProjectServer_cmds_c_ast.h"

static void 
DebugCreateAndInitCB(bool succeeded, void *userData)
{
    if (!succeeded)
    {
        Errorf("aslGroupProject_DebugCreateAndInit failed");
    }
}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(GroupProject);
void
aslGroupProject_DebugCreateAndInit(int containerType, U32 containerID, int ownerType, U32 ownerID)
{
    aslGroupProject_CreateAndInitProjectContainer(containerType, containerID, ownerType, ownerID, DebugCreateAndInitCB, NULL);   
}

void GiveNumeric_CB(TransactionReturnVal *returnval, void *userData)
{

}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(GroupProject);
void
aslGroupProject_GiveNumeric(int containerType, U32 containerID, const char *projectName, const char *numericName, S32 value)
{
    TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("GiveNumeric", containerType, containerID, GiveNumeric_CB, NULL);
    AutoTrans_GroupProject_tr_ApplyNumeric(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, containerType, containerID, projectName, numericName, NumericOp_Add, value);
}

void SetUnlock_CB(TransactionReturnVal *returnval, void *userData)
{

}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(GroupProject);
void
aslGroupProject_SetUnlock(int containerType, U32 containerID, const char *projectName, const char *unlockName)
{
    TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("SetUnlock", containerType, containerID, SetUnlock_CB, NULL);
    AutoTrans_GroupProject_tr_SetUnlock(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, containerType, containerID, projectName, unlockName);
}

void ClearUnlock_CB(TransactionReturnVal *returnval, void *userData)
{

}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(GroupProject);
void
aslGroupProject_ClearUnlock(int containerType, U32 containerID, const char *projectName, const char *unlockName)
{
    TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("ClearUnlock", containerType, containerID, ClearUnlock_CB, NULL);
    AutoTrans_GroupProject_tr_ClearUnlock(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, containerType, containerID, projectName, unlockName);
}

void
SetNextTask_CB(TransactionReturnVal *returnval, void *userData)
{

}

AUTO_STRUCT;
typedef struct SetNextTaskContainerCreateCBData
{
    GlobalType containerType;
    ContainerID containerID;
    STRING_POOLED projectName;  AST(POOL_STRING)
    int taskSlotNum;
    STRING_POOLED taskName;     AST(POOL_STRING)
} SetNextTaskContainerCreateCBData;

void
SetNextTaskContainerCreate_CB(bool succeeded, SetNextTaskContainerCreateCBData *cbData)
{
    if ( succeeded )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("SetNextTaskAfterContainerCreate", cbData->containerType, cbData->containerID, SetNextTask_CB, NULL);
        AutoTrans_GroupProject_tr_SetNextTask(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, cbData->containerType, cbData->containerID, cbData->projectName, cbData->taskSlotNum, cbData->taskName);

    }
    StructDestroy(parse_SetNextTaskContainerCreateCBData, cbData);
}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(GroupProject);
void
aslGroupProject_SetNextTask(int containerType, U32 containerID, int ownerType, U32 ownerID, const char *projectName, int taskSlotNum, const char *taskName)
{
    if ( aslGroupProject_ProjectContainerExists(containerType, containerID, false) )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("SetNextTask", containerType, containerID, SetNextTask_CB, NULL);
        AutoTrans_GroupProject_tr_SetNextTask(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, containerType, containerID, projectName, taskSlotNum, taskName);
    }
    else
    {
        SetNextTaskContainerCreateCBData *cbData = StructCreate(parse_SetNextTaskContainerCreateCBData);
        cbData->containerID = containerID;
        cbData->containerType = containerType;
        cbData->projectName = allocAddString(projectName);
        cbData->taskSlotNum = taskSlotNum;
        cbData->taskName = allocAddString(taskName);

        aslGroupProject_CreateAndInitProjectContainer(containerType, containerID, ownerType, ownerID, SetNextTaskContainerCreate_CB, cbData);
    }
}
// This struct is used for both setting the player message and the project name.
AUTO_STRUCT;
typedef struct SetStringContainerCreateCBData
{
    GlobalType containerType;
    ContainerID containerID;
    STRING_POOLED projectName;  AST(POOL_STRING)
    const char *playerString;
} SetStringContainerCreateCBData;

void
SetProjectMessage_CB(TransactionReturnVal *returnval, void *userData)
{
}

void
SetProjectMessageContainerCreate_CB(bool succeeded, SetStringContainerCreateCBData *cbData)
{
    if ( succeeded )
    {
        TransactionReturnVal *returnVal = objCreateManagedReturnVal(SetProjectMessage_CB, NULL);
        AutoTrans_GroupProject_tr_SetProjectMessage(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, cbData->containerType, cbData->containerID, cbData->projectName, cbData->playerString);

    }
    StructDestroy(parse_SetStringContainerCreateCBData, cbData);
}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(GroupProject);
void
aslGroupProject_SetProjectMessage(int containerType, U32 containerID, int ownerType, U32 ownerID, const char *projectName, char *projectMessage)
{
    // Truncate the message to the max length.
    if ( strlen(projectMessage) > PROJECT_MESSAGE_MAX_LEN )
    {
        projectMessage[PROJECT_MESSAGE_MAX_LEN] = '\0';
    }

    if ( aslGroupProject_ProjectContainerExists(containerType, containerID, false) )
    {
        TransactionReturnVal *returnVal = objCreateManagedReturnVal(SetProjectMessage_CB, NULL);
        AutoTrans_GroupProject_tr_SetProjectMessage(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, containerType, containerID, projectName, projectMessage);
    }
    else
    {
        SetStringContainerCreateCBData *cbData = StructCreate(parse_SetStringContainerCreateCBData);
        cbData->containerID = containerID;
        cbData->containerType = containerType;
        cbData->projectName = allocAddString(projectName);
        cbData->playerString = strdup(projectMessage);

        aslGroupProject_CreateAndInitProjectContainer(containerType, containerID, ownerType, ownerID, SetProjectMessageContainerCreate_CB, cbData);
    }
}

void
SetProjectPlayerName_CB(TransactionReturnVal *returnval, void *userData)
{
}

void
SetProjectPlayerNameContainerCreate_CB(bool succeeded, SetStringContainerCreateCBData *cbData)
{
    if ( succeeded )
    {
        TransactionReturnVal *returnVal = objCreateManagedReturnVal(SetProjectPlayerName_CB, NULL);
        AutoTrans_GroupProject_tr_SetProjectPlayerName(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, cbData->containerType, cbData->containerID, cbData->projectName, cbData->playerString);

    }
    StructDestroy(parse_SetStringContainerCreateCBData, cbData);
}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(GroupProject);
void
aslGroupProject_SetProjectPlayerName(int containerType, U32 containerID, int ownerType, U32 ownerID, const char *projectName, char *projectPlayerName)
{
    // Truncate the name to the max length.
    if ( strlen(projectPlayerName) > PROJECT_PLAYER_NAME_MAX_LEN )
    {
        projectPlayerName[PROJECT_PLAYER_NAME_MAX_LEN] = '\0';
    }

    if ( aslGroupProject_ProjectContainerExists(containerType, containerID, false) )
    {
        TransactionReturnVal *returnVal = objCreateManagedReturnVal(SetProjectPlayerName_CB, NULL);
        AutoTrans_GroupProject_tr_SetProjectPlayerName(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, containerType, containerID, projectName, projectPlayerName);
    }
    else
    {
        SetStringContainerCreateCBData *cbData = StructCreate(parse_SetStringContainerCreateCBData);
        cbData->containerID = containerID;
        cbData->containerType = containerType;
        cbData->projectName = allocAddString(projectName);
        cbData->playerString = strdup(projectPlayerName);

        aslGroupProject_CreateAndInitProjectContainer(containerType, containerID, ownerType, ownerID, SetProjectPlayerNameContainerCreate_CB, cbData);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject) ACMD_NAME(aslGroupProject_DumpDebugLog);
void
aslGroupProject_DumpDebugLogCmd(void)
{
    aslGroupProject_DumpDebugLog();
}
#include "AutoGen/aslGroupProjectServer_cmds_c_ast.c"