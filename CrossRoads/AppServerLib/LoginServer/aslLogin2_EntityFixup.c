/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_EntityFixup.h"
#include "GlobalTypes.h"
#include "objTransactions.h"
#include "stdtypes.h"
#include "earray.h"
#include "IntFIFO.h"
#include "aslLogin2_Error.h"

#include "AutoGen/aslLogin2_EntityFixup_c_ast.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

AUTO_STRUCT;
typedef struct PendingEntityFixupInfo
{
    GlobalType containerType;
    ContainerID containerID;
    bool fixupStarted;
    bool fixupComplete;
} PendingEntityFixupInfo;

AUTO_STRUCT;
typedef struct PlayerEntityFixupState
{
    ContainerID playerEntityID;
    ContainerID accountID;

    U32 timeStarted;

    // The list of dependent containers (saved pets, puppets and shared bank) that need to be fixed up.
    EARRAY_OF(PendingEntityFixupInfo) dependentContainers;

    // The number of dependent containers that have completed fixup.
    S32 fixupsCompleted;
    S32 fixupsStarted;

    bool dependentContainerListReceived;
    bool sharedBankCheckComplete;
    bool failed;

    bool fixupSharedBank;
    bool sharedBankExists;

    // String used to record errors for logging.
    STRING_MODIFIABLE errorString;          AST(ESTRING)

    // Completion callback data.
    RequestEntityFixupCB cbFunc;            NO_AST
    void *userData;                         NO_AST
} PlayerEntityFixupState;

// Fixup states are put on this queue when all their dependent container transactions are complete and they are
//  waiting to run the final transaction on the player entity.
static PointerFIFO *s_waitingForFinalFixup = NULL;

// The number of currently active fixup transactions.
static U32 s_numActiveTransactions = 0;

// This queue is used to hold fixup state for players waiting to start fixup.
static PointerFIFO *s_fixupQueue = NULL;

static S32 s_InitialFixupQueueSize = 256;
//__CATEGORY Settings that control Entity Structural Fixup queueing on the login server.
// Change the stating size of the Entity Structural Fixup Queue.  Note that the queue will automatically get larger if needed,
//  and changes to this setting will not effect anything once the queue is created when the first player is queued.
AUTO_CMD_INT(s_InitialFixupQueueSize, InitialFixupQueueSize) ACMD_AUTO_SETTING(EntityFixupQueue, LOGINSERVER);

static U32 s_MaximumActiveEntityFixupTransactions = 32;
// The maximum number of entity fixup transactions that can be running at one time on each loginserver.
AUTO_CMD_INT(s_MaximumActiveEntityFixupTransactions, MaximumActiveEntityFixupTransactions) ACMD_AUTO_SETTING(EntityFixupQueue, LOGINSERVER);

static void
EntityFixupComplete(PlayerEntityFixupState *fixupState)
{
    if ( fixupState->failed )
    {
        aslLogin2_Log("Entity fixup failed for player entity %d. %s", fixupState->playerEntityID, NULL_TO_EMPTY(fixupState->errorString));
    }
    else
    {
        aslLogin2_Log("Entity fixup succeeded for player entity %d.", fixupState->playerEntityID);
    }

    // Notify the caller that we are done.
    if ( fixupState->cbFunc )
    {
        (* fixupState->cbFunc)(fixupState->playerEntityID, !fixupState->failed, fixupState->userData);
    }

    // Clean up state.
    StructDestroy(parse_PlayerEntityFixupState, fixupState);
}

// Completion callback for player container fixup transactions.
void
PlayerContainerFixupCB(TransactionReturnVal *returnVal, PlayerEntityFixupState *fixupState)
{
    s_numActiveTransactions--;

    if ( returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE )
    {
        fixupState->failed = true;
        estrConcatf(&fixupState->errorString, "Transaction to fixup player entity container failed. accountID=%d, playerID=%d\n", fixupState->accountID, fixupState->playerEntityID);
    }

    // We are all done, so clean up and inform the caller.
    EntityFixupComplete(fixupState);
}

// Completion callback for dependent container fixup transactions.
void
DependentContainerFixupCB(TransactionReturnVal *returnVal, PlayerEntityFixupState *fixupState)
{
    fixupState->fixupsCompleted++;
    s_numActiveTransactions--;

    if ( returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE )
    {
        fixupState->failed = true;
        estrConcatf(&fixupState->errorString, "Transaction to fixup dependent container failed. accountID=%d, playerID=%d\n", fixupState->accountID, fixupState->playerEntityID);
    }

    if ( fixupState->failed )
    {
        if ( fixupState->fixupsStarted == fixupState->fixupsCompleted )
        {
            // All pending transactions have finished, so we can clean up now.
            EntityFixupComplete(fixupState);
        }
    }
    else
    {
        if ( fixupState->fixupsCompleted == eaSize(&fixupState->dependentContainers) )
        {
            // All dependent container fixups have completed, so we can queue the fixup of the player entity.
            PointerFIFO_Push(s_waitingForFinalFixup, fixupState);
        }
    }
}

void
aslLogin2_EntityFixupTick(void)
{
    // Only process the queue if it has been initialized.
    if ( s_fixupQueue != NULL )
    {
        PlayerEntityFixupState *fixupState;
        TransactionReturnVal *returnVal;
        GlobalType containerType;
        ContainerID containerID;

        // If there are any fixup states that are waiting to do their final player entity fixup, then do them first.
        while ( ( s_numActiveTransactions < s_MaximumActiveEntityFixupTransactions ) &&
            PointerFIFO_Count(s_waitingForFinalFixup) )
        {
            if ( PointerFIFO_Get(s_waitingForFinalFixup, &fixupState) == false )
            {
                devassertmsg(false, "Failed to get item from waitingForFinalFixup queue when it should not be empty.");
            }
            else
            {
                returnVal = objCreateManagedReturnVal(PlayerContainerFixupCB, fixupState);
                AutoTrans_entity_tr_MigrateEntityVersion(returnVal, GLOBALTYPE_LOGINSERVER, GLOBALTYPE_ENTITYPLAYER, fixupState->playerEntityID);

                s_numActiveTransactions++;
            }
        }

        while ( s_numActiveTransactions < s_MaximumActiveEntityFixupTransactions )
        {
            // Examine the first entry on the queue if one exists.
            if ( PointerFIFO_Peek(s_fixupQueue, &fixupState) )
            {
                while ( ( s_numActiveTransactions < s_MaximumActiveEntityFixupTransactions ) && 
                    ( fixupState->fixupsStarted < eaSize(&fixupState->dependentContainers) ) &&
                    !fixupState->failed )
                {
                    containerType = fixupState->dependentContainers[fixupState->fixupsStarted]->containerType;
                    containerID = fixupState->dependentContainers[fixupState->fixupsStarted]->containerID;

                    returnVal = objCreateManagedReturnVal(DependentContainerFixupCB, fixupState);
                    AutoTrans_entity_tr_MigrateEntityVersion(returnVal, GLOBALTYPE_LOGINSERVER, containerType, containerID);

                    fixupState->fixupsStarted++;
                    s_numActiveTransactions++;
                }

                if ( ( fixupState->fixupsStarted == eaSize(&fixupState->dependentContainers) ) || fixupState->failed )
                {
                    // Fixup has been requested for all dependent container, so remove the fixup state from the queue.
                    // Once the dependent container transactions are all complete we will fixup the player entity.
                    PointerFIFO_Get(s_fixupQueue, &fixupState);
                }
            }
            else
            {
                // Queue is empty.
                break;
            }
        }
    }
}

static void
QueueForEntityFixup(PlayerEntityFixupState *fixupState)
{
    if ( s_fixupQueue == NULL )
    {
        s_fixupQueue = PointerFIFO_Create(s_InitialFixupQueueSize);
        s_waitingForFinalFixup = PointerFIFO_Create(s_MaximumActiveEntityFixupTransactions);
    }

    PointerFIFO_Push(s_fixupQueue, fixupState);
}

static void
QueueForEntityFixupIfReady(PlayerEntityFixupState *fixupState)
{
    if ( fixupState->dependentContainerListReceived && fixupState->sharedBankCheckComplete )
    {
        if ( fixupState->failed )
        {
            // If we failed to get the data needed for the fixup, then clean up and report failure to the caller.
            EntityFixupComplete(fixupState);
        }
        else
        {
            QueueForEntityFixup(fixupState);
        }
    }
}

static void
SharedBankCheckCB(TransactionReturnVal *returnVal, PlayerEntityFixupState *fixupState)
{
    int result;
    fixupState->sharedBankCheckComplete = true;

    if ( RemoteCommandCheck_DBCheckSingleContainerExists(returnVal, &result) == TRANSACTION_OUTCOME_SUCCESS )
    {
        fixupState->sharedBankExists = result;
        if ( fixupState->sharedBankExists )
        {
            // If it exists, add the shared bank container to the list of dependent containers that should be fixed up.
            PendingEntityFixupInfo *entityFixupInfo = StructCreate(parse_PendingEntityFixupInfo);
            entityFixupInfo->containerType = GLOBALTYPE_ENTITYSHAREDBANK;
            entityFixupInfo->containerID = fixupState->accountID;
            eaPush(&fixupState->dependentContainers, entityFixupInfo);
        }
    }
    else
    {
        fixupState->sharedBankExists = false;
        fixupState->failed = true;
        estrConcatf(&fixupState->errorString, "aslLogin2_EntityFixup: Check for account shared bank existence failed.  accountID=%d, playerID=%d\n", fixupState->accountID, fixupState->playerEntityID);
    }

    QueueForEntityFixupIfReady(fixupState);
}

static void
DependentContainerCB(TransactionReturnVal *returnVal, PlayerEntityFixupState *fixupState)
{
    ContainerRefArray *dependentContainerRefs;

    fixupState->dependentContainerListReceived = true;

    if ( RemoteCommandCheck_DBReturnDependentContainers(returnVal, &dependentContainerRefs) == TRANSACTION_OUTCOME_SUCCESS )
    {
        int i;
        for ( i = eaSize(&dependentContainerRefs->containerRefs) - 1; i >= 0; i-- )
        {
            GlobalType containerType = dependentContainerRefs->containerRefs[i]->containerType;
            ContainerID containerID = dependentContainerRefs->containerRefs[i]->containerID;

            // Only perform migration on entity containers
            if ( (containerType == GLOBALTYPE_ENTITYPLAYER) || (containerType == GLOBALTYPE_ENTITYPUPPET) || (containerType == GLOBALTYPE_ENTITYSAVEDPET)) 
            {
                PendingEntityFixupInfo *entityFixupInfo = StructCreate(parse_PendingEntityFixupInfo);
                entityFixupInfo->containerType = containerType;
                entityFixupInfo->containerID = containerID;
                eaPush(&fixupState->dependentContainers, entityFixupInfo);
            }
        }
    }
    else
    {
        fixupState->failed = true;
        estrConcatf(&fixupState->errorString, "aslLogin2_EntityFixup: Getting list of dependent containers failed.  accountID=%d, playerID=%d\n", fixupState->accountID, fixupState->playerEntityID);
    }

    QueueForEntityFixupIfReady(fixupState);
}

// This function will handle entity structural fixup for the player entity, saved pets, and optionally the account shared bank.
// Since the need to perform fixup is based on the fixup version of the main player entity, we fix up all other entities first, and only fix up
//  the player if all the rest succeed.  This ensures that we don't end up with a player that has been fixed up but other dependent containers
//  that are still at the old version.
// This function operates within a single shard.
void
aslLogin2_RequestPlayerEntityFixup(ContainerID playerEntityID, ContainerID accountID, bool fixupSharedBank, RequestEntityFixupCB cbFunc, void *userData)
{
    PlayerEntityFixupState *fixupState;
    TransactionReturnVal *dependentContainerReturnVal;

    fixupState = StructCreate(parse_PlayerEntityFixupState);
    fixupState->playerEntityID = playerEntityID;
    fixupState->accountID = accountID;
    fixupState->fixupSharedBank = fixupSharedBank;
    fixupState->timeStarted = timeSecondsSince2000();
    fixupState->cbFunc = cbFunc;
    fixupState->userData = userData;

    // NOTE - we check if the shared bank exists and get the list of dependent containers in parallel.

    if ( fixupSharedBank )
    {
        TransactionReturnVal *sharedBankCheckReturnVal = objCreateManagedReturnVal(SharedBankCheckCB, fixupState);

        // Ask the objectDB if the shared bank container exists.
        RemoteCommand_DBCheckSingleContainerExists(sharedBankCheckReturnVal, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYSHAREDBANK, accountID);
    }
    else
    {
        // We can ignore the shard bank if it was not requested for fixup.
        fixupState->sharedBankExists = false;
        fixupState->sharedBankCheckComplete = true;
    }

    // Request the list of dependent containers from the ObjectDB.
    dependentContainerReturnVal = objCreateManagedReturnVal(DependentContainerCB, fixupState);
    RemoteCommand_DBReturnDependentContainers(dependentContainerReturnVal, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, playerEntityID);
}

#include "AutoGen/aslLogin2_EntityFixup_c_ast.c"