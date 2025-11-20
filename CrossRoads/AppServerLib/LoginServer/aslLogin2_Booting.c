/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_Booting.h"
#include "Login2ServerCommon.h"
#include "EString.h"
#include "stdtypes.h"
#include "StashTable.h"
#include "StringCache.h"
#include "PlayerBooter.h"
#include "aslLogin2_Error.h"
#include "aslLogin2_Util.h"
#include "aslLogin2_StateMachine.h"
#include "aslLogin2_ClientComm.h"

#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/aslLogin2_Booting_c_ast.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

//
// Do gameserver booting, possibly across shards.
//

// StashTable to keep track of boot requests that are in progress.
static StashTable s_PendingPlayerBootRequests = NULL;

AUTO_STRUCT;
typedef struct BootPlayerState
{
    ContainerID playerID;
    STRING_POOLED shardName;                AST(POOL_STRING)

    U32 timeStarted;
    U64 requestToken;

    bool failed;

    // String used to record errors for logging.
    STRING_MODIFIABLE errorString;          AST(ESTRING)

    // Completion callback data.
    BootPlayerCB cbFunc;                    NO_AST
    void *userData;                         NO_AST
} BootPlayerState;

static bool
AddActiveRequest(BootPlayerState *bootState)
{
    // Create the stash table if it does not already exist;
    if ( s_PendingPlayerBootRequests == NULL )
    {
        s_PendingPlayerBootRequests = stashTableCreateInt(50);
    }

    return stashIntAddPointer(s_PendingPlayerBootRequests, (int)bootState->playerID, bootState, false);
}

static BootPlayerState *
GetActiveRequest(ContainerID playerID)
{
    BootPlayerState *bootState = NULL;

    // Create the stash table if it does not already exist;
    if ( s_PendingPlayerBootRequests == NULL )
    {
        s_PendingPlayerBootRequests = stashTableCreateInt(50);
        return NULL;
    }

    if ( stashIntFindPointer(s_PendingPlayerBootRequests, (int)playerID, &bootState) == false )
    {
        return NULL;
    }

    return bootState;
}

static void
RemoveActiveRequest(ContainerID playerID)
{
    BootPlayerState *bootState = NULL;

    stashIntRemovePointer(s_PendingPlayerBootRequests, (int)playerID, &bootState);
}

AUTO_STRUCT;
typedef struct BootInShardState
{
    ContainerID playerID;
    Login2InterShardDestination *returnDestination;
} BootInShardState;

// This callback runs on the remote shard.  It cleans up after the booting and returns status to the originating shard.
static void 
BootPlayerInShardCB(PlayerBooterResults *playerBooterResults, BootInShardState *bootInShardState)
{
    if (!playerBooterResults->bFinallySucceeded)
    {
        char *errorString = NULL;
        int i;

        // Generate an error string for the failures.
        estrPrintf(&errorString, "Something went wrong while the login server was booting playerID %u ", bootInShardState->playerID);

        for (i = 0; i < eaSize(&playerBooterResults->ppResults); i++)
        {
            estrConcatf(&errorString, ": %s ", playerBooterResults->ppResults[i]->pResultString);
        }

        // Send an alert.
        ErrorOrAlert("LOGIN_BOOT_FAILURE", "%s", errorString);

        // Free the error string.
        estrDestroy(&errorString);

        // Tell the database to attempt to fix the container ownerships, which is likely wrong.
        RemoteCommand_AttemptToFixContainerWithBrokenOwnership(GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, bootInShardState->playerID, "Player booting failed... trying to fix");
    }

    // Inform the calling shard whether the boot request succeeded.
    RemoteCommand_Intershard_aslLogin2_ReturnBootPlayerStatus(bootInShardState->returnDestination->shardName, bootInShardState->returnDestination->serverType, 
        bootInShardState->returnDestination->serverID, bootInShardState->playerID, playerBooterResults->bFinallySucceeded, bootInShardState->returnDestination->requestToken);

    // Clean up state.
    StructDestroy(parse_BootInShardState, bootInShardState);
}

// This is the command that runs on the remote shard to do the booting.
AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void
aslLogin2_BootPlayerInShard(ACMD_OWNABLE(Login2InterShardDestination) ppReturnDestination, U32 playerID, const char *message)
{
    BootInShardState *bootInShardState = StructCreate(parse_BootInShardState);

    // Take ownership of the return destination struct.
    bootInShardState->returnDestination = *ppReturnDestination;
    *ppReturnDestination = NULL;

    bootInShardState->playerID = playerID;

    BootPlayerWithBooter(playerID, BootPlayerInShardCB, bootInShardState, message);
}

// Handle the completion of the player booting operation.  Log any errors and call the callback to return results to the caller.
static void
BootPlayerComplete(BootPlayerState *bootState)
{
    // Remove from the table of active requests.
    RemoveActiveRequest(bootState->playerID);

    // Log success or failure
    if ( bootState->failed )
    {
        aslLogin2_Log("aslLogin2_BootPlayer failed for playerID %u.\n%s", bootState->playerID, bootState->errorString);
    }
    else
    {
        aslLogin2_Log("aslLogin2_BootPlayer succeeded for playerID %u.", bootState->playerID);
    }

    // Notify the caller that we are done.
    if ( bootState->cbFunc )
    {
        (* bootState->cbFunc)(!bootState->failed, bootState->userData);
    }

    // Clean up state.
    StructDestroy(parse_BootPlayerState, bootState);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void
aslLogin2_ReturnBootPlayerStatus(U32 playerID, bool success, U64 requestToken)
{
    BootPlayerState *bootState;

    bootState = GetActiveRequest(playerID);
    if ( bootState == NULL )
    {
        aslLogin2_Log("Received player booting status does not match an active request.  PlayerID = %u, RequestToken = %llu", playerID, requestToken);
        return;
    }

    if ( !success )
    {
        bootState->failed = true;

        estrConcatf(&bootState->errorString, "Remote shard boot request for playerID %u failed\n", playerID);
    }

    BootPlayerComplete(bootState);
}

// Boot a logged in player from a remote shard.
void
aslLogin2_BootPlayer(ContainerID playerID, const char *shardName, const char *message, BootPlayerCB cbFunc, void *userData)
{
    BootPlayerState *bootState = StructCreate(parse_BootPlayerState);
    Login2InterShardDestination myDestination = {0};

    bootState->playerID = playerID;
    bootState->shardName = allocAddString(shardName);
    bootState->timeStarted = timeSecondsSince2000();
    bootState->cbFunc = cbFunc;
    bootState->userData = userData;
    bootState->requestToken = Login2_GenerateRequestToken();

    if ( AddActiveRequest(bootState) == false )
    {
        bootState->failed = true;

        estrConcatf(&bootState->errorString, "Duplicate player boot request for playerID %u\n", playerID);

        BootPlayerComplete(bootState);
        return;
    }

    // Fill in the destination struct, which will tell the remote shard where to send the response.
    Login2_FillDestinationStruct(&myDestination, bootState->requestToken);

    // Send the command to the remote shard.
    RemoteCommand_Intershard_aslLogin2_BootPlayerInShard(shardName, GLOBALTYPE_LOGINSERVER, aslLogin2_GetRandomServerOfTypeInShard(shardName, GLOBALTYPE_LOGINSERVER), &myDestination, playerID, message);

    return;
}


//
// Do login server booting.
// 
void FailAllLoginsWithAccountID(U32 accountID);

AUTO_COMMAND;
void
aslLogin2_BootFromLoginServer(U32 accountID, U32 excludeServerID)
{
    if ( excludeServerID != objServerID() )
    {
        Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(accountID);
        if ( loginState )
        {
            aslLogin2_FailLogin(loginState, "Login2_DuplicateLogin");
        }
    }
}

// Boot the player from all login servers in the cluster.
void
aslLogin2_BootAccountFromAllLoginServers(ContainerID accountID, ContainerID excludeServerID)
{
    STRING_EARRAY shardNameList;
    int i;
    static char *cmdString = NULL;

    estrPrintf(&cmdString, "aslLogin2_BootFromLoginServer %u %u", accountID, excludeServerID);

    shardNameList = Login2_GetConnectedShardNamesInCluster();
    for ( i = eaSize(&shardNameList) - 1; i >= 0; i-- )
    {
        RemoteCommand_Intershard_SendCommandToAllServersOfType(shardNameList[i], GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_LOGINSERVER, GetAppGlobalID(), cmdString);
    }
}

#include "AutoGen/aslLogin2_Booting_c_ast.c"