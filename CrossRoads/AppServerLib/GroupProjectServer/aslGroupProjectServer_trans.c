/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "aslGroupProjectServer.h"
#include "GroupProjectCommon.h"
#include "GroupProjectCommon_trans.h"
#include "itemCommon.h"
#include "earray.h"
#include "GlobalTypeEnum.h"
#include "AutoTransDefs.h"
#include "ReferenceSystem.h"
#include "timing.h"
#include "logging.h"
#include "AutoGen/GroupProjectCommon_h_ast.h"

AUTO_TRANS_HELPER;
bool
GroupProject_trh_AddProject(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *projectName)
{
    NOCONST(GroupProjectState) *projectState;
    GroupProjectDef *projectDef;
    int i;

    // Don't re-add the project if it already exists.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( NONNULL(projectState) )
    {
        return false;
    }

    // Create the project state struct.
    projectState = StructCreateNoConst(parse_GroupProjectState);

    // Set the reference to the def for this project.
    SET_HANDLE_FROM_STRING(g_GroupProjectDict, projectName, projectState->projectDef);

    // Get the project def.  Create fails if the project def does not exist.
    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        StructDestroyNoConst(parse_GroupProjectState, projectState);
        return false;
    }

    if ( GroupProject_ContainerTypeForProjectType(projectDef->type) != projectContainer->containerType )
    {
        // The project type does not match the container type.
        StructDestroyNoConst(parse_GroupProjectState, projectState);
        return false;
    }

    // Initialize the donation task slots
    for ( i = 0; i < eaiSize(&projectDef->slotTypes); i++ )
    {
        NOCONST(DonationTaskSlot) *slot;

        slot = StructCreateNoConst(parse_DonationTaskSlot);
        slot->state = DonationTaskState_None;
        slot->taskSlotType = projectDef->slotTypes[i];
        slot->taskSlotNum = i;

        eaPush(&projectState->taskSlots, slot);
    }

    // Add the project to the container.
    eaIndexedPushUsingStringIfPossible(&projectContainer->projectList, projectName, projectState);

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "AddProject", 
        "ProjectName %s", projectName);
    return true;
}

//
// Initialize the group project container.
//
AUTO_TRANS_HELPER;
enumTransactionOutcome 
GroupProject_trh_InitGroupProjectContainer(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, U32 containerType, U32 containerID, U32 ownerType, U32 ownerID, InitialProjectNames *initialProjectNames)
{
    int i;

    projectContainer->containerType = containerType;
    projectContainer->containerID = containerID;
    projectContainer->ownerType = ownerType;
    projectContainer->ownerID = ownerID;

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "InitGroupProjectContainer", "");

    if ( initialProjectNames )
    {
        for( i = 0; i < eaSize(&initialProjectNames->initialProjectNames); i++ )
        {
            if ( !GroupProject_trh_AddProject(ATR_PASS_ARGS, projectContainer, initialProjectNames->initialProjectNames[i]) )
            {
                return TRANSACTION_OUTCOME_FAILURE;
            }
        }
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, .Containerid, .Ownertype, .Ownerid, .Projectlist");
enumTransactionOutcome 
GroupProject_tr_InitGroupProjectContainer(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, 
    U32 containerType, U32 containerID, U32 ownerType, U32 ownerID, InitialProjectNames *initialProjectNames)
{
    return GroupProject_trh_InitGroupProjectContainer(ATR_PASS_ARGS, projectContainer, containerType, containerID, ownerType, ownerID, initialProjectNames);
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, projectList[]");
enumTransactionOutcome
GroupProject_tr_AddProject(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName)
{
    if ( GroupProject_trh_AddProject(ATR_PASS_ARGS, projectContainer, projectName) )
    {
        return TRANSACTION_OUTCOME_SUCCESS;
    }

    return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_SetUnlock(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *unlockName)
{
    NOCONST(GroupProjectUnlockDefRefContainer) *unlockDefRefContainer;
    GroupProjectUnlockDef *unlockDefRef;
    GroupProjectDef *projectDef;

    // Check if the unlock has already been set.
    unlockDefRefContainer = eaIndexedGetUsingString(&projectState->unlocks, unlockName);
    if ( NONNULL(unlockDefRefContainer) )
    {
        // The unlock has already been set.
        return true;
    }

    // Get the project def.
    projectDef = GET_REF(projectState->projectDef);
    if ( projectDef == NULL )
    {
        return false;
    }

    // Make sure the unlock is valid for the project.
    unlockDefRef = eaIndexedGetUsingString(&projectDef->unlockDefs, unlockName);
    if ( unlockDefRef == NULL )
    {
        return false;
    }

    // Create the unlock.
    unlockDefRefContainer = StructCreateNoConst(parse_GroupProjectUnlockDefRefContainer);
    SET_HANDLE_FROM_STRING(g_GroupProjectUnlockDict, unlockName, unlockDefRefContainer->unlockDef);

    // Add the unlock to the project state.
    eaIndexedPushUsingStringIfPossible(&projectState->unlocks, unlockName, unlockDefRefContainer);

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "SetUnlock", 
        "ProjectName %s UnlockName %s", projectDef->name, unlockName);

    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_ApplyNumeric(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *numericName, NumericOp op, S32 value)
{
    NOCONST(GroupProjectNumericData) *numericData;
    GroupProjectDef *projectDef;
    GroupProjectNumericDef *numericDef;
    GroupProjectNumericDefRef *numericDefRef;
    UnlocksForNumeric *unlocksForNumeric;
    int i;
    S32 oldValue = 0;
    S64 finalValue;

    numericData = eaIndexedGetUsingString(&projectState->numericData, numericName);

    projectDef = GET_REF(projectState->projectDef);
    if ( projectDef == NULL )
    {
        return false;
    }

    //XXX - add support for max/min values
    if ( numericData )
    {
        numericDef = GET_REF(numericData->numericDef);

        // Save the old value for logging.
        oldValue = numericData->numericVal;

        if ( op == NumericOp_Add )
        {
            finalValue = numericData->numericVal + value;
        }
        else if ( op == NumericOp_SetTo )
        {
            finalValue = value;
        }
        else
        {
            return false;
        }

    }
    else
    {
        numericDefRef = eaIndexedGetUsingString(&projectDef->validNumerics, numericName);
        if ( numericDefRef == NULL )
        {
            return false;
        }

        numericDef = GET_REF(numericDefRef->numericDef);
        if ( numericDef == NULL )
        {
            return false;
        }

        numericData = StructCreateNoConst(parse_GroupProjectNumericData);
        SET_HANDLE_FROM_STRING(g_GroupProjectNumericDict, numericName, numericData->numericDef);
        finalValue = value;

        eaIndexedPushUsingStringIfPossible(&projectState->numericData, numericName, numericData);
    }

    // NOTE - finalValue, numericDef and numericData must be valid for any case that gets here!

    // Clamp value to signed 32-bit
    if(finalValue > INT_MAX)
    {
        finalValue = INT_MAX;
    }
    else if (finalValue < INT_MIN)
    {
        finalValue = INT_MIN;
    }

    // If the numeric has a maximum value, then clamp it to the maximum.
    if ( ( numericDef->maxValue ) > 0 && ( finalValue > numericDef->maxValue ) )
    {
        finalValue = numericDef->maxValue;
    }

    numericData->numericVal = (S32)finalValue;

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "ApplyNumeric", 
        "ProjectName %s NumericName %s OldValue %d NewValue %d", 
        projectDef->name, numericName, oldValue, numericData->numericVal);

    // Process any unlocks that are triggered by this numeric.
    unlocksForNumeric = aslGroupProject_GetUnlocksForNumeric(numericDef);

    if ( NONNULL(unlocksForNumeric) )
    {
        for ( i = eaSize(&unlocksForNumeric->unlocks) - 1; i >= 0; i-- )
        {
            GroupProjectUnlockDefRef *unlockDefRef = unlocksForNumeric->unlocks[i];
            GroupProjectUnlockDef *unlockDef = GET_REF(unlockDefRef->unlockDef);
            if ( NONNULL(unlockDef) )
            {
                GroupProjectNumericDef *numericDefFromUnlock = GET_REF(unlockDef->numeric);
                devassert(numericDef == numericDefFromUnlock);
                devassert(unlockDef->type == UnlockType_NumericValueEqualOrGreater);

                if ( unlockDef->type == UnlockType_NumericValueEqualOrGreater )
                {
                    if ( numericData->numericVal >= unlockDef->triggerValue )
                    {
                        if ( !GroupProject_trh_SetUnlock(ATR_PASS_ARGS, projectState, unlockDef->name) )
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }

    return true;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, "projectList[]");
enumTransactionOutcome
GroupProject_tr_ApplyNumeric(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, const char *numericName, int numericOp, S32 value)
{
    NOCONST(GroupProjectState) *projectState;

    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( NONNULL(projectState) )
    {
        if ( GroupProject_trh_ApplyNumeric(ATR_PASS_ARGS, projectState, numericName, numericOp, value) )
        {
            return TRANSACTION_OUTCOME_SUCCESS;
        }
    }

    return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, "projectList[]");
enumTransactionOutcome
GroupProject_tr_SetUnlock(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, const char *unlockName)
{
    NOCONST(GroupProjectState) *projectState;

    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( NONNULL(projectState) )
    {
        if ( GroupProject_trh_SetUnlock(ATR_PASS_ARGS, projectState, unlockName) )
        {
            return TRANSACTION_OUTCOME_SUCCESS;
        }
    }

    return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_ClearUnlock(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *unlockName)
{
    NOCONST(GroupProjectUnlockDefRefContainer) *unlockDefRefContainer;
    GroupProjectUnlockDef *unlockDefRef;
    GroupProjectDef *projectDef;

    // Get the project def.
    projectDef = GET_REF(projectState->projectDef);
    if ( projectDef == NULL )
    {
        return false;
    }

    // Make sure the unlock is valid for the project.
    unlockDefRef = eaIndexedGetUsingString(&projectDef->unlockDefs, unlockName);
    if ( unlockDefRef == NULL )
    {
        return false;
    }

    // Check if the unlock has already been set.
    unlockDefRefContainer = eaIndexedGetUsingString(&projectState->unlocks, unlockName);
    if ( ISNULL(unlockDefRefContainer) )
    {
        // The unlock is not set.
        return false;
    }

    // Remove the unlock from the project state.
    eaFindAndRemove(&projectState->unlocks, unlockDefRefContainer);

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "ClearUnlock", 
        "ProjectName %s UnlockName %s", projectDef->name, unlockName);

    return true;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, "projectList[]");
enumTransactionOutcome
GroupProject_tr_ClearUnlock(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, const char *unlockName)
{
    NOCONST(GroupProjectState) *projectState;

    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( NONNULL(projectState) )
    {
        if ( GroupProject_trh_ClearUnlock(ATR_PASS_ARGS, projectState, unlockName) )
        {
            return TRANSACTION_OUTCOME_SUCCESS;
        }
    }

    return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_GrantTaskRewards(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, bool startRewards)
{
    GroupProjectDef *projectDef;
    DonationTaskDef *taskDef;
    GroupProjectNumericDef *numericDef;
    GroupProjectUnlockDef *unlockDef;
    int i;
    S32 value;
    EARRAY_OF(DonationTaskReward) rewards;

    if ( ISNULL(projectState) || ISNULL(taskSlot) )
    {
        return false;
    }

    // Get project def.
    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return false;
    }

    // Get donation task def.
    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return false;
    }

    if ( startRewards )
    {
        rewards = taskDef->taskStartRewards;
    }
    else
    {
        rewards = taskDef->taskRewards;
    }

    for ( i = 0; i < eaSize(&rewards); i++ )
    {
        DonationTaskReward *reward;
        NumericOp numericOp;

        reward = rewards[i];

        switch ( reward->rewardType )
        {
        case DonationTaskRewardType_NumericAdd:
        case DonationTaskRewardType_NumericSet:
            // Pick the correct NumericOp based on reward type.
            if ( reward->rewardType == DonationTaskRewardType_NumericAdd )
            {
                numericOp = NumericOp_Add;
            }
            else if ( reward->rewardType == DonationTaskRewardType_NumericSet )
            {
                numericOp = NumericOp_SetTo;
            }
            else
            {
                return false;
            }

            // Get the numeric def of the reward numeric.
            numericDef = GET_REF(reward->numericDef);
            if ( ISNULL(numericDef) )
            {
                return false;
            }

            // Get the constant value to reward.
            if ( !GroupProject_trh_GetProjectConstant(ATR_PASS_ARGS, projectState, reward->rewardConstant, &value) )
            {
                return false;
            }

            // Apply the reward value to the numeric.
            if ( !GroupProject_trh_ApplyNumeric(ATR_PASS_ARGS, projectState, numericDef->name, numericOp, value) )
            {
                return false;
            }
            break;

        case DonationTaskRewardType_Unlock:
            // Get the unlock def.
            unlockDef = GET_REF(reward->unlockDef);
            if ( ISNULL(unlockDef) )
            {
                return false;
            }

            // Grant the unlock.
            if ( !GroupProject_trh_SetUnlock(ATR_PASS_ARGS, projectState, unlockDef->name) )
            {
                return false;
            }
            break;

        default:
            return false;
        }
    }

    return true;
}

//
// This function promotes the next task to be the new active task.  There must be no current task, or it must be complete.
//
AUTO_TRANS_HELPER;
bool
GroupProject_trh_ActivateNextTask(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot)
{
    DonationTaskDef *taskDef;
    int i;
    GroupProjectDef *projectDef;

    if ( ISNULL(taskSlot) || ISNULL(projectState) )
    {
        return false;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return false;
    }

    // Fail if the slot is not either empty or in completed state.
    if ( ( taskSlot->state != DonationTaskState_None ) && ( taskSlot->state != DonationTaskState_Completed ) )
    {
        return false;
    }

    // Find the taskDef for the next task.
    taskDef = GET_REF(taskSlot->nextTaskDef);
    if ( ISNULL(taskDef) )
    {
        return false;
    }

    // Make sure the slot types match.
    devassert(taskDef->slotType == taskSlot->taskSlotType);

    // If there was a previous task it should have been cleaned out by now.
    devassert(REF_STRING_FROM_HANDLE(taskSlot->taskDef) == NULL);

    // Set the new task def.
    SET_HANDLE_FROM_STRING(g_DonationTaskDict, taskDef->name, taskSlot->taskDef);

    // Remove the next task, since it is being promoted to active task.
    REMOVE_HANDLE(taskSlot->nextTaskDef);

    // Set the state.
    taskSlot->state = DonationTaskState_AcceptingDonations;

    // Make sure the buckets are empty.
    devassert(eaSize(&taskSlot->buckets) == 0);

    // Initialize the buckets.
    for ( i = 0; i < eaSize(&taskDef->buckets); i++ )
    {
        NOCONST(DonationTaskBucketData) *bucketData = StructCreateNoConst(parse_DonationTaskBucketData);
        bucketData->bucketName = taskDef->buckets[i]->name;
        bucketData->donationCount = 0;
        eaPush(&taskSlot->buckets, bucketData);
    }

    // Initialize times.
    taskSlot->finalizedTime = 0;
    taskSlot->completionTime = 0;
    taskSlot->startTime = timeSecondsSince2000();

    // Reset the count of buckets that have been completed.
    taskSlot->completedBuckets = 0;

    // Grant task start rewards.
    if ( !GroupProject_trh_GrantTaskRewards(ATR_PASS_ARGS, projectState, taskSlot, true) )
    {
        return false;
    }

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "ActivateTask", 
        "ProjectName %s TaskSlot %d TaskName %s", projectDef->name, taskSlot->taskSlotNum, taskDef->name);

    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_SetNextTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, DonationTaskDef *nextTaskDef, const char *projectName)
{
    // NOTE - caller should validate that the task is valid for the current project.

    if ( ISNULL(taskSlot) || ISNULL(nextTaskDef) )
    {
        return false;
    }
    
    // Slot types must match.
    if ( nextTaskDef->slotType != taskSlot->taskSlotType )
    {
        return false;
    }

    // Clean up the previous "next task" reference if one exists.
    if ( NONNULL(REF_STRING_FROM_HANDLE(taskSlot->nextTaskDef)) )
    {
        REMOVE_HANDLE(taskSlot->nextTaskDef);
    }

    // Set the reference to the next task.
    SET_HANDLE_FROM_STRING(g_DonationTaskDict, nextTaskDef->name, taskSlot->nextTaskDef);

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "SetNextTask", 
        "ProjectName %s TaskSlot %d TaskName %s", projectName, taskSlot->taskSlotNum, nextTaskDef->name);

    return true;
}

extern int gDebugTimeAfterFinalizeToCompleteTasks;

AUTO_TRANS_HELPER;
U32
GroupProject_trh_GetTaskCompletionTime(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot)
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

AUTO_TRANS_HELPER;
bool
GroupProject_trh_CompleteTaskInternal(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, GroupProjectDef *projectDef, DonationTaskDef *taskDef)
{
    NOCONST(DonationTaskDefRefContainer) *taskDefRef;
    bool bCanceled = false;

    // Write log.
    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "CompleteTask", 
        "ProjectName %s TaskSlot %d TaskName %s", projectDef->name, taskSlot->taskSlotNum, taskDef->name);

    // Clear buckets.
    eaClearStructNoConst(&taskSlot->buckets, parse_DonationTaskBucketData);

    // Clear times.
    taskSlot->completionTime = 0;
    taskSlot->finalizedTime = 0;
    taskSlot->startTime = 0;

    if ( taskSlot->state == DonationTaskState_Canceled )
    {
        bCanceled = true;
    }

    // Set state to completed.
    taskSlot->state = DonationTaskState_Completed;

    if ( !bCanceled )
    {
        // Grant rewards.
        if ( !GroupProject_trh_GrantTaskRewards(ATR_PASS_ARGS, projectState, taskSlot, false) )
        {
            return false;
        }

        // Add non-repeatable tasks to the completed task list.
        if ( !taskDef->repeatable )
        {
            taskDefRef = StructCreateNoConst(parse_DonationTaskDefRefContainer);
            SET_HANDLE_FROM_REFERENT(g_DonationTaskDict, taskDef, taskDefRef->taskDef);
            if ( !eaIndexedAdd(&projectState->completedTasks, taskDefRef) )
            {
                return false;
            }
        }
    }

    // Remove the reference to the completed task.
    REMOVE_HANDLE(taskSlot->taskDef);

    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_CompleteTask(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot)
{
    U32 taskCompletionTime;
    DonationTaskDef *taskDef;
    GroupProjectDef *projectDef;

    if ( ISNULL(projectState) || ISNULL(taskSlot) )
    {
        return false;
    }

    // Make sure task is in correct state.
    devassert((taskSlot->state == DonationTaskState_Finalized) || (taskSlot->state == DonationTaskState_RewardClaimed) || (taskSlot->state == DonationTaskState_Canceled));
    if ( ( taskSlot->state != DonationTaskState_Finalized ) && (taskSlot->state != DonationTaskState_RewardClaimed) && ( taskSlot->state != DonationTaskState_Canceled ) )
    {
        return false;
    }

    // Make sure time has really expired.
    taskCompletionTime = GroupProject_trh_GetTaskCompletionTime(ATR_PASS_ARGS, taskSlot);
    devassert(taskCompletionTime <= timeSecondsSince2000());
    if ( taskCompletionTime > timeSecondsSince2000() )
    {
        return false;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return false;
    }

    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return false;
    }

    // Only tasks in finalized state need to check to see if they should go to RewardPending or do the actual completion.
    if ( taskSlot->state == DonationTaskState_Finalized )
    {
        if ( REF_STRING_FROM_HANDLE(taskDef->completionRewardTable) )
        {
            // Only player projects can have a completion reward table.
            if ( projectDef->type != GroupProjectType_Player )
            {
                return false;
            }

            // defer completion until the reward has been claimed.
            taskSlot->state = DonationTaskState_RewardPending;
            return true;
        }
    }

    return GroupProject_trh_CompleteTaskInternal(ATR_PASS_ARGS, projectState, taskSlot, projectDef, taskDef);
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, projectList[]");
enumTransactionOutcome
GroupProject_tr_SetNextTask(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int slotNum, const char *taskName)
{
    NOCONST(GroupProjectState) *projectState;
    GroupProjectDef *projectDef;
    DonationTaskDef *taskDef;
    DonationTaskDefRef *taskDefRef;
    NOCONST(DonationTaskSlot) *taskSlot;
    int i;

    if ( ISNULL(projectContainer) || ISNULL(projectName) || ISNULL(taskName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        // If the container doesn't contain project state for this project, then add it.
        if ( !GroupProject_trh_AddProject(ATR_PASS_ARGS, projectContainer, projectName) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( ISNULL(projectState) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    // Get the project def.
    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Validate the slot number.
    if ( ( slotNum < 0 ) || ( slotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task def.
    taskDefRef = eaIndexedGetUsingString(&projectDef->donationTaskDefs, taskName);
    if ( ISNULL(taskDefRef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }
    taskDef = GET_REF(taskDefRef->taskDef);
    if ( ISNULL(taskDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, slotNum);
    if ( ISNULL(taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // If it is a non-repeatable task, make sure that it has not been completed already.
    if ( !taskDef->repeatable )
    {
        NOCONST(DonationTaskDefRefContainer) *taskDefRefContainer;

        taskDefRefContainer = eaIndexedGetUsingString(&projectState->completedTasks, taskDef->name);
        if ( taskDefRefContainer != NULL )
        {
            // The task has already been completed.
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    // Make sure the task is not already active or queued for another slot.
    for ( i = eaSize(&projectState->taskSlots) - 1; i >= 0; i-- )
    {
        DonationTaskDef *slotTaskDef;

        // Matches with the current slot are ok, unless the task is non-repeatable.
        if ( i == slotNum )
        {
            if ( !taskDef->repeatable )
            {
                // Check the current task.
                slotTaskDef = GET_REF(projectState->taskSlots[i]->taskDef);
                if ( taskDef == slotTaskDef )
                {
                    return TRANSACTION_OUTCOME_FAILURE;
                }
            }
        }
        else
        {
            // Check the current task.
            slotTaskDef = GET_REF(projectState->taskSlots[i]->taskDef);
            if ( taskDef == slotTaskDef )
            {
                return TRANSACTION_OUTCOME_FAILURE;
            }

            // Check the next task.
            slotTaskDef = GET_REF(projectState->taskSlots[i]->nextTaskDef);
            if ( taskDef == slotTaskDef )
            {
                return TRANSACTION_OUTCOME_FAILURE;
            }
        }
    }

    // Set as next task.
    if ( !GroupProject_trh_SetNextTask(ATR_PASS_ARGS, taskSlot, taskDef, projectName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // If there is no current task, then promote this one.
    if ( ISNULL(REF_STRING_FROM_HANDLE(taskSlot->taskDef)) )
    {
        if ( !GroupProject_trh_ActivateNextTask(ATR_PASS_ARGS, projectState, taskSlot) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, "projectList[]");
enumTransactionOutcome
GroupProject_tr_CompleteTask(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int slotNum)
{
    NOCONST(GroupProjectState) *projectState;
    GroupProjectDef *projectDef;
    NOCONST(DonationTaskSlot) *taskSlot;

    if ( ISNULL(projectContainer) || ISNULL(projectName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the project def.
    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Validate the slot number.
    if ( ( slotNum < 0 ) || ( slotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, slotNum);
    if ( ISNULL(taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task in in the right state.
    if ( ( taskSlot->state != DonationTaskState_Finalized ) &&
        ( taskSlot->state != DonationTaskState_RewardClaimed ) &&
        ( taskSlot->state != DonationTaskState_Canceled ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    if ( !GroupProject_trh_CompleteTask(ATR_PASS_ARGS, projectState, taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // If this task is in complete state and there is a next task, then promote it.
    if ( ( taskSlot->state == DonationTaskState_Completed ) && NONNULL(REF_STRING_FROM_HANDLE(taskSlot->nextTaskDef) ) )
    {
        if ( !GroupProject_trh_ActivateNextTask(ATR_PASS_ARGS, projectState, taskSlot) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, projectList[]");
enumTransactionOutcome
GroupProject_tr_SetProjectMessage(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, const char *projectMessage)
{
    NOCONST(GroupProjectState) *projectState;

    if ( ISNULL(projectContainer) || ISNULL(projectName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        // If the container doesn't contain project state for this project, then add it.
        if ( !GroupProject_trh_AddProject(ATR_PASS_ARGS, projectContainer, projectName) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( ISNULL(projectState) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    // Make sure the string is not too long.
    if ( strlen(projectMessage) > PROJECT_MESSAGE_MAX_LEN )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // If there is an old message, free it.
    if ( projectState->projectMessage )
    {
        free(projectState->projectMessage);
    }

    // Set the new message.
    projectState->projectMessage = strdup(projectMessage);

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, projectList[]");
enumTransactionOutcome
GroupProject_tr_SetProjectPlayerName(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, const char *projectPlayerName)
{
    NOCONST(GroupProjectState) *projectState;

    if ( ISNULL(projectContainer) || ISNULL(projectName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        // If the container doesn't contain project state for this project, then add it.
        if ( !GroupProject_trh_AddProject(ATR_PASS_ARGS, projectContainer, projectName) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( ISNULL(projectState) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    // Make sure the string is not too long.
    if ( strlen(projectPlayerName) > PROJECT_PLAYER_NAME_MAX_LEN )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // If there is an old message, free it.
    if ( projectState->projectPlayerName )
    {
        free(projectState->projectPlayerName);
    }

    // Set the new message.
    projectState->projectPlayerName = strdup(projectPlayerName);

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
void
GroupProject_trh_FixupTaskSlot(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, const char *projectName)
{
    // Check to see if the next DonationTask still exists.
    if ( REF_IS_SET_BUT_ABSENT(taskSlot->nextTaskDef) )
    {
        TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
            "Project %s: nextTaskDef %s not found and removed.", projectName, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(taskSlot->nextTaskDef)));

        // Remove the next task, since it doesn't exist anymore.
        REMOVE_HANDLE(taskSlot->nextTaskDef);
    }

    if ( ( taskSlot->state == DonationTaskState_AcceptingDonations ) || ( taskSlot->state == DonationTaskState_Finalized ) )
    {
        // Check to see if the active DonationTask still exists.
        if ( REF_IS_SET_BUT_ABSENT(taskSlot->taskDef) )
        {
            TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                "Project %s: active taskDef %s not found.  Slot cleared.", projectName, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(taskSlot->taskDef)));

            // Clear out this task since it doesn't exist anymore.
            REMOVE_HANDLE(taskSlot->taskDef);

            // Clear buckets.
            eaClearStructNoConst(&taskSlot->buckets, parse_DonationTaskBucketData);

            // Clear times.
            taskSlot->completionTime = 0;
            taskSlot->finalizedTime = 0;
            taskSlot->startTime = 0;

            taskSlot->completedBuckets = 0;
            taskSlot->state = DonationTaskState_None;
        }
        else
        {
            DonationTaskDef *taskDef = GET_REF(taskSlot->taskDef);
            int i;

            if ( taskSlot->state == DonationTaskState_Finalized )
            {
                // Make sure time to complete is correct.
                if ( taskSlot->completionTime != ( taskDef->secondsToComplete + taskSlot->finalizedTime ) )
                {
                    taskSlot->completionTime = taskDef->secondsToComplete + taskSlot->finalizedTime;
                    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                        "Project %s: Task %s: update completion time to %d.", projectName, taskDef->name, taskSlot->completionTime);
                }
            }
            else if ( taskSlot->state == DonationTaskState_AcceptingDonations )
            {
                int completedBucketCount = 0;

                // First iterate the persisted buckets and get rid of any that don't match buckets in the def.
                for ( i = eaSize(&taskSlot->buckets) - 1; i >= 0; i-- )
                {
                    NOCONST(DonationTaskBucketData) *bucketData = taskSlot->buckets[i];
                    GroupProjectDonationRequirement *donationRequirement;
                    int bucketIndex;

                    bucketIndex = DonationTask_FindRequirement(taskDef, bucketData->bucketName);
                    if ( bucketIndex >= 0 )
                    {
                        donationRequirement = taskDef->buckets[bucketIndex];
                    }
                    else
                    {
                        donationRequirement = NULL;
                    }

                    if ( ISNULL(donationRequirement) )
                    {
                        TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                            "Project %s: Task %s: bucket %s does not exist in def.  Current donation count %d.  Removed bucket.", 
                            projectName, taskDef->name, bucketData->bucketName, bucketData->donationCount);

                        // The bucket doesn't exist in the def anymore, so remove it.
                        eaRemove(&taskSlot->buckets, i);
                        StructDestroyNoConst(parse_DonationTaskBucketData, bucketData);
                    }
                    else
                    {
                        // Keep count of any completed buckets.
                        if ( bucketData->donationCount >= donationRequirement->count )
                        {
                            completedBucketCount++;
                        }
                    }
                }

                if ( eaSize(&taskSlot->buckets) < eaSize(&taskDef->buckets) )
                {
                    // There are some new buckets that need to be added to the persisted data.
                    for ( i = eaSize(&taskDef->buckets) - 1; i >= 0; i-- )
                    {
                        GroupProjectDonationRequirement *donationRequirement = taskDef->buckets[i];
                        NOCONST(DonationTaskBucketData) *bucketData;

                        bucketData = eaIndexedGetUsingString(&taskSlot->buckets, donationRequirement->name);
                        if ( ISNULL(bucketData) )
                        {
                            TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                                "Project %s: Task %s: bucket %s does not exist in container.  Added bucket.", 
                                projectName, taskDef->name, donationRequirement->name);

                            // The bucket doesn't exist, so create it.
                            bucketData = StructCreateNoConst(parse_DonationTaskBucketData);
                            bucketData->bucketName = donationRequirement->name;
                            bucketData->donationCount = 0;
                            eaPush(&taskSlot->buckets, bucketData);
                        }
                    }
                }

                devassert( eaSize(&taskSlot->buckets) == eaSize(&taskDef->buckets) );

				if ( taskSlot->completedBuckets != (U32)completedBucketCount )
                {
                    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                        "Project %s: Task %s: updated completed bucket count.  old value=%d, new value=%d", 
                        projectName, taskDef->name, taskSlot->completedBuckets, completedBucketCount);

                    taskSlot->completedBuckets = completedBucketCount;
                }

                // If the donations are now complete, then finalize the task.  This could happen if the required quantity of a bucket is reduced.
                if ( completedBucketCount == eaSize(&taskSlot->buckets) )
                {
                    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                        "Project %s: Task %s: finalize task.", projectName, taskDef->name);

                    GroupProject_trh_FinalizeTask(ATR_PASS_ARGS, taskSlot, taskDef, projectName);
                }
            }
        }
    }
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_FixupProject(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState)
{
    int i;
    GroupProjectDef *projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_FAILURE(LOG_GROUPPROJECT, "FixupProject", 
            "Fixup failed.  Project %s not found.", NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(projectState->projectDef)));
        return false;
    }

    for ( i = eaSize(&projectState->taskSlots) - 1; i >= 0; i-- )
    {
        NOCONST(DonationTaskSlot) *taskSlot = projectState->taskSlots[i];
        GroupProject_trh_FixupTaskSlot(ATR_PASS_ARGS, taskSlot, projectDef->name);
    }

    return true;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Projectlist, .Containertype, .Containerid, .Ownertype, .Ownerid");
enumTransactionOutcome
GroupProject_tr_FixupProjectContainer(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, U32 containerType, U32 containerID, U32 ownerType, U32 ownerID, InitialProjectNames *initialProjectNames)
{
    int i;

    if ( eaSize(&projectContainer->projectList) == 0 )
    {
        if ( GroupProject_trh_InitGroupProjectContainer(ATR_PASS_ARGS, projectContainer, containerType, containerID, ownerType, ownerID, initialProjectNames) == TRANSACTION_OUTCOME_FAILURE )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    for ( i = eaSize(&projectContainer->projectList) - 1; i >= 0; i-- )
    {
        if ( !GroupProject_trh_FixupProject(ATR_PASS_ARGS, projectContainer->projectList[i]) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }
    return TRANSACTION_OUTCOME_SUCCESS;
}