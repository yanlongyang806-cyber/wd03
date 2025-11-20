#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "GlobalTypeEnum.h"
#include "ReferenceSystem.h"

typedef struct GroupProjectNumericDef GroupProjectNumericDef;
typedef struct GroupProjectUnlockDefRef GroupProjectUnlockDefRef;

// Five minute timeout for creating containers.
#define GROUP_PROJECT_CONTAINER_CREATE_TIMEOUT 5*60

typedef void (*CreateProjectContainerCB)(bool succeeded, void *userData);

AUTO_ENUM;
typedef enum GroupProjectContainerCreateState {
    CreateContainer_CreateStarted,
    CreateContainer_InitStarted,
    CreateContainer_WaitingForContainer,
    CreateContainer_Failed,
    CreateContainer_InitFailed,
} GroupProjectContainerCreateState;

AUTO_STRUCT;
typedef struct GroupProjectContainerCreateWaiter
{
    // This is needed so that structparser doesn't choke.
    int dummy;

    // Callback to call.
    CreateProjectContainerCB cbFunc;    NO_AST

    // User data.
    void *userData;     NO_AST
} GroupProjectContainerCreateWaiter;

//
// This struct is used to maintain state during creation of GroupProject containers, and to ensure
//  that the container creation is done only once, even if multiple operations arrive before the creation is complete.
//
AUTO_STRUCT;
typedef struct GroupProjectContainerCreateCBData {
    // The current state of this container creation.
    GroupProjectContainerCreateState state;

    // The type of the container being created.
    GlobalType containerType;

    // The ID of the container being created.
    ContainerID containerID;

    // The type of the owner.
    GlobalType ownerType;

    // The ID of the owner.
    ContainerID ownerID;

    // The time that the creation started. Used to timeout creations that never complete.
    U32 createTime;

    // List of waiters that need to be notified when the container is created.
    EARRAY_OF(GroupProjectContainerCreateWaiter) waiters;
} GroupProjectContainerCreateCBData;

AUTO_STRUCT;
typedef struct InitialProjectNames
{
    STRING_EARRAY initialProjectNames;    AST(POOL_STRING)
} InitialProjectNames;

AUTO_STRUCT;
typedef struct UnlocksForNumeric
{
    REF_TO(GroupProjectNumericDef) numericDef;      AST(KEY)
    EARRAY_OF(GroupProjectUnlockDefRef) unlocks;
} UnlocksForNumeric;

void aslGroupProject_CreateAndInitProjectContainer(GlobalType containerType, ContainerID containerID, GlobalType ownerType, ContainerID ownerID, CreateProjectContainerCB cbFunc, void *userData);
bool aslGroupProject_ProjectContainerExists(GlobalType containerType, ContainerID containerID, bool checkPending);
UnlocksForNumeric *aslGroupProject_GetUnlocksForNumeric(GroupProjectNumericDef *numericDef);
void aslGroupProject_DumpDebugLog(void);
