/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_DeleteCharacter.h"
#include "aslLogin2_IntershardCommands.h"
#include "aslLogin2_Error.h"
#include "aslLogin2_Util.h"

#include "TransactionOutcomes.h"
#include "objTransactions.h"
#include "GlobalTypeEnum.h"
#include "StringCache.h"
#include "timing.h"

#include "AutoGen/aslLogin2_IntershardCommands_h_ast.h"
#include "AutoGen/aslLogin2_DeleteCharacter_c_ast.h"

#define DELETE_CHARACTER_COMMAND_NAME "Login2DeleteCharacter"

AUTO_STRUCT;
typedef struct DeleteCharacterInShardState
{
    Login2IntershardCommandRemoteState *commandRemoteState; AST(UNOWNED)
    ContainerID playerID;
} DeleteCharacterInShardState;

AUTO_STRUCT;
typedef struct DeleteCharacterState
{
    ContainerID playerID;
    STRING_POOLED shardName;                AST(POOL_STRING)

    U32 timeStarted;

    // Completion callback data.
    DeleteCharacterCB cbFunc;               NO_AST
    U64 userData;                           NO_AST
} DeleteCharacterState;

// This callback runs on the remote shard.  It cleans up after the character delete and returns status to the originating shard.
static void 
DeleteCharacterInShardCB(TransactionReturnVal *returnVal, DeleteCharacterInShardState *deleteCharacterInShardState)
{
    const char *errorString = NULL;
    bool success = true;
    Login2IntershardArgU32 returnStruct = {0};

    if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        success = false;
        errorString = "Delete character command failed in remote shard.";
    }

    returnStruct.value = deleteCharacterInShardState->playerID;

    aslLogin2_IntershardCommandRemoteComplete(success, &returnStruct, errorString, deleteCharacterInShardState->commandRemoteState);

    // Clean up state.
    StructDestroy(parse_DeleteCharacterInShardState, deleteCharacterInShardState);
}

bool
DeleteCharacterCommandHandler(const char *commandName, Login2IntershardArgU32 *argStruct, Login2IntershardCommandRemoteState *commandRemoteState)
{
    TransactionReturnVal *returnVal;
    DeleteCharacterInShardState *deleteCharacterInShardState = StructCreate(parse_DeleteCharacterInShardState);

    deleteCharacterInShardState->commandRemoteState = commandRemoteState;
    deleteCharacterInShardState->playerID = argStruct->value;

    // Request that the character be deleted by the ObjectDB.
    returnVal = objCreateManagedReturnVal(DeleteCharacterInShardCB, deleteCharacterInShardState);
    objRequestContainerDelete(returnVal, GLOBALTYPE_ENTITYPLAYER, deleteCharacterInShardState->playerID, GLOBALTYPE_OBJECTDB, 0);

    // Free argStruct.
    return true;
}

// Handle the completion of the character delete operation.  Log any errors and call the callback to return results to the caller.
static void
DeleteCharacterComplete(bool success, const char *errorString, DeleteCharacterState *deleteCharacterState)
{
    // Log success or failure
    if ( !success )
    {
        aslLogin2_Log("aslLogin2_DeleteCharacter failed for playerID %u.\n%s", deleteCharacterState->playerID, errorString);
    }
    else
    {
        aslLogin2_Log("aslLogin2_DeleteCharacter succeeded for playerID %u.", deleteCharacterState->playerID);
    }

    // Notify the caller that we are done.
    if ( deleteCharacterState->cbFunc )
    {
        (* deleteCharacterState->cbFunc)(success, deleteCharacterState->userData);
    }

    // Clean up state.
    StructDestroy(parse_DeleteCharacterState, deleteCharacterState);
}

static void
DeleteCharacterCommandCB(const char *commandName, bool success, const char *errorString, Login2IntershardArgU32 *returnStruct, DeleteCharacterState *deleteCharacterState)
{
    devassert(returnStruct->value == deleteCharacterState->playerID);
    DeleteCharacterComplete(success, errorString, deleteCharacterState);
}

void 
aslLogin2_DeleteCharacter(ContainerID playerID, const char *shardName, DeleteCharacterCB cbFunc, U64 userData)
{
    DeleteCharacterState *deleteCharacterState = StructCreate(parse_DeleteCharacterState);
    Login2IntershardArgU32 argStruct = {0};

    deleteCharacterState->playerID = playerID;
    deleteCharacterState->shardName = allocAddString(shardName);
    deleteCharacterState->timeStarted = timeSecondsSince2000();
    deleteCharacterState->cbFunc = cbFunc;
    deleteCharacterState->userData = userData;

    argStruct.value = playerID;

    if ( !aslLogin2_CallIntershardCommand(shardName, aslLogin2_GetRandomServerOfTypeInShard(shardName, GLOBALTYPE_LOGINSERVER), DELETE_CHARACTER_COMMAND_NAME, &argStruct, deleteCharacterState) )
    {
        DeleteCharacterComplete(true, "CallIntershardCommand failed", deleteCharacterState);
    }

    return;
}

AUTO_RUN;
void
aslLogin2_DeleteCharacterAutoRun(void)
{
    aslLogin2_RegisterIntershardCommand(DELETE_CHARACTER_COMMAND_NAME, parse_Login2IntershardArgU32, parse_Login2IntershardArgU32, DeleteCharacterCommandHandler, DeleteCharacterCommandCB);
}

#include "AutoGen/aslLogin2_DeleteCharacter_c_ast.c"