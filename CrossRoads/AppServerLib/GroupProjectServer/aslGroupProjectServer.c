/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslGroupProjectServer.h"
#include "GroupProjectCommon.h"
#include "windefinclude.h"
#include "ServerLib.h"
#include "AppServerLib.h"
#include "timing.h"
#include "AutoTransDefs.h"
#include "AutoStartupSupport.h"
#include "ResourceManager.h"
#include "objSchema.h"
#include "objTransactions.h"
#include "StringCache.h"
#include "GlobalTypes.h"
#include "objIndex.h"
#include "LoggedTransactions.h"
#include "memlog.h"
#include "TimedCallback.h"

#include "AutoGen/aslGroupProjectServer_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GroupProjectCommon_h_ast.h"
#include "AutoGen/aslGroupProjectServer_c_ast.h"

U32 gDebugTimeAfterFinalizeToCompleteTasks = 0;
AUTO_CMD_INT(gDebugTimeAfterFinalizeToCompleteTasks, DebugTimeAfterFinalizeToCompleteTasks);

int gDebugLogObjIndex = 0;
AUTO_CMD_INT(gDebugLogObjIndex, DebugLogObjIndex);

static U32 sStartupTime = 0;
static bool sContainerLoadingDone = false;

static EARRAY_OF(GroupProjectContainerCreateCBData) sPendingContainerCreates = NULL;

static ObjectIndex *s_FinalizeIndex;

static EARRAY_OF(UnlocksForNumeric) s_UnlocksForNumerics = NULL;

static U32 *s_RewardClaimedList = NULL;

static MemLog s_debugMemLog = {0};

void
aslGroupProject_DumpDebugLog(void)
{
    memlog_dump(&s_debugMemLog);
}

static StashTable s_InitialProjectNames = NULL;

static InitialProjectNames *
GetInitialProjectNames(GlobalType containerType)
{
    InitialProjectNames *initialProjectNames = NULL;
    ResourceIterator iter = {0};
    GroupProjectDef *projectDef;

    if ( s_InitialProjectNames == NULL )
    {
        s_InitialProjectNames = stashTableCreateInt(8);
    }

    if ( stashIntFindPointer(s_InitialProjectNames, containerType, &initialProjectNames) )
    {
        return initialProjectNames;
    }

    if ( !resInitIterator(g_GroupProjectDict, &iter) )
    {
        return NULL;
    }

    initialProjectNames = StructCreate(parse_InitialProjectNames);

    while ( resIteratorGetNext(&iter, NULL, &projectDef) )
    {
        // Currently only support guild and personal projects.
        if ( ( ( projectDef->type == GroupProjectType_Guild ) && ( containerType == GLOBALTYPE_GROUPPROJECTCONTAINERGUILD ) ) || 
            ( ( projectDef->type == GroupProjectType_Player ) && ( containerType == GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER ) ) )
        {
            eaPush(&initialProjectNames->initialProjectNames, (char *)projectDef->name);
        }
    }
	resFreeIterator(&iter);

    stashIntAddPointer(s_InitialProjectNames, containerType, initialProjectNames, false);

    return initialProjectNames;
}

//
// Find if there is a currently pending container create.
//
static GroupProjectContainerCreateCBData *
GetPendingContainerCreateData(GlobalType containerType, ContainerID containerID)
{
    int i;

    // the array should be short, so just do a linear scan for now
    for( i = eaSize(&sPendingContainerCreates) - 1; i >= 0; i-- )
    {
        if ( ( sPendingContainerCreates[i]->containerType == containerType ) && ( sPendingContainerCreates[i]->containerID == containerID ) )
        {
            return sPendingContainerCreates[i];
        }
    }

    return NULL;
}

bool
aslGroupProject_ProjectContainerExists(GlobalType containerType, ContainerID containerID, bool checkPending)
{
    if ( objGetContainer(containerType, containerID) != NULL )
    {
        if ( !checkPending || ( GetPendingContainerCreateData(containerType, containerID) == NULL ) )
        {
            return true;
        }
    }

    return false;
}

static bool
ValidateTaskSlot(DonationTaskSlot *taskSlot)
{
    DonationTaskDef *taskDef;

    // Check to see if the next DonationTask still exists.
    if ( REF_IS_SET_BUT_ABSENT(taskSlot->nextTaskDef) )
    {
        return false;
    }

    if ( ( taskSlot->state == DonationTaskState_AcceptingDonations ) || ( taskSlot->state == DonationTaskState_Finalized ) )
    {
        // Check to see if the active DonationTask still exists.
        if ( REF_IS_SET_BUT_ABSENT(taskSlot->taskDef) )
        {
            return false;
        }

        taskDef = GET_REF(taskSlot->taskDef);
        if ( taskDef == NULL )
        {
            return false;
        }

        if ( taskSlot->state == DonationTaskState_Finalized )
        {
            // Time to complete must have changed.
            if ( taskDef->secondsToComplete + taskSlot->finalizedTime != taskSlot->completionTime )
            {
                return false;
            }
        } 
        else if ( taskSlot->state == DonationTaskState_AcceptingDonations )
        {
            // Validate buckets on any tasks that are accepting donations.
            int i;
            U32 completedBucketCount;

            // Number of buckets has changed.
            if ( eaSize(&taskSlot->buckets) != eaSize(&taskDef->buckets) )
            {
                return false;
            }

            completedBucketCount = 0;

            // Check all the buckets.
            for ( i = eaSize(&taskSlot->buckets) - 1; i >= 0; i-- )
            {
                GroupProjectDonationRequirement *donationRequirement;
                DonationTaskBucketData *bucketData;
                int bucketIndex;

                bucketData = taskSlot->buckets[i];
                if ( bucketData == NULL )
                {
                    return false;
                }

                bucketIndex = DonationTask_FindRequirement(taskDef, bucketData->bucketName);
                if ( bucketIndex < 0 )
                {
                    return false;
                }

                donationRequirement = taskDef->buckets[bucketIndex];
                if ( donationRequirement == NULL )
                {
                    return false;
                }

                if ( bucketData->donationCount >= donationRequirement->count )
                {
                    completedBucketCount++;
                }
            }

			if ( completedBucketCount != taskSlot->completedBuckets )
            {
                return false;
            }
        }
    }

    return true;
}

static bool
ValidateProjectState(GroupProjectState *projectState)
{
    GroupProjectDef *projectDef;
    int i;

    projectDef = GET_REF(projectState->projectDef);
    if ( projectDef == NULL )
    {
        return false;
    }

    for ( i = eaSize(&projectState->taskSlots) - 1; i >= 0; i-- )
    {
        DonationTaskSlot *taskSlot = projectState->taskSlots[i];
        if ( ValidateTaskSlot(taskSlot) == false )
        {
            return false;
        }
    }

    return true;
}

static bool
ValidateProjectContainer(GroupProjectContainer *projectContainer)
{
    int i;

    if ( eaSize(&projectContainer->projectList) == 0 )
    {
        return false;
    }

    for ( i = eaSize(&projectContainer->projectList) - 1; i >= 0; i-- )
    {
        GroupProjectState *projectState = projectContainer->projectList[i];
        if ( ValidateProjectState(projectState) == false )
        {
            return false;
        }
    }

    return true;
}

#define MAX_CONCURRENT_GROUP_PROJECT_FIXUPS 500

static INT_EARRAY s_GroupProjectFixupQueue = NULL;
static int s_GroupProjectFixupsComplete = 0;
static int s_ActiveGroupProjectFixups = 0;
static int s_GroupProjectFixupDone = false;
static int s_GroupProjectFixupStarted = false;
static int s_NumFixupsFailed = 0;
static int s_CurrentDefIndex = 0;

static void
GroupProjectFixup_CB(TransactionReturnVal *returnVal, void *cbData)
{
    if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        s_NumFixupsFailed++;
    }
    s_ActiveGroupProjectFixups--;
    s_GroupProjectFixupsComplete++;

    if ( ( s_GroupProjectFixupsComplete % 100 ) == 0 ) 
    {
        printf("Completed fixup of %d group project containers.  %d remaining.\n", 
            s_GroupProjectFixupsComplete, ea32Size(&s_GroupProjectFixupQueue) + s_ActiveGroupProjectFixups);
    }
    if ( ( s_ActiveGroupProjectFixups == 0 ) && ( ea32Size(&s_GroupProjectFixupQueue) == 0 ) )
    {
        printf("Completed fixup of all(%d) group project containers.  %d failed.\n", s_GroupProjectFixupsComplete, s_NumFixupsFailed);
        s_GroupProjectFixupDone = true;
    }
}

static void
RunGroupProjectFixup(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
    InitialProjectNames *initialProjectNames = NULL;

    while ( ( s_ActiveGroupProjectFixups < MAX_CONCURRENT_GROUP_PROJECT_FIXUPS ) && ( ea32Size(&s_GroupProjectFixupQueue) > 0 ) )
    {
        ContainerID containerID = ea32Pop(&s_GroupProjectFixupQueue);
        GlobalType containerType = ea32Pop(&s_GroupProjectFixupQueue);
        TransactionReturnVal *returnVal =  LoggedTransactions_CreateManagedReturnValObj("GroupProjectFixup", containerType, containerID, GroupProjectFixup_CB, NULL);
        GlobalType ownerType;
        s_ActiveGroupProjectFixups++;

        if ( containerType == GLOBALTYPE_GROUPPROJECTCONTAINERGUILD )
        {
            ownerType = GLOBALTYPE_GUILD;
        }
        else if ( containerType ==  GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER )
        {
            ownerType = GLOBALTYPE_ENTITYPLAYER;
        }
        else
        {
            Errorf("Unexpected group project container type in fixup queue: %d", containerType);
            continue;
        }

        initialProjectNames = GetInitialProjectNames(containerType);
        AutoTrans_GroupProject_tr_FixupProjectContainer(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, containerType, containerID, containerType, containerID, ownerType, containerID, initialProjectNames);
    }

    if ( ea32Size(&s_GroupProjectFixupQueue) > 0 )
    {
        // If there are more transactions to run, then run again in 1 second.
        TimedCallback_Run(RunGroupProjectFixup, NULL, 1);
    }
}

static void
ValidateAllContainers(void)
{
    ContainerIterator iter = {0};
    GroupProjectContainer *projectContainer;

    s_GroupProjectFixupStarted = true;

    // Add guild projects to fixup queue.
    objInitContainerIteratorFromType(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, &iter);
    while ( projectContainer = objGetNextObjectFromIterator(&iter) )
    {
        if ( ValidateProjectContainer(projectContainer) == false )
        {
            ea32Push(&s_GroupProjectFixupQueue, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD);
            ea32Push(&s_GroupProjectFixupQueue, projectContainer->containerID);
        }
    }
	objClearContainerIterator(&iter);

    // Add personal projects to fixup queue.
    objInitContainerIteratorFromType(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, &iter);
    while ( projectContainer = objGetNextObjectFromIterator(&iter) )
    {
        if ( ValidateProjectContainer(projectContainer) == false )
        {
            ea32Push(&s_GroupProjectFixupQueue, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER);
            ea32Push(&s_GroupProjectFixupQueue, projectContainer->containerID);
        }
    }
	objClearContainerIterator(&iter);

    if ( ea32Size(&s_GroupProjectFixupQueue) > 0 )
    {
        RunGroupProjectFixup(NULL, 0, NULL);
    }
    else
    {
        printf("No group project container fixup required.\n");
        s_GroupProjectFixupDone = true;
    }
}

static void
ProcessPendingContainers(void)
{
    int i, j;
    U32 curTime = timeSecondsSince2000();

    for( i = eaSize(&sPendingContainerCreates)-1; i >= 0; i-- )
    {
        GroupProjectContainerCreateCBData *pendingCreateData = sPendingContainerCreates[i];

        // Check if the container is now present.
        if ( pendingCreateData->state == CreateContainer_WaitingForContainer && aslGroupProject_ProjectContainerExists(pendingCreateData->containerType, pendingCreateData->containerID, false) )
        {
            // Remove from the pending list.
            eaRemove(&sPendingContainerCreates, i);

            // Notify waiters.
            for(j = 0; j < eaSize(&pendingCreateData->waiters); j++)
            {
                (* pendingCreateData->waiters[j]->cbFunc)(true, pendingCreateData->waiters[j]->userData);
            }

            StructDestroy(parse_GroupProjectContainerCreateCBData, pendingCreateData);
        }
        else if ( pendingCreateData->state == CreateContainer_Failed || pendingCreateData->state == CreateContainer_InitFailed ||
            ( pendingCreateData->createTime + GROUP_PROJECT_CONTAINER_CREATE_TIMEOUT ) < curTime )
        {
            const char *logString = NULL;

            // remove from the pending list
            eaRemove(&sPendingContainerCreates, i);

            if ( pendingCreateData->state == CreateContainer_Failed )
            {
                logString = "Container Create Failed";
            }
            else if ( pendingCreateData->state == CreateContainer_InitFailed )
            {
                logString = "Container Init Failed";
            }
            else
            {
                logString = "Container Create Timeout";
            }

            Errorf("GroupProject container create failed: %s", logString);

            // Notify waiters.
            for(j = 0; j < eaSize(&pendingCreateData->waiters); j++)
            {
                (* pendingCreateData->waiters[j]->cbFunc)(false, pendingCreateData->waiters[j]->userData);
            }

            StructDestroy(parse_GroupProjectContainerCreateCBData, pendingCreateData);
        }
    }
}

static void
InitContainer_CB(TransactionReturnVal *pReturn, GroupProjectContainerCreateCBData *pendingCreateData)
{
    if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
        devassert(pendingCreateData->state == CreateContainer_InitStarted);

        pendingCreateData->state = CreateContainer_WaitingForContainer;
    }
    else
    {
        pendingCreateData->state = CreateContainer_InitFailed;
    }
}

static void
CreateContainer_CB(TransactionReturnVal *pReturn, GroupProjectContainerCreateCBData *pendingCreateData)
{
    if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
        TransactionReturnVal *pNewReturn;
        InitialProjectNames *initialProjectNames;

        devassert(pendingCreateData->state == CreateContainer_CreateStarted);

        // Update state.
        pendingCreateData->state = CreateContainer_InitStarted;

        initialProjectNames = GetInitialProjectNames(pendingCreateData->containerType);

        // Run the init transaction to initialize the container.
        pNewReturn = LoggedTransactions_CreateManagedReturnValObj("InitContainer", pendingCreateData->containerType, pendingCreateData->containerID, InitContainer_CB, pendingCreateData);
        AutoTrans_GroupProject_tr_InitGroupProjectContainer(pNewReturn, GLOBALTYPE_GROUPPROJECTSERVER, pendingCreateData->containerType, pendingCreateData->containerID, 
            pendingCreateData->containerType, pendingCreateData->containerID, pendingCreateData->ownerType, pendingCreateData->ownerID, initialProjectNames);
    }
    else
    {
        pendingCreateData->state = CreateContainer_Failed;
    }
}

//
// Create a new GroupProjectContainer.
// This should only be called if the container doesn't already exist.
//
void
aslGroupProject_CreateAndInitProjectContainer(GlobalType containerType, ContainerID containerID, GlobalType ownerType, ContainerID ownerID, CreateProjectContainerCB cbFunc, void *userData)
{
    GroupProjectContainerCreateCBData *pendingCreateData;
    GroupProjectContainerCreateWaiter *waiter;

    // Set up waiter struct.
    waiter = StructCreate(parse_GroupProjectContainerCreateWaiter);
    waiter->cbFunc = cbFunc;
    waiter->userData = userData;

    pendingCreateData = GetPendingContainerCreateData(containerType, containerID);

    if ( pendingCreateData != NULL )
    {
        devassert( ( pendingCreateData->ownerType == ownerType ) && ( pendingCreateData->ownerID == ownerID ) );

        // There is already a pending create for this container, so just add another waiter.
        eaPush(&pendingCreateData->waiters, waiter);
    }
    else
    {
        TransactionRequest *request = objCreateTransactionRequest();

        // create the struct that tracks pending container creates
        pendingCreateData = StructCreate(parse_GroupProjectContainerCreateCBData);

        pendingCreateData->containerType = containerType;
        pendingCreateData->containerID = containerID;
        pendingCreateData->ownerType = ownerType;
        pendingCreateData->ownerID = ownerID;

        pendingCreateData->createTime = timeSecondsSince2000();
        pendingCreateData->state = CreateContainer_CreateStarted;

        eaPush(&pendingCreateData->waiters, waiter);

        // add it to the global list of pending creates
        eaPush(&sPendingContainerCreates, pendingCreateData);

        // This manually built transaction is required to force the containerID to be the accountID
        objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
            "VerifyContainer containerIDVar %s %d",
            GlobalTypeToName(containerType),
            containerID);

        // Move the container to the auction server
        objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, "containerIDVar", 
            "MoveContainerTo containerVar %s TRVAR_containerIDVar %s %d",
            GlobalTypeToName(containerType), GlobalTypeToName(GLOBALTYPE_GROUPPROJECTSERVER), 0);

        objAddToTransactionRequestf(request, GLOBALTYPE_GROUPPROJECTSERVER, 0, "containerVar containerIDVar ContainerVarBinary", 
            "ReceiveContainerFrom %s TRVAR_containerIDVar ObjectDB 0 TRVAR_containerVar",
            GlobalTypeToName(containerType));

        objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
            objCreateManagedReturnVal(CreateContainer_CB, pendingCreateData), "CreateGroupProjectContainer", request);
        objDestroyTransactionRequest(request);
    }
}

static void
ContainerLoadingDone(void)
{
    printf("Player group project container transfer complete\n");
    sContainerLoadingDone = true;
}

static void
GuildContainerLoadingDone(void)
{
    printf("Guild group project container transfer complete\n");

    aslAcquireContainerOwnership(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, ContainerLoadingDone);
}

AUTO_STARTUP(GroupProjectServer) ASTRT_DEPS(GroupProjects);
void 
aslGroupProjectServerStartup(void)
{
}

static U32
GetTaskCompletionTime(DonationTaskSlot *taskSlot)
{
    // Debugging option to cause task completion to happen at a specified time after task was finalized.
    if ( gDebugTimeAfterFinalizeToCompleteTasks )
    {
        return(taskSlot->finalizedTime + gDebugTimeAfterFinalizeToCompleteTasks);
    }
    else
    {
        return(taskSlot->completionTime);
    }
}

static U32
GetNextTaskCompletionTime(GroupProjectContainer *projectContainer)
{
    U32 earliestCompletionTime = U32_MAX;

    if ( projectContainer )
    {
        int i;
        for( i = eaSize(&projectContainer->projectList) - 1; i >= 0; i-- )
        {
            int j;
            GroupProjectState *projectState = projectContainer->projectList[i];

            for ( j = eaSize(&projectState->taskSlots) - 1; j >= 0; j-- )
            {
                DonationTaskSlot *taskSlot = projectState->taskSlots[j];

                if ( ( taskSlot->state == DonationTaskState_Finalized || taskSlot->state == DonationTaskState_Canceled ) && !taskSlot->completionRequested )
                {
                    U32 taskCompletionTime;

                    taskCompletionTime = GetTaskCompletionTime(taskSlot);

                    if ( taskCompletionTime < earliestCompletionTime )
                    {
                        earliestCompletionTime = taskCompletionTime;
                    }
                }
            }
        }
    }

    if ( earliestCompletionTime == U32_MAX )
    {
        return 0;
    }

    return earliestCompletionTime;
}

//
// Return true if any of the tasks in the project container are in the RewardClaimed state.
//
static bool
GetAnyTasksRewardClaimed(GroupProjectContainer *projectContainer)
{
    // Only player projects can have claimed rewards.
    if ( projectContainer && ( projectContainer->containerType == GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER ) )
    {
        int i;
        for( i = eaSize(&projectContainer->projectList) - 1; i >= 0; i-- )
        {
            int j;
            GroupProjectState *projectState = projectContainer->projectList[i];

            for ( j = eaSize(&projectState->taskSlots) - 1; j >= 0; j-- )
            {
                DonationTaskSlot *taskSlot = projectState->taskSlots[j];

                if ( ( taskSlot->state == DonationTaskState_RewardClaimed ) && !taskSlot->completionRequested )
                {
                    return true;
                }
            }
        }
    }

    return false;
}

void
AddContainerToIndices(GroupProjectContainer *projectContainer)
{
    U32 nextCompletionTime = GetNextTaskCompletionTime(projectContainer);

    devassert(projectContainer->nextCompletionTime == 0);

    if ( nextCompletionTime != 0 )
    {
        if ( gDebugLogObjIndex )
        {
            memlog_printf(&s_debugMemLog, "Adding container %d to finalize index with completion time %u", projectContainer->containerID, nextCompletionTime);
        }
        projectContainer->nextCompletionTime = nextCompletionTime;
        objIndexInsert(s_FinalizeIndex, projectContainer);
    }

    // If any of the tasks in this container are in the RewardClaimed state, then add it to the list so that we can complete the task.
    if ( GetAnyTasksRewardClaimed(projectContainer) )
    {
        if ( gDebugLogObjIndex )
        {
            memlog_printf(&s_debugMemLog, "Adding container %d to reward claimed list", projectContainer->containerID);
        }
        ea32PushUnique(&s_RewardClaimedList, projectContainer->containerID);
    }
}

U32
RemoveContainerFromIndices(GroupProjectContainer *projectContainer)
{
    U32 nextCompletionTime = projectContainer->nextCompletionTime;

    if ( nextCompletionTime != 0 )
    {
        if ( gDebugLogObjIndex )
        {
            memlog_printf(&s_debugMemLog, "Removing container %d from finalize index", projectContainer->containerID);
        }
        objIndexRemove(s_FinalizeIndex, projectContainer);
        projectContainer->nextCompletionTime = 0;
    }

    return nextCompletionTime;
}

static void 
AddProjectContainerCB(Container *con, GroupProjectContainer *projectContainer)
{
    AddContainerToIndices(projectContainer);
}

static void 
RemoveProjectContainerCB(Container *con, GroupProjectContainer *projectContainer)
{	
    RemoveContainerFromIndices(projectContainer);
}

static void 
CommitProjectStatePreCB(Container *con, ObjectPathOperation **operations)
{
    GroupProjectContainer *projectContainer = (GroupProjectContainer *)con->containerData;

    RemoveContainerFromIndices(projectContainer);
}

static void 
CommitProjectStatePostCB(Container *con, ObjectPathOperation **operations)
{
    GroupProjectContainer *projectContainer = (GroupProjectContainer *)con->containerData;

    AddContainerToIndices(projectContainer);
}

static void
InitCompletionIndex(void)
{
    //For updating indices.
    objRegisterContainerTypeAddCallback(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, AddProjectContainerCB);
    objRegisterContainerTypeRemoveCallback(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, RemoveProjectContainerCB);
    objRegisterContainerTypeCommitCallback(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, CommitProjectStatePreCB, ".Projectlist[*].Taskslots[*].State", true, false, true, NULL);
    objRegisterContainerTypeCommitCallback(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, CommitProjectStatePostCB, ".Projectlist[*].Taskslots[*].State", true, false, false, NULL);

    objRegisterContainerTypeAddCallback(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, AddProjectContainerCB);
    objRegisterContainerTypeRemoveCallback(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, RemoveProjectContainerCB);
    objRegisterContainerTypeCommitCallback(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, CommitProjectStatePreCB, ".Projectlist[*].Taskslots[*].State", true, false, true, NULL);
    objRegisterContainerTypeCommitCallback(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, CommitProjectStatePostCB, ".Projectlist[*].Taskslots[*].State", true, false, false, NULL);

    s_FinalizeIndex = objIndexCreateWithStringPaths(4, 0, parse_GroupProjectContainer, ".nextCompletionTime", ".containerID", NULL);
}

AUTO_STRUCT;
typedef struct CompleteTaskCBData
{
    GlobalType containerType;
    ContainerID containerID;
    GlobalType ownerType;
    ContainerID ownerID;
    STRING_POOLED projectName;  AST(POOL_STRING)
    U32 taskSlotNum;
} CompleteTaskCBData;

static void
CompleteTask_CB(TransactionReturnVal *returnVal, CompleteTaskCBData *cbData)
{
    if ( cbData != NULL )
    {
        // If the transaction completed successfully, then clear the completion requested flag.
        if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
        {
            GroupProjectContainer *projectContainer = (GroupProjectContainer *)objGetContainerData(cbData->containerType, cbData->containerID);
            if ( projectContainer != NULL )
            {
                GroupProjectState *projectState = eaIndexedGetUsingString(&projectContainer->projectList, cbData->projectName);
                if ( projectState != NULL ) 
                {
                    DonationTaskSlot *taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, cbData->taskSlotNum);
                    if ( taskSlot != NULL )
                    {
                        taskSlot->completionRequested = false;
                    }
                }
            }
        }

        if ( cbData->ownerType == GLOBALTYPE_ENTITYPLAYER )
        {
            RemoteCommand_gslGroupProject_TaskCompleteCB(cbData->ownerType, cbData->ownerID, cbData->ownerID, cbData->projectName);
        }

        StructDestroy(parse_CompleteTaskCBData, cbData);
    }
}

static void
ProcessCompletedTasks(void)
{
    ObjectIndexIterator iter = {0};
    GroupProjectContainer *projectContainer;
    U32 currentTime = timeSecondsSince2000();
    int i, j, k;
    CompleteTaskCBData *cbData;
    static EARRAY_OF(CompleteTaskCBData) s_completedTasks = NULL;

	objIndexObtainReadLock(s_FinalizeIndex);
    // Collect all tasks in finalized state.
    if ( objIndexGetIterator(s_FinalizeIndex, &iter, ITERATE_FORWARD) )
    {
        while ( projectContainer = (GroupProjectContainer *)objIndexGetNext(&iter) )
        {
            // Stop when we get to containers that have a next completion time in the future.
            if ( projectContainer->nextCompletionTime > currentTime )
            {
                break;
            }

            for ( i = eaSize(&projectContainer->projectList) - 1; i >= 0; i-- )
            {
                GroupProjectState *projectState = projectContainer->projectList[i];
                for ( j = eaSize(&projectState->taskSlots) - 1; j >= 0; j-- )
                {
                    DonationTaskSlot *taskSlot = projectState->taskSlots[j];
                    if ( ( taskSlot->state == DonationTaskState_Finalized || taskSlot->state == DonationTaskState_Canceled ) &&
                        ( GetTaskCompletionTime(taskSlot) <= currentTime ) && !taskSlot->completionRequested )
                    {
                        // Task is ready to complete.
                        const char *projectName = REF_STRING_FROM_HANDLE(projectState->projectDef);
                        cbData = StructCreate(parse_CompleteTaskCBData);

                        // Initialize data for the callback.
                        cbData->containerID = projectContainer->containerID;
                        cbData->containerType = projectContainer->containerType;
                        cbData->ownerID = projectContainer->ownerID;
                        cbData->ownerType = projectContainer->ownerType;
                        cbData->projectName = allocAddString(projectName);
                        cbData->taskSlotNum = taskSlot->taskSlotNum;

                        // Flag this task so that we don't request the transaction multiple times.
                        taskSlot->completionRequested = true;

                        // Save the completed tasks and process them after we complete the iteration.
                        eaPush(&s_completedTasks, cbData);
                    }
                }
            }
        }
    }
	objIndexReleaseReadLock(s_FinalizeIndex);

    // Collect all tasks in RewardComplete state.
    for ( k = ea32Size(&s_RewardClaimedList) - 1; k >= 0; k-- )
    {
        projectContainer = (GroupProjectContainer *)objGetContainerData(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, s_RewardClaimedList[k]);
        if ( projectContainer )
        {
            for ( i = eaSize(&projectContainer->projectList) - 1; i >= 0; i-- )
            {
                GroupProjectState *projectState = projectContainer->projectList[i];
                for ( j = eaSize(&projectState->taskSlots) - 1; j >= 0; j-- )
                {
                    DonationTaskSlot *taskSlot = projectState->taskSlots[j];
                    if ( ( taskSlot->state == DonationTaskState_RewardClaimed ) && !taskSlot->completionRequested )
                    {
                        // Task is ready to complete.
                        const char *projectName = REF_STRING_FROM_HANDLE(projectState->projectDef);
                        cbData = StructCreate(parse_CompleteTaskCBData);

                        // Initialize data for the callback.
                        cbData->containerID = projectContainer->containerID;
                        cbData->containerType = projectContainer->containerType;
                        cbData->ownerID = projectContainer->ownerID;
                        cbData->ownerType = projectContainer->ownerType;
                        cbData->projectName = allocAddString(projectName);
                        cbData->taskSlotNum = taskSlot->taskSlotNum;

                        // Flag this task so that we don't request the transaction multiple times.
                        taskSlot->completionRequested = true;

                        // Save the completed tasks and process them after we complete the iteration.
                        eaPush(&s_completedTasks, cbData);
                    }
                }
            }
        }
    }
    // The loop above has processed the entire list of containers in the RewareClaimed state, so we can now clear it.
    ea32Clear(&s_RewardClaimedList);

    if ( gDebugLogObjIndex )
    {
        memlog_printf(&s_debugMemLog, "Done iterating completed tasks at %u", currentTime);
    }

    // Call the transactions to complete the tasks.
    for ( i = eaSize(&s_completedTasks) - 1; i >= 0; i-- )
    {                        
        TransactionReturnVal *returnVal;
        cbData = s_completedTasks[i];
        s_completedTasks[i] = NULL;

        returnVal = LoggedTransactions_CreateManagedReturnValObj("CompleteTask", cbData->containerType, cbData->containerID, CompleteTask_CB, cbData);

        AutoTrans_GroupProject_tr_CompleteTask(returnVal, GLOBALTYPE_GROUPPROJECTSERVER, cbData->containerType, cbData->containerID, 
            cbData->projectName, cbData->taskSlotNum);
    }

    eaClear(&s_completedTasks);
}

static void
AddUnlockToNumericMapping(GroupProjectUnlockDef *unlockDef)
{
    UnlocksForNumeric *unlocksForNumeric;
    GroupProjectNumericDef *numericDef;
    GroupProjectUnlockDefRef *unlockDefRef;

    if ( unlockDef && unlockDef->type == UnlockType_NumericValueEqualOrGreater )
    {
        if ( s_UnlocksForNumerics == NULL )
        {
            eaIndexedEnable(&s_UnlocksForNumerics, parse_UnlocksForNumeric);
        }

        numericDef = GET_REF(unlockDef->numeric);
        if ( numericDef == NULL )
        {
            Errorf("NumericValueEqualOrGreater unlock %s refers to non-existent numeric", unlockDef->name);
            return;
        }

        unlocksForNumeric = eaIndexedGetUsingString(&s_UnlocksForNumerics, numericDef->name);
        if ( unlocksForNumeric == NULL )
        {
            // There is no entry for this numeric, so create one.
            unlocksForNumeric = StructCreate(parse_UnlocksForNumeric);
            SET_HANDLE_FROM_REFERENT(g_GroupProjectNumericDict, numericDef, unlocksForNumeric->numericDef);
            eaIndexedAdd(&s_UnlocksForNumerics, unlocksForNumeric);
        }

        unlockDefRef = eaIndexedGetUsingString(&unlocksForNumeric->unlocks, unlockDef->name);
        if ( unlockDefRef == NULL )
        {
            // There is no entry for this unlock, so create one.
            unlockDefRef = StructCreate(parse_GroupProjectUnlockDefRef);
            SET_HANDLE_FROM_REFERENT(g_GroupProjectUnlockDict, unlockDef, unlockDefRef->unlockDef);
            eaIndexedAdd(&unlocksForNumeric->unlocks, unlockDefRef);
        }
    }
}

static void
BuildUnlockNumericMapping(void)
{
    RefDictIterator iter = {0};
    GroupProjectUnlockDef *unlockDef = NULL;

    RefSystem_InitRefDictIterator(g_GroupProjectUnlockDict, &iter);
    while ( unlockDef = RefSystem_GetNextReferentFromIterator(&iter) )
    {
        AddUnlockToNumericMapping(unlockDef);
    }
}

UnlocksForNumeric *
aslGroupProject_GetUnlocksForNumeric(GroupProjectNumericDef *numericDef)
{
    UnlocksForNumeric *unlocksForNumeric;
    unlocksForNumeric = eaIndexedGetUsingString(&s_UnlocksForNumerics, numericDef->name);

    return unlocksForNumeric;
}

static int 
GroupProjectServerOncePerFrame(F32 fElapsed)
{
    static bool bOnce = false;
    static bool ready = false;
    static U32 lastFrameTimeSeconds = 0;
    U32 currentTime = timeSecondsSince2000();

    if(!bOnce) {
        sStartupTime = timeSecondsSince2000();

        InitCompletionIndex();

        aslAcquireContainerOwnership(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, GuildContainerLoadingDone);

        ATR_DoLateInitialization();

        BuildUnlockNumericMapping();
        bOnce = true;
    }

    if(sContainerLoadingDone)
    {
        if ( !s_GroupProjectFixupStarted )
        {
            // Validate all containers.  This checks that their state is consistent with current design data,
            //  and will perform fixup if the design data has changed in a way that requires the persisted data to change
            //  as well.
            ValidateAllContainers();
        }

        if ( s_GroupProjectFixupDone )
        {
            if ( !ready )
            {
                ready = true;
                RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
            }
            ProcessPendingContainers();

            if ( lastFrameTimeSeconds != currentTime )
            {
                lastFrameTimeSeconds = currentTime;

                if ( gDebugLogObjIndex )
                {
                    memlog_printf(&s_debugMemLog, "Start processing completed tasks at %u", currentTime);
                }

                ProcessCompletedTasks();

                if ( gDebugLogObjIndex )
                {
                    memlog_printf(&s_debugMemLog, "Done processing completed tasks at %u", currentTime);
                }
            }
        }
    }

    return 1;
}

int GroupProjectServerInit(void)
{
    memlog_init(&s_debugMemLog);

    AutoStartup_SetTaskIsOn("GroupProjectServer", 1);
    AutoStartup_RemoveAllDependenciesOn("WorldLib");

    loadstart_printf("Running Auto Startup...");
    DoAutoStartup();
    loadend_printf(" done.");

    resFinishLoading();

    objRegisterNativeSchema(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, parse_GroupProjectContainer, NULL, NULL, NULL, NULL, NULL);
    objRegisterNativeSchema(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, parse_GroupProjectContainer, NULL, NULL, NULL, NULL, NULL);

    objLoadAllGenericSchemas();

    stringCacheFinalizeShared();

    assertmsg(GetAppGlobalType() == GLOBALTYPE_GROUPPROJECTSERVER, "Group Project server type not set");

    loadstart_printf("Connecting GroupProjectServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

    while (!InitObjectTransactionManager(GetAppGlobalType(),
        gServerLibState.containerID,
        gServerLibState.transactionServerHost,
        gServerLibState.transactionServerPort,
        gServerLibState.bUseMultiplexerForTransactions, NULL)) {
            Sleep(1000);
    }
    if (!objLocalManager()) {
        loadend_printf("Failed.");
        return 0;
    }

    loadend_printf("Connected.");

    gAppServer->oncePerFrame = GroupProjectServerOncePerFrame;

    return 1;
}

AUTO_RUN;
int RegisterGroupProjectServer(void)
{
    aslRegisterApp(GLOBALTYPE_GROUPPROJECTSERVER, GroupProjectServerInit, 0);
    return 1;
}

#include "AutoGen/aslGroupProjectServer_h_ast.c"
#include "AutoGen/aslGroupProjectServer_c_ast.c"